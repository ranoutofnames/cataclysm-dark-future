// HAND1: hand sub-locations (wrist, palm, back of hand, fingers) and the hand items converted
// to them. DDA source: data/json/body_parts.json:1160-1239 (the eight sub_body_part entries),
// items/armor/gloves.json, items/armor/jewelry.json and items/tool_armor.json. Expected numbers
// are CDF's flat hand coverage and encumbrance at the base (bbc9aa0e76), re-derived by rule A
// (DDA shape, CDF aggregate coverage and encumbrance), so a wrong conversion cannot agree with
// them by construction.
#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "damage.h"
#include "enums.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "type_id.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// CDF: DDA body_parts.json:1160-1239, the eight hand sub-locations and their max_coverage.
const std::map<std::string, int> expected_hand_sub_parts = {
    { "hand_wrist_l", 10 }, { "hand_palm_l", 20 },
    { "hand_back_l", 20 }, { "hand_fingers_l", 50 },
    { "hand_wrist_r", 10 }, { "hand_palm_r", 20 },
    { "hand_back_r", 20 }, { "hand_fingers_r", 50 },
};

bool covers_sub( const std::vector<sub_bodypart_id> &covered, const std::string &id )
{
    const sub_bodypart_str_id wanted( id );
    return std::ranges::any_of( covered, [&wanted]( const sub_bodypart_id & sbp ) {
        return sbp.id() == wanted;
    } );
}

std::set<std::string> sub_names( const std::vector<sub_bodypart_id> &covered )
{
    std::set<std::string> names;
    for( const sub_bodypart_id &sbp : covered ) {
        names.insert( sbp.id().str() );
    }
    return names;
}

// Builds the expected covered sub-locations of a portion set: `parts` holds w(rist), p(alm),
// b(ack), f(ingers); `right_only` is the single right glove_jackson.
std::set<std::string> expected_sub_set( const std::string &parts, bool right_only )
{
    std::set<std::string> out;
    const std::map<char, std::string> names = {
        { 'w', "hand_wrist_" }, { 'p', "hand_palm_" },
        { 'b', "hand_back_" }, { 'f', "hand_fingers_" },
    };
    for( const char c : parts ) {
        if( right_only ) {
            out.insert( names.at( c ) + "r" );
        } else {
            out.insert( names.at( c ) + "l" );
            out.insert( names.at( c ) + "r" );
        }
    }
    return out;
}

} // namespace

TEST_CASE( "hand1_sub_parts_load", "[armor][hand1][p3]" )
{
    REQUIRE( expected_hand_sub_parts.size() == 8 );
    for( const auto &[id, coverage] : expected_hand_sub_parts ) {
        CAPTURE( id );
        const sub_bodypart_str_id sbp( id );
        REQUIRE( sbp.is_valid() );
        CHECK( sbp->max_coverage == coverage );
        CHECK_FALSE( sbp->secondary );
        const std::string parent = id.back() == 'l' ? "hand_l" : "hand_r";
        CHECK( sbp->parent == bodypart_str_id( parent ) );
        CHECK( sbp->part_side == ( id.back() == 'l' ? side::LEFT : side::RIGHT ) );
        CHECK( sbp->opposite.is_valid() );
        CHECK_FALSE( sbp->opposite->secondary );
        // the parent lists the part (DDA body_parts.json:558, 589)
        const std::vector<sub_bodypart_str_id> &listed = sbp->parent->sub_parts;
        CHECK( std::ranges::find( listed, sbp ) != listed.end() );
    }
    for( const char *bp : { "hand_l", "hand_r" } ) {
        CAPTURE( bp );
        const std::vector<sub_bodypart_str_id> &listed = bodypart_str_id( bp )->sub_parts;
        CHECK( listed.size() == 4 );
        int total = 0;
        for( const sub_bodypart_str_id &sbp : listed ) {
            if( !sbp->secondary ) {
                total += sbp->max_coverage;
            }
        }
        // CDF: the four primary hand locations add up to 100, like the other parts
        CHECK( total == 100 );
    }
    // CDF: the foot parts are not touched by this packet (FOOT1 left 10 of them)
    for( const char *id : { "foot_sole_l", "foot_arch_l", "foot_toes_l", "foot_ankle_l",
                            "foot_heel_l", "foot_sole_r", "foot_arch_r", "foot_toes_r",
                            "foot_ankle_r", "foot_heel_r" } ) {
        CAPTURE( id );
        CHECK( sub_bodypart_str_id( id ).is_valid() );
    }
    CHECK( bodypart_str_id( "foot_l" )->sub_parts.size() == 5 );
    CHECK( bodypart_str_id( "foot_r" )->sub_parts.size() == 5 );
}

