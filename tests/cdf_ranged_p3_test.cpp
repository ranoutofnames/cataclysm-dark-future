#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "player_helpers.h"
#include "ranged.h"
#include "rng.h"

namespace {

// CDF: P3-8-8a. Spawns `id` with a full stanag30 of 556 and the requested damage on the
// gun and on its magazine. The anonymous namespace of cdf_item_ammo_consume_p3_test.cpp is
// not shared, so this repeats the loaded_m4a1 pattern there (tests/cdf_item_ammo_consume_p3_test.cpp:43).
auto loaded_gun( const itype_id &id, int gun_damage, int mag_damage ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( id );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ) );
    gun->put_in( std::move( mag ) );
    gun->set_damage( gun_damage );
    REQUIRE( gun->damage() == gun_damage );
    REQUIRE( gun->magazine_current() != nullptr );
    gun->magazine_current()->set_damage( mag_damage );
    REQUIRE( gun->magazine_current()->damage() == mag_damage );
    return gun;
}

// One seed for the whole run, and dirt is reset before every shot, so the dirt misfire
// (0 at dirt 0) and the fouling branch stay out of the way and the jam model is the only
// per-shot randomness left.
auto count_failures( item &gun, int shots ) -> int {
    rng_set_engine_seed( 4242424242 );
    int failures = 0;
    for( int shot = 0; shot < shots; ++shot ) {
        gun.set_var( "dirt", 0 );
        if( !ranged::handle_gun_damage( get_player_character(), gun ) ) {
            ++failures;
        }
    }
    return failures;
}

} // namespace

TEST_CASE( "jam_magazine_damage_4_always_jams", "[p3][ranged]" ) {
    clear_avatar();
    // Magazine damage level 4 is 2.5 x mag_jam_mult (>= 1 for every magazine in CDF data).
    detached_ptr<item> gun = loaded_gun( itype_id( "m4a1" ), 0, 4000 );
    CHECK( count_failures( *gun, 300 ) == 300 );
}

TEST_CASE( "jam_gun_mult_scales_the_damage_table", "[p3][ranged]" ) {
    clear_avatar();
    // Gun damage level 0 is 0.0005 x gun_jam_mult, so 2000 makes every shot jam.
    detached_ptr<item> gun = loaded_gun( itype_id( "test_jam_gun_mult_high" ), 0, 0 );
    CHECK( count_failures( *gun, 300 ) == 300 );
}

TEST_CASE( "jam_NEVER_JAMS_never_jams", "[p3][ranged]" ) {
    clear_avatar();
    // Both tables are far above 1 here, so only the flag can stop the failures.
    detached_ptr<item> gun = loaded_gun( itype_id( "test_jam_gun_mult_high" ), 4000, 4000 );
    gun->set_flag( flag_id( "NEVER_JAMS" ) );
    CHECK( count_failures( *gun, 300 ) == 0 );
}

TEST_CASE( "jam_gun_mult_zero_has_no_table_jam", "[p3][ranged]" ) {
    clear_avatar();
    // gun_jam_mult 0 zeroes the gun table; only the durability malfunction (1/384 per shot)
    // and the pristine-magazine table (0.00027 per shot) remain.
    detached_ptr<item> gun = loaded_gun( itype_id( "test_jam_gun_mult_zero" ), 4000, 0 );
    CHECK( count_failures( *gun, 300 ) <= 8 );
}

TEST_CASE( "jam_gun_damage_2_follows_the_table", "[p3][ranged]" ) {
    clear_avatar();
    // Gun damage level 2 is 0.15 per shot; the durability malfunction adds 1/384 and the
    // pristine magazine adds 0.00027, so the expected count over 2000 shots is about 305.
    detached_ptr<item> gun = loaded_gun( itype_id( "m4a1" ), 2000, 0 );
    const int failures = count_failures( *gun, 2000 );
    CHECK( failures > 230 );
    CHECK( failures < 380 );
}

TEST_CASE( "gun_jam_mult_loads_with_default_one", "[p3][ranged]" ) {
    clear_avatar();
    CHECK( itype_id( "m4a1" ).obj().gun->gun_jam_mult == Approx( 1.0 ) );
    CHECK( itype_id( "test_jam_gun_mult_high" ).obj().gun->gun_jam_mult == Approx( 2000.0 ) );
    CHECK( itype_id( "test_jam_gun_mult_zero" ).obj().gun->gun_jam_mult == Approx( 0.0 ) );
}
