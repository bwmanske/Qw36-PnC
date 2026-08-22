# Producer-Consumer System — Specification

## 1. Overview

A multi-threaded Producer-Consumer system implemented in C++17, built with CMake.
The Producer reads a JSON configuration file, initializes a test plugin (PWD, BENCH, or ECHO),
generates work units, and dispatches them over the network to one or more Consumers.
Consumers process work units using a pluggable handler and a thread pool, then return
results. The Producer tracks all sent work units, validates results, and persists
checkpoint state so it can resume after shutdown.

Both roles share a common source tree with `#ifdef` conditionals to produce
Windows and Linux binaries from the same files.

The system uses a plugin architecture: the Producer dispatches through a `TestPlugin`
table, and the Consumer processes through an `IWorkUnitHandler` interface. Results
are persisted through an `IResultSink` interface.

## 2. Buildables

| # | Target             | Type       | CMake Target Name |
|---|--------------------|------------|-------------------|
| 1 | Common library     | Static lib | `common`          |
| 2 | Producer library   | Static lib | `producer_lib`    |
| 3 | Consumer library   | Static lib | `consumer_lib`    |
| 4 | Producer CLI       | Executable | `producer`        |
| 5 | Consumer CLI       | Executable | `consumer`        |

Single CMake target names — no platform-specific suffixes. CMake selects the
appropriate platform behavior via `#ifdef _WIN32` / `#else` in the source files.

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
  number of Consumers, each on its own detached I/O thread.
- **File transfer**: Secondary TCP connection on `port + 1`. Consumer sends
  `0x01` + null-terminated filename. Producer responds with 4-byte big-endian
  file size + raw bytes. Size `0` means file not found.
- **Sibling file manifest**: Consumer sends `0x02` to get a JSON manifest of
  all sibling files (when `--transfer-siblings` is set). Producer responds with
  4-byte big-endian JSON length + JSON array of `{name, size, sha256}` objects.
  Empty array `[]` if no siblings or flag not set. Consumer downloads each file
  via `0x01` and verifies SHA-256. Local consumers skip manifest entirely.
- **Socket recv timeout**: 10 s on control channel, 30 s on file transfer channel.
- **Consumer registration**: Producer tracks connected consumers via
  `connected_consumers_` map. Logs registration and disconnect events with
  reclaimed work unit count.
- **Work request throttling**: Consumer limits work requests to max 1 per 50 ms.
- **Duplicate tracking**: Consumer maintains an LRU cache of 3000 completed
  `work_unit_id` values to detect and discard duplicate work units.

## 4. Message Types

Five message types flow between Producer and Consumer:

### Version (Consumer → Producer)

```json
{
  "msg_type": "version",
  "version": "0.6",
  "consumer_id": "cons-001"
}
```

Sent as the first message on connect (TCP or UDP) to verify both sides share the
same protocol version (`PC_VERSION`). A mismatch causes the Consumer to exit with
code 4.

### Work Unit (Producer → Consumer)

```json
{
  "msg_type": "work_unit",
  "test_type": "PWD",
  "source_file": "data.bin",
  "permutation": "sequential",
  "permutation_seed": 12345,
  "work_unit_id": "prod-001-42",
  "seq": 42,
  "timestamp": "2026-07-30T12:00:00.000Z",
  "producer_id": "prod-001",
  "source_hash": "abc123...",
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
  "timestamp": "2026-07-30T12:00:01.250Z",
  "found_password": "correct-horse",
  "file_error": "corrupted archive"
}
```

The `found_password` and `file_error` fields are optional. When `found_password`
is present, the Producer stops dispatching. When `file_error` is present, the
Producer logs the error and stops.

### Work Request (Consumer → Producer)

```json
{
  "msg_type": "work_request",
  "consumer_id": "cons-001",
  "threads_available": 4,
  "timestamp": "2026-07-30T12:00:00.500Z"
}
```

### Heartbeat (Consumer → Producer)

```json
{
  "msg_type": "heartbeat",
  "consumer_id": "cons-001",
  "timestamp": "2026-07-30T12:00:05.000Z"
}
```

Consumer sends a heartbeat every 5 seconds. The Producer's connection monitor
checks every 5 seconds and detects stale connections after 30 seconds of no
activity. Stale consumers have their sockets closed and their in-flight work
units reclaimed.