TEST_CASE( "hand1_random_sub_part_distribution", "[armor][hand1][p3]" )
{
    constexpr int rolls = 100000;
    std::map<std::string, int> counts;
    for( int i = 0; i < rolls; ++i ) {
        counts[bodypart_str_id( "hand_l" )->random_sub_part().str()]++;
    }
    for( const sub_bodypart_str_id &sbp : bodypart_str_id( "hand_l" )->sub_parts ) {
        if( sbp->secondary ) {
            continue;
        }
        CAPTURE( sbp.str() );
        const double share = 100.0 * counts[sbp.str()] / rolls;
        CHECK( share == Approx( sbp->max_coverage ).margin( 1.0 ) );
    }
    // CDF: the right hand has the same four, so the left rolls never return a right id
    for( const auto &[id, n] : counts ) {
        CAPTURE( id, n );
        CHECK( id.ends_with( "_l" ) );
    }
}

namespace {

// CDF: the 18 converted gloves and watches plus the suit (rule 8). Coverage and encumbrance are
// CDF's flat hand values at the base (bbc9aa0e76). game_watch is the one changed row (rule 4).
struct hand1_expect {
    const char *id;
    int coverage;
    int encumbrance;
    const char *parts;
    bool right_only;
    bool whole_body; // CDF: the enviro suit covers the whole body, so its set has more than the hands
};

const std::vector<hand1_expect> hand1_table = {
    { "diving_watch", 5, 0, "w", false, false },
    { "game_watch", 10, 0, "w", false, false },       // CDF: 15 flat at the base -> 10, DDA's wrist is already 100
    { "gold_watch", 5, 0, "w", false, false },
    { "platinum_watch", 5, 0, "w", false, false },
    { "rad_monitor", 5, 0, "w", false, false },
    { "sf_watch", 10, 0, "w", false, false },
    { "silver_watch", 10, 0, "w", false, false },
    { "wristwatch", 5, 0, "w", false, false },
    { "gauntlets_chitin", 95, 10, "wpbf", false, false },
    { "gauntlets_larmor", 60, 4, "wpbf", false, false },      // CDF: the portions give 59.9, rounded up to 60
    { "glove_jackson", 95, 0, "wpbf", true, false },          // CDF: a single right glove
    { "gloves_fingerless", 50, 0, "wpbf", false, false },     // CDF: the portions give 50.1
    { "gloves_fingerless_mod", 50, 3, "wpbf", false, false }, // CDF: the portions give 49.8
    { "gloves_golf", 90, 5, "wpbf", false, false },           // CDF: the portions give 90.1
    { "gloves_hsurvivor", 100, 30, "wpbf", false, false },
    { "gloves_plate", 100, 25, "wpbf", false, false },
    { "gloves_wool_fingerless", 50, 7, "wpbf", false, false },
    { "sockmitts", 40, 0, "wpb", false, false },
    { "robofac_enviro_suit", 100, 15, "wpbf", false, true }, // CDF: rule 8, FOOT1 converted the feet
};

} // namespace

