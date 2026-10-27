#include "avatar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_action.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <string>

namespace {

// Spawns a battery-loaded test tool and hands it to the avatar; returns the carried item.
auto carry_loaded_tool( const char *tool_id, int charges ) -> item * {
    detached_ptr<item> tool = item::spawn( itype_id( tool_id ) );
    detached_ptr<item> battery = item::spawn( itype_id( "light_battery_cell" ) );
    battery->ammo_unset();
    item *bat = &*battery;
    tool->put_in( std::move( battery ) );
    bat->ammo_set( itype_id( "battery" ), charges );
    item &carried = get_avatar().i_add( std::move( tool ) );
    return &carried;
}

auto pick_for_crowbar() -> item * {
    const item_action_map map = item_action_generator::generator().map_actions_to_items( get_avatar() );
    const auto it = map.find( "CROWBAR" );
    return it == map.end() ? nullptr : it->second;
}

} // namespace

// DDA tests/multimag_test.cpp item_action_comparator_*; DDA item_action.cpp:186-205
// CDF: both fixtures need the same ammo_required so that only expected_cost_per_use separates them
// CDF: DDA's rule only replaces a candidate when it holds fewer remaining uses, so the cheaper
// tool wins outright only when it is seen first, or when it holds fewer charges than the dearer one.
TEST_CASE( "item_action_comparator_prefers_lower_expected_cost", "[p3][item_action]" ) {
    clear_avatar();
    const int req = item::spawn( itype_id( "test_ammo_scale_plain_tool" ) )->ammo_required();
    REQUIRE( req > 0 );
    // CDF: fixture CROWBAR ammo_scale: 2 (scaled) vs 1 (plain), so costs are 2*req vs req
    SECTION( "cheaper tool seen first, dearer tool holds fewer charges" ) {
        // The old ammo_remaining() rule would replace plain with the scaled tool here.
        item *plain = carry_loaded_tool( "test_ammo_scale_plain_tool", 10 * req );
        item *scaled = carry_loaded_tool( "test_ammo_scale_tool", 2 * req );
        REQUIRE( scaled->expected_cost_per_use( "CROWBAR" ) == 2 * req );
        REQUIRE( plain->expected_cost_per_use( "CROWBAR" ) == req );
        CHECK( pick_for_crowbar() == plain );
    }
    SECTION( "dearer tool seen first, cheaper tool holds fewer charges" ) {
        item *scaled = carry_loaded_tool( "test_ammo_scale_tool", 10 * req );
        item *plain = carry_loaded_tool( "test_ammo_scale_plain_tool", 2 * req );
        REQUIRE( scaled->ammo_required() == plain->ammo_required() );
        CHECK( pick_for_crowbar() == plain );
    }
}

TEST_CASE( "item_action_comparator_equal_cost_prefers_fewer_remaining", "[p3][item_action]" ) {
    for( const bool low_first : { true, false } ) {
        clear_avatar();
        const int req = item::spawn( itype_id( "test_ammo_scale_plain_tool" ) )->ammo_required();
        REQUIRE( req > 0 );
        item *low = nullptr;
        item *high = nullptr;
        if( low_first ) {
            low = carry_loaded_tool( "test_ammo_scale_plain_tool", 3 * req );
            high = carry_loaded_tool( "test_ammo_scale_plain_tool", 9 * req );
        } else {
            high = carry_loaded_tool( "test_ammo_scale_plain_tool", 9 * req );
            low = carry_loaded_tool( "test_ammo_scale_plain_tool", 3 * req );
        }
        // CDF: equal cost and ammo_required, so the tie-break is ammo_remaining (legacy tool)
        REQUIRE( low->ammo_remaining() == 3 * req );
        REQUIRE( high->ammo_remaining() == 9 * req );
        CHECK( pick_for_crowbar() == low );
    }
}
