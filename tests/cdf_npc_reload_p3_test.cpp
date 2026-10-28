// CDF P3 6-5: NPC reload selection skips an empty top source (DDA npc::select_ammo,
// npcmove.cpp:2833-2837).
#include "catch/catch.hpp"

#include "calendar.h"
#include "debug.h"
#include "item.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>

namespace
{
auto &setup_npc_with_gun( npc *&out_who, item *&out_gun )
{
    clear_all_state();
    npc &who = spawn_npc( {50, 50, 0}, "test_talker" );
    clear_character( who );
    item &gun = who.i_add( item::spawn( "glock_19", calendar::start_of_cataclysm, 0 ) );
    REQUIRE( who.wield( gun ) );
    out_who = &who;
    out_gun = &who.primary_weapon();
    return who;
}
} // namespace

TEST_CASE( "npc_reload_skips_empty_top_source", "[p3][npc]" )
{
    npc *who = nullptr;
    item *gun = nullptr;
    setup_npc_with_gun( who, gun );
    REQUIRE( gun->is_gun() );
    REQUIRE( gun->ammo_remaining() == 0 );
    item &mag = who->i_add( item::spawn( "glockmag", calendar::start_of_cataclysm, 0 ) );
    REQUIRE( mag.ammo_remaining() == 0 );

    CHECK( who->find_usable_ammo( *gun ) == nullptr );
    CHECK_FALSE( who->can_reload_current() );

    // The empty magazine must be treated as no source at all: do_reload reports "no usable
    // ammo" (DDA npc::do_reload) instead of trying to reload with it, and spends nothing.
    const int moves_before = who->moves;
    const std::string msg = capture_debugmsg_during( [&]() {
        who->do_reload( *gun );
    } );
    CHECK( msg.find( "no usable ammo" ) != std::string::npos );
    CHECK( gun->ammo_remaining() == 0 );
    CHECK( who->moves == moves_before );
}

TEST_CASE( "npc_reload_uses_loaded_source", "[p3][npc]" )
{
    npc *who = nullptr;
    item *gun = nullptr;
    setup_npc_with_gun( who, gun );
    who->i_add( item::spawn( "glockmag", calendar::start_of_cataclysm, 0 ) );
    item &loaded = who->i_add( item::spawn( "glockmag", calendar::start_of_cataclysm, 0 ) );
    loaded.ammo_set( itype_id( "9mm" ), 15 );
    REQUIRE( loaded.ammo_remaining() == 15 );

    CHECK( who->find_usable_ammo( *gun ) == &loaded );
    CHECK( who->can_reload_current() );
}

TEST_CASE( "npc_reload_direct_no_activity", "[p3][npc]" )
{
    npc *who = nullptr;
    item *gun = nullptr;
    setup_npc_with_gun( who, gun );
    item &loaded = who->i_add( item::spawn( "glockmag", calendar::start_of_cataclysm, 0 ) );
    loaded.ammo_set( itype_id( "9mm" ), 15 );
    REQUIRE( who->can_reload_current() );

    who->do_reload( *gun );

    CHECK_FALSE( who->activity );
    CHECK( gun->ammo_remaining() > 0 );
}
