// DDA source: tests/item_test.cpp (DDA commit b4b62254ae2), P1-transplant J5.
// TEST_CASEs copied (2): rigid_armor_compliance, rigid_splint_compliance
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md): `guy.wield( item )`
// takes a detached item, `guy.wear( weapon, false )` is wear_possessed( weapon, false ) (it
// returns true when the item was worn), `worn.top_items_loc( guy ).front().get_item()` is
// `worn.front()`.
#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "effect.h"
#include "enums.h"
#include "item.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_arm_splint( "arm_splint" );

static const itype_id itype_test_armguard( "test_armguard" );

static const efftype_id effect_disabled( "disabled" );

// BN reads a broken limb from the "disabled" effect (Character::is_limb_broken), which suffer.cpp
// adds when a part's hp is zero; there is no turn processing here, so add it as well
static void break_right_arm( avatar &guy )
{
    guy.set_part_hp_cur( bodypart_id( "arm_r" ), 0 );
    guy.add_effect( effect_disabled, 1_hours, bodypart_str_id( "arm_r" ) );
}

TEST_CASE( "rigid_armor_compliance", "[item][armor]" )
{
    clear_all_state();
    avatar &guy = get_avatar();
    clear_avatar();
    // check if you can swap a rigid armor
    REQUIRE( !guy.wield( item::spawn( itype_test_armguard ) ) );

    REQUIRE( guy.wear_possessed( guy.primary_weapon(), false ) );

    CHECK( guy.worn.front()->get_side() == side::LEFT );

    guy.change_side( *guy.worn.front() );

    CHECK( guy.worn.front()->get_side() == side::RIGHT );

    // check if you can't wear 3 rigid armors
    clear_avatar();

    REQUIRE( !guy.wield( item::spawn( itype_test_armguard ) ) );
    REQUIRE( guy.wear_possessed( guy.primary_weapon(), false ) );

    REQUIRE( !guy.wield( item::spawn( itype_test_armguard ) ) );
    REQUIRE( guy.wear_possessed( guy.primary_weapon(), false ) );

    REQUIRE( !guy.wield( item::spawn( itype_test_armguard ) ) );
    REQUIRE( !guy.wear_possessed( guy.primary_weapon(), false ) );
}

TEST_CASE( "rigid_splint_compliance", "[item][armor]" )
{
    clear_all_state();
    avatar &guy = get_avatar();
    clear_avatar();

    // check if you can wear a splint
    break_right_arm( guy );
    REQUIRE( !guy.wield( item::spawn( itype_arm_splint ) ) );
    REQUIRE( guy.wear_possessed( guy.primary_weapon(), false ) );

    // check if you cannot wear a splint if something rigid is on that arm
    clear_avatar();

    REQUIRE( !guy.wield( item::spawn( itype_test_armguard ) ) );
    break_right_arm( guy );
    REQUIRE( guy.wear_possessed( guy.primary_weapon(), false ) );

    CHECK( guy.worn.front()->get_side() == side::LEFT );
    // swap side to the broken arm side
    guy.change_side( *guy.worn.front() );
    // should fail to wear
    REQUIRE( !guy.wield( item::spawn( itype_arm_splint ) ) );
    REQUIRE( !guy.wear_possessed( guy.primary_weapon(), false ) );

    // check if you can wear a splint if nothing rigid is on that arm
    clear_avatar();

    REQUIRE( !guy.wield( item::spawn( itype_test_armguard ) ) );
    break_right_arm( guy );
    REQUIRE( guy.wear_possessed( guy.primary_weapon(), false ) );

    CHECK( guy.worn.front()->get_side() == side::LEFT );

    // should be able to wear the arm is open
    REQUIRE( !guy.wield( item::spawn( itype_arm_splint ) ) );
    REQUIRE( guy.wear_possessed( guy.primary_weapon(), false ) );
}
