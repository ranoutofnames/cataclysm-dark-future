#include "avatar.h"
#include "calendar.h"
#include "carried_stacks.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "game_inventory.h"
#include "input.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "map/map.h"
#include "reload/reload.h"
#include "state_helpers.h"
#include "type_id.h"

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

// How many carried stacks the select_ammo preset shows for the host. add_character_items lists the
// wielded and worn items and the top level of the carry pockets (these tests wear only a backpack,
// wield nothing, and the map is empty). select_ammo with prompt off reaches the interactive
// execute() unless exactly one entry is listed, and execute() spins forever headless, so the
// select_ammo sections REQUIRE this count before the call.
auto listed_candidates( const avatar &you, item &host ) -> size_t
{
    size_t listed = 0;
    for( const carried_stack &stack : carried_item_stacks( you ) ) {
        if( game_menus::inv::select_ammo_shows( host, *stack.front(), true ) ) {
            ++listed;
        }
    }
    return listed;
}

// CDF: ctxt is protected; these read the actions a selector registered.
class ammo_selector_actions_probe : public ammo_inventory_selector
{
    public:
        using ammo_inventory_selector::ammo_inventory_selector;

        auto actions() const -> std::vector<std::string> {
            return ctxt.get_registered_actions_copy();
        }
};

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
