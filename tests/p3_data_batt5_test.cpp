// CDF: P3-BATT5. The 12 mod battery tools take DDA's explicit pocket (PL-33b
// TASK-draft-BATT5-v1): 8 wells the loader synthesizes from the legacy BN `magazines` key become
// explicit DDA MAGAZINE_WELL pockets with flags derived from their CDF cells (W7 rule), and the 4
// CRT tools without a DDA twin that carry NO_UNLOAD keep DDA's integral class, written as an
// explicit MAGAZINE pocket at their own CDF `max_charges` (200 / 250 / 100 / 100). Capacity stays
// CDF in every row (AUTO-236: cells keep CDF sizes).
//
// CDF: the test binary loads core data and TEST_DATA only (tests/test_main.cpp), so a mod item is
// not an itype_id here. The cases read the mod JSON files directly, the way batt1_pin.json pins
// the base rows; the loader-level check is --check-mods, which loads every mod. The hidden
// `[batt5snap]` case WARNs the loaded pockets when a run does load the mods
// (`--mods=aftershock,arcana,...`).
#include "catch/catch.hpp"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "type_id.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string BATT5_PIN = "tests/data/dda_staged/battery_mod_pin.json";
const std::string BATT5_BEFORE = "tests/data/dda_staged/battery_mod_before.json";

struct mod_pocket {
    std::string type;
    std::set<std::string> flags;
    std::set<std::string> ids;
    std::string def;
    int ammo_battery = -1;
    bool rigid = false;
    bool carry = false;
    int volume_ml = 0;
};

struct mod_record {
    std::string id;
    std::string parent;
    bool magazines = false;
    bool pockets = false;
    bool has_ammo = false;
    int max_charges = 0;
    std::vector<mod_pocket> list;
};

struct pin_row {
    std::string id;
    std::string group;
    std::string file;
    std::string kind;
    std::string def;
    std::vector<std::string> flags;
    std::vector<std::string> ir;
    std::vector<std::string> old_cells;
    int capacity = 0;
    int integral = 0;
    std::vector<mod_pocket> other;
};

struct cell_info {
    int capacity = 0;
    std::set<std::string> flags;
};

auto read_pocket( JsonObject p ) -> mod_pocket
{
    p.allow_omitted_members();
    mod_pocket out;
    // CDF: a pocket without `pocket_type` loads as a CONTAINER (pocket_data default)
    out.type = p.get_string( "pocket_type", "CONTAINER" );
    if( p.has_member( "flag_restriction" ) ) {
        for( const std::string &f : p.get_string_array( "flag_restriction" ) ) {
            out.flags.insert( f );
        }
    }
    if( p.has_member( "item_restriction" ) ) {
        for( const std::string &i : p.get_string_array( "item_restriction" ) ) {
            out.ids.insert( i );
        }
    }
    out.def = p.get_string( "default_magazine", "" );
    if( p.has_member( "ammo_restriction" ) ) {
        JsonObject ammo = p.get_object( "ammo_restriction" );
        ammo.allow_omitted_members();
        if( ammo.size() == 1 && ammo.has_int( "battery" ) ) {
            out.ammo_battery = ammo.get_int( "battery" );
        }
    }
    out.rigid = p.get_bool( "rigid", false );
    out.carry = p.get_bool( "carry", false );
    out.volume_ml = p.get_int( "volume_ml", 0 );
    return out;
}

auto read_other_pockets( JsonObject jo ) -> std::vector<mod_pocket>
{
    std::vector<mod_pocket> out;
    for( JsonObject p : jo.get_array( "other_pockets" ) ) {
        // read_pocket reads a copy, so the loop's own object must allow its omitted members too
        p.allow_omitted_members();
        out.push_back( read_pocket( p ) );
    }
    return out;
}

