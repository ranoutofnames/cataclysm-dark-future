// PL-62 AT4: golden text files for the selector screens.
//
// A scene renders a fixed selector with the AT3 headless draw helper and compares the dumped
// rows, line by line, with tests/data/golden/ui/<scene>.txt.  Any change to a scene is a diff the
// orchestrator can read, with no human looking at the game.  CDF_GOLDEN_UPDATE=1 rewrites a
// golden file and passes with a warning (PLAN.md section 4 rule 6, section 8 risk 5).
//
// The scenes are built on AT3's fixture (a worn windbreaker holding a small cardboard box, the
// box holding a TEST 1kg cube) and on the AT3 render helper.  They switch the header stats rows
// off: the stats depend on the avatar's body weight, capacity and every worn pocket, and this
// packet has no game build to derive those numbers from.  What these goldens pin is the column
// layout, the category rows, the chevrons, the marks and the footer (see the REPORT: which golden
// files were written by hand from the code, not from a run).
//
// The expected rows are derived from DDA's inventory_ui.cpp at b4b62254ae2:
//  * a parent row carries a chevron, collapsed (▶) or open (▼): DDA inventory_ui.cpp:1799-1806
//  * collapse hides the contents rows: DDA inventory_ui.cpp:578-601 (is_hidden)
//  * hide-all (SHOW_HIDE_CONTENTS_ALL) cycles show-all -> hide-all: DDA inventory_ui.cpp:3589-3600
//  * categories view puts each container's contents under their natural category header:
//    DDA inventory_ui.cpp:2195-2211 (add_character_items)
//  * footer "[key] Filter", "< [key] categories >" and the category-selection label:
//    DDA inventory_ui.cpp:3408-3434, 3915-3916
//  * multiselect marks - / + / # and the selection column: DDA inventory_ui.cpp:1848-1856,
//    1877-1904, 4399-4404

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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
// ---------------------------------------------------------------------------------------------
// Golden file handling (step 1).  split out of the compare so T1/T2 can drive it on a temp file.
// ---------------------------------------------------------------------------------------------
const char *const golden_dir = "tests/data/golden/ui/";
const char *const golden_update_env = "CDF_GOLDEN_UPDATE";

bool golden_update_requested()
{
    const char *const value = std::getenv( golden_update_env );
    return value != nullptr && *value != '\0' && std::string( value ) != "0";
}

std::vector<std::string> read_golden_file( const std::string &path )
{
    std::vector<std::string> lines;
    std::ifstream in( path );
    std::string line;
    while( std::getline( in, line ) ) {
        lines.push_back( line );
    }
    return lines;
}

void write_golden_file( const std::string &path, const std::vector<std::string> &lines )
{
    const std::filesystem::path file( path );
    if( file.has_parent_path() ) {
        std::filesystem::create_directories( file.parent_path() );
    }
    std::ofstream out( path, std::ios::trunc );
    for( const std::string &line : lines ) {
        out << line << '\n';
    }
}

// A unified diff of the first 40 differing rows: "@@ line N @@" then the expected and the actual
// row.  A line count mismatch is a difference too (a missing or extra row).
std::string golden_diff( const std::vector<std::string> &expected,
                         const std::vector<std::string> &actual )
{
    std::string out;
    const size_t count = std::max( expected.size(), actual.size() );
    size_t shown = 0;
    for( size_t i = 0; i < count && shown < 40; ++i ) {
        const std::string empty;
        const std::string &e = i < expected.size() ? expected[i] : empty;
        const std::string &a = i < actual.size() ? actual[i] : empty;
        if( i < expected.size() && i < actual.size() && e == a ) {
            continue;
        }
        out += string_format( "@@ line %d @@\n-%s\n+%s\n", static_cast<int>( i ) + 1, e, a );
        ++shown;
    }
    return out;
}

struct golden_result {
    bool ok = false;
    std::string message;
};

golden_result compare_golden_at( const std::string &path, const std::vector<std::string> &actual,
                                 bool update )
{
    if( update ) {
        write_golden_file( path, actual );
        return { true, "updated " + path };
    }
    if( !std::filesystem::exists( path ) ) {
        return { false, "golden file missing: " + path +
                 "\nrun with CDF_GOLDEN_UPDATE=1 to create it" };
    }
    const std::vector<std::string> expected = read_golden_file( path );
    if( expected == actual ) {
        return { true, std::string() };
    }
    return { false, "golden mismatch: " + path + "\n" + golden_diff( expected, actual ) };
}

// Compare the dump of one scene with tests/data/golden/ui/<name>.txt.
void compare_golden( const std::string &name, const std::vector<std::string> &actual )
{
    const std::string path = std::string( golden_dir ) + name + ".txt";
    const golden_result result = compare_golden_at( path, actual, golden_update_requested() );
    if( !result.ok ) {
        FAIL( result.message );
    }
    if( golden_update_requested() ) {
        WARN( result.message );
    }
}
} // namespace

