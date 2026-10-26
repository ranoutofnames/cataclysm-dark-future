#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "units.h"

#include <utility>

// 8-3a (OD-2): a legacy USES_BIONIC_POWER tool pulls the avatar's bionic power
// into the charges_of count (DDA visitable.cpp:1190-1193).
TEST_CASE( "cdf_visitable_legacy_bionic_pool_counts_power", "[p3][visitable]" ) {
    clear_all_state();
    // CDF: toolset is the USES_BIONIC_POWER tool with no ammo type and no
    // magazine pocket (data/json/items/fake.json:31-39); it spawns with 0 charges.
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    REQUIRE( toolset->has_flag( flag_USES_BIONIC_POWER ) );
    toolset->charges = 0;

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 50_kJ );

    CHECK( toolset->charges_of( itype_id( "toolset" ) ) == 50 );
    CHECK( toolset->charges_of( itype_id( "toolset" ), 20 ) == 20 );
}

// 8-3a guard: a tool without USES_BIONIC_POWER keeps counting only its own
// charges even when the avatar has bionic power.
TEST_CASE( "cdf_visitable_legacy_tool_without_bionic_flag_ignores_power", "[p3][visitable]" ) {
    clear_all_state();
    detached_ptr<item> hotplate = item::spawn( itype_id( "hotplate" ),
                             calendar::start_of_cataclysm, 7 );
    REQUIRE( hotplate->is_tool() );
    REQUIRE_FALSE( hotplate->has_flag( flag_USES_BIONIC_POWER ) );

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 50_kJ );

    CHECK( hotplate->charges_of( itype_id( "hotplate" ) ) == 7 );
}

// 8-3a guard: two USE_UPS tools share one UPS and the UPS is billed once
// (DDA visitable.cpp:1194-1200; CDF keeps the charges_of( itype_UPS ) call).
TEST_CASE( "cdf_visitable_legacy_ups_pool_billed_once", "[p3][visitable]" ) {
    clear_all_state();
    clear_avatar();
    Character &chr = get_player_character();
    // CDF: USE_UPS tool (data/json/items/tool/electronics.json:173) with no charges.
    chr.i_add( item::spawn( itype_id( "wrist_comp_budget" ), calendar::start_of_cataclysm, 0 ) );
    chr.i_add( item::spawn( itype_id( "wrist_comp_budget" ), calendar::start_of_cataclysm, 0 ) );
    // CDF: UPS_off carries IS_UPS (data/json/items/tool/electronics.json:904).
    chr.i_add( item::spawn( itype_id( "UPS_off" ), calendar::start_of_cataclysm, 500 ) );

    CHECK( chr.charges_of( itype_id( "wrist_comp_budget" ), 1000 ) == 500 );
    CHECK( chr.charges_of( itype_id( "wrist_comp_budget" ), 100 ) == 100 );
}

namespace {

// CDF: DDA multimag_test.cpp:1279-1289 as tests/cdf_item_ammo_p3_test.cpp:76 has it; detached_ptr instead of a value item
auto make_multimag_consume_tool( int well_a_charges = 12, int well_b_charges = 30 ) -> detached_ptr<item> {
    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_tool_consume" ) );
    detached_ptr<item> mag_a = item::spawn( itype_id( "glockmag" ) );
    mag_a->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, well_a_charges ),
                   pocket_type::MAGAZINE );
    detached_ptr<item> mag_b = item::spawn( itype_id( "stanag30" ) );
    mag_b->put_in( item::spawn( itype_id( "556" ), calendar::turn, well_b_charges ),
                   pocket_type::MAGAZINE );
    REQUIRE( tool->put_in( std::move( mag_a ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool->put_in( std::move( mag_b ), pocket_type::MAGAZINE_WELL ).success() );
    return tool;
}

// CDF: DDA multimag_test.cpp:1294-1309; detached_ptr, plain ids; an empty cell stays empty instead of
// receiving a 0-charge battery
auto make_multimag_welder( int batt_charges, int oxygen_qty, int rod_qty ) -> detached_ptr<item> {
    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_vehicle_welder" ) );
    detached_ptr<item> batt_mag = item::spawn( itype_id( "heavy_battery_cell" ) );
    if( batt_charges > 0 ) {
        batt_mag->put_in( item::spawn( itype_id( "battery" ), calendar::turn, batt_charges ),
                          pocket_type::MAGAZINE );
    }
    REQUIRE( tool->put_in( std::move( batt_mag ), pocket_type::MAGAZINE_WELL ).success() );
    if( oxygen_qty > 0 ) {
        REQUIRE( tool->put_in( item::spawn( itype_id( "oxygen" ), calendar::turn, oxygen_qty ),
                               pocket_type::MAGAZINE ).success() );
    }
    if( rod_qty > 0 ) {
        REQUIRE( tool->put_in( item::spawn( itype_id( "welding_wire_steel_tests_dupe" ), calendar::turn,
                                            rod_qty ), pocket_type::MAGAZINE ).success() );
    }
    return tool;
}

// CDF: DDA wears a backpack with chr.worn.wear_item( chr, item( itype_backpack ), false, false )
void wear_backpack( Character &chr ) {
    chr.wear_item( item::spawn( itype_id( "backpack" ) ), false );
}

} // namespace

