# Reloading, unloading and aiming

This page describes reloading, unloading and aiming as CDF plays them today; storage and zones are covered later on this page, and modder notes for these activities are in [player_activity.md](../mod/json/reference/creatures/player_activity.md).

## Reloading

Reloading is a timed action, and you can be interrupted while it runs.

- If you are interrupted, the reload stops and does not resume. Your recoil is set to its maximum. Start the reload again if you still need it.
- A reload plays the gun's reload sound once, also when it is interrupted.

If a reload cannot start, the game says one of these:

- `Never mind.`: you backed out of the choice.
- `You need a compatible magazine to reload the %s!`: the item needs a magazine and has none.
- `Nothing to reload!`: you carry matching ammo, but the item has no room for it.
- `You don't have any %s to reload your %s!`: you carry no ammo that fits. The first `%s` is the ammo and the second is the item.

## Ammo screen

The ammo screen is the only reload screen. It opens from the reload key on a gun, from `{` (select default ammo), and from the "Reload item" picker, which asks you to choose the item first when you press `r` on the map.

- The ammo screen is keyboard only.
- You can select several rows. `=` or `+` adds one to the count of the highlighted row, and `-` takes one back.
- Rows are grouped under a header for each well of the gun.
- If several magazines match, "Reload which destination on %s?" asks which one to load.

## Nested ammo

The ammo screen also lists nested ammo and liquid, meaning ammo and liquid that sit inside containers:

- containers you carry;
- containers on the map;
- containers in vehicle cargo.

A liquid has its own row. A magazine that is already loaded in a gun is not listed.

## Unloading

Press `U` to open "Unload item", a list of what you can unload. An item inside another item is listed only when it sits in a container or mod pocket.

- Press `U` on that screen to switch the destination between Auto and Manual. The hint line shows the current choice. It starts as Manual.
- Auto: the items are unloaded without asking for a destination.
- Manual: you pick a container to put the items into. Any item with a container pocket that you can insert into is offered, and carried containers count. If there is none, the game says "You have no container to insert items."
- Your choice is kept with your interface settings (`unload_auto_contain`).
- Unloading takes time. When it finishes, the game says "You unload your %s." An ammo belt that also comes apart as it is unloaded says "You disassemble your %s." instead.
- If several magazines or containers can be unloaded, "Unload which source?" asks which one. If several items can be unloaded, "Unload what?" asks which one.

## Aim screen

While you aim, the ammo of your gun is shown:

- `Ammo: %s%s (%d/%d)` shows the rounds loaded and the capacity. When no rounds are loaded, the line is `Ammo: %s%s` without the count.
- A manual turret with several ammo pockets gets one line per pocket: `%s: %s (vehicle %d, need %d)` when the ammo comes from a vehicle, otherwise `%s: %s (%d, need %d)`.
- `[o] to reload/switch ammo.` is shown in fire and turret modes. `o` is the switch ammo key.

The Wield line shows the ammo of the wielded gun:

- A gun with one ammo pocket shows `(cur/max)`. A magazine gun with no magazine shows `(empty)`.
- A gun with several ammo pockets shows one `cur/max` for each pocket.
- `[mode]` appears only when the gun has more than one mode.

## Bows and other RAS weapons

RAS weapons are bows, slingshots and other weapons that load as they shoot.

- The option "Unload your bow etc after canceling shooting" (`UNLOAD_RAS_WEAPON`, in the interface options, default on) decides whether these weapons are unloaded when you leave the aim screen without a shot.
- On the aim screen, `-` (TOGGLE_UNLOAD_RAS_WEAPON) flips the choice for that aim. The hint line in fire mode says what `-` does next: `[-] to keep the <weapon> loaded after quitting.` when unloading is on, and `[-] to unload the <weapon> after quitting.` when it is off.
- If unloading is on when you leave the aim screen without a shot, the weapon is unloaded, but only if it holds ammo. Otherwise it stays loaded.

## Jams and fail-to-feed

A gun can jam when it fires. This is CDF's damage-based model.