#if defined(TILES) || defined(_WIN32)
// Friend of inventory_selector (inventory_ui.h:999): the draw functions, w_inv and prepare_layout
// are private/protected.  This lives at global scope so the friend declaration matches it.  The
// three members are copied token for token from tests/selector_draw_test.cpp (AT3): both
// translation units are linked into one test binary, so the class must have one definition.
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
// G5: the ITEM/CATEGORY navigation mode toggle is protected; a probe can call it without touching
// the friend class above.
struct navigation_mode_probe : inventory_selector {
    using inventory_selector::inventory_selector;
    void enter_category_mode() {
        toggle_navigation_mode();
    }
};

// One string per window line, cells concatenated, trailing blanks trimmed (AT3 window_text).
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

// The view mode is process-wide (FM7).  Restore the four saved states and the uistate switches the
// scenes touch.
struct selector_view_mode_guard {
    inventory_selector::uimode sel = inventory_sel_default_state.uimode;
    inventory_selector::uimode inv = inventory_ui_default_state.uimode;
    inventory_selector::uimode pickup_sel = pickup_sel_default_state.uimode;
    inventory_selector::uimode pickup_ui = pickup_ui_default_state.uimode;
    bool numpad = uistate.numpad_navigation;
    bool unload_auto = uistate.unload_auto_contain;
    ~selector_view_mode_guard() {
        inventory_sel_default_state.uimode = sel;
        inventory_ui_default_state.uimode = inv;
        pickup_sel_default_state.uimode = pickup_sel;
        pickup_ui_default_state.uimode = pickup_ui;
        uistate.numpad_navigation = numpad;
        uistate.unload_auto_contain = unload_auto;
        uistate.hide_entries_override.reset();
    }
};

// A worn windbreaker holding a small cardboard box; the box holds a TEST 1kg cube.  The drop
// scene also carries a stack of test_rock charges in the body storage (a carried stack, not a
// loose map item: PLAN.md section 4 rule 1) so its mark can show a count.
struct selector_fixture {
    static avatar &fresh_avatar() {
        clear_all_state();
        uistate.numpad_navigation = false;
        uistate.unload_auto_contain = false;
        inventory_sel_default_state.uimode = inventory_selector::uimode::categories;
        return get_avatar();
    }

    selector_view_mode_guard view_mode;
    avatar &u = fresh_avatar();
    item *jacket = nullptr;
    item *box = nullptr;
    item *content = nullptr;
    item *rock = nullptr;

    explicit selector_fixture( int rock_charges = 0 ) {
        clear_character( u, true );
        u.wear_item( item::spawn( itype_id( "jacket_windbreaker" ), calendar::turn ), false );
        jacket = u.worn.front();

        detached_ptr<item> box_ptr = item::spawn( itype_id( "box_small" ), calendar::turn );
        item *const box_raw = box_ptr.get();
        REQUIRE( jacket->put_in( std::move( box_ptr ), pocket_type::CONTAINER ).success() );
        box = box_raw;

        detached_ptr<item> content_ptr = item::spawn( itype_id( "test_1kg_cube" ), calendar::turn );
        item *const content_raw = content_ptr.get();
        REQUIRE( box->put_in( std::move( content_ptr ), pocket_type::CONTAINER ).success() );
        content = content_raw;

        if( rock_charges > 0 ) {
            rock = &u.i_add( item::spawn( itype_id( "test_rock" ), calendar::turn, rock_charges ) );
        }

        uistate.hide_entries_override.reset();
    }
};

inventory_entry *entry_for( inventory_column &column, const item *it )
{
    for( inventory_entry &entry : column.entries ) {
        if( entry.is_item() && entry.any_item() == it ) {
            return &entry;
        }
    }
    return nullptr;
}

// Render, prove the scene is deterministic (two dumps in one process), then compare the golden.
void check_scene( const std::string &name, inventory_selector &sel, int width, int height )
{
    inventory_selector_test_access::render( sel, width, height );
    const std::vector<std::string> first =
        window_text( inventory_selector_test_access::window_of( sel ) );
    inventory_selector_test_access::render( sel, width, height );
    const std::vector<std::string> second =
        window_text( inventory_selector_test_access::window_of( sel ) );

    CAPTURE( name );
    REQUIRE( static_cast<int>( first.size() ) == height );
    CHECK( first == second );
    compare_golden( name, first );
}

// T1: the comparison reports a changed row and prints a diff of it.  The golden lives in a temp
// dir, so the seeded defect never touches the repo files.
TEST_CASE( "golden compare fails on a changed line and prints a diff", "[audit][golden]" ) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "cdf_at4_golden_selftest";
    std::filesystem::remove_all( dir );
    const std::string path = ( dir / "scene.txt" ).string();

    write_golden_file( path, { "line one", "line two", "line three" } );

    const golden_result same = compare_golden_at( path, { "line one", "line two", "line three" },
                               false );
    CHECK( same.ok );

    const golden_result changed = compare_golden_at( path,
    { "line one", "line TWO", "line three" }, false );
    CHECK_FALSE( changed.ok );
    CHECK( changed.message.find( "@@ line 2 @@" ) != std::string::npos );
    CHECK( changed.message.find( "-line two" ) != std::string::npos );
    CHECK( changed.message.find( "+line TWO" ) != std::string::npos );

    const golden_result shorter = compare_golden_at( path, { "line one" }, false );
    CHECK_FALSE( shorter.ok );
    CHECK( shorter.message.find( "@@ line 2 @@" ) != std::string::npos );

    std::filesystem::remove_all( dir );
}

