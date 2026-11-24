// DDA source: tests/item_test.cpp (DDA commit b4b62254ae2). Staged for P1-transplant J3, not compiled until J3 lands.
// TEST_CASEs copied (5): item_volume, items_spawn_in_their_default_containers, item_single_type_contents, item_nested_contents, item_rotten_contents
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md). Differences:
// - E1-3 (PC-E1): the E_FILE_STORAGE branch of check_spawning_in_container and the "software"
//   section of item_single_type_contents are back, with the TEST_DATA fixtures test_laptop and
//   test_software_fixture in place of DDA's usb_drive and software_hacking (CDF's usb_drive and
//   software items change shape in E1-10). The two DDA e-file cache cases (tests/item_test.cpp:
//   423-577) are appended: efile_photos / efile_recipes come from TEST_DATA until E1-4, CDF has no
//   stacking_info / tname::segments (stacks_with returns bool, so the bit checks become bool
//   checks) and no ITEM_BROKEN fault (the two "broken" sections are dropped).
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
#include "recipe.h"
#include "subbodypart.h"
#include "test_data.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"

static const item_category_id item_category_clothing( "clothing" );

static const item_category_id item_category_container( "container" );

static const item_category_id item_category_food( "food" );

static const item_category_id item_category_spare_parts( "spare_parts" );

static const item_category_id item_category_tools( "tools" );

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

static const itype_id itype_test_laptop( "test_laptop" );

static const itype_id itype_test_rock( "test_rock" );

static const itype_id itype_test_software_boxed_fixture( "test_software_boxed_fixture" );

static const itype_id itype_test_software_fixture( "test_software_fixture" );

static const itype_id itype_wrapper( "wrapper" );

static const itype_id itype_efile_photos( "efile_photos" );

static const itype_id itype_efile_recipes( "efile_recipes" );

static const recipe_id recipe_2byarm_guard( "2byarm_guard" );

static const recipe_id recipe_armguard_chitin( "armguard_chitin" );

