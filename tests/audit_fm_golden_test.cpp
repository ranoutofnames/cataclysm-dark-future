// PL-63 FM8: golden text files for the FM1-FM7 selector formatting.
//
// The PL-62 AT4 harness (tests/selector_golden_test.cpp) pins the AT3 fixture: a worn windbreaker
// with a small cardboard box inside.  FM8 adds the scenes of the FM8 draft that go beyond that
// fixture (S1, S3, S4, S5, S6, S7, S8, S9, S10; S2/S2b are the existing g2/g2b scenes) and one
// more window size where the AT4 scene only had 100x40.  Each scene is rendered with the AT3
// headless draw helper (tests/selector_draw_test.cpp) and compared row by row with
// tests/data/golden/ui/fm_<scene>_<width>x<height>.txt.
//
// The expected rows are hand-written from the DDA drawing code at b4b62254ae2 (see the REPORT for
// the per-scene DDA file:line table).  They pin the layout, the row texts (item names with the
// DETAILED_CONTAINERS suffix), the chevrons, the < > highlight carets, the mark column, the
// selection column and the footer.  Header stat lines are pinned only where they come from the
// container being filled (S9); the avatar pocket summary (S7) depends on the avatar's body and is
// only checked for its labels, as in AT4.
//
// CDF_GOLDEN_UPDATE=1 rewrites a golden file and passes with a warning (PLAN.md section 4 rule 6).
//
// DDA anchors (b4b62254ae2 inventory_ui.cpp unless noted):
//  * listing worn items and their contents: 2195-2212; chevron = vis_contents: 2040, drawn 1800-1808
//  * DETAILED_CONTAINERS suffix: 250-265, 810-860 (option default WORN: options.cpp:2200-2209)
//  * collapse hides contents, filter shows them again: 575-601, 1561-1564
//  * hide-all cycle: 3585-3600; hierarchy mode indent/sort: 139, 1432-1458
//  * < > carets (INVENTORY_HIGHLIGHT, default symbol): 1799-1829; options.cpp:1976-1985
//  * multiselect marks and selection column: 1849-1856, 1877-1904, 4399-4404, 4406-4411
//  * drop header (summary + Selected line): 4917-4939; insert header: 3987-4016, 4733-4756
//  * pickup title/caption/hint: game_inventory.cpp:2219-2226, inventory_ui.cpp:4993-5016
//  * unload two-line header: 5302-5315

#include "audit_support.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#if defined(TILES) || defined(_WIN32)
#include "cursesport.h"
#endif
#include "game.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_parent_checks.h"
#include "item_pocket.h"
#include "map/map.h"
#include "player_helpers.h"
#include "ret_val.h"
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
// Golden file handling.  AT4's copy lives in a namespace private to selector_golden_test.cpp, so
// this file carries the same small helpers; the two must stay in step.
// ---------------------------------------------------------------------------------------------
const char *const fm_golden_dir = "tests/data/golden/ui/";
const char *const fm_golden_update_env = "CDF_GOLDEN_UPDATE";

bool fm_golden_update_requested()
{
    const char *const value = std::getenv( fm_golden_update_env );
    return value != nullptr && *value != '\0' && std::string( value ) != "0";
}

std::vector<std::string> fm_read_golden_file( const std::string &path )
{
    std::vector<std::string> lines;
    std::ifstream in( path );
    std::string line;
    while( std::getline( in, line ) ) {
        lines.push_back( line );
    }
    return lines;
}

void fm_write_golden_file( const std::string &path, const std::vector<std::string> &lines )
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

std::string fm_golden_path( const std::string &name )
{
    return std::string( fm_golden_dir ) + name + ".txt";
}

// A unified diff of the first 40 differing rows: "@@ line N @@" then the expected and the actual
// row.  A line count mismatch is a difference too (a missing or extra row).
std::string fm_golden_diff( const std::vector<std::string> &expected,
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

struct fm_golden_result {
    bool ok = false;
    std::string message;
};

fm_golden_result fm_compare_golden_at( const std::string &path,
                                       const std::vector<std::string> &actual, bool update )
{
    if( update ) {
        fm_write_golden_file( path, actual );
        return { true, "updated " + path };
    }
    if( !std::filesystem::exists( path ) ) {
        return { false, "golden file missing: " + path +
                 "\nrun with CDF_GOLDEN_UPDATE=1 to create it" };
    }
    const std::vector<std::string> expected = fm_read_golden_file( path );
    if( expected == actual ) {
        return { true, std::string() };
    }
    return { false, "golden mismatch: " + path + "\n" + fm_golden_diff( expected, actual ) };
}

void fm_compare_golden( const std::string &name, const std::vector<std::string> &actual )
{
    const fm_golden_result result =
        fm_compare_golden_at( fm_golden_path( name ), actual, fm_golden_update_requested() );
    if( !result.ok ) {
        FAIL( result.message );
    }
    if( fm_golden_update_requested() ) {
        WARN( result.message );
    }
}
} // namespace

