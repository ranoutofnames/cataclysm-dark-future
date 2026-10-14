-- A pocket handle must fail cleanly (no crash, a Lua error or false) once the item it belongs
-- to has been destroyed. test_data.destroy_* destroy the owner from the C++ side, so the handle
-- outlives it exactly like a script that holds a pocket of an item that burns or is consumed.
local gun = gapi.create_item(ItypeId.new("glock_19"), -1)

local function first_holster(owner)
  for _, p in ipairs(owner:get_pockets()) do
    if p:is_holster() then
      return p
    end
  end
  return nil
end

local function check_owner_destroyed(label, owner, destroy)
  local pockets = owner:get_pockets()
  assert(#pockets == 2, label .. ": expected 2 pockets, got " .. #pockets)
  local holster = first_holster(owner)
  assert(holster ~= nil, label .. ": no holster pocket")

  -- while the owner lives every method works
  assert(holster:is_valid(), label .. ": handle not valid before destroy")
  assert(holster:is_holster(), label .. ": is_holster")
  assert(#holster:items() == 0, label .. ": holster should be empty")
  holster:volume_capacity()
  holster:remaining_volume()
  holster:weight_capacity()
  holster:remaining_weight()
  holster:type()
  holster:watertight()
  holster:sealed()
  assert(holster:can_contain(gun), label .. ": holster should take the pistol")

  destroy()

  -- is_valid answers without raising; every other method raises a Lua error
  assert(not holster:is_valid(), label .. ": handle still valid after destroy")
  for _, other in ipairs(pockets) do
    assert(not other:is_valid(), label .. ": sibling handle still valid after destroy")
  end

  local probes = {
    type = function() return holster:type() end,
    volume_capacity = function() return holster:volume_capacity() end,
    remaining_volume = function() return holster:remaining_volume() end,
    weight_capacity = function() return holster:weight_capacity() end,
    remaining_weight = function() return holster:remaining_weight() end,
    is_holster = function() return holster:is_holster() end,
    watertight = function() return holster:watertight() end,
    sealed = function() return holster:sealed() end,
    items = function() return holster:items() end,
    can_contain = function() return holster:can_contain(gun) end,
  }
  for name, probe in pairs(probes) do
    local ok, err = pcall(probe)
    assert(not ok, label .. ": " .. name .. " did not raise on a destroyed owner")
    assert(tostring(err):find("stale", 1, true),
      label .. ": " .. name .. " raised an unexpected error: " .. tostring(err))
  end
  return holster
end

local detached_holster = check_owner_destroyed("detached", test_data["detached_owner"],
  test_data["destroy_detached_owner"])
local map_holster = check_owner_destroyed("map", test_data["map_owner"],
  test_data["destroy_map_owner"])

-- the memory of the destroyed items is released at the end of the turn: the handles must
-- still be safe afterwards, even if the address is reused by a new item
test_data["reclaim"]()
local reuse = gapi.create_item(ItypeId.new("test_pocket_rig"), -1)
assert(#reuse:get_pockets() == 2)
assert(not detached_holster:is_valid(), "detached handle valid again after reclaim")
assert(not map_holster:is_valid(), "map handle valid again after reclaim")
assert(not pcall(function() return detached_holster:remaining_volume() end))
assert(not pcall(function() return map_holster:remaining_volume() end))

test_data["ok"] = true
