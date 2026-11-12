#!/usr/bin/env bash
# Build only the generated headers of a configured Ninja build directory, so that clang-tidy and
# cppcheck can parse the sources without a full compile.
# Usage: contrib/ci-fork/gen-headers.sh <builddir>
export LC_ALL=C
set -euo pipefail

build=${1:?usage: gen-headers.sh <builddir>}
mapfile -t outs < <(ninja -C "$build" -t targets all | awk -F': ' '$2 == "CUSTOM_COMMAND" && $1 ~ /\.(h|hpp|hh|inc|ipp)$/ {print $1}')
echo "generated headers to build: ${#outs[@]}"
if [ "${#outs[@]}" -gt 0 ]; then
  ninja -C "$build" "${outs[@]}"
fi
