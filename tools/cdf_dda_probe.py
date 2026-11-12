#!/usr/bin/env python3
"""CDF DDA compatibility probe (report only).

Reads DDA JSON and reports what CDF's loader would not understand. It never
loads CDF, never changes a loader and never changes data. Standard library
only. Run as:

  py -3 -I tools/cdf_dda_probe.py [--dda DIR] [--cdf DIR] [--out FILE]
  py -3 -I tools/cdf_dda_probe.py --selftest [--fixture DIR]

--dda      DDA checkout (the folder holding data/json and data/mods).
           Default: env CDF_DDA_SRC, else the DDA checkout on this machine.
--cdf      CDF checkout (default: the repository this file lives in).
--out      write the TSV to FILE instead of stdout.
--selftest check the expected counts against tests/data/dda_probe (exit 1 on a
           mismatch). --fixture DIR selects another fixture root.

Output is TSV with the columns scope, check, name, count, example. The
example is the first file (relative to the checkout) where the row was seen.

Checks:
  typed_objects      objects with a string "type" (per scope)
  unknown_type       "type" value that CDF's init.cpp does not register
  item_object        "ITEM" objects by subtypes (joined with +, or (none))
  unread_key         top-level key of an item-like object that CDF never
                     reads (heuristic: no quoted literal of that name in CDF
                     src/*.cpp and src/*.h)
  undefined_flag     item-like "flags" entry with no json_flag object in CDF
                     data/json or data/mods
  missing_dependency MOD_INFO dependency id that is not a CDF mod id
  no_modinfo         DDA mod dir without modinfo.json
  parse_error        JSON file that does not parse to an object or a list

Scopes: dda:core (data/json) and dda:mod:<dir> (each data/mods folder).
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile

ITEM_LIKE = {
    "ITEM", "GENERIC", "ARMOR", "TOOL", "TOOL_ARMOR", "AMMO", "GUN", "GUNMOD",
    "MAGAZINE", "COMESTIBLE", "BOOK", "PET_ARMOR", "BATTERY", "ENGINE", "WHEEL",
    "BIONIC_ITEM", "TOOLMOD",
}

DEFAULT_DDA = os.environ.get(
    "CDF_DDA_SRC", r"D:\Library\Games\PC Games\Cataclysm\CDDA\CDDA SRC")
DEFAULT_CDF = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_FIXTURE = os.path.join(DEFAULT_CDF, "tests", "data", "dda_probe")

# Expected rows of the fixture (scope, check, name) -> count. Each count is
# derived by hand from tests/data/dda_probe; see the report for the reasoning.
EXPECTED = {
    ("dda:core", "typed_objects", "-"): 4,
    ("dda:core", "unknown_type", "ITEM"): 2,
    ("dda:core", "unknown_type", "widget"): 1,
    ("dda:core", "item_object", "GENERIC"): 1,
    ("dda:core", "item_object", "GUN"): 1,
    ("dda:core", "unread_key", "frobnicate"): 1,
    ("dda:core", "undefined_flag", "EDIBLE_FROZEN"): 1,
    ("dda:mod:fake_a", "typed_objects", "-"): 3,
    ("dda:mod:fake_a", "unknown_type", "ITEM"): 1,
    ("dda:mod:fake_a", "unknown_type", "effect_on_condition"): 1,
    ("dda:mod:fake_a", "item_object", "GENERIC"): 1,
    ("dda:mod:fake_a", "undefined_flag", "NOPE"): 1,
    ("dda:mod:fake_a", "missing_dependency", "dda"): 1,
    ("dda:mod:fake_b", "typed_objects", "-"): 3,
    ("dda:mod:fake_b", "parse_error", "-"): 1,
    ("dda:mod:fake_b", "unknown_type", "TOOL"): 1,
    ("dda:mod:fake_b", "unknown_type", "ITEM"): 1,
    ("dda:mod:fake_b", "item_object", "GUN+TOOL"): 1,
    ("dda:mod:fake_b", "undefined_flag", "NOPE"): 1,
}


def rel(base, path):
    return os.path.relpath(path, base).replace("\\", "/")


def read_text(path):
    with open(path, encoding="utf-8", errors="ignore") as f:
        return f.read()


def iter_json(root, base):
    """Yield (relpath, obj) for every dict in each *.json under root.

    A file that does not parse (or is neither a dict nor a list) yields
    (relpath, None) once, so the caller can count it as parse_error.
    """
    for dirpath, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            if not name.endswith(".json"):
                continue
            path = os.path.join(dirpath, name)
            where = rel(base, path)
            try:
                with open(path, encoding="utf-8-sig") as f:
                    data = json.load(f)
            except (ValueError, OSError):
                yield where, None
                continue
            if isinstance(data, dict):
                data = [data]
            if not isinstance(data, list):
                yield where, None
                continue
            for obj in data:
                if isinstance(obj, dict):
                    yield where, obj


def cdf_types(cdf):
    src = read_text(os.path.join(cdf, "src", "init.cpp"))
    return set(re.findall(r'add\(\s*"([A-Za-z_0-9]+)"', src))


def cdf_literals(cdf):
    lits = set()
    for dirpath, dirs, files in os.walk(os.path.join(cdf, "src")):
        dirs.sort()
        for name in sorted(files):
            if name.endswith((".cpp", ".h")):
                text = read_text(os.path.join(dirpath, name))
                lits.update(re.findall(r'"([A-Za-z_][A-Za-z0-9_\-]*)"', text))
    return lits


def cdf_data(cdf):
    """Return (json_flag ids, MOD_INFO ids) from CDF data/json and data/mods."""
    flags = set()
    mods = set()
    for obj_rel, obj in iter_json(os.path.join(cdf, "data", "json"), cdf):
        if obj is not None and obj.get("type") == "json_flag":
            if isinstance(obj.get("id"), str):
                flags.add(obj["id"])
    for obj_rel, obj in iter_json(os.path.join(cdf, "data", "mods"), cdf):
        if obj is None:
            continue
        if obj.get("type") == "json_flag" and isinstance(obj.get("id"), str):
            flags.add(obj["id"])
        if obj.get("type") == "MOD_INFO" and isinstance(obj.get("id"), str):
            mods.add(obj["id"])
    return flags, mods


class Acc:
    """Counts rows keyed by (scope, check, name); keeps the first example."""

    def __init__(self):
        self.rows = {}

    def add(self, scope, check, name, example):
        key = (scope, check, name)
        if key in self.rows:
            self.rows[key][0] += 1
        else:
            self.rows[key] = [1, example]


def scan_scope(acc, scope, root, base, types, literals, flags, cdf_mods):
    for where, obj in iter_json(root, base):
        if obj is None:
            acc.add(scope, "parse_error", "-", where)
            continue
        kind = obj.get("type")
        if not isinstance(kind, str):
            continue
        acc.add(scope, "typed_objects", "-", where)
        if kind not in types:
            acc.add(scope, "unknown_type", kind, where)
        if kind == "ITEM":
            subs = obj.get("subtypes", [])
            if isinstance(subs, str):
                subs = [subs]
            names = sorted(s for s in subs if isinstance(s, str)) if isinstance(subs, list) else []
            acc.add(scope, "item_object", "+".join(names) or "(none)", where)
        if kind == "MOD_INFO":
            deps = obj.get("dependencies", [])
            for dep in deps if isinstance(deps, list) else []:
                if isinstance(dep, str) and dep not in cdf_mods:
                    acc.add(scope, "missing_dependency", dep, where)
        if kind in ITEM_LIKE:
            for key in obj:
                if key not in literals and not key.startswith("//"):
                    acc.add(scope, "unread_key", key, where)
            item_flags = obj.get("flags", [])
            for flag in item_flags if isinstance(item_flags, list) else []:
                if isinstance(flag, str) and flag not in flags:
                    acc.add(scope, "undefined_flag", flag, where)


def probe(dda, cdf):
    types = cdf_types(cdf)
    literals = cdf_literals(cdf)
    flags, cdf_mods = cdf_data(cdf)
    acc = Acc()
    scan_scope(acc, "dda:core", os.path.join(dda, "data", "json"), dda,
               types, literals, flags, cdf_mods)
    mods = os.path.join(dda, "data", "mods")
    if os.path.isdir(mods):
        for name in sorted(os.listdir(mods)):
            mdir = os.path.join(mods, name)
            if not os.path.isdir(mdir):
                continue
            scope = "dda:mod:" + name
            if not os.path.isfile(os.path.join(mdir, "modinfo.json")):
                acc.add(scope, "no_modinfo", "-", rel(dda, mdir))
            scan_scope(acc, scope, mdir, dda, types, literals, flags, cdf_mods)
    return acc.rows


def format_tsv(rows):
    lines = ["scope\tcheck\tname\tcount\texample"]
    order = sorted(rows, key=lambda k: (k[0], k[1], -rows[k][0], k[2]))
    for key in order:
        count, example = rows[key]
        lines.append("\t".join([key[0], key[1], key[2], str(count), example]))
    return "\n".join(lines) + "\n"


def selftest(fixture):
    """Copy the fixture, add one truncated JSON file, and compare every row."""
    tmp = tempfile.mkdtemp(prefix="cdf_dda_probe_")
    try:
        root = os.path.join(tmp, "fixture")
        shutil.copytree(fixture, root)
        broken = os.path.join(root, "dda", "data", "mods", "fake_b", "broken.json")
        with open(broken, "w", encoding="utf-8") as f:
            f.write('[{"type": "ITEM", "id": ')
        rows = probe(os.path.join(root, "dda"), os.path.join(root, "cdf"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    actual = {key: value[0] for key, value in rows.items()}
    problems = []
    for key in sorted(set(EXPECTED) | set(actual)):
        want = EXPECTED.get(key)
        got = actual.get(key)
        if want != got:
            problems.append("MISMATCH %s expected=%s got=%s" % (" / ".join(key), want, got))
    for line in problems:
        print(line)
    if problems:
        print("selftest FAILED: %d mismatch(es) against %s" % (len(problems), fixture))
        return 1
    print("selftest ok: %d rows matched in %s" % (len(EXPECTED), fixture))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="CDF DDA compatibility probe (report only).")
    ap.add_argument("--dda", default=DEFAULT_DDA, help="DDA checkout")
    ap.add_argument("--cdf", default=DEFAULT_CDF, help="CDF checkout")
    ap.add_argument("--out", help="write TSV to this file")
    ap.add_argument("--selftest", action="store_true", help="check the fixture counts")
    ap.add_argument("--fixture", default=DEFAULT_FIXTURE, help="fixture root for --selftest")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest(args.fixture)
    if not os.path.isdir(os.path.join(args.dda, "data", "json")):
        print("error: no data/json under --dda %s" % args.dda, file=sys.stderr)
        return 2
    if not os.path.isfile(os.path.join(args.cdf, "src", "init.cpp")):
        print("error: no src/init.cpp under --cdf %s" % args.cdf, file=sys.stderr)
        return 2
    text = format_tsv(probe(args.dda, args.cdf))
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
