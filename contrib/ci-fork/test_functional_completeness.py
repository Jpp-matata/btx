#!/usr/bin/env python3
"""CLI regressions for functional selection/result completeness (no BTX binary).

Run: python3 contrib/ci-fork/test_functional_completeness.py
BTX_CI_TEST_HELPER can select the unchanged baseline helper for a paired run.
BTX_CI_TEST_OUTPUT optionally retains command output outside temporary fixtures.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(os.environ.get("BTX_CI_TEST_HELPER", Path(__file__).with_name("btx_ci.py"))).resolve()


class FunctionalCompleteness(unittest.TestCase):
    def run_case(self, selected, rows, runner_exit=0, known=()):
        with tempfile.TemporaryDirectory(prefix="btx-ci-completeness-") as scratch:
            root = Path(scratch)
            runner = root / "build/test/functional/test_runner.py"
            runner.parent.mkdir(parents=True)
            # Only the test runner is simulated. Both production helper commands
            # run in subprocesses, including their actual CSV and known-list readers.
            runner.write_text(
                "import csv, sys\n"
                f"BTX_BASE_SCRIPTS = {selected!r}\nALL_SCRIPTS = BTX_BASE_SCRIPTS\n"
                "if __name__ == '__main__':\n"
                "    path = next(a.split('=', 1)[1] for a in sys.argv if a.startswith('--resultsfile='))\n"
                "    with open(path, 'w', newline='') as f:\n"
                "        w = csv.writer(f)\n"
                "        w.writerow(['test', 'status', 'duration(seconds)'])\n"
                f"        w.writerows({rows!r})\n"
                f"    sys.exit({runner_exit!r})\n",
                encoding="utf-8",
            )
            known_path = root / "known.txt"
            known_path.write_text("# kind | name | class | first failing line | seen on\n" + "".join(
                f"functional | {name} | pre-existing | fixture failure | baseline\n" for name in known
            ), encoding="utf-8")
            out = root / "results"
            command = [sys.executable, str(HELPER), "functional", "--builddir", str(root / "build"),
                       "--label", "completeness", "--out", str(out), "--tmpdirprefix", str(root / "runtime")]
            wrapper = subprocess.run(command, capture_output=True, text=True, check=False, timeout=30)
            gate_command = [sys.executable, str(HELPER), "known", "--label", "completeness",
                            "--known", str(known_path), "--out", str(root / "gate"),
                            "--functional", str(out / "functional-results.csv")]
            # The workflow runs this step even if the wrapper fails (!cancelled()).
            gate = subprocess.run(gate_command, capture_output=True, text=True, check=False, timeout=30)
            result = {"selected": (out / "selected.txt").read_text().splitlines(),
                      "csv": (out / "functional-results.csv").read_text(),
                      "wrapper_exit": wrapper.returncode, "gate_exit": gate.returncode,
                      "wrapper_output": wrapper.stdout + wrapper.stderr,
                      "gate_output": gate.stdout + gate.stderr}
            if os.environ.get("BTX_CI_TEST_OUTPUT"):
                logs = Path(os.environ["BTX_CI_TEST_OUTPUT"])
                logs.mkdir(parents=True, exist_ok=True)
                (logs / (self._testMethodName + ".json")).write_text(json.dumps(result, indent=2) + "\n")
            self.assertEqual(result["selected"], selected)
            return result

    def test_complete_passes(self):
        r = self.run_case(["alpha.py", "beta.py"], [("alpha.py", "Passed", 1), ("beta.py", "Passed", 1)])
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (0, 0))

    def test_complete_known_failure_remains_allowed(self):
        r = self.run_case(["alpha.py", "beta.py"], [("alpha.py", "Passed", 1), ("beta.py", "Failed", 1)],
                          runner_exit=1, known=["beta.py"])
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (0, 0))

    def test_complete_new_failure_is_rejected_by_gate(self):
        r = self.run_case(["alpha.py", "beta.py"], [("alpha.py", "Passed", 1), ("beta.py", "Failed", 1)], runner_exit=1)
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (0, 1))

    def test_complete_skip_keeps_existing_policy(self):
        r = self.run_case(["alpha.py", "beta.py"], [("alpha.py", "Passed", 1), ("beta.py", "Skipped", 0)])
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (0, 0))

    def test_partial_with_runner_error_fails(self):
        r = self.run_case(["alpha.py", "beta.py"], [("alpha.py", "Passed", 1)], runner_exit=2)
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (1, 0))
        self.assertIn("missing=1", r["wrapper_output"])

    def test_partial_even_with_zero_runner_exit_fails(self):
        r = self.run_case(["alpha.py", "beta.py"], [("alpha.py", "Passed", 1)])
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (1, 0))

    def test_known_failure_cannot_hide_another_missing_test(self):
        r = self.run_case(["alpha.py", "beta.py"], [("alpha.py", "Failed", 1)],
                          runner_exit=1, known=["alpha.py", "beta.py"])
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (1, 0))

    def test_missing_variant_is_not_covered_by_base_name(self):
        r = self.run_case(["alpha.py", "alpha.py --descriptors"], [("alpha.py", "Passed", 1)])
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (1, 0))

    def test_empty_results_fail(self):
        r = self.run_case(["alpha.py"], [], runner_exit=2)
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (1, 1))

    def test_aggregate_row_does_not_count_as_a_test(self):
        r = self.run_case(["alpha.py"], [("ALL", "Passed", 1)])
        self.assertEqual((r["wrapper_exit"], r["gate_exit"]), (1, 1))


if __name__ == "__main__":
    unittest.main()
