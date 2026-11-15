# Data audit (`tools/cdf_audit.py`)

`tools/cdf_audit.py` is a static audit of CDF's JSON against Cataclysm: DDA.
It never starts the game and never changes data. It finds the class of defect
that a hand audit of the ballistic vests exposed: a flag that was dropped in
the port, an item that no pocket accepts, an id that does not exist.

Run it with the isolated interpreter, from the repository root:

```
py -3 -I tools/cdf_audit.py --cdf . --dda <DDA checkout> --out audit.tsv
py -3 -I tools/cdf_audit.py --section          # one line for cdf_report.py
py -3 -I tools/cdf_audit.py --selftest         # fixture tree, exit 1 on mismatch
```

`--dda` defaults to `%CDF_DDA_SRC%`, then `C:\CDF\CDDA SRC`. `--cdf` defaults to
the checkout the script lives in. Only `data/json` is read by default; mods are
the separate `check-mods` report section. A file that does not parse is listed
in the output as a `parse` row and the run continues.

## Output

The output is TSV with the columns `check`, `severity`, `id`, `detail` and
`source`. `source` is `cdf:` or `dda:` followed by the path relative to that
tree and the 1-based line of the object's `id`. Severities are `error`,
`warning` and `info`. The section line printed by `--section` is:

```
data-audit<TAB>PASS|FAIL<TAB>N findings
```

It exits 0 on PASS and 1 on FAIL. `cdf_report.py` can import
`cdf_audit.section(cdf_dir, dda_dir)`, which returns `(result, count, rows)`.

## Checks

| Check | Severity | What it reports |
|---|---|---|
| A1 | error / info | For every id present in both trees, after `copy-from` resolution: a `pocket_data` key or a flag that only DDA has (error), or only CDF has (info). It also compares the three per-portion fields named by the vest hand audit: `cover_vitals`, `volume_encumber_modifier` and `cover_ranged`. Values are never compared: CDF numbers stay CDF. |
| A2 | error | An insert-only item (`ABLATIVE*` or `CANT_WEAR`) that no pocket accepts, or a BN `ARMOR_PLATE` item that no ablative (plate) pocket accepts. Restriction and capacity both count. |
| A3 | warning | A pocket with `flag_restriction` or `item_restriction` that no item in the data can fill, including items that match the restriction but do not fit. |
| A4 | error | An item id named by an item group, a profession, a recipe component, an `item_restriction` or a migration that does not exist. Ids that only appear as migration sources count as existing, because the loader rewrites them. `EMPTY_GROUP` is an engine group. |
| A5 | error | A flag used on an item or in `flag_restriction` with no `json_flag` definition. `LIGHT_<n>` is excluded: `item_factory.cpp` parses it and removes it. |
| A6 | error | A migration target that does not exist, a migration chain longer than three, or a cycle. |
| A7 | warning | A starting-kit or item-group entry whose container has no pocket that can hold the entry. This is a static pocket test; the real spawn test is the kit sweep. Gunmods are installed through the gunmod system and are skipped. |

## The copy-from resolver

The resolver implements `copy-from`, `extend`, `delete`, `relative` and
`proportional`, including the legacy `contains` volume of BN containers and
`abstract` objects. It is a Python approximation of the loader: anything the
loader does that is not visible in JSON is not modelled. A1 and the DDA side
of the audit are therefore best read as "this is what the data says", not as
the engine's final item. The boilerplate is reused from
`tools/cdf_dda_probe.py`.

## Fixtures

`tests/data/audit_fixtures/` holds two miniature trees:

- `seeded/` plants one defect per check and has `expected.tsv`;
- `clean/` is the same shape without defects and must report nothing.

`--selftest` runs both and exits 1 on a mismatch, in the style of
`tools/cdf_dda_probe.py --selftest`. When changing a check, change its fixture
and `expected.tsv` in the same edit.

## Reading a first run

A first run is expected to fail. Most A1 rows are not defects: CDF is BN-based,
so many shared ids carry a different flag set, and BN containers still use the
legacy `contains` volume instead of DDA's `pocket_data`. Deliberate differences
belong in `tests/data/audit/allow.tsv` (the report runner's allowlist), not in
this tool. A green audit means "no known class of defect", not "the data is
correct".

## Kit and group spawn audit

`tests/audit_kits_test.cpp` (`[audit][kits]`) is the dynamic counterpart of A4,
A6 and A7: it spawns the real starting kits, item groups and migrations in the
game and checks where the items actually go. It needs a built test binary and
never changes data. Run it through the shard runner with a positive tag:

```
python -I tools/cdf_test.py --src <tree> --tags "[audit][kits]"
CDF_SWEEP_SHARD=1/8 python -I tools/cdf_test.py --src <tree> --tags "[audit][kits]"
```

`CDF_SWEEP_SHARD=i/n` (1-based) gives each process every n-th profession, item
group and migration; the default is one process for everything. The report
runner sets the shard count.

The case writes one row per finding to `report\sweep-kits.tsv`, columns
`check`, `id`, `severity`, `detail`. Findings whose `(check, id)` pair is in
`tests/data/audit/allow.tsv` (check id, item id, reason, date) are counted but
do not fail the run; a missing allowlist is an empty one. The allowlist is for
deliberate CDF-vs-DDA differences, not for silencing a check.

| Check | Severity | What it reports |
|---|---|---|
| K1 | error | A worn item's top-level contents do not fit the pocket they sit in (`can_contain` re-check with `ignore_contents`), or a child sits in the MIGRATION pocket. |
| K2 | error | A starting item is on the map at the start position, or the kit's item count changes between the spawn and the character + overflow (DDA `starting_items` conservation check). |
| K3 | error | An item the character could not store ("overflow", no pocket accepts it), or a non-stack item with a nonzero count and zero volume. |
| K4 | error | A debugmsg while spawning or storing the kit. |
| K5 | error | `item::contents` of a worn item does not survive a JSON save/load round trip, or the round trip itself reports a debugmsg. |
| G1 | error | An item group spawned an item whose type id does not exist. |
| G2 | error | A spawned item's contents tree violates its parent's pocket rules (same re-check as K1). |
| G3 | info | A group with entries produced nothing from all three seeds (42, 7, 123). Not a failure; it may be a probability-0 group. |
| G4 | error | A debugmsg while spawning an item group. |
| M1 | error | A migration's replacement id does not exist. |
| M2 | error | A migration chain is longer than three steps or contains a cycle (A6's dynamic form). |
| M3 | error | Spawning the migrated id fails, returns a different type, or reports a debugmsg. |

Professions are checked for both genders and five starting-trait sets. CDF has
no hobby system, so the trait vectors DDA's `new_character_test` passes to
`profession::items` stand in for hobbies; they are passed to the item
substitutions only, because setting mutations on the character is what makes
that test take hours and does not change what the K checks inspect. Kits use
seed 42 (male) and 999 (female); item groups use 42, 7 and 123; all fixed, so a
shard is reproducible.

`TEST_CASE("kit and group spawn seeded defects", "[audit][kits][defect]")` is
the self-test required by PLAN section 6: it force-inserts a 20 L television
into a worn backpack's 15 L pocket and requires a K1 row, and points a
migration at a missing id and requires an M1 row. If either stops reporting,
the sweep is broken, not green.
