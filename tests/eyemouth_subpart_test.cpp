#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "damage.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// EYEMOUTH1: the eyes and mouth sub-locations (DDA ids and weights) and the six items converted
// to use them. Head numbers are DDA's head portions (HEAD2); eyes and mouth numbers are DDA's portion data.

namespace
{

struct eyemouth_expected {
    const char *id;
    int max_coverage;
    const char *parent;
};

const std::vector<eyemouth_expected> eyemouth_sub_parts = {
    { "eyes_left", 50, "eyes" }, { "eyes_right", 50, "eyes" },
    { "mouth_lips", 15, "mouth" }, { "mouth_nose", 15, "mouth" },
    { "mouth_cheeks", 30, "mouth" }, { "mouth_chin", 40, "mouth" },
};

const std::vector<std::string> mouth_ids = { "mouth_lips", "mouth_nose", "mouth_cheeks", "mouth_chin" };

auto sbp( const std::string &id ) -> sub_bodypart_str_id
{
    return sub_bodypart_str_id( id );
}

auto set_covers( const item &it, const std::vector<std::string> &expected ) -> bool
{
    const body_part_set got = it.get_covered_body_parts();
    if( got.count() != expected.size() ) {
        return false;
    }
    return std::ranges::all_of( expected, [&]( const std::string & id ) {
        return got.test( bodypart_str_id( id ) );
    } );
}

// Share of `hits` hits on `bp` that a worn `id` stops (damage reduced), 0..1.
auto stop_rate( const std::string &id, const char *bp, int hits ) -> double
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    detached_ptr<item> worn = item::spawn( id );
    // CDF: a cut of 10 destroys thin cloth and plastic on the first hit, and the item then stops
    // nothing for the other hits; UNBREAKABLE keeps it on so the rate measures coverage only.
    worn->set_flag( flag_id( "UNBREAKABLE" ) );
    guy.wear_item( std::move( worn ), false );
    const bodypart_id part( bp );
    int stopped = 0;
    for( int i = 0; i < hits; ++i ) {
        damage_instance dmg( damage_type::DT_CUT, 10 );
        guy.absorb_hit( part, dmg );
        if( dmg.damage_units[0].amount < 10.0f ) {
            ++stopped;
        }
    }
    return static_cast<double>( stopped ) / hits;
}

} // namespace

TEST_CASE( "eyemouth1_sub_parts_load", "[armor][eyemouth1]" )
{
    for( const eyemouth_expected &e : eyemouth_sub_parts ) {
        CAPTURE( e.id );
        const sub_bodypart_str_id id( e.id );
        REQUIRE( id.is_valid() );
        CHECK( id->max_coverage == e.max_coverage );
        CHECK( id->parent == bodypart_str_id( e.parent ) );
        CHECK_FALSE( id->secondary );
        const std::vector<sub_bodypart_str_id> &listed = id->parent->sub_parts;
        CHECK( std::ranges::find( listed, id ) != listed.end() );
    }
    CHECK( sbp( "eyes_left" )->part_side == side::LEFT );
    CHECK( sbp( "eyes_right" )->part_side == side::RIGHT );
    CHECK( sbp( "eyes_left" )->opposite == sbp( "eyes_right" ) );
    CHECK( sbp( "eyes_right" )->opposite == sbp( "eyes_left" ) );
    // CDF: the mouth entries have no opposite (DDA's sub_limb_debug is not ported), it is themselves
    for( const std::string &id : mouth_ids ) {
        CAPTURE( id );
        CHECK( sbp( id )->part_side == side::BOTH );
        CHECK( sbp( id )->opposite == sbp( id ) );
    }
    for( const char *bp : { "eyes", "mouth" } ) {
        CAPTURE( bp );
        int total = 0;
        for( const sub_bodypart_str_id &s : bodypart_str_id( bp )->sub_parts ) {
            total += s->max_coverage;
        }
        CHECK( total == 100 );
    }
    CHECK( bodypart_str_id( "eyes" )->sub_parts.size() == 2 );
    CHECK( bodypart_str_id( "mouth" )->sub_parts.size() == 4 );
    // CDF: HAND1 gave the hands four sub-locations; the appendix has none
    CHECK( bodypart_str_id( "num_bp" )->sub_parts.empty() );
    // the 24 older entries are unchanged
    CHECK( sbp( "torso_upper" )->max_coverage == 60 );
    CHECK( sbp( "head_throat" )->max_coverage == 20 );
    CHECK( sbp( "head_ear_r" )->max_coverage == 10 );
    CHECK( bodypart_str_id( "head" )->sub_parts.size() == 6 );
}