TEST_CASE( "hand1_aggregate_and_encumbrance_kept", "[armor][hand1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id hand_l( "hand_l" );
    const bodypart_id hand_r( "hand_r" );

    REQUIRE( hand1_table.size() == 19 );
    for( const hand1_expect &row : hand1_table ) {
        CAPTURE( row.id );
        detached_ptr<item> it = item::spawn( itype_id( row.id ) );
        const bodypart_id &hand = row.right_only ? hand_r : hand_l;
        CHECK( it->get_coverage( hand ) == row.coverage );
        CHECK( it->get_encumber( guy, hand ) == row.encumbrance );
        // the sub-locations are the DDA portion sets, not a flat "all four"
        if( row.whole_body ) {
            const std::set<std::string> all = sub_names( it->get_covered_sub_body_parts() );
            for( const std::string &sub : expected_sub_set( row.parts, row.right_only ) ) {
                CAPTURE( sub );
                CHECK( all.contains( sub ) );
            }
        } else {
            CHECK( sub_names( it->get_covered_sub_body_parts() ) ==
                   expected_sub_set( row.parts, row.right_only ) );
        }
    }
}

TEST_CASE( "hand1_watches_wrist_only", "[armor][hand1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id hand_l( "hand_l" );
    const bodypart_id hand_r( "hand_r" );

    for( const char *id : { "diving_watch", "game_watch", "gold_watch", "platinum_watch",
                            "rad_monitor", "sf_watch", "silver_watch", "wristwatch" } ) {
        CAPTURE( id );
        detached_ptr<item> watch = item::spawn( itype_id( id ) );
        CHECK( watch->is_sided() );

        watch->set_side( side::LEFT );
        CHECK( watch->covers( hand_l ) );
        CHECK_FALSE( watch->covers( hand_r ) );
        CHECK( watch->covers( sub_bodypart_str_id( "hand_wrist_l" ) ) );
        CHECK_FALSE( watch->covers( sub_bodypart_str_id( "hand_palm_l" ) ) );
        CHECK_FALSE( watch->covers( sub_bodypart_str_id( "hand_back_l" ) ) );
        CHECK_FALSE( watch->covers( sub_bodypart_str_id( "hand_fingers_l" ) ) );
        CHECK( sub_names( watch->get_covered_sub_body_parts() ) == std::set<std::string>{
                   "hand_wrist_l" } );

        watch->set_side( side::RIGHT );
        CHECK( watch->covers( hand_r ) );
        CHECK_FALSE( watch->covers( hand_l ) );
        CHECK( sub_names( watch->get_covered_sub_body_parts() ) == std::set<std::string>{
                   "hand_wrist_r" } );
    }
}

TEST_CASE( "hand1_fingerless_leave_fingers_open", "[armor][hand1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id hand_l( "hand_l" );

    SECTION( "fingerless leather gloves guard the palm most" ) {
        detached_ptr<item> gloves = item::spawn( itype_id( "gloves_fingerless" ) );
        // CDF: DDA 75/100/50 scaled to the CDF flat 50 (rule A) give 56/74/37
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_wrist_l" ) ) == 56 );
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_back_l" ) ) == 56 );
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_palm_l" ) ) == 74 );
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_fingers_l" ) ) == 37 );
        CHECK( gloves->get_coverage( hand_l ) == 50 );
        CHECK( gloves->get_encumber( guy, hand_l ) == 0 );
    }
    SECTION( "fingerless wool gloves cover the fingers at 40" ) {
        detached_ptr<item> gloves = item::spawn( itype_id( "gloves_wool_fingerless" ) );
        // CDF: DDA 100/67 scaled to the CDF flat 50 give 60/40
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_wrist_l" ) ) == 60 );
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_palm_l" ) ) == 60 );
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_back_l" ) ) == 60 );
        CHECK( gloves->get_coverage( hand_l, sub_bodypart_str_id( "hand_fingers_l" ) ) == 40 );
        CHECK( gloves->get_coverage( hand_l ) == 50 );
        CHECK( gloves->get_encumber( guy, hand_l ) == 7 );
    }
    SECTION( "sock mitts do not cover the fingers at all" ) {
        detached_ptr<item> mitts = item::spawn( itype_id( "sockmitts" ) );
        // CDF: DDA's wrist/palm/back 100 scaled to the CDF flat 40 give 80
        CHECK( mitts->get_coverage( hand_l, sub_bodypart_str_id( "hand_wrist_l" ) ) == 80 );
        CHECK( mitts->get_coverage( hand_l, sub_bodypart_str_id( "hand_palm_l" ) ) == 80 );
        CHECK( mitts->get_coverage( hand_l, sub_bodypart_str_id( "hand_back_l" ) ) == 80 );
        CHECK_FALSE( mitts->covers( sub_bodypart_str_id( "hand_fingers_l" ) ) );
        CHECK( mitts->get_coverage( hand_l ) == 40 );
    }
    SECTION( "leather armor gauntlets are fully covered but thin on the fingers" ) {
        detached_ptr<item> gauntlets = item::spawn( itype_id( "gauntlets_larmor" ) );
        // CDF: DDA 75/100/50 scaled to the CDF flat 60 (rule A) give 62/83/41
        CHECK( gauntlets->get_coverage( hand_l, sub_bodypart_str_id( "hand_wrist_l" ) ) == 62 );
        CHECK( gauntlets->get_coverage( hand_l, sub_bodypart_str_id( "hand_palm_l" ) ) == 83 );
        CHECK( gauntlets->get_coverage( hand_l, sub_bodypart_str_id( "hand_back_l" ) ) == 83 );
        CHECK( gauntlets->get_coverage( hand_l, sub_bodypart_str_id( "hand_fingers_l" ) ) == 41 );
        CHECK( gauntlets->get_coverage( hand_l ) == 60 );
        CHECK( gauntlets->get_encumber( guy, hand_l ) == 4 );
    }
}

