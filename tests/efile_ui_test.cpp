// CDF: PC-E1 step E1-12 (view-mode key in the e-file pickers). Written before the fix.
// E1-6 report, "Function differences": CDF's inventory_selector::toggle_categorize_contained() clears
// the selector and re-adds the character items and the map squares only, so the rows that the e-file
// menus add through add_contained_ebooks / add_contained_efiles (ebookread, efile_select) are gone
// after the view-mode key, and remove_duplicate_itypes (ebooksave) is undone. DDA
// (inventory_ui.cpp:3821) re-labels the entries in place. DDA has no test of this, so every case is
// CDF-new. The cases drive the selector as the menus do and read the columns, as E1-6's tests do.
#include <algorithm>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "inventory_ui.h"
#include "item.h"
#include "item_contents.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_battery( "battery" );
static const itype_id itype_test_efile_scan_book_120( "test_efile_scan_book_120" );
static const itype_id itype_test_efile_scan_book_240( "test_efile_scan_book_240" );
static const itype_id itype_test_efile_small( "test_efile_small" );
static const itype_id itype_test_eink_tablet_pc( "test_eink_tablet_pc" );

namespace
{

auto start_clean() -> void
{
    clear_all_state();
    set_time( calendar::turn_zero + 12_hours );
    clear_map();
    clear_avatar();
}

auto store_file( item &device, const itype_id &file ) -> item &
{
    const std::vector<item *> before = device.efiles();
    REQUIRE( device.put_in( item::spawn( file ), pocket_type::E_FILE_STORAGE, false, nullptr,
                            true ).success() );
    for( item *stored : device.efiles() ) {
        if( std::find( before.begin(), before.end(), stored ) == before.end() ) {
            stored->set_browsed( true );
            return *stored;
        }
    }
    FAIL( "the stored file was not found" );
    return device;
}

auto rows_of( const inventory_selector &sel ) -> std::vector<const item *>
{
    std::vector<const item *> found;
    for( const inventory_column *col : { &sel.own_gear_column, &sel.own_inv_column,
                                         &sel.map_column
                                       } ) {
        for( const inventory_entry &entry : col->entries ) {
            if( entry.is_item() ) {
                found.push_back( entry.any_item() );
            }
        }
    }
    return found;
}

auto has_row( const inventory_selector &sel, const item &it ) -> bool
{
    const std::vector<const item *> found = rows_of( sel );
    return std::find( found.begin(), found.end(), &it ) != found.end();
}

// the view mode is shared state (inventory_sel_default_state): every case puts it back
struct view_mode_guard {
    inventory_selector_save_state saved = inventory_sel_default_state;
    ~view_mode_guard() {
        inventory_sel_default_state = saved;
    }
};

} // namespace

// ebookread (DDA game_inventory.cpp:1632-1663): the list is the books of one reader.
TEST_CASE( "efile_ui_view_mode_key_keeps_the_ebook_rows", "[efile][ui]" )
{
    start_clean();
    const view_mode_guard guard;
    avatar &who = get_avatar();
    item &tablet = who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    tablet.ammo_set( itype_battery, 10 );
    item &book = store_file( tablet, itype_test_efile_scan_book_120 );
    item &small = store_file( tablet, itype_test_efile_small );

    inventory_pick_selector sel( who );
    sel.clear_items();
    REQUIRE( sel.add_contained_ebooks( &tablet ) );
    REQUIRE( has_row( sel, book ) );
    REQUIRE_FALSE( has_row( sel, small ) );

    // twice: hierarchy and back
    for( int press = 1; press <= 2; ++press ) {
        CAPTURE( press );
        sel.toggle_categorize_contained();
        CHECK( has_row( sel, book ) );
        CHECK_FALSE( has_row( sel, small ) );
        CHECK( rows_of( sel ).size() == 1 );
    }
}

