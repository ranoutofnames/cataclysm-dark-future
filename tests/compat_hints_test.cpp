// COMPAT4 (COMPAT-DDA-PROBE1 A5, A9, A10; AUTO-240): DDA-only top-level types are read and
// logged once instead of aborting the file, the inert DDA flag file defines each id once, and
// an unknown DDA key error carries the CDF equivalent. Fixtures: tests/data/compat_hints/.
#include "catch/catch.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "debug.h"
#include "filesystem.h"
#include "flag.h"
#include "fstream_utils.h"
#include "init.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "type_id.h"

// CDF: COMPAT4 test hook, defined in src/init.cpp: number of log lines written for a DDA-only type.
size_t compat_inert_type_log_count( const std::string &type );

namespace {

const std::string HINT_FIXTURES = "tests/data/compat_hints/";
const std::string FLAG_FILE = "data/json/flags_dda_compat.json";

// Loads each object of a fixture array through the real type table, as a mod file would.
void load_fixture_objects( const std::string &path, const std::string &src )
{
    const bool ok = read_from_file_json( path, [&]( JsonIn & jsin ) {
        JsonArray all = jsin.get_array();
        while( all.has_more() ) {
            JsonObject jo = all.next_object();
            DynamicDataLoader::get_instance().load_object( jo, src );
        }
    } );
    REQUIRE( ok );
}

// Collects the ids of every json_flag object in one file.
std::vector<std::string> json_flag_ids_in( const std::string &path )
{
    std::vector<std::string> out;
    read_from_file_json( path, [&]( JsonIn & jsin ) {
        if( !jsin.test_array() ) {
            return;
        }
        JsonArray all = jsin.get_array();
        while( all.has_more() ) {
            JsonObject jo = all.next_object();
            jo.allow_omitted_members();
            if( jo.get_string( "type", "" ) == "json_flag" ) {
                out.push_back( jo.get_string( "id" ) );
            }
        }
    } );
    return out;
}

} // namespace

TEST_CASE( "dda_only_inert_types_load_and_log_once", "[compat][p3]" )
{
    REQUIRE_NOTHROW( load_fixture_objects( HINT_FIXTURES + "inert_types.json", "compat4" ) );
    CHECK( flag_id( "COMPAT4_FIXTURE_FLAG" ).is_valid() );
    CHECK( compat_inert_type_log_count( "widget" ) == 1 );
}

TEST_CASE( "dda_flag_compat_file_defines_each_id_once", "[compat][p3]" )
{
    const std::vector<std::string> compat_ids = json_flag_ids_in( FLAG_FILE );
    REQUIRE_FALSE( compat_ids.empty() );

    std::set<std::string> unique( compat_ids.begin(), compat_ids.end() );
    CHECK( unique.size() == compat_ids.size() );

    for( const std::string &id : unique ) {
        INFO( id );
        CHECK( flag_id( id ).is_valid() );
    }

    // Count each id across every data/json file: the compat file must hold the only definition.
    std::set<std::string> defined_elsewhere;
    for( const std::string &file : get_files_from_path( ".json", "data/json", true, true ) ) {
        if( file.ends_with( "flags_dda_compat.json" ) ) {
            continue;
        }
        for( const std::string &id : json_flag_ids_in( file ) ) {
            if( unique.count( id ) ) {
                INFO( id << " also defined in " << file );
                defined_elsewhere.insert( id );
            }
        }
    }
    CHECK( defined_elsewhere.empty() );
}

TEST_CASE( "dda_edible_frozen_fixture_item_flags_are_defined", "[compat][p3]" )
{
    CHECK( flag_id( "EDIBLE_FROZEN" ).is_valid() );
    load_fixture_objects( HINT_FIXTURES + "item_edible_frozen.json", "compat4" );
    const std::vector<const itype *> found = item_controller->find( []( const itype & t ) {
        return t.get_id() == itype_id( "compat4_frozen_item" );
    } );
    REQUIRE( found.size() == 1 );
    const itype *def = found.front();
    REQUIRE_FALSE( def->item_tags.empty() );
    // Same check Item_factory::finalize_post runs on item_tags ("undefined flag" debugmsg).
    for( const flag_id &f : def->item_tags ) {
        INFO( f.str() );
        CHECK( f.is_valid() );
    }
}

TEST_CASE( "dda_armor_hint_names_armor_data", "[compat][p3]" )
{
    const std::string msg = capture_debugmsg_during( [&]() {
        load_fixture_objects( HINT_FIXTURES + "item_armor_array.json", "compat4" );
    } );
    CHECK( msg.find( "Invalid or misplaced field name \"armor\"" ) != std::string::npos );
    CHECK( msg.find( "CDF reads armor_data, with armor_portion_data nested inside it" ) != std::string::npos );
    CHECK( msg.find( "armor_portion_data, not a DDA armor array" ) == std::string::npos );
}

TEST_CASE( "dda_variants_key_still_errors_with_cdf_hint", "[compat][p3]" )
{
    const std::string msg = capture_debugmsg_during( [&]() {
        load_fixture_objects( HINT_FIXTURES + "item_variants.json", "compat4" );
    } );
    CHECK( msg.find( "Invalid or misplaced field name \"variants\"" ) != std::string::npos );
    CHECK( msg.find( "not read by CDF" ) != std::string::npos );
}
