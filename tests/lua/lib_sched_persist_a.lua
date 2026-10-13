-- Phase A: build scheduler state in test_data.storage (a plain table that C++ serialises).
local sched = require("lib.cdf_sched")

local S = sched.new({ now = function() return 1000 end, storage = test_data.storage })
S.handler("once", function() end)
S.handler("beat", function() end)
S.run_after(300, "once", { who = "me", n = 5, flag = true, f = 1.5 }, { id = "alarm" })
S.run_every("heartbeat", 60, "beat", { tag = "hb" })
S.run_every("ranged", { 100, 200 }, "beat", nil, { first = 150, catch_up = "all", max_catch_up = 3 })
assert(S.count() == 3)
test_data.ok = true
