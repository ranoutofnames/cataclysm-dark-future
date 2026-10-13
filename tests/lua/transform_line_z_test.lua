---@type Map
local m = test_data.map
test_data.count = m:transform_line("lua_test_wall_to_floor", test_data.a, test_data.b)
