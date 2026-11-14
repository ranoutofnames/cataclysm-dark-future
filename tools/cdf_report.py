#!/usr/bin/env python3
"""CDF one-command check report (PL-62 AT0).

Runs every check family that is installed in this tree and prints one page
(the format is PLAN.md section 3):

    CDF check report  <date time>  tree <path>  commit <sha12>  build <number>
    RESULT: PASS | FAIL (n sections failed)
    section            result  counts                       detail file
    ...

Each family is independent; a family that is not installed is SKIP (not
installed), and one that needs a build is SKIP (no build) in dry mode.

Usage:
  py -3 -I tools/cdf_report.py [--src DIR] [--out report] [--only fam,fam]
                               [--quick] [--skip-build] [--dda DIR]
  py -3 -I tools/cdf_report.py --selftest

--src        checkout to check (default: the repository this file lives in).
--out        report directory (default: report under --src).
--only       run only these section names, comma separated.
--quick      dry mode: do not build, do not run anything that needs the
             build; the build sections print SKIP (no build). Python families
             still run.
--skip-build do not build, but run the build sections if their binaries exist.
--dda        DDA checkout for data-audit (default: %CDF_DDA_SRC%, then
             C:\\CDF\\CDDA SRC).
--selftest   run the fake-section proof of PASS, FAIL, SKIP, allowlist and
             new-finding handling; exit 0 on success.

Exit code is 0 only when every section passes (SKIP is neutral) and there are
no new findings. Details go to sibling files in the report directory; the page
never carries them.

Only the standard library is used. The build, check-mods, junit and shard
behaviour is copied from the tools outside the repository, not imported:
C:\\Claude\\Workspace 1\\.claude\\skills\\cdf-test\\cdf_test.py and
C:\\CDF\\staging\\shardrun.py (origins also in docs/en/dev/check_report.md).
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DEFAULT_SRC = os.path.dirname(HERE)
DEFAULT_DDA = os.environ.get("CDF_DDA_SRC", r"C:\CDF\CDDA SRC")
DEFAULT_BUILD_BAT = os.environ.get("CDF_BUILD_BAT", r"C:\CDF\tools\cdf_build.bat")
DEFAULT_BASELINE = os.environ.get(
    "CDF_TEST_BASELINE", r"C:\CDF\orchestration\test-baseline.json")
TEST_EXE = os.path.join("out", "build", "win-tiles", "tests",
                        "cata_test-tiles.exe")
GAME_EXE = os.path.join("out", "build", "win-tiles", "src",
                        "cataclysm-bn-tiles.exe")
ALLOW_PATH = os.path.join("tests", "data", "audit", "allow.tsv")
SHARD_TIMEOUT = 1500  # seconds per shard, as shardrun.py

# Copied from cdf_test.py:108-116 (the binary can exit 0 after a data error).
CHECK_MODS_ERROR_RE = re.compile(
    r"\bERROR\b|\bError\b|[Ee]rror:|json-error|\bException\b|terminate called"
    r"|Failed to generate test world")


class Context:
    def __init__(self, src, out, dda, quick, skip_build, shard_size):
        self.src = os.path.abspath(src)
        self.out = out if os.path.isabs(out) else os.path.join(self.src, out)
        self.dda = dda
        self.quick = quick
        self.skip_build = skip_build
        self.shard_size = shard_size
        self.build = "dry" if quick else "pending"
        self.build_log = None
        self.commit, self.build_number = describe_tree(self.src)
        os.makedirs(self.out, exist_ok=True)

    @property
    def test_exe(self):
        return os.path.join(self.src, TEST_EXE)

    @property
    def game_exe(self):
        return os.path.join(self.src, GAME_EXE)

    @property
    def allow_path(self):
        return os.path.join(self.src, ALLOW_PATH)


class Finding:
    def __init__(self, check, item, reason):
        self.check = check
        self.item = item
        self.reason = reason


class Result:
    def __init__(self, name, result, counts="", detail="", findings=None):
        self.name = name
        self.result = result
        self.counts = counts
        self.detail = detail
        self.findings = findings or []


def describe_tree(src):
    """(commit sha12, build number) from git and VERSION.txt, or unknown."""
    commit = ""
    try:
        p = subprocess.run(["git", "-C", src, "rev-parse", "--short=12", "HEAD"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                           universal_newlines=True, timeout=30)
        if p.returncode == 0:
            commit = p.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    build = ""
    version = os.path.join(src, "VERSION.txt")
    if os.path.isfile(version):
        with open(version, encoding="utf-8", errors="replace") as f:
            for line in f:
                key, _sep, value = line.partition(":")
                if key.strip().lower() == "build number":
                    build = value.strip()
                elif key.strip().lower() == "commit sha" and not commit:
                    commit = value.strip()[:12]
    return commit or "unknown", build or "unknown"


def skip(name, reason):
    return Result(name, "SKIP", reason)


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def run_capture(cmd, cwd, timeout):
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True,
                       errors="replace", timeout=timeout)
    return p.returncode, p.stdout


# --------------------------------------------------------------- allowlist

def audit_result(rows, allowed_count=0):
    """Turn allow-filtered cdf_audit rows into (result, counts, findings)."""
    findings = [Finding(row[0], row[2], row[3]) for row in rows]
    total = len(rows) + allowed_count
    if allowed_count:
        counts = "%d findings (%d new, %d allowed)" % (
            total, len(rows), allowed_count)
    else:
        counts = "%d findings (%d new)" % (total, len(rows))
    return ("FAIL" if rows else "PASS"), counts, findings


# --------------------------------------------------------------- sections

def section_data_audit(ctx):
    tool = os.path.join(ctx.src, "tools", "cdf_audit.py")
    if not os.path.isfile(tool):
        return skip("data-audit", "not installed")
    try:
        import cdf_audit
    except Exception as exc:  # a broken tool must be visible, not a crash
        detail = os.path.join(ctx.out, "audit-import.log")
        write_text(detail, "%s\n" % exc)
        return Result("data-audit", "FAIL", "import failed", detail)
    dda = ctx.dda if ctx.dda and os.path.isdir(
        os.path.join(ctx.dda, "data", "json")) else None
    try:
        _result, _count, rows, allowed = cdf_audit.section(
            ctx.src, dda, ctx.allow_path)
    except Exception as exc:
        detail = os.path.join(ctx.out, "audit-error.log")
        write_text(detail, "%s\n" % exc)
        return Result("data-audit", "FAIL", "audit failed", detail)
    detail = os.path.join(ctx.out, "audit.tsv")
    write_text(detail, cdf_audit.format_tsv(rows))
    result, counts, findings = audit_result(rows, allowed)
    if dda is None:
        counts += " (no DDA)"
    return Result("data-audit", result, counts, detail, findings)


def section_keybindings(ctx):
    """AT2a's tool, reserved: py -3 -I tools/cdf_kbcheck.py --section."""
    tool = os.path.join(ctx.src, "tools", "cdf_kbcheck.py")
    if not os.path.isfile(tool):
        return skip("keybindings", "not installed")
    detail = os.path.join(ctx.out, "keybindings.log")
    try:
        rc, text = run_capture([sys.executable, "-I", tool, "--section"],
                               ctx.src, 900)
    except (OSError, subprocess.SubprocessError) as exc:
        write_text(detail, "%s\n" % exc)
        return Result("keybindings", "FAIL", "tool failed", detail)
    write_text(detail, text)
    # Same one-line contract as cdf_audit.py --section.
    match = None
    for line in reversed(text.splitlines()):
        parts = line.split("\t")
        if len(parts) >= 2 and parts[0].strip() == "keybindings" \
                and parts[1].strip() in ("PASS", "FAIL"):
            match = parts
            break
    if match is None:
        return Result("keybindings", "FAIL" if rc else "PASS",
                      "no section line (rc=%d)" % rc, detail)
    counts = "\t".join(match[2:]).strip() or "-"
    return Result("keybindings", match[1].strip(), counts, detail)


