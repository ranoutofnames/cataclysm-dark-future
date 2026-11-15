# Data audit: pocket sweeps and the DT1 pocket dump

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
