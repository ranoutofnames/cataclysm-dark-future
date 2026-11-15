// PL-62 AT3: headless draw dump for the inventory selector.
//
// The render helper below is the test-side twin of inventory_selector::refresh_window(): it sizes
// w_inv, lays the columns out for that size and calls the four private draw functions in the same
// order.  No live ui_adaptor is needed, and nothing is drawn to a real screen: the curses port
// keeps the cells in cata_cursesport::WINDOW::line, which window_cells() copies out.
//
// The grid tests only exist in the curses-backed builds (TILES or _WIN32).  The font coverage test
// (T4b) is data-only and runs everywhere.

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#if defined(TILES) || defined(_WIN32)
#include "cursesport.h"
#endif
#include "inventory_ui.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "translations.h"
#include "uistate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
// T4b: the non-ASCII glyphs the selector can draw.  The chevrons are inventory_ui.cpp:1533, the
// ellipsis is inventory_ui.cpp:2744 ("…"), and the frame glyphs come from draw_frame() ->
// draw_border()/mvwhline() (output.h LINE_*_S, U+2500..U+2524).
struct glyph_probe {
    const char *name;
    uint32_t codepoint;
};

const std::array<glyph_probe, 11> selector_glyphs = {{
        { "▶", 0x25B6 },
        { "▼", 0x25BC },
        { "…", 0x2026 },
        { "─", 0x2500 },
        { "│", 0x2502 },
        { "┌", 0x250C },
        { "┐", 0x2510 },
        { "└", 0x2514 },
        { "┘", 0x2518 },
        { "├", 0x251C },
        { "┤", 0x2524 },
    }
};

std::vector<unsigned char> read_binary_file( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    return std::vector<unsigned char>( std::istreambuf_iterator<char>( in ),
                                       std::istreambuf_iterator<char>() );
}

uint16_t read_u16( const std::vector<unsigned char> &data, size_t offset )
{
    return static_cast<uint16_t>( ( data[offset] << 8 ) | data[offset + 1] );
}

uint32_t read_u32( const std::vector<unsigned char> &data, size_t offset )
{
    return ( static_cast<uint32_t>( data[offset] ) << 24 ) |
           ( static_cast<uint32_t>( data[offset + 1] ) << 16 ) |
           ( static_cast<uint32_t>( data[offset + 2] ) << 8 ) |
           static_cast<uint32_t>( data[offset + 3] );
}

bool cmap_format4_has( const std::vector<unsigned char> &data, size_t offset, uint32_t codepoint )
{
    if( codepoint > 0xffff || offset + 14 > data.size() ) {
        return false;
    }
    const size_t seg_count_x2 = read_u16( data, offset + 6 );
    const size_t seg_count = seg_count_x2 / 2;
    const size_t end_codes = offset + 14;
    const size_t start_codes = end_codes + seg_count_x2 + 2; // skip reservedPad
    const size_t id_deltas = start_codes + seg_count_x2;
    const size_t id_range_offsets = id_deltas + seg_count_x2;
    if( id_range_offsets + seg_count_x2 > data.size() ) {
        return false;
    }
    for( size_t i = 0; i < seg_count; ++i ) {
        const uint16_t end = read_u16( data, end_codes + 2 * i );
        const uint16_t start = read_u16( data, start_codes + 2 * i );
        if( codepoint < start || codepoint > end ) {
            continue;
        }
        const uint16_t delta = read_u16( data, id_deltas + 2 * i );
        const uint16_t range_offset = read_u16( data, id_range_offsets + 2 * i );
        if( range_offset == 0 ) {
            return ( ( codepoint + delta ) & 0xffff ) != 0;
        }
        const size_t glyph_offset = id_range_offsets + 2 * i + range_offset +
                                    2 * ( codepoint - start );
        if( glyph_offset + 2 > data.size() ) {
            return false;
        }
        uint16_t glyph = read_u16( data, glyph_offset );
        if( glyph != 0 ) {
            glyph = static_cast<uint16_t>( ( glyph + delta ) & 0xffff );
        }
        return glyph != 0;
    }
    return false;
}