// T2: the update path rewrites the file (the CDF_GOLDEN_UPDATE=1 branch of compare_golden).
TEST_CASE( "golden update flag rewrites a file in a temp dir", "[audit][golden]" ) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "cdf_at4_golden_update";
    std::filesystem::remove_all( dir );
    const std::string path = ( dir / "scene.txt" ).string();

    write_golden_file( path, { "old line" } );
    const golden_result updated = compare_golden_at( path, { "new line", "second line" }, true );
    CHECK( updated.ok );
    CHECK( updated.message.find( "updated" ) != std::string::npos );
    CHECK( read_golden_file( path ) == std::vector<std::string>{ "new line", "second line" } );

    std::filesystem::remove_all( dir );
}

// G1: default inventory view (categories), jacket and box open, box contents listed.  Both window
// sizes (the draft's G1*).
TEST_CASE( "G1 default view golden", "[audit][golden]" ) {
    selector_fixture f;
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    check_scene( "g1_100x40", sel, 100, 40 );
    check_scene( "g1_80x24", sel, 80, 24 );
}

// G2/S2: the inner container collapsed.  The box keeps its row with ▶, the contents row is gone,
// the worn jacket row and its chevron do not move.
TEST_CASE( "G2 box collapsed golden", "[audit][golden]" ) {
    selector_fixture f;
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    REQUIRE( sel.select( f.box ) );
    sel.toggle_selected_contents();

    check_scene( "g2_box_collapsed_100x40", sel, 100, 40 );
    check_scene( "g2_box_collapsed_80x24", sel, 80, 24 );
}

// G2b/S2b: the owner's case, the WORN jacket collapsed.  The jacket row stays listed with ▶ and
// its contents rows (box and cube) are gone; with the inventory column empty the worn column is
// the only one and moves left.
TEST_CASE( "G2b worn jacket collapsed golden", "[audit][golden]" ) {
    selector_fixture f;
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    REQUIRE( sel.select( f.jacket ) );
    sel.toggle_selected_contents();

    check_scene( "g2b_jacket_collapsed_100x40", sel, 100, 40 );
    check_scene( "g2b_jacket_collapsed_80x24", sel, 80, 24 );
}

// G3: hide-all pressed once (show all contents, same rows as G1) and twice (only the worn rows,
// chevron collapsed).  DDA inventory_ui.cpp:3589-3600.
TEST_CASE( "G3 hide-all once and twice golden", "[audit][golden]" ) {
    selector_fixture f;
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    sel.cycle_hide_override_all();
    check_scene( "g3_hide_all_once_100x40", sel, 100, 40 );

    sel.cycle_hide_override_all();
    check_scene( "g3_hide_all_twice_100x40", sel, 100, 40 );
}

// G4: a filter that matches the carried cube only; the jacket row and the box row drop out and the
// filter text with the typed string is drawn in the footer.
TEST_CASE( "G4 filter golden", "[audit][golden]" ) {
    selector_fixture f;
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );
    sel.apply_filter( "cube" );

    check_scene( "g4_filter_cube_100x40", sel, 100, 40 );
}

// G5: category selection mode (CATEGORY_SELECTION).  The rows do not change; the footer names the
// mode between the ┤ ├ junctions.
TEST_CASE( "G5 category selection mode golden", "[audit][golden]" ) {
    selector_fixture f;
    navigation_mode_probe sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    sel.enter_category_mode();

    check_scene( "g5_category_mode_100x40", sel, 100, 40 );
}

// G6: drop selector with two marked rows and a count.  The box is marked whole (+), the carried
// rock stack is marked 2 of its 3 charges (#); the selection column lists both with its own caption.
TEST_CASE( "G6 drop selector marks golden", "[audit][golden]" ) {
    selector_fixture f( 3 );
    inventory_drop_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    inventory_entry *const box_entry = entry_for( sel.own_inv_column, f.box );
    inventory_entry *const rock_entry = entry_for( sel.own_inv_column, f.rock );
    REQUIRE( box_entry != nullptr );
    REQUIRE( rock_entry != nullptr );
    sel.toggle_entry( *box_entry, 1 );
    sel.toggle_entry( *rock_entry, 2 );

    check_scene( "g6_drop_marks_100x40", sel, 100, 40 );
}

// G8: unload selector.  Its own two-line header (title row plus the Confirm/Cancel/contain hint)
// pushes the rows down one line; the rows themselves are the default view's.
TEST_CASE( "G8 unload selector golden", "[audit][golden]" ) {
    selector_fixture f;
    unload_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    check_scene( "g8_unload_100x40", sel, 100, 40 );
}
} // namespace
#endif // TILES || _WIN32
