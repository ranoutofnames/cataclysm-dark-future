# Item migration (`MIGRATION`)

A `MIGRATION` object retires an item id: whatever data or save still names the old id gets the
replacement instead. CDF extends BN's migration with counted contents and applies the whole
migration when an item is spawned, not only when a save is loaded.

## The obsoletion folder

Retired content lives in `data/json/obsoletion/`, and mods keep their own `obsolete.json` and
`migration.json`:

- `obsoletion/items.json` keeps the old item *definitions* of ids that are still referenced
  (mapgen palettes, dialogue, third-party mods). A kept definition keeps the id valid, so
  `--check-mods` and `check_definitions` still pass for those references.
- `obsoletion/migration.json` holds the `MIGRATION` objects.
- `obsoletion/recipes.json` holds obsolete recipe stubs,
  `{ "type": "recipe", "result": "old_id", "obsolete": true }`. A migration deletes every
  non-obsolete recipe whose result is the migrated id (and its uncraft entry), so a recipe that
  must go on working has to be retargeted to the replacement.

## Fields

```json
{
  "type": "MIGRATION",
  "id": "modularvestceramic",
  "replace": "ballistic_vest_esapi",
  "flags": [ "FIT" ],
  "charges": 0,
  "contents": [ "esbi_plate", { "id": "esapi_plate", "count": 2 } ]
}
```

| Field | Meaning |
|---|---|
| `id` | the retired id, or an array of ids sharing one migration |
| `replace` | the item type that replaces it (must exist) |
| `flags` | flags set on the migrated item |
| `charges` | charges set on the migrated item (0: leave them) |
| `contents` | items the migrated item holds |

### Counted `contents`

An entry is an item id (count 1) or an object `{ "id": ..., "count": n }` with `n > 0`.
Repeated ids add up, so `[ "esapi_plate", "esapi_plate" ]` and
`[ { "id": "esapi_plate", "count": 2 } ]` mean the same.

The count is "up to": when the item already holds some of an id, only the missing ones are
added. Loading a save twice, or spawning from an already migrated item, never doubles the
contents. Contents go in through the normal item router, so a plate lands in a matching plate
slot of a carrier, and an item the old type left in the MIGRATION pocket is re-homed into a
pocket of the new type when one now accepts it.

## Where a migration is applied

When a save is loaded, an item with a retired id becomes the replacement with the migration's
flags, charges and contents. The same happens for every place that spawns an item from an id in
data:

- itemgroups, including profession kits and NPC class groups (the entry's own `contents-group`
  and `contents-item` are added after the migration's contents);
- mapgen `item`, `items` and `liquids`;
- vehicle prototype `items`;
- legacy array-form profession items;
- the `u_buy_item` dialogue effect;
- Lua `Character:create_item`;
- the debug wish menu.

A spawn from C++ with `item::spawn( id )` never migrates, because migration contents and
deserialization need the real, obsolete type. Use `item_controller->spawn_migrated( id, birthday )`
when an id can come from data.

The `MIGRATION_CHECKS` debug option reports ids that itemgroups still name.

## Retiring an item

1. Move the definition to `obsoletion/items.json` (or the mod's `obsolete.json`) if anything
   still names it.
2. Add the `MIGRATION` with `replace` and, if the replacement needs parts, `contents`.
3. Turn its recipes into obsolete stubs, or retarget them to the replacement.
4. Do not edit the references: itemgroups, kits, vehicles and palettes are migrated at load.
