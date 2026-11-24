-- CDF: PC-E1 step E1-4. Lua replacements of the DDA e-file use actions that DDA writes as effects
-- on condition (DDA b4b62254ae2, data/json/effects_on_condition/computer_eocs.json).
--
-- EFILE_MAP  <- EOC_CHECK_MAP_CACHE (computer_eocs.json:19-35), item `efile_map`.
-- EFILE_LORE <- EOC_READ_LOCAL_FILES (computer_eocs.json:3-18), item `efile_lore`.
local ui = require("lib.ui")

local M = {}

-- DDA reveals around the item's spawn_location (flag PRESERVE_SPAWN_LOC), at z 0.  CDF has no
-- spawn location; an item that carries the map center var (reveal_map_center_omt, set for items of
-- the category maps) uses it, any other file reveals around the user.
local MAP_CENTER_VAR = "reveal_map_center_omt"

---@param params ItemUseParams
---@return TripointAbsOmt
local function map_cache_center(params)
  local file = params.item
  if file:has_var(MAP_CENTER_VAR) then
    local stored = file:get_var_tri(MAP_CENTER_VAR, Tripoint.new(0, 0, 0))
    return TripointAbsOmt.new(stored.x, stored.y, 0)
  end
  local pos = params.pos
  if pos == nil then pos = params.user:get_pos_ms() end
  local omt = gapi.bub_to_abs(pos):to_omt()
  return TripointAbsOmt.new(omt.x, omt.y, 0)
end

---@param params ItemUseParams
---@return integer
function M.map(params)
  local file = params.item
  if file:get_var_str("map_cache", "") == "read" then
    gapi.add_msg(MsgType.info, locale.gettext("You already noted everything this map cache can offer."))
    return 0
  end

  overmapbuffer.reveal(map_cache_center(params), gapi.rng(11, 36))
  file:set_var_str("map_cache", "read")
  gapi.add_msg(MsgType.good, locale.gettext("You found some useful data in the map cache and noted it."))
  -- DDA effect_on_conditions_actor::use returns 0 (the file is not consumed)
  return 0
end

local LORE_CATEGORY = "local_files_simple"
-- DDA keeps the id of the shown snippet in the var <category>_snippet_id of the file
-- (npctalk.cpp:5563-5578, same_snippet), so a file always shows the same snippet.
local LORE_SNIPPET_VAR = LORE_CATEGORY .. "_snippet_id"

-- DDA rolls the var snippet_file once, "has" or "lack", then acts on it (computer_eocs.json:6-8)
---@param file Item
---@return string
local function lore_roll(file)
  local roll = file:get_var_str("snippet_file", "")
  if roll ~= "has" and roll ~= "lack" then
    roll = gapi.rng(0, 1) == 0 and "has" or "lack"
    file:set_var_str("snippet_file", roll)
  end
  return roll
end

---@param params ItemUseParams
---@return integer
function M.lore(params)
  local file = params.item
  if lore_roll(file) == "lack" then
    gapi.add_msg(MsgType.neutral, locale.gettext("You found nothing interesting."))
    return 0
  end

  local avatar = gapi.get_avatar()
  local id = file:get_var_str(LORE_SNIPPET_VAR, "")
  if id == "" then
    id = gapi.random_snippet_id(LORE_CATEGORY)
    if id == nil then
      gapi.add_msg(MsgType.neutral, locale.gettext("You found nothing interesting."))
      return 0
    end
    file:set_var_str(LORE_SNIPPET_VAR, id)
    avatar:add_snippet(id)
  end
  local text = gapi.get_snippet(id)
  if text ~= nil then
    ui.popup(text)
    gapi.add_msg(MsgType.neutral, text)
  end
  -- DDA effect_on_conditions_actor::use returns 0 (the file is not consumed)
  return 0
end

return M
