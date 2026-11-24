// CDF: PC-E1 step E1-5a (bookbinder_copy and ebooksave activity actors). Tests for DDA
// b4b62254ae2 activity_actor.cpp:1130-1200 (bookbinder_copy_activity_actor) and :3611-3770
// (ebooksave_activity_actor). Written before the code.
// DEVIATION: DDA has no test of either actor (tests/player_activities_test.cpp only builds the
// binder activity in a list of stubs), so every case here is CDF-new. The item-model checks come
// first: the actors keep safe_reference<item> members and arrays of them, so a bare vector of
// references is round-tripped before the actors rely on it.
#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "detached_ptr.h"
#include "effect.h"
#include "game_constants.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "coordinates.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "safe_reference.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

static const efftype_id effect_blind( "blind" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_paper( "paper" );
static const itype_id itype_efile_junk( "efile_junk" );
static const itype_id itype_efile_recipes( "efile_recipes" );
static const itype_id itype_pen( "pen" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_test_efile_binder( "test_efile_binder" );
static const itype_id itype_test_efile_identify_book( "test_efile_identify_book" );
static const itype_id itype_test_efile_scan_book_120( "test_efile_scan_book_120" );
static const itype_id itype_test_efile_scan_book_240( "test_efile_scan_book_240" );
static const itype_id itype_test_efile_copiable( "test_efile_copiable" );
static const itype_id itype_test_efile_etd( "test_efile_etd" );
static const itype_id itype_efile_photos( "efile_photos" );
static const itype_id itype_test_ebook_reader( "test_ebook_reader" );
static const itype_id itype_test_efile_etd_slow( "test_efile_etd_slow" );
static const itype_id itype_test_efile_etd_tiny( "test_efile_etd_tiny" );
static const itype_id itype_test_efile_usb_a( "test_efile_usb_a" );
static const itype_id itype_test_smart_phone( "test_smart_phone" );
static const itype_id itype_test_efile_medium( "test_efile_medium" );
static const itype_id itype_test_efile_small( "test_efile_small" );
static const itype_id itype_test_eink_tablet_pc( "test_eink_tablet_pc" );
static const itype_id itype_test_laptop( "test_laptop" );
static const itype_id itype_test_ereader_tiny( "test_ereader_tiny" );

static const recipe_id recipe_water_clean( "water_clean" );

static const skill_id skill_computer( "computer" );

namespace
{

// the clock starts on a ten-minute boundary so the battery rule (one check every
// pages_per_charge * time_per_page = 10 minutes) crosses exactly one boundary in 600 turns
auto start_clean_day() -> void
{
    clear_all_state();
    set_time( calendar::turn_zero + 12_hours );
}

// DDA run loop of the existing book scanning tests: one second a turn, 100 moves each
auto run_activity( avatar &who, int max_turns ) -> int
{
    int turns = 0;
    while( turns < max_turns && !who.activity->is_null() ) {
        calendar::turn += 1_turns;
        who.moves = 100;
        who.activity->do_turn( who );
        ++turns;
    }
    return turns;
}

auto assign_ebooksave( avatar &who, const std::vector<item *> &books, item &reader ) -> void
{
    auto actor = std::make_unique<ebooksave_activity_actor>( books, &reader );
    who.assign_activity( std::make_unique<player_activity>( std::move( actor ) ) );
}

auto assign_binder_copy( avatar &who, item &binder ) -> void
{
    auto actor = std::make_unique<bookbinder_copy_activity_actor>( binder, recipe_water_clean );
    who.assign_activity( std::make_unique<player_activity>( std::move( actor ) ) );
}

auto battery_reader( avatar &who, int charges ) -> item &
{
    item &reader = who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    reader.ammo_set( itype_battery, charges );
    REQUIRE( reader.ammo_remaining() == charges );
    return reader;
}

auto file_ids( const item &reader ) -> std::vector<itype_id>
{
    std::vector<itype_id> ids;
    for( const item *file : reader.efiles() ) {
        ids.push_back( file->typeId() );
    }
    return ids;
}

template<typename T>
auto json_of( const T &thing ) -> std::string
{
    std::ostringstream out;
    JsonOut jsout( out );
    thing.serialize( jsout );
    return out.str();
}

auto load_item( avatar &who, const std::string &text ) -> item &
{
    std::istringstream in( text );
    JsonIn jsin( in );
    detached_ptr<item> loaded = item::spawn( jsin );
    REQUIRE( loaded );
    return who.i_add( std::move( loaded ) );
}

// The real load order (cf. reload_activity_actor_survives_save_and_load): save the running activity
// and the given items, release the activity's references, destroy the originals as after a quit,
// load the items again, then load the activity: its references resolve to the loaded copies.
// Returns the loaded items in the order of @p items.
auto save_and_load( avatar &who, const std::vector<item *> &items ) -> std::vector<item *>
{
    const std::string activity_text = json_of( *who.activity );
    std::vector<std::string> item_texts;
    for( const item *it : items ) {
        item_texts.push_back( json_of( *it ) );
    }
    who.activity = std::make_unique<player_activity>();
    for( item *it : items ) {
        detached_ptr<item> gone = it->detach();
    }
    std::vector<item *> loaded;
    for( const std::string &text : item_texts ) {
        loaded.push_back( &load_item( who, text ) );
    }
    std::istringstream in( activity_text );
    JsonIn jsin( in );
    auto activity = std::make_unique<player_activity>();
    activity->deserialize( jsin );
    who.activity = std::move( activity );
    return loaded;
}

} // namespace

// A safe_reference is saved as an id that resolves only once the item itself is loaded again, so a
// round trip must go through the items too (the order of
// reload_activity_actor_survives_save_and_load).
TEST_CASE( "efile_actor_safe_reference_vectors_round_trip", "[efile][actor]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    std::string refs_text;
    std::string first_text;
    std::string second_text;
    {
        item &first = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
        item &second = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );
        std::vector<safe_reference<item>> refs;
        refs.emplace_back( &first );
        refs.emplace_back( &second );
        safe_reference<item> single( &first );

        std::ostringstream out;
        JsonOut jsout( out );
        jsout.start_object();
        jsout.member( "refs", refs );
        jsout.member( "single", single );
        jsout.end_object();
        refs_text = out.str();
        first_text = json_of( first );
        second_text = json_of( second );
        // the references are released before the originals go away
        refs.clear();
        single = safe_reference<item>();
        detached_ptr<item> gone_first = first.detach();
        detached_ptr<item> gone_second = second.detach();
    }
    item &first = load_item( who, first_text );
    item &second = load_item( who, second_text );

    std::vector<safe_reference<item>> loaded_refs;
    safe_reference<item> loaded_single;
    std::istringstream in( refs_text );
    JsonIn jsin( in );
    JsonObject data = jsin.get_object();
    data.read( "refs", loaded_refs );
    data.read( "single", loaded_single );

    REQUIRE( loaded_refs.size() == 2 );
    REQUIRE( static_cast<bool>( loaded_refs[0] ) );
    REQUIRE( static_cast<bool>( loaded_refs[1] ) );
    CHECK( loaded_refs[0].get() == &first );
    CHECK( loaded_refs[1].get() == &second );
    REQUIRE( static_cast<bool>( loaded_single ) );
    CHECK( loaded_single.get() == &first );
}

TEST_CASE( "efile_actor_safe_references_survive_moves_and_notice_destruction",
           "[efile][actor]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 10 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    safe_reference<item> book_ref( &book );
    safe_reference<item> reader_ref( &reader );
    REQUIRE( static_cast<bool>( book_ref ) );

    // a copy goes into the e-file pocket (put_in( const item & )): the original and its
    // reference stay
    REQUIRE( reader.put_in( book, pocket_type::E_FILE_STORAGE, false, &who, true ).success() );
    CHECK( static_cast<bool>( book_ref ) );
    CHECK( book_ref.get() == &book );
    CHECK( reader.efiles().size() == 1 );
    CHECK( reader.efiles()[0] != &book );

    // a reference to the stored file survives moving it onto another device
    item &other = who.i_add( item::spawn( itype_test_eink_tablet_pc ) );
    safe_reference<item> file_ref( reader.efiles()[0] );
    detached_ptr<item> detached = file_ref->detach();
    REQUIRE( other.put_in( std::move( detached ), pocket_type::E_FILE_STORAGE, false, &who,
                           true ).success() );
    REQUIRE( static_cast<bool>( file_ref ) );
    CHECK( file_ref->parent_item() == &other );
    CHECK( reader.efiles().empty() );

    // an item that is destroyed reads as invalid at once, before the end of the turn
    {
        detached_ptr<item> gone = book.detach();
    }
    CHECK_FALSE( static_cast<bool>( book_ref ) );
    CHECK( static_cast<bool>( reader_ref ) );
}

TEST_CASE( "bookbinder_copy_start_sets_ten_minutes_a_page", "[efile][actor][binder]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    const int pages = bookbinder_copy_activity_actor::pages_for_recipe( *recipe_water_clean );
    CHECK( pages == 1 + recipe_water_clean->difficulty / 2 );

    assign_binder_copy( who, binder );
    REQUIRE( who.activity->id() == activity_id( "ACT_BINDER_COPY_RECIPE" ) );
    CHECK( who.activity->get_moves_left() == to_moves<int>( pages * 10_minutes ) );
}

TEST_CASE( "bookbinder_copy_is_canceled_when_it_is_too_dark_to_write", "[efile][actor][binder]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    who.i_add( item::spawn( itype_paper, calendar::turn, 5 ) );
    who.i_add( item::spawn( itype_pen ) );
    assign_binder_copy( who, binder );
    REQUIRE_FALSE( who.activity->is_null() );

    who.add_effect( effect_blind, 1_hours );
    REQUIRE( who.is_blind() );
    run_activity( who, 1 );
    CHECK( who.activity->is_null() );
    CHECK( binder.get_saved_recipes().empty() );
}

TEST_CASE( "bookbinder_copy_stores_the_recipe_and_spends_paper_and_ink", "[efile][actor][binder]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    const int pages = bookbinder_copy_activity_actor::pages_for_recipe( *recipe_water_clean );
    item &paper = who.i_add( item::spawn( itype_paper, calendar::turn, pages + 4 ) );
    item &pen = who.i_add( item::spawn( itype_pen ) );
    const int pen_charges = pen.ammo_remaining();
    REQUIRE( pen_charges > pages );
    REQUIRE( paper.charges == pages + 4 );

    assign_binder_copy( who, binder );
    REQUIRE_FALSE( who.activity->is_null() );
    run_activity( who, to_turns<int>( pages * 10_minutes ) + 5 );
    REQUIRE( who.activity->is_null() );

    CHECK( binder.get_saved_recipes().count( recipe_water_clean ) == 1 );
    // DDA: the paper is used up from the inventory and the same number of sheets goes into the
    // binder's pocket
    CHECK( pen.ammo_remaining() == pen_charges - pages );
    int sheets_in_binder = 0;
    for( const item *it : binder.all_items_top( pocket_type::MAGAZINE ) ) {
        REQUIRE( it->typeId() == itype_paper );
        sheets_in_binder += it->charges;
    }
    CHECK( sheets_in_binder == pages );
    CHECK( who.charges_of( itype_paper ) == 4 );
}

TEST_CASE( "bookbinder_copy_survives_saving_and_loading", "[efile][actor][binder]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    const int pages = bookbinder_copy_activity_actor::pages_for_recipe( *recipe_water_clean );
    who.i_add( item::spawn( itype_paper, calendar::turn, pages ) );
    item &pen = who.i_add( item::spawn( itype_pen ) );
    const int pen_charges = pen.ammo_remaining();

    assign_binder_copy( who, binder );
    REQUIRE_FALSE( who.activity->is_null() );
    run_activity( who, 30 );
    REQUIRE_FALSE( who.activity->is_null() );
    const int moves_left = who.activity->get_moves_left();
    REQUIRE( moves_left < to_moves<int>( pages * 10_minutes ) );

    item &loaded_binder = *save_and_load( who, {&binder} ).front();
    REQUIRE_FALSE( who.activity->is_null() );
    REQUIRE( who.activity->id() == activity_id( "ACT_BINDER_COPY_RECIPE" ) );
    // the progress is saved with the actor, else a loaded activity would never finish
    CHECK( who.activity->get_moves_left() == moves_left );

    run_activity( who, to_turns<int>( pages * 10_minutes ) + 5 );
    REQUIRE( who.activity->is_null() );
    // rec_id, pages and the binder came back: the recipe is in the loaded binder
    CHECK( loaded_binder.get_saved_recipes().count( recipe_water_clean ) == 1 );
    CHECK( pen.ammo_remaining() == pen_charges - pages );
}

TEST_CASE( "ebooksave_statics_follow_the_dda_formulas", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );
    item &small = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    item &large = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );
    REQUIRE( item::pages_in_book( *small.type ) == 120 );
    REQUIRE( item::pages_in_book( *large.type ) == 240 );

    CHECK( ebooksave_activity_actor::time_per_page == 5_seconds );
    CHECK( ebooksave_activity_actor::pages_per_charge == 120 );
    CHECK( ebooksave_activity_actor::total_pages( {&small} ) == 120 );
    CHECK( ebooksave_activity_actor::total_pages( {&small, &large} ) == 360 );
    CHECK( ebooksave_activity_actor::required_time( {&small} ) == 600_seconds );
    CHECK( ebooksave_activity_actor::required_time( {&small, &large} ) == 1800_seconds );
    // DDA: pages / pages_per_charge, times the charges one use takes
    CHECK( ebooksave_activity_actor::required_charges( {&small}, &reader ) ==
           1 * reader.ammo_required() );
    CHECK( ebooksave_activity_actor::required_charges( {&small, &large}, &reader ) ==
           3 * reader.ammo_required() );
}

