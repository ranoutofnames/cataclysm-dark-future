#!/usr/bin/env python3
"""Plan (and later apply) the DDA magazine / pocket port for CDF items (P3-2).

BN stores ammo and magazine data in item keys (`ammo_type` + `capacity`, `clip_size`, `ammo` +
`max_charges`, `magazines`, `magazine_well`). DDA expresses all of it as `pocket_data`
(MAGAZINE / MAGAZINE_WELL pockets). This tool decides, per CDF item id, which `pocket_data` the
item should get and which BN keys go away, with the DDA twin (same id in the DDA tree) as the
model. Deviations from DDA are marked `// CDF:` below and in the notes of each record.

Status of this tool: chunk 1a. Implemented: loader, classification, R1 (MAG) and R2 (GUN-INT) in
`--report` mode. Rules R3-R9 (one planner function each, registered in PLANNERS), `--apply` and
`--check` come in later chunks.

Rules (PLAN-P3 section 4.3; the numbers are the plan's)
  R1  MAG with twin. Copy DDA's `pocket_data` (MAGAZINE, rigid, ammo_restriction,
      allowed_speedloaders when set). Remove `capacity`/`count` where DDA's twin has no such key
      (DDA data has no `capacity`, e.g. `glockmag`; the loader still reads it,
      item_factory.cpp load( islot_magazine )). Keep `ammo_type`, `reload_time`, `default_ammo`,
      `linkage`, `reliability`. Differences of kind `cap` take DDA's number and are listed per id.
      MAG without twin: author the pocket from its own ammo_type + capacity,
      `ammo_restriction: { <ammo_type>: capacity }`, rigid true as DDA magazines of the same ammo.
  R2  GUN-INT. `pocket_data: [ { pocket_type: MAGAZINE, rigid: true,
      ammo_restriction: { <each ammo>: clip_size }, allowed_speedloaders: [...] } ]`; speedloader
      ids come from the BN `magazines` entries flagged SPEEDLOADER; `clip_size` stays; `magazines`
      removed. Where a DDA twin exists, its pocket is copied instead and differences reported.
  R3  WELL        (later chunk)  MAGAZINE_WELL, item_restriction = DDA list + CDF ids - missing ids
  R4  TOOL-INT    (later chunk)  `ammo` -> `tool_ammo`, MAGAZINE pocket, drop max_charges
  R5  CHARGES     (later chunk)  DDA pocket when every ammotype exists, else stays on charges
  R6  ADAPTOR     (later chunk)  no edit; goes through the golden matrix
  R7  CDF-only    authored by R1-R4 (action AUTHOR); also the `dda-obsolete` twins
  R8  DDA-POCKET  (later chunk)  copy the DDA pocket when flag and ammotype prerequisites exist
  R9  +STORAGE / +EXPLICIT: tagged in the record (`tags`); EXPLICIT pockets are merged (the
      existing non-magazine pockets are kept in front of the emitted one) and need a hand look;
      STORAGE rows need the carry pocket re-emitted (later chunk)
  R10 No stats, recipes or item groups change; data/json/items/* and the mods only.

CDF deviations (all marked `// CDF:`)
  // CDF: R1 for MAG without a DDA twin also drops `capacity` and `count` (DDA convention; the
  //      pocket carries the number). `count` must go with `capacity` because item_factory.cpp
  //      rejects count > capacity ("invalid count") and a magazine with capacity 0.
  // CDF: R1 `cap` rows with a twin that keeps its own `capacity` key take DDA's number there too.
  // CDF: R2 `cap` rows: `clip_size` follows the twin when the twin has the key (pocket and key
  //      must agree), listed in `set_keys`.
  // CDF: a twin whose pocket shape differs (DDA well where CDF has an integral magazine) is
  //      NEEDS-HAND, not guessed; the stop rule counts these.
  // CDF: pockets are validated here against pocket_data::load / check_definition
  //      (src/pocket_data.cpp); violations are reported per record, never fixed silently.

Record actions: KEEP (nothing to change), COPY-TWIN (twin pocket emitted), AUTHOR (pocket authored
from CDF fields), DROP-KEY (pocket already present, only keys go), NEEDS-HAND (no rule covers
it), UNPLANNED (class has no planner yet).

Run from anywhere:

  py -3 -I tools/cdf_port_dda_mags.py [--cdf <CDF root>] [--dda <DDA root>]
        [--classes MAG,GUN-INT] [--ids a,b] --report [--out FILE.md] [--json FILE.json]

Read-only: `--apply` and `--check` are reserved for the next chunk.
"""

import argparse
import collections
import copy
import json
import os
import sys
from pathlib import Path

# ---------------------------------------------------------------------------------------------
# Loader (copied from the P3 census scripts mag_diff.py / make_items_md.py; the tool is
# standalone). JSON is read with utf-8-sig, so BOMs and CRLF line ends are harmless.
# ---------------------------------------------------------------------------------------------

