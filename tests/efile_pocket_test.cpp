// CDF: PC-E1 step E1-2 (e-file pocket rules, routing, walk exclusion). Tests for the behaviour of
// the E_FILE_STORAGE pocket in DDA b4b62254ae2 (item_pocket.cpp:663, 1367-1372, 1817, 1945, 2124;
// item_contents.cpp:743, 953; item_group.cpp:103-104). Written before the code.
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "debug.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "visitable.h"

static const itype_id itype_test_book( "test_book" );
static const itype_id itype_test_ebook_reader( "test_ebook_reader" );
static const itype_id itype_test_efile_small( "test_efile_small" );
static const itype_id itype_test_efile_usable( "test_efile_usable" );
static const itype_id itype_test_laptop( "test_laptop" );
static const itype_id itype_rock( "rock" );

static const trait_id trait_DEBUG_STORAGE( "DEBUG_STORAGE" );

namespace
{

auto estorage_pocket( item &it ) -> item_pocket *
{
    const std::vector<item_pocket *> pockets = it.contents.get_pockets( pocket_type::E_FILE_STORAGE );
    return pockets.empty() ? nullptr : pockets.front();
}

// the pockets that are not E_FILE_STORAGE, in order, as the default walk must list them
auto expected_walk( item &it ) -> std::vector<item *>
{
    std::vector<item *> ret;
    for( item_pocket &pocket : it.contents.get_pockets() ) {
        if( pocket.is_type( pocket_type::E_FILE_STORAGE ) ) {
            continue;
        }
        const std::vector<item *> items = pocket.all_items_top();
        ret.insert( ret.end(), items.begin(), items.end() );
    }
    return ret;
}

void store_files( item &device, int small_files, bool with_book )
{
    for( int i = 0; i < small_files; ++i ) {
        item small( itype_test_efile_small );
        REQUIRE( device.contents.insert_item( small, pocket_type::E_FILE_STORAGE ).success() );
    }
    if( with_book ) {
        item book( itype_test_book );
        REQUIRE( device.contents.insert_item( book, pocket_type::E_FILE_STORAGE ).success() );
    }
}

} // namespace

TEST_CASE( "efile_pocket_accepts_only_estorable_items", "[efile][pocket]" )
{
    clear_all_state();
    item reader( itype_test_ebook_reader );
    item_pocket *pocket = estorage_pocket( reader );
    REQUIRE( pocket != nullptr );

    item small( itype_test_efile_small );
    item book( itype_test_book );
    item rock( itype_rock );

    CHECK( pocket->can_contain( small ).success() );
    // a physical book is far bigger than the 1 ml the pocket is declared with: files have no size
    CHECK( pocket->can_contain( book ).success() );
    CHECK_FALSE( pocket->can_contain( rock ).success() );

    CHECK( reader.put_in( small, pocket_type::E_FILE_STORAGE ).success() );
    CHECK( reader.put_in( book, pocket_type::E_FILE_STORAGE ).success() );
    const ret_val<void> refused = reader.put_in( rock, pocket_type::E_FILE_STORAGE, false, nullptr,
                                  true );
    CHECK_FALSE( refused.success() );
    CHECK( refused.str().find( "only books, e-scannable items can go into e-file pocket" ) !=
           std::string::npos );
    CHECK( reader.efiles().size() == 2 );
}

TEST_CASE( "efile_files_add_no_volume_weight_or_length", "[efile][pocket]" )
{
    clear_all_state();
    item reader( itype_test_ebook_reader );
    const units::volume volume_before = reader.volume();
    const units::mass weight_before = reader.weight();
    const units::length length_before = reader.length();

    // the book is the one that could leak: 1.5 L and 1.6 kg, a file has none of that
    store_files( reader, 50, true );
    REQUIRE( reader.efiles().size() == 51 );

    CHECK( reader.volume() == volume_before );
    CHECK( reader.weight() == weight_before );
    CHECK( reader.length() == length_before );
    item_pocket *pocket = estorage_pocket( reader );
    REQUIRE( pocket != nullptr );
    // DDA item_pocket.cpp:663
    CHECK( pocket->item_length_modifier() == 0_mm );
    CHECK( pocket->item_size_modifier() == 0_ml );
    CHECK( pocket->item_weight_modifier() == 0_gram );
    // DDA item_pocket.cpp:2124
    CHECK( pocket->is_container_like_type() );
    CHECK_FALSE( pocket->is_standard_type() );
    CHECK( reader.all_items_container_top().size() == 51 );
}

