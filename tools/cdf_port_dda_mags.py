#!/usr/bin/env python3
"""Plan (and later apply) the DDA magazine / pocket port for CDF items (P3-2).

BN stores ammo and magazine data in item keys (`ammo_type` + `capacity`, `clip_size`, `ammo` +
`max_charges`, `magazines`, `magazine_well`). DDA expresses all of it as `pocket_data`
(MAGAZINE / MAGAZINE_WELL pockets). This tool decides, per CDF item id, which `pocket_data` the
item should get and which BN keys go away, with the DDA twin (same id in the DDA tree) as the
model. Deviations from DDA are marked `// CDF:` below and in the notes of each record.

Status of this tool: chunk 1c. Implemented: loader, classification, R1 (MAG), R2 (GUN-INT),
R3 (WELL), R4 (TOOL-INT), R5 (CHARGES), R6 (ADAPTOR), R8 (DDA-POCKET), the R9 STORAGE/EXPLICIT
handling and the HAND-FALLBACK defaults for NEEDS-HAND rows, all in `--report` mode. `--apply`
and `--check` come in the next chunk.

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
  R3  WELL        MAGAZINE_WELL, item_restriction = DDA twin list + CDF `magazines` ids the twin
                  lacks (kept, `// CDF:`) - ids CDF lacks (`dropped_ids`). `magazine_well` moves
                  from the item key into the pocket; BN `magazines` / `magazine_well` are dropped
                  keys. Flag-restricted DDA wells (BATTERY_*, GASFILTER_*) are copied as is. The
                  loader sets default_magazine = item_restriction.front(), so list order is the
                  default. A twin well emptied by the id drop, or a twin with an integral
                  MAGAZINE where CDF has a well, is NEEDS-HAND. CDF-only: item_restriction = the
                  listed mags that exist in CDF, magazine_well from the item (tools without one
                  get rigid true, the DDA tool convention).
  R4  TOOL-INT    `ammo` -> `tool_ammo` (renamed_keys; the CDF loader keeps `ammo` as alias,
                  `// CDF:`), pocket MAGAZINE { ammo_restriction: { <ammo>: max_charges } } (the
                  twin's MAGAZINE / MAGAZINE_WELL pockets are copied where a twin exists),
                  `max_charges` / `initial_charges` dropped (DDA "Redundant max charge value").
                  Battery tools without a twin pocket take the nearest DDA twin tool's shape
                  (copy-from chain, id stem, same parent, same max_charges + volume): flag well
                  copied from a BATTERY_* twin, or integral MAGAZINE {battery: max_charges}; no
                  evidence -> NEEDS-HAND (size flag unknown). Ammotypes whose default ammo item
                  is empty in CDF are dropped from pockets (check_definition would reject them).
  R5  CHARGES     tools with `max_charges` and no `ammo` (DDA does the same for ammo-less
                  tools). Where every ammotype / well id / flag of the twin's MAGAZINE (or
                  MAGAZINE_WELL) pocket exists in CDF (an ammotype whose default ammo item is
                  empty counts as undefined), the twin pocket is copied (COPY-TWIN; the item's
                  `max_charges` / `initial_charges` go, `tool_ammo` is set from the twin); else
                  the tool stays on `charges` (KEEP-CHARGES, the missing names are listed).
  R6  ADAPTOR     no edit: `magazine_adaptor` and `magazines_like` stay (KEEP); the 6 REVIEW
                  rows (DDA twin has the adaptor) are KEEP-REVIEW, the golden matrix covers them
  R7  CDF-only    authored by R1-R4 (action AUTHOR); also the `dda-obsolete` twins
  R8  DDA-POCKET  copy the DDA pocket when its flag, ammotype and item-id prerequisites exist
                  in CDF (COPY-TWIN, `tool_ammo` set from the twin), else LEAVE and list the
                  missing names; an item that already has its own explicit magazine pocket
                  (TEST_DATA fixtures) is KEEP.
  R9  +STORAGE / +EXPLICIT: tagged in the record (`tags`). Pocket order follows
      synthesize_pockets (src/item_factory.cpp): container/holster/bandolier, MAGAZINE,
      MAGAZINE_WELL, MOD, CORPSE, software, then the worn-storage carry pockets last. So a
      STORAGE row (ARMOR/TOOL_ARMOR with storage > 0, not explicit) is emitted as
      [magazine/well pocket(s)] + [carry pocket(s)]: the garment overlay layout when one exists
      (`item_overlay` pocket_data, every pocket carry), else `{CONTAINER, carry, max_contains_
      volume: storage}` (+ rigid when the item is) and a holster strap pocket when storage >=
      10 L (`carry_pockets`). EXPLICIT rows (the item or its copy-from parent has pocket_data)
      keep their existing pockets in front and get the magazine pocket appended; they are
      noted for a hand look. A STORAGE+EXPLICIT row gets no synthesized carry pocket (the
      explicit list is assumed to carry the garment pockets; capacity noted). `synth_lost`
      lists the synthesized pockets an explicit pocket_data suppresses (MOD for guns and
      ammo tools, container_data, holster, bandolier): the loader returns early on
      explicit_pockets, DDA synthesizes MOD itself, so P3-1 must keep synthesizing MOD or
      --apply must emit it.
  HAND-FALLBACK   NEEDS-HAND rows get a concrete pocket so --apply is deterministic: the
      planner re-run without the twin (the item's own CDF shape: integral magazine from
      ammo/clip_size/max_charges, well from `magazines`; battery tools without size evidence:
      integral MAGAZINE {battery: max_charges}), else the row is left as it is. The action stays
      HAND-FALLBACK and the row is listed in the report; the reason is kept in `hand_reason`.
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
  // CDF: R3 keeps CDF-only magazine ids in DDA wells (listed per record, `well_diff.cdf_added`),
  //      owner decision Q3; ids CDF lacks are dropped and listed in `dropped_ids`.
  // CDF: R4 `tool_ammo` is DDA's key; `ammo` stays accepted by the CDF loader as an alias.
  // CDF: pockets are validated here against pocket_data::load / check_definition
  //      (src/pocket_data.cpp); violations are reported per record, never fixed silently.

Record actions: KEEP (nothing to change), KEEP-REVIEW (R6 REVIEW row), KEEP-CHARGES (R5, stays
on charges), LEAVE (R8, prerequisites missing), COPY-TWIN (twin pocket emitted), AUTHOR (pocket
authored from CDF fields), DROP-KEY (pocket already present, only keys go), HAND-FALLBACK
(NEEDS-HAND row with a default; flagged), NEEDS-HAND (only when the fallback is disabled),
UNPLANNED (class has no planner).

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
        self.overlays = {}    # item_overlay id -> pocket_data layout (worn storage, R9)
        for o, rel in entries:
            t = o.get("type")
            if t == "ammunition_type" and isinstance(o.get("id"), str):
                self.ammotypes.setdefault(o["id"], o.get("default"))
            elif t == "json_flag" and isinstance(o.get("id"), str):
                self.flags.add(o["id"])
            elif t == "item_overlay" and isinstance(o.get("id"), str)                     and isinstance(o.get("pocket_data"), list):
                self.overlays.setdefault(o["id"], o["pocket_data"])
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
            parts.append(pt + ("(carry)" if p.get("carry") else ""))
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
                elif ctx.cdf.ammotypes[at] not in ctx.cdf.items:
                    # pocket_data::check_definition: the default ammo item must exist
                    del ar[at]
                    out["dropped_ids"].append({"where": "ammo_restriction", "id": at,
                                               "why": "ammotype has no default ammo item in "
                                                      "CDF yet (default %r)"
                                                      % ctx.cdf.ammotypes[at]})
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


def storage_ml(eff):
    """Worn storage of an item in ml (0 when none or not a plain volume string)."""
    v = eff.get("storage")
    if v is None:
        return 0.0
    ml = to_ml(v)
    return ml if ml is not None else 0.0


def synth_lost(rec):
    """Pockets BN's synthesize_pockets builds that an explicit pocket_data would suppress
    (item_factory.cpp: `if( obj.explicit_pockets ) return;` before any synthesis). DDA adds
    the MOD pocket itself; CDF does not, so --apply must emit it or P3-1 must keep synthesizing
    it. Only meaningful for items that have no explicit pocket_data today."""
    eff, t = rec["eff"], rec["type"]
    if eff.get("pocket_data"):
        return []
    lost = []
    if t == "GUN" or (t == "GUNMOD" and isinstance(eff.get("gun_data"), dict)) or             (t in TOOLISH and (eff.get("ammo") or eff.get("tool_ammo") or eff.get("magazines"))):
        lost.append("MOD")
    if eff.get("container_data"):
        lost.append("container_data")
    ua = json.dumps(eff.get("use_action"), sort_keys=True)
    for kind in ("holster", "bandolier"):
        if '"%s"' % kind in ua:
            lost.append(kind)
    return lost


def new_record(rec, cls, twin, ctx):
    eff = rec["eff"]
    tags = []
    if rec["type"] in ("ARMOR", "TOOL_ARMOR") and storage_ml(eff) > 0:
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
        "violations": [], "tags": tags, "notes": [], "carry": [], "synth_lost": synth_lost(rec),
    }


def carry_pockets(ctx, rec):
    """R9: the worn-storage pockets synthesize_pockets derives (item_factory.cpp, last block):
    the garment overlay layout when one exists (every pocket `carry`), else one CONTAINER
    `carry` pocket of the item's `storage` (rigid as the item), plus a holster strap pocket
    when storage >= 10 L. `moves` (100) and `max_item_length` take the loader defaults, which
    equal what the synthesis sets."""
    if rec["type"] not in ("ARMOR", "TOOL_ARMOR") or storage_ml(rec["eff"]) <= 0:
        return []
    ov = ctx.cdf.overlays.get(rec["id"])
    if ov:
        res = []
        for p in ov:
            if isinstance(p, dict):
                q = collections.OrderedDict(copy.deepcopy(p))
                q["carry"] = True
                res.append(q)
        return res
    main = collections.OrderedDict()
    main["pocket_type"] = "CONTAINER"
    main["carry"] = True
    main["max_contains_volume"] = rec["eff"]["storage"]
    if rec["eff"].get("rigid"):
        main["rigid"] = True
    res = [main]
    if storage_ml(rec["eff"]) >= 10000:
        strap = collections.OrderedDict()
        strap["pocket_type"] = "CONTAINER"
        strap["carry"] = True
        strap["holster"] = True
        strap["max_contains_volume"] = "10 L"
        strap["max_item_volume"] = "10 L"
        strap["max_item_length"] = "150 cm"
        if rec["eff"].get("rigid"):
            strap["rigid"] = True
        res.append(strap)
    return res


def finish(out, rec, existing_pockets, new_pockets, mag_types=("MAGAZINE",), ctx=None):
    """Merge with an existing explicit pocket_data (R9 EXPLICIT) and settle the action.
    `mag_types` are the pocket types the planner replaces (R3 adds MAGAZINE_WELL).
    Emitted order follows synthesize_pockets: existing explicit pockets, the magazine/well
    pocket(s), then (R9 STORAGE) the carry pocket(s), which BN derives from `storage` and which
    an explicit pocket_data would otherwise drop."""
    keep = [p for p in existing_pockets if p.get("pocket_type") not in mag_types]
    out["pocket_data"] = keep + new_pockets
    if keep:
        out["notes"].append("EXPLICIT: %d existing non-magazine pocket(s) kept in front "
                            "(R9, merge by hand)" % len(keep))
    same = [p for p in existing_pockets if p.get("pocket_type") in mag_types] == new_pockets
    if same and new_pockets:
        out["action"] = "DROP-KEY" if out["dropped_keys"] or out["set_keys"] else "KEEP"
        out["notes"].append("the effective item already has this pocket")
        return
    if "STORAGE" in out["tags"] and new_pockets and ctx is not None:
        if existing_pockets:
            cap = sum(to_ml(p.get("max_contains_volume")) or 0.0 for p in existing_pockets
                      if p.get("pocket_type", "CONTAINER") == "CONTAINER" and not p.get("holster"))
            out["notes"].append("STORAGE+EXPLICIT: the explicit pocket_data already carries the "
                                "garment pockets (%.0f ml in CONTAINER pockets vs storage %.0f "
                                "ml); no carry pocket added, check by hand (R9)"
                                % (cap, storage_ml(rec["eff"])))
        else:
            carry = carry_pockets(ctx, rec)
            if carry:
                out["carry"] = carry
                out["pocket_data"] = out["pocket_data"] + copy.deepcopy(carry)
                out["notes"].append(
                    "STORAGE (R9): %d carry pocket(s) from %s re-emitted after the magazine "
                    "pocket (synthesize_pockets order: magazine/well, MOD, carry)"
                    % (len(carry), "the garment overlay" if ctx.cdf.overlays.get(rec["id"])
                       else "`storage` %s" % rec["eff"]["storage"]))


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
    finish(out, rec, existing, new, ctx=ctx)
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
    finish(out, rec, existing, new, ctx=ctx)
    return out


# ---------------------------------------------------------------------------------------------
# R3 / R4 helpers (chunk 1b)
# ---------------------------------------------------------------------------------------------

def to_ml(v):
    """'250 ml' / '0.5 L' -> float ml, None when it is not a plain volume string."""
    s = str(v).strip().lower().replace(" ", "")
    for unit, mul in (("ml", 1.0), ("l", 1000.0)):
        if s.endswith(unit):
            try:
                return float(s[:-len(unit)]) * mul
            except ValueError:
                return None
    return None


def accepts(p, mid, ctx):
    """True when the well pocket p takes magazine mid (item_restriction or a shared flag)."""
    if mid in listify(p.get("item_restriction")):
        return True
    fr = set(f for f in listify(p.get("flag_restriction")) if isinstance(f, str))
    return bool(fr & ctx.flags_of.get(mid, set()))


def move_item_key(ctx, rec, out, key, reason):
    """Record the removal of an item-level BN key (own, gun_data) or its inheritance."""
    raw, eff = rec["raw"], rec["eff"]
    site = key_site(raw, eff, key)
    if site is None:
        return
    value = merged_gun_view(rec).get(key)
    if site == "inherited":
        src = inherited_from(ctx, raw, key)
        out["inherited_keys"].append({"key": key, "value": value, "from": src})
        out["notes"].append("`%s` is inherited from `%s` (copy-from): not dropped here; goes "
                            "when that object is ported, else the loader still reads it"
                            % (key, src))
    else:
        out["dropped_keys"].append({"key": key, "value": value, "site": site, "reason": reason})


def fix_well_ids(p, ctx, out):
    """Drop item ids CDF lacks from a copied well pocket (listed in dropped_ids)."""
    if "item_restriction" in p:
        keep = []
        for i in listify(p["item_restriction"]):
            if i in ctx.cdf.items:
                keep.append(i)
            else:
                out["dropped_ids"].append({"where": "item_restriction", "id": i,
                                           "why": "item absent in CDF"})
        p["item_restriction"] = keep
    dm = p.get("default_magazine")
    if dm and dm not in ctx.cdf.items:
        out["dropped_ids"].append({"where": "default_magazine", "id": dm,
                                   "why": "item absent in CDF"})
        del p["default_magazine"]


def battery_shapes(ctx):
    """CDF item id -> (kind, DDA pocket) for items whose usable DDA twin shows a battery
    pocket: a MAGAZINE_WELL with a BATTERY_* flag restriction, or a MAGAZINE with battery."""
    if getattr(ctx, "_bshapes", None) is None:
        res = {}
        for iid in ctx.cdf.items:
            tw = usable_twin(ctx.dda.items.get(iid))
            if tw is None:
                continue
            for p in pocket_list(tw["eff"]):
                pt = p.get("pocket_type")
                if pt == "MAGAZINE_WELL" and any(str(f).startswith("BATTERY_")
                                                 for f in listify(p.get("flag_restriction"))):
                    res[iid] = ("WELL", p)
                    break
                if pt == "MAGAZINE" and isinstance(p.get("ammo_restriction"), dict) \
                        and "battery" in p["ammo_restriction"]:
                    res[iid] = ("MAG", p)
                    break
        ctx._bshapes = res
    return ctx._bshapes


STEM_SUFFIXES = ("_on", "_off", "_music", "_screen_lit", "_flashlight", "_nv", "_lit", "_filled",
                 "_active", "_inactive", "_charging")


def battery_sibling(ctx, rec):
    """Nearest CDF item with a DDA twin that shows a battery pocket, with the evidence used:
    copy-from chain, id stem (`_on`, `_music`, ...), same copy-from parent, same max_charges
    and volume. Returns (id, kind, pocket, evidence) or None."""
    shapes = battery_shapes(ctx)
    iid, raw, eff = rec["id"], rec["raw"], rec["eff"]
    if getattr(ctx, "_by_parent", None) is None:
        bp = collections.defaultdict(list)
        for oid, r in ctx.cdf.items.items():
            cf = r["raw"].get("copy-from")
            if isinstance(cf, str):
                bp[cf].append(oid)
        ctx._by_parent = bp

    def hit(cid, why):
        if cid != iid and cid in shapes:
            return (cid,) + shapes[cid] + (why,)
        return None

    seen, o = set(), raw
    while isinstance(o.get("copy-from"), str):
        cf = o["copy-from"]
        if cf in seen or cf not in ctx.cdf.idx:
            break
        seen.add(cf)
        h = hit(cf, "copy-from chain")
        if h:
            return h
        cands = ctx.cdf.idx[cf]
        o = next((p for p in cands if p.get("type") == o.get("type")), cands[0])
    stems, cur = [], iid
    changed = True
    while changed:
        changed = False
        for suf in STEM_SUFFIXES:
            if cur.endswith(suf) and len(cur) > len(suf):
                cur = cur[:-len(suf)]
                stems.append(cur)
                changed = True
    for stem in stems + [iid]:
        for cid in (stem, stem + "_on", stem + "_off"):
            h = hit(cid, "id stem")
            if h:
                return h
    for cf in seen | ({raw["copy-from"]} if isinstance(raw.get("copy-from"), str) else set()):
        for cid in ctx._by_parent.get(cf, []):
            h = hit(cid, "same copy-from parent `%s`" % cf)
            if h:
                return h
    mc, vol = to_int(eff.get("max_charges")), eff.get("volume")
    if mc > 0 and vol is not None:
        same = [c for c in shapes if c != iid and c in ctx.cdf.items
                and to_int(ctx.cdf.items[c]["eff"].get("max_charges")) == mc
                and ctx.cdf.items[c]["eff"].get("volume") == vol]
        flags = set(json.dumps(shapes[c][1].get("flag_restriction"), sort_keys=True) +
                    shapes[c][0] for c in same)
        if same and len(flags) == 1:
            return (same[0],) + shapes[same[0]] + (
                "same max_charges %d and volume %s as %d twin tool(s)" % (mc, vol, len(same)),)
    return None


# ---------------------------------------------------------------------------------------------
# R3 WELL
# ---------------------------------------------------------------------------------------------

def plan_well(ctx, rec, cls, exp, twin):
    """R3 WELL: MAGAZINE_WELL, item_restriction = twin list + CDF ids it lacks - ids CDF lacks."""
    out = new_record(rec, cls, twin, ctx)
    out["rule"] = "R3"
    out["well_diff"] = {"cdf_added": [], "dda_only_kept": [], "unfit": []}
    raw, eff = rec["raw"], rec["eff"]
    gun = merged_gun_view(rec)
    for k in ("relative", "proportional"):
        if isinstance(raw.get(k), dict) and any(
                x in raw[k] for x in ("magazines", "magazine_well", "ammo")):
            out["action"] = "NEEDS-HAND"
            out["notes"].append("%s modifies magazines/magazine_well/ammo, not resolved by this "
                                "tool" % k)
            return out
    mags, loaders = exp["mags"], exp.get("speedloaders", [])
    present = [x for x in mags if x in ctx.cdf.items]
    for x in mags:
        if x not in ctx.cdf.items:
            out["dropped_ids"].append({"where": "magazines", "id": x,
                                       "why": "item absent in CDF (CDF's own list)"})
    if loaders:
        out["notes"].append("`magazines` also lists speedloader(s) {%s}: wells take magazines "
                            "only, not carried into the pocket (hand look)" % ",".join(loaders))
    shell = twin is not None and usable_twin(twin) is None
    if shell:
        out["notes"].append("DDA twin is an obsoletion shell (%s): authored from the CDF fields "
                            "(R7)" % twin["file"])
    twin = usable_twin(twin)
    pk = pocket_list(twin["eff"]) if twin else []
    twells = [p for p in pk if p.get("pocket_type") == "MAGAZINE_WELL"]
    tmag = [p for p in pk if p.get("pocket_type") == "MAGAZINE"]
    tother = [p for p in pk if p.get("pocket_type") not in ("MAGAZINE", "MAGAZINE_WELL")]
    existing = pocket_list(eff)
    if tother:
        out["notes"].append("DDA twin also has non-magazine pockets (%s), not copied"
                            % ",".join(sorted(set(p.get("pocket_type", "CONTAINER")
                                                  for p in tother))))
    well_key = gun.get("magazine_well")
    new = []
    if twells:
        new = copy.deepcopy(twells)
        out["action"] = "COPY-TWIN"
        twin_fronts = [listify(p.get("item_restriction"))[0] if p.get("item_restriction")
                       else None for p in new]
        for p in new:
            fix_well_ids(p, ctx, out)
        if any(d["where"] == "item_restriction" for d in out["dropped_ids"]):
            out["diff"].append("mags-absent")
        target = next((p for p in new if "item_restriction" in p), None)
        for x in present:
            if any(accepts(p, x, ctx) for p in new):
                continue
            if target is not None:
                target["item_restriction"].append(x)
                out["well_diff"]["cdf_added"].append(x)
            else:
                out["well_diff"]["unfit"].append(x)
        out["well_diff"]["dda_only_kept"] = [
            x for p in new for x in listify(p.get("item_restriction"))
            if x not in mags and x not in out["well_diff"]["cdf_added"]]
        if out["well_diff"]["cdf_added"]:
            out["diff"].append("mags+cdf")
            out["notes"].append("// CDF: kept CDF-only magazine id(s) in the well: %s"
                                % ",".join(out["well_diff"]["cdf_added"]))
        if out["well_diff"]["dda_only_kept"]:
            out["diff"].append("mags+dda")
        if out["well_diff"]["unfit"]:
            out["diff"].append("mags-unfit")
            out["notes"].append("CDF magazines {%s} are not accepted by the DDA flag well "
                                "(no item_restriction to extend)"
                                % ",".join(out["well_diff"]["unfit"]))
        for p in new:
            if "item_restriction" in p and not p["item_restriction"]:
                if p.get("flag_restriction"):
                    del p["item_restriction"]
                else:
                    out["action"] = "NEEDS-HAND"
                    out["notes"].insert(0, "twin well item_restriction is empty after dropping "
                                        "ids CDF lacks and no CDF magazine fits")
                    return out
        first = new[0]
        tv = first.get("magazine_well")
        if well_key is not None and tv is None:
            if first.get("rigid"):
                out["notes"].append("CDF `magazine_well` %s not carried: the twin pocket is "
                                    "rigid (rigid overrides magazine_well)" % well_key)
            else:
                first["magazine_well"] = well_key
                out["diff"].append("well-volume+cdf")
                out["notes"].append("// CDF: twin pocket has no magazine_well; CDF's %s moved "
                                    "into the pocket" % well_key)
        elif well_key is not None and to_ml(tv) != to_ml(well_key):
            out["diff"].append("well-volume")
            out["notes"].append("magazine_well CDF %s vs DDA %s (DDA's pocket value is used)"
                                % (well_key, tv))
        # the loader sets default_magazine = item_restriction.front() (pocket_data.cpp)
        fronts = [(a, listify(p.get("item_restriction"))[0] if p.get("item_restriction") else None)
                  for a, p in zip(twin_fronts, new)]
        for was, now in fronts:
            if was is not None and now is not None and was != now:
                out["diff"].append("default-shift")
                out["notes"].append("default magazine (first item_restriction id) changes from "
                                    "%s to %s: the DDA default id is absent in CDF" % (was, now))
    elif tmag:
        out["action"] = "NEEDS-HAND"
        out["diff"].append("shape(dda=MAGAZINE)")
        out["notes"].insert(0, "DDA twin has an integral magazine (%s) where CDF has a magazine "
                            "well (%s): shape decision, not covered by R1-R9"
                            % (compact(tmag), ",".join(mags) or "-"))
        return out
    else:
        if twin is not None:
            out["diff"].append("nopocket")
            out["notes"].append("DDA twin has no magazine pocket: authored like a CDF-only item")
        if not present:
            out["action"] = "NEEDS-HAND"
            out["notes"].insert(0, "none of the listed magazines exists in CDF (%s)"
                                % (",".join(mags) or "-"))
            return out
        p = collections.OrderedDict()
        p["pocket_type"] = "MAGAZINE_WELL"
        if well_key is not None:
            p["magazine_well"] = well_key
        elif rec["type"] != "GUN":
            p["rigid"] = True   # DDA convention for tool wells without a volume
        p["item_restriction"] = list(present)
        dm = exp.get("default")
        if dm in ctx.cdf.items:
            p["default_magazine"] = dm
        elif dm:
            out["notes"].append("default magazine %s is absent in CDF: no default_magazine" % dm)
        if dm and mags and dm != mags[0]:
            out["notes"].append("default_magazine %s (first non-speedloader) instead of the "
                                "first listed id %s" % (dm, mags[0]))
        new = [p]
        out["action"] = "AUTHOR"
    move_item_key(ctx, rec, out, "magazines", "pocket item_restriction replaces the BN list")
    move_item_key(ctx, rec, out, "magazine_well", "moves to the pocket's magazine_well")
    finish(out, rec, existing, new, ("MAGAZINE", "MAGAZINE_WELL"), ctx=ctx)
    return out


# ---------------------------------------------------------------------------------------------
# R4 TOOL-INT
# ---------------------------------------------------------------------------------------------

def plan_tool_int(ctx, rec, cls, exp, twin):
    """R4 TOOL-INT: `ammo` -> `tool_ammo`, MAGAZINE {ammo: max_charges}, drop max/initial."""
    out = new_record(rec, cls, twin, ctx)
    out["rule"] = "R4"
    out["renamed_keys"] = []
    raw, eff = rec["raw"], rec["eff"]
    for k in ("relative", "proportional"):
        if isinstance(raw.get(k), dict) and any(
                x in raw[k] for x in ("ammo", "max_charges", "initial_charges", "magazines")):
            out["action"] = "NEEDS-HAND"
            out["notes"].append("%s modifies ammo/max_charges/initial_charges, not resolved by "
                                "this tool" % k)
            return out
    want = exp["ammo"]
    shell = twin is not None and usable_twin(twin) is None
    if shell:
        out["notes"].append("DDA twin is an obsoletion shell (%s, pocket %s): authored from the "
                            "CDF fields (R7)" % (twin["file"], compact(pocket_list(twin["eff"]))))
    twin = usable_twin(twin)
    pk = pocket_list(twin["eff"]) if twin else []
    tshape = [p for p in pk if p.get("pocket_type") in ("MAGAZINE", "MAGAZINE_WELL")]
    tother = [p for p in pk if p.get("pocket_type") not in ("MAGAZINE", "MAGAZINE_WELL")]
    existing = pocket_list(eff)
    if tother:
        out["notes"].append("DDA twin also has non-magazine pockets (%s), not copied"
                            % ",".join(sorted(set(p.get("pocket_type", "CONTAINER")
                                                  for p in tother))))
    battery = "battery" in want
    new = []
    if tshape:
        out["action"] = "COPY-TWIN"
        tmag = [p for p in tshape if p.get("pocket_type") == "MAGAZINE"]
        if tmag:
            ammo_kinds(want, pocket_ammo(tmag), out)
        else:
            out["diff"].append("well(dda)")
            out["notes"].append("DDA twin carries a magazine well (%s) where CDF has integral "
                                "charges (max_charges %s)"
                                % (",".join(json.dumps(p.get("flag_restriction") or
                                                       p.get("item_restriction"))
                                            for p in tshape),
                                   ",".join(str(v) for v in want.values())))
        for p in copy.deepcopy(tshape):
            if p.get("pocket_type") == "MAGAZINE":
                got = sanitize([p], ctx, out)
                if not got:
                    out["action"] = "NEEDS-HAND"
                    out["notes"].insert(0, "no usable ammotype in the twin pocket (%s)" % "; ".join(
                        "%s: %s" % (d["id"], d["why"]) for d in out["dropped_ids"]
                        if d["where"] == "ammo_restriction"))
                    return out
                new += got
            else:
                fix_well_ids(p, ctx, out)
                if "item_restriction" in p and not p["item_restriction"]:
                    if p.get("flag_restriction"):
                        del p["item_restriction"]
                    else:
                        out["action"] = "NEEDS-HAND"
                        out["notes"].insert(0, "twin well item_restriction is empty after "
                                            "dropping ids CDF lacks")
                        return out
                new.append(p)
        if any(d["where"] == "item_restriction" for d in out["dropped_ids"]):
            out["diff"].append("mags-absent")
    else:
        if twin is not None:
            out["diff"].append("nopocket")
            out["notes"].append("DDA twin has no magazine pocket (tool_ammo %s, max_charges %s): "
                                "authored like a CDF-only item"
                                % (twin["eff"].get("tool_ammo"), twin["eff"].get("max_charges")))
        if battery:
            sib = battery_sibling(ctx, rec)
            if sib is None and getattr(ctx, "fallback", False):
                # HAND-FALLBACK: the plan's fallback for a CDF battery tool without size
                # evidence is the integral battery magazine of its own max_charges
                new = [collections.OrderedDict([
                    ("pocket_type", "MAGAZINE"), ("rigid", True),
                    ("ammo_restriction", collections.OrderedDict(want))])]
                out["diff"].append("fallback-integral")
                out["notes"].append("no size evidence for a battery well: integral MAGAZINE "
                                    "{battery: %s} (CDF-only integral battery tool shape)"
                                    % eff.get("max_charges"))
                out["action"] = "AUTHOR"
                new = sanitize(new, ctx, out)
                if not new:
                    out["action"] = "NEEDS-HAND"
                    return out
                sib = "done"
            if sib is None:
                out["action"] = "NEEDS-HAND"
                out["notes"].insert(0, "battery tool (max_charges %s): no DDA twin tool of the "
                                    "same size class found (copy-from, id stem, parent, "
                                    "max_charges+volume), size flag unknown; fallback would be "
                                    "integral MAGAZINE {battery: %s}"
                                    % (eff.get("max_charges"), eff.get("max_charges")))
                return out
            if sib != "done":
                sid, kind, sp, why = sib
            if sib == "done":
                pass
            elif kind == "WELL":
                p = copy.deepcopy(sp)
                fix_well_ids(p, ctx, out)
                new = [p]
                out["diff"].append("sibling-well")
                out["notes"].append("battery well authored from the DDA twin of `%s` (%s): "
                                    "flag_restriction %s, default_magazine %s; CDF has no "
                                    "magazine id of its own for this tool"
                                    % (sid, why, json.dumps(sp.get("flag_restriction")),
                                       sp.get("default_magazine")))
            else:
                new = [collections.OrderedDict([
                    ("pocket_type", "MAGAZINE"), ("rigid", True),
                    ("ammo_restriction", collections.OrderedDict(want))])]
                out["diff"].append("sibling-integral")
                out["notes"].append("twin of `%s` (%s) is an integral battery MAGAZINE (DDA %s): "
                                    "authored from CDF ammo and max_charges"
                                    % (sid, why, compact([sp])))
            out["action"] = "AUTHOR"
        else:
            if not want or any(n <= 0 for n in want.values()):
                out["action"] = "NEEDS-HAND"
                out["notes"].append("no usable ammo/max_charges to author a pocket")
                return out
            new = [collections.OrderedDict([
                ("pocket_type", "MAGAZINE"), ("rigid", True),
                ("ammo_restriction", collections.OrderedDict(want))])]
            out["action"] = "AUTHOR"
            new = sanitize(new, ctx, out)
            if not new:
                out["action"] = "NEEDS-HAND"
                out["notes"].insert(0, "no usable ammotype for the pocket (%s)" % "; ".join(
                    "%s: %s" % (d["id"], d["why"]) for d in out["dropped_ids"]
                    if d["where"] == "ammo_restriction"))
                return out
    # keys: ammo -> tool_ammo (alias kept by the CDF loader, // CDF:), max/initial charges go
    site = key_site(raw, eff, "ammo")
    if site == "inherited":
        src = inherited_from(ctx, raw, "ammo")
        out["inherited_keys"].append({"key": "ammo", "value": eff.get("ammo"), "from": src})
        out["notes"].append("`ammo` is inherited from `%s` (copy-from): renamed when that "
                            "object is ported" % src)
    elif site is not None:
        out["renamed_keys"].append({"from": "ammo", "to": "tool_ammo", "value": eff.get("ammo"),
                                    "site": site})
    move_item_key(ctx, rec, out, "max_charges",
                  "DDA: the magazine pocket carries the capacity (\"Redundant max charge value\")")
    move_item_key(ctx, rec, out, "initial_charges", "DDA: not kept on tools with a magazine "
                  "pocket (R4)")
    if rec["type"] == "ARMOR":
        out["notes"].append("type ARMOR (not TOOL_ARMOR) with ammo/max_charges: check the DDA "
                            "loader accepts tool_ammo here")
    finish(out, rec, existing, new, ("MAGAZINE", "MAGAZINE_WELL"), ctx=ctx)
    return out


# ---------------------------------------------------------------------------------------------
# R5 CHARGES, R6 ADAPTOR, R8 DDA-POCKET (chunk 1c)
# ---------------------------------------------------------------------------------------------

def pocket_prereqs(pockets, ctx):
    """What a twin's magazine pockets need that CDF lacks (plan 4.4): ammotypes (defined, with
    an existing default ammo item), item ids, flags, default_magazine. [] = copyable as is."""
    miss = []
    for p in pockets:
        ar = p.get("ammo_restriction")
        if isinstance(ar, dict):
            for at in ar:
                if at not in ctx.cdf.ammotypes:
                    miss.append("ammotype %s undefined" % at)
                elif ctx.cdf.ammotypes[at] not in ctx.cdf.items:
                    miss.append("ammotype %s has no default ammo item (default %r)"
                                % (at, ctx.cdf.ammotypes[at]))
        for key in ("item_restriction", "allowed_speedloaders"):
            for i in listify(p.get(key)):
                if i not in ctx.cdf.items:
                    miss.append("%s id %s absent" % (key, i))
        for f in listify(p.get("flag_restriction")):
            if ctx.cdf.flags and f not in ctx.cdf.flags:
                miss.append("flag %s undefined" % f)
        dm = p.get("default_magazine")
        if dm and dm not in ctx.cdf.items:
            miss.append("default_magazine %s absent" % dm)
    return miss


def plan_copy_pocket(ctx, rec, cls, twin, rule, missing_action):
    """Shared by R5 and R8: copy the twin's MAGAZINE / MAGAZINE_WELL pockets when every
    prerequisite exists in CDF, else `missing_action` and the missing names."""
    out = new_record(rec, cls, twin, ctx)
    out["rule"] = rule
    eff = rec["eff"]
    twin = usable_twin(twin)
    pk = pocket_list(twin["eff"]) if twin else []
    tshape = [p for p in pk if p.get("pocket_type") in ("MAGAZINE", "MAGAZINE_WELL")]
    if not tshape:
        out["action"] = missing_action
        out["notes"].append("DDA twin has no magazine pocket (or is an obsoletion shell): "
                            "nothing to copy")
        return out
    em = [p for p in pocket_list(eff) if p.get("pocket_type") in ("MAGAZINE", "MAGAZINE_WELL")]
    if em and rule == "R8":
        out["action"] = "KEEP"
        out["notes"].append("the CDF item already has its own explicit magazine pocket(s) "
                            "(TEST_DATA fixture); nothing to copy")
        if em != tshape:
            out["diff"].append("differs-from-twin")
            out["notes"].append("CDF pocket %s differs from the DDA twin's %s"
                                % (compact(em), compact(tshape)))
        return out
    miss = pocket_prereqs(tshape, ctx)
    if miss:
        out["action"] = missing_action
        out["diff"].append("prereq-missing")
        out["notes"].append("%s: %s" % (
            "stays on charges" if rule == "R5" else "left as it is",
            "; ".join(sorted(set(miss)))))
        out["missing"] = sorted(set(miss))
        return out
    new = copy.deepcopy(tshape)
    out["action"] = "COPY-TWIN"
    if rule == "R5":
        # the twin pocket takes the place of the item's own charge count
        charges = to_int(eff.get("max_charges"))
        tm = [p for p in tshape if p.get("pocket_type") == "MAGAZINE"]
        if len(tm) == 1 and isinstance(tm[0].get("ammo_restriction"), dict) \
                and len(tm[0]["ammo_restriction"]) == 1:
            at, n = next(iter(tm[0]["ammo_restriction"].items()))
            if to_int(n) != charges:
                out["diff"].append("cap")
                out["cap"] = [{"ammo": at, "cdf": charges, "dda": to_int(n)}]
        move_item_key(ctx, rec, out, "max_charges", "DDA twin carries the charge count in its "
                      "magazine pocket (R5)")
        move_item_key(ctx, rec, out, "initial_charges", "DDA twin has no initial_charges (R5)")
    # DDA names the ammo in `tool_ammo` (or `ammo`); the CDF item has neither
    tam = twin["eff"].get("tool_ammo", twin["eff"].get("ammo"))
    if tam is not None and not eff.get("tool_ammo") and not eff.get("ammo"):
        out["set_keys"]["tool_ammo"] = tam
    existing = pocket_list(eff)
    for p in new:
        if p.get("pocket_type") == "MAGAZINE_WELL":
            fix_well_ids(p, ctx, out)
    finish(out, rec, existing, new, ("MAGAZINE", "MAGAZINE_WELL"), ctx=ctx)
    return out


def plan_charges(ctx, rec, cls, exp, twin):
    """R5 CHARGES: DDA's MAGAZINE pocket when every ammotype (and well id/flag) exists in
    CDF, else the tool stays on `charges` (KEEP-CHARGES)."""
    return plan_copy_pocket(ctx, rec, cls, twin, "R5", "KEEP-CHARGES")


def plan_dda_pocket(ctx, rec, cls, exp, twin):
    """R8 DDA-POCKET: copy the DDA pocket when its flag / ammotype prerequisites exist in
    CDF, else leave the item and list it (LEAVE)."""
    return plan_copy_pocket(ctx, rec, cls, twin, "R8", "LEAVE")


def plan_adaptor(ctx, rec, cls, exp, twin):
    """R6 ADAPTOR: no edit. Rows with a DDA twin are the REVIEW rows; the golden covers them."""
    out = new_record(rec, cls, twin, ctx)
    out["rule"] = "R6"
    out["action"] = "KEEP"
    out["notes"].append("`magazine_adaptor` (%s) and `magazines_like` stay (DDA "
                        "item_factory.cpp:3673-3691, CDF item_factory.cpp:2913-2921); no edit"
                        % json.dumps(rec["eff"].get("magazine_adaptor")))
    if twin is not None:
        out["action"] = "KEEP-REVIEW"
        out["notes"].append("REVIEW row (DDA twin has the adaptor too): the golden matrix covers "
                            "it, the adaptor swaps the well restriction "
                            "(item_contents.cpp:359-369)")
    return out


def hand_fallback(ctx, rec, cls, exp, twin, planner, first):
    """NEEDS-HAND rows get a concrete, deterministic pocket so --apply is deterministic, but
    stay visibly flagged (action HAND-FALLBACK, listed in the report). The fallback is the
    item's own CDF shape: the planner re-run with no twin (CDF-only authoring; battery tools
    without size evidence take the integral MAGAZINE {battery: max_charges}); when even that
    cannot be authored the row is left as it is (empty pocket list, no key changes)."""
    ctx.fallback = True
    try:
        alt = planner(ctx, rec, cls, exp, None)
    finally:
        ctx.fallback = False
    reason = "; ".join(first["notes"]) or "no rule covers it"
    if alt["action"] in ("AUTHOR", "COPY-TWIN") and alt["pocket_data"]:
        alt["has_twin"] = first["has_twin"]
        alt["twin_file"] = first["twin_file"]
        alt["tags"] = first["tags"]
        alt["action"] = "HAND-FALLBACK"
        alt["fallback"] = "authored"
        desc = "authored from the CDF fields: %s" % compact(alt["pocket_data"])
    else:
        keep = {k: first[k] for k in ("id", "class", "has_twin", "source_file", "twin_file",
                                      "rule", "tags", "synth_lost")}
        alt = new_record(rec, cls, twin, ctx)
        alt.update(keep)
        alt["action"] = "HAND-FALLBACK"
        alt["fallback"] = "left-as-is"
        desc = "left as it is (no pocket authored, no key changes)"
    alt["hand_reason"] = reason
    alt["diff"] = list(dict.fromkeys(first["diff"] + alt["diff"]))
    alt["ammo_diff"] = first.get("ammo_diff") or alt["ammo_diff"]
    alt["notes"] = ["HAND-FALLBACK (was NEEDS-HAND): %s => %s" % (reason, desc)] + \
        [n for n in alt["notes"] if n not in first["notes"]]
    return alt


# class -> planner. R7 is "author when there is no twin" inside each planner; R9 is the
# STORAGE/EXPLICIT handling in finish() and carry_pockets().
PLANNERS = collections.OrderedDict([
    ("MAG", plan_mag),
    ("GUN-INT", plan_gun_int),
    ("TOOL-INT", plan_tool_int),
    ("WELL", plan_well),
    ("ADAPTOR", plan_adaptor),
    ("CHARGES", plan_charges),
    ("DDA-POCKET", plan_dda_pocket),
])
HAND_FALLBACK_CLASSES = ("MAG", "GUN-INT", "TOOL-INT", "WELL")
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
            if out["action"] == "NEEDS-HAND" and cls in HAND_FALLBACK_CLASSES:
                out = hand_fallback(ctx, rec, cls, exp, twin, planner, out)
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
    L.append("# P3-2 magazine port plan (%s)\n" % ",".join(sorted(by_cls)))
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
    hand = [r for r in records if r["action"] in ("NEEDS-HAND", "HAND-FALLBACK")]
    twin_rows = sum(1 for r in records if r["has_twin"])
    hand_twin = [r for r in hand if r["has_twin"]]
    # the plan's 18 `ammo`-kind rows are the R1 + R2 (MAG, GUN-INT) rows; R4 TOOL-INT rows with
    # an ammo difference are reported in the "Ammo differences" section, not counted here
    r1_ammo = [r for r in records if r["class"] in ("MAG", "GUN-INT") and r["has_twin"]
               and "ammo" in r["diff"]]
    need = len(hand_twin) + len(r1_ammo)
    L.append("## Stop rule\n")
    L.append("Twin rows that need a hand rule over the %d twin rows of this report: %d "
             "HAND-FALLBACK twin rows (of %d HAND-FALLBACK rows in all) + %d R1/R2 `ammo`-kind "
             "MAG/GUN-INT rows (CDF-only ammotype lost in the pocket, owner decision, listed "
             "below) = %d, "
             "%.1f%%. Limit 5%% of 725 = 36: **%s**.\n"
             % (twin_rows, len(hand_twin), len(hand), len(r1_ammo), need,
                100.0 * need / max(1, twin_rows),
                "under the limit" if need < 36 else "OVER the limit"))
    if hand:
        L.append("### HAND-FALLBACK rows (default applied, still flagged: hand review wanted)\n")
        L.append("| id | class | twin | fallback | proposed pocket | reason |\n|---|---|---|---|---|---|")
        for r in hand:
            L.append("| `%s` | %s | %s | %s | %s | %s |" % (
                r["id"], r["class"], "yes" if r["has_twin"] else "no", r.get("fallback", "-"),
                compact(r["pocket_data"]).replace("|", "/"),
                r.get("hand_reason", "; ".join(r["notes"])).replace("|", "/")))
        L.append("")
    if r1_ammo:
        L.append("### R1/R2 `ammo`-kind MAG/GUN-INT rows (owner decision: DDA's pocket drops the CDF-only "
                 "ammotype)\n")
        L.append("| id | CDF-only ammo | DDA-only ammo |\n|---|---|---|")
        for r in r1_ammo:
            L.append("| `%s` | %s | %s |" % (r["id"],
                                             ",".join(r["ammo_diff"].get("cdf_only", [])) or "-",
                                             ",".join(r["ammo_diff"].get("dda_only", [])) or "-"))
        L.append("")
    r5 = [r for r in records if r["rule"] == "R5"]
    if r5:
        L.append("## R5 CHARGES (tools with max_charges and no ammo)\n")
        cp = [r for r in r5 if r["action"] == "COPY-TWIN"]
        kp = [r for r in r5 if r["action"] == "KEEP-CHARGES"]
        L.append("%d rows: %d take DDA's pocket (COPY-TWIN), %d stay on `charges` "
                 "(KEEP-CHARGES). The plan estimated 37 / 16; the difference is the ids "
                 "listed below (e.g. `fire_drill*`: `notched_plank`/`notched_stick` absent).\n"
                 % (len(r5), len(cp), len(kp)))
        if kp:
            L.append("| stays on charges | missing |\n|---|---|")
            for r in kp:
                L.append("| `%s` | %s |" % (r["id"], "; ".join(r.get("missing", ["-"]))))
            L.append("")
        mi = collections.OrderedDict()
        for r in kp:
            for m_ in r.get("missing", []):
                mi.setdefault(m_, []).append(r["id"])
        if mi:
            L.append("Missing prerequisites, distinct: " + "; ".join(
                "%s (%d item(s))" % (k, len(v)) for k, v in mi.items()) + "\n")
    r6 = [r for r in records if r["rule"] == "R6"]
    if r6:
        L.append("## R6 ADAPTOR\n")
        L.append("%d rows, no edit (`magazine_adaptor` and `magazines_like` stay): KEEP=%d, "
                 "KEEP-REVIEW=%d (golden matrix covers: %s).\n"
                 % (len(r6), sum(1 for r in r6 if r["action"] == "KEEP"),
                    sum(1 for r in r6 if r["action"] == "KEEP-REVIEW"),
                    ", ".join("`%s`" % r["id"] for r in r6 if r["action"] == "KEEP-REVIEW")))
    r8 = [r for r in records if r["rule"] == "R8"]
    if r8:
        L.append("## R8 DDA-POCKET\n")
        L.append("| id | action | pocket | missing / note |\n|---|---|---|---|")
        for r in r8:
            L.append("| `%s` | %s | %s | %s |" % (
                r["id"], r["action"], compact(r["pocket_data"]).replace("|", "/"),
                "; ".join(r.get("missing", []) or r["notes"]).replace("|", "/") or "-"))
        L.append("")
    st = [r for r in records if "STORAGE" in r["tags"] or "EXPLICIT" in r["tags"]]
    L.append("## R9 STORAGE / EXPLICIT rows\n")
    L.append("Order follows synthesize_pockets: magazine / well pocket(s), (MOD), then the "
             "carry pocket(s) last. EXPLICIT rows keep the existing pockets in front and "
             "get the magazine pocket appended (hand look). %d rows (STORAGE %d, EXPLICIT %d); "
             "the census counted 14 STORAGE rows because it tags `armor_wyrm` (`storage` "
             "\"0 L\" inherited), which has no worn storage.\n"
             % (len(st), sum(1 for r in st if "STORAGE" in r["tags"]),
                sum(1 for r in st if "EXPLICIT" in r["tags"])))
    L.append("| id | class | action | tags | emitted pocket list | hand review |\n|---|---|---|---|---|---|")
    for r in st:
        review = "yes: EXPLICIT merge" if "EXPLICIT" in r["tags"] else (
            "yes: HAND-FALLBACK" if r["action"] == "HAND-FALLBACK" else "no")
        L.append("| `%s` | %s | %s | %s | %s | %s |" % (
            r["id"], r["class"], r["action"], ",".join(r["tags"]),
            compact(r["pocket_data"]).replace("|", "/"), review))
    L.append("")
    sl = collections.Counter(k for r in records for k in r["synth_lost"])
    L.append("Pockets synthesized by BN today that an explicit pocket_data suppresses "
             "(`synth_lost`, rows without explicit pocket_data): %s. DDA synthesizes the MOD "
             "pocket for guns itself; in CDF `synthesize_pockets` returns before it when "
             "`explicit_pockets` is set, so P3-1 must keep synthesizing it or `--apply` must "
             "emit `{ \"pocket_type\": \"MOD\" }` (owner decision).\n"
             % (", ".join("%s=%d" % kv for kv in sorted(sl.items())) or "none"))
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
    L.append("## R3 CDF-only magazine ids kept in DDA wells (id, item)\n")
    kept = [(x, r["id"]) for r in records for x in r.get("well_diff", {}).get("cdf_added", [])]
    if kept:
        by_id = collections.OrderedDict()
        for x, i in kept:
            by_id.setdefault(x, []).append(i)
        L.append("%d (item, id) pairs, %d distinct ids, %d items.\n"
                 % (len(kept), len(by_id), len(set(i for _x, i in kept))))
        L.append("| magazine id | items |\n|---|---|")
        for x, items in by_id.items():
            L.append("| `%s` | %s |" % (x, ", ".join("`%s`" % i for i in items)))
    else:
        L.append("none")
    L.append("")
    gone = collections.OrderedDict()
    for r in records:
        for d in r["dropped_ids"]:
            if d["where"] in ("item_restriction", "default_magazine"):
                gone.setdefault(d["id"], []).append(r["id"])
    if gone or any(r["rule"] in ("R3", "R4") for r in records):
        L.append("## R3/R4 item ids dropped from copied wells (absent in CDF, distinct)\n")
        if gone:
            L.append("%d distinct ids.\n" % len(gone))
            L.append("| dropped id | items |\n|---|---|")
            for x, items in sorted(gone.items()):
                L.append("| `%s` | %s |" % (x, ", ".join("`%s`" % i for i in items)))
        else:
            L.append("none")
        L.append("")
    own = [r for r in records if any(d["where"] == "magazines" for d in r["dropped_ids"])]
    if own:
        L.append("CDF `magazines` ids that do not exist in CDF (left out of the pocket): %s\n"
                 % "; ".join("`%s`: %s" % (r["id"], ",".join(
                     d["id"] for d in r["dropped_ids"] if d["where"] == "magazines"))
                             for r in own))
    ren = [r for r in records if r.get("renamed_keys")]
    if any(r["rule"] == "R4" for r in records):
        L.append("## R4 key moves\n")
        L.append("`ammo` renamed to `tool_ammo` on %d items; `max_charges` dropped on %d, "
                 "`initial_charges` on %d (inherited ones are in the inherited-keys section).\n"
                 % (len(ren),
                    sum(1 for r in records if r["rule"] == "R4" and any(
                        d["key"] == "max_charges" for d in r["dropped_keys"])),
                    sum(1 for r in records if r["rule"] == "R4" and any(
                        d["key"] == "initial_charges" for d in r["dropped_keys"]))))
        sib = [r for r in records if any(d.startswith("sibling") for d in r["diff"])]
        if sib:
            L.append("Battery tools authored from the nearest DDA twin tool:\n")
            L.append("| id | pocket | evidence |\n|---|---|---|")
            for r in sib:
                L.append("| `%s` | %s | %s |" % (
                    r["id"], compact(r["pocket_data"]).replace("|", "/"),
                    next((n for n in r["notes"] if n.startswith(("twin of", "battery well"))),
                         "-").replace("|", "/")))
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
