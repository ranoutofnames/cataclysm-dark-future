// CDF: MW1 (PL-64 gap G15). DDA limits how many copies of one item type can be worn with
// itype::max_worn (DDA itype.h:1661, loaded at item_factory.cpp:4595); Character::wear refuses
// the extra copies (DDA character_attire.cpp:262 and :1360). CDF data carries no max_worn yet,
// so the loader is checked with an inline item and the wear check with a real armor whose shared
// itype gets max_worn 1 for the case (restored by the guard).
#include "avatar.h"
#include "catch/catch.hpp"
#include "detached_ptr.h"
#include "game_constants.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>
#include <vector>

namespace {

const itype *find_test_item(Item_factory &factory, const std::string &id) {
    const std::vector<const itype *> hits = factory.find([&id](const itype &t) {
        return t.get_id() == itype_id(id);
    });
    REQUIRE(hits.size() == 1);
    return hits.front();
}

}

TEST_CASE("max_worn is read from item JSON", "[character][fm]") {
    Item_factory factory;
    std::istringstream buffer(R"([
        {"type": "ARMOR", "id": "test_max_worn_one", "name": "test max worn one",
         "max_worn": 1, "material_thickness": 1},
        {"type": "ARMOR", "id": "test_max_worn_default", "name": "test max worn default",
         "material_thickness": 1}
    ])");
    JsonIn jsin(buffer);
    JsonArray items = jsin.get_array();
    for (const JsonObject& jo : items) {
        factory.load_armor(jo, "bn");
    }
    factory.finalize();

    CHECK(find_test_item(factory, "test_max_worn_one")->max_worn == 1);
    // the default is DDA's MAX_WORN_PER_TYPE when the key is absent
    CHECK(find_test_item(factory, "test_max_worn_default")->max_worn == MAX_WORN_PER_TYPE);
}

TEST_CASE("wearing a second copy of an item with max_worn 1 is refused", "[character][fm]") {
    clear_all_state();
    avatar& ch = get_avatar();

    SECTION("max_worn 1 refuses the second copy") {
        detached_ptr<item> first = item::spawn("boots_combat");
        REQUIRE(first != nullptr);

        // CDF: runtime edit of the shared itype for this case only, restored by the guard
        itype& type = const_cast<itype&>(*first->type);
        const int saved = type.max_worn;
        struct max_worn_guard {
            itype& t;
            int v;
            ~max_worn_guard() {
                t.max_worn = v;
            }
        } guard{type, saved};
        type.max_worn = 1;

        // the first copy goes on
        CHECK(ch.wear_item(std::move(first), false) == nullptr);

        detached_ptr<item> second = item::spawn("boots_combat");
        const ret_val<bool> res = ch.can_wear(*second);
        INFO(res.str());
        CHECK_FALSE(res.success());
        // the refused copy is not worn and is handed back
        CHECK(ch.wear_item(std::move(second), false) != nullptr);
        CHECK(ch.amount_worn(itype_id("boots_combat")) == 1);
    }

    SECTION("the default limit allows two copies") {
        detached_ptr<item> first = item::spawn("boots_combat");
        detached_ptr<item> second = item::spawn("boots_combat");
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(first->type->max_worn == MAX_WORN_PER_TYPE);

        // two soft boots go on; only the third pair hits the limit
        CHECK(ch.wear_item(std::move(first), false) == nullptr);
        CHECK(ch.wear_item(std::move(second), false) == nullptr);
        CHECK(ch.amount_worn(itype_id("boots_combat")) == 2);
    }
}
