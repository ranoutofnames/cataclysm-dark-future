// DDA source: tests/unseal_and_spill_test.cpp (DDA commit b4b62254ae2), the whole scenario table:
// 5 container locations x 3 scenarios x 2 player actions.
// Translated for CDF (TT1-TT8, COMMON.md):
//  * item_location handles are item pointers; a safe_reference stands for the original location.
//  * moving a container to the ground keeps the item (CDF detaches it, DDA copies it and removes
//    the original), so "the original location is invalid" becomes "the original item is now on
//    the ground" (the ground contents are matched as in DDA).
//  * fixture items are in data/mods/TEST_DATA/dda_unseal_items.json.
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cached_options.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "cata_utility.h"
#include "character.h"
#include "contents_change_handler.h"
#include "coordinates.h"
#include "detached_ptr.h"
#include "enums.h"
#include "item.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vpart_position.h"

// *INDENT-OFF*
static const itype_id itype_test_liquid_1ml( "test_liquid_1ml" );
static const itype_id itype_test_rag("test_rag");
static const itype_id itype_test_restricted_container_holder( "test_restricted_container_holder" );
static const itype_id itype_test_solid_1ml( "test_solid_1ml" );
static const itype_id itype_test_watertight_open_sealed_container_1L( "test_watertight_open_sealed_container_1L" );
static const itype_id itype_test_watertight_open_sealed_container_250ml( "test_watertight_open_sealed_container_250ml" );
static const itype_id itype_test_watertight_open_sealed_multipocket_container_2x1L( "test_watertight_open_sealed_multipocket_container_2x1L" );
static const itype_id itype_test_watertight_open_sealed_multipocket_container_2x250ml( "test_watertight_open_sealed_multipocket_container_2x250ml" );

static const vproto_id vehicle_prototype_test_cargo_space( "test_cargo_space" );
// *INDENT-ON*

namespace
{
template<typename ...Enums>
class enum_tuple
{
    private:
        using tuple_type = std::tuple<Enums...>;
        tuple_type enums;

        template<int ind>
        void increment() {
            using enum_t = std::tuple_element_t<ind, tuple_type>;
            enum_t &val = std::get<ind>( enums );
            if( val != enum_t::end ) {
                val = static_cast<enum_t>( static_cast<int>( val ) + 1 );
            }
            if( val == enum_t::end && ind > 0 ) {
                val = enum_t::begin;
                // *INDENT-OFF*
                increment<( ind > 0 ? ind - 1 : 0 )>();
                // *INDENT-ON*
            }
        }

    public:
        explicit enum_tuple( Enums ...enums )
            : enums( enums... ) {
        }

        template<typename Enum>
        Enum get() const {
            return std::get<Enum>( enums );
        }

        enum_tuple operator++() {
            // *INDENT-OFF*
            increment<std::tuple_size_v<tuple_type> - 1>();
            // *INDENT-ON*
            return *this;
        }

        bool operator==( const enum_tuple &rhs ) const {
            return enums == rhs.enums;
        }

        bool operator!=( const enum_tuple &rhs ) const {
            return !operator==( rhs );
        }

        static enum_tuple begin() {
            return enum_tuple( Enums::begin... );
        }

        static enum_tuple end() {
            return ++enum_tuple( static_cast<Enums>( static_cast<int>( Enums::end ) - 1 )... );
        }
};

enum class container_location : int {
    begin,
    inventory = begin,
    worn,
    wielded,
    vehicle,
    ground,
    end,
};

enum class scenario : int {
    begin,
    // a single-pocket container holding a liquid
    contained_liquid = begin,
    // a multi-pocket container holding a liquid, inside a single-pocket container
    nested_contained_liquid,
    /*
    a multi-pocket container with:
      - another multi-pocket container holding a liquid
      - a solid item
    */
    recursive_multi_pocket,
    end,
};

enum class player_action : int {
    begin,
    spill_all = begin,
    cancel_spill,
    end,
};

class test_scenario
{
    private:
        using enum_tuple_type = enum_tuple<container_location, scenario, player_action>;
        enum_tuple_type value;

