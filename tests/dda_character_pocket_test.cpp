// DDA source: tests/item_pocket_test.cpp (DDA commit b4b62254ae2), P1-transplant J4.
// TEST_CASEs copied (2): character_best_pocket, picking_up_items_respects_pocket_autoinsert_settings
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md):
//  * item_location handles are item pointers; Character::best_pocket returns pocket_choice
//    (.container / .pocket for DDA's .first / .second); top_items_loc is Character::top_items;
//    get_wielded_item is primary_weapon; set_wielded_item is the detached wield.
//  * pickup_activity_actor( { obj1, obj2 }, { 1, 1 }, pos, autopickup ) is CDF's pickup activity
//    (assign_pickup_activity in player_helpers), CDF's pickup entry point; the expected numbers
//    (what ends on the ground, in the top container, in the nested one) are DDA's.
//  * T11: CDF moves items where DDA copies them, so item_pocket equality is checked as identity
//    (the very pocket of the very container), which is stronger.
#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "coordinates.h"
#include "debug.h"
#include "detached_ptr.h"
#include "flat_set.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_test_backpack( "test_backpack" );

static const itype_id itype_test_balloon( "test_balloon" );

static const itype_id itype_test_jug_large_open( "test_jug_large_open" );

static const itype_id itype_test_jug_plastic( "test_jug_plastic" );

static const itype_id itype_test_pine_nuts( "test_pine_nuts" );

static const itype_id itype_test_rag( "test_rag" );

static const itype_id itype_test_rock( "test_rock" );

static const itype_id itype_test_socks( "test_socks" );

static const itype_id itype_test_watertight_open_sealed_container_1L(
    "test_watertight_open_sealed_container_1L" );

// wear_item returns the item if it was not worn
static bool wear_it( Character &who, detached_ptr<item> &&it )
{
    return !who.wear_item( std::move( it ), false );
}

static item &spawn_on_ground( map &here, const tripoint_bub_ms &where, detached_ptr<item> &&it )
{
    item &ref = *it;
    here.add_item( where, std::move( it ) );
    return ref;
}

static item_pocket *get_only_pocket( item &container )
{
    std::vector<item_pocket *> pockets = container.get_container_pockets();
    REQUIRE( pockets.size() == 1 );
    return pockets[0];
}

static item *add_item_to_best_pocket( Character &dummy, detached_ptr<item> &&it )
{
    const pocket_choice target = dummy.best_pocket( *it );
    REQUIRE( target.pocket );

    item *ret = nullptr;
    // CDF (T2): the item itself goes in, DDA's pocket->add( it, &ret ) puts a copy
    target.pocket->add( std::move( it ), &ret );
    REQUIRE( target.pocket->has_item( *ret ) );

    return ret;
}

