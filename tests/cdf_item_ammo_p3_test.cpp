#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "examine_item_menu.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "units.h"

#include <sstream>
#include <string>
#include <vector>

namespace {

// Index of the MAGAZINE_WELL pocket in the order well_pocket_at uses: every
// pocket returned by item::get_pockets( all ), wells included.
auto pocket_index( const item &it, bool want_well ) -> int {
    int idx = 0;
    for( const item_pocket *p : it.get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) == want_well ) {
            return idx;
        }
        ++idx;
    }
    return -1;
}

auto loaded_m4a1( int rounds ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, rounds ) );
    gun->put_in( std::move( mag ) );
    return gun;
}

// tests/cdf_item_pocket_test.cpp:24-30
auto load_pocket( const std::string &json ) -> pocket_data {
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    JsonObject jo = jsin.get_object();
    pocket_data data;
    data.load( jo );
    return data;
}

// A MAGAZINE pocket holding six .40 rounds that takes the 6-round .40 speedloader.
auto speedloader_pocket_data() -> pocket_data {
    return load_pocket( R"({
        "pocket_type": "MAGAZINE",
        "ammo_restriction": { "40": 6 },
        "allowed_speedloaders": [ "40_speedloader6" ]
    })" );
}

auto loaded_speedloader( int rounds ) -> detached_ptr<item> {
    detached_ptr<item> speedloader = item::spawn( itype_id( "40_speedloader6" ) );
    if( rounds > 0 ) {
        speedloader->put_in( item::spawn( itype_id( "40sw" ), calendar::turn, rounds ) );
    }
    return speedloader;
}

// CDF: copied from DDA multimag_test.cpp:1279-1290; detached_ptr instead of a value item
auto make_multimag_consume_tool( int well_a_charges = 12, int well_b_charges = 30 ) -> detached_ptr<item> {
    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_tool_consume" ) );
    detached_ptr<item> mag_a = item::spawn( itype_id( "glockmag" ) );
    mag_a->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, well_a_charges ),
                   pocket_type::MAGAZINE );
    detached_ptr<item> mag_b = item::spawn( itype_id( "stanag30" ) );
    mag_b->put_in( item::spawn( itype_id( "556" ), calendar::turn, well_b_charges ),
                   pocket_type::MAGAZINE );
    REQUIRE( tool->put_in( std::move( mag_a ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool->put_in( std::move( mag_b ), pocket_type::MAGAZINE_WELL ).success() );
    return tool;
}

} // namespace

TEST_CASE( "per-well ammo accessors on a gun", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );

    CHECK( gun->ammo_remaining( well_idx ) == 30 );
    CHECK( gun->ammo_capacity( well_idx ) == 30 );
    CHECK( gun->remaining_ammo_capacity( well_idx ) == 0 );

    CHECK( gun->ammo_remaining( -1 ) == 0 );
    CHECK( gun->ammo_remaining( 999 ) == 0 );
    CHECK( gun->ammo_capacity( -1 ) == 0 );
    CHECK( gun->ammo_capacity( 999 ) == 0 );
    CHECK( gun->remaining_ammo_capacity( -1 ) == 0 );
    CHECK( gun->remaining_ammo_capacity( 999 ) == 0 );

    const int other_idx = pocket_index( *gun, false );
    REQUIRE( other_idx >= 0 );
    CHECK( gun->ammo_remaining( other_idx ) == 0 );
    CHECK( gun->ammo_capacity( other_idx ) == 0 );
    CHECK( gun->remaining_ammo_capacity( other_idx ) == 0 );
}

TEST_CASE( "per-well remaining_ammo_capacity tracks the loaded count", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 20 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );

    CHECK( gun->ammo_remaining( well_idx ) == 20 );
    CHECK( gun->ammo_capacity( well_idx ) == 30 );
    CHECK( gun->remaining_ammo_capacity( well_idx ) == 10 );
}

TEST_CASE( "all_magazine_well_pockets lists only wells", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );

    const std::vector<item_pocket *> wells = gun->all_magazine_well_pockets();
    REQUIRE( wells.size() == 1 );
    CHECK( wells.front()->is_type( pocket_type::MAGAZINE_WELL ) );

    const item &const_gun = *gun;
    const std::vector<const item_pocket *> const_wells = const_gun.all_magazine_well_pockets();
    REQUIRE( const_wells.size() == 1 );
    CHECK( const_wells.front()->is_type( pocket_type::MAGAZINE_WELL ) );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK( backpack->all_magazine_well_pockets().empty() );
}

TEST_CASE( "allows_speedloader needs a speedloader pocket", "[p3][ammo]" ) {
    // CDF: no CDF data item sets allowed_speedloaders, so only the false path
    // is covered here.
    detached_ptr<item> gun = loaded_m4a1( 30 );
    CHECK_FALSE( gun->allows_speedloader( itype_id( "40_speedloader6" ) ) );
    CHECK_FALSE( gun->allows_speedloader( itype_id( "stanag30" ) ) );
}

TEST_CASE( "item can_reload_with an ammo item", "[p3][ammo]" ) {
    // DDA tests/reload_magazine_test.cpp:52-62, with i_add swapped for item::spawn
    // and the pocket API reached directly.
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    detached_ptr<item> ammo = item::spawn( itype_id( "556" ) );
    detached_ptr<item> alt_ammo = item::spawn( itype_id( "223" ) );
    detached_ptr<item> bad_ammo = item::spawn( itype_id( "9mm" ) );
    detached_ptr<item> bad_mag = item::spawn( itype_id( "glockmag" ) );

    CHECK( mag->can_reload_with( *ammo, true ) );
    CHECK( mag->can_reload_with( *alt_ammo, true ) );
    CHECK_FALSE( mag->can_reload_with( *bad_ammo, true ) );
    CHECK_FALSE( mag->can_reload_with( *bad_mag, true ) );

    // The same predicate through item_contents agrees.
    CHECK( mag->contents.can_reload_with( *ammo, true ) );
    CHECK( mag->contents.can_reload_with( *alt_ammo, true ) );
    CHECK_FALSE( mag->contents.can_reload_with( *bad_ammo, true ) );
    CHECK_FALSE( mag->contents.can_reload_with( *bad_mag, true ) );
}

TEST_CASE( "uses_firing_requirements is false without itype data", "[p3][ammo]" ) {
    // CDF: no data item sets firing_requirements yet (data port 7-R2), so a gun
    // and a magazine both report false.
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    CHECK_FALSE( gun->uses_firing_requirements() );
    CHECK_FALSE( mag->uses_firing_requirements() );
}

TEST_CASE( "ammo_identity_mag returns the loaded magazine", "[p3][ammo]" ) {
    // No firing requirements: ammo_identity_mag falls through to magazine_current.
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ) );
    item *mag_ptr = &*mag;
    gun->put_in( std::move( mag ) );

    const item *identity = gun->ammo_identity_mag();
    REQUIRE( identity != nullptr );
    CHECK( identity == mag_ptr );
    CHECK( identity->typeId() == itype_id( "stanag30" ) );
    CHECK( identity->ammo_remaining() == 30 );

    detached_ptr<item> unloaded = item::spawn( itype_id( "m4a1" ) );
    CHECK( unloaded->ammo_identity_mag() == nullptr );
}

