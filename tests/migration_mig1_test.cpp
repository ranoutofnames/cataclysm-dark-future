#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "json.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "state_helpers.h"
#include "type_id.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// CDF: P3-MIG1 (PL-45, PL-45-b, PL-51 part 2). The packet turns the 5 CDF items that were pure
// aliases of a DDA MIGRATION source into MIGRATION objects (data/json/obsoletion/migration.json),
// deletes the 5 recipes that produced them (DDA did the same: 8d184c95a8e for lard,
// c247b4237c5 for imitation peanut butter) and rewrites every reference to the target id.
//
// Fixtures (inputs, re-derived on the MIG1 base with the PL-45 rules; the numbers are re-derived,
// not copied from the draft):
//   tests/data/migration/mig1-target.json       convert / ask_owner / keep / recipes / sources
//   tests/data/migration/neg-mig1.json          mutation inputs for M-1..M-3
//   tests/data/migration/obsol-coverage.tsv     the 28 ids CDF obsoleted and DDA migrates
//   tests/data/migration/neg-mig1-coverage.json mutation inputs for M-7/M-8
//
// Loader facts the tests pin (src/item_factory.cpp:4187-4235, src/filesystem.cpp:381-383):
// `load_migration` does `migrations[ m.id ] = m` for every id of every object, so a duplicate
// MIGRATION id is not an error and the object loaded last wins; files load in sorted path order.
// M-6 makes the order explicit by pinning that no id is defined twice in data/json/obsoletion/*,
// which is why the 5 converted ids can live in the existing obsoletion/migration.json.

