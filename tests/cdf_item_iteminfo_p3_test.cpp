// Job 9-1a-a: item info, compatible lists (`Compatible guns` on magazines, `Compatible magazines`
// block on guns).
// DDA source: tests/iteminfo_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASE: gun_or_other_ranged_weapon_attributes (DDA:1764), ONLY the section
// `compatible magazines` (DDA:1882-1907). The other sections (DDA:1772-1881 and 1909-1957) are
// cut by 9-1a-a, see plans/P3/cut-tests.md; `ammo capacity of weapon` is ported by 9-1a-b.
// CDF-only: magazine_info_lists_compatible_guns (DDA has no test for `Compatible guns`).

#include <string>
#include <vector>

#include "ammo.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "options_helpers.h"
#include "output.h"
#include "pocket_type.h"
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

    // DDA tests/iteminfo_test.cpp 1809-1818 (9-1a-a ledger row: ported by 9-1a-b).
    SECTION( "ammo capacity of weapon" ) {
        const std::vector<iteminfo_parts> gun_capacity = { iteminfo_parts::GUN_CAPACITY };
        override_option opt_art( "ENABLE_ASCII_ART_ITEM", "false" );

        // CDF: DDA's literal. test_compbow is a BN integral gun (clip_size 1, no MAGAZINE pocket), so
        // gun_info keeps the zero-argument ammo_capacity() for it (the same text is pinned by
        // tests/iteminfo_test.cpp "ranged weapon attributes").
        REQUIRE( compbow.magazine_integral() );
        CHECK( iteminfo_p3_str( compbow, gun_capacity ) ==
               "--\n"
               "Capacity: <color_c_yellow>1</color> round of arrows\n" );

        // DDA: "FIXME: Why empty?": an unloaded gun with a magazine well has nothing to report
        // (the gate is magazine_integral() or a loaded magazine). CDF's glock_19 is DDA's test_glock.
        REQUIRE_FALSE( glock.magazine_integral() );
        CHECK( iteminfo_p3_str( glock, gun_capacity ).empty() );
    }

    // DDA tests/iteminfo_test.cpp 1856-1865 (the DDA TODOs are covered by
    // iteminfo_magazine_label_pluralization).
    SECTION( "gun type and current magazine" ) {
        const std::vector<iteminfo_parts> magazine = { iteminfo_parts::GUN_MAGAZINE };
        override_option opt_art( "ENABLE_ASCII_ART_ITEM", "false" );

        // When unloaded, the "Magazine:" section should not be shown
        CHECK( iteminfo_p3_str( glock, magazine ).empty() );
    }
}