TEST_CASE( "primary_ammo_pocket finds the magazine well", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item_pocket *p = gun->primary_ammo_pocket();
    REQUIRE( p != nullptr );
    CHECK( p->is_type( pocket_type::MAGAZINE_WELL ) );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK( backpack->primary_ammo_pocket() == nullptr );
}

TEST_CASE( "speedloader accepted by a pocket that allows it", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    detached_ptr<item> speedloader = loaded_speedloader( 6 );

    CHECK( pocket.can_reload_with( *speedloader, true ) );
}

TEST_CASE( "speedloader not in the allowed list is rejected", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    detached_ptr<item> other = item::spawn( itype_id( "38_speedloader" ) );
    other->put_in( item::spawn( itype_id( "38_special" ), calendar::turn, 7 ) );

    CHECK_FALSE( pocket.can_reload_with( *other, true ) );
}

TEST_CASE( "speedloader rejected by a full pocket", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    pocket.insert( item::spawn( itype_id( "40sw" ), calendar::turn, 6 ) );
    REQUIRE( pocket.remaining_ammo_capacity( ammotype( "40" ) ) < 6 );
    detached_ptr<item> speedloader = loaded_speedloader( 6 );

    CHECK_FALSE( pocket.can_reload_with( *speedloader, true ) );
}

TEST_CASE( "empty speedloader is rejected without a crash", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    detached_ptr<item> speedloader = loaded_speedloader( 0 );
    REQUIRE( speedloader->loaded_ammo().is_null() );

    CHECK_FALSE( pocket.can_reload_with( *speedloader, true ) );
}

TEST_CASE( "loaded_ammo returns the speedloader rounds", "[p3][ammo]" ) {
    detached_ptr<item> speedloader = loaded_speedloader( 6 );
    const item &rounds = speedloader->loaded_ammo();
    REQUIRE_FALSE( rounds.is_null() );
    CHECK( rounds.is_ammo() );
    CHECK( rounds.typeId() == itype_id( "40sw" ) );
    CHECK( rounds.count() == 6 );

    detached_ptr<item> empty = item::spawn( itype_id( "40_speedloader6" ) );
    CHECK( empty->loaded_ammo().is_null() );
}

// DDA item_gun_tool_ammo.cpp:1528-1542: the carrier overload and the no-arg
// forwarder agree on a locally loaded gun.
TEST_CASE( "ammo_remaining with a carrier matches the no-arg overload", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );

    CHECK( gun->ammo_remaining( nullptr ) == 30 );
    CHECK( gun->ammo_remaining() == 30 );
}

// Guard: a magazine's own rounds are still summed by the no-arg overload.
TEST_CASE( "ammo_remaining sums a loaded magazine", "[p3][ammo]" ) {
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ) );

    CHECK( mag->ammo_remaining() == 10 );
}

// Guard: a charges-only tool with no ammo type reports its charges (the DDA
// ammo.empty() branch).
TEST_CASE( "ammo_remaining returns charges for a charges-only tool", "[p3][ammo]" ) {
    detached_ptr<item> candle = item::spawn( itype_id( "candle" ) );
    REQUIRE( candle->is_tool() );
    REQUIRE( candle->ammo_types().empty() );

    candle->charges = 37;
    CHECK( candle->ammo_remaining() == 37 );
}

// O1 gate: a gun carrying an ammo type but no magazine pocket keeps its direct charges (the
// // CDF: guard, D14; tools left it in 4-b2 and own an integral MAGAZINE pocket instead).
TEST_CASE( "ammo_remaining keeps direct charges on an ammo gun with no magazine pocket",
           "[p3][ammo]" ) {
    detached_ptr<item> gun = item::spawn( itype_id( "pipe_pistol" ) );
    REQUIRE( gun->is_gun() );
    REQUIRE_FALSE( gun->ammo_types().empty() );
    REQUIRE_FALSE( gun->contents.has_pocket_type( pocket_type::MAGAZINE ) );
    REQUIRE_FALSE( gun->contents.has_pocket_type( pocket_type::MAGAZINE_WELL ) );

    gun->charges = 5;
    CHECK( gun->ammo_remaining() == 5 );
}

// 4-b2: a tool with an ammo type and max_charges gets an integral MAGAZINE pocket from the loader;
// ammo_set puts the rounds there, ammo_remaining reads them, `charges` stays 0.
TEST_CASE( "ammo_set puts an integral tool's rounds in its magazine pocket", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> lantern = item::spawn( itype_id( "gasoline_lantern" ) );
    REQUIRE( lantern->is_tool() );
    REQUIRE_FALSE( lantern->ammo_types().empty() );
    REQUIRE( lantern->contents.has_pocket_type( pocket_type::MAGAZINE ) );
    REQUIRE( lantern->magazine_integral() );
    // spawned full from initial_charges, in the pocket
    CHECK( lantern->charges == 0 );
    CHECK( lantern->ammo_remaining() == 500 );

    lantern->ammo_set( itype_id( "gasoline" ), 120 );
    CHECK( lantern->charges == 0 );
    CHECK( lantern->ammo_remaining() == 120 );
    CHECK( lantern->ammo_capacity( ammotype( "gasoline" ) ) == 500 );

    lantern->ammo_set( itype_id( "gasoline" ), -1 );
    CHECK( lantern->ammo_remaining() == 500 );

    lantern->ammo_unset();
    CHECK( lantern->ammo_remaining() == 0 );
    CHECK( lantern->contents.has_pocket_type( pocket_type::MAGAZINE ) );
}

// DDA item_gun_tool_ammo.cpp:2342-2350: the item-level magazines_current pair
// forwards to item_contents.
TEST_CASE( "item magazines_current lists loaded magazines", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item *mag = gun->magazine_current();
    REQUIRE( mag != nullptr );

    const item &const_gun = *gun;
    const std::vector<const item *> const_mags = const_gun.magazines_current();
    REQUIRE( const_mags.size() == 1 );
    CHECK( const_mags.front() == mag );

    std::vector<item *> mags = gun->magazines_current();
    REQUIRE( mags.size() == 1 );
    CHECK( mags.front() == mag );

    detached_ptr<item> unloaded = item::spawn( itype_id( "m4a1" ) );
    CHECK( unloaded->magazines_current().empty() );
}

// DDA item_gun_tool_ammo.cpp:1720-1769: per-ammotype capacity reads the
// matching pocket.
TEST_CASE( "ammo_capacity by ammotype reads the magazine's own pocket", "[p3][ammo]" ) {
    const ammotype round = item::spawn( itype_id( "556" ) )->ammo_type();

    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    CHECK( mag->ammo_capacity( round ) == 30 );

    // CDF: loaded_m4a1( 0 ) still holds an empty stanag30, so its well reports
    // the magazine's 30-round capacity. A m4a1 with no magazine reports 0.
    detached_ptr<item> empty_gun = item::spawn( itype_id( "m4a1" ) );
    CHECK( empty_gun->ammo_capacity( round ) == 0 );

    detached_ptr<item> empty_mag_gun = loaded_m4a1( 0 );
    CHECK( empty_mag_gun->ammo_capacity( round ) == 30 );

    detached_ptr<item> loaded_gun = loaded_m4a1( 30 );
    CHECK( loaded_gun->ammo_capacity( round ) == 30 );
}

