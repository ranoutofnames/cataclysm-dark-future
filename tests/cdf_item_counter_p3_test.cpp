#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "examine_item_menu.h"
#include "explosion.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "state_helpers.h"
#include "units.h"

#include <sstream>
#include <string>
#include <vector>

// CDF: DDA emp_blast (explosion.cpp:750-906) has no battery drain; CDF drains
// the ammo count instead of the direct charges, so a magazine well drains too.
TEST_CASE( "emp_blast drains a battery tool's magazine well", "[p3][ammo]" ) {
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    detached_ptr<item> tool = item::spawn( itype_id( "test_battery_tool" ) );
    tool->ammo_set( itype_id( "battery" ), 40 );
    here.add_item( pos, std::move( tool ) );

    REQUIRE( here.i_at( pos ).size() == 1 );
    REQUIRE( here.i_at( pos ).only_item().ammo_remaining() == 40 );

    explosion_handler::emp_blast( pos );

    CHECK( here.i_at( pos ).only_item().ammo_remaining() == 0 );
}
