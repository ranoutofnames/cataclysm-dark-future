#include "catch/catch.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "clzones.h"
#include "coordinates.h"
#include "faction.h"
#include "filesystem.h"
#include "game.h"
#include "game_constants.h"
#include "state_helpers.h"
#include "translations.h"
#include "type_id.h"
#include "world.h"

// ZM1: the zone manager window seams. The window itself is checked by hand
// (see the packet's in-game checklist); these cases cover the pure statics that
// the drawing and input code calls.
//
// The fixture zone types come from data/mods/TEST_DATA/zone_ui_p3.json.

namespace
{

const zone_type_id test_visible_zone( "TEST_VISIBLE_ZONE" );
const zone_type_id test_hidden_zone( "TEST_HIDDEN_ZONE" );

auto reset_zone_state() -> void
{
    clear_all_state();
    zone_manager::reset_manager();
}

auto add_zone( zone_manager &mgr, const std::string &name, const zone_type_id &type,
               const tripoint_abs_ms &center ) -> void
{
    mgr.add( name, type, your_fac, false, true, center, center );
}

} // namespace

TEST_CASE( "zm1 ZM-T1 hidden zone types are listed only in debug", "[zones][ui]" )
{
    reset_zone_state();
    REQUIRE( test_visible_zone.is_valid() );
    REQUIRE( test_hidden_zone.is_valid() );
    REQUIRE( test_hidden_zone->hidden );

    auto &mgr = zone_manager::get_manager();
    const auto from = get_avatar().abs_pos();
    add_zone( mgr, "hidden zone", test_hidden_zone, from );

    // a hidden type is dropped while the manager is not in debug mode
    CHECK( zone_manager_ui::visible_zones( mgr, your_fac, true, from, false ).empty() );

    const auto debug_view = zone_manager_ui::visible_zones( mgr, your_fac, true, from, true );
    REQUIRE( debug_view.size() == 1 );
    CHECK( debug_view.front().get().get_type() == test_hidden_zone );
}

TEST_CASE( "zm1 ZM-T2 distance rule", "[zones][ui]" )
{
    reset_zone_state();
    REQUIRE( test_visible_zone.is_valid() );

    auto &mgr = zone_manager::get_manager();
    const auto from = get_avatar().abs_pos();
    add_zone( mgr, "near zone", test_visible_zone, from + tripoint_rel_ms( 5, 0, 0 ) );
    // CDF: MAX_VIEW_DISTANCE is SEEX * HALF_MAPSIZE = 204 here, not DDA's 60,
    // so the far zone sits just beyond CDF's limit.
    add_zone( mgr, "edge zone", test_visible_zone,
              from + tripoint_rel_ms( MAX_VIEW_DISTANCE, 0, 0 ) );
    add_zone( mgr, "far zone", test_visible_zone,
              from + tripoint_rel_ms( MAX_VIEW_DISTANCE + 1, 0, 0 ) );
    // the rule is 3-D: a zone 10 tiles away on another z-level is still listed
    add_zone( mgr, "other z zone", test_visible_zone, from + tripoint_rel_ms( 10, 0, 1 ) );

    const auto nearby = zone_manager_ui::visible_zones( mgr, your_fac, false, from, false );
    REQUIRE( nearby.size() == 3 );
    CHECK( std::ranges::any_of( nearby, []( const auto & ref ) {
        return ref.get().get_name() == "near zone";
    } ) );
    CHECK( std::ranges::any_of( nearby, []( const auto & ref ) {
        return ref.get().get_name() == "edge zone";
    } ) );
    CHECK( std::ranges::any_of( nearby, []( const auto & ref ) {
        return ref.get().get_name() == "other z zone";
    } ) );
    CHECK_FALSE( std::ranges::any_of( nearby, []( const auto & ref ) {
        return ref.get().get_name() == "far zone";
    } ) );

    const auto all = zone_manager_ui::visible_zones( mgr, your_fac, true, from, false );
    CHECK( all.size() == 4 );
}