// DDA item_gun_tool_ammo.cpp:2481-2527: a loaded well is the pocket for its
// ammotype; items without ammo pockets have none.
TEST_CASE( "pocket_for_ammo returns the matching well", "[p3][ammo]" ) {
    const ammotype round = item::spawn( itype_id( "556" ) )->ammo_type();

    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item_pocket *p = gun->pocket_for_ammo( round );
    REQUIRE( p != nullptr );
    CHECK( p->is_type( pocket_type::MAGAZINE_WELL ) );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK( backpack->pocket_for_ammo( round ) == nullptr );
}

// DDA item_gun_tool_ammo.cpp:1635-1656: a loaded gun's remaining capacity is
// the loaded magazine's free space.
TEST_CASE( "remaining_ammo_capacity of a loaded gun is the magazine's free space",
           "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 20 );
    CHECK( gun->remaining_ammo_capacity() == 10 );

    detached_ptr<item> full = loaded_m4a1( 30 );
    CHECK( full->remaining_ammo_capacity() == 0 );
}

// DDA item_gun_tool_ammo.cpp:1830-1916: the carrier overload drains the loaded
// magazine (R-1).
TEST_CASE( "ammo_consume with a carrier drains a loaded gun", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    CHECK( gun->ammo_consume( 5, pos, nullptr ) == 5 );
    CHECK( gun->ammo_remaining() == 25 );
}

// DDA item_gun_tool_ammo.cpp:1825-1828: the map overload forwards the map
// through the magazine recursion, which ejects the spent magazine (R-2).
TEST_CASE( "ammo_consume with a map overload ejects a MAG_EJECT magazine", "[p3][ammo]" ) {
    // CDF: garandclip is MAG_EJECT (data/json/items/magazine/3006.json) and the
    // garand is its host gun.
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    detached_ptr<item> gun = item::spawn( itype_id( "garand" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "garandclip" ) );
    mag->put_in( item::spawn( itype_id( "3006" ), calendar::turn, 8 ) );
    gun->put_in( std::move( mag ) );
    REQUIRE( gun->magazine_current() != nullptr );

    CHECK( gun->ammo_consume( 8, here, pos, nullptr ) == 8 );
    CHECK( gun->magazine_current() == nullptr );
    REQUIRE( here.i_at( pos ).size() == 1 );
    CHECK( here.i_at( pos ).only_item().typeId() == itype_id( "garandclip" ) );
}

// DDA item_gun_tool_ammo.cpp:1800-1813: the carrier overload counts rounds (R-3).
TEST_CASE( "ammo_sufficient with a carrier counts rounds", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );

    CHECK( gun->ammo_sufficient( nullptr, 30 ) );
    CHECK_FALSE( gun->ammo_sufficient( nullptr, 31 ) );
}

// DDA item_gun_tool_ammo.cpp:1815-1823 and 2598-2604: the method form scales
// the quantity by itype::ammo_scale (8-4b).
TEST_CASE( "ammo_sufficient_method_applies_ammo_scale", "[p3][ammo][8-4b]" ) {
    clear_all_state();
    detached_ptr<item> tool = item::spawn( itype_id( "test_ammo_scale_tool" ) );
    const int req = tool->ammo_required();
    REQUIRE( req > 0 );
    detached_ptr<item> battery = item::spawn( itype_id( "light_battery_cell" ) );
    battery->ammo_unset();
    item *bat = &*battery;
    tool->put_in( std::move( battery ) );
    bat->ammo_set( itype_id( "battery" ), 2 * req );
    Character &c = get_avatar();

    CHECK( tool->ammo_sufficient( &c ) );
    CHECK( tool->ammo_sufficient( &c, "no_such_method" ) );
    CHECK( tool->ammo_sufficient( &c, "CROWBAR" ) );
    CHECK_FALSE( tool->ammo_sufficient( &c, "transform" ) );
    CHECK_FALSE( tool->ammo_sufficient( &c, "CROWBAR", 2 ) );
    CHECK( tool->expected_cost_per_use( "transform" ) == 3 * req );

    bat->ammo_set( itype_id( "battery" ), 0 );
    CHECK( tool->ammo_sufficient( &c, "HAMMER" ) );
    CHECK_FALSE( tool->ammo_sufficient( &c, "no_such_method" ) );
}

// DDA item_gun_tool_ammo.cpp:1790-1798: first_ammo reaches the loaded round (R-4).
TEST_CASE( "first_ammo of a loaded magazine is its round", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 10 );
    const ammotype round = item::spawn( itype_id( "556" ) )->ammo_type();

    REQUIRE_FALSE( gun->first_ammo().is_null() );
    CHECK( gun->first_ammo().is_ammo() );
    CHECK( gun->first_ammo().ammo_type() == round );

    const item &const_gun = *gun;
    CHECK( const_gun.first_ammo().typeId() == itype_id( "556" ) );
}

// DDA item_gun_tool_ammo.cpp:2258-2261: uses_magazine is the MAGAZINE_WELL test (R-5).
TEST_CASE( "uses_magazine is true for a gun with a magazine well", "[p3][ammo]" ) {
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    CHECK( gun->uses_magazine() );

    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    CHECK_FALSE( mag->uses_magazine() );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK_FALSE( backpack->uses_magazine() );
}

// O1/D14 gate: a gun carrying an ammo type but no magazine pocket keeps its direct charges, and
// ammo_consume must drain them (G-6, OD-3).
TEST_CASE( "ammo_consume drains a legacy gun's direct charges", "[p3][ammo]" ) {
    clear_map();
    detached_ptr<item> gun = item::spawn( itype_id( "pipe_pistol" ) );
    REQUIRE( gun->is_gun() );
    REQUIRE_FALSE( gun->ammo_types().empty() );
    REQUIRE_FALSE( gun->contents.has_pocket_type( pocket_type::MAGAZINE ) );
    REQUIRE_FALSE( gun->contents.has_pocket_type( pocket_type::MAGAZINE_WELL ) );

    gun->charges = 5;
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();
    CHECK( gun->ammo_consume( 3, pos, nullptr ) == 3 );
    CHECK( gun->charges == 2 );
}

// 4-b2: an integral tool drains the rounds in its MAGAZINE pocket; `charges` is not touched.
TEST_CASE( "ammo_consume drains an integral tool's magazine pocket", "[p3][ammo]" ) {
    clear_map();
    detached_ptr<item> lantern = item::spawn( itype_id( "gasoline_lantern" ) );
    REQUIRE( lantern->contents.has_pocket_type( pocket_type::MAGAZINE ) );
    lantern->ammo_set( itype_id( "gasoline" ), 5 );
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();
    CHECK( lantern->ammo_consume( 3, pos, nullptr ) == 3 );
    CHECK( lantern->ammo_remaining() == 2 );
    CHECK( lantern->charges == 0 );
}

// DDA item_gun_tool_ammo.cpp:1911-1915: the return is clamped to the loaded
// rounds (G-7).
TEST_CASE( "ammo_consume clamps to the loaded rounds", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 10 );
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    CHECK( gun->ammo_consume( 40, pos, nullptr ) == 10 );
    CHECK( gun->ammo_remaining() == 0 );
}

