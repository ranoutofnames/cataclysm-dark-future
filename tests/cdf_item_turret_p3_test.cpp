#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
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

// CDF: P3-8-5a-4 drain and fire-cycle tests. D1-D3 port the drain halves of
// DDA tests/vehicle_part_test.cpp:272-316, 318-373, 390-420; D4-D6 follow the
// CDF fire path (turret_data::prepare_fire -> consume -> post_fire).
TEST_CASE( "drain_back_bills_battery_and_tank_per_pocket", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms pos( 65, 65, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), pos, 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->charge_battery( 1000, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int tank_index = static_cast<int>( tanks.begin()->part_index() );
    veh->part( tank_index ).ammo_set( itype_id( "gasoline" ), 50 );
    REQUIRE( veh->part( tank_index ).ammo_remaining() == 50 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_vehicle_combo" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "power" ) == 1 );
    REQUIRE( bindings.count( "fluid" ) == 1 );

    REQUIRE( tool->consume_tool_uses( 1, here, pos, nullptr ) == 1 );

    vehicle::drain_back_multimag( *veh, *tool, bindings );

    // CDF: DEFAULT costs power 5 + fluid 2 (TEST_DATA/items.json:2059), so only
    // the used delta leaves each vehicle store.
    CHECK( veh->fuel_left( itype_id( "battery" ), true ) == 995 );
    CHECK( veh->part( tank_index ).ammo_remaining() == 48 );
}

TEST_CASE( "drain_back_bills_each_bound_tank", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms pos( 65, 65, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), pos, 0_degrees, 0, 0 );
    REQUIRE( veh );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int first_tank = static_cast<int>( tanks.begin()->part_index() );
    const int second_tank =
        veh->install_part( tripoint_mnt_veh( 1, 0, 0 ), vpart_id( "tank" ), true );
    REQUIRE( second_tank >= 0 );
    REQUIRE( second_tank != first_tank );

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

    REQUIRE( tool->consume_tool_uses( 1, here, pos, nullptr ) == 1 );

    vehicle::drain_back_multimag( *veh, *tool, bindings );

    // CDF: each pocket costs 2 (TEST_DATA/items.json:2143), and each bound
    // pocket must bill its own tank once, not one tank twice.
    CHECK( veh->part( first_tank ).ammo_remaining() == 4 );
    CHECK( veh->part( second_tank ).ammo_remaining() == 4 );
}

TEST_CASE( "drain_back_direct_battery_bills_use_not_pocket", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms pos( 65, 65, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), pos, 0_degrees, 0, 0 );
    REQUIRE( veh );
    veh->discharge_battery( veh->fuel_left( itype_id( "battery" ), true ) );
    veh->charge_battery( 1000, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );

    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_direct_battery" ),
                                           calendar::turn, 0 );
    REQUIRE( tool->uses_firing_requirements() );

    const auto bindings = vehicle::prepare_multimag_pockets( *veh, *tool );
    REQUIRE( bindings.count( "core" ) == 1 );
    // CDF: prep only fills the pocket; the pool is untouched (8-5a-2).
    CHECK( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );

    REQUIRE( tool->consume_tool_uses( 1, here, pos, nullptr ) == 1 );

    vehicle::drain_back_multimag( *veh, *tool, bindings );

    // CDF: DEFAULT costs 5 (TEST_DATA/items.json:2156), so the pool loses the
    // use, not the pocket's 40-charge fill.
    CHECK( veh->fuel_left( itype_id( "battery" ), true ) == 995 );
}

TEST_CASE( "turret_fire_cycle_bills_vehicle_and_clears_bound_pockets", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms pos( 65, 65, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), pos, 0_degrees, 0, 0 );
    REQUIRE( veh );
    const auto turret_index = veh->install_part( tripoint_mnt_veh::zero(),
                              vpart_id( "turret_test_multimag_turret_gun" ), true );
    REQUIRE( turret_index >= 0 );
    veh->charge_battery( 1000, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );

    vehicle_part &turret_part = veh->part( turret_index );
    item &gun = turret_part.get_base();

    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    REQUIRE( mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ),
                          pocket_type::MAGAZINE ).success() );
    REQUIRE( gun.put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.ammo_remaining_in_pocket( "ammo" ) == 15 );

    auto qry = veh->turret_query( turret_part );
    REQUIRE( qry );

    avatar &who = get_avatar();
    qry.prepare_fire( who );
    // CDF: prep fills the empty power well from the vehicle network; one
    // DEFAULT shot costs ammo 1 + power 5 (TEST_DATA/items.json:1586).
    REQUIRE( gun.ammo_remaining_in_pocket( "power" ) >= 5 );
    REQUIRE( gun.consume_one_shot( here, pos, nullptr ) == 1 );

    qry.post_fire( who, 1 );

    CHECK( veh->fuel_left( itype_id( "battery" ), true ) == 995 );
    CHECK( gun.ammo_remaining_in_pocket( "power" ) == 0 );
    REQUIRE( gun.pocket_by_id( "power" ) != nullptr );
    CHECK( gun.pocket_by_id( "power" )->empty() );
    // The player-loaded magazine keeps its round: only vehicle-filled pockets
    // are cleared.
    CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 14 );
}

