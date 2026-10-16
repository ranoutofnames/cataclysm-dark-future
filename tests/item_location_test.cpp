#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <vector>

// Adapted from DDA item_location_test.cpp:44,81,95 to BN's item* handle (CDF option A)
static const itype_id itype_backpack("backpack");
static const itype_id itype_jeans("jeans");

TEST_CASE("character_all_items_loc_includes_contents", "[item][item_location]") {
    Character& dummy = get_player_character();
    clear_avatar();
    detached_ptr<item> bp = item::spawn(itype_backpack, calendar::turn);
    item& pack = *bp;
    pack.put_in(item::spawn(itype_jeans, calendar::turn), pocket_type::CONTAINER);
    dummy.wear_item(std::move(bp), false);

    const std::vector<item*> top = dummy.top_items_loc();
    CHECK(std::find(top.begin(), top.end(), &pack) != top.end());
    const std::vector<item*> all = dummy.all_items_loc();
    CHECK(std::find(all.begin(), all.end(), &pack) != all.end());
    CHECK(std::any_of(all.begin(), all.end(), [](const item* i) {
        return i->typeId() == itype_jeans;
    }));
}

TEST_CASE("character_invlet_to_item_and_remove_worn", "[item][item_location]") {
    Character& dummy = get_player_character();
    clear_avatar();
    detached_ptr<item> bp = item::spawn(itype_backpack, calendar::turn);
    item& pack = *bp;
    dummy.wear_item(std::move(bp), false);
    pack.invlet = 'q';
    CHECK(dummy.invlet_to_item('q') == &pack);
    CHECK(dummy.invlet_to_item(0x152) == nullptr);
    dummy.remove_worn_items_with([](detached_ptr<item>&& it) {
        return it->typeId() == itype_backpack ? detached_ptr<item>() : std::move(it);
    });
    CHECK(dummy.top_items_loc().empty());
}
