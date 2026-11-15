// PL-64 IN3 (G39) and WR1 (G41): wield/takeoff and wear refusals.
// DDA sources: Character::wield, src/character_inventory.cpp:2985-3070 (on_takeoff before the
// item moves) and Character::can_wear, src/character_attire.cpp:104-170.
#include <string>

#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "detached_ptr.h"
#include "flag.h"
#include "item.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_esapi_plate( "esapi_plate" );
static const itype_id itype_knife_combat( "knife_combat" );
static const itype_id itype_knife_hunting( "knife_hunting" );
static const itype_id itype_socks_wool( "socks_wool" );
static const itype_id itype_wristwatch( "wristwatch" );

// IN3: a failed wield must leave a container-sourced item in its container, as DDA does by
// returning false before the item is removed. The avatar path is src/avatar_action.cpp.
TEST_CASE( "failed_wield_keeps_container_item_in_its_container",
           "[character][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, false );

    detached_ptr<item> carried = item::spawn( itype_knife_hunting );
    REQUIRE( !you.wield( std::move( carried ) ) );
    REQUIRE( you.primary_weapon().typeId() == itype_knife_hunting );

    detached_ptr<item> backpack_holder = item::spawn( itype_backpack );
    item &backpack = *backpack_holder;
    REQUIRE( you.wear_item( std::move( backpack_holder ), false ) == nullptr );
    REQUIRE( you.is_worn( backpack ) );

    detached_ptr<item> nested_holder = item::spawn( itype_knife_combat );
    item &nested = *nested_holder;
    REQUIRE( backpack.can_contain( nested ).success() );
    REQUIRE( backpack.put_in( std::move( nested_holder ), pocket_type::CONTAINER ).success() );
    REQUIRE( nested.parent_item() == &backpack );

    // The "Stop wielding" prompt cannot be answered in tests, so unwield() fails. Before IN3 the
    // container branch then obtained the knife and left it in the main inventory.
    avatar_action::wield( nested );

    CHECK( you.primary_weapon().typeId() == itype_knife_hunting );
    CHECK( nested.parent_item() == &backpack );
    CHECK( backpack.num_item_stacks() == 1 );
    CHECK( you.amount_worn( itype_backpack ) == 1 );
}

// IN3: wielding a worn item runs its takeoff effects (DDA Character::wield calls
// item::on_takeoff before moving the item). The observable CDF effect is the side reset.
TEST_CASE( "wielding_a_worn_item_runs_its_takeoff", "[character][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, false );

    detached_ptr<item> watch_holder = item::spawn( itype_wristwatch );
    item &watch = *watch_holder;
    REQUIRE( watch.is_sided() );
    REQUIRE( you.wear_item( std::move( watch_holder ), false ) == nullptr );
    REQUIRE( you.is_worn( watch ) );
    REQUIRE( watch.get_side() != side::BOTH );

    avatar_action::wield( watch );

    CHECK( you.primary_weapon().typeId() == itype_wristwatch );
    CHECK_FALSE( you.is_worn( watch ) );
    CHECK( watch.get_side() == side::BOTH );
}

// WR1: wool refusal (DDA Character::can_wear, character_attire.cpp:124-127).
TEST_CASE( "wool_allergy_refuses_wool_clothing", "[character][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, false );
    you.set_mutation( trait_id( "WOOLALLERGY" ) );

    detached_ptr<item> socks = item::spawn( itype_socks_wool );
    const ret_val<bool> res = you.can_wear( *socks );
    INFO( res.str() );
    CHECK_FALSE( res.success() );
    CHECK( res.str().find( "wool" ) != std::string::npos );
    CHECK( you.wear_item( std::move( socks ), false ) != nullptr );

    // DDA also refuses a non-wool item that carries the "wooled" clothing mod flag.
    detached_ptr<item> wooled = item::spawn( itype_id( "socks" ) );
    wooled->set_flag( flag_id( "wooled" ) );
    CHECK_FALSE( you.can_wear( *wooled ).success() );

    // Not allergic: the same item can be worn.
    you.unset_mutation( trait_id( "WOOLALLERGY" ) );
    detached_ptr<item> socks_ok = item::spawn( itype_socks_wool );
    CHECK( you.can_wear( *socks_ok ).success() );
}

// WR1: "Can't be worn directly" refusal. CDF has the same refusal since e7e7f009 (P1 pocket
// system port) with an armor-plate message; the refusal itself is what this case pins.
TEST_CASE( "cant_wear_items_are_refused", "[character][fm]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, false );

    detached_ptr<item> plate = item::spawn( itype_esapi_plate );
    const ret_val<bool> res = you.can_wear( *plate );
    INFO( res.str() );
    CHECK_FALSE( res.success() );
}