TEST_CASE( "post_fire_without_bindings_leaves_pockets_alone", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms pos( 65, 65, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), pos, 0_degrees, 0, 0 );
    REQUIRE( veh );
    const auto turret_index = veh->install_part( tripoint_mnt_veh::zero(),
                              vpart_id( "turret_test_multimag_turret_gun" ), true );
    REQUIRE( turret_index >= 0 );
    veh->charge_battery( 1000, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );

    vehicle_part &turret_part = veh->part( turret_index );
    item &gun = turret_part.get_base();

    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    REQUIRE( mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ),
                          pocket_type::MAGAZINE ).success() );
    REQUIRE( gun.put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.ammo_remaining_in_pocket( "ammo" ) == 15 );

    auto qry = veh->turret_query( turret_part );
    REQUIRE( qry );

    // No prepare_fire: mm_bindings_ stays empty, so nothing is billed or cleared.
    qry.post_fire( get_avatar(), 1 );

    CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 15 );
    CHECK( veh->fuel_left( itype_id( "battery" ), true ) == 1000 );
}

TEST_CASE( "post_fire_empties_leftover_magazine_wells_of_tank_turrets", "[p3][turret]" )
{
    // CDF: fixture is the existing mounted flamethrower (USE_TANKS,
    // data/json/vehicleparts/turret.json:114-127) on test_turret_rig's tank
    // (TEST_DATA/vehicle.json:9-10), tank-fed as in
    // tests/vehicle_turrets_test.cpp:80-87. The leftover is an existing
    // pressurized_tank magazine (data/json/items/magazine/liquid.json:3).
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms pos( 65, 65, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), pos, 0_degrees, 0, 0 );
    REQUIRE( veh );
    const auto turret_index =
        veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "flamethrower" ), true );
    REQUIRE( turret_index >= 0 );
    REQUIRE( veh->part_flag( turret_index, "USE_TANKS" ) );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int tank_index = static_cast<int>( tanks.begin()->part_index() );
    veh->part( tank_index ).ammo_set( itype_id( "napalm" ) );
    REQUIRE( veh->part( tank_index ).ammo_remaining() > 0 );

    vehicle_part &turret_part = veh->part( turret_index );
    item &gun = turret_part.get_base();
    // CDF: counts the wells that hold something (the pocket itself always exists)
    const auto loaded_well_count = [&gun]() {
        return gun.get_pockets( []( const item_pocket &pocket ) {
            return pocket.is_type( pocket_type::MAGAZINE_WELL ) && !pocket.empty();
        } ).size();
    };
    REQUIRE( loaded_well_count() == 0 );
    REQUIRE( gun.put_in( item::spawn( itype_id( "pressurized_tank" ) ),
                         pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( loaded_well_count() == 1 );

    auto qry = veh->turret_query( turret_part );
    REQUIRE( qry );
    qry.post_fire( get_avatar(), 1 );

    CHECK( loaded_well_count() == 0 );
}

// CDF: P3-8-5b-1 turret menu tests. E1-E4 port the turret side of the multimag
// work: the NO_TURRET opt-out, the reload/unload gates, the ammo choice fed by
// vehicle tanks, and the range/ammo data read off a prepared copy.
TEST_CASE( "vehicle_turret_filter_multimag_per_gun_opt_out", "[p3][turret]" )
{
    clear_all_state();
    SECTION( "NO_TURRET multimag gun keeps the per-gun opt-out" ) {
        detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ),
                                              calendar::turn, 0 );
        REQUIRE( gun->uses_firing_requirements() );
        CHECK( gun->has_flag( flag_NO_TURRET ) );
    }
    SECTION( "multimag gun without NO_TURRET passes the filter" ) {
        detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_turret_gun" ),
                                              calendar::turn, 0 );
        REQUIRE( gun->uses_firing_requirements() );
        CHECK_FALSE( gun->has_flag( flag_NO_TURRET ) );
    }
}

