// CDF: PC-E1 step E1-4 (e-file data foundations, additive). Tests for the DDA b4b62254ae2 data
// that E1-4 ports into the core data: data/json/items/software.json:3-271 (abstract_software,
// abstract_efile*, efile_*, train_data, software_encryption_codes, software_archived_data),
// data/json/itemgroups/efiles.json (11 groups), data/json/snippets/e_logs.json (local_files_simple)
// and the Lua replacement of the EOC use action EOC_CHECK_MAP_CACHE
// (data/json/effects_on_condition/computer_eocs.json:19-35). Written before the data.
//
// The use action of efile_lore (DDA EOC_READ_LOCAL_FILES, computer_eocs.json:3-18) is the Lua
// action EFILE_LORE (E1-4b); it needs the snippet bindings gapi.random_snippet_id,
// gapi.get_snippet, Avatar:add_snippet and Avatar:has_seen_snippet, tested here as well.
// E1-10b: the five software ids that BN data defined are the DDA definitions now, and the efiles_*
// groups take the CDF book groups without the survivor's note (survnote is a paper note and no file),
// so the group test has no exception left.
#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catalua_impl.h"
#include "catalua_sol.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "flag.h"
#include "item.h"
#include "item_category.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "overmap/overmapbuffer.h"
#include "state_helpers.h"
#include "text_snippets.h"
#include "type_id.h"
#include "units.h"

namespace
{

struct efile_expectation {
    const char *id;
    units::ememory size;
    const char *category;
    bool exclusive;
};

// DDA data/json/items/software.json:3-271 (b4b62254ae2). abstract_efile, abstract_efile_exclusive,
// abstract_efile_copiable, abstract_efile_exclusive_copiable, abstract_efile_read and
// abstract_efile_recipes are abstract: they are covered through the items that copy from them.
const std::vector<efile_expectation> &expected_efiles()
{
    static const std::vector<efile_expectation> expected = {
        { "abstract_software", 1_KB, "software", true },
        { "software_encryption_codes", 1_GB, "software", true },
        { "software_archived_data", 64_GB, "software", true },
        { "train_data", 64_GB, "software", true },
        { "efile_junk", 1_KB, "e_files", true },
        { "efile_lore", 64_MB, "e_files", true },
        { "efile_map", 32_MB, "e_files", true },
        { "efile_photos", 4_MB, "e_files", true },
        { "efile_recipes", 4_MB, "e_files", true },
        { "efile_recipes_science", 4_MB, "e_files", true },
        { "efile_recipes_medical", 4_MB, "e_files", true },
    };
    return expected;
}

struct efile_action_result {
    int value = 0;
    std::string messages; // every gapi.add_msg text of the run, joined by " | "
};

// Runs the Lua use action module data/json/lua/iuse/efile.lua with the params table that
// preload.lua hands to a registered iuse function (ItemUseParams: user, item, pos).  The unit-test
// binary stubs the message log out (tests/fake_messages.cpp), so gapi.add_msg is wrapped to record
// the texts.  Returns nullopt when the script failed.
auto run_efile_action( const char *function_name, item &file ) -> std::optional<efile_action_result>
{
    auto lua = make_lua_state();
    lua.globals()["file"] = &file;
    lua.globals()["function_name"] = function_name;
    const auto result = lua.safe_script( R"(
        package.path = package.path .. ";data/json/?.lua;data/lua/?.lua"
        package.loaded["lua/iuse/efile"] = nil
        local log = {}
        local real_gapi = gapi
        gapi = setmetatable({ add_msg = function(_, text) table.insert(log, text) end },
                            { __index = real_gapi })
        local efile = require("lua/iuse/efile")
        local u = real_gapi.get_avatar()
        local value = efile[function_name]({ user = u, item = file, pos = u:get_pos_ms() })
        gapi = real_gapi
        return value, table.concat(log, " | ")
    )", sol::script_pass_on_error );
    if( !result.valid() ) {
        const sol::error err = result;
        WARN( "efile.lua " << function_name << " failed: " << err.what() );
        return std::nullopt;
    }
    return efile_action_result{ result.get<int>( 0 ), result.get<std::string>( 1 ) };
}

// string_id::obj() resolves the itype (CDF has no item::find_type)
auto type_of( const char *id ) -> const itype *
{
    const itype_id type_id( id );
    return type_id.is_valid() ? &type_id.obj() : nullptr;
}

} // namespace

