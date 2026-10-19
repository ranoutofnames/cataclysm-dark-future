#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

// P1a acceptance test, rewritten for the DDA save format (P1-transplant J3, savegame_version
// 32): item contents round trip through the pocket format with nothing lost, and contents saved
// for pockets the item no longer has are routed to the pockets it has now. The BN v29 and CDF v30
// formats are not read any more (no old-save compatibility), so their fixtures are built with
// put_in and saved with the current format instead.

namespace {

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

auto spawn_with(const std::string& id, int charges = 0) -> detached_ptr<item> {
    detached_ptr<item> ret = item::spawn(itype_id(id));
    if (charges > 0) { ret->charges = charges; }
    return ret;
}

struct fixture {
    std::string name;
    std::function<detached_ptr<item>()> build;
    std::string expected;
    int count;
};

const std::vector<fixture>& fixtures() {
    static const std::vector<fixture> list = {
        {"canteen with water",
         [] {
             detached_ptr<item> ret = spawn_with("canteen");
             ret->put_in(spawn_with("water_clean", 2));
             return ret;
         },
         "canteen(water_cleanx2)", 2},
        {"gun with magazine and gunmods",
         [] {
             detached_ptr<item> ret = spawn_with("m4a1");
             detached_ptr<item> mag = spawn_with("stanag30");
             mag->put_in(spawn_with("556", 30));
             ret->put_in(std::move(mag));
             ret->put_in(spawn_with("red_dot_sight"));
             ret->put_in(spawn_with("shoulder_strap"));
             return ret;
         },
         "m4a1(red_dot_sight,shoulder_strap,stanag30(556x30))", 5},
        {"holster with a pistol",
         [] {
             detached_ptr<item> ret = spawn_with("holster");
             ret->put_in(spawn_with("glock_19"));
             return ret;
         },
         "holster(glock_19)", 2},
        {"bandolier",
         [] {
             detached_ptr<item> ret = spawn_with("bandolier_shotgun");
             ret->put_in(spawn_with("shot_00", 12));
             return ret;
         },
         "bandolier_shotgun(shot_00x12)", 2},
        {"sealed can",
         [] {
             detached_ptr<item> ret = spawn_with("can_drink");
             ret->put_in(spawn_with("water_clean", 1));
             return ret;
         },
         "can_drink(water_cleanx1)", 2},
        {"nested backpack, bottle, water",
         [] {
             detached_ptr<item> ret = spawn_with("backpack");
             detached_ptr<item> bottle = spawn_with("bottle_plastic");
             bottle->put_in(spawn_with("water_clean", 2));
             ret->put_in(std::move(bottle));
             ret->put_in(spawn_with("rock"));
             return ret;
         },
         "backpack(bottle_plastic(water_cleanx2),rockx1)", 4},
    };
    return list;
}

} // namespace

TEST_CASE("item contents round trip through the DDA pocket format", "[item][pocket][save]") {
    for (const fixture& f : fixtures()) {
        INFO(f.name);
        detached_ptr<item> it = f.build();
        REQUIRE(it);
        CHECK(signature(*it) == f.expected);
        CHECK(count_items(*it) == f.count);
        const std::string saved = save_item(*it);
        INFO(saved);
        CHECK(saved.find("\"pocket_type\"") != std::string::npos);
        detached_ptr<item> again = load_item(saved);
        REQUIRE(again);
        CHECK(signature(*again) == f.expected);
        CHECK(count_items(*again) == f.count);
        // loading makes perishable items active, so compare the second save with the third
        const std::string saved_again = save_item(*again);
        detached_ptr<item> third = load_item(saved_again);
        CHECK(save_item(*third) == saved_again);
    }
}

TEST_CASE("contents are in the right pockets after a load", "[item][pocket][save]") {
    SECTION("gun") {
        detached_ptr<item> gun = load_item(save_item(*fixtures()[1].build()));
        CHECK(pocket_of(*gun, "stanag30") == pocket_type::MAGAZINE_WELL);
        CHECK(pocket_of(*gun, "red_dot_sight") == pocket_type::MOD);
        CHECK(pocket_of(*gun, "shoulder_strap") == pocket_type::MOD);
        CHECK(gun->magazine_current() != nullptr);
        CHECK(gun->ammo_remaining() == 30);
        CHECK(gun->gunmods().size() == 2);
    }
    SECTION("holster") {
        detached_ptr<item> holster = load_item(save_item(*fixtures()[2].build()));
        CHECK(pocket_of(*holster, "glock_19") == pocket_type::CONTAINER);
        CHECK(holster->contents.pocket_of(&holster->contents.front())->is_holster());
    }
    SECTION("bandolier") {
        detached_ptr<item> band = load_item(save_item(*fixtures()[3].build()));
        CHECK(pocket_of(*band, "shot_00") == pocket_type::CONTAINER);
        CHECK(band->ammo_remaining() == 12);
    }
    SECTION("sealed can stays sealed and full") {
        detached_ptr<item> can = load_item(save_item(*fixtures()[4].build()));
        CHECK(can->typeId().str() == "can_drink");
        CHECK(pocket_of(*can, "water_clean") == pocket_type::CONTAINER);
    }
    SECTION("an empty sealed can still becomes an open can") {
        detached_ptr<item> can = load_item(R"({"typeid":"can_drink"})");
        CHECK(can->typeId().str() == "can_drink_unsealed");
    }
}

