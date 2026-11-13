// CDF: P3-6-6a. CDF-only (DDA's select_ammo is silent). The four messages that only the BN reload
// menu printed ("Never mind.", "You need a compatible magazine to reload the %s!", "Nothing to
// reload!", "You don't have any %s to reload your %s!") live in reload_selection::report, and
// game_menus::inv::select_ammo calls it. Messages is stubbed in the test binary
// (tests/fake_messages.cpp), so the text is read through a standalone avatar whose
// add_msg_if_player records what it is given (player::add_msg_if_player is virtual).
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "game_inventory.h"
#include "item.h"
#include "map_helpers.h"
#include "messages.h"
#include "player_helpers.h"
#include "reload/reload.h"
#include "reload/reload_selection.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>
#include <vector>

namespace {
using reload_selection::selection_outcome;

class recording_avatar : public avatar {
public:
    using avatar::add_msg_if_player;
    void add_msg_if_player(const std::string& msg) const override { log.push_back(msg); }
    void add_msg_if_player(const game_message_params&, const std::string& msg) const override {
        log.push_back(msg);
    }
    mutable std::vector<std::string> log;
};

auto has(const std::string& text, const std::string& part) -> bool {
    return text.find(part) != std::string::npos;
}

auto outcome_name(selection_outcome outcome) -> std::string {
    switch (outcome) {
        case selection_outcome::automatic: return "automatic";
        case selection_outcome::interaction_required: return "interaction_required";
        case selection_outcome::empty_supplied: return "empty_supplied";
        case selection_outcome::missing_magazine: return "missing_magazine";
        case selection_outcome::nothing_to_reload: return "nothing_to_reload";
        case selection_outcome::missing_ammunition: return "missing_ammunition";
    }
    return "?";
}

// Fill the gun and every gunmod magazine pocket with `ammo`
auto fill_gun(item& gun, const std::string& ammo) -> void {
    gun.ammo_set(itype_id(ammo), -1);
    for (item* mod : gun.gunmods()) {
        if (mod->ammo_capacity() > 0) { mod->ammo_set(itype_id(ammo), -1); }
    }
}
} // namespace

// Z1
TEST_CASE("report_missing_magazine_message", "[reload_selection][p3]") {
    clear_all_state();
    auto& who = get_avatar();
    // CDF: glock_19 has a MAGAZINE_WELL, so with no magazine anywhere it needs one first
    auto& gun = *item::spawn_temporary("glock_19", calendar::start_of_cataclysm, 0);
    const auto result = reload_selection::prepare(who, gun);
    CAPTURE(outcome_name(result.outcome));
    REQUIRE(result.outcome == selection_outcome::missing_magazine);

    auto recorder = recording_avatar{};
    reload_selection::report(recorder, gun, result.outcome);
    INFO("messages printed: " << recorder.log.size());
    REQUIRE(recorder.log.size() == 1);
    CHECK(has(recorder.log.front(), "You need a compatible magazine to reload the"));
    CHECK(has(recorder.log.front(), gun.tname()));
}

// Z2
TEST_CASE("report_nothing_to_reload_and_missing_ammunition", "[reload_selection][p3]") {
    clear_all_state();
    auto& who = get_avatar();

    SECTION("a full gun with matching ammunition around has nothing to reload") {
        auto& ammo = who.i_add(item::spawn("38_special", calendar::start_of_cataclysm, 6));
        auto& gun = who.i_add(item::spawn("sw_619", calendar::start_of_cataclysm, 0));
        gun.ammo_set(ammo.typeId(), -1);
        REQUIRE(gun.ammo_remaining() == gun.ammo_capacity());

        const auto result = reload_selection::prepare(who, gun);
        CAPTURE(outcome_name(result.outcome));
        REQUIRE(result.outcome == selection_outcome::nothing_to_reload);

        auto recorder = recording_avatar{};
        reload_selection::report(recorder, gun, result.outcome);
        REQUIRE(recorder.log.size() == 1);
        CHECK(has(recorder.log.front(), "Nothing to reload!"));
        CHECK_FALSE(has(recorder.log.front(), "You don't have any"));
    }

    SECTION("the same gun with no ammunition around names the ammunition") {
        auto& gun = who.i_add(item::spawn("sw_619", calendar::start_of_cataclysm, 0));
        const auto result = reload_selection::prepare(who, gun);
        CAPTURE(outcome_name(result.outcome));
        REQUIRE(result.outcome == selection_outcome::missing_ammunition);

        auto recorder = recording_avatar{};
        reload_selection::report(recorder, gun, result.outcome);
        REQUIRE(recorder.log.size() == 1);
        CHECK(has(recorder.log.front(), "You don't have any"));
        // CDF: sw_619 takes the ammo types "38" (.38) and "357mag" (.357 magnum), ammo_types.json
        CHECK(has(recorder.log.front(), ".38"));
        CHECK(has(recorder.log.front(), "to reload your"));
        CHECK(has(recorder.log.front(), gun.tname()));
        CHECK_FALSE(has(recorder.log.front(), "Nothing to reload!"));
    }

    SECTION("an outcome that is not a failure prints nothing") {
        auto& gun = *item::spawn_temporary("sw_619", calendar::start_of_cataclysm, 0);
        auto recorder = recording_avatar{};
        reload_selection::report(recorder, gun, selection_outcome::automatic);
        reload_selection::report(recorder, gun, selection_outcome::interaction_required);
        CHECK(recorder.log.empty());
    }

    SECTION("an empty supplied list says never mind") {
        auto& gun = *item::spawn_temporary("sw_619", calendar::start_of_cataclysm, 0);
        auto recorder = recording_avatar{};
        reload_selection::report(recorder, gun, selection_outcome::empty_supplied);
        REQUIRE(recorder.log.size() == 1);
        CHECK(recorder.log.front() == "Never mind.");
    }
}

