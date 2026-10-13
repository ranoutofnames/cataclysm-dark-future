---@type Map
local m = test_data.map
local id = test_data.transform_id

test_data.at_ok = m:transform_at(id, test_data.single)
test_data.at_unknown = m:transform_at("no_such_transform", test_data.single)

test_data.radius_count = m:transform_radius(id, 2, test_data.center)
test_data.radius_unknown = m:transform_radius("no_such_transform", 2, test_data.center)

test_data.line_count = m:transform_line(id, test_data.line_a, test_data.line_b)
test_data.line_unknown = m:transform_line("no_such_transform", test_data.line_a, test_data.line_b)