## 5. Work Unit Lifecycle

1. **`pending`** — Job is generated by the plugin, not yet dispatched.
2. **`sent`** — Job sent to a Consumer; Producer waits for a result.
3. **`completed`** — Consumer returned `status: "success"`; removed from pending list.
4. **`failed`** — Consumer returned `status: "failure"` or disconnected; returned to `pending` for re-dispatch.

## 6. Threading Model

### Producer

| Thread                  | Count | Responsibility                              |
|-------------------------|-------|---------------------------------------------|
| Main                    | 1     | CLI, accept loop, lifecycle, signals        |
| Dispatcher              | 1     | Plugin exit conditions, max units, duration |
| Checkpoint              | 1     | Writes state.json + backup every 60s        |
| File transfer           | 1     | Accepts file transfer connections on port+1 |
| Monitor connections     | 1     | Detects stale consumers, reclaims work units|
| Client handler          | N     | 1 detached thread per Consumer connection   |

### Consumer

| Thread                  | Count        | Responsibility                              |
|-------------------------|--------------|---------------------------------------------|
| Main                    | 1            | CLI, lifecycle, signals, max-messages check |
| Receiver                | 1            | Read frames, validate, push to work queue   |
| Heartbeat               | 1            | Sends heartbeat every 5s                    |
| Pool                    | 1 per core   | Pop work units, process via handler, send results |

## 7. Checkpoint State

The Producer persists state so it can resume after shutdown:

- **Primary**: `--checkpoint-dir/state.json`
- **Backup**: `--checkpoint-dir/state.backup.json`
- **Schedule**: Every 60 seconds + immediately on graceful shutdown
- **Resume**: `--resume` flag re-applies permutation, skips to `last_completed_seq`
- **Default directory**: `%APPDATA%\Producer\` (Windows), `~/.local/share/producer/` (Linux)
  via `get_data_directory()` from `common/util.h`

```json
{
  "producer_id": "prod-001",
  "source_file": "data.bin",
  "permutation": "sequential",
  "permutation_seed": 12345,
  "total_jobs": 1000,
  "last_completed_seq": 42,
  "last_completed_work_unit_id": "prod-001-42",
  "completed_count": 42,
  "pending_count": 958,
  "failed_count": 0,
  "checkpoint_timestamp": "2026-07-30T12:05:00.000Z",
  "consumers_connected": [],
  "plugin_state": { ... }
}
```

The `plugin_state` field holds plugin-specific resume data (e.g., PWD permutation
state, BENCH chunk offset).

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
- `ssize_t` typedef (not defined on Windows — provided in `socket.h`)
- `NOMINMAX` must be `#define`d before `<windows.h>` to prevent `min`/`max` macro conflicts

## 9. CLI Interface

### Producer

```
producer --file PATH [OPTIONS]
```

| Flag               | Default        | Description                                      |
|--------------------|----------------|--------------------------------------------------|
| `--file`           | *(required)*   | Path to the main JSON config file                |
| `--port`           | 9876           | Port to bind on                                  |
| `--transport`      | `tcp`          | Transport protocol (`tcp` or `udp`)              |
| `--permutation`    | `sequential`   | Job permutation mode                             |
| `--seed`           | (current time) | PRNG seed for `random` permutation               |
| `--duration`       | 0              | Run duration in seconds (0 = run until done)     |
| `--max-time`       | 0              | Max time before shutdown; value may end in `s`/`m`/`h` (e.g. `30s`, `5m`, `1h`; bare number = seconds; 0 = no limit) |
| `--gateway`        | 192.168.1.1    | Default local gateway IPv4 address               |
| `--checkpoint-dir`| (data dir)     | Directory for checkpoint state files             |
| `--resume`         | false          | Resume from checkpoint if one exists             |
| `--test-type`      | (from config)  | Test type identifier (e.g. PWD, BENCH)           |
| `--transfer-siblings` | false       | Transfer all sibling files in config directory to remote consumers |

### Consumer

```
consumer [OPTIONS]
```

