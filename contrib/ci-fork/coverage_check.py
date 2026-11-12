#!/usr/bin/env python3
"""Check a coverage.json before it is published (standard library only).

The rules match contrib/ci-fork/coverage.schema.json and add what a JSON schema cannot say:
covered <= total, each percent equal to 100 * covered / total (to 0.05), measured_at not after
generated_at and neither in the future, suite counts that add up, unique area ids, and no
home-directory paths, e-mail addresses or IPv4 addresses anywhere in the file.

  coverage_check.py coverage.json [--repository btxchain/btx] [--branch release-0.34.16] [--max-age-days 30]
  coverage_check.py --selftest

Exit 0 and one summary line when the file is good; exit 1 and one line per problem otherwise.
"""
import argparse
import datetime
import json
import re
import sys

MAX_BYTES = 65536
TOP = {"schema_version", "repository", "branch", "commit", "measured_at", "generated_at", "produced_by", "build",
       "suites", "floors", "scope", "totals", "areas", "notes"}
TOP_REQUIRED = TOP - {"produced_by"}
BUILD = {"type", "compiler", "flags", "gcovr", "counters_cleared_before_run", "thread_shim"}
UNIT = {"run", "passed", "failed", "skipped", "timed_out", "timeout_s", "excluded"}
FUNCTIONAL = {"run", "passed", "failed", "skipped", "stalled_exactreplay", "timeout_factor"}
TOTALS = {"files", "lines", "branches", "functions", "unit_only_lines_percent"}
AREA = {"id", "label", "paths", "files", "lines_covered", "lines_total", "lines_percent", "branches_percent",
        "functions_percent", "unit_only_lines_percent", "note"}
HOME_DIRS = ("Users", "home", "root")
PRIVATE = [
    ("home path", re.compile(r"(?:/(?:%s)/|[A-Za-z]:\\Users\\)" % "|".join(HOME_DIRS))),
    ("e-mail address", re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")),
    ("IPv4 address", re.compile(r"(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])")),
    (".local host name", re.compile(r"\b[A-Za-z0-9-]+\.local\b")),
]


def when(s):
    return datetime.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)


def strings(x, path="$"):
    if isinstance(x, str):
        yield path, x
    elif isinstance(x, dict):
        for k, v in x.items():
            yield from strings(k, path + ".<key>")
            yield from strings(v, f"{path}.{k}")
    elif isinstance(x, list):
        for i, v in enumerate(x):
            yield from strings(v, f"{path}[{i}]")


