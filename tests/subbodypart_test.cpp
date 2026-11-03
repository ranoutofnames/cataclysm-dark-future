#include "ablative_coverage.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "damage.h"
#include "item.h"
#include "itype.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

// Expected DDA ids and max_coverage for the 24 primary sub-locations.
const std::map<std::string, int> expected_sub_parts = {
    { "torso_upper", 60 }, { "torso_lower", 40 },
    { "arm_shoulder_l", 20 }, { "arm_upper_l", 40 }, { "arm_elbow_l", 5 }, { "arm_lower_l", 35 },
    { "arm_shoulder_r", 20 }, { "arm_upper_r", 40 }, { "arm_elbow_r", 5 }, { "arm_lower_r", 35 },
    { "leg_hip_l", 20 }, { "leg_upper_l", 40 }, { "leg_knee_l", 5 }, { "leg_lower_l", 35 },
    { "leg_hip_r", 20 }, { "leg_upper_r", 40 }, { "leg_knee_r", 5 }, { "leg_lower_r", 35 },
    { "head_forehead", 15 }, { "head_crown", 25 }, { "head_nape", 20 }, { "head_throat", 20 },
    { "head_ear_l", 10 }, { "head_ear_r", 10 },
};

} // namespace

TEST_CASE("sub body parts load with DDA's ids and coverage", "[armor][subpart]") {
    REQUIRE(expected_sub_parts.size() == 24);
    for (const auto &[id, coverage] : expected_sub_parts) {
        CAPTURE(id);
        const sub_bodypart_str_id sbp(id);
        REQUIRE(sbp.is_valid());
        CHECK(sbp->max_coverage == coverage);
        CHECK_FALSE(sbp->secondary);
        // the parent lists the part
        const std::vector<sub_bodypart_str_id> &listed = sbp->parent->sub_parts;
        CHECK(std::ranges::find(listed, sbp) != listed.end());
        CHECK(sbp->opposite.is_valid());
    }
    // primary sub-parts of a body part add up to 100
    for (const char *bp : { "torso", "head", "arm_l", "arm_r", "leg_l", "leg_r" }) {
        CAPTURE(bp);
        int total = 0;
        for (const sub_bodypart_str_id &sbp : bodypart_str_id(bp)->sub_parts) {
            // CDF: the primary locations add up to 100; secondary ones are a pool of their own
            if (!sbp->secondary) {
                total += sbp->max_coverage;
            }
        }
        CHECK(total == 100);
    }
    // parts without sub-locations have none and roll the null id
    CHECK(bodypart_str_id("hand_l")->sub_parts.empty());
    CHECK(bodypart_str_id("hand_l")->random_sub_part().is_null());
}

TEST_CASE("random_sub_part follows max_coverage", "[armor][subpart]") {
    constexpr int rolls = 100000;
    const auto distribution = [&]( const char *bp ) {
        std::map<std::string, int> counts;
        for (int i = 0; i < rolls; ++i) {
            counts[bodypart_str_id(bp)->random_sub_part().str()]++;
        }
        return counts;
    };
    const auto check = [&]( const char *bp ) {
        CAPTURE(bp);
        const std::map<std::string, int> counts = distribution(bp);
        for (const sub_bodypart_str_id &sbp : bodypart_str_id(bp)->sub_parts) {
            // CDF: random_sub_part() rolls the primary pool; secondary ones have their own test
            if (sbp->secondary) {
                continue;
            }
            CAPTURE(sbp.str());
            const auto found = counts.find(sbp.str());
            const double share = found == counts.end() ? 0.0 :
                                 100.0 * found->second / rolls;
            CHECK(share == Approx(sbp->max_coverage).margin(1.0));
        }
    };
    check("torso");
    check("head");
    check("arm_r");
    check("leg_l");
}

