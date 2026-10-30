#include "catch/catch.hpp"

#include "clzones.h"
#include "json.h"
#include "memory_fast.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>

// MOLLE2 step 3 (DDA unload zone, clzones.h unload_options): the zone type, the options object
// and its save format. The sort step (M3-M5) is tested with the sort-loop commit.

namespace
{
const zone_type_id zone_unload_all( "UNLOAD_ALL" );

auto options_to_json( const zone_options &options ) -> std::string
{
    std::ostringstream out;
    JsonOut jsout( out );
    jsout.start_object();
    options.serialize( jsout );
    jsout.end_object();
    return out.str();
}

void options_from_json( zone_options &options, const std::string &text )
{
    std::istringstream in( text );
    JsonIn jsin( in );
    const JsonObject jo = jsin.get_object();
    options.deserialize( jo );
}
} // namespace

TEST_CASE( "unload_zone_type_loads", "[molle][zone]" )
{
    clear_all_state();
    // the zone type comes from data/json/loot_zones.json
    CHECK( zone_unload_all.is_valid() );
    CHECK( zone_manager::get_manager().has_type( zone_unload_all ) );

    const shared_ptr_fast<zone_options> options = zone_options::create( zone_unload_all );
    REQUIRE( options );
    // only an unload_options answers has_options() and the descriptions of the zone
    CHECK( options->has_options() );
    CHECK( dynamic_cast<const unload_options *>( options.get() ) != nullptr );
    CHECK( zone_options::is_valid( zone_unload_all, *options ) );
    CHECK( options->get_zone_name_suggestion().find( "Unload" ) != std::string::npos );

    // other option classes and the plain base class are refused for this type
    const shared_ptr_fast<zone_options> plot = zone_options::create( zone_type_id( "FARM_PLOT" ) );
    REQUIRE( plot );
    CHECK_FALSE( zone_options::is_valid( zone_unload_all, *plot ) );
    const shared_ptr_fast<zone_options> plain = zone_options::create( zone_type_id( "LOOT_UNSORTED" ) );
    REQUIRE( plain );
    CHECK_FALSE( zone_options::is_valid( zone_unload_all, *plain ) );
    // and an unload_options is refused for another type
    CHECK_FALSE( zone_options::is_valid( zone_type_id( "LOOT_UNSORTED" ), *options ) );
}

TEST_CASE( "unload_options_round_trip", "[molle][zone]" )
{
    clear_all_state();
    const std::string everything =
        R"({"mark":"x","mods":true,"molle":true,"sparse_only":true,"sparse_threshold":7,"always_unload":true})";

    SECTION( "defaults" ) {
        const shared_ptr_fast<zone_options> fresh = zone_options::create( zone_unload_all );
        REQUIRE( fresh );
        const unload_options *defaults = dynamic_cast<const unload_options *>( fresh.get() );
        REQUIRE( defaults != nullptr );
        CHECK( defaults->get_mark().empty() );
        CHECK_FALSE( defaults->unload_mods() );
        CHECK_FALSE( defaults->unload_molle() );
        CHECK_FALSE( defaults->unload_sparse_only() );
        CHECK( defaults->unload_sparse_threshold() == 20 );
        CHECK_FALSE( defaults->unload_always() );
        std::istringstream in( options_to_json( *fresh ) );
        JsonIn jsin( in );
        const JsonObject jo = jsin.get_object();
        CHECK( jo.get_string( "mark" ).empty() );
        CHECK_FALSE( jo.get_bool( "mods" ) );
        CHECK_FALSE( jo.get_bool( "molle" ) );
        CHECK_FALSE( jo.get_bool( "sparse_only" ) );
        // DDA clzones.h: int sparse_threshold = 20
        CHECK( jo.get_int( "sparse_threshold" ) == 20 );
        CHECK_FALSE( jo.get_bool( "always_unload" ) );
    }

    SECTION( "all six values survive a save and a load" ) {
        const shared_ptr_fast<zone_options> first = zone_options::create( zone_unload_all );
        REQUIRE( first );
        options_from_json( *first, everything );
        const unload_options *loaded = dynamic_cast<const unload_options *>( first.get() );
        REQUIRE( loaded != nullptr );
        CHECK( loaded->get_mark() == "x" );
        CHECK( loaded->unload_mods() );
        CHECK( loaded->unload_molle() );
        CHECK( loaded->unload_sparse_only() );
        CHECK( loaded->unload_sparse_threshold() == 7 );
        CHECK( loaded->unload_always() );
        // the first object holds the values: a second one reads what the first one wrote
        const std::string saved = options_to_json( *first );
        const shared_ptr_fast<zone_options> second = zone_options::create( zone_unload_all );
        REQUIRE( second );
        options_from_json( *second, saved );

        for( const std::string &text : { saved, options_to_json( *second ) } ) {
            std::istringstream in( text );
            JsonIn jsin( in );
            const JsonObject jo = jsin.get_object();
            CHECK( jo.get_string( "mark" ) == "x" );
            CHECK( jo.get_bool( "mods" ) );
            CHECK( jo.get_bool( "molle" ) );
            CHECK( jo.get_bool( "sparse_only" ) );
            CHECK( jo.get_int( "sparse_threshold" ) == 7 );
            CHECK( jo.get_bool( "always_unload" ) );
        }
    }
}
