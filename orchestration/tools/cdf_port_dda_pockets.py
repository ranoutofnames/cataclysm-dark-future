#!/usr/bin/env python3
"""P1f data port: DDA pocket_data onto CDF wearable storage.

// CDF: plan orchestration/plans/P1f/PLAN.md section 3.1 (rules R1-R9) and
// ITEMS.md sections A (holsters), B (bandoliers), C (container_data), E (flags).
// CDF: DDA behaviour and data are the reference (per DDA). Ported pockets are
// DDA's pocket_data verbatim, except R2 (carry:true for overlay garments) and
// R7 (ammo types CDF does not define are dropped and noted in a "//" key).
// CDF: AUTHOR rows (no DDA twin) are translated mechanically from BN fields (R3).

Modes:
  --report              print the plan per item id (no writes)
  --apply [--files F..] edit data/json in the worktree (item files, overlay,
                        obsoletion, flags.json); then json_formatter on changed files
  --check               verify the data against the plan (exit 1 on any error)

Run from the worktree (the script finds the worktree root two levels up):
  python -I orchestration/tools/cdf_port_dda_pockets.py --report
"""

import argparse
import copy
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_DDA = Path(r"D:\Library\Games\PC Games\Cataclysm\CDDA\CDDA SRC")
DEFAULT_ITEMS = Path(r"D:\Library\Games\PC Games\Cataclysm\CDF\Dark-Future-Dev-Docs"
                     r"\orchestration\plans\P1f\ITEMS.md")
DEFAULT_FORMATTER = Path("out/build/win-tiles/tools/format/json_formatter.exe")

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "data" / "json"
OVERLAY = DATA / "items" / "cdf_overlay_garment_pockets.json"
OBSOLETE = DATA / "obsoletion" / "items.json"
CDF_FLAGS = DATA / "flags.json"

HOLSTER_ACTIONS = {"holster", "bandolier"}
THIN_HOLSTER_KEYS = ("holster_prompt", "holster_msg")
# CDF: keys a ported pocket may carry: DDA pocket keys plus the BN-authored ones.
ALLOWED_POCKET_KEYS = {
    "pocket_type", "holster", "ammo_restriction", "flag_restriction", "item_restriction",
    "carry", "max_contains_volume", "max_contains_weight", "max_item_length", "min_item_length",
    "max_item_volume", "min_item_volume", "moves", "ripoff", "extra_encumbrance",
    "activity_noise", "magazine_well", "transparent", "description", "rigid",
    "max_item_weight", "min_item_weight", "min_contains_volume", "min_contains_weight",
    "spoil_multiplier", "watertight", "open_container", "sealed_data", "sealed_by_type",
    "volume_encumber_modifier", "weight_encumber_modifier", "favorite_settings",
    "flag_restriction", "fire_protection", "forbidden_flags", "item_restriction",
    "max_item_count", "pocket_restriction", "magazine_well", "ammo_restriction",
}
# CDF: R3 sibling table. CDF-only holster -> DDA sibling whose first pocket gives max_item_length.
SIBLINGS = {
    "mag_satchel_leg": "legpouch_large",
    "shoulder_pouch": "chestpouch",
    "belt_magazine": "chestrig",
    "bandolier_knife": "leg_sheath6",
    "wristholster": "bootstrap",
    "wristsheath": "bootsheath",
    "survivor_hiking_backpack": "backpack_hiking",
    "power_armor_back_hardpoint": "back_holster",
    "power_armor_leg_hardpoint": "holster",
    "power_armor_chest_rig": "chestrig",
}
# CDF: R3 literal lengths where the plan states them (power_armor_leg_hardpoint is 30 cm).
LITERAL_LENGTH = {"power_armor_leg_hardpoint": "30 cm", "wristholster": "20 cm"}