ITEM_TYPES = {
    "ITEM", "AMMO", "ARMOR", "TOOL", "TOOL_ARMOR", "GENERIC", "COMESTIBLE", "GUN", "GUNMOD",
    "TOOLMOD", "MAGAZINE", "BATTERY", "BOOK", "COMPONENT_ID", "MONSTER_ITEM", "BIONIC_ITEM",
    "CONTAINER", "WHEEL", "ENGINE", "FUEL", "PET_ARMOR",
}
META = {"copy-from", "abstract", "id", "extend", "delete", "relative", "proportional"}
GUNISH = {"GUN", "GUNMOD"}
TOOLISH = {"TOOL", "TOOL_ARMOR"}
POCKET_TYPES = {"CONTAINER", "MAGAZINE", "MAGAZINE_WELL", "MOD", "CORPSE", "SOFTWARE"}
# keys pocket_data::load reads (src/pocket_data.cpp), plus "//" comments
POCKET_KEYS = {
    "pocket_type", "ammo_restriction", "flag_restriction", "item_restriction",
    "material_restriction", "allowed_speedloaders", "default_magazine", "id", "description",
    "name", "skill_restriction", "carry", "min_item_volume", "max_item_volume",
    "max_contains_volume", "max_contains_weight", "max_item_length", "min_item_length",
    "extra_encumbrance", "volume_encumber_modifier", "ripoff", "activity_noise",
    "spoil_multiplier", "insulation", "weight_multiplier", "volume_multiplier", "magazine_well",
    "moves", "fire_protection", "watertight", "airtight", "open_container", "transparent",
    "rigid", "forbidden", "holster", "ablative", "inherits_flags", "ememory_max", "sector",
    "sealed_data",
}


def listify(v):
    if v is None:
        return []
    return list(v) if isinstance(v, list) else [v]


def to_int(v):
    try:
        return int(v)
    except Exception:
        return 0


def load_tree(root):
    """Return [(object, relative path)] for every JSON object under root."""
    entries = []
    errors = []
    for dp, dns, fns in os.walk(root):
        dns.sort()
        for fn in sorted(fns):
            if not fn.lower().endswith(".json"):
                continue
            path = os.path.join(dp, fn)
            rel = os.path.relpath(path, root).replace("\\", "/")
            try:
                with open(path, "r", encoding="utf-8-sig") as fh:
                    data = json.load(fh)
            except Exception as exc:
                errors.append((rel, str(exc)[:80]))
                continue
            if isinstance(data, dict):
                data = [data]
            if isinstance(data, list):
                for o in data:
                    if isinstance(o, dict):
                        entries.append((o, rel))
    return entries, errors


def build_index(entries):
    idx = collections.defaultdict(list)
    for o, _rel in entries:
        k = o.get("id")
        if isinstance(k, str):
            idx[k].append(o)
        ab = o.get("abstract")
        if isinstance(ab, str) and ab != k:
            idx[ab].append(o)
    return idx


def effective(o, idx, stack):
    """copy-from resolved view of an object. `relative`/`proportional` are not applied."""
    cf = o.get("copy-from")
    if not isinstance(cf, str):
        return dict(o)
    if cf in stack or cf not in idx:
        return dict(o)
    cands = idx[cf]
    po = next((p for p in cands if p.get("type") == o.get("type")), cands[0])
    parent = effective(po, idx, stack | {cf})
    merged = {k: v for k, v in parent.items() if k not in META}
    for k, v in o.items():
        if k not in META:
            merged[k] = v
    ext = o.get("extend")
    if isinstance(ext, dict):
        for k, v in ext.items():
            merged[k] = listify(merged.get(k)) + listify(v)
    dele = o.get("delete")
    if isinstance(dele, dict):
        for k, v in dele.items():
            if k in merged:
                rem = listify(v)
                merged[k] = [x for x in listify(merged[k]) if x not in rem]
    return merged


def is_item(o):
    return isinstance(o.get("id"), str) and o.get("type") in ITEM_TYPES and "abstract" not in o


class Tree:
    """One side (CDF or DDA): data/json then data/mods, first definition of an id wins."""

    def __init__(self, root):
        self.root = Path(root)
        entries = []
        self.errors = []
        for sub in ("json", "mods"):
            r = self.root / "data" / sub
            if not r.is_dir():
                continue
            ents, errs = load_tree(str(r))
            for o, rel in ents:
                entries.append((o, "data/%s/%s" % (sub, rel)))
            self.errors += ["data/%s/%s" % (sub, e[0]) + ": " + e[1] for e in errs]
        self.idx = build_index(entries)
        self.items = collections.OrderedDict()
        self.dups = 0
        self.ammotypes = {}   # ammunition_type id -> default ammo item id
        self.flags = set()    # json_flag ids
        for o, rel in entries:
            t = o.get("type")
            if t == "ammunition_type" and isinstance(o.get("id"), str):
                self.ammotypes.setdefault(o["id"], o.get("default"))
            elif t == "json_flag" and isinstance(o.get("id"), str):
                self.flags.add(o["id"])
            if not is_item(o):
                continue
            iid = o["id"]
            if iid in self.items:
                self.dups += 1
                continue
            self.items[iid] = {"id": iid, "type": o["type"], "file": rel, "raw": o,
                               "eff": effective(o, self.idx, {iid})}


def pocket_list(eff):
    return [p for p in listify(eff.get("pocket_data")) if isinstance(p, dict)]


# ---------------------------------------------------------------------------------------------
# Classification (make_items_md.py classify(), same classes and order)
# ---------------------------------------------------------------------------------------------

