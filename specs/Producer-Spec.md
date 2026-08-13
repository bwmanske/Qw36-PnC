# Producer Specification

## 1. Overview

A multi-threaded Producer CLI application implemented in C++17, built with CMake.
The Producer reads a JSON configuration file, initializes a test-type-specific
plugin, generates work units on demand, and dispatches them over the network to
one or more Consumers. Each Consumer processes work units and returns results.
The Producer tracks all dispatched work units and removes them from its pending
list upon receiving successful results. On shutdown, the Producer writes a
checkpoint JSON so it can resume from the last completed state on restart. A
periodic backup is written approximately once per minute to protect against
abnormal termination.

The source tree is shared between Windows and Linux builds via `#ifdef`
conditionals. Two CMake targets are produced:

| # | Target             | Platform | CMake Target Name |
|---|--------------------|----------|-------------------|
| 1 | Producer CLI       | Both     | `producer`        |
| 2 | Consumer CLI       | Both     | `consumer`        |

The Producer depends on two static libraries: `common` (sockets, messages,
queue, checkpoint, SHA-256, signal handling) and `producer_lib` (producer
logic, work tracker, plugins).

## 2. Network

- **Medium**: Ethernet
- **IP version**: IPv4 only
- **Transport**: Configurable — TCP or UDP (CLI flag, default TCP)
- **Addressing**:
  - Producer binds to `0.0.0.0` on a configurable port to accept incoming
    Consumer connections (TCP) or to send datagrams (UDP).
  - Default local gateway: `192.168.1.1`
- **Multi-Consumer**: The Producer accepts and maintains simultaneous TCP
  connections from any number of Consumers. Each connection is handled on its
  own dedicated detached I/O thread.
- **TCP framing**: Length-prefixed JSON frames
  - 4-byte big-endian `uint32_t` frame length (payload bytes only, excludes header)
  - N-byte JSON payload (UTF-8)
- **UDP framing**: Same length-prefixed format per datagram. Each datagram
  carries exactly one JSON message. Datagram size must not exceed MTU
  (default 1500 bytes); messages exceeding the limit are dropped with a
  warning logged to stderr.
- **Socket recv timeout**: Accepted client sockets and file transfer sockets
  have a 10-second receive timeout (`set_recv_timeout(10000)`). When the
  timeout fires, `recv_data` returns `-2`, which propagates as a
  `"Socket recv timeout"` exception from `read_exact`.

### File Transfer Channel

A secondary TCP server listens on `port + 1` for file transfer requests.
Protocol:
1. Consumer connects and sends `0x01` followed by a null-terminated filename.
2. Producer resolves the file (checks CWD, config directory, source file,
   and config file paths in order).
3. Producer responds with 4-byte big-endian file size, followed by raw bytes.
4. If the file is not found, the producer sends size `0` and closes the connection.

## 3. Main JSON Config

The Producer requires a valid JSON configuration file at startup (provided
via `--file`). The config specifies the test type, plugin config file, source
data file, and operational limits.

- **Validation**: The Producer validates the file exists, is readable, and
  contains well-formed JSON before starting any worker threads. If validation
  fails, the Producer exits with code 1 and prints an error to stderr.
- **Path**: Provided via `--file` CLI flag. No default — the flag is mandatory.
- **`test_type` field is required** — if missing or empty, the Producer exits
  with code 1.

### Example Config

```json
{
  "test_type": "PWD",
  "config_file": "pwd_config.json",
  "source_file": "data.bin",
  "duration": 3600,
  "max_units": 0,
  "max_idle_seconds": 300
}
```

| Field              | Type   | Required | Description                                      |
|--------------------|--------|----------|--------------------------------------------------|
| `test_type`        | string | yes      | Plugin identifier: `"PWD"` or `"BENCH"`          |
| `config_file`      | string | no       | Path to plugin-specific config file              |
| `source_file`      | string | no       | Path to source data file (used by plugins)       |
| `duration`         | int    | no       | Max run duration in seconds (0 = no limit)       |
| `max_units`        | int    | no       | Max completed work units before shutdown (0 = no limit) |
| `max_idle_seconds` | int    | no       | Max idle time before shutdown (default: 300)     |

## 4. Plugin Architecture

The Producer uses a plugin-based architecture to support different test types.
Each plugin is a `TestPlugin` dispatch table (`include/producer/test_plugin.h`)
containing four `std::function` members:

