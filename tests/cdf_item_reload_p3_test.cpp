#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "type_id.h"
#include "player_helpers.h"

#include <string>

// CDF: P3-3-6; ported from DDA tests/multimag_test.cpp:626-693.
TEST_CASE( "item_reload_with_pocket_index_targets_specific_well", "[p3][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    // Multi-well gun with only the first (glock) well loaded; the stanag
    // well stays empty so item::reload(stanag_idx) has somewhere to insert.
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "test_multimag_gun" ) );
    detached_ptr<item> glock_mag = item::spawn( itype_id( "glockmag" ) );
    glock_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun_ptr->put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );

    // CDF: i_add returns item &, not an item_location.
    item &gun = you.i_add( std::move( gun_ptr ) );

    // Identify the two well indices (glock-loaded and empty-stanag) in gun.
    int loaded_well_idx = -1;
    int empty_well_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets(
        []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( p->magazine_current() != nullptr ) {
                    loaded_well_idx = idx;
                } else {
                    empty_well_idx = idx;
                }
            }
            ++idx;
        }
    }
    REQUIRE( loaded_well_idx >= 0 );
    REQUIRE( empty_well_idx >= 0 );

    SECTION( "pocket_index = empty stanag well inserts there, leaves glock untouched" ) {
        detached_ptr<item> replacement_stanag = item::spawn( itype_id( "stanag30" ) );
        replacement_stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ), pocket_type::MAGAZINE );
        item &replacement = you.i_add( std::move( replacement_stanag ) );

        const int glock_count_before = gun.ammo_remaining( loaded_well_idx );
        REQUIRE( gun.reload( you, replacement, 1, empty_well_idx ) );
        CHECK( gun.ammo_remaining( empty_well_idx ) == 10 );
        CHECK( gun.ammo_remaining( loaded_well_idx ) == glock_count_before );
    }

    SECTION( "invalid pocket_index aborts reload" ) {
        detached_ptr<item> replacement_stanag = item::spawn( itype_id( "stanag30" ) );
        replacement_stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ), pocket_type::MAGAZINE );
        item &replacement = you.i_add( std::move( replacement_stanag ) );

        const int glock_count_before = gun.ammo_remaining( loaded_well_idx );
        bool reload_result = true;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            // Out-of-range index. Aborts with a debugmsg, which we capture
            // so it does not flag the test as failed.
            reload_result = gun.reload( you, replacement, 1, 99 );
        } );
        CHECK( !reload_result );
        CHECK( dmsg.find( "pocket_index" ) != std::string::npos );
        CHECK( gun.ammo_remaining( loaded_well_idx ) == glock_count_before );
        CHECK( gun.ammo_remaining( empty_well_idx ) == 0 );
    }
}
