#!/usr/bin/env python3
"""Fork CI helper for release-0.34.16 (standard library only; Linux, macOS and Windows).

Used by .github/workflows/btx-0.34.16-ci.yml on a fork's ci-0.34.16 branch; not proposed upstream
(upstream runs no hosted CI, see .github/workflows/README.md). It started as the helper embedded in the
fork workflow of 24 Oct - 7 Nov 2026 and moved here unchanged except for the additions listed below.

Every result is printed as a GitHub annotation titled RESULT, so it can be
read through the public check-runs annotations API without a token.
GitHub keeps at most 10 annotations of each level per step, so each command
spends at most 9 notices and 9 errors and folds the rest into one line.

Subcommands: ann, configure, build, build-summary, suites, merge, prune, relink,
test-defs, dlls, smoke, msvc-run, userns, selftest; added for ci-0.34.16: suites --shard-costs
(shards balanced by measured seconds), known (the known-failing gate), functional (a cost-balanced
shard of the default functional list), cppcheck (product-code scan with a baseline that only shrinks)
secrets (a scan of the lines a branch adds) and lint-diff (new output lines of a lint that already
fails on the base).
"""
import argparse
import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

IS_WIN = os.name == "nt"
MAX_MSG = 900


def esc(s):
    return s.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


class Ann:
    """Annotation emitter with a per-step budget (GitHub: 10 per level per step)."""

    def __init__(self, notices=9, errors=9, logfile=None):
        self.left = {"notice": notices, "error": errors, "warning": 3}
        self.lock = threading.Lock()
        self.logfile = logfile

    def emit(self, level, msg):
        msg = " ".join(msg.split())
        if len(msg) > MAX_MSG:
            msg = msg[: MAX_MSG - 12] + " ...(cut)"
        with self.lock:
            if self.logfile:
                with open(self.logfile, "a", encoding="utf-8") as f:
                    f.write(f"{level}\t{msg}\n")
            if self.left.get(level, 0) <= 0:
                print(f"[annotation budget spent] {level}: {msg}", flush=True)
                return
            self.left[level] -= 1
            print(f"::{level} title=RESULT::{esc(msg)}", flush=True)


def tail(path, n=40):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        sys.stdout.write("".join(lines[-n:]))
        sys.stdout.flush()
    except OSError:
        pass


def read(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def gh_output(key, value):
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write(f"{key}={value}\n")
    print(f"[output] {key}={value}")


def ccache_stats():
    exe = shutil.which("ccache")
    if not exe:
        return "ccache=absent"
    try:
        out = subprocess.run([exe, "--print-stats"], capture_output=True, text=True, timeout=60).stdout
    except Exception:
        return "ccache=?"
    hit = miss = 0
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) == 2 and parts[1].strip().isdigit():
            if parts[0] in ("direct_cache_hit", "preprocessed_cache_hit"):
                hit += int(parts[1])
            elif parts[0] == "cache_miss":
                miss += int(parts[1])
    return f"ccache_hit={hit} ccache_miss={miss}"


def strip_dashdash(rest):
    return rest[1:] if rest and rest[0] == "--" else rest


# ---------------------------------------------------------------- ann
def cmd_ann(a):
    Ann().emit(a.level, " ".join(a.msg))
    return 0


# ---------------------------------------------------------------- configure
RE_CONFIGURE_ERROR = re.compile(r"^(CMake Error\b|error:|fatal error\b|-- Running vcpkg install - failed)", re.I)


def first_configure_error(txt):
    """The first error line of a CMake (and vcpkg manifest install) log, with the line that explains a CMake
    Error header; '' when there is none."""
    lines = txt.splitlines()
    for i, line in enumerate(lines):
        s = line.strip()
        if not RE_CONFIGURE_ERROR.match(s):
            continue
        if s.startswith("CMake Error"):
            s += " " + next((x.strip() for x in lines[i + 1:i + 8] if x.strip()), "")
        return s[:300]
    return ""


def cmd_configure(a):
    os.makedirs(a.logs, exist_ok=True)
    log = os.path.join(a.logs, f"cmake-{a.name}.log")
    cmd = ["cmake"] + strip_dashdash(a.cmake)
    t0 = time.time()
    with open(log, "w", encoding="utf-8") as f:
        f.write("$ " + " ".join(cmd) + "\n")
        f.flush()
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
    secs = int(time.time() - t0)
    txt = read(log)
    tail(log, 30)
    info = [f"job={a.job}", f"step=configure-{a.name}", f"rc={rc}", f"secs={secs}"]
    m = re.search(r"The CXX compiler identification is (.+)", txt)
    if m:
        info.append("cxx=" + m.group(1).strip().replace(" ", "-"))
    m = re.search(r'Found Boost: (\S+) \(found suitable version "([^"]+)"', txt)
    if m:
        info.append("boost=" + m.group(2))
    m = re.search(r'Found OpenSSL: .*?\(found (?:suitable )?version "([^"]+)"', txt)
    if m:
        info.append("openssl=" + m.group(1))
    cache = read(os.path.join(a.builddir, "CMakeCache.txt"))
    for key in ("BTX_HAVE_STATIC_TEST_PROBE", "WITH_MODELNET", "BTX_ENABLE_METAL", "BTX_MATMUL_METAL_PRECOMPILE_KERNELS"):
        m = re.search(r"^" + key + r":[A-Z]+=(.*)$", cache, re.M)
        if m:
            info.append(f"{key}={m.group(1)}")
    if rc != 0:
        info.append("first_error=" + (first_configure_error(txt) or "none-in-log"))
    Ann().emit("notice" if rc == 0 else "error", " ".join(info))
    return rc