TEST_CASE( "efile_core_items_load_with_memory", "[efile][data]" )
{
    clear_all_state();
    for( const efile_expectation &expected : expected_efiles() ) {
        CAPTURE( expected.id );
        REQUIRE( itype_id( expected.id ).is_valid() );
        detached_ptr<item> file = item::spawn( itype_id( expected.id ) );
        // item::ememory_size() of a recipe catalog or photo gallery scales with its content (DDA
        // item.cpp:2779-2799, empty = 0), so the type's size is what has to be positive
        CHECK( file->type->ememory_size > 0_KB );
        CHECK( file->type->ememory_size == expected.size );
        CHECK( file->get_category().get_id() == item_category_id( expected.category ) );
        CHECK( file->is_estorable() );
        CHECK( file->is_estorable_exclusive() == expected.exclusive );
        CHECK_FALSE( file->is_browsed() );
        CHECK( file->has_flag( flag_id( "E_STORABLE_EXCLUSIVE" ) ) );
        CHECK_FALSE( file->has_flag( flag_id( "E_STORABLE" ) ) );
        // abstract_efile: nothing weighs anything, nothing can be dropped or taken out
        CHECK( file->has_flag( flag_id( "ZERO_WEIGHT" ) ) );
        CHECK( file->has_flag( flag_id( "NO_DROP" ) ) );
        CHECK( file->has_flag( flag_id( "IRREMOVABLE" ) ) );
    }
}

TEST_CASE( "efile_collection_files_carry_their_use_and_flags", "[efile][data]" )
{
    clear_all_state();
    SECTION( "photo gallery" ) {
        const itype *photos = type_of( "efile_photos" );
        REQUIRE( photos != nullptr );
        CHECK( photos->has_flag( flag_id( "E_COPIABLE" ) ) );
        CHECK( photos->has_flag( flag_id( "E_FILE_COLLECTION" ) ) );
        CHECK( photos->get_use( "VIEW_PHOTOS" ) != nullptr );
        CHECK( !photos->memory_card_data );
    }
    SECTION( "recipe catalogs" ) {
        for( const char *id : { "efile_recipes", "efile_recipes_science", "efile_recipes_medical" } ) {
            CAPTURE( id );
            const itype *catalog = type_of( id );
            REQUIRE( catalog != nullptr );
            CHECK( catalog->has_flag( flag_id( "E_COPIABLE" ) ) );
            CHECK( catalog->has_flag( flag_id( "E_FILE_COLLECTION" ) ) );
            CHECK( catalog->get_use( "VIEW_RECIPES" ) != nullptr );
            CHECK( static_cast<bool>( catalog->memory_card_data ) );
        }
    }
    SECTION( "files without a collection" ) {
        for( const char *id : { "efile_junk", "efile_lore", "efile_map", "train_data" } ) {
            CAPTURE( id );
            const itype *file = type_of( id );
            REQUIRE( file != nullptr );
            CHECK_FALSE( file->has_flag( flag_id( "E_COPIABLE" ) ) );
            CHECK_FALSE( file->has_flag( flag_id( "E_FILE_COLLECTION" ) ) );
        }
    }
}

// E1-3 defined efile_photos and efile_recipes in TEST_DATA because item.cpp hard-codes the two ids;
// E1-4 adds the real ones, so the fixture copies must be gone (a TEST_DATA copy would override the
// core one silently).
TEST_CASE( "efile_core_photos_and_recipes_are_not_test_fixtures", "[efile][data]" )
{
    std::ifstream fixtures( "data/mods/TEST_DATA/efile_item_fixtures.json" );
    REQUIRE( fixtures.is_open() );
    const std::string text( ( std::istreambuf_iterator<char>( fixtures ) ),
                            std::istreambuf_iterator<char>() );
    CHECK( text.find( "\"id\": \"efile_photos\"" ) == std::string::npos );
    CHECK( text.find( "\"id\": \"efile_recipes\"" ) == std::string::npos );
}

TEST_CASE( "efile_local_files_snippets_load", "[efile][data]" )
{
    clear_all_state();
    // DDA data/json/snippets/e_logs.json: one category with named, id'd entries
    const snippet_id picked = SNIPPET.random_id_from_category( "local_files_simple" );
    REQUIRE( !picked.is_null() );
    const std::optional<translation> text = SNIPPET.get_snippet_by_id( picked );
    REQUIRE( text.has_value() );
    CHECK( !text->translated().empty() );
}

