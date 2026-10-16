#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse_actor.h"
#include "json.h"
#include "state_helpers.h"

#include <algorithm>

#include <sstream>
#include <string>

// MOLLE webbing: P1e step 32 (DDA #53114, #66026, #54771), rewritten for P1-transplant J5 to the
// DDA model: an attached pouch is one of the item's `additional_pockets`, not an item in a
// pocket. Only the attach and detach actions (item_contents::add_pocket / remove_pocket) add or
// remove one: no put_in, insert_item, rebuild or type change does.

namespace
{
auto load_item( const std::string &json ) -> detached_ptr<item>
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    return item::spawn( jsin );
}

auto save_item( const item &it ) -> std::string
{
    std::ostringstream buffer;
    JsonOut jsout( buffer );
    it.serialize( jsout );
    return buffer.str();
}

auto pouch_count( const item &carrier ) -> int
{
    return static_cast<int>( carrier.contents.get_added_pockets().size() );
}

auto plate_count( const item &carrier ) -> int
{
    int plates = 0;
    for( const item_pocket &pocket : carrier.contents.get_pockets() ) {
        if( pocket.get_data().ablative && !pocket.empty() ) {
            ++plates;
        }
    }
    return plates;
}
} // namespace

TEST_CASE( "a vest with attach_molle takes pouches up to its webbing size", "[item][molle]" )
{
    clear_all_state();
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    const use_function *use = vest->get_use( "attach_molle" );
    REQUIRE( use != nullptr );
    CHECK( vest->get_use( "detach_molle" ) != nullptr );
    CHECK( vest->contents.get_additional_space_used() == 0 );
    CHECK_FALSE( vest->contents.has_additional_pockets() );
    CHECK( pouch_count( *vest ) == 0 );
}