def parse_ammo_list(v):
    return [a for a in listify(v) if isinstance(a, str)]


def parse_magazines(v):
    """BN `magazines`: [[ammo, [mag, ...]], ...] -> ordered {ammo: [mags]}"""
    res = collections.OrderedDict()
    for ent in listify(v):
        if isinstance(ent, list) and len(ent) == 2 and isinstance(ent[0], str):
            res[ent[0]] = [m for m in listify(ent[1]) if isinstance(m, str)]
    return res


def merged_gun_view(rec):
    """BN gunmods that are guns keep ammo / clip_size / magazines inside `gun_data`."""
    e = rec["eff"]
    if rec["type"] == "GUNMOD" and isinstance(e.get("gun_data"), dict):
        e = dict(e)
        for k, v in e["gun_data"].items():
            e.setdefault(k, v)
    return e


def classify(rec, flags_of):
    """Return (class, expectation) for a CDF effective record, or (None, None)."""
    t = rec["type"]
    e = merged_gun_view(rec)
    mags = parse_magazines(e.get("magazines"))
    if t == "MAGAZINE":
        types = parse_ammo_list(e.get("ammo_type"))
        cap = to_int(e.get("capacity"))
        return "MAG", {"ammo": collections.OrderedDict((a, cap) for a in types)}
    if t in GUNISH | TOOLISH | {"ARMOR", "GENERIC", "COMESTIBLE"} and mags:
        flat, loaders = [], []
        for ms in mags.values():
            for m in ms:
                fl = flags_of.get(m, set())
                tgt = loaders if ("SPEEDLOADER" in fl or "SPEEDLOADER_CLIP" in fl) else flat
                if m not in tgt:
                    tgt.append(m)
        if not flat and loaders and to_int(e.get("clip_size")) > 0:
            ammo = parse_ammo_list(e.get("ammo"))
            return "GUN-INT", {"ammo": collections.OrderedDict(
                (a, to_int(e.get("clip_size"))) for a in ammo), "speedloaders": loaders}
        return "WELL", {"mags": flat, "default": (flat or [None])[0],
                        "well": e.get("magazine_well"), "speedloaders": loaders}
    if t in GUNISH and to_int(e.get("clip_size")) > 0:
        ammo = parse_ammo_list(e.get("ammo"))
        return "GUN-INT", {"ammo": collections.OrderedDict(
            (a, to_int(e.get("clip_size"))) for a in ammo), "speedloaders": []}
    if t not in GUNISH and t not in ("MAGAZINE", "AMMO") and e.get("ammo") \
            and to_int(e.get("max_charges")) > 0:
        ammo = parse_ammo_list(e.get("ammo"))
        return "TOOL-INT", {"ammo": collections.OrderedDict(
            (a, to_int(e.get("max_charges"))) for a in ammo)}
    if t in TOOLISH and to_int(e.get("max_charges")) > 0 and not e.get("ammo"):
        return "CHARGES", {"charges": to_int(e.get("max_charges"))}
    if t in ("GUNMOD", "TOOLMOD") and e.get("magazine_adaptor"):
        return "ADAPTOR", {"adaptor": e.get("magazine_adaptor")}
    return None, None


def is_obsolete_file(path):
    pl = path.lower()
    return "obsoletion" in pl or "migration" in pl


# ---------------------------------------------------------------------------------------------
# Helpers shared by the planners
# ---------------------------------------------------------------------------------------------

class Ctx:
    def __init__(self, cdf, dda):
        self.cdf = cdf
        self.dda = dda
        self.flags_of = {}
        for iid, rec in cdf.items.items():
            self.flags_of[iid] = set(f for f in listify(rec["eff"].get("flags"))
                                     if isinstance(f, str))


def pocket_ammo(pockets):
    """{ammotype: number} over the MAGAZINE pockets."""
    out = collections.OrderedDict()
    for p in pockets:
        if p.get("pocket_type") != "MAGAZINE":
            continue
        ar = p.get("ammo_restriction")
        if isinstance(ar, dict):
            for k, v in ar.items():
                out[k] = to_int(v)
    return out


def pocket_loaders(pockets):
    out = []
    for p in pockets:
        if p.get("pocket_type") == "MAGAZINE":
            for s in listify(p.get("allowed_speedloaders")):
                if s not in out:
                    out.append(s)
    return out


def compact(pockets):
    parts = []
    for p in pockets:
        pt = p.get("pocket_type", "CONTAINER")
        if pt == "MAGAZINE":
            ar = p.get("ammo_restriction")
            s = ",".join("%s:%s" % kv for kv in ar.items()) if isinstance(ar, dict) else ""
            sl = listify(p.get("allowed_speedloaders"))
            parts.append("MAGAZINE{%s%s%s}" % (s, ";sl=%d" % len(sl) if sl else "",
                                               "" if p.get("rigid") else ";!rigid"))
        else:
            parts.append(pt)
    return " + ".join(parts) if parts else "-"


def key_site(raw, eff, key):
    """Where a key lives for the item: 'own', 'gun_data', 'inherited' or None."""
    if key in raw:
        return "own"
    gd = raw.get("gun_data")
    if isinstance(gd, dict) and key in gd:
        return "gun_data"
    if key in eff:
        return "inherited"
    gde = eff.get("gun_data")
    if isinstance(gde, dict) and key in gde:
        return "inherited"
    return None


