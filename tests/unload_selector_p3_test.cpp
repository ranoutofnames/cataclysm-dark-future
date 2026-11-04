#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "game_inventory.h"
#include "input.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_pocket.h"
#include "json.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "uistate.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// CDF: P3-5-4 (the unload selector, DDA PR-less port of inventory_ui.cpp:5302-5377 and
// game_inventory.cpp:3112-3153), CDF-only. DDA has no test of unload_selector, its hint or
// CONTAIN_MODE (a grep of DDA tests/ at b4b62254ae2 for unload_selector, unload_auto_contain and
// CONTAIN_MODE is empty). execute() waits for keyboard input and is not driven here (owner
// checklist: after 6-4c wires U). item_location becomes item * (D1) and the return value is the
// named struct unload_selection (AGENT_RULES: no std::pair in interfaces). Expected numbers come
// from the JSON capacities and the keybindings.json entry, not from the code under test.
// ---------------------------------------------------------------------------

namespace
{

// CDF: ctxt and hint_string are protected; these read what a selector registered and said.
class unload_selector_probe : public unload_selector
{
    public:
        using unload_selector::unload_selector;

        auto actions() const -> std::vector<std::string> {
            return ctxt.get_registered_actions_copy();
        }
        auto hint() -> std::string {
            return hint_string();
        }
};

// CDF: stanag30 is not used; glockmag takes 9mm and holds 15 (data/json/items/magazine/9mm.json)
auto loaded_mag( int rounds ) -> detached_ptr<item>
{
    detached_ptr<item> mag = item::spawn( "glockmag", calendar::start_of_cataclysm, 0 );
    if( rounds > 0 ) {
        REQUIRE( mag->put_in( item::spawn( "9mm", calendar::start_of_cataclysm, rounds ),
                              pocket_type::MAGAZINE ).success() );
    }
    return mag;
}

// RAII: sets the thief mode and pins it with THIEF_MODE_KEEP = YES (as cdf_item_reload_entry_p3_test.cpp does),
// so check_stealing never reaches the pickup::query_thief() popup.
struct thief_guard {
    explicit thief_guard( const std::string &mode ) {
        avatar &who = get_avatar();
        who.set_value( "THIEF_MODE", mode );
        who.set_value( "THIEF_MODE_KEEP", "YES" );
    }
    ~thief_guard() {
        avatar &who = get_avatar();
        who.set_value( "THIEF_MODE", "THIEF_ASK" );
        who.set_value( "THIEF_MODE_KEEP", "NO" );
    }
};

} // namespace

// CDF: P3-5-4 U1; DDA game_inventory.cpp:3126-3127 (unload_selector_preset::is_shown).
TEST_CASE( "unload_selector_shows_loaded_gun_not_empty_gun", "[p3][unload][ui][5-4]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;

    // CDF: sw_619 takes 38_special (data/json/items/gun/38.json) and keeps its rounds as direct charges
    detached_ptr<item> loaded_gun = item::spawn( "sw_619", bday, 0 );
    loaded_gun->ammo_set( itype_id( "38_special" ), 6 );
    REQUIRE( loaded_gun->ammo_remaining() == 6 );
    detached_ptr<item> empty_gun = item::spawn( "sw_619", bday, 0 );
    REQUIRE( empty_gun->ammo_remaining() == 0 );
    CHECK( game_menus::inv::unload_selector_shows( *loaded_gun ) );
    CHECK_FALSE( game_menus::inv::unload_selector_shows( *empty_gun ) );

    detached_ptr<item> full_mag = loaded_mag( 15 );
    detached_ptr<item> empty_mag = loaded_mag( 0 );
    REQUIRE( full_mag->ammo_remaining() == 15 );
    REQUIRE( empty_mag->ammo_remaining() == 0 );
    CHECK( game_menus::inv::unload_selector_shows( *full_mag ) );
    CHECK_FALSE( game_menus::inv::unload_selector_shows( *empty_mag ) );

    // CDF: knife_combat (swords_and_blades.json) holds nothing
    detached_ptr<item> knife = item::spawn( "knife_combat", bday );
    CHECK_FALSE( game_menus::inv::unload_selector_shows( *knife ) );

    SECTION( "a magazine seated in a gun's well is no row of its own, the gun is" ) {
        // CDF: glock_19 has a magazine well that takes glockmag (stated in cdf_item_reload_entry_p3_test.cpp)
        detached_ptr<item> mag = loaded_mag( 15 );
        item *mag_ptr = mag.get();
        detached_ptr<item> glock = item::spawn( "glock_19", bday, 0 );
        glock->put_in( std::move( mag ) );
        REQUIRE( glock->magazine_current() == mag_ptr );
        REQUIRE( mag_ptr->parent_item() == glock.get() );
        // DDA unload_selector_preset: _pk_type = { CONTAINER, MOD }, so the contents of a magazine well are not listed
        CHECK( game_menus::inv::unload_selector_shows( *glock ) );
        CHECK_FALSE( game_menus::inv::unload_selector_shows( *mag_ptr ) );
    }
}

