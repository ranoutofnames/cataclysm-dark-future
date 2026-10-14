-- Phase A: create and assign a mission, then C++ saves and clears all missions.
local missions = require("lib.cdf_mission")

game.mission_functions["test_mission_start"] = function(p) test_data.started = true end
game.mission_functions["test_mission_end"] = function(p) test_data.ended = (test_data.ended or 0) + 1 end
game.mission_functions["test_mission_fail"] = function(p) test_data.failed = true end

local m = assert(missions.create("TEST_LUA_MISSION"))
assert(missions.assign(m))
assert(m:get_status() == "in_progress")
assert(test_data.started == true)
test_data.uid = m:get_uid()
test_data.ok = true
