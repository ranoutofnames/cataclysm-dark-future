#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character_functions.h"
#include "coordinates.h"
#include "distribution_grid.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "overmap/overmapbuffer.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "visitable.h"

#include <algorithm>
#include <string>
#include <vector>

#include "overmap/overmap.h"

// P3-4-c-Jb: after 4-b2 a tool keeps its rounds in an integral MAGAZINE pocket and `charges`
// stays 0. The character and inventory lookups must read ammo_remaining(), not charges.

namespace {

auto contains_item( const std::vector<item *> &v, const item *target ) -> bool {
    return std::ranges::find( v, target ) != v.end();
}

} // namespace

// CDF: gasoline_lantern spawns with 500 gasoline in its pocket (cdf_item_ammo_p3_test.cpp).
TEST_CASE( "id_lookups_see_pocket_ammo", "[p3][charges][character]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    clear_avatar();
    const itype_id lantern_id( "gasoline_lantern" );
    item &lantern = u.i_add( item::spawn( lantern_id, calendar::turn ) );
    REQUIRE( lantern.charges == 0 );
    REQUIRE( lantern.ammo_remaining() == 500 );

    SECTION( "full lantern is found with need_charges" ) {
        CHECK( u.has_item_with_id( lantern_id, true ) );
        CHECK( u.get_item_with_id( lantern_id, true ) != nullptr );
        CHECK( u.all_items_with_id( lantern_id, true ).size() == 1 );
        CHECK( contains_item( u.all_items( true ), &lantern ) );
    }
    SECTION( "empty lantern is NOT found with need_charges" ) {
        lantern.ammo_unset();
        REQUIRE( lantern.ammo_remaining() == 0 );
        CHECK_FALSE( u.has_item_with_id( lantern_id, true ) );
        CHECK( u.get_item_with_id( lantern_id, true ) == nullptr );
        CHECK( u.all_items_with_id( lantern_id, true ).empty() );
        CHECK_FALSE( contains_item( u.all_items( true ), &lantern ) );
        // without need_charges the empty lantern is still there
        CHECK( u.has_item_with_id( lantern_id, false ) );
        CHECK( u.get_item_with_id( lantern_id, false ) != nullptr );
        CHECK( u.all_items_with_id( lantern_id, false ).size() == 1 );
        CHECK( contains_item( u.all_items( false ), &lantern ) );
    }
}

TEST_CASE( "flag_lookups_see_pocket_ammo", "[p3][charges][character]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    clear_avatar();
    const flag_id flag( "WATERPROOF" );
    item &lantern = u.i_add( item::spawn( itype_id( "gasoline_lantern" ), calendar::turn ) );
    lantern.set_flag( flag );
    REQUIRE( lantern.ammo_remaining() == 500 );

    SECTION( "full flagged lantern is found" ) {
        CHECK( u.has_item_with_flag( flag, true ) );
        CHECK( contains_item( u.all_items_with_flag( flag, true ), &lantern ) );
    }
    SECTION( "empty flagged lantern is NOT found with need_charges" ) {
        lantern.ammo_unset();
        CHECK_FALSE( u.has_item_with_flag( flag, true ) );
        CHECK_FALSE( contains_item( u.all_items_with_flag( flag, true ), &lantern ) );
        CHECK( u.has_item_with_flag( flag, false ) );
        CHECK( contains_item( u.all_items_with_flag( flag, false ), &lantern ) );
    }
}

TEST_CASE( "wielded_container_shows_the_tool_ammo", "[p3][charges][character]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    clear_avatar();

    SECTION( "bag holding one full lantern" ) {
        detached_ptr<item> bag = item::spawn( itype_id( "backpack" ), calendar::turn );
        bag->put_in( item::spawn( itype_id( "gasoline_lantern" ), calendar::turn ) );
        REQUIRE( bag->contents.num_item_stacks() == 1 );
        REQUIRE( !u.wield( std::move( bag ) ) );
        const std::string line = character_funcs::fmt_wielded_weapon( u );
        CHECK( line.find( "(500)" ) != std::string::npos );
    }
    SECTION( "jug holding 3 water (count by charges unchanged)" ) {
        detached_ptr<item> jug = item::spawn( itype_id( "jug_plastic" ), calendar::turn );
        jug->put_in( item::spawn( itype_id( "water" ), calendar::turn, 3 ) );
        REQUIRE( jug->contents.num_item_stacks() == 1 );
        REQUIRE( !u.wield( std::move( jug ) ) );
        const std::string line = character_funcs::fmt_wielded_weapon( u );
        CHECK( line.find( "(3)" ) != std::string::npos );
        CHECK( line.find( "(0)" ) == std::string::npos );
    }
}

