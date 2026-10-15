// DDA source: tests/item_pocket_test.cpp (DDA commit b4b62254ae2), J3 cases.
// TEST_CASEs ported (6 of 10): best_pocket_in_item_contents, guns_and_gunmods,
// multipocket_liquid_transfer_test, best_pocket_for_pocket-holster_mix,
// item_cannot_contain_contents_it_already_has, Sawed_off_fits_in_large_holster,
// bag_with_restrictions_and_nested_bag_does_not_fit_too_large_items.
// CUT full_wallet_spawn_test: spawns wallet_*_full item groups absent from CDF (plan P1-parity section 2, content backlog).
// CUT auto_whitelist: its fixture is a nested mapgen (manual_nested_mapgen / nested_mapgen_auto_wl_test)
//   and unload_activity_actor::unload, neither of which CDF has (J6 owns the unload action). The
//   whitelist logic itself (item::add_automatic_whitelist in item::in_container) is covered by
//   the ported items_spawn_in_their_default_containers case in dda_item_test.cpp.
// CUT pocket_mods: needs islot_mod::add_pockets, pocket_by_id and the DDA debug_* items (multimag, plan P3).
// CUT unload_from_spillable_container: needs unload_activity_actor::unload (J6).
// Translation TT1-TT8 (orchestration/plans/P1-transplant/COMMON.md): item_location handles are plain
// item pointers or references, clear_map_without_vision() is clear_map(), and map::add_item takes a
// detached item.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "coordinates.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "flat_set.h"
#include "detached_ptr.h"
#include "handle_liquid.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "map/map.h"
#include "map_helpers.h"
#include "map/map_selector.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "rng.h"
#include "test_data.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "weather/weather.h"

