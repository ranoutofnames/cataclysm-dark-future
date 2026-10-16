# DDA staged fixtures

Files here are verbatim DDA test data kept for reference. The game does not load them. The CDF copy of a staged fixture sits in `data/mods/TEST_DATA/` with a `.p3` suffix, which the loader ignores.

- `dda_pocket_mods_test.json`: verbatim DDA `pocket_mods_test.json`. The CDF copy is `data/mods/TEST_DATA/pocket_mods_test.json.p3`.
- `data/mods/TEST_DATA/dda_multimag_items.json.p3` (P3-2 step 5): verbatim DDA objects: 30 `test_multimag_*` items, the `test_multimag_full_load` itemgroup, the `test_turret_rig` vehicle, the `cz75mag_10rd` magazine with its `copy-from` parent `cz75mag_16rd`, `debug_backpack`, and `welding_wire` (ammo type, referenced by a test item). Only objects CDF lacks are copied. P3-1 step 5 renames it to `dda_multimag_items.json` once the loader reads the multimag keys. Ids and missing prerequisites are listed in `Z:\cdf-exchange\outbox\27-P3-2-PREP\work\step5_fixtures.md`.

## p3_golden.json (P3-2 step 4)

The magazine port plan of `tools/cdf_port_dda_mags.py`, one entry per port target (1165 items: COPY-TWIN, AUTHOR, DROP-KEY and authored HAND-FALLBACK rows; KEEP, KEEP-CHARGES, KEEP-REVIEW and LEAVE rows are not in it). It is the pocket_data every ported item must load with, not a copy of any DDA file.

Format (`format` 1): `{ "format": 1, "dda": "<DDA git hash the twins were read from>", "items": { "<item id>": { ... } } }`. Items are sorted by id, indent 2, LF line ends. Each item has, in this key order:

- `class`: MAG, GUN-INT, TOOL-INT, WELL, CHARGES or DDA-POCKET; `rule`: R1..R8; `action`: COPY-TWIN, AUTHOR, DROP-KEY or HAND-FALLBACK.
- `twin`: a DDA twin item with the same id exists.
- `pockets`: the item's complete effective `pocket_data` after the port, only the keys that are present, in the order the loader must produce (`port_preserves_pocket_order` relies on that order; carry pockets of +STORAGE items come last). No `//` keys.
- `cdf_kept_ids`: CDF-only magazine ids kept in a DDA well (`// CDF:`, owner decision Q3).
- `dropped_ids`: ammotypes and item ids the DDA twin names that CDF lacks and the pocket therefore omits.
- `storage`: the row is tagged +STORAGE (worn storage pockets re-emitted after the magazine pocket).

Regenerate it from the unported tree (this worktree's base, or any tree with the BN keys still in place; DDA at `C:\CDF\CDDA SRC`):

    py -3 -I tools/cdf_port_dda_mags.py --golden tests/data/dda_staged/p3_golden.json

On an already ported tree pass the pristine tree as the plan source: `--cdf <ported> --baseline-cdf <unported>`. The output is deterministic, so a diff after regeneration is a real plan change. `--check [--baseline-cdf <unported>]` verifies a tree against the same plan (exit 1 on any difference).

Consumer: `tests/cdf_mag_port_test.cpp` (P3-7): every ported id's loaded pockets must equal this file, and pocket order is preserved. The cases are hidden (`[.]`) until P3-7 turns them on, because MAIN's C++ still reads `capacity` / `magazines` and no item is ported in the tree that carries this file.
