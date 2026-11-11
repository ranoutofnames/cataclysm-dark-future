#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vpart_position.h"
#include "vehicle/vpart_range.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

// CDF: LINKUP1b tests. Spec: PL-33e TASK-draft-LINKUP1-v4.md, LINKUP1b test table L6-L9, plus
// the vehicle-only scope of PL-35-b LINKUP1b-RESCOPE-v2 (LB-t0..t3, plus LB-t5 and draft L6-L9). The fixtures are in
// TEST_DATA/linkup1b.json. The vehicle side (connected battery graph, CABLE_PORTS/BATTERY part
// flags, two-vehicle link, tow) does not exist at cdf/p3 7f8f182d7b, so this file does not
// compile before LINKUP1b (the red-first run).

namespace
{

auto charged_car( const tripoint_bub_ms &pos, int charge ) -> vehicle * // *NOPAD*
{
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "car" ), pos, 0_degrees, 0, 0, false );
    REQUIRE( veh != nullptr );
    veh->charge_battery( charge, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ) ) == charge );
    return veh;
}

// First working part matching `pred`, or nothing.
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

// The optional_vpart_position of a part, as the iuse code obtains it from here.veh_at().
auto part_position( vehicle &veh, int part_index ) -> optional_vpart_position
{
    return get_map().veh_at( veh.bub_part_location( part_index ) );
}

auto process_at( detached_ptr<item> &&held, const tripoint_bub_ms &pos ) -> detached_ptr<item>
{
    return item::process( std::move( held ), nullptr, pos, false, 1 );
}

} // namespace

// LB-t0: the CABLE_PORTS and BATTERY vehicle part flags load and are set on the parts that carry
// them in DDA (dashboard, controls_electronic, robot_controls; battery parts).
TEST_CASE( "linkupb_vehicle_part_flags", "[link][linkup1b]" )
{
    CHECK( vpart_id( "dashboard" )->has_flag( VPFLAG_CABLE_PORTS ) );
    CHECK( vpart_id( "controls_electronic" )->has_flag( VPFLAG_CABLE_PORTS ) );
    CHECK( vpart_id( "robot_controls" )->has_flag( VPFLAG_CABLE_PORTS ) );
    CHECK( vpart_id( "battery_car" )->has_flag( VPFLAG_BATTERY ) );
    CHECK( vpart_id( "storage_battery" )->has_flag( VPFLAG_BATTERY ) );
    CHECK_FALSE( vpart_id( "frame_vertical_2" )->has_flag( VPFLAG_CABLE_PORTS ) );
    CHECK_FALSE( vpart_id( "frame_vertical_2" )->has_flag( VPFLAG_BATTERY ) );
}

// LB-t1: linking a tool to a vehicle battery sets both link states and the connected battery
// level is the vehicle's charge (DDA vehicle.cpp:5560-5573).
TEST_CASE( "linkupb_link_to_battery_reads_connected_level", "[link][linkup1b]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
    REQUIRE( battery );
    const optional_vpart_position vp = part_position( *veh, *battery );
    REQUIRE( vp );

    item tool( itype_id( "test_linkupb_welder" ) );
    REQUIRE( tool.can_link_up() );
    const ret_val<void> result = tool.link_to( vp, link_state::vehicle_battery );
    REQUIRE( result.success() );
    CHECK( tool.link_has_state( link_state::no_link ) );
    CHECK( tool.link_has_states( link_state::no_link, link_state::vehicle_battery ) );
    CHECK( tool.link().t_veh == veh );

    CHECK( veh->connected_battery_power_level().first == 100 );
    CHECK( tool.ammo_remaining_linked( here, nullptr ) == 100 );
}

// LB-t2: a part without CABLE_PORTS/BATTERY refuses the link.
TEST_CASE( "linkupb_link_refused_without_ports", "[link][linkup1b]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    // Parts share a mount (frame, controls, battery), so pick a mount where none of them is
    // linkable; avail_linkable_part() looks at every part at the mount.
    std::optional<tripoint_bub_ms> plain_pos;
    for( const vpart_reference &vp : veh->get_all_parts() ) {
        const bool any_linkable = std::ranges::any_of(
                                      veh->parts_at_relative( vp.mount(), false ), [veh]( int p ) {
            return veh->part( p ).info().has_flag( VPFLAG_CABLE_PORTS ) ||
                   veh->part( p ).info().has_flag( VPFLAG_BATTERY );
        } );
        if( !any_linkable ) {
            plain_pos = veh->bub_part_location( vp.part_index() );
            break;
        }
    }
    REQUIRE( plain_pos );
    const optional_vpart_position vp = get_map().veh_at( *plain_pos );
    REQUIRE( vp );

    item tool( itype_id( "test_linkupb_welder" ) );
    const ret_val<void> result = tool.link_to( vp, link_state::vehicle_port );
    CHECK_FALSE( result.success() );
    CHECK( tool.has_no_links() );
    CHECK_FALSE( tool.has_link_data() );
}

