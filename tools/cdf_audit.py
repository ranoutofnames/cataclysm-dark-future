#!/usr/bin/env python3
"""CDF data audit: static checks over CDF's JSON against DDA (report only).

Reads JSON from a CDF tree and (for A1) a DDA checkout, resolves copy-from,
and prints TSV rows:

    check  severity  id  detail  source

Checks (PL-62 AT1):
  A1 pocket-diff    DDA-only pocket key or flag = error, CDF-only = info.
                    Also the three per-portion fields named by VESTDATA1
                    (cover_vitals, volume_encumber_modifier, cover_ranged).
  A2 dead-plate     an insert-only item (ABLATIVE* / CANT_WEAR, or a BN
                    ARMOR_PLATE, which must fit an ablative pocket) that no
                    pocket accepts (restriction and capacity) = error.
  A3 dead-pocket    a restricted pocket that no item can fill = warning.
  A4 missing-id     item id named by an item group, profession, recipe
                    component, item_restriction or migration does not exist
                    (migration sources count as existing: they are rewritten).
  A5 undefined-flag a flag used on an item or in flag_restriction has no
                    json_flag definition = error.
  A6 migration      a migration target is missing, a chain is longer than 3,
                    or a chain cycles = error.
  A7 kit-container  a group/profession entry whose container has no pocket
                    that can hold the entry = warning (AT6 is the real spawn
                    test).

Usage:
  py -3 -I tools/cdf_audit.py [--cdf DIR] [--dda DIR] [--out FILE]
  py -3 -I tools/cdf_audit.py --section
  py -3 -I tools/cdf_audit.py --selftest [--fixture DIR]

--cdf      CDF checkout (default: the repository this file lives in).
--dda      DDA checkout (default: env CDF_DDA_SRC, else C:\\CDF\\CDDA SRC).
--out      write the TSV to FILE instead of stdout.
--section  print one line "data-audit<TAB>PASS|FAIL<TAB>N findings" for AT0;
           exit 0 on PASS, 1 on FAIL. AT0 can also import section(cdf, dda).
--selftest run the fixture tree: the seeded tree must match seeded/expected.tsv
           and the clean tree must report nothing (exit 1 on a mismatch).
--fixture  fixture root for --selftest (default tests/data/audit_fixtures).

The copy-from resolver is the approximation PLAN.md section 8.3 warns about:
it handles copy-from, extend, delete, relative and proportional for the fields
compared here, and generic_factory quirks that are not JSON-visible are not
modelled. CDF numbers are never compared; only presence of keys/flags is.

Only data/json is audited by default (mods are the separate check-mods
section). Standard library only.
"""
import argparse
import copy
import json
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
# Reuse the probe's loader (PL-62 AT1 "read first"); do not rewrite it.
from cdf_dda_probe import iter_json  # noqa: E402

DEFAULT_CDF = os.path.dirname(HERE)
DEFAULT_DDA = os.environ.get("CDF_DDA_SRC", r"C:\CDF\CDDA SRC")
DEFAULT_FIXTURE = os.path.join(DEFAULT_CDF, "tests", "data", "audit_fixtures")

ITEM_TYPES = {
    "ITEM", "GENERIC", "ARMOR", "TOOL_ARMOR", "TOOL", "AMMO", "GUN", "GUNMOD",
    "MAGAZINE", "COMESTIBLE", "BOOK", "PET_ARMOR", "BATTERY", "ENGINE", "WHEEL",
    "BIONIC_ITEM", "TOOLMOD", "CONTAINER",
}
SEVERITY_ORDER = {"error": 0, "warning": 1, "info": 2}
PORTION_FIELDS = ("cover_vitals", "volume_encumber_modifier", "cover_ranged")
HEADER = ["check", "severity", "id", "detail", "source"]


# ---------------------------------------------------------------- small utils

def as_list(value):
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def item_id(obj):
    ident = obj.get("id")
    if isinstance(ident, str):
        return ident
    ident = obj.get("ident")
    if isinstance(ident, str):
        # A few BN-era objects still use "ident".
        return ident
    return None


def norm_path(path):
    return path.replace("\\", "/")


