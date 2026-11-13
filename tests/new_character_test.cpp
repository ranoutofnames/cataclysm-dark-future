#include "avatar.h"
#include "catch/catch.hpp"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "item_contents.h"
#include "itype.h"
#include "json.h"
#include "mutation.h"
#include "newcharacter.h"
#include "pldata.h"
#include "profession.h"
#include "ret_val.h"
#include "scenario.h"
#include "state_helpers.h"
#include "string_id.h"
#include "type_id.h"

#include <array>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

auto reset_scenario(avatar& u, const scenario* scen) -> void;

static auto operator<<(std::ostream& s, const std::vector<trait_id>& v)
    -> std::ostream& { // *NOPAD*
    for (const auto& e : v) { s << e.c_str() << " "; }
    return s;
}

using starting_item_trait_set = std::vector<trait_id>;
using starting_item_trait_sets_t = std::vector<starting_item_trait_set>;

static auto starting_item_trait_sets(const std::vector<trait_id>& traits)
    -> starting_item_trait_sets_t {
    auto ret = starting_item_trait_sets_t{};
    for (const auto& trait : traits) { ret.push_back({trait}); }
    ret.insert(
        ret.end(),
        {
            {trait_id("ANTIWHEAT"), trait_id("MEATARIAN")},
            {trait_id("ANTIWHEAT"), trait_id("VEGETARIAN")},
            {trait_id("ANTIFRUIT"), trait_id("MEATARIAN")},
            {trait_id("ANTIFRUIT"), trait_id("ANTIJUNK")},
            {trait_id("ANTIWHEAT"), trait_id("ANTIJUNK")},
            {trait_id("LACTOSE"), trait_id("VEGETARIAN")},
            {trait_id("ANTIWHEAT"), trait_id("ANTIJUNK"), trait_id("MEATARIAN")},
        });
    return ret;
}

static auto try_set_traits(Character& ch, const std::vector<trait_id>& traits) -> bool {
    ch.clear_mutations();
    newcharacter::add_traits(ch); // mandatory prof/scen traits
    for (const auto& tr : traits) {
        if (newcharacter::has_conflicting_trait(ch, tr) || !g->scen->traitquery(tr)) {
            return false;
        } else if (!ch.has_trait(tr)) {
            ch.set_mutation(tr);
        }
    }
    return true;
}

static auto get_sanitized_player() -> avatar {
    // You'd think that this hp stuff would be in the c'tor...
    // But the ctor is called by game::game, before we load anatomies
    avatar ret = avatar();
    ret.set_body();
    ret.recalc_hp();

    // Set these to starving/parched so can_eat doesn't return TOO_FULL
    ret.set_stored_kcal(100);
    ret.set_thirst(10000);
    return ret;
}

