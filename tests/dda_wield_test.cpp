// DDA source: tests/wield_test.cpp (DDA commit b4b62254ae2), P1-transplant J4.
// TEST_CASEs copied (3): Wield_test, Wield_time_test, unwield_to_pocket_preserves_inventory_cache
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md):
//  * item_location handles are item references: `wield( item_loc )` is
//    `wield( item, item.obtain_cost( who ) )` (the cost of fetching the item from where it is),
//    `wield( *item_loc )` is `wield( item )`; obtain_cost( who ) is item::obtain_cost.
//  * T11: CDF moves the item where DDA copies it. DDA's "the old item is not removed" checks of
//    the by-item wield become "the old item is gone from the ground"; each place says so.
//  * DDA's sheath has a holster pocket (data/json/items/armor/sheath.json); CDF's sheath has the
//    holster use action only until P1f, so the cases use TEST_DATA's test_wield_sheath, which is
//    DDA's sheath with its pocket.
//  * cache_has_item_with is DDA's inventory search cache, which CDF does not have; has_item_with
//    asks the same question.
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "coordinates.h"
#include "detached_ptr.h"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_aspirin( "aspirin" );

static const itype_id itype_backpack( "backpack" );

static const itype_id itype_backpack_hiking( "backpack_hiking" );

static const itype_id itype_bag_plastic( "bag_plastic" );

static const itype_id itype_halligan( "halligan" );

static const itype_id itype_knife_combat( "knife_combat" );

static const itype_id itype_knife_hunting( "knife_hunting" );
// DDA knife_hunting is 100 ml, CDF's is 250 ml (content backlog); the obtain cost test needs 100 ml
static const itype_id itype_test_knife_hunting( "test_knife_hunting" );

static const itype_id itype_metal_tank( "metal_tank" );

static const itype_id itype_pants_cargo( "pants_cargo" );

static const itype_id itype_sheath( "test_wield_sheath" );

static const itype_id itype_test_9mm_ammo( "test_9mm_ammo" );

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

static void wield_check_from_inv( avatar &guy, const itype_id &item_name, const int expected_moves )
{
    guy.remove_primary_weapon();
    clear_worn( guy );
    detached_ptr<item> spawned_holder = item::spawn( item_name, calendar::turn, 1 );
    item &spawned_item = *spawned_holder;
    detached_ptr<item> backpack_holder = item::spawn( itype_backpack );
    item &backpack = *backpack_holder;
    REQUIRE( backpack.can_contain( spawned_item ).success() );
    REQUIRE( wear_it( guy, std::move( backpack_holder ) ) );

    REQUIRE( backpack.put_in( std::move( spawned_holder ), pocket_type::CONTAINER ).success() );
    REQUIRE( backpack.num_item_stacks() == 1 );
    item &item_loc = backpack.only_item();
    CAPTURE( item_name );
    CAPTURE( item_loc.typeId() );

    guy.set_moves( 1000 );
    const int old_moves = guy.get_moves();
    REQUIRE( guy.wield( item_loc, item_loc.obtain_cost( guy ) ) );
    CAPTURE( guy.primary_weapon().typeId() );
    const int move_cost = old_moves - guy.get_moves();

    INFO( "Strength:" << guy.get_str() );
    CHECK( Approx( expected_moves ).epsilon( 0.1f ) == move_cost );
}

static void wield_check_from_ground( avatar &guy, const itype_id &item_name,
                                     const int expected_moves )
{
    item &spawned_item = spawn_on_ground( get_map(), guy.bub_pos(),
                                          item::spawn( item_name, calendar::turn, 1 ) );
    CHECK( spawned_item.obtain_cost( guy ) == Approx( expected_moves ).epsilon( 0.1f ) );
}