def line_of(root, relpath, needle):
    """1-based line of the first line matching needle (regex) in relpath."""
    path = os.path.join(root, relpath.replace("/", os.sep))
    try:
        with open(path, encoding="utf-8", errors="ignore") as f:
            for number, line in enumerate(f, 1):
                if needle.search(line):
                    return number
    except OSError:
        pass
    return 1


def id_line(root, relpath, ident):
    return line_of(root, relpath,
                   re.compile(r'"(?:id|ident|result)"\s*:\s*"'
                              + re.escape(ident) + r'"'))


UNITS = {
    "ml": 1.0, "milliliter": 1.0, "milliliters": 1.0, "cm3": 1.0,
    "l": 1000.0, "liter": 1000.0, "liters": 1000.0,
    "g": 1.0, "gram": 1.0, "grams": 1.0, "kg": 1000.0,
    "mg": 0.001,
    "mm": 0.1, "cm": 1.0, "m": 100.0, "in": 2.54,
}
QUANTITY_RE = re.compile(r"^\s*(-?[0-9.]+)\s*([A-Za-z0-9]+)?\s*$")


def quantity(value):
    """Return (number, unit) for 12, "2000 ml" or None."""
    if isinstance(value, (int, float)):
        return float(value), None
    if not isinstance(value, str):
        return None
    match = QUANTITY_RE.match(value)
    if not match:
        return None
    return float(match.group(1)), (match.group(2) or "").lower()


def to_base(value, base_unit):
    """Convert a volume/weight/length value to ml/g/cm."""
    parsed = quantity(value)
    if parsed is None:
        return None
    number, unit = parsed
    if not unit:
        return number
    factor = UNITS.get(unit)
    if factor is None:
        return None
    return number * factor


def add_quantity(left, right):
    a = quantity(left)
    b = quantity(right)
    if a is None or b is None:
        return left
    if a[1] and b[1] and a[1] != b[1]:
        fa, fb = UNITS.get(a[1]), UNITS.get(b[1])
        if fa is None or fb is None:
            return left
        return "%g %s" % (a[0] + b[0] * fb / fa, b[1])
    unit = b[1] or a[1] or ""
    return "%g %s" % (a[0] + b[0], unit) if unit else a[0] + b[0]


def mul_quantity(value, factor):
    a = quantity(value)
    if a is None:
        return value
    unit = a[1] or ""
    return "%g %s" % (a[0] * factor, unit) if unit else a[0] * factor


# ---------------------------------------------------------------- tree loader

