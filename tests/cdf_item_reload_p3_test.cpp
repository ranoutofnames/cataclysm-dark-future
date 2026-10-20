#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "type_id.h"
#include "player_helpers.h"

#include <string>

// CDF: P3-3-6; ported from DDA tests/multimag_test.cpp:626-693.
TEST_CASE( "item_reload_with_pocket_index_targets_specific_well", "[p3][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    // Multi-well gun with only the first (glock) well loaded; the stanag
    // well stays empty so item::reload(stanag_idx) has somewhere to insert.
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "test_multimag_gun" ) );
    detached_ptr<item> glock_mag = item::spawn( itype_id( "glockmag" ) );
    glock_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun_ptr->put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );

    // CDF: i_add returns item &, not an item_location.
    item &gun = you.i_add( std::move( gun_ptr ) );

    // Identify the two well indices (glock-loaded and empty-stanag) in gun.
    int loaded_well_idx = -1;
    int empty_well_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets(
        []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( p->magazine_current() != nullptr ) {
                    loaded_well_idx = idx;
                } else {
                    empty_well_idx = idx;
                }
            }
            ++idx;
        }
    }
    REQUIRE( loaded_well_idx >= 0 );
    REQUIRE( empty_well_idx >= 0 );

    SECTION( "pocket_index = empty stanag well inserts there, leaves glock untouched" ) {
        detached_ptr<item> replacement_stanag = item::spawn( itype_id( "stanag30" ) );
        replacement_stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ), pocket_type::MAGAZINE );
        item &replacement = you.i_add( std::move( replacement_stanag ) );

        const int glock_count_before = gun.ammo_remaining( loaded_well_idx );
        REQUIRE( gun.reload( you, replacement, 1, empty_well_idx ) );
        CHECK( gun.ammo_remaining( empty_well_idx ) == 10 );
        CHECK( gun.ammo_remaining( loaded_well_idx ) == glock_count_before );
    }

    SECTION( "invalid pocket_index aborts reload" ) {
        detached_ptr<item> replacement_stanag = item::spawn( itype_id( "stanag30" ) );
        replacement_stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ), pocket_type::MAGAZINE );
        item &replacement = you.i_add( std::move( replacement_stanag ) );

        const int glock_count_before = gun.ammo_remaining( loaded_well_idx );
        bool reload_result = true;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            // Out-of-range index. Aborts with a debugmsg, which we capture
            // so it does not flag the test as failed.
            reload_result = gun.reload( you, replacement, 1, 99 );
        } );
        CHECK( !reload_result );
        CHECK( dmsg.find( "pocket_index" ) != std::string::npos );
        CHECK( gun.ammo_remaining( loaded_well_idx ) == glock_count_before );
        CHECK( gun.ammo_remaining( empty_well_idx ) == 0 );
    }
}

// CDF: P3-3-6b; ported from DDA item_gun_tool_ammo.cpp:4453-4491 (item::is_reloadable).
TEST_CASE( "item_is_reloadable_magazine_well", "[p3][reload][magazine]" )
{
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "test_multimag_gun" ) );
    item &gun = *gun_ptr;
    SECTION( "both wells empty" ) {
        CHECK( gun.is_reloadable() );
    }
    SECTION( "one full well keeps the empty well reloadable, both full does not" ) {
        // Full magazines: glockmag holds 15 of 9mm, stanag30 holds 30 of 556.
        detached_ptr<item> glock_mag = item::spawn( itype_id( "glockmag" ) );
        glock_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
        REQUIRE( glock_mag->contents.is_magazine_full() );
        gun.put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );
        CHECK( gun.is_reloadable() );

        detached_ptr<item> stanag_mag = item::spawn( itype_id( "stanag30" ) );
        stanag_mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ), pocket_type::MAGAZINE );
        REQUIRE( stanag_mag->contents.is_magazine_full() );
        gun.put_in( std::move( stanag_mag ), pocket_type::MAGAZINE_WELL );
        int full_wells = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & pk ) {
        return pk.is_type( pocket_type::MAGAZINE_WELL );
    } ) ) {
            REQUIRE( !p->empty() );
            if( p->front().contents.is_magazine_full() ) {
                ++full_wells;
            }
        }
        REQUIRE( full_wells == 2 );
        CHECK_FALSE( gun.is_reloadable() );
    }
}

