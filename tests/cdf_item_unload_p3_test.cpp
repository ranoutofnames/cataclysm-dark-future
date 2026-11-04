#include "activity_actor_definitions.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "cached_options.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "examine_item_menu.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "safe_reference.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const activity_id ACT_INSERT_ITEM("ACT_INSERT_ITEM");
static const activity_id ACT_UNLOAD("ACT_UNLOAD");

static const itype_id itype_backpack_hiking("backpack_hiking");
static const itype_id itype_flashlight("flashlight");
static const itype_id itype_glock_19("glock_19");
static const itype_id itype_pencil("pencil");
static const itype_id itype_steel_pan("steel_pan");
static const itype_id itype_test_p3_cage("test_p3_cage");
static const itype_id itype_test_p3_split_jug("test_p3_split_jug");
static const itype_id itype_test_rock("test_rock");
static const itype_id itype_tums("tums");
static const itype_id itype_test_liquid_1ml("test_liquid_1ml");
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
static const itype_id itype_pipe_pistol("pipe_pistol");
static const itype_id itype_stanag30("stanag30");
static const itype_id itype_test_multimag_aux_gun("test_multimag_aux_gun");
static const itype_id itype_test_multimag_gun("test_multimag_gun");
static const itype_id itype_test_p3_two_well_gunmod("test_p3_two_well_gunmod");

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

    // CDF: P3-6-4d. The gun has two sources (the 9mm pocket and the cell): without
    // bypass_activity unload_item asks "Unload which source?" (DDA character_inventory.cpp:2722), a
    // uilist that cannot be answered here. The call passes bypass_activity; the checks are
    // unchanged.
    REQUIRE(avatar_funcs::unload_item(you, gun, true));

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

// CDF: P3-6-4b step 0. avatar_action::unload_all( you, true ) took a list of every item the
// avatar carries before it unloaded anything. Unloading moves items by copying them (the
// originals are destroyed), so entries later in the list were dangling pointers and the copies
// were never reached. Here a pouch holds three loaded magazines inside a pack that is not to be
// emptied: unloading the pouch relocates the magazines (the three in the list are destroyed), and
// the loop has to reach the copies and empty them too. The fixture is
// TEST_DATA/unload_container_p3.json.
TEST_CASE("unload_all_inventory_variant_runs_to_completion", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& pack = *you.top_items().front();
    // the pack itself is not to be emptied (unloading a worn pack with no other storage puts its
    // contents on the floor): its pockets are not unloadable
    for (item_pocket* pkt : pack.get_container_pockets()) { pkt->settings.set_unloadable(false); }
    detached_ptr<item> pouch = item::spawn(itype_test_p3_cage);
    // CDF: glockmag holds 15 (magazine/9mm.json:72); 15 + 7 + 3 rounds of 9mm
    for (const int rounds : {15, 7, 3}) {
        detached_ptr<item> mag = item::spawn(itype_glockmag);
        REQUIRE(mag->put_in(item::spawn(itype_9mm, calendar::turn, rounds), pocket_type::MAGAZINE)
                    .success());
        REQUIRE(pouch->put_in(std::move(mag), pocket_type::CONTAINER).success());
    }
    item& pouch_ref = *pouch;
    REQUIRE(pack.put_in(std::move(pouch), pocket_type::CONTAINER).success());
    REQUIRE(pouch_ref.all_items_top(pocket_type::CONTAINER).size() == 3);
    REQUIRE(you.charges_of(itype_9mm) == 0);

    avatar_action::unload_all(you, true);

    CHECK(!you.activity);
    CHECK(pouch_ref.contents.empty());
    const std::vector<item*> mags = you.items_with([](const item& it) {
        return it.typeId() == itype_glockmag;
    });
    REQUIRE(mags.size() == 3);
    for (const item* mag : mags) { CHECK(mag->ammo_remaining() == 0); }
    CHECK(you.charges_of(itype_9mm) == 25);
}

namespace {

// CDF: sets the thief mode and pins it with THIEF_MODE_KEEP = YES, so query_consume_ownership
// never reaches a popup; the dtor restores the defaults (as the reload entry tests do).
struct thief_mode_guard_p3 {
    explicit thief_mode_guard_p3(const std::string& mode) {
        avatar& who = get_avatar();
        who.set_value("THIEF_MODE", mode);
        who.set_value("THIEF_MODE_KEEP", "YES");
    }
    ~thief_mode_guard_p3() {
        avatar& who = get_avatar();
        who.set_value("THIEF_MODE", "THIEF_ASK");
        who.set_value("THIEF_MODE_KEEP", "NO");
    }
};

// CDF: what the liquid handling does in test mode (nobody picks from the menu), restored on exit
struct liquid_action_guard_p3 {
    explicit liquid_action_guard_p3(const test_mode_spilling_action_t action)
        : saved(test_mode_spilling_action) {
        test_mode_spilling_action = action;
    }
    ~liquid_action_guard_p3() { test_mode_spilling_action = saved; }
    test_mode_spilling_action_t saved;
};

// CDF: P3-6-4b. A hiking backpack (data/json/items/armor/storage.json:27) holding one item of each
// of `ids` in its pockets, carried by the avatar.
auto add_pack_holding(avatar& you, const std::vector<itype_id>& ids) -> item& {
    detached_ptr<item> pack = item::spawn(itype_backpack_hiking);
    for (const itype_id& id : ids) {
        REQUIRE(pack->put_in(item::spawn(id), pocket_type::CONTAINER).success());
    }
    return you.i_add(std::move(pack));
}

auto moves_of_activity(avatar& you) -> int {
    REQUIRE(you.has_activity(ACT_UNLOAD));
    const activity_actor* actor = you.activity->get_actor();
    REQUIRE(actor != nullptr);
    return actor->progress.get_moves_total();
}

auto carried_of_type(avatar& you, const itype_id& id) -> std::vector<item*> {
    return you.items_with([&id](const item& it) { return it.typeId() == id; });
}

} // namespace