class Tree:
    """One JSON tree (CDF or DDA) with items, groups and definitions."""

    def __init__(self, root, label, dirs=("data/json",)):
        self.root = root
        self.label = label
        self.dirs = [os.path.join(root, d) for d in dirs]
        self.objects = []          # (relpath, obj)
        self.items = {}            # id -> (obj, relpath)
        self.abstracts = {}        # id -> (obj, relpath)
        self.flags = set()
        self.requirements = set()
        self.groups = {}           # id -> (obj, relpath)
        self.professions = []      # (obj, relpath)
        self.recipes = []          # (obj, relpath)
        self.migrations = []       # (obj, relpath)
        self.parse_errors = []
        self._resolved = {}
        self._resolving = set()
        self._load()

    def _dirs(self):
        for path in self.dirs:
            if os.path.isdir(path):
                yield path
            else:
                self.parse_errors.append(norm_path(os.path.relpath(path, self.root)))

    def _load(self):
        for path in self._dirs():
            for relpath, obj in iter_json(path, self.root):
                if obj is None:
                    self.parse_errors.append(norm_path(relpath))
                    continue
                rel = norm_path(relpath)
                self.objects.append((rel, obj))
                kind = obj.get("type")
                ident = obj.get("id")
                abstract_name = obj.get("abstract")
                if isinstance(abstract_name, str):
                    # DDA/BN item abstracts use "abstract": "id", not type.
                    self.abstracts.setdefault(abstract_name, (obj, rel))
                if kind == "json_flag":
                    for name in as_list(ident):
                        if isinstance(name, str):
                            self.flags.add(name)
                elif kind == "requirement":
                    for name in as_list(ident):
                        if isinstance(name, str):
                            self.requirements.add(name)
                elif kind == "item_group":
                    for name in as_list(ident):
                        if isinstance(name, str):
                            self.groups.setdefault(name, (obj, rel))
                elif kind == "profession":
                    self.professions.append((obj, rel))
                elif kind == "recipe":
                    self.recipes.append((obj, rel))
                elif kind == "MIGRATION":
                    self.migrations.append((obj, rel))
                elif kind == "abstract":
                    for name in as_list(ident):
                        if isinstance(name, str):
                            self.abstracts.setdefault(name, (obj, rel))
                elif kind in ITEM_TYPES:
                    for name in as_list(ident):
                        if isinstance(name, str):
                            self.items[name] = (obj, rel)

    def source(self, relpath, ident, obj=None):
        line = id_line(self.root, relpath, ident)
        return "%s:%s:%d" % (self.label, relpath, line)

    def _definition(self, ident):
        found = self.items.get(ident) or self.abstracts.get(ident)
        return found[0] if found else None

    def resolved(self, ident):
        if ident in self._resolved:
            return self._resolved[ident]
        definition = self._definition(ident)
        if definition is None:
            return None
        result = self._resolve_obj(definition, ())
        self._resolved[ident] = result
        return result

    def _resolve_obj(self, obj, chain):
        base_from = obj.get("copy-from")
        result = {}
        if isinstance(base_from, str) and base_from not in chain:
            base = self._definition(base_from)
            if base is not None:
                result = copy.deepcopy(self._resolve_obj(base, chain + (base_from,)))
        for key, value in obj.items():
            if key in ("copy-from", "extend", "delete", "relative",
                       "proportional", "//"):
                continue
            result[key] = copy.deepcopy(value)
        for key, value in (obj.get("extend") or {}).items():
            if isinstance(value, list):
                result[key] = copy.deepcopy(result.get(key) or []) + copy.deepcopy(value)
            elif isinstance(value, dict):
                merged = copy.deepcopy(result.get(key) or {})
                merged.update(copy.deepcopy(value))
                result[key] = merged
        for key, value in (obj.get("delete") or {}).items():
            if isinstance(value, list) and isinstance(result.get(key), list):
                for entry in value:
                    if entry in result[key]:
                        result[key].remove(entry)
            elif isinstance(result.get(key), dict):
                for name in as_list(value):
                    result[key].pop(name, None)
        for key, value in (obj.get("relative") or {}).items():
            if key in result:
                result[key] = add_quantity(result[key], value)
        for key, value in (obj.get("proportional") or {}).items():
            if key in result:
                if isinstance(value, (int, float)) and isinstance(result[key], (int, float)):
                    result[key] = result[key] * value
                elif isinstance(value, (int, float)):
                    result[key] = mul_quantity(result[key], value)
        return result

    def item_flags(self, ident):
        obj = self.resolved(ident) or {}
        return {flag for flag in as_list(obj.get("flags")) if isinstance(flag, str)}

    def pockets(self, ident):
        obj = self.resolved(ident) or {}
        found = [p for p in as_list(obj.get("pocket_data")) if isinstance(p, dict)]
        contains = obj.get("contains")
        container_data = obj.get("container_data")
        if contains is None and isinstance(container_data, dict):
            contains = container_data.get("contains")
        if not found and contains is not None:
            # BN/CDF CONTAINER items still use the legacy "contains" volume.
            found = [{"pocket_type": "CONTAINER",
                      "max_contains_volume": contains}]
        return found

    def raw_pockets(self, ident):
        obj = self.resolved(ident) or {}
        return [p for p in as_list(obj.get("pocket_data")) if isinstance(p, dict)]

    def item_type(self, ident):
        return (self.resolved(ident) or {}).get("type")

    def item_facts(self, ident):
        obj = self.resolved(ident) or {}
        return {
            "flags": self.item_flags(ident),
            "volume": to_base(obj.get("volume"), "ml"),
            "weight": to_base(obj.get("weight"), "g"),
            "length": to_base(obj.get("longest_side"), "cm"),
        }

    def migration_sources(self):
        sources = set()
        for obj, _rel in self.migrations:
            for ident in as_list(obj.get("id")):
                if isinstance(ident, str):
                    sources.add(ident)
        return sources

    def live_ids(self):
        return set(self.items) | self.migration_sources()