namespace {

// CDF: the 21 jewelry rows (12 rings, 9 bracelets). CDF has no hand coverage on them at the base;
// the portion is DDA's (rule 7). The flags are DDA's, added over CDF's FANCY/SUPER_FANCY.
struct jewelry_expect {
    const char *id;
    bool ring;
    int coverage;
    std::vector<const char *> flags;
};

const std::vector<jewelry_expect> jewelry_table = {
    { "bead_bracelet", false, 20, { "NO_WEAR_EFFECT", "BELTED", "OVERSIZE" } },
    { "bracelet_friendship", false, 30, { "NO_WEAR_EFFECT", "BELTED", "OVERSIZE" } },
    { "copper_bracelet", false, 20, { "BELTED", "NO_WEAR_EFFECT", "OVERSIZE" } },
    { "copper_ring", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "diamond_ring", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "garnet_gold_bracelet", false, 20, { "NO_WEAR_EFFECT", "OVERSIZE" } },
    { "garnet_gold_ring", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "garnet_platinum_bracelet", false, 20, { "BELTED", "NO_WEAR_EFFECT", "OVERSIZE" } },
    { "garnet_platinum_ring", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "garnet_silver_bracelet", false, 20, { "NO_WEAR_EFFECT", "OVERSIZE" } },
    { "garnet_silver_ring", true, 0, { "NO_WEAR_EFFECT" } },
    { "gold_bracelet", false, 20, { "BELTED", "NO_WEAR_EFFECT", "OVERSIZE" } },
    { "gold_ring", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "platinum_bracelet", false, 50, { "BELTED", "NO_WEAR_EFFECT", "OVERSIZE" } },
    { "platinum_ring", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "ring_engagement", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "ring_purity", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "ring_signet", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "ring_wedding", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
    { "silver_bracelet", false, 20, { "NO_WEAR_EFFECT", "BELTED", "OVERSIZE" } },
    { "silver_ring", true, 0, { "NO_WEAR_EFFECT", "SKINTIGHT" } },
};

} // namespace

TEST_CASE( "hand1_jewelry_in_place", "[armor][hand1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id hand_l( "hand_l" );

    REQUIRE( jewelry_table.size() == 21 );
    for( const jewelry_expect &row : jewelry_table ) {
        CAPTURE( row.id );
        detached_ptr<item> it = item::spawn( itype_id( row.id ) );
        // CDF: a ring covers the fingers only, a bracelet the wrist only
        CHECK( sub_names( it->get_covered_sub_body_parts() ) ==
               expected_sub_set( row.ring ? "f" : "w", false ) );
        CHECK( it->get_coverage( hand_l, sub_bodypart_str_id( row.ring ? "hand_fingers_l" :
                                "hand_wrist_l" ) ) == row.coverage );
        CHECK( it->get_encumber( guy, hand_l ) == 0 );
        for( const char *flag : row.flags ) {
            CAPTURE( flag );
            CHECK( it->has_flag( flag_id( flag ) ) );
        }
    }
    // CDF: CDF's own FANCY / SUPER_FANCY flags stay
    CHECK( item::spawn( itype_id( "diamond_ring" ) )->has_flag( flag_id( "FANCY" ) ) );
    CHECK( item::spawn( itype_id( "ring_wedding" ) )->has_flag( flag_id( "SUPER_FANCY" ) ) );
}

TEST_CASE( "hand1_children_keep_their_own_numbers", "[armor][hand1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id hand_l( "hand_l" );

    // CDF: the children set their own flat encumbrance at the base (chitin 10, hsurvivor 30,
    // plate 25); the converted portions carry the child's number, not the parent's
    detached_ptr<item> acidchitin = item::spawn( itype_id( "gauntlets_acidchitin" ) );
    CHECK( acidchitin->has_sub_coverage() );
    CHECK( acidchitin->get_encumber( guy, hand_l ) == 15 );
    CHECK( acidchitin->get_coverage( hand_l ) == 95 );

    detached_ptr<item> sasurvivor = item::spawn( itype_id( "gloves_sasurvivor" ) );
    CHECK( sasurvivor->get_encumber( guy, hand_l ) == 25 );
    CHECK( sasurvivor->get_coverage( hand_l ) == 100 );

    detached_ptr<item> superalloy = item::spawn( itype_id( "gloves_plate_superalloy" ) );
    CHECK( superalloy->get_encumber( guy, hand_l ) == 20 );
    CHECK( superalloy->get_coverage( hand_l ) == 100 );

    // these set no flat key: they inherit the parent's portions and their numbers
    detached_ptr<item> carbon = item::spawn( itype_id( "carbon_gloves" ) );
    CHECK( carbon->get_encumber( guy, hand_l ) == 25 );
    CHECK( carbon->get_coverage( hand_l ) == 100 );

    detached_ptr<item> scute = item::spawn( itype_id( "gauntlets_scute" ) );
    CHECK( scute->get_encumber( guy, hand_l ) == 4 );
    CHECK( scute->get_coverage( hand_l ) == 60 );

    // CDF: gauntlets_plarmor's proportional 1.25 encumbrance (4 -> 5) is lost on a converted
    // parent, so the child carries its own portions
    detached_ptr<item> plarmor = item::spawn( itype_id( "gauntlets_plarmor" ) );
    CHECK( plarmor->get_encumber( guy, hand_l ) == 5 );
    CHECK( plarmor->get_coverage( hand_l ) == 60 );

    // CDF: the Arcana_BN and MagicalNights children, only when the mods are loaded
    if( itype_id( "gauntlets_necro" ).is_valid() ) {
        detached_ptr<item> necro = item::spawn( itype_id( "gauntlets_necro" ) );
        CHECK( necro->get_coverage( hand_l ) == 100 );
        CHECK( necro->get_encumber( guy, hand_l ) == 16 );

        detached_ptr<item> necro_xl = item::spawn( itype_id( "gauntlets_necro_xl" ) );
        CHECK( necro_xl->get_coverage( hand_l ) == 75 );
        CHECK( necro_xl->get_encumber( guy, hand_l ) == 12 );

        detached_ptr<item> demonchitin = item::spawn( itype_id( "gauntlets_demonchitin" ) );
        // CDF: 0.9 x the parent's 10 encumbrance, the child's own proportional number
        CHECK( demonchitin->get_coverage( hand_l ) == 95 );
        CHECK( demonchitin->get_encumber( guy, hand_l ) == 9 );
    }
}

namespace {

// CDF: the 77 gem items that copy the six garnet rings and bracelets (12 gold rings, 13 each
// silver/platinum rings and gold/silver/platinum bracelets); they carry no flat key and inherit
// the portion from their parent.
const std::vector<const char *> gem_children = {
    "amethyst_gold_ring", "aquamarine_gold_ring", "emerald_gold_ring", "alexandrite_gold_ring",
    "ruby_gold_ring", "peridot_gold_ring", "sapphire_gold_ring", "tourmaline_gold_ring",
    "citrine_gold_ring", "blue_topaz_gold_ring", "opal_gold_ring", "pearl_gold_ring",
    "diamond_silver_ring", "amethyst_silver_ring", "aquamarine_silver_ring", "emerald_silver_ring",
    "alexandrite_silver_ring", "ruby_silver_ring", "peridot_silver_ring", "sapphire_silver_ring",
    "tourmaline_silver_ring", "citrine_silver_ring", "blue_topaz_silver_ring", "opal_silver_ring",
    "pearl_silver_ring", "diamond_platinum_ring", "amethyst_platinum_ring",
    "aquamarine_platinum_ring", "emerald_platinum_ring", "alexandrite_platinum_ring",
    "ruby_platinum_ring", "peridot_platinum_ring", "sapphire_platinum_ring",
    "tourmaline_platinum_ring", "citrine_platinum_ring", "blue_topaz_platinum_ring",
    "opal_platinum_ring", "pearl_platinum_ring",
    "diamond_gold_bracelet", "amethyst_gold_bracelet", "aquamarine_gold_bracelet",
    "emerald_gold_bracelet", "alexandrite_gold_bracelet", "ruby_gold_bracelet",
    "peridot_gold_bracelet", "sapphire_gold_bracelet", "tourmaline_gold_bracelet",
    "citrine_gold_bracelet", "blue_topaz_gold_bracelet", "opal_gold_bracelet",
    "pearl_gold_bracelet", "diamond_silver_bracelet", "amethyst_silver_bracelet",
    "aquamarine_silver_bracelet", "emerald_silver_bracelet", "alexandrite_silver_bracelet",
    "ruby_silver_bracelet", "peridot_silver_bracelet", "sapphire_silver_bracelet",
    "tourmaline_silver_bracelet", "citrine_silver_bracelet", "blue_topaz_silver_bracelet",
    "opal_silver_bracelet", "pearl_silver_bracelet", "diamond_platinum_bracelet",
    "amethyst_platinum_bracelet", "aquamarine_platinum_bracelet", "emerald_platinum_bracelet",
    "alexandrite_platinum_bracelet", "ruby_platinum_bracelet", "peridot_platinum_bracelet",
    "sapphire_platinum_bracelet", "tourmaline_platinum_bracelet", "citrine_platinum_bracelet",
    "blue_topaz_platinum_bracelet", "opal_platinum_bracelet", "pearl_platinum_bracelet",
};

} // namespace

TEST_CASE( "hand1_gem_children_inherit", "[armor][hand1][p3]" )
{
    REQUIRE( gem_children.size() == 77 );
    int covered_rings = 0;
    int covered_bracelets = 0;
    for( const char *id : gem_children ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ) );
        CHECK( it->has_sub_coverage() );
        if( covers_sub( it->get_covered_sub_body_parts(), "hand_fingers_l" ) ) {
            ++covered_rings;
        }
        if( covers_sub( it->get_covered_sub_body_parts(), "hand_wrist_l" ) ) {
            ++covered_bracelets;
        }
    }
    // the 38 rings and the 39 bracelets all inherit their parent's sub-location
    CHECK( covered_rings == 38 );
    CHECK( covered_bracelets == 39 );
}

