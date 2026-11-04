#include "activity_actor_definitions.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "game.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const activity_id ACT_UNLOAD("ACT_UNLOAD");

static const itype_id itype_556("556");
static const itype_id itype_9mm("9mm");
static const itype_id itype_ammolink223("ammolink223");
static const itype_id itype_backpack("backpack");
static const itype_id itype_belt223("belt223");
static const itype_id itype_glockmag("glockmag");
static const itype_id itype_heavy_battery_cell("heavy_battery_cell");
static const itype_id itype_plut_cell("plut_cell");
static const itype_id itype_test_multimag_gun_integral_ammo("test_multimag_gun_integral_ammo");
static const itype_id itype_test_plutonium_mag("test_plutonium_mag");

namespace {

// CDF: P3-6-4a. A glock magazine (capacity 15, data/json/items/magazine/9mm.json) holding
// `rounds` 9mm, carried in the pack of the cleared avatar.
auto add_loaded_glockmag(avatar& you, int rounds) -> item& {
    detached_ptr<item> mag = item::spawn(itype_glockmag);
    REQUIRE(mag->put_in(item::spawn(itype_9mm, calendar::turn, rounds), pocket_type::MAGAZINE)
                .success());
    return you.i_add(std::move(mag));
}

// CDF: P3-6-4a. belt223 (data/json/items/magazine/223.json:3, linkage ammolink223) holding `rounds`
// 556. belt223 has "count": 100 and spawns with 100 rounds of its default ammo: ammo_set replaces
// them.
auto add_loaded_belt(avatar& you, int rounds) -> item& {
    detached_ptr<item> belt = item::spawn(itype_belt223);
    belt->ammo_set(itype_556, rounds);
    return you.i_add(std::move(belt));
}

auto assign_unload(avatar& you, int moves, item& target) -> void {
    you.assign_activity(
        std::make_unique<player_activity>(std::make_unique<unload_activity_actor>(moves, target)));
}

auto serialized(const std::unique_ptr<activity_actor>& actor) -> std::string {
    std::ostringstream oss;
    {
        JsonOut jsout(oss);
        serialize(actor, jsout);
    }
    return oss.str();
}

} // namespace

// CDF: P3-6-4a V1. DDA activity_actor.cpp:6517-6537 (serialize, deserialize) and the table row at
// :15445. The actor is written, read back through the registered deserializer and written again:
// the second text is the first and it carries the actor type, the moves and the target.
TEST_CASE("unload_activity_actor_serializes_and_registers", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& mag = add_loaded_glockmag(you, 15);

    std::unique_ptr<activity_actor> actor = std::make_unique<unload_activity_actor>(321, mag);
    const std::string first = serialized(actor);
    CAPTURE(first);
    CHECK(first.find("\"actor_type\":\"ACT_UNLOAD\"") != std::string::npos);
    CHECK(first.find("\"moves_total\":321") != std::string::npos);
    // the target is saved too
    CHECK(first.find("\"target\":") != std::string::npos);

    std::unique_ptr<activity_actor> loaded;
    std::istringstream in(first);
    JsonIn jsin(in);
    deserialize(loaded, jsin);
    REQUIRE(loaded);
    CHECK(loaded->get_type() == ACT_UNLOAD);
    const std::string second = serialized(loaded);
    CAPTURE(second);
    CHECK(second == first);
}

