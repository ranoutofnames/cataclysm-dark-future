#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"

#include <sstream>
#include <string>
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

// tests/cdf_item_pocket_test.cpp:24-30
auto load_pocket( const std::string &json ) -> pocket_data {
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    JsonObject jo = jsin.get_object();
    pocket_data data;
    data.load( jo );
    return data;
}

// A MAGAZINE pocket holding six .40 rounds that takes the 6-round .40 speedloader.
auto speedloader_pocket_data() -> pocket_data {
    return load_pocket( R"({
        "pocket_type": "MAGAZINE",
        "ammo_restriction": { "40": 6 },
        "allowed_speedloaders": [ "40_speedloader6" ]
    })" );
}

auto loaded_speedloader( int rounds ) -> detached_ptr<item> {
    detached_ptr<item> speedloader = item::spawn( itype_id( "40_speedloader6" ) );
    if( rounds > 0 ) {
        speedloader->put_in( item::spawn( itype_id( "40sw" ), calendar::turn, rounds ) );
    }
    return speedloader;
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

TEST_CASE( "uses_firing_requirements is false without itype data", "[p3][ammo]" ) {
    // CDF: no data item sets firing_requirements yet (data port 7-R2), so a gun
    // and a magazine both report false.
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    CHECK_FALSE( gun->uses_firing_requirements() );
    CHECK_FALSE( mag->uses_firing_requirements() );
}

TEST_CASE( "ammo_identity_mag returns the loaded magazine", "[p3][ammo]" ) {
    // No firing requirements: ammo_identity_mag falls through to magazine_current.
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ) );
    item *mag_ptr = &*mag;
    gun->put_in( std::move( mag ) );

    const item *identity = gun->ammo_identity_mag();
    REQUIRE( identity != nullptr );
    CHECK( identity == mag_ptr );
    CHECK( identity->typeId() == itype_id( "stanag30" ) );
    CHECK( identity->ammo_remaining() == 30 );

    detached_ptr<item> unloaded = item::spawn( itype_id( "m4a1" ) );
    CHECK( unloaded->ammo_identity_mag() == nullptr );
}

TEST_CASE( "primary_ammo_pocket finds the magazine well", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item_pocket *p = gun->primary_ammo_pocket();
    REQUIRE( p != nullptr );
    CHECK( p->is_type( pocket_type::MAGAZINE_WELL ) );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK( backpack->primary_ammo_pocket() == nullptr );
}

TEST_CASE( "speedloader accepted by a pocket that allows it", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    detached_ptr<item> speedloader = loaded_speedloader( 6 );

    CHECK( pocket.can_reload_with( *speedloader, true ) );
}

TEST_CASE( "speedloader not in the allowed list is rejected", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    detached_ptr<item> other = item::spawn( itype_id( "38_speedloader" ) );
    other->put_in( item::spawn( itype_id( "38_special" ), calendar::turn, 7 ) );

    CHECK_FALSE( pocket.can_reload_with( *other, true ) );
}

TEST_CASE( "speedloader rejected by a full pocket", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    pocket.insert( item::spawn( itype_id( "40sw" ), calendar::turn, 6 ) );
    REQUIRE( pocket.remaining_ammo_capacity( ammotype( "40" ) ) < 6 );
    detached_ptr<item> speedloader = loaded_speedloader( 6 );

    CHECK_FALSE( pocket.can_reload_with( *speedloader, true ) );
}

TEST_CASE( "empty speedloader is rejected without a crash", "[p3][ammo]" ) {
    const pocket_data data = speedloader_pocket_data();
    item_pocket pocket( &data );
    detached_ptr<item> speedloader = loaded_speedloader( 0 );
    REQUIRE( speedloader->loaded_ammo().is_null() );

    CHECK_FALSE( pocket.can_reload_with( *speedloader, true ) );
}

TEST_CASE( "loaded_ammo returns the speedloader rounds", "[p3][ammo]" ) {
    detached_ptr<item> speedloader = loaded_speedloader( 6 );
    const item &rounds = speedloader->loaded_ammo();
    REQUIRE_FALSE( rounds.is_null() );
    CHECK( rounds.is_ammo() );
    CHECK( rounds.typeId() == itype_id( "40sw" ) );
    CHECK( rounds.count() == 6 );

    detached_ptr<item> empty = item::spawn( itype_id( "40_speedloader6" ) );
    CHECK( empty->loaded_ammo().is_null() );
}

