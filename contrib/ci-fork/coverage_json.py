#!/usr/bin/env python3
"""Turn a gcovr JSON summary and the test results of one coverage run into coverage.json.

coverage.json is the small, public file a status page can show: line, branch and function
coverage of the product code, per area, with the facts every coverage number needs (commit,
suite set, timeouts, what did not finish, build type, gcovr version, counters cleared). Its
format is contrib/ci-fork/coverage.schema.json; contrib/ci-fork/coverage_check.py checks a file
against the same rules without third-party modules.

Inputs come from the measured recipe of the dev hub (gcc --coverage build, ctest --output-junit,
test_runner.py --resultsfile, gcovr --json-summary):

  coverage_json.py --summary summary.json [--unit-summary summary-unit.json]
      --junit unit-junit.xml [--junit unit-junit-retry.xml] [--functional-csv func.csv --stall-list stall.txt]
      --commit <40 hex> --measured-at 2026-11-12T12:00:00Z --timeout-s 1800 --gcovr 8.8
      --compiler "gcc 13.3.0" --out coverage.json

Product code = src/ minus test, bench and fuzz code and the vendored leveldb, crc32c, secp256k1,
minisketch and crypto/ctaes trees (the definition the 12 Nov baseline used). Standard library only.
"""
import argparse
import csv
import datetime
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

SCHEMA_VERSION = 1
THIRD = ("leveldb/", "crc32c/", "secp256k1/", "minisketch/", "crypto/ctaes/")

# (id, label, paths shown to readers, selector on the src/-relative file name)
AREAS = [
    ("consensus", "src/consensus", ["consensus/"], lambda f: f.startswith("consensus/")),
    ("validation", "src/validation.cpp", ["validation.cpp"], lambda f: f == "validation.cpp"),
    ("script", "src/script", ["script/"], lambda f: f.startswith("script/")),
    ("wallet", "src/wallet", ["wallet/"], lambda f: f.startswith("wallet/")),
    ("net_processing", "src/net_processing.cpp", ["net_processing.cpp"], lambda f: f == "net_processing.cpp"),
    ("net", "src/net* (other)", ["net*.cpp", "net*.h"],
     lambda f: "/" not in f and f.startswith("net") and f != "net_processing.cpp"),
    ("matmul", "src/matmul", ["matmul/"], lambda f: f.startswith("matmul/")),
    ("shielded", "src/shielded", ["shielded/"], lambda f: f.startswith("shielded/")),
    ("modelnet", "src/modelnet", ["modelnet/"], lambda f: f.startswith("modelnet/")),
    ("rpc", "src/rpc", ["rpc/"], lambda f: f.startswith("rpc/")),
    ("pq", "src/pq and src/pqkey", ["pq/", "pqkey.*"], lambda f: f.startswith("pq/") or f.startswith("pqkey.")),
    ("node", "src/node", ["node/"], lambda f: f.startswith("node/")),
    ("kernel", "src/kernel", ["kernel/"], lambda f: f.startswith("kernel/")),
    ("policy", "src/policy", ["policy/"], lambda f: f.startswith("policy/")),
    ("pow", "src/pow.cpp", ["pow.cpp"], lambda f: f == "pow.cpp"),
    ("univalue", "src/univalue (in-tree)", ["univalue/"], lambda f: f.startswith("univalue/")),
]


def is_product(f):
    return not (f.startswith(("test/", "bench/", "fuzz/")) or "/test/" in f or "/fuzz/" in f or f.startswith(THIRD))


def area_of(f):
    for aid, _label, _paths, sel in AREAS:
        if sel(f):
            return aid
    return "other"


def norm(name):
    name = name.replace("\\", "/")
    return name[4:] if name.startswith("src/") else name


def load_summary(path):
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    if "files" not in d:
        raise ValueError(f"{path}: not a gcovr --json-summary file")
    return {norm(x["filename"]): x for x in d["files"]}


def pct(c, t):
    return round(100.0 * c / t, 1) if t else None


def triple(files, kind):
    c = sum(x.get(f"{kind}_covered", 0) for x in files)
    t = sum(x.get(f"{kind}_total", 0) for x in files)
    return {"covered": c, "total": t, "percent": pct(c, t)}


def junit_counts(paths, timeout_s, exclude_re):
    final = {}
    for p in paths:
        for tc in ET.parse(p).getroot().iter("testcase"):
            name = tc.get("name") or ""
            st = tc.get("status") or ""
            fail = tc.find("failure")
            if st == "run":
                final[name] = "passed"
            elif st == "notrun" or tc.find("skipped") is not None:
                final[name] = "skipped"
            elif fail is not None and (fail.get("message") or "").lower().startswith("timeout"):
                final[name] = "timed_out"
            else:
                final[name] = "failed"
    out = {"run": len(final), "passed": 0, "failed": 0, "skipped": 0, "timed_out": 0, "timeout_s": timeout_s,
           "excluded": [exclude_re] if exclude_re else []}
    for v in final.values():
        out[v] += 1
    return out


def functional_counts(path, stall_path, timeout_factor):
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        rows = [r for r in csv.DictReader(f) if (r.get("test") or "").strip() not in ("", "ALL")]
    stall = set()
    if stall_path:
        with open(stall_path, "r", encoding="utf-8") as f:
            stall = {x.strip() for x in f if x.strip()}
    st = [(r.get("status") or "").strip() for r in rows]
    failed = {r["test"].strip() for r in rows if (r.get("status") or "").strip() == "Failed"}
    return {"run": len(rows), "passed": st.count("Passed"), "failed": st.count("Failed"), "skipped": st.count("Skipped"),
            "stalled_exactreplay": len(failed & stall), "timeout_factor": timeout_factor}