// LB-t3: two vehicles joined by a cable share their battery level (DDA search_connected_vehicles,
// CDF distribution_graph::traverse).
TEST_CASE( "linkupb_two_vehicles_share_battery_level", "[link][linkup1b]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *first = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );
    vehicle *second = charged_car( tripoint_bub_ms( 10, 18, 0 ), 50 );

    const std::optional<int> first_battery = part_with_flag( *first, "BATTERY" );
    const std::optional<int> second_battery = part_with_flag( *second, "BATTERY" );
    REQUIRE( first_battery );
    REQUIRE( second_battery );

    item cable( itype_id( "test_linkupb_cable" ) );
    REQUIRE( cable.can_link_up() );
    const ret_val<void> first_link = cable.link_to( part_position( *first, *first_battery ),
                                  link_state::vehicle_battery );
    REQUIRE( first_link.success() );
    const ret_val<void> second_link = cable.link_to( part_position( *second, *second_battery ),
                                   link_state::vehicle_battery );
    REQUIRE( second_link.success() );

    // The two-vehicle branch installs the cable parts and resets the item's own link.
    CHECK_FALSE( cable.has_link_data() );
    CHECK( first->has_part( "POWER_TRANSFER" ) );
    CHECK( second->has_part( "POWER_TRANSFER" ) );
    CHECK( first->connected_battery_power_level().first == 150 );
    CHECK( second->connected_battery_power_level().first == 150 );
}

// LB-t5: detaching a cable from the vehicle side (DDA vehicle.cpp:7702 unlink_cables) resets
// the item's link on its next processing turn (DDA process_link last_disconnected check).
TEST_CASE( "linkupb_vehicle_side_detach", "[link][linkup1b]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
    REQUIRE( battery );
    const optional_vpart_position vp = part_position( *veh, *battery );
    REQUIRE( vp );

    item tool( itype_id( "test_linkupb_welder" ) );
    REQUIRE( tool.link_to( vp, link_state::vehicle_battery ).success() );

    // A processed link flags the vehicle part; unlink_cables() is what the vehicle menu calls.
    detached_ptr<item> held = item::spawn( tool );
    held = process_at( std::move( held ), tripoint_bub_ms( 10, 12, 0 ) );
    REQUIRE( held );
    REQUIRE( held->has_link_data() );

    veh->unlink_cables( vp->mount(), get_avatar(), true, false, false );
    held = process_at( std::move( held ), tripoint_bub_ms( 10, 12, 0 ) );
    REQUIRE( held );
    CHECK_FALSE( held->has_link_data() );
}

// L6: connecting within the cable's max_length keeps source/target states; a tool far beyond it
// breaks loose on the next processing turn (DDA process_link check_length).
TEST_CASE( "linkupb_distance_check", "[link][linkup1b]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> port = part_with_flag( *veh, "CABLE_PORTS" );
    REQUIRE( port );
    const optional_vpart_position vp = part_position( *veh, *port );
    REQUIRE( vp );

    item tool( itype_id( "test_linkupb_welder" ) );
    REQUIRE( tool.link_to( vp, link_state::vehicle_port ).success() );
    CHECK( tool.link().source == link_state::no_link );
    CHECK( tool.link().target == link_state::vehicle_port );

    // Two tiles away: within the fixture's 10-tile cable.
    detached_ptr<item> near_tool = item::spawn( tool );
    near_tool = process_at( std::move( near_tool ), tripoint_bub_ms( 10, 12, 0 ) );
    REQUIRE( near_tool );
    CHECK( near_tool->has_link_data() );
    CHECK( near_tool->link().length == 2 );

    // Thirty tiles away: over the cable's length, the link comes loose.
    detached_ptr<item> far_tool = item::spawn( tool );
    far_tool = process_at( std::move( far_tool ), tripoint_bub_ms( 10, 40, 0 ) );
    REQUIRE( far_tool );
    CHECK_FALSE( far_tool->has_link_data() );
    CHECK( far_tool->link_length() == -2 );
}

