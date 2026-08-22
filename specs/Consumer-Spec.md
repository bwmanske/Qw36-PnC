# Consumer Specification

## 1. Overview

A multi-threaded Consumer CLI application implemented in C++17, built with CMake.
The Consumer connects to a Producer over the network, receives work units,
ensures the source file is available locally, processes work units through a
pluggable `IWorkUnitHandler`, and returns results to the Producer. Idle threads
in the pool request new work units from the Producer.

The source tree is shared between Windows and Linux builds via `#ifdef`
conditionals. A single CMake target is produced:

| # | Target             | Platform | CMake Target Name |
|---|--------------------|----------|-------------------|
| 1 | Consumer CLI       | Both     | `consumer`        |

## 2. Network

- **Medium**: Ethernet
- **IP version**: IPv4 only
- **Transport**: Configurable — TCP or UDP (CLI flag, default TCP)
- **Addressing**:
  - Consumer connects to the Producer at a configurable host and port.
  - Default local gateway: `192.168.1.1`
  - When the Producer resides on the same machine, the Consumer connects to
    `127.0.0.1` (localhost). The `--local` flag forces localhost binding.
- **TCP framing**: Length-prefixed JSON frames
  - 4-byte big-endian `uint32_t` frame length (payload bytes only, excludes header)
  - N-byte JSON payload (UTF-8)
- **UDP framing**: Same length-prefixed format per datagram. The Consumer
  binds to the configured port and receives datagrams from any Producer on
  the local subnet.
- **Socket recv timeout**:
  - Control channel: 10 seconds (`set_recv_timeout(10000)`)
  - File transfer channel: 30 seconds (`set_recv_timeout(30000)`)
  - A timeout throws `"Socket recv timeout"`, breaking the receive loop.

## 3. Source File Download

Every work unit message from the Producer contains a `source_file` field with
the absolute path to the source file on the Producer's machine. The Consumer must
ensure this file is available locally before processing work units.

### Download Logic

1. On receiving the first work unit, the Consumer extracts `source_file`.
2. The Consumer checks whether a file with the same name exists in the local
   working directory (or the `--file-dir` path, if set).
3. **If the file exists locally**: The Consumer verifies the file by comparing
   its SHA-256 hash against the `source_hash` field in the message (if
   provided). If the hash matches, the file is used as-is.
4. **If the file does not exist or the hash does not match**: The Consumer
   requests the file from the Producer over a secondary TCP connection on
   port `--port + 1` (the file transfer port).
5. The Producer serves the file as a raw byte stream with a 4-byte big-endian
   length prefix followed by the file contents.
6. The Consumer writes the received bytes to the local file path, verifies
   the SHA-256 hash, and proceeds.

### File Transfer Protocol

```
[4-byte big-endian file size][N-byte file contents]
```

- The Consumer sends a single-byte request code `0x01` followed by the
  null-terminated filename on the file transfer port.
- The Producer responds with the length-prefixed file contents.
- If the Producer does not have the file, it responds with a 0-byte payload
  and the Consumer logs an error and exits with code 3.

### Fallback

If the file transfer fails (connection refused, timeout, hash mismatch), the
Consumer logs the error to stderr and exits with code 3. The Consumer will
not process work units without a valid local copy of the source file.

## 4. JSON Message Format — Work Unit (Inbound, Producer → Consumer)

```json
{
  "msg_type": "work_unit",
  "test_type": "PWD",
  "source_file": "/path/to/archive.zip",
  "permutation": "sequential",
  "permutation_seed": 12345,
  "work_unit_id": "prod-001-42",
  "seq": 42,
  "timestamp": "2026-07-30T12:00:00.000Z",
  "producer_id": "prod-001",
  "job": {
    "password": "abc123"
  },
  "source_hash": "a1b2c3..."
}
```

| Field                | Type   | Required | Description                                      |
|----------------------|--------|----------|--------------------------------------------------|
| `msg_type`           | string | yes      | Always `"work_unit"`                             |
| `test_type`          | string | yes      | Test plugin type (`PWD`, `BENCH`, etc.)          |
| `source_file`        | string | yes      | Absolute path to the source file on the Producer |
| `permutation`        | string | yes      | Permutation mode applied by the Producer         |
| `permutation_seed`   | int64  | no       | PRNG seed (present only when `permutation` is `random`) |
| `work_unit_id`       | string | yes      | Unique identifier for this work unit             |
| `seq`                | int64  | yes      | Monotonically increasing sequence number         |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |
| `producer_id`        | string | yes      | Unique identifier for the Producer instance      |
| `job`                | object | yes      | Handler-specific payload                         |
| `source_hash`        | string | no       | SHA-256 hex digest of the source file            |

