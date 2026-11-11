#!/usr/bin/env python3
"""Plan, apply and check the LINKUP1e `link_up` port (PL-33e TASK-draft-LINKUP1-v4, LINKUP1e).

DDA gives 45 tools beyond the battery hold list a `link_up` action: the cable is a second power
source beside the tool's own well (`ammo_remaining( include_linked )`, LINKUP1a). CDF has the
action engine (LINKUP1a) and the CABLE pocket (LINKUP1b) but none of the 45 items carries the
action yet. This tool adds the DDA action object to each item's own record and nothing else: the
well / battery pocket list stays exactly as it is (the brief: "no other key of the item changes").

Reads ONLY the staged rows file (tests/data/dda_staged/linkup1e_rows.json, the TSV group `other`
rows re-derived from DDA b4b62254ae2); writes the item files when asked.

Rules:
  - the action object is copied from the DDA twin verbatim (`type`, `cable_length`, `charge_rate`
    and the row's other keys: radio_on's menu_text/ammo_scale, electric_lighter's `//` note).
  - an item without `use_action` gets `[ { ... } ]`; a string or a single object becomes an array
    whose existing value is kept first, exactly as DDA writes the twin (`[ "CAMERA", { link_up } ]`).
  - a copy-from child needs its own action: `use_action` is not inherited member-wise.

Usage (from the worktree root):
  python -I tools/cdf_port_linkup_uses.py --list
  python -I tools/cdf_port_linkup_uses.py --apply [--only FILE ...]
  python -I tools/cdf_port_linkup_uses.py --check
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

ROWS = "tests/data/dda_staged/linkup1e_rows.json"
UNIT = "  "
ACTION = "link_up"


def read_rows(root, path):
    with open(Path(root) / path, encoding="utf-8") as fh:
        data = json.load(fh)
    rows = []
    for item_id, row in data["items"].items():
        row = dict(row)
        row["id"] = item_id
        rows.append(row)
    return rows


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
        if isinstance(o, dict) and "abstract" not in o:
            out.append((o.get("id"), s, e, o))
    return text, eol, out


def fmt_action(action):
    """DDA's inline form: { "type": "link_up", "cable_length": N, "charge_rate": "R W" }."""
    parts = []
    for k, v in action.items():
        parts.append("%s: %s" % (json.dumps(k), json.dumps(v, ensure_ascii=False)))
    return "{ " + ", ".join(parts) + " }"


def reindent(value, unit):
    """Indent the continuation lines of a JSON value by `unit` (its first line stays in place)."""
    lines = value.split("\n")
    return lines[0] + "".join("\n" + unit + line for line in lines[1:])


def edit_action(seg, action, eol):
    """Give one object's text the row's link_up action; returns (text, why)."""
    entry = MAGS._find(seg, "use_action")
    text = fmt_action(action)
    if entry is None:
        keys = MAGS.scan_keys(seg)
        indent = line_indent(seg, keys[0]["ks"])
        return MAGS.add_key(seg, "use_action", "[ " + text + " ]", eol, indent, UNIT), "added"
    value = seg[entry["vs"]:entry["ve"]]
    parsed = json.loads(value)
    if isinstance(parsed, str):
        return seg[:entry["vs"]] + "[ " + json.dumps(parsed) + ", " + text + " ]" + \
            seg[entry["ve"]:], "wrapped string"
    if isinstance(parsed, list):
        if any(isinstance(x, dict) and x.get("type") == ACTION for x in parsed):
            return seg, "already has link_up"
        closing = value.rfind("]")
        head = value[:closing].rstrip()
        tail = value[closing + 1:]
        if "\n" in value:
            indent = value[:closing].split("\n")[-1]
            insert = "," + eol + indent + UNIT + text + eol + indent
        else:
            insert = ", " + text + " "
        return seg[:entry["vs"]] + head + insert + "]" + tail + seg[entry["ve"]:], "appended"
    if isinstance(parsed, dict):
        if parsed.get("type") == ACTION:
            return seg, "already has link_up"
        base = line_indent(seg, entry["ks"])
        elem = base + UNIT
        body = reindent(value, UNIT)
        new = "[" + eol + elem + body + "," + eol + elem + text + eol + base + "]"
        return seg[:entry["vs"]] + new + seg[entry["ve"]:], "wrapped object"
    raise SystemExit("unexpected use_action shape: %r" % (parsed,))


