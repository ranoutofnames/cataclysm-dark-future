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
    // CDF: faction_api_version is 2, so attitude FOLLOW alone is no alliance; the follower faction
    // makes is_player_ally() true and wants_to_buy() accepts cheap items (src/npc.cpp:1880-1888,
    // 2280-2293). CDF: the setter is npc::set_fac (npc.h:791).
    buyer.set_fac( faction_id( "your_followers" ) );

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

// CDF TR1b: a row is priced from item::price_no_contents (DDA npctrade.cpp:184), so the bag row
// and the hammer row pay for the bag and the hammer once each, not for the hammer twice.
TEST_CASE( "a container trade row is not priced with its contents", "[npc][trade][fm]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    npc &buyer = spawn_npc( u.bub_pos() + point( 5, 0 ), "test_talker" );
    clear_character( buyer, true );
    buyer.set_attitude( NPCATT_FOLLOW );

    REQUIRE( !u.wear_item( item::spawn( itype_id( "backpack" ) ), false ) );
    item *carrier = u.worn.back();
    REQUIRE( carrier != nullptr );

    detached_ptr<item> outer = item::spawn( itype_id( "backpack" ) );
    item *outer_ptr = outer.get();
    detached_ptr<item> hammer = item::spawn( itype_id( "hammer" ) );
    item *hammer_ptr = hammer.get();
    outer_ptr->set_owner( u );
    hammer_ptr->set_owner( u );

    outer_ptr->put_in( std::move( hammer ) );
    REQUIRE( outer_ptr->contents.num_item_stacks() == 1 );

    ret_val<item *> added = u.try_add( std::move( outer ) );
    REQUIRE( added.success() );
    REQUIRE( outer_ptr->parent_item() == carrier );

    std::vector<item_pricing> rows = npc_trading::init_buying( buyer, u, false );
    const auto row_for = [&rows]( item * target ) -> item_pricing * {
        const auto found = std::ranges::find_if( rows, [target]( const item_pricing & ip ) {
            return std::ranges::find( ip.locs, target ) != ip.locs.end();
        } );
        return found == rows.end() ? nullptr : &( *found );
    };

    item_pricing *outer_row = row_for( outer_ptr );
    item_pricing *hammer_row = row_for( hammer_ptr );
    REQUIRE( outer_row != nullptr );
    REQUIRE( hammer_row != nullptr );

    // CDF: init_buying charges npc::value( it, price_no_contents ); item_pricing::adjust_values
    // then applies the barter adjustment.
    const double adjust = npc_trading::net_price_adjustment( buyer, u );
    const auto value_of = [&buyer]( item * it ) -> int {
        return buyer.value( *it, static_cast<int>( it->price_no_contents( true ) ) );
    };
    CHECK( outer_row->price == Approx( value_of( outer_ptr ) * adjust ).epsilon( 0.001 ) );
    CHECK( hammer_row->price == Approx( value_of( hammer_ptr ) * adjust ).epsilon( 0.001 ) );
    // the bag alone is worth less than the bag with the hammer inside
    CHECK( outer_ptr->price( true ) > outer_ptr->price_no_contents( true ) );
    // bag + hammer rows pay once each: the hammer is not paid for twice
    CHECK( outer_row->price + hammer_row->price ==
           Approx( ( value_of( outer_ptr ) + value_of( hammer_ptr ) ) * adjust ).epsilon( 0.001 ) );
}

