#include "avatar.h"
#include "calendar.h"
#include "carried_stacks.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "game_inventory.h"
#include "input.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "ranged.h"
#include "reload/reload.h"
#include "safe_reference.h"
#include "state_helpers.h"
#include "type_id.h"
#include "vehicle/vehicle.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// CDF: P3-5-3a (5-2c and the select_ammo core of 5-3); CDF-only. DDA has no test of the ammo
// selector, its count keys, select_ammo_inventory_preset or the destination of select_ammo (a grep
// of DDA tests/ at b4b62254ae2 for ammo_inventory_selector, select_ammo_inventory_preset,
// INCREASE_COUNT and DECREASE_COUNT is empty); its two callers of select_ammo in tests
// (reloading_test.cpp:853, reload_time_test.cpp:56) go through Character::select_ammo, which CDF
// does not have. execute() needs keyboard input and is not driven here (owner checklist row 36).
// item_location becomes item * (D1). Expected numbers come from the JSON capacities (glockmag 15,
// stanag30 30), from item_reload_option::qty (a well target swaps one magazine: 1; a magazine
// target takes the room it has) and from the INVENTORY entries of keybindings.json, not from the
// code under test.
// ---------------------------------------------------------------------------

static const itype_id itype_38_special( "38_special" );
static const itype_id itype_556( "556" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_test_multimag_gun( "test_multimag_gun" );

namespace
{

auto loaded_mag( const itype_id &mag_id, const itype_id &ammo_id, int count ) -> detached_ptr<item>
{
    detached_ptr<item> mag = item::spawn( mag_id );
    mag->put_in( item::spawn( ammo_id, calendar::turn, count ), pocket_type::MAGAZINE );
    return mag;
}

// A test_multimag_gun with a glockmag of the given rounds already in its first well; `mag` gets the
// magazine (it stays valid when the gun is moved into the inventory).
auto gun_with_glockmag( int rounds, item *&mag ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_test_multimag_gun );
    detached_ptr<item> loaded = loaded_mag( itype_glockmag, itype_9mm, rounds );
    mag = loaded.get();
    gun->put_in( std::move( loaded ), pocket_type::MAGAZINE_WELL );
    return gun;
}

// The position of the first MAGAZINE_WELL of the host in get_pockets order, or -1. For
// test_multimag_gun it is the glockmag well (TEST_DATA items.json lists it first).
auto first_well_index( item &host ) -> int
{
    int idx = 0;
    for( const item_pocket *p : host.get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            return idx;
        }
        ++idx;
    }
    return -1;
}

// CDF: P3-NESTAMMO1. The preset of the select_ammo selector reduced to its is_shown (the real preset
// is private to game_inventory.cpp): game_menus::inv::select_ammo_shows is what it asks.
class select_ammo_shows_preset : public inventory_selector_preset
{
    public:
        select_ammo_shows_preset( item &host, bool empty ) : host( host ), empty( empty ) {}

        bool is_shown( const item *loc ) const override {
            return game_menus::inv::select_ammo_shows( host, *loc, empty );
        }

    private:
        item &host;
        bool empty;
};

// CDF: ctxt is protected; these read the actions a selector registered. P3-NESTAMMO1: and the rows.
class ammo_selector_actions_probe : public ammo_inventory_selector
{
    public:
        using ammo_inventory_selector::ammo_inventory_selector;

        auto actions() const -> std::vector<std::string> {
            return ctxt.get_registered_actions_copy();
        }

        // The item of every row of the three columns, in column order.
        auto listed_items() const -> std::vector<const item *> {
            std::vector<const item *> rows;
            for( const inventory_column *col : { &own_gear_column, &own_inv_column, &map_column } ) {
                for( const inventory_entry *entry : col->get_all_entries() ) {
                    if( entry->is_item() ) {
                        rows.push_back( entry->any_item() );
                    }
                }
            }
            return rows;
        }

        // CDF: P3-NESTAMMO2. The category of the row of the item (nullptr when it has no row).
        auto row_category( const item *it ) const -> const item_category * {
            for( const inventory_column *col : { &own_gear_column, &own_inv_column, &map_column } ) {
                for( const inventory_entry *entry : col->get_all_entries() ) {
                    if( entry->is_item() && entry->any_item() == it ) {
                        return entry->get_category_ptr();
                    }
                }
            }
            return nullptr;
        }
};

// How many rows the select_ammo selector lists for the host, from the carried items (the map is not
// added; these tests keep the floor empty or say so). It is the list select_ammo builds with
// add_character_items: the wielded and worn items, the top level of the carry pockets and, as DDA's
// add_entry_rec does, what sits nested in a carried container (P3-NESTAMMO1). select_ammo with prompt
// off reaches the interactive execute() unless exactly one entry is listed, and execute() spins
// forever headless, so the select_ammo sections REQUIRE this count before the call.
auto listed_candidates( avatar &you, item &host ) -> size_t
{
    const select_ammo_shows_preset preset( host, true );
    ammo_selector_actions_probe sel( you, host, preset );
    sel.clear_items();
    sel.add_character_items( you );
    return sel.item_entry_count();
}

// CDF: P3-NESTAMMO2. The rows of the list select_ammo builds: the carried items and, as
// add_nearby_items( 1 ) adds them, the map squares around the avatar and the vehicle cargo on them.
// Never opens a selector (see listed_candidates).
auto listed_with_map( avatar &you, item &host ) -> size_t
{
    const select_ammo_shows_preset preset( host, true );
    ammo_selector_actions_probe sel( you, host, preset );
    sel.clear_items();
    sel.add_character_items( you );
    sel.add_nearby_items( 1 );
    return sel.item_entry_count();
}

class plain_selector_probe : public inventory_pick_selector
{
    public:
        using inventory_pick_selector::inventory_pick_selector;

        auto actions() const -> std::vector<std::string> {
            return ctxt.get_registered_actions_copy();
        }
};

auto has_action( const std::vector<std::string> &actions, const std::string &id ) -> bool
{
    return std::ranges::find( actions, id ) != actions.end();
}

auto has_key( const std::vector<char> &keys, char key ) -> bool
{
    return std::ranges::find( keys, key ) != keys.end();
}

} // namespace

TEST_CASE( "ammo_selector_registers_the_count_keys", "[p3][ui][5-2c]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    detached_ptr<item> gun = item::spawn( itype_test_multimag_gun );

    const ammo_selector_actions_probe ammo_sel( you, *gun );
    const plain_selector_probe plain_sel( you );

    // DDA inventory_ui.cpp 4022-4023: the ammo selector registers both ids.
    CHECK( has_action( ammo_sel.actions(), "INCREASE_COUNT" ) );
    CHECK( has_action( ammo_sel.actions(), "DECREASE_COUNT" ) );
    // ADV-14 ruling 5: no other selector has the count keys.
    CHECK_FALSE( has_action( plain_sel.actions(), "INCREASE_COUNT" ) );
    CHECK_FALSE( has_action( plain_sel.actions(), "DECREASE_COUNT" ) );
}

