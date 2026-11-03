// CDF CAP1b: the carry volume strings and the trade pane/gate use carried_capacity(), the pair of
// carried_volume(), not the all-pocket volume_capacity() (holster pockets).
// Fixtures as carry_capacity_test.cpp; survivor_hiking_backpack only (CAP1c pending).
#include <cstdlib>
#include <string>
#include <utility>

#include "advanced_inv.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "character.h"
#include "color.h"
#include "item.h"
#include "npc.h"
#include "npctrade.h"
#include "output.h"
#include "panels.h"
#include "pickup.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "units_utility.h"

namespace
{

avatar &fresh_avatar()
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
    u.unset_mutation( trait_id( "PACKMULE" ) );
    u.unset_mutation( trait_id( "DISORGANIZED" ) );
    u.unset_mutation( trait_id( "HOARDER" ) );
    u.clear_morale();
    return u;
}

void wear( Character &c, const char *id )
{
    detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
    // CDF: wear_item returns the item back on failure and an empty pointer on success
    REQUIRE( !c.wear_item( std::move( it ), false ) );
}

void add_rocks( Character &c, int n )
{
    // CDF: TEST_DATA items.json: test_rock is 250 ml
    for( int i = 0; i < n; i++ ) {
        c.i_add( item::spawn( itype_id( "test_rock" ), calendar::turn ) );
    }
}

// CDF: storage.json: survivor_hiking_backpack has one 60 L CONTAINER (carry) pocket and two 2500 ml
// sheath holster pockets, so the carry sum is 60000 ml and the DDA sum 65000 ml
constexpr int pack_carry_ml = 60000;
constexpr int pack_dda_ml = 65000;

// CDF: the survivor pack (60 L carry pocket) is filled, then DISORGANIZED (packmule_modifier 0.6)
// takes the bar to 36 L: the load is 24 L over the bar with every item inside a pocket. (PACKMULE
// cannot do this: it raises the bar but not what a pocket physically holds. jerrypack is no use
// either: its pocket takes only items up to 65 ml.)
void overfill_with_disorganized( Character &c )
{
    add_rocks( c, ( pack_carry_ml - c.carried_volume().value() ) / 250 );
    REQUIRE( c.carried_volume() == pack_carry_ml * 1_ml );
    c.set_mutation( trait_id( "DISORGANIZED" ) );
    REQUIRE( std::abs( c.carried_capacity().value() - 36000 ) <= 2 );
    REQUIRE( c.carried_volume() > c.carried_capacity() );
}

std::string expected_volume_string( const Character &c )
{
    const double carried = round_up( convert_volume( to_milliliter( c.carried_volume() ) ), 2 );
    const double cap = round_up( convert_volume( to_milliliter( c.carried_capacity() ) ), 2 );
    return string_format( "%.2f/%.2f", carried, cap );
}

} // namespace

TEST_CASE( "cap1b_trade_pane_uses_the_carried_bar", "[cap1b][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    const npc_trading::pane_volume vol = npc_trading::trade_pane_volume( u, 0_ml );
    CHECK( vol.max == u.carried_capacity() );
    CHECK( vol.max == pack_carry_ml * 1_ml );
    CHECK( vol.max != pack_dda_ml * 1_ml );
    CHECK( vol.used == u.carried_volume() );
}

TEST_CASE( "cap1b_trade_pane_follows_the_selection", "[cap1b][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    add_rocks( u, 4 ); // 1000 ml
    REQUIRE( u.carried_volume() == 1000_ml );
    CHECK( npc_trading::trade_pane_volume( u, 2000_ml ).used == u.carried_volume() + 2000_ml );
    CHECK( npc_trading::trade_pane_volume( u, -1000_ml ).used == u.carried_volume() - 1000_ml );
}

TEST_CASE( "cap1b_carry_room_is_the_bar_minus_the_load", "[cap1b][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    CHECK( npc_trading::carry_room( u ) == u.carried_capacity() - u.carried_volume() );
    add_rocks( u, pack_carry_ml / 250 );
    REQUIRE( u.carried_volume() == u.carried_capacity() );
    CHECK( npc_trading::carry_room( u ) == 0_ml );
    // CDF: CAP1 C3: the holster room is still free in the DDA total, but it is no carry room
    CHECK( u.volume_capacity() - u.carried_volume() == 5000_ml );
}

