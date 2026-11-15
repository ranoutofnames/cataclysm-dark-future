#include "avatar.h"
#include "carried_stacks.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "item_contents.h"
#include "npctrade.h"
#include "npc.h"
#include "player_helpers.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <vector>

// CDF G35 / TR1: DDA lists every carried item in the trade panes, nested ones too
// (npctrade.cpp:127 items_with), so one item can be sold out of a bag while the bag stays.
TEST_CASE( "nested carried items are listed and sold individually", "[npc][trade][fm]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    npc &buyer = spawn_npc( u.bub_pos() + point( 5, 0 ), "test_talker" );
    clear_character( buyer, true );
    buyer.set_attitude( NPCATT_FOLLOW );

    REQUIRE( !u.wear_item( item::spawn( itype_id( "backpack" ) ), false ) );
    item *pack = u.worn.back();
    REQUIRE( pack != nullptr );

    detached_ptr<item> outer = item::spawn( itype_id( "bag_plastic" ) );
    item *outer_ptr = outer.get();
    detached_ptr<item> inner = item::spawn( itype_id( "bag_plastic" ) );
    item *inner_ptr = inner.get();
    detached_ptr<item> rock = item::spawn( itype_id( "rock" ) );
    item *rock_ptr = rock.get();
    detached_ptr<item> wire = item::spawn( itype_id( "solder_wire" ) );
    item *wire_ptr = wire.get();

    // init_buying only lists what the seller owns (G35 test setup)
    outer_ptr->set_owner( u );
    inner_ptr->set_owner( u );
    rock_ptr->set_owner( u );
    wire_ptr->set_owner( u );

    inner_ptr->put_in( std::move( rock ) );
    inner_ptr->put_in( std::move( wire ) );
    REQUIRE( inner_ptr->contents.num_item_stacks() == 2 );
    outer_ptr->put_in( std::move( inner ) );
    REQUIRE( outer_ptr->contents.num_item_stacks() == 1 );

    ret_val<item *> added = u.try_add( std::move( outer ) );
    REQUIRE( added.success() );
    REQUIRE( outer_ptr->parent_item() == pack );

    std::vector<item_pricing> rows = npc_trading::init_buying( buyer, u, false );
    const auto row_for = [&rows]( item * target ) -> item_pricing * {
        const auto found = std::ranges::find_if( rows, [target]( const item_pricing & ip ) {
            return std::ranges::find( ip.locs, target ) != ip.locs.end();
        } );
        return found == rows.end() ? nullptr : &( *found );
    };

    REQUIRE( row_for( outer_ptr ) != nullptr );
    // the bag inside the worn backpack and the items inside that bag are rows of their own
    REQUIRE( row_for( inner_ptr ) != nullptr );
    REQUIRE( row_for( rock_ptr ) != nullptr );
    CHECK( row_for( wire_ptr ) != nullptr );

    item_pricing *rock_row = row_for( rock_ptr );
    REQUIRE( rock_row != nullptr );
    rock_row->selected = true;
    rock_row->npc_has = 1;

    npc_trading::transfer_items( rows, u, buyer, false );

    // only the selected nested item left; the bag, the other bag and the rest stay
    CHECK( buyer.has_item( *rock_ptr ) );
    CHECK( !u.has_item( *rock_ptr ) );
    CHECK( u.has_item( *outer_ptr ) );
    CHECK( u.has_item( *inner_ptr ) );
    CHECK( u.has_item( *wire_ptr ) );
    CHECK( inner_ptr->contents.num_item_stacks() == 1 );
}