// L7: the length used by the cable is recorded and a move beyond max_length breaks the link.
// CDF: DDA's link model records the used length in link_data; the cable's own charges stay at
// max_charges (the BN charge-slack model is process_cable(), removed in LINKUP1d).
TEST_CASE( "linkupb_length_limit", "[link][linkup1b]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );

    const std::optional<int> port = part_with_flag( *veh, "CABLE_PORTS" );
    REQUIRE( port );
    const optional_vpart_position vp = part_position( *veh, *port );
    REQUIRE( vp );

    item tool( itype_id( "test_linkupb_welder" ) );
    REQUIRE( tool.max_link_length() == -2 );
    tool.update_link_traits();
    CHECK( tool.max_link_length() == 10 );

    REQUIRE( tool.link_to( vp, link_state::vehicle_port ).success() );
    detached_ptr<item> held = item::spawn( tool );
    held = process_at( std::move( held ), tripoint_bub_ms( 10, 13, 0 ) );
    REQUIRE( held );
    REQUIRE( held->has_link_data() );
    CHECK( held->link().length == 3 );
    CHECK( held->max_link_length() == 10 );

    // Move five more tiles: 8 total is still within 10.
    held = process_at( std::move( held ), tripoint_bub_ms( 10, 18, 0 ) );
    REQUIRE( held );
    REQUIRE( held->has_link_data() );
    CHECK( held->link().length == 8 );

    // Move three more: 11 > 10, the cable breaks loose.
    held = process_at( std::move( held ), tripoint_bub_ms( 10, 21, 0 ) );
    REQUIRE( held );
    CHECK_FALSE( held->has_link_data() );
}

// L8: an extension cable put in the CABLE pocket adds its length to the cable (DDA
// link_extend_cable, update_link_traits), and removing it shrinks the length back
// (DDA remove_extensions).
TEST_CASE( "linkupb_extension_cable", "[link][linkup1b]" )
{
    clear_all_state();

    item cable( itype_id( "test_linkupb_cable" ) );
    REQUIRE( cable.can_link_up() );
    REQUIRE( cable.max_link_length() == -2 );
    cable.update_link_traits();
    const int base_length = cable.max_link_length();
    CHECK( base_length == 10 );

    item extension( itype_id( "test_linkupb_extension" ) );
    REQUIRE( cable.put_in( extension, pocket_type::CABLE ).success() );
    CHECK( cable.cables().size() == 1 );
    cable.update_link_traits();
    CHECK( cable.max_link_length() == base_length + 3 );

    cable.get_contents().clear_pockets_if( []( const item_pocket & pocket ) {
        return pocket.is_type( pocket_type::CABLE );
    } );
    CHECK( cable.cables().empty() );
    cable.update_link_traits();
    CHECK( cable.max_link_length() == base_length );
}

// L9: a tow cable attached to two vehicles sets tow_data on both (DDA link_tow_cable and the
// two-vehicle link_to branch).
TEST_CASE( "linkupb_tow_cable", "[link][linkup1b]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *towing = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );
    vehicle *towed = charged_car( tripoint_bub_ms( 10, 18, 0 ), 100 );

    const auto external_part = []( vehicle & veh ) -> std::optional<int> {
        return find_part( veh, [&veh]( const vehicle_part & p ) {
            return veh.is_external_part( veh.bub_part_location( p ) );
        } );
    };
    const std::optional<int> towing_part = external_part( *towing );
    const std::optional<int> towed_part = external_part( *towed );
    REQUIRE( towing_part );
    REQUIRE( towed_part );

    item tow_cable( itype_id( "test_linkupb_tow_cable" ) );
    REQUIRE( tow_cable.can_link_up() );
    REQUIRE( tow_cable.link_to( part_position( *towing, *towing_part ),
                                link_state::vehicle_tow ).success() );
    // The actor's "attach to the towing vehicle" entry sets the source end.
    tow_cable.link().source = link_state::vehicle_tow;
    tow_cable.link().target = link_state::no_link;
    REQUIRE( tow_cable.link_to( part_position( *towed, *towed_part ),
                                link_state::vehicle_tow ).success() );

    CHECK( towing->has_part( "TOW_CABLE" ) );
    CHECK( towed->has_part( "TOW_CABLE" ) );
    CHECK( towing->is_towing() );
    CHECK( towed->is_towed() );
    CHECK( towing->tow_data.get_towed() == towed );
    CHECK( towed->tow_data.get_towed_by() == towing );
}
