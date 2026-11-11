// CDF: P3-BATT1. The 117 battery tools of groups W1/W7 leave the well the loader synthesizes from
// the legacy BN `magazines` key (src/item_factory.cpp, "Guns and tools that take magazines get a
// magazine well") and take DDA's explicit MAGAZINE_WELL pocket: DDA's `flag_restriction`, the CDF
// default cell (AUTO-236: cells keep CDF sizes) and `item_restriction` for the cells the flags no
// longer admit. `batt1_pin.json` holds the expected tuple per row (PL-33b battery-tools.json);
// `battery_before.json` is the before-snapshot of the pockets that must survive the edit.
#include "catch/catch.hpp"
#include "calendar.h"
#include "flag.h"
#include "flat_set.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "type_id.h"
#include "units.h"

#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

const std::string BATT1_PIN = "tests/data/dda_staged/batt1_pin.json";
const std::string BATT1_BEFORE = "tests/data/dda_staged/battery_before.json";

struct batt1_row {
    std::string id;
    std::string group;
    std::string packet;
    std::string file;
    std::vector<std::string> flags;
    std::string def;
    std::vector<std::string> ir;
    std::vector<std::string> old_cells;
    int capacity = 0;
};

struct before_pocket {
    std::string type;
    int volume_ml = 0;
    bool carry = false;
};

