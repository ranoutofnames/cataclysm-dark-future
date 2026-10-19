#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"

#include <vector>

namespace {

// Index of the MAGAZINE_WELL pocket in the order well_pocket_at uses: every
// pocket returned by item::get_pockets( all ), wells included.
auto pocket_index( const item &it, bool want_well ) -> int {
    int idx = 0;
    for( const item_pocket *p : it.get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) == want_well ) {
            return idx;
        }
        ++idx;
    }
    return -1;
}

auto loaded_m4a1( int rounds ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, rounds ) );
    gun->put_in( std::move( mag ) );
    return gun;
}

} // namespace

TEST_CASE( "per-well ammo accessors on a gun", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );

    CHECK( gun->ammo_remaining( well_idx ) == 30 );
    CHECK( gun->ammo_capacity( well_idx ) == 30 );
    CHECK( gun->remaining_ammo_capacity( well_idx ) == 0 );

    CHECK( gun->ammo_remaining( -1 ) == 0 );
    CHECK( gun->ammo_remaining( 999 ) == 0 );
    CHECK( gun->ammo_capacity( -1 ) == 0 );
    CHECK( gun->ammo_capacity( 999 ) == 0 );
    CHECK( gun->remaining_ammo_capacity( -1 ) == 0 );
    CHECK( gun->remaining_ammo_capacity( 999 ) == 0 );

    const int other_idx = pocket_index( *gun, false );
    REQUIRE( other_idx >= 0 );
    CHECK( gun->ammo_remaining( other_idx ) == 0 );
    CHECK( gun->ammo_capacity( other_idx ) == 0 );
    CHECK( gun->remaining_ammo_capacity( other_idx ) == 0 );
}

TEST_CASE( "per-well remaining_ammo_capacity tracks the loaded count", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 20 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );

    CHECK( gun->ammo_remaining( well_idx ) == 20 );
    CHECK( gun->ammo_capacity( well_idx ) == 30 );
    CHECK( gun->remaining_ammo_capacity( well_idx ) == 10 );
}

TEST_CASE( "all_magazine_well_pockets lists only wells", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );

    const std::vector<item_pocket *> wells = gun->all_magazine_well_pockets();
    REQUIRE( wells.size() == 1 );
    CHECK( wells.front()->is_type( pocket_type::MAGAZINE_WELL ) );

    const item &const_gun = *gun;
    const std::vector<const item_pocket *> const_wells = const_gun.all_magazine_well_pockets();
    REQUIRE( const_wells.size() == 1 );
    CHECK( const_wells.front()->is_type( pocket_type::MAGAZINE_WELL ) );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK( backpack->all_magazine_well_pockets().empty() );
}

TEST_CASE( "allows_speedloader needs a speedloader pocket", "[p3][ammo]" ) {
    // CDF: no CDF data item sets allowed_speedloaders, so only the false path
    // is covered here.
    detached_ptr<item> gun = loaded_m4a1( 30 );
    CHECK_FALSE( gun->allows_speedloader( itype_id( "40_speedloader6" ) ) );
    CHECK_FALSE( gun->allows_speedloader( itype_id( "stanag30" ) ) );
}

TEST_CASE( "item can_reload_with an ammo item", "[p3][ammo]" ) {
    // DDA tests/reload_magazine_test.cpp:52-62, with i_add swapped for item::spawn
    // and the pocket API reached directly.
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    detached_ptr<item> ammo = item::spawn( itype_id( "556" ) );
    detached_ptr<item> alt_ammo = item::spawn( itype_id( "223" ) );
    detached_ptr<item> bad_ammo = item::spawn( itype_id( "9mm" ) );
    detached_ptr<item> bad_mag = item::spawn( itype_id( "glockmag" ) );

    CHECK( mag->can_reload_with( *ammo, true ) );
    CHECK( mag->can_reload_with( *alt_ammo, true ) );
    CHECK_FALSE( mag->can_reload_with( *bad_ammo, true ) );
    CHECK_FALSE( mag->can_reload_with( *bad_mag, true ) );

    // The same predicate through item_contents agrees.
    CHECK( mag->contents.can_reload_with( *ammo, true ) );
    CHECK( mag->contents.can_reload_with( *alt_ammo, true ) );
    CHECK_FALSE( mag->contents.can_reload_with( *bad_ammo, true ) );
    CHECK_FALSE( mag->contents.can_reload_with( *bad_mag, true ) );
}
