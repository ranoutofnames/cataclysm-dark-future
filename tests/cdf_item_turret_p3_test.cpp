#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "map/map.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"

// CDF: P3-8-5a-1 tests. Multimag turret types, the per-pocket prep wrapper, and
// the hand-written test turret part (the DDA per-gun turret generator does not
// exist in CDF, so the test part lives in TEST_DATA/vehicle_parts_p3.json).

TEST_CASE( "multimag_state_structs_have_dda_defaults", "[p3][turret]" )
{
    clear_all_state();

    multimag_pocket_state pocket_state;
    CHECK( pocket_state.initial_qty == 0 );
    CHECK( pocket_state.kind == multimag_pocket_state::source_kind::BATTERY );
    CHECK( pocket_state.vpart_index == -1 );

    multimag_display_pocket display_pocket;
    CHECK( display_pocket.local_qty == 0 );
    CHECK( display_pocket.effective_qty == 0 );
    CHECK( display_pocket.per_use_qty == 0 );
    CHECK_FALSE( display_pocket.vehicle_bound );
}

TEST_CASE( "prepare_multimag_pockets_ignores_items_without_firing_requirements",
           "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh =
        here.add_vehicle( vproto_id( "none" ), tripoint_bub_ms( 65, 65, 0 ), 270_degrees, 0, 0 );
    REQUIRE( veh );

    detached_ptr<item> glock = item::spawn( "glock_19", calendar::turn, 0 );
    REQUIRE_FALSE( glock->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *glock );
    CHECK( bindings.empty() );
}

TEST_CASE( "test_turret_part_loads_and_installs", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();

    const auto turret_vpart_id = vpart_id( "turret_test_multimag_turret_gun" );
    REQUIRE( turret_vpart_id.is_valid() );
    REQUIRE( turret_vpart_id->has_flag( "TURRET" ) );

    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), tripoint_bub_ms( 65, 65, 0 ),
                                    0_degrees, 0, 0 );
    REQUIRE( veh );

    // CDF: test_turret_rig (TEST_DATA/vehicle.json) carries its turret_mount at mount zero.
    const auto turret_index = veh->install_part( tripoint_mnt_veh::zero(), turret_vpart_id, true );
    REQUIRE( turret_index >= 0 );

    vehicle_part &turret_part = veh->part( turret_index );
    CHECK( turret_part.is_turret() );
    auto qry = veh->turret_query( turret_part );
    CHECK( qry );
}

TEST_CASE( "prepare_multimag_pockets_wrapper_uses_default_mode", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh =
        here.add_vehicle( vproto_id( "none" ), tripoint_bub_ms( 65, 65, 0 ), 270_degrees, 0, 0 );
    REQUIRE( veh );

    detached_ptr<item> tool = item::spawn( "test_multimag_turret_gun", calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto via_wrapper = vehicle::prepare_multimag_pockets( *veh, *tool );
    const auto via_explicit = vehicle::prepare_multimag_pockets(
        *veh, *tool, gun_mode_id( "DEFAULT" ), itype_id::NULL_ID() );

    // CDF: multimag_pocket_state is a verbatim DDA copy with no operator==, so
    // the maps are compared key by key instead of with map == map.
    REQUIRE( via_wrapper.size() == via_explicit.size() );
    for ( const auto &[pocket_id, wrapper_state] : via_wrapper ) {
        const auto explicit_it = via_explicit.find( pocket_id );
        REQUIRE( explicit_it != via_explicit.end() );
        CHECK( wrapper_state.initial_qty == explicit_it->second.initial_qty );
        CHECK( wrapper_state.kind == explicit_it->second.kind );
        CHECK( wrapper_state.vpart_index == explicit_it->second.vpart_index );
    }
    // CDF: this packet stops at the pocket loop head, so both maps stay empty
    // until 8-5a-2 fills the battery branch.
    CHECK( via_wrapper.empty() );
}
