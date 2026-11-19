#include "catch/catch.hpp"
#include "fstream_utils.h"
#include "json.h"
#include "flag.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse_actor.h"
#include "units.h"
#include "itype_slots/islot_tool.h"
#include "itype_slots/islot_container.h"
#include "itype_slots/islot_armor.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

auto count_type(const itype& t, pocket_type pt) -> int {
    return static_cast<int>(std::ranges::count_if(t.pockets, [pt](const pocket_data& p) {
        return p.type == pt;
    }));
}

// The holster pockets of the worn layouts (CAP1c): the item_overlay entries of
// cdf_overlay_garment_pockets.json that carry "holster": true. A garment without a layout gets
// the 10 L strap pocket when its storage is 10 L or more (item_factory synthesize_pockets).
auto overlay_holster_counts() -> const std::map<std::string, int>& {
    static const std::map<std::string, int> counts = [] {
        std::map<std::string, int> ret;
        const bool read = read_from_file_json("data/json/items/cdf_overlay_garment_pockets.json",
        [&ret](JsonIn& jsin) {
            JsonArray all = jsin.get_array();
            while (all.has_more()) {
                JsonObject overlay = all.next_object();
                overlay.allow_omitted_members();
                int holsters = 0;
                JsonArray pockets = overlay.get_array("pocket_data");
                while (pockets.has_more()) {
                    JsonObject pocket = pockets.next_object();
                    pocket.allow_omitted_members();
                    if (pocket.get_bool("holster", false)) { holsters++; }
                }
                ret[overlay.get_string("id")] = holsters;
            }
        });
        REQUIRE(read);
        return ret;
    }();
    return counts;
}

auto worn_holsters(const itype& t) -> int {
    if (!t.armor || t.armor->storage <= 0_ml) { return 0; }
    const auto layout = overlay_holster_counts().find(t.get_id().str());
    if (layout != overlay_holster_counts().end()) { return layout->second; }
    return t.armor->storage >= 10_liter ? 1 : 0;
}

// The pocket set the plan's step-2 table predicts for a type without "pocket_data".
auto predicted_pockets(const itype& t) -> std::map<pocket_type, int> {
    std::map<pocket_type, int> ret;
    int containers = 0;
    if (t.container) { containers++; }
    // P1f: holsters and bandoliers are explicit pocket_data now, there is nothing to synthesise
    // attach_molle is no longer a pocket: MOLLE is DDA additional_pockets (J5, owner decision)
    // worn storage: the carry pockets are checked in garment_pocket_test, the holsters are counted here
    if (containers > 0) { ret[pocket_type::CONTAINER] = containers; }
    if (worn_holsters(t) > 0) { ret[pocket_type::CONTAINER] += worn_holsters(t); }
    if (t.magazine) { ret[pocket_type::MAGAZINE] = 1; }
    // 4-b2: a tool with an ammo type and max_charges gets an integral MAGAZINE pocket (money keeps charges)
    if (t.tool && !t.tool->ammo_id.empty() && t.tool->max_charges > 0 && t.magazines.empty() &&
        !t.tool->ammo_id.contains(ammotype("money"))) {
        ret[pocket_type::MAGAZINE] = 1;
    }
    if ((t.gun || t.tool) && !t.magazines.empty()) { ret[pocket_type::MAGAZINE_WELL] = 1; }
    if (t.gun || (t.tool && (!t.tool->ammo_id.empty() || !t.magazines.empty()))) {
        ret[pocket_type::MOD] = 1;
    }
    if (t.has_flag(flag_CORPSE)) { ret[pocket_type::CORPSE] = 1; }
    if (t.get_id() == itype_id("usb_drive")) { ret[pocket_type::SOFTWARE] = 1; }
    // CDF: LINKUP1b, DDA item_factory.cpp:4144-4148: a link_up item gets a CABLE pocket.
    if (t.get_use("link_up") != nullptr) { ret[pocket_type::CABLE] = 1; }
    return ret;
}

auto actual_pockets(const itype& t) -> std::map<pocket_type, int> {
    std::map<pocket_type, int> ret;
    for (const pocket_data& p : t.pockets) {
        if (!p.carry) { ret[p.type]++; } // worn storage is checked in garment_pocket_test
    }
    return ret;
}

} // namespace

TEST_CASE("every item type gets the predicted pockets", "[item][pocket][synthesis]") {
    int checked = 0;
    for (const itype* t : item_controller->all()) {
        if (t->explicit_pockets) { continue; }
        INFO(t->get_id().str());
        CHECK(actual_pockets(*t) == predicted_pockets(*t));
        CHECK_FALSE(std::ranges::any_of(t->pockets, [](const pocket_data& p) {
            return p.type == pocket_type::MIGRATION;
        }));
        for (const pocket_data& p : t->pockets) { CHECK(p.check_definition() == ""); }
        checked++;
    }
    CHECK(checked > 1000);
}

