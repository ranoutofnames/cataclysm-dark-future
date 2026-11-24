// CDF: PC-E1 step E1-6 (e-file inventory menus and selector support). DDA b4b62254ae2 has no test
// of these menus (DDA tests/ never open an inventory_selector for e-files), so every case here is
// CDF-new. UI is hard to test headless: the cases test what each menu OFFERS (rows per column,
// greyed or not, and why) and what its header says, not the drawing. The menu functions build
// their preset, selector and header through the same static builders the hooks below call, so the
// tests run the shipped code path up to execute().
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "color.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "detached_ptr.h"
#include "effect.h"
#include "game_constants.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_contents.h"
#include "item_handling_util.h"
#include "map/map.h"
#include "map_helpers.h"
#include "output.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "skill.h"
#include "state_helpers.h"
#include "string_utils.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_battery( "battery" );
static const itype_id itype_meat( "meat" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_test_efile_copiable( "test_efile_copiable" );
static const itype_id itype_test_efile_etd( "test_efile_etd" );
static const itype_id itype_test_efile_etd_tiny( "test_efile_etd_tiny" );
static const itype_id itype_test_efile_forbidden_device( "test_efile_forbidden_device" );
static const itype_id itype_test_efile_medium( "test_efile_medium" );
static const itype_id itype_test_efile_scan_book_120( "test_efile_scan_book_120" );
static const itype_id itype_test_efile_scan_book_240( "test_efile_scan_book_240" );
static const itype_id itype_test_efile_small( "test_efile_small" );
static const itype_id itype_test_efile_usb_a( "test_efile_usb_a" );
static const itype_id itype_test_eink_tablet_pc( "test_eink_tablet_pc" );
static const itype_id itype_test_generic_book_fixture( "test_generic_book_fixture" );
static const itype_id itype_test_laptop( "test_laptop" );
static const itype_id itype_test_smart_phone( "test_smart_phone" );

static const efftype_id effect_blind( "blind" );

// Test hooks of src/game_inventory.cpp (precedent: probe_common_selector_action). Declared here and
// there, not in game_inventory.h (a hook there would rebuild its includers). Keep the definitions
// identical.
namespace game_menus
{
namespace inv
{
struct efile_menu_row {
    item *it = nullptr;
    // locations of the entry (one entry can stand for a stack)
    size_t locations = 0;
    // false: the row is greyed
    bool enabled = true;
    // why it is greyed
    std::string denial;
    // 0 gear column, 1 inventory column, 2 map column
    int column = 0;
};
using efile_menu_rows = std::vector<efile_menu_row>;

bool ereader_preset_shows( player &you, const item &it );
std::string ereader_preset_denial( player &you, const item &it );
std::string ebookread_preset_denial( player &you, const item &book );
efile_menu_rows ebookread_rows( player &you, item *ereader );
efile_menu_rows ebooksave_rows( player &who, item *ereader );
inventory_selector::header_stats ebooksave_stats( item *ereader, const drop_locations &chosen );
efile_menu_rows edevice_select_rows( player &who, item *used_edevice, bool browse_equals,
                                     bool auto_include_used_edevice, bool unusable_only,
                                     efile_action action );
efile_menu_rows efile_select_rows( player &who, item *used_edevice,
                                   const std::vector<item *> &target_edevices,
                                   efile_action action, bool from_used_edevice );
inventory_selector::header_stats efile_select_stats( player &who, item *used_edevice,
        const std::vector<item *> &target_edevices, efile_action action, bool from_used_edevice,
        const drop_locations &chosen );
bool smoke_food_preset_shows( const item &it );
inventory_selector::header_stats smoke_food_stats( units::volume total_capacity,
        units::volume used_capacity, const drop_locations &chosen );
} // namespace inv
} // namespace game_menus

namespace
{

using game_menus::inv::efile_menu_row;
using game_menus::inv::efile_menu_rows;

auto start_clean() -> void
{
    clear_all_state();
    set_time( calendar::turn_zero + 12_hours );
    clear_map();
    clear_avatar();
}

auto charged_device( avatar &who, const itype_id &type, int charges ) -> item &
{
    item &device = who.i_add( item::spawn( type ) );
    device.ammo_set( itype_battery, charges );
    REQUIRE( device.ammo_remaining() == charges );
    return device;
}

// stores a file in the device's e-file pocket (no memory check) and returns it
auto store_file( item &device, const itype_id &file, bool browsed = true ) -> item &
{
    const std::vector<item *> before = device.efiles();
    REQUIRE( device.put_in( item::spawn( file ), pocket_type::E_FILE_STORAGE, false, nullptr,
                            true ).success() );
    for( item *stored : device.efiles() ) {
        if( std::find( before.begin(), before.end(), stored ) == before.end() ) {
            stored->set_browsed( browsed );
            return *stored;
        }
    }
    FAIL( "the stored file was not found" );
    return device;
}

// lays an item on the floor @p distance tiles east of @p who and returns it
auto lay_east( avatar &who, const itype_id &type, int distance ) -> item &
{
    const tripoint_bub_ms here = who.bub_pos();
    detached_ptr<item> laid = item::spawn( type );
    item &placed = *laid;
    get_map().add_item( tripoint_bub_ms( here.x() + distance, here.y(), here.z() ),
                        std::move( laid ) );
    return placed;
}

auto items_of( const efile_menu_rows &rows ) -> std::vector<const item *>
{
    std::vector<const item *> found;
    for( const efile_menu_row &row : rows ) {
        found.push_back( row.it );
    }
    return found;
}

auto offers( const efile_menu_rows &rows, const item &it ) -> bool
{
    const std::vector<const item *> found = items_of( rows );
    return std::find( found.begin(), found.end(), &it ) != found.end();
}

auto find_row( const efile_menu_rows &rows, const item &it ) -> const efile_menu_row *
{
    for( const efile_menu_row &row : rows ) {
        if( row.it == &it ) {
            return &row;
        }
    }
    return nullptr;
}

auto count_of_type( const efile_menu_rows &rows, const itype_id &type, int column ) -> int
{
    return std::count_if( rows.begin(), rows.end(), [&]( const efile_menu_row & row ) {
        return row.it->typeId() == type && row.column == column;
    } );
}

auto join_cells( const inventory_selector::header_stats_line &line ) -> std::string
{
    return join( line, "" );
}

auto fill_selector( inventory_selector &sel, avatar &who, int radius ) -> void
{
    sel.add_character_items( who );
    sel.add_nearby_items( radius );
}

auto entries_of( const inventory_column &col ) -> std::vector<const inventory_entry *>
{
    std::vector<const inventory_entry *> found;
    for( const inventory_entry &entry : col.entries ) {
        if( entry.is_item() ) {
            found.push_back( &entry );
        }
    }
    return found;
}

} // namespace

// DDA game_inventory.cpp:1395-1445. The reader picker shows only e-storage items; an item whose
// battery cannot power a use is greyed with the DDA wording.
TEST_CASE( "efile_menu_ereader_preset_shows_devices_and_denies_dead_batteries",
           "[efile][menu][ereader]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 10 );
    item &dead = who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    item &rock = who.i_add( item::spawn( itype_rock ) );

    CHECK( game_menus::inv::ereader_preset_shows( who, tablet ) );
    CHECK( game_menus::inv::ereader_preset_shows( who, dead ) );
    CHECK_FALSE( game_menus::inv::ereader_preset_shows( who, book ) );
    CHECK_FALSE( game_menus::inv::ereader_preset_shows( who, rock ) );

    CHECK( game_menus::inv::ereader_preset_denial( who, tablet ).empty() );
    CHECK( game_menus::inv::ereader_preset_denial( who, dead ) == "Needs at least 1 charge." );
}

// DDA game_inventory.cpp:1612-1663 (ebookread_inventory_preset): a book is denied with the first
// reason the reading check gives, and not otherwise.
TEST_CASE( "efile_menu_ebookread_preset_denies_what_the_reader_cannot_read",
           "[efile][menu][ereader]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );

    CHECK( game_menus::inv::ebookread_preset_denial( who, book ).empty() );

    // a blind reader cannot read (avatar::get_book_reader: "It's too dark to read!")
    who.add_effect( effect_blind, 1_hours );
    REQUIRE( who.is_blind() );
    std::vector<std::string> reasons;
    REQUIRE( who.get_book_reader( book, reasons ) == nullptr );
    REQUIRE_FALSE( reasons.empty() );
    CHECK( game_menus::inv::ebookread_preset_denial( who, book ) == reasons.front() );
}

// DDA game_inventory.cpp:1612-1663: the picker lists the books stored on the chosen reader only.
TEST_CASE( "efile_menu_ebookread_lists_the_books_of_the_reader", "[efile][menu][ereader]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 10 );
    item &other = charged_device( who, itype_test_laptop, 10 );
    item &book = store_file( tablet, itype_test_efile_scan_book_120 );
    item &junk = store_file( tablet, itype_test_efile_small );
    item &other_book = store_file( other, itype_test_efile_scan_book_240 );

    const efile_menu_rows rows = game_menus::inv::ebookread_rows( who, &tablet );
    CHECK( offers( rows, book ) );
    CHECK_FALSE( offers( rows, junk ) );
    CHECK_FALSE( offers( rows, other_book ) );
    CHECK( rows.size() == 1 );
}

// DDA inventory_ui.cpp:2165-2220: the selector adders.
TEST_CASE( "efile_menu_selector_adds_the_ebooks_and_efiles_of_a_device", "[efile][menu][selector]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 10 );
    item &book = store_file( tablet, itype_test_efile_scan_book_120 );
    item &small = store_file( tablet, itype_test_efile_small );
    item &rock = who.i_add( item::spawn( itype_rock ) );

    SECTION( "add_contained_ebooks lists only the books" ) {
        inventory_pick_selector sel( who );
        CHECK( sel.add_contained_ebooks( &tablet ) );
        const std::vector<const inventory_entry *> found = entries_of( sel.own_inv_column );
        REQUIRE( found.size() == 1 );
        CHECK( found[0]->any_item() == &book );
        CHECK( entries_of( sel.own_gear_column ).empty() );
        CHECK( entries_of( sel.map_column ).empty() );
    }
    SECTION( "add_contained_efiles lists every file" ) {
        inventory_pick_selector sel( who );
        CHECK( sel.add_contained_efiles( &tablet ) );
        const std::vector<const inventory_entry *> found = entries_of( sel.own_inv_column );
        REQUIRE( found.size() == 2 );
        std::vector<const item *> listed;
        for( const inventory_entry *entry : found ) {
            listed.push_back( entry->any_item() );
        }
        CHECK( std::find( listed.begin(), listed.end(), &book ) != listed.end() );
        CHECK( std::find( listed.begin(), listed.end(), &small ) != listed.end() );
    }
    SECTION( "an item that is no e-storage adds nothing" ) {
        inventory_pick_selector sel( who );
        CHECK_FALSE( sel.add_contained_ebooks( &rock ) );
        CHECK_FALSE( sel.add_contained_efiles( &rock ) );
        CHECK( entries_of( sel.own_inv_column ).empty() );
    }
    SECTION( "add_character_ebooks lists the books of every device the character carries" ) {
        item &laptop = charged_device( who, itype_test_laptop, 10 );
        item &laptop_book = store_file( laptop, itype_test_efile_scan_book_240 );
        inventory_pick_selector sel( who );
        sel.add_character_ebooks( who );
        const std::vector<const inventory_entry *> found = entries_of( sel.own_inv_column );
        std::vector<const item *> listed;
        for( const inventory_entry *entry : found ) {
            listed.push_back( entry->any_item() );
        }
        CHECK( found.size() == 2 );
        CHECK( std::find( listed.begin(), listed.end(), &book ) != listed.end() );
        CHECK( std::find( listed.begin(), listed.end(), &laptop_book ) != listed.end() );
    }
}

// DDA inventory_ui.cpp:2017-2040, 2105-2140: add_efiles makes the walk list the files of an
// e-device as rows instead of walking its pockets.
TEST_CASE( "efile_menu_selector_add_efiles_lists_the_files_of_devices", "[efile][menu][selector]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 10 );
    item &small = store_file( tablet, itype_test_efile_small );
    item &ground_tablet = lay_east( who, itype_test_eink_tablet_pc, 1 );
    item &ground_small = store_file( ground_tablet, itype_test_efile_medium );

    const auto listed = [&]( inventory_selector & sel, const item & it ) {
        for( inventory_column *col : { &sel.own_gear_column, &sel.own_inv_column,
                                       &sel.map_column
                                     } ) {
            for( const inventory_entry *entry : entries_of( *col ) ) {
                if( entry->any_item() == &it ) {
                    return true;
                }
            }
        }
        return false;
    };

    SECTION( "without add_efiles the files are not rows" ) {
        inventory_pick_selector sel( who );
        sel.add_character_items( who );
        sel.add_nearby_items( 2 );
        CHECK( listed( sel, tablet ) );
        CHECK_FALSE( listed( sel, small ) );
        CHECK_FALSE( listed( sel, ground_small ) );
    }
    SECTION( "with add_efiles carried and nearby devices give their files" ) {
        inventory_pick_selector sel( who );
        sel.add_character_items( who, true );
        sel.add_nearby_items( 2, true );
        CHECK( listed( sel, tablet ) );
        CHECK( listed( sel, small ) );
        CHECK( listed( sel, ground_tablet ) );
        CHECK( listed( sel, ground_small ) );
    }
}

// DDA inventory_ui.cpp:3599-3625: one entry per itype in each column; a stack is cut to one
// location. CDF has no itype variants, so include_variants changes nothing.
TEST_CASE( "efile_menu_selector_remove_duplicate_itypes_keeps_one_row_per_type",
           "[efile][menu][selector]" )
{
    start_clean();
    avatar &who = get_avatar();
    for( int i = 0; i < 3; ++i ) {
        who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    }
    who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );
    for( int i = 0; i < 2; ++i ) {
        lay_east( who, itype_test_efile_scan_book_120, 2 );
    }

    for( const bool include_variants : { false, true } ) {
        CAPTURE( include_variants );
        inventory_pick_selector sel( who );
        fill_selector( sel, who, 3 );
        sel.remove_duplicate_itypes( include_variants );
        const std::vector<const inventory_entry *> carried = entries_of( sel.own_inv_column );
        const std::vector<const inventory_entry *> ground = entries_of( sel.map_column );
        // two types carried, one type on the ground: one row each, one location each
        CHECK( carried.size() == 2 );
        for( const inventory_entry *entry : carried ) {
            CHECK( entry->locations.size() == 1 );
        }
        REQUIRE( ground.size() == 1 );
        CHECK( ground[0]->locations.size() == 1 );
        CHECK( ground[0]->any_item()->typeId() == itype_test_efile_scan_book_120 );
    }
}