TEST_CASE( "ammo_selector_count_keys_are_bound_in_the_inventory_context", "[p3][ui][5-2c]" )
{
    inp_mngr.init();
    input_context ctxt( "INVENTORY" );
    ctxt.register_action( "INCREASE_COUNT" );
    ctxt.register_action( "DECREASE_COUNT" );

    // DDA keybindings.json 4424-4460 binds = and + (increase) and - (decrease) on the keyboard; the
    // keypad and gamepad bindings have no CDF form (ADV-17 Q-16-7).
    const std::vector<char> increase = ctxt.keys_bound_to( "INCREASE_COUNT" );
    const std::vector<char> decrease = ctxt.keys_bound_to( "DECREASE_COUNT" );
    CHECK( increase.size() == 2 );
    CHECK( has_key( increase, '=' ) );
    CHECK( has_key( increase, '+' ) );
    CHECK( decrease.size() == 1 );
    CHECK( has_key( decrease, '-' ) );
}

TEST_CASE( "ammo_selector_count_keys_do_not_collide_with_other_inventory_keys", "[p3][ui][5-2c]" )
{
    inp_mngr.init();
    // The 24 INVENTORY entries of data/raw/keybindings/keybindings.json before P3-5-3a (read with a
    // JSON parse at 51961b2041), and the two new ids.
    const std::vector<std::string> ids = {
        "ORGANIZE_MENU", "SHOW_HIDE_CONTENTS", "VIEW_CATEGORY_MODE", "EXAMINE", "CATEGORY_SELECTION",
        "INVENTORY_FILTER", "TOGGLE_FAVORITE", "WIELD", "WEAR", "DROP_NON_FAVORITE", "FAV_PRIORITY",
        "FAV_AUTO_PICKUP", "FAV_AUTO_UNLOAD", "FAV_ITEM", "FAV_CATEGORY", "FAV_WHITELIST",
        "FAV_WHITELIST_CONTENTS", "FAV_BLACKLIST", "FAV_SAVE_PRESET", "FAV_APPLY_PRESET",
        "FAV_DEL_PRESET", "FAV_CLEAR", "FAV_MOVE_ITEM", "FAV_CONTEXT_MENU",
        "INCREASE_COUNT", "DECREASE_COUNT"
    };
    input_context ctxt( "INVENTORY" );
    for( const std::string &id : ids ) {
        ctxt.register_action( id );
    }

    const auto actions_on_key = [&ctxt, &ids]( char key ) {
        int count = 0;
        for( const std::string &id : ids ) {
            if( has_key( ctxt.keys_bound_to( id ), key ) ) {
                ++count;
            }
        }
        return count;
    };
    // Exactly one action of the context uses each count key: the new one.
    CHECK( actions_on_key( '=' ) == 1 );
    CHECK( actions_on_key( '+' ) == 1 );
    CHECK( actions_on_key( '-' ) == 1 );
}

TEST_CASE( "inventory_selector_item_entry_count_counts_item_entries", "[p3][ui][5-3a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    plain_selector_probe sel( you );

    SECTION( "an empty selector has no item entries" ) {
        CHECK( sel.item_entry_count() == 0 );
    }

    SECTION( "every item entry counts once, in any column" ) {
        detached_ptr<item> nine = item::spawn( itype_9mm, calendar::turn, 30 );
        detached_ptr<item> mag = loaded_mag( itype_glockmag, itype_9mm, 15 );
        sel.add_item( sel.own_inv_column, nine.get() );
        sel.add_item( sel.map_column, mag.get() );
        // DDA inventory_ui.cpp 3517-3525 counts item entries only, never category rows.
        CHECK( sel.item_entry_count() == 2 );
    }
}

TEST_CASE( "select_ammo_shows_follows_the_reload_targets_of_the_host", "[p3][ui][5-3a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    SECTION( "a magazine that fits an empty well is shown" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        item &glock = you.i_add( loaded_mag( itype_glockmag, itype_9mm, 15 ) );
        item &stanag = you.i_add( loaded_mag( itype_stanag30, itype_556, 30 ) );
        CHECK( game_menus::inv::select_ammo_shows( gun, glock, true ) );
        CHECK( game_menus::inv::select_ammo_shows( gun, stanag, true ) );
    }

    SECTION( "ammo that no target of the host accepts is hidden" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        item &special = you.i_add( item::spawn( itype_38_special, calendar::turn, 7 ) );
        CHECK_FALSE( game_menus::inv::select_ammo_shows( gun, special, true ) );
    }

    SECTION( "a magazine already in the host is hidden" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 15, in_gun ) );
        REQUIRE( in_gun->parent_item() == &gun );
        // DDA 3230: the items of the host itself are never offered to it.
        CHECK_FALSE( game_menus::inv::select_ammo_shows( gun, *in_gun, true ) );
    }

    SECTION( "empty magazines are shown only when the empty flag is set" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        item &empty_mag = you.i_add( item::spawn( itype_glockmag ) );
        CHECK( game_menus::inv::select_ammo_shows( gun, empty_mag, true ) );
        CHECK_FALSE( game_menus::inv::select_ammo_shows( gun, empty_mag, false ) );
    }

    SECTION( "loose ammo is shown only while the loaded magazine has room" ) {
        item *roomy_mag = nullptr;
        item *full_mag = nullptr;
        // glockmag capacity 15 (9mm.json): 5 loaded leaves room, 15 loaded leaves none.
        item &roomy = you.i_add( gun_with_glockmag( 5, roomy_mag ) );
        item &full = you.i_add( gun_with_glockmag( 15, full_mag ) );
        item &nine = you.i_add( item::spawn( itype_9mm, calendar::turn, 30 ) );
        CHECK( game_menus::inv::select_ammo_shows( roomy, nine, true ) );
        CHECK_FALSE( game_menus::inv::select_ammo_shows( full, nine, true ) );
    }

    // [UNSURE] needs a liquid placed on the floor of the avatar's tile and a bare floor without
    // LIQUIDCONT. If this section is red or its REQUIREs fail while the other sections of this case
    // are green, delete this section only, with a log line in the report and a Blocked line; do not
    // change the assertions.
    SECTION( "liquid on the ground is hidden" ) {
        item &bottle = you.i_add( item::spawn( itype_id( "bottle_plastic" ) ) );
        detached_ptr<item> water_ptr = item::spawn( itype_id( "water" ), calendar::turn, 1 );
        item *water = water_ptr.get();
        get_map().add_item( you.bub_pos(), std::move( water_ptr ) );
        REQUIRE( water->where() == item_location_type::map );
        // The premise: the bottle would take the water, so only the ground rule can hide it.
        REQUIRE( find_matching_reload_target( get_possible_reload_targets( bottle ), *water ) !=
                 nullptr );
        // DDA 3234-3238: a liquid on a map square without LIQUIDCONT is not offered.
        CHECK_FALSE( game_menus::inv::select_ammo_shows( bottle, *water, true ) );
    }
}

