// DDA source: tests/item_pickup_test.cpp (DDA commit b4b62254ae2), P1-transplant J4.
// TEST_CASEs copied (4): putting_items_into_inventory_with_put_in_or_i_add,
// pickup_m4_with_a_rope_in_a_hiking_backpack, charged_item_pickup_full_merge_no_debugmsg,
// charged_item_pickup_partial_merge_no_debugmsg
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md):
//  * item_location handles are item references; map::add_item takes a detached item (TT3).
//  * Character::pick_up + process_activity goes through CDF's pickup entry point, the pickup
//    activity (assign_pickup_activity in player_helpers); what ends where and "no debugmsg" stay.
//  * T11: CDF moves items where DDA copies them (wear_item, put_in, i_add, pick_up). DDA's
//    "the original is not in inventory" checks become "the original is the item in inventory";
//    each such place is marked "CDF (T11)".
#include <climits>
#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "debug.h"
#include "detached_ptr.h"
#include "item.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"

static const itype_id itype_backpack_hiking( "backpack_hiking" );

static const itype_id itype_debug_modular_m4_carbine( "debug_modular_m4_carbine" );

static const itype_id itype_nail( "nail" );

static const itype_id itype_rope_6( "test_rope_6" );

// Puts an item on the map and returns it; the item stays where it was spawned (it is a heap
// object, so the reference stays valid when it is moved in). Charge stacks may merge.
static item &spawn_on_ground( map &here, const tripoint_bub_ms &where, detached_ptr<item> &&it )
{
    item &ref = *it;
    here.add_item( where, std::move( it ) );
    return ref;
}

// wear_item returns the item if it was not worn
static bool wear_it( avatar &they, detached_ptr<item> &&it )
{
    return !they.wear_item( std::move( it ), false );
}

static int ground_count_of( map &here, const tripoint_bub_ms &pos, const itype_id &type )
{
    int total = 0;
    for( const item *it : here.i_at( pos ) ) {
        if( it->typeId() == type ) {
            total++;
        }
    }
    return total;
}

// This test case exists by way of documenting and exhibiting some potentially unexpected behavior
// of the following functions for transferring items into inventory:
//
// - Character::wear_item
// - Character::pick_up
// - Character::i_add
// - item::put_in
//
// namely, that in DDA these functions create *copies* of the items, and the original item
// references will not refer to the items placed in inventory. CDF (T11) moves the item
// instead: the original reference IS the item that ends up in inventory.
TEST_CASE( "putting_items_into_inventory_with_put_in_or_i_add", "[pickup][inventory]" )
{
    avatar &they = get_avatar();
    map &here = get_map();
    clear_all_state();
    // TT4: a bare character, no DEBUG_STORAGE
    clear_character( they, false );
    clear_map();

    // Spawn items on the map at this location
    const tripoint_bub_ms ground = they.bub_pos();
    item &rope_map = spawn_on_ground( here, ground, item::spawn( itype_rope_6 ) );
    item &backpack_map = spawn_on_ground( here, ground, item::spawn( itype_backpack_hiking ) );

    // Set unique IDs on the items, to verify them later
    const std::string backpack_uid = random_string( 10 );
    const std::string rope_uid = random_string( 10 );
    backpack_map.set_var( "uid", backpack_uid );
    rope_map.set_var( "uid", rope_uid );

    // Ensure avatar does not currently possess these items, or items with their uid
    REQUIRE_FALSE( they.has_item( backpack_map ) );
    REQUIRE_FALSE( they.has_item( rope_map ) );
    REQUIRE_FALSE( character_has_item_with_var_val( they, "uid", backpack_uid ) );
    REQUIRE_FALSE( character_has_item_with_var_val( they, "uid", rope_uid ) );

    WHEN( "avatar wears a hiking backpack from the ground with wear_item" ) {
        clear_worn( they );
        // CDF (T11): the backpack itself is worn, no copy
        item &backpack = backpack_map;
        REQUIRE( wear_it( they, backpack.detach() ) );

        THEN( "they have the backpack" ) {
            // They have the same type
            CHECK( backpack.typeId() == itype_backpack_hiking );
            // They have the same uid
            CHECK( backpack.get_var( "uid" ) == backpack_uid );
            CHECK( character_has_item_with_var_val( they, "uid", backpack_uid ) );
            // It is in avatar's possession
            CHECK( they.has_item( backpack ) );
            // CDF (T11): and no backpack is left on the ground (DDA: the ground one is not theirs)
            CHECK( ground_count_of( here, ground, itype_backpack_hiking ) == 0 );
        }

        WHEN( "using put_in to put a rope directly into the backpack" ) {
            REQUIRE( backpack.put_in( rope_map.detach(), pocket_type::CONTAINER ).success() );

            THEN( "the rope moved into the backpack" ) {
                // CDF (T11): DDA: the original rope is not in inventory or the backpack
                CHECK( they.has_item( rope_map ) );
                CHECK( backpack.has_item( rope_map ) );
                CHECK( ground_count_of( here, ground, itype_rope_6 ) == 0 );
            }
            THEN( "they have the rope in inventory" ) {
                CHECK( character_has_item_with_var_val( they, "uid", rope_uid ) );
            }
        }

        WHEN( "using i_add to put the rope into inventory" ) {
            // Add the rope to the inventory (goes in backpack, as it's the only thing worn)
            item &rope_new = they.i_add( rope_map.detach() );

            THEN( "the rope item is in inventory and in the backpack" ) {
                CHECK( they.has_item( rope_new ) );
                CHECK( backpack.has_item( rope_new ) );
                CHECK( character_has_item_with_var_val( they, "uid", rope_uid ) );
            }
            THEN( "the rope moved, none is left on the ground" ) {
                // CDF (T11): DDA: the original rope is not in inventory or the backpack
                CHECK( &rope_new == &rope_map );
                CHECK( ground_count_of( here, ground, itype_rope_6 ) == 0 );
            }
        }
    }

    // Picking up is a pickup activity, which can be executed with the process_activity() helper.
    // But picking up cannot wield or wear items in the act of picking them up;
    // the available storage needs to be worn ahead of time.
    GIVEN( "avatar is not wearing anything that can store items" ) {
        clear_worn( they );

        WHEN( "avatar tries to get the backpack with pick_up" ) {
            assign_pickup_activity( they, { &backpack_map }, { 1 } );
            process_activity( they );

            THEN( "they fail to acquire the backpack" ) {
                CHECK_FALSE( character_has_item_with_var_val( they, "uid", backpack_uid ) );
            }
        }

        WHEN( "avatar tries to get the rope with pick_up" ) {
            assign_pickup_activity( they, { &rope_map }, { 1 } );
            process_activity( they );

            THEN( "they fail to acquire the rope" ) {
                CHECK_FALSE( character_has_item_with_var_val( they, "uid", rope_uid ) );
            }
        }
    }
}

