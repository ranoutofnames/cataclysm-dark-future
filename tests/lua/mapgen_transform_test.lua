---@type MapgenConstructor
local target = test_data.target
test_data.ok = target:transform_at("mapgen_test", test_data.pos)
test_data.unknown = target:transform_at("no_such_transform", test_data.pos)
