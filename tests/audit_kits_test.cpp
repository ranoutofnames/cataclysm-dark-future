// PL-62 AT6: kit, itemgroup and migration spawn audit.
//
// Spawns what the game spawns (every profession's starting kit for both genders and a few
// starting-trait variants, every item group three times, every MIGRATION id) on a test
// character and proves each item went somewhere sensible: into a pocket that accepts it, not
// onto the floor, no debugmsg, and the contents survive a JSON round trip.
//
// This is the dynamic counterpart of the static checks A4/A6/A7 in tools/cdf_audit.py.
// The kit idiom follows CDF's port of DDA tests/new_character_test.cpp (b4b62254ae2:115-207):
// build the avatar, set the profession and gender, call the same add_profession_items the
// character creation screen calls, then inspect the worn tree.
//
// One generated row per finding, written to report/sweep-kits.tsv (PLAN.md section 3). A
// finding listed in tests/data/audit/allow.tsv (check id, item id, reason, date) does not
// fail the case (PLAN.md section 5). CDF_SWEEP_SHARD=i/n limits the professions, groups and
// migrations to index % n == i-1, so several processes can run the sweep without Catch
// growing by one case per item (PLAN.md section 4 rule 5).

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "newcharacter.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "profession.h"
#include "ret_val.h"
#include "rng.h"
#include "scenario.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "type_id.h"
#include "units.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

// ------------------------------------------------------------------------------------------------
// Rows, TSV and the allowlist (PLAN.md sections 3 and 5). Kept in this file, not in
// tests/audit_support.*: AT5a owns that file on a parallel branch (AT6 brief).
// ------------------------------------------------------------------------------------------------

struct row {
    std::string check;
    std::string id;
    std::string severity;
    std::string detail;
};

std::string tsv_safe( const std::string &value )
{
    std::string out = value;
    std::replace( out.begin(), out.end(), '\t', ' ' );
    std::replace( out.begin(), out.end(), '\n', ' ' );
    std::replace( out.begin(), out.end(), '\r', ' ' );
    return out;
}

std::string compact( std::string value, std::size_t limit = 240 )
{
    std::string out;
    out.reserve( value.size() );
    bool last_space = false;
    for( const char c : value ) {
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if( space ) {
            if( !last_space ) {
                out.push_back( ' ' );
            }
        } else {
            out.push_back( c );
        }
        last_space = space;
    }
    if( out.size() > limit ) {
        out.resize( limit );
        out += "...";
    }
    return out;
}

class row_sink
{
    public:
        void add( const std::string &check, const std::string &id, const std::string &severity,
                  const std::string &detail ) {
            const std::string key = check + '\t' + id + '\t' + detail;
            if( !seen_.insert( key ).second ) {
                return;
            }
            rows.push_back( { check, id, severity, compact( detail ) } );
        }

        bool has( const std::string &check ) const {
            return std::ranges::any_of( rows, [&check]( const row & r ) {
                return r.check == check;
            } );
        }

        std::vector<row> rows;

    private:
        std::set<std::string> seen_;
};

std::vector<std::string> split_tabs( const std::string &line )
{
    std::vector<std::string> cells;
    std::size_t start = 0;
    while( start <= line.size() ) {
        const std::size_t end = line.find( '\t', start );
        if( end == std::string::npos ) {
            cells.push_back( line.substr( start ) );
            break;
        }
        cells.push_back( line.substr( start, end - start ) );
        start = end + 1;
    }
    return cells;
}

struct allow_entry {
    std::string check;
    std::string id;
};

// tests/data/audit/allow.tsv: check id, item id, reason, date. A missing file is an empty
// allowlist (AT0 is not on this base yet; the brief says "if present").
std::vector<allow_entry> read_allowlist( const std::string &path )
{
    std::vector<allow_entry> out;
    std::ifstream in( path );
    std::string line;
    while( std::getline( in, line ) ) {
        if( line.empty() || line[0] == '#' ) {
            continue;
        }
        const std::vector<std::string> cells = split_tabs( line );
        if( cells.size() >= 2 ) {
            out.push_back( { cells[0], cells[1] } );
        }
    }
    return out;
}