TEST_CASE( "cap1b_carry_room_for_an_npc", "[cap1b][pocket]" )
{
    clear_all_state();
    standard_npc np( "trader" );
    clear_character( np, false );
    wear( np, "survivor_hiking_backpack" );
    REQUIRE( np.carried_capacity() == pack_carry_ml * 1_ml );
    add_rocks( np, 4 );
    CHECK( npc_trading::carry_room( np ) == pack_carry_ml * 1_ml - np.carried_volume() );
    CHECK( npc_trading::carry_room( np ) > 0_ml );
    overfill_with_disorganized( np );
    // the gate needs the sign: no floor at 0
    CHECK( npc_trading::carry_room( np ) < 0_ml );
    CHECK( npc_trading::carry_room( np ) == np.carried_capacity() - np.carried_volume() );
}

TEST_CASE( "cap1b_debug_storage_does_not_overflow", "[cap1b][pocket]" )
{
    avatar &u = fresh_avatar();
    u.set_mutation( trait_id( "DEBUG_STORAGE" ) );
    const units::volume room = npc_trading::carry_room( u );
    CHECK( room > 0_ml );
    CHECK( room + 5000_liter > room );
    CHECK( npc_trading::trade_pane_volume( u, 1000_ml ).used == u.carried_volume() + 1000_ml );
    u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
}

TEST_CASE( "cap1b_side_panel_string", "[cap1b][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    CHECK( carry_volume_string( u ) == expected_volume_string( u ) );
    // CDF: 60 L carry pocket, not the 65 L DDA total (volume units may be converted)
    const double cap = round_up( convert_volume( pack_carry_ml ), 2 );
    CHECK( carry_volume_string( u ) == string_format( "%.2f/%.2f", 0.0, cap ) );

    CHECK( get_volume_color( u ) == c_light_gray );
    add_rocks( u, 160 ); // 40 L of 60 L: 66.7 %
    CHECK( get_volume_color( u ) == c_yellow );
    add_rocks( u, 48 ); // 52 L of 60 L: 86.7 %
    CHECK( get_volume_color( u ) == c_red );
    CHECK( carry_volume_string( u ) == expected_volume_string( u ) );
}

TEST_CASE( "cap1b_pickup_header", "[cap1b][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    const units::volume cap = u.carried_capacity();
    const std::string at_bar = pickup::selector_head( u, 0_gram, cap );
    CHECK( at_bar.find( "<color_c_red>" ) == std::string::npos );
    CHECK( remove_color_tags( at_bar ).find( "Vol " + format_volume( cap ) + "/" +
            format_volume( cap ) ) != std::string::npos );
    const std::string over = pickup::selector_head( u, 0_gram, cap + 1_ml );
    CHECK( over.find( "<color_c_red>" ) != std::string::npos );
    // CDF: 63 L is under the 65 L DDA total but over the 60 L bar
    const std::string between = pickup::selector_head( u, 0_gram, 63000_ml );
    CHECK( between.find( "<color_c_red>" ) != std::string::npos );
}

TEST_CASE( "cap1b_advanced_inventory_header", "[cap1b][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    add_rocks( u, pack_carry_ml / 250 ); // exactly the bar
    REQUIRE( u.carried_volume() == u.carried_capacity() );
    advanced_inv_inventory_head_data head = advanced_inv_inventory_head( u );
    CHECK( head.volume_carried == format_volume( u.carried_volume() ) );
    CHECK( head.volume_capacity == format_volume( u.carried_capacity() ) );
    CHECK_FALSE( head.volume_over );

    fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    overfill_with_disorganized( u );
    head = advanced_inv_inventory_head( u );
    CHECK( head.volume_capacity == format_volume( u.carried_capacity() ) );
    CHECK( head.volume_over );
    // CDF: with the backpack the holster room must not hide an over-full bar
    fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    CHECK( advanced_inv_inventory_head( u ).volume_capacity == format_volume( 60000_ml ) );
}