TEST_CASE("synthesised pockets spot checks", "[item][pocket][synthesis]") {
    SECTION("canteen: one watertight container") {
        const itype& t = *itype_id("canteen");
        REQUIRE(t.pockets.size() == 1);
        const pocket_data& p = t.pockets.front();
        CHECK(p.type == pocket_type::CONTAINER);
        CHECK(p.watertight);
        CHECK(p.raw_volume_capacity == t.container->contains);
        CHECK_FALSE(p.holster);
    }
    SECTION("holster: one holster pocket from pocket_data, thin use action") {
        const itype& t = *itype_id("holster");
        REQUIRE(t.get_use("holster") != nullptr);
        REQUIRE(t.explicit_pockets);
        REQUIRE(count_type(t, pocket_type::CONTAINER) == 1);
        const pocket_data& p = t.pockets.front();
        CHECK(p.holster);
        // P-2: the holster use action makes its holster pockets refuse active items
        CHECK(p.no_active_items);
        CHECK(p.moves == 50);
    }
    SECTION("two holster pockets declared in pocket_data") {
        const itype& t = *itype_id("test_holster_two");
        REQUIRE(t.pockets.size() == 2);
        for (const pocket_data& p : t.pockets) {
            CHECK(p.holster);
            CHECK(p.no_active_items);
            CHECK(p.max_item_volume == 1_liter);
            CHECK(p.min_item_volume == 100_ml);
            CHECK(p.max_contains_weight == 2_kilogram);
            CHECK(p.moves == 50);
        }
    }
    SECTION("bandolier: one ammo-restricted container") {
        const itype& t = *itype_id("bandolier_shotgun");
        REQUIRE(t.pockets.size() == 1);
        const pocket_data& p = t.pockets.front();
        CHECK(p.type == pocket_type::CONTAINER);
        CHECK_FALSE(p.ammo_restriction.empty());
    }
    SECTION("STANAG magazine: magazine pocket for 30 rounds") {
        const itype& t = *itype_id("stanag30");
        REQUIRE(t.pockets.size() == 1);
        const pocket_data& p = t.pockets.front();
        CHECK(p.type == pocket_type::MAGAZINE);
        CHECK(p.ammo_restriction.at(ammotype("223")) == 30);
    }
    SECTION("M4: magazine well and mod pocket") {
        const itype& t = *itype_id("m4a1");
        CHECK(count_type(t, pocket_type::MAGAZINE_WELL) == 1);
        CHECK(count_type(t, pocket_type::MOD) == 1);
        CHECK(count_type(t, pocket_type::CONTAINER) == 0);
        const auto well = std::ranges::find_if(t.pockets, [](const pocket_data& p) {
            return p.type == pocket_type::MAGAZINE_WELL;
        });
        CHECK(well->item_id_restriction.contains(itype_id("stanag30")));
    }
    SECTION("flashlight: battery well and mod pocket") {
        const itype& t = *itype_id("flashlight");
        CHECK(count_type(t, pocket_type::MAGAZINE_WELL) == 1);
        CHECK(count_type(t, pocket_type::MOD) == 1);
    }
    SECTION("corpse: corpse pocket") {
        const itype& t = *itype_id("corpse");
        CHECK(count_type(t, pocket_type::CORPSE) == 1);
    }
    SECTION("usb drive: software pocket") {
        const itype& t = *itype_id("usb_drive");
        // CDF: BN's digital-media data gives the drive container_data (1 ml for its files) next to
        // the software pocket, so it carries a CONTAINER pocket as well
        REQUIRE(t.pockets.size() == 2);
        CHECK(count_type(t, pocket_type::SOFTWARE) == 1);
        CHECK(count_type(t, pocket_type::CONTAINER) == 1);
    }
    SECTION("rock: no pockets") { CHECK(itype_id("rock")->pockets.empty()); }
}

TEST_CASE("explicit pocket_data overrides synthesis", "[item][pocket][synthesis]") {
    const itype& t = *itype_id("test_pocket_two");
    CHECK(t.explicit_pockets);
    REQUIRE(t.pockets.size() == 2);
    CHECK(t.pockets[0].raw_volume_capacity == 2_liter);
    CHECK(t.pockets[0].max_contains_weight == 5_kilogram);
    CHECK(t.pockets[0].moves == 120);
    CHECK(t.pockets[1].raw_volume_capacity == 500_ml);
    CHECK(t.pockets[1].watertight);
    CHECK(t.pockets[1].rigid);

    const itype& dda = *itype_id("test_pocket_dda");
    REQUIRE(dda.pockets.size() == 1);
    CHECK(dda.pockets[0].raw_volume_capacity == 1_liter);
    CHECK(dda.pockets[0].max_item_length == 20_cm);
}
