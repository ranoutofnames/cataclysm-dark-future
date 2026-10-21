#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

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