// efile_select (DDA game_inventory.cpp:1797-1900): the list is the files of the source devices.
TEST_CASE( "efile_ui_view_mode_key_keeps_the_efile_rows", "[efile][ui]" )
{
    start_clean();
    const view_mode_guard guard;
    avatar &who = get_avatar();
    item &tablet = who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    item &other = who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    item &book = store_file( tablet, itype_test_efile_scan_book_120 );
    item &small = store_file( tablet, itype_test_efile_small );
    item &other_book = store_file( other, itype_test_efile_scan_book_240 );

    inventory_pick_selector sel( who );
    REQUIRE( sel.add_contained_efiles( &tablet ) );
    REQUIRE( sel.add_contained_efiles( &other ) );
    REQUIRE( rows_of( sel ).size() == 3 );

    for( int press = 1; press <= 2; ++press ) {
        CAPTURE( press );
        sel.toggle_categorize_contained();
        CHECK( has_row( sel, book ) );
        CHECK( has_row( sel, small ) );
        CHECK( has_row( sel, other_book ) );
        CHECK( rows_of( sel ).size() == 3 );
    }
}

// the file rows next to the character rows: the rebuild adds the character items again and must not
// lose or duplicate the file rows
TEST_CASE( "efile_ui_view_mode_key_keeps_files_next_to_the_character_items", "[efile][ui]" )
{
    start_clean();
    const view_mode_guard guard;
    avatar &who = get_avatar();
    item &tablet = who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    item &stored = store_file( tablet, itype_test_efile_small );
    item &loose = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );

    SECTION( "a device the menu opened" ) {
        inventory_pick_selector sel( who );
        sel.add_character_items( who );
        sel.add_contained_efiles( &tablet );
        REQUIRE( has_row( sel, stored ) );
        REQUIRE( has_row( sel, loose ) );
        const size_t before = rows_of( sel ).size();
        for( int press = 1; press <= 2; ++press ) {
            CAPTURE( press );
            sel.toggle_categorize_contained();
            CHECK( has_row( sel, stored ) );
            CHECK( has_row( sel, loose ) );
            CHECK( rows_of( sel ).size() == before );
        }
    }

    SECTION( "files listed by add_efiles" ) {
        inventory_pick_selector sel( who );
        sel.add_character_items( who, true );
        REQUIRE( has_row( sel, stored ) );
        for( int press = 1; press <= 2; ++press ) {
            CAPTURE( press );
            sel.toggle_categorize_contained();
            CHECK( has_row( sel, stored ) );
            CHECK( has_row( sel, loose ) );
        }
    }
}

// ebooksave (DDA game_inventory.cpp:1666-1716): one row per item type, kept after the key
TEST_CASE( "efile_ui_view_mode_key_keeps_the_duplicates_removed", "[efile][ui]" )
{
    start_clean();
    const view_mode_guard guard;
    avatar &who = get_avatar();
    for( int i = 0; i < 3; ++i ) {
        who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    }
    who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );

    inventory_pick_selector sel( who );
    sel.add_character_items( who );
    sel.remove_duplicate_itypes( true );
    const size_t one_per_type = rows_of( sel ).size();
    REQUIRE( one_per_type == 2 );

    for( int press = 1; press <= 2; ++press ) {
        CAPTURE( press );
        sel.toggle_categorize_contained();
        CHECK( rows_of( sel ).size() == one_per_type );
    }
}

// a plain picker is not changed by the fix
TEST_CASE( "efile_ui_view_mode_key_rebuilds_a_plain_picker", "[efile][ui]" )
{
    start_clean();
    const view_mode_guard guard;
    avatar &who = get_avatar();
    item &loose = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );

    inventory_pick_selector sel( who );
    sel.add_character_items( who );
    REQUIRE( has_row( sel, loose ) );
    for( int press = 1; press <= 2; ++press ) {
        sel.toggle_categorize_contained();
        CHECK( has_row( sel, loose ) );
    }
}
