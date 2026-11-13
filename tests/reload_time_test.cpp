// DDA source: tests/reload_time_test.cpp (DDA commit b4b62254ae2), case
// reload_from_inventory_times. Adapted for CDF: BN has no fire_gun spending moves for the reload,
// so the RELOAD_AND_SHOOT cost is read from ranged::time_to_attack with the option chosen by
// reload_selection::prepare.
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "ranged.h"
#include "reload/reload_selection.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>

static void check_reload_time(
    const std::string& weapon, const std::string& ammo, const std::string& container,
    int expected_moves) {
    clear_all_state();
    auto& here = get_map();
    const auto test_origin = tripoint_bub_ms(60, 60, 0);
    auto& shooter = get_avatar();
    shooter.setpos(test_origin);
    auto wielded = item::spawn(weapon, calendar::start_of_cataclysm);
    shooter.set_primary_weapon(std::move(wielded));
    if (container.empty()) {
        here.add_item(test_origin, item::spawn(ammo, calendar::start_of_cataclysm));
    } else {
        shooter.wear_item(item::spawn(container, calendar::start_of_cataclysm), false);
        shooter.i_add(item::spawn(ammo, calendar::start_of_cataclysm, 1));
    }
    REQUIRE(shooter.primary_weapon().typeId() == itype_id(weapon));
    // CDF: P3-6-6a, the pure selection in place of the BN menu (same options, no UI)
    const auto result = reload_selection::prepare(
        shooter, shooter.primary_weapon(),
        {.prompt = false, .discovery = {.include_empty_mags = true, .include_potential = true}});
    auto opt = result.selected;
    REQUIRE(result.outcome == reload_selection::selection_outcome::automatic);
    REQUIRE(opt);
    // DDA's fire_gun spends time_to_attack plus the RAS reload cost; BN folds both into
    // time_to_attack.
    const auto spent_moves = ranged::time_to_attack(shooter, shooter.primary_weapon(), opt.ammo);
    CAPTURE(opt.moves());
    CHECK(spent_moves > expected_moves * 0.95);
    CHECK(spent_moves < expected_moves * 1.05);
}

// CDF: expected moves are derived from CDF data (archery time_to_attack 80/6, no base "reload" on
// bows and slings, CDF arrow volume 250 ml/10), not DDA's; functionality port, not a balance port
TEST_CASE("reload_from_inventory_times", "[reload][inventory][balance]") {
    SECTION("slingshot from a backpack") {
        check_reload_time("slingshot", "pebble", "backpack", 482);
    }
    SECTION("slingshot from an ammo pouch") {
        check_reload_time("slingshot", "pebble", "ammo_pouch", 217);
    }
    SECTION("slingshot from a tool belt") {
        check_reload_time("slingshot", "pebble", "tool_belt", 281);
    }
    SECTION("slingshot from a pocket") { check_reload_time("slingshot", "pebble", "pants", 282); }
    SECTION("slingshot from the ground") { check_reload_time("slingshot", "pebble", "", 262); }
    SECTION("staff sling from a backpack") {
        check_reload_time("staff_sling", "rock", "backpack", 644);
    }
    SECTION("staff sling from a stone pouch") {
        check_reload_time("staff_sling", "rock", "stone_pouch", 364);
    }
    SECTION("staff sling from a tool belt") {
        check_reload_time("staff_sling", "rock", "tool_belt", 432);
    }
    SECTION("staff sling from a pocket") { check_reload_time("staff_sling", "rock", "pants", 444); }
    SECTION("staff sling from the ground") { check_reload_time("staff_sling", "rock", "", 424); }
    SECTION("bow from a giant novelty backpack") {
        check_reload_time("longbow", "arrow_wood", "backpack_giant", 482);
    }
    SECTION("bow from a quiver") { check_reload_time("longbow", "arrow_wood", "quiver", 202); }
    SECTION("bow from the ground") { check_reload_time("longbow", "arrow_wood", "", 262); }
}