# ---------------------------------------------------------------- acceptance

def pocket_accepts(pocket, facts, ident):
    """True if the pocket accepts the item on restriction and capacity."""
    flag_restriction = [f for f in as_list(pocket.get("flag_restriction"))
                        if isinstance(f, str)]
    item_restriction = [i for i in as_list(pocket.get("item_restriction"))
                        if isinstance(i, str)]
    if flag_restriction and not (set(flag_restriction) & facts["flags"]):
        return False
    if item_restriction and ident not in item_restriction:
        return False
    max_volume = to_base(pocket.get("max_contains_volume"), "ml")
    if max_volume is not None and facts["volume"] is not None:
        if facts["volume"] > max_volume + 1e-6:
            return False
    max_weight = to_base(pocket.get("max_contains_weight"), "g")
    if max_weight is not None and facts["weight"] is not None:
        if facts["weight"] > max_weight + 1e-6:
            return False
    max_length = to_base(pocket.get("max_item_length"), "cm")
    if max_length is not None and facts["length"] is not None:
        if facts["length"] > max_length + 1e-6:
            return False
    return True


def restriction_label(pocket):
    parts = []
    if pocket.get("flag_restriction"):
        parts.append("flag_restriction=" + ",".join(str(x) for x in as_list(pocket["flag_restriction"])))
    if pocket.get("item_restriction"):
        parts.append("item_restriction=" + ",".join(str(x) for x in as_list(pocket["item_restriction"])))
    return " ".join(parts)


def restriction_candidates(pocket, tree, facts_by_id):
    """Items that pass the pocket's restriction (capacity not applied)."""
    flag_restriction = [f for f in as_list(pocket.get("flag_restriction"))
                        if isinstance(f, str)]
    item_restriction = [i for i in as_list(pocket.get("item_restriction"))
                        if isinstance(i, str)]
    candidates = set(tree.items)
    if flag_restriction:
        candidates = {i for i in candidates
                      if set(flag_restriction) & facts_by_id[i]["flags"]}
    if item_restriction:
        candidates &= {i for i in candidates if i in item_restriction}
    return candidates


def pocket_fillers(container, tree, facts_by_id):
    """Map pocket index -> number of items that pocket accepts."""
    counts = {}
    for index, pocket in enumerate(tree.pockets(container)):
        count = 0
        for ident in restriction_candidates(pocket, tree, facts_by_id):
            if pocket_accepts(pocket, facts_by_id[ident], ident):
                count += 1
        counts[index] = count
    return counts


# ---------------------------------------------------------------- entries

def iter_group_entries(obj, seen=None):
    """Yield every entry dict of an item_group, at any nesting depth."""
    seen = set() if seen is None else seen
    if id(obj) in seen or not isinstance(obj, dict):
        return
    seen.add(id(obj))
    for entry in as_list(obj.get("entries")) + as_list(obj.get("items")):
        if isinstance(entry, dict):
            yield entry
            for key in ("collection", "distribution", "entries", "items"):
                for nested in as_list(entry.get(key)):
                    if isinstance(nested, dict):
                        yield from iter_group_entries(nested, seen)
        elif isinstance(entry, str):
            yield {"item": entry}


def group_item_ids(obj, seen=None):
    """All item ids named by a group, recursively (for contents-group)."""
    seen = set() if seen is None else seen
    if id(obj) in seen or not isinstance(obj, dict):
        return []
    seen.add(id(obj))
    found = []
    for entry in iter_group_entries(obj):
        for key in ("item", "container-item", "contents-item"):
            for ident in as_list(entry.get(key)):
                if isinstance(ident, str):
                    found.append(ident)
    return found


def walk_profession_entries(profession):
    """Yield (entry, relpath) for starting-item entries of a profession."""
    items = profession.get("items")
    if not isinstance(items, dict):
        return
    for slot in items.values():
        if isinstance(slot, list):
            continue
        if isinstance(slot, dict):
            for entry in as_list(slot.get("entries")):
                if isinstance(entry, dict):
                    yield entry


def strip_variant(ident):
    return ident.split(":", 1)[0]