TEST_CASE( "ebooksave_scans_a_copy_and_keeps_the_book", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    safe_reference<item> book_ref( &book );

    assign_ebooksave( who, {&book}, reader );
    REQUIRE( who.activity->id() == activity_id( "ACT_EBOOKSAVE" ) );
    // 5 seconds a page
    CHECK( who.activity->get_moves_left() == to_moves<int>( 120 * 5_seconds ) );
    // nothing is stored until the book is fully scanned
    run_activity( who, 599 );
    CHECK_FALSE( who.activity->is_null() );
    CHECK( reader.efiles().empty() );

    run_activity( who, 10 );
    REQUIRE( who.activity->is_null() );
    CHECK( file_ids( reader ) == std::vector<itype_id> { itype_test_efile_scan_book_120 } );
    // the original stays, and is not the stored copy
    REQUIRE( static_cast<bool>( book_ref ) );
    CHECK( book_ref.get() == &book );
    CHECK( who.has_item( book ) );
    CHECK( reader.efiles()[0] != &book );
    // 120 pages are one battery use (DDA: one use every pages_per_charge * time_per_page)
    CHECK( reader.ammo_remaining() == 39 );
}

TEST_CASE( "ebooksave_takes_one_battery_use_per_120_pages", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );
    item &small = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    item &large = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );

    assign_ebooksave( who, {&small, &large}, reader );
    REQUIRE_FALSE( who.activity->is_null() );
    CHECK( who.activity->get_moves_left() == to_moves<int>( 360 * 5_seconds ) );
    run_activity( who, 1800 + 10 );
    REQUIRE( who.activity->is_null() );

    // both books are stored (DDA scans from the back of the list: the large one first)
    std::vector<itype_id> ids = file_ids( reader );
    std::sort( ids.begin(), ids.end() );
    CHECK( ids == std::vector<itype_id> { itype_test_efile_scan_book_120,
                                          itype_test_efile_scan_book_240
                                        } );
    CHECK( who.has_item( small ) );
    CHECK( who.has_item( large ) );
    // 1800 seconds cross three ten-minute boundaries
    CHECK( reader.ammo_remaining() == 37 );
}

