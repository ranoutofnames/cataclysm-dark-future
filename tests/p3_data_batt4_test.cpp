// CDF: P3-BATT4. Boundary of the battery-store shim after LINKUP1c/1e: a base tool may hold a
// battery only through explicit `pocket_data` (the DDA form); everything else that still carries a
// legacy well or a stay-integral pocket is listed in tests/data/battery_hold.json with its owning
// packet. BATT4 also finishes the two tools LINKUP1c could not convert: DDA's mess kit heats food
// for free and DDA's voltmeter has no power, so both lose their store (AUTO-265).
#include "catch/catch.hpp"
#include "avatar.h"
#include "calendar.h"
#include "flag.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "player_helpers.h"
#include "type_id.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace
{

const std::string BATT4_HOLD = "tests/data/battery_hold.json";
const std::string CRT_TOOLS = "data/mods/CRT_EXPANSION/items/crt_tools.json";

struct hold_entry {
    std::string kind;
    std::string packet;
    std::string file;
};

struct base_scan {
    std::set<std::string> battery_well;
    std::set<std::string> integral;
    std::set<std::string> filter_well;
    // legacy = the well is synthesized from `magazines`, or the integral pocket from `max_charges`
    std::set<std::string> legacy;
};

auto read_hold() -> std::map<std::string, hold_entry>
{
    std::ifstream file( BATT4_HOLD, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    std::map<std::string, hold_entry> out;
    for( const JsonMember member : root.get_object( "items" ) ) {
        JsonObject jo = member.get_object();
        jo.allow_omitted_members();
        out[member.name()] = {
            .kind = jo.get_string( "kind" ),
            .packet = jo.get_string( "packet" ),
            .file = jo.get_string( "file", "" ),
        };
    }
    return out;
}

auto ids_of_kind( const std::map<std::string, hold_entry> &hold, const std::string &kind )
-> std::set<std::string>
{
    std::set<std::string> out;
    for( const auto &[id, entry] : hold ) {
        if( entry.kind == kind ) {
            out.insert( id );
        }
    }
    return out;
}

auto joined( const std::set<std::string> &ids ) -> std::string
{
    std::string out;
    for( const std::string &id : ids ) {
        out += id + " ";
    }
    return out;
}

// CDF: only items defined by the core content pack (base data) are scanned; the test binary also
// loads data/mods/TEST_DATA (loader fixtures, PL-33b D5) and some tests add runtime itypes. JSON
// items carry their source mod, runtime types carry none.
auto is_core_data( const itype &type ) -> bool
{
    return !type.src.empty() && type.src.back().second == mod_id( "bn" );
}

auto is_battery_flag( const flag_id &flag ) -> bool
{
    return flag.str().starts_with( "BATTERY" );
}

// A well takes battery cells when it is flag-restricted to a BATTERY_* flag or id-restricted to a
// magazine whose ammo type is battery.
auto well_takes_battery( const pocket_data &pocket ) -> bool
{
    for( const flag_id &flag : pocket.get_flag_restrictions() ) {
        if( is_battery_flag( flag ) ) {
            return true;
        }
    }
    for( const itype_id &cell : pocket.item_id_restriction ) {
        if( cell.is_valid() && cell->magazine &&
            cell->magazine->type.contains( ammotype( "battery" ) ) ) {
            return true;
        }
    }
    return false;
}

auto is_filter_well( const pocket_data &pocket ) -> bool
{
    for( const flag_id &flag : pocket.get_flag_restrictions() ) {
        const auto name = flag.str();
        if( name.starts_with( "GASFILTER" ) || name.starts_with( "REBREATHER" ) ) {
            return true;
        }
    }
    for( const itype_id &item : pocket.item_id_restriction ) {
        const auto name = item.str();
        if( name.starts_with( "gasfilter" ) || name.starts_with( "rebreather_cartridge" ) ) {
            return true;
        }
    }
    return false;
}

auto has_legacy_battery_store( const itype &type ) -> bool
{
    if( type.explicit_pockets ) {
        return false;
    }
    if( !type.magazines.empty() ) {
        return true;
    }
    return type.tool && type.tool->max_charges > 0 &&
           type.tool->ammo_id.contains( ammotype( "battery" ) );
}

auto scan_base() -> base_scan
{
    base_scan out;
    int tools = 0;
    for( const itype *type : item_controller->all() ) {
        if( type->tool == nullptr || !is_core_data( *type ) ) {
            continue;
        }
        ++tools;
        bool battery_well = false;
        bool filter_well = false;
        bool integral = false;
        for( const pocket_data &pocket : type->pockets ) {
            if( pocket.type == pocket_type::MAGAZINE_WELL ) {
                battery_well = battery_well || well_takes_battery( pocket );
                filter_well = filter_well || is_filter_well( pocket );
            } else if( pocket.type == pocket_type::MAGAZINE &&
                       pocket.ammo_restriction.count( ammotype( "battery" ) ) > 0 ) {
                integral = true;
            }
        }
        const auto id = type->get_id().str();
        if( battery_well ) {
            out.battery_well.insert( id );
        }
        if( filter_well ) {
            out.filter_well.insert( id );
        }
        if( integral ) {
            out.integral.insert( id );
        }
        if( has_legacy_battery_store( *type ) ) {
            out.legacy.insert( id );
        }
    }
    // the scan sees the base tools (a floor, so an empty walk cannot pass)
    REQUIRE( tools > 500 );
    return out;
}

auto count_type( const itype &type, pocket_type wanted ) -> int
{
    int count = 0;
    for( const pocket_data &pocket : type.pockets ) {
        count += pocket.type == wanted ? 1 : 0;
    }
    return count;
}

} // namespace

// T1: mess_kit heats for free, voltmeter takes no cell, crt_mess_kit follows the mess kit.
// Mutation: put `magazines` + `magazine_well` back on mess_kit (or charges_per_use 5).
TEST_CASE( "batt4 mess kit and voltmeter hold no cell", "[battery][wells][batt4]" )
{
    clear_avatar();

    SECTION( "mess_kit" ) {
        const itype_id id( "mess_kit" );
        REQUIRE( id.is_valid() );
        const auto &type = id.obj();
        CHECK_FALSE( type.explicit_pockets );
        CHECK( type.magazines.empty() );
        CHECK( count_type( type, pocket_type::MAGAZINE_WELL ) == 0 );
        CHECK( count_type( type, pocket_type::MAGAZINE ) == 0 );
        // DDA heats food for free: the CDF HOTPLATE iuse costs 0 charges and still works
        CHECK( type.get_use( "TOGGLE_HEATS_FOOD" ) != nullptr );
        CHECK( type.has_flag( flag_HEATS_FOOD_USING_CHARGES ) );
        REQUIRE( type.tool );
        CHECK( type.charges_to_use() == 0 );
        item kit( id, calendar::turn );
        CHECK( kit.ammo_remaining() == 0 );
        CHECK( kit.ammo_required() == 0 );
        CHECK( get_avatar().has_enough_charges( kit, false ) );
    }

    SECTION( "voltmeter" ) {
        const itype_id id( "voltmeter" );
        REQUIRE( id.is_valid() );
        const auto &type = id.obj();
        CHECK_FALSE( type.explicit_pockets );
        CHECK( type.magazines.empty() );
        CHECK( count_type( type, pocket_type::MAGAZINE_WELL ) == 0 );
        // CDF: the Lua VOLTMETER action is charge-free and stays (preload.lua -> lua/iuse/voltmeter.lua)
        CHECK( type.get_use( "VOLTMETER" ) != nullptr );
    }
}

// T2: the legacy store is the only way a base tool gets a battery without explicit pocket_data, and
// the only ids allowed to keep it are the parked and retired entries of the hold file
// (APPL1 emptied the parked set; RETIRE1 deleted the retired ids, so no exemption is left).
// Mutation: add a base tool with `magazines` (a new battery well) -> it is not listed.
TEST_CASE( "batt4 legacy battery stores are exactly the listed exemptions", "[battery][wells][batt4]" )
{
    const auto scan = scan_base();
    const auto hold = read_hold();
    std::set<std::string> listed;
    for( const auto &[id, entry] : hold ) {
        if( entry.kind == "parked" || entry.kind == "retired" ) {
            listed.insert( id );
        }
    }
    CAPTURE( joined( scan.legacy ) );
    CAPTURE( joined( listed ) );
    CHECK( scan.legacy == listed );
}

// T3: the hold file is exactly the documented boundary: the three native control laptop wells, the
// filter/cartridge wells, every stay-integral battery pocket, the (now empty) parked set and the retired
// ids. A tool added to the file under any kind fails its set or its kind predicate.
// Mutation: add `flashlight` as native, or drop `vac_pump` from the retired list.
TEST_CASE( "batt4 exemption list is exactly the documented boundary", "[battery][wells][batt4]" )
{
    const auto scan = scan_base();
    const auto hold = read_hold();
    REQUIRE_FALSE( hold.empty() );

    const std::set<std::string> known_kinds = {
        "native", "filter", "integral", "parked", "retired", "no-pocket"
    };
    for( const auto &[id, entry] : hold ) {
        CAPTURE( id, entry.kind, entry.packet );
        CHECK( known_kinds.count( entry.kind ) == 1 );
        CHECK_FALSE( entry.packet.empty() );
        CHECK_FALSE( entry.file.empty() );
        CHECK( std::filesystem::exists( entry.file ) );
    }

    // (a) native, no DDA twin: the three control laptop forms (PL-33b T2 a)
    const std::set<std::string> native_ids = {
        "control_laptop", "control_laptop_screen_lit", "control_laptop_music"
    };
    CHECK( ids_of_kind( hold, "native" ) == native_ids );
    for( const std::string &id : native_ids ) {
        CAPTURE( id );
        REQUIRE( itype_id( id ).is_valid() );
        CHECK( scan.battery_well.count( id ) == 1 );
        CHECK( itype_id( id ).obj().explicit_pockets );
    }

    // (b) filter and cartridge wells: not battery tools, accepted cell stores are not their point
    CHECK( ids_of_kind( hold, "filter" ) == scan.filter_well );
    for( const std::string &id : ids_of_kind( hold, "filter" ) ) {
        CAPTURE( id );
        REQUIRE( itype_id( id ).is_valid() );
        const auto &type = itype_id( id ).obj();
        bool filter = false;
        for( const pocket_data &pocket : type.pockets ) {
            if( pocket.type == pocket_type::MAGAZINE_WELL && is_filter_well( pocket ) ) {
                filter = true;
                CHECK( pocket.default_magazine.is_valid() );
            }
        }
        CHECK( filter );
    }

    // (c) stay-integral battery pockets: DDA class integral or BATT2's integral pocket
    CHECK( ids_of_kind( hold, "integral" ) == scan.integral );
    for( const std::string &id : ids_of_kind( hold, "integral" ) ) {
        CAPTURE( id );
        REQUIRE( itype_id( id ).is_valid() );
        const auto &type = itype_id( id ).obj();
        CHECK( count_type( type, pocket_type::MAGAZINE ) == 1 );
        CHECK( count_type( type, pocket_type::MAGAZINE_WELL ) == 0 );
    }

    // (d) APPL1 closed the AUTO-265 parked pair: autoclave and hydraulic_press are no longer parked.
    // They lost the legacy well and take the DDA appliance link (link_up), so no parked row is left.
    CHECK( ids_of_kind( hold, "parked" ).empty() );
    for( const std::string &id : { "autoclave", "hydraulic_press" } ) {
        CAPTURE( id );
        REQUIRE( itype_id( id ).is_valid() );
        CHECK( scan.legacy.count( id ) == 0 );
        CHECK( itype_id( id )->get_use( "link_up" ) != nullptr );
    }

    // (e) retired obsoletion ids: RETIRE1 deleted all 12 items (see tests/retired_ids_test.cpp), so
    // the hold file lists none and none of them is defined any more.
    const std::set<std::string> retired_ids = {
        "closed_loop_extractor_large", "closed_loop_extractor_large_on",
        "closed_loop_extractor_small", "closed_loop_extractor_small_on", "dab_pen_on",
        "vac_oven_small", "vac_oven_small_done", "vac_oven_small_full", "vac_oven_small_on",
        "vac_pump", "vac_pump_on", "welder_crude"
    };
    CHECK( ids_of_kind( hold, "retired" ).empty() );
    for( const std::string &id : retired_ids ) {
        CAPTURE( id );
        CHECK_FALSE( itype_id( id ).is_valid() );
        CHECK( scan.legacy.count( id ) == 0 );
    }

    // (f) DDA has no pocket at all: fake_firestarter stays without a battery store
    CHECK( ids_of_kind( hold, "no-pocket" ) == std::set<std::string>{ "fake_firestarter" } );
    REQUIRE( itype_id( "fake_firestarter" ).is_valid() );
    CHECK( scan.battery_well.count( "fake_firestarter" ) == 0 );
    CHECK( scan.integral.count( "fake_firestarter" ) == 0 );
}

// T4: the crt_mess_kit record follows mess_kit. The CRT_EXPANSION mod is not loaded by the test
// binary (core + TEST_DATA only), so the record is pinned in the JSON, like BATT5 does for mod rows.
// Mutation: put `initial_charges`/`max_charges` back on crt_mess_kit (the synthesized integral pocket
// returns), or `charges_per_use` back to 1 (the heating costs power again).
TEST_CASE( "batt4 crt mess kit record has no battery store", "[battery][wells][batt4]" )
{
    std::ifstream file( CRT_TOOLS, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    bool found = false;
    for( JsonObject jo : jsin.get_array() ) {
        jo.allow_omitted_members();
        if( !jo.has_string( "id" ) || jo.get_string( "id" ) != "crt_mess_kit" ) {
            continue;
        }
        found = true;
        CHECK_FALSE( jo.has_member( "pocket_data" ) );
        CHECK_FALSE( jo.has_member( "magazines" ) );
        CHECK_FALSE( jo.has_member( "magazine_well" ) );
        CHECK_FALSE( jo.has_member( "max_charges" ) );
        CHECK_FALSE( jo.has_member( "initial_charges" ) );
        CHECK( jo.get_int( "charges_per_use", -1 ) == 0 );
        // the heating stays: same HOTPLATE iuse as the mess kit (the loader turns it into
        // TOGGLE_HEATS_FOOD + HEATS_FOOD_USING_CHARGES)
        CHECK( jo.get_string( "use_action", "" ) == "HOTPLATE" );
        CHECK( jo.get_string( "sub", "" ) == "hotplate" );
    }
    CHECK( found );
}