// DDA game_inventory.cpp:1666-1716: what the scan menu offers.
TEST_CASE( "efile_menu_ebooksave_offers_owned_copiable_unsaved_books_within_reach",
           "[efile][menu][ebooksave]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    // already saved on the reader: the type is hidden everywhere
    store_file( tablet, itype_test_efile_scan_book_120 );
    item &saved_copy = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    item &fresh = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );
    item &generic = who.i_add( item::spawn( itype_test_generic_book_fixture ) );
    item &rock = who.i_add( item::spawn( itype_rock ) );

    const efile_menu_rows rows = game_menus::inv::ebooksave_rows( who, &tablet );
    CHECK( offers( rows, fresh ) );
    CHECK_FALSE( offers( rows, saved_copy ) );
    // a generic book is not copiable (DDA is_ecopiable)
    CHECK_FALSE( offers( rows, generic ) );
    CHECK_FALSE( offers( rows, rock ) );
    // the files of the reader itself are not rows
    for( const efile_menu_row &row : rows ) {
        CHECK( row.enabled );
        CHECK( row.denial.empty() );
    }
}

TEST_CASE( "efile_menu_ebooksave_reach_is_pickup_range", "[efile][menu][ebooksave]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    REQUIRE( PICKUP_RANGE == 6 );
    item &near_book = lay_east( who, itype_test_efile_scan_book_120, PICKUP_RANGE );
    item &far_book = lay_east( who, itype_test_efile_scan_book_240, PICKUP_RANGE + 1 );

    const efile_menu_rows rows = game_menus::inv::ebooksave_rows( who, &tablet );
    CHECK( offers( rows, near_book ) );
    CHECK_FALSE( offers( rows, far_book ) );
    const efile_menu_row *row = find_row( rows, near_book );
    REQUIRE( row != nullptr );
    CHECK( row->column == 2 );
}

