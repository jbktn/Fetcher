#!/usr/bin/env bash
# Builds Fetcher on Linux or macOS and runs the tests.
#   scripts/build-unix.sh            build and test
#   scripts/build-unix.sh --package  also create the .deb and .tar.gz, or the .dmg
# Set BUILD_DIR to build somewhere other than build/<platform>, and pass extra
# CMake options in CMAKE_ARGS.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
build_dir="${BUILD_DIR:-$root/build/$platform}"

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi

extra_args=()
if [[ "$platform" == darwin ]]; then
    # A universal app runs natively on both Apple silicon and Intel Macs.
    extra_args+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
fi

# The ${array[@]+...} form is needed because macOS ships bash 3.2, where
# expanding an empty array under "set -u" is an error.
# shellcheck disable=SC2086
cmake -S "$root" -B "$build_dir" ${generator[@]+"${generator[@]}"} -DCMAKE_BUILD_TYPE=Release \
    ${extra_args[@]+"${extra_args[@]}"} ${CMAKE_ARGS:-}
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

if [[ "${1:-}" == "--package" ]]; then
    cpack --config "$build_dir/CPackConfig.cmake" -B "$build_dir/package"
fi
