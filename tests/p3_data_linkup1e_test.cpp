#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "enum_conversions.h"
#include "item.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/vehicle.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// CDF: LINKUP1e tests. Spec: PL-33e TASK-draft-LINKUP1-v4.md, LINKUP1e test table L14-L15.
// The 45 tools of the TSV group `other` keep their CDF wells and battery pockets and gain the DDA
// `link_up` action as a second power source. The rows are in
// tests/data/dda_staged/linkup1e_rows.json, re-derived from DDA b4b62254ae2.
//
// L14 has two halves: the loaded action must equal the row (cable_length, charge_rate, menu_text
// and the DDA defaults efficiency 0.85 / targets {no_link, vehicle_port}), and the pocket list must
// stay what it is today. The pocket signature is read from the loaded item and leaves out the MOD
// and CABLE pockets the loader adds itself (src/item_factory.cpp add_cable_pocket, LINKUP1b), as
// the p3 golden comparator does.
//
// L15 runs L2 on real data: a camera with an empty battery and a link to a charged car reads the
// car's battery level and consumes from it. The link fields are set the way
// tests/p3_linkup1a_test.cpp sets them for its fixture.

namespace
{

const std::string LINKUP1E_ROWS = "tests/data/dda_staged/linkup1e_rows.json";

struct linkup_row {
    std::string id;
    std::string file;
    std::string pockets;
    int cable_length = 0;
    int charge_rate_mw = 0;
    bool has_menu_text = false;
    std::string menu_text;
};

auto read_rows() -> std::vector<linkup_row>
{
    std::ifstream file( LINKUP1E_ROWS, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object( "items" );
    items.allow_omitted_members();
    std::vector<linkup_row> out;
    for( const JsonMember member : items ) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        linkup_row r;
        r.id = member.name();
        r.file = row.get_string( "file" );
        r.pockets = row.get_string( "pockets" );
        r.cable_length = row.get_int( "cable_length" );
        r.charge_rate_mw = row.get_int( "charge_rate_mw" );
        // CDF: menu_text lives in the DDA action object, not in a row-level key
        JsonObject action = row.get_object( "action" );
        action.allow_omitted_members();
        if( action.has_string( "menu_text" ) ) {
            r.has_menu_text = true;
            r.menu_text = action.get_string( "menu_text" );
        }
        out.push_back( r );
    }
    return out;
}

auto pocket_signature( const itype &type ) -> std::string
{
    std::ostringstream os;
    // CDF: the hash must not depend on the global C++ locale (localized_compare sets classic, the
    // startup locale groups digits); the stored hashes are the classic-locale form.
    os.imbue( std::locale::classic() );
    // The legacy magazines table (what the loader synthesizes a well from, when present).
    os << "magazines:";
    for( const auto &[ammo, ids] : type.magazines ) {
        os << ammo.str() << "=";
        for( const itype_id &id : ids ) {
            os << id.str() << ",";
        }
        os << ";";
    }
    os << " pockets:";
    for( const pocket_data &p : type.pockets ) {
        // CDF: the loader adds these itself (MOD1 / LINKUP1b), they are not JSON of the item.
        if( p.type == pocket_type::MOD || p.type == pocket_type::CABLE ) {
            continue;
        }
        os << "[" << io::enum_to_string( p.type ) << " rigid=" << ( p.rigid ? 1 : 0 );
        os << " ammo=";
        for( const auto &[ammo, n] : p.ammo_restriction ) {
            os << ammo.str() << ":" << n << ",";
        }
        os << " items=";
        for( const itype_id &id : p.item_id_restriction ) {
            os << id.str() << ",";
        }
        os << " flags=";
        for( const flag_id &f : p.get_flag_restrictions() ) {
            os << f.str() << ",";
        }
        os << " default=" << p.default_magazine.str();
        os << " well=" << units::to_milliliter( p.magazine_well );
        os << " vol=" << units::to_milliliter( p.raw_volume_capacity );
        os << " maxw=" << units::to_gram( p.max_contains_weight );
        os << " maxvol=" << ( p.max_item_volume ? units::to_milliliter( *p.max_item_volume ) : -1 );
        os << " maxlen=" << units::to_millimeter( p.max_item_length );
        os << "]";
    }
    return os.str();
}

// CDF: L14b pins the pocket list as a hash of the full signature (the 45 rows are too long to
// store as text without wrapping in the test output); the signature itself is printed on failure.
auto pocket_hash( const itype &type ) -> std::string
{
    const std::string sig = pocket_signature( type );
    uint64_t h = 1469598103934665603ULL;
    for( const unsigned char c : sig ) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char out[17];
    std::snprintf( out, sizeof( out ), "%016llx", static_cast<unsigned long long>( h ) );
    return out;
}

auto count_pockets( const itype &type, pocket_type t ) -> int
{
    int n = 0;
    for( const pocket_data &p : type.pockets ) {
        n += p.type == t ? 1 : 0;
    }
    return n;
}

// Links `tool` to the vehicle's battery the way link_to() does (item_gun_tool_ammo.cpp), as in
// tests/p3_linkup1a_test.cpp.
auto link_tool_to( item &tool, vehicle &veh ) -> void
{
    tool.link().target = link_state::vehicle_battery;
    tool.link().t_veh = &veh;
    tool.link().t_abs_pos = bub_to_abs( veh.bub_ms_location() );
    tool.link().t_mount = tripoint_mnt_veh::zero();
    tool.update_link_traits();
    tool.link().length = 1;
}

} // namespace

