#include "avatar.h"
#include "calendar.h"
#include "character.h"
#include "character_encumbrance.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse_actor.h"
#include "json.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

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

// MOLLE2 N3 (DDA item_armor.cpp:655-678 item::get_layer( bodypart_id )): a torso garment with
// attached pouches counts as BELTED for the layering code. CDF's layer is one value per item, so
// the rule is a helper used where the code asks "which layer is this on".
TEST_CASE( "belted_layer_changes_wear_order", "[molle]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    detached_ptr<item> vest_holder = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    item &vest = *vest_holder;
    // CDF: the vest has the OUTER flag (cdf_ballistic_vests.json) and covers the torso only
    REQUIRE( vest.get_layer() == layer_level::OUTER_LAYER );
    REQUIRE( !you.wear_item( std::move( vest_holder ), false ) );

    const bodypart_str_id torso( "torso" );
    const auto torso_layers = [&you, &torso]() {
        you.reset_encumbrance();
        return you.get_encumbrance().elems.at( torso );
    };
    const size_t outer = static_cast<size_t>( layer_level::OUTER_LAYER );
    const size_t belted = static_cast<size_t>( layer_level::BELTED_LAYER );

    SECTION( "layer penalty: without pouches the vest is on the OUTER layer only" ) {
        const encumbrance_data data = torso_layers();
        CHECK( data.layer_penalty_details[outer].pieces.size() == 1 );
        CHECK( data.layer_penalty_details[belted].pieces.empty() );
    }

    SECTION( "layer penalty: with a pouch the vest is on the BELTED layer instead" ) {
        vest.contents.add_pocket( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
        REQUIRE( vest.contents.has_additional_pockets() );
        const encumbrance_data data = torso_layers();
        // the layering value of an item is its encumbrance, kept between 2 and 10
        const int expected = std::min( 10, std::max( 2, vest.get_encumber( you, bodypart_id( "torso" ) ) ) );
        REQUIRE( data.layer_penalty_details[belted].pieces.size() == 1 );
        CHECK( data.layer_penalty_details[belted].pieces.front() == expected );
        CHECK( data.layer_penalty_details[outer].pieces.empty() );
    }
}

TEST_CASE( "torso_with_pouches_is_belted", "[molle]" )
{
    clear_all_state();
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    const bodypart_id torso( "torso" );
    const bodypart_id arm_l( "arm_l" );
    const bodypart_id leg_l( "leg_l" );

    CHECK_FALSE( vest->has_belted_layer_for( torso ) );
    CHECK_FALSE( vest->has_belted_layer_for( arm_l ) );
    CHECK_FALSE( vest->has_belted_layer_for( leg_l ) );
    vest->contents.add_pocket( item::spawn( itype_id( "chestpouch" ), calendar::turn ) );
    CHECK( vest->has_belted_layer_for( torso ) );
    CHECK_FALSE( vest->has_belted_layer_for( arm_l ) );
    CHECK_FALSE( vest->has_belted_layer_for( leg_l ) );

    // CDF: 2byarm_guard has the BELTED flag and covers both arms (arm_l, arm_r)
    // CDF: a garment that is BELTED by type is belted on every part it covers
    detached_ptr<item> belt = item::spawn( itype_id( "2byarm_guard" ), calendar::turn );
    REQUIRE( belt->get_layer() == layer_level::BELTED_LAYER );
    int covered = 0;
    for( const bodypart_id &bp : get_avatar().get_all_body_parts() ) {
        if( belt->covers( bp ) ) {
            ++covered;
            CHECK( belt->has_belted_layer_for( bp ) );
        }
    }
    CHECK( covered > 0 );
}

// CDF: dispose_item's "Store in" options are built by this non-header helper of character.cpp
// (character.h is P3-open and not in the MOLLE2 file lock): prompt and action per entry.
using dispose_store_entries = std::vector<std::pair<std::string, std::function<detached_ptr<item>()>>>;
dispose_store_entries dispose_store_options( Character &who, detached_ptr<item> &obj );

TEST_CASE( "store_in_menu_lists_attached_pouches", "[molle]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    detached_ptr<item> vest_holder = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    item &vest = *vest_holder;
    detached_ptr<item> pouch = item::spawn( itype_id( "chestpouch" ), calendar::turn );
    const item *pouch_ptr = pouch.get();
    vest.contents.add_pocket( std::move( pouch ) );
    REQUIRE( !you.wear_item( std::move( vest_holder ), false ) );

    detached_ptr<item> mag = item::spawn( itype_id( "stanag20" ), calendar::turn );
    const item *mag_ptr = mag.get();
    REQUIRE( vest.contents.get_added_pocket( 0 ) != nullptr );
    // CDF: chestpouch holds a stanag20 (see "PALS pouches attach with their contents" above)
    REQUIRE( vest.contents.get_added_pocket( 0 )->can_contain( *mag ).success() );

    auto opts = dispose_store_options( you, mag );
    const auto find_option = [&opts]( const std::string & prefix ) {
        return std::ranges::find_if( opts, [&prefix]( const auto & o ) {
            return o.first.rfind( prefix, 0 ) == 0;
        } );
    };
    CHECK( find_option( "Store in " + vest.tname() ) != opts.end() );
    const auto pouch_entry = find_option( "  >" + pouch_ptr->tname() );
    REQUIRE( pouch_entry != opts.end() );
    // choosing the pouch entry puts the magazine in the pouch's pocket
    CHECK_FALSE( pouch_entry->second() );
    CHECK( vest.contents.get_added_pocket( 0 )->has_item( *mag_ptr ) );

    // an object the pouch cannot take (CDF: chestpouch only takes MAG_COMPACT magazines) but the
    // vest's own plate pocket can (CDF: ABLATIVE_LARGE esapi_plate) has the vest entry and no pouch entry
    detached_ptr<item> tool = item::spawn( itype_id( "esapi_plate" ), calendar::turn );
    REQUIRE_FALSE( vest.contents.get_added_pocket( 0 )->can_contain( *tool ).success() );
    REQUIRE( vest.can_contain( *tool ).success() );
    const auto tool_opts = dispose_store_options( you, tool );
    CHECK( std::ranges::any_of( tool_opts, [&vest]( const auto & o ) {
        return o.first.rfind( "Store in " + vest.tname(), 0 ) == 0;
    } ) );
    CHECK( std::ranges::none_of( tool_opts, []( const auto & o ) {
        return o.first.rfind( "  >", 0 ) == 0;
    } ) );
}