| Function           | Signature                                                  | Description                                      |
|--------------------|------------------------------------------------------------|--------------------------------------------------|
| `startup`          | `void(config_path, resume_state)`                          | Reads plugin config file, restores state from checkpoint |
| `next_unit`        | `bool(WorkUnitMessage& out)`                               | Generates next work unit, populates `out.job`, returns `false` when exhausted |
| `checkpoint`       | `nlohmann::json()`                                         | Returns plugin-specific state for checkpoint merge |
| `exit_conditions`  | `bool()`                                                   | Returns `true` when the plugin wants to stop     |

The plugin is selected by the `test_type` field in the main JSON config.
Registration occurs in `producer.cpp::init_plugin()`.

### PWD Plugin (`PWD_plugin.cpp`)

Password permutation generator. Wraps the legacy C-style `PWD_NextUnit` class
which uses `#define` constants, raw arrays, and no `std::` prefixes.

- **Config file** (`pwd_config.json`):
  - `use_lower_alpha`, `use_upper_alpha`, `use_numeric`, `use_non_alpha` — character set flags
  - `max_password_length` — maximum password length (default: 10)
  - `starting_password` — optional starting password to resume from
- **Exit conditions**: Returns `true` when `g_password_found` or `g_file_error` is set.
- **Global state functions**:
  - `pwd_set_found(password)` — called when a consumer reports a found password
  - `pwd_set_file_error(msg)` — called when a consumer reports a file error
  - `pwd_get_found_password()` / `pwd_get_file_error()` — retrieve state for statistics

### BENCH Plugin (`BENCH_plugin.cpp`)

File chunk benchmark. Reads a source file in configurable chunks, base64-encodes
each chunk, and includes a SHA-256 hash for verification.

- **Config file**: `chunk_size` (default: 128 bytes)
- **Source file**: Set via `set_bench_source_file(path)` after plugin initialization
- **Exit conditions**: Returns `true` when the entire file has been chunked
- **Checkpoint state**: `offset`, `processed`, `seq`, `chunk_size`, `file_size`,
  `total_chunks`, `transactions_per_second`

### Adding a New Test Type

1. Create `XXX_plugin.h/cpp` in `include/producer/` and `src/producer/`
2. Implement `create_xxx_plugin()` returning a `TestPlugin`
3. Register in `producer.cpp::init_plugin()` with a new `else if` branch
4. Create corresponding `XXX_Handler.h/cpp` in consumer
5. Register handler in `consumer.cpp` constructor

## 5. Work Unit Tracking

The Producer maintains an in-memory work unit table via the `WorkTracker` class
(`include/producer/work_tracker.h`). Each entry tracks:

| Field            | Type     | Description                                      |
|------------------|----------|--------------------------------------------------|
| `work_unit_id`   | string   | Unique ID: `<producer_id>-<seq>`                 |
| `seq`            | int64    | Monotonically increasing sequence number         |
| `job`            | object   | The job object generated by the plugin           |
| `status`         | enum     | `Pending`, `Sent`, `Completed`, `Failed`         |
| `consumer_id`    | string   | ID of the Consumer that received this unit       |
| `sent_at`        | string   | ISO 8601 timestamp when dispatched               |
| `completed_at`   | string   | ISO 8601 timestamp when result received           |

### Lifecycle

1. **`Pending`** — Work unit generated by plugin, not yet dispatched.
2. **`Sent`** — Work unit has been sent to a Consumer. The Producer waits for a
   result message.
3. **`Completed`** — Consumer returned a successful result. The entry is
   logically removed from the active pending list (retained in completed
   history for statistics).
4. **`Failed`** — Consumer returned a failure result, or the Consumer
   connection was lost and the work unit was not acknowledged. The entry
   returns to `Pending` for re-dispatch.

### Re-dispatch

When a Consumer disconnects (detected by the client handler or the monitor
thread), the Producer calls `tracker_.get_failed_for_consumer(consumer_id)` to
mark all unacknowledged units as `Failed`, which makes them available for
re-dispatch to another Consumer (or the same Consumer if it reconnects).

## 6. JSON Message Format — Work Unit (Producer → Consumer)

Every work unit message sent by the Producer is a JSON object. The `test_type`
field identifies which plugin generated the unit.

