# Producer Specification

## 1. Overview

A multi-threaded Producer CLI application implemented in C++17, built with CMake.
The Producer reads a job file, applies a test data permutation, tracks all sent
work units, and dispatches them over the network to one or more Consumers.
Each Consumer processes work units and returns results. The Producer removes
work units from its pending list upon receiving successful results. On shutdown,
the Producer writes a checkpoint JSON so it can resume from the last successfully
completed work unit on restart. A periodic backup is written approximately once
per minute to protect against abnormal termination.

The source tree is shared between Windows and Linux builds via `#ifdef`
conditionals. Two CMake targets are produced:

| # | Target             | Platform | CMake Target Name |
|---|--------------------|----------|-------------------|
| 1 | Producer CLI       | Windows  | `producer_win`    |
| 2 | Producer CLI       | Linux    | `producer_linux`  |

## 2. Network

- **Medium**: Ethernet
- **IP version**: IPv4 only
- **Transport**: Configurable — TCP or UDP (CLI flag, default TCP)
- **Addressing**:
  - Producer binds to `0.0.0.0` on a configurable port to accept incoming
    Consumer connections (TCP) or to send datagrams (UDP).
  - Default local gateway: `192.168.1.1`
  - When the Consumer resides on the same machine, the Producer listens on
    `127.0.0.1` (localhost) instead.
- **Multi-Consumer**: The Producer accepts and maintains simultaneous TCP
  connections from any number of Consumers. Each connection is handled on its
  own dedicated I/O thread.
- **TCP framing**: Length-prefixed JSON frames
  - 4-byte big-endian `uint32_t` frame length (payload bytes only, excludes header)
  - N-byte JSON payload (UTF-8)
- **UDP framing**: Same length-prefixed format per datagram. Each datagram
  carries exactly one JSON message. Datagram size must not exceed MTU
  (default 1500 bytes); messages exceeding the limit are dropped with a
  warning logged to stderr.

## 3. Job File

The Producer requires a valid input file path at startup. The file contains
the work items (jobs) that the Producer will turn into JSON messages.

- **Format**: JSON array of job objects, or one JSON object per line (NDJSON).
- **Validation**: The Producer validates the file exists, is readable, and
  contains well-formed JSON before starting any worker threads. If validation
  fails, the Producer exits with code 1 and prints an error to stderr.
- **Path**: Provided via `--file` CLI flag. No default — the flag is mandatory.

### Example Job File (JSON array)

```json
[
  { "job_id": 1, "task": "render", "params": { "width": 1920, "height": 1080 } },
  { "job_id": 2, "task": "encode", "params": { "codec": "h264", "bitrate": 5000 } }
]
```

### Example Job File (NDJSON)

```json
{"job_id": 1, "task": "render", "params": { "width": 1920, "height": 1080 }}
{"job_id": 2, "task": "encode", "params": { "codec": "h264", "bitrate": 5000 }}
```

## 4. Test Data Permutation

The Producer applies a permutation to the test data before generating messages.
The permutation determines the order in which jobs are dispatched to Consumers.

- **Modes** (CLI flag `--permutation`, default `sequential`):
  - `sequential` — jobs dispatched in file order
  - `random` — jobs shuffled using a seeded PRNG
  - `round_robin` — jobs distributed round-robin across Consumers
  - `reverse` — jobs dispatched in reverse file order
- **Seed**: For `random` mode, a `--seed` flag sets the PRNG seed (default:
  current time). The seed is included in the JSON message metadata for
  reproducibility.
- The permutation is applied once at startup. The resulting ordered list is
  the dispatch queue. On resume from a checkpoint, the Producer re-applies
  the same permutation and skips ahead to the index after the last successful
  work unit.

## 5. Work Unit Tracking

The Producer maintains an in-memory work unit table for every job in the
permuted list. Each entry tracks:

| Field            | Type     | Description                                      |
|------------------|----------|--------------------------------------------------|
| `work_unit_id`   | string   | Unique ID: `<producer_id>-<seq>`                 |
| `seq`            | int64    | Monotonically increasing sequence number         |
| `job`            | object   | The job object from the input file               |
| `status`         | enum     | `pending`, `sent`, `completed`, `failed`         |
| `consumer_id`    | string   | ID of the Consumer that received this unit       |
| `sent_at`        | string   | ISO 8601 timestamp when dispatched               |
| `completed_at`   | string   | ISO 8601 timestamp when result received           |

