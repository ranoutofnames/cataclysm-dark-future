// Job 5-5d: the Wield line of a gun with several ammo pockets (U7, sidebar part).
// Function under test: character_funcs::fmt_wielded_weapon (src/character_functions.cpp).
// DDA source: src/character_inventory.cpp weapname / weapname_mode / weapname_ammo
// (DDA commit b4b62254ae2). Job 5-5e: the line now follows DDA's `mode name ammo` order, with the
// single-pocket "(cur/max)" and "(empty)" text (was CDF's `ammo [mode] name`, draft fact 4).

#include <string>
#include <utility>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character_functions.h"
#include "gun_mode.h"
#include "item.h"
#include "itype.h"
#include "output.h"
#include "player_helpers.h"
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

// CDF: test_multimag_gun has no firing_requirements, so the Wield line keeps its single count for it
// (DDA weapname_ammo keys off uses_firing_requirements). The turret gun has the glock well and a
// battery well plus firing requirements, so it takes the per-pocket summary.
auto make_turret_gun( int glock_rounds = 15, int charges = 1000 ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_turret_gun" ) );
    REQUIRE( gun->put_in( make_loaded_glockmag( glock_rounds ),
                          pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->put_in( make_battery_cell( charges ), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}
} // namespace

TEST_CASE( "wield_line_dual_well_gun_lists_both_wells", "[p3][weapon]" )
{
    clear_all_state();
    avatar &chr = get_avatar();
    clear_avatar();
    // CDF: pin the capacities the expected counts come from
    REQUIRE( itype_id( "glockmag" )->magazine->capacity == 15 );
    REQUIRE( itype_id( "heavy_battery_cell" )->magazine->capacity == 1000 );

    detached_ptr<item> gun = make_turret_gun( 15, 1000 );
    // CDF: the multimag branch of fmt_wielded_weapon keys off uses_firing_requirements()
    REQUIRE( gun->uses_firing_requirements() );
    REQUIRE( !chr.wield( std::move( gun ) ) );

    const std::string text = remove_color_tags( character_funcs::fmt_wielded_weapon( chr ) );
    CAPTURE( text );
    CHECK( text.find( "(15/15, 1000/1000)" ) != std::string::npos );
    // The old single-count path would print only the current mode's well, "(15)".
    CHECK( text.find( "(15)" ) == std::string::npos );
    // CDF: 5-5e: the ammo list follows the gun name (DDA "mode name ammo"), it no longer leads the line
    CHECK( text.find( remove_color_tags( chr.primary_weapon().type_name() ) ) <
           text.find( "(15/15, 1000/1000)" ) );
}

TEST_CASE( "wield_line_colours_follow_the_ratio", "[p3][weapon]" )
{
    clear_all_state();
    avatar &chr = get_avatar();
    clear_avatar();
    // CDF: glockmag capacity 15 (data/json/items/magazine); the /15 denominators below come from it
    REQUIRE( itype_id( "glockmag" )->magazine->capacity == 15 );

    SECTION( "full glock well is white" ) {
        detached_ptr<item> gun = make_turret_gun();
        REQUIRE( !chr.wield( std::move( gun ) ) );
        const std::string raw = character_funcs::fmt_wielded_weapon( chr );
        CAPTURE( raw );
        CHECK( raw.find( "<color_c_white>15/15</color>" ) != std::string::npos );
    }

    SECTION( "4/15 is red" ) {
        detached_ptr<item> gun = make_turret_gun( 4, 1000 );
        REQUIRE( !chr.wield( std::move( gun ) ) );
        const std::string raw = character_funcs::fmt_wielded_weapon( chr );
        CAPTURE( raw );
        CHECK( raw.find( "<color_c_red>4/15</color>" ) != std::string::npos );
    }

    SECTION( "9/15 is yellow" ) {
        detached_ptr<item> gun = make_turret_gun( 9, 1000 );
        REQUIRE( !chr.wield( std::move( gun ) ) );
        const std::string raw = character_funcs::fmt_wielded_weapon( chr );
        CAPTURE( raw );
        CHECK( raw.find( "<color_c_yellow>9/15</color>" ) != std::string::npos );
    }

    SECTION( "12/15 is light green" ) {
        detached_ptr<item> gun = make_turret_gun( 12, 1000 );
        REQUIRE( !chr.wield( std::move( gun ) ) );
        const std::string raw = character_funcs::fmt_wielded_weapon( chr );
        CAPTURE( raw );
        CHECK( raw.find( "<color_c_light_green>12/15</color>" ) != std::string::npos );
    }

    SECTION( "an empty glock magazine is light red" ) {
        detached_ptr<item> gun = make_turret_gun( 0, 1000 );
        REQUIRE( !chr.wield( std::move( gun ) ) );
        const std::string raw = character_funcs::fmt_wielded_weapon( chr );
        CAPTURE( raw );
        CHECK( raw.find( "<color_c_light_red>0/15</color>" ) != std::string::npos );
    }
}

TEST_CASE( "wield_line_integral_pockets", "[p3][weapon]" )
{
    clear_all_state();
    avatar &chr = get_avatar();
    clear_avatar();
    // CDF: heavy_battery_cell capacity 1000 (data/mods/TEST_DATA), the /1000 below
    REQUIRE( itype_id( "heavy_battery_cell" )->magazine->capacity == 1000 );

    detached_ptr<item> gun = make_integral_gun();
    // CDF: the fixture's integral 9mm pocket has ammo_restriction { "9mm": 1 } (TEST_DATA items.json)
    REQUIRE( gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ),
                          pocket_type::MAGAZINE ).success() );
    REQUIRE( !chr.wield( std::move( gun ) ) );

    const std::string text = remove_color_tags( character_funcs::fmt_wielded_weapon( chr ) );
    CAPTURE( text );
    CHECK( text.find( "(1/1, 0/1000)" ) != std::string::npos );
}

