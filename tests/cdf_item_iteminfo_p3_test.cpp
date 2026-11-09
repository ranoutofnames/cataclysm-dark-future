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
#include "debug.h"
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

namespace
{
// DDA tests/iteminfo_test.cpp 1989-1994.
auto iteminfo_p3_battery_only_gun() -> item
{
    item gun( itype_id( "test_multimag_gun_integral_ammo" ) );
    item battery( itype_id( "heavy_battery_cell" ) );
    battery.ammo_set( itype_id( "battery" ), 100 );
    REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun );
}

// CDF-only: an empty battery cell in the `power` well and nothing else loaded.
auto iteminfo_p3_empty_battery_gun() -> item
{
    item gun( itype_id( "test_multimag_gun_consume" ) );
    item battery( itype_id( "heavy_battery_cell" ) );
    REQUIRE( battery.ammo_remaining() == 0 );
    REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun );
}

// Every info part, as the item info window asks for them.
auto iteminfo_p3_all( const item &it ) -> std::string
{
    return remove_color_tags( it.info_string( iteminfo_query::all, 1 ) );
}
} // namespace

// DDA tests/iteminfo_test.cpp 1983-2007, name kept (D11); clear_all_state() for clear_avatar(), literals for
// the itype_* statics. The `Capacity (ammo)` check is CDF-only (the pocket id is `ammo`, items.json 1864).
TEST_CASE( "multimag_gun_info_with_battery_only_loaded", "[p3][iteminfo][gun][multimag]" )
{
    clear_all_state();

    item gun( itype_id( "test_multimag_gun_integral_ammo" ) );
    REQUIRE( gun.uses_firing_requirements() );

    item battery( itype_id( "heavy_battery_cell" ) );
    battery.ammo_set( itype_id( "battery" ), 100 );
    REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );

    const std::vector<iteminfo_parts> capacity_parts = { iteminfo_parts::GUN_CAPACITY };
    const std::string capacity = iteminfo_p3_str( gun, capacity_parts );
    CAPTURE( capacity );
    CHECK( capacity.find( "Capacity" ) != std::string::npos );
    CHECK( capacity.find( ">1</color> round of " ) != std::string::npos );
    CHECK( capacity.find( ">0</color> round" ) == std::string::npos );
    // CDF: the projectile pocket is the integral `ammo` MAGAZINE pocket restricted to 9mm 1.
    CHECK( remove_color_tags( capacity ).find( "Capacity (ammo): 1 round of " ) != std::string::npos );

    const std::vector<iteminfo_parts> per_shot_parts = { iteminfo_parts::AMMO_TO_FIRE };
    const std::string per_shot = iteminfo_p3_str( gun, per_shot_parts );
    CAPTURE( per_shot );
    CHECK( per_shot.find( "Per-shot consumption" ) != std::string::npos );
}

