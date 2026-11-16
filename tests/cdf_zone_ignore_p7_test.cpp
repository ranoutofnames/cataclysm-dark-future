// pk7 F111 (DDA activity_item_handling.cpp:1067-1081 ignore_contents, clzones.cpp
// ignorable_options): a sorting zone can be marked "ignore contents" so the sorter leaves its
// tile alone. DDA's ignorable zone types are AUTO_EAT, AUTO_DRINK, DISASSEMBLE and
// SOURCE_FIREWOOD (clzones.cpp:85-90).
#include "catch/catch.hpp"

#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "clzones.h"
#include "coordinates.h"
#include "item.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>
#include <vector>

// CDF: defined in clzones.cpp; the ignorable_options class is file-local there (this job may not
// edit clzones.h), so the declaration is repeated for the test.
bool zone_ignore_contents( const Character &you, const tripoint_abs_ms &src );

namespace
{
const zone_type_id zone_auto_eat( "AUTO_EAT" );
const zone_type_id zone_unsorted( "LOOT_UNSORTED" );
const zone_type_id zone_loot_custom( "LOOT_CUSTOM" );
const activity_id act_move_loot( "ACT_MOVE_LOOT" );

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

void add_zone( const zone_type_id &type, const tripoint_bub_ms &where,
               const std::string &options_json = "" )
{
    shared_ptr_fast<zone_options> options = zone_options::create( type );
    if( !options_json.empty() ) {
        options_from_json( *options, options_json );
    }
    const tripoint_abs_ms abs = bub_to_abs( where );
    zone_manager::get_manager().add( "pk7 test", type, your_fac, false, true, abs, abs, options );
    zone_manager::get_manager().cache_data();
}

void run_sort( avatar &you )
{
    you.assign_activity( act_move_loot );
    for( int turn = 0; turn < 300 && you.has_activity( act_move_loot ); ++turn ) {
        you.set_moves( 100 );
        activity_on_turn_move_loot( *you.activity, you );
    }
}

auto count_on( const tripoint_bub_ms &where, const itype_id &type ) -> int
{
    int found = 0;
    for( item *it : get_map().i_at( where ) ) {
        if( it->typeId() == type ) {
            found += it->count();
        }
    }
    return found;
}

struct zone_scene {
    avatar &you = get_avatar();
    tripoint_bub_ms src;
    tripoint_bub_ms dest;

    zone_scene() {
        clear_all_state();
        clear_map();
        zone_manager::reset_manager();
        clear_character( you, true );
        src = you.bub_pos();
        dest = tripoint_bub_ms( src.x() + 3, src.y(), src.z() );
    }
};
} // namespace

TEST_CASE( "ignore_contents_zone_options", "[zone][p7]" )
{
    clear_all_state();
    const shared_ptr_fast<zone_options> options = zone_options::create( zone_auto_eat );
    REQUIRE( options );
    CHECK( options->has_options() );
    CHECK( zone_options::is_valid( zone_auto_eat, *options ) );
    // the option belongs to the ignorable zone types only
    CHECK_FALSE( zone_options::is_valid( zone_unsorted, *options ) );

    const shared_ptr_fast<zone_options> plain = zone_options::create( zone_unsorted );
    REQUIRE( plain );
    CHECK_FALSE( plain->has_options() );
    CHECK_FALSE( zone_options::is_valid( zone_auto_eat, *plain ) );

    SECTION( "defaults to off" ) {
        REQUIRE( options->get_descriptions().size() == 1 );
        CHECK( options->get_descriptions().front().first == "Ignore contents: " );
        CHECK( options->get_descriptions().front().second == "False" );
        std::istringstream in( options_to_json( *options ) );
        JsonIn jsin( in );
        const JsonObject jo = jsin.get_object();
        CHECK_FALSE( jo.get_bool( "ignore_contents" ) );
    }

    SECTION( "the flag survives a save and a load" ) {
        options_from_json( *options, R"({"ignore_contents":true})" );
        CHECK( options->get_descriptions().front().second == "True" );
        const shared_ptr_fast<zone_options> loaded = zone_options::create( zone_auto_eat );
        REQUIRE( loaded );
        options_from_json( *loaded, options_to_json( *options ) );
        std::istringstream in( options_to_json( *loaded ) );
        JsonIn jsin( in );
        const JsonObject jo = jsin.get_object();
        CHECK( jo.get_bool( "ignore_contents" ) );
    }
}

TEST_CASE( "ignore_contents_skips_the_tile_in_sort", "[zone][p7]" )
{
    zone_scene scene;
    get_map().add_item_or_charges( scene.src, item::spawn( itype_id( "rock" ), calendar::turn ) );
    add_zone( zone_unsorted, scene.src );
    add_zone( zone_loot_custom, scene.dest, R"({"mark":"rock"})" );

    SECTION( "option off: the rock is sorted" ) {
        add_zone( zone_auto_eat, scene.src, R"({"ignore_contents":false})" );
        run_sort( scene.you );
        CHECK( count_on( scene.src, itype_id( "rock" ) ) == 0 );
        CHECK( count_on( scene.dest, itype_id( "rock" ) ) == 1 );
    }

    SECTION( "option on: the tile is left alone" ) {
        add_zone( zone_auto_eat, scene.src, R"({"ignore_contents":true})" );
        CHECK( zone_ignore_contents( scene.you, bub_to_abs( scene.src ) ) );
        CHECK_FALSE( zone_ignore_contents( scene.you, bub_to_abs( scene.dest ) ) );
        run_sort( scene.you );
        CHECK( count_on( scene.src, itype_id( "rock" ) ) == 1 );
        CHECK( count_on( scene.dest, itype_id( "rock" ) ) == 0 );
    }
}
