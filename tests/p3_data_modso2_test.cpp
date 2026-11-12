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

// CDF: P3-7-MODS-O part 2 (AUTO-262): the CRT_EXPANSION rows of the packet (10 GUN-INT AUTHOR +
// the 3 STOR6 hand rows crt_em_vest, crt_em_vest_on, crt_solarpack) plus the armor_portion_data
// conversion that makes the mod load on the TORSO2 base. The mod is not in the unit-test world
// (it loads core + TEST_DATA only, tests/test_main.cpp extract_mod_selection), so the loaded-item
// cases can only print these ids as skipped: the port is checked at JSON level here and by the
// tool's `--check` and `--check-mods crt_expansion` in the packet verify. The batch rows are also
// read by the generic cases (tests/p3_batch_generic_test.cpp B1-B5,
// tests/p3_golden_batches_test.cpp G2).
//
// Mutations the cases kill: put `max_charges`/`ammo` back on a converted hand row (S1/S3); change
// one ammo cap or a CONTAINER volume in one mod file (S1/S2); swap the pocket order of a hand row
// (S3); turn a flat `encumbrance`/`coverage` back on crt_backpack/crt_solarpack or drop a portion
// key (A1); edit a row already owned by BATT4/BATT5 (S4). The 12 neg fixtures kill the comparator
// (B5).

// Defined in tests/cdf_mag_port_test.cpp: compares the loaded item `id` with a golden pockets
// array given as JSON text.
auto p3_golden_compare_pockets( const std::string &id, const std::string &pockets_json,
                                bool &was_loaded ) -> std::vector<std::string>;

namespace {

const std::string MODSO2_BATCH_PATH = "tests/data/dda_staged/p3_batches/7-MODSO2.json";

// CDF: the loader gives a restricted well 2 km; the plan/golden does not carry the key.
const std::string MAGAZINE_WELL_KEY = "\"MAGAZINE_WELL\"";

// CDF: rows owned by an already merged packet (BATT5: the four integral CRT battery tools;
// BATT4: crt_mess_kit). They are pinned, not edited: the batch keeps them in `not_edited`.
const std::vector<std::string> OWNED_ELSEWHERE = {
    "crt_gasmask", "crt_gasmask_on", "crt_helmet", "crt_helmet_on", "crt_mess_kit" };

// The two armor rows whose legacy flat keys cannot sit on a specifically_covers parent.
const std::vector<std::string> ARMOR_ROWS = { "crt_backpack", "crt_solarpack" };

struct modso2_row {
    std::string id;
    std::string file;
    std::string class_name;
    std::string action;
    std::string pockets_json;
    std::string pockets_canon;
    std::vector<std::string> dropped_keys;
    std::vector<std::string> own_keys;
    std::vector<std::string> flat_keys;
    std::string armor_json;
    std::string armor_canon;
    bool has_own_keys = false;
    bool has_armor = false;
    bool not_edited = false;
    bool hand_row = false;
};

struct modso2_batch {
    std::map<std::string, modso2_row> items;
    std::map<std::string, modso2_row> unloaded_pockets;
    std::map<std::string, modso2_row> not_edited;
    std::map<std::string, modso2_row> armor;
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
        parts.erase( "max_item_length" );
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
    return "null";
}

auto read_row( const JsonMember &member, bool force_not_edited ) -> modso2_row
{
    JsonObject row = member.get_object();
    row.allow_omitted_members();
    modso2_row out;
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
    if( row.has_array( "own_keys" ) ) {
        out.has_own_keys = true;
        out.own_keys = row.get_string_array( "own_keys" );
    }
    if( row.has_array( "flat_keys" ) ) {
        out.flat_keys = row.get_string_array( "flat_keys" );
    }
    if( row.has_array( "pocket_data" ) ) {
        JsonArray pockets = row.get_array( "pocket_data" );
        out.pockets_canon = canon_array( pockets );
        out.pockets_json = pockets.str();
    }
    if( row.has_array( "armor_portion_data" ) ) {
        JsonArray portions = row.get_array( "armor_portion_data" );
        out.has_armor = true;
        out.armor_canon = canon_array( portions );
        out.armor_json = portions.str();
    }
    return out;
}

auto read_batch() -> const modso2_batch & {
    static modso2_batch batch;
    static bool read = false;
    if( read ) {
        return batch;
    }
    read = true;

    std::ifstream file( MODSO2_BATCH_PATH, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    REQUIRE( root.get_string( "batch" ) == "7-MODSO2" );

    const auto read_group = [&]( const char *name, std::map<std::string, modso2_row> &rows_out,
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
    read_group( "unloaded_pockets", batch.unloaded_pockets, false );
    read_group( "not_edited", batch.not_edited, true );
    read_group( "armor", batch.armor, false );
    return batch;
}

struct own_record {
    bool found = false;
    bool has_pocket_data = false;
    std::string pockets_canon;
    bool has_armor = false;
    std::string armor_canon;
    std::set<std::string> keys;
};

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
        if( entry.has_array( "armor_portion_data" ) ) {
            out.has_armor = true;
            out.armor_canon = canon_array( entry.get_array( "armor_portion_data" ) );
        }
    }
    out.found = matches == 1;
    return out;
}

auto find_row( const modso2_batch &batch, const std::string &id ) -> const modso2_row *
{
    for( const auto *group : { &batch.items, &batch.unloaded_pockets, &batch.not_edited,
                              &batch.armor } ) {
        const auto found = group->find( id );
        if( found != group->end() ) {
            return &found->second;
        }
    }
    return nullptr;
}

} // namespace