TEST_CASE( "ebooksave_is_canceled_when_the_battery_runs_out", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 0 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );

    assign_ebooksave( who, {&book}, reader );
    REQUIRE_FALSE( who.activity->is_null() );
    run_activity( who, 1300 );
    CHECK( who.activity->is_null() );
    CHECK( reader.efiles().empty() );
    CHECK( who.has_item( book ) );
}

TEST_CASE( "ebooksave_stops_when_the_battery_empties_midway", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    // one use is enough for the first ten minutes; the second boundary finds the battery empty
    item &reader = battery_reader( who, 1 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );

    assign_ebooksave( who, {&book}, reader );
    REQUIRE_FALSE( who.activity->is_null() );
    run_activity( who, 1300 );
    CHECK( who.activity->is_null() );
    CHECK( reader.ammo_remaining() == 0 );
    // the book (20 minutes) was not finished
    CHECK( reader.efiles().empty() );
}

TEST_CASE( "ebooksave_skips_a_book_that_vanishes_and_goes_on", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );
    item &kept = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    item &doomed = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );

    // DDA scans from the back: the doomed book is scanned first
    assign_ebooksave( who, {&kept, &doomed}, reader );
    REQUIRE_FALSE( who.activity->is_null() );
    run_activity( who, 100 );
    REQUIRE_FALSE( who.activity->is_null() );
    {
        detached_ptr<item> gone = doomed.detach();
    }
    run_activity( who, 1800 + 10 );
    // DEVIATION from the plan text ("canceled when the book vanishes"): DDA skips the missing
    // book (activity_actor.cpp:3641-3660, 3697-3720) and finishes the others
    CHECK( who.activity->is_null() );
    CHECK( file_ids( reader ) == std::vector<itype_id> { itype_test_efile_scan_book_120 } );
    CHECK( who.has_item( kept ) );
}

TEST_CASE( "ebooksave_with_no_book_to_scan_ends_at_once", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );

    // DDA start_scanning_next_book: an empty list zeroes the time. The activity must finish and
    // not run forever (CDF activities complete only through their progress counter)
    assign_ebooksave( who, {}, reader );
    run_activity( who, 20 );
    CHECK( who.activity->is_null() );
    CHECK( reader.efiles().empty() );
    CHECK( reader.ammo_remaining() == 40 );
}

TEST_CASE( "ebooksave_is_canceled_when_the_reader_vanishes", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );

    assign_ebooksave( who, {&book}, reader );
    REQUIRE_FALSE( who.activity->is_null() );
    run_activity( who, 50 );
    REQUIRE_FALSE( who.activity->is_null() );
    {
        detached_ptr<item> gone = reader.detach();
    }
    run_activity( who, 1300 );
    // CDF guard (DDA's item_location would be invalid): no crash, the activity ends
    CHECK( who.activity->is_null() );
    CHECK( who.has_item( book ) );
}

// DDA parity: neither the actor (activity_actor.cpp:3691-3716) nor the menu
// (game_inventory.cpp:1666-1716) checks the free e-memory, so a book is stored even when the
// device is full; the free memory just goes negative.
TEST_CASE( "ebooksave_does_not_check_the_free_memory", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = who.i_add( item::spawn( itype_test_ereader_tiny ) );
    reader.ammo_set( itype_battery, 40 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );

    assign_ebooksave( who, {&book}, reader );
    REQUIRE_FALSE( who.activity->is_null() );
    run_activity( who, 700 );
    CHECK( who.activity->is_null() );
    // 120 pages are 2400 KB, the pocket holds 1 KB: the file is stored all the same
    CHECK( reader.efiles().size() == 1 );
    CHECK( reader.remaining_ememory() < 0_KB );
    CHECK( who.has_item( book ) );
}

TEST_CASE( "ebooksave_survives_saving_and_loading", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );
    item &other_reader = battery_reader( who, 40 );
    item &small = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    item &large = who.i_add( item::spawn( itype_test_efile_scan_book_240 ) );
    REQUIRE( &reader != &other_reader );

    assign_ebooksave( who, {&small, &large}, reader );
    REQUIRE_FALSE( who.activity->is_null() );
    // the large book (scanned first) is half done
    run_activity( who, 600 );
    REQUIRE_FALSE( who.activity->is_null() );
    const int moves_left = who.activity->get_moves_left();

    const std::vector<item *> loaded =
        save_and_load( who, {&reader, &other_reader, &small, &large} );
    item &loaded_reader = *loaded[0];
    item &loaded_other_reader = *loaded[1];
    REQUIRE_FALSE( who.activity->is_null() );
    REQUIRE( who.activity->id() == activity_id( "ACT_EBOOKSAVE" ) );
    CHECK( who.activity->get_moves_left() == moves_left );

    run_activity( who, 1500 );
    REQUIRE( who.activity->is_null() );
    // both files landed in the reader the actor was built with, none in the other one
    std::vector<itype_id> ids = file_ids( loaded_reader );
    std::sort( ids.begin(), ids.end() );
    CHECK( ids == std::vector<itype_id> { itype_test_efile_scan_book_120,
                                          itype_test_efile_scan_book_240
                                        } );
    CHECK( loaded_other_reader.efiles().empty() );
    // the three boundaries of the whole scan were paid by the right reader (the one at 600
    // seconds before the save, the other two after it)
    CHECK( loaded_reader.ammo_remaining() == 37 );
    CHECK( loaded_other_reader.ammo_remaining() == 40 );
}