static const itype_id itype_XL_holster( "XL_holster" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_debug_modular_m4_carbine( "debug_modular_m4_carbine" );
static const itype_id itype_shotgun_d( "shotgun_d" );
static const itype_id itype_shoulder_strap( "shoulder_strap" );
static const itype_id itype_stock_none( "stock_none" );
static const itype_id itype_test_9mm_ammo( "test_9mm_ammo" );
static const itype_id itype_test_backpack( "dda_test_backpack" );
static const itype_id itype_test_can_drink( "test_can_drink" );
static const itype_id itype_test_flashlight( "test_flashlight" );
static const itype_id itype_test_gas( "test_gas" );
static const itype_id itype_test_glock( "test_glock" );
static const itype_id itype_test_glockmag( "test_glockmag" );
static const itype_id itype_test_halligan( "dda_test_halligan" );
static const itype_id itype_test_jug_plastic( "dda_test_jug_plastic" );
static const itype_id itype_test_liquid( "test_liquid" );
static const itype_id itype_test_mini_backpack( "test_mini_backpack" );
static const itype_id itype_test_robofac_armor_rig( "test_robofac_armor_rig" );
static const itype_id itype_test_screwdriver( "dda_test_screwdriver" );
static const itype_id itype_test_tool_belt_pocket_mix( "test_tool_belt_pocket_mix" );
static const itype_id itype_test_utility_belt( "test_utility_belt" );
static const itype_id itype_test_waterskin( "dda_test_waterskin" );
static const itype_id itype_water( "water" );


// Best pocket
// -----------
// The "best pocket" for an item is the most appropriate pocket for containing it. Only CONTAINER
// type pockets are eligible; other pocket types such as MAGAZINE or MOD cannot be "best".
//
// Functions:
// item::best_pocket
// item_contents::best_pocket
//
static bool has_best_pocket( item &container, const item &thing )
{
    return container.best_pocket( thing ).pocket != nullptr;
}

static item_pocket *get_only_pocket( item &container )
{
    std::vector<item_pocket *> pockets = container.get_container_pockets();
    REQUIRE( pockets.size() == 1 );
    return pockets[0];
}

TEST_CASE( "best_pocket_in_item_contents", "[pocket][item][best]" )
{
    // Waterskins can be best pockets for liquids
    SECTION( "item with one watertight pocket has best_pocket for liquid" ) {
        // Must have a CONTAINER pocket, first and foremost
        item skin( itype_test_waterskin );
        REQUIRE( skin.is_container() );
        // Prerequisite: It can contain water
        item liquid( itype_test_liquid );
        REQUIRE( skin.can_contain( liquid ).success() );

        // Has a best pocket for liquid
        CHECK( has_best_pocket( skin, liquid ) );
    }

    // Test utility belt has pockets best for holding small and large tools, liquids and gases
    SECTION( "item with many different pockets can have best_pocket for different items" ) {
        // Utility belt has CONTAINER pockets
        item util_belt( itype_test_utility_belt );
        REQUIRE( util_belt.is_container() );
        // It can contain small and large tools
        item screwdriver( itype_test_screwdriver );
        item halligan( itype_test_halligan );
        REQUIRE( util_belt.can_contain( screwdriver ).success() );
        REQUIRE( util_belt.can_contain( halligan ).success() );
        // It can contain liquid and gas
        item liquid( itype_test_liquid );
        item gas( itype_test_gas, calendar::turn_zero, item::default_charges_tag{} );
        REQUIRE( util_belt.can_contain( liquid ).success() );
        REQUIRE( util_belt.can_contain( gas ).success() );

        // Utility belt has best_pocket for all these things
        CHECK( has_best_pocket( util_belt, screwdriver ) );
        CHECK( has_best_pocket( util_belt, halligan ) );
        CHECK( has_best_pocket( util_belt, liquid ) );
        CHECK( has_best_pocket( util_belt, gas ) );
    }

    // Because best_pocket only works for CONTAINER pockets, a gun item with a MAGAZINE_WELL pocket
    // is not best_pocket for a compatible magazine, and a mag item with a MAGAZINE pocket is not
    // best_pocket for compatible ammos.
    SECTION( "non-container pockets cannot be best_pocket" ) {
        // Gun that accepts magazines
        item glock( itype_test_glock );
        REQUIRE( glock.has_pocket_type( pocket_type::MAGAZINE_WELL ) );
        // Empty magazine
        item glockmag( itype_test_glockmag, calendar::turn, 0 );
        REQUIRE( glockmag.has_pocket_type( pocket_type::MAGAZINE ) );
        REQUIRE( glockmag.ammo_remaining( ) == 0 );
        // A single 9mm bullet
        item glockammo( itype_test_9mm_ammo, calendar::turn, 1 );
        REQUIRE( glockammo.is_ammo() );
        REQUIRE( glockammo.charges == 1 );

        // Although gun can contain magazine, and magazine can contain bullet...
        REQUIRE( glock.can_contain( glockmag ).success() );
        REQUIRE( glockmag.can_contain( glockammo ).success() );
        // Gun is not best_pocket for magazine, and magazine is not best_pocket for bullet.
        CHECK_FALSE( has_best_pocket( glock, glockmag ) );
        CHECK_FALSE( has_best_pocket( glockmag, glockammo ) );
    }

    // sealable pockets may be filled and sealed, to spawn with a "factory seal" in-game.
    // While sealed, they clearly cannot be the best pocket for storing anything.
    SECTION( "sealed pockets cannot be best_pocket" ) {
        // Regular aluminum beverage can and something to fill it with
        item can( itype_test_can_drink );
        REQUIRE( can.is_container() );
        item liquid( itype_test_liquid );
        REQUIRE( can.can_contain( liquid ).success() );

        // Before being sealed, it can be best pocket for liquid
        CHECK( has_best_pocket( can, liquid ) );
        // Fill with liquid, seal it, and ensure success
        can.put_in( liquid, pocket_type::CONTAINER );
        REQUIRE( can.seal() ); // This must succeed, or next assertion is meaningless
        // Now sealed, the can cannot be best_pocket for liquid
        CHECK_FALSE( has_best_pocket( can, liquid ) );
    }

    SECTION( "pockets with favorite settings" ) {
        item liquid( itype_test_liquid );

        WHEN( "item is blacklisted" ) {
            item skin( itype_test_waterskin );
            REQUIRE( has_best_pocket( skin, liquid ) );
            get_only_pocket( skin )->settings.blacklist_item( liquid.typeId() );

            THEN( "pocket cannot be best pocket" ) {
                CHECK_FALSE( has_best_pocket( skin, liquid ) );
            }
        }

        WHEN( "item is whitelisted" ) {
            item skin( itype_test_waterskin );
            REQUIRE( has_best_pocket( skin, liquid ) );
            get_only_pocket( skin )->settings.whitelist_item( liquid.typeId() );

            THEN( "pocket can be best pocket" ) {
                CHECK( has_best_pocket( skin, liquid ) );
            }
        }
    }
}

TEST_CASE( "guns_and_gunmods", "[pocket][gunmod]" )
{
    item m4a1( itype_debug_modular_m4_carbine );
    item strap( itype_shoulder_strap );
    // Guns cannot "contain" gunmods, but gunmods can be inserted into guns
    CHECK_FALSE( m4a1.can_contain( strap ).success() );
    CHECK( m4a1.put_in( strap, pocket_type::MOD ).success() );
}

TEST_CASE( "multipocket_liquid_transfer_test", "[pocket][item][liquid]" )
{
    clear_map();
    clear_avatar();
    map &m = get_map();
    Character &u = get_player_character();
    item water( itype_water );
    item cont_jug( itype_test_jug_plastic );
    item cont_suit( itype_test_robofac_armor_rig );

    // Place a container at the character's feet
    REQUIRE( !m.add_item_or_charges( u.bub_pos(), item::spawn( cont_jug ) ) );
    item *jug_w_water = nullptr;
    for( item *it : m.i_at( u.bub_pos() ) ) {
        if( it->typeId() == cont_jug.typeId() ) {
            jug_w_water = it;
        }
    }
    REQUIRE( jug_w_water != nullptr );

    GIVEN( "character wearing a multipocket liquid container" ) {
        REQUIRE( !u.wear_item( item::spawn( cont_suit ), false ) );
        item *suit = nullptr;
        for( item *worn_it : u.worn ) {
            if( worn_it->typeId() == cont_suit.typeId() ) {
                suit = worn_it;
            }
        }
        REQUIRE( suit != nullptr );
        REQUIRE( suit->is_container_empty() );

        // CDF: liquid_handler::transfer_liquid is the exported perform_liquid_transfer of the
        // liquid menu; DDA's extra arguments (nullptr, nullptr, -1, nullptr) are for the
        // ground, vehicle and monster destinations
        const auto transfer = [&u]( item & liquid, item * dest, const tripoint_bub_ms & pos ) {
            struct liquid_dest_opt liquid_target;
            liquid_target.pos = pos;
            liquid_target.dest_opt = LD_ITEM;
            liquid_target.it = dest;
            u.set_moves( 100 );
            liquid_handler::transfer_liquid( liquid, liquid_target );
        };
        const auto remove_top = []( item * from, item * what ) {
            // the detached item is destroyed
            from->contents.remove_top( what );
        };

        WHEN( "unloading liquid from a full container into worn container" ) {
            jug_w_water->fill_with( water );
            REQUIRE( jug_w_water->all_items_top().size() == 1 );
            REQUIRE( jug_w_water->all_items_top().front()->charges == 15 );
            transfer( *jug_w_water->all_items_top().front(), suit, u.bub_pos() );
            THEN( "liquid fills the worn container's pockets, some left over" ) {
                CHECK( u.get_moves() == 0 );
                CHECK( !jug_w_water->only_item().is_null() );
                CHECK( jug_w_water->only_item().charges == 3 );
                CHECK( suit->all_items_top().size() == 2 );
                int total = 0;
                for( item *&it : suit->all_items_top() ) {
                    CHECK( it->charges == 6 );
                    total += it->charges;
                }
                total += jug_w_water->only_item().charges;
                CHECK( total == 15 );
            }
        }

        WHEN( "unloading liquid from a full container into partly filled worn container" ) {
            suit->fill_with( water, 4 );
            jug_w_water->fill_with( water );
            REQUIRE( suit->only_item().charges == 4 );
            REQUIRE( jug_w_water->all_items_top().size() == 1 );
            REQUIRE( jug_w_water->all_items_top().front()->charges == 15 );
            transfer( *jug_w_water->all_items_top().front(), suit, u.bub_pos() );
            THEN( "liquid fills the worn container's pockets, some left over" ) {
                CHECK( u.get_moves() == 0 );
                CHECK( !jug_w_water->only_item().is_null() );
                CHECK( jug_w_water->only_item().charges == 7 );
                CHECK( suit->all_items_top().size() == 2 );
                int total = 0;
                for( item *&it : suit->all_items_top() ) {
                    CHECK( it->charges == 6 );
                    total += it->charges;
                }
                total += jug_w_water->only_item().charges;
                CHECK( total == 19 );
            }
        }

        WHEN( "unloading liquid from an almost empty container into worn container" ) {
            jug_w_water->fill_with( water, 2 );
            REQUIRE( jug_w_water->all_items_top().size() == 1 );
            REQUIRE( jug_w_water->all_items_top().front()->charges == 2 );
            transfer( *jug_w_water->all_items_top().front(), suit, u.bub_pos() );
            m.make_active( *jug_w_water );
            // CDF: a liquid that is poured out completely is detached from its source, DDA leaves
            // it there with 0 charges and the test removes it (item_pocket_test.cpp:2486-2487)
            CHECK( jug_w_water->is_container_empty() );
            THEN( "liquid fills one of the worn container's pockets, none left over" ) {
                CHECK( u.get_moves() == 0 );
                CHECK( jug_w_water->is_container_empty() );
                CHECK( suit->all_items_top().size() == 1 );
                CHECK( suit->all_items_top().front()->charges == 2 );
            }
        }

        WHEN( "unloading liquid from an almost empty container into partly filled worn container" ) {
            suit->fill_with( water, 5 );
            jug_w_water->fill_with( water, 2 );
            REQUIRE( suit->only_item().charges == 5 );
            REQUIRE( jug_w_water->all_items_top().size() == 1 );
            REQUIRE( jug_w_water->all_items_top().front()->charges == 2 );
            transfer( *jug_w_water->all_items_top().front(), suit, u.bub_pos() );
            m.make_active( *jug_w_water );
            // CDF: a liquid that is poured out completely is detached from its source, DDA leaves
            // it there with 0 charges and the test removes it (item_pocket_test.cpp:2486-2487)
            CHECK( jug_w_water->is_container_empty() );
            THEN( "liquid fills one of the worn container's pockets, none left over" ) {
                CHECK( u.get_moves() == 0 );
                CHECK( jug_w_water->is_container_empty() );
                CHECK( suit->all_items_top().size() == 2 );
                int total = 0;
                for( item *&it : suit->all_items_top() ) {
                    total += it->charges;
                    CHECK( it->charges > 0 );
                    CHECK( it->charges <= 6 );
                }
                CHECK( total == 7 );
            }
        }

        WHEN( "unloading liquid from worn container into empty container" ) {
            REQUIRE( jug_w_water->is_container_empty() );
            suit->fill_with( water );
            REQUIRE( suit->all_items_top().size() == 2 );
            for( item *&it : suit->all_items_top() ) {
                REQUIRE( it->charges == 6 );
                transfer( *it, jug_w_water, u.bub_pos() );
                CHECK( u.get_moves() == 0 );
                CHECK( it->charges == 0 );
                remove_top( suit, it );
            }
            THEN( "liquid fills most of the empty container, none left over" ) {
                CHECK( u.get_moves() == 0 );
                CHECK( !jug_w_water->only_item().is_null() );
                CHECK( jug_w_water->only_item().charges == 12 );
                CHECK( suit->is_container_empty() );
            }
        }

        WHEN( "unloading liquid from worn container into partly filled container" ) {
            suit->fill_with( water );
            jug_w_water->fill_with( water, 8 );
            REQUIRE( suit->all_items_top().size() == 2 );
            REQUIRE( jug_w_water->only_item().charges == 8 );
            for( item *&it : suit->all_items_top() ) {
                REQUIRE( it->charges == 6 );
                transfer( *it, jug_w_water, u.bub_pos() );
                if( u.get_moves() == 0 && it->charges == 0 ) {
                    remove_top( suit, it );
                }
            }
            THEN( "liquid fills the container, some left over" ) {
                CHECK( jug_w_water->only_item().charges == 15 );
                CHECK( suit->all_items_top().size() == 1 );
                CHECK( suit->all_items_top().front()->charges == 5 );
            }
        }
    }
}

TEST_CASE( "best_pocket_for_pocket-holster_mix", "[pocket][item]" )
{
    avatar &u = get_avatar();
    item tool_belt( itype_test_tool_belt_pocket_mix );
    item flashlight( itype_test_flashlight );

    GIVEN( "character wearing a tool belt" ) {
        clear_avatar();
        REQUIRE( !u.wield( item::spawn( flashlight ) ) );
        REQUIRE( !u.wear_item( item::spawn( tool_belt ), false ) );
        item *tb_ptr = nullptr;
        for( item *worn_it : u.worn ) {
            if( worn_it->typeId() == tool_belt.typeId() ) {
                tb_ptr = worn_it;
            }
        }
        REQUIRE( tb_ptr != nullptr );
        item *tb = tb_ptr;
        // CDF: holster_actor::store takes the detached item; the wielded flashlight is that item
        const auto holster_wielded = [&u, tb]( const holster_actor & actor ) {
            return !actor.store( u, *tb, u.remove_primary_weapon() );
        };

        WHEN( "attaching flashlight to tool belt" ) {
            REQUIRE( tb->can_holster( u.primary_weapon() ) );
            const holster_actor *ptr = dynamic_cast<const holster_actor *>
                                       ( tb->type->get_use( "holster" )->get_actor_ptr() );
            REQUIRE( !!ptr );
            CHECK( holster_wielded( *ptr ) );

            THEN( "flashlight stored in smallest available holster" ) {
                bool found = false;
                const std::list<std::string> valid_pkts = { "P4", "P5", "P6" };
                for( const item_pocket *pkt : tb->get_container_pockets() ) {
                    if( !pkt->empty() && pkt->front().typeId() == flashlight.typeId() ) {
                        CAPTURE( pkt->get_pocket_data()->pocket_name.translated() );
                        CHECK( !found ); // we shouldn't find this item in multiple pockets
                        found = true;
                        bool in_valid_pkt = false;
                        for( const std::string &n : valid_pkts ) {
                            if( pkt->get_pocket_data()->pocket_name.translated() == n ) {
                                in_valid_pkt = true;
                            }
                        }
                        CHECK( in_valid_pkt );
                    }
                }
                CHECK( found );
            }
        }

        WHEN( "attaching flashlight to tool belt, whitelisted pocket 1" ) {
            for( item_pocket *pkt : tb->get_container_pockets() ) {
                if( pkt->get_pocket_data()->pocket_name.translated() == "P1" ) {
                    pkt->settings.whitelist_item( flashlight.typeId() );
                }
            }
            REQUIRE( tb->can_holster( u.primary_weapon() ) );
            const holster_actor *ptr = dynamic_cast<const holster_actor *>
                                       ( tb->type->get_use( "holster" )->get_actor_ptr() );
            REQUIRE( !!ptr );
            CHECK( holster_wielded( *ptr ) );

            THEN( "flashlight stored in pocket 1" ) {
                bool found = false;
                const std::list<std::string> valid_pkts = { "P1" };
                for( const item_pocket *pkt : tb->get_container_pockets() ) {
                    if( !pkt->empty() && pkt->front().typeId() == flashlight.typeId() ) {
                        CAPTURE( pkt->get_pocket_data()->pocket_name.translated() );
                        CHECK( !found ); // we shouldn't find this item in multiple pockets
                        found = true;
                        bool in_valid_pkt = false;
                        for( const std::string &n : valid_pkts ) {
                            if( pkt->get_pocket_data()->pocket_name.translated() == n ) {
                                in_valid_pkt = true;
                            }
                        }
                        CHECK( in_valid_pkt );
                    }
                }
                CHECK( found );
            }
        }

        WHEN( "attaching flashlight to tool belt, whitelisted pocket 2" ) {
            for( item_pocket *pkt : tb->get_container_pockets() ) {
                if( pkt->get_pocket_data()->pocket_name.translated() == "P2" ) {
                    pkt->settings.whitelist_item( flashlight.typeId() );
                }
            }
            REQUIRE( tb->can_holster( u.primary_weapon() ) );
            const holster_actor *ptr = dynamic_cast<const holster_actor *>
                                       ( tb->type->get_use( "holster" )->get_actor_ptr() );
            REQUIRE( !!ptr );
            CHECK( holster_wielded( *ptr ) );

            THEN( "flashlight stored in pocket 2" ) {
                bool found = false;
                const std::list<std::string> valid_pkts = { "P2" };
                for( const item_pocket *pkt : tb->get_container_pockets() ) {
                    if( !pkt->empty() && pkt->front().typeId() == flashlight.typeId() ) {
                        CAPTURE( pkt->get_pocket_data()->pocket_name.translated() );
                        CHECK( !found ); // we shouldn't find this item in multiple pockets
                        found = true;
                        bool in_valid_pkt = false;
                        for( const std::string &n : valid_pkts ) {
                            if( pkt->get_pocket_data()->pocket_name.translated() == n ) {
                                in_valid_pkt = true;
                            }
                        }
                        CHECK( in_valid_pkt );
                    }
                }
                CHECK( found );
            }
        }

        WHEN( "attaching flashlight to tool belt, whitelisted pocket 5" ) {
            for( item_pocket *pkt : tb->get_container_pockets() ) {
                if( pkt->get_pocket_data()->pocket_name.translated() == "P5" ) {
                    pkt->settings.whitelist_item( flashlight.typeId() );
                }
            }
            REQUIRE( tb->can_holster( u.primary_weapon() ) );
            const holster_actor *ptr = dynamic_cast<const holster_actor *>
                                       ( tb->type->get_use( "holster" )->get_actor_ptr() );
            REQUIRE( !!ptr );
            CHECK( holster_wielded( *ptr ) );

            THEN( "flashlight stored in pocket 5" ) {
                bool found = false;
                const std::list<std::string> valid_pkts = { "P5" };
                for( const item_pocket *pkt : tb->get_container_pockets() ) {
                    if( !pkt->empty() && pkt->front().typeId() == flashlight.typeId() ) {
                        CAPTURE( pkt->get_pocket_data()->pocket_name.translated() );
                        CHECK( !found ); // we shouldn't find this item in multiple pockets
                        found = true;
                        bool in_valid_pkt = false;
                        for( const std::string &n : valid_pkts ) {
                            if( pkt->get_pocket_data()->pocket_name.translated() == n ) {
                                in_valid_pkt = true;
                            }
                        }
                        CHECK( in_valid_pkt );
                    }
                }
                CHECK( found );
            }
        }
    }
}

TEST_CASE( "item_cannot_contain_contents_it_already_has", "[item][pocket]" )
{
    item backpack( itype_test_backpack );
    item bottle( itype_bottle_plastic );
    item water( itype_water );

    water.charges = 1;
    bottle.fill_with( water, 1 );
    REQUIRE( !bottle.is_container_empty() );
    REQUIRE( bottle.only_item().typeId() == water.typeId() );
    backpack.put_in( bottle, pocket_type::CONTAINER );
    REQUIRE( !backpack.is_container_empty() );
    REQUIRE( backpack.only_item().typeId() == bottle.typeId() );

    const tripoint_bub_ms ipos = get_player_character().bub_pos();
    map &m = get_map();
    clear_map();

    m.add_item( ipos, item::spawn( backpack ) );
    item *backpack_loc = nullptr;
    for( item *it : m.i_at( ipos ) ) {
        if( it->typeId() == backpack.typeId() ) {
            backpack_loc = it;
        }
    }
    REQUIRE( backpack_loc != nullptr );
    item *bottle_loc = &backpack_loc->only_item();
    item *water_loc = &bottle_loc->only_item();

    REQUIRE( water_loc->count() == 1 );

    const item &water_item = *water_loc;

    // Check bottle containing water
    bool in_top = false;
    for( const item *contained : bottle_loc->all_items_top() ) {
        if( contained == water_loc ) {
            in_top = true;
        }
    }
    CHECK( in_top );
    CHECK( bottle_loc->can_contain( water_item ).success() );
    CHECK( !bottle_loc->can_contain( water_item, false, false, true, false, bottle_loc ).success() );

    // Check backpack containing bottle containing water
    in_top = false;
    for( const item *contained : backpack_loc->all_items_top() ) {
        if( contained == water_loc ) {
            in_top = true;
        }
    }
    CHECK( !in_top );
    CHECK( backpack_loc->can_contain( water_item ).success() );
    CHECK( !backpack_loc->can_contain( water_item, false, false, true, false, bottle_loc ).success() );
}

// RED-UNTIL GUNLEN: item::length() of a gun does not follow its barrel mods in CDF (DDA
// integral_longest_side / barrel_length), so a sawed-off shotgun is as long as the whole gun.
#if 0
TEST_CASE( "Sawed_off_fits_in_large_holster", "[item][pocket]" )
{
    item double_barrel( itype_shotgun_d );
    item large_holster( itype_XL_holster );

    //add the mods
    double_barrel.put_in( item( itype_stock_none, calendar::turn ), pocket_type::MOD );
    double_barrel.put_in( item( itype_barrel_small, calendar::turn ), pocket_type::MOD );

    const ret_val<void> fits = large_holster.can_contain( double_barrel );
    CAPTURE( fits.str() );
    CHECK( fits.success() );
}
#endif // RED-UNTIL GUNLEN

// this tests for cases where we try to find a nested pocket for items (when a parent pocket has some restrictions) and find a massive bag inside the parent pocket
// need to make sure we don't try to fit things larger than the parent pockets remaining volume inside the child pocket if it is non-rigid
TEST_CASE( "bag_with_restrictions_and_nested_bag_does_not_fit_too_large_items", "[item][pocket]" )
{
    item backpack( itype_test_backpack );
    item backpack_two( itype_test_backpack );
    item mini_backpack( itype_test_mini_backpack );

    mini_backpack.put_in( backpack, pocket_type::CONTAINER );
    REQUIRE( !mini_backpack.is_container_empty() );
    REQUIRE( mini_backpack.only_item().typeId() == backpack.typeId() );

    // need to set a setting on the pocket for this to work since that's when nesting starts trying weird stuff
    mini_backpack.get_contents().get_standard_pockets().front()->settings.set_disabled( true );

    // check if the game thinks the mini bag can contain the second bag (it can't)
    // but that the bag could otherwise fit if not in the parent pocket
    CHECK( backpack.can_contain( backpack_two ).success() );
    CHECK( !mini_backpack.can_contain( backpack_two ).success() );

}
