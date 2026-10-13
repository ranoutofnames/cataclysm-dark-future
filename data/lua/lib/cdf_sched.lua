-- CDF helper: persistent scheduler (replacement for DDA EOC RECURRING / time_in_future).
--
-- Saved tasks reference handlers BY NAME; functions cannot be saved. Register handlers from
-- main.lua on every load with S.handler(name, fn). Task args must be plain serialisable data.
-- All times are integer turns (1 turn = 1 second) internally; durations may be given as ints
-- (turns) or TimeDuration objects.
local time = require("lib.cdf_time")

local M = {}

local SCHEMA_VERSION = 1
local RETRY_BLOCKED = 60 -- turns between retries for tasks whose handler or target is missing
local DEFAULT_EXPIRE = 30 * 24 * 3600 -- 30 days, for target-bound tasks
local DEFAULT_MAX_CATCH_UP = 10

local function default_now() return gapi.current_turn():to_turn() end

local function default_rand(lo, hi)
  if gapi and gapi.rng then return gapi.rng(lo, hi) end
  return math.random(lo, hi)
end

local function default_on_error(msg)
  if gdebug and gdebug.log_error then gdebug.log_error(msg) end
end

local function as_turns(x) return time.to_turns(x) end

--- Create a scheduler engine.
---@param opts table? { now=fn->turn|TimePoint, storage=table, storage_fn=fn->table, on_error=fn,
---  rand=fn(lo,hi), ready=fn->boolean }. `ready` tells whether the world (and its saved storage) is
---  live; while it returns false, run_every/run_after calls are queued (see S.world_loaded).
---  `storage_fn` re-reads the storage table on every access (it is replaced by a world load).
function M.new(opts)
  opts = opts or {}
  local S = {}
  local get_now = opts.now or default_now
  local rand = opts.rand or default_rand
  local on_error = opts.on_error or default_on_error
  local storage = opts.storage or {}
  local storage_fn = opts.storage_fn
  local ready = opts.ready
  local last_storage = nil
  local pending_every = {} -- id -> { interval, handler, args, o }; replayed on every load (idempotent)
  local pending_every_order = {}
  local pending_after = {} -- one-shot registrations made before the world was live
  local handlers = {}
  local ifns = {} -- runtime-only function intervals, by task id
  local retry_at = {} -- runtime-only: id -> turn before which a blocked task is not retried
  local warned = {}
  local next_due = nil -- cached earliest due time; nil means "recompute"

  local function now()
    local n = get_now()
    if type(n) == "number" then return n end
    return n:to_turn()
  end

  local function tasks()
    if storage_fn then storage = storage_fn() end
    if storage ~= last_storage then
      last_storage = storage
      next_due = nil -- the storage table was replaced: any cached due time is stale
    end
    local st = storage._sched
    if st == nil then
      st = { v = SCHEMA_VERSION, seq = 0, tasks = {} }
      storage._sched = st
    end
    if st.tasks == nil then st.tasks = {} end
    if st.seq == nil then st.seq = 0 end
    return st.tasks
  end

  -- Task ids become table keys; keep them strings so serialisation is stable.
  local function norm_id(id) return tostring(id) end

  local function roll_interval(t, id)
    local fn = ifns[id]
    if fn then
      local ok, r = pcall(fn)
      if ok and r ~= nil then
        local turns = math.max(1, as_turns(r))
        t.every = turns
        return turns
      end
      if not ok then on_error("cdf_sched: interval function for '" .. id .. "' failed: " .. tostring(r)) end
    end
    if t.range then return rand(t.range[1], t.range[2]) end
    return math.max(1, t.every or 1)
  end

  local function set_interval(t, id, interval)
    t.range, t.every = nil, nil
    ifns[id] = nil
    if type(interval) == "function" then
      ifns[id] = interval
      t.every = 1
    elseif type(interval) == "table" then
      local lo = as_turns(interval.lo or interval[1])
      local hi = as_turns(interval.hi or interval[2])
      if hi < lo then lo, hi = hi, lo end
      t.range = { lo, hi }
      t.every = lo
    else
      t.every = math.max(1, as_turns(interval))
    end
  end

  local function target_of(creature)
    if creature.is_avatar and creature:is_avatar() then return { kind = "avatar" } end
    if creature.is_npc and creature:is_npc() then return { kind = "npc", id = creature:getID():get_value() } end
    error("cdf_sched: target must be the avatar or an NPC (monsters have no stable id)", 3)
  end

  local function resolve_target(tgt)
    if tgt == nil then return true, nil end
    if tgt.kind == "avatar" then
      local a = gapi.get_avatar()
      return a ~= nil, a
    end
    if tgt.kind == "npc" then
      for _, n in ipairs(gapi.get_all_npcs()) do
        if n:getID():get_value() == tgt.id then return true, n end
      end
    end
    return false, nil
  end

  local function mark_dirty() next_due = nil end

  local function put(id, t, o)
    if t.args == nil then t.args = {} end
    if o then
      if o.target then t.tgt = target_of(o.target) end
      if o.expire then t.exp = now() + as_turns(o.expire) end
      if o.catch_up then t.cu = o.catch_up end
      if o.max_catch_up then t.mcu = o.max_catch_up end
    end
    if t.tgt and not t.exp then t.exp = now() + DEFAULT_EXPIRE end
    tasks()[id] = t
    retry_at[id] = nil
    mark_dirty()
    return id
  end

  local function new_id()
    tasks() -- make sure state exists
    local st = storage._sched
    st.seq = st.seq + 1
    return "auto_" .. st.seq
  end

  ---Register a runtime handler under a name. Call on every load.
  function S.handler(name, fn)
    handlers[name] = fn
    warned[name] = nil
    for id in pairs(retry_at) do retry_at[id] = nil end
    mark_dirty()
  end

  ---One-shot task after `delay`. Returns the task id. opts.id makes it idempotent.
  function S.run_after(delay, handler, args, o)
    if ready and not ready() then
      pending_after[#pending_after + 1] = { delay = delay, handler = handler, args = args, o = o }
      return o and o.id and norm_id(o.id) or nil
    end
    local id = (o and o.id) and norm_id(o.id) or new_id()
    return put(id, { at = now() + as_turns(delay), h = handler, args = args }, o)
  end

  ---One-shot task after a random delay in [lo, hi].
  function S.run_after_range(lo, hi, handler, args, o)
    lo, hi = as_turns(lo), as_turns(hi)
    if hi < lo then lo, hi = hi, lo end
    return S.run_after(rand(lo, hi), handler, args, o)
  end

  ---Recurring task, idempotent by id: re-calling keeps the existing phase.
  function S.run_every(id, interval, handler, args, o)
    id = norm_id(id)
    if ready and not ready() then
      if pending_every[id] == nil then pending_every_order[#pending_every_order + 1] = id end
      pending_every[id] = { interval = interval, handler = handler, args = args, o = o }
      if type(interval) == "function" then ifns[id] = interval end
      return id
    end
    local existing = tasks()[id]
    local t = existing or {}
    t.h = handler
    t.args = args
    set_interval(t, id, interval)
    if not existing then
      local first
      if o and o.first ~= nil then
        first = as_turns(o.first)
      else
        first = roll_interval(t, id)
      end
      t.at = now() + first
    end
    return put(id, t, o)
  end

  function S.cancel(id)
    id = norm_id(id)
    pending_every[id] = nil
    local all = tasks()
    if all[id] == nil then return false end
    all[id] = nil
    ifns[id] = nil
    retry_at[id] = nil
    mark_dirty()
    return true
  end

  ---Cancel every task, or every task using `handler`. Returns the number cancelled.
  function S.cancel_all(handler)
    local ids = {}
    for id, t in pairs(tasks()) do
      if handler == nil or t.h == handler then ids[#ids + 1] = id end
    end
    for _, id in ipairs(ids) do S.cancel(id) end
    return #ids
  end

  ---Call once the world is live and its saved storage has been loaded (on_game_load / on_game_started).
  ---Drops cached state, then applies registrations queued while no world was loaded: recurring
  ---tasks keep the saved phase (or anchor to the real current turn), one-shots with an id that is
  ---already scheduled are left alone.
  function S.world_loaded()
    next_due = nil
    last_storage = nil
    for id in pairs(retry_at) do retry_at[id] = nil end
    for _, id in ipairs(pending_every_order) do
      local p = pending_every[id]
      if p then S.run_every(id, p.interval, p.handler, p.args, p.o) end
    end
    local after = pending_after
    pending_after = {}
    for _, p in ipairs(after) do
      if not (p.o and p.o.id and S.exists(p.o.id)) then S.run_after(p.delay, p.handler, p.args, p.o) end
    end
    mark_dirty()
  end

  function S.exists(id) return tasks()[norm_id(id)] ~= nil end

  ---Time left until the task is due as a TimeDuration (0 if overdue), or nil.
  function S.time_left(id)
    local t = tasks()[norm_id(id)]
    if t == nil then return nil end
    return time.to_duration(math.max(0, t.at - now()))
  end

  ---Same as time_left but as an integer number of turns.
  function S.turns_left(id)
    local t = tasks()[norm_id(id)]
    if t == nil then return nil end
    return math.max(0, t.at - now())
  end

  ---Array of {id, handler, at (turn), every (turns|nil)} sorted by due time then id.
  function S.list()
    local out = {}
    for id, t in pairs(tasks()) do
      out[#out + 1] = { id = id, handler = t.h, at = t.at, every = t.every, range = t.range }
    end
    table.sort(out, function(a, b)
      if a.at ~= b.at then return a.at < b.at end
      return a.id < b.id
    end)
    return out
  end

  ---Number of pending tasks.
  function S.count()
    local n = 0
    for _ in pairs(tasks()) do n = n + 1 end
    return n
  end

  local function effective_due(id, t)
    local r = retry_at[id]
    if r and r > t.at then return r end
    return t.at
  end

  local function compute_next_due()
    local best = math.huge
    for id, t in pairs(tasks()) do
      local d = effective_due(id, t)
      if d < best then best = d end
    end
    next_due = best
  end

  -- Run one firing. Returns "done" | "blocked" | "cancel" | "error".
  local function fire(id, t, cur, scheduled_for)
    local fn = handlers[t.h]
    if fn == nil then
      if not warned[t.h] then
        warned[t.h] = true
        on_error("cdf_sched: no handler registered for '" .. tostring(t.h) .. "' (task '" .. id .. "')")
      end
      return "blocked"
    end
    local found, target = resolve_target(t.tgt)
    if not found then return "blocked" end
    local ctx = {
      id = id,
      handler = t.h,
      args = t.args,
      target = target,
      scheduled_for = time.to_timepoint(scheduled_for),
      late = time.to_duration(math.max(0, cur - scheduled_for)),
    }
    local ok, res = pcall(fn, ctx)
    if not ok then
      on_error("cdf_sched: handler '" .. t.h .. "' (task '" .. id .. "') failed: " .. tostring(res))
      return "error"
    end
    if res == false then return "cancel" end
    return "done"
  end

  -- A blocked task (missing handler or target) gets the default expiry so it cannot linger forever.
  local function note_blocked(id, t, cur)
    retry_at[id] = cur + RETRY_BLOCKED
    if not t.exp then
      t.exp = cur + DEFAULT_EXPIRE
      t.bx = true
    end
  end

  local function note_ok(t)
    if t.bx then t.exp, t.bx = nil, nil end
  end

  local function pump_recurring(id, t, cur)
    local scheduled_for = t.at
    local r = fire(id, t, cur, scheduled_for)
    if r == "blocked" then
      note_blocked(id, t, cur)
      return
    end
    note_ok(t)
    if r == "cancel" then
      if tasks()[id] == t then S.cancel(id) end
      return
    end
    if tasks()[id] ~= t then return end -- handler replaced or cancelled it
    if r == "error" then
      t.at = cur + RETRY_BLOCKED
      return
    end
    local nxt = scheduled_for + roll_interval(t, id)
    if t.cu == "all" and nxt <= cur then
      local cap = t.mcu or DEFAULT_MAX_CATCH_UP
      local ran = 0
      while nxt <= cur and ran < cap do
        local r2 = fire(id, t, cur, nxt)
        ran = ran + 1
        if r2 == "cancel" then
          if tasks()[id] == t then S.cancel(id) end
          return
        end
        if tasks()[id] ~= t then return end
        nxt = nxt + roll_interval(t, id)
      end
    end
    if nxt <= cur then nxt = cur + roll_interval(t, id) end
    t.at = nxt
  end

  ---Run all due tasks. Called automatically once per turn by the installed hook.
  function S.pump()
    local cur = now()
    tasks() -- re-reads the storage first: a replaced table must invalidate the cached next_due
    if next_due ~= nil and cur < next_due then return end
    local due = {}
    for id, t in pairs(tasks()) do
      if effective_due(id, t) <= cur then due[#due + 1] = { id = id, t = t, at = t.at } end
    end
    table.sort(due, function(a, b)
      if a.at ~= b.at then return a.at < b.at end
      return a.id < b.id
    end)
    for _, d in ipairs(due) do
      local id, t = d.id, d.t
      if tasks()[id] == t then -- still present and not replaced earlier in this pump
        if t.exp and cur >= t.exp then
          S.cancel(id)
        elseif t.every ~= nil then
          pump_recurring(id, t, cur)
        else
          local r = fire(id, t, cur, t.at)
          if r == "blocked" then
            note_blocked(id, t, cur)
          elseif r == "error" then
            if tasks()[id] == t then t.at = cur + RETRY_BLOCKED end
          elseif tasks()[id] == t then
            tasks()[id] = nil -- one-shot done (a handler may have re-used the id: then t differs)
          end
        end
      end
    end
    compute_next_due()
  end

  return S
end

-- ---------------------------------------------------------------------------------------
-- Game wiring. Reload-safe: exactly one on_every_x hook per Lua state; it pumps whatever
-- scheduler instances are currently registered (each require overwrites its mod's slot).
-- ---------------------------------------------------------------------------------------

local function holder_table()
  local rt = game.mod_runtime
  local h = rt["bn"]
  if h == nil then h = rt[game.current_mod] end
  return h
end

---Scheduler bound to a mod: state in game.mod_storage[mod_id]._sched.
---main.lua runs at data load, before the world's saved storage is loaded and before a new game's
---start turn exists. Registrations made until then are queued and applied from the lib's own
---on_game_load / on_game_started hooks.
---@param mod_id string
function M.for_mod(mod_id)
  local h = holder_table()
  local reg = h._cdf_sched_registry
  if reg == nil then
    reg = { instances = {}, installed = false, world_live = false }
    h._cdf_sched_registry = reg
  end
  local S = M.new({
    storage_fn = function() return game.mod_storage[mod_id] end,
    ready = function() return reg.world_live end,
  })
  reg.instances[mod_id] = S
  if not reg.installed then
    reg.installed = true
    local function loaded()
      reg.world_live = true
      for _, inst in pairs(reg.instances) do
        local ok, err = pcall(inst.world_loaded)
        if not ok then default_on_error("cdf_sched: world_loaded failed: " .. tostring(err)) end
      end
    end
    game.add_hook("on_game_load", loaded)
    game.add_hook("on_game_started", loaded)
    gapi.add_on_every_x_hook(TimeDuration.from_turns(1), function()
      for _, inst in pairs(reg.instances) do
        local ok, err = pcall(inst.pump)
        if not ok then default_on_error("cdf_sched: pump failed: " .. tostring(err)) end
      end
    end)
  end
  return S
end

return M
