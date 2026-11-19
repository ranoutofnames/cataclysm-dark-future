#include "ablative_coverage.h"
#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "profession.h"
#include "recipe_dictionary.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "type_id.h"
#include "itype_slots/islot_armor.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// P3-VEST2: the Old Guard IOTV suit as its own item (data/json/items/armor/cdf_oldguard_armor.json),
// the retired BN Old Guard ids and BN loose plates migrating into the DDA plate system
// (data/json/obsoletion/cdf_vest_migration.json) and the plate-only DDA vests in the starting kits.

namespace {

// type ids of the plates (and protectors) in the plate slots of a vest, sorted
auto plate_ids(const item& vest) -> std::vector<std::string> {
    std::vector<std::string> ids;
    for (const item* plate : ablative::plates(vest)) {
        ids.push_back(plate->typeId().str());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

auto count_of(const std::vector<std::string>& ids, const std::string& id) -> int {
    return static_cast<int>(std::count(ids.begin(), ids.end(), id));
}

auto put_in(item& carrier, const std::string& id) -> bool {
    return carrier.try_put_in(item::spawn(itype_id(id))).success();
}

// what each retired Old Guard id comes out as: ids of the plates the migration puts inside, besides
// the two protectors every one of them has
struct oldguard_row {
    const char* retired;
    const char* large;
    const char* medium;
};

const std::vector<oldguard_row> oldguard_rows = {
    {"oldguard_armor", nullptr, nullptr},
    {"oldguard_armor_ceramic", "esapi_plate", "esbi_plate"},
    {"oldguard_armor_hardsteel", "hc_esapi_plate", "hc_esbi_plate"},
    {"oldguard_armor_steel", "mc_esapi_plate", "mc_esbi_plate"},
    {"oldguard_armor_superalloy", "qt_esapi_plate", "qt_esbi_plate"},
};

} // namespace

TEST_CASE("oldguard_suit_has_the_plate_and_protector_slots_of_the_heavy_vest",
          "[armor][ablative][oldguard]") {
    clear_all_state();
    REQUIRE(itype_id("ballistic_vest_oldguard").is_valid());
    detached_ptr<item> vest = item::spawn(itype_id("ballistic_vest_oldguard"));
    REQUIRE(vest->typeId() == itype_id("ballistic_vest_oldguard"));
    CHECK(vest->type->nname(1) == "Old Guard IOTV suit");
    // the item itself is empty: the guards come with the migration and the kit groups
    CHECK(ablative::plates(*vest).empty());
    CHECK(ablative::has_plate_slots(*vest));

    SECTION("2 large and 2 medium plate slots") {
        CHECK(put_in(*vest, "esapi_plate"));
        CHECK(put_in(*vest, "esapi_plate"));
        CHECK_FALSE(put_in(*vest, "esapi_plate"));
        CHECK(put_in(*vest, "esbi_plate"));
        CHECK(put_in(*vest, "esbi_plate"));
        CHECK_FALSE(put_in(*vest, "esbi_plate"));
        CHECK(ablative::plates(*vest).size() == 4);
    }
    SECTION("one shoulder, one groin and one collar slot") {
        for (const char* id : {"iotv_shoulder_plate", "iotv_groin_plate", "iotv_neck_plate"}) {
            CAPTURE(id);
            CHECK(put_in(*vest, id));
            CHECK_FALSE(put_in(*vest, id));
        }
        CHECK(ablative::plates(*vest).size() == 3);
    }
    SECTION("all seven slots at once, 2 + 2 + 3") {
        for (const char* id : {"esapi_plate", "esapi_plate", "esbi_plate", "esbi_plate",
                               "iotv_shoulder_plate", "iotv_groin_plate", "iotv_neck_plate"}) {
            CAPTURE(id);
            CHECK(put_in(*vest, id));
        }
        CHECK(ablative::plates(*vest).size() == 7);
    }
    SECTION("same weight and volume as the DDA heavy vest") {
        detached_ptr<item> heavy = item::spawn(itype_id("ballistic_vest_heavy"));
        CHECK(vest->weight() == heavy->weight());
        CHECK(vest->volume() == heavy->volume());
    }
}

TEST_CASE("retired_oldguard_ids_come_out_as_the_oldguard_suit_with_groin_and_shoulder_guards",
          "[armor][ablative][oldguard]") {
    clear_all_state();
    for (const oldguard_row& row : oldguard_rows) {
        CAPTURE(row.retired);
        const itype_id retired(row.retired);
        CHECK(item_controller->migrate_id(retired) == itype_id("ballistic_vest_oldguard"));
        detached_ptr<item> spawned;
        const std::string dmsg =
            capture_debugmsg_during([&]() { spawned = item_controller->spawn_migrated(retired, calendar::turn); });
        CHECK(dmsg.empty());
        REQUIRE(spawned);
        const item& vest = *spawned;
        CHECK(vest.typeId() == itype_id("ballistic_vest_oldguard"));
        const std::vector<std::string> plates = plate_ids(vest);
        // the groin and shoulder protectors, but not the collar
        CHECK(count_of(plates, "iotv_groin_plate") == 1);
        CHECK(count_of(plates, "iotv_shoulder_plate") == 1);
        CHECK(count_of(plates, "iotv_neck_plate") == 0);
        // the pouches of the old suit are attached
        CHECK(vest.contents.has_additional_pockets());
        if (row.large != nullptr) {
            CHECK(count_of(plates, row.large) == 2);
            CHECK(count_of(plates, row.medium) == 2);
            CHECK(plates.size() == 6);
        } else {
            CHECK(plates.size() == 2);
        }
    }
}

TEST_CASE("oldguard_item_groups_spawn_the_suit_with_groin_and_shoulder_guards",
          "[armor][ablative][oldguard]") {
    clear_all_state();
    for (int i = 0; i < 25; ++i) {
        for (const bool pristine : {true, false}) {
            const std::string group = pristine ? "oldguard_ballistic_vest_pristine" : "oldguard_ballistic_vest";
            CAPTURE(group);
            std::vector<detached_ptr<item>> spawned;
            const std::string dmsg =
                capture_debugmsg_during([&]() { spawned = item_group::items_from(item_group_id(group)); });
            CHECK(dmsg.empty());
            REQUIRE(spawned.size() == 1);
            const item& vest = *spawned.front();
            CHECK(vest.typeId() == itype_id("ballistic_vest_oldguard"));
            const std::vector<std::string> plates = plate_ids(vest);
            CHECK(count_of(plates, "iotv_groin_plate") == 1);
            CHECK(count_of(plates, "iotv_shoulder_plate") == 1);
            CHECK(count_of(plates, "iotv_neck_plate") == 0);
            if (pristine) {
                CHECK(count_of(plates, "esapi_plate") == 2);
                CHECK(count_of(plates, "esbi_plate") == 2);
            }
        }
    }
}

TEST_CASE("oldguard_recipe_makes_the_oldguard_suit_from_a_heavy_vest",
          "[armor][ablative][oldguard]") {
    clear_all_state();
    CHECK(recipe_id("ballistic_vest_oldguard").is_valid());
    // the heavy vest is the DDA item: not crafted from itself any more
    CHECK_FALSE(recipe_id("ballistic_vest_heavy").is_valid());
}

TEST_CASE("retired_bn_plates_come_out_as_esapi_plates_that_the_vest_takes",
          "[armor][ablative][oldguard]") {
    clear_all_state();
    const std::map<std::string, std::string> retired_to_plate = {
        {"ceramic_ballistic_plate", "esapi_plate"},
        {"hard_ballistic_plate", "hc_esapi_plate"},
        {"super_ballistic_plate", "qt_esapi_plate"},
        {"scrap_ballistic_plate", "scrap_esapi_plate"},
    };
    for (const auto& [retired, plate_id] : retired_to_plate) {
        CAPTURE(retired, plate_id);
        CHECK(item_controller->migrate_id(itype_id(retired)) == itype_id(plate_id));
        detached_ptr<item> plate;
        const std::string dmsg =
            capture_debugmsg_during([&]() { plate = item_controller->spawn_migrated(itype_id(retired), calendar::turn); });
        CHECK(dmsg.empty());
        REQUIRE(plate);
        CHECK(plate->typeId() == itype_id(plate_id));
        CHECK(plate->has_flag(flag_id("ABLATIVE_LARGE")));

        // the plate fits a slot of each DDA vest, which the BN plate did not
        for (const char* vest_id : {"ballistic_vest_esapi", "ballistic_vest_heavy", "ballistic_vest_oldguard"}) {
            CAPTURE(vest_id);
            detached_ptr<item> vest = item::spawn(itype_id(vest_id));
            const ret_val<bool> res = vest->try_put_in(item::spawn(itype_id(plate_id)));
            CHECK(res.success());
            CHECK(plate_ids(*vest) == std::vector<std::string>{plate_id});
        }
    }
    // the plate of the old kit is the one the vest takes, spawned through the migration
    detached_ptr<item> vest = item::spawn(itype_id("ballistic_vest_esapi"));
    detached_ptr<item> plate = item_controller->spawn_migrated(itype_id("ceramic_ballistic_plate"), calendar::turn);
    REQUIRE(plate);
    CHECK(vest->try_put_in(std::move(plate)).success());
    CHECK(plate_ids(*vest) == std::vector<std::string>{"esapi_plate"});
}

TEST_CASE("starting_kits_carry_plates_inside_the_vest_and_magazines_as_separate_items",
          "[armor][ablative][oldguard]") {
    clear_all_state();
    // every starting kit of the profession list that has a plate vest
    const std::vector<std::string> professions = {
        "soldier", "mil_auto_rifleman", "mil_grenadier", "mil_marksman", "prof_combat_engineer",
        "prof_combat_medic", "specops", "bio_soldier", "bio_sniper", "bio_specops", "winter_army",
        "prof_feral_soldier", "mil_drone_operator", "mall_ninja",
    };
    for (const std::string& prof_id : professions) {
        CAPTURE(prof_id);
        const string_id<profession> prof(prof_id);
        REQUIRE(prof.is_valid());
        std::vector<detached_ptr<item>> items;
        const std::string dmsg = capture_debugmsg_during([&]() { items = prof->items(true, {}); });
        // a magazine that fits no pocket of the vest used to be reported and lost here
        CHECK(dmsg.empty());
        int vests = 0;
        for (const detached_ptr<item>& it : items) {
            CAPTURE(it->typeId().str());
            // a loose plate cannot be worn: the new-character code would flag it
            CHECK_FALSE(it->has_flag(flag_id("CANT_WEAR")));
            const std::string id = it->typeId().str();
            if (id.rfind("ballistic_vest_", 0) == 0) {
                ++vests;
                const std::vector<std::string> plates = plate_ids(*it);
                CHECK_FALSE(plates.empty());
                for (const std::string& plate : plates) {
                    CHECK(plate != "ceramic_ballistic_plate");
                }
            }
        }
        CHECK(vests >= 1);
    }

    SECTION("the soldier has a full plate set and the magazines next to the vest") {
        const std::vector<detached_ptr<item>> items = string_id<profession>("soldier")->items(true, {});
        int magazines = 0;
        for (const detached_ptr<item>& it : items) {
            if (it->typeId() == itype_id("ballistic_vest_esapi")) {
                const std::vector<std::string> plates = plate_ids(*it);
                CHECK(count_of(plates, "esapi_plate") == 2);
                CHECK(count_of(plates, "esbi_plate") == 2);
            } else if (it->typeId() == itype_id("stanag30")) {
                ++magazines;
            }
        }
        CHECK(magazines == 2);
    }
}

TEST_CASE("plate_vests_keep_the_dda_vitals_pouch_and_protector_coverage",
          "[armor][ablative][oldguard]") {
    clear_all_state();
    const bodypart_str_id torso("torso");
    for (const char* id : {"ballistic_vest_esapi", "ballistic_vest_heavy", "ballistic_vest_oldguard"}) {
        CAPTURE(id);
        detached_ptr<item> vest = item::spawn(itype_id(id));
        const bodypart_id torso_id("torso");
        for (const char* sub : {"torso_upper", "torso_lower"}) {
            CAPTURE(sub);
            CHECK(vest->get_coverage(torso_id, sub_bodypart_str_id(sub), item::cover_type::COVER_VITALS) == 90);
        }
        // DDA ballistic_armor.json:91-103: the pouches attached to the vest weigh on the body at 0.3
        REQUIRE(static_cast<bool>(vest->type->armor));
        bool found = false;
        for (const armor_portion_data& portion : vest->type->armor->data) {
            if (portion.covers.test(torso)) {
                found = true;
                CHECK(portion.volume_encumber_modifier == Approx(0.3f));
            }
        }
        CHECK(found);
    }
    // DDA ballistic_armor.json:1456-1546
    detached_ptr<item> shoulder = item::spawn(itype_id("iotv_shoulder_plate"));
    const bodypart_id arm_l("arm_l");
    CHECK(shoulder->get_coverage(arm_l, sub_bodypart_str_id("arm_shoulder_l"), item::cover_type::COVER_RANGED) == 95);
    CHECK(shoulder->get_coverage(arm_l, sub_bodypart_str_id("arm_shoulder_l"), item::cover_type::COVER_VITALS) == 100);
    CHECK(shoulder->get_coverage(arm_l, sub_bodypart_str_id("arm_shoulder_l")) == 80);
    detached_ptr<item> groin = item::spawn(itype_id("iotv_groin_plate"));
    const bodypart_id leg_l("leg_l");
    CHECK(groin->get_coverage(leg_l, sub_bodypart_str_id("leg_hip_l"), item::cover_type::COVER_VITALS) == 100);
    CHECK(groin->get_coverage(leg_l, sub_bodypart_str_id("leg_hip_l")) == 75);
}
