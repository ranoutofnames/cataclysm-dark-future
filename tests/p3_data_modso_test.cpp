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

// CDF: P3-7-MODS-O part 1: the Aftershock, Arcana_BN, Civilians, CrazyCataclysm, DinoMod, Jump Pad,
// flowerpots_plus, smart_house_remotes and teleportation_mod rows of the packet. CRT_EXPANSION is
// part 2 (AUTO-262) and is not in the batch. The mods are not in the unit-test world (it loads core
// + TEST_DATA only, tests/test_main.cpp extract_mod_selection), so the loaded-item cases can only
// print these ids as skipped: the port is checked at JSON level here and by the tool's `--check` in
// the packet verify. The batch rows are also read by the generic cases
// (tests/p3_batch_generic_test.cpp B1-B5, tests/p3_golden_batches_test.cpp G2).
//
// S1 pins every converted own record: the dropped keys and ids are gone and the own `pocket_data`
// equals the plan. S2 pins one id per mod (and the rows whose effective pocket comes from an
// already ported copy-from parent or a LEAVE row). S3 pins the two hand rows (afs_eidolon_derringer,
// laser_rifle_dino2: AUTO-239 #3 and #6) and prints the 8 CRT_EXPANSION hand rows left to part 2.
//
// Mutations the cases kill: put `magazines`/`max_charges`/`magazine_well`/`clip_size` back on one
// converted record (S1/B1); change one pocket key or cap in a mod file (S1/S2); swap the hand rows'
// pocket shape (S3); edit a not_edited record (S1/S2).

// Defined in tests/cdf_mag_port_test.cpp: compares the loaded item `id` with a golden pockets
// array given as JSON text.
auto p3_golden_compare_pockets( const std::string &id, const std::string &pockets_json,
                                bool &was_loaded ) -> std::vector<std::string>;

