#include "ablative_coverage.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "creature.h"
#include "damage.h"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "monster.h"
#include "options.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "profession.h"
#include "pocket_type.h"
#include "recipe_dictionary.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "itype_slots/islot_armor.h"

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace {

// Test-only coverage model: always picks the first plate of the carrier, so every case below
// holds whatever model P1e step 27 installs.
auto first_plate_model() -> plate_coverage_model {
    return []( item &carrier, const Character &, const bodypart_id &, const sub_bodypart_str_id &,
               const attack_context & ) -> item_pocket * {
        for (item_pocket &pocket : carrier.contents.get_pockets()) {
            if (pocket.get_data().ablative && !pocket.empty()) {
                return &pocket;
            }
        }
        return nullptr;
    };
}

// Test-only coverage model that honours the sub-location: picks the first plate that covers
// the hit sub-location at all (what DDA's model does before it walks the slices).
auto covering_plate_model() -> plate_coverage_model {
    return []( item &carrier, const Character &, const bodypart_id &bp,
               const sub_bodypart_str_id &sbp, const attack_context & ) -> item_pocket * {
        for (item_pocket &pocket : carrier.contents.get_pockets()) {
            if (pocket.get_data().ablative && !pocket.empty() &&
                pocket.items().front()->get_coverage(bp, sbp) > 0) {
                return &pocket;
            }
        }
        return nullptr;
    };
}

auto put_plate(item &carrier, const std::string &id) -> bool {
    return carrier.try_put_in(item::spawn(itype_id(id))).success();
}

auto plate_ids(item &carrier) -> std::vector<std::string> {
    std::vector<std::string> ids;
    for (const item *plate : ablative::plates(carrier)) {
        ids.push_back(plate->typeId().str());
    }
    return ids;
}

// Cut damage absorbed out of 10 by the torso of the character.
auto absorbed(Character &guy, int amount = 10) -> int {
    damage_instance dmg(damage_type::DT_CUT, amount);
    guy.absorb_hit(bodypart_id("torso"), dmg);
    return amount - static_cast<int>(dmg.damage_units[0].amount);
}

} // namespace

TEST_CASE("plate slots take only matching plates", "[armor][ablative]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);

    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    REQUIRE(ablative::has_plate_slots(*carrier));
    REQUIRE(ablative::plates(*carrier).empty());

    SECTION("a plate goes into a matching slot, and only one plate per slot") {
        CHECK(put_plate(*carrier, "test_plate_large"));
        CHECK(put_plate(*carrier, "test_plate_large"));
        CHECK(plate_ids(*carrier).size() == 2);
        // both large slots are taken: refused, with a message that says why
        detached_ptr<item> third = item::spawn(itype_id("test_plate_large"));
        const ret_val<bool> res = carrier->try_put_in(std::move(third));
        CHECK_FALSE(res.success());
        CHECK(std::string(res.c_str()).find("taken") != std::string::npos);
        // the medium slots are still free
        CHECK(put_plate(*carrier, "test_plate_medium"));
        CHECK(put_plate(*carrier, "test_plate_medium"));
        CHECK(plate_ids(*carrier).size() == 4);
    }
    SECTION("a medium plate doesn't go into a large slot") {
        CHECK(put_plate(*carrier, "test_plate_medium"));
        CHECK(put_plate(*carrier, "test_plate_medium"));
        CHECK_FALSE(put_plate(*carrier, "test_plate_medium"));
        for (const item_pocket &pocket : carrier->contents.get_pockets()) {
            if (!pocket.empty()) {
                CHECK(pocket.get_data().get_flag_restrictions().contains(flag_id("ABLATIVE_MEDIUM")));
            }
        }
    }
    SECTION("the protector fits only its own slot") {
        CHECK(put_plate(*carrier, "test_plate_protector"));
        CHECK_FALSE(put_plate(*carrier, "test_plate_protector"));
        // a large plate doesn't take its place
        CHECK(put_plate(*carrier, "test_plate_large"));
        const item_pocket *protector_slot = nullptr;
        for (const item_pocket &pocket : carrier->contents.get_pockets()) {
            if (pocket.get_data().item_id_restriction.contains(itype_id("test_plate_protector"))) {
                protector_slot = &pocket;
            }
        }
        REQUIRE(protector_slot != nullptr);
        CHECK(protector_slot->size() == 1);
        CHECK(protector_slot->items().front()->typeId() == itype_id("test_plate_protector"));
    }
    SECTION("other items are refused") {
        CHECK_FALSE(put_plate(*carrier, "test_screwdriver"));
        CHECK_FALSE(put_plate(*carrier, "test_longshirt"));
    }
    SECTION("best_pocket chooses a matching plate slot, as in DDA") {
        // DDA item_contents::best_pocket does not exclude ablative pockets (J3, owner U4/D6): a
        // plate that fits a slot is slotted. So does the carried inventory (Character::best_pocket,
        // J4), see "carrying a plate slots it".
        detached_ptr<item> plate = item::spawn(itype_id("test_plate_large"));
        const pocket_choice chosen = carrier->contents.best_pocket(*plate);
        REQUIRE(chosen.pocket != nullptr);
        CHECK(chosen.pocket->get_data().ablative);
        // the router (spawns, migration) places it in the slot too
        carrier->put_in(std::move(plate));
        REQUIRE(plate_ids(*carrier).size() == 1);
        const item *placed = ablative::plates(*carrier).front();
        const item_pocket *pocket = carrier->contents.pocket_of(placed);
        REQUIRE(pocket != nullptr);
        CHECK(pocket->get_data().ablative);
    }
    SECTION("a plate can't be worn on its own") {
        detached_ptr<item> plate = item::spawn(itype_id("test_plate_large"));
        CHECK(plate->has_flag(flag_id("CANT_WEAR")));
        CHECK_FALSE(guy.can_wear(*plate).success());
    }
    SECTION("inserting a plate costs the slot's moves") {
        // J4 (deliberate, not weakened): DDA's item_store_cost has no plate-slot maximum; the
        // 800 moves are the slot pocket's own `moves`, which the insert path charges.
        detached_ptr<item> plate = item::spawn(itype_id("test_plate_large"));
        const pocket_choice slot = carrier->contents.best_pocket(*plate);
        REQUIRE(slot.pocket != nullptr);
        CHECK(slot.pocket->moves() >= 800);
        CHECK(carrier->insert_cost(*plate) >= 800);
    }
    SECTION("a plate taken out of a worn carrier slots back in when stored, per DDA") {
        // J4 (U4/D6): a plate is a legal content of its plate slot, so Character::best_pocket
        // chooses the slot again (DDA item_pocket.cpp can_contain via is_compatible)
        detached_ptr<item> worn_ptr = item::spawn(itype_id("test_plate_carrier"));
        REQUIRE(put_plate(*worn_ptr, "test_plate_large"));
        item &worn_carrier = *worn_ptr;
        guy.wear_item(std::move(worn_ptr), false);
        REQUIRE(ablative::plates(worn_carrier).size() == 1);

        detached_ptr<item> taken = worn_carrier.contents.remove_top(ablative::plates(worn_carrier).front());
        REQUIRE(taken);
        REQUIRE(ablative::plates(worn_carrier).empty());
        const ret_val<item *> added = guy.try_add(std::move(taken));
        REQUIRE(added.success());
        CHECK(ablative::plates(worn_carrier).size() == 1);
    }
    SECTION("a removed plate is intact and can be inserted again on purpose") {
        REQUIRE(put_plate(*carrier, "test_plate_large"));
        item *plate = ablative::plates(*carrier).front();
        detached_ptr<item> removed = carrier->contents.remove_top(plate);
        REQUIRE(removed);
        CHECK(removed->typeId() == itype_id("test_plate_large"));
        CHECK(ablative::plates(*carrier).empty());
        // and it can go back
        CHECK(carrier->try_put_in(std::move(removed)).success());
    }
    SECTION("put_in slots a plate into a matching plate slot by itself, as in DDA") {
        // the by-hand path of every put_in caller: no slot is named
        carrier->put_in(item::spawn(itype_id("test_plate_medium")));
        carrier->put_in(item::spawn(itype_id("test_plate_large")));
        carrier->put_in(item::spawn(itype_id("test_plate_protector")));
        REQUIRE(ablative::plates(*carrier).size() == 3);
        for (const item *plate : ablative::plates(static_cast<const item &>(*carrier))) {
            const item_pocket *slot = carrier->contents.pocket_of(plate);
            REQUIRE(slot != nullptr);
            CHECK(slot->get_data().ablative);
            CHECK(slot->is_compatible(*plate).success());
        }
        // a plate of the wrong size is not forced into a slot of another size
        carrier->put_in(item::spawn(itype_id("test_plate_medium")));
        carrier->put_in(item::spawn(itype_id("test_plate_medium")));
        int mediums = 0;
        for (const item *plate : ablative::plates(static_cast<const item &>(*carrier))) {
            if (plate->typeId() == itype_id("test_plate_medium")) {
                ++mediums;
            }
        }
        // the carrier has two medium slots: the third medium plate is not in a plate slot
        CHECK(mediums == 2);
    }
}