def iso(s):
    try:
        d = datetime.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        raise argparse.ArgumentTypeError(f"want YYYY-MM-DDTHH:MM:SSZ, got {s!r}")
    return d.strftime("%Y-%m-%dT%H:%M:%SZ")


def build(a):
    final = load_summary(a.summary)
    unit = load_summary(a.unit_summary) if a.unit_summary else None
    prod = {f: x for f, x in final.items() if is_product(f)}
    if not prod:
        raise ValueError("no product files in the summary")
    areas = []
    groups = {}
    for f, x in prod.items():
        groups.setdefault(area_of(f), []).append(f)
    labels = [(aid, label, paths) for aid, label, paths, _ in AREAS] + [("other", "everything else", ["(the rest of src/)"])]
    for aid, label, paths in labels:
        fs = groups.get(aid, [])
        if not fs:
            continue
        xs = [prod[f] for f in fs]
        li, br, fn = triple(xs, "line"), triple(xs, "branch"), triple(xs, "function")
        item = {"id": aid, "label": label, "paths": paths, "files": len(fs),
                "lines_covered": li["covered"], "lines_total": li["total"], "lines_percent": li["percent"],
                "branches_percent": br["percent"], "functions_percent": fn["percent"]}
        if unit is not None:
            ux = [unit[f] for f in fs if f in unit]
            item["unit_only_lines_percent"] = triple(ux, "line")["percent"]
        areas.append(item)
    xs = list(prod.values())
    doc = {
        "schema_version": SCHEMA_VERSION,
        "repository": a.repository,
        "branch": a.branch,
        "commit": a.commit,
        "measured_at": a.measured_at,
        "generated_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "produced_by": a.produced_by,
        "build": {"type": "Coverage", "compiler": a.compiler, "flags": a.flags, "gcovr": a.gcovr,
                  "counters_cleared_before_run": not a.counters_not_cleared, "thread_shim": a.thread_shim},
        "suites": {},
        "floors": True,
        "scope": ("product code only: src/ minus test, bench and fuzz code and the vendored leveldb, crc32c, "
                  "secp256k1, minisketch and crypto/ctaes trees" + (f"; {a.scope_note}" if a.scope_note else "")),
        "totals": {"files": len(xs), "lines": triple(xs, "line"), "branches": triple(xs, "branch"),
                   "functions": triple(xs, "function")},
        "areas": areas,
        "notes": list(a.note or []),
    }
    if unit is not None:
        ux = [x for f, x in unit.items() if is_product(f)]
        doc["totals"]["unit_only_lines_percent"] = triple(ux, "line")["percent"]
    if a.junit:
        doc["suites"]["unit"] = junit_counts(a.junit, a.timeout_s, a.unit_excluded)
    if a.functional_csv:
        doc["suites"]["functional"] = functional_counts(a.functional_csv, a.stall_list, a.timeout_factor)
    timed_out = doc["suites"].get("unit", {}).get("timed_out", 0)
    failed_f = doc["suites"].get("functional", {}).get("failed", 0)
    if timed_out or failed_f:
        doc["notes"].insert(0, "A killed or timed-out test writes no coverage counters: every number is a floor.")
    return doc


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--summary", required=True, help="gcovr --json-summary of the whole measured run")
    p.add_argument("--unit-summary", help="gcovr --json-summary taken after the unit tests only")
    p.add_argument("--junit", action="append", help="ctest --output-junit file; repeat for retry passes (later wins)")
    p.add_argument("--functional-csv", help="test_runner.py --resultsfile CSV")
    p.add_argument("--stall-list", help="functional scripts whose node logs say 'ExactReplay required'")
    p.add_argument("--commit", required=True)
    p.add_argument("--repository", default="btxchain/btx")
    p.add_argument("--branch", default="release-0.34.16")
    p.add_argument("--measured-at", required=True, type=iso, help="end of the measured run, UTC")
    p.add_argument("--produced-by", default="fork coverage run (contrib/ci-fork)")
    p.add_argument("--compiler", required=True)
    p.add_argument("--flags", default="-Og --coverage, WITH_MODELNET=ON, WITH_ZMQ=ON, BUILD_GUI=OFF, BUILD_BENCH=OFF")
    p.add_argument("--gcovr", required=True, help="gcovr version")
    p.add_argument("--thread-shim", default="BTX_HWC=2 OMP_NUM_THREADS=2")
    p.add_argument("--counters-not-cleared", action="store_true")
    p.add_argument("--timeout-s", type=int, required=True, help="ctest --timeout of the unit run")
    p.add_argument("--unit-excluded", help="regex of unit suites left out of the run, if any")
    p.add_argument("--timeout-factor", type=int, default=1)
    p.add_argument("--scope-note")
    p.add_argument("--note", action="append")
    p.add_argument("--out", required=True)
    a = p.parse_args(argv)
    if not re.fullmatch(r"[0-9a-f]{40}", a.commit):
        p.error("--commit wants the full 40-hex SHA")
    doc = build(a)
    tmp = a.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, sort_keys=False)
        f.write("\n")
    os.replace(tmp, a.out)
    t = doc["totals"]
    print(f"coverage.json: {t['files']} product files, lines {t['lines']['percent']}%, branches "
          f"{t['branches']['percent']}%, functions {t['functions']['percent']}%, {len(doc['areas'])} areas -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
