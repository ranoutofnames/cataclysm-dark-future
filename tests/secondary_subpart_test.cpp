#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "damage.h"
#include "item.h"
#include "itype.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

struct secondary_expect {
    int max_coverage;
    const char *parent;
    std::vector<std::string> under;
};

// DDA: data/json/body_parts.json:921-999 (max_coverage, parent, locations_under)
const std::map<std::string, secondary_expect> expected_secondary = {
    { "torso_neck", { 10, "torso", { "torso_upper" } } },
    { "torso_hanging_front", { 35, "torso", { "torso_upper", "torso_lower" } } },
    { "torso_hanging_back", { 45, "torso", { "torso_upper", "torso_lower" } } },
    { "torso_waist", { 10, "torso", { "leg_hip_r", "leg_hip_l" } } },
    { "leg_draped_l", { 100, "leg_l", { "leg_hip_l", "leg_upper_l" } } },
    { "leg_draped_r", { 100, "leg_r", { "leg_hip_r", "leg_upper_r" } } },
};

} // namespace

TEST_CASE( "secondary sub body parts load with DDA's ids and coverage", "[armor][subpart]" )
{
    REQUIRE( expected_secondary.size() == 6 );
    for( const auto &[id, expect] : expected_secondary ) {
        CAPTURE( id );
        const sub_bodypart_str_id sbp( id );
        REQUIRE( sbp.is_valid() );
        CHECK( sbp->secondary );
        CHECK( sbp->max_coverage == expect.max_coverage );
        CHECK( sbp->parent.str() == expect.parent );
        // the parent lists the part
        const std::vector<sub_bodypart_str_id> &listed = sbp->parent->sub_parts;
        CHECK( std::ranges::find( listed, sbp ) != listed.end() );
        std::vector<std::string> under;
        for( const sub_bodypart_str_id &u : sbp->locations_under ) {
            under.push_back( u.str() );
        }
        CHECK( under == expect.under );
    }
    // the secondary locations of the torso add up to 100
    int total = 0;
    for( const sub_bodypart_str_id &sbp : bodypart_str_id( "torso" )->sub_parts ) {
        total += sbp->secondary ? sbp->max_coverage : 0;
    }
    CHECK( total == 100 );
}

TEST_CASE( "random_sub_part picks within the primary or the secondary pool", "[armor][subpart]" )
{
    constexpr int rolls = 100000;
    const auto distribution = [&]( const char *bp, bool secondary ) {
        std::map<std::string, int> counts;
        for( int i = 0; i < rolls; ++i ) {
            counts[bodypart_str_id( bp )->random_sub_part( secondary ).str()]++;
        }
        return counts;
    };
    const auto share = []( const std::map<std::string, int> &counts, const std::string & id ) {
        const auto found = counts.find( id );
        return found == counts.end() ? 0.0 : 100.0 * found->second / rolls;
    };

    SECTION( "torso, primary pool" ) {
        const std::map<std::string, int> counts = distribution( "torso", false );
        CHECK( counts.size() == 2 );
        // CDF: torso_upper 60, torso_lower 40 (data/json/body_parts_sub.json)
        CHECK( share( counts, "torso_upper" ) == Approx( 60.0 ).margin( 2.0 ) );
        CHECK( share( counts, "torso_lower" ) == Approx( 40.0 ).margin( 2.0 ) );
        for( const auto &[id, expect] : expected_secondary ) {
            CAPTURE( id );
            CHECK( counts.count( id ) == 0 );
        }
    }
    SECTION( "torso, secondary pool" ) {
        const std::map<std::string, int> counts = distribution( "torso", true );
        CHECK( counts.size() == 4 );
        // DDA: body_parts.json:921-999
        CHECK( share( counts, "torso_neck" ) == Approx( 10.0 ).margin( 2.0 ) );
        CHECK( share( counts, "torso_hanging_front" ) == Approx( 35.0 ).margin( 2.0 ) );
        CHECK( share( counts, "torso_hanging_back" ) == Approx( 45.0 ).margin( 2.0 ) );
        CHECK( share( counts, "torso_waist" ) == Approx( 10.0 ).margin( 2.0 ) );
    }
    SECTION( "a leg has one secondary location" ) {
        const std::map<std::string, int> counts = distribution( "leg_l", true );
        CHECK( counts.size() == 1 );
        CHECK( share( counts, "leg_draped_l" ) == Approx( 100.0 ) );
        const std::map<std::string, int> primary = distribution( "leg_l", false );
        CHECK( primary.count( "leg_draped_l" ) == 0 );
    }
    SECTION( "a part without sub-locations rolls the null id in both pools" ) {
        // CDF: HAND1 gave the hands theirs, so the appendix stands in
        CHECK( bodypart_str_id( "num_bp" )->random_sub_part( false ).is_null() );
        CHECK( bodypart_str_id( "num_bp" )->random_sub_part( true ).is_null() );
        // arms have no secondary location
        CHECK( bodypart_str_id( "arm_l" )->random_sub_part( true ).is_null() );
    }
}

TEST_CASE( "flat garments cover only primary sub-locations", "[armor][subpart]" )
{
    clear_all_state();
    detached_ptr<item> shirt = item::spawn( itype_id( "tshirt" ) );
    CHECK_FALSE( shirt->has_sub_coverage() );
    const std::vector<sub_bodypart_id> covered = shirt->get_covered_sub_body_parts();
    std::set<std::string> ids;
    for( const sub_bodypart_id &sbp : covered ) {
        ids.insert( sbp->id.str() );
    }
    CHECK( ids.count( "torso_upper" ) == 1 );
    CHECK( ids.count( "torso_lower" ) == 1 );
    for( const auto &[id, expect] : expected_secondary ) {
        CAPTURE( id );
        CHECK( ids.count( id ) == 0 );
    }
}

