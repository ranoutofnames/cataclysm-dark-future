// COMPAT5 (COMPAT-DDA-PROBE1 A6, A12; AUTO-240): DDA migration-family types are read and logged
// once instead of aborting the file, and an unresolved copy-from names the parent, child and mod.
// Fixtures: tests/data/compat_migration/.
#include "catch/catch.hpp"

#include <cstddef>
#include <string>

#include "flag.h"
#include "fstream_utils.h"
#include "init.h"
#include "item_factory.h"
#include "type_id.h"
#include "json.h"
#include "json_source_location.h"
#include "type_id.h"

// CDF: COMPAT4 test hook (src/init.cpp): log lines written for a DDA-only type.
size_t compat_inert_type_log_count( const std::string &type );
// CDF: COMPAT5 test hook (src/init.cpp): text of the report for a deferred object that never resolved.
std::string compat_unresolved_deferred_message( const json_source_location &loc,
        const std::string &src );

namespace
{
const std::string MIG_FIXTURES = "tests/data/compat_migration/";

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
} // namespace

TEST_CASE( "dda_migration_types_do_not_abort", "[compat][migration][p3]" )
{
    REQUIRE_NOTHROW( load_fixture_objects( MIG_FIXTURES + "all_types.json", "compat5" ) );
    CHECK( flag_id( "COMPAT5_NORMAL_FLAG" ).is_valid() );
    for( const char *type : { "TRAIT_MIGRATION", "bionic_migration", "effect_migration",
                              "vehicle_part_migration", "ter_furn_migration",
                              "overmap_special_migration", "camp_migration"
                            } ) {
        INFO( type );
        CHECK( compat_inert_type_log_count( type ) == 1 );
    }
}

TEST_CASE( "dda_migration_types_log_once", "[compat][migration][p3]" )
{
    REQUIRE_NOTHROW( load_fixture_objects( MIG_FIXTURES + "log_once.json", "compat5" ) );
    CHECK( compat_inert_type_log_count( "camp_migration" ) == 1 );
}

// All seven types are inert (no CDF subsystem has a migration table for them), so no id is applied.
TEST_CASE( "applied_types_resolve", "[compat][migration][p3]" )
{
    REQUIRE_NOTHROW( load_fixture_objects( MIG_FIXTURES + "all_types.json", "compat5" ) );
    CHECK_FALSE( trait_id( "compat5_new_trait" ).is_valid() );
    CHECK_FALSE( itype_id( "compat5_old_trait" ).is_valid() );
}

TEST_CASE( "copy_from_missing_parent_names_parent", "[compat][migration][p3]" )
{
    std::string msg;
    read_from_file_json( MIG_FIXTURES + "missing_parent.json", [&]( JsonIn & jsin ) {
        JsonArray all = jsin.get_array();
        JsonObject jo = all.next_object();
        jo.allow_omitted_members();
        msg = compat_unresolved_deferred_message( jo.get_source_location(), "compat5mod" );
    } );
    CHECK( msg.find( "compat5_parent_never_defined" ) != std::string::npos );
    CHECK( msg.find( "compat5_orphan_child" ) != std::string::npos );
    CHECK( msg.find( "compat5mod" ) != std::string::npos );
    // behaviour unchanged: the child is not loaded
    CHECK_FALSE( itype_id( "compat5_orphan_child" ).is_valid() );
}