TEST_CASE("pocket placement and empty pockets in the DDA format", "[item][pocket][save]") {
    SECTION("pocket placement survives the round trip") {
        detached_ptr<item> holster = spawn_with("test_holster_two");
        holster->put_in(spawn_with("glock_19"));
        detached_ptr<item> damaged = spawn_with("glock_19");
        damaged->set_damage(1000);
        holster->put_in(std::move(damaged));
        const std::string saved = save_item(*holster);
        detached_ptr<item> again = load_item(saved);
        REQUIRE(again->contents.num_item_stacks() == 2);
        for (const item* e : again->contents.all_items_top()) {
            CHECK(again->contents.pocket_of(e)->is_holster());
        }
        CHECK(again->contents.pocket_of(again->contents.all_items_top()[0])
              != again->contents.pocket_of(again->contents.all_items_top()[1]));
    }

    SECTION("an item in its default state writes no contents") {
        detached_ptr<item> empty = load_item(R"({"typeid":"canteen"})");
        CHECK(save_item(*empty).find("contents") == std::string::npos);
        // CDF: a gun's magazine pocket is collapsed from the start (DDA item.cpp:222), so its
        // contents are written; a pocketless item still writes none
        detached_ptr<item> gun = spawn_with("m4a1");
        CHECK(save_item(*gun).find("contents") != std::string::npos);
        detached_ptr<item> rock = spawn_with("rock");
        CHECK(save_item(*rock).find("contents") == std::string::npos);
    }

    SECTION("a gun with a magazine writes its pockets") {
        detached_ptr<item> gun = spawn_with("m4a1");
        gun->put_in(spawn_with("stanag30"));
        const std::string saved = save_item(*gun);
        // the magazine well (DDA pocket type 2)
        CHECK(saved.find("\"pocket_type\":2") != std::string::npos);
        detached_ptr<item> again = load_item(saved);
        CHECK(pocket_of(*again, "stanag30") == pocket_type::MAGAZINE_WELL);
    }
}

TEST_CASE("an overfull saved container is recovered by overflow", "[item][pocket][save]") {
    clear_map();
    const tripoint_bub_ms pos(10, 10, 0);
    // the DDA format loads without rule checks (DDA #68051): three screwdrivers in a box for two
    detached_ptr<item> box = load_item(R"({"typeid":"test_box_small","contents":{"contents":[
        {"pocket_type":0,"contents":[
            {"typeid":"test_screwdriver"},{"typeid":"test_screwdriver"},{"typeid":"test_screwdriver"}]}]}})");
    REQUIRE(box);
    // nothing lost on load
    CHECK(box->contents.num_item_stacks() == 3);
    CHECK(box->contents.overflow_pending());
    CHECK(box->contents.overflow(nullptr, pos) == 1);
    CHECK(box->contents.num_item_stacks() == 2);
    CHECK(get_map().i_at(pos).size() == 1);
}

TEST_CASE("loads recover items from saved pockets that don't exist", "[item][pocket][save]") {
    SECTION("a saved pocket past the last one falls back to the router") {
        detached_ptr<item> holster = load_item(R"({"typeid":"holster","contents":{"contents":[
            {"pocket_type":0,"contents":[]},
            {"pocket_type":0,"contents":[{"typeid":"glock_19"}]}]}})");
        REQUIRE(holster);
        REQUIRE(holster->contents.num_item_stacks() == 1);
        CHECK(pocket_of(*holster, "glock_19") == pocket_type::CONTAINER);
        CHECK(holster->contents.pocket_of(&holster->contents.front())->is_holster());
    }
    SECTION("an unknown pocket type falls back to the router") {
        detached_ptr<item> holster = load_item(R"({"typeid":"holster","contents":{"contents":[
            {"pocket_type":99,"contents":[{"typeid":"glock_19"}]}]}})");
        REQUIRE(holster);
        REQUIRE(holster->contents.num_item_stacks() == 1);
        CHECK(pocket_of(*holster, "glock_19") == pocket_type::CONTAINER);
    }
    SECTION("a pocket type the item no longer has") {
        detached_ptr<item> box = load_item(R"({"typeid":"test_box","contents":{"contents":[
            {"pocket_type":3,"contents":[{"typeid":"test_screwdriver"}]}]}})");
        REQUIRE(box);
        REQUIRE(box->contents.num_item_stacks() == 1);
        CHECK(pocket_of(*box, "test_screwdriver") == pocket_type::CONTAINER);
    }
    SECTION("saved MIGRATION items move into a real pocket that takes them") {
        detached_ptr<item> box = load_item(R"({"typeid":"test_box","contents":{"contents":[
            {"pocket_type":8,"contents":[{"typeid":"test_screwdriver"}]}]}})");
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
