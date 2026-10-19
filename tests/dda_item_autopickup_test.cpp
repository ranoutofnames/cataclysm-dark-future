// DDA source: tests/item_autopickup_test.cpp (DDA commit b4b62254ae2), P2-8b port.
// TEST_CASEs ported (7): auto_pickup_should_recognize_container_content,
// auto_pickup_should_improve_your_life, auto_pickup_should_consider_item_rigidness_and_seal,
// auto_pickup_should_respect_volume_and_weight_limits, auto_pickup_should_consider_item_ownership,
// auto_pickup_item_rule_cache, auto_pickup_should_not_implicitly_pickup_corpses.
// Not ported: adjacent_auto_pickup_diverse_items_performance ([.][performance], needs ids and
// avatar_action::move benchmarks) and auto_pickup_leaves_reserved_items_alone (CDF has no
// craft_reservation, see the CDF comment at the end of the file).
// Translated for CDF (same rules as dda_item_pickup_test.cpp):
//  * No item_location / map_cursor: fixtures use item pointers and references on the tile.
//    unique_item keeps a detached prototype and spawns copies on the map (item::spawn( const item &
//    )).
//  * Pickup entry point: pickup::pick_up( pos, -1 ) (what Pickup::autopickup is in CDF) followed by
//    process_activity.
//  * The test items that CDF does not have are replaced by CDF items (see "CDF:" at each use) and
//  the
//    numbers re-derived from CDF item data. Functionality is tested, not DDA balance.
#include "auto_pickup.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "detached_ptr.h"
#include "enums.h"
#include "item.h"
#include "map/map.h"
#include "map_helpers.h"
#include "options.h"
#include "options_helpers.h"
#include "pickup.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

static const itype_id itype_aspirin("aspirin");
static const itype_id itype_backpack("backpack");
static const itype_id itype_bag_body_bag("bag_body_bag");
static const itype_id itype_bag_plastic("bag_plastic");
static const itype_id itype_battery("battery");
static const itype_id itype_bottle_plastic("bottle_plastic");
static const itype_id itype_box_cigarette("box_cigarette");
static const itype_id itype_box_small("box_small");
static const itype_id itype_can_medium("can_medium");
static const itype_id itype_can_tuna("can_tuna");
static const itype_id itype_candy2("candy2");
static const itype_id itype_candycigarette("candycigarette");
static const itype_id itype_cig("cig");
static const itype_id itype_codeine("codeine");
static const itype_id itype_corpse("corpse");
static const itype_id itype_light_battery_cell("light_battery_cell");
static const itype_id itype_marble("marble");
static const itype_id itype_meat_canned("meat_canned");
static const itype_id itype_paper("paper");
static const itype_id itype_pebble("pebble");
static const itype_id itype_rolling_paper("rolling_paper");
static const itype_id itype_storage_battery("storage_battery");
static const itype_id itype_water_clean("water_clean");
static const itype_id itype_wrapper("wrapper");

// CDF: substitutes for ids that CDF lacks (checked in data/json): bottle_plastic_pill_prescription,
// diving_flashlight_small_hipower, 1l_bronze, money_one/five/ten, wallet_leather.
// small plastic bottle (250 ml, seals, watertight), the closest small bottle to a prescription
// bottle
static const itype_id itype_bottle_plastic_small("bottle_plastic_small");
// household flashlight: tool taking light_battery_cell through its magazine well
static const itype_id itype_flashlight("flashlight");
// 3.75 kg, 750 ml iron ore: deadweight that is over the weight limit but not the volume limit, like
// the DDA 1 L bronze ingot. (sheet_metal, 6 kg, has a longest side of 100 cm: a backpack holding it
// does not fit the 40 cm pocket of the worn backpack, so the whole-backpack pickup would be
// refused.)
static const itype_id itype_iron_ore("iron_ore");
// non-rigid small cloth container (250 ml, rigid: false) in place of the leather wallet
static const itype_id itype_bag_canvas_small("bag_canvas_small");
// two paper items of different names in place of the money bills (money_pile 2.5 L, money_bundle
// 250 ml)
static const itype_id itype_money_pile("money_pile");
static const itype_id itype_money_bundle("money_bundle");

