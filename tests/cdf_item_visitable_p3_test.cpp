#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "debug_log_capture.h"
#include "flag.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map/map_selector.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "units.h"

#include <string>
#include <utility>
#include <vector>

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
    // CDF: LINKUP1c. hotplate is a cable tool now (no battery pocket), so the charges-of-a-well
    // exemplar is TEST_DATA/test_battery_tool.
    detached_ptr<item> battery_tool = item::spawn( itype_id( "test_battery_tool" ),
                                   calendar::start_of_cataclysm, 7 );
    REQUIRE( battery_tool->is_tool() );
    REQUIRE_FALSE( battery_tool->has_flag( flag_USES_BIONIC_POWER ) );

    get_avatar().set_max_power_level( 100_kJ );
    get_avatar().set_power_level( 50_kJ );

    CHECK( battery_tool->charges_of( itype_id( "test_battery_tool" ) ) == 7 );
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

// DDA tests/visitable_remove_test.cpp:514-524 (charges_of with in_tools).
// CDF: lighter ammo is chem_butane here (DDA: butane); backpack via wear_backpack
TEST_CASE( "charges_of_in_tools_counts_ammo_loaded_in_a_tool", "[p3][visitable]" )
{
    clear_all_state();
    clear_avatar();
    Character &guy = get_avatar();
    wear_backpack( guy );

    const itype_id fluid( "chem_butane" );
    item &lighter = guy.i_add( tool_with_ammo( itype_id( "lighter" ), 10 ) );
    REQUIRE( lighter.ammo_current() == fluid );
    REQUIRE( guy.has_amount( itype_id( "lighter" ), 1 ) );

    CHECK( guy.charges_of( fluid, INT_MAX, return_true<item>, nullptr, true ) == 10 );
    CHECK( guy.charges_of( fluid, INT_MAX, return_true<item>, nullptr, false ) == 0 );

    SECTION( "crafting inventory" ) {
        guy.invalidate_crafting_inventory();
        const temp_crafting_inventory &crafting = guy.crafting_inventory();
        CHECK( crafting.charges_of( fluid, INT_MAX, return_true<item>, nullptr, true ) == 10 );
        CHECK( crafting.charges_of( fluid, INT_MAX, return_true<item>, nullptr, false ) == 0 );
    }
}

// DDA visitable.cpp:1111-1118, 1263-1268: the raw IS_UPS sum (no ups_eff_mult) for generic visitables.
TEST_CASE( "charges_of_ups_counts_raw_is_ups_items_on_the_ground", "[p3][visitable]" )
{
    clear_all_state();
    clear_map();
    clear_avatar();
    const tripoint_bub_ms pos = get_avatar().bub_pos();
    REQUIRE( !get_map().add_item_or_charges( pos, tool_with_ammo( itype_id( "UPS_off" ), 40 ) ) );

    CHECK( map_cursor( pos ).charges_of( itype_id( "UPS" ) ) == 40 );
    CHECK( map_cursor( pos ).charges_of( itype_id( "UPS" ), 15 ) == 15 );
    CHECK( map_cursor( pos ).charges_of( itype_id( "UPS" ), INT_MAX, []( const item & ) {
        return false;
    } ) == 0 );
}

// DDA item.cpp:4013 / character.h:3146 (use_charges with in_tools).
// CDF: lighter ammo is chem_butane here (DDA: butane); no radius overload in CDF.
TEST_CASE( "use_charges_in_tools_drains_ammo_loaded_in_a_tool", "[p3][visitable]" )
{
    clear_all_state();
    clear_avatar();
    Character &guy = get_avatar();
    wear_backpack( guy );

    const itype_id fluid( "chem_butane" );
    item &lighter = guy.i_add( tool_with_ammo( itype_id( "lighter" ), 10 ) );
    REQUIRE( lighter.ammo_current() == fluid );

    CHECK( guy.use_charges( fluid, 4 ).empty() );
    CHECK( lighter.ammo_remaining() == 10 );

    const auto used = guy.use_charges( fluid, 4, return_true<item>, true );
    CHECK( used.size() == 1 );
    CHECK( lighter.ammo_remaining() == 6 );
}