// CDF: P3-5-4 U2; DDA game_inventory.cpp:3126-3127 and 3119 (_pk_type = { CONTAINER, MOD }).
TEST_CASE( "unload_selector_shows_container_with_contents", "[p3][unload][ui][5-4]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> empty_pack = item::spawn( "backpack", bday );
    CHECK_FALSE( game_menus::inv::unload_selector_shows( *empty_pack ) );

    SECTION( "a backpack holding a loaded magazine" ) {
        detached_ptr<item> mag = loaded_mag( 15 );
        item *mag_ptr = mag.get();
        // CDF: backpack (data/json/items/armor/storage.json) has container pockets that take a glockmag
        detached_ptr<item> pack = item::spawn( "backpack", bday );
        REQUIRE( pack->put_in( std::move( mag ), pocket_type::CONTAINER ).success() );
        REQUIRE( mag_ptr->parent_item() == pack.get() );
        CHECK( game_menus::inv::unload_selector_shows( *pack ) );
        // the magazine lies in a CONTAINER pocket of the pack: it is a row of its own as well
        CHECK( game_menus::inv::unload_selector_shows( *mag_ptr ) );
    }
    SECTION( "a backpack holding a plain item" ) {
        // CDF: knife_combat (swords_and_blades.json) fits a backpack
        detached_ptr<item> pack = item::spawn( "backpack", bday );
        REQUIRE( pack->put_in( item::spawn( "knife_combat", bday ), pocket_type::CONTAINER ).success() );
        CHECK( game_menus::inv::unload_selector_shows( *pack ) );
    }
}

// CDF: P3-5-4 U3; DDA game_inventory.cpp:3126 (!is_invisible_installed_gunmod) and item_location.cpp:1181-1191.
TEST_CASE( "unload_selector_hides_installed_gunmods", "[p3][unload][ui][5-4]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;

    // CDF: ar15 (gun/223.json) takes the underslung shotgun u_shotgun (gunmod/underbarrel.json:611, clip 2, ammo shot)
    detached_ptr<item> rifle = item::spawn( "ar15", bday, 0 );
    REQUIRE( rifle->put_in( item::spawn( "u_shotgun", bday, 0 ), pocket_type::MOD ).success() );
    REQUIRE( rifle->gunmods().size() == 1 );
    item &mod = *rifle->gunmods().front();
    mod.ammo_set( itype_id( "shot_00" ), 1 );
    REQUIRE( mod.ammo_remaining() == 1 );
    REQUIRE( mod.parent_item() == rifle.get() );
    // the loaded mod makes the gun unloadable (item_funcs::can_be_unloaded walks the mods) ...
    CHECK( game_menus::inv::unload_selector_shows( *rifle ) );
    // ... but the mod itself is no row
    CHECK_FALSE( game_menus::inv::unload_selector_shows( mod ) );

    SECTION( "a mod with is_visible_when_installed stays a row" ) {
        // CDF: TEST_DATA test_visible_underslung_shotgun is u_shotgun with the key
        detached_ptr<item> rifle2 = item::spawn( "ar15", bday, 0 );
        REQUIRE( rifle2->put_in( item::spawn( "test_visible_underslung_shotgun", bday, 0 ),
                                 pocket_type::MOD ).success() );
        item &visible = *rifle2->gunmods().front();
        visible.ammo_set( itype_id( "shot_00" ), 1 );
        REQUIRE( visible.ammo_remaining() == 1 );
        CHECK( game_menus::inv::unload_selector_shows( visible ) );
    }
    SECTION( "the same mod outside a gun is a row" ) {
        detached_ptr<item> loose = item::spawn( "u_shotgun", bday, 0 );
        loose->ammo_set( itype_id( "shot_00" ), 1 );
        REQUIRE( loose->ammo_remaining() == 1 );
        CHECK( game_menus::inv::unload_selector_shows( *loose ) );
    }
}

// CDF: P3-5-4 U4; DDA inventory_ui.cpp:5311-5315 (unload_selector::hint_string).
TEST_CASE( "unload_selector_hint_names_the_destination_mode", "[p3][unload][ui][5-4]" )
{
    clear_all_state();
    inp_mngr.init();
    avatar &you = get_avatar();
    const bool saved = uistate.unload_auto_contain;
    unload_selector_probe sel( you );

    // CDF: the key text comes from the INVENTORY context, "Select destination" is DDA's wording
    input_context ctxt( "INVENTORY" );
    ctxt.register_action( "CONTAIN_MODE" );
    const std::string key_desc = ctxt.get_desc( "CONTAIN_MODE" );
    REQUIRE_FALSE( key_desc.empty() );

    uistate.unload_auto_contain = false;
    const std::string manual = sel.hint();
    CHECK( manual.find( "Manual" ) != std::string::npos );
    CHECK( manual.find( "Auto" ) == std::string::npos );
    CHECK( manual.find( key_desc ) != std::string::npos );
    CHECK( manual.find( "Select destination" ) != std::string::npos );

    uistate.unload_auto_contain = true;
    const std::string automatic = sel.hint();
    CHECK( automatic.find( "Auto" ) != std::string::npos );
    CHECK( automatic.find( "Manual" ) == std::string::npos );
    CHECK( automatic.find( key_desc ) != std::string::npos );

    uistate.unload_auto_contain = saved;
}

