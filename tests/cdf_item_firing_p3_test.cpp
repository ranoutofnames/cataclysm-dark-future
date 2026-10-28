// DDA source: tests/multimag_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASEs: expected_cost_per_use sums effective_qty (DDA:1799);
// CDF-only guard: expected_cost_per_use method string is neutral (Decision 3).

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "debug_log_capture.h"
#include "examine_item_menu.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_functions.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "relic.h"
#include "state_helpers.h"
#include "units.h"

#include <sstream>
#include <string>
#include <vector>

static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_p3_multimag_gun_consume( "p3_multimag_gun_consume" );
static const itype_id itype_test_multimag_aux_gun( "test_multimag_aux_gun" );
static const itype_id itype_test_multimag_consume_gunmod( "test_multimag_consume_gunmod" );
static const itype_id itype_test_multimag_consume_gunmod_mult( "test_multimag_consume_gunmod_mult" );
static const itype_id itype_test_multimag_gun_consume( "test_multimag_gun_consume" );
static const itype_id itype_test_multimag_tool_gunmod( "test_multimag_tool_gunmod" );

// DDA tests/multimag_test.cpp:106-113.
static item make_loaded_glock()
{
    item mag( itype_id( "glockmag" ) );
    mag.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    item gun( itype_id( "glock_19" ) );
    gun.put_in( mag, pocket_type::MAGAZINE_WELL );
    // CDF: item's move constructor is deleted (src/item.h:241); copy into the
    // return value like tests/cdf_item_multimag_p3_test.cpp does.
    return item( gun );
}

// DDA tests/multimag_test.cpp:1267-1277.
static item make_multimag_consume_gun( int bb_count = 15, int batt = 100 )
{
    item gun( itype_id( "test_multimag_gun_consume" ) );
    item bb_mag( itype_id( "glockmag" ) );
    bb_mag.put_in( item( itype_id( "9mm" ), calendar::turn, bb_count ), pocket_type::MAGAZINE );
    item batt_mag( itype_id( "heavy_battery_cell" ) );
    batt_mag.put_in( item( itype_id( "battery" ), calendar::turn, batt ),
                     pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun ); // CDF: item move ctor deleted
}

// CDF: test_multimag_gun_consume has explicit pocket_data, which suppresses the
// generated MOD pocket; cases that install mods use the p3_ copy adding a mod
// bay (data/mods/TEST_DATA/multimag_p3.json), as in
// tests/cdf_item_pocket_p3_test.cpp. Stats and firing_requirements are inherited.
static item make_multimag_consume_gun_with_mod_bay( int bb_count = 15, int batt = 100 )
{
    item gun( itype_p3_multimag_gun_consume );
    item bb_mag( itype_id( "glockmag" ) );
    bb_mag.put_in( item( itype_id( "9mm" ), calendar::turn, bb_count ), pocket_type::MAGAZINE );
    item batt_mag( itype_id( "heavy_battery_cell" ) );
    batt_mag.put_in( item( itype_id( "battery" ), calendar::turn, batt ),
                     pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun ); // CDF: item move ctor deleted
}

