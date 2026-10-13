local mod = game.mod_runtime[game.current_mod]

game.add_hook("on_game_started", function(...)
  if mod.on_game_started then return mod.on_game_started(...) end
end)

gapi.register_action_menu_entry({
  id = "lua_helper_demo_status",
  name = "Helper demo: status",
  category = "misc",
  fn = function()
    if mod.show_status then mod.show_status() end
  end,
})

gapi.register_action_menu_entry({
  id = "lua_helper_demo_transform",
  name = "Helper demo: transform r=3 around me",
  category = "misc",
  fn = function()
    if mod.transform_here then mod.transform_here() end
  end,
})

gapi.register_action_menu_entry({
  id = "lua_helper_demo_update_mapgen",
  name = "Helper demo: run update mapgen here",
  category = "misc",
  fn = function()
    if mod.update_mapgen_here then mod.update_mapgen_here() end
  end,
})
