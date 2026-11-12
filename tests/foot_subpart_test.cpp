// FOOT1: foot sub-locations (sole, arch, toes, ankle, heel) and the foot items converted to them.
// DDA source: data/json/body_parts.json:1296-1395 (the ten sub_body_part entries),
// items/armor/boots.json (per-sub-location portions). Expected numbers are CDF's flat foot
// coverage and encumbrance at the base (cc4d9bae9a), re-derived by rule A (DDA shape, CDF
// aggregate coverage and encumbrance), so a wrong conversion cannot agree with them by
// construction.
#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "damage.h"
#include "enums.h"
#include "item.h"
#include "itype.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "type_id.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

// CDF: DDA body_parts.json:1296-1395, the ten foot sub-locations and their max_coverage.
const std::map<std::string, int> expected_foot_sub_parts = {
    { "foot_sole_l", 25 }, { "foot_arch_l", 25 }, { "foot_toes_l", 10 },
    { "foot_ankle_l", 20 }, { "foot_heel_l", 20 },
    { "foot_sole_r", 25 }, { "foot_arch_r", 25 }, { "foot_toes_r", 10 },
    { "foot_ankle_r", 20 }, { "foot_heel_r", 20 },
};

bool covers_sub( const std::vector<sub_bodypart_id> &covered, const std::string &id )
{
    const sub_bodypart_str_id wanted( id );
    return std::ranges::any_of( covered, [&wanted]( const sub_bodypart_id & sbp ) {
        return sbp.id() == wanted;
    } );
}

} // namespace

TEST_CASE( "foot1_sub_parts_load", "[armor][foot1][p3]" )
{
    REQUIRE( expected_foot_sub_parts.size() == 10 );
    for( const auto &[id, coverage] : expected_foot_sub_parts ) {
        CAPTURE( id );
        const sub_bodypart_str_id sbp( id );
        REQUIRE( sbp.is_valid() );
        CHECK( sbp->max_coverage == coverage );
        CHECK_FALSE( sbp->secondary );
        const std::string parent = id.back() == 'l' ? "foot_l" : "foot_r";
        CHECK( sbp->parent == bodypart_str_id( parent ) );
        CHECK( sbp->opposite.is_valid() );
        CHECK_FALSE( sbp->opposite->secondary );
        // the parent lists the part (DDA body_parts.json:807, 841)
        const std::vector<sub_bodypart_str_id> &listed = sbp->parent->sub_parts;
        CHECK( std::ranges::find( listed, sbp ) != listed.end() );
    }
    // CDF: the five primary foot locations add up to 100, like the head, torso, arms and legs
    for( const char *bp : { "foot_l", "foot_r" } ) {
        CAPTURE( bp );
        int total = 0;
        for( const sub_bodypart_str_id &sbp : bodypart_str_id( bp )->sub_parts ) {
            if( !sbp->secondary ) {
                total += sbp->max_coverage;
            }
        }
        CHECK( total == 100 );
    }
    // CDF: HAND1 added the hand sub-locations; this packet's "hands have none" check moved to
    // tests/hand_subpart_test.cpp
}

TEST_CASE( "foot1_random_sub_part_distribution", "[armor][foot1][p3]" )
{
    constexpr int rolls = 100000;
    std::map<std::string, int> counts;
    for( int i = 0; i < rolls; ++i ) {
        counts[bodypart_str_id( "foot_l" )->random_sub_part().str()]++;
    }
    for( const sub_bodypart_str_id &sbp : bodypart_str_id( "foot_l" )->sub_parts ) {
        if( sbp->secondary ) {
            continue;
        }
        CAPTURE( sbp.str() );
        const double share = 100.0 * counts[sbp.str()] / rolls;
        CHECK( share == Approx( sbp->max_coverage ).margin( 1.0 ) );
    }
    // CDF: the right foot has the same five, so the left rolls never return a right id
    for( const auto &[id, n] : counts ) {
        CAPTURE( id, n );
        CHECK( id.ends_with( "_l" ) );
    }
}

TEST_CASE( "foot1_boots_cover_all_five", "[armor][foot1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    detached_ptr<item> boots = item::spawn( itype_id( "boots" ) );
    const std::vector<sub_bodypart_id> covered = boots->get_covered_sub_body_parts();
    CHECK( covered.size() == 10 );
    for( const auto &[id, coverage] : expected_foot_sub_parts ) {
        CAPTURE( id );
        CHECK( covers_sub( covered, id ) );
    }
    // CDF: boots cover the foot at 90 and encumber it 12 at the base (flat coverage 90,
    // encumbrance 12); DDA's upper and sole portions carry the same numbers here
    CHECK( boots->get_coverage( foot_l ) == 90 );
    CHECK( boots->get_coverage( foot_l, sub_bodypart_str_id( "foot_sole_l" ) ) == 90 );
    CHECK( boots->get_coverage( foot_l, sub_bodypart_str_id( "foot_toes_l" ) ) == 90 );
    CHECK( boots->get_encumber( guy, foot_l ) == 12 );
}