TEST_CASE( "select_ammo_returns_the_chosen_source_without_a_menu", "[p3][ui][5-3a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    SECTION( "a magazine for an empty well is the only choice and goes to that well" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        item &glock = you.i_add( loaded_mag( itype_glockmag, itype_9mm, 15 ) );
        const int glock_well = first_well_index( gun );
        REQUIRE( glock_well >= 0 );
        REQUIRE( listed_candidates( you, gun ) == 1 );

        const item_reload_option opt = game_menus::inv::select_ammo( you, gun );
        REQUIRE( opt );
        CHECK( opt.target == &gun );
        CHECK( opt.ammo == &glock );
        CHECK( opt.pocket_index == glock_well );
        // A well-targeted reload swaps one magazine (item_reload_option::qty, pocket_index >= 0).
        CHECK( opt.qty() == 1 );
    }

    SECTION( "loose ammo tops up the magazine in the gun to its room" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &nine = you.i_add( item::spawn( itype_9mm, calendar::turn, 30 ) );
        REQUIRE( in_gun->parent_item() == &gun );
        REQUIRE( listed_candidates( you, gun ) == 1 );

        const item_reload_option opt = game_menus::inv::select_ammo( you, gun );
        REQUIRE( opt );
        CHECK( opt.target == in_gun );
        CHECK( opt.ammo == &nine );
        CHECK( opt.pocket_index == item_reload_option::POCKET_FALLBACK );
        // glockmag capacity 15 (9mm.json) minus 5 loaded: the amount is the room, not the 30 held.
        CHECK( opt.qty() == 10 );
    }

    // [UNSURE] relies on popup() returning at once in test mode (query_popup::query_once returns
    // for test_mode). If this section is red while the other section of this case is green,
    // delete this section only, with a log line in the report and a Blocked line.
    SECTION( "nothing to reload gives an empty option" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        REQUIRE( listed_candidates( you, gun ) == 0 );

        const item_reload_option opt = game_menus::inv::select_ammo( you, gun );
        CHECK_FALSE( opt );
    }
}

// ---------------------------------------------------------------------------
// CDF: P3-NESTAMMO1; CDF-only (DDA has no test of the ammo selector list). DDA's selector walks the whole
// carried tree (inventory_ui.cpp:2017-2047 add_entry_rec, 2109-2141 add_contained_items), so a liquid in a
// carried jug and ammo in a box in a bag are listed when the preset shows them; the CDF selector listed
// only the top level of the carry pockets. The tests go through the public seam of the selector
// (add_character_items, item_entry_count) and never open one: select_ammo is called only with prompt off
// and exactly one row listed (REQUIREd before the call), the guard of every select_ammo test in this file.
// Expected rows come from the JSON (jug_plastic and 2lcanteen are watertight, water_clean is a liquid,
// box_small and box_medium take loose ammo, glockmag holds 15 9mm) and from the DDA rules above.
// ---------------------------------------------------------------------------

