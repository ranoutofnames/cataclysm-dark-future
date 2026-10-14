-- CDF helper: create, assign, complete and fail missions from Lua.
-- A mission type is JSON (`mission_definition`); its start/end/fail phases can run Lua with
-- "start": "lua:<id>" (callback in game.mission_functions[<id>]). This module drives the instances.
-- Pure Lua; touches no game state at require time.
local M = {}

---@param t string|MissionTypeIdRaw
---@return MissionTypeIdRaw
local function type_id(t)
  if type(t) == "string" then return MissionTypeIdRaw.new(t) end
  return t
end

---@param a CharacterId
---@param b CharacterId
---@return boolean
local function same_player(a, b) return a:get_value() == b:get_value() end

--- Create (reserve) a mission of the given type, not yet assigned to anyone.
---@param mission_type string|MissionTypeIdRaw
---@param giver Character|CharacterId|nil mission giver (NPC); nil for none
---@return Mission?, string? @mission, or nil and a reason
M.create = function(mission_type, giver)
  local giver_id
  if giver == nil then
    giver_id = CharacterId.new()
  elseif giver.getID then
    giver_id = giver:getID()
  else
    giver_id = giver
  end
  local m = Mission.reserve_new(type_id(mission_type), giver_id)
  if m == nil then return nil, "unknown mission type" end
  return m
end

--- Assign the mission to the avatar; runs the type's start phase. Only works once, on an
--- unassigned mission that has not started.
---@param m Mission
---@return boolean, string? @true, or false and a reason
M.assign = function(m)
  if m:get_status() ~= "yet_to_start" then return false, "mission already started" end
  if m:is_assigned() then return false, "mission already assigned" end
  m:assign(gapi.get_avatar())
  return true
end

--- Complete an assigned, in-progress mission: runs the end phase and moves it to completed.
--- Unless opts.force is set, the mission goal must be satisfied (`m:is_complete(opts.npc_id)`).
---@param m Mission
---@param opts { force: boolean?, npc_id: CharacterId? }?
---@return boolean, string? @true, or false and a reason
M.complete = function(m, opts)
  opts = opts or {}
  if not m:in_progress() then return false, "mission is not in progress" end
  if not same_player(m:get_assigned_player_id(), gapi.get_avatar():getID()) then
    return false, "mission is not assigned to the avatar"
  end
  if not opts.force and not m:is_complete(opts.npc_id) then return false, "goal not complete" end
  m:wrap_up()
  return true
end

--- Fail an in-progress mission: runs the fail phase and moves it to failed.
---@param m Mission
---@return boolean, string? @true, or false and a reason
M.fail = function(m)
  if not m:in_progress() then return false, "mission is not in progress" end
  m:fail()
  return true
end

--- Mission with the given unique id, or nil.
---@param uid integer
---@return Mission?
M.find = function(uid) return Mission.find(uid) end

return M
