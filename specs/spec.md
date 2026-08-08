# Producer-Consumer System — Specification

## 1. Overview

A multi-threaded Producer-Consumer system implemented in C++17, built with CMake.
The Producer reads a job file, applies a test data permutation, tracks all sent
work units, and dispatches them over the network to one or more Consumers.
Consumers process work units using a thread pool and return results. The Producer
removes completed work units from its pending list and persists checkpoint state
so it can resume after shutdown.

Both roles share a common source tree with `#ifdef` conditionals to produce
Windows and Linux binaries from the same files.

Detailed specifications:
- [`Producer-Spec.md`](./Producer-Spec.md)
- [`Consumer-Spec.md`](./Consumer-Spec.md)

## 2. Buildables

| # | Target             | Platform | CMake Target Name |
|---|--------------------|----------|-------------------|
| 1 | Producer CLI       | Windows  | `producer_win`    |
| 2 | Producer CLI       | Linux    | `producer_linux`  |
| 3 | Consumer CLI       | Windows  | `consumer_win`    |
| 4 | Consumer CLI       | Linux    | `consumer_linux`  |

CMake selects the appropriate target based on `CMAKE_SYSTEM_NAME`.
Cross-compilation is not required for Phase 1.

## 3. Communication

- **Medium**: Ethernet
- **IP version**: IPv4 only
- **Transport**: Configurable — TCP or UDP (CLI flag, default TCP)
- **Default local gateway**: `192.168.1.1`
- **Localhost**: When Producer and Consumer are on the same machine, use `127.0.0.1`
- **Framing**: Length-prefixed JSON frames
  - 4-byte big-endian `uint32_t` frame length (payload bytes only, excludes header)
  - N-byte JSON payload (UTF-8)
- **Multi-Consumer**: Producer accepts simultaneous TCP connections from any
  number of Consumers, each on its own I/O thread.
- **File transfer**: Secondary TCP connection on `port + 1` for Consumer to
  download the source job file from the Producer.

## 4. Message Types

Three message types flow between Producer and Consumer:

### Work Unit (Producer → Consumer)

```json
{
  "msg_type": "work_unit",
  "source_file": "/path/to/jobs.json",
  "permutation": "random",
  "work_unit_id": "prod-001-42",
  "seq": 42,
  "timestamp": "2026-07-30T12:00:00.000Z",
  "producer_id": "prod-001",
  "job": { "job_id": 1, "task": "render", "params": { ... } }
}
```

### Result (Consumer → Producer)

```json
{
  "msg_type": "result",
  "work_unit_id": "prod-001-42",
  "seq": 42,
  "consumer_id": "cons-001",
  "status": "success",
  "result": { "output": "...", "duration_ms": 1250 },
  "timestamp": "2026-07-30T12:00:01.250Z"
}
```

### Work Request (Consumer → Producer)

```json
{
  "msg_type": "work_request",
  "consumer_id": "cons-001",
  "threads_available": 4,
  "timestamp": "2026-07-30T12:00:00.500Z"
}
```

## 5. Work Unit Lifecycle

1. **`pending`** — Job is in the permuted list, not yet dispatched.
2. **`sent`** — Job sent to a Consumer; Producer waits for a result.
3. **`completed`** — Consumer returned `status: "success"`; removed from pending list.
4. **`failed`** — Consumer returned `status: "failure"` or disconnected; returned to `pending` for re-dispatch.

## 6. Threading Model

### Producer

| Thread                  | Count | Responsibility                              |
|-------------------------|-------|---------------------------------------------|
| Main                    | 1     | CLI, lifecycle, signals, checkpoint timer   |
| Dispatcher              | 1     | Job list, work unit tracking, result processing |
| I/O                     | N     | 1 per Consumer connection, read/write frames |
| Checkpoint              | 1     | Writes state.json + backup every 60s        |

### Consumer

| Thread                  | Count        | Responsibility                              |
|-------------------------|--------------|---------------------------------------------|
| Main                    | 1            | CLI, lifecycle, signals, file download      |
| Receiver                | 1            | Read frames, validate, push to work queue   |
| Pool                    | 1 per core   | Pop work units, process, send results       |
| File transfer           | 1 (on demand)| Download source file from Producer          |

## 7. Checkpoint State

The Producer persists state so it can resume after shutdown:

- **Primary**: `--checkpoint-dir/state.json`
- **Backup**: `--checkpoint-dir/state.backup.json`
- **Schedule**: Every 60 seconds + immediately on graceful shutdown
- **Resume**: `--resume` flag re-applies permutation, skips to `last_completed_seq`