// DDA tests/multimag_test.cpp:1978-2048; the four DDA SECTIONs plus two CDF-only sections.
TEST_CASE( "charges_of_for_multimag_tool_reports_local_uses", "[p3][visitable]" ) {
    clear_all_state();
    clear_avatar();
    Character &chr = get_player_character();

    SECTION( "no external pool: local-only count" ) {
        item &carried = chr.i_add( make_multimag_consume_tool( 12, 30 ) );
        REQUIRE( carried.uses_firing_requirements() );
        // CDF: test_multimag_tool_consume consumption_per_use well_a qty 2,
        // well_b qty 1 (data/mods/TEST_DATA/items.json:1895); 12 9mm in glockmag
        // gives 12 / 2 = 6, 30 556 in stanag30 gives 30 / 1 = 30; min 6.
        CHECK( chr.charges_of( itype_id( "test_multimag_tool_consume" ) ) == 6 );
    }

    SECTION( "routes external UPS pool when item has USE_UPS + battery pocket" ) {
        // CDF: welder consumption_per_use power 5, oxygen 2, rod 1
        // (data/mods/TEST_DATA/items.json:1988). oxygen 10 / 2 = 5,
        // rod 5 / 1 = 5, power (100 UPS + 0 local) / 5 = 20; min 5.
        wear_backpack( chr );
        item &welder = chr.i_add( make_multimag_welder( 0, 10, 5 ) );
        REQUIRE( welder.uses_firing_requirements() );
        REQUIRE( chr.amount_of( itype_id( "test_multimag_vehicle_welder" ) ) == 1 );

        // CDF: UPS_ON absent; UPS_off carries IS_UPS and spawn charges fill its
        // magazine, as in the 8-3a case.
        chr.i_add( item::spawn( itype_id( "UPS_off" ), calendar::start_of_cataclysm, 100 ) );
        // CDF: Character::available_ups is absent; the same fact read through
        // charges_of( UPS ), which counts IS_UPS charges.
        REQUIRE( chr.charges_of( itype_id( "UPS" ) ) == 100 );

        CHECK( chr.charges_of( itype_id( "test_multimag_vehicle_welder" ) ) == 5 );
    }

    SECTION( "routes external UPS pool from ground via crafting_inventory" ) {
        // CDF: the same welder; UPS 100 on the ground, not in the character.
        clear_map();
        wear_backpack( chr );
        item &welder = chr.i_add( make_multimag_welder( 0, 10, 5 ) );
        REQUIRE( welder.uses_firing_requirements() );
        REQUIRE( chr.amount_of( itype_id( "test_multimag_vehicle_welder" ) ) == 1 );
        REQUIRE( chr.charges_of( itype_id( "UPS" ) ) == 0 );

        REQUIRE( !get_map().add_item_or_charges( chr.bub_pos(),
                 item::spawn( itype_id( "UPS_off" ), calendar::start_of_cataclysm, 100 ) ) );
        chr.invalidate_crafting_inventory();
        const temp_crafting_inventory &crafting = chr.crafting_inventory();
        // CDF: the ground UPS is seen by the crafting inventory (the only main
        // that sees it), so apply_external_pools_multimag must query main.
        REQUIRE( crafting.charges_of( itype_id( "UPS" ) ) == 100 );
        CHECK( crafting.charges_of( itype_id( "test_multimag_vehicle_welder" ) ) == 5 );
    }

    SECTION( "inventory dedup: two multimag USE_UPS welders share one UPS" ) {
        // CDF: two welders (5, 6, 3): oxygen 6 / 2 = 3, rod 3 / 1 = 3,
        // battery 5; UPS 10. Welder 1: 3 uses, bill 3 x 5 - 5 = 10 -> pool 0;
        // welder 2: battery 5 / 5 = 1 use; 3 + 1 = 4 (without dedup 3 + 3 = 6).
        wear_backpack( chr );
        REQUIRE( chr.i_add( make_multimag_welder( 5, 6, 3 ) ).uses_firing_requirements() );
        REQUIRE( chr.amount_of( itype_id( "test_multimag_vehicle_welder" ) ) == 1 );
        REQUIRE( chr.i_add( make_multimag_welder( 5, 6, 3 ) ).uses_firing_requirements() );
        REQUIRE( chr.amount_of( itype_id( "test_multimag_vehicle_welder" ) ) == 2 );

        chr.i_add( item::spawn( itype_id( "UPS_off" ), calendar::start_of_cataclysm, 10 ) );
        REQUIRE( chr.charges_of( itype_id( "UPS" ) ) == 10 );

        CHECK( chr.charges_of( itype_id( "test_multimag_vehicle_welder" ) ) == 4 );
    }

    SECTION( "CDF: empty second well reports zero uses" ) {
        item &carried = chr.i_add( make_multimag_consume_tool( 12, 0 ) );
        REQUIRE( carried.uses_firing_requirements() );
        // CDF: non-fungible pockets, DDA 1364-1371 semantics; well_b 0 / 1
        // caps the count at 0 (a legacy sum would give 12).
        CHECK( chr.charges_of( itype_id( "test_multimag_tool_consume" ) ) == 0 );
    }

    SECTION( "CDF: UPS pool size limits the count" ) {
        wear_backpack( chr );
        item &welder = chr.i_add( make_multimag_welder( 0, 10, 5 ) );
        REQUIRE( welder.uses_firing_requirements() );
        REQUIRE( chr.amount_of( itype_id( "test_multimag_vehicle_welder" ) ) == 1 );
        chr.i_add( item::spawn( itype_id( "UPS_off" ), calendar::start_of_cataclysm, 10 ) );
        REQUIRE( chr.charges_of( itype_id( "UPS" ) ) == 10 );
        // CDF: power 10 / 5 = 2 caps before oxygen (5) and rod (5).
        CHECK( chr.charges_of( itype_id( "test_multimag_vehicle_welder" ) ) == 2 );
    }
}