class RowSink:
    def __init__(self):
        self.rows = {}
        self.order = []

    def add(self, check, severity, ident, detail, source):
        key = (check, severity, ident, detail, source)
        if key not in self.rows:
            self.rows[key] = True
            self.order.append(key)

    def sorted_rows(self):
        return sorted(self.order, key=lambda r: (
            r[0], SEVERITY_ORDER.get(r[1], 9), r[2], r[3], r[4]))


# ---------------------------------------------------------------- checks

def check_a1(cdf, dda, sink):
    for ident in sorted(set(cdf.items) & set(dda.items)):
        cdef, crel = cdf.items[ident]
        ddef, drel = dda.items[ident]
        cobj = cdf.resolved(ident) or {}
        dobj = dda.resolved(ident) or {}
        csource = cdf.source(crel, ident)
        dsource = dda.source(drel, ident)
        cpockets = cdf.raw_pockets(ident)
        dpockets = dda.raw_pockets(ident)
        if len(cpockets) != len(dpockets):
            sink.add("A1", "error", ident,
                     "pocket count CDF=%d DDA=%d" % (len(cpockets), len(dpockets)),
                     dsource)
        for index in range(min(len(cpockets), len(dpockets))):
            ckeys = {k for k in cpockets[index] if k != "//"}
            dkeys = {k for k in dpockets[index] if k != "//"}
            for key in sorted(dkeys - ckeys):
                sink.add("A1", "error", ident,
                         "pocket[%d] key %s only in DDA" % (index, key), dsource)
            for key in sorted(ckeys - dkeys):
                sink.add("A1", "info", ident,
                         "pocket[%d] key %s only in CDF" % (index, key), csource)
        cflags = cdf.item_flags(ident)
        dflags = dda.item_flags(ident)
        for flag in sorted(dflags - cflags):
            sink.add("A1", "error", ident, "flag %s only in DDA" % flag, dsource)
        for flag in sorted(cflags - dflags):
            sink.add("A1", "info", ident, "flag %s only in CDF" % flag, csource)
        # VESTDATA1's per-portion omissions (the doc names these three fields).
        cportions = [p for p in as_list(cobj.get("armor_portion_data"))
                     if isinstance(p, dict)]
        dportions = [p for p in as_list(dobj.get("armor")) if isinstance(p, dict)]
        if cportions and dportions:
            cbykey = {}
            for portion in cportions:
                key = (tuple(sorted(as_list(portion.get("covers")))),
                       tuple(sorted(as_list(portion.get("specifically_covers")))))
                cbykey.setdefault(key, portion)
            for portion in dportions:
                key = (tuple(sorted(as_list(portion.get("covers")))),
                       tuple(sorted(as_list(portion.get("specifically_covers")))))
                cportion = cbykey.get(key)
                if cportion is None:
                    continue
                for field in PORTION_FIELDS:
                    if field in portion and field not in cportion:
                        sink.add("A1", "error", ident,
                                 "armor portion %s field %s only in DDA"
                                 % ("+".join(key[1]) or "+".join(key[0]), field),
                                 dsource)


def check_a2(cdf, sink):
    facts = {ident: cdf.item_facts(ident) for ident in cdf.items}
    pockets = []
    for container in cdf.items:
        for index, pocket in enumerate(cdf.pockets(container)):
            pockets.append((container, index, pocket))
    for ident in sorted(cdf.items):
        flags = facts[ident]["flags"]
        armor_plate = "ARMOR_PLATE" in flags
        if not (armor_plate or any(f.startswith("ABLATIVE") for f in flags)
                or "CANT_WEAR" in flags):
            continue
        accepted = False
        for container, _index, pocket in pockets:
            # BN's ARMOR_PLATE items are plate-carrier plates: they must fit
            # an ablative (plate) pocket, not just any container.
            if armor_plate and not pocket.get("ablative"):
                continue
            if pocket_accepts(pocket, facts[ident], ident):
                accepted = True
                break
        if not accepted:
            _obj, rel = cdf.items[ident]
            sink.add("A2", "error", ident,
                     "insert-only item accepted by no pocket (flags %s)"
                     % ",".join(sorted(flags)), cdf.source(rel, ident))


