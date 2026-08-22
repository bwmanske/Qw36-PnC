# BUILDING.md

## Quick Start

```powershell
# Windows (PowerShell)
.\build.ps1          # configure + build (Release)
.\build.ps1 -Target all  # clean + build + test
```

```bash
# Linux / WSL (Bash)
./build.sh            # configure + build (Release)
./build.sh all        # clean + build + test
```

## Targets

| Target    | Description                    |
|-----------|--------------------------------|
| `build`   | Configure CMake and compile    |
| `test`    | Run all test executables       |
| `clean`   | Remove the `build/` directory  |
| `rebuild` | Clean, then configure + build  |
| `all`     | Clean, build, then run tests   |

`build` is the default when no target is specified.

## Options

### PowerShell (`build.ps1`)

| Parameter   | Values              | Default   | Description                      |
|-------------|---------------------|-----------|----------------------------------|
| `-Target`   | `build`, `test`, `clean`, `rebuild`, `all` | `build` | Action to perform                |
| `-Config`   | `Release`, `Debug`  | `Release` | CMake build configuration        |
| `-NoTests`  | *(switch)*          | off       | Skip building test targets       |

### Bash (`build.sh`)

| Option        | Values              | Default   | Description                      |
|---------------|---------------------|-----------|----------------------------------|
| *(positional)*| `build`, `test`, `clean`, `rebuild`, `all` | `build` | Action to perform                |
| `--config`    | `Release`, `Debug`  | `Release` | CMake build configuration        |
| `--no-tests`  | *(flag)*            | off       | Skip building test targets       |

## Examples

```powershell
# PowerShell
.\build.ps1
.\build.ps1 -Target test
.\build.ps1 -Target all -Config Debug
.\build.ps1 -Target build -NoTests
.\build.ps1 -Target rebuild
```

```bash
# Bash
./build.sh
./build.sh test
./build.sh all --config Debug
./build.sh build --no-tests
./build.sh rebuild
```

## What Happens Under the Hood

### `build` / `rebuild` / `all`

1. **Configure** — `cmake -B build -DCMAKE_BUILD_TYPE=<Config> -DBUILD_TESTS=ON`
   - Fetches dependencies: `nlohmann/json`, `zlib`, `libarchive`, `googletest`
   - Creates `build/` directory with generated files

2. **Build** — `cmake --build build --config <Config>`
   - Produces `producer.exe` and `consumer.exe` under `build/<target>/`
   - Produces test executables under `build/tests/<Config>/`

### `test`

Runs each `test_*.exe` (Windows) or `test_*` (Linux) found in `build/tests/<Config>/`, reporting PASS/FAIL per executable. This bypasses `ctest`, which does not reliably discover tests on Windows with the current CMake/GTest configuration.

## Prerequisites

- **CMake** 3.20+
- **MSVC** (Windows) or **GCC/Clang** (Linux) with C++17 support
- **Git** (for FetchContent dependency retrieval)
- **PowerShell** 7+ (Windows) or **Bash** 4+ (Linux)

## Manual Build (Without Scripts)

```powershell
# Configure
cmake -B build -DBUILD_TESTS=ON

# Build
cmake --build build --config Release

# Run tests (Windows)
build\tests\Release\test_message.exe
build\tests\Release\test_queue.exe
# ... etc.
```

```bash
# Configure
cmake -B build -DBUILD_TESTS=ON

# Build
cmake --build build --config Release

# Run tests (Linux)
./build/tests/Release/test_message
./build/tests/Release/test_queue
# ... etc.
```
