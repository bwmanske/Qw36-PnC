# Consumer Specification

## 1. Overview

A multi-threaded Consumer CLI application implemented in C++17, built with CMake.
The Consumer connects to a Producer over the network, receives work units,
ensures the source job file is available locally, processes jobs using a thread
pool, and returns results to the Producer. Idle threads in the pool request
new work units from the Producer.

The source tree is shared between Windows and Linux builds via `#ifdef`
conditionals. Two CMake targets are produced:

| # | Target             | Platform | CMake Target Name |
|---|--------------------|----------|-------------------|
| 3 | Consumer CLI       | Windows  | `consumer_win`    |
| 4 | Consumer CLI       | Linux    | `consumer_linux`  |

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

## 3. Source File Download

Every work unit message from the Producer contains a `source_file` field with
the absolute path to the job file on the Producer's machine. The Consumer must
ensure this file is available locally before processing jobs.

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
not process jobs without a valid local copy of the source file.

## 4. JSON Message Format — Work Unit (Inbound, Producer → Consumer)

```json
{
  "msg_type": "work_unit",
  "source_file": "/path/to/jobs.json",
  "permutation": "random",
  "permutation_seed": 12345,
  "work_unit_id": "prod-001-42",
  "seq": 42,
  "timestamp": "2026-07-30T12:00:00.000Z",
  "producer_id": "prod-001",
  "job": {
    "job_id": 1,
    "task": "render",
    "params": { "width": 1920, "height": 1080 }
  }
}
```

| Field                | Type   | Required | Description                                      |
|----------------------|--------|----------|--------------------------------------------------|
| `msg_type`           | string | yes      | Always `"work_unit"`                             |
| `source_file`        | string | yes      | Absolute path to the job file on the Producer    |
| `permutation`        | string | yes      | Permutation mode applied by the Producer         |
| `permutation_seed`   | int64  | no       | PRNG seed (present only when `permutation` is `random`) |
| `work_unit_id`       | string | yes      | Unique identifier for this work unit             |
| `seq`                | int64  | yes      | Monotonically increasing sequence number         |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |
| `producer_id`        | string | yes      | Unique identifier for the Producer instance      |
| `job`                | object | yes      | The job object to process                        |
| `source_hash`        | string | no       | SHA-256 hex digest of the source file            |

### Validation

The Consumer validates every inbound work unit:
- `msg_type` is `"work_unit"`.
- All required fields are present and non-empty.
- `work_unit_id` has not been processed already (duplicate detection).
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
    "output": "rendered_frame_001.png",
    "duration_ms": 1250
  },
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
| `result`             | object | yes      | Processing result data                           |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |

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

## 7. Thread Pool

The Consumer uses a thread pool to process work units concurrently.

- **Default size**: 1 thread per logical core (`std::thread::hardware_concurrency()`).
  If `hardware_concurrency()` returns 0, default to 4.
- **Configurable**: The `--threads` flag overrides the default.
- **Work distribution**:
  - The receiver thread pushes incoming work units into an internal bounded
    queue.
  - Pool threads pop work units from the queue and process them.
  - When a pool thread finishes and the queue is empty, it signals the
    coordinator that it is idle.
  - The coordinator batches idle thread counts and sends a `work_request`
    to the Producer.

### Thread Pool Architecture

```
[Network Socket]
       │
       ▼
 [Receiver Thread] ───push───► [Bounded Work Queue]
                                            │
                                    ┌───────┼───────┐
                                    ▼       ▼       ▼
                               [Thread] [Thread] [Thread] ... (pool)
                                    │       │       │
                                    ▼       ▼       ▼
                              [Process Job] → [Send Result] → [Signal Idle]
```

### Execution Order

1. Main thread starts, connects to Producer, sends initial `work_request`.
2. Receiver thread starts, begins reading frames from the socket.
3. On first work unit, receiver signals main thread with `source_file` path.
4. Main thread checks local file availability, downloads if needed.
5. Once the source file is verified locally, the thread pool is started.
6. Pool threads begin popping work units from the queue.
7. After each job completes, the thread sends the result back and signals
   the coordinator. If the queue is empty, the coordinator requests more
   work from the Producer.

## 8. CLI Interface

```
consumer [--host HOST] [--port PORT] [--transport tcp|udp] [--threads N] [--file-dir DIR] [--max-messages N] [--local] [--gateway IP] [--consumer-id ID]
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
| `--consumer-id`| (auto-generated)   | Unique Consumer ID (default: `cons-<hostname>-<pid>`) |

## 9. Job Processing

The Consumer processes each job from the `job` field in the work unit. The
processing behavior for Phase 1 is:

1. Parse the `job` object.
2. Log the job details to stdout (work_unit_id, job_id, task, params).
3. Simulate processing with a short delay (configurable via `--simulate-ms`,
   default 10ms) to allow observing concurrency.
4. Build the result object with `status: "success"`.
5. Send the result message back to the Producer.
6. Signal the coordinator that the thread is idle.

Phase 2 will introduce actual task execution (rendering, encoding, etc.).

## 10. Platform Conditionals

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

## 11. Graceful Shutdown

The Consumer handles SIGINT (Ctrl+C) and SIGTERM:

1. Signal handler sets an atomic `stop_requested` flag.
2. Main thread detects the flag.
3. Receiver thread stops reading, signals the work queue to shut down.
4. Pool threads finish their current work units, send results, and exit.
5. Any work units that were popped but not yet completed are sent back as
   `"failure"` results so the Producer can re-dispatch them.
6. Socket is closed, resources are released.
7. Final statistics are printed to stdout.

### Statistics

Printed on exit:
- Total work units received
- Total work units completed successfully
- Total work units failed
- Total work units returned unprocessed (shutdown during processing)
- Total work units discarded (validation failures)
- Source file path and local status (existing / downloaded)
- Thread pool size
- Duration in seconds
- Average throughput (completed msg/s)
- Sequence number range (first seq, last seq)

## 12. Error Handling

| Condition                                  | Behavior                                          |
|--------------------------------------------|---------------------------------------------------|
| Cannot connect to Producer (TCP)           | Retry every 5 seconds, up to 30 attempts, then exit code 2 |
| Producer disconnects (TCP)                 | Log warning, attempt reconnect every 5s           |
| No messages received (UDP, 60s timeout)    | Log warning, continue listening                   |
| Source file download fails                 | Exit code 3, error to stderr                      |
| Source file hash mismatch                  | Exit code 3, error to stderr                      |
| Malformed JSON work unit                   | Log to stderr, discard, no result sent            |
| Duplicate `work_unit_id`                   | Log warning, send `"success"` result with cached data if available |
| Result send fails                          | Log error, retry once, then mark as failed locally |
| Job processing throws exception            | Catch, log to stderr, send `"failure"` result     |

## 13. Acceptance Criteria

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
- [ ] Duplicate `work_unit_id` messages are detected and handled
- [ ] `--max-messages N` stops processing after N completed units
- [ ] `--local` flag connects to 127.0.0.1
- [ ] On shutdown, in-progress work units are returned as `"failure"` results
- [ ] Ctrl+C shuts down cleanly within 5 seconds
- [ ] Final statistics are printed to stdout
- [ ] All unit tests pass
