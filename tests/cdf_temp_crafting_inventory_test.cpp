#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "player_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"

#include <vector>

static const itype_id itype_2x4("2x4");
static const itype_id itype_hammer("hammer");

TEST_CASE("cdf_temp_crafting_inventory_basic", "[cdf][crafting]") {
    temp_crafting_inventory inv;
    CHECK(inv.size() == 0);

    item& plank = *item::spawn_temporary(itype_2x4, calendar::turn);
    inv.add_item_loc(plank);
    CHECK(inv.size() == 1);
    CHECK(inv.has_item(plank));
    CHECK(inv.count_item(itype_2x4) == 1);
    CHECK(inv.amount_of(itype_2x4) == 1);

    item& copy = inv.add_item_copy(plank);
    CHECK(&copy != &plank);
    CHECK(inv.size() == 2);
    CHECK(inv.amount_of(itype_2x4) == 2);

    item& pseudo = inv.add_pseudo_item(itype_hammer);
    CHECK(pseudo.has_flag(flag_id("PSEUDO")));
    CHECK(inv.amount_of(itype_hammer, true) == 1);
    CHECK(inv.amount_of(itype_hammer, false) == 0);
    CHECK(inv.has_quality(quality_id("HAMMER"), 1));
}

TEST_CASE("cdf_temp_crafting_inventory_remove_and_dump", "[cdf][crafting]") {
    temp_crafting_inventory inv;
    item& a = inv.add_item_copy(*item::spawn_temporary(itype_2x4, calendar::turn));
    inv.add_item_copy(*item::spawn_temporary(itype_hammer, calendar::turn));

    std::vector<item*> dest;
    inv.dump(dest);
    CHECK(dest.size() == 2);

    inv.remove_item(a);
    CHECK(inv.size() == 1);
    CHECK(!inv.has_item(a));

    temp_crafting_inventory other;
    other.add_all_ref(inv);
    CHECK(other.size() == 1);

    inv.clear();
    CHECK(inv.size() == 0);
    CHECK(other.size() == 1);
}

TEST_CASE("cdf_temp_crafting_inventory_character", "[cdf][crafting]") {
    avatar& u = get_avatar();
    clear_avatar();
    item& plank = *item::spawn_temporary(itype_2x4, calendar::turn);
    u.i_add(item::spawn(plank));

    temp_crafting_inventory inv;
    inv.add_all_ref(u);
    CHECK(inv.amount_of(itype_2x4) == 1);
}

TEST_CASE("cdf_crafting_temp_inventory_builder", "[cdf][crafting]") {
    avatar& u = get_avatar();
    clear_avatar();
    u.wield(item::spawn(itype_hammer, calendar::turn));
    u.i_add(item::spawn(itype_2x4, calendar::turn));
    u.invalidate_crafting_inventory();

    const temp_crafting_inventory& temp = u.crafting_inventory();
    CHECK(temp.amount_of(itype_hammer) == 1);
    CHECK(temp.amount_of(itype_2x4) == 1);
}
