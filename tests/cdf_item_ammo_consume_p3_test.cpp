#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"

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

// The pocket_id of the first MAGAZINE_WELL, read from pocket data (never hard-coded).
auto well_pocket_id( const item &it ) -> std::string {
    for( const item_pocket *p : it.get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) && p->get_pocket_data() != nullptr ) {
            return p->get_pocket_data()->pocket_id;
        }
    }
    return std::string();
}

auto loaded_m4a1( int rounds ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, rounds ) );
    gun->put_in( std::move( mag ) );
    return gun;
}

} // namespace

TEST_CASE( "item_contents ammo_consume drains a loaded magazine well", "[p3][ammo]" ) {
    clear_map();
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    CHECK( gun->get_contents().ammo_consume( 10, pos ) == 10 );
    CHECK( gun->ammo_remaining( well_idx ) == 20 );
}

TEST_CASE( "item_contents ammo_consume does not over-drain a magazine", "[p3][ammo]" ) {
    // CDF: stanag30 carries no MAG_DESTROY/MAG_EJECT (data/json/items/magazine/223.json),
    // so an emptied magazine stays in the well.
    clear_map();
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    CHECK( gun->get_contents().ammo_consume( 40, pos ) == 30 );
    CHECK( gun->ammo_remaining( well_idx ) == 0 );
    REQUIRE( gun->magazine_current() != nullptr );
    CHECK( gun->magazine_current()->ammo_remaining() == 0 );
}

TEST_CASE( "item_contents ammo_consume guards", "[p3][ammo]" ) {
    clear_map();
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    CHECK( gun->get_contents().ammo_consume( 0, pos ) == 0 );
    CHECK( gun->ammo_remaining( well_idx ) == 30 );

    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );
    CHECK( backpack->get_contents().ammo_consume( 10, pos ) == 0 );
}

TEST_CASE( "item_contents ammo_consume_in_pocket matches a pocket id", "[p3][ammo]" ) {
    clear_map();
    detached_ptr<item> gun = loaded_m4a1( 30 );
    const int well_idx = pocket_index( *gun, true );
    REQUIRE( well_idx >= 0 );
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    // CDF: no CDF data declares a pocket "id" (data/json/**/*.json has no pocket id;
    // pocket_data.cpp:61 loads it from "id"), so the well's get_pocket_data()->pocket_id
    // is empty. The matching-id path is unreachable with current data (see report), so
    // only the guard paths are covered here.
    const std::string well_id = well_pocket_id( *gun );

    CHECK( gun->get_contents().ammo_consume_in_pocket( well_id, 10, &here, pos ) == 0 );
    CHECK( gun->ammo_remaining( well_idx ) == 30 );

    CHECK( gun->get_contents().ammo_consume_in_pocket( "no_such_pocket", 10, &here, pos ) == 0 );
    CHECK( gun->ammo_remaining( well_idx ) == 30 );

    // the item-level forwarder routes to the same contents method
    CHECK( gun->ammo_consume_in_pocket( "no_such_pocket", 10, here, pos ) == 0 );
    CHECK( gun->ammo_remaining( well_idx ) == 30 );
}

TEST_CASE( "item_contents ammo_consume ejects an emptied MAG_EJECT magazine", "[p3][ammo]" ) {
    // CDF: garandclip is MAG_EJECT (data/json/items/magazine/3006.json) and the garand takes it.
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    detached_ptr<item> gun = item::spawn( itype_id( "garand" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "garandclip" ) );
    mag->put_in( item::spawn( itype_id( "3006" ), calendar::turn, 8 ) );
    gun->put_in( std::move( mag ) );
    REQUIRE( gun->magazine_current() != nullptr );
    REQUIRE( gun->magazine_current()->ammo_remaining() == 8 );

    CHECK( gun->get_contents().ammo_consume( 8, pos ) == 8 );
    CHECK( gun->magazine_current() == nullptr );
    REQUIRE( here.i_at( pos ).size() == 1 );
    CHECK( here.i_at( pos ).only_item().typeId() == itype_id( "garandclip" ) );
}
