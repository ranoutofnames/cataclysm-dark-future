// PL-64 PK1: the DDA single pickup menu becomes the default (G01) and the pickup stash path
// places ammo by the pocket settings instead of BN's ammo-container pre-pass (G38).
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "detached_ptr.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "options.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bandolier_pistol( "bandolier_pistol" );

// Charges of the given type directly inside the container.
static auto charges_in( const item &container, const itype_id &type ) -> int
{
    int total = 0;
    for( const item *it : container.all_items_top() ) {
        if( it->typeId() == type ) {
            total += it->charges;
        }
    }
    return total;
}

// G38: ammo picked up on the ground must go where the pocket settings send it; the old
// i_add_to_container pre-pass filled any ammo container already holding that ammo first and
// ignored its blacklist/whitelist and priority.
static void check_pickup_ammo_uses_pocket_settings( bool autopickup )
{
    avatar &they = get_avatar();
    map &here = get_map();
    clear_all_state();
    clear_character( they, false );
    clear_map();

    const tripoint_bub_ms ground = they.bub_pos();

    // An ammo container that already holds 9mm, with its pocket blacklisting 9mm.
    detached_ptr<item> bandolier_owned = item::spawn( itype_bandolier_pistol, calendar::turn );
    item &bandolier = *bandolier_owned;
    REQUIRE( !they.wear_item( std::move( bandolier_owned ), false ) );
    bandolier.force_insert_item( item::spawn( itype_9mm, calendar::turn, 5 ),
                                 pocket_type::CONTAINER );
    REQUIRE( bandolier.contents.front().charges == 5 );

    int blacklisted = 0;
    for( item_pocket *pocket : bandolier.get_container_pockets() ) {
        pocket->settings.blacklist_item( itype_9mm );
        ++blacklisted;
    }
    REQUIRE( blacklisted > 0 );

    // A plain backpack pocket still accepts the rounds.
    detached_ptr<item> bag_owned = item::spawn( itype_backpack, calendar::turn );
    item &backpack = *bag_owned;
    REQUIRE( !they.wear_item( std::move( bag_owned ), false ) );

    // 5 more 9mm rounds on the ground.
    detached_ptr<item> ground_owned = item::spawn( itype_9mm, calendar::turn, 5 );
    item &ground_ammo = *ground_owned;
    here.add_item( ground, std::move( ground_owned ) );
    REQUIRE( here.i_at( ground ).size() == 1 );

    assign_pickup_activity( they, { &ground_ammo }, { 0 }, autopickup );
    process_activity( they );

    CHECK( bandolier.contents.front().charges == 5 );
    CHECK( charges_in( backpack, itype_9mm ) == 5 );
    CHECK( they.charges_of( itype_9mm ) == 10 );
    CHECK( here.i_at( ground ).empty() );
}

TEST_CASE( "new_pickup_menu_is_the_default", "[pickup][options]" )
{
    // G01: the DDA selector is the default; the old pickup list stays behind the option for now.
    CHECK( get_option<bool>( "NEW_PICKUP_MENU" ) );
}

TEST_CASE( "pickup_ammo_uses_pocket_settings", "[pickup][pocket][autopickup]" )
{
    GIVEN( "autopickup" ) {
        check_pickup_ammo_uses_pocket_settings( true );
    }

    GIVEN( "manual pickup" ) {
        check_pickup_ammo_uses_pocket_settings( false );
    }
}