| Flag             | Default            | Description                                      |
|------------------|--------------------|--------------------------------------------------|
| `--host`         | 127.0.0.1          | Producer host IPv4 address                       |
| `--port`         | 9876               | Producer port                                    |
| `--transport`    | `tcp`              | Transport protocol (`tcp` or `udp`)              |
| `--threads`      | (cores)            | Thread pool size (default: 1 per core)           |
| `--file-dir`     | `./`               | Local directory for downloaded source files      |
| `--max-messages` | 0                  | Stop after N completed work units                |
| `--local`        | false              | Force localhost connection (127.0.0.1)           |
| `--gateway`      | 192.168.1.1        | Default local gateway IPv4 address               |
| `--consumer-id`  | (auto-generated)   | Unique Consumer ID                               |
| `--handler`      | (none)             | Work unit handler type (e.g. PWD, BENCH, ECHO)   |
| `--handler-config`| (none)            | Path to handler-specific config file             |
| `--result-file`  | (auto-generated)   | Write results to JSON lines file                 |
| `--max-failures` | 0                  | Stop after N failure results                     |
| `--max-duration` | 0                  | Stop after N seconds                             |
| `--timeout`      | 0                  | Close after N seconds with no producer communication (0 = no limit) |

## 10. Project Structure

```
project/
├── CMakeLists.txt              # Top-level CMake configuration
├── specs/
│   ├── spec.md                 # This document (umbrella)
│   ├── Producer-Spec.md        # Producer detailed spec
│   └── Consumer-Spec.md        # Consumer detailed spec
├── docs/
│   ├── BUILDING.md             # Build instructions (Windows/Linux)
│   ├── COMMUNICATION.md        # Network protocol reference
│   ├── IMPLEMENTATION.md       # Implementation guide for developers
│   ├── PROGRESS.md             # Remaining tasks tracker
│   ├── TESTING.md              # Testing strategy
│   └── USER_GUIDE.md           # End-user documentation
├── include/
│   ├── common/
│   │   ├── message.h           # JSON message types (version, work_unit, result, work_request, heartbeat)
│   │   ├── queue.h             # Thread-safe bounded queue
│   │   ├── socket.h            # Platform-abstracted TCP/UDP socket wrapper
│   │   ├── types.h             # Shared type definitions
│   │   ├── checkpoint.h        # Checkpoint state read/write
│   │   ├── util.h              # SHA-256, platform data directory
│   │   ├── signal_handler.h    # Platform-specific signal handling
│   │   └── archive_validator.h # Archive validation (ZIP, RAR, 7Z) via libarchive
│   ├── producer/
│   │   ├── producer.h          # Producer engine
│   │   ├── work_tracker.h      # Work unit tracking table
│   │   ├── test_plugin.h       # TestPlugin dispatch table interface
│   │   ├── PWD_plugin.h        # PWD plugin factory
│   │   ├── BENCH_plugin.h      # BENCH plugin factory
│   │   └── ECHO_plugin.h       # ECHO plugin factory
│   └── consumer/
│       ├── consumer.h          # Consumer engine
│       ├── thread_pool.h       # Consumer thread pool
│       ├── work_unit_handler.h # IWorkUnitHandler interface
│       ├── result_sink.h       # IResultSink interface
│       ├── file_result_sink.h  # FileResultSink (JSON lines)
│       ├── PWD_Handler.h       # PWD handler implementation
│       ├── BENCH_Handler.h     # BENCH handler implementation
│       └── ECHO_Handler.h      # ECHO handler implementation
├── src/
│   ├── common/
│   │   ├── message.cpp
│   │   ├── queue.cpp
│   │   ├── socket.cpp          # Contains #ifdef _WIN32 / #else
│   │   ├── signal_handler.cpp  # Platform-specific signal handling
│   │   ├── checkpoint.cpp
│   │   ├── util.cpp            # SHA-256 (RFC 6234), get_data_directory
│   │   └── archive_validator.cpp
│   ├── producer/
│   │   ├── producer.cpp
│   │   ├── work_tracker.cpp
│   │   ├── PWD_NextUnit.cpp    # Legacy C-style permutation engine
│   │   ├── PWD_plugin.cpp      # PWD TestPlugin wrapper
│   │   ├── BENCH_plugin.cpp    # BENCH TestPlugin implementation
│   │   ├── ECHO_plugin.cpp     # ECHO TestPlugin implementation
│   │   └── main.cpp            # Producer entry point
│   └── consumer/
│       ├── consumer.cpp
│       ├── thread_pool.cpp
│       ├── PWD_Handler.cpp     # PWD handler (uses ArchiveValidator)
│       ├── BENCH_Handler.cpp   # BENCH handler
│       ├── ECHO_Handler.cpp    # ECHO handler
│       ├── file_result_sink.cpp
│       └── main.cpp            # Consumer entry point
└── tests/
    ├── CMakeLists.txt
    ├── test_message.cpp        # Message serialization round-trip (all 5 types)
    ├── test_queue.cpp          # Thread-safe queue stress test
    ├── test_work_tracker.cpp   # Work unit lifecycle
    ├── test_checkpoint.cpp     # Checkpoint save/resume
    ├── test_integration.cpp    # Lifecycle + real-process end-to-end
    ├── test_pwd_next_unit.cpp  # PWD_NextUnit permutation engine
    ├── test_sha256.cpp         # SHA-256 RFC 6234 vectors + file hash
    ├── test_file_result_sink.cpp # FileResultSink JSON lines + stats
    ├── test_util.cpp           # parse_duration tests
    ├── test_thread_pool.cpp    # ThreadPool dispatch, idle callback, drain, shutdown
    ├── test_echo.cpp           # ECHO plugin + handler
    └── test_socket.cpp         # TCP/UDP frame round-trip
```

