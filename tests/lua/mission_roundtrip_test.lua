-- Mission create / assign / complete / fail through lib.cdf_mission, with Lua phase callbacks.
-- Two mission types: phases written as "lua:<id>" strings and as { "lua": "<id>" } objects.
local missions = require("lib.cdf_mission")

local log = {}
local function note(phase)
  return function(p)
    log[#log + 1] = phase .. ":" .. p.mission:get_uid() .. ":" .. p.mission:get_status()
    assert(p.mission_type ~= nil)
  end
end
gapi.register_mission_phase("test_mission_start", note("start"))
game.mission_functions["test_mission_end"] = note("end")
game.mission_functions["test_mission_fail"] = note("fail")

local function check_log(expected)
  assert(#log == #expected, "log length " .. #log .. ": " .. table.concat(log, ","))
  for i, line in ipairs(expected) do
    assert(log[i] == line, "log[" .. i .. "] = " .. log[i] .. ", expected " .. line)
  end
end

local m, err = missions.create("TEST_LUA_MISSION")
assert(m, err)
local uid = m:get_uid()
assert(m:get_status() == "yet_to_start")
assert(not m:is_assigned())
assert(Mission.find(uid) ~= nil and Mission.find(uid):get_uid() == uid)
assert(Mission.find(uid + 100000) == nil)
check_log({})

-- completing before the mission started does nothing
local ok, why = missions.complete(m)
assert(ok == false and why == "mission is not in progress")

-- assign runs the start phase once; status is still yet_to_start while it runs
assert(missions.assign(m))
assert(m:get_status() == "in_progress" and m:in_progress() and m:is_assigned())
check_log({ "start:" .. uid .. ":yet_to_start" })
ok, why = missions.assign(m)
assert(ok == false and why == "mission already started")
check_log({ "start:" .. uid .. ":yet_to_start" })

-- the goal (a script-set step) is not satisfied yet
ok, why = missions.complete(m)
assert(ok == false and why == "goal not complete")
assert(m:get_status() == "in_progress")

-- completing runs the end phase after the status became success
m:step_complete(1)
assert(missions.complete(m))
assert(m:get_status() == "success" and not m:in_progress())
check_log({ "start:" .. uid .. ":yet_to_start", "end:" .. uid .. ":success" })
ok = missions.fail(m)
assert(ok == false)

-- second mission (object form), failed
local f = assert(missions.create("TEST_LUA_MISSION_OBJ"))
local fuid = f:get_uid()
assert(fuid ~= uid)
assert(missions.assign(f))
assert(missions.fail(f))
assert(f:get_status() == "failure" and f:has_failed())
check_log({
  "start:" .. uid .. ":yet_to_start",
  "end:" .. uid .. ":success",
  "start:" .. fuid .. ":yet_to_start",
  "fail:" .. fuid .. ":failure",
})

-- force completes a mission whose goal is not satisfied
local g = assert(missions.create("TEST_LUA_MISSION"))
assert(missions.assign(g))
assert(missions.complete(g, { force = true }))
assert(g:get_status() == "success")

-- both are visible in the avatar's lists and in Mission.get_all
local avatar = gapi.get_avatar()
assert(#avatar:get_completed_missions() == 2)
assert(#avatar:get_failed_missions() == 1)
assert(#avatar:get_active_missions() == 0)
assert(#Mission.get_all() == 3)

-- an unknown mission type is reported as nil, not a crash
local bad, bad_why = missions.create("TEST_NO_SUCH_MISSION_TYPE")
assert(bad == nil and bad_why == "unknown mission type")

test_data.ok = true