// CDF: P3-6-4a V1b. CDF's safe_reference is a uid into the save tables: the actor text is written
// first (that assigns the uid), the magazine is saved, then destroyed as after a quit; loading the
// magazine back registers the uid and the actor text resolves to the loaded copy (the pattern of
// reload_activity_actor_survives_save_and_load). The loaded activity then unloads that copy.
TEST_CASE("unload_activity_actor_survives_save_and_load", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    std::string actor_json;
    std::string mag_json;
    {
        item& mag = add_loaded_glockmag(you, 15);
        std::unique_ptr<activity_actor> actor = std::make_unique<unload_activity_actor>(321, mag);
        actor_json = serialized(actor);
        // the reference is released before the original goes away
        actor.reset();

        std::ostringstream mag_oss;
        {
            JsonOut jsout(mag_oss);
            mag.serialize(jsout);
        }
        mag_json = mag_oss.str();
        detached_ptr<item> gone_mag = mag.detach();
    }
    REQUIRE(mag_json.find("\"id\":") != std::string::npos);

    std::istringstream mag_in(mag_json);
    JsonIn mag_jsin(mag_in);
    detached_ptr<item> loaded_mag_ptr = item::spawn(mag_jsin);
    REQUIRE(loaded_mag_ptr);
    item& mag = you.i_add(std::move(loaded_mag_ptr));
    REQUIRE(mag.ammo_remaining() == 15);

    std::unique_ptr<activity_actor> actor;
    std::istringstream actor_in(actor_json);
    JsonIn actor_jsin(actor_in);
    deserialize(actor, actor_jsin);
    REQUIRE(actor);
    REQUIRE(actor->get_type() == ACT_UNLOAD);

    you.assign_activity(std::make_unique<player_activity>(std::move(actor)));
    REQUIRE(you.has_activity(ACT_UNLOAD));
    CHECK(mag.ammo_remaining() == 15);
    process_activity(you);
    CHECK(!you.activity);
    CHECK(mag.ammo_remaining() == 0);
    CHECK(you.charges_of(itype_9mm) == 15);
}

// CDF: P3-6-4a V2. DDA activity_actor.cpp:6420-6427 (finish) and :6473-6514 (the non-container
// branch). Running the activity moves the 15 9mm out of the magazine as one stack.
TEST_CASE("unload_actor_finish_empties_a_magazine", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& mag = add_loaded_glockmag(you, 15);
    REQUIRE(mag.ammo_remaining() == 15);

    assign_unload(you, 100, mag);
    REQUIRE(you.has_activity(ACT_UNLOAD));
    process_activity(you);

    CHECK(!you.activity);
    CHECK(mag.ammo_remaining() == 0);
    // CDF: glockmag holds 15 (magazine/9mm.json:72); the rounds arrive as a single stack of 15
    const std::vector<item*> rounds = you.items_with([](const item& it) {
        return it.typeId() == itype_9mm;
    });
    REQUIRE(rounds.size() == 1);
    CHECK(rounds.front()->charges == 15);
    // the magazine itself is not destroyed (no MAG_DESTROY on a glock magazine)
    CHECK(you.amount_of(itype_glockmag) == 1);
    // CDF: the unload message "You unload your %s." is not asserted: tests/fake_messages.cpp stubs
    // every add_msg, so the text cannot be read back.
}

// CDF: P3-6-4a V3. DDA activity_actor.cpp:6495-6508 (belt linkage and the disassemble message),
// :6510-6512 (MAG_DESTROY on an emptied belt).
TEST_CASE("unload_actor_disassembles_a_belt", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& belt = add_loaded_belt(you, 30);
    REQUIRE(belt.is_ammo_belt());
    REQUIRE(belt.ammo_remaining() == 30);

    assign_unload(you, 100, belt);
    process_activity(you);

    CHECK(!you.activity);
    // the 30 rounds are back as one stack
    const std::vector<item*> rounds = you.items_with([](const item& it) {
        return it.typeId() == itype_556;
    });
    REQUIRE(rounds.size() == 1);
    CHECK(rounds.front()->charges == 30);
    // CDF: belt223 names ammolink223 as its linkage (223.json:12); one link per round
    CHECK(you.charges_of(itype_ammolink223) == 30);
    // CDF: magazine_belt is MAG_DESTROY (classes/magazine.json:15): the emptied belt is gone
    CHECK(you.amount_of(itype_belt223) == 0);
    // CDF: the "You disassemble your %s." message is not asserted (stubbed add_msg)
}

