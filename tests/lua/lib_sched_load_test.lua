-- Load-order safety, target resolution, expiry, run_after_range and for_mod wiring of cdf_sched.
-- Uses stubbed `gapi` / `game` globals; the Lua state is private to this test.
local sched = require("lib.cdf_sched")

local clock = { t = 5 }
local errors = {}
local hooks, every_hooks = {}, {}

-- The harness runs test scripts in a sandbox env; required lib modules only see the real globals,
-- so the stubs must be written through _G.
_G.gapi = {
  current_turn = function() return TimePoint.from_turn(clock.t) end,
  add_on_every_x_hook = function(_, fn) every_hooks[#every_hooks + 1] = fn end,
  rng = function(lo) return lo end,
  get_avatar = function() return nil end,
  get_all_npcs = function() return {} end,
}
_G.game = {
  current_mod = "m",
  mod_storage = { m = {} },
  mod_runtime = { bn = {} },
  add_hook = function(name, fn)
    hooks[name] = hooks[name] or {}
    table.insert(hooks[name], fn)
  end,
}
local function fire_hook(name)
  for _, fn in ipairs(hooks[name] or {}) do fn() end
end
local function pump_all()
  for _, fn in ipairs(every_hooks) do fn() end
end

-- ---- for_mod wiring: registry reuse, single pump hook, hooks installed once ----
local S = sched.for_mod("m")
local S_again = sched.for_mod("m")
assert(S ~= S_again)
assert(#every_hooks == 1, "one pump hook")
assert(#hooks.on_game_load == 1 and #hooks.on_game_started == 1)

-- ---- register before load (stale clock, empty pre-load storage) ----
local ticks = 0
S_again.handler("tick", function() ticks = ticks + 1 end)
S_again.run_every("beat", 60, "tick", {})
S_again.run_after(30, "tick", {}, { id = "once" })
assert(game.mod_storage.m._sched == nil, "nothing written to pre-load storage")
clock.t = 5000 -- pre-load clock must not matter
pump_all()
assert(ticks == 0, "nothing fires before the world is live")

-- ---- existing save: loaded storage holds beat with phase 1060; "once" already scheduled ----
game.mod_storage.m._sched = {
  v = 1,
  seq = 0,
  tasks = {
    beat = { h = "tick", args = {}, every = 60, at = 1060 },
    once = { h = "tick", args = {}, at = 1030 },
  },
}
clock.t = 1000
fire_hook("on_game_load")
local list = S_again.list()
assert(#list == 2)
assert(S_again.turns_left("beat") == 60, "saved phase kept")
assert(S_again.turns_left("once") == 30, "existing one-shot not re-anchored")
clock.t = 1059
pump_all()
assert(ticks == 1, "only the saved 'once' fired, got " .. ticks)
clock.t = 1060
pump_all()
assert(ticks == 2)

-- ---- storage table replaced wholesale: cached next_due must be dropped ----
clock.t = 2000
S_again.cancel_all()
pump_all() -- caches next_due = huge
game.mod_storage.m = {
  _sched = { v = 1, seq = 0, tasks = { late = { h = "tick", args = {}, at = 2001 } } },
}
clock.t = 2001
pump_all()
assert(ticks == 3, "replaced storage picked up")

-- ---- new game: registrations anchor to the real start turn, nothing fires early ----
do
  local n = 0
  local G = {}
  local state = { live = false }
  clock.t = 5
  local N = sched.new({
    now = function() return clock.t end,
    storage_fn = function() return G.st end,
    ready = function() return state.live end,
  })
  G.st = {}
  N.handler("h", function() n = n + 1 end)
  N.run_every("beat", 60, "h")
  N.run_after_range(10, 20, "h", nil, { id = "rr" })
  assert(N.count() == 0, "queued, not scheduled")
  state.live = true
  clock.t = 100000 -- start turn
  G.st = {}
  N.world_loaded()
  assert(N.turns_left("beat") == 60)
  assert(N.turns_left("rr") == 10)
  N.pump()
  assert(n == 0, "nothing fired at start")
  clock.t = 100010
  N.pump()
  assert(n == 1)
end

-- ---- run_after_range bounds, TimeDuration args ----
do
  clock.t = 0
  local R = sched.new({ now = function() return clock.t end, storage = {}, rand = function(_, hi) return hi end })
  R.handler("h", function() end)
  R.run_after_range(TimeDuration.from_turns(10), TimeDuration.from_turns(40), "h", nil, { id = "a" })
  assert(R.turns_left("a") == 40)
  R.run_after_range(40, 10, "h", nil, { id = "b" }) -- swapped bounds
  assert(R.turns_left("b") == 40)
end

-- ---- target resolution and expiry ----
do
  clock.t = 0
  local avatar_present = false
  local avatar = { is_avatar = function() return true end }
  local npc_list = {}
  gapi.get_avatar = function() return avatar_present and avatar or nil end
  gapi.get_all_npcs = function() return npc_list end
  local function mk_npc(idv)
    return {
      is_avatar = function() return false end,
      is_npc = function() return true end,
      getID = function() return { get_value = function() return idv end } end,
    }
  end
  local T = sched.new({
    now = function() return clock.t end,
    storage = {},
    on_error = function(m) errors[#errors + 1] = m end,
  })
  local got = {}
  T.handler("h", function(ctx) got[#got + 1] = ctx.target end)

  -- avatar target: waits while unresolved, then fires with the target
  T.run_after(10, "h", nil, { id = "av", target = avatar })
  clock.t = 10
  T.pump()
  assert(#got == 0 and T.exists("av"), "unresolved target waits")
  avatar_present = true
  clock.t = 70
  T.pump()
  assert(#got == 1 and got[1] == avatar and not T.exists("av"))

  -- npc target resolved by id
  local npc = mk_npc(42)
  T.run_after(1, "h", nil, { id = "npc", target = npc })
  clock.t = 71
  T.pump()
  assert(#got == 1 and T.exists("npc"))
  npc_list = { mk_npc(7), npc }
  clock.t = 140
  T.pump()
  assert(#got == 2 and got[2] == npc)

  -- explicit expire drops an unresolved target task
  npc_list = {}
  T.run_after(1, "h", nil, { id = "exp", target = mk_npc(99), expire = 100 })
  clock.t = 200
  T.pump()
  assert(T.exists("exp"))
  clock.t = 300
  T.pump()
  assert(not T.exists("exp"), "expired")

  -- a plain creature is rejected as a target
  local ok = pcall(T.run_after, 1, "h", nil, { target = { is_avatar = function() return false end } })
  assert(not ok)
end

-- ---- missing handler and no target still expires ----
do
  clock.t = 0
  errors = {}
  local E = sched.new({
    now = function() return clock.t end,
    storage = {},
    on_error = function(m) errors[#errors + 1] = m end,
  })
  E.run_after(5, "nobody", nil, { id = "orphan" })
  clock.t = 10
  E.pump()
  assert(E.exists("orphan"))
  clock.t = 31 * 24 * 3600
  E.pump()
  assert(not E.exists("orphan"), "orphan expired")
  -- but a handler registered in time clears the temporary expiry
  clock.t = 0
  E.run_every("keep", 10, "late_h", nil, nil)
  clock.t = 10
  E.pump()
  local hits = 0
  E.handler("late_h", function() hits = hits + 1 end)
  clock.t = 80
  E.pump()
  assert(hits >= 1 and E.exists("keep"))
  assert(E.list()[1].id == "keep")
end

test_data.ok = true