static auto load_scenario_whitelist(const std::string& scenario_id) -> void {
    const auto json =
        std::string(R"({ "subtype": "whitelist", "scenarios": [ ")") + scenario_id + R"(" ] })";
    auto stream = std::istringstream(json);
    auto jsin = JsonIn(stream);
    auto jo = jsin.get_object();
    scen_blacklist::load_scen_blacklist(jo, "test");
    scenario::check_definitions();
}

struct scoped_scenario_blacklist_reset {
    ~scoped_scenario_blacklist_reset() {
        reset_scenarios_blacklist();
        scenario::check_definitions();
    }
};

struct failure {
    string_id<profession> prof;
    std::vector<trait_id> mut;
    itype_id item_name;
    std::string reason;
};

namespace std {
template <> struct less<failure> {
    auto operator()(const failure& lhs, const failure& rhs) const -> bool {
        // by profession, item and reason: a profession can fail with more than one item
        return std::tie(lhs.prof, lhs.item_name, lhs.reason) <
               std::tie(rhs.prof, rhs.item_name, rhs.reason);
    }
};
} // namespace std

// TODO: According to profiling (interrupt, backtrace, wait a few seconds, repeat) with a sample
// size of 20, 70% of the time is due to the call to Character::set_mutation in try_set_traits.
// When the mutation stuff isn't commented out, the test takes 110 minutes (not a typo)!

TEST_CASE("skin_tone_is_mandatory_with_default", "[new_character][traits]") {
    clear_all_state();

    const auto skin_tone = std::string("skin_tone");
    const auto skin_lighter = trait_id("SKIN_LIGHTER");

    CHECK(mutation_type_exists(skin_tone));
    CHECK(mutation_type_is_mandatory(skin_tone));
    CHECK(mutation_type_swaps_on_conflict(skin_tone));
    CHECK(mutation_type_random_chance(skin_tone) == 0);

    const auto defaults = get_default_mutations_for_types();
    CHECK(std::ranges::any_of(defaults, [&](const auto& default_mutation) {
        return default_mutation.type_id == skin_tone && default_mutation.trait == skin_lighter;
    }));

    auto ch = get_sanitized_player();
    ch.clear_mutations();
    newcharacter::add_default_mutation_type_traits(ch);

    CHECK(ch.has_base_trait(skin_lighter));
    CHECK(ch.has_trait(skin_lighter));
}

TEST_CASE("default_character_respects_scenario_whitelist", "[new_character][scenario]") {
    const auto whitelisted_scenario = string_id<scenario>("wilderness");
    [[maybe_unused]] const auto reset_blacklist = scoped_scenario_blacklist_reset{};
    load_scenario_whitelist(whitelisted_scenario.c_str());

    auto ch = get_sanitized_player();
    auto points = points_left();
    g->scen = scenario::generic();

    ch.randomize(false, points, true);

    CHECK(g->scen->ident() == whitelisted_scenario);
}

TEST_CASE("scenario_reset_preserves_or_defaults_hair_style", "[new_character][scenario][traits]") {
    clear_all_state();

    const auto* target_scenario = &string_id<scenario>("wilderness").obj();
    const auto default_hair_style = trait_id("hair_medium");
    const auto bald_hair_style = trait_id("HAIR_BALD");

    auto ch = get_sanitized_player();
    ch.male = true;
    ch.prof = profession::generic();
    g->scen = scenario::generic();

    SECTION("uses the default hair style when no hair style is selected") {
        reset_scenario(ch, target_scenario);

        CHECK(ch.has_trait(default_hair_style));
        CHECK(!ch.has_trait(bald_hair_style));
    }

    SECTION("preserves explicitly selected bald hair style") {
        ch.set_mutation(bald_hair_style);

        reset_scenario(ch, target_scenario);

        CHECK(ch.has_trait(bald_hair_style));
        CHECK(!ch.has_trait(default_hair_style));
    }

    g->scen = scenario::generic();
}

TEST_CASE("starting_items", "[slow]") {
    clear_all_state();
    // Every starting trait that interferes with food/clothing
    const std::vector<trait_id> mutations =
        {trait_id("ANTIFRUIT"), trait_id("ANTIJUNK"), trait_id("ANTIWHEAT"),
         // trait_id( "ARM_TENTACLES" ),
         // trait_id( "BEAK" ),
         trait_id("CANNIBAL"),
         // trait_id( "CARNIVORE" ),
         // trait_id( "HERBIVORE" ),
         // trait_id( "HOOVES" ),
         trait_id("LACTOSE"),
         // trait_id( "LEG_TENTACLES" ),
         trait_id("MEATARIAN"),
         // trait_id( "RAP_TALONS" ),
         // trait_id( "TAIL_FLUFFY" ),
         // trait_id( "TAIL_LONG" ),
         trait_id("VEGETARIAN"), trait_id("WOOLALLERGY")};
    // Prof/scen combinations that need to be checked.
    std::unordered_map<const scenario*, std::vector<string_id<profession>>> scen_prof_combos;
    for (const auto& id : scenario::generic()->permitted_professions()) {
        scen_prof_combos[scenario::generic()].push_back(id);
    }

    std::set<failure> failures;

    avatar& ch = get_avatar();

    ch = get_sanitized_player();

    // Avoid false positives from ingredients like salt and cornmeal.
    const avatar control = get_sanitized_player();

    for (const auto& traits : starting_item_trait_sets(mutations)) {
        for (const auto& pair : scen_prof_combos) {
            g->scen = pair.first;
            for (const auto& prof : pair.second) {
                ch.prof = prof;
                if (!try_set_traits(ch, traits)) {
                    continue; // Trait conflict: this prof/scen/trait combo is impossible to attain
                }
                for (const auto male : {true, false}) {
                    ch.worn.clear();
                    ch.reset_encumbrance();
                    ch.male = male;
                    std::vector<detached_ptr<item>> items = prof->items(ch.male, traits);
                    /*
                    for( item * const &it : items ) {
                        const std::vector<item *> it_contents = it->contents.all_items_top();
                        for( item * const &top_content_item : it_contents ) {
                            items.push_back( top_content_item );
                        }
                    }*/

                    for (detached_ptr<item>& det_it : items) {
                        item* it = &*det_it;
                        // read before the item can be moved: the label must be the item itself
                        const itype_id it_id = it->typeId();
                        const bool is_food =
                            !it->is_seed() && it->is_food() && !ch.can_eat(*it).success()
                            && control.can_eat(*it).success();
                        // RNG_SURVIVOR's kit is random (survivorzed_head x survivorzed_extra x
                        // ...), so two rigid pieces on the same sub-location can come up together
                        // (DDA's own rigid rule does the same). The real flow carries what it
                        // cannot wear, see starting_items_that_cannot_be_worn_are_carried, and
                        // DDA's starting_items does not wear items at all.
                        const bool rigid_conflict_ok = prof->ident().str() == "RNG_SURVIVOR";
                        // newcharacter::add_profession_items carries a no_auto_equip item in
                        // the kit instead of wearing it (DDA add_profession_items)
                        const bool wearable = it->is_armor()
                                              && !it->has_flag(flag_id("no_auto_equip"));
                        // An item that cannot be worn is handed back, keep it alive until the
                        // end of this iteration so it is neither destroyed nor read after free.
                        detached_ptr<item> not_worn;
                        if (wearable) {
                            not_worn = ch.wear_item(std::move(det_it), false);
                        }
                        const bool is_armor = static_cast<bool>(not_worn) && !rigid_conflict_ok;
                        // Seeds don't count- they're for growing things, not eating
                        if (is_food || is_armor) {
                            failures.insert(
                                failure{prof->ident(), ch.get_mutations(), it_id,
                                        is_food ? "Couldn't eat it" : "Couldn't wear it."});
                        }

                        // Per DDA, pouches and bandoliers also use the holster action but their
                        // pockets are not holster pockets
                        const bool is_holster =
                            it->is_armor() && it->type->get_use("holster")
                            && !it->get_pockets([](const item_pocket& pk) {
                                      return pk.is_holster();
                                  }).empty();
                        if (is_holster) {
                            const item& holstered_it = it->get_contained();
                            const bool empty_holster = holstered_it.is_null();
                            if (!empty_holster && !it->can_holster(holstered_it, true)) {
                                failures.insert(
                                    failure{prof->ident(), ch.get_mutations(), it->typeId(),
                                            "Couldn't put item back to holster"});
                            }
                        }
                    }
                } // all genders
            } // all profs
        } // all scens
    }
    std::stringstream failure_messages;
    for (const failure& f : failures) {
        failure_messages << f.prof.c_str() << " " << f.mut << " " << f.item_name.str() << ": "
                         << f.reason << "\n";
    }
    INFO(failure_messages.str());
    REQUIRE(failures.empty());
}

TEST_CASE("starting_items_that_cannot_be_worn_are_carried", "[new_character]") {
    clear_all_state();
    avatar& ch = get_avatar();
    ch = get_sanitized_player();
    g->scen = scenario::generic();

    // Two rigid helmets cannot be worn together: the second one must not be lost.
    std::vector<detached_ptr<item>> items;
    items.push_back(item::spawn("helmet_army"));
    items.push_back(item::spawn("helmet_army"));
    std::vector<detached_ptr<item>> overflow;
    newcharacter::add_profession_items(ch, std::move(items), overflow);

    int total = 0;
    int worn = 0;
    ch.visit_items([&](item* it) -> VisitResponse {
        if (it->typeId().str() == "helmet_army") {
            total++;
        }
        return VisitResponse::NEXT;
    });
    for (const item* it : ch.worn) {
        if (it->typeId().str() == "helmet_army") {
            worn++;
        }
    }
    for (const detached_ptr<item>& it : overflow) {
        if (it && it->typeId().str() == "helmet_army") {
            total++;
        }
    }
    CHECK(worn == 1);
    CHECK(total == 2);
}

// FIXD: BN's "You're already wearing footwear!" rule is gone; DDA decides by the rigid test, the
// layer and the per-type limit (DDA Character::can_wear, src/character_attire.cpp:240-275).
TEST_CASE("armor_riot_and_boots_combat_can_be_worn_together", "[armor][fixd][p3]") {
    clear_all_state();
    avatar& ch = get_avatar();
    ch = get_sanitized_player();
    g->scen = scenario::generic();

    SECTION("armor_riot then boots_combat") {
        CHECK(ch.wear_item(item::spawn("armor_riot"), false) == nullptr);
        detached_ptr<item> boots = item::spawn("boots_combat");
        const ret_val<bool> ok = ch.can_wear(*boots);
        INFO(ok.str());
        CHECK(ok.success());
        CHECK(ch.wear_item(std::move(boots), false) == nullptr);
    }
    SECTION("boots_combat then armor_riot") {
        CHECK(ch.wear_item(item::spawn("boots_combat"), false) == nullptr);
        detached_ptr<item> riot = item::spawn("armor_riot");
        const ret_val<bool> ok = ch.can_wear(*riot);
        INFO(ok.str());
        CHECK(ok.success());
        CHECK(ch.wear_item(std::move(riot), false) == nullptr);
    }
    // DDA has no "already wearing footwear" rule: two pairs of ordinary (soft) boots go on, the
    // per-type limit (MAX_WORN_PER_TYPE) refuses a third pair of the same kind.
    SECTION("ordinary boots follow the per-type limit, not a footwear rule") {
        CHECK(ch.wear_item(item::spawn("boots_combat"), false) == nullptr);
        CHECK(ch.wear_item(item::spawn("boots_combat"), false) == nullptr);
        detached_ptr<item> third = item::spawn("boots_combat");
        CHECK_FALSE(ch.can_wear(*third).success());
    }
    // Two rigid foot items still conflict (DDA rigid_test on the sub-parts).
    SECTION("two rigid boots are refused by the rigid rule") {
        CHECK(ch.wear_item(item::spawn("boots_plate"), false) == nullptr);
        detached_ptr<item> second = item::spawn("boots_steel");
        const ret_val<bool> res = ch.can_wear(*second);
        INFO(res.str());
        CHECK_FALSE(res.success());
    }
}
