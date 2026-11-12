#include "active_tile_data.h"
#include "active_tile_data_def.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "distribution_grid.h"
#include "item.h"
#include "iuse_actor.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "point.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vpart_position.h"
#include "vehicle/vpart_range.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

// CDF: GRIDLINK1 tests. Spec: plans/P3/GRIDLINK1/brief.md. The BN feature "cable a vehicle to a
// distribution grid connector" (iuse::cable_attach, removed in LINKUP1d) comes back through the
// DDA link-up cable: one cable end on the vehicle, the other on the f_cable_connector furniture,
// which turns the cable into a targets_grid vehicle part and registers the vehicle with the
// connector (active_tiles::vehicle_connector_tile::connected_vehicles). The grid backend
// (distribution_graph, distribution_grid) is CDF's BN one and is not changed.

namespace
{

const itype_id itype_battery( "battery" );
const furn_str_id f_cable_connector( "f_cable_connector" );
const furn_str_id f_battery( "f_battery" );

auto charged_car( const tripoint_bub_ms &pos, int charge ) -> vehicle * // *NOPAD*
{
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "car" ), pos, 0_degrees, 0, 0, false );
    REQUIRE( veh != nullptr );
    veh->charge_battery( charge, false );
    REQUIRE( veh->fuel_left( itype_battery ) == charge );
    return veh;
}

auto find_part( vehicle &veh, const std::function<bool( const vehicle_part & )> &pred )
    -> std::optional<int>
{
    for( const vpart_reference &vp : veh.get_all_parts() ) {
        if( vp.part().removed || vp.part().is_broken() ) {
            continue;
        }
        if( pred( vp.part() ) ) {
            return static_cast<int>( vp.part_index() );
        }
    }
    return std::nullopt;
}

auto part_with_flag( vehicle &veh, const std::string &flag ) -> std::optional<int>
{
    return find_part( veh, [&flag]( const vehicle_part &p ) {
        return p.info().has_flag( flag );
    } );
}

auto grid_cable_part( vehicle &veh ) -> std::optional<int>
{
    return find_part( veh, []( const vehicle_part &p ) {
        return p.has_flag( vehicle_part::targets_grid );
    } );
}

auto part_position( vehicle &veh, int part_index ) -> optional_vpart_position
{
    return get_map().veh_at( veh.bub_part_location( part_index ) );
}

auto link_actor( const itype_id &id ) -> const link_up_actor * // *NOPAD*
{
    REQUIRE( id.is_valid() );
    const use_function *use = id->get_use( "link_up" );
    REQUIRE( use != nullptr );
    return static_cast<const link_up_actor *>( use->get_actor_ptr() );
}

// A free tile within `reach` of `from` that no vehicle part occupies.
auto free_tile_near_point( const tripoint_bub_ms &from, int reach ) -> tripoint_bub_ms
{
    map &here = get_map();
    static const std::vector<point> offsets = {
        point_east, point_south, point_west, point_north,
        point_east + point_south, point_west + point_south,
        point_east + point_north, point_west + point_north
    };
    for( int r = 1; r <= reach; r++ ) {
        for( const point &offset : offsets ) {
            const tripoint_bub_ms candidate = from + point( offset.x * r, offset.y * r );
            if( here.inbounds( candidate ) && !here.veh_at( candidate ) ) {
                return candidate;
            }
        }
    }
    // No free tile inside the reach: the caller's setup is wrong.
    return from;
}

// A tile next to the vehicle that no vehicle part occupies, within `reach` of `near_part`.
auto free_tile_near( vehicle &veh, int near_part, int reach ) -> tripoint_bub_ms
{
    return free_tile_near_point( veh.bub_part_location( near_part ), reach );
}

struct grid_fixture {
    tripoint_bub_ms connector_bub;
    tripoint_abs_ms connector_abs;
    vehicle_connector_tile *connector;
    battery_tile *battery;
    distribution_grid *grid;
};

// A grid connector next to the vehicle and, optionally, a charged grid battery tile beside it.
auto make_grid( vehicle &veh, int near_part, bool with_battery, int battery_charge )
    -> grid_fixture
{
    map &here = get_map();
    const tripoint_bub_ms connector_bub = free_tile_near( veh, near_part, 2 );
    const tripoint_abs_ms connector_abs = bub_to_abs( connector_bub );
    here.furn_set( connector_bub, f_cable_connector );
    vehicle_connector_tile *connector =
        active_tiles::furn_at<vehicle_connector_tile>( connector_abs );
    REQUIRE( connector != nullptr );

    battery_tile *battery = nullptr;
    if( with_battery ) {
        const tripoint_bub_ms battery_bub = free_tile_near_point( connector_bub, 2 );
        here.furn_set( battery_bub, f_battery );
        battery = active_tiles::furn_at<battery_tile>( bub_to_abs( battery_bub ) );
        REQUIRE( battery != nullptr );
        REQUIRE( battery->mod_resource( battery_charge ) == 0 );
    }

    distribution_grid &grid = get_distribution_grid_tracker().grid_at( connector_abs );
    REQUIRE( !grid.empty() );
    return grid_fixture{ connector_bub, connector_abs, connector, battery, &grid };
}

} // namespace

