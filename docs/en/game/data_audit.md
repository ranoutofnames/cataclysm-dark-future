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
