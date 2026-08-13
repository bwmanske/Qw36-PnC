# Project Progress

## Completed

### Specifications
- [x] `specs/spec.md` — Umbrella specification (3-message protocol, work unit lifecycle, checkpoint, multi-consumer)
- [x] `specs/Producer-Spec.md` — Producer detailed spec (job file, permutation, work tracking, checkpoint/resume)
- [x] `specs/Consumer-Spec.md` — Consumer detailed spec (thread pool, work requests, result reporting, file download)

### Documentation
- [x] `docs/IMPLEMENTATION.md` — Developer guide (file structure, build commands, platform conditionals, class designs)
- [x] `docs/USER_GUIDE.md` — End-user guide (CLI options, quick start, multi-consumer, troubleshooting)

### Project Scaffolding
- [x] CMake build system with FetchContent (nlohmann/json, GTest)
- [x] Common library: `message.h/cpp`, `queue.h/cpp`, `socket.h/cpp`, `signal_handler.cpp`, `checkpoint.h/cpp`
- [x] Producer library: `producer.h/cpp`, `work_tracker.h/cpp`, `main.cpp`
- [x] Consumer library: `consumer.h/cpp`, `thread_pool.h/cpp`, `main.cpp`
- [x] Platform conditionals: WinSock2/POSIX sockets, signal handling, hostname
- [x] Length-prefixed TCP framing (4-byte big-endian + JSON payload)
- [x] Three message types: `work_unit`, `result`, `work_request`
- [x] Work unit tracking: pending → sent → completed/failed lifecycle
- [x] Checkpoint save/load with backup and corrupt recovery
- [x] Consumer thread pool with idle callbacks and result callbacks
- [x] Multi-consumer support (dedicated I/O thread per connection)
- [x] Re-dispatch on consumer disconnect

### Build & Test
- [x] Windows build (MSVC): `producer.exe` + `consumer.exe`
- [x] 66/67 unit tests passing (message, queue, work_tracker, checkpoint, integration, pwd_next_unit, sha256, file_result_sink)
- [x] Test libraries: `producer_lib`, `consumer_lib` for test linking

### Pluggable Handler Architecture
- [x] Renamed `permutations.*` → `PWD_NextUnit.*` (class: `Permutations` → `PWD_NextUnit`)
- [x] `include/producer/work_unit_generator.h` — `IWorkUnitGenerator` interface
- [x] `include/consumer/work_unit_handler.h` — `IWorkUnitHandler` interface
- [x] `include/consumer/result_sink.h` — `IResultSink` interface (saving, stopping criteria)
- [x] `src/consumer/PWD_Handler.h/cpp` — PWD handler implementation
- [x] `test_type` field added to `WorkUnitMessage`
- [x] `--handler TYPE` CLI flag on consumer
- [x] `--test-type TYPE` CLI flag on producer
- [x] Thread pool dispatches through `IWorkUnitHandler`
- [x] Result callback chains through `IResultSink`

### File Transfer & Verification
- [x] `include/common/util.h` + `src/common/util.cpp` — SHA-256 (pure C++ RFC 6234) + `get_data_directory()`
- [x] File transfer protocol over `port + 1`: Consumer sends `0x01` + filename, Producer responds with length-prefixed file
- [x] Producer file transfer server: listens on `port + 1`, resolves file from job dir, sends raw bytes
- [x] Consumer file download: connects to `port + 1`, receives length-prefixed file, writes to `--file-dir`
- [x] SHA-256 hash included in `WorkUnitMessage.source_hash` by producer
- [x] Consumer verifies local file hash against `source_hash`; re-downloads on mismatch
- [x] Checkpoint data directory defaults to `%APPDATA%\Producer\` (Win) / `~/.local/share/producer/` (Linux)

## Remaining

### High Priority
- [x] File transfer protocol (port+1) — Consumer downloads source file from Producer
- [x] SHA-256 file hash verification — pure C++ RFC 6234, cross-platform
- [x] Checkpoint data directory (`%AppData%\Roaming\Producer\` on Win, `~/.local/share/producer/` on Linux)
- [x] Plugin architecture — `TestPlugin` struct with `startup`, `next_unit`, `checkpoint`, `exit_conditions`
- [x] PWD plugin — `PWD_StartUp`, `PWD_NextUnit`, `PWD_CheckPoint`, `PWD_ExitConditions`
- [x] BENCH plugin — `BENCH_StartUp`, `BENCH_NextUnit`, `BENCH_CheckPoint`, `BENCH_ExitConditions`
- [x] BENCH_Handler — consumer-side chunk comparison with base64 decode + SHA-256 verify
- [x] Producer refactored — reads main JSON config, instantiates plugin, on-demand work generation
- [x] Checkpoint extended — `plugin_state` field for plugin-specific resume data
- [x] Work request throttling (max 1 per 50ms, per consumer)
- [x] Consumer disconnect detection — TCP keepalive or application-level heartbeat
- [x] Consumer registration — populate `connected_consumers_` on first message, remove on disconnect
- [x] Disconnect logging — log consumer disconnect + count of reclaimed work units
- [x] Socket recv timeout — bounded detection of dead connections (no infinite recv block)
- [x] Duplicate `work_unit_id` tracking in consumer — LRU cache (3000 entries, list + unordered_set)
- [ ] Consumer shutdown returns in-progress units as `"failure"` results
- [ ] Default `IResultSink` implementation (file-based result storage)

### Medium Priority
- [ ] UDP transport integration in producer/consumer logic
- [ ] End-to-end integration test (spawn producer + consumer, verify full cycle)
- [ ] Stopping criteria in `IResultSink` (max failures, time limit, etc.)
- [ ] Additional handler types (XXX_NextUnit + XXX_Handler pairs)
- [ ] `PWD_NextUnit` checkpoint state serialization for resume

### Low Priority
- [ ] Linux build verification
- [ ] Performance benchmarking
- [ ] Dashboard / telemetry endpoint
- [ ] WebSocket or HTTP/2 transport option

## Open Questions

1. **ResultSink default behavior** — Should the default sink write results to a file, stdout, or both? What format (JSON lines, CSV, structured)?
2. **Stopping criteria** — What conditions should trigger the Consumer to stop? Options: max failures, max duration, all work consumed, explicit signal from Producer.
3. **PWD_NextUnit integration** — Should the Producer use `PWD_NextUnit` as a drop-in replacement for the job-file-based dispatch, or should it be an alternative mode (`--test-type PWD`)?
4. **Handler discovery** — Should handlers be compiled-in (current approach) or loadable via plugins/dlls?
5. **Checkpoint for PWD tests** — The `PWD_NextUnit` class has internal state (charIndicies, testPwdLen). Should this state be serialized into the checkpoint so the Producer can resume mid-permutation?
6. **Consumer result aggregation** — Should the Consumer aggregate results before sending to Producer, or send each result individually (current approach)?
7. **Multiple handlers per Consumer** — Should a single Consumer process multiple test types, or be dedicated to one handler type?

## Build Commands

```bash
# Configure (with tests)
cmake -B build -DBUILD_TESTS=ON