bool allowed( const row &r, const std::vector<allow_entry> &allowlist )
{
    return std::ranges::any_of( allowlist, [&r]( const allow_entry & a ) {
        return a.check == r.check && a.id == r.id;
    } );
}

void write_rows( const std::string &path, const std::vector<row> &rows )
{
    const std::filesystem::path file( path );
    if( file.has_parent_path() ) {
        std::filesystem::create_directories( file.parent_path() );
    }
    std::ofstream out( path, std::ios::trunc );
    out << "check\tid\tseverity\tdetail\n";
    for( const row &r : rows ) {
        out << tsv_safe( r.check ) << '\t' << tsv_safe( r.id ) << '\t'
            << tsv_safe( r.severity ) << '\t' << tsv_safe( r.detail ) << '\n';
    }
}

// ------------------------------------------------------------------------------------------------
// Sharding: CDF_SWEEP_SHARD=i/n (1-based), the same variable AT5 uses.
// ------------------------------------------------------------------------------------------------

struct shard_config {
    bool active = false;
    std::size_t index = 0;
    std::size_t count = 1;
};

shard_config env_shard()
{
    shard_config out;
    const char *const value = std::getenv( "CDF_SWEEP_SHARD" );
    if( value == nullptr || *value == '\0' ) {
        return out;
    }
    const std::string text( value );
    const std::size_t slash = text.find( '/' );
    if( slash == std::string::npos ) {
        return out;
    }
    const int index = std::atoi( text.substr( 0, slash ).c_str() );
    const int count = std::atoi( text.substr( slash + 1 ).c_str() );
    if( index < 1 || count < 1 || index > count ) {
        return out;
    }
    out.active = true;
    out.index = static_cast<std::size_t>( index );
    out.count = static_cast<std::size_t>( count );
    return out;
}

bool in_shard( std::size_t i, const shard_config &shard )
{
    return !shard.active || i % shard.count == shard.index - 1;
}

// ------------------------------------------------------------------------------------------------
// Item helpers.
// ------------------------------------------------------------------------------------------------

int total_count( const item &it )
{
    int n = it.count();
    for( const item *child : it.contents.all_items_top() ) {
        n += total_count( *child );
    }
    return n;
}

// Ids, counts and nesting, order-independent: the state a save/load cycle must preserve.
std::string item_signature( const item &it )
{
    std::vector<std::string> children;
    for( const item *child : it.contents.all_items_top() ) {
        children.push_back( item_signature( *child ) );
    }
    std::ranges::sort( children );
    std::string out = it.typeId().str() + "x" + std::to_string( it.count() );
    if( !children.empty() ) {
        out += "(";
        for( std::size_t i = 0; i < children.size(); ++i ) {
            out += ( i > 0 ? "," : "" ) + children[i];
        }
        out += ")";
    }
    return out;
}

std::string save_item( const item &it )
{
    std::ostringstream buffer;
    JsonOut jsout( buffer );
    it.serialize( jsout );
    return buffer.str();
}

detached_ptr<item> load_item( const std::string &json )
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    return item::spawn( jsin );
}

// K1/G2: every top-level item of @p parent sits in a pocket of that item which accepts it,
// recursively. can_contain( ..., ignore_contents = true ) re-checks the item's own size,
// weight and restrictions without counting it twice against the pocket it already occupies.
// A MIGRATION pocket is "inventory without a pocket": the item was routed past every real one.
bool contents_fit( const item &parent, std::string &reason )
{
    for( const item *child : parent.contents.all_items_top() ) {
        const item_pocket *pocket = parent.contents.pocket_of( child );
        if( pocket == nullptr ) {
            reason = string_format( "%s has no pocket", child->typeId().str() );
            return false;
        }
        if( pocket->get_type() == pocket_type::MIGRATION ) {
            reason = string_format( "%s is in the MIGRATION pocket", child->typeId().str() );
            return false;
        }
        const ret_val<item_pocket::contain_code> fit = pocket->can_contain( *child, true );
        if( !fit.success() ) {
            reason = string_format( "%s does not fit its pocket: %s", child->typeId().str(),
                                    fit.str() );
            return false;
        }
        if( !contents_fit( *child, reason ) ) {
            return false;
        }
    }
    return true;
}