// DDA item_gun_tool_ammo.cpp:1771-1788 with the CDF gate (OD-10): a gun reports
// ammo_to_fire, a non-tool non-gun reports zero (G-8).
// R-12 (lookup 9): has_enough_charges counts the carrier's bionic power. A
// nullptr carrier means no bionic power (ADV-7 / OD-4): there is no avatar
// fallback in the bodies.
TEST_CASE( "has_enough_charges counts the carrier's bionic power", "[p3][ammo]" ) {
    clear_all_state();
    // CDF: toolset is the USES_BIONIC_POWER tool with no ammo type and no
    // magazine pocket (data/json/items/fake.json:31-39); it spawns with 0 charges.
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    REQUIRE( toolset->has_flag( flag_USES_BIONIC_POWER ) );
    toolset->charges = 0;

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 0_kJ );

    standard_npc who( "carrier" );
    who.set_max_power_level( 100_kJ );
    who.set_power_level( 50_kJ );

    CHECK( who.has_enough_charges( *toolset, false ) );
}

// R-12b (guard): an NPC with no power and a charged avatar still reports false
// after the edit; the no-arg forwarder is not consulted.
TEST_CASE( "has_enough_charges does not fall back to the avatar's power", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    toolset->charges = 0;

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 50_kJ );

    standard_npc who( "carrier" );
    who.set_max_power_level( 100_kJ );
    who.set_power_level( 0_kJ );

    CHECK_FALSE( who.has_enough_charges( *toolset, false ) );
}

// R-11: consume_charges drains the carrier's bionic power, not the item's
// charges or the avatar's power.
TEST_CASE( "consume_charges drains the carrier's bionic power", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    toolset->charges = 0;

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 0_kJ );

    standard_npc who( "carrier" );
    who.set_max_power_level( 100_kJ );
    who.set_power_level( 50_kJ );

    who.consume_charges( *toolset, 3 );
    CHECK( who.get_power_level() == 47_kJ );
    CHECK( toolset->charges == 0 );
}

TEST_CASE( "ammo_required of a gun is its ammo_to_fire, of a magazine zero", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    // CDF: m4a1 inherits rifle_base (data/json/items/classes/gun.json), which
    // sets no ammo_to_fire, so the default is 1 (src/itype.h:730); no gunmods.
    REQUIRE( gun->type->gun->ammo_to_fire == 1 );
    CHECK( gun->ammo_required() == 1 );

    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    CHECK( mag->ammo_required() == 0 );
}

// 3-4b-c1 guard (passes before and after the caller edit; the avatar was the
// implicit carrier through the transitional forwarder): rate_action_use passes
// its avatar as the carrier, so a USES_BIONIC_POWER tool is usable with power.
TEST_CASE( "rate_action_use of a charged bionic tool is good for the avatar with power",
           "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    REQUIRE( toolset->has_flag( flag_USES_BIONIC_POWER ) );
    toolset->charges = 0;

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 50_kJ );
    CHECK( examine_item_menu::rate_action_use( get_avatar(), *toolset ) ==
          examine_item_menu::hint_rating::good );

    get_avatar().set_power_level( 0_kJ );
    CHECK( examine_item_menu::rate_action_use( get_avatar(), *toolset ) ==
          examine_item_menu::hint_rating::iffy );
}

// 3-4b-c2 guard (passes before and after the caller edits): a nullptr carrier
// has no bionic power, an avatar carrier does.
TEST_CASE( "a nullptr carrier counts no bionic power", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    REQUIRE( toolset->has_flag( flag_USES_BIONIC_POWER ) );
    toolset->charges = 0;

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 50_kJ );
    CHECK_FALSE( toolset->ammo_sufficient( nullptr ) );
    CHECK( toolset->ammo_sufficient( &get_avatar() ) );
}

// 3-4b-d3 R-14: a legacy tool (direct `charges`, magazine_well but no `magazines` list) with a
// magazine loaded in its well counts that magazine only; the old BN ammo_remaining() returned
// the loaded magazine's count and ignored the tool's own `charges`.
TEST_CASE( "a legacy tool with a loaded magazine counts the magazine only", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> tool = item::spawn( itype_id( "smart_phone_music" ) );
    detached_ptr<item> battery = item::spawn( itype_id( "light_battery_cell" ) );
    battery->ammo_unset();
    REQUIRE( battery->ammo_remaining() == 0 );
    // 4-b2: the phone's own rounds live in its integral MAGAZINE pocket; the parked battery wins.
    REQUIRE( tool->ammo_remaining() > 0 );

    item *bat = &*battery;
    tool->put_in( std::move( battery ) );
    REQUIRE( tool->magazine_current() == bat );
    CHECK( tool->ammo_remaining() == 0 );

    bat->ammo_set( itype_id( "battery" ), 10 );
    CHECK( tool->ammo_remaining() == 10 );
}

// 3-4b-d3 R-15: ammo_set on a gun with a magazine_well and a `magazines` list spawns the default
// magazine, puts it in the well and fills it (vehicle_part::ammo_set for a turret relies on it).
TEST_CASE( "a mounted-gun ammo_set loads the belt", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> gun = item::spawn( itype_id( "m2browning" ) );
    gun->ammo_set( itype_id( "50bmg" ) );
    CHECK( gun->magazine_current() != nullptr );
    CHECK( gun->ammo_remaining() > 0 );
    CHECK( gun->ammo_sufficient( nullptr ) );
}

// 3-4c-a: pocket predicates, has_ammo / has_ammo_data.
TEST_CASE( "pocket_is_primary_ammo true for the gun's ammo pocket, false for a battery pocket",
           "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item_pocket *well = gun->primary_ammo_pocket();
    REQUIRE( well != nullptr );
    CHECK( gun->pocket_is_primary_ammo( well ) );
    CHECK_FALSE( gun->pocket_is_primary_ammo( nullptr ) );

    detached_ptr<item> carver = item::spawn( itype_id( "carver_off" ) );
    const item_pocket *battery_well = carver->primary_ammo_pocket();
    REQUIRE( battery_well != nullptr );
    CHECK_FALSE( gun->pocket_is_primary_ammo( battery_well ) );
}

TEST_CASE( "pocket_accepts_battery true for a battery well, false for an ammo well",
           "[p3][ammo]" ) {
    detached_ptr<item> carver = item::spawn( itype_id( "carver_off" ) );
    const item_pocket *battery_well = nullptr;
    for( const item_pocket *p : carver->get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            battery_well = p;
        }
    }
    REQUIRE( battery_well != nullptr );
    CHECK( carver->pocket_accepts_battery( battery_well ) );

    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item_pocket *ammo_well = gun->primary_ammo_pocket();
    REQUIRE( ammo_well != nullptr );
    CHECK_FALSE( gun->pocket_accepts_battery( ammo_well ) );
    CHECK_FALSE( gun->pocket_accepts_battery( nullptr ) );
}

