// Job 9-1a-a: item info, compatible lists (`Compatible guns` on magazines, `Compatible magazines`
// block on guns).
// DDA source: tests/iteminfo_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASE: gun_or_other_ranged_weapon_attributes (DDA:1764), ONLY the section
// `compatible magazines` (DDA:1882-1907). The other sections (DDA:1772-1881 and 1909-1957) are
// cut by 9-1a-a, see plans/P3/cut-tests.md; `ammo capacity of weapon` is ported by 9-1a-b.
// CDF-only: magazine_info_lists_compatible_guns (DDA has no test for `Compatible guns`).

#include <string>
#include <vector>

#include "catch/catch.hpp"
#include "item.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "options_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

namespace
{
auto iteminfo_p3_str( const item &it, const std::vector<iteminfo_parts> &parts ) -> std::string
{
    const iteminfo_query query( parts );
    return it.info_string( query, 1 );
}
} // namespace

TEST_CASE( "magazine_info_lists_compatible_guns", "[p3][iteminfo]" )
{
    clear_all_state();

    SECTION( "exactly one gun" ) {
        const item mag( itype_id( "test_glockmag" ) );
        REQUIRE( mag.is_magazine() );
        REQUIRE_FALSE( mag.is_tool() );
        const std::string s = iteminfo_p3_str( mag, { iteminfo_parts::MAGAZINE_COMPATIBLE_GUNS } );
        // CDF: only `test_glock` lists `test_glockmag` in a well (dda_pocket_items.json 1034-1070);
        // its name is `Test Glock`.
        CHECK( s.find( "<color_c_white>Compatible guns</color>: Test Glock\n" ) !=
               std::string::npos );
    }

    SECTION( "many guns" ) {
        const item mag( itype_id( "stanag30" ) );
        REQUIRE( mag.is_magazine() );
        REQUIRE_FALSE( mag.is_tool() );
        const std::string s = iteminfo_p3_str( mag, { iteminfo_parts::MAGAZINE_COMPATIBLE_GUNS } );
        CHECK( s.find( "<color_c_white>Compatible guns</color>: " ) != std::string::npos );
        CHECK( s.find( "M4A1" ) != std::string::npos );
        CHECK( s.find( "test multimag gun" ) != std::string::npos );
        // CDF: `glock_19` lists only 9mm magazines (9mm.json 92-105).
        CHECK( s.find( "Glock 19" ) == std::string::npos );
    }

    SECTION( "part off" ) {
        const item mag( itype_id( "test_glockmag" ) );
        REQUIRE( mag.is_magazine() );
        const std::string s = iteminfo_p3_str( mag, { iteminfo_parts::MAGAZINE_CAPACITY } );
        CHECK( s.find( "Compatible guns" ) == std::string::npos );
    }
}

TEST_CASE( "gun_or_other_ranged_weapon_attributes", "[p3][iteminfo][weapon][gun]" )
{
    clear_all_state();

    item compbow( itype_id( "test_compbow" ) );
    item glock( itype_id( "glock_19" ) );
    item rag( itype_id( "test_rag" ) );

    SECTION( "compatible magazines" ) {
        const std::vector<iteminfo_parts> allowed_mags = { iteminfo_parts::GUN_ALLOWED_MAGAZINES };

        REQUIRE( compbow.magazine_integral() );
        REQUIRE( compbow.magazine_compatible().empty() );
        CHECK( iteminfo_p3_str( compbow, allowed_mags ).empty() );

        REQUIRE_FALSE( glock.magazine_integral() );
        REQUIRE_FALSE( glock.magazine_compatible().empty() );
        REQUIRE( glock.uses_magazine() );
        // CDF: `glock_19` has an `ascii_picture`, which item::final_info appends as DESCRIPTION lines
        // when ENABLE_ASCII_ART_ITEM is on (item.cpp, end of final_info); DDA's `test_glock` has none.
        override_option opt_art( "ENABLE_ASCII_ART_ITEM", "false" );
        // CDF: DDA used `test_glock` and its two `test_glock*` magazines; CDF's `test_glock` has no
        // `magazines` key, so `magazine_compatible()` (BN body, 3-5a OD-3) is empty for it, and CDF
        // has no second Test Glock magazine. The literal is `glock_19`'s seven magazines in
        // lexicographic id order (glock17_17, glock17_22, glock_drum_100rd, glock_drum_50rd,
        // glockbigmag, glockmag, kriss_vector_9mm_mag; names from data/json/items/magazine/9mm.json).
        // The real list has an eighth entry, TEST_DATA `glockmag_test` (items.json 810, name
        // `TEST Magazine`, copy-from glockmag), which sorts between glockmag and kriss_vector_9mm_mag.
        CHECK( iteminfo_p3_str( glock, allowed_mags ) ==
               "--\n"
               "<color_c_white>Compatible magazines</color>:\n"
               "<color_c_white>Types</color>: Glock 17 17-round magazine, Glock 17 22-round magazine, "
               "Glock 100-round drum magazine, Glock 50-round drum magazine, Glock extended magazine, "
               "Glock magazine, TEST Magazine, and Kriss Vector 9mm magazine\n" );

        REQUIRE_FALSE( rag.magazine_integral() );
        REQUIRE( rag.magazine_compatible().empty() );
        CHECK( iteminfo_p3_str( rag, allowed_mags ).empty() );
    }
}