namespace {

class unique_item {
private:
    // CDF: a detached prototype that never goes to the map; spawn_item puts a copy of it there
    // (DDA keeps an item value and add_item copies it).
    detached_ptr<item> instance;

public:
    // Create a simple wrapper for given item with random UID.
    explicit unique_item(const item& from, bool no_uid = false): instance(item::spawn(from)) {
        if (get_uid().empty() && !no_uid) { instance->set_var("uid", random_string(10)); }
    }
    // Construct item with given type and quantity (-1 is the default charges of CDF's item ctor).
    explicit unique_item(const itype_id& type, int quantity = -1, bool no_uid = false)
        : instance(item::spawn(type, calendar::turn, quantity)) {
        if (!no_uid) { instance->set_var("uid", random_string(10)); }
    }
    // Construct item with given type and list of items contained inside item pocket.
    unique_item(const itype_id& type, const std::vector<const unique_item*>& content)
        : instance(item::spawn(type, calendar::turn)) {
        instance->set_var("uid", random_string(10));
        for (const unique_item* entry : content) {
            instance->force_insert_item(*entry->get(), pocket_type::CONTAINER);
        }
    }
    // Returns the UID value assigned to this item.
    std::string get_uid() const { return instance->get_var("uid"); }
    // Returns a pointer to item instance.
    const item* get() const { return &*instance; }
    // Set the owner of this item instance to given avatar.
    void set_owner(avatar& who) { instance->set_owner(who); }
    // Returns true if both items have the same UID string value.
    bool is_same_item(const item* with) const {
        std::string item_uid = with->get_var("uid");
        std::string this_uid = get_uid();
        // if items don't have UID then compare types and charges
        if (this_uid.empty() && item_uid.empty()) {
            return instance->typeId() == with->typeId() && instance->charges == with->charges;
        }
        return this_uid == item_uid;
    }
    // Spawns a copy of this item in the given location on the map.
    // Returns true if item was spawned and false otherwise.
    bool spawn_item(const tripoint_bub_ms& where) {
        get_map().add_item(where, item::spawn(*instance));
        CHECK_FALSE(get_uid().empty());
        return is_on_ground(where);
    }
    // Returns item instance of this unique item in the given location
    item& find_on_ground(const tripoint_bub_ms& where) const {
        for (item* it : get_map().i_at(where)) {
            if (is_same_item(it)) { return *it; }
        }
        return null_item_reference();
    }
    // Returns true if this item if found in the given map stack
    bool is_on_ground(const tripoint_bub_ms& where) const {
        return !find_on_ground(where).is_null();
    }
    // Returns the item that matches this item in given container or null.
    const item& find_in_container(const item& where) const {
        for (const item* it : where.all_items_top()) {
            if (is_same_item(it)) { return *it; }
        }
        return null_item_reference();
    }
};

} // namespace

// Add the given item to auto-pickup character rules and check rules.
static void add_autopickup_rule(const item* what, bool include) {
    auto_pickup::player_settings& rules = get_auto_pickup();

    rules.add_rule(what, include);
    // CDF: DDA also REQUIREs rules.has_rule( what ). CDF's has_rule compares tname( 1 ) (with
    // prefix) with the stored tname( 1, false ), which can differ for items with contents, so only
    // the resulting rule state is checked.

    std::string item_name = what->tname(1, false);
    rule_state pickup_state = rules.base_settings::check_item(item_name);
    REQUIRE(pickup_state == (include ? RULE_WHITELISTED : RULE_BLACKLISTED));
}

// Add the given items to auto-pickup character rules and check rules.
static void add_autopickup_rules(std::map<const unique_item*, bool> what) {
    for (const std::pair<const unique_item* const, bool>& entry : what) {
        add_autopickup_rule(entry.first->get(), entry.second);
    }
}

// Add the given items to auto-pickup character rules and check rules
static void add_autopickup_rules(const std::vector<unique_item*>& what, bool include) {
    for (unique_item* it : what) { add_autopickup_rule(it->get(), include); }
}

// Simulate character moving over a tile that contains items.
static void simulate_auto_pickup(const tripoint_bub_ms& pos, avatar& they) {
    // CDF: Pickup::autopickup( pos ) is pickup::pick_up( pos, -1 )
    pickup::pick_up(pos, -1);
    process_activity(they);
}