TEST_CASE( "item_use_charges_in_tools_matches_ammo_current", "[p3][visitable]" )
{
    clear_all_state();
    const itype_id fluid( "chem_butane" );
    detached_ptr<item> lighter = tool_with_ammo( itype_id( "lighter" ), 10 );
    item &obj = *lighter;
    REQUIRE( obj.ammo_current() == fluid );

    int qty = 4;
    std::vector<detached_ptr<item>> used;
    const tripoint_bub_ms pos( tripoint_bub_ms::zero() );

    // CDF: only the in_tools=true path is asserted at item level. DDA does not drain a
    // non-matching tool's ammo with in_tools=false either (item.cpp:4012-4013 match gate,
    // non-match returns NEXT at 4041), so the no-in_tools Character-level case is covered by
    // use_charges_in_tools_drains_ammo_loaded_in_a_tool above (plan S4).
    lighter = item::use_charges( std::move( lighter ), fluid, qty, used, pos, return_true<item>,
                                 true );
    CHECK( qty == 0 );
    CHECK( obj.ammo_remaining() == 6 );
}

// DDA tests/multimag_test.cpp:1958-1975.
// CDF: Character::use_charges returns detached_ptrs; DDA calls item::use_charges via item_location.
TEST_CASE( "Character_use_charges_drains_multimag_tool_via_consume_tool_uses", "[p3][firing]" )
{
    clear_all_state();
    clear_avatar();
    Character &chr = get_avatar();
    item &tool = chr.i_add( make_multimag_consume_tool( 12, 30 ) );
    REQUIRE( tool.uses_firing_requirements() );

    // factor=1, well_a qty=2, well_b qty=1, 1 use -> 2 + 1 drain
    std::vector<detached_ptr<item>> used = chr.use_charges( itype_id( "test_multimag_tool_consume" ),
                                           1 );
    CHECK( used.size() == 1 );
    CHECK( tool.ammo_remaining_in_pocket( "well_a" ) == 10 );
    CHECK( tool.ammo_remaining_in_pocket( "well_b" ) == 29 );
}

// CDF-only regression: DDA's Character::use_charges visits every item and calls item::use_charges
// on each, which itself descends; ported literally that bills a multimag tool more than once.
// Here a tool at top level and one inside a worn container must be billed exactly one use in total.
TEST_CASE( "Character_use_charges_bills_each_multimag_tool_once", "[p3][firing]" )
{
    clear_all_state();
    clear_avatar();
    Character &chr = get_avatar();
    item &tool_a = chr.i_add( make_multimag_consume_tool( 12, 30 ) );
    detached_ptr<item> tool_b_owned = make_multimag_consume_tool( 12, 30 );
    item &tool_b = *tool_b_owned;
    detached_ptr<item> pack = item::spawn( itype_id( "backpack" ) );
    REQUIRE( pack->put_in( std::move( tool_b_owned ), pocket_type::CONTAINER ).success() );
    chr.wear_item( std::move( pack ), false );
    REQUIRE( chr.amount_of( itype_id( "test_multimag_tool_consume" ) ) == 2 );

    std::vector<detached_ptr<item>> used = chr.use_charges( itype_id( "test_multimag_tool_consume" ),
                                           1 );
    CHECK( used.size() == 1 );
    const int well_a = tool_a.ammo_remaining_in_pocket( "well_a" ) +
                       tool_b.ammo_remaining_in_pocket( "well_a" );
    const int well_b = tool_a.ammo_remaining_in_pocket( "well_b" ) +
                       tool_b.ammo_remaining_in_pocket( "well_b" );
    CHECK( well_a == 12 + 10 );
    CHECK( well_b == 30 + 29 );
}

// Character-path counterpart of legacy_charges_per_use_factor_non_divisible_hard_fails
// (DDA multimag_test.cpp:1847-1870). CDF-only. The guard message must appear exactly once.
TEST_CASE( "Character_use_charges_multimag_non_divisible_hard_fails", "[p3][firing]" )
{
    clear_all_state();
    clear_avatar();
    Character &chr = get_avatar();
    detached_ptr<item> tool = item::spawn( itype_id( "test_multimag_tool_factor" ) );
    detached_ptr<item> mag_a = item::spawn( itype_id( "glockmag" ) );
    mag_a->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    REQUIRE( tool->put_in( std::move( mag_a ), pocket_type::MAGAZINE_WELL ).success() );
    item &carried = chr.i_add( std::move( tool ) );
    REQUIRE( carried.uses_firing_requirements() );

    std::vector<detached_ptr<item>> used;
    const std::string captured = capture_debugmsg_during( [&]() {
        used = chr.use_charges( itype_id( "test_multimag_tool_factor" ), 7 ); // factor=5
    } );
    size_t count = 0;
    for( size_t pos = captured.find( "not a multiple" ); pos != std::string::npos;
         pos = captured.find( "not a multiple", pos + 1 ) ) {
        count++;
    }
    CHECK( captured.find( "factor" ) != std::string::npos );
    CHECK( count == 1 );
    CHECK( used.empty() );
    CHECK( carried.ammo_remaining_in_pocket( "well_a" ) == 15 );
}