TEST_CASE( "wield_line_single_pocket_gun_is_unchanged", "[p3][weapon]" )
{
    clear_all_state();
    avatar &chr = get_avatar();
    clear_avatar();

    detached_ptr<item> gun = item::spawn( itype_id( "glock_19" ) );
    REQUIRE( gun->put_in( make_loaded_glockmag(), pocket_type::MAGAZINE_WELL ).success() );
    // CDF: a plain pistol takes the non-multimag arm; this pins the branch the guard covers
    REQUIRE_FALSE( gun->uses_firing_requirements() );
    REQUIRE( !chr.wield( std::move( gun ) ) );

    const std::string text = remove_color_tags( character_funcs::fmt_wielded_weapon( chr ) );
    const std::string type_name = remove_color_tags( chr.primary_weapon().type_name() );
    CAPTURE( text, type_name );
    // CDF: 5-5e: DDA layout, the gun name then the "(cur/max)" count (was "(15)" before the name).
    // The glockmag capacity is 15 (asserted in the dual-well case above), so the max is 15.
    CHECK( text.starts_with( type_name ) );
    CHECK( text.ends_with( " (15/15)" ) );
}

TEST_CASE( "wield_line_empty_single_pocket_gun_says_empty", "[p3][weapon]" )
{
    clear_all_state();
    avatar &chr = get_avatar();
    clear_avatar();

    // CDF: 5-5e: DDA weapname_ammo prints "(empty)" for a magazine well with no magazine
    detached_ptr<item> gun = item::spawn( itype_id( "glock_19" ) );
    REQUIRE( gun->uses_magazine() );
    REQUIRE( gun->magazine_current() == nullptr );
    REQUIRE( !chr.wield( std::move( gun ) ) );

    const std::string text = remove_color_tags( character_funcs::fmt_wielded_weapon( chr ) );
    const std::string type_name = remove_color_tags( chr.primary_weapon().type_name() );
    CAPTURE( text, type_name );
    CHECK( text.starts_with( type_name ) );
    CHECK( text.ends_with( " (empty)" ) );
    CHECK( text.find( "(0" ) == std::string::npos );
}

TEST_CASE( "wield_line_mode_only_when_there_is_a_choice", "[p3][weapon]" )
{
    clear_all_state();
    avatar &chr = get_avatar();
    clear_avatar();

    SECTION( "a single-mode pistol shows no brackets" ) {
        detached_ptr<item> gun = item::spawn( itype_id( "glock_19" ) );
        REQUIRE( gun->put_in( make_loaded_glockmag(), pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( gun->gun_all_modes().size() == 1 );
        REQUIRE( !chr.wield( std::move( gun ) ) );

        const std::string text = remove_color_tags( character_funcs::fmt_wielded_weapon( chr ) );
        CAPTURE( text );
        CHECK( text.find( '[' ) == std::string::npos );
    }

    SECTION( "a multi-mode rifle keeps the bracket" ) {
        // CDF: m4a1 modes are DEFAULT semi and AUTO auto (data/json/items/gun/223.json:350);
        // the stanag30 loading pattern is the one in tests/cdf_item_ammo_p3_test.cpp:43-49
        detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
        REQUIRE( gun->put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( gun->gun_all_modes().size() > 1 );
        REQUIRE( !chr.wield( std::move( gun ) ) );

        const std::string mode = chr.primary_weapon().gun_current_mode().tname();
        const std::string text = remove_color_tags( character_funcs::fmt_wielded_weapon( chr ) );
        CAPTURE( text, mode );
        CHECK( text.find( "[" + mode + "]" ) != std::string::npos );
    }
}

TEST_CASE( "wield_line_other_arms_unchanged", "[p3][weapon]" )
{
    clear_all_state();
    avatar &chr = get_avatar();
    clear_avatar();

    SECTION( "bare hands read fists" ) {
        REQUIRE_FALSE( chr.is_armed() );
        // CDF: the first branch of fmt_wielded_weapon returns _( "fists" )
        CHECK( character_funcs::fmt_wielded_weapon( chr ) == "fists" );
    }

    SECTION( "a non-gun tool keeps its (n) prefix" ) {
        detached_ptr<item> lantern = item::spawn( itype_id( "gasoline_lantern" ) );
        REQUIRE_FALSE( lantern->is_gun() );
        lantern->ammo_set( itype_id( "gasoline" ), 7 );
        REQUIRE( lantern->ammo_remaining() == 7 );
        REQUIRE( !chr.wield( std::move( lantern ) ) );

        // CDF: the container/tool arm of fmt_wielded_weapon is string_format( "(%d) %s", ... )
        const std::string text = remove_color_tags( character_funcs::fmt_wielded_weapon( chr ) );
        CAPTURE( text );
        CHECK( text.starts_with( "(7) " ) );
    }
}