// Require that the given list of items be found contained in item.
static void expect_to_find(const item& in, const std::vector<const unique_item*>& what) {
    CHECK_FALSE(in.is_null());
    CHECK(in.all_items_top().size() == what.size());

    // make sure all items on the list have been picked up
    // CDF: item has no has_item_with( predicate ); the top level items are searched (the size check
    // above makes this equivalent for flat contents).
    for (const unique_item* entry : what) {
        const std::vector<const item*> top = in.all_items_top();
        REQUIRE(std::any_of(top.begin(), top.end(), [entry](const item* it) {
            return entry->is_same_item(it);
        }));
    }
}

// Reset all variables and settings needed to do a clean test.
static void clear_everything() {
    avatar& they = get_avatar();
    clear_all_state();
    // CDF: a bare character, no DEBUG_STORAGE (as dda_item_pickup_test.cpp); the test wears a
    // backpack
    clear_character(they, false);
    clear_map();

    auto_pickup::player_settings& rules = get_auto_pickup();
    rules.clear_character_rules();
    // this will clear the cache and recreate the map
    rules.base_settings::check_item(std::string());

    // reset all autopickup options to default values
    options_manager& options = get_options();
    options.get_option("AUTO_PICKUP_WEIGHT_LIMIT").setValue("0");
    // CDF: the option is called AUTO_PICKUP_VOL_LIMIT
    options.get_option("AUTO_PICKUP_VOL_LIMIT").setValue("0");
    options.get_option("AUTO_PICKUP_OWNED").setValue("false");
}

// Wear a backpack and return it (DDA: *they.wear_item( item( itype_backpack ) )).
static item& wear_backpack(avatar& they) {
    detached_ptr<item> bag = item::spawn(itype_backpack, calendar::turn);
    item& backpack = *bag;
    // CDF: wear_item returns the item if it was not worn
    detached_ptr<item> not_worn = they.wear_item(std::move(bag), false);
    REQUIRE(!not_worn);
    REQUIRE(they.has_item(backpack));
    return backpack;
}

TEST_CASE("auto_pickup_should_recognize_container_content", "[autopickup][item]") {
    avatar& they = get_avatar();
    map& here = get_map();
    clear_everything();
    // CDF: add_rule asks to enable AUTO_PICKUP through a query when the option is off
    override_option auto_pickup_on("AUTO_PICKUP", "true");

    // this is where items will be picked up from
    const tripoint_bub_ms ground = they.bub_pos();

    // wear backpack and store item reference
    item& backpack = wear_backpack(they);

    GIVEN("avatar is about to walk over a tile filled with items") {
        // make sure no items exist on the ground before we add them
        REQUIRE(here.i_at(ground).empty());

        // add random items to the tile on the ground
        here.add_item(ground, item::spawn(itype_marble, calendar::turn));
        here.add_item(ground, item::spawn(itype_pebble, calendar::turn));

        // codeine (WL)
        WHEN("there is an item on the ground whitelisted in auto-pickup rules") {
            unique_item item_codeine = unique_item(itype_codeine);
            REQUIRE(item_codeine.spawn_item(ground));
            add_autopickup_rule(item_codeine.get(), true);

            THEN("the whitelisted item should be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(backpack, {&item_codeine});
            }
        }
        // small plastic bottle > (WL)aspirin
        WHEN(
            "there is a container on the ground containing only items whitelisted in auto-pickup rules") {
            unique_item item_aspirin = unique_item(itype_aspirin);
            // CDF: bottle_plastic_small instead of bottle_plastic_pill_prescription
            unique_item item_prescription_bottle =
                unique_item(itype_bottle_plastic_small, {&item_aspirin});
            REQUIRE(item_prescription_bottle.spawn_item(ground));
            add_autopickup_rule(item_aspirin.get(), true);

            THEN("the container along with its contents should be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(backpack, {&item_prescription_bottle});
                expect_to_find(
                    item_prescription_bottle.find_in_container(backpack), {&item_aspirin});
            }
        }
        // plastic bag > paper, paper wrapper (WL) > chocolate candy
        WHEN(
            "there is a container on the ground with a deeply nested item whitelisted in auto-pickup rules") {
            const unique_item item_paper = unique_item(itype_paper);
            const unique_item item_chocolate_candy = unique_item(itype_candy2);
            const unique_item item_paper_wrapper =
                unique_item(itype_wrapper, {&item_chocolate_candy});
            unique_item item_plastic_bag =
                unique_item(itype_bag_plastic, {&item_paper, &item_paper_wrapper});
            REQUIRE(item_plastic_bag.spawn_item(ground));
            add_autopickup_rule(item_chocolate_candy.get(), true);

            THEN("only the deeply nested item should be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(backpack, {&item_paper_wrapper});
                expect_to_find(
                    item_paper_wrapper.find_in_container(backpack), {&item_chocolate_candy});
                // make sure the item has remained on the ground
                REQUIRE(item_plastic_bag.is_on_ground(ground));
            }
        }
    }
}

