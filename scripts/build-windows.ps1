# Builds Fetcher with the portable toolchain and runs the tests.
#   scripts\bootstrap-windows.ps1   (once, to download the toolchain)
#   scripts\build-windows.ps1       (add -Package to also create the .zip)
param(
    [switch]$Package,
    [string]$BuildDir = 'build\windows',
    [string[]]$CMakeArgs = @()
)
# Not 'Stop': the tools below write warnings to stderr, which Windows PowerShell
# can treat as fatal. Their exit codes are checked instead.
$ErrorActionPreference = 'Continue'

$root = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $root '.toolchain'
$env:PATH = @(
    (Join-Path $toolchain 'llvm-mingw-20261020-ucrt-x86_64\bin'),
    (Join-Path $toolchain 'cmake-4.4.5-windows-x86_64\bin'),
    $toolchain,
    $env:PATH
) -join ';'

if (-not (Get-Command clang++ -ErrorAction SilentlyContinue)) {
    throw 'Toolchain not found. Run scripts\bootstrap-windows.ps1 first.'
}

Set-Location $root

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-windres @CMakeArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }

cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

if ($Package) {
    cpack --config (Join-Path $BuildDir 'CPackConfig.cmake') -B (Join-Path $BuildDir 'package')
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
}