```json
{
  "producer_id": "prod-001",
  "source_file": "/path/to/jobs.json",
  "permutation": "random",
  "permutation_seed": 12345,
  "total_jobs": 1000,
  "last_completed_seq": 42,
  "completed_count": 42,
  "pending_count": 958,
  "checkpoint_timestamp": "2026-07-30T12:05:00.000Z"
}
```

## 8. Platform Conditionals

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

## 9. CLI Interface

### Producer

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
| `--checkpoint-dir`| `./`          | Directory for checkpoint state files             |
| `--resume`       | false          | Resume from checkpoint if one exists             |

### Consumer

```
consumer [--host HOST] [--port PORT] [--transport tcp|udp] [--threads N] [--file-dir DIR] [--max-messages N] [--local] [--gateway IP] [--consumer-id ID]
```

| Flag           | Default            | Description                                      |
|----------------|--------------------|--------------------------------------------------|
| `--host`       | 127.0.0.1          | Producer host IPv4 address                       |
| `--port`       | 9876               | Producer port                                    |
| `--transport`  | `tcp`              | Transport protocol (`tcp` or `udp`)              |
| `--threads`    | (cores)            | Thread pool size (default: 1 per core)           |
| `--file-dir`   | `./`               | Local directory for downloaded source files      |
| `--max-messages`| 0                 | Stop after N completed work units                |
| `--local`      | false              | Force localhost connection (127.0.0.1)           |
| `--gateway`    | 192.168.1.1        | Default local gateway IPv4 address               |
| `--consumer-id`| (auto-generated)   | Unique Consumer ID                               |

## 10. Project Structure

```
project/
├── CMakeLists.txt              # Top-level CMake configuration
├── specs/
│   ├── spec.md                 # This document (umbrella)
│   ├── Producer-Spec.md        # Producer detailed spec
│   └── Consumer-Spec.md        # Consumer detailed spec
├── docs/
│   ├── IMPLEMENTATION.md       # Implementation guide for developers
│   └── USER_GUIDE.md           # End-user documentation
├── include/
│   ├── common/
│   │   ├── message.h           # JSON message types (work_unit, result, work_request)
│   │   ├── queue.h             # Thread-safe bounded queue
│   │   ├── socket.h            # Platform-abstracted TCP/UDP socket wrapper
│   │   ├── types.h             # Shared type definitions
│   │   └── checkpoint.h        # Checkpoint state read/write
│   ├── producer/
│   │   ├── producer.h          # Producer engine
│   │   └── work_tracker.h      # Work unit tracking table
│   └── consumer/
│       ├── consumer.h          # Consumer engine
│       └── thread_pool.h       # Consumer thread pool
├── src/
│   ├── common/
│   │   ├── message.cpp
│   │   ├── queue.cpp
│   │   ├── socket.cpp          # Contains #ifdef _WIN32 / #else
│   │   ├── signal_handler.cpp  # Platform-specific signal handling
│   │   └── checkpoint.cpp
│   ├── producer/
│   │   ├── producer.cpp
│   │   ├── work_tracker.cpp
│   │   └── main.cpp            # Producer entry point
│   └── consumer/
│       ├── consumer.cpp
│       ├── thread_pool.cpp
│       └── main.cpp            # Consumer entry point
└── tests/
    ├── CMakeLists.txt
    ├── test_message.cpp        # Message serialization round-trip
    ├── test_queue.cpp          # Thread-safe queue stress test
    ├── test_work_tracker.cpp   # Work unit lifecycle
    ├── test_checkpoint.cpp     # Checkpoint save/resume
    └── test_integration.cpp    # Producer-Consumer end-to-end
```

## 11. Dependencies

| Library       | Purpose                          | Integration      |
|---------------|----------------------------------|------------------|
| nlohmann/json | JSON serialization/deserialization | FetchContent     |
| GTest         | Unit and integration testing     | FetchContent     |

No external threading or networking libraries — use standard C++17
(`<thread>`, `<atomic>`, `<mutex>`, `<condition_variable>`) and
platform sockets (`WinSock2` / `POSIX`).

