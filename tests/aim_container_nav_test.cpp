#include "advanced_inv.h"
#include "advanced_inv_area.h"
#include "advanced_inv_listitem.h"
#include "advanced_inv_pane.h"
#include "avatar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

#include <string>
#include <vector>

// P3-UI5: advanced inventory container navigation (DDA advanced_inv.cpp 1355-1468 and
// advanced_inv_pane.cpp 338-478).  Headless: pane and area state only, no ui_adaptor.

namespace
{
// Puts a new item of type @p id into a container pocket of @p outer and returns it.
item *put_in_container( item &outer, const char *id )
{
    REQUIRE( outer.put_in( item::spawn( itype_id( id ) ), pocket_type::CONTAINER ).success() );
    for( item *it : outer.all_items_top() ) {
        if( it->typeId() == itype_id( id ) ) {
            return it;
        }
    }
    FAIL( "the item that was just put in is not in the container" );
    return nullptr;
}
} // namespace

TEST_CASE( "pane container is per pane", "[advanced_inv][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> bag_a = item::spawn( itype_id( "backpack" ) );
    detached_ptr<item> bag_b = item::spawn( itype_id( "backpack" ) );
    advanced_inv_area square_south( AIM_SOUTH );
    advanced_inv_area square_container( AIM_CONTAINER );
    advanced_inventory_pane left;
    advanced_inventory_pane right;
    left.set_area( square_south );
    right.set_area( square_south );

    left.enter_container( &*bag_a, square_container );
    CHECK( left.get_container() == &*bag_a );
    CHECK( left.get_area() == AIM_CONTAINER );
    CHECK( left.get_container_base_loc() == AIM_SOUTH );
    // the other pane did not notice
    CHECK( right.get_container() == nullptr );
    CHECK( right.get_area() == AIM_SOUTH );

    right.enter_container( &*bag_b, square_container );
    CHECK( left.get_container() == &*bag_a );
    CHECK( right.get_container() == &*bag_b );

    left.leave_container();
    CHECK( left.get_container() == nullptr );
    CHECK( right.get_container() == &*bag_b );
}

TEST_CASE( "ITEMS_PARENT steps out one level", "[advanced_inv][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> outer_owner = item::spawn( itype_id( "backpack" ) );
    item *outer = &*outer_owner;
    item *inner = put_in_container( *outer, "bag_plastic" );
    REQUIRE( inner->parent_item() == outer );
    REQUIRE( outer->parent_item() == nullptr );

    advanced_inv_area square_south( AIM_SOUTH );
    advanced_inv_area square_container( AIM_CONTAINER );
    advanced_inventory_pane pane;
    pane.set_area( square_south );
    pane.enter_container( outer, square_container );
    pane.enter_container( inner, square_container );
    // entering a container from a container keeps the area the pane came from
    CHECK( pane.get_container_base_loc() == AIM_SOUTH );

    // first step: the bag that holds the bag
    aim_parent_target step = pane.parent_target( nullptr );
    REQUIRE( step.kind == aim_parent_kind::container );
    CHECK( step.container == outer );
    pane.enter_container( step.container, square_container );
    CHECK( pane.get_container() == outer );

    // second step: the square the outer bag lies on
    step = pane.parent_target( nullptr );
    CHECK( step.kind == aim_parent_kind::area );
    CHECK( step.area == AIM_SOUTH );

    SECTION( "the parent is the other pane's container" ) {
        pane.enter_container( inner, square_container );
        step = pane.parent_target( outer );
        CHECK( step.kind == aim_parent_kind::swap_panes );
    }

    SECTION( "a pane without a container has no parent" ) {
        advanced_inventory_pane empty_pane;
        empty_pane.set_area( square_south );
        CHECK( empty_pane.parent_target( nullptr ).kind == aim_parent_kind::none );
    }
}

TEST_CASE( "ITEMS_PARENT from a worn bag returns to the inventory view", "[advanced_inv][ui][p3]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    u.update_body_storage();
    u.wear_item( item::spawn( itype_id( "jeans" ) ) );
    // any carried item will do, only its place in the garment's pocket matters
    REQUIRE( u.try_add( item::spawn( itype_id( "rock" ) ) ).success() );

    advanced_inv_area square_inventory( AIM_INVENTORY );
    advanced_inv_area square_worn( AIM_WORN );
    advanced_inv_area square_container( AIM_CONTAINER );
    square_inventory.init();
    advanced_inventory_pane pane;
    pane.set_area( square_inventory );
    pane.add_items_from_area( square_inventory );
    REQUIRE( pane.items.size() == 1 );
    item *carried = pane.items.front().items.front();
    item *garment = carried->parent_item();
    REQUIRE( garment != nullptr );
    REQUIRE( garment->parent_item() == nullptr );

    SECTION( "from the inventory view" ) {
        pane.enter_container( carried, square_container );
        const aim_parent_target step = pane.parent_target( nullptr );
        // skips past the individual garment, no pane for the jeans in between
        CHECK( step.kind == aim_parent_kind::area );
        CHECK( step.area == AIM_INVENTORY );
    }

    SECTION( "from the worn items view" ) {
        advanced_inventory_pane worn_pane;
        worn_pane.set_area( square_worn );
        worn_pane.enter_container( garment, square_container );
        const aim_parent_target step = worn_pane.parent_target( nullptr );
        CHECK( step.kind == aim_parent_kind::area );
        CHECK( step.area == AIM_WORN );
    }
}