namespace {

const std::string MIG1_TARGET = "tests/data/migration/mig1-target.json";
const std::string MIG1_NEG = "tests/data/migration/neg-mig1.json";
const std::string MIG1_COVERAGE = "tests/data/migration/obsol-coverage.tsv";
const std::string MIG1_NEG_COVERAGE = "tests/data/migration/neg-mig1-coverage.json";
const std::string OBSOLETION_DIR = "data/json/obsoletion";
const std::string MIGRATION_FILE = "data/json/obsoletion/migration.json";

// id -> target (MIGRATION replace, or the DDA target of a kept item).
using id_map = std::map<std::string, std::string>;
// file -> id -> occurrences of `"id"`.
using ref_counts = std::map<std::string, std::map<std::string, int>>;

struct mig1_recipe {
    std::string id;
    std::string result;
    std::vector<std::string> uses; // ids the rewritten components must still list
};

struct mig1_source {
    std::string item;
    std::string recipe; // empty when the source is the item group
    std::string group;  // empty when the source is the recipe
};

struct mig1_target {
    id_map convert;
    id_map ask_owner;
    std::map<std::string, std::string> keep; // id -> MIG1 class
    std::vector<mig1_recipe> recipes;
    std::vector<mig1_source> sources;
};

struct mig1_coverage_row {
    std::string id;
    std::string kind;
    std::string dda_replace;
    std::string cdf_replace;
    std::string disposition;
};

struct mig1_mutation {
    std::string test_case;
    std::string kind;
    std::string id;
    std::string replace;
    std::string probe;
};

auto read_target() -> mig1_target {
    std::ifstream file( MIG1_TARGET, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    mig1_target out;
    for( const JsonMember member : root.get_object( "convert" ) ) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        out.convert[member.name()] = row.get_string( "replace" );
    }
    for( const JsonMember member : root.get_object( "ask_owner" ) ) {
        out.ask_owner[member.name()] = member.get_string();
    }
    for( const JsonMember member : root.get_object( "keep" ) ) {
        out.keep[member.name()] = member.get_string();
    }
    for( JsonObject row : root.get_array( "recipes" ) ) {
        row.allow_omitted_members();
        mig1_recipe r;
        r.id = row.get_string( "id" );
        r.result = row.get_string( "result" );
        if( row.has_array( "uses" ) ) {
            for( const std::string &id : row.get_string_array( "uses" ) ) {
                r.uses.push_back( id );
            }
        }
        out.recipes.push_back( r );
    }
    for( JsonObject row : root.get_array( "sources" ) ) {
        row.allow_omitted_members();
        mig1_source s;
        s.item = row.get_string( "item" );
        s.recipe = row.get_string( "recipe", "" );
        s.group = row.get_string( "group", "" );
        out.sources.push_back( s );
    }
    return out;
}

auto read_mutations( const std::string &path ) -> std::vector<mig1_mutation> {
    std::ifstream file( path, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    std::vector<mig1_mutation> out;
    for( JsonObject row : root.get_array( "mutations" ) ) {
        row.allow_omitted_members();
        mig1_mutation m;
        m.test_case = row.get_string( "case" );
        m.kind = row.get_string( "kind" );
        m.id = row.get_string( "id" );
        m.replace = row.get_string( "replace", "" );
        m.probe = row.get_string( "probe", "" );
        out.push_back( m );
    }
    return out;
}

auto split_tabs( const std::string &line ) -> std::vector<std::string> {
    std::vector<std::string> cells;
    size_t begin = 0;
    while( true ) {
        const size_t tab = line.find( '\t', begin );
        cells.push_back( line.substr( begin, tab == std::string::npos ? std::string::npos :
                                      tab - begin ) );
        if( tab == std::string::npos ) {
            return cells;
        }
        begin = tab + 1;
    }
}

auto read_coverage() -> std::vector<mig1_coverage_row> {
    std::ifstream file( MIG1_COVERAGE, std::ios::binary );
    REQUIRE( file.good() );
    std::vector<mig1_coverage_row> out;
    std::string line;
    bool header = true;
    while( std::getline( file, line ) ) {
        if( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        if( header ) {
            header = false;
            continue;
        }
        if( line.empty() ) {
            continue;
        }
        const std::vector<std::string> cells = split_tabs( line );
        REQUIRE( cells.size() == 5 );
        out.push_back( mig1_coverage_row{ cells[0], cells[1], cells[2], cells[3], cells[4] } );
    }
    return out;
}

// id -> the obsoletion files that define it. With `only_migrations` only MIGRATION objects are
// collected (the ids the loader's map can silently overwrite); otherwise every object with an
// `id` counts (the CDF obsoletion set of the coverage table).
auto obsoletion_owners( bool only_migrations ) -> std::map<std::string, std::vector<std::string>> {
    std::map<std::string, std::vector<std::string>> out;
    for( const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator( OBSOLETION_DIR ) ) {
        if( entry.path().extension() != ".json" ) {
            continue;
        }
        std::ifstream file( entry.path(), std::ios::binary );
        REQUIRE( file.good() );
        JsonIn jsin( file );
        JsonArray entries = jsin.get_array();
        for( JsonObject object : entries ) {
            object.allow_omitted_members();
            if( only_migrations && ( !object.has_string( "type" ) ||
                                     object.get_string( "type" ) != "MIGRATION" ) ) {
                continue;
            }
            if( object.has_string( "id" ) ) {
                out[object.get_string( "id" )].push_back( entry.path().generic_string() );
            } else if( object.has_array( "id" ) ) {
                for( const std::string &id : object.get_string_array( "id" ) ) {
                    out[id].push_back( entry.path().generic_string() );
                }
            }
        }
    }
    return out;
}

// `"id"` with its quotes: `"lard"` never matches inside `"human_lard"`.
auto token_count( const std::string &text, const std::string &id ) -> int {
    const std::string needle = "\"" + id + "\"";
    int found = 0;
    size_t pos = 0;
    while( ( pos = text.find( needle, pos ) ) != std::string::npos ) {
        found++;
        pos += needle.size();
    }
    return found;
}

auto scan_references( const std::set<std::string> &ids, int &files_scanned ) -> ref_counts {
    ref_counts out;
    for( const std::string &root : { "data/json", "data/mods" } ) {
        for( const std::filesystem::directory_entry &entry :
             std::filesystem::recursive_directory_iterator( root ) ) {
            if( entry.path().extension() != ".json" ) {
                continue;
            }
            std::ifstream file( entry.path(), std::ios::binary );
            if( !file.good() ) {
                continue;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            const std::string text = buffer.str();
            files_scanned++;
            for( const std::string &id : ids ) {
                const int found = token_count( text, id );
                if( found > 0 ) {
                    out[entry.path().generic_string()][id] += found;
                }
            }
        }
    }
    return out;
}

// M-2 body: the only place a converted id may appear is its own MIGRATION object, once.
auto stray_violations( const std::set<std::string> &ids, const ref_counts &refs )
-> std::vector<std::string> {
    std::vector<std::string> out;
    const auto migration = refs.find( MIGRATION_FILE );
    for( const auto &file : refs ) {
        for( const auto &id : file.second ) {
            if( file.first == MIGRATION_FILE ) {
                if( id.second != 1 ) {
                    out.push_back( id.first + ": " + std::to_string( id.second ) +
                                   " occurrences in " + file.first +
                                   " (expected exactly one MIGRATION object)" );
                }
            } else {
                out.push_back( id.first + ": stray reference in " + file.first );
            }
        }
    }
    for( const std::string &id : ids ) {
        if( migration == refs.end() || migration->second.count( id ) == 0 ) {
            out.push_back( id + ": no MIGRATION object in " + MIGRATION_FILE );
        }
    }
    return out;
}

// M-1 body: the alias is no longer an itype, it resolves to the target, the target exists, and no
// recipe still produces it (the loader deletes recipes whose result was migrated:
// src/item_factory.cpp:1553-1556, so a left-over recipe would be dead data).
auto alias_violations( const id_map &convert, const std::set<std::string> &defined,
                       const id_map &migrated ) -> std::vector<std::string> {
    std::vector<std::string> out;
    for( const auto &row : convert ) {
        if( defined.count( row.first ) > 0 ) {
            out.push_back( row.first + ": still a defined itype" );
        }
        const auto target = migrated.find( row.first );
        const std::string actual = target == migrated.end() ? row.first : target->second;
        if( actual != row.second ) {
            out.push_back( row.first + ": migrates to " + actual + ", expected " + row.second );
        }
        if( defined.count( row.second ) == 0 ) {
            out.push_back( row.first + ": target " + row.second + " is not defined" );
        }
    }
    return out;
}

// M-3 body: every kept item is still a defined itype and is not a MIGRATION source.
auto keep_violations( const std::vector<std::string> &ids, const std::set<std::string> &defined,
                      const id_map &migrated ) -> std::vector<std::string> {
    std::vector<std::string> out;
    for( const std::string &id : ids ) {
        if( defined.count( id ) == 0 ) {
            out.push_back( id + ": no longer a defined itype" );
        }
        const auto target = migrated.find( id );
        if( target != migrated.end() && target->second != id ) {
            out.push_back( id + ": migrated to " + target->second +
                           ", a kept item must not migrate" );
        }
    }
    return out;
}

// M-7 body: the 12 item-only rows still have their obsoletion item object; the 16 MIGRATION rows
// still exist and carry the target of the table.
auto coverage_violations( const std::vector<mig1_coverage_row> &rows,
                          const std::function<bool( const itype_id & )> &is_defined,
                          const std::function<std::string( const std::string & )> &migrated )
-> std::vector<std::string> {
    std::vector<std::string> out;
    for( const mig1_coverage_row &row : rows ) {
        if( row.kind == "item" ) {
            if( !is_defined( itype_id( row.id ) ) ) {
                out.push_back( row.id + ": the obsoletion item object is gone" );
            }
            continue;
        }
        const std::string actual = migrated( row.id );
        if( actual != row.cdf_replace ) {
            out.push_back( row.id + ": migrates to " + actual + ", the table says " +
                           row.cdf_replace );
        }
    }
    return out;
}

// M-8 body: the table is exactly the pinned 28 ids, once each, with a disposition, and every row is
// a CDF obsoletion id.
auto table_violations( const std::vector<mig1_coverage_row> &rows,
                       const std::set<std::string> &expected,
                       const std::set<std::string> &obsoletion ) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for( const mig1_coverage_row &row : rows ) {
        if( !seen.insert( row.id ).second ) {
            out.push_back( row.id + ": listed twice" );
        }
        if( row.disposition.empty() ) {
            out.push_back( row.id + ": empty disposition" );
        }
        if( obsoletion.count( row.id ) == 0 ) {
            out.push_back( row.id + ": not a CDF obsoletion id" );
        }
        if( expected.count( row.id ) == 0 ) {
            out.push_back( row.id + ": not one of the 28 ids" );
        }
    }
    for( const std::string &id : expected ) {
        if( seen.count( id ) == 0 ) {
            out.push_back( id + ": no row in the table" );
        }
    }
    return out;
}

auto recipe_lists( const recipe &r, const std::string &id ) -> bool {
    for( const std::vector<item_comp> &alternatives :
         r.simple_requirements().get_components() ) {
        for( const item_comp &comp : alternatives ) {
            if( comp.type == itype_id( id ) ) {
                return true;
            }
        }
    }
    return false;
}

auto join( const std::vector<std::string> &lines ) -> std::string {
    std::string out;
    for( size_t i = 0; i < lines.size() && i < 15; i++ ) {
        out += lines[i] + "\n";
    }
    return out;
}

// Does any violation line name `id`? (The checker reports one line per broken row.)
auto names_id( const std::vector<std::string> &violations, const std::string &id ) -> bool {
    for( const std::string &line : violations ) {
        if( line.rfind( id + ":", 0 ) == 0 ) {
            return true;
        }
    }
    return false;
}

// The 28 ids that are both a CDF obsoletion id and a DDA MIGRATION source (PL-45-b method:
// CDF obsoletion item objects + MIGRATION ids of data/json/obsoletion/*, DDA's 857 core migration
// sources; re-derived on the MIG1 base, PL-51 part 2 table).
const std::set<std::string> &coverage_ids() {
    static const std::set<std::string> ids = {
        "40x46mm_buckshot_m118", "40x46mm_buckshot_m199", "40x46mm_flechette_m118",
        "40x46mm_flechette_m199", "40x46mm_slug_m118", "40x46mm_slug_m199", "8mm_fmj",
        "AID_bio_meteorologist", "AID_bio_watch", "blueberries_cooked", "famasmag", "gobag",
        "mask_wsurvivorxl", "personal_gobag", "power_armor_basic", "power_armor_basic_on",
        "power_armor_heavy", "power_armor_heavy_on", "power_armor_light", "power_armor_light_on",
        "rifle_40", "rifle_45", "rifle_9mm", "sandwich_cucumber", "strawberries_cooked",
        "superalloy_harness_dog", "surv_hand_cannon", "surv_six_shooter"
    };
    return ids;
}

} // namespace

// M-1: each converted id is no longer a defined itype, resolves to its DDA target, and the target
// is defined. Mutation killed: N1 (the alias item left in the data), N4 (a target that does not
// exist in CDF).
TEST_CASE( "mig1 aliases migrate", "[mig1][migration]" ) {
    clear_all_state();
    const mig1_target target = read_target();
    REQUIRE( target.convert.size() == 5 );

    std::set<std::string> defined;
    id_map migrated;
    for( const auto &row : target.convert ) {
        for( const std::string &id : { row.first, row.second } ) {
            if( itype_id( id ).is_valid() ) {
                defined.insert( id );
            }
        }
        migrated[row.first] = item_controller->migrate_id( itype_id( row.first ) ).str();
    }
    const std::vector<std::string> violations =
        alias_violations( target.convert, defined, migrated );
    INFO( "M-1 violations:\n" << join( violations ) );
    CHECK( violations.empty() );

    size_t checked = 0;
    for( const auto &row : target.convert ) {
        CAPTURE( row.first, row.second );
        // the item object is gone
        CHECK_FALSE( itype_id( row.first ).is_valid() );
        // the spawn path hands out the DDA target
        detached_ptr<item> spawned =
            item_controller->spawn_migrated( itype_id( row.first ), calendar::turn, 1 );
        REQUIRE( spawned );
        CHECK( spawned->typeId() == itype_id( row.second ) );
        // the recipe that produced the alias went with it (DDA deleted the same rows)
        CHECK_FALSE( recipe_id( row.first ).is_valid() );
        checked++;
    }
    std::cout << "mig1 aliases migrate: checked " << checked << std::endl;
    CHECK( checked == 5 );
}

// M-2: no converted id survives outside its MIGRATION object, in the base data or in any mod.
// Mutation killed: N2 (a stray reference, e.g. a recipe component back to `lard`).
TEST_CASE( "mig1 no stray references", "[mig1][migration]" ) {
    const mig1_target target = read_target();
    std::set<std::string> ids;
    for( const auto &row : target.convert ) {
        ids.insert( row.first );
    }
    int files_scanned = 0;
    const ref_counts refs = scan_references( ids, files_scanned );
    const std::vector<std::string> violations = stray_violations( ids, refs );
    INFO( "M-2 violations:\n" << join( violations ) );
    CHECK( violations.empty() );
    std::cout << "mig1 no stray references: checked " << ids.size() << " ids in " << files_scanned
              << " files" << std::endl;
    CHECK( ids.size() == 5 );
    // a scanner that read nothing would pass vacuously
    CHECK( files_scanned > 2000 );
}

// M-3: every kept candidate (B variants with own stats, D own items, T missing DDA target) and the
// 4 ask_owner items are still defined itypes and are not a MIGRATION source.
// Mutation killed: N3 (one of them migrated away or its item object deleted).
TEST_CASE( "mig1 kept items stay", "[mig1][migration]" ) {
    const mig1_target target = read_target();
    std::vector<std::string> ids;
    for( const auto &row : target.keep ) {
        ids.push_back( row.first );
    }
    for( const auto &row : target.ask_owner ) {
        ids.push_back( row.first );
    }
    std::set<std::string> defined;
    id_map live;
    for( const std::string &id : ids ) {
        if( itype_id( id ).is_valid() ) {
            defined.insert( id );
        }
        // A MIGRATION over a live item would silently replace it in every item group and
        // requirement (src/item_factory.cpp:1529-1600) - the hazard that made DDA's verbatim
        // migration file impossible for CDF (PL-51 part 2: 282 DDA sources are live CDF items).
        live[id] = item_controller->migrate_id( itype_id( id ) ).str();
    }
    const std::vector<std::string> violations = keep_violations( ids, defined, live );
    INFO( "M-3 violations:\n" << join( violations ) );
    CHECK( violations.empty() );
    std::cout << "mig1 kept items stay: checked " << ids.size() << " keep "
              << target.keep.size() << " ask_owner " << target.ask_owner.size()
              << " migrated " << violations.size() << std::endl;
    // CDF: 325 keep ids (B 77, D 142, T 106) + 4 ask_owner, re-derived on the MIG1 base
    CHECK( target.keep.size() == 325 );
    CHECK( target.ask_owner.size() == 4 );
    CHECK( ids.size() == 329 );
}

// M-4: the negative fixtures name real targets and every mutation is caught by the M-1..M-3
// checkers. Mutation killed: dropping a target row from mig1-target.json.
TEST_CASE( "mig1 negative fixtures complete", "[mig1][migration]" ) {
    const mig1_target target = read_target();
    const std::vector<mig1_mutation> mutations = read_mutations( MIG1_NEG );
    std::set<std::string> cases;
    size_t checked = 0;
    for( const mig1_mutation &mutation : mutations ) {
        CAPTURE( mutation.test_case, mutation.kind, mutation.id );
        cases.insert( mutation.test_case );
        // the fixture names an id of the target list and a real target
        const auto converted = target.convert.find( mutation.id );
        const bool in_target = converted != target.convert.end() ||
                               target.keep.count( mutation.id ) > 0 ||
                               target.ask_owner.count( mutation.id ) > 0;
        CHECK( in_target );
        if( converted != target.convert.end() ) {
            CHECK( mutation.replace == converted->second );
        }
        if( !mutation.replace.empty() ) {
            CHECK( itype_id( mutation.replace ).is_valid() );
        }

        std::vector<std::string> violations;
        if( mutation.kind == "alias_item_defined" ) {
            // the alias item is still there (N1)
            std::set<std::string> defined = { mutation.probe, mutation.replace };
            id_map migrated = { { mutation.id, mutation.replace } };
            violations = alias_violations( { { mutation.id, mutation.replace } }, defined,
                                           migrated );
        } else if( mutation.kind == "replace_missing" ) {
            // the MIGRATION points at an item that does not exist (N4)
            std::set<std::string> defined;
            id_map migrated = { { mutation.id, mutation.probe } };
            violations = alias_violations( { { mutation.id, mutation.replace } }, defined,
                                           migrated );
        } else if( mutation.kind == "stray_reference" ) {
            // a data file still names the converted id (N2)
            const ref_counts refs = { { "data/json/recipes/food/other.json",
                                         { { mutation.id, 1 } } } };
            violations = stray_violations( { mutation.id }, refs );
        } else if( mutation.kind == "keep_item_missing" ) {
            // a kept item was migrated away (N3)
            violations = keep_violations( std::vector<std::string>{ mutation.probe },
                                          std::set<std::string>(), id_map() );
        } else if( mutation.kind == "keep_item_migrated" ) {
            // a MIGRATION object was added over a live kept item (N3)
            const id_map migrated = { { mutation.probe, mutation.replace } };
            violations = keep_violations( std::vector<std::string>{ mutation.probe },
                                          std::set<std::string>{ mutation.probe }, migrated );
        } else {
            FAIL( "unknown mutation kind: " << mutation.kind );
        }
        INFO( "mutation " << mutation.kind << " violations:\n" << join( violations ) );
        CHECK( !violations.empty() );
        CHECK( names_id( violations, mutation.id ) );
        checked++;
    }
    std::cout << "mig1 negative fixtures complete: checked " << checked << std::endl;
    CHECK( checked == 5 );
    CHECK( cases == std::set<std::string>( { "M-1", "M-2", "M-3" } ) );
}

// M-5: the recipes that named a converted id still load, still craft, name the target and every
// component id still exists; each target keeps a source. Mutation killed: N2 (a broken reference,
// e.g. a component or a requirement id that no longer exists).
TEST_CASE( "mig1 groups and recipes still spawn", "[mig1][migration]" ) {
    clear_all_state();
    const mig1_target target = read_target();
    size_t checked = 0;
    for( const mig1_recipe &row : target.recipes ) {
        CAPTURE( row.id, row.result );
        REQUIRE( recipe_id( row.id ).is_valid() );
        const recipe &r = recipe_id( row.id ).obj();
        CHECK( r.result() == itype_id( row.result ) );
        CHECK( itype_id( row.result ).is_valid() );
        for( const std::vector<item_comp> &alternatives :
             r.simple_requirements().get_components() ) {
            for( const item_comp &comp : alternatives ) {
                if( comp.requirement ) {
                    // a nested requirement list (LIST), expanded by the loader
                    continue;
                }
                CAPTURE( comp.type.str() );
                CHECK( comp.type.is_valid() );
            }
        }
        for( const std::string &used : row.uses ) {
            CAPTURE( used );
            CHECK( recipe_lists( r, used ) );
        }
        const std::string captured = capture_debugmsg_during( [&]() {
            detached_ptr<item> spawned = item::spawn( itype_id( row.result ), calendar::turn, 1 );
            ( void )spawned;
        } );
        CHECK( captured.empty() );
        checked++;
    }
    for( const mig1_source &source : target.sources ) {
        CAPTURE( source.item, source.recipe, source.group );
        CHECK( itype_id( source.item ).is_valid() );
        if( !source.recipe.empty() ) {
            REQUIRE( recipe_id( source.recipe ).is_valid() );
            CHECK( recipe_id( source.recipe ).obj().result() == itype_id( source.item ) );
        }
        if( !source.group.empty() ) {
            CHECK( item_group::group_is_defined( item_group_id( source.group ) ) );
            CHECK( item_group::group_contains_item( item_group_id( source.group ),
                    itype_id( source.item ) ) );
        }
        checked++;
    }
    std::cout << "mig1 groups and recipes still spawn: checked " << checked << " recipes "
              << target.recipes.size() << " sources " << target.sources.size() << std::endl;
    CHECK( target.recipes.size() == 10 );
    CHECK( target.sources.size() == 5 );
    CHECK( checked == 15 );
}

// M-6: a MIGRATION id is defined once in data/json/obsoletion/*.json. The loader keeps the object
// loaded last (src/item_factory.cpp:4238-4246) with no error, so a duplicate would silently
// override; this pins the explicit order (no duplicate exists, so sorted path order cannot matter
// for the 5 converted ids, which all live in obsoletion/migration.json).
// Mutation killed: a second MIGRATION object for `rifle_40` in another obsoletion file.
TEST_CASE( "mig1 migration ids unique", "[mig1][migration]" ) {
    const mig1_target target = read_target();
    const std::map<std::string, std::vector<std::string>> owners = obsoletion_owners( true );
    std::vector<std::string> duplicates;
    for( const auto &entry : owners ) {
        if( entry.second.size() > 1 ) {
            duplicates.push_back( entry.first );
        }
    }
    // CDF: no duplicate MIGRATION id exists in the obsoletion files on this base; an entry added
    // here must name why the second object is legitimate.
    INFO( "duplicate ids:\n" << join( duplicates ) );
    CHECK( duplicates.empty() );

    size_t checked = 0;
    for( const auto &row : target.convert ) {
        CAPTURE( row.first );
        const auto found = owners.find( row.first );
        REQUIRE( found != owners.end() );
        CHECK( found->second.size() == 1 );
        if( found->second.size() == 1 ) {
            CHECK( found->second.front() == MIGRATION_FILE );
        }
        checked++;
    }
    std::cout << "mig1 migration ids unique: checked " << checked << " ids of "
              << owners.size() << " obsoletion ids" << std::endl;
    CHECK( checked == 5 );
}

// M-7 (PL-45-b): for the 16 MIGRATION rows of the coverage table the CDF MIGRATION object still
// exists with the table's target; for the 12 item-only rows the obsoletion item object still
// exists. MIG1 deletes and retargets nothing else. Mutations killed: NM-1, NM-2.
TEST_CASE( "obsoletion_rows_unchanged", "[migration][mig1]" ) {
    const std::vector<mig1_coverage_row> rows = read_coverage();
    const auto is_defined = []( const itype_id & id ) {
        return id.is_valid();
    };
    const auto migrated = []( const std::string & id ) {
        return item_controller->migrate_id( itype_id( id ) ).str();
    };
    const std::vector<std::string> violations = coverage_violations( rows, is_defined, migrated );
    INFO( "M-7 violations:\n" << join( violations ) );
    CHECK( violations.empty() );
    std::cout << "obsoletion_rows_unchanged: checked " << rows.size() << std::endl;
    CHECK( rows.size() == 28 );

    // NM-1: a deleted MIGRATION object leaves the old id unmigrated
    for( const mig1_mutation &mutation : read_mutations( MIG1_NEG_COVERAGE ) ) {
        if( mutation.kind != "drop_migration" && mutation.kind != "retarget_migration" ) {
            continue;
        }
        CAPTURE( mutation.kind, mutation.id );
        const auto rewritten = [&]( const std::string & id ) {
            return id == mutation.id ? mutation.probe : migrated( id );
        };
        const std::vector<std::string> caught = coverage_violations( rows, is_defined, rewritten );
        INFO( "mutation violations:\n" << join( caught ) );
        CHECK( !caught.empty() );
        CHECK( names_id( caught, mutation.id ) );
    }
}

// M-8 (PL-45-b): the table lists the 28 ids exactly once, each with a disposition, and every id is
// a CDF obsoletion id. Mutations killed: NM-3 (a removed row), NM-4 (a foreign id).
TEST_CASE( "coverage_table_complete", "[migration][mig1]" ) {
    const std::vector<mig1_coverage_row> rows = read_coverage();
    std::set<std::string> obsoletion;
    for( const auto &entry : obsoletion_owners( false ) ) {
        obsoletion.insert( entry.first );
    }
    const std::vector<std::string> violations =
        table_violations( rows, coverage_ids(), obsoletion );
    INFO( "M-8 violations:\n" << join( violations ) );
    CHECK( violations.empty() );
    std::cout << "coverage_table_complete: checked " << rows.size() << std::endl;
    CHECK( rows.size() == 28 );

    // NM-3: removed row; NM-4: foreign id
    for( const mig1_mutation &mutation : read_mutations( MIG1_NEG_COVERAGE ) ) {
        if( mutation.kind == "drop_row" ) {
            std::vector<mig1_coverage_row> dropped;
            for( const mig1_coverage_row &row : rows ) {
                if( row.id != mutation.id ) {
                    dropped.push_back( row );
                }
            }
            CAPTURE( mutation.kind, mutation.id );
            const std::vector<std::string> caught =
                table_violations( dropped, coverage_ids(), obsoletion );
            INFO( "mutation violations:\n" << join( caught ) );
            CHECK( !caught.empty() );
        } else if( mutation.kind == "add_foreign_row" ) {
            std::vector<mig1_coverage_row> extended = rows;
            extended.push_back( mig1_coverage_row{ mutation.probe, "item", "-", "-",
                                                   "negative fixture" } );
            CAPTURE( mutation.kind, mutation.id );
            const std::vector<std::string> caught = table_violations( extended, coverage_ids(),
                                                  obsoletion );
            INFO( "mutation violations:\n" << join( caught ) );
            CHECK( !caught.empty() );
        }
    }
}
