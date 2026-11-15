# Data audit (`tools/cdf_audit.py`)

`tools/cdf_audit.py` is a static audit of CDF's JSON against Cataclysm: DDA.
It never starts the game and never changes data. It finds the class of defect
that a hand audit of the ballistic vests exposed: a flag that was dropped in
the port, an item that no pocket accepts, an id that does not exist.

Run it with the isolated interpreter, from the repository root:

```
py -3 -I tools/cdf_audit.py --cdf . --dda <DDA checkout> --out audit.tsv
py -3 -I tools/cdf_audit.py --section          # one line for cdf_report.py
py -3 -I tools/cdf_audit.py --allow <allowlist.tsv> --section
py -3 -I tools/cdf_audit.py --selftest         # fixture tree, exit 1 on mismatch
```

`--dda` defaults to `%CDF_DDA_SRC%`, then `C:\CDF\CDDA SRC`. `--cdf` defaults to
the checkout the script lives in. Only `data/json` is read by default; mods are
the separate `check-mods` report section. A file that does not parse is listed
in the output as a `parse` row and the run continues.

`--allow` names the allowlist (default `tests/data/audit/allow.tsv` when that
file exists). It only affects `--section` and the imported `section()`: a row
the allowlist matches is dropped from the findings and counted, while `--out`
and stdout still carry every row. The format is the AT1 triage
`allow-draft.tsv` one: a header plus `check`, `detail_regex`,
`family_regex`, `except_id_regex`, `verdict`, `reason`.
`detail_regex` matches the `detail` column; `family_regex` matches the CDF
item's family (`data/json/items/armor/x.json` is `items/armor`,
`data/json/items/x.json` is `items`, `data/json/obsoletion/x.json` is
`obsoletion`, and a definition elsewhere is `?`); a row whose id matches
`except_id_regex` is never allowed (`^$` excludes nothing).

## Output

The output is TSV with the columns `check`, `severity`, `id`, `detail` and
`source`. `source` is `cdf:` or `dda:` followed by the path relative to that
tree and the 1-based line of the object's `id`. Severities are `error`,
`warning` and `info`. The section line printed by `--section` is:

```
data-audit<TAB>PASS|FAIL<TAB>N findings[, allowed M]
```

`N` is the number of findings left after the allowlist; the `, allowed M`
suffix appears only when the allowlist matched rows. It exits 0 on PASS and 1
on FAIL. `cdf_report.py` imports
`cdf_audit.section(cdf_dir, dda_dir, allow_path)`, which returns
`(result, count, rows, allowed)`. The third value holds only the non-allowed
rows, so the report runner does not filter a second time.

## Checks

| Check | Severity | What it reports |
|---|---|---|
| A1 | error / info | For every id present in both trees, after `copy-from` resolution: a `pocket_data` key or a flag that only DDA has (error), or only CDF has (info). It also compares the three per-portion fields named by the vest hand audit: `cover_vitals`, `volume_encumber_modifier` and `cover_ranged`. Values are never compared: CDF numbers stay CDF. |
| A2 | error | An insert-only item (`ABLATIVE*` or `CANT_WEAR`) that no pocket accepts, or a BN `ARMOR_PLATE` item that no ablative (plate) pocket accepts. Restriction and capacity both count. |
| A3 | warning | A pocket with `flag_restriction` or `item_restriction` that no item in the data can fill, including items that match the restriction but do not fit. |
| A4 | error | An item id named by an item group, a profession, a recipe component, an `item_restriction` or a migration that does not exist. Item-group and profession entries nested inside a `collection` or `distribution` are visited at every depth. Ids that only appear as migration sources count as existing, because the loader rewrites them. `EMPTY_GROUP` is an engine group. |
| A5 | error | A flag used on an item or in `flag_restriction` with no `json_flag` definition. `LIGHT_<n>` is excluded: `item_factory.cpp` parses it and removes it. |
| A6 | error | A migration target that does not exist, a migration chain longer than three, or a cycle. |
| A7 | warning | A starting-kit or item-group entry whose container has no pocket that can hold the entry. This is a static pocket test; the real spawn test is the kit sweep. Entries nested inside a `collection` or `distribution` are visited at every depth. Gunmods are installed through the gunmod system and are skipped. |

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