TEST_CASE( "furniture_tool_gets_tile_ammo_in_its_pocket", "[p3][charges][inventory]" ) {
    clear_all_state();
    map &m = get_map();
    avatar &u = get_avatar();
    clear_avatar();
    constexpr tripoint_bub_ms start_pos( 60, 60, 0 );
    u.setpos( start_pos );
    const tripoint_bub_ms furn_pos = start_pos + point_east;
    m.furn_set( furn_pos, furn_str_id( "f_char_purifier" ) );
    REQUIRE( m.furn( furn_pos ) == furn_str_id( "f_char_purifier" ) );

    // CDF: char_purifier_fake takes charcoal, max 500 (furniture-tools.json).
    const auto find_purifier = [&]() -> const item * {
        const item *found = nullptr;
        u.crafting_inventory().visit_items( [&]( const item * it ) {
            if( it->typeId() == itype_id( "char_purifier_fake" ) ) {
                found = it;
                return VisitResponse::ABORT;
            }
            return VisitResponse::NEXT;
        } );
        return found;
    };

    SECTION( "12 charcoal on the tile" ) {
        m.add_item( furn_pos, item::spawn( itype_id( "charcoal" ), calendar::turn, 12 ) );
        u.invalidate_crafting_inventory();
        const item *purifier = find_purifier();
        REQUIRE( purifier != nullptr );
        CHECK( purifier->ammo_remaining() == 12 );
        CHECK( purifier->charges == 0 );
    }
    SECTION( "no charcoal on the tile" ) {
        u.invalidate_crafting_inventory();
        const item *purifier = find_purifier();
        REQUIRE( purifier != nullptr );
        CHECK( purifier->ammo_remaining() == 0 );
        CHECK( purifier->charges == 0 );
    }
}

// Guard: grid fake tools have no ammo type and keep `charges` (S0254, no edit).
TEST_CASE( "grid_tool_keeps_charges", "[p3][charges][inventory][grids]" ) {
    clear_all_state();
    map &m = get_map();
    avatar &u = get_avatar();
    constexpr tripoint_bub_ms start_pos( 60, 60, 0 );
    const tripoint_abs_ms start_pos_abs( map_local_to_abs( m, start_pos ) );
    u.setpos( start_pos );
    clear_avatar();
    auto om = ACTIVE_OVERMAP_BUFFER.get_om_global( u.abs_omt_pos() );
    om.om->set_electric_grid_connections( om.local, {} );

    m.furn_set( start_pos + point( 10, 0 ), furn_str_id( "f_battery" ) );
    m.furn_set( start_pos + point_east, furn_str_id( "f_oven" ) );
    distribution_grid_tracker &tracker = get_distribution_grid_tracker();
    distribution_grid &grid = tracker.grid_at( start_pos_abs + point( 10, 0 ) );
    REQUIRE( !grid.empty() );
    REQUIRE( &grid == &tracker.grid_at( start_pos_abs + point_east ) );
    grid.mod_resource( 10 );
    REQUIRE( grid.get_resource() == 10 );
    u.invalidate_crafting_inventory();
    CHECK( u.crafting_inventory().charges_of( itype_id( "fake_oven" ), 10 ) >= 10 );
}

// Guard: reduce_charges on count-by-charges items is untouched (S0075-S0078, no edit).
TEST_CASE( "reduce_charges_still_splits_count_by_charges", "[p3][charges][character]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    clear_avatar();
    detached_ptr<item> jug = item::spawn( itype_id( "jug_plastic" ), calendar::turn );
    jug->put_in( item::spawn( itype_id( "water" ), calendar::turn, 10 ) );
    item &carried_jug = u.i_add( std::move( jug ) );
    item *water = &carried_jug.contents.front();
    REQUIRE( water->charges == 10 );
    detached_ptr<item> taken = u.reduce_charges( water, 4 );
    REQUIRE( taken );
    CHECK( taken->charges == 4 );
    CHECK( water->charges == 6 );
}
