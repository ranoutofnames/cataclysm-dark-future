#include "active_tile_data.h"
#include "active_tile_data_def.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "crafting.h"
#include "distribution_grid.h"
#include "enums.h"
#include "flag.h"
#include "item.h"
#include "iuse_actor.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "recipe.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vpart_position.h"
#include "vehicle/vpart_range.h"
#include "itype_slots/islot_tool.h"

#include <fstream>
#include <optional>
#include <string>
#include <vector>

// CDF: APPL1 tests. Spec: plans/P3/APPL1/brief.md (closes the AUTO-265 / DEFERRED-REGISTER:133
// parked pair). DDA models autoclave and hydraulic_press as disconnected appliance items
// (DDA:data/json/items/appliances.json:185, :212) that become vehicle-appliance parts drawing power
// from a grid or a linked vehicle. CDF has no appliance entity, so the two CDF tools take the DDA
// device shape instead: no battery well, no legacy `magazines`, a `link_up` use_action (the
// appliance port, LINKUP1b/GRIDLINK1) and their power comes from a cabled vehicle or a grid
// connector through a cabled vehicle. The CDF item stats, the AUTOCLAVE iuse, `charges_per_use`
// and the existing recipes stay.

namespace
{

const std::string APPL1_MED = "data/json/items/tool/med.json";
const std::string APPL1_METAL = "data/json/items/tool/metalworking.json";

const itype_id itype_battery( "battery" );
const furn_str_id f_cable_connector( "f_cable_connector" );
const furn_str_id f_battery( "f_battery" );

// CDF: the pinned appliance-port values. DDA has no link_up on the two appliance items, so the
// rate comes from the DDA appliance part: autoclave ap_autoclave `epower` -1500 W
// (DDA:data/json/furniture_and_terrain/appliances.json:993ff); the hydraulic press states no
// epower (its pseudo tool is charge-free), so CDF's own 36 kW motor (the item description) is used.
constexpr int AUTOCLAVE_RATE_MW = 1500000;
constexpr int HYDRAULIC_PRESS_RATE_MW = 36000000;
constexpr int APPLIANCE_CABLE_LENGTH = 2;

// CDF: own record only (no abstracts, no copy-from ancestors), the BATT4 T4 pattern.
auto own_record_has( const std::string &file_name, const std::string &id, const std::string &key )
-> bool
{
    std::ifstream file( file_name, std::ios::binary );
    JsonIn jsin( file );
    for( JsonObject jo : jsin.get_array() ) {
        jo.allow_omitted_members();
        if( jo.has_string( "id" ) && jo.get_string( "id" ) == id && !jo.has_string( "abstract" ) ) {
            return jo.has_member( key );
        }
    }
    return false;
}

auto count_pockets( const itype &type, pocket_type wanted ) -> int
{
    int count = 0;
    for( const pocket_data &pocket : type.pockets ) {
        count += pocket.type == wanted ? 1 : 0;
    }
    return count;
}

auto link_actor( const itype_id &id ) -> const link_up_actor * // *NOPAD*
{
    REQUIRE( id.is_valid() );
    const use_function *use = id->get_use( "link_up" );
    REQUIRE( use != nullptr );
    return static_cast<const link_up_actor *>( use->get_actor_ptr() );
}

// A vehicle with a dashboard (the CABLE_PORTS appliance port) and a 50 kW storage battery.
auto powered_car( const tripoint_bub_ms &pos, int charge ) -> vehicle * // *NOPAD*
{
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "none" ), pos, 0_degrees, 0, 0, false );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "frame_vertical" ), true ) >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "dashboard" ), true ) >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh( 1, 0, 0 ), vpart_id( "frame_vertical" ),
                                true ) >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh( 1, 0, 0 ), vpart_id( "storage_battery" ),
                                true ) >= 0 );
    veh->charge_battery( charge );
    REQUIRE( veh->fuel_left( itype_battery ) == charge );
    here.add_vehicle_to_cache( veh );
    here.build_map_cache( pos.z(), true );
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

// CDF: the DDA appliance port in CDF terms: the vehicle's CABLE_PORTS part (controls), LINKUP1b.
auto link_to_port( item &tool, vehicle &veh ) -> ret_val<void>
{
    const std::optional<int> port = part_with_flag( veh, "CABLE_PORTS" );
    REQUIRE( port );
    return tool.link_to( part_position( veh, *port ), link_state::vehicle_port );
}

auto add_item( avatar &u, const std::string &id, int count = 1 ) -> item & // *NOPAD*
{
    item &added = u.i_add( item::spawn( itype_id( id ), calendar::turn ) );
    for( int i = 1; i < count; i++ ) {
        u.i_add( item::spawn( itype_id( id ), calendar::turn ) );
    }
    return added;
}

