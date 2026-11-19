#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "itype_slots/all_slots.h"

#include <string>
#include <vector>

// CDF: P3-7END. The C++ switch after the last gun group: item::magazine_integral() is the DDA body
// (pocket only, DDA src/item_gun_tool_ammo.cpp:2253-2256), the legacy direct-charges guard is gone,
// and no base or mod item still relies on the synthesized integral magazine (kSynthesizeIntegralMagazine,
// "needs pocket definitions").

namespace {

auto has_magazine_pocket( const item &it ) -> bool
{
    return it.contents.has_pocket_type( pocket_type::MAGAZINE );
}

auto has_pocket_of( const itype &t, pocket_type type ) -> bool
{
    for( const pocket_data &pocket : t.pockets ) {
        if( pocket.type == type ) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE( "magazine_integral follows pockets", "[p3][integral][7end]" ) {
    clear_all_state();

    SECTION( "items without a MAGAZINE pocket are not integral" ) {
        // CDF: a gunmod, a toolmod and armor used to be integral by the BN "return true" fall-through
        item gunmod( "bow_stabilizer" );
        REQUIRE_FALSE( has_magazine_pocket( gunmod ) );
        CHECK_FALSE( gunmod.magazine_integral() );

        item toolmod( "magazine_battery_medium_mod" );
        REQUIRE_FALSE( has_magazine_pocket( toolmod ) );
        CHECK_FALSE( toolmod.magazine_integral() );

        item armor( "jeans" );
        REQUIRE_FALSE( has_magazine_pocket( armor ) );
        CHECK_FALSE( armor.magazine_integral() );

        item food( "apple" );
        REQUIRE_FALSE( has_magazine_pocket( food ) );
        CHECK_FALSE( food.magazine_integral() );
    }

    SECTION( "hand-built gun and tool without a MAGAZINE pocket are not integral" ) {
        // CDF: the DDA body ignores clip and max_charges; only the pockets count
        itype gun_type;
        gun_type.gun = cata::make_value<islot_gun>();
        gun_type.gun->clip = 10;
        item gun( &gun_type );
        REQUIRE_FALSE( has_pocket_of( gun_type, pocket_type::MAGAZINE ) );
        CHECK_FALSE( gun.magazine_integral() );

        itype tool_type;
        tool_type.tool = cata::make_value<islot_tool>();
        tool_type.tool->max_charges = 4;
        tool_type.tool->ammo_id.insert( ammotype( "battery" ) );
        item tool( &tool_type );
        REQUIRE_FALSE( has_pocket_of( tool_type, pocket_type::MAGAZINE ) );
        CHECK_FALSE( tool.magazine_integral() );
    }

    SECTION( "a battery cell, a magazine, an integral gun and an integral tool are integral" ) {
        item cell( "light_battery_cell" );
        REQUIRE( has_magazine_pocket( cell ) );
        CHECK( cell.magazine_integral() );

        item speedloader( "38_speedloader" );
        REQUIRE( has_magazine_pocket( speedloader ) );
        CHECK( speedloader.magazine_integral() );

        item revolver( "sw_619" );
        REQUIRE( has_magazine_pocket( revolver ) );
        CHECK( revolver.magazine_integral() );

        item lantern( "gasoline_lantern" );
        REQUIRE( has_magazine_pocket( lantern ) );
        CHECK( lantern.magazine_integral() );
    }
}

TEST_CASE( "no item needs pocket definitions", "[p3][integral][7end]" ) {
    // CDF: a tool that would still synthesize its integral MAGAZINE pocket at load (no explicit
    // pocket_data, no magazines list, max_charges and ammo) is a hit: base data and mods must have none.
    std::vector<std::string> hits;
    for( const itype *t : item_controller->all() ) {
        if( needs_integral_magazine( *t ) ) {
            hits.push_back( t->get_id().str() );
        }
    }
    std::string joined;
    for( const std::string &id : hits ) {
        joined += id + " ";
    }
    INFO( "items still relying on the synthesized integral magazine: " << joined );
    CHECK( hits.empty() );
}

TEST_CASE( "guns with a clip keep a magazine pocket or a well", "[p3][integral][7end]" ) {
    // CDF: a gun with a clip and neither a MAGAZINE pocket nor a MAGAZINE_WELL would lose its
    // integral magazine with the DDA body (stop rule of the packet).
    std::vector<std::string> offenders;
    for( const itype *t : item_controller->all() ) {
        if( !t->gun || t->gun->clip <= 0 ) {
            continue;
        }
        if( !has_pocket_of( *t, pocket_type::MAGAZINE ) &&
            !has_pocket_of( *t, pocket_type::MAGAZINE_WELL ) ) {
            offenders.push_back( t->get_id().str() );
        }
    }
    std::string joined;
    for( const std::string &id : offenders ) {
        joined += id + " ";
    }
    INFO( "guns with a clip and no magazine pocket or well: " << joined );
    CHECK( offenders.empty() );
}

TEST_CASE( "R2 sample guns reload and fire without the guard", "[p3][integral][7end]" ) {
    clear_all_state();
    clear_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    SECTION( "sw_619 revolver: integral cylinder" ) {
        item gun( "sw_619", calendar::turn );
        REQUIRE( gun.magazine_integral() );
        item loose38( "38_special", calendar::turn, 6 );
        CHECK( gun.can_reload_with( loose38, false ) );

        gun.put_in( item( "38_special", calendar::turn, 7 ), pocket_type::MAGAZINE );
        REQUIRE( gun.ammo_remaining() == 7 );
        CHECK( gun.ammo_consume( 1, pos, nullptr ) == 1 );
        CHECK( gun.ammo_remaining() == 6 );
    }

    SECTION( "1895sbl rifle: integral magazine of 4570" ) {
        item gun( "1895sbl", calendar::turn );
        REQUIRE( gun.magazine_integral() );
        const ret_val<void> put = gun.put_in( item( "4570_sp", calendar::turn, 7 ), pocket_type::MAGAZINE );
        INFO( "put_in: " << put.str() );
        REQUIRE( put.success() );
        REQUIRE( gun.ammo_remaining() == 7 );
        CHECK( gun.ammo_consume( 1, pos, nullptr ) == 1 );
        CHECK( gun.ammo_remaining() == 6 );
    }

    SECTION( "mosin44 rifle: a 762R clip reloads and fires" ) {
        item gun( "mosin44", calendar::turn );
        item clip( "762R_clip", calendar::turn );
        clip.put_in( item( "762_54R", calendar::turn, 5 ), pocket_type::MAGAZINE );
        REQUIRE( clip.ammo_remaining() == 5 );
        CHECK( gun.can_reload_with( clip, false ) );
        // CDF: mosin44 has an integral MAGAZINE pocket (R2a); the speedloader is accepted by
        // can_reload_with above, its rounds go in the pocket directly
        const ret_val<void> put = gun.put_in( item( "762_54R", calendar::turn, 5 ), pocket_type::MAGAZINE );
        INFO( "put_in rounds: " << put.str() );
        REQUIRE( put.success() );
        REQUIRE( gun.ammo_remaining() == 5 );
        CHECK( gun.ammo_consume( 1, pos, nullptr ) == 1 );
        CHECK( gun.ammo_remaining() == 4 );
    }
}

TEST_CASE( "pocketless gun with legacy keys has no integral magazine", "[p3][integral][7end]" ) {
    // CDF: P3-7END round 2 (T3 guard case). A gun with a clip, an ammo type and loose direct charges
    // but no MAGAZINE pocket and no well. The deleted direct-charges guard (D14) answered
    // "reloadable with 38" and "5 rounds" here; under the DDA body the gun has no integral magazine.
    // Re-adding the guard to can_reload_with, is_reloadable, ammo_remaining or ammo_consume turns this red.
    clear_all_state();
    itype gun_type;
    gun_type.gun = cata::make_value<islot_gun>();
    gun_type.gun->clip = 10;
    gun_type.gun->ammo.insert( ammotype( "38" ) );
    item gun( &gun_type );
    gun.charges = 5;
    REQUIRE_FALSE( has_pocket_of( gun_type, pocket_type::MAGAZINE ) );
    REQUIRE_FALSE( has_pocket_of( gun_type, pocket_type::MAGAZINE_WELL ) );
    REQUIRE_FALSE( gun.magazine_integral() );

    item loose38( "38_special", calendar::turn, 6 );
    CHECK_FALSE( gun.can_reload_with( loose38, false ) );
    CHECK_FALSE( gun.is_reloadable() );
    CHECK( gun.ammo_remaining() == 0 );
}

TEST_CASE( "ammo_set and ammo_unset write the MAGAZINE pocket of a pocketed gun", "[p3][integral][7end]" ) {
    // CDF: 7ENDC round 4 (DDA item_gun_tool_ammo.cpp:261-396): for an item with a MAGAZINE pocket the
    // answer of ammo_set / ammo_remaining / ammo_consume / ammo_unset is what the pocket holds, and the
    // direct `charges` stay 0. sw_619 has a 7 round MAGAZINE pocket (38.json:133-139).
    clear_all_state();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();
    item gun( "sw_619", calendar::turn );
    REQUIRE( gun.magazine_integral() );
    REQUIRE( gun.ammo_remaining() == 0 );

    SECTION( "ammo_set puts the rounds in the pocket" ) {
        gun.ammo_set( itype_id( "38_special" ), 5 );
        CHECK( gun.ammo_remaining() == 5 );
        CHECK( gun.charges == 0 );
        CHECK( gun.ammo_consume( 3, pos, nullptr ) == 3 );
        CHECK( gun.ammo_remaining() == 2 );
        CHECK( gun.charges == 0 );
    }

    SECTION( "a negative qty fills the pocket" ) {
        gun.ammo_set( itype_id( "38_special" ), -1 );
        CHECK( gun.ammo_remaining() == 7 );
        CHECK( gun.charges == 0 );
    }

    SECTION( "ammo_unset empties the pocket" ) {
        gun.ammo_set( itype_id( "38_special" ), 5 );
        REQUIRE( gun.ammo_remaining() == 5 );
        gun.ammo_unset();
        CHECK( gun.ammo_remaining() == 0 );
        CHECK( gun.contents.has_pocket_type( pocket_type::MAGAZINE ) );
    }
}
