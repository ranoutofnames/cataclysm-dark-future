#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "itype.h"
#include "player_helpers.h"
#include "recipe.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"

// P3-4-c-Jc2: guards for the two facts behind the "no edit" rows in crafting, recipe and
// profession. A tool keeps its rounds in an integral MAGAZINE pocket, so `charges` stays 0 and
// a crafted tool comes out with its default load. Tool amounts in crafting come from
// charges_of, which is pocket-aware.

// Guard K1: crafting a tool does not unload it. BN wrote `charges = 0` for every result without
// craft charges; with the pocket model that line is a no-op, as it is in DDA (recipe.cpp:1167-1226).
TEST_CASE( "crafted_pocket_tool_has_the_default_spawn_load", "[p3][charges][crafting]" ) {
    clear_all_state();
    const recipe_id lantern_recipe( "gasoline_lantern" );
    REQUIRE( lantern_recipe.is_valid() );
    const recipe &rec = lantern_recipe.obj();
    REQUIRE( rec.result() == itype_id( "gasoline_lantern" ) );

    auto crafted = rec.create_result();
    // CDF: gasoline_lantern has initial_charges 500 in a MAGAZINE pocket
    // (data/json/items/tool/lighting.json).
    auto default_load = item::spawn( itype_id( "gasoline_lantern" ), calendar::turn,
                                     item::default_charges_tag{} );
    REQUIRE( default_load->ammo_remaining() == 500 );

    // The unload in recipe::create_result() only clears `charges`; the pocket keeps its ammo.
    CHECK( crafted->charges == 0 );
    CHECK( crafted->ammo_remaining() == default_load->ammo_remaining() );
}

// Guard K2: the crafting inventory counts a tool's pocket ammo, not its (always 0) `charges`
// (visitable.cpp:1392, the comment names DDA visitable.cpp:1412-1414).
TEST_CASE( "crafting_inventory_counts_pocket_ammo_of_a_tool", "[p3][charges][crafting]" ) {
    clear_all_state();
    auto &u = get_avatar();
    clear_avatar();
    const itype_id lantern_id( "gasoline_lantern" );
    item &lantern = u.i_add( item::spawn( lantern_id, calendar::turn ) );
    lantern.ammo_set( itype_id( "gasoline" ), 120 );
    REQUIRE( lantern.charges == 0 );
    REQUIRE( lantern.ammo_remaining() == 120 );

    // The cached crafting inventory holds its own copies, so refresh it before each read.
    u.invalidate_crafting_inventory();
    CHECK( u.crafting_inventory().charges_of( lantern_id, 500 ) == 120 );

    SECTION( "unloaded tool contributes nothing" ) {
        lantern.ammo_unset();
        REQUIRE( lantern.ammo_remaining() == 0 );
        u.invalidate_crafting_inventory();
        CHECK( u.crafting_inventory().charges_of( lantern_id, 500 ) == 0 );
    }
}