TEST_CASE( "efile_menu_ebooksave_skips_books_owned_by_others", "[efile][menu][ebooksave]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    item &mine = lay_east( who, itype_test_efile_scan_book_120, 2 );
    item &theirs = lay_east( who, itype_test_efile_scan_book_240, 3 );
    theirs.set_owner( faction_id( "hells_raiders" ) );

    const efile_menu_rows rows = game_menus::inv::ebooksave_rows( who, &tablet );
    CHECK( offers( rows, mine ) );
    CHECK_FALSE( offers( rows, theirs ) );
}

TEST_CASE( "efile_menu_ebooksave_collapses_duplicate_itypes", "[efile][menu][ebooksave]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    for( int i = 0; i < 2; ++i ) {
        who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );
        lay_east( who, itype_test_efile_scan_book_240, 2 );
    }

    const efile_menu_rows rows = game_menus::inv::ebooksave_rows( who, &tablet );
    // one row for the carried type and one for the floor type, each cut to one location
    CHECK( count_of_type( rows, itype_test_efile_scan_book_240, 1 ) == 1 );
    CHECK( count_of_type( rows, itype_test_efile_scan_book_240, 2 ) == 1 );
    for( const efile_menu_row &row : rows ) {
        CHECK( row.locations == 1 );
    }
}

