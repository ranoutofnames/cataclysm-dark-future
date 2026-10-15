// DDA source: tests/item_test.cpp (DDA commit b4b62254ae2), cases item_length_sanity_check and
// corpse_length_sanity_check. Translated for CDF (TT1-TT8): items are heap objects, so samples are
// spawned through item::spawn and corpses through item::make_corpse (detached_ptr).
#include <cmath>
#include <string>

#include "calendar.h"
#include "catch/catch.hpp"
#include "enums.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "math_defines.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "type_id.h"
#include "units.h"

static void assert_minimum_length_to_volume_ratio( const item &target )
{
    if( target.type->get_id().is_null() ) {
        return;
    }
    CAPTURE( target.type->get_id() );
    CAPTURE( target.volume() );
    CAPTURE( target.base_volume() );
    CAPTURE( target.type->volume );
    if( target.made_of( phase_id::LIQUID ) || target.is_soft() ) {
        CHECK( target.length() == 0_mm );
        return;
    }
    if( target.volume() == 0_ml ) {
        CHECK( target.length() == -1_mm );
        return;
    }
    if( target.volume() == 1_ml ) {
        CHECK( target.length() >= 0_mm );
        return;
    }
    // Minimum possible length is if the item is a sphere.
    const float minimal_diameter = std::cbrt( ( 3.0 * units::to_milliliter( target.base_volume() ) ) /
                                   ( 4.0 * M_PI ) );
    CHECK( units::to_millimeter( target.length() ) >= minimal_diameter * 10.0 );
}

TEST_CASE( "item_length_sanity_check", "[item]" )
{
    for( const itype *type : item_controller->all() ) {
        detached_ptr<item> sample = item::spawn( type, calendar::turn_zero, item::solitary_tag {} );
        assert_minimum_length_to_volume_ratio( *sample );
    }
}

TEST_CASE( "corpse_length_sanity_check", "[item]" )
{
    for( const mtype &type : MonsterGenerator::generator().get_all_mtypes() ) {
        INFO( type.id.str() )
        detached_ptr<item> sample = item::make_corpse( type.id );
        assert_minimum_length_to_volume_ratio( *sample );
    }
}