TEST_CASE( "Wield_test", "[wield]" )
{
    clear_all_state();
    clear_map();
    detached_ptr<item> knife_hunting_holder = item::spawn( itype_knife_hunting );
    item &knife_hunting = *knife_hunting_holder;
    detached_ptr<item> knife_combat_holder = item::spawn( itype_knife_combat );
    item &knife_combat = *knife_combat_holder;
    item &sheath_proto = *item::spawn_temporary( itype_sheath );

    REQUIRE( sheath_proto.can_holster( knife_combat ) );

    map &here = get_map();

    avatar &you = get_avatar();
    // TT4: a bare character, no DEBUG_STORAGE
    clear_character( you, false );

    GIVEN( "A Player" ) {
        tripoint_bub_ms pos = you.bub_pos();

        GIVEN( "with an item is on the ground" ) {
            item &spawned_item = spawn_on_ground( here, pos, item::spawn( itype_knife_hunting ) );

            REQUIRE_FALSE( here.i_at( pos ).empty() );

            WHEN( "trying to wield it using its item_location" ) {
                REQUIRE( you.wield( spawned_item, spawned_item.obtain_cost( you ) ) );
                item &wield_loc = you.primary_weapon();

                THEN( "you wield the item" ) {
                    REQUIRE_FALSE( wield_loc.is_null() );
                    REQUIRE( wield_loc.typeId() == itype_knife_hunting );
                }

                AND_THEN( "the old item is removed" ) {
                    CHECK( spawned_item.where() != item_location_type::map );
                    REQUIRE( here.i_at( pos ).empty() );
                }
            }

            WHEN( "trying to wield it" ) {
                REQUIRE( you.wield( spawned_item ) );
                item &wield_loc = you.primary_weapon();

                THEN( "the old item is not removed" ) {
                    // CDF (T11): wielding by item moves it as well; DDA copies it and the caller
                    // has to remove the old item. Nothing is left on the ground here.
                    INFO( "If wielded by item you have to manually remove the old item" );
                    CHECK( here.i_at( pos ).empty() );
                }

                AND_THEN( "you wield the item" ) {
                    REQUIRE_FALSE( wield_loc.is_null() );
                    REQUIRE( wield_loc.typeId() == itype_knife_hunting );
                }
            }
        }

        GIVEN( "you already wield an item" ) {
            you.remove_primary_weapon();
            REQUIRE( !you.wield( std::move( knife_hunting_holder ) ) );

            item &weapon = you.primary_weapon();
            REQUIRE_FALSE( weapon.is_null() );

            INFO( "Would normally open dialogbox to unwield, returns false if test_mode" );
            WHEN( "you try to wield it again with item" ) {
                REQUIRE_FALSE( you.wield( weapon ) );
            }

            AND_WHEN( "you try to wield it again with item_location" ) {
                REQUIRE_FALSE( you.wield( weapon, weapon.obtain_cost( you ) ) );
            }

            AND_WHEN( "you try and wield another item" ) {
                REQUIRE_FALSE( you.wield( knife_combat ) );
            }
        }

        GIVEN( "Wearing an sheath with another item in its pocket" ) {
            REQUIRE( you.amount_worn( itype_sheath ) == 0 );
            detached_ptr<item> sheath_holder = item::spawn( itype_sheath );
            item &sheath_loc = *sheath_holder;
            REQUIRE( wear_it( you, std::move( sheath_holder ) ) );
            REQUIRE( you.top_items().front() == &sheath_loc );
            REQUIRE( you.amount_worn( itype_sheath ) == 1 );
            REQUIRE( sheath_loc.empty() );

            const ret_val<item *> knife_added = you.try_add( std::move( knife_combat_holder ) );
            REQUIRE( knife_added.success() );
            item *knife_loc = knife_added.value();
            INFO( knife_loc->describe_location( &you ) );
            REQUIRE_FALSE( sheath_loc.empty() );
            REQUIRE( knife_loc->parent_item() != nullptr );

            WHEN( "wielding the worn sheath item" ) {
                REQUIRE( you.wield( sheath_loc ) );

                item &wielded_sheath = you.primary_weapon();
                REQUIRE_FALSE( wielded_sheath.empty() );
                knife_loc = &wielded_sheath.contents.first_item();

                THEN( "you wield the sheath" ) {
                    REQUIRE_FALSE( wielded_sheath.is_null() );
                }

                AND_THEN( "the knife is still in the sheath" ) {
                    REQUIRE( knife_loc != nullptr );
                    REQUIRE( knife_loc->parent_item() == &wielded_sheath );
                }

                AND_THEN( "you no longer wear the item" ) {
                    INFO( you.top_items().size() );
                    REQUIRE( you.amount_worn( itype_sheath ) == 0 );
                }

                AND_WHEN( "trying to wield contained item" ) {
                    REQUIRE_FALSE( you.wield( *knife_loc ) );
                }

                AND_WHEN( "trying to wield contained item_location" ) {
                    REQUIRE_FALSE( you.wield( *knife_loc, knife_loc->obtain_cost( you ) ) );

                    THEN( "you still wield the sheath" ) {
                        REQUIRE( you.is_wielding( wielded_sheath ) );
                    }
                }
            }

            WHEN( "wielding the worn sheath location" ) {
                REQUIRE( you.wield( sheath_loc, sheath_loc.obtain_cost( you ) ) );
                item &wielded = you.primary_weapon();
                REQUIRE_FALSE( wielded.empty() );

                THEN( "you wield the sheath" ) {
                    REQUIRE_FALSE( wielded.is_null() );
                }
                AND_THEN( "you no longer wear the item" ) {
                    REQUIRE( you.amount_worn( itype_sheath ) == 0 );
                }
            }
        }

        GIVEN( "item with charges wielded and on the ground" ) {
            const int charges = 100;
            detached_ptr<item> held = item::spawn( itype_test_9mm_ammo );
            held->charges = charges;
            item &spawned_item = spawn_on_ground( here, pos, item::spawn( itype_test_9mm_ammo ) );
            spawned_item.charges = charges;
            REQUIRE( !you.wield( std::move( held ) ) );
            item &wielded = you.primary_weapon();

            AND_THEN( "the wielded item has the correct amount of charges" ) {
                REQUIRE_FALSE( wielded.is_null() );
                REQUIRE( wielded.charges == charges );
            }

            AND_WHEN( "wielding the item" ) {
                REQUIRE( you.wield( spawned_item ) );
                THEN( "the charges get merged" ) {
                    REQUIRE( &wielded == &you.primary_weapon() );
                    REQUIRE( wielded.charges == 2 * charges );
                }
                AND_THEN( "the old item is not removed" ) {
                    // CDF (T11): the other stack is merged into the wielded one and is gone
                    INFO( "If wielded by item you have to manually remove the old item" );
                    REQUIRE( here.i_at( pos ).empty() );
                }
            }
            WHEN( "wielding the location" ) {
                REQUIRE( you.wield( spawned_item, spawned_item.obtain_cost( you ) ) );
                THEN( "the charges get merged" ) {
                    REQUIRE( &wielded == &you.primary_weapon() );
                    REQUIRE( wielded.charges == 2 * charges );
                }
                AND_THEN( "the old item is removed" ) {
                    REQUIRE( here.i_at( pos ).empty() );
                }
            }
        }
    }

    GIVEN( "An NPC" ) {
        tripoint_bub_ms next_to = you.bub_pos() + tripoint_north;
        npc &guy = spawn_npc( next_to, "test_talker" );

        g->load_npcs();
        // TT4: a bare NPC, no DEBUG_STORAGE
        clear_character( guy, false );

        GIVEN( "An item is on the ground" ) {
            item &spawned_item = spawn_on_ground( here, guy.bub_pos(),
                                                  item::spawn( itype_knife_hunting, calendar::turn, 1 ) );

            REQUIRE_FALSE( here.i_at( guy.bub_pos() ).empty() );

            WHEN( "trying to wield it using its item_location" ) {
                REQUIRE( guy.wield( spawned_item, spawned_item.obtain_cost( guy ) ) );
                item &weapon = guy.primary_weapon();

                THEN( "they wield the item" ) {
                    CHECK_FALSE( weapon.is_null() );
                    CHECK( weapon.typeId() == itype_knife_hunting );
                }

                AND_THEN( "the old item is removed" ) {
                    CHECK( here.i_at( guy.bub_pos() ).empty() );
                }
            }
        }

        GIVEN( "Wearing an sheath with another item in its pocket" ) {
            REQUIRE( guy.amount_worn( itype_sheath ) == 0 );
            detached_ptr<item> sheath_holder = item::spawn( itype_sheath );
            item &sheath_loc = *sheath_holder;
            REQUIRE( wear_it( guy, std::move( sheath_holder ) ) );
            REQUIRE( guy.top_items().front() == &sheath_loc );
            REQUIRE( sheath_loc.typeId() == itype_sheath );
            REQUIRE( guy.amount_worn( itype_sheath ) == 1 );
            REQUIRE( sheath_loc.empty() );

            const ret_val<item *> knife_added = guy.try_add( std::move( knife_combat_holder ) );
            REQUIRE( knife_added.success() );
            item *knife_loc = knife_added.value();
            INFO( knife_loc->describe_location( &guy ) );
            REQUIRE_FALSE( sheath_loc.empty() );
            REQUIRE( knife_loc->parent_item() != nullptr );

            WHEN( "wielding the worn sheath item" ) {
                REQUIRE( guy.wield( sheath_loc ) );

                item &wielded_sheath = guy.primary_weapon();
                REQUIRE_FALSE( wielded_sheath.empty() );
                knife_loc = &wielded_sheath.contents.first_item();

                THEN( "you wield the sheath" ) {
                    REQUIRE_FALSE( wielded_sheath.is_null() );
                }

                AND_THEN( "the knife is still in the sheath" ) {
                    REQUIRE( knife_loc != nullptr );
                    REQUIRE( knife_loc->parent_item() == &wielded_sheath );
                }

                AND_THEN( "you no longer wear the item" ) {
                    INFO( guy.top_items().size() );
                    REQUIRE( guy.amount_worn( itype_sheath ) == 0 );
                }

                AND_WHEN( "trying to wield contained item" ) {
                    REQUIRE( guy.wield( *knife_loc ) );

                    THEN( "you wield the knife, not the sheath" ) {
                        REQUIRE_FALSE( guy.primary_weapon().is_null() );
                        CHECK( guy.primary_weapon().typeId() == itype_knife_combat );
                    }
                }

                AND_WHEN( "trying to wield contained item_location" ) {
                    REQUIRE_FALSE( guy.wield( *knife_loc, knife_loc->obtain_cost( guy ) ) );

                    THEN( "you still wield the sheath" ) {
                        REQUIRE( guy.is_wielding( wielded_sheath ) );
                    }
                }
            }
        }

        AND_GIVEN( "already wielding an item" ) {
            guy.remove_primary_weapon();
            REQUIRE( !guy.wield( std::move( knife_hunting_holder ) ) );

            item &weapon = guy.primary_weapon();
            REQUIRE_FALSE( weapon.is_null() );

            WHEN( "trying to wield it again with item" ) {
                INFO( "some tests require returning true, even if nothing changes" );
                REQUIRE( guy.wield( weapon ) );
                THEN( "you wield the item again" ) {
                    REQUIRE_FALSE( guy.primary_weapon().is_null() );
                }
            }

            AND_WHEN( "trying to wield it again with item_location" ) {
                REQUIRE( guy.wield( weapon, weapon.obtain_cost( guy ) ) );
            }

            AND_WHEN( "trying and wield another item" ) {
                REQUIRE_FALSE( guy.can_stash( weapon ).success() );
                // TT2: a detached item is wielded through its holder; an empty result means it was taken
                REQUIRE( !guy.wield( std::move( knife_combat_holder ) ) );
                THEN( "wield the other item" ) {
                    REQUIRE_FALSE( guy.primary_weapon().is_null() );
                    REQUIRE( guy.primary_weapon().typeId() == itype_knife_combat );
                }

                AND_THEN( "the old item gets dropped" ) {
                    REQUIRE_FALSE( here.i_at( guy.bub_pos() ).empty() );
                }
            }
        }
    }
}

