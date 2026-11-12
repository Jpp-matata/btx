#!/usr/bin/env bash
# Run every check of the repo's lint runner one at a time, and every test/lint/lint-*.py one at a time,
# so that each has its own result. Writes <outdir>/lint-results.tsv (name, pass|fail|timeout, seconds), one log
# per check. A check that runs longer than LINT_TIMEOUT seconds (default 900) is stopped and recorded as
# timeout. Exit 0: the verdict is the known-failing gate's (btx_ci.py known --lint ...).
# Usage: COMMIT_RANGE=<base>..HEAD contrib/ci-fork/lint.sh <test_runner binary> <outdir>
export LC_ALL=C
set -uo pipefail

runner=${1:?usage: lint.sh <test_runner> <outdir>}
out=${2:?outdir}
limit=${LINT_TIMEOUT:-900}
mkdir -p "$out/logs"
: > "$out/lint-results.tsv"
record() {
  local name=$1 rc=$2 secs=$3 st=fail
  if [ "$rc" -eq 0 ]; then st=pass; elif [ "$rc" -eq 124 ]; then st=timeout; fi
  printf '%s\t%s\t%s\n' "$name" "$st" "$secs" >> "$out/lint-results.tsv"
  printf '%-48s %s (%ss)\n' "$name" "$st" "$secs"
}
mapfile -t lints < <("$runner" --help | sed -n 's/^\([a-z_]*\): ".*"$/\1/p' | grep -vx 'all_python_linters')
for l in "${lints[@]}"; do
  t0=$(date +%s)
  RUST_BACKTRACE=1 timeout "$limit" "$runner" --lint="$l" > "$out/logs/$l.log" 2>&1
  rc=$?
  record "$l" "$rc" "$(( $(date +%s) - t0 ))"
done
for f in test/lint/lint-*.py; do
  n=$(basename "$f")
  t0=$(date +%s)
  timeout "$limit" python3 "$f" > "$out/logs/$n.log" 2>&1
  rc=$?
  record "$n" "$rc" "$(( $(date +%s) - t0 ))"
done
echo "lint checks: $(wc -l < "$out/lint-results.tsv"), failing: $(grep -c $'\tfail\t' "$out/lint-results.tsv"), timed out: $(grep -c $'\ttimeout\t' "$out/lint-results.tsv")"
exit 0
