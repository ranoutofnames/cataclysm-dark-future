// CO1 (PL-64 G23): a dead character's gear goes inside the corpse item, as DDA
// character.cpp:6332-6334 does, instead of lying next to it on the tile.
// DDA data/json/items/corpses/corpses.json gives the corpse item a CONTAINER pocket, so the
// loot list can be nested and the gear moves with the corpse.

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <array>
#include <vector>

namespace
{

auto has_item_of_type( const std::vector<item *> &items, const itype_id &id ) -> bool
{
    return std::ranges::any_of( items, [&id]( const item * it ) {
        return it->typeId() == id;
    } );
}

auto container_pockets( const item &it ) -> std::vector<const item_pocket *>
{
    return it.get_contents().get_pockets( pocket_type::CONTAINER );
}

} // namespace

// Every corpse item that copies from "corpse" gets DDA's CONTAINER pocket through copy-from.
TEST_CASE( "corpse_items_have_the_dda_container_pocket", "[corpse][nesting]" )
{
    clear_all_state();
    // The 16 corpse rows DATA-DIFF.tsv lists as MISSING-POCKETS (rmi2_corpse is a quest item
    // and does not copy from "corpse").
    const std::array<const char *, 16> corpse_ids = {
        {
            "corpse",
            "corpse_bloody",
            "corpse_child_calm",
            "corpse_child_gunned",
            "corpse_generic_boy",
            "corpse_generic_female",
            "corpse_generic_girl",
            "corpse_generic_human",
            "corpse_generic_male",
            "corpse_gunned",
            "corpse_half_beheaded",
            "corpse_halved_upper",
            "corpse_oldwoman_jewelry",
            "corpse_painful",
            "corpse_scorched",
            "corpse_stabbed",
        }
    };

    for( const char *id : corpse_ids ) {
        INFO( id );
        detached_ptr<item> corpse = item::spawn( itype_id( id ), calendar::turn );
        const std::vector<const item_pocket *> pockets = container_pockets( *corpse );
        REQUIRE( pockets.size() == 1 );
        // DDA data/json/items/corpses/corpses.json: not rigid, not picked into automatically,
        // its contents are visible.
        CHECK_FALSE( pockets.front()->rigid() );
        CHECK( pockets.front()->is_forbidden() );
        CHECK( pockets.front()->transparent() );
        CHECK( corpse->is_container() );
    }
}

TEST_CASE( "corpse_container_pocket_holds_gear", "[corpse][nesting]" )
{
    clear_all_state();
    detached_ptr<item> corpse = item::make_corpse( mtype_id::NULL_ID(), calendar::turn, "Test Subject" );
    REQUIRE( corpse->is_corpse() );
    REQUIRE( corpse->is_container() );
    REQUIRE( container_pockets( *corpse ).size() == 1 );

    corpse->force_insert_item( item::spawn( itype_id( "rock" ), calendar::turn ),
                               pocket_type::CONTAINER );

    const std::vector<item *> inside = corpse->all_items_top( pocket_type::CONTAINER );
    REQUIRE( inside.size() == 1 );
    CHECK( inside.front()->typeId() == itype_id( "rock" ) );
    CHECK( corpse->all_items_top().size() == 1 );
    CHECK_FALSE( corpse->is_container_empty() );
}

// DDA character.cpp:6332-6334: place_corpse puts the worn and carried gear inside the corpse.
TEST_CASE( "place_corpse_nests_the_gear", "[corpse][nesting]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    u.death_drops = true;
    const tripoint_bub_ms pos = u.bub_pos();

    // Body storage item and a worn item, so both branches of Character::visit_items are covered.
    u.i_add( item::spawn( itype_id( "rock" ), calendar::turn ) );
    detached_ptr<item> refused = u.wear_item( item::spawn( itype_id( "jeans" ), calendar::turn ),
                               false );
    REQUIRE( !refused );
    REQUIRE( u.has_item_with_id( itype_id( "rock" ) ) );

    u.place_corpse();

    map &here = get_map();
    std::vector<item *> corpses;
    for( item *it : here.i_at( pos ) ) {
        if( it->is_corpse() ) {
            corpses.push_back( it );
        }
    }
    REQUIRE( corpses.size() == 1 );
    // Nothing was left lying next to the corpse.
    CHECK( here.i_at( pos ).size() == 1 );

    const std::vector<item *> inside = corpses.front()->all_items_top( pocket_type::CONTAINER );
    REQUIRE( inside.size() == 2 );
    CHECK( has_item_of_type( inside, itype_id( "rock" ) ) );
    CHECK( has_item_of_type( inside, itype_id( "jeans" ) ) );
}

// DDA data/json/items/corpses/corpses.json: max_item_volume 150 L. An item that the pocket
// refuses is dropped on the tile, as the brief asks, instead of being lost.
TEST_CASE( "place_corpse_drops_gear_the_corpse_refuses", "[corpse][nesting]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    u.death_drops = true;
    const tripoint_bub_ms pos = u.bub_pos();

    // 200 L, over the corpse pocket's 150 L max_item_volume. DEBUG_STORAGE (clear_avatar) holds it.
    const itype_id barrel_id( "55gal_firebarrel" );
    u.i_add( item::spawn( barrel_id, calendar::turn ) );
    REQUIRE( u.has_item_with_id( barrel_id ) );

    u.place_corpse();

    map &here = get_map();
    int corpses = 0;
    int barrels_on_tile = 0;
    for( item *it : here.i_at( pos ) ) {
        if( it->is_corpse() ) {
            corpses++;
            CHECK( it->all_items_top( pocket_type::CONTAINER ).empty() );
        } else if( it->typeId() == barrel_id ) {
            barrels_on_tile++;
        }
    }
    CHECK( corpses == 1 );
    CHECK( barrels_on_tile == 1 );
    CHECK( u.primary_weapon().is_null() );
}
