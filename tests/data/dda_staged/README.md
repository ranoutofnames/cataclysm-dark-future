# DDA staged fixtures

Files here are verbatim DDA test data kept for reference. The game does not load them. The CDF copy of a staged fixture sits in `data/mods/TEST_DATA/` with a `.p3` suffix, which the loader ignores.

- `dda_pocket_mods_test.json`: verbatim DDA `pocket_mods_test.json`. The CDF copy is `data/mods/TEST_DATA/pocket_mods_test.json.p3`.
- `data/mods/TEST_DATA/dda_multimag_items.json.p3` (P3-2 step 5): verbatim DDA objects: 30 `test_multimag_*` items, the `test_multimag_full_load` itemgroup, the `test_turret_rig` vehicle, the `cz75mag_10rd` magazine with its `copy-from` parent `cz75mag_16rd`, `debug_backpack`, and `welding_wire` (ammo type, referenced by a test item). Only objects CDF lacks are copied. P3-1 step 5 renames it to `dda_multimag_items.json` once the loader reads the multimag keys. Ids and missing prerequisites are listed in `Z:\cdf-exchange\outbox\27-P3-2-PREP\work\step5_fixtures.md`.