def load_json(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def as_list(value):
    if value is None:
        return []
    if isinstance(value, list):
        return list(value)
    return [value]


def is_holster_action(entry):
    return isinstance(entry, dict) and entry.get("type") in HOLSTER_ACTIONS


def parse_items_md(path):
    """Return (rows, flag_rows). Rows come from sections A, B, C; flag_rows from section E."""
    rows, flag_rows = [], []
    section, header = None, None
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("## "):
            section, header = line[3:4], None
            continue
        if not line.startswith("|"):
            header = None
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if all(re.fullmatch(r":?-+:?", c) for c in cells):
            continue
        if header is None:
            header = cells
            continue
        row = dict(zip(header, cells))
        if section in ("A", "B", "C"):
            item_id = row.get("id", "").strip("`")
            dda_match = re.match(r"`([^`]+)`\s*:\s*(\S+)", row.get("DDA id : file", ""))
            action = re.match(r"([A-Za-z]+)", row.get("port action", "none"))
            rows.append({
                "section": section,
                "id": item_id,
                "cdf_file": row.get("CDF file", ""),
                "dda_id": dda_match.group(1) if dda_match else None,
                "dda_file": dda_match.group(2) if dda_match else None,
                "action": action.group(1).upper() if action else "NONE",
                "text": row.get("port action", ""),
            })
        elif section == "E" and "flag" in row:
            ids = re.findall(r"`([^`]+)`", row.get("CDF items that lack it (add)", ""))
            flag_rows.append({"flag": row["flag"].strip("*"), "ids": ids})
    return rows, flag_rows


class Ctx:
    """Indexes DDA and CDF data once and keeps the loaded CDF documents for writing."""

    def __init__(self, dda_root):
        self.dda_index = {}
        for path in (Path(dda_root) / "data" / "json").rglob("*.json"):
            try:
                doc = load_json(path)
            except (ValueError, OSError):
                continue
            for obj in doc if isinstance(doc, list) else []:
                if isinstance(obj, dict) and isinstance(obj.get("id"), str):
                    self.dda_index.setdefault(obj["id"], []).append(
                        (str(path.relative_to(Path(dda_root) / "data" / "json")).replace("\\", "/"), obj))
        self.docs = {}
        self.cdf_index = {}
        for path in DATA.rglob("*.json"):
            try:
                doc = load_json(path)
            except ValueError:
                continue
            self.docs[path] = doc
            for obj in doc if isinstance(doc, list) else []:
                if isinstance(obj, dict) and isinstance(obj.get("id"), str) and obj.get("type") != "item_overlay":
                    self.cdf_index.setdefault(obj["id"], []).append((path, obj))
        self.ammo_types = {o["id"] for lst in self.cdf_index.values() for _, o in lst
                           if o.get("type") == "ammunition_type"}
        self.json_flags = {o["id"] for lst in self.cdf_index.values() for _, o in lst
                           if o.get("type") == "json_flag"}

    def dda_obj(self, item_id, dda_file):
        cands = self.dda_index.get(item_id, [])
        for rel, obj in cands:
            if dda_file and rel.endswith(dda_file):
                return obj
        return cands[0][1] if cands else None

    def cdf_obj(self, item_id, cdf_file=None):
        # CDF: item objects only. Their types are upper case (ARMOR, GENERIC, ...); item_action,
        # json_flag, ammunition_type and item_overlay share ids with items and are not items.
        cands = [(p, o) for p, o in self.cdf_index.get(item_id, [])
                 if str(o.get("type", "")).isupper()]
        for path, obj in cands:
            if cdf_file and str(path).replace("\\", "/").endswith(cdf_file):
                return path, obj
        return cands[0] if cands else (None, None)


def effective(index, obj, key, seen=0):
    """Field value after copy-from resolution (child keys override parent keys)."""
    if obj is None or seen > 20:
        return None
    if key in obj:
        return obj[key]
    parent = obj.get("copy-from")
    if not parent:
        return None
    cands = index.get(parent, [])
    if not cands:
        return None
    parent_obj = cands[0][1] if isinstance(cands[0], tuple) else cands[0]
    return effective(index, parent_obj, key, seen + 1)


def dda_holster_thin(dda_obj_eff_use):
    for entry in as_list(dda_obj_eff_use):
        if is_holster_action(entry):
            thin = {"type": "holster"}
            for key in THIN_HOLSTER_KEYS:
                if key in entry:
                    thin[key] = entry[key]
            return thin
    return None


def filter_ammo(ctx, pockets, notes):
    for pocket in pockets:
        ar = pocket.get("ammo_restriction")
        if not isinstance(ar, dict):
            continue
        keep = {k: v for k, v in ar.items() if k in ctx.ammo_types}
        dropped = sorted(k for k in ar if k not in ctx.ammo_types)
        if dropped:
            pocket["ammo_restriction"] = keep
            note = "CDF: ammo types not defined in CDF dropped: " + ", ".join(dropped)
            pocket["//"] = (pocket["//"] + "; " + note) if pocket.get("//") else note
            notes.append("dropped " + ",".join(dropped))
    return pockets


def copy_pockets(ctx, row, overlay_ids, notes):
    """R1/R2/R7/R9: DDA pocket_data verbatim, with overlay carry and ammo filter."""
    dda = ctx.dda_obj(row["dda_id"], row["dda_file"])
    pockets = effective(ctx.dda_index, dda, "pocket_data")
    if pockets is None:
        return None
    pockets = filter_ammo(ctx, copy.deepcopy(pockets), notes)
    if row["id"] in overlay_ids:
        for pocket in pockets:
            pocket["carry"] = True
        notes.append("R2 overlay carry:true")
    return pockets


def author_pockets(ctx, row, bn_use, bn_storage, notes):
    """R3: translate a BN holster or bandolier use_action into pockets."""
    pockets = []
    if bn_storage:
        pockets.append({"pocket_type": "CONTAINER", "max_contains_volume": bn_storage})
        notes.append("R3 kept worn storage " + bn_storage)
    if bn_use.get("type") == "bandolier":
        ammo = {a: bn_use.get("capacity") for a in bn_use.get("ammo", [])}
        pocket = {"ammo_restriction": ammo, "moves": bn_use.get("draw_cost")}
        pockets.append(pocket)
        return filter_ammo(ctx, pockets, notes)
    flags = [f for f in bn_use.get("flags", []) if f != "HOLSTER_PISTOL"]
    skills = bn_use.get("skills", [])
    length = LITERAL_LENGTH.get(row["id"])
    if length is None and row["id"] in SIBLINGS:
        sib = ctx.dda_obj(SIBLINGS[row["id"]], None)
        sib_pockets = effective(ctx.dda_index, sib, "pocket_data") or []
        length = sib_pockets[0].get("max_item_length") if sib_pockets else None
    for _ in range(int(bn_use.get("multi", 1))):
        pocket = {"holster": True}
        if "max_volume" in bn_use:
            pocket["max_contains_volume"] = bn_use["max_volume"]
            pocket["max_item_volume"] = bn_use["max_volume"]
        if "min_volume" in bn_use:
            pocket["min_item_volume"] = bn_use["min_volume"]
        if "max_weight" in bn_use:
            pocket["max_contains_weight"] = bn_use["max_weight"]
        if length:
            pocket["max_item_length"] = length
        if flags and not (skills and not flags):
            pocket["flag_restriction"] = flags
        if "draw_cost" in bn_use:
            pocket["moves"] = bn_use["draw_cost"]
        pockets.append(pocket)
    return pockets


def plan_row(ctx, row, overlay_ids):
    """Return a dict describing what apply does for one ITEMS.md row."""
    path, obj = ctx.cdf_obj(row["id"], row["cdf_file"])
    result = {"id": row["id"], "action": row["action"], "file": row["cdf_file"],
              "path": path, "obj": obj, "pockets": None, "notes": [], "thin": None,
              "use_action_after": None, "strip_text": False, "error": None,
              "dda_id": row["dda_id"], "dda_file": row["dda_file"]}
    if obj is None:
        result["error"] = "id not found in CDF data/json"
        return result
    use = effective(ctx.cdf_index, obj, "use_action")
    storage = effective(ctx.cdf_index, obj, "storage")
    notes = result["notes"]
    if row["action"] == "COPY":
        if row["dda_id"] is None:
            result["error"] = "COPY row without DDA id"
            return result
        result["pockets"] = copy_pockets(ctx, row, overlay_ids, notes)
        dda = ctx.dda_obj(row["dda_id"], row["dda_file"])
        result["thin"] = dda_holster_thin(effective(ctx.dda_index, dda, "use_action"))
        if result["pockets"] is None:
            result["error"] = "DDA item has no pocket_data"
    elif row["action"] == "AUTHOR":
        bn = next((a for a in as_list(use) if is_holster_action(a)), None)
        if bn is None:
            result["error"] = "AUTHOR row without BN holster/bandolier action"
            return result
        result["pockets"] = author_pockets(ctx, row, bn, storage, notes)
        if bn.get("type") == "holster":
            result["thin"] = {"type": "holster", **{k: bn[k] for k in THIN_HOLSTER_KEYS if k in bn}}
    elif row["action"] == "OBSOLETE":
        result["strip_text"] = True
    # R1 use_action: holster or bandolier entries are replaced by the DDA thin holster (or removed).
    if row["action"] in ("COPY", "AUTHOR", "OBSOLETE"):
        base = as_list(use)
        kept = [a for a in base if not is_holster_action(a)]
        new = kept + ([result["thin"]] if result["thin"] else [])
        result["use_action_after"] = new
        if row["action"] != "OBSOLETE" and not result["thin"]:
            result["strip_text"] = True
    return result


def set_use_action(obj, eff_before, new):
    """Write the use_action list; keeps single-entry dict form when possible."""
    if new == eff_before:
        return False
    if not new:
        if "copy-from" in obj:
            obj["use_action"] = []
        else:
            obj.pop("use_action", None)
    elif len(new) == 1 and isinstance(new[0], dict):
        obj["use_action"] = new[0]
    else:
        obj["use_action"] = new
    return True


def strip_activate(text):
    return re.sub(r"\s*Activate to [^.]*\.", "", text)


def apply_plan(ctx, plan, overlay_ids, changed_files):
    obj = plan["obj"]
    if plan["error"] or obj is None:
        return
    if plan["action"] in ("COPY", "AUTHOR"):
        obj["pocket_data"] = plan["pockets"]
        obj.pop("container_data", None)
        if plan["action"] == "COPY":
            dda = ctx.dda_obj(plan["dda_id"], plan["dda_file"])
            if effective(ctx.dda_index, dda, "storage") is None:
                obj.pop("storage", None)
        else:
            obj.pop("storage", None)
    if plan["action"] == "COPY" or plan["action"] == "AUTHOR" or plan["action"] == "OBSOLETE":
        eff_before = as_list(effective(ctx.cdf_index, obj, "use_action"))
        set_use_action(obj, eff_before, plan["use_action_after"])
    if plan["strip_text"] and isinstance(obj.get("description"), str):
        obj["description"] = strip_activate(obj["description"])
    changed_files.add(plan["path"])


def apply_overlay(ids):
    """R2: drop overlay entries for ported ids by text, so the rest of the file stays byte-identical.

    Entries are top-level blocks: the opening line starts with two spaces and the closing
    line is "  ]}" or "  }", each optionally followed by a comma.
    """
    lines = OVERLAY.read_text(encoding="utf-8").split(chr(10))
    out, removed, skipping = [], 0, False
    for line in lines:
        if not skipping and any(line.startswith('  {"type": "item_overlay", "id": "%s",' % i) for i in ids):
            skipping, removed = True, removed + 1
            continue
        if skipping:
            if re.match(r"^  \]?\}", line):
                skipping = False
            continue
        out.append(line)
    if removed:
        OVERLAY.write_text(chr(10).join(out), encoding="utf-8", newline=chr(10))
    return removed


def apply_flags(ctx, flag_rows, changed_files):
    for entry in flag_rows:
        for item_id in entry["ids"]:
            path, obj = ctx.cdf_obj(item_id)
            if obj is None:
                continue
            base = as_list(effective(ctx.cdf_index, obj, "flags"))
            if entry["flag"] in base:
                continue
            obj["flags"] = base + [entry["flag"]]
            changed_files.add(path)
    dda_flags = ctx.dda_index.get("SHEATH_BOW", [])
    if "SHEATH_BOW" not in ctx.json_flags and dda_flags:
        doc = ctx.docs[CDF_FLAGS]
        doc.append(copy.deepcopy(dda_flags[0][1]))
        changed_files.add(CDF_FLAGS)
        ctx.json_flags.add("SHEATH_BOW")


def run_check(ctx, rows, flag_rows, overlay_ids):
    errors = []
    dda_keys = set(ALLOWED_POCKET_KEYS)
    for row in rows:
        if row["action"] in ("COPY", "AUTHOR"):
            _, obj = ctx.cdf_obj(row["id"], row["cdf_file"])
            if obj is None:
                errors.append(row["id"] + ": missing")
                continue
            if "pocket_data" not in obj:
                errors.append(row["id"] + ": no pocket_data")
            if "container_data" in obj:
                errors.append(row["id"] + ": container_data still present")
            for entry in as_list(obj.get("use_action")):
                if is_holster_action(entry) and set(entry) - {"type", *THIN_HOLSTER_KEYS}:
                    errors.append(row["id"] + ": BN holster/bandolier fields still present")
                if is_holster_action(entry) and entry.get("type") == "bandolier":
                    errors.append(row["id"] + ": bandolier action still present")
            for pocket in obj.get("pocket_data", []):
                for key in pocket:
                    if key.startswith("//"):
                        continue
                    if key not in dda_keys:
                        errors.append(row["id"] + ": unknown pocket key " + key)
                for flag in pocket.get("flag_restriction", []):
                    if flag not in ctx.json_flags:
                        errors.append(row["id"] + ": flag " + flag + " not defined")
                for ammo in pocket.get("ammo_restriction", {}):
                    if ammo not in ctx.ammo_types:
                        errors.append(row["id"] + ": ammo " + ammo + " not defined")
            if row["id"] in overlay_ids:
                errors.append(row["id"] + ": overlay entry still present")
        elif row["action"] == "INHERIT":
            _, obj = ctx.cdf_obj(row["id"], row["cdf_file"])
            if obj is not None and ("pocket_data" in obj or "container_data" in obj):
                errors.append(row["id"] + ": INHERIT child declares its own pocket_data")
    for entry in flag_rows:
        for item_id in entry["ids"]:
            _, obj = ctx.cdf_obj(item_id)
            if obj is not None and entry["flag"] not in as_list(effective(ctx.cdf_index, obj, "flags")):
                errors.append(item_id + ": flag " + entry["flag"] + " still missing")
    if "SHEATH_BOW" not in ctx.json_flags:
        errors.append("SHEATH_BOW not defined in CDF")
    return errors


def format_json(formatter, paths):
    """Run BN's json_formatter on the changed files only (in place)."""
    status = 0
    for path in paths:
        status |= subprocess.run([str(formatter), str(path)], check=False).returncode
    return status


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--report", action="store_true")
    mode.add_argument("--apply", action="store_true")
    mode.add_argument("--check", action="store_true")
    parser.add_argument("--dda", default=str(DEFAULT_DDA))
    parser.add_argument("--items", default=str(DEFAULT_ITEMS))
    parser.add_argument("--files", nargs="*", default=None,
                        help="limit --apply to these CDF file basenames (e.g. ammo_pouch.json)")
    parser.add_argument("--formatter", default=str(DEFAULT_FORMATTER),
                        help="json_formatter binary (build-scripts/format-json.sh builds a fresh one)")
    args = parser.parse_args(argv)

    rows, flag_rows = parse_items_md(Path(args.items))
    ctx = Ctx(args.dda)
    overlay_ids = {o["id"] for o in ctx.docs[OVERLAY] if isinstance(o, dict) and "id" in o}
    if args.files:
        rows = [r for r in rows if Path(r["cdf_file"]).name in args.files]

    if args.report:
        print("id | action | pockets | ammo dropped | thin holster | strip text | notes")
        for row in rows:
            plan = plan_row(ctx, row, overlay_ids)
            count = len(plan["pockets"]) if plan["pockets"] else 0
            print(" | ".join([plan["id"], plan["action"], str(count),
                              "; ".join(n for n in plan["notes"] if n.startswith("dropped")) or "-",
                              "yes" if plan["thin"] else "no",
                              "yes" if plan["strip_text"] else "no",
                              (plan["error"] or "; ".join(plan["notes"])) or "-"]))
        print("ammo types defined in CDF:", len(ctx.ammo_types), "| json flags defined:", len(ctx.json_flags))
        return 0

    if args.check:
        errors = run_check(ctx, rows, flag_rows, overlay_ids)
        for err in errors:
            print("ERROR", err)
        print("check:", "ok" if not errors else f"{len(errors)} error(s)")
        return 1 if errors else 0

    changed = set()
    for row in rows:
        plan = plan_row(ctx, row, overlay_ids)
        if plan["error"]:
            print("SKIP", plan["id"], plan["error"])
            continue
        apply_plan(ctx, plan, overlay_ids, changed)
    if not args.files:
        removed = apply_overlay({r["id"] for r in rows if r["action"] == "COPY"})
        print("overlay entries removed:", removed)
        apply_flags(ctx, flag_rows, changed)
    for path in sorted(changed):
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(ctx.docs[path], fh, indent=2, ensure_ascii=False)
            fh.write("\n")
    print("changed files:", len(changed))
    return format_json(ROOT / args.formatter, sorted(changed))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