# Configure (without tests)
cmake -B build -DBUILD_TESTS=OFF

# Build Release
cmake --build build --config Release

# Run tests
ctest --test-dir build -C Release
# Or run individual test executables directly:
build/tests/Release/test_message.exe
build/tests/Release/test_queue.exe
build/tests/Release/test_work_tracker.exe
build/tests/Release/test_checkpoint.exe
build/tests/Release/test_integration.exe
build/tests/Release/test_pwd_next_unit.exe
build/tests/Release/test_sha256.exe
build/tests/Release/test_file_result_sink.exe
```

## File Tree

```
project/
├── CMakeLists.txt
├── docs/
│   ├── IMPLEMENTATION.md
│   ├── PROGRESS.md              ← this file
│   └── USER_GUIDE.md
├── specs/
│   ├── spec.md
│   ├── Producer-Spec.md
│   └── Consumer-Spec.md
├── include/
│   ├── common/
│   │   ├── checkpoint.h
│   │   ├── message.h            ← test_type field added
│   │   ├── queue.h
│   │   ├── signal_handler.h
│   │   ├── socket.h
│   │   ├── archive_validator.h  ← libarchive wrapper
│   │   ├── types.h
│   │   └── util.h               ← SHA-256 + get_data_directory()
│   ├── consumer/
│   │   ├── consumer.h           ← handler_type, handler_, sink_
│   │   ├── result_sink.h        ← IResultSink interface
│   │   ├── thread_pool.h        ← set_handler()
│   │   ├── work_unit_handler.h  ← IWorkUnitHandler interface
│   │   ├── PWD_Handler.h
│   │   ├── BENCH_Handler.h
│   │   └── file_result_sink.h   ← IResultSink implementation
│   └── producer/
│       ├── producer.h           ← plugin architecture
│       ├── work_tracker.h
│       ├── work_unit_generator.h ← IWorkUnitGenerator interface
│       ├── test_plugin.h        ← TestPlugin dispatch table
│       ├── PWD_plugin.h
│       └── BENCH_plugin.h
├── src/
│   ├── common/
│   │   ├── checkpoint.cpp
│   │   ├── message.cpp          ← test_type serialization
│   │   ├── queue.cpp
│   │   ├── signal_handler.cpp
│   │   ├── socket.cpp
│   │   ├── util.cpp             ← SHA-256 + platform data dir
│   │   └── archive_validator.cpp ← libarchive wrapper
│   ├── consumer/
│   │   ├── consumer.cpp         ← handler, file transfer, SHA-256 verify
│   │   ├── main.cpp             ← --handler TYPE flag
│   │   ├── PWD_Handler.cpp      ← PWD work unit handler
│   │   ├── PWD_Handler.h
│   │   ├── BENCH_Handler.cpp    ← BENCH chunk comparison handler
│   │   ├── BENCH_Handler.h
│   │   ├── file_result_sink.cpp ← JSON lines + sink_stats
│   │   └── thread_pool.cpp      ← dispatches through IWorkUnitHandler
│   └── producer/
│       ├── main.cpp             ← --test-type TYPE, default data dir
│       ├── producer.cpp         ← plugin architecture, on-demand generation
│       ├── PWD_NextUnit.cpp     ← renamed from permutations.cpp
│       ├── PWD_NextUnit.h       ← renamed from permutations.h
│       ├── PWD_plugin.cpp       ← PWD_StartUp, PWD_NextUnit, PWD_CheckPoint, PWD_ExitConditions
│       ├── BENCH_plugin.cpp     ← BENCH_StartUp, BENCH_NextUnit, BENCH_CheckPoint, BENCH_ExitConditions
│       └── work_tracker.cpp
└── tests/
    ├── CMakeLists.txt
    ├── test_checkpoint.cpp
    ├── test_file_result_sink.cpp
    ├── test_integration.cpp
    ├── test_message.cpp
    ├── test_pwd_next_unit.cpp
    ├── test_queue.cpp
    ├── test_sha256.cpp
    └── test_work_tracker.cpp
```