TEST_CASE( "efile_files_are_not_shed_by_the_overflow_pass", "[efile][pocket]" )
{
    clear_all_state();
    Character &dummy = get_avatar();
    clear_character( dummy );
    // a carried body storage, so the device is really carried instead of being wielded
    dummy.set_mutation( trait_DEBUG_STORAGE );

    detached_ptr<item> device = item::spawn( itype_test_ebook_reader );
    store_files( *device, 50, true );
    item &held = dummy.i_add( std::move( device ) );
    REQUIRE( held.efiles().size() == 51 );

    // BUG2 regression: the first turn after a pickup runs the DDA overflow pass over every
    // carried item (DDA item_pocket.cpp:1813-1817 skips E_FILE_STORAGE)
    dummy.invalidate_inventory_validity_cache();
    dummy.drop_invalid_inventory();
    held.overflow( dummy.bub_pos(), &dummy );

    CHECK( held.efiles().size() == 51 );
    for( const item_pocket *pocket : held.contents.get_pockets( pocket_type::MIGRATION ) ) {
        CHECK( pocket->empty() );
    }
    CHECK( held.parent_item() == dummy.get_body_storage() );
}

TEST_CASE( "efile_pocket_never_spills", "[efile][pocket]" )
{
    clear_all_state();
    Character &dummy = get_avatar();
    clear_character( dummy );
    item reader( itype_test_ebook_reader );
    store_files( reader, 3, true );
    item_pocket *pocket = estorage_pocket( reader );
    REQUIRE( pocket != nullptr );

    // DDA item_pocket.cpp:1945
    CHECK_FALSE( pocket->spill_contents( dummy.bub_pos() ) );
    reader.spill_contents( dummy.bub_pos() );
    CHECK( reader.efiles().size() == 4 );
}

TEST_CASE( "efile_walks_do_not_return_installed_files", "[efile][pocket][walk]" )
{
    clear_all_state();
    Character &dummy = get_avatar();
    clear_character( dummy );
    dummy.set_mutation( trait_DEBUG_STORAGE );

    detached_ptr<item> device = item::spawn( itype_test_ebook_reader );
    item small( itype_test_efile_small );
    item usable( itype_test_efile_usable );
    REQUIRE( device->contents.insert_item( small, pocket_type::E_FILE_STORAGE ).success() );
    REQUIRE( device->contents.insert_item( usable, pocket_type::E_FILE_STORAGE ).success() );
    item &held = dummy.i_add( std::move( device ) );
    REQUIRE( held.efiles().size() == 2 );

    const auto is_file = []( const item & it ) {
        return it.typeId() == itype_test_efile_small || it.typeId() == itype_test_efile_usable;
    };
    std::vector<const item *> visited;
    const item &const_held = held;
    const_held.visit_items( [&visited]( const item * it ) {
        visited.push_back( it );
        return VisitResponse::NEXT;
    } );
    CHECK( std::ranges::none_of( visited, [&is_file]( const item * it ) {
        return is_file( *it );
    } ) );
    CHECK( std::ranges::find( visited, &held ) != visited.end() );

    CHECK( dummy.items_with( is_file ).empty() );
    CHECK_FALSE( dummy.has_amount( itype_test_efile_small, 1 ) );
    CHECK( dummy.amount_of( itype_test_efile_usable ) == 0 );
    CHECK( dummy.charges_of( itype_test_efile_usable ) == 0 );
    CHECK( held.get_usable_item( "transform" ) == nullptr );

    // control: the same file loose in the inventory is found by every one of them
    item &loose = dummy.i_add( item::spawn( itype_test_efile_usable ) );
    CHECK( dummy.items_with( is_file ).size() == 1 );
    CHECK( dummy.has_amount( itype_test_efile_usable, 1 ) );
    CHECK( loose.get_usable_item( "transform" ) == &loose );
}