def check_a3(cdf, sink):
    facts = {ident: cdf.item_facts(ident) for ident in cdf.items}
    for container in sorted(cdf.items):
        _obj, rel = cdf.items[container]
        for index, pocket in enumerate(cdf.pockets(container)):
            if not (pocket.get("flag_restriction") or pocket.get("item_restriction")):
                continue
            candidates = restriction_candidates(pocket, cdf, facts)
            if not candidates:
                sink.add("A3", "warning", container,
                         "pocket[%d] %s matches no item"
                         % (index, restriction_label(pocket)),
                         cdf.source(rel, container))
                continue
            if not any(pocket_accepts(pocket, facts[item], item) for item in candidates):
                sink.add("A3", "warning", container,
                         "pocket[%d] %s: no matching item fits capacity"
                         % (index, restriction_label(pocket)),
                         cdf.source(rel, container))


def missing_item(cdf, live, ident):
    return strip_variant(ident) not in live and ident not in live


def check_a4(cdf, sink):
    live = cdf.live_ids()
    for group_id in sorted(cdf.groups):
        obj, rel = cdf.groups[group_id]
        source = cdf.source(rel, group_id)
        for entry in iter_group_entries(obj):
            for key in ("item", "container-item", "contents-item"):
                for ident in as_list(entry.get(key)):
                    if not isinstance(ident, str):
                        continue
                    if missing_item(cdf, live, ident):
                        sink.add("A4", "error", ident,
                                 "missing item id in item_group %s (%s)"
                                 % (group_id, key), source)
            for key in ("group", "contents-group"):
                for ident in as_list(entry.get(key)):
                    if (isinstance(ident, str) and ident not in cdf.groups
                            and ident != "EMPTY_GROUP"):
                        sink.add("A4", "error", ident,
                                 "missing item_group id in item_group %s (%s)"
                                 % (group_id, key), source)
    for profession, rel in cdf.professions:
        pid = item_id(profession) or "profession"
        source = cdf.source(rel, pid)
        items = profession.get("items")
        if isinstance(items, list):
            for ident in items:
                if isinstance(ident, str) and missing_item(cdf, live, ident):
                    sink.add("A4", "error", ident,
                             "missing item id in profession %s" % pid, source)
        elif isinstance(items, dict):
            for slot in items.values():
                if isinstance(slot, list):
                    for ident in slot:
                        if isinstance(ident, str) and missing_item(cdf, live, ident):
                            sink.add("A4", "error", ident,
                                     "missing item id in profession %s" % pid, source)
                elif isinstance(slot, dict):
                    for ident in as_list(slot.get("items")):
                        if isinstance(ident, str) and missing_item(cdf, live, ident):
                            sink.add("A4", "error", ident,
                                     "missing item id in profession %s" % pid, source)
        for entry in walk_profession_entries(profession):
            for key in ("item", "container-item", "contents-item"):
                for ident in as_list(entry.get(key)):
                    if isinstance(ident, str) and missing_item(cdf, live, ident):
                        sink.add("A4", "error", ident,
                                 "missing item id in profession %s (%s)"
                                 % (pid, key), source)
    for recipe, rel in cdf.recipes:
        rid = recipe.get("result") or recipe.get("id") or "recipe"
        if not isinstance(rid, str):
            rid = "recipe"
        source = cdf.source(rel, rid)
        for ident in recipe_component_ids(recipe.get("components")):
            if ident in cdf.requirements or ident == "LIST":
                continue
            if missing_item(cdf, live, ident):
                sink.add("A4", "error", ident,
                         "missing item id in recipe component for %s" % rid,
                         source)
    for container in sorted(cdf.items):
        _obj, rel = cdf.items[container]
        for index, pocket in enumerate(cdf.pockets(container)):
            for ident in as_list(pocket.get("item_restriction")):
                if isinstance(ident, str) and missing_item(cdf, live, ident):
                    sink.add("A4", "error", ident,
                             "missing item id in item_restriction of %s pocket[%d]"
                             % (container, index), cdf.source(rel, container))
    for migration, rel in cdf.migrations:
        from_ids = [i for i in as_list(migration.get("id")) if isinstance(i, str)]
        mid = ",".join(from_ids) or "MIGRATION"
        source = cdf.source(rel, from_ids[0] if from_ids else mid)
        for key in ("replace", "contents"):
            for value in as_list(migration.get(key)):
                if isinstance(value, dict):
                    value = value.get("id")
                if isinstance(value, str) and missing_item(cdf, live, value):
                    sink.add("A4", "error", value,
                             "missing migration target in %s (%s)" % (mid, key),
                             source)