TEST_CASE("plate coverage seam", "[armor][ablative]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);

    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    REQUIRE(put_plate(*carrier, "test_plate_large"));
    item &worn_carrier = *carrier;
    guy.wear_item(std::move(carrier), false);

    SECTION("an empty model: no plate is ever chosen") {
        scoped_plate_coverage_model none{plate_coverage_model()};
        const int soft = absorbed(guy);
        // the carrier alone absorbs 4 of the 10 cut damage at 100% coverage
        CHECK(soft == 4);
        CHECK(select_ablative_pocket(worn_carrier, guy, bodypart_id("torso"),
                                     sub_bodypart_str_id::NULL_ID(), attack_context()) == nullptr);
    }
    SECTION("a model that picks the plate lets it mitigate before the carrier") {
        int without_model = 0;
        {
            scoped_plate_coverage_model none{plate_coverage_model()};
            without_model = absorbed(guy);
        }
        scoped_plate_coverage_model model(first_plate_model());
        const int with_model = absorbed(guy);
        CHECK(with_model > without_model);
    }
    SECTION("deal_damage passes the attacker and its tile into the model") {
        clear_creatures();
        const tripoint_bub_ms where(guy.bub_pos().x() + 2, guy.bub_pos().y(), guy.bub_pos().z());
        monster *zed = g->place_critter_at(mtype_id("mon_zombie"), where);
        REQUIRE(zed != nullptr);

        attack_context seen;
        bool called = false;
        scoped_plate_coverage_model model([&](item &, const Character &, const bodypart_id &,
                                               const sub_bodypart_str_id &,
                                               const attack_context &ctx) -> item_pocket * {
            seen = ctx;
            called = true;
            return nullptr;
        });
        damage_instance dmg(damage_type::DT_CUT, 5);
        guy.deal_damage(zed, bodypart_id("torso"), dmg);
        CHECK(called);
        CHECK(seen.source == zed);
        REQUIRE(seen.origin.has_value());
        CHECK(*seen.origin == zed->bub_pos());

        // the two-argument absorb_hit passes an empty context
        called = false;
        seen = attack_context();
        damage_instance plain(damage_type::DT_CUT, 5);
        guy.absorb_hit(bodypart_id("torso"), plain);
        CHECK(called);
        CHECK(seen.source == nullptr);
        CHECK_FALSE(seen.origin.has_value());
    }
}

TEST_CASE("plates are damaged and replaced", "[armor][ablative]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    scoped_plate_coverage_model model(first_plate_model());

    const auto hit_until = [&](const std::function<bool()> &done) {
        for (int i = 0; i < 5000 && !done(); ++i) {
            absorbed(guy, 60);
        }
        return done();
    };

    SECTION("a plate with non_functional becomes the compromised plate in the same slot") {
        detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
        REQUIRE(put_plate(*carrier, "test_plate_large"));
        item &worn = *carrier;
        guy.wear_item(std::move(carrier), false);
        const item *plate_before = ablative::plates(worn).front();
        const item_pocket *slot = worn.contents.pocket_of(plate_before);
        REQUIRE(slot != nullptr);
        REQUIRE(hit_until([&] { return ablative::plates(worn).front()->typeId() != itype_id("test_plate_large"); }));
        const std::vector<const item *> after = ablative::plates(static_cast<const item &>(worn));
        REQUIRE(after.size() == 1);
        CHECK(after.front()->typeId() == itype_id("test_plate_broken"));
        // in the same slot
        CHECK(worn.contents.pocket_of(after.front()) == slot);
    }
    SECTION("a plate without non_functional is removed") {
        detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
        REQUIRE(put_plate(*carrier, "test_plate_medium"));
        item &worn = *carrier;
        guy.wear_item(std::move(carrier), false);
        REQUIRE(hit_until([&] { return ablative::plates(worn).empty(); }));
        CHECK(ablative::plates(worn).empty());
    }
}

// DDA item_degrade.cpp:964-985 (item::damage_armor_transforms): plates are rated to survive three
// hits at the damage they were made for.
TEST_CASE("a plate breaks with a chance that scales with the damage it is rated for", "[armor][ablative]") {
    clear_all_state();
    detached_ptr<item> plate = item::spawn(itype_id("test_plate_large"));
    const int resist = plate->damage_resist(DT_CUT, true);
    REQUIRE(resist > 0);

    constexpr int trials = 20000;
    const auto break_rate = [&](float amount, double multiplier = 1.0) {
        int broken = 0;
        for (int i = 0; i < trials; ++i) {
            damage_unit du(DT_CUT, amount);
            if (plate->damage_armor_transforms(du, multiplier) == item::armor_status::TRANSFORMED) {
                ++broken;
            }
        }
        return 100.0 * broken / trials;
    };

    SECTION("a hit of the rated damage breaks it about one time in three") {
        CHECK(break_rate(static_cast<float>(resist)) == Approx(33.3).margin(2.0));
    }
    SECTION("half the rated damage breaks it about one time in six") {
        CHECK(break_rate(resist / 2.0f) == Approx(16.65).margin(2.0));
    }
    SECTION("chip damage under a fifth of the rating never breaks it") {
        CHECK(break_rate(resist * 0.19f) == 0.0);
    }
    SECTION("far more than the rating always breaks it") {
        CHECK(break_rate(resist * 4.0f) >= 99.0);
    }
    SECTION("an enchantment multiplier scales the chance, zero or less removes it") {
        CHECK(break_rate(static_cast<float>(resist), 0.5) == Approx(16.65).margin(2.0));
        CHECK(break_rate(static_cast<float>(resist), 0.0) == 0.0);
    }
    SECTION("in the armor flow a carrier plate breaks after about three rated hits") {
        // each trial: a fresh worn carrier takes hits of the rated cut damage until the plate
        // is replaced; the average is about three (33.3% each)
        Character &guy = get_player_character();
        clear_character(*guy.as_player(), true);
        scoped_plate_coverage_model model(first_plate_model());
        int total_hits = 0;
        constexpr int carriers = 400;
        for (int i = 0; i < carriers; ++i) {
            guy.worn.clear();
            detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
            REQUIRE(put_plate(*carrier, "test_plate_large"));
            item &worn = *carrier;
            guy.wear_item(std::move(carrier), false);
            int hits = 0;
            while (hits < 100 && ablative::plates(worn).front()->typeId() == itype_id("test_plate_large")) {
                absorbed(guy, resist);
                ++hits;
            }
            REQUIRE(hits < 100);
            total_hits += hits;
        }
        CHECK(static_cast<double>(total_hits) / carriers == Approx(3.0).margin(0.6));
    }
}

