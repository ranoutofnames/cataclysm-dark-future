#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "flag.h"
#include "item.h"
#include "iuse_actor.h"
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
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

// CDF: LINKUP1d tests. Spec: PL-33e TASK-draft-LINKUP1-v4.md, LINKUP1d test table L13a-L13c.
// L13a scans the files the BN cable system lived in, L13b pins the four converted cable items and
// their vehicle parts against their DDA twins, L13c drives the Cable Charger System through the
// link states (DDA character get_cable_ups / get_cable_vehicle). The fixtures are in
// TEST_DATA/linkup1d.json.

namespace
{

const flag_id flag_single_use( "SINGLE_USE" );

// The BN cable system's symbols. L13a requires none of them in src/ or data/ any more.
const std::vector<std::string> bn_cable_symbols = {
    "cable_connection_data", "process_cable", "CABLE_ATTACH", "TOW_ATTACH"
};

const std::vector<std::string> bn_cable_files = {
    "src/item.cpp",
    "src/item.h",
    "src/iuse.cpp",
    "src/iuse.h",
    "src/bionics.cpp",
    "src/bionics_ui.cpp",
    "src/character.h",
    "src/item_factory.cpp",
    "src/vehicle/vehicle_part.cpp",
    "src/vehicle/vehicle_part.h",
    "src/vehicle/vehicle.cpp",
    "data/json/item_actions.json",
    "data/json/items/vehicle/cables.json",
    "data/json/vehicleparts/cables.json"
};

auto read_file( const std::string &path ) -> std::string
{
    std::ifstream file( path, std::ios::binary );
    REQUIRE( file.good() );
    return std::string( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
}

auto link_actor( const itype_id &id ) -> const link_up_actor * // *NOPAD*
{
    REQUIRE( id.is_valid() );
    const use_function *use = id->get_use( "link_up" );
    REQUIRE( use != nullptr );
    return static_cast<const link_up_actor *>( use->get_actor_ptr() );
}

auto charged_car( const tripoint_bub_ms &pos, int charge ) -> vehicle * // *NOPAD*
{
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "car" ), pos, 0_degrees, 0, 0, false );
    REQUIRE( veh != nullptr );
    veh->charge_battery( charge, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ) ) == charge );
    return veh;
}

auto part_with_flag( vehicle &veh, const std::string &flag ) -> std::optional<int>
{
    for( const vpart_reference &vp : veh.get_all_parts() ) {
        if( vp.part().removed || vp.part().is_broken() ) {
            continue;
        }
        if( vp.part().info().has_flag( flag ) ) {
            return static_cast<int>( vp.part_index() );
        }
    }
    return std::nullopt;
}

auto part_position( vehicle &veh, int part_index ) -> optional_vpart_position
{
    return get_map().veh_at( veh.bub_part_location( part_index ) );
}

} // namespace

// L13a: the BN cable system is gone from the files that carried it. Mutation: leave one
// reference (e.g. restore CABLE_ATTACH in item_actions.json) -> this fails.
TEST_CASE( "linkupd_bn_cable_system_is_gone", "[link][linkup1d]" )
{
    for( const std::string &path : bn_cable_files ) {
        INFO( "file " << path );
        const std::string contents = read_file( path );
        for( const std::string &symbol : bn_cable_symbols ) {
            INFO( "symbol " << symbol );
            CHECK( contents.find( symbol ) == std::string::npos );
        }
    }
}

