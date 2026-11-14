#include "key_script_helper.h"

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "game_inventory.h"
#include "input.h"
#include "inventory_examiner.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "uistate.h"

#include <algorithm>
#include <string>
#include <vector>

// CDF: PL-64 IN1. Drives the real execute() of the inventory screens with scripted keys (see
// key_script_helper.h). Every expected result is read from DDA (b4b62254ae2), not from the CDF
// code under test; the row ids are INPUT-PATHS.tsv ids.
//
// The screens are grouped in the order the packet asked for: main inventory, drop, pickup, insert,
// then consume, unload and compare.
//
// Cases that need a later fix (IN2, or MS1 for the mouse registration) are written as the
// DDA-correct expectation and are EXPECTED TO FAIL on the base: they carry no special tag, only
// the [inventory][ui][keys] tags, so the failure is visible when the tag is run before that fix.
//
// The passing cases are the harness baseline: each one closes a screen through its real execute()
// and proves the scripted queue drives the screen, so a failing case above is a real difference and
// not a broken harness.

namespace
{

// Puts an item in the avatar's inventory under a known invlet, so a keypress never matches it as
// an invlet (find_entry_by_invlet runs before the action handlers).
auto carry(avatar &you, const std::string &type, char invlet = 'a') -> item &
{
    item &it = you.i_add(item::spawn(type));
    it.invlet = invlet;
    return it;
}

auto has_action(const std::vector<std::string> &actions, const std::string &id) -> bool
{
    return std::ranges::find(actions, id) != actions.end();
}

// The only pick selector the tests can name (inv_internal's local one is not visible): its ctxt is
// protected, a subclass can read the registered actions.
class pick_actions_probe : public inventory_pick_selector
{
    public:
        using inventory_pick_selector::inventory_pick_selector;
        auto actions() const -> std::vector<std::string> {
            return ctxt.get_registered_actions_copy();
        }
};

// Same for a multiselector (the drop screen), where INPUT-PATHS says the mouse actions are missing.
class drop_actions_probe : public inventory_drop_selector
{
    public:
        using inventory_drop_selector::inventory_drop_selector;
        auto actions() const -> std::vector<std::string> {
            return ctxt.get_registered_actions_copy();
        }
};

} // namespace

// ---------------------------------------------------------------------------
// Main inventory
// ---------------------------------------------------------------------------

TEST_CASE("key script: semicolon in the main inventory keeps the screen open",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    // INP-P-001. DDA inventory_ui.cpp:3635-3640: VIEW_CATEGORY_MODE toggles the view and stays
    // open. CDF's common_inventory_selector::handle_action returns true, which closes the screen.
    const key_script_result res =
        run_screen([&] { game_menus::inv::common(you); }, key_script_keys{";"});
    CHECK(res.still_open);
    CHECK(res.exhausted);
}

TEST_CASE("key script: unload_all is reachable and closes the main inventory by design",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    // INP-P-003. CDF-only action (DDA has none); its handler returns true, so the pick execute
    // returns and common() runs avatar_action::unload_all (game_inventory.cpp ~292-296) and leaves
    // the loop: the screen closes by design. On the base the action has no binding at all
    // (keybindings.json:2137-2140), so this requirement fails.
    const int unload_key = inp_mngr.get_first_char_for_action("unload_all", "INVENTORY");
    REQUIRE(unload_key != 0);
    const key_script_result res = run_screen([&] { game_menus::inv::common(you); },
    key_script_keys{std::string(1, static_cast<char>(unload_key)), "ESC"});
    CHECK_FALSE(res.still_open);
}

TEST_CASE("key script: TAB does not switch the inventory column", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");
    get_map().add_item(you.bub_pos(), item::spawn("rock"));

    inventory_pick_selector sel(you, default_preset);
    sel.add_character_items(you);
    sel.add_map_items(you.bub_pos());

    // INP-P-008. DDA has no key for plain TAB (SWITCH_LISTS is not registered here); CDF binds TAB
    // to PREV_COLUMN, so the selection leaves the inventory column.
    CHECK(sel.action_bound_to_key('\t').empty());
    const key_script_result res = run_screen(sel, key_script_keys{"TAB"});
    CHECK(res.active_column == 0);
}

