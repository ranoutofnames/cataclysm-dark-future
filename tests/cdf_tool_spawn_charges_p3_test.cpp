// CDF: P3 FIXC regression. P3-L1C (01e7256b6f) converted hotplate, soldering_iron and welder to DDA
// link_up and removed their battery wells, so they now have no MAGAZINE pocket. Spawning such a tool
// with a quantity went through item.cpp's spawn constructor `type->tool` branch, which called ammo_set()
// and logged "Tried to set ammo battery[N] without suitable magazine". DDA item.cpp:262-265 only sets
// ammo when there is ammo to set; the CDF guard now also requires the magazine target ammo_set() needs.

#include <string>

#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "type_id.h"

TEST_CASE( "pocketless_battery_tool_spawn_with_charges_logs_nothing", "[p3][charges][spawn]" )
{
    GIVEN( "battery tools with no battery well or MAGAZINE pocket, as after the P3-L1C link_up conversion" ) {
        for( const char *id : { "hotplate", "soldering_iron", "welder" } ) {
            CAPTURE( id );
            std::string dmsg;
            detached_ptr<item> tool;
            dmsg = capture_debugmsg_during( [&]() {
                tool = item::spawn( itype_id( id ), calendar::start_of_cataclysm, 20 );
            } );
            CHECK( dmsg.empty() );
            REQUIRE( tool );
            CHECK( tool->ammo_types().size() > 0 );
        }
    }
}