TEST_CASE( "PALS pouches attach with their contents and use up webbing", "[item][molle]" )
{
    clear_all_state();
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    const int pockets_before = static_cast<int>( vest->contents.get_pockets().size() );
    const units::mass vest_weight = vest->weight();
    detached_ptr<item> pouch = item::spawn( itype_id( "chestpouch" ), calendar::turn );
    REQUIRE( pouch->can_attach_as_pocket() );
    REQUIRE( pouch->get_pocket_size() == 1 );
    const units::mass pouch_weight = pouch->weight( false );
    const units::volume pouch_volume = pouch->volume();
    const int pouch_pockets = static_cast<int>(pouch->contents.get_container_pockets().size());
    REQUIRE(pouch_pockets > 0);
    pouch->put_in(item::spawn(itype_id("stanag20"), calendar::turn));
    REQUIRE(pouch->contents.all_items_top().size() == 1);
    const item* pouch_ptr = pouch.get();

    vest->contents.add_pocket( std::move( pouch ) );

    REQUIRE( pouch_count( *vest ) == 1 );
    CHECK( vest->contents.get_added_pockets().front() == pouch_ptr );
    CHECK( vest->contents.get_additional_space_used() == 1 );
    // the pouch's pockets are the vest's now, with the magazine in them
    CHECK( static_cast<int>( vest->contents.get_pockets().size() ) == pockets_before + pouch_pockets );
    CHECK( vest->contents.all_items_top().size() == 1 );
    CHECK( vest->contents.get_added_pocket( 0 ) != nullptr );
    CHECK( vest->contents.get_added_pocket( 1 ) == nullptr );
    // and the pouch itself is empty
    CHECK( pouch_ptr->contents.all_items_top().empty() );

    SECTION( "the pouch adds its weight and volume to the vest (DDA item.cpp:2071, 2310)" ) {
        const units::mass magazine_weight = vest->contents.all_items_top().front()->weight();
        CHECK( vest->weight() == vest_weight + pouch_weight + magazine_weight );
        CHECK( vest->contents.get_additional_weight() == pouch_weight );
        CHECK( vest->contents.get_additional_volume() == pouch_volume );
    }

    SECTION("a magazine goes into the pocket of an attached pouch") {
        detached_ptr<item> spare = item::spawn(itype_id("stanag20"), calendar::turn);
        const pocket_choice chosen = vest->contents.best_pocket(*spare);
        REQUIRE(chosen.pocket != nullptr);
        // the chosen pocket is one of the pouch's pockets, which sit after the vest's own
        const auto &all_pockets = vest->contents.get_pockets();
        const auto chosen_at = std::ranges::find_if( all_pockets, [&chosen]( const item_pocket &p ) {
            return &p == chosen.pocket;
        } );
        REQUIRE( chosen_at != all_pockets.end() );
        const auto chosen_index = std::distance( all_pockets.begin(), chosen_at );
        CHECK( chosen_index >= pockets_before - 1 );
        CHECK( chosen.pocket->get_data().type != pocket_type::MIGRATION );
        CHECK( vest->contents.insert_item( std::move( spare ) ).success() );
        CHECK( vest->contents.all_items_top().size() == 2 );
    }

    SECTION( "the pouch shows in the vest name like a mod" ) {
        CHECK( vest->tname().find( "+1" ) != std::string::npos );
    }

    SECTION( "a pouch that doesn't fit the webbing is refused by the action's size check" ) {
        // the webbing of the vest is 10: nine more small pouches fill it
        for( int i = 0; i < 9; ++i ) {
            vest->contents.add_pocket( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        }
        CHECK( vest->contents.get_additional_space_used() == 10 );
        const use_function *use = vest->get_use( "attach_molle" );
        REQUIRE( use != nullptr );
        const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>( use->get_actor_ptr() );
        REQUIRE( actor != nullptr );
        CHECK( actor->size - vest->contents.get_additional_space_used() == 0 );
    }

    SECTION( "an item without a PALS flag can't be attached" ) {
        detached_ptr<item> rock = item::spawn( itype_id( "rock" ), calendar::turn );
        CHECK_FALSE( rock->can_attach_as_pocket() );
    }
}

TEST_CASE( "pouches are never attached by put_in, insert_item, rebuilds or type changes",
           "[item][molle]" )
{
    clear_all_state();
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    REQUIRE( pouch_count( *vest ) == 0 );

    SECTION( "put_in" ) {
        vest->put_in( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        CHECK( pouch_count( *vest ) == 0 );
        CHECK( vest->contents.get_additional_space_used() == 0 );
    }
    SECTION( "insert_item" ) {
        vest->contents.insert_item( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        CHECK( pouch_count( *vest ) == 0 );
    }
    SECTION( "try_put_in" ) {
        vest->try_put_in( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        CHECK( pouch_count( *vest ) == 0 );
    }
    SECTION( "rebuilding the pockets keeps the attached pouches and attaches nothing" ) {
        vest->put_in( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        vest->contents.add_pocket( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        const int pockets = static_cast<int>( vest->contents.get_pockets().size() );
        vest->contents.rebuild_pockets();
        CHECK( pouch_count( *vest ) == 1 );
        CHECK( static_cast<int>( vest->contents.get_pockets().size() ) == pockets );
    }
    SECTION( "a type change keeps the attached pouches and what is in them" ) {
        detached_ptr<item> pouch = item::spawn( itype_id( "chestpouch" ), calendar::turn );
        pouch->put_in( item::spawn( itype_id( "stanag30" ), calendar::turn ) );
        vest->contents.add_pocket( std::move( pouch ) );
        vest->put_in( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        vest->convert( itype_id( "ballistic_vest_esapi" ) );
        vest->contents.rebuild_pockets();
        CHECK( pouch_count( *vest ) == 1 );
        int magazines = 0;
        for( const item *it : vest->contents.all_items_top() ) {
            magazines += it->typeId() == itype_id( "stanag30" ) ? 1 : 0;
        }
        CHECK( magazines == 1 );
    }
    SECTION( "spilling or clearing the vest does not take the pouches off" ) {
        vest->contents.add_pocket( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        vest->contents.clear_items();
        CHECK( pouch_count( *vest ) == 1 );
    }
}

TEST_CASE( "a migrated MBR vest comes with two chest pouches and its plates",
           "[item][molle][migration]" )
{
    clear_all_state();
    detached_ptr<item> vest = item_controller->spawn_migrated( itype_id( "modularvestceramic" ),
                              calendar::turn );
    REQUIRE( vest );
    CHECK( vest->typeId() == itype_id( "ballistic_vest_esapi" ) );
    CHECK( pouch_count( *vest ) == 2 );
    CHECK( vest->contents.get_additional_space_used() == 2 );
    CHECK( plate_count( *vest ) == 4 );
}

TEST_CASE( "an attached pouch comes off with its contents", "[item][molle]" )
{
    clear_all_state();
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    detached_ptr<item> pouch = item::spawn( itype_id( "chestpouch" ), calendar::turn );
    pouch->put_in( item::spawn( itype_id( "stanag30" ), calendar::turn ) );
    vest->contents.add_pocket( std::move( pouch ) );
    REQUIRE( pouch_count( *vest ) == 1 );
    const int pockets_with_pouch = static_cast<int>( vest->contents.get_pockets().size() );

    detached_ptr<item> removed = vest->contents.remove_pocket( 0 );
    REQUIRE( removed );
    CHECK( pouch_count( *vest ) == 0 );
    CHECK( vest->contents.get_additional_space_used() == 0 );
    CHECK( static_cast<int>( vest->contents.get_pockets().size() ) < pockets_with_pouch );
    CHECK( vest->contents.all_items_top().empty() );
    // the magazine went back into the pouch
    REQUIRE( removed->contents.all_items_top().size() == 1 );
    CHECK( removed->contents.all_items_top().front()->typeId() == itype_id( "stanag30" ) );
}

TEST_CASE( "attached pouches survive a copy and a save", "[item][molle]" )
{
    clear_all_state();
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    detached_ptr<item> pouch = item::spawn( itype_id( "chestpouch" ), calendar::turn );
    pouch->put_in( item::spawn( itype_id( "stanag30" ), calendar::turn ) );
    vest->contents.add_pocket( std::move( pouch ) );
    vest->contents.add_pocket( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );

    SECTION( "a copy" ) {
        detached_ptr<item> copy = item::spawn( *vest );
        CHECK( pouch_count( *copy ) == 2 );
        CHECK( copy->contents.get_additional_space_used() == 2 );
        REQUIRE( copy->contents.all_items_top().size() == 1 );
        CHECK( copy->contents.all_items_top().front()->typeId() == itype_id( "stanag30" ) );
        CHECK( copy->contents.get_pockets().size() == vest->contents.get_pockets().size() );
        // the copy is a different set of pouches
        CHECK( copy->contents.get_added_pockets().front() != vest->contents.get_added_pockets().front() );
    }
    SECTION( "a save" ) {
        const std::string saved = save_item( *vest );
        CHECK( saved.find( "additional_pockets" ) != std::string::npos );
        detached_ptr<item> loaded = load_item( saved );
        REQUIRE( loaded );
        CHECK( pouch_count( *loaded ) == 2 );
        CHECK( loaded->contents.get_additional_space_used() == 2 );
        REQUIRE( loaded->contents.all_items_top().size() == 1 );
        CHECK( loaded->contents.all_items_top().front()->typeId() == itype_id( "stanag30" ) );
        CHECK( loaded->contents.get_pockets().size() == vest->contents.get_pockets().size() );
        // and saving it again gives the same thing
        CHECK( save_item( *loaded ) == saved );
    }
}
