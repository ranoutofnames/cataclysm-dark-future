#!/usr/bin/env python3
"""P1f K3: convert in-tree mod items from BN holster/bandolier use_actions to DDA pockets.

// CDF: companion of cdf_port_dda_pockets.py (plan P1f section 3.2 G8 and ITEMS.md G, H).
// COPY   : mod item with a DDA twin (Magiclysm, Aftershock): DDA pocket_data verbatim (R1/R7).
// AUTHOR : mod item without a DDA twin: BN fields translated to pockets (R3).
// OVERRIDE: mod redefinition of a core bandolier id: the core ported pocket gets the mod's extra
//           ammo types at the override's capacity.
// REMOVE : obsolete shell, the BN action is dropped.

Run from the worktree root:  python -I orchestration/tools/cdf_port_dda_mod_pockets.py [--apply]
"""

import importlib.util
import json
import copy
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("pockets", HERE / "cdf_port_dda_pockets.py")
pockets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pockets)

ROOT = pockets.ROOT
DDA = pockets.DEFAULT_DDA
MODS = ROOT / "data" / "mods"

# id -> (action, dda id)
COPY = {
    "afs_herc_rig": "afs_herc_rig", "mbelt_technomancer_toolbelt": "mbelt_technomancer_toolbelt",
    "belt_str": "belt_str", "mbelt_haste": "mbelt_haste", "mbelt_pockets_greater": "mbelt_pockets_greater",
    "mbelt_pockets_lesser": "mbelt_pockets_lesser", "mbelt_thor": "mbelt_thor",
    "mbelt_weaponry": "mbelt_weaponry", "mbelt_leather": "mbelt_leather",
}
AUTHOR = {"afs_boot_quiver", "mana_gem", "mana_gem_blood", "mana_gem_dull", "mana_gem_pure",
          "satchel_eternity", "crt_backpack", "crt_chestrig", "crt_warmor"}
REMOVE = {"afs_mbr_titanium"}
OVERRIDE = {"quiver", "quiver_large", "quiver_birchbark", "quiver_large_birchbark", "bandolier_pistol",
            "ammo_pouch", "ammo_satchel_leg", "bandolier_rifle", "bandolier_wrist"}
# CDF: R3 length siblings for mod items (nearest DDA twin of the item they copy or resemble).
MOD_SIBLING = {"crt_backpack": "molle_pack", "crt_chestrig": "chestrig"}


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def build_ctx():
    ctx = pockets.Ctx(DDA)
    # CDF: extend the DDA index with DDA mods (abstracts included) and the CDF index with mods.
    for path in (DDA / "data" / "mods").rglob("*.json"):
        try:
            doc = load(path)
        except (ValueError, OSError):
            continue
        for obj in doc if isinstance(doc, list) else []:
            if not isinstance(obj, dict):
                continue
            key = obj.get("id") if isinstance(obj.get("id"), str) else obj.get("abstract")
            if isinstance(key, str):
                ctx.dda_index.setdefault(key, []).append((str(path), obj))
    for path in (DDA / "data" / "json").rglob("*.json"):
        try:
            doc = load(path)
        except (ValueError, OSError):
            continue
        for obj in doc if isinstance(doc, list) else []:
            if isinstance(obj, dict) and isinstance(obj.get("abstract"), str):
                ctx.dda_index.setdefault(obj["abstract"], []).append((str(path), obj))
    docs = {}
    for path in MODS.rglob("*.json"):
        try:
            doc = load(path)
        except ValueError:
            continue
        docs[path] = doc
        for obj in doc if isinstance(doc, list) else []:
            if not isinstance(obj, dict):
                continue
            key = obj.get("id") if isinstance(obj.get("id"), str) else obj.get("abstract")
            if isinstance(key, str) and obj.get("type") != "item_overlay":
                ctx.cdf_index.setdefault(key, []).append((path, obj))
                if isinstance(obj.get("id"), str) and obj.get("type") == "ammunition_type":
                    ctx.ammo_types.add(obj["id"])
                if obj.get("type") == "json_flag":
                    ctx.json_flags.add(obj["id"])
    return ctx, docs


