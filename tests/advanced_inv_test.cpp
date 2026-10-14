#include "advanced_inv_area.h"
#include "advanced_inv_listitem.h"
#include "advanced_inv_pane.h"
#include "avatar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "player_helpers.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>

// CDF P1c follow-up: the inventory pane of the advanced inventory manager names the garment
// (or the body storage) that carries each stack.  Only the pane's list logic is tested; the
// curses drawing has no test hooks.

TEST_CASE("advanced inventory pane names the garment carrying a stack",
          "[advanced_inv][pocket]") {
    clear_all_state();
    avatar& u = get_avatar();

    advanced_inv_area square(AIM_INVENTORY);
    advanced_inventory_pane pane;

    SECTION("a worn garment") {
        clear_character(u, false);
        u.update_body_storage();
        u.wear_item(item::spawn(itype_id("jeans")));
        REQUIRE(u.try_add(item::spawn(itype_id("rock"))).success());
        square.init();
        pane.add_items_from_area(square);
        REQUIRE(pane.items.size() == 1);
        const advanced_inv_listitem& entry = pane.items.front();
        const item* garment = entry.items.front()->parent_item();
        REQUIRE(garment != nullptr);
        CHECK(u.is_worn(*garment));
        CHECK(entry.holder == garment->tname(1, false));
        CHECK(!entry.holder.empty());
    }

    SECTION("the body storage") {
        clear_character(u, true);
        u.update_body_storage();
        REQUIRE(u.try_add(item::spawn(itype_id("rock"))).success());
        square.init();
        pane.add_items_from_area(square);
        REQUIRE(pane.items.size() == 1);
        const advanced_inv_listitem& entry = pane.items.front();
        const item* storage = entry.items.front()->parent_item();
        REQUIRE(storage != nullptr);
        CHECK(storage == u.get_body_storage());
        CHECK(entry.holder == storage->tname(1, false));
    }

    SECTION("nothing is carried") {
        clear_character(u, false);
        u.update_body_storage();
        square.init();
        pane.add_items_from_area(square);
        CHECK(pane.items.empty());
    }
}
