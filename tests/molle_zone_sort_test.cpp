#include "catch/catch.hpp"

#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "clzones.h"
#include "coordinates.h"
#include "item.h"
#include "item_contents.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>
#include <vector>

// MOLLE2 step 4: the unload step of the "sort out loot" activity for the UNLOAD_ALL zone
// (DDA activity_item_handling.cpp unload_item, set_unload_options). The sort activity is driven
// through activity_on_turn_move_loot, the function the ACT_MOVE_LOOT turn handler calls.

namespace
{
const zone_type_id zone_unload_all( "UNLOAD_ALL" );
const zone_type_id zone_unsorted( "LOOT_UNSORTED" );
const zone_type_id zone_loot_custom( "LOOT_CUSTOM" );
const activity_id act_move_loot( "ACT_MOVE_LOOT" );

void add_zone( const zone_type_id &type, const tripoint_bub_ms &where,
               const std::string &options_json = "" )
{
    shared_ptr_fast<zone_options> options = zone_options::create( type );
    if( !options_json.empty() ) {
        std::istringstream in( options_json );
        JsonIn jsin( in );
        const JsonObject jo = jsin.get_object();
        options->deserialize( jo );
    }
    const tripoint_abs_ms abs = bub_to_abs( where );
    zone_manager::get_manager().add( "molle2 test", type, your_fac, false, true, abs, abs, options );
    zone_manager::get_manager().cache_data();
}

// run the sort activity until it ends, a few hundred turns at most
void run_sort( avatar &you )
{
    you.assign_activity( act_move_loot );
    for( int turn = 0; turn < 300 && you.has_activity( act_move_loot ); ++turn ) {
        you.set_moves( 100 );
        activity_on_turn_move_loot( *you.activity, you );
    }
}

auto count_on( const tripoint_bub_ms &where, const itype_id &type ) -> int
{
    int found = 0;
    for( item *it : get_map().i_at( where ) ) {
        if( it->typeId() == type ) {
            found += it->count();
        }
    }
    return found;
}

auto content_count( const item &carrier ) -> int
{
    return static_cast<int>( carrier.contents.all_items_top().size() );
}

auto pouch_count( const item &carrier ) -> int
{
    return static_cast<int>( carrier.contents.get_added_pockets().size() );
}

struct sort_scene {
    avatar &you = get_avatar();
    tripoint_bub_ms src;
    tripoint_bub_ms dest;

    sort_scene() {
        clear_all_state();
        clear_map();
        zone_manager::reset_manager();
        clear_character( you, true );
        src = you.bub_pos();
        dest = tripoint_bub_ms( src.x() + 3, src.y(), src.z() );
    }
};
} // namespace

TEST_CASE( "unload_zone_dumps_contents", "[molle][zone]" )
{
    sort_scene scene;
    detached_ptr<item> bag = item::spawn( itype_id( "backpack_hiking" ), calendar::turn );
    // CDF: three different item types, so that no sparse rule can interfere
    const std::vector<itype_id> inside = { itype_id( "rock" ), itype_id( "rag" ), itype_id( "nail" ) };
    for( const itype_id &type : inside ) {
        REQUIRE( bag->put_in( item::spawn( type, calendar::turn ), pocket_type::CONTAINER ).success() );
    }
    REQUIRE( content_count( *bag ) == 3 );
    item &bag_ref = *bag;
    get_map().add_item_or_charges( scene.src, std::move( bag ) );
    add_zone( zone_unsorted, scene.src );

    SECTION( "no other zone: the bag is emptied onto its own tile" ) {
        add_zone( zone_unload_all, scene.src, R"({"always_unload":false})" );
        run_sort( scene.you );
        CHECK( content_count( bag_ref ) == 0 );
        for( const itype_id &type : inside ) {
            CHECK( count_on( scene.src, type ) >= 1 );
        }
        CHECK( count_on( scene.src, itype_id( "backpack_hiking" ) ) == 1 );
    }

    SECTION( "a zone that takes the bag: not unloaded unless always_unload" ) {
        add_zone( zone_unload_all, scene.src, R"({"always_unload":false})" );
        add_zone( zone_loot_custom, scene.dest, R"({"mark":"backpack"})" );
        run_sort( scene.you );
        // the bag went to its zone with everything inside
        CHECK( count_on( scene.dest, itype_id( "backpack_hiking" ) ) == 1 );
        CHECK( content_count( bag_ref ) == 3 );
        CHECK( count_on( scene.src, itype_id( "rock" ) ) == 0 );
    }

    SECTION( "always_unload empties it even so" ) {
        add_zone( zone_unload_all, scene.src, R"({"always_unload":true})" );
        add_zone( zone_loot_custom, scene.dest, R"({"mark":"backpack"})" );
        run_sort( scene.you );
        CHECK( content_count( bag_ref ) == 0 );
        for( const itype_id &type : inside ) {
            CHECK( count_on( scene.src, type ) >= 1 );
        }
    }
}