auto read_pin() -> std::vector<pin_row>
{
    std::ifstream file( BATT5_PIN, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    std::vector<pin_row> rows;
    for( JsonObject jo : root.get_array( "rows" ) ) {
        jo.allow_omitted_members();
        pin_row r;
        r.id = jo.get_string( "id" );
        r.group = jo.get_string( "group" );
        r.file = jo.get_string( "file" );
        r.kind = jo.get_string( "kind" );
        if( r.kind == "integral" ) {
            r.integral = jo.get_int( "integral" );
            r.capacity = r.integral;
        } else {
            r.def = jo.get_string( "default" );
            r.flags = jo.get_string_array( "flags" );
            r.ir = jo.get_string_array( "item_restriction" );
            r.old_cells = jo.get_string_array( "old_cells" );
            r.capacity = jo.get_int( "capacity_before" );
        }
        r.other = read_other_pockets( jo );
        rows.push_back( r );
    }
    REQUIRE( rows.size() == 12 );
    return rows;
}

auto read_before() -> std::map<std::string, std::vector<mod_pocket>>
{
    std::ifstream file( BATT5_BEFORE, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    std::map<std::string, std::vector<mod_pocket>> out;
    for( JsonObject jo : root.get_array( "rows" ) ) {
        jo.allow_omitted_members();
        out[jo.get_string( "id" )] = read_other_pockets( jo );
    }
    return out;
}

auto read_records( const std::string &file_name ) -> std::map<std::string, mod_record>
{
    std::ifstream file( file_name, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    std::map<std::string, mod_record> out;
    for( JsonObject jo : jsin.get_array() ) {
        jo.allow_omitted_members();
        if( !jo.has_string( "id" ) || jo.has_string( "abstract" ) ) {
            continue;
        }
        mod_record r;
        r.id = jo.get_string( "id" );
        r.parent = jo.get_string( "copy-from", "" );
        r.magazines = jo.has_member( "magazines" );
        r.pockets = jo.has_member( "pocket_data" );
        r.has_ammo = jo.has_member( "ammo" );
        r.max_charges = jo.get_int( "max_charges", 0 );
        if( r.pockets ) {
            for( JsonObject p : jo.get_array( "pocket_data" ) ) {
                p.allow_omitted_members();
                r.list.push_back( read_pocket( p ) );
            }
        }
        out[r.id] = r;
    }
    return out;
}

// CDF: the record whose explicit list the loader ends up with (own or a copy-from ancestor).
auto effective_record( const std::map<std::string, mod_record> &records,
                       const std::string &id ) -> const mod_record *
{
    const mod_record *cur = nullptr;
    const auto found = records.find( id );
    if( found == records.end() ) {
        return nullptr;
    }
    cur = &found->second;
    for( int depth = 0; depth < 8 && cur != nullptr; depth++ ) {
        if( cur->pockets ) {
            return cur;
        }
        if( cur->parent.empty() ) {
            return nullptr;
        }
        const auto up = records.find( cur->parent );
        cur = up == records.end() ? nullptr : &up->second;
    }
    return nullptr;
}

auto count_type( const mod_record &r, const std::string &type ) -> int
{
    int n = 0;
    for( const mod_pocket &p : r.list ) {
        n += p.type == type ? 1 : 0;
    }
    return n;
}

// CDF: the cell items keep their CDF capacities (AUTO-236); read them from the two battery files.
auto read_cells() -> std::map<std::string, cell_info>
{
    std::map<std::string, cell_info> out;
    std::map<std::string, std::string> parents;
    for( const std::string &file_name :
         {"data/json/items/battery.json", "data/json/items/vehicle/battery.json"} ) {
        std::ifstream file( file_name, std::ios::binary );
        REQUIRE( file.good() );
        JsonIn jsin( file );
        for( JsonObject jo : jsin.get_array() ) {
            jo.allow_omitted_members();
            if( !jo.has_string( "id" ) ) {
                continue;
            }
            cell_info c;
            if( jo.has_member( "capacity" ) ) {
                c.capacity = jo.get_int( "capacity" );
            }
            if( jo.has_member( "flags" ) ) {
                for( const std::string &f : jo.get_string_array( "flags" ) ) {
                    c.flags.insert( f );
                }
            }
            if( jo.has_string( "copy-from" ) ) {
                parents[jo.get_string( "id" )] = jo.get_string( "copy-from" );
            }
            out[jo.get_string( "id" )] = c;
        }
    }
    // one level of copy-from is enough for these cells; flags merge, capacity is overridden
    for( const auto &[id, parent] : parents ) {
        const auto up = out.find( parent );
        if( up == out.end() || out.count( id ) == 0 ) {
            continue;
        }
        for( const std::string &f : up->second.flags ) {
            out[id].flags.insert( f );
        }
        if( out[id].capacity == 0 ) {
            out[id].capacity = up->second.capacity;
        }
    }
    return out;
}

auto accepts( const mod_pocket &well, const cell_info &cell, const std::string &id ) -> bool
{
    if( well.ids.count( id ) > 0 ) {
        return true;
    }
    for( const std::string &f : well.flags ) {
        if( cell.flags.count( f ) > 0 ) {
            return true;
        }
    }
    return false;
}

// CDF: no record of the copy-from chain may keep the legacy `magazines` key (draft step 1).
auto chain_has_legacy( const std::map<std::string, mod_record> &records,
                       const std::string &id ) -> bool
{
    std::string cur = id;
    for( int depth = 0; depth < 8; depth++ ) {
        const auto found = records.find( cur );
        if( found == records.end() ) {
            return false;
        }
        if( found->second.magazines ) {
            return true;
        }
        if( found->second.parent.empty() ) {
            return false;
        }
        cur = found->second.parent;
    }
    return false;
}

} // namespace

TEST_CASE( "p3 batt5 well form and CDF capacity", "[battery][wells][batt5]" ) {
    // T1: exactly one MAGAZINE_WELL with the DDA flags of the row, the CDF default cell and the old
    // `magazines` list; the default cell's own capacity is the pinned one. Mutation: give
    // jumppad_deployer the light_battery_cell default (its cell is 100, not 50).
    const std::map<std::string, cell_info> cells = read_cells();
    std::map<std::string, std::map<std::string, mod_record>> records;
    for( const pin_row &r : read_pin() ) {
        if( r.kind != "well" ) {
            continue;
        }
        CAPTURE( r.id, r.group, r.file );
        if( records.count( r.file ) == 0 ) {
            records[r.file] = read_records( r.file );
        }
        const mod_record *own = effective_record( records.at( r.file ), r.id );
        INFO( "the row has no explicit pocket_data in its copy-from chain (still synthesized)" );
        REQUIRE( own != nullptr );
        // CDF: no record of the chain keeps the legacy key (the explicit list replaces it)
        CHECK_FALSE( chain_has_legacy( records.at( r.file ), r.id ) );
        CHECK( own->magazines == false );
        REQUIRE( count_type( *own, "MAGAZINE_WELL" ) == 1 );
        const mod_pocket *well = nullptr;
        for( const mod_pocket &p : own->list ) {
            if( p.type == "MAGAZINE_WELL" ) {
                well = &p;
            }
        }
        REQUIRE( well != nullptr );
        CHECK( well->flags == std::set<std::string>( r.flags.begin(), r.flags.end() ) );
        CHECK( well->def == r.def );
        CHECK( well->ids == std::set<std::string>( r.ir.begin(), r.ir.end() ) );
        // CDF: the loader takes default_magazine from the first item_restriction entry
        CHECK_FALSE( r.ir.empty() );
        CHECK( r.ir.front() == r.def );
        REQUIRE( cells.count( r.def ) == 1 );
        CHECK( cells.at( r.def ).capacity == r.capacity );
    }
}

TEST_CASE( "p3 batt5 integral rows keep the CDF number", "[battery][wells][batt5]" ) {
    // T2: the 4 NO_UNLOAD CRT tools get one integral MAGAZINE pocket at their own max_charges and
    // no well; the legacy keys stay until 7-END. Mutation: give crt_gasmask_on 200.
    std::map<std::string, std::map<std::string, mod_record>> records;
    for( const pin_row &r : read_pin() ) {
        if( r.kind != "integral" ) {
            continue;
        }
        CAPTURE( r.id, r.integral );
        if( records.count( r.file ) == 0 ) {
            records[r.file] = read_records( r.file );
        }
        const mod_record *own = effective_record( records.at( r.file ), r.id );
        INFO( "the row has no explicit pocket_data in its copy-from chain (still synthesized)" );
        REQUIRE( own != nullptr );
        CHECK( own->magazines == false );
        CHECK( count_type( *own, "MAGAZINE_WELL" ) == 0 );
        REQUIRE( count_type( *own, "MAGAZINE" ) == 1 );
        const mod_pocket *mag = nullptr;
        for( const mod_pocket &p : own->list ) {
            if( p.type == "MAGAZINE" ) {
                mag = &p;
            }
        }
        REQUIRE( mag != nullptr );
        CHECK( mag->rigid );
        CHECK( mag->ammo_battery == r.integral );
        // CDF: legacy path until 7-END (`ammo`/`max_charges`/`initial_charges` stay)
        CHECK( own->has_ammo );
        CHECK( own->max_charges == r.integral );
    }
}

TEST_CASE( "p3 batt5 old cells still fit", "[battery][wells][batt5]" ) {
    // T3: every cell of the old `magazines` list is still accepted by the written well; a heavy
    // cell is refused by the light and medium rows. Mutation: drop coalescent_converter's
    // item_restriction (its storage batteries carry no BATTERY_* flag), or add BATTERY_HEAVY to a
    // light row.
    const std::map<std::string, cell_info> cells = read_cells();
    REQUIRE( cells.count( "heavy_battery_cell" ) == 1 );
    int refused_heavy = 0;
    std::map<std::string, std::map<std::string, mod_record>> records;
    for( const pin_row &r : read_pin() ) {
        if( r.kind != "well" ) {
            continue;
        }
        CAPTURE( r.id, r.file );
        if( records.count( r.file ) == 0 ) {
            records[r.file] = read_records( r.file );
        }
        const mod_record *own = effective_record( records.at( r.file ), r.id );
        INFO( "the row has no explicit pocket_data in its copy-from chain (still synthesized)" );
        REQUIRE( own != nullptr );
        const mod_pocket *well = nullptr;
        for( const mod_pocket &p : own->list ) {
            if( p.type == "MAGAZINE_WELL" ) {
                well = &p;
            }
        }
        REQUIRE( well != nullptr );
        for( const std::string &cell : r.old_cells ) {
            CAPTURE( cell );
            REQUIRE( cells.count( cell ) == 1 );
            CHECK( accepts( *well, cells.at( cell ), cell ) );
        }
        if( !accepts( *well, cells.at( "heavy_battery_cell" ), "heavy_battery_cell" ) ) {
            ++refused_heavy;
        }
    }
    // CDF: 7 of the 8 rows hold light/medium cells only; coalescent_converter takes heavy ones
    CHECK( refused_heavy == 7 );
    // CDF: the CDF cell numbers are untouched (AUTO-236)
    REQUIRE( cells.count( "light_battery_cell" ) == 1 );
    CHECK( cells.at( "light_battery_cell" ).capacity == 100 );
    REQUIRE( cells.count( "medium_battery_cell" ) == 1 );
    CHECK( cells.at( "medium_battery_cell" ).capacity == 500 );
    REQUIRE( cells.count( "medium_storage_battery" ) == 1 );
    CHECK( cells.at( "medium_storage_battery" ).capacity == 10000 );
}

TEST_CASE( "p3 batt5 pocket completeness", "[battery][wells][batt5]" ) {
    // T4: the list after the port is the before-snapshot with only the well (or the integral
    // MAGAZINE) written: the MOD pocket stays. Mutation: drop `{ "pocket_type": "MOD" }`.
    const std::map<std::string, std::vector<mod_pocket>> before = read_before();
    REQUIRE( before.size() == 12 );
    std::map<std::string, std::map<std::string, mod_record>> records;
    for( const pin_row &r : read_pin() ) {
        CAPTURE( r.id, r.kind );
        REQUIRE( before.count( r.id ) == 1 );
        if( records.count( r.file ) == 0 ) {
            records[r.file] = read_records( r.file );
        }
        const mod_record *own = effective_record( records.at( r.file ), r.id );
        INFO( "the row has no explicit pocket_data in its copy-from chain (still synthesized)" );
        REQUIRE( own != nullptr );
        std::vector<mod_pocket> loaded;
        for( const mod_pocket &p : own->list ) {
            if( p.type != "MAGAZINE_WELL" &&
                !( r.kind == "integral" && p.type == "MAGAZINE" ) ) {
                loaded.push_back( p );
            }
        }
        const std::vector<mod_pocket> &want = before.at( r.id );
        REQUIRE( loaded.size() == want.size() );
        for( size_t i = 0; i < want.size(); i++ ) {
            CAPTURE( i, want[i].type );
            CHECK( loaded[i].type == want[i].type );
            CHECK( loaded[i].carry == want[i].carry );
            CHECK( loaded[i].volume_ml == want[i].volume_ml );
        }
        // CDF: the written list is the before-snapshot plus exactly one new entry
        CHECK( own->list.size() == want.size() + 1 );
    }
}

TEST_CASE( "p3 batt5 loaded pocket snapshot", "[.][batt5snap]" ) {
    // diagnostic, always passes: run with the mods loaded to see the pockets the loader builds
    for( const pin_row &r : read_pin() ) {
        if( !itype_id( r.id ).is_valid() ) {
            WARN( r.id << " not loaded (mods not loaded by this run)" );
            continue;
        }
        std::ostringstream os;
        for( const auto &pocket : itype_id( r.id ).obj().pockets ) {
            os << io::enum_to_string( pocket.type ) << " ";
        }
        WARN( r.id << " pockets: " << os.str() );
    }
}