#if defined(TILES) || defined(_WIN32)
// Friend of inventory_selector (inventory_ui.h:1007): the draw functions, w_inv and prepare_layout
// are private/protected.  Token for token the AT3/AT4 helper (tests/selector_draw_test.cpp): both
// translation units are linked into one test binary, so the class must have one definition.
struct inventory_selector_test_access {
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

    // Seeded-defect hook: the chevron is only a flag on the entry, cleared here so the check has
    // to notice that the glyph is gone.
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
// One string per window line, cells concatenated, trailing blanks trimmed (AT3 window_text).
std::vector<std::string> fm_window_text( const catacurses::window &win )
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

bool fm_any_row_contains( const std::vector<std::string> &rows, const std::string &needle )
{
    return std::any_of( rows.begin(), rows.end(), [&needle]( const std::string &row ) {
        return row.find( needle ) != std::string::npos;
    } );
}

// The view mode is process-wide (FM7).  Restore the four saved states and the uistate switches
// the scenes touch.
struct fm_view_mode_guard {
    inventory_selector::uimode sel = inventory_sel_default_state.uimode;
    inventory_selector::uimode inv = inventory_ui_default_state.uimode;
    inventory_selector::uimode pickup_sel = pickup_sel_default_state.uimode;
    inventory_selector::uimode pickup_ui = pickup_ui_default_state.uimode;
    bool numpad = uistate.numpad_navigation;
    bool unload_auto = uistate.unload_auto_contain;
    ~fm_view_mode_guard() {
        inventory_sel_default_state.uimode = sel;
        inventory_ui_default_state.uimode = inv;
        pickup_sel_default_state.uimode = pickup_sel;
        pickup_ui_default_state.uimode = pickup_ui;
        uistate.numpad_navigation = numpad;
        uistate.unload_auto_contain = unload_auto;
        uistate.hide_entries_override.reset();
    }
};

enum class fm_scene {
    none,        // empty avatar (S8 places map items itself)
    pack,        // S1/S3/S4/S5/S6
    jacket_box,  // S10
    drop,        // S7
    vest,        // S9
};

// One fixture for every FM8 scene.  The avatar is cleared first, then the scene builder of
// audit_support.cpp puts the items where the FM8 draft wants them.
struct fm_fixture {
    static avatar &fresh_avatar() {
        clear_all_state();
        uistate.numpad_navigation = false;
        uistate.unload_auto_contain = false;
        inventory_sel_default_state.uimode = inventory_selector::uimode::categories;
        inventory_ui_default_state.uimode = inventory_selector::uimode::categories;
        // DDA inventory_ui.cpp:347-348: the pickup menu keeps its own view mode (the UI one opens
        // in hierarchy mode); game_inventory.cpp:564-565 gives the preset the selector state.
        pickup_sel_default_state.uimode = inventory_selector::uimode::categories;
        pickup_ui_default_state.uimode = inventory_selector::uimode::hierarchy;
        return get_avatar();
    }

    fm_view_mode_guard view_mode;
    avatar &u = fresh_avatar();
    item *jacket = nullptr;
    item *pants = nullptr;
    item *pack = nullptr;
    item *box = nullptr;
    item *rock = nullptr;
    item *cube = nullptr;
    item *vest = nullptr;
    item *medium_plate = nullptr;
    item *large_plate = nullptr;

