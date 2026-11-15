#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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

auto held_ids( const item &helmet ) -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for( const item_pocket &pocket : helmet.contents.get_pockets() ) {
        for( const item *held : pocket.items() ) {
            ids.push_back( held->typeId().str() );
        }
    }
    return ids;
}

auto holds( const item &helmet, const std::string &id ) -> bool
{
    const std::vector<std::string> ids = held_ids( helmet );
    return std::find( ids.begin(), ids.end(), id ) != ids.end();
}

} // namespace

// CDF: HD1 (G07). DDA's helmet attachment pockets (DDA hats.json:633/847 and
// helmets.json:164/390/431/745/1488/2023), loaded through pocket_data.
TEST_CASE( "helmet_attachment_pockets_match_dda", "[armor][helmet][fm]" )
{
    const std::vector<std::pair<std::string, int>> expected = {
        { "hat_hard", 6 },
        { "hat_hard_hooded", 4 },
        { "helmet_army", 6 },
        { "helmet_bike", 2 },
        { "helmet_nasal", 1 },
        { "pickelhaube", 2 },
        { "tac_helmet", 6 },
        { "tac_fullhelmet", 1 },
    };
    for( const std::pair<std::string, int> &row : expected ) {
        CAPTURE( row.first );
        const item helmet( itype_id( row.first ) );
        CHECK( static_cast<int>( helmet.contents.get_pockets().size() ) == row.second );
        // every one of these pockets is a flag-restricted attachment slot
        int restricted = 0;
        for( const item_pocket &pocket : helmet.contents.get_pockets() ) {
            if( !pocket.get_data().get_flag_restrictions().empty() ) {
                restricted++;
            }
        }
        CHECK( restricted == row.second );
    }
}

TEST_CASE( "a_head_attachment_fits_its_helmet_and_is_worn_with_it", "[armor][helmet][fm]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );

    detached_ptr<item> hard = item::spawn( itype_id( "hat_hard" ) );
    REQUIRE( hard );

    SECTION( "an attachment cannot be worn on its own" ) {
        detached_ptr<item> shield = item::spawn( itype_id( "face_shield" ) );
        REQUIRE( shield );
        CHECK_FALSE( guy.can_wear( *shield ).success() );
    }

    SECTION( "attachments go into their pockets and are worn with the helmet" ) {
        for( const char *id : { "face_shield", "attachable_ear_muffs", "nape_protector",
                                "plastic_mandible_guard", "counterweight_pouch" } ) {
            CAPTURE( id );
            CHECK( hard->try_put_in( item::spawn( itype_id( id ) ) ).success() );
        }
        REQUIRE( held_ids( *hard ).size() == 5 );

        guy.wear_item( std::move( hard ), false );
        item *worn_hard = worn_item( guy, "hat_hard" );
        REQUIRE( worn_hard != nullptr );
        CHECK( guy.is_wearing( itype_id( "hat_hard" ) ) );
        CHECK( holds( *worn_hard, "face_shield" ) );
        CHECK( holds( *worn_hard, "attachable_ear_muffs" ) );
        CHECK( holds( *worn_hard, "counterweight_pouch" ) );
        // CDF: the ablative slot works like DDA's: the attached shield now covers the eyes,
        // which the bare hard hat does not (item_armor.cpp:336-343).
        CHECK( worn_hard->get_coverage( bodypart_id( "eyes" ) ) == 100 );
    }
}

// CDF: HD1 (G07). A helmet only takes the attachment its pocket is flagged for: the bike
// helmet has the strapped mandible guard pocket, not the rail-mounted one (DDA helmets.json:745).
TEST_CASE( "a_helmet_refuses_a_wrong_attachment", "[armor][helmet][fm]" )
{
    clear_all_state();

    detached_ptr<item> bike = item::spawn( itype_id( "helmet_bike" ) );
    REQUIRE( bike );

    const ret_val<bool> rail_guard =
        bike->try_put_in( item::spawn( itype_id( "ballistic_mandible_guard" ) ) );
    CHECK_FALSE( rail_guard.success() );
    CHECK_FALSE( std::string( rail_guard.c_str() ).empty() );
    // no ear pocket on this one either
    CHECK_FALSE( bike->try_put_in( item::spawn( itype_id( "attachable_ear_muffs" ) ) ).success() );
    // nor does a plain tool fit any helmet pocket
    CHECK_FALSE( bike->try_put_in( item::spawn( itype_id( "test_screwdriver" ) ) ).success() );
    // the strapped guard it was made for does fit
    CHECK( bike->try_put_in( item::spawn( itype_id( "plastic_mandible_guard" ) ) ).success() );
}