// DDA game_inventory.cpp:1666-1716: the header shows the charges and the time the marked books
// need, from the statics of the activity.
TEST_CASE( "efile_menu_ebooksave_header_shows_charges_and_time", "[efile][menu][ebooksave]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );
    const std::vector<item *> books = { &book };
    const int charges = ebooksave_activity_actor::required_charges( books, &tablet );
    const time_duration time = ebooksave_activity_actor::required_time( books );

    drop_locations chosen;
    chosen.emplace_back( book, 1 );
    const inventory_selector::header_stats stats = game_menus::inv::ebooksave_stats( &tablet,
            chosen );
    REQUIRE( stats.size() >= 2 );
    const std::string charge_line = join_cells( stats[0] );
    const std::string time_line = join_cells( stats[1] );
    CHECK( charge_line.find( "Charges" ) != std::string::npos );
    CHECK( charge_line.find( std::to_string( charges ) ) != std::string::npos );
    CHECK( charge_line.find( "50" ) != std::string::npos );
    CHECK( time_line.find( "Estimated time" ) != std::string::npos );
    CHECK( time_line.find( to_string( time ) ) != std::string::npos );
}

namespace
{

// A scene for the device picker: the used laptop L1 (browsed, two files), a phone P, a second
// laptop L2 (browsed, a file), a USB-A stick U, a plain card E (no use action), a card with a
// forbidden pocket F and a tablet T without a charge.
struct device_scene {
    item *laptop = nullptr;
    item *phone = nullptr;
    item *laptop2 = nullptr;
    item *usb = nullptr;
    item *card = nullptr;
    item *forbidden = nullptr;
    item *dead_tablet = nullptr;
};

auto make_scene( avatar &who ) -> device_scene
{
    device_scene s;
    s.laptop = &charged_device( who, itype_test_laptop, 50 );
    s.laptop->set_browsed( true );
    store_file( *s.laptop, itype_test_efile_copiable );
    store_file( *s.laptop, itype_test_efile_small );
    s.phone = &charged_device( who, itype_test_smart_phone, 50 );
    s.laptop2 = &charged_device( who, itype_test_laptop, 50 );
    s.laptop2->set_browsed( true );
    store_file( *s.laptop2, itype_test_efile_medium );
    s.usb = &who.i_add( item::spawn( itype_test_efile_usb_a ) );
    s.card = &who.i_add( item::spawn( itype_test_efile_etd ) );
    s.forbidden = &who.i_add( item::spawn( itype_test_efile_forbidden_device ) );
    s.dead_tablet = &who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    return s;
}

auto offered_set( const efile_menu_rows &rows, const device_scene &s ) -> std::vector<const item *>
{
    std::vector<const item *> found;
    for( const item *dev : { s.laptop, s.phone, s.laptop2, s.usb, s.card, s.forbidden,
                             s.dead_tablet } ) {
        if( offers( rows, *dev ) ) {
            found.push_back( dev );
        }
    }
    return found;
}

} // namespace

