# Functional CI result completeness evidence

Same final CLI tests:4failed/6passed before,10passed after. Helper selftest and diff check passed. Source hashes and each case's wrapper/gate exits are in results.json.

Reproduce from the code commit:

    python3 contrib/ci-fork/test_functional_completeness.py
    python3 contrib/ci-fork/btx_ci.py selftest --job local

BTX_CI_TEST_HELPER may select the original parent helper for the paired baseline. Public log copies replace machine-local paths only; original hashes are recorded. The patch carries the exact code/test/workflow changes with a public author identity.

Scope:macOS arm64/Python3.9.6; real helper commands/parsers, simulated runner. No hosted Actions, Ubuntu/Python3.12 or actual BTX functional-suite run. Missing selected results are rejected by the wrapper; complete known failures retain the existing gate policy. No historical false-green was observed. Stale/duplicate/malformed result handling is outside this repair. Patch application was checked at matrix source64889922, not executed there.