// CDF: P3-6-4b W1. DDA character_inventory.cpp:2548-2587: unloading a container assigns the unload
// actor and the contents leave the container when it finishes (activity_actor.cpp:6435-6467).
TEST_CASE("unload_container_starts_a_timed_activity", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& pack = add_pack_holding(you, {itype_glock_19, itype_flashlight});
    REQUIRE(pack.all_items_top(pocket_type::CONTAINER).size() == 2);

    REQUIRE(avatar_funcs::unload_item(you, pack));

    REQUIRE(you.has_activity(ACT_UNLOAD));
    // the pack is still full until the activity runs
    CHECK(pack.all_items_top(pocket_type::CONTAINER).size() == 2);

    process_activity(you);

    CHECK(!you.activity);
    CHECK(pack.contents.empty());
    // both items are carried by the avatar now, neither of them inside the pack
    for (const itype_id& id : {itype_glock_19, itype_flashlight}) {
        const std::vector<item*> found = carried_of_type(you, id);
        REQUIRE(found.size() == 1);
        CHECK(found.front()->parent_item() != &pack);
    }
}

// CDF: P3-6-4b W2. DDA character_inventory.cpp:2569-2586: an item that stacks with the one before
// it costs max( bulk handling cost, 1 ), any other item its full handling cost.
TEST_CASE("unload_container_cost_discounts_stacks", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    // CDF: a flashlight (lighting.json:183) is a tool that does not count by charges, so five of
    // them are five items in the pack that stack with each other (test_rock merges into one item)
    item& same_pack = add_pack_holding(you, std::vector<itype_id>(5, itype_flashlight));
    const std::vector<item*> lights = same_pack.all_items_top(pocket_type::CONTAINER);
    REQUIRE(lights.size() == 5);
    for (const item* light : lights) { REQUIRE(lights.front()->stacks_with(*light)); }
    const int first = you.item_handling_cost(*lights.front());
    const int next = std::max(you.item_handling_cost(*lights.front(), true, 0, -1, true), 1);
    // CDF: INVENTORY_HANDLING_PENALTY 100 (game_constants.h:73) + 100 ml / 20 ml = 105 for the
    // first flashlight; the stack discount drops the base cost: 0 + 5 = 5 for each of the other
    // four
    REQUIRE(first == 105);
    REQUIRE(next == 5);

    REQUIRE(avatar_funcs::unload_item(you, same_pack));
    const int stacked_moves = moves_of_activity(you);
    CHECK(stacked_moves == first + 4 * next);
    you.cancel_activity();

    // five items that do not stack with each other: the full handling cost for each
    item& mixed_pack = add_pack_holding(
        you, {itype_test_rock, itype_pencil, itype_flashlight, itype_glock_19, itype_steel_pan});
    int full_cost = 0;
    const std::vector<item*> mixed = mixed_pack.all_items_top(pocket_type::CONTAINER);
    REQUIRE(mixed.size() == 5);
    for (const item* contained : mixed) { full_cost += you.item_handling_cost(*contained); }

    REQUIRE(avatar_funcs::unload_item(you, mixed_pack));
    const int mixed_moves = moves_of_activity(you);
    CHECK(mixed_moves == full_cost);
    CHECK(stacked_moves < mixed_moves);
}

// CDF: P3-6-4b W3. DDA activity_actor.cpp:6459-6467: the pocket types are emptied in the order
// CONTAINER, MAGAZINE_WELL, MAGAZINE and only the first one that changed goes in one action.
// The fixture is TEST_DATA/unload_container_p3.json (no shipped item has both pockets).
TEST_CASE("unload_container_stops_after_first_changed_pocket_type", "[p3][unload][pocket]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    detached_ptr<item> cage_ptr = item::spawn(itype_test_p3_cage);
    REQUIRE(cage_ptr->put_in(item::spawn(itype_test_rock), pocket_type::CONTAINER).success());
    REQUIRE(cage_ptr->put_in(item::spawn(itype_glockmag), pocket_type::MAGAZINE_WELL).success());
    item& cage = you.i_add(std::move(cage_ptr));
    REQUIRE(cage.all_items_top(pocket_type::CONTAINER).size() == 1);
    REQUIRE(cage.all_items_top(pocket_type::MAGAZINE_WELL).size() == 1);

    REQUIRE(avatar_funcs::unload_item(you, cage));
    process_activity(you);

    CHECK(!you.activity);
    CHECK(cage.all_items_top(pocket_type::CONTAINER).empty());
    // the magazine in the well stays for the next press
    CHECK(cage.all_items_top(pocket_type::MAGAZINE_WELL).size() == 1);

    REQUIRE(avatar_funcs::unload_item(you, cage));
    process_activity(you);

    CHECK(cage.all_items_top(pocket_type::MAGAZINE_WELL).empty());
    CHECK(carried_of_type(you, itype_glockmag).size() == 1);
    CHECK(carried_of_type(you, itype_test_rock).size() == 1);
}

// CDF: P3-6-4b W4. DDA character_inventory.cpp:2575 and activity_actor.cpp:6447: only the pockets
// whose settings allow unloading are emptied, the actor path included.
TEST_CASE("unload_container_leaves_non_unloadable_pockets", "[p3][unload][pocket]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& pack = add_pack_holding(you, {itype_test_rock});
    for (item_pocket* pkt : pack.get_container_pockets()) { pkt->settings.set_unloadable(false); }

    avatar_funcs::unload_item(you, pack);
    process_activity_bounded(you, 20, 1000);
    CHECK(!you.activity);
    CHECK(pack.all_items_top(pocket_type::CONTAINER).size() == 1);

    for (item_pocket* pkt : pack.get_container_pockets()) { pkt->settings.set_unloadable(true); }
    REQUIRE(avatar_funcs::unload_item(you, pack));
    process_activity_bounded(you, 20, 1000);
    CHECK(!you.activity);
    CHECK(pack.contents.empty());
}

