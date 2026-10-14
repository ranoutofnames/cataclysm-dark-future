#include "avatar.h"
#include "bionics.h"
#include "catch/catch.hpp"
#include "character.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "json.h"
#include "npc.h"
#include "npctrade.h"
#include "player_helpers.h"
#include "point.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

#include <sstream>
#include <vector>

// CDF P1c step 19: the character pocket API and the body storage are dead code (nothing in
// the game calls them yet), so these tests also check that they leave the legacy carrying
// numbers alone.

TEST_CASE("body storage follows the CARRY_STORAGE enchantment", "[character][pocket][body_storage]") {
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, false);
    u.update_body_storage();
    clear_map();

    CHECK(u.body_storage_capacity() == 0_ml);
    u.update_body_storage();
    CHECK(u.get_body_storage() == nullptr);

    u.add_bionic(bionic_id("bio_storage"));
    u.recalculate_enchantment_cache();
    CHECK(u.body_storage_capacity() == 2000_ml);
    u.update_body_storage();
    REQUIRE(u.get_body_storage() != nullptr);
    CHECK(u.carried_capacity() == 2000_ml);
    CHECK(u.carried_volume() == 0_ml);
    CHECK(u.volume_carried() == 0_ml);
    CHECK(u.volume_capacity() == 2000_ml);

    SECTION("items go in and show up in the carried view") {
        auto r = u.try_add(item::spawn(itype_id("rock")));
        REQUIRE(r.success());
        CHECK(u.get_body_storage()->contents.num_item_stacks() == 1);
        CHECK(u.carried_view().size() == 1);
        CHECK(u.volume_carried() == u.carried_volume());
        CHECK(u.carried_volume() > 0_ml);
        CHECK(u.free_carry_volume() < 2000_ml);
    }

    SECTION("a shrinking compartment spills onto the ground") {
        REQUIRE(u.try_add(item::spawn(itype_id("rock"))).success());
        u.remove_bionic(bionic_id("bio_storage"));
        u.recalculate_enchantment_cache();
        CHECK(u.body_storage_capacity() == 0_ml);
        u.update_body_storage();
        CHECK(u.get_body_storage() == nullptr);
        CHECK(!get_map().i_at(u.bub_pos()).empty());
    }
}

TEST_CASE("DEBUG_STORAGE keeps unlimited capacity", "[character][pocket][body_storage]") {
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, true);
    u.update_body_storage();
    CHECK(u.body_storage_capacity() == units::volume_max);
    CHECK(u.carried_capacity() == units::volume_max);
    u.update_body_storage();
    REQUIRE(u.get_body_storage() != nullptr);
    CHECK(u.can_stash(*item::spawn_temporary(itype_id("rock"))).success());
    CHECK(u.try_add(item::spawn(itype_id("rock"))).success());
}

TEST_CASE("best_pocket and try_add use worn carry pockets", "[character][pocket][body_storage]") {
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, false);
    u.update_body_storage();

    item& rock = *item::spawn_temporary(itype_id("rock"));
    CHECK(!u.can_stash(rock).success());
    CHECK(u.best_pocket(rock).second == nullptr);
    {
        auto r = u.try_add(item::spawn(itype_id("rock")));
        CHECK(!r.success());
    }

    u.wear_item(item::spawn(itype_id("jeans")));
    const units::volume legacy_capacity = u.volume_capacity();
    const units::volume legacy_carried = u.volume_carried();

    auto best = u.best_pocket(rock);
    REQUIRE(best.second != nullptr);
    CHECK(best.second->is_carry());
    CHECK(u.can_stash(rock).success());

    auto r = u.try_add(item::spawn(itype_id("rock")));
    REQUIRE(r.success());
    CHECK(u.carried_volume() == r.value()->volume());
    CHECK(u.carried_capacity() > 0_ml);
    CHECK(u.free_carry_volume() > 0_ml);
    // the capacity and volume numbers are the pocket sums now
    CHECK(u.volume_capacity() == u.carried_capacity());
    CHECK(u.volume_capacity() == legacy_capacity);
    CHECK(u.volume_carried() == legacy_carried + r.value()->volume());
    CHECK(u.carried_view().size() == 1);
}

TEST_CASE("a refused try_add leaves the item with the caller", "[character][pocket][body_storage]") {
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, false);
    u.update_body_storage();
    detached_ptr<item> rock = item::spawn(itype_id("rock"));
    item* raw = rock.get();
    auto r = u.try_add(std::move(rock));
    CHECK(!r.success());
    CHECK(rock.get() == raw);
}

TEST_CASE("shop stock is not carried", "[npc][shop]") {
    clear_all_state();
    npc& shopkeeper = spawn_npc(get_avatar().bub_pos() + point(5, 0), "test_talker");
    const units::volume carried_before = shopkeeper.volume_carried();
    const size_t slices_before = shopkeeper.inv_const_slice().size();
    shopkeeper.shop_stock.add_item(item::spawn(itype_id("rock")), false);
    shopkeeper.shop_stock.add_item(item::spawn(itype_id("rock")), false);
    CHECK(shopkeeper.shop_stock.size() > 0);
    // the stock does not count as carried (the NPC's own starting kit does)
    CHECK(shopkeeper.volume_carried() == carried_before);
    CHECK(shopkeeper.inv_const_slice().size() == slices_before);

    SECTION("an item leaves the stock through its location") {
        item* it = shopkeeper.shop_stock.const_stack(0).front();
        detached_ptr<item> taken = it->detach();
        CHECK(taken);
        CHECK(shopkeeper.shop_stock.size() == 0);
    }
    SECTION("the wares move to the inventory when the shopkeeper dies") {
        shopkeeper.shop_stock_to_inventory();
        CHECK(shopkeeper.shop_stock.size() == 0);
        CHECK(shopkeeper.inv_const_slice().size() == slices_before + 1);
    }
}

