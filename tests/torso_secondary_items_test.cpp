#include "catch/catch.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
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

// TORSO2: the CDF items that DDA places on torso_hanging_back, torso_hanging_front, torso_waist or
// torso_neck name those locations with DDA's shape (owner ruling: location set, coverage, encumbrance
// and max_encumbrance of the portion follow DDA; resists, warmth, volume, weight and price stay CDF's),
// and the 17 PCH1 items that lost a location get it back.
// tests/data/torso2_expected.json: `sub`, coverage, encumbrance and max_encumbrance (when DDA gives a
// range) are DDA's portion data at b4b62254ae2 (coverage is the torso value derived from the portions,
// see src/item_factory.cpp finalize of sub portions: primary locations weigh by max_coverage, secondary
// ones take the best coverage); flags are the CDF flags at base e3f1c1022d.

namespace
{

struct torso2_row {
    std::string id;
    std::string parent;
    std::vector<std::string> sub;
    int coverage = 0;
    int encumbrance = 0;
    std::optional<int> max_encumbrance;
    std::vector<std::string> flags;
    bool pch1 = false;
};

auto read_rows() -> std::vector<torso2_row>
{
    std::vector<torso2_row> rows;
    const bool read = read_from_file_json( "tests/data/torso2_expected.json", [&]( JsonIn & jsin ) {
        JsonArray all = jsin.get_array();
        while( all.has_more() ) {
            JsonObject jo = all.next_object();
            torso2_row row;
            row.id = jo.get_string( "id" );
            row.parent = jo.get_string( "parent", "" );
            row.sub = jo.get_string_array( "sub" );
            row.coverage = jo.get_int( "coverage" );
            row.encumbrance = jo.get_int( "encumbrance" );
            if( jo.has_int( "max_encumbrance" ) ) {
                row.max_encumbrance = jo.get_int( "max_encumbrance" );
            }
            row.flags = jo.get_string_array( "flags" );
            row.pch1 = jo.get_bool( "pch1", false );
            rows.push_back( row );
        }
    } );
    REQUIRE( read );
    return rows;
}

// the sub-locations of the torso that `it` covers
auto torso_sub_locations( const item &it ) -> std::set<std::string>
{
    std::set<std::string> ids;
    for( const sub_bodypart_id &sbp : it.get_covered_sub_body_parts() ) {
        if( sbp->parent.str() == "torso" ) {
            ids.insert( sbp->id.str() );
        }
    }
    return ids;
}

auto as_set( const std::vector<std::string> &v ) -> std::set<std::string>
{
    return std::set<std::string>( v.begin(), v.end() );
}

auto read_text( const std::string &path ) -> std::string
{
    std::ifstream in( path, std::ios::binary );
    REQUIRE( in.good() );
    return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

} // namespace

TEST_CASE( "torso2 expected data lists every converted item", "[armor][subpart][torso2]" )
{
    const std::vector<torso2_row> rows = read_rows();
    // CDF: 95 table rows (97 minus the 2 held back: heavy_flashlight, flamethrower, which are not
    // wearable in CDF) + 11 copy-from children (rope_30 and its three children stay plain: DDA
    // string.json:115 gives rope_30 no sub-location; heavy_flashlight_on stays with its parent)
    CHECK( rows.size() == 106 );
    int pch1 = 0;
    int children = 0;
    for( const torso2_row &row : rows ) {
        pch1 += row.pch1 ? 1 : 0;
        children += row.parent.empty() ? 0 : 1;
        CHECK( itype_id( row.id ).is_valid() );
    }
    CHECK( pch1 == 17 );
    CHECK( children == 11 );
}

TEST_CASE( "converted items cover the DDA locations of the torso", "[armor][subpart][torso2]" )
{
    clear_all_state();
    for( const torso2_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        CHECK( it->has_sub_coverage( bodypart_id( "torso" ) ) );
        CHECK( torso_sub_locations( *it ) == as_set( row.sub ) );
    }
}

TEST_CASE( "converted items keep their torso coverage and encumbrance", "[armor][subpart][torso2]" )
{
    clear_all_state();
    const bodypart_id torso( "torso" );
    for( const torso2_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        // the numbers of the file are DDA's for the portion (owner ruling); the aggregate derived
        // from the portions must equal them
        CHECK( it->get_coverage( torso ) == row.coverage );
        const std::optional<armor_portion_data> portion = it->portion_for_bodypart( torso );
        REQUIRE( portion.has_value() );
        CHECK( portion->encumber == row.encumbrance );
        if( row.max_encumbrance.has_value() ) {
            CHECK( portion->max_encumber == *row.max_encumbrance );
        }
    }
}

TEST_CASE( "a converted item has one torso portion and no flat torso portion", "[armor][subpart][torso2]" )
{
    clear_all_state();
    const bodypart_str_id torso( "torso" );
    for( const torso2_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        const itype &type = itype_id( row.id ).obj();
        REQUIRE( type.armor );
        int named = 0;
        for( const armor_portion_data &portion : type.armor->data ) {
            named += portion.covers.test( torso ) ? 1 : 0;
        }
        // the aggregate derived from the sub portions is the only portion naming the torso
        CHECK( named == 1 );
        int sub_portions = 0;
        for( const armor_portion_data &portion : type.armor->sub_data ) {
            REQUIRE( !portion.sub_coverage.empty() );
            sub_portions += portion.covers.test( torso ) ? 1 : 0;
        }
        CHECK( sub_portions >= 1 );
    }
}

TEST_CASE( "converted items keep their flags", "[armor][subpart][torso2]" )
{
    clear_all_state();
    for( const torso2_row &row : read_rows() ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        for( const std::string &flag : row.flags ) {
            CAPTURE( flag );
            CHECK( it->has_flag( flag_id( flag ) ) );
        }
    }
}

TEST_CASE( "mixed items cover exactly the DDA primary and secondary locations", "[armor][subpart][torso2]" )
{
    clear_all_state();
    struct mixed_row {
        const char *id;
        std::set<std::string> expected;
    };
    // DDA ammo_pouch.json:808, :868 (heavy_load_bearing_vest, breacher), the sling variants of CDF
    // cdf_pouch_extras.json (DDA ammo_pouch.json load_bearing_vest_sling, heavy_..._sling)
    const std::vector<mixed_row> mixed = {
        { "heavy_load_bearing_vest", { "torso_waist", "torso_upper", "torso_lower" } },
        {
            "heavy_load_bearing_vest_breacher",
            { "torso_hanging_front", "torso_waist", "torso_upper", "torso_lower" }
        },
        {
            "heavy_load_bearing_vest_sling",
            { "torso_hanging_front", "torso_waist", "torso_upper", "torso_lower" }
        },
        { "load_bearing_vest_sling", { "torso_hanging_front", "torso_upper" } },
    };
    for( const mixed_row &row : mixed ) {
        CAPTURE( row.id );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        CHECK( torso_sub_locations( *it ) == row.expected );
        // the primary locations keep the vest's coverage (DDA and CDF agree: 85, pinned in pch1_pouches_test)
        CHECK( it->get_coverage( bodypart_id( "torso" ), sub_bodypart_str_id( "torso_upper" ) ) == 85 );
    }
}

TEST_CASE( "the 17 PCH1 items cover their location and carry no dropped-location note",
           "[armor][subpart][torso2][pch1]" )
{
    clear_all_state();
    // PCH1 comment "//3": the location the item lost because torso_waist, torso_hanging_front and
    // torso_hanging_back did not exist (CDF: data/json/items/armor/cdf_molle_*.json, cdf_pouch_extras.json)
    const std::vector<std::pair<const char *, const char *>> restored = {
        { "heavy_load_bearing_vest", "torso_waist" },
        { "heavy_load_bearing_vest_breacher", "torso_waist" },
        { "heavy_load_bearing_vest_breacher", "torso_hanging_front" },
        { "molle_medium_rucksack", "torso_hanging_back" },
        { "molle_large_rucksack", "torso_hanging_back" },
        { "radio_pouch", "torso_waist" },
        { "gadget_pouch", "torso_waist" },
        { "h2o_pouch", "torso_waist" },
        { "canteen_pouch", "torso_waist" },
        { "flashlight_pouch", "torso_waist" },
        { "tactical_grenade_pouch", "torso_waist" },
        { "tactical_shotshell_pouch", "torso_waist" },
        { "sustainment_pouch", "torso_waist" },
        { "pipe_quiver_large", "torso_hanging_back" },
        { "quiver_simple_cloth", "torso_hanging_back" },
        { "load_bearing_vest_sling", "torso_hanging_front" },
        { "heavy_load_bearing_vest_sling", "torso_hanging_front" },
        { "heavy_load_bearing_vest_sling", "torso_waist" },
        { "ammo_satchel_leather", "torso_waist" },
    };
    std::set<std::string> items;
    for( const auto &[id, location] : restored ) {
        CAPTURE( id, location );
        detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
        CHECK( torso_sub_locations( *it ).count( location ) == 1 );
        items.insert( id );
    }
    CHECK( items.size() == 17 );
    for( const char *path : { "data/json/items/armor/cdf_molle_carriers.json",
                              "data/json/items/armor/cdf_molle_pouches.json",
                              "data/json/items/armor/cdf_pouch_extras.json"
                            } ) {
        CAPTURE( path );
        CHECK( read_text( path ).find( "\"//3\"" ) == std::string::npos );
    }
}

namespace
{
// Fraction of `hits` torso hits of 10 cut that the worn `id` alone reduces.
auto torso_reduced_rate( const std::string &id, int hits ) -> double
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    guy.wear_item( item::spawn( id ), false );
    const bodypart_id torso( "torso" );
    int reduced = 0;
    for( int i = 0; i < hits; ++i ) {
        // every absorbed hit wears the garment: keep it whole, so the rate does not fall as it breaks
        if( guy.worn.empty() ) {
            guy.wear_item( item::spawn( id ), false );
        }
        ( *guy.worn.begin() )->set_damage( 0 );
        damage_instance dmg( DT_CUT, 10 );
        guy.absorb_hit( torso, dmg );
        if( dmg.damage_units[0].amount < 10.0f ) {
            ++reduced;
        }
    }
    return static_cast<double>( reduced ) / hits;
}
} // namespace

