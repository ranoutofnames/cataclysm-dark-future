# Pockets and ammo bindings

This guide describes the Lua bindings in cdf/p3 at 08ed04b1fe, the commit it was written for; bindings added by later merges are not listed here. It is for modders who read `item` and `character` values from scripts. For how to walk pockets and put things in them, see [Looking at item pockets and putting things in them](cookbook.md#looking-at-item-pockets-and-putting-things-in-them) and [Carried items and pockets](cookbook.md#carried-items-and-pockets) in the cookbook.

The examples assume an item in `you:all_items(false)` as the cookbook gets one.

```lua
local you = gapi.get_avatar()
local gun = you:all_items(false)[1] -- a gun you carry
```

## Loaded magazines

### magazines()

Returns the magazines currently loaded in the item, from its magazine wells and magazine pockets, as a list of item handles. The list is empty when none is loaded.

```lua
local list = gun:magazines()
local names = {}
for i, m in ipairs(list) do
  names[i] = m:get_type():str()
end
print(#list)     -- 2 for a gun with two loaded wells
print(names[1])  -- glockmag
print(names[2])  -- stanag30
```

Proved by the test case `lua_item_magazines_binding`: two loaded magazines in pocket order, and 0 for an empty gun.

### ammo_remaining()

Returns the total ammo of the item. The form without an argument is unchanged by the pocket work.

```lua
print(gun:ammo_remaining()) -- 30 for an m4a1 loaded with 30 rounds
```

Proved by the test case `lua_item_ammo_remaining_total_unchanged`.

### ammo_remaining( idx )

Returns the ammo of one pocket. The idx is 0-based and counts every pocket of the item in `get_pockets()` order, not only the magazine wells; it returns 0 for an out-of-range idx and for a pocket that is not a magazine well.

```lua
-- Walk the pockets and remember the index of each loaded magazine
local idx = 0
local well = {}
for _, pocket in ipairs(gun:get_pockets()) do
  for _, it in ipairs(pocket:items()) do
    well[it:get_type():str()] = idx
  end
  idx = idx + 1
end
print(gun:ammo_remaining(well["glockmag"])) -- 15
print(gun:ammo_remaining(well["stanag30"])) -- 7
print(gun:ammo_remaining(#gun:get_pockets())) -- 0, out of range
print(gun:ammo_remaining(-1))                 -- 0, negative
```

Proved by the test case `lua_item_ammo_remaining_index`. Every gun has a MOD pocket, so it counts as one of the pockets.

## Pockets by id

### pocket_by_id( id )

Returns the pocket whose pocket id (the `id` of its `pocket_data` entry, for example `ammo` or `power`) matches, or nil when the id is empty or no pocket has it. If several pockets share the id, the first in `get_pockets()` order is returned. The handle is the same kind that `get_pockets()` returns.

```lua
local ammo = gun:pocket_by_id("ammo")
print(ammo:is_valid())                    -- true
print(#ammo:items())                      -- 1 (glockmag in the test fixture)
print(#gun:pocket_by_id("power"):items()) -- 0 (the power pocket holds nothing)
print(gun:pocket_by_id("") == nil)        -- true
print(gun:pocket_by_id("nope") == nil)    -- true
```

The ids above are examples only; use the ids of your own item's `pocket_data`. The test case `lua_item_pocket_by_id_resolves_the_named_pocket` proves these values.

A handle goes stale when its item is destroyed: `is_valid()` then returns false.

```lua
local h = gun:pocket_by_id("ammo")
print(h:is_valid()) -- true while the gun exists
-- after the gun is destroyed, h:is_valid() returns false
```

Proved by the test case `lua_item_pocket_by_id_handle_goes_stale`.

## Firing requirements

### uses_firing_requirements()

Returns true when the item type declares `firing_requirements`. It checks the item level only: a bare gunmod is false, and classic items are false.

```lua
print(gun:uses_firing_requirements()) -- true for the ammo-well gun of the test
-- false for an m4a1 with 30 rounds, and for a bare stanag30 magazine
```

Proved by the test cases `lua_item_uses_firing_requirements_true` and `lua_item_uses_firing_requirements_false`.

## Spending ammo

### ammo_consume( qty, pos )

Consumes `qty` units of ammo and returns how many were consumed. Only the two-argument form exists in Lua, `ammo_consume( qty, pos )`; the three-argument C++ overload with a carrier is not bound.

```lua
local pos = TripointBubMs.new(0, 0, 0)
local consumed = gun:ammo_consume(3, pos)
print(consumed)              -- 3
print(gun:ammo_remaining())  -- drops from 30 to 27
```

Proved by the test case `lua_item_ammo_consume_two_arg_unchanged`.

## Magazine jam multiplier

### mag_jam_mult

`islot_magazine.mag_jam_mult` is the multiplier of the chance that a magazine makes its gun jam from physical damage: 1.0 is standard. The field is read-only from Lua: assigning to it is an error.

```lua
local mag = gapi.create_item(ItypeId.new("stanag30"), -1)
print(mag:get_type():obj():slot_magazine().mag_jam_mult) -- 1.0
local m4 = gapi.create_item(ItypeId.new("m4a1"), -1)
print(m4:get_type():obj():slot_magazine() == nil)       -- true, the m4a1 has no magazine slot
```

Proved by the test cases `lua_islot_magazine_mag_jam_mult_default`, `lua_islot_magazine_mag_jam_mult_reads_data` (a test magazine with 2.5 reads 2.5) and `lua_islot_magazine_mag_jam_mult_readonly`.

## Carried volume

### volume_carried

Returns the volume of what is carried in the carrying pockets: the contents, not the worn items themselves.

### carried_capacity()

Returns the capacity of the carrying pockets, the pair of `volume_carried`.

### volume_capacity()

Returns the total capacity of every container pocket, holsters and plates included. It is not the pair of `volume_carried`.

To compare contents with room, use `volume_carried` against `carried_capacity()`.

## Battery charge in a mod

The e-book mod reads its battery through `ammo_remaining`, not through `charges`: its percentage is the ammo left divided by the ammo capacity. The line is in the mod file `data/mods/ebook_lua/main.lua`, not in the API, and the arithmetic may change with the mod.

`local charge_p = math.floor(item:ammo_remaining() / item:ammo_capacity(false) * 100 + 0.5)`

Proved by the test case `ebook_lua_battery_arithmetic_uses_ammo_remaining`.