// CDF TR1b: DDA npctrade.cpp:58-79 returns the contents an NPC refuses to the giver; without it
// a refused item rides along when its container row is sold.
TEST_CASE( "a sold container returns contents the NPC refuses", "[npc][trade][fm]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    npc &buyer = spawn_npc( u.bub_pos() + point( 5, 0 ), "test_talker" );
    clear_character( buyer, true );
    // not an ally: wants_to_buy only takes items worth at least 80 cents
    buyer.set_attitude( NPCATT_NULL );

    REQUIRE( !u.wear_item( item::spawn( itype_id( "backpack" ) ), false ) );
    item *carrier = u.worn.back();
    REQUIRE( carrier != nullptr );

    detached_ptr<item> pack = item::spawn( itype_id( "backpack" ) );
    item *pack_ptr = pack.get();
    detached_ptr<item> bomb = item::spawn( itype_id( "grenade" ) );
    item *bomb_ptr = bomb.get();
    pack_ptr->set_owner( u );
    bomb_ptr->set_owner( u );
    // an active grenade is dangerous: npc::value() returns -1000, so the NPC refuses it
    bomb_ptr->activate();

    pack_ptr->put_in( std::move( bomb ) );
    REQUIRE( pack_ptr->contents.num_item_stacks() == 1 );

    ret_val<item *> added = u.try_add( std::move( pack ) );
    REQUIRE( added.success() );
    REQUIRE( pack_ptr->parent_item() == carrier );

    std::vector<item_pricing> rows = npc_trading::init_buying( buyer, u, false );
    const auto row_for = [&rows]( item * target ) -> item_pricing * {
        const auto found = std::ranges::find_if( rows, [target]( const item_pricing & ip ) {
            return std::ranges::find( ip.locs, target ) != ip.locs.end();
        } );
        return found == rows.end() ? nullptr : &( *found );
    };

    item_pricing *pack_row = row_for( pack_ptr );
    REQUIRE( pack_row != nullptr );
    // the refused grenade is not a row of its own
    CHECK( row_for( bomb_ptr ) == nullptr );

    pack_row->selected = true;
    pack_row->npc_has = 1;
    npc_trading::transfer_items( rows, u, buyer, false );

    CHECK( buyer.has_item( *pack_ptr ) );
    CHECK( !u.has_item( *pack_ptr ) );
    // DDA returns the refused content to the player instead of moving it to the NPC
    CHECK( u.has_item( *bomb_ptr ) );
    CHECK( !buyer.has_item( *bomb_ptr ) );
}

// CDF TR1b: the container row is priced without its contents, so an unselected content row must
// not ride along with it. DDA's selector auto-selects a container's contents and cannot split
// them; CDF's flat rows are independent, so the unselected hammer stays with the seller.
TEST_CASE( "a sold container leaves unselected contents with the seller", "[npc][trade][fm]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    npc &buyer = spawn_npc( u.bub_pos() + point( 5, 0 ), "test_talker" );
    clear_character( buyer, true );
    buyer.set_attitude( NPCATT_FOLLOW );

    REQUIRE( !u.wear_item( item::spawn( itype_id( "backpack" ) ), false ) );
    item *carrier = u.worn.back();
    REQUIRE( carrier != nullptr );

    detached_ptr<item> pack = item::spawn( itype_id( "backpack" ) );
    item *pack_ptr = pack.get();
    detached_ptr<item> hammer = item::spawn( itype_id( "hammer" ) );
    item *hammer_ptr = hammer.get();
    pack_ptr->set_owner( u );
    hammer_ptr->set_owner( u );

    pack_ptr->put_in( std::move( hammer ) );
    REQUIRE( pack_ptr->contents.num_item_stacks() == 1 );

    ret_val<item *> added = u.try_add( std::move( pack ) );
    REQUIRE( added.success() );
    REQUIRE( pack_ptr->parent_item() == carrier );

    std::vector<item_pricing> rows = npc_trading::init_buying( buyer, u, false );
    const auto row_for = [&rows]( item * target ) -> item_pricing * {
        const auto found = std::ranges::find_if( rows, [target]( const item_pricing & ip ) {
            return std::ranges::find( ip.locs, target ) != ip.locs.end();
        } );
        return found == rows.end() ? nullptr : &( *found );
    };

    item_pricing *pack_row = row_for( pack_ptr );
    REQUIRE( pack_row != nullptr );
    REQUIRE( row_for( hammer_ptr ) != nullptr );

    pack_row->selected = true;
    pack_row->npc_has = 1;
    npc_trading::transfer_items( rows, u, buyer, false );

    CHECK( buyer.has_item( *pack_ptr ) );
    CHECK( !u.has_item( *pack_ptr ) );
    // the hammer row was not selected, so it stays with the player (it was not paid for)
    CHECK( u.has_item( *hammer_ptr ) );
    CHECK( !buyer.has_item( *hammer_ptr ) );
}
