// DDA source: tests/item_test.cpp (DDA commit b4b62254ae2). Staged for P1-transplant J3, not compiled until J3 lands.
// TEST_CASEs copied (5): item_volume, items_spawn_in_their_default_containers, item_single_type_contents, item_nested_contents, item_rotten_contents
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md). Differences:
// - E1 (plan D16): the E_FILE_STORAGE branch of check_spawning_in_container and the "software"
//   section of item_single_type_contents are cut, there is no e-file storage in CDF.
// - BN has no item variants: the two rock variants are the TEST_DATA items test_rock_blue and
//   test_rock_green (copies of test_rock) instead of set_itype_variant.
// - item::in_its_container( qty ) is the static detached form.
#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "detached_ptr.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "enum_bitset.h"
#include "enums.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "item_category.h"
#include "item_factory.h"
#include "itype.h"
#include "material.h"
#include "math_defines.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "requirements.h"
#include "ret_val.h"
#include "string_formatter.h"
#include "subbodypart.h"
#include "test_data.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"

static const item_category_id item_category_clothing( "clothing" );

static const item_category_id item_category_container( "container" );

static const item_category_id item_category_food( "food" );

static const item_category_id item_category_spare_parts( "spare_parts" );

static const itype_id itype_bag_plastic( "bag_plastic" );

static const itype_id itype_battery( "battery" );

static const itype_id itype_butter( "butter" );

static const itype_id itype_chem_black_powder( "chem_black_powder" );

static const itype_id itype_chem_muriatic_acid( "chem_muriatic_acid" );

static const itype_id itype_hammer( "hammer" );

static const itype_id itype_jeans( "jeans" );

static const itype_id itype_rocuronium( "rocuronium" );

static const itype_id itype_single_malt_whiskey( "single_malt_whiskey" );

static const itype_id itype_test_rock_blue( "test_rock_blue" );

static const itype_id itype_test_rock_green( "test_rock_green" );

static const itype_id itype_walnut( "walnut" );

static const itype_id itype_water( "water" );

static const itype_id itype_wrapper( "wrapper" );

// item_volume is identical to the case in tests/item_test.cpp (BN origin), so it is not repeated here.

static void check_spawning_in_container( const itype_id &item_type )
{
    item test_item( item_type );
    REQUIRE( test_item.type->default_container );
    detached_ptr<item> container_det = item::in_its_container( item::spawn( test_item ), 1 );
    item &container_item = *container_det;
    CHECK( container_item.typeId() == *test_item.type->default_container );
    if( container_item.is_container() ) {
        CHECK( container_item.has_item_with( [&test_item]( const item & it ) {
            return it.typeId() == test_item.typeId();
        } ) );
    } else {
        FAIL( "Not container or software storage." );
    }
}

TEST_CASE( "items_spawn_in_their_default_containers", "[item]" )
{
    check_spawning_in_container( itype_water );
    check_spawning_in_container( itype_single_malt_whiskey );
    check_spawning_in_container( itype_rocuronium );
    check_spawning_in_container( itype_chem_muriatic_acid );
    check_spawning_in_container( itype_chem_black_powder );
}

TEST_CASE( "item_single_type_contents", "[item]" )
{
    std::array<item, 2> rocks = { item( itype_test_rock_blue ), item( itype_test_rock_green ) };
    item walnut( itype_walnut );
    item bag( itype_bag_plastic );
    REQUIRE( bag.get_category_of_contents().id == item_category_container );
    int const num = GENERATE( 1, 2 );
    bool ret = true;
    for( int i = 0; i < num; i++ ) {
        ret &= bag.put_in( rocks[i], pocket_type::CONTAINER ).success();
    }
    REQUIRE( ret );
    CAPTURE( num, bag.display_name() );
    CHECK( bag.get_category_of_contents() == *item_category_spare_parts );
    REQUIRE( walnut.get_category_of_contents().id != rocks[0].get_category_of_contents().id );
    REQUIRE( bag.put_in( walnut, pocket_type::CONTAINER ).success() );
    if( num == 1 ) {
        // 1 rock and 1 walnut - nothing dominates
        CHECK( bag.get_category_of_contents().id == item_category_container );
    } else {
        // 2 rock and 1 walnuts - rocks dominate
        CHECK( bag.get_category_of_contents().id == item_category_spare_parts );
        REQUIRE( bag.put_in( walnut, pocket_type::CONTAINER ).success() );
        item hammer( itype_hammer );
        REQUIRE( hammer.get_category_of_contents().id != rocks[0].get_category_of_contents().id );
        REQUIRE( hammer.get_category_of_contents().id != walnut.get_category_of_contents().id );
        REQUIRE( bag.put_in( hammer, pocket_type::CONTAINER ).success() );
        // no dominant category anymore - revert to container
        CHECK( bag.get_category_of_contents().id == item_category_container );
    }

    SECTION( "clothing" ) {
        item jeans( itype_jeans );
        REQUIRE( jeans.get_category_of_contents().id == item_category_clothing );
        REQUIRE( walnut.get_category_of_contents().id == item_category_food );
        REQUIRE( jeans.put_in( walnut, pocket_type::CONTAINER ).success() );
        CHECK( jeans.get_category_of_contents().id == item_category_clothing );
    }
}

TEST_CASE( "item_nested_contents", "[item]" )
{
    item walnut( itype_walnut );
    item outer_bag( itype_bag_plastic );
    item inner_bag1( itype_bag_plastic );
    item inner_bag2( itype_bag_plastic );

    REQUIRE( inner_bag1.put_in( walnut, pocket_type::CONTAINER ).success() );
    REQUIRE( inner_bag1.put_in( walnut, pocket_type::CONTAINER ).success() );
    CHECK( inner_bag1.get_category_of_contents().id == item_category_food );

    REQUIRE( inner_bag2.put_in( walnut, pocket_type::CONTAINER ).success() );
    CHECK( inner_bag2.get_category_of_contents().id == item_category_food );

    REQUIRE( outer_bag.put_in( inner_bag1, pocket_type::CONTAINER ).success() );
    REQUIRE( outer_bag.put_in( inner_bag2, pocket_type::CONTAINER ).success() );
    CAPTURE( outer_bag.display_name() );
    // outer_bag
    //   inner_bag1
    //     walnut
    //     walnut
    //   inner_bag2
    //     walnut
    CHECK( outer_bag.get_category_of_contents().id == item_category_food );
}

TEST_CASE( "item_rotten_contents", "[item]" )
{
    item wrapper( itype_wrapper );
    REQUIRE( wrapper.get_category_of_contents().id == item_category_container );

    item butter_rotten( itype_butter );
    butter_rotten.set_relative_rot( 1.01 );
    REQUIRE( wrapper.put_in( butter_rotten, pocket_type::CONTAINER ).success() );
    REQUIRE( wrapper.put_in( butter_rotten, pocket_type::CONTAINER ).success() );
    CAPTURE( wrapper.display_name() );
    CHECK( wrapper.get_category_of_contents().id == item_category_food );

    item butter( itype_butter );
    butter.set_relative_rot( 0.5 );
    REQUIRE( wrapper.put_in( butter, pocket_type::CONTAINER ).success() );
    CAPTURE( wrapper.display_name() );
    // wrapper
    //   butter (rotten)
    //   butter (rotten)
    //   butter
    CHECK( wrapper.get_category_of_contents().id == item_category_food );
}