// DDA game_inventory.cpp:1726-1795.
TEST_CASE( "efile_menu_edevice_select_honours_browsed_power_and_forbidden_pockets",
           "[efile][menu][edevice]" )
{
    start_clean();
    avatar &who = get_avatar();
    const device_scene s = make_scene( who );
    REQUIRE( s.laptop->is_browsed() );
    REQUIRE_FALSE( s.phone->is_browsed() );

    SECTION( "browse: the unbrowsed, compatible, powered, allowed devices" ) {
        const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, s.laptop, false,
                                     true, false, EF_BROWSE );
        // the laptops are browsed, the tablet has no charge, the card with the forbidden pocket is
        // out; the phone, the USB stick and the plain card are offered
        const std::vector<const item *> expected = { s.phone, s.usb, s.card };
        CHECK( offered_set( rows, s ) == expected );
    }
    SECTION( "browsed devices: the used device is included only on request" ) {
        const efile_menu_rows with_used = game_menus::inv::edevice_select_rows( who, s.laptop, true,
                                          true, false, EF_BROWSE );
        const std::vector<const item *> expected_with = { s.laptop, s.laptop2 };
        CHECK( offered_set( with_used, s ) == expected_with );
        const efile_menu_rows without_used = game_menus::inv::edevice_select_rows( who, s.laptop,
                                             true, false, false, EF_BROWSE );
        const std::vector<const item *> expected_without = { s.laptop2 };
        CHECK( offered_set( without_used, s ) == expected_without );
    }
    SECTION( "unusable_only keeps the devices without a use action" ) {
        const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, s.laptop, false,
                                     true, true, EF_BROWSE );
        const std::vector<const item *> expected = { s.usb, s.card };
        CHECK( offered_set( rows, s ) == expected );
    }
    SECTION( "the forbidden pocket is never offered" ) {
        for( const bool browse_equals : { false, true } ) {
            const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, s.laptop,
                                         browse_equals, true, false, EF_BROWSE );
            CHECK_FALSE( offers( rows, *s.forbidden ) );
        }
    }
}