def section_tag(ctx, name, tag):
    """A C++ check family: a positive name list through shardrun behaviour."""
    if ctx.quick:
        return skip(name, "no build")
    if ctx.build == "failed":
        return Result(name, "FAIL", "build failed", ctx.build_log)
    exe = ctx.test_exe
    if not os.path.isfile(exe):
        return skip(name, "no build")
    names, list_log = list_test_names(ctx, exe, tag)
    list_detail = os.path.join(ctx.out, "%s-names.log" % name)
    write_text(list_detail, list_log)
    if names is None:
        return Result(name, "FAIL", "test list failed", list_detail)
    if not names:
        return skip(name, "not installed")
    out_dir = os.path.join(ctx.out, name)
    summary = run_shards(ctx, exe, names, out_dir, ctx.shard_size)
    detail = os.path.join(out_dir, "summary.json")
    write_text(detail, json.dumps(summary, indent=1) + "\n")
    crashed = len(summary["crashed_shards"])
    never = len(summary["never_run"])
    fails = len(summary["fails"])
    bad = bool(fails or crashed or never)
    counts = "ran %d/%d, fails %d" % (len(set(summary["ran"])), len(names),
                                      fails)
    if crashed:
        counts += ", crashed %d" % crashed
    if never:
        counts += ", never run %d" % never
    return Result(name, "FAIL" if bad else "PASS", counts, detail)