// CDF: P3-6-4a V4. DDA character_inventory.cpp:2658-2675 (the magazine branch assigns the actor)
// with the CDF cost of ruling Q-16-2 (b): half the reload cost per stack, at most 200 moves.
TEST_CASE("unload_item_on_a_magazine_starts_a_timed_activity", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    // CDF: three 9mm (9mm.json: 250 ml per 50 rounds, so 15 ml here, under the 20 ml that adds
    // one handling move) with skill_gun 0 and the magazine reload_time of 100 per round
    // (itype.h:854): item_reload_cost is 300, the unload costs half of it, 150, below the cap of
    // 200
    item& mag = add_loaded_glockmag(you, 3);
    REQUIRE(you.get_skill_level(skill_id("gun")) == 0);
    REQUIRE(mag.ammo_remaining() == 3);

    REQUIRE(avatar_funcs::unload_item(you, mag));

    REQUIRE(you.has_activity(ACT_UNLOAD));
    // the rounds stay in the magazine until the activity runs
    CHECK(mag.ammo_remaining() == 3);
    const activity_actor* actor = you.activity->get_actor();
    REQUIRE(actor != nullptr);
    CHECK(actor->progress.get_moves_total() == 150);

    process_activity(you);
    CHECK(!you.activity);
    CHECK(mag.ammo_remaining() == 0);
    CHECK(you.charges_of(itype_9mm) == 3);
}

// CDF: P3-6-4a V5. DDA character_inventory.cpp:2658-2660: with bypass_activity the static
// unload runs at once, no activity and no moves are spent (OD-9).
TEST_CASE("unload_item_bypass_activity_is_instant", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& mag = add_loaded_glockmag(you, 15);
    const int moves_before = you.get_moves();

    REQUIRE(avatar_funcs::unload_item(you, mag, true));

    CHECK(!you.activity);
    CHECK(mag.ammo_remaining() == 0);
    CHECK(you.charges_of(itype_9mm) == 15);
    CHECK(you.get_moves() == moves_before);
}

// CDF: P3-6-4a V6. CDF keeps its cap of 200 moves (ruling Q-16-2 (b)).
TEST_CASE("unload_cost_is_capped_at_200", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    // CDF: 100 rounds, magazine reload_time 100 per round (itype.h:854): item_reload_cost is
    // at least 10000, so half of it is far above the cap
    item& belt = add_loaded_belt(you, 100);
    REQUIRE(belt.ammo_remaining() == 100);
    REQUIRE(you.item_reload_cost(belt, *belt.all_items_top(pocket_type::MAGAZINE).front(), 100) / 2
            > 200);

    REQUIRE(avatar_funcs::unload_item(you, belt));

    REQUIRE(you.has_activity(ACT_UNLOAD));
    const activity_actor* actor = you.activity->get_actor();
    REQUIRE(actor != nullptr);
    CHECK(actor->progress.get_moves_total() == 200);
    // a belt costs the same numbers as any magazine in CDF: no extra halving
    CHECK(belt.ammo_remaining() == 100);
}

// CDF: P3-6-4a V7. The activity is interruptible: a turn of it does not unload and cancelling
// it leaves the magazine loaded.
TEST_CASE("unload_actor_is_interruptible", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& mag = add_loaded_glockmag(you, 15);

    // far more moves than a turn can pay
    assign_unload(you, 100000, mag);
    REQUIRE(you.has_activity(ACT_UNLOAD));
    CHECK(mag.ammo_remaining() == 15);

    you.moves = 100;
    you.activity->do_turn(you);
    REQUIRE(you.has_activity(ACT_UNLOAD));
    CHECK(mag.ammo_remaining() == 15);

    you.cancel_activity();
    CHECK(!you.activity);
    CHECK(mag.ammo_remaining() == 15);
    // nothing was moved into the pack (charges_of does not count the rounds inside a magazine)
    CHECK(you.charges_of(itype_9mm) == 0);
    // cancelled and not suspended: nothing comes back by itself
    you.resume_backlog_activity();
    CHECK(!you.activity);
}

