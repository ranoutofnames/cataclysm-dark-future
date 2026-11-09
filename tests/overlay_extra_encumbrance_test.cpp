// CDF-authored tests for ENC1: the garment pocket overlay carries DDA's per-pocket
// `extra_encumbrance`, so a filled backpack pocket adds encumbrance (DDA item_armor.cpp:568-576).
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "output.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
// A real CDF item whose longest side is 60 cm (cdf_overlay_lengths.json): it fits the main
// pocket of a daypack (max_item_length 60 cm) and the long-item pockets (min_item_length 60 cm).
const itype_id itype_filler( "PR24-extended" );
const itype_id itype_daypack( "daypack" );

std::vector<int> extras_of( const itype_id &id )
{
    std::vector<int> out;
    for( const pocket_data &pk : id->pockets ) {
        if( pk.type == pocket_type::CONTAINER && pk.extra_encumbrance != 0 ) {
            out.push_back( pk.extra_encumbrance );
        }
    }
    std::sort( out.begin(), out.end() );
    return out;
}

item_pocket *pocket_with_extra( item &it, int extra )
{
    for( item_pocket *pk : it.get_container_pockets() ) {
        if( pk->get_pocket_data()->extra_encumbrance == extra ) {
            return pk;
        }
    }
    return nullptr;
}

item_pocket *main_pocket( item &it )
{
    for( item_pocket *pk : it.get_container_pockets() ) {
        if( pk->get_pocket_data()->extra_encumbrance == 0 && !pk->get_pocket_data()->holster ) {
            return pk;
        }
    }
    return nullptr;
}

// Encumbrance is read from the item itself, so the packs are not worn (worn ordering would make
// the references ambiguous); the character is only the second argument of get_encumber.
item &spawn_pack( std::vector<detached_ptr<item>> &keep, const itype_id &id )
{
    keep.push_back( item::spawn( id, calendar::turn ) );
    return *keep.back();
}
} // namespace

TEST_CASE( "enc1_overlay_pockets_carry_the_dda_values", "[armor][pocket][enc1]" )
{
    clear_all_state();
    // DDA: the values below are what DDA defines (pocket_data extra_encumbrance of each bag).
    const std::map<std::string, std::vector<int>> expected = {
        { "backpack", { 3, 10 } },
        { "backpack_leather", { 10 } },
        { "backpack_tactical_large", { 3, 3, 3 } },
        { "bigback", { 1, 1 } },
        { "boxpack", { 3, 10 } },
        { "daypack", { 3, 10 } },
        { "duffelbag", { 5 } },
        { "makeshift_knapsack", { 10 } },
        { "rucksack", { 3, 3, 3, 10 } },
        { "survivor_duffel_bag", { 1, 1, 5 } },
        { "survivor_rucksack", { 1, 1, 3, 3, 3 } },
        { "travelpack", { 1, 1 } },
    };
    size_t total = 0;
    for( const auto &row : expected ) {
        CAPTURE( row.first );
        const itype_id id( row.first );
        REQUIRE( id.is_valid() );
        const std::vector<int> got = extras_of( id );
        CHECK( got == row.second );
        total += got.size();
    }
    CHECK( total == 28 );
}

TEST_CASE( "enc1_filled_pocket_adds_encumbrance", "[armor][pocket][enc1]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, true );
    const bodypart_id torso( "torso" );

    std::vector<detached_ptr<item>> keep;
    item &in_ten = spawn_pack( keep, itype_daypack );
    item &in_three = spawn_pack( keep, itype_daypack );
    item &in_main = spawn_pack( keep, itype_daypack );
    item_pocket *p10 = pocket_with_extra( in_ten, 10 );
    item_pocket *p3 = pocket_with_extra( in_three, 3 );
    item_pocket *main = main_pocket( in_main );
    REQUIRE( p10 != nullptr );
    REQUIRE( p3 != nullptr );
    REQUIRE( main != nullptr );

    const int empty_enc = in_ten.get_encumber( u, torso );
    // daypack encumbrance 2 (DDA storage.json:1000, [ 2, 18 ]; TORSO2 takes DDA's portion numbers)
    REQUIRE( empty_enc == 2 );
    REQUIRE( in_three.get_encumber( u, torso ) == empty_enc );
    REQUIRE( in_main.get_encumber( u, torso ) == empty_enc );

    detached_ptr<item> a = item::spawn( itype_filler, calendar::turn );
    detached_ptr<item> b = item::spawn( itype_filler, calendar::turn );
    detached_ptr<item> c = item::spawn( itype_filler, calendar::turn );
    REQUIRE( p10->can_contain( *a ).success() );
    REQUIRE( p3->can_contain( *b ).success() );
    REQUIRE( main->can_contain( *c ).success() );
    REQUIRE( p10->insert_item( std::move( a ) ).success() );
    REQUIRE( p3->insert_item( std::move( b ) ).success() );
    REQUIRE( main->insert_item( std::move( c ) ).success() );

    const int with_ten = in_ten.get_encumber( u, torso );
    const int with_three = in_three.get_encumber( u, torso );
    const int with_main = in_main.get_encumber( u, torso );
    CAPTURE( empty_enc, with_ten, with_three, with_main );
    // The two long-item pockets are both 5 L holsters without a magazine well, so the same item
    // gives the same volume term in each: only extra_encumbrance (10 vs 3) can differ.
    CHECK( with_ten - with_three == 10 - 3 );
    // The volume term itself is whatever the 3-pocket added beyond its 3.
    const int volume_term = with_three - empty_enc - 3;
    CHECK( with_ten - empty_enc == 10 + volume_term );
    // The main pocket has extra_encumbrance 0: no extra on top of the volume term (its 2 L
    // magazine well swallows this 2 L item, so the volume term is 0 there).
    CHECK( with_main == empty_enc );
}

TEST_CASE( "enc1_empty_pocket_adds_nothing", "[armor][pocket][enc1]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, true );
    const bodypart_id torso( "torso" );

    std::vector<detached_ptr<item>> keep;
    item &pack = spawn_pack( keep, itype_daypack );
    const int base = pack.get_encumber( u, torso );
    // daypack encumbrance 2 (DDA storage.json:1000, [ 2, 18 ]; TORSO2 takes DDA's portion numbers)
    CHECK( base == 2 );

    item_pocket *big = pocket_with_extra( pack, 10 );
    REQUIRE( big != nullptr );
    detached_ptr<item> a = item::spawn( itype_filler, calendar::turn );
    REQUIRE( big->insert_item( std::move( a ) ).success() );
    CHECK( pack.get_encumber( u, torso ) > base );
    big->clear_items();
    REQUIRE( big->empty() );
    CHECK( pack.get_encumber( u, torso ) == base );
}

TEST_CASE( "enc1_iteminfo_line", "[armor][pocket][enc1]" )
{
    clear_all_state();
    detached_ptr<item> pack = item::spawn( itype_daypack, calendar::turn );
    std::string text;
    int idx = 0;
    for( const item_pocket *pk : pack->get_container_pockets() ) {
        std::vector<iteminfo> info;
        pk->general_info( info, idx++, true );
        for( const iteminfo &i : info ) {
            text += remove_color_tags( i.sName ) + "\n";
        }
    }
    CHECK( text.find( "Causes 10 additional encumbrance while in use." ) != std::string::npos );
    CHECK( text.find( "Causes 3 additional encumbrance while in use." ) != std::string::npos );
    CHECK( text.find( "Causes 1 additional encumbrance while in use." ) == std::string::npos );
}