// CDF: P3-6-4b W5. A liquid in a container is not handed over inside the removal pass: the solids
// leave first and the liquid is then offered to consume_liquid (which may open a menu). In test
// mode the menu is replaced by test_mode_spilling_action (handle_liquid.cpp, get_liquid_target).
TEST_CASE("unload_container_liquid_is_handled_after_the_pass", "[p3][unload][liquid]") {
    clear_all_state();
    avatar& you = get_avatar();
    map& here = get_map();
    const tripoint_bub_ms spot(60, 60, 0);
    g->place_player(spot);
    here.i_clear(spot);
    you.wear_item(item::spawn(itype_backpack), false);
    detached_ptr<item> jug_ptr = item::spawn(itype_test_p3_split_jug);
    REQUIRE(jug_ptr->put_in(item::spawn(itype_test_rock), pocket_type::CONTAINER).success());
    // CDF: test_liquid_1ml (TEST_DATA/dda_unseal_items.json:4) is a liquid that stays an item on
    // the ground (water_clean is poured into a field); the jug's second pocket is the watertight
    // one
    REQUIRE(
        jug_ptr
            ->put_in(item::spawn(itype_test_liquid_1ml, calendar::turn, 2), pocket_type::CONTAINER)
            .success());
    item& jug = you.i_add(std::move(jug_ptr));
    REQUIRE(jug.all_items_top(pocket_type::CONTAINER).size() == 2);

    SECTION("the liquid is poured out once the solids are gone") {
        const liquid_action_guard_p3 guard(test_mode_spilling_action_t::spill_all);
        REQUIRE(avatar_funcs::unload_item(you, jug));
        process_activity(you);

        CHECK(jug.contents.empty());
        CHECK(carried_of_type(you, itype_test_rock).size() == 1);
        int on_floor = 0;
        for (const item* floor_it : here.i_at(spot)) {
            if (floor_it->typeId() == itype_test_liquid_1ml) { on_floor += floor_it->charges; }
        }
        CHECK(on_floor == 2);
    }
    SECTION("a liquid that nobody takes stays in the container") {
        const liquid_action_guard_p3 guard(test_mode_spilling_action_t::cancel_spill);
        REQUIRE(avatar_funcs::unload_item(you, jug));
        process_activity(you);

        CHECK(jug.all_items_top(pocket_type::CONTAINER).size() == 1);
        CHECK(jug.all_items_top(pocket_type::CONTAINER).front()->typeId() == itype_test_liquid_1ml);
        CHECK(carried_of_type(you, itype_test_rock).size() == 1);
    }
}

// CDF: P3-6-4b W6. The checks before the cost loop: an empty container is "already empty" (no
// activity) and a foreign item is refused under THIEF_HONEST (query_consume_ownership). The message
// texts are not asserted: tests/fake_messages.cpp stubs add_msg.
TEST_CASE("unload_container_already_empty_and_foreign_item", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    SECTION("an empty container") {
        item& pack = add_pack_holding(you, {});
        CHECK_FALSE(avatar_funcs::unload_item(you, pack));
        CHECK(!you.activity);
    }
    SECTION("a foreign-owned container under THIEF_HONEST") {
        item& pack = add_pack_holding(you, {itype_test_rock});
        // CDF: foreign faction that does not shoot on sight (likes_u 30, factions.json:235), which
        // query_consume_ownership needs to ask at all; the avatar is your_followers
        pack.set_owner(faction_id("free_merchants"));
        REQUIRE_FALSE(pack.is_owned_by(you, true));
        const thief_mode_guard_p3 guard("THIEF_HONEST");

        CHECK_FALSE(avatar_funcs::unload_item(you, pack));
        CHECK(!you.activity);
        CHECK(pack.all_items_top(pocket_type::CONTAINER).size() == 1);
    }
}

// CDF: P3-6-4b W7. DDA tests/item_pocket_test.cpp:3036-3091 (issue 75156, a segfault): the static
// unload of an item that is nested in another container. backpack > steel pan > two bottles of
// antacid tablets (1 and 4). The tablet moves into the other bottle, the pan spills its bottles
// into the pack.
TEST_CASE("unload_from_spillable_container", "[item][pocket]") {
    clear_all_state();
    avatar& u = get_avatar();
    map& here = get_map();
    REQUIRE(!u.wear_item(item::spawn(itype_backpack), false));
    item* backpack = u.top_items().front();
    GIVEN("spillable container contains two pillbottles, one with 4 pills and one with 1 pill") {
        // Starting inventory looks like:
        //   backpack >
        //     steel_pan >
        //       bottle_plastic_small > antacid tablet (1)
        //       bottle_plastic_small > antacid tablet (4)
        // (the pan would normally reject the bottles with a spill message)
        REQUIRE(backpack->put_in(item::spawn(itype_steel_pan), pocket_type::CONTAINER).success());
        item* steelpan = backpack->all_items_top().front();
        REQUIRE(
            steelpan
                ->put_in(item::in_its_container(item::spawn(itype_tums, calendar::turn, 1), 1),
                         pocket_type::CONTAINER)
                .success());
        REQUIRE(
            steelpan
                ->put_in(item::in_its_container(item::spawn(itype_tums, calendar::turn, 1), 4),
                         pocket_type::CONTAINER)
                .success());
        WHEN("unload the pillbottle that only has one pill") {
            item* bottle1 = steelpan->all_items_top().front();
            const safe_reference<item> bottle1_ref(bottle1);
            unload_activity_actor::unload(u, *bottle1);
            THEN(
                "pill is unloaded into bottle that previously had 4 pills, remaining empty bottle is kept") {
                // Expected inventory after unloading should be:
                //   backpack >
                //     steel_pan
                //     bottle_plastic_small
                //     bottle_plastic_small > antacid tablet (5)
                CHECK(here.i_at(u.bub_pos()).empty()); // no items spilled to ground
                CHECK(u.top_items().size() == 1);      // backpack is still only inventory item
                const std::vector<item*> backpack_items_vec = backpack->all_items_top();
                REQUIRE(backpack_items_vec.size() == 3);
                CHECK(backpack_items_vec[0]->typeId().str() == "steel_pan");
                CHECK(backpack_items_vec[0]->empty());
                CHECK(backpack_items_vec[1]->typeId().str() == "bottle_plastic_small");
                CHECK(backpack_items_vec[1]->empty());
                CHECK(backpack_items_vec[2]->typeId().str() == "bottle_plastic_small");
                // CDF: DDA stores the five tablets as five items; here they are one stack of
                // five (the tablets count by charges), so the five are counted by charges
                REQUIRE(backpack_items_vec[2]->all_items_top().size() == 1);
                CHECK(backpack_items_vec[2]->all_items_top().front()->charges == 5);
                // the spilled bottle was moved (copied) out of the pan: the reference to it
                // is gone, as the item_location of DDA is
                CHECK_FALSE(static_cast<bool>(bottle1_ref));
            }
        }
    }
}