// CDF-only (ADVISOR-1 of 9-1a-b, corrected): the DDA head must not debugmsg for a loaded magazine that holds
// no projectile ammo, and the unloaded explicit-pocket gun keeps BN's message.
TEST_CASE( "gun_info_head_loaded_magazine_without_projectile_ammo", "[p3][iteminfo][multimag]" )
{
    clear_all_state();

    SECTION( "empty battery cell only: no debugmsg" ) {
        const item gun = iteminfo_p3_empty_battery_gun();
        REQUIRE( gun.uses_firing_requirements() );
        REQUIRE( gun.magazine_current() != nullptr );
        std::string s;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            s = iteminfo_p3_all( gun );
        } );
        CAPTURE( dmsg );
        CAPTURE( s );
        CHECK( dmsg.empty() );
        CHECK( s.find( "Weapon is not loaded, so stats below assume the default ammo: " ) !=
               std::string::npos );
    }

    SECTION( "unloaded explicit-pocket gun without magazines: BN message, no debugmsg" ) {
        // CDF: test_multimag_gun_consume has wells from pocket_data and no `magazines` key. MAGPOCKET (DDA rule,
        // PL-09 Q2 (a)): an explicit-well gun answers its first well's default, glockmag (items.json:1627,
        // item_restriction [glockmag]).
        const item gun( itype_id( "test_multimag_gun_consume" ) );
        REQUIRE( gun.magazine_current() == nullptr );
        REQUIRE( gun.magazine_default() == itype_id( "glockmag" ) );
        std::string s;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            s = iteminfo_p3_all( gun );
        } );
        CAPTURE( dmsg );
        CAPTURE( s );
        CHECK( dmsg.empty() );
        CHECK( s.find( "Weapon is not loaded, so stats below assume the default ammo: " ) !=
               std::string::npos );
    }

    // CDF: kept last. test_multimag_gun_integral_ammo has an integral MAGAZINE pocket but no islot_magazine, so
    // item::magazine_info (MAGAZINE_RELOAD) dereferences a null type->magazine; a crash here must not hide the
    // sections above.
    SECTION( "battery-only loaded gun: every part renders without a debugmsg" ) {
        const item gun = iteminfo_p3_battery_only_gun();
        std::string s;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            s = iteminfo_p3_all( gun );
        } );
        CAPTURE( dmsg );
        CAPTURE( s );
        CHECK( dmsg.empty() );
        // DDA 1160-1167 and 1173-1179: the loaded battery's default ammo stands in for the projectile ammo.
        CHECK( s.find( "Weapon is not loaded, so stats below assume the default ammo: " +
                       itype_id( "battery" )->nname( 1 ) ) != std::string::npos );
        CHECK( s.find( "Capacity (ammo): 1 round of " ) != std::string::npos );
    }
}

// DDA item_info.cpp 1456-1459. CDF-only test (DDA has no direct test for the line).
TEST_CASE( "gun_info_energy_per_shot", "[p3][iteminfo]" )
{
    clear_all_state();
    const std::vector<iteminfo_parts> upscost = { iteminfo_parts::AMMO_UPSCOST };

    SECTION( "energy gun prints the drain with its unit" ) {
        const item gun( itype_id( "test_energy_gun" ) );
        REQUIRE( units::to_kilojoule( gun.get_gun_energy_drain() ) == 5 );
        const std::string s = iteminfo_p3_plain( gun, upscost );
        CAPTURE( s );
        // CDF: energy_drain "5 kJ" (items.json 2352); units::display( 5 kJ ) is "5 kJ".
        CHECK( s.find( "Energy per shot: 5 kJ" ) != std::string::npos );
    }

    SECTION( "gunmod multiplier and modifier are included" ) {
        item gun( itype_id( "test_energy_gun" ) );
        item mod( itype_id( "test_energy_gunmod" ) );
        REQUIRE( gun.put_in( mod, pocket_type::MOD ).success() );
        REQUIRE( units::to_kilojoule( gun.get_gun_energy_drain() ) == 11 );
        const std::string s = iteminfo_p3_plain( gun, upscost );
        CAPTURE( s );
        // CDF: 5 kJ * 2.0 + 1 kJ (items.json 2353).
        CHECK( s.find( "Energy per shot: 11 kJ" ) != std::string::npos );
    }

    SECTION( "gun without energy drain prints nothing" ) {
        const item gun = iteminfo_p3_loaded_glock();
        REQUIRE( units::to_kilojoule( gun.get_gun_energy_drain() ) == 0 );
        const std::string s = iteminfo_p3_plain( gun, upscost );
        CAPTURE( s );
        CHECK( s.find( "Energy per shot" ) == std::string::npos );
    }

    SECTION( "part off prints nothing" ) {
        const item gun( itype_id( "test_energy_gun" ) );
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::AMMO_REMAINING } );
        CAPTURE( s );
        CHECK( s.find( "Energy per shot" ) == std::string::npos );
    }

    SECTION( "UPS-charge gun keeps its own line, no energy line" ) {
        // CDF: plasma_gun has ups_charges 15 and no energy_drain (plasma.json 3); the UPS line is BN's and
        // stays (separate drains, ADV-12 OD-2).
        const item gun = iteminfo_p3_loaded_plasma();
        REQUIRE( gun.get_gun_ups_drain() == 15 );
        const std::string s = iteminfo_p3_plain( gun, upscost );
        CAPTURE( s );
        CHECK( s.find( "Uses 15 charges of UPS per shot" ) != std::string::npos );
        CHECK( s.find( "Energy per shot" ) == std::string::npos );
    }
}

