#include "catalua_impl.h"
#include "catalua_sol.h"
#include "catch/catch.hpp"

#include <string>

// Lua spawn paths go through Item_factory::spawn_migrated (P1e), so an obsolete id from
// data/json/obsoletion/migration.json spawns its replacement.
TEST_CASE("lua_spawn_migrates_obsolete_id", "[lua][item][migration]") {
    auto lua = make_lua_state();
    auto test_data = lua.create_table();
    lua.globals()["test_data"] = test_data;

    run_lua_script(lua, "tests/lua/item_migration_test.lua");

    CHECK(test_data.get<std::string>("migrated_type") == "arrow_field_point_fletched");
}