// G0: the cables that carried the BN grid attachment advertise the new grid target; the tow
// cable does not (BN's TOW_ATTACH never reached the grid).
TEST_CASE( "gridlink1_cable_targets", "[gridlink1][p3]" )
{
    const link_up_actor *jumper = link_actor( itype_id( "jumper_cable" ) );
    CHECK( jumper->targets.count( link_state::vehicle_grid ) == 1 );
    const link_up_actor *heavy = link_actor( itype_id( "jumper_cable_heavy" ) );
    CHECK( heavy->targets == jumper->targets );
    const link_up_actor *debug = link_actor( itype_id( "jumper_cable_debug" ) );
    CHECK( debug->targets.count( link_state::vehicle_grid ) == 1 );

    const link_up_actor *extension = link_actor( itype_id( "extension_cable" ) );
    CHECK( extension->targets.count( link_state::vehicle_grid ) == 1 );
    const link_up_actor *long_extension = link_actor( itype_id( "long_extension_cable" ) );
    CHECK( long_extension->targets == extension->targets );

    const link_up_actor *tow = link_actor( itype_id( "hd_tow_cable" ) );
    CHECK( tow->targets.count( link_state::vehicle_grid ) == 0 );
}

// G1: a jumper cable with one end on the vehicle battery and the other on a grid connector turns
// into a grid vehicle part: the connector lists the vehicle, the grid counts its charge, and
// power flows in both directions (BN iuse::cable_attach, 5123360d7b^:src/iuse.cpp:8711-8726).
TEST_CASE( "gridlink1_vehicle_joins_grid", "[gridlink1][p3]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
    REQUIRE( battery );
    grid_fixture fx = make_grid( *veh, *battery, true, 0 );

    item cable( itype_id( "jumper_cable" ) );
    REQUIRE( cable.can_link_up() );
    REQUIRE( cable.link_to( part_position( *veh, *battery ),
                            link_state::vehicle_battery ).success() );

    const ret_val<void> grid_link = cable.link_to( fx.connector_abs, link_state::vehicle_grid );
    REQUIRE( grid_link.success() );
    // The cable is consumed: it is now a vehicle part, as the BN cable was.
    CHECK_FALSE( cable.has_link_data() );

    const std::optional<int> cable_part = grid_cable_part( *veh );
    REQUIRE( cable_part );
    CHECK( veh->part( *cable_part ).info().item == itype_id( "jumper_cable" ) );
    CHECK( veh->part( *cable_part ).info().has_flag( "POWER_TRANSFER" ) );
    CHECK( veh->part( *cable_part ).target.first == fx.connector_abs );
    CHECK( veh->part( *cable_part ).target.second == fx.connector_abs );

    REQUIRE( fx.connector->connected_vehicles.size() == 1 );
    CHECK( fx.connector->connected_vehicles.front() == veh->abs_ms_location() );

    // The grid sees the vehicle's charge...
    REQUIRE( fx.grid->get_resource() == 100 );

    // ...power flows from the vehicle into the grid...
    CHECK( fx.grid->mod_resource( -40 ) == 0 );
    CHECK( veh->fuel_left( itype_battery ) == 60 );

    // ...and from the grid into the vehicle.
    REQUIRE( fx.battery->mod_resource( 100 ) == 0 );
    CHECK( veh->discharge_battery( 120, true ) == 0 );
    CHECK( veh->fuel_left( itype_battery ) == 0 );
    CHECK( fx.battery->get_resource() == 40 );
}

// G2: detaching the cable from the vehicle side (the "Disconnect power connections" menu entry,
// vehicle::unlink_cables) removes the vehicle from the grid.
TEST_CASE( "gridlink1_detach_removes_vehicle", "[gridlink1][p3]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
    REQUIRE( battery );
    grid_fixture fx = make_grid( *veh, *battery, false, 0 );

    item cable( itype_id( "jumper_cable" ) );
    REQUIRE( cable.link_to( part_position( *veh, *battery ),
                            link_state::vehicle_battery ).success() );
    REQUIRE( cable.link_to( fx.connector_abs, link_state::vehicle_grid ).success() );

    const std::optional<int> cable_part = grid_cable_part( *veh );
    REQUIRE( cable_part );
    REQUIRE( fx.grid->get_resource() == 100 );

    const tripoint_mnt_veh cable_mount = veh->part( *cable_part ).mount;
    veh->unlink_cables( cable_mount, get_avatar(), true, true, true );

    CHECK( fx.connector->connected_vehicles.empty() );
    CHECK( fx.grid->get_resource() == 0 );
    CHECK_FALSE( grid_cable_part( *veh ) );
    // The vehicle keeps its own charge after the cable is detached.
    CHECK( veh->fuel_left( itype_battery ) == 100 );
}