// CDF: P3-6-4b W8. bypass_activity (a CDF extension of the container branch, DDA has none there):
// the container empties at once, no activity is created and the cost of the loop is charged.
TEST_CASE("unload_container_bypass_charges_the_loop_moves", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& pack = add_pack_holding(you, std::vector<itype_id>(5, itype_flashlight));
    const std::vector<item*> lights = pack.all_items_top(pocket_type::CONTAINER);
    REQUIRE(lights.size() == 5);
    // the W2 sum for a stack of five
    const int first = you.item_handling_cost(*lights.front());
    const int next = std::max(you.item_handling_cost(*lights.front(), true, 0, -1, true), 1);
    // CDF: 105 for the first flashlight and 5 for each stack mate (see W2)
    REQUIRE(first == 105);
    REQUIRE(next == 5);
    const int moves_before = you.get_moves();

    REQUIRE(avatar_funcs::unload_item(you, pack, true));

    CHECK(!you.activity);
    CHECK(pack.contents.empty());
    CHECK(moves_before - you.get_moves() == first + 4 * next);
}

// CDF: P3-6-4c X1. DDA character_ammo.cpp:255-290 (Character::rate_action_unload), in its order: a
// container with something in it is good, NO_UNLOAD is cant, a loaded magazine well is good, a
// gun mod that is a loaded gun is good, an item without ammo types is cant, an item with rounds
// or casings is good and every other ammo user is iffy (the "maybe" colour of the examine menu).
TEST_CASE("rate_action_unload_per_well_rule", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    using examine_item_menu::hint_rating;

    SECTION("a backpack with one item is good, an empty one is not") {
        // CDF: backpack_hiking (data/json/items/armor/storage.json:27) has container pockets
        detached_ptr<item> pack = item::spawn(itype_backpack_hiking);
        CHECK(examine_item_menu::rate_action_unload(you, *pack) != hint_rating::good);
        REQUIRE(pack->put_in(item::spawn(itype_flashlight), pocket_type::CONTAINER).success());
        CHECK(examine_item_menu::rate_action_unload(you, *pack) == hint_rating::good);
    }

    SECTION("NO_UNLOAD is cant even when the item holds ammo") {
        // CDF: the M72 LAW (data/json/items/gun/66mm.json:26) extends flags with NO_UNLOAD
        detached_ptr<item> law = item::spawn("LAW");
        law->ammo_set(itype_id("66mm_HEAT"), 1);
        REQUIRE(law->ammo_remaining() == 1);
        CHECK(examine_item_menu::rate_action_unload(you, *law) == hint_rating::cant);
    }

    SECTION("a gun with two wells") {
        // CDF: test_multimag_gun (data/mods/TEST_DATA/items.json:1467) has a glockmag well and a
        // stanag30 well
        detached_ptr<item> gun = item::spawn("test_multimag_gun");
        REQUIRE(gun->magazines_current().empty());
        // no magazine and no rounds: the gun takes ammo, so it is "maybe" and not "no"
        CHECK(examine_item_menu::rate_action_unload(you, *gun) == hint_rating::iffy);

        // only the second well holds a magazine (the magazine is empty: the well is what counts)
        REQUIRE(gun->put_in(item::spawn("stanag30"), pocket_type::MAGAZINE_WELL).success());
        REQUIRE(gun->magazines_current().size() == 1);
        CHECK(examine_item_menu::rate_action_unload(you, *gun) == hint_rating::good);
    }

    SECTION("an item that takes no ammo is cant") {
        // CDF: the pencil (data/mods/TEST_DATA/dda_pocket_items.json) has no ammo types and
        // NO_UNLOAD; the empty steel pan (data/json/items/generic/dining_kitchen.json) has no ammo
        // types and no NO_UNLOAD, so only the ammo type test can rate it cant
        detached_ptr<item> pencil = item::spawn(itype_pencil);
        REQUIRE(pencil->ammo_types().empty());
        CHECK(examine_item_menu::rate_action_unload(you, *pencil) == hint_rating::cant);
        detached_ptr<item> pan = item::spawn(itype_steel_pan);
        REQUIRE(pan->ammo_types().empty());
        REQUIRE_FALSE(pan->has_flag(flag_id("NO_UNLOAD")));
        REQUIRE(pan->empty());
        CHECK(examine_item_menu::rate_action_unload(you, *pan) == hint_rating::cant);
    }

    SECTION("a rifle is good through a loaded gun mod") {
        // CDF: ar15 (gun/223.json) takes the underslung shotgun u_shotgun
        // (gunmod/underbarrel.json:611, clip 2, ammo shot)
        detached_ptr<item> rifle = item::spawn("ar15");
        CHECK(examine_item_menu::rate_action_unload(you, *rifle) == hint_rating::iffy);
        REQUIRE(rifle->put_in(item::spawn("u_shotgun"), pocket_type::MOD).success());
        REQUIRE(rifle->gunmods().size() == 1);
        CHECK(examine_item_menu::rate_action_unload(you, *rifle) == hint_rating::iffy);
        rifle->gunmods().front()->ammo_set(itype_id("shot_00"), 1);
        REQUIRE(rifle->gunmods().front()->ammo_remaining() == 1);
        CHECK(examine_item_menu::rate_action_unload(you, *rifle) == hint_rating::good);
    }

    SECTION("a gun with rounds in it is good") {
        // CDF: sw_619 takes 38_special and keeps its rounds as direct charges (gun/38.json)
        detached_ptr<item> revolver = item::spawn("sw_619");
        CHECK(examine_item_menu::rate_action_unload(you, *revolver) == hint_rating::iffy);
        revolver->ammo_set(itype_id("38_special"), 6);
        REQUIRE(revolver->ammo_remaining() == 6);
        CHECK(examine_item_menu::rate_action_unload(you, *revolver) == hint_rating::good);
    }
}