TEST_CASE( "zm1 ZM-T3 cursor after add", "[zones][ui]" )
{
    // two vehicle zones before and after: the cursor skips them and lands on
    // the newly added zone
    CHECK( zone_manager_ui::cursor_after_add( 5, 2, 2 ) == 2 );
    // the vehicle count changed (a vehicle zone was added): the new entry is
    // the last one
    CHECK( zone_manager_ui::cursor_after_add( 5, 1, 2 ) == 4 );
    // no vehicle zones at all
    CHECK( zone_manager_ui::cursor_after_add( 3, 0, 0 ) == 2 );
    // one add at the end of an otherwise empty list
    CHECK( zone_manager_ui::cursor_after_add( 1, 0, 0 ) == 0 );
}

TEST_CASE( "zm1 ZM-T4 vehicle mass keys", "[zones][ui]" )
{
    reset_zone_state();
    REQUIRE( test_visible_zone.is_valid() );

    auto &mgr = zone_manager::get_manager();
    const auto from = get_avatar().abs_pos();
    add_zone( mgr, "handler", test_visible_zone, from );
    add_zone( mgr, "vehicle", test_visible_zone, from + tripoint_rel_ms( 1, 0, 0 ) );
    add_zone( mgr, "storage", test_visible_zone, from + tripoint_rel_ms( 2, 0, 0 ) );

    auto zones = mgr.get_zones( your_fac );
    REQUIRE( zones.size() == 3 );
    const auto vehicle = std::ranges::find_if( zones, []( const auto & ref ) {
        return ref.get().get_name() == "vehicle";
    } );
    REQUIRE( vehicle != zones.end() );
    // the mass keys act on the vehicle flag; the seam only needs the flag
    vehicle->get().set_is_vehicle( true );

    const auto vehicle_only = []( const zone_data & zone ) {
        return zone.get_is_vehicle();
    };

    CHECK( zone_manager_ui::set_enabled_where( zones, vehicle_only, false ) );
    for( const auto & ref : zones ) {
        const auto &zone = ref.get();
        CHECK( zone.get_enabled() == ( zone.get_name() != "vehicle" ) );
    }
    // the second call changes nothing
    CHECK_FALSE( zone_manager_ui::set_enabled_where( zones, vehicle_only, false ) );

    CHECK( zone_manager_ui::set_enabled_where( zones, vehicle_only, true ) );
    for( const auto & ref : zones ) {
        CHECK( ref.get().get_enabled() );
    }
    CHECK_FALSE( zone_manager_ui::set_enabled_where( zones, vehicle_only, true ) );
}

TEST_CASE( "zm1 ZM-T5 the temporary save restores the zones", "[zones][ui]" )
{
    reset_zone_state();
    REQUIRE( test_visible_zone.is_valid() );

    auto *const world = g->get_active_world();
    REQUIRE( world != nullptr );
    const auto zone_file_base = world->info->folder_path() + "/" +
                                base64_encode( get_avatar().get_save_id() );
    const auto cleanup = on_out_of_scope( [zone_file_base]() {
        remove_file( zone_file_base + ".zones.json" );
        remove_file( zone_file_base + ".zoneszmgr-temp.json" );
        zone_manager::reset_manager();
    } );

    auto &zones = zone_manager::get_manager();
    const auto from = get_avatar().abs_pos();
    add_zone( zones, "temp zone", test_visible_zone, from );
    REQUIRE( zones.size() == 1 );

    // the state at entry: saved to the temporary file before the window loop
    REQUIRE( zones.save_zones( "zmgr-temp" ) );

    // the player disables the zone; the normal save on disk now differs from
    // the temporary file
    auto edited = zones.get_zones( your_fac );
    REQUIRE( edited.size() == 1 );
    edited.front().get().set_enabled( false );
    REQUIRE( zones.save_zones() );

    zones.load_zones( "zmgr-temp" );
    const auto restored = zones.get_zones( your_fac );
    REQUIRE( restored.size() == 1 );
    CHECK( restored.front().get().get_enabled() );

    // the empty-suffix file still holds the changed zone: loading it again
    // gives the disabled state back
    zones.load_zones();
    const auto from_disk = zones.get_zones( your_fac );
    REQUIRE( from_disk.size() == 1 );
    CHECK_FALSE( from_disk.front().get().get_enabled() );
}