def plan(ctx, docs):
    results = []
    for path, doc in docs.items():
        for obj in doc if isinstance(doc, list) else []:
            if not isinstance(obj, dict):
                continue
            key = obj.get("id") if isinstance(obj.get("id"), str) else obj.get("abstract")
            if key is None:
                continue
            uses = [a for a in pockets.as_list(obj.get("use_action")) if pockets.is_holster_action(a)]
            if not uses and not (key in COPY and "pocket_data" not in obj):
                continue
            if "pocket_data" in obj and key not in OVERRIDE:
                continue
            if key in COPY:
                kind = "COPY"
            elif key in AUTHOR:
                kind = "AUTHOR"
            elif key in REMOVE:
                kind = "REMOVE"
            elif key in OVERRIDE:
                kind = "OVERRIDE"
            else:
                kind = "UNKNOWN"
            results.append((path, obj, key, kind, uses[0] if uses else None))
    return results


def core_item(ctx, item_id):
    for path, obj in ctx.cdf_index.get(item_id, []):
        if str(path).replace("\\", "/").find("/data/json/") >= 0 and str(obj.get("type", "")).isupper():
            return obj
    return None


def mod_pockets(ctx, key, kind, obj, use):
    notes = []
    if kind == "COPY":
        row = {"id": key, "dda_id": COPY[key], "dda_file": None}
        pk = pockets.copy_pockets(ctx, row, set(), notes)
        dda = ctx.dda_obj(COPY[key], None)
        thin = pockets.dda_holster_thin(pockets.effective(ctx.dda_index, dda, "use_action"))
        return pk, thin, True
    if kind == "AUTHOR":
        row = {"id": key, "dda_id": None, "dda_file": None}
        storage = pockets.effective(ctx.cdf_index, obj, "storage")
        if key in MOD_SIBLING:
            pockets.SIBLINGS[key] = MOD_SIBLING[key]
        pk = pockets.author_pockets(ctx, row, use, storage, notes)
        thin = None
        if use.get("type") == "holster":
            thin = {"type": "holster", **{k: use[k] for k in pockets.THIN_HOLSTER_KEYS if k in use}}
        return pk, thin, False
    if kind == "OVERRIDE":
        core = core_item(ctx, key)
        base = copy.deepcopy(pockets.effective(ctx.cdf_index, core, "pocket_data") or [])
        cap = use["capacity"]
        out = []
        for p in base:
            if "ammo_restriction" in p:
                ar = dict(p["ammo_restriction"])
                for a in use["ammo"]:
                    if a in ctx.ammo_types:
                        ar[a] = max(cap, ar.get(a, 0))
                    else:
                        notes.append("undefined ammo " + a)
                p["ammo_restriction"] = ar
            out.append(p)
        return out, None, False
    return None, None, False


def main(argv):
    apply = "--apply" in argv
    ctx, docs = build_ctx()
    changed = set()
    for path, obj, key, kind, use in plan(ctx, docs):
        if kind in ("UNKNOWN",):
            print("UNKNOWN", key, path)
            continue
        if kind == "REMOVE":
            pk, thin = None, None
        else:
            pk, thin, drop_storage = mod_pockets(ctx, key, kind, obj, use)[0:3]
            if pk is None:
                print("SKIP no pockets", key)
                continue
        print(kind, key, "pockets:", len(pk) if pk else 0)
        if not apply:
            continue
        if pk is not None:
            obj["pocket_data"] = pk
            if kind in ("COPY", "AUTHOR"):
                obj.pop("storage", None)
        eff_before = pockets.as_list(pockets.effective(ctx.cdf_index, obj, "use_action"))
        kept = [a for a in pockets.as_list(obj.get("use_action")) if not pockets.is_holster_action(a)]
        new = kept + ([thin] if thin else [])
        pockets.set_use_action(obj, eff_before, new)
        if not thin and isinstance(obj.get("description"), str):
            obj["description"] = pockets.strip_activate(obj["description"])
        changed.add(path)
    if apply:
        for path in sorted(changed):
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                json.dump(docs[path], fh, indent=2, ensure_ascii=False)
                fh.write("\n")
            subprocess.run([str(ROOT / pockets.DEFAULT_FORMATTER), str(path)], check=False)
        print("changed:", len(changed))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
