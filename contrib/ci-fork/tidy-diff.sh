#!/usr/bin/env bash
# clang-tidy on the lines a branch changes: src/ C++ only, the vendored trees left out, the repo's own
# src/.clang-tidy (warnings are errors there) and the bitcoin-tidy plugin. Exit 0 when no changed line
# draws a warning; the whole-tree count is not gated here.
# Usage: contrib/ci-fork/tidy-diff.sh <base-rev> <builddir> <libbitcoin-tidy.so> <outdir> [jobs]
export LC_ALL=C
set -euo pipefail

base=${1:?usage: tidy-diff.sh <base-rev> <builddir> <plugin> <outdir> [jobs]}
build=${2:?builddir}
plugin=${3:?plugin}
out=${4:?outdir}
jobs=${5:-$(nproc)}
llvm=${TIDY_LLVM_V:-19}
mkdir -p "$out"
git diff --no-color --no-ext-diff -U0 "$base" HEAD -- 'src/*.cpp' 'src/*.h' \
  ':(exclude)src/leveldb' ':(exclude)src/secp256k1' ':(exclude)src/crc32c' ':(exclude)src/minisketch' \
  ':(exclude)src/crypto/ctaes' ':(exclude)src/libbitcoinpqc' ':(exclude)src/univalue' > "$out/changed.diff"
files=$(grep -c '^+++ b/' "$out/changed.diff" || true)
echo "changed C++ files against ${base:0:12}: $files"
if [ "$files" -eq 0 ]; then
  echo "no changed C++ lines: nothing to check" | tee "$out/tidy.txt"
  : > "$out/findings.txt"
  exit 0
fi
rc=0
"clang-tidy-diff-$llvm.py" -p1 -path "$build" -j "$jobs" -quiet -clang-tidy-binary "clang-tidy-$llvm" \
  -load "$plugin" < "$out/changed.diff" > "$out/tidy.txt" 2>&1 || rc=$?
grep -E '^[^ ]+:[0-9]+:[0-9]+: (warning|error): ' "$out/tidy.txt" | sort -u > "$out/findings.txt" || true
echo "findings on changed lines: $(wc -l < "$out/findings.txt"); clang-tidy-diff exit code $rc"
exit "$rc"