// DDA avatar.cpp:810-850: a scanned book is identified for the avatar (avatar::identify, E1-5b).
// The avatar remembers identified types across test cases, so the case has a book of its own.
TEST_CASE( "ebooksave_identifies_the_scanned_book", "[efile][actor][ebooksave]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &reader = battery_reader( who, 40 );
    item &book = who.i_add( item::spawn( itype_test_efile_identify_book ) );
    REQUIRE_FALSE( who.has_identified( book.typeId() ) );

    assign_ebooksave( who, {&book}, reader );
    run_activity( who, 700 );
    REQUIRE( who.activity->is_null() );
    REQUIRE( reader.efiles().size() == 1 );
    CHECK( who.has_identified( book.typeId() ) );
}

// ---------------------------------------------------------------------------------------------
// E1-5b: efile_activity_actor (DDA activity_actor.cpp:3828-4440). The DDA `edevice` case is in
// tests/player_activities_test.cpp; these are the CDF-new cases. Each one carries its own set-up.
// ---------------------------------------------------------------------------------------------

namespace
{

auto charged_device( avatar &who, const itype_id &type, int charges ) -> item &
{
    item &device = who.i_add( item::spawn( type ) );
    device.ammo_set( itype_battery, charges );
    REQUIRE( device.ammo_remaining() == charges );
    return device;
}

// stores a file in the device's e-file pocket (no memory check, like the actors) and returns it
// (the file list has no defined order: the new file is the one that was not there before)
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

auto assign_efile( avatar &who, item *used, const std::vector<item *> &targets,
                   const std::vector<item *> &files, efile_action action,
                   efile_combo combo = efile_combo::COMBO_NONE ) -> void
{
    auto actor = std::make_unique<efile_activity_actor>( used, targets, files, action, combo );
    who.assign_activity( std::make_unique<player_activity>( std::move( actor ) ) );
}

} // namespace

// The efile actor keeps lists of references to files that live INSIDE a device's e-file pocket,
// not to top-level items. The reference round trip of the other cases only proves top-level items.
TEST_CASE( "efile_actor_reference_to_a_stored_file_round_trips", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    item &laptop = charged_device( who, itype_test_laptop, 10 );
    item &file = store_file( laptop, itype_test_efile_small );
    std::string refs_text;
    std::string laptop_text;
    {
        std::vector<safe_reference<item>> refs;
        refs.emplace_back( &file );
        std::ostringstream out;
        JsonOut jsout( out );
        jsout.start_object();
        jsout.member( "refs", refs );
        jsout.end_object();
        refs_text = out.str();
        laptop_text = json_of( laptop );
        refs.clear();
        detached_ptr<item> gone = laptop.detach();
    }
    item &loaded_laptop = load_item( who, laptop_text );

    std::vector<safe_reference<item>> loaded_refs;
    std::istringstream in( refs_text );
    JsonIn jsin( in );
    JsonObject data = jsin.get_object();
    data.read( "refs", loaded_refs );
    REQUIRE( loaded_laptop.efiles().size() == 1 );
    REQUIRE( loaded_refs.size() == 1 );
    REQUIRE( static_cast<bool>( loaded_refs[0] ) );
    CHECK( loaded_refs[0].get() == loaded_laptop.efiles()[0] );
    CHECK( loaded_refs[0]->parent_item() == &loaded_laptop );
}

TEST_CASE( "efile_actor_statics_follow_the_dda_tables", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );

    SECTION( "names and predicates" ) {
        CHECK( efile_activity_actor::efile_action_name( EF_BROWSE ) == "browse" );
        CHECK( efile_activity_actor::efile_action_name( EF_BROWSE, true ) == "browsed" );
        CHECK( efile_activity_actor::efile_action_name( EF_MOVE_FROM_THIS, false, true ) ==
               "move files onto" );
        CHECK( efile_activity_actor::efile_action_name( EF_MOVE_ONTO_THIS, true, true ) ==
               "moved files off of" );
        CHECK( efile_activity_actor::efile_action_name( EF_COPY_ONTO_THIS, false, true ) ==
               "copy files off of" );
        CHECK( efile_activity_actor::efile_action_name( EF_WIPE, true, true ) == "wiped" );
        CHECK_FALSE( efile_activity_actor::efile_action_exclude_used( EF_BROWSE ) );
        CHECK_FALSE( efile_activity_actor::efile_action_exclude_used( EF_WIPE ) );
        CHECK( efile_activity_actor::efile_action_exclude_used( EF_COPY_FROM_THIS ) );
        CHECK( efile_activity_actor::efile_action_exclude_used( EF_MOVE_ONTO_THIS ) );
        CHECK( efile_activity_actor::efile_action_is_from( EF_MOVE_FROM_THIS ) );
        CHECK( efile_activity_actor::efile_action_is_from( EF_COPY_FROM_THIS ) );
        CHECK_FALSE( efile_activity_actor::efile_action_is_from( EF_MOVE_ONTO_THIS ) );
        CHECK_FALSE( efile_activity_actor::efile_action_is_from( EF_WIPE ) );
    }

    SECTION( "the device can process files only with the E_FILE_DEVICE use action" ) {
        item laptop( itype_test_laptop );
        item card( itype_test_efile_etd );
        item rock( itype_rock );
        CHECK( efile_activity_actor::edevice_has_use( &laptop ) );
        CHECK_FALSE( efile_activity_actor::edevice_has_use( &card ) );
        CHECK_FALSE( efile_activity_actor::edevice_has_use( &rock ) );
    }

    SECTION( "processing times" ) {
        item &laptop = charged_device( who, itype_test_laptop, 10 );
        item &tablet = charged_device( who, itype_test_eink_tablet_pc, 10 );
        item &medium = store_file( laptop, itype_test_efile_medium );
        item &small = store_file( laptop, itype_test_efile_small );
        const efile_transfer transfer( &laptop, &tablet );
        const auto time_of = [&]( item & file, efile_action action, bool low_skill ) {
            return efile_activity_actor::efile_processing_time( &file, transfer, action, who,
                    low_skill );
        };
        // browse: a flat 15 seconds a file, three times as long for a clumsy user
        CHECK( time_of( medium, EF_BROWSE, false ) == 15_seconds );
        CHECK( time_of( medium, EF_BROWSE, true ) == 45_seconds );
        // wipe: 1 second plus the size over the local rate (24 GB / 200 MB = 120)
        CHECK( time_of( medium, EF_WIPE, false ) == 121_seconds );
        CHECK( time_of( small, EF_WIPE, false ) == 1_seconds );
        // transfer between two 30 MB/s devices: 24 GB / 30 MB = 800
        CHECK( time_of( medium, EF_COPY_FROM_THIS, false ) == 801_seconds );
        CHECK( time_of( medium, EF_COPY_FROM_THIS, true ) == 2403_seconds );
        CHECK( time_of( small, EF_MOVE_FROM_THIS, false ) == 1_seconds );
        // the whole activity: one boot a device plus every file on every device
        CHECK( efile_activity_actor::total_processing_time( &laptop, {&tablet}, {&medium, &small},
                EF_COPY_FROM_THIS, who, false ) == 10_seconds + 801_seconds + 1_seconds );
        CHECK( efile_activity_actor::total_processing_time( &laptop, {&tablet, &laptop},
                {&small}, EF_BROWSE, who, false ) == 2 * 10_seconds + 2 * 15_seconds );
    }

    SECTION( "a file both devices already hold is not copied again" ) {
        item &laptop = charged_device( who, itype_test_laptop, 10 );
        item &tablet = charged_device( who, itype_test_eink_tablet_pc, 10 );
        item &copiable = store_file( laptop, itype_test_efile_copiable );
        item &plain = store_file( laptop, itype_test_efile_small );
        const efile_transfer transfer( &laptop, &tablet );
        CHECK_FALSE( efile_activity_actor::efile_skip_copy( transfer, copiable ) );
        store_file( tablet, itype_test_efile_copiable );
        store_file( tablet, itype_test_efile_small );
        CHECK( efile_activity_actor::efile_skip_copy( transfer, copiable ) );
        // only copiable files are skipped
        CHECK_FALSE( efile_activity_actor::efile_skip_copy( transfer, plain ) );
        // a skipped file takes one second
        CHECK( efile_activity_actor::efile_processing_time( &copiable, transfer, EF_COPY_FROM_THIS,
                who, false ) == 1_seconds );
    }

    SECTION( "files are filtered by the device they are on" ) {
        item &laptop = charged_device( who, itype_test_laptop, 10 );
        item &tablet = charged_device( who, itype_test_eink_tablet_pc, 10 );
        item &on_laptop = store_file( laptop, itype_test_efile_small );
        item &on_tablet = store_file( tablet, itype_test_efile_medium );
        std::vector<item *> files = {&on_laptop, &on_tablet};
        const std::vector<item *> filtered = efile_activity_actor::filter_edevice_efiles( &laptop,
                                             files );
        CHECK( filtered == std::vector<item *> {&on_laptop} );
        CHECK( files == std::vector<item *> {&on_tablet} );
    }
}

