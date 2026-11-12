# DDA staged fixtures

Files here are verbatim DDA test data kept for reference. The game does not load them. The CDF copy of a staged fixture sits in `data/mods/TEST_DATA/` with a `.p3` suffix, which the loader ignores.

- `dda_pocket_mods_test.json`: verbatim DDA `pocket_mods_test.json`. The CDF copy is `data/mods/TEST_DATA/pocket_mods_test.json.p3`.
- `data/mods/TEST_DATA/dda_multimag_items.json.p3` (P3-2 step 5): verbatim DDA objects: 30 `test_multimag_*` items, the `test_multimag_full_load` itemgroup, the `test_turret_rig` vehicle, the `cz75mag_10rd` magazine with its `copy-from` parent `cz75mag_16rd`, `debug_backpack`, and `welding_wire` (ammo type, referenced by a test item). Only objects CDF lacks are copied. P3-1 step 5 renames it to `dda_multimag_items.json` once the loader reads the multimag keys. Ids and missing prerequisites are listed in `Z:\cdf-exchange\outbox\27-P3-2-PREP\work\step5_fixtures.md`.

## p3_golden.json (P3-2 step 4)

The magazine port plan of `tools/cdf_port_dda_mags.py`, one entry per port target plus the 4 KEEP-REVIEW adaptor rows (1168 items: the 1165 regen-target ids plus 3 TEST_DATA ids, listed in `p3_golden_target_ids.json` and checked by `p3 golden file has format 1...`; 1145 port targets, COPY-TWIN, AUTHOR, DROP-KEY, authored HAND-FALLBACK and the KEEP-CDF variants (CDF ammo sets, capacities and battery wells kept), and the 4 adaptor rows `magazine_battery_mod`, `debug_retool_mag_well`, `retool_ar15_300blk`, `arredondo_chute`; plain KEEP, KEEP-CHARGES and LEAVE rows are not in it). It is the pocket_data every ported item must load with, not a copy of any DDA file.

Format (`format` 1): `{ "format": 1, "dda": "<DDA git hash the twins were read from>", "items": { "<item id>": { ... } } }`. Items are sorted by id, indent 2, LF line ends. Each item has, in this key order:

