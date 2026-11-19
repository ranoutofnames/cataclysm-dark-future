#include "catch/catch.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "damage.h"
#include "fstream_utils.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "itype_slots/islot_armor.h"

// TORSO2b: the 22 CDF coats, cloaks and aprons (plus 3 copy-from children) cover the legs through
// leg_draped_l and leg_draped_r (DDA's portions: location sets, coverage and encumbrance; CDF
// keeps resists, warmth, pockets and flags). tests/data/torso2b_expected.json: `upper` is the
// non-leg body parts the item covered before the conversion, `sub` the sub-locations it must cover
// afterwards, `specific` those named by a portion with specifically_covers, `parts` the derived
// coverage, encumbrance, max_encumbrance (-1: depends on the pockets) and the number of
// specifically_covers portions per body part, from DDA b4b62254ae2. Warmth, pockets and flags are
// the CDF values of the TORSO2 tip.

namespace
{

struct draped_row {
    std::string id;
    std::string parent;
    std::vector<std::string> upper;
    std::vector<std::string> sub;
    std::vector<std::string> specific;
    struct part_row {
        std::string part;
        int coverage = 0;
        int encumbrance = 0;
        int max_encumbrance = -1;
        int sub_named = 0;
    };
    std::vector<part_row> parts;
    int warmth = 0;
    int pockets = 0;
    std::vector<std::string> flags;
};

auto read_rows() -> std::vector<draped_row>
{
    std::vector<draped_row> rows;
    const bool read = read_from_file_json( "tests/data/torso2b_expected.json", [&]( JsonIn & jsin ) {
        JsonArray all = jsin.get_array();
        while( all.has_more() ) {
            JsonObject jo = all.next_object();
            draped_row row;
            row.id = jo.get_string( "id" );
            row.parent = jo.get_string( "parent", "" );
            row.upper = jo.get_string_array( "upper" );
            row.sub = jo.get_string_array( "sub" );
            row.specific = jo.get_string_array( "specific" );
            JsonArray part_array = jo.get_array( "parts" );
            while( part_array.has_more() ) {
                JsonObject pj = part_array.next_object();
                draped_row::part_row part;
                part.part = pj.get_string( "part" );
                part.coverage = pj.get_int( "coverage" );
                part.encumbrance = pj.get_int( "encumbrance" );
                part.max_encumbrance = pj.get_int( "max_encumbrance" );
                part.sub_named = pj.get_int( "sub_named" );
                row.parts.push_back( part );
            }
            row.warmth = jo.get_int( "warmth" );
            row.pockets = jo.get_int( "pockets" );
            row.flags = jo.get_string_array( "flags" );
            rows.push_back( row );
        }
    } );
    REQUIRE( read );
    return rows;
}

auto as_set( const std::vector<std::string> &v ) -> std::set<std::string>
{
    return std::set<std::string>( v.begin(), v.end() );
}

// every sub-location `it` covers, whatever the parent
auto sub_locations( const item &it ) -> std::set<std::string>
{
    std::set<std::string> ids;
    for( const sub_bodypart_id &sbp : it.get_covered_sub_body_parts() ) {
        ids.insert( sbp->id.str() );
    }
    return ids;
}

const std::vector<std::string> primary_leg_locations = {
    "leg_hip_l", "leg_upper_l", "leg_knee_l", "leg_lower_l",
    "leg_hip_r", "leg_upper_r", "leg_knee_r", "leg_lower_r"
};

auto parts_of( const draped_row &row ) -> std::vector<std::string>
{
    std::vector<std::string> parts = row.upper;
    parts.push_back( "leg_l" );
    parts.push_back( "leg_r" );
    return parts;
}

} // namespace

TEST_CASE( "leg draped expected data lists the 25 items", "[armor][subpart][torso2b]" )
{
    const std::vector<draped_row> rows = read_rows();
    // CDF: 22 table rows (TORSO2b conversion table) + optical_cloak_on, duster_faux_fur and
    // sleeveless_duster_faux_fur, the copy-from children that repeated `covers`
    CHECK( rows.size() == 25 );
    int children = 0;
    for( const draped_row &row : rows ) {
        children += row.parent.empty() ? 0 : 1;
        CHECK( itype_id( row.id ).is_valid() );
        CHECK( !row.parts.empty() );
    }
    CHECK( children == 3 );
}

