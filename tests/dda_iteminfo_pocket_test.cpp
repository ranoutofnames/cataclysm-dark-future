// DDA source: tests/iteminfo_test.cpp (DDA commit b4b62254ae2), cases pocket_info_for_a_simple_container,
// pocket_info_units_-_imperial_or_metric, pocket_info_for_a_multi-pocket_item, vol_to_info and
// weight_to_info. The text must match DDA exactly. The DDA pocket values of test_waterskin and
// test_jug_plastic are on the twins test_dda_waterskin and test_dda_jug_plastic (the CDF fixtures
// of those ids carry BN container data and are used by BN tests).
// item_rigidity is not repeated: CDF has the BN case of that name in tests/iteminfo_test.cpp.
// CUT ammo_restriction_info: it needs the DDA magazine model of P3 (item::is_magazine for guns and
// item::ammo_types derived from pockets) and the item "match"; BN guns and quivers are not magazines.
// Translated for CDF (TT1-TT8): items are heap objects, the DDA item_info_str helper is rebuilt
// on item::info_string.
#include <string>
#include <vector>

#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_test_jug_plastic( "test_dda_jug_plastic" );
static const itype_id itype_test_tool_belt_pocket_mix( "test_tool_belt_pocket_mix" );
static const itype_id itype_test_waterskin( "test_dda_waterskin" );

// Call the info() function on an item with given flags, and return the formatted string.
static std::string item_info_str( const item &it, const std::vector<iteminfo_parts> &part_flags )
{
    const iteminfo_query query( part_flags );
    return it.info_string( query, 1 );
}

// Functions:
// item_contents::info
// item_pocket::general_info
TEST_CASE( "pocket_info_for_a_simple_container", "[iteminfo][pocket][container]" )
{
    clear_all_state();

    detached_ptr<item> test_waterskin = item::spawn( itype_test_waterskin );
    std::vector<iteminfo_parts> pockets = { iteminfo_parts::DESCRIPTION_POCKETS };

    override_option opt_vol( "VOLUME_UNITS", "l" );
    override_option opt_weight( "USE_METRIC_WEIGHTS", "kg" );
    override_option opt_dist( "DISTANCE_UNITS", "metric" );

    // Simple containers with only one pocket show a "Total capacity" section
    // with all the pocket restrictions and other attributes
    CHECK( item_info_str( *test_waterskin, pockets ) ==
           "--\n"
           "<color_c_white>Total capacity</color>:\n"
           "Volume: <color_c_yellow>1.50</color> L  Weight: <color_c_yellow>3.00</color> kg\n"
           "Item length: <color_c_yellow>0</color> cm to <color_c_yellow>12</color> cm\n"
           "Maximum item volume: <color_c_yellow>0.015</color> L\n"
           "Base moves to remove item: <color_c_yellow>220</color>\n"
           "This pocket can <color_c_cyan>contain a liquid</color>.\n" );
}

// Functions:
// item_contents::info
// item_pocket::general_info
TEST_CASE( "pocket_info_units_-_imperial_or_metric", "[iteminfo][pocket][units]" )
{
    clear_all_state();

    detached_ptr<item> test_jug = item::spawn( itype_test_jug_plastic );
    std::vector<iteminfo_parts> pockets = { iteminfo_parts::DESCRIPTION_POCKETS };

    GIVEN( "metric units" ) {
        override_option opt_vol( "VOLUME_UNITS", "l" );
        override_option opt_weight( "USE_METRIC_WEIGHTS", "kg" );
        override_option opt_dist( "DISTANCE_UNITS", "metric" );

        THEN( "pocket capacity is shown in liters, kilograms, and millimeters" ) {
            CHECK( item_info_str( *test_jug, pockets ) ==
                   "--\n"
                   "<color_c_white>Total capacity</color>:\n"
                   "Volume: <color_c_yellow>3.75</color> L  Weight: <color_c_yellow>5.00</color> kg\n"
                   "Item length: <color_c_yellow>0</color> mm to <color_c_yellow>226</color> mm\n"
                   "Maximum item volume: <color_c_yellow>0.015</color> L\n"
                   "Base moves to remove item: <color_c_yellow>100</color>\n"
                   "This pocket is <color_c_cyan>rigid</color>.\n"
                   "This pocket can <color_c_cyan>contain a liquid</color>.\n" );
        }
    }

    GIVEN( "imperial units" ) {
        override_option opt_vol( "VOLUME_UNITS", "qt" );
        override_option opt_weight( "USE_METRIC_WEIGHTS", "lbs" );
        override_option opt_dist( "DISTANCE_UNITS", "imperial" );

        THEN( "pocket capacity is shown in quarts, pounds, and inches" ) {
            CHECK( item_info_str( *test_jug, pockets ) ==
                   "--\n"
                   "<color_c_white>Total capacity</color>:\n"
                   "Volume: <color_c_yellow>3.97</color> qt  Weight: <color_c_yellow>11.02</color> lbs\n"
                   "Item length: <color_c_yellow>0</color> in. to <color_c_yellow>8</color> in.\n"
                   "Maximum item volume: <color_c_yellow>0.016</color> qt\n"
                   "Base moves to remove item: <color_c_yellow>100</color>\n"
                   "This pocket is <color_c_cyan>rigid</color>.\n"
                   "This pocket can <color_c_cyan>contain a liquid</color>.\n" );
        }
    }
}