static const recipe_id recipe_balclava( "balclava" );

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
    } else if( test_item.is_software() ) {
        REQUIRE( container_item.is_estorage() );
        const std::vector<const item *> softwares = container_item.softwares();
        CHECK( !softwares.empty() );
        for( const item *itm : softwares ) {
            CHECK( itm->typeId() == test_item.typeId() );
        }
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
    // CDF: E1-3, a fixture takes the place of DDA's software data (the software branch above)
    check_spawning_in_container( itype_test_software_boxed_fixture );
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

    SECTION( "software" ) {
        // CDF: E1-3, test_laptop for DDA's usb_drive, test_software_fixture for software_hacking
        item laptop( itype_test_laptop );
        item software( itype_test_software_fixture );
        REQUIRE( laptop.get_category_of_contents().id == item_category_tools );
        REQUIRE( laptop.put_in( software, pocket_type::E_FILE_STORAGE ).success() );
        CHECK( laptop.get_category_of_contents().id == item_category_tools );
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

// DDA tests/item_test.cpp:408-421
static detached_ptr<item> make_photo_gallery( int n_photos )
{
    detached_ptr<item> gallery = item::spawn( itype_efile_photos );
    std::vector<item::extended_photo_def> photos;
    photos.reserve( n_photos );
    for( int i = 0; i < n_photos; ++i ) {
        item::extended_photo_def photo;
        photo.quality = 3;
        photo.name = string_format( "test_photo_%d", i );
        photo.description = "A test photo for stacking comparison.";
        photos.push_back( photo );
    }
    gallery->write_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS" );
    return gallery;
}

// DDA tests/item_test.cpp:423-516. CDF: stacks_with returns bool (no stacking_info bits), so the
// EMEMORY / VARS bit checks are the bool result; the item model uses detached_ptr.
TEST_CASE( "efile_photos_total_photos_count_cache", "[item][estorage]" )
{
    SECTION( "fresh write caches the count" ) {
        detached_ptr<item> gallery = make_photo_gallery( 5 );
        CHECK( gallery->has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        CHECK( gallery->get_var( "CAMERA_EXTENDED_PHOTOS_count", 0 ) == 5 );
        CHECK( gallery->total_photos() == 5 );
    }

    SECTION( "legacy save without cache var lazy-fills on first call" ) {
        detached_ptr<item> gallery = make_photo_gallery( 7 );
        gallery->erase_var( "CAMERA_EXTENDED_PHOTOS_count" );
        REQUIRE_FALSE( gallery->has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        CHECK( gallery->total_photos() == 7 );
        CHECK( gallery->has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        CHECK( gallery->get_var( "CAMERA_EXTENDED_PHOTOS_count", 0 ) == 7 );
    }

    SECTION( "two galleries with same photos stack" ) {
        detached_ptr<item> a = make_photo_gallery( 3 );
        detached_ptr<item> b = make_photo_gallery( 3 );
        CHECK( a->stacks_with( *b ) );
    }

    SECTION( "mixed cache state still stacks" ) {
        // Cache key is derived; the variables compare must ignore it or lazy-fill desyncs split stacks.
        detached_ptr<item> a = make_photo_gallery( 4 );
        detached_ptr<item> b = make_photo_gallery( 4 );
        b->erase_var( "CAMERA_EXTENDED_PHOTOS_count" );
        REQUIRE( a->has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        REQUIRE_FALSE( b->has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        CHECK( a->stacks_with( *b ) );
    }

    SECTION( "differing photo counts do not stack" ) {
        detached_ptr<item> a = make_photo_gallery( 3 );
        detached_ptr<item> b = make_photo_gallery( 5 );
        REQUIRE( a->total_photos() != b->total_photos() );
        CHECK_FALSE( a->stacks_with( *b ) );
    }

    SECTION( "containers with differing nested galleries do not stack" ) {
        // CDF: test_laptop for DDA's usb_drive
        item a( itype_test_laptop );
        item b( itype_test_laptop );
        REQUIRE( a.put_in( make_photo_gallery( 3 ), pocket_type::E_FILE_STORAGE ).success() );
        REQUIRE( b.put_in( make_photo_gallery( 5 ), pocket_type::E_FILE_STORAGE ).success() );
        CHECK_FALSE( a.stacks_with( b ) );
    }

    SECTION( "occupied_ememory sums every top-level efile" ) {
        item drive( itype_test_laptop );
        detached_ptr<item> g1 = make_photo_gallery( 2 );
        detached_ptr<item> g2 = make_photo_gallery( 5 );
        const units::ememory expected = g1->ememory_size() + g2->ememory_size();
        REQUIRE( drive.put_in( *g1, pocket_type::E_FILE_STORAGE ).success() );
        REQUIRE( drive.put_in( *g2, pocket_type::E_FILE_STORAGE ).success() );
        const bool sum_matches = drive.occupied_ememory() == expected;
        CHECK( sum_matches );
    }

    SECTION( "estorage and non-estorage pair do not stack" ) {
        item drive( itype_test_laptop );
        item rock( itype_test_rock );
        REQUIRE( drive.is_estorage() );
        REQUIRE_FALSE( rock.is_estorage() );
        CHECK_FALSE( drive.stacks_with( rock, false, true ) );
    }

    SECTION( "non-estorage pair is not affected" ) {
        item a( itype_test_rock );
        item b( itype_test_rock );
        REQUIRE_FALSE( a.is_estorage() );
        CHECK( a.stacks_with( b ) );
    }
}

static detached_ptr<item> make_recipe_book( const std::set<recipe_id> &recipes )
{
    detached_ptr<item> book = item::spawn( itype_efile_recipes );
    book->set_saved_recipes( recipes );
    return book;
}

// DDA tests/item_test.cpp:524-577. CDF: the two sections that use flag_ITEM_BROKEN are dropped (no
// is_broken_on_active in CDF); the cache-count part of "set_saved_recipes while broken" stays.
TEST_CASE( "efile_recipes_count_cache", "[item][estorage]" )
{
    const std::set<recipe_id> two = { recipe_balclava, recipe_2byarm_guard };
    const std::set<recipe_id> three = { recipe_balclava, recipe_2byarm_guard, recipe_armguard_chitin };

    SECTION( "set_saved_recipes caches the count" ) {
        detached_ptr<item> book = make_recipe_book( two );
        CHECK( book->has_var( "EIPC_RECIPES_count" ) );
        CHECK( book->get_var( "EIPC_RECIPES_count", 0 ) == 2 );
    }

    SECTION( "legacy save without cache var lazy-fills via ememory_size" ) {
        detached_ptr<item> book = make_recipe_book( three );
        book->erase_var( "EIPC_RECIPES_count" );
        REQUIRE_FALSE( book->has_var( "EIPC_RECIPES_count" ) );
        const bool nonzero = book->ememory_size().value() > 0;
        REQUIRE( nonzero );
        CHECK( book->has_var( "EIPC_RECIPES_count" ) );
        CHECK( book->get_var( "EIPC_RECIPES_count", 0 ) == 3 );
    }

    SECTION( "lazy fill deduplicates malformed CSV" ) {
        item book( itype_efile_recipes );
        book.set_var( "EIPC_RECIPES", std::string( ",balclava,balclava," ) );
        REQUIRE_FALSE( book.has_var( "EIPC_RECIPES_count" ) );
        const bool nonzero = book.ememory_size().value() > 0;
        REQUIRE( nonzero );
        CHECK( book.get_var( "EIPC_RECIPES_count", 0 ) == 1 );
    }

    SECTION( "mixed cache state still stacks" ) {
        detached_ptr<item> a = make_recipe_book( two );
        detached_ptr<item> b = make_recipe_book( two );
        b->erase_var( "EIPC_RECIPES_count" );
        REQUIRE( a->has_var( "EIPC_RECIPES_count" ) );
        REQUIRE_FALSE( b->has_var( "EIPC_RECIPES_count" ) );
        CHECK( a->stacks_with( *b ) );
    }
}
