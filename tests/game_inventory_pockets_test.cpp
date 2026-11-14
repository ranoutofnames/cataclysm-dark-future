#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "flag.h"
#include "game_inventory.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <string>
#include <utility>

namespace game_menus::inv
{
// CDF: FM5a. Defined in src/game_inventory.cpp, which declares them itself; they are not in
// game_inventory.h (a test hook there would rebuild its includers). Keep the signatures identical.
std::string take_off_none_message();
bool consume_meds_filter( const item &it );
// CDF: FM5 amendment (G43), same pattern: the consume preset is file-local (src/game_inventory.cpp).
bool consume_preset_shows( player &p, const item &it );
// CDF: FM5 amendment (IC1/G11), same pattern: the unload menu filter is file-local.
bool unload_menu_accepts( const item &container );
// CDF: FM5b. Same pattern: the common selector class is file-local (src/game_inventory.cpp:119).
struct common_selector_action {
    bool closes;
    bool category_mode;
};
common_selector_action probe_common_selector_action( player &p, const std::string &action );
} // namespace game_menus::inv

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

// CDF: FM5a, DDA game_inventory.cpp:487. The empty branch shows its text through popup(), a no-op
// in the test build (popup.cpp:269), so take_off and this test share take_off_none_message().
TEST_CASE( "take off with nothing worn says \"You're not wearing anything.\"",
           "[inventory][ui][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    REQUIRE( you.worn.empty() );

    CHECK( game_menus::inv::take_off_none_message() == "You're not wearing anything." );
    CHECK( game_menus::inv::take_off( you ) == nullptr );
}

// CDF: FM5a, DDA game_inventory.cpp:1096-1098. bandages are comestible MED (med.json:218-233);
// rag is not a medication but has the heal use_action (generic.json:1899-1906), so it is a medical
// tool (item_gun_tool_ammo.cpp:156-159) and the filter must list it. knife_combat is neither.
TEST_CASE( "medication filter includes medical tools", "[inventory][ui][fm]" )
{
    clear_all_state();
    detached_ptr<item> bandages = item::spawn( "bandages", calendar::start_of_cataclysm );
    detached_ptr<item> rag = item::spawn( "rag", calendar::start_of_cataclysm );
    detached_ptr<item> knife = item::spawn( "knife_combat", calendar::start_of_cataclysm );

    REQUIRE( bandages->is_medication() );
    REQUIRE_FALSE( rag->is_medication() );
    REQUIRE( rag->is_medical_tool() );
    REQUIRE_FALSE( knife->is_medication() );
    REQUIRE_FALSE( knife->is_medical_tool() );

    CHECK( game_menus::inv::consume_meds_filter( *bandages ) );
    CHECK( game_menus::inv::consume_meds_filter( *rag ) );
    CHECK_FALSE( game_menus::inv::consume_meds_filter( *knife ) );
}

// CDF: FM5b. `;` is VIEW_CATEGORY_MODE. inventory_pick_selector::execute (inventory_ui.cpp:3730)
// closes the screen whenever the selector's handle_action returns true, which is what used to
// happen here. DDA handles the key in inventory_selector::on_input (DDA inventory_ui.cpp:3640) and
// stays open; CDF's on_input (inventory_ui.cpp:3426) has no such branch, so the common selector
// toggles and returns false itself. The class is file-local, so the test drives handle_action
// through game_menus::inv::probe_common_selector_action, as the P3-UI4 tests do for the presets.
TEST_CASE( "view mode key toggles and does not close the inventory", "[inventory][ui][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();

    const game_menus::inv::common_selector_action toggled =
        game_menus::inv::probe_common_selector_action( you, "VIEW_CATEGORY_MODE" );
    CHECK_FALSE( toggled.closes );
    // A fresh selector starts in the category view (inventory_ui.h:771), so the key switched it.
    CHECK_FALSE( toggled.category_mode );

    // unload_all still closes the screen: execute() returns and game_menus::inv::common runs the
    // unload through selected_unload_all().
    CHECK( game_menus::inv::probe_common_selector_action( you, "unload_all" ).closes );
}

// CDF: FM5 amendment (G43), DDA game_inventory.cpp:822 and consumption.cpp:1890-1895. The old
// preset listed a container through Character::can_consume(), so a bottle with a drink in it had a
// row of its own next to the drink. DDA lists an item only when the item itself is consumable.
TEST_CASE( "consume menu hides a bottle that only holds a drink", "[inventory][ui][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &bottle = you.i_add( item::spawn( "bottle_plastic", calendar::start_of_cataclysm ) );
    const detached_ptr<item> water = item::spawn( "water_clean", calendar::start_of_cataclysm );
    bottle.fill_with( *water );
    REQUIRE_FALSE( bottle.is_container_empty() );
    const item &drink = bottle.get_contained();

    CHECK_FALSE( game_menus::inv::consume_preset_shows( you, bottle ) );
    CHECK( game_menus::inv::consume_preset_shows( you, drink ) );
}

// CDF: FM5 amendment (G43), DDA consumption.cpp:1890-1895: frozen food is not offered, MELTS food is.
// CDF: BN has no EDIBLE_FROZEN flag, so MELTS is the only DDA exception left.
TEST_CASE( "consume menu hides frozen food", "[inventory][ui][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &jerky = you.i_add( item::spawn( "jerky", calendar::start_of_cataclysm ) );
    REQUIRE( jerky.is_comestible() );
    REQUIRE_FALSE( jerky.has_flag( flag_FROZEN ) );
    CHECK( game_menus::inv::consume_preset_shows( you, jerky ) );

    jerky.set_flag( flag_FROZEN );
    CHECK_FALSE( game_menus::inv::consume_preset_shows( you, jerky ) );

    jerky.set_flag( flag_MELTS );
    CHECK( game_menus::inv::consume_preset_shows( you, jerky ) );
}

// CDF: FM5 amendment (IC1/G11), DDA item_container.cpp:460. tool_belt (belts.json:428) has only
// carry pockets, so CDF's is_container() is false while DDA's "has a container pocket" is true. Food
// in its pouch must still be offered by the consume menu; the parent guard used to drop it.
TEST_CASE( "consume menu lists food inside a belt pouch", "[inventory][ui][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    detached_ptr<item> belt = item::spawn( "tool_belt", calendar::start_of_cataclysm );
    detached_ptr<item> snack = item::spawn( "jerky", calendar::start_of_cataclysm );
    item *snack_ptr = snack.get();
    REQUIRE( belt->put_in( std::move( snack ), pocket_type::CONTAINER ).success() );

    REQUIRE_FALSE( belt->is_container() );
    REQUIRE( belt->has_pocket_type( pocket_type::CONTAINER ) );
    REQUIRE( snack_ptr->where() == item_location_type::container );

    CHECK( game_menus::inv::consume_preset_shows( you, *snack_ptr ) );
}

// CDF: FM5 amendment (IC1/G11), DDA item_container.cpp:460: a belt holding something is a valid
// unload target in DDA, because a carry pocket is a container pocket.
TEST_CASE( "unload menu accepts a belt with something in its pouch", "[inventory][ui][fm]" )
{
    clear_all_state();
    detached_ptr<item> belt = item::spawn( "tool_belt", calendar::start_of_cataclysm );
    REQUIRE_FALSE( belt->is_container() );
    CHECK_FALSE( game_menus::inv::unload_menu_accepts( *belt ) );

    REQUIRE( belt->put_in( item::spawn( "jerky", calendar::start_of_cataclysm ),
                           pocket_type::CONTAINER ).success() );
    CHECK( game_menus::inv::unload_menu_accepts( *belt ) );
}
