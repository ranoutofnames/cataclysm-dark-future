// DDA source: tests/multimag_test.cpp (DDA commit b4b62254ae2).
// Batch 1b (P3-9-5b): B5 reload_activity_actor_serializes_pocket_index (DDA:1040), B6 item_group_spawn_fills_every_well
// and item_wide_charges_on_multi_well_rejected (DDA:1170,1194), B7 item_action_comparator_ranks_legacy_below_multimag_by_cost
// and shots_remaining_mode_aware (DDA:2050,1737), B8 raw_ammo_tool_drains_paper_count_via_ammo_consume and
// ammo_consume_on_item_without_charges_per_use_drains_raw_count (DDA:2064,2083). Parked: briefs/b95/parked-vehicle_turret_filter_*.cpp (NO_TURRET absent in CDF).
// Ported TEST_CASEs: multimag_predicates
// (DDA:1311-1341), toolmod multiply SECTION of mod_consumption_mods_override_per_pocket
// (DDA:1623-1632), REGULATED-mode WHEN of gunmod_supplies_per_mode_firing_requirements
// (DDA:1456-1466).
// Parked (briefs/b95/parked-integral_magazine_ammo_consume.cpp): integral_magazine_ammo_consume (DDA:175),
// sw_619 has no integral MAGAZINE pocket in CDF.
// Parked, not ported (blocked by OD-7: islot_gunmod::hide_modes absent in CDF, see
// src/item_factory.cpp:1870): the "DEFAULT is hidden" THEN and the hidden-DEFAULT WHEN of
// gunmod_supplies_per_mode_firing_requirements, and gunmod_hide_modes_global_kill_switch.

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "debug.h"
#include "item_group.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "reload/reload.h"
#include "type_id.h"

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static const itype_id itype_556( "556" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_heavy_battery_cell( "heavy_battery_cell" );
static const itype_id itype_p3_multimag_gun_consume( "p3_multimag_gun_consume" );
static const itype_id itype_p3_multimag_tool_consume( "p3_multimag_tool_consume" );
static const itype_id itype_p3_book_binder( "p3_book_binder" );
static const itype_id itype_p3_paper_ammo( "p3_paper_ammo" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_test_multimag_consume_toolmod( "test_multimag_consume_toolmod" );
static const itype_id itype_test_multimag_gun_consume( "test_multimag_gun_consume" );
static const itype_id itype_test_multimag_gun( "test_multimag_gun" );
static const itype_id itype_test_multimag_regulated_mod( "test_multimag_regulated_mod" );

// DDA tests/multimag_test.cpp:1267-1277. CDF: item's move constructor is deleted
// (src/item.h:241), copy into the return value.
static item make_multimag_consume_gun( int bb_count = 15, int batt = 100 )
{
    item gun( itype_test_multimag_gun_consume );
    item bb_mag( itype_glockmag );
    bb_mag.put_in( item( itype_9mm, calendar::turn, bb_count ), pocket_type::MAGAZINE );
    item batt_mag( itype_heavy_battery_cell );
    batt_mag.put_in( item( itype_battery, calendar::turn, batt ), pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun );
}

// CDF: p3_ copy of test_multimag_gun_consume adding a MOD pocket
// (data/mods/TEST_DATA/multimag_p3.json); the DDA fixture generates one itself.
static item make_multimag_consume_gun_with_mod_bay( int bb_count = 15, int batt = 100 )
{
    item gun( itype_p3_multimag_gun_consume );
    item bb_mag( itype_glockmag );
    bb_mag.put_in( item( itype_9mm, calendar::turn, bb_count ), pocket_type::MAGAZINE );
    item batt_mag( itype_heavy_battery_cell );
    batt_mag.put_in( item( itype_battery, calendar::turn, batt ), pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun );
}

// DDA tests/multimag_test.cpp:1279-1289, on the p3_ copy with a MOD pocket (see above).
static item make_multimag_consume_tool_with_mod_bay( int well_a_charges = 12,
        int well_b_charges = 30 )
{
    item tool( itype_p3_multimag_tool_consume );
    item mag_a( itype_glockmag );
    mag_a.put_in( item( itype_9mm, calendar::turn, well_a_charges ), pocket_type::MAGAZINE );
    item mag_b( itype_stanag30 );
    mag_b.put_in( item( itype_556, calendar::turn, well_b_charges ), pocket_type::MAGAZINE );
    REQUIRE( tool.put_in( mag_a, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool.put_in( mag_b, pocket_type::MAGAZINE_WELL ).success() );
    return item( tool );
}

// DDA tests/multimag_test.cpp:1311-1341. Depends on 2-d fixtures and 3-5a/8-1
// (uses_firing_requirements, pocket_by_id, ammo_remaining_in_pocket).
TEST_CASE( "multimag_predicates", "[multimag][consume][p3]" )
{
    SECTION( "uses_firing_requirements" ) {
        item legacy( itype_glock_19 );
        CHECK_FALSE( legacy.uses_firing_requirements() );

        item gun = make_multimag_consume_gun();
        CHECK( gun.uses_firing_requirements() );
    }

    SECTION( "needs_charges_to_use covers multimag" ) {
        item gun = make_multimag_consume_gun();
        CHECK( gun.needs_charges_to_use() );
    }

    SECTION( "pocket_by_id" ) {
        item gun = make_multimag_consume_gun();
        CHECK( gun.pocket_by_id( "ammo" ) != nullptr );
        CHECK( gun.pocket_by_id( "power" ) != nullptr );
        CHECK( gun.pocket_by_id( "missing" ) == nullptr );
    }

    SECTION( "ammo_remaining_in_pocket" ) {
        item gun = make_multimag_consume_gun( 12, 80 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 12 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 80 );
        CHECK( gun.ammo_remaining_in_pocket( "missing" ) == 0 );
    }
}

// DDA tests/multimag_test.cpp:1623-1632 (toolmod multiply SECTION of
// mod_consumption_mods_override_per_pocket; the gunmod SECTION is ported as
// cdf_item_firing_p3_test.cpp "mod_consumption_mods_override_per_pocket").
// Depends on consumption_mods (3-5a/8-1).
TEST_CASE( "mod_consumption_mods_toolmod_multiply_per_pocket", "[multimag][consume][mod][p3]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    SECTION( "toolmod multiply scales a pocket's per-use cost" ) {
        item tool = make_multimag_consume_tool_with_mod_bay( 12, 30 );  // well_a 2/use, well_b 1/use
        REQUIRE( tool.put_in( item( itype_test_multimag_consume_toolmod ),
                              pocket_type::MOD ).success() );
        const int got = tool.consume_tool_uses( 3, here, pos, nullptr );
        CHECK( got == 3 );
        // well_a: max(1, round(2*0.5)) = 1 per use -> 12 - 3
        CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 9 );
        CHECK( tool.ammo_remaining_in_pocket( "well_b" ) == 27 );  // untouched
    }
}

// DDA tests/multimag_test.cpp:1456-1466, the REGULATED-mode WHEN. The sibling THEN
// ("DEFAULT is hidden") and WHEN (hidden DEFAULT blocked) need hide_modes, absent in
// CDF (OD-7), and are parked, not ported.
TEST_CASE( "gunmod_supplies_per_mode_firing_requirements_regulated_fire",
           "[multimag][consume][mod][p3]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    item gun = make_multimag_consume_gun_with_mod_bay( 15, 100 );
    REQUIRE( gun.put_in( item( itype_test_multimag_regulated_mod ),
                         pocket_type::MOD ).success() );
    const std::map<gun_mode_id, gun_mode> modes = gun.gun_all_modes();
    REQUIRE( modes.count( gun_mode_id( "REGULATED" ) ) == 1 );

    const int got = gun.consume_shots( gun_mode_id( "REGULATED" ), 3, here, pos, nullptr );
    CHECK( got == 3 );
    // 15 - 3*1
    CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 12 );
    // 100 - 3*1 (host DEFAULT would have drawn 5/shot)
    CHECK( gun.ammo_remaining_in_pocket( "power" ) == 97 );
}