TEST_CASE( "has_ammo false for an empty magazine gun, true once loaded", "[p3][ammo]" ) {
    // CDF: loaded_m4a1( 0 ) puts a zero-charge round item in the magazine, which is still an ammo
    // item for has_ammo; an empty magazine is built directly instead.
    detached_ptr<item> empty = item::spawn( itype_id( "m4a1" ) );
    empty->put_in( item::spawn( itype_id( "stanag30" ) ) );
    CHECK_FALSE( empty->has_ammo() );
    detached_ptr<item> loaded = loaded_m4a1( 30 );
    CHECK( loaded->has_ammo() );
}

TEST_CASE( "has_ammo is false for a battery-only tool", "[p3][ammo]" ) {
    detached_ptr<item> carver = item::spawn( itype_id( "carver_off" ) );
    CHECK_FALSE( carver->has_ammo() );
    CHECK_FALSE( carver->has_ammo_data() );
}

TEST_CASE( "has_ammo_data matches has_ammo for a loaded magazine gun", "[p3][ammo]" ) {
    // CDF: empty magazine built directly (see the has_ammo case above).
    detached_ptr<item> empty = item::spawn( itype_id( "m4a1" ) );
    empty->put_in( item::spawn( itype_id( "stanag30" ) ) );
    CHECK( empty->has_ammo_data() == empty->has_ammo() );
    detached_ptr<item> loaded = loaded_m4a1( 30 );
    CHECK( loaded->has_ammo_data() == loaded->has_ammo() );
    CHECK( loaded->has_ammo_data() );
}

TEST_CASE( "multimag_loaded_ammo_does_not_borrow_sibling_pocket", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_integral_ammo" ) );
    REQUIRE( gun->uses_firing_requirements() );

    detached_ptr<item> battery = item::spawn( itype_id( "heavy_battery_cell" ) );
    // CDF: ammo_set takes an id; ammo_default() of the cell is "battery" (DDA used itype_battery, 100).
    battery->ammo_set( battery->ammo_default(), 56 );
    REQUIRE( gun->put_in( std::move( battery ), pocket_type::MAGAZINE_WELL ).success() );

    CHECK_FALSE( gun->has_ammo() );
    CHECK_FALSE( gun->has_ammo_data() );
    CHECK( gun->ammo_data() == nullptr );
    CHECK( gun->loaded_ammo().is_null() );
    CHECK( gun->ammo_current() == itype_id::NULL_ID() );

    // After loading the slug pocket, identity is anchored to the projectile
    // and loaded_ammo() must resolve to the slug, not null.
    REQUIRE( gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ),
                          pocket_type::MAGAZINE ).success() );
    CHECK( gun->has_ammo() );
    CHECK( gun->has_ammo_data() );
    REQUIRE( gun->ammo_data() != nullptr );
    CHECK( gun->ammo_current() == itype_id( "9mm" ) );
    const item &loaded = gun->loaded_ammo();
    CHECK_FALSE( loaded.is_null() );
    CHECK( loaded.typeId() == itype_id( "9mm" ) );
}

TEST_CASE( "multimag_integral_pocket_skips_casings", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_integral_ammo" ) );
    REQUIRE( gun->uses_firing_requirements() );

    detached_ptr<item> battery = item::spawn( itype_id( "heavy_battery_cell" ) );
    // CDF: ammo_set takes an id; 56 instead of DDA's 100, so the totals below are 56 and 57.
    battery->ammo_set( battery->ammo_default(), 56 );
    REQUIRE( gun->put_in( std::move( battery ), pocket_type::MAGAZINE_WELL ).success() );

    // Casing alone in the projectile pocket: not loaded ammo.
    detached_ptr<item> casing = item::spawn( itype_id( "9mm_casing" ), calendar::turn, 1 );
    casing->set_flag( flag_CASING );
    gun->force_insert_item( std::move( casing ), pocket_type::MAGAZINE );
    CHECK_FALSE( gun->has_ammo() );
    CHECK_FALSE( gun->has_ammo_data() );
    CHECK( gun->ammo_data() == nullptr );
    CHECK( gun->ammo_remaining() == 56 );

    // Live slug alongside the casing: identity resolves to slug, count is 1.
    REQUIRE( gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ),
                          pocket_type::MAGAZINE ).success() );
    CHECK( gun->has_ammo() );
    CHECK( gun->has_ammo_data() );
    CHECK( gun->ammo_remaining() == 57 );
}

TEST_CASE( "ammo_data on a plain magazine and on ammo", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> round = item::spawn( itype_id( "9mm" ) );
    REQUIRE( round->ammo_data() != nullptr );
    CHECK( round->ammo_data()->get_id() == itype_id( "9mm" ) );

    detached_ptr<item> empty_mag = item::spawn( itype_id( "glockmag" ) );
    CHECK( empty_mag->ammo_data() == nullptr );

    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    REQUIRE( mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ),
                          pocket_type::MAGAZINE ).success() );
    REQUIRE( mag->ammo_data() != nullptr );
    CHECK( mag->ammo_data()->get_id() == itype_id( "9mm" ) );
}

TEST_CASE( "battery_energy_test", "[p3][ammo]" ) {
    clear_all_state();
    // CDF: medium_battery_cell is a MAGAZINE here (is_battery() false); battery_test is the BATTERY item, 30 kJ.
    detached_ptr<item> test_battery = item::spawn( itype_id( "battery_test" ) );
    // CDF: mod_energy( 30_kJ ) in place of ammo_set( ammo_default(), 56 ).
    REQUIRE( test_battery->mod_energy( 30_kJ ) == 0_J );

    SECTION( "Integer drain from battery" ) {
        REQUIRE( test_battery->energy_remaining() == 30_kJ ); // CDF: no carrier argument, 30 kJ
        units::energy consumed = test_battery->energy_consume( 20_kJ, tripoint_bub_ms::zero(),
                                 nullptr ); // CDF: 20 kJ (DDA 40)
        CHECK( test_battery->energy_remaining() == 10_kJ ); // CDF: 10 kJ (DDA 16)
        CHECK( consumed == 20_kJ ); // CDF: 20 kJ (DDA 40)
    }

    SECTION( "Integer over-drain from battery" ) {
        REQUIRE( test_battery->energy_remaining() == 30_kJ ); // CDF: no carrier argument, 30 kJ
        units::energy consumed = test_battery->energy_consume( 400_kJ, tripoint_bub_ms::zero(), nullptr );
        CHECK( test_battery->energy_remaining() == 0_kJ ); // CDF: no carrier argument
        CHECK( consumed == 30_kJ ); // CDF: 30 kJ (DDA 56)
    }

    SECTION( "Non-integer drain from battery" ) {
        // Battery charge is in mJ now, so check for precise numbers.
        // 4.5 kJ drain is 4.5 kJ drain
        REQUIRE( test_battery->energy_remaining() == 30_kJ ); // CDF: no carrier argument, 30 kJ
        units::energy consumed = test_battery->energy_consume( 4500_J, tripoint_bub_ms::zero(), nullptr );
        CHECK( test_battery->energy_remaining() == 25.5_kJ ); // CDF: 25.5 kJ (DDA 51.5)
        CHECK( consumed == 4500_J );
    }

    SECTION( "Tiny Non-integer drain from battery" ) {
        // Make sure lots of tiny discharges sum up as expected.
        REQUIRE( test_battery->energy_remaining() == 30_kJ ); // CDF: no carrier argument, 30 kJ
        for( int i = 0; i < 133; ++i ) {
            units::energy consumed = test_battery->energy_consume( 2_J, tripoint_bub_ms::zero(), nullptr );
            CHECK( consumed == 2_J );
        }
        CHECK( test_battery->energy_remaining() == 29734_J ); // CDF: 30000 - 133 * 2 (DDA 55734)
    }

    SECTION( "Non-integer over-drain from battery" ) {
        REQUIRE( test_battery->energy_remaining() == 30_kJ ); // CDF: no carrier argument, 30 kJ
        units::energy consumed = test_battery->energy_consume( 500500_J, tripoint_bub_ms::zero(),
                                 nullptr );
        CHECK( test_battery->energy_remaining() == 0_kJ ); // CDF: no carrier argument
        CHECK( consumed == 30_kJ ); // CDF: 30 kJ (DDA 56)
    }

    SECTION( "zero drain from battery" ) {
        REQUIRE( test_battery->energy_remaining() == 30_kJ ); // CDF: no carrier argument, 30 kJ
        units::energy consumed = test_battery->energy_consume( 0_J, tripoint_bub_ms::zero(), nullptr );
        CHECK( test_battery->energy_remaining() == 30_kJ ); // CDF: no carrier argument, 30 kJ
        CHECK( consumed == 0_kJ );
    }
}

