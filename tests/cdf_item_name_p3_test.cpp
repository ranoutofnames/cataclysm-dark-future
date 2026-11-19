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
#include "itype_slots/islot_magazine.h"

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

namespace
{
// CDF: DDA multimag_test.cpp:2139-2140 (`item gun( itype_test_multimag_gun_integral_ammo )`);
// detached_ptr instead of a value item
auto make_integral_gun() -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_integral_ammo" ) );
    REQUIRE( gun->uses_firing_requirements() );
    return gun;
}

// CDF: DDA multimag_test.cpp:2157-2158 (heavy cell loaded with `battery`); detached_ptr, charge count
// as a parameter
auto make_battery_cell( int charges ) -> detached_ptr<item>
{
    detached_ptr<item> battery = item::spawn( itype_id( "heavy_battery_cell" ) );
    battery->ammo_set( itype_id( "battery" ), charges );
    return battery;
}
} // namespace

// DDA tests/multimag_test.cpp 2135-2178, name and section names kept (D11); tag [p3][name] instead of
// [multimag][display]. The DDA CHECKs are kept as written; the exact-text and raw-colour CHECKs are
// CDF additions (the DDA substrings alone also hold for the old single-well text).
TEST_CASE( "display_name_integral_magazine_per_pocket_segments", "[p3][name]" )
{
    clear_all_state();
    detached_ptr<item> gun = make_integral_gun();
    // CDF: pin the names and the battery capacity the expected strings below are derived from
    REQUIRE( itype_id( "9mm" )->nname( 1 ) == "9x19mm JHP" );
    REQUIRE( itype_id( "battery" )->nname( 1 ) == "battery" );
    REQUIRE( itype_id( "heavy_battery_cell" )->magazine->capacity == 1000 );

    SECTION( "slug only" ) {
        REQUIRE( gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ),
                              pocket_type::MAGAZINE ).success() );
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        // Slug pocket loaded; battery pocket renders as "0/<cap>".
        CHECK( name.find( "1/1" ) != std::string::npos );
        // Per-pocket renderer must not print "1/0" - that is the aggregate
        // path applying slug count against an empty battery capacity.
        CHECK( name.find( "1/0" ) == std::string::npos );
        // CDF: exact text (1 = integral 9mm restriction, 1000 = heavy_battery_cell capacity), the
        // empty well projects its default magazine and default ammo
        CHECK( name == base + " (1/1 9x19mm JHP, 0/1000 battery)" );
        const std::string raw = gun->display_name();
        CHECK( raw.find( "<color_c_white>1/1</color>" ) != std::string::npos );
        CHECK( raw.find( "<color_c_light_red>0/1000</color>" ) != std::string::npos );
    }

    SECTION( "slug + battery does not aggregate" ) {
        REQUIRE( gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ),
                              pocket_type::MAGAZINE ).success() );
        REQUIRE( gun->put_in( make_battery_cell( 100 ), pocket_type::MAGAZINE_WELL ).success() );

        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        // Slug pocket shows 1/1, battery pocket shows 100/<cap>. The
        // aggregate "101/<cap>" or "1/100" forms must never appear.
        CHECK( name.find( "1/1" ) != std::string::npos );
        CHECK( name.find( "100/" ) != std::string::npos );
        // CDF: the two forbidden aggregate forms of the DDA comment, and the exact text
        CHECK( name.find( "101/" ) == std::string::npos );
        CHECK( name.find( "1/100" ) == std::string::npos );
        CHECK( name == base + " (1/1 9x19mm JHP, 100/1000 battery)" );
        // CDF: 100/1000 = 0.10 is below 1/3 (c_red, DDA item.cpp 1746)
        const std::string raw = gun->display_name();
        CHECK( raw.find( "<color_c_white>1/1</color>" ) != std::string::npos );
        CHECK( raw.find( "<color_c_red>100/1000</color>" ) != std::string::npos );
    }

    SECTION( "battery only does not borrow slug count" ) {
        REQUIRE( gun->put_in( make_battery_cell( 50 ), pocket_type::MAGAZINE_WELL ).success() );
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        CHECK( name.find( "0/1" ) != std::string::npos );
        CHECK( name.find( "50/" ) != std::string::npos );
        // CDF: the empty slug pocket projects the 9mm restriction (1) and the default 9mm ammo
        CHECK( name == base + " (0/1 9x19mm JHP, 50/1000 battery)" );
        const std::string raw = gun->display_name();
        CHECK( raw.find( "<color_c_light_red>0/1</color>" ) != std::string::npos );
        CHECK( raw.find( "<color_c_red>50/1000</color>" ) != std::string::npos );
    }

    SECTION( "CDF: empty gun shows both pockets at zero" ) {
        // CDF: DDA enters the per-pocket path with nothing loaded (two ammo pockets on a multimag
        // host); a spawned gun has an empty well (item::spawn does not dress it).
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        CHECK( name == base + " (0/1 9x19mm JHP, 0/1000 battery)" );
        const std::string raw = gun->display_name();
        CHECK( raw.find( "<color_c_light_red>0/1</color>" ) != std::string::npos );
        CHECK( raw.find( "<color_c_light_red>0/1000</color>" ) != std::string::npos );
    }
}