```json
{
  "msg_type": "work_unit",
  "test_type": "PWD",
  "source_file": "data.bin",
  "source_hash": "abc123...",
  "work_unit_id": "prod-4821-42",
  "seq": 42,
  "timestamp": "2026-07-30T12:00:00.000Z",
  "producer_id": "prod-4821",
  "job": {
    "task": "PWD",
    "password": "abc",
    "indicies": [0, 1, 2],
    "text": "abc",
    "seq": 42
  }
}
```

| Field                | Type   | Required | Description                                      |
|----------------------|--------|----------|--------------------------------------------------|
| `msg_type`           | string | yes      | Always `"work_unit"`                             |
| `test_type`          | string | yes      | Plugin identifier (`"PWD"`, `"BENCH"`, etc.)     |
| `source_file`        | string | yes      | Path to the source data file                     |
| `source_hash`        | string | no       | SHA-256 hex digest of the source file            |
| `work_unit_id`       | string | yes      | Unique identifier for this work unit             |
| `seq`                | int64  | yes      | Monotonically increasing sequence number         |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |
| `producer_id`        | string | yes      | Unique identifier for this Producer instance     |
| `job`                | object | yes      | Plugin-specific work data                        |

## 7. JSON Message Format — Result (Consumer → Producer)

The Consumer sends a result message back to the Producer after processing
a work unit.

```json
{
  "msg_type": "result",
  "work_unit_id": "prod-4821-42",
  "seq": 42,
  "consumer_id": "cons-001",
  "status": "success",
  "result": {
    "output": "result_data",
    "duration_ms": 1250
  },
  "timestamp": "2026-07-30T12:00:01.250Z",
  "found_password": "abc123"
}
```

| Field                | Type   | Required | Description                                      |
|----------------------|--------|----------|--------------------------------------------------|
| `msg_type`           | string | yes      | Always `"result"`                                |
| `work_unit_id`       | string | yes      | Matches the `work_unit_id` from the work unit    |
| `seq`                | int64  | yes      | Matches the `seq` from the work unit             |
| `consumer_id`        | string | yes      | Unique identifier for this Consumer instance     |
| `status`             | string | yes      | `"success"` or `"failure"`                       |
| `result`             | object | yes      | Processing result data                           |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |
| `found_password`     | string | no       | Password found by consumer (PWD plugin)          |
| `file_error`         | string | no       | File error reported by consumer (PWD plugin)     |

When the Producer receives a result with `status: "success"`, it marks the
work unit as `Completed`. When the status is `"failure"`, the work unit is
marked `Failed` and returned to `Pending` for re-dispatch.

**Producer halt conditions**: If `found_password` is present, the Producer
calls `pwd_set_found()` and sets `running_ = false`. If `file_error` is
present, the Producer calls `pwd_set_file_error()` and sets `running_ = false`.

## 8. JSON Message Format — Work Request (Consumer → Producer)

When a Consumer thread pool has idle threads, the Consumer sends a work
request to ask for new work units.

```json
{
  "msg_type": "work_request",
  "consumer_id": "cons-001",
  "threads_available": 4,
  "timestamp": "2026-07-30T12:00:00.500Z"
}
```

| Field                | Type   | Required | Description                                      |
|----------------------|--------|----------|--------------------------------------------------|
| `msg_type`           | string | yes      | Always `"work_request"`                          |
| `consumer_id`        | string | yes      | Unique identifier for this Consumer instance     |
| `threads_available`  | int    | yes      | Number of idle threads requesting work           |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |

The Producer responds by sending up to `threads_available` work unit messages
to that Consumer connection. Each unit is generated by calling `plugin_.next_unit()`.

## 9. JSON Message Format — Heartbeat (Consumer → Producer)

Consumers send heartbeat messages every 5 seconds to indicate they are still
alive and processing work.

```json
{
  "msg_type": "heartbeat",
  "consumer_id": "cons-001",
  "timestamp": "2026-07-30T12:00:05.000Z"
}
```

| Field                | Type   | Required | Description                                      |
|----------------------|--------|----------|--------------------------------------------------|
| `msg_type`           | string | yes      | Always `"heartbeat"`                             |
| `consumer_id`        | string | yes      | Unique identifier for this Consumer instance     |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |

Heartbeat messages update the consumer's `last_activity` timestamp in the
Producer's connection tracker. No work units are dispatched in response.

## 10. Consumer Registration and Monitoring