def check(doc, raw_len, repository, max_age_days, now=None):
    errs = []
    now = now or datetime.datetime.now(datetime.timezone.utc)

    def need(cond, msg):
        if not cond:
            errs.append(msg)
        return cond

    def is_int(v):
        return isinstance(v, int) and not isinstance(v, bool) and v >= 0

    def is_pct(v, nullable=True):
        return (v is None and nullable) or (isinstance(v, (int, float)) and not isinstance(v, bool) and 0 <= v <= 100)

    def keys(obj, allowed, required, where):
        if not need(isinstance(obj, dict), f"{where}: want an object"):
            return False
        extra = sorted(set(obj) - allowed)
        missing = sorted(required - set(obj))
        need(not extra, f"{where}: unknown keys {extra}")
        need(not missing, f"{where}: missing keys {missing}")
        return not extra and not missing

    need(raw_len <= MAX_BYTES, f"file is {raw_len} bytes (limit {MAX_BYTES})")
    if not keys(doc, TOP, TOP_REQUIRED, "$"):
        return errs
    need(doc["schema_version"] == 1, "schema_version: want 1")
    need(isinstance(doc["repository"], str) and doc["repository"] == repository,
         f"repository: want {repository!r}, got {doc['repository']!r}")
    need(isinstance(doc["branch"], str) and re.fullmatch(r"[A-Za-z0-9._/-]{1,100}", doc["branch"] or "") is not None,
         "branch: want a branch name")
    need(isinstance(doc["commit"], str) and re.fullmatch(r"[0-9a-f]{40}", doc["commit"]) is not None,
         "commit: want the full 40-hex SHA")
    times = {}
    for k in ("measured_at", "generated_at"):
        try:
            times[k] = when(doc[k])
        except (TypeError, ValueError):
            errs.append(f"{k}: want YYYY-MM-DDTHH:MM:SSZ")
    if len(times) == 2:
        need(times["measured_at"] <= times["generated_at"], "measured_at is after generated_at")
        need(times["generated_at"] <= now + datetime.timedelta(minutes=10), "generated_at is in the future")
        if max_age_days:
            need(now - times["measured_at"] <= datetime.timedelta(days=max_age_days),
                 f"measured_at is more than {max_age_days} days old")
    need(doc["floors"] is True, "floors: want true (a killed test writes no counters)")
    need(isinstance(doc["scope"], str) and 0 < len(doc["scope"]) <= 400, "scope: want a short text")
    if keys(doc["build"], BUILD, BUILD, "build"):
        b = doc["build"]
        for k in ("type", "compiler", "flags", "gcovr", "thread_shim"):
            need(isinstance(b[k], str) and 0 < len(b[k]) <= 200, f"build.{k}: want a short text")
        need(b["counters_cleared_before_run"] is True, "build.counters_cleared_before_run: want true")
    s = doc["suites"]
    if need(isinstance(s, dict) and set(s) <= {"unit", "functional"} and s, "suites: want unit and/or functional"):
        if "unit" in s and keys(s["unit"], UNIT, UNIT, "suites.unit"):
            u = s["unit"]
            if all(is_int(u[k]) for k in ("run", "passed", "failed", "skipped", "timed_out", "timeout_s")):
                need(u["run"] == u["passed"] + u["failed"] + u["skipped"] + u["timed_out"],
                     "suites.unit: run != passed + failed + skipped + timed_out")
            else:
                errs.append("suites.unit: counts want integers >= 0")
            need(isinstance(u["excluded"], list) and all(isinstance(x, str) for x in u["excluded"]),
                 "suites.unit.excluded: want a list of texts")
        if "functional" in s and keys(s["functional"], FUNCTIONAL, FUNCTIONAL, "suites.functional"):
            f = s["functional"]
            if all(is_int(f[k]) for k in FUNCTIONAL):
                need(f["run"] == f["passed"] + f["failed"] + f["skipped"],
                     "suites.functional: run != passed + failed + skipped")
                need(f["stalled_exactreplay"] <= f["failed"], "suites.functional: more stalls than failures")
            else:
                errs.append("suites.functional: counts want integers >= 0")
    t = doc["totals"]
    if keys(t, TOTALS, TOTALS - {"unit_only_lines_percent"}, "totals"):
        need(is_int(t["files"]) and t["files"] > 0, "totals.files: want a count > 0")
        for k in ("lines", "branches", "functions"):
            v = t[k]
            if keys(v, {"covered", "total", "percent"}, {"covered", "total", "percent"}, f"totals.{k}"):
                if need(is_int(v["covered"]) and is_int(v["total"]) and v["covered"] <= v["total"] and v["total"] > 0,
                        f"totals.{k}: want 0 <= covered <= total, total > 0"):
                    need(is_pct(v["percent"], False) and abs(v["percent"] - 100.0 * v["covered"] / v["total"]) <= 0.05,
                         f"totals.{k}.percent does not match covered/total")
        if "unit_only_lines_percent" in t:
            need(is_pct(t["unit_only_lines_percent"], False), "totals.unit_only_lines_percent: want 0-100")
    areas = doc["areas"]
    if need(isinstance(areas, list) and 0 < len(areas) <= 40, "areas: want 1-40 entries"):
        ids = []
        for i, ar in enumerate(areas):
            where = f"areas[{i}]"
            if not keys(ar, AREA, {"id", "label", "paths", "lines_percent"}, where):
                continue
            ids.append(ar["id"])
            need(isinstance(ar["id"], str) and re.fullmatch(r"[a-z0-9_]{1,40}", ar["id"]) is not None, f"{where}.id")
            need(isinstance(ar["label"], str) and 0 < len(ar["label"]) <= 80, f"{where}.label")
            need(isinstance(ar["paths"], list) and ar["paths"] and all(isinstance(p, str) for p in ar["paths"]),
                 f"{where}.paths")
            for k in ("lines_percent", "branches_percent", "functions_percent", "unit_only_lines_percent"):
                if k in ar:
                    need(is_pct(ar[k], k != "lines_percent"), f"{where}.{k}: want 0-100")
            if "lines_covered" in ar or "lines_total" in ar:
                c, tt = ar.get("lines_covered"), ar.get("lines_total")
                if need(is_int(c) and is_int(tt) and c <= tt and tt > 0, f"{where}: want 0 <= lines_covered <= lines_total"):
                    need(is_pct(ar["lines_percent"], False) and abs(ar["lines_percent"] - 100.0 * c / tt) <= 0.05,
                         f"{where}.lines_percent does not match lines_covered/lines_total")
            if "files" in ar:
                need(is_int(ar["files"]), f"{where}.files")
            if "note" in ar:
                need(isinstance(ar["note"], str) and len(ar["note"]) <= 300, f"{where}.note")
        need(len(ids) == len(set(ids)), "areas: ids are not unique")
    need(isinstance(doc["notes"], list) and len(doc["notes"]) <= 20
         and all(isinstance(n, str) and len(n) <= 300 for n in doc["notes"]), "notes: want up to 20 short texts")
    for path, text in strings(doc):
        for name, rx in PRIVATE:
            if rx.search(text):
                errs.append(f"{path}: looks like a {name}; coverage.json is public")
    return errs