std::vector<batt1_row> read_pin()
{
    std::ifstream file( BATT1_PIN, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    std::vector<batt1_row> rows;
    for( JsonObject jo : root.get_array( "rows" ) ) {
        jo.allow_omitted_members();
        batt1_row r;
        r.id = jo.get_string( "id" );
        r.group = jo.get_string( "group" );
        r.packet = jo.get_string( "packet" );
        r.file = jo.get_string( "file" );
        r.flags = jo.get_string_array( "flags" );
        r.def = jo.get_string( "default" );
        r.ir = jo.get_string_array( "item_restriction" );
        r.old_cells = jo.get_string_array( "old_cells" );
        r.capacity = jo.get_int( "capacity_before" );
        rows.push_back( r );
    }
    REQUIRE( rows.size() == 118 );
    return rows;
}

std::map<std::string, std::vector<before_pocket>> read_before()
{
    std::ifstream file( BATT1_BEFORE, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    std::map<std::string, std::vector<before_pocket>> out;
    for( JsonObject jo : root.get_array( "rows" ) ) {
        jo.allow_omitted_members();
        for( JsonObject p : jo.get_array( "other_pockets" ) ) {
            p.allow_omitted_members();
            before_pocket b;
            b.type = p.get_string( "pocket_type" );
            b.volume_ml = p.get_int( "volume_ml", 0 );
            b.carry = p.get_bool( "carry", false );
            out[jo.get_string( "id" )].push_back( b );
        }
    }
    return out;
}

auto wells_of( const itype &t ) -> std::vector<const pocket_data *>
{
    std::vector<const pocket_data *> out;
    for( const pocket_data &p : t.pockets ) {
        if( p.type == pocket_type::MAGAZINE_WELL ) {
            out.push_back( &p );
        }
    }
    return out;
}

auto flag_names( const pocket_data &p ) -> std::set<std::string>
{
    std::set<std::string> out;
    for( const flag_id &f : p.get_flag_restrictions() ) {
        out.insert( f.str() );
    }
    return out;
}

auto id_names( const cata::flat_set<itype_id> &ids ) -> std::set<std::string>
{
    std::set<std::string> out;
    for( const itype_id &i : ids ) {
        out.insert( i.str() );
    }
    return out;
}

// CDF: which of the pinned files define `id` themselves (no abstracts, own record only)
auto own_record_has( const std::string &file_name, const std::string &id, const std::string &key ) -> bool
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

struct data_record {
    bool magazines = false;
    bool pockets = false;
    bool has_parent = false;
    std::string parent;
};

auto read_records( const std::string &file_name ) -> std::map<std::string, data_record>
{
    std::ifstream file( file_name, std::ios::binary );
    JsonIn jsin( file );
    std::map<std::string, data_record> out;
    for( JsonObject jo : jsin.get_array() ) {
        jo.allow_omitted_members();
        if( !jo.has_string( "id" ) || jo.has_string( "abstract" ) ) {
            continue;
        }
        data_record r;
        r.magazines = jo.has_member( "magazines" );
        r.pockets = jo.has_member( "pocket_data" );
        if( jo.has_string( "copy-from" ) ) {
            r.has_parent = true;
            r.parent = jo.get_string( "copy-from" );
        }
        out[jo.get_string( "id" )] = r;
    }
    return out;
}

// CDF: the loaded well comes from the own record or a copy-from ancestor (draft step 1); no record
// of the chain may keep the legacy `magazines` key
auto chain_has_explicit_pockets( const std::map<std::string, data_record> &records,
                                 const std::string &id ) -> bool
{
    std::string cur = id;
    for( int depth = 0; depth < 8; depth++ ) {
        const auto it = records.find( cur );
        if( it == records.end() || it->second.magazines ) {
            return false;
        }
        if( it->second.pockets ) {
            return true;
        }
        if( !it->second.has_parent ) {
            return false;
        }
        cur = it->second.parent;
    }
    return false;
}

} // namespace

TEST_CASE( "p3 batt1 well form", "[battery][wells][batt1]" ) {
    // T1: exactly one MAGAZINE_WELL, DDA's flags, the CDF default cell. Mutation: drop one added
    // flag on thermal_socks.
    for( const batt1_row &r : read_pin() ) {
        CAPTURE( r.id, r.group, r.packet );
        REQUIRE( itype_id( r.id ).is_valid() );
        const std::vector<const pocket_data *> wells = wells_of( itype_id( r.id ).obj() );
        REQUIRE( wells.size() == 1 );
        CHECK( flag_names( *wells[0] ) == std::set<std::string>( r.flags.begin(), r.flags.end() ) );
        CHECK( wells[0]->default_magazine.str() == r.def );
        // CDF: the loader sets default_magazine = item_restriction.front(), so the pinned default
        // cell is listed first; the rest is the old `magazines` list (see batt1_pin.json)
        std::set<std::string> want( r.ir.begin(), r.ir.end() );
        CHECK( id_names( wells[0]->item_id_restriction ) == want );
    }
}

TEST_CASE( "p3 batt1 default cell keeps the capacity", "[battery][wells][batt1]" ) {
    // T2: spawn with the default cell. Mutation: thermal_socks takes DDA's medium cell (500).
    for( const batt1_row &r : read_pin() ) {
        CAPTURE( r.id, r.capacity, r.def );
        item it( r.id, calendar::turn, r.capacity );
        CHECK( it.ammo_capacity() == r.capacity );
        CHECK( it.ammo_remaining() == r.capacity );
        CHECK( it.ammo_capacity( true ) == r.capacity );
    }
}

TEST_CASE( "p3 batt1 old cells still fit", "[battery][wells][batt1]" ) {
    // T3: every cell of the old `magazines` list reloads. Mutation: delete item_restriction on
    // thermal_suit (light_minus_battery_cell no longer fits).
    for( const batt1_row &r : read_pin() ) {
        CAPTURE( r.id );
        item it( r.id, calendar::turn );
        for( const std::string &cell : r.old_cells ) {
            CAPTURE( cell );
            // CDF: the well's id list is what the untargeted reload path reads
            CHECK( it.magazine_compatible().count( itype_id( cell ) ) == 1 );
            CHECK( it.can_reload_with( item( itype_id( cell ), calendar::turn ), false ) );
        }
    }
}

TEST_CASE( "p3 batt1 data has no legacy well keys", "[battery][wells][batt1]" ) {
    // T4: no BATT1 row keeps `magazines` in its own record, and the loaded well comes from an
    // explicit `pocket_data` in the copy-from chain. Mutation: put `magazines` back on one row.
    // CDF: the 3 N1 rows are check-only (draft step 4) and keep their dead `magazines` key.
    std::map<std::string, std::map<std::string, data_record>> records;
    for( const batt1_row &r : read_pin() ) {
        if( r.packet != "BATT1" ) {
            continue;
        }
        CAPTURE( r.id, r.file );
        if( records.count( r.file ) == 0 ) {
            records[r.file] = read_records( r.file );
        }
        CHECK_FALSE( own_record_has( r.file, r.id, "magazines" ) );
        CHECK( chain_has_explicit_pockets( records.at( r.file ), r.id ) );
    }
}

TEST_CASE( "p3 batt1 well refuses a cell outside flags and restriction", "[battery][wells][batt1]" ) {
    // T5: a heavy cell and a storage battery do not fit a light tool. Mutation: add every BATT
    // flag to miner_hat (the heavy cell then fits).
    item hat( "miner_hat", calendar::turn );
    CHECK_FALSE( hat.can_reload_with( item( itype_id( "heavy_battery_cell" ), calendar::turn ), false ) );
    CHECK_FALSE( hat.can_reload_with( item( itype_id( "storage_battery" ), calendar::turn ), false ) );
}

TEST_CASE( "p3 batt1 pocket completeness", "[battery][wells][batt1]" ) {
    // T6: the list after the port is the before-snapshot with only the well entry replaced: the
    // MOD pocket (and the sparkledogsuit CONTAINER) stay. Mutation: drop the CONTAINER from
    // sparkledogsuit (a dropped MOD is re-added by MOD1, so it is no longer a mutation).
    // CDF: P3-MODSFIX. The loader's CABLE pocket (LINKUP1e link_up, e.g. electric_blanket) is
    // skipped: it is added after the JSON pocket_data and is not part of the before-snapshot.
    const std::map<std::string, std::vector<before_pocket>> before = read_before();
    // CDF: the 115 BATT1 rows plus the 3 N1 rows the snapshot was taken on
    REQUIRE( before.size() == 118 );
    for( const batt1_row &r : read_pin() ) {
        CAPTURE( r.id );
        REQUIRE( before.count( r.id ) == 1 );
        const itype &t = itype_id( r.id ).obj();
        std::vector<const pocket_data *> loaded;
        int wells = 0;
        for( const pocket_data &p : t.pockets ) {
            if( p.type == pocket_type::MAGAZINE_WELL ) {
                ++wells;
            } else if( p.type == pocket_type::CABLE ) {
                // CDF: P3-MODSFIX. LINKUP1e gives every link_up tool a CABLE pocket in the loader
                // (src/item_factory.cpp add_cable_pocket), so it is not in the before-snapshot;
                // tests/p3_data_linkup1e_test.cpp pins it (the golden_view rule).
                continue;
            } else if( p.type == pocket_type::MOD || p.type == pocket_type::CONTAINER ) {
                loaded.push_back( &p );
            } else {
                CAPTURE( static_cast<int>( p.type ) );
                FAIL( "unexpected pocket type on a BATT1 row" );
            }
        }
        CHECK( wells == 1 );
        const std::vector<before_pocket> &want = before.at( r.id );
        REQUIRE( loaded.size() == want.size() );
        for( size_t i = 0; i < want.size(); i++ ) {
            CAPTURE( i, want[i].type );
            CHECK( io::enum_to_string( loaded[i]->type ) == want[i].type );
            if( want[i].type == "CONTAINER" ) {
                CHECK( loaded[i]->raw_volume_capacity == want[i].volume_ml * 1_ml );
                CHECK( loaded[i]->carry == want[i].carry );
            }
        }
    }
}

TEST_CASE( "p3 batt1 soldering fixture well has no rigid override", "[battery][wells][pocket][modsfix]" ) {
    // CDF: P3-MODSFIX. test_soldering_iron's well carried `rigid` and `magazine_well` together, so
    // every run logged "warnings for type test_soldering_iron: pocket 0 (MAGAZINE_WELL): rigid
    // overrides magazine_well" and init failed (src/pocket_data.cpp:259). DDA's TEST_DATA soldering
    // iron well is not rigid; the CDF fixture keeps the 250 ml well the staged golden pins.
    // Mutation: put "rigid": true back on that well.
    const std::vector<const pocket_data *> wells = wells_of( itype_id( "test_soldering_iron" ).obj() );
    REQUIRE( wells.size() == 1 );
    CHECK( wells[0]->magazine_well == 250_ml );
    CHECK_FALSE( wells[0]->rigid );
    CHECK( wells[0]->max_item_length >= 2_km );
}