TEST_CASE( "eyemouth1_random_sub_part_distribution", "[armor][eyemouth1]" )
{
    constexpr int rolls = 100000;
    for( const char *bp : { "eyes", "mouth" } ) {
        CAPTURE( bp );
        std::map<std::string, int> counts;
        for( int i = 0; i < rolls; ++i ) {
            counts[bodypart_str_id( bp )->random_sub_part().str()]++;
        }
        REQUIRE_FALSE( bodypart_str_id( bp )->sub_parts.empty() );
        for( const sub_bodypart_str_id &s : bodypart_str_id( bp )->sub_parts ) {
            CAPTURE( s.str() );
            const auto found = counts.find( s.str() );
            const double share = found == counts.end() ? 0.0 : 100.0 * found->second / rolls;
            CHECK( share == Approx( s->max_coverage ).margin( 1.0 ) );
        }
    }
}

namespace
{

struct kept_row {
    const char *id;
    std::vector<std::string> covers;
    int head_cov; // 0 when the item does not cover the head
    int head_enc;
    int eyes_cov; // 0 when the item does not cover the eyes
    int eyes_enc;
    int mouth_cov;
    int mouth_enc;
};

// CDF: the head numbers are DDA's head portions as HEAD2 converted them (coverage aggregate and
// weight-derived or DDA-written encumbrance, see briefs/head2/head2-v5-table.md). The eyes and mouth numbers are DDA's portion data (owner ruling: where the item sits
// and how much it restricts there follows DDA), see briefs/eyemouth1/literals.md.
const std::vector<kept_row> kept_rows = {
    // DDA helmets.json helmet_corinthian: eyes 50/0, nose+cheeks 95/0, lips+chin 75/0 = 84
    { "helmet_corinthian", { "head", "eyes", "mouth" }, 60, 30, 50, 0, 84, 0 },
    // DDA helmet_kabuto: eyes 35/0, lips 80/10, cheeks+chin+nose 100/0 = 97
    { "helmet_kabuto", { "head", "eyes", "mouth" }, 99, 49, 35, 0, 97, 10 },
    // DDA helmet_motor: eyes visor 100/5; nose, chin, cheeks+lips 100/5 each
    { "helmet_motor", { "head", "eyes", "mouth" }, 76, 34, 100, 5, 100, 15 },
    // DDA mask_gas: eyes 100/10, lips+nose+chin 100/[20,40], cheeks 100/20
    // CDF: HEAD2 adds DDA's head portions (crown+nape 25/0, forehead 100/20): aggregate 26, 20
    { "mask_gas", { "head", "mouth", "eyes" }, 26, 20, 100, 10, 100, 40 },
    { "mask_gas_xl", { "head", "mouth", "eyes" }, 26, 20, 100, 10, 100, 40 },
    // DDA mask_ski: lips+cheeks+chin 100/10 = 85
    { "mask_ski", { "head", "mouth" }, 65, 6, 0, 0, 85, 10 },
    // DDA headgear: cheeks+chin 95/0 = 66.5, rounds to 67 (no eyes portion in DDA or CDF)
    { "headgear", { "head", "mouth" }, 43, 12, 0, 0, 67, 0 },
    // DDA helmet_barbute: eyes 65/0, cheeks 100/0, lips+nose+chin 50/0 = 65. The DDA portion that
    // lists mouth and head together is a mouth-only portion here (HEAD2 parity row; the head
    // sub-locations are DDA's other head portions). carbon_helmet follows helmet_barbute (705 g).
    { "helmet_barbute", { "head", "eyes", "mouth" }, 64, 27, 65, 0, 65, 0 },
    { "helmet_barbute_superalloy", { "head", "eyes", "mouth" }, 64, 25, 65, 0, 65, 0 },
    { "carbon_helmet", { "head", "eyes", "mouth" }, 64, 14, 65, 0, 65, 0 },
    // DDA helmet_galea: cheeks+chin 40/0 = 28 (no eyes)
    { "helmet_galea", { "head", "mouth" }, 60, 30, 0, 0, 28, 0 },
    // DDA helmet_nasal: nose 90/0 = 13.5, rounds to 14. DDA has no eyes portion, so the eyes are
    // dropped (orchestrator ruling 2026-11-04, placement follows DDA); head 44/21 is DDA's
    // head portions (HEAD2)
    { "helmet_nasal", { "head", "mouth" }, 44, 21, 0, 0, 14, 0 },
};

} // namespace

