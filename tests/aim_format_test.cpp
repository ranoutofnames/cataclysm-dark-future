// FM6: advanced inventory formatting (DDA advanced_inv.cpp 298-373, 439-499, 853-880, 1253-1281
// and advanced_inv_pane.cpp 214-228).  DDA has no tests for this drawing; every case here checks
// the pure pieces the drawing uses, because the curses output has no test hook.
#include "advanced_inv.h"
#include "advanced_inv_area.h"
#include "advanced_inv_listitem.h"
#include "advanced_inv_pane.h"
#include "avatar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "color.h"
#include "item.h"
#include "output.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "type_id.h"
#include "units.h"
#include "units_utility.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

// Puts a new item of type @p id into a container pocket of @p outer and returns it.
item *put_in_container( item &outer, const char *id )
{
    std::vector<const item *> before;
    for( const item *it : outer.all_items_top() ) {
        before.push_back( it );
    }
    REQUIRE( outer.put_in( item::spawn( itype_id( id ) ), pocket_type::CONTAINER ).success() );
    for( item *it : outer.all_items_top() ) {
        if( it->typeId() == itype_id( id ) && std::ranges::find( before, it ) == before.end() ) {
            return it;
        }
    }
    FAIL( "the item that was just put in is not in the container" );
    return nullptr;
}

} // namespace

TEST_CASE( "pane header marks the other pane's container", "[advanced_inv][ui][fm]" )
{
    clear_all_state();
    detached_ptr<item> outer_owner = item::spawn( itype_id( "backpack" ) );
    item *outer = &*outer_owner;
    item *inner = put_in_container( *outer, "bag_plastic" );
    detached_ptr<item> rock_owner = item::spawn( itype_id( "rock" ) );
    item *rock = &*rock_owner;
    REQUIRE( inner->parent_item() == outer );

    // DDA advanced_inv.cpp:298-304: the highlighted set is the other pane's container and its
    // ancestors, so the container row and the bag that holds it both match.
    CHECK( advanced_inv_is_other_pane_container( *inner, inner ) );
    CHECK( advanced_inv_is_other_pane_container( *outer, inner ) );
    CHECK_FALSE( advanced_inv_is_other_pane_container( *inner, outer ) );
    CHECK_FALSE( advanced_inv_is_other_pane_container( *rock, inner ) );
    CHECK_FALSE( advanced_inv_is_other_pane_container( *rock, nullptr ) );

    advanced_inventory_pane pane;
    CHECK( pane.other_cont == -1 );
    pane.items.emplace_back( rock, 0, 1, AIM_WORN, false );
    pane.items.emplace_back( outer, 1, 1, AIM_WORN, false );
    pane.items.emplace_back( inner, 2, 1, AIM_WORN, false );
    // the row on the page, used for the brown/white-on-yellow highlight and the pointer.  Like
    // DDA, every matching row is marked and the pointer uses the last one on the page.
    CHECK( advanced_inv_other_cont_line( pane.items, 0, 10, inner ) == 2 );
    CHECK( advanced_inv_other_cont_line( pane.items, 1, 1, inner ) == 0 );
    CHECK( advanced_inv_other_cont_line( pane.items, 0, 10, outer ) == 1 );
    CHECK( advanced_inv_other_cont_line( pane.items, 0, 10, rock ) == 0 );
    CHECK( advanced_inv_other_cont_line( pane.items, 0, 10, nullptr ) == -1 );
}

TEST_CASE( "container header numbers are unpadded and coloured by capacity",
           "[advanced_inv][ui][fm]" )
{
    clear_all_state();
    detached_ptr<item> bag_owner = item::spawn( itype_id( "backpack" ) );
    item *bag = &*bag_owner;
    put_in_container( *bag, "rock" );

    const advanced_inv_container_head_data head = advanced_inv_container_head( *bag );
    // DDA advanced_inv.cpp:340-346: "%.1f/%.1f", no field width, red only when over capacity
    CHECK( head.weight_contained == string_format( "%.1f",
            convert_weight( bag->get_total_contained_weight() ) ) );
    CHECK( head.weight_contained.find( ' ' ) == std::string::npos );
    CHECK( head.weight_capacity == string_format( "/%.1f %s  ",
            convert_weight( bag->get_total_weight_capacity() ), weight_units() ) );
    CHECK( head.volume_contained == format_volume( bag->get_contents_volume() ) );
    CHECK_FALSE( head.weight_over );
    CHECK_FALSE( head.volume_over );

    CHECK( advanced_inv_capacity_color( false ) == c_light_green );
    CHECK( advanced_inv_capacity_color( true ) == c_red );
}