// K3: a non-stack item with count but no volume. Charge stacks (ammo) and liquids are
// excluded: a zero per-charge volume is how DDA data is written.
void check_zero_volume( row_sink &sink, const item &it )
{
    if( !it.is_null() && it.count() > 0 && !it.count_by_charges() && it.volume() == 0_ml ) {
        sink.add( "K3", it.typeId().str(), "error",
                  string_format( "zero volume with count %d", it.count() ) );
    }
    for( const item *child : it.contents.all_items_top() ) {
        check_zero_volume( sink, *child );
    }
}

// K1, K3 and K5 over the worn tree. Shared with the seeded-defect proof so the proof exercises
// the same code the sweep does.
void check_worn_items( row_sink &sink, const avatar &ch )
{
    for( const item *worn : ch.worn ) {
        if( worn == nullptr ) {
            continue;
        }
        std::string reason;
        if( !contents_fit( *worn, reason ) ) {
            sink.add( "K1", worn->typeId().str(), "error", reason );
        }
        check_zero_volume( sink, *worn );

        const std::string json = save_item( *worn );
        detached_ptr<item> loaded;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            loaded = load_item( json );
        } );
        if( !loaded ) {
            sink.add( "K5", worn->typeId().str(), "error", "did not load after a JSON round trip" );
            continue;
        }
        const std::string before = item_signature( *worn );
        const std::string after = item_signature( *loaded );
        if( before != after ) {
            sink.add( "K5", worn->typeId().str(), "error",
                      "contents changed in a JSON round trip: " + before + " -> " + after );
        }
        if( !dmsg.empty() ) {
            sink.add( "K5", worn->typeId().str(), "error", dmsg );
        }
    }
}

// ------------------------------------------------------------------------------------------------
// K1-K5: one profession kit. @p defect runs after the kit is applied (PLAN.md section 6).
// ------------------------------------------------------------------------------------------------

using defect_fn = std::function<void( avatar & )>;

void check_kit( row_sink &sink, const profession &prof, bool male,
                const std::vector<trait_id> &traits, unsigned int seed, int variant,
                const defect_fn &defect = defect_fn() )
{
    avatar &ch = get_avatar();
    // Character overload: debug_storage = false (a real inventory) and skip_nutrition = true
    // (the kit audit does not eat; DDA's starting_items test does not either once mutations
    // are left out).
    clear_character( static_cast<Character &>( ch ), true );
    ch.worn.clear();
    ch.set_body();
    ch.recalc_hp();
    ch.reset_encumbrance();
    ch.male = male;
    ch.prof = prof.ident();
    g->scen = scenario::generic();

    // One seed for the whole kit: the same profession on a shard always spawns the same items.
    rng_set_engine_seed( seed );

    std::vector<detached_ptr<item>> spawned;
    std::vector<detached_ptr<item>> overflow;
    int before = 0;
    const std::string dmsg = capture_debugmsg_during( [&]() {
        spawned = prof.items( male, traits );
        for( const detached_ptr<item> &it : spawned ) {
            if( it ) {
                before += total_count( *it );
            }
        }
        // CDF: DDA's player_character.add_profession_items() stores the kit itself; CDF's free
        // function takes the avatar and returns the items it could not store in `overflow`.
        newcharacter::add_profession_items( ch, std::move( spawned ), overflow );
        if( defect ) {
            defect( ch );
        }
    } );

    const std::string id = string_format( "%s/%s/t%d", prof.ident().str(),
                                          male ? "male" : "female", variant );

    // K4: no debugmsg while spawning and storing the kit.
    if( !dmsg.empty() ) {
        sink.add( "K4", id, "error", dmsg );
    }

    // K2: nothing on the floor at the starting position, and no item lost between the spawn
    // and the character + overflow (DDA new_character_test.cpp:174-184 counts the same way).
    map &here = get_map();
    if( !here.i_at( ch.bub_pos() ).empty() ) {
        sink.add( "K2", id, "error", "starting item on the map at the start position" );
        clear_map();
    }
    int after = 0;
    for( const item *it : ch.top_items_loc() ) {
        after += total_count( *it );
    }
    if( !ch.primary_weapon().is_null() ) {
        after += total_count( ch.primary_weapon() );
    }
    for( const detached_ptr<item> &it : overflow ) {
        if( it ) {
            after += total_count( *it );
        }
    }
    if( before != after ) {
        sink.add( "K2", id, "error",
                  string_format( "kit count changed: spawned %d, stored+overflow %d", before,
                                 after ) );
    }

    // K3: an item the character could not store is an "inventory without a pocket".
    for( const detached_ptr<item> &it : overflow ) {
        if( it ) {
            sink.add( "K3", it->typeId().str(), "error",
                      "overflow: no pocket on the character accepts it" );
        }
    }

    check_worn_items( sink, ch );
}

