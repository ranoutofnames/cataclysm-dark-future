-- Demo of Lua mission phases. missions.json refers to these callbacks as "lua:<id>".
local vars = require("lib.cdf_vars")
local missions = require("lib.cdf_mission")

local mod_id = game.current_mod
local mod = game.mod_runtime[mod_id]

-- Phase callbacks live in game.mission_functions and get { mission, mission_type }.
-- Re-registered on every load; saved missions only store the mission type id and uid.
game.mission_functions["cdf_demo_mission_start"] = function(params)
  vars.global(mod_id).inc("started")
  gapi.add_msg("Lua demo mission " .. params.mission:get_uid() .. " started.")
end

game.mission_functions["cdf_demo_mission_end"] = function(params)
  vars.global(mod_id).inc("completed")
  gapi.add_msg("Lua demo mission " .. params.mission:get_uid() .. " completed.")
end

game.mission_functions["cdf_demo_mission_fail"] = function(params)
  vars.global(mod_id).inc("failed")
  gapi.add_msg("Lua demo mission " .. params.mission:get_uid() .. " failed.")
end

-- Console helpers. Example: game.mod_runtime["cdf_lua_mission_demo"].start()
mod.start = function()
  local m = missions.create("CDF_LUA_DEMO_MISSION")
  assert(missions.assign(m))
  return m:get_uid()
end

-- The mission goal is satisfied by a script, so mark the step done, then complete.
mod.finish = function(uid)
  local m = missions.find(uid)
  m:step_complete(1)
  return missions.complete(m)
end

mod.abandon = function(uid) return missions.fail(missions.find(uid)) end

mod.show_status = function()
  local V = vars.global(mod_id)
  gapi.add_msg(
    string.format(
      "Lua mission demo: started=%d completed=%d failed=%d",
      V.get("started", 0),
      V.get("completed", 0),
      V.get("failed", 0)
    )
  )
end