bool cmap_format12_has( const std::vector<unsigned char> &data, size_t offset, uint32_t codepoint )
{
    if( offset + 16 > data.size() ) {
        return false;
    }
    const uint32_t group_count = read_u32( data, offset + 12 );
    for( uint32_t i = 0; i < group_count; ++i ) {
        const size_t group = offset + 16 + 12 * static_cast<size_t>( i );
        if( group + 12 > data.size() ) {
            return false;
        }
        const uint32_t start = read_u32( data, group );
        const uint32_t end = read_u32( data, group + 4 );
        const uint32_t start_glyph = read_u32( data, group + 8 );
        if( codepoint >= start && codepoint <= end ) {
            return start_glyph + ( codepoint - start ) != 0;
        }
    }
    return false;
}

// Reads the TrueType cmap table directly (no font library needed).  Prefers the full Unicode
// (3,10) format 12 subtable, then (3,1) format 4.
bool font_has_glyph( const std::vector<unsigned char> &data, uint32_t codepoint )
{
    if( data.size() < 12 ) {
        return false;
    }
    const size_t table_count = read_u16( data, 4 );
    size_t cmap_offset = 0;
    for( size_t i = 0; i < table_count; ++i ) {
        const size_t record = 12 + 16 * i;
        if( record + 16 > data.size() ) {
            return false;
        }
        if( data[record] == 'c' && data[record + 1] == 'm' && data[record + 2] == 'a' &&
            data[record + 3] == 'p' ) {
            cmap_offset = read_u32( data, record + 8 );
            break;
        }
    }
    if( cmap_offset == 0 || cmap_offset + 4 > data.size() ) {
        return false;
    }
    const size_t subtable_count = read_u16( data, cmap_offset + 2 );
    size_t best_offset = 0;
    int best_score = -1;
    for( size_t i = 0; i < subtable_count; ++i ) {
        const size_t record = cmap_offset + 4 + 8 * i;
        if( record + 8 > data.size() ) {
            break;
        }
        const uint16_t platform = read_u16( data, record );
        const uint16_t encoding = read_u16( data, record + 2 );
        const size_t offset = cmap_offset + read_u32( data, record + 4 );
        if( offset + 2 > data.size() ) {
            continue;
        }
        const uint16_t format = read_u16( data, offset );
        int score = -1;
        if( format == 12 && platform == 3 && encoding == 10 ) {
            score = 100;
        } else if( format == 12 ) {
            score = 80;
        } else if( format == 4 && platform == 3 && encoding == 1 ) {
            score = 60;
        } else if( format == 4 && platform == 0 ) {
            score = 50;
        } else if( format == 4 ) {
            score = 40;
        }
        if( score > best_score ) {
            best_score = score;
            best_offset = offset;
        }
    }
    if( best_score < 0 ) {
        return false;
    }
    return read_u16( data, best_offset ) == 12
           ? cmap_format12_has( data, best_offset, codepoint )
           : cmap_format4_has( data, best_offset, codepoint );
}
} // namespace

#if defined(TILES) || defined(_WIN32)
// Friend of inventory_selector: the draw functions and w_inv are private.  This lives at global
// scope so the friend declaration in inventory_ui.h matches it.
struct inventory_selector_test_access {
    // Mirror inventory_selector::refresh_window() without a ui_adaptor.  The borders take four
    // columns/rows; the columns get the client size the draw functions actually see.
    static void render( inventory_selector &sel, int width, int height ) {
        sel.w_inv = catacurses::newwin( height, width, point_zero );
        sel.prepare_layout( static_cast<size_t>( std::max( 1, width - 4 ) ),
                            static_cast<size_t>( std::max( 2, height - 4 ) ) );
        catacurses::werase( sel.w_inv );
        sel.draw_frame( sel.w_inv );
        sel.draw_header( sel.w_inv );
        sel.draw_columns( sel.w_inv );
        sel.draw_footer( sel.w_inv );
    }

    static catacurses::window window_of( const inventory_selector &sel ) {
        return sel.w_inv;
    }

