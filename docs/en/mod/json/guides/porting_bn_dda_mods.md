# Porting a BN or DDA mod to CDF

This page is for a modder who has a mod made for Cataclysm: Bright Nights (BN) or Cataclysm:
Dark Days Ahead (DDA) and wants it to load on CDF. It says what the game does today, section by
section, and where CDF differs from DDA. It is not a balance guide: CDF keeps its own item data
and numbers, so the numbers on this page are CDF's. (CDF: mod_manager.cpp, get_default_core_content_pack)

## Pockets: storage and charges

A garment gets its worn layout from an `item_overlay` when it has one. Without an overlay, the
layout is derived from `storage`, and a garment with `storage` of 10 L or more gets split
pockets: a main container and a strap pocket. (CDF: item_factory.cpp, synthesize_pockets)

Legacy fields become pockets through `synthesize_pockets`. The table of which legacy fields
convert is in [Item creation](../reference/items/item_creation.md). A gun or tool that does not
declare a MOD pocket gets one added when it loads. (CDF: item_factory.cpp, synthesize_pockets)

A tool with `max_charges`, an ammo list, no legacy `magazines` key and no `pocket_data` gets an
integral magazine pocket. Money (cash cards) gets none. The loader builds the pocket from
`max_charges` and the tool's ammo list. When it adds one, the loader reports
`<id> needs pocket definitions` through debugmsg, so a data file that relies on this path shows
the message at load. A tool from a third-party mod that has no `pocket_data` still gets the
pocket. (CDF: item_factory.cpp, needs_integral_magazine and add_integral_magazine_pocket)

Message: `<id> needs pocket definitions`. (CDF: item_factory.cpp, synthesize_pockets)

## Ammo keys: tool_ammo and magazines

The key for a tool's ammo list is `tool_ammo`. It takes a string or an array. DDA documents it
as `"tool_ammo": [ "NULL" ]`, with the note "Ammo types used for reloading". `tool_ammo` and
`ammo` fill the same list, so when both are present the lists are combined. (CDF: item_factory.cpp, Item_factory::load for islot_tool)

`ammo_types` is a vehicle part key. CDF item loaders do not read it, so do not use it on an item.
This is the only place this page names it. (CDF: item_factory.cpp, Item_factory::load for islot_tool; veh_type.cpp, part loader)

For a MAGAZINE_WELL pocket with `item_restriction`, each magazine's ammo types get that magazine
in the item's `magazines` map. The well's `default_magazine` becomes the `magazine_default` of
each of those types. When the legacy capacity value is missing, it is derived from the pocket
(`derive_legacy_counts_from_pockets`). The legacy `magazines` key is still read. (CDF: item_factory.cpp, derive_legacy_counts_from_pockets)

## Carry and carried capacity

The container pockets of a worn garment default to `carry: true`. The default does not apply to
a holster or an ablative pocket, and it does not apply when the JSON sets `carry` itself. (CDF: item_factory.cpp, synthesize_pockets)

Carried-item lookups list holster pockets together with the carried items. The carry flag decides
only how much worn storage a pocket adds; listing and capacity are separate rules. (CDF: visitable.cpp, is_listed_pocket)

Worn storage is the sum of the `carry` pockets. (CDF: item.cpp, item::get_storage)

## Armor sub-locations

CDF has 54 sub-location ids. The 18 hand and foot ids were added to the 36 that existed before;
they are the wrists, palms, backs and fingers of each hand, and the soles, arches, toes, ankles
and heels of each foot. The id table is on [Sub-body parts](../reference/creatures/sub_body_parts.md).
(CDF: body_parts_sub.json, sub-location ids)

## Item overlays