// CDF: P3-6-4c X3. DDA character_inventory.cpp:2578-2589: with a destination container the cost
// loop is skipped and every top item of the source becomes a drop_location (the whole stack for
// items that count by charges) for the insert actor, which moves the contents when it runs.
TEST_CASE("unload_with_new_container_uses_the_insert_actor", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    // CDF: a worn hiking backpack (50 L) holds the two hiking packs (14 L each) the cases carry;
    // the small backpack (15 L, storage.json) cannot
    you.wear_item(item::spawn(itype_backpack_hiking), false);

    SECTION("items that do not count by charges") {
        item& pack = add_pack_holding(you, {itype_glock_19, itype_flashlight});
        item& second = you.i_add(item::spawn(itype_backpack_hiking));
        REQUIRE(pack.all_items_top(pocket_type::CONTAINER).size() == 2);
        REQUIRE(second.contents.empty());
        const int moves_before = you.get_moves();

        REQUIRE(avatar_funcs::unload_item(you, pack, false, &second));

        CHECK(you.has_activity(ACT_INSERT_ITEM));
        CHECK_FALSE(you.has_activity(ACT_UNLOAD));
        // no cost loop up front: the insert actor pays its moves per item when it runs
        CHECK(you.get_moves() == moves_before);
        // nothing moved yet
        CHECK(pack.all_items_top(pocket_type::CONTAINER).size() == 2);
        CHECK(second.contents.empty());

        process_activity(you);

        CHECK(!you.activity);
        CHECK(pack.contents.empty());
        const std::vector<item*> inside = second.all_items_top(pocket_type::CONTAINER);
        REQUIRE(inside.size() == 2);
        for (const itype_id& id : {itype_glock_19, itype_flashlight}) {
            const std::vector<item*> found = carried_of_type(you, id);
            REQUIRE(found.size() == 1);
            CHECK(found.front()->parent_item() == &second);
        }
    }

    SECTION("a stack that counts by charges moves whole") {
        // CDF: 9mm (data/json/items/ammo/9mm.json) counts by charges
        detached_ptr<item> source = item::spawn(itype_backpack_hiking);
        REQUIRE(source->put_in(item::spawn(itype_9mm, calendar::turn, 30), pocket_type::CONTAINER)
                    .success());
        item& pack = you.i_add(std::move(source));
        item& second = you.i_add(item::spawn(itype_backpack_hiking));
        REQUIRE(pack.all_items_top(pocket_type::CONTAINER).size() == 1);

        REQUIRE(avatar_funcs::unload_item(you, pack, false, &second));
        REQUIRE(you.has_activity(ACT_INSERT_ITEM));
        process_activity(you);

        CHECK(!you.activity);
        CHECK(pack.contents.empty());
        int rounds = 0;
        for (const item* it : second.all_items_top(pocket_type::CONTAINER)) {
            REQUIRE(it->typeId() == itype_9mm);
            rounds += it->charges;
        }
        CHECK(rounds == 30);
    }
}

// CDF: P3-6-4c X4. DDA character_inventory.cpp:2550-2554 and 2658-2676: only the container branch
// reads new_container. A magazine keeps its timed unload; an empty container is refused first.
TEST_CASE("unload_new_container_ignored_by_magazines_and_empty_checks", "[p3][unload][activity]") {
    clear_all_state();
    avatar& you = get_avatar();
    // CDF: a worn hiking backpack (50 L) holds the two hiking packs (14 L each) the cases carry;
    // the small backpack (15 L, storage.json) cannot
    you.wear_item(item::spawn(itype_backpack_hiking), false);
    item& second = you.i_add(item::spawn(itype_backpack_hiking));

    SECTION("a loaded magazine") {
        item& mag = add_loaded_glockmag(you, 10);
        REQUIRE(mag.ammo_remaining() == 10);

        REQUIRE(avatar_funcs::unload_item(you, mag, false, &second));

        CHECK(you.has_activity(ACT_UNLOAD));
        CHECK_FALSE(you.has_activity(ACT_INSERT_ITEM));
        process_activity(you);
        CHECK(!you.activity);
        CHECK(mag.ammo_remaining() == 0);
        CHECK(you.charges_of(itype_9mm) == 10);
    }

    SECTION("an empty container") {
        detached_ptr<item> empty = item::spawn(itype_backpack_hiking);
        item& pack = you.i_add(std::move(empty));
        REQUIRE(pack.contents.empty());

        // the message "is already empty" is not asserted: add_msg is stubbed in the tests
        CHECK_FALSE(avatar_funcs::unload_item(you, pack, false, &second));

        CHECK(!you.activity);
        CHECK(second.contents.empty());
    }
}