// CDF: P3-5-4 U6; DDA keybindings.json:2630-2636 (CONTAIN_MODE) and inventory_ui.cpp:5304 (register_action).
TEST_CASE( "contain_mode_key_is_defined", "[p3][unload][ui][5-4]" )
{
    clear_all_state();
    inp_mngr.init();
    avatar &you = get_avatar();

    input_context ctxt( "INVENTORY" );
    ctxt.register_action( "CONTAIN_MODE" );
    // CDF: DDA binds U and shift-u; BN's keyboard input method has the one character "U" (keybindings.json
    // 'TOGGLE_UNREAD_RECIPES_FIRST' is the precedent for a capital-letter binding)
    const std::vector<char> keys = ctxt.keys_bound_to( "CONTAIN_MODE" );
    REQUIRE( keys.size() == 1 );
    CHECK( keys.front() == 'U' );
    CHECK( ctxt.get_action_name( "CONTAIN_MODE" ) == "Toggle contain mode for unloaded items" );

    const std::string dmsg = capture_debugmsg_during( [&you]() {
        const unload_selector_probe sel( you );
        const std::vector<std::string> actions = sel.actions();
        CHECK( std::ranges::find( actions, std::string( "CONTAIN_MODE" ) ) != actions.end() );
    } );
    CHECK( dmsg.empty() );
}

// CDF: P3-5-4 U8; DDA avatar_action.h (check_stealing) and activity_actor.cpp:10004-10028.
TEST_CASE( "check_stealing_is_public", "[p3][unload][ui][5-4]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;
    // CDF: foreign faction, data/json/npcs/factions.json:468; the avatar is your_followers (avatar.cpp:1280-1283)
    detached_ptr<item> foreign = item::spawn( "sw_619", bday, 0 );
    foreign->set_owner( faction_id( "hells_raiders" ) );
    REQUIRE_FALSE( foreign->is_owned_by( who, true ) );
    detached_ptr<item> mine = item::spawn( "sw_619", bday, 0 );
    mine->set_owner( who );
    REQUIRE( mine->is_owned_by( who, true ) );

    SECTION( "THIEF_HONEST refuses a foreign item and takes an owned one" ) {
        const thief_guard guard( "THIEF_HONEST" );
        CHECK_FALSE( avatar_action::check_stealing( who, *foreign ) );
        CHECK( avatar_action::check_stealing( who, *mine ) );
    }
    SECTION( "THIEF_STEAL takes a foreign item" ) {
        const thief_guard guard( "THIEF_STEAL" );
        CHECK( avatar_action::check_stealing( who, *foreign ) );
    }
}

// CDF: P3-5-4 U9; DDA game_inventory.cpp:3144-3147 (the empty selector returns a null item and false).
// Popups return at once in the test build (popup.cpp:269), so the empty branch can be called.
TEST_CASE( "unload_menu_with_nothing_to_unload_returns_nothing", "[p3][unload][ui][5-4]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const bool saved = uistate.unload_auto_contain;

    // the Auto flag is not part of the result of an empty menu
    uistate.unload_auto_contain = true;
    const unload_selection chosen = game_menus::inv::unload( you );
    CHECK( chosen.to_unload == nullptr );
    CHECK_FALSE( chosen.auto_contain );
    uistate.unload_auto_contain = saved;
}

// CDF: P3-5-4 U5; DDA inventory_ui.cpp:382 and 481 (unload_auto_contain read and written by the
// uistate serialize). CDF uistatedata::serialize is at savegame_json.cpp:5206.
TEST_CASE( "unload_auto_contain_survives_save_and_load", "[p3][unload][ui][5-4]" )
{
    const auto round_trip = []( bool flag ) {
        uistatedata saved;
        saved.unload_auto_contain = flag;
        std::ostringstream out;
        JsonOut jsout( out );
        saved.serialize( jsout );

        std::istringstream in( out.str() );
        JsonIn jsin( in );
        JsonObject jo = jsin.get_object();
        uistatedata loaded;
        // the member starts at its default so that a missing key cannot pass for a read one
        loaded.unload_auto_contain = !flag;
        loaded.deserialize( jo );
        return loaded.unload_auto_contain;
    };

    // DDA: the flag defaults to false (the Manual destination)
    CHECK_FALSE( uistatedata().unload_auto_contain );
    CHECK( round_trip( true ) );
    CHECK_FALSE( round_trip( false ) );
}
