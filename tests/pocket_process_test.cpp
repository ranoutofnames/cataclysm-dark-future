// Pocket process behaviour from PL-64 G13 (leak) and G37 (pocket spoil_multiplier), ported from
// DDA pin b4b62254ae2: item_pocket::leak (item_pocket.cpp:2014-2047) and the spoil multiplier
// that item_pocket::process passes down (item_pocket.cpp:1997-2007, applied in item_degrade.cpp).
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "weather/weather.h"

#include <cstdlib>
#include <functional>

namespace
{

// CDF: TEST_DATA/dda_pocket_items.json test_utility_belt, pocket "P8" (micro cooler)
constexpr float pocket_preserve_multiplier = 0.5f;

auto container_pocket( item &it, const std::function<bool( const item_pocket & )> &filter )
-> item_pocket *
{
    for( item_pocket *pocket : it.get_container_pockets() ) {
        if( filter( *pocket ) ) {
            return pocket;
        }
    }
    return nullptr;
}

auto liquid_on_ground( const tripoint_bub_ms &pos, const itype_id &id ) -> int
{
    int charges = 0;
    for( const item *it : get_map().i_at( pos ) ) {
        if( it->typeId() == id ) {
            charges += it->charges;
        }
    }
    return charges;
}

} // namespace

// G13: DDA item_pocket.cpp:2014-2047, item_contents.cpp:2766-2774, item_container.cpp:894-910.
// Liquid in a non-watertight pocket drips onto the ground when leak() runs.
TEST_CASE( "non-watertight pocket leaks its liquid", "[pocket][fm][leak]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );
    here.i_clear( origin );

    detached_ptr<item> box = item::spawn( itype_id( "test_box" ) );
    item_pocket *pocket = container_pocket( *box, []( const item_pocket & p ) {
        return !p.watertight();
    } );
    REQUIRE( pocket != nullptr );

    // Unchecked insert: a non-watertight pocket refuses liquids through insert_item().
    pocket->add( item::spawn( itype_id( "test_liquid_1ml" ), calendar::turn, 5 ) );
    REQUIRE( pocket->size() == 1 );

    box->leak( here, nullptr, origin, nullptr );

    CHECK( pocket->empty() );
    CHECK( liquid_on_ground( origin, itype_id( "test_liquid_1ml" ) ) == 5 );
}

// G13: a watertight pocket keeps the liquid (DDA item::leak returns false for it).
TEST_CASE( "watertight pocket does not leak", "[pocket][fm][leak]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );
    here.i_clear( origin );

    detached_ptr<item> can = item::spawn( itype_id( "test_watertight_open_sealed_container_250ml" ) );
    item_pocket *pocket = container_pocket( *can, []( const item_pocket & p ) {
        return p.watertight();
    } );
    REQUIRE( pocket != nullptr );

    pocket->add( item::spawn( itype_id( "test_liquid_1ml" ), calendar::turn, 5 ) );
    REQUIRE( pocket->size() == 1 );

    can->leak( here, nullptr, origin, nullptr );

    CHECK( pocket->size() == 1 );
    CHECK( pocket->front().typeId() == itype_id( "test_liquid_1ml" ) );
    CHECK( pocket->front().charges == 5 );
    CHECK( liquid_on_ground( origin, itype_id( "test_liquid_1ml" ) ) == 0 );
}

// G37: DDA item_pocket.cpp:1997-2007 takes the lowest spoil_multiplier on the way down and
// item_degrade.cpp scales the rot gained by it. Meat in the 0.5 pocket rots at half the rate.
TEST_CASE( "food in a spoil_multiplier pocket rots slower", "[pocket][fm][rot]" )
{
    clear_all_state();
    calendar::turn = calendar::start_of_cataclysm + 91_days;
    weather_manager weather;
    const tripoint_bub_ms pos( 60, 60, 0 );

    detached_ptr<item> belt = item::spawn( itype_id( "test_utility_belt" ) );
    item_pocket *cooler = container_pocket( *belt, []( const item_pocket & p ) {
        return p.get_data().spoil_multiplier == pocket_preserve_multiplier;
    } );
    REQUIRE( cooler != nullptr );
    REQUIRE( cooler->insert_item( item::spawn( itype_id( "meat_cooked" ) ) ).success() );

    detached_ptr<item> loose = item::spawn( itype_id( "meat_cooked" ) );

    calendar::turn += 1_hours;
    belt = item::process( std::move( belt ), nullptr, pos, false,
                          temperature_flag::TEMP_HEATER, weather, 1 );
    loose = item::process( std::move( loose ), nullptr, pos, false,
                           temperature_flag::TEMP_HEATER, weather, 1 );

    // The food stays in the belt; find it in case processing re-homed it to another pocket.
    const item *contained = nullptr;
    for( const item *it : belt->contents.all_items_ptr() ) {
        if( it->typeId() == itype_id( "meat_cooked" ) ) {
            contained = it;
            break;
        }
    }
    REQUIRE( contained != nullptr );
    const int pocket_rot = to_turns<int>( contained->get_rot() );
    const int loose_rot = to_turns<int>( loose->get_rot() );

    CHECK( pocket_rot > 0 );
    CHECK( pocket_rot < loose_rot );
    // CDF: time_duration keeps whole turns, so allow the one-turn rounding of the halving.
    CHECK( std::abs( pocket_rot * 2 - loose_rot ) <= 1 );
}