def recipe_component_ids(value):
    found = []
    if isinstance(value, str):
        return found
    if isinstance(value, list):
        if value and isinstance(value[0], str):
            found.append(value[0])
            return found
        for entry in value:
            found.extend(recipe_component_ids(entry))
    return found


def check_a5(cdf, sink):
    for ident in sorted(cdf.items):
        _obj, rel = cdf.items[ident]
        source = cdf.source(rel, ident)
        for flag in sorted(cdf.item_flags(ident)):
            # LIGHT_<n> is parsed and removed by item_factory.cpp:563-575,
            # so it deliberately has no json_flag object.
            if flag.startswith("LIGHT_") and flag[6:].isdigit():
                continue
            if flag not in cdf.flags:
                sink.add("A5", "error", ident,
                         "undefined flag %s" % flag, source)
        for index, pocket in enumerate(cdf.pockets(ident)):
            for flag in as_list(pocket.get("flag_restriction")):
                if isinstance(flag, str) and flag not in cdf.flags:
                    sink.add("A5", "error", ident,
                             "undefined flag_restriction %s in pocket[%d]"
                             % (flag, index), source)


def check_a6(cdf, sink):
    replace = {}
    for migration, _rel in cdf.migrations:
        target = migration.get("replace")
        if isinstance(target, dict):
            target = target.get("id")
        if not isinstance(target, str):
            continue
        for ident in as_list(migration.get("id")):
            if isinstance(ident, str):
                replace[ident] = target
    live = set(cdf.items)
    for migration, rel in cdf.migrations:
        from_ids = [i for i in as_list(migration.get("id")) if isinstance(i, str)]
        target = migration.get("replace")
        if isinstance(target, dict):
            target = target.get("id")
        if not isinstance(target, str):
            continue
        mid = ",".join(from_ids) or "MIGRATION"
        source = cdf.source(rel, from_ids[0] if from_ids else mid)
        chain = [target]
        seen = set(from_ids) | {target}
        current = target
        hops = 1
        problem = None
        while current not in live:
            if current not in replace:
                problem = "target %s does not exist" % chain[-1]
                break
            if replace[current] in seen:
                problem = "cycle " + " -> ".join(chain + [replace[current]])
                break
            current = replace[current]
            chain.append(current)
            seen.add(current)
            hops += 1
            if hops > 3:
                problem = "chain longer than 3: " + " -> ".join(chain)
                break
        if problem:
            sink.add("A6", "error", mid, problem, source)


def check_a7(cdf, sink):
    facts = {ident: cdf.item_facts(ident) for ident in cdf.items}

    def fits(container, content):
        if content not in facts:
            return True
        # Gunmods are installed through the gunmod system, not pockets.
        if cdf.item_type(content) == "GUNMOD":
            return True
        for pocket in cdf.pockets(container):
            if pocket_accepts(pocket, facts[content], content):
                return True
        return False

    def check_entry(entry, source):
        container = entry.get("container-item")
        contents = []
        if isinstance(container, str):
            contents = [i for i in as_list(entry.get("item")) if isinstance(i, str)]
        else:
            container = entry.get("item")
            if isinstance(container, str):
                contents = [i for i in as_list(entry.get("contents-item"))
                            if isinstance(i, str)]
                for group_id in as_list(entry.get("contents-group")):
                    group = cdf.groups.get(group_id)
                    if group:
                        contents.extend(group_item_ids(group[0]))
        if not isinstance(container, str) or container not in cdf.items:
            return
        for content in contents:
            if content in cdf.items and not fits(container, content):
                sink.add("A7", "warning", container,
                         "no pocket holds %s" % content, source)

    for group_id in sorted(cdf.groups):
        obj, rel = cdf.groups[group_id]
        source = cdf.source(rel, group_id)
        for entry in iter_group_entries(obj):
            check_entry(entry, source)
    for profession, rel in cdf.professions:
        pid = item_id(profession) or "profession"
        source = cdf.source(rel, pid)
        items = profession.get("items")
        if isinstance(items, dict):
            for slot in items.values():
                if isinstance(slot, dict):
                    for entry in as_list(slot.get("entries")):
                        if isinstance(entry, dict):
                            check_entry(entry, source)