// S1: JSON level, the world does not load the mod. Every converted record lost the keys the plan
// drops and carries exactly the planned pocket_data.
TEST_CASE( "p3 modso2 json check", "[p3][data][MODSO][MODSO2]" ) {
    const modso2_batch &batch = read_batch();
    size_t converted = 0;
    const auto check_converted = [&]( const modso2_row &row ) {
        INFO( "batch 7-MODSO2 id " << row.id << " file " << row.file );
        const own_record own = read_own( row.file, row.id );
        REQUIRE( own.found );
        for( const std::string &key : row.dropped_keys ) {
            INFO( "dropped key " << key );
            CHECK( own.keys.count( key ) == 0 );
        }
        REQUIRE( own.has_pocket_data );
        INFO( "own " << own.pockets_canon << "\nplan " << row.pockets_canon );
        CHECK( own.pockets_canon == row.pockets_canon );
        converted++;
    };
    for( const auto &entry : batch.items ) {
        check_converted( entry.second );
    }
    for( const auto &entry : batch.unloaded_pockets ) {
        check_converted( entry.second );
    }
    std::cout << "p3 modso2 json: converted " << converted << " not_edited "
              << batch.not_edited.size() << std::endl;
    // 13 rows: 10 GUN-INT in `items` + the 3 STOR6 hand rows in `unloaded_pockets` (their golden
    // pocket list has more than one pocket, they are mod items and the generic B3 counts every
    // multi-pocket golden id of a batch as loaded, so S1 reads them here).
    CHECK( batch.items.size() == 10 );
    CHECK( batch.unloaded_pockets.size() == 3 );
    CHECK( converted == 13 );
    // the 5 rows another merged packet owns stay pinned, not edited
    CHECK( batch.not_edited.size() == OWNED_ELSEWHERE.size() );
}

