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
- [x] 127/127 tests passing (message, queue, work_tracker, checkpoint, integration, pwd_next_unit, sha256, file_result_sink, util, thread_pool, echo, bench, socket)
- [x] Test libraries: `producer_lib`, `consumer_lib` for test linking

### Linux Build Verification (WSL2)
- [x] Linux build verified on WSL2 Ubuntu (CMake 4.2.3, g++ 15.2.0): configure + build succeed with 0 errors; all 13 test executables + `producer` + `consumer` produced
- [x] Cross-platform fixes (all guarded so Windows behavior is unchanged):
  - `CMakeLists.txt` — `ENABLE_CNG` now platform-conditional (ON on Windows, OFF elsewhere); link the `archive_static` target instead of the hardcoded MSVC `Release/archive.lib` path
  - `include/common/util.h` — added `<cstdint>`/`<cstddef>` (`uint8_t`/`size_t` on GCC)
  - `include/common/queue.h` — added `<stdexcept>`
  - `src/common/socket.cpp` — fixed 3 string-concat errors in the POSIX branch; `close()` now calls `shutdown(SHUT_RDWR)` before `close()` on POSIX (a bare `close()` does not unblock a blocked `accept()`); `bind()` sets `SO_REUSEADDR` on POSIX (allows re-binding a port whose prior connection is in FIN-WAIT/TIME_WAIT)
  - `src/consumer/consumer.cpp` — `<arpa/inet.h>`/`<unistd.h>` for POSIX (`htonl`/`ntohl`/`gethostname`)
  - `src/producer/producer.cpp` — `<arpa/inet.h>` for POSIX; `run()` now creates/binds the listening sockets **before** starting worker threads (fixes a race where `file_transfer_loop` accepted on an unbound socket → busy-loop flood); `dispatcher_loop()` closes the server socket after the loop (covers every stop path)
  - `src/producer/work_tracker.cpp` — `get_pending()` now returns entries in `seq` order (FIFO) instead of `unordered_map` hash order
  - `tests/test_checkpoint.cpp` — `Paths` test uses a writable temp dir instead of `/test/dir` (root-level, permission-denied on Linux)
- [x] All 13 test suites pass on Linux (127 tests); manual end-to-end runs (real `producer` + `consumer`, ECHO) work perfectly: both exit 0, no file-transfer flood, 5/5 results with hash match, checkpoint written
- [x] `test_integration` suite-context hang **fixed** — `find_executable` had an unbounded directory-walk loop (root cause + fix in **Known Issues**)

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

### Producer Status Display & Checkpoint Placeholders
- [x] `TestPlugin.status` — optional 5th member ("output plugin"); each plugin returns its own status lines for the console display
- [x] In-place producer console status (1 Hz, multi-line block): common metrics (elapsed, **WU/s = generated/elapsed**, generated, dispatched, completed, failed, pending, consumers) + per-plugin section
  - PWD → `Seq`, `Max len`; BENCH → `Offset`/`file_size` (%), `Chunk size`; ECHO → `Generated`/`total`, `Payload` size
  - Rendered in place via ANSI (`\033[<N>A` cursor-up + `\033[2K` clear-line); Windows enables `ENABLE_VIRTUAL_TERMINAL_PROCESSING` at startup
  - `status_mutex_` serializes the status render against scrolling event logs (all `std::cout` event logs routed through `Producer::log()`) so output never tears
- [x] `--no-status` CLI flag disables the status block (clean output for file/CI capture)
- [x] Consistent checkpoint placeholders — all three `checkpoint()` routines return minimal key resumable state (PWD: `seq`/`testPwdLen`/`charIndicies`; BENCH: `offset`/`seq`; ECHO: `generated`/`seq`)
- [x] Progress counters made `std::atomic` (PWD `seq`; BENCH `offset`/`seq`; ECHO `generated`/`seq`) so the status thread reads them safely
- [x] `Producer::shutdown()` made idempotent (`shutdown_done_` guard) — fixes pre-existing double "Checkpoint written" + "Statistics" output (it was called from both `run()` and the destructor)

### PWD Checkpoint Serialization (real resume)
- [x] `PWD_NextUnit` accessors — added `get_charIndicies(int)`, `get_permuteStatus()`, `set_permuteStatus(int)` (the class previously only had a per-index setter, so `checkpoint()` could not read the real odometer state)
- [x] PWD `checkpoint()` now saves the real generator position: `charIndicies[0..9]` (was zero-filled), `testPwdLen`, `permuteStatus`, `seq`
- [x] PWD `startup()` restores `permuteStatus` in addition to `charIndicies`/`testPwdLen`/`seq` — an exhausted (`PERMUTE_DONE`) generator stays exhausted on resume
- [x] Tests — `GetCharIndicies_RoundTrip`, `PermuteStatus_DefaultSuccess`, `PermuteStatus_SetGet`, `CheckpointRoundTrip_FullState` (multi-digit position round-trips to the identical next password), `CheckpointRoundTrip_DoneStaysDone`
- [x] E2E verified — real producer+consumer PWD run: checkpoint holds real indicies (e.g. `charIndicies[0]=23`='x', `seq=24`); `--resume` continues seamlessly from 'x' → 'y','z' → 2-char wrap, `seq` 24→48 with no reset or gap

