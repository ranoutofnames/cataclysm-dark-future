local time = require("lib.cdf_time")

assert(time.to_turns(90) == 90)
assert(time.to_turns(TimeDuration.from_minutes(2)) == 120)
assert(time.to_turns(TimePoint.from_turn(500)) == 500)
assert(time.to_duration(30):to_turns() == 30)
assert(time.turns(15):to_turns() == 15)
local d = TimeDuration.from_turns(7)
assert(time.to_duration(d) == d)
assert(time.now_turn() == time.now():to_turn())
assert(time.since(time.now_turn() - 10):to_turns() == 10)
assert(not pcall(time.to_turns, "nope"))

test_data.ok = true