    // Seeded-defect hook for T4: the chevron is only a flag on the entry, cleared here so the
    // check function has to notice that the glyph is gone.
    static void clear_chevrons( inventory_selector &sel ) {
        for( inventory_column *col : { &sel.own_gear_column, &sel.own_inv_column, &sel.map_column } ) {
            for( inventory_entry &entry : col->entries ) {
                entry.chevron = false;
            }
        }
    }
};

namespace
{
// One string per window line, cells concatenated, trailing blanks trimmed.
std::vector<std::string> window_text( const catacurses::window &win )
{
    std::vector<std::string> rows;
    for( const std::vector<cata_cursesport::cursecell> &line :
         cata_cursesport::window_cells( win ) ) {
        std::string row;
        for( const cata_cursesport::cursecell &cell : line ) {
            row += cell.ch;
        }
        while( !row.empty() && row.back() == ' ' ) {
            row.pop_back();
        }
        rows.push_back( row );
    }
    return rows;
}

// Colour-class letters, one per cell (space for a blank cell):
// K/R/G/Y/B/M/C/W by base colour, lowercase for the plain, uppercase for the bold variant.
std::string color_class( cata_cursesport::base_color color )
{
    static const char *const names = "krgybmcw";
    const int index = static_cast<int>( color );
    if( index < 0 || index > 15 ) {
        return "?";
    }
    const char name = names[index % 8];
    return std::string( 1, index >= 8 ? static_cast<char>( std::toupper( name ) ) : name );
}

std::vector<std::string> window_text_colored( const catacurses::window &win )
{
    std::vector<std::string> rows;
    for( const std::vector<cata_cursesport::cursecell> &line :
         cata_cursesport::window_cells( win ) ) {
        std::string row;
        for( const cata_cursesport::cursecell &cell : line ) {
            if( cell.ch.empty() || cell.ch == " " ) {
                row += ' ';
            } else {
                row += color_class( cell.FG );
            }
        }
        rows.push_back( row );
    }
    return rows;
}

bool any_row_contains( const std::vector<std::string> &rows, const std::string &needle )
{
    return std::any_of( rows.begin(), rows.end(), [&needle]( const std::string &row ) {
        return row.find( needle ) != std::string::npos;
    } );
}

// The visible row of an item in any column.
inventory_entry *row_for( inventory_selector &sel, const item *it )
{
    for( inventory_column *col : { &sel.own_gear_column, &sel.own_inv_column, &sel.map_column } ) {
        for( inventory_entry &entry : col->entries ) {
            if( entry.is_item() && entry.any_item() == it ) {
                return &entry;
            }
        }
    }
    return nullptr;
}

// The drawn text of one entry's row, plus the cell just left of the text: that is where draw()
// puts the chevron.
struct row_snapshot {
    std::string text;
    std::string chevron;
    cata_cursesport::base_color chevron_fg = static_cast<cata_cursesport::base_color>( 0 );
    int y = -1;
    int chevron_x = -1;
    bool has_chevron_cell = false;
};

row_snapshot dump_row( const inventory_selector &sel, const inventory_entry *entry )
{
    row_snapshot result;
    for( const inventory_entry_rect &rect : sel.rect_entry_map ) {
        if( rect.entry != entry ) {
            continue;
        }
        const std::vector<std::vector<cata_cursesport::cursecell>> grid =
            cata_cursesport::window_cells( inventory_selector_test_access::window_of( sel ) );
        const int y = rect.rect.p_min.y;
        if( y < 0 || static_cast<size_t>( y ) >= grid.size() ) {
            return result;
        }
        for( int x = rect.rect.p_min.x;
             x <= rect.rect.p_max.x && x >= 0 &&
             static_cast<size_t>( x ) < grid[y].size(); ++x ) {
            result.text += grid[y][x].ch;
        }
        result.y = y;
        const int chevron_x = rect.rect.p_min.x - 1;
        result.chevron_x = chevron_x;
        if( chevron_x >= 0 && static_cast<size_t>( chevron_x ) < grid[y].size() ) {
            result.has_chevron_cell = true;
            result.chevron = grid[y][chevron_x].ch;
            result.chevron_fg = grid[y][chevron_x].FG;
        }
        return result;
    }
    return result;
}

bool row_has_chevron( const row_snapshot &row )
{
    return row.chevron == "▼" || row.chevron == "▶";
}

// The view mode is process-wide (FM7).  Every draw test starts from the compiled-in defaults and
// puts the four saved states back, as the other view-mode tests do.
struct selector_view_mode_guard {
    inventory_selector::uimode sel = inventory_sel_default_state.uimode;
    inventory_selector::uimode inv = inventory_ui_default_state.uimode;
    inventory_selector::uimode pickup_sel = pickup_sel_default_state.uimode;
    inventory_selector::uimode pickup_ui = pickup_ui_default_state.uimode;
    ~selector_view_mode_guard() {
        inventory_sel_default_state.uimode = sel;
        inventory_ui_default_state.uimode = inv;
        pickup_sel_default_state.uimode = pickup_sel;
        pickup_ui_default_state.uimode = pickup_ui;
    }
};

// A worn jacket with a box inside; the box holds a rock.  Everything is worn or carried, so the
// fixture never hits the loose-item trap (a map item is not loaded the same way).
struct jacket_box_fixture {
    static avatar &fresh_avatar() {
        clear_all_state();
        inventory_sel_default_state.uimode = inventory_selector::uimode::categories;
        return get_avatar();
    }

