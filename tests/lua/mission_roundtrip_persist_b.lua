-- Phase B: missions were serialised, cleared and loaded again (C++); callbacks are re-registered
-- the way a mod's main.lua does it. The saved mission only knows its type id and uid.
local missions = require("lib.cdf_mission")

game.mission_functions["test_mission_start"] = function(p) test_data.restarted = true end
game.mission_functions["test_mission_end"] = function(p) test_data.ended = (test_data.ended or 0) + 1 end
game.mission_functions["test_mission_fail"] = function(p) test_data.failed = true end

local m = Mission.find(test_data.uid)
assert(m ~= nil, "mission lost in save/load")
assert(m:get_uid() == test_data.uid)
assert(m:get_status() == "in_progress" and m:in_progress())
assert(m:is_assigned())
assert(m:get_assigned_player_id():get_value() == gapi.get_avatar():getID():get_value())
assert(#Mission.get_all() == 1)

-- the start phase must not run again for a restored in-progress mission
assert(test_data.restarted == nil)
assert(missions.assign(m) == false)
assert(test_data.restarted == nil)

m:step_complete(1)
assert(missions.complete(m))
assert(m:get_status() == "success")
assert(test_data.ended == 1, "end phase should run once after restore")
assert(not test_data.failed)
test_data.ok = true
