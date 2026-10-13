#include "catch/catch.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"

// P1a acceptance test: BN (savegame v29) item contents load into pockets with
// nothing lost, and the v30 pocket format round-trips.

namespace
{

auto load_item(const std::string& json) -> detached_ptr<item> {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    return item::spawn(jsin);
}

auto save_item(const item& it) -> std::string {
    std::ostringstream buffer;
    JsonOut jsout(buffer);
    it.serialize(jsout);
    return buffer.str();
}

// Sorted, recursive description of an item: ids, charges and nesting.
auto signature(const item& it) -> std::string {
    std::vector<std::string> inner;
    for (const item* e : it.contents.all_items_top()) { inner.push_back(signature(*e)); }
    std::ranges::sort(inner);
    std::string ret = it.typeId().str();
    if (it.charges != 0) { ret += "x" + std::to_string(it.charges); }
    if (!inner.empty()) {
        ret += "(";
        for (size_t i = 0; i < inner.size(); ++i) { ret += (i > 0 ? "," : "") + inner[i]; }
        ret += ")";
    }
    return ret;
}

auto count_items(const item& it) -> int {
    int n = 1;
    for (const item* e : it.contents.all_items_top()) { n += count_items(*e); }
    return n;
}

auto pocket_of(const item& container, const std::string& id) -> pocket_type {
    for (const item* e : container.contents.all_items_top()) {
        if (e->typeId().str() == id) { return container.contents.pocket_of(e)->get_type(); }
    }
    FAIL("no " << id << " in " << container.typeId().str());
    return pocket_type::LAST;
}

struct fixture {
    std::string name;
    std::string json;
    std::string expected;
    int count;
};

const std::vector<fixture>& bn_v29_fixtures() {
    static const std::vector<fixture> fixtures = {
        {"BN object format", R"({"typeid":"canteen","contents":{"items":[{"typeid":"water_clean","charges":2}]}})",
         "canteen(water_cleanx2)", 2},
        {"pre-BN array format", R"({"typeid":"canteen","contents":[{"typeid":"water_clean","charges":2}]})",
         "canteen(water_cleanx2)", 2},
        {"gun with magazine and gunmods",
         R"({"typeid":"m4a1","contents":{"items":[
             {"typeid":"stanag30","contents":{"items":[{"typeid":"556","charges":30}]}},
             {"typeid":"red_dot_sight"},
             {"typeid":"shoulder_strap"}]}})",
         "m4a1(red_dot_sight,shoulder_strap,stanag30(556x30))", 5},
        {"holster with a pistol", R"({"typeid":"holster","contents":{"items":[{"typeid":"glock_19"}]}})",
         "holster(glock_19)", 2},
        {"bandolier", R"({"typeid":"bandolier_shotgun","contents":{"items":[{"typeid":"shot_00","charges":12}]}})",
         "bandolier_shotgun(shot_00x12)", 2},
        {"sealed can", R"({"typeid":"can_drink","contents":{"items":[{"typeid":"water_clean","charges":1}]}})",
         "can_drink(water_cleanx1)", 2},
        {"nested backpack, bottle, water",
         R"({"typeid":"backpack","contents":{"items":[
             {"typeid":"bottle_plastic","contents":{"items":[{"typeid":"water_clean","charges":2}]}},
             {"typeid":"rock"}]}})",
         "backpack(bottle_plastic(water_cleanx2),rockx1)", 4},
    };
    return fixtures;
}

} // namespace

TEST_CASE("BN v29 item contents load into pockets unchanged", "[item][pocket][save]") {
    for (const fixture& f : bn_v29_fixtures()) {
        INFO(f.name);
        detached_ptr<item> it = load_item(f.json);
        REQUIRE(it);
        CHECK(signature(*it) == f.expected);
        CHECK(count_items(*it) == f.count);
    }
}

TEST_CASE("legacy contents are routed to the right pockets", "[item][pocket][save]") {
    SECTION("gun") {
        detached_ptr<item> gun = load_item(bn_v29_fixtures()[2].json);
        CHECK(pocket_of(*gun, "stanag30") == pocket_type::MAGAZINE_WELL);
        CHECK(pocket_of(*gun, "red_dot_sight") == pocket_type::MOD);
        CHECK(pocket_of(*gun, "shoulder_strap") == pocket_type::MOD);
        CHECK(gun->magazine_current() != nullptr);
        CHECK(gun->ammo_remaining() == 30);
        CHECK(gun->gunmods().size() == 2);
    }
    SECTION("holster") {
        detached_ptr<item> holster = load_item(bn_v29_fixtures()[3].json);
        CHECK(pocket_of(*holster, "glock_19") == pocket_type::CONTAINER);
        CHECK(holster->contents.pocket_of(&holster->contents.front())->is_holster());
    }
    SECTION("bandolier") {
        detached_ptr<item> band = load_item(bn_v29_fixtures()[4].json);
        CHECK(pocket_of(*band, "shot_00") == pocket_type::CONTAINER);
        CHECK(band->ammo_remaining() == 12);
    }
    SECTION("sealed can stays sealed and full") {
        detached_ptr<item> can = load_item(bn_v29_fixtures()[5].json);
        CHECK(can->typeId().str() == "can_drink");
        CHECK(pocket_of(*can, "water_clean") == pocket_type::CONTAINER);
    }
    SECTION("an empty sealed can still becomes an open can") {
        detached_ptr<item> can = load_item(R"({"typeid":"can_drink"})");
        CHECK(can->typeId().str() == "can_drink_unsealed");
    }
}