def inherited_from(ctx, raw, key):
    """Id of the object up the copy-from chain that defines `key` (None when not found)."""
    seen = set()
    o = raw
    while isinstance(o.get("copy-from"), str):
        cf = o["copy-from"]
        if cf in seen or cf not in ctx.cdf.idx:
            return None
        seen.add(cf)
        cands = ctx.cdf.idx[cf]
        o = next((p for p in cands if p.get("type") == o.get("type")), cands[0])
        if key in o or (isinstance(o.get("gun_data"), dict) and key in o["gun_data"]):
            return o.get("id") or o.get("abstract") or cf
    return None


def usable_twin(twin):
    """R7: a twin that only is an obsoletion/migration shell is not a model; author instead."""
    if twin is not None and is_obsolete_file(twin["file"]):
        return None
    return twin


def sanitize(pockets, ctx, out):
    """Plan 4.4: ammotypes and item ids the twin names but CDF lacks are dropped, never
    created (check_definition would reject them); each one is listed in `dropped_ids`."""
    for p in pockets:
        ar = p.get("ammo_restriction")
        if isinstance(ar, dict):
            for at in list(ar):
                if at not in ctx.cdf.ammotypes:
                    del ar[at]
                    out["dropped_ids"].append({"where": "ammo_restriction", "id": at,
                                               "why": "ammotype undefined in CDF"})
        for key in ("allowed_speedloaders", "item_restriction"):
            if key in p:
                keep = [i for i in listify(p[key]) if i in ctx.cdf.items]
                for i in listify(p[key]):
                    if i not in ctx.cdf.items:
                        out["dropped_ids"].append({"where": key, "id": i,
                                                   "why": "item absent in CDF"})
                if keep:
                    p[key] = keep
                else:
                    del p[key]
                    out["notes"].append("%s emptied by dropping absent ids (now unrestricted)"
                                        % key)
    return [p for p in pockets if p.get("ammo_restriction") != {}]


def new_record(rec, cls, twin, ctx):
    eff = rec["eff"]
    tags = []
    if eff.get("storage") not in (None, "0 ml", "0"):
        tags.append("STORAGE")
    if eff.get("pocket_data"):
        tags.append("EXPLICIT")
    twin_file = twin["file"] if twin else None
    if twin_file and is_obsolete_file(twin_file):
        tags.append("dda-obsolete")
    return {
        "id": rec["id"], "class": cls, "has_twin": twin is not None,
        "source_file": rec["file"], "twin_file": twin_file, "action": None, "rule": None,
        "pocket_data": [], "dropped_keys": [], "inherited_keys": [], "dropped_ids": [],
        "set_keys": {}, "cap": [], "diff": [], "ammo_diff": {},
        "violations": [], "tags": tags, "notes": [],
    }


def finish(out, rec, existing_pockets, new_pockets):
    """Merge with an existing explicit pocket_data (R9 EXPLICIT) and settle the action."""
    keep = [p for p in existing_pockets if p.get("pocket_type") != "MAGAZINE"]
    out["pocket_data"] = keep + new_pockets
    if keep:
        out["notes"].append("EXPLICIT: %d existing non-magazine pocket(s) kept in front "
                            "(R9, merge by hand)" % len(keep))
    same = [p for p in existing_pockets if p.get("pocket_type") == "MAGAZINE"] == new_pockets
    if same and new_pockets:
        out["action"] = "DROP-KEY" if out["dropped_keys"] or out["set_keys"] else "KEEP"
        out["notes"].append("the effective item already has this pocket")


def check_pocket(p, ctx):
    """Static version of pocket_data::load / check_definition (src/pocket_data.cpp)."""
    v = []
    pt = p.get("pocket_type", "CONTAINER")
    if pt not in POCKET_TYPES:
        v.append("unknown pocket_type \"%s\"" % pt)
    for k in p:
        if k not in POCKET_KEYS and not k.startswith("//"):
            v.append("unknown pocket key \"%s\"" % k)
    for k in ("ammo_restriction", "flag_restriction", "item_restriction"):
        if k in p and not p[k]:
            v.append("pocket defines empty %s" % k)
    ar = p.get("ammo_restriction")
    if pt == "MAGAZINE" and not ar:
        v.append("MAGAZINE pocket without ammo_restriction (falls through to container checks)")
    if ar is not None and not isinstance(ar, dict):
        v.append("ammo_restriction must be an object {ammotype: number}")
        ar = None
    for at, n in (ar or {}).items():
        if at not in ctx.cdf.ammotypes:
            v.append("ammo_restriction has invalid ammo type \"%s\"" % at)
        else:
            da = ctx.cdf.ammotypes[at]
            if not da or da not in ctx.cdf.items:
                v.append("ammo type \"%s\" has an undefined default ammo item \"%s\"" % (at, da))
        if not isinstance(n, int) or isinstance(n, bool) or n <= 0:
            v.append("ammo_restriction for \"%s\" must be a positive integer (is %r)" % (at, n))
    for sl in listify(p.get("allowed_speedloaders")):
        if sl not in ctx.cdf.items:
            v.append("allowed_speedloaders has invalid item id \"%s\"" % sl)
    for it in listify(p.get("item_restriction")):
        if it not in ctx.cdf.items:
            v.append("item_restriction has invalid item id \"%s\"" % it)
    dm = p.get("default_magazine")
    if dm and dm not in ctx.cdf.items:
        v.append("default_magazine has invalid item id \"%s\"" % dm)
    if ctx.cdf.flags:
        for f in listify(p.get("flag_restriction")):
            if f not in ctx.cdf.flags:
                v.append("flag_restriction has invalid flag \"%s\"" % f)
    if p.get("rigid") and p.get("magazine_well"):
        v.append("rigid overrides magazine_well")
    return v