TEST_CASE( "foot1_open_shoes_leave_gaps", "[armor][foot1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    SECTION( "flip-flops cover the arch and the sole only" ) {
        detached_ptr<item> flip_flops = item::spawn( itype_id( "flip_flops" ) );
        CHECK( flip_flops->covers( sub_bodypart_str_id( "foot_arch_l" ) ) );
        CHECK( flip_flops->covers( sub_bodypart_str_id( "foot_sole_l" ) ) );
        CHECK_FALSE( flip_flops->covers( sub_bodypart_str_id( "foot_toes_l" ) ) );
        CHECK_FALSE( flip_flops->covers( sub_bodypart_str_id( "foot_ankle_l" ) ) );
        CHECK_FALSE( flip_flops->covers( sub_bodypart_str_id( "foot_heel_l" ) ) );
        // CDF: flat coverage 10 and encumbrance 35 at the base
        CHECK( flip_flops->get_coverage( foot_l ) == 10 );
        CHECK( flip_flops->get_encumber( guy, foot_l ) == 35 );
    }

    SECTION( "heels cover the toes, heel and sole, not the ankle or the arch" ) {
        detached_ptr<item> heels = item::spawn( itype_id( "heels" ) );
        CHECK( heels->covers( sub_bodypart_str_id( "foot_toes_l" ) ) );
        CHECK( heels->covers( sub_bodypart_str_id( "foot_heel_l" ) ) );
        CHECK( heels->covers( sub_bodypart_str_id( "foot_sole_l" ) ) );
        CHECK_FALSE( heels->covers( sub_bodypart_str_id( "foot_ankle_l" ) ) );
        CHECK_FALSE( heels->covers( sub_bodypart_str_id( "foot_arch_l" ) ) );
        // CDF: flat coverage 30 and encumbrance 50 at the base
        CHECK( heels->get_coverage( foot_l ) == 30 );
        CHECK( heels->get_encumber( guy, foot_l ) == 50 );
    }

    SECTION( "sneakers cover everything, the ankle less than the rest" ) {
        detached_ptr<item> sneakers = item::spawn( itype_id( "sneakers" ) );
        for( const auto &[id, coverage] : expected_foot_sub_parts ) {
            CAPTURE( id );
            CHECK( sneakers->covers( sub_bodypart_str_id( id ) ) );
        }
        // CDF: DDA's 100 upper / 60 ankle scaled to the CDF flat 80 (rule A) give 87 / 52
        CHECK( sneakers->get_coverage( foot_l, sub_bodypart_str_id( "foot_toes_l" ) ) == 87 );
        CHECK( sneakers->get_coverage( foot_l, sub_bodypart_str_id( "foot_heel_l" ) ) == 87 );
        CHECK( sneakers->get_coverage( foot_l, sub_bodypart_str_id( "foot_arch_l" ) ) == 87 );
        CHECK( sneakers->get_coverage( foot_l, sub_bodypart_str_id( "foot_sole_l" ) ) == 87 );
        CHECK( sneakers->get_coverage( foot_l, sub_bodypart_str_id( "foot_ankle_l" ) ) == 52 );
        CHECK( sneakers->get_coverage( foot_l ) == 80 );
        CHECK( sneakers->get_encumber( guy, foot_l ) == 4 );
    }
}