// L14a: every id of the TSV group `other` carries the DDA link_up action of its row.
TEST_CASE( "linkup1e rows carry the tsv link_up action", "[link][linkup1e]" )
{
    clear_all_state();
    const std::vector<linkup_row> rows = read_rows();
    std::cout << "linkup1e rows: " << rows.size() << std::endl;
    REQUIRE( rows.size() == 45 );
    int with_menu_text = 0;
    for( const linkup_row &row : rows ) {
        CAPTURE( row.id, row.file );
        REQUIRE( itype_id( row.id ).is_valid() );
        const itype &type = itype_id( row.id ).obj();
        const use_function *use = type.get_use( "link_up" );
        REQUIRE( use != nullptr );
        const auto *actor = dynamic_cast<const link_up_actor *>( use->get_actor_ptr() );
        REQUIRE( actor != nullptr );
        CHECK( actor->cable_length == row.cable_length );
        CHECK( actor->charge_rate == row.charge_rate_mw );
        if( row.has_menu_text ) {
            with_menu_text++;
            CHECK( actor->menu_text.translated() == row.menu_text );
        } else {
            CHECK( actor->menu_text.empty() );
        }
        // CDF: DDA's defaults for a row that states neither key (iuse_actor.cpp link_up_actor::load)
        CHECK( actor->efficiency == Approx( 0.85f ) );
        CHECK( actor->move_cost == 5 );
        CHECK( actor->targets == std::set<link_state>( { link_state::no_link, link_state::vehicle_port } ) );

        item it( itype_id( row.id ), calendar::turn, 0 );
        CHECK( it.can_link_up() );
        // CDF: LINKUP1b, src/item_factory.cpp add_cable_pocket: a link_up item has one CABLE pocket
        CHECK( count_pockets( type, pocket_type::CABLE ) == 1 );
        // The loaded traits the link reads (item_gun_tool_ammo.cpp item::update_link_traits).
        it.update_link_traits();
        CHECK( it.link().max_length == row.cable_length );
        CHECK( it.link().charge_rate == row.charge_rate_mw );
        CHECK( it.link().efficiency == Approx( 0.85f ) );
    }
    // CDF: the one row whose DDA action carries menu_text (radio_on, "Plug in / Unplug")
    CHECK( with_menu_text == 1 );
}

// L14b: the pocket list of every row is unchanged. The hash covers the loaded pockets (left
// out: the loader's MOD and CABLE pockets) and the legacy `magazines` table; it is regenerated by
// the hidden snapshot case below.
TEST_CASE( "linkup1e rows keep their pocket list", "[link][linkup1e]" )
{
    clear_all_state();
    for( const linkup_row &row : read_rows() ) {
        CAPTURE( row.id, row.pockets );
        CAPTURE( pocket_signature( itype_id( row.id ).obj() ) );
        REQUIRE( itype_id( row.id ).is_valid() );
        REQUIRE_FALSE( row.pockets.empty() );
        CHECK( pocket_hash( itype_id( row.id ).obj() ) == row.pockets );
    }
}

// CDF: regenerates the `pockets` hashes of tests/data/dda_staged/linkup1e_rows.json (the
// p3 batt2 snapshot pattern). Run with --tags "[linkup1esnap]"; a stale row fails and prints the
// full signature it hashed.
TEST_CASE( "linkup1e pocket signature snapshot", "[.][linkup1esnap]" )
{
    for( const linkup_row &row : read_rows() ) {
        CAPTURE( row.id );
        CAPTURE( pocket_signature( itype_id( row.id ).obj() ) );
        CHECK( pocket_hash( itype_id( row.id ).obj() ) == row.pockets );
    }
}

// L15: L2 on real data. A camera with an empty battery draws from the car's battery through the
// link and spends its charges there (DDA item_gun_tool_ammo.cpp:1426-1480, :1861-1865). The
// mutation "drop the action from camera" kills it at can_link_up() and at the link length.
TEST_CASE( "linkup1e camera draws from the link", "[link][linkup1e]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "car" ), tripoint_bub_ms( 10, 10, 0 ), 0_degrees, 0, 0,
                                     false );
    REQUIRE( veh != nullptr );
    veh->charge_battery( 100, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ) ) == 100 );

    item camera( itype_id( "camera" ), calendar::turn, 0 );
    REQUIRE( camera.can_link_up() );
    CHECK( camera.ammo_remaining( nullptr ) == 0 );
    CHECK( camera.ammo_remaining_linked( here, nullptr ) == 0 );

    link_tool_to( camera, *veh );
    CHECK( camera.link_length() == 1 );
    CHECK( camera.link().max_length == 3 );
    CHECK( camera.ammo_remaining_linked( here, nullptr ) == 100 );
    CHECK( camera.ammo_remaining( nullptr ) == 0 );
    CHECK( camera.ammo_sufficient( nullptr, 1 ) );

    const int consumed = camera.ammo_consume( 10, tripoint_bub_ms( 10, 10, 0 ), nullptr );
    CHECK( consumed == 10 );
    CHECK( veh->fuel_left( itype_id( "battery" ) ) == 90 );
    CHECK( camera.charges == 0 );
    CHECK( camera.ammo_remaining_linked( here, nullptr ) == 90 );
}