TEST_CASE("a destroyed carrier spills its plates", "[armor][ablative]") {
    clear_all_state();
    clear_map();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);

    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier_soft"));
    REQUIRE(put_plate(*carrier, "test_plate_large"));
    guy.wear_item(std::move(carrier), false);
    REQUIRE(guy.worn.size() == 1);
    // an empty model: the plate takes nothing, the carrier takes it all
    scoped_plate_coverage_model none{plate_coverage_model()};
    for (int i = 0; i < 5000 && !guy.worn.empty(); ++i) {
        absorbed(guy, 60);
    }
    REQUIRE(guy.worn.empty());
    bool found = false;
    for (const item *it : get_map().i_at(guy.bub_pos())) {
        found = found || it->typeId() == itype_id("test_plate_large");
    }
    CHECK(found);
}

TEST_CASE("plates change what a carrier covers and encumbers", "[armor][ablative]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    const bodypart_id torso("torso");
    const bodypart_id arm_l("arm_l");

    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    SECTION("a protector makes the carrier cover the shoulders") {
        CHECK_FALSE(carrier->covers(arm_l));
        REQUIRE(put_plate(*carrier, "test_plate_protector"));
        CHECK(carrier->covers(arm_l));
        CHECK(carrier->covers(bodypart_id("arm_r")));
        // deliberate rewrite (P1-transplant J5, DDA item_armor.cpp:804-868): where the carrier
        // has no coverage of its own it is as good as its best plate
        CHECK(carrier->get_coverage(arm_l) > 0);
        CHECK(carrier->get_coverage(arm_l) ==
              ablative::plates(static_cast<const item &>(*carrier)).front()->get_coverage(arm_l));
    }
    SECTION("plate encumbrance is added, plate volume is not stored volume") {
        const int base = carrier->get_encumber(guy, torso);
        REQUIRE(base == 4);
        REQUIRE(put_plate(*carrier, "test_plate_large"));
        REQUIRE(put_plate(*carrier, "test_plate_large"));
        CHECK(carrier->get_encumber(guy, torso) == base + 3 + 3);
    }
    SECTION("weight includes the plates") {
        const units::mass base = carrier->weight();
        REQUIRE(put_plate(*carrier, "test_plate_large"));
        const item *plate = ablative::plates(static_cast<const item &>(*carrier)).front();
        CHECK(carrier->weight() == base + plate->weight());
    }
    SECTION("the carrier's plates are listed in the item info") {
        REQUIRE(put_plate(*carrier, "test_plate_large"));
        const std::vector<iteminfo> info = carrier->info();
        bool listed = false;
        for (const iteminfo &line : info) {
            listed = listed || line.sName.find("TEST large plate") != std::string::npos;
        }
        CHECK(listed);
    }
}

TEST_CASE("a plate carrier tests its own coverage with the plates' roll", "[armor][ablative]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);

    SECTION("the carrier's coverage test uses the roll the model was given") {
        detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier_half"));
        // deliberate rewrite (P1-transplant J5): the coverage of a carrier is the larger of its
        // own and its plates', so the plate has to cover less than the 50 of the carrier
        REQUIRE(put_plate(*carrier, "test_plate_light"));
        REQUIRE(carrier->get_coverage(bodypart_id("torso")) == 50);
        guy.wear_item(std::move(carrier), false);

        int last_roll = 0;
        // a model that sees the roll and never picks a plate: only the carrier acts
        scoped_plate_coverage_model model([&](item &, const Character &, const bodypart_id &,
                                               const sub_bodypart_str_id &,
                                               const attack_context &ctx) -> item_pocket * {
            last_roll = ctx.coverage_roll;
            return nullptr;
        });
        int covered_hits = 0;
        int gap_hits = 0;
        for (int i = 0; i < 300; ++i) {
            last_roll = 0;
            const int soft = absorbed(guy);
            REQUIRE(last_roll >= 1);
            REQUIRE(last_roll <= 100);
            // the carrier mitigates exactly when the shared roll is inside its 50% coverage
            if (last_roll <= 50) {
                CHECK(soft > 0);
                ++covered_hits;
            } else {
                CHECK(soft == 0);
                ++gap_hits;
            }
        }
        CHECK(covered_hits > 0);
        CHECK(gap_hits > 0);
    }
    SECTION("armor without plate slots is not given a roll") {
        guy.wear_item(item::spawn(itype_id("test_plain_vest")), false);
        bool called = false;
        scoped_plate_coverage_model model([&](item &, const Character &, const bodypart_id &,
                                               const sub_bodypart_str_id &,
                                               const attack_context &) -> item_pocket * {
            called = true;
            return nullptr;
        });
        absorbed(guy);
        CHECK_FALSE(called);
    }
}

// Both settings of NEW_ARMOR_CALCULATION go through the same plate code (armor_take_hit).
namespace {

void check_plate_wear_under(const std::string &setting) {
    clear_all_state();
    const override_option calculation("NEW_ARMOR_CALCULATION", setting);
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);

    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    REQUIRE(put_plate(*carrier, "test_plate_large"));
    item &worn = *carrier;
    guy.wear_item(std::move(carrier), false);

    int without_model = 0;
    {
        scoped_plate_coverage_model none{plate_coverage_model()};
        without_model = absorbed(guy);
    }
    scoped_plate_coverage_model model(first_plate_model());
    // the plate mitigates before the carrier in both calculations
    CHECK(absorbed(guy) > without_model);

    bool replaced = false;
    for (int i = 0; i < 5000 && !replaced; ++i) {
        absorbed(guy, 60);
        replaced = ablative::plates(worn).front()->typeId() != itype_id("test_plate_large");
    }
    REQUIRE(replaced);
    CHECK(ablative::plates(worn).front()->typeId() == itype_id("test_plate_broken"));
}

} // namespace

TEST_CASE("plates wear under both armor damage calculations", "[armor][ablative]") {
    SECTION("NEW_ARMOR_CALCULATION on") {
        check_plate_wear_under("true");
    }
    SECTION("NEW_ARMOR_CALCULATION off") {
        check_plate_wear_under("false");
    }
}

// J4 (deliberate, not weakened): this case used to say the carried inventory never uses plate
// slots. DDA's Character::best_pocket reaches every pocket of the worn items, plate slots
// included (owner U4/D6), so a carried plate is slotted; the overflow check at the end is unchanged.
TEST_CASE("carrying a plate slots it", "[armor][ablative]") {
    clear_all_state();
    clear_map();
    Character &guy = get_player_character();
    // no debug storage: the character has no carry pocket at all
    clear_character(*guy.as_player(), false);

    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    item &worn_carrier = *carrier;
    guy.wear_item(std::move(carrier), false);
    REQUIRE(ablative::has_plate_slots(worn_carrier));
    REQUIRE(ablative::plates(worn_carrier).empty());

    detached_ptr<item> plate = item::spawn(itype_id("test_plate_large"));
    SECTION("the plate slot is the best pocket for a plate") {
        const pocket_choice chosen = guy.best_pocket(*plate);
        REQUIRE(chosen.pocket != nullptr);
        CHECK(chosen.pocket->get_data().ablative);
        CHECK(chosen.container == &worn_carrier);
        CHECK(guy.can_stash(*plate).success());
        CHECK(guy.try_add(item::spawn(itype_id("test_plate_large"))).success());
        CHECK(ablative::plates(worn_carrier).size() == 1);
    }
    SECTION("i_add, which autopickup uses, slots a plate instead of dropping it") {
        guy.i_add(std::move(plate));
        CHECK(ablative::plates(worn_carrier).size() == 1);
        bool on_ground = false;
        for (const item *it : get_map().i_at(guy.bub_pos())) {
            on_ground = on_ground || it->typeId() == itype_id("test_plate_large");
        }
        CHECK_FALSE(on_ground);
    }
    SECTION("overflow only reaches a plate slot for items that came from the migration pocket") {
        // overflow places an evicted item through fitting_pocket( it, from_migration )
        CHECK(worn_carrier.contents.fitting_pocket(*plate, false) == nullptr);
        const item_pocket *slot = worn_carrier.contents.fitting_pocket(*plate, true);
        REQUIRE(slot != nullptr);
        CHECK(slot->get_data().ablative);
    }
}