TEST_CASE( "zm1 ZM-T6 row text", "[zones][ui]" )
{
    REQUIRE( test_visible_zone.is_valid() );

    const auto from = tripoint_abs_ms( 100, 100, 0 );
    zone_data plain( "row zone", test_visible_zone, your_fac, false, true, from, from );
    const auto plain_text = zone_manager_ui::zone_row_text( plain, from, 45 );
    REQUIRE( plain_text.size() >= 42 );
    CHECK( plain_text[41] == ' ' );

    zone_data vehicle = plain;
    vehicle.set_is_vehicle( true );
    const auto vehicle_text = zone_manager_ui::zone_row_text( vehicle, from, 45 );
    REQUIRE( vehicle_text.size() >= 42 );
    CHECK( vehicle_text[41] == '*' );

    // the enabled flag is colour only: the text is the same either way
    zone_data disabled( "row zone", test_visible_zone, your_fac, false, false, from, from );
    CHECK( zone_manager_ui::zone_row_text( disabled, from, 45 ) == plain_text );
}

TEST_CASE( "zm1 ZM-T7 faction view", "[zones][ui]" )
{
    reset_zone_state();
    REQUIRE( test_visible_zone.is_valid() );

    const faction_id other_faction( "free_merchants" );
    REQUIRE( other_faction.is_valid() );

    auto &mgr = zone_manager::get_manager();
    const auto from = get_avatar().abs_pos();
    add_zone( mgr, "my zone", test_visible_zone, from );
    mgr.add( "their zone", test_visible_zone, other_faction, false, true, from, from );

    const auto mine = zone_manager_ui::visible_zones( mgr, your_fac, true, from, false );
    REQUIRE( mine.size() == 1 );
    CHECK( mine.front().get().get_name() == "my zone" );
    CHECK( mine.front().get().get_faction() == your_fac );

    const auto theirs = zone_manager_ui::visible_zones( mgr, other_faction, true, from, false );
    REQUIRE( theirs.size() == 1 );
    CHECK( theirs.front().get().get_name() == "their zone" );
    CHECK( theirs.front().get().get_faction() == other_faction );
}

TEST_CASE( "zm1 ZM-T8 move direction", "[zones][ui]" )
{
    CHECK( zone_manager_ui::move_zone_index( 3, 5, true ) == 2 );
    CHECK( zone_manager_ui::move_zone_index( 3, 5, false ) == 4 );
    // the ends clamp
    CHECK( zone_manager_ui::move_zone_index( 0, 5, true ) == 0 );
    CHECK( zone_manager_ui::move_zone_index( 4, 5, false ) == 4 );
    // a list of one entry cannot move
    CHECK( zone_manager_ui::move_zone_index( 0, 1, true ) == 0 );
    CHECK( zone_manager_ui::move_zone_index( 0, 1, false ) == 0 );
    CHECK( zone_manager_ui::move_zone_index( 0, 0, true ) == 0 );
}

TEST_CASE( "zm1 ZM-T9 hidden types in the add menu", "[zones][ui]" )
{
    reset_zone_state();
    REQUIRE( test_visible_zone.is_valid() );
    REQUIRE( test_hidden_zone.is_valid() );

    const auto &mgr = zone_manager::get_manager();
    const auto types = mgr.selectable_types();
    CHECK( std::ranges::contains( types, test_visible_zone ) );
    CHECK_FALSE( std::ranges::contains( types, test_hidden_zone ) );
    CHECK( std::ranges::is_sorted( types, []( const zone_type_id & lhs, const zone_type_id & rhs ) {
        return localized_compare( lhs->name(), rhs->name() );
    } ) );
}
