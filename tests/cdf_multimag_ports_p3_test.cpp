// DDA source: tests/multimag_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASEs: multimag_predicates
// (DDA:1311-1341), toolmod multiply SECTION of mod_consumption_mods_override_per_pocket
// (DDA:1623-1632), REGULATED-mode WHEN of gunmod_supplies_per_mode_firing_requirements
// (DDA:1456-1466).
// Parked (briefs/b95/parked-integral_magazine_ammo_consume.cpp): integral_magazine_ammo_consume (DDA:175),
// sw_619 has no integral MAGAZINE pocket in CDF.
// Parked, not ported (blocked by OD-7: islot_gunmod::hide_modes absent in CDF, see
// src/item_factory.cpp:1870): the "DEFAULT is hidden" THEN and the hidden-DEFAULT WHEN of
// gunmod_supplies_per_mode_firing_requirements, and gunmod_hide_modes_global_kill_switch.

#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_type.h"
#include "type_id.h"

#include <map>

static const itype_id itype_556( "556" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_heavy_battery_cell( "heavy_battery_cell" );
static const itype_id itype_p3_multimag_gun_consume( "p3_multimag_gun_consume" );
static const itype_id itype_p3_multimag_tool_consume( "p3_multimag_tool_consume" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_test_multimag_consume_toolmod( "test_multimag_consume_toolmod" );
static const itype_id itype_test_multimag_gun_consume( "test_multimag_gun_consume" );
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