TEST_CASE( "L1 the items cover the draped legs and not the primary leg locations",
           "[armor][subpart][torso2b]" )
{
    clear_all_state();
    for( const draped_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        const std::set<std::string> covered = sub_locations( *it );
        CHECK( covered.count( "leg_draped_l" ) == 1 );
        CHECK( covered.count( "leg_draped_r" ) == 1 );
        for( const std::string &leg : primary_leg_locations ) {
            CAPTURE( leg );
            CHECK( covered.count( leg ) == 0 );
        }
        // the upper portion still covers the primary locations it did (both torso locations, the
        // arm and head locations of the row)
        CHECK( covered == as_set( row.sub ) );
        CHECK( it->has_sub_coverage( bodypart_id( "leg_l" ) ) );
        CHECK( it->has_sub_coverage( bodypart_id( "leg_r" ) ) );
    }
}

TEST_CASE( "L2 the derived coverage and encumbrance per body part are DDA's",
           "[armor][subpart][torso2b]" )
{
    clear_all_state();
    for( const draped_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        CHECK( row.parts.size() == parts_of( row ).size() );
        for( const draped_row::part_row &expected : row.parts ) {
            CAPTURE( expected.part );
            const bodypart_id bp( expected.part );
            // DDA b4b62254ae2 portions (coats.json, cloaks.json, ...): the derived number per body part
            CHECK( it->get_coverage( bp ) == expected.coverage );
            const std::optional<armor_portion_data> portion = it->portion_for_bodypart( bp );
            REQUIRE( portion.has_value() );
            CHECK( portion->encumber == expected.encumbrance );
            if( expected.max_encumbrance >= 0 ) {
                CHECK( portion->max_encumber == expected.max_encumbrance );
            } else {
                CHECK( portion->max_encumber >= portion->encumber );
            }
        }
    }
}

TEST_CASE( "L3 flags, warmth and pockets are unchanged", "[armor][subpart][torso2b]" )
{
    clear_all_state();
    for( const draped_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        for( const std::string &flag : row.flags ) {
            CAPTURE( flag );
            CHECK( it->has_flag( flag_id( flag ) ) );
        }
        CHECK( it->get_warmth() == row.warmth );
        CHECK( it->type->pockets.size() == static_cast<size_t>( row.pockets ) );
    }
}

TEST_CASE( "L4 each body part is named by one portion, with or without specifically_covers",
           "[armor][subpart][torso2b]" )
{
    clear_all_state();
    for( const draped_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        const itype &type = itype_id( row.id ).obj();
        REQUIRE( type.armor );
        for( const std::string &part : parts_of( row ) ) {
            CAPTURE( part );
            const bodypart_str_id bp( part );
            // the derived aggregate is the only entry of `data` naming the part; a stray plain
            // `covers` for it would be a second one
            int named = 0;
            for( const armor_portion_data &portion : type.armor->data ) {
                named += portion.covers.test( bp ) ? 1 : 0;
            }
            CHECK( named == 1 );
            int sub_named = 0;
            for( const armor_portion_data &portion : type.armor->sub_data ) {
                sub_named += portion.covers.test( bp ) ? 1 : 0;
            }
            // the legs, and the parts DDA gives a specifically_covers portion (torso split in
            // upper and lower, arm sub-locations), are named by sub_data portions
            int expected_sub_named = 0;
            for( const draped_row::part_row &expected : row.parts ) {
                if( expected.part == part ) {
                    expected_sub_named = expected.sub_named;
                }
            }
            CHECK( sub_named == expected_sub_named );
        }
        std::set<std::string> draped;
        for( const armor_portion_data &portion : type.armor->sub_data ) {
            for( const sub_bodypart_str_id &sbp : portion.sub_coverage ) {
                draped.insert( sbp.str() );
            }
        }
        CHECK( draped == as_set( row.specific ) );
        CHECK( draped.count( "leg_draped_l" ) == 1 );
        CHECK( draped.count( "leg_draped_r" ) == 1 );
    }
}