    public:
        explicit test_scenario( const enum_tuple_type &value ) : value( value ) {
        }

        void run();

        test_scenario operator++() {
            ++value;
            return *this;
        }

        bool operator==( const test_scenario &rhs ) const {
            return value == rhs.value;
        }

        bool operator!=( const test_scenario &rhs ) const {
            return !operator==( rhs );
        }

        static test_scenario begin() {
            return test_scenario( enum_tuple_type::begin() );
        }

        static test_scenario end() {
            return test_scenario( enum_tuple_type::end() );
        }
};

void unseal_items_containing( contents_change_handler &handler, item &root,
                              const std::set<itype_id> &types )
{
    for( item *it : root.all_items_top( pocket_type::CONTAINER ) ) {
        if( it ) {
            if( types.count( it->typeId() ) ) {
                handler.unseal_pocket_containing( *it );
            }
            unseal_items_containing( handler, *it, types );
        }
    }
}

struct item_initialization {
    itype_id id;
    bool fill_parent = false;
    bool seal = false;
    std::vector<item_initialization> contents;
};

detached_ptr<item> initialize( const item_initialization &init )
{
    detached_ptr<item> it = item::spawn( init.id );
    for( const item_initialization &content_init : init.contents ) {
        detached_ptr<item> content = initialize( content_init );
        if( content_init.fill_parent ) {
            REQUIRE( it->fill_with( *content ) >= 1 );
        } else {
            ret_val<void> ret = it->put_in( std::move( content ), pocket_type::CONTAINER );
            INFO( ret.str() );
            REQUIRE( ret.success() );
        }
    }
    if( init.seal ) {
        REQUIRE( it->seal() );
    }
    return it;
}

struct item_result {
    itype_id id;
    bool sealed;
    bool parent_pocket_sealed;
    std::vector<item_result> contents;