`item_overlay` is a JSON type that the loader registers. It sets the pocket layout of a garment.
It reads `id` and `pocket_data`. Other fields are not read, and the unknown-field check reports
them (see [Messages you may see](#messages-you-may-see)). The overlay layout replaces the one
derived from `storage`. (CDF: item_factory.cpp, Item_factory::load_item_overlay; init.cpp, DynamicDataLoader::initialize)

An overlay can fail in two ways. Both are reported with debugmsg:

- `item_overlay: id '<id>' is not a known item`
- `item_overlay: pocket_data of '<id>' needs a garment with storage`

(CDF: item_factory.cpp, Item_factory::check_definitions)

A garment that has both `storage` and `pocket_data` is checked. When its container pockets differ
from `storage` by more than 10 percent, a Warn log line is written:
`item <id>: storage and pocket_data capacities differ by more than 10%`. (CDF: item_factory.cpp, Item_factory::check_definitions)

## Reload and unload

Reloading and unloading run through actors. The old `activity_handlers::reload_finish` is gone,
and `ACT_RELOAD` runs only through `reload_activity_actor`. Code reads an activity's actor with
`player_activity::get_actor()`. (CDF: activity_actor.cpp, reload_activity_actor; player_activity.h, get_actor)

For what the player sees, read [Reloading, unloading and aiming](../../../game/reload_unload_storage.md).
The player page describes what the player sees; this page only covers the mod-facing side. (CDF: activity_actor.cpp, reload_activity_actor)

## Cables: link_up

BN's cable item actions are removed: `CABLE_ATTACH` and `TOW_ATTACH` no longer exist. DDA's
`link_up` item action replaces them, and the item action is `link_up`. (CDF: item_actions.json, link_up)

An item with a `link_up` use action gets a CABLE pocket for extension cables, added after the
pocket list is built. The use-action keys are on [Item creation](../reference/items/item_creation.md). (CDF: item_factory.cpp, add_cable_pocket)

## Filter migration

The gas filter is migrated in `obsoletion/cdf_filter_migration.json`. It retires
`rebreather_filter` and replaces it with `rebreather_cartridge_air`. The ammo lives in the
cartridges, so `gasfilter_s` and `gasfilter_m` stay valid. The example and its JSON are on
[Item migration](../reference/items/item_migration.md#example-gas-filter-cartridges). (CDF: cdf_filter_migration.json, MIGRATION)

## Migrations and obsoletion

A `MIGRATION` object retires an item id and names its replacement. DDA's migration page uses the
same shape: an item type can be migrated to another item and then removed. (CDF: item_factory.cpp, check_migration_target)

`data/json/obsoletion/migration_dda.json` holds 236 `MIGRATION` objects for DDA item ids, one per
id. A mod that names one of those DDA ids gets the CDF replacement at load. (CDF: migration_dda.json, MIGRATION objects)

A missing migration target is handled by where the migration comes from:

- from the core pack (`bn`), the load reports the debugmsg `Replacement item for migration <id> does not exist`, and the migration is not skipped;
- from a non-core mod, a Warn log line is written and the migration is skipped:
  `Skipped migration <id> of <mod>: replacement item <x> does not exist`.

The load-time check also reports `Invalid migration target: <x>` for a core target. (CDF: item_factory.cpp, check_migration_target; item_factory.cpp, Item_factory::check_definitions)

## Mod dependencies

A mod that lists `"dda"` in its dependencies is resolved to the core pack, which is `bn`. When a
list names both ids, the duplicate is removed and one entry remains. Name `bn` in new mods. (CDF: mod_manager.cpp, load_modfile)

## DDA-only content

Top-level types that only DDA reads are read and ignored, and each one is logged once as an Info
line: `DDA-only type "<type>" from mod "<src>" is not read by CDF; its objects are ignored.` There
are 18 such types: 11 DDA-only types (widget, jmath_function, end_screen, help, mood_face,
weather_generator, character_mod, conduct, climbing_aid, body_graph, damage_info_order) and seven
DDA migration-family types (TRAIT_MIGRATION, bionic_migration, effect_migration,
vehicle_part_migration, ter_furn_migration, overmap_special_migration, camp_migration). (CDF: init.cpp, load_dda_inert_type)

`data/json/flags_dda_compat.json` loads 189 inert DDA-only flags. Each has a `//` comment that says
so. No C++ code reads them, so they do nothing. (CDF: flags_dda_compat.json, json_flag objects)

`"type": "ITEM"` with a `subtypes` array loads the item as its legacy per-type objects would. The
18 known subtypes are AMMO, ARMOR, BATTERY, BIONIC_ITEM, BOOK, COMESTIBLE, CONTAINER, ENGINE,
FUEL, GENERIC, GUN, GUNMOD, MAGAZINE, PET_ARMOR, TOOL, TOOLMOD, TOOL_ARMOR and WHEEL. `stack_size`
is read for AMMO and COMESTIBLE. DDA documents this under the heading "Subtypes" in its item
JSON page. [UNSURE: the DDA wording was not copied line for line; check the lines before quoting.]
(CDF: item_factory.cpp, load_item; init.cpp, DynamicDataLoader::initialize)

## Messages you may see

Message texts are copied from the source. The placeholders are `<id>`, `<x>`, `<p>`, `<c>`,
`<m>`, `<type>`, `<src>`, `<name>` and `<hint>`. (CDF: item_factory.cpp, Item_factory::check_definitions; init.cpp, load_dda_inert_type)

- `Invalid or misplaced field name "<name>" in JSON data`. For twelve DDA keys the message ends
  with `; CDF: <hint>`. The keys are armor, variants, display_type, sided, replace_materials,
  variant_type, read_skill, read_fun, freezing_point, growth_stages, tick_action and max_worn.
  The check only runs when the strict JSON report is on (`json_report_strict`).
  [UNSURE: when strict checking is on in a normal run.]
  (CDF: json.cpp, JsonObject::report_unvisited)
- `copy-from parent "<p>" of "<c>" (mod "<m>") is never defined (or the dependency is circular), this object is discarded`
  (CDF: init.cpp, compat_unresolved_deferred_message)
- `JSON contains circular dependency, this object is discarded`, when the object has no parent name.
  (CDF: init.cpp, compat_unresolved_deferred_message)
- `needs pocket definitions`, as above. (CDF: item_factory.cpp, add_integral_magazine_pocket)
- `DDA-only type "<type>" from mod "<src>" is not read by CDF; its objects are ignored.`
  (CDF: init.cpp, load_dda_inert_type)
- `unknown ITEM subtype '<x>'`, when a subtype is not in the list, and `ITEM needs at least one subtype`
  when the array is empty. (CDF: item_factory.cpp, load_item)
- `item_overlay: id '<id>' is not a known item` and `item_overlay: pocket_data of '<id>' needs a garment with storage`.
  (CDF: item_factory.cpp, Item_factory::check_definitions)

## What is not converted

Nothing on this page is converted for old saves. Retired item ids still go through `MIGRATION`
objects when a save loads, and nothing else is converted. (CDF: item_factory.cpp, check_migration_target)

BN's overmap terrain keys are not covered here. They come with the BN merge and are not part of
this page. (CDF: item_factory.cpp, check_migration_target)

## Developer tool

`tools/cdf_dda_probe.py` is a developer aid in the repository. It is not a player feature.
[UNSURE: its purpose was taken from its name and was not traced.] (CDF: tools/cdf_dda_probe.py, file name)
