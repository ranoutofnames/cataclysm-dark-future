#!/usr/bin/env python3
"""CDF item category audit: what category every item ends up in, CDF vs DDA.

Reads CDF's data/json from the checkout this file lives in and DDA's data/json
at a pinned revision (through `git -C <dda> cat-file --batch`, so the checkout
itself may be at any revision). Resolves copy-from and applies the category
rule of each game's code:

  item_factory.cpp  optional( jo, "category", category_force )
                    if category is unset: category_force = calc_category( obj )
  item.cpp          get_category_shallow() = category_force if valid, else null

calc_category (CDF item_factory.cpp:5183, DDA item_factory.cpp:5263) picks the
first matching slot in this order, for both games:

  (CDF only) artifact_data -> artifacts
  gun and not gunmod -> guns
  magazine -> magazines
  ammo -> ammo
  tool -> tools
  armor -> clothing            <- the crux: armor type alone is NOT "armor"
  comestible (comestible_type MED) -> drugs, else food
  book -> books
  gunmod -> mods
  bionic -> bionics
  any melee damage type > MELEE_STAT (5) -> weapons
  otherwise -> other

The slot of an item is its JSON type (CDF legacy types: ARMOR, TOOL_ARMOR,
TOOL, ...) or, for "type": "ITEM", the union of its "subtypes" and the
subtypes/slots it inherits through copy-from (both loaders keep the base
itype's slots, so slots accumulate down the chain).

The only way an item reaches the "armor" category is an explicit
"category": "armor" in JSON (as DDA does for vests, plates and helmets).
CDF's item_category.json has to define that category, or the id is invalid.

Writes report/category-diff.tsv:

    id  cdf category  dda category  cdf rule  dda rule  verdict

verdicts: same, diff, cdf-only (id absent from DDA), cdf-undefined-cat
(the CDF category id has no ITEM_CATEGORY object: the game renders it as the
empty dummy category). Standard library only; it only reads files.

Usage:
  py -3 -I tools/cdf_category_audit.py [--cdf DIR] [--dda DIR] [--rev SHA]
                                       [--out FILE]
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
# Reuse the audit's tree loader and copy-from resolver; do not rewrite them.
from cdf_audit import Tree, as_list  # noqa: E402

DEFAULT_CDF = os.path.dirname(HERE)
DEFAULT_DDA = os.environ.get("CDF_DDA_SRC", r"C:\CDF\CDDA SRC")
DEFAULT_REV = "b4b62254ae2"
DEFAULT_OUT = os.path.join(DEFAULT_CDF, "report", "category-diff.tsv")

MELEE_STAT = 5

# JSON type -> itype slots that the type's loader fills.
LEGACY_SLOTS = {
    "ARMOR": {"armor"},
    "TOOL": {"tool"},
    "TOOL_ARMOR": {"tool", "armor"},
    "GUN": {"gun"},
    "GUNMOD": {"gunmod", "mod"},
    "TOOLMOD": {"mod"},
    "MAGAZINE": {"magazine"},
    "AMMO": {"ammo"},
    "COMESTIBLE": {"comestible"},
    "BOOK": {"book"},
    "BIONIC_ITEM": {"bionic"},
    "PET_ARMOR": {"pet_armor"},
    "CONTAINER": {"container"},
    "ENGINE": {"engine"},
    "WHEEL": {"wheel"},
    "FUEL": {"fuel"},
    "BATTERY": {"battery"},
    "GENERIC": set(),
}

# "type": "ITEM" subtype -> slots. DDA's itype::load_slots and CDF's load_item.
SUBTYPE_SLOTS = {
    "ARMOR": {"armor"},
    "TOOL": {"tool"},
    "TOOL_ARMOR": {"tool", "armor"},
    "PET_ARMOR": {"pet_armor"},
    "GUN": {"gun"},
    "GUNMOD": {"gunmod", "mod"},
    "AMMO": {"ammo"},
    "MAGAZINE": {"magazine"},
    "COMESTIBLE": {"comestible"},
    "BOOK": {"book"},
    "BIONIC_ITEM": {"bionic"},
    "TOOLMOD": {"mod"},
    "ENGINE": {"engine"},
    "WHEEL": {"wheel"},
    "BATTERY": {"battery"},
    "FUEL": {"fuel"},
    "CONTAINER": {"container"},
    "SEED": set(),
    "BREWABLE": set(),
    "COMPOSTABLE": set(),
    "MILLING": set(),
    "ARTIFACT": set(),
}


def git(dda, args):
    result = subprocess.run(["git", "-C", dda] + args, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise SystemExit("git %s failed: %s" % (" ".join(args),
                                                result.stderr.decode("utf-8", "replace")))
    return result.stdout.decode("utf-8", "replace")


def iter_git_json(dda, rev, subdir="data/json"):
    """Yield (relpath, obj) for every object in rev:subdir, like iter_json."""
    names = [name for name in git(dda, ["ls-tree", "-r", "--name-only", rev,
                                        "--", subdir]).splitlines()
             if name.endswith(".json")]
    names.sort()
    proc = subprocess.Popen(["git", "-C", dda, "cat-file", "--batch"],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for name in names:
            proc.stdin.write(("%s:%s\n" % (rev, name)).encode("utf-8"))
            proc.stdin.flush()
            header = proc.stdout.readline().decode("utf-8", "replace").split()
            if len(header) < 3 or header[1] != "blob":
                yield name, None
                continue
            size = int(header[2])
            blob = proc.stdout.read(size)
            proc.stdout.read(1)  # trailing newline of the batch record
            try:
                data = json.loads(blob.decode("utf-8-sig"))
            except ValueError:
                yield name, None
                continue
            if isinstance(data, dict):
                data = [data]
            if not isinstance(data, list):
                yield name, None
                continue
            for obj in data:
                if isinstance(obj, dict):
                    yield name, obj
    finally:
        proc.stdin.close()
        proc.wait()


class GitTree(Tree):
    """A Tree whose objects come from a git revision instead of the worktree."""

    def __init__(self, label, objects):
        self.root = ""
        self.label = label
        self.dirs = []
        self.objects = []
        self.items = {}
        self.abstracts = {}
        self.flags = set()
        self.requirements = set()
        self.groups = {}
        self.professions = []
        self.recipes = []
        self.migrations = []
        self.parse_errors = []
        self._resolved = {}
        self._resolving = set()
        for relpath, obj in objects:
            if obj is None:
                self.parse_errors.append(relpath)
                continue
            self.objects.append((relpath, obj))
            ident = obj.get("id")
            abstract = obj.get("abstract")
            if isinstance(abstract, str):
                self.abstracts.setdefault(abstract, (obj, relpath))
            if obj.get("type") not in ("ITEM",) and obj.get("type") not in LEGACY_SLOTS:
                continue
            for name in as_list(ident):
                if isinstance(name, str):
                    self.items.setdefault(name, (obj, relpath))


def slots_of(tree, ident, chain=()):
    """itype slots of ident, accumulating through copy-from (both loaders do)."""
    found = tree.items.get(ident)
    if found is None:
        return set()
    obj = found[0]
    result = set()
    base_from = obj.get("copy-from")
    if isinstance(base_from, str) and base_from not in chain:
        result |= slots_of(tree, base_from, chain + (base_from,))
    kind = obj.get("type")
    if kind == "ITEM":
        for subtype in as_list(obj.get("subtypes")):
            if isinstance(subtype, str):
                result |= SUBTYPE_SLOTS.get(subtype, set())
    else:
        result |= LEGACY_SLOTS.get(kind, set())
    return result


def melee_damage(tree, ident, cdf):
    obj = tree.resolved(ident) or {}
    if cdf:
        # CDF's load_basic_info: assign "bashing" and "cutting" (no stab).
        return max([obj.get("bashing") or 0, obj.get("cutting") or 0])
    damage = obj.get("melee_damage")
    if not isinstance(damage, dict):
        return 0
    values = []
    for value in damage.values():
        if isinstance(value, (int, float)):
            values.append(value)
    return max(values) if values else 0


def category_of(tree, ident, cdf):
    """Return (category, rule) by the game's own rule."""
    raw = (tree.items.get(ident) or (None, ""))[0] or {}
    obj = tree.resolved(ident) or {}

    rule = "json category"
    value = raw.get("category")
    if not isinstance(value, str):
        rule = "copy-from category"
        value = obj.get("category")
    if isinstance(value, str) and value:
        return value, rule

    slots = slots_of(tree, ident)
    if cdf and obj.get("artifact_data"):
        return "artifacts", "calc:artifact_data"
    if "gun" in slots and "gunmod" not in slots:
        return "guns", "calc:gun"
    if "magazine" in slots:
        return "magazines", "calc:magazine"
    if "ammo" in slots:
        return "ammo", "calc:ammo"
    if "tool" in slots:
        return "tools", "calc:tool"
    if "armor" in slots:
        return "clothing", "calc:armor"
    if "comestible" in slots:
        comest = obj.get("comestible_type") or obj.get("comesttype")
        return ("drugs", "calc:comestible-MED") if comest == "MED" else \
            ("food", "calc:comestible")
    if "book" in slots:
        return "books", "calc:book"
    if "gunmod" in slots:
        return "mods", "calc:gunmod"
    if "bionic" in slots:
        return "bionics", "calc:bionic"
    if melee_damage(tree, ident, cdf) > MELEE_STAT:
        return "weapons", "calc:melee"
    return "other", "calc:none"


