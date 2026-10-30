#!/usr/bin/env python3
"""Generate CDF `item_overlay` data from Cataclysm: Dark Days Ahead.

CDF keeps DDA-derived item data out of the BN item files so that upstream (BN) merges stay clean.
Instead it lives in CDF-only `item_overlay` objects, which the game applies on top of the
loaded item definitions (see "item_overlay" in docs/en/mod/json/reference/items/item_creation.md).

Currently generated:

  data/json/items/cdf_overlay_lengths.json
      `longest_side` for items whose real length matters. DDA gives every item a length (by
      default the side of a cube of the item's volume). Only items that are much longer than
      that default matter for pocket length limits (DDA #40186), so an id is written when:
        - the id exists in both CDF and DDA, is not abstract, and DDA defines a `longest_side`
          (own or inherited through copy-from);
        - DDA's length is at least MIN_LENGTH_CM; and
        - DDA's length is more than RATIO times the length CDF would derive from the volume of
          one unit of the item (round(cbrt(volume in ml)) cm).
      Items that CDF defines a `longest_side` for itself are skipped (explicit data wins anyway).

  data/json/items/cdf_overlay_garment_pockets.json   (with --pockets)
      The DDA pocket layout, verbatim, of every CDF garment (ARMOR/TOOL_ARMOR with `storage`)
      whose id exists in DDA with at least one CONTAINER pocket in its `pocket_data`. Each pocket
      is copied as DDA defines it (no scaling), including `extra_encumbrance` (a filled pocket adds
      that much encumbrance). `ripoff` and `activity_noise` are not loaded from overlays; each
      dropped value is kept in a "//" comment on the pocket so it can
      be switched on later without regenerating. Ablative pockets are skipped (they belong to
      the ablative armor work), as are non-CONTAINER pockets (magazine wells etc.: CDF builds
      those from BN `magazines`). A per-garment report is printed.

DDA now uses one "ITEM" type (with "subtypes"); BN keeps one type per kind of item. Both are read.

Run from anywhere:

  python tools/cdf_port_dda_overlays.py --dda "<DDA SRC>" [--cdf <CDF SRC>] [--check]

`--check` compares the output with the committed file and exits 1 if they differ (no write).
Refresh the table after updating the DDA mirror by running the script and committing the result.
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path

MIN_LENGTH_CM = 40
RATIO = 1.25

ITEM_TYPES = {
    "GENERIC", "GUN", "TOOL", "AMMO", "ARMOR", "WHEEL", "COMESTIBLE", "CONTAINER", "MAGAZINE",
    "GUNMOD", "TOOL_ARMOR", "BOOK", "BIONIC_ITEM", "ENGINE", "PET_ARMOR", "BATTERY", "TOOLMOD",
    "ITEM",  # DDA unified the item types into "ITEM" with "subtypes"
}

LENGTH_UNITS = {"mm": 1, "cm": 10, "km": 1000000, "meter": 1000, "m": 1000}
LENGTH_RE = re.compile(r"\s*([+-]?\d+)\s*(mm|cm|km|meter|m)\b")


def parse_length_mm(value):
    """Parses '84 cm' / '1 meter 20 cm' to millimeters. Returns None if it is not a length."""
    if not isinstance(value, str):
        return None
    pos = 0
    total = 0
    value = value.strip()
    while pos < len(value):
        m = LENGTH_RE.match(value, pos)
        if not m:
            return None
        total += int(m.group(1)) * LENGTH_UNITS[m.group(2)]
        pos = m.end()
    return total if value else None


def parse_volume_ml(value):
    """Parses '2 L' / '250 ml' (or a legacy integer, 250 ml per unit) to milliliters."""
    if isinstance(value, int):
        return value * 250
    if not isinstance(value, str):
        return None
    total = 0
    pos = 0
    for m in re.finditer(r"\s*([+-]?\d+(?:\.\d+)?)\s*(ml|L)\b", value):
        if m.start() != pos:
            return None
        total += float(m.group(1)) * (1000 if m.group(2) == "L" else 1)
        pos = m.end()
    return total if pos == len(value) and pos else None


def load_items(root):
    """All item objects below root/data/json, keyed by id (abstracts by their 'abstract' name)."""
    items = {}
    abstracts = {}
    for path in sorted((root / "data" / "json").rglob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (ValueError, OSError):
            continue
        if isinstance(data, dict):
            data = [data]
        if not isinstance(data, list):
            continue
        for obj in data:
            if not isinstance(obj, dict) or obj.get("type") not in ITEM_TYPES:
                continue
            if "abstract" in obj:
                abstracts[obj["abstract"]] = obj
            elif "id" in obj:
                items[obj["id"]] = obj
    return items, abstracts


def resolve(obj, items, abstracts, field, seen=None):
    """Value of a field after copy-from inheritance, or None."""
    seen = seen or set()
    while obj is not None:
        if field in obj:
            return obj[field]
        parent = obj.get("copy-from")
        if parent is None or parent in seen:
            return None
        seen.add(parent)
        obj = items.get(parent) or abstracts.get(parent)
    return None


def derived_length_mm(obj, items, abstracts):
    """The length the game derives from volume when no longest_side is given."""
    volume = parse_volume_ml(resolve(obj, items, abstracts, "volume"))
    if volume is None:
        return 0
    stack = resolve(obj, items, abstracts, "stack_size")
    if obj.get("type") in ("AMMO", "COMESTIBLE") and isinstance(stack, int) and stack > 0:
        volume /= stack
    return int(round(volume ** (1.0 / 3.0))) * 10 if volume > 0 else 0


def generate(cdf_root, dda_root):
    cdf, cdf_abs = load_items(cdf_root)
    dda, dda_abs = load_items(dda_root)
    rows = []
    for item_id in sorted(cdf):
        if item_id not in dda:
            continue
        if resolve(cdf[item_id], cdf, cdf_abs, "longest_side") is not None:
            continue
        dda_len = parse_length_mm(resolve(dda[item_id], dda, dda_abs, "longest_side"))
        if dda_len is None or dda_len < MIN_LENGTH_CM * 10:
            continue
        if dda_len <= RATIO * derived_length_mm(cdf[item_id], cdf, cdf_abs):
            continue
        rows.append({"type": "item_overlay", "id": item_id, "longest_side": format_length(dda_len)})
    return rows


POCKET_KEYS = [
    "pocket_type", "max_contains_volume", "max_contains_weight", "max_item_volume",
    "min_item_volume", "max_item_length", "min_item_length", "holster", "flag_restriction",
    "item_restriction", "rigid", "moves", "transparent", "magazine_well", "watertight",
    "airtight", "open_container", "volume_encumber_modifier", "spoil_multiplier",
    "weight_multiplier", "volume_multiplier",
    "extra_encumbrance",  # CDF: ENC1, read by pocket_data.cpp and item::get_encumber
]
DROPPED_KEYS = ["ripoff", "activity_noise"]
GARMENT_TYPES = {"ARMOR", "TOOL_ARMOR"}


def is_garment(obj, items, abstracts):
    if obj.get("type") in GARMENT_TYPES:
        return True
    subtypes = resolve(obj, items, abstracts, "subtypes") or []
    return obj.get("type") == "ITEM" and bool(set(subtypes) & GARMENT_TYPES)


def pocket_volume_ml(pocket):
    value = parse_volume_ml(pocket.get("max_contains_volume"))
    return value if value is not None else 0


def generate_pockets(cdf_root, dda_root):
    """Returns (rows, report) for the garment pocket overlay."""
    cdf, cdf_abs = load_items(cdf_root)
    dda, dda_abs = load_items(dda_root)
    rows = []
    report = []
    for item_id in sorted(cdf):
        obj = cdf[item_id]
        if item_id not in dda or obj.get("type") not in GARMENT_TYPES:
            continue
        storage = parse_volume_ml(resolve(obj, cdf, cdf_abs, "storage"))
        if not storage:
            continue
        layout = resolve(dda[item_id], dda, dda_abs, "pocket_data")
        if not isinstance(layout, list):
            continue
        pockets = []
        for pocket in layout:
            if pocket.get("pocket_type", "CONTAINER") != "CONTAINER" or pocket.get("ablative"):
                continue
            out = {k: pocket[k] for k in POCKET_KEYS if k in pocket}
            dropped = {k: pocket[k] for k in DROPPED_KEYS if k in pocket}
            if dropped:
                out["//"] = "DDA values not loaded by CDF: " + json.dumps(dropped)
            pockets.append(out)
        if not pockets:
            continue
        rows.append({"type": "item_overlay", "id": item_id, "pocket_data": pockets})
        main = sum(pocket_volume_ml(p) for p in pockets if not p.get("holster"))
        total = sum(pocket_volume_ml(p) for p in pockets)
        report.append((item_id, storage, main, total))
    return rows, report


def format_length(mm):
    if mm % 10 == 0:
        return "%d cm" % (mm // 10)
    return "%d mm" % mm


def render_pockets(rows):
    # One pocket per line keeps diffs reviewable.
    lines = ["["]
    for i, row in enumerate(rows):
        lines.append('  {"type": "item_overlay", "id": %s, "pocket_data": [' % json.dumps(row["id"]))
        for j, pocket in enumerate(row["pocket_data"]):
            lines.append("    " + json.dumps(pocket) + ("," if j + 1 < len(row["pocket_data"]) else ""))
        lines.append("  ]}" + ("," if i + 1 < len(rows) else ""))
    lines.append("]")
    return "\n".join(lines) + "\n"


def print_report(report):
    print("%-34s %9s %9s %9s %6s" % ("id", "BN L", "DDA L", "+straps", "ratio"))
    for item_id, storage, main, total in report:
        print("%-34s %9.2f %9.2f %9.2f %6.2f" % (item_id, storage / 1000, main / 1000, total / 1000,
                                                total / storage))
    print("\nGarments that lose capacity (DDA total below 0.9x BN storage):")
    for item_id, storage, main, total in report:
        if total < 0.9 * storage:
            print("  %s: %.2f L -> %.2f L" % (item_id, storage / 1000, total / 1000))


def render(rows):
    # One object per line keeps diffs reviewable; json_formatter accepts this layout.
    lines = ["[", ]
    for i, row in enumerate(rows):
        lines.append("  " + json.dumps(row) + ("," if i + 1 < len(rows) else ""))
    lines.append("]")
    return "\n".join(lines) + "\n"


def main():
    here = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--cdf", type=Path, default=here, help="CDF source root (default: this repo)")
    parser.add_argument("--dda", type=Path, required=True, help="Cataclysm: DDA source root")
    parser.add_argument("--out", type=Path, help="output file (default: the committed overlay)")
    parser.add_argument("--pockets", action="store_true",
                        help="generate the garment pocket overlay instead of the length overlay")
    parser.add_argument("--check", action="store_true", help="compare with the output file only")
    args = parser.parse_args()

    if args.pockets:
        out = args.out or args.cdf / "data" / "json" / "items" / "cdf_overlay_garment_pockets.json"
        rows, report = generate_pockets(args.cdf, args.dda)
        text = render_pockets(rows)
        print_report(report)
        print("%d garments, %d pockets" % (len(rows), sum(len(r["pocket_data"]) for r in rows)))
    else:
        out = args.out or args.cdf / "data" / "json" / "items" / "cdf_overlay_lengths.json"
        text = render(generate(args.cdf, args.dda))
    if args.check:
        same = out.exists() and out.read_text(encoding="utf-8") == text
        print("up to date" if same else "OUT OF DATE: rerun without --check")
        return 0 if same else 1
    out.write_text(text, encoding="utf-8", newline="\n")
    print("wrote %d overlays to %s" % (text.count('"item_overlay"'), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
