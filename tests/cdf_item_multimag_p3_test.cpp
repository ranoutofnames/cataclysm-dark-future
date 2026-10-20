// DDA source: tests/multimag_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASEs: single_well_magazine_operations (DDA:132),
// dual_well_magazine_operations (DDA:190), per_well_ammo_remaining_capacity_overloads
// (DDA:406), wield_collision_sees_mags_from_every_well (DDA:1156),
// primary_ammo_pocket_selection_order (DDA:1342),
// multimag_ammo_capacity_resolves_per_pocket (DDA:2242).

#include <set>
#include <vector>

#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_type.h"
#include "type_id.h"

static const ammotype ammo_9mm( "9mm" );
static const ammotype ammo_battery( "battery" );

static const itype_id itype_556( "556" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_heavy_battery_cell( "heavy_battery_cell" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_test_multimag_gun( "test_multimag_gun" );
static const itype_id itype_test_multimag_gun_consume( "test_multimag_gun_consume" );
static const itype_id itype_test_multimag_gun_integral_ammo( "test_multimag_gun_integral_ammo" );

namespace {

// CDF: item's move constructor is deleted (src/item.h:241); DDA returns the built
// item by value. A named local therefore cannot be returned directly, so copy it
// explicitly into the return value.
static item make_loaded_glock()
{
    item mag( itype_glockmag );
    mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    item gun( itype_glock_19 );
    gun.put_in( mag, pocket_type::MAGAZINE_WELL );
    return item( gun );
}

static item make_loaded_stanag()
{
    item mag( itype_stanag30 );
    mag.put_in( item( itype_556, calendar::turn, 30 ), pocket_type::MAGAZINE );
    return item( mag );
}

static item make_dual_well_gun()
{
    item gun( itype_test_multimag_gun );
    item glock_mag( itype_glockmag );
    glock_mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun.put_in( glock_mag, pocket_type::MAGAZINE_WELL );
    gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
    return item( gun );
}

// Build a fully-loaded multimag gun with both wells populated.
// ammo well: glockmag with `bb_count` rounds of 9mm (capped to mag capacity).
// power well: heavy_battery_cell with `batt` charges.
static item make_multimag_consume_gun( int bb_count = 15, int batt = 100 )
{
    item gun( itype_test_multimag_gun_consume );
    item bb_mag( itype_glockmag );
    bb_mag.put_in( item( itype_9mm, calendar::turn, bb_count ), pocket_type::MAGAZINE );
    item batt_mag( itype_heavy_battery_cell );
    batt_mag.put_in( item( itype_battery, calendar::turn, batt ), pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun );
}

} // namespace

TEST_CASE( "single_well_magazine_operations", "[multimag][p3]" )
{
    SECTION( "ammo_remaining and magazine_current" ) {
        item gun = make_loaded_glock();

        CHECK( gun.ammo_remaining() == 15 );
        REQUIRE( gun.magazine_current() != nullptr );
        CHECK( gun.magazine_current()->typeId() == itype_glockmag );
    }

    SECTION( "ammo_consume drains loaded magazine" ) {
        clear_map();
        map &here = get_map();
        // CDF: tripoint_bub_ms::zero is a function here, not a DDA zero object.
        tripoint_bub_ms pos( tripoint_bub_ms::zero() );
        item gun = make_loaded_glock();

        CHECK( gun.ammo_consume( 3, here, pos, nullptr ) == 3 );
        CHECK( gun.ammo_remaining() == 12 );
        CHECK( gun.ammo_consume( 12, here, pos, nullptr ) == 12 );
        CHECK( gun.ammo_remaining() == 0 );
    }

    SECTION( "first_ammo returns loaded ammo type" ) {
        item gun = make_loaded_glock();
        CHECK( gun.first_ammo().typeId() == itype_9mm );
    }

    SECTION( "first_ammo on empty gun" ) {
        item gun( itype_glock_19 );
        CHECK( gun.first_ammo().is_null() );
    }

    SECTION( "magazines_current" ) {
        item gun = make_loaded_glock();
        std::vector<item *> mags = gun.magazines_current();
        REQUIRE( mags.size() == 1 );
        CHECK( mags[0] == gun.magazine_current() );

        item empty_gun( itype_glock_19 );
        CHECK( empty_gun.magazines_current().empty() );
    }
}

TEST_CASE( "dual_well_magazine_operations", "[multimag][p3]" )
{
    SECTION( "magazines_current with both loaded" ) {
        item gun = make_dual_well_gun();
        CHECK( gun.magazines_current().size() == 2 );
    }

    SECTION( "magazines_current with only second loaded" ) {
        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );

        std::vector<item *> mags = gun.magazines_current();
        REQUIRE( mags.size() == 1 );
        REQUIRE( gun.magazine_current() != nullptr );
        CHECK( mags[0] == gun.magazine_current() );
    }

    SECTION( "ammo_consume with first well empty" ) {
        clear_map();
        map &here = get_map();
        // CDF: tripoint_bub_ms::zero is a function here, not a DDA zero object.
        tripoint_bub_ms pos( tripoint_bub_ms::zero() );

        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );

        CHECK( gun.ammo_remaining() == 30 );
        CHECK( gun.ammo_consume( 10, here, pos, nullptr ) == 10 );
        CHECK( gun.ammo_remaining() == 20 );
    }

    SECTION( "ammo_consume drains both wells" ) {
        clear_map();
        map &here = get_map();
        // CDF: tripoint_bub_ms::zero is a function here, not a DDA zero object.
        tripoint_bub_ms pos( tripoint_bub_ms::zero() );

        item gun = make_dual_well_gun();

        CHECK( gun.ammo_consume( 20, here, pos, nullptr ) == 20 );
        std::vector<item *> remaining = gun.magazines_current();
        REQUIRE( remaining.size() == 2 );
        CHECK( remaining[0]->ammo_remaining() == 0 );
        CHECK( remaining[1]->ammo_remaining() == 25 );
    }

    SECTION( "first_ammo with first well empty" ) {
        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
        CHECK( gun.first_ammo().typeId() == itype_556 );
    }

    SECTION( "first_ammo with both wells empty" ) {
        item gun( itype_test_multimag_gun );
        CHECK( gun.first_ammo().is_null() );
    }
}