TEST_CASE("key script: LEFT switches the inventory column", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");
    get_map().add_item(you.bub_pos(), item::spawn("rock"));

    // DDA binds LEFT to NEXT_COLUMN: the column moves once and the screen stays open.
    inventory_pick_selector sel(you, default_preset);
    sel.add_character_items(you);
    sel.add_map_items(you.bub_pos());
    const key_script_result res = run_screen(sel, key_script_keys{"LEFT"});
    CHECK(res.active_column == 1);
}

// ---------------------------------------------------------------------------
// Drop
// ---------------------------------------------------------------------------

TEST_CASE("key script: semicolon in the drop screen keeps it open and toggles the view",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    // INP-P-022. DDA inventory_multiselector::on_input (4795-4807) handles VIEW_CATEGORY_MODE; in
    // CDF the handler exists (inventory_ui.cpp:4098-4099, handle_mark_action) but the action is
    // never registered in a multiselector, so the dead branch can never fire.
    inventory_drop_selector sel(you, default_preset);
    sel.add_character_items(you);
    const key_script_result res = run_screen(sel, key_script_keys{";"});
    CHECK(res.still_open);
    CHECK(res.mode == "hierarchy");
}

TEST_CASE("key script: w does nothing in the drop and insert screens", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    item &hammer = carry(you, "hammer");

    // INP-P-026. DDA's drop selector does not register WIELD/WEAR; CDF's base on_input wields the
    // highlighted row in every selector.
    SECTION("drop") {
        inventory_drop_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"w"});
        CHECK(res.still_open);
        CHECK_FALSE(you.is_wielding(hammer));
    }
    SECTION("insert") {
        inventory_insert_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"w"});
        CHECK(res.still_open);
        CHECK_FALSE(you.is_wielding(hammer));
    }
}

TEST_CASE("key script: count keys mark a count in a multiselect drop screen",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    item &ammo = you.i_add(item::spawn("9mm", calendar::turn, 20));
    ammo.invlet = 'a';

    inventory_drop_selector sel(you, default_preset);
    sel.add_character_items(you);
    // DDA inventory_multiselector::on_input (4784-4787): a digit opens the count prompt, pre-filled
    // with that digit; RETURN confirms it, the next RETURN confirms the drop.
    const key_script_result res = run_screen(sel, key_script_keys{"3", "RETURN", "RETURN"});
    REQUIRE_FALSE(res.still_open);
    REQUIRE(res.returned_items.size() == 1);
    CHECK(res.returned_items.front() == &ammo);
    CHECK(res.returned_counts.front() == 3);
}

TEST_CASE("key script: a multiselector registers the mouse actions", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    // INP-P-018. DDA registers MOUSE_MOVE/SELECT/CLICK_AND_DRAG in the base ctor (3452-3453,
    // 3468-3469), so its multiselectors get them; CDF registers them only in the pick ctor.
    drop_actions_probe sel(you, default_preset);
    sel.add_character_items(you);
    const std::vector<std::string> actions = sel.actions();
    CHECK(has_action(actions, "SELECT"));
    CHECK(has_action(actions, "MOUSE_MOVE"));
    CHECK(has_action(actions, "CLICK_AND_DRAG"));
}

// ---------------------------------------------------------------------------
// Pickup
// ---------------------------------------------------------------------------

TEST_CASE("key script: the pickup screen returns the marked ground item",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    get_map().add_item(you.bub_pos(), item::spawn("rock"));

    // DDA pickup_selector::execute 5045-5049; TOGGLE_ENTRY marks the row, CONFIRM returns it.
    inventory_pickup_selector sel(you, default_preset);
    sel.add_map_items(you.bub_pos());
    // The map column is the second one; the carried columns are empty, so make it the active one.
    sel.set_active_column(1);
    const key_script_result res = run_screen(sel, key_script_keys{"l", "RETURN"});
    REQUIRE_FALSE(res.still_open);
    REQUIRE(res.returned_items.size() == 1);
    CHECK(res.returned_items.front()->typeId() == itype_id("rock"));
    CHECK(res.returned_counts.front() == 1);
}