### Lifecycle

1. **`pending`** — Job is in the permuted list, not yet dispatched.
2. **`sent`** — Job has been sent to a Consumer. The Producer waits for a
   result message.
3. **`completed`** — Consumer returned a successful result. The entry is
   logically removed from the active pending list (retained in completed
   history for statistics).
4. **`failed`** — Consumer returned a failure result, or the Consumer
   connection was lost and the work unit was not acknowledged. The entry
   returns to `pending` for re-dispatch.

### Re-dispatch

When a Consumer disconnects without acknowledging a work unit, the Producer
marks that unit as `failed`, resets its status to `pending`, and makes it
available for re-dispatch to another Consumer (or the same Consumer if it
reconnects).

## 6. JSON Message Format — Work Unit (Producer → Consumer)

Every work unit message sent by the Producer is a JSON object. The first two
required fields are the source file path and the test data permutation.

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
| `permutation`        | string | yes      | Permutation mode applied                         |
| `permutation_seed`   | int64  | no       | PRNG seed (present only when `permutation` is `random`) |
| `work_unit_id`       | string | yes      | Unique identifier for this work unit             |
| `seq`                | int64  | yes      | Monotonically increasing sequence number         |
| `timestamp`          | string | yes      | ISO 8601 UTC timestamp                           |
| `producer_id`        | string | yes      | Unique identifier for this Producer instance     |
| `job`                | object | yes      | The job object from the input file               |

## 7. JSON Message Format — Result (Consumer → Producer)

The Consumer sends a result message back to the Producer after processing
a work unit.

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

When the Producer receives a result with `status: "success"`, it marks the
work unit as `completed` and removes it from the pending list. When the
status is `"failure"`, the work unit is marked `failed` and returned to
`pending` for re-dispatch.

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
to that Consumer connection.

## 9. Checkpoint State File

On shutdown (graceful or abnormal), the Producer writes a checkpoint JSON
file so it can resume from where it left off. The checkpoint records the
permutation state and the index of the last successfully completed work unit.

### File Paths

- **Primary**: `--checkpoint-dir/state.json`
- **Backup**: `--checkpoint-dir/state.backup.json`

The backup is a copy of the previous primary, written before the new primary
is flushed. This ensures that if the write is interrupted, the backup holds
the last known good state.

### Write Schedule

- Every 60 seconds while the Producer is running.
- Immediately on graceful shutdown (SIGINT/SIGTERM).
- The backup file is updated alongside each primary write.

### Checkpoint Format

```json
{
  "producer_id": "prod-001",
  "source_file": "/path/to/jobs.json",
  "permutation": "random",
  "permutation_seed": 12345,
  "total_jobs": 1000,
  "last_completed_seq": 42,
  "last_completed_work_unit_id": "prod-001-42",
  "completed_count": 42,
  "pending_count": 958,
  "failed_count": 0,
  "checkpoint_timestamp": "2026-07-30T12:05:00.000Z",
  "consumers_connected": [
    { "consumer_id": "cons-001", "pending_units": 3 }
  ]
}
```

| Field                          | Type   | Description                                      |
|--------------------------------|--------|--------------------------------------------------|
| `producer_id`                  | string | Unique identifier for this Producer instance     |
| `source_file`                  | string | Path to the job file                             |
| `permutation`                  | string | Permutation mode                                 |
| `permutation_seed`             | int64  | PRNG seed (if `random`)                          |
| `total_jobs`                   | int64  | Total jobs in the input file                     |
| `last_completed_seq`           | int64  | Sequence number of the last completed work unit  |
| `last_completed_work_unit_id`  | string | Work unit ID of the last completed unit          |
| `completed_count`              | int64  | Number of completed work units                   |
| `pending_count`                | int64  | Number of pending (sent but not completed) units |
| `failed_count`                 | int64  | Number of failed work units                      |
| `checkpoint_timestamp`         | string | ISO 8601 UTC timestamp of this checkpoint        |
| `consumers_connected`          | array  | List of connected Consumers and their pending units |

### Resume Behavior

On startup, if `--checkpoint-dir/state.json` exists:
1. The Producer loads the checkpoint.
2. Re-validates the source file path.
3. Re-applies the same permutation with the same seed.
4. Skips all work units up to and including `last_completed_seq`.
5. Continues dispatching from the next work unit.

If `state.json` is corrupt but `state.backup.json` is valid, the Producer
uses the backup and logs a warning.

## 10. Threading Model

