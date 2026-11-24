// CDF: PC-E1 step E1-3 (item-level e-file API, stacking, name and dark reading). Tests for the
// item.cpp behaviour of DDA b4b62254ae2: item.cpp:970-979 (stacking), 1517-1521 (dark reading),
// 2709-2890 (predicates, ememory_size, gallery, catalog, photos), 3499-3560 (saved recipes,
// generate_recipes), item_tname.cpp:695-713 (free memory in the name), item_container.cpp:1016-1072
// (aggregated contents). Written before the code.
// DEVIATION: the fixtures efile_photos / efile_recipes live in TEST_DATA (efile_item_fixtures.json)
// until E1-4 adds the real ones; the DDA "broken e-file" cases are dropped (CDF has no ITEM_BROKEN
// fault, the carry-over of E1-2 keeps the browsed check only); is_estorage_usable and the device
// branch of get_saved_recipes need efile_activity_actor::edevice_has_use (real since E1-5b): the
// negative paths are tested here first, the positive ones and the recursion guard at the end
// of the file.
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "color.h"
#include "detached_ptr.h"
#include "flag.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "itype.h"
#include "output.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_battery( "battery" );
static const itype_id itype_efile_photos( "efile_photos" );
static const itype_id itype_efile_recipes( "efile_recipes" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_test_efile_box( "test_efile_box" );
static const itype_id itype_test_efile_etd( "test_efile_etd" );
static const itype_id itype_test_efile_selfcatalog( "test_efile_selfcatalog" );
static const itype_id itype_test_efile_large( "test_efile_large" );
static const itype_id itype_test_efile_medium( "test_efile_medium" );
static const itype_id itype_test_efile_small( "test_efile_small" );
static const itype_id itype_test_laptop( "test_laptop" );
static const itype_id itype_test_laptop_no_dark( "test_laptop_no_dark" );
static const itype_id itype_test_memory_card_fixture( "test_memory_card_fixture" );
static const itype_id itype_test_ebook_reader( "test_ebook_reader" );
static const itype_id itype_test_software_fixture( "test_software_fixture" );

static const recipe_id recipe_2byarm_guard( "2byarm_guard" );
static const recipe_id recipe_balclava( "balclava" );

namespace
{

auto make_photo_gallery( int n_photos ) -> detached_ptr<item>
{
    detached_ptr<item> gallery = item::spawn( itype_efile_photos );
    std::vector<item::extended_photo_def> photos;
    for( int i = 0; i < n_photos; ++i ) {
        item::extended_photo_def photo;
        photo.quality = 3;
        photo.name = string_format( "test_photo_%d", i );
        photo.description = "A test photo.";
        photos.push_back( photo );
    }
    gallery->write_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS" );
    return gallery;
}

auto plain_name( const item &it ) -> std::string
{
    return remove_color_tags( it.tname() );
}

} // namespace

TEST_CASE( "efile_ememory_size_follows_recipe_and_photo_counts", "[efile][item]" )
{
    clear_all_state();
    SECTION( "a recipe catalog costs its type size per stored recipe" ) {
        item catalog( itype_efile_recipes );
        CHECK( catalog.ememory_size() == 0_KB );
        catalog.set_saved_recipes( { recipe_balclava, recipe_2byarm_guard } );
        CHECK( catalog.ememory_size() == 8_MB );
        catalog.set_saved_recipes( { recipe_balclava } );
        CHECK( catalog.ememory_size() == 4_MB );
    }
    SECTION( "a photo gallery costs its type size per photo" ) {
        CHECK( item( itype_efile_photos ).ememory_size() == 0_KB );
        CHECK( make_photo_gallery( 3 )->ememory_size() == 12_MB );
    }
    SECTION( "other files cost their type size" ) {
        CHECK( item( itype_test_efile_medium ).ememory_size() == 24_GB );
        CHECK( item( itype_rock ).ememory_size() == 0_KB );
    }
    SECTION( "the device counts the changed size of its files" ) {
        item laptop( itype_test_laptop );
        REQUIRE( laptop.put_in( make_photo_gallery( 5 ), pocket_type::E_FILE_STORAGE ).success() );
        CHECK( laptop.occupied_ememory() == 20_MB );
        CHECK( laptop.remaining_ememory() == 1_TB - 20_MB );
    }
}

TEST_CASE( "efile_gallery_and_recipe_catalog_are_found_inside_a_device", "[efile][item]" )
{
    clear_all_state();
    item laptop( itype_test_laptop );
    CHECK( laptop.get_photo_gallery() == nullptr );
    CHECK( laptop.get_recipe_catalog() == nullptr );

    REQUIRE( laptop.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );
    CHECK( laptop.get_photo_gallery() == nullptr );
    CHECK( laptop.get_recipe_catalog() == nullptr );

    REQUIRE( laptop.put_in( make_photo_gallery( 2 ), pocket_type::E_FILE_STORAGE ).success() );
    REQUIRE( laptop.put_in( item( itype_efile_recipes ), pocket_type::E_FILE_STORAGE ).success() );

    item *gallery = laptop.get_photo_gallery();
    REQUIRE( gallery != nullptr );
    CHECK( gallery->typeId() == itype_efile_photos );
    CHECK( gallery->total_photos() == 2 );
    const item &const_laptop = laptop;
    REQUIRE( const_laptop.get_photo_gallery() != nullptr );
    REQUIRE( const_laptop.get_recipe_catalog() != nullptr );
    CHECK( const_laptop.get_recipe_catalog()->typeId() == itype_efile_recipes );

    SECTION( "an item with memory card data is its own catalog" ) {
        item card( itype_test_memory_card_fixture );
        CHECK( card.get_recipe_catalog() == &card );
        CHECK( card.get_photo_gallery() == nullptr );
    }
}

TEST_CASE( "efile_extended_photos_read_write_and_count_cache", "[efile][item]" )
{
    clear_all_state();
    item gallery( itype_efile_photos );
    std::vector<item::extended_photo_def> none;

    SECTION( "a missing variable reads as no photos and does not throw" ) {
        CHECK_FALSE( gallery.read_extended_photos( none, "CAMERA_EXTENDED_PHOTOS", false ) );
        CHECK( none.empty() );
        CHECK( gallery.total_photos() == 0 );
        // the lazily computed count is cached
        CHECK( gallery.get_var( "CAMERA_EXTENDED_PHOTOS_count", -1 ) == 0 );
    }

    SECTION( "writing stores the photos and the count, reading returns them" ) {
        std::vector<item::extended_photo_def> photos( 3 );
        photos[0].quality = 1;
        photos[0].name = "first";
        photos[1].name = "second";
        photos[2].description = "third";
        gallery.write_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS" );
        CHECK( gallery.get_var( "CAMERA_EXTENDED_PHOTOS_count", -1 ) == 3 );
        CHECK( gallery.total_photos() == 3 );

        std::vector<item::extended_photo_def> back;
        REQUIRE( gallery.read_extended_photos( back, "CAMERA_EXTENDED_PHOTOS", false ) );
        REQUIRE( back.size() == 3 );
        CHECK( back[0].quality == 1 );
        CHECK( back[0].name == "first" );
        CHECK( back[1].name == "second" );
        CHECK( back[2].description == "third" );

        // insert_at_begin puts the stored photos in front of those already in the vector
        std::vector<item::extended_photo_def> existing( 1 );
        existing[0].name = "existing";
        REQUIRE( gallery.read_extended_photos( existing, "CAMERA_EXTENDED_PHOTOS", true ) );
        REQUIRE( existing.size() == 4 );
        CHECK( existing[0].name == "first" );
        CHECK( existing[3].name == "existing" );
    }

    SECTION( "total_photos sums both photo variables and refills a missing count" ) {
        std::vector<item::extended_photo_def> photos( 2 );
        gallery.write_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS" );
        gallery.write_extended_photos( std::vector<item::extended_photo_def>( 3 ),
                                       "CAMERA_MONSTER_PHOTOS" );
        CHECK( gallery.total_photos() == 5 );
        gallery.erase_var( "CAMERA_EXTENDED_PHOTOS_count" );
        gallery.erase_var( "CAMERA_MONSTER_PHOTOS_count" );
        CHECK( gallery.total_photos() == 5 );
        CHECK( gallery.has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        CHECK( gallery.has_var( "CAMERA_MONSTER_PHOTOS_count" ) );
    }

    SECTION( "unreadable photo data counts as no photos" ) {
        gallery.set_var( "CAMERA_EXTENDED_PHOTOS", std::string( "{ not json" ) );
        CHECK_FALSE( gallery.read_extended_photos( none, "CAMERA_EXTENDED_PHOTOS", false ) );
        CHECK( gallery.total_photos() == 0 );
    }
}

TEST_CASE( "efile_software_category_and_saved_recipes", "[efile][item]" )
{
    clear_all_state();
    CHECK( item( itype_test_software_fixture ).is_software() );
    CHECK_FALSE( item( itype_test_laptop ).is_software() );
    CHECK_FALSE( item( itype_test_efile_small ).is_software() );

    SECTION( "saved recipes are stored as a list with a cached count" ) {
        item card( itype_test_memory_card_fixture );
        CHECK( card.get_saved_recipes().empty() );
        card.set_saved_recipes( { recipe_balclava, recipe_2byarm_guard } );
        const std::set<recipe_id> expected = { recipe_balclava, recipe_2byarm_guard };
        CHECK( card.get_saved_recipes() == expected );
        CHECK( card.has_var( "EIPC_RECIPES" ) );
        CHECK( card.get_var( "EIPC_RECIPES_count", -1 ) == 2 );
    }

    SECTION( "invalid and empty recipe ids are filtered out" ) {
        item card( itype_test_memory_card_fixture );
        card.set_var( "EIPC_RECIPES", std::string( ",balclava,no_such_recipe_in_cdf,balclava," ) );
        const std::set<recipe_id> expected = { recipe_balclava };
        CHECK( card.get_saved_recipes() == expected );
    }

    SECTION( "an unusable device does not report the recipes of its catalog" ) {
        // DDA item.cpp:3499-3523: the catalog counts only for an e-device that can be used
        item laptop( itype_test_laptop );
        item catalog( itype_efile_recipes );
        catalog.set_saved_recipes( { recipe_balclava } );
        REQUIRE( laptop.put_in( catalog, pocket_type::E_FILE_STORAGE ).success() );
        CHECK_FALSE( laptop.is_estorage_usable( get_avatar() ) );
        laptop.set_browsed( false );
        CHECK_FALSE( laptop.is_estorage_usable( get_avatar() ) );
    }
}

TEST_CASE( "efile_generate_recipes_follows_the_memory_card_data", "[efile][item]" )
{
    clear_all_state();
    item card( itype_test_memory_card_fixture );
    REQUIRE( card.get_saved_recipes().empty() );
    card.generate_recipes();
    const std::set<recipe_id> first = card.get_saved_recipes();
    REQUIRE_FALSE( first.empty() );
    // recipes_amount is 3
    CHECK( first.size() <= 3 );
    for( const recipe_id &rid : first ) {
        const recipe &r = rid.obj();
        CAPTURE( rid.str() );
        CHECK_FALSE( r.never_learn );
        CHECK_FALSE( r.obsolete );
        CHECK( r.difficulty >= 1 );
        CHECK( r.difficulty <= 4 );
        CHECK( ( r.category == "CC_FOOD" || r.category == "CC_ELECTRONIC" ) );
    }
    // saved recipes are never picked again
    card.generate_recipes();
    const std::set<recipe_id> second = card.get_saved_recipes();
    CHECK( second.size() >= first.size() );
    for( const recipe_id &rid : first ) {
        CHECK( second.count( rid ) == 1 );
    }

    SECTION( "an item without memory card data generates nothing" ) {
        item rock( itype_rock );
        rock.generate_recipes();
        CHECK( rock.get_saved_recipes().empty() );
        CHECK_FALSE( rock.has_var( "EIPC_RECIPES" ) );
    }
}

TEST_CASE( "efile_devices_stack_only_with_equal_memory", "[efile][item][stacking]" )
{
    clear_all_state();
    SECTION( "items without e-storage are not affected" ) {
        CHECK( item( itype_rock ).stacks_with( item( itype_rock ) ) );
        // an e-device and a plain item never stack, even when the type check is skipped
        CHECK_FALSE( item( itype_test_laptop ).stacks_with( item( itype_rock ), false, true ) );
    }
    SECTION( "empty devices stack, devices with different files do not" ) {
        item a( itype_test_laptop );
        item b( itype_test_laptop );
        CHECK( a.stacks_with( b ) );
        REQUIRE( a.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );
        CHECK_FALSE( a.stacks_with( b ) );
        REQUIRE( b.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );
        CHECK( a.stacks_with( b ) );
        REQUIRE( a.put_in( item( itype_test_efile_medium ), pocket_type::E_FILE_STORAGE ).success() );
        REQUIRE( b.put_in( item( itype_test_efile_large ), pocket_type::E_FILE_STORAGE ).success() );
        REQUIRE( a.occupied_ememory() != b.occupied_ememory() );
        CHECK_FALSE( a.stacks_with( b ) );
    }
    SECTION( "photo galleries with the same number of photos stack" ) {
        detached_ptr<item> a = make_photo_gallery( 3 );
        detached_ptr<item> b = make_photo_gallery( 3 );
        CHECK( a->stacks_with( *b ) );
    }
    SECTION( "the derived count variables are not part of the identity" ) {
        detached_ptr<item> a = make_photo_gallery( 4 );
        detached_ptr<item> b = make_photo_gallery( 4 );
        b->erase_var( "CAMERA_EXTENDED_PHOTOS_count" );
        REQUIRE( a->has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        REQUIRE_FALSE( b->has_var( "CAMERA_EXTENDED_PHOTOS_count" ) );
        CHECK( a->stacks_with( *b ) );

        item c( itype_efile_recipes );
        item d( itype_efile_recipes );
        c.set_saved_recipes( { recipe_balclava } );
        d.set_saved_recipes( { recipe_balclava } );
        d.erase_var( "EIPC_RECIPES_count" );
        CHECK( c.stacks_with( d ) );
    }
    SECTION( "photo galleries with different photo counts do not stack" ) {
        detached_ptr<item> a = make_photo_gallery( 3 );
        detached_ptr<item> b = make_photo_gallery( 5 );
        REQUIRE( a->total_photos() != b->total_photos() );
        CHECK_FALSE( a->stacks_with( *b ) );
    }
    SECTION( "devices that hold different galleries do not stack" ) {
        item a( itype_test_laptop );
        item b( itype_test_laptop );
        REQUIRE( a.put_in( make_photo_gallery( 3 ), pocket_type::E_FILE_STORAGE ).success() );
        REQUIRE( b.put_in( make_photo_gallery( 5 ), pocket_type::E_FILE_STORAGE ).success() );
        CHECK_FALSE( a.stacks_with( b ) );
    }
    SECTION( "the sum of the file sizes is the occupied memory" ) {
        item drive( itype_test_laptop );
        detached_ptr<item> g1 = make_photo_gallery( 2 );
        detached_ptr<item> g2 = make_photo_gallery( 5 );
        const units::ememory expected = g1->ememory_size() + g2->ememory_size();
        REQUIRE( drive.put_in( *g1, pocket_type::E_FILE_STORAGE ).success() );
        REQUIRE( drive.put_in( *g2, pocket_type::E_FILE_STORAGE ).success() );
        CHECK( drive.occupied_ememory() == expected );
    }
}

TEST_CASE( "efile_files_inherit_dark_use_from_the_device", "[efile][item]" )
{
    clear_all_state();
    static const flag_id flag_CAN_USE_IN_DARK( "CAN_USE_IN_DARK" );
    item laptop( itype_test_laptop );
    REQUIRE( laptop.has_flag( flag_CAN_USE_IN_DARK ) );
    REQUIRE( laptop.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );
    // read the file first: the device is not asked about its flags before
    const std::vector<item *> files = laptop.efiles();
    REQUIRE( files.size() == 1 );
    CHECK( files.front()->has_flag( flag_CAN_USE_IN_DARK ) );
    // a file outside of a device does not have the flag
    CHECK_FALSE( item( itype_test_efile_small ).has_flag( flag_CAN_USE_IN_DARK ) );

    SECTION( "a device that cannot be used in the dark passes nothing on" ) {
        item dim( itype_test_laptop_no_dark );
        REQUIRE_FALSE( dim.has_flag( flag_CAN_USE_IN_DARK ) );
        REQUIRE( dim.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );
        const std::vector<item *> dim_files = dim.efiles();
        REQUIRE( dim_files.size() == 1 );
        CHECK_FALSE( dim_files.front()->has_flag( flag_CAN_USE_IN_DARK ) );
    }
}

TEST_CASE( "efile_free_memory_is_part_of_the_item_name", "[efile][item][tname]" )
{
    clear_all_state();
    SECTION( "an unbrowsed device is marked as such" ) {
        item reader( itype_test_ebook_reader );
        CHECK( plain_name( reader ).find( " (unbrowsed)" ) != std::string::npos );
        CHECK( plain_name( reader ).find( "free" ) == std::string::npos );
    }
    SECTION( "a browsed device shows its free memory, coloured by the free share" ) {
        item reader( itype_test_ebook_reader );
        reader.set_browsed( true );
        CHECK( plain_name( reader ).find( " (32.0 GB free)" ) != std::string::npos );
        CHECK( reader.tname().find( colorize( "32.0 GB", c_light_green ) ) != std::string::npos );

        REQUIRE( reader.put_in( item( itype_test_efile_medium ), pocket_type::E_FILE_STORAGE ).success() );
        CHECK( plain_name( reader ).find( " (8.0 GB/32.0 GB free)" ) != std::string::npos );
        CHECK( reader.tname().find( colorize( "8.0 GB/32.0 GB", c_light_red ) ) != std::string::npos );
    }
    SECTION( "a half full device is yellow" ) {
        item laptop( itype_test_laptop );
        laptop.set_browsed( true );
        REQUIRE( laptop.put_in( item( itype_test_efile_large ), pocket_type::E_FILE_STORAGE ).success() );
        CHECK( plain_name( laptop ).find( " (500.0 GB/1.0 TB free)" ) != std::string::npos );
        CHECK( laptop.tname().find( colorize( "500.0 GB/1.0 TB", c_yellow ) ) != std::string::npos );
    }
    SECTION( "items without e-storage show nothing" ) {
        const std::string name = plain_name( item( itype_rock ) );
        CHECK( name.find( "free)" ) == std::string::npos );
        CHECK( name.find( "unbrowsed" ) == std::string::npos );
    }
}

TEST_CASE( "efile_files_show_in_the_contents_of_a_browsed_container_like_item", "[efile][item][tname]" )
{
    clear_all_state();
    static const item_category_id item_category_container( "container" );
    static const item_category_id item_category_e_files( "e_files" );
    item box( itype_test_efile_box );
    REQUIRE( box.get_category_of_contents().id == item_category_container );
    REQUIRE( box.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );

    SECTION( "unbrowsed: the files are not shown" ) {
        CHECK( box.get_category_of_contents().id == item_category_container );
        CHECK( plain_name( box ).find( " > " ) == std::string::npos );
    }
    SECTION( "browsed: the files give the category and the name segment" ) {
        box.set_browsed( true );
        CHECK( box.get_category_of_contents().id == item_category_e_files );
        const std::string name = plain_name( box );
        CHECK( name.find( " > " ) != std::string::npos );
        CHECK( name.find( "small test file" ) != std::string::npos );
    }
    SECTION( "browsed with several files the count is shown" ) {
        box.set_browsed( true );
        REQUIRE( box.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );
        REQUIRE( box.put_in( item( itype_test_efile_small ), pocket_type::E_FILE_STORAGE ).success() );
        const std::string name = plain_name( box );
        CHECK( name.find( " > 3 " ) != std::string::npos );
    }
}

// ---------------------------------------------------------------------------------------------
// E1-5b: the positive paths E1-3 could not test before efile_activity_actor::edevice_has_use was
// real (DDA item.cpp:2709-2823 is_estorage_usable, :3499-3523 get_saved_recipes).
// ---------------------------------------------------------------------------------------------

TEST_CASE( "efile_a_browsed_charged_device_is_usable", "[efile][item][edevice]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    clear_avatar();
    item &laptop = who.i_add( item::spawn( itype_test_laptop ) );

    SECTION( "needs to be browsed" ) {
        laptop.ammo_set( itype_battery, 10 );
        CHECK_FALSE( laptop.is_estorage_usable( who ) );
        laptop.set_browsed( true );
        CHECK( laptop.is_estorage_usable( who ) );
    }
    SECTION( "needs a charge" ) {
        laptop.set_browsed( true );
        CHECK_FALSE( laptop.is_estorage_usable( who ) );
        laptop.ammo_set( itype_battery, 1 );
        CHECK( laptop.is_estorage_usable( who ) );
    }
    SECTION( "needs the E_FILE_DEVICE use action" ) {
        item &card = who.i_add( item::spawn( itype_test_efile_etd ) );
        card.set_browsed( true );
        CHECK_FALSE( card.is_estorage_usable( who ) );
        item rock( itype_rock );
        CHECK_FALSE( rock.is_estorage_usable( who ) );
    }
}

TEST_CASE( "efile_a_device_reports_the_recipes_of_its_catalog", "[efile][item][edevice]" )
{
    clear_all_state();
    const std::set<recipe_id> expected = { recipe_balclava, recipe_2byarm_guard };
    item catalog( itype_efile_recipes );
    catalog.set_saved_recipes( expected );

    SECTION( "a device with the use action" ) {
        item laptop( itype_test_laptop );
        REQUIRE( laptop.put_in( catalog, pocket_type::E_FILE_STORAGE ).success() );
        CHECK( laptop.get_saved_recipes() == expected );
        // the device's own variable is not read once a catalog is there
        laptop.set_var( "EIPC_RECIPES", std::string( "2byarm_guard" ) );
        CHECK( laptop.get_saved_recipes() == expected );
    }
    SECTION( "a device without the use action keeps its own recipes" ) {
        item card( itype_test_efile_etd );
        REQUIRE( card.put_in( catalog, pocket_type::E_FILE_STORAGE ).success() );
        CHECK( card.get_saved_recipes().empty() );
    }
    SECTION( "a device without a catalog" ) {
        item laptop( itype_test_laptop );
        REQUIRE( laptop.put_in( item( itype_test_efile_small ),
                                pocket_type::E_FILE_STORAGE ).success() );
        CHECK( laptop.get_saved_recipes().empty() );
    }
}

// DDA has no guard here (no item has both). get_saved_recipes -> get_recipe_catalog -> the item
// itself -> get_saved_recipes looped once edevice_has_use was true (E1-3 report).
TEST_CASE( "efile_a_device_that_is_its_own_catalog_does_not_recurse", "[efile][item][edevice]" )
{
    clear_all_state();
    const std::set<recipe_id> expected = { recipe_balclava };
    item device( itype_test_efile_selfcatalog );
    REQUIRE( device.get_recipe_catalog() == &device );
    CHECK( efile_activity_actor::edevice_has_use( &device ) );
    CHECK( device.get_saved_recipes().empty() );
    device.set_saved_recipes( expected );
    CHECK( device.get_saved_recipes() == expected );

    // generate_recipes reads the saved recipes first
    device.generate_recipes();
    CHECK_FALSE( device.get_saved_recipes().empty() );
    CHECK( device.get_saved_recipes().count( recipe_balclava ) == 1 );

    // a catalog file stored inside it is not consulted: the item's own list wins
    item catalog( itype_efile_recipes );
    catalog.set_saved_recipes( { recipe_2byarm_guard } );
    REQUIRE( device.put_in( catalog, pocket_type::E_FILE_STORAGE ).success() );
    CHECK( device.get_recipe_catalog() == &device );
    CHECK( device.get_saved_recipes().count( recipe_balclava ) == 1 );
}