TEST_CASE( "needs_charges_to_use covers multimag", "[p3][ammo]" ) {
    clear_all_state();
    // DDA multimag_test.cpp:1267-1277 make_multimag_consume_gun()
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ) );
    detached_ptr<item> bb_mag = item::spawn( itype_id( "glockmag" ) );
    bb_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    detached_ptr<item> batt_mag = item::spawn( itype_id( "heavy_battery_cell" ) );
    batt_mag->put_in( item::spawn( itype_id( "battery" ), calendar::turn, 100 ),
                      pocket_type::MAGAZINE );
    REQUIRE( gun->put_in( std::move( bb_mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->put_in( std::move( batt_mag ), pocket_type::MAGAZINE_WELL ).success() );
    CHECK( gun->needs_charges_to_use() );
}

TEST_CASE( "needs_charges_to_use true for a loaded m4a1, false for a plain non-gun item",
           "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> gun = loaded_m4a1( 30 );
    REQUIRE( gun->ammo_required() > 0 );
    CHECK( gun->needs_charges_to_use() );

    detached_ptr<item> rock = item::spawn( itype_id( "rock" ) );
    REQUIRE( rock->get_gun_ups_drain() == 0 );
    CHECK_FALSE( rock->needs_charges_to_use() );
}

// 3-8t: tools with ammo and max_charges get an integral MAGAZINE pocket, so is_magazine() is
// true for them (D13). The flashlight named in the brief has only a MAGAZINE_WELL
// (data/json/items/tool/lighting.json:183), so these cases use oil_lamp (lamp_oil, 750) and
// mp3 (battery, 200), which carry an integral pocket and no type->magazine.
// CDF: every base MAGAZINE_WELL has an item_id_restriction (smart_phone_music's "magazine_well"
// is only a volume offset, not a well pocket), so the R4 case uses the TEST_DATA test_flashlight
// (data/mods/TEST_DATA/dda_pocket_items.json:998), whose MAGAZINE_WELL has no restriction.
TEST_CASE( "item_reload_cost does not dereference a null magazine for a tool",
           "[p3][ammo][magazine]" ) {
    clear_all_state();
    detached_ptr<item> lamp = item::spawn( itype_id( "oil_lamp" ) );
    REQUIRE( lamp->is_tool() );
    REQUIRE( lamp->is_magazine() );
    REQUIRE( lamp->type->magazine == nullptr );
    detached_ptr<item> oil = item::spawn( itype_id( "lamp_oil" ), calendar::turn, 100 );
    // CDF: DDA character_ammo.cpp:216 obtain_cost( ammo ) path; the crash was the null deref
    CHECK( get_avatar().item_reload_cost( *lamp, *oil, 1 ) > 0 );

    // The gun branch is unchanged.
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    CHECK( get_avatar().item_reload_cost( *gun, *mag, 1 ) > 0 );
}

TEST_CASE( "visit_items sees ammo inside a tool but not inside a gun",
           "[p3][ammo][magazine]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    u.worn.clear();
    u.inv_clear();
    u.remove_primary_weapon();
    u.wear_item( item::spawn( "backpack" ) );

    detached_ptr<item> lamp = item::spawn( itype_id( "oil_lamp" ) );
    REQUIRE( lamp->put_in( item::spawn( itype_id( "lamp_oil" ), calendar::turn, 100 ),
                           pocket_type::MAGAZINE ).success() );
    u.i_add( std::move( lamp ) );
    detached_ptr<item> gun = loaded_m4a1( 30 );
    u.i_add( std::move( gun ) );

    int oil_seen = 0;
    int rounds_seen = 0;
    int mags_seen = 0;
    u.visit_items( [&]( item * it ) -> VisitResponse {
        if( it->typeId() == itype_id( "lamp_oil" ) ) {
            ++oil_seen;
        } else if( it->typeId() == itype_id( "556" ) ) {
            ++rounds_seen;
        } else if( it->typeId() == itype_id( "stanag30" ) ) {
            ++mags_seen;
        }
        return VisitResponse::NEXT;
    } );
    CHECK( oil_seen == 1 ); // tool contents are visible (DDA recurses everywhere)
    CHECK( mags_seen == 0 ); // gun contents stay hidden (BN rule kept)
    CHECK( rounds_seen == 0 );
}

TEST_CASE( "a tool with ammo does not fit a magazine well",
           "[p3][ammo][magazine][pocket]" ) {
    clear_all_state();
    detached_ptr<item> phone = item::spawn( itype_id( "test_flashlight" ) );
    REQUIRE( pocket_index( *phone, true ) >= 0 );
    detached_ptr<item> player_tool = item::spawn( itype_id( "mp3" ) );
    REQUIRE( player_tool->is_tool() );
    REQUIRE( player_tool->is_magazine() );
    item_pocket *pocket = phone->contents.fitting_pocket( *player_tool, false );
    CHECK( ( pocket == nullptr || !pocket->is_type( pocket_type::MAGAZINE_WELL ) ) );

    // A plain battery magazine still reaches the MAGAZINE_WELL.
    detached_ptr<item> cell = item::spawn( itype_id( "light_battery_cell" ) );
    REQUIRE( cell->is_magazine() );
    item_pocket *cell_pocket = phone->contents.fitting_pocket( *cell, false );
    REQUIRE( cell_pocket != nullptr );
    CHECK( cell_pocket->is_type( pocket_type::MAGAZINE_WELL ) );
}

// 4-c-3 R-1: item::price counts the rounds of an integral magazine. The pocket's rounds are read
// with ammo_remaining(); the old read of `charges` gave 0 rounds for a tool with a pocket.
TEST_CASE( "item::price counts an integral magazine's rounds", "[p3][ammo]" ) {
    clear_all_state();
    detached_ptr<item> loaded = item::spawn( itype_id( "gasoline_lantern" ) );
    detached_ptr<item> empty = item::spawn( itype_id( "gasoline_lantern" ) );
    REQUIRE( loaded->magazine_integral() );
    loaded->ammo_set( itype_id( "gasoline" ), 120 );
    empty->ammo_unset();
    REQUIRE( loaded->ammo_remaining() == 120 );
    REQUIRE( empty->ammo_remaining() == 0 );
    REQUIRE( loaded->ammo_data() != nullptr );

    detached_ptr<item> fuel = item::spawn( itype_id( "gasoline" ), calendar::turn, 120 );
    const float fuel_price = fuel->price( true );
    REQUIRE( fuel_price > 0.0f );
    // The fuel item in the pocket is priced by the visit; the integral term adds the rounds again.
    CHECK( loaded->price( true ) - empty->price( true ) > fuel_price * 1.5f );
}

TEST_CASE( "ammo_scale_loads_from_use_action_object_and_array", "[p3][ammo]" )
{
    const itype &t = itype_id( "test_ammo_scale_tool" ).obj();
    CHECK( t.ammo_scale.at( "transform" ) == 3 );
    CHECK( t.ammo_scale.at( "CROWBAR" ) == 2 );
    CHECK( t.ammo_scale.at( "HAMMER" ) == 0 );
    CHECK( t.ammo_scale.count( "HEAT_FOOD" ) == 0 );
}

TEST_CASE( "ammo_scale_defaults_are_zero_for_quality_and_attach_methods", "[p3][ammo]" )
{
    CHECK( itype_id( "hacksaw" ).obj().ammo_scale.at( "HACKSAW" ) == 0 );
    CHECK( itype_id( "test_multimag_consume_gunmod" ).obj().ammo_scale.at( "GUNMOD_ATTACH" ) == 0 );
    CHECK( itype_id( "test_multimag_consume_toolmod" ).obj().ammo_scale.at( "TOOLMOD_ATTACH" ) == 0 );
    CHECK( itype_id( "glock_19" ).obj().ammo_scale.at( "detach_gunmods" ) == 0 );
    CHECK( itype_id( "rock" ).obj().ammo_scale.empty() );
}

// DDA tests/multimag_test.cpp:1825-1846
// CDF: extra tag [8-4d] so this case can be run alone; DDA uses [multimag][consume]
TEST_CASE( "Character_consume_charges_hard_guards_multimag_tools", "[p3][ammo][8-4d]" ) {
    clear_avatar();
    // CDF: get_player_character() used instead of DDA get_avatar()
    Character &chr = get_player_character();
    // CDF: i_add returns item &, not item_location
    item &carried = chr.i_add( make_multimag_consume_tool( 12, 30 ) );
    REQUIRE( carried.uses_firing_requirements() );

    bool destroyed = false;
    const std::string captured = capture_debugmsg_during( [&]() {
        destroyed = chr.consume_charges( carried, 1 );
    } );
    CHECK_FALSE( destroyed );
    CHECK( captured.find( "multimag" ) != std::string::npos );
    // CDF: the 4-arg ammo_consume guard already prints "multimag", so the DDA check alone passes before the edit
    CHECK( captured.find( "consume_charges called on multimag tool" ) != std::string::npos );
    CHECK( captured.find( "ammo_consume(" ) == std::string::npos );
    CHECK( carried.ammo_remaining_in_pocket( "well_a" ) == 12 );
    CHECK( carried.ammo_remaining_in_pocket( "well_b" ) == 30 );
}

// 3-4d: DDA multimag_test.cpp uses_energy / is_chargeable / energy_remaining( carrier ) on a
// gun with an ammo well and a power well.
static auto make_energy_block_gun( item *&cell_out, int battery_charges ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ) );
    detached_ptr<item> cell = item::spawn( itype_id( "heavy_battery_cell" ) );
    cell->ammo_set( itype_id( "battery" ), battery_charges );
    cell_out = &*cell;
    REQUIRE( gun->put_in( std::move( cell ), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}

TEST_CASE( "energy_block_uses_energy_follows_loaded_battery_well", "[p3][ammo][3-4d]" ) {
    clear_all_state();
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ) );
    CHECK_FALSE( gun->uses_energy() );
    detached_ptr<item> cell = item::spawn( itype_id( "heavy_battery_cell" ) );
    cell->ammo_set( itype_id( "battery" ), 100 );
    REQUIRE( gun->put_in( std::move( cell ), pocket_type::MAGAZINE_WELL ).success() );
    CHECK( gun->uses_energy() );
}