// The below incredibly-specific test case is designed as a regression test for #52422 in which
// picking up items from the ground could result in inventory items being dropped.
//
// One such case is when an "inner container" (container within a container) would be selected as
// the "best pocket" for a picked up item, but inserting the item makes it too big or heavy for its
// outer container, forcing it to be dropped on the ground (along with whatever was inserted).
//
// The reproduction use case here is: Wearing only a backpack containing a rope, when picking up
// an M4 from the ground, the M4 should go into the backpack, not into the rope, and neither the
// rope nor the M4 should be dropped.
TEST_CASE( "pickup_m4_with_a_rope_in_a_hiking_backpack", "[pickup][container]" )
{
    avatar &they = get_avatar();
    map &here = get_map();
    clear_all_state();
    clear_character( they, false );
    clear_map();

    // Spawn items on the map at this location
    const tripoint_bub_ms ground = they.bub_pos();
    item &m4a1 = spawn_on_ground( here, ground, item::spawn( itype_debug_modular_m4_carbine ) );
    item &rope_map = spawn_on_ground( here, ground, item::spawn( itype_rope_6 ) );
    item &backpack_map = spawn_on_ground( here, ground, item::spawn( itype_backpack_hiking ) );

    // Ensure that rope and backpack are containers, both capable of holding the M4
    // CDF: is_container() leaves out the worn-storage pockets of a garment (D1), so the
    // precondition for the backpack is stated as "has a container pocket".
    REQUIRE( rope_map.is_container() );
    REQUIRE_FALSE( backpack_map.get_container_pockets().empty() );
    REQUIRE( rope_map.can_contain( m4a1 ).success() );
    REQUIRE( backpack_map.can_contain( m4a1 ).success() );
    REQUIRE( backpack_map.can_contain( rope_map ).success() );

    // Give the M4 a serial number (and the rope too, it's also a deadly weapon)
    const std::string m4_uid = random_string( 10 );
    const std::string rope_uid = random_string( 10 );
    m4a1.set_var( "uid", m4_uid );
    rope_map.set_var( "uid", rope_uid );

    GIVEN( "avatar is wearing a backpack with a short rope in it" ) {
        // Wear the backpack from the map
        item &backpack = backpack_map;
        REQUIRE( wear_it( they, backpack.detach() ) );
        REQUIRE( they.has_item( backpack ) );
        // Put the rope in
        item &rope = they.i_add( rope_map.detach() );
        REQUIRE( they.has_item( rope ) );

        WHEN( "they pick up the M4" ) {
            CHECK_FALSE( backpack.has_item( m4a1 ) );
            // Now pick up the M4
            assign_pickup_activity( they, { &m4a1 }, { 1 } );
            process_activity( they );

            // Neither the rope nor the M4 should have been dropped
            THEN( "they should have the rope and M4 still in possession" ) {
                CHECK( character_has_item_with_var_val( they, "uid", rope_uid ) );
                CHECK( character_has_item_with_var_val( they, "uid", m4_uid ) );
            }
        }
    }
}

// Helper: count total charges of a given item type on a map tile
static int ground_charges_of( map &here, const tripoint_bub_ms &pos,
                              const itype_id &type )
{
    int total = 0;
    for( const item *it : here.i_at( pos ) ) {
        if( it->typeId() == type ) {
            total += it->charges;
        }
    }
    return total;
}

