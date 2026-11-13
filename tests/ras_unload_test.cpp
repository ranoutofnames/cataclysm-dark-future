// CDF: P3-8-6c. DDA unloads a RELOAD_AND_SHOOT gun (bow, crossbow, slingshot) when the aim screen
// is left without firing, if the player's per-session choice says so (DDA activity_actor.cpp:600-607,
// 639-642, 661, 684, 749-763; ranged.cpp:344, 2859, 2907, 2994-2996, 3088-3109, 4270-4282;
// options.cpp:2007-2011). DDA has no test for any of it, so every case here is new.
// The aim screen (target_ui) has no harness and is not driven: the option, the key and the hint line
// are covered through the seams (option value, actor fields, ranged_text::ras_unload_hint) and the
// owner checklist. No case here opens a menu: the actor is called directly and reload_requested is
// never set (reload_wielded would open the ammo selector).
// Literals are read from the CDF item JSON: longbow has one MAGAZINE pocket { "arrow": 1 }
// (data/json/items/ranged/archery.json:704-709), glock_19 takes glockmag (15 rounds).
#include <memory>
#include <sstream>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "input.h"
#include "item.h"
#include "json.h"
#include "options.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ranged_text.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_arrow_wood( "arrow_wood" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_longbow( "longbow" );
static const itype_id itype_9mm( "9mm" );

namespace
{

// A longbow with `arrows` arrows in its MAGAZINE pocket, wielded by the avatar, who also wears a
// backpack so an unloaded arrow has somewhere to go. Returns the bow.
item &wield_bow( avatar &you, int arrows )
{
    you.wear_item( item::spawn( itype_backpack ), false );
    detached_ptr<item> bow = item::spawn( itype_longbow );
    if( arrows > 0 ) {
        REQUIRE( bow->put_in( item::spawn( itype_arrow_wood, calendar::turn, arrows ),
                              pocket_type::MAGAZINE ).success() );
    }
    REQUIRE( !you.wield( std::move( bow ) ) );
    item &wielded = you.primary_weapon();
    REQUIRE( wielded.typeId() == itype_longbow );
    REQUIRE( wielded.has_flag( flag_id( "RELOAD_AND_SHOOT" ) ) );
    REQUIRE( wielded.ammo_remaining() == arrows );
    return wielded;
}

// Arrows the avatar carries or wears that are not inside `gun`.
int arrows_outside( const avatar &you, const item &gun )
{
    int count = 0;
    for( const item *it : you.all_items_loc() ) {
        if( it->typeId() == itype_arrow_wood && it->parent_item() != &gun ) {
            count += it->count();
        }
    }
    return count;
}

} // namespace

TEST_CASE( "ras_unload_when_flag_set", "[ranged][ras]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &bow = wield_bow( you, 1 );

    aim_activity_actor actor;
    actor.should_unload_RAS = true;
    actor.unload_RAS_weapon();

    CHECK( bow.ammo_remaining() == 0 );
    CHECK( arrows_outside( you, bow ) == 1 );
}

TEST_CASE( "ras_keep_loaded_when_flag_clear", "[ranged][ras]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &bow = wield_bow( you, 1 );

    aim_activity_actor actor;
    actor.should_unload_RAS = false;
    actor.unload_RAS_weapon();

    CHECK( bow.ammo_remaining() == 1 );
    CHECK( arrows_outside( you, bow ) == 0 );
}

TEST_CASE( "ras_unload_empty_bow_does_nothing", "[ranged][ras]" )
{
    // DDA :757-761 unloads only `if( gun->ammo_remaining() )`: an empty bow produces nothing.
    clear_all_state();
    avatar &you = get_avatar();
    item &bow = wield_bow( you, 0 );

    aim_activity_actor actor;
    actor.should_unload_RAS = true;
    actor.unload_RAS_weapon();

    CHECK( bow.ammo_remaining() == 0 );
    CHECK( arrows_outside( you, bow ) == 0 );
}

