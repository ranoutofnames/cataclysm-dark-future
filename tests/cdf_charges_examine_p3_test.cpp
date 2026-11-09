#include "catch/catch.hpp"

#include "avatar.h"
#include "calendar.h"
#include "debug.h"
#include "iexamine.h"
#include "item.h"
#include "map/map.h"
#include "map_helpers.h"
#include "messages.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>

// 4-c-Jc1a: examine sites that read tool rounds after 4-b2 moved them into a pocket.
// CDF: BN-only (use_furn_fake_item has no DDA counterpart); sign's filter fixes DDA's
// `it.charges > 0` (iexamine.cpp:5082).

namespace
{

// CDF: charcoal stacks on the tile are count-by-charges, so the pile size is the sum of charges.
auto charcoal_on( const tripoint_bub_ms &pos ) -> int
{
    int sum = 0;
    for( const item * const it : get_map().i_at( pos ) ) {
        if( it->typeId() == itype_id( "charcoal" ) ) {
            sum += it->charges;
        }
    }
    return sum;
}

} // namespace

// CDF: test_fake_noise_tool (TEST_DATA/charges_jc1a_p3.json) uses 3 charcoal per use.
TEST_CASE( "furn_fake_item_draws_its_ammo_from_the_tile", "[iexamine][charges][p3]" )
{
    clear_all_state();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms pos = u.bub_pos() + point_east;
    here.furn_set( pos, furn_id( "f_test_fake_tool" ) );
    here.add_item( pos, item::spawn( itype_id( "charcoal" ), calendar::turn, 12 ) );
    REQUIRE( charcoal_on( pos ) == 12 );

    iexamine::use_furn_fake_item( u, pos );

    CHECK( charcoal_on( pos ) == 9 );
}

TEST_CASE( "furn_fake_item_without_ammo_on_the_tile_does_nothing", "[iexamine][charges][p3]" )
{
    clear_all_state();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms pos = u.bub_pos() + point_east;
    here.furn_set( pos, furn_id( "f_test_fake_tool" ) );
    REQUIRE( charcoal_on( pos ) == 0 );

    Messages::clear_messages();
    const std::string captured = capture_debugmsg_during( [&]() {
        iexamine::use_furn_fake_item( u, pos );
    } );

    CHECK( captured.empty() );
    CHECK( charcoal_on( pos ) == 0 );
    // CDF: bites a fake tool built with rounds the tile does not have (the use message appears
    // only if the tool ran); the tool's own guards are shadowed, see the 4-c-Jc1b plan.
    for( const auto &msg : Messages::recent_messages( 0 ) ) {
        INFO( "message: " << msg.second );
        CHECK( msg.second.find( "You use the test tool." ) == std::string::npos );
    }
}

TEST_CASE( "sign_writing_tool_filter_counts_pocket_ammo", "[iexamine][charges][p3]" )
{
    clear_all_state();

    // CDF: spray_can has WRITE_MESSAGE and ammo spray_paint (tool/misc.json), so its paint lives
    // in a pocket since 4-b2 and `charges` stays 0.
    detached_ptr<item> spray = item::spawn( itype_id( "spray_can" ) );
    REQUIRE( spray->ammo_remaining() > 0 );
    CHECK( iexamine::can_write_with( *spray ) );
    spray->ammo_unset();
    REQUIRE( spray->ammo_remaining() == 0 );
    CHECK_FALSE( iexamine::can_write_with( *spray ) );

    // CDF: pen is WRITE_MESSAGE and charge-only (no ammo type), ammo_remaining() returns charges.
    detached_ptr<item> pen = item::spawn( itype_id( "pen" ) );
    REQUIRE( pen->ammo_remaining() > 0 );
    CHECK( iexamine::can_write_with( *pen ) );
    pen->charges = 0;
    CHECK_FALSE( iexamine::can_write_with( *pen ) );
}