TEST_CASE( "efile_menu_edevice_select_checks_compatibility_except_when_browsing",
           "[efile][menu][edevice]" )
{
    start_clean();
    avatar &who = get_avatar();
    const device_scene s = make_scene( who );
    // the phone bans USB-A: the pair is incompatible
    REQUIRE( efile_activity_actor::edevices_compatible( s.phone, s.usb ) ==
             efile_activity_actor::ECOMPAT_NONE );
    s.usb->set_browsed( true );
    s.card->set_browsed( true );

    SECTION( "an action that moves files refuses an incompatible device" ) {
        const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, s.phone, true,
                                     false, false, EF_MOVE_FROM_THIS );
        CHECK_FALSE( offers( rows, *s.usb ) );
        CHECK( offers( rows, *s.card ) );
    }
    SECTION( "browsing a browsed device needs no compatibility" ) {
        const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, s.phone, true,
                                     false, false, EF_BROWSE );
        CHECK( offers( rows, *s.usb ) );
    }
    SECTION( "reading refuses an incompatible device and, as DDA does, lets a slow one through" ) {
        // DDA game_inventory.cpp:1766-1768: the clause "reading needs a fast pair" is followed by
        // "any compatible device", so a slow pair is offered too (the first clause decides nothing)
        s.phone->set_browsed( true );
        const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, s.laptop, true,
                                     true, false, EF_READ );
        CHECK( offers( rows, *s.laptop2 ) );
        CHECK( offers( rows, *s.phone ) );
        const efile_menu_rows from_phone = game_menus::inv::edevice_select_rows( who, s.phone, true,
                                           true, false, EF_READ );
        CHECK_FALSE( offers( from_phone, *s.usb ) );
        CHECK( offers( from_phone, *s.laptop ) );
    }
}