TEST_CASE( "container pane free volume", "[advanced_inv][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> bag_owner = item::spawn( itype_id( "backpack" ) );
    item *bag = &*bag_owner;
    advanced_inv_area square_container( AIM_CONTAINER );
    advanced_inventory_pane pane;
    pane.enter_container( bag, square_container );

    const units::volume free_empty = bag->get_remaining_volume();
    REQUIRE( free_empty > 0_ml );
    CHECK( pane.free_volume( square_container ) == free_empty );
    CHECK( pane.free_weight_capacity() == bag->get_remaining_weight_capacity() );

    put_in_container( *bag, "rock" );
    // the contents count: the pane asks the container, not its total capacity
    CHECK( pane.free_volume( square_container ) == bag->get_remaining_volume() );
    CHECK( pane.free_volume( square_container ) < free_empty );
    CHECK( pane.free_weight_capacity() == bag->get_remaining_weight_capacity() );

    SECTION( "no container, nothing free" ) {
        advanced_inventory_pane empty_pane;
        empty_pane.set_area( square_container );
        CHECK( empty_pane.free_volume( square_container ) == 0_ml );
        CHECK( empty_pane.free_weight_capacity() == 0_gram );
    }
}

TEST_CASE( "container pane lists the contents of its container", "[advanced_inv][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> bag_owner = item::spawn( itype_id( "backpack" ) );
    item *bag = &*bag_owner;
    item *rock = put_in_container( *bag, "rock" );
    advanced_inv_area square_container( AIM_CONTAINER );
    advanced_inventory_pane pane;

    SECTION( "no container, no rows" ) {
        pane.set_area( square_container );
        pane.add_items_from_area( square_container );
        CHECK( pane.items.empty() );
    }

    SECTION( "the rows are the container's items" ) {
        pane.enter_container( bag, square_container );
        pane.add_items_from_area( square_container );
        REQUIRE( pane.items.size() == 1 );
        CHECK( pane.items.front().items.front() == rock );
        CHECK( pane.items.front().area == AIM_CONTAINER );
        // the row is a normal item row, so ITEMS_CONTAINER can enter it when it is a container
        CHECK( pane.items.front().is_item_entry() );
    }
}

TEST_CASE( "unload container selects the container's items for the unload activity",
           "[advanced_inv][ui][p3]" )
{
    clear_all_state();
    detached_ptr<item> bag_owner = item::spawn( itype_id( "backpack" ) );
    item *bag = &*bag_owner;
    CHECK( advanced_inv_unload_targets( *bag ).empty() );

    item *plain = put_in_container( *bag, "rock" );
    item *kept = put_in_container( *bag, "rock" );
    kept->set_favorite( true );
    REQUIRE( plain != kept );

    const std::vector<item *> targets = advanced_inv_unload_targets( *bag );
    // favorites stay in the bag
    REQUIRE( targets.size() == 1 );
    CHECK( targets.front() == plain );
    CHECK_FALSE( targets.front()->is_favorite );
}

TEST_CASE( "home and end set the pane index to first and last row", "[advanced_inv][ui][p3]" )
{
    clear_all_state();
    advanced_inventory_pane pane;
    std::vector<detached_ptr<item>> rocks;
    constexpr int rows = 30;
    for( int i = 0; i < rows; ++i ) {
        rocks.push_back( item::spawn( itype_id( "rock" ) ) );
        pane.items.emplace_back( &*rocks.back(), i, 1, AIM_SOUTH, false );
    }

    SECTION( "home and end" ) {
        pane.index = 17;
        pane.scroll_to_start();
        CHECK( pane.index == 0 );
        pane.scroll_to_end();
        CHECK( pane.index == rows - 1 );
    }

    SECTION( "page scroll moves by a page and clamps" ) {
        pane.index = 5;
        pane.scroll_page( 10, +1 );
        CHECK( pane.index == 15 );
        pane.scroll_page( 10, +1 );
        CHECK( pane.index == 25 );
        // not a whole page left: stop at the last row
        pane.scroll_page( 10, +1 );
        CHECK( pane.index == rows - 1 );
        // and wrap from there, as DDA does
        pane.scroll_page( 10, +1 );
        CHECK( pane.index == 0 );

        pane.index = 5;
        pane.scroll_page( 10, -1 );
        CHECK( pane.index == 0 );
        pane.scroll_page( 10, -1 );
        CHECK( pane.index == rows - 1 );
        pane.scroll_page( 10, -1 );
        CHECK( pane.index == rows - 11 );
    }

    SECTION( "an empty pane stays put" ) {
        advanced_inventory_pane empty_pane;
        empty_pane.scroll_to_start();
        empty_pane.scroll_to_end();
        empty_pane.scroll_page( 10, +1 );
        CHECK( empty_pane.index == 0 );
    }
}
