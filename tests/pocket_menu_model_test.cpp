// CDF-authored tests for the pocket management menu model (src/item_pocket_menu.cpp), which holds
// everything of DDA's pocket_favorite_callback except the drawing.
#include "calendar.h"
#include "catch/catch.hpp"
#include "game.h"
#include "input.h"
#include "item.h"
#include "item_category.h"
#include "item_pocket.h"
#include "item_pocket_menu.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>
#include <vector>

static const itype_id itype_backpack_hiking("backpack_hiking");
static const itype_id itype_test_rag("test_rag");
static const itype_id itype_test_rock("test_rock");

TEST_CASE("pocket menu model lists pockets and maps rows", "[pocket][favorite][ui]") {
    clear_all_state();
    detached_ptr<item> pack_d = item::spawn(itype_backpack_hiking, calendar::turn);
    item& pack = *pack_d;
    const std::vector<item_pocket*> pockets = pack.get_container_pockets();
    REQUIRE(!pockets.empty());

    pocket_settings_model model({&pack});
    REQUIRE(model.rows().size() == pockets.size() + 1);
    CHECK(model.rows()[0].pocket == nullptr);
    CHECK_FALSE(model.rows()[0].enabled);
    CHECK(model.pocket_at(0) == nullptr);
    CHECK(model.pocket_at(-1) == nullptr);
    CHECK(model.pocket_at(99) == nullptr);
    for (size_t i = 0; i < pockets.size(); ++i) {
        CHECK(model.pocket_at(i + 1) == pockets[i]);
        CHECK(model.pocket_number_at(i + 1) == static_cast<int>(i) + 1);
        CHECK(model.rows()[i + 1].enabled);
        CHECK(model.rows()[i + 1].text.find(std::to_string(i + 1) + " - ") == 0);
    }

    SECTION("a nested container is listed with an indent") {
        detached_ptr<item> inner_d = item::spawn(itype_backpack_hiking, calendar::turn);
        item& inner = *inner_d;
        REQUIRE(pockets.front()->insert_item(std::move(inner_d)).success());
        model.rebuild();
        CHECK(model.rows().size() > pockets.size() + 1);
        bool found = false;
        for (const pocket_row& row : model.rows()) {
            if (row.pocket == nullptr && row.holder != &pack) {
                found = true;
                CHECK(row.text.find("  ") == 0);
            }
        }
        CHECK(found);
    }
}

TEST_CASE("pocket menu model edits mark the settings as edited", "[pocket][favorite][ui]") {
    clear_all_state();
    detached_ptr<item> pack_d = item::spawn(itype_backpack_hiking, calendar::turn);
    item_pocket& pk = *pack_d->get_container_pockets().front();
    item& pack_ref = *pack_d;
    pocket_settings_model model({&pack_ref});
    REQUIRE_FALSE(pk.settings.was_edited());

    model.set_priority(pk, 5);
    CHECK(pk.settings.priority() == 5);
    CHECK(pk.settings.was_edited());

    const bool disabled = pk.settings.is_disabled();
    model.toggle_auto_pickup(pk);
    CHECK(pk.settings.is_disabled() == !disabled);
    const bool unloadable = pk.settings.is_unloadable();
    model.toggle_auto_unload(pk);
    CHECK(pk.settings.is_unloadable() == !unloadable);

    SECTION("whitelist and blacklist modes") {
        model.whitelist = true;
        model.toggle_item(pk, itype_test_rock);
        CHECK(pk.settings.get_item_whitelist().count(itype_test_rock) == 1);
        model.toggle_item(pk, itype_test_rock);
        CHECK(pk.settings.get_item_whitelist().empty());
        model.whitelist = false;
        model.toggle_item(pk, itype_test_rag);
        CHECK(pk.settings.get_item_blacklist().count(itype_test_rag) == 1);
        model.toggle_category(pk, item_category_id("tools"));
        CHECK(pk.settings.get_category_blacklist().count(item_category_id("tools")) == 1);
    }
    SECTION("whitelist contents and clear") {
        REQUIRE(pk.insert_item(item::spawn(itype_test_rock, calendar::turn)).success());
        CHECK(model.whitelist_contents(pk));
        CHECK(pk.settings.get_item_whitelist().count(itype_test_rock) == 1);
        CHECK_FALSE(model.whitelist_contents(pk));
        model.clear(pk);
        CHECK(pk.settings.get_item_whitelist().empty());
        CHECK(pk.settings.was_edited());
    }
}

TEST_CASE("pocket menu model moves items between pockets", "[pocket][favorite][ui]") {
    clear_all_state();
    Character& guy = get_player_character();
    clear_avatar();
    detached_ptr<item> pack_d = item::spawn(itype_backpack_hiking, calendar::turn);
    item& pack = *pack_d;
    detached_ptr<item> inner_d = item::spawn(itype_backpack_hiking, calendar::turn);
    item& inner = *inner_d;
    item_pocket& outer_pk = *pack.get_container_pockets().front();
    REQUIRE(outer_pk.insert_item(std::move(inner_d)).success());
    pocket_settings_model model({&pack});

    int outer_row = -1;
    int inner_row = -1;
    for (size_t i = 0; i < model.rows().size(); ++i) {
        if (model.rows()[i].pocket == &outer_pk) {
            outer_row = i;
        } else if (model.rows()[i].pocket != nullptr && model.rows()[i].holder == &inner
                   && inner_row < 0) {
            inner_row = i;
        }
    }
    REQUIRE(outer_row >= 0);
    REQUIRE(inner_row >= 0);

    CHECK(pocket_settings_model::movable_items(outer_pk) == std::vector<item*>{&inner});
    // an item can not go inside itself
    CHECK_FALSE(model.can_move_to(inner, inner_row));
    // a pocket that holds nothing yet takes a rock
    detached_ptr<item> rock = item::spawn(itype_test_rock, calendar::turn);
    CHECK(model.can_move_to(*rock, inner_row));
    CHECK_FALSE(model.can_move_to(*rock, 0));

    SECTION("move through store") {
        item& rock_ref = *rock;
        REQUIRE(outer_pk.insert_item(std::move(rock)).success());
        const std::string err =
            pocket_settings_model::move_item(guy, rock_ref, *model.pocket_at(inner_row));
        CHECK(err.empty());
        model.rebuild();
        int rocks = 0;
        inner.visit_items([&](const item* it) {
            rocks += it->typeId() == itype_test_rock ? 1 : 0;
            return VisitResponse::NEXT;
        });
        CHECK(rocks == 1);
    }
}

TEST_CASE("pocket menu keybindings are registered", "[pocket][favorite][ui][keybinding]") {
    for (const std::string& id :
         {"FAV_PRIORITY", "FAV_AUTO_PICKUP", "FAV_AUTO_UNLOAD", "FAV_ITEM", "FAV_CATEGORY",
          "FAV_WHITELIST", "FAV_WHITELIST_CONTENTS", "FAV_BLACKLIST", "FAV_SAVE_PRESET",
          "FAV_APPLY_PRESET", "FAV_DEL_PRESET", "FAV_CLEAR", "FAV_MOVE_ITEM", "FAV_CONTEXT_MENU"}) {
        CAPTURE(id);
        CHECK_FALSE(inp_mngr.get_input_for_action(id, "INVENTORY").empty());
    }
}
