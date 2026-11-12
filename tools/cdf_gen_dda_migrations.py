#!/usr/bin/env python3
"""Generate data/json/obsoletion/migration_dda.json (CDF MIGDDA1, PL-49 A8).

Keeps the DDA MIGRATION objects (all of DDA data/json: obsoletion_and_migration_0.I/.J,
irradiated comestible migrations) that are clean for CDF:
  - the migrated-from id is not an item type anywhere in CDF data (core or mods),
  - CDF has no MIGRATION for it already (core data/json),
  - the target id (and every `contents` id) is an item type of CDF core data/json,
  - the row needs nothing the CDF loader does not know (variant, from_variant,
    reset_item_vars rows are left out).
Run with -I:  py -3 -I tools/cdf_gen_dda_migrations.py [--dda DIR] [--check] [--report]
--check exits 1 when the committed file differs from the generated text.
"""
import argparse
import json
import os
import sys

ITEM_TYPES = {
    "AMMO", "GUN", "ARMOR", "PET_ARMOR", "TOOL", "TOOLMOD", "TOOL_ARMOR", "BOOK",
    "COMESTIBLE", "CONTAINER", "ENGINE", "WHEEL", "FUEL", "GUNMOD", "MAGAZINE",
    "BATTERY", "GENERIC", "BIONIC_ITEM",
}
UNSUPPORTED = ("variant", "from_variant", "reset_item_vars")
OUT_REL = "data/json/obsoletion/migration_dda.json"


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def objects(path):
    data = load_json(path)
    if isinstance(data, dict):
        data = [data]
    return [o for o in data if isinstance(o, dict)] if isinstance(data, list) else []


def walk(root):
    for base, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            if name.endswith(".json"):
                yield os.path.join(base, name)


def ids_of(obj):
    i = obj.get("id")
    if isinstance(i, str):
        return [i]
    if isinstance(i, list):
        return [x for x in i if isinstance(x, str)]
    return []


def scan_cdf(root):
    """(core item ids, mod item ids, core migration ids)"""
    core, mods, mig = set(), set(), set()
    own = os.path.normpath(os.path.join(root, OUT_REL))
    for path in walk(os.path.join(root, "data", "json")):
        if os.path.normpath(path) == own:
            continue  # the generated file must not influence its own input
        try:
            objs = objects(path)
        except (ValueError, OSError):
            continue
        for o in objs:
            t = o.get("type")
            if t in ITEM_TYPES and not o.get("abstract"):
                core.update(ids_of(o))
            elif t == "MIGRATION":
                mig.update(ids_of(o))
    for path in walk(os.path.join(root, "data", "mods")):
        try:
            objs = objects(path)
        except (ValueError, OSError):
            continue
        for o in objs:
            if o.get("type") in ITEM_TYPES and not o.get("abstract"):
                mods.update(ids_of(o))
    return core, mods, mig


def content_ids(o):
    out = []
    for c in o.get("contents", []):
        out.append(c if isinstance(c, str) else c.get("id", ""))
    return out


def dda_rows(dda):
    """Every MIGRATION object of DDA core data/json, in sorted path order (last one wins)."""
    rows = {}
    base = os.path.join(dda, "data", "json")
    for path in walk(base):
        try:
            objs = objects(path)
        except (ValueError, OSError):
            continue
        for o in objs:
            if o.get("type") == "MIGRATION":
                for i in ids_of(o):
                    rows[i] = o
    return rows


def generate(root, dda):
    core, mods, mig = scan_cdf(root)
    rows = dda_rows(dda)
    kept, stats = [], {"dda_rows": len(rows)}
    reasons = {"source_live_core": 0, "source_live_mod": 0, "cdf_has_migration": 0,
               "target_missing": 0, "unsupported_field": 0, "self": 0}
    for src in sorted(rows):
        o = rows[src]
        target = o.get("replace")
        if src in core:
            reasons["source_live_core"] += 1
        elif src in mods:
            reasons["source_live_mod"] += 1
        elif src in mig:
            reasons["cdf_has_migration"] += 1
        elif src == target:
            reasons["self"] += 1
        elif any(k in o for k in UNSUPPORTED):
            reasons["unsupported_field"] += 1
        elif target not in core or not all(c in core for c in content_ids(o)):
            reasons["target_missing"] += 1
        else:
            row = {"type": "MIGRATION", "id": src, "replace": target}
            for k in ("flags", "charges", "contents"):
                if k in o:
                    row[k] = o[k]
            kept.append(row)
    stats.update(reasons)
    stats["kept"] = len(kept)
    return kept, stats


def fmt_value(v):
    if isinstance(v, list):
        if not v:
            return "[  ]"
        if all(isinstance(x, str) for x in v):
            return "[ " + ", ".join(json.dumps(x) for x in v) + " ]"
        return "[ " + ", ".join(fmt_value(x) for x in v) + " ]"
    if isinstance(v, dict):
        return "{ " + ", ".join("%s: %s" % (json.dumps(k), fmt_value(x)) for k, x in v.items()) + " }"
    return json.dumps(v)


def render(rows):
    out = ["["]
    for n, r in enumerate(rows):
        out.append("  {")
        keys = list(r)
        for m, k in enumerate(keys):
            out.append("    %s: %s%s" % (json.dumps(k), fmt_value(r[k]), "," if m + 1 < len(keys) else ""))
        out.append("  }" + ("," if n + 1 < len(rows) else ""))
    out.append("]")
    return "\n".join(out) + "\n"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--dda", default=os.environ.get("CDDA_SRC", r"C:\CDF\CDDA SRC"))
    ap.add_argument("--root", default=os.path.dirname(here))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    rows, stats = generate(a.root, a.dda)
    text = render(rows)
    out = os.path.join(a.root, OUT_REL)
    if a.report:
        print(stats)
    if a.check:
        try:
            with open(out, encoding="utf-8", newline="") as f:
                cur = f.read()
        except OSError:
            cur = None
        if cur != text:
            print("%s differs from the generated file" % OUT_REL, file=sys.stderr)
            return 1
        print("ok: %d rows" % len(rows))
        return 0
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s: %d rows" % (OUT_REL, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