// DDA item_info.cpp 1461-1479. CDF-only test (DDA 2003-2006 asserts only the heading, in
// multimag_gun_info_with_battery_only_loaded above).
TEST_CASE( "gun_info_per_shot_consumption", "[p3][iteminfo][multimag]" )
{
    clear_all_state();
    const std::vector<iteminfo_parts> to_fire = { iteminfo_parts::AMMO_TO_FIRE };

    SECTION( "two modes: one line per mode" ) {
        const item gun = iteminfo_p3_consume_gun();
        REQUIRE( gun.uses_firing_requirements() );
        const std::string s = iteminfo_p3_plain( gun, to_fire );
        CAPTURE( s );
        // CDF: modes DEFAULT "default" (1) and BURST "burst" (3) (items.json 1624); firing_requirements
        // DEFAULT ammo 1 + power 5, BURST ammo 3 + power 15 (1636-1639); the ammo well holds 9mm, the power
        // well battery, so the entry names are the loaded ammo's names (format_consumption_requirements).
        const std::string bb = "9mm";
        CHECK( s.find( "Per-shot consumption (default): 1 " + itype_id( bb )->nname( 1 ) + " + 5 " +
                       itype_id( "battery" )->nname( 5 ) ) != std::string::npos );
        CHECK( s.find( "Per-shot consumption (burst): 3 " + itype_id( bb )->nname( 3 ) + " + 15 " +
                       itype_id( "battery" )->nname( 15 ) ) != std::string::npos );
        int lines = 0;
        for( size_t at = s.find( "Per-shot consumption (" ); at != std::string::npos;
             at = s.find( "Per-shot consumption (", at + 1 ) ) {
            lines++;
        }
        CHECK( lines == 2 );
    }

    SECTION( "legacy gun prints nothing" ) {
        const item gun = iteminfo_p3_loaded_glock();
        REQUIRE_FALSE( gun.uses_firing_requirements() );
        const std::string s = iteminfo_p3_plain( gun, to_fire );
        CAPTURE( s );
        CHECK( s.find( "Per-shot consumption" ) == std::string::npos );
    }

    SECTION( "part off prints nothing" ) {
        const item gun = iteminfo_p3_consume_gun();
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::AMMO_REMAINING } );
        CAPTURE( s );
        CHECK( s.find( "Per-shot consumption" ) == std::string::npos );
    }
}

// CDF-only (Fact 4): a loaded BN integral gun keeps its round in curammo; has_ammo_data() is false for it,
// ammo_data() is not.
TEST_CASE( "gun_info_loaded_bn_integral_gun", "[p3][iteminfo]" )
{
    clear_all_state();

    SECTION( "Ammunition line is printed" ) {
        const item gun = iteminfo_p3_loaded_plasma();
        REQUIRE( gun.magazine_integral() );
        REQUIRE_FALSE( gun.uses_firing_requirements() );
        REQUIRE( gun.ammo_data() != nullptr );
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::AMMO_REMAINING } );
        CAPTURE( s );
        // CDF: iteminfo_p3_loaded_plasma loads 30 plasma (plasma.json 3, clip_size 30).
        CHECK( s.find( "Ammunition: " + itype_id( "plasma" )->nname( 30 ) ) != std::string::npos );
    }

    SECTION( "is not reported as unloaded" ) {
        const item gun = iteminfo_p3_loaded_plasma();
        const std::string s = iteminfo_p3_plain( gun, { iteminfo_parts::GUN_DEFAULT_AMMO } );
        CAPTURE( s );
        CHECK( s.find( "not loaded" ) == std::string::npos );
    }
}