TEST_CASE( "Wield_time_test", "[wield]" )
{
    clear_all_state();
    clear_map();

    SECTION( "A knife in a sheath in cargo pants in a plastic bag in a backpack" ) {
        avatar &guy = get_avatar();
        clear_character( guy, false );
        detached_ptr<item> backpack_holder = item::spawn( itype_backpack );
        item &backpack = *backpack_holder;
        detached_ptr<item> plastic_bag_holder = item::spawn( itype_bag_plastic );
        item &plastic_bag = *plastic_bag_holder;
        detached_ptr<item> cargo_pants_holder = item::spawn( itype_pants_cargo );
        item &cargo_pants = *cargo_pants_holder;
        detached_ptr<item> sheath_holder = item::spawn( itype_sheath );
        item &sheath = *sheath_holder;
        detached_ptr<item> knife_holder = item::spawn( itype_test_knife_hunting );

        REQUIRE( wear_it( guy, std::move( backpack_holder ) ) );
        REQUIRE( backpack.put_in( std::move( plastic_bag_holder ), pocket_type::CONTAINER ).success() );
        REQUIRE( backpack.num_item_stacks() == 1 );

        REQUIRE( plastic_bag.put_in( std::move( cargo_pants_holder ),
                                     pocket_type::CONTAINER ).success() );
        REQUIRE( plastic_bag.num_item_stacks() == 1 );

        REQUIRE( cargo_pants.put_in( std::move( sheath_holder ), pocket_type::CONTAINER ).success() );
        REQUIRE( cargo_pants.num_item_stacks() == 1 );

        REQUIRE( sheath.put_in( std::move( knife_holder ), pocket_type::CONTAINER ).success() );
        REQUIRE( sheath.num_item_stacks() == 1 );

        item &knife_loc = sheath.only_item();

        const int knife_obtain_cost = knife_loc.obtain_cost( guy );
        // This is kind of bad, on linux/OSX this value is 112.
        // On mingw-64 on wine, and probably on VS this is 111.
        // Most likely this is due to floating point differences, but I wasn't able to find where.
        CAPTURE( knife_obtain_cost );
        CHECK( ( knife_obtain_cost == Approx( 105 ).margin( 1 ) ) );
    }

    SECTION( "Wielding without hand encumbrance" ) {
        avatar &guy = get_avatar();
        clear_character( guy, false );

        wield_check_from_inv( guy, itype_aspirin, 300 );
        clear_character( guy, false );
        wield_check_from_inv( guy, itype_knife_combat, 325 );
        clear_character( guy, false );
        wield_check_from_ground( guy, itype_metal_tank, 300 );
        clear_character( guy, false );
    }
}