TEST_CASE( "efile_menu_edevice_select_for_receiving_files_needs_browsed_devices_with_files",
           "[efile][menu][edevice]" )
{
    start_clean();
    avatar &who = get_avatar();
    const device_scene s = make_scene( who );
    s.phone->set_browsed( true );
    REQUIRE( s.phone->efiles().empty() );

    for( const efile_action action : { EF_MOVE_ONTO_THIS, EF_COPY_ONTO_THIS } ) {
        CAPTURE( action );
        const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, s.laptop, true,
                                     false, false, action );
        // laptop2 is browsed and holds a file; the phone is browsed but empty
        CHECK( offers( rows, *s.laptop2 ) );
        CHECK_FALSE( offers( rows, *s.phone ) );
        CHECK_FALSE( offers( rows, *s.laptop ) );
    }
}

TEST_CASE( "efile_menu_edevice_select_looks_at_nearby_devices", "[efile][menu][edevice]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    laptop.set_browsed( true );
    item &near_card = lay_east( who, itype_test_efile_etd, PICKUP_RANGE );
    item &far_card = lay_east( who, itype_test_efile_etd_tiny, PICKUP_RANGE + 1 );
    item &theirs = lay_east( who, itype_test_efile_usb_a, 2 );
    theirs.set_owner( faction_id( "hells_raiders" ) );

    const efile_menu_rows rows = game_menus::inv::edevice_select_rows( who, &laptop, false, true,
                                 false, EF_BROWSE );
    CHECK( offers( rows, near_card ) );
    CHECK_FALSE( offers( rows, far_card ) );
    CHECK_FALSE( offers( rows, theirs ) );
}

// DDA game_inventory.cpp:1797-1900.
TEST_CASE( "efile_menu_efile_select_lists_only_the_files_of_the_chosen_devices",
           "[efile][menu][efile]" )
{
    start_clean();
    avatar &who = get_avatar();
    const device_scene s = make_scene( who );
    const std::vector<item *> laptop_files = s.laptop->efiles();
    const std::vector<item *> laptop2_files = s.laptop2->efiles();
    REQUIRE( laptop_files.size() == 2 );
    REQUIRE( laptop2_files.size() == 1 );

    SECTION( "a from action lists the files of the used device" ) {
        const efile_menu_rows rows = game_menus::inv::efile_select_rows( who, s.laptop,
                                     { s.laptop2 }, EF_MOVE_FROM_THIS, true );
        CHECK( rows.size() == 2 );
        for( const item *file : laptop_files ) {
            CHECK( offers( rows, *file ) );
        }
        CHECK_FALSE( offers( rows, *laptop2_files[0] ) );
    }
    SECTION( "an onto action lists the files of the chosen devices" ) {
        const efile_menu_rows rows = game_menus::inv::efile_select_rows( who, s.laptop,
                                     { s.laptop2 }, EF_MOVE_ONTO_THIS, false );
        CHECK( rows.size() == 1 );
        CHECK( offers( rows, *laptop2_files[0] ) );
    }
    SECTION( "copying lists only copiable files" ) {
        const efile_menu_rows rows = game_menus::inv::efile_select_rows( who, s.laptop,
                                     { s.laptop2 }, EF_COPY_FROM_THIS, true );
        REQUIRE( rows.size() == 1 );
        CHECK( rows[0].it->typeId() == itype_test_efile_copiable );
        const efile_menu_rows moved = game_menus::inv::efile_select_rows( who, s.laptop,
                                      { s.laptop2 }, EF_MOVE_FROM_THIS, true );
        CHECK( moved.size() == 2 );
    }
    SECTION( "reading lists the files of the used device" ) {
        const efile_menu_rows rows = game_menus::inv::efile_select_rows( who, s.laptop, {},
                                     EF_READ, false );
        CHECK( rows.size() == 2 );
    }
}