TEST_CASE( "turret_can_reload_and_unload_follow_multimag_pockets", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    const auto turret_index = veh->install_part( tripoint_mnt_veh::zero(),
                              vpart_id( "turret_test_multimag_turret_gun" ), true );
    REQUIRE( turret_index >= 0 );

    vehicle_part &turret_part = veh->part( turret_index );
    item &gun = turret_part.get_base();
    REQUIRE( gun.uses_firing_requirements() );
    REQUIRE( gun.pocket_by_id( "ammo" ) != nullptr );
    REQUIRE( gun.pocket_by_id( "ammo" )->empty() );
    REQUIRE( gun.pocket_by_id( "power" ) != nullptr );
    REQUIRE( gun.pocket_by_id( "power" )->empty() );

    {
        auto qry = veh->turret_query( turret_part );
        REQUIRE( qry );
        CHECK( qry.can_reload() );
        CHECK_FALSE( qry.can_unload() );
    }

    // The second well is restricted to heavy_battery_cell in the fixture; widen
    // it in memory so a magazine can sit in the second well only.
    item_pocket *power_pocket = gun.pocket_by_id( "power" );
    REQUIRE( power_pocket != nullptr );
    pocket_data power_data = power_pocket->get_data();
    power_data.item_id_restriction.insert( itype_id( "glockmag" ) );
    power_pocket->set_data( &power_data );

    REQUIRE( power_pocket->insert_item( item::spawn( itype_id( "glockmag" ) ) ).success() );
    REQUIRE( gun.pocket_by_id( "ammo" )->empty() );

    auto qry = veh->turret_query( turret_part );
    REQUIRE( qry );
    // The magazine is empty, so ammo_remaining() stays 0 and only a scan of the
    // wells can see it.
    CHECK( qry.can_unload() );
}

TEST_CASE( "turret_ammo_options_list_tank_fuels_for_liquid_pockets", "[p3][turret]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ),
                                     tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh );
    const auto turret_index = veh->install_part(
                                  tripoint_mnt_veh::zero(),
                                  vpart_id( "turret_test_multimag_turret_flamethrower" ), true );
    REQUIRE( turret_index >= 0 );
    veh->charge_battery( 1000, false );

    vehicle_part &turret_part = veh->part( turret_index );
    item &gun = turret_part.get_base();
    REQUIRE( gun.uses_firing_requirements() );
    // A cell in the power well, as the prep would leave it.
    item_pocket *power_pocket = gun.pocket_by_id( "power" );
    REQUIRE( power_pocket != nullptr );
    REQUIRE( power_pocket->insert_item(
                 item::spawn( itype_id( "heavy_battery_cell" ) ) ).success() );

    auto qry = veh->turret_query( turret_part );
    REQUIRE( qry );
    // No fuel in the tank and a battery pocket that is never offered as ammo.
    CHECK( qry.ammo_options().empty() );

    const auto tanks = veh->get_avail_parts( vpart_bitflags::VPFLAG_FLUIDTANK );
    REQUIRE_FALSE( tanks.empty() );
    const int tank_index = static_cast<int>( tanks.begin()->part_index() );
    veh->part( tank_index ).ammo_set( itype_id( "gasoline" ), 50 );
    REQUIRE( veh->part( tank_index ).ammo_remaining() == 50 );

    const auto opts = qry.ammo_options();
    CHECK( opts.count( itype_id( "gasoline" ) ) == 1 );
    CHECK_FALSE( opts.contains( itype_id( "battery" ) ) );
    CHECK( opts.size() == 1 );
}

TEST_CASE( "turret_range_and_ammo_data_come_from_prepared_copy", "[p3][turret]" )
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
    item &gun = turret_part.get_base();

    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    REQUIRE( mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 10 ),
                          pocket_type::MAGAZINE ).success() );
    REQUIRE( gun.put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.ammo_remaining_in_pocket( "ammo" ) == 10 );

    auto qry = veh->turret_query( turret_part );
    REQUIRE( qry );

    const itype *ammo = qry.ammo_data();
    REQUIRE( ammo != nullptr );
    CHECK( ammo->get_id() == itype_id( "9mm" ) );
    // The gun range already counts the loaded magazine's ammo, and the port
    // adds the ammo bonus on top of it.
    CHECK( qry.range() == qry.base().gun_range() + ammo->ammo->range );

    // The power well was filled on the copy, not on the mounted gun.
    REQUIRE( gun.pocket_by_id( "power" ) != nullptr );
    CHECK( gun.pocket_by_id( "power" )->empty() );
    CHECK( gun.ammo_remaining_in_pocket( "power" ) == 0 );
    CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 10 );
}