TEST_CASE("auto_pickup_should_improve_your_life", "[autopickup][item]") {
    avatar& they = get_avatar();
    map& here = get_map();
    clear_everything();
    override_option auto_pickup_on("AUTO_PICKUP", "true");

    // this is where items will be picked up from
    const tripoint_bub_ms ground = they.bub_pos();

    // wear backpack and store item reference
    item& backpack = wear_backpack(they);

    // flashlight > light battery (WL)
    WHEN(
        "there is a powered tool on the ground loaded with a light battery whitelisted in auto-pickup rules") {
        // CDF: flashlight (tool, magazine light_battery_cell) instead of
        // diving_flashlight_small_hipower
        detached_ptr<item> flashlight_ptr = item::spawn(itype_flashlight, calendar::turn);
        item& item_flashlight = *flashlight_ptr;
        detached_ptr<item> battery_ptr = item::spawn(itype_light_battery_cell, calendar::turn);
        item& battery_on_ground = *battery_ptr;
        here.add_item(ground, std::move(battery_ptr));
        REQUIRE_FALSE(battery_on_ground.is_null());

        // insert light battery into flashlight by reloading the item
        // CDF: reload takes the ammo item itself, not an item_location
        REQUIRE(item_flashlight.reload(they, battery_on_ground, 1));
        // battery was removed from ground so update variable
        const item* item_light_battery = item_flashlight.magazine_current();
        REQUIRE(item_light_battery != nullptr);

        unique_item uitem_light_battery = unique_item(*item_light_battery, true);
        unique_item uitem_flashlight = unique_item(item_flashlight);
        REQUIRE(uitem_flashlight.spawn_item(ground));

        // we want to remove and pickup the battery from the flashlight
        add_autopickup_rule(uitem_light_battery.get(), true);

        THEN("the light battery from the tool should be picked up") {
            simulate_auto_pickup(ground, they);
            expect_to_find(backpack, {&uitem_light_battery});
            // make sure the item has remained on the ground
            REQUIRE(uitem_flashlight.is_on_ground(ground));
        }
    }
}