TEST_CASE( "efiles_groups_spawn_only_estorable_items", "[efile][data]" )
{
    clear_all_state();
    const std::vector<std::string> groups = {
        "efiles_science", "efiles_software", "efiles_medical", "SUS_efiles_sewage", "efiles_scrap",
        "efiles_school", "efiles_contraband", "civilian_portable_efiles", "civilian_smartphone_efiles",
        "civilian_laptop_efiles", "civilian_einktablet_efiles", "SUS_efiles_office"
    };
    // E1-10b: the five software ids are the DDA definitions now (E_STORABLE_EXCLUSIVE), and the book
    // groups behind the efiles_* groups are the CDF copies without the survivor's note (survnote is a
    // paper note, not a file): no exception is left, every spawned item is a file.
    for( const std::string &group : groups ) {
        CAPTURE( group );
        const item_group_id group_id( group );
        REQUIRE( item_group::group_is_defined( group_id ) );
        for( const itype *type : item_group::every_possible_item_from( group_id ) ) {
            CAPTURE( type->get_id().str() );
            detached_ptr<item> spawned = item::spawn( type->get_id() );
            CHECK( spawned->is_estorable() );
        }
        // the same through real spawns
        for( int i = 0; i < 20; ++i ) {
            for( detached_ptr<item> &spawned : item_group::items_from( group_id, calendar::turn ) ) {
                CAPTURE( spawned->typeId().str() );
                CHECK( spawned->is_estorable() );
            }
        }
    }
}

TEST_CASE( "efile_map_use_reveals_once_and_flips_its_var", "[efile][data][lua]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    const tripoint_abs_omt here( u.abs_omt_pos().x(), u.abs_omt_pos().y(), 0 );

    SECTION( "no stored center: the map around the user is revealed" ) {
        // 3 tiles away is inside the smallest radius, rng( 11, 36 ), of the DDA effect
        const tripoint_abs_omt probe = here + point( 3, 0 );
        get_active_overmapbuffer().set_seen( probe, false );
        REQUIRE_FALSE( get_active_overmapbuffer().seen( probe ) );

        detached_ptr<item> file = item::spawn( itype_id( "efile_map" ) );
        REQUIRE( file->get_var( "map_cache", "" ).empty() );
        const auto first = run_efile_action( "map", *file );
        REQUIRE( first.has_value() );
        // DDA effect_on_conditions_actor::use returns 0: the use consumes nothing
        CHECK( first->value == 0 );
        CHECK( get_active_overmapbuffer().seen( probe ) );
        CHECK( file->get_var( "map_cache", "" ) == "read" );
        CHECK( first->messages == "You found some useful data in the map cache and noted it." );

        // a second use reveals nothing new, says so and keeps the var
        get_active_overmapbuffer().set_seen( probe, false );
        const auto second = run_efile_action( "map", *file );
        REQUIRE( second.has_value() );
        CHECK( second->value == 0 );
        CHECK_FALSE( get_active_overmapbuffer().seen( probe ) );
        CHECK( file->get_var( "map_cache", "" ) == "read" );
        CHECK( second->messages == "You already noted everything this map cache can offer." );
    }

    SECTION( "a stored center (reveal_map_center_omt) is used instead of the user's tile" ) {
        // farther than the largest radius (36), so the user's own surroundings stay untouched
        const tripoint_abs_omt center = here + point( 60, 0 );
        const tripoint_abs_omt near_center = center + point( 2, 0 );
        const tripoint_abs_omt near_user = here + point( 2, 0 );
        get_active_overmapbuffer().set_seen( near_center, false );
        get_active_overmapbuffer().set_seen( near_user, false );

        detached_ptr<item> file = item::spawn( itype_id( "efile_map" ) );
        file->set_var( "reveal_map_center_omt", center );
        const auto result = run_efile_action( "map", *file );
        REQUIRE( result.has_value() );
        CHECK( get_active_overmapbuffer().seen( near_center ) );
        CHECK_FALSE( get_active_overmapbuffer().seen( near_user ) );
        CHECK( file->get_var( "map_cache", "" ) == "read" );
    }

    SECTION( "a file already marked read reveals nothing" ) {
        const tripoint_abs_omt probe = here + point( 3, 0 );
        get_active_overmapbuffer().set_seen( probe, false );
        detached_ptr<item> file = item::spawn( itype_id( "efile_map" ) );
        file->set_var( "map_cache", "read" );
        const auto result = run_efile_action( "map", *file );
        REQUIRE( result.has_value() );
        CHECK_FALSE( get_active_overmapbuffer().seen( probe ) );
    }
}