# ---------------------------------------------------------------- build
def cmd_build(a):
    os.makedirs(a.logs, exist_ok=True)
    log = os.path.join(a.logs, f"build-{a.name}.log")
    with open(os.path.join(a.logs, f"build-{a.name}.t0"), "w", encoding="utf-8") as f:
        f.write(str(int(time.time())))
    rcfile = os.path.join(a.logs, f"build-{a.name}.rc")
    if os.path.exists(rcfile):
        os.remove(rcfile)
    if shutil.which("ccache"):
        subprocess.call(["ccache", "-z"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    cmd = ["ninja", "-C", a.builddir] + strip_dashdash(a.ninja)
    t0 = time.time()
    with open(log, "w", encoding="utf-8") as f:
        f.write("$ " + " ".join(cmd) + "\n")
        f.flush()
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
    with open(rcfile, "w", encoding="utf-8") as f:
        f.write(f"{rc} {int(time.time() - t0)}\n")
    tail(log, 60)
    return rc


def cmd_build_summary(a):
    log = os.path.join(a.logs, f"build-{a.name}.log")
    rcfile = os.path.join(a.logs, f"build-{a.name}.rc")
    t0 = read(os.path.join(a.logs, f"build-{a.name}.t0")).strip()
    if os.path.exists(rcfile):
        rc, secs = read(rcfile).split()
    else:
        rc = "timeout"
        secs = str(int(time.time()) - int(t0)) if t0.isdigit() else "?"
    txt = read(log)
    failed = re.findall(r"^FAILED: (?:\[code=\d+\] )?(\S+)", txt, re.M)
    prog = re.findall(r"^\[(\d+)/(\d+)\]", txt, re.M)
    warnings = len(re.findall(r"warning: ", txt))
    exes = []
    for spec in a.exe or []:
        key, _, path = spec.partition("=")
        ok = os.path.isfile(path)
        exes.append(f"{key}={'yes' if ok else 'no'}")
        gh_output(key, "yes" if ok else "no")
    msg = [f"job={a.job}", f"step=build-{a.name}", f"rc={rc}", f"secs={secs}", f"failed_edges={len(failed)}",
           f"warnings={warnings}", ccache_stats()]
    if prog:
        msg.append(f"progress={prog[-1][0]}/{prog[-1][1]}")
    msg += exes
    if failed:
        msg.append("first_failed=" + ",".join(os.path.basename(x) for x in failed[:4]))
        err = first_compile_error(txt)
        if err:
            msg.append("first_error=" + err)
    codes = error_codes(txt)
    if codes:
        msg.append("msvc_error_codes=" + codes)
    if rc == "timeout":
        msg.append("note=step-timeout:ccache-saved,re-run-continues")
    Ann().emit("notice" if rc == "0" else "error", " ".join(msg))
    return 0


RE_COMPILE_ERROR = re.compile(r"^((?:[A-Za-z]:)?[^\s:()]+)(?::(\d+)(?::\d+)?|\((\d+)(?:,\d+)?\)):? "
                              r"(?:fatal )?error:? ?(C\d{4}:)? ?(.*)$", re.M)


def first_compile_error(txt):
    """'file:line: message' of the first gcc, clang or MSVC compile error in a ninja log, '' if none."""
    m = RE_COMPILE_ERROR.search(txt)
    if not m:
        return ""
    where = f"{os.path.basename(m.group(1).replace(chr(92), '/'))}:{m.group(2) or m.group(3)}"
    code = (m.group(4) + " ") if m.group(4) else ""
    return f"{where}: {code}{m.group(5)}".strip()[:240]


def error_codes(txt, top=8):
    """Histogram of MSVC error codes (cl.exe: 'error C4235:'), most frequent first."""
    counts = {}
    for c in re.findall(r"\berror (C\d{4})\b", txt):
        counts[c] = counts.get(c, 0) + 1
    return ",".join(f"{k}:{v}" for k, v in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:top])


# ---------------------------------------------------------------- suites
RE_RUNNING = re.compile(r"Running (\d+) test cases?")
RE_SKIP = re.compile(r'Test (suite|case) "([^"]+)" is skipped because')
RE_ERR = re.compile(r'error: in "([^"]+)"[:]?(.*)')


def kill_tree(p):
    try:
        if IS_WIN:
            subprocess.call(["taskkill", "/F", "/T", "/PID", str(p.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            os.killpg(p.pid, signal.SIGKILL)
    except Exception:
        pass
    try:
        p.kill()
    except Exception:
        pass


def list_suites(binary):
    p = subprocess.run([binary, "--list_content"], capture_output=True, text=True, errors="replace", timeout=600)
    names = []
    for line in (p.stderr + "\n" + p.stdout).splitlines():
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\*?\s*$", line)
        if m:
            names.append(m.group(1))
    return names, p.returncode


def run_one(binary, suite, timeout, extra):
    cmd = [binary, f"--run_test={suite}", "--log_level=test_suite", "--color_output=no"] + extra
    kw = {}
    if IS_WIN:
        kw["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        kw["start_new_session"] = True
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    timed_out = False
    try:
        out, _ = p.communicate(timeout=max(1, timeout))
    except subprocess.TimeoutExpired:
        timed_out = True
        kill_tree(p)
        try:
            out, _ = p.communicate(timeout=30)
        except Exception:
            out = b""
    secs = time.time() - t0
    text = (out or b"").decode("utf-8", errors="replace")
    kept, skipped_cases = [], 0
    for line in text.splitlines():
        m = RE_SKIP.search(line)
        if m:
            if m.group(1) == "case" and m.group(2).startswith(suite + "/"):
                skipped_cases += 1
                kept.append(line)
            continue
        kept.append(line)
    text = "\n".join(kept)
    m = RE_RUNNING.search(text)
    errs = RE_ERR.findall(text)
    rc = p.returncode
    if timed_out:
        status = "timeout"
    elif rc == 0:
        status = "pass"
    elif rc == 200 and "no test cases matching filter" in text:
        status = "absent"
    elif rc in (200, 201):
        status = "fail"
    else:
        status = "crash"
    first = ""
    if errs:
        first = (errs[0][0] + ":" + errs[0][1]).strip()[:240]
    elif status in ("crash", "fail", "timeout"):
        lines = [x for x in kept if x.strip()]
        first = (lines[-1] if lines else "")[:240]
    return {
        "suite": suite, "status": status, "rc": rc, "secs": round(secs, 1),
        "expected": int(m.group(1)) if m else -1,
        "cases": text.count("Leaving test case"),
        "errors": len(errs),
        "absent_msgs": text.count("absent here"),
        "skipped": skipped_cases,
        "first": first, "log": text,
    }


def shard_select(names, spec):
    """Shard K/N: every N-th suite of the sorted selection, starting at the K-th (1-based).
    The shards of one N are disjoint and together cover the selection."""
    k, sep, n = spec.partition("/")
    if not sep or not k.isdigit() or not n.isdigit():
        raise ValueError(f"shard={spec} (want K/N)")
    k, n = int(k), int(n)
    if n < 1 or not 1 <= k <= n:
        raise ValueError(f"shard={spec} (want 1<=K<=N)")
    pick = set(sorted(names)[k - 1::n])
    return [s for s in names if s in pick]


def cmd_suites(a):
    out = a.out
    os.makedirs(os.path.join(out, "logs"), exist_ok=True)
    ann = Ann(notices=a.max_notices, errors=a.max_errors, logfile=os.path.join(out, "annotations.tsv"))
    binary = os.path.abspath(a.bin)
    t_start = time.time()
    if not os.path.isfile(binary):
        ann.emit("error", f"label={a.label} job={a.job} status=no-binary bin={os.path.basename(binary)}")
        return 1
    have, lrc = list_suites(binary)
    if not have:
        ann.emit("error", f"label={a.label} job={a.job} status=list_content-failed rc={lrc}")
        return 1
    have_set = set(have)
    absent_listed = []
    if a.suites:
        want = [s.strip() for s in a.suites.split(",") if s.strip()]
        absent_listed = [s for s in want if s not in have_set]
        sel = [s for s in want if s in have_set]
    else:
        inc = re.compile(a.include)
        exc = re.compile(a.exclude) if a.exclude else None
        sel = [s for s in have if inc.search(s) and not (exc and exc.search(s))]
    if a.suites_file:
        with open(a.suites_file, "r", encoding="utf-8") as f:
            listed = [x.strip() for x in f if x.strip() and not x.lstrip().startswith("#")]
        absent_listed += [s for s in listed if s not in have_set]
        keep = set(listed)
        sel = [s for s in sel if s in keep]
    if a.shard:
        try:
            if a.shard_costs:
                sel, _ = cost_shard_select(sel, a.shard, read_costs(a.shard_costs))
            else:
                sel = shard_select(sel, a.shard)
        except (OSError, ValueError) as e:
            ann.emit("error", f"label={a.label} job={a.job} status=bad-shard {e}")
            return 1
    with open(os.path.join(out, "listed.txt"), "w", encoding="utf-8") as f:
        f.write("".join(x + "\n" for x in have))
    with open(os.path.join(out, "selected.txt"), "w", encoding="utf-8") as f:
        f.write("".join(x + "\n" for x in sel))
    serial_re = re.compile(a.serial) if a.serial else None
    par = [s for s in sel if not (serial_re and serial_re.search(s))]
    ser = [s for s in sel if serial_re and serial_re.search(s)]
    stop_at = a.stop_at if a.stop_at else None
    if a.stop_in:
        # Relative budget, so the summary is emitted before the step's own timeout fires.
        rel = int(t_start + a.stop_in * 60)
        stop_at = min(stop_at, rel) if stop_at else rel
    extra = strip_dashdash(a.extra or [])
    results = []
    rlock = threading.Lock()

    def job(suite, rep):
        now = time.time()
        if stop_at and now >= stop_at:
            r = {"suite": suite, "status": "not_run", "rc": "", "secs": 0, "expected": -1, "cases": 0,
                 "errors": 0, "absent_msgs": 0, "skipped": 0, "first": "", "log": ""}
        else:
            tmo = a.timeout
            deadline_cut = False
            if stop_at and stop_at - now < tmo:
                tmo = stop_at - now
                deadline_cut = True
            r = run_one(binary, suite, tmo, extra)
            if r["status"] == "timeout" and deadline_cut:
                r["status"] = "deadline"
        r["rep"] = rep
        if r["status"] not in ("pass", "not_run") or a.keep_logs:
            fn = os.path.join(out, "logs", f"{suite}-{rep}.log")
            with open(fn, "w", encoding="utf-8") as f:
                f.write(r["log"])
        r["log"] = ""
        with rlock:
            results.append(r)
            if r["status"] not in ("pass", "not_run"):
                print(f"[{a.label}] {suite} #{rep}: {r['status']} rc={r['rc']} {r['first']}", flush=True)
        return r

    for rep in range(1, a.repeat + 1):
        with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
            list(ex.map(lambda s: job(s, rep), par))
        for s in ser:
            job(s, rep)

    results.sort(key=lambda r: (r["suite"], r["rep"]))
    with open(os.path.join(out, "results.tsv"), "w", encoding="utf-8") as f:
        f.write("label\tsuite\trep\tstatus\trc\tsecs\texpected\tcases\terrors\tabsent_msgs\tskipped\tfirst\n")
        for r in results:
            f.write("\t".join(str(x) for x in (a.label, r["suite"], r["rep"], r["status"], r["rc"], r["secs"], r["expected"],
                                                 r["cases"], r["errors"], r["absent_msgs"], r["skipped"], r["first"])) + "\n")
    count = {}
    for r in results:
        count[r["status"]] = count.get(r["status"], 0) + 1
    bad = [r for r in results if r["status"] in ("fail", "crash", "timeout")]
    tot = lambda k: sum(r[k] for r in results)
    secs = int(time.time() - t_start)
    summary = (f"label={a.label} job={a.job} suites={len(sel)} runs={len(results)} pass={count.get('pass', 0)} "
               f"fail={count.get('fail', 0)} crash={count.get('crash', 0)} timeout={count.get('timeout', 0)} "
               f"deadline={count.get('deadline', 0)} not_run={count.get('not_run', 0)} absent={count.get('absent', 0)} "
               f"cases={tot('cases')} errors={tot('errors')} absent_msgs={tot('absent_msgs')} skipped={tot('skipped')} "
               f"secs={secs} jobs={a.jobs} repeat={a.repeat} timeout={a.timeout} shard={a.shard or 'all'}")
    if absent_listed:
        summary += " not_in_binary=" + ",".join(absent_listed)
    ann.emit("error" if bad else "notice", summary)

    if a.per_suite:
        for s in sel:
            rs = [r for r in results if r["suite"] == s]
            ok = all(r["status"] == "pass" for r in rs)
            j = lambda k: "/".join(str(r[k]) for r in rs)
            line = (f"label={a.label} suite={s} runs={len(rs)} pass={sum(r['status'] == 'pass' for r in rs)} "
                    f"status={j('status')} cases={j('cases')} errors={j('errors')} absent_msgs={j('absent_msgs')} "
                    f"skipped={j('skipped')} secs={j('secs')}")
            if not ok:
                firsts = [r["first"] for r in rs if r["first"]]
                if firsts:
                    line += f" first={firsts[0]}"
            ann.emit("notice" if ok else "error", line)
    else:
        seen, names = set(), []
        for r in bad:
            if r["suite"] not in seen:
                seen.add(r["suite"])
                names.append(r)
        shown = names[: max(0, a.max_errors - 2)]
        for r in shown:
            ann.emit("error", f"label={a.label} suite={r['suite']} status={r['status']} rc={r['rc']} "
                              f"cases={r['cases']} errors={r['errors']} secs={r['secs']} first={r['first']}")
        rest = names[len(shown):]
        if rest:
            ann.emit("error", f"label={a.label} more_failing={len(rest)}: " + ",".join(r["suite"] for r in rest))
        nr = [r["suite"] for r in results if r["status"] in ("not_run", "deadline")]
        if nr:
            ann.emit("notice", f"label={a.label} not_run_or_deadline={len(nr)} first=" + ",".join(nr[:20]))
    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump({"label": a.label, "counts": count, "selected": len(sel), "listed": len(have), "secs": secs,
                   "shard": a.shard or "all", "timeout": a.timeout, "jobs": a.jobs}, f, indent=1)
    return 1 if bad else 0


# ---------------------------------------------------------------- merge
MERGE_RANK = {"pass": 0, "absent": 1, "fail": 2, "crash": 3, "timeout": 4, "deadline": 5, "not_run": 6}


def merge_rows(files):
    """Read results.tsv files. Returns (header, rows, problems). Rows keep their file of origin."""
    header, rows, problems = None, [], []
    for fn in files:
        with open(fn, "r", encoding="utf-8", errors="replace") as f:
            lines = [x for x in f.read().splitlines() if x.strip()]
        if not lines:
            problems.append(f"empty:{fn}")
            continue
        if header is None:
            header = lines[0]
        elif lines[0] != header:
            problems.append(f"header-mismatch:{fn}")
            continue
        cols = header.split("\t")
        for line in lines[1:]:
            vals = line.split("\t")
            if len(vals) != len(cols):
                problems.append(f"bad-row:{fn}")
                continue
            r = dict(zip(cols, vals))
            r["file"] = fn
            rows.append(r)
    return header, rows, problems


def merge_final(rows):
    """One row per suite. With several runs of a suite (a later pass after a timeout), the run from the
    later file wins, so pass order on the command line is the order of the passes. A later not_run never
    replaces a result, and a later deadline (cut by the budget) only replaces a timeout or deadline that
    ran for less time. Each final row carries the history of all runs, e.g. timeout:1800.2>pass:2411.0."""
    def secs(r):
        try:
            return float(r.get("secs") or 0)
        except ValueError:
            return 0.0

    final, order, hist = {}, [], {}
    for r in rows:
        s = r["suite"]
        hist.setdefault(s, []).append(f"{r['status']}:{r.get('secs', '')}")
        if s not in final:
            order.append(s)
            final[s] = r
            continue
        old = final[s]
        if r["status"] == "not_run":
            continue
        if r["status"] == "deadline" and old["status"] in ("timeout", "deadline") and secs(r) <= secs(old):
            continue
        final[s] = r
    out = []
    for s in order:
        r = dict(final[s])
        r["history"] = ">".join(hist[s])
        out.append(r)
    return out


def cmd_merge(a):
    ann = Ann(notices=a.max_notices, errors=a.max_errors)
    files = []
    for pat in a.inputs:
        got = sorted(glob.glob(pat, recursive=True))
        files += [g for g in got if g not in files]
    if not files:
        ann.emit("error", f"label={a.label} status=no-inputs patterns={','.join(a.inputs)}")
        return 1
    header, rows, problems = merge_rows(files)
    final = merge_final(rows)
    listed = set()
    for pat in a.listed or []:
        for fn in sorted(glob.glob(pat, recursive=True)):
            with open(fn, "r", encoding="utf-8", errors="replace") as f:
                listed |= {x.strip() for x in f if x.strip()}
    seen = {r["suite"] for r in final}
    unaccounted = sorted(listed - seen)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "results-all.tsv"), "w", encoding="utf-8") as f:
        f.write((header or "") + "\tfile\thistory\n")
        for r in sorted(final, key=lambda r: r["suite"]):
            f.write("\t".join(r[c] for c in header.split("\t")) + "\t" + r["file"] + "\t" + r["history"] + "\n")
    count = {}
    for r in final:
        count[r["status"]] = count.get(r["status"], 0) + 1
    by = {}
    for r in final:
        by.setdefault(r["status"], []).append(r["suite"])
    summary = {"label": a.label, "files": files, "suites": len(final), "counts": count,
               "listed": len(listed), "unaccounted": unaccounted, "problems": problems,
               "by_status": {k: sorted(v) for k, v in by.items() if k != "pass"}}
    with open(os.path.join(a.out, "summary-all.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    for st in ("timeout", "deadline", "not_run"):
        with open(os.path.join(a.out, f"retry-{st}.txt"), "w", encoding="utf-8") as f:
            f.write("".join(x + "\n" for x in sorted(by.get(st, []))))
    bad = [r for r in final if r["status"] in ("fail", "crash", "timeout")]
    line = (f"label={a.label} files={len(files)} suites={len(final)} listed={len(listed)} unaccounted={len(unaccounted)} "
            + " ".join(f"{k}={count.get(k, 0)}" for k in ("pass", "fail", "crash", "timeout", "deadline", "not_run", "absent"))
            + (f" problems={len(problems)}" if problems else ""))
    ann.emit("error" if (bad or unaccounted or problems) else "notice", line)
    for st in ("fail", "crash", "timeout"):
        names = sorted(by.get(st, []))
        if names:
            ann.emit("error", f"label={a.label} {st}={len(names)}: " + ",".join(names))
    if unaccounted:
        ann.emit("error", f"label={a.label} unaccounted={len(unaccounted)} first=" + ",".join(unaccounted[:30]))
    return 1 if (bad or unaccounted or problems) else 0


# ---------------------------------------------------------------- prune (mingw test_btx)
def obj_to_src(obj, target, src_root):
    """Build-dir object path of <target> (relative to the build dir) -> absolute source path, or None."""
    marker = f"/CMakeFiles/{target}.dir/"
    t = obj.replace("\\", "/")
    if marker not in t:
        return None
    tdir, rel = t.split(marker, 1)
    rel = re.sub(r"\.(obj|o)$", "", rel)
    parts = [".." if p == "__" else p for p in rel.split("/")]
    return os.path.normpath(os.path.join(os.path.abspath(src_root), tdir, *parts))


def drop_sources(src_root, target, want):
    """Remove the files in `want` (absolute paths) from add_executable(<target> ...) and
    target_sources(<target> ...) blocks under src/. A library that lists the same file keeps it.
    Returns the dropped files relative to src_root."""
    pruned = []
    start = re.compile(r"^\s*(add_executable|target_sources)\(\s*" + re.escape(target) + r"(\s|$|\))")
    for root, _dirs, files in os.walk(os.path.join(src_root, "src")):
        if "CMakeLists.txt" not in files:
            continue
        cml = os.path.join(root, "CMakeLists.txt")
        lines = read(cml).splitlines(keepends=True)
        keep, changed, depth = [], False, 0
        for line in lines:
            code = line.split("#", 1)[0]
            if depth == 0 and start.match(code):
                depth = code.count("(") - code.count(")")
                keep.append(line)
                continue
            if depth > 0:
                tok = code.strip()
                if re.fullmatch(r"[A-Za-z0-9_./+-]+\.(cpp|cc|c)", tok):
                    full = os.path.normpath(os.path.join(os.path.abspath(root), tok))
                    if full in want:
                        pruned.append(os.path.relpath(full, os.path.abspath(src_root)).replace("\\", "/"))
                        changed = True
                        continue
                depth += code.count("(") - code.count(")")
            keep.append(line)
        if changed:
            with open(cml, "w", encoding="utf-8", newline="") as f:
                f.write("".join(keep))
    return pruned


def prune_state(logs):
    try:
        with open(os.path.join(logs, "pruned.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"compile": [], "link": [], "other": [], "unmapped": [], "rounds": []}


def write_prune_state(logs, st):
    os.makedirs(logs, exist_ok=True)
    with open(os.path.join(logs, "pruned.json"), "w", encoding="utf-8") as f:
        json.dump(st, f, indent=1)
    with open(os.path.join(logs, "pruned.txt"), "w", encoding="utf-8") as f:
        f.write("# test sources removed from the mingw test_btx build because they do not compile there\n")
        f.write("".join(x + "\n" for x in sorted(st["compile"])))
        f.write("# test sources removed because they use code from the files above (undefined at link time)\n")
        f.write("".join(x + "\n" for x in sorted(st["link"])))
        for r in st["rounds"]:
            f.write(f"#   link round {r['round']}: dropped {len(r['dropped'])}; first undefined: {' | '.join(r['first_undefined'][:6])}\n")
        f.write("# failed edges outside test_btx objects\n" + "".join(x + "\n" for x in st["other"]))
        f.write("# failed test objects that could not be mapped to a CMakeLists line\n" + "".join(x + "\n" for x in st["unmapped"]))


def cmd_prune(a):
    ann = Ann()
    txt = read(a.log)
    failed = [t.replace("\\", "/") for t in re.findall(r"^FAILED: (?:\[code=\d+\] )?(\S+)", txt, re.M)]
    srcs, other = [], []
    for t in failed:
        s = obj_to_src(t, a.target, a.src)
        if s is None:
            other.append(t)
        else:
            srcs.append(s)
    pruned = drop_sources(a.src, a.target, set(srcs))
    pruned_set = set(pruned)
    unpruned = [os.path.relpath(s, os.path.abspath(a.src)).replace("\\", "/") for s in srcs]
    unpruned = [s for s in unpruned if s not in pruned_set]
    st = prune_state(a.logs)
    st["compile"] = sorted(set(st["compile"]) | pruned_set)
    st["other"], st["unmapped"] = other, unpruned
    write_prune_state(a.logs, st)
    names = sorted(set(os.path.basename(p) for p in pruned))
    swarm = [n for n in names if n.startswith("swarm_")]
    htlc = [n for n in names if "htlc" in n]
    ann.emit("notice" if not other else "error",
             f"job={a.job} step=prune failed_edges={len(failed)} test_objects={len(srcs)} pruned={len(pruned)} "
             f"pruned_swarm={len(swarm)} pruned_htlc={len(htlc)} unmapped={len(unpruned)} other_failed={len(other)} "
             f"other_first={','.join(os.path.basename(o) for o in other[:4])}")
    if names:
        ann.emit("notice", f"job={a.job} step=prune-list n={len(names)}: " + ",".join(n[:-4] if n.endswith('.cpp') else n for n in names))
    gh_output("pruned", str(len(pruned)))
    return 0


TEST_DEFS_MARK = "# fork CI only (mingw): test_btx gets secp256k1's interface definitions"


def cmd_test_defs(a):
    """CI-only edit: give <target> the INTERFACE_COMPILE_DEFINITIONS of secp256k1 (SECP256K1_STATIC for a
    static build). test_btx already takes secp256k1's include directories, and key_tests.cpp includes
    <secp256k1.h>; without the definition, SECP256K1_API is dllimport on Windows and the link looks for
    __imp_secp256k1_* (run 1). On Linux and macOS the definition has no effect."""
    ann = Ann()
    cml = os.path.join(a.src, "src", "test", "CMakeLists.txt")
    txt = read(cml)
    if not txt:
        ann.emit("error", f"job={a.job} step=test-defs status=fail missing={cml}")
        return 1
    if TEST_DEFS_MARK in txt:
        ann.emit("notice", f"job={a.job} step=test-defs status=already-present")
        return 0
    line = f"target_compile_definitions({a.target} PRIVATE $<TARGET_PROPERTY:secp256k1,INTERFACE_COMPILE_DEFINITIONS>)\n"
    with open(cml, "a", encoding="utf-8", newline="") as f:
        f.write(("" if txt.endswith("\n") else "\n") + "\n" + TEST_DEFS_MARK + "\n" + line)
    ann.emit("notice", f"job={a.job} step=test-defs status=ok file=src/test/CMakeLists.txt added=" + line.strip().replace(" ", "_"))
    return 0


RE_IN_FUNC = re.compile(r"(\S+?\.obj): in function")
RE_UNDEF_OBJ = re.compile(r"(\S+?\.obj):\S*:?\s*(?:more )?undefined references? to [`']([^'`]+)'")
RE_UNDEF_LIB = re.compile(r"(\S+?\.a)\(([^)]+)\):\S*:?\s*(?:more )?undefined references? to [`']([^'`]+)'")
RE_UNDEF_ANY = re.compile(r"undefined references? to [`']([^'`]+)'")


def link_undefined(txt, target):
    """Parse GNU ld errors: (test objects of <target> with undefined references, archive members with
    undefined references, first undefined symbols). Handles the one-line form and the 'in function' form."""
    objs, libs, syms = [], [], []
    last_obj = None
    for line in txt.splitlines():
        m = RE_IN_FUNC.search(line)
        if m:
            last_obj = m.group(1)
            continue
        if "undefined reference" not in line:
            continue
        ml = RE_UNDEF_LIB.search(line)
        mo = RE_UNDEF_OBJ.search(line)
        ma = RE_UNDEF_ANY.search(line)
        if ma and ma.group(1) not in syms:
            syms.append(ma.group(1))
        if ml:
            libs.append(os.path.basename(ml.group(1)) + "(" + ml.group(2) + ")")
        elif mo:
            objs.append(mo.group(1))
        elif last_obj:
            objs.append(last_obj)
    marker = f"/CMakeFiles/{target}.dir/"
    tobjs = sorted(set(o.replace("\\", "/") for o in objs if marker in o.replace("\\", "/")))
    other = sorted(set(o for o in objs if marker not in o.replace("\\", "/"))) + sorted(set(libs))
    return tobjs, other, syms


def cmd_relink(a):
    """Build <target> again after `prune`; when the link fails because kept test files use code from the
    pruned ones, drop those test files too and link again, up to --rounds times."""
    ann = Ann()
    os.makedirs(a.logs, exist_ok=True)
    final = os.path.join(a.logs, f"build-{a.name}.log")
    t0 = time.time()
    with open(os.path.join(a.logs, f"build-{a.name}.t0"), "w", encoding="utf-8") as f:
        f.write(str(int(t0)))
    rcfile = os.path.join(a.logs, f"build-{a.name}.rc")
    if os.path.exists(rcfile):
        os.remove(rcfile)
    if shutil.which("ccache"):
        subprocess.call(["ccache", "-z"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    st = prune_state(a.logs)
    cmd = ["ninja", "-C", a.builddir] + strip_dashdash(a.ninja)
    rc, rnd, txt = 1, 0, ""
    while rnd < a.rounds:
        rnd += 1
        log = os.path.join(a.logs, f"build-{a.name}-r{rnd}.log")
        with open(log, "w", encoding="utf-8") as f:
            f.write("$ " + " ".join(cmd) + "\n")
            f.flush()
            rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
        txt = read(log)
        if rc == 0:
            ann.emit("notice", f"job={a.job} step=relink round={rnd} rc=0 linked=yes dropped_for_link={len(st['link'])}")
            break
        failed = re.findall(r"^FAILED: (?:\[code=\d+\] )?(\S+)", txt, re.M)
        tobjs, other, syms = link_undefined(txt, a.target)
        want = set(filter(None, (obj_to_src(o, a.target, a.src) for o in tobjs)))
        # Undefined references from a library cannot be fixed by dropping test files, and an __imp_ symbol
        # is a dllimport/static mismatch in the build setup, not code from a pruned file: stop and report.
        imp = [x for x in syms if x.startswith("__imp_")]
        dropped = drop_sources(a.src, a.target, want) if want and not other and not imp else []
        st["link"] = sorted(set(st["link"]) | set(dropped))
        st["rounds"].append({"round": rnd, "dropped": dropped, "first_undefined": syms[:12],
                             "undefined_in_libraries": other[:12]})
        write_prune_state(a.logs, st)
        names = ",".join(os.path.basename(d)[:-4] for d in dropped[:12])
        ann.emit("error" if not dropped else "notice",
                 f"job={a.job} step=relink round={rnd} rc={rc} failed_edges={len(failed)} "
                 f"first_failed={','.join(os.path.basename(x) for x in failed[:3])} undefined_symbols={len(syms)} "
                 f"test_objects_with_undefined={len(tobjs)} undefined_outside_tests={len(other)} dllimport_misses={len(imp)} "
                 f"objects={','.join(os.path.basename(o) for o in tobjs[:4])} dropped={len(dropped)} "
                 f"names={names or 'none'} first_undefined={' | '.join(syms[:3])[:300]}")
        if not dropped:
            break
    # build-summary reads this file: the last round only, so its counts are that round's
    with open(final, "w", encoding="utf-8") as f:
        f.write(txt)
    with open(rcfile, "w", encoding="utf-8") as f:
        f.write(f"{rc} {int(time.time() - t0)}\n")
    gh_output("relink_rounds", str(rnd))
    gh_output("link_dropped", str(len(st["link"])))
    tail(final, 60)
    return rc


# ---------------------------------------------------------------- dlls (bundle mingw runtime DLLs)
def cmd_dlls(a):
    ann = Ann()
    dirs = []
    for pat in a.search:
        dirs += [d for d in glob.glob(pat) if os.path.isdir(d)]
    index = {}
    for d in dirs:
        for fn in os.listdir(d):
            if fn.lower().endswith(".dll"):
                index.setdefault(fn.lower(), os.path.join(d, fn))
    os.makedirs(a.dest, exist_ok=True)
    queue = [e for e in a.exe if os.path.isfile(e)]
    imports, bundled, system = {}, {}, set()
    while queue:
        f = queue.pop()
        out = subprocess.run([a.objdump, "-p", f], capture_output=True, text=True, errors="replace").stdout
        names = re.findall(r"DLL Name: (\S+)", out)
        imports[os.path.basename(f)] = names
        for n in names:
            k = n.lower()
            if k in bundled or k in system:
                continue
            if k in index:
                bundled[k] = index[k]
                shutil.copy2(index[k], os.path.join(a.dest, os.path.basename(index[k])))
                queue.append(index[k])
            else:
                system.add(n)
    with open(os.path.join(a.dest, "MANIFEST-dlls.json"), "w", encoding="utf-8") as f:
        json.dump({"imports": imports, "bundled": sorted(os.path.basename(v) for v in bundled.values()),
                   "system": sorted(system)}, f, indent=1)
    sizes = []
    for e in a.exe:
        if os.path.isfile(e):
            sizes.append(f"{os.path.basename(e)}={os.path.getsize(e) // (1024 * 1024)}MB")
        else:
            sizes.append(f"{os.path.basename(e)}=missing")
    ann.emit("notice", f"job={a.job} step=package {' '.join(sizes)} bundled_dlls={','.join(sorted(os.path.basename(v) for v in bundled.values())) or 'none'} "
                       f"system_dlls={','.join(sorted(system))}")
    return 0


# ---------------------------------------------------------------- smoke (regtest btxd)
RE_NO_COOKIE = re.compile(r"Could not locate RPC credentials|Authorization failed")


def find_cookies(D):
    """Every .cookie under the datadir, plus the default datadir (diagnostics only)."""
    found = sorted(glob.glob(os.path.join(D, "**", ".cookie"), recursive=True))
    for env in ("LOCALAPPDATA", "APPDATA", "HOME"):
        base = os.environ.get(env)
        if base:
            for sub in ("BTX", ".btx"):
                found += sorted(glob.glob(os.path.join(base, sub, "**", ".cookie"), recursive=True))
    return found


def wait_cookie(want, proc, budget):
    """Wait until btxd has written its RPC cookie. btx-cli reads the cookie once per attempt, before it
    connects, and -rpcwait does not retry an HTTP 401. A cli call made while the RPC port is bound but
    the cookie is not yet written fails for good with 'Could not locate RPC credentials'."""
    t0 = time.time()
    while time.time() - t0 < budget:
        try:
            if os.path.getsize(want) > 0:
                return True, time.time() - t0
        except OSError:
            pass
        if proc.poll() is not None:
            break
        time.sleep(0.25)
    return False, time.time() - t0


def cmd_smoke(a):
    ann = Ann(notices=9, errors=9)
    D = os.path.abspath(a.datadir)
    os.makedirs(D, exist_ok=True)
    btxd, cli = os.path.abspath(a.btxd), os.path.abspath(a.cli)
    extra = a.extra or []
    tag = f"label={a.label} job={a.job}"
    # The same datadir and the same network for btxd and btx-cli, as in the tester guide: regtest=1 in
    # <datadir>/btx.conf (both programs read it) and -datadir/-regtest on both command lines.
    with open(os.path.join(D, "btx.conf"), "w", encoding="ascii", newline="\n") as f:
        f.write("regtest=1\n")
    common = [f"-datadir={D}", "-regtest"]
    cookie = os.path.join(D, "regtest", ".cookie")
    cmd = [btxd] + common + ["-server=1", "-listen=0", "-discover=0", "-dnsseed=0", "-fixedseeds=0", "-listenonion=0",
                             "-upnp=0", "-natpmp=0", "-connect=0", "-printtoconsole=0", "-fallbackfee=0.0002",
                             "-modelbind=0", f"-port={a.port}", f"-rpcport={a.rpcport}"] + extra
    clic = [cli] + common + [f"-rpcport={a.rpcport}"]
    done, failed, late_fail = [], [], []

    def rpc(*args, timeout=120):
        try:
            p = subprocess.run(clic + list(args), capture_output=True, text=True, errors="replace", timeout=timeout)
            return p.returncode, p.stdout.strip(), p.stderr.strip()
        except subprocess.TimeoutExpired:
            return -999, "", f"timeout after {timeout}s"

    def fail(step, why):
        failed.append(step)
        ann.emit("error", f"{tag} step={step} status=fail {why}")

    try:
        ver = subprocess.run([btxd, "-version"], capture_output=True, text=True, errors="replace", timeout=60).stdout.splitlines()
        ver = ver[0] if ver else "?"
    except Exception as e:
        ver = f"error:{e}"
    logf = open(os.path.join(D, "btxd-console.txt"), "w", encoding="utf-8")
    kw = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if IS_WIN else {"start_new_session": True}
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, **kw)
    have, cwait = wait_cookie(cookie, proc, 240)
    cinfo = f"cookie={cookie} cookie_found={'yes' if have else 'no'} cookie_wait={cwait:.1f}s"
    if not have:
        # Not where the cli looks. Say where it is instead; if it is somewhere else, point the cli at it
        # so the remaining steps still run, and report the mismatch as an error.
        others = find_cookies(D)
        cinfo += f" cookies_elsewhere={','.join(others) or 'none'}"
        late_fail.append("cookie")
        if others:
            clic.append(f"-rpccookiefile={others[0]}")
            ann.emit("error", f"{tag} step=cookie status=fail expected={cookie} found={others[0]} (cli pointed at it)")
    attempts = 0
    while True:
        attempts += 1
        rc, out, err = rpc("-rpcwait", "-rpcwaittimeout=240", "getblockchaininfo", timeout=270)
        if rc == 0 or attempts >= 5 or not RE_NO_COOKIE.search(err) or proc.poll() is not None:
            break
        time.sleep(1)
    cinfo += f" rpc_attempts={attempts}"
    if rc != 0:
        fail("start", f"rc={rc} alive={proc.poll() is None} exit={proc.poll()} version={ver} {cinfo} err={err[:200]}")
    else:
        info = json.loads(out)
        ann.emit("notice", f"{tag} step=start status=ok secs={time.time() - t0:.1f} version={ver} chain={info.get('chain')} "
                           f"blocks={info.get('blocks')} {cinfo} args={' '.join(extra) or 'none'}")
        done.append("start")
        for method in a.capture or []:
            rc, out, err = rpc(method)
            text = out if rc == 0 else f"rc={rc} {err}"
            if a.out:
                os.makedirs(a.out, exist_ok=True)
                with open(os.path.join(a.out, f"{a.label}-{method}.txt"), "w", encoding="utf-8") as f:
                    f.write(text + "\n")
            ann.emit("notice", f"{tag} step=capture method={method} rc={rc} answer={' '.join(text.split())[:300]}")
        if a.linger:
            time.sleep(a.linger)
            rc, out, err = rpc("getnetworkinfo")
            alive = proc.poll() is None
            if rc == 0 and alive:
                ni = json.loads(out)
                ann.emit("notice", f"{tag} step=linger status=ok secs={a.linger} alive=yes subversion={ni.get('subversion')}")
                done.append("linger")
            else:
                fail("linger", f"rc={rc} alive={alive} err={err[:200]}")
        addr = None
        if a.wallet and not failed:
            rc1, _, e1 = rpc("createwallet", "ci")
            rc2, addr, e2 = rpc("-rpcwallet=ci", "getnewaddress")
            if rc1 == 0 and rc2 == 0 and addr:
                ann.emit("notice", f"{tag} step=wallet status=ok address_kind={addr[:5]}... len={len(addr)}")
                done.append("wallet")
            else:
                addr = None
                fail("wallet", f"createwallet_rc={rc1} getnewaddress_rc={rc2} err={(e1 or e2)[:200]}")
        height = 0
        if addr and a.mine > 0:
            t1 = time.time()
            rc, out, err = rpc("generatetoaddress", "1", addr, timeout=min(a.mine_budget, 900))
            if rc != 0:
                fail("mine1", f"rc={rc} secs={time.time() - t1:.1f} err={err[:240]}")
            else:
                first = time.time() - t1
                ann.emit("notice", f"{tag} step=mine1 status=ok secs={first:.1f}")
                done.append("mine1")
                rc, out, _ = rpc("getblockcount")
                height = int(out) if rc == 0 and out.isdigit() else 1
                while height < a.mine and time.time() - t1 < a.mine_budget:
                    n = min(10, a.mine - height)
                    left = a.mine_budget - (time.time() - t1)
                    rc, out, err = rpc("generatetoaddress", str(n), addr, timeout=max(30, int(left)))
                    if rc != 0:
                        break
                    rc2, out2, _ = rpc("getblockcount")
                    height = int(out2) if rc2 == 0 and out2.isdigit() else height
                secs = time.time() - t1
                level_ok = height >= a.mine
                msg = f"{tag} step=mine status={'ok' if level_ok else 'short'} target={a.mine} height={height} secs={secs:.1f}"
                if level_ok:
                    ann.emit("notice", msg)
                    done.append("mine")
                else:
                    fail("mine", msg.replace(tag + " step=mine ", "") + f" last_err={err[:160]}")
        if addr and a.send and height >= 101:
            rc, out, err = rpc("-rpcwallet=ci", "getbalances")
            trusted = json.loads(out).get("mine", {}).get("trusted") if rc == 0 else None
            rc, addr2, err = rpc("-rpcwallet=ci", "getnewaddress")
            rc, txid, err = rpc("-rpcwallet=ci", "sendtoaddress", addr2, "1.0") if rc == 0 else (rc, "", err)
            if rc != 0 or not txid:
                fail("send", f"rc={rc} trusted={trusted} err={err[:240]}")
            else:
                rc, _, err = rpc("generatetoaddress", "1", addr, timeout=600)
                rc2, out2, err2 = rpc("-rpcwallet=ci", "gettransaction", txid)
                conf = json.loads(out2).get("confirmations") if rc2 == 0 else None
                fee = json.loads(out2).get("fee") if rc2 == 0 else None
                if rc == 0 and conf and conf >= 1:
                    ann.emit("notice", f"{tag} step=send status=ok confirmations={conf} fee={fee} trusted_before={trusted}")
                    done.append("send")
                else:
                    fail("send", f"generate_rc={rc} gettransaction_rc={rc2} confirmations={conf} err={(err or err2)[:200]}")
    # stop
    t2 = time.time()
    if proc.poll() is None:
        rc, _, err = rpc("stop", timeout=60)
        try:
            proc.wait(timeout=180)
            code = proc.returncode
            if code == 0:
                ann.emit("notice", f"{tag} step=stop status=ok exit=0 stop_rc={rc} secs={time.time() - t2:.1f}")
                done.append("stop")
            else:
                fail("stop", f"exit={code} stop_rc={rc}")
        except subprocess.TimeoutExpired:
            fail("stop", f"still running 180s after stop (stop_rc={rc}); killed")
            kill_tree(proc)
    else:
        fail("stop", f"btxd had already exited with {proc.returncode}")
    logf.close()
    failed += late_fail  # a cookie in the wrong place fails the smoke, after the other steps have run
    dbg = os.path.join(D, "regtest", "debug.log")
    txt = read(dbg)
    errs = len(re.findall(r"(?i)\b(error|exception|assert)", txt))
    modeld = len(re.findall(r"(?i)modeld", txt))
    swarm = len(re.findall(r"(?i)swarm", txt))
    m = re.search(r"Generated RPC authentication cookie (\S+)", txt)
    logged_cookie = m.group(1) if m else "not-in-log"
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        for src in (dbg, os.path.join(D, "btxd-console.txt")):
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(a.out, f"{a.label}-{os.path.basename(src)}"))
    ann.emit("error" if failed else "notice",
             f"{tag} summary={'fail' if failed else 'ok'} done={','.join(done) or 'none'} failed={','.join(failed) or 'none'} "
             f"debuglog_lines={txt.count(chr(10))} debuglog_error_words={errs} modeld_lines={modeld} swarm_lines={swarm} "
             f"cookie_in_debuglog={logged_cookie}")
    return 1 if failed else 0


# ---------------------------------------------------------------- msvc-run (Windows)
def parse_set_output(text):
    """Parse the output of cmd's `set` into an environment dict with upper-case keys."""
    env = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep and key and not key.startswith(" "):
            env[key.upper()] = value
    return env


def edit_path(path, prepend, drop):
    parts = [p for p in path.split(";") if p]
    low = [d.lower() for d in drop]
    parts = [p for p in parts if not any(d in p.lower() for d in low)]
    return ";".join(list(prepend) + parts)


def cmd_msvc_run(a):
    if not IS_WIN:
        print("msvc-run: Windows only", flush=True)
        return 2
    root = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(root, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    vs = subprocess.run([vswhere, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                         "-property", "installationPath"], capture_output=True, text=True, errors="replace").stdout.strip()
    devcmd = os.path.join(vs, "Common7", "Tools", "VsDevCmd.bat") if vs else ""
    if not devcmd or not os.path.isfile(devcmd):
        Ann().emit("error", f"job={a.job} step=msvc-env status=no-vsdevcmd vs={vs or 'none'}")
        return 2
    # VsDevCmd's own output goes to nul (as in contrib/devtools/build-btx-windows.ps1), so stdout holds
    # only the UTF-16 output of `set`; stderr is kept apart and shown if the environment is missing.
    p = subprocess.run('cmd /d /u /s /c "call "{}" -no_logo -arch=x64 -host_arch=x64 >nul && set"'.format(devcmd),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    env = parse_set_output(p.stdout.decode("utf-16le", errors="replace"))
    if "VCTOOLSVERSION" not in env:
        print(p.stderr.decode("utf-8", errors="replace")[-2000:], flush=True)
        Ann().emit("error", f"job={a.job} step=msvc-env status=vsdevcmd-failed rc={p.returncode} devcmd={devcmd}")
        return 2
    env["PATH"] = edit_path(env.get("PATH", ""), a.path_prepend or [], a.path_drop or [])
    env["VSLANG"] = "1033"  # English compiler messages
    for k in ("CL", "_CL_"):
        env.pop(k, None)
    cmd = strip_dashdash(a.argv)
    if not cmd:
        print(f"VCToolsVersion={env.get('VCTOOLSVERSION')} WindowsSDKVersion={env.get('WINDOWSSDKVERSION', '').strip(chr(92))}")
        return 0
    exe = shutil.which(cmd[0], path=env["PATH"]) or cmd[0]
    return subprocess.call([exe] + cmd[1:], env=env)


# ---------------------------------------------------------------- userns (Linux)
def cmd_userns(a):
    ann = Ann()
    res = []
    ok_user = False
    for name, cmd in (("user", ["unshare", "-Ur", "true"]), ("user_mount", ["unshare", "-Urm", "true"]),
                      ("user_net", ["unshare", "-Urn", "true"]), ("user_pid", ["unshare", "-Urpf", "--mount-proc", "true"])):
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            rc = p.returncode
            note = p.stderr.strip().replace(" ", "_")[:80]
        except Exception as e:
            rc, note = -1, str(e)[:80]
        res.append(f"{name}={'ok' if rc == 0 else 'rc' + str(rc)}" + ("" if rc == 0 else f"({note})"))
        if name == "user":
            ok_user = rc == 0
    for path in ("/proc/sys/kernel/apparmor_restrict_unprivileged_userns", "/proc/sys/kernel/unprivileged_userns_clone",
                 "/proc/sys/user/max_user_namespaces"):
        v = read(path).strip()
        res.append(f"{os.path.basename(path)}={v if v else 'absent'}")
    res.append(f"uid={os.getuid()}")
    ann.emit("notice" if ok_user else "error", f"job={a.job} step=userns " + " ".join(res))
    return 0 if ok_user else 1


def selftest_additions():
    """Checks for the ci-0.34.16 additions (no files, no network)."""
    names = [f"s{i:03d}" for i in range(40)]
    costs = {n: float(i * 7 % 13 + (300 if i % 9 == 0 else 0)) for i, n in enumerate(names) if i != 5}
    members, load = cost_shards(names, 6, costs)
    flat = sorted(sum((sorted(m) for m in members), []))
    assert flat == sorted(names) and sum(len(m) for m in members) == len(names)
    assert max(load) - min(load) <= max(costs.values())
    first, _ = cost_shard_select(names, "1/6", costs)
    assert set(first) == members[0] and costs.get(first[0], 0) >= costs.get(first[-1], 0)
    assert cost_shard_select(names, "1/1", costs)[0][0] == max(names, key=lambda n: (costs.get(n, 0), n))
    for bad in ("0/3", "4/3", "x"):
        try:
            parse_shard(bad)
            raise AssertionError(bad)
        except ValueError:
            pass
    allx = ["a.py", "a.py --x", "b.py --y", "b.py --y --z", "c.py"]
    assert functional_args(["a.py", "b.py --y"], allx) == (["a.py", "b.py --y"], ["a.py --x", "b.py --y --z"])
    assert functional_args(["a.py", "a.py --x", "c.py"], allx) == (["a.py", "c.py"], [])
    assert functional_args(["a.py --x", "c.py"], allx) == (["a.py --x", "c.py"], [])
    res = {k: {} for k in KNOWN_KINDS}
    res["unit"] = {"u1": ("fail", 1.0, "x"), "u2": ("pass", 1.0, ""), "u3": ("timeout", 900.0, ""), "u4": ("not_run", 0, "")}
    res["functional"] = {"f1.py": ("Failed", 9.0, ""), "f2.py": ("Passed", 1.0, ""), "f3.py": ("Skipped", 0, "")}
    known = {("unit", "u1"): ["unit", "u1", "c", "l", "s"], ("unit", "u2"): ["unit", "u2", "c", "l", "s"],
             ("functional", "f1.py"): ["functional", "f1.py", "c", "l", "s"]}
    new, still, fixed, unmeasured, nxt = known_gate(res, known, "abc")
    assert [n[1] for n in new] == ["u3"] and len(still) == 2 and [f[1] for f in fixed] == ["u2"]
    assert [u[1] for u in unmeasured] == ["u4"] and sorted(r[1] for r in nxt) == ["f1.py", "u1", "u3"]
    pem = "-----BEGIN " + "PRIVATE KEY-----"  # split so this file never holds the marker itself
    diff = ("+++ b/src/a.cpp\n@@ -1,0 +5,2 @@\n+int x;\n+// " + pem.replace("BEGIN ", "BEGIN EC ") + "\n"
            "+++ b/src/test/data/k.pem\n@@ -0,0 +1 @@\n+" + pem + "\n"
            "+++ b/doc/x.md\n@@ -3 +3 @@\n-old\n+token: ghp_" + "a" * 36 + "\n")
    assert scan_added_lines(diff) == [("src/a.cpp", 6, "private-key-block"), ("doc/x.md", 3, "github-token")]
    assert is_test_path("wallet/test/a.cpp") and is_test_path("leveldb/x.cc") and not is_test_path("wallet/rpc/a.cpp")
    base = {("nullPointer", "a.cpp"): 1, ("uninitMemberVar", "b.h"): 2}
    rs = [("nullPointer", "error", "a.cpp", "3", "m"), ("nullPointer", "error", "a.cpp", "9", "m"),
          ("uninitMemberVar", "warning", "b.h", "1", "m")]
    br = "Checking commit range (a..b):\nabc1234 ci: x\nsrc/a.cpp:12:3: trailing\nsrc/b.cpp:7: x\n"
    bs = "src/a.cpp:40:3: trailing\n"
    assert lint_norm(br, ["/w"]) == ["src/a.cpp:N: trailing", "src/b.cpp:N: x"] and lint_norm(bs, []) == ["src/a.cpp:N: trailing"]
    assert lint_norm("args: {'-gen', '-b'}\n", []) == lint_norm("args: {'-b', '-gen'}\n", []) == ["args: {'-b', '-gen'}"]
    grown, shrunk, _ = cppcheck_compare(rs, base)
    assert grown == [(("nullPointer", "a.cpp"), 2, 1)] and shrunk == [(("uninitMemberVar", "b.h"), 1, 2)]


def cmd_selftest(a):
    assert esc("a%b\nc") == "a%25b%0Ac"
    names = [f"s{i:03d}" for i in range(23)][::-1]
    shards = [shard_select(names, f"{k}/5") for k in range(1, 6)]
    assert sorted(sum(shards, [])) == sorted(names) and sum(len(x) for x in shards) == len(names)
    assert shard_select(names, "1/1") == names
    for bad in ("0/3", "4/3", "3", "a/b", "1/0"):
        try:
            shard_select(names, bad)
            raise AssertionError(bad)
        except ValueError:
            pass
    rows = [{"suite": "a", "status": "timeout", "secs": "1800.1"}, {"suite": "b", "status": "pass", "secs": "1"},
            {"suite": "a", "status": "pass", "secs": "2400"}, {"suite": "b", "status": "not_run", "secs": "0"},
            {"suite": "c", "status": "timeout", "secs": "1800"}, {"suite": "c", "status": "deadline", "secs": "900"}]
    fin = {r["suite"]: r for r in merge_final(rows)}
    assert fin["a"]["status"] == "pass" and fin["a"]["history"] == "timeout:1800.1>pass:2400"
    assert fin["b"]["status"] == "pass" and fin["c"]["status"] == "timeout"
    assert error_codes("x(1): error C4235: a\ny(2): error C3861: b\nz: error C4235: c") == "C4235:2,C3861:1"
    assert parse_set_output("Path=C:\\a;C:\\b\r\nVCToolsVersion=14.44.35207\r\n=C:=C:\\x") == {
        "PATH": "C:\\a;C:\\b", "VCTOOLSVERSION": "14.44.35207"}
    assert edit_path("C:\\x;C:\\Strawberry\\c\\bin;;C:\\y", ["C:\\L"], ["\\strawberry\\"]) == "C:\\L;C:\\x;C:\\y"
    log = ("-- Running vcpkg install\nerror: building berkeleydb:x64-windows-static failed with: BUILD_FAILED\n"
           "-- Running vcpkg install - failed\nCMake Error at D:/w/vcpkg/scripts/buildsystems/vcpkg.cmake:938 (message):\n"
           "  vcpkg install failed.\n")
    assert first_configure_error(log) == "error: building berkeleydb:x64-windows-static failed with: BUILD_FAILED"
    assert first_configure_error("-- Performing Test HAVE_ERROR\nCMake Error at x.cmake:1 (message):\n\n  no cl\n") == \
        "CMake Error at x.cmake:1 (message): no cl"
    assert first_configure_error("-- Configuring done\n") == ""
    gcc = ("[3297/3301] Building CXX object x.obj\nFAILED: x.obj\n/w/src/modelnet/realm_channel.cpp: In member function 'f':\n"
           "/w/src/modelnet/realm_channel.cpp:887:5: error: 'ReplaceAll' was not declared in this scope\n")
    assert first_compile_error(gcc) == "realm_channel.cpp:887: 'ReplaceAll' was not declared in this scope"
    msvc = "FAILED: y.obj\nD:\\a\\btx\\src\\y.cpp(12,5): error C3861: 'ReplaceAll': identifier not found\n"
    assert first_compile_error(msvc) == "y.cpp:12: C3861: 'ReplaceAll': identifier not found"
    assert first_compile_error("[1/2] ok\nwarning: unused\n") == ""
    selftest_additions()
    Ann().emit("notice", f"job={a.job} step=tools status=ok python={sys.version.split()[0]} sha={os.environ.get('GITHUB_SHA', '?')[:12]} "
                         f"ref={os.environ.get('GITHUB_REF_NAME', '?')}")
    return 0


# ---------------------------------------------------------------- cost-balanced shards
def parse_shard(spec):
    k, sep, n = (spec or "").partition("/")
    if not sep or not k.isdigit() or not n.isdigit():
        raise ValueError(f"shard={spec} (want K/N)")
    k, n = int(k), int(n)
    if n < 1 or not 1 <= k <= n:
        raise ValueError(f"shard={spec} (want 1<=K<=N)")
    return k, n


def read_costs(path):
    """Read a costs file: one 'seconds<TAB>name' per line, '#' lines are comments. Names may hold spaces
    (functional variants such as 'wallet_basic.py --descriptors')."""
    costs = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            secs, sep, name = line.partition("\t")
            if not sep:
                continue
            try:
                costs[name.strip()] = max(0.0, float(secs))
            except ValueError:
                continue
    return costs


def cost_shards(names, n, costs):
    """Longest-processing-time split of names into n shards by measured seconds. Names without a cost get the
    median of the known costs. Deterministic: ties break on the name and the shard index. Returns
    (list of n name sets, list of n loads)."""
    known = sorted(costs[s] for s in names if s in costs)
    default = known[len(known) // 2] if known else 1.0
    load = [0.0] * n
    members = [set() for _ in range(n)]
    for s in sorted(names, key=lambda s: (-costs.get(s, default), s)):
        i = min(range(n), key=lambda j: (load[j], j))
        load[i] += costs.get(s, default)
        members[i].add(s)
    return members, load


def cost_shard_select(names, spec, costs):
    """Shard K/N of names, balanced by cost; the result keeps the longest suites first."""
    k, n = parse_shard(spec)
    members, load = cost_shards(names, n, costs)
    known = sorted(costs[s] for s in names if s in costs)
    default = known[len(known) // 2] if known else 1.0
    pick = members[k - 1]
    return sorted((s for s in names if s in pick), key=lambda s: (-costs.get(s, default), s)), load


# ---------------------------------------------------------------- known-failing gate
KNOWN_KINDS = ("unit", "ctest", "functional", "lint")
FAIL_STATUS = {"unit": ("fail", "crash", "timeout"), "ctest": ("fail", "timeout"), "functional": ("Failed",),
               "lint": ("fail", "timeout")}
UNMEASURED_STATUS = ("deadline", "not_run")


def read_known(path):
    """known-failing.txt: 'kind | name | class | first failing line | seen on', '#' lines are comments."""
    known = {}
    with open(path, "r", encoding="utf-8") as f:
        for no, line in enumerate(f, 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = [p.strip() for p in line.rstrip("\n").split(" | ")]
            if len(parts) != 5 or parts[0] not in KNOWN_KINDS or not parts[1]:
                raise ValueError(f"{path}:{no}: want 'kind | name | class | first failing line | seen on'")
            known[(parts[0], parts[1])] = parts
    return known


def read_junit(files):
    """ctest --output-junit files, later files win. Returns {name: (status, secs, first)}; status is
    pass, fail, timeout or skip."""
    import xml.etree.ElementTree as ET
    out = {}
    for fn in files:
        root = ET.parse(fn).getroot()
        for tc in root.iter("testcase"):
            name = tc.get("name") or ""
            st = tc.get("status") or ""
            fail = tc.find("failure")
            if st == "run":
                status = "pass"
            elif st == "notrun" or tc.find("skipped") is not None:
                status = "skip"
            elif fail is not None and (fail.get("message") or "").lower().startswith("timeout"):
                status = "timeout"
            else:
                status = "fail"
            try:
                secs = float(tc.get("time") or 0)
            except ValueError:
                secs = 0.0
            first = ""
            so = tc.find("system-out")
            if status in ("fail", "timeout") and so is not None and so.text:
                lines = [x for x in so.text.splitlines() if "error" in x.lower()] or so.text.splitlines()[-1:]
                first = lines[0][:240] if lines else ""
            out[name] = (status, secs, first)
    return out


def read_functional_csv(files):
    """test_runner.py --resultsfile CSVs (test,status,duration(seconds)); the ALL row is dropped."""
    import csv
    out = {}
    for fn in files:
        with open(fn, "r", encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                name = (row.get("test") or "").strip()
                if not name or name == "ALL":
                    continue
                try:
                    secs = float(row.get("duration(seconds)") or 0)
                except ValueError:
                    secs = 0.0
                out[name] = (row.get("status") or "", secs, "")
    return out


def read_lint_tsv(files):
    """lint results written by the lint job: 'name<TAB>pass|fail<TAB>seconds'."""
    out = {}
    for fn in files:
        with open(fn, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 2 and parts[0] and not parts[0].startswith("#"):
                    out[parts[0]] = (parts[1], float(parts[2]) if len(parts) > 2 and parts[2] else 0.0, "")
    return out


def globs(patterns):
    files = []
    for pat in patterns or []:
        for g in sorted(glob.glob(pat, recursive=True)):
            if g not in files:
                files.append(g)
    return files


def gate_results(a):
    """All results by kind: {kind: {name: (status, secs, first)}}."""
    res = {k: {} for k in KNOWN_KINDS}
    ufiles = globs(a.unit)
    if ufiles:
        _, rows, _ = merge_rows(ufiles)
        for r in merge_final(rows):
            try:
                secs = float(r.get("secs") or 0)
            except ValueError:
                secs = 0.0
            res["unit"][r["suite"]] = (r["status"], secs, r.get("first", ""))
    res["ctest"] = read_junit(globs(a.junit))
    res["functional"] = read_functional_csv(globs(a.functional))
    res["lint"] = read_lint_tsv(globs(a.lint))
    return res


def known_gate(res, known, sha):
    """Compare results with the known-failing list. Returns (new, still, fixed, unmeasured, next_rows)."""
    new, still, fixed, unmeasured, next_rows = [], [], [], [], []
    for kind in KNOWN_KINDS:
        for name, (status, secs, first) in sorted(res[kind].items()):
            entry = known.get((kind, name))
            if status in FAIL_STATUS[kind]:
                if entry:
                    still.append((kind, name, status))
                    next_rows.append(entry)
                else:
                    new.append((kind, name, status, first))
                    next_rows.append([kind, name, "new", (first or status).replace("|", "/")[:200], sha])
            elif status in UNMEASURED_STATUS:
                unmeasured.append((kind, name, status))
                if entry:
                    next_rows.append(entry)
            elif entry:
                fixed.append((kind, name, status))
    return new, still, fixed, unmeasured, next_rows


def cmd_known(a):
    ann = Ann(notices=a.max_notices, errors=a.max_errors)
    try:
        known = read_known(a.known)
    except (OSError, ValueError) as e:
        ann.emit("error", f"label={a.label} status=bad-known-list {e}")
        return 1
    res = gate_results(a)
    total = sum(len(v) for v in res.values())
    if total == 0:
        ann.emit("error", f"label={a.label} status=no-results")
        return 1
    sha = os.environ.get("GITHUB_SHA", "local")[:8]
    new, still, fixed, unmeasured, next_rows = known_gate(res, known, sha)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "known-report.tsv"), "w", encoding="utf-8") as f:
        f.write("verdict\tkind\tname\tstatus\n")
        for kind, name, status, _ in new:
            f.write(f"new\t{kind}\t{name}\t{status}\n")
        for kind, name, status in still:
            f.write(f"known\t{kind}\t{name}\t{status}\n")
        for kind, name, status in fixed:
            f.write(f"fixed\t{kind}\t{name}\t{status}\n")
        for kind, name, status in unmeasured:
            f.write(f"unmeasured\t{kind}\t{name}\t{status}\n")
    with open(os.path.join(a.out, "known-failing.next.txt"), "w", encoding="utf-8") as f:
        f.write("# kind | name | class | first failing line | seen on\n")
        for row in sorted(next_rows, key=lambda r: (KNOWN_KINDS.index(r[0]), r[1])):
            f.write(" | ".join(row) + "\n")
    counts = " ".join(f"{k}={len(res[k])}" for k in KNOWN_KINDS)
    line = (f"label={a.label} results={total} ({counts}) new_failures={len(new)} known_failures={len(still)} "
            f"known_now_passing={len(fixed)} unmeasured={len(unmeasured)}")
    bad = bool(new) or (bool(unmeasured) and not a.allow_unmeasured)
    ann.emit("error" if bad else "notice", line)
    for kind, name, status, first in new[: max(0, a.max_errors - 3)]:
        ann.emit("error", f"label={a.label} NEW {kind} {name} status={status} first={first}")
    if len(new) > max(0, a.max_errors - 3):
        ann.emit("error", f"label={a.label} more_new={len(new) - max(0, a.max_errors - 3)} (known-report.tsv)")
    if unmeasured:
        ann.emit("error" if not a.allow_unmeasured else "notice",
                 f"label={a.label} unmeasured={len(unmeasured)} (budget cut: deadline/not_run) first="
                 + ",".join(n for _, n, _ in unmeasured[:20]))
    if fixed:
        ann.emit("notice", f"label={a.label} known_now_passing={len(fixed)}: shrink known-failing.txt: "
                           + ",".join(n for _, n, _ in fixed[:30]))
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(f"### {a.label}\n\n| | count |\n|---|---:|\n")
            for k in KNOWN_KINDS:
                f.write(f"| {k} results | {len(res[k])} |\n")
            f.write(f"| new failures | {len(new)} |\n| known failures | {len(still)} |\n"
                    f"| known, now passing | {len(fixed)} |\n| unmeasured (budget) | {len(unmeasured)} |\n\n")
            if new:
                f.write("New failures:\n\n" + "".join(f"- `{k}` `{n}`: {s}\n" for k, n, s, _ in new[:200]) + "\n")
    return 1 if bad else 0


# ---------------------------------------------------------------- functional shards
def functional_lists(runner_path):
    """(default list, ALL_SCRIPTS) of test_runner.py. Loading it runs only its definitions: main() sits behind
    __name__ == '__main__', and the lists are not plain literals (TEST_FRAMEWORK_UNIT_TESTS is a name)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("btx_ci_functional_test_runner", runner_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return list(mod.BTX_BASE_SCRIPTS), list(dict.fromkeys(mod.ALL_SCRIPTS))


def functional_args(shard_names, all_scripts):
    """(names to pass, exact names to exclude) so that test_runner.py runs exactly shard_names, once each.
    test_runner.py selects by prefix ('x.py' also picks 'x.py --flag') and does not drop duplicates, so a name
    another passed name already picks is not passed again, and the variants a prefix adds beyond the shard are
    excluded by exact name (a name with a space is excluded exactly)."""
    def stem(t):
        return t if ".py" in t else t + ".py"

    def expand(names):
        out = []
        for t in names:
            out += [s for s in all_scripts if s.startswith(stem(t))]
        return out

    want = list(dict.fromkeys(shard_names))
    args = [t for t in want if not any(u != t and t.startswith(stem(u)) for u in want)]
    picked = expand(args)
    extra = sorted(set(picked) - set(want))
    bad = [e for e in extra if " " not in e]
    missing = sorted(set(want) - set(picked))
    if bad or missing:
        raise ValueError("cannot select exactly: exclude=" + ",".join(bad) + " missing=" + ",".join(missing))
    return args, extra


def cmd_functional(a):
    ann = Ann(notices=a.max_notices, errors=a.max_errors)
    runner = os.path.join(a.builddir, "test", "functional", "test_runner.py")
    try:
        default, all_scripts = functional_lists(runner)
    except (OSError, AttributeError, ImportError, SyntaxError) as e:
        ann.emit("error", f"label={a.label} status=no-test-list {e}")
        return 1
    sel = default
    if a.exclude:
        exc = re.compile(a.exclude)
        sel = [s for s in sel if not exc.search(s)]
    loads = []
    if a.shard:
        costs = read_costs(a.costs) if a.costs else {}
        try:
            sel, loads = cost_shard_select(sel, a.shard, costs)
        except ValueError as e:
            ann.emit("error", f"label={a.label} status=bad-shard {e}")
            return 1
    try:
        args, extra = functional_args(sel, all_scripts)
    except ValueError as e:
        ann.emit("error", f"label={a.label} status=bad-selection {e}")
        return 1
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "selected.txt"), "w", encoding="utf-8") as f:
        f.write("".join(x + "\n" for x in sel))
    if a.list_only:
        for s in sel:
            print(s)
        ann.emit("notice", f"label={a.label} shard={a.shard or 'all'} scripts={len(sel)} of={len(default)} "
                           f"loads={','.join(str(int(x)) for x in loads)}")
        return 0
    results = os.path.join(a.out, "functional-results.csv")
    tmp = os.path.abspath(a.tmpdirprefix)
    os.makedirs(tmp, exist_ok=True)
    cmd = [sys.executable, runner, f"--configfile={os.path.join(a.builddir, 'test', 'config.ini')}",
           f"--jobs={a.jobs}", f"--tmpdirprefix={tmp}", f"--resultsfile={results}", "--combinedlogslen=0"]
    if extra:
        cmd.append("--exclude=" + ",".join(extra))
    cmd += strip_dashdash(a.extra or []) + args
    t0 = time.time()
    with open(os.path.join(a.out, "functional.log"), "w", encoding="utf-8") as f:
        f.write("$ " + " ".join(cmd) + "\n")
        f.flush()
        try:
            rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=a.stop_in * 60 if a.stop_in else None)
        except subprocess.TimeoutExpired:
            rc = "budget"
    secs = int(time.time() - t0)
    got = read_functional_csv([results]) if os.path.exists(results) else {}
    count = {}
    for st, _, _ in got.values():
        count[st] = count.get(st, 0) + 1
    missing = [s for s in sel if s not in got]
    stall = []
    for s, (st, _, _) in got.items():
        if st == "Failed":
            base = s.split(" ")[0].replace(".py", "")
            hits = glob.glob(os.path.join(tmp, "test_runner_*", f"{base}_*", "node*", "regtest", "debug.log"))
            if any("ExactReplay required" in read(h) for h in hits):
                stall.append(s)
    with open(os.path.join(a.out, "stall.txt"), "w", encoding="utf-8") as f:
        f.write("".join(x + "\n" for x in sorted(stall)))
    tail(os.path.join(a.out, "functional.log"), 40)
    ann.emit("notice" if not count.get("Failed") and not missing else "error",
             f"label={a.label} shard={a.shard or 'all'} scripts={len(sel)} passed={count.get('Passed', 0)} "
             f"failed={count.get('Failed', 0)} skipped={count.get('Skipped', 0)} missing={len(missing)} "
             f"exactreplay_stall={len(stall)} rc={rc} secs={secs} jobs={a.jobs}")
    failed = sorted(s for s, (st, _, _) in got.items() if st == "Failed")
    if failed:
        ann.emit("error", f"label={a.label} failed={len(failed)}: " + ",".join(failed[:60]))
    # The verdict is the known-failing gate's; this step fails only when the runner produced no results.
    return 0 if got else 1


# ---------------------------------------------------------------- cppcheck (product code)
VENDORED = ("leveldb/", "secp256k1/", "crc32c/", "univalue/", "minisketch/", "libbitcoinpqc/", "crypto/ctaes/")
CPPCHECK_DEFS = ["-D__GNUC__=13", "-D__GNUC_MINOR__=3", "-D__GNUC_PATCHLEVEL__=0", "-D__linux__=1", "-D__linux=1",
                 "-D__unix__=1", "-D__x86_64__=1", "-D__amd64__=1", "-D__SIZEOF_INT128__=16", "-D__BYTE_ORDER__=1234",
                 "-D__ORDER_LITTLE_ENDIAN__=1234", "-D__ORDER_BIG_ENDIAN__=4321", "-D__CHAR_BIT__=8"]
CPPCHECK_ARGS = ["--library=gnu", "--library=boost", "--enable=warning,performance,portability", "--inconclusive",
                 "--suppress=missingIncludeSystem"]


def is_test_path(rel):
    return (rel.startswith(("test/", "bench/", "fuzz/", "qt/test/")) or "/test/" in rel or "/fuzz/" in rel
            or rel.startswith(VENDORED))


def src_rel(path, src_root):
    p = os.path.normpath(path).replace("\\", "/")
    root = os.path.normpath(os.path.join(src_root, "src")).replace("\\", "/") + "/"
    return p[len(root):] if p.startswith(root) else None


def cppcheck_units(build, src_root, only=None):
    with open(os.path.join(build, "compile_commands.json"), "r", encoding="utf-8") as f:
        db = json.load(f)
    seen, keep = set(), []
    only_re = re.compile(only) if only else None
    for e in db:
        f = e["file"] if os.path.isabs(e["file"]) else os.path.join(e["directory"], e["file"])
        rel = src_rel(f, src_root)
        if rel is None or is_test_path(rel) or rel in seen:
            continue
        if only_re and not only_re.search("src/" + rel):
            continue
        seen.add(rel)
        keep.append(dict(e, file=os.path.normpath(f)))
    return keep


def cppcheck_results(xml_files, src_root):
    """Unique cppcheck results in product code: [(id, severity, rel_file, line, msg)]."""
    import xml.etree.ElementTree as ET
    seen, out = set(), []
    for fn in xml_files:
        try:
            root = ET.parse(fn).getroot()
        except ET.ParseError:
            continue
        for e in root.iter("error"):
            sev = e.get("severity") or ""
            if sev == "information":
                continue
            loc = e.find("location")
            path = loc.get("file") if loc is not None else ""
            rel = src_rel(path if os.path.isabs(path) else os.path.join(src_root, path), src_root) if path else None
            if rel is None or is_test_path(rel):
                continue
            k = (e.get("id") or "", sev, rel, loc.get("line") if loc is not None else "", e.get("msg") or "")
            if k not in seen:
                seen.add(k)
                out.append(k)
    return out


def read_baseline(path):
    base = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            n, rid, rel = line.rstrip("\n").split("\t")
            base[(rid, rel)] = int(n)
    return base


def cppcheck_compare(results, base, scope_re=None):
    """Per (id, file) counts against the baseline. Returns (grown, shrunk, counts)."""
    counts = {}
    for rid, _sev, rel, _line, _msg in results:
        counts[(rid, rel)] = counts.get((rid, rel), 0) + 1
    grown = sorted((k, v, base.get(k, 0)) for k, v in counts.items() if v > base.get(k, 0))
    keys = [k for k in base if not scope_re or scope_re.search("src/" + k[1])]
    shrunk = sorted((k, counts.get(k, 0), base[k]) for k in keys if counts.get(k, 0) < base[k])
    return grown, shrunk, counts


def cmd_cppcheck(a):
    ann = Ann(notices=a.max_notices, errors=a.max_errors)
    os.makedirs(os.path.join(a.out, "tu"), exist_ok=True)
    src_root = os.path.abspath(a.src)
    units = cppcheck_units(a.build, src_root, a.only)
    if not units:
        ann.emit("error", f"label={a.label} status=no-units only={a.only or ''}")
        return 1
    ver = subprocess.run(["cppcheck", "--version"], capture_output=True, text=True, encoding="utf-8").stdout.strip()

    def one(i_e):
        i, e = i_e
        db = os.path.join(a.out, "tu", f"{i:05d}.json")
        with open(db, "w", encoding="utf-8") as f:
            json.dump([e], f)
        xml = os.path.join(a.out, "tu", f"{i:05d}.xml")
        t0 = time.time()
        try:
            rc = subprocess.call(["cppcheck", f"--project={db}"] + CPPCHECK_DEFS + CPPCHECK_ARGS
                                 + ["--xml", "--xml-version=2", f"--output-file={xml}", "--quiet"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=a.tu_timeout)
        except subprocess.TimeoutExpired:
            rc = "timeout"
        return src_rel(e["file"], src_root), rc, round(time.time() - t0, 1), xml

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        runs = list(ex.map(one, enumerate(units)))
    secs = int(time.time() - t0)
    with open(os.path.join(a.out, "tu-times.tsv"), "w", encoding="utf-8") as f:
        f.write("".join(f"{rel}\t{rc}\t{s}\n" for rel, rc, s, _ in runs))
    bad_tu = [rel for rel, rc, _, _ in runs if rc != 0]
    results = cppcheck_results([x for _, _, _, x in runs], src_root)
    with open(os.path.join(a.out, "cppcheck-results.tsv"), "w", encoding="utf-8") as f:
        f.write("id\tseverity\tfile\tline\tmsg\n")
        for r in sorted(results, key=lambda r: (r[2], int(r[3] or 0), r[0])):
            f.write("\t".join(str(x).replace("\t", " ") for x in r) + "\n")
    by_sev = {}
    for r in results:
        by_sev[r[1]] = by_sev.get(r[1], 0) + 1
    scope_re = re.compile(a.only) if a.only else None
    counts_line = (f"label={a.label} {ver.replace(' ', '-')} units={len(units)} unit_failures={len(bad_tu)} "
                   f"results={len(results)} " + " ".join(f"{k}={v}" for k, v in sorted(by_sev.items())) + f" secs={secs} jobs={a.jobs}")
    if a.write_baseline:
        counts = {}
        for rid, _s, rel, _l, _m in results:
            counts[(rid, rel)] = counts.get((rid, rel), 0) + 1
        with open(a.write_baseline, "w", encoding="utf-8") as f:
            f.write(f"# cppcheck baseline: count<TAB>id<TAB>file (src/ relative). {ver}; product code; "
                    f"{len(results)} results in {len(counts)} (id, file) pairs; commit {os.environ.get('GITHUB_SHA', a.commit or '?')[:12]}\n")
            for (rid, rel), n in sorted(counts.items(), key=lambda kv: (kv[0][1], kv[0][0])):
                f.write(f"{n}\t{rid}\t{rel}\n")
    if not a.baseline:
        ann.emit("error" if bad_tu else "notice", counts_line)
        return 1 if bad_tu else 0
    base = read_baseline(a.baseline)
    grown, shrunk, _ = cppcheck_compare(results, base, scope_re)
    with open(os.path.join(a.out, "cppcheck-new.tsv"), "w", encoding="utf-8") as f:
        f.write("id\tfile\tnow\tbaseline\n")
        for (rid, rel), now, was in grown:
            f.write(f"{rid}\t{rel}\t{now}\t{was}\n")
    bad = bool(grown) or bool(bad_tu)
    ann.emit("error" if bad else "notice", counts_line + f" grown_pairs={len(grown)} shrunk_pairs={len(shrunk)} "
             f"baseline_results={sum(base.values())}")
    for (rid, rel), now, was in grown[: max(0, a.max_errors - 3)]:
        lines = [r for r in results if r[0] == rid and r[2] == rel]
        ann.emit("error", f"label={a.label} NEW cppcheck {rid} src/{rel} now={now} baseline={was} "
                          f"first=line {lines[0][3]}: {lines[0][4]}")
    if bad_tu:
        ann.emit("error", f"label={a.label} unit_failures={len(bad_tu)} first=" + ",".join(bad_tu[:10]))
    if shrunk:
        ann.emit("notice", f"label={a.label} baseline can shrink: {len(shrunk)} (id, file) pairs went down; "
                           "commit the --write-baseline output")
    return 1 if bad else 0


# ---------------------------------------------------------------- secret scan of added lines
SECRET_PATTERNS = [
    ("private-key-block", re.compile(r"-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY-----")),
    ("github-token", re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{60,})\b")),
    ("aws-access-key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("slack-token", re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}\b")),
    ("assigned-api-key", re.compile(r"(?i)\b(?:api[_-]?key|bearer|token)\b\s*[:=]\s*['\"][A-Za-z0-9_\-]{32,}['\"]")),
]
# Test trees hold throwaway PEM keys by design; tokens are flagged everywhere.
SECRET_KEY_EXEMPT = re.compile(r"^(?:src/test/|src/wallet/test/|test/|src/[a-z_]+/test/)")


def scan_added_lines(diff_text):
    hits, path, line_no = [], None, 0
    for raw in diff_text.splitlines():
        if raw.startswith("+++ "):
            path = raw[6:] if raw.startswith("+++ b/") else None
            continue
        if raw.startswith("@@"):
            m = re.search(r"\+(\d+)", raw)
            line_no = int(m.group(1)) if m else 0
            continue
        if path is None:
            continue
        if raw.startswith("+"):
            for name, rx in SECRET_PATTERNS:
                if rx.search(raw[1:]) and not (name == "private-key-block" and SECRET_KEY_EXEMPT.match(path)):
                    hits.append((path, line_no, name))
            line_no += 1
        elif not raw.startswith("-"):
            line_no += 1
    return hits


def cmd_secrets(a):
    ann = Ann()
    diff = subprocess.run(["git", "diff", "--no-color", "--no-ext-diff", "-U0", a.base, "HEAD", "--"],
                          capture_output=True, text=True, encoding="utf-8", errors="replace")
    if diff.returncode != 0:
        ann.emit("error", f"label={a.label} status=git-diff-failed base={a.base}")
        return 1
    hits = scan_added_lines(diff.stdout)
    for path, line, name in hits[:8]:
        ann.emit("error", f"label={a.label} possible-secret kind={name} at {path}:{line} (value not printed)")
    ann.emit("error" if hits else "notice", f"label={a.label} added_line_secret_hits={len(hits)} base={a.base[:12]}")
    return 1 if hits else 0



# ---------------------------------------------------------------- lints that already fail on the base
RE_ANSI = re.compile(r"\x1b\[[0-9;]*m")
RE_LINENO = re.compile(r":\d+(?::\d+)?(?=[:\s]|$)")
RE_ONELINE = re.compile(r"^[0-9a-f]{7,40} ")
RE_PYSET = re.compile(r"\{('[^{}]*')\}")


def sort_pyset(m):
    """A printed Python set has no stable order across runs ({'-a', '-b'} vs {'-b', '-a'}); sort it."""
    return "{" + ", ".join(sorted(x.strip() for x in m.group(1).split(","))) + "}"


def lint_norm(text, roots):
    """Output lines of a lint, comparable across commits: no colours, no worktree paths, no line numbers,
    no commit list (the runner prints the commits of COMMIT_RANGE first)."""
    out = []
    for line in RE_ANSI.sub("", text).splitlines():
        for r in roots:
            if r:
                line = line.replace(r.rstrip("/") + "/", "")
        if not line.strip() or line.startswith("Checking commit range") or RE_ONELINE.match(line):
            continue
        out.append(RE_PYSET.sub(sort_pyset, RE_LINENO.sub(":N", line.rstrip())))
    return out


def run_lint(name, runner, cwd, commit_range):
    env = dict(os.environ, COMMIT_RANGE=commit_range, RUST_BACKTRACE="1")
    cmd = [sys.executable, os.path.join("test", "lint", name)] if name.endswith(".py") else [runner, f"--lint={name}"]
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return p.returncode, p.stdout + p.stderr


def cmd_lint_diff(a):
    """A lint that fails on the release base too is known-failing as a whole; this keeps it useful: the lint
    runs again on the base, and every output line the branch adds (line numbers ignored) is a new failure,
    written as an extra result row '<lint>:new-hits'."""
    ann = Ann()
    known = read_known(a.known)
    res = read_lint_tsv([a.results])
    todo = [n for n, (st, _, _) in sorted(res.items()) if st == "fail" and ("lint", n) in known and ":" not in n]
    if not todo:
        ann.emit("notice", f"label={a.label} known_failing_lints_rerun_on_base=0")
        return 0
    wt = os.path.abspath(os.path.join(a.out, "base-tree"))
    os.makedirs(a.out, exist_ok=True)
    subprocess.run(["git", "worktree", "remove", "--force", wt], capture_output=True)
    if subprocess.run(["git", "worktree", "add", "--detach", wt, a.base], capture_output=True).returncode != 0:
        ann.emit("error", f"label={a.label} status=no-base-worktree base={a.base[:12]}")
        return 1
    here = os.path.abspath(".")
    rows, new_total = [], 0
    try:
        for name in todo:
            with open(os.path.join(a.logs, name + ".log"), "r", encoding="utf-8", errors="replace") as f:
                branch_out = f.read()
            rc, base_out = run_lint(name, os.path.abspath(a.runner), wt, f"{a.base}..{a.base}")
            with open(os.path.join(a.out, name + ".base.log"), "w", encoding="utf-8") as f:
                f.write(base_out)
            extra = Counter(lint_norm(branch_out, [here, wt])) - Counter(lint_norm(base_out, [here, wt]))
            lines = sorted(extra.elements())
            with open(os.path.join(a.out, name + ".new.txt"), "w", encoding="utf-8") as f:
                f.write("".join(x + "\n" for x in lines))
            if lines:
                new_total += len(lines)
                rows.append(f"{name}:new-hits\tfail\t0\n")
                ann.emit("error", f"label={a.label} lint={name} base_rc={rc} new_lines={len(lines)} first={lines[0][:300]}")
            else:
                ann.emit("notice", f"label={a.label} lint={name} base_rc={rc} new_lines=0 (fails on the base the same way)")
    finally:
        subprocess.run(["git", "worktree", "remove", "--force", wt], capture_output=True)
    with open(a.results, "a", encoding="utf-8") as f:
        f.writelines(rows)
    ann.emit("error" if new_total else "notice",
             f"label={a.label} known_failing_lints_rerun_on_base={len(todo)} with_new_lines={len(rows)} new_lines={new_total}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("ann")
    p.add_argument("level", choices=["notice", "error", "warning"])
    p.add_argument("msg", nargs="+")
    p = sp.add_parser("configure")
    p.add_argument("--name", required=True)
    p.add_argument("--job", required=True)
    p.add_argument("--logs", required=True)
    p.add_argument("--builddir", required=True)
    p.add_argument("cmake", nargs=argparse.REMAINDER)
    p = sp.add_parser("build")
    p.add_argument("--name", required=True)
    p.add_argument("--logs", required=True)
    p.add_argument("--builddir", required=True)
    p.add_argument("ninja", nargs=argparse.REMAINDER)
    p = sp.add_parser("build-summary")
    p.add_argument("--name", required=True)
    p.add_argument("--job", required=True)
    p.add_argument("--logs", required=True)
    p.add_argument("--exe", nargs="*", help="key=path; writes key=yes|no to GITHUB_OUTPUT")
    p = sp.add_parser("suites")
    p.add_argument("--bin", required=True)
    p.add_argument("--label", required=True)
    p.add_argument("--job", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--suites", help="comma list (exact names)")
    p.add_argument("--include", default=".")
    p.add_argument("--exclude")
    p.add_argument("--serial", help="regex of suites run alone after the others")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    p.add_argument("--timeout", type=int, default=900)
    p.add_argument("--stop-at", type=int, default=0, help="unix time; later suites are not started")
    p.add_argument("--stop-in", type=int, default=0, help="minutes from now; combined with --stop-at, the earlier wins")
    p.add_argument("--per-suite", action="store_true")
    p.add_argument("--keep-logs", action="store_true")
    p.add_argument("--max-notices", type=int, default=9)
    p.add_argument("--max-errors", type=int, default=9)
    p.add_argument("--suites-file", help="file with one suite per line; intersected with the selection")
    p.add_argument("--shard", help="K/N: run every N-th suite of the sorted selection, from the K-th")
    p.add_argument("--shard-costs", help="seconds<TAB>suite file: shard K/N balanced by cost, longest first")
    p.add_argument("extra", nargs=argparse.REMAINDER)
    p = sp.add_parser("merge")
    p.add_argument("--label", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--inputs", nargs="+", required=True, help="results.tsv globs, in pass order (later wins)")
    p.add_argument("--listed", nargs="*", help="listed.txt globs, to report suites no run accounted for")
    p.add_argument("--max-notices", type=int, default=9)
    p.add_argument("--max-errors", type=int, default=9)
    p = sp.add_parser("prune")
    p.add_argument("--log", required=True)
    p.add_argument("--src", required=True)
    p.add_argument("--target", default="test_btx")
    p.add_argument("--logs", required=True)
    p.add_argument("--job", required=True)
    p = sp.add_parser("test-defs")
    p.add_argument("--src", required=True)
    p.add_argument("--target", default="test_btx")
    p.add_argument("--job", required=True)
    p = sp.add_parser("relink")
    p.add_argument("--name", required=True)
    p.add_argument("--job", required=True)
    p.add_argument("--logs", required=True)
    p.add_argument("--builddir", required=True)
    p.add_argument("--src", required=True)
    p.add_argument("--target", default="test_btx")
    p.add_argument("--rounds", type=int, default=5)
    p.add_argument("ninja", nargs=argparse.REMAINDER)
    p = sp.add_parser("dlls")
    p.add_argument("--objdump", required=True)
    p.add_argument("--dest", required=True)
    p.add_argument("--job", required=True)
    p.add_argument("--search", nargs="+", required=True)
    p.add_argument("--exe", nargs="+", required=True)
    p = sp.add_parser("smoke")
    p.add_argument("--btxd", required=True)
    p.add_argument("--cli", required=True)
    p.add_argument("--datadir", required=True)
    p.add_argument("--label", required=True)
    p.add_argument("--job", required=True)
    p.add_argument("--port", type=int, default=38333)
    p.add_argument("--rpcport", type=int, default=38332)
    p.add_argument("--wallet", action="store_true")
    p.add_argument("--mine", type=int, default=0)
    p.add_argument("--mine-budget", type=int, default=900)
    p.add_argument("--send", action="store_true")
    p.add_argument("--linger", type=int, default=0)
    p.add_argument("--extra", action="append")
    p.add_argument("--capture", action="append", help="RPC method to call after start and save, e.g. getmodelnetworkinfo")
    p.add_argument("--out")
    p = sp.add_parser("msvc-run")
    p.add_argument("--job", default="local")
    p.add_argument("--path-prepend", action="append", help="directory put first in PATH")
    p.add_argument("--path-drop", action="append", help="drop PATH entries that contain this text (case-insensitive)")
    p.add_argument("argv", nargs=argparse.REMAINDER, help="the command to run (the subcommand name is in a.cmd)")
    p = sp.add_parser("userns")
    p.add_argument("--job", required=True)
    p = sp.add_parser("selftest")
    p.add_argument("--job", default="tools")
    p = sp.add_parser("known")
    p.add_argument("--label", required=True)
    p.add_argument("--known", required=True, help="known-failing.txt")
    p.add_argument("--out", required=True)
    p.add_argument("--unit", nargs="*", help="results.tsv globs of the suites runner (later passes win)")
    p.add_argument("--junit", nargs="*", help="ctest --output-junit globs")
    p.add_argument("--functional", nargs="*", help="test_runner.py --resultsfile CSV globs")
    p.add_argument("--lint", nargs="*", help="lint TSV globs (name, pass|fail, seconds)")
    p.add_argument("--allow-unmeasured", action="store_true", help="budget-cut suites are a notice, not an error")
    p.add_argument("--max-notices", type=int, default=9)
    p.add_argument("--max-errors", type=int, default=9)
    p = sp.add_parser("functional")
    p.add_argument("--builddir", required=True)
    p.add_argument("--label", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--shard", help="K/N of the default list, balanced by --costs")
    p.add_argument("--costs", help="seconds<TAB>script file")
    p.add_argument("--exclude", help="regex of scripts left out (none by default)")
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--tmpdirprefix", default="func-tmp")
    p.add_argument("--stop-in", type=int, default=0, help="minutes; the runner is stopped after that")
    p.add_argument("--list-only", action="store_true")
    p.add_argument("--max-notices", type=int, default=9)
    p.add_argument("--max-errors", type=int, default=9)
    p.add_argument("extra", nargs=argparse.REMAINDER, help="-- then extra test_runner.py options")
    p = sp.add_parser("cppcheck")
    p.add_argument("--src", required=True, help="repository root")
    p.add_argument("--build", required=True, help="build dir with compile_commands.json and generated headers")
    p.add_argument("--out", required=True)
    p.add_argument("--label", default="cppcheck")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    p.add_argument("--only", help="regex on 'src/<path>': scan only these units and compare only these files")
    p.add_argument("--tu-timeout", type=int, default=1200)
    p.add_argument("--baseline", help="count<TAB>id<TAB>file baseline; red when a pair grows")
    p.add_argument("--write-baseline", help="write this run's counts in the baseline format")
    p.add_argument("--commit", help="commit named in a written baseline (default: GITHUB_SHA)")
    p.add_argument("--max-notices", type=int, default=9)
    p.add_argument("--max-errors", type=int, default=9)
    p = sp.add_parser("lint-diff")
    p.add_argument("--runner", required=True, help="the built test/lint/test_runner")
    p.add_argument("--results", required=True, help="lint-results.tsv of the branch (rows are appended)")
    p.add_argument("--logs", required=True, help="directory with <lint>.log of the branch run")
    p.add_argument("--base", required=True)
    p.add_argument("--known", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--label", default="lint-diff")
    p = sp.add_parser("secrets")
    p.add_argument("--base", required=True, help="the diff base (merge-base with the release line)")
    p.add_argument("--label", default="secrets")
    a = ap.parse_args()
    fn = {"ann": cmd_ann, "configure": cmd_configure, "build": cmd_build, "build-summary": cmd_build_summary,
          "suites": cmd_suites, "merge": cmd_merge, "msvc-run": cmd_msvc_run, "prune": cmd_prune, "relink": cmd_relink, "test-defs": cmd_test_defs, "dlls": cmd_dlls, "smoke": cmd_smoke, "userns": cmd_userns,
          "selftest": cmd_selftest, "known": cmd_known, "functional": cmd_functional, "cppcheck": cmd_cppcheck,
          "secrets": cmd_secrets, "lint-diff": cmd_lint_diff}[a.cmd]
    return fn(a)


if __name__ == "__main__":
    sys.exit(main())