TEST_CASE( "energy_block_is_chargeable_below_capacity_only", "[p3][ammo][3-4d]" ) {
    clear_all_state();
    item *cell = nullptr;
    detached_ptr<item> gun = make_energy_block_gun( cell, 1 );
    REQUIRE( cell != nullptr );
    const int cap = cell->ammo_capacity( ammotype( "battery" ) );
    REQUIRE( cap > 1 );
    cell->ammo_set( itype_id( "battery" ), cap );
    CHECK_FALSE( gun->is_chargeable() );
    cell->ammo_set( itype_id( "battery" ), cap - 1 );
    CHECK( gun->is_chargeable() );
}

TEST_CASE( "energy_block_energy_remaining_sums_well_with_nullptr_carrier", "[p3][ammo][3-4d]" ) {
    clear_all_state();
    item *cell = nullptr;
    detached_ptr<item> gun = make_energy_block_gun( cell, 100 );
    CHECK( gun->energy_remaining( nullptr ) == units::from_kilojoule( 100 ) );
}

TEST_CASE( "shots_remaining_ammo_part_divides_ammo_by_required", "[p3][ammo][3-4d]" ) {
    clear_all_state();
    detached_ptr<item> g = loaded_m4a1( 30 );
    REQUIRE( g->ammo_required() > 0 );
    Character &c = get_avatar();
    CHECK( g->shots_remaining( get_map(), &c ) ==
           g->ammo_remaining_linked( get_map(), &c ) / g->ammo_required() );
}

TEST_CASE( "ammo_sufficient_follows_shots_remaining", "[p3][ammo][3-4d]" ) {
    clear_all_state();
    detached_ptr<item> g = loaded_m4a1( 30 );
    REQUIRE( g->ammo_required() > 0 );
    Character &c = get_avatar();
    CHECK( g->ammo_sufficient( &c, 3 ) == ( g->shots_remaining( get_map(), &c ) >= 3 ) );
    g->ammo_set( itype_id( "556" ), 0 );
    CHECK_FALSE( g->ammo_sufficient( &c ) );
}

TEST_CASE( "tool_uses_remaining_legacy_branch_uses_shots_remaining", "[p3][ammo][3-4d]" ) {
    clear_all_state();
    detached_ptr<item> tool = item::spawn( itype_id( "smart_phone_music" ) );
    detached_ptr<item> battery = item::spawn( itype_id( "light_battery_cell" ) );
    battery->ammo_unset();
    item *bat = &*battery;
    tool->put_in( std::move( battery ) );
    REQUIRE( tool->magazine_current() == bat );
    bat->ammo_set( itype_id( "battery" ), 10 );
    Character &c = get_avatar();
    CHECK( tool->tool_uses_remaining( get_map(), &c ) == tool->shots_remaining( get_map(), &c ) );
}

