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