TEST_CASE("a shoulder protector stops shoulder hits and never forearm hits", "[armor][ablative]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    const bodypart_id arm_l("arm_l");
    const sub_bodypart_str_id shoulder("arm_shoulder_l");
    const sub_bodypart_str_id lower("arm_lower_l");

    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    item &worn = *carrier;
    guy.wear_item(std::move(carrier), false);

    SECTION("without the protector the carrier doesn't cover the arm") {
        CHECK_FALSE(worn.covers(arm_l));
        CHECK(select_ablative_pocket(worn, guy, arm_l, shoulder, attack_context()) == nullptr);
    }
    SECTION("the plate's coverage names the sub-location") {
        REQUIRE(put_plate(worn, "test_plate_protector"));
        const item *protector = ablative::plates(static_cast<const item &>(worn)).front();
        CHECK(protector->get_coverage(arm_l, shoulder) == 80);
        CHECK(protector->get_coverage(arm_l, lower) == 0);
        CHECK(worn.covers(arm_l));
    }
    SECTION("a model that honours sub-locations picks the protector for the shoulder only") {
        REQUIRE(put_plate(worn, "test_plate_protector"));
        scoped_plate_coverage_model model(covering_plate_model());
        CHECK(select_ablative_pocket(worn, guy, arm_l, shoulder, attack_context()) != nullptr);
        CHECK(select_ablative_pocket(worn, guy, arm_l, lower, attack_context()) == nullptr);
        CHECK(select_ablative_pocket(worn, guy, arm_l, sub_bodypart_str_id("arm_upper_l"),
                                     attack_context()) == nullptr);
    }
    SECTION("arm hits are reduced only when they landed on the shoulder") {
        REQUIRE(put_plate(worn, "test_plate_protector"));
        sub_bodypart_str_id seen = sub_bodypart_str_id::NULL_ID();
        const plate_coverage_model inner = covering_plate_model();
        scoped_plate_coverage_model model([&](item &carrier_item, const Character &wearer,
                                               const bodypart_id &bp, const sub_bodypart_str_id &sbp,
                                               const attack_context &ctx) -> item_pocket * {
            seen = sbp;
            return inner(carrier_item, wearer, bp, sbp, ctx);
        });
        int stopped_on_shoulder = 0;
        for (int i = 0; i < 1000; ++i) {
            if (ablative::plates(worn).empty()) {
                // a destroyed protector is replaced, the case is about which hits it can stop
                REQUIRE(put_plate(worn, "test_plate_protector"));
            }
            seen = sub_bodypart_str_id::NULL_ID();
            damage_instance dmg(damage_type::DT_CUT, 10);
            guy.absorb_hit(arm_l, dmg);
            const bool reduced = dmg.damage_units[0].amount < 10.0f;
            if (reduced) {
                // only the shoulder can be stopped
                CHECK(seen == shoulder);
                ++stopped_on_shoulder;
            }
            if (seen == lower) {
                CHECK_FALSE(reduced);
            }
        }
        CHECK(stopped_on_shoulder > 0);
    }
}

// DDA's plate coverage model (P1e step 27): stacked slices of the rolled sub-location.
namespace {

// Two large plates (45 of the upper torso each) and two medium ones (35 of the lower torso
// each), in the carrier's pocket order: large, large, medium, medium.
auto sliced_carrier() -> detached_ptr<item> {
    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    carrier->put_in(item::spawn(itype_id("test_slice_plate_large")));
    carrier->put_in(item::spawn(itype_id("test_slice_plate_large")));
    carrier->put_in(item::spawn(itype_id("test_slice_plate_medium")));
    carrier->put_in(item::spawn(itype_id("test_slice_plate_medium")));
    return carrier;
}

// Position of @p chosen among the plate slots of @p carrier, -1 for no slot.
auto plate_slot_index(item &carrier, const item_pocket *chosen) -> int {
    if (chosen == nullptr) {
        return -1;
    }
    int index = 0;
    for (const item_pocket &pocket : carrier.contents.get_pockets()) {
        if (!pocket.get_data().ablative) {
            continue;
        }
        if (&pocket == chosen) {
            return index;
        }
        ++index;
    }
    return -1;
}

} // namespace

TEST_CASE("the DDA model walks plate slices in pocket order", "[armor][ablative][dda_model]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    const bodypart_id torso("torso");
    const sub_bodypart_str_id upper("torso_upper");
    const sub_bodypart_str_id lower("torso_lower");

    detached_ptr<item> carrier = sliced_carrier();
    REQUIRE(ablative::plates(*carrier).size() == 4);

    // the model in play when nothing else is installed
    CHECK(static_cast<bool>(get_plate_coverage_model()));

    const auto slot_for_roll = [&](const sub_bodypart_str_id &sbp, int roll) {
        attack_context ctx;
        ctx.coverage_roll = roll;
        return plate_slot_index(*carrier, select_ablative_pocket(*carrier, guy, torso, sbp, ctx));
    };

    SECTION("upper torso: rolls 1-45 hit the first large plate, 46-90 the second, 91-100 miss") {
        for (int roll = 1; roll <= 100; ++roll) {
            const int expected = roll <= 45 ? 0 : roll <= 90 ? 1 : -1;
            CAPTURE(roll);
            CHECK(slot_for_roll(upper, roll) == expected);
        }
    }
    SECTION("lower torso: the large plates cover nothing, the medium ones 1-35 and 36-70") {
        for (int roll = 1; roll <= 100; ++roll) {
            const int expected = roll <= 35 ? 2 : roll <= 70 ? 3 : -1;
            CAPTURE(roll);
            CHECK(slot_for_roll(lower, roll) == expected);
        }
    }
    SECTION("a roll equal to the coverage is a hit (DDA #66874)") {
        CHECK(slot_for_roll(upper, 45) == 0);
        CHECK(slot_for_roll(upper, 46) == 1);
        CHECK(slot_for_roll(upper, 90) == 1);
        CHECK(slot_for_roll(upper, 91) == -1);
    }
    SECTION("an empty slot is skipped, so the next plate takes its slice") {
        item *first = ablative::plates(*carrier).front();
        detached_ptr<item> removed = carrier->contents.remove_top(first);
        REQUIRE(removed);
        // the remaining large plate now covers rolls 1-45
        CHECK(slot_for_roll(upper, 1) == 1);
        CHECK(slot_for_roll(upper, 45) == 1);
        CHECK(slot_for_roll(upper, 46) == -1);
    }
    SECTION("a carrier with no plates never selects one") {
        detached_ptr<item> empty = item::spawn(itype_id("test_plate_carrier"));
        attack_context ctx;
        ctx.coverage_roll = 1;
        CHECK(select_ablative_pocket(*empty, guy, torso, upper, ctx) == nullptr);
    }
}

