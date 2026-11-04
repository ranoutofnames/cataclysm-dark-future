#include "catch/catch.hpp"
#include "item.h"
#include "item_group.h"
#include "map/map.h"
#include "map_helpers.h"
#include "coordinates.h"
#include "state_helpers.h"
#include "type_id.h"

#include <utility>
#include <vector>

// 4-c-Ja: a group entry's `charges` on a tool goes to the magazine or pocket (DDA item_group.cpp:642-656),
// never into item::charges, except for charge-only tools.
// CDF: capacities below are re-derived from CDF data: gasoline_lantern max_charges 500
// (data/json/items/tool/lighting.json), medium_battery_cell capacity 5000 (rolled [0,500] always fits) (data/json/items/battery.json),
// gps_device takes the light battery cells.

namespace {

auto spawn_from( const char *group ) -> detached_ptr<item> {
    std::vector<detached_ptr<item>> items = item_group::items_from( item_group_id( group ) );
    REQUIRE( items.size() == 1 );
    return std::move( items.front() );
}

} // namespace

TEST_CASE( "group_charges_on_a_pocket_tool_go_to_the_pocket", "[p3][charges][item_group]" ) {
    clear_all_state();
    detached_ptr<item> lantern = spawn_from( "test_charges_lantern_120_p3" );
    REQUIRE( lantern->typeId() == itype_id( "gasoline_lantern" ) );
    CHECK( lantern->ammo_remaining() == 120 );
    CHECK( lantern->charges == 0 );
}

TEST_CASE( "group_charges_on_a_charge_only_tool_stay_direct", "[p3][charges][item_group]" ) {
    clear_all_state();
    detached_ptr<item> candle = spawn_from( "test_charges_candle_10_p3" );
    REQUIRE( candle->typeId() == itype_id( "candle" ) );
    REQUIRE( candle->ammo_types().empty() );
    CHECK( candle->charges == 10 );
}

TEST_CASE( "group_charges_are_capped_by_the_pocket", "[p3][charges][item_group]" ) {
    clear_all_state();
    detached_ptr<item> lantern = spawn_from( "test_charges_lantern_9999_p3" );
    CHECK( lantern->ammo_remaining() == 500 );
    CHECK( lantern->charges == 0 );
}

TEST_CASE( "group_charges_load_a_magazine_into_a_magazine_tool", "[p3][charges][item_group]" ) {
    clear_all_state();
    detached_ptr<item> gps = spawn_from( "test_charges_gps_5_p3" );
    REQUIRE( gps->typeId() == itype_id( "gps_device" ) );
    REQUIRE_FALSE( gps->magazine_integral() );
    CHECK( gps->magazine_current() != nullptr );
    CHECK( gps->ammo_remaining() == 5 );
    CHECK( gps->charges == 0 );
}

// B1: a well TOOL_ARMOR (helmet_eod shape) with charges [0, 500] used to spawn an empty cell.
TEST_CASE( "group_charges_fill_the_well_of_a_tool_armor", "[p3][charges][item_group]" ) {
    clear_all_state();
    SECTION( "fixed amount" ) {
        detached_ptr<item> helmet = spawn_from( "test_charges_helmet_fixed_p3" );
        REQUIRE( helmet->typeId() == itype_id( "test_helmet_eod_p3" ) );
        REQUIRE( helmet->magazine_current() != nullptr );
        CHECK( helmet->ammo_remaining() == 300 );
        CHECK( helmet->charges == 0 );
    }
    SECTION( "amount above the cell capacity is capped" ) {
        detached_ptr<item> helmet = spawn_from( "test_charges_helmet_over_p3" );
        REQUIRE( helmet->magazine_current() != nullptr );
        // CDF: capacity comes from the cell (medium_battery_cell holds 5000 here), not a literal
        const int cap = helmet->magazine_current()->ammo_capacity();
        REQUIRE( cap > 0 );
        REQUIRE( cap < 9999 );
        CHECK( helmet->ammo_remaining() == cap );
    }
    SECTION( "rolled amount in [0, 500] is the cell content" ) {
        bool any_nonzero = false;
        for( int i = 0; i < 50; i++ ) {
            detached_ptr<item> helmet = spawn_from( "test_charges_helmet_p3" );
            const int left = helmet->ammo_remaining();
            CHECK( left >= 0 );
            CHECK( left <= 500 );
            if( left > 0 ) {
                any_nonzero = true;
                REQUIRE( helmet->magazine_current() != nullptr );
                CHECK( helmet->magazine_current()->ammo_remaining() == left );
            }
        }
        // 50 rolls of rng(0,500) are all zero with probability 501^-50
        CHECK( any_nonzero );
    }
}

// G1: documents item.cpp stacks_with S0274; tools with different pocket ammo do not stack.
TEST_CASE( "tools_with_different_pocket_ammo_do_not_stack", "[p3][charges][item]" ) {
    clear_all_state();
    detached_ptr<item> a = item::spawn( itype_id( "gasoline_lantern" ) );
    detached_ptr<item> b = item::spawn( itype_id( "gasoline_lantern" ) );
    detached_ptr<item> c = item::spawn( itype_id( "gasoline_lantern" ) );
    a->ammo_set( itype_id( "gasoline" ), 100 );
    b->ammo_set( itype_id( "gasoline" ), 500 );
    c->ammo_set( itype_id( "gasoline" ), 500 );
    CHECK_FALSE( a->stacks_with( *b ) );
    CHECK_FALSE( b->stacks_with( *a ) );
    CHECK( b->stacks_with( *c ) );
}

// G2: documents map::spawn_an_item S0434/S0435; charges passed to spawn_item do not reach a pocket tool.
TEST_CASE( "map_spawn_charges_do_not_reach_a_pocket_tool", "[p3][charges][map]" ) {
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();
    here.spawn_item( pos, "gasoline_lantern", 1, 50 );
    bool found = false;
    for( item * const it : here.i_at( pos ) ) {
        if( it->typeId() == itype_id( "gasoline_lantern" ) ) {
            found = true;
            // CDF: gasoline_lantern spawns full from initial_charges 500 (see cdf_item_ammo_p3_test.cpp)
            CHECK( it->ammo_remaining() == 500 );
        }
    }
    CHECK( found );
}