TEST_CASE( "efile_actor_start_adds_boot_and_file_times", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    store_file( laptop, itype_test_efile_small, false );
    store_file( laptop, itype_test_efile_copiable, false );
    store_file( laptop, itype_efile_junk, false );

    SECTION( "browse" ) {
        assign_efile( who, &laptop, {&laptop}, laptop.efiles(), EF_BROWSE );
        REQUIRE( who.activity->id() == activity_id( "ACT_E_FILE" ) );
        // 10 seconds boot + 15 seconds a file
        CHECK( who.activity->get_moves_left() == to_moves<int>( 10_seconds + 3 * 15_seconds ) );
    }
    SECTION( "a user without computer skill needs three times as long for the files" ) {
        who.set_skill_level( skill_computer, 0 );
        assign_efile( who, &laptop, {&laptop}, laptop.efiles(), EF_BROWSE );
        REQUIRE( who.activity->id() == activity_id( "ACT_E_FILE" ) );
        CHECK( who.activity->get_moves_left() == to_moves<int>( 10_seconds + 3 * 45_seconds ) );
    }
    SECTION( "no devices: nothing to do and the activity ends at once" ) {
        assign_efile( who, &laptop, {}, laptop.efiles(), EF_BROWSE );
        CHECK( who.activity->get_moves_left() == 0 );
        run_activity( who, 3 );
        CHECK( who.activity->is_null() );
    }
}

TEST_CASE( "efile_actor_browse_removes_junk_and_generates_recipes", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    item &small = store_file( laptop, itype_test_efile_small, false );
    store_file( laptop, itype_efile_junk, false );
    item &catalog = store_file( laptop, itype_efile_recipes, false );
    REQUIRE( catalog.get_saved_recipes().empty() );
    REQUIRE_FALSE( laptop.is_browsed() );

    assign_efile( who, &laptop, {&laptop}, laptop.efiles(), EF_BROWSE );
    run_activity( who, 200 );
    REQUIRE( who.activity->is_null() );

    CHECK( laptop.is_browsed() );
    // the junk file is deleted by browsing it, the others are marked browsed
    const std::vector<itype_id> ids = file_ids( laptop );
    CHECK( ids.size() == 2 );
    CHECK( std::count( ids.begin(), ids.end(), itype_efile_junk ) == 0 );
    CHECK( small.is_browsed() );
    // the catalog got its recipes generated when it was browsed
    CHECK( catalog.is_browsed() );
    CHECK_FALSE( catalog.get_saved_recipes().empty() );
}

TEST_CASE( "efile_actor_combo_browses_then_moves_the_files_onto_the_used_device",
           "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    store_file( laptop, itype_test_efile_small, false );
    store_file( laptop, itype_test_efile_copiable, false );

    SECTION( "the files fit onto the used device" ) {
        item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
        assign_efile( who, &tablet, {&laptop}, laptop.efiles(), EF_BROWSE,
                      efile_combo::COMBO_MOVE_ONTO_BROWSE );
        run_activity( who, 400 );
        REQUIRE( who.activity->is_null() );
        CHECK( laptop.is_browsed() );
        CHECK( laptop.efiles().empty() );
        REQUIRE( tablet.efiles().size() == 2 );
        for( const item *file : tablet.efiles() ) {
            CHECK( file->is_browsed() );
        }
    }
    SECTION( "the files do not fit: the move is canceled and the files stay" ) {
        // the tiny reader holds 1 KB, the copiable file is 1 MB
        item &tiny = charged_device( who, itype_test_ereader_tiny, 50 );
        assign_efile( who, &tiny, {&laptop}, laptop.efiles(), EF_BROWSE,
                      efile_combo::COMBO_MOVE_ONTO_BROWSE );
        run_activity( who, 400 );
        REQUIRE( who.activity->is_null() );
        CHECK( laptop.is_browsed() );
        CHECK( laptop.efiles().size() == 2 );
        CHECK( tiny.efiles().empty() );
    }
}

TEST_CASE( "efile_actor_runs_out_of_battery_mid_transfer", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    item &medium = store_file( laptop, itype_test_efile_medium );
    // the 800 second transfer needs three battery uses (one every four minutes), the tablet has two
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 2 );

    SECTION( "the used device" ) {
        assign_efile( who, &tablet, {&laptop}, {&medium}, EF_COPY_ONTO_THIS );
        run_activity( who, 1000 );
        REQUIRE( who.activity->is_null() );
        CHECK( tablet.ammo_remaining() == 0 );
        CHECK( tablet.efiles().empty() );
        CHECK( laptop.efiles().size() == 1 );
    }
    SECTION( "a target device" ) {
        assign_efile( who, &laptop, {&tablet}, {&medium}, EF_COPY_FROM_THIS );
        run_activity( who, 1000 );
        REQUIRE( who.activity->is_null() );
        CHECK( tablet.ammo_remaining() == 0 );
        CHECK( tablet.efiles().empty() );
        // the used device pays its uses all the same
        CHECK( laptop.ammo_remaining() < 50 );
    }
}