// CDF: P3-6-4a V8. Guns and tools never reach the actor (ruling ADV-14, 6-4d owns them): a gun
// with an integral MAGAZINE pocket is is_magazine() under D13, but unloading it stays instant.
TEST_CASE("unload_item_on_a_multimag_gun_stays_synchronous", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    detached_ptr<item> gun_ptr = item::spawn(itype_test_multimag_gun_integral_ammo);
    REQUIRE(gun_ptr->is_magazine());
    REQUIRE(gun_ptr->put_in(item::spawn(itype_9mm, calendar::turn, 1), pocket_type::MAGAZINE)
                .success());
    detached_ptr<item> cell = item::spawn(itype_heavy_battery_cell);
    cell->ammo_set(itype_id("battery"), 100);
    REQUIRE(gun_ptr->put_in(std::move(cell), pocket_type::MAGAZINE_WELL).success());
    item& gun = you.i_add(std::move(gun_ptr));
    REQUIRE(gun.magazines_current().size() == 1);

    REQUIRE(avatar_funcs::unload_item(you, gun));

    CHECK(!you.activity);
    CHECK(!you.has_activity(ACT_UNLOAD));
    // unloaded at once: the cell left the well and sits in the pack
    CHECK(gun.magazines_current().empty());
    const std::vector<item*> cells = you.items_with([](const item& it) {
        return it.typeId() == itype_heavy_battery_cell;
    });
    CHECK(cells.size() == 1);
}

// CDF: P3-6-4a V9. unload_all calls unload_item once per item in a loop, so every call has to
// finish before the next (a timed activity per item would replace the one before it). The
// floor variant is used: the inventory variant walks a list of every item, rounds included, that
// the unloading itself invalidates.
TEST_CASE("unload_all_empties_every_magazine_and_leaves_no_activity", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    map& here = get_map();
    const tripoint_bub_ms spot(60, 60, 0);
    g->place_player(spot);
    here.i_clear(spot);
    you.wear_item(item::spawn(itype_backpack), false);

    for (const int rounds : {15, 7}) {
        detached_ptr<item> mag = item::spawn(itype_glockmag);
        REQUIRE(mag->put_in(item::spawn(itype_9mm, calendar::turn, rounds), pocket_type::MAGAZINE)
                    .success());
        here.add_item(spot, std::move(mag));
    }
    REQUIRE(here.i_at(spot).size() == 2);

    avatar_action::unload_all(you, false);

    CHECK(!you.activity);
    CHECK(here.i_at(spot).size() == 2);
    for (item* mag : here.i_at(spot)) { CHECK(mag->ammo_remaining() == 0); }
    CHECK(you.charges_of(itype_9mm) == 22);
}

// CDF: P3-6-4a V10. DDA activity_actor.cpp:6476-6478: plutonium leaves a magazine in cells,
// not in charges. No base-data magazine takes plutonium: TEST_DATA/unload_p3.json adds one.
TEST_CASE("unload_actor_divides_plutonium_charges", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    detached_ptr<item> mag_ptr = item::spawn(itype_test_plutonium_mag);
    // CDF: PLUTONIUM_CHARGES is 500 (game_constants.h:80): 1000 charges are 2 cells
    REQUIRE(
        mag_ptr->put_in(item::spawn(itype_plut_cell, calendar::turn, 1000), pocket_type::MAGAZINE)
            .success());
    item& mag = you.i_add(std::move(mag_ptr));
    REQUIRE(mag.ammo_remaining() == 1000);

    unload_activity_actor::unload(you, mag);

    CHECK(mag.ammo_remaining() == 0);
    CHECK(you.charges_of(itype_plut_cell) == 2);
}

// CDF: P3-6-4a V11. The target is a safe_reference: if it is destroyed while the activity runs, the
// activity ends quietly (DDA holds an item_location and has no such guard).
TEST_CASE("unload_actor_tolerates_a_lost_target", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& mag = add_loaded_glockmag(you, 15);

    assign_unload(you, 100, mag);
    REQUIRE(you.has_activity(ACT_UNLOAD));
    // the magazine is destroyed under the running activity
    { detached_ptr<item> gone = mag.detach(); }

    REQUIRE(process_activity_bounded(you, 20, 1000));
    CHECK(!you.activity);
    CHECK(you.amount_of(itype_glockmag) == 0);
    // the rounds went with the magazine: nothing was unloaded into the pack
    CHECK(you.charges_of(itype_9mm) == 0);
}