TEST_CASE( "ammo_selector_lists_what_is_nested_in_a_carried_container", "[p3][ui][nestammo1]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "bigback" ) ), false );

    SECTION( "a liquid in a carried jug is listed, and the option carries the jug" ) {
        item &canteen = you.i_add( item::spawn( itype_id( "2lcanteen" ) ) );
        item &jug = you.i_add( item::spawn( itype_id( "jug_plastic" ) ) );
        REQUIRE( jug.put_in( item::spawn( itype_id( "water_clean" ), calendar::turn_zero, 2 ),
                             pocket_type::CONTAINER ).success() );
        item &water = jug.contents.front();
        // The premise: the canteen takes the water but not the jug, so only a walk into the jug finds it.
        REQUIRE( game_menus::inv::select_ammo_shows( canteen, water, true ) );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( canteen, jug, true ) );

        const select_ammo_shows_preset preset( canteen, true );
        ammo_selector_actions_probe sel( you, canteen, preset );
        sel.clear_items();
        sel.add_character_items( you );
        CHECK( sel.item_entry_count() == 1 );
        CHECK( sel.listed_items() == std::vector<const item *>{ &water } );

        // CDF convention (reload.cpp discover_ammo, character_functions.cpp:1083-1087): a reload option
        // carries the container of a contained liquid. prompt off and one row: no menu.
        REQUIRE( listed_candidates( you, canteen ) == 1 );
        const item_reload_option opt = game_menus::inv::select_ammo( you, canteen );
        REQUIRE( opt );
        CHECK( opt.target == &canteen );
        CHECK( opt.ammo == &jug );
    }

    SECTION( "a liquid in a carried jug that the target cannot take is not listed" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        item &jug = you.i_add( item::spawn( itype_id( "jug_plastic" ) ) );
        REQUIRE( jug.put_in( item::spawn( itype_id( "water_clean" ), calendar::turn_zero, 2 ),
                             pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( gun, jug.contents.front(), true ) );
        CHECK( listed_candidates( you, gun ) == 0 );
    }

    SECTION( "ammo in a box in a carried bag is listed, and the option is that ammo" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &box = you.i_add( item::spawn( itype_id( "box_small" ) ) );
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        item &nine = box.contents.front();
        REQUIRE( game_menus::inv::select_ammo_shows( gun, nine, true ) );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( gun, box, true ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_character_items( you );
        CHECK( sel.item_entry_count() == 1 );
        CHECK( sel.listed_items() == std::vector<const item *>{ &nine } );

        REQUIRE( listed_candidates( you, gun ) == 1 );
        const item_reload_option opt = game_menus::inv::select_ammo( you, gun );
        REQUIRE( opt );
        CHECK( opt.target == in_gun );
        CHECK( opt.ammo == &nine );
        // glockmag capacity 15 minus 5 loaded: the room, not the 30 in the box.
        CHECK( opt.qty() == 10 );
    }

    SECTION( "ammo two containers deep is listed" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &outer = you.i_add( item::spawn( itype_id( "box_medium" ) ) );
        detached_ptr<item> inner_ptr = item::spawn( itype_id( "box_small" ) );
        REQUIRE( inner_ptr->put_in( item::spawn( itype_9mm, calendar::turn, 30 ),
                                    pocket_type::CONTAINER ).success() );
        item &inner = *inner_ptr;
        REQUIRE( outer.put_in( std::move( inner_ptr ), pocket_type::CONTAINER ).success() );
        item &nine = inner.contents.front();
        REQUIRE( nine.parent_item() == &inner );
        REQUIRE( inner.parent_item() == &outer );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_character_items( you );
        CHECK( sel.listed_items() == std::vector<const item *>{ &nine } );
    }

    SECTION( "an item listed at the top level of the carry pockets is not listed twice" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &loose = you.i_add( item::spawn( itype_9mm, calendar::turn, 12 ) );
        item &box = you.i_add( item::spawn( itype_id( "box_small" ) ) );
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        item &nested = box.contents.front();

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_character_items( you );
        std::vector<const item *> rows = sel.listed_items();
        CHECK( rows.size() == 2 );
        CHECK( std::ranges::count( rows, &loose ) == 1 );
        CHECK( std::ranges::count( rows, &nested ) == 1 );
    }

    SECTION( "every container of a stack of identical containers is opened" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &first = you.i_add( item::spawn( itype_id( "box_small" ) ) );
        item &second = you.i_add( item::spawn( itype_id( "box_small" ) ) );
        REQUIRE( first.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        REQUIRE( second.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        // The carried stacks group the two boxes (one stack of two), the rounds are two items.
        REQUIRE( first.stacks_with( second ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_character_items( you );
        const std::vector<const item *> rows = sel.listed_items();
        CHECK( rows.size() == 2 );
        CHECK( std::ranges::count( rows, &first.contents.front() ) == 1 );
        CHECK( std::ranges::count( rows, &second.contents.front() ) == 1 );
    }

    SECTION( "what is not ammo for the target stays unlisted" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &box = you.i_add( item::spawn( itype_id( "box_small" ) ) );
        REQUIRE( box.put_in( item::spawn( itype_38_special, calendar::turn, 7 ),
                             pocket_type::CONTAINER ).success() );
        REQUIRE( box.put_in( item::spawn( itype_id( "pebble" ), calendar::turn, 3 ),
                             pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( gun, box.contents.front(), true ) );
        CHECK( listed_candidates( you, gun ) == 0 );
    }

    SECTION( "the ammo inside the magazine of the host gun is not offered to it" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        // The rounds in the host's own magazine would be accepted by that magazine (top up with itself).
        item &rounds = in_gun->contents.front();
        REQUIRE( rounds.parent_item() == in_gun );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, rounds, true ) );
        CHECK( listed_candidates( you, gun ) == 0 );
    }

    SECTION( "the magazine and the ammo loaded in another gun are not taken from it" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item *other_mag = nullptr;
        item &other = you.i_add( gun_with_glockmag( 10, other_mag ) );
        REQUIRE( other_mag->parent_item() == &other );
        // CDF discovery never steals either (character_functions.cpp:1113-1120); both would be shown.
        REQUIRE( game_menus::inv::select_ammo_shows( gun, *other_mag, true ) );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, other_mag->contents.front(), true ) );
        CHECK( listed_candidates( you, gun ) == 0 );
    }

    SECTION( "the rounds in a loose magazine are not taken out of it" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> spare_ptr = loaded_mag( itype_glockmag, itype_9mm, 10 );
        item &spare = you.i_add( std::move( spare_ptr ) );
        item &rounds = spare.contents.front();
        REQUIRE( rounds.parent_item() == &spare );
        // Both would be shown: the magazine swaps in, the rounds top up the loaded one.
        REQUIRE( game_menus::inv::select_ammo_shows( gun, spare, true ) );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, rounds, true ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_character_items( you );
        // CDF discovery never takes ammo out of a magazine (character_functions.cpp:1113): the magazine only.
        CHECK( sel.listed_items() == std::vector<const item *>{ &spare } );
    }

    SECTION( "a NO_UNLOAD container is not opened" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &box = you.i_add( item::spawn( itype_id( "box_small" ) ) );
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        REQUIRE( listed_candidates( you, gun ) == 1 );
        box.set_flag( flag_id( "NO_UNLOAD" ) );
        // DDA inventory_ui.cpp:2113 add_contained_items.
        CHECK( listed_candidates( you, gun ) == 0 );
    }

    SECTION( "only the ammo selector walks into carried containers" ) {
        item &canteen = you.i_add( item::spawn( itype_id( "2lcanteen" ) ) );
        item &jug = you.i_add( item::spawn( itype_id( "jug_plastic" ) ) );
        REQUIRE( jug.put_in( item::spawn( itype_id( "water_clean" ), calendar::turn_zero, 2 ),
                             pocket_type::CONTAINER ).success() );
        REQUIRE( listed_candidates( you, canteen ) == 1 );

        // The same preset on any other selector: drop, pick-up and the rest keep their top-level list.
        const select_ammo_shows_preset preset( canteen, true );
        plain_selector_probe plain( you, preset );
        plain.clear_items();
        plain.add_character_items( you );
        CHECK( plain.item_entry_count() == 0 );
    }
}

// ---------------------------------------------------------------------------
// CDF: P3-NESTAMMO2; CDF-only (DDA has no test of the ammo selector list). The map half of NESTAMMO1: DDA's
// _add_map_items runs add_entry_rec on every item of a map square and of vehicle cargo (inventory_ui.cpp:
// 2221-2262, 2337-2358), so ammo in a box on the floor next to the avatar, a liquid in a jug on the floor and
// ammo in a box in vehicle cargo are listed when the preset shows them. The CDF add_items listed only the
// top level of the stack. Same rules as the carried walk of NESTAMMO1 (no magazine opened, a gun's loaded
// magazine not offered, NO_UNLOAD not opened, ammo selector only). Rows go through the public seam of the
// selector (add_nearby_items / add_map_items / add_vehicle_items, item_entry_count, listed_items) and no test
// opens a selector: select_ammo is only called with prompt off and exactly one row listed (REQUIREd first).
// Expected rows come from the JSON (box_small and box_medium take loose ammo, jug_plastic and 2lcanteen are
// watertight, water_clean is a liquid, glockmag holds 15 9mm) and from the DDA rules above.
// ---------------------------------------------------------------------------

TEST_CASE( "ammo_selector_lists_what_is_nested_in_containers_on_the_map", "[p3][ui][nestammo2]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "bigback" ) ), false );
    const tripoint_bub_ms next_to_you = you.bub_pos() + tripoint_east;
    REQUIRE( get_map().i_at( next_to_you ).empty() );

    SECTION( "ammo in a box on an adjacent tile is listed, and the option is that ammo" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        item &nine = box.contents.front();
        get_map().add_item( next_to_you, std::move( box_ptr ) );
        REQUIRE( nine.parent_item() == &box );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, nine, true ) );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( gun, box, true ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_nearby_items( 1 );
        CHECK( sel.item_entry_count() == 1 );
        CHECK( sel.listed_items() == std::vector<const item *>{ &nine } );

        REQUIRE( listed_with_map( you, gun ) == 1 );
        const item_reload_option opt = game_menus::inv::select_ammo( you, gun );
        REQUIRE( opt );
        CHECK( opt.target == in_gun );
        CHECK( opt.ammo == &nine );
        // glockmag capacity 15 minus 5 loaded: the room, not the 30 in the box.
        CHECK( opt.qty() == 10 );
    }

    SECTION( "ammo in a box on the avatar's own tile is listed" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        get_map().add_item( you.bub_pos(), std::move( box_ptr ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_map_items( you.bub_pos() );
        CHECK( sel.listed_items() == std::vector<const item *>{ &box.contents.front() } );
    }

    SECTION( "a liquid in a jug on the ground is listed, and the option carries the jug" ) {
        item &canteen = you.i_add( item::spawn( itype_id( "2lcanteen" ) ) );
        detached_ptr<item> jug_ptr = item::spawn( itype_id( "jug_plastic" ) );
        item &jug = *jug_ptr;
        REQUIRE( jug.put_in( item::spawn( itype_id( "water_clean" ), calendar::turn_zero, 2 ),
                             pocket_type::CONTAINER ).success() );
        item &water = jug.contents.front();
        get_map().add_item( next_to_you, std::move( jug_ptr ) );
        // The premise: the water sits in the jug, not loose on the floor, so the ground-liquid rule
        // (DDA game_inventory.cpp:3234-3238) does not hide it; the jug itself is not ammo.
        REQUIRE( water.where() == item_location_type::container );
        REQUIRE( game_menus::inv::select_ammo_shows( canteen, water, true ) );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( canteen, jug, true ) );

        const select_ammo_shows_preset preset( canteen, true );
        ammo_selector_actions_probe sel( you, canteen, preset );
        sel.clear_items();
        sel.add_nearby_items( 1 );
        CHECK( sel.item_entry_count() == 1 );
        CHECK( sel.listed_items() == std::vector<const item *>{ &water } );

        // CDF convention (character_functions.cpp:1083-1087): the option carries the container of a liquid.
        REQUIRE( listed_with_map( you, canteen ) == 1 );
        const item_reload_option opt = game_menus::inv::select_ammo( you, canteen );
        REQUIRE( opt );
        CHECK( opt.target == &canteen );
        CHECK( opt.ammo == &jug );
    }

    SECTION( "water loose on the ground stays hidden" ) {
        item &canteen = you.i_add( item::spawn( itype_id( "2lcanteen" ) ) );
        detached_ptr<item> water_ptr = item::spawn( itype_id( "water_clean" ), calendar::turn_zero, 2 );
        item &water = *water_ptr;
        get_map().add_item( next_to_you, std::move( water_ptr ) );
        REQUIRE( water.where() == item_location_type::map );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( canteen, water, true ) );
        CHECK( listed_with_map( you, canteen ) == 0 );
    }

    SECTION( "ammo two containers deep on the ground is listed" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> outer_ptr = item::spawn( itype_id( "box_medium" ) );
        item &outer = *outer_ptr;
        detached_ptr<item> inner_ptr = item::spawn( itype_id( "box_small" ) );
        item &inner = *inner_ptr;
        REQUIRE( inner.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        REQUIRE( outer.put_in( std::move( inner_ptr ), pocket_type::CONTAINER ).success() );
        item &nine = inner.contents.front();
        get_map().add_item( next_to_you, std::move( outer_ptr ) );
        REQUIRE( nine.parent_item() == &inner );
        REQUIRE( inner.parent_item() == &outer );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_nearby_items( 1 );
        CHECK( sel.listed_items() == std::vector<const item *>{ &nine } );
    }

    SECTION( "every box of a stack of identical boxes on the ground is opened" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> first_ptr = item::spawn( itype_id( "box_small" ) );
        detached_ptr<item> second_ptr = item::spawn( itype_id( "box_small" ) );
        item &first = *first_ptr;
        item &second = *second_ptr;
        REQUIRE( first.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        REQUIRE( second.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        REQUIRE( first.stacks_with( second ) );
        get_map().add_item( next_to_you, std::move( first_ptr ) );
        get_map().add_item( next_to_you, std::move( second_ptr ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_nearby_items( 1 );
        const std::vector<const item *> rows = sel.listed_items();
        CHECK( rows.size() == 2 );
        CHECK( std::ranges::count( rows, &first.contents.front() ) == 1 );
        CHECK( std::ranges::count( rows, &second.contents.front() ) == 1 );
    }

    SECTION( "loose ammo on the ground and the same ammo in a box are two rows under one header" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> loose_ptr = item::spawn( itype_9mm, calendar::turn, 12 );
        item &loose = *loose_ptr;
        get_map().add_item( next_to_you, std::move( loose_ptr ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        item &nested = box.contents.front();
        get_map().add_item( next_to_you, std::move( box_ptr ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_nearby_items( 1 );
        const std::vector<const item *> rows = sel.listed_items();
        CHECK( rows.size() == 2 );
        CHECK( std::ranges::count( rows, &loose ) == 1 );
        CHECK( std::ranges::count( rows, &nested ) == 1 );
        // The nested row sits under the same square header as the loose row (not under 9mm's own category).
        REQUIRE( sel.row_category( &loose ) != nullptr );
        REQUIRE( sel.row_category( &nested ) != nullptr );
        CHECK( sel.row_category( &nested )->get_id() == sel.row_category( &loose )->get_id() );
    }

    SECTION( "carried and ground containers are both walked" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item &carried_box = you.i_add( item::spawn( itype_id( "box_small" ) ) );
        REQUIRE( carried_box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ),
                                     pocket_type::CONTAINER ).success() );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &ground_box = *box_ptr;
        REQUIRE( ground_box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ),
                                    pocket_type::CONTAINER ).success() );
        get_map().add_item( next_to_you, std::move( box_ptr ) );
        CHECK( listed_with_map( you, gun ) == 2 );
    }

    SECTION( "what is not ammo for the target stays unlisted" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_38_special, calendar::turn, 7 ),
                             pocket_type::CONTAINER ).success() );
        REQUIRE( box.put_in( item::spawn( itype_id( "pebble" ), calendar::turn, 3 ),
                             pocket_type::CONTAINER ).success() );
        get_map().add_item( next_to_you, std::move( box_ptr ) );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( gun, box.contents.front(), true ) );
        CHECK( listed_with_map( you, gun ) == 0 );
    }

    SECTION( "a box out of reach is not opened" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        get_map().add_item( you.bub_pos() + tripoint( 2, 0, 0 ), std::move( box_ptr ) );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, box.contents.front(), true ) );
        CHECK( listed_with_map( you, gun ) == 0 );
    }

    SECTION( "the magazine loaded in a gun on the ground and its ammo are not taken from it" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        item *other_mag = nullptr;
        detached_ptr<item> other_ptr = gun_with_glockmag( 10, other_mag );
        item &other = *other_ptr;
        get_map().add_item( next_to_you, std::move( other_ptr ) );
        REQUIRE( other_mag->parent_item() == &other );
        // Both would be shown; CDF discovery never steals either (character_functions.cpp:1113-1120).
        REQUIRE( game_menus::inv::select_ammo_shows( gun, *other_mag, true ) );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, other_mag->contents.front(), true ) );
        CHECK( listed_with_map( you, gun ) == 0 );
    }

    SECTION( "the rounds in a loose magazine on the ground are not taken out of it" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> spare_ptr = loaded_mag( itype_glockmag, itype_9mm, 10 );
        item &spare = *spare_ptr;
        get_map().add_item( next_to_you, std::move( spare_ptr ) );
        item &rounds = spare.contents.front();
        REQUIRE( game_menus::inv::select_ammo_shows( gun, spare, true ) );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, rounds, true ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_nearby_items( 1 );
        CHECK( sel.listed_items() == std::vector<const item *>{ &spare } );
    }

    SECTION( "a NO_UNLOAD box on the ground is not opened" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        get_map().add_item( next_to_you, std::move( box_ptr ) );
        REQUIRE( listed_with_map( you, gun ) == 1 );
        box.set_flag( flag_id( "NO_UNLOAD" ) );
        // DDA inventory_ui.cpp:2113 add_contained_items.
        CHECK( listed_with_map( you, gun ) == 0 );
    }

    SECTION( "only the ammo selector walks into containers on the map" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        get_map().add_item( next_to_you, std::move( box_ptr ) );
        REQUIRE( listed_with_map( you, gun ) == 1 );

        // The same preset on any other selector: drop, pick-up and the rest keep their top-level list.
        const select_ammo_shows_preset preset( gun, true );
        plain_selector_probe plain( you, preset );
        plain.clear_items();
        plain.add_nearby_items( 1 );
        CHECK( plain.item_entry_count() == 0 );
    }
}

