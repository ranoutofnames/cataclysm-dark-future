// CDF-authored tests for the DDA pocket settings consumers that P2-1 added:
// COLLAPSE_CONTENTS, stacking, the name marker and the settings kept by rebuild_pockets.
// DDA has no tests for these (its item_tname_test "container has whitelist" sits in the
// tname segment test BN cannot run).
#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "filesystem.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "item_pocket.h"
#include "json.h"
#include "npc.h"
#include "path_info.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static const itype_id itype_backpack_hiking("backpack_hiking");
static const itype_id itype_bottle_plastic("bottle_plastic");
static const itype_id itype_stanag30("stanag30");
static const itype_id itype_test_box("test_box");
static const itype_id itype_test_pocket_two("test_pocket_two");
static const itype_id itype_test_rag("test_rag");
static const itype_id itype_test_rock("test_rock");

static const item_group_id test_ps_bulk_group("test_ps_bulk_group");
static const item_group_id test_ps_single_group("test_ps_single_group");

static auto first_pocket(item& it) -> item_pocket& {
    const std::vector<item_pocket*> pkts = it.get_container_pockets();
    REQUIRE(!pkts.empty());
    return *pkts.front();
}

TEST_CASE("COLLAPSE_CONTENTS collapses the standard pockets", "[pocket][favorite]") {
    clear_all_state();
    SECTION("an item with the flag starts collapsed") {
        // is_collapsed() is false for an empty item
        detached_ptr<item> bottle = item::spawn(itype_bottle_plastic, calendar::turn);
        bottle->put_in(item::spawn(itype_id("water_clean"), calendar::turn, 1));
        REQUIRE(!bottle->contents.empty());
        CHECK(bottle->is_collapsed());
    }
    SECTION("an item without the flag does not") {
        detached_ptr<item> pack = item::spawn(itype_backpack_hiking, calendar::turn);
        CHECK_FALSE(pack->is_collapsed());
        detached_ptr<item> box = item::spawn(itype_test_box, calendar::turn);
        CHECK_FALSE(box->is_collapsed());
    }
    SECTION("magazine pockets collapse by default, but the item is not collapsed") {
        detached_ptr<item> mag = item::spawn(itype_stanag30, calendar::turn);
        const std::vector<item_pocket*> mag_pockets = mag->get_pockets([](const item_pocket& p) {
            return p.is_type(pocket_type::MAGAZINE) || p.is_type(pocket_type::MAGAZINE_WELL);
        });
        REQUIRE(!mag_pockets.empty());
        for (const item_pocket* pkt : mag_pockets) { CHECK(pkt->settings.is_collapsed()); }
        CHECK_FALSE(mag->is_collapsed());
    }
}

TEST_CASE("containers with different white and blacklists do not stack", "[pocket][favorite]") {
    clear_all_state();
    detached_ptr<item> a = item::spawn(itype_test_pocket_two, calendar::turn);
    detached_ptr<item> b = item::spawn(itype_test_pocket_two, calendar::turn);
    REQUIRE(a->stacks_with(*b));

    first_pocket(*a).settings.whitelist_item(itype_test_rock);
    CHECK_FALSE(a->stacks_with(*b));
    CHECK_FALSE(b->stacks_with(*a));

    first_pocket(*b).settings.whitelist_item(itype_test_rock);
    CHECK(a->stacks_with(*b));

    first_pocket(*b).settings.blacklist_item(itype_test_rag);
    CHECK_FALSE(a->stacks_with(*b));
    first_pocket(*a).settings.blacklist_item(itype_test_rag);
    CHECK(a->stacks_with(*b));

    first_pocket(*a).settings.whitelist_category(item_category_id("tools"));
    CHECK_FALSE(a->stacks_with(*b));
}

TEST_CASE("a container with a whitelist or blacklist is marked in its name", "[pocket][favorite]") {
    clear_all_state();
    const std::string mark = "⁺";
    detached_ptr<item> pack = item::spawn(itype_backpack_hiking, calendar::turn);
    CHECK(pack->tname().find(mark) == std::string::npos);

    first_pocket(*pack).settings.whitelist_item(itype_test_rock);
    const std::string dark = pack->tname();
    CHECK(dark.find(mark) != std::string::npos);
    CHECK(dark.find("dark_gray") != std::string::npos);

    // a list the player edited is drawn lighter
    first_pocket(*pack).settings.set_was_edited();
    const std::string light = pack->tname();
    CHECK(light.find(mark) != std::string::npos);
    CHECK(light.find("light_gray") != std::string::npos);

    first_pocket(*pack).settings.clear_item(itype_test_rock);
    CHECK(pack->tname().find(mark) == std::string::npos);
}