### ConsumerInfo

Each connected consumer is tracked via a `ConsumerInfo` struct:

| Field            | Type                                      | Description                                      |
|------------------|-------------------------------------------|--------------------------------------------------|
| `socket`         | `Socket*`                                 | Pointer to the consumer's socket                 |
| `last_activity`  | `std::chrono::steady_clock::time_point`   | Timestamp of last received message               |
| `registered_at`  | `std::chrono::steady_clock::time_point`   | Timestamp of first registration                  |

### Registration Lifecycle

- **`register_consumer(consumer_id, socket)`** — Called on the first message
  from a consumer (work_request, result, or heartbeat). Logs consumer ID and
  total connected count.
- **`update_consumer_activity(consumer_id)`** — Called on every `work_request`,
  `result`, or `heartbeat` message. Updates `last_activity` timestamp.
- **`unregister_consumer(consumer_id)`** — Removes the consumer from the
  `connected_consumers_` map. Called on disconnect.

### Monitor Thread

A dedicated background thread (`monitor_connections()`) runs every 5 seconds:
1. Iterates over all connected consumers.
2. Identifies consumers idle for more than 30 seconds (`last_activity` older
   than 30s).
3. For each stale consumer:
   - Logs consumer ID and stale duration.
   - Closes the consumer's socket.
   - Reclaims work units via `tracker_.get_failed_for_consumer(id)`.
   - Logs count of reclaimed work units.
   - Removes consumer from the map.

### Disconnect Logging

When a consumer disconnects (either through the client handler detecting a
closed socket, or through the monitor thread detecting staleness), the Producer
logs:
```
[producer] Consumer disconnected: <consumer_id> (reclaimed <N> work units)
```

## 11. Checkpoint State File

On shutdown (graceful or abnormal), the Producer writes a checkpoint JSON
file so it can resume from where it left off. The checkpoint records the
plugin state, work unit counters, and tracking information.

### File Paths

- **Primary**: `<checkpoint-dir>/state.json`
- **Backup**: `<checkpoint-dir>/state.backup.json`

The backup is a copy of the previous primary, written before the new primary
is flushed. This ensures that if the write is interrupted, the backup holds
the last known good state.

### Write Schedule

- Every 60 seconds while the Producer is running (checkpoint thread).
- Immediately on graceful shutdown (`write_final_checkpoint()`).
- The backup file is updated alongside each primary write.

### Checkpoint Format

```json
{
  "producer_id": "prod-4821",
  "source_file": "data.bin",
  "permutation": "sequential",
  "permutation_seed": 0,
  "total_jobs": 1000,
  "last_completed_seq": 42,
  "last_completed_work_unit_id": "prod-4821-42",
  "completed_count": 42,
  "pending_count": 958,
  "failed_count": 0,
  "checkpoint_timestamp": "2026-07-30T12:05:00.000Z",
  "consumers_connected": [
    { "consumer_id": "cons-001", "pending_units": 3 }
  ],
  "plugin_state": {
    "seq": 42,
    "testPwdLen": 3,
    "charIndicies": [0, 1, 2, -1, -1, -1, -1, -1, -1, -1]
  }
}
```

| Field                          | Type   | Description                                      |
|--------------------------------|--------|--------------------------------------------------|
| `producer_id`                  | string | Unique identifier for this Producer instance     |
| `source_file`                  | string | Path to the source data file                     |
| `permutation`                  | string | Permutation mode (retained for compatibility)    |
| `permutation_seed`             | int64  | PRNG seed (if applicable)                        |
| `total_jobs`                   | int64  | Total work units generated                       |
| `last_completed_seq`           | int64  | Sequence number of the last completed work unit  |
| `last_completed_work_unit_id`  | string | Work unit ID of the last completed unit          |
| `completed_count`              | int64  | Number of completed work units                   |
| `pending_count`                | int64  | Number of pending (sent but not completed) units |
| `failed_count`                 | int64  | Number of failed work units                      |
| `checkpoint_timestamp`         | string | ISO 8601 UTC timestamp of this checkpoint        |
| `consumers_connected`          | array  | List of connected Consumers and their pending units |
| `plugin_state`                 | object | Plugin-specific state (optional, from `plugin_.checkpoint()`) |

### Resume Behavior

