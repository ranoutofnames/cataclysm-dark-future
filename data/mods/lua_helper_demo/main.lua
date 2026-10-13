-- Demo of the CDF helper library: persistent scheduler + persistent variables.
local sched = require("lib.cdf_sched")
local vars = require("lib.cdf_vars")

local mod_id = game.current_mod
local mod = game.mod_runtime[mod_id]

local S = sched.for_mod(mod_id)
local V = vars.global(mod_id)

-- Handlers are registered by name on every load; saved tasks refer to these names.
S.handler("demo_heartbeat", function(ctx)
  local n = V.add("heartbeats", 1)
  gdebug.log_info("lua_helper_demo: heartbeat " .. tostring(n))
end)

S.handler("demo_alarm", function(ctx)
  gapi.add_msg("Helper demo: the 5 minute alarm goes off!")
  V.inc("alarms")
end)

-- Idempotent: safe to call on each load, keeps the existing phase.
S.run_every("heartbeat", TimeDuration.from_minutes(1), "demo_heartbeat", {}, { catch_up = "skip" })

-- main.lua runs before the world loads, so one-shots that should happen once per game are
-- scheduled from the on_game_started hook (registered in preload.lua). The recurring task above is
-- queued by the library and applied after load, keeping its saved phase.
mod.on_game_started = function()
  V.set("alarm_scheduled", true)
  S.run_after(TimeDuration.from_minutes(5), "demo_alarm", {}, { id = "alarm" })
end

mod.show_status = function()
  local left = S.time_left("alarm")
  gapi.add_msg(
    string.format(
      "Helper demo: heartbeats=%d alarms=%d tasks=%d alarm_left=%s",
      V.get("heartbeats", 0),
      V.get("alarms", 0),
      #S.list(),
      left and tostring(left:to_turns()) .. " turns" or "none"
    )
  )
end