// DDA tests/multimag_test.cpp 1524-1534, name kept (D11); tag [p3][name] instead of [multimag][mod].
// CDF: p3_multimag_gun_integral_ammo (multimag_p3.json 4) instead of test_multimag_gun_integral_ammo:
// explicit pocket_data suppresses the generated MOD pocket, so the DDA fixture cannot take the gunmod
// here (same swap as tests/cdf_item_pocket_p3_test.cpp 132-135).
TEST_CASE( "capacity_mods_reflected_in_item_name", "[p3][name]" )
{
    clear_all_state();
    detached_ptr<item> gun = item::spawn( itype_id( "p3_multimag_gun_integral_ammo" ) );
    REQUIRE( gun->uses_firing_requirements() );

    SECTION( "capacity gunmod doubles the integral pocket in the name" ) {
        REQUIRE( gun->put_in( item::spawn( itype_id( "test_multimag_capacity_gunmod" ) ),
                              pocket_type::MOD ).success() );
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        // the integral ammo pocket capacity doubles 1 -> 2; empty it shows as 0/2
        CHECK( name.find( "0/2" ) != std::string::npos );
        // CDF: exact text (capacity_mods multiply 2.0 on pocket `ammo`, items.json 1827; the empty
        // power well is unchanged)
        CHECK( name == base + " (0/2 9x19mm JHP, 0/1000 battery)" );
    }

    SECTION( "CDF: without the mod the same gun reads 0/1" ) {
        // CDF: the base of the doubling above, so the "0/2" CHECK is not satisfied by accident
        const std::string base = remove_color_tags( gun->tname() );
        const std::string name = remove_color_tags( gun->display_name() );
        CAPTURE( base, name );
        CHECK( name == base + " (0/1 9x19mm JHP, 0/1000 battery)" );
    }
}

// CDF-only (Step 4, DDA item.cpp 1825-1842): the AMMO_IN_NAMES text chain. Its guarded fallbacks
// (empty ammo_types()) have no CDF input, so these sections are guards: the loaded and unloaded
// magazine texts must not change.
TEST_CASE( "display_name_ammo_in_names_chain", "[p3][name]" )
{
    clear_all_state();

    SECTION( "option on: loaded magazine names its ammo variant" ) {
        const override_option opt( "AMMO_IN_NAMES", "true" );
        detached_ptr<item> mag = make_loaded_glockmag();
        const std::string base = remove_color_tags( mag->tname() );
        const std::string name = remove_color_tags( mag->display_name() );
        CAPTURE( base, name );
        // CDF: ammo_current() is 9mm, nname "9x19mm JHP"; 15 = glockmag capacity
        CHECK( name == base + " (15/15 9x19mm JHP)" );
    }

    SECTION( "option on: unloaded magazine names its ammotype" ) {
        const override_option opt( "AMMO_IN_NAMES", "true" );
        detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
        const std::string base = remove_color_tags( mag->tname() );
        const std::string name = remove_color_tags( mag->display_name() );
        CAPTURE( base, name );
        // CDF: no ammo loaded, so the ammotype name "9x19mm" (ammo_types.json 113) is used
        CHECK( name == base + " (0/15 9x19mm)" );
    }

    SECTION( "option off: no ammo text" ) {
        const override_option opt( "AMMO_IN_NAMES", "false" );
        detached_ptr<item> mag = make_loaded_glockmag();
        const std::string base = remove_color_tags( mag->tname() );
        const std::string name = remove_color_tags( mag->display_name() );
        CAPTURE( base, name );
        CHECK( name == base + " (15/15)" );
    }
}