TEST_CASE( "eyemouth1_numbers_follow_dda", "[armor][eyemouth1]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    for( const kept_row &row : kept_rows ) {
        CAPTURE( row.id );
        detached_ptr<item> it = item::spawn( itype_id( row.id ) );
        CHECK( it->has_sub_coverage() );
        CHECK( set_covers( *it, row.covers ) );
        CHECK( it->get_coverage( bodypart_id( "mouth" ) ) == row.mouth_cov );
        CHECK( it->get_encumber( guy, bodypart_id( "mouth" ) ) == row.mouth_enc );
        if( row.head_cov > 0 ) {
            CHECK( it->get_coverage( bodypart_id( "head" ) ) == row.head_cov );
            CHECK( it->get_encumber( guy, bodypart_id( "head" ) ) == row.head_enc );
        } else {
            CHECK_FALSE( it->covers( bodypart_id( "head" ) ) );
        }
        if( row.eyes_cov > 0 ) {
            CHECK( it->get_coverage( bodypart_id( "eyes" ) ) == row.eyes_cov );
            CHECK( it->get_encumber( guy, bodypart_id( "eyes" ) ) == row.eyes_enc );
        } else {
            CHECK_FALSE( it->covers( bodypart_id( "eyes" ) ) );
        }
    }
}

TEST_CASE( "eyemouth1_shapes_leave_gaps", "[armor][eyemouth1]" )
{
    const bodypart_id mouth( "mouth" );
    const auto check_shape = [&]( const char *id, const std::map<std::string, int> &expected ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ) );
        for( const auto &[name, cov] : expected ) {
            CAPTURE( name );
            CHECK( it->get_coverage( mouth, sbp( name ) ) == cov );
            CHECK( it->covers( sbp( name ) ) == ( cov > 0 ) );
        }
    };
    // DDA numbers: the ski mask leaves the nose open
    check_shape( "mask_ski", { { "mouth_lips", 100 }, { "mouth_nose", 0 }, { "mouth_cheeks", 100 },
            { "mouth_chin", 100 }
        } );
    check_shape( "helmet_kabuto", { { "mouth_lips", 80 }, { "mouth_nose", 100 },
            { "mouth_cheeks", 100 }, { "mouth_chin", 100 }
        } );
    check_shape( "helmet_corinthian", { { "mouth_lips", 75 }, { "mouth_nose", 95 },
            { "mouth_cheeks", 95 }, { "mouth_chin", 75 }
        } );
    check_shape( "headgear", { { "mouth_lips", 0 }, { "mouth_nose", 0 }, { "mouth_cheeks", 95 },
            { "mouth_chin", 95 }
        } );
    check_shape( "helmet_galea", { { "mouth_lips", 0 }, { "mouth_nose", 0 }, { "mouth_cheeks", 40 },
            { "mouth_chin", 40 }
        } );
    check_shape( "helmet_nasal", { { "mouth_lips", 0 }, { "mouth_nose", 90 }, { "mouth_cheeks", 0 },
            { "mouth_chin", 0 }
        } );
    for( const char *id : { "helmet_barbute", "helmet_barbute_superalloy", "carbon_helmet" } ) {
        check_shape( id, { { "mouth_lips", 50 }, { "mouth_nose", 50 }, { "mouth_cheeks", 100 },
                { "mouth_chin", 50 }
            } );
    }
    for( const char *id : { "helmet_motor", "mask_gas", "mask_gas_xl" } ) {
        check_shape( id, { { "mouth_lips", 100 }, { "mouth_nose", 100 }, { "mouth_cheeks", 100 },
                { "mouth_chin", 100 }
            } );
    }
}

