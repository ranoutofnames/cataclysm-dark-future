#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_category.h"
#include "type_id.h"

#include <string>

namespace
{

// item_factory.cpp:5183 calc_category makes an ARMOR item "clothing" unless its
// JSON sets "category"; DDA's armor data sets it for protective gear, so the
// category an item ends up in is the one its data asks for (item.cpp:1318
// get_category_shallow).  CDF's category ids are the lower-case ones from
// data/json/item_category.json; the displayed name is all caps.
auto category_id_of( const std::string &id ) -> std::string
{
    item &it = *item::spawn_temporary( itype_id( id ), calendar::start_of_cataclysm );
    return it.get_category().get_id().str();
}

} // namespace

TEST_CASE("ballistic plates, vests and helmets are in the armor category", "[item][category]")
{
    // DDA b4b62254ae2 data/json/items/armor/ballistic_armor.json: "category": "armor".
    CHECK(category_id_of("esapi_plate") == "armor");
    CHECK(category_id_of("esbi_plate") == "armor");
    CHECK(category_id_of("ballistic_vest_esapi") == "armor");
    CHECK(category_id_of("helmet_army") == "armor");
}

TEST_CASE("plain garments are in the clothing category", "[item][category]")
{
    // No "category" in JSON: calc_category's obj.armor -> clothing rule.
    CHECK(category_id_of("tshirt") == "clothing");
    CHECK(category_id_of("jeans") == "clothing");
}

TEST_CASE("item categories have a DDA rank and an all-caps header", "[item][category]")
{
    // DDA b4b62254ae2 data/json/item_category.json: armor sort_rank 20, clothing 19.
    REQUIRE(item_category_id("armor").is_valid());
    CHECK(item_category_id("armor").obj().name() == "ARMOR");
    CHECK(item_category_id("armor").obj().sort_rank() == 20);
    CHECK(item_category_id("clothing").obj().name() == "CLOTHING");
    CHECK(item_category_id("clothing").obj().sort_rank() == 19);
    CHECK(item_category_id("armor").obj().sort_rank() >
          item_category_id("clothing").obj().sort_rank());

    // Pseudo categories made in code (src/inventory_ui.cpp:1914-1922) keep their caps.
    CHECK(item_category_id("ITEMS_WORN").obj().name() == "ITEMS WORN");
    CHECK(item_category_id("WEAPON_HELD").obj().name() == "WEAPON HELD");
}