TEST_CASE( "ammo_selector_lists_what_is_nested_in_vehicle_cargo", "[p3][ui][nestammo2]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "bigback" ) ), false );
    map &here = get_map();

    // A frame with one "box" cargo part (data/json/vehicleparts/cargo.json), the avatar on the next tile.
    const tripoint_bub_ms origin( 60, 60, 0 );
    vehicle *veh = here.add_vehicle( vproto_id( "none" ), origin, 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "frame_vertical" ), true ) >= 0 );
    const int part = veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "box" ), true );
    REQUIRE( part >= 0 );
    here.add_vehicle_to_cache( veh );
    here.build_map_cache( origin.z(), true );
    REQUIRE( here.veh_at( origin ) );
    REQUIRE( veh->get_items( part ).empty() );
    you.setpos( origin + tripoint_south );

    SECTION( "ammo in a box in vehicle cargo is listed, and the option is that ammo" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        item &nine = box.contents.front();
        REQUIRE_FALSE( veh->add_item( part, std::move( box_ptr ) ) );
        REQUIRE( nine.parent_item() == &box );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, nine, true ) );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( gun, box, true ) );

        const select_ammo_shows_preset preset( gun, true );
        ammo_selector_actions_probe sel( you, gun, preset );
        sel.clear_items();
        sel.add_vehicle_items( origin );
        CHECK( sel.item_entry_count() == 1 );
        CHECK( sel.listed_items() == std::vector<const item *>{ &nine } );

        // The way select_ammo reaches it: the square next to the avatar, one row, no menu.
        REQUIRE( listed_with_map( you, gun ) == 1 );
        const item_reload_option opt = game_menus::inv::select_ammo( you, gun );
        REQUIRE( opt );
        CHECK( opt.target == in_gun );
        CHECK( opt.ammo == &nine );
        CHECK( opt.qty() == 10 );
    }

    SECTION( "a liquid in a jug in vehicle cargo is listed, and the option carries the jug" ) {
        item &canteen = you.i_add( item::spawn( itype_id( "2lcanteen" ) ) );
        detached_ptr<item> jug_ptr = item::spawn( itype_id( "jug_plastic" ) );
        item &jug = *jug_ptr;
        REQUIRE( jug.put_in( item::spawn( itype_id( "water_clean" ), calendar::turn_zero, 2 ),
                             pocket_type::CONTAINER ).success() );
        item &water = jug.contents.front();
        REQUIRE_FALSE( veh->add_item( part, std::move( jug_ptr ) ) );
        REQUIRE( game_menus::inv::select_ammo_shows( canteen, water, true ) );

        const select_ammo_shows_preset preset( canteen, true );
        ammo_selector_actions_probe sel( you, canteen, preset );
        sel.clear_items();
        sel.add_vehicle_items( origin );
        CHECK( sel.listed_items() == std::vector<const item *>{ &water } );

        REQUIRE( listed_with_map( you, canteen ) == 1 );
        const item_reload_option opt = game_menus::inv::select_ammo( you, canteen );
        REQUIRE( opt );
        CHECK( opt.target == &canteen );
        CHECK( opt.ammo == &jug );
    }

    SECTION( "what is not ammo for the target stays unlisted" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_38_special, calendar::turn, 7 ),
                             pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( veh->add_item( part, std::move( box_ptr ) ) );
        REQUIRE_FALSE( game_menus::inv::select_ammo_shows( gun, box.contents.front(), true ) );
        CHECK( listed_with_map( you, gun ) == 0 );
    }

    SECTION( "a NO_UNLOAD box in vehicle cargo is not opened" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( veh->add_item( part, std::move( box_ptr ) ) );
        REQUIRE( listed_with_map( you, gun ) == 1 );
        box.set_flag( flag_id( "NO_UNLOAD" ) );
        CHECK( listed_with_map( you, gun ) == 0 );
    }

    SECTION( "only the ammo selector walks into vehicle cargo" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 5, in_gun ) );
        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ) );
        item &box = *box_ptr;
        REQUIRE( box.put_in( item::spawn( itype_9mm, calendar::turn, 30 ), pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( veh->add_item( part, std::move( box_ptr ) ) );
        REQUIRE( listed_with_map( you, gun ) == 1 );

        const select_ammo_shows_preset preset( gun, true );
        plain_selector_probe plain( you, preset );
        plain.clear_items();
        plain.add_vehicle_items( origin );
        CHECK( plain.item_entry_count() == 0 );
    }
}