TEST_CASE( "item_is_reloadable_integral_magazine", "[p3][reload][magazine]" )
{
    detached_ptr<item> mag_ptr = item::spawn( itype_id( "glockmag" ) );
    item &mag = *mag_ptr;
    SECTION( "empty" ) {
        CHECK( mag.is_reloadable() );
    }
    SECTION( "full" ) {
        mag.put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
        REQUIRE( mag.remaining_ammo_capacity() == 0 );
        CHECK_FALSE( mag.is_reloadable() );
    }
}

TEST_CASE( "item_is_reloadable_gunmod", "[p3][reload][magazine]" )
{
    // Host: m4a1 (one well, a MOD pocket) with a full stanag30, so it has no reloadable pocket.
    // CDF: not pipe_pistol: it is a legacy direct-charges gun (D14, cdf_legacy_direct_charges),
    // reloadable before the mod is fitted.
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "m4a1" ) );
    item &gun = *gun_ptr;
    detached_ptr<item> stanag_mag = item::spawn( itype_id( "stanag30" ) );
    stanag_mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ), pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( std::move( stanag_mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.gunmods().empty() );
    REQUIRE_FALSE( gun.is_reloadable() );

    detached_ptr<item> mod = item::spawn( itype_id( "test_multimag_aux_gun" ) );
    REQUIRE( gun.put_in( std::move( mod ), pocket_type::MOD ).success() );
    REQUIRE( gun.gunmods().size() == 1 );
    // only the mod's empty well can make the host reloadable
    CHECK( gun.gunmods().front()->is_reloadable() );
    CHECK( gun.is_reloadable() );
}

TEST_CASE( "item_is_reloadable_legacy_charges_gun", "[p3][reload][magazine]" )
{
    // CDF: legacy direct-charges gun (pipe_pistol: ammo 38, no magazine pocket) stays reloadable.
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "pipe_pistol" ) );
    CHECK( gun_ptr->is_reloadable() );
}

TEST_CASE( "item_is_reloadable_watertight_container", "[p3][reload][magazine]" )
{
    detached_ptr<item> jug_ptr = item::spawn( itype_id( "test_watertight_open_sealed_container_1L" ) );
    item &jug = *jug_ptr;
    SECTION( "empty" ) {
        CHECK( jug.is_reloadable() );
    }
    SECTION( "holding a non-liquid" ) {
        REQUIRE( jug.put_in( item::spawn( itype_id( "rock" ) ), pocket_type::CONTAINER ).success() );
        CHECK_FALSE( jug.is_reloadable() );
    }
}

// CDF: P3-3-6b; DDA item_gun_tool_ammo.cpp:4476 skips a CONTAINER pocket that is not watertight.
TEST_CASE( "item_is_reloadable_non_watertight_container", "[p3][reload][magazine]" )
{
    detached_ptr<item> bag_ptr = item::spawn( itype_id( "backpack" ) );
    item &bag = *bag_ptr;
    REQUIRE( bag.contents.has_pocket_type( pocket_type::CONTAINER ) );
    CHECK_FALSE( bag.is_reloadable() );
}

TEST_CASE( "item_gun_tool_ammo_no_default_ammo_not_reloadable", "[p3][reload][magazine]" )
{
    // ammo: [ "NULL" ] and no pocket_data, so ammo_default() is the NULL id
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "debug_modular_m4_carbine" ) );
    item &gun = *gun_ptr;
    REQUIRE( gun.is_gun() );
    CHECK_FALSE( gun.is_reloadable() );
}