// A tile next to the vehicle that no vehicle part occupies, within `reach`.
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
    return from;
}

} // namespace

// A1: the autoclave record drops the CDF battery well and the legacy magazine list and takes the
// DDA appliance link; the CDF AUTOCLAVE iuse, the ammo type and the stats stay.
// Mutation: put `magazine_well`/`magazines` back -> a MAGAZINE_WELL pocket returns and link_up is
// not required by the pocket check; drop the link_up entry -> get_use( "link_up" ) is null.
TEST_CASE( "appl1 autoclave takes the DDA appliance link", "[appl1][p3]" )
{
    const itype_id id( "autoclave" );
    REQUIRE( id.is_valid() );
    CHECK_FALSE( own_record_has( APPL1_MED, "autoclave", "magazines" ) );
    CHECK_FALSE( own_record_has( APPL1_MED, "autoclave", "magazine_well" ) );
    CHECK_FALSE( own_record_has( APPL1_MED, "autoclave", "max_charges" ) );
    CHECK_FALSE( own_record_has( APPL1_MED, "autoclave", "initial_charges" ) );

    const itype &type = id.obj();
    CHECK( type.magazines.empty() );
    CHECK( count_pockets( type, pocket_type::MAGAZINE_WELL ) == 0 );
    CHECK( count_pockets( type, pocket_type::MAGAZINE ) == 0 );
    // CDF functionality stays: the sterilizing iuse and the battery ammo type
    CHECK( type.get_use( "AUTOCLAVE" ) != nullptr );
    REQUIRE( type.tool );
    CHECK( type.tool->ammo_id.contains( ammotype( "battery" ) ) );
    // DDA weight/volume/price are CDF's (AUTO-236: no balance port)
    CHECK( type.weight == 34500_gram );

    const link_up_actor *actor = link_actor( id );
    CHECK( actor->cable_length == APPLIANCE_CABLE_LENGTH );
    CHECK( actor->charge_rate == AUTOCLAVE_RATE_MW );

    item tool( id, calendar::turn );
    REQUIRE( tool.can_link_up() );
    tool.update_link_traits();
    CHECK( tool.link().max_length == APPLIANCE_CABLE_LENGTH );
    CHECK( tool.link().charge_rate == AUTOCLAVE_RATE_MW );
}

// A2: the hydraulic press follows the same shape and keeps its CDF power cost (20 charges/use) and
// its CDF recipe role (the item is the `hydraulic_press` tool of the casing recipes).
// Mutation: restore `magazines` or drop the link_up entry.
TEST_CASE( "appl1 hydraulic press takes the DDA appliance link", "[appl1][p3]" )
{
    const itype_id id( "hydraulic_press" );
    REQUIRE( id.is_valid() );
    CHECK_FALSE( own_record_has( APPL1_METAL, "hydraulic_press", "magazines" ) );
    CHECK_FALSE( own_record_has( APPL1_METAL, "hydraulic_press", "magazine_well" ) );
    CHECK_FALSE( own_record_has( APPL1_METAL, "hydraulic_press", "max_charges" ) );
    CHECK_FALSE( own_record_has( APPL1_METAL, "hydraulic_press", "initial_charges" ) );

    const itype &type = id.obj();
    CHECK( type.magazines.empty() );
    CHECK( count_pockets( type, pocket_type::MAGAZINE_WELL ) == 0 );
    CHECK( count_pockets( type, pocket_type::MAGAZINE ) == 0 );
    REQUIRE( type.tool );
    CHECK( type.tool->ammo_id.contains( ammotype( "battery" ) ) );
    // CDF: the press is a charged recipe tool (AUTO-239 Q-DRAIN keeps the CDF per-use drain)
    CHECK( type.charges_to_use() == 20 );
    CHECK( type.weight == 320_kilogram );

    const link_up_actor *actor = link_actor( id );
    CHECK( actor->cable_length == APPLIANCE_CABLE_LENGTH );
    CHECK( actor->charge_rate == HYDRAULIC_PRESS_RATE_MW );

    item tool( id, calendar::turn );
    REQUIRE( tool.can_link_up() );
    tool.update_link_traits();
    CHECK( tool.link().max_length == APPLIANCE_CABLE_LENGTH );
    CHECK( tool.link().charge_rate == HYDRAULIC_PRESS_RATE_MW );
}