// ------------------------------------------------------------------------------------------------
// G1-G3: one item group, three seeds.
// ------------------------------------------------------------------------------------------------

// TEST_DATA probes for the max_item_length rule: both groups deliberately put the 40 cm
// test_long_rod into a pouch whose pocket allows 20 cm, so item_group logs the very refusal the
// fixture exists to exercise.  They are not data defects.
bool expected_group_debugmsg( const std::string &group_id )
{
    return group_id == "test_long_rod_in_pouch" || group_id == "test_pouch_with_long_rod";
}

void check_group( row_sink &sink, const item_group_id &group_id )
{
    const std::string id = group_id.str();
    const Item_spawn_data *const data = item_controller->get_group( group_id );
    const bool has_entries = data != nullptr && !data->every_item().empty();
    int produced = 0;

    for( const int seed : { 42, 7, 123 } ) {
        rng_set_engine_seed( static_cast<unsigned int>( seed ) );
        std::vector<detached_ptr<item>> items;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            items = item_group::items_from( group_id, calendar::turn_zero );
        } );
        if( !dmsg.empty() && !expected_group_debugmsg( id ) ) {
            sink.add( "G4", id, "error", dmsg );
        }
        produced += static_cast<int>( items.size() );
        for( const detached_ptr<item> &it : items ) {
            if( !it || it->is_null() || !it->typeId().is_valid() ) {
                sink.add( "G1", id, "error",
                          string_format( "seed %d spawned an unknown item type", seed ) );
                continue;
            }
            std::string reason;
            if( !contents_fit( *it, reason ) ) {
                sink.add( "G2", it->typeId().str(), "error",
                          string_format( "seed %d: %s", seed, reason ) );
            }
        }
    }

    // G3 is informational: a group whose entries exist but which produced nothing under all
    // three seeds may legitimately be a probability-0 group; the planner asked for info.
    if( produced == 0 && has_entries ) {
        sink.add( "G3", id, "info", "no item from seeds 42, 7 and 123" );
    }
}

// ------------------------------------------------------------------------------------------------
// M1-M3: migration ids.
// ------------------------------------------------------------------------------------------------