// S2: samples across the classes and the two copy-from shapes. The mod is not loaded in this
// world, so the record is compared at JSON level and the case says which path ran.
TEST_CASE( "p3 modso2 samples", "[p3][data][MODSO][MODSO2]" ) {
    const modso2_batch &batch = read_batch();
    const std::vector<std::pair<std::string, std::string>> samples = {
        // GUN-INT, a no-copy-from gun and a copy-from energy gun (CDF ammo kept)
        { "GUN-INT", "crt_cqb_si" },
        { "GUN-INT", "crt_pelletsniper" },
        { "GUN-INT", "ds_rivet_gun" },
        // a GUNMOD whose gun_data carries the ammo
        { "GUN-INT", "flare_gmod" },
        // the STOR6 TOOL-INT rows: integral battery plus the storage as a native pocket
        { "TOOL-INT", "crt_em_vest" },
        { "TOOL-INT", "crt_em_vest_on" },
        { "TOOL-INT", "crt_solarpack" },
        // a row BATT5 already owns: pinned as not edited
        { "TOOL-INT", "crt_helmet" },
    };
    size_t loaded = 0;
    size_t json = 0;
    for( const auto &sample : samples ) {
        INFO( "class " << sample.first << " id " << sample.second );
        const modso2_row *row = find_row( batch, sample.second );
        REQUIRE( row != nullptr );
        if( row->not_edited ) {
            json++;
            const own_record own = read_own( row->file, row->id );
            REQUIRE( own.found );
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
    std::cout << "p3 modso2 samples: ids " << samples.size() << " loaded " << loaded << " json "
              << json << std::endl;
    CHECK( loaded + json == samples.size() );
}

// S3: the 3 STOR6 hand rows have exactly the planned pocket list, the storage is that list's
// CONTAINER, and the CDF legacy keys the tool retires are gone. Mutations: put max_charges or
// `ammo` back, change one volume, swap the MAGAZINE and CONTAINER pockets.
TEST_CASE( "p3 modso2 hand rows", "[p3][data][MODSO][MODSO2]" ) {
    const modso2_batch &batch = read_batch();
    const std::vector<std::pair<std::string, std::string>> hand_ids = {
        { "crt_em_vest", "2500 ml" },
        { "crt_em_vest_on", "2500 ml" },
        { "crt_solarpack", "9 L" },
    };
    size_t checked = 0;
    for( const auto &hand : hand_ids ) {
        INFO( "hand row " << hand.first );
        const modso2_row *row = find_row( batch, hand.first );
        REQUIRE( row != nullptr );
        CHECK( row->hand_row );
        const own_record own = read_own( row->file, hand.first );
        REQUIRE( own.found );
        REQUIRE( own.has_pocket_data );
        INFO( "own " << own.pockets_canon << "\nplan " << row->pockets_canon );
        CHECK( own.pockets_canon == row->pockets_canon );
        // CDF: R4 legacy keys: max_charges moves into the pocket, ammo is DDA's tool_ammo
        CHECK( own.keys.count( "max_charges" ) == 0 );
        CHECK( own.keys.count( "ammo" ) == 0 );
        CHECK( own.keys.count( "tool_ammo" ) == 1 );
        CHECK( own.keys.count( "magazines" ) == 0 );
        CHECK( own.keys.count( "magazine_well" ) == 0 );
        CHECK( own.keys.count( "clip_size" ) == 0 );
        // CDF storage stays CDF: `storage` is the same capacity as the CONTAINER pocket
        CHECK( own.keys.count( "storage" ) == 1 );
        // the plan carries the same CONTAINER volume (S1 compares it with the record)
        CHECK( row->pockets_canon.find( '"' + hand.second + '"' ) != std::string::npos );
        checked++;
    }
    std::cout << "p3 modso2 hand rows: checked " << checked << std::endl;
    CHECK( checked == 3 );
}

// A1: crt_backpack and crt_solarpack are copy-from children of molle_pack, whose armor data names
// specifically_covers (= torso_hanging_back); a flat encumbrance/max_encumbrance/coverage/covers
// on the child is a loader error on this base. CDF: the flat numbers move into one portion with
// the parent's placement ([ encumbrance, max_encumbrance ] keeps both CDF numbers).
// Mutation: restore a flat key, or drop/change a portion key (the mod stops loading).
TEST_CASE( "p3 modso2 armor portions", "[p3][data][MODSO][MODSO2]" ) {
    const modso2_batch &batch = read_batch();
    size_t checked = 0;
    for( const std::string &id : ARMOR_ROWS ) {
        INFO( "armor row " << id );
        // crt_solarpack is also a pocket row in `unloaded_pockets`: read the armor group directly
        const auto found = batch.armor.find( id );
        REQUIRE( found != batch.armor.end() );
        const modso2_row *row = &found->second;
        CHECK( row->action == "ARMOR-PORTION" );
        const own_record own = read_own( row->file, id );
        REQUIRE( own.found );
        for( const std::string &key : row->flat_keys ) {
            INFO( "flat key " << key );
            CHECK( own.keys.count( key ) == 0 );
        }
        REQUIRE( own.has_armor );
        INFO( "own " << own.armor_canon << "\nplan " << row->armor_canon );
        CHECK( own.armor_canon == row->armor_canon );
        checked++;
    }
    std::cout << "p3 modso2 armor portions: checked " << checked << std::endl;
    CHECK( checked == ARMOR_ROWS.size() );
}

// S4: the rows another merged packet owns are not edited here: their own key set is pinned.
// Mutation: add or remove one key on one of them (a second packet touching the same object).
TEST_CASE( "p3 modso2 rows owned elsewhere", "[p3][data][MODSO][MODSO2]" ) {
    const modso2_batch &batch = read_batch();
    size_t checked = 0;
    for( const std::string &id : OWNED_ELSEWHERE ) {
        INFO( "owned-elsewhere row " << id );
        const auto found = batch.not_edited.find( id );
        REQUIRE( found != batch.not_edited.end() );
        const modso2_row &row = found->second;
        REQUIRE( row.has_own_keys );
        const own_record own = read_own( row.file, id );
        REQUIRE( own.found );
        const std::set<std::string> expected( row.own_keys.begin(), row.own_keys.end() );
        CHECK( own.keys == expected );
        checked++;
    }
    std::cout << "p3 modso2 owned elsewhere: checked " << checked << std::endl;
    CHECK( checked == OWNED_ELSEWHERE.size() );
}
