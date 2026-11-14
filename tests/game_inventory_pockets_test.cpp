#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "game_inventory.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <string>
#include <utility>

// ---------------------------------------------------------------------------
// CDF: P3-UI4 (the insert picker, DDA game_inventory.cpp:2060-2086 and the preset
// inventory_holster_preset, inventory_ui.cpp:907-958), CDF-only. DDA has no test of the preset.
// The preset is in an anonymous namespace; these call it through game_menus::inv::insert_preset_shows
// and insert_preset_denial. The expected strings are those of item_pocket.cpp (can_contain) and
// were read from there, not produced by the code under test. Real ids: ballistic_vest_esapi has two
// ABLATIVE_LARGE and two ABLATIVE_MEDIUM plate pockets (cdf_ballistic_vests.json), esapi_plate is
// ABLATIVE_LARGE (cdf_ballistic_armor.json).
// ---------------------------------------------------------------------------

namespace
{
auto spawn_plate() -> detached_ptr<item>
{
    return item::spawn( "esapi_plate", calendar::start_of_cataclysm );
}

auto spawn_vest_with_plates( int plates ) -> detached_ptr<item>
{
    detached_ptr<item> vest = item::spawn( "ballistic_vest_esapi", calendar::start_of_cataclysm );
    for( int i = 0; i < plates; i++ ) {
        REQUIRE( vest->put_in( spawn_plate(), pocket_type::CONTAINER ).success() );
    }
    return vest;
}
} // namespace

// CDF: P3-UI4 T1; item_pocket.cpp "ablative pocket already contains a plate".
TEST_CASE( "insert_denial_for_a_plate_when_both_large_slots_are_full", "[inventory][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> vest = spawn_vest_with_plates( 2 );
    detached_ptr<item> third = spawn_plate();
    const std::string denial = game_menus::inv::insert_preset_denial( *vest, get_avatar(), *third );
    CHECK( denial.find( "already contains a plate" ) != std::string::npos );
}

// CDF: P3-UI4 T2
TEST_CASE( "insert_denial_for_a_plate_when_a_large_slot_is_free", "[inventory][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> vest = spawn_vest_with_plates( 1 );
    detached_ptr<item> second = spawn_plate();
    CHECK( game_menus::inv::insert_preset_denial( *vest, get_avatar(), *second ).empty() );

    detached_ptr<item> empty_vest = spawn_vest_with_plates( 0 );
    CHECK( game_menus::inv::insert_preset_denial( *empty_vest, get_avatar(), *second ).empty() );
}

// CDF: P3-UI4 T3; no plate pocket takes a knife, the flag restriction text is the reason.
TEST_CASE( "insert_denial_for_an_item_no_plate_slot_accepts", "[inventory][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> vest = spawn_vest_with_plates( 0 );
    detached_ptr<item> knife = item::spawn( "knife_combat", calendar::start_of_cataclysm );
    const std::string denial = game_menus::inv::insert_preset_denial( *vest, get_avatar(), *knife );
    CHECK_FALSE( denial.empty() );
    CHECK( denial.find( "holster does not accept" ) != std::string::npos );
}

// CDF: P3-UI4 T4; existing behaviour (DDA inventory_holster_preset::is_shown), pinned.
TEST_CASE( "insert_preset_hides_the_holster_and_its_direct_contents", "[inventory][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> vest = item::spawn( "ballistic_vest_esapi", calendar::start_of_cataclysm );
    detached_ptr<item> plate = spawn_plate();
    item *plate_ptr = plate.get();
    REQUIRE( vest->put_in( std::move( plate ), pocket_type::CONTAINER ).success() );
    detached_ptr<item> knife = item::spawn( "knife_combat", calendar::start_of_cataclysm );

    CHECK_FALSE( game_menus::inv::insert_preset_shows( *vest, get_avatar(), *vest ) );
    CHECK_FALSE( game_menus::inv::insert_preset_shows( *vest, get_avatar(), *plate_ptr ) );
    CHECK( game_menus::inv::insert_preset_shows( *vest, get_avatar(), *knife ) );
}