// 8-1d-0: magazine_current and energy_consume to the DDA shape (ADV-16 Q-15-1).
TEST_CASE( "heavy_cell_in_power_well_drains_through_gun", "[p3][ammo][8-1d-0]" ) {
    clear_all_state();
    item *cell = nullptr;
    detached_ptr<item> gun = make_energy_block_gun( cell, 100 );
    REQUIRE( gun->energy_remaining( nullptr ) == 100_kJ );
    units::energy used = gun->energy_consume( 20_kJ, tripoint_bub_ms::zero(), nullptr );
    CHECK( used == 20_kJ );
    CHECK( gun->energy_remaining( nullptr ) == 80_kJ );
}

TEST_CASE( "heavy_cell_over_drain_stops_at_empty", "[p3][ammo][8-1d-0]" ) {
    clear_all_state();
    item *cell = nullptr;
    detached_ptr<item> gun = make_energy_block_gun( cell, 100 );
    REQUIRE( gun->energy_remaining( nullptr ) == 100_kJ );
    units::energy used = gun->energy_consume( 500_kJ, tripoint_bub_ms::zero(), nullptr );
    CHECK( used == 100_kJ );
    CHECK( gun->energy_remaining( nullptr ) == 0_kJ );
}

TEST_CASE( "empty_power_well_consumes_nothing", "[p3][ammo][8-1d-0]" ) {
    clear_all_state();
    item *cell = nullptr;
    detached_ptr<item> gun = make_energy_block_gun( cell, 0 );
    units::energy used = gun->energy_consume( 5_kJ, tripoint_bub_ms::zero(), nullptr );
    CHECK( used == 0_kJ );
}

TEST_CASE( "loaded_plain_magazine_is_not_drained", "[p3][ammo][8-1d-0]" ) {
    clear_all_state();
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    item *mag_ptr = &*mag;
    REQUIRE( gun->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    units::energy used = gun->energy_consume( 5_kJ, tripoint_bub_ms::zero(), nullptr );
    CHECK( used == 0_kJ );
    CHECK( mag_ptr->ammo_remaining() == 15 );
}

// CDF: pins DDA's first-loaded-well behaviour (energy_consume drains the first loaded well only, while energy_remaining sums all wells); delete if that DDA quirk is changed.
TEST_CASE( "magazine_current_returns_first_loaded_well", "[p3][ammo][8-1d-0]" ) {
    clear_all_state();
    item *cell = nullptr;
    detached_ptr<item> gun = make_energy_block_gun( cell, 100 );
    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    REQUIRE( gun->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->magazine_current() != nullptr );
    CHECK( gun->magazine_current()->typeId() == itype_id( "glockmag" ) );
}

// 8-4e: Character::invoke_item method path (4-arg overload), consume_charges needs-charges gate
// and the has_fire firestarter predicate. DDA test names for the invoke path are not in the
// packet (brief open question 6); the names below are CDF proposals.
TEST_CASE( "Character_invoke_item_method_checks_method_ammo", "[p3][ammo][8-4e]" ) {
    clear_all_state();
    // CDF: toolset is the USES_BIONIC_POWER tool (data/json/items/fake.json:32-40), first use_action HAMMER
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    REQUIRE( toolset->has_flag( flag_USES_BIONIC_POWER ) );
    toolset->charges = 0;

    standard_npc who( "carrier" );
    who.set_max_power_level( 100_kJ );
    who.set_power_level( 0_kJ );
    who.set_moves( 50 );

    const bool used = who.invoke_item( &*toolset, "HAMMER", who.bub_pos(), 123 );
    CHECK_FALSE( used );
    CHECK( who.get_moves() == 123 );
}

namespace
{
// CDF: test-only use_function so a multimag tool (no use_action in TEST_DATA) has a method
// that reports one charge used.
auto test_8_4e_use( player *, item *, bool, const tripoint_bub_ms & ) -> int {
    return 1;
}
} // namespace

TEST_CASE( "Character_invoke_item_multimag_routes_to_activation_consume", "[p3][ammo][8-4e]" ) {
    clear_all_state();
    Character &chr = get_player_character();
    item &carried = chr.i_add( make_multimag_consume_tool( 12, 30 ) );
    REQUIRE( carried.uses_firing_requirements() );

    // CDF: inject a method on the shared itype for this case only; removed again by the guard
    itype &ity = const_cast<itype &>( *carried.type );
    const std::string method = "TEST_8_4E_USE";
    ity.use_methods.emplace( method, use_function( method, test_8_4e_use ) );
    struct method_guard {
        itype &t;
        std::string m;
        ~method_guard() {
            t.use_methods.erase( m );
        }
    } guard{ ity, method };

    REQUIRE( carried.ammo_sufficient( &chr, method ) );
    const bool result = chr.invoke_item( &carried, method, chr.bub_pos(), chr.get_moves() );
    // DDA returns false on the firing-requirements route (brief open question 1)
    CHECK_FALSE( result );
    // consumption_per_use: well_a 2, well_b 1, one charge used
    CHECK( carried.ammo_remaining_in_pocket( "well_a" ) == 10 );
    CHECK( carried.ammo_remaining_in_pocket( "well_b" ) == 29 );
}

TEST_CASE( "Character_consume_charges_gate_detaches_tool_without_charge_need", "[p3][ammo][8-4e]" ) {
    clear_all_state();
    clear_avatar();
    Character &chr = get_player_character();
    // CDF: horn_bicycle is a TOOL with no ammo, no charges_per_use and no firing requirements
    const itype_id tool_id( "horn_bicycle" );
    item &it = chr.i_add( item::spawn( tool_id ) );
    REQUIRE( it.is_tool() );
    CHECK( !it.needs_charges_to_use() );
    REQUIRE( chr.has_amount( tool_id, 1 ) );

    const bool destroyed = chr.consume_charges( it, 1 );
    CHECK( destroyed );
    CHECK_FALSE( chr.has_amount( tool_id, 1 ) );
}

TEST_CASE( "Character_has_fire_counts_firestarter_ammo_sufficient", "[p3][ammo][8-4e]" ) {
    clear_all_state();
    clear_avatar();
    Character &chr = get_player_character();
    // CDF: lighter is a FIRESTARTER tool (data/json/items/tool/fire.json:82-99) taking the
    // "lighter_fluid" ammotype. "lighter_fluid" is an ammotype, not an item: the item is chem_butane
    // (fuel.json), and passing the ammotype to ammo_set made item_factory register a bogus
    // empty itype that leaked into item_length_sanity_check.
    item &lighter = chr.i_add( item::spawn( itype_id( "lighter" ) ) );
    REQUIRE( lighter.has_flag( flag_FIRESTARTER ) );

    // no fuel: not sufficient, so no fire
    lighter.ammo_set( itype_id( "chem_butane" ), 0 );
    REQUIRE_FALSE( lighter.ammo_sufficient( &chr, 1 ) );
    CHECK_FALSE( chr.has_fire( 1 ) );

    // fuelled: sufficient, so fire
    lighter.ammo_set( itype_id( "chem_butane" ), 5 );
    REQUIRE( lighter.ammo_sufficient( &chr, 1 ) );
    CHECK( chr.has_fire( 1 ) );
}
