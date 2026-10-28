// DDA source: tests/ammo_set_test.cpp (DDA commit b4b6225).
// Ported TEST_CASEs: ammo_set_items_with_MAGAZINE_pockets (DDA:23),
// ammo_set_items_with_CONTAINER_pockets (DDA:382).
// CDF: includes and tags adapted; test bodies are verbatim DDA.
// CDF: M24 GIVEN section parked (see briefs/b10/PARKED-m24-section.cpp.txt).

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "itype.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"
#include "value_ptr.h"

static const itype_id itype_box_small( "box_small" );
static const itype_id itype_cz75mag_20rd( "cz75mag_20rd" );

TEST_CASE( "ammo_set_items_with_MAGAZINE_pockets", "[p3][ammo][ammo_set][magazine]" )
{
    GIVEN( "empty 9mm CZ 75 20-round magazine" ) {
        item cz75mag_20rd( itype_cz75mag_20rd );
        REQUIRE( cz75mag_20rd.is_magazine() );
        REQUIRE( cz75mag_20rd.ammo_remaining( ) == 0 );
        REQUIRE( cz75mag_20rd.ammo_default() );
        itype_id ammo_default_id = cz75mag_20rd.ammo_default();
        itype_id ammo9mm_id( "9mm" );
        REQUIRE( ammo_default_id.str() == ammo9mm_id.str() );
        const ammotype &amtype = ammo9mm_id->ammo->type;
        REQUIRE( cz75mag_20rd.ammo_capacity( amtype ) == 20 );
        WHEN( "set 9mm ammo in the magazine w/o quantity" ) {
            cz75mag_20rd.ammo_set( ammo9mm_id );
            THEN( "magazine has 20 rounds of 9mm" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 20 );
                CHECK( cz75mag_20rd.ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the magazine -1 quantity" ) {
            cz75mag_20rd.ammo_set( ammo9mm_id, -1 );
            THEN( "magazine has 20 rounds of 9mm" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 20 );
                CHECK( cz75mag_20rd.ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the magazine 21 quantity" ) {
            cz75mag_20rd.ammo_set( ammo9mm_id, 21 );
            THEN( "magazine has 20 rounds of 9mm" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 20 );
                CHECK( cz75mag_20rd.ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the magazine 20 quantity" ) {
            cz75mag_20rd.ammo_set( ammo9mm_id, 20 );
            THEN( "magazine has 20 rounds of 9mm" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 20 );
                CHECK( cz75mag_20rd.ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the magazine 12 quantity" ) {
            cz75mag_20rd.ammo_set( ammo9mm_id, 12 );
            THEN( "magazine has 12 rounds of 9mm" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 12 );
                CHECK( cz75mag_20rd.ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the magazine 1 quantity" ) {
            cz75mag_20rd.ammo_set( ammo9mm_id, 1 );
            THEN( "magazine has 1 round of 9mm" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 1 );
                CHECK( cz75mag_20rd.ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the magazine 0 quantity" ) {
            cz75mag_20rd.ammo_set( ammo9mm_id, 0 );
            THEN( "magazine has 0 round of null" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 0 );
                CHECK( cz75mag_20rd.ammo_current().is_null() );
            }
        }
        WHEN( "set 9mm FMJ ammo in the magazine 15 quantity" ) {
            itype_id ammo9mmfmj_id( "9mmfmj" );
            cz75mag_20rd.ammo_set( ammo9mmfmj_id, 15 );
            THEN( "magazine has 15 round of 9mm FMJ" ) {
                CHECK( cz75mag_20rd.ammo_remaining( ) == 15 );
                CHECK( cz75mag_20rd.ammo_current().str() == ammo9mmfmj_id.str() );
            }
        }
        WHEN( "set 308 ammo in the 9mm magazine" ) {
            itype_id ammo308_id( "308" );
            std::string dmsg = capture_debugmsg_during( [&cz75mag_20rd, &ammo308_id]() {
                cz75mag_20rd.ammo_set( ammo308_id, 15 );
            } );
            THEN( "get debugmsg with \"Tried to set invalid ammo of 308 for cz75mag_20rd\"" ) {
                CHECK_THAT( dmsg,
                            Catch::Matchers::EndsWith( "Tried to set invalid ammo of 308 for cz75mag_20rd" ) );
                AND_THEN( "magazine has 0 round of null" ) {
                    CHECK( cz75mag_20rd.ammo_remaining( ) == 0 );
                    CHECK( cz75mag_20rd.ammo_current().is_null() );
                }
            }
        }
    }
}

TEST_CASE( "ammo_set_items_with_CONTAINER_pockets", "[p3][ammo][ammo_set][magazine]" )
{
    GIVEN( "small box" ) {
        item box( itype_box_small );
        REQUIRE_FALSE( box.is_gun() );
        REQUIRE_FALSE( box.is_magazine() );
        REQUIRE( box.is_container_empty() );
        REQUIRE( box.magazine_current() == nullptr );
        REQUIRE( box.magazine_compatible().empty() );
        itype_id ammo9mm_id( "9mm" );
        WHEN( "set 9mm ammo in the small box" ) {
            std::string dmsg = capture_debugmsg_during( [&box, &ammo9mm_id]() {
                box.ammo_set( ammo9mm_id, 10 );
            } );
            THEN( "get debugmsg with \"Tried to set invalid ammo of 9mm for box_small\"" ) {
                CHECK_THAT( dmsg, Catch::Matchers::EndsWith( "Tried to set invalid ammo of 9mm for box_small" ) );
                AND_THEN( "small box still empty" ) {
                    REQUIRE_FALSE( box.is_gun() );
                    REQUIRE_FALSE( box.is_magazine() );
                    CHECK( box.is_container_empty() );
                }
            }
        }
    }
}