TEST_CASE("pocket settings survive a layout change", "[pocket][favorite]") {
    clear_all_state();
    detached_ptr<item> bag = item::spawn(itype_test_pocket_two, calendar::turn);
    item_pocket& pkt = first_pocket(*bag);
    pkt.settings.whitelist_item(itype_test_rock);
    pkt.settings.set_priority(7);
    pkt.settings.set_was_edited();

    // two pockets become one: a different layout, so the pockets are rebuilt
    bag->convert(itype_test_box);
    const item_pocket& after = first_pocket(*bag);
    CHECK(after.settings.get_item_whitelist().contains(itype_test_rock));
    CHECK(after.settings.priority() == 7);
    CHECK(after.settings.was_edited());

    bag->convert(itype_test_pocket_two);
    const item_pocket& back = first_pocket(*bag);
    CHECK(back.settings.get_item_whitelist().contains(itype_test_rock));
    CHECK(back.settings.priority() == 7);
}

TEST_CASE("a container's pocket settings round trip through a save", "[pocket][favorite]") {
    clear_all_state();
    detached_ptr<item> pack = item::spawn(itype_backpack_hiking, calendar::turn);
    item_pocket::favorite_settings& s = first_pocket(*pack).settings;
    s.whitelist_item(itype_test_rock);
    s.blacklist_item(itype_test_rag);
    s.set_priority(3);
    s.set_was_edited();

    std::ostringstream out;
    JsonOut jsout(out);
    pack->serialize(jsout);
    std::istringstream in(out.str());
    JsonIn jsin(in);
    detached_ptr<item> loaded = item::spawn(jsin);

    const item_pocket::favorite_settings& l = first_pocket(*loaded).settings;
    CHECK(l.get_item_whitelist().contains(itype_test_rock));
    CHECK(l.get_item_blacklist().contains(itype_test_rag));
    CHECK(l.priority() == 3);
    CHECK(l.was_edited());
}

TEST_CASE("spawned containers get an automatic whitelist", "[pocket][favorite][item_group]") {
    clear_all_state();
    SECTION("a group entry with a container") {
        const std::vector<detached_ptr<item>> items = item_group::items_from(test_ps_single_group);
        REQUIRE(items.size() == 1);
        CHECK(first_pocket(*items.front()).settings.get_item_whitelist().contains(itype_test_rock));
    }
    SECTION("a group entry that fills a container in bulk") {
        const std::vector<detached_ptr<item>> items = item_group::items_from(test_ps_bulk_group);
        REQUIRE(!items.empty());
        CHECK(first_pocket(*items.front()).settings.get_item_whitelist().contains(itype_test_rock));
    }
    SECTION("in_container") {
        detached_ptr<item> rock = item::spawn(itype_test_rock, calendar::turn);
        detached_ptr<item> boxed = item::in_container(itype_test_box, std::move(rock));
        CHECK(first_pocket(*boxed).settings.get_item_whitelist().contains(itype_test_rock));
    }
}

