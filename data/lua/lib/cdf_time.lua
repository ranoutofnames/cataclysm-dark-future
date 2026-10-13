-- CDF helper: time conversions. Pure Lua; touches no game state at require time.
-- Internal convention: a "turn" is one in-game second (the unit of TimePoint:to_turn).
local M = {}

---@param x any
---@return boolean
local function is_obj(x) return type(x) == "userdata" or type(x) == "table" end

--- Current game time as a TimePoint.
M.now = function() return gapi.current_turn() end

--- Current game time as an integer turn count.
M.now_turn = function() return gapi.current_turn():to_turn() end

--- Convert an int (turns), TimeDuration, or TimePoint into an integer turn count.
---@param x number|TimeDuration|TimePoint
---@return integer
M.to_turns = function(x)
  if type(x) == "number" then return math.floor(x) end
  if is_obj(x) then
    if x.to_turns then return x:to_turns() end
    if x.to_turn then return x:to_turn() end
  end
  error("cdf_time.to_turns: expected number, TimeDuration or TimePoint, got " .. type(x), 2)
end

--- Convert an int (turns) or TimeDuration into a TimeDuration.
---@param x number|TimeDuration
---@return TimeDuration
M.to_duration = function(x)
  if type(x) == "number" then return TimeDuration.from_turns(math.floor(x)) end
  return x
end

--- Convert an int (turn) or TimePoint into a TimePoint.
---@param x number|TimePoint
---@return TimePoint
M.to_timepoint = function(x)
  if type(x) == "number" then return TimePoint.from_turn(math.floor(x)) end
  return x
end

--- Shorthand for a TimeDuration of `n` turns (seconds).
---@param n number
---@return TimeDuration
M.turns = function(n) return TimeDuration.from_turns(math.floor(n)) end

--- Time elapsed since `tp` (TimePoint or integer turn).
---@param tp TimePoint|integer
---@return TimeDuration
M.since = function(tp) return TimeDuration.from_turns(M.now_turn() - M.to_turns(tp)) end

return M