### Validation

The Consumer validates every inbound work unit:
- `msg_type` is `"work_unit"`.
- All required fields are present and non-empty.
- `work_unit_id` has not been processed already (duplicate detection via LRU cache).
- `job` is a valid JSON object.
- Messages failing validation are logged to stderr and discarded. No result
  is sent back for invalid messages.

## 5. JSON Message Format — Result (Outbound, Consumer → Producer)

After processing a work unit, the Consumer sends a result back:

```json
{
  "msg_type": "result",
  "work_unit_id": "prod-001-42",
  "seq": 42,
  "consumer_id": "cons-001",
  "status": "success",
  "result": {
    "password": "abc123",
    "output": "password_valid",
    "duration_ms": 125
  },
  "found_password": "abc123",
  "timestamp": "2026-07-30T12:00:01.250Z"
}
```

| Field                | Type   | Required | Description                                      |
|----------------------|--------|----------|--------------------------------------------------|
| `msg_type`           | string | yes      | Always `"result"`                                |
| `work_unit_id`       | string | yes      | Matches the `work_unit_id` from the work unit    |
| `seq`                | int64  | yes      | Matches the `seq` from the work unit             |
| `consumer_id`        | string | yes      | Unique identifier for this Consumer instance     |
| `status`             | string | yes      | `"success"` or `"failure"`                       |
| `result`             | object | yes      | Handler-specific result data                     |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |
| `found_password`     | string | no       | Set when PWD handler finds the correct password. Producer halts on receipt. |
| `file_error`         | string | no       | Set when PWD handler encounters a file error. Producer halts on receipt. |

### Duplicate Work Unit Response

When a duplicate `work_unit_id` is detected, the Consumer sends:

```json
{
  "msg_type": "result",
  "work_unit_id": "prod-001-42",
  "seq": 42,
  "consumer_id": "cons-001",
  "status": "success",
  "result": {
    "note": "duplicate, already processed"
  },
  "timestamp": "2026-07-30T12:00:01.500Z"
}
```

## 6. JSON Message Format — Work Request (Outbound, Consumer → Producer)

When idle threads are available, the Consumer requests more work:

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

### Request Timing

- On initial connection, the Consumer sends a work request for all available
  threads in the pool.
- After each work unit completes processing, the finishing thread signals
  the coordinator. If the internal work queue is empty, the coordinator
  sends a new work request for the number of idle threads.
- To avoid flooding, work requests are throttled: maximum one request per
  50ms per Consumer connection.
- **Idle safety net**: the main loop also sends a work request whenever the
  pool is fully idle (`active_count() == 0 && queue_empty()`), guarding
  against the throttle dropping the idle-callback request after a burst.

### Idle Timeout (`--timeout`)

- The Consumer tracks the time of the last frame received from the Producer
  (`last_comm_time_`, updated in the receiver loop).
- If `--timeout SEC` is set and no communication has occurred for that many
  seconds, the main loop shuts the Consumer down (exit code 0).

## 7. JSON Message Format — Heartbeat (Outbound, Consumer → Producer)

The Consumer sends a heartbeat every 5 seconds on a dedicated thread:

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

### Heartbeat Behavior

- Sent every 5 seconds via `heartbeat_thread_`.
- Keeps the connection alive for the Producer's stale consumer detection.
- On the Producer side, the consumer is registered on first message
  (work_request, result, or heartbeat). The Producer tracks `consumer_id`,
  socket pointer, `last_activity`, and `registered_at`.
- If heartbeat send fails, the thread logs the error and exits.

## 8. Handler Architecture

Work unit processing is delegated to pluggable handlers implementing the
`IWorkUnitHandler` interface (`include/consumer/work_unit_handler.h`).

### IWorkUnitHandler Interface

```cpp
class IWorkUnitHandler {
public:
    virtual ~IWorkUnitHandler() = default;
    virtual std::string type() const = 0;
    virtual ResultMessage handle(const WorkUnitMessage& work) = 0;
    virtual void configure(const std::string& config_path) {}  // Default no-op
};
```

- `type()` — Returns the handler type string (e.g., `"PWD"`, `"BENCH"`, `"ECHO"`).
- `handle(work)` — Processes a work unit and returns a `ResultMessage`.
- `configure(config_path)` — Optional handler configuration (default no-op).
  Called after handler instantiation if `--handler-config` is provided.

### Handler Selection

The handler is selected via the `--handler TYPE` CLI flag. The Consumer
constructor instantiates the appropriate handler:

