#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace {

auto loaded_m4a1( int rounds ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, rounds ) );
    gun->put_in( std::move( mag ) );
    return gun;
}

} // namespace

TEST_CASE( "pocket_by_id nullptr for empty and unknown ids", "[p3][magazine]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item &cgun = *gun;
    CHECK( gun->pocket_by_id( "" ) == nullptr );
    CHECK( gun->pocket_by_id( "nope" ) == nullptr );
    CHECK( cgun.pocket_by_id( "" ) == nullptr );
    CHECK( cgun.pocket_by_id( "nope" ) == nullptr );
}

TEST_CASE( "pocket_by_id finds the well by pocket id; const overload agrees", "[p3][magazine]" ) {
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ) );
    const item &cgun = *gun;
    item_pocket *ammo = gun->pocket_by_id( "ammo" );
    item_pocket *power = gun->pocket_by_id( "power" );
    REQUIRE( ammo != nullptr );
    REQUIRE( power != nullptr );
    CHECK( ammo != power );
    CHECK( cgun.pocket_by_id( "ammo" ) == ammo );
    CHECK( cgun.pocket_by_id( "power" ) == power );
}

TEST_CASE( "ammo_remaining_in_pocket sums the well's magazine; 0 for empty and unknown ids",
           "[p3][magazine]" ) {
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ) );
    REQUIRE( gun->pocket_by_id( "ammo" ) != nullptr );
    CHECK( gun->ammo_remaining_in_pocket( "ammo" ) == 0 );

    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    mag->ammo_set( itype_id( "9mm" ), 15 );
    REQUIRE( mag->ammo_remaining() == 15 );
    gun->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL );

    CHECK( gun->ammo_remaining_in_pocket( "ammo" ) == 15 );
    CHECK( gun->ammo_remaining_in_pocket( "nope" ) == 0 );
    CHECK( gun->ammo_remaining_in_pocket( "" ) == 0 );
}

TEST_CASE( "item_magazines_default_one_entry_per_well", "[p3][magazine]" ) {
    const item gun( itype_id( "test_multimag_gun" ) );
    const std::vector<itype_id> defaults = gun.magazines_default();
    REQUIRE( defaults.size() == 2 );
    CHECK( std::count( defaults.begin(), defaults.end(), itype_id( "glockmag" ) ) == 1 );
    CHECK( std::count( defaults.begin(), defaults.end(), itype_id( "stanag30" ) ) == 1 );

    // The m4a1 well is synthesized and has no default magazine until 3-5b.
    detached_ptr<item> m4a1 = item::spawn( itype_id( "m4a1" ) );
    const std::vector<itype_id> m4_defaults = m4a1->magazines_default();
    REQUIRE( m4_defaults.size() == 1 );
    // CDF: 3-5b sets the synthesized well default
    CHECK( m4_defaults.front() == itype_id( "stanag30" ) );
}

TEST_CASE( "magazine_integral true for nailgun and fish_trap, false for m1911", "[p3][magazine]" ) {
    CHECK( item( itype_id( "nailgun" ) ).magazine_integral() );
    CHECK( item( itype_id( "fish_trap" ) ).magazine_integral() );
    CHECK_FALSE( item( itype_id( "m1911" ) ).magazine_integral() );
}

TEST_CASE( "magazine_integral true for colt_army", "[p3][magazine]" ) {
    CHECK( item( itype_id( "colt_army" ) ).magazine_integral() );
}

TEST_CASE( "ctor puts belt223 default ammo in the MAGAZINE pocket", "[p3][magazine]" ) {
    detached_ptr<item> belt = item::spawn( itype_id( "belt223" ) );
    CHECK( belt->ammo_remaining() == 100 );
    CHECK( belt->has_pocket_type( pocket_type::MAGAZINE ) );
}

TEST_CASE( "dress_magazine_wells_fills_empty_siblings", "[p3][magazine]" )
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun" ), calendar::turn );
    // Pre-load only the glock well.
    detached_ptr<item> glock_mag = item::spawn( itype_id( "glockmag" ), calendar::turn );
    glock_mag->ammo_set( itype_id( "9mm" ), 15 );
    gun->put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );
    REQUIRE( gun->magazines_current().size() == 1 );

    gun->dress_magazine_wells( /*insert_default_mag=*/true, /*fill_with_default_ammo=*/true );

    const std::vector<item *> mags = gun->magazines_current();
    REQUIRE( mags.size() == 2 );
    std::set<itype_id> mag_types;
    for( const item *m : mags ) {
        mag_types.insert( m->typeId() );
    }
    CHECK( mag_types.count( itype_id( "glockmag" ) ) == 1 );
    CHECK( mag_types.count( itype_id( "stanag30" ) ) == 1 );
    for( const item *m : mags ) {
        CHECK( m->ammo_remaining() > 0 );
    }
}

TEST_CASE( "dress_magazine_wells_tops_up_empty_mag", "[p3][magazine]" )
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun" ), calendar::turn );
    detached_ptr<item> empty_mag = item::spawn( itype_id( "glockmag" ), calendar::turn );
    REQUIRE( empty_mag->ammo_remaining() == 0 );
    gun->put_in( std::move( empty_mag ), pocket_type::MAGAZINE_WELL );

    gun->dress_magazine_wells( /*insert_default_mag=*/false, /*fill_with_default_ammo=*/true );

    const std::vector<item *> mags = gun->magazines_current();
    REQUIRE( mags.size() == 1 );
    CHECK( mags[0]->typeId() == itype_id( "glockmag" ) );
    CHECK( mags[0]->ammo_remaining() > 0 );
}

TEST_CASE( "dress_magazine_wells_inserts_default_mag", "[p3][magazine]" )
{
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ), calendar::turn );
    REQUIRE( gun->magazines_current().empty() );

    gun->dress_magazine_wells( /*insert_default_mag=*/true, /*fill_with_default_ammo=*/false );

    const std::vector<item *> mags = gun->magazines_current();
    REQUIRE( mags.size() == 1 );
    CHECK( mags[0]->typeId() == itype_id( "stanag30" ) );
    CHECK( mags[0]->ammo_remaining() == 0 );
}

TEST_CASE( "dress_magazine_wells_leaves_loaded_mag_alone", "[p3][magazine]" )
{
    detached_ptr<item> gun = loaded_m4a1( 10 );
    REQUIRE( gun->magazines_current().size() == 1 );
    const int before = gun->magazines_current().front()->ammo_remaining();
    REQUIRE( before == 10 );

    gun->dress_magazine_wells( /*insert_default_mag=*/false, /*fill_with_default_ammo=*/true );

    REQUIRE( gun->magazines_current().size() == 1 );
    CHECK( gun->magazines_current().front()->ammo_remaining() == before );
}
