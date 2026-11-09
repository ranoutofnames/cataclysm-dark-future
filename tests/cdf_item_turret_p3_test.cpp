#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vpart_range.h"

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

// CDF: P3-8-5a-3 tank tests. Fixtures are the test_multimag_* items in
// TEST_DATA/items.json. test_turret_rig carries the ordinary `tank` part
// (TEST_DATA/vehicle.json:10), filled through vehicle_part::ammo_set like
// tests/vehicle_turrets_test.cpp:87.
TEST_CASE( "prepare_multimag_pockets_fills_fluid_pocket_from_tank", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->charge_battery( 1000, false );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int tank_index = static_cast<int>( tanks.begin()->part_index() );
    // CDF: 50 gasoline charges; the fluid pocket caps at 40 (items.json:2056).
    veh->part( tank_index ).ammo_set( itype_id( "gasoline" ), 50 );
    REQUIRE( veh->part( tank_index ).ammo_remaining() == 50 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_vehicle_combo" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "fluid" ) == 1 );
    CHECK( tool->ammo_remaining_in_pocket( "fluid" ) > 0 );
    CHECK( bindings.at( "fluid" ).kind == multimag_pocket_state::source_kind::TANK );
    CHECK( bindings.at( "fluid" ).vpart_index == tank_index );
    // CDF: prep only fills the pocket; billing the tank is 8-5a-4.
    CHECK( veh->part( tank_index ).ammo_remaining() == 50 );
}

TEST_CASE( "well_pocket_synthesizes_magazine_from_tank", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int tank_index = static_cast<int>( tanks.begin()->part_index() );
    veh->part( tank_index ).ammo_set( itype_id( "gasoline" ), 50 );
    REQUIRE( veh->part( tank_index ).ammo_remaining() == 50 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_well_fluid" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "fuel" ) == 1 );
    CHECK( bindings.at( "fuel" ).kind == multimag_pocket_state::source_kind::TANK );
    CHECK( bindings.at( "fuel" ).vpart_index == tank_index );
    CHECK( bindings.at( "fuel" ).initial_qty > 0 );
    CHECK( tool->ammo_remaining_in_pocket( "fuel" ) > 0 );
}

TEST_CASE( "two_same_fuel_tanks_bind_each_pocket_to_its_own_part", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int first_tank = static_cast<int>( tanks.begin()->part_index() );
    const int second_tank =
        veh->install_part( tripoint_mnt_veh( 1, 0, 0 ), vpart_id( "tank" ), true );
    REQUIRE( second_tank >= 0 );
    REQUIRE( second_tank != first_tank );

    // CDF: 6 charges each, above the 2-per-use cost, so each tank serves one pocket.
    veh->part( first_tank ).ammo_set( itype_id( "gasoline" ), 6 );
    veh->part( second_tank ).ammo_set( itype_id( "gasoline" ), 6 );
    REQUIRE( veh->part( first_tank ).ammo_remaining() == 6 );
    REQUIRE( veh->part( second_tank ).ammo_remaining() == 6 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_two_fluid" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "left" ) == 1 );
    REQUIRE( bindings.count( "right" ) == 1 );
    CHECK( bindings.at( "left" ).kind == multimag_pocket_state::source_kind::TANK );
    CHECK( bindings.at( "right" ).kind == multimag_pocket_state::source_kind::TANK );
    // Each pocket is backed by a distinct vpart and gets the per-tank budget,
    // not double the same tank's contents.
    CHECK( bindings.at( "left" ).vpart_index != bindings.at( "right" ).vpart_index );
    CHECK( bindings.at( "left" ).initial_qty == 6 );
    CHECK( bindings.at( "right" ).initial_qty == 6 );
}

TEST_CASE( "preferred_primary_picks_that_tank_first", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->charge_battery( 1000, false );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int diesel_tank = static_cast<int>( tanks.begin()->part_index() );
    const int gasoline_tank =
        veh->install_part( tripoint_mnt_veh( 1, 0, 0 ), vpart_id( "tank" ), true );
    REQUIRE( gasoline_tank >= 0 );
    REQUIRE( gasoline_tank != diesel_tank );

    veh->part( diesel_tank ).ammo_set( itype_id( "diesel" ), 50 );
    veh->part( gasoline_tank ).ammo_set( itype_id( "gasoline" ), 50 );
    REQUIRE( veh->part( diesel_tank ).ammo_remaining() == 50 );
    REQUIRE( veh->part( gasoline_tank ).ammo_remaining() == 50 );

    // CDF: the loaded combo fixture restricts `fluid` to gasoline only
    // (TEST_DATA/items.json:2056) and no loaded fixture has two fuels in one
    // ammo_restriction, so this test extends the pocket data in memory.
    // C4 fuel pair: diesel (ammotype diesel) and gasoline (ammotype gasoline).
    // [UNSURE] the brief assumes a two-fuel fixture; see report.
    pocket_data two_fuel;
    detached_ptr<item> preferred_tool =
        item::spawn( itype_id( "test_multimag_vehicle_combo" ), calendar::turn, 0 );
    REQUIRE( preferred_tool->uses_firing_requirements() );
    item_pocket *fluid = preferred_tool->pocket_by_id( "fluid" );
    REQUIRE( fluid != nullptr );
    two_fuel = fluid->get_data();
    two_fuel.ammo_restriction[ammotype( "diesel" )] = 40;
    fluid->set_data( &two_fuel );

    // Gasoline is the second fuel and the second tank, so the 5-arg call with
    // that preference must skip the lower-indexed diesel tank.
    const auto preferred = vehicle::prepare_multimag_pockets(
        *veh, *preferred_tool, gun_mode_id( "DEFAULT" ), itype_id( "gasoline" ) );
    REQUIRE( preferred.count( "fluid" ) == 1 );
    CHECK( preferred.at( "fluid" ).kind == multimag_pocket_state::source_kind::TANK );
    CHECK( preferred.at( "fluid" ).vpart_index == gasoline_tank );

    // Null preference: pass 0 is skipped, so the first restriction key
    // (diesel, map order) and its tank win.
    pocket_data two_fuel_fallback;
    detached_ptr<item> fallback_tool =
        item::spawn( itype_id( "test_multimag_vehicle_combo" ), calendar::turn, 0 );
    REQUIRE( fallback_tool->uses_firing_requirements() );
    item_pocket *fallback_fluid = fallback_tool->pocket_by_id( "fluid" );
    REQUIRE( fallback_fluid != nullptr );
    two_fuel_fallback = fallback_fluid->get_data();
    two_fuel_fallback.ammo_restriction[ammotype( "diesel" )] = 40;
    fallback_fluid->set_data( &two_fuel_fallback );

    const auto fallback = vehicle::prepare_multimag_pockets( *veh, *fallback_tool );
    REQUIRE( fallback.count( "fluid" ) == 1 );
    CHECK( fallback.at( "fluid" ).kind == multimag_pocket_state::source_kind::TANK );
    CHECK( fallback.at( "fluid" ).vpart_index == diesel_tank );
}
