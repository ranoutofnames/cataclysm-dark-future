# Pockets, magazine wells and ammo

This page is for a contributor who changes the C++ of the pocket model. It describes what the code does today. Every paragraph names the symbols it talks about and the file that defines them, by file name only. Use `git grep -n <symbol>` in the repository to find the lines.

The Lua bindings for the same model are in the [Lua pockets guide](../../mod/lua/guides/pockets_and_ammo.md). For the JSON side, see the [Pockets section of the item reference](../../mod/json/reference/items/item_creation.md).

## Pocket types and wells

The pocket types are the values of the `pocket_type` enum, defined in `pocket_type.h`. They are `CONTAINER` (general storage, holsters and bandoliers), `MAGAZINE` (ammo inside a magazine), `MAGAZINE_WELL` (magazines or batteries inside a gun or tool), `MOD` (gunmods and toolmods), `CORPSE`, `SOFTWARE`, `E_FILE_STORAGE` (electronic files on an e-device, held by their `ememory_size`), `CABLE` (power and data cables with their connections) and `MIGRATION` (items that have no home in the item's current pockets). `EBOOK` is obsolete: it stays in the enum only for old saves, and no data uses it. The enum is saved as strings, so its order can change freely.

Each pocket is one `item_pocket` object, defined in `item_pocket.h`. `get_type` returns the pocket's type, and `saved_type` returns the type kept for saving.

A gun or tool that takes magazines has a `MAGAZINE_WELL` pocket. The default-magazine lookup in `item_pocket.h` answers only for a `MAGAZINE_WELL` pocket and returns nothing for the other types. `magazine_default`, in `item.cpp`, uses that live well unless guard G holds. Guard G is `has_conversion_or_adaptor_mod`, also in `item.cpp`: when an installed gunmod or toolmod has an `ammo_modifier` or a `magazine_adaptor`, the legacy tables answer instead. DDA has the same guard in `item_gun_tool_ammo.cpp` (lines 2263-2278 in DDA, quoted in a CDF comment).

## Synthesis of pockets

Items without explicit `pocket_data` get a pocket list from `synthesize_pockets`, a member of `Item_factory` declared in `item_factory.h` and defined in `item_factory.cpp`. The list is built from the legacy fields of the item: a `CONTAINER` pocket for `container` data, a `MAGAZINE` pocket for `magazine` data, and a `MAGAZINE_WELL` for a gun or tool with magazines. The magazine well only accepts the magazines the item lists, and its size follows the biggest of them.

An item with explicit `pocket_data` keeps its pockets. `synthesize_pockets` returns early for it, after it has set the carry default of worn containers and added what the explicit list lacks. A gun or tool that declares no `MOD` pocket gets one, as DDA does after loading (`item_factory.cpp`, DDA lines 4136-4140). An item with the `link_up` use action gets a `CABLE` pocket, see the next paragraph.

`add_cable_pocket`, a lambda inside `synthesize_pockets` in `item_factory.cpp`, adds a `CABLE` pocket that is not rigid to any item with a `link_up` use action, unless the item already has one. This runs on the explicit path and on the synthesized path alike. DDA adds the same pocket in `item_factory.cpp` (lines 4144-4148), so the rule is DDA's.

The finishing step of `synthesize_pockets` runs on every exit path. It copies the `NO_UNLOAD` and `NO_RELOAD` flags onto each pocket and names each pocket after its item. DDA does the same in `item_factory.cpp` (lines 940-956).

A tool with charges and an ammo id, and no magazines and no explicit `pocket_data`, gets an integral magazine pocket. `add_integral_magazine_pocket`, a free function in `item_factory.cpp`, adds a rigid `MAGAZINE` pocket for it, with the tool's `max_charges` as the ammo limit. Money is excluded. The constant `kSynthesizeIntegralMagazine` in `item_factory.cpp` switches this on. The function also builds the text `needs pocket definitions`, and `synthesize_pockets` raises it as a debugmsg. Built-in data should give every such tool explicit pocket definitions, so the debugmsg marks a data bug. Tools from mods without `pocket_data` still get the pocket. DDA raises the same debugmsg in `item_factory.cpp` (line 4127).

`item::magazine_integral`, in `item.cpp`, is true when the item has a `MAGAZINE` pocket: it checks `contents.has_pocket_type( pocket_type::MAGAZINE )`. This is DDA's body (`item_gun_tool_ammo.cpp`, lines 2253-2256). Tool ammo is held in a `MAGAZINE` pocket. The ammo code reads tool ammo from the pocket model, and there is no separate charges path for tools.

## Lookup layer

`item::ammo_remaining` is declared in `item.h` and defined in `item_gun_tool_ammo.cpp`. It has three overloads: one with no argument, one with a magazine well index, and one with a `Character` carrier. Only the carrier overload also counts the pockets the carrier wears or holds, so use it when a character's gear matters.

`magazines_current`, defined in `item_contents.cpp` and declared in `item_contents.h` and `item.h`, lists the magazines loaded in the item's pockets. There is a non-const and a const version.

`visit_items` is a member template of `visitable`, declared in `visitable.h`. It is defined in `visitable.cpp` and specialised there for `item`, `item_contents` and `temp_crafting_inventory`. It walks every item of the visitable and skips the pockets that `is_listed_pocket` rejects.

`is_listed_pocket` is defined in `visitable.cpp` and again in `character.cpp`, and both use the same rule. A pocket is listed with the carried items when it is a carry or holster pocket of type `CONTAINER`. The `carry` flag only decides capacity, so a holstered item stays visible to the reload, wield and use lookups. A tool's own ammo or magazine pocket is never listed, so a loaded tool's ammo stays visible only to the tool-aware lookups.

`carried_volume` and `carried_capacity` are both members of `Character`, defined in `character.cpp` and declared in `character.h`. They are the pair the carried panes show. `carried_capacity` counts the worn `carry` pockets, multiplied by the packmule factor, plus the body storage. Holster and plate pockets do not count toward capacity. Debug storage has no limit.

## Reload: the selection path and the reload actor

Every reload caller goes through `select_ammo`, a function in the `game_menus::inv` namespace, declared in `game_inventory.h` and defined in `game_inventory.cpp`. NPCs do not get the menu: `npcmove.cpp` calls `reload_selection::prepare` directly.

`reload_selection` is a namespace in `reload/reload_selection.h`, implemented in `reload/reload_selection.cpp`. Its `prepare` function decides the outcome, such as an automatic choice or a missing magazine, and returns the option list in a `selection_result`. `selection_outcome` is the enum of outcomes. `prepare` has two overloads: one takes the base item, the other takes a list of options.

The reload itself is `reload_activity_actor`, defined in `activity_actor_definitions.h` and implemented in `activity_actor.cpp`. It is registered for the `ACT_RELOAD` activity. Its members `can_reload`, `start`, `reload` and `finish` do the work, and `serialize` and `deserialize` save it. The finish step is a member of this actor.

When a gun with the `RELOAD_AND_SHOOT` flag is aimed and its ammo location is not valid, `aim_activity_actor::load_RAS_weapon` (in `activity_actor.cpp`) calls `select_ammo` to pick the ammo. DDA does the same in `activity_actor.cpp`.

Putting a magazine into a holster is `insert_item_activity_actor`, in `activity_actor.cpp`. The put works on a copy first, and the original is removed only after the put succeeds. A gun that has a magazine well but no `CONTAINER` pocket refuses the put, so the magazine stays where it was. The comment in the code cites DDA's `activity_actor.cpp` (line 8122) for this put.

## Unload

The unload activity is `unload_activity_actor`, defined in `activity_actor_definitions.h` and implemented in `activity_actor.cpp`. It is registered for `ACT_UNLOAD`. Its `start`, `finish`, `unload`, `serialize` and `deserialize` members do the work. The non-container branch follows DDA's `unload_activity_actor` at `activity_actor.cpp` lines 6414-6514.

Guns and tools stay in `unload_item`, a function in `avatar_functions.cpp`. It either calls `unload_activity_actor::unload` directly or queues the actor as a timed activity.

## Multimag and turrets

A turret's gun can be a multimag, a gun whose ammo sits in several pockets bound to vehicle stores. `turret_data::ammo_data`, in `turret.cpp`, makes a temporary copy of the gun and runs `vehicle::prepare_multimag_pockets` on that copy. The queries that follow read the prepared copy and never change the real gun. `prepare_multimag_pockets` is declared in `vehicle.h`. It takes no map argument.

`turret_data::prepare_fire`, in `turret.cpp`, bills the bound pockets back when the gun uses firing requirements. It calls `vehicle::drain_back_multimag`, declared in `vehicle.h`, which clears only the pockets that were bound.

`turret_data::multimag_display_state`, in `turret.cpp`, returns one `multimag_display_pocket` for each ammo pocket of the turret. The struct is defined in `vehicle.h`.

Firing from a multimag is handled in `item_gun_tool_ammo.cpp`. The static function `resolve_firing_entries` picks the pocket entries a shot uses, and `multimag_drain` spends them.

## Link-up cables and their save keys

A cable tool has the `link_up` use action. `link_up_actor`, declared in `iuse_actor.h`, is that action, and its constructor gives the action name `link_up`. Its `load` function in `iuse_actor.cpp` reads `cable_length`, `charge_rate`, `efficiency`, `targets` and `can_extend`. The comment in the code cites DDA's `iuse_actor.h` (lines 1161-1194) for this actor.

The cable state is the link model: each link is an `item::link_data`, held in the item and saved under the item's `link_data` key. `item::link_data::serialize` in `savegame_json.cpp` writes these keys: `link_i_state`, `link_t_state`, `link_t_abs_pos`, `link_t_mount`, `link_length`, `link_max_length`, `link_last_processed`, `link_charge_rate`, `link_charge_efficiency` and `link_charge_interval`. `item::link_data::deserialize`, in the same file, reads the same keys, so a change to one must change the other.

## Aim activity and should_unload_RAS

`aim_activity_actor`, in `activity_actor_definitions.h`, has the member `should_unload_RAS`. A `RELOAD_AND_SHOOT` gun is unloaded when the aim ends without a shot, if this flag is set. The member is saved and loaded by that name in `activity_actor.cpp`. The `unload_RAS_weapon` member of the same class, also in `activity_actor.cpp`, does the unload.

`ranged.cpp` keeps a local `unload_RAS_weapon` flag while the target UI and the aim loop run. It copies the flag into `should_unload_RAS` at four points. When an aim is resumed, the flag is read back from the activity's saved value. The option `UNLOAD_RAS_WEAPON`, defined in `options.cpp`, sets the default for new aims. The key `TOGGLE_UNLOAD_RAS_WEAPON`, defined in `keybindings.json`, flips the flag during the aim. DDA's `ranged.cpp` (line 3108) copies the flag the same way.

The hint line for this key is `ras_unload_hint`, in `ranged_text.h` and called from `ranged.cpp`. It is shown in Fire mode only, because that is the only mode where the key is bound. The same header declares the other aim-screen text: `switch_ammo_hint_shown`, `aim_ammo_row`, and `turret_row`, which gives one row per turret pocket.

## Where to look

Start from the symbol in this page. Run `git grep -n <symbol>` in the repository and the file name that comes back is the one named above. The page does not list line numbers, because they move with every edit. The Lua side of the same model is in the [Lua pockets guide](../../mod/lua/guides/pockets_and_ammo.md). For the defining item actions and their JSON fields, use the [Pockets section of the item reference](../../mod/json/reference/items/item_creation.md). The general layout of game objects is in [game objects](../explanation/game_objects.md).