def twin_magazine_pockets(twin):
    if twin is None:
        return [], []
    pk = pocket_list(twin["eff"])
    return [p for p in pk if p.get("pocket_type") == "MAGAZINE"], \
           [p for p in pk if p.get("pocket_type") != "MAGAZINE"]


def cap_differences(want, got):
    """Per-ammo number differences between CDF (want) and the DDA pocket (got)."""
    rows = []
    for a in want:
        if a in got and got[a] != want[a]:
            rows.append({"ammo": a, "cdf": want[a], "dda": got[a]})
    return rows


def ammo_kinds(want, got, out):
    if set(want) != set(got):
        out["diff"].append("ammo")
        out["ammo_diff"] = {"cdf_only": [a for a in want if a not in got],
                            "dda_only": [a for a in got if a not in want]}
        out["notes"].append("ammo differs: CDF {%s} vs DDA {%s}"
                            % (",".join(want), ",".join(got)))
    else:
        rows = cap_differences(want, got)
        if rows:
            out["diff"].append("cap")
            out["cap"] = rows


# ---------------------------------------------------------------------------------------------
# Planners, one per rule group. Signature: plan_x(ctx, rec, cls, exp, twin) -> record dict.
# ---------------------------------------------------------------------------------------------

def plan_mag(ctx, rec, cls, exp, twin):
    """R1 MAG: copy the twin's MAGAZINE pocket, or author {ammo_type: capacity}."""
    out = new_record(rec, cls, twin, ctx)
    out["rule"] = "R1"
    raw, eff = rec["raw"], rec["eff"]
    for k in ("relative", "proportional"):
        if isinstance(raw.get(k), dict) and any(
                x in raw[k] for x in ("capacity", "count", "ammo_type")):
            out["action"] = "NEEDS-HAND"
            out["notes"].append("%s modifies capacity/count/ammo_type, not resolved by this tool" % k)
            return out
    want = exp["ammo"]
    shell = twin is not None and usable_twin(twin) is None
    if shell:
        out["notes"].append("DDA twin is an obsoletion shell (%s, pocket %s): authored from the "
                            "CDF fields (R7)" % (twin["file"],
                                                compact(twin_magazine_pockets(twin)[0])))
    twin = usable_twin(twin)
    tmag, tother = twin_magazine_pockets(twin)
    existing = pocket_list(eff)
    if tother:
        out["notes"].append("DDA twin also has non-magazine pockets (%s), not copied"
                            % ",".join(sorted(set(p.get("pocket_type", "CONTAINER")
                                                  for p in tother))))
    if tmag:
        new = copy.deepcopy(tmag)
        out["action"] = "COPY-TWIN"
        ammo_kinds(want, pocket_ammo(tmag), out)
        # R1: remove capacity/count where the twin has no such key
        teff = twin["eff"]
        for key in ("capacity", "count"):
            site = key_site(raw, eff, key)
            if site is None:
                continue
            if key in teff:
                if to_int(teff[key]) != to_int(eff.get(key)):
                    out["set_keys"][key] = teff[key]  # CDF: follow the twin's own key
            elif site == "inherited":
                out["inherited_keys"].append({"key": key, "value": eff.get(key),
                                              "from": inherited_from(ctx, raw, key)})
            else:
                out["dropped_keys"].append({"key": key, "value": eff.get(key), "site": site,
                                            "reason": "DDA twin has no `%s`" % key})
    else:
        if twin is not None:
            out["diff"].append("nopocket")
            out["notes"].append("DDA twin has no MAGAZINE pocket: authored like a CDF-only item")
        if not want:
            out["action"] = "NEEDS-HAND"
            out["notes"].append("MAGAZINE has no ammo_type: cannot author a pocket")
            return out
        if any(n <= 0 for n in want.values()):
            out["action"] = "NEEDS-HAND"
            out["notes"].append("MAGAZINE capacity is %d: cannot author a pocket"
                                % list(want.values())[0])
            return out
        new = [{"pocket_type": "MAGAZINE", "rigid": True,
                "ammo_restriction": collections.OrderedDict(want)}]
        out["action"] = "AUTHOR"
        # CDF: no twin to read from; DDA's convention (no `capacity`/`count` on magazines) applies
        for key in ("capacity", "count"):
            site = key_site(raw, eff, key)
            if site == "inherited":
                out["inherited_keys"].append({"key": key, "value": eff.get(key),
                                              "from": inherited_from(ctx, raw, key)})
            elif site is not None:
                out["dropped_keys"].append({"key": key, "value": eff.get(key), "site": site,
                                            "reason": "pocket carries the number (DDA convention)"})
    for d in out["inherited_keys"]:
        out["notes"].append("`%s` is inherited from `%s` (copy-from): not dropped here; goes "
                            "when that object is ported, else the loader still reads it"
                            % (d["key"], d["from"]))
    new = sanitize(new, ctx, out)
    if not new:
        out["action"] = "NEEDS-HAND"
        out["notes"].append("no ammotype of the pocket is defined in CDF")
        return out
    finish(out, rec, existing, new)
    return out