On startup, if `--resume` is passed and `<checkpoint-dir>/state.json` exists:
1. The Producer loads the checkpoint.
2. Extracts `plugin_state` and passes it to `plugin_.startup()`.
3. Sets `next_seq_` to `last_completed_seq + 1`.
4. The plugin restores its internal state from `plugin_state`.
5. Continues generating work units from the resumed position.

If `state.json` is corrupt but `state.backup.json` is valid, the Producer
uses the backup and logs a warning.

## 12. Threading Model

| Thread                    | Count | Purpose                                          |
|---------------------------|-------|--------------------------------------------------|
| Main thread               | 1     | Accept loop (TCP), manages lifecycle, signal handling |
| Dispatcher thread         | 1     | Plugin exit conditions, max units, duration checks |
| Checkpoint thread         | 1     | Periodic saves every 60s                         |
| File transfer thread      | 1     | Accepts connections on port+1                    |
| Monitor thread            | 1     | Stale connection detection every 5s              |
| Client handler thread     | 1 per connection | Detached thread per TCP client, reads frames, dispatches work, handles results |

### Thread Details

- **Main thread**: Parses CLI arguments, validates config file, loads checkpoint
  (if present), initializes plugin, starts background threads, runs the TCP
  accept loop. On each accept, spawns a detached thread for `handle_client()`.
- **Dispatcher thread**: Runs a loop checking `plugin_.exit_conditions()`,
  `max_units_` completion count, and `duration` limits. Sets `running_ = false`
  when any condition is met. Sleeps 100ms between checks.
- **Checkpoint thread**: Runs on a 60-second interval. Calls `plugin_.checkpoint()`
  to get plugin state, merges with tracker state, and saves via `CheckpointManager`.
- **File transfer thread**: Accepts connections on `port + 1`, spawns detached
  threads for each file transfer request.
- **Monitor thread**: Runs every 5 seconds, detects stale consumers (idle >30s),
  closes sockets, reclaims work units, and removes from consumer map.
- **Client handler threads** (detached): Read frames, dispatch to
  `handle_work_request()`, `handle_result()`, or heartbeat handler. On
  disconnect, reclaim work units and unregister consumer.

## 13. CLI Interface

```
producer --file PATH [--port PORT] [--transport tcp|udp] [--permutation MODE] [--seed N] [--duration SECONDS] [--gateway IP] [--checkpoint-dir DIR] [--resume] [--test-type TYPE]
```

| Flag               | Default            | Description                                      |
|--------------------|--------------------|--------------------------------------------------|
| `--file`           | *(required)*       | Path to the main JSON config file                |
| `--port`           | 9876               | Port to bind on                                  |
| `--transport`      | `tcp`              | Transport protocol (`tcp` or `udp`)              |
| `--permutation`    | `sequential`       | Job permutation mode (retained for compatibility) |
| `--seed`           | 0                  | PRNG seed for `random` permutation               |
| `--duration`       | 0                  | Run duration in seconds (0 = run until done)     |
| `--gateway`        | 192.168.1.1        | Default local gateway IPv4 address               |
| `--checkpoint-dir` | platform default   | Directory for checkpoint state files             |
| `--resume`         | false              | Resume from checkpoint if one exists             |
| `--test-type`      | *(from config)*    | Test type identifier (overrides config file)     |

The default checkpoint directory is determined by `get_data_directory()`:
- **Windows**: `%APPDATA%\Producer\`
- **Linux**: `~/.local/share/producer/`

The directory is created automatically if it does not exist.

## 14. Platform Conditionals

Shared source files use preprocessor guards for platform-specific code:

```cpp
#ifdef _WIN32
  // Windows-specific: WinSock2, thread naming, etc.
#else
  // Linux-specific: POSIX sockets, pthread_setname_np, etc.