TEST_CASE( "unload_zone_sparse_threshold", "[molle][zone]" )
{
    sort_scene scene;
    detached_ptr<item> bag = item::spawn( itype_id( "backpack_hiking" ), calendar::turn );
    // CDF: 6 pliers (over the threshold of 5), 5 screwdrivers (not over it), one stack of nails (charges)
    for( int i = 0; i < 6; ++i ) {
        REQUIRE( bag->put_in( item::spawn( itype_id( "pliers" ), calendar::turn ),
                              pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( item::spawn( itype_id( "pliers" ), calendar::turn )->count_by_charges() );
    }
    for( int i = 0; i < 5; ++i ) {
        REQUIRE( bag->put_in( item::spawn( itype_id( "screwdriver" ), calendar::turn ),
                              pocket_type::CONTAINER ).success() );
    }
    detached_ptr<item> nails = item::spawn( itype_id( "nail" ), calendar::turn, 50 );
    REQUIRE( nails->count_by_charges() );
    REQUIRE( bag->put_in( std::move( nails ), pocket_type::CONTAINER ).success() );
    item &bag_ref = *bag;
    get_map().add_item_or_charges( scene.src, std::move( bag ) );
    add_zone( zone_unsorted, scene.src );
    add_zone( zone_unload_all, scene.src, R"({"sparse_only":true,"sparse_threshold":5})" );

    run_sort( scene.you );

    int pliers_inside = 0;
    for( const item *it : bag_ref.contents.all_items_top() ) {
        if( it->typeId() == itype_id( "pliers" ) ) {
            ++pliers_inside;
        }
    }
    CHECK( pliers_inside == 6 );
    CHECK( count_on( scene.src, itype_id( "pliers" ) ) == 0 );
    CHECK( count_on( scene.src, itype_id( "screwdriver" ) ) == 5 );
    CHECK( count_on( scene.src, itype_id( "nail" ) ) == 50 );
}

TEST_CASE( "unload_zone_molle_option", "[molle][zone]" )
{
    sort_scene scene;
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    // CDF: two chestpouches, as in molle_test.cpp, each holding a magazine
    for( int i = 0; i < 2; ++i ) {
        detached_ptr<item> pouch = item::spawn( itype_id( "chestpouch" ), calendar::turn );
        pouch->put_in( item::spawn( itype_id( "stanag20" ), calendar::turn ) );
        vest->contents.add_pocket( std::move( pouch ) );
    }
    REQUIRE( pouch_count( *vest ) == 2 );
    item &vest_ref = *vest;
    get_map().add_item_or_charges( scene.src, std::move( vest ) );
    add_zone( zone_unsorted, scene.src );

    SECTION( "molle off: nothing is detached" ) {
        add_zone( zone_unload_all, scene.src, R"({"molle":false})" );
        run_sort( scene.you );
        CHECK( pouch_count( vest_ref ) == 2 );
        CHECK( count_on( scene.src, itype_id( "chestpouch" ) ) == 0 );
    }

    SECTION( "molle on: both pouches lie on the tile with their contents" ) {
        add_zone( zone_unload_all, scene.src, R"({"molle":true})" );
        run_sort( scene.you );
        CHECK( pouch_count( vest_ref ) == 0 );
        CHECK( vest_ref.contents.get_additional_space_used() == 0 );
        REQUIRE( count_on( scene.src, itype_id( "chestpouch" ) ) == 2 );
        for( item *it : get_map().i_at( scene.src ) ) {
            if( it->typeId() == itype_id( "chestpouch" ) ) {
                CHECK( content_count( *it ) == 1 );
            }
        }
        // the webbing is free again: attaching works
        vest_ref.contents.add_pocket( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        CHECK( pouch_count( vest_ref ) == 1 );
    }
}
