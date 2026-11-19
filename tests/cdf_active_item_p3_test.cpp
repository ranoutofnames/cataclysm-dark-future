#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "map/map.h"
#include "map_helpers.h"
#include "itype.h"
#include "state_helpers.h"
#include "type_id.h"
#include "itype_slots/islot_tool.h"
#include "itype_slots/islot_ammo.h"

#include <utility>

// Ported from DDA tests/active_item_test.cpp:82-110 (b4b6225).
TEST_CASE( "non_energy_tool_power_consumption_rate", "[p3][active_item]" )
{
    clear_all_state();
    // Gasoline lantern without a battery, using gasoline instead
    // CDF: items are spawned detached
    detached_ptr<item> lantern = item::spawn( itype_id( "gasoline_lantern_on" ) );
    const itype_id &default_ammo_id = lantern->ammo_default();
    const itype_id &default_ammo = default_ammo_id;
    // CDF: ammo_capacity takes the ammotype; read it from the ammo item type
    const int ammo_capacity = lantern->ammo_capacity( default_ammo->ammo->type );
    lantern->ammo_set( default_ammo, ammo_capacity );
    REQUIRE_FALSE( lantern->uses_energy() );

    lantern->activate(); // CDF: active is private
    // CDF: an item has to live somewhere (a map tile) while it is processed,
    // otherwise ammo_consume has no position; park it on the map between turns.
    map &here = get_map();
    const tripoint_bub_ms pos{ 60, 60, 0 };
    here.i_clear( pos );
    item *test_lantern = lantern.get();
    here.add_item( pos, std::move( lantern ) );

    // Now process the tool until it runs out of fuel.
    int seconds_active = 0;
    // Must be captured before it's inactive and transforms
    int turns_per_charge = test_lantern->type->tool->turns_per_charge;
    REQUIRE( test_lantern->ammo_remaining( ) == ammo_capacity );
    do {
        calendar::turn += 1_seconds;
        // CDF: process takes and returns ownership (activate=false: do not invoke the use action)
        detached_ptr<item> held = test_lantern->detach();
        held = item::process( std::move( held ), nullptr, pos, false, 1 );
        REQUIRE( held );
        test_lantern = held.get();
        here.add_item( pos, std::move( held ) );
        seconds_active++;
    } while( test_lantern->is_active() );
    REQUIRE( test_lantern->ammo_remaining( ) == 0 );
    // Runtime vaguely in the bounds we expect.
    CHECK( seconds_active > ( turns_per_charge - 1 ) * ammo_capacity );
    CHECK( seconds_active < ( turns_per_charge + 1 ) * ammo_capacity );
}
