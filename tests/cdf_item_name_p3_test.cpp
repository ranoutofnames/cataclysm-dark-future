// Job 5-5a: item names, per-well segments of multi-well items (U7).
// DDA source: tests/multimag_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASE: display_name_multi_well_per_well_counts (DDA:270-351), its first five sections.
// The sixth section (bare HWP, DDA:339-350) is cut, see plans/P3/cut-tests.md.
// CDF-only sections carry the prefix `CDF: `.

#include <string>
#include <utility>

#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "itype.h"
#include "options_helpers.h"
#include "output.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

namespace
{
// CDF: DDA multimag_test.cpp:115-120 make_loaded_stanag; detached_ptr instead of a value item,
// plus a round count so a case can load fewer than 30
auto make_loaded_stanag( int rounds = 30 ) -> detached_ptr<item>
{
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    if( rounds > 0 ) {
        mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, rounds ),
                     pocket_type::MAGAZINE );
    }
    return mag;
}

// CDF: DDA multimag_test.cpp:106-113 loads a glockmag with 15 9mm; detached_ptr, count parameter
auto make_loaded_glockmag( int rounds = 15 ) -> detached_ptr<item>
{
    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    if( rounds > 0 ) {
        mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, rounds ),
                     pocket_type::MAGAZINE );
    }
    return mag;
}