namespace {

// CDF: the 54 converted rows. Coverage and encumbrance are CDF's flat foot values at the base
// (cc4d9bae9a); the six rows whose aggregate changes because DDA covers fewer sub-locations are
// marked below with the new aggregate (rule 4).
struct foot1_expect {
    const char *id;
    int coverage;
    int encumbrance;
};

const std::vector<foot1_expect> foot1_table = {
    { "bastsandals", 40, 10 },
    { "boots", 90, 12 },
    { "boots_acidchitin", 30, 15 },        // CDF: 95 flat -> 30, DDA toes+ankle only
    { "boots_bone", 90, 16 },
    { "boots_bunker", 100, 40 },
    { "boots_chitin", 30, 10 },            // CDF: 95 flat -> 30, DDA toes+ankle only
    { "boots_combat", 100, 25 },
    { "boots_fur", 90, 20 },
    { "boots_hiking", 95, 12 },
    { "boots_hsurvivor", 100, 30 },
    { "boots_plate", 100, 25 },
    { "boots_rubber", 100, 15 },
    { "boots_scrap", 80, 20 },
    { "boots_steel", 90, 20 },
    { "boots_survivor", 100, 24 },
    { "boots_western", 95, 24 },
    { "boots_winter", 90, 25 },
    { "boots_wsurvivor", 100, 38 },
    { "bootsheath", 5, 3 },
    { "bootstrap", 5, 2 },
    { "chainmail_suit_faraday", 75, 30 },  // CDF: 100 flat -> 75, DDA toes+ankle+heel+arch only
    { "cleats", 80, 15 },
    { "clogs", 78, 20 },                   // CDF: 85 flat -> 78, DDA toes + heel/arch shape
    { "clownshoes", 85, 60 },
    { "dance_shoes", 60, 5 },
    { "dress_shoes", 60, 15 },
    { "flip_flops", 10, 35 },
    { "flip_flops_exp", 12, 30 },
    { "geta", 42, 10 },                    // CDF: 50 flat -> 42, DDA arch + sole only
    { "golf_shoes", 70, 12 },
    { "heels", 30, 50 },
    { "knee_high_boots", 65, 20 },
    { "leathersandals", 20, 5 },
    { "legpouch", 5, 1 },
    { "lowtops", 80, 0 },
    { "mocassins", 50, 2 },
    { "motorbike_boots", 95, 30 },
    { "rm13_armor", 100, 30 },
    { "rm13_armor_on", 100, 10 },
    { "robofac_enviro_suit", 100, 15 },
    { "roller_blades", 100, 25 },
    { "roller_shoes_off", 80, 4 },
    { "roller_shoes_on", 80, 4 },
    { "rollerskates", 100, 25 },
    { "shoes_birchbark", 50, 15 },
    { "shoes_bowling", 80, 10 },
    { "slippers", 30, 30 },
    { "sneakers", 80, 4 },
    { "socks_ankle", 80, 0 },
    { "socks_bag", 70, 3 },
    { "straw_sandals", 30, 14 },
    { "swim_fins", 75, 25 },               // CDF: 85 flat -> 75, DDA toes/heel/arch/sole shape
    { "thigh_high_boots", 80, 25 },
    { "wetsuit_booties", 100, 13 },
};

} // namespace

TEST_CASE( "foot1_aggregate_and_encumbrance_kept", "[armor][foot1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    REQUIRE( foot1_table.size() == 54 );
    for( const foot1_expect &row : foot1_table ) {
        CAPTURE( row.id );
        detached_ptr<item> it = item::spawn( itype_id( row.id ) );
        CHECK( it->get_coverage( foot_l ) == row.coverage );
        CHECK( it->get_encumber( guy, foot_l ) == row.encumbrance );
    }
}

TEST_CASE( "foot1_sided_accessories", "[armor][foot1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );
    const bodypart_id foot_r( "foot_r" );

    for( const char *id : { "bootsheath", "bootstrap", "legpouch" } ) {
        CAPTURE( id );
        detached_ptr<item> pouch = item::spawn( itype_id( id ) );
        // CDF: flat coverage 5 at the base; DDA's ankle portion at 25 is 5 of the foot's 100
        CHECK( pouch->get_coverage( foot_l ) == 5 );

        pouch->set_side( side::LEFT );
        const std::vector<sub_bodypart_id> left = pouch->get_covered_sub_body_parts();
        CHECK( covers_sub( left, "foot_ankle_l" ) );
        CHECK_FALSE( covers_sub( left, "foot_ankle_r" ) );
        CHECK_FALSE( covers_sub( left, "foot_toes_l" ) );

        pouch->set_side( side::RIGHT );
        const std::vector<sub_bodypart_id> right = pouch->get_covered_sub_body_parts();
        CHECK( covers_sub( right, "foot_ankle_r" ) );
        CHECK_FALSE( covers_sub( right, "foot_ankle_l" ) );
    }
}

namespace {

// Fraction of `hits` left-foot hits that a worn `id` stops (damage is reduced), 0..1.
auto foot_stop_rate( const std::string &id, int hits ) -> double
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    guy.wear_item( item::spawn( id ), false );
    const bodypart_id foot_l( "foot_l" );
    int stopped = 0;
    for( int i = 0; i < hits; ++i ) {
        damage_instance dmg( damage_type::DT_CUT, 10 );
        guy.absorb_hit( foot_l, dmg );
        if( dmg.damage_units[0].amount < 10.0f ) {
            ++stopped;
        }
    }
    return static_cast<double>( stopped ) / hits;
}

} // namespace

