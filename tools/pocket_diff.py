#!/usr/bin/env python3
"""Compare CDF's loaded pockets with DDA's raw JSON (PL-64 DT1).

The Catch2 case tests/pocket_dump_test.cpp writes report/pocket-dump.tsv from the pockets CDF
actually loaded (including the ones synthesized for legacy BN armor and containers).  This tool
resolves DDA's copy-from chains at a pinned revision and writes report/pocket-diff.tsv with one
row per differing field.

Usage:
    py -3 tools/pocket_diff.py
    py -3 tools/pocket_diff.py --tsv report/pocket-dump.tsv --out report/pocket-diff.tsv
    py -3 tools/pocket_diff.py --repo "C:/CDF/CDDA SRC" --rev b4b62254ae2

The DDA side is read with `git -C <repo> show <rev>:<path>`; nothing is checked out.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_REPO = r"C:\CDF\CDDA SRC"
DEFAULT_REV = "b4b62254ae2"
DEFAULT_TSV = "report/pocket-dump.tsv"
DEFAULT_OUT = "report/pocket-diff.tsv"

# The TSV columns written by tests/pocket_dump_test.cpp and the DDA JSON key each comes from.
FIELD_KEYS = {
    "pocket_type": "pocket_type",
    "flag_restriction": "flag_restriction",
    "item_restriction": "item_restriction",
    "volume_ml": "max_contains_volume",
    "max_item_volume_ml": "max_item_volume",
    "holster": "holster",
    "ripoff": "ripoff",
    "magazine_well_ml": "magazine_well",
    "ammo_restriction": "ammo_restriction",
    "rigid": "rigid",
    "watertight": "watertight",
}

DEFAULT_MAX_CONTAINS_VOLUME_ML = 200000000


def git(repo, args):
    proc = subprocess.run(
        ["git", "-C", str(repo)] + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), proc.stderr.strip()))
    return proc.stdout


def to_ml(value):
    """DDA volume strings ('9 L', '350 ml') to an integer of millilitres."""
    if value is None:
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return int(value)
    text = str(value).strip()
    match = re.match(r"^([-+]?[0-9]+(?:\.[0-9]+)?)\s*([A-Za-z\"]*)$", text)
    if not match:
        return text
    number = float(match.group(1))
    unit = match.group(2).lower().replace('"', "")
    factors = {
        "": 1,
        "ml": 1,
        "milliliter": 1,
        "milliliters": 1,
        "l": 1000,
        "liter": 1000,
        "liters": 1000,
        "kl": 1000000,
    }
    factor = factors.get(unit)
    if factor is None:
        return text
    return int(number * factor)


def text_list(value):
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    return ";".join(sorted(str(v) for v in value))


def ammo_text(value):
    if not isinstance(value, dict):
        return text_list(value)
    return ";".join("%s:%s" % (key, value[key]) for key in sorted(value))


def dda_pocket_fields(pocket):
    return {
        "pocket_type": str(pocket.get("pocket_type", "CONTAINER")),
        "flag_restriction": text_list(pocket.get("flag_restriction")),
        "item_restriction": text_list(pocket.get("item_restriction")),
        "volume_ml": str(to_ml(pocket.get("max_contains_volume", DEFAULT_MAX_CONTAINS_VOLUME_ML))),
        "max_item_volume_ml": "-" if pocket.get("max_item_volume") is None
        else str(to_ml(pocket.get("max_item_volume"))),
        "holster": "1" if pocket.get("holster", False) else "0",
        "ripoff": str(int(pocket.get("ripoff", 0))),
        "magazine_well_ml": str(to_ml(pocket.get("magazine_well", 0))),
        "ammo_restriction": ammo_text(pocket.get("ammo_restriction")),
        "rigid": "1" if pocket.get("rigid", False) else "0",
        "watertight": "1" if pocket.get("watertight", False) else "0",
    }


def definition_paths(repo, rev):
    output = git(repo, ["ls-tree", "-r", "--name-only", rev, "--", "data/json"])
    paths = []
    for path in output.splitlines():
        if not path.endswith(".json"):
            continue
        if "/items/" in path or "/artifact/" in path or "/obsoletion_and_migration_" in path:
            paths.append(path)
    return paths


def load_definitions(repo, rev, warn):
    definitions = {}
    duplicates = 0
    for path in definition_paths(repo, rev):
        try:
            text = git(repo, ["show", "%s:%s" % (rev, path)])
        except RuntimeError as error:
            warn(str(error))
            continue
        try:
            data = json.loads(text)
        except ValueError:
            warn("skipping unparsable JSON: %s" % path)
            continue
        entries = data if isinstance(data, list) else [data]
        for entry in entries:
            if not isinstance(entry, dict) or "id" not in entry:
                continue
            ident = str(entry["id"])
            if ident in definitions:
                duplicates += 1
                # Prefer a concrete definition with pockets over a stub/abstract one.
                if "pocket_data" not in entry and "pocket_data" in definitions[ident]:
                    continue
            definitions[ident] = entry
    if duplicates:
        warn("%d duplicate ids across DDA files; last definition kept" % duplicates)
    return definitions


def resolve(ident, definitions, warn, seen=None):
    """Resolve copy-from for one DDA item id.  Returns (entry, unresolved_note)."""
    seen = set() if seen is None else seen
    if ident in seen:
        return {}, "copy-from cycle at %s" % ident
    seen.add(ident)
    entry = definitions.get(ident)
    if entry is None:
        return {}, "copy-from base %s not in the scanned DDA files" % ident

    base_id = entry.get("copy-from")
    merged = {}
    note = ""
    if base_id:
        merged, note = resolve(str(base_id), definitions, warn, seen)
    for key, value in entry.items():
        if key in ("copy-from", "extend", "delete", "relative"):
            continue
        if key == "pocket_data":
            merged[key] = value
        else:
            merged[key] = value
    return merged, note


def read_tsv(path):
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as handle:
        header = handle.readline().rstrip("\r\n").split("\t")
        for line in handle:
            line = line.rstrip("\r\n")
            if not line:
                continue
            cells = line.split("\t")
            if len(cells) < len(header):
                cells += [""] * (len(header) - len(cells))
            rows.append(dict(zip(header, cells)))
    return header, rows


def main(argv=None):
    parser = argparse.ArgumentParser(description="CDF loaded pockets vs DDA raw JSON")
    parser.add_argument("--repo", default=os.environ.get("CDF_DDA_SRC", DEFAULT_REPO),
                        help="DDA git checkout (default: %(default)s)")
    parser.add_argument("--rev", default=DEFAULT_REV, help="DDA revision (default: %(default)s)")
    parser.add_argument("--tsv", default=DEFAULT_TSV,
                        help="CDF pocket dump (default: %(default)s)")
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help="difference TSV (default: %(default)s)")
    parser.add_argument("--quiet", action="store_true", help="only print the summary")
    args = parser.parse_args(argv)

    def warn(message):
        if not args.quiet:
            print("pocket_diff: " + message, file=sys.stderr)

    if not Path(args.tsv).exists():
        print("pocket_diff: missing %s; run cata_test --tags \"[.pocket_dump]\" first" % args.tsv,
              file=sys.stderr)
        return 2
    if not Path(args.repo).exists():
        print("pocket_diff: missing DDA repo %s" % args.repo, file=sys.stderr)
        return 2

    _, cdf_rows = read_tsv(args.tsv)
    by_item = {}
    for row in cdf_rows:
        by_item.setdefault(row["item"], []).append(row)

    definitions = load_definitions(args.repo, args.rev, warn)

    differences = []
    compared = 0
    missing = 0
    for item_id in sorted(by_item):
        entry = definitions.get(item_id)
        if entry is None:
            missing += 1
            differences.append((item_id, "", "item", "present", "absent",
                                "not in DDA raw JSON (CDF-only id)"))
            continue
        resolved, note = resolve(item_id, definitions, warn)
        dda_pockets = resolved.get("pocket_data") or []
        if not isinstance(dda_pockets, list):
            differences.append((item_id, "", "pocket_data", "list", type(dda_pockets).__name__,
                                "DDA pocket_data is not a list"))
            continue
        cdf_pockets = by_item[item_id]
        compared += 1
        if len(cdf_pockets) != len(dda_pockets):
            differences.append((item_id, "", "pocket_count", str(len(cdf_pockets)),
                                str(len(dda_pockets)), note or "pocket count differs"))
        for index in range(min(len(cdf_pockets), len(dda_pockets))):
            cdf = cdf_pockets[index]
            dda = dda_pocket_fields(dda_pockets[index])
            for field in FIELD_KEYS:
                if cdf.get(field, "") != dda.get(field, ""):
                    differences.append((item_id, str(index), field, cdf.get(field, ""),
                                        dda.get(field, ""), note or "value differs"))

    out_path = Path(args.out)
    if out_path.parent:
        out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="") as handle:
        handle.write("item_id\tpocket_index\tfield\tcdf\tdda\tnote\n")
        for row in differences:
            handle.write("\t".join(str(cell).replace("\t", " ") for cell in row) + "\n")

    print("pocket_diff: %d differences, %d items compared, %d CDF-only ids -> %s"
          % (len(differences), compared, missing, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