TEST_CASE("DDA plate hit rates over rolled torso hits", "[armor][ablative][dda_model]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    const bodypart_id torso("torso");

    detached_ptr<item> carrier = sliced_carrier();
    REQUIRE(ablative::plates(*carrier).size() == 4);

    SECTION("select_ablative_pocket: about 27%, 27%, 14%, 14% of hits and 18% gaps") {
        constexpr int hits = 20000;
        std::array<int, 5> counts{}; // slots 0-3, then gaps
        for (int i = 0; i < hits; ++i) {
            // the sub-location is rolled as Character::absorb_hit rolls it; the model rolls
            // its own coverage roll for an empty context
            const sub_bodypart_str_id &sbp = torso->random_sub_part();
            const int slot = plate_slot_index(*carrier,
                                              select_ablative_pocket(*carrier, guy, torso, sbp, attack_context()));
            counts[slot < 0 ? 4 : slot]++;
        }
        const auto rate = [&](int slot) { return 100.0 * counts[slot] / hits; };
        CHECK(rate(0) == Approx(27.0).margin(1.5));
        CHECK(rate(1) == Approx(27.0).margin(1.5));
        CHECK(rate(2) == Approx(14.0).margin(1.5));
        CHECK(rate(3) == Approx(14.0).margin(1.5));
        CHECK(rate(4) == Approx(18.0).margin(1.5));
    }
    SECTION("through absorb_hit on a worn carrier the plates stop about 82% of torso hits") {
        guy.wear_item(std::move(carrier), false);
        constexpr int hits = 4000;
        int plated = 0;
        for (int i = 0; i < hits; ++i) {
            // the soft carrier alone absorbs 4 of 10 cut at 100% coverage; more means a plate
            if (absorbed(guy) > 4) {
                ++plated;
            }
        }
        CHECK(100.0 * plated / hits == Approx(82.0).margin(4.0));
    }
}

// DDA's armor plates and IOTV protectors (P1e step 29).
TEST_CASE("the DDA plate items load with their size, coverage and leftovers", "[armor][ablative][plates]") {
    clear_all_state();
    const bodypart_id torso("torso");
    const sub_bodypart_str_id upper("torso_upper");
    const sub_bodypart_str_id lower("torso_lower");

    const std::vector<std::string> steel_tiers = {"scrap", "lc", "mc", "hc", "ch", "qt"};
    std::vector<std::string> large = {"esapi_plate", "steel_ballistic_plate",
                                      "heavy_steel_ballistic_plate", "destroyed_large_ceramic_plate"};
    std::vector<std::string> medium = {"esbi_plate", "deltoid_plates",
                                       "destroyed_medium_ceramic_plate"};
    for (const std::string &tier : steel_tiers) {
        large.push_back(tier + "_esapi_plate");
        medium.push_back(tier + "_esbi_plate");
    }

    SECTION("large plates: ABLATIVE_LARGE, CANT_WEAR, 45 of the upper torso") {
        for (const std::string &id : large) {
            CAPTURE(id);
            detached_ptr<item> plate = item::spawn(itype_id(id));
            REQUIRE(plate->find_armor_data() != nullptr);
            CHECK(plate->has_flag(flag_id("ABLATIVE_LARGE")));
            CHECK_FALSE(plate->has_flag(flag_id("ABLATIVE_MEDIUM")));
            CHECK(plate->has_flag(flag_id("CANT_WEAR")));
            CHECK(plate->get_coverage(torso, upper) == 45);
            CHECK(plate->get_coverage(torso, lower) == 0);
        }
    }
    SECTION("medium plates: ABLATIVE_MEDIUM, CANT_WEAR, 35 of the lower torso or the shoulders") {
        for (const std::string &id : medium) {
            CAPTURE(id);
            detached_ptr<item> plate = item::spawn(itype_id(id));
            REQUIRE(plate->find_armor_data() != nullptr);
            CHECK(plate->has_flag(flag_id("ABLATIVE_MEDIUM")));
            CHECK_FALSE(plate->has_flag(flag_id("ABLATIVE_LARGE")));
            CHECK(plate->has_flag(flag_id("CANT_WEAR")));
            if (id == "deltoid_plates") {
                CHECK(plate->get_coverage(bodypart_id("arm_l"), sub_bodypart_str_id("arm_shoulder_l")) == 35);
                CHECK(plate->get_coverage(bodypart_id("arm_l"), sub_bodypart_str_id("arm_lower_l")) == 0);
            } else {
                CHECK(plate->get_coverage(torso, lower) == 35);
                CHECK(plate->get_coverage(torso, upper) == 0);
            }
        }
    }
    SECTION("ceramic plates shatter into the compromised plate of their own size") {
        const struct {
            const char *plate;
            const char *leftover;
            const char *size;
        } pairs[] = {
            {"esapi_plate", "destroyed_large_ceramic_plate", "ABLATIVE_LARGE"},
            {"esbi_plate", "destroyed_medium_ceramic_plate", "ABLATIVE_MEDIUM"},
        };
        for (const auto &pair : pairs) {
            CAPTURE(pair.plate);
            detached_ptr<item> plate = item::spawn(itype_id(pair.plate));
            REQUIRE(plate->find_armor_data() != nullptr);
            CHECK(plate->find_armor_data()->non_functional == itype_id(pair.leftover));
            CHECK_FALSE(plate->find_armor_data()->damage_verb.empty());
            detached_ptr<item> leftover = item::spawn(itype_id(pair.leftover));
            CHECK(leftover->has_flag(flag_id(pair.size)));
            // compromised: it protects less than the plate it came from
            CHECK(leftover->bullet_resist() < plate->bullet_resist());
            // and it has no leftover of its own
            REQUIRE(leftover->find_armor_data() != nullptr);
            CHECK(leftover->find_armor_data()->non_functional.is_null());
        }
    }
    SECTION("the steel tiers keep DDA's order in bullet resistance") {
        for (const char *kind : {"esapi", "esbi"}) {
            int previous = -1;
            for (const std::string &tier : steel_tiers) {
                const std::string id = tier + "_" + kind + "_plate";
                CAPTURE(id);
                detached_ptr<item> plate = item::spawn(itype_id(id));
                const int resist = plate->bullet_resist();
                CHECK(resist > previous);
                previous = resist;
            }
        }
    }
    SECTION("IOTV protectors cover the shoulders, groin and neck") {
        detached_ptr<item> shoulder = item::spawn(itype_id("iotv_shoulder_plate"));
        detached_ptr<item> groin = item::spawn(itype_id("iotv_groin_plate"));
        detached_ptr<item> neck = item::spawn(itype_id("iotv_neck_plate"));
        for (const item *protector : {&*shoulder, &*groin, &*neck}) {
            CHECK(protector->has_flag(flag_id("CANT_WEAR")));
            CHECK_FALSE(protector->has_flag(flag_id("ABLATIVE_LARGE")));
            CHECK_FALSE(protector->has_flag(flag_id("ABLATIVE_MEDIUM")));
        }
        const bodypart_id arm_l("arm_l");
        CHECK(shoulder->covers(arm_l));
        CHECK(shoulder->get_coverage(arm_l, sub_bodypart_str_id("arm_shoulder_l")) == 80);
        CHECK(shoulder->get_coverage(arm_l, sub_bodypart_str_id("arm_upper_l")) == 80);
        CHECK(shoulder->get_coverage(arm_l, sub_bodypart_str_id("arm_lower_l")) == 0);
        CHECK(groin->get_coverage(bodypart_id("leg_l"), sub_bodypart_str_id("leg_hip_l")) == 75);
        CHECK(groin->get_coverage(bodypart_id("leg_l"), sub_bodypart_str_id("leg_upper_l")) == 0);
        CHECK(neck->get_coverage(bodypart_id("head"), sub_bodypart_str_id("head_throat")) == 90);
        CHECK(neck->get_coverage(bodypart_id("head"), sub_bodypart_str_id("head_nape")) == 90);
        CHECK(neck->get_coverage(bodypart_id("head"), sub_bodypart_str_id("head_forehead")) == 0);
    }
    SECTION("the ESAPI and ESBI ids are live items again, not migrated to BN's plates") {
        CHECK(item_controller->migrate_id(itype_id("esapi_plate")) == itype_id("esapi_plate"));
        CHECK(item_controller->migrate_id(itype_id("esbi_plate")) == itype_id("esbi_plate"));
    }
    SECTION("plates can be crafted") {
        for (const std::string &id : large) {
            if (id.rfind("destroyed_", 0) == 0 || id == "steel_ballistic_plate" ||
                id == "heavy_steel_ballistic_plate") {
                continue;
            }
            CAPTURE(id);
            CHECK(recipe_id(id).is_valid());
        }
        CHECK(recipe_id("esbi_plate").is_valid());
        CHECK(recipe_id("iotv_shoulder_plate").is_valid());
    }
}