// DDA tests/multimag_test.cpp:1279-1289.
static item make_multimag_consume_tool( int well_a_charges = 12, int well_b_charges = 30 )
{
    item tool( itype_id( "test_multimag_tool_consume" ) );
    item mag_a( itype_id( "glockmag" ) );
    mag_a.put_in( item( itype_id( "9mm" ), calendar::turn, well_a_charges ),
                  pocket_type::MAGAZINE );
    item mag_b( itype_id( "stanag30" ) );
    mag_b.put_in( item( itype_id( "556" ), calendar::turn, well_b_charges ),
                  pocket_type::MAGAZINE );
    REQUIRE( tool.put_in( mag_a, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool.put_in( mag_b, pocket_type::MAGAZINE_WELL ).success() );
    return item( tool ); // CDF: item move ctor deleted
}

// DDA tests/multimag_test.cpp:1290-1309. Multimag USE_UPS welder fixture: power
// well = heavy_battery_cell mag, oxygen and rod are direct MAGAZINE pockets with
// ammo_restriction. per_use: power=5, oxygen=2, rod=1.
// CDF: test_multimag_vehicle_welder (data/mods/TEST_DATA/items.json:1983) and
// AMMO welding_wire_steel_tests_dupe (same file:1962) are CDF fixture names.
static item make_multimag_welder( int batt_charges, int oxygen_qty, int rod_qty )
{
    item tool( itype_id( "test_multimag_vehicle_welder" ) );
    item batt_mag( itype_id( "heavy_battery_cell" ) );
    batt_mag.put_in( item( itype_id( "battery" ), calendar::turn, batt_charges ),
                     pocket_type::MAGAZINE );
    REQUIRE( tool.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    if( oxygen_qty > 0 ) {
        REQUIRE( tool.put_in( item( itype_id( "oxygen" ), calendar::turn, oxygen_qty ),
                              pocket_type::MAGAZINE ).success() );
    }
    if( rod_qty > 0 ) {
        REQUIRE( tool.put_in( item( itype_id( "welding_wire_steel_tests_dupe" ),
                                    calendar::turn, rod_qty ),
                              pocket_type::MAGAZINE ).success() );
    }
    return item( tool ); // CDF: item move ctor deleted
}

// DDA tests/multimag_test.cpp:1799-1809. CDF: the legacy gun's cost per use is
// the DDA literal 1, not ammo_required(); glock_19 has no ammo_to_fire
// (data/json/items/gun/9mm.json:73, copy-from pistol_base) and the default is 1
// (src/itype.h:738).
TEST_CASE( "expected_cost_per_use_sums_effective_qty", "[p3][firing]" )
{
    item gun( itype_test_multimag_gun_consume );
    CHECK( gun.expected_cost_per_use() == 6 );

    item legacy( itype_glock_19 );
    // CDF: glock_19 has no ammo_to_fire (9mm.json:73, pistol_base), itype.h:738 default 1
    CHECK( legacy.expected_cost_per_use() == 1 );
}

// Decision 3: itype::ammo_scale is absent, so method_scale is neutral and any
// method string keeps the same cost.
TEST_CASE( "expected_cost_per_use_method_string_is_neutral", "[p3][firing]" )
{
    item gun( itype_test_multimag_gun_consume );
    CHECK( gun.expected_cost_per_use( "no_such_method" ) == 6 );

    item legacy( itype_glock_19 );
    // CDF: glock_19 has no ammo_to_fire (9mm.json:73, pistol_base), itype.h:738 default 1
    CHECK( legacy.expected_cost_per_use( "no_such_method" ) == 1 );
}

// DDA tests/multimag_test.cpp:1785-1797.
TEST_CASE( "format_consumption_requirements_scaling", "[p3][firing]" )
{
    item tool = make_multimag_consume_tool();
    const std::string s = tool.format_consumption_requirements(
                              /*method=*/"", gun_mode_id( "DEFAULT" ), /*uses=*/3 );
    // qty 2 * 3 = 6 from well_a, qty 1 * 3 = 3 from well_b.
    CHECK( s.find( '6' ) != std::string::npos );
    CHECK( s.find( '3' ) != std::string::npos );
    CHECK( s.find( '+' ) != std::string::npos );
}

// DDA tests/multimag_test.cpp:1923-1931, format SECTION only: the
// consume_shots section is owned by 8-1b.
TEST_CASE( "gunmod_added_modes_inherit_DEFAULT_format", "[p3][firing]" )
{
    item gun = make_multimag_consume_gun();
    const std::string s = gun.format_consumption_requirements(
                              /*method=*/"", gun_mode_id( "AUTO" ), /*uses=*/1 );
    CHECK_FALSE( s.empty() );
    // DEFAULT qty=1 + 5 -> string contains both "1" and "5".
    CHECK( s.find( '1' ) != std::string::npos );
    CHECK( s.find( '5' ) != std::string::npos );
}

// DDA tests/multimag_test.cpp:1905-1921 and 1933-1940 (the format SECTION,
// 1923-1931, is gunmod_added_modes_inherit_DEFAULT_format above).
TEST_CASE( "gunmod_added_modes_inherit_DEFAULT_firing_requirements", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    tripoint_bub_ms pos( tripoint_bub_ms::zero() ); // CDF: zero is a function here

    SECTION( "consume_shots on an unlisted mode falls back to DEFAULT" ) {
        item gun = make_multimag_consume_gun();
        REQUIRE( gun.uses_firing_requirements() ); // CDF
        // "AUTO" is not in test_multimag_gun_consume's modes [DEFAULT, BURST].
        // It's also not in firing_requirements; treated as gunmod-added.
        const int got = gun.consume_shots( gun_mode_id( "AUTO" ), 2, here, pos, nullptr );
        // DEFAULT entries: ammo qty=1, power qty=5; 2 shots -> 2 ammo, 10 power.
        CHECK( got == 2 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 13 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 90 );
    }

    SECTION( "BURST mode uses its own firing_requirements, not DEFAULT" ) {
        item gun = make_multimag_consume_gun();
        REQUIRE( gun.uses_firing_requirements() ); // CDF
        const int got = gun.consume_shots( gun_mode_id( "BURST" ), 1, here, pos, nullptr );
        // BURST entries: ammo qty=3, power qty=15.
        CHECK( got == 1 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 12 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 85 );
    }
}

// DDA tests/multimag_test.cpp:1943-1956.
TEST_CASE( "format_consumption_requirements_legacy_item", "[p3][firing]" )
{
    item gun = make_loaded_glock();
    REQUIRE_FALSE( gun.uses_firing_requirements() );
    const std::string s_one = gun.format_consumption_requirements(
                                  /*method=*/"", gun_mode_id( "DEFAULT" ), /*uses=*/1 );
    // glock_19 ammo_required = 1, no energy_drain.
    CHECK( s_one.find( '1' ) != std::string::npos );

    const std::string s_three = gun.format_consumption_requirements(
                                    /*method=*/"", gun_mode_id( "DEFAULT" ), /*uses=*/3 );
    CHECK( s_three.find( '3' ) != std::string::npos );
}

// DDA tests/multimag_test.cpp:1364-1379.
TEST_CASE( "tool_uses_remaining_feasibility", "[p3][firing]" )
{
    SECTION( "non-fungible local stock; pocket B empty caps capacity" ) {
        // tool_a qty=2, tool_b qty=1. well_a 12 charges, well_b 0 charges.
        item tool = make_multimag_consume_tool( 12, 0 );
        CHECK( tool.tool_uses_remaining_local() == 0 );
    }

    SECTION( "abundant local in both wells supports min-uses computation" ) {
        // qty 2 from well_a, qty 1 from well_b.
        // well_a 12 / 2 = 6, well_b 30 / 1 = 30 -> min 6 uses.
        item tool = make_multimag_consume_tool( 12, 30 );
        CHECK( tool.tool_uses_remaining_local() == 6 );
    }
}

// DDA tests/multimag_test.cpp:1895-1903.
TEST_CASE( "tool_uses_remaining_vs_local", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    item tool = make_multimag_consume_tool( 12, 30 );
    // No carrier, no UPS, no cable: external pool is zero.
    CHECK( tool.tool_uses_remaining_local() == tool.tool_uses_remaining( here, nullptr ) );
}

// DDA tests/multimag_test.cpp:1381-1402. Tool and welder SECTIONs (1404-1433)
// are not ported here (owned by the tool-consumption job).
// CDF: gun_mode_id constants absent
TEST_CASE( "consume_shots_and_consume_tool_uses_drain_per_pocket", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    tripoint_bub_ms pos( tripoint_bub_ms::zero() ); // CDF: zero is a function here

    SECTION( "gun DEFAULT mode drains 1 ammo + 5 power per shot" ) {
        item gun = make_multimag_consume_gun( 15, 100 );
        REQUIRE( gun.uses_firing_requirements() );
        const int got = gun.consume_shots( gun_mode_id( "DEFAULT" ), 3, here, pos, nullptr );
        CHECK( got == 3 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 12 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 85 );
    }

    SECTION( "gun BURST mode drains 3 ammo + 15 power per shot" ) {
        item gun = make_multimag_consume_gun( 15, 100 );
        REQUIRE( gun.uses_firing_requirements() );
        const int got = gun.consume_shots( gun_mode_id( "BURST" ), 2, here, pos, nullptr );
        CHECK( got == 2 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 9 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 70 );
    }
}

// DDA tests/multimag_test.cpp:1607-1621, gunmod SECTION only: the toolmod
// multiply SECTION (1623-1632) is not ported here.
TEST_CASE( "mod_consumption_mods_override_per_pocket", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    SECTION( "gunmod set overrides a pocket's per-shot cost" ) {
        item gun = make_multimag_consume_gun_with_mod_bay( 15, 100 );  // DEFAULT: ammo 1, power 5
        REQUIRE( gun.uses_firing_requirements() );
        REQUIRE( gun.put_in( item( itype_test_multimag_consume_gunmod ),
                             pocket_type::MOD ).success() );
        const int got = gun.consume_shots( gun_mode_id( "DEFAULT" ), 2, here, pos, nullptr );
        CHECK( got == 2 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 13 );   // 1/shot untouched
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 96 );  // set 2/shot -> 100 - 2*2
    }
}

// DDA tests/multimag_test.cpp:1646-1676.
TEST_CASE( "consumption_mods_combine_order_independent", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    // Two mods on the power pocket: set 2 and multiply 0.5 -> 2*0.5 = 1 per shot,
    // independent of install order.
    auto power_drained = [&]( bool set_first ) -> int {
        item gun = make_multimag_consume_gun_with_mod_bay( 15, 100 );
        REQUIRE( gun.uses_firing_requirements() );
        if( set_first )
        {
            REQUIRE( gun.put_in( item( itype_test_multimag_consume_gunmod ), pocket_type::MOD ).success() );
            REQUIRE( gun.put_in( item( itype_test_multimag_consume_gunmod_mult ),
                                 pocket_type::MOD ).success() );
        } else
        {
            REQUIRE( gun.put_in( item( itype_test_multimag_consume_gunmod_mult ),
                                 pocket_type::MOD ).success() );
            REQUIRE( gun.put_in( item( itype_test_multimag_consume_gunmod ), pocket_type::MOD ).success() );
        }
        REQUIRE( gun.consume_shots( gun_mode_id( "DEFAULT" ), 1, here, pos, nullptr ) == 1 );
        return 100 - gun.ammo_remaining_in_pocket( "power" );
    };

    const int set_first = power_drained( true );
    const int mult_first = power_drained( false );
    CAPTURE( set_first, mult_first );
    CHECK( set_first == 1 );
    CHECK( mult_first == 1 );
}

// DDA tests/multimag_test.cpp:1678-1701.
TEST_CASE( "tool_gunmod_firing_requirements_is_not_own_consumption", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    // A TOOL+GUNMOD's mode_firing_requirements is the cost it imposes on its gun
    // host, not its own tool consumption: the bare item is not a multimag host.
    item tg( itype_test_multimag_tool_gunmod );
    REQUIRE( tg.is_tool() );
    REQUIRE( tg.is_gunmod() );
    CHECK_FALSE( tg.uses_firing_requirements() );

    SECTION( "installed on a gun, its mode draws the mod's per-pocket cost" ) {
        item gun = make_multimag_consume_gun_with_mod_bay( 15, 100 );
        REQUIRE( gun.uses_firing_requirements() );
        REQUIRE( gun.put_in( item( itype_test_multimag_tool_gunmod ),
                             pocket_type::MOD ).success() );
        // CDF: gun_mode_TGMODE absent
        REQUIRE( gun.gun_all_modes().count( gun_mode_id( "TGMODE" ) ) == 1 );
        const int got = gun.consume_shots( gun_mode_id( "TGMODE" ), 2, here, pos, nullptr );
        CHECK( got == 2 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 13 );   // 15 - 2*1
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 98 );  // 100 - 2*1 (DEFAULT would be 5)
    }
}