namespace {

// CDF: P3-6-4d. test_multimag_gun (data/mods/TEST_DATA/items.json:1467) with a glockmag holding 15
// 9mm in its first well and a stanag30 (data/json/items/magazine/223.json:141) holding 30 556 in
// the second one, carried by the avatar.
auto add_two_well_gun(avatar& you) -> item& {
    detached_ptr<item> gun = item::spawn(itype_test_multimag_gun);
    detached_ptr<item> glock = item::spawn(itype_glockmag);
    REQUIRE(
        glock->put_in(item::spawn(itype_9mm, calendar::turn, 15), pocket_type::MAGAZINE).success());
    detached_ptr<item> stanag = item::spawn(itype_stanag30);
    REQUIRE(stanag->put_in(item::spawn(itype_556, calendar::turn, 30), pocket_type::MAGAZINE)
                .success());
    REQUIRE(gun->put_in(std::move(glock), pocket_type::MAGAZINE_WELL).success());
    REQUIRE(gun->put_in(std::move(stanag), pocket_type::MAGAZINE_WELL).success());
    item& placed = you.i_add(std::move(gun));
    REQUIRE(placed.magazines_current().size() == 2);
    return placed;
}

// CDF: P3-6-4d. test_multimag_gun_integral_ammo (items.json:1846): one 9mm in its integral MAGAZINE
// pocket (ammo_restriction 9mm: 1) and, with_cell, a heavy_battery_cell charged with 56 in the
// well (the charge of cdf_item_ammo_p3_test.cpp:645).
auto add_integral_gun(avatar& you, bool with_cell) -> item& {
    detached_ptr<item> gun = item::spawn(itype_test_multimag_gun_integral_ammo);
    REQUIRE(
        gun->put_in(item::spawn(itype_9mm, calendar::turn, 1), pocket_type::MAGAZINE).success());
    if (with_cell) {
        detached_ptr<item> cell = item::spawn(itype_heavy_battery_cell);
        cell->ammo_set(cell->ammo_default(), 56);
        REQUIRE(cell->ammo_remaining() == 56);
        REQUIRE(gun->put_in(std::move(cell), pocket_type::MAGAZINE_WELL).success());
    }
    item& placed = you.i_add(std::move(gun));
    REQUIRE(placed.magazines_current().size() == (with_cell ? 1u : 0u));
    return placed;
}

auto integral_pockets_of(item& gun) -> std::vector<item_pocket*> {
    return gun.get_pockets([](const item_pocket& p) { return p.is_type(pocket_type::MAGAZINE); });
}

} // namespace

// CDF: P3-6-4d Y1. DDA multimag_test.cpp:949 (character_unload_ejects_every_loaded_magazine): the
// gun branch ejects every loaded magazine, not the first well only.
TEST_CASE("unload_gun_ejects_every_loaded_magazine", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& gun = add_two_well_gun(you);

    REQUIRE(avatar_funcs::unload_item(you, gun, true));

    CHECK(gun.magazines_current().empty());
    // both magazines are in the inventory and kept their rounds
    const std::vector<item*> glocks = carried_of_type(you, itype_glockmag);
    const std::vector<item*> stanags = carried_of_type(you, itype_stanag30);
    REQUIRE(glocks.size() == 1);
    REQUIRE(stanags.size() == 1);
    CHECK(glocks.front()->ammo_remaining() == 15);
    CHECK(stanags.front()->ammo_remaining() == 30);
    CHECK(glocks.front()->parent_item() != &gun);
    CHECK(stanags.front()->parent_item() != &gun);
}

// CDF: P3-6-4d Y2. DDA multimag_test.cpp:1120 (multimag_unload_drains_integral_and_well): a gun
// with an integral MAGAZINE pocket and a battery in its well gives up both.
TEST_CASE("unload_gun_drains_the_integral_pocket_and_the_well", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& gun = add_integral_gun(you, true);
    REQUIRE(integral_pockets_of(gun).size() == 1);

    REQUIRE(avatar_funcs::unload_item(you, gun, true));

    CHECK(gun.magazines_current().empty());
    for (item_pocket* pocket : integral_pockets_of(gun)) {
        for (const item* left : pocket->all_items_top()) { CHECK(left->has_flag(flag_CASING)); }
    }
    CHECK(you.charges_of(itype_9mm) == 1);
    const std::vector<item*> cells = carried_of_type(you, itype_heavy_battery_cell);
    REQUIRE(cells.size() == 1);
    CHECK(cells.front()->ammo_remaining() == 56);
}

// CDF: P3-6-4d Y3. CDF-only. The integral-only gun has ammo_capacity() 0 (no clip, no well
// magazine), which the old gate refused; the drainable pocket is the exemption.
TEST_CASE("unload_integral_only_multimag_gun_reaches_the_drain_branch", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& gun = add_integral_gun(you, false);
    // CDF: the gate of unload_item refuses ammo_capacity() <= 0; this gun has none to give
    REQUIRE(gun.ammo_capacity() <= 0);
    REQUIRE(gun.ammo_remaining() == 1);

    REQUIRE(avatar_funcs::unload_item(you, gun, true));

    CHECK(you.charges_of(itype_9mm) == 1);
    CHECK(gun.ammo_remaining() == 0);
    for (item_pocket* pocket : integral_pockets_of(gun)) {
        for (const item* left : pocket->all_items_top()) { CHECK(left->has_flag(flag_CASING)); }
    }
}

// CDF: P3-6-4d. CDF-only. With one source the picker is not asked, so the default call (no
// bypass_activity) of a gun unload stays synchronous and starts no activity (ADV-14).
TEST_CASE("unload_gun_with_one_source_needs_no_bypass", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    SECTION("one integral pocket") {
        item& gun = add_integral_gun(you, false);

        REQUIRE(avatar_funcs::unload_item(you, gun));

        CHECK(!you.activity);
        CHECK(gun.ammo_remaining() == 0);
        CHECK(you.charges_of(itype_9mm) == 1);
    }

    SECTION("one magazine") {
        detached_ptr<item> gun_ptr = item::spawn(itype_test_multimag_gun);
        detached_ptr<item> glock = item::spawn(itype_glockmag);
        REQUIRE(glock->put_in(item::spawn(itype_9mm, calendar::turn, 15), pocket_type::MAGAZINE)
                    .success());
        REQUIRE(gun_ptr->put_in(std::move(glock), pocket_type::MAGAZINE_WELL).success());
        item& gun = you.i_add(std::move(gun_ptr));
        REQUIRE(gun.magazines_current().size() == 1);

        REQUIRE(avatar_funcs::unload_item(you, gun));

        CHECK(!you.activity);
        CHECK(gun.magazines_current().empty());
        CHECK(carried_of_type(you, itype_glockmag).size() == 1);
    }
}