// Z3
TEST_CASE("select_ammo_empty_list_explains_itself", "[reload_selection][p3]") {
    clear_all_state();
    // CDF: nothing carried and nothing on the ground, so the ammo selector has no entry and
    // select_ammo takes the empty-list path, which must name what is missing (the bare popup
    // "You have nothing to reload." says nothing about why). In the test build the popup returns
    // at once (popup.cpp:269), so the path runs headless.
    auto recorder = recording_avatar{};
    auto& gun = *item::spawn_temporary("sw_619", calendar::start_of_cataclysm, 0);
    const auto option = game_menus::inv::select_ammo(recorder, gun);
    CHECK_FALSE(option);
    INFO("messages printed: " << recorder.log.size());
    REQUIRE(recorder.log.size() == 1);
    CHECK(has(recorder.log.front(), "You don't have any"));
    CHECK(has(recorder.log.front(), gun.tname()));
}

// Z5
TEST_CASE("report_integral_magazine_guns", "[reload_selection][p3]") {
    clear_all_state();
    auto& who = get_avatar();
    // CDF: the three converted integral-magazine guns (one MAGAZINE pocket each, clip_size 7):
    // data/json/items/gun/38.json (sw_619), 4570.json (1895sbl), shot.json (ksg). Ammunition ids
    // are the ammo_types.json defaults of "38", "4570" and "shot". The ksg also has the
    // ksg_aux_shotgun built-in mod with its own magazine pocket (underbarrel.json:191), so it
    // offers two targets for one box of shells.
    struct integral_gun {
        std::string gun;
        std::string ammo;
        std::string ammo_name;
        selection_outcome loaded_outcome;
    };
    const auto guns = std::vector<integral_gun>{
        {"sw_619", "38_special", ".38", selection_outcome::automatic},
        {"1895sbl", "4570_sp", ".45-70", selection_outcome::automatic},
        {"ksg", "shot_00", "shot", selection_outcome::interaction_required},
    };
    REQUIRE_FALSE(guns.empty());

    for (const auto& entry : guns) {
        CAPTURE(entry.gun);
        SECTION(entry.gun + ": empty gun and no ammunition is missing ammunition, not a missing magazine") {
            clear_all_state();
            auto& gun = *item::spawn_temporary(itype_id(entry.gun), calendar::start_of_cataclysm, 0);
            REQUIRE(gun.ammo_remaining() == 0);
            const auto result = reload_selection::prepare(who, gun);
            CAPTURE(outcome_name(result.outcome));
            REQUIRE(result.outcome == selection_outcome::missing_ammunition);

            auto recorder = recording_avatar{};
            reload_selection::report(recorder, gun, result.outcome);
            REQUIRE(recorder.log.size() == 1);
            CHECK(has(recorder.log.front(), "You don't have any"));
            CHECK(has(recorder.log.front(), entry.ammo_name));
            CHECK_FALSE(has(recorder.log.front(), "compatible magazine"));
        }

        SECTION(entry.gun + ": full gun and matching ammunition is nothing to reload") {
            clear_all_state();
            who.i_add(item::spawn(itype_id(entry.ammo), calendar::start_of_cataclysm, 10));
            auto& gun = who.i_add(item::spawn(itype_id(entry.gun), calendar::start_of_cataclysm, 0));
            fill_gun(gun, entry.ammo);
            REQUIRE(gun.ammo_remaining() == gun.ammo_capacity());
            const auto result = reload_selection::prepare(who, gun);
            CAPTURE(outcome_name(result.outcome));
            CHECK(result.outcome == selection_outcome::nothing_to_reload);

            auto recorder = recording_avatar{};
            reload_selection::report(recorder, gun, result.outcome);
            REQUIRE(recorder.log.size() == 1);
            CHECK(has(recorder.log.front(), "Nothing to reload!"));
        }

        SECTION(entry.gun + ": empty gun and matching ammunition offers the ammunition") {
            clear_all_state();
            auto& ammo = who.i_add(item::spawn(itype_id(entry.ammo), calendar::start_of_cataclysm, 10));
            auto& gun = who.i_add(item::spawn(itype_id(entry.gun), calendar::start_of_cataclysm, 0));
            const auto result = reload_selection::prepare(who, gun);
            CAPTURE(outcome_name(result.outcome));
            REQUIRE(result.outcome == entry.loaded_outcome);
            if (result.outcome == selection_outcome::automatic) {
                CHECK(result.selected.ammo == &ammo);
            } else {
                REQUIRE_FALSE(result.options.empty());
                for (const auto& option : result.options) { CHECK(option.ammo == &ammo); }
            }
        }
    }
}

// Z6
TEST_CASE("report_well_gun_without_magazine_stays_missing_magazine", "[reload_selection][p3]") {
    clear_all_state();
    auto& who = get_avatar();
    // CDF: glock_19 is a MAGAZINE_WELL gun that 7-R2 did not convert to an integral pocket
    auto& gun = *item::spawn_temporary("glock_19", calendar::start_of_cataclysm, 0);
    const auto result = reload_selection::prepare(who, gun);
    CAPTURE(outcome_name(result.outcome));
    CHECK(result.outcome == selection_outcome::missing_magazine);
    CHECK_FALSE(result.selected);
    CHECK(result.options.empty());
}