// DDA tests/multimag_test.cpp:1703-1735.
TEST_CASE( "aux_gunmod_mode_blocked_on_multimag_host", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    item gun = make_multimag_consume_gun_with_mod_bay( 15, 100 );
    REQUIRE( gun.uses_firing_requirements() );
    REQUIRE( gun.put_in( item( itype_test_multimag_aux_gun ), pocket_type::MOD ).success() );

    // The aux gun-gunmod contributes a mode whose gun_mode target is the aux item.
    gun_mode_id aux_mode;
    for( const std::pair<const gun_mode_id, gun_mode> &m : gun.gun_all_modes() ) {
        if( m.second.target != &gun ) {
            aux_mode = m.first;
        }
    }
    REQUIRE_FALSE( aux_mode.is_empty() );

    WHEN( "the aux mode is selected on the multimag host" ) {
        THEN( "it is blocked: not fireable, host pockets untouched" ) {
            CHECK( gun.firing_mode_blocked( aux_mode ) );
            int consumed = -1;
            // consume_shots intentionally debugmsgs once when refusing an aux mode.
            const std::string dmsg = capture_debugmsg_during( [&]() {
                consumed = gun.consume_shots( aux_mode, 1, here, pos, nullptr );
            } );
            CHECK( consumed == 0 );
            CHECK( dmsg.find( "aux-gunmod" ) != std::string::npos );
            CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 15 );
            CHECK( gun.ammo_remaining_in_pocket( "power" ) == 100 );
        }
    }
}

