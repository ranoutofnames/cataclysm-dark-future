#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

#include <string>
#include <vector>

// PAL1 (PL-35-b, step 0 and the gate): pins the BN power armor layering rules
// (character.cpp can_wear power armor block, is_wearing_power_armor) and the energy branch
// (character.cpp has_enough_charges). They pass on the base; later DDA ports must keep them green.

namespace {

auto spawn_pa(const char *id) -> detached_ptr<item> {
    REQUIRE(itype_id(id).is_valid());
    return item::spawn(itype_id(id));
}

auto fresh_guy() -> Character & {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    return guy;
}

} // namespace

// PAL-t0: the gate. Mutation (data-only, no rebuild): delete POWERARMOR_EXO from power_armor_basic
// in data/json/obsoletion/items.json:4486 -> the basic cases go red.
TEST_CASE("pal_t0_obsoletion_suits_keep_exo_ups_flags", "[power_armor][pal1]") {
    const std::vector<std::string> ids = {
        "power_armor_basic", "power_armor_basic_on",
        "power_armor_heavy", "power_armor_heavy_on",
        "power_armor_light", "power_armor_light_on",
    };
    for (const std::string &id : ids) {
        CAPTURE(id);
        REQUIRE(itype_id(id).is_valid());
        const detached_ptr<item> it = spawn_pa(id.c_str());
        CHECK(it->has_flag(flag_id("POWERARMOR_EXO")));
        CHECK(it->has_flag(flag_id("USE_UPS")));
        CHECK(it->has_flag(flag_id("NAT_UPS")));
    }
}

// PAL-t1: an external over an exoskeleton on overlapping parts is refused.
// Mutation (C++): drop the POWERARMOR_EXO check at character.cpp:3704.
TEST_CASE("pal_t1_external_over_exoskeleton_refused", "[power_armor][pal1]") {
    Character &guy = fresh_guy();
    guy.wear_item(spawn_pa("power_armor_exo_1"), false);
    REQUIRE(guy.is_wearing(itype_id("power_armor_exo_1")));
    REQUIRE(guy.is_wearing_power_armor());

    // depowered_armor covers the exo's parts; flag it as an external at runtime (not PLATING)
    detached_ptr<item> ext = spawn_pa("depowered_armor");
    ext->set_flag(flag_id("POWERARMOR_EXTERNAL"));
    const ret_val<bool> res = guy.can_wear(*ext);
    CHECK_FALSE(res.success());
    CHECK(res.str().find("externals over an exoskeleton") != std::string::npos);

    // a helmet (external, other parts) is accepted over the same exoskeleton
    const detached_ptr<item> helm = spawn_pa("power_armor_helmet_basic");
    CHECK(guy.can_wear(*helm).success());
}

// PAL-t2: two exoskeletons are refused.
// Mutation (C++): remove the EXO && EXO branch at character.cpp:3694.
TEST_CASE("pal_t2_two_exoskeletons_refused", "[power_armor][pal1]") {
    Character &guy = fresh_guy();
    guy.wear_item(spawn_pa("power_armor_exo_1"), false);
    REQUIRE(guy.is_wearing_power_armor());
    const detached_ptr<item> second = spawn_pa("power_armor_exo_2");
    const ret_val<bool> res = guy.can_wear(*second);
    CHECK_FALSE(res.success());
    CHECK(res.str().find("multiple exoskeletons") != std::string::npos);
}

// PAL-t3: energy branch of has_enough_charges for power armor: a powered armor-interface bionic
// with power, or a UPS with USE_UPS, or nothing.
// Mutation (C++): skip the USE_UPS branch at character.cpp:9177.
// CDF: no power armor in the data has charges_per_use (as in DDA, they use power_draw), so ammo_required() is 0 and
// has_enough_charges returns true before the energy branch; the branch is pinned on a TEST_DATA suit that spends 25 charges.
TEST_CASE("pal_t3_energy_branch_bionic_ups_neither", "[power_armor][pal1]") {
    Character &guy = fresh_guy();
    detached_ptr<item> suit = spawn_pa("pal_test_powered_armor");
    REQUIRE(suit->ammo_required() > 0);
    REQUIRE(suit->has_flag(flag_id("USE_UPS")));
    guy.set_power_level(0_kJ);

    SECTION("neither") {
        CHECK_FALSE(guy.has_enough_charges(*suit, false));
    }
    SECTION("UPS with charges, USE_UPS") {
        guy.i_add(item::spawn(itype_id("UPS_off"), calendar::start_of_cataclysm, 500));
        CHECK(guy.has_enough_charges(*suit, false));
    }
    SECTION("powered bio_power_armor_interface with power") {
        guy.add_bionic(bionic_id("bio_power_storage"));
        guy.add_bionic(bionic_id("bio_power_armor_interface"));
        for (bionic &b : guy.get_bionic_collection()) {
            if (b.id == bionic_id("bio_power_armor_interface")) {
                b.powered = true;
            }
        }
        guy.set_power_level(guy.get_max_power_level());
        CHECK(guy.has_enough_charges(*suit, false));
    }
}

// PAL-t4: a power armor component without an exoskeleton is refused by BN rules; with the
// exoskeleton the same component is accepted. Mutation (C++): apply no is_wearing_power_armor
// requirement (character.cpp:3698).
// [UNSURE] base has no DDA-ported is_power_armor id; this pins the is_power_armor component rule only.
TEST_CASE("pal_t4_power_armor_component_needs_exoskeleton", "[power_armor][pal1]") {
    Character &guy = fresh_guy();
    const detached_ptr<item> helm = spawn_pa("power_armor_helmet_basic");
    REQUIRE(helm->is_power_armor());
    const ret_val<bool> res = guy.can_wear(*helm);
    CHECK_FALSE(res.success());
    CHECK(res.str().find("only wear power armor components") != std::string::npos);
    guy.wear_item(spawn_pa("power_armor_exo_1"), false);
    CHECK(guy.can_wear(*helm).success());
}
