// CDF: P3-RETIRE1. The twelve retired obsoletion ids of tests/data/battery_hold.json (kind "retired")
// are deleted as item definitions, so the 7ENDC switch finds no legacy battery item. Nothing spawned
// or crafted them: no item group entry and no live recipe names them (the welder_crude recipe was
// obsolete). Two ids have a live CDF target and keep a MIGRATION (welder_crude -> welder, dab_pen_on
// -> dab_pen); the other ten have no live CDF equivalent (DDA keeps them, CDF does not), so they
// migrate to nothing.
#include "catch/catch.hpp"

#include "calendar.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "type_id.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

const std::vector<std::string> RETIRED_IDS = {
    "closed_loop_extractor_large", "closed_loop_extractor_large_on",
    "closed_loop_extractor_small", "closed_loop_extractor_small_on", "dab_pen_on",
    "vac_oven_small", "vac_oven_small_done", "vac_oven_small_full", "vac_oven_small_on",
    "vac_pump", "vac_pump_on", "welder_crude"
};

// retired id -> the live CDF item it migrates to
const std::vector<std::pair<std::string, std::string>> MIGRATED_IDS = {
    { "welder_crude", "welder" },
    { "dab_pen_on", "dab_pen" },
};

} // namespace

// T1: the twelve ids are not item definitions any more.
// Mutation: re-add one deleted definition (a TEST_DATA TOOL named vac_pump) -> red.
TEST_CASE( "retired ids are not defined item types", "[retired][p3]" )
{
    for( const std::string &id : RETIRED_IDS ) {
        CAPTURE( id );
        CHECK_FALSE( itype_id( id ).is_valid() );
    }
}

// T2: the two ids with a live target resolve to it; the target is a defined item.
// Mutation: delete the welder_crude MIGRATION from data/json/obsoletion/migration.json -> red.
TEST_CASE( "retired ids with a live target migrate to it", "[retired][p3]" )
{
    for( const auto &[from, to] : MIGRATED_IDS ) {
        CAPTURE( from, to );
        REQUIRE( itype_id( to ).is_valid() );
        CHECK( item_controller->migrate_id( itype_id( from ) ) == itype_id( to ) );
        detached_ptr<item> spawned = item_controller->spawn_migrated( itype_id( from ), calendar::turn );
        REQUIRE( spawned );
        CHECK( spawned->typeId() == itype_id( to ) );
    }
}

// T3: no item group still names a retired id (the migrated ones are rewritten on load anyway).
// Mutation: add a TEST_DATA item group entry for vac_pump -> red.
TEST_CASE( "no item group refers to a retired id", "[retired][p3]" )
{
    for( const item_group_id &group : item_controller->get_all_group_names() ) {
        const Item_spawn_data *spawn = item_controller->get_group( group );
        REQUIRE( spawn != nullptr );
        for( const std::string &id : RETIRED_IDS ) {
            CAPTURE( group.str(), id );
            CHECK_FALSE( spawn->has_item( itype_id( id ) ) );
        }
    }
}

// T4: no recipe makes or needs a retired id.
TEST_CASE( "no recipe refers to a retired id", "[retired][p3]" )
{
    const std::set<std::string> retired( RETIRED_IDS.begin(), RETIRED_IDS.end() );
    for( const auto &entry : recipe_dict ) {
        const recipe &rec = entry.first.obj();
        std::vector<std::string> refs = { rec.result().str() };
        for( const std::vector<tool_comp> &alt : rec.simple_requirements().get_tools() ) {
            for( const tool_comp &tool : alt ) {
                refs.push_back( tool.type.str() );
            }
        }
        for( const std::vector<item_comp> &alt : rec.simple_requirements().get_components() ) {
            for( const item_comp &comp : alt ) {
                refs.push_back( comp.type.str() );
            }
        }
        for( const std::string &ref : refs ) {
            if( retired.count( ref ) > 0 ) {
                CAPTURE( rec.ident().str(), ref );
                FAIL( "recipe refers to a retired id" );
            }
        }
    }
}