    explicit item_result( const itype_id id, const bool sealed, const bool parent_pocket_sealed,
                          std::vector<item_result> contents ) :
        id( id ), sealed( sealed ), parent_pocket_sealed( parent_pocket_sealed ),
        contents( std::move( contents ) ) {}
    explicit item_result( const itype_id id ) :
        id( id ), sealed( false ), parent_pocket_sealed( false ),
        contents( {} ) {
    }
};

void match( const item &it, const item_result &result );

// `parent` is the container the contents are in, or null for a tile, a vehicle or the worn list.
void match( const item *parent, const std::vector<item *> &contents,
            std::vector<item_result> content_results )
{
    CHECK( content_results.size() == contents.size() );
    for( item *content : contents ) {
        if( content ) {
            INFO( "looking for match in expected result: id = " + content->typeId().str() );
            bool found = false;
            for( auto it = content_results.begin(); it != content_results.end(); ++it ) {
                const item_result &content_result = *it;
                // Assuming all items with the same id are exactly the same for now.
                // Maybe use notes as unique ids instead?
                if( content->typeId() == content_result.id ) {
                    match( *content, content_result );
                    if( parent != nullptr ) {
                        const item_pocket *pocket = parent->contained_where( *content );
                        REQUIRE( pocket );
                        CHECK( content_result.parent_pocket_sealed == pocket->sealed() );
                    }
                    found = true;
                    // erase to only match once
                    content_results.erase( it );
                    break;
                }
            }
            INFO( "unexpected result: " << content->typeId().str() );
            // every item in `contents` should have a match
            CHECK( found );
        }
    }
    std::string unfound = "expected result not found: ";
    for( const item_result &content_result : content_results ) {
        unfound += content_result.id.str() + ", ";
    }
    INFO( unfound );
    // every element of `content_results` should have a match
    CHECK( content_results.empty() );
}

void match( const item &it, const item_result &result )
{
    INFO( "match: id = " << result.id.str() );
    REQUIRE( it.typeId() == result.id );
    CHECK( result.sealed == it.any_pockets_sealed() );
    match( &it, const_cast<item &>( it ).all_items_top( pocket_type::CONTAINER ), result.contents );
}

std::pair<item_initialization, std::string> scenario_initial_state( scenario cur_scenario )
{
    item_initialization init;
    std::string init_str;

    switch( cur_scenario ) {
        case scenario::contained_liquid: {
            init_str = "scenario::contained_liquid";
            init = {
                item_initialization {
                    itype_test_watertight_open_sealed_container_250ml, false, true,
                    {
                        item_initialization { itype_test_liquid_1ml, true, false, {} }
                    }
                }
            };
            break;
        }
        case scenario::nested_contained_liquid: {
            init_str = "scenario::nested_contained_liquid";
            init = {
                item_initialization {
                    itype_test_watertight_open_sealed_container_1L, false, true,
                    {
                        item_initialization {
                            itype_test_watertight_open_sealed_multipocket_container_2x250ml,
                            false, true,
                            {
                                item_initialization { itype_test_liquid_1ml, true, false, {} }
                            }
                        }
                    }
                }
            };
            break;
        }
        case scenario::recursive_multi_pocket: {
            init_str = "scenario::recursive_multi_pocket";
            init = {
                item_initialization {
                    itype_test_watertight_open_sealed_multipocket_container_2x1L,
                    false, true,
                    {
                        item_initialization {
                            itype_test_watertight_open_sealed_multipocket_container_2x250ml,
                            false, true,
                            { item_initialization { itype_test_liquid_1ml, true, false, {} } }
                        },
                        item_initialization { itype_test_solid_1ml, true, false, {} }
                    }
                }
            };
            break;
        }
        default:
            FAIL( "unknown scenario" );
    }
    return { init, init_str };
}

std::string container_location_to_string( container_location cur_container_loc )
{
    switch( cur_container_loc ) {
        case container_location::inventory:
            return "container_location::inventory";
        case container_location::worn:
            return "container_location::worn";
        case container_location::wielded:
            return "container_location::wielded";
        case container_location::vehicle:
            return "container_location::vehicle";
        case container_location::ground:
            return "container_location::ground";
        default:
            FAIL( "unknown container_location" );
            return "";
    }
}

// Puts the item where the scenario wants it and returns it (the item itself is not copied).
item *prepare_item( avatar &guy, map &here, detached_ptr<item> &&it,
                    container_location cur_container_loc )
{
    item *ptr = &*it;
    switch( cur_container_loc ) {
        case container_location::inventory: {
            detached_ptr<item> holder = item::spawn( itype_test_restricted_container_holder );
            item &holder_ref = *holder;
            REQUIRE( !guy.wear_item( std::move( holder ), false ) );
            ret_val<void> ret = holder_ref.put_in( std::move( it ), pocket_type::CONTAINER );
            INFO( ret.str() );
            REQUIRE( ret.success() );
            return &holder_ref.only_item();
        }
        case container_location::worn: {
            REQUIRE( !guy.wear_item( std::move( it ), false ) );
            return ptr;
        }
        case container_location::wielded: {
            guy.set_primary_weapon( std::move( it ) );
            return ptr;
        }
        case container_location::vehicle: {
            vehicle *veh = here.add_vehicle( vehicle_prototype_test_cargo_space, guy.bub_pos(),
                                             -90_degrees, 0, 0 );
            REQUIRE( veh );
            here.board_vehicle( guy.bub_pos(), &guy );
            std::optional<vpart_reference> vp =
                here.veh_at( guy.bub_pos() ).part_with_feature( vpart_bitflags::VPFLAG_CARGO, true );
            REQUIRE( vp.has_value() );
            detached_ptr<item> not_added = veh->add_item( vp->part_index(), std::move( it ) );
            REQUIRE( !not_added );
            return ptr;
        }
        case container_location::ground: {
            here.add_item( guy.bub_pos(), std::move( it ) );
            return ptr;
        }
        default: {
            FAIL( "unknown container_location" );
            return nullptr;
        }
    }
}

std::pair<std::optional<item_result>, std::vector<item_result>> scenario_final_state(
            scenario cur_scenario, bool will_spill_outer, bool do_spill )
{
    std::optional<item_result> original_location;
    std::vector<item_result> ground;
    switch( cur_scenario ) {
        case scenario::contained_liquid:
            if( !will_spill_outer ) {
                // no change
                original_location = item_result{
                    itype_test_watertight_open_sealed_container_250ml, false, false,
                    {
                        item_result { itype_test_liquid_1ml }
                    }
                };
            } else if( do_spill ) {
                // container remains in place, liquid spilled on ground
                original_location = item_result{ itype_test_watertight_open_sealed_container_250ml };
                ground = { item_result { itype_test_liquid_1ml } };
            }
            // (will_spill_outer && !do_spill)
            else {
                // container put on ground, liquid not spilled
                original_location = std::nullopt;
                ground = {
                    item_result {
                        itype_test_watertight_open_sealed_container_250ml, false, false,
                        {
                            item_result { itype_test_liquid_1ml }
                        }
                    }
                };
            }
            break;
        case scenario::nested_contained_liquid:
            if( !will_spill_outer && do_spill ) {
                original_location = item_result{
                    itype_test_watertight_open_sealed_container_1L, false, false,
                    {
                        item_result { itype_test_watertight_open_sealed_multipocket_container_2x250ml }
                    }
                };
                ground = { item_result { itype_test_liquid_1ml } };
            } else if( !do_spill ) {
                original_location = item_result{ itype_test_watertight_open_sealed_container_1L };
                ground = {
                    item_result {
                        itype_test_watertight_open_sealed_multipocket_container_2x250ml,
                        false, false,
                        {
                            item_result { itype_test_liquid_1ml },
                            item_result { itype_test_liquid_1ml }
                        }
                    }
                };
            } else {
                original_location = item_result{ itype_test_watertight_open_sealed_container_1L };
                ground = {
                    item_result { itype_test_liquid_1ml },
                    item_result { itype_test_watertight_open_sealed_multipocket_container_2x250ml }
                };
            }
            break;
        case scenario::recursive_multi_pocket:
            if( !will_spill_outer && do_spill ) {
                original_location = item_result{
                    itype_test_watertight_open_sealed_multipocket_container_2x1L,
                    false, false,
                    {
                        item_result { itype_test_watertight_open_sealed_multipocket_container_2x250ml },
                        item_result { itype_test_solid_1ml },
                        item_result { itype_test_solid_1ml }
                    }
                };
                ground = {
                    item_result { itype_test_liquid_1ml }
                };
            } else if( !will_spill_outer && !do_spill ) {
                original_location = item_result{
                    itype_test_watertight_open_sealed_multipocket_container_2x1L,
                    false, false,
                    {
                        item_result { itype_test_solid_1ml },
                        item_result { itype_test_solid_1ml }
                    }
                };
                ground = {
                    item_result {
                        itype_test_watertight_open_sealed_multipocket_container_2x250ml,
                        false, false,
                        {
                            item_result { itype_test_liquid_1ml },
                            item_result { itype_test_liquid_1ml }
                        }
                    },
                };
            } else if( do_spill ) {
                original_location = item_result{
                    itype_test_watertight_open_sealed_multipocket_container_2x1L
                };
                ground = {
                    item_result { itype_test_liquid_1ml },
                    item_result { itype_test_watertight_open_sealed_multipocket_container_2x250ml },
                    item_result { itype_test_solid_1ml }
                };
            } else {
                original_location = item_result{ itype_test_watertight_open_sealed_multipocket_container_2x1L };
                ground = {
                    item_result {
                        itype_test_watertight_open_sealed_multipocket_container_2x250ml,
                        false, false,
                        {
                            item_result { itype_test_liquid_1ml },
                            item_result { itype_test_liquid_1ml }
                        }
                    },
                    item_result { itype_test_solid_1ml }
                };
            }
            break;
        default: {
            FAIL( "unknown scenario" );
        }
    }
    return { original_location, ground };
}

void test_scenario::run()
{
    // a bare character, no DEBUG_STORAGE (TT4): spilled items must not find a pocket on the body
    clear_character( get_avatar(), false );
    clear_map();

    const container_location cur_container_loc = value.get<container_location>();
    const scenario cur_scenario = value.get<scenario>();
    const player_action cur_player_action = value.get<player_action>();

    std::pair<item_initialization, std::string> scenario_init = scenario_initial_state( cur_scenario );

    // INFO() is scoped, so the message won't be shown if we put in in the switch block.
    INFO( scenario_init.second );
    detached_ptr<item> it = initialize( scenario_init.first );

    map &here = get_map();
    avatar &guy = get_avatar();
    std::string container_loc_str = container_location_to_string( cur_container_loc );

    // INFO() is scoped, so the message won't be shown if we put in in the switch block.
    INFO( container_loc_str );
    item *it_ptr = prepare_item( guy, here, std::move( it ), cur_container_loc );
    safe_reference<item> it_loc( it_ptr );

    if( guy.primary_weapon().is_null() ) {
        // so the guy does not wield spilled solid items
        guy.set_primary_weapon( item::spawn( itype_test_rag ) );
    }

    std::string player_action_str;
    restore_on_out_of_scope restore_test_mode_spilling(
        test_mode_spilling_action );

    // liquid handling requires player input, so auto-select with player_action instead
    switch( cur_player_action ) {
        case player_action::spill_all: {
            player_action_str = "player_action::spill_all";
            test_mode_spilling_action = test_mode_spilling_action_t::spill_all;
            break;
        }
        case player_action::cancel_spill: {
            player_action_str = "player_action::cancel_spill";
            test_mode_spilling_action = test_mode_spilling_action_t::cancel_spill;
            break;
        }
        default: {
            FAIL( "unknown player_action" );
            return;
        }
    }
    // INFO() is scoped, so the message won't be shown if we put in in the switch block.
    INFO( player_action_str );

    contents_change_handler handler;
    // TODO replace with actual activities
    unseal_items_containing( handler, *it_ptr,
                             std::set<itype_id> { itype_test_liquid_1ml, itype_test_solid_1ml } );
    handler.handle_by( guy );

    // check final state
    // whether the outermost container will spill. inner containers will always spill.
    bool will_spill_outer = false;
    switch( cur_container_loc ) {
        case container_location::inventory:
        case container_location::worn:
        case container_location::vehicle:
            will_spill_outer = true;
            break;
        case container_location::wielded:
        case container_location::ground:
            will_spill_outer = false;
            break;
        default: {
            FAIL( "unknown container_location" );
            return;
        }
    }

    bool do_spill = false;
    switch( cur_player_action ) {
        case player_action::spill_all:
            do_spill = true;
            break;
        case player_action::cancel_spill:
            do_spill = false;
            break;
        default: {
            FAIL( "unknown player_action" );
            return;
        }
    }

    std::vector<item_result> vehicle_results;
    std::vector<item_result> worn_results;
    std::optional<item_result> wielded_results;
    std::pair<std::optional<item_result>, std::vector<item_result>> intended_result =
                scenario_final_state( cur_scenario, will_spill_outer, do_spill );
    const std::optional<item_result> &original_location = intended_result.first;
    std::vector<item_result> &ground = intended_result.second;

    switch( cur_container_loc ) {
        case container_location::ground:
            if( original_location ) {
                ground.emplace_back( *original_location );
            }
            break;
        case container_location::inventory:
            if( original_location ) {
                worn_results.emplace_back(
                    itype_test_restricted_container_holder, false, false,
                    std::vector<item_result> { *original_location }
                );
            } else {
                worn_results.emplace_back( itype_test_restricted_container_holder );
            }
            break;
        case container_location::worn:
            if( original_location ) {
                worn_results.emplace_back( *original_location );
            }
            break;
        case container_location::vehicle:
            if( original_location ) {
                vehicle_results.emplace_back( *original_location );
            }
            break;
        case container_location::wielded:
            if( original_location ) {
                REQUIRE( !wielded_results.has_value() );
                wielded_results = original_location;
            }
            break;
        default:
            FAIL( "unknown container_location" );
    }
    if( cur_container_loc != container_location::wielded ) {
        REQUIRE( !wielded_results.has_value() );
        wielded_results = item_result { itype_test_rag };
    }

    // Shows where everything ended up when a check below fails
    std::string where_dump = "worn: ";
    for( item *worn_it : guy.top_items() ) {
        where_dump += worn_it->typeId().str() + "[";
        for( const item *inside : worn_it->all_items_top_recursive( pocket_type::CONTAINER ) ) {
            where_dump += inside->typeId().str() + " ";
        }
        where_dump += "] ";
    }
    where_dump += "inv: ";
    for( const std::vector<item *> *stack : guy.inv_const_slice() ) {
        const item *stack_parent = stack->front()->parent_item();
        where_dump += stack->front()->typeId().str() + "x" + std::to_string( stack->size() ) + " in " +
                      ( stack_parent ? stack_parent->typeId().str() : std::string( "nothing" ) ) + " ";
    }
    where_dump += "wielded: " + guy.primary_weapon().typeId().str() + " ground: ";
    for( item * const &ground_it : here.i_at( guy.bub_pos() ) ) {
        where_dump += ground_it->typeId().str() + "[";
        for( const item *inside : ground_it->all_items_top_recursive( pocket_type::CONTAINER ) ) {
            where_dump += inside->typeId().str() + " ";
        }
        where_dump += "] ";
    }
    INFO( where_dump );

    INFO( "checking original item" );
    if( original_location ) {
        REQUIRE( !!it_loc );
        match( *it_loc, *original_location );
    } else {
        // CDF: the container was set on the ground, it keeps its identity (DDA copies it)
        REQUIRE( !!it_loc );
        CHECK( it_loc->where() == item_location_type::map );
    }
    INFO( "checking ground items" );
    std::vector<item *> ground_items;
    for( item * const &ground_item : here.i_at( guy.bub_pos() ) ) {
        ground_items.push_back( ground_item );
    }
    match( nullptr, ground_items, ground );
    INFO( "checking vehicle items" );
    std::optional<vpart_reference> vp = here.veh_at( guy.bub_pos() )
                                        .part_with_feature( vpart_bitflags::VPFLAG_CARGO, true );
    if( cur_container_loc == container_location::vehicle ) {
        REQUIRE( vp.has_value() );
        std::vector<item *> vehicle_items;
        for( item * const &vehicle_item : vp->vehicle().get_items( vp->part_index() ) ) {
            vehicle_items.push_back( vehicle_item );
        }
        match( nullptr, vehicle_items, vehicle_results );
    } else {
        REQUIRE( !vp.has_value() );
    }
    INFO( "checking worn items" );
    match( nullptr, guy.top_items(), worn_results );
    INFO( "checking wielded item" );
    if( wielded_results ) {
        match( guy.primary_weapon(), *wielded_results );
    } else {
        REQUIRE( !guy.is_armed() );
    }
}
} // namespace

TEST_CASE( "unseal_and_spill" )
{
    test_scenario current = test_scenario::begin();
    const test_scenario end = test_scenario::end();
    while( current != end ) {
        current.run();
        ++current;
    }
}
