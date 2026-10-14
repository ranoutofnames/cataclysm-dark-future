#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "iuse_actor.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"

#include <vector>

namespace {

auto count_items_of(const avatar& you, const itype_id& id) -> int {
    return you.amount_of(id);
}

} // namespace

TEST_CASE("a full pocket is not offered the item it would refuse", "[item][pocket][holster]") {
    detached_ptr<item> rig = item::spawn(itype_id("test_holster_mixed"));
    detached_ptr<item> first = item::spawn(itype_id("glock_19"));
    detached_ptr<item> second = item::spawn(itype_id("glock_19"));
    CHECK(rig->can_holster(*first));
    item* stored = &*first;
    rig->put_in(std::move(first));
    REQUIRE(rig->contents.pocket_of(stored) != nullptr);
    CHECK(rig->contents.pocket_of(stored)->is_holster());
    // the sheath is still free, so the holster as a whole has a free pocket...
    CHECK(rig->has_free_holster_pocket());
    // ...but the only pocket that fits a pistol is full: the picker must not offer it
    CHECK_FALSE(rig->can_holster(*second, false));
    CHECK(rig->can_holster(*second, true));
}

TEST_CASE("a refused holster item goes back to the character", "[item][pocket][holster]") {
    clear_map();
    clear_avatar();
    avatar& you = get_avatar();

    detached_ptr<item> holster_ptr = item::spawn(itype_id("test_holster_two"));
    item& holster = *holster_ptr;
    you.i_add(std::move(holster_ptr));
    const auto* actor =
        dynamic_cast<const holster_actor*>(holster.type->get_use("holster")->get_actor_ptr());
    REQUIRE(actor != nullptr);

    // fill both slots
    for (int i = 0; i < 2; ++i) {
        detached_ptr<item> pistol = item::spawn(itype_id("glock_19"));
        CHECK_FALSE(actor->store(you, holster, std::move(pistol)));
    }
    REQUIRE_FALSE(holster.has_free_holster_pocket());
    REQUIRE(holster.contents.num_item_stacks() == 2);

    // the item was taken out of the inventory before the store: it must not be destroyed
    detached_ptr<item> third = item::spawn(itype_id("glock_19"));
    actor->store_or_return(you, holster, std::move(third));
    CHECK(holster.contents.num_item_stacks() == 2);
    // the third pistol is in the inventory or on the ground at the feet, not gone
    const int in_inventory = count_items_of(you, itype_id("glock_19"));
    const int on_ground = static_cast<int>(get_map().i_at(you.bub_pos()).size());
    // two in the holster plus the returned one
    CHECK(in_inventory + on_ground == 3);
}

TEST_CASE("copying an item keeps each item in its pocket", "[item][pocket][holster]") {
    detached_ptr<item> holster = item::spawn(itype_id("test_holster_two"));
    holster->put_in(item::spawn(itype_id("glock_19")));
    holster->put_in(item::spawn(itype_id("glock_19")));
    REQUIRE(holster->contents.num_item_stacks() == 2);

    detached_ptr<item> copy = item::spawn(*holster);
    REQUIRE(copy->contents.num_item_stacks() == 2);
    const std::vector<item*>& orig = holster->contents.all_items_top();
    const std::vector<item*>& dup = copy->contents.all_items_top();
    const auto index_of = [](const item& container, const item* it) -> int {
        int i = 0;
        for (const item_pocket& pocket : container.contents.get_pockets()) {
            if (&pocket == container.contents.pocket_of(it)) {
                return i;
            }
            i++;
        }
        return -1;
    };
    for (size_t i = 0; i < orig.size(); ++i) {
        CHECK(index_of(*copy, dup[i]) == index_of(*holster, orig[i]));
    }

    // a copy over an item of another type
    detached_ptr<item> other = item::spawn(itype_id("test_box"));
    *other = *holster;
    REQUIRE(other->contents.num_item_stacks() == 2);
    CHECK(other->contents.pocket_of(other->contents.all_items_top()[0]) != nullptr);
}

TEST_CASE("drawing from a holster through the real action wields the item",
          "[item][pocket][holster]") {
    clear_map();
    clear_avatar();
    avatar& you = get_avatar();

    detached_ptr<item> holster_ptr = item::spawn(itype_id("test_holster_one"));
    item& holster = *holster_ptr;
    you.i_add(std::move(holster_ptr));
    const auto* actor =
        dynamic_cast<const holster_actor*>(holster.type->get_use("holster")->get_actor_ptr());
    REQUIRE(actor != nullptr);

    // a single slot, filled: holster_actor::use goes straight to drawing, with no menu
    detached_ptr<item> pistol = item::spawn(itype_id("glock_19"));
    CHECK_FALSE(actor->store(you, holster, std::move(pistol)));
    REQUIRE(holster.contents.num_item_stacks() == 1);
    REQUIRE_FALSE(holster.has_free_holster_pocket());
    REQUIRE(you.primary_weapon().is_null());
    const item* stored = holster.contents.all_items_top()[0];

    CHECK(actor->use(you, holster, false, you.bub_pos()) == 0);

    // the item left its pocket and is now wielded; the holster is empty and free again
    CHECK(holster.contents.empty());
    CHECK(holster.has_free_holster_pocket());
    CHECK(&you.primary_weapon() == stored);
    CHECK(you.primary_weapon().typeId() == itype_id("glock_19"));
}