TEST_CASE("key script: the pickup screen marks part of a ground stack",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    get_map().add_item(you.bub_pos(), item::spawn("9mm", calendar::turn, 20));

    // Same count-key path as the drop screen (DDA inventory_multiselector::on_input 4784-4787).
    inventory_pickup_selector sel(you, default_preset);
    sel.add_map_items(you.bub_pos());
    sel.set_active_column(1);
    const key_script_result res = run_screen(sel, key_script_keys{"3", "RETURN", "RETURN"});
    REQUIRE_FALSE(res.still_open);
    REQUIRE(res.returned_items.size() == 1);
    CHECK(res.returned_counts.front() == 3);
}

// ---------------------------------------------------------------------------
// Insert
// ---------------------------------------------------------------------------

TEST_CASE("key script: the insert screen returns no drop on QUIT", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    inventory_insert_selector sel(you, default_preset);
    sel.add_character_items(you);
    const key_script_result res = run_screen(sel, key_script_keys{"ESC"});
    CHECK_FALSE(res.still_open);
    CHECK(res.returned_items.empty());
}

// ---------------------------------------------------------------------------
// Consume (inv_internal's plain pick selector)
// ---------------------------------------------------------------------------

TEST_CASE("key script: semicolon toggles the view in a plain pick screen",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    // INP-P-021. inv_internal (game_inventory.cpp:189) builds this class for consume, wear,
    // take-off and the other plain pickers. DDA registers VIEW_CATEGORY_MODE in the base ctor
    // (inventory_ui.cpp:3471) and toggles it (3640); CDF does neither outside common().
    inventory_pick_selector sel(you, default_preset);
    sel.add_character_items(you);
    const key_script_result res = run_screen(sel, key_script_keys{";"});
    CHECK(res.still_open);
    CHECK(res.mode == "hierarchy");
}

TEST_CASE("key script: w does not wield from a plain pick screen", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    item &hammer = carry(you, "hammer");

    // INP-P-016. DDA registers WIELD/WEAR only in the pickup selector (4997-4998); the base
    // selector leaves the key to ANY_INPUT, which does nothing.
    inventory_pick_selector sel(you, default_preset);
    sel.add_character_items(you);
    const key_script_result res = run_screen(sel, key_script_keys{"w"});
    CHECK(res.still_open);
    CHECK_FALSE(you.is_wielding(hammer));
}

TEST_CASE("key script: an unbound key does not pick an item", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    // INP-P-017. DDA has no unbound-key pick hook; the key is ANY_INPUT and the screen stays open.
    // CDF's pick_by_unbound_key virtual is never overridden, so it behaves the same (dead hook).
    inventory_pick_selector sel(you, default_preset);
    sel.add_character_items(you);
    const key_script_result res = run_screen(sel, key_script_keys{"z"});
    CHECK(res.still_open);
    CHECK(res.returned_items.empty());
}

TEST_CASE("key script: the inventory registers DDA's numpad navigation toggle",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");

    // INP-P-019. DDA registers TOGGLE_NUMPAD_NAVIGATION (F5) in the base ctor (3472) and flips
    // uistate.numpad_navigation (3642-3644). CDF has neither the action nor the flag.
    pick_actions_probe sel(you, default_preset);
    sel.add_character_items(you);
    CHECK(has_action(sel.actions(), "TOGGLE_NUMPAD_NAVIGATION"));
}

TEST_CASE("key script: examine does not crash", "[inventory][ui][keys]") {
    // R-001 / R-002. DDA guards EXAMINE with if(selected) (3662-3667). CDF's column handler calls
    // get_selected().any_item() unguarded; with a valid item row that is a no-op, so this is the
    // regression baseline for the guarded path (the empty-column path needs IN2's guard).
    SECTION("pick screen") {
        clear_all_state();
        avatar &you = get_avatar();
        carry(you, "hammer");
        inventory_pick_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"e"});
        CHECK(res.still_open);
    }
    SECTION("examiner") {
        clear_all_state();
        avatar &you = get_avatar();
        item &bag = you.i_add(item::spawn("backpack"));
        REQUIRE(bag.put_in(item::spawn("rock"), pocket_type::CONTAINER).success());
        inventory_examiner ex(you, &bag);
        ex.add_contained_items(&bag);
        const key_script_result res = run_screen(ex, key_script_keys{"e"});
        CHECK(res.still_open);
    }
}