// CDF: DDA multimag_test.cpp:106-113 make_loaded_glock
auto make_loaded_glock() -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "glock_19" ) );
    REQUIRE( gun->put_in( make_loaded_glockmag(), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}

// CDF: DDA multimag_test.cpp:122-130 make_dual_well_gun; round counts as parameters
auto make_dual_well_gun( int glock_rounds = 15, int stanag_rounds = 30 ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun" ) );
    REQUIRE( gun->put_in( make_loaded_glockmag( glock_rounds ),
                          pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->put_in( make_loaded_stanag( stanag_rounds ),
                          pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}
} // namespace

TEST_CASE( "display_name_multi_well_per_well_counts", "[p3][name]" )
{
    clear_all_state();

    SECTION( "single loaded well prints (amount/max ammo) once" ) {
        detached_ptr<item> gun = make_loaded_glock();
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( name );
        CHECK( name.find( "(15/15" ) != std::string::npos );
        // CDF: a single well stays on BN's percentage path (ADV-14 ruling 7): the whole
        // " (15/15 ...)" is one c_green run (100 %), not DDA's c_white "15/15" inside plain
        // parentheses. This is the guard against a wrong multi-well branch condition.
        CHECK( gun->display_name().find( "<color_c_green> (15/15" ) != std::string::npos );
    }

    SECTION( "dual loaded wells: per-well counts in single paren" ) {
        detached_ptr<item> gun = make_dual_well_gun();
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        // CDF: search after the tname so a '(' inside the tname is not taken for the ammo paren
        const size_t open = name.find( '(', base.size() );
        const size_t close = name.find( ')', open );
        REQUIRE( open != std::string::npos );
        REQUIRE( close != std::string::npos );
        const std::string inner = name.substr( open + 1, close - open - 1 );
        CAPTURE( inner );
        // Both well counts appear, comma-separated, inside one outer paren.
        CHECK( inner.find( "15/15" ) != std::string::npos );
        CHECK( inner.find( "30/30" ) != std::string::npos );
        CHECK( inner.find( ',' ) != std::string::npos );
        // CDF: exact text (15 = glockmag capacity, 30 = stanag30 capacity, names of 9mm and 556)
        CHECK( name == base + " (15/15 9x19mm JHP, 30/30 5.56x45mm NATO M855A1)" );
        // CDF: full wells are c_white (DDA item.cpp 1740)
        const std::string raw = gun->display_name();
        CHECK( raw.find( "<color_c_white>15/15</color>" ) != std::string::npos );
        CHECK( raw.find( "<color_c_white>30/30</color>" ) != std::string::npos );
    }

    SECTION( "one well loaded out of two still emits per-well segments" ) {
        const override_option opt( "AMMO_IN_NAMES", "false" );
        detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun" ) );
        REQUIRE( gun->put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL ).success() );
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        const size_t open = name.find( '(', base.size() );
        const size_t close = name.find( ')', open );
        REQUIRE( open != std::string::npos );
        REQUIRE( close != std::string::npos );
        const std::string inner = name.substr( open + 1, close - open - 1 );
        CAPTURE( inner );
        CHECK( inner.find( "0/15" ) != std::string::npos );
        CHECK( inner.find( "30/30" ) != std::string::npos );
        CHECK( inner.find( ',' ) != std::string::npos );
        // CDF: the empty first well projects its default magazine (glockmag, capacity 15, default
        // ammo 9mm) and names that ammo; the filled second well names 556
        CHECK( inner == "0/15 9x19mm JHP, 30/30 5.56x45mm NATO M855A1" );
        // CDF: an empty well is c_light_red (DDA item.cpp 1741)
        CHECK( gun->display_name().find( "<color_c_light_red>0/15</color>" ) !=
               std::string::npos );
    }

    SECTION( "dual loaded wells use variant ammo names, not generic class names" ) {
        const override_option opt( "AMMO_IN_NAMES", "true" );
        detached_ptr<item> gun = make_dual_well_gun();
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( name );
        // Variant nname (e.g. "9x19mm JHP") not the generic ammotype name.
        // CDF: DDA item::find_type( id )->nname( 1 ) does not exist; itype_id::operator-> resolves
        // the itype. The two names are pinned so the section cannot compare production with itself.
        const std::string variant_9mm = itype_id( "9mm" )->nname( 1 );
        const std::string variant_556 = itype_id( "556" )->nname( 1 );
        REQUIRE( variant_9mm == "9x19mm JHP" );
        REQUIRE( variant_556 == "5.56x45mm NATO M855A1" );
        CHECK( name.find( variant_9mm ) != std::string::npos );
        CHECK( name.find( variant_556 ) != std::string::npos );
    }

    SECTION( "AMMO_IN_NAMES off: dual wells still label per-well ammo" ) {
        // Multi-well items always label per-well ammo regardless of AMMO_IN_NAMES so distinct
        // ammotypes sharing one host stay distinguishable in the inventory tname.
        const override_option opt( "AMMO_IN_NAMES", "false" );
        detached_ptr<item> gun = make_dual_well_gun();
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        const std::string variant_9mm = itype_id( "9mm" )->nname( 1 );
        const std::string variant_556 = itype_id( "556" )->nname( 1 );
        REQUIRE( variant_9mm == "9x19mm JHP" );
        REQUIRE( variant_556 == "5.56x45mm NATO M855A1" );
        CHECK( name.find( variant_9mm ) != std::string::npos );
        CHECK( name.find( variant_556 ) != std::string::npos );
        CHECK( name.find( "15/15" ) != std::string::npos );
        CHECK( name.find( "30/30" ) != std::string::npos );
        // CDF: exact text, identical to the option-on text
        CHECK( name == base + " (15/15 9x19mm JHP, 30/30 5.56x45mm NATO M855A1)" );
    }

    SECTION( "CDF: empty dual-well gun shows both wells at zero" ) {
        // CDF: DDA enters the per-well path with no magazine loaded (two wells); a spawned gun has
        // empty wells (item::spawn does not dress them). Both wells project their default magazine.
        detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun" ) );
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        CHECK( name == base + " (0/15 9x19mm JHP, 0/30 5.56x45mm NATO M855A1)" );
        const std::string raw = gun->display_name();
        CHECK( raw.find( "<color_c_light_red>0/15</color>" ) != std::string::npos );
        CHECK( raw.find( "<color_c_light_red>0/30</color>" ) != std::string::npos );
    }

    SECTION( "CDF: segment colours follow DDA's thresholds" ) {
        // CDF: DDA item.cpp 1740-1753. 4/15 = 0.27 is below 1/3 (c_red), 24/30 = 0.80 is neither
        // below 2/3 nor full (c_light_green), 9/15 = 0.60 is below 2/3 (c_yellow), full is c_white.
        {
            detached_ptr<item> gun = make_dual_well_gun( 4, 24 );
            const std::string raw = gun->display_name();
            CAPTURE( raw );
            CHECK( raw.find( "<color_c_red>4/15</color>" ) != std::string::npos );
            CHECK( raw.find( "<color_c_light_green>24/30</color>" ) != std::string::npos );
        }
        {
            detached_ptr<item> gun = make_dual_well_gun( 9, 30 );
            const std::string raw = gun->display_name();
            CAPTURE( raw );
            CHECK( raw.find( "<color_c_yellow>9/15</color>" ) != std::string::npos );
            CHECK( raw.find( "<color_c_white>30/30</color>" ) != std::string::npos );
        }
    }
}