| Flag Value | Handler Class    | Header                              |
|------------|------------------|-------------------------------------|
| `PWD`      | `PWD_Handler`    | `include/consumer/PWD_Handler.h`    |
| `BENCH`    | `BENCH_Handler`  | `include/consumer/BENCH_Handler.h`  |
| `ECHO`     | `ECHO_Handler`   | `include/consumer/ECHO_Handler.h`   |

If no handler is specified, work units are processed with a default fallback
that returns `"failure"` with `"no handler registered"`.

### PWD_Handler

Validates a candidate password against an archive file using `ArchiveValidator`.

- Reads `password` from `work.job`.
- Calls `ArchiveValidator::validate(archive_path, password)`.
- On valid password: returns `"success"` with `found_password` set to the password.
- On wrong password: returns `"failure"` with `"wrong_password"` in result.
- On file error: sets global `g_pwd_file_error` flag, returns `"failure"` with
  `file_error` set. Subsequent calls short-circuit with the same error.
- The Producer halts processing when it receives a result with either
  `found_password` or `file_error` set.

### BENCH_Handler

Compares file chunks using base64 decode + SHA-256 verification.

- Reads `offset`, `chunk_size`, `data` (base64), and `hash` from `work.job`.
- Decodes the base64 `data` field to get expected bytes.
- Reads the corresponding chunk from the local source file at `offset`.
- Computes SHA-256 of the local chunk, compares against `expected_hash`.
- Returns `"success"` with `match`, `offset`, `chunk_size`, `expected_hash`,
  `actual_hash`, and `duration_ms` in the result.

### ECHO_Handler

Verifies payload hash and supports configurable power-law distributed delay.

- Reads `payload` and `hash` from `work.job`.
- Computes SHA-256 of the payload, compares against `hash`.
- Applies a configurable delay: `delay = max_delay * (1 - r^1.64)` where
  `r ~ Uniform(0,1)`. Configured via `echo_config.json` with `max_delay_sec`.
- `configure(config_path)` reads `echo_config.json` for `max_delay_sec`.
- Returns `"success"` with `match`, `hash`, `delay_ms`, and `duration_ms` in the result.

## 9. Result Sink Architecture

Results are optionally persisted through pluggable sinks implementing the
`IResultSink` interface (`include/consumer/result_sink.h`).

### IResultSink Interface

```cpp
class IResultSink {
public:
    virtual ~IResultSink() = default;
    virtual std::string type() const = 0;
    virtual void on_result(const ResultMessage& result) = 0;
    virtual bool should_stop() const = 0;
    virtual nlohmann::json summary() const = 0;
};
```

- `type()` — Returns the sink type string (e.g., `"file"`).
- `on_result(result)` — Called for each processed result.
- `should_stop()` — Returns `true` if processing should halt.
- `summary()` — Returns a JSON object with sink statistics.

### FileResultSink

The default `IResultSink` implementation (`include/consumer/file_result_sink.h`).

- **Default behavior**: Created automatically when no `--result-file` is specified.
  Default path: `%APPDATA%\Producer\results_<consumer_id>.jsonl` (Windows) or
  `~/.local/share/producer/results_<consumer_id>.jsonl` (Linux).
- When `--result-file FILE` is specified, uses the given path instead.
- Opens the file in append mode (`std::ios::app`).
- Writes one JSON line per result (JSON Lines format).
- Each line includes the full result plus a `sink_stats` object with running
  totals: `total`, `successes`, `failures`.
- Thread-safe via `std::mutex`.
- **Stopping criteria**: Constructor accepts `max_failures` and `max_duration_sec`
  (both default to 0 = disabled). `should_stop()` returns `true` when
  `failures_ >= max_failures_` or elapsed time >= `max_duration_sec_`.
- Configured via `--max-failures N` and `--max-duration SEC` CLI flags.

Example JSON line:

```json
{"msg_type":"result","work_unit_id":"prod-001-42","seq":42,"consumer_id":"cons-001","status":"success","result":{"password":"abc123","output":"password_valid","duration_ms":125},"found_password":"abc123","timestamp":"2026-07-30T12:00:01.250Z","sink_stats":{"total":1,"successes":1,"failures":0}}
```

## 10. Duplicate Detection

The Consumer maintains an LRU cache of completed `work_unit_id`s to detect
and handle duplicate work units.

- **Data structures**: `std::list<std::string>` (LRU order) +
  `std::unordered_set<std::string>` (O(1) lookup).
- **Capacity**: 3000 entries maximum (`kMaxCompletedIds`).
- **Eviction**: When capacity is exceeded, the oldest entry (list back) is
  removed from both the list and the set.
