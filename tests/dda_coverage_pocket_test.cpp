// DDA source: tests/coverage_test.cpp (DDA commit b4b62254ae2), P1-transplant J5.
// TEST_CASEs copied (2): Ghost_ablative_vest, Off_Limb_Ghost_ablative_vest
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md): items are detached
// and copied with item::spawn, `monster::melee_attack` returns nothing in BN (the average damage
// is taken over all attacks, which cannot miss with that accuracy), `absorb_hit` has no
// weakpoint_attack argument, and `clear_worn` is the test helper.
#include <string>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "coordinates.h"
#include "damage.h"
#include "item.h"
#include "item_contents.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "subbodypart.h"
#include "type_id.h"

static const efftype_id effect_sleep( "sleep" );

static const itype_id itype_test_ghost_vest( "test_ghost_vest" );
static const itype_id itype_test_plate( "test_plate" );
static const itype_id itype_test_plate_skirt_super( "test_plate_skirt_super" );

static const mtype_id mon_manhack( "mon_manhack" );

static const int num_iters = 10000;
// CDF: positions relative to the middle of the reality bubble instead of HALF_MAPSIZE
static auto dude_pos() -> tripoint_bub_ms
{
    return get_player_character().bub_pos() + tripoint_rel_ms( 4, 0, 0 );
}

static auto mon_pos() -> tripoint_bub_ms
{
    return get_player_character().bub_pos() + tripoint_rel_ms( 3, 0, 0 );
}

static void check_near( const std::string &subject, float actual, const float expected,
                        const float tolerance )
{
    INFO( string_format( "%s is about %.1f (+/- %.2f) with val %.1f", subject, expected, tolerance,
                         actual ) );
    CHECK( actual == Approx( expected ).margin( tolerance ) );
}

static void check_not_near( const std::string &subject, float actual, const float undesired,
                            const float tolerance )
{
    INFO( string_format( "%s is not about %.1f (+/- %.1f)  with val %.1f", subject, undesired,
                         tolerance, actual ) );
    CHECK_FALSE( actual == Approx( undesired ).margin( tolerance ) );
}

static float get_avg_melee_dmg( const item &cloth )
{
    monster zed( mon_manhack, mon_pos() );
    standard_npc dude( "TestCharacter", dude_pos(), {}, 0, 8, 8, 8, 8 );

    clear_character( dude, true );
    dude.setpos( dude_pos() );
    dude.add_effect( effect_sleep, 1_hours );

    int dam_acc = 0;
    int iterations = 0;
    for( int i = 0; i < num_iters; i++ ) {
        dude.set_all_parts_hp_to_max();
        dude.clear_effects();
        dude.add_effect( effect_sleep, 1_hours );
        clear_worn( dude );
        dude.wear_item( item::spawn( cloth ), false );
        zed.melee_attack( dude, 10000.0f );
        iterations++;
        dam_acc += dude.get_hp_max() - dude.get_hp();
        if( dude.is_dead() ) {
            break;
        }
    }
    INFO( string_format( "%s landed %d attacks on character, causing %d damage total.",
                         zed.get_name(), iterations, dam_acc ) );
    iterations = iterations ? iterations : 1;
    return static_cast<float>( dam_acc ) / iterations;
}

TEST_CASE( "Ghost_ablative_vest", "[coverage]" )
{
    clear_all_state();
    SECTION( "Ablative not covered same limb" ) {
        detached_ptr<item> full = item::spawn( itype_test_ghost_vest );
        full->force_insert_item( item::spawn( itype_test_plate ), pocket_type::CONTAINER );
        full->force_insert_item( item::spawn( itype_test_plate ), pocket_type::CONTAINER );
        detached_ptr<item> empty = item::spawn( itype_test_ghost_vest );

        // make sure vest only covers torso_upper when it has armor in it
        REQUIRE( full->covers( sub_bodypart_str_id( "torso_upper" ) ) );
        REQUIRE( !empty->covers( sub_bodypart_str_id( "torso_upper" ) ) );
        const float dmg_full = get_avg_melee_dmg( *full );
        const float dmg_empty = get_avg_melee_dmg( *empty );
        // make sure the armor is counting even if the base vest doesn't do anything
        check_not_near( "Average damage", dmg_full, dmg_empty, 0.5f );
    }
}

TEST_CASE( "Off_Limb_Ghost_ablative_vest", "[coverage]" )
{
    clear_all_state();
    SECTION( "Ablative not covered separate limb" ) {
        detached_ptr<item> full = item::spawn( itype_test_ghost_vest );
        full->force_insert_item( item::spawn( itype_test_plate_skirt_super ), pocket_type::CONTAINER );

        standard_npc dude( "TestCharacter", dude_pos(), {}, 0, 8, 8, 8, 8 );
        dude.wear_item( std::move( full ), false );
        damage_instance du_full = damage_instance( DT_BULLET, 100.0f );
        dude.absorb_hit( bodypart_id( "leg_l" ), du_full );
        check_near( "Damage Protected", du_full.total_damage(), 0.0f, 0.1f );
    }
}
