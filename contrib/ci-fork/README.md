# Fork CI for release-0.34.16

Helpers for `.github/workflows/btx-0.34.16-ci.yml` and `.github/workflows/btx-0.34.16-coverage.yml`
on a fork's `ci-0.34.16` branch. They are not proposed upstream: upstream runs no GitHub-hosted CI
(`.github/workflows/README.md`), so a fork run is the evidence a pull request carries.

| File | What it does |
|---|---|
| `btx_ci.py` | The job helper (standard library only). Results are annotations titled `RESULT`. Subcommands: `configure`, `build`, `build-summary`, `suites` (one `test_btx` suite per process; `--shard K/N --shard-costs` balances shards by measured seconds), `merge`, `functional` (a cost-balanced shard of the default functional list, each script exactly once), `known` (the known-failing gate), `lint-diff`, `cppcheck`, `secrets`, and the MinGW and Windows helpers. `selftest` checks the pure parts. |
| `known-failing.txt` | `kind \| name \| class \| first failing line \| seen on`. A failure not listed here is red; the list only shrinks. The `summary` job writes the next list from a run, and lists every new failure in `RESULT` notices (`new_list i/n`, `[stall]` = exactreplay-stall), so the list can be read without a token. A row of class `new` or `new:exactreplay-stall` there is unreviewed: it needs a cause before it enters this file. |
| `unit-suite-seconds.tsv`, `functional-seconds.tsv` | Measured seconds per suite and per script, for the shard split. |
| `cppcheck-baseline.tsv` | cppcheck 2.13 results in product code per `(id, file)`. The `cppcheck` job is red when a count grows. |
| `lint.sh` | Runs each lint check alone, so each has its own result. |
| `tidy-diff.sh`, `gen-headers.sh` | clang-tidy (repo config, bitcoin-tidy plugin) on the changed lines only; the generated headers a compile database needs. |
| `hwc.c` | The thread-count shim of `dev/04-build-and-test.md` section 3 (testing only). |
| `coverage_json.py`, `coverage_check.py`, `coverage.schema.json` | A gcovr JSON summary and the run's results into `coverage.json`; its checks; its schema. |

Run the checks locally:

```bash
python3 contrib/ci-fork/btx_ci.py selftest --job local
python3 contrib/ci-fork/coverage_check.py --selftest
shellcheck contrib/ci-fork/*.sh
```

Rules the jobs keep: no secrets and no `pull_request` trigger; actions pinned to full commit SHAs;
downloads checked against fixed sha256 values; no test skipped and no timeout raised to make a job
green (a suite that times out is rerun alone at the same 900 s); every number names the commit and
the command it came from.
