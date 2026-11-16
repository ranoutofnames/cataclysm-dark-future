#include "avatar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "itype.h"
#include "iuse.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>

// CDF: LINKUP1a regression. BN's itype::tick (src/itype.cpp:186-194) calls every use method with
// t = true while an item is active. DDA's link_up actor has no tick parameter (DDA
// iuse_actor.cpp:4928) and DDA's itype::tick only walks tick_action (DDA itype.cpp:259-266), so
// the ported actor must ignore a tick call. Without the guard an active link_up item (the
// smartphone flashlight app is one: data/json/items/tool/electronics.json) opened the cable menu
// "What to do with the smartphone's cable?" every turn.
namespace
{

// A menu opened during a test is reported by uilist::query (src/ui.cpp:1046-1049).
auto opened_a_menu( const std::string &dmsg ) -> bool
{
    return dmsg.find( "Tried to open UI in test mode" ) != std::string::npos;
}

} // namespace

// A tick call on the actor is not a player action: it must return without a menu.
TEST_CASE( "link_up_actor_does_not_open_a_menu_on_a_tick", "[link][linkup1a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item welder( itype_id( "test_linkup_welder" ) );
    REQUIRE( welder.can_link_up() );
    const use_function *use = welder.type->get_use( "link_up" );
    REQUIRE( use != nullptr );

    int result = -1;
    const std::string dmsg = capture_debugmsg_during( [&]() {
        result = use->call( you, welder, /*active=*/true, you.bub_pos() );
    } );
    CHECK( result == 0 );
    CHECK_FALSE( opened_a_menu( dmsg ) );
}

// The real path: process_tool() ticks an active tool through itype::tick, which reaches link_up.
TEST_CASE( "active_link_up_tool_ticks_without_the_link_menu", "[link][linkup1a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item welder( itype_id( "test_linkup_welder" ) );
    welder.activate();
    REQUIRE( welder.is_active() );

    const std::string dmsg = capture_debugmsg_during( [&]() {
        welder.type->tick( you, welder, you.bub_pos() );
    } );
    CHECK_FALSE( opened_a_menu( dmsg ) );
}