    selector_view_mode_guard view_mode;
    avatar &u = fresh_avatar();
    item *jacket = nullptr;
    item *box = nullptr;
    item *rock = nullptr;

    jacket_box_fixture() {
        clear_character( u, true );
        u.wear_item( item::spawn( itype_id( "jacket_windbreaker" ), calendar::turn ), false );
        jacket = u.worn.front();

        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ), calendar::turn );
        item *const box_raw = box_ptr.get();
        REQUIRE( jacket->put_in( std::move( box_ptr ), pocket_type::CONTAINER ).success() );
        box = box_raw;

        detached_ptr<item> rock_ptr = item::spawn( itype_id( "test_rock" ), calendar::turn );
        item *const rock_raw = rock_ptr.get();
        REQUIRE( box->put_in( std::move( rock_ptr ), pocket_type::CONTAINER ).success() );
        rock = rock_raw;

        uistate.hide_entries_override.reset();
    }
};

struct hide_override_guard {
    hide_override_guard() {
        uistate.hide_entries_override.reset();
    }
    ~hide_override_guard() {
        uistate.hide_entries_override.reset();
    }
};

// T1
TEST_CASE( "window_text reads what was drawn", "[audit][ui]" ) {
    const catacurses::window win = catacurses::newwin( 4, 20, point_zero );
    catacurses::mvwprintw( win, point( 2, 1 ), "abc" );

    const std::vector<std::string> rows = window_text( win );
    REQUIRE( rows.size() == 4 );
    CHECK( rows[1] == "  abc" );
    CHECK( rows[0].empty() );
    CHECK( rows[3].empty() );
}

// T2
TEST_CASE( "render produces a non-empty frame for the default selector", "[audit][ui]" ) {
    const hide_override_guard guard;
    jacket_box_fixture f;
    inventory_selector sel( f.u );
    sel.add_character_items( f.u );

    for( const point size : { point( 100, 40 ), point( 80, 24 ) } ) {
        CAPTURE( size.x );
        CAPTURE( size.y );
        inventory_selector_test_access::render( sel, size.x, size.y );
        const std::vector<std::string> rows =
            window_text( inventory_selector_test_access::window_of( sel ) );
        REQUIRE( static_cast<int>( rows.size() ) == size.y );
        CHECK_FALSE( rows.empty() );
        CHECK( any_row_contains( rows, item::nname( itype_id( "jacket_windbreaker" ) ) ) );
        CHECK( any_row_contains( rows, "┌" ) );
        // the view-mode/filter footer line is always drawn
        CHECK( any_row_contains( rows, _("Filter") ) );
    }
}

