#include "catch/catch.hpp"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "relic.h"

#include <set>
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

// CDF: talking_doll (data/json/items/generic/toys_and_sports.json) is a TOOL that still uses BN's "ammo"
// key (fake_firestarter did until P3-7-R4b removed its battery pocket), so this exercises the
// tool_ammo alias through the real loader.
TEST_CASE("tool ammo alias fills ammo_id", "[p3][loader]") {
    REQUIRE(item("talking_doll").type->tool);
    CHECK_FALSE(item("talking_doll").type->tool->ammo_id.empty());
    CHECK(item("talking_doll").type->tool->ammo_id.count(ammotype("battery")) == 1);
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

TEST_CASE("legacy_charges_per_use_factor loads from JSON", "[p3][loader]") {
    // CDF: literals come from data/mods/TEST_DATA/items.json.
    CHECK( itype_id( "test_multimag_tool_factor" ).obj().legacy_charges_per_use_factor == 5 );
    CHECK( itype_id( "9mm" ).obj().legacy_charges_per_use_factor == 1 );
}

TEST_CASE("check_firing_requirements rejects legacy_charges_per_use_factor below 1", "[p3][loader]") {
    // CDF: hand-built itype with a valid firing_requirements and factor 0.
    itype tool;
    tool.tool = cata::make_value<islot_tool>();
    add_firing_requirement( tool.firing_requirements, "DEFAULT", "mag", 1 );
    tool.pockets.push_back( p3_magazine( "mag" ) );
    CHECK( check_firing_requirements( tool ).empty() );
    tool.legacy_charges_per_use_factor = 0;
    CHECK_THAT( check_firing_requirements( tool ),
                Catch::Contains( "legacy_charges_per_use_factor must be >= 1, got 0" ) );
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

// CDF: 1-e integral magazine synthesis. Hand-built itypes, mirroring the 1-d idiom above.
namespace {

itype make_p3_ammo_tool( const std::vector<std::string> &ammo, int max_charges ) {
    itype t;
    t.tool = cata::make_value<islot_tool>();
    for( const std::string &a : ammo ) {
        t.tool->ammo_id.insert( ammotype( a ) );
    }
    t.tool->max_charges = max_charges;
    return t;
}

} // namespace

TEST_CASE("integral magazine synthesis builds a tool pocket", "[p3][loader]") {
    itype tool = make_p3_ammo_tool( { "battery" }, 4 );
    CHECK( needs_integral_magazine( tool ) );
    REQUIRE( tool.pockets.empty() );

    const std::string msg = add_integral_magazine_pocket( tool );
    REQUIRE( tool.pockets.size() == 1 );
    const pocket_data &pocket = tool.pockets.front();
    CHECK( pocket.type == pocket_type::MAGAZINE );
    CHECK( pocket.rigid );
    CHECK( pocket.holster );
    REQUIRE( pocket.ammo_restriction.size() == 1 );
    CHECK( pocket.ammo_restriction.at( ammotype( "battery" ) ) == 4 );
    CHECK_THAT( msg, Catch::Contains( "needs pocket definitions" ) );
    // The max_charges/def_charges zeroing is deferred to P3-7.
    CHECK( tool.tool->max_charges == 4 );
}

TEST_CASE("integral magazine synthesis skips tools that need no pocket", "[p3][loader]") {
    SECTION( "no ammo_id" ) {
        itype tool = make_p3_ammo_tool( {}, 4 );
        CHECK_FALSE( needs_integral_magazine( tool ) );
        CHECK( add_integral_magazine_pocket( tool ).empty() );
        CHECK( tool.pockets.empty() );
    }
    SECTION( "no max_charges" ) {
        itype tool = make_p3_ammo_tool( { "battery" }, 0 );
        CHECK_FALSE( needs_integral_magazine( tool ) );
        CHECK( add_integral_magazine_pocket( tool ).empty() );
        CHECK( tool.pockets.empty() );
    }
    SECTION( "magazines defined" ) {
        itype tool = make_p3_ammo_tool( { "battery" }, 4 );
        tool.magazines[ammotype( "battery" )].insert( itype_id( "p3_mag" ) );
        CHECK_FALSE( needs_integral_magazine( tool ) );
        CHECK( add_integral_magazine_pocket( tool ).empty() );
        CHECK( tool.pockets.empty() );
    }
    SECTION( "not a tool" ) {
        itype plain;
        CHECK_FALSE( needs_integral_magazine( plain ) );
        CHECK( add_integral_magazine_pocket( plain ).empty() );
    }
}

TEST_CASE("integral magazine synthesis warns on explicit pockets", "[p3][loader]") {
    itype tool = make_p3_ammo_tool( { "battery" }, 4 );
    tool.explicit_pockets = true;
    CHECK_FALSE( needs_integral_magazine( tool ) );
    const std::string msg = add_integral_magazine_pocket( tool );
    CHECK( tool.pockets.empty() );
    CHECK_THAT( msg, Catch::Contains( "Redundant max charge value" ) );
}

TEST_CASE("integral magazine synthesis handles two ammotypes", "[p3][loader]") {
    itype tool = make_p3_ammo_tool( { "battery", "charcoal" }, 4 );
    CHECK( needs_integral_magazine( tool ) );
    CHECK_FALSE( add_integral_magazine_pocket( tool ).empty() );
    REQUIRE( tool.pockets.size() == 1 );
    const pocket_data &pocket = tool.pockets.front();
    CHECK( pocket.type == pocket_type::MAGAZINE );
    REQUIRE( pocket.ammo_restriction.size() == 2 );
    CHECK( pocket.ammo_restriction.at( ammotype( "battery" ) ) == 4 );
    CHECK( pocket.ammo_restriction.at( ammotype( "charcoal" ) ) == 4 );
}

// CDF: P3-7-PRE. Fixtures in data/mods/TEST_DATA/derive_counts_p3.json.
TEST_CASE( "derive_capacity_from_magazine_pocket", "[p3][loader]" ) {
    const itype &mag = itype_id( "p3pre_mag" ).obj();
    REQUIRE( mag.magazine );
    // CDF: the fixture has no `capacity` key; its MAGAZINE pocket allows 12 9mm
    CHECK( mag.magazine->capacity == 12 );
}

TEST_CASE( "derive_max_charges_from_tool_pocket", "[p3][loader]" ) {
    const itype &tool = itype_id( "p3pre_tool" ).obj();
    REQUIRE( tool.tool );
    // CDF: the fixture has no `max_charges` key; its MAGAZINE pocket allows 30 battery
    CHECK( tool.tool->max_charges == 30 );
    CHECK( tool.can_have_charges() );
    CHECK( tool.tool->ammo_id.contains( ammotype( "battery" ) ) );
}

TEST_CASE( "derive_keeps_explicit_legacy_values", "[p3][loader]" ) {
    // CDF: legacy capacity 7 against a pocket of 12, legacy max_charges 9 against 30
    CHECK( itype_id( "p3pre_mag_legacy" ).obj().magazine->capacity == 7 );
    CHECK( itype_id( "p3pre_tool_legacy" ).obj().tool->max_charges == 9 );
}

TEST_CASE( "derive_ammo_types_and_default_magazine_from_well", "[p3][loader]" ) {
    const itype &tool = itype_id( "p3pre_well_tool" ).obj();
    REQUIRE( tool.tool );
    CHECK( tool.tool->ammo_id.contains( ammotype( "9mm" ) ) );
    CHECK( tool.magazine_default.at( ammotype( "9mm" ) ) == itype_id( "p3pre_mag" ) );
    item it( "p3pre_well_tool" );
    REQUIRE( !it.ammo_types().empty() );
    // the spawned tool gets its default cartridge in the default magazine, from the well
    item loaded( "p3pre_well_tool", calendar::turn, 5 );
    const item *mag = loaded.magazine_current();
    REQUIRE( mag != nullptr );
    CHECK( mag->typeId() == itype_id( "p3pre_mag" ) );
    CHECK( loaded.ammo_remaining() == 5 );
}

// CDF: P3-5-4 U7; DDA itype.h:1074 and item_factory.cpp:3884 (is_visible_when_installed). DDA sets the
// key on mipim (gunmod/rail.json:79) and mounted_flashlight (gunmod/underbarrel.json:605); a grep of
// CDF data/ finds neither id, so the only item with the key is the TEST_DATA fixture
// test_visible_underslung_shotgun (visible_gunmod_p3.json, copy-from u_shotgun).
TEST_CASE( "is_visible_when_installed_loads", "[p3][loader][5-4]" )
{
    const std::set<std::string> visible_ids = {
        "mipim", "mounted_flashlight", "test_visible_underslung_shotgun"
    };
    int gunmods = 0;
    int visible_found = 0;
    for( const itype *t : item_controller->all() ) {
        if( !t->gunmod ) {
            continue;
        }
        ++gunmods;
        const bool expected = visible_ids.count( t->get_id().str() ) > 0;
        INFO( t->get_id().str() );
        CHECK( t->gunmod->is_visible_when_installed == expected );
        if( t->gunmod->is_visible_when_installed ) {
            ++visible_found;
        }
    }
    // CDF: u_shotgun (gunmod/underbarrel.json:611) has no key; its copy-from child sets it
    REQUIRE( gunmods > 1 );
    CHECK( visible_found == 1 );
    CHECK_FALSE( itype_id( "u_shotgun" ).obj().gunmod->is_visible_when_installed );
    CHECK( itype_id( "test_visible_underslung_shotgun" ).obj().gunmod->is_visible_when_installed );
}