// DDA tests/multimag_test.cpp:1746-1763. Passes before the edit: the 4-arg
// ammo_consume guard for multimag items is already present (src/item_gun_tool_ammo.cpp).
TEST_CASE( "ammo_consume_qty_hard_guard_for_multimag", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    tripoint_bub_ms pos( tripoint_bub_ms::zero() ); // CDF: zero is a function here

    item gun = make_multimag_consume_gun();
    REQUIRE( gun.uses_firing_requirements() );
    int consumed = 0;
    const std::string captured = capture_debugmsg_during( [&]() {
        consumed = gun.ammo_consume( 5, here, pos, nullptr );
    } );
    CHECK( consumed == 0 );
    CHECK( captured.find( "multimag" ) != std::string::npos );
    // Stocks unchanged.
    CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 15 );
    CHECK( gun.ammo_remaining_in_pocket( "power" ) == 100 );
}

// NEW (8-1b): a non-gun is never blocked. The "gun with no modes is true"
// sub-case needs a TEST_DATA gun with empty modes; none exists, so it is not
// written (listed under Blocked in the report).
TEST_CASE( "firing_mode_blocked_guard", "[p3][firing]" )
{
    item tool_gm( itype_test_multimag_tool_gunmod );
    CHECK_FALSE( tool_gm.firing_mode_blocked( gun_mode_id( "DEFAULT" ) ) );
}

