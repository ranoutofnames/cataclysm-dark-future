#include "ablative_coverage.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "iuse_actor.h"
#include "rng.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

// GRP1: the 40 DDA vest, plate, MOLLE and kit item groups as CDF data (DDA military.json:33-390,
// custom_lbvs.json:4-130, Clothing_Gear/gear.json:446-470 at b4b62254ae2). Data only; the cases are
// appended per step (A plates, B MOLLE, C kits, D vests). Every random draw is seeded.

namespace {
using spawned_items = std::vector<detached_ptr<item>>;

// Collects failures of a draw loop so a broken group reports once, with its first example.
struct failure_log {
    int count = 0;
    std::string first;
    void expect(bool ok, const std::string& what) {
        if (!ok && count++ == 0) { first = what; }
    }
};

auto spawn(const std::string& group) -> spawned_items {
    return item_group::items_from(item_group_id(group));
}

// type ids of the top-level items of a spawn, sorted
auto type_ids(spawned_items& list) -> std::vector<std::string> {
    std::vector<std::string> ids;
    for (detached_ptr<item>& it : list) { ids.push_back(it->typeId().str()); }
    std::sort(ids.begin(), ids.end());
    return ids;
}

auto count_of(const std::vector<std::string>& ids, const std::string& id) -> int {
    return static_cast<int>(std::count(ids.begin(), ids.end(), id));
}

// how many of the ids belong to the set
auto count_in(const std::vector<std::string>& ids, const std::set<std::string>& among) -> int {
    return static_cast<int>(std::count_if(ids.begin(), ids.end(), [&among](const std::string& id) {
        return among.count(id) > 0;
    }));
}

auto possible_ids(const std::string& group) -> std::set<std::string> {
    std::set<std::string> ids;
    for (const itype* type : item_group::every_possible_item_from(item_group_id(group))) {
        ids.insert(type->get_id().str());
    }
    return ids;
}

// CDF: plate kinds of data/json/items/armor/cdf_ballistic_armor.json (large slot: ESAPI, medium
// slot: ESBI)
const std::set<std::string> large_plates = {"esapi_plate", "destroyed_large_ceramic_plate"};
const std::set<std::string> medium_plates = {"esbi_plate", "destroyed_medium_ceramic_plate"};
const std::set<std::string> iotv_plates =
    {"iotv_groin_plate", "iotv_neck_plate", "iotv_shoulder_plate"};

constexpr int max_draw_damage = 2 * itype::damage_scale;
} // namespace

