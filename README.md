# Producer-Consumer

A cross-platform (Windows / Linux) C++17 producer-consumer test framework. A **Producer** dispatches work units over the network to one or more **Consumers**, which process them in a thread pool and return results.

## How it works

- **Producer** — reads a JSON config, initializes a test plugin (PWD, BENCH, or ECHO), and dispatches work units to Consumers over TCP or UDP.
- **Consumer** — connects to a Producer, downloads the source file if needed, processes work units in a thread pool, and returns results.

Work is distributed across all connected Consumers; if one disconnects, its pending work is reclaimed and re-dispatched to the rest.

## Quick start

Build (see [docs/BUILDING.md](docs/BUILDING.md)):

```powershell
# Windows (PowerShell)
.\build.ps1 -Target all     # clean + build + test
```

```bash
# Linux / WSL (Bash)
./build.sh all              # clean + build + test
```

Run:

```bash
# Terminal 1 — Producer
producer --file config.json

# Terminal 2 — Consumer
consumer --handler PWD
```

By default the Producer listens on `0.0.0.0:9876` and the Consumer connects to `127.0.0.1:9876`. See [docs/USER_GUIDE.md](docs/USER_GUIDE.md) for all options and multi-machine / multi-consumer setups.

## Features

- TCP or UDP transport (`--transport tcp|udp`)
- Multiple simultaneous Consumers with automatic work reclamation on disconnect
- Source-file download over a dedicated channel (`port + 1`) with SHA-256 verification
- Checkpointing + `--resume` for mid-run recovery
- Pluggable test types: **PWD** (password permutations), **BENCH** (file-chunk benchmark), **ECHO** (payload / hash)
- 13 test suites / 138 tests

## Documentation

| Doc | Purpose |
|-----|---------|
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Running the producer/consumer, all CLI options |
| [docs/BUILDING.md](docs/BUILDING.md) | Build targets, options, prerequisites |
| [docs/TESTING.md](docs/TESTING.md) | Test suites and how to run them |
| [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md) | Architecture and design |
| [docs/COMMUNICATION.md](docs/COMMUNICATION.md) | Network protocol |
| [docs/PROGRESS.md](docs/PROGRESS.md) | Status, completed work, known issues |
| [specs/spec.md](specs/spec.md) | Overall specification |
| [specs/Producer-Spec.md](specs/Producer-Spec.md) | Producer specification |
| [specs/Consumer-Spec.md](specs/Consumer-Spec.md) | Consumer specification |

## CI

GitHub Actions (`.github/workflows/ci.yml`) builds and runs all 13 test suites on Windows (MSVC) and Linux (GCC) for every push/PR to `master`.