// Functions:
// item_contents::info
// item_pocket::general_info
TEST_CASE( "pocket_info_for_a_multi-pocket_item", "[iteminfo][pocket][multiple]" )
{
    clear_all_state();

    detached_ptr<item> test_belt = item::spawn( itype_test_tool_belt_pocket_mix );
    std::vector<iteminfo_parts> pockets = { iteminfo_parts::DESCRIPTION_POCKETS };

    override_option opt_vol( "VOLUME_UNITS", "l" );
    override_option opt_weight( "USE_METRIC_WEIGHTS", "kg" );
    override_option opt_dist( "DISTANCE_UNITS", "metric" );

    // When multiple pockets have the same attributes, they are combined headings like:
    //
    //  Pockets 1, 2, and 3
    //  Volume: ...  Weight: ...
    //
    // The "Total capacity" indicates the sum Volume/Weight capacity of all pockets.
    CHECK( item_info_str( *test_belt, pockets ) ==
           "--\n"
           "<color_c_white>Total capacity</color>:\n"
           "Volume: <color_c_yellow>7.00</color> L  Weight: <color_c_yellow>9.00</color> kg\n"
           "--\n"
           "<color_c_white>Pocket 1</color>\n"
           "Volume: <color_c_yellow>1.00</color> L  Weight: <color_c_yellow>1.50</color> kg\n"
           "Item length: <color_c_yellow>0</color> cm to <color_c_yellow>40</color> cm\n"
           "Base moves to remove item: <color_c_yellow>100</color>\n"
           "--\n"
           "<color_c_white>Pockets 2 and 3</color>\n"
           "Volume: <color_c_yellow>1.50</color> L  Weight: <color_c_yellow>1.50</color> kg\n"
           "Item length: <color_c_yellow>0</color> cm to <color_c_yellow>70</color> cm\n"
           "Minimum item volume: <color_c_yellow>0.050</color> L\n"
           "Base moves to remove item: <color_c_yellow>50</color>\n"
           "This is a <color_c_cyan>holster</color>, it only holds <color_c_cyan>one item at a time</color>.\n"
           "<color_c_white>Restrictions</color>:\n"
           "* Item must clip onto a belt loop\n"
           "* <color_c_white>or</color> Item must fit in a sheath\n"
           "--\n"
           "<color_c_white>Pockets 4, 5, and 6</color>\n"
           "Volume: <color_c_yellow>1.00</color> L  Weight: <color_c_yellow>1.50</color> kg\n"
           "Item length: <color_c_yellow>0</color> cm to <color_c_yellow>70</color> cm\n"
           "Minimum item volume: <color_c_yellow>0.050</color> L\n"
           "Base moves to remove item: <color_c_yellow>50</color>\n"
           "This is a <color_c_cyan>holster</color>, it only holds <color_c_cyan>one item at a time</color>.\n"
           "<color_c_white>Restrictions</color>:\n"
           "* Item must clip onto a belt loop\n"
           "* <color_c_white>or</color> Item must fit in a sheath\n" );
}

// Functions:
// vol_to_info from item.cpp
TEST_CASE( "vol_to_info", "[iteminfo][volume]" )
{
    clear_all_state();

    override_option opt_vol( "VOLUME_UNITS", "l" );
    iteminfo vol = vol_to_info( "BASE", "Volume", 3_liter );
    // strings
    CHECK( vol.sType == "BASE" );
    CHECK( vol.sName == "Volume" );
    CHECK( vol.sFmt == "<num> L" );
    CHECK( vol.sValue == "3.00" );
    // doubles
    CHECK( vol.dValue == 3.0 );
    // booleans
    CHECK( vol.bDrawName == true );
    CHECK( vol.bLowerIsBetter == true );
    CHECK( vol.bNewLine == false );
    CHECK( vol.bShowPlus == false );
    CHECK( vol.is_int == false );
    CHECK( vol.three_decimal == false );
}

// Functions:
// weight_to_info from item.cpp
TEST_CASE( "weight_to_info", "[iteminfo][weight]" )
{
    clear_all_state();

    override_option opt( "USE_METRIC_WEIGHTS", "kg" );
    iteminfo wt = weight_to_info( "BASE", "Weight", 3_kilogram );
    // strings
    CHECK( wt.sType == "BASE" );
    CHECK( wt.sName == "Weight" );
    CHECK( wt.sFmt == "<num> kg" );
    CHECK( wt.sValue == "3.00" );
    // doubles
    CHECK( wt.dValue == 3.0 );
    // booleans
    CHECK( wt.bDrawName == true );
    CHECK( wt.bLowerIsBetter == true );
    CHECK( wt.bNewLine == false );
    CHECK( wt.bShowPlus == false );
    CHECK( wt.is_int == false );
    CHECK( wt.three_decimal == false );
}

static const itype_id itype_test_quiver("test_quiver");

TEST_CASE("ammo_restriction_info", "[iteminfo][ammo_restriction]") {
    clear_all_state();

    SECTION("container pocket with ammo restriction") {
        std::vector<iteminfo_parts> pockets = {iteminfo_parts::DESCRIPTION_POCKETS};
        detached_ptr<item> quiver = item::spawn(itype_test_quiver);
        REQUIRE_FALSE(quiver->is_magazine());
        REQUIRE_FALSE(quiver->ammo_types().empty());
        CHECK(
            item_info_str(*quiver, pockets)
            == "--\n"
               "<color_c_white>Total capacity</color>:\n"
               "Holds: <color_c_yellow>20</color> rounds of arrows\n"
               "Holds: <color_c_yellow>20</color> rounds of bolts\n"
               "Base moves to remove item: <color_c_yellow>20</color>\n");
    }
    // DDA's "magazine pocket with ammo restriction" section needs guns/tools as magazines
    // (is_magazine on test_matches/test_compbow); not available in BN (P3).
}