// Job 9-1c: print_ammo, gun_damage( itype_id ) and the Variants lines (DDA tests: none; barrel_test sections cut, no barrel-length system).
// Job 08-9-1c: `Variants: ` ammo line for magazines and guns, `item::print_ammo` and
// `item::gun_damage( const itype_id & )`.
// DDA source: item_gun_tool_ammo.cpp:2171-2205 (`print_ammo`), :1278-1304
// (`gun_damage( itype_id )`), item_info.cpp:828-840 and :1370-1380 (the two lines).
// Expected strings are built from the itypes (`nname( 1 )`), never typed.
//

namespace
{

// Same builder shape as tests/cdf_item_ammo_p3_test.cpp:43-49, with the well insert checked.
auto p3_loaded_m4a1( int rounds ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, rounds ) );
    REQUIRE( gun->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}

// CDF: test_multimag_gun (data/mods/TEST_DATA/items.json:1484-1504) has two wells
// (glockmag, stanag30) and no firing_requirements.
auto p3_multimag_gun() -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun" ) );
    detached_ptr<item> glock_mag = item::spawn( itype_id( "glockmag" ) );
    glock_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ),
                       pocket_type::MAGAZINE );
    detached_ptr<item> stanag = item::spawn( itype_id( "stanag30" ) );
    stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ),
                    pocket_type::MAGAZINE );
    REQUIRE( gun->put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->put_in( std::move( stanag ), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}

// As tests/cdf_item_iteminfo_p3_test.cpp:27-31, but colour tags removed for "contains" checks.
auto p3_info_plain( const item &it, const std::vector<iteminfo_parts> &parts ) -> std::string
{
    return remove_color_tags( it.info_string( iteminfo_query( parts ), 1 ) );
}

// `<info>NAME(N)</info>` for the gun branch of print_ammo.
auto p3_gun_variant( const item &gun, const itype_id &ammo ) -> std::string
{
    return "<info>" + ammo->nname( 1 ) + "(" +
           std::to_string( static_cast<int>( gun.gun_damage( ammo ).total_damage() ) ) + ")</info>";
}

// `<info>NAME</info>(N)` for the magazine-with-gun branch of print_ammo.
auto p3_mag_variant( const item &gun, const itype_id &ammo ) -> std::string
{
    return "<info>" + ammo->nname( 1 ) + "</info>(" +
           std::to_string( static_cast<int>( gun.gun_damage( ammo ).total_damage() ) ) + ")";
}

} // namespace

// DDA item_gun_tool_ammo.cpp:2190-2205 (magazine, no gun): names only.
TEST_CASE( "print_ammo_magazine_lists_ammo", "[p3][iteminfo]" )
{
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    const std::string s = mag->print_ammo( ammotype( "223" ) );
    CAPTURE( s );
    CHECK( s.find( "<info>" + itype_id( "556" )->nname( 1 ) + "</info>" ) != std::string::npos );
    CHECK( s.find( itype_id( "223" )->nname( 1 ) ) != std::string::npos );
    CHECK( s.find( itype_id( "556_incendiary" )->nname( 1 ) ) != std::string::npos );
    CHECK( s.find( itype_id( "9mm" )->nname( 1 ) ) == std::string::npos );
}

// DDA item_gun_tool_ammo.cpp:2192-2198 (magazine, with the gun): name and damage.
TEST_CASE( "print_ammo_magazine_with_gun_shows_damage", "[p3][iteminfo]" )
{
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    detached_ptr<item> gun = p3_loaded_m4a1( 30 );
    const std::string s = mag->print_ammo( ammotype( "223" ), &*gun );
    CAPTURE( s );
    CHECK( s.find( p3_mag_variant( *gun, itype_id( "556" ) ) ) != std::string::npos );
}