// L13b: the four BN cable items equal their DDA twins in use_action, flags and qualities, with
// the CDF cable length (max_charges); the four parts equal the DDA parts; extension_cable and
// long_extension_cable exist. Mutation: restore `use_action: "CABLE_ATTACH"` on jumper_cable.
TEST_CASE( "linkupd_cable_items_match_their_dda_twins", "[link][linkup1d]" )
{
    // jumper_cable: DDA data/json/items/tool/cables.json.
    const link_up_actor *jumper = link_actor( itype_id( "jumper_cable" ) );
    CHECK( jumper->cable_length == -1 );
    CHECK( jumper->efficiency == Approx( 0.85f ) );
    CHECK( jumper->targets == std::set<link_state>{
        link_state::no_link, link_state::bio_cable, link_state::ups, link_state::solarpack,
        link_state::vehicle_battery
    } );
    CHECK( jumper->can_extend == std::set<std::string>{ "jumper_cable", "jumper_cable_heavy" } );
    CHECK( itype_id( "jumper_cable" )->maximum_charges() == 3 );
    CHECK( itype_id( "jumper_cable" )->has_flag( flag_CABLE_SPOOL ) );
    CHECK( itype_id( "jumper_cable" )->has_flag( flag_single_use ) );
    CHECK_FALSE( itype_id( "jumper_cable" )->has_flag( flag_DESTROY_ON_DECHARGE ) );
    CHECK( itype_id( "jumper_cable" )->qualities.at( quality_id( "JUMPSTART" ) ) == 1 );

    // jumper_cable_heavy copies the parent's action and keeps the CDF length.
    const link_up_actor *heavy = link_actor( itype_id( "jumper_cable_heavy" ) );
    CHECK( heavy->targets == jumper->targets );
    CHECK( heavy->can_extend == jumper->can_extend );
    CHECK( itype_id( "jumper_cable_heavy" )->maximum_charges() == 20 );

    // hd_tow_cable: tow target, no charging efficiency, ROPE 2.
    const link_up_actor *tow = link_actor( itype_id( "hd_tow_cable" ) );
    CHECK( tow->targets == std::set<link_state>{ link_state::no_link, link_state::vehicle_tow } );
    CHECK( tow->efficiency == Approx( 0.0f ) );
    CHECK( tow->can_extend.empty() );
    CHECK( itype_id( "hd_tow_cable" )->maximum_charges() == 6 );
    CHECK( itype_id( "hd_tow_cable" )->has_flag( flag_TOW_CABLE ) );
    CHECK( itype_id( "hd_tow_cable" )->has_flag( flag_single_use ) );
    CHECK( itype_id( "hd_tow_cable" )->has_flag( flag_NO_SALVAGE ) );
    CHECK_FALSE( itype_id( "hd_tow_cable" )->has_flag( flag_DESTROY_ON_DECHARGE ) );
    CHECK( itype_id( "hd_tow_cable" )->qualities.at( quality_id( "ROPE" ) ) == 2 );

    // jumper_cable_debug: its own action, efficiency 1.0, the debug target set.
    const link_up_actor *debug = link_actor( itype_id( "jumper_cable_debug" ) );
    CHECK( debug->efficiency == Approx( 1.0f ) );
    CHECK( debug->targets == std::set<link_state>{
        link_state::no_link, link_state::bio_cable, link_state::ups, link_state::solarpack,
        link_state::vehicle_port, link_state::vehicle_battery, link_state::vehicle_tow
    } );
    CHECK( debug->can_extend == std::set<std::string>{ "ELECTRICAL_DEVICES", "jumper_cable_debug" } );
    CHECK( itype_id( "jumper_cable_debug" )->maximum_charges() == 50 );

    // extension_cable / long_extension_cable: new items, DDA stats.
    const link_up_actor *extension = link_actor( itype_id( "extension_cable" ) );
    CHECK( extension->targets == std::set<link_state>{
        link_state::no_link, link_state::vehicle_port
    } );
    CHECK( extension->can_extend == std::set<std::string>{
        "ELECTRICAL_DEVICES", "extension_cable", "long_extension_cable"
    } );
    CHECK( itype_id( "extension_cable" )->maximum_charges() == 10 );
    CHECK( link_actor( itype_id( "long_extension_cable" ) )->targets == extension->targets );
    CHECK( itype_id( "long_extension_cable" )->maximum_charges() == 30 );

    // The vehicle parts: DDA data/json/vehicleparts/vehicle_parts.json:2380-2479.
    const auto part_has = []( const vpart_id & id, const std::string & flag ) {
        return id->has_flag( flag );
    };
    for( const std::string &cable_part : { "jumper_cable", "jumper_cable_heavy",
                                          "jumper_cable_debug", "extension_cable", "long_extension_cable" } ) {
        INFO( "part " << cable_part );
        const vpart_id id( cable_part );
        REQUIRE( id.is_valid() );
        CHECK( part_has( id, "NO_INSTALL_HIDDEN" ) );
        CHECK( part_has( id, "UNMOUNT_ON_DAMAGE" ) );
        CHECK( part_has( id, "POWER_TRANSFER" ) );
        CHECK_FALSE( part_has( id, "UNMOUNT_ON_MOVE" ) );
        CHECK_FALSE( part_has( id, "NOINSTALL" ) );
        CHECK( id->durability == 120 );
        CHECK( id->item == itype_id( cable_part ) );
    }
    CHECK( vpart_id( "hd_tow_cable" )->has_flag( "TOW_CABLE" ) );
    CHECK_FALSE( vpart_id( "hd_tow_cable" )->has_flag( "POWER_TRANSFER" ) );
    CHECK( vpart_id( "jumper_cable_heavy" )->epower == 5000 );
}