TEST_CASE("pocket presets are saved to and loaded from a file", "[pocket][favorite]") {
    clear_all_state();
    const std::string saved_path = PATH_INFO::pocket_presets();
    const std::string path = PATH_INFO::config_dir() + "test_pocket_presets.json";
    remove_file(path);
    PATH_INFO::set_pocket_presets(path);

    item_pocket::deserialize_presets(JsonArray());
    item_pocket::load_presets();
    CHECK_FALSE(item_pocket::has_preset("keep rocks"));

    item_pocket::favorite_settings preset;
    preset.set_preset_name("keep rocks");
    preset.whitelist_item(itype_test_rock);
    preset.set_priority(5);
    item_pocket::add_preset(preset);
    CHECK(item_pocket::has_preset("keep rocks"));
    CHECK(file_exist(path));

    // forget the list in memory, then read the file back
    item_pocket::pocket_presets.clear();
    REQUIRE_FALSE(item_pocket::has_preset("keep rocks"));
    item_pocket::load_presets();
    REQUIRE(item_pocket::has_preset("keep rocks"));
    const item_pocket::favorite_settings& loaded = *item_pocket::find_preset("keep rocks");
    CHECK(loaded.get_item_whitelist().contains(itype_test_rock));
    CHECK(loaded.priority() == 5);

    item_pocket::delete_preset(item_pocket::find_preset("keep rocks"));
    item_pocket::pocket_presets.clear();
    item_pocket::load_presets();
    CHECK_FALSE(item_pocket::has_preset("keep rocks"));

    SECTION("a damaged file gives a message and an empty list") {
        {
            std::ofstream out(path, std::ios::binary);
            out << "[ { not json";
        }
        const std::string dmsg = capture_debugmsg_during([&]() { item_pocket::load_presets(); });
        CHECK_FALSE(dmsg.empty());
        CHECK(item_pocket::pocket_presets.empty());
    }

    remove_file(path);
    PATH_INFO::set_pocket_presets(saved_path);
    item_pocket::pocket_presets.clear();
}

TEST_CASE(
    "manual liquid pour ignores pocket settings, automatic fills do not", "[pocket][favorite]") {
    clear_all_state();
    // per DDA handle_liquid.cpp:501: a hand-poured liquid goes in, an automatic fill is refused
    avatar& you = get_avatar();
    clear_avatar();
    SECTION("manual") {
        detached_ptr<item> bottle = item::spawn(itype_bottle_plastic, calendar::turn);
        first_pocket(*bottle).settings.set_disabled(true);
        detached_ptr<item> water = item::spawn(itype_id("water_clean"), calendar::turn, 1);
        water = you.pour_into(*bottle, std::move(water), -1, true);
        CHECK_FALSE(water);
        CHECK(!bottle->contents.empty());
    }
    SECTION("automatic") {
        detached_ptr<item> bottle = item::spawn(itype_bottle_plastic, calendar::turn);
        first_pocket(*bottle).settings.set_disabled(true);
        detached_ptr<item> water = item::spawn(itype_id("water_clean"), calendar::turn, 1);
        water = you.pour_into(*bottle, std::move(water));
        CHECK(water);
        CHECK(bottle->contents.empty());
    }
}

TEST_CASE("unload_item leaves pockets that are not unloadable alone", "[pocket][favorite]") {
    clear_all_state();
    avatar& you = get_avatar();
    clear_avatar();
    detached_ptr<item> pack_d = item::spawn(itype_backpack_hiking, calendar::turn);
    item& pack = *pack_d;
    for (item_pocket* pkt : pack.get_container_pockets()) { pkt->settings.set_unloadable(false); }
    pack.put_in(item::spawn(itype_test_rock, calendar::turn));
    REQUIRE(!pack.contents.empty());
    you.i_add(std::move(pack_d));
    avatar_funcs::unload_item(you, pack);
    CHECK_FALSE(pack.contents.empty());
    for (item_pocket* pkt : pack.get_container_pockets()) { pkt->settings.set_unloadable(true); }
    avatar_funcs::unload_item(you, pack);
    CHECK(pack.contents.empty());
}

TEST_CASE("an NPC does not stash into a disabled pocket", "[pocket][favorite][npc]") {
    clear_all_state();
    standard_npc dude("TestCharacter", tripoint_bub_ms(60, 60, 0), {}, 0, 8, 8, 8, 8);
    clear_character(dude, true);
    detached_ptr<item> pack_d = item::spawn(itype_backpack_hiking, calendar::turn);
    item& pack = *pack_d;
    REQUIRE(!dude.wear_item(std::move(pack_d), false));
    detached_ptr<item> rock = item::spawn(itype_test_rock, calendar::turn);
    CHECK(dude.can_stash(*rock).success());
    for (item_pocket* pkt : pack.get_container_pockets()) { pkt->settings.set_disabled(true); }
    // CDF: the body storage still takes the rock, so check the pack is skipped
    const pocket_choice choice = dude.best_pocket(*rock, nullptr, false);
    CHECK(choice.container != &pack);
}