// ---- T4-T6: red until the sums (step 4) and the hit roll (step 5) ----

TEST_CASE( "secondary coverage is not weighted by max_coverage", "[armor][subpart]" )
{
    clear_all_state();
    const bodypart_id torso( "torso" );
    SECTION( "a pack naming only a secondary location covers the torso at its coverage" ) {
        detached_ptr<item> pack = item::spawn( itype_id( "test_hanging_pack" ) );
        CHECK( pack->has_sub_coverage() );
        // CDF: fixture coverage 30, torso_hanging_back is 45 of 100, but the best coverage is
        // taken unweighted for a secondary-only portion
        CHECK( pack->get_coverage( torso ) == 30 );
        CHECK( pack->get_coverage( torso, sub_bodypart_str_id( "torso_hanging_back" ) ) == 30 );
        CHECK( pack->get_coverage( torso, sub_bodypart_str_id( "torso_upper" ) ) == 0 );
    }
    SECTION( "a mixed vest keeps the primary weighted value" ) {
        detached_ptr<item> vest = item::spawn( itype_id( "test_mixed_vest" ) );
        // CDF: upper 100 * 60 / 100; the waist does not enter the sum
        CHECK( vest->get_coverage( torso ) == 60 );
        CHECK( vest->get_coverage( torso, sub_bodypart_str_id( "torso_waist" ) ) == 50 );
        CHECK( vest->get_coverage( torso, sub_bodypart_str_id( "torso_upper" ) ) == 100 );
    }
}

namespace
{

// Fraction of `hits` torso hits of 10 cut that are reduced by the worn `id` alone.
auto torso_reduced_rate( const std::string &id, int hits ) -> double
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    guy.wear_item( item::spawn( id ), false );
    const bodypart_id torso( "torso" );
    int reduced = 0;
    for( int i = 0; i < hits; ++i ) {
        damage_instance dmg( damage_type::DT_CUT, 10 );
        guy.absorb_hit( torso, dmg );
        if( dmg.damage_units[0].amount < 10.0f ) {
            ++reduced;
        }
    }
    return static_cast<double>( reduced ) / hits;
}

} // namespace

TEST_CASE( "a torso hit also rolls the secondary location", "[armor][subpart]" )
{
    constexpr int hits = 10000;
    SECTION( "a full-coverage pack takes the hit when the back location is rolled" ) {
        // DDA: torso_hanging_back is 45 of the secondary pool
        CHECK( torso_reduced_rate( "test_hanging_pack_full", hits ) == Approx( 0.45 ).margin( 0.04 ) );
    }
    SECTION( "a 30% pack takes about 13.5%" ) {
        CHECK( torso_reduced_rate( "test_hanging_pack", hits ) == Approx( 0.135 ).margin( 0.03 ) );
    }
    SECTION( "a flat vest alone absorbs exactly once per hit" ) {
        clear_all_state();
        Character &guy = get_player_character();
        clear_character( *guy.as_player(), true );
        guy.wear_item( item::spawn( "test_plain_vest" ), false );
        const bodypart_id torso( "torso" );
        std::set<float> outcomes;
        for( int i = 0; i < 2000; ++i ) {
            // CDF: 100 cut, so that the vest's resistance leaves a remainder that a second pass
            // would take off again (10 cut is absorbed to 0 by one pass and hides a second one)
            damage_instance dmg( damage_type::DT_CUT, 100 );
            guy.absorb_hit( torso, dmg );
            outcomes.insert( dmg.damage_units[0].amount );
        }
        // unabsorbed or absorbed once; a second pass at the secondary location would take off twice
        // both passes share one coverage roll, so a second absorb shows only in the amount:
        // a hit takes off the vest's cut resistance once (CDF: test_plain_vest, plastic thickness 2)
        const float resist = static_cast<float>( item::spawn( "test_plain_vest" )->damage_resist( DT_CUT ) );
        REQUIRE( resist > 0.0f );
        CHECK( outcomes == std::set<float>{ 100.0f, 100.0f - resist } );
        CHECK( torso_reduced_rate( "test_plain_vest", hits ) == Approx( 0.50 ).margin( 0.03 ) );
    }
}

TEST_CASE( "a plain torso portion on an item with sub-portions elsewhere absorbs once per hit",
           "[armor][subpart]" )
{
    // CDF: armor_riot_torso has a plain torso portion and sub-portions on the limbs; the secondary
    // pass must not test its torso a second time
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const std::string id = "test_plain_torso_sub_arms";
    guy.wear_item( item::spawn( id ), false );
    const bodypart_id torso( "torso" );
    std::set<float> outcomes;
    for( int i = 0; i < 2000; ++i ) {
        damage_instance dmg( damage_type::DT_CUT, 100 );
        guy.absorb_hit( torso, dmg );
        outcomes.insert( dmg.damage_units[0].amount );
    }
    const float resist = static_cast<float>( item::spawn( id )->damage_resist( DT_CUT ) );
    REQUIRE( resist > 0.0f );
    CHECK( outcomes == std::set<float>{ 100.0f, 100.0f - resist } );
    CHECK( torso_reduced_rate( id, 10000 ) == Approx( 0.50 ).margin( 0.03 ) );
}
