# AGENTS.md — Producer-Consumer C++ Project

## Build & Test

Use the build scripts (`build.ps1` on Windows, `build.sh` on Linux). See `docs/BUILDING.md`.

```powershell
# Quick build + test (PowerShell)
.\build.ps1 -Target all

# Build only
.\build.ps1

# Run tests only
.\build.ps1 -Target test
```

```bash
# Quick build + test (Bash)
./build.sh all
```

**Manual cmake commands** (if scripts aren't available):

```powershell
cmake -B build -DBUILD_TESTS=ON
cmake --build build --config Release
```

Run tests directly (ctest does NOT discover tests on Windows):

```
build\tests\Release\test_message.exe
build\tests\Release\test_queue.exe
build\tests\Release\test_work_tracker.exe
build\tests\Release\test_checkpoint.exe
build\tests\Release\test_integration.exe
build\tests\Release\test_pwd_next_unit.exe
build\tests\Release\test_sha256.exe
build\tests\Release\test_file_result_sink.exe
build\tests\Release\test_util.exe
build\tests\Release\test_thread_pool.exe
build\tests\Release\test_echo.exe
build\tests\Release\test_socket.exe
```

**Debug / shutdown options** (useful when running the executables by hand):
- Producer `--max-time DUR` — stop after a duration; value may end in `s`/`m`/`h` (e.g. `30s`, `5m`, `1h`; bare number = seconds; `0` = no limit).
- Consumer `--timeout SEC` — close after N seconds with no producer communication (`0` = no limit).

## MSVC Gotchas

- Always `#define NOMINMAX` before `<windows.h>` — MSVC's `min`/`max` macros break `std::min`/`std::max`.
- Never name functions `send` or `recv` — WinSock2 defines them as macros. Use `send_data`/`recv_data` (already done in `socket.h/cpp`).
- `ssize_t` is not defined on Windows — the `common/` namespace provides a typedef in `socket.h`.
- `std::istreambuf_iterator` does not construct `std::vector<uint8_t>` reliably on MSVC — use `tellg` + `seekg` + `read` instead.

## Architecture

| Layer | Targets | Purpose |
|-------|---------|---------|
| `common` | `common` (static lib) | Sockets, messages, queue, checkpoint, SHA-256, signal handling, archive_validator |
| `producer_lib` | `producer_lib` (static lib) | Producer logic, work tracker, PWD_NextUnit, PWD/BENCH plugins |
| `consumer_lib` | `consumer_lib` (static lib) | Consumer logic, thread pool, PWD/BENCH handlers, FileResultSink |
| Exe | `producer`, `consumer` | CLI entry points |

Tests link against `common`, `producer_lib`, and/or `consumer_lib` — never the executables.

## Network Protocol

- **Control channel**: TCP on `--port`, length-prefixed JSON frames (4-byte big-endian `uint32_t` + UTF-8 payload).
- **File transfer**: TCP on `port + 1`. Consumer sends `0x01` + null-terminated filename. Producer responds with 4-byte big-endian file size + raw bytes. Size `0` means file not found.
- Five message types: `version` (handshake), `work_unit`, `result`, `work_request`, `heartbeat` (see `include/common/message.h`).

## Plugin Architecture

Producer uses a `TestPlugin` dispatch table (`include/producer/test_plugin.h`) with 4 `std::function` members:
- `startup(config_path, resume_state)` — reads plugin config, restores from checkpoint
- `next_unit(out)` — generates next work unit, returns `false` when exhausted
- `checkpoint()` — returns plugin-specific state for checkpoint merge
- `exit_conditions()` — returns `true` when plugin wants to stop

Consumer uses `IWorkUnitHandler` (`include/consumer/work_unit_handler.h`) and `IResultSink` (`include/consumer/result_sink.h`).

**Main JSON config** (`--file config.json`):
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

**New test type** = `XXX_plugin.h/cpp` (producer) + `XXX_Handler.h/cpp` (consumer) + registration in `producer.cpp::init_plugin()` and `consumer.cpp` constructor.

**Existing plugins:**
- `PWD` — password permutation generator (`PWD_plugin.cpp`, `PWD_Handler.cpp`)
- `BENCH` — file chunk benchmark (`BENCH_plugin.cpp`, `BENCH_Handler.cpp`)

## PWD_NextUnit

- Legacy C-style class in `src/producer/PWD_NextUnit.h/cpp` — uses `#define` constants, raw arrays, no `std::` prefixes.
- Wrapped by `PWD_plugin.cpp` which implements the `TestPlugin` interface.
- `PWD_Handler` in `src/consumer/` IS wired and selected via `--handler PWD`.

## Data Directory

- Checkpoint files default to `%APPDATA%\Producer\` (Windows) or `~/.local/share/producer/` (Linux).
- Use `get_data_directory()` from `common/util.h` — creates the directory if missing.
- Override with `--checkpoint-dir DIR` on producer CLI.

## Remaining Tasks (see `docs/PROGRESS.md`)

- `PWD_NextUnit` checkpoint state serialization for resume
- Linux build verification
- Performance benchmarking
- Dashboard / telemetry endpoint
- WebSocket or HTTP/2 transport option