// ---------------------------------------------------------------------------
// CDF: P3-5-3b (the "Reload which destination on %s?" menu of select_ammo); CDF-only. DDA has no
// test of the menu (a grep of DDA tests/ at b4b62254ae2 for "which destination", "(well ", "top up"
// and find_all_matching_reload_targets finds only multimag_test.cpp 741-780, get_possible_reload_targets).
// The labels are tested through reload_destination_labels, the function that builds them. The menu
// itself is a uilist, which in test mode logs a debugmsg and answers UILIST_ERROR, so the select_ammo
// case reads that cancel path with capture_debugmsg_during, as cdf_item_reload_p3_test.cpp does for the
// uilist of reload_overflow_drops_item_when_no_room_via_actor. Choosing an entry, and the INT_MAX
// refill after it, need input: owner checklist row 38. item_location becomes item * (D1). Expected
// labels are built from the item names read in the JSON ("Glock magazine" 9mm.json 62, "STANAG
// 30-round magazine" 223.json 143, "heavy battery" battery.json 244, the pocket names of
// reload_dest_p3.json) and from tname() of the same items, not from the code under test.
// ---------------------------------------------------------------------------

namespace game_menus::inv
{
// CDF: P3-5-3b. Defined in src/game_inventory.cpp, which declares it itself; it is not in
// game_inventory.h (a test hook there would rebuild its 26 includers). Keep the signature identical.
auto reload_destination_labels( const std::vector<const reload_target *> &matches )
-> std::vector<std::string>;
} // namespace game_menus::inv

static const itype_id itype_debug_gun_with_mag_well( "debug_gun_with_mag_well" );
static const itype_id itype_p3_dest_named_integral_gun( "p3_dest_named_integral_gun" );
static const itype_id itype_p3_dest_named_wells_gun( "p3_dest_named_wells_gun" );
static const itype_id itype_test_multimag_gun_integral_ammo( "test_multimag_gun_integral_ammo" );
static const itype_id itype_test_multimag_gun_same_type( "test_multimag_gun_same_type" );

namespace
{

// The labels of the targets at the given positions of `targets`, in that order.
auto labels_of( const std::vector<reload_target> &targets, const std::vector<size_t> &which )
-> std::vector<std::string>
{
    std::vector<const reload_target *> matches;
    for( const size_t i : which ) {
        matches.push_back( &targets.at( i ) );
    }
    return game_menus::inv::reload_destination_labels( matches );
}

} // namespace

TEST_CASE( "reload_destination_labels_name_the_wells", "[p3][ui][5-3b]" )
{
    clear_all_state();

    SECTION( "a single well is named by its default magazine and has no number" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // test_multimag_gun: a glockmag well, then a stanag30 well (TEST_DATA items.json 1467-1488).
        REQUIRE( targets.size() == 2 );
        const std::vector<std::string> expected = { gun.tname() + ": Glock magazine" };
        CHECK( labels_of( targets, { 0 } ) == expected );
    }

    SECTION( "wells of one gun are numbered in the order of the matches" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun_same_type );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // test_multimag_gun_same_type: two glockmag wells (TEST_DATA items.json 1489-1510).
        REQUIRE( targets.size() == 2 );
        const std::string owner = gun.tname();
        const std::vector<std::string> expected = { owner + ": Glock magazine (well 1)",
                                                    owner + ": Glock magazine (well 2)"
                                                  };
        CHECK( labels_of( targets, { 0, 1 } ) == expected );
    }

    SECTION( "the number follows the order of the matches, not the pocket" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        REQUIRE( targets.size() == 2 );
        const std::string owner = gun.tname();
        // Wells of different magazine types of one owner are numbered too (DDA 3341, 3376).
        const std::vector<std::string> expected = { owner + ": STANAG 30-round magazine (well 1)",
                                                    owner + ": Glock magazine (well 2)"
                                                  };
        CHECK( labels_of( targets, { 1, 0 } ) == expected );
    }

    SECTION( "a pocket name is used in place of the default magazine name" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_p3_dest_named_wells_gun );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // reload_dest_p3.json: two glockmag wells named "upper well" and "lower well".
        REQUIRE( targets.size() == 2 );
        const std::string owner = gun.tname();
        const std::vector<std::string> expected = { owner + ": upper well (well 1)",
                                                    owner + ": lower well (well 2)"
                                                  };
        CHECK( labels_of( targets, { 0, 1 } ) == expected );
    }
}

