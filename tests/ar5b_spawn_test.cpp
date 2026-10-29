#include "catch/catch.hpp"

#include "fstream_utils.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "json.h"
#include "rng.h"
#include "state_helpers.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

// AR5B-SPAWN: the AR-5 vests and plates reach the CDF groups that play DDA's spawn roles.
// Ids and weights are DDA b4b62254ae2 as written (functionality port).

namespace {

auto group_holds(const char *group, const char *id) -> bool {
    return item_group::group_contains_item(item_group_id(group), itype_id(id));
}

// Spawn `draws` times from a group and count how often `id` appeared (fixed seed).
auto count_spawns(const char *group, const char *id, int draws) -> int {
    auto found = 0;
    for (auto i = 0; i < draws; ++i) {
        const std::vector<detached_ptr<item>> spawned = item_group::items_from(item_group_id(group));
        for (const detached_ptr<item> &it : spawned) {
            if (it->typeId() == itype_id(id)) {
                ++found;
            }
        }
    }
    return found;
}

} // namespace

TEST_CASE("ar5b_armor_plates_group", "[armor][ar5b][spawn]") {
    clear_all_state();
    // DDA itemgroups/military.json:19-30; CDF: the plate ids are defined in
    // data/json/items/armor/cdf_ballistic_armor.json (stab_plate from AR5-4)
    const std::set<std::string> expected = {"esapi_plate", "stab_plate", "steel_ballistic_plate",
                                            "heavy_steel_ballistic_plate", "esbi_plate"};
    Item_spawn_data *group = item_controller->get_group(item_group_id("armor_plates"));
    REQUIRE(group != nullptr);
    std::set<std::string> held;
    for (const itype *type : group->every_item()) {
        held.insert(type->get_id().str());
    }
    CHECK(held == expected);
    for (const std::string &id : expected) {
        INFO(id);
        CHECK(itype_id(id).is_valid());
    }
    CHECK(group_holds("armor_plates", "stab_plate"));
}

namespace {

// Read the entries of palette `palette_id`, item symbol `symbol`, from the palette file (tests run
// from the repository root).
struct palette_entry {
    std::string item;
    std::string group;
};

auto palette_symbol_entries(const char *path, const char *palette_id, const char *symbol)
-> std::vector<palette_entry> {
    std::vector<palette_entry> out;
    const bool read = read_from_file_json(path, [&](JsonIn &jsin) {
        JsonArray all = jsin.get_array();
        while (all.has_more()) {
            JsonObject palette = all.next_object();
            palette.allow_omitted_members();
            if (palette.get_string("id", "") != palette_id) {
                continue;
            }
            JsonObject items = palette.get_object("items");
            items.allow_omitted_members();
            JsonObject sym = items.get_object(symbol);
            sym.allow_omitted_members();
            JsonObject item = sym.get_object("item");
            item.allow_omitted_members();
            JsonArray entries = item.get_array("entries");
            while (entries.has_more()) {
                JsonObject entry = entries.next_object();
                entry.allow_omitted_members();
                out.push_back({entry.get_string("item", ""), entry.get_string("group", "")});
            }
        }
    });
    REQUIRE(read);
    return out;
}

} // namespace

TEST_CASE("ar5b_mil_surplus_palette_lists_vests_and_plates", "[armor][ar5b][spawn]") {
    clear_all_state();
    // DDA mapgen_palettes/mil_surplus.json:200-208 (looted form); CDF palette mil_surplus, symbol B
    const std::vector<palette_entry> entries =
        palette_symbol_entries("data/json/mapgen_palettes/mil_surplus.json", "mil_surplus", "B");
    REQUIRE_FALSE(entries.empty());
    const auto has_item = [&](const char *id) {
        return std::any_of(entries.begin(), entries.end(),
                           [&](const palette_entry &e) { return e.item == id; });
    };
    CHECK(has_item("ballistic_vest_esapi"));
    CHECK(has_item("ballistic_vest_light"));
    CHECK(has_item("soft_3a_vest"));
    CHECK(std::any_of(entries.begin(), entries.end(),
                      [](const palette_entry &e) { return e.group == "armor_plates"; }));
}

