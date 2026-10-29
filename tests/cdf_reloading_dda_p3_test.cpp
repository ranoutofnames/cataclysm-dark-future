#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <memory>
#include <utility>

static const flag_id json_flag_CASING( "CASING" );

static const itype_id itype_40_casing( "40_casing" );
static const itype_id itype_40_speedloader6( "40_speedloader6" );
static const itype_id itype_40sw( "40sw" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_backpack( "backpack" );
// CDF: DDA sw_610 has an integral magazine pocket; use the TEST_DATA copy.
static const itype_id itype_sw_610( "p3_sw_610" );

// CDF: P3-9-5d; ported from DDA tests/reloading_test.cpp:514-637 (b4b6225), helper test_reloading :67-101.
// CDF: items are detached_ptr-owned; the avatar takes ownership of a copy, so `target`/`ammo` stay untouched.
// CDF: item has no is_magazine_full(); the checks below use item_contents::is_magazine_full.
static void test_reloading( item &target, item &ammo, bool expect_success = true )
{
    avatar &dummy = get_avatar();

    clear_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( itype_backpack, calendar::turn_zero ) );

    dummy.i_add( item::spawn( ammo ) );
    // CDF: snapshot of the target before the reload, to compare contents afterwards.
    detached_ptr<item> before = item::spawn( target );
    dummy.set_primary_weapon( item::spawn( target ) );

    // CDF: avatar_action::reload_wielded instead of g->reload_wielded.
    avatar_action::reload_wielded( false );

    INFO( "Tried to reload " << target.tname() << " with " << ammo.tname() );

    if( expect_success ) {
        CHECK( dummy.activity );
    } else {
        CHECK( !dummy.activity );
    }

    process_activity( dummy );

    // Quick and dirty check to see if anything was actually reloaded.
    // Doesn't check if right thing went to right place. But something did go somewhere
    bool contents_changed = !before->get_contents().same_contents( dummy.primary_weapon().get_contents() );
    if( expect_success ) {
        CHECK( contents_changed );
    } else {
        CHECK( !contents_changed );
    }
}

// CDF: spawn helper for ammo stacks.
static detached_ptr<item> rounds( const itype_id &id, int n )
{
    return item::spawn( id, calendar::turn, n );
}

TEST_CASE( "speedloader_reloading", "[p3][reload]" )
{
    clear_all_state();

    SECTION( "empty gun" ) {
        detached_ptr<item> gun_owner = item::spawn( itype_sw_610 );
        item &gun = *gun_owner;
        SECTION( "empty speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;

            test_reloading( gun, speedloader, false );
        }

        SECTION( "partially empty speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.put_in( rounds( itype_40sw, 3 ), pocket_type::MAGAZINE );

            REQUIRE( !speedloader.empty() );
            REQUIRE( !speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader );
        }

        SECTION( "full speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.put_in( rounds( itype_40sw, 6 ), pocket_type::MAGAZINE );

            REQUIRE( speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader );
        }

        SECTION( "speedloader with wrong ammo" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.force_insert_item( rounds( itype_9mm, 6 ), pocket_type::MAGAZINE );

            REQUIRE( speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader, false );
        }
    }

    SECTION( "full gun" ) {
        detached_ptr<item> gun_owner = item::spawn( itype_sw_610 );
        item &gun = *gun_owner;
        gun.put_in( rounds( itype_40sw, 6 ), pocket_type::MAGAZINE );
        REQUIRE( gun.get_contents().is_magazine_full() );

        SECTION( "empty speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;

            test_reloading( gun, speedloader, false );
        }

        SECTION( "partially empty speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.put_in( rounds( itype_40sw, 3 ), pocket_type::MAGAZINE );

            REQUIRE( !speedloader.empty() );
            REQUIRE( !speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader, false );
        }

        SECTION( "full speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.put_in( rounds( itype_40sw, 6 ), pocket_type::MAGAZINE );

            REQUIRE( speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader, false );
        }

        SECTION( "speedloader with wrong ammo" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.force_insert_item( rounds( itype_9mm, 6 ), pocket_type::MAGAZINE );

            REQUIRE( speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader, false );
        }
    }

    SECTION( "gun full of casings" ) {
        detached_ptr<item> gun_owner = item::spawn( itype_sw_610 );
        item &gun = *gun_owner;
        detached_ptr<item> casings = rounds( itype_40_casing, 6 );
        casings->set_flag( json_flag_CASING );
        gun.force_insert_item( std::move( casings ), pocket_type::MAGAZINE );

        SECTION( "empty speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;

            test_reloading( gun, speedloader, false );
        }

        SECTION( "partially empty speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.put_in( rounds( itype_40sw, 3 ), pocket_type::MAGAZINE );

            REQUIRE( !speedloader.empty() );
            REQUIRE( !speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader );
        }

        SECTION( "full speedloader" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.put_in( rounds( itype_40sw, 6 ), pocket_type::MAGAZINE );

            REQUIRE( speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader );
        }

        SECTION( "speedloader with wrong ammo" ) {
            detached_ptr<item> speedloader_owner = item::spawn( itype_40_speedloader6 );
        item &speedloader = *speedloader_owner;
            speedloader.force_insert_item( rounds( itype_9mm, 6 ), pocket_type::MAGAZINE );

            REQUIRE( speedloader.get_contents().is_magazine_full() );

            test_reloading( gun, speedloader, false );
        }
    }
}