TEST_CASE( "reload_destination_labels_name_integral_and_loaded_magazines", "[p3][ui][5-3b]" )
{
    clear_all_state();

    SECTION( "an integral magazine without a pocket name" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun_integral_ammo );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // Pocket order of the fixture: the 9mm MAGAZINE pocket, then the battery well (items.json 1868-1896).
        REQUIRE( targets.size() == 2 );
        REQUIRE( targets[0].kind == reload_target::kind::integral_magazine );
        const std::string owner = gun.tname();
        const std::vector<std::string> expected = { owner + ": integral magazine",
                                                    owner + ": heavy battery"
                                                  };
        CHECK( labels_of( targets, { 0, 1 } ) == expected );
    }

    SECTION( "an integral magazine with a pocket name" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_p3_dest_named_integral_gun );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        REQUIRE( targets.size() == 2 );
        REQUIRE( targets[0].kind == reload_target::kind::integral_magazine );
        const std::string owner = gun.tname();
        const std::vector<std::string> expected = { owner + ": chamber", owner + ": heavy battery" };
        CHECK( labels_of( targets, { 0, 1 } ) == expected );
    }

    SECTION( "a loaded magazine is a top up of that magazine, numbered only among its own type" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun );
        detached_ptr<item> glock = loaded_mag( itype_glockmag, itype_9mm, 5 );
        detached_ptr<item> stanag = loaded_mag( itype_stanag30, itype_556, 5 );
        item &glock_mag = *glock;
        item &stanag_mag = *stanag;
        gun_ptr->put_in( std::move( glock ), pocket_type::MAGAZINE_WELL );
        gun_ptr->put_in( std::move( stanag ), pocket_type::MAGAZINE_WELL );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // Order: well 0, the glockmag in it, well 1, the stanag30 in it.
        REQUIRE( targets.size() == 4 );
        REQUIRE( targets[1].kind == reload_target::kind::loaded_mag );
        REQUIRE( targets[3].kind == reload_target::kind::loaded_mag );
        const std::string owner = gun.tname();
        const std::vector<std::string> expected = { owner + ": top up " + glock_mag.tname(),
                                                    owner + ": top up " + stanag_mag.tname()
                                                  };
        CHECK( labels_of( targets, { 1, 3 } ) == expected );
    }

    SECTION( "two loaded magazines of one type in one gun are numbered" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun_same_type );
        detached_ptr<item> first = loaded_mag( itype_glockmag, itype_9mm, 5 );
        detached_ptr<item> second = loaded_mag( itype_glockmag, itype_9mm, 7 );
        item &first_mag = *first;
        item &second_mag = *second;
        // CDF: wells filled one by one, put_in routes both magazines into the first well (see cdf_item_reload_p3_test.cpp ~1300)
        std::vector<item_pocket *> wells;
        for( item_pocket *p : gun_ptr->get_pockets( []( const item_pocket & pp ) {
        return pp.is_type( pocket_type::MAGAZINE_WELL );
        } ) ) {
            wells.push_back( p );
        }
        REQUIRE( wells.size() == 2 );
        REQUIRE( wells[0]->insert_item( std::move( first ) ).success() );
        REQUIRE( wells[1]->insert_item( std::move( second ) ).success() );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        REQUIRE( targets.size() == 4 );
        REQUIRE( targets[1].kind == reload_target::kind::loaded_mag );
        REQUIRE( targets[3].kind == reload_target::kind::loaded_mag );
        const std::string owner = gun.tname();
        const std::vector<std::string> expected = { owner + ": top up " + targets[1].target->tname() + " (well 1)",
                                                    owner + ": top up " + targets[3].target->tname() + " (well 2)"
                                                  };
        // The two targets are the two magazines, whichever well each went into.
        REQUIRE( ( targets[1].target == &first_mag || targets[1].target == &second_mag ) );
        CHECK( labels_of( targets, { 1, 3 } ) == expected );
    }

    SECTION( "wells and loaded magazines are counted apart" ) {
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun_same_type );
        detached_ptr<item> loaded = loaded_mag( itype_glockmag, itype_9mm, 5 );
        item &mag = *loaded;
        gun_ptr->put_in( std::move( loaded ), pocket_type::MAGAZINE_WELL );
        item &gun = *gun_ptr;
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // Order: well 0, the glockmag in it, well 1 (empty).
        REQUIRE( targets.size() == 3 );
        REQUIRE( targets[1].kind == reload_target::kind::loaded_mag );
        const std::string owner = gun.tname();
        // Two wells are numbered; the one magazine is alone of its type and is not.
        const std::vector<std::string> expected = { owner + ": Glock magazine (well 1)",
                                                    owner + ": top up " + mag.tname(),
                                                    owner + ": Glock magazine (well 2)"
                                                  };
        CHECK( labels_of( targets, { 0, 1, 2 } ) == expected );
    }
}

TEST_CASE( "select_ammo_asks_which_destination_for_a_source_that_fits_several", "[p3][ui][5-3b]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    SECTION( "a source that fits two wells opens the destination menu, and cancelling it gives an empty option" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun_same_type ) );
        item &glock = you.i_add( loaded_mag( itype_glockmag, itype_9mm, 15 ) );
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // Both glockmag wells take the magazine, so the choice of destination is the player's.
        REQUIRE( find_all_matching_reload_targets( targets, glock ).size() == 2 );
        // One listed source and prompt off: select_ammo does not reach execute() (which would spin).
        REQUIRE( listed_candidates( you, gun ) == 1 );

        bool chose = true;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            chose = static_cast<bool>( game_menus::inv::select_ammo( you, gun ) );
        } );
        // The uilist answers UILIST_ERROR in test mode: DDA's cancel branch, an empty option.
        CHECK_FALSE( chose );
        CHECK( dmsg.find( "test mode" ) != std::string::npos );
    }

    // [UNSURE] relies on a normal select_ammo call logging nothing at all (capture_debugmsg_during also
    // returns the captured log). If this section is red on dmsg while the other section of this case is
    // green, delete this section only, with a log line in the report and a Blocked line.
    SECTION( "a source that fits one well takes no menu" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        you.i_add( loaded_mag( itype_glockmag, itype_9mm, 15 ) );
        REQUIRE( listed_candidates( you, gun ) == 1 );

        bool chose = false;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            chose = static_cast<bool>( game_menus::inv::select_ammo( you, gun ) );
        } );
        CHECK( chose );
        CHECK( dmsg.empty() );
    }
}