### Localhost Consumer CPU Yield
- [x] Rationale — the producer services each consumer on a dedicated I/O thread (no central dispatch bottleneck), so the only cross-connection contention is CPU. A CPU-heavy local PWD consumer (`ArchiveValidator::validate`) can saturate the box and delay the producer's I/O bursts → remote TCP backpressure. Lowering the local consumer's priority lets the producer's bursts win under saturation.
- [x] `common/util` helpers — `is_localhost_host(host)` (true for `localhost`/`::1`/any `127.0.0.0/8`) and `set_process_priority_below_normal()` (Windows: `BELOW_NORMAL_PRIORITY_CLASS`; Linux: `setpriority(PRIO_PROCESS, 0, 5)` — nice `+5` approximates Windows *below normal*; raising nice needs no elevated privileges)
- [x] Consumer auto-yields — at the top of `Consumer::run()` (before worker threads start, so they inherit it), a consumer whose effective host is localhost lowers its process priority and logs it
- [x] `--no-yield` CLI flag — sets `ConsumerConfig::yield_cpu = false` to opt out (e.g. local-only runs wanting max local throughput)
- [x] Producer priority intentionally unchanged (it is I/O-bound; raising it buys little and risks priority inversion with disk/network)
- [x] Tests — `IsLocalhostHost.*` (loopback v4/v6, name, non-local, malformed) + `ProcessPriority.SetBelowNormal` (set → assert → restore; skips if the OS call is unavailable)

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
- [x] `PWD_NextUnit` checkpoint state serialization for resume — PWD `checkpoint()` now saves the real generator position (`charIndicies[0..9]` via new `get_charIndicies()`, `testPwdLen`, `permuteStatus` via new `get_permuteStatus()`); `startup()` restores all three (new `set_permuteStatus()`). Verified e2e: checkpoint holds real indicies and `--resume` continues seamlessly (no reset/gap).

### Low Priority
- [x] Linux build verification — build + **12/12 suites pass** on WSL2 (the `test_integration` suite-context hang was root-caused and fixed; see **Known Issues**)
- [ ] Performance benchmarking
- [ ] Dashboard / telemetry endpoint
- [ ] WebSocket or HTTP/2 transport option

## Known Issues

### [RESOLVED 2026-08-27] `test_integration` hangs when run after other test binaries (Linux)

**Status:** Fixed. Root cause identified and corrected in `tests/test_integration.cpp::find_executable`. Verified: full suite (4 preceding binaries + `test_integration`) passes 7/7 on WSL2, and Windows 12/12 suites still pass.

**Symptom (original)**
- `test_integration` hung 100% at `Integration.EndToEnd_ECHO_FullCycle` (5th of 7 tests) when run after the other test binaries, killed by the runner's 60 s timeout (exit 124). The other 5 tests passed.

**Root cause**
The hang was **not** in the forked producer/consumer or in `waitpid`. The test process itself spun at ~100% CPU (1 thread, no children, only 3 fds open, ~8 voluntary context switches) in a tight **userspace** loop, *before* forking. The culprit was the `find_executable()` helper's directory walk:

```cpp
fs::path dir = fs::current_path();
while (true) {
    ...
    if (!dir.has_parent_path()) break;
    dir = dir.parent_path();
}
```

Two defects combined:
1. **Unbounded loop on this toolchain.** On the WSL2 toolchain (Ubuntu 26.04, g++ 15.2.0, glibc 2.43), `std::filesystem::path::parent_path()` of the root `/` returns `/` and `has_parent_path()` of `/` returns `true`. So once the walk reached the root, `dir` never changed and the `while(true)` loop spun forever. (Reproduced in isolation with a minimal `parent_path()`/`has_parent_path()` walk: 100,000+ iterations at `dir="/" has_parent=1`.)
2. **Search path missed the Linux build layout.** The old code only checked `dir/producer` and `dir/build/Release/producer`. The Linux single-config build places the executables at `dir/build/producer`, which was never checked — so the walk always ran all the way to the root (and then spun).

The "passes in isolation / hangs in the suite" distinction tracked the **CWD**: when the CWD is such that the producer is found before the root (e.g. under `build/`), the loop returns early; when it is not (e.g. CWD = project root with the old search path), the walk reaches the root and spins.

**Fix** (`tests/test_integration.cpp::find_executable`)
- Check the directory itself **and** `build/Release`, `build/Debug`, and `build` (covers Windows multi-config and Linux single-config layouts), so the producer/consumer are found on both platforms.
- Make the walk robust so it can never spin: break when `parent_path()` makes no progress (`parent == dir`) **and** keep a hard 1000-iteration cap as a safety net.

**Verification**
- Linux (WSL2): full suite sequence (test_message, test_queue, test_work_tracker, test_checkpoint, then test_integration) → all pass; `EndToEnd_ECHO_FullCycle` completes in ~10 s. `test_integration` in isolation also passes 7/7.
- Windows: full build + all 12 suites pass (no regression).

## Open Questions

1. **ResultSink default behavior** — ~~Should the default sink write results to a file, stdout, or both? What format (JSON lines, CSV, structured)?~~ **RESOLVED**: File-based JSONL with auto-generated path.
2. **Stopping criteria** — ~~What conditions should trigger the Consumer to stop? Options: max failures, max duration, all work consumed, explicit signal from Producer.~~ **RESOLVED**: `max_failures` and `max_duration_sec` via `FileResultSink`.
3. **PWD_NextUnit integration** — Should the Producer use `PWD_NextUnit` as a drop-in replacement for the job-file-based dispatch, or should it be an alternative mode (`--test-type PWD`)?
4. **Handler discovery** — Should handlers be compiled-in (current approach) or loadable via plugins/dlls?
5. **Checkpoint for PWD tests** — ~~The `PWD_NextUnit` class has internal state (charIndicies, testPwdLen). Should this state be serialized into the checkpoint so the Producer can resume mid-permutation?~~ **RESOLVED**: Yes — `checkpoint()` now saves the real odometer state (`charIndicies[0..9]`, `testPwdLen`, `permuteStatus`) and `startup()` restores it, so `--resume` continues mid-permutation without reset.
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