# ---------------------------------------------------------------- driver

def run_audit(cdf_dir, dda_dir=None, dirs=("data/json",)):
    cdf = Tree(cdf_dir, "cdf", dirs)
    sink = RowSink()
    check_a2(cdf, sink)
    check_a3(cdf, sink)
    check_a4(cdf, sink)
    check_a5(cdf, sink)
    check_a6(cdf, sink)
    check_a7(cdf, sink)
    if dda_dir and os.path.isdir(os.path.join(dda_dir, "data", "json")):
        dda = Tree(dda_dir, "dda", ("data/json",))
        check_a1(cdf, dda, sink)
    return sink.sorted_rows(), cdf.parse_errors


def format_tsv(rows, parse_errors=()):
    lines = ["\t".join(HEADER)]
    for row in rows:
        lines.append("\t".join(row))
    for error in parse_errors:
        lines.append("parse\twarning\t-\tfile did not parse\t%s" % error)
    return "\n".join(lines) + "\n"


def counts(rows):
    result = {}
    for row in rows:
        result[row[0]] = result.get(row[0], 0) + 1
    return result


def section(cdf_dir, dda_dir=None):
    """AT0 entry point: (result, count, rows)."""
    rows, _errors = run_audit(cdf_dir, dda_dir)
    errors = sum(1 for row in rows if row[1] == "error")
    return ("FAIL" if errors else "PASS"), len(rows), rows


def selftest(fixture):
    problems = 0
    seeded = os.path.join(fixture, "seeded")
    expected_path = os.path.join(seeded, "expected.tsv")
    rows, errors = run_audit(os.path.join(seeded, "cdf"),
                             os.path.join(seeded, "dda"))
    actual = [tuple(row) for row in rows]
    expected = []
    with open(expected_path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("check\t"):
                continue
            expected.append(tuple(line.split("\t")))
    if set(actual) != set(expected) or len(actual) != len(expected):
        for row in sorted(set(actual) - set(expected)):
            print("UNEXPECTED\t" + "\t".join(row))
        for row in sorted(set(expected) - set(actual)):
            print("MISSING\t" + "\t".join(row))
        problems += 1
    if errors:
        print("parse errors in seeded fixture: " + ", ".join(errors))
        problems += 1
    clean = os.path.join(fixture, "clean")
    clean_rows, clean_errors = run_audit(os.path.join(clean, "cdf"),
                                         os.path.join(clean, "dda"))
    if clean_rows or clean_errors:
        print("clean fixture reported %d rows, %d parse errors"
              % (len(clean_rows), len(clean_errors)))
        for row in clean_rows:
            print("CLEAN\t" + "\t".join(row))
        problems += 1
    if problems:
        print("selftest FAILED: %d problem(s) against %s" % (problems, fixture))
        return 1
    print("selftest ok: %d seeded rows matched, clean fixture empty"
          % len(expected))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="CDF data audit (report only).")
    parser.add_argument("--cdf", default=DEFAULT_CDF)
    parser.add_argument("--dda", default=DEFAULT_DDA)
    parser.add_argument("--out", help="write TSV to this file")
    parser.add_argument("--section", action="store_true",
                        help="print the AT0 section line and exit 0/1")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--fixture", default=DEFAULT_FIXTURE)
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest(args.fixture)
    if not os.path.isdir(os.path.join(args.cdf, "data", "json")):
        print("error: no data/json under --cdf %s" % args.cdf, file=sys.stderr)
        return 2
    rows, errors = run_audit(args.cdf, args.dda)
    if args.section:
        failed = any(row[1] == "error" for row in rows)
        print("data-audit\t%s\t%d findings" % ("FAIL" if failed else "PASS",
                                               len(rows)))
        return 1 if failed else 0
    text = format_tsv(rows, errors)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
