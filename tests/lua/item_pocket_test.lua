-- Pocket API: item:get_pockets, try_put_in, can_contain_item, itype:pockets
local rig = gapi.create_item(ItypeId.new("test_pocket_rig"), -1)

local pockets = rig:get_pockets()
test_data["total"] = #pockets
test_data["containers"] = #rig:get_pockets(PocketType.CONTAINER)
test_data["mods"] = #rig:get_pockets(PocketType.MOD)

local holster = nil
for _, p in ipairs(pockets) do
  if p:is_holster() then
    holster = p
  end
end
test_data["holster_found"] = holster ~= nil

local pistol = gapi.create_item(ItypeId.new("glock_19"), -1)
local can, can_why = rig:can_contain_item(pistol)
test_data["can_contain"] = can
test_data["can_why_empty"] = can_why == nil or can_why == ""

local put_ok, put_why = rig:try_put_in(pistol)
test_data["put_ok"] = put_ok
test_data["holster_items"] = #holster:items()

-- too big for any pocket: refused with a reason
local jug = gapi.create_item(ItypeId.new("test_jug_plastic"), -1)
local jug_ok, jug_why = rig:try_put_in(jug)
test_data["jug_ok"] = jug_ok
test_data["jug_why_given"] = jug_why ~= nil and jug_why ~= ""

-- type data
local type_pockets = rig:get_type():obj():pockets()
test_data["type_pockets"] = #type_pockets
test_data["type_holsters"] = 0
for _, p in ipairs(type_pockets) do
  if p:is_holster() then
    test_data["type_holsters"] = test_data["type_holsters"] + 1
  end
end

-- the old bindings still work
test_data["old_can_contain"] = rig:can_contain(gapi.create_item(ItypeId.new("glock_19"), -1))
test_data["old_slot_container_nil"] = rig:get_type():obj():slot_container() == nil
