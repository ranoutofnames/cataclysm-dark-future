// DDA source: tests/char_volume_test.cpp (DDA commit b4b62254ae2). Staged for P1-transplant J4, not compiled until J4 lands.
// TEST_CASEs copied (1): character_baseline_volumes
// DDA test helpers / APIs this file uses that BN lacks (identifier absent from CDF src/ and tests/):
//   get_base_volume, get_healthy_kcal
//
// CUT (J4, reason stated in orchestration/reports/P1-transplant.md "## J4"): this case does not test
// Character::volume_carried. It checks Character::get_base_volume() (a Brozek body-density formula,
// DDA character.cpp:2812) against numbers that come from DDA's body model: get_healthy_kcal()
// (character_health.cpp:904), height() with the size height limits (character.cpp:4149) and
// bodyweight() = 76562390 mg (character_health.cpp:2071). CDF/BN have none of that model (its
// bodyweight() is bmi() * height^2, character.cpp), so the expected 73485_ml etc. cannot hold,
// and the body model is not part of P1. The case is kept below under `#if 0`, not weakened;
// DDA volume_carried itself is covered by the capacity and best-pocket cases. Needs an
// orchestrator decision if the DDA body model is ever ported.
#if 0 // CUT(J4): needs DDA's body model (get_healthy_kcal, DDA height/bodyweight), see above
#include <vector>

#include "cata_catch.h"
#include "character.h"
#include "player_helpers.h"
#include "type_id.h"
#include "units.h"

static const trait_id trait_HUGE( "HUGE" );

static const trait_id trait_LARGE( "LARGE" );

static const trait_id trait_SMALL( "SMALL" );

static const trait_id trait_SMALL2( "SMALL2" );

static units::volume your_volume_with_trait( trait_id new_trait )
{
    clear_avatar();
    Character &you = get_player_character();
    you.set_mutation( new_trait );
    return you.get_base_volume();
}

static int your_height_with_trait( trait_id new_trait )
{
    clear_avatar();
    Character &you = get_player_character();
    you.set_mutation( new_trait );
    return you.height();
}

TEST_CASE( "character_baseline_volumes", "[volume]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.set_stored_kcal( you.get_healthy_kcal() );
    REQUIRE( you.get_mutations().empty() );
    REQUIRE( you.height() == 175 );
    REQUIRE( you.bodyweight() == 76562390_milligram );
    CHECK( you.get_base_volume() == 73485_ml );

    REQUIRE( your_height_with_trait( trait_SMALL2 ) == 70 );
    CHECK( your_volume_with_trait( trait_SMALL2 ) == 23326_ml );

    REQUIRE( your_height_with_trait( trait_SMALL ) == 122 );
    CHECK( your_volume_with_trait( trait_SMALL ) == 42476_ml );

    REQUIRE( your_height_with_trait( trait_LARGE ) == 227 );
    CHECK( your_volume_with_trait( trait_LARGE ) == 116034_ml );

    REQUIRE( your_height_with_trait( trait_HUGE ) == 280 );
    CHECK( your_volume_with_trait( trait_HUGE ) == 156228_ml );
}
#endif // CUT(J4)