TEST_CASE( "ras_unload_ignores_other_guns", "[ranged][ras]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_backpack ), false );
    detached_ptr<item> gun = item::spawn( itype_glock_19 );
    detached_ptr<item> mag = item::spawn( itype_glockmag );
    // CDF: glockmag pocket ammo_restriction { "9mm": 15 } (data/json/items/magazine/9mm.json:80-100).
    mag->put_in( item::spawn( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    REQUIRE( gun->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( !you.wield( std::move( gun ) ) );
    item &wielded = you.primary_weapon();
    REQUIRE( wielded.typeId() == itype_glock_19 );
    REQUIRE_FALSE( wielded.has_flag( flag_id( "RELOAD_AND_SHOOT" ) ) );
    REQUIRE( wielded.ammo_remaining() == 15 );

    aim_activity_actor actor;
    actor.should_unload_RAS = true;
    actor.unload_RAS_weapon();

    CHECK( wielded.ammo_remaining() == 15 );
}

TEST_CASE( "ras_should_unload_roundtrip", "[ranged][ras]" )
{
    SECTION( "the flag survives serialize and deserialize" ) {
        aim_activity_actor actor;
        actor.should_unload_RAS = true;
        std::ostringstream os;
        JsonOut jsout( os );
        actor.serialize( jsout );
        // The member replaced loaded_RAS_weapon (dead in CDF): the old key is gone, the new one is written.
        CHECK( os.str().find( "\"should_unload_RAS\":true" ) != std::string::npos );
        CHECK( os.str().find( "loaded_RAS_weapon" ) == std::string::npos );

        std::istringstream is( os.str() );
        JsonIn jsin( is );
        std::unique_ptr<activity_actor> back = aim_activity_actor::deserialize( jsin );
        REQUIRE( back != nullptr );
        const aim_activity_actor *read = dynamic_cast<const aim_activity_actor *>( back.get() );
        REQUIRE( read != nullptr );
        CHECK( read->should_unload_RAS );
    }
    SECTION( "an actor without the key reads false" ) {
        std::istringstream is( "{}" );
        JsonIn jsin( is );
        std::unique_ptr<activity_actor> back = aim_activity_actor::deserialize( jsin );
        REQUIRE( back != nullptr );
        const aim_activity_actor *read = dynamic_cast<const aim_activity_actor *>( back.get() );
        REQUIRE( read != nullptr );
        CHECK_FALSE( read->should_unload_RAS );
    }
}

TEST_CASE( "ras_unload_hint_text", "[ranged][ras]" )
{
    // DDA ranged.cpp:4270-4282: the line tells what the key will do next, so with unloading off
    // it offers "unload", with unloading on it offers "keep".
    CHECK( ranged_text::ras_unload_hint( false, "-", "bow" ) == "[-] to unload the bow after quitting." );
    CHECK( ranged_text::ras_unload_hint( true, "-", "bow" ) ==
           "[-] to keep the bow loaded after quitting." );
    CHECK( ranged_text::ras_unload_hint( true, "x", "longbow" ) ==
           "[x] to keep the longbow loaded after quitting." );
}

TEST_CASE( "ras_unload_option_default", "[ranged][ras][option]" )
{
    // DDA options.cpp:2007-2011: default true.
    CHECK( get_options().has_option( "UNLOAD_RAS_WEAPON" ) );
    CHECK( get_option<bool>( "UNLOAD_RAS_WEAPON" ) );
}

TEST_CASE( "ras_unload_when_aim_finishes_aborted", "[ranged][ras]" )
{
    // DDA :600-606: the aborted branch of finish unloads before the optional reload. reload_requested
    // stays false here: reload_wielded would open the ammo menu.
    clear_all_state();
    avatar &you = get_avatar();

    SECTION( "flag set: the arrow comes out" ) {
        item &bow = wield_bow( you, 1 );
        aim_activity_actor actor;
        actor.aborted = true;
        actor.should_unload_RAS = true;
        player_activity act;
        actor.finish( act, you );
        CHECK( bow.ammo_remaining() == 0 );
        CHECK( arrows_outside( you, bow ) == 1 );
    }
    SECTION( "flag clear: the arrow stays nocked" ) {
        item &bow = wield_bow( you, 1 );
        aim_activity_actor actor;
        actor.aborted = true;
        actor.should_unload_RAS = false;
        player_activity act;
        actor.finish( act, you );
        CHECK( bow.ammo_remaining() == 1 );
        CHECK( arrows_outside( you, bow ) == 0 );
    }
}

TEST_CASE( "ras_unload_when_aim_canceled", "[ranged][ras]" )
{
    // DDA :639-642: canceled unloads too (an interrupted aim).
    clear_all_state();
    avatar &you = get_avatar();

    SECTION( "flag set: the arrow comes out" ) {
        item &bow = wield_bow( you, 1 );
        aim_activity_actor actor;
        actor.should_unload_RAS = true;
        player_activity act;
        actor.canceled( act, you );
        CHECK( bow.ammo_remaining() == 0 );
        CHECK( arrows_outside( you, bow ) == 1 );
    }
    SECTION( "flag clear: the arrow stays nocked" ) {
        item &bow = wield_bow( you, 1 );
        aim_activity_actor actor;
        actor.should_unload_RAS = false;
        player_activity act;
        actor.canceled( act, you );
        CHECK( bow.ammo_remaining() == 1 );
        CHECK( arrows_outside( you, bow ) == 0 );
    }
}

TEST_CASE( "ras_unload_key_is_bound_in_the_target_context", "[ranged][ras]" )
{
    // DDA data/raw/keybindings.json:1718-1724: `-` in category TARGET. CDF binds the keyboard only and
    // no other aim-screen action may share the key.
    inp_mngr.init();
    input_context ctxt( "TARGET" );
    ctxt.register_action( "TOGGLE_UNLOAD_RAS_WEAPON" );
    const std::vector<char> keys = ctxt.keys_bound_to( "TOGGLE_UNLOAD_RAS_WEAPON" );
    REQUIRE( keys.size() == 1 );
    CHECK( keys.front() == '-' );

    for( const std::string &other : { "FIRE", "NEXT_TARGET", "PREV_TARGET", "CENTER", "TOGGLE_SNAP_TO_TARGET",
                                      "SWITCH_AMMO", "SWITCH_MODE", "AIM", "TOGGLE_MOVE_CURSOR_VIEW", "TOGGLE_TURRET_LINES"
                                    } ) {
        ctxt.register_action( other );
        const std::vector<char> other_keys = ctxt.keys_bound_to( other );
        CHECK( std::find( other_keys.begin(), other_keys.end(), '-' ) == other_keys.end() );
    }
}