- **Main thread**: Parses CLI arguments, validates job file, loads checkpoint
  (if present), manages lifecycle, handles SIGINT/SIGTERM, runs the periodic
  checkpoint writer timer.
- **Dispatcher thread** (1): Maintains the permuted job list, responds to
  `work_request` messages from Consumers, dispatches work units, tracks
  work unit statuses, processes result messages.
- **I/O threads** (1 per Consumer connection): Each accepted TCP connection
  gets a dedicated thread for reading and writing frames on that socket.
- **Checkpoint thread** (1): Runs on a 60-second timer, writes the primary
  and backup checkpoint files.

## 11. CLI Interface

```
producer --file PATH [--port PORT] [--transport tcp|udp] [--permutation MODE] [--seed N] [--duration SECONDS] [--gateway IP] [--checkpoint-dir DIR] [--resume]
```

| Flag             | Default        | Description                                      |
|------------------|----------------|--------------------------------------------------|
| `--file`         | *(required)*   | Path to the job file (JSON array or NDJSON)      |
| `--port`         | 9876           | Port to bind on                                  |
| `--transport`    | `tcp`          | Transport protocol (`tcp` or `udp`)              |
| `--permutation`  | `sequential`   | Job permutation mode                             |
| `--seed`         | (current time) | PRNG seed for `random` permutation               |
| `--duration`     | 0              | Run duration in seconds (0 = run until done)     |
| `--gateway`      | 192.168.1.1    | Default local gateway IPv4 address               |
| `--checkpoint-dir` | `./`         | Directory for checkpoint state files             |
| `--resume`       | false          | Resume from checkpoint if one exists             |

## 12. Platform Conditionals

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

## 13. Graceful Shutdown

The Producer handles SIGINT (Ctrl+C) and SIGTERM:

1. Signal handler sets an atomic `stop_requested` flag.
2. Main thread detects the flag.
3. Dispatcher thread stops accepting new work requests.
4. Checkpoint is written immediately (primary + backup).
5. I/O threads finish sending in-flight frames and close sockets.
6. I/O threads join, resources are released.
7. Final statistics are printed to stdout.

### Statistics

Printed on exit:
- Total jobs read from file
- Total work units dispatched
- Total work units completed
- Total work units failed / re-dispatched
- Total work units still pending
- Number of Consumers connected during session
- Duration in seconds
- Average throughput (completed msg/s)
- Permutation mode and seed (if applicable)
- Checkpoint file path and last write time

## 14. Error Handling

| Condition                              | Behavior                                          |
|----------------------------------------|---------------------------------------------------|
| `--file` not provided                  | Exit code 1, usage message to stderr              |
| Job file does not exist                | Exit code 1, error to stderr                      |
| Job file is not valid JSON             | Exit code 1, error to stderr                      |
| Port already in use                    | Exit code 2, error to stderr                      |
| Checkpoint file corrupt, backup valid  | Use backup, log warning                           |
| Both checkpoint files corrupt          | Log error, start fresh from beginning             |
| Consumer disconnects (TCP)             | Mark unacknowledged units as `failed` → `pending` |
| UDP datagram exceeds MTU               | Drop message, log warning to stderr               |
| Checkpoint write fails                 | Log error to stderr, continue running             |

## 15. Acceptance Criteria

- [ ] `cmake -B build && cmake --build build` succeeds on Windows (MSVC)
- [ ] `cmake -B build && cmake --build build` succeeds on Linux (GCC/Clang)
- [ ] Producer exits with code 1 when `--file` is missing or invalid
- [ ] Producer reads and validates the job file before starting workers
- [ ] All four permutation modes produce correct output orderings
- [ ] Every sent work unit contains `source_file`, `permutation`, and `work_unit_id`
- [ ] Producer tracks all work units in the pending table
- [ ] Producer removes work unit from pending list on successful result
- [ ] Producer re-dispatches work units when Consumer disconnects
- [ ] Producer accepts multiple simultaneous Consumer connections
- [ ] Producer responds to `work_request` messages with available work units
- [ ] Checkpoint file is written every 60 seconds and on shutdown
- [ ] Backup checkpoint file exists and is a valid prior state
- [ ] `--resume` restores state and continues from last completed seq
- [ ] `--seed` produces reproducible permutations
- [ ] Ctrl+C shuts down cleanly within 5 seconds with checkpoint written
- [ ] Final statistics are printed to stdout
- [ ] All unit tests pass