// DDA tests/multimag_test.cpp:1765-1783. The name says "non_divisible" but the
// DDA body has only the divisible SECTION, so both are kept as written.
// CDF: the legacy_charges_per_use_factor field now exists, but CDF use_charges has no
// firing-requirements branch, so the guard and DDA's hard_fails case
// (multimag_test.cpp:1847) are Blocked pending the use_charges rewrite (8-4f).
// This case covers the multimag consumption path only.
TEST_CASE( "legacy_charges_per_use_factor_non_divisible_request", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    tripoint_bub_ms pos( tripoint_bub_ms::zero() ); // CDF: zero is a function here

    // Tool with factor=5 + one well @ qty=1.
    item tool( itype_id( "test_multimag_tool_factor" ) );
    item mag_a( itype_id( "glockmag" ) );
    mag_a.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    REQUIRE( tool.put_in( mag_a, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool.uses_firing_requirements() );

    SECTION( "divisible qty: 10 charges -> 2 uses -> 2 from well" ) {
        const int got = tool.consume_tool_uses( 2, here, pos, nullptr );
        CHECK( got == 2 );
        CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 13 );
    }
}

// DDA tests/multimag_test.cpp:1381, all three tool SECTIONs (1404-1420 and the
// welder SECTION 1421-1432, restored here: Ledger row 2).
// CDF: new TEST_CASE name; the DDA name is already used by 8-1b above.
TEST_CASE( "consume_tool_uses_drain_per_pocket", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    tripoint_bub_ms pos( tripoint_bub_ms::zero() ); // CDF: zero is a function here

    SECTION( "tool consume drains both wells linearly" ) {
        item tool = make_multimag_consume_tool( 12, 30 );
        REQUIRE( tool.uses_firing_requirements() );
        const int got = tool.consume_tool_uses( 3, here, pos, nullptr );
        CHECK( got == 3 );
        CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 6 );
        CHECK( tool.ammo_remaining_in_pocket( "well_b" ) == 27 );
    }

    SECTION( "shortfall in one well caps consumption" ) {
        // well_b has 1 charge. qty=1 means only 1 use possible.
        item tool = make_multimag_consume_tool( 12, 1 );
        REQUIRE( tool.uses_firing_requirements() );
        const int got = tool.consume_tool_uses( 5, here, pos, nullptr );
        CHECK( got == 1 );
        CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 10 );
        CHECK( tool.ammo_remaining_in_pocket( "well_b" ) == 0 );
    }

    SECTION( "direct MAGAZINE oxygen pocket drains alongside MAGAZINE_WELL battery" ) {
        // Welder per_use: power=5, oxygen=2, rod=1. Single use drains all three.
        item welder = make_multimag_welder( 100, 20, 5 );
        REQUIRE( welder.ammo_remaining_in_pocket( "power" ) == 100 );
        REQUIRE( welder.ammo_remaining_in_pocket( "oxygen" ) == 20 );
        REQUIRE( welder.ammo_remaining_in_pocket( "rod" ) == 5 );
        const int got = welder.consume_tool_uses( 1, here, pos, nullptr );
        CHECK( got == 1 );
        CHECK( welder.ammo_remaining_in_pocket( "power" ) == 95 );
        CHECK( welder.ammo_remaining_in_pocket( "oxygen" ) == 18 );
        CHECK( welder.ammo_remaining_in_pocket( "rod" ) == 4 );
    }
}