def section_check_mods(ctx):
    if ctx.quick:
        return skip("check-mods", "no build")
    if ctx.build == "failed":
        return Result("check-mods", "FAIL", "build failed", ctx.build_log)
    exe = ctx.game_exe
    if not os.path.isfile(exe):
        return skip("check-mods", "no build")
    detail = os.path.join(ctx.out, "check-mods.log")
    try:
        rc, text = run_capture([exe, "--check-mods"], ctx.src, 1800)
    except (OSError, subprocess.SubprocessError) as exc:
        write_text(detail, "%s\n" % exc)
        return Result("check-mods", "FAIL", "run failed", detail)
    write_text(detail, text)
    hits = [line.strip() for line in text.splitlines()
            if not line.startswith("Checking mod ")
            and CHECK_MODS_ERROR_RE.search(line)]
    ok = rc == 0 and not hits
    counts = "ok" if ok else "%d error line(s), rc=%d" % (len(hits), rc)
    return Result("check-mods", "PASS" if ok else "FAIL", counts, detail)


def section_unit_suite(ctx):
    if ctx.quick:
        return skip("unit-suite", "no build")
    if ctx.build == "failed":
        return Result("unit-suite", "FAIL", "build failed", ctx.build_log)
    exe = ctx.test_exe
    if not os.path.isfile(exe):
        return skip("unit-suite", "no build")
    out_dir = os.path.join(ctx.out, "unit-suite")
    os.makedirs(out_dir, exist_ok=True)
    user_dir = os.path.join(out_dir, "user_dir")
    os.makedirs(user_dir, exist_ok=True)
    xml = os.path.join(out_dir, "tests.xml")
    log = os.path.join(out_dir, "tests.log")
    cmd = [exe, "--rng-seed", "42", "-r", "junit", "-o", xml,
           "--user-dir=%s%s" % (user_dir, os.sep)]
    try:
        with open(log, "w", encoding="utf-8", errors="replace") as f:
            rc = subprocess.run(cmd, cwd=ctx.src, stdout=f,
                                stderr=subprocess.STDOUT).returncode
    except OSError as exc:
        write_text(log, "%s\n" % exc)
        return Result("unit-suite", "FAIL", "run failed", log)
    detail = os.path.join(out_dir, "results.json")
    try:
        root = ET.parse(xml).getroot()
    except Exception as exc:
        write_text(detail, json.dumps({"error": str(exc), "rc": rc}, indent=1))
        return Result("unit-suite", "FAIL", "tests=ERROR rc=%d" % rc, detail)
    cases = {}
    for tc in root.iter("testcase"):
        name = (tc.get("name") or "").split("/")[0].strip()
        bad = tc.find("failure") is not None or tc.find("error") is not None
        cases[name] = cases.get(name, False) or bad
    failing = {name for name, bad in cases.items() if bad}
    baseline = load_baseline(ctx)
    new = sorted(failing - baseline) if baseline is not None else sorted(failing)
    fixed = sorted((baseline or set()) & (set(cases) - failing))
    data = {"total": len(cases), "failing": sorted(failing),
            "new_failures": new, "fixed": fixed}
    write_text(detail, json.dumps(data, indent=1) + "\n")
    counts = "new_failures=%d fixed=%d (%d/%d)" % (
        len(new), len(fixed), len(cases) - len(failing), len(cases))
    if baseline is None:
        counts += " NO BASELINE"
    return Result("unit-suite", "FAIL" if new else "PASS", counts, detail)