TEST_CASE( "container area without container prints the square totals", "[advanced_inv][ui][fm]" )
{
    // DDA advanced_inv.cpp:358-373: only a real container uses the container head; the empty
    // container area falls through to the square branch.
    CHECK( advanced_inv_head_kind_for( AIM_CONTAINER, true ) == advanced_inv_head_kind::container );
    CHECK( advanced_inv_head_kind_for( AIM_CONTAINER, false ) == advanced_inv_head_kind::square );
    CHECK( advanced_inv_head_kind_for( AIM_INVENTORY, false ) == advanced_inv_head_kind::character );
    CHECK( advanced_inv_head_kind_for( AIM_WORN, false ) == advanced_inv_head_kind::character );
    CHECK( advanced_inv_head_kind_for( AIM_ALL, false ) == advanced_inv_head_kind::all );
    CHECK( advanced_inv_head_kind_for( AIM_SOUTH, false ) == advanced_inv_head_kind::square );
}

TEST_CASE( "worn pane lists the wielded weapon first", "[advanced_inv][ui][fm]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    u.update_body_storage();
    u.wear_item( item::spawn( itype_id( "jeans" ) ) );
    u.set_primary_weapon( item::spawn( itype_id( "rock" ) ) );
    item *weapon = &u.primary_weapon();
    REQUIRE_FALSE( weapon->is_null() );

    advanced_inv_area square_worn( AIM_WORN );
    square_worn.init();
    advanced_inventory_pane pane;
    pane.set_area( square_worn );
    pane.add_items_from_area( square_worn );

    // DDA advanced_inv_pane.cpp:214-228: the weapon is row 0, the worn items follow
    REQUIRE( pane.items.size() == 2 );
    CHECK( pane.items.front().items.front() == weapon );
    CHECK( pane.items.back().items.front()->typeId() == itype_id( "jeans" ) );
}

TEST_CASE( "inventory pane lists contents of a worn bag", "[advanced_inv][ui][fm]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    u.update_body_storage();
    u.wear_item( item::spawn( itype_id( "backpack" ) ) );
    REQUIRE( u.worn.size() == 1 );
    item &bag = **u.worn.begin();
    item *rock = put_in_container( bag, "rock" );

    advanced_inv_area square_inventory( AIM_INVENTORY );
    square_inventory.init();
    advanced_inventory_pane pane;
    pane.set_area( square_inventory );
    pane.add_items_from_area( square_inventory );

    // DDA advanced_inv_pane.cpp:125-152 lists the contents of worn containers in the inventory
    // pane.  CDF keeps its stack model and garment column (KEEP).
    REQUIRE( pane.items.size() == 1 );
    CHECK( pane.items.front().items.front() == rock );
    CHECK( pane.items.front().holder == bag.tname( 1, false ) );

    // FM6 a1: showing the bag in the other pane does not remove the rock from this list.
    pane.items.clear();
    pane.other_pane_container = &bag;
    pane.add_items_from_area( square_inventory );
    REQUIRE( pane.items.size() == 1 );
    CHECK( pane.items.front().items.front() == rock );
}

TEST_CASE( "page indicator on a single page", "[advanced_inv][ui][fm]" )
{
    // DDA advanced_inv.cpp:853-865: the active pane always shows "page x of y", so a list with
    // one page reads "page 1 of 1".
    CHECK( advanced_inv_page_count( 0, 10 ) == 1 );
    CHECK( advanced_inv_page_count( 1, 10 ) == 1 );
    CHECK( advanced_inv_page_count( 10, 10 ) == 1 );
    CHECK( advanced_inv_page_count( 11, 10 ) == 2 );
    CHECK( advanced_inv_page_count( 21, 10 ) == 3 );
    CHECK( advanced_inv_page_count( 5, 0 ) == 1 );
}

