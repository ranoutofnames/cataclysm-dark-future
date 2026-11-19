#include "bodypart.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "enums.h"
#include "item.h"
#include "itype.h"
#include "state_helpers.h"
#include "units.h"
#include "itype_slots/islot_armor.h"

#include <map>
#include <set>
#include <string>

// HEAD2 loader: `encumbrance_modifiers` (a head encumbrance derived from the item weight, DDA
// item_factory.cpp:1302-1330, itype.cpp:440-507) and the portion `layers` key (stored only).
// Fixtures: data/mods/TEST_DATA/head2_loader_p3.json. Expected numbers are worked out by hand from the
// head table (1 mg 1, 300 g 4, 800 g 8, ...), e.g. 550 g is 4 + 4 * 250 / 500 = 6.

namespace
{

// The per-body-part head portion of an item type (the derived one when the item has sub portions).
auto head_portion( const std::string &id ) -> const armor_portion_data *
{
    const itype &type = itype_id( id ).obj();
    REQUIRE( type.armor );
    for( const armor_portion_data &portion : type.armor->data ) {
        if( portion.covers.test( bodypart_str_id( "head" ) ) ) {
            return &portion;
        }
    }
    return nullptr;
}

auto head_encumber( const std::string &id ) -> int
{
    const armor_portion_data *portion = head_portion( id );
    REQUIRE( portion != nullptr );
    return portion->encumber;
}

} // namespace

TEST_CASE( "head2_head_table_loads", "[armor][head2][loader]" )
{
    const std::map<units::mass, int> &table = bodypart_str_id( "head" )->encumbrance_per_weight;
    const std::map<units::mass, int> expected = {
        { 1_milligram, 1 }, { 300_gram, 4 }, { 800_gram, 8 }, { 1300_gram, 20 },
        { 1600_gram, 30 }, { 2200_gram, 50 }, { 5600_gram, 80 }, { 10000_gram, 160 }
    };
    CHECK( table.size() == 8 );
    CHECK( table == expected );
    // other parts have no table
    CHECK( bodypart_str_id( "torso" )->encumbrance_per_weight.empty() );
}

TEST_CASE( "head2_weight_to_encumbrance", "[armor][head2][loader]" )
{
    // 1 g is below the first interpolation step: 1 + 3 * (1 g - 1 mg) / (300 g - 1 mg) rounds to 1
    CHECK( head_encumber( "p3h_1g_none" ) == 1 );
    CHECK( head_encumber( "p3h_300g_none" ) == 4 );
    // halfway between 4 (300 g) and 8 (800 g) is 6; a floor or a lower-point lookup gives 4 or 5
    CHECK( head_encumber( "p3h_550g_none" ) == 6 );
    CHECK( head_encumber( "p3h_5600g_none" ) == 80 );
    // 400 g: 4 + 4 * 100 / 500 = 4.8, rounded 5 (a floor in the interpolation gives 4)
    CHECK( head_encumber( "p3h_400g_none" ) == 5 );
}

TEST_CASE( "head2_modifiers", "[armor][head2][loader]" )
{
    CHECK( head_encumber( "p3h_550g_imbalanced" ) == 16 );
    CHECK( head_encumber( "p3h_550g_neck" ) == 16 );
    // 6 * 0.8 = 4.8, rounded 5
    CHECK( head_encumber( "p3h_550g_supported" ) == 5 );
    CHECK( head_encumber( "p3h_550g_neck_supported" ) == 15 );
    CHECK( head_encumber( "p3h_5600g_supported" ) == 64 );
}

TEST_CASE( "head2_two_portions_concatenate", "[armor][head2][loader]" )
{
    // forehead and crown each name RESTRICTS_NECK and WELL_SUPPORTED: 6 * 0.6 = 3.6, 4, plus 20
    const armor_portion_data *portion = head_portion( "p3h_550g_two_portions" );
    REQUIRE( portion != nullptr );
    CHECK( portion->encumber_modifiers.size() == 4 );
    CHECK( portion->encumber == 24 );
}

TEST_CASE( "head2_varsize_and_floor", "[armor][head2][loader]" )
{
    // 6 doubled is 12 (the +10 cap is 16); 30 doubled is 60, capped at 40
    CHECK( head_encumber( "p3h_550g_varsize" ) == 12 );
    CHECK( head_encumber( "p3h_1600g_varsize" ) == 40 );
    // 1 * 0.8 = 0.8 rounds to 1 and is never below 1
    CHECK( head_encumber( "p3h_1g_supported" ) == 1 );
}

TEST_CASE( "head2_modifiers_win_and_max_encumber", "[armor][head2][loader]" )
{
    // encumbrance 30 is given too and ignored: 300 g is 4
    CHECK( head_encumber( "p3h_300g_both_keys" ) == 4 );
    // a 1000 ml non-rigid pocket adds 1000 / 250 = 4 to the full-storage encumbrance
    const armor_portion_data *portion = head_portion( "p3h_300g_pocket" );
    REQUIRE( portion != nullptr );
    CHECK( portion->encumber == 4 );
    CHECK( portion->max_encumber == portion->encumber + 4 );
    // without a pocket the maximum is the encumbrance
    const armor_portion_data *plain = head_portion( "p3h_300g_none" );
    REQUIRE( plain != nullptr );
    CHECK( plain->max_encumber == plain->encumber );
}

TEST_CASE( "head2_no_table_debugmsg", "[armor][head2][loader]" )
{
    armor_portion_data portion;
    portion.encumber_modifiers = { encumbrance_modifier::NONE };
    int value = 0;
    const std::string captured = capture_debugmsg_during( [&]() {
        // CDF: the eyes have no table; DDA gives 100 and an error
        value = portion.calc_encumbrance( 300_gram, bodypart_id( "eyes" ) );
    } );
    CHECK( captured.find( "Can't find a notable point" ) != std::string::npos );
    CHECK( value == 100 );
}

TEST_CASE( "head2_portion_layers_load", "[armor][head2][loader]" )
{
    const armor_portion_data *portion = head_portion( "p3h_layers_plain" );
    REQUIRE( portion != nullptr );
    CHECK( portion->layers == std::set<layer_level>{ layer_level::OUTER_LAYER } );
    // no key, no layers
    const armor_portion_data *none = head_portion( "p3h_300g_none" );
    REQUIRE( none != nullptr );
    CHECK( none->layers.empty() );
}

TEST_CASE( "head2_layers_union", "[armor][head2][loader]" )
{
    const armor_portion_data *portion = head_portion( "p3h_layers_union" );
    REQUIRE( portion != nullptr );
    CHECK( portion->layers == std::set<layer_level>{ layer_level::OUTER_LAYER, layer_level::BELTED_LAYER } );
    // the first sub portion only has OUTER, the second none: the union is OUTER
    const armor_portion_data *first = head_portion( "p3h_layers_first_only" );
    REQUIRE( first != nullptr );
    CHECK( first->layers == std::set<layer_level>{ layer_level::OUTER_LAYER } );
}

TEST_CASE( "head2_numeric_only_unchanged", "[armor][head2][loader]" )
{
    // a portion with `encumbrance` and no modifiers keeps the number at any weight
    CHECK( head_encumber( "p3h_numeric_light" ) == 10 );
    CHECK( head_encumber( "p3h_numeric_heavy" ) == 10 );
}