## 11. Dependencies

| Library       | Version | Purpose                          | Integration      |
|---------------|---------|----------------------------------|------------------|
| nlohmann/json | v3.11.3 | JSON serialization/deserialization | FetchContent     |
| GTest         | v1.14.0 | Unit and integration testing     | FetchContent     |
| zlib          | v1.3.1  | Compression (required by libarchive) | FetchContent |
| libarchive    | v3.7.9  | Archive validation (ZIP, RAR, 7Z) | FetchContent    |

SHA-256 is implemented as pure C++ (RFC 6234) in `common/util.cpp` — no external
crypto library. No external threading or networking libraries — standard C++17
(`<thread>`, `<atomic>`, `<mutex>`, `<condition_variable>`) and platform sockets
(`WinSock2` / `POSIX`).

## 12. CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.20)
project(ProducerConsumer LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── nlohmann/json (v3.11.3) ──────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── zlib (v1.3.1, required by libarchive) ────────────────────────
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
)
FetchContent_MakeAvailable(zlib)

# ── libarchive (v3.7.9, ZIP/RAR/7Z validation) ───────────────────
FetchContent_Declare(
    libarchive
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG v3.7.9
)
FetchContent_MakeAvailable(libarchive)

# ── Common static library ────────────────────────────────────────
add_library(common STATIC
    src/common/message.cpp
    src/common/queue.cpp
    src/common/socket.cpp
    src/common/signal_handler.cpp
    src/common/checkpoint.cpp
    src/common/util.cpp
    src/common/archive_validator.cpp
)
target_include_directories(common PUBLIC include)
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(common PUBLIC archive_static)

# ── Producer static library (no main) ────────────────────────────
add_library(producer_lib STATIC
    src/producer/producer.cpp
    src/producer/work_tracker.cpp
    src/producer/PWD_NextUnit.cpp
    src/producer/PWD_plugin.cpp
    src/producer/BENCH_plugin.cpp
    src/producer/ECHO_plugin.cpp
)
target_link_libraries(producer_lib PUBLIC common)

# ── Consumer static library (no main) ────────────────────────────
add_library(consumer_lib STATIC
    src/consumer/consumer.cpp
    src/consumer/thread_pool.cpp
    src/consumer/PWD_Handler.cpp
    src/consumer/BENCH_Handler.cpp
    src/consumer/ECHO_Handler.cpp
    src/consumer/file_result_sink.cpp
)
target_link_libraries(consumer_lib PUBLIC common)

# ── Executables ──────────────────────────────────────────────────
add_executable(producer src/producer/main.cpp)
target_link_libraries(producer PRIVATE producer_lib)

add_executable(consumer src/consumer/main.cpp)
target_link_libraries(consumer PRIVATE consumer_lib)

# ── Platform-specific linking ────────────────────────────────────
if(WIN32)
    target_link_libraries(common PRIVATE ws2_32)
endif()