// The ledger of the callers of item_contents::all_items_top() (reports/E1-2.md) all see only the
// output of that accessor, so it is checked here for every shape of item: unchanged for every
// item without an E_FILE pocket, and exactly the files dropped on a device that has one.
TEST_CASE( "efile_default_walk_is_unchanged_for_items_without_files", "[efile][pocket][walk]" )
{
    clear_all_state();
    std::vector<detached_ptr<item>> shapes;

    detached_ptr<item> bag = item::spawn( itype_id( "test_backpack" ) );
    REQUIRE( bag->put_in( item( itype_rock ), pocket_type::CONTAINER ).success() );
    // an item that a real pocket parked in the MIGRATION pocket
    REQUIRE( bag->contents.insert_item( item( itype_rock ), pocket_type::MIGRATION ).success() );
    shapes.push_back( std::move( bag ) );

    detached_ptr<item> gun = item::spawn( itype_id( "test_glock" ) );
    REQUIRE( gun->put_in( item( itype_id( "test_glockmag" ) ),
                          pocket_type::MAGAZINE_WELL ).success() );
    shapes.push_back( std::move( gun ) );

    shapes.push_back( item::spawn( itype_id( "test_battery_tool" ), calendar::start_of_cataclysm,
                                   7 ) );

    detached_ptr<item> cable = item::spawn( itype_id( "test_linkupb_cable" ) );
    REQUIRE( cable->put_in( item( itype_id( "test_linkupb_extension" ) ),
                            pocket_type::CABLE ).success() );
    shapes.push_back( std::move( cable ) );

    for( const detached_ptr<item> &shape : shapes ) {
        CAPTURE( shape->typeId().str() );
        const std::vector<item *> walk = shape->contents.all_items_top();
        CHECK( walk == expected_walk( *shape ) );
        std::vector<item *> recursive = shape->contents.all_items_ptr();
        CHECK( recursive.size() >= walk.size() );
        CHECK( shape->contents.has_any_with( []( const item & ) {
            return true;
        } ) == !walk.empty() );
        if( !walk.empty() ) {
            CHECK( shape->contents.get_item_with( []( const item & ) {
                return true;
            } ) == walk.front() );
        }
    }

    SECTION( "a device with a battery and files lists only the battery" ) {
        item reader( itype_test_ebook_reader );
        reader.ammo_set( itype_id( "battery" ), 10 );
        const std::vector<item *> before = reader.contents.all_items_top();
        store_files( reader, 5, true );
        const std::vector<item *> after = reader.contents.all_items_top();
        CHECK( after == expected_walk( reader ) );
        CHECK( after.size() == before.size() );
        const std::vector<item *> files = reader.efiles();
        REQUIRE( files.size() == 6 );
        const std::vector<item *> recursive = reader.contents.all_items_ptr();
        for( const item *file : files ) {
            CHECK( std::ranges::find( after, file ) == after.end() );
            CHECK( std::ranges::find( recursive, file ) == recursive.end() );
        }
        // the explicit typed walk still reaches them
        CHECK( reader.contents.all_items_top( pocket_type::E_FILE_STORAGE ).size() == 6 );
    }
}