namespace {

const std::string MODSO_BATCH_PATH = "tests/data/dda_staged/p3_batches/7-MODS-O.json";

// CDF: the loader gives a restricted well 2 km (src/item_factory.cpp synthesize_pockets); the CDF
// test `magazine wells have an explicit length limit` (tests/item_length_test.cpp:161) requires
// every MAGAZINE_WELL pocket to state it, so the data has the key and the plan/golden does not.
// Compare wells without it, as tools/cdf_port_dda_mags.py --check does (well_length_agnostic).
const std::string MAGAZINE_WELL_KEY = "\"MAGAZINE_WELL\"";
const std::string WELL_LENGTH_KEY = "max_item_length";

// CDF: the 8 CRT_EXPANSION hand rows (HAND-FALLBACK) are part 2 (AUTO-262), not in this batch.
const std::vector<std::string> CRT_HAND_ROWS = {
    "crt_em_vest",  "crt_em_vest_on", "crt_gasmask", "crt_gasmask_on",
    "crt_helmet",   "crt_helmet_on",  "crt_mess_kit", "crt_solarpack" };

struct modso_row {
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
    bool hand_row = false;
};

struct modso_batch {
    std::map<std::string, modso_row> items;
    std::map<std::string, modso_row> unloaded_pockets;   // converted, but >1 golden pocket
    std::map<std::string, modso_row> not_edited;
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

auto read_row( const JsonMember &member, bool force_not_edited ) -> modso_row
{
    JsonObject row = member.get_object();
    row.allow_omitted_members();
    modso_row out;
    out.id = member.name();
    out.file = row.get_string( "file" );
    out.class_name = row.get_string( "class" );
    out.action = row.get_string( "action" );
    out.not_edited = force_not_edited ||
                     ( row.has_bool( "not_edited" ) && row.get_bool( "not_edited" ) );
    out.hand_row = row.has_bool( "hand_fallback" ) && row.get_bool( "hand_fallback" );
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
    if( row.has_array( "pockets" ) ) {
        JsonArray pockets = row.get_array( "pockets" );
        out.pockets_canon = canon_array( pockets );
        out.pockets_json = pockets.str();
    }
    return out;
}

auto read_batch() -> const modso_batch & {
    static modso_batch batch;
    static bool read = false;
    if( read ) {
        return batch;
    }
    read = true;

    std::ifstream file( MODSO_BATCH_PATH, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    REQUIRE( root.get_string( "batch" ) == "7-MODS-O" );

    if( root.has_object( "lua_refs" ) ) {
        JsonObject refs = root.get_object( "lua_refs" );
        refs.allow_omitted_members();
        for( const JsonMember member : refs ) {
            batch.lua_refs.push_back( member.name() );
        }
    }

    const auto read_group = [&]( const char *name, std::map<std::string, modso_row> &rows_out,
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
    // CDF: converted rows whose golden pocket list has more than one pocket (robe_shadow and
    // robe_shadow_on). They are mod items, so they do not load; the generic B3 counts every
    // multi-pocket id of a batch as loaded, so they sit in this group and S1 checks them.
    read_group( "unloaded_pockets", batch.unloaded_pockets, false );
    read_group( "not_edited", batch.not_edited, true );
    return batch;
}

auto find_row( const modso_batch &batch, const std::string &id ) -> const modso_row *
{
    const auto item = batch.items.find( id );
    if( item != batch.items.end() ) {
        return &item->second;
    }
    const auto uncounted = batch.unloaded_pockets.find( id );
    if( uncounted != batch.unloaded_pockets.end() ) {
        return &uncounted->second;
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

// S1: JSON level, the world does not load the mods. Every converted record lost the keys and ids
// the plan drops and carries exactly the planned pocket_data; the rows that need no edit (a
// copy-from parent is already ported, or the LEAVE row) still have exactly their own key set.
TEST_CASE( "p3 modso json check", "[p3][data][MODSO]" ) {
    const modso_batch &batch = read_batch();
    size_t converted = 0;
    size_t not_edited = 0;
    const auto check_converted = [&]( const modso_row &row ) {
        INFO( "batch 7-MODS-O id " << row.id << " file " << row.file );
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
            return;
        }
        converted++;
        REQUIRE( own.has_pocket_data );
        INFO( "own " << own.pockets_canon << "\nplan " << row.pockets_canon );
        CHECK( own.pockets_canon == row.pockets_canon );
    };
    for( const auto &entry : batch.items ) {
        check_converted( entry.second );
    }
    for( const auto &entry : batch.unloaded_pockets ) {
        check_converted( entry.second );
    }
    for( const auto &entry : batch.not_edited ) {
        const modso_row &row = entry.second;
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
    std::cout << "p3 modso json: converted " << converted << " not_edited " << not_edited
              << std::endl;
    // 110 converted-or-kept rows: 108 converted (106 in items + the 2 multi-pocket rows of
    // unloaded_pockets) plus civilian_cop_glock22 and plasma_ejector (effective pocket from the
    // already ported parent) and afs_hardlight_longbow (LEAVE)
    CHECK( batch.items.size() == 108 );
    CHECK( batch.unloaded_pockets.size() == 2 );
    CHECK( converted == 108 );
    CHECK( not_edited == 3 );
}

// S2: one id per mod, plus the two rows whose effective pocket comes from an already ported parent
// and the LEAVE row. The mod is not loaded in this world, so the record is compared at JSON level
// and the case says which path ran.
TEST_CASE( "p3 modso samples", "[p3][data][MODSO]" ) {
    const modso_batch &batch = read_batch();
    const std::vector<std::pair<std::string, std::string>> samples = {
        // Aftershock: GUN-INT, TOOL-INT, CHARGES, and the hand row
        { "Aftershock", "afs_rebar_rifle" },
        { "Aftershock", "afs_murdersaw" },
        { "Aftershock", "afs_holo_cloak_mk2" },
        { "Aftershock", "afs_eidolon_derringer" },
        // Arcana_BN: MAG, GUN-INT, TOOL-INT, the EXPLICIT storage row
        { "Arcana_BN", "arcana_mech_power_cell" },
        { "Arcana_BN", "bloodscourge" },
        { "Arcana_BN", "bloodaxe" },
        { "Arcana_BN", "robe_shadow" },
        // Civilians: WELL and the row inherited from the ported glock_22
        { "Civilians", "civilian_survivor_guardian_smg_gun" },
        { "Civilians", "civilian_cop_glock22" },
        // CrazyCataclysm: WELL
        { "CrazyCataclysm", "yeet_cannon" },
        // DinoMod: the hand row (DDA well)
        { "DinoMod", "laser_rifle_dino2" },
        // flowerpots_plus: TOOL-INT
        { "flowerpots_plus", "portable_planter_m" },
    };
    size_t loaded = 0;
    size_t json = 0;
    for( const auto &sample : samples ) {
        INFO( "mod " << sample.first << " id " << sample.second );
        const modso_row *row = find_row( batch, sample.second );
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
    std::cout << "p3 modso samples: ids " << samples.size() << " loaded " << loaded << " json "
              << json << std::endl;
    CHECK( loaded + json == samples.size() );
}

// S3: the two hand rows of part 1 have exactly the planned pocket and no legacy key. The 8
// CRT_EXPANSION hand rows are part 2 and must not be in this batch. Mutations: swap the two pocket
// shapes, or put clip_size back on laser_rifle_dino2.
TEST_CASE( "p3 modso hand rows", "[p3][data][MODSO]" ) {
    const modso_batch &batch = read_batch();
    const std::vector<std::string> hand_ids = { "afs_eidolon_derringer", "laser_rifle_dino2" };
    size_t checked = 0;
    for( const std::string &id : hand_ids ) {
        INFO( "hand row " << id );
        const modso_row *row = find_row( batch, id );
        REQUIRE( row != nullptr );
        CHECK( row->hand_row );
        const own_record own = read_own( row->file, id );
        REQUIRE( own.found );
        REQUIRE( own.has_pocket_data );
        INFO( "own " << own.pockets_canon << "\nplan " << row->pockets_canon );
        CHECK( own.pockets_canon == row->pockets_canon );
        // CDF: no record of the copy-from chain may keep the legacy magazine keys
        CHECK( own.keys.count( "magazines" ) == 0 );
        CHECK( own.keys.count( "magazine_well" ) == 0 );
        checked++;
    }
    // afs_eidolon_derringer: DDA's integral 4-round magazine (5x50 kept, CDF ammo)
    const modso_row *derringer = find_row( batch, "afs_eidolon_derringer" );
    REQUIRE( derringer != nullptr );
    CHECK( derringer->class_name == "GUN-INT" );
    CHECK( derringer->pockets_canon.find( MAGAZINE_WELL_KEY ) == std::string::npos );
    CHECK( derringer->pockets_canon.find( "\"MAGAZINE\"" ) != std::string::npos );
    // laser_rifle_dino2: the DDA well (AUTO-239 #3), so the CDF clip_size 1 is gone
    const modso_row *laser = find_row( batch, "laser_rifle_dino2" );
    REQUIRE( laser != nullptr );
    CHECK( laser->class_name == "WELL" );
    CHECK( laser->pockets_canon.find( MAGAZINE_WELL_KEY ) != std::string::npos );
    const own_record laser_own = read_own( laser->file, laser->id );
    CHECK( laser_own.keys.count( "clip_size" ) == 0 );
    // the CRT_EXPANSION hand rows are part 2 (AUTO-262): none of them is in this batch
    size_t crt = 0;
    for( const std::string &id : CRT_HAND_ROWS ) {
        if( find_row( batch, id ) == nullptr ) {
            crt++;
        }
    }
    std::cout << "p3 modso hand rows: checked " << checked << " crt_part2 " << crt << std::endl;
    CHECK( checked == 2 );
    CHECK( crt == CRT_HAND_ROWS.size() );
}
