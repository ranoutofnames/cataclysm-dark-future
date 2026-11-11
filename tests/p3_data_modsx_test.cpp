#include "catch/catch.hpp"
#include "json.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "itype.h"
#include "type_id.h"

// CDF: P3-7-MODS-X: the exotic_ammo and MagicalNights rows of the packet
// (data/mods/{exotic_ammo,MagicalNights}). The two mods are not in the unit-test world (it loads
// core + test_data only, tests/test_main.cpp extract_mod_selection), so the loaded-item cases can
// only print these ids as skipped: the port is checked at JSON level here and by the tool's
// `--check` in the packet verify. The batch rows are also read by the generic cases
// (tests/p3_batch_generic_test.cpp B1-B5, tests/p3_golden_batches_test.cpp G2).
//
// S1 pins that one id per class of each mod has the batch/golden pocket (or is the KEEP adaptor);
// S2 pins the charges -> MAGAZINE rows (the mod has no .lua file that reads item.charges, so the
// Lua compatibility list is empty); S3 compares every converted own `pocket_data` with the plan
// and checks the dropped keys and ids, the part the generic cases cannot see for unloaded items.
//
// Mutations the cases kill: change one cap or pocket key in a mod file (S1/S3); put `max_charges`
// back on a converted charges row (S2); leave `magazines` / `capacity` / `max_charges` on a
// converted record (S3); edit the KEEP adaptor or the already-converted rune_druid_weapon (S3).

// Defined in tests/cdf_mag_port_test.cpp: compares the loaded item `id` with a golden pockets
// array given as JSON text.
auto p3_golden_compare_pockets( const std::string &id, const std::string &pockets_json,
                                bool &was_loaded ) -> std::vector<std::string>;