TEST_CASE( "a rucksack takes about 36 percent of torso hits on the pack", "[armor][subpart][torso2]" )
{
    constexpr int hits = 10000;
    REQUIRE( item::spawn( "rucksack" )->damage_resist( DT_CUT ) > 0.0f );
    // rucksack coverage 80 (DDA storage.json:2577) x 45% of the secondary pool that is
    // torso_hanging_back (DDA body_parts.json:921-999) = 36% of torso hits
    CHECK( torso_reduced_rate( "rucksack", hits ) == Approx( 0.36 ).margin( 0.03 ) );
    // a flat garment of the same coverage would reduce 80% of them: the pack must be well under that
    CHECK( torso_reduced_rate( "rucksack", hits ) < 0.5 );
}

TEST_CASE( "backpack_giant covers the DDA upper thighs besides the hanging back", "[armor][subpart][torso2]" )
{
    clear_all_state();
    // DDA storage.json:387: torso_hanging_back 100/20 and leg_upper_l/r coverage 60, encumbrance [ 10, 10 ]
    detached_ptr<item> it = item::spawn( itype_id( "backpack_giant" ), calendar::turn );
    for( const char *leg : { "leg_l", "leg_r" } ) {
        CAPTURE( leg );
        const bodypart_id bp( leg );
        CHECK( it->has_sub_coverage( bp ) );
        // leg_upper is 40 of the 100 of a leg: 60 * 40 / 100
        CHECK( it->get_coverage( bp ) == 24 );
        const std::optional<armor_portion_data> portion = it->portion_for_bodypart( bp );
        REQUIRE( portion.has_value() );
        CHECK( portion->encumber == 10 );
        CHECK( portion->max_encumber == 10 );
    }
    CHECK( it->covers( sub_bodypart_str_id( "leg_upper_l" ) ) );
    CHECK( it->covers( sub_bodypart_str_id( "leg_upper_r" ) ) );
    CHECK( !it->covers( sub_bodypart_str_id( "leg_knee_l" ) ) );
}