// Balance guard, part 1 (plan section 6). Deterministic: compares the bullet resistance of each
// ESAPI plate tier. The guard against BN's old combined vests (mean torso damage, no more than
// 10% worse) needs their values recorded from a pre-P1e build and is not in this file yet.
TEST_CASE("plate tiers order scrap < lc < mc < hc < ch < qt", "[armor][ablative][balance]") {
    clear_all_state();
    const std::array<std::string, 6> tiers = {"scrap", "lc", "mc", "hc", "ch", "qt"};
    int previous = -1;
    for (const std::string &tier : tiers) {
        CAPTURE(tier);
        const detached_ptr<item> plate = item::spawn(itype_id(tier + "_esapi_plate"));
        const int resist = plate->bullet_resist();
        CHECK(resist > previous);
        previous = resist;
    }
}

// AR5-VESTS: DDA vests, plates and EOD jackets ported as plain JSON (data/json/items/armor/
// cdf_ballistic_vests.json, cdf_ballistic_armor.json). Every literal below is re-derived from
// that CDF JSON, not from the loaded item.
TEST_CASE("ar5_level_3_vest_takes_two_large_plates", "[armor][ablative][ar5]") {
    clear_all_state();
    const bodypart_id torso("torso");
    detached_ptr<item> carrier = item::spawn(itype_id("level_3_vest"));
    REQUIRE(carrier->find_armor_data() != nullptr);
    CHECK(ablative::has_plate_slots(*carrier));
    CHECK(ablative::plates(*carrier).empty());

    // the two plate slots are large ones (flag_restriction in the JSON)
    int large_slots = 0;
    for (const item_pocket &pocket : carrier->contents.get_pockets()) {
        if (pocket.get_data().ablative) {
            CHECK(pocket.get_data().get_flag_restrictions().contains(flag_id("ABLATIVE_LARGE")));
            ++large_slots;
        }
    }
    CHECK(large_slots == 2);

    CHECK(put_plate(*carrier, "esapi_plate"));
    CHECK(put_plate(*carrier, "esapi_plate"));
    CHECK(plate_ids(*carrier) == std::vector<std::string>{"esapi_plate", "esapi_plate"});
    detached_ptr<item> third = item::spawn(itype_id("esapi_plate"));
    const ret_val<bool> res = carrier->try_put_in(std::move(third));
    CHECK_FALSE(res.success());
    CHECK(std::string(res.c_str()).find("taken") != std::string::npos);

    // a medium plate does not go into a large slot
    detached_ptr<item> fresh = item::spawn(itype_id("level_3_vest"));
    CHECK_FALSE(put_plate(*fresh, "esbi_plate"));

    // CDF: coverage 100 on the upper torso and 80 on the lower torso, as in the JSON
    detached_ptr<item> bare = item::spawn(itype_id("level_3_vest"));
    CHECK(bare->get_coverage(torso, sub_bodypart_str_id("torso_upper")) == 100);
    CHECK(bare->get_coverage(torso, sub_bodypart_str_id("torso_lower")) == 80);
    CHECK_FALSE(bare->covers(bodypart_id("arm_l")));
    // CDF: weight 2650 g, volume 6 L
    CHECK(bare->weight() == 2650_gram);
    CHECK(bare->volume() == 6_liter);
}

TEST_CASE("ar5_stab_and_eod_plates_fit_large_slots", "[armor][ablative][ar5]") {
    clear_all_state();
    for (const std::string id : {"stab_plate", "eod_plate"}) {
        CAPTURE(id);
        detached_ptr<item> plate = item::spawn(itype_id(id));
        REQUIRE(plate->find_armor_data() != nullptr);
        CHECK(plate->has_flag(flag_id("ABLATIVE_LARGE")));
        CHECK(plate->has_flag(flag_id("CANT_WEAR")));
        CHECK(plate->has_flag(flag_id("NO_SALVAGE")));
        CHECK_FALSE(plate->has_flag(flag_id("ABLATIVE_MEDIUM")));
        // CDF: both cover 45 of the upper torso, as in the JSON
        CHECK(plate->get_coverage(bodypart_id("torso"), sub_bodypart_str_id("torso_upper")) == 45);
        detached_ptr<item> carrier = item::spawn(itype_id("level_3_vest"));
        CHECK(put_plate(*carrier, id));
        CHECK(plate_ids(*carrier) == std::vector<std::string>{id});
    }
    // CDF: eod_plate covers the lower torso too (JSON: 45), stab_plate does not
    detached_ptr<item> eod = item::spawn(itype_id("eod_plate"));
    detached_ptr<item> stab = item::spawn(itype_id("stab_plate"));
    CHECK(eod->get_coverage(bodypart_id("torso"), sub_bodypart_str_id("torso_lower")) == 45);
    CHECK(stab->get_coverage(bodypart_id("torso"), sub_bodypart_str_id("torso_lower")) == 0);
    // CDF: weights and volumes from the JSON
    CHECK(eod->weight() == 2800_gram);
    CHECK(eod->volume() == 2950_ml);
    CHECK(stab->weight() == 500_gram);
    CHECK(stab->volume() == 300_ml);
    // neither plate goes into a medium slot
    detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
    int medium_slots = 0;
    for (const std::string id : {"stab_plate", "eod_plate"}) {
        for (const item_pocket &pocket : carrier->contents.get_pockets()) {
            if (pocket.get_data().ablative &&
                pocket.get_data().get_flag_restrictions().contains(flag_id("ABLATIVE_MEDIUM"))) {
                ++medium_slots;
                detached_ptr<item> p = item::spawn(itype_id(id));
                CHECK_FALSE(pocket.can_contain(*p).success());
            }
        }
    }
    CHECK(medium_slots == 4);
}