def list_test_names(ctx, exe, tag):
    """Positive Catch2 name list for a tag; (names or None, log text)."""
    cmd = [exe, tag, "--list-test-names-only"]
    try:
        rc, text = run_capture(cmd, ctx.src, 300)
    except (OSError, subprocess.SubprocessError) as exc:
        return None, "%s\n" % exc
    if rc != 0:
        return None, "command: %s\nrc=%d\n%s" % (" ".join(cmd), rc, text)
    names = []
    for line in text.splitlines():
        name = line.strip()
        if not name or name.startswith("="):
            continue
        if len(name) >= 2 and name[0] == '"' and name[-1] == '"':
            name = name[1:-1]
        names.append(name)
    return names, text


def esc_name(name):
    """shardrun.py:26-31, the Catch2 test-spec escaping."""
    for ch in ("\\", ",", "[", "]", "*"):
        name = name.replace(ch, "\\" + ch)
    if name.startswith("~"):
        name = "\\" + name
    return '"' + name + '"' if " " in name or name.startswith('"') else name


def run_shards(ctx, exe, names, out_dir, size):
    """Copied from C:\\CDF\\staging\\shardrun.py (outside the repository)."""
    os.makedirs(out_dir, exist_ok=True)
    user_dir = os.path.join(out_dir, "user_dir")
    os.makedirs(user_dir, exist_ok=True)
    summary = {"shards": [], "ran": [], "fails": [],
               "crashed_shards": [], "never_run": [], "skipped": []}
    case_re = re.compile(r'<testcase [^>]*?\sname="([^"]*)"[^>]*?(/?)>')
    fail_re = re.compile(
        r'<testcase [^>]*?\sname="([^"]*)"[^>]*?(?<!/)>\s*<(?:failure|error)')

    def top(name):
        return name.split("/")[0]

    for start in range(0, len(names), size):
        chunk = names[start:start + size]
        k = start // size
        xml = os.path.join(out_dir, "shard%d.xml" % k)
        log = os.path.join(out_dir, "shard%d.log" % k)
        spec = ",".join(esc_name(name) for name in chunk)
        cmd = [exe, spec, "--rng-seed", "42", "-r", "junit", "-o", xml,
               "--user-dir=%s%s" % (user_dir, os.sep)]
        timed_out = False
        with open(log, "w", encoding="utf-8", errors="replace") as lf:
            p = subprocess.Popen(cmd, cwd=ctx.src, stdin=subprocess.DEVNULL,
                                 stdout=lf, stderr=subprocess.STDOUT)
            try:
                p.wait(timeout=SHARD_TIMEOUT)
            except subprocess.TimeoutExpired:
                timed_out = True
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)],
                               capture_output=True)
        text = ""
        if os.path.exists(xml):
            with open(xml, encoding="utf-8", errors="replace") as f:
                text = f.read()
        ran = []
        for name, _slash in case_re.findall(text):
            name = top(name)
            if name not in ran:
                ran.append(name)
        fails = []
        for match in fail_re.finditer(text):
            name = top(match.group(1))
            if name not in fails:
                fails.append(name)
        complete = (text.rstrip().endswith("</testsuites>")
                    and "Stack trace at fatal error" not in text)
        if not complete or timed_out:
            summary["crashed_shards"].append(k)
        summary["shards"].append({"k": k, "n": len(chunk), "ran": len(ran),
                                  "fails": len(fails), "complete": complete,
                                  "timed_out": timed_out,
                                  "rc": p.returncode})
        summary["ran"] += ran
        summary["fails"] += [f for f in fails if f not in summary["fails"]]
        summary["never_run"] += [n for n in chunk if n not in ran]
        write_text(os.path.join(out_dir, "summary.json"),
                   json.dumps(summary, indent=1) + "\n")
        print("shard %d: %d tests, ran %d, fails %d, complete=%s"
              % (k, len(chunk), len(ran), len(fails), complete), flush=True)
    return summary