// Character::best_pocket
// - See if wielded item can hold it - start with this as default
// - For each worn item, see if best_pocket is better; if so, use it
// + Return the item that has the best pocket
//
// What is the best pocket to put @it into? the pockets in @avoid do not count
// Character::best_pocket( it, avoid )
// NOTE: different syntax than item_contents::best_pocket
// (Second argument is `avoid` item pointer, not parent item location)
TEST_CASE( "character_best_pocket", "[pocket][character][best]" )
{
    Character &dummy = get_player_character();

    // reset dummy equipment before each test. TT4: a bare character, no DEBUG_STORAGE
    clear_all_state();
    clear_character( get_avatar(), false );

    WHEN( "the player is wielding a container" ) {
        detached_ptr<item> socks = item::spawn( itype_test_socks );
        detached_ptr<item> container_holder = item::spawn( itype_test_watertight_open_sealed_container_1L );
        item &container = *container_holder;

        // wield the container item.
        REQUIRE( !dummy.wield( std::move( container_holder ) ) );

        THEN( "the player can stash an item in it" ) {
            // check to see if the item can be stored in any pocket.
            CHECK( dummy.can_stash_partial( *socks ) );

            // and check to see if the item is actually stored in the wielded container.
            const pocket_choice pocket = dummy.best_pocket( *socks );
            REQUIRE( pocket.pocket );
            CHECK( pocket.pocket == get_only_pocket( container ) );
        }
    }

    WHEN( "the player is wearing a container" ) {
        detached_ptr<item> socks = item::spawn( itype_test_socks );
        detached_ptr<item> backpack_holder = item::spawn( itype_test_backpack );
        item &backpack = *backpack_holder;

        // wear the backpack item.
        REQUIRE( wear_it( dummy, std::move( backpack_holder ) ) );

        THEN( "the player can stash an item in it" ) {
            // check to see if the item can be stored in any pocket.
            CHECK( dummy.can_stash_partial( *socks ) );

            // and check to see if the item is actually stored in the wielded container.
            const pocket_choice pocket = dummy.best_pocket( *socks );
            REQUIRE( pocket.pocket );
            CHECK( pocket.pocket == get_only_pocket( backpack ) );
        }
    }

    WHEN( "wielding- and wearing a container" ) {
        detached_ptr<item> socks = item::spawn( itype_test_socks );
        detached_ptr<item> container_wear_holder =
            item::spawn( itype_test_watertight_open_sealed_container_1L );
        item &container_wear = *container_wear_holder;
        detached_ptr<item> container_wield_holder =
            item::spawn( itype_test_watertight_open_sealed_container_1L );
        item &container_wield = *container_wield_holder;

        // wield- and wear the respective container items.
        REQUIRE( !dummy.wield( std::move( container_wield_holder ) ) );
        REQUIRE( wear_it( dummy, std::move( container_wear_holder ) ) );

        THEN( "the wielded container has priority" ) {
            // check to see if the item can be stored in any pocket.
            CHECK( dummy.can_stash_partial( *socks ) );

            // and try to find a fitting pocket.
            // then check to see if it is, indeed, the wielded container.
            const pocket_choice pocket = dummy.best_pocket( *socks );
            REQUIRE( pocket.pocket );
            CHECK( pocket.pocket == get_only_pocket( container_wield ) );
            CHECK( pocket.pocket != get_only_pocket( container_wear ) );
        }
    }

    WHEN( "wearing a container with a nested rigid container" ) {
        detached_ptr<item> socks = item::spawn( itype_test_socks );
        detached_ptr<item> backpack_holder = item::spawn( itype_test_backpack );
        detached_ptr<item> container_holder = item::spawn( itype_test_jug_plastic );
        item &container = *container_holder;
        detached_ptr<item> filler = item::spawn( itype_test_rag );

        // wear the backpack item.
        REQUIRE( wear_it( dummy, std::move( backpack_holder ) ) );

        // nest the rigid container inside of the worn backpack.
        add_item_to_best_pocket( dummy, std::move( container_holder ) );

        // fill the parent container
        dummy.i_add_or_drop( *filler, 55 );

        THEN( "best pocket will be in nested rigid container" ) {
            // check to see if the item can be stored in any pocket.
            CHECK( dummy.can_stash_partial( *socks ) );

            // and try to find a fitting pocket.
            // then check to see if it is, indeed, the nested container.
            const pocket_choice best_pocket = dummy.best_pocket( *socks );
            REQUIRE( best_pocket.pocket );
            CHECK( best_pocket.pocket == get_only_pocket( container ) );
        }
    }

    WHEN( "wearing a container with a nested non-rigid container" ) {
        detached_ptr<item> socks = item::spawn( itype_test_socks );
        detached_ptr<item> backpack_holder = item::spawn( itype_test_backpack );
        item &backpack = *backpack_holder;
        detached_ptr<item> backpack_nested = item::spawn( itype_test_backpack );

        // wear the backpack item.
        REQUIRE( wear_it( dummy, std::move( backpack_holder ) ) );

        // nest the non-rigid container inside of the worn backpack.
        add_item_to_best_pocket( dummy, std::move( backpack_nested ) );

        THEN( "best pocket will be in worn container" ) {
            // check to see if the item can be stored in any pocket.
            CHECK( dummy.can_stash_partial( *socks ) );

            // and try to find a fitting pocket.
            // then check to see if it is, indeed, the worn container.
            const pocket_choice best_pocket = dummy.best_pocket( *socks );
            REQUIRE( best_pocket.pocket );
            CHECK( best_pocket.pocket == get_only_pocket( backpack ) );
        }
    }

    WHEN( "wearing a container with a nested rigid container which should be avoided" ) {
        detached_ptr<item> socks = item::spawn( itype_test_socks );
        detached_ptr<item> backpack_holder = item::spawn( itype_test_backpack );
        item &backpack = *backpack_holder;
        detached_ptr<item> container = item::spawn( itype_test_jug_plastic );

        // wear the backpack item.
        REQUIRE( wear_it( dummy, std::move( backpack_holder ) ) );

        // nest the rigid container inside of the worn backpack.
        item *nested_container = add_item_to_best_pocket( dummy, std::move( container ) );

        THEN( "best pocket for new item will be in worn container" ) {
            // check to see if the item can be stored in any pocket.
            CHECK( dummy.can_stash_partial( *socks ) );

            // and try to find a fitting pocket.
            // then check to see if it is, indeed, the worn backpack
            // when the nested (preferable) container should be avoided.
            const pocket_choice best_pocket = dummy.best_pocket( *socks, nested_container );
            REQUIRE( best_pocket.pocket );
            CHECK( best_pocket.pocket == get_only_pocket( backpack ) );
        }
    }
}