def load_categories(tree):
    """item_category ids defined in CDF data/json (CDF has no mod categories here)."""
    ids = set()
    for _relpath, obj in tree.objects:
        if obj.get("type") == "ITEM_CATEGORY":
            for name in as_list(obj.get("id")):
                if isinstance(name, str):
                    ids.add(name)
    return ids


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cdf", default=DEFAULT_CDF)
    parser.add_argument("--dda", default=DEFAULT_DDA)
    parser.add_argument("--rev", default=DEFAULT_REV)
    parser.add_argument("--out", default=DEFAULT_OUT)
    args = parser.parse_args()

    cdf_tree = Tree(args.cdf, "cdf")
    dda_tree = GitTree("dda", iter_git_json(args.dda, args.rev))
    cdf_categories = load_categories(cdf_tree)

    rows = []
    counts = {"same": 0, "diff": 0, "cdf-only": 0, "cdf-undefined-cat": 0}
    for ident in sorted(cdf_tree.items):
        cdf_cat, cdf_rule = category_of(cdf_tree, ident, True)
        if ident in dda_tree.items:
            dda_cat, dda_rule = category_of(dda_tree, ident, False)
        else:
            dda_cat, dda_rule = "", "absent"
        if not dda_cat:
            verdict = "cdf-only"
        elif cdf_cat != dda_cat:
            verdict = "diff"
        elif cdf_cat not in cdf_categories:
            verdict = "cdf-undefined-cat"
        else:
            verdict = "same"
        counts[verdict] += 1
        rows.append((ident, cdf_cat, dda_cat, cdf_rule, dda_rule, verdict))

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("id\tcdf category\tdda category\tcdf rule\tdda rule\tverdict\n")
        for row in rows:
            f.write("\t".join(row) + "\n")

    print("items: %d (CDF) %d (DDA@%s); rows: %d"
          % (len(cdf_tree.items), len(dda_tree.items), args.rev, len(rows)))
    for verdict in ("same", "diff", "cdf-only", "cdf-undefined-cat"):
        print("  %-18s %d" % (verdict, counts[verdict]))
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