def load_baseline(ctx):
    path = os.path.join(ctx.src, "orchestration", "test-baseline.json")
    if not os.path.isfile(path):
        path = DEFAULT_BASELINE
    if not os.path.isfile(path):
        return None
    try:
        with open(path, encoding="utf-8") as f:
            return set(json.load(f).get("failing", []))
    except (OSError, ValueError):
        return None


# --------------------------------------------------------------- build

def prepare_build(ctx):
    if ctx.quick:
        return
    if ctx.skip_build:
        ctx.build = "ok" if os.path.isfile(ctx.test_exe) else "unavailable"
        return
    if not os.path.isfile(DEFAULT_BUILD_BAT):
        ctx.build = "unavailable"
        return
    ctx.build_log = os.path.join(ctx.out, "build.log")
    cmd = 'cmd /c ""%s" "%s""' % (DEFAULT_BUILD_BAT, ctx.src)
    try:
        with open(ctx.build_log, "w", encoding="utf-8",
                  errors="replace") as f:
            rc = subprocess.run(cmd, cwd=ctx.src, stdout=f,
                                stderr=subprocess.STDOUT).returncode
    except OSError as exc:
        write_text(ctx.build_log, "%s\n" % exc)
        ctx.build = "failed"
        return
    ctx.build = "ok" if rc == 0 else "failed"


# --------------------------------------------------------------- registry

def section_ui_smoke(ctx):
    return section_tag(ctx, "ui-smoke", "[audit][ui]")


def section_ui_golden(ctx):
    return section_tag(ctx, "ui-golden", "[audit][golden]")


def section_sweep_armor(ctx):
    return section_tag(ctx, "sweep-armor", "[audit][sweep]")


def section_sweep_kits(ctx):
    return section_tag(ctx, "sweep-kits", "[audit][kits]")


SECTIONS = [
    ("keybindings", section_keybindings),
    ("data-audit", section_data_audit),
    ("ui-smoke", section_ui_smoke),
    ("ui-golden", section_ui_golden),
    ("sweep-armor", section_sweep_armor),
    ("sweep-kits", section_sweep_kits),
    ("check-mods", section_check_mods),
    ("unit-suite", section_unit_suite),
]


def select_sections(only):
    if not only:
        return list(SECTIONS)
    wanted = [name.strip() for name in only.split(",") if name.strip()]
    known = dict(SECTIONS)
    unknown = [name for name in wanted if name not in known]
    if unknown:
        raise ValueError("unknown section(s): %s (known: %s)"
                         % (", ".join(unknown), ", ".join(known)))
    return [(name, known[name]) for name in wanted]


# --------------------------------------------------------------- page

def display_path(ctx, path):
    if not path:
        return "-"
    try:
        return os.path.relpath(path, ctx.src)
    except ValueError:
        return path


def render_page(ctx, results):
    commit = ctx.commit
    build = ctx.build_number
    lines = [
        "CDF check report  %s  tree %s  commit %s  build %s"
        % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), ctx.src,
           commit, build),
    ]
    failed = sum(1 for result in results if result.result == "FAIL")
    lines.append("RESULT: %s%s" % (
        "PASS" if not failed else "FAIL",
        "" if not failed else " (%d sections failed)" % failed))
    lines.append("%-18s %-6s %-31s %s"
                 % ("section", "result", "counts", "detail file"))
    for result in results:
        lines.append("%-18s %-6s %-31s %s"
                     % (result.name, result.result, result.counts,
                        display_path(ctx, result.detail)))
    findings = []
    for result in results:
        findings.extend(result.findings)
    if not findings:
        lines.append("NEW findings (not in the allowlist): none")
    else:
        lines.append("NEW findings (not in the allowlist):")
        for finding in findings[:15]:
            lines.append("  %-10s %-26s %s"
                         % (finding.check, finding.item, finding.reason))
        if len(findings) > 15:
            lines.append("  ... %d more (see the detail files)"
                         % (len(findings) - 15))
    return "\n".join(lines) + "\n"


def exit_code(results):
    failed = any(result.result == "FAIL" for result in results)
    new = any(result.findings for result in results)
    return 1 if failed or new else 0


