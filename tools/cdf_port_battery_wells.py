#!/usr/bin/env python3
"""Plan, apply and check the BATT1 battery-well port (PL-33b TASK-draft-BATT1-v2).

CDF still synthesizes a MAGAZINE_WELL pocket from the legacy BN `magazines` key
(src/item_factory.cpp "Guns and tools that take magazines get a magazine well"). DDA states the
well in `pocket_data`. This tool replaces the legacy keys of the 117 BATT1 rows (groups W1 and W7)
with the DDA well form and keeps the pockets the loader synthesizes today (the MOD pocket of a
tool, and the storage CONTAINER of a worn garment).

BATT5 (PL-33b TASK-draft-BATT5-v1) reuses the same rules for the 12 mod rows: 8 synthesized wells
and 4 integral tools (`crt_gasmask(_on)`, `crt_helmet(_on)`; NO_UNLOAD, DDA class integral) that get
the explicit DDA MAGAZINE pocket at their own CDF `max_charges`, with the MOD pocket behind it.

Reads ONLY the target file (outbox/PL-33b/targets/battery-tools.json, rows `packet` BATT1/BATT5 and
"BATT1 test") and the target item files; writes the item files, the test pin and the
before-snapshot.

Rules (draft step 1, plus the rulings the draft names):
  - well = { "pocket_type": "MAGAZINE_WELL", "rigid": true,
             "flag_restriction": target_flags,
             "item_restriction": [target_default] + target_item_restriction,
             "default_magazine": target_default }
    `flag_restriction` / `item_restriction` are omitted when their list is empty.
  - CDF: the loader sets default_magazine = item_restriction.front() (DDA item_pocket.cpp:160-165,
    cdf_port_dda_mags.py R3), so target_default is written first in item_restriction. It is also
    the flag-accepted default cell, so the extra entry admits nothing new.
  - a row with its own `magazines` gets its own `pocket_data`; a copy-from child without one
    inherits the parent's list (draft step 1). `magazine_well` goes with `magazines`.
  - every written list keeps `{ "pocket_type": "MOD" }` (the tool MOD pocket of the before-state)
    and, where the row's garment storage synthesizes a CONTAINER today, that CONTAINER.
  - N1 rows (packet "BATT1 test") are pin-only: no data edit.
  - `ammo` is not in the draft's drop list and stays (it carries the battery ammotype).

Usage (from the worktree root):
  python -I tools/cdf_port_battery_wells.py --target <battery-tools.json> --list
  python -I tools/cdf_port_battery_wells.py --target <...> --apply [--only FILE ...]
  python -I tools/cdf_port_battery_wells.py --target <...> --check
  python -I tools/cdf_port_battery_wells.py --target <...> --pin tests/data/dda_staged/batt1_pin.json \
      --before tests/data/dda_staged/battery_before.json
"""
import argparse
import importlib.util
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def _mags():
    spec = importlib.util.spec_from_file_location("cdf_port_dda_mags", str(HERE / "cdf_port_dda_mags.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MAGS = _mags()

PACKETS = ("BATT1", "BATT1 test", "BATT5")
N1_PACKET = "BATT1 test"
INTEGRAL_METHOD = "NO-TWIN-integral-NO_UNLOAD"
UNIT = "  "


def read_target(path):
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    return [r for r in data if r.get("packet") in PACKETS]


def is_integral(row):
    """BATT5: a tool without a DDA twin that carries NO_UNLOAD stays integral (draft step 2)."""
    return bool(row.get("target_int")) or (row.get("method") or "") == INTEGRAL_METHOD


def own_objects(root, rel):
    """Top-level item objects of a data file (no abstracts), with their text spans."""
    path = Path(root) / rel
    text = path.read_bytes().decode("utf-8")
    eol = "\r\n" if "\r\n" in text else "\n"
    out = []
    for s, e in MAGS.scan_top_objects(text):
        seg = text[s:e]
        try:
            o = json.loads(seg)
        except ValueError:
            continue
        if "abstract" not in o:
            out.append((o.get("id"), s, e, o))
    return text, eol, out


def item_file_of(root, item_id, prefer):
    """Find the file that defines `item_id` (prefer the target file, else scan data/json/items)."""
    path = Path(root) / prefer
    if path.exists():
        try:
            for o in json.loads(path.read_bytes().decode("utf-8")):
                if isinstance(o, dict) and o.get("id") == item_id and "abstract" not in o:
                    return prefer
        except ValueError:
            pass
    for p in sorted(Path(root).glob("data/json/items/**/*.json")):
        try:
            for o in json.loads(p.read_bytes().decode("utf-8")):
                if isinstance(o, dict) and o.get("id") == item_id and "abstract" not in o:
                    return str(p.relative_to(root)).replace("\\", "/")
        except ValueError:
            continue
    return None


def resolved(root, item_id, prefer, keys, seen=()):
    """The value of the first of `keys` on the copy-from chain (CDF item data is flat here)."""
    rel = item_file_of(root, item_id, prefer)
    if rel is None or item_id in seen:
        return None, None
    text, _, objs = own_objects(root, rel)
    del text
    for iid, _s, _e, o in objs:
        if iid != item_id:
            continue
        for k in keys:
            if k in o:
                return k, o[k]
        parent = o.get("copy-from")
        if parent:
            return resolved(root, parent, rel, keys, seen + (item_id,))
        return None, None
    return None, None


def before_other_pockets(root, row):
    """Pockets the loader synthesizes today besides the well (before-snapshot, draft step 0)."""
    pockets = [{"pocket_type": "MOD"}]
    kind, storage = resolved(root, row["id"], row["file"], ("storage",))
    if kind == "storage" and storage:
        ml = volume_ml(storage)
        if ml > 0:
            pockets.append({"pocket_type": "CONTAINER", "volume": str(storage), "volume_ml": ml,
                            "carry": True})
            if ml >= 10000:
                pockets.append({"pocket_type": "CONTAINER", "volume": "10 L", "volume_ml": 10000,
                                "carry": False, "holster": True})
    return pockets


def volume_ml(text):
    text = str(text).strip().lower()
    try:
        if text.endswith("ml"):
            return int(float(text[:-2].strip()))
        if text.endswith("l"):
            return int(float(text[:-1].strip()) * 1000)
        if text.endswith("l "):
            return int(float(text[:-2].strip()) * 1000)
    except ValueError:
        return 0
    return 0


def expected_pockets(row, others):
    """The pocket_data list the row's own record must carry."""
    if is_integral(row):
        # CDF: BATT5 draft step 2. A tool without a DDA twin that carries NO_UNLOAD stays integral:
        # the DDA integral MAGAZINE form (rigid + the ammo count, as R4a/R4b/BATT2), then the MOD
        # pocket the tool has today. Capacity is the object's own `max_charges` (CDF number).
        mag = {"pocket_type": "MAGAZINE", "rigid": True,
               "ammo_restriction": {"battery": int(row["target_int"])}}
        pockets = [mag]
        for p in others:
            if p["pocket_type"] == "MOD":
                pockets.append({"pocket_type": "MOD"})
        return pockets
    # CDF: the old `magazines` list stays in item_restriction, order default-cell-first. The draft's
    # subset (target_item_restriction) covers only the cells DDA's flags exclude, but CDF's
    # untargeted reload (item::is_reloadable_helper -> magazine_compatible) reads item_restriction
    # alone, and the pre-port legacy list answered there; the whole list keeps NPC reloads and the
    # ammo_set fallback at parity (found by auto_pickup_should_improve_your_life).
    ir = [row["target_default"]]
    ir.extend(x for x in (row.get("cdf_list") or []) if x != row["target_default"])
    well = {"pocket_type": "MAGAZINE_WELL", "max_item_length": "2 km", "rigid": True}
    if row.get("target_flags"):
        well["flag_restriction"] = list(row["target_flags"])
    if ir:
        well["item_restriction"] = ir
    well["default_magazine"] = row["target_default"]
    pockets = [well]
    for p in others:
        if p["pocket_type"] == "MOD":
            pockets.append({"pocket_type": "MOD"})
        elif p["pocket_type"] == "CONTAINER":
            c = {"pocket_type": "CONTAINER",
                 "max_contains_volume": p.get("volume") or ("%d ml" % p["volume_ml"])}
            if p.get("holster"):
                c["holster"] = True
            pockets.append(c)
    return pockets


def fmt_pocket(p, indent, eol):
    """BN-style pocket entry: MOD and the integral MAGAZINE inline, a well multi-line."""
    if p.get("pocket_type") == "MOD":
        return '{ "pocket_type": "MOD" }'
    if p.get("pocket_type") == "MAGAZINE":
        return ('{ "pocket_type": "MAGAZINE", "rigid": true, "ammo_restriction": '
                '{ "battery": %d } }' % p["ammo_restriction"]["battery"])
    inner = indent + UNIT
    fields = []
    for k, x in p.items():
        if isinstance(x, bool):
            text = "true" if x else "false"
        elif isinstance(x, list):
            text = "[ " + ", ".join(json.dumps(i) for i in x) + " ]"
        else:
            text = json.dumps(x, ensure_ascii=False)
        fields.append(inner + UNIT + json.dumps(k) + ": " + text)
    return "{" + eol + ("," + eol).join(fields) + eol + inner + "}"


def fmt_value(v, indent, eol):
    """BN-style layout: pockets multi-line, short key lists inline, MOD inline."""
    inner = indent + UNIT
    body = [inner + fmt_pocket(p, indent, eol) for p in v]
    return "[" + eol + ("," + eol).join(body) + eol + indent + "]"


def edit_row(seg, row, others, eol):
    """Replace `magazines` with `pocket_data` and drop `magazine_well` in one object's text."""
    e = MAGS._find(seg, "magazines")
    if e is None:
        return seg, "no own magazines"
    lead = seg[:e["ks"]]
    ls = lead.rfind("\n") + 1
    indent = lead[ls:] if not lead[ls:].strip() else MAGS_INDENT
    value = fmt_value(expected_pockets(row, others), indent, eol)
    seg = seg[:e["vs"]] + value + seg[e["ve"]:]
    seg = seg[:e["ks"]] + json.dumps("pocket_data") + seg[e["ke"]:]
    seg = MAGS.remove_key(seg, "magazine_well")
    return seg, "edited"


def edit_integral_row(seg, row, others, eol):
    """Insert `pocket_data` after the `ammo` key of an integral tool (no `magazines` key to replace)."""
    e = MAGS._find(seg, "ammo")
    if e is None:
        return None
    nl = seg.find(eol, e["ve"])
    if nl < 0:
        return None
    ls = seg.rfind(eol, 0, e["ks"]) + len(eol)
    indent = seg[ls:e["ks"]] if not seg[ls:e["ks"]].strip() else MAGS_INDENT
    value = fmt_value(expected_pockets(row, others), indent, eol)
    line = indent + json.dumps("pocket_data") + ": " + value + "," + eol
    return seg[:nl + len(eol)] + line + seg[nl + len(eol):]


MAGS_INDENT = "    "


def apply_file(root, rel, rows, dry_run):
    path = Path(root) / rel
    text, eol, objs = own_objects(root, rel)
    by_id = {iid: (s, e) for iid, s, e, _o in objs}
    todo = [r for r in rows if r["file"] == rel and r.get("packet") != N1_PACKET]
    done, skipped = [], []
    # edit from the last object to the first so the earlier spans stay valid
    order = sorted(todo, key=lambda r: -by_id[r["id"]][0] if r["id"] in by_id else 0)
    for r in order:
        if r["id"] not in by_id:
            skipped.append((r["id"], "object not found"))
            continue
        s, e = by_id[r["id"]]
        others = before_other_pockets(root, r)
        seg = text[s:e]
        if MAGS._find(seg, "pocket_data") is not None:
            skipped.append((r["id"], "already has pocket_data"))
            continue
        if is_integral(r):
            new = edit_integral_row(seg, r, others, eol)
            why = "no ammo key"
        else:
            new, why = edit_row(seg, r, others, eol)
        if new is not None and new != seg:
            text = text[:s] + new + text[e:]
            done.append(r["id"])
        else:
            skipped.append((r["id"], why))
    if done:
        try:
            json.loads(text.lstrip("\ufeff"))
        except ValueError as exc:
            raise SystemExit("%s: edited text does not parse: %s" % (rel, exc))
        if not dry_run:
            path.write_bytes(text.encode("utf-8"))
    return done, skipped


def check(root, rows):
    """Rows whose own record still carries `magazines`, or whose explicit list differs."""
    problems = []
    files = sorted({r["file"] for r in rows if r.get("packet") != N1_PACKET})
    for rel in files:
        text, eol, objs = own_objects(root, rel)
        del text, eol
        by_id = {iid: o for iid, _s, _e, o in objs}
        for r in rows:
            if r["file"] != rel or r.get("packet") == N1_PACKET:
                continue
            o = by_id.get(r["id"])
            if o is None:
                problems.append((r["id"], "object not found"))
                continue
            if "magazines" in o:
                problems.append((r["id"], "own magazines still present"))
            if "pocket_data" not in o:
                if is_integral(r):
                    problems.append((r["id"], "no own pocket_data"))
                continue
            want = expected_pockets(r, before_other_pockets(root, r))
            if norm(o["pocket_data"]) != norm(want):
                problems.append((r["id"], "pocket_data differs: %s" % json.dumps(o["pocket_data"])[:120]))
    return problems


def norm(v):
    if isinstance(v, dict):
        return {k: norm(x) for k, x in v.items() if not k.startswith("//")}
    if isinstance(v, list):
        return [norm(x) for x in v]
    return v


def effective_source(root, row, by_id):
    """The row whose own `magazines`/`pocket_data` the loaded well comes from.

    A copy-from child without its own legacy list inherits the parent's pockets (draft step 1), so
    its expected tuple is the nearest ancestor's, not its own planner row (the planner derived a
    per-id tuple for every row, including children whose keys never reach the loader).
    """
    cur = row
    seen = set()
    while cur is not None and cur["id"] not in seen:
        seen.add(cur["id"])
        _text, _eol, objs = own_objects(root, cur["file"])
        obj = next((o for iid, _s, _e, o in objs if iid == cur["id"]), None)
        if obj is None:
            return row
        if "pocket_data" in obj or "magazines" in obj:
            return cur
        parent = obj.get("copy-from")
        if not parent or parent not in by_id:
            return cur
        cur = by_id[parent]
    return row


def own_object(root, row):
    _text, _eol, objs = own_objects(root, row["file"])
    for iid, _s, _e, o in objs:
        if iid == row["id"]:
            return o
    return None


def packet_label(rows):
    names = sorted({r["packet"] for r in rows})
    if names == ["BATT1", "BATT1 test"]:
        return "BATT1"
    return "/".join(names)


def write_pin(path, rows, root):
    by_id = {r["id"]: r for r in rows}
    out = []
    for r in rows:
        if is_integral(r):
            o = own_object(root, r) or {}
            out.append({
                "id": r["id"],
                "group": r["group"],
                "file": r["file"],
                "packet": r["packet"],
                "kind": "integral",
                "integral": int(r["target_int"]),
                "ammo": o.get("ammo"),
                "max_charges": o.get("max_charges"),
                "initial_charges": o.get("initial_charges", 0),
                "other_pockets": before_other_pockets(root, r),
            })
            continue
        src = effective_source(root, r, by_id)
        others = before_other_pockets(root, r)
        out.append({
            "id": r["id"],
            "group": r["group"],
            "file": r["file"],
            "packet": r["packet"],
            "kind": "well",
            "source_id": src["id"],
            "flags": list(src.get("target_flags") or []),
            "default": src["target_default"],
            "item_restriction": ([src["target_default"]] + [
                x for x in (src.get("cdf_list") or []) if x != src["target_default"]]
                                 if src.get("cdf_list") else []),
            "target_item_restriction": list(src.get("target_item_restriction") or []),
            "old_cells": list(src.get("cdf_list") or []),
            "capacity_before": src["capacity_before"],
            "other_pockets": others,
        })
    label = packet_label(rows)
    doc = {
        "format": 1,
        "packet": label,
        "source": "outbox/PL-33b/targets/battery-tools.json (rows packet %s); "
                  "capacity = the CDF default cell's number (AUTO-236: cells keep CDF sizes); "
                  "item_restriction = the target cells, target default first (loader rule)" % label,
        "rows": out,
    }
    Path(path).write_bytes((json.dumps(doc, indent=1, ensure_ascii=False) + "\n").encode("utf-8"))


def write_before(path, rows, root):
    out = []
    for r in rows:
        if is_integral(r):
            old = {
                "pocket_type": "MAGAZINE",
                "ammo_restriction": {"battery": int(r["target_int"])},
                "note": "the synthesized integral MAGAZINE pocket, rewritten by the pin file",
            }
        else:
            old = {
                "pocket_type": "MAGAZINE_WELL",
                "item_restriction": list(r.get("cdf_list") or []),
                "default_magazine": r.get("cdf_default"),
                "note": "the synthesized well replaced by the pin file",
            }
        out.append({
            "id": r["id"],
            "old_pocket": old,
            "other_pockets": before_other_pockets(root, r),
        })
    label = packet_label(rows)
    doc = {
        "format": 1,
        "packet": label,
        "note": "before-snapshot (draft step 0): the pockets the loader synthesizes for every %s row "
                "before the port, minus the well/integral entry the pin file replaces. Mod items, so "
                "derived from the legacy keys and the item records (the test binary loads no mods)."
                % label,
        "rows": out,
    }
    Path(path).write_bytes((json.dumps(doc, indent=1, ensure_ascii=False) + "\n").encode("utf-8"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True, help="outbox/PL-33b/targets/battery-tools.json")
    ap.add_argument("--root", default=".", help="worktree root (default: cwd)")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", nargs="*", default=None, help="restrict --apply to these files")
    ap.add_argument("--packets", nargs="*", default=None, help="restrict rows to these packet labels")
    ap.add_argument("--pin", help="write the expected-tuple pin file")
    ap.add_argument("--before", help="write the before-snapshot file")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    rows = read_target(args.target)
    if args.packets:
        rows = [r for r in rows if r["packet"] in args.packets]
    print("rows: %d (%d edits, %d N1 checks)" % (
        len(rows), sum(1 for r in rows if r["packet"] != N1_PACKET),
        sum(1 for r in rows if r["packet"] == N1_PACKET)))

    if args.list:
        for rel in sorted({r["file"] for r in rows}):
            ids = [r["id"] for r in rows if r["file"] == rel]
            print("%3d  %s" % (len(ids), rel))

    if args.pin:
        write_pin(args.pin, rows, root)
        print("wrote %s" % args.pin)
    if args.before:
        write_before(args.before, rows, root)
        print("wrote %s" % args.before)

    if args.check:
        problems = check(root, rows)
        for iid, why in problems:
            print("FAIL %s: %s" % (iid, why))
        print("check: %d row(s) with problems" % len(problems))
        return 1 if problems else 0

    if args.apply or args.dry_run:
        counts, skipped = {}, []
        for rel in sorted({r["file"] for r in rows}):
            if args.only and not any(rel == f or rel.startswith(f.rstrip("/") + "/") for f in args.only):
                continue
            done, skip = apply_file(root, rel, rows, args.dry_run or not args.apply)
            skipped.extend((rel, *s) for s in skip)
            if done:
                counts[rel] = len(done)
        for rel, n in counts.items():
            print("%5d  %s" % (n, rel))
        for x in skipped:
            print("SKIP %s %s: %s" % x)
        print("%s: %d row(s) in %d file(s)" % (
            "would edit" if (args.dry_run or not args.apply) else "edited",
            sum(counts.values()), len(counts)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
