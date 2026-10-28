#include "catch/catch.hpp"
#include "item.h"
#include "type_id.h"

// Ported from DDA tests/ammo_test.cpp (ammo_types case, b4b6225), TOOL sections only.
// Depends on 4-b2 (integral MAGAZINE pocket synthesized for legacy TOOL json).
// The GUN sections are parked in briefs/b95/parked-B4-gun-sections.cpp: legacy GUN json has no pocket_data.

namespace {

// Return true if the given item has non-empty ammo_types
auto has_ammo_types( const item &it ) -> bool {
    return !it.ammo_types().empty();
}

} // namespace

// CDF: split from DDA "ammo_types" (GUN sections cannot pass yet), items built with item::spawn.
TEST_CASE( "ammo_types_tools", "[ammo][ammo_types][p3]" ) {
    SECTION( "TOOL items with integral MAGAZINE pockets have ammo_types" ) {
        detached_ptr<item> sewing_kit = item::spawn( itype_id( "sewing_kit" ) );
        REQUIRE( sewing_kit->magazine_integral() );

        CHECK( has_ammo_types( *item::spawn( itype_id( "needle_bone" ) ) ) );
        CHECK( has_ammo_types( *item::spawn( itype_id( "needle_wood" ) ) ) );
        CHECK( has_ammo_types( *sewing_kit ) );
        CHECK( has_ammo_types( *item::spawn( itype_id( "tailors_kit" ) ) ) );
    }

    SECTION( "TOOL items with NO pockets have ammo_types" ) {
        // NOTE: Fish trap is a TOOL with "ammo", but no "pocket_data", so an implicit MAGAZINE
        // pocket is added on JSON load.
        detached_ptr<item> fish_trap = item::spawn( itype_id( "fish_trap" ) );
        REQUIRE( fish_trap->magazine_integral() );

        CHECK( has_ammo_types( *fish_trap ) );
    }
}

// CDF: split from DDA "ammo_types". The DDA expectation that GUN items with integral MAGAZINE
// pockets report ammo_types holds in CDF (items built with item::spawn).
TEST_CASE( "ammo_types_guns", "[ammo][ammo_types][p3]" ) {
    SECTION( "GUN items with integral MAGAZINE pockets have ammo_types" ) {
        detached_ptr<item> nailgun = item::spawn( itype_id( "nailgun" ) );
        detached_ptr<item> colt_army = item::spawn( itype_id( "colt_army" ) );
        REQUIRE( nailgun->magazine_integral() );
        REQUIRE( colt_army->magazine_integral() );

        CHECK( has_ammo_types( *nailgun ) );
        CHECK( has_ammo_types( *colt_army ) );
        CHECK( has_ammo_types( *item::spawn( itype_id( "hand_crossbow" ) ) ) );
        CHECK( has_ammo_types( *item::spawn( itype_id( "compositebow" ) ) ) );
        CHECK( has_ammo_types( *item::spawn( itype_id( "sling" ) ) ) );
        CHECK( has_ammo_types( *item::spawn( itype_id( "slingshot" ) ) ) );
    }
}