// CDF: P3-6-4d Y4. DDA character_inventory.cpp:2753 and :2780: every source costs half of the
// reload cost of what it holds, a magazine with qty -1 and an integral pocket with its rounds.
TEST_CASE("unload_moves_cost_half_reload_per_source", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    SECTION("two magazines") {
        item& gun = add_two_well_gun(you);
        const std::vector<item*> mags = gun.magazines_current();
        REQUIRE(mags.size() == 2);
        const int first = you.item_reload_cost(gun, *mags[0], -1) / 2;
        const int second = you.item_reload_cost(gun, *mags[1], -1) / 2;
        // CDF: the two magazines are not free and differ, so charging once or the full cost shows
        REQUIRE(first > 0);
        REQUIRE(second > 0);
        REQUIRE(first != second);
        const int before = you.get_moves();

        REQUIRE(avatar_funcs::unload_item(you, gun, true));

        CHECK(before - you.get_moves() == first + second);
    }

    SECTION("an integral pocket and a magazine") {
        item& gun = add_integral_gun(you, true);
        const std::vector<item*> mags = gun.magazines_current();
        REQUIRE(mags.size() == 1);
        const std::vector<item_pocket*> pockets = integral_pockets_of(gun);
        REQUIRE(pockets.size() == 1);
        const std::vector<item*> rounds = pockets.front()->all_items_top();
        REQUIRE(rounds.size() == 1);
        const int cell_cost = you.item_reload_cost(gun, *mags[0], -1) / 2;
        const int round_cost = you.item_reload_cost(gun, *rounds[0], 1) / 2;
        REQUIRE(cell_cost > 0);
        REQUIRE(round_cost > 0);
        const int before = you.get_moves();

        REQUIRE(avatar_funcs::unload_item(you, gun, true));

        CHECK(before - you.get_moves() == cell_cost + round_cost);
    }
}

// CDF: P3-6-4d Y5. The refusals stay: the gate (a non-magazine thing is "can't unload") and the
// "isn't loaded" check. The message texts are not asserted: add_msg is stubbed in the tests.
TEST_CASE("unload_gate_still_refuses_non_magazine_items", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    SECTION("a stack of tums: charges, but no ammo type") {
        // CDF: tums (data/json/items/comestibles) is count_by_charges with no ammo type; for such
        // an item ammo_remaining() is its charges, so only the gate stops the loose-ammo branch
        item& pills = you.i_add(item::spawn(itype_tums, calendar::turn, 4));
        REQUIRE(pills.ammo_types().empty());
        REQUIRE(pills.ammo_remaining() > 0);
        const int charges = pills.charges;

        CHECK_FALSE(avatar_funcs::unload_item(you, pills, true));

        CHECK(pills.charges == charges);
    }

    SECTION("a pencil") {
        item& pencil = you.i_add(item::spawn(itype_pencil));
        REQUIRE(pencil.ammo_types().empty());

        CHECK_FALSE(avatar_funcs::unload_item(you, pencil, true));
    }

    SECTION("a gun with nothing in it") {
        detached_ptr<item> gun_ptr = item::spawn(itype_test_multimag_gun);
        item& gun = you.i_add(std::move(gun_ptr));
        REQUIRE(gun.magazines_current().empty());
        const int before = you.get_moves();

        CHECK_FALSE(avatar_funcs::unload_item(you, gun, true));

        CHECK(you.get_moves() == before);
    }
}

// CDF: P3-6-4d Y6. DDA character_inventory.cpp:2828-2832: after a gun unload the aim recoil is
// reset to the maximum and an item with MAG_DESTROY that holds nothing any more is gone.
TEST_CASE("unload_gun_resets_recoil_and_destroys_mag_destroy_items", "[p3][unload][multimag]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    SECTION("recoil") {
        item& gun = add_two_well_gun(you);
        you.recoil = 0;

        REQUIRE(avatar_funcs::unload_item(you, gun, true));

        CHECK(you.recoil == MAX_RECOIL);
    }

    SECTION("MAG_DESTROY") {
        // CDF: no gun or tool in the data has MAG_DESTROY (only belts, 40mm.json:20): the flag is
        // set on the instance; the gun holds one magazine and is empty after the unload
        detached_ptr<item> gun_ptr = item::spawn(itype_test_multimag_gun);
        detached_ptr<item> glock = item::spawn(itype_glockmag);
        REQUIRE(glock->put_in(item::spawn(itype_9mm, calendar::turn, 5), pocket_type::MAGAZINE)
                    .success());
        REQUIRE(gun_ptr->put_in(std::move(glock), pocket_type::MAGAZINE_WELL).success());
        gun_ptr->set_flag(flag_MAG_DESTROY);
        item& gun = you.i_add(std::move(gun_ptr));
        REQUIRE(gun.has_flag(flag_MAG_DESTROY));

        REQUIRE(avatar_funcs::unload_item(you, gun, true));

        // nothing touches the gun from here: it is destroyed
        CHECK(carried_of_type(you, itype_test_multimag_gun).empty());
        const std::vector<item*> mags = carried_of_type(you, itype_glockmag);
        CHECK(mags.size() == 1);
    }

    SECTION("a gun without MAG_DESTROY stays") {
        item& gun = add_two_well_gun(you);

        REQUIRE(avatar_funcs::unload_item(you, gun, true));

        CHECK(carried_of_type(you, itype_test_multimag_gun).size() == 1);
    }
}

