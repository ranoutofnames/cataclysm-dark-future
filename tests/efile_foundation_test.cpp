// CDF: PC-E1 step E1-1 (E-file engine foundations). Tests for the declarations, units, pocket
// type, loaders and the trivial item predicates of the DDA e-file engine (DDA b4b62254ae2).
// Written before the code: none of this compiled before E1-1.
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "debug.h"
#include "enum_conversions.h"
#include "flag.h"
#include "game.h"
#include "game_inventory.h"
#include "player_helpers.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "itype_slots/islot_book.h"
#include "itype_slots/islot_tool.h"
#include "itype_slots/memory_card_info.h"
#include "iteminfo_query.h"
#include "iuse.h"
#include "json.h"
#include "npc.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"
#include "units_serde.h"
#include "value_ptr.h"

static const itype_id itype_test_book( "test_book" );
static const itype_id itype_test_ebook_reader( "test_ebook_reader" );
static const itype_id itype_test_efile_copiable( "test_efile_copiable" );
static const itype_id itype_test_efile_large( "test_efile_large" );
static const itype_id itype_test_efile_medium( "test_efile_medium" );
static const itype_id itype_test_efile_small( "test_efile_small" );
static const itype_id itype_test_laptop( "test_laptop" );
static const itype_id itype_test_memory_card_fixture( "test_memory_card_fixture" );
static const itype_id itype_rock( "rock" );

static auto parse_ememory( const std::string &json ) -> units::ememory
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    return read_from_json_string<units::ememory>( jsin, units::ememory_units );
}

TEST_CASE( "efile_ememory_units_parse_and_display", "[efile][foundation][units]" )
{
    CHECK( parse_ememory( "\"512 KB\"" ) == 512_KB );
    CHECK( parse_ememory( "\"32 GB\"" ) == 32_GB );
    CHECK( parse_ememory( "\"1 TB\"" ) == 1_TB );
    CHECK( parse_ememory( "\"2 MB\"" ) == 2000_KB );
    CHECK( 1_TB == 1000_GB );
    CHECK( 1_GB == 1000_MB );
    CHECK( 1_MB == 1000_KB );
    CHECK( units::from_terabytes( 2 ) == 2_TB );
    // a terabyte does not fit in an int of kilobytes
    CHECK( ( 3_TB ).value() == 3000000000LL );

    CHECK_THROWS( parse_ememory( "\"\"" ) );
    CHECK_THROWS( parse_ememory( "\"32\"" ) );
    CHECK_THROWS( parse_ememory( "\"32 PB\"" ) );
    CHECK_THROWS( parse_ememory( "32" ) );

    // DDA units.cpp:157-188: serialized as kilobytes, displayed with one decimal above 1 MB
    std::ostringstream os;
    JsonOut jsout( os );
    ( 24_GB ).serialize( jsout );
    CHECK( os.str() == "\"24000000 KB\"" );
    CHECK( parse_ememory( os.str() ) == 24_GB );

    CHECK( units::display( 512_KB ) == "512 KB" );
    CHECK( units::display( 32_GB ) == "32.0 GB" );
    CHECK( units::display( 1_TB ) == "1.0 TB" );
    CHECK( units::display( 12500_MB ) == "12.5 GB" );
}

TEST_CASE( "efile_pocket_type_names_round_trip", "[efile][foundation]" )
{
    CHECK( io::enum_to_string( pocket_type::E_FILE_STORAGE ) == "E_FILE_STORAGE" );
    CHECK( io::enum_to_string( pocket_type::EBOOK ) == "EBOOK" );
    CHECK( io::string_to_enum<pocket_type>( "E_FILE_STORAGE" ) == pocket_type::E_FILE_STORAGE );
    CHECK( io::string_to_enum<pocket_type>( "EBOOK" ) == pocket_type::EBOOK );
    for( int i = 0; i < static_cast<int>( pocket_type::LAST ); ++i ) {
        const pocket_type pt = static_cast<pocket_type>( i );
        CHECK( io::string_to_enum<pocket_type>( io::enum_to_string( pt ) ) == pt );
    }
}

