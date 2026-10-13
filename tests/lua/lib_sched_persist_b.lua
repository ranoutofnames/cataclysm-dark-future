-- Phase B: fresh Lua state, storage restored from the serialised copy of phase A.
local sched = require("lib.cdf_sched")

local clock = 1100 -- 100 turns later: heartbeat (due 1060) missed, alarm (due 1300) pending
local S = sched.new({
  now = function() return clock end,
  storage = test_data.storage,
  on_error = function(m) error(m) end,
  rand = function(lo, hi) return lo end,
})
assert(S.count() == 3, "tasks lost in save/load")
assert(S.exists("alarm") and S.exists("heartbeat") and S.exists("ranged"))

local fired = { once = 0, beat = 0 }
local seen
S.handler("once", function(ctx)
  fired.once = fired.once + 1
  seen = ctx.args
end)
S.handler("beat", function(ctx) fired.beat = fired.beat + 1 end)

S.pump()
assert(fired.beat == 1, "heartbeat should run once for the missed cycle, got " .. fired.beat)
assert(fired.once == 0)
-- phase preserved: next due = missed due (1060) + interval, not now + interval
local hb
for _, e in ipairs(S.list()) do
  if e.id == "heartbeat" then hb = e end
end
assert(hb.every == 60 and hb.at == 1120, "heartbeat at " .. tostring(hb.at))
local rg
for _, e in ipairs(S.list()) do
  if e.id == "ranged" then rg = e end
end
assert(rg.range[1] == 100 and rg.range[2] == 200)

clock = 1300
S.pump()
assert(fired.once == 1 and seen.who == "me" and seen.n == 5 and seen.flag == true and seen.f == 1.5)
assert(not S.exists("alarm"))
clock = 5000
S.pump()
assert(fired.once == 1, "alarm must fire exactly once")
test_data.ok = true