## 12. CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.20)
project(ProducerConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FetchContent for nlohmann/json and GTest

# Common library (shared by both Producer and Consumer)
add_library(common STATIC
    src/common/message.cpp
    src/common/queue.cpp
    src/common/socket.cpp
    src/common/signal_handler.cpp
    src/common/checkpoint.cpp
)
target_include_directories(common PUBLIC include)

# Producer
add_executable(producer src/producer/producer.cpp src/producer/work_tracker.cpp src/producer/main.cpp)
target_link_libraries(producer PRIVATE common)

# Consumer
add_executable(consumer src/consumer/consumer.cpp src/consumer/thread_pool.cpp src/consumer/main.cpp)
target_link_libraries(consumer PRIVATE common)

# Platform-specific linking
if(WIN32)
    target_link_libraries(producer PRIVATE ws2_32)
    target_link_libraries(consumer PRIVATE ws2_32)
endif()

# Tests
option(BUILD_TESTS "Build unit and integration tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

## 13. Internal Queue Design

A bounded, thread-safe queue used by both Producer and Consumer:

- **Template**: `template<typename T> class BoundedQueue`
- **Synchronization**: `std::mutex` + `std::condition_variable`
- **Operations**: `push()` (blocks if full), `pop()` (blocks if empty),
  `try_push()`, `try_pop()`, `size()`, `shutdown()`
- **Shutdown**: A `shutdown()` method unblocks all waiting threads so they
  can exit cleanly.

## 14. Graceful Shutdown

Both applications handle SIGINT (Ctrl+C) and SIGTERM:

1. Signal handler sets an atomic `stop_requested` flag.
2. Main thread detects the flag and initiates shutdown sequence.
3. Worker threads finish current work and exit.
4. Sockets are closed, resources are released.
5. Final statistics are printed to stdout.

### Producer Shutdown

- Stops accepting new work requests.
- Writes checkpoint immediately (primary + backup).
- I/O threads finish in-flight frames and close.
- In-progress work units are left as `sent` in the checkpoint for re-dispatch on resume.

### Consumer Shutdown

- Receiver thread stops, signals work queue to shut down.
- Pool threads finish current work units and send results.
- Uncompleted work units are sent back as `"failure"` results.

### Statistics

**Producer prints on exit:**
- Total jobs read from file
- Total work units dispatched
- Total work units completed
- Total work units failed / re-dispatched
- Total work units still pending
- Number of Consumers connected during session
- Duration in seconds
- Average throughput (completed msg/s)
- Checkpoint file path and last write time

**Consumer prints on exit:**
- Total work units received
- Total work units completed successfully
- Total work units failed
- Total work units returned unprocessed
- Source file path and local status
- Thread pool size
- Duration in seconds
- Average throughput (completed msg/s)

## 15. Phases

### Phase 1 — Core (this spec)
- [ ] CMake project with platform conditionals
- [ ] Shared common library (message, queue, socket, checkpoint)
- [ ] Producer: job file, permutation, work unit tracking, multi-Consumer, checkpoint
- [ ] Consumer: thread pool, work requests, result reporting, file download
- [ ] TCP/UDP length-prefixed JSON framing
- [ ] Three message types: `work_unit`, `result`, `work_request`
- [ ] CLI argument parsing for both apps
- [ ] Graceful shutdown with statistics and checkpoint
- [ ] Unit tests for message, queue, work tracker, checkpoint
- [ ] Integration test (spawn producer, connect consumer, verify full lifecycle)

### Phase 2 — Extensions (future)
- [ ] Actual task execution (rendering, encoding, etc.)
- [ ] Consumer-to-Consumer load balancing
- [ ] Persistent result storage
- [ ] WebSocket or HTTP/2 transport option
- [ ] Performance benchmarking suite
- [ ] Dashboard / telemetry endpoint

## 16. Acceptance Criteria

- [ ] `cmake -B build && cmake --build build` succeeds on Windows (MSVC)
- [ ] `cmake -B build && cmake --build build` succeeds on Linux (GCC/Clang)
- [ ] Producer exits with code 1 when `--file` is missing or invalid
- [ ] Producer reads, validates, and permutes the job file
- [ ] Producer tracks all work units through their lifecycle
- [ ] Producer removes completed work units from pending list on result
- [ ] Producer re-dispatches work units when Consumer disconnects
- [ ] Producer accepts multiple simultaneous Consumer connections
- [ ] Producer responds to `work_request` with available work units
- [ ] Checkpoint file written every 60s and on shutdown; backup maintained
- [ ] `--resume` restores state and continues from last completed seq
- [ ] Consumer thread pool defaults to 1 thread per core
- [ ] Consumer sends `work_request` on connect and when threads are idle
- [ ] Consumer sends `result` back after each work unit
- [ ] Consumer downloads source file when not available locally
- [ ] All three message types are valid JSON with required fields
- [ ] Sequence numbers are monotonically increasing
- [ ] Ctrl+C shuts down both apps cleanly within 5 seconds
- [ ] Final statistics are printed to stdout
- [ ] All unit and integration tests pass