TEST_CASE( "L5 hakama and hakama_gi cover only the draped legs", "[armor][subpart][torso2b]" )
{
    clear_all_state();
    for( const char *id : { "hakama", "hakama_gi" } ) {
        CAPTURE( id );
        detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
        CHECK( sub_locations( *it ) == std::set<std::string>{ "leg_draped_l", "leg_draped_r" } );
        CHECK( it->covers( bodypart_id( "leg_l" ) ) );
        CHECK( it->covers( bodypart_id( "leg_r" ) ) );
        for( const char *other : { "torso", "arm_l", "arm_r", "head", "hand_l", "hand_r", "foot_l",
                                   "foot_r", "eyes", "mouth"
                                 } ) {
            CAPTURE( other );
            CHECK( !it->covers( bodypart_id( other ) ) );
        }
    }
}

namespace
{

constexpr int leg_hits = 2000;

// Wears the garments in the order given (innermost first) on a cleared player character.
void wear_all( const std::vector<std::string> &worn )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    for( const std::string &id : worn ) {
        guy.wear_item( item::spawn( id ), false );
        REQUIRE( guy.is_wearing( itype_id( id ) ) );
    }
}

// Every absorbed hit wears the garments: keep them whole, so the rates do not fall as they break.
void keep_whole( Character &guy )
{
    for( item * const &it : guy.worn ) {
        it->set_damage( 0 );
    }
}

// Fraction of `hits` left-leg hits of 10 cut that the worn garments reduce.
auto leg_reduced_rate( const std::vector<std::string> &worn, int hits ) -> double
{
    wear_all( worn );
    Character &guy = get_player_character();
    const bodypart_id leg( "leg_l" );
    int reduced = 0;
    for( int i = 0; i < hits; ++i ) {
        keep_whole( guy );
        damage_instance dmg( DT_CUT, 10 );
        guy.absorb_hit( leg, dmg );
        if( dmg.damage_units[0].amount < 10.0f ) {
            ++reduced;
        }
    }
    return static_cast<double>( reduced ) / hits;
}

// The distinct cut amounts that remain of 100 cut after the worn garments.
auto leg_outcomes( const std::vector<std::string> &worn, int hits ) -> std::set<float>
{
    wear_all( worn );
    Character &guy = get_player_character();
    const bodypart_id leg( "leg_l" );
    std::set<float> outcomes;
    for( int i = 0; i < hits; ++i ) {
        keep_whole( guy );
        damage_instance dmg( DT_CUT, 100 );
        guy.absorb_hit( leg, dmg );
        outcomes.insert( dmg.damage_units[0].amount );
    }
    return outcomes;
}

} // namespace

TEST_CASE( "L6 a duster over bare legs takes its coverage share of leg hits",
           "[armor][subpart][torso2b]" )
{
    // DDA coats.json:641: the duster legs portion has coverage 86; the draped location is the
    // whole secondary leg pool (max_coverage 100), so the second pass tests the full 86. (CDF's
    // flat coverage before the conversion was 90.)
    const double rate = leg_reduced_rate( { "duster" }, leg_hits );
    CHECK( rate >= 0.78 );
    CHECK( rate == Approx( 0.86 ).margin( 0.03 ) );
}

TEST_CASE( "L7 jeans under a duster: a leg hit reaches both garments", "[armor][subpart][torso2b]" )
{
    const std::set<float> duster = leg_outcomes( { "duster" }, leg_hits );
    const std::set<float> jeans = leg_outcomes( { "jeans" }, leg_hits );
    const std::set<float> both = leg_outcomes( { "jeans", "duster" }, leg_hits );
    REQUIRE( duster.size() == 2 );
    REQUIRE( jeans.size() == 2 );
    // the jeans act on the first pass (flat), the duster on the second: some hits take off both
    // resistances, below the best either garment alone does
    CHECK( *both.begin() < *duster.begin() );
    CHECK( *both.begin() < *jeans.begin() );
}
