#include "catalua_impl.h"
#include "catalua_sol.h"
#include "catch/catch.hpp"

#include <string>

// Lua side of the plate and sub-location layer (P1e step 31). Kept in its own file so it does
// not touch tests/catalua_test.cpp. Uses the TEST_DATA carriers and plates of
// tests/ablative_armor_test.cpp and the sub-location vests of tests/subbodypart_test.cpp.
TEST_CASE("lua_ablative_and_sub_part_api", "[lua][armor][ablative][subpart]") {
    auto lua = make_lua_state();
    auto test_data = lua.create_table();
    lua.globals()["test_data"] = test_data;

    run_lua_script(lua, "tests/lua/ablative_test.lua");

    CHECK(test_data.get<int>("slots") == 5);
    CHECK(test_data.get<int>("plates_empty") == 0);
    CHECK(test_data.get<int>("type_slots") == 5);
    CHECK(test_data.get<bool>("large_ok"));
    CHECK(test_data.get<bool>("medium_ok"));
    CHECK(test_data.get<int>("plates") == 2);
    CHECK(test_data.get<std::string>("first_plate") == "test_plate_large");
    CHECK(test_data.get<std::string>("second_plate") == "test_plate_medium");
    CHECK(test_data.get<int>("rig_plates") == 0);
    CHECK(test_data.get<int>("rig_slots") == 0);

    // CDF: sub_parts() lists the secondary locations too (torso: 2 primary + 4 secondary)
    CHECK(test_data.get<int>("torso_parts") == 6);
    CHECK(test_data.get<std::string>("torso_part_1") == "torso_upper");
    CHECK(test_data.get<std::string>("torso_part_2") == "torso_lower");
    CHECK(test_data.get<int>("upper_max_coverage") == 60);
    CHECK(test_data.get<std::string>("upper_parent") == "torso");
    CHECK_FALSE(test_data.get<std::string>("upper_name").empty());
    // CDF: HAND1 gave the hand four sub-locations (hand_parts is 4, was 0)
    CHECK(test_data.get<int>("hand_parts") == 4);
    CHECK(test_data.get<int>("arm_parts") == 4);
    CHECK(test_data.get<bool>("sub_id_valid"));
    CHECK(test_data.get<std::string>("sub_id_str") == "torso_upper");

    CHECK(test_data.get<int>("vest_torso") == 92);
    CHECK(test_data.get<int>("vest_upper") == 100);
    CHECK(test_data.get<int>("vest_lower") == 80);
    CHECK(test_data.get<int>("vest_int_torso") == 92);
    CHECK(test_data.get<int>("vest_int_upper") == 100);

    CHECK(test_data.get<int>("plain_torso") == 50);
    CHECK(test_data.get<int>("plain_upper") == 50);
    CHECK(test_data.get<int>("plain_lower") == 50);
}