// L13c: a character with bio_cable and a cable linked to a vehicle battery (or a UPS) is powered
// through the DDA path (get_cable_vehicle / get_cable_ups). Mutation: make get_cable_ups return
// empty -> the UPS section fails; drop get_cable_vehicle -> the vehicle section fails.
TEST_CASE( "linkupd_bionic_cable_reads_the_link_states", "[link][linkup1d]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    avatar &you = get_avatar();
    you.add_bionic( bionic_id( "bio_cable" ) );
    REQUIRE( you.get_remote_fueled_bionic() == bionic_id( "bio_cable" ) );

    SECTION( "a vehicle battery" ) {
        vehicle *veh = charged_car( tripoint_bub_ms( 10, 10, 0 ), 100 );
        const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
        REQUIRE( battery );

        item &cable = you.i_add( item::spawn( itype_id( "jumper_cable" ) ) );
        REQUIRE( cable.link_to( part_position( *veh, *battery ), link_state::vehicle_battery ).success() );
        cable.link().source = link_state::bio_cable;

        const std::vector<vehicle *> linked = you.get_cable_vehicle();
        REQUIRE( linked.size() == 1 );
        CHECK( linked.front() == veh );
        CHECK( you.get_cable_ups().empty() );

        CHECK( you.find_remote_fuel( true ) == itype_id( "battery" ) );
        CHECK( you.consume_remote_fuel( 10_kJ ) == 0_J );
        CHECK( veh->connected_battery_power_level().first == 90 );
    }

    SECTION( "a UPS" ) {
        item &ups = you.i_add( item::spawn( itype_id( "test_linkupd_ups" ) ) );
        REQUIRE( ups.ammo_remaining() > 10 );
        ups.set_var( "cable", "plugged_in" );

        item &cable = you.i_add( item::spawn( itype_id( "jumper_cable" ) ) );
        cable.link().source = link_state::ups;
        cable.link().target = link_state::bio_cable;
        cable.update_link_traits();

        const std::vector<item *> upses = you.get_cable_ups();
        REQUIRE( upses.size() == 1 );
        CHECK( upses.front() == &ups );
        CHECK( you.get_cable_vehicle().empty() );

        const int before = ups.ammo_remaining();
        CHECK( you.find_remote_fuel( true ) == itype_id( "battery" ) );
        CHECK( you.consume_remote_fuel( 10_kJ ) == 0_J );
        CHECK( ups.ammo_remaining() == before - 10 );
    }

    SECTION( "a solar pack" ) {
        item &solar = you.i_add( item::spawn( itype_id( "test_linkupd_solar" ) ) );
        solar.set_var( "cable", "plugged_in" );

        item &cable = you.i_add( item::spawn( itype_id( "jumper_cable" ) ) );
        cable.link().source = link_state::solarpack;
        cable.link().target = link_state::bio_cable;
        cable.update_link_traits();

        const std::vector<item *> packs = you.get_cable_solar();
        REQUIRE( packs.size() == 1 );
        CHECK( packs.front() == &solar );
    }
}