    explicit fm_fixture( fm_scene scene ) {
        clear_character( u, true );
        bool ok = true;
        switch( scene ) {
            case fm_scene::none:
                break;
            case fm_scene::pack:
                ok = audit::build_pack_scene( u, jacket, pants, pack, box, rock );
                break;
            case fm_scene::jacket_box:
                ok = audit::build_jacket_box_scene( u, jacket, box, cube );
                break;
            case fm_scene::drop:
                ok = audit::build_drop_scene( u, box, rock );
                break;
            case fm_scene::vest:
                ok = audit::build_vest_scene( u, vest, medium_plate, large_plate );
                break;
        }
        REQUIRE( ok );
        uistate.hide_entries_override.reset();
    }
};

inventory_entry *fm_entry_for( inventory_column &column, const item *it )
{
    for( inventory_entry &entry : column.entries ) {
        if( entry.is_item() && entry.any_item() == it ) {
            return &entry;
        }
    }
    return nullptr;
}

// Render, prove the scene is deterministic (two dumps in one process), then compare the golden.
void fm_check_scene( const std::string &name, inventory_selector &sel, int width, int height )
{
    inventory_selector_test_access::render( sel, width, height );
    const std::vector<std::string> first =
        fm_window_text( inventory_selector_test_access::window_of( sel ) );
    inventory_selector_test_access::render( sel, width, height );
    const std::vector<std::string> second =
        fm_window_text( inventory_selector_test_access::window_of( sel ) );

    CAPTURE( name );
    REQUIRE( static_cast<int>( first.size() ) == height );
    CHECK( first == second );
    fm_compare_golden( name, first );
}

void fm_check_both_sizes( const std::string &prefix, inventory_selector &sel )
{
    fm_check_scene( prefix + "_100x40", sel, 100, 40 );
    fm_check_scene( prefix + "_80x24", sel, 80, 24 );
}

// S1: the owner's default inventory.  A worn windbreaker, cargo pants and hiking backpack; the
// pack holds a small cardboard box, the box a rock.  Every row is open: the worn rows carry the
// DETAILED_CONTAINERS suffix (DDA inventory_ui.cpp:250-265, 810-860), the pack and the box carry
// the open chevron (DDA :2040, 1800-1808), and the box and the rock sit under their own category
// because the box takes the category of what is inside it (item::get_category).
TEST_CASE( "FM S1 default view with a hiking pack and cargo pants", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::pack );
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    fm_check_both_sizes( "fm_s1", sel );
}

// S3: SHOW_HIDE_CONTENTS_ALL pressed once (show all: the S1 rows) and twice (hide all: only the
// worn rows, the pack's chevron closed).  DDA inventory_ui.cpp:3585-3600, 590-592.
TEST_CASE( "FM S3 hide-all once and twice", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::pack );
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    sel.cycle_hide_override_all();
    fm_check_both_sizes( "fm_s3_once", sel );

    sel.cycle_hide_override_all();
    fm_check_both_sizes( "fm_s3_twice", sel );
}

// S4: hierarchy mode (VIEW_CATEGORY_MODE).  No category headers; the pack's contents sit indented
// under the pack row.  DDA inventory_ui.cpp:2195-2212 (hierarchy branch), 1432-1458 (indent sort),
// 1651-1673 (indent = 2 + entry indent).
TEST_CASE( "FM S4 hierarchy mode indents the pack contents", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::pack );
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    sel.toggle_categorize_contained();

    fm_check_both_sizes( "fm_s4_hierarchy", sel );
}

// S5: a row highlighted inside the pack.  The box is the selected row, so the pack row gets the
// parent caret '<' and the rock row the child caret '>' (over the chevron cell of the pack row).  DDA
// inventory_ui.cpp:1799-1829 (INVENTORY_HIGHLIGHT "symbol" default, options.cpp:1976-1985),
// 4335-4371 (which rows are marked).
TEST_CASE( "FM S5 parent and child carets inside the pack", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::pack );
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    REQUIRE( sel.select( f.box ) );
    sel.highlight();

    fm_check_both_sizes( "fm_s5_carets", sel );
}

// S6: a filter that matches an item inside the collapsed pack.  The pack is collapsed first (its
// contents leave the list), then the filter is typed: rows hidden by a collapsed parent are shown
// again and only the rows matching "rock" stay.  DDA inventory_ui.cpp:1561-1564 (filter beats
// collapse), item_search.cpp:208-212 (the filter sees the contents suffix).
TEST_CASE( "FM S6 filter inside the collapsed pack", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::pack );
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    REQUIRE( sel.select( f.pack ) );
    sel.toggle_selected_contents();
    sel.apply_filter( "rock" );

    fm_check_both_sizes( "fm_s6_filter_rock", sel );
}

