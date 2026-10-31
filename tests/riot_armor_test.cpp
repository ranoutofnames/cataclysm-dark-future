#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "iuse_actor.h"
#include "material.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "type_id.h"
#include "units.h"

#include <string>

// RIOT1. DDA riot chest, arm and leg guards and the riot armor suit (torso_armor.json:2437,
// arms_armor.json:1691, legs_armor.json:2040, suits_protection.json:1331 at b4b62254ae2) as CDF data.
// Literals below are re-derived from data/json/items/armor/cdf_riot_armor.json (the CDF copy).

namespace {

auto spawn_riot(const char *id) -> detached_ptr<item> {
    REQUIRE(itype_id(id).is_valid());
    return item::spawn(itype_id(id));
}

auto cov(const item &it, const char *bp, const char *sub) -> int {
    return it.get_coverage(bodypart_id(bp), sub_bodypart_str_id(sub));
}

auto molle_size(const item &it) -> int {
    const use_function *use = it.get_use("attach_molle");
    if (use == nullptr) {
        return 0;
    }
    const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>(use->get_actor_ptr());
    return actor != nullptr ? actor->size : 0;
}


} // namespace

TEST_CASE("riot1_material_and_items_exist", "[armor][riot1]") {
    clear_all_state();
    // CDF: plastic_pad is copy-from plastic (data/json/materials.json), DDA materials.json:1731
    REQUIRE(material_id("plastic_pad").is_valid());
    for (const char *id : { "armor_riot_torso", "armor_riot_arm", "armor_riot_leg" }) {
        CAPTURE(id);
        detached_ptr<item> it = spawn_riot(id);
        CHECK(it->find_armor_data() != nullptr);
        CHECK(it->made_of(material_id("plastic_pad")));
        CHECK(it->made_of(material_id("thermo_resin")));
    }
}

TEST_CASE("riot1_torso_coverage_and_molle", "[armor][riot1]") {
    clear_all_state();
    Character &guy = get_player_character();
    detached_ptr<item> torso = spawn_riot("armor_riot_torso");
    // CDF: torso 75 / encumbrance 6; arms and legs 65 on the upper parts; hips 30 (cdf_riot_armor.json)
    CHECK(torso->get_coverage(bodypart_id("torso")) == 75);
    CHECK(torso->get_encumber(guy, bodypart_id("torso")) == 6);
    CHECK(cov(*torso, "arm_l", "arm_shoulder_l") == 65);
    CHECK(cov(*torso, "arm_r", "arm_upper_r") == 65);
    CHECK(cov(*torso, "leg_l", "leg_upper_l") == 65);
    CHECK(cov(*torso, "leg_r", "leg_hip_r") == 30);
    CHECK(cov(*torso, "arm_l", "arm_lower_l") == 0);
    // CDF: attach_molle size 6 and detach_molle
    CHECK(molle_size(*torso) == 6);
    CHECK(torso->get_use("detach_molle") != nullptr);
}

TEST_CASE("riot1_arm_and_leg_guards", "[armor][riot1]") {
    clear_all_state();
    Character &guy = get_player_character();
    detached_ptr<item> arms = spawn_riot("armor_riot_arm");
    // CDF: lower arm and elbow 90, encumbrance 3 (cdf_riot_armor.json)
    CHECK(cov(*arms, "arm_l", "arm_lower_l") == 90);
    CHECK(cov(*arms, "arm_r", "arm_elbow_r") == 90);
    CHECK(cov(*arms, "arm_l", "arm_upper_l") == 0);
    CHECK(arms->get_encumber(guy, bodypart_id("arm_l")) == 3);
    detached_ptr<item> legs = spawn_riot("armor_riot_leg");
    CHECK(cov(*legs, "leg_l", "leg_knee_l") == 90);
    CHECK(cov(*legs, "leg_r", "leg_lower_r") == 90);
    CHECK(cov(*legs, "leg_l", "leg_upper_l") == 0);
    CHECK(legs->get_encumber(guy, bodypart_id("leg_l")) == 3);
    // CDF: whole foot at 75, encumbrance 2 (DDA foot_arch/foot_ankle sub-locations do not exist)
    CHECK(legs->get_coverage(bodypart_id("foot_l")) == 75);
    CHECK(legs->get_coverage(bodypart_id("foot_r")) == 75);
    CHECK(legs->get_encumber(guy, bodypart_id("foot_l")) == 2);
}