// Storing the wielded weapon into a worn container via dispose_item's
// "Store in <bag>" path must refresh inv_search_caches; otherwise cached
// lookups miss the item.
TEST_CASE( "unwield_to_pocket_preserves_inventory_cache",
           "[wield][inventory_cache]" )
{
    clear_all_state();
    clear_map();
    avatar &they = get_avatar();
    clear_character( they, false );

    detached_ptr<item> backpack_holder = item::spawn( itype_backpack_hiking );
    item &backpack = *backpack_holder;
    REQUIRE( wear_it( they, std::move( backpack_holder ) ) );

    detached_ptr<item> halligan_holder = item::spawn( itype_halligan );
    REQUIRE( backpack.can_contain( *halligan_holder ).success() );
    REQUIRE( !they.wield( std::move( halligan_holder ) ) );
    REQUIRE( they.is_armed() );

    // Prime "HAS TYPE halligan" while wielded. CDF has no inventory search cache (DDA keeps
    // item_locations in it, which die when the item is moved out); has_item_with asks the same.
    const auto has_halligan = [&they]() {
        return they.has_item_with( []( const item & it ) {
            return it.typeId() == itype_halligan;
        } );
    };
    REQUIRE( has_halligan() );

    // Mimic the exact call dispose_item makes for "Store in <worn bag>".
    item &wielded_ref = they.primary_weapon();
    // avatar::store is the save function, so go through Character
    static_cast<Character &>( they ).store( backpack, wielded_ref, false,
                backpack.insert_cost( wielded_ref ),
                pocket_type::CONTAINER, true );

    REQUIRE_FALSE( they.is_armed() );
    REQUIRE( backpack.num_item_stacks() == 1 );

    CHECK( has_halligan() );
}