def plan_gun_int(ctx, rec, cls, exp, twin):
    """R2 GUN-INT: MAGAZINE pocket {each ammo: clip_size} + allowed_speedloaders."""
    out = new_record(rec, cls, twin, ctx)
    out["rule"] = "R2"
    raw, eff = rec["raw"], rec["eff"]
    gun = merged_gun_view(rec)
    for k in ("relative", "proportional"):
        if isinstance(raw.get(k), dict) and any(
                x in raw[k] for x in ("clip_size", "ammo", "magazines")):
            out["action"] = "NEEDS-HAND"
            out["notes"].append("%s modifies clip_size/ammo/magazines, not resolved by this tool" % k)
            return out
    want = exp["ammo"]
    loaders = exp.get("speedloaders", [])
    shell = twin is not None and usable_twin(twin) is None
    if shell:
        out["notes"].append("DDA twin is an obsoletion shell (%s, pocket %s): authored from the "
                            "CDF fields (R7)" % (twin["file"],
                                                compact(twin_magazine_pockets(twin)[0])))
    twin = usable_twin(twin)
    tmag, tother = twin_magazine_pockets(twin)
    existing = pocket_list(eff)
    twin_wells = [p for p in tother if p.get("pocket_type") == "MAGAZINE_WELL"]
    if tother:
        out["notes"].append("DDA twin also has non-magazine pockets (%s), not copied"
                            % ",".join(sorted(set(p.get("pocket_type", "CONTAINER")
                                                  for p in tother))))
    if tmag:
        new = copy.deepcopy(tmag)
        out["action"] = "COPY-TWIN"
        ammo_kinds(want, pocket_ammo(tmag), out)
        dsl = pocket_loaders(tmag)
        if loaders and not dsl:
            out["diff"].append("speedloaders-unrestricted")
        elif loaders and set(dsl) != set(loaders):
            out["diff"].append("speedloaders")
            out["notes"].append("speedloaders differ: CDF {%s} vs DDA {%s}"
                                % (",".join(loaders), ",".join(dsl)))
        elif dsl and not loaders:
            out["diff"].append("speedloaders+dda")
            out["notes"].append("DDA twin allows speedloaders {%s} that CDF `magazines` lacks"
                                % ",".join(dsl))
        # CDF: clip_size stays, but follows the twin's key when pocket and key would disagree
        tclip = twin["eff"].get("clip_size")
        if tclip is None and isinstance(twin["eff"].get("gun_data"), dict):
            tclip = twin["eff"]["gun_data"].get("clip_size")
        if "cap" in out["diff"] and tclip is not None and to_int(tclip) != to_int(gun.get("clip_size")):
            out["set_keys"]["clip_size"] = tclip
    elif twin_wells:
        out["action"] = "NEEDS-HAND"
        out["diff"].append("shape(dda=WELL)")
        out["notes"].append("DDA twin has a MAGAZINE_WELL where CDF has an integral magazine "
                            "(clip_size %s): shape decision, not covered by R1-R9"
                            % gun.get("clip_size"))
        return out
    else:
        if twin is not None:
            out["diff"].append("nopocket")
            out["notes"].append("DDA twin has no MAGAZINE pocket: authored like a CDF-only item")
        if not want:
            out["action"] = "NEEDS-HAND"
            out["notes"].append("gun has clip_size but no `ammo`: cannot author a pocket")
            return out
        p = collections.OrderedDict()
        p["pocket_type"] = "MAGAZINE"
        p["rigid"] = True
        p["ammo_restriction"] = collections.OrderedDict(want)
        if loaders:
            p["allowed_speedloaders"] = list(loaders)
        new = [p]
        out["action"] = "AUTHOR"
    # R2: BN `magazines` removed (only reached for speedloader-only lists, see classify)
    if loaders:
        site = key_site(raw, eff, "magazines")
        if site == "inherited":
            out["inherited_keys"].append({"key": "magazines", "value": gun.get("magazines"),
                                          "from": inherited_from(ctx, raw, "magazines")})
            out["notes"].append("`magazines` is inherited from `%s` (copy-from): not dropped "
                                "here; goes when that object is ported"
                                % out["inherited_keys"][-1]["from"])
        elif site is not None:
            out["dropped_keys"].append({"key": "magazines", "value": gun.get("magazines"),
                                        "site": site,
                                        "reason": "speedloader-only list moves to allowed_speedloaders"})
    new = sanitize(new, ctx, out)
    if not new:
        out["action"] = "NEEDS-HAND"
        out["notes"].append("no ammotype of the pocket is defined in CDF")
        return out
    finish(out, rec, existing, new)
    return out


