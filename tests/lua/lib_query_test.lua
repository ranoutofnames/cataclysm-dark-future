local q = require("lib.cdf_query")

-- chance helpers: deterministic edges
assert(q.chance(0, 5) == false)
assert(q.chance(5, 5) == true)
assert(q.chance(1, 0) == false)
assert(q.one_in(1) == true)
local hits = 0
for _ = 1, 400 do
  if q.one_in(2) then hits = hits + 1 end
end
assert(hits > 100 and hits < 300, "one_in(2) hits " .. hits)

-- creature queries
local av = gapi.get_avatar()

assert(q.has_effect(av, "downed") == false)
assert(q.effect_intensity(av, "downed") == 0)
assert(q.has_trait(av, "NYCTOPHOBIA") == false)
assert(q.has_item_with_flag(av, "WATERPROOF") ~= nil)
assert(q.inventory_find(av, function() return false end) == nil)

-- has_quality: stub character holding a real hammer item
do
  local hammer = Item.spawn(ItypeId.new("hammer"), -1)
  local holder = { all_items = function() return { hammer } end }
  assert(q.has_quality(holder, "HAMMER", 3) == true)
  assert(q.has_quality(holder, "HAMMER") == true)
  assert(q.has_quality(holder, "HAMMER", 4) == false)
  assert(q.has_quality(holder, "SCREW") == false)
  assert(q.has_quality({ all_items = function() return {} end }, "HAMMER") == false)
  assert(q.has_quality(av, "NO_SUCH_QUALITY_ID") == false)
end

-- distance + nearby
local near = test_data.near_monster
local far = test_data.far_monster
assert(q.distance(av, av) == 0)
assert(q.distance(av, near) == test_data.near_dist)
assert(q.distance(av, far) == test_data.far_dist)
assert(q.distance(av, near, "square") == test_data.near_dist)
local found = q.monsters_nearby(av, test_data.near_dist)
assert(#found == 1 and found[1] == near, "nearby count " .. #found)
assert(q.count_nearby(av, test_data.far_dist) == 2)
assert(q.count_nearby(av, test_data.far_dist, { type_ids = { "mon_zombie" } }) == 2)
assert(q.count_nearby(av, test_data.far_dist, { type_ids = { "mon_no_such" } }) == 0)
assert(q.count_nearby(av, test_data.far_dist, { exclude = near }) == 1)
assert(q.count_nearby(av, test_data.far_dist, { pred = function(m) return m == far end }) == 1)
assert(q.count_nearby(av, test_data.far_dist, { species_ids = { "ZOMBIE" } }) == 2)
assert(q.count_nearby(av, test_data.far_dist, { species_ids = { "BIRD", "ZOMBIE" } }) == 2)
assert(q.count_nearby(av, test_data.far_dist, { species_ids = { "BIRD" } }) == 0)
assert(q.count_nearby(av, test_data.far_dist, { faction_ids = { "zombie" } }) == 2)
assert(q.count_nearby(av, test_data.far_dist, { faction_ids = { "human" } }) == 0)
assert(#q.npcs_nearby(av, 50) == 0)

test_data.ok = true
