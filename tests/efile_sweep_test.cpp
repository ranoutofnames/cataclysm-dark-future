// CDF: PC-E1 step E1-12 (whole-data sweep of the e-file model). RECONCILE 5.2: spawn every item type
// and every item group once and check the e-file invariants of the structure, not of single ids.
// The audit cases of E1-10a (tests/efile_data_audit_test.cpp) check named items; this sweep checks
// everything that exists. It has no paired fix: it was written green against the finished model and
// guards against a later data or code change that puts a file in the wrong place.
// DDA has no such test, so every case is CDF-new.
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "calendar.h"
#include "catch/catch.hpp"
#include "detached_ptr.h"
#include "enum_conversions.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_efile_junk( "efile_junk" );

namespace
{

auto is_file_type( const itype &type ) -> bool
{
    return std::ranges::any_of( type.get_flags(), []( const flag_id & flag ) {
        return flag.str() == "E_STORABLE" || flag.str() == "E_STORABLE_EXCLUSIVE";
    } );
}

// a placeholder that a reference to an undefined id creates ("undefined-<id>")
auto is_placeholder( const itype &type ) -> bool
{
    return type.nname( 1 ).rfind( "undefined-", 0 ) == 0;
}

auto pocket_type_name( const item_pocket &pocket ) -> std::string
{
    return io::enum_to_string( pocket.get_type() );
}

// Every pocket of @p it and of everything inside it: files only in E_FILE pockets, nothing that is a
// file in a MIGRATION pocket, E_FILE pockets within their size. Returns the number of E_FILE pockets.
auto check_structure( const item &it, int &files_seen ) -> int
{
    int efile_pockets = 0;
    for( const item_pocket &pocket : it.contents.get_pockets() ) {
        CAPTURE( it.typeId().str(), pocket_type_name( pocket ) );
        if( pocket.is_type( pocket_type::E_FILE_STORAGE ) ) {
            efile_pockets++;
            for( const item *content : pocket.all_items_top() ) {
                CAPTURE( content->typeId().str() );
                CHECK( content->is_estorable() );
                files_seen++;
            }
        } else if( pocket.is_type( pocket_type::MIGRATION ) ) {
            for( const item *content : pocket.all_items_top() ) {
                CAPTURE( content->typeId().str() );
                CHECK_FALSE( content->is_estorable() );
            }
        }
        CHECK_FALSE( pocket.is_type( pocket_type::EBOOK ) );
        for( const item *content : pocket.all_items_top() ) {
            efile_pockets += check_structure( *content, files_seen );
        }
    }
    if( it.is_estorage() ) {
        CHECK( it.occupied_ememory() <= it.total_ememory() );
    }
    return efile_pockets;
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

// ids of every item_group object of one file (as the audit of E1-10a does: there is no registry)
auto item_group_ids_in( const std::string &path ) -> std::vector<std::string>
{
    std::vector<std::string> out;
    read_from_file_json( path, [&]( JsonIn & jsin ) {
        if( !jsin.test_array() ) {
            return;
        }
        JsonArray all = jsin.get_array();
        while( all.has_more() ) {
            JsonObject jo = all.next_object();
            jo.allow_omitted_members();
            if( jo.get_string( "type", "" ) == "item_group" && jo.has_string( "id" ) ) {
                out.push_back( jo.get_string( "id" ) );
            }
        }
    } );
    return out;
}

} // namespace

// The Lua binding (src/catalua_bindings.cpp:555, reg_enum) registers every value below
// enum_traits<pocket_type>::last under its io::enum_to_string name. The Lua case itself
// (tests/catalua_test.cpp, tag [lua]) hangs on some machines, so this is the runnable proof of the
// same mechanism: both e-file values are below `last` and have their own name.
TEST_CASE( "efile_sweep_pocket_types_have_names_for_the_lua_enum", "[efile][sweep]" )
{
    constexpr int last = static_cast<int>( enum_traits<pocket_type>::last );
    std::set<std::string> names;
    for( int i = 0; i < last; ++i ) {
        const std::string name = io::enum_to_string( static_cast<pocket_type>( i ) );
        CAPTURE( i, name );
        CHECK_FALSE( name.empty() );
        CHECK( names.insert( name ).second );
    }
    CHECK( static_cast<int>( pocket_type::E_FILE_STORAGE ) < last );
    CHECK( static_cast<int>( pocket_type::EBOOK ) < last );
    CHECK( io::enum_to_string( pocket_type::E_FILE_STORAGE ) == "E_FILE_STORAGE" );
    CHECK( io::enum_to_string( pocket_type::EBOOK ) == "EBOOK" );
    CHECK( names.contains( "E_FILE_STORAGE" ) );
    CHECK( names.contains( "EBOOK" ) );
}

// Every item type, spawned once: the pocket data and the fresh item agree.
TEST_CASE( "efile_sweep_every_item_type_has_a_sane_efile_structure", "[efile][sweep]" )
{
    clear_all_state();
    int estorage_types = 0;
    int checked = 0;
    for( const itype *type : item_controller->all() ) {
        if( is_placeholder( *type ) ) {
            continue;
        }
        INFO( type->get_id().str() );
        int efile_pocket_data = 0;
        for( const pocket_data &pocket : type->pockets ) {
            // DDA data never uses the obsolete EBOOK pocket
            CHECK( pocket.type != pocket_type::EBOOK );
            if( pocket.type == pocket_type::E_FILE_STORAGE ) {
                efile_pocket_data++;
                CHECK( pocket.ememory_max > 0_KB );
            }
        }
        // a file does not carry files: the e-file pocket belongs to the devices
        if( is_file_type( *type ) ) {
            CHECK( efile_pocket_data == 0 );
        }
        CHECK( efile_pocket_data <= 1 );

        detached_ptr<item> spawned = item::spawn( type->get_id() );
        int files_seen = 0;
        const int efile_pockets = check_structure( *spawned, files_seen );
        CHECK( efile_pockets >= efile_pocket_data );
        CHECK( spawned->is_estorage() == ( efile_pocket_data > 0 ) );
        // a fresh type holds no file until a group or a player puts one in
        CHECK( files_seen == 0 );
        estorage_types += efile_pocket_data;
        checked++;
    }
    CHECK( checked > 1000 );
    CHECK( estorage_types > 50 );
}

// Every estorage type with one file, saved and loaded again: the file stays in the E_FILE pocket
// (never MIGRATION), the memory in use is the same and a second save is identical.
TEST_CASE( "efile_sweep_every_estorage_type_keeps_a_file_through_a_save", "[efile][sweep]" )
{
    clear_all_state();
    int devices = 0;
    for( const itype *type : item_controller->all() ) {
        if( is_placeholder( *type ) ) {
            continue;
        }
        detached_ptr<item> device = item::spawn( type->get_id() );
        if( !device->is_estorage() ) {
            continue;
        }
        INFO( type->get_id().str() );
        REQUIRE( device->put_in( item::spawn( itype_efile_junk ), pocket_type::E_FILE_STORAGE, false,
                                 nullptr, true ).success() );
        REQUIRE( device->efiles().size() == 1 );
        const units::ememory occupied = device->occupied_ememory();
        CHECK( occupied > 0_KB );

        const std::string saved = save_item( *device );
        detached_ptr<item> loaded = load_item( saved );
        REQUIRE( loaded );
        CHECK( loaded->typeId() == type->get_id() );
        CHECK( loaded->is_estorage() );
        REQUIRE( loaded->efiles().size() == 1 );
        CHECK( loaded->occupied_ememory() == occupied );
        for( const item *file : loaded->efiles() ) {
            const item_pocket *pocket = loaded->contents.pocket_of( file );
            REQUIRE( pocket != nullptr );
            CHECK( pocket->is_type( pocket_type::E_FILE_STORAGE ) );
        }
        int files_seen = 0;
        check_structure( *loaded, files_seen );
        CHECK( files_seen == 1 );
        CHECK( save_item( *loaded ) == saved );
        devices++;
    }
    CHECK( devices > 50 );
}

// Every item group of core data and TEST_DATA, spawned a few times: whatever comes out keeps its
// files in E_FILE pockets and its other pockets in order. This is where a plain put_in of a file
// (the E1-11a mind scan bug) would show: the E1-2 router sends it to MIGRATION.
TEST_CASE( "efile_sweep_no_item_group_puts_a_file_in_a_migration_pocket", "[efile][sweep]" )
{
    clear_all_state();
    std::vector<std::string> files = get_files_from_path( ".json", "data/json", true, true );
    const std::vector<std::string> test_data = get_files_from_path( ".json", "data/mods/TEST_DATA",
            true, true );
    files.insert( files.end(), test_data.begin(), test_data.end() );
    std::set<std::string> ids;
    for( const std::string &path : files ) {
        for( const std::string &id : item_group_ids_in( path ) ) {
            ids.insert( id );
        }
    }
    REQUIRE( ids.size() > 500 );

    int spawned_items = 0;
    int files_in_pockets = 0;
    for( const std::string &id : ids ) {
        const item_group_id group_id( id );
        if( !item_group::group_is_defined( group_id ) ) {
            continue;
        }
        INFO( id );
        for( int roll = 0; roll < 3; ++roll ) {
            for( detached_ptr<item> &spawned : item_group::items_from( group_id,
                    calendar::turn ) ) {
                check_structure( *spawned, files_in_pockets );
                spawned_items++;
            }
        }
    }
    CHECK( spawned_items > 1000 );
    CHECK( files_in_pockets > 0 );
}