def line_indent(seg, pos):
    """Whitespace that precedes `pos` on its own line."""
    ls = seg.rfind("\n", 0, pos) + 1
    return seg[ls:pos] if not seg[ls:pos].strip() else "    "


def apply_file(root, rel, rows, dry_run):
    path = Path(root) / rel
    text, eol, objs = own_objects(root, rel)
    by_id = {iid: (s, e) for iid, s, e, _o in objs}
    todo = [r for r in rows if r["file"] == rel]
    done, skipped = [], []
    # edit from the last object to the first so the earlier spans stay valid
    order = sorted(todo, key=lambda r: -by_id[r["id"]][0] if r["id"] in by_id else 0)
    for row in order:
        if row["id"] not in by_id:
            skipped.append((row["id"], "object not found"))
            continue
        s, e = by_id[row["id"]]
        seg = text[s:e]
        new, why = edit_action(seg, row["action"], eol)
        if new is None:
            skipped.append((row["id"], why))
            continue
        if new == seg:
            skipped.append((row["id"], why))
            continue
        text = text[:s] + new + text[e:]
        done.append(row["id"])
    if done:
        try:
            json.loads(text.lstrip("\ufeff"))
        except ValueError as exc:
            raise SystemExit("%s: edited text does not parse: %s" % (rel, exc))
        if not dry_run:
            path.write_bytes(text.encode("utf-8"))
    return done, skipped


def check(root, rows, path):
    problems = []
    files = sorted({r["file"] for r in rows})
    for rel in files:
        _text, _eol, objs = own_objects(root, rel)
        by_id = {iid: o for iid, _s, _e, o in objs}
        for row in rows:
            if row["file"] != rel:
                continue
            o = by_id.get(row["id"])
            if o is None:
                problems.append((row["id"], "object not found"))
                continue
            found = link_ups(o)
            if len(found) != 1:
                problems.append((row["id"], "%d link_up actions" % len(found)))
                continue
            if found[0] != row["action"]:
                problems.append((row["id"], "action differs: %s" % json.dumps(found[0], sort_keys=True)))
    # every link_up item in base data must be a row of this packet (no stray edits)
    known = {r["id"] for r in rows}
    extras = set()
    for p in sorted(Path(root).glob("data/json/items/**/*.json")):
        try:
            objs = json.loads(p.read_bytes().decode("utf-8"))
        except ValueError:
            continue
        if not isinstance(objs, list):
            continue
        for o in objs:
            if isinstance(o, dict) and link_ups(o) and o.get("id") not in known:
                extras.add((str(p.relative_to(root)).replace("\\", "/"), o.get("id")))
    return problems, sorted(extras)


def link_ups(o):
    ua = o.get("use_action")
    if ua is None:
        return []
    if isinstance(ua, dict):
        ua = [ua]
    if isinstance(ua, str):
        return []
    return [x for x in ua if isinstance(x, dict) and x.get("type") == ACTION]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="worktree root (default: the current directory)")
    ap.add_argument("--rows", default=ROWS, help="staged rows file (default: %s)" % ROWS)
    ap.add_argument("--apply", action="store_true", help="write the item files")
    ap.add_argument("--check", action="store_true", help="report rows whose action is missing or differs")
    ap.add_argument("--list", action="store_true", help="print the rows and their current state")
    ap.add_argument("--only", action="append", default=[], help="limit --apply to these files")
    a = ap.parse_args()
    root = Path(a.root).resolve()
    rows = read_rows(root, a.rows)
    if a.list:
        for row in rows:
            print("%-24s %-10d %-12s %s" % (row["id"], row["cable_length"],
                                            row["action"]["charge_rate"], row["file"]))
        print("rows: %d" % len(rows))
        return 0
    if a.apply:
        files = sorted({r["file"] for r in rows})
        if a.only:
            files = [f for f in files if f in set(a.only)]
        total = 0
        for rel in files:
            done, skipped = apply_file(root, rel, rows, dry_run=False)
            total += len(done)
            for item_id, why in skipped:
                print("skip %s: %s" % (item_id, why))
        print("applied %d of %d rows" % (total, len(rows)))
        return 0
    if a.check:
        problems, extras = check(root, rows, a.rows)
        for item_id, why in problems:
            print("PROBLEM %s: %s" % (item_id, why))
        for rel, item_id in extras:
            print("extra link_up outside the packet: %s (%s)" % (item_id, rel))
        print("check: rows=%d problems=%d extras=%d" % (len(rows), len(problems), len(extras)))
        return 0 if not problems else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
