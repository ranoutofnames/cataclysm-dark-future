-- Obsolete item ids spawned from Lua are migrated: gapi.create_item goes through spawn_migrated
local arrow = gapi.create_item(ItypeId.new("arrow_field_point"), -1)
test_data["migrated_type"] = arrow:get_type():str()