TEST_CASE( "eyemouth1_flat_items_cover_all_six", "[armor][eyemouth1]" )
{
    const std::vector<std::string> all_eyes = { "eyes_left", "eyes_right" };
    for( const char *id : { "balclava", "scarf", "mask_fsurvivor", "goggles_nv",
                            "sunglasses"
                          } ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ) );
        CHECK_FALSE( it->has_sub_coverage() );
        const bool eyes = it->covers( bodypart_id( "eyes" ) );
        const bool mouth = it->covers( bodypart_id( "mouth" ) );
        CHECK( ( eyes || mouth ) );
        for( const std::string &s : all_eyes ) {
            CAPTURE( s );
            CHECK( it->covers( sbp( s ) ) == eyes );
        }
        for( const std::string &s : mouth_ids ) {
            CAPTURE( s );
            CHECK( it->covers( sbp( s ) ) == mouth );
        }
    }
    // the ones named in the packet cover both
    CHECK( item::spawn( itype_id( "mask_fsurvivor" ) )->covers( bodypart_id( "mouth" ) ) );
    CHECK( item::spawn( itype_id( "mask_fsurvivor" ) )->covers( bodypart_id( "eyes" ) ) );
    CHECK( item::spawn( itype_id( "goggles_nv" ) )->covers( bodypart_id( "eyes" ) ) );
}

TEST_CASE( "eyemouth1_hit_roll_matches_aggregate", "[armor][eyemouth1]" )
{
    constexpr int hits = 20000;
    // the ski mask stops its aggregate of the hits on the mouth (DDA shape: 85, the nose is open)
    CHECK( stop_rate( "mask_ski", "mouth", hits ) == Approx( 0.85 ).margin( 0.03 ) );
    // CDF: the per-sub-location half (hits rolled onto mouth_nose are never stopped) is not
    // measured here, because absorb_hit does not expose the rolled sub-location. The open nose
    // is what makes the aggregate 85 and not 100, so the 0.85 check above covers it.
}

TEST_CASE( "eyemouth1_mixed_item_keeps_eyes", "[armor][eyemouth1]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    detached_ptr<item> mask = item::spawn( itype_id( "mask_gas" ) );
    CHECK( mask->has_sub_coverage() );
    const bodypart_id eyes( "eyes" );
    CHECK( mask->get_coverage( eyes, sbp( "eyes_left" ) ) == 100 );
    CHECK( mask->get_coverage( eyes, sbp( "eyes_right" ) ) == 100 );
    CHECK( mask->get_coverage( eyes ) == 100 );
    CHECK( mask->get_encumber( guy, eyes ) == 10 ); // DDA: eyes portion encumbrance 10
    CHECK( stop_rate( "mask_gas", "eyes", 20000 ) == Approx( 1.0 ).margin( 0.03 ) );
}

TEST_CASE( "eyemouth1_child_numbers", "[armor][eyemouth1]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    // CDF: the children of helmet_barbute copy its DDA eyes and mouth portions; the superalloy
    // one keeps its own head encumbrance 25 (it had a flat key, so it carries its own portions)
    detached_ptr<item> alloy = item::spawn( itype_id( "helmet_barbute_superalloy" ) );
    detached_ptr<item> carbon = item::spawn( itype_id( "carbon_helmet" ) );
    CHECK( alloy->get_encumber( guy, bodypart_id( "head" ) ) == 25 );
    // CDF: carbon_helmet's own weight (705 g) through the head table, HEAD2
    CHECK( carbon->get_encumber( guy, bodypart_id( "head" ) ) == 14 );
    CHECK( alloy->has_sub_coverage() );
    CHECK( carbon->has_sub_coverage() );
}
