#!/usr/bin/env bash
#
# Build and test the Producer-Consumer project.
# Cross-platform build helper (Bash edition).
#
# Usage:
#   ./build.sh [TARGET] [OPTIONS]
#
# Targets:
#   build    Configure and compile (default)
#   test     Run all test executables
#   clean    Remove the build directory
#   rebuild  Clean then build
#   all      Clean, build, then test
#
# Options:
#   --config RELEASE|DEBUG   Build configuration (default: Release)
#   --no-tests               Disable test builds
#
# Examples:
#   ./build.sh
#   ./build.sh all
#   ./build.sh test --config Debug
#   ./build.sh build --no-tests

set -euo pipefail

BUILD_DIR="build"
CONFIG="Release"
TESTS_FLAG="-DBUILD_TESTS=ON"
TARGET="build"

# ── Parse arguments ──────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        build|test|clean|rebuild|all)
            TARGET="$1"
            shift
            ;;
        --config)
            CONFIG="$2"
            shift 2
            ;;
        --no-tests)
            TESTS_FLAG="-DBUILD_TESTS=OFF"
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

# ── Functions ────────────────────────────────────────────────────
configure() {
    echo -e "\033[36m==> Configuring CMake ($CONFIG)...\033[0m"
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" $TESTS_FLAG
}

build() {
    echo -e "\033[36m==> Building ($CONFIG)...\033[0m"
    cmake --build "$BUILD_DIR" --config "$CONFIG"
    echo -e "\033[32m==> Build complete.\033[0m"
}

run_tests() {
    TEST_DIR="$BUILD_DIR/tests/$CONFIG"
    if [[ ! -d "$TEST_DIR" ]]; then
        echo -e "\033[33m==> Test directory not found. Build with tests first.\033[0m"
        return
    fi

    mapfile -t TEST_EXES < <(find "$TEST_DIR" -maxdepth 1 -name "test_*" -type f -executable 2>/dev/null)
    if [[ ${#TEST_EXES[@]} -eq 0 ]]; then
        echo -e "\033[33m==> No test executables found.\033[0m"
        return
    fi

    echo -e "\033[36m==> Running ${#TEST_EXES[@]} test(s)...\033[0m"
    FAILED=0
    for exe in "${TEST_EXES[@]}"; do
        BASENAME=$(basename "$exe")
        printf "  [%s] " "$BASENAME"
        if "$exe" > /dev/null 2>&1; then
            echo -e "\033[32mPASS\033[0m"
        else
            echo -e "\033[31mFAIL (exit $?)\033[0m"
            ((FAILED++)) || true
        fi
    done

    if [[ $FAILED -eq 0 ]]; then
        echo -e "\033[32m==> All tests passed.\033[0m"
    else
        echo -e "\033[31m==> $FAILED test(s) failed.\033[0m"
        exit "$FAILED"
    fi
}

clean() {
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
        echo -e "\033[32m==> Cleaned $BUILD_DIR\033[0m"
    else
        echo -e "\033[33m==> Nothing to clean.\033[0m"
    fi
}

# ── Dispatch ─────────────────────────────────────────────────────
case "$TARGET" in
    build)   configure; build ;;
    test)    run_tests ;;
    clean)   clean ;;
    rebuild) clean; configure; build ;;
    all)     clean; configure; build; run_tests ;;
esac