TEST_CASE( "hand1_flat_items_cover_all_four", "[armor][hand1][p3]" )
{
    for( const char *id : { "gloves_leather", "mittens", "gloves_wool" } ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ) );
        CHECK_FALSE( it->has_sub_coverage() );
        const std::vector<sub_bodypart_id> covered = it->get_covered_sub_body_parts();
        CHECK( covered.size() == 8 );
        for( const auto &[sub, coverage] : expected_hand_sub_parts ) {
            CAPTURE( sub );
            CHECK( covers_sub( covered, sub ) );
        }
    }
}

namespace {

// Fraction of `hits` left-hand hits that a worn `id` stops (damage is reduced), 0..1.
auto hand_stop_rate( const std::string &id, int hits ) -> double
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    guy.wear_item( item::spawn( id ), false );
    const bodypart_id hand_l( "hand_l" );
    int stopped = 0;
    for( int i = 0; i < hits; ++i ) {
        damage_instance dmg( damage_type::DT_CUT, 10 );
        guy.absorb_hit( hand_l, dmg );
        if( dmg.damage_units[0].amount < 10.0f ) {
            ++stopped;
        }
    }
    return static_cast<double>( stopped ) / hits;
}

} // namespace

TEST_CASE( "hand1_hit_roll_matches_aggregate", "[armor][hand1][p3]" )
{
    constexpr int hits = 20000;

    SECTION( "fingerless gloves stop about their 50% hand aggregate" ) {
        detached_ptr<item> gloves = item::spawn( itype_id( "gloves_fingerless" ) );
        CHECK( gloves->has_sub_coverage() );
        // CDF: wrist/back 56, palm 74, fingers 37 -> 0.10*56 + 0.20*56 + 0.20*74 + 0.50*37
        CHECK( hand_stop_rate( "gloves_fingerless", hits ) == Approx( 0.50 ).margin( 0.03 ) );
    }
    SECTION( "flat gloves keep their own independent roll" ) {
        detached_ptr<item> gloves = item::spawn( itype_id( "gloves_leather" ) );
        CHECK_FALSE( gloves->has_sub_coverage() );
        CHECK( hand_stop_rate( "gloves_leather", hits ) == Approx( 0.95 ).margin( 0.03 ) );
    }
}