TEST_CASE("ar5_light_vest_family_plate_and_magazine_pockets", "[armor][ablative][ar5]") {
    clear_all_state();
    for (const std::string id : {"ballistic_vest_light", "ballistic_vest_light_operator",
                                 "ballistic_vest_light_pouches"}) {
        CAPTURE(id);
        detached_ptr<item> carrier = item::spawn(itype_id(id));
        REQUIRE(carrier->find_armor_data() != nullptr);
        CHECK(ablative::has_plate_slots(*carrier));
        // CDF: the pouches vest's unrestricted 900 ml zipper pouch takes an esbi_plate like any
        // container, so test that no plate slot took it instead of the put result
        put_plate(*carrier, "esbi_plate");
        CHECK(plate_ids(*carrier).empty());
        CHECK(put_plate(*carrier, "esapi_plate"));
        CHECK(put_plate(*carrier, "esapi_plate"));
        CHECK(plate_ids(*carrier).size() == 2);
        CHECK_FALSE(put_plate(*carrier, "esapi_plate"));
        // CDF: thorax only, 90 of the upper torso (JSON)
        detached_ptr<item> bare = item::spawn(itype_id(id));
        CHECK(bare->get_coverage(bodypart_id("torso"), sub_bodypart_str_id("torso_upper")) == 90);
        CHECK(bare->get_coverage(bodypart_id("torso"), sub_bodypart_str_id("torso_lower")) == 0);
        // CDF: weight 900 g, volume 4 L (JSON)
        CHECK(bare->weight() == 900_gram);
        CHECK(bare->volume() == 4_liter);
        int ablative_pockets = 0;
        int other_pockets = 0;
        for (const item_pocket &pocket : bare->contents.get_pockets()) {
            if (pocket.get_data().ablative) {
                ++ablative_pockets;
                CHECK_FALSE(pocket.get_data().get_flag_restrictions().contains(flag_id("ABLATIVE_MEDIUM")));
            } else if (pocket.get_data().type == pocket_type::CONTAINER) {
                ++other_pockets;
            }
        }
        CHECK(ablative_pockets == 2);
        // CDF: operator has the radio pouch and the glowstick hook, the others one pouch (JSON)
        CHECK(other_pockets == (id == "ballistic_vest_light_operator" ? 2 : 1));
    }
    const auto container_pockets = [](const item & it) {
        std::vector<const item_pocket *> res;
        for (const item_pocket &pocket : it.contents.get_pockets()) {
            if (!pocket.get_data().ablative && pocket.get_data().type == pocket_type::CONTAINER) {
                res.push_back(&pocket);
            }
        }
        return res;
    };
    detached_ptr<item> mag = item::spawn(itype_id("ballistic_vest_light"));
    const auto mag_pockets = container_pockets(*mag);
    REQUIRE(mag_pockets.size() == 1);
    // CDF: 900 ml MAG_COMPACT pocket (JSON)
    CHECK(mag_pockets[0]->volume_capacity() == 900_ml);
    CHECK(mag_pockets[0]->get_data().get_flag_restrictions().contains(flag_id("MAG_COMPACT")));
    detached_ptr<item> pouches = item::spawn(itype_id("ballistic_vest_light_pouches"));
    const auto pouch_pockets = container_pockets(*pouches);
    REQUIRE(pouch_pockets.size() == 1);
    // CDF: 900 ml zipper pouch, no flag restriction (JSON)
    CHECK(pouch_pockets[0]->volume_capacity() == 900_ml);
    CHECK(pouch_pockets[0]->get_data().get_flag_restrictions().empty());
    detached_ptr<item> op = item::spawn(itype_id("ballistic_vest_light_operator"));
    const auto op_pockets = container_pockets(*op);
    REQUIRE(op_pockets.size() == 2);
    // CDF: radio pouch 500 ml, glowstick hook 30 ml and a holster (JSON)
    int radio = 0;
    int hook = 0;
    for (const item_pocket *pocket : op_pockets) {
        if (pocket->volume_capacity() == 500_ml) {
            ++radio;
            CHECK_FALSE(pocket->get_data().holster);
        } else if (pocket->volume_capacity() == 30_ml) {
            ++hook;
            CHECK(pocket->get_data().holster);
        }
    }
    CHECK(radio == 1);
    CHECK(hook == 1);
}

TEST_CASE("ar5_soft_vests_have_no_plate_slots", "[armor][ablative][ar5]") {
    clear_all_state();
    const bodypart_id torso("torso");
    for (const std::string id : {"soft_3a_vest", "stab_vest", "fbi_armor_vest"}) {
        CAPTURE(id);
        detached_ptr<item> carrier = item::spawn(itype_id(id));
        REQUIRE(carrier->find_armor_data() != nullptr);
        CHECK_FALSE(ablative::has_plate_slots(*carrier));
        CHECK_FALSE(put_plate(*carrier, "esapi_plate"));
        CHECK_FALSE(put_plate(*carrier, "esbi_plate"));
        // CDF: a 300 ml stab_plate fits an ordinary 500 ml pocket, so only check that no plate slot holds it
        put_plate(*carrier, "stab_plate");
        CHECK(ablative::plates(*carrier).empty());
        std::vector<units::volume> sizes;
        for (const item_pocket &pocket : carrier->contents.get_pockets()) {
            if (pocket.get_data().type == pocket_type::CONTAINER) {
                CHECK_FALSE(pocket.get_data().ablative);
                sizes.push_back(pocket.volume_capacity());
            }
        }
        // CDF: pockets 500, 500 and 10 ml (JSON)
        CHECK(sizes == std::vector<units::volume>{500_ml, 500_ml, 10_ml});
    }
    // CDF: soft_3a_vest and fbi_armor_vest: 100 of the upper and 80 of the lower torso (JSON);
    // stab_vest: 100 of the torso
    for (const std::string id : {"soft_3a_vest", "fbi_armor_vest"}) {
        CAPTURE(id);
        detached_ptr<item> carrier = item::spawn(itype_id(id));
        CHECK(carrier->get_coverage(torso, sub_bodypart_str_id("torso_upper")) == 100);
        CHECK(carrier->get_coverage(torso, sub_bodypart_str_id("torso_lower")) == 80);
        // CDF: weight 2650 g, volume 6 L (JSON)
        CHECK(carrier->weight() == 2650_gram);
        CHECK(carrier->volume() == 6_liter);
    }
    detached_ptr<item> stab = item::spawn(itype_id("stab_vest"));
    CHECK(stab->get_coverage(torso) == 100);
    // CDF: weight 1814 g, volume 6 L (JSON)
    CHECK(stab->weight() == 1814_gram);
    CHECK(stab->volume() == 6_liter);
}

TEST_CASE("ar5_eod_jackets_cover_arms_and_take_their_plates", "[armor][ablative][ar5]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    const bodypart_id torso("torso");

    detached_ptr<item> jacket = item::spawn(itype_id("jacket_eod"));
    REQUIRE(jacket->find_armor_data() != nullptr);
    CHECK(jacket->covers(torso));
    CHECK(jacket->covers(bodypart_id("arm_l")));
    CHECK(jacket->covers(bodypart_id("arm_r")));
    CHECK_FALSE(jacket->covers(bodypart_id("leg_l")));
    CHECK(ablative::has_plate_slots(*jacket));
    CHECK(put_plate(*jacket, "eod_plate"));
    CHECK_FALSE(put_plate(*jacket, "eod_plate"));
    CHECK(plate_ids(*jacket) == std::vector<std::string>{"eod_plate"});
    // CDF: encumbrance [71, 80] in the JSON: the empty jacket is 71, the full one 80
    detached_ptr<item> empty_jacket = item::spawn(itype_id("jacket_eod"));
    CHECK(empty_jacket->get_avg_encumber(guy) == 71);
    CHECK(empty_jacket->get_avg_encumber(guy, item::encumber_flags::assume_full) == 80);
    // CDF: weight 13500 g, volume 15 L (JSON); the DDA max_worn field is dropped
    CHECK(empty_jacket->weight() == 13500_gram);
    CHECK(empty_jacket->volume() == 15_liter);
    // the deltoid plates do not go into the EOD jacket's one large slot
    CHECK_FALSE(put_plate(*empty_jacket, "deltoid_plates"));

    detached_ptr<item> light = item::spawn(itype_id("jacket_eod_light"));
    REQUIRE(light->find_armor_data() != nullptr);
    CHECK(light->covers(torso));
    CHECK(light->covers(bodypart_id("arm_l")));
    CHECK(light->covers(bodypart_id("arm_r")));
    CHECK(put_plate(*light, "esapi_plate"));
    CHECK(put_plate(*light, "deltoid_plates"));
    CHECK_FALSE(put_plate(*light, "esapi_plate"));
    CHECK_FALSE(put_plate(*light, "deltoid_plates"));
    CHECK(plate_ids(*light).size() == 2);
    // CDF: encumbrance [31, 55] in the JSON
    detached_ptr<item> empty_light = item::spawn(itype_id("jacket_eod_light"));
    CHECK(empty_light->get_avg_encumber(guy) == 31);
    CHECK(empty_light->get_avg_encumber(guy, item::encumber_flags::assume_full) == 55);
    // CDF: weight 7000 g, volume 15 L (JSON)
    CHECK(empty_light->weight() == 7000_gram);
    CHECK(empty_light->volume() == 15_liter);
}