// @p resolve is a parameter so the seeded-defect proof can point a migration at a missing id
// without touching loaded data.
void check_migration_target( row_sink &sink, const itype_id &source,
                             const std::function<itype_id( const itype_id & )> &resolve,
                             bool spawn )
{
    const itype_id target = resolve( source );
    if( !target.is_valid() ) {
        sink.add( "M1", source.str(), "error",
                  string_format( "replacement %s does not exist", target.str() ) );
        return;
    }
    if( !spawn ) {
        return;
    }
    detached_ptr<item> spawned;
    const std::string dmsg = capture_debugmsg_during( [&]() {
        spawned = item_controller->spawn_migrated( source, calendar::turn_zero );
    } );
    if( !spawned || spawned->is_null() ) {
        sink.add( "M3", source.str(), "error", "spawn of the replacement failed" );
    } else if( spawned->typeId() != target ) {
        sink.add( "M3", source.str(), "error",
                  string_format( "spawned %s, expected %s", spawned->typeId().str(),
                                 target.str() ) );
    }
    if( !dmsg.empty() ) {
        sink.add( "M3", source.str(), "error", dmsg );
    }
}

// M2: the chain a save or a spawn follows is bounded (A6 uses three as the limit) and has no
// cycle. find_migration is a public accessor, so this needs no source edit.
void check_migration_chain( row_sink &sink, const itype_id &source )
{
    std::set<itype_id> seen;
    itype_id current = source;
    std::size_t steps = 0;
    while( const migration *const mig = item_controller->find_migration( current ) ) {
        if( !seen.insert( current ).second ) {
            sink.add( "M2", source.str(), "error",
                      string_format( "migration cycle at %s", current.str() ) );
            return;
        }
        current = mig->replace;
        if( ++steps > 3 ) {
            sink.add( "M2", source.str(), "error", "migration chain longer than three" );
            return;
        }
    }
}

// Every MIGRATION id in the data the test binary loads: core JSON plus the TEST_DATA mod
// (tests/test_main.cpp always loads it). Copied from tests/migration_dda_test.cpp:78-113.
std::vector<itype_id> migration_ids()
{
    std::set<itype_id> ids;
    for( const std::string &root : { "data/json", "data/mods/TEST_DATA" } ) {
        if( !std::filesystem::exists( root ) ) {
            continue;
        }
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
            if( text.find( "\"MIGRATION\"" ) == std::string::npos ) {
                continue;
            }
            std::istringstream in( text );
            JsonIn jsin( in );
            if( !jsin.test_array() ) {
                continue;
            }
            for( JsonObject jo : jsin.get_array() ) {
                jo.allow_omitted_members();
                if( jo.get_string( "type", "" ) != "MIGRATION" ) {
                    continue;
                }
                if( jo.has_array( "id" ) ) {
                    for( const std::string &id : jo.get_string_array( "id" ) ) {
                        ids.insert( itype_id( id ) );
                    }
                } else {
                    ids.insert( itype_id( jo.get_string( "id" ) ) );
                }
            }
        }
    }
    return std::vector<itype_id>( ids.begin(), ids.end() );
}

// The trait variants ("hobbies"): CDF has no hobby system, so the starting-trait vectors the
// DDA new_character_test passes to profession::items stand in for it. They are passed to the
// item substitutions only; setting mutations on the character is the 70%-of-runtime part of
// that test (see its comment) and changes nothing the K checks look at.
const std::vector<std::vector<trait_id>> &kit_trait_sets()
{
    static const std::vector<std::vector<trait_id>> sets = {
        {},
        { trait_id( "ANTIWHEAT" ) },
        { trait_id( "ANTIFRUIT" ), trait_id( "ANTIJUNK" ) },
        { trait_id( "LACTOSE" ), trait_id( "VEGETARIAN" ) },
        { trait_id( "ANTIWHEAT" ), trait_id( "ANTIJUNK" ), trait_id( "MEATARIAN" ) },
    };
    return sets;
}

} // namespace