// CDF-only guard (S6 fix round): a gun is not skipped by Character::use_charges. Ammo inside a
// gun's magazine must still be drained (base returned NEXT for guns; DDA item.cpp:4041 too).
TEST_CASE( "Character_use_charges_descends_into_gun_magazine", "[p3][visitable]" )
{
    clear_all_state();
    clear_avatar();
    Character &guy = get_avatar();
    wear_backpack( guy );

    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ) );
    gun->put_in( std::move( mag ) );
    item &carried = guy.i_add( std::move( gun ) );
    REQUIRE( carried.ammo_remaining() == 30 );

    const auto used = guy.use_charges( itype_id( "556" ), 10 );
    CHECK( !used.empty() );
    CHECK( carried.ammo_remaining() == 20 );
}

// CDF (CAP1d): a loaded tool's ammo is visible only to the tool-aware lookups, wherever the tool sits:
// a carry pocket, a holster pocket of a backpack (strap) or a real holster. DDA item.cpp:4013.
namespace
{
enum class tool_place { carry, strap, holster };

auto place_loaded_lighter( Character &guy, tool_place where ) -> item *
{
    detached_ptr<item> det = tool_with_ammo( itype_id( "lighter" ), 10 );
    if( where == tool_place::carry ) {
        wear_backpack( guy );
        return &guy.i_add( std::move( det ) );
    }
    detached_ptr<item> holder = item::spawn( itype_id( where == tool_place::strap ? "backpack" : "holster" ) );
    item &worn = *holder;
    guy.wear_item( std::move( holder ), false );
    for( item_pocket &pocket : worn.contents.get_pockets() ) {
        if( pocket.is_holster() && pocket.is_type( pocket_type::CONTAINER ) ) {
            // CDF: add() skips the holster's item restrictions; the lookups, not the pocket rules, are tested
            item *placed = nullptr;
            pocket.add( std::move( det ), &placed );
            return placed;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE( "loaded_tool_ammo_stays_hidden_from_plain_lookups_in_any_pocket", "[p3][visitable]" )
{
    const itype_id fluid( "chem_butane" );
    const auto where = GENERATE( tool_place::carry, tool_place::strap, tool_place::holster );
    clear_all_state();
    clear_avatar();
    Character &guy = get_avatar();
    item *lighter = place_loaded_lighter( guy, where );
    REQUIRE( lighter != nullptr );
    REQUIRE( lighter->ammo_remaining() == 10 );

    SECTION( "charges_of" ) {
        CHECK( guy.charges_of( fluid, INT_MAX, return_true<item>, nullptr, false ) == 0 );
        CHECK( guy.charges_of( fluid, INT_MAX, return_true<item>, nullptr, true ) == 10 );
    }
    SECTION( "amount_of of the tool itself" ) {
        CHECK( guy.has_amount( itype_id( "lighter" ), 1 ) );
        CHECK( guy.amount_of( itype_id( "lighter" ) ) == 1 );
    }
    SECTION( "remove_items_with honours SKIP on a tool" ) {
        int seen_fluid = 0;
        guy.remove_items_with( [&]( detached_ptr<item> &&e ) {
            if( e->typeId() == fluid ) {
                seen_fluid++;
            }
            return e->is_tool() ? VisitResponse::SKIP : VisitResponse::NEXT;
        } );
        CHECK( seen_fluid == 0 );
        CHECK( lighter->ammo_remaining() == 10 );
    }
    SECTION( "use_charges, in_tools false then true" ) {
        CHECK( guy.use_charges( fluid, 4 ).empty() );
        CHECK( lighter->ammo_remaining() == 10 );
        const auto used = guy.use_charges( fluid, 4, return_true<item>, true );
        CHECK( used.size() == 1 );
        CHECK( lighter->ammo_remaining() == 6 );
    }
}
