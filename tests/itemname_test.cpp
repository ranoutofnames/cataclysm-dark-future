#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "detached_ptr.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "item.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

#include <memory>
#include <string>

TEST_CASE("item sizing display", "[item][iteminfo][display_name][sizing]") {
    clear_all_state();
    GIVEN("player is a normal size") {
        g->u.clear_mutations();

        WHEN("the item is a normal size") {
            std::string name = item::spawn_temporary("chestguard_hard")->display_name();
            THEN("the item name has no qualifier") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>hard chest guard");
            }
        }

        WHEN("the item is oversized") {
            std::string name = item::spawn_temporary("bootsheath")->display_name();
            THEN("the item name has no qualifier") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>ankle sheath");
            }
        }

        WHEN("the item is undersized") {
            item& i = *item::spawn_temporary("tunic");
            i.set_flag(flag_UNDERSIZE);
            i.set_flag(flag_FIT);
            std::string name = i.display_name();

            THEN("we have the correct sizing") {
                const item::sizing sizing_level = i.get_sizing(g->u);
                CHECK(sizing_level == item::sizing::small_sized_human_char);
            }

            THEN("the item name says its too small") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>tunic (too small)");
            }
        }
    }

    GIVEN("player is a huge size") {
        g->u.clear_mutations();
        g->u.toggle_trait(trait_id("HUGE_OK"));

        WHEN("the item is a normal size") {
            std::string name = item::spawn_temporary("chestguard_hard")->display_name();
            THEN("the item name says its too small") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>hard chest guard (too small)");
            }
        }

        WHEN("the item is oversized") {
            std::string name = item::spawn_temporary("bootsheath")->display_name();
            THEN("the item name has no qualifier") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>ankle sheath");
            }
        }

        WHEN("the item is undersized") {
            item& i = *item::spawn_temporary("tunic");
            i.set_flag(flag_UNDERSIZE);
            i.set_flag(flag_FIT);
            std::string name = i.display_name();

            THEN("we have the correct sizing") {
                const item::sizing sizing_level = i.get_sizing(g->u);
                CHECK(sizing_level == item::sizing::small_sized_big_char);
            }

            THEN("the item name says its tiny") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>tunic (tiny!)");
            }
        }
    }

    GIVEN("player is a tiny size") {
        g->u.clear_mutations();
        g->u.toggle_trait(trait_id("SMALL2_OK"));

        WHEN("the item is a normal size") {
            std::string name = item::spawn_temporary("chestguard_hard")->display_name();
            THEN("the item name says its too big") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>hard chest guard (too big)");
            }
        }

        WHEN("the item is oversized") {
            std::string name = item::spawn_temporary("bootsheath")->display_name();
            THEN("the item name has no qualifier") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>ankle sheath (huge!)");
            }
        }

        WHEN("the item is undersized") {
            item& i = *item::spawn_temporary("tunic");
            i.set_flag(flag_UNDERSIZE);
            i.set_flag(flag_FIT);
            std::string name = i.display_name();

            THEN("we have the correct sizing") {
                const item::sizing sizing_level = i.get_sizing(g->u);
                CHECK(sizing_level == item::sizing::small_sized_small_char);
            }

            THEN("the item name has no qualifier") {
                CHECK(name == "<color_c_light_green>||\u00A0</color>tunic");
            }
        }
    }
}

// DDA source: tests/itemname_test.cpp:136-166 (DDA commit b4b62254ae2), P1f K7.
// CDF: items are spawned detached, and the name format is BN's: a container with a single content
// stack shows the content name, the container in brackets and "(held/capacity)".
TEST_CASE("display_name_includes_item_contents", "[item][display_name][contents]") {
    clear_all_state();

    detached_ptr<item> arrow_ptr =
        item::spawn(itype_id("test_arrow_wood"), calendar::turn_zero, item::default_charges_tag{});
    // Arrows are ammo with a default count of 10
    REQUIRE(arrow_ptr->is_ammo());
    REQUIRE(arrow_ptr->count() == 10);

    item& quiver = *item::spawn_temporary(itype_id("test_quiver"));
    // Quivers are not magazines, nor do they have magazines
    REQUIRE_FALSE(quiver.is_magazine());
    REQUIRE_FALSE(quiver.magazine_current());
    // But they can contain ammo (CDF: item::ammo_types stays empty for non-gun, tool or magazine)
    REQUIRE(quiver.can_contain(*arrow_ptr).success());

    // Check empty quiver display
    CHECK(quiver.display_name()
          == "<color_c_light_green>||\u00A0</color>test quiver<color_c_light_gray> (0/20)</color>");

    // Insert one stack of arrows
    quiver.put_in(std::move(arrow_ptr), pocket_type::CONTAINER);
    // Expect 10 arrows remaining and displayed
    CHECK(quiver.ammo_remaining() == 10);
    CHECK(
        quiver.display_name()
        == "<color_c_light_green>||\u00A0</color>test wooden broadhead arrow (test quiver) (10/20)");
}
