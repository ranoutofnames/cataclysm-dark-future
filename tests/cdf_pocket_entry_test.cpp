// CDF-authored tests for the pocket entry points (P2-4): has_container_pockets, collapse/expand.
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "inventory_ui.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_backpack_hiking("backpack_hiking");
static const itype_id itype_test_rock("test_rock");

TEST_CASE("has_container_pockets", "[pocket][inventory][ui]") {
    clear_all_state();
    detached_ptr<item> pack = item::spawn(itype_backpack_hiking, calendar::turn);
    detached_ptr<item> rock = item::spawn(itype_test_rock, calendar::turn);
    CHECK(pack->has_container_pockets());
    CHECK_FALSE(rock->has_container_pockets());
}

TEST_CASE("collapse and expand set every standard pocket", "[pocket][inventory][ui]") {
    clear_all_state();
    detached_ptr<item> pack = item::spawn(itype_backpack_hiking, calendar::turn);
    REQUIRE(!pack->get_standard_pockets().empty());
    for (item_pocket* pocket : pack->get_standard_pockets()) {
        pocket->settings.set_collapse(true);
    }
    for (const item_pocket* pocket : pack->get_standard_pockets()) {
        CHECK(pocket->settings.is_collapsed());
    }
    for (item_pocket* pocket : pack->get_standard_pockets()) {
        pocket->settings.set_collapse(false);
    }
    for (const item_pocket* pocket : pack->get_standard_pockets()) {
        CHECK_FALSE(pocket->settings.is_collapsed());
    }
}

TEST_CASE("inventory rows of a collapsed garment are hidden", "[pocket][inventory][ui]") {
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, true);
    u.wear_item(item::spawn(itype_backpack_hiking, calendar::turn), false);
    item& pack = *u.worn.front();
    item& rock = u.i_add(item::spawn(itype_test_rock, calendar::turn));
    REQUIRE(rock.parent_item() == &pack);

    const auto rock_rows = [&u]() {
        inventory_selector sel(u);
        sel.add_character_items(u);
        sel.own_inv_column.prepare_paging();
        size_t n = 0;
        for (const inventory_entry& e : sel.own_inv_column.entries) { n += e.is_item() ? 1 : 0; }
        return n;
    };
    CHECK(rock_rows() == 1);
    for (item_pocket* pocket : pack.get_standard_pockets()) { pocket->settings.set_collapse(true); }
    REQUIRE(pack.is_collapsed());
    CHECK(rock_rows() == 0);
    for (item_pocket* pocket : pack.get_standard_pockets()) {
        pocket->settings.set_collapse(false);
    }
    CHECK(rock_rows() == 1);
}