TEST_CASE("auto_pickup_should_consider_item_rigidness_and_seal", "[autopickup][item]") {
    avatar& they = get_avatar();
    clear_everything();
    override_option auto_pickup_on("AUTO_PICKUP", "true");

    // this is where items will be picked up from
    const tripoint_bub_ms ground = they.bub_pos();

    // wear backpack and store item reference
    item& backpack = wear_backpack(they);

    // canvas bag (WL) > pile of money, money bundle (WL), paper
    // CDF: bag_canvas_small (rigid: false) for the leather wallet, money_pile / money_bundle /
    // paper for the one, five and ten dollar bills (none of them is in the CDF data).
    WHEN(
        "there is a non-rigid whitelisted container on the ground with three items, one whitelisted") {
        unique_item item_money_one = unique_item(itype_money_pile);
        unique_item item_money_five = unique_item(itype_money_bundle);
        unique_item item_money_ten = unique_item(itype_paper);
        unique_item item_leather_wallet = unique_item(
            itype_bag_canvas_small, {&item_money_one, &item_money_five, &item_money_ten});
        REQUIRE(item_leather_wallet.spawn_item(ground));
        add_autopickup_rules({&item_leather_wallet, &item_money_five}, true);

        THEN(
            "the container should be picked up and non-whitelisted items should be dropped on the ground") {
            simulate_auto_pickup(ground, they);
            expect_to_find(backpack, {&item_leather_wallet});
            expect_to_find(item_leather_wallet.find_in_container(backpack), {&item_money_five});
            // make sure the item has remained on the ground
            REQUIRE(item_money_one.is_on_ground(ground));
        }
    }
    // small cardboard box (WL) > paper, chocolate candy (BL), marble
    WHEN(
        "there is a rigid whitelisted container on the ground with three items (one blacklisted)") {
        unique_item item_paper = unique_item(itype_paper);
        unique_item item_chocolate_candy = unique_item(itype_candy2);
        unique_item item_marble = unique_item(itype_marble);
        unique_item cardboard_box =
            unique_item(itype_box_small, {&item_paper, &item_chocolate_candy, &item_marble});
        REQUIRE(cardboard_box.spawn_item(ground));
        add_autopickup_rules({{&cardboard_box, true}, {&item_chocolate_candy, false}});
        THEN(
            "the rigid container should be picked up and blacklisted items should be dropped on the ground") {
            simulate_auto_pickup(ground, they);
            expect_to_find(backpack, {&cardboard_box});
            expect_to_find(cardboard_box.find_in_container(backpack), {&item_paper, &item_marble});
            // make sure the item has remained on the ground
            REQUIRE(item_chocolate_candy.is_on_ground(ground));
        }
    }
    // plastic bottle > clean water (2)(WL)
    WHEN(
        "there is a rigid blacklisted container on the ground with liquid whitelisted in auto-pickup rules") {
        // construct and fill bottle with clean water
        detached_ptr<item> bottle_ptr = item::spawn(itype_bottle_plastic, calendar::turn);
        item& item_plastic_bottle = *bottle_ptr;
        unique_item item_clean_water = unique_item(itype_water_clean, 2);
        REQUIRE(item_plastic_bottle.fill_with(*item_clean_water.get()) == 2);

        // spawn bottle filled with clean water in the world
        unique_item item_bottled_water = unique_item(item_plastic_bottle);
        REQUIRE(item_bottled_water.spawn_item(ground));

        add_autopickup_rules({{&item_clean_water, true}, {&item_bottled_water, false}});
        THEN("the liquid should not be picked up from the container") {
            simulate_auto_pickup(ground, they);
            expect_to_find(backpack, {});
            // make sure the item has remained on the ground
            REQUIRE(item_bottled_water.is_on_ground(ground));
        }
    }
    // small tin can (sealed) > canned tuna fish (WL), canned meat
    WHEN(
        "there is a sealed container on the ground containing items whitelisted in auto-pickup rules") {
        detached_ptr<item> can_ptr = item::spawn(itype_can_medium, calendar::turn);
        item& item_medium_tin_can = *can_ptr;
        unique_item item_canned_tuna = unique_item(itype_can_tuna, -1, true);
        unique_item item_canned_meat = unique_item(itype_meat_canned, -1, true);

        // insert items inside can and seal it
        item_medium_tin_can.force_insert_item(*item_canned_tuna.get(), pocket_type::CONTAINER);
        item_medium_tin_can.force_insert_item(*item_canned_meat.get(), pocket_type::CONTAINER);
        // CDF: can_medium unseals_into can_medium_unsealed, i.e. the can is sealed by its type and
        // putting items in converts it to the opened can (item::on_contents_changed), so it is
        // converted back as item::in_container does; seal() then has nothing to do and the sealed
        // state is REQUIREd instead of seal()'s result.
        item_medium_tin_can.convert(itype_can_medium);
        item_medium_tin_can.seal();
        REQUIRE(item_medium_tin_can.all_pockets_sealed());

        unique_item item_sealed_tuna = unique_item(item_medium_tin_can);
        REQUIRE(item_sealed_tuna.spawn_item(ground));

        add_autopickup_rule(item_canned_tuna.get(), true);
        THEN("the container should be picked up instead of whitelisted items") {
            simulate_auto_pickup(ground, they);
            expect_to_find(backpack, {&item_sealed_tuna});
            // make sure the item seal was not broken
            REQUIRE(item_sealed_tuna.find_in_container(backpack).all_pockets_sealed());
        }
    }
    // small tin can (sealed) > canned tuna fish (WL), canned meat
    WHEN("there is a sealed container on the ground containing no whitelisted items") {
        detached_ptr<item> can_ptr = item::spawn(itype_can_medium, calendar::turn);
        item& item_medium_tin_can = *can_ptr;
        detached_ptr<item> bottle_ptr = item::spawn(itype_bottle_plastic, calendar::turn);
        item& item_bottle_plastic = *bottle_ptr;
        unique_item item_canned_tuna = unique_item(itype_can_tuna, -1, true);
        unique_item item_canned_meat = unique_item(itype_meat_canned, -1, true);

        // insert items inside can and seal it
        item_medium_tin_can.force_insert_item(*item_canned_tuna.get(), pocket_type::CONTAINER);
        item_medium_tin_can.force_insert_item(*item_canned_meat.get(), pocket_type::CONTAINER);
        // CDF: converted back to the sealed type, see above
        item_medium_tin_can.convert(itype_can_medium);
        item_medium_tin_can.seal();
        REQUIRE(item_medium_tin_can.all_pockets_sealed());

        unique_item item_sealed_tuna = unique_item(item_medium_tin_can);
        REQUIRE(item_sealed_tuna.spawn_item(ground));
        // autopickup something other than the sealed items
        unique_item item_red_herring = unique_item(item_bottle_plastic);
        add_autopickup_rule(item_red_herring.get(), true);
        THEN("nothing should be picked up") {
            simulate_auto_pickup(ground, they);
            expect_to_find(backpack, {});
            // make sure the item seal was not broken
            REQUIRE(item_sealed_tuna.is_on_ground(ground));
            REQUIRE(item_sealed_tuna.find_on_ground(ground).all_pockets_sealed());
        }
    }
}