// T3
TEST_CASE( "fixture wears its items", "[audit][ui]" ) {
    jacket_box_fixture f;
    CHECK( f.u.is_worn( *f.jacket ) );
    REQUIRE( f.box->parent_item() == f.jacket );
    REQUIRE( f.rock->parent_item() == f.box );
    // CDF: the box sits in the carry pocket of the worn jacket, so its location is the owning
    // top-level location, the character (src/locations.cpp:737-742). The rock sits in the box's
    // CONTAINER pocket, which is not a carry pocket, so its location is the box
    // (contents_item_location::where() is container, src/locations.cpp:678-681); parent_item()
    // above is what proves both nesting steps.
    CHECK( f.box->where() == item_location_type::character );
    CHECK( f.rock->where() == item_location_type::container );
}

// T4 and T5: the owner case.  A worn jacket holding a box shows the open chevron; collapsing the
// jacket turns it into the closed one and hides the box (and the rock inside it).  This is the
// DDA expectation (inventory_ui.cpp:1800-1807, 1520-1533 here).
TEST_CASE( "container rows show a chevron", "[audit][ui]" ) {
    const hide_override_guard guard;
    jacket_box_fixture f;
    inventory_selector sel( f.u );
    sel.add_character_items( f.u );

    SECTION( "open: jacket and box have the open chevron, the rock has none" ) {
        inventory_selector_test_access::render( sel, 100, 40 );
        inventory_entry *jacket_row = row_for( sel, f.jacket );
        inventory_entry *box_row = row_for( sel, f.box );
        inventory_entry *rock_row = row_for( sel, f.rock );
        REQUIRE( jacket_row != nullptr );
        REQUIRE( box_row != nullptr );
        REQUIRE( rock_row != nullptr );

        CHECK( dump_row( sel, jacket_row ).chevron == "▼" );
        CHECK( dump_row( sel, box_row ).chevron == "▼" );
        CHECK_FALSE( row_has_chevron( dump_row( sel, rock_row ) ) );
    }

    SECTION( "collapsing the worn jacket removes its contents rows" ) {
        inventory_selector_test_access::render( sel, 100, 40 );
        REQUIRE( sel.select( f.jacket ) );
        sel.toggle_selected_contents();

        inventory_selector_test_access::render( sel, 100, 40 );
        inventory_entry *jacket_row = row_for( sel, f.jacket );
        REQUIRE( jacket_row != nullptr );
        CHECK( dump_row( sel, jacket_row ).chevron == "▶" );
        CHECK( row_for( sel, f.box ) == nullptr );
        CHECK( row_for( sel, f.rock ) == nullptr );

        const std::vector<std::string> rows =
            window_text( inventory_selector_test_access::window_of( sel ) );
        CHECK_FALSE( any_row_contains( rows, item::nname( itype_id( "box_small" ) ) ) );
        CHECK_FALSE( any_row_contains( rows, item::nname( itype_id( "test_rock" ) ) ) );
    }

    SECTION( "expanding returns the contents rows" ) {
        inventory_selector_test_access::render( sel, 100, 40 );
        REQUIRE( sel.select( f.jacket ) );
        sel.toggle_selected_contents();
        inventory_selector_test_access::render( sel, 100, 40 );
        REQUIRE( sel.select( f.jacket ) );
        sel.toggle_selected_contents();

        inventory_selector_test_access::render( sel, 100, 40 );
        inventory_entry *jacket_row = row_for( sel, f.jacket );
        REQUIRE( jacket_row != nullptr );
        CHECK( dump_row( sel, jacket_row ).chevron == "▼" );
        CHECK( row_for( sel, f.box ) != nullptr );
        CHECK( row_for( sel, f.rock ) != nullptr );
    }

    SECTION( "the seeded defect: with the chevron flag off the check reports it" ) {
        inventory_selector_test_access::render( sel, 100, 40 );
        inventory_entry *jacket_row = row_for( sel, f.jacket );
        REQUIRE( jacket_row != nullptr );
        REQUIRE( row_has_chevron( dump_row( sel, jacket_row ) ) );

        inventory_selector_test_access::clear_chevrons( sel );
        inventory_selector_test_access::render( sel, 100, 40 );
        jacket_row = row_for( sel, f.jacket );
        REQUIRE( jacket_row != nullptr );
        CHECK_FALSE( row_has_chevron( dump_row( sel, jacket_row ) ) );
    }
}