TEST_CASE("grp1_plate_groups_yield_the_right_plates", "[armor][grp1][spawn]") {
    clear_all_state();
    rng_set_engine_seed(20261102);
    constexpr int draws = 300;

    SECTION("pristine_esapi_plates is exactly two ESAPI and two ESBI plates") {
        // CDF: DDA military.json:90, entries esapi_plate x2 and esbi_plate x2, no prob
        failure_log log;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("pristine_esapi_plates");
            const std::vector<std::string> ids = type_ids(list);
            log.expect(count_of(ids, "esapi_plate") == 2 && count_of(ids, "esbi_plate") == 2
                           && ids.size() == 4,
                       "draw " + std::to_string(i) + " has " + std::to_string(ids.size()));
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
    }
    SECTION("pristine_esapi_front_plates is exactly two ESAPI plates") {
        failure_log log;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("pristine_esapi_front_plates");
            log.expect(type_ids(list) == std::vector<std::string>({"esapi_plate", "esapi_plate"}),
                       "draw " + std::to_string(i));
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
    }
    SECTION("some_esapi_plates is four plates, two large and two medium, working or destroyed") {
        // CDF: DDA military.json:47 = 2 x maybe_esapi_plate + 2 x maybe_esbi_plate, each 50/50
        failure_log log;
        std::set<std::string> seen;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("some_esapi_plates");
            const std::vector<std::string> ids = type_ids(list);
            log.expect(ids.size() == 4 && count_in(ids, large_plates) == 2
                           && count_in(ids, medium_plates) == 2,
                       "draw " + std::to_string(i));
            seen.insert(ids.begin(), ids.end());
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
        // both halves of the 50/50 show up for both sizes (300 draws, miss chance below 1e-80)
        CHECK(
            seen
            == std::set<std::string>(
                {"esapi_plate", "destroyed_large_ceramic_plate", "esbi_plate",
                 "destroyed_medium_ceramic_plate"}));
    }
    SECTION("some_esapi_front_plates is two large plates, working or destroyed") {
        failure_log log;
        std::set<std::string> seen;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("some_esapi_front_plates");
            const std::vector<std::string> ids = type_ids(list);
            log.expect(ids.size() == 2 && count_in(ids, large_plates) == 2,
                       "draw " + std::to_string(i));
            seen.insert(ids.begin(), ids.end());
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
        CHECK(seen == large_plates);
    }
    SECTION("maybe_esapi_plate and maybe_esbi_plate give one plate or its destroyed twin") {
        failure_log log;
        std::set<std::string> seen_large;
        std::set<std::string> seen_medium;
        for (int i = 0; i < draws; ++i) {
            spawned_items large = spawn("maybe_esapi_plate");
            spawned_items medium = spawn("maybe_esbi_plate");
            const std::vector<std::string> large_ids = type_ids(large);
            const std::vector<std::string> medium_ids = type_ids(medium);
            log.expect(large_ids.size() == 1 && medium_ids.size() == 1, "draw " + std::to_string(i));
            seen_large.insert(large_ids.begin(), large_ids.end());
            seen_medium.insert(medium_ids.begin(), medium_ids.end());
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
        CHECK(seen_large == large_plates);
        CHECK(seen_medium == medium_plates);
    }
    SECTION("steel, heavy steel and stab plates come in pairs with damage 0 to 2") {
        // CDF: DDA military.json:66-90, { "damage": [ 0, 2 ] } on both entries of each group
        const std::vector<std::pair<std::string, std::string>> groups = {
            {"some_steel_front_plates", "steel_ballistic_plate"},
            {"some_heavy_steel_front_plates", "heavy_steel_ballistic_plate"},
            {"some_stab_front_plates", "stab_plate"},
        };
        for (const auto& row : groups) {
            CAPTURE(row.first);
            failure_log log;
            int lowest = max_draw_damage;
            int highest = 0;
            for (int i = 0; i < draws; ++i) {
                spawned_items list = spawn(row.first);
                const std::vector<std::string> ids = type_ids(list);
                log.expect(ids == std::vector<std::string>({row.second, row.second}),
                           "draw " + std::to_string(i));
                for (detached_ptr<item>& it : list) {
                    log.expect(it->damage() >= 0 && it->damage() <= max_draw_damage,
                               "damage " + std::to_string(it->damage()));
                    lowest = std::min(lowest, it->damage());
                    highest = std::max(highest, it->damage());
                }
            }
            CAPTURE(log.first);
            CHECK(log.count == 0);
            CHECK(lowest == 0);
            CHECK(highest == max_draw_damage);
        }
    }
    SECTION("ballistic_protectors is the three IOTV plates") {
        // CDF: DDA military.json:104
        failure_log log;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("ballistic_protectors");
            log.expect(
                type_ids(list)
                    == std::vector<std::string>(
                        {"iotv_groin_plate", "iotv_neck_plate", "iotv_shoulder_plate"}),
                "draw " + std::to_string(i));
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
    }
    SECTION("civilian_ballistic_inserts gives a pair of steel, stab or ESAPI plates, 30/45/25") {
        // CDF: DDA Clothing_Gear/gear.json:446, group entries with prob 30, 45 and 25
        constexpr int many = 600;
        failure_log log;
        std::map<std::string, int> pairs;
        for (int i = 0; i < many; ++i) {
            spawned_items list = spawn("civilian_ballistic_inserts");
            const std::vector<std::string> ids = type_ids(list);
            const bool pair = ids.size() == 2 && ids[0] == ids[1];
            log.expect(pair, "draw " + std::to_string(i));
            if (pair) { pairs[ids[0]]++; }
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
        CHECK(pairs.size() == 3);
        CHECK(pairs["steel_ballistic_plate"] / static_cast<double>(many)
              == Approx(0.30).margin(0.08));
        CHECK(pairs["stab_plate"] / static_cast<double>(many) == Approx(0.45).margin(0.08));
        CHECK(pairs["esapi_plate"] / static_cast<double>(many) == Approx(0.25).margin(0.08));
    }
    SECTION("civilian_face_armor gives one gas mask, dust mask or ballistic glasses") {
        // CDF: DDA Clothing_Gear/gear.json:457, three entries prob 20 each; the gas mask has
        // "charges": 100
        failure_log log;
        std::map<std::string, int> seen;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("civilian_face_armor");
            const std::vector<std::string> ids = type_ids(list);
            log.expect(ids.size() == 1, "draw " + std::to_string(i));
            for (detached_ptr<item>& it : list) {
                seen[it->typeId().str()]++;
                if (it->typeId().str() == "mask_gas") {
                    // CDF: "charges": 100 of the group entry, the gas mask holds exactly that
                    log.expect(it->ammo_remaining() == 100,
                               "mask_gas has " + std::to_string(it->ammo_remaining()) + " charges");
                }
            }
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
        CHECK(seen.size() == 3);
        CHECK(seen["mask_gas"] > 0);
        CHECK(seen["mask_dust"] > 0);
        CHECK(seen["glasses_bal"] > 0);
    }
}

namespace {
// size of the attach_molle action of an item, or -1 when it has none (as
// tests/pch1_pouches_test.cpp)
auto molle_size(const item& it) -> int {
    const use_function* use = it.get_use("attach_molle");
    if (use == nullptr) { return -1; }
    const molle_attach_actor* actor = dynamic_cast<const molle_attach_actor*>(use->get_actor_ptr());
    return actor == nullptr ? -1 : actor->size;
}

// type ids of the pouches attached to a carrier (its additional pockets), sorted
auto attached_ids(const item& carrier) -> std::vector<std::string> {
    std::vector<std::string> ids;
    for (const item* pouch : carrier.contents.get_added_pockets()) {
        ids.push_back(pouch->typeId().str());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// CDF: the pouches of DDA custom_lbvs.json:33-75 (molle_accessories, molle_vest_bag_accessories)
const std::set<std::string> accessory_pouches =
    {"dump_pouch",      "gadget_pouch",  "sustainment_pouch",        "tool_pouch",
     "radio_pouch",     "grenade_pouch", "tactical_shotshell_pouch", "tactical_grenade_pouch",
     "flashlight_pouch"};
const std::set<std::string> bag_pouches = {
    "dump_pouch",
    "gadget_pouch",
    "sustainment_pouch",
    "tool_pouch",
    "radio_pouch",
    "grenade_pouch",
    "tactical_shotshell_pouch",
    "tactical_grenade_pouch",
    "flashlight_pouch",
    "tactical_holster",
    "canteen_pouch",
    "h2o_pouch",
    "gas_mask_pouch"};
// the pouches a custom_*_lbv group can give: the bag pouches plus its own first entry
const std::set<std::string> lbv_pouches = {
    "dump_pouch",
    "gadget_pouch",
    "sustainment_pouch",
    "tool_pouch",
    "radio_pouch",
    "grenade_pouch",
    "tactical_shotshell_pouch",
    "tactical_grenade_pouch",
    "flashlight_pouch",
    "tactical_holster",
    "canteen_pouch",
    "h2o_pouch",
    "gas_mask_pouch",
    "chestpouch",
    "triple_stacker_pouch"};

auto join(const std::set<std::string>& ids) -> std::string {
    std::string text;
    for (const std::string& id : ids) { text += id + " "; }
    return text;
}
} // namespace

TEST_CASE("grp1_molle_groups_spawn_pouches", "[armor][grp1][spawn]") {
    clear_all_state();
    rng_set_engine_seed(20261103);
    constexpr int draws = 600;

    SECTION(
        "molle_accessories, molle_vest_bag_accessories and molle_belt_contents give only their pouches") {
        // CDF: DDA custom_lbvs.json:20-75; the three groups are collections of pouches
        const std::vector<std::pair<std::string, std::set<std::string>>> rows = {
            {"molle_accessories", accessory_pouches},
            {"molle_vest_bag_accessories", bag_pouches},
            {"molle_belt_contents", bag_pouches},
        };
        for (const auto& row : rows) {
            CAPTURE(row.first);
            CHECK(possible_ids(row.first) == row.second);
            failure_log log;
            std::set<std::string> seen;
            for (int i = 0; i < draws; ++i) {
                spawned_items list = spawn(row.first);
                for (detached_ptr<item>& it : list) {
                    log.expect(row.second.count(it->typeId().str()) > 0, it->typeId().str());
                    seen.insert(it->typeId().str());
                }
            }
            CAPTURE(log.first);
            CHECK(log.count == 0);
            // every pouch of the group shows up (the rarest is 3.3 percent: miss chance below 1e-8)
            CAPTURE(join(seen));
            CHECK(seen == row.second);
        }
    }
    SECTION(
        "custom_light_lbv, custom_medium_lbv and custom_heavy_lbv give one chest pouch run or stacker pouch run") {
        // CDF: DDA custom_lbvs.json:77-106, first entry is a distribution of chestpouch [1,2] and
        // triple_stacker_pouch ([1,2] only on the heavy one), then molle_vest_bag_accessories
        struct lbv_row {
            const char* group;
            int max_chest;
            int max_stacker;
        };
        const std::vector<lbv_row> rows =
            {{"custom_light_lbv", 2, 1}, {"custom_medium_lbv", 2, 1}, {"custom_heavy_lbv", 2, 2}};
        for (const lbv_row& row : rows) {
            CAPTURE(row.group);
            CHECK(possible_ids(row.group) == lbv_pouches);
            failure_log log;
            bool chest_seen = false;
            bool stacker_seen = false;
            int max_chest_seen = 0;
            int max_stacker_seen = 0;
            for (int i = 0; i < draws; ++i) {
                spawned_items list = spawn(row.group);
                const std::vector<std::string> ids = type_ids(list);
                const int chest = count_of(ids, "chestpouch");
                const int stacker = count_of(ids, "triple_stacker_pouch");
                // exactly one of the two, within its count
                log.expect((chest > 0) != (stacker > 0), "draw " + std::to_string(i));
                log.expect(chest <= row.max_chest && stacker <= row.max_stacker,
                           "draw " + std::to_string(i));
                log.expect(count_in(ids, lbv_pouches) == static_cast<int>(ids.size()),
                           "draw " + std::to_string(i));
                chest_seen = chest_seen || chest > 0;
                stacker_seen = stacker_seen || stacker > 0;
                max_chest_seen = std::max(max_chest_seen, chest);
                max_stacker_seen = std::max(max_stacker_seen, stacker);
            }
            CAPTURE(log.first);
            CHECK(log.count == 0);
            CHECK(chest_seen);
            CHECK(stacker_seen);
            CHECK(max_chest_seen == row.max_chest);
            CHECK(max_stacker_seen == row.max_stacker);
        }
    }
    SECTION("molle_backpacks_and_accessories gives a MOLLE pack or rucksack with pouches on it") {
        // CDF: DDA custom_lbvs.json:108-130: molle_pack 70, medium rucksack 15, large rucksack 15,
        // with molle_backpacks_contents (sustainment_pouch, camelbak, bag accessories,
        // deployment_bag)
        CHECK(possible_ids("molle_backpacks_and_accessories")
              == std::set<std::string>(
                  {"molle_pack", "molle_medium_rucksack", "molle_large_rucksack"}));
        std::set<std::string> contents = bag_pouches;
        contents.insert({"camelbak", "deployment_bag"});
        CHECK(possible_ids("molle_backpacks_contents") == contents);
        failure_log log;
        std::map<std::string, int> carriers;
        int carrying_pouches = 0;
        bool deployment_bag_seen = false;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("molle_backpacks_and_accessories");
            // the pouches are attached to the carrier, nothing is left on the ground
            log.expect(list.size() == 1,
                       "draw " + std::to_string(i) + " has " + std::to_string(list.size()));
            for (detached_ptr<item>& it : list) {
                carriers[it->typeId().str()]++;
                const std::vector<std::string> attached = attached_ids(*it);
                carrying_pouches += attached.empty() ? 0 : 1;
                deployment_bag_seen =
                    deployment_bag_seen || count_of(attached, "deployment_bag") > 0;
                log.expect(molle_size(*it) > 0
                               && it->contents.get_additional_space_used() <= molle_size(*it),
                           it->typeId().str() + " is overfull");
                log.expect(count_in(attached, contents) == static_cast<int>(attached.size()),
                           it->typeId().str() + " carries an unexpected pouch");
            }
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
        CHECK(carriers.size() == 3);
        CHECK(carriers["molle_pack"] / static_cast<double>(draws) == Approx(0.70).margin(0.07));
        CHECK(carriers["molle_medium_rucksack"] / static_cast<double>(draws)
              == Approx(0.15).margin(0.07));
        CHECK(carriers["molle_large_rucksack"] / static_cast<double>(draws)
              == Approx(0.15).margin(0.07));
        CHECK(carrying_pouches > draws / 2);
        CHECK(deployment_bag_seen);
    }
    SECTION("loaded_webbing_belt is a webbing belt with pouches on it") {
        // CDF: DDA custom_lbvs.json:14, webbing_belt with contents-group molle_belt_contents
        failure_log log;
        int carrying_pouches = 0;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("loaded_webbing_belt");
            log.expect(list.size() == 1 && list.front()->typeId() == itype_id("webbing_belt"),
                       "draw " + std::to_string(i));
            for (detached_ptr<item>& it : list) {
                const std::vector<std::string> attached = attached_ids(*it);
                carrying_pouches += attached.empty() ? 0 : 1;
                log.expect(count_in(attached, bag_pouches) == static_cast<int>(attached.size()),
                           "unexpected pouch on the belt");
            }
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
        CHECK(carrying_pouches > draws / 2);
    }
}

TEST_CASE("grp1_loaded_lbv_pouches_sit_in_molle_slots", "[armor][grp1][spawn]") {
    clear_all_state();
    rng_set_engine_seed(20261104);
    constexpr int draws = 300;
    // CDF: DDA custom_lbvs.json:4, load_bearing_vest 60 (custom_medium_lbv), light 20, heavy 20;
    // attach_molle sizes 8, 4 and 14 (data/json/items/armor/cdf_molle_carriers.json)
    const std::map<std::string, int> molle_slots =
        {{"load_bearing_vest", 8}, {"light_load_bearing_vest", 4}, {"heavy_load_bearing_vest", 14}};
    failure_log log;
    std::map<std::string, int> vests;
    for (int i = 0; i < draws; ++i) {
        spawned_items list = spawn("loaded_lbv");
        // one vest, no loose pouch
        log.expect(list.size() == 1,
                   "draw " + std::to_string(i) + " has " + std::to_string(list.size())
                       + " top-level items");
        for (detached_ptr<item>& vest : list) {
            const std::string id = vest->typeId().str();
            const auto slots = molle_slots.find(id);
            log.expect(slots != molle_slots.end(), "unexpected " + id);
            if (slots == molle_slots.end()) { continue; }
            vests[id]++;
            log.expect(molle_size(*vest) == slots->second, id + " webbing size");
            const std::vector<std::string> attached = attached_ids(*vest);
            // every pouch is one of the vest's MOLLE pockets and the vest has the first pouch of
            // its group
            log.expect(count_in(attached, lbv_pouches) == static_cast<int>(attached.size()),
                       id + " pouch set");
            log.expect(
                count_of(attached, "chestpouch") + count_of(attached, "triple_stacker_pouch") > 0,
                id + " has no chest or stacker pouch attached (draw " + std::to_string(i) + ")");
            log.expect(vest->contents.get_additional_space_used() <= slots->second,
                       id + " overfull");
            for (const item* pouch : vest->contents.get_added_pockets()) {
                log.expect(pouch->can_attach_as_pocket(), pouch->typeId().str() + " is not PALS");
            }
        }
    }
    CAPTURE(log.first);
    CHECK(log.count == 0);
    CHECK(vests.size() == 3);
    CHECK(vests["load_bearing_vest"] / static_cast<double>(draws) == Approx(0.60).margin(0.10));
    CHECK(
        vests["light_load_bearing_vest"] / static_cast<double>(draws) == Approx(0.20).margin(0.10));
    CHECK(
        vests["heavy_load_bearing_vest"] / static_cast<double>(draws) == Approx(0.20).margin(0.10));
}

TEST_CASE("grp1_kit_groups_combine_plates_and_pouches", "[armor][grp1][spawn]") {
    clear_all_state();
    rng_set_engine_seed(20261105);
    constexpr int draws = 300;

    // the kit groups are collections of plate groups and a custom_*_lbv group: every item of a
    // spawn is a plate or a pouch, none is a vest
    std::set<std::string> plate_kinds = large_plates;
    plate_kinds.insert(medium_plates.begin(), medium_plates.end());
    plate_kinds.insert(iotv_plates.begin(), iotv_plates.end());

    struct kit_row {
        const char* group;
        int large;     // plates for a large slot
        int medium;    // plates for a medium slot
        bool iotv;     // the three IOTV protectors
        bool pristine; // no destroyed plate
    };
    // CDF: DDA military.json:111-153; some_esapi_plates and the front plate groups give 2 large
    // (and 2 medium)
    const std::vector<kit_row> rows = {
        {"kitted_heavy_vest", 2, 2, true, true},
        {"used_kitted_heavy_vest", 2, 2, true, false},
        {"used_kitted_medium_vest", 2, 2, false, false},
        {"kitted_medium_vest", 2, 2, false, true},
        {"kitted_light_vest", 2, 0, false, true},
        {"used_kitted_light_vest", 2, 0, false, false},
    };
    for (const kit_row& row : rows) {
        CAPTURE(row.group);
        failure_log log;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn(row.group);
            const std::vector<std::string> ids = type_ids(list);
            const std::string where = "draw " + std::to_string(i);
            log.expect(count_in(ids, large_plates) == row.large, where + " large plates");
            log.expect(count_in(ids, medium_plates) == row.medium, where + " medium plates");
            log.expect(count_in(ids, iotv_plates) == (row.iotv ? 3 : 0), where + " protectors");
            if (row.iotv) {
                for (const std::string& plate : iotv_plates) {
                    log.expect(count_of(ids, plate) == 1, where + " " + plate);
                }
            }
            if (row.pristine) {
                log.expect(count_of(ids, "destroyed_large_ceramic_plate") == 0
                               && count_of(ids, "destroyed_medium_ceramic_plate") == 0,
                           where + " destroyed plate");
            }
            // the rest is the pouches of the custom_*_lbv group, and it always has a first pouch
            log.expect(count_of(ids, "chestpouch") + count_of(ids, "triple_stacker_pouch") > 0,
                       where + " has no chest or stacker pouch");
            for (detached_ptr<item>& it : list) {
                const std::string id = it->typeId().str();
                if (plate_kinds.count(id) == 0) {
                    log.expect(lbv_pouches.count(id) > 0,
                               where + " " + id + " is neither a plate nor a pouch");
                } else if (iotv_plates.count(id) > 0 && !row.pristine) {
                    log.expect(it->damage() >= 0 && it->damage() <= max_draw_damage,
                               where + " protector damage");
                } else if (iotv_plates.count(id) > 0) {
                    log.expect(it->damage() == 0, where + " pristine protector is damaged");
                }
            }
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
    }

    SECTION("the pristine kits carry exactly the pristine plate counts") {
        // CDF: DDA military.json:90 and :97, pristine_esapi_plates and pristine_esapi_front_plates
        failure_log log;
        for (int i = 0; i < draws; ++i) {
            spawned_items medium = spawn("kitted_medium_vest");
            spawned_items light = spawn("kitted_light_vest");
            spawned_items heavy = spawn("kitted_heavy_vest");
            const std::vector<std::string> medium_ids = type_ids(medium);
            const std::vector<std::string> light_ids = type_ids(light);
            const std::vector<std::string> heavy_ids = type_ids(heavy);
            log.expect(
                count_of(medium_ids, "esapi_plate") == 2 && count_of(medium_ids, "esbi_plate") == 2,
                "kitted_medium_vest draw " + std::to_string(i));
            log.expect(
                count_of(light_ids, "esapi_plate") == 2 && count_of(light_ids, "esbi_plate") == 0,
                "kitted_light_vest draw " + std::to_string(i));
            log.expect(
                count_of(heavy_ids, "esapi_plate") == 2 && count_of(heavy_ids, "esbi_plate") == 2,
                "kitted_heavy_vest draw " + std::to_string(i));
        }
        CAPTURE(log.first);
        CHECK(log.count == 0);
    }
    SECTION("the used kits show both working and destroyed plates") {
        std::set<std::string> seen;
        for (int i = 0; i < draws; ++i) {
            spawned_items list = spawn("used_kitted_medium_vest");
            const std::vector<std::string> ids = type_ids(list);
            seen.insert(ids.begin(), ids.end());
        }
        CHECK(seen.count("destroyed_large_ceramic_plate") == 1);
        CHECK(seen.count("destroyed_medium_ceramic_plate") == 1);
        CHECK(seen.count("esapi_plate") == 1);
        CHECK(seen.count("esbi_plate") == 1);
    }
}

namespace {
// type ids of the plates in the plate slots of a vest, sorted (pattern: plate_ids of
// tests/ablative_armor_test.cpp:54)
auto plate_ids(const item& vest) -> std::vector<std::string> {
    std::vector<std::string> ids;
    for (const item* plate : ablative::plates(vest)) { ids.push_back(plate->typeId().str()); }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// CDF: plate slots of the vests, data/json/items/armor/cdf_ballistic_vests.json: the ESAPI vest has
// two large and two medium slots, the heavy vest the same plus the three IOTV protector slots, the
// light vests and level_3_vest two large slots
struct slot_layout {
    int large;
    int medium;
    bool iotv;
};

auto layout_of(const std::string& id) -> const slot_layout* {
    static const std::map<std::string, slot_layout> layouts = {
        {"ballistic_vest_esapi", {2, 2, false}},
        {"ballistic_vest_heavy", {2, 2, true}},
        {"level_3_vest", {2, 0, false}},
        {"ballistic_vest_light", {2, 0, false}},
        {"ballistic_vest_light_operator", {2, 0, false}},
        {"ballistic_vest_light_pouches", {2, 0, false}},
    };
    const auto found = layouts.find(id);
    return found == layouts.end() ? nullptr : &found->second;
}

// every plate slot holds a plate of its kind: working only when the group is pristine
void check_plates(failure_log& log, const item& vest, bool pristine, const std::string& where) {
    const std::string id = vest.typeId().str();
    const slot_layout* layout = layout_of(id);
    if (layout == nullptr) { return; }
    const std::vector<std::string> plates = plate_ids(vest);
    log.expect(count_in(plates, large_plates) == layout->large, where + " " + id + " large plates");
    log.expect(count_in(plates, medium_plates) == layout->medium,
               where + " " + id + " medium plates");
    log.expect(count_in(plates, iotv_plates) == (layout->iotv ? 3 : 0),
               where + " " + id + " protectors");
    log.expect(
        static_cast<int>(plates.size()) == layout->large + layout->medium + (layout->iotv ? 3 : 0),
        where + " " + id + " plate count " + std::to_string(plates.size()));
    if (pristine) {
        log.expect(count_of(plates, "destroyed_large_ceramic_plate") == 0
                       && count_of(plates, "destroyed_medium_ceramic_plate") == 0,
                   where + " " + id + " destroyed plate");
    }
}

struct vest_group_row {
    const char* group;
    std::set<std::string> allowed; // top-level items a draw may hold
    int min_plated;                // vests with plates per draw
    int max_plated;
    bool pristine;
    bool with_pouches; // every plated vest has a pouch attached (the group uses a kitted_* group)
    std::set<std::string> must_see; // item ids that show up in the draws
};

const std::set<std::string> load_bearing_vests =
    {"load_bearing_vest", "light_load_bearing_vest", "heavy_load_bearing_vest"};
const std::set<std::string> light_vests =
    {"ballistic_vest_light", "ballistic_vest_light_operator", "ballistic_vest_light_pouches"};
} // namespace

TEST_CASE("grp1_vest_groups_resolve_and_carry_plates", "[armor][grp1][spawn]") {
    clear_all_state();
    rng_set_engine_seed(20261106);
    constexpr int draws = 500;

    std::set<std::string> military_allowed =
        {"ballistic_vest_esapi", "level_3_vest", "ballistic_vest_heavy"};
    military_allowed.insert(load_bearing_vests.begin(), load_bearing_vests.end());
    military_allowed.insert(light_vests.begin(), light_vests.end());
    std::set<std::string> military_seen =
        {"ballistic_vest_esapi", "level_3_vest", "ballistic_vest_heavy", "ballistic_vest_light",
         "load_bearing_vest"};
    // CDF: DDA military.json:157-390; plated vests keep the vest's own pouches only through the kit
    // groups
    const std::vector<vest_group_row> rows = {
        {"military_ballistic_vest", military_allowed, 1, 1, false, false, military_seen},
        {"swat_ballistic_vest",
         {"ballistic_vest_esapi", "tacvest", "heavy_load_bearing_vest_breacher"},
         1,
         1,
         false,
         false,
         {"ballistic_vest_esapi", "tacvest", "heavy_load_bearing_vest_breacher"}},
        {"swat_ballistic_vest_pristine",
         {"ballistic_vest_esapi"},
         1,
         1,
         true,
         true,
         {"ballistic_vest_esapi"}},
        {"military_ballistic_vest_pristine", military_allowed, 0, 4, true, false, military_seen},
        {"us_ballistic_vest_pristine",
         {"ballistic_vest_esapi"},
         1,
         1,
         true,
         true,
         {"ballistic_vest_esapi"}},
        {"contractor_ballistic_vest_light", light_vests, 1, 1, true, true, light_vests},
        {"military_ballistic_vest_light", light_vests, 1, 1, false, true, light_vests},
        {"military_ballistic_vest_light_pristine", light_vests, 1, 1, true, true, light_vests},
        {"ballistic_vest_light_pristine",
         {"ballistic_vest_light"},
         1,
         1,
         true,
         true,
         {"ballistic_vest_light"}},
        {"military_ballistic_vest_heavy_pristine",
         {"ballistic_vest_heavy"},
         1,
         1,
         true,
         true,
         {"ballistic_vest_heavy"}},
        {"xedra_ballistic_vest_heavy_pristine",
         {"ballistic_vest_heavy"},
         1,
         1,
         true,
         true,
         {"ballistic_vest_heavy"}},
        {"military_ballistic_vest_heavy",
         {"ballistic_vest_heavy"},
         1,
         1,
         false,
         true,
         {"ballistic_vest_heavy"}},
    };
    REQUIRE(rows.size() == 12);

    for (const vest_group_row& row : rows) {
        CAPTURE(row.group);
        CHECK(item_group::group_is_defined(item_group_id(row.group)));
        failure_log log;
        std::set<std::string> seen;
        std::string debug_output;
        debug_output = capture_debugmsg_during([&]() {
            for (int i = 0; i < draws; ++i) {
                spawned_items list = spawn(row.group);
                const std::string where = "draw " + std::to_string(i);
                int plated = 0;
                for (detached_ptr<item>& it : list) {
                    const std::string id = it->typeId().str();
                    log.expect(it->typeId().is_valid(), where + " unknown id " + id);
                    log.expect(row.allowed.count(id) > 0, where + " unexpected " + id);
                    seen.insert(id);
                    if (ablative::has_plate_slots(*it)) {
                        ++plated;
                        check_plates(log, *it, row.pristine, where);
                        if (row.with_pouches) {
                            log.expect(!it->contents.get_added_pockets().empty(),
                                       where + " " + id + " has no pouch");
                        }
                    }
                }
                log.expect(plated >= row.min_plated && plated <= row.max_plated,
                           where + " has " + std::to_string(plated) + " plated vests");
            }
        });
        CAPTURE(log.first);
        CAPTURE(debug_output);
        CHECK(debug_output.empty());
        CHECK(log.count == 0);
        for (const std::string& id : row.must_see) {
            CAPTURE(id);
            CHECK(seen.count(id) == 1);
        }
    }
}

TEST_CASE("grp1_every_group_of_the_closure_exists", "[armor][grp1][spawn]") {
    clear_all_state();
    // CDF: the 40 groups of DDA military.json:33-390, custom_lbvs.json:4-130, gear.json:446-470
    const std::vector<std::string> plates =
        {"civilian_ballistic_inserts",  "civilian_face_armor",
         "maybe_esapi_plate",           "maybe_esbi_plate",
         "some_esapi_plates",           "some_esapi_front_plates",
         "some_steel_front_plates",     "some_heavy_steel_front_plates",
         "some_stab_front_plates",      "pristine_esapi_plates",
         "pristine_esapi_front_plates", "ballistic_protectors"};
    const std::vector<std::string> molle = {
        "loaded_lbv",
        "loaded_webbing_belt",
        "molle_vest_bag_accessories",
        "molle_accessories",
        "molle_belt_contents",
        "custom_light_lbv",
        "custom_medium_lbv",
        "custom_heavy_lbv",
        "molle_backpacks_and_accessories",
        "molle_backpacks_contents"};
    const std::vector<std::string> kits =
        {"kitted_heavy_vest",  "used_kitted_heavy_vest", "used_kitted_medium_vest",
         "kitted_medium_vest", "kitted_light_vest",      "used_kitted_light_vest"};
    const std::vector<std::string> vests = {
        "military_ballistic_vest",
        "swat_ballistic_vest",
        "swat_ballistic_vest_pristine",
        "military_ballistic_vest_pristine",
        "us_ballistic_vest_pristine",
        "contractor_ballistic_vest_light",
        "military_ballistic_vest_light",
        "military_ballistic_vest_light_pristine",
        "ballistic_vest_light_pristine",
        "military_ballistic_vest_heavy_pristine",
        "xedra_ballistic_vest_heavy_pristine",
        "military_ballistic_vest_heavy"};
    REQUIRE(plates.size() + molle.size() + kits.size() + vests.size() == 40);

    const auto check_closure = [](const std::vector<std::string>& groups) {
        for (const std::string& group : groups) {
            CAPTURE(group);
            REQUIRE(item_group::group_is_defined(item_group_id(group)));
            // a group that exists gives at least one item type, and every type it names is defined
            const std::set<const itype*> types = item_group::every_possible_item_from(
                item_group_id(group));
            CHECK_FALSE(types.empty());
            for (const itype* type : types) {
                CAPTURE(type->get_id().str());
                CHECK(type->get_id().is_valid());
            }
            // and it spawns something without a debugmsg
            const std::string msg = capture_debugmsg_during([&group]() {
                bool any = false;
                for (int i = 0; i < 50; ++i) {
                    spawned_items list = spawn(group);
                    any = any || !list.empty();
                }
                CHECK(any);
            });
            CHECK(msg.empty());
        }
    };
    SECTION("plate groups") { check_closure(plates); }
    SECTION("MOLLE and load-bearing vest groups") { check_closure(molle); }
    SECTION("kit groups") { check_closure(kits); }
    SECTION("vest groups") { check_closure(vests); }
}