TEST_CASE( "foot1_hit_roll_matches_aggregate", "[armor][foot1][p3]" )
{
    constexpr int hits = 20000;

    SECTION( "heels stop about their 30% foot aggregate" ) {
        // CDF: toes and heel at 49, sole at 61 -> 0.10 * 49 + 0.20 * 49 + 0.25 * 61 = 30%
        CHECK( foot_stop_rate( "heels", hits ) == Approx( 0.30 ).margin( 0.03 ) );
    }
    SECTION( "boots stop about 90%" ) {
        CHECK( foot_stop_rate( "boots", hits ) == Approx( 0.90 ).margin( 0.03 ) );
    }
    SECTION( "chitin boots stop about their converted 30%, not the old flat 95%" ) {
        // CDF: flat 95 at the base; DDA guards toes and ankle only, 0.10 + 0.20 = 30%
        CHECK( foot_stop_rate( "boots_chitin", hits ) == Approx( 0.30 ).margin( 0.03 ) );
    }
}

TEST_CASE( "foot1_children_keep_their_own_numbers", "[armor][foot1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    // CDF: the children set their own flat encumbrance at the base (boots 12, chitin 10,
    // hsurvivor 30, plate 25); the converted portions carry the child's number, not the parent's
    detached_ptr<item> western = item::spawn( itype_id( "boots_western" ) );
    CHECK( western->get_encumber( guy, foot_l ) == 24 );
    CHECK( western->get_coverage( foot_l ) == 95 );

    detached_ptr<item> bone = item::spawn( itype_id( "boots_bone" ) );
    CHECK( bone->get_encumber( guy, foot_l ) == 16 );

    detached_ptr<item> acidchitin = item::spawn( itype_id( "boots_acidchitin" ) );
    CHECK( acidchitin->get_encumber( guy, foot_l ) == 15 );
    CHECK( acidchitin->get_coverage( foot_l ) == 30 );

    detached_ptr<item> sasurvivor = item::spawn( itype_id( "boots_sasurvivor" ) );
    CHECK( sasurvivor->get_encumber( guy, foot_l ) == 25 );

    detached_ptr<item> superalloy = item::spawn( itype_id( "boots_plate_superalloy" ) );
    CHECK( superalloy->get_encumber( guy, foot_l ) == 20 );

    // carbon_boots sets no flat key: it inherits boots_plate's portions and their 25
    detached_ptr<item> carbon = item::spawn( itype_id( "carbon_boots" ) );
    CHECK( carbon->get_encumber( guy, foot_l ) == 25 );
    CHECK( carbon->get_coverage( foot_l ) == 100 );

    // rm13_armor_on sets its own encumbrance 10 over the whole suit
    detached_ptr<item> rm13_on = item::spawn( itype_id( "rm13_armor_on" ) );
    CHECK( rm13_on->get_encumber( guy, foot_l ) == 10 );
    CHECK( rm13_on->get_encumber( guy, bodypart_id( "torso" ) ) == 10 );
}

TEST_CASE( "foot1_flat_items_cover_all_ten", "[armor][foot1][p3]" )
{
    for( const char *id : { "socks", "sleeping_bag", "power_armor_exo_1", "thermal_socks" } ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ) );
        CHECK_FALSE( it->has_sub_coverage() );
        const std::vector<sub_bodypart_id> covered = it->get_covered_sub_body_parts();
        for( const auto &[sub, coverage] : expected_foot_sub_parts ) {
            CAPTURE( sub );
            CHECK( covers_sub( covered, sub ) );
        }
    }
}

TEST_CASE( "foot1_riot_foot_portion", "[armor][foot1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    // CDF: DDA legs_armor.json:2068-2074 and suits_protection.json:1382-1388, the foot portion
    // covers the arch and the ankle at 75 only
    for( const char *id : { "armor_riot_leg", "armor_riot" } ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ) );
        CHECK( it->get_coverage( foot_l, sub_bodypart_str_id( "foot_arch_l" ) ) == 75 );
        CHECK( it->get_coverage( foot_l, sub_bodypart_str_id( "foot_ankle_l" ) ) == 75 );
        CHECK( it->get_coverage( foot_l, sub_bodypart_str_id( "foot_toes_l" ) ) == 0 );
        CHECK( it->get_coverage( foot_l, sub_bodypart_str_id( "foot_heel_l" ) ) == 0 );
        CHECK( it->get_coverage( foot_l, sub_bodypart_str_id( "foot_sole_l" ) ) == 0 );
        CHECK( it->get_encumber( guy, foot_l ) == 2 );
    }
}