# ── Tests (link against libs, not executables) ───────────────────
option(BUILD_TESTS "Build unit and integration tests" ON)
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()
```

Tests link against `common`, `producer_lib`, and/or `consumer_lib` — never the
executables. Current test count: 110/110 passing.

## 13. Internal Queue Design

A bounded, thread-safe queue used by both Producer and Consumer:

- **Template**: `template<typename T> class BoundedQueue`
- **Synchronization**: `std::mutex` + `std::condition_variable`
- **Operations**: `push()` (blocks if full), `pop()` (blocks if empty),
  `try_push()`, `try_pop()`, `size()`, `shutdown()`
- **Shutdown**: A `shutdown()` method unblocks all waiting threads so they
  can exit cleanly.

## 14. Plugin Architecture

### Producer — `TestPlugin`

The Producer uses a `TestPlugin` dispatch table (`include/producer/test_plugin.h`)
with four `std::function` members:

| Member             | Signature                                          | Purpose                              |
|--------------------|----------------------------------------------------|--------------------------------------|
| `startup`          | `void(config_path, resume_state)`                  | Reads plugin config, restores state  |
| `next_unit`        | `bool(out WorkUnitMessage&)`                       | Generates next work unit             |
| `checkpoint`       | `nlohmann::json()`                                 | Returns plugin-specific state        |
| `exit_conditions`  | `bool()`                                           | Returns `true` when plugin wants to stop |

The `next_unit` method returns `false` when the plugin is exhausted.

### Existing Plugins

| Plugin | Producer | Consumer | Description                        |
|--------|----------|----------|------------------------------------|
| PWD    | `PWD_plugin.cpp` | `PWD_Handler.cpp` | Password permutation generator   |
| BENCH  | `BENCH_plugin.cpp` | `BENCH_Handler.cpp` | File chunk benchmark           |
| ECHO   | `ECHO_plugin.cpp` | `ECHO_Handler.cpp` | Echo/verification with power-law delay |

### Adding a New Test Type

1. Create `XXX_plugin.h/cpp` in `src/producer/` implementing `TestPlugin`.
2. Create `XXX_Handler.h/cpp` in `src/consumer/` implementing `IWorkUnitHandler`.
3. Register in `producer.cpp::init_plugin()` with a `test_type_` check.
4. Register in `consumer.cpp` constructor with a `handler_type` check.
5. Add to `consumer_lib` in `CMakeLists.txt`.

### Consumer — `IWorkUnitHandler`

Pure virtual interface (`include/consumer/work_unit_handler.h`):
- `type()` — returns handler type string
- `handle(work)` — processes a `WorkUnitMessage`, returns `ResultMessage`
- `configure(config_path)` — optional handler configuration (default no-op)

### Consumer — `IResultSink`

Pure virtual interface (`include/consumer/result_sink.h`):
- `type()` — returns sink type string
- `on_result(result)` — receives a result for persistence
- `should_stop()` — returns `true` if the sink wants to halt processing
- `summary()` — returns JSON summary of collected results

`FileResultSink` (`include/consumer/file_result_sink.h`) writes JSON lines to a
file, tracking total/success/failure counts. Supports stopping criteria:
`max_failures` (stop after N failures) and `max_duration_sec` (stop after N seconds).
Created automatically with a default path when no `--result-file` is specified.

### PWD_NextUnit

Legacy C-style class in `src/producer/PWD_NextUnit.h/cpp` — uses `#define`
constants, raw arrays, no `std::` prefixes. Wrapped by `PWD_plugin.cpp` which
implements the `TestPlugin` interface.

## 15. Graceful Shutdown

Both applications handle SIGINT (Ctrl+C) and SIGTERM:

1. Signal handler sets an atomic `stop_requested` flag.
2. Main thread detects the flag and initiates shutdown sequence.
3. Worker threads finish current work and exit.
4. Sockets are closed, resources are released.
5. Final statistics are printed to stdout.

### Producer Shutdown

- Stops accepting new work requests.
- Writes checkpoint immediately (primary + backup), including `plugin_state`.
- I/O threads finish in-flight frames and close.
- In-progress work units are left as `sent` in the checkpoint for re-dispatch on resume.
- Connection monitor stops, stale consumers are not reclaimed during shutdown.

### Consumer Shutdown

- Receiver thread stops, signals work queue to shut down.
- Heartbeat thread stops.
- Pool threads finish current work units and send results.
- Uncompleted work units are sent back as `"failure"` results.

### Statistics

