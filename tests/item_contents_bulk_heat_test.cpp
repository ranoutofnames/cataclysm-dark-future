#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "units.h"

// P3-UI9: item_contents bulk-fill forwarders (DDA item_contents.cpp:1610-1621).
// CDF's pocket level is still a no-op (the P1d size cache covers the sums), so the
// contract tested here is that bracketing a fill never changes the reported volume.
// heat_up/cold_up are NOT ported: CDF has no item temperature/specific_energy state.

TEST_CASE( "bulk fill on a container reports the running volume", "[item][pocket][p3]" )
{
    detached_ptr<item> bag = item::spawn( itype_id( "test_backpack" ) );
    bag->begin_bulk_fill();
    units::volume sum = 0_ml;
    for( int i = 0; i < 3; ++i ) {
        detached_ptr<item> cube = item::spawn( itype_id( "test_1kg_cube" ) );
        sum += cube->volume();
        bag->put_in( std::move( cube ) );
        CHECK( bag->contents.contents_volume() == sum );
    }
    CHECK( bag->contents.contents_volume() == sum );
    bag->end_bulk_fill();
    CHECK( bag->contents.contents_volume() == sum );
}

TEST_CASE( "end_bulk_fill leaves exact values", "[item][pocket][p3]" )
{
    detached_ptr<item> bag = item::spawn( itype_id( "test_backpack" ) );
    bag->put_in( item::spawn( itype_id( "test_1kg_cube" ) ) );
    const units::volume before = bag->contents.contents_volume();
    bag->begin_bulk_fill();
    bag->end_bulk_fill();
    bag->begin_bulk_fill();
    bag->put_in( item::spawn( itype_id( "test_1kg_cube" ) ) );
    bag->end_bulk_fill();
    CHECK( bag->contents.contents_volume() == before * 2 );
}