// T10: the chevron cell carries the "has contents" colour (c_dark_gray for a normal container row),
// not the item text colour.
TEST_CASE( "chevron cells are coloured as has-contents", "[audit][ui]" ) {
    const hide_override_guard guard;
    jacket_box_fixture f;
    inventory_selector sel( f.u );
    sel.add_character_items( f.u );
    inventory_selector_test_access::render( sel, 100, 40 );

    inventory_entry *jacket_row = row_for( sel, f.jacket );
    REQUIRE( jacket_row != nullptr );
    const row_snapshot row = dump_row( sel, jacket_row );
    REQUIRE( row_has_chevron( row ) );
    cata_cursesport::base_color expected =
        cata_cursesport::colorpairs[c_dark_gray.to_color_pair_index()].FG;
    if( c_dark_gray.is_bold() ) {
        expected = static_cast<cata_cursesport::base_color>( expected + 8 );
    }
    CHECK( row.chevron_fg == expected );

    const std::vector<std::string> colored = window_text_colored(
                inventory_selector_test_access::window_of( sel ) );
    REQUIRE( row.y >= 0 );
    REQUIRE( static_cast<size_t>( row.y ) < colored.size() );
    REQUIRE( row.chevron_x >= 0 );
    REQUIRE( static_cast<size_t>( row.chevron_x ) < colored[row.y].size() );
    CHECK( colored[row.y][row.chevron_x] == color_class( expected )[0] );

    // the rock has no chevron cell at all, so its colour is not the chevron colour
    inventory_entry *rock_row = row_for( sel, f.rock );
    REQUIRE( rock_row != nullptr );
    const row_snapshot rock = dump_row( sel, rock_row );
    CHECK_FALSE( row_has_chevron( rock ) );
    CHECK_FALSE( rock.chevron_fg == expected );
}

// T6: the footer and a caller hint name the keys the input_context actually reports.  The base
// selector's footer carries the filter and view-mode keys; the inventory screen's own hint is set
// by its caller, so the test sets one from the same context.
TEST_CASE( "hint line names the bound keys", "[audit][ui]" ) {
    const hide_override_guard guard;
    jacket_box_fixture f;

    struct input_probe : inventory_selector {
        using inventory_selector::inventory_selector;
        using inventory_selector::ctxt;
    } probe( f.u );

    for( const char *action : { "INVENTORY_FILTER", "VIEW_CATEGORY_MODE", "SHOW_HIDE_CONTENTS",
                                "EXAMINE_CONTENTS", "NEXT_COLUMN", "PREV_COLUMN" } ) {
        CAPTURE( action );
        if( std::string( action ) == "PREV_COLUMN" ) {
            // DDA binds PREV_COLUMN to BACKTAB and shift+TAB, which are not printable, so
            // keys_bound_to() has no key to report (src/input.cpp:838-855); get_desc lists them
            // (DDA input_context.cpp:312-352).
            CHECK_FALSE( probe.ctxt.get_desc( action ).empty() );
        } else {
            CHECK_FALSE( probe.ctxt.keys_bound_to( action ).empty() );
        }
    }

    inventory_selector sel( f.u );
    sel.set_hint( string_format( "%s %s %s %s %s",
                                 probe.ctxt.get_desc( "SHOW_HIDE_CONTENTS" ),
                                 probe.ctxt.get_desc( "EXAMINE_CONTENTS" ),
                                 probe.ctxt.get_desc( "NEXT_COLUMN" ),
                                 probe.ctxt.get_desc( "PREV_COLUMN" ),
                                 probe.ctxt.get_desc( "INVENTORY_FILTER" ) ) );
    sel.add_character_items( f.u );
    inventory_selector_test_access::render( sel, 100, 40 );
    const std::vector<std::string> rows =
        window_text( inventory_selector_test_access::window_of( sel ) );

    for( const char *action : { "INVENTORY_FILTER", "VIEW_CATEGORY_MODE", "SHOW_HIDE_CONTENTS",
                                "EXAMINE_CONTENTS", "NEXT_COLUMN", "PREV_COLUMN" } ) {
        CAPTURE( action );
        CHECK( any_row_contains( rows, probe.ctxt.get_desc( action ) ) );
    }
}