TEST_CASE( "efile_menu_efile_select_header_shows_memory_charges_and_time", "[efile][menu][efile]" )
{
    start_clean();
    avatar &who = get_avatar();
    who.set_skill_level( skill_id( "computer" ), 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    item &phone = charged_device( who, itype_test_smart_phone, 30 );
    item &medium = store_file( laptop, itype_test_efile_medium );
    drop_locations chosen;
    chosen.emplace_back( medium, 1 );
    const auto header = [&]( item & used, const std::vector<item *> &targets, efile_action action,
    bool from_used ) {
        return game_menus::inv::efile_select_stats( who, &used, targets, action, from_used,
                chosen );
    };

    SECTION( "memory, charges and time of a fast copy onto the phone" ) {
        const inventory_selector::header_stats stats = header( laptop, { &phone },
                EF_COPY_FROM_THIS, true );
        REQUIRE( stats.size() == 3 );
        const std::string memory_line = join_cells( stats[0] );
        const std::string charge_line = join_cells( stats[1] );
        const std::string time_line = join_cells( stats[2] );
        CHECK( memory_line.find( "Memory" ) != std::string::npos );
        CHECK( memory_line.find( units::display( medium.ememory_size() ) ) != std::string::npos );
        CHECK( memory_line.find( units::display( phone.remaining_ememory() ) ) !=
               std::string::npos );
        CHECK( memory_line.find( get_tag_from_color( c_red ) ) == std::string::npos );
        CHECK( charge_line.find( "Charges" ) != std::string::npos );
        CHECK( charge_line.find( "30" ) != std::string::npos );
        CHECK( time_line.find( "Estimated time" ) != std::string::npos );
        CHECK( time_line.find( "slow" ) == std::string::npos );
    }
    SECTION( "a file that does not fit turns the memory red" ) {
        item &tiny = who.i_add( item::spawn( itype_test_efile_etd_tiny ) );
        const inventory_selector::header_stats stats = header( laptop, { &tiny }, EF_COPY_FROM_THIS,
                true );
        REQUIRE( stats.size() == 3 );
        CHECK( join_cells( stats[0] ).find( get_tag_from_color( c_red ) ) != std::string::npos );
    }
    SECTION( "two devices without high-speed compatibility are flagged slow" ) {
        item &laptop2 = charged_device( who, itype_test_laptop, 50 );
        const inventory_selector::header_stats stats = header( laptop, { &laptop2 },
                EF_COPY_FROM_THIS, true );
        REQUIRE( stats.size() == 3 );
        CHECK( join_cells( stats[2] ).find( "(slow!)" ) != std::string::npos );
        SECTION( "an external transfer device takes the flag away" ) {
            item &card = who.i_add( item::spawn( itype_test_efile_etd ) );
            card.set_browsed( true );
            const inventory_selector::header_stats fast = header( laptop, { &laptop2 },
                    EF_COPY_FROM_THIS, true );
            REQUIRE( fast.size() == 3 );
            CHECK( join_cells( fast[2] ).find( "(slow!)" ) == std::string::npos );
        }
    }
}

// RECONCILE smoke/U64, DDA game_inventory.cpp:2601-2635 (smokable_selector_preset, smoke_food).
TEST_CASE( "efile_menu_smoke_food_offers_unspoiled_smokable_food", "[efile][menu][smoke]" )
{
    start_clean();
    detached_ptr<item> fresh = item::spawn( itype_meat, calendar::turn, 2 );
    CHECK( game_menus::inv::smoke_food_preset_shows( *fresh ) );
    detached_ptr<item> rotten = item::spawn( itype_meat, calendar::turn, 2 );
    rotten->set_rot( 365_days );
    REQUIRE( rotten->rotten() );
    CHECK_FALSE( game_menus::inv::smoke_food_preset_shows( *rotten ) );
    detached_ptr<item> stone = item::spawn( itype_rock );
    CHECK_FALSE( game_menus::inv::smoke_food_preset_shows( *stone ) );
}

TEST_CASE( "efile_menu_smoke_food_header_shows_the_volume_of_the_marked_food",
           "[efile][menu][smoke]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &meat = who.i_add( item::spawn( itype_meat, calendar::turn, 4 ) );
    REQUIRE( meat.count() == 4 );
    const units::volume total = units::from_liter( 20 );
    const units::volume used = units::from_liter( 5 );
    // two of the four charges: half the volume of the stack
    drop_locations chosen;
    chosen.emplace_back( meat, 2 );
    const units::volume added = meat.volume() * 2 / meat.count();
    const inventory_selector::header_stats stats = game_menus::inv::smoke_food_stats( total, used,
            chosen );
    REQUIRE( stats.size() == 1 );
    const std::string line = join_cells( stats[0] );
    CHECK( line.find( "Volume" ) != std::string::npos );
    CHECK( line.find( format_volume( used + added ) ) != std::string::npos );
    CHECK( line.find( format_volume( total ) ) != std::string::npos );
}
