<#
.SYNOPSIS
  Build and test the Producer-Consumer project.

.DESCRIPTION
  Cross-platform build helper (PowerShell edition).
  Wraps CMake configure, build, and test steps.

.PARAMETER Target
  Action to perform:
    build   - Configure and compile (default)
    test    - Run all test executables
    clean   - Remove the build directory
    rebuild - Clean then build
    all     - Clean, build, then test

.PARAMETER Config
  CMake configuration: Release (default) or Debug.

.PARAMETER Tests
  Switch to enable test builds (-DBUILD_TESTS=ON).
  Enabled by default.

.EXAMPLE
  .\build.ps1
  .\build.ps1 -Target all
  .\build.ps1 -Target test -Config Debug
  .\build.ps1 -Target build -NoTests
#>

param(
    [ValidateSet("build", "test", "clean", "rebuild", "all")]
    [string]$Target = "build",

    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [switch]$NoTests
)

$ErrorActionPreference = "Stop"
$buildDir = "build"
$testsFlag = if ($NoTests) { "-DBUILD_TESTS=OFF" } else { "-DBUILD_TESTS=ON" }

function Configure {
    Write-Host "==> Configuring CMake ($Config)..." -ForegroundColor Cyan
    cmake -B $buildDir "-DCMAKE_BUILD_TYPE=$Config" $testsFlag
}

function Build {
    Write-Host "==> Building ($Config)..." -ForegroundColor Cyan
    cmake --build $buildDir --config $Config
    Write-Host "==> Build complete." -ForegroundColor Green
}

function Run-Tests {
    if (-not (Test-Path "$buildDir\tests\$Config")) {
        Write-Host "==> Test directory not found. Build with tests first." -ForegroundColor Yellow
        return
    }

    $testExes = Get-ChildItem -Path "$buildDir\tests\$Config" -Filter "test_*.exe"
    if ($testExes.Count -eq 0) {
        Write-Host "==> No test executables found." -ForegroundColor Yellow
        return
    }

    Write-Host "==> Running $($testExes.Count) test(s)..." -ForegroundColor Cyan
    $failed = 0
    foreach ($exe in $testExes) {
        Write-Host "  [$($exe.BaseName)]" -NoNewline
        $exitCode = 0
        & $exe.FullName | Out-Null
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 0) {
            Write-Host " PASS" -ForegroundColor Green
        } else {
            Write-Host " FAIL (exit $exitCode)" -ForegroundColor Red
            $failed++
        }
    }

    if ($failed -eq 0) {
        Write-Host "==> All tests passed." -ForegroundColor Green
    } else {
        Write-Host "==> $failed test(s) failed." -ForegroundColor Red
        exit $failed
    }
}

function Clean {
    if (Test-Path $buildDir) {
        Remove-Item -Recurse -Force $buildDir
        Write-Host "==> Cleaned $buildDir" -ForegroundColor Green
    } else {
        Write-Host "==> Nothing to clean." -ForegroundColor Yellow
    }
}

switch ($Target) {
    "build"   { Configure; Build }
    "test"    { Run-Tests }
    "clean"   { Clean }
    "rebuild" { Clean; Configure; Build }
    "all"     { Clean; Configure; Build; Run-Tests }
}