TEST_CASE( "efile_actor_device_out_of_reach_fails_the_activity", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    store_file( laptop, itype_test_efile_small );
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    const tripoint_bub_ms here = who.bub_pos();
    // a dropped device keeps a valid safe_reference: the actor has to ask where the device is
    const auto drop_at = [&]( item & device, int distance ) {
        detached_ptr<item> dropped = device.detach();
        get_map().add_item( tripoint_bub_ms( here.x() + distance, here.y(), here.z() ),
                            std::move( dropped ) );
    };

    SECTION( "a target device dropped beyond reach before the start" ) {
        drop_at( tablet, 10 );
        assign_efile( who, &laptop, {&tablet}, laptop.efiles(), EF_COPY_FROM_THIS );
        run_activity( who, 100 );
        REQUIRE( who.activity->is_null() );
        CHECK( tablet.efiles().empty() );
    }
    SECTION( "a target device carried away mid-activity" ) {
        assign_efile( who, &laptop, {&tablet}, laptop.efiles(), EF_COPY_FROM_THIS );
        run_activity( who, 5 );
        REQUIRE_FALSE( who.activity->is_null() );
        drop_at( tablet, 10 );
        run_activity( who, 100 );
        REQUIRE( who.activity->is_null() );
        CHECK( tablet.efiles().empty() );
    }
    SECTION( "the used device dropped beyond reach" ) {
        assign_efile( who, &laptop, {&tablet}, laptop.efiles(), EF_COPY_FROM_THIS );
        run_activity( who, 5 );
        REQUIRE_FALSE( who.activity->is_null() );
        drop_at( laptop, 10 );
        run_activity( who, 100 );
        REQUIRE( who.activity->is_null() );
        CHECK( tablet.efiles().empty() );
    }
    // DEVIATION from DDA (an item_location goes invalid when its item is dropped): a device that
    // lies within PICKUP_RANGE on the map can still be used, as the menus offer such devices.
    SECTION( "a target device dropped within reach keeps working" ) {
        drop_at( tablet, 1 );
        assign_efile( who, &laptop, {&tablet}, laptop.efiles(), EF_COPY_FROM_THIS );
        run_activity( who, 100 );
        REQUIRE( who.activity->is_null() );
        CHECK( tablet.efiles().size() == 1 );
    }
}

TEST_CASE( "efile_actor_skips_a_file_that_vanishes_and_goes_on", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    item &small = store_file( laptop, itype_test_efile_small );
    item &medium = store_file( laptop, itype_test_efile_medium );
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );

    SECTION( "mid-activity" ) {
        // the files are processed from the back of the list: the medium one (800 seconds) first
        assign_efile( who, &laptop, {&tablet}, {&small, &medium}, EF_COPY_FROM_THIS );
        run_activity( who, 60 );
        REQUIRE_FALSE( who.activity->is_null() );
        {
            detached_ptr<item> gone = small.detach();
        }
        run_activity( who, 2000 );
        REQUIRE( who.activity->is_null() );
        CHECK( file_ids( tablet ) == std::vector<itype_id> {itype_test_efile_medium} );
    }
    SECTION( "before the start" ) {
        {
            detached_ptr<item> gone = small.detach();
        }
        assign_efile( who, &laptop, {&tablet}, {&small, &medium}, EF_COPY_FROM_THIS );
        run_activity( who, 2000 );
        REQUIRE( who.activity->is_null() );
        CHECK( file_ids( tablet ) == std::vector<itype_id> {itype_test_efile_medium} );
    }
    SECTION( "a file moved to another device by someone else is not processed here" ) {
        // the actor works on the file it was given only while it is where the action expects it
        item &other = charged_device( who, itype_test_eink_tablet_pc, 50 );
        assign_efile( who, &laptop, {&tablet}, {&medium}, EF_MOVE_FROM_THIS );
        run_activity( who, 30 );
        REQUIRE_FALSE( who.activity->is_null() );
        detached_ptr<item> detached = medium.detach();
        REQUIRE( other.put_in( std::move( detached ), pocket_type::E_FILE_STORAGE, false, &who,
                               true ).success() );
        run_activity( who, 2000 );
        REQUIRE( who.activity->is_null() );
        CHECK( tablet.efiles().empty() );
        CHECK( other.efiles().size() == 1 );
    }
}

TEST_CASE( "efile_actor_survives_saving_and_loading", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    store_file( laptop, itype_test_efile_small );
    store_file( laptop, itype_test_efile_medium );
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    item &other_tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );

    // the transfer of the 24 GB file is a third done at the save
    assign_efile( who, &laptop, {&tablet}, laptop.efiles(), EF_COPY_FROM_THIS );
    run_activity( who, 280 );
    REQUIRE_FALSE( who.activity->is_null() );
    const int moves_left = who.activity->get_moves_left();

    const std::vector<item *> loaded = save_and_load( who, {&laptop, &tablet, &other_tablet} );
    item &loaded_laptop = *loaded[0];
    item &loaded_tablet = *loaded[1];
    item &loaded_other_tablet = *loaded[2];
    REQUIRE_FALSE( who.activity->is_null() );
    REQUIRE( who.activity->id() == activity_id( "ACT_E_FILE" ) );
    CHECK( who.activity->get_moves_left() == moves_left );

    run_activity( who, 2000 );
    REQUIRE( who.activity->is_null() );
    // both files were copied onto the target the actor was built with, none onto the other one,
    // and the originals stay
    std::vector<itype_id> ids = file_ids( loaded_tablet );
    std::sort( ids.begin(), ids.end() );
    std::vector<itype_id> expected = {itype_test_efile_medium, itype_test_efile_small};
    std::sort( expected.begin(), expected.end() );
    CHECK( ids == expected );
    CHECK( loaded_other_tablet.efiles().empty() );
    CHECK( loaded_laptop.efiles().size() == 2 );
}

TEST_CASE( "efile_actor_serializes_its_own_state", "[efile][actor][edevice]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    store_file( laptop, itype_test_efile_small );
    item &tablet = charged_device( who, itype_test_eink_tablet_pc, 50 );
    assign_efile( who, &laptop, {&tablet}, laptop.efiles(), EF_MOVE_FROM_THIS );
    run_activity( who, 5 );
    REQUIRE_FALSE( who.activity->is_null() );

    const std::string text = json_of( *who.activity );
    for( const char *member : {"used_edevice", "target_edevices", "target_edevices_copy",
                               "action_type", "combo_type", "selected_efiles",
                               "currently_processed_efiles", "target_edevices_count",
                               "processed_edevices", "failed_edevices", "processed_efiles",
                               "failed_efiles", "started_processing", "done_processing",
                               "next_edevice_booted", "computer_low_skill",
                               "turns_left_on_current_edevice", "turns_left_on_current_efile",
                               "progress"} ) {
        INFO( member );
        CHECK( text.find( std::string( "\"" ) + member + "\"" ) != std::string::npos );
    }
}

// ---------------------------------------------------------------------------------------------
// E1-5c: port compatibility, the transfer rate, the external transfer device and the recipe
// catalog / photo gallery merge (DDA activity_actor.cpp:4036-4060, 4156-4217, 4360-4399). The DDA
// edevice cases that cover the rate and the catalog merge are in tests/player_activities_test.cpp;
// these are the CDF-new cases for the pieces the DDA test does not reach.
// ---------------------------------------------------------------------------------------------