TEST_CASE( "kit and group spawn", "[audit][kits]" )
{
    clear_all_state();
    clear_map();

    row_sink sink;
    const shard_config shard = env_shard();

    // K1-K5: every profession, both genders, every trait variant.
    const std::vector<profession> &professions = profession::get_all();
    REQUIRE( !professions.empty() );
    const std::vector<std::vector<trait_id>> &trait_sets = kit_trait_sets();
    for( std::size_t i = 0; i < professions.size(); ++i ) {
        if( !in_shard( i, shard ) ) {
            continue;
        }
        const profession &prof = professions[i];
        for( const bool male : { true, false } ) {
            for( std::size_t t = 0; t < trait_sets.size(); ++t ) {
                // PLAN.md section 4 rule 3: fixed seeds 42, 7, 123, 999.
                const unsigned int seed = male ? 42U : 999U;
                check_kit( sink, prof, male, trait_sets[t], seed, static_cast<int>( t ) );
            }
        }
    }

    // G1-G3: every item group, three seeds.
    const std::vector<item_group_id> groups = item_controller->get_all_group_names();
    REQUIRE( !groups.empty() );
    for( std::size_t i = 0; i < groups.size(); ++i ) {
        if( in_shard( i, shard ) ) {
            check_group( sink, groups[i] );
        }
    }

    // M1-M3: every MIGRATION id.
    const std::vector<itype_id> migrations = migration_ids();
    REQUIRE( !migrations.empty() );
    for( std::size_t i = 0; i < migrations.size(); ++i ) {
        if( !in_shard( i, shard ) ) {
            continue;
        }
        check_migration_target( sink, migrations[i],
                                []( const itype_id & id ) {
            return item_controller->migrate_id( id );
        }, true );
        check_migration_chain( sink, migrations[i] );
    }

    write_rows( "report/sweep-kits.tsv", sink.rows );

    const std::vector<allow_entry> allowlist = read_allowlist( "tests/data/audit/allow.tsv" );
    std::string new_findings;
    int new_count = 0;
    for( const row &r : sink.rows ) {
        if( r.severity == "error" && !allowed( r, allowlist ) ) {
            ++new_count;
            if( new_count <= 20 ) {
                new_findings += string_format( "%s\t%s\t%s\n", r.check, r.id, r.detail );
            }
        }
    }
    INFO( "report/sweep-kits.tsv; new findings:\n" + new_findings );
    CAPTURE( sink.rows.size() );
    CAPTURE( new_count );
    CHECK( new_count == 0 );
}

TEST_CASE( "kit and group spawn seeded defects", "[audit][kits][defect]" )
{
    clear_all_state();

    SECTION( "K1 reports an item forced into a worn pocket it does not fit" ) {
        avatar &ch = get_avatar();
        clear_character( static_cast<Character &>( ch ), true );
        ch.worn.clear();
        ch.set_body();
        ch.recalc_hp();

        // CDF: the backpack's worn storage is its item_overlay layout (synthesize_pockets,
        // src/item_factory.cpp:1252-1259), a 25 L main pocket that takes the 20 L television, so
        // the seed wears a bag whose pocket it cannot fit: the purse's single pocket is 5 L.
        detached_ptr<item> bag = item::spawn( "purse" );
        item *const bag_ptr = &*bag;
        REQUIRE( ch.wear_item( std::move( bag ), false ) == nullptr );
        REQUIRE( !bag_ptr->contents.get_pockets( pocket_type::CONTAINER ).empty() );
        // 20 L television into the purse's 5 L pocket: force_insert_item skips the checks,
        // exactly the kind of defect a migration or a spawn modifier can create.
        bag_ptr->force_insert_item( item::spawn( "television" ), pocket_type::CONTAINER );

        // The rule itself, proved on the bag: K1 must refuse the television.
        std::string reason;
        CHECK_FALSE( contents_fit( *bag_ptr, reason ) );
        CHECK_FALSE( reason.empty() );
        // and the worn walk reports it as a second check.
        row_sink sink;
        check_worn_items( sink, ch );
        CHECK( sink.has( "K1" ) );
    }

    SECTION( "M1 reports a migration pointed at a missing id" ) {
        row_sink sink;
        check_migration_target( sink, itype_id( "kit_audit_seeded_source" ),
                                []( const itype_id & ) {
            return itype_id( "kit_audit_missing_target" );
        }, false );
        CHECK( sink.has( "M1" ) );
    }
}