TEST_CASE( "efile_only_device_reports_no_errors", "[efile][pocket][walk]" )
{
    clear_all_state();
    item reader( itype_test_ebook_reader );
    store_files( reader, 3, true );
    REQUIRE_FALSE( reader.contents.empty() );
    // a device that holds only files reports non-empty contents but has no top item to show
    const auto no_error = []( const char *what, const std::function<void()> &call ) {
        const std::string msg = capture_debugmsg_during( call );
        INFO( what );
        INFO( msg );
        CHECK( msg.empty() );
    };
    no_error( "tname", [&reader]() {
        reader.tname();
    } );
    no_error( "display_name", [&reader]() {
        reader.display_name();
    } );
    no_error( "info", [&reader]() {
        reader.info();
    } );
    no_error( "volume", [&reader]() {
        reader.volume();
    } );
    no_error( "weight", [&reader]() {
        reader.weight();
    } );
}

TEST_CASE( "efile_best_pocket_takes_only_exclusive_files", "[efile][pocket]" )
{
    clear_all_state();
    item reader( itype_test_ebook_reader );
    item small( itype_test_efile_small );
    item book( itype_test_book );
    // DDA item_contents.cpp:953: best_pocket is for pickup; only an exclusive file goes to E_FILE
    CHECK( reader.best_pocket( small ).pocket == nullptr );
    CHECK( reader.best_pocket( book ).pocket == nullptr );

    item exclusive( itype_test_efile_small );
    exclusive.set_flag( flag_id( "E_STORABLE_EXCLUSIVE" ) );
    REQUIRE( exclusive.is_estorable_exclusive() );
    const pocket_choice choice = reader.best_pocket( exclusive );
    REQUIRE( choice.pocket != nullptr );
    CHECK( choice.pocket->is_type( pocket_type::E_FILE_STORAGE ) );
}

TEST_CASE( "efile_combine_convert_moves_files", "[efile][pocket]" )
{
    clear_all_state();
    item source( itype_test_ebook_reader );
    store_files( source, 2, false );
    item dest( itype_test_ebook_reader );
    // DDA item_contents.cpp:743: a convert load treats E_FILE like MIGRATION
    dest.contents.combine( source.contents, true );
    CHECK( dest.efiles().size() == 2 );
    for( const item_pocket *pocket : dest.contents.get_pockets( pocket_type::MIGRATION ) ) {
        CHECK( pocket->empty() );
    }
}

TEST_CASE( "efile_item_groups_spawn_files_into_the_device_pocket", "[efile][pocket][item_group]" )
{
    clear_all_state();
    const auto spawn_laptops = []( const char *group, std::vector<detached_ptr<item>> &out ) {
        const std::string msg = capture_debugmsg_during( [&group, &out]() {
            for( detached_ptr<item> &it : item_group::items_from( item_group_id( group ) ) ) {
                out.push_back( std::move( it ) );
            }
        } );
        CHECK( msg.empty() );
    };
    const auto find_laptop = []( const std::vector<detached_ptr<item>> &items ) -> const item * {
        for( const detached_ptr<item> &it : items )
        {
            if( it && it->typeId() == itype_test_laptop ) {
                return &*it;
            }
        }
        return nullptr;
    };

    SECTION( "contents-item" ) {
        std::vector<detached_ptr<item>> items;
        spawn_laptops( "test_edevices_power", items );
        const item *laptop = find_laptop( items );
        REQUIRE( laptop != nullptr );
        CHECK( laptop->efiles().size() == 1 );
        CHECK( laptop->contents.get_pockets( pocket_type::E_FILE_STORAGE ).size() == 1 );
    }
    SECTION( "contents-group" ) {
        std::vector<detached_ptr<item>> items;
        spawn_laptops( "test_edevices_standard", items );
        const item *laptop = find_laptop( items );
        REQUIRE( laptop != nullptr );
        CHECK( laptop->efiles().size() == 3 );
    }
    SECTION( "a run of identical files" ) {
        std::vector<detached_ptr<item>> items;
        spawn_laptops( "test_efile_pocket_run", items );
        const item *laptop = find_laptop( items );
        REQUIRE( laptop != nullptr );
        CHECK( laptop->efiles().size() == 3 );
    }
}