- **Behavior**: On receiving a duplicate, the Consumer immediately sends a
  `"success"` result with `{"note": "duplicate, already processed"}` and
  does not submit the work unit to the thread pool.
- **Thread safety**: Protected by `completed_ids_mutex_`.
- **Population**: IDs are added to the cache when a `"success"` result is
  produced by the thread pool (via the `result_callback_`).

## 11. Thread Pool

The Consumer uses a thread pool to process work units concurrently.

- **Default size**: 1 thread per logical core (`std::thread::hardware_concurrency()`).
  If `hardware_concurrency()` returns 0, default to 4.
- **Configurable**: The `--threads` flag overrides the default.
- **Work distribution**:
  - The receiver thread pushes incoming work units into an internal bounded
    queue (`BoundedQueue<WorkUnitMessage>`, capacity 4096).
  - Pool threads pop work units from the queue and dispatch them through
    the `IWorkUnitHandler`.
  - When a pool thread finishes and the queue is empty, it signals the
    coordinator via `idle_callback_`.
  - The coordinator sends a `work_request` to the Producer (throttled).

### Thread Pool Architecture

```
[Network Socket]
       │
       ▼
 [Receiver Thread] ───push───► [Bounded Work Queue (4096)]
                                            │
                                    ┌───────┼───────┐
                                    ▼       ▼       ▼
                               [Thread] [Thread] [Thread] ... (pool)
                                    │       │       │
                                    ▼       ▼       ▼
                              [IWorkUnitHandler]
                                    │
                                    ▼
                              [ResultCallback]
                                    │
                          ┌─────────┼──────────┐
                          ▼         ▼          ▼
                    [mark_completed] [sink_->on_result] [send_result]
                          │
                          ▼
                   [Duplicate LRU Cache]
```

### Execution Order

1. Main thread starts, connects to Producer, sends initial `work_request`.
2. Receiver thread starts, begins reading frames from the socket.
3. Heartbeat thread starts, sends heartbeat every 5 seconds.
4. On first work unit, receiver signals main thread with `source_file` path.
5. Main thread (within receiver) checks local file availability, downloads if needed.
6. Once the source file is verified locally, the thread pool is started with
   the configured handler.
7. Pool threads begin popping work units from the queue.
8. Each thread dispatches through `IWorkUnitHandler::handle()`.
9. The result callback marks the ID as completed, writes to the result sink,
   and sends the result to the Producer.
10. If the queue is empty, the idle callback triggers a `work_request`.

### Threading Model Summary

| Thread              | Responsibility                                      |
|---------------------|-----------------------------------------------------|
| Main                | Connect, lifecycle management, signal handling       |
| Receiver            | Reads frames, validates, pushes to work queue       |
| Heartbeat           | Sends heartbeat every 5s                            |
| Pool (N threads)    | Dispatches work through `IWorkUnitHandler`           |
| File transfer       | On-demand during source file download               |

## 12. CLI Interface

```
consumer [--host HOST] [--port PORT] [--transport tcp|udp] [--threads N] [--file-dir DIR] [--max-messages N] [--local] [--gateway IP] [--consumer-id ID] [--handler TYPE] [--handler-config FILE] [--result-file FILE] [--max-failures N] [--max-duration SEC] [--timeout SEC]
```

| Flag           | Default            | Description                                      |
|----------------|--------------------|--------------------------------------------------|
| `--host`       | 127.0.0.1          | Producer host IPv4 address to connect to         |
| `--port`       | 9876               | Producer port to connect to                      |
| `--transport`  | `tcp`              | Transport protocol (`tcp` or `udp`)              |
| `--threads`    | (cores)            | Thread pool size (default: 1 per core)           |
| `--file-dir`   | `./`               | Local directory for downloaded source files      |
| `--max-messages`| 0                 | Stop after N completed work units (0 = infinite) |
| `--local`      | false              | Force localhost connection (127.0.0.1)           |
| `--gateway`    | 192.168.1.1        | Default local gateway IPv4 address               |
| `--consumer-id`| (auto-generated)   | Unique Consumer ID (default: `cons-<hostname>-<hash>`) |
| `--handler`    | (none)             | Work unit handler type (`PWD`, `BENCH`, `ECHO`)  |
| `--handler-config`| (none)          | Path to handler-specific config file             |
| `--result-file`| (auto-generated)   | Write results to JSON lines file                 |
| `--max-failures`| 0                 | Stop after N failure results (0 = no limit)      |
| `--max-duration`| 0                 | Stop after N seconds (0 = no limit)              |
| `--timeout`    | 0                  | Close after N seconds with no producer communication (0 = no limit) |