TEST_CASE("auto_pickup_should_respect_volume_and_weight_limits", "[autopickup][item]") {
    avatar& they = get_avatar();
    clear_everything();
    override_option auto_pickup_on("AUTO_PICKUP", "true");

    // this is where items will be picked up from
    const tripoint_bub_ms ground = they.bub_pos();

    // wear backpack and store item reference
    item& backpack = wear_backpack(they);

    // backpack > steel sheet (WL), cigarette (WL), paper (WL)
    // CDF: iron_ore (3.75 kg, 750 ml) stands in for the 1 L bronze ingot (1l_bronze is not in CDF).
    // CDF's AUTO_PICKUP_WEIGHT_LIMIT ranges 0..20 (DDA 0..100), so the weight limit is 20 * 50 g =
    // 1 kg (DDA 80 = 4 kg) and the volume limit stays 20 * 50 ml = 1 L: the ore exceeds only the
    // weight limit, the cigarette (about 83 ml, a few g) and paper (15 ml, 3 g) exceed neither. The
    // ground backpack itself (2 L plus contents, over 4 kg) exceeds the weight limit.
    GIVEN("there is a container with some items that exceed volume or weight limit") {
        options_manager& options = get_options();
        options_manager::cOpt& ap_weight_limit = options.get_option("AUTO_PICKUP_WEIGHT_LIMIT");
        // CDF: AUTO_PICKUP_VOL_LIMIT (DDA: AUTO_PICKUP_VOLUME_LIMIT)
        options_manager::cOpt& ap_volume_limit = options.get_option("AUTO_PICKUP_VOL_LIMIT");

        unique_item item_ingot_deadweight = unique_item(itype_iron_ore, 1);
        unique_item item_cigarette = unique_item(itype_cig);
        unique_item item_paper = unique_item(itype_paper);
        unique_item item_backpack =
            unique_item(itype_backpack, {&item_ingot_deadweight, &item_cigarette, &item_paper});
        REQUIRE(item_backpack.spawn_item(ground));
        add_autopickup_rules({&item_ingot_deadweight, &item_cigarette, &item_paper}, true);
        WHEN("auto pickup limit game options are disabled") {
            ap_weight_limit.setValue("0");
            ap_volume_limit.setValue("0");
            THEN("all items should be picked up regardless of weight and volume") {
                simulate_auto_pickup(ground, they);
                expect_to_find(backpack, {&item_backpack});
                expect_to_find(item_backpack.find_in_container(backpack),
                               {&item_ingot_deadweight, &item_cigarette, &item_paper});
            }
        }
        WHEN("auto pickup limit game options are not disabled") {
            // CDF: the option maximum is 20 (DDA: 80 = 4.0 kilograms), so 1.0 kilogram
            ap_weight_limit.setValue("20");
            REQUIRE(get_option<int>("AUTO_PICKUP_WEIGHT_LIMIT") == 20);

            ap_volume_limit.setValue("20"); // 1.0 liter
            REQUIRE(get_option<int>("AUTO_PICKUP_VOL_LIMIT") == 20);
            THEN("only items that do not exceed volume and weight limit should be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(backpack, {&item_cigarette, &item_paper});
                expect_to_find(item_backpack.find_on_ground(ground), {&item_ingot_deadweight});
                // make sure excluded items were not dropped on the ground
                REQUIRE_FALSE(item_ingot_deadweight.is_on_ground(ground));
            }
        }
    }

    GIVEN("a whitelist rule for picking up battery charges") {
        detached_ptr<item> store_bat_ptr = item::spawn(itype_storage_battery, calendar::turn);
        item& item_store_bat = *store_bat_ptr;
        detached_ptr<item> bat_ptr = item::spawn(itype_battery, calendar::turn, 10);
        item& item_bat = *bat_ptr;
        item_store_bat.ammo_set(itype_battery, 10);
        REQUIRE(!item_store_bat.all_items_top().empty());
        REQUIRE(item_store_bat.all_items_top().front()->charges == 10);
        unique_item item_storage_battery = unique_item(item_store_bat);
        unique_item item_battery = unique_item(item_bat);
        REQUIRE(item_storage_battery.spawn_item(ground));
        add_autopickup_rules({&item_storage_battery}, false);
        add_autopickup_rules({&item_battery}, true);
        THEN("ignore battery charges") {
            simulate_auto_pickup(ground, they);
            expect_to_find(backpack, {});
            CHECK(item_storage_battery.is_on_ground(ground));
            const item& on_ground = item_storage_battery.find_on_ground(ground);
            REQUIRE(!on_ground.is_null());
            REQUIRE(!on_ground.all_items_top().empty());
            REQUIRE(on_ground.all_items_top().front()->charges == 10);
        }
    }
}