namespace
{

auto rate_of( avatar &who, item &used, item &target, item &file ) -> units::ememory
{
    const efile_transfer transfer( &used, &target );
    return efile_activity_actor::current_etransfer_rate( who, transfer, &file );
}

// a browsed external transfer device, carried by @p who
auto carried_etd( avatar &who, const itype_id &type ) -> item &
{
    item &etd = who.i_add( item::spawn( type ) );
    etd.set_browsed( true );
    return etd;
}

auto some_recipes( size_t count ) -> std::vector<recipe_id>
{
    std::vector<recipe_id> found;
    for( const auto &entry : recipe_dict ) {
        if( !entry.second.obsolete && !entry.second.never_learn ) {
            found.push_back( entry.first );
            if( found.size() == count ) {
                break;
            }
        }
    }
    REQUIRE( found.size() == count );
    return found;
}

auto store_catalog( item &device, const std::set<recipe_id> &recipes ) -> item &
{
    item &catalog = store_file( device, itype_efile_recipes );
    catalog.set_saved_recipes( recipes );
    REQUIRE( catalog.get_saved_recipes() == recipes );
    return catalog;
}

auto store_gallery( item &device, const std::vector<std::string> &names ) -> item &
{
    item &gallery = store_file( device, itype_efile_photos );
    std::vector<item::extended_photo_def> photos;
    for( const std::string &name : names ) {
        item::extended_photo_def photo;
        photo.quality = 2;
        photo.name = name;
        photo.description = "a photo of " + name;
        photos.push_back( photo );
    }
    gallery.write_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS" );
    return gallery;
}

auto photo_names( const item &gallery ) -> std::vector<std::string>
{
    std::vector<item::extended_photo_def> photos;
    gallery.read_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS", false );
    std::vector<std::string> names;
    for( const item::extended_photo_def &photo : photos ) {
        names.push_back( photo.name );
    }
    return names;
}

auto count_files_of( const item &device, const itype_id &type ) -> int
{
    int count = 0;
    for( const item *file : device.efiles() ) {
        if( file->typeId() == type ) {
            count++;
        }
    }
    return count;
}

} // namespace

// DDA activity_actor.cpp:4187-4217: no port class when either is not a tool; the same e_port is the
// slow (Bluetooth) class; different ports are fast unless one device bans the other's port.
TEST_CASE( "efile_actor_port_classes_follow_the_dda_table", "[efile][actor][ports]" )
{
    start_clean_day();
    item laptop( itype_test_laptop );
    item other_laptop( itype_test_laptop );
    item tablet( itype_test_eink_tablet_pc );
    item reader( itype_test_ebook_reader );
    item phone( itype_test_smart_phone );
    item usb( itype_test_efile_usb_a );
    item rock( itype_rock );
    using compat = efile_activity_actor;

    // different ports, nobody bans anybody: fast
    CHECK( compat::edevices_compatible( laptop, tablet ) == compat::ECOMPAT_FAST );
    CHECK( compat::edevices_compatible( tablet, laptop ) == compat::ECOMPAT_FAST );
    CHECK( compat::edevices_compatible( laptop, usb ) == compat::ECOMPAT_FAST );
    CHECK( compat::edevices_compatible( laptop, phone ) == compat::ECOMPAT_FAST );
    CHECK( compat::edevices_compatible( tablet, phone ) == compat::ECOMPAT_FAST );
    // the same port: slow, also for two copies of one device
    CHECK( compat::edevices_compatible( laptop, other_laptop ) == compat::ECOMPAT_SLOW );
    CHECK( compat::edevices_compatible( tablet, reader ) == compat::ECOMPAT_SLOW );
    // the tablets and the phone ban USB-A, in both directions
    CHECK( compat::edevices_compatible( tablet, usb ) == compat::ECOMPAT_NONE );
    CHECK( compat::edevices_compatible( usb, tablet ) == compat::ECOMPAT_NONE );
    CHECK( compat::edevices_compatible( phone, usb ) == compat::ECOMPAT_NONE );
    CHECK( compat::edevices_compatible( usb, reader ) == compat::ECOMPAT_NONE );
    // a non-tool has no port at all
    CHECK( compat::edevices_compatible( rock, laptop ) == compat::ECOMPAT_NONE );
    CHECK( compat::edevices_compatible( laptop, rock ) == compat::ECOMPAT_NONE );
    CHECK( compat::edevices_compatible( rock, rock ) == compat::ECOMPAT_NONE );
    // the pointer form is the same table (and a vanished device is no device)
    CHECK( compat::edevices_compatible( &laptop, &tablet ) == compat::ECOMPAT_FAST );
    CHECK( compat::edevices_compatible( &laptop, &other_laptop ) == compat::ECOMPAT_SLOW );
    CHECK( compat::edevices_compatible( &tablet, &usb ) == compat::ECOMPAT_NONE );
    CHECK( compat::edevices_compatible( &laptop, nullptr ) == compat::ECOMPAT_NONE );
    CHECK( compat::edevices_compatible( nullptr, &laptop ) == compat::ECOMPAT_NONE );
}

// DDA activity_actor.cpp:4156-4180 (the external transfer device) and :4360-4399 (the rate).
TEST_CASE( "efile_actor_transfer_rate_and_the_external_transfer_device", "[efile][actor][ports]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    item &other_laptop = charged_device( who, itype_test_laptop, 50 );
    item &medium = store_file( laptop, itype_test_efile_medium );
    item &small = store_file( laptop, itype_test_efile_small );

    SECTION( "a fast pair runs at the slower device and never looks for an external device" ) {
        item &usb = who.i_add( item::spawn( itype_test_efile_usb_a ) );
        carried_etd( who, itype_test_efile_etd );
        CHECK( rate_of( who, laptop, usb, medium ) == 5_MB );
        CHECK( efile_activity_actor::find_external_transfer_estorage( who, &medium, &laptop,
                &usb ) == nullptr );
    }
    SECTION( "a slow pair without an external device runs at the Bluetooth rate" ) {
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 1_MB );
        CHECK( efile_activity_actor::find_external_transfer_estorage( who, &medium, &laptop,
                &other_laptop ) == nullptr );
    }
    SECTION( "a slow pair uses half the rate of a browsed external device" ) {
        item &etd = carried_etd( who, itype_test_efile_etd );
        CHECK( efile_activity_actor::find_external_transfer_estorage( who, &medium, &laptop,
                &other_laptop ) == &etd );
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 6_MB );
        // the rate is part of the time: 24 GB / 6 MB, plus the second every transfer takes
        const efile_transfer transfer( &laptop, &other_laptop );
        CHECK( efile_activity_actor::efile_processing_time( &medium, transfer, EF_COPY_FROM_THIS,
                who, false ) == 4001_seconds );
    }
    SECTION( "the Bluetooth rate is the floor" ) {
        carried_etd( who, itype_test_efile_etd_slow );
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 1_MB );
    }
    SECTION( "an external device that is not browsed is ignored" ) {
        item &etd = who.i_add( item::spawn( itype_test_efile_etd ) );
        REQUIRE_FALSE( etd.is_browsed() );
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 1_MB );
    }
    SECTION( "an external device needs room for the file" ) {
        item &tiny = carried_etd( who, itype_test_efile_etd_tiny );
        // 24 GB do not fit on 1 MB; a 1 KB file does
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 1_MB );
        CHECK( rate_of( who, laptop, other_laptop, small ) == 6_MB );
        CHECK( efile_activity_actor::find_external_transfer_estorage( who, &small, &laptop,
                &other_laptop ) == &tiny );
    }
    SECTION( "an e-device with the use action is not an external device" ) {
        item &third = charged_device( who, itype_test_laptop, 50 );
        third.set_browsed( true );
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 1_MB );
    }
    SECTION( "an external device lying within PICKUP_RANGE counts, one beyond does not" ) {
        const tripoint_bub_ms here = who.bub_pos();
        const auto lay_etd_at = [&]( int distance ) {
            detached_ptr<item> etd = item::spawn( itype_test_efile_etd );
            etd->set_browsed( true );
            get_map().add_item( tripoint_bub_ms( here.x() + distance, here.y(), here.z() ),
                                std::move( etd ) );
        };
        lay_etd_at( PICKUP_RANGE + 1 );
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 1_MB );
        lay_etd_at( PICKUP_RANGE );
        CHECK( rate_of( who, laptop, other_laptop, medium ) == 6_MB );
    }
}