static void test_pickup_autoinsert_results( Character &u, bool wear, const item *nested,
        size_t on_ground, size_t in_top, size_t in_nested, bool count_by_charges = false )
{
    map &m = get_map();
    u.set_moves( 100 );
    // CDF: run the pickup activity to its end, giving it moves every turn
    process_activity( *u.as_player() );
    if( count_by_charges ) {
        size_t charges_on_ground = 0;
        for( const item *it : m.i_at( u.bub_pos() ) ) {
            charges_on_ground += it->charges;
        }
        CHECK( charges_on_ground == on_ground );
    } else {
        CHECK( m.i_at( u.bub_pos() ).size() == on_ground );
    }
    if( !wear ) {
        CHECK( !u.primary_weapon().is_null() );
        if( count_by_charges ) {
            size_t charges_in_top = -1;
            for( const item *it : u.primary_weapon().all_items_top() ) {
                if( nested == nullptr || it->typeId() != nested->typeId() ) {
                    charges_in_top = it->charges;
                }
            }
            CHECK( charges_in_top == in_top );
        } else {
            CHECK( u.primary_weapon().all_items_top().size() == in_top );
        }
        CHECK( u.top_items().empty() );
    } else {
        CHECK( u.primary_weapon().is_null() );
        CHECK( u.top_items().size() == 1 );
        if( count_by_charges ) {
            size_t charges_in_top = -1;
            for( const item *it : u.top_items().front()->all_items_top() ) {
                if( nested == nullptr || it->typeId() != nested->typeId() ) {
                    charges_in_top = it->charges;
                }
            }
            CHECK( charges_in_top == in_top );
        } else {
            CHECK( u.top_items().front()->all_items_top().size() == in_top );
        }
    }
    if( nested != nullptr ) {
        if( count_by_charges ) {
            size_t charges_in_top = 0;
            for( const item *it : nested->all_items_top() ) {
                charges_in_top += it->charges;
            }
            CHECK( charges_in_top == in_nested );
        } else {
            CHECK( nested->all_items_top().size() == in_nested );
        }
        item *top_it = wear ? u.top_items().front() : &u.primary_weapon();
        // top-level container still contains nested container
        CHECK( !!top_it->contained_where( *nested ) );
    }
}

// DDA: item_location::obtain into the character's inventory; the item then is found by its uid.
static item *give_item_to_char( Character &u, item &i )
{
    const std::string id = random_string( 10 );
    i.set_var( "uid", id );
    i.obtain( u );
    const std::vector<item *> found = u.items_with( [&id]( const item & it ) {
        return it.get_var( "uid" ) == id;
    } );
    REQUIRE( found.size() == 1 );
    return found.front();
}