# class -> planner. Later chunks add WELL (R3), TOOL-INT (R4), CHARGES (R5), ADAPTOR (R6),
# DDA-POCKET (R8); R7 is "author when there is no twin" inside each planner, R9 is the
# STORAGE/EXPLICIT tagging above.
PLANNERS = collections.OrderedDict([
    ("MAG", plan_mag),
    ("GUN-INT", plan_gun_int),
])
ALL_CLASSES = ["MAG", "GUN-INT", "TOOL-INT", "WELL", "ADAPTOR", "CHARGES", "DDA-POCKET"]


def build_records(ctx, classes, ids):
    records = []
    for iid, rec in ctx.cdf.items.items():
        if ids and iid not in ids:
            continue
        cls, exp = classify(rec, ctx.flags_of)
        twin = ctx.dda.items.get(iid)
        if cls is None:
            if twin is not None and any(p.get("pocket_type") in ("MAGAZINE", "MAGAZINE_WELL")
                                        for p in pocket_list(twin["eff"])):
                cls, exp = "DDA-POCKET", {}
            else:
                continue
        if cls == "CHARGES" and (twin is None or not any(
                p.get("pocket_type") in ("MAGAZINE", "MAGAZINE_WELL")
                for p in pocket_list(twin["eff"]))):
            continue
        if cls not in classes:
            continue
        planner = PLANNERS.get(cls)
        if planner is None:
            out = new_record(rec, cls, twin, ctx)
            out["action"] = "UNPLANNED"
            out["notes"].append("no planner for class %s in this chunk" % cls)
        else:
            out = planner(ctx, rec, cls, exp, twin)
        for p in out["pocket_data"]:
            out["violations"] += check_pocket(p, ctx)
        records.append(out)
    records.sort(key=lambda r: (ALL_CLASSES.index(r["class"]), r["id"]))
    return records


# ---------------------------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------------------------

def summary_counts(records):
    by_cls = collections.OrderedDict()
    for r in records:
        c = by_cls.setdefault(r["class"], collections.Counter())
        c["total"] += 1
        c["twin" if r["has_twin"] else "cdf-only"] += 1
        c[r["action"]] += 1
    return by_cls


def write_markdown(path, records, args, ctx):
    by_cls = summary_counts(records)
    actions = sorted(set(r["action"] for r in records))
    L = []
    L.append("# P3-2 magazine port plan (R1 MAG, R2 GUN-INT)\n")
    L.append("CDF `%s`, DDA `%s`. Classes: %s. Generated by `tools/cdf_port_dda_mags.py --report`; "
             "read-only.\n" % (args.cdf, args.dda, ",".join(sorted(by_cls)) or "-"))
    L.append("## Counts\n")
    L.append("| class | total | twin | CDF-only | " + " | ".join(actions) + " |")
    L.append("|---|---|---|---|" + "---|" * len(actions))
    for c, cnt in by_cls.items():
        L.append("| %s | %d | %d | %d | %s |" % (c, cnt["total"], cnt["twin"], cnt["cdf-only"],
                                                " | ".join(str(cnt[a]) for a in actions)))
    L.append("")
    dk = collections.Counter(d["key"] for r in records for d in r["dropped_keys"])
    kinds = collections.Counter(k for r in records for k in r["diff"])
    L.append("Dropped keys: %s. Difference kinds: %s. Records with violations: %d. Tags: %s.\n"
             % (", ".join("%s=%d" % kv for kv in sorted(dk.items())) or "none",
                ", ".join("%s=%d" % kv for kv in sorted(kinds.items())) or "none",
                sum(1 for r in records if r["violations"]),
                ", ".join("%s=%d" % kv for kv in sorted(collections.Counter(
                    t for r in records for t in r["tags"]).items())) or "none"))
    hand = [r for r in records if r["action"] == "NEEDS-HAND"]
    twin_rows = sum(1 for r in records if r["has_twin"])
    L.append("## Stop rule\n")
    L.append("NEEDS-HAND rows with a DDA twin: %d of %d twin rows in this report (%.1f%%; limit 5%% "
             "of all 725 twin rows = 36).\n"
             % (sum(1 for r in hand if r["has_twin"]), twin_rows,
                100.0 * sum(1 for r in hand if r["has_twin"]) / max(1, twin_rows)))
    if hand:
        L.append("| id | class | twin | why |\n|---|---|---|---|")
        for r in hand:
            L.append("| `%s` | %s | %s | %s |" % (r["id"], r["class"], "yes" if r["has_twin"] else "no",
                                               "; ".join(r["notes"])))
        L.append("")
    caps = [r for r in records if r["cap"]]
    L.append("## Capacity differences (CDF number replaced by DDA's)\n")
    if caps:
        L.append("| id | class | ammo | CDF | DDA |\n|---|---|---|---|---|")
        for r in caps:
            for c in r["cap"]:
                L.append("| `%s` | %s | %s | %d | %d |" % (r["id"], r["class"], c["ammo"],
                                                         c["cdf"], c["dda"]))
    else:
        L.append("none")
    L.append("")
    amm = [r for r in records if r["ammo_diff"]]
    L.append("## Ammo differences (the pocket takes DDA's ammotypes; `ammo_type`/`ammo` stay "
             "CDF's, so rows with CDF-only ammo lose it in the pocket: owner decision)\n")
    if amm:
        L.append("| id | class | CDF-only ammo | DDA-only ammo |\n|---|---|---|---|")
        for r in amm:
            L.append("| `%s` | %s | %s | %s |" % (r["id"], r["class"],
                                                 ",".join(r["ammo_diff"]["cdf_only"]) or "-",
                                                 ",".join(r["ammo_diff"]["dda_only"]) or "-"))
    else:
        L.append("none")
    L.append("")
    dids = [r for r in records if r["dropped_ids"]]
    L.append("## Ammotypes / item ids dropped from copied pockets (undefined or absent in CDF)\n")
    if dids:
        L.append("| id | class | where | dropped |\n|---|---|---|---|")
        for r in dids:
            L.append("| `%s` | %s | %s | %s |" % (
                r["id"], r["class"], ",".join(sorted(set(d["where"] for d in r["dropped_ids"]))),
                ",".join(d["id"] for d in r["dropped_ids"])))
    else:
        L.append("none")
    L.append("")
    inh = [r for r in records if r["inherited_keys"]]
    L.append("## Keys inherited through copy-from (not droppable on the item itself)\n")
    if inh:
        L.append("| id | class | key | defined on |\n|---|---|---|---|")
        for r in inh:
            for d in r["inherited_keys"]:
                L.append("| `%s` | %s | %s | `%s` |" % (r["id"], r["class"], d["key"], d["from"]))
    else:
        L.append("none")
    L.append("")
    viol = [r for r in records if r["violations"]]
    L.append("## Pocket constraint violations (pocket_data::load / check_definition)\n")
    if viol:
        L.append("| id | class | action | violations |\n|---|---|---|---|")
        for r in viol:
            L.append("| `%s` | %s | %s | %s |" % (r["id"], r["class"], r["action"],
                                                 "; ".join(sorted(set(r["violations"])))))
    else:
        L.append("none")
    L.append("")
    L.append("## Records\n")
    L.append("| id | class | twin | file | action | pocket | dropped | set | diff | tags | notes |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for r in records:
        L.append("| `%s` | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["id"], r["class"], "yes" if r["has_twin"] else "-", r["source_file"],
            r["action"], compact(r["pocket_data"]).replace("|", "/"),
            ",".join(d["key"] for d in r["dropped_keys"]) or "-",
            ",".join("%s=%s" % kv for kv in r["set_keys"].items()) or "-",
            ",".join(r["diff"]) or "-", ",".join(r["tags"]) or "-",
            "; ".join(r["notes"]).replace("|", "/") or "-"))
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")


