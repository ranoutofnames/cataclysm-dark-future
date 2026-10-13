local sched = require("lib.cdf_sched")

local clock = { t = 1000 }
local errors = {}
local function make(storage)
  return sched.new({
    now = function() return clock.t end,
    storage = storage or {},
    on_error = function(m) errors[#errors + 1] = m end,
    rand = function(lo, hi) return lo end,
  })
end
local function advance(S, to)
  clock.t = to
  S.pump()
end

-- one-shot: not before due, once at/after due
do
  local S = make()
  local hits = {}
  S.handler("h", function(ctx) hits[#hits + 1] = ctx end)
  local id = S.run_after(100, "h", { x = 1 })
  assert(S.exists(id))
  assert(S.turns_left(id) == 100)
  advance(S, 1099)
  assert(#hits == 0)
  advance(S, 1100)
  assert(#hits == 1 and hits[1].args.x == 1 and hits[1].id == id and hits[1].late:to_turns() == 0)
  assert(hits[1].scheduled_for:to_turn() == 1100)
  assert(not S.exists(id))
  advance(S, 5000)
  assert(#hits == 1)
  assert(S.time_left(id) == nil)
end

-- TimeDuration delay accepted; late reported
do
  clock.t = 0
  local S = make()
  local late
  S.handler("h", function(ctx) late = ctx.late:to_turns() end)
  S.run_after(TimeDuration.from_minutes(1), "h")
  advance(S, 75)
  assert(late == 15)
end

-- recurring: period, phase kept on idempotent re-register
do
  clock.t = 0
  local S = make()
  local n = 0
  S.handler("tick", function() n = n + 1 end)
  S.run_every("beat", 60, "tick")
  advance(S, 59)
  assert(n == 0)
  advance(S, 60)
  assert(n == 1)
  advance(S, 90)
  S.run_every("beat", 60, "tick") -- re-register: same phase
  assert(S.count() == 1)
  advance(S, 119)
  assert(n == 1)
  advance(S, 120)
  assert(n == 2)
  advance(S, 180)
  assert(n == 3)
  -- opts.first
  S.run_every("later", 60, "tick", nil, { first = 5 })
  advance(S, 185)
  assert(n == 4)
end

-- idempotent one-shot id replaces
do
  clock.t = 0
  local S = make()
  local n = 0
  S.handler("h", function() n = n + 1 end)
  S.run_after(50, "h", nil, { id = "same" })
  S.run_after(80, "h", nil, { id = "same" })
  assert(S.count() == 1)
  advance(S, 60)
  assert(n == 0)
  advance(S, 80)
  assert(n == 1)
end

-- range interval stays in bounds (rand injected, then real math.random)
do
  clock.t = 0
  local S = sched.new({
    now = function() return clock.t end,
    storage = {},
    rand = function(lo, hi) return hi end,
  })
  S.handler("h", function() end)
  S.run_every("r", { 10, 20 }, "h")
  local e = S.list()[1]
  assert(e.at == 20 and e.range[1] == 10 and e.range[2] == 20)
  S.run_after_range(5, 9, "h", nil, { id = "ar" })
  assert(S.turns_left("ar") == 9)
  local S2 = sched.new({ now = function() return clock.t end, storage = {}, rand = math.random })
  S2.handler("h", function() end)
  for i = 1, 20 do
    S2.run_after_range(30, 40, "h", nil, { id = "x" .. i })
    local left = S2.turns_left("x" .. i)
    assert(left >= 30 and left <= 40)
  end
end

-- function interval
do
  clock.t = 0
  local S = make()
  local n = 0
  S.handler("h", function() n = n + 1 end)
  local calls = 0
  S.run_every("f", function()
    calls = calls + 1
    return 25
  end, "h")
  advance(S, 25)
  assert(n == 1 and calls >= 2)
  assert(S.list()[1].at == 50)
end

-- returning false cancels a recurring task
do
  clock.t = 0
  local S = make()
  local n = 0
  S.handler("h", function()
    n = n + 1
    if n == 2 then return false end
  end)
  S.run_every("c", 10, "h")
  advance(S, 10)
  advance(S, 20)
  assert(n == 2 and not S.exists("c"))
  advance(S, 100)
  assert(n == 2)
end

-- erroring handler is isolated, logged and retried; other tasks still run
do
  clock.t = 0
  errors = {}
  local S = make()
  local good = 0
  S.handler("bad", function() error("boom") end)
  S.handler("good", function() good = good + 1 end)
  S.run_after(10, "bad", nil, { id = "b" })
  S.run_after(10, "good", nil, { id = "g" })
  advance(S, 10)
  assert(good == 1)
  assert(#errors == 1 and errors[1]:find("boom"))
  assert(S.exists("b") and not S.exists("g"))
  assert(S.turns_left("b") == 60)
end

-- unknown handler is logged once and the task waits
do
  clock.t = 0
  errors = {}
  local S = make()
  S.run_after(5, "ghost", nil, { id = "gh" })
  advance(S, 5)
  advance(S, 6)
  advance(S, 200)
  assert(#errors == 1)
  assert(S.exists("gh"))
  local fired = 0
  S.handler("ghost", function() fired = fired + 1 end)
  advance(S, 201)
  assert(fired == 1)
end

-- catch_up: skip (default) vs all with cap
do
  clock.t = 0
  local S = make()
  local n = 0
  S.handler("h", function() n = n + 1 end)
  S.run_every("skip", 10, "h")
  advance(S, 1000)
  assert(n == 1)
  assert(S.list()[1].at == 1010)
  S.cancel("skip")

  n = 0
  clock.t = 0
  local S2 = make()
  S2.handler("h", function() n = n + 1 end)
  S2.run_every("all", 10, "h", nil, { catch_up = "all", max_catch_up = 10 })
  advance(S2, 1000)
  assert(n == 11) -- first due firing plus 10 capped catch-ups
  assert(S2.list()[1].at > 1000)
  n = 0
  clock.t = 0
  local S3 = make()
  S3.handler("h", function() n = n + 1 end)
  S3.run_every("all", 10, "h", nil, { catch_up = "all" })
  advance(S3, 35)
  assert(n == 3)
  assert(S3.list()[1].at == 40)
end

-- cancel / cancel_all / exists / time_left / list ordering
do
  clock.t = 0
  local S = make()
  S.handler("a", function() end)
  S.handler("b", function() end)
  S.run_after(30, "a", nil, { id = "z" })
  S.run_after(30, "b", nil, { id = "y" })
  S.run_after(10, "a", nil, { id = "w" })
  local l = S.list()
  assert(l[1].id == "w" and l[2].id == "y" and l[3].id == "z")
  assert(S.time_left("z"):to_turns() == 30)
  assert(S.cancel("w") and not S.cancel("w"))
  assert(S.cancel_all("a") == 1)
  assert(S.count() == 1 and S.exists("y"))
  assert(S.cancel_all() == 1 and S.count() == 0)
  assert(S.time_left("nothing") == nil)
end

-- a handler may reschedule itself under the same id
do
  clock.t = 0
  local S = make()
  local n = 0
  S.handler("h", function()
    n = n + 1
    if n < 3 then S.run_after(10, "h", nil, { id = "self" }) end
  end)
  S.run_after(10, "h", nil, { id = "self" })
  advance(S, 10)
  assert(n == 1 and S.exists("self"))
  advance(S, 20)
  advance(S, 30)
  assert(n == 3 and not S.exists("self"))
end

-- deterministic order: by due time then id
do
  clock.t = 0
  local S = make()
  local order = {}
  S.handler("h", function(ctx) order[#order + 1] = ctx.id end)
  S.run_after(20, "h", nil, { id = "b" })
  S.run_after(10, "h", nil, { id = "c" })
  S.run_after(20, "h", nil, { id = "a" })
  advance(S, 50)
  assert(table.concat(order, ",") == "c,a,b")
end

test_data.ok = true