- The chance of a jam rises with the gun's damage and with the magazine. Some guns never jam.
- A jam prints `Your %s malfunctions!` and the shot does not fire. Wear alone can also cause this message, with no fault.
- A jam can leave the gun with the fault "Fail to feed": "This gun did not load the round in the chamber properly."
- Fix it with the mend "Clear the failure to feed". It is one action, and the message is "You clear the failure to feed in the %s." This page does not state how long it takes.

## Keys

| Binding id | Action | Screen | Default |
|---|---|---|---|
| `CONTAIN_MODE` | Switch the destination between Auto and Manual | Unload item | `U` |
| `INCREASE_COUNT` | Add one to the count of the highlighted row | Ammo screen | `=`, `+` |
| `DECREASE_COUNT` | Take one back from the count of the highlighted row | Ammo screen | `-` |
| `unload` | Unload or empty the wielded item | In play | `U` |
| `unload_container` | Unload a container onto the ground | In play | `BACKSPACE` |
| `reload_item` | Reload an item (asks you to pick it first) | In play | `r` |
| `select_default_ammo` | Select default ammo for the wielded item | In play | `{` |
| `RELOAD` | Reload the highlighted item | Inventory item | `r` |
| `UNLOAD` | Unload the highlighted item | Inventory item | `u` |
| `PART_RELOAD` | Part reload | Inventory item | `p` |
| `AIM` | Aim | Aim screen | `.`, `5` |
| `AIMED_SHOT` | Aimed shot | Aim screen | `a` |
| `SWITCH_AMMO` | Reload or switch ammo | Aim screen | `o` |
| `FIRE` | Fire | Aim screen | `f`, `RETURN`, `MOUSE_RIGHT` |
| `TOGGLE_UNLOAD_RAS_WEAPON` | Keep or unload the bow etc after quitting | Aim screen | `-` |

`reload_weapon`, `reload_wielded` and `unload_all` have no default key; bind them in the keybindings menu if you want them.

## Carried bar

The carried bar shows how much you carry as `used / max`. You see it in the sidebar, in the trade window, in the pickup header, in the advanced inventory header, and in the headers of the inventory and pickup selectors. The bar changes colour as you fill up: above 65% full, and again above 85%.

Only carry pockets add to the maximum. A carry pocket is a container pocket that counts as storage. Carry pockets count in the items you wield, in the worn items and in body storage. Holsters and plates do not count.

- The container pockets of a worn garment count as carry by default. A pocket can opt out with `"carry": false` in its data.
- The Packmule mutation raises the carry pockets you wear. It does not raise body storage.

When you trade with an NPC, the NPC's room is what the NPC can carry minus what the NPC already carries. The check looks only at the NPC. If a trade would take the NPC over its room, the trade is refused with "<name> can't carry all that.", where `<name>` is the NPC's name. Shopkeepers are set to 5000 litres and 5000 kg, so they do not run out of room in practice.

## Pockets and storage

- `i` opens "Inventory Organization", the pocket menu, from the inventory.
- `v` on a container in the inventory opens that item's pocket menu, with the text "Modify pocket settings and move items between pockets."
- Enter opens the action list for the item. It has these entries: move item, item, category, whitelist, blacklist, priority, auto pickup, auto unload, save, apply and delete preset, whitelist contents, and clear.
- `*` marks an item as a favourite. `f` adds or removes the favourite mark on the highlighted item.
- `,` marks or unmarks the non-favourite items in the multidrop menu.
- `;` switches between the categories view and the hierarchy (tree) view. The footer shows `< [;] hierarchy >` or `< [;] categories >`. In hierarchy view, a container's contents are listed under it.

If you put a magazine into a gun that has no pocket for it, the magazine stays in your hands. The put is only completed when it succeeds, so the magazine is not lost when it fails.

## Item info and names

The item info text uses these labels.

- Magazine: `Capacity: ` followed by `<n> round of <ammo>` or `<n> rounds of <ammo>`, one for each ammo type. `Variants: ` lists the variants.
- Gun: `Magazine: ` when one magazine is loaded, or `Magazines: ` with a count when there are several. For a gun that needs firing requirements, each well gets `Capacity: ` or `Capacity (<pocket name>): `. `Variants: ` appears only when the gun's magazine is not built in.
- Ammo: `Ammunition: ` shows the ammo loaded in each well, or the ammo name when nothing is loaded. `Ammunition consumed per shot: ` appears when a shot uses more than one unit of ammo. A gun with a magazine that is not built in shows `Compatible magazines:` and a `Types:` list. Without a list of magazines, only the header shows.

