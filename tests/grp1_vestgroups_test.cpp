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

