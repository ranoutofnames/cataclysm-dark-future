// CDF: PC-E1 step E1-2 (e-file pocket save mapping). DDA saves a pocket type as an integer
// (DDA pocket_type.h:8-18, savegame_json.cpp:280-291): SOFTWARE 5, E_FILE_STORAGE 6, CABLE 7,
// MIGRATION 8, EBOOK 9. Written before the code.
#include <sstream>
#include <string>
#include <vector>

#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "json.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_test_book( "test_book" );
static const itype_id itype_test_ebook_reader( "test_ebook_reader" );
static const itype_id itype_test_efile_medium( "test_efile_medium" );
static const itype_id itype_test_efile_small( "test_efile_small" );

namespace
{

// item::serialize writes "contents" only when the pockets differ from a fresh item, so the
// contents are also written explicitly (as tests/savegame_cable_pocket_test.cpp does)
auto save_contents_of( const item &it ) -> std::string
{
    std::ostringstream buffer;
    JsonOut jsout( buffer );
    jsout.start_object();
    jsout.member( "typeid", it.typeId() );
    jsout.member( "contents", it.get_contents() );
    jsout.end_object();
    return buffer.str();
}

auto save_item( const item &it ) -> std::string
{
    std::ostringstream buffer;
    JsonOut jsout( buffer );
    it.serialize( jsout );
    return buffer.str();
}

auto load_item( const std::string &json ) -> detached_ptr<item>
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    return item::spawn( jsin );
}

auto make_pocket_data( const std::string &json ) -> pocket_data
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    JsonObject jo = jsin.get_object();
    pocket_data pd;
    pd.load( jo );
    return pd;
}

auto save_pocket( const item_pocket &pocket ) -> std::string
{
    std::ostringstream buffer;
    JsonOut jsout( buffer );
    pocket.serialize( jsout );
    return buffer.str();
}

void load_pocket( item_pocket &pocket, const std::string &json )
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    pocket.deserialize( jsin.get_object() );
}

} // namespace

TEST_CASE( "efile_pocket_saves_with_the_dda_ints", "[efile][savegame]" )
{
    clear_all_state();
    const pocket_data efile = make_pocket_data(
                                  R"({ "pocket_type": "E_FILE_STORAGE", "rigid": true, "ememory_max": "32 GB" })" );
    const pocket_data ebook = make_pocket_data( R"({ "pocket_type": "EBOOK", "rigid": true })" );
    REQUIRE( efile.type == pocket_type::E_FILE_STORAGE );
    REQUIRE( ebook.type == pocket_type::EBOOK );

    item_pocket efile_pocket( &efile );
    item_pocket ebook_pocket( &ebook );
    const std::string efile_json = save_pocket( efile_pocket );
    const std::string ebook_json = save_pocket( ebook_pocket );
    CHECK( efile_json.find( "\"pocket_type\":6" ) != std::string::npos );
    CHECK( ebook_json.find( "\"pocket_type\":9" ) != std::string::npos );

    // and back: neither is read as MIGRATION (8, the default for an unknown int)
    item_pocket efile_loaded( &efile );
    item_pocket ebook_loaded( &ebook );
    load_pocket( efile_loaded, efile_json );
    load_pocket( ebook_loaded, ebook_json );
    CHECK( efile_loaded.saved_type() == pocket_type::E_FILE_STORAGE );
    CHECK( ebook_loaded.saved_type() == pocket_type::EBOOK );
}

TEST_CASE( "efile_device_keeps_its_files_through_a_save", "[efile][savegame]" )
{
    clear_all_state();
    detached_ptr<item> device = item::spawn( itype_test_ebook_reader );
    item small( itype_test_efile_small );
    item medium( itype_test_efile_medium );
    item book( itype_test_book );
    REQUIRE( device->contents.insert_item( small, pocket_type::E_FILE_STORAGE ).success() );
    REQUIRE( device->contents.insert_item( medium, pocket_type::E_FILE_STORAGE ).success() );
    REQUIRE( device->contents.insert_item( book, pocket_type::E_FILE_STORAGE ).success() );
    const units::ememory occupied = device->occupied_ememory();
    REQUIRE( device->efiles().size() == 3 );

    const std::string contents_json = save_contents_of( *device );
    INFO( contents_json );
    // DDA int 6 is E_FILE_STORAGE; 8 would be MIGRATION
    CHECK( contents_json.find( "\"pocket_type\":6" ) != std::string::npos );

    const auto check_loaded = [&]( const detached_ptr<item> &loaded ) {
        REQUIRE( loaded );
        CHECK( loaded->typeId() == itype_test_ebook_reader );
        CHECK( loaded->efiles().size() == 3 );
        CHECK( loaded->ebooks().size() == 1 );
        CHECK( loaded->occupied_ememory() == occupied );
        for( const item *file : loaded->efiles() ) {
            const item_pocket *pocket = loaded->contents.pocket_of( file );
            REQUIRE( pocket != nullptr );
            // no file lands in the MIGRATION pocket
            CHECK( pocket->is_type( pocket_type::E_FILE_STORAGE ) );
        }
    };
    SECTION( "the contents written explicitly" ) {
        check_loaded( load_item( contents_json ) );
    }
    SECTION( "the whole item as saved in a save game" ) {
        const std::string item_json = save_item( *device );
        INFO( item_json );
        check_loaded( load_item( item_json ) );
    }
}