TEST_CASE( "efile_storage_pocket_capacity_loads_and_tracks_free_memory", "[efile][foundation]" )
{
    item reader( itype_test_ebook_reader );
    REQUIRE( reader.is_estorage() );
    CHECK( reader.total_ememory() == 32_GB );
    CHECK( reader.occupied_ememory() == 0_KB );
    CHECK( reader.remaining_ememory() == 32_GB );
    CHECK( reader.contents.has_pocket_type( pocket_type::E_FILE_STORAGE ) );
    CHECK( reader.contents.occupied_ememory() == 0_KB );

    item small( itype_test_efile_small );
    item medium( itype_test_efile_medium );
    REQUIRE( small.ememory_size() == 1_KB );
    REQUIRE( medium.ememory_size() == 24_GB );

    REQUIRE( reader.contents.insert_item( small, pocket_type::E_FILE_STORAGE ).success() );
    CHECK( reader.occupied_ememory() == 1_KB );
    CHECK( reader.remaining_ememory() == 32_GB - 1_KB );

    REQUIRE( reader.contents.insert_item( medium, pocket_type::E_FILE_STORAGE ).success() );
    CHECK( reader.occupied_ememory() == 24_GB + 1_KB );
    CHECK( reader.remaining_ememory() == 8_GB - 1_KB );
    CHECK( reader.contents.occupied_ememory() == reader.occupied_ememory() );

    SECTION( "the accessors list the stored files" ) {
        CHECK( reader.efiles().size() == 2 );
        CHECK( reader.contents.efiles().size() == 2 );
        // neither test file is a book or a software item
        CHECK( reader.ebooks().empty() );
        CHECK( reader.softwares().empty() );
        // the stored files are reachable through the pocket type walk
        CHECK( reader.contents.all_items_top( pocket_type::E_FILE_STORAGE ).size() == 2 );
    }
    SECTION( "a book is listed as an ebook" ) {
        item book( itype_test_book );
        // the checked insert of a physical-size book is the pocket rules of E1-2, so insert unchecked here
        reader.contents.force_insert_item( book, pocket_type::E_FILE_STORAGE );
        CHECK( reader.ebooks().size() == 1 );
        CHECK( reader.efiles().size() == 3 );
    }

    item laptop( itype_test_laptop );
    CHECK( laptop.total_ememory() == 1_TB );
    item plain( itype_rock );
    CHECK_FALSE( plain.is_estorage() );
    CHECK( plain.total_ememory() == 0_KB );
    CHECK( plain.remaining_ememory() == 0_KB );
}

TEST_CASE( "efile_bad_ememory_max_fails_to_load", "[efile][foundation]" )
{
    const auto load_pocket = []( const std::string &json ) {
        std::istringstream buffer( json );
        JsonIn jsin( buffer );
        JsonObject jo = jsin.get_object();
        pocket_data pd;
        pd.load( jo );
        return pd;
    };
    const pocket_data good = load_pocket(
                                 R"({ "pocket_type": "E_FILE_STORAGE", "rigid": true, "ememory_max": "32 GB" })" );
    CHECK( good.type == pocket_type::E_FILE_STORAGE );
    CHECK( good.ememory_max == 32_GB );
    // check_definition skips the standard volume rules for this pocket type
    CHECK( good.check_definition().empty() );

    CHECK_THROWS( load_pocket(
                      R"({ "pocket_type": "E_FILE_STORAGE", "ememory_max": "plenty" })" ) );
    CHECK_THROWS( load_pocket(
                      R"({ "pocket_type": "E_FILE_STORAGE", "ememory_max": 32 })" ) );
}

TEST_CASE( "efile_item_type_ememory_sizes_and_book_default", "[efile][foundation]" )
{
    CHECK( itype_test_efile_small->ememory_size == 1_KB );
    CHECK( itype_test_efile_medium->ememory_size == 24_GB );
    CHECK( itype_test_efile_large->ememory_size == 500_GB );
    CHECK( itype_test_efile_copiable->ememory_size == 1_MB );
    // a book without ememory_size takes 20 KB per page (DDA item_factory.cpp:839-841)
    const itype &book = *itype_test_book;
    REQUIRE( book.book );
    REQUIRE( item::pages_in_book( book ) == 1587 / 5 );
    CHECK( book.ememory_size == 20_KB * item::pages_in_book( book ) );
    CHECK( item::pages_in_book( *itype_rock ) == 0 );
    CHECK( itype_rock->ememory_size == 0_KB );
}

TEST_CASE( "efile_tool_slot_transfer_fields_load", "[efile][foundation]" )
{
    const itype &laptop = *itype_test_laptop;
    REQUIRE( laptop.tool );
    CHECK( laptop.tool->etransfer_rate == 30_MB );
    CHECK( laptop.tool->e_port == "laptop" );
    CHECK( laptop.tool->e_ports_banned.empty() );
    const itype &reader = *itype_test_ebook_reader;
    REQUIRE( reader.tool );
    CHECK( reader.tool->e_port == "tablet" );
    REQUIRE( reader.tool->e_ports_banned.size() == 1 );
    CHECK( reader.tool->e_ports_banned.front() == "USB-A" );
    // a tool without the fields keeps the defaults
    const itype &flashlight = *itype_id( "flashlight" );
    REQUIRE( flashlight.tool );
    CHECK( flashlight.tool->etransfer_rate == 0_KB );
    CHECK( flashlight.tool->e_port.empty() );
}

TEST_CASE( "efile_memory_card_and_generic_book_data_load", "[efile][foundation]" )
{
    const itype &card = *itype_test_memory_card_fixture;
    REQUIRE( card.memory_card_data );
    CHECK( card.memory_card_data->data_chance == 0.5f );
    CHECK( card.memory_card_data->recipes_chance == 0.25f );
    CHECK( card.memory_card_data->recipes_amount == 3 );
    CHECK( card.memory_card_data->recipes_level_min == 1 );
    CHECK( card.memory_card_data->recipes_level_max == 4 );
    CHECK( card.memory_card_data->secret_recipes );
    CHECK( card.memory_card_data->recipes_categories.count( "CC_FOOD" ) == 1 );
    CHECK( card.memory_card_data->recipes_categories.count( "CC_ELECTRONIC" ) == 1 );
    CHECK_FALSE( itype_rock->memory_card_data );

    CHECK_FALSE( itype_test_book->book->generic );
    CHECK( itype_id( "test_generic_book_fixture" )->book->generic );
}

