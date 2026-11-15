#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <string>

namespace
{

auto worn_item( Character &guy, const std::string &id ) -> item *
{
    for( item *worn : guy.worn ) {
        if( worn->typeId() == itype_id( id ) ) {
            return worn;
        }
    }
    return nullptr;
}

auto pocketed( item &container, const std::string &id ) -> item *
{
    for( item_pocket &pocket : container.contents.get_pockets() ) {
        for( item *held : pocket.items() ) {
            if( held->typeId() == itype_id( id ) ) {
                return held;
            }
        }
    }
    return nullptr;
}

} // namespace

// CDF: HD2. DDA item.cpp:1488 update_inherited_flags() makes the flags of the contents of a
// pocket with `inherits_flags` (DDA item_pocket.cpp:2137) count as flags of the carrying item.
// DDA item.cpp:2353 has_flag() then answers for them, so Character::worn_with_flag and
// Character::item_worn_with_flag see a helmet's ear muffs (character_attire.cpp:1050/1080).
TEST_CASE( "pocket_contents_with_inherits_flags_pass_their_flags_to_the_container",
           "[pocket][flags]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );

    detached_ptr<item> hard = item::spawn( itype_id( "hat_hard" ) );
    REQUIRE( hard );
    CHECK_FALSE( hard->has_flag( flag_DEAF ) );

    SECTION( "standby ear muffs carry no DEAF flag to inherit" ) {
        REQUIRE( hard->try_put_in( item::spawn( itype_id( "attachable_ear_muffs" ) ) ).success() );
        CHECK_FALSE( hard->has_flag( flag_DEAF ) );
        guy.wear_item( std::move( hard ), false );
        CHECK_FALSE( guy.worn_with_flag( flag_DEAF ) );
        CHECK_FALSE( guy.is_deaf() );
    }

    SECTION( "ear muffs in the helmet's inheriting pocket deafen the wearer" ) {
        detached_ptr<item> muffs = item::spawn( itype_id( "attachable_ear_muffs_on" ) );
        REQUIRE( muffs );
        // the muffs themselves have the flag; the helmet inherits it from the pocket contents
        REQUIRE( muffs->has_flag( flag_DEAF ) );
        REQUIRE( hard->try_put_in( std::move( muffs ) ).success() );
        CHECK( hard->has_flag( flag_DEAF ) );

        guy.wear_item( std::move( hard ), false );
        item *worn_hard = worn_item( guy, "hat_hard" );
        REQUIRE( worn_hard != nullptr );
        CHECK( guy.worn_with_flag( flag_DEAF ) );
        CHECK( guy.item_worn_with_flag( flag_DEAF ) == worn_hard );
        CHECK( guy.has_item_with_flag( flag_DEAF, false ) );
        CHECK( guy.is_deaf() );

        // taking the muffs out drops the inherited flag again
        item *worn_muffs = pocketed( *worn_hard, "attachable_ear_muffs_on" );
        REQUIRE( worn_muffs != nullptr );
        detached_ptr<item> taken = worn_hard->contents.remove_top( worn_muffs );
        REQUIRE( taken );
        CHECK_FALSE( worn_hard->has_flag( flag_DEAF ) );
        CHECK_FALSE( guy.worn_with_flag( flag_DEAF ) );
        CHECK_FALSE( guy.is_deaf() );
    }
}

// CDF: HD2. Only a pocket marked inherits_flags passes flags on (DDA item_pocket.cpp:2137).
// A backpack pocket has no such key, so an item in it stays invisible to has_flag().
TEST_CASE( "a_pocket_without_inherits_flags_does_not_pass_its_contents_flags_on",
           "[pocket][flags]" )
{
    clear_all_state();

    detached_ptr<item> pack = item::spawn( itype_id( "backpack" ) );
    REQUIRE( pack );
    detached_ptr<item> muffs = item::spawn( itype_id( "attachable_ear_muffs_on" ) );
    REQUIRE( muffs );
    REQUIRE( muffs->has_flag( flag_DEAF ) );
    REQUIRE( pack->try_put_in( std::move( muffs ) ).success() );
    CHECK_FALSE( pack->has_flag( flag_DEAF ) );
}
