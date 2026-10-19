#include "catch/catch.hpp"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "relic.h"

#include <sstream>
#include <string>
#include <vector>

namespace {

template <typename T>
auto load_struct(const std::string& json) -> T {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    T out;
    out.deserialize(jo);
    return out;
}

auto load_firing_requirements(const std::string& json) -> firing_requirement_set {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    firing_requirement_set out;
    out.deserialize_firing_requirements(jo, "fr");
    return out;
}

auto load_consumption_per_use(const std::string& json) -> firing_requirement_set {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    firing_requirement_set out;
    out.deserialize_consumption_per_use(jo, "cpu");
    return out;
}

} // namespace

TEST_CASE("pocket_consumption_entry loads DDA-format JSON", "[p3][loader]") {
    const pocket_consumption_entry entry =
        load_struct<pocket_consumption_entry>(R"({"pocket":"mag","qty":2})");
    CHECK(entry.pocket == "mag");
    CHECK(entry.qty == 2);

    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"pocket":"mag","qty":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"pocket":"mag","qty":-1})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"qty":2})"), JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"pocket":"mag"})"), JsonError);
}

TEST_CASE("pocket_consumption_mod loads DDA-format JSON", "[p3][loader]") {
    const pocket_consumption_mod mod =
        load_struct<pocket_consumption_mod>(R"({"pocket":"mag","set":3,"multiply":0.5})");
    CHECK(mod.pocket == "mag");
    REQUIRE(mod.set.has_value());
    CHECK(*mod.set == 3);
    CHECK(mod.multiply == Approx(0.5f));

    const pocket_consumption_mod plain =
        load_struct<pocket_consumption_mod>(R"({"pocket":"mag"})");
    CHECK_FALSE(plain.set.has_value());
    CHECK(plain.multiply == Approx(1.0f));

    CHECK_THROWS_AS(load_struct<pocket_consumption_mod>(R"({"pocket":"mag","set":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_mod>(R"({"pocket":"mag","multiply":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_mod>(R"({"pocket":"mag","multiply":-1})"),
                    JsonError);
}

TEST_CASE("pocket_capacity_mod loads DDA-format JSON", "[p3][loader]") {
    const pocket_capacity_mod mod =
        load_struct<pocket_capacity_mod>(R"({"pocket":"mag","multiply":2})");
    CHECK(mod.pocket == "mag");
    CHECK(mod.multiply == Approx(2.0f));

    const pocket_capacity_mod plain =
        load_struct<pocket_capacity_mod>(R"({"pocket":"mag"})");
    CHECK(plain.multiply == Approx(1.0f));

    CHECK_THROWS_AS(load_struct<pocket_capacity_mod>(R"({"pocket":"mag","multiply":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_capacity_mod>(R"({"pocket":"mag","multiply":-1})"),
                    JsonError);
}

TEST_CASE("firing_requirement_set deserialize_firing_requirements", "[p3][loader]") {
    const firing_requirement_set fr = load_firing_requirements(
        R"({"fr":{"DEFAULT":[{"pocket":"a","qty":1}],"AUTO":[{"pocket":"a","qty":3},{"pocket":"b","qty":1}]}})");
    CHECK(fr.per_mode.size() == 2);
    REQUIRE(fr.for_mode(gun_mode_id("AUTO")) != nullptr);
    CHECK(fr.for_mode(gun_mode_id("AUTO"))->size() == 2);
    CHECK(fr.for_mode(gun_mode_id("BURST")) == nullptr);
    CHECK_FALSE(fr.empty());

    const firing_requirement_set commented = load_firing_requirements(
        R"({"fr":{"DEFAULT":[{"pocket":"a","qty":1}],"//":"x"}})");
    CHECK(commented.per_mode.size() == 1);

    CHECK_THROWS_AS(load_firing_requirements(R"({"fr":{}})"), JsonError);
    CHECK_THROWS_AS(load_firing_requirements(R"({"fr":{"//":"x"}})"), JsonError);
    CHECK_THROWS_AS(load_firing_requirements(R"({"fr":{"DEFAULT":[]}})"), JsonError);
    CHECK_THROWS_AS(load_firing_requirements(
                        R"({"fr":{"DEFAULT":{"pocket":"a","qty":1}}})"),
                    JsonError);
}

TEST_CASE("firing_requirement_set deserialize_consumption_per_use", "[p3][loader]") {
    const firing_requirement_set fr =
        load_consumption_per_use(R"({"cpu":[{"pocket":"a","qty":2}]})");
    REQUIRE(fr.for_mode(gun_mode_id("DEFAULT")) != nullptr);
    CHECK(fr.for_mode(gun_mode_id("DEFAULT"))->front().qty == 2);
}

// CDF: cdf_loader_p3_test.cpp has no Item_factory harness, so per the 1-b brief this checks the
// itype-level member indirectly: build an itype through the 1-a helpers with the same member
// names load_basic_info passes, and assert nothing is lost.
TEST_CASE("itype firing_requirements member", "[p3][loader]") {
    const itype bare;
    CHECK(bare.firing_requirements.empty());

    itype gun;
    {
        std::istringstream buffer(
            R"({"firing_requirements":{"DEFAULT":[{"pocket":"a","qty":1}]}})");
        JsonIn jsin(buffer);
        JsonObject jo = jsin.get_object();
        gun.firing_requirements.deserialize_firing_requirements(jo, "firing_requirements");
    }
    REQUIRE(gun.firing_requirements.for_mode(gun_mode_id("DEFAULT")) != nullptr);
    CHECK(gun.firing_requirements.for_mode(gun_mode_id("DEFAULT"))->size() == 1);
    CHECK_FALSE(gun.firing_requirements.empty());

    itype tool;
    {
        std::istringstream buffer(
            R"({"consumption_per_use":[{"pocket":"a","qty":2}]})");
        JsonIn jsin(buffer);
        JsonObject jo = jsin.get_object();
        tool.firing_requirements.deserialize_consumption_per_use(jo, "consumption_per_use");
    }
    REQUIRE(tool.firing_requirements.for_mode(gun_mode_id("DEFAULT")) != nullptr);
    CHECK(tool.firing_requirements.for_mode(gun_mode_id("DEFAULT"))->front().qty == 2);

    CHECK_THROWS_AS(load_consumption_per_use(R"({"cpu":[{"pocket":"a","qty":0}]})"),
                    JsonError);
}

TEST_CASE("magazine mag_jam_mult default", "[p3][loader]") {
    CHECK(item("stanag30").type->magazine->mag_jam_mult == 1.0);
}

// CDF: fake_firestarter (data/json/items/fake.json) is a TOOL that still uses BN's "ammo" key,
// so this exercises the tool_ammo alias through the real loader. No CDF JSON uses "tool_ammo"
// and "pocket_mods" has no loaded file (pocket_mods_test.json.p3 arrives with job 2-d), so
// neither key has loaded-data coverage until then.
TEST_CASE("tool ammo alias fills ammo_id", "[p3][loader]") {
    REQUIRE(item("fake_firestarter").type->tool);
    CHECK_FALSE(item("fake_firestarter").type->tool->ammo_id.empty());
    CHECK(item("fake_firestarter").type->tool->ammo_id.count(ammotype("battery")) == 1);
}

TEST_CASE("islot_mod add_pockets defaults empty", "[p3][loader]") {
    REQUIRE(item("arrowrest").type->mod);
    CHECK(item("arrowrest").type->mod->add_pockets.empty());
}

namespace {

itype make_p3_gun( const std::vector<std::string> &modes ) {
    itype t;
    t.gun = cata::make_value<islot_gun>();
    for( const std::string &m : modes ) {
        t.gun->modes.emplace( gun_mode_id( m ), gun_modifier_data( m, 1, {} ) );
    }
    return t;
}

void add_firing_requirement( firing_requirement_set &fr, const std::string &mode,
                             const std::string &pocket, int qty ) {
    pocket_consumption_entry entry;
    entry.pocket = pocket;
    entry.qty = qty;
    fr.per_mode[gun_mode_id( mode )].push_back( entry );
}

pocket_data p3_magazine( const std::string &id ) {
    pocket_data p( pocket_type::MAGAZINE );
    p.pocket_id = id;
    p.ammo_restriction[ammotype( "223" )] = 30;
    return p;
}

} // namespace

TEST_CASE("check_firing_requirements ignores empty requirements", "[p3][loader]") {
    // An odd gun/tool is still ignored while firing_requirements is empty.
    itype odd;
    odd.gun = cata::make_value<islot_gun>();
    odd.gun->ups_charges = 3;
    odd.gun->ammo_to_fire = 2;
    odd.tool = cata::make_value<islot_tool>();
    odd.tool->charges_per_use = 1;
    odd.tool->power_draw = 5;
    CHECK( check_firing_requirements( odd ).empty() );
}

TEST_CASE("check_firing_requirements mutual exclusions", "[p3][loader]") {
    SECTION("gun ups_charges") {
        itype gun = make_p3_gun( { "DEFAULT" } );
        add_firing_requirement( gun.firing_requirements, "DEFAULT", "mag", 1 );
        gun.pockets.push_back( p3_magazine( "mag" ) );
        gun.gun->ups_charges = 1;
        CHECK_THAT( check_firing_requirements( gun ),
                    Catch::Contains( "firing_requirements and non-zero ups_charges are mutually exclusive" ) );
    }
    SECTION("gun ammo_to_fire") {
        itype gun = make_p3_gun( { "DEFAULT" } );
        add_firing_requirement( gun.firing_requirements, "DEFAULT", "mag", 1 );
        gun.pockets.push_back( p3_magazine( "mag" ) );
        gun.gun->ammo_to_fire = 2;
        CHECK_THAT( check_firing_requirements( gun ),
                    Catch::Contains( "firing_requirements and non-default ammo_to_fire are mutually exclusive" ) );
    }
    SECTION("tool charges_per_use") {
        itype tool;
        tool.tool = cata::make_value<islot_tool>();
        add_firing_requirement( tool.firing_requirements, "DEFAULT", "mag", 1 );
        tool.pockets.push_back( p3_magazine( "mag" ) );
        tool.tool->charges_per_use = 1;
        CHECK_THAT( check_firing_requirements( tool ),
                    Catch::Contains( "consumption_per_use and charges_per_use are mutually exclusive" ) );
    }
    SECTION("tool power_draw") {
        itype tool;
        tool.tool = cata::make_value<islot_tool>();
        add_firing_requirement( tool.firing_requirements, "DEFAULT", "mag", 1 );
        tool.pockets.push_back( p3_magazine( "mag" ) );
        tool.tool->power_draw = 1;
        CHECK_THAT( check_firing_requirements( tool ),
                    Catch::Contains( "consumption_per_use and power_draw are mutually exclusive" ) );
    }
}

TEST_CASE("check_firing_requirements mode checks", "[p3][loader]") {
    SECTION("firing_requirements names an unknown mode") {
        itype gun = make_p3_gun( { "DEFAULT" } );
        add_firing_requirement( gun.firing_requirements, "AUTO", "mag", 1 );
        gun.pockets.push_back( p3_magazine( "mag" ) );
        CHECK_THAT( check_firing_requirements( gun ),
                    Catch::Contains( "firing_requirements mode \"AUTO\" is not in modes" ) );
    }
    SECTION("base mode has no entry") {
        itype gun = make_p3_gun( { "DEFAULT", "AUTO" } );
        add_firing_requirement( gun.firing_requirements, "DEFAULT", "mag", 1 );
        gun.pockets.push_back( p3_magazine( "mag" ) );
        CHECK_THAT( check_firing_requirements( gun ),
                    Catch::Contains( "firing_requirements is missing entry for base mode \"AUTO\"" ) );
    }
}

TEST_CASE("check_firing_requirements pocket references", "[p3][loader]") {
    SECTION("unknown pocket id") {
        itype gun = make_p3_gun( { "DEFAULT" } );
        add_firing_requirement( gun.firing_requirements, "DEFAULT", "ghost", 1 );
        CHECK_THAT( check_firing_requirements( gun ),
                    Catch::Contains( "firing_requirements references unknown pocket id \"ghost\"" ) );
    }
    SECTION("container pocket cannot be referenced") {
        itype gun = make_p3_gun( { "DEFAULT" } );
        add_firing_requirement( gun.firing_requirements, "DEFAULT", "bag", 1 );
        pocket_data bag( pocket_type::CONTAINER );
        bag.pocket_id = "bag";
        gun.pockets.push_back( bag );
        CHECK_THAT( check_firing_requirements( gun ),
                    Catch::Contains( "neither a MAGAZINE_WELL nor a MAGAZINE" ) );
    }
    SECTION("magazine well without default_magazine") {
        itype gun = make_p3_gun( { "DEFAULT" } );
        add_firing_requirement( gun.firing_requirements, "DEFAULT", "well", 1 );
        pocket_data well( pocket_type::MAGAZINE_WELL );
        well.pocket_id = "well";
        gun.pockets.push_back( well );
        CHECK_THAT( check_firing_requirements( gun ),
                    Catch::Contains( "no default_magazine" ) );
    }
}

TEST_CASE("check_firing_requirements consistent gun", "[p3][loader]") {
    itype gun = make_p3_gun( { "DEFAULT", "AUTO" } );
    add_firing_requirement( gun.firing_requirements, "DEFAULT", "mag", 1 );
    add_firing_requirement( gun.firing_requirements, "AUTO", "mag", 3 );
    gun.pockets.push_back( p3_magazine( "mag" ) );
    CHECK( check_firing_requirements( gun ).empty() );
}

TEST_CASE("check_firing_requirements gunmod modes", "[p3][loader]") {
    itype mod;
    mod.gunmod = cata::make_value<islot_gunmod>();
    mod.gunmod->mode_modifier.emplace( gun_mode_id( "BURST" ),
                                       gun_modifier_data( "BURST", 3, {} ) );
    SECTION("mode not in mode_modifier") {
        add_firing_requirement( mod.gunmod->firing_requirements, "AUTO", "mag", 1 );
        CHECK_THAT( check_firing_requirements( mod ),
                    Catch::Contains( "gunmod mode_firing_requirements mode \"AUTO\" is not in this mod's mode_modifier" ) );
    }
    SECTION("consistent gunmod") {
        add_firing_requirement( mod.gunmod->firing_requirements, "BURST", "mag", 3 );
        CHECK( check_firing_requirements( mod ).empty() );
    }
}
