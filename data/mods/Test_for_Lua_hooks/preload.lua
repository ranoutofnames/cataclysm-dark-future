gdebug.log_info("LUA HOOKS TEST: PRELOAD ONLINE")

--@class LuaHooksTest
local mod = game.mod_runtime[game.current_mod]

game.add_hook("on_mon_death", function(...) return mod.chicken_death(...) end)
game.add_hook("on_character_gains_mutation", function(...) return mod.on_gain_mutation(...) end)
game.add_hook("on_character_loses_mutation", function(...) return mod.on_lose_mutation(...) end)
game.add_hook("on_avatar_enters_omt", function(...) return mod.on_enter_omt(...) end)