TEST_CASE("key script: w on an empty pick screen does not crash", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();

    // R-006. wield()/wear() return early for a dummy entry (inventory_ui.cpp:2987, 3005), so the
    // unguarded base handler is a no-op on an empty selector (the UNSURE in INPUT-PATHS is resolved).
    inventory_pick_selector sel(you, default_preset);
    const key_script_result res = run_screen(sel, key_script_keys{"w"});
    CHECK(res.still_open);
    CHECK(res.selection == nullptr);
}

TEST_CASE("key script: DOWN then CONFIRM picks the second row", "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");
    carry(you, "wrench", 'b');

    // INP-P-006. DDA column on_input moves the cursor and stays open; CONFIRM then returns the
    // row the cursor moved to. The row order is the column's category order, so compare the two.
    inventory_pick_selector first_row(you, default_preset);
    first_row.add_character_items(you);
    const key_script_result first = run_screen(first_row, key_script_keys{"RETURN"});
    REQUIRE(first.returned != nullptr);

    inventory_pick_selector second_row(you, default_preset);
    second_row.add_character_items(you);
    const key_script_result second = run_screen(second_row, key_script_keys{"DOWN", "RETURN"});
    REQUIRE_FALSE(second.still_open);
    REQUIRE(second.returned != nullptr);
    CHECK(second.returned != first.returned);
}

TEST_CASE("key script: the consume pick screen returns the confirmed item",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    item &hammer = carry(you, "hammer");

    SECTION("CONFIRM returns the highlighted item") {
        inventory_pick_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"RETURN"});
        CHECK_FALSE(res.still_open);
        CHECK(res.returned == &hammer);
    }
    SECTION("QUIT returns no item") {
        inventory_pick_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"ESC"});
        CHECK_FALSE(res.still_open);
        CHECK(res.returned == nullptr);
    }
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------

TEST_CASE("key script: the unload screen returns the confirmed item",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    item &hammer = carry(you, "hammer");

    // DDA inventory_ui.cpp:5302-5317: CONFIRM returns the highlighted item, QUIT returns nothing.
    SECTION("CONFIRM returns the highlighted item") {
        unload_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"RETURN"});
        CHECK_FALSE(res.still_open);
        CHECK(res.returned == &hammer);
    }
    SECTION("QUIT returns no item") {
        unload_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"ESC"});
        CHECK_FALSE(res.still_open);
        CHECK(res.returned == nullptr);
    }
    SECTION("CONTAIN_MODE toggles the destination and stays open") {
        // INP-P-030 (MATCH): DDA inventory_ui.cpp:5305 flips uistate.unload_auto_contain.
        const bool initial = uistate.unload_auto_contain;
        unload_selector sel(you, default_preset);
        sel.add_character_items(you);
        const key_script_result res = run_screen(sel, key_script_keys{"U"});
        CHECK(res.still_open);
        CHECK(uistate.unload_auto_contain != initial);
        uistate.unload_auto_contain = initial;
    }
}

// ---------------------------------------------------------------------------
// Compare
// ---------------------------------------------------------------------------

TEST_CASE("key script: semicolon toggles the view in the compare screen",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    carry(you, "hammer");
    carry(you, "wrench", 'b');

    // INP-P-023. DDA's compare loop sends every other action to inventory_selector::on_input, which
    // toggles VIEW_CATEGORY_MODE (3635). CDF's base on_input has no such branch.
    inventory_compare_selector sel(you);
    sel.add_character_items(you);
    const key_script_result res = run_screen(sel, key_script_keys{";"});
    CHECK(res.still_open);
    CHECK(res.mode == "hierarchy");
}

TEST_CASE("key script: the compare screen returns the two marked items",
          "[inventory][ui][keys]") {
    clear_all_state();
    avatar &you = get_avatar();
    item &hammer = carry(you, "hammer");
    item &wrench = carry(you, "wrench", 'b');

    // INP-P-024. DDA inventory_ui.cpp:4599-: two TOGGLE_ENTRY marks close the screen with the pair.
    inventory_compare_selector sel(you);
    sel.add_character_items(you);
    const key_script_result res = run_screen(sel, key_script_keys{"l", "DOWN", "l"});
    REQUIRE_FALSE(res.still_open);
    REQUIRE(res.returned_items.size() == 2);
    // The row order is the column's category order, so only the membership is asserted.
    CHECK(std::ranges::find(res.returned_items, &hammer) != res.returned_items.end());
    CHECK(std::ranges::find(res.returned_items, &wrench) != res.returned_items.end());
}