def selftest():
    """A minimal good file passes; each planted fault is rejected."""
    import copy
    now = when("2026-11-12T13:00:00Z")
    good = {
        "schema_version": 1, "repository": "btxchain/btx", "branch": "release-0.34.16", "commit": "c" * 40,
        "measured_at": "2026-11-12T10:24:49Z", "generated_at": "2026-11-12T12:00:00Z", "produced_by": "selftest",
        "build": {"type": "Coverage", "compiler": "gcc 13.3.0", "flags": "-Og --coverage", "gcovr": "8.8",
                  "counters_cleared_before_run": True, "thread_shim": "BTX_HWC=2"},
        "suites": {"unit": {"run": 4, "passed": 2, "failed": 1, "skipped": 0, "timed_out": 1, "timeout_s": 900,
                            "excluded": []},
                   "functional": {"run": 3, "passed": 1, "failed": 2, "skipped": 0, "stalled_exactreplay": 1,
                                  "timeout_factor": 4}},
        "floors": True, "scope": "product code", "notes": ["every number is a floor"],
        "totals": {"files": 2, "lines": {"covered": 7, "total": 10, "percent": 70.0},
                   "branches": {"covered": 1, "total": 4, "percent": 25.0},
                   "functions": {"covered": 2, "total": 2, "percent": 100.0}, "unit_only_lines_percent": 60.0},
        "areas": [{"id": "wallet", "label": "src/wallet", "paths": ["wallet/"], "files": 1, "lines_covered": 3,
                   "lines_total": 4, "lines_percent": 75.0, "branches_percent": None, "functions_percent": 100.0}],
    }
    assert check(good, 1000, "btxchain/btx", 0, now) == [], check(good, 1000, "btxchain/btx", 0, now)
    planted = [
        ("short commit", lambda d: d.__setitem__("commit", "c6f9678d")),
        ("unknown key", lambda d: d.__setitem__("extra", 1)),
        ("wrong repository", lambda d: d.__setitem__("repository", "someone/btx")),
        ("percent mismatch", lambda d: d["totals"]["lines"].__setitem__("percent", 71.0)),
        ("covered above total", lambda d: d["totals"]["branches"].update(covered=5, percent=125.0)),
        ("future generated_at", lambda d: d.__setitem__("generated_at", "2026-11-13T00:00:00Z")),
        ("measured after generated", lambda d: d.__setitem__("measured_at", "2026-11-12T12:30:00Z")),
        ("bad time format", lambda d: d.__setitem__("measured_at", "12 Nov 2026")),
        ("floors false", lambda d: d.__setitem__("floors", False)),
        ("counters not cleared", lambda d: d["build"].__setitem__("counters_cleared_before_run", False)),
        ("unit counts do not add up", lambda d: d["suites"]["unit"].__setitem__("passed", 3)),
        ("more stalls than failures", lambda d: d["suites"]["functional"].__setitem__("stalled_exactreplay", 3)),
        ("duplicate area id", lambda d: d["areas"].append(dict(d["areas"][0]))),
        ("area percent mismatch", lambda d: d["areas"][0].__setitem__("lines_percent", 50.0)),
        ("home path in a note", lambda d: d["notes"].append("log in /home/runner/x")),
        ("IPv4 address", lambda d: d.__setitem__("produced_by", "box " + ".".join(["10", "0", "0", "12"]))),
        ("e-mail address", lambda d: d.__setitem__("scope", "ask someone@example.org")),
        ("bool as count", lambda d: d["suites"]["unit"].__setitem__("failed", True)),
    ]
    for name, mutate in planted:
        d = copy.deepcopy(good)
        mutate(d)
        assert check(d, 1000, "btxchain/btx", 0, now), name
    assert check(good, MAX_BYTES + 1, "btxchain/btx", 0, now), "size limit"
    assert check(good, 1000, "btxchain/btx", 1, now + datetime.timedelta(days=2)), "max age"
    print(f"coverage_check: selftest OK (1 good file accepted, {len(planted) + 2} planted faults rejected)")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("file", nargs="?")
    p.add_argument("--selftest", action="store_true", help="run the built-in planted-fault checks")
    p.add_argument("--repository", default="btxchain/btx")
    p.add_argument("--branch", help="the branch the file must name (default: any)")
    p.add_argument("--max-age-days", type=int, default=0, help="0 = no age limit")
    a = p.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.file:
        p.error("a coverage.json file is required")
    with open(a.file, "rb") as f:
        raw = f.read()
    try:
        doc = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as e:
        print(f"coverage_check: not UTF-8 JSON: {e}")
        return 1
    errs = check(doc, len(raw), a.repository, a.max_age_days)
    if a.branch and isinstance(doc, dict) and doc.get("branch") != a.branch:
        errs.append(f"branch: want {a.branch!r}, got {doc.get('branch')!r}")
    if errs:
        for e in errs:
            print(f"coverage_check: {e}")
        return 1
    t = doc["totals"]
    print(f"coverage_check: OK {doc['repository']}@{doc['commit'][:12]} measured {doc['measured_at']}: lines "
          f"{t['lines']['percent']}%, branches {t['branches']['percent']}%, functions {t['functions']['percent']}%, "
          f"{len(doc['areas'])} areas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