TEST_CASE("v30 pocket format round trip", "[item][pocket][save]") {
    for (const fixture& f : bn_v29_fixtures()) {
        INFO(f.name);
        detached_ptr<item> it = load_item(f.json);
        const std::string saved = save_item(*it);
        INFO(saved);
        CHECK(saved.find("\"pockets\"") != std::string::npos);
        detached_ptr<item> again = load_item(saved);
        REQUIRE(again);
        CHECK(signature(*again) == f.expected);
        CHECK(save_item(*again) == saved);
    }

    SECTION("pocket placement survives the round trip") {
        detached_ptr<item> holster = load_item(R"({"typeid":"test_holster_two","contents":{"items":[
            {"typeid":"glock_19"},{"typeid":"glock_19","damaged":1000}]}})");
        const std::string saved = save_item(*holster);
        detached_ptr<item> again = load_item(saved);
        REQUIRE(again->contents.num_item_stacks() == 2);
        for (const item* e : again->contents.all_items_top()) {
            CHECK(again->contents.pocket_of(e)->is_holster());
        }
        CHECK(again->contents.pocket_of(again->contents.all_items_top()[0])
              != again->contents.pocket_of(again->contents.all_items_top()[1]));
    }

    SECTION("empty pockets are not written") {
        detached_ptr<item> gun = load_item(R"({"typeid":"m4a1","contents":{"items":[{"typeid":"stanag30"}]}})");
        const std::string saved = save_item(*gun);
        CHECK(saved.find("MAGAZINE_WELL") != std::string::npos);
        CHECK(saved.find("\"MOD\"") == std::string::npos);
        detached_ptr<item> empty = load_item(R"({"typeid":"canteen"})");
        CHECK(save_item(*empty).find("contents") == std::string::npos);
    }
}

TEST_CASE("an overfull legacy container is recovered by overflow", "[item][pocket][save]") {
    clear_map();
    const tripoint_bub_ms pos(10, 10, 0);
    detached_ptr<item> box = load_item(R"({"typeid":"test_box_small","contents":{"items":[
        {"typeid":"test_screwdriver"},{"typeid":"test_screwdriver"},{"typeid":"test_screwdriver"}]}})");
    REQUIRE(box);
    // nothing lost on load: the one that doesn't fit waits in MIGRATION
    CHECK(box->contents.num_item_stacks() == 3);
    REQUIRE(box->contents.migration_pocket() != nullptr);
    CHECK(box->contents.migration_pocket()->size() == 1);
    CHECK(box->contents.overflow_pending());
    CHECK(box->contents.overflow(nullptr, pos) == 1);
    CHECK(box->contents.num_item_stacks() == 2);
    CHECK(get_map().i_at(pos).size() == 1);
}

TEST_CASE("v30 loads recover items from saved pockets that don't exist", "[item][pocket][save]") {
    SECTION("an unknown pocket index falls back to the router") {
        detached_ptr<item> holster = load_item(R"({"typeid":"holster","contents":{"pockets":[
            {"type":"CONTAINER","index":7,"items":[{"typeid":"glock_19"}]}]}})");
        REQUIRE(holster);
        REQUIRE(holster->contents.num_item_stacks() == 1);
        CHECK(pocket_of(*holster, "glock_19") == pocket_type::CONTAINER);
        CHECK(holster->contents.pocket_of(&holster->contents.front())->is_holster());
    }
    SECTION("an unknown pocket type falls back to the router") {
        detached_ptr<item> holster = load_item(R"({"typeid":"holster","contents":{"pockets":[
            {"type":"NO_SUCH_POCKET","index":0,"items":[{"typeid":"glock_19"}]}]}})");
        REQUIRE(holster);
        REQUIRE(holster->contents.num_item_stacks() == 1);
        CHECK(pocket_of(*holster, "glock_19") == pocket_type::CONTAINER);
    }
    SECTION("a pocket type the item no longer has") {
        detached_ptr<item> box = load_item(R"({"typeid":"test_box","contents":{"pockets":[
            {"type":"MOD","index":0,"items":[{"typeid":"test_screwdriver"}]}]}})");
        REQUIRE(box);
        REQUIRE(box->contents.num_item_stacks() == 1);
        CHECK(pocket_of(*box, "test_screwdriver") == pocket_type::CONTAINER);
    }
    SECTION("saved MIGRATION items move into a real pocket that takes them") {
        detached_ptr<item> box = load_item(R"({"typeid":"test_box","contents":{"pockets":[
            {"type":"MIGRATION","index":0,"items":[{"typeid":"test_screwdriver"}]}]}})");
        REQUIRE(box);
        REQUIRE(box->contents.num_item_stacks() == 1);
        CHECK(pocket_of(*box, "test_screwdriver") == pocket_type::CONTAINER);
    }
}

TEST_CASE("a gun with a magazine_adaptor mod round trips through a save", "[item][pocket][save]") {
    detached_ptr<item> gun = item::spawn(itype_id("m4a1"));
    gun->put_in(item::spawn(itype_id("retool_ar15_300blk")));
    gun->put_in(item::spawn(itype_id("stanag30")));
    const std::string expected = signature(*gun);
    REQUIRE(gun->magazine_current() != nullptr);

    const std::string saved = save_item(*gun);
    INFO(saved);
    detached_ptr<item> again = load_item(saved);
    REQUIRE(again);
    CHECK(signature(*again) == expected);
    CHECK(pocket_of(*again, "stanag30") == pocket_type::MAGAZINE_WELL);
    CHECK(pocket_of(*again, "retool_ar15_300blk") == pocket_type::MOD);
    CHECK(again->magazine_current() != nullptr);
    CHECK(save_item(*again) == saved);
}