def main():
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cdf", type=Path, default=here, help="CDF source root (default: this repo)")
    ap.add_argument("--dda", type=Path, default=Path(r"C:\CDF\CDDA SRC"),
                    help="Cataclysm: DDA source root")
    ap.add_argument("--classes", default="",
                    help="comma separated classes (default: every class with a planner)")
    ap.add_argument("--ids", default="", help="comma separated item ids")
    ap.add_argument("--report", action="store_true", help="plan per id (read-only)")
    ap.add_argument("--out", type=Path, help="markdown report file (with --report)")
    ap.add_argument("--json", type=Path, help="JSON file, one record per id (with --report)")
    ap.add_argument("--apply", action="store_true", help="reserved (next chunk)")
    ap.add_argument("--check", action="store_true", help="reserved (next chunk)")
    args = ap.parse_args()

    if args.apply or args.check:
        print("--apply and --check are not implemented in this chunk", file=sys.stderr)
        return 2
    if not args.report:
        ap.error("nothing to do: use --report")
    classes = [c.strip() for c in args.classes.split(",") if c.strip()] or list(PLANNERS)
    for c in classes:
        if c not in ALL_CLASSES:
            ap.error("unknown class %s (known: %s)" % (c, ",".join(ALL_CLASSES)))
    ids = set(i.strip() for i in args.ids.split(",") if i.strip())

    cdf, dda = Tree(args.cdf), Tree(args.dda)
    for side, t in (("CDF", cdf), ("DDA", dda)):
        for e in t.errors:
            print("warning: %s JSON not read: %s" % (side, e), file=sys.stderr)
    ctx = Ctx(cdf, dda)
    records = build_records(ctx, set(classes), ids)

    if args.out:
        write_markdown(str(args.out), records, args, ctx)
    if args.json:
        with open(str(args.json), "w", encoding="utf-8", newline="\n") as fh:
            json.dump(records, fh, indent=1, ensure_ascii=False)
            fh.write("\n")
    for c, cnt in summary_counts(records).items():
        print("%-10s total=%d twin=%d cdf-only=%d  %s" % (
            c, cnt["total"], cnt["twin"], cnt["cdf-only"],
            " ".join("%s=%d" % (k, v) for k, v in sorted(cnt.items())
                     if k not in ("total", "twin", "cdf-only"))))
    print("actions: %s" % dict(collections.Counter(r["action"] for r in records)))
    print("violations: %d records, NEEDS-HAND: %d" % (
        sum(1 for r in records if r["violations"]),
        sum(1 for r in records if r["action"] == "NEEDS-HAND")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
