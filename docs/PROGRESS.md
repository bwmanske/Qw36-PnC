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
- [x] 111/111 tests passing (message, queue, work_tracker, checkpoint, integration, pwd_next_unit, sha256, file_result_sink, util, thread_pool, echo, socket)
- [x] Test libraries: `producer_lib`, `consumer_lib` for test linking

### Linux Build Verification (WSL2)
- [x] Linux build verified on WSL2 Ubuntu (CMake 4.2.3, g++ 15.2.0): configure + build succeed with 0 errors; all 12 test executables + `producer` + `consumer` produced
- [x] Cross-platform fixes (all guarded so Windows behavior is unchanged):
  - `CMakeLists.txt` — `ENABLE_CNG` now platform-conditional (ON on Windows, OFF elsewhere); link the `archive_static` target instead of the hardcoded MSVC `Release/archive.lib` path
  - `include/common/util.h` — added `<cstdint>`/`<cstddef>` (`uint8_t`/`size_t` on GCC)
  - `include/common/queue.h` — added `<stdexcept>`
  - `src/common/socket.cpp` — fixed 3 string-concat errors in the POSIX branch; `close()` now calls `shutdown(SHUT_RDWR)` before `close()` on POSIX (a bare `close()` does not unblock a blocked `accept()`); `bind()` sets `SO_REUSEADDR` on POSIX (allows re-binding a port whose prior connection is in FIN-WAIT/TIME_WAIT)
  - `src/consumer/consumer.cpp` — `<arpa/inet.h>`/`<unistd.h>` for POSIX (`htonl`/`ntohl`/`gethostname`)
  - `src/producer/producer.cpp` — `<arpa/inet.h>` for POSIX; `run()` now creates/binds the listening sockets **before** starting worker threads (fixes a race where `file_transfer_loop` accepted on an unbound socket → busy-loop flood); `dispatcher_loop()` closes the server socket after the loop (covers every stop path)
  - `src/producer/work_tracker.cpp` — `get_pending()` now returns entries in `seq` order (FIFO) instead of `unordered_map` hash order
  - `tests/test_checkpoint.cpp` — `Paths` test uses a writable temp dir instead of `/test/dir` (root-level, permission-denied on Linux)
- [x] 11 of 12 test suites pass on Linux (104 tests); manual end-to-end runs (real `producer` + `consumer`, ECHO) work perfectly: both exit 0, no file-transfer flood, 5/5 results with hash match, checkpoint written
- [ ] `test_integration` hangs when run after other test binaries — see **Known Issues** (on hold)

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
- [x] Consumer shutdown returns in-progress units as `"failure"` results
- [x] Default `IResultSink` implementation (file-based result storage)

### Medium Priority
- [x] UDP transport integration in producer/consumer logic
- [x] End-to-end integration test (spawn producer + consumer, verify full cycle) — `Integration.EndToEnd_ECHO_FullCycle` + `Integration.EndToEnd_TimeoutShutdown` + `Integration.EndToEnd_ECHO_UDP_FullCycle` (UDP transport)
- [x] Stopping criteria in `IResultSink` (max failures, time limit, etc.)
- [x] Additional handler types (XXX_NextUnit + XXX_Handler pairs)
- [x] Producer `--max-time DUR` and consumer `--timeout SEC` shutdown options (`parse_duration` in `common/util`)
- [x] Consumer idle safety net — main loop requests work when pool is fully idle (`ThreadPool::queue_empty()`)
- [ ] `PWD_NextUnit` checkpoint state serialization for resume

### Low Priority
- [x] Linux build verification — build + 11/12 suites pass on WSL2; one known issue (`test_integration` suite-context hang) documented under **Known Issues** (on hold)
- [ ] Performance benchmarking
- [ ] Dashboard / telemetry endpoint
- [ ] WebSocket or HTTP/2 transport option

## Known Issues

### [ON HOLD] `test_integration` hangs when run after other test binaries (Linux)

**Status:** On hold — documented 2026-08-23 during Linux build verification. Root cause not yet identified. Windows unaffected (111/111 passing).

**Symptom**
- `test_integration` passes 100% when run **in isolation** (verified repeatedly, with and without `stdbuf`).
- When run **after other test binaries** (the normal full-suite sequence), it hangs 100% at `Integration.EndToEnd_ECHO_FullCycle` (5th of 7 tests) and is killed by the runner's 60 s timeout (exit 124).
- The other 5 tests in the binary pass; the hang is specific to the fork+exec end-to-end test.

**What the test does** (`tests/test_integration.cpp`, `Integration.EndToEnd_ECHO_FullCycle`)
1. Forks the real `producer` (`--port 19876 --max-time 30s`, ECHO plugin, 5 units).
2. Sleeps 1 s, forks the real `consumer` (`--max-messages 5 --timeout 30`).
3. Calls raw `waitpid(pid, &status, 0)` on both — **no timeout**. If either child fails to exit, the test blocks forever.

