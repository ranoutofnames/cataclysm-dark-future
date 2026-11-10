#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/vehicle.h"

#include <sstream>
#include <string>

// CDF: LINKUP1a tests. Spec: PL-33e TASK-draft-LINKUP1-v4.md, LINKUP1a test table L1-L5.
// The fixtures are test_linkup_welder / test_linkup_plain_tool in
// TEST_DATA/linkup1a.json. The linked half of L2/L3/L4 needs the link API itself, so this file
// does not compile before LINKUP1a (that is the red-first run).

namespace
{

auto save_item( const item &it ) -> std::string
{
    std::ostringstream buffer;
    JsonOut jsout( buffer );
    it.serialize( jsout );
    return buffer.str();
}

// Links `tool` to the vehicle's battery the way link_to() does (item_gun_tool_ammo.cpp).
auto link_tool_to( item &tool, vehicle &veh ) -> void
{
    tool.link().target = link_state::vehicle_battery;
    tool.link().t_veh = &veh;
    tool.link().t_abs_pos = bub_to_abs( veh.bub_ms_location() );
    tool.link().t_mount = tripoint_mnt_veh::zero();
    tool.update_link_traits();
    tool.link().length = 1;
}

auto charged_car() -> vehicle *
{
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "car" ), tripoint_bub_ms( 10, 10, 0 ), 0_degrees, 0, 0,
                                     false );
    REQUIRE( veh != nullptr );
    veh->charge_battery( 100, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ) ) == 100 );
    return veh;
}

} // namespace

// L1: link_data serialization round trip keeps source/target state, max_length, charge_rate,
// efficiency and t_abs_pos.
TEST_CASE( "link_data_serialization_round_trip", "[link][linkup1a]" )
{
    clear_all_state();

    item tool( itype_id( "test_linkup_welder" ) );
    REQUIRE( tool.can_link_up() );

    item::link_data &link = tool.link();
    link.source = link_state::bio_cable;
    link.target = link_state::vehicle_port;
    link.t_abs_pos = tripoint_abs_ms( 123, 456, 0 );
    link.t_mount = tripoint_mnt_veh( 1, 2, 0 );
    link.length = 4;
    link.max_length = 7;
    link.charge_rate = 1234;
    link.efficiency = 0.5f;
    link.charge_interval = 99;

    const std::string saved = save_item( tool );
    CAPTURE( saved );
    REQUIRE( saved.find( "\"link_data\"" ) != std::string::npos );

    std::istringstream buffer( saved );
    JsonIn jsin( buffer );
    detached_ptr<item> loaded = item::spawn( jsin );
    REQUIRE( loaded );
    REQUIRE( loaded->has_link_data() );

    const item::link_data &restored = loaded->link();
    CHECK( restored.source == link_state::bio_cable );
    CHECK( restored.target == link_state::vehicle_port );
    CHECK( restored.t_abs_pos == tripoint_abs_ms( 123, 456, 0 ) );
    CHECK( restored.t_mount == tripoint_mnt_veh( 1, 2, 0 ) );
    CHECK( restored.length == 4 );
    CHECK( restored.max_length == 7 );
    CHECK( restored.charge_rate == 1234 );
    CHECK( restored.efficiency == Approx( 0.5f ) );
    CHECK( restored.charge_interval == 99 );
}

// L2: ammo_remaining( include_linked ) of a linked tool counts the linked vehicle's battery
// (DDA item_gun_tool_ammo.cpp:1474-1484).
TEST_CASE( "linked_tool_reads_vehicle_battery", "[link][linkup1a]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *veh = charged_car();

    item tool( itype_id( "test_linkup_welder" ) );
    REQUIRE_FALSE( tool.has_link_data() );
    CHECK( tool.ammo_remaining_linked( here, nullptr ) == 0 );

    link_tool_to( tool, *veh );
    CHECK( tool.link_length() == 1 );
    CHECK( tool.ammo_remaining_linked( here, nullptr ) == 100 );
    // The plain accessor keeps the DDA contract: only include_linked sees the cable.
    CHECK( tool.ammo_remaining( nullptr ) == 0 );
    CHECK( tool.ammo_capacity( ammotype( "battery" ), true ) ==
           veh->battery_power_level().second );
}

// L3: ammo_consume() on a linked tool discharges the vehicle battery and leaves the tool's own
// charges alone (DDA:1861-1870).
TEST_CASE( "linked_tool_consume_discharges_vehicle", "[link][linkup1a]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *veh = charged_car();

    item tool( itype_id( "test_linkup_welder" ) );
    link_tool_to( tool, *veh );

    const int consumed = tool.ammo_consume( 10, tripoint_bub_ms( 10, 10, 0 ), nullptr );
    CHECK( consumed == 10 );
    CHECK( veh->fuel_left( itype_id( "battery" ) ) == 90 );
    CHECK( tool.charges == 0 );
    CHECK( tool.ammo_remaining( nullptr ) == 0 );
    CHECK( tool.ammo_remaining_linked( here, nullptr ) == 90 );
}

// L4: an unlinked link_up tool reports 0 charges and refuses use (ammo_sufficient,
// DDA:1805); linking it to a charged battery makes it sufficient.
TEST_CASE( "unlinked_tool_has_no_charges", "[link][linkup1a]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *veh = charged_car();

    item tool( itype_id( "test_linkup_welder" ) );
    CHECK( tool.ammo_remaining_linked( here, nullptr ) == 0 );
    CHECK_FALSE( tool.ammo_sufficient( nullptr, 1 ) );

    link_tool_to( tool, *veh );
    CHECK( tool.ammo_sufficient( nullptr, 1 ) );
}

// L5: can_link_up() is true only for an item whose use_action has a link_up actor
// (DDA:3610-3647).
TEST_CASE( "can_link_up_needs_the_action", "[link][linkup1a]" )
{
    clear_all_state();

    item tool( itype_id( "test_linkup_welder" ) );
    item plain( itype_id( "test_linkup_plain_tool" ) );

    CHECK( tool.can_link_up() );
    CHECK_FALSE( tool.has_link_data() );
    CHECK_FALSE( plain.can_link_up() );

    // The actor loaded the fixture's cable_length and charge_rate ("800 W" -> 800000 mW), and
    // update_link_traits() copies them into the link data (DDA item_gun_tool_ammo.cpp:3850-3885).
    tool.update_link_traits();
    CHECK( tool.link().max_length == 3 );
    CHECK( tool.link().charge_rate == 800000 );
    CHECK( tool.link().efficiency == Approx( 0.85f ) );
}