namespace
{
// DDA tests/multimag_test.cpp 106-130 (make_loaded_glock, make_loaded_stanag, make_dual_well_gun) and
// tests/cdf_item_firing_p3_test.cpp (consume gun and tool builders).
// CDF: item's move constructor is deleted (src/item.h:241), so each builder returns a copy.
auto iteminfo_p3_plain( const item &it, const std::vector<iteminfo_parts> &parts ) -> std::string
{
    return remove_color_tags( iteminfo_p3_str( it, parts ) );
}

auto iteminfo_p3_loaded_glock() -> item
{
    item mag( itype_id( "glockmag" ) );
    mag.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    item gun( itype_id( "glock_19" ) );
    gun.put_in( mag, pocket_type::MAGAZINE_WELL );
    return item( gun );
}

auto iteminfo_p3_dual_well_gun() -> item
{
    item gun( itype_id( "test_multimag_gun" ) );
    item glock_mag( itype_id( "glockmag" ) );
    glock_mag.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    item stanag_mag( itype_id( "stanag30" ) );
    stanag_mag.put_in( item( itype_id( "556" ), calendar::turn, 30 ), pocket_type::MAGAZINE );
    gun.put_in( glock_mag, pocket_type::MAGAZINE_WELL );
    gun.put_in( stanag_mag, pocket_type::MAGAZINE_WELL );
    return item( gun );
}

auto iteminfo_p3_consume_gun() -> item
{
    item gun( itype_id( "test_multimag_gun_consume" ) );
    item bb_mag( itype_id( "glockmag" ) );
    bb_mag.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    item batt_mag( itype_id( "heavy_battery_cell" ) );
    batt_mag.put_in( item( itype_id( "battery" ), calendar::turn, 100 ), pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun );
}

auto iteminfo_p3_consume_tool() -> item
{
    item tool( itype_id( "test_multimag_tool_consume" ) );
    item mag_a( itype_id( "glockmag" ) );
    mag_a.put_in( item( itype_id( "9mm" ), calendar::turn, 12 ), pocket_type::MAGAZINE );
    item mag_b( itype_id( "stanag30" ) );
    mag_b.put_in( item( itype_id( "556" ), calendar::turn, 30 ), pocket_type::MAGAZINE );
    REQUIRE( tool.put_in( mag_a, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool.put_in( mag_b, pocket_type::MAGAZINE_WELL ).success() );
    return item( tool );
}

auto iteminfo_p3_loaded_plasma() -> item
{
    // CDF: a BN integral gun (clip_size 30) keeps its rounds in direct charges; ammo_set fills them.
    item gun( itype_id( "plasma_gun" ) );
    gun.ammo_set( itype_id( "plasma" ), 30 );
    return item( gun );
}
} // namespace

// DDA tests/multimag_test.cpp 353-404, name kept (D11). Section 3 is re-derived (`// CDF:`); the fourth
// section is CDF-only.
TEST_CASE( "iteminfo_magazine_label_pluralization", "[p3][iteminfo][multimag][display]" )
{
    clear_all_state();
    const std::vector<iteminfo_parts> parts = {
        iteminfo_parts::GUN_MAGAZINE,
        iteminfo_parts::TOOL_MAGAZINE_CURRENT
    };

    SECTION( "single loaded: singular 'Magazine:' label" ) {
        const item gun = iteminfo_p3_loaded_glock();
        REQUIRE( gun.magazines_current().size() == 1 );
        const std::string name = remove_color_tags( gun.magazines_current().front()->tname() );
        const std::string s = iteminfo_p3_plain( gun, parts );
        CAPTURE( s );
        CHECK( s.find( "Magazine: " + name ) != std::string::npos );
        CHECK( s.find( "Magazines: " ) == std::string::npos );
        // CDF: literal anchor, the glockmag name (9mm.json 62).
        CHECK( s.find( "Glock magazine" ) != std::string::npos );
    }

    SECTION( "dual loaded: plural 'Magazines:' label, both names listed" ) {
        const item gun = iteminfo_p3_dual_well_gun();
        const std::vector<const item *> mags = gun.magazines_current();
        REQUIRE( mags.size() == 2 );
        const std::string first = remove_color_tags( mags[0]->tname() );
        const std::string second = remove_color_tags( mags[1]->tname() );
        REQUIRE( first != second );
        const std::string s = iteminfo_p3_plain( gun, parts );
        CAPTURE( s );
        // The two names appear comma-separated in the same line (DDA 385-392, as one exact check).
        CHECK( s.find( "Magazines: " + first + ", " + second ) != std::string::npos );
        CHECK( s.find( "Magazine: " ) == std::string::npos );
    }

    SECTION( "single loaded tool: tool_info path uses singular 'Magazine:'" ) {
        // CDF: DDA loads medium_battery_cell into its flashlight; CDF's flashlight lists the light cells
        // (lighting.json 183: light_disposable_cell, light_battery_cell, light_minus_battery_cell).
        item flashlight( itype_id( "flashlight" ) );
        item cell( itype_id( "light_battery_cell" ) );
        REQUIRE( flashlight.put_in( cell, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( flashlight.magazines_current().size() == 1 );
        const std::string name = remove_color_tags( flashlight.magazines_current().front()->tname() );
        const std::string s = iteminfo_p3_plain( flashlight, parts );
        CAPTURE( s );
        CHECK( s.find( "Magazine: " + name ) != std::string::npos );
        CHECK( s.find( "Magazines: " ) == std::string::npos );
    }

    SECTION( "CDF: two loaded wells on a tool use the plural label" ) {
        // CDF-only (DDA has no tool with two wells in this test): test_multimag_tool_consume, wells
        // well_a (glockmag) and well_b (stanag30), items.json 1876.
        const item tool = iteminfo_p3_consume_tool();
        REQUIRE( tool.is_tool() );
        REQUIRE_FALSE( tool.magazine_integral() );
        const std::vector<const item *> mags = tool.magazines_current();
        REQUIRE( mags.size() == 2 );
        const std::string first = remove_color_tags( mags[0]->tname() );
        const std::string second = remove_color_tags( mags[1]->tname() );
        const std::string s = iteminfo_p3_plain( tool, parts );
        CAPTURE( s );
        CHECK( s.find( "Magazines: " + first + ", " + second ) != std::string::npos );
    }
}

// DDA tests/iteminfo_test.cpp 2584 (name and section name kept, D11); ONLY the section
// `compatible magazines` (2643-2663) is ported here. The other four sections (2591-2641) are cut by
// 9-1a-b, see plans/P3/cut-tests.md.
TEST_CASE( "tool_info", "[p3][iteminfo][tool]" )
{
    clear_all_state();

    SECTION( "compatible magazines" ) {
        const std::vector<iteminfo_parts> magazine_compat = { iteminfo_parts::TOOL_MAGAZINE_COMPATIBLE };
        override_option opt_art( "ENABLE_ASCII_ART_ITEM", "false" );

        // Rag has no magazine capacity
        item rag( itype_id( "test_rag" ) );
        REQUIRE_FALSE( rag.magazine_integral() );
        REQUIRE( rag.magazine_compatible().empty() );

        CHECK( iteminfo_p3_str( rag, magazine_compat ).empty() );

        // Acetylene torch is a tool with compatible magazines
        item oxy_torch( itype_id( "oxy_torch" ) );
        REQUIRE_FALSE( oxy_torch.magazine_integral() );
        REQUIRE_FALSE( oxy_torch.magazine_compatible().empty() );
        REQUIRE( oxy_torch.uses_magazine() );

        // CDF: DDA's oxy_torch lists three tanks ("small welding tank, tiny welding tank, and welding
        // tank"). CDF's oxy_torch lists two (workshop.json 775: weldtank, tinyweldtank), named
        // "welding tank" and "small welding tank" (weldgas.json), in lexicographic id order
        // (tinyweldtank, weldtank), two items joined with " and " by enumerate_as_string.
        CHECK( iteminfo_p3_str( oxy_torch, magazine_compat ) ==
               "--\n"
               "<color_c_white>Compatible magazines</color>:\n"
               "<color_c_white>Types</color>: small welding tank and welding tank\n" );
    }
}

// CDF-only (DDA has no test for the per-well capacity label; its multimag capacity half is in
// multimag_gun_info_with_battery_only_loaded, 9-1b).
TEST_CASE( "gun_info_capacity_per_ammotype_and_well", "[p3][iteminfo][multimag]" )
{
    clear_all_state();
    const std::vector<iteminfo_parts> gun_capacity = { iteminfo_parts::GUN_CAPACITY };

    SECTION( "dual-well gun without firing requirements: one plain label per ammotype, each well's own capacity" ) {
        const item gun = iteminfo_p3_dual_well_gun();
        REQUIRE_FALSE( gun.uses_firing_requirements() );
        const std::string s = iteminfo_p3_plain( gun, gun_capacity );
        CAPTURE( s );
        // CDF: glockmag holds 15 (9mm.json 59-75), stanag30 holds 30 (223.json 141-155); the gun lists
        // ammo 9mm and 223 (items.json 1477). Old code printed the first loaded magazine's 15 for both.
        CHECK( s.find( "Capacity: 15 rounds of " + ammotype( "9mm" )->name() ) != std::string::npos );
        CHECK( s.find( "Capacity: 30 rounds of " + ammotype( "223" )->name() ) != std::string::npos );
        CHECK( s.find( "Capacity (" ) == std::string::npos );
    }

    SECTION( "firing-requirements gun: label names the well" ) {
        const item gun = iteminfo_p3_consume_gun();
        REQUIRE( gun.uses_firing_requirements() );
        const std::string s = iteminfo_p3_plain( gun, gun_capacity );
        CAPTURE( s );
        // CDF: the gun lists ammo 9mm only (items.json 1618); its 9mm well has id `ammo` (1627) and no
        // pocket_name, so the label is `Capacity (ammo): `; glockmag holds 15. The battery well is not an
        // ammotype of the gun, so there is no `Capacity (power)` line (DDA 1355 loops mod->ammo_types()).
        CHECK( s.find( "Capacity (ammo): 15 rounds of " + ammotype( "9mm" )->name() ) !=
               std::string::npos );
        CHECK( s.find( "Capacity (power)" ) == std::string::npos );
        CHECK( s.find( "Capacity: " ) == std::string::npos );
    }

    SECTION( "integral gun keeps its clip capacity" ) {
        // CDF: plasma_gun is a BN integral gun, clip_size 30 (plasma.json 3); ammo_capacity( at ) would
        // be 0 for it, the zero-argument capacity is the clip.
        const item gun = iteminfo_p3_loaded_plasma();
        REQUIRE( gun.magazine_integral() );
        const std::string s = iteminfo_p3_plain( gun, gun_capacity );
        CAPTURE( s );
        CHECK( s.find( "Capacity: 30 rounds of " + ammotype( "plasma" )->name() ) !=
               std::string::npos );
    }
}

// CDF-only (DDA has no test for the per-pocket `Ammunition:` entries).
TEST_CASE( "gun_info_ammunition_per_pocket", "[p3][iteminfo][multimag]" )
{
    clear_all_state();
    const std::vector<iteminfo_parts> ammo_remaining = { iteminfo_parts::AMMO_REMAINING };

    SECTION( "firing-requirements gun: one entry per loaded pocket, prefixed by the pocket id" ) {
        const item gun = iteminfo_p3_consume_gun();
        REQUIRE( gun.uses_firing_requirements() );
        const std::string s = iteminfo_p3_plain( gun, ammo_remaining );
        CAPTURE( s );
        // CDF: the wells have ids `ammo` and `power` (items.json 1627, 1629) and no pocket_name; the
        // ammo well holds 15 9mm, the power well 100 battery (the helper above).
        const std::string ammo_entry = itype_id( "9mm" )->nname( 15 );
        const std::string power_entry = itype_id( "battery" )->nname( 100 );
        CHECK( s.find( "Ammunition: ammo: " + ammo_entry + ", power: " + power_entry ) !=
               std::string::npos );
    }

    SECTION( "plain gun keeps the single Ammunition line" ) {
        const item gun = iteminfo_p3_loaded_glock();
        REQUIRE_FALSE( gun.uses_firing_requirements() );
        const std::string s = iteminfo_p3_plain( gun, ammo_remaining );
        CAPTURE( s );
        CHECK( s.find( "Ammunition: " + itype_id( "9mm" )->nname( 15 ) ) != std::string::npos );
    }

    SECTION( "part off" ) {
        const item gun = iteminfo_p3_consume_gun();
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::GUN_CAPACITY } );
        CAPTURE( s );
        CHECK( s.find( "Ammunition: " ) == std::string::npos );
    }
}

// CDF-only (DDA has no direct test for this line; DDA 2003-2006 asserts the per-shot line, 9-1b).
TEST_CASE( "gun_info_ammunition_consumed_per_shot", "[p3][iteminfo]" )
{
    clear_all_state();

    SECTION( "ammo_required above one prints the DDA line" ) {
        // CDF: plasma_gun has ammo_to_fire 3 (plasma.json 14) and no gunmods.
        const item gun = iteminfo_p3_loaded_plasma();
        REQUIRE( gun.ammo_required() == 3 );
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::AMMO_TO_FIRE } );
        CAPTURE( s );
        CHECK( s.find( "Ammunition consumed per shot: 3" ) != std::string::npos );
        CHECK( s.find( "ammo per shot" ) == std::string::npos );
    }

    SECTION( "part off prints nothing" ) {
        const item gun = iteminfo_p3_loaded_plasma();
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::AMMO_REMAINING } );
        CAPTURE( s );
        CHECK( s.find( "per shot" ) == std::string::npos );
    }

    SECTION( "ammo_required of one prints nothing" ) {
        const item gun = iteminfo_p3_loaded_glock();
        REQUIRE( gun.ammo_required() == 1 );
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::AMMO_TO_FIRE } );
        CAPTURE( s );
        CHECK( s.find( "per shot" ) == std::string::npos );
    }
}