// DDA tests/multimag_test.cpp:1040. Depends on 6-1a (reload_activity_actor serialises
// pocket_index). CDF: no item_location; item_reload_option takes item * + parent item
// (see cdf_item_reload_p3_test.cpp), spawned with item::spawn / i_add( detached_ptr ).
TEST_CASE( "reload_activity_actor_serializes_pocket_index",
           "[multimag][reload][serialize]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun );
    detached_ptr<item> glock_mag = item::spawn( itype_glockmag );
    glock_mag->put_in( item::spawn( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun_ptr->put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );
    item &gun = you.i_add( std::move( gun_ptr ) );

    detached_ptr<item> ammo_ptr = item::spawn( itype_glockmag );
    ammo_ptr->put_in( item::spawn( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    item &ammo = you.i_add( std::move( ammo_ptr ) );

    item_reload_option opt( &get_avatar(), &gun, &gun, ammo, /*pocket_index=*/0 );
    reload_activity_actor actor( std::move( opt ) );

    std::ostringstream oss;
    JsonOut jsout( oss );
    actor.serialize( jsout );
    const std::string serialized = oss.str();
    CAPTURE( serialized );
    CHECK( serialized.find( "\"pocket_index\":0" ) != std::string::npos );
}

// DDA tests/multimag_test.cpp:1170. Depends on 3-5b (dress_magazine_wells at spawn sites).
// CDF: items_from returns detached_ptr<item>.
TEST_CASE( "item_group_spawn_fills_every_well",
           "[multimag][spawn]" )
{
    const std::vector<detached_ptr<item>> items = item_group::items_from(
                item_group_id( "test_multimag_full_load" ) );
    REQUIRE( items.size() == 1 );
    const item &gun = *items[0];
    REQUIRE( gun.typeId() == itype_test_multimag_gun );

    const std::vector<const item *> mags = gun.magazines_current();
    CHECK( mags.size() == 2 );

    std::set<itype_id> mag_types;
    for( const item *m : mags ) {
        mag_types.insert( m->typeId() );
    }
    CHECK( mag_types.count( itype_glockmag ) == 1 );
    CHECK( mag_types.count( itype_stanag30 ) == 1 );

    for( const item *m : mags ) {
        CHECK( m->ammo_remaining() > 0 );
    }
}

// DDA tests/multimag_test.cpp:1194. Depends on 3-5b ("ambiguous" debugmsg in Item_modifier::modify).
// CDF: modify takes/returns detached_ptr<item>; the modify signature has no context string.
TEST_CASE( "item_wide_charges_on_multi_well_rejected",
           "[multimag][spawn]" )
{
    Item_modifier modifier;
    modifier.charges = { 20, 20 };
    detached_ptr<item> gun = item::spawn( itype_test_multimag_gun );

    const std::string dmsg = capture_debugmsg_during( [&modifier, &gun]() {
        gun = modifier.modify( std::move( gun ) );
    } );

    CHECK( dmsg.find( "ambiguous" ) != std::string::npos );
    REQUIRE( gun );
    CHECK( gun->typeId() == itype_test_multimag_gun );
    CHECK( gun->magazines_current().empty() );
    CHECK( gun->ammo_remaining() == 0 );
}

// DDA tests/multimag_test.cpp:2050. Depends on 8-4c (expected_cost_per_use / needs_charges_to_use).
TEST_CASE( "item_action_comparator_ranks_legacy_below_multimag_by_cost",
           "[multimag][consume]" )
{
    item legacy( itype_glock_19 );
    item multimag = make_multimag_consume_gun();
    // glock_19: ammo_to_fire=1 -> expected_cost_per_use=1.
    // multimag DEFAULT: ammo qty=1 + power qty=5 -> sum=6.
    CHECK( legacy.expected_cost_per_use() < multimag.expected_cost_per_use() );

    // Multimag tool reports needs_charges_to_use, so the action-menu
    // free-to-use shortcut never auto-marks it.
    CHECK( multimag.needs_charges_to_use() );
}

// DDA tests/multimag_test.cpp:1737. Depends on 8-1 (mode-aware shots_remaining).
TEST_CASE( "shots_remaining_mode_aware",
           "[multimag][consume]" )
{
    item gun = make_multimag_consume_gun( 15, 100 );
    const map &here = get_map();
    // DEFAULT mode is the gun's selected default.
    CHECK( gun.shots_remaining( here, nullptr ) == std::min( 15, 100 / 5 ) );
}

// DDA tests/multimag_test.cpp:2064. Depends on 4-b2 (ammo_consume on raw-ammo tools).
// CDF: DDA `book_binder` / `paper` do not exist (CDF `paper` is a GENERIC); the p3_book_binder /
// p3_paper_ammo fixtures in data/mods/TEST_DATA/multimag_p3.json copy them. Assertions unchanged.
TEST_CASE( "raw_ammo_tool_drains_paper_count_via_ammo_consume",
           "[multimag][consume]" )
{
    clear_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    // book_binder is a TOOL with a MAGAZINE pocket of paper, no
    // charges_per_use; bookbinder copy drains a per-recipe page count
    // directly via ammo_consume.
    item binder( itype_p3_book_binder );
    item paper_pile( itype_p3_paper_ammo, calendar::turn, 50 );
    REQUIRE( binder.put_in( paper_pile, pocket_type::MAGAZINE ).success() );
    REQUIRE( binder.ammo_remaining() == 50 );

    const int consumed = binder.ammo_consume( 7, pos, nullptr );
    CHECK( consumed == 7 );
    CHECK( binder.ammo_remaining() == 43 );
}

// DDA tests/multimag_test.cpp:2083. Depends on 4-b2 (ammo_consume with raw count).
TEST_CASE( "ammo_consume_on_item_without_charges_per_use_drains_raw_count",
           "[multimag][consume]" )
{
    clear_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    // Equivalent shape to multi_cooker's upfront-buffer activation:
    // legacy item with no charges_per_use; raw ammo_consume must drain
    // exactly the requested charge count, not collapse to a use count.
    item bb_mag( itype_glockmag );
    bb_mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    item legacy( itype_glock_19 );
    REQUIRE( legacy.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );

    const int consumed = legacy.ammo_consume( 4, pos, nullptr );
    CHECK( consumed == 4 );
    CHECK( legacy.ammo_remaining() == 11 );
}