// DDA activity_actor.cpp:4036-4045: a recipe catalog that is moved or copied is merged into the
// catalog the target already has, instead of being stored as a second file.
TEST_CASE( "efile_actor_merges_recipe_catalogs", "[efile][actor][merge]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    const std::vector<recipe_id> recipes = some_recipes( 3 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    item &phone = charged_device( who, itype_test_smart_phone, 50 );
    const std::set<recipe_id> source_recipes = {recipes[0], recipes[1]};
    const std::set<recipe_id> all_recipes = {recipes[0], recipes[1], recipes[2]};
    store_catalog( laptop, source_recipes );

    SECTION( "a move merges into the target's catalog and removes the source file" ) {
        store_catalog( phone, {recipes[1], recipes[2]} );
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_MOVE_FROM_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( count_files_of( phone, itype_efile_recipes ) == 1 );
        CHECK( phone.efiles().size() == 1 );
        CHECK( phone.get_saved_recipes() == all_recipes );
        CHECK( laptop.efiles().empty() );
    }
    SECTION( "a copy merges too and the source keeps its own catalog" ) {
        store_catalog( phone, {recipes[1], recipes[2]} );
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_COPY_FROM_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( phone.efiles().size() == 1 );
        CHECK( phone.get_saved_recipes() == all_recipes );
        CHECK( laptop.efiles().size() == 1 );
        CHECK( laptop.get_saved_recipes() == source_recipes );
    }
    SECTION( "moving onto the used device merges into the used device's catalog" ) {
        store_catalog( phone, {recipes[2]} );
        assign_efile( who, &phone, {&laptop}, laptop.efiles(), EF_MOVE_ONTO_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( phone.efiles().size() == 1 );
        CHECK( phone.get_saved_recipes() == all_recipes );
        CHECK( laptop.efiles().empty() );
    }
    SECTION( "a target without a catalog just receives the file" ) {
        REQUIRE( phone.efiles().empty() );
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_MOVE_FROM_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( count_files_of( phone, itype_efile_recipes ) == 1 );
        CHECK( phone.get_saved_recipes() == source_recipes );
        CHECK( laptop.efiles().empty() );
    }
    SECTION( "a copy onto a target without a catalog stores a second catalog" ) {
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_COPY_FROM_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( count_files_of( phone, itype_efile_recipes ) == 1 );
        CHECK( phone.get_saved_recipes() == source_recipes );
        CHECK( count_files_of( laptop, itype_efile_recipes ) == 1 );
    }
}

// DDA activity_actor.cpp:4046-4059: the same for a photo gallery ("CAMERA_EXTENDED_PHOTOS"); the
// photos of the arriving gallery go to the front of the target's list.
TEST_CASE( "efile_actor_merges_photo_galleries", "[efile][actor][merge]" )
{
    start_clean_day();
    avatar &who = get_avatar();
    clear_avatar();
    who.set_skill_level( skill_computer, 1 );
    item &laptop = charged_device( who, itype_test_laptop, 50 );
    item &phone = charged_device( who, itype_test_smart_phone, 50 );
    store_gallery( laptop, {"cat", "dog"} );
    const std::vector<std::string> arriving = {"cat", "dog"};
    const std::vector<std::string> both = {"cat", "dog", "fox", "owl", "bat"};

    SECTION( "a move keeps the photos of both galleries in one gallery" ) {
        store_gallery( phone, {"fox", "owl", "bat"} );
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_MOVE_FROM_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( count_files_of( phone, itype_efile_photos ) == 1 );
        REQUIRE( phone.get_photo_gallery() != nullptr );
        CHECK( photo_names( *phone.get_photo_gallery() ) == both );
        CHECK( phone.get_photo_gallery()->total_photos() == 5 );
        CHECK( laptop.efiles().empty() );
    }
    SECTION( "a copy merges as well and the source keeps its photos" ) {
        store_gallery( phone, {"fox", "owl", "bat"} );
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_COPY_FROM_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( count_files_of( phone, itype_efile_photos ) == 1 );
        REQUIRE( phone.get_photo_gallery() != nullptr );
        CHECK( photo_names( *phone.get_photo_gallery() ) == both );
        REQUIRE( laptop.get_photo_gallery() != nullptr );
        CHECK( photo_names( *laptop.get_photo_gallery() ) == arriving );
    }
    SECTION( "a target without a gallery receives the gallery as it is" ) {
        REQUIRE( phone.get_photo_gallery() == nullptr );
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_MOVE_FROM_THIS );
        run_activity( who, 300 );
        REQUIRE( who.activity->is_null() );
        CHECK( count_files_of( phone, itype_efile_photos ) == 1 );
        REQUIRE( phone.get_photo_gallery() != nullptr );
        CHECK( photo_names( *phone.get_photo_gallery() ) == arriving );
        CHECK( laptop.efiles().empty() );
    }
    SECTION( "a recipe catalog and a gallery in one move are merged each into its own kind" ) {
        const std::vector<recipe_id> recipes = some_recipes( 2 );
        store_catalog( laptop, {recipes[0]} );
        store_gallery( phone, {"fox"} );
        store_catalog( phone, {recipes[1]} );
        assign_efile( who, &laptop, {&phone}, laptop.efiles(), EF_MOVE_FROM_THIS );
        run_activity( who, 400 );
        REQUIRE( who.activity->is_null() );
        CHECK( phone.efiles().size() == 2 );
        CHECK( phone.get_saved_recipes() == std::set<recipe_id> {recipes[0], recipes[1]} );
        REQUIRE( phone.get_photo_gallery() != nullptr );
        CHECK( photo_names( *phone.get_photo_gallery() ) ==
               std::vector<std::string> {"cat", "dog", "fox"} );
        CHECK( laptop.efiles().empty() );
    }
}
