#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "state_helpers.h"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "map/map.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"


// In JSON, "battery" is both an "ammunition_type" (ammo_types.json) and an "AMMO" (ammo.json)
static const ammotype ammo_battery( "battery" );


static const itype_id itype_diving_flashlight_small_hipower( "diving_flashlight_small_hipower" );
static const itype_id itype_light_battery_cell( "light_battery_cell" );
static const itype_id itype_medium_battery_cell( "medium_battery_cell" );



TEST_CASE( "installing_battery_in_tool", "[p3][battery][tool][install]" )
{
    item bat_cell( itype_light_battery_cell );
    item flashlight( itype_diving_flashlight_small_hipower );

    const int bat_charges = bat_cell.ammo_capacity( ammo_battery );
    REQUIRE( bat_charges > 0 );

    SECTION( "flashlight with no battery installed" ) {
        REQUIRE( !flashlight.magazine_current() );

        CHECK( flashlight.ammo_remaining( ) == 0 );
        CHECK( flashlight.ammo_capacity( ammo_battery ) == 0 );
        CHECK( flashlight.remaining_ammo_capacity() == 0 );
    }

    SECTION( "dead battery installed in flashlight" ) {
        // Ensure battery is dead
        bat_cell.ammo_set( bat_cell.ammo_default(), 0 );
        REQUIRE( bat_cell.ammo_remaining( ) == 0 );

        // Put battery in flashlight
        REQUIRE( flashlight.has_pocket_type( pocket_type::MAGAZINE_WELL ) );
        ret_val<void> result = flashlight.put_in( bat_cell, pocket_type::MAGAZINE_WELL );
        CHECK( result.success() );
        CHECK( flashlight.magazine_current() );

        // No remaining ammo
        CHECK( flashlight.ammo_remaining( ) == 0 );
    }

    SECTION( "charged battery installed in flashlight" ) {
        // Charge the battery
        bat_cell.ammo_set( bat_cell.ammo_default(), bat_charges );
        REQUIRE( bat_cell.ammo_remaining( ) == bat_charges );

        // Put battery in flashlight
        REQUIRE( flashlight.has_pocket_type( pocket_type::MAGAZINE_WELL ) );
        ret_val<void> result = flashlight.put_in( bat_cell, pocket_type::MAGAZINE_WELL );
        CHECK( result.success() );
        CHECK( flashlight.magazine_current() );

        // Flashlight has a full charge
        CHECK( flashlight.ammo_remaining( ) == bat_charges );
    }

    SECTION( "wrong size battery for flashlight" ) {
        item med_bat_cell( itype_medium_battery_cell );

        // Should fail to install the magazine
        REQUIRE( flashlight.has_pocket_type( pocket_type::MAGAZINE_WELL ) );
        std::string dmsg = capture_debugmsg_during( [&flashlight, &med_bat_cell]() {
            ret_val<void> result = flashlight.put_in( med_bat_cell, pocket_type::MAGAZINE_WELL );
            CHECK_FALSE( result.success() );
        } );
        CHECK_THAT( dmsg,
                    Catch::Matchers::EndsWith( "holster does not accept this item type or form factor" ) );
        CHECK_FALSE( flashlight.magazine_current() );
    }
}