// DDA item_gun_tool_ammo.cpp:1528-1542: the carrier overload and the no-arg
// forwarder agree on a locally loaded gun.
TEST_CASE( "ammo_remaining with a carrier matches the no-arg overload", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );

    CHECK( gun->ammo_remaining( nullptr ) == 30 );
    CHECK( gun->ammo_remaining() == 30 );
}

// Guard: a magazine's own rounds are still summed by the no-arg overload.
TEST_CASE( "ammo_remaining sums a loaded magazine", "[p3][ammo]" ) {
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ) );

    CHECK( mag->ammo_remaining() == 10 );
}

// Guard: a charges-only tool with no ammo type reports its charges (the DDA
// ammo.empty() branch).
TEST_CASE( "ammo_remaining returns charges for a charges-only tool", "[p3][ammo]" ) {
    detached_ptr<item> candle = item::spawn( itype_id( "candle" ) );
    REQUIRE( candle->is_tool() );
    REQUIRE( candle->ammo_types().empty() );

    candle->charges = 37;
    CHECK( candle->ammo_remaining() == 37 );
}

// O1 gate: a tool carrying an ammo type but no magazine pocket keeps its
// direct charges (the // CDF: guard until 4-b2 converts tool ammo).
TEST_CASE( "ammo_remaining keeps direct charges on an ammo tool with no magazine pocket",
           "[p3][ammo]" ) {
    detached_ptr<item> lantern = item::spawn( itype_id( "gasoline_lantern" ) );
    REQUIRE( lantern->is_tool() );
    REQUIRE_FALSE( lantern->ammo_types().empty() );
    REQUIRE_FALSE( lantern->contents.has_pocket_type( pocket_type::MAGAZINE ) );
    REQUIRE_FALSE( lantern->contents.has_pocket_type( pocket_type::MAGAZINE_WELL ) );

    lantern->charges = 5;
    CHECK( lantern->ammo_remaining() == 5 );
}

// DDA item_gun_tool_ammo.cpp:2342-2350: the item-level magazines_current pair
// forwards to item_contents.
TEST_CASE( "item magazines_current lists loaded magazines", "[p3][ammo]" ) {
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item *mag = gun->magazine_current();
    REQUIRE( mag != nullptr );

    const item &const_gun = *gun;
    const std::vector<const item *> const_mags = const_gun.magazines_current();
    REQUIRE( const_mags.size() == 1 );
    CHECK( const_mags.front() == mag );

    std::vector<item *> mags = gun->magazines_current();
    REQUIRE( mags.size() == 1 );
    CHECK( mags.front() == mag );

    detached_ptr<item> unloaded = item::spawn( itype_id( "m4a1" ) );
    CHECK( unloaded->magazines_current().empty() );
}

// DDA item_gun_tool_ammo.cpp:1720-1769: per-ammotype capacity reads the
// matching pocket.
TEST_CASE( "ammo_capacity by ammotype reads the magazine's own pocket", "[p3][ammo]" ) {
    const ammotype round = item::spawn( itype_id( "556" ) )->ammo_type();

    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    CHECK( mag->ammo_capacity( round ) == 30 );

    // CDF: loaded_m4a1( 0 ) still holds an empty stanag30, so its well reports
    // the magazine's 30-round capacity. A m4a1 with no magazine reports 0.
    detached_ptr<item> empty_gun = item::spawn( itype_id( "m4a1" ) );
    CHECK( empty_gun->ammo_capacity( round ) == 0 );

    detached_ptr<item> empty_mag_gun = loaded_m4a1( 0 );
    CHECK( empty_mag_gun->ammo_capacity( round ) == 30 );

    detached_ptr<item> loaded_gun = loaded_m4a1( 30 );
    CHECK( loaded_gun->ammo_capacity( round ) == 30 );
}

// DDA item_gun_tool_ammo.cpp:2481-2527: a loaded well is the pocket for its
// ammotype; items without ammo pockets have none.
TEST_CASE( "pocket_for_ammo returns the matching well", "[p3][ammo]" ) {
    const ammotype round = item::spawn( itype_id( "556" ) )->ammo_type();

    detached_ptr<item> gun = loaded_m4a1( 30 );
    const item_pocket *p = gun->pocket_for_ammo( round );
    REQUIRE( p != nullptr );
    CHECK( p->is_type( pocket_type::MAGAZINE_WELL ) );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK( backpack->pocket_for_ammo( round ) == nullptr );
}