// G3: DDA vehicle-to-vehicle link-up is unchanged: two cable parts, no grid part, shared level.
TEST_CASE( "gridlink1_vehicle_to_vehicle_unchanged", "[gridlink1][p3]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *first = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );
    vehicle *second = charged_car( tripoint_bub_ms( 10, 18, 0 ), 50 );

    const std::optional<int> first_battery = part_with_flag( *first, "BATTERY" );
    const std::optional<int> second_battery = part_with_flag( *second, "BATTERY" );
    REQUIRE( first_battery );
    REQUIRE( second_battery );

    item cable( itype_id( "jumper_cable_heavy" ) );
    REQUIRE( cable.link_to( part_position( *first, *first_battery ),
                            link_state::vehicle_battery ).success() );
    REQUIRE( cable.link_to( part_position( *second, *second_battery ),
                            link_state::vehicle_battery ).success() );

    CHECK_FALSE( cable.has_link_data() );
    CHECK( first->has_part( "POWER_TRANSFER" ) );
    CHECK( second->has_part( "POWER_TRANSFER" ) );
    CHECK_FALSE( grid_cable_part( *first ) );
    CHECK_FALSE( grid_cable_part( *second ) );
    CHECK( first->connected_battery_power_level().first == 150 );
    CHECK( second->connected_battery_power_level().first == 150 );
}

// G4: the grid end can be attached first; the vehicle end then closes the link the same way.
TEST_CASE( "gridlink1_grid_first_then_vehicle", "[gridlink1][p3]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
    REQUIRE( battery );
    grid_fixture fx = make_grid( *veh, *battery, false, 0 );

    item cable( itype_id( "jumper_cable" ) );
    REQUIRE( cable.link_to( fx.connector_abs, link_state::vehicle_grid ).success() );
    CHECK( cable.link_has_states( link_state::vehicle_grid, link_state::no_link ) );

    REQUIRE( cable.link_to( part_position( *veh, *battery ),
                            link_state::vehicle_battery ).success() );
    CHECK_FALSE( cable.has_link_data() );

    const std::optional<int> cable_part = grid_cable_part( *veh );
    REQUIRE( cable_part );
    CHECK( veh->part( *cable_part ).target.first == fx.connector_abs );
    REQUIRE( fx.connector->connected_vehicles.size() == 1 );
    CHECK( fx.connector->connected_vehicles.front() == veh->abs_ms_location() );
}

// G5: the cable's length still applies, and only a real grid connector can be chosen.
TEST_CASE( "gridlink1_distance_and_connector_checks", "[gridlink1][p3]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
    REQUIRE( battery );

    // A connector ten tiles away: the jumper cable is only three tiles long.
    const tripoint_bub_ms far_bub( 25, 10, 0 );
    const tripoint_abs_ms far_abs = bub_to_abs( far_bub );
    get_map().furn_set( far_bub, f_cable_connector );
    vehicle_connector_tile *far_connector =
        active_tiles::furn_at<vehicle_connector_tile>( far_abs );
    REQUIRE( far_connector );

    // A plain tile with no connector.
    const tripoint_bub_ms plain_bub( 14, 14, 0 );
    REQUIRE_FALSE( get_map().veh_at( plain_bub ) );
    REQUIRE( active_tiles::furn_at<vehicle_connector_tile>( bub_to_abs( plain_bub ) ) == nullptr );

    item cable( itype_id( "jumper_cable" ) );
    REQUIRE( cable.link_to( part_position( *veh, *battery ),
                            link_state::vehicle_battery ).success() );

    CHECK_FALSE( cable.link_to( bub_to_abs( plain_bub ), link_state::vehicle_grid ).success() );
    CHECK_FALSE( cable.link_to( far_abs, link_state::vehicle_grid ).success() );

    CHECK( cable.link_has_state( link_state::vehicle_battery ) );
    CHECK_FALSE( grid_cable_part( *veh ) );
    CHECK( far_connector->connected_vehicles.empty() );
}

// G6: an extension cable reaches the grid from the vehicle controls (vehicle_port), like the BN
// CABLE_ATTACH cables did.
TEST_CASE( "gridlink1_extension_cable_to_controls", "[gridlink1][p3]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> port = part_with_flag( *veh, "CABLE_PORTS" );
    REQUIRE( port );
    grid_fixture fx = make_grid( *veh, *port, false, 0 );

    item cable( itype_id( "extension_cable" ) );
    REQUIRE( cable.link_to( part_position( *veh, *port ), link_state::vehicle_port ).success() );
    REQUIRE( cable.link_to( fx.connector_abs, link_state::vehicle_grid ).success() );

    const std::optional<int> cable_part = grid_cable_part( *veh );
    REQUIRE( cable_part );
    CHECK( veh->part( *cable_part ).info().item == itype_id( "extension_cable" ) );
    REQUIRE( fx.connector->connected_vehicles.size() == 1 );
    CHECK( fx.connector->connected_vehicles.front() == veh->abs_ms_location() );
}
