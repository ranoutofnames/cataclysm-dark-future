# Running the check report

`tools/cdf_report.py` runs every check family that is installed in a checkout
and prints one page. It never changes data and never commits anything.

## One command

From the checkout root:

```
py -3 -I tools/cdf_report.py --src .
```

The page is printed and written to `report/REPORT.md`. Detail files are
siblings in `report/`. The exit code is 0 only when every section passes
(`SKIP` is neutral) and there are no new findings; otherwise it is 1.

Useful variants:

```
py -3 -I tools/cdf_report.py --src . --quick
py -3 -I tools/cdf_report.py --src . --only data-audit,keybindings
py -3 -I tools/cdf_report.py --src . --skip-build
py -3 -I tools/cdf_report.py --selftest
```

- `--quick` is the dry mode: nothing is built and anything that needs the
  build prints `SKIP (no build)`. The Python families still run.
- `--skip-build` uses binaries that already exist but does not build.
- `--only` runs the named sections in the order given and ignores the rest.
- `--selftest` uses fake sections to prove the PASS, FAIL, SKIP, allowlist
  and new-finding paths and the exit code; it exits 0 on success.

The header shows the tree, the commit (12 characters) and the build number
from `VERSION.txt`. A tree that was never built has no `VERSION.txt`; the
header then says `build unknown`.

## Sections

| Section | Family | What it means |
|---|---|---|
| `keybindings` | AT2a | The user keybinding checker (`tools/cdf_kbcheck.py`). It is `SKIP (not installed)` until that tool is merged. |
| `data-audit` | AT1 | `tools/cdf_audit.py` compares CDF JSON against DDA: pocket keys, flags, dead plates, dead pockets, missing ids, undefined flags, migration chains and static kit containers. |
| `ui-smoke` | AT3 | `[audit][ui]` tests: the headless window dump checks chevrons, hints and keys. |
| `ui-golden` | AT4 | `[audit][golden]` tests: the golden screen text files. |
| `sweep-armor` | AT5 | `[audit][sweep]` tests: container and armor sweeps over real items. |
| `sweep-kits` | AT6 | `[audit][kits]` tests: kit, itemgroup and migration spawn audit. |
| `check-mods` | all | The game binary loads every mod with `--check-mods`; a data error fails even when the exit code is 0. |
| `unit-suite` | all | The full Catch2 suite through a junit report, compared with the test baseline. `new_failures` is the number to look at. |

A family that is merged but has no tests with its tag prints
`SKIP (not installed)`. Tags are always run through a positive Catch2 name
list, never through an exclusion-only filter.

The `keybindings` adapter expects the same section contract as the data audit:
`py -3 -I tools/cdf_kbcheck.py --section` prints one line
`keybindings<TAB>PASS|FAIL<TAB>counts` and exits 0 on PASS. The full output is
kept in `report/keybindings.log`.

## What to do on FAIL

1. Open the detail file in the `detail file` column.
2. For `data-audit`, a real defect is fixed in the data (or in the port that
   dropped the field). A deliberate CDF difference is not fixed: it goes in
   `tests/data/audit/allow.tsv`.
3. For a test section, the failing test name is in `summary.json`; the shard
   log has the Catch2 output.
4. For `unit-suite`, compare `new_failures` with the baseline; a new failure
   is a regression in this tree.
5. For `check-mods`, the error line is in `check-mods.log`.

A green page means "no known class of defect", not "the game is correct".
Mouse feel, key feel, colours, tileset art and sound stay manual.

## The allowlist

`tests/data/audit/allow.tsv` is tab separated with a header row:

```
check	item	reason	date
A1	example_item	deliberate CDF flag	2026-11-14
```

A finding whose check id and item id are both in the allowlist is counted but
does not fail the report. The page always shows `N findings (M new)` and lists
the first 15 new findings. The orchestrator adds rows after reading the
finding; a deliberate CDF-vs-DDA difference gets the word `deliberate` in the
reason. Comments starting with `#` and blank lines are ignored.

## Where the behaviour comes from

The helper scripts that build and run the tests live outside the repository,
so their behaviour is copied instead of imported:

- build command and the `--check-mods` error markers:
  `C:\Claude\Workspace 1\.claude\skills\cdf-test\cdf_test.py`
  (build at lines 31-33 and 81-84, check-mods at lines 98-116, junit and
  baseline comparison at lines 301-335);
- sharded test runs and the Catch2 name escaping: `C:\CDF\staging\shardrun.py`;
- the build helper itself: `C:\CDF\tools\cdf_build.bat` (override with
  `CDF_BUILD_BAT`);
- the test baseline: `orchestration/test-baseline.json` in the checkout if it
  exists, else `C:\CDF\orchestration\test-baseline.json` (override with
  `CDF_TEST_BASELINE`).

The test binary is
`out/build/win-tiles/tests/cata_test-tiles.exe`; the game binary is
`out/build/win-tiles/src/cataclysm-bn-tiles.exe`. Catch2 in this tree supports
`--list-test-names-only`, which the tag adapters use.