TEST_CASE("shop stock transfers on death, trades and survives a save", "[npc][shop]") {
    clear_all_state();
    npc& shopkeeper = spawn_npc(get_avatar().bub_pos() + point(5, 0), "test_talker");
    clear_character(shopkeeper, true);
    shopkeeper.shop_stock.add_item(item::spawn(itype_id("rock")), false);
    shopkeeper.shop_stock.add_item(item::spawn(itype_id("2x4")), false);
    const size_t stacks = shopkeeper.shop_stock.size();
    REQUIRE(stacks == 2);

    SECTION("the stock is listed for trade and bought items leave it") {
        avatar& u = get_avatar();
        clear_character(u, true);
        std::vector<item_pricing> wares = npc_trading::init_selling(shopkeeper);
        REQUIRE(!wares.empty());
        for (item_pricing& ip : wares) {
            ip.selected = true;
            ip.npc_has = static_cast<int>(ip.locs.size());
            ip.u_has = ip.npc_has;
        }
        npc_trading::transfer_items(wares, shopkeeper, u, true);
        CHECK(shopkeeper.shop_stock.size() < stacks);
        CHECK(u.inv_size() > 0);
    }

    SECTION("the stock survives a save and load") {
        std::ostringstream out;
        JsonOut jsout(out);
        shopkeeper.serialize(jsout);
        std::istringstream in(out.str());
        JsonIn jsin(in);
        npc loaded;
        loaded.deserialize(jsin);
        CHECK(loaded.shop_stock.size() == stacks);
    }

    SECTION("the wares go to the inventory when the shopkeeper dies") {
        shopkeeper.die(nullptr);
        CHECK(shopkeeper.shop_stock.size() == 0);
    }

    SECTION("the wares go to the inventory when the shopkeeper is recruited") {
        shopkeeper.set_attitude(NPCATT_FOLLOW);
        CHECK(shopkeeper.shop_stock.size() == 0);
        CHECK(shopkeeper.inv_size() > 0);
    }
}

// CDF P1c step 20 review: liquids in a bottle carried in a backpack.
TEST_CASE("a liquid in a bottle in a backpack behaves", "[character][pocket][liquid]") {
    clear_all_state();
    clear_map();
    avatar& u = get_avatar();
    clear_character(u, false);
    REQUIRE(!u.wear_item(item::spawn(itype_id("backpack")), false));
    item* pack = u.worn.back();
    REQUIRE(pack != nullptr);

    detached_ptr<item> bottle = item::in_its_container(item::spawn(itype_id("water_clean")));
    REQUIRE(bottle->typeId() != itype_id("water_clean"));
    ret_val<item*> added = u.try_add(std::move(bottle));
    REQUIRE(added.success());
    item& carried = *added.value();
    REQUIRE(!carried.is_container_empty());
    item& water = carried.contents.front();

    // a carried bottle is inventory, not "in a container"; the water in it is
    CHECK(carried.parent_item() == pack);
    CHECK(!carried.is_in_container());
    CHECK(water.is_in_container());
    CHECK(u.carried_view().size() == 1);
    CHECK(u.volume_carried() >= carried.volume());

    SECTION("taking the backpack off keeps the bottle and its water inside") {
        u.set_mutation(trait_id("DEBUG_STORAGE")); // somewhere for the pack to go
        REQUIRE(u.takeoff(*pack));
        CHECK(get_map().i_at(u.bub_pos()).empty());
        bool found_water = false;
        u.visit_items([&](item* it) {
            if (it->typeId() == itype_id("water_clean")) {
                found_water = true;
            }
            return VisitResponse::NEXT;
        });
        CHECK(found_water);
    }

    SECTION("the water in the carried bottle can be drunk") {
        // consume() starts an activity, so only the ability is checked here
        CHECK(u.can_consume(water));
        CHECK(u.carried_view().size() == 1);
    }
}

// CDF P1c step 21: NPC starting kit and pickups follow the pockets.
TEST_CASE("npc kit that fits no pocket waits at its feet", "[npc][pocket]") {
    clear_all_state();
    clear_map();
    npc& guy = spawn_npc(get_avatar().bub_pos() + point(5, 0), "test_talker");
    clear_character(guy, true);
    guy.starting_overflow.add_item(item::spawn(itype_id("rock")), false);
    REQUIRE(guy.starting_overflow.size() == 1);

    SECTION("it survives a save") {
        std::ostringstream out;
        JsonOut jsout(out);
        guy.serialize(jsout);
        std::istringstream in(out.str());
        JsonIn jsin(in);
        npc loaded;
        loaded.deserialize(jsin);
        CHECK(loaded.starting_overflow.size() == 1);
    }

    SECTION("on_load drops it on the ground") {
        guy.on_load();
        CHECK(guy.starting_overflow.size() == 0);
        CHECK(!get_map().i_at(guy.bub_pos()).empty());
    }
}

TEST_CASE("npc pickups respect pocket fit", "[npc][pocket]") {
    clear_all_state();
    clear_map();
    npc& guy = spawn_npc(get_avatar().bub_pos() + point(5, 0), "test_talker");
    clear_character(guy, true);
    guy.wear_item(item::spawn(itype_id("jeans")), false);
    const tripoint_bub_ms where = guy.bub_pos();
    get_map().add_item_or_charges(where, item::spawn(itype_id("2x4")));
    get_map().add_item_or_charges(where, item::spawn(itype_id("rock")));

    for (item* it : guy.pick_up_item_map(where)) {
        CHECK(guy.can_stash(*it).success());
    }
}