// DDA item_gun_tool_ammo.cpp:2200-2205 (gun, no magazine): name and damage for the gun itself.
TEST_CASE( "print_ammo_gun_without_magazine", "[p3][iteminfo]" )
{
    // A bare m4a1: magazine_current() is null, so the gun branch runs.
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    REQUIRE( gun->magazine_current() == nullptr );
    const std::string s = gun->print_ammo( ammotype( "223" ) );
    CAPTURE( s );
    for( const char *id : { "223", "556", "556_incendiary" } ) {
        CHECK( s.find( p3_gun_variant( *gun, itype_id( id ) ) ) != std::string::npos );
    }
}

// DDA item_gun_tool_ammo.cpp:2184-2188: USES_BIONIC_POWER without a magazine is "energy".
TEST_CASE( "print_ammo_bionic_energy", "[p3][iteminfo]" )
{
    // CDF: toolset is the USES_BIONIC_POWER tool with no ammo and no magazine
    // (data/json/items/fake.json:32-40); it returns the energy label.
    detached_ptr<item> toolset = item::spawn( itype_id( "toolset" ) );
    REQUIRE( toolset->is_tool() );
    REQUIRE( toolset->magazine_current() == nullptr );
    CHECK( toolset->print_ammo( ammotype( "223" ) ) == "energy" );
}

// DDA item_gun_tool_ammo.cpp:2190-2205: a plain ammo item has no list, and a magazine
// asked for a type it does not take returns nothing.
TEST_CASE( "print_ammo_empty_for_plain_ammo", "[p3][iteminfo]" )
{
    detached_ptr<item> round = item::spawn( itype_id( "556" ) );
    CHECK( round->print_ammo( ammotype( "223" ) ).empty() );

    // CDF: glockmag takes 9mm only (data/json/items/magazine/9mm.json:59-75).
    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    CHECK( mag->print_ammo( ammotype( "223" ) ).empty() );
}

// CDF: DDA item_gun_tool_ammo.cpp:2173 picks magazine_current() (the first loaded well);
// the multimag adaptation selects the well that takes the requested type.
TEST_CASE( "print_ammo_multimag_gun_asks_the_right_well", "[p3][iteminfo]" )
{
    detached_ptr<item> gun = p3_multimag_gun();
    REQUIRE( gun->all_magazine_well_pockets().size() == 2 );

    const std::string nine = gun->print_ammo( ammotype( "9mm" ) );
    CAPTURE( nine );
    CHECK( nine.find( itype_id( "9mm" )->nname( 1 ) ) != std::string::npos );
    CHECK( nine.find( itype_id( "223" )->nname( 1 ) ) == std::string::npos );
    CHECK( nine.find( itype_id( "556" )->nname( 1 ) ) == std::string::npos );

    const std::string two23 = gun->print_ammo( ammotype( "223" ) );
    CAPTURE( two23 );
    CHECK( two23.find( itype_id( "223" )->nname( 1 ) ) != std::string::npos );
    CHECK( two23.find( itype_id( "9mm" )->nname( 1 ) ) == std::string::npos );
}

// DDA item_gun_tool_ammo.cpp:1278-1304: the id overload agrees with the loaded-ammo overload,
// including an attached gunmod's damage.
TEST_CASE( "gun_damage_id_matches_loaded_gun_damage", "[p3][iteminfo]" )
{
    detached_ptr<item> gun = p3_loaded_m4a1( 30 );
    REQUIRE( gun->ammo_current() == itype_id( "556" ) );
    // CDF: accelerator_gunmod has damage_modifier bullet +10 and fits the m4a1 barrel slot
    // (data/json/items/gunmod/barrel.json:47-70), so a missing gunmod loop changes the result.
    REQUIRE( gun->put_in( item::spawn( itype_id( "accelerator_gunmod" ) ),
                          pocket_type::MOD ).success() );
    REQUIRE( gun->gunmods().size() == 1 );
    REQUIRE( itype_id( "accelerator_gunmod" )->gunmod->damage.total_damage() != 0.0f );

    const itype_id loaded = gun->ammo_current();
    CHECK( gun->gun_damage( loaded ).total_damage() == gun->gun_damage().total_damage() );
}