static void test_pickup_autoinsert_sub_sub( bool autopickup, bool wear, bool soft_nested )
{
    map &m = get_map();
    Character &u = get_player_character();
    clear_all_state();
    clear_map();
    clear_character( *u.as_player(), false );
    item &cont1 = spawn_on_ground( m, u.bub_pos(), item::spawn( itype_test_jug_large_open ) );
    item &cont2 = spawn_on_ground( m, u.bub_pos(), item::spawn( itype_test_balloon ) );
    item &obj1 = spawn_on_ground( m, u.bub_pos(), item::spawn( itype_test_rock ) );
    item &obj2 = spawn_on_ground( m, u.bub_pos(), item::spawn( itype_test_rag ) );
    const itype_id obj1_type = obj1.typeId();
    const itype_id obj2_type = obj2.typeId();
    // the soft object, as a prototype for fill_with (DDA: `item soft_obj( itype_test_rag )`)
    detached_ptr<item> soft_obj_holder = item::spawn( itype_test_rag );
    item &soft_obj = *soft_obj_holder;
    assign_pickup_activity( *u.as_player(), { &obj1, &obj2 }, { 1, 1 }, autopickup );

    item *pack = nullptr;
    if( wear ) {
        REQUIRE( wear_it( u, item::spawn( itype_test_backpack ) ) );
        pack = u.top_items().front();
        REQUIRE( pack != nullptr );
        REQUIRE( m.i_at( u.bub_pos() ).size() == 4 );
        REQUIRE( u.primary_weapon().is_null() );
        REQUIRE( u.top_items().size() == 1 );
        REQUIRE( u.top_items().front()->all_items_top().empty() );
    } else {
        REQUIRE( !u.wield( item::spawn( itype_test_backpack ) ) );
        pack = &u.primary_weapon();
        REQUIRE_FALSE( pack->is_null() );
        REQUIRE( m.i_at( u.bub_pos() ).size() == 4 );
        REQUIRE( !u.primary_weapon().is_null() );
        REQUIRE( u.primary_weapon().all_items_top().empty() );
        REQUIRE( u.top_items().empty() );
    }

    WHEN( "backpack autoinsert as normal" ) {
        WHEN( "space available in backpack" ) {
            THEN( "pickup all" ) {
                test_pickup_autoinsert_results( u, wear, nullptr, 2, 2, 0 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, 60, false, false, true );
            THEN( "pickup none" ) {
                test_pickup_autoinsert_results( u, wear, nullptr, 4, 60, 0 );
            }
        }
    }

    WHEN( "no nested, backpack autoinsert disabled" ) {
        WHEN( "space available in backpack" ) {
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( "pickup none" ) {
                test_pickup_autoinsert_results( u, wear, nullptr, 4, 0, 0 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, 60, false, false, true );
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( "pickup none" ) {
                test_pickup_autoinsert_results( u, wear, nullptr, 4, 60, 0 );
            }
        }
    }

    WHEN( "no nested, all autoinsert settings as normal" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            THEN( "pickup all, nested empty" ) {
                test_pickup_autoinsert_results( u, wear, c, 1, 3, 0 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            THEN( ( soft_nested ? "pickup none, nested empty" : "pickup all, nested filled" ) ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 3, 60, 0 );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 1, 45, 2 );
                }
            }
        }
    }

    WHEN( "all autoinsert settings disabled" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( "pickup none, nested empty" ) {
                test_pickup_autoinsert_results( u, wear, c, 3, 1, 0 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( "pickup none, nested empty" ) {
                test_pickup_autoinsert_results( u, wear, c, 3, soft_nested ? 60 : 45, 0 );
            }
        }
    }

    WHEN( "top container autoinsert settings disabled" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( "pickup all, nested filled" ) {
                test_pickup_autoinsert_results( u, wear, c, 1, 1, 2 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( ( soft_nested ? "pickup none, nested empty" : "pickup all, nested filled" ) ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 3, 60, 0 );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 1, 45, 2 );
                }
            }
        }
    }

    WHEN( "nested container autoinsert settings disabled" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( "pickup all, nested empty" ) {
                test_pickup_autoinsert_results( u, wear, c, 1, 3, 0 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_disabled( true );
            }
            THEN( "pickup none, nested empty" ) {
                test_pickup_autoinsert_results( u, wear, c, 3, soft_nested ? 60 : 45, 0 );
            }
        }
    }

    WHEN( "nested container whitelisting 1 item" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.whitelist_item( obj1_type );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj1_type ) == 1 );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj2_type ) == 0 );
            }
            THEN( "pickup all, nested partly filled" ) {
                test_pickup_autoinsert_results( u, wear, c, 1, 2, 1 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.whitelist_item( obj1_type );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj1_type ) == 1 );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj2_type ) == 0 );
            }
            THEN( ( soft_nested ? "pickup none, nested empty" : "pickup one, nested partly filled" ) ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 3, 60, 0 );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 2, 45, 1 );
                }
            }
        }
    }

    WHEN( "nested container whitelisting 2 items" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.whitelist_item( obj1_type );
                pkts->settings.whitelist_item( obj2_type );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj1_type ) == 1 );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj2_type ) == 1 );
            }
            THEN( "pickup all, nested filled" ) {
                test_pickup_autoinsert_results( u, wear, c, 1, 1, 2 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.whitelist_item( obj1_type );
                pkts->settings.whitelist_item( obj2_type );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj1_type ) == 1 );
                REQUIRE( pkts->settings.get_item_whitelist().count( obj2_type ) == 1 );
            }
            THEN( ( soft_nested ? "pickup none, nested empty" : "pickup all, nested filled" ) ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 3, 60, 0 );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 1, 45, 2 );
                }
            }
        }
    }

    WHEN( "nested container accepts item stack" ) {
        obj1.detach();
        obj2.detach();
        if( soft_nested ) {
            cont1.detach();
        } else {
            cont2.detach();
        }
        item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
        WHEN( "item stack too large to fit in top-level container" ) {
            detached_ptr<item> stack = item::spawn( itype_test_pine_nuts );
            stack->charges = 300;
            item &obj3 = spawn_on_ground( m, u.bub_pos(), std::move( stack ) );
            REQUIRE( obj3.charges == 300 );
            u.cancel_activity();
            assign_pickup_activity( *u.as_player(), { &obj3 }, { 300 }, autopickup );
            THEN( ( soft_nested ? "pickup most, nested empty" : "pickup all, overflow into nested" ) ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 61, 239, 0, true );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 64, 176, 60, true );
                }
            }
        }
    }

    WHEN( "nested container blacklisting item stack" ) {
        obj1.detach();
        obj2.detach();
        if( soft_nested ) {
            cont1.detach();
        } else {
            cont2.detach();
        }
        item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
        for( item_pocket *pkts : c->get_container_pockets() ) {
            pkts->settings.blacklist_item( itype_test_pine_nuts );
            REQUIRE( pkts->settings.get_item_blacklist().count( itype_test_pine_nuts ) == 1 );
        }
        WHEN( "item stack too large to fit in top-level container" ) {
            detached_ptr<item> stack = item::spawn( itype_test_pine_nuts );
            stack->charges = 300;
            item &obj3 = spawn_on_ground( m, u.bub_pos(), std::move( stack ) );
            REQUIRE( obj3.charges == 300 );
            u.cancel_activity();
            assign_pickup_activity( *u.as_player(), { &obj3 }, { 300 }, autopickup );
            THEN( "pickup most, nested empty" ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 61, 239, 0, true );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 124, 176, 0, true );
                }
            }
        }
    }

    WHEN( "nested container whitelisting something other than item stack" ) {
        obj1.detach();
        if( soft_nested ) {
            cont1.detach();
        } else {
            cont2.detach();
        }
        item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
        for( item_pocket *pkts : c->get_container_pockets() ) {
            pkts->settings.whitelist_item( obj2_type );
            REQUIRE( pkts->settings.get_item_whitelist().count( obj2_type ) == 1 );
            REQUIRE( pkts->settings.get_item_whitelist().count( itype_test_pine_nuts ) == 0 );
        }
        obj2.detach();
        WHEN( "item stack too large to fit in top-level container" ) {
            detached_ptr<item> stack = item::spawn( itype_test_pine_nuts );
            stack->charges = 300;
            item &obj3 = spawn_on_ground( m, u.bub_pos(), std::move( stack ) );
            REQUIRE( obj3.charges == 300 );
            u.cancel_activity();
            assign_pickup_activity( *u.as_player(), { &obj3 }, { 300 }, autopickup );
            THEN( "pickup most, nested empty" ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 61, 239, 0, true );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 124, 176, 0, true );
                }
            }
        }
    }

    WHEN( "nested container high priority" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_priority( 50 );
                REQUIRE( pkts->settings.priority() == 50 );
            }
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_priority( 10 );
                REQUIRE( pkts->settings.priority() == 10 );
            }
            THEN( "pickup all, nested filled" ) {
                test_pickup_autoinsert_results( u, wear, c, 1, 1, 2 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_priority( 50 );
                REQUIRE( pkts->settings.priority() == 50 );
            }
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_priority( 10 );
                REQUIRE( pkts->settings.priority() == 10 );
            }
            THEN( ( soft_nested ? "pickup none, nested empty" : "pickup all, nested filled" ) ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 3, 60, 0 );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 1, 45, 2 );
                }
            }
        }
    }

    WHEN( "nested container same priority as top container" ) {
        WHEN( "space available in backpack" ) {
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_priority( 50 );
                REQUIRE( pkts->settings.priority() == 50 );
            }
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_priority( 50 );
                REQUIRE( pkts->settings.priority() == 50 );
            }
            THEN( "pickup all, nested empty" ) {
                test_pickup_autoinsert_results( u, wear, c, 1, 3, 0 );
            }
        }
        WHEN( "no space available in backpack" ) {
            pack->fill_with( soft_obj, soft_nested ? 59 : 44, false, false, true );
            item *c = give_item_to_char( u, soft_nested ? cont2 : cont1 );
            for( item_pocket *pkts : c->get_container_pockets() ) {
                pkts->settings.set_priority( 50 );
                REQUIRE( pkts->settings.priority() == 50 );
            }
            for( item_pocket *pkts : pack->get_container_pockets() ) {
                pkts->settings.set_priority( 50 );
                REQUIRE( pkts->settings.priority() == 50 );
            }
            THEN( ( soft_nested ? "pickup none, nested empty" : "pickup all, nested filled" ) ) {
                if( soft_nested ) {
                    test_pickup_autoinsert_results( u, wear, c, 3, 60, 0 );
                } else {
                    test_pickup_autoinsert_results( u, wear, c, 1, 45, 2 );
                }
            }
        }
    }
}

static void test_pickup_autoinsert_sub( bool autopickup, bool wear )
{
    GIVEN( "backpack contains rigid nested container" ) {
        test_pickup_autoinsert_sub_sub( autopickup, wear, false );
    }

    GIVEN( "backpack contains soft nested container" ) {
        test_pickup_autoinsert_sub_sub( autopickup, wear, true );
    }
}

static void test_pickup_autoinsert( bool autopickup )
{
    GIVEN( "player wearing backpack, items on ground" ) {
        test_pickup_autoinsert_sub( autopickup, true );
    }

    GIVEN( "player holding backpack, items on ground" ) {
        test_pickup_autoinsert_sub( autopickup, false );
    }
}

TEST_CASE( "picking_up_items_respects_pocket_autoinsert_settings", "[pocket][item]" )
{
    GIVEN( "autopickup" ) {
        test_pickup_autoinsert( true );
    }

    GIVEN( "manual pickup" ) {
        test_pickup_autoinsert( false );
    }
}