// T8: the DDA action set AT2 settled is registered by the selector's input_context.
TEST_CASE( "required actions are registered in the selector", "[audit][ui]" ) {
    clear_all_state();
    avatar &u = get_avatar();

    struct action_probe : inventory_selector {
        using inventory_selector::inventory_selector;
        using inventory_selector::ctxt;
    } probe( u );

    const std::vector<std::string> actions = probe.ctxt.get_registered_actions_copy();
    for( const char *action : { "DOWN", "UP", "PAGE_DOWN", "PAGE_UP", "SCROLL_UP", "SCROLL_DOWN",
                                "NEXT_COLUMN", "PREV_COLUMN", "CONFIRM", "QUIT",
                                "CATEGORY_SELECTION", "TOGGLE_NUMPAD_NAVIGATION",
                                "TOGGLE_FAVORITE", "HOME", "END", "HELP_KEYBINDINGS",
                                "INVENTORY_FILTER", "EXAMINE", "ANY_INPUT", "ORGANIZE_MENU",
                                "VIEW_CATEGORY_MODE", "SHOW_HIDE_CONTENTS", "EXAMINE_CONTENTS",
                                "SHOW_HIDE_CONTENTS_ALL", "RESET_FILTER",
                                "TOGGLE_SKIP_UNSELECTABLE", "COORDINATE", "MOUSE_MOVE",
                                "SELECT", "CLICK_AND_DRAG" } ) {
        CAPTURE( action );
        CHECK( std::find( actions.begin(), actions.end(), action ) != actions.end() );
    }
}

// T9: the drop and insert selectors render, and their headers carry the used/total volume line.
TEST_CASE( "drop selector and insert selector draw", "[audit][ui]" ) {
    const hide_override_guard guard;
    jacket_box_fixture f;

    SECTION( "drop" ) {
        inventory_drop_selector sel( f.u );
        sel.add_character_items( f.u );
        inventory_selector_test_access::render( sel, 100, 40 );
        const std::vector<std::string> rows =
            window_text( inventory_selector_test_access::window_of( sel ) );
        CHECK_FALSE( rows.empty() );
        CHECK( any_row_contains( rows, "Bulk Volume:" ) );
    }

    SECTION( "insert" ) {
        struct holster_preset : inventory_selector_preset {
            const item *holster = nullptr;
            const item *get_holster() const override {
                return holster;
            }
        } preset;
        preset.holster = f.jacket;

        inventory_insert_selector sel( f.u, preset );
        sel.add_character_items( f.u );
        inventory_selector_test_access::render( sel, 100, 40 );
        const std::vector<std::string> rows =
            window_text( inventory_selector_test_access::window_of( sel ) );
        CHECK_FALSE( rows.empty() );
        CHECK( any_row_contains( rows, "Total Volume:" ) );
    }
}
} // namespace
#endif // TILES || _WIN32

// T4b: every non-ASCII glyph the selector draws is in at least one of the two shipped fonts.
TEST_CASE( "selector glyphs are covered by the shipped fonts", "[audit][ui]" ) {
    const std::vector<unsigned char> terminus = read_binary_file( "data/font/Terminus.ttf" );
    const std::vector<unsigned char> unifont = read_binary_file( "data/font/unifont.ttf" );
    REQUIRE_FALSE( terminus.empty() );
    REQUIRE_FALSE( unifont.empty() );

    for( const glyph_probe &glyph : selector_glyphs ) {
        CAPTURE( glyph.name );
        CAPTURE( glyph.codepoint );
        CHECK( ( font_has_glyph( terminus, glyph.codepoint ) ||
                 font_has_glyph( unifont, glyph.codepoint ) ) );
    }
}