#endif
```

Areas requiring conditionals:
- Socket initialization (`WSAStartup` vs. none)
- Signal handling (`signal` vs. `sigaction`)
- Thread naming (optional, platform-specific)
- File path separators (use `std::filesystem` where possible)
- `ssize_t` typedef (not defined on Windows — provided in `socket.h`)
- `NOMINMAX` must be defined before `<windows.h>` to prevent `min`/`max` macro conflicts

## 15. Graceful Shutdown

The Producer handles SIGINT (Ctrl+C) and SIGTERM:

1. Signal handler sets an atomic `stop_requested` flag.
2. Main thread's accept loop detects the flag and exits.
3. `shutdown()` is called:
   - Sets `running_`, `checkpoint_running_`, `file_transfer_running_`,
     `monitor_running_` to `false`.
   - Shuts down dispatch and result queues.
   - Joins dispatcher, checkpoint, file transfer, and monitor threads.
   - Writes final checkpoint.
   - Closes server socket.
   - Prints final statistics.

### Statistics

Printed on exit:
```
=== Producer Statistics ===
Test type:             PWD
Work units generated:  1500
Work units dispatched: 1500
Work units completed:  1450
Work units failed:     10
Work units pending:    40
Password found:        abc123
=========================
```

For PWD test type, additionally prints:
- `Password found` (if `pwd_get_found_password()` is non-empty)
- `File error` (if `pwd_get_file_error()` is non-empty)

## 16. Error Handling

| Condition                              | Behavior                                          |
|----------------------------------------|---------------------------------------------------|
| `--file` not provided                  | Exit code 1, usage message to stderr              |
| Config file does not exist             | Exit code 1, error to stderr                      |
| Config file is not valid JSON          | Exit code 1, error to stderr                      |
| `test_type` missing or empty           | Exit code 1, error to stderr                      |
| Unknown `test_type`                    | Exit code 1, error to stderr                      |
| Port already in use                    | Exit code 2, error to stderr                      |
| Checkpoint file corrupt, backup valid  | Use backup, log warning                           |
| Both checkpoint files corrupt          | Log error, start fresh from beginning             |
| Consumer disconnects (TCP)             | Mark unacknowledged units as `Failed` → `Pending` |
| Consumer stale (>30s idle)             | Close socket, reclaim work units, log count       |
| UDP datagram exceeds MTU               | Drop message, log warning to stderr               |
| Checkpoint write fails                 | Log error to stderr, continue running             |
| Socket recv timeout (10s)              | Throw "Socket recv timeout" exception             |
| BENCH source file not found            | Exit code 1, error to stderr                      |

## 17. Acceptance Criteria

- [ ] `cmake -B build -DBUILD_TESTS=ON && cmake --build build --config Release` succeeds on Windows (MSVC)
- [ ] `cmake -B build -DBUILD_TESTS=ON && cmake --build build` succeeds on Linux (GCC/Clang)
- [ ] Producer exits with code 1 when `--file` is missing or invalid
- [ ] Producer reads and validates the JSON config file before starting workers
- [ ] Producer exits with code 1 when `test_type` is missing or unknown
- [ ] PWD plugin generates password permutations correctly
- [ ] BENCH plugin generates file chunks with correct offset and hash
- [ ] Every sent work unit contains `test_type`, `source_file`, and `work_unit_id`
- [ ] Producer tracks all work units in the WorkTracker
- [ ] Producer marks work unit as `Completed` on successful result
- [ ] Producer marks work unit as `Failed` on failure result
- [ ] Producer halts (`running_ = false`) when `found_password` is received
- [ ] Producer halts (`running_ = false`) when `file_error` is received
- [ ] Producer accepts multiple simultaneous Consumer connections
- [ ] Producer responds to `work_request` messages with available work units
- [ ] Producer accepts `heartbeat` messages and updates consumer activity
- [ ] Monitor thread detects stale consumers after 30s of inactivity
- [ ] Monitor thread closes stale consumer socket and reclaims work units
- [ ] Disconnect logging includes consumer ID and reclaimed work unit count
- [ ] Checkpoint file is written every 60 seconds and on shutdown
- [ ] Backup checkpoint file exists and is a valid prior state
- [ ] Checkpoint includes `plugin_state` from `plugin_.checkpoint()`
- [ ] `--resume` restores plugin state and continues from last completed seq
- [ ] File transfer server accepts on `port + 1` and serves files correctly
- [ ] File transfer responds with size `0` for not-found files
- [ ] Socket recv timeout (10s) throws "Socket recv timeout" on accepted sockets
- [ ] Ctrl+C shuts down cleanly within 5 seconds with checkpoint written
- [ ] Final statistics include test type, generated, dispatched, completed, failed, pending counts
- [ ] PWD statistics include found password or file error when applicable
- [ ] Default checkpoint directory is `%APPDATA%\Producer\` on Windows
- [ ] Default checkpoint directory is `~/.local/share/producer/` on Linux
- [ ] All unit tests pass
