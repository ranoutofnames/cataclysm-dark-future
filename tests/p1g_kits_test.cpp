// CDF-authored (P1g). DDA has no unpack mechanism: a kit is a pocketed container and its
// contents are spawned inside it by a `container-item` item group. These tests pin the data
// side of that port: no BN unpack/fold use_actions remain, the BN kit ids are containers, the
// removed ids migrate, and the kit groups spawn their contents inside the kit.
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

// The BN kit ids that used `unpack`, `UNPACK_ITEM` or `PACK_ITEM` and still exist; the `_on`
// toggles and the two go bags no longer exist as items and are checked separately.
const std::vector<std::string> kit_item_ids =
    {"1st_aid",           "1st_aid_deluxe",  "1st_aid_ifak",   "premium_survival_kit",
     "alien_pod_storage", "alien_pod_organ", "folding_poncho", "emer_blanket"};

const std::vector<std::string> removed_item_ids =
    {"gobag", "personal_gobag", "folding_poncho_on", "emer_blanket_on"};

auto has_container_pocket(const itype& type) -> bool {
    return std::any_of(type.pockets.begin(), type.pockets.end(), [](const pocket_data& pocket) {
        return pocket.type == pocket_type::CONTAINER;
    });
}

// Depth-first search of `it`'s contents (any pocket, any nesting) for an item of type `id`.
auto find_in_contents(const item& it, const itype_id& id) -> const item* {
    for (const item* child : it.contents.all_items_top()) {
        if (child->typeId() == id) { return child; }
        if (const item* found = find_in_contents(*child, id)) { return found; }
    }
    return nullptr;
}

// Spawns `group` and returns the single kit container it must produce.
auto spawn_single_kit(const std::string& group) -> detached_ptr<item> {
    std::vector<detached_ptr<item>> spawned;
    const std::string msg = capture_debugmsg_during([&spawned, &group]() {
        spawned = item_group::items_from(item_group_id(group));
    });
    CHECK(msg.empty());
    REQUIRE(spawned.size() == 1);
    return std::move(spawned[0]);
}

} // namespace

TEST_CASE("kit items have no unpack or fold use_action", "[p1g][kit]") {
    clear_all_state();
    for (const std::string& id : kit_item_ids) {
        CAPTURE(id);
        REQUIRE(itype_id(id).is_valid());
        const itype& type = itype_id(id).obj();
        CHECK(type.get_use("unpack") == nullptr);
        CHECK(type.get_use("UNPACK_ITEM") == nullptr);
        CHECK(type.get_use("PACK_ITEM") == nullptr);
    }
}

TEST_CASE("removed kit ids migrate to their DDA replacements", "[p1g][kit][migration]") {
    clear_all_state();
    for (const std::string& id : removed_item_ids) {
        CAPTURE(id);
        CHECK_FALSE(itype_id(id).is_valid());
    }
    CHECK(item_controller->migrate_id(itype_id("gobag")) == itype_id("duffelbag"));
    CHECK(item_controller->migrate_id(itype_id("personal_gobag")) == itype_id("duffelbag"));
    CHECK(item_controller->migrate_id(itype_id("folding_poncho_on")) == itype_id("folding_poncho"));
    CHECK(item_controller->migrate_id(itype_id("emer_blanket_on")) == itype_id("emer_blanket"));
}

TEST_CASE("kit ids are containers with a CONTAINER pocket", "[p1g][kit]") {
    clear_all_state();
    for (const std::string& id :
         {"1st_aid", "1st_aid_deluxe", "1st_aid_ifak", "premium_survival_kit", "alien_pod_storage",
          "alien_pod_organ"}) {
        CAPTURE(id);
        REQUIRE(itype_id(id).is_valid());
        CHECK(has_container_pocket(itype_id(id).obj()));
    }
    // poncho and blanket are plain armor now: worn, no pocket
    for (const std::string& id : {"folding_poncho", "emer_blanket"}) {
        CAPTURE(id);
        REQUIRE(itype_id(id).is_valid());
        const itype& type = itype_id(id).obj();
        CHECK(static_cast<bool>(type.armor));
        CHECK_FALSE(has_container_pocket(type));
    }
}

TEST_CASE("kit item groups spawn their contents inside the kit", "[p1g][kit][item_group]") {
    clear_all_state();
    const std::vector<std::pair<std::string, std::string>> groups =
        {{"individual_first_aid_kit", "1st_aid_ifak"},
         {"pocket_first_aid_kit", "1st_aid"},
         {"deluxe_first_aid_kit", "1st_aid_deluxe"},
         {"premium_contents", "premium_survival_kit"},
         {"gobag_contents", "duffelbag"},
         {"migo_storage_pod_contents", "alien_pod_storage"},
         {"migo_organ_pod_contents", "alien_pod_organ"}};
    for (const auto& group : groups) {
        CAPTURE(group.first);
        std::vector<detached_ptr<item>> spawned;
        const std::string msg = capture_debugmsg_during([&spawned, &group]() {
            spawned = item_group::items_from(item_group_id(group.first));
        });
        CHECK(msg.empty());
        // one kit container, with everything inside it and nothing loose beside it
        REQUIRE(spawned.size() == 1);
        CHECK(spawned[0]->typeId() == itype_id(group.second));
        CHECK_FALSE(spawned[0]->contents.empty());
    }
}

TEST_CASE("kit groups hold their listed items inside the kit container", "[p1g][kit][item_group]") {
    clear_all_state();
    // all entries of these groups are certain (no `prob`), so each listed item must be inside
    const std::vector<std::pair<std::string, std::vector<std::string>>> kits = {
        {"pocket_first_aid_kit", {"medical_tape", "bandages", "medical_gauze", "booklet_firstaid"}},
        {"deluxe_first_aid_kit",
         {"medical_tape", "aspirin", "bandages", "medical_gauze", "scissors"}},
        {"individual_first_aid_kit", {"medical_tape", "bandages", "medical_gauze"}},
        {"premium_contents", {"knife_folding", "flint_steel", "picklocks", "handflare"}}};
    const std::vector<std::string> kit_container_ids =
        {"1st_aid", "1st_aid_deluxe", "1st_aid_ifak", "premium_survival_kit"};
    for (size_t i = 0; i < kits.size(); ++i) {
        const std::string& group = kits[i].first;
        CAPTURE(group);
        const detached_ptr<item> kit = spawn_single_kit(group);
        REQUIRE(static_cast<bool>(kit));
        CHECK(kit->typeId() == itype_id(kit_container_ids[i]));
        REQUIRE_FALSE(kit->contents.empty());
        REQUIRE_FALSE(kit->contents.all_items_top().empty());
        for (const std::string& content_id : kits[i].second) {
            CAPTURE(content_id);
            REQUIRE(itype_id(content_id).is_valid());
            CHECK(find_in_contents(*kit, itype_id(content_id)) != nullptr);
        }
    }
}

TEST_CASE(
    "gobag contents is a duffelbag holding a filled first aid kit", "[p1g][kit][item_group]") {
    clear_all_state();
    const detached_ptr<item> bag = spawn_single_kit("gobag_contents");
    REQUIRE(static_cast<bool>(bag));
    CHECK(bag->typeId() == itype_id("duffelbag"));
    REQUIRE_FALSE(bag->contents.empty());
    const item* ifak = find_in_contents(*bag, itype_id("1st_aid_ifak"));
    REQUIRE(ifak != nullptr);
    REQUIRE_FALSE(ifak->contents.empty());
    CHECK(find_in_contents(*ifak, itype_id("bandages")) != nullptr);
}