// DDA item_gun_tool_ammo.cpp:1278-1304: the given ammo decides the damage, not ammo_data().
TEST_CASE( "gun_damage_id_uses_the_given_ammo", "[p3][iteminfo]" )
{
    // A bare gun has no ammo_data(), so reading it instead of the id would make both equal.
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    REQUIRE( gun->ammo_data() == nullptr );

    // CDF: 223 is 42 bullet and 556 is 223 with a relative -5 (data/json/items/ammo/223.json:20,38);
    // both values are read from the itypes instead of being typed.
    const float ammo223 = itype_id( "223" )->ammo->damage.total_damage();
    const float ammo556 = itype_id( "556" )->ammo->damage.total_damage();
    REQUIRE( ammo223 != ammo556 );

    const float dmg223 = gun->gun_damage( itype_id( "223" ) ).total_damage();
    const float dmg556 = gun->gun_damage( itype_id( "556" ) ).total_damage();
    CHECK( dmg223 - dmg556 == ammo223 - ammo556 );
}

// DDA item_gun_tool_ammo.cpp:1278-1304: an id with no ammo slot contributes no ammo damage.
TEST_CASE( "gun_damage_id_non_ammo_is_base", "[p3][iteminfo]" )
{
    // Loaded, so an implementation that read ammo_data() would add the 556 damage.
    detached_ptr<item> gun = p3_loaded_m4a1( 30 );
    REQUIRE( gun->ammo_data() != nullptr );

    // CDF: stanag30 is a magazine itype with no ammo slot; the id overload must stay on the base.
    const itype_id stanag = itype_id( "stanag30" );
    REQUIRE( stanag->ammo == nullptr );
    CHECK( gun->gun_damage( stanag ).total_damage() == gun->gun_damage( false ).total_damage() );
}

// DDA item_info.cpp:828-840: magazine capacity block appends a Variants line.
TEST_CASE( "info_shows_variants_for_magazine", "[p3][iteminfo]" )
{
    clear_all_state();
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    REQUIRE_FALSE( mag->is_tool() );
    const std::string s = p3_info_plain( *mag, { iteminfo_parts::MAGAZINE_CAPACITY } );
    CAPTURE( s );
    CHECK( s.find( "Variants: " ) != std::string::npos );
    CHECK( s.find( itype_id( "556" )->nname( 1 ) ) != std::string::npos );
}

// DDA item_info.cpp:1370-1380: the gun capacity block appends Variants with damage, but only
// when the gun is not an integral-clip gun.
TEST_CASE( "info_shows_variants_for_gun_with_magazine_well", "[p3][iteminfo]" )
{
    clear_all_state();
    detached_ptr<item> gun = p3_loaded_m4a1( 30 );
    REQUIRE_FALSE( gun->magazine_integral() );
    const std::string s = p3_info_plain( *gun, { iteminfo_parts::GUN_CAPACITY } );
    CAPTURE( s );
    CHECK( s.find( "Variants: " ) != std::string::npos );
    // The gun branch prints `<info>NAME(N)</info>`; colour tags are stripped here.
    CHECK( s.find( itype_id( "556" )->nname( 1 ) + "(" +
                   std::to_string( static_cast<int>(
                                      gun->gun_damage( itype_id( "556" ) ).total_damage() ) ) +
                   ")" ) != std::string::npos );

    // CDF: test_compbow is an integral gun (clip_size 1, data/mods/TEST_DATA/items.json), so the
    // !magazine_integral() gate keeps Variants off it (tests/iteminfo_test.cpp:710-716).
    detached_ptr<item> compbow = item::spawn( itype_id( "test_compbow" ) );
    REQUIRE( compbow->magazine_integral() );
    const std::string s2 = p3_info_plain( *compbow, { iteminfo_parts::GUN_CAPACITY } );
    CAPTURE( s2 );
    CHECK( s2.find( "Variants: " ) == std::string::npos );
}
