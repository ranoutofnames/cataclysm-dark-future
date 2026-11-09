#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "map/map.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"

#include <utility>

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
    // CDF: 8-5a-2: the wrapper loads the tool's empty pockets, and a loaded pocket blocks prep, so
    // the explicit call needs its own fresh tool to see the same starting state.
    detached_ptr<item> tool2 = item::spawn( "test_multimag_turret_gun", calendar::turn, 0 );
    const auto via_explicit = vehicle::prepare_multimag_pockets(
        *veh, *tool2, gun_mode_id( "DEFAULT" ), itype_id::NULL_ID() );

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
    // CDF: 8-5a-2 fills the battery branch: the empty power well gets a (zero-charge) cell on a
    // vehicle without a battery, and its state is recorded; the ammo well is not a battery pocket.
    CHECK( via_wrapper.count( "power" ) == 1 );
    CHECK( via_wrapper.count( "ammo" ) == 0 );
}

// CDF: P3-8-5a-2 battery tests. Fixtures are the test_multimag_* items in
// TEST_DATA/items.json. test_turret_rig carries a storage_battery part
// (TEST_DATA/vehicle.json:9), so charge_battery feeds fuel_left( "battery" ).
TEST_CASE( "prepare_multimag_pockets_populates_battery_pockets", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->charge_battery( 1000, false );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_vehicle_combo" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "power" ) == 1 );
    CHECK( tool->ammo_remaining_in_pocket( "power" ) > 0 );
    CHECK( bindings.at( "power" ).kind == multimag_pocket_state::source_kind::BATTERY );
    CHECK( bindings.at( "power" ).initial_qty ==
           tool->ammo_remaining_in_pocket( "power" ) );
}

TEST_CASE( "two_battery_pockets_share_one_network_without_double_claim", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->discharge_battery( veh->fuel_left( itype_id( "battery" ), true ) );
    veh->charge_battery( 8, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 8 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_two_battery" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    const int left = bindings.count( "left" ) ? bindings.at( "left" ).initial_qty : 0;
    const int right = bindings.count( "right" ) ? bindings.at( "right" ).initial_qty : 0;
    CHECK( left + right <= 8 );
    // CDF: item::feasible_tool_uses( int external_pool ) exists (item.h:2576);
    // the per-use cost is left 5 + right 5, so an 8-unit network has no use.
    CHECK( tool->feasible_tool_uses( 0 ) == 0 );
}

TEST_CASE( "direct_magazine_battery_pocket_caps_at_pocket_capacity", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->discharge_battery( veh->fuel_left( itype_id( "battery" ), true ) );
    veh->charge_battery( 1000, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_direct_battery" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "core" ) == 1 );
    // CDF: the core pocket caps at 40 (TEST_DATA/items.json:2155), far below
    // the 1000-unit network.
    CHECK( bindings.at( "core" ).initial_qty == 40 );
    CHECK( tool->ammo_remaining_in_pocket( "core" ) == 40 );
    // CDF: prep only fills the pocket; billing the network is 8-5a-4.
    CHECK( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );
}

TEST_CASE( "mixed_well_and_direct_battery_pockets_account_claims", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->discharge_battery( veh->fuel_left( itype_id( "battery" ), true ) );
    // CDF: heavy_battery_cell holds 1000 here (DDA 259; tests/p3_data_r1_test.cpp:214).
    // The network is raised from DDA's 350 so the well fills to its 1000 cap
    // before the direct pocket, keeping the mixed-claim check (1000 + 40).
    veh->charge_battery( 1050, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 1050 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_mixed_battery" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "well" ) == 1 );
    REQUIRE( bindings.count( "direct" ) == 1 );
    CHECK( bindings.at( "well" ).initial_qty == 1000 );
    CHECK( bindings.at( "direct" ).initial_qty == 40 );
}

TEST_CASE( "prepare_multimag_pockets_skips_pockets_with_content", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->charge_battery( 1000, false );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_turret_gun" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    REQUIRE( mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 10 ),
                          pocket_type::MAGAZINE ).success() );
    REQUIRE( tool->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );

    const auto first = vehicle::prepare_multimag_pockets( *veh, *tool );
    CHECK( first.count( "power" ) == 1 );
    CHECK( first.count( "ammo" ) == 0 );
    CHECK( first.at( "power" ).kind == multimag_pocket_state::source_kind::BATTERY );

    // CDF: existing content blocks prep (DDA vehicle_use.cpp:1984-1986), so the
    // power well the first call just filled is skipped on the second call.
    const auto second = vehicle::prepare_multimag_pockets( *veh, *tool );
    CHECK( second.count( "power" ) == 0 );
}

TEST_CASE( "turret_query_uses_prepared_copy", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );

    const auto turret_index = veh->install_part( tripoint_mnt_veh::zero(),
                              vpart_id( "turret_test_multimag_turret_gun" ), true );
    REQUIRE( turret_index >= 0 );
    veh->charge_battery( 1000, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );

    vehicle_part &turret_part = veh->part( turret_index );
    REQUIRE( turret_part.is_turret() );
    item &gun = turret_part.get_base();

    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    REQUIRE( mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 10 ),
                          pocket_type::MAGAZINE ).success() );
    REQUIRE( gun.put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );

    {
        auto qry = veh->turret_query( turret_part );
        REQUIRE( qry );
        CHECK( qry.query() == turret_data::status::ready );
    }
    // CDF: the query prepped a temporary copy, so the mounted gun's own power
    // well must still be empty.
    CHECK( gun.ammo_remaining_in_pocket( "power" ) == 0 );

    veh->discharge_battery( veh->fuel_left( itype_id( "battery" ), true ) );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 0 );

    auto qry_dry = veh->turret_query( veh->part( turret_index ) );
    REQUIRE( qry_dry );
    CHECK( qry_dry.query() == turret_data::status::no_ammo );
}