// CDF: P3-6-4d Y7. DDA activity_actor.cpp:761 and :935: gunmod_remove unloads the mod with
// bypass_activity, so a mod with several sources is emptied without the "Unload which source?"
// question (a uilist cannot be answered here: asked, it would cancel and the removal would fail).
TEST_CASE("gunmod_remove_unloads_without_a_picker", "[p3][unload][gunmod]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);

    SECTION("a mod with two loaded wells") {
        // CDF: test_p3_two_well_gunmod (data/mods/TEST_DATA/unload_gun_p3.json), pipe_pistol is its
        // mod_targets
        detached_ptr<item> gun_ptr = item::spawn(itype_pipe_pistol);
        REQUIRE(gun_ptr->put_in(item::spawn(itype_test_p3_two_well_gunmod), pocket_type::MOD)
                    .success());
        item& gun = you.i_add(std::move(gun_ptr));
        REQUIRE(gun.gunmods().size() == 1);
        item& mod = *gun.gunmods().front();
        detached_ptr<item> glock = item::spawn(itype_glockmag);
        REQUIRE(glock->put_in(item::spawn(itype_9mm, calendar::turn, 15), pocket_type::MAGAZINE)
                    .success());
        detached_ptr<item> stanag = item::spawn(itype_stanag30);
        REQUIRE(stanag->put_in(item::spawn(itype_556, calendar::turn, 30), pocket_type::MAGAZINE)
                    .success());
        REQUIRE(mod.put_in(std::move(glock), pocket_type::MAGAZINE_WELL).success());
        REQUIRE(mod.put_in(std::move(stanag), pocket_type::MAGAZINE_WELL).success());
        REQUIRE(mod.magazines_current().size() == 2);

        REQUIRE(avatar_funcs::gunmod_remove(you, gun, mod));

        CHECK(gun.gunmods().empty());
        CHECK(carried_of_type(you, itype_test_p3_two_well_gunmod).size() == 1);
        const std::vector<item*> glocks = carried_of_type(you, itype_glockmag);
        const std::vector<item*> stanags = carried_of_type(you, itype_stanag30);
        REQUIRE(glocks.size() == 1);
        REQUIRE(stanags.size() == 1);
        CHECK(glocks.front()->ammo_remaining() == 15);
        CHECK(stanags.front()->ammo_remaining() == 30);
    }

    SECTION("a mod with one loaded well") {
        detached_ptr<item> gun_ptr = item::spawn(itype_pipe_pistol);
        REQUIRE(
            gun_ptr->put_in(item::spawn(itype_test_multimag_aux_gun), pocket_type::MOD).success());
        item& gun = you.i_add(std::move(gun_ptr));
        REQUIRE(gun.gunmods().size() == 1);
        item& mod = *gun.gunmods().front();
        detached_ptr<item> glock = item::spawn(itype_glockmag);
        REQUIRE(glock->put_in(item::spawn(itype_9mm, calendar::turn, 15), pocket_type::MAGAZINE)
                    .success());
        REQUIRE(mod.put_in(std::move(glock), pocket_type::MAGAZINE_WELL).success());
        REQUIRE(mod.magazines_current().size() == 1);

        REQUIRE(avatar_funcs::gunmod_remove(you, gun, mod));

        CHECK(gun.gunmods().empty());
        const std::vector<item*> glocks = carried_of_type(you, itype_glockmag);
        REQUIRE(glocks.size() == 1);
        CHECK(glocks.front()->ammo_remaining() == 15);
    }
}

// CDF: P3-6-4d (carried from the 6-4c audit). DDA Character::add_or_drop_with_msg( ..., avoid ):
// an item taken out of a container must not be put back into the container it left (it would
// merge with itself, item.cpp merge_charges). The avatar wears nothing but the hiking pack
// (data/json/items/armor/storage.json:27), so the pack is the only place the pencil can go; with
// the pack named in `avoid` the pencil goes to the hands, without it the pack takes it back.
TEST_CASE("add_or_drop_with_msg_avoids_the_named_container", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    REQUIRE(!you.wear_item(item::spawn(itype_backpack_hiking), false));
    item* pack = nullptr;
    for (item* const& worn_item : you.worn) {
        if (worn_item->typeId() == itype_backpack_hiking) { pack = worn_item; }
    }
    REQUIRE(pack != nullptr);
    REQUIRE(pack->put_in(item::spawn(itype_pencil), pocket_type::CONTAINER).success());
    const std::vector<item*> inside = pack->all_items_top(pocket_type::CONTAINER);
    REQUIRE(inside.size() == 1);
    item* const pencil = inside.front();

    // the item leaves the pack as in unload_item
    detached_ptr<item> taken;
    pack->contents.remove_top_items_with([&](detached_ptr<item>&& e) {
        if (&*e == pencil) {
            taken = std::move(e);
            return detached_ptr<item>();
        }
        return std::move(e);
    });
    REQUIRE(taken);
    REQUIRE(pack->contents.empty());

    SECTION("with the pack in avoid, the pencil is not put back into it") {
        taken = avatar_funcs::add_or_drop_with_msg(you, std::move(taken), true, pack);

        CHECK(!taken);
        CHECK(pencil->parent_item() != pack);
        CHECK(pack->contents.empty());
    }

    SECTION("without avoid, the pack takes it back") {
        taken = avatar_funcs::add_or_drop_with_msg(you, std::move(taken), true);

        CHECK(!taken);
        CHECK(pencil->parent_item() == pack);
    }
}

// CDF: P3-6-4d, BN-only caller check (AUTO-199). A gun that keeps its rounds as direct charges (no
// magazine, no well: sw_619, data/json/items/ranged/pistols.json, takes 38_special) is neither a
// magazine source nor a drainable integral pocket: unload_item still reaches the loose-ammo branch
// and gives the rounds back, as before.
TEST_CASE("unload_gun_with_direct_charges_still_returns_its_rounds", "[p3][unload]") {
    clear_all_state();
    avatar& you = get_avatar();
    you.wear_item(item::spawn(itype_backpack), false);
    item& gun = you.i_add(item::spawn("sw_619", calendar::start_of_cataclysm, 0));
    gun.ammo_set(itype_id("38_special"), 6);
    REQUIRE(gun.ammo_remaining() == 6);
    REQUIRE(gun.magazines_current().empty());
    REQUIRE(gun.magazine_current() == nullptr);

    REQUIRE(avatar_funcs::unload_item(you, gun));

    CHECK(!you.activity);
    CHECK(gun.ammo_remaining() == 0);
    CHECK(you.charges_of(itype_id("38_special")) == 6);
}
