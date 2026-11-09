#include "catch/catch.hpp"

#include "avatar.h"
#include "calendar.h"
#include "iuse.h"
#include "item.h"
#include "itype.h"
#include "iuse_actor.h"
#include "map/map.h"
#include "map_helpers.h"
#include "messages.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>

// 4-c-Jc1b: iuse readers of tool rounds after 4-b2 moved them into a pocket.
// DDA b4b62254ae2 iuse.cpp:1479, 1519, 1535-1560 (fish trap), iuse_actor.cpp:2334-2404 (fireweapon).
// CDF: DIVE_TANK has no DDA counterpart.

// CDF: fish_trap ammo is fish_bait, max_charges 20 (tool/fishing.json:16).
TEST_CASE( "fish_trap_without_bait_in_its_pocket_refuses_to_deploy", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> trap_owner = item::spawn( itype_id( "fish_trap" ) );
    item &trap = *trap_owner;
    trap.ammo_unset();
    REQUIRE( trap.ammo_remaining() == 0 );

    iuse::fish_trap( &u, &trap, false, u.bub_pos() );

    CHECK_FALSE( trap.is_active() );
}

TEST_CASE( "fish_trap_tick_stops_when_it_has_no_bait", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> trap_owner = item::spawn( itype_id( "fish_trap" ) );
    item &trap = *trap_owner;
    trap.ammo_unset();
    trap.activate();
    REQUIRE( trap.is_active() );

    iuse::fish_trap( &u, &trap, true, u.bub_pos() );

    CHECK_FALSE( trap.is_active() );
}

TEST_CASE( "fish_trap_tick_keeps_waiting_while_bait_is_loaded", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> trap_owner = item::spawn( itype_id( "fish_trap" ) );
    item &trap = *trap_owner;
    trap.ammo_set( itype_id( "fish_bait" ), 5 );
    REQUIRE( trap.ammo_remaining() == 5 );
    trap.activate();
    trap.set_age( 0_turns );

    iuse::fish_trap( &u, &trap, true, u.bub_pos() );

    CHECK( trap.is_active() );
    CHECK( trap.ammo_remaining() == 5 );
}

TEST_CASE( "fish_trap_tick_uses_up_the_bait", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.bub_pos() + point_east;
    // CDF: t_water_dp carries FISHABLE; the tick returns before using the bait on any other tile.
    here.ter_set( pos, ter_id( "t_water_dp" ) );
    REQUIRE( here.has_flag( "FISHABLE", pos ) );

    detached_ptr<item> trap_owner = item::spawn( itype_id( "fish_trap" ) );
    item &trap = *trap_owner;
    trap.ammo_set( itype_id( "fish_bait" ), 5 );
    REQUIRE( trap.ammo_remaining() == 5 );
    trap.activate();
    // set_birthday clamps to turn_zero, so the clock has to be past it for the trap to be 4 hours old
    calendar::turn = calendar::turn_zero + 1_days;
    trap.set_age( 4_hours );

    iuse::fish_trap( &u, &trap, true, pos );

    CHECK_FALSE( trap.is_active() );
    CHECK( trap.ammo_remaining() == 0 );
}

namespace
{

// CDF: scuba_tank and scuba_tank_on carry ammo nitrox, 60 max (tool_armor.json:2958-2990). The tick
// path treats an unworn tank as switched off, so each case wears the tank first.
auto wear_tank( avatar &u, const char *type, int air, bool active ) -> item &
{
    detached_ptr<item> tank = item::spawn( itype_id( type ) );
    item &ref = *tank;
    if( air > 0 ) {
        ref.ammo_set( itype_id( "nitrox" ), air );
    } else {
        ref.ammo_unset();
    }
    if( active ) {
        ref.activate();
    }
    REQUIRE_FALSE( u.wear_item( std::move( tank ), false ) );
    REQUIRE( u.is_worn( ref ) );
    REQUIRE( ref.typeId() == itype_id( type ) );
    return ref;
}

} // namespace

TEST_CASE( "dive_tank_refuses_to_start_empty", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    item &tank = wear_tank( u, "scuba_tank", 0, false );
    REQUIRE( tank.ammo_remaining() == 0 );

    iuse::dive_tank( &u, &tank, false, u.bub_pos() );

    CHECK( tank.typeId() == itype_id( "scuba_tank" ) );
    CHECK_FALSE( tank.is_active() );
}