TEST_CASE("riot1_flags_and_size", "[armor][riot1]") {
    clear_all_state();
    for (const char *id : { "armor_riot_torso", "armor_riot_arm", "armor_riot_leg" }) {
        CAPTURE(id);
        detached_ptr<item> it = spawn_riot(id);
        for (const char *flag : { "STURDY", "OUTER", "WATER_FRIENDLY", "PADDED" }) {
            CAPTURE(flag);
            CHECK(it->has_flag(flag_id(flag)));
        }
    }
    detached_ptr<item> torso = spawn_riot("armor_riot_torso");
    detached_ptr<item> arms = spawn_riot("armor_riot_arm");
    detached_ptr<item> legs = spawn_riot("armor_riot_leg");
    CHECK_FALSE(torso->has_flag(flag_id("BLOCK_WHILE_WORN")));
    CHECK(arms->has_flag(flag_id("BLOCK_WHILE_WORN")));
    CHECK(legs->has_flag(flag_id("BLOCK_WHILE_WORN")));
    // CDF: weight, volume, longest side from cdf_riot_armor.json (DDA numbers)
    CHECK(torso->weight() == 2000_gram);
    CHECK(arms->weight() == 500_gram);
    CHECK(legs->weight() == 500_gram);
    CHECK(torso->base_volume() == 10000_ml);
    CHECK(arms->base_volume() == 2500_ml);
    CHECK(legs->base_volume() == 2500_ml);
    CHECK(torso->length() == 60_cm);
    CHECK(arms->length() == 40_cm);
    CHECK(legs->length() == 40_cm);
}

TEST_CASE("riot1_pieces_stack_without_overlap", "[armor][riot1]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    guy.wear_item(spawn_riot("armor_riot_torso"), false);
    guy.wear_item(spawn_riot("armor_riot_arm"), false);
    guy.wear_item(spawn_riot("armor_riot_leg"), false);
    CHECK(guy.is_wearing(itype_id("armor_riot_torso")));
    CHECK(guy.is_wearing(itype_id("armor_riot_arm")));
    CHECK(guy.is_wearing(itype_id("armor_riot_leg")));
}

TEST_CASE("riot1_suit_is_the_dda_suit", "[armor][riot1]") {
    clear_all_state();
    Character &guy = get_player_character();
    detached_ptr<item> suit = spawn_riot("armor_riot");
    // CDF: 3 kg, 12500 ml (was 4000 g and 7500 ml), MOLLE 6, longest side 60 cm
    CHECK(suit->weight() == 3000_gram);
    CHECK(suit->base_volume() == 12500_ml);
    CHECK(molle_size(*suit) == 6);
    CHECK(suit->length() == 60_cm);
    CHECK(suit->get_coverage(bodypart_id("torso")) == 75);
    CHECK(suit->get_encumber(guy, bodypart_id("torso")) == 6);
    CHECK(cov(*suit, "arm_l", "arm_lower_l") == 90);
    CHECK(cov(*suit, "leg_r", "leg_knee_r") == 90);
    CHECK(cov(*suit, "arm_l", "arm_upper_l") == 65);
    CHECK(cov(*suit, "leg_l", "leg_hip_l") == 30);
    CHECK(suit->get_coverage(bodypart_id("foot_l")) == 75);
    CHECK(suit->made_of(material_id("plastic_pad")));
    CHECK(suit->made_of(material_id("thermo_resin")));
    CHECK(suit->has_flag(flag_id("BLOCK_WHILE_WORN")));
}