# --------------------------------------------------------------- selftest

def selftest():
    cases = 0

    def check(label, condition):
        nonlocal cases
        cases += 1
        if not condition:
            raise AssertionError(label)

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "src")
        os.makedirs(src)

        fake_ctx = Context(src, "report", None, True, True, 25)
        check("default allow path",
              fake_ctx.allow_path == os.path.join(src, ALLOW_PATH))
        fake_ctx.commit, fake_ctx.build_number = "deadbeefcafe", "test"
        passed = Result("keybindings", "PASS", "0 findings", "")
        skipped = Result("ui-smoke", "SKIP", "no build", "")
        failed = Result("data-audit", "FAIL", "2 findings (1 new)",
                        os.path.join(fake_ctx.out, "audit.tsv"),
                        [Finding("A1", "new_item", "flag only in DDA")])

        page = render_page(fake_ctx, [passed, skipped, failed])
        check("FAIL count", "RESULT: FAIL (1 sections failed)" in page)
        check("SKIP row", "ui-smoke" in page and "SKIP" in page)
        check("new finding row", "new_item" in page
              and "flag only in DDA" in page)
        check("FAIL exit code", exit_code([passed, skipped, failed]) == 1)

        page = render_page(fake_ctx, [passed, skipped])
        check("PASS page", "RESULT: PASS" in page)
        check("SKIP is neutral", "RESULT: PASS" in page
              and exit_code([passed, skipped]) == 0)

        rows = [
            ("A1", "error", "new_item", "flag only in DDA", "src"),
        ]
        result, counts, findings = audit_result(rows, 1)
        check("allowed count", counts == "2 findings (1 new, 1 allowed)")
        check("new finding fails", result == "FAIL"
              and findings[0].item == "new_item")
        result, counts, findings = audit_result([], 1)
        check("all allowed passes", result == "PASS"
              and counts == "1 findings (0 new, 1 allowed)" and not findings)
        result, counts, findings = audit_result(rows)
        check("no allow count", counts == "1 findings (1 new)")

        selected = select_sections("data-audit,keybindings")
        check("--only selects", [name for name, _fn in selected]
              == ["data-audit", "keybindings"])
        try:
            select_sections("nope")
        except ValueError:
            cases += 1
        else:
            raise AssertionError("unknown --only name was accepted")

        check("page is at most 60 lines", page.count("\n") <= 60)

    print("selftest ok: %d cases" % cases)
    return 0


# --------------------------------------------------------------- main

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="CDF one-command check report (PL-62 AT0).")
    parser.add_argument("--src", default=DEFAULT_SRC)
    parser.add_argument("--out", default="report")
    parser.add_argument("--only", default="")
    parser.add_argument("--quick", action="store_true",
                        help="dry mode: no build, build sections SKIP")
    parser.add_argument("--skip-build", action="store_true",
                        help="do not build, run binaries that already exist")
    parser.add_argument("--dda", default=DEFAULT_DDA)
    parser.add_argument("--shard-size", type=int, default=25)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()
    if not os.path.isdir(os.path.join(args.src, "data", "json")):
        print("error: no data/json under --src %s" % args.src,
              file=sys.stderr)
        return 2
    try:
        selected = select_sections(args.only)
    except ValueError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    ctx = Context(args.src, args.out, args.dda, args.quick,
                  args.skip_build, max(1, args.shard_size))
    build_sections = {"ui-smoke", "ui-golden", "sweep-armor", "sweep-kits",
                      "check-mods", "unit-suite"}
    if any(name in build_sections for name, _fn in selected):
        prepare_build(ctx)
    results = []
    for name, function in selected:
        try:
            results.append(function(ctx))
        except Exception as exc:  # one broken family must not hide the rest
            detail = os.path.join(ctx.out, "%s-error.log" % name)
            write_text(detail, "%s\n" % exc)
            results.append(Result(name, "FAIL", "section crashed", detail))

    page = render_page(ctx, results)
    report = os.path.join(ctx.out, "REPORT.md")
    write_text(report, page)
    sys.stdout.write(page)
    return exit_code(results)


if __name__ == "__main__":
    sys.exit(main())