TEST_CASE("auto_pickup_should_consider_item_ownership", "[autopickup][item]") {
    avatar& they = get_avatar();
    clear_everything();
    override_option auto_pickup_on("AUTO_PICKUP", "true");

    // this is where items will be picked up from
    const tripoint_bub_ms ground = they.bub_pos();

    // wear backpack and store item reference
    item& backpack = wear_backpack(they);

    // candy cigarette(WL)
    // pack(WL) > cigarette (WL), rolling paper (WL)
    GIVEN("there is a container with some items and an item outside it on the ground") {
        options_manager::cOpt& autopickup_owned = get_options().get_option("AUTO_PICKUP_OWNED");
        // make sure the autopickup owned option is disabled
        if (autopickup_owned.value_as<bool>()) { autopickup_owned.setValue("false"); }
        unique_item item_candy_cigarette = unique_item(itype_candycigarette);
        // CDF: box_cigarette holds 84 ml; a default stack of 20 cigarettes (about 83 ml) plus the
        // rolling paper (15 ml) overflows it when the pack is picked up, so the cigarettes are a
        // smaller stack.
        unique_item item_cigarette = unique_item(itype_cig, 5);
        unique_item item_rolling_paper = unique_item(itype_rolling_paper);
        unique_item item_pack =
            unique_item(itype_box_cigarette, {&item_cigarette, &item_rolling_paper});
        WHEN("only the item outside the container is owned by avatar") {
            item_candy_cigarette.set_owner(they);
            REQUIRE(item_candy_cigarette.get()->is_owned_by(they));

            REQUIRE(item_candy_cigarette.spawn_item(ground));
            REQUIRE(item_pack.spawn_item(ground));

            add_autopickup_rules(
                {&item_candy_cigarette, &item_cigarette, &item_rolling_paper}, true);
            THEN("only the container should be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(backpack, {&item_pack});
                expect_to_find(
                    item_pack.find_in_container(backpack), {&item_cigarette, &item_rolling_paper});
                // make sure the item has remained on the ground
                REQUIRE(item_candy_cigarette.is_on_ground(ground));
            }
        }
        WHEN("only the container is owned by avatar") {
            item_pack.set_owner(they);
            REQUIRE(item_pack.get()->is_owned_by(they));

            REQUIRE(item_candy_cigarette.spawn_item(ground));
            REQUIRE(item_pack.spawn_item(ground));

            add_autopickup_rules({&item_candy_cigarette, &item_pack}, true);
            THEN("the item outside the container should be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(backpack, {&item_candy_cigarette});
                expect_to_find(
                    item_pack.find_on_ground(ground), {&item_cigarette, &item_rolling_paper});
            }
        }
    }
}