// Regression tests for #85439: charged-item pickup that merges into existing
// stacks must not call on_pickup() on the detached local copy in pick_one_up().
// The real items already get on_pickup() via item_pocket::fill_with().
TEST_CASE( "charged_item_pickup_full_merge_no_debugmsg", "[pickup][charges]" )
{
    avatar &they = get_avatar();
    map &here = get_map();
    clear_all_state();
    clear_character( they, false );
    clear_map();

    const tripoint_bub_ms ground = they.bub_pos();

    // Wear a backpack so we have somewhere to stash items
    REQUIRE( wear_it( they, item::spawn( itype_backpack_hiking ) ) );

    // Pre-seed inventory with some nails
    detached_ptr<item> seed_nails = item::spawn( itype_nail );
    seed_nails->charges = 10;
    they.i_add( std::move( seed_nails ) );

    const int inv_before = they.charges_of( itype_nail );
    REQUIRE( inv_before == 10 );

    // Prime inv_search_cache -- the bug only fires when cache entries exist
    they.has_item_with( []( const item & it ) {
        return it.typeId() == itype_nail;
    } );

    // Place more nails on the ground (small enough to fully absorb)
    detached_ptr<item> ground_nails = item::spawn( itype_nail );
    ground_nails->charges = 5;
    item &ground_nail_stack = spawn_on_ground( here, ground, std::move( ground_nails ) );

    const int ground_before = ground_charges_of( here, ground, itype_nail );
    REQUIRE( ground_before == 5 );
    const int total_before = inv_before + ground_before;

    // Build pickup list from items on the ground
    REQUIRE( here.i_at( ground ).size() == 1 );
    const std::vector<item *> to_pick_up = { &ground_nail_stack };

    const std::string dmsg = capture_debugmsg_during( [&]() {
        assign_pickup_activity( they, to_pick_up, { 0 } );
        process_activity( they );
    } );

    const int inv_after = they.charges_of( itype_nail );
    const int ground_after = ground_charges_of( here, ground, itype_nail );

    CHECK( dmsg.empty() );
    CHECK( inv_after + ground_after == total_before );
    CHECK( inv_after == 15 );
    CHECK( ground_after == 0 );
}

TEST_CASE( "charged_item_pickup_partial_merge_no_debugmsg", "[pickup][charges]" )
{
    avatar &they = get_avatar();
    map &here = get_map();
    clear_all_state();
    clear_character( they, false );
    clear_map();

    const tripoint_bub_ms ground = they.bub_pos();

    // Wear a backpack so we have somewhere to stash items
    REQUIRE( wear_it( they, item::spawn( itype_backpack_hiking ) ) );

    // Determine total nail capacity of the empty backpack
    detached_ptr<item> probe_holder = item::spawn( itype_nail );
    item &probe_nail = *probe_holder;
    probe_nail.charges = 1;
    int remaining = INT_MAX;
    they.can_stash_partial( probe_nail, remaining );
    const int total_capacity = INT_MAX - remaining;
    REQUIRE( total_capacity > 100 );

    // Fill backpack to (capacity - margin) via i_add_or_fill
    const int margin = 50;
    detached_ptr<item> prefill_nails = item::spawn( itype_nail );
    prefill_nails->charges = total_capacity - margin;
    they.i_add_or_fill( std::move( prefill_nails ), true, nullptr, nullptr,
                        /*allow_drop=*/false, /*allow_wield=*/false );

    const int inv_before = they.charges_of( itype_nail );
    REQUIRE( inv_before > 0 );

    // Prime inv_search_cache -- the bug only fires when cache entries exist
    they.has_item_with( []( const item & it ) {
        return it.typeId() == itype_nail;
    } );

    // Re-probe remaining capacity after prefill
    int post_fill_remaining = INT_MAX;
    they.can_stash_partial( probe_nail, post_fill_remaining );
    const int can_still_fit = INT_MAX - post_fill_remaining;

    // Place more nails on ground than remaining capacity
    const int ground_count = can_still_fit + 50;
    REQUIRE( can_still_fit > 0 );
    REQUIRE( ground_count > can_still_fit );
    const int expected_transferred = can_still_fit;
    const int expected_remaining = ground_count - expected_transferred;

    detached_ptr<item> ground_nails = item::spawn( itype_nail );
    ground_nails->charges = ground_count;
    item &ground_nail_stack = spawn_on_ground( here, ground, std::move( ground_nails ) );

    const int ground_before = ground_charges_of( here, ground, itype_nail );
    REQUIRE( ground_before == ground_count );

    // Build pickup list
    REQUIRE( here.i_at( ground ).size() == 1 );
    const std::vector<item *> to_pick_up = { &ground_nail_stack };

    const std::string dmsg = capture_debugmsg_during( [&]() {
        assign_pickup_activity( they, to_pick_up, { 0 } );
        process_activity( they );
    } );

    const int inv_after = they.charges_of( itype_nail );
    const int ground_after = ground_charges_of( here, ground, itype_nail );

    CHECK( dmsg.empty() );
    CHECK( inv_after == inv_before + expected_transferred );
    CHECK( ground_after == expected_remaining );
}