TEST_CASE( "cdf_visitable_multimag_bionic_pool_counts_power", "[p3][visitable]" ) {
    clear_all_state();
    clear_avatar();
    Character &chr = get_player_character();
    wear_backpack( chr );
    detached_ptr<item> welder = make_multimag_welder( 0, 10, 5 );
    // CDF: runtime flag instead of a new TEST_DATA fixture, items.json is a helper lock file
    welder->set_flag( flag_USES_BIONIC_POWER );
    item &carried = chr.i_add( std::move( welder ) );
    REQUIRE( carried.uses_firing_requirements() );
    REQUIRE( chr.amount_of( itype_id( "test_multimag_vehicle_welder" ) ) == 1 );

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 10_kJ );
    // CDF: any_bionic pool 10 kJ + UPS 0 = 10; power qty 5 gives 2
    // (DDA 1217-1220 via apply_external_pools_multimag).
    CHECK( chr.charges_of( itype_id( "test_multimag_vehicle_welder" ) ) == 2 );

    get_avatar().set_power_level( 0_kJ );
    CHECK( chr.charges_of( itype_id( "test_multimag_vehicle_welder" ) ) == 0 );
}

TEST_CASE( "cdf_visitable_multimag_limit_caps_report", "[p3][visitable]" ) {
    clear_all_state();
    clear_avatar();
    Character &chr = get_player_character();
    item &tool = chr.i_add( make_multimag_consume_tool( 12, 30 ) );
    REQUIRE( tool.uses_firing_requirements() );
    CHECK( chr.charges_of( itype_id( "test_multimag_tool_consume" ), 3 ) == 3 );
    CHECK( chr.charges_of( itype_id( "test_multimag_tool_consume" ) ) == 6 );

    // CDF: the two welders from the dedup section; the first welder's 3 uses
    // reach the limit, so the loop breaks before the second is billed.
    wear_backpack( chr );
    REQUIRE( chr.i_add( make_multimag_welder( 5, 6, 3 ) ).uses_firing_requirements() );
    REQUIRE( chr.i_add( make_multimag_welder( 5, 6, 3 ) ).uses_firing_requirements() );
    REQUIRE( chr.amount_of( itype_id( "test_multimag_vehicle_welder" ) ) == 2 );
    chr.i_add( item::spawn( itype_id( "UPS_off" ), calendar::start_of_cataclysm, 10 ) );
    REQUIRE( chr.charges_of( itype_id( "UPS" ) ) == 10 );

    CHECK( chr.charges_of( itype_id( "test_multimag_vehicle_welder" ), 2 ) == 2 );
    CHECK( chr.charges_of( itype_id( "test_multimag_vehicle_welder" ) ) == 4 );
}

TEST_CASE( "cdf_visitable_multimag_item_self_count", "[p3][visitable]" ) {
    clear_all_state();
    clear_avatar();
    detached_ptr<item> tool = make_multimag_consume_tool( 12, 30 );
    REQUIRE( tool->uses_firing_requirements() );
    // CDF: the visitable<item> instantiation: the tool is both root and main.
    CHECK( tool->charges_of( itype_id( "test_multimag_tool_consume" ) ) == 6 );
    CHECK( tool->charges_of( itype_id( "test_multimag_tool_consume" ), 3 ) == 3 );
}