**Producer prints on exit:**
- Test type
- Total work units generated
- Total work units dispatched
- Total work units completed
- Total work units failed
- Total work units pending
- Password found / File error (PWD mode)
- Checkpoint file path

**Consumer prints on exit:**
- Total work units received
- Total work units completed successfully
- Total work units failed
- Total work units discarded (duplicates, invalid)
- Consumer ID
- Sequence range
- Result sink summary

## 16. Phases

### Phase 1 — Core (COMPLETED)
- [x] CMake project with platform conditionals
- [x] Shared common library (message, queue, socket, checkpoint, util, archive_validator)
- [x] Producer: JSON config, plugin architecture, work unit tracking, multi-Consumer, checkpoint
- [x] Consumer: thread pool, work requests, result reporting, file download, handler architecture
- [x] TCP/UDP length-prefixed JSON framing
- [x] Five message types: `version`, `work_unit`, `result`, `work_request`, `heartbeat`
- [x] CLI argument parsing for both apps
- [x] Graceful shutdown with statistics and checkpoint
- [x] Plugin architecture: `TestPlugin` dispatch table, PWD and BENCH plugins
- [x] Consumer handler architecture: `IWorkUnitHandler`, `IResultSink`, `FileResultSink`
- [x] Heartbeat protocol: 5s interval, 30s stale detection
- [x] Consumer registration and disconnect tracking with work unit reclamation
- [x] Work request throttling (max 1/50ms) + idle safety net
- [x] Duplicate work_unit_id tracking (LRU cache, 3000 entries)
- [x] Archive validation via libarchive (ZIP, RAR, 7Z)
- [x] SHA-256 (pure C++ RFC 6234)
- [x] Producer `--max-time` and consumer `--timeout` shutdown options
- [x] Unit tests for message, queue, work tracker, checkpoint, util, thread pool, echo, socket
- [x] End-to-end integration tests (spawn real producer + consumer, verify full cycle and timeout shutdown)
- [x] 110/110 tests passing

### Phase 2 — Extensions (future)
- [ ] Actual task execution (rendering, encoding, etc.)
- [ ] Consumer-to-Consumer load balancing
- [ ] Persistent result storage
- [ ] WebSocket or HTTP/2 transport option
- [ ] Performance benchmarking suite
- [ ] Dashboard / telemetry endpoint
- [x] UDP transport integration (producer udp_loop, consumer UDP send/recv, version handshake over UDP) (producer udp_loop, consumer UDP send/recv, version handshake over UDP)
- [x] Default `IResultSink` implementation

## 17. Acceptance Criteria

- [x] `cmake -B build && cmake --build build` succeeds on Windows (MSVC)
- [x] `cmake -B build && cmake --build build` succeeds on Linux (GCC/Clang)
- [x] Producer exits with code 1 when `--file` is missing or invalid
- [x] Producer reads, validates, and initializes from JSON config file
- [x] Producer tracks all work units through their lifecycle
- [x] Producer removes completed work units from pending list on result
- [x] Producer re-dispatches work units when Consumer disconnects
- [x] Producer accepts multiple simultaneous Consumer connections
- [x] Producer responds to `work_request` with available work units
- [x] Producer handles `heartbeat` messages and detects stale consumers after 30s
- [x] Checkpoint file written every 60s and on shutdown; backup maintained
- [x] Checkpoint includes `plugin_state` for plugin-specific resume
- [x] `--resume` restores state and continues from last completed seq
- [x] Consumer thread pool defaults to 1 thread per core
- [x] Consumer sends `work_request` on connect and when threads are idle
- [x] Consumer sends `result` back after each work unit
- [x] Consumer downloads source file when not available locally
- [x] Consumer sends heartbeat every 5s
- [x] Consumer throttles work requests to max 1 per 50ms
- [x] Consumer tracks completed work_unit_ids in LRU cache (3000 entries)
- [x] All five message types are valid JSON with required fields
- [x] Sequence numbers are monotonically increasing
- [x] Ctrl+C shuts down both apps cleanly within 5 seconds
- [x] Final statistics are printed to stdout
- [x] All unit and integration tests pass (110/110)
- [x] PWD plugin: generates password permutations, reports found password
- [x] BENCH plugin: generates file chunk work units
- [x] FileResultSink writes JSON lines to file
- [x] ArchiveValidator validates ZIP, RAR, 7Z archives with libarchive