TEST_CASE("body part coverage is derived from specifically_covers", "[armor][subpart]") {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    const bodypart_id torso("torso");

    SECTION("a vest with upper 100 and lower 80 covers the torso at 92") {
        detached_ptr<item> vest = item::spawn(itype_id("test_sub_vest"));
        CHECK(vest->has_sub_coverage());
        CHECK(vest->covers(torso));
        CHECK(vest->get_coverage(torso) == 92);
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_upper")) == 100);
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_lower")) == 80);
        // encumbrance of the portions is summed (DDA)
        CHECK(vest->get_encumber(guy, torso) == 6);
        CHECK(vest->covers(sub_bodypart_str_id("torso_upper")));
        CHECK(vest->covers(sub_bodypart_str_id("torso_lower")));
        CHECK_FALSE(vest->covers(sub_bodypart_str_id("arm_upper_l")));
    }
    SECTION("a vest covering only the upper torso covers it at 60") {
        detached_ptr<item> vest = item::spawn(itype_id("test_sub_upper_vest"));
        CHECK(vest->get_coverage(torso) == 60);
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_upper")) == 100);
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_lower")) == 0);
        CHECK(vest->covers(sub_bodypart_str_id("torso_upper")));
        CHECK_FALSE(vest->covers(sub_bodypart_str_id("torso_lower")));
    }
    SECTION("a sub-location named by two entries takes the higher coverage, in both views") {
        detached_ptr<item> vest = item::spawn(itype_id("test_sub_overlap_vest"));
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_upper")) == 100);
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_lower")) == 50);
        // 0.6 * 100 + 0.4 * 50, not the sum of the two entries (which would be capped at 100)
        CHECK(vest->get_coverage(torso) == 80);
    }
    SECTION("items without sub data return the body part coverage for every sub-location") {
        detached_ptr<item> vest = item::spawn(itype_id("test_plain_vest"));
        CHECK_FALSE(vest->has_sub_coverage());
        CHECK(vest->get_coverage(torso) == 50);
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_upper")) == 50);
        CHECK(vest->get_coverage(torso, sub_bodypart_str_id("torso_lower")) == 50);
        CHECK(vest->covers(sub_bodypart_str_id("torso_lower")));
        CHECK_FALSE(vest->covers(sub_bodypart_str_id("arm_upper_l")));
    }
}

namespace {

// Fraction of `hits` torso hits that a worn `id` stops (damage is reduced), 0..1.
auto torso_stop_rate(const std::string &id, int hits) -> double {
    clear_all_state();
    Character &guy = get_player_character();
    clear_character(*guy.as_player(), true);
    guy.wear_item(item::spawn(id), false);
    const bodypart_id torso("torso");
    int stopped = 0;
    for (int i = 0; i < hits; ++i) {
        damage_instance dmg(damage_type::DT_CUT, 10);
        guy.absorb_hit(torso, dmg);
        if (dmg.damage_units[0].amount < 10.0f) {
            ++stopped;
        }
    }
    return static_cast<double>(stopped) / hits;
}

} // namespace

TEST_CASE("sub-location hit roll", "[armor][subpart]") {
    constexpr int hits = 10000;

    SECTION("a vest covering only the upper torso stops about 60% of torso hits") {
        CHECK(torso_stop_rate("test_sub_upper_vest", hits) == Approx(0.60).margin(0.03));
    }
    SECTION("upper 100 and lower 80 stops about 92%") {
        CHECK(torso_stop_rate("test_sub_vest", hits) == Approx(0.92).margin(0.03));
    }
    SECTION("legacy armor keeps its own independent roll") {
        CHECK(torso_stop_rate("test_plain_vest", hits) == Approx(0.50).margin(0.03));
    }
    SECTION("the two-argument and the context absorb_hit agree") {
        clear_all_state();
        Character &guy = get_player_character();
        clear_character(*guy.as_player(), true);
        guy.wear_item(item::spawn("test_sub_vest"), false);
        const bodypart_id torso("torso");
        int with_ctx = 0;
        int without = 0;
        for (int i = 0; i < hits; ++i) {
            damage_instance a(damage_type::DT_CUT, 10);
            guy.absorb_hit(torso, a, attack_context());
            with_ctx += a.damage_units[0].amount < 10.0f ? 1 : 0;
            damage_instance b(damage_type::DT_CUT, 10);
            guy.absorb_hit(torso, b);
            without += b.damage_units[0].amount < 10.0f ? 1 : 0;
        }
        CHECK(static_cast<double>(with_ctx) / hits == Approx(0.92).margin(0.03));
        CHECK(static_cast<double>(without) / hits == Approx(0.92).margin(0.03));
    }
}