TEST_CASE( "hand1_wear_everything_on_one_hand", "[armor][hand1][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );

    const std::vector<const char *> items = {
        "gold_ring", "silver_ring", "gold_bracelet", "wristwatch",
    };
    // CDF: rings are rigid_layer_only at 0 coverage and bracelets/watches are on the belted
    // layer, so nothing here blocks anything else, as CDF allows today
    for( const char *id : items ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ) );
        CHECK( it->has_sub_coverage() );
        it->set_side( side::LEFT );
        CHECK( guy.check_rigid_conflicts( *it ).success() );
        CHECK_FALSE( guy.wear_item( std::move( it ), false ) );
    }
    for( const char *id : { "gloves_leather", "gauntlets_larmor" } ) {
        CAPTURE( id );
        detached_ptr<item> gloves = item::spawn( itype_id( id ) );
        CHECK( guy.check_rigid_conflicts( *gloves ).success() );
        CHECK_FALSE( guy.wear_item( std::move( gloves ), false ) );
    }
}

TEST_CASE( "hand1_natural_attack_with_jewelry", "[armor][hand1][p3]" )
{
    const bodypart_id hand_l( "hand_l" );
    const bodypart_id hand_r( "hand_r" );

    SECTION( "a ring on the left hand restricts natural attacks on that hand only" ) {
        clear_all_state();
        Character &guy = get_player_character();
        clear_character( *guy.as_player(), true );
        detached_ptr<item> ring = item::spawn( itype_id( "gold_ring" ) );
        ring->set_side( side::LEFT );
        REQUIRE_FALSE( guy.wear_item( std::move( ring ), false ) );
        // CDF: DDA rule, the ring has no ALLOWS_NATURAL_ATTACKS (character.cpp:2220-2229)
        CHECK( guy.natural_attack_restricted_on( hand_l ) );
        CHECK_FALSE( guy.natural_attack_restricted_on( hand_r ) );
    }
    SECTION( "fingerless gloves allow natural attacks" ) {
        clear_all_state();
        Character &guy = get_player_character();
        clear_character( *guy.as_player(), true );
        REQUIRE_FALSE( guy.wear_item( item::spawn( itype_id( "gloves_fingerless" ) ), false ) );
        CHECK_FALSE( guy.natural_attack_restricted_on( hand_l ) );
        CHECK_FALSE( guy.natural_attack_restricted_on( hand_r ) );
    }
}