TEST_CASE("auto_pickup_item_rule_cache", "[autopickup][item]") {
    clear_everything();
    override_option auto_pickup_on("AUTO_PICKUP", "true");
    auto_pickup::player_settings& rules = get_auto_pickup();
    detached_ptr<item> marble_ptr = item::spawn(itype_marble, calendar::turn);
    item& marble = *marble_ptr;

    CHECK(rules.check_item(marble) == RULE_NONE);

    rules.add_rule(&marble, true);
    CHECK(rules.check_item(marble) == RULE_WHITELISTED);

    rules.remove_rule(&marble);
    CHECK(rules.check_item(marble) == RULE_NONE);

    rules.add_rule(&marble, false);
    CHECK(rules.check_item(marble) == RULE_BLACKLISTED);
}

TEST_CASE("auto_pickup_should_not_implicitly_pickup_corpses", "[autopickup][item]") {
    avatar& they = get_avatar();
    clear_everything();
    override_option auto_pickup_on("AUTO_PICKUP", "true");

    // this is where items will be picked up from
    const tripoint_bub_ms ground = they.bub_pos();

    // wield body bag and store item reference
    // CDF: no set_wielded_item / get_wielded_item; set_primary_weapon puts the bag in the hands and
    // primary_weapon() is the handle (returns the previously wielded item, none after clear)
    detached_ptr<item> previous = they.set_primary_weapon(
        item::spawn(itype_bag_body_bag, calendar::turn));
    item& body_bag = they.primary_weapon();
    REQUIRE_FALSE(body_bag.is_null());
    REQUIRE(body_bag.typeId() == itype_bag_body_bag);

    GIVEN("there is a generic corpse on the ground") {
        unique_item item_corpse = unique_item(itype_corpse);
        REQUIRE(item_corpse.spawn_item(ground));

        WHEN("the corpse is whitelisted in auto-pickup rules") {
            add_autopickup_rule(item_corpse.get(), true);
            THEN("the corpse should be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(body_bag, {&item_corpse});
            }
        }
        WHEN("the corpse is NOT whitelisted in auto-pickup rules") {
            THEN("the corpse should NOT be picked up") {
                simulate_auto_pickup(ground, they);
                expect_to_find(body_bag, {});
            }
            WHEN("the corpse contains whitelisted items") {
                unique_item item_cigarette = unique_item(itype_cig);
                unique_item item_rolling_paper = unique_item(itype_rolling_paper);

                item& found = item_corpse.find_on_ground(ground);
                REQUIRE(!found.is_null());
                // CDF: a corpse has a CORPSE pocket (item_contents.cpp), not a CONTAINER pocket
                found.force_insert_item(*item_cigarette.get(), pocket_type::CORPSE);
                found.force_insert_item(*item_rolling_paper.get(), pocket_type::CORPSE);

                add_autopickup_rules({&item_cigarette, &item_rolling_paper}, true);
                THEN("whitelisted items should be picked up without the corpse") {
                    simulate_auto_pickup(ground, they);
                    expect_to_find(body_bag, {&item_cigarette, &item_rolling_paper});
                }
            }
        }
    }
}

// CDF: DDA TEST_CASE "auto_pickup_leaves_reserved_items_alone" ([autopickup][item][reservation]) is
// not ported. It drives craft_reservation (get_craft_reservations().set( rec )), a subsystem CDF
// does not have; P2-8a did not port the two craft_reservation guards of auto_pickup.cpp either.