// NEW (8-1c): a legacy tool with no firing requirements drains qty * ammo_required().
TEST_CASE( "activation_consume_legacy_tool_drains_qty_times_ammo_required", "[p3][firing]" )
{
    clear_map();
    item tool( itype_id( "test_soldering_iron" ) );
    tool.ammo_set( itype_id( "battery" ), 10 );
    REQUIRE_FALSE( tool.uses_firing_requirements() );
    REQUIRE( tool.ammo_required() == 1 );
    CHECK( tool.activation_consume( 2, tripoint_bub_ms::zero(), nullptr ) == 2 );
    CHECK( tool.ammo_remaining() == 8 );
}

// NEW (8-1c): firing-requirement tools route activation_consume through consume_tool_uses.
TEST_CASE( "activation_consume_firing_requirements_tool_uses_consume_tool_uses", "[p3][firing]" )
{
    clear_map();
    item tool = make_multimag_consume_tool( 12, 30 );
    REQUIRE( tool.uses_firing_requirements() );
    CHECK( tool.activation_consume( 3, tripoint_bub_ms::zero(), nullptr ) == 3 );
    CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 6 );
    CHECK( tool.ammo_remaining_in_pocket( "well_b" ) == 27 );
}

// NEW (8-1c): guard of Decision 2b. A non-tool with no firing requirements
// returns uses and drains nothing.
TEST_CASE( "consume_tool_uses_non_tool_gate", "[p3][firing]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();
    item gun = make_loaded_glock();
    REQUIRE_FALSE( gun.uses_firing_requirements() );
    const int before = gun.ammo_remaining();
    CHECK( gun.consume_tool_uses( 2, here, pos, nullptr ) == 2 );
    CHECK( gun.ammo_remaining() == before );
}