## 13. Platform Conditionals

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
- Hostname retrieval (`GetComputerNameEx` vs. `gethostname`)
- `ssize_t` typedef (not defined on Windows — provided in `socket.h`)

## 14. Graceful Shutdown

The Consumer handles SIGINT (Ctrl+C) and SIGTERM:

1. Signal handler sets an atomic `stop_requested` flag via `SignalHandler`.
2. Main thread detects the flag in its polling loop (500ms interval).
3. `running_` is set to `false`.
4. Thread pool is shut down; worker threads finish current work and exit.
5. `drain_pending()` collects queued + active work units from the pool.
6. For each pending unit, a `"failure"` result with `error: "consumer shutdown"` is sent.
7. Work queue is shut down, blocking the receiver thread.
6. Receiver and heartbeat threads are joined.
7. Socket is closed, resources are released.
8. Final statistics are printed to stdout.

### Statistics

Printed on exit:

```
=== Consumer Statistics ===
Work units received:     150
Work units completed:    148
Work units failed:       2
Work units discarded:    0
Consumer ID:             cons-myhost-4821
Sequence range:          1 - 150
Result sink:             file file=results.jsonl total=150 ok=148 fail=2
===========================
```

- Total work units received
- Total work units completed successfully (from pool)
- Total work units failed (from pool)
- Total work units discarded (validation failures)
- Consumer ID
- Sequence number range (first seq, last seq)
- Result sink summary (type, file, total, ok, fail) — only if a sink is configured

## 15. Error Handling

| Condition                                  | Behavior                                          |
|--------------------------------------------|---------------------------------------------------|
| Cannot connect to Producer (TCP)           | Retry every 5 seconds, up to 30 attempts, then exit code 2 |
| Producer disconnects (TCP)                 | Receiver loop breaks, shutdown proceeds            |
| Socket recv timeout (10s control)          | Throws `"Socket recv timeout"`, breaks receive loop |
| Socket recv timeout (30s file transfer)    | Throws `"Socket recv timeout"`, exit code 3        |
| Source file download fails                 | Exit code 3, error to stderr                      |
| Source file hash mismatch                  | Re-downloads the file                             |
| Malformed JSON work unit                   | Log to stderr, discard, no result sent            |
| Duplicate `work_unit_id`                   | Send `"success"` result with `"duplicate, already processed"` |
| Result send fails                          | Log error to stderr                               |
| Work unit processing throws exception      | Caught by handler, returns `"failure"` result     |
| No handler registered                      | Returns `"failure"` with `"no handler registered"` |
| PWD file error                             | Sets `file_error`, global flag prevents further attempts |

## 16. Acceptance Criteria

- [ ] `cmake -B build && cmake --build build` succeeds on Windows (MSVC)
- [ ] `cmake -B build && cmake --build build` succeeds on Linux (GCC/Clang)
- [ ] Consumer connects to Producer over TCP and receives work units
- [ ] Consumer receives work units over UDP when `--transport udp` is set
- [ ] Consumer downloads source file when it does not exist locally
- [ ] Consumer skips download when source file exists and hash matches
- [ ] Consumer exits with code 3 when source file download fails
- [ ] Thread pool defaults to 1 thread per core
- [ ] `--threads N` overrides the default pool size
- [ ] Consumer sends `work_request` on connection and when threads are idle
- [ ] Consumer sends `result` back to Producer after each work unit
- [ ] All required JSON fields are validated on every work unit
- [ ] Duplicate `work_unit_id` messages are detected via LRU cache (3000 max)
- [ ] Duplicate work units receive `"success"` with `"duplicate, already processed"`
- [ ] `--max-messages N` stops processing after N received units
- [ ] `--timeout SEC` shuts the Consumer down after N seconds without producer communication
- [ ] `--local` flag connects to 127.0.0.1
- [ ] `--handler PWD` instantiates `PWD_Handler`
- [ ] `--handler BENCH` instantiates `BENCH_Handler`
- [ ] PWD_Handler sets `found_password` on correct password
- [ ] PWD_Handler sets `file_error` on archive file error
- [ ] BENCH_Handler decodes base64, reads file chunk, verifies SHA-256
- [ ] `--result-file FILE` creates `FileResultSink` writing JSON lines
- [ ] Each JSON line includes `sink_stats` with running totals
- [ ] Heartbeat sent every 5 seconds via dedicated thread
- [ ] Work request throttled to max 1 per 50ms per connection
- [ ] Socket recv timeout of 10s on control channel
- [ ] Socket recv timeout of 30s on file transfer channel
- [ ] On shutdown, final statistics are printed to stdout
- [ ] Ctrl+C shuts down cleanly within 5 seconds
- [ ] All unit tests pass
