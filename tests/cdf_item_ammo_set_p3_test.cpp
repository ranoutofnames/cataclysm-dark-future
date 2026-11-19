// DDA source: tests/ammo_set_test.cpp (DDA commit b4b6225).
// Ported TEST_CASEs: ammo_set_items_with_MAGAZINE_pockets (DDA:23),
// ammo_set_items_with_CONTAINER_pockets (DDA:382).
// CDF: includes and tags adapted; test bodies are verbatim DDA.
// CDF: M24 GIVEN section parked (see briefs/b10/PARKED-m24-section.cpp.txt).
// Also ported: ..._MAGAZINE_WELL_pockets_with_magazine (DDA:191), ..._without_magazine (DDA:301).

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
#include "itype_slots/islot_magazine.h"
#include "itype_slots/islot_ammo.h"

static const itype_id itype_box_small( "box_small" );
static const itype_id itype_cz75( "cz75_ammo_set_test" );
static const itype_id itype_cz75mag_10rd( "cz75mag_10rd" );
static const itype_id itype_cz75mag_16rd( "cz75mag_16rd" );
static const itype_id itype_cz75mag_20rd( "cz75mag_20rd" );
static const itype_id itype_cz75mag_26rd( "cz75mag_26rd" );

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

// CDF: WELL cases below need the TEST_DATA cz75 override (data/mods/TEST_DATA/ammo_set_p3.json).
TEST_CASE( "ammo_set_items_with_MAGAZINE_WELL_pockets_with_magazine",
           "[p3][ammo][ammo_set][magazine]" )
{
    GIVEN( "CZ 75 B 9mm gun with empty 9mm CZ 75 20-round magazine" ) {
        item cz75( itype_cz75 );
        item cz75mag_20rd( itype_cz75mag_20rd );
        REQUIRE( cz75.is_gun() );
        REQUIRE_FALSE( cz75.is_magazine() );
        REQUIRE( cz75.magazine_current() == nullptr );
        REQUIRE( cz75.magazine_compatible().count( cz75mag_20rd.typeId() ) == 1 );
        REQUIRE( cz75mag_20rd.is_magazine() );
        REQUIRE( cz75mag_20rd.ammo_remaining( ) == 0 );
        REQUIRE( cz75mag_20rd.ammo_default() );
        itype_id ammo_default_id = cz75mag_20rd.ammo_default();
        itype_id ammo9mm_id( "9mm" );
        REQUIRE( ammo_default_id.str() == ammo9mm_id.str() );
        const ammotype &amtype = ammo9mm_id->ammo->type;
        REQUIRE( cz75mag_20rd.ammo_capacity( amtype ) == 20 );
        cz75.put_in( cz75mag_20rd, pocket_type::MAGAZINE_WELL );
        REQUIRE( cz75.magazine_current()->typeId() == itype_cz75mag_20rd );
        REQUIRE( cz75.ammo_capacity( amtype ) == 20 );
        WHEN( "set 9mm ammo in the gun with magazine w/o quantity" ) {
            cz75.ammo_set( ammo9mm_id );
            THEN( "gun and current magazine has 20 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 20 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 20 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun with magazine -1 quantity" ) {
            cz75.ammo_set( ammo9mm_id, -1 );
            THEN( "gun and current magazine has 20 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 20 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 20 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun with magazine 21 quantity" ) {
            cz75.ammo_set( ammo9mm_id, 21 );
            THEN( "gun and current magazine has 20 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 20 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 20 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun with magazine 20 quantity" ) {
            cz75.ammo_set( ammo9mm_id, 20 );
            THEN( "gun and current magazine has 20 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 20 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 20 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun with magazine 12 quantity" ) {
            cz75.ammo_set( ammo9mm_id, 12 );
            THEN( "gun and current magazine has 12 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 12 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 12 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the current magazine of a gun 1 quantity" ) {
            cz75.magazine_current()->ammo_set( ammo9mm_id, 1 );
            THEN( "gun and current magazine has 1 round of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 1 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 1 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun with magazine 0 quantity" ) {
            cz75.ammo_set( ammo9mm_id, 0 );
            THEN( "gun and current magazine has 0 rounds of null" ) {
                CHECK( cz75.ammo_remaining( ) == 0 );
                CHECK( cz75.ammo_current().is_null() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 0 );
                CHECK( cz75.magazine_current()->ammo_current().is_null() );
            }
        }
        WHEN( "set 9mm FMJ ammo in the gun with magazine 10 quantity" ) {
            itype_id ammo9mmfmj_id( "9mmfmj" );
            cz75.ammo_set( ammo9mmfmj_id, 10 );
            THEN( "gun and current magazine has 10 rounds of 9mm FMJ" ) {
                CHECK( cz75.ammo_remaining( ) == 10 );
                CHECK( cz75.ammo_current().str() == ammo9mmfmj_id.str() );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 10 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mmfmj_id.str() );
            }
        }
        WHEN( "set 308 ammo in the 9mm gun with magazine" ) {
            itype_id ammo308_id( "308" );
            std::string dmsg = capture_debugmsg_during( [&cz75, &ammo308_id]() {
                cz75.ammo_set( ammo308_id, 15 );
            } );
            THEN( "get debugmsg with \"Tried to set invalid ammo of 308 for cz75\"" ) {
                CHECK_THAT( dmsg, Catch::Matchers::EndsWith( "Tried to set invalid ammo of 308 for cz75_ammo_set_test" ) );
                AND_THEN( "gun has 0 round of null" ) {
                    CHECK( cz75.ammo_remaining( ) == 0 );
                    CHECK( cz75.ammo_current().is_null() );
                }
            }
        }
    }
}

TEST_CASE( "ammo_set_items_with_MAGAZINE_WELL_pockets_without_magazine",
           "[p3][ammo][ammo_set][magazine]" )
{
    GIVEN( "CZ 75 B 9mm gun w/o magazine" ) {
        item cz75( itype_cz75 );
        itype_id ammo9mm_id( "9mm" );
        REQUIRE( cz75.is_gun() );
        REQUIRE_FALSE( cz75.is_magazine() );
        REQUIRE( cz75.magazine_current() == nullptr );
        REQUIRE( cz75.magazine_compatible().size() == 4 );
        REQUIRE( cz75.magazine_compatible().count( itype_cz75mag_10rd ) == 1 );
        REQUIRE( cz75.magazine_compatible().count( itype_cz75mag_16rd ) == 1 );
        REQUIRE( cz75.magazine_compatible().count( itype_cz75mag_20rd ) == 1 );
        REQUIRE( cz75.magazine_compatible().count( itype_cz75mag_26rd ) == 1 );
        REQUIRE( cz75.magazine_default() == itype_cz75mag_10rd );
        const ammotype &amtype = ammo9mm_id->ammo->type;
        REQUIRE( cz75.ammo_capacity( amtype ) == 0 );
        REQUIRE( !cz75.ammo_default().is_null() );
        REQUIRE( cz75.magazine_default()->magazine->default_ammo.str() == ammo9mm_id.str() );
        WHEN( "set 9mm ammo in the gun w/o magazine w/o quantity" ) {
            cz75.ammo_set( ammo9mm_id );
            THEN( "gun with cz75mag_10rd magazine has 10 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 10 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                REQUIRE( cz75.magazine_current() != nullptr );
                CHECK( cz75.magazine_current()->typeId() == itype_cz75mag_10rd );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 10 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun w/o magazine 19 quantity" ) {
            cz75.ammo_set( ammo9mm_id, 19 );
            THEN( "gun with new cz75mag_20rd magazine has 19 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 19 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                REQUIRE( cz75.magazine_current() != nullptr );
                CHECK( cz75.magazine_current()->typeId() == itype_cz75mag_20rd );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 19 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun w/o magazine 21 quantity" ) {
            cz75.ammo_set( ammo9mm_id, 21 );
            THEN( "gun with new cz75mag_26rd magazine has 21 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 21 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                REQUIRE( cz75.magazine_current() != nullptr );
                CHECK( cz75.magazine_current()->typeId() == itype_cz75mag_26rd );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 21 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 9mm ammo in the gun w/o magazine 9000 quantity" ) {
            cz75.ammo_set( ammo9mm_id, 9000 );
            THEN( "gun with new cz75mag_26rd magazine has 26 rounds of 9mm" ) {
                CHECK( cz75.ammo_remaining( ) == 26 );
                CHECK( cz75.ammo_current().str() == ammo9mm_id.str() );
                REQUIRE( cz75.magazine_current() != nullptr );
                CHECK( cz75.magazine_current()->typeId() == itype_cz75mag_26rd );
                CHECK( cz75.magazine_current()->ammo_remaining( ) == 26 );
                CHECK( cz75.magazine_current()->ammo_current().str() == ammo9mm_id.str() );
            }
        }
        WHEN( "set 308 ammo in the 9mm gun w/o magazine 2 quantity" ) {
            itype_id ammo308_id( "308" );
            std::string dmsg = capture_debugmsg_during( [&cz75, &ammo308_id]() {
                cz75.ammo_set( ammo308_id, 2 );
            } );
            THEN( "get debugmsg with \"Tried to set invalid ammo of 308 for cz75\"" ) {
                REQUIRE( !dmsg.empty() );
                CHECK_THAT( dmsg, Catch::Matchers::EndsWith( "Tried to set invalid ammo of 308 for cz75_ammo_set_test" ) );
                AND_THEN( "gun w/o magazine has 0 round of null" ) {
                    CHECK( cz75.ammo_remaining( ) == 0 );
                    CHECK( cz75.ammo_current().is_null() );
                    CHECK( cz75.magazine_current() == nullptr );
                }
            }
        }
    }
}