// P3-8-1d energy tail. Test numbers follow the brief (test 7 dropped, HD-8).
static const itype_id itype_test_energy_gun( "test_energy_gun" );
static const itype_id itype_test_energy_gunmod( "test_energy_gunmod" );

// CDF: CDF-only test (no DDA counterpart).
TEST_CASE( "get_gun_energy_drain_zero_without_energy_drain", "[p3][firing][energy][8-1d]" )
{
    item gun( itype_test_multimag_gun_consume );
    CHECK( gun.get_gun_energy_drain() == 0_kJ );
    item rock( itype_id( "rock" ) );
    CHECK( rock.get_gun_energy_drain() == 0_kJ );
}

// CDF: CDF-only test (no DDA counterpart).
TEST_CASE( "get_gun_energy_drain_sums_gunmod_multiplier_and_modifier",
           "[p3][firing][energy][8-1d]" )
{
    item gun( itype_test_energy_gun );
    REQUIRE( gun.get_gun_energy_drain() == 5_kJ );
    REQUIRE( gun.put_in( item( itype_test_energy_gunmod ), pocket_type::MOD ).success() );
    // 5 kJ * 2.0 + 1 kJ
    CHECK( gun.get_gun_energy_drain() == 11_kJ );
}

// CDF: CDF-only test (no DDA counterpart). Uses p3_multimag_gun_consume: the
// TEST_DATA gun has explicit pocket_data and so no MOD pocket for the gunmod.
TEST_CASE( "effective_qty_battery_well_energy_modifier", "[p3][firing][energy][8-1d]" )
{
    item gun( itype_p3_multimag_gun_consume );
    REQUIRE( gun.put_in( item( itype_test_energy_gunmod ), pocket_type::MOD ).success() );
    REQUIRE( gun.pocket_accepts_battery( gun.pocket_by_id( "power" ) ) );
    REQUIRE_FALSE( gun.pocket_accepts_battery( gun.pocket_by_id( "ammo" ) ) );

    pocket_consumption_entry e;
    e.pocket = "power";
    e.qty = 5;
    // 5 * 2.0 + 1 kJ
    CHECK( gun.effective_qty( e ) == 11 );

    e.pocket = "ammo";
    e.qty = 1;
    // The energy fields skip the primary pocket.
    CHECK( gun.effective_qty( e ) == 1 );
}

// CDF: CDF-only test (no DDA counterpart).
TEST_CASE( "format_consumption_requirements_energy_part", "[p3][firing][energy][8-1d]" )
{
    item gun( itype_test_energy_gun );
    REQUIRE_FALSE( gun.uses_firing_requirements() );
    const std::string one = gun.format_consumption_requirements( "", gun_mode_id( "DEFAULT" ), 1 );
    CHECK( one.find( ", 5 kJ" ) != std::string::npos );
    const std::string three = gun.format_consumption_requirements( "", gun_mode_id( "DEFAULT" ), 3 );
    CHECK( three.find( ", 15 kJ" ) != std::string::npos );

    item glock = make_loaded_glock();
    const std::string g = glock.format_consumption_requirements( "", gun_mode_id( "DEFAULT" ), 1 );
    CHECK( g.find( "kJ" ) == std::string::npos );
}

// CDF: CDF-only test (no DDA counterpart). No "enough energy" section (open question 1).
TEST_CASE( "consume_shots_legacy_energy_half", "[p3][firing][energy][8-1d]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero(); // CDF: zero is a function here
    item gun( itype_test_energy_gun );

    SECTION( "short energy returns 0" ) {
        item battery( itype_id( "battery_test" ) );
        REQUIRE( battery.mod_energy( 30_kJ ) == 0_J );
        REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( gun.energy_remaining( nullptr ) == 30_kJ );
        CHECK( gun.consume_shots( gun_mode_id( "DEFAULT" ), 7, here, pos, nullptr ) == 0 );
    }

    SECTION( "empty well returns 0" ) {
        CHECK( gun.consume_shots( gun_mode_id( "DEFAULT" ), 1, here, pos, nullptr ) == 0 );
    }
}