// DDA EOC_READ_LOCAL_FILES (computer_eocs.json:3-18, npctalk.cpp:5563-5578): the has/lack roll of
// the per-file var snippet_file is made once; "has" shows a snippet of the category
// local_files_simple (the same one every time: its id is kept in the var
// local_files_simple_snippet_id), stores it in the lore on the first read and pops it up;
// "lack" says nothing interesting was found.
TEST_CASE( "efile_lore_result_is_stable_per_file", "[efile][data][lua]" )
{
    clear_all_state();
    const itype *lore = type_of( "efile_lore" );
    REQUIRE( lore != nullptr );
    CHECK( lore->get_use( "EFILE_LORE" ) != nullptr );

    SECTION( "the roll is made once and kept" ) {
        detached_ptr<item> file = item::spawn( itype_id( "efile_lore" ) );
        const auto first = run_efile_action( "lore", *file );
        REQUIRE( first.has_value() );
        const std::string roll = file->get_var( "snippet_file", "" );
        CHECK( ( roll == "has" || roll == "lack" ) );
        const auto second = run_efile_action( "lore", *file );
        REQUIRE( second.has_value() );
        CHECK( file->get_var( "snippet_file", "" ) == roll );
        CHECK( first->value == 0 );
        CHECK( second->value == 0 );
    }
    SECTION( "has: the same snippet is shown each time and stored in the lore once" ) {
        detached_ptr<item> file = item::spawn( itype_id( "efile_lore" ) );
        file->set_var( "snippet_file", "has" );
        const auto first = run_efile_action( "lore", *file );
        REQUIRE( first.has_value() );
        const std::string id = file->get_var( "local_files_simple_snippet_id", "" );
        REQUIRE( !id.empty() );
        CHECK( get_avatar().has_seen_snippet( snippet_id( id ) ) );
        CHECK( first->messages.find( "nothing interesting" ) == std::string::npos );
        const std::optional<translation> text = SNIPPET.get_snippet_by_id( snippet_id( id ) );
        REQUIRE( text.has_value() );
        CHECK( first->messages == text->translated() );
        const auto second = run_efile_action( "lore", *file );
        REQUIRE( second.has_value() );
        CHECK( file->get_var( "local_files_simple_snippet_id", "" ) == id );
        CHECK( second->messages == first->messages );
        CHECK( file->get_var( "snippet_file", "" ) == "has" );
    }
    SECTION( "lack: nothing interesting, nothing stored" ) {
        detached_ptr<item> file = item::spawn( itype_id( "efile_lore" ) );
        file->set_var( "snippet_file", "lack" );
        const std::set<snippet_id> snippets_before = get_avatar().get_snippets();
        const auto result = run_efile_action( "lore", *file );
        REQUIRE( result.has_value() );
        CHECK( result->messages == "You found nothing interesting." );
        CHECK( file->get_var( "local_files_simple_snippet_id", "" ).empty() );
        CHECK( get_avatar().get_snippets() == snippets_before );
    }
}

// The three Lua bindings the use action needs: gapi.random_snippet_id, gapi.get_snippet,
// Avatar:add_snippet and Avatar:has_seen_snippet.  Snippet ids cross the binding as strings.
TEST_CASE( "efile_snippet_lua_bindings", "[efile][data][lua]" )
{
    clear_all_state();
    auto lua = make_lua_state();
    const auto result = lua.safe_script( R"(
        local u = gapi.get_avatar()
        local out = {}
        out.no_category = gapi.random_snippet_id("no_such_snippet_category") == nil
        -- the lore of the avatar is not reset between test cases: take a snippet not read yet
        local id = nil
        for _ = 1, 500 do
          id = gapi.random_snippet_id("local_files_simple")
          if not u:has_seen_snippet(id) then break end
        end
        out.id = id
        out.unknown_text = gapi.get_snippet("no_such_snippet_id") == nil
        out.text = gapi.get_snippet(id)
        out.unseen = u:has_seen_snippet(id) == false
        u:add_snippet(id)
        out.seen = u:has_seen_snippet(id) == true
        out.other_unseen = u:has_seen_snippet("no_such_snippet_id") == false
        return out
    )", sol::script_pass_on_error );
    REQUIRE( result.valid() );
    const sol::table out = result.get<sol::table>();
    CHECK( out.get<bool>( "no_category" ) );
    CHECK( out.get<bool>( "unknown_text" ) );
    CHECK( out.get<bool>( "unseen" ) );
    CHECK( out.get<bool>( "seen" ) );
    CHECK( out.get<bool>( "other_unseen" ) );
    const std::string id = out.get<std::string>( "id" );
    CHECK( id.rfind( "local_files_simple_", 0 ) == 0 );
    const std::optional<translation> expected = SNIPPET.get_snippet_by_id( snippet_id( id ) );
    REQUIRE( expected.has_value() );
    CHECK( out.get<std::string>( "text" ) == expected->translated() );
    CHECK( get_avatar().has_seen_snippet( snippet_id( id ) ) );
}
