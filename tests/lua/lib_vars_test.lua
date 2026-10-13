local vars = require("lib.cdf_vars")

-- dialogue keys must match src/condition.cpp get_talk_varname
assert(vars.dialogue_key("x") == "npctalk_var_x")
assert(vars.dialogue_key("x", "dialogue") == "npctalk_var_dialogue_x")
assert(vars.dialogue_key("x", "dialogue", "ctx") == "npctalk_var_dialogue_ctx_x")
assert(vars.dialogue_key("x", nil, "ctx") == "npctalk_var_ctx_x")

-- expects
assert(pcall(vars.expects, { a = 1, b = 2 }, "a", "b"))
assert(not pcall(vars.expects, { a = 1 }, "a", "b"))
assert(not pcall(vars.expects, nil, "a"))

-- table-backed variables (what vars.global wraps)
local store = {}
local V = vars.from_table(store)
assert(V.get("n", 5) == 5)
assert(not V.has("n"))
V.set("n", 3)
assert(V.get("n") == 3 and V.has("n"))
assert(V.add("n", 4) == 7)
assert(V.inc("n") == 8)
assert(store.n == 8)
V.set("flag", true)
assert(V.get_bool("flag") == true)
V.set("s", "hi")
assert(V.get_str("s") == "hi")
V.clear("n")
assert(not V.has("n") and V.get_num("n", 0) == 0)

-- table-backed variables (vars.global is the same wrapper over game.mod_storage[mod]._vars)
local store = { _vars = {} }
local G = vars.from_table(store._vars)
G.inc("count")
G.inc("count")
assert(G.get("count") == 2)
assert(store._vars.count == 2)

-- creature-backed variables
local av = gapi.get_avatar()
local C = vars.of(av)
C.clear("l1_test")
assert(not C.has("l1_test"))
assert(C.get_num("l1_test", 9) == 9)
C.set("l1_test", 4)
assert(C.has("l1_test"))
assert(av:get_value("l1_test") == "4")
assert(C.get_num("l1_test") == 4)
assert(C.add("l1_test", 2) == 6)
assert(C.inc("l1_test") == 7)
C.set("l1_flag", true)
assert(C.get_bool("l1_flag") == true)
-- the same store JSON dialogue vars use
av:set_value(vars.dialogue_key("l1_dlg", "dialogue", "ctx"), "yes")
assert(C.get_str("npctalk_var_dialogue_ctx_l1_dlg") == "yes")
C.clear("l1_test")
C.clear("l1_flag")
av:remove_value("npctalk_var_dialogue_ctx_l1_dlg")
assert(not C.has("l1_test"))

test_data.ok = true