// A3: the autoclave runs its CBM craft only when cabled to a vehicle battery. DDA's appliance
// autoclave draws from the appliance power; CDF bridges that through the link-up model (LINKUP1a/b).
// The CBM recipe needs 5000 autoclave charges, so the linked vehicle must supply them.
// Mutation: keep the old well -> the uncabled item is "sufficient" once a cell is loaded, and the
// linked case never reaches charges_of.
TEST_CASE( "appl1 autoclave craft draws from a cabled vehicle", "[appl1][p3]" )
{
    clear_all_state();
    clear_avatar();
    build_test_map( ter_id( "t_pavement" ) );
    avatar &u = get_avatar();
    map &here = get_map();
    vehicle *veh = powered_car( tripoint_bub_ms( 10, 10, 0 ), 6000 );

    item &autoclave = add_item( u, "autoclave" );
    REQUIRE( autoclave.can_link_up() );
    // uncabled: no power at all
    CHECK( autoclave.ammo_remaining_linked( here, &u ) == 0 );
    CHECK_FALSE( autoclave.ammo_sufficient( &u, 5000 ) );

    // the soldering half of the recipe is self-contained (integral 500 conc_alcohol charges)
    add_item( u, "soldering_ethanol" );
    add_item( u, "solder_wire" );
    // qualities SCREW_FINE / WRENCH_FINE (plain qualities, no charges)
    add_item( u, "large_repairkit" );
    add_item( u, "element", 2 );
    add_item( u, "amplifier", 2 );
    add_item( u, "circuit" );
    add_item( u, "cable", 6 );
    add_item( u, "burnt_out_bionic" );
    add_item( u, "pouch_autoclave" );
    for( int i = 0; i < 4; i++ ) {
        u.i_add( item::in_its_container( item::spawn( itype_id( "water" ), calendar::turn, 8 ) ) );
    }

    const recipe_id rid( "bio_lighter" );
    REQUIRE( rid.is_valid() );
    const recipe &rec = rid.obj();
    u.set_skill_level( rec.skill_used, std::max( rec.difficulty, 2 ) );
    for( const auto &[skill, level] : rec.required_skills ) {
        u.set_skill_level( skill, level );
    }
    u.learn_recipe( &rec );

    u.invalidate_crafting_inventory();
    CHECK_FALSE( u.can_make( &rec, 1 ) );

    REQUIRE( link_to_port( autoclave, *veh ).success() );
    CHECK( autoclave.ammo_remaining_linked( here, &u ) == 6000 );

    u.invalidate_crafting_inventory();
    CHECK( u.crafting_inventory().charges_of( itype_id( "autoclave" ), 5000 ) >= 5000 );
    REQUIRE( u.can_make( &rec, 1 ) );

    u.make_craft( rid, 1 );
    REQUIRE( u.activity );
    CHECK( u.activity->id() == activity_id( "ACT_CRAFT" ) );
    u.cancel_activity();
}

// A4: the hydraulic press runs its casing craft only when cabled to a vehicle battery; the CDF
// recipe (223_casing, 10 press charges) and the CDF charge cost stay.
// Mutation: restore the CDF well -> the uncabled press reports the cell's charges.
TEST_CASE( "appl1 hydraulic press craft draws from a cabled vehicle", "[appl1][p3]" )
{
    clear_all_state();
    clear_avatar();
    build_test_map( ter_id( "t_pavement" ) );
    avatar &u = get_avatar();
    map &here = get_map();
    vehicle *veh = powered_car( tripoint_bub_ms( 10, 10, 0 ), 200 );

    item &press = add_item( u, "hydraulic_press" );
    REQUIRE( press.can_link_up() );
    CHECK( press.ammo_remaining_linked( here, &u ) == 0 );
    CHECK_FALSE( press.ammo_sufficient( &u, 10 ) );

    // CASING_FORMING 1
    add_item( u, "large_repairkit" );
    add_item( u, "cartridgebrass", 5 );
    // the recipe's second tool group is forging_standard (CONTAIN_HOT 1 + a charged forge);
    // the electric forge is itself a well-less LINKUP1c tool, so it is cabled to the same vehicle
    add_item( u, "crucible_clay" );
    item &forge = add_item( u, "forge" );
    REQUIRE( link_to_port( forge, *veh ).success() );

    const recipe_id rid( "223_casing" );
    REQUIRE( rid.is_valid() );
    const recipe &rec = rid.obj();
    u.set_skill_level( rec.skill_used, std::max( rec.difficulty, 2 ) );
    u.learn_recipe( &rec );

    u.invalidate_crafting_inventory();
    CHECK_FALSE( u.can_make( &rec, 1 ) );

    REQUIRE( link_to_port( press, *veh ).success() );
    CHECK( press.ammo_remaining_linked( here, &u ) == 200 );
    u.invalidate_crafting_inventory();
    REQUIRE( u.can_make( &rec, 1 ) );

    u.make_craft( rid, 1 );
    REQUIRE( u.activity );
    CHECK( u.activity->id() == activity_id( "ACT_CRAFT" ) );
    u.cancel_activity();
}