// ---------------------------------------------------------------------------
// CDF: P3-8-2a-sel (ranged::prompt_select_default_ammo_for now asks game_menus::inv::select_ammo, as
// DDA's select_ammo( weapon, false ) does); CDF-only. DDA has no test of ACTION_SELECT_DEFAULT_AMMO or
// of the default-ammo branch of game::reload (a grep of DDA tests/ at b4b62254ae2 for ammo_location,
// select_default_ammo and "keep that ammo" finds only reload_time_test.cpp 57 and 67). The three messages
// ("Cleared ...", "Selected ...", "You need to keep that ammo on you ...") are not asserted: the test
// build stubs Messages (tests/fake_messages.cpp), so what is observable is avatar::ammo_location. The
// texts are owner checklist row 39. A destination menu is a uilist: in test mode it logs a debugmsg and
// answers UILIST_ERROR (see the 5-3b case), so S1 reads that cancel path with capture_debugmsg_during.
// item_location becomes item * (D1). Every select_ammo call below has exactly one candidate listed and
// prompt off, so it never reaches the interactive execute() (which would spin): each section REQUIREs it.
// ---------------------------------------------------------------------------

static const itype_id itype_arrow_wood( "arrow_wood" );
static const itype_id itype_longbow( "longbow" );

TEST_CASE( "select_default_ammo_uses_game_menus_select_ammo", "[p3][ui][8-2a-sel]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );
    you.ammo_location = nullptr;
    REQUIRE( you.ammo_location.get_const() == nullptr );
    // CDF: P3-8-2a-sel fix round. clear_all_state() does not reset avatar::ammo_location, and
    // "ammo_location = nullptr" builds a safe_reference record keyed on the null pointer that the avatar then
    // holds (mem_count 1). Another test that calls cleanup_references() (npc_book_activity_safe_reference_cleanup)
    // would report that live record and free it under the avatar, and a later avatar_action::reload
    // (reload_on_vehicle_cargo) would fault in safe_reference::operator!. Leave the avatar with an unassigned
    // reference, which holds no record.
    const on_out_of_scope clear_default_ammo( [&you]() {
        you.ammo_location = safe_reference<item>();
    } );

    SECTION( "a source that fits two wells asks for the destination and sets no default" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun_same_type ) );
        item &glock = you.i_add( loaded_mag( itype_glockmag, itype_9mm, 15 ) );
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        // Both glockmag wells take the magazine, so the destination is the player's choice (DDA's picker).
        REQUIRE( find_all_matching_reload_targets( targets, glock ).size() == 2 );
        REQUIRE( listed_candidates( you, gun ) == 1 );

        const std::string dmsg = capture_debugmsg_during( [&]() {
            ranged::prompt_select_default_ammo_for( you, gun );
        } );
        // The destination uilist answers UILIST_ERROR in test mode: DDA's cancel branch, an empty option,
        // so nothing is stored. BN's picker took the magazine without asking and stored it.
        CHECK( dmsg.find( "test mode" ) != std::string::npos );
        CHECK( you.ammo_location.get_const() == nullptr );
    }

    SECTION( "ammo that fits one destination and is carried becomes the default" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        item &glock = you.i_add( loaded_mag( itype_glockmag, itype_9mm, 15 ) );
        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        REQUIRE( find_all_matching_reload_targets( targets, glock ).size() == 1 );
        REQUIRE( listed_candidates( you, gun ) == 1 );

        ranged::prompt_select_default_ammo_for( you, gun );
        CHECK( you.ammo_location.get_const() == &glock );
    }

    SECTION( "choosing the same ammo again clears the default" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        item &glock = you.i_add( loaded_mag( itype_glockmag, itype_9mm, 15 ) );
        REQUIRE( listed_candidates( you, gun ) == 1 );

        ranged::prompt_select_default_ammo_for( you, gun );
        REQUIRE( you.ammo_location.get_const() == &glock );
        // The magazine is still the only candidate, so the second call picks it again: DDA clears it.
        ranged::prompt_select_default_ammo_for( you, gun );
        CHECK( you.ammo_location.get_const() == nullptr );
    }

    // [UNSURE] needs add_nearby_items( 1 ) to offer an item lying on the avatar's own square. The premise
    // is proved by REQUIREing that select_ammo itself returns the floor magazine before the helper is
    // called. If a REQUIRE of this section fails (or the section hangs), delete this section only, with a
    // log line in the report and a Blocked line; do not change the assertions.
    SECTION( "ammo lying on the ground is not kept as the default" ) {
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        detached_ptr<item> floor_ptr = loaded_mag( itype_glockmag, itype_9mm, 15 );
        item &floor_mag = *floor_ptr;
        get_map().add_item( you.bub_pos(), std::move( floor_ptr ) );
        REQUIRE( floor_mag.where() == item_location_type::map );
        // Nothing carried is a candidate, and the floor magazine is: one entry, no execute().
        REQUIRE( listed_candidates( you, gun ) == 0 );
        REQUIRE( game_menus::inv::select_ammo_shows( gun, floor_mag, true ) );
        const item_reload_option offered = game_menus::inv::select_ammo( you, gun, false, true, true );
        REQUIRE( offered );
        REQUIRE( offered.ammo == &floor_mag );

        // DDA's else branch ("You need to keep that ammo on you ..."): the default stays unset.
        ranged::prompt_select_default_ammo_for( you, gun );
        CHECK( you.ammo_location.get_const() == nullptr );
        CHECK( floor_mag.where() == item_location_type::map );
    }

    // [UNSURE] needs the legacy integral bow to take arrow_wood through the owner-level target (a BN
    // integral gun: clip_size 1, no MAGAZINE pocket, answered by the direct-charges guard removed in 7ENDC). If a REQUIRE
    // of this section fails, delete this section only, with a log line in the report and a Blocked line.
    SECTION( "a bow takes its arrow as the default ammo" ) {
        item &bow = you.i_add( item::spawn( itype_longbow ) );
        item &arrow = you.i_add( item::spawn( itype_arrow_wood, calendar::turn, 5 ) );
        REQUIRE( bow.has_flag( flag_id( "RELOAD_AND_SHOOT" ) ) );
        REQUIRE( listed_candidates( you, bow ) == 1 );

        ranged::prompt_select_default_ammo_for( you, bow );
        CHECK( you.ammo_location.get_const() == &arrow );
    }

    // [UNSURE] DDA parity, the one player-visible loss against BN (Open question 1): select_ammo accepts
    // include_potential and does not read it, so a gun that is already full lists no ammo (the preset hides
    // what the target cannot take now, DDA game_inventory.cpp 3226-3259) and no default is stored. BN
    // listed ammo for a full gun. If a REQUIRE of this section fails, delete this section only, with a log
    // line in the report and a Blocked line; if the owner rules to keep BN's behaviour, this section is
    // removed by the job that implements it.
    SECTION( "a gun that is already full lists no ammo, so no default is set" ) {
        item *in_gun = nullptr;
        item &gun = you.i_add( gun_with_glockmag( 15, in_gun ) );
        you.i_add( item::spawn( itype_9mm, calendar::turn, 30 ) );
        REQUIRE( listed_candidates( you, gun ) == 0 );

        ranged::prompt_select_default_ammo_for( you, gun );
        CHECK( you.ammo_location.get_const() == nullptr );
    }
}
