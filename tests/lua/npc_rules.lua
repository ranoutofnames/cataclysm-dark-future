---@type NPC
local guy = test_data.npc

-- defaults: allow_pick_up is off, use_guns is on
test_data.pick_up_default = guy:get_rule("allow_pick_up")
test_data.use_guns_default = guy:get_rule("use_guns")

-- set / get round trip
test_data.set_on_ok = guy:set_rule("allow_pick_up", true)
test_data.pick_up_on = guy:get_rule("allow_pick_up")
test_data.set_off_ok = guy:set_rule("allow_pick_up", false)
test_data.pick_up_off = guy:get_rule("allow_pick_up")

-- unknown names are reported, not errors
test_data.unknown_get_is_nil = guy:get_rule("no_such_rule") == nil
test_data.unknown_set = guy:set_rule("no_such_rule", true)

-- a temporary override (installed by the test) hides the permanent state
-- unless check_override is false; set_rule only touches the permanent state
test_data.use_guns_effective = guy:get_rule("use_guns")
test_data.use_guns_permanent = guy:get_rule("use_guns", false)
guy:set_rule("use_guns", false)
test_data.use_guns_permanent_after = guy:get_rule("use_guns", false)
test_data.use_guns_effective_after = guy:get_rule("use_guns")