// A5: the grid connector reaches the appliance through a cabled vehicle (GRIDLINK1). The vehicle
// battery is empty; the grid battery tile supplies the autoclave's charges.
// Mutation: drop the grid link -> the discharge has nothing to draw and the consume fails.
TEST_CASE( "appl1 appliance drafts from a grid connector", "[appl1][p3]" )
{
    clear_all_state();
    clear_avatar();
    build_test_map( ter_id( "t_pavement" ) );
    avatar &u = get_avatar();
    map &here = get_map();
    vehicle *veh = powered_car( tripoint_bub_ms( 10, 10, 0 ), 0 );

    const std::optional<int> battery = part_with_flag( *veh, "BATTERY" );
    REQUIRE( battery );
    const tripoint_bub_ms connector_bub = free_tile_near_point( veh->bub_ms_location(), 2 );
    const tripoint_abs_ms connector_abs = bub_to_abs( connector_bub );
    here.furn_set( connector_bub, f_cable_connector );
    vehicle_connector_tile *connector =
        active_tiles::furn_at<vehicle_connector_tile>( connector_abs );
    REQUIRE( connector != nullptr );

    const tripoint_bub_ms grid_battery_bub = free_tile_near_point( connector_bub, 2 );
    here.furn_set( grid_battery_bub, f_battery );
    battery_tile *grid_battery = active_tiles::furn_at<battery_tile>( bub_to_abs( grid_battery_bub ) );
    REQUIRE( grid_battery != nullptr );
    REQUIRE( grid_battery->mod_resource( 5000 ) == 0 );
    distribution_grid &grid = get_distribution_grid_tracker().grid_at( connector_abs );
    REQUIRE( !grid.empty() );

    item cable( itype_id( "jumper_cable" ) );
    REQUIRE( cable.link_to( part_position( *veh, *battery ),
                            link_state::vehicle_battery ).success() );
    REQUIRE( cable.link_to( connector_abs, link_state::vehicle_grid ).success() );

    item &autoclave = add_item( u, "autoclave" );
    REQUIRE( link_to_port( autoclave, *veh ).success() );
    // the vehicle's own battery is empty; the grid is not part of the vehicle charge sum
    CHECK( autoclave.ammo_remaining_linked( here, &u ) == 0 );

    const int consumed = autoclave.ammo_consume( 5000, tripoint_bub_ms( 10, 10, 0 ), &u );
    CHECK( consumed == 5000 );
    CHECK( veh->fuel_left( itype_battery ) == 0 );
    CHECK( grid.get_resource() == 0 );
}

// A6: the CDF vehicle appliance parts keep working with the well-less tool (the "vehicle autoclave
// craft" path): the pseudo tool the map hands to the crafting inventory is linked to the vehicle
// battery instead of being loaded with ammo_set into a well it no longer has.
// Mutation: keep the 4-b1 ammo_set only -> the pseudo autoclave has no link and no charges.
TEST_CASE( "appl1 vehicle autoclave part powers its tool", "[appl1][p3]" )
{
    clear_all_state();
    clear_avatar();
    build_test_map( ter_id( "t_pavement" ) );
    avatar &u = get_avatar();
    map &here = get_map();

    const tripoint_bub_ms vehicle_pos( 60, 60, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "none" ), vehicle_pos, 0_degrees, 0, 0, false );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "frame_vertical" ), true ) >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "autoclave" ), true ) >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh( 1, 0, 0 ), vpart_id( "frame_vertical" ),
                                true ) >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh( 1, 0, 0 ), vpart_id( "storage_battery" ),
                                true ) >= 0 );
    veh->charge_battery( 6000 );
    here.add_vehicle_to_cache( veh );
    here.build_map_cache( vehicle_pos.z(), true );
    u.setpos( vehicle_pos + tripoint( 0, 1, 0 ) );

    u.invalidate_crafting_inventory();
    const temp_crafting_inventory &inv = u.crafting_inventory();
    bool found = false;
    inv.visit_items( [&]( const item * it ) {
        if( it->typeId() != itype_id( "autoclave" ) ) {
            return VisitResponse::NEXT;
        }
        found = true;
        CHECK( it->has_link_data() );
        CHECK( it->ammo_remaining_linked( here, &u ) == 6000 );
        return VisitResponse::ABORT;
    } );
    CHECK( found );
    CHECK( inv.charges_of( itype_id( "autoclave" ), 5000 ) == 5000 );
}
