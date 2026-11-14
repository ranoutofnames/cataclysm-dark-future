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