TEST_CASE( "move-all pre-check weighs the load as well as the volume", "[advanced_inv][ui][fm]" )
{
    clear_all_state();
    // FM6, G46 (DDA advanced_inv.cpp:1127-1156): the move-all pre-check compares the source pane's
    // totals with what the destination has free, and for the inventory and a container also with
    // its free weight capacity.  The prompt then names whichever is short (DDA 1185-1189).
    SECTION( "everything fits" ) {
        const advanced_inv_move_all_limit fits = advanced_inv_move_all_limit_for(
                    5_liter, 5_kilogram, 10_liter, 10_kilogram, true );
        CHECK( fits.over_volume == -5_liter );
        CHECK( fits.over_weight == -5_kilogram );
        CHECK( fits.limitation.empty() );
    }

    SECTION( "only the room is short" ) {
        const advanced_inv_move_all_limit limit = advanced_inv_move_all_limit_for(
                    12_liter, 5_kilogram, 10_liter, 10_kilogram, true );
        CHECK( limit.over_volume == 2_liter );
        CHECK( limit.over_weight == -5_kilogram );
        CHECK( limit.limitation == "room" );
    }

    SECTION( "only the weight capacity is short" ) {
        const advanced_inv_move_all_limit limit = advanced_inv_move_all_limit_for(
                    5_liter, 12_kilogram, 10_liter, 10_kilogram, true );
        CHECK( limit.over_volume == -5_liter );
        CHECK( limit.over_weight == 2_kilogram );
        CHECK( limit.limitation == "weight capacity" );
    }

    SECTION( "both are short" ) {
        const advanced_inv_move_all_limit limit = advanced_inv_move_all_limit_for(
                    12_liter, 12_kilogram, 10_liter, 10_kilogram, true );
        CHECK( limit.over_volume == 2_liter );
        CHECK( limit.over_weight == 2_kilogram );
        CHECK( limit.limitation == "room or weight capacity" );
    }

    SECTION( "a map square has no weight limit" ) {
        // DDA advanced_inv.cpp:1138-1142 weighs only the inventory and a container
        const advanced_inv_move_all_limit limit = advanced_inv_move_all_limit_for(
                    5_liter, 12_kilogram, 10_liter, 10_kilogram, false );
        CHECK( limit.over_volume == -5_liter );
        CHECK( limit.over_weight == 0_gram );
        CHECK( limit.limitation.empty() );
    }

    SECTION( "a container's remaining capacity" ) {
        detached_ptr<item> bag_owner = item::spawn( itype_id( "backpack" ) );
        item *bag = &*bag_owner;
        advanced_inv_area square_container( AIM_CONTAINER );
        advanced_inventory_pane pane;
        pane.enter_container( bag, square_container );

        // one gram more than the bag can still take, and nothing to add volume-wise
        const units::volume free_volume = pane.free_volume( square_container );
        const advanced_inv_move_all_limit limit = advanced_inv_move_all_limit_for(
                    0_ml, bag->get_remaining_weight_capacity() + 1_gram,
                    free_volume, pane.free_weight_capacity(), true );
        CHECK( limit.over_volume == -free_volume );
        CHECK( limit.over_weight == 1_gram );
        CHECK( limit.limitation == "weight capacity" );
    }
}

TEST_CASE( "sort names match DDA for the shared sorts", "[advanced_inv][ui][fm]" )
{
    // DDA advanced_inv.cpp:211-244
    CHECK( advanced_inv_sort_name( SORTBY_NONE ) == "none" );
    CHECK( advanced_inv_sort_name( SORTBY_NAME ) == "name" );
    CHECK( advanced_inv_sort_name( SORTBY_WEIGHT ) == "weight" );
    CHECK( advanced_inv_sort_name( SORTBY_VOLUME ) == "volume" );
    CHECK( advanced_inv_sort_name( SORTBY_CHARGES ) == "charges" );
    CHECK( advanced_inv_sort_name( SORTBY_CATEGORY ) == "category" );
    CHECK( advanced_inv_sort_name( SORTBY_DAMAGE ) == "offensive power" );
    CHECK( advanced_inv_sort_name( SORTBY_AMMO ) == "ammo/charge type" );
    CHECK( advanced_inv_sort_name( SORTBY_SPOILAGE ) == "spoilage" );
    CHECK( advanced_inv_sort_name( SORTBY_PRICE ) == "barter value" );
    CHECK( advanced_inv_sort_name( SORTBY_DENSITY ) == "density" );
    CHECK( advanced_inv_sort_name( SORTBY_PRICEPERVOLUME ) == "barter value / volume" );
    CHECK( advanced_inv_sort_name( SORTBY_PRICEPERWEIGHT ) == "barter value / weight" );
    CHECK( advanced_inv_sort_name( SORTBY_STACKS ) == "amount" );
}