- `class`: MAG, GUN-INT, TOOL-INT, WELL, CHARGES or DDA-POCKET; `rule`: R1..R8; `action`: COPY-TWIN, COPY-TWIN-KEEP-CDF-AMMO, AUTHOR, AUTHOR-KEEP-CDF-WELL, AUTHOR-KEEP-CDF-INTEGRAL (AUTO-8: TOOL-INT rows keep the CDF integral charges), DROP-KEY, HAND-FALLBACK or KEEP-REVIEW.
- `twin`: a DDA twin item with the same id exists.
- KEEP-REVIEW rows (R6 ADAPTOR, no edit) carry the item as it is today: `pockets` is its effective `pocket_data` (empty for the adaptor mods), plus an `adaptor` block `{ "cdf": {magazine_adaptor, magazines_like}, "dda": {...} }` (the DDA twin's values, null where the twin lacks them). The adaptor swaps the well restriction (item_contents.cpp:359-369), so the C++ test must cover them; `--check` does not touch them.
- `pockets`: the item's complete effective `pocket_data` after the port, only the keys that are present, in the order the loader must produce (`port_preserves_pocket_order` relies on that order; carry pockets of +STORAGE items come last). No `//` keys.
- `cdf_kept_ids`: CDF-only magazine ids kept in a DDA well (`// CDF:`, owner decision Q3).
- `dropped_ids`: ammotypes and item ids the DDA twin names that CDF lacks and the pocket therefore omits.
- `storage`: the row is tagged +STORAGE (worn storage pockets re-emitted after the magazine pocket).

Regenerate it from the unported tree (this worktree's base, or any tree with the BN keys still in place; DDA at `C:\CDF\CDDA SRC`):

    py -3 -I tools/cdf_port_dda_mags.py --golden tests/data/dda_staged/p3_golden.json

On an already ported tree pass the pristine tree as the plan source: `--cdf <ported> --baseline-cdf <unported>`. The output is deterministic, so a diff after regeneration is a real plan change. `--check [--baseline-cdf <unported>]` verifies a tree against the same plan (exit 1 on any difference).

Consumer: `tests/cdf_mag_port_test.cpp` (P3-7): every ported id's loaded pockets must equal this file, and pocket order is preserved. The cases are hidden (`[.]`) until P3-7 turns them on, because MAIN's C++ still reads `capacity` / `magazines` and no item is ported in the tree that carries this file.

## p3_batches (7-GOLD1)

`p3_batches/<batch>.json`: `{ "batch": "<name>", "items": { "<id>": { ... } } }`, the ids of one 7-* packet. `tests/p3_golden_batches_test.cpp` (`[p3][golden][batches]`, not hidden) reads only the keys of `items`; other members are free (the 7-* packets put the CDF legacy literals there). A batch id must be a golden id (case `p3 golden batch ids are golden ids`), must load (an unloaded id is counted as skipped and printed, never silent) and must match the golden pockets index by index (`compare_pocket`). The neg- files are skipped by `read_batches`. `_selftest.json` is the fixture: 19 ids already converted to DDA wells; `p3 golden selftest batch` pins `loaded == 19`, so a batch that loads nothing cannot pass. The class cases `[R1]`..`[R8]` stay hidden until the last batch of a class is in.

### Generic batch cases (P3-7-BATCH1, `[p3][data][batch]`)

`tests/p3_batch_generic_test.cpp` reads every `p3_batches/<batch>.json` (not `neg-*.json`) and every `p3_batches/neg-<batch>.json`, so a 7-* data packet copies its two files here and writes no comparison code. Each case prints `batch <name>: ids N checked M skipped K`; a member a row lacks is skipped and counted, never silent. B1, B2 and B5 fail when they checked nothing at all.

Batch rows: `file, class, action, twin, pockets, dropped_keys, dropped_ids, tags, legacy`, and where present `cap_diff`, `legacy_from`, `hand_fallback`, `applied_by_tool`, `well_not_applied`, `well_diff` (`cdf_added`, `dda_only_kept`, `unfit`), `own_keys`. Top-level `batch` and `items` are read; the rest is free. An `id@file` key names a duplicate id.

| Case | Reads | Asserts |
|---|---|---|
| `p3 batch legacy keys gone` (B1) | `file`, `dropped_keys` | the own record (`id` or `abstract` equals the id) in `file` has none of the dropped keys; a row whose `action` ends `-PARKED` or that has `well_not_applied` is skipped, counted and printed (it keeps its legacy keys by the owner's rule) |
| `p3 batch kept and dropped ids` (B2) | `dropped_ids` (`{where, id}`), `well_diff.cdf_added` | each dropped id is absent from the loaded pocket field named by `where` (`item_restriction`, `allowed_speedloaders`, `ammo_restriction`, `flag_restriction`, `default_magazine`); each `cdf_added` id is in the loaded `item_restriction` |
| `p3 batch pocket order` (B3) | the batch ids and the golden | ids with two or more golden pockets keep the golden pocket-type order; none in the merged batches yet (`checked 0`) |
| `p3 batch rows not edited` (B4) | `own_keys` | the own record's top-level key set equals `own_keys`; rows without it are skipped (none of the merged batches has it) |
| `p3 batch negative mutations` (B5) | `neg-<batch>.json` | see below |

`p3_batches/neg-<batch>.json`: `{ "batch", "mutations": [ { "case", "kind", "id", "pocket", "mutation", "expect" } ] }`. B5 applies each mutation to an in-memory copy of the golden `pockets` of `id` (a golden id; it need not be in the batch) and compares it with the loaded item (`p3_golden_compare_pockets` in `cdf_mag_port_test.cpp`): the unmutated copy must match, the mutated one must report a mismatch, and every mismatch line must start with that id. Kinds: `cap_plus_one` (first backticked ammo type in `mutation`, else the first), `well_volume` (magazine_well to `1 ml`), `drop_restriction_id`, `drop_speedloader` (the backticked id, else the first), `add_bogus_id`, `wrong_default`, `flag` (adds a bogus flag), `pocket_order` (swap pockets 0 and 1), `hand_fallback_integral` (flips MAGAZINE and MAGAZINE_WELL). An unknown kind, or a kind that does not apply to the pocket, fails the case.

`p3_golden_target_ids.json` lists the 1165 regen-target ids with their class plus the 3 TEST_DATA ids added later (`test_fake_noise_tool` TOOL-INT, `test_helmet_eod_p3` WELL, `test_visible_underslung_shotgun` GUN-INT); the golden count itself is read from the file. LINKUP1c (2026-11-11) removed 20 cable tools (the LINKUP1c conversions, e.g. `coffeemaker`, `welder`, `elec_shears`): the DDA twin has no battery pocket and the CDF item has none now, so the plan tool emits no row for them. BATT4 (2026-11-11) removed `mess_kit`, `crt_mess_kit` and `voltmeter` the same way (no battery pocket in DDA and none in CDF now). APPL1 (2026-11-11) removed `autoclave` and `hydraulic_press` the same way (DDA appliance items with no battery pocket; CDF gives them the appliance `link_up` instead).