**Facts established during diagnosis**
1. Running the *exact* producer/consumer commands manually (same port 19876, same flags) works perfectly: producer exit 0, consumer exit 0, 5/5 results with hash match, checkpoint written. The producer/consumer logic is correct.
2. No shared state from the preceding tests: after running `test_message`, `test_queue`, `test_work_tracker`, `test_checkpoint`, there are **no** leftover `/tmp` files, **no** ports in TIME_WAIT, and **no** orphan processes.
3. Yet running those 4 binaries before `test_integration` reproduces the hang 100%; running `test_integration` alone passes 100%.
4. The consumer's connect retry is bounded (30 attempts × 5 s, then `exit(2)`) — it cannot hang forever on connect.
5. The producer's `dispatcher_loop` checks `--max-time` every 100 ms — even if the plugin never exhausted, the producer should exit within ~30 s.
6. `SO_REUSEADDR` was added to `Socket::bind()` (POSIX) during this investigation: back-to-back isolated runs now pass 3/3 (a second run previously could fail to re-bind over the first run's FIN-WAIT/TIME_WAIT). This fixed the isolated re-bind case but **not** the suite-context hang.
7. The hang is not `stdbuf`-related (isolated runs with `stdbuf -oL -eL` pass).

**Suspects / next diagnostic steps**
1. **Attach to the hung children:** run the suite sequence, and while `EndToEnd_ECHO_FullCycle` is hanging, inspect the forked producer/consumer: `/proc/<pid>/wchan`, `/proc/<pid>/stack`, `ls -l /proc/<pid>/fd`, and `strace -p <pid>` to see exactly where each is blocked.
2. **Bisect the preceding tests:** run `test_integration` after each single preceding test (message, queue, work_tracker, checkpoint) to find which one triggers the hang.
3. **Fork+exec context:** the prime suspect is something about forking from the gtest process after other binaries have run (kernel/TCP stack state, timing/load). Note the in-binary state is identical in both cases (the first 4 in-binary tests run before the fork either way), so the difference is purely "other processes ran first".
4. **Port-specific kernel state:** try a different fixed port (e.g. 19890) to rule out port-specific state.
5. **Test robustness (independent of root cause):** `EndToEnd_ECHO_FullCycle` uses raw `waitpid()` with no timeout, whereas `EndToEnd_TimeoutShutdown` uses `wait_bounded()` (30 s timeout + SIGKILL). Switching the former to `wait_bounded()` would turn the hang into a clean, diagnostic test failure and stop one flaky run from stalling the whole suite.

**Workaround for now:** run `test_integration` in isolation (it passes reliably):
```bash
build/tests/test_integration                 # Linux
build\tests\Release\test_integration.exe     # Windows
```

## Open Questions

1. **ResultSink default behavior** — ~~Should the default sink write results to a file, stdout, or both? What format (JSON lines, CSV, structured)?~~ **RESOLVED**: File-based JSONL with auto-generated path.
2. **Stopping criteria** — ~~What conditions should trigger the Consumer to stop? Options: max failures, max duration, all work consumed, explicit signal from Producer.~~ **RESOLVED**: `max_failures` and `max_duration_sec` via `FileResultSink`.
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
# Or run individual test executables directly (ctest does NOT discover tests on Windows):
build/tests/Release/test_message.exe
build/tests/Release/test_queue.exe
build/tests/Release/test_work_tracker.exe
build/tests/Release/test_checkpoint.exe
build/tests/Release/test_integration.exe
build/tests/Release/test_pwd_next_unit.exe
build/tests/Release/test_sha256.exe
build/tests/Release/test_file_result_sink.exe
build/tests/Release/test_util.exe
build/tests/Release/test_thread_pool.exe
build/tests/Release/test_echo.exe
build/tests/Release/test_socket.exe
```

## File Tree

```
project/
├── CMakeLists.txt
├── docs/
│   ├── BUILDING.md
│   ├── COMMUNICATION.md
│   ├── IMPLEMENTATION.md
│   ├── PROGRESS.md              ← this file
│   ├── TESTING.md
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
│   │   ├── ECHO_Handler.h
│   │   └── file_result_sink.h   ← IResultSink implementation
│   └── producer/
│       ├── producer.h           ← plugin architecture
│       ├── work_tracker.h
│       ├── work_unit_generator.h ← IWorkUnitGenerator interface
│       ├── test_plugin.h        ← TestPlugin dispatch table
│       ├── PWD_plugin.h
│       ├── BENCH_plugin.h
│       └── ECHO_plugin.h
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
│   │   ├── ECHO_Handler.cpp     ← ECHO echo/verification handler
│   │   ├── ECHO_Handler.h
│   │   ├── file_result_sink.cpp ← JSON lines + sink_stats
│   │   └── thread_pool.cpp      ← dispatches through IWorkUnitHandler
│   └── producer/
│       ├── main.cpp             ← --test-type TYPE, default data dir
│       ├── producer.cpp         ← plugin architecture, on-demand generation
│       ├── PWD_NextUnit.cpp     ← renamed from permutations.cpp
│       ├── PWD_NextUnit.h       ← renamed from permutations.h
│       ├── PWD_plugin.cpp       ← PWD_StartUp, PWD_NextUnit, PWD_CheckPoint, PWD_ExitConditions
│       ├── BENCH_plugin.cpp     ← BENCH_StartUp, BENCH_NextUnit, BENCH_CheckPoint, BENCH_ExitConditions
│       ├── ECHO_plugin.cpp      ← ECHO_StartUp, ECHO_NextUnit, ECHO_CheckPoint, ECHO_ExitConditions
│       └── work_tracker.cpp
└── tests/
    ├── CMakeLists.txt
    ├── test_checkpoint.cpp
    ├── test_echo.cpp
    ├── test_file_result_sink.cpp
    ├── test_integration.cpp
    ├── test_message.cpp
    ├── test_pwd_next_unit.cpp
    ├── test_queue.cpp
    ├── test_sha256.cpp
    ├── test_socket.cpp
    ├── test_thread_pool.cpp
    ├── test_util.cpp
    └── test_work_tracker.cpp
```