Names show the ammo for guns and magazines with more than one well. Each well shows `current/max ammo name`, and the wells are joined with `, `. The ammo name always shows in this form. The number is coloured by how full the well is:

- empty: light red;
- under one third: red;
- under two thirds: yellow;
- otherwise: light green;
- full: white.

For a gun or magazine with one well, the ammo name shows only when the option "Add ammo to weapon/magazine names" is on.

The Wield line is described in [Aim screen](#aim-screen). A gun with neither a well nor a built-in magazine shows `(cur)`, with the current count and no maximum. The ammo text follows the item name.

## Tools

A tool with a magazine pocket keeps its charge in that pocket. You reload it on the ammo screen, the same way as a gun; see [Ammo screen](#ammo-screen). A battery tool starts with its charges loaded into that pocket.

- A tool that uses charges shows its cost in the use menu as `(-N)`, where `N` is the number of charges the action takes.
- A cash card keeps its money as loose charges. You cannot unload it or reload it.

## Power

Cables link power and towing between vehicles and the items around them. A cable has a loose end. Use the cable to attach that end to a target, such as a dashboard or electronics control unit, a vehicle battery, an electric grid connector, a tow cable on a towing vehicle, a Cable Charger System CBM, a UPS or a solar pack. You can extend another cable, remove the cable extensions, or detach a cable. Cables use the `link_up` item action.

The menu entries are these exact texts:

- "Attach to dashboard or electronics control unit"
- "Attach to vehicle battery"
- "Attach to electric grid connector"
- "Attach tow cable to towing vehicle"
- "Attach to Cable Charger System CBM"
- "Attach to UPS"
- "Attach to solar pack"
- "Extend another cable" (for a cable), or "Extend with another cable" (for another item)
- "Remove cable extensions"
- "Detach"
- "Attach loose end to Cable Charger System CBM"

The game prints these messages:

- "You attach the cable to your Cable Charger System."
- "You attach the cable to the UPS."
- "You attach the cable to the solar pack."
- "You connect the %1$s to the %2$s."
- "You extend the %1$s with the %2$s."
- "You disconnect the %1$s from the %2$s."
- "You reel in the %s and wind it up."

The vehicle menu has two entries for taking cables off: "Disconnect power connections" and "Disconnect items and tow cables". No key is shown for either entry.

Plutonium fuel is stored in packs. One fuel cell holds 500 charges. Unloading a pack gives back whole cells: the charges divided by 500 come out as cells, and any remainder is lost. A pack with less than one cell's worth gives back nothing.

| Pack | Cells | Charges | Taken by |
|---|---|---|---|
| Small plutonium fuel pack | 2 | 1000 | holo cloak, dimensional anchor |
| Medium plutonium fuel pack | 5 | 2500 | phase immersion suit |
| Large plutonium fuel pack | 10 | 5000 | RM13 armor |
| Reactor plutonium fuel pack | 20 | 10000 | minireactor (vehicle part) |

No recipe, item group or shop yet supplies the small, medium or large packs. The reactor pack has a recipe.

The minireactor vehicle part takes the reactor pack, and also the small and large packs. Load a fuel cell into a pack to refill it.

## Turret rows

On the vehicle screen, a turret with several ammo pockets shows one row for each pocket. The row gives the pocket name, the ammo and the count. A `[v] ` mark shows that the ammo comes from the vehicle's supply. A row is red when its pocket holds less than one shot.

`Reload <turret>` (key `r`) appears when the turret can be reloaded. It opens the same ammo picker as a gun, and the reload runs as a gun reload does.

When you aim a manual turret with several ammo pockets, each pocket gets one row:

- `<pocket>: <ammo> (vehicle N, need M)` when the ammo comes from the vehicle;
- `<pocket>: <ammo> (N, need M)` otherwise.

`N` is the count in the pocket and `M` is the amount one shot needs. The row is red when `N` is below one shot. The rest of the aim screen is in the Aim section above.