namespace {

const std::string MODSX_BATCH_PATH = "tests/data/dda_staged/p3_batches/7-MODS-X.json";

// CDF: the loader gives a restricted well 2 km (src/item_factory.cpp synthesize_pockets); the CDF
// test `magazine wells have an explicit length limit` (tests/item_length_test.cpp:161) requires
// every MAGAZINE_WELL pocket to state it, so the data has the key and the plan/golden does not.
// Compare wells without it, as tools/cdf_port_dda_mags.py --check does (well_length_agnostic).
const std::string MAGAZINE_WELL_KEY = "\"MAGAZINE_WELL\"";
const std::string WELL_LENGTH_KEY = "max_item_length";

struct modsx_row {
    std::string id;
    std::string file;
    std::string class_name;
    std::string action;
    std::string pockets_json;   // the plan/golden pockets, as text
    std::string pockets_canon;  // the same, canonicalised
    std::vector<std::string> dropped_keys;
    std::vector<std::string> json_dropped_ids;
    std::vector<std::string> own_keys;
    bool has_own_keys = false;
    bool not_edited = false;
    bool had_max_charges = false;
};

struct modsx_batch {
    std::map<std::string, modsx_row> items;
    std::map<std::string, modsx_row> not_edited;
    std::vector<std::string> lua_refs;
};

auto escape_text( const std::string &text ) -> std::string
{
    std::string out;
    for( const char c : text ) {
        if( c == '"' || c == '\\' ) {
            out += '\\';
        }
        out += c;
    }
    return out;
}

auto canon_value( const JsonValue &value ) -> std::string;

// A canonical text of a JSON value: member order ignored, `//` comments and the CDF-added well
// length limit ignored. Two pockets compare equal iff their canonical texts are equal.
auto canon_object( const JsonObject &object ) -> std::string
{
    std::map<std::string, std::string> parts;
    for( const JsonMember member : object ) {
        if( member.is_comment() ) {
            continue;
        }
        parts[member.name()] = canon_value( JsonValue( member ) );
    }
    const auto type = parts.find( "pocket_type" );
    if( type != parts.end() && type->second == MAGAZINE_WELL_KEY ) {
        parts.erase( WELL_LENGTH_KEY );
    }
    std::string out = "{";
    for( const auto &entry : parts ) {
        if( out.size() > 1 ) {
            out += ',';
        }
        out += '"' + entry.first + "\":" + entry.second;
    }
    return out + "}";
}

auto canon_array( const JsonArray &array ) -> std::string
{
    std::string out = "[";
    for( const JsonValue value : array ) {
        if( out.size() > 1 ) {
            out += ',';
        }
        out += canon_value( value );
    }
    return out + "]";
}

auto canon_value( const JsonValue &value ) -> std::string
{
    if( value.test_object() ) {
        return canon_object( value.get_object() );
    }
    if( value.test_array() ) {
        return canon_array( value.get_array() );
    }
    if( value.test_string() ) {
        return '"' + escape_text( value.get_string() ) + '"';
    }
    if( value.test_bool() ) {
        return value.get_bool() ? "true" : "false";
    }
    if( value.test_int() ) {
        return std::to_string( value.get_int() );
    }
    if( value.test_float() ) {
        return std::to_string( value.get_float() );
    }
    // null is the only value left (JsonValue has no test_null in this tree)
    return "null";
}

auto read_row( const JsonMember &member, bool force_not_edited ) -> modsx_row
{
    JsonObject row = member.get_object();
    row.allow_omitted_members();
    modsx_row out;
    out.id = member.name();
    out.file = row.get_string( "file" );
    out.class_name = row.get_string( "class" );
    out.action = row.get_string( "action" );
    out.not_edited = force_not_edited ||
                     ( row.has_bool( "not_edited" ) && row.get_bool( "not_edited" ) );
    if( row.has_array( "dropped_keys" ) ) {
        out.dropped_keys = row.get_string_array( "dropped_keys" );
    }
    if( row.has_array( "json_dropped_ids" ) ) {
        for( JsonObject dropped : row.get_array( "json_dropped_ids" ) ) {
            dropped.allow_omitted_members();
            out.json_dropped_ids.push_back( dropped.get_string( "id" ) );
        }
    }
    if( row.has_array( "own_keys" ) ) {
        out.has_own_keys = true;
        out.own_keys = row.get_string_array( "own_keys" );
    }
    if( row.has_object( "legacy" ) ) {
        JsonObject legacy = row.get_object( "legacy" );
        legacy.allow_omitted_members();
        out.had_max_charges = legacy.has_member( "max_charges" ) &&
                              legacy.get_member( "max_charges" ).test_int();
    }
    if( row.has_array( "pockets" ) ) {
        JsonArray pockets = row.get_array( "pockets" );
        out.pockets_canon = canon_array( pockets );
        out.pockets_json = pockets.str();
    }
    return out;
}

auto read_batch() -> const modsx_batch & {
    static modsx_batch batch;
    static bool read = false;
    if( read ) {
        return batch;
    }
    read = true;

    std::ifstream file( MODSX_BATCH_PATH, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    REQUIRE( root.get_string( "batch" ) == "7-MODS-X" );

    if( root.has_object( "lua_refs" ) ) {
        JsonObject refs = root.get_object( "lua_refs" );
        refs.allow_omitted_members();
        for( const JsonMember member : refs ) {
            batch.lua_refs.push_back( member.name() );
        }
    }

    const auto read_group = [&]( const char *name, std::map<std::string, modsx_row> &rows_out,
                                 bool force_not_edited ) {
        if( !root.has_object( name ) ) {
            return;
        }
        JsonObject rows = root.get_object( name );
        rows.allow_omitted_members();
        for( const JsonMember member : rows ) {
            rows_out.emplace( member.name(), read_row( member, force_not_edited ) );
        }
    };
    read_group( "items", batch.items, false );
    read_group( "not_edited", batch.not_edited, true );
    return batch;
}

auto find_row( const modsx_batch &batch, const std::string &id ) -> const modsx_row *
{
    const auto item = batch.items.find( id );
    if( item != batch.items.end() ) {
        return &item->second;
    }
    const auto kept = batch.not_edited.find( id );
    if( kept != batch.not_edited.end() ) {
        return &kept->second;
    }
    return nullptr;
}

struct own_record {
    bool found = false;
    bool has_pocket_data = false;
    std::string pockets_canon;
    std::set<std::string> keys;
};

// The one record of `id` in `path` (its own top-level keys and the canonical `pocket_data`).
auto read_own( const std::string &path, const std::string &id ) -> own_record
{
    own_record out;
    std::ifstream file( path, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonArray entries = jsin.get_array();
    size_t matches = 0;
    for( JsonObject entry : entries ) {
        entry.allow_omitted_members();
        if( !entry.has_string( "id" ) || entry.get_string( "id" ) != id ) {
            continue;
        }
        matches++;
        for( const JsonMember member : entry ) {
            out.keys.insert( member.name() );
        }
        if( entry.has_array( "pocket_data" ) ) {
            out.has_pocket_data = true;
            out.pockets_canon = canon_array( entry.get_array( "pocket_data" ) );
        }
    }
    out.found = matches == 1;
    return out;
}

} // namespace

// S1: one id per class of each mod (and the KEEP adaptor) has the planned pocket. The mod is not
// loaded in this world, so the record is compared at JSON level and the case says which path ran.
TEST_CASE( "p3 modsx samples", "[p3][data][MODSX]" ) {
    const modsx_batch &batch = read_batch();
    const std::vector<std::pair<std::string, std::string>> samples = {
        // MagicalNights: MAG, WELL, GUN-INT, TOOL-INT, CHARGES, DDA-POCKET
        { "MagicalNights", "small_mana_crystal" },
        { "MagicalNights", "vibrator_mana" },
        { "MagicalNights", "gunblade" },
        { "MagicalNights", "mspider_box" },
        { "MagicalNights", "heat_cube" },
        { "MagicalNights", "druid_recurve" },
        // exotic_ammo: MAG, WELL, GUN-INT, ADAPTOR (the KEEP row)
        { "exotic_ammo", "223_speedloader5" },
        { "exotic_ammo", "af2011a1_38super" },
        { "exotic_ammo", "l_enforcer_45" },
        { "exotic_ammo", "lwfeed" },
    };
    size_t loaded = 0;
    size_t json = 0;
    for( const auto &sample : samples ) {
        INFO( "mod " << sample.first << " id " << sample.second );
        const modsx_row *row = find_row( batch, sample.second );
        REQUIRE( row != nullptr );
        if( row->not_edited ) {
            json++;
            const own_record own = read_own( row->file, row->id );
            REQUIRE( own.found );
            CHECK_FALSE( own.has_pocket_data );
            if( row->has_own_keys ) {
                const std::set<std::string> expected( row->own_keys.begin(), row->own_keys.end() );
                CHECK( own.keys == expected );
            }
            continue;
        }
        const itype_id type( row->id );
        if( type.is_valid() ) {
            loaded++;
            bool was_loaded = false;
            const std::vector<std::string> failures =
                p3_golden_compare_pockets( row->id, row->pockets_json, was_loaded );
            REQUIRE( was_loaded );
            INFO( "failures: " << failures.size() );
            CHECK( failures.empty() );
            continue;
        }
        json++;
        const own_record own = read_own( row->file, row->id );
        REQUIRE( own.found );
        REQUIRE( own.has_pocket_data );
        INFO( "own " << own.pockets_canon << "\nplan " << row->pockets_canon );
        CHECK( own.pockets_canon == row->pockets_canon );
    }
    std::cout << "p3 modsx samples: ids " << samples.size() << " loaded " << loaded
              << " json " << json << std::endl;
    CHECK( loaded + json == samples.size() );
}

// S2: the charges -> MAGAZINE rows. data/mods/MagicalNights has no .lua file (the draft's Lua
// question): no id has to stay on `charges`, so every row whose legacy record had `max_charges`
// must have lost the key and taken the planned pocket. A kept id would need `max_charges` and no
// magazine pocket; the list `lua_refs` of the batch is the place to add one.
TEST_CASE( "p3 modsx lua ids", "[p3][data][MODSX]" ) {
    const modsx_batch &batch = read_batch();
    CHECK( batch.lua_refs.empty() );
    size_t converted = 0;
    size_t kept = 0;
    for( const auto &entry : batch.items ) {
        const modsx_row &row = entry.second;
        const bool charges_row = row.class_name == "CHARGES" ||
                                 std::find( row.dropped_keys.begin(), row.dropped_keys.end(),
                                            "max_charges" ) != row.dropped_keys.end();
        if( !charges_row ) {
            continue;
        }
        INFO( "converted charges id " << row.id << " file " << row.file );
        const own_record own = read_own( row.file, row.id );
        REQUIRE( own.found );
        converted++;
        CHECK( own.keys.count( "max_charges" ) == 0 );
        if( !row.not_edited ) {
            REQUIRE( own.has_pocket_data );
            CHECK( own.pockets_canon == row.pockets_canon );
        }
    }
    for( const std::string &id : batch.lua_refs ) {
        const modsx_row *row = find_row( batch, id );
        REQUIRE( row != nullptr );
        INFO( "kept lua id " << id );
        const own_record own = read_own( row->file, id );
        REQUIRE( own.found );
        kept++;
        CHECK( own.keys.count( "max_charges" ) == 1 );
        CHECK( own.pockets_canon.find( MAGAZINE_WELL_KEY ) == std::string::npos );
    }
    std::cout << "p3 modsx lua ids: refs " << batch.lua_refs.size() << " kept " << kept
              << " converted " << converted << std::endl;
    // the packet's CHARGES class: 25 rows plus mtailors_kit (re-planned CHARGES on the real base)
    CHECK( converted >= 26 );
}

// S3: JSON level, the world does not load the mods. Every converted record lost the keys the plan
// drops and carries exactly the planned pocket_data; the KEEP / already-converted rows still have
// exactly their own key set; a dropped well id is absent from the pocket.
TEST_CASE( "p3 modsx json check", "[p3][data][MODSX]" ) {
    const modsx_batch &batch = read_batch();
    size_t converted = 0;
    size_t not_edited = 0;
    for( const auto &entry : batch.items ) {
        const modsx_row &row = entry.second;
        INFO( "batch 7-MODS-X id " << row.id << " file " << row.file );
        const own_record own = read_own( row.file, row.id );
        REQUIRE( own.found );
        for( const std::string &key : row.dropped_keys ) {
            INFO( "dropped key " << key );
            CHECK( own.keys.count( key ) == 0 );
        }
        for( const std::string &dropped : row.json_dropped_ids ) {
            INFO( "dropped id " << dropped );
            CHECK( own.pockets_canon.find( '"' + dropped + '"' ) == std::string::npos );
        }
        if( row.not_edited ) {
            not_edited++;
            if( row.has_own_keys ) {
                const std::set<std::string> expected( row.own_keys.begin(), row.own_keys.end() );
                CHECK( own.keys == expected );
            }
            continue;
        }
        converted++;
        REQUIRE( own.has_pocket_data );
        INFO( "own " << own.pockets_canon << "\nplan " << row.pockets_canon );
        CHECK( own.pockets_canon == row.pockets_canon );
    }
    for( const auto &entry : batch.not_edited ) {
        const modsx_row &row = entry.second;
        INFO( "not edited id " << row.id << " file " << row.file );
        const own_record own = read_own( row.file, row.id );
        REQUIRE( own.found );
        not_edited++;
        CHECK_FALSE( own.has_pocket_data );
        if( row.has_own_keys ) {
            const std::set<std::string> expected( row.own_keys.begin(), row.own_keys.end() );
            CHECK( own.keys == expected );
        }
    }
    std::cout << "p3 modsx json: converted " << converted << " not_edited " << not_edited
              << std::endl;
    // 118 packet ids: 116 converted + rune_druid_weapon (already converted by its parent) + lwfeed
    CHECK( batch.items.size() == 117 );
    CHECK( converted == 116 );
    CHECK( not_edited == 2 );
}
