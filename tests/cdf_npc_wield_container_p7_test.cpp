// pk7 F116/F117 (DDA npc.cpp:1665-1710 npc::wield, npcmove.cpp:4916-4934 weapon choice):
// an NPC takes a weapon out of the container it is wielding, and its weapon choice looks inside
// containers instead of only at the top level.
#include "catch/catch.hpp"

#include "calendar.h"
#include "character.h"
#include "coordinates.h"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

#include <vector>

static const itype_id itype_bat( "bat" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bag_plastic( "bag_plastic" );

namespace
{
auto spawn_test_npc() -> npc &
{
    clear_all_state();
    clear_map();
    npc &who = spawn_npc( {50, 50, 0}, "test_talker" );
    clear_character( who );
    clear_worn( who );
    who.remove_primary_weapon();
    return who;
}

// DDA tests use wear_item's return value: null on success
auto wear_it( Character &who, detached_ptr<item> &&it ) -> bool
{
    return !who.wear_item( std::move( it ), false );
}

auto first_contained( item &container ) -> item &
{
    std::vector<item *> top = container.contents.all_items_top();
    REQUIRE( !top.empty() );
    return *top.front();
}
} // namespace

TEST_CASE( "npc_wield_extracts_weapon_from_wielded_container", "[npc][p7]" )
{
    npc &who = spawn_test_npc();
    detached_ptr<item> backpack_holder = item::spawn( itype_backpack, calendar::turn );
    item &backpack = *backpack_holder;
    backpack.force_insert_item( item::spawn( itype_bat, calendar::turn ), pocket_type::CONTAINER );
    item &bat = first_contained( backpack );

    who.set_primary_weapon( std::move( backpack_holder ) );
    REQUIRE( who.is_wielding( backpack ) );
    REQUIRE( backpack.has_item( bat ) );
    who.set_moves( 1000 );

    // wielding the bat has to take it out of the wielded backpack first; the backpack is then
    // stowed (worn or carried), not dropped, and the draw costs the retrieval moves
    REQUIRE( who.wield( bat ) );
    CHECK( who.primary_weapon().typeId() == itype_bat );
    CHECK_FALSE( who.is_wielding( backpack ) );
    CHECK( who.has_item( backpack ) );
    CHECK( who.moves < 1000 );
}

TEST_CASE( "npc_weapon_choice_looks_inside_containers", "[npc][p7]" )
{
    npc &who = spawn_test_npc();
    // a bat two containers deep: DDA's weapon scan descends into every container, the old scan
    // only looked at the top level and at one holster level
    detached_ptr<item> outer_holder = item::spawn( itype_backpack, calendar::turn );
    item &outer = *outer_holder;
    detached_ptr<item> inner_holder = item::spawn( itype_bag_plastic, calendar::turn );
    item &inner = *inner_holder;
    inner.force_insert_item( item::spawn( itype_bat, calendar::turn ), pocket_type::CONTAINER );
    item &bat = first_contained( inner );
    outer.force_insert_item( std::move( inner_holder ), pocket_type::CONTAINER );
    REQUIRE( outer.has_item( bat ) );

    REQUIRE( wear_it( who, std::move( outer_holder ) ) );
    who.clear_npc_ai_info_cache( npc_ai_info::range );
    REQUIRE_FALSE( who.is_armed() );

    REQUIRE( who.wield_better_weapon() );
    REQUIRE( who.primary_weapon().typeId() == itype_bat );
    CHECK_FALSE( inner.has_item( bat ) );
}
