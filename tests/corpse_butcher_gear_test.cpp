// CO2 (follow-up of CO1): the gear of a dead character is nested in the corpse's CONTAINER
// pocket (CO1). Butchery and dissection must reveal that gear before the corpse item is
// destroyed, instead of letting it vanish with the corpse or treating it as an implant.
// DDA butchery.cpp:1026-1048 (butchery_drops_harvest) is the reference.

#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

#include <utility>
#include <vector>

namespace
{

auto corpse_on_tile( map &here, const tripoint_bub_ms &pos ) -> item *
{
    for( item *it : here.i_at( pos ) ) {
        if( it->is_corpse() ) {
            return it;
        }
    }
    return nullptr;
}

auto tile_count_of_type( map &here, const tripoint_bub_ms &pos, const itype_id &id ) -> int
{
    int count = 0;
    for( item *it : here.i_at( pos ) ) {
        if( it->typeId() == id ) {
            count++;
        }
    }
    return count;
}

// Puts a corpse with one piece of CONTAINER gear on the map and returns it.
auto place_corpse_with_gear( map &here, const tripoint_bub_ms &pos,
                             detached_ptr<item> &&gear ) -> item *
{
    detached_ptr<item> corpse_owner = item::make_corpse( mtype_id( "mon_zombie" ), calendar::turn, "" );
    item &corpse = *corpse_owner;
    corpse.force_insert_item( std::move( gear ), pocket_type::CONTAINER );
    here.add_item( pos, std::move( corpse_owner ) );
    return corpse_on_tile( here, pos );
}

} // namespace

// DDA butchery.cpp:1029-1047: a successful gear roll reveals the CONTAINER contents on the
// ground. CDF used to detach the corpse with the gear still inside, destroying the gear with it.
TEST_CASE( "butcher_reveals_corpse_container_gear", "[corpse][butcher]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.bub_pos();

    u.set_skill_level( skill_id( "survival" ), 12 );
    item &kit = u.i_add( item::spawn( itype_id( "butchering_kit" ), calendar::turn ) );
    u.wield( kit );
    REQUIRE( u.primary_weapon().typeId() == itype_id( "butchering_kit" ) );
    u.invalidate_crafting_inventory();

    item *corpse = place_corpse_with_gear( here, pos,
                                          item::spawn( itype_id( "rock" ), calendar::turn ) );
    REQUIRE( corpse != nullptr );

    player_activity act( activity_id( "ACT_BUTCHER" ), 0, 0 );
    act.targets.emplace_back( *corpse );
    activity_handlers::butcher_finish( &act, &u );

    CHECK( corpse_on_tile( here, pos ) == nullptr );
    CHECK( tile_count_of_type( here, pos, itype_id( "rock" ) ) == 1 );
}

// DDA butchery.cpp:1029-1047 applies to DISSECT too, with its own gear roll; the CBM roll runs
// on the CORPSE pocket (CDF: components) only. Gear must not be wrecked as if it were a bionic.
TEST_CASE( "dissect_reveals_container_gear_without_wrecking_it", "[corpse][butcher]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.bub_pos();

    u.set_skill_level( skill_id( "firstaid" ), 10 );
    u.set_skill_level( skill_id( "electronics" ), 10 );
    item &kit = u.i_add( item::spawn( itype_id( "butchering_kit" ), calendar::turn ) );
    u.wield( kit );
    REQUIRE( u.primary_weapon().typeId() == itype_id( "butchering_kit" ) );
    u.invalidate_crafting_inventory();

    item *corpse = place_corpse_with_gear( here, pos,
                                          item::spawn( itype_id( "rock" ), calendar::turn ) );
    REQUIRE( corpse != nullptr );

    // A faulty CBM is loose gear in the CONTAINER pocket. The bionic extraction path would
    // convert it to a burnt-out bionic; the gear roll must hand it back unchanged (DDA
    // butchery.cpp:1029-1041).
    detached_ptr<item> faulty = item::spawn( itype_id( "bio_power_storage" ), calendar::turn );
    faulty->set_flag( flag_id( "BIONIC_FAULTY" ) );
    corpse->force_insert_item( std::move( faulty ), pocket_type::CONTAINER );

    player_activity act( activity_id( "ACT_DISSECT" ), 0, 0 );
    act.targets.emplace_back( *corpse );
    activity_handlers::butcher_finish( &act, &u );

    CHECK( corpse_on_tile( here, pos ) == nullptr );
    CHECK( tile_count_of_type( here, pos, itype_id( "rock" ) ) == 1 );
    CHECK( tile_count_of_type( here, pos, itype_id( "bio_power_storage" ) ) == 1 );
    CHECK( tile_count_of_type( here, pos, itype_id( "burnt_out_bionic" ) ) == 0 );
}

// DDA butchery.cpp:1027 excludes FIELD_DRESS, SKIN, BLEED and DISMEMBER: those actions leave the
// corpse on the map, so the gear stays in its CONTAINER pocket.
TEST_CASE( "field_dress_keeps_container_gear_in_the_corpse", "[corpse][butcher]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.bub_pos();

    u.set_skill_level( skill_id( "survival" ), 12 );
    item &kit = u.i_add( item::spawn( itype_id( "butchering_kit" ), calendar::turn ) );
    u.wield( kit );
    REQUIRE( u.primary_weapon().typeId() == itype_id( "butchering_kit" ) );
    u.invalidate_crafting_inventory();

    item *corpse = place_corpse_with_gear( here, pos,
                                          item::spawn( itype_id( "rock" ), calendar::turn ) );
    REQUIRE( corpse != nullptr );

    player_activity act( activity_id( "ACT_FIELD_DRESS" ), 0, 0 );
    act.targets.emplace_back( *corpse );
    activity_handlers::butcher_finish( &act, &u );

    item *corpse_after = corpse_on_tile( here, pos );
    REQUIRE( corpse_after != nullptr );
    const std::vector<item *> inside = corpse_after->all_items_top( pocket_type::CONTAINER );
    REQUIRE( inside.size() == 1 );
    CHECK( inside.front()->typeId() == itype_id( "rock" ) );
    CHECK( tile_count_of_type( here, pos, itype_id( "rock" ) ) == 0 );
}