// S7: drop selector with two marks, one of them a count.  The empty box is marked whole (+), the
// carried stack of three rocks 2 of 3 (#); the selection column on the right lists both.  The
// avatar pocket summary and the Selected line are only checked for their labels: their numbers
// come from the avatar's body, as in AT4.  DDA inventory_ui.cpp:1849-1856 (mark glyph),
// 4399-4404 (toggle_entry), 4406-4411 (selection column hidden on overflow), 4917-4939 (header).
TEST_CASE( "FM S7 drop selector with two marks and a count", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::drop );
    inventory_drop_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    inventory_entry *const box_entry = fm_entry_for( sel.own_inv_column, f.box );
    inventory_entry *const rock_entry = fm_entry_for( sel.own_inv_column, f.rock );
    REQUIRE( box_entry != nullptr );
    REQUIRE( rock_entry != nullptr );
    sel.toggle_entry( *rock_entry, 2 );
    sel.toggle_entry( *box_entry, 1 );

    // The header summary and Selected line exist with stats on (labels only, no numbers).
    sel.set_display_stats( true );
    inventory_selector_test_access::render( sel, 100, 40 );
    const std::vector<std::string> with_stats =
        fm_window_text( inventory_selector_test_access::window_of( sel ) );
    CHECK( fm_any_row_contains( with_stats, "Bulk Volume:" ) );
    CHECK( fm_any_row_contains( with_stats, "Selected" ) );

    sel.set_display_stats( false );
    fm_check_both_sizes( "fm_s7_drop", sel );
}

// S8: pickup selector from a tile: the title, the item captions and the two-line hint.  The tile
// holds a rock and a small cardboard box with a cube; the map square's name is the category.  The
// pickup preset lists the standard pockets (DDA game_inventory.cpp:562-565), so the cube is a
// child row; the pickup menu opens in hierarchy mode (inventory_ui.cpp:168-172).  Hint text:
// DDA inventory_ui.cpp:4993-5016; keys from input.cpp:464-484 and data/raw/keybindings.
TEST_CASE( "FM S8 pickup selector from a tile", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::none );
    const tripoint_bub_ms pos = f.u.bub_pos();

    detached_ptr<item> box = item::spawn( itype_id( "box_small" ), calendar::turn );
    REQUIRE( box->put_in( item::spawn( itype_id( "test_1kg_cube" ), calendar::turn ),
                          pocket_type::CONTAINER ).success() );
    g->m.add_item_or_charges( pos, std::move( box ) );
    g->m.add_item_or_charges( pos, item::spawn( itype_id( "test_rock" ), calendar::turn ) );

    pickup_inventory_preset preset( f.u );
    preset.save_state = &pickup_ui_default_state;
    inventory_pickup_selector sel( f.u, preset );
    sel.set_display_stats( false );
    sel.set_title( _( "Pickup" ) );
    sel.add_map_items( pos );

    fm_check_both_sizes( "fm_s8_pickup", sel );
}

// S9: insert selector for a worn ballistic vest with its two large plate pockets filled.  The
// header shows the vest's volume and weight lines, a loose ESBI plate is offered, and a loose
// ESAPI plate is greyed with the denial text of a full large pocket.  DDA inventory_ui.cpp:3987-4016
// (container header), 4941-4958 (insert header + Selected), 907 inventory_holster_preset and
// game_inventory.cpp:2064-2085 (the real insert menu), 1849-1904 (selection column).
namespace
{
// The insert menu's preset, copied from src/game_inventory.cpp:1548-1621 (it is file local there).
// It decides which items the vest can take and why it refuses the others.
class fm_insert_preset : public inventory_selector_preset
{
    public:
        fm_insert_preset( item &holster, const Character &who ) : holster( holster ), who( who ) {}

        const item *get_holster() const override {
            return &holster;
        }

        bool is_shown( const item *contained ) const override {
            if( contained == &holster ) {
                return false;
            }
            if( is_ancestor( contained, holster ) || is_ancestor( &holster, *contained ) ) {
                return false;
            }
            if( !contained->is_container() && contained->made_of( phase_id::LIQUID ) &&
                !contained->is_frozen_liquid() ) {
                return false;
            }
            return contained->parent_item() != &holster;
        }