TEST_CASE( "efile_item_predicates", "[efile][foundation]" )
{
    item small( itype_test_efile_small );
    item book( itype_test_book );
    item copiable( itype_test_efile_copiable );
    item rock( itype_rock );
    item reader( itype_test_ebook_reader );

    CHECK( small.is_estorable() );
    CHECK_FALSE( small.is_estorable_exclusive() );
    CHECK( book.is_estorable() );
    CHECK( copiable.is_estorable() );
    CHECK_FALSE( rock.is_estorable() );
    CHECK_FALSE( rock.is_estorable_exclusive() );
    CHECK( reader.is_estorage() );
    CHECK_FALSE( book.is_estorage() );

    // a physical book is copiable unless it is generic; plain files are not
    CHECK( book.is_ecopiable() );
    CHECK_FALSE( small.is_ecopiable() );
    CHECK_FALSE( rock.is_ecopiable() );

    // DDA item.cpp:2757-2770: the "browsed" item variable, false by default
    CHECK_FALSE( reader.is_browsed() );
    reader.set_browsed( true );
    CHECK( reader.is_browsed() );
    CHECK( reader.get_var( "browsed" ) == "true" );
    reader.set_browsed( false );
    CHECK_FALSE( reader.is_browsed() );

    // efiles_all_browsed: true when the device is browsed or every file on it is
    REQUIRE( reader.contents.insert_item( small, pocket_type::E_FILE_STORAGE ).success() );
    CHECK_FALSE( reader.efiles_all_browsed() );
    reader.set_browsed( true );
    CHECK( reader.efiles_all_browsed() );
}

TEST_CASE( "efile_flags_exist", "[efile][foundation]" )
{
    CHECK( flag_E_STORABLE.is_valid() );
    CHECK( flag_E_STORABLE_EXCLUSIVE.is_valid() );
    CHECK( flag_E_COPIABLE.is_valid() );
    CHECK( flag_E_FILE_COLLECTION.is_valid() );
    for( const char *id : { "E_FILE_DEVICE", "E_FILE_DEVICE_UNREAD", "VIEW_RECIPES", "VIEW_PHOTOS" } ) {
        INFO( id );
        CHECK( flag_id( id ).is_valid() );
    }
    CHECK( static_cast<size_t>( iteminfo_parts::BASE_EMEMORY ) < static_cast<size_t>
           ( iteminfo_parts::BASE_DEBUG ) );
    CHECK( item_category_id( "e_files" ).is_valid() );
    CHECK( item_category_id( "software" ).is_valid() );
}

// The declarations later E1 steps fill in. Each of these must exist and link, and a stub must
// return "nothing" (E1-1 owns the headers, the later steps own the bodies).
TEST_CASE( "efile_declarations_exist_and_stubs_do_nothing", "[efile][foundation]" )
{
    // iuse entries, registered next to CAMERA
    const auto efiledevice = &iuse::efiledevice;
    const auto ebooksave = &iuse::ebooksave;
    const auto view_photos = &iuse::view_photos;
    const auto view_recipes = &iuse::view_recipes;
    CHECK( efiledevice != nullptr );
    CHECK( ebooksave != nullptr );
    CHECK( view_photos != nullptr );
    CHECK( view_recipes != nullptr );
    CHECK( item_controller->has_iuse( "E_FILE_DEVICE" ) );
    CHECK( item_controller->has_iuse( "EBOOKSAVE" ) );
    CHECK( item_controller->has_iuse( "VIEW_PHOTOS" ) );
    CHECK( item_controller->has_iuse( "VIEW_RECIPES" ) );

    item reader( itype_test_ebook_reader );
    CHECK( reader.get_photo_gallery() == nullptr );
    CHECK( reader.get_recipe_catalog() == nullptr );
    CHECK( reader.total_photos() == 0 );
    CHECK_FALSE( reader.is_software() );
    CHECK( reader.get_saved_recipes().empty() );

    // actor and enum declarations
    CHECK( io::enum_to_string( efile_action::EF_BROWSE ) == "EF_BROWSE" );
    CHECK( io::enum_to_string( efile_action::EF_WIPE ) == "EF_WIPE" );
    CHECK( io::enum_to_string( efile_combo::COMBO_NONE ) == "COMBO_NONE" );
    CHECK( efile_activity_actor::local_etransfer_rate == 200_MB );

    // character and NPC entry points exist and are inert without data
    avatar &dummy = get_avatar();
    CHECK( dummy.count_softwares( itype_id( "software_math" ) ) == 0 );
    CHECK_FALSE( dummy.has_software( itype_id( "software_math" ) ) );
    CHECK( dummy.consume_software_container( itype_id( "software_math" ) ) == false );
}
