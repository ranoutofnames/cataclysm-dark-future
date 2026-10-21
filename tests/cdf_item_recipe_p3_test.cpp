#include <string>

#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "recipe.h"
#include "type_id.h"

static const ammotype ammo_battery( "battery" );

static const itype_id itype_medium_battery_cell( "medium_battery_cell" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_stick( "stick" );

static const recipe_id
recipe_medium_battery_cell_empty_magazine_output( "medium_battery_cell_empty_magazine_output" );
static const recipe_id
recipe_medium_battery_cell_full_magazine_output( "medium_battery_cell_full_magazine_output" );
static const recipe_id
recipe_test_rock_need_full_magazine_mixed( "test_rock_need_full_magazine_mixed" );
static const recipe_id
recipe_test_rock_ordinary_stick_and_battery( "test_rock_ordinary_stick_and_battery" );
static const recipe_id
recipe_test_rock_ordinary_stick_and_rock( "test_rock_ordinary_stick_and_rock" );
static const skill_id skill_fabrication( "fabrication" );

// CDF: items are detached_ptr; DDA returns an item by value.
static detached_ptr<item> battery_with_charge( const int charges )
{
    detached_ptr<item> battery = item::spawn( itype_medium_battery_cell );
    battery->ammo_set( battery->ammo_default(), charges );
    return battery;
}

static Character &prepare_crafter()
{
    clear_avatar();
    clear_map();
    Character &guy = get_player_character();
    guy.set_skill_level( skill_fabrication, 10 );
    return guy;
}

// CDF: ported from DDA tests/recipe_need_full_magazine_test.cpp; components are added as detached_ptr.
TEST_CASE( "ordinary_recipes_do_not_require_full_magazines", "[p3][recipe]" )
{
    SECTION( "an incomplete magazine is accepted without the flag" ) {
        Character &guy = prepare_crafter();
        detached_ptr<item> battery = item::spawn( itype_medium_battery_cell );
        const int capacity = battery->ammo_capacity( ammo_battery );
        REQUIRE( capacity > 1 );
        battery->ammo_set( battery->ammo_default(), capacity / 2 );

        guy.i_add( item::spawn( itype_stick ) );
        guy.i_add( std::move( battery ) );
        guy.invalidate_crafting_inventory();

        CHECK( guy.can_start_craft( &recipe_test_rock_ordinary_stick_and_battery.obj(),
                                    recipe_filter_flags::none,
                                    1 ) );
    }

    SECTION( "ordinary non-magazine components are unaffected" ) {
        Character &guy = prepare_crafter();
        guy.i_add( item::spawn( itype_stick ) );
        guy.i_add( item::spawn( itype_rock ) );
        guy.invalidate_crafting_inventory();

        CHECK( guy.can_start_craft( &recipe_test_rock_ordinary_stick_and_rock.obj(),
                                    recipe_filter_flags::none, 1 ) );
    }
}

TEST_CASE( "NEED_FULL_MAGAZINE_only_filters_magazine_components", "[p3][recipe]" )
{
    Character &guy = prepare_crafter();
    const item battery( itype_medium_battery_cell );
    const int capacity = battery.ammo_capacity( ammo_battery );
    REQUIRE( capacity > 1 );

    guy.i_add( item::spawn( itype_rock ) );

    SECTION( "a full magazine and an ordinary component satisfy the recipe" ) {
        guy.i_add( battery_with_charge( capacity ) );
        guy.invalidate_crafting_inventory();

        CHECK( guy.can_start_craft( &recipe_test_rock_need_full_magazine_mixed.obj(),
                                    recipe_filter_flags::none, 1 ) );
    }

    SECTION( "a half-full magazine does not satisfy the recipe" ) {
        guy.i_add( battery_with_charge( capacity / 2 ) );
        guy.invalidate_crafting_inventory();

        CHECK_FALSE( guy.can_start_craft( &recipe_test_rock_need_full_magazine_mixed.obj(),
                                          recipe_filter_flags::none, 1 ) );
    }

    SECTION( "an empty magazine does not satisfy the recipe" ) {
        guy.i_add( battery_with_charge( 0 ) );
        guy.invalidate_crafting_inventory();

        CHECK_FALSE( guy.can_start_craft( &recipe_test_rock_need_full_magazine_mixed.obj(),
                                          recipe_filter_flags::none, 1 ) );
    }
}

TEST_CASE( "full_magazine_output_fill", "[p3][recipe]" )
{
    const detached_ptr<item> r = recipe_medium_battery_cell_full_magazine_output->create_result();
    REQUIRE( r->is_magazine() );
    CHECK( r->ammo_remaining() == r->ammo_capacity( ammo_battery ) );
    CHECK( r->ammo_remaining() > 0 );

    const detached_ptr<item> r_empty =
        recipe_medium_battery_cell_empty_magazine_output->create_result();
    REQUIRE( r_empty->is_magazine() );
    CHECK( r_empty->ammo_remaining() == 0 );
}