        std::string get_denial( const item *it ) const override {
            if( who.is_worn( *it ) ) {
                const ret_val<bool> ret = who.can_takeoff( *it );
                if( !ret.success() ) {
                    return ret.str();
                }
            }
            if( it->is_bucket_nonempty() ) {
                return _( "item would spill" );
            }
            detached_ptr<item> item_copy = item::spawn( *it );
            item_copy->charges = 1;
            ret_val<void> ret = holster.can_contain( *item_copy, false, false, true, false,
                                it->parent_item() );
            if( !ret.success() ) {
                return !ret.str().empty() ? ret.str() : _( "item can't be stored there" );
            }
            ret = parents_can_contain_recursive( holster, *item_copy );
            if( !ret.success() ) {
                return !ret.str().empty() ? ret.str() : _( "item can't be stored there" );
            }
            return {};
        }

    private:
        static bool is_ancestor( const item *maybe_ancestor, const item &it ) {
            for( const item *parent = it.parent_item(); parent != nullptr;
                 parent = parent->parent_item() ) {
                if( parent == maybe_ancestor ) {
                    return true;
                }
            }
            return false;
        }

        item &holster;
        const Character &who;
};
} // namespace

TEST_CASE( "FM S9 insert selector for a vest with plates", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::vest );
    fm_insert_preset preset( *f.vest, f.u );

    inventory_insert_selector sel( f.u, preset );
    sel.add_character_items( f.u );

    inventory_selector_test_access::render( sel, 100, 40 );
    const std::vector<std::string> rows =
        fm_window_text( inventory_selector_test_access::window_of( sel ) );
    CHECK( fm_any_row_contains( rows, "Contents Weight:" ) );
    CHECK( fm_any_row_contains( rows, "Selected" ) );

    fm_check_both_sizes( "fm_s9_insert", sel );
}

// S10: unload selector at both sizes.  Its own two-line hint pushes the rows down one line; the
// rows themselves are the AT4 fixture's.  DDA inventory_ui.cpp:5302-5315.
TEST_CASE( "FM S10 unload selector at both sizes", "[audit][golden][fm]" ) {
    fm_fixture f( fm_scene::jacket_box );
    unload_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    fm_check_both_sizes( "fm_s10_unload", sel );
}

// The seeded-defect proof: the comparison reports a changed row (temp dir, never a repo file) and
// a scene with the chevron flags cleared no longer matches its golden.
TEST_CASE( "FM golden compare fails on a changed line", "[audit][golden][fm][defect]" ) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "cdf_fm8_golden_selftest";
    std::filesystem::remove_all( dir );
    const std::string path = ( dir / "scene.txt" ).string();

    fm_write_golden_file( path, { "line one", "line two", "line three" } );

    const fm_golden_result same = fm_compare_golden_at( path,
                                  { "line one", "line two", "line three" }, false );
    CHECK( same.ok );

    const fm_golden_result changed = fm_compare_golden_at( path,
    { "line one", "line TWO", "line three" }, false );
    CHECK_FALSE( changed.ok );
    CHECK( changed.message.find( "@@ line 2 @@" ) != std::string::npos );
    CHECK( changed.message.find( "-line two" ) != std::string::npos );
    CHECK( changed.message.find( "+line TWO" ) != std::string::npos );

    const fm_golden_result shorter = fm_compare_golden_at( path, { "line one" }, false );
    CHECK_FALSE( shorter.ok );
    CHECK( shorter.message.find( "@@ line 2 @@" ) != std::string::npos );

    std::filesystem::remove_all( dir );
}

TEST_CASE( "FM seeded defect: clearing the chevrons breaks the S1 golden",
           "[audit][golden][fm][defect]" ) {
    fm_fixture f( fm_scene::pack );
    inventory_selector sel( f.u );
    sel.set_display_stats( false );
    sel.add_character_items( f.u );

    inventory_selector_test_access::render( sel, 100, 40 );
    const std::vector<std::string> rendered =
        fm_window_text( inventory_selector_test_access::window_of( sel ) );
    REQUIRE( fm_any_row_contains( rendered, "▼" ) );

    inventory_selector_test_access::clear_chevrons( sel );
    inventory_selector_test_access::render( sel, 100, 40 );
    const std::vector<std::string> defect =
        fm_window_text( inventory_selector_test_access::window_of( sel ) );

    CHECK_FALSE( fm_any_row_contains( defect, "▼" ) );
    CHECK( rendered != defect );

    const fm_golden_result result =
        fm_compare_golden_at( fm_golden_path( "fm_s1_100x40" ), defect, false );
    CHECK_FALSE( result.ok );
    CHECK( result.message.find( "@@ line " ) != std::string::npos );
}
} // namespace
#endif // TILES || _WIN32