// CDF: CDF-only test (no DDA counterpart).
TEST_CASE( "needs_charges_to_use_true_for_energy_only_gun", "[p3][firing][energy][8-1d]" )
{
    item gun( itype_test_energy_gun );
    REQUIRE( gun.ammo_required() == 0 );
    REQUIRE( gun.get_gun_ups_drain() == 0 );
    REQUIRE_FALSE( gun.uses_firing_requirements() );
    CHECK( gun.needs_charges_to_use() );
}

static itype energy_p3_gun( const std::vector<std::string> &modes )
{
    itype t;
    t.gun = cata::make_value<islot_gun>();
    for( const std::string &m : modes ) {
        t.gun->modes.emplace( gun_mode_id( m ), gun_modifier_data( m, 1, {} ) );
    }
    return t;
}

static void energy_add_requirement( firing_requirement_set &fr, const std::string &mode,
                                    const std::string &pocket, int qty )
{
    pocket_consumption_entry entry;
    entry.pocket = pocket;
    entry.qty = qty;
    fr.per_mode[gun_mode_id( mode )].push_back( entry );
}

// CDF: CDF-only test (no DDA counterpart).
TEST_CASE( "check_firing_requirements_rejects_energy_drain_with_firing_requirements",
           "[p3][firing][energy][8-1d]" )
{
    itype t = energy_p3_gun( { "DEFAULT" } );
    energy_add_requirement( t.firing_requirements, "DEFAULT", "power", 1 );

    t.gun->energy_drain = 5_kJ;
    CHECK( check_firing_requirements( t ).find(
               "firing_requirements and non-zero energy_drain are mutually exclusive" )
           != std::string::npos );

    t.gun->energy_drain = 0_kJ;
    CHECK( check_firing_requirements( t ).find( "energy_drain" ) == std::string::npos );
}

// DDA tests/multimag_test.cpp:1872-1893.
TEST_CASE( "uses_energy_and_is_chargeable_across_multiple_wells", "[multimag][consume][8-1d]" )
{
    SECTION( "multimag tool with battery + non-battery wells reports energy use" ) {
        // test_multimag_gun_consume has glockmag (9mm) + heavy_battery_cell
        // wells; verify uses_energy detects the battery-flavored well.
        item gun = make_multimag_consume_gun();
        CHECK( gun.uses_energy() );
    }

    SECTION( "is_chargeable considers only the battery-flavored capacity" ) {
        // Battery well empty, projectile well full. Aggregate ammo_remaining
        // would include the projectile count and falsely match capacity.
        item gun( itype_test_multimag_gun_consume );
        item bb_mag( itype_id( "glockmag" ) );
        bb_mag.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
        item batt_mag( itype_id( "heavy_battery_cell" ) );
        REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
        CHECK( gun.is_chargeable() );
    }
}

// CDF: tests the shared helper (item_funcs::use_charges_via_tool_uses) used by
// item::use_charges and Character::use_charges (P3-8-4f S3).
TEST_CASE( "use_charges_via_tool_uses_bills_whole_uses", "[p3][firing]" )
{
    clear_map();
    tripoint_bub_ms pos( tripoint_bub_ms::zero() );

    item tool( itype_id( "test_multimag_tool_factor" ) );
    item mag_a( itype_id( "glockmag" ) );
    mag_a.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    REQUIRE( tool.put_in( mag_a, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool.uses_firing_requirements() );

    SECTION( "divisible" ) {
        CHECK( item_funcs::use_charges_via_tool_uses( tool, 10, pos, nullptr ) == 10 );
        CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 13 );
    }
    SECTION( "not a multiple" ) {
        int got = -1;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            got = item_funcs::use_charges_via_tool_uses( tool, 7, pos, nullptr );
        } );
        CHECK( got == 0 );
        CHECK( dmsg.find( "factor" ) != std::string::npos );
        CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 15 );
    }
}