TEST_CASE( "per_well_ammo_remaining_capacity_overloads",
           "[multimag][capacity][p3]" )
{
    item gun = make_dual_well_gun();
    REQUIRE( gun.magazines_current().size() == 2 );
    // Drain a few rounds out of the first well so the per-well numbers diverge
    // from the second well's full count.
    {
        clear_map();
        map &here = get_map();
        // CDF: tripoint_bub_ms::zero is a function here, not a DDA zero object.
        const tripoint_bub_ms pos( tripoint_bub_ms::zero() );
        REQUIRE( gun.ammo_consume( 5, here, pos, nullptr ) == 5 );
    }

    // Compute well indices from the fixture; do not hardcode.
    int glock_idx = -1;
    int stanag_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( const item *mag = p->magazine_current() ) {
                    if( mag->typeId() == itype_glockmag ) {
                        glock_idx = idx;
                    } else if( mag->typeId() == itype_stanag30 ) {
                        stanag_idx = idx;
                    }
                }
            }
            ++idx;
        }
    }
    REQUIRE( glock_idx >= 0 );
    REQUIRE( stanag_idx >= 0 );

    SECTION( "per-well ammo_remaining returns each well's loaded count" ) {
        CHECK( gun.ammo_remaining( glock_idx ) == 10 );
        CHECK( gun.ammo_remaining( stanag_idx ) == 30 );
        // Aggregate is the sum.
        CHECK( gun.ammo_remaining() == 40 );
    }

    SECTION( "per-well ammo_capacity returns each loaded magazine's capacity" ) {
        CHECK( gun.ammo_capacity( glock_idx ) == 15 );
        CHECK( gun.ammo_capacity( stanag_idx ) == 30 );
    }

    SECTION( "per-well remaining_ammo_capacity is per-mag, not item-aggregate" ) {
        CHECK( gun.remaining_ammo_capacity( glock_idx ) == 5 );
        CHECK( gun.remaining_ammo_capacity( stanag_idx ) == 0 );
    }

    SECTION( "out-of-range or non-well indices return 0" ) {
        CHECK( gun.ammo_remaining( -1 ) == 0 );
        CHECK( gun.ammo_remaining( 999 ) == 0 );
        CHECK( gun.ammo_capacity( -1 ) == 0 );
        CHECK( gun.remaining_ammo_capacity( -1 ) == 0 );
    }
}

TEST_CASE( "wield_collision_sees_mags_from_every_well",
           "[multimag][wield][p3]" )
{
    item gun = make_dual_well_gun();
    const std::vector<item *> mags = gun.magazines_current();
    REQUIRE( mags.size() == 2 );
    std::set<itype_id> mag_types;
    for( const item *m : mags ) {
        mag_types.insert( m->typeId() );
    }
    CHECK( mag_types.count( itype_glockmag ) == 1 );
    CHECK( mag_types.count( itype_stanag30 ) == 1 );
}

TEST_CASE( "primary_ammo_pocket_selection_order",
           "[multimag][consume][p3]" )
{
    SECTION( "loaded primary-ammo well preferred over earlier non-matching well" ) {
        // Power well listed first in the JSON; ammo well listed second.
        // gun.ammo includes 9mm (the ammo well's ammotype) but not battery.
        item gun = make_multimag_consume_gun();
        const item_pocket *primary = gun.primary_ammo_pocket();
        REQUIRE( primary != nullptr );
        const item *mag = primary->magazine_current();
        REQUIRE( mag != nullptr );
        CHECK( mag->typeId() == itype_glockmag );
    }

    SECTION( "ammo_data routes through primary_ammo_pocket" ) {
        item gun = make_multimag_consume_gun();
        const itype *adata = gun.ammo_data();
        REQUIRE( adata != nullptr );
        CHECK( adata->get_id() == itype_9mm );
    }
}

TEST_CASE( "multimag_ammo_capacity_resolves_per_pocket", "[multimag][capacity][p3]" )
{
    item gun( itype_test_multimag_gun_integral_ammo );
    REQUIRE( gun.uses_firing_requirements() );

    // Empty: still reports projectile pocket capacity for 9mm.
    CHECK( gun.ammo_capacity( ammo_9mm ) == 1 );

    item battery( itype_heavy_battery_cell );
    battery.ammo_set( itype_battery, 100 );
    REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );

    // Battery loaded, slug pocket empty: slug capacity still from integral pocket.
    CHECK( gun.ammo_capacity( ammo_9mm ) == 1 );
    CHECK( gun.ammo_capacity( ammo_battery ) ==
           battery.ammo_capacity( ammo_battery ) );
}