TEST_CASE( "dive_tank_runs_with_air", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    item &tank = wear_tank( u, "scuba_tank", 10, false );
    REQUIRE( tank.ammo_remaining() == 10 );

    iuse::dive_tank( &u, &tank, false, u.bub_pos() );

    CHECK( tank.typeId() == itype_id( "scuba_tank_on" ) );
    CHECK( tank.is_active() );
}

TEST_CASE( "dive_tank_shuts_off_when_air_runs_out", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    item &tank = wear_tank( u, "scuba_tank_on", 0, true );
    REQUIRE( tank.ammo_remaining() == 0 );

    iuse::dive_tank( &u, &tank, true, u.bub_pos() );

    CHECK( tank.typeId() == itype_id( "scuba_tank" ) );
    CHECK_FALSE( tank.is_active() );
}

TEST_CASE( "dive_tank_tick_keeps_running_with_air", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    item &tank = wear_tank( u, "scuba_tank_on", 10, true );
    REQUIRE( tank.ammo_remaining() == 10 );

    iuse::dive_tank( &u, &tank, true, u.bub_pos() );

    CHECK( tank.typeId() == itype_id( "scuba_tank_on" ) );
    CHECK( tank.is_active() );
}

namespace
{

// CDF: firekatana_off/_on (swords_and_blades.json:2090-2145) use ammo gasoline and lighter_fluid,
// charges_per_use 1; the messages below are the item's own lacks_fuel and charges_extinguish text.
auto fireweapon_actor( const item &it, const char *method ) -> const iuse_actor &
{
    const iuse_actor *actor = it.type->use_methods.at( method ).get_actor_ptr();
    REQUIRE( actor != nullptr );
    return *actor;
}

} // namespace

TEST_CASE( "fireweapon_can_use_reads_the_pocket", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> katana = item::spawn( itype_id( "firekatana_off" ) );
    const iuse_actor &actor = fireweapon_actor( *katana, "fireweapon_off" );

    katana->ammo_unset();
    REQUIRE( katana->ammo_remaining() == 0 );
    CHECK_FALSE( actor.can_use( u, *katana, false, u.bub_pos() ).success() );

    katana->ammo_set( itype_id( "gasoline" ), 10 );
    REQUIRE( katana->ammo_remaining() == 10 );
    CHECK( actor.can_use( u, *katana, false, u.bub_pos() ).success() );
}

TEST_CASE( "fireweapon_off_use_reads_the_pocket", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> katana = item::spawn( itype_id( "firekatana_off" ) );
    const iuse_actor &actor = fireweapon_actor( *katana, "fireweapon_off" );

    // CDF: Messages is stubbed in the test binary, so the lacks-fuel message is observed through the move cost:
    // the actor returns before spending moves when the pocket is empty and spends them once there is fuel
    katana->ammo_unset();
    u.moves = 100;
    actor.use( u, *katana, false, u.bub_pos() );
    CHECK( u.moves == 100 );

    katana->ammo_set( itype_id( "gasoline" ), 10 );
    actor.use( u, *katana, false, u.bub_pos() );
    CHECK( u.moves < 100 );
}

TEST_CASE( "fireweapon_on_tick_reads_the_pocket", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> katana = item::spawn( itype_id( "firekatana_on" ) );
    const iuse_actor &actor = fireweapon_actor( *katana, "fireweapon_on" );
    katana->ammo_set( itype_id( "gasoline" ), 10 );
    REQUIRE( katana->ammo_remaining() == 10 );

    // CDF: Messages is stubbed; a voluntary use (t false) with fuel puts the blade out, which a read of 0 fuel would not
    actor.use( u, *katana, false, u.bub_pos() );

    CHECK( katana->typeId() == itype_id( "firekatana_off" ) );
}

TEST_CASE( "fireweapon_on_tick_empty_runs_dry", "[iuse][charges][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> katana = item::spawn( itype_id( "firekatana_on" ) );
    const iuse_actor &actor = fireweapon_actor( *katana, "fireweapon_on" );
    katana->ammo_unset();
    REQUIRE( katana->ammo_remaining() == 0 );

    // CDF: Messages is stubbed; with the pocket empty a voluntary use does not extinguish (the revert is process_tool's)
    actor.use( u, *katana, false, u.bub_pos() );

    CHECK( katana->typeId() == itype_id( "firekatana_on" ) );
}