TEST_CASE("ar5_all_eleven_items_exist", "[armor][ablative][ar5]") {
    clear_all_state();
    for (const std::string id : {"level_3_vest", "stab_plate", "eod_plate", "ballistic_vest_light",
                                 "ballistic_vest_light_operator", "ballistic_vest_light_pouches",
                                 "soft_3a_vest", "stab_vest", "fbi_armor_vest", "jacket_eod",
                                 "jacket_eod_light"}) {
        CAPTURE(id);
        REQUIRE(itype_id(id).is_valid());
        detached_ptr<item> it = item::spawn(itype_id(id));
        CHECK(it->find_armor_data() != nullptr);
    }
    // AR5-8 (orchestrator correction 2026-10-29): the two XL vests and the Xedra plate are ported too
    for (const std::string id : {"xl_ballistic_vest_lc", "xl_ballistic_vest_qt", "lead_plate_xedra_mark"}) {
        CAPTURE(id);
        REQUIRE(itype_id(id).is_valid());
        detached_ptr<item> it = item::spawn(itype_id(id));
        CHECK(it->find_armor_data() != nullptr);
    }
}

TEST_CASE("ar5_xl_vests_have_fixed_plates_and_no_slots", "[armor][ablative][ar5]") {
    clear_all_state();
    const bodypart_id torso("torso");
    detached_ptr<item> soft = item::spawn(itype_id("xl_ballistic_vest"));
    detached_ptr<item> lc = item::spawn(itype_id("xl_ballistic_vest_lc"));
    detached_ptr<item> qt = item::spawn(itype_id("xl_ballistic_vest_qt"));
    REQUIRE(lc->find_armor_data() != nullptr);
    REQUIRE(qt->find_armor_data() != nullptr);
    for (item *vest : {lc.get(), qt.get()}) {
        // the plates are fixed into the vest: no slot takes another plate
        CHECK_FALSE(ablative::has_plate_slots(*vest));
        CHECK_FALSE(put_plate(*vest, "esapi_plate"));
        CHECK(ablative::plates(*vest).empty());
        CHECK(vest->has_flag(flag_id("OVERSIZE")));
        CHECK(vest->has_flag(flag_id("STURDY")));
        CHECK(vest->get_coverage(torso) == 100);
        // CDF: 9 L, torso only (JSON)
        CHECK(vest->volume() == 9_liter);
        CHECK(vest->covers(torso));
        CHECK_FALSE(vest->covers(bodypart_id("arm_l")));
    }
    // CDF: weights 12500 g and 18000 g, encumbrance 14 and 20 (JSON)
    CHECK(lc->weight() == 12500_gram);
    CHECK(qt->weight() == 18000_gram);
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    CHECK(lc->get_avg_encumber(guy) == 14);
    CHECK(qt->get_avg_encumber(guy) == 20);
    // CDF: materials mild steel -> steel, tempered steel -> superalloy, with the kevlar of the soft XL vest
    CHECK(lc->made_of(material_id("kevlar")));
    CHECK(lc->made_of(material_id("steel")));
    CHECK_FALSE(lc->made_of(material_id("superalloy")));
    CHECK(qt->made_of(material_id("kevlar")));
    CHECK(qt->made_of(material_id("superalloy")));
    CHECK_FALSE(qt->made_of(material_id("steel")));
    // the steel plates make them protect more than the soft XL vest, tempered more than mild
    CHECK(soft->bullet_resist() < lc->bullet_resist());
    CHECK(lc->bullet_resist() < qt->bullet_resist());
}

TEST_CASE("ar5_lead_plate_xedra_mark_fits_large_slots", "[armor][ablative][ar5]") {
    clear_all_state();
    detached_ptr<item> plate = item::spawn(itype_id("lead_plate_xedra_mark"));
    REQUIRE(plate->find_armor_data() != nullptr);
    CHECK(plate->has_flag(flag_id("ABLATIVE_LARGE")));
    CHECK(plate->has_flag(flag_id("CANT_WEAR")));
    CHECK(plate->has_flag(flag_id("STURDY")));
    CHECK(plate->made_of(material_id("lead")));
    CHECK(plate->made_of(material_id("superalloy")));
    // CDF: weight 10545 g, volume 1110 ml, 46 cm (JSON)
    CHECK(plate->weight() == 10545_gram);
    CHECK(plate->volume() == 1110_ml);
    CHECK(plate->length() == 46_cm);
    CHECK(plate->get_coverage(bodypart_id("torso"), sub_bodypart_str_id("torso_upper")) == 45);
    CHECK(plate->get_coverage(bodypart_id("torso"), sub_bodypart_str_id("torso_lower")) == 45);

    // CDF: the plate weighs 10545 g, over the 5 kg max_contains_weight of the level_3_vest slots (same
    // numbers in DDA), so that vest refuses it; the EOD jacket's large slot (JSON) takes it
    detached_ptr<item> vest = item::spawn(itype_id("level_3_vest"));
    CHECK_FALSE(put_plate(*vest, "lead_plate_xedra_mark"));
    CHECK(ablative::plates(*vest).empty());
    detached_ptr<item> jacket = item::spawn(itype_id("jacket_eod"));
    CHECK(put_plate(*jacket, "lead_plate_xedra_mark"));
    CHECK_FALSE(put_plate(*jacket, "lead_plate_xedra_mark"));
    CHECK(plate_ids(*jacket) == std::vector<std::string>{"lead_plate_xedra_mark"});
    // a large plate in the light jacket's large slot next to deltoid plates in its medium one
    detached_ptr<item> deltoid = item::spawn(itype_id("jacket_eod_light"));
    CHECK(put_plate(*deltoid, "lead_plate_xedra_mark"));
    CHECK(put_plate(*deltoid, "deltoid_plates"));
    CHECK(plate_ids(*deltoid).size() == 2);
    detached_ptr<item> soft = item::spawn(itype_id("soft_3a_vest"));
    CHECK_FALSE(put_plate(*soft, "lead_plate_xedra_mark"));
}

TEST_CASE("ar5_spawn_groups_hold_the_new_ids", "[armor][ablative][ar5]") {
    clear_all_state();
    CHECK(item_group::group_contains_item(item_group_id("coats_unisex"), itype_id("soft_3a_vest")));
    CHECK(item_group::group_contains_item(item_group_id("coats_unisex"), itype_id("stab_vest")));
    CHECK(item_group::group_contains_item(item_group_id("allsporting"), itype_id("stab_vest")));
    const auto starts_with = [](const char *prof_id, const char *item_id) {
        const profession &prof = string_id<profession>(prof_id).obj();
        for (detached_ptr<item> &it : prof.items(true, {})) {
            if (it->typeId() == itype_id(item_id)) {
                return true;
            }
        }
        return false;
    };
    CHECK(starts_with("mall_cop", "stab_vest"));
    CHECK(starts_with("security", "soft_3a_vest"));
    // each profession gets only its own vest
    CHECK_FALSE(starts_with("mall_cop", "soft_3a_vest"));
    CHECK_FALSE(starts_with("security", "stab_vest"));
}