- `seeded/` plants one defect per check, has `expected.tsv`, and has
  `allow.tsv` with two allowed rows plus an `except_id_regex` row that must
  not allow the planted one;
- `clean/` is the same shape without defects and must report nothing.

The seeded group also plants a missing id and a kit overflow nested inside a
`collection`/`distribution`, so the selftest fails if the walker stops at the
outer entry.

`--selftest` runs both and exits 1 on a mismatch, in the style of
`tools/cdf_dda_probe.py --selftest`. When changing a check, change its fixture
and `expected.tsv` in the same edit.

## Reading a first run

A first run is expected to fail. Most A1 rows are not defects: CDF is BN-based,
so many shared ids carry a different flag set, and BN containers still use the
legacy `contains` volume instead of DDA's `pocket_data`. Deliberate differences
belong in `tests/data/audit/allow.tsv` (the report runner's allowlist), not in
this tool; run `--section --allow tests/data/audit/allow.tsv` to see only rows
the allowlist does not match. A green audit means "no known class of defect",
not "the data is correct".
## Pocket sweeps and the DT1 pocket dump

This page describes the two headless data audits added for the inventory work. They do not
change item data; they report what the game loaded.

## Flow sweep (AT5a)

`tests/audit_sweep_flows_test.cpp` walks every armor, tool-armor and pocket-bearing item type in
id order and records one row per probe:

| Flow | Check |
|---|---|
| `F1` | spawn, then wear (armor) or wield (other containers) with no debugmsg |
| `F2` | up to 3 accepted items per pocket go in; pocket volume and weight grow by the item's |
| `F3` | one refused item per restricted pocket, with a non-empty reason and no change |
| `F4` | each accepted item unloads again and the pocket is restored |
| `F5` | drop the container with contents on the floor, pick it up, count and weight are kept |
| `F6` | item JSON round trip keeps the type, contents count and weight |
| `PLATE` | every `ABLATIVE*` plate fills every matching carrier slot; one more plate is refused |

Rows go to `report/sweep-flows.tsv` (`item`, `flow`, `ok`, `reason`). A failing row fails the test
unless it is listed in `report/sweep-flows-allow.tsv` by item and flow:

```
<item id><TAB><flow>
```

The allowlist is a report file, not part of the test, so a data-fix batch can shrink it.

Quick runs: `CDF_SWEEP_LIMIT=N` limits the number of item types, and `CDF_SWEEP_SHARD=i/n`
processes every n-th type starting at shard i (1-based). A plate with no carrier in the data is
reported as a successful `no-carrier` row. More than 5% failures in `F1` fails the test: that is a
loader problem, not a sweep problem.

## Pocket dump and diff (PL-64 DT1)

`tests/pocket_dump_test.cpp` is hidden (`[.pocket_dump]`). It writes the final pockets of every
loaded item type to `report/pocket-dump.tsv`: pocket type, flag and item restrictions, volume
capacity, maximum item volume, holster, ripoff, magazine well, ammo restrictions, rigid and
watertight.

Run it, then compare with DDA's raw JSON at the pinned revision:

```
cata_test --tags "[.pocket_dump]"
py -3 tools/pocket_diff.py
```

`tools/pocket_diff.py` reads DDA through `git -C <repo> show <rev>:<path>` (default
`C:\CDF\CDDA SRC`, revision `b4b62254ae2`), resolves `copy-from` chains, and writes
`report/pocket-diff.tsv` with one row per differing field (`item_id`, `pocket_index`, `field`,
`cdf`, `dda`, `note`). This is how synthesized BN pockets show up as real differences.

## UI sweep (AT5b)

`tests/audit_sweep_ui_test.cpp` (`[audit][sweep][ui]`) takes every armor and tool-armor type that
has a container pocket, in id order, wears it, puts up to two accepted items inside and drives
the inventory selector:

| Flow | Check |
|---|---|
| `wear` | spawn and wear the type with no debugmsg |
| `collapse` | collapse the worn row: its contents rows leave the list and the row stays; collapse again and they return |
| `hide-all` | the show/hide-all cycle: show all, hide every container's contents, then back to the pocket settings |
| `highlight` | the contents carry the child mark, the worn row the parent mark (DDA `inventory_ui.cpp:4335-4371`) |
| `insert` | the insert selector over the worn container lists the candidate and its header volume line is non-empty |

Rows go to `report/sweep-ui.tsv` (`item`, `flow`, `ok`, `reason`); a failing row fails the test
unless `report/sweep-ui-allow.tsv` lists it by item and flow. `CDF_SWEEP_LIMIT=N` and
`CDF_SWEEP_SHARD=i/n` work as for the flow sweep. A type with no accepted probe in the fixed
candidate pool is recorded as a successful `skipped` row, so it stays visible in the table.

The seeded-defect proof of AT5b step 5 (forcing `holds_contents()` to `is_container()`) is manual:
`holds_contents()` is file-static in `inventory_ui.cpp`, and this packet does not edit source.
The `collapse` and `hide-all` rows are the checks that would report that defect: a worn container
whose contents stay listed.
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

## Formatting scenes (FM8)

`tests/audit_fm_golden_test.cpp` (`[audit][golden][fm]`) renders the FM1-FM7 formatting scenes
with the AT3 headless draw helper and compares the dump row by row with
`tests/data/golden/ui/fm_*.txt`. The PL-62 AT4 scenes in
`tests/selector_golden_test.cpp` (`[audit][golden]`) remain the base case; FM8 adds the scenes of
the FM8 draft that need another fixture:

| Scene | Fixture | What the golden pins |
|---|---|---|
| `fm_s1_*` | worn windbreaker, cargo pants, hiking backpack; the pack holds a box, the box a rock | the default view: three worn rows with the `DETAILED_CONTAINERS` suffix, `▼` on the pack, the box and rock under `Spare parts` |
| `fm_s3_once_*`, `fm_s3_twice_*` | same | `SHOW_HIDE_CONTENTS_ALL` pressed once (show all) and twice (only worn rows, `▶` on the pack) |
| `fm_s4_hierarchy_*` | same | hierarchy mode: no category headers, box under the pack, rock indented twice as far |
| `fm_s5_carets_*` | same, box selected | `<` on the pack row and `>` on the rock row (the `INVENTORY_HIGHLIGHT` symbol mode) |
| `fm_s6_filter_rock_*` | same, pack collapsed, filter `rock` | the filter re-shows the rows hidden by the collapse (the box and the rock) and drops the worn rows |
| `fm_s7_drop_*` | an empty box and a three-rock stack carried, marks `+` and `2 of 3 #` | the mark column and the right-hand `ITEMS TO DROP` list |
| `fm_s8_pickup_*` | a rock and a box with a cube on the avatar's tile | the `Pickup` title, the two-line hint and the map rows with their square name as the category |
| `fm_s9_insert_*` | a worn US ballistic vest with two ESAPI plates, a loose ESBI and a loose ESAPI plate carried | the container header lines, the offered ESBI plate and the ESAPI plate greyed with `not enough space` |
| `fm_s10_unload_*` | the AT4 fixture | the unload screen at 80x24 (the AT4 `g8` only pins 100x40) |

Every scene is rendered at 100x40 and 80x24. `CDF_GOLDEN_UPDATE=1` rewrites the files and passes
with a warning, as in AT4. The header stat numbers are pinned only for S9, where they come from
the vest's pockets; S7 depends on the avatar's body summary, so its test checks only that the
`Bulk Volume:` and `Selected` lines exist. `TEST_CASE("FM seeded defect: clearing the chevrons
breaks the S1 golden", ...)` clears the chevron flags and requires the dump to differ from
`fm_s1_100x40`, and `TEST_CASE("FM golden compare fails on a changed line", ...)` proves the diff
report on a temp file.

The expected rows are hand-written from DDA `b4b62254ae2`; the REPORT lists the DDA file:line for
each scene, and the goldens are expected to fail where CDF still differs (a later run shows the
exact rows).
