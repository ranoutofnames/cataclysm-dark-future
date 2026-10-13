test_data.has_known = gapi.has_update_mapgen("lua_test_update")
test_data.has_unknown = gapi.has_update_mapgen("no_such_update_mapgen")
test_data.unknown_ok = gapi.run_mapgen_update("no_such_update_mapgen", test_data.omt)
test_data.run_ok = gapi.run_mapgen_update("lua_test_update", test_data.omt, { cancel_on_collision = false })
