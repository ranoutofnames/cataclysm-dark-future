// DDA source: tests/item_pocket_test.cpp (DDA commit b4b62254ae2). Compiled and enabled.
// TEST_CASEs copied (15): max_item_length, max_item_volume, max_container_volume,
// magazine_with_ammo_restriction, pocket_with_item_flag_restriction,
// holster_can_contain_one_fitting_item, pockets_containing_liquids, pockets_containing_gases,
// rigid_and_non-rigid_or_flexible_pockets, corpse_can_contain_anything, sealed_containers,
// when_one_pocket_is_better_than_another, pocket_favorites_allow_or_restrict_items,
// pocket_favorites_allow_or_restrict_containers, pocket_leak CUT; usb_drives_and_software was CUT
// until E1-10a (it needs E_FILE_STORAGE and the DDA usb_drive and software data) and is copied at
// the end of the file. The accepts_item/whitelist_item/blacklist_* pocket
// settings API this file uses exists in CDF src (item_pocket.h favorite_settings). Only
// is_better_favorite is absent, and in DDA it is a TODO comment too.
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
#include "itype_slots/islot_comestible.h"

static const ammotype ammo_45( "45" );

static const ammotype ammo_test_9mm( "test_9mm" );

static const itype_id itype_55gal_drum( "55gal_drum" );
static const itype_id itype_XL_holster( "XL_holster" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bag_plastic( "bag_plastic" ); //Purposefully nonsense
static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_debug_modular_m4_carbine( "debug_modular_m4_carbine" );
static const itype_id itype_ketchup( "ketchup" );
static const itype_id itype_mustard( "mustard" );
static const itype_id itype_paper( "paper" );
static const itype_id itype_pencil( "pencil" );
static const itype_id itype_shotgun_d( "shotgun_d" );
static const itype_id itype_shoulder_strap( "shoulder_strap" );
static const itype_id itype_software_math( "software_math" );
static const itype_id itype_steel_pan( "steel_pan" );
static const itype_id itype_stock_none( "stock_none" );
static const itype_id itype_test_45_ammo( "test_45_ammo" );
static const itype_id itype_test_9mm_ammo( "test_9mm_ammo" );
static const itype_id itype_test_apple( "test_apple" );
static const itype_id itype_test_backpack( "dda_test_backpack" );
static const itype_id itype_test_balloon( "test_balloon" );
static const itype_id itype_test_box( "dda_test_box" );
static const itype_id itype_test_can_drink( "test_can_drink" );
static const itype_id itype_test_clumsy_sword( "dda_test_clumsy_sword" );
static const itype_id itype_test_fire_ax( "test_fire_ax" );
static const itype_id itype_test_flashlight( "test_flashlight" );
static const itype_id itype_test_gas( "test_gas" );
static const itype_id itype_test_glock( "test_glock" );
static const itype_id itype_test_glockmag( "test_glockmag" );
static const itype_id itype_test_halligan( "dda_test_halligan" );
static const itype_id itype_test_jug_large_open( "test_jug_large_open" );
static const itype_id itype_test_jug_plastic( "dda_test_jug_plastic" );
static const itype_id itype_test_liquid( "test_liquid" );
static const itype_id itype_test_mini_backpack( "test_mini_backpack" );
static const itype_id itype_test_pine_nuts( "test_pine_nuts" );
static const itype_id itype_test_rag( "test_rag" );
static const itype_id itype_test_robofac_armor_rig( "test_robofac_armor_rig" );
static const itype_id itype_test_rock( "test_rock" );
static const itype_id itype_test_rod_14cm( "test_rod_14cm" );
static const itype_id itype_test_rod_15cm( "test_rod_15cm" );
static const itype_id itype_test_screwdriver( "dda_test_screwdriver" );
static const itype_id itype_test_socks( "test_socks" );
static const itype_id itype_test_sonic_screwdriver( "dda_test_sonic_screwdriver" );
static const itype_id itype_test_tool_belt_pocket_mix( "test_tool_belt_pocket_mix" );
static const itype_id itype_test_utility_belt( "test_utility_belt" );
static const itype_id itype_test_waterskin( "dda_test_waterskin" );
static const itype_id
itype_test_watertight_open_sealed_container_1L( "test_watertight_open_sealed_container_1L" );
static const itype_id itype_tums( "tums" );
static const itype_id itype_usb_drive( "usb_drive" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_clean( "water_clean" );

static const pocket_type pocket_container = pocket_type::CONTAINER;

// FIXME: The failure messaging from these helper functions is pretty terrible, due to the multiple
// CHECKs in each function. Capturing it.tname() helps only slightly. The `rate_can.value()` is an
// integer, so it is difficult to see when a failure reason is returned instead of success.

// Expect pocket.can_contain( it ) to be successful (contain_code::SUCCESS)
static void expect_can_contain( const item_pocket &pocket, const item &it )
{
    CAPTURE( it.tname() );
    const ret_val<item_pocket::contain_code> rate_can = pocket.can_contain( it );
    INFO( rate_can.str() );
    CHECK( rate_can.success() );
    CHECK( rate_can.str().empty() );
    CHECK( rate_can.value() == item_pocket::contain_code::SUCCESS );
}

// Expect pocket.can_contain( it ) to fail, with an expected reason and contain_code
static void expect_cannot_contain( const item_pocket &pocket, const item &it,
                                   const std::string &expect_reason,
                                   item_pocket::contain_code expect_code )
{
    CAPTURE( it.tname() );
    ret_val<item_pocket::contain_code> rate_can = pocket.can_contain( it );
    CHECK_FALSE( rate_can.success() );
    CHECK( rate_can.str() == expect_reason );
    CHECK( rate_can.value() == expect_code );
}

// Call pocket.insert_item( it ) and expect it to be successful (contain_code::SUCCESS)
static void expect_can_insert( item_pocket &pocket, const item &it )
{
    CAPTURE( it.tname() );
    CHECK( pocket.can_contain( it ).value() == item_pocket::contain_code::SUCCESS );
    ret_val<item *> rate_can = pocket.insert_item( it );
    CHECK( rate_can.success() );
    CHECK( rate_can.str().empty() );
    REQUIRE( rate_can.value() != nullptr );
    CHECK( rate_can.value()->stacks_with( it ) );
}

// Call pocket.insert_item( it ) and expect it to fail, with an expected reason and contain_code
static void expect_cannot_insert( item_pocket &pocket, const item &it,
                                  const std::string &expect_reason,
                                  item_pocket::contain_code expect_code )
{
    CAPTURE( it.tname() );
    CHECK( pocket.can_contain( it ).value() == expect_code );
    ret_val<item *> rate_can = pocket.insert_item( it );
    CHECK_FALSE( rate_can.success() );
    CHECK( rate_can.str() == expect_reason );
    CHECK( rate_can.value() == nullptr );
}

// Max item length
// ---------------
// Pocket "max_item_length" is the biggest item (by longest side) that can fit.
//
// Related JSON fields:
// "max_item_length"
//
// Functions:
// item_pocket::can_contain
//
TEST_CASE( "max_item_length", "[pocket][max_item_length]" )
{
    // Test items with different lengths
    item screwdriver( itype_test_screwdriver );
    item sonic( itype_test_sonic_screwdriver );
    item sword( itype_test_clumsy_sword );

    // Sheath that may contain items
    pocket_data data_sheath( pocket_type::CONTAINER );
    // Has plenty of weight/ volume, since we're only testing length
    data_sheath.raw_volume_capacity = 10_liter;
    data_sheath.max_contains_weight = 10_kilogram;

    GIVEN( "pocket has max_item_length enough for some items, but not others" ) {
        // Sheath is just long enough for sonic screwdriver
        const units::length max_length = sonic.length();
        data_sheath.max_item_length = max_length;
        item_pocket pocket_sheath( &data_sheath );

        THEN( "it can contain an item with longest_side shorter than max_item_length" ) {
            REQUIRE( screwdriver.length() < max_length );
            expect_can_contain( pocket_sheath, screwdriver );
        }

        THEN( "it can contain an item with longest_side equal to max_item_length" ) {
            REQUIRE( sonic.length() == max_length );
            expect_can_contain( pocket_sheath, sonic );
        }

        THEN( "it cannot contain an item with longest_side longer than max_item_length" ) {
            REQUIRE( sword.length() > max_length );
            expect_cannot_contain( pocket_sheath, sword, "item is too long",
                                   item_pocket::contain_code::ERR_TOO_BIG );
        }
    }

    // Default max_item_length assumes pocket is a cube, limiting max length to the diagonal
    // dimension of one side of the cube (the opening into the box, so to speak).
    //
    // For a 1-liter volume, a 10 cm cube encloses it, so the longest diagonal is:
    //
    //      10 cm * sqrt(2) =~ 14.14 cm
    //
    // This test ensures that a 1-liter box with unspecified max_item_length can indeed contain
    // items up to 14 cm in length, but not ones that are 15 cm.
    //
    // NOTE: In theory, the interior space of the cube can accommodate a longer item between its
    // opposite diagonals, assuming it is infinitely thin:
    //
    //      10 cm * sqrt(3) =~ 17.32 cm
    //
    // Items of such length are not currently allowed in a 1-liter pocket.

    GIVEN( "a 1-liter box without a defined max_item_length" ) {
        item box( itype_test_box );
        REQUIRE( box.get_total_capacity() == 1_liter );

        THEN( "it can hold an item 14 cm in length" ) {
            item rod_14( itype_test_rod_14cm );
            REQUIRE( rod_14.length() == 14_cm );

            REQUIRE( box.is_container_empty() );
            box.put_in( rod_14, pocket_type::CONTAINER );
            // Item went into the box
            CHECK_FALSE( box.is_container_empty() );
        }

        THEN( "it cannot hold an item 15 cm in length" ) {
            item rod_15( itype_test_rod_15cm );
            REQUIRE( rod_15.length() == 15_cm );

            REQUIRE( box.is_container_empty() );
            std::string dmsg = capture_debugmsg_during( [&box, &rod_15]() {
                ret_val<void> result = box.put_in( rod_15, pocket_type::CONTAINER );
                CHECK_FALSE( result.success() );
            } );
            CHECK_THAT( dmsg, Catch::Matchers::EndsWith( "item is too long" ) );
            // Box should still be empty
            CHECK( box.is_container_empty() );
        }
    }
}

// Max item volume
// ---------------
// "max_item_volume" is the size of the mouth or opening into the pocket. Liquids and gases, as well
// as "soft" items, can always fit through. All other items must fit within "max_item_volume" to be
// put in the pocket.
//
// Related JSON fields:
// "max_item_volume"
//
// Functions:
// item_pocket::can_contain
//
TEST_CASE( "max_item_volume", "[pocket][max_item_volume]" )
{
    // Test items
    item screwdriver( itype_test_screwdriver );
    item rag( itype_test_rag );
    item rock( itype_test_rock );
    item gas( itype_test_gas );
    item liquid( itype_test_liquid );

    // Air-tight, water-tight, jug-style container
    pocket_data data_jug( pocket_type::CONTAINER );
    data_jug.airtight = true;
    data_jug.watertight = true;

    // No significant limits on length or weight
    data_jug.max_item_length = 1_m;
    data_jug.max_contains_weight = 10_kilogram;

    // Jug capacity is more than enough for several rocks, rags, or screwdrivers
    data_jug.raw_volume_capacity = 2_liter;
    REQUIRE( data_jug.volume_capacity() > 5 * rock.volume() );
    REQUIRE( data_jug.volume_capacity() > 5 * rag.volume() );
    REQUIRE( data_jug.volume_capacity() > 10 * screwdriver.volume() );

    // But it has a narrow mouth, just barely enough to fit a screwdriver
    const units::volume max_volume = screwdriver.volume();
    data_jug.max_item_volume = max_volume;

    // The screwdriver will fit
    REQUIRE( screwdriver.volume() <= max_volume );
    // All other items have too much volume to fit the opening
    REQUIRE( rag.volume() > max_volume );
    REQUIRE( rock.volume() > max_volume );
    REQUIRE( gas.volume() > max_volume );
    REQUIRE( liquid.volume() > max_volume );

    GIVEN( "large airtight and watertight pocket with a narrow opening" ) {
        item_pocket pocket_jug( &data_jug );

        THEN( "it can contain liquids" ) {
            REQUIRE( liquid.made_of( phase_id::LIQUID ) );
            expect_can_contain( pocket_jug, liquid );
        }
        THEN( "it can contain gases" ) {
            REQUIRE( gas.made_of( phase_id::GAS ) );
            expect_can_contain( pocket_jug, gas );
        }
        THEN( "it can contain solid items that fit through the opening" ) {
            REQUIRE_FALSE( screwdriver.is_soft() );
            expect_can_contain( pocket_jug, screwdriver );
        }
        THEN( "it can contain soft items larger than the opening" ) {
            REQUIRE( rag.is_soft() );
            expect_can_contain( pocket_jug, rag );
        }
        THEN( "it cannot contain solid items larger than the opening" ) {
            REQUIRE_FALSE( rock.is_soft() );
            expect_cannot_contain( pocket_jug, rock, "item is too big",
                                   item_pocket::contain_code::ERR_TOO_BIG );
        }
    }
}

// Max container volume
// --------------------
// Normally, the "max_contains_volume" from pocket data JSON is the largest total volume of
// items the pocket can hold. It is returned by the pocket_data::max_contains_volume() function.
//
// However, pockets with an "ammo_restriction", the pocket_data::max_contains_volume() is derived
// from the size of the ammo given in the restriction.
//
// Related JSON fields:
// "max_contains_volume"
// "ammo_restriction"
//
// Functions:
// pocket_data::max_contains_volume
//
TEST_CASE( "max_container_volume", "[pocket][max_contains_volume]" )
{
    WHEN( "pocket has no ammo_restriction" ) {
        pocket_data data_box( pocket_type::CONTAINER );
        // Just a normal 1-liter box
        data_box.raw_volume_capacity = 1_liter;
        REQUIRE( data_box.ammo_restriction.empty() );

        THEN( "volume_capacity is the pocket's volume_capacity" ) {
            CHECK( data_box.volume_capacity() == 1_liter );
        }
    }

    WHEN( "pocket has ammo_restriction" ) {
        pocket_data data_ammo_box( pocket_type::CONTAINER );

        // 9mm ammo is 50 rounds per 250ml (or 200 rounds per liter), so this ammo box
        // should be exactly 1 liter in size, so it can contain this much ammo.
        data_ammo_box.ammo_restriction.emplace( ammo_test_9mm, 200 );
        REQUIRE_FALSE( data_ammo_box.ammo_restriction.empty() );

        // And because actual volume is derived from ammo needs, this volume should be ignored.
        data_ammo_box.raw_volume_capacity = 1_ml;

        THEN( "volume_capacity is based on the pocket's ammo type" ) {
            CHECK( data_ammo_box.volume_capacity() == 1_liter );
        }
    }

    WHEN( "pocket has multiple ammo_restrictions" ) {
        pocket_data data_ammo_box( pocket_type::CONTAINER );

        data_ammo_box.ammo_restriction.emplace( ammo_test_9mm, 200 );
        data_ammo_box.ammo_restriction.emplace( ammo_45, 90 );
        data_ammo_box.raw_volume_capacity = 1_ml;

        THEN( "volume_capacity uses the largest restricted ammo volume" ) {
            CHECK( data_ammo_box.volume_capacity() == 1_liter );
        }
    }

    WHEN( "pocket has multiple ammo_restrictions with a larger alternate ammo type" ) {
        pocket_data data_ammo_box( pocket_type::CONTAINER );

        data_ammo_box.ammo_restriction.emplace( ammo_test_9mm, 200 );
        data_ammo_box.ammo_restriction.emplace( ammo_45, 150 );
        data_ammo_box.raw_volume_capacity = 1_ml;

        THEN( "volume_capacity follows the most space-intensive ammo" ) {
            CHECK( data_ammo_box.volume_capacity() == 1250_ml );
        }
    }
}

// Ammo restriction
// ----------------
// Pockets with "ammo_restriction" defined have capacity based on those restrictions (from the
// type(s) of ammo and quantity of each. They can only contain ammo of those types and counts,
// and mixing different ammos in the same pocket is prohibited.
//
// Related JSON fields:
// "ammo_restriction"
//
// Functions:
// item_pocket::can_contain
// item_pocket::insert_item
//
TEST_CASE( "magazine_with_ammo_restriction", "[pocket][magazine][ammo_restriction]" )
{
    pocket_data data_mag( pocket_type::MAGAZINE );

    // ammo_restriction takes precedence over volume/length/weight,
    // so it doesn't matter what these are set to - they can even be 0.
    data_mag.raw_volume_capacity = 0_liter;
    data_mag.min_item_volume = 0_liter;
    data_mag.max_item_length = 0_m;
    data_mag.max_contains_weight = 0_gram;

    GIVEN( "magazine has 9mm ammo_restriction" ) {
        // Magazine ammo_restriction may be a single { ammotype, count } or a list of ammotypes and
        // counts. This mag 10 rounds of 9mm, as if it were defined in JSON as:
        //
        //      "ammo_restriction": { "9mm", 10 }
        //
        const int full_clip_qty = 10;
        data_mag.ammo_restriction.emplace( ammo_test_9mm, full_clip_qty );
        item_pocket pocket_mag( &data_mag );

        WHEN( "it does not already contain any ammo" ) {
            REQUIRE( pocket_mag.empty() );

            THEN( "it can contain a full clip of 9mm ammo" ) {
                const item ammo_9mm( itype_test_9mm_ammo, calendar::turn_zero, full_clip_qty );
                expect_can_contain( pocket_mag, ammo_9mm );
            }

            THEN( "it cannot contain items of the wrong ammo type" ) {
                item rock( itype_test_rock );
                item ammo_45( itype_test_45_ammo, calendar::turn_zero, 1 );
                expect_cannot_contain( pocket_mag, rock, "item is not the correct ammo type",
                                       item_pocket::contain_code::ERR_AMMO );
                expect_cannot_contain( pocket_mag, ammo_45, "item is not the correct ammo type",
                                       item_pocket::contain_code::ERR_AMMO );
            }

            THEN( "it cannot contain items that are not ammo" ) {
                item rag( itype_test_rag );
                expect_cannot_contain( pocket_mag, rag, "item is not ammunition",
                                       item_pocket::contain_code::ERR_AMMO );
            }
        }

        WHEN( "it is partly full of ammo" ) {
            const int half_clip_qty = full_clip_qty / 2;
            item ammo_9mm_half_clip( itype_test_9mm_ammo, calendar::turn_zero, half_clip_qty );
            expect_can_insert( pocket_mag, ammo_9mm_half_clip );

            THEN( "it can contain more of the same ammo" ) {
                item ammo_9mm_refill( itype_test_9mm_ammo, calendar::turn_zero,
                                      full_clip_qty - half_clip_qty );
                expect_can_contain( pocket_mag, ammo_9mm_refill );
            }

            THEN( "it cannot contain more ammo than ammo_restriction allows" ) {
                item ammo_9mm_overfill( itype_test_9mm_ammo, calendar::turn_zero,
                                        full_clip_qty - half_clip_qty + 1 );
                expect_cannot_contain( pocket_mag, ammo_9mm_overfill,
                                       "tried to put too many charges of ammo in item",
                                       item_pocket::contain_code::ERR_NO_SPACE );
            }
        }

        WHEN( "it is completely full of ammo" ) {
            item ammo_9mm_full_clip( itype_test_9mm_ammo, calendar::turn_zero, full_clip_qty );
            expect_can_insert( pocket_mag, ammo_9mm_full_clip );

            THEN( "it cannot contain any more of the same ammo" ) {
                item ammo_9mm_bullet( itype_test_9mm_ammo, calendar::turn_zero, 1 );
                expect_cannot_contain( pocket_mag, ammo_9mm_bullet,
                                       "tried to put too many charges of ammo in item",
                                       item_pocket::contain_code::ERR_NO_SPACE );
            }
        }
    }

    GIVEN( "magazine has multiple ammo_restrictions" ) {
        const int full_clip_qty_9mm = 10;
        const int full_clip_qty_45 = 6;
        data_mag.ammo_restriction.emplace( ammo_test_9mm, full_clip_qty_9mm );
        data_mag.ammo_restriction.emplace( ammo_45, full_clip_qty_45 );
        item_pocket pocket_mag( &data_mag );

        WHEN( "it does not already contain any ammo" ) {
            REQUIRE( pocket_mag.empty() );

            THEN( "it can contain a full clip of any allowed ammo type" ) {
                const item ammo_9mm( itype_test_9mm_ammo, calendar::turn_zero, full_clip_qty_9mm );
                const item ammo_45( itype_test_45_ammo, calendar::turn_zero, full_clip_qty_45 );
                expect_can_contain( pocket_mag, ammo_9mm );
                expect_can_contain( pocket_mag, ammo_45 );
            }

            THEN( "it cannot exceed any allowed ammo restriction" ) {
                item ammo_9mm_over( itype_test_9mm_ammo, calendar::turn_zero, full_clip_qty_9mm + 1 );
                item ammo_45_over( itype_test_45_ammo, calendar::turn_zero, full_clip_qty_45 + 1 );
                expect_cannot_contain( pocket_mag, ammo_9mm_over,
                                       "tried to put too many charges of ammo in item",
                                       item_pocket::contain_code::ERR_NO_SPACE );
                expect_cannot_contain( pocket_mag, ammo_45_over,
                                       "tried to put too many charges of ammo in item",
                                       item_pocket::contain_code::ERR_NO_SPACE );
            }
        }

        WHEN( "it is partly full of 9mm ammo" ) {
            const int half_clip_qty_9mm = full_clip_qty_9mm / 2;
            item ammo_9mm_half_clip( itype_test_9mm_ammo, calendar::turn_zero, half_clip_qty_9mm );
            expect_can_insert( pocket_mag, ammo_9mm_half_clip );

            THEN( "it can contain more of the same ammo" ) {
                item ammo_9mm_refill( itype_test_9mm_ammo, calendar::turn_zero,
                                      full_clip_qty_9mm - half_clip_qty_9mm );
                expect_can_contain( pocket_mag, ammo_9mm_refill );
            }

            THEN( "it cannot contain ammo of another allowed type" ) {
                item ammo_45( itype_test_45_ammo, calendar::turn_zero, 1 );
                expect_cannot_contain( pocket_mag, ammo_45, "can't mix different ammo",
                                       item_pocket::contain_code::ERR_NO_SPACE );
            }
        }

        WHEN( "it is partly full of .45 ammo" ) {
            const int half_clip_qty_45 = full_clip_qty_45 / 2;
            item ammo_45_half_clip( itype_test_45_ammo, calendar::turn_zero, half_clip_qty_45 );
            expect_can_insert( pocket_mag, ammo_45_half_clip );

            THEN( "it can contain more of the same ammo" ) {
                item ammo_45_refill( itype_test_45_ammo, calendar::turn_zero,
                                     full_clip_qty_45 - half_clip_qty_45 );
                expect_can_contain( pocket_mag, ammo_45_refill );
            }

            THEN( "it cannot contain ammo of another allowed type" ) {
                item ammo_9mm( itype_test_9mm_ammo, calendar::turn_zero, 1 );
                expect_cannot_contain( pocket_mag, ammo_9mm, "can't mix different ammo",
                                       item_pocket::contain_code::ERR_NO_SPACE );
            }
        }
    }

}

// Flag restriction
// ----------------
// The "get_flag_restrictions" list from pocket data JSON gives compatible item flag(s) for this pocket.
// An item with any one of those tags may be inserted into the pocket.
//
// Related JSON fields:
// "flag_restriction"
// "min_item_volume"
// "max_contains_volume"
//
// Functions:
// item_pocket::can_contain
// pocket_data::max_contains_volume
//
TEST_CASE( "pocket_with_item_flag_restriction", "[pocket][flag_restriction]" )
{
    // Items with BELT_CLIP flag
    item screwdriver( itype_test_screwdriver );
    item sonic( itype_test_sonic_screwdriver );
    item halligan( itype_test_halligan );
    item axe( itype_test_fire_ax );

    // Items without BELT_CLIP flag
    item rag( itype_test_rag );
    item rock( itype_test_rock );

    // Ensure the tools have expected volume relationships
    // (too small, minimum size, maximum size, too big)
    REQUIRE( screwdriver.volume() < sonic.volume() );
    REQUIRE( sonic.volume() < halligan.volume() );
    REQUIRE( halligan.volume() < axe.volume() );

    // Test pocket with BELT_CLIP flag
    pocket_data data_belt( pocket_type::CONTAINER );

    // Sonic screwdriver is the smallest item it can hold
    data_belt.min_item_volume = sonic.volume();
    // Halligan bar is the largest item it can hold
    data_belt.raw_volume_capacity = halligan.volume();

    // Plenty of length and weight, since we only care about flag restrictions here
    data_belt.max_item_length = 10_m;
    data_belt.max_contains_weight = 10_kilogram;

    // TODO: Add test for pocket with multiple flag_restriction entries, and ensure
    // items with any of those flags can be inserted in the pocket.

    GIVEN( "pocket with BELT_CLIP flag restriction" ) {
        data_belt.add_flag_restriction( flag_BELT_CLIP );
        item_pocket pocket_belt( &data_belt );

        GIVEN( "item has BELT_CLIP flag" ) {
            REQUIRE( screwdriver.has_flag( flag_BELT_CLIP ) );
            REQUIRE( sonic.has_flag( flag_BELT_CLIP ) );
            REQUIRE( halligan.has_flag( flag_BELT_CLIP ) );
            REQUIRE( axe.has_flag( flag_BELT_CLIP ) );

            WHEN( "item volume is less than min_item_volume" ) {
                REQUIRE( screwdriver.volume() < data_belt.min_item_volume );

                THEN( "pocket cannot contain it, because it is too small" ) {
                    expect_cannot_contain( pocket_belt, screwdriver, "item is too small",
                                           item_pocket::contain_code::ERR_TOO_SMALL );
                }

                THEN( "item cannot be inserted into the pocket" ) {
                    expect_cannot_insert( pocket_belt, screwdriver, "item is too small",
                                          item_pocket::contain_code::ERR_TOO_SMALL );
                }
            }

            WHEN( "item volume is equal to min_item_volume" ) {
                REQUIRE( sonic.volume() == data_belt.min_item_volume );

                THEN( "pocket can contain it" ) {
                    expect_can_contain( pocket_belt, sonic );
                }

                THEN( "item can be inserted successfully" ) {
                    expect_can_insert( pocket_belt, sonic );
                }
            }

            WHEN( "item volume is equal to max_contains_volume" ) {
                REQUIRE( halligan.volume() == data_belt.volume_capacity() );

                THEN( "pocket can contain it" ) {
                    expect_can_contain( pocket_belt, halligan );
                }

                THEN( "item can be inserted successfully" ) {
                    expect_can_insert( pocket_belt, halligan );
                }
            }

            WHEN( "item volume is greater than max_contains_volume" ) {
                REQUIRE( axe.volume() > data_belt.volume_capacity() );

                THEN( "pocket cannot contain it, because it is too big" ) {
                    expect_cannot_contain( pocket_belt, axe, "item is too big",
                                           item_pocket::contain_code::ERR_TOO_BIG );
                }

                THEN( "item cannot be inserted into the pocket" ) {
                    expect_cannot_insert( pocket_belt, axe, "item is too big",
                                          item_pocket::contain_code::ERR_TOO_BIG );
                }
            }
        }

        GIVEN( "item without BELT_CLIP flag" ) {
            REQUIRE_FALSE( rag.has_flag( flag_BELT_CLIP ) );
            REQUIRE_FALSE( rock.has_flag( flag_BELT_CLIP ) );
            // Ensure they are not too large otherwise
            REQUIRE_FALSE( rag.volume() > data_belt.volume_capacity() );
            REQUIRE_FALSE( rock.volume() > data_belt.volume_capacity() );

            THEN( "pocket cannot contain it, because it does not have the flag" ) {
                expect_cannot_contain( pocket_belt, rag, "holster does not accept this item type or form factor",
                                       item_pocket::contain_code::ERR_FLAG );
                expect_cannot_contain( pocket_belt, rock, "holster does not accept this item type or form factor",
                                       item_pocket::contain_code::ERR_FLAG );
            }
        }
    }
}

// Holster
// -------
// Pockets with the "holster" attribute set to true may hold only a single item or stack.
//
// Related JSON fields:
// "holster"
// "max_item_length"
// "max_contains_volume"
// "max_contains_weight"
//
// Functions:
// item_pocket::can_contain
// item_pocket::insert_item
//
TEST_CASE( "holster_can_contain_one_fitting_item", "[pocket][holster]" )
{
    // Start with a basic test handgun from data/mods/TEST_DATA/items.json
    item glock( itype_test_glock );

    // Construct data for a holster to perfectly fit this gun
    pocket_data data_holster( pocket_type::CONTAINER );
    data_holster.holster = true;
    data_holster.raw_volume_capacity = glock.volume();
    data_holster.max_item_length = glock.length();
    data_holster.max_contains_weight = glock.weight();

    GIVEN( "holster has enough volume, length, and weight capacity" ) {
        // Use the perfectly-fitted holster data
        item_pocket pocket_holster( &data_holster );

        THEN( "it can contain the item" ) {
            expect_can_contain( pocket_holster, glock );
        }

        THEN( "item can be successfully inserted" ) {
            expect_can_insert( pocket_holster, glock );
        }
    }

    GIVEN( "holster has not enough weight capacity" ) {
        // Reduce perfect holster weight capacity by 1 gram
        data_holster.max_contains_weight = glock.weight() - 1_gram;
        item_pocket pocket_holster( &data_holster );

        THEN( "it cannot contain the item, because it is too heavy" ) {
            expect_cannot_contain( pocket_holster, glock, "item is too heavy",
                                   item_pocket::contain_code::ERR_TOO_HEAVY );
        }

        THEN( "item cannot be successfully inserted" ) {
            expect_cannot_insert( pocket_holster, glock, "item is too heavy",
                                  item_pocket::contain_code::ERR_TOO_HEAVY );
        }
    }

    GIVEN( "holster has not enough volume capacity" ) {
        // Reduce perfect holster volume capacity by 1 ml
        data_holster.raw_volume_capacity = glock.volume() - 1_ml;
        item_pocket pocket_holster( &data_holster );

        THEN( "it cannot contain the item, because it is too big" ) {
            expect_cannot_contain( pocket_holster, glock, "item is too big",
                                   item_pocket::contain_code::ERR_TOO_BIG );
        }

        THEN( "item cannot be successfully inserted" ) {
            expect_cannot_insert( pocket_holster, glock, "item is too big",
                                  item_pocket::contain_code::ERR_TOO_BIG );
        }
    }

    // TODO: Add test for item with longest_side greater than max_item_length

    GIVEN( "holster already contains an item" ) {
        // Put another item in the holster first
        item_pocket pocket_holster( &data_holster );
        expect_can_insert( pocket_holster, item( itype_test_glock ) );

        THEN( "it cannot contain the item, because holster can only hold one item" ) {
            expect_cannot_contain( pocket_holster, glock, "holster already contains an item",
                                   item_pocket::contain_code::ERR_NO_SPACE );
        }

        THEN( "another item cannot be successfully inserted" ) {
            expect_cannot_insert( pocket_holster, glock, "holster already contains an item",
                                  item_pocket::contain_code::ERR_NO_SPACE );
        }
    }
}

// Watertight pockets
// ------------------
// To contain liquids, a pocket must have the "watertight" attribute set to true. After a pocket has
// been partially filled with a liquid, adding a different liquid to it is prohibited, since liquids
// can't mix.
//
// Related JSON fields:
// "watertight"
//
// Functions:
// item_pocket::watertight
//
TEST_CASE( "pockets_containing_liquids", "[pocket][watertight][liquid]" )
{
    // Liquids
    item ketchup( itype_ketchup, calendar::turn_zero, item::default_charges_tag{} );
    item mustard( itype_mustard, calendar::turn_zero, item::default_charges_tag{} );

    // Non-liquids
    item rock( itype_test_rock );
    item glock( itype_test_glock );

    // Large watertight container
    pocket_data data_bucket( pocket_type::CONTAINER );
    data_bucket.watertight = true;
    data_bucket.raw_volume_capacity = 10_liter;
    data_bucket.max_item_length = 1_m;
    data_bucket.max_contains_weight = 10_kilogram;

    GIVEN( "pocket is watertight" ) {
        item_pocket pocket_bucket( &data_bucket );
        REQUIRE( pocket_bucket.watertight() );

        WHEN( "pocket is empty" ) {
            REQUIRE( pocket_bucket.empty() );

            THEN( "it can contain liquid items" ) {
                expect_can_contain( pocket_bucket, ketchup );
                expect_can_contain( pocket_bucket, mustard );
            }

            THEN( "it can contain non-liquid items" ) {
                expect_can_contain( pocket_bucket, rock );
                expect_can_contain( pocket_bucket, glock );
            }
        }

        WHEN( "pocket already contains some liquid" ) {
            expect_can_insert( pocket_bucket, ketchup );
            REQUIRE_FALSE( pocket_bucket.empty() );

            THEN( "it can contain more of the same liquid" ) {
                expect_can_contain( pocket_bucket, ketchup );
            }

            THEN( "it cannot contain a different liquid" ) {
                expect_cannot_contain( pocket_bucket, mustard, "can't mix liquid with contained item",
                                       item_pocket::contain_code::ERR_LIQUID );
            }

            THEN( "it cannot contain a non-liquid" ) {
                expect_cannot_contain( pocket_bucket, rock, "can't put non liquid into pocket with liquid",
                                       item_pocket::contain_code::ERR_LIQUID );
            }
        }

        WHEN( "pocket already contains a non-liquid" ) {
            expect_can_insert( pocket_bucket, rock );
            REQUIRE_FALSE( pocket_bucket.empty() );

            THEN( "it can contain other non-liquids" ) {
                expect_can_contain( pocket_bucket, glock );
            }

            THEN( "it cannot contain a liquid" ) {
                expect_cannot_contain( pocket_bucket, ketchup, "can't mix liquid with contained item",
                                       item_pocket::contain_code::ERR_LIQUID );
                expect_cannot_contain( pocket_bucket, mustard, "can't mix liquid with contained item",
                                       item_pocket::contain_code::ERR_LIQUID );
            }
        }
    }

    GIVEN( "pocket is not watertight" ) {
        // Poke a hole in the bucket
        data_bucket.watertight = false;
        item_pocket pocket_bucket( &data_bucket );

        THEN( "it cannot contain liquid items" ) {
            expect_cannot_contain( pocket_bucket, ketchup, "can't contain liquid",
                                   item_pocket::contain_code::ERR_LIQUID );
            expect_cannot_contain( pocket_bucket, mustard, "can't contain liquid",
                                   item_pocket::contain_code::ERR_LIQUID );
        }

        THEN( "it can still contain non-liquid items" ) {
            expect_can_contain( pocket_bucket, rock );
            expect_can_contain( pocket_bucket, glock );
        }
    }
}

// Airtight pockets
// ----------------
// To contain gases, a pocket must have the "airtight" attribute set to true.
//
// Related JSON fields:
// "airtight"
//
// Functions:
// item_pocket::airtight
//
TEST_CASE( "pockets_containing_gases", "[pocket][airtight][gas]" )
{
    item gas( itype_test_gas, calendar::turn_zero, item::default_charges_tag{} );

    // A potentially airtight container
    pocket_data data_balloon( pocket_type::CONTAINER );

    // Has capacity for several charges of gas
    data_balloon.raw_volume_capacity = 4 * gas.volume();
    data_balloon.max_contains_weight = 4 * gas.weight();
    // Avoid any length restrictions
    data_balloon.max_item_length = 1_m;

    GIVEN( "pocket is airtight" ) {
        data_balloon.airtight = true;
        item_pocket pocket_balloon( &data_balloon );
        REQUIRE( pocket_balloon.airtight() );

        THEN( "it can contain a gas" ) {
            expect_can_contain( pocket_balloon, gas );
        }

        // TODO: Test that mixing gases in a pocket is prohibited, like it is for liquids.
    }

    GIVEN( "pocket is not airtight" ) {
        // Leaky balloon!
        data_balloon.airtight = false;
        item_pocket pocket_balloon( &data_balloon );
        REQUIRE_FALSE( pocket_balloon.airtight() );

        THEN( "it cannot contain a gas" ) {
            expect_cannot_contain( pocket_balloon, gas, "can't contain gas",
                                   item_pocket::contain_code::ERR_GAS );
        }
    }
}

// Pocket rigidity
// ---------------
// When a pocket is "rigid", the total volume of the pocket itself does not change, like it were a
// glass jar (having the same outer volume whether full or empty).
//
// When "rigid" is false (the default), the pocket grows larger as items fill it up.
//
// Related JSON fields:
// "rigid"
//
// Functions:
// item_pocket::can_contain
// item_pocket::insert_item
// item_pocket::item_size_modifier
//
TEST_CASE( "rigid_and_non-rigid_or_flexible_pockets", "[pocket][rigid][flexible]" )
{
    item rock( itype_test_rock );

    // Pocket with enough space for 2 rocks
    pocket_data data_sock( pocket_type::CONTAINER );
    data_sock.raw_volume_capacity = 2 * rock.volume();

    // Can hold plenty of length and weight (we only care about volume here)
    data_sock.max_item_length = 5 * rock.length();
    data_sock.max_contains_weight = 10 * rock.weight();

    GIVEN( "a non-rigid pocket" ) {
        // Sock is freshly washed, so it's non-rigid
        data_sock.rigid = false;
        item_pocket pocket_sock( &data_sock );

        WHEN( "pocket is empty" ) {
            REQUIRE( pocket_sock.empty() );
            THEN( "it can contain an item" ) {
                expect_can_contain( pocket_sock, rock );
            }

            THEN( "item_size_modifier is zero" ) {
                CHECK( pocket_sock.item_size_modifier() == 0_ml );
            }
        }

        WHEN( "pocket is partially filled" ) {
            // One rock should fill the sock half-way
            expect_can_insert( pocket_sock, item( itype_test_rock ) );
            REQUIRE_FALSE( pocket_sock.empty() );

            THEN( "it can contain another item" ) {
                expect_can_contain( pocket_sock, rock );
            }

            THEN( "item_size_modifier is the contained item volume" ) {
                CHECK( pocket_sock.item_size_modifier() == rock.volume() );
            }
        }

        WHEN( "pocket is full" ) {
            // Two rocks should be enough to fill it
            expect_can_insert( pocket_sock, item( itype_test_rock ) );
            expect_can_insert( pocket_sock, item( itype_test_rock ) );

            REQUIRE_FALSE( pocket_sock.empty() );
            REQUIRE( pocket_sock.full( true ) );

            THEN( "it cannot contain another item" ) {
                expect_cannot_contain( pocket_sock, rock, "not enough space",
                                       item_pocket::contain_code::ERR_NO_SPACE );
            }

            THEN( "item_size_modifier is the total of contained item volumes" ) {
                CHECK( pocket_sock.item_size_modifier() == 2 * rock.volume() );
            }
        }
    }

    GIVEN( "a rigid pocket" ) {
        // Our sock got all crusty with zombie guts and is rigid now.
        data_sock.rigid = true;
        item_pocket pocket_sock( &data_sock );
        REQUIRE( pocket_sock.rigid() );

        WHEN( "pocket is empty" ) {
            REQUIRE( pocket_sock.empty() );

            THEN( "it can contain an item" ) {
                expect_can_contain( pocket_sock, rock );
            }

            THEN( "item_size_modifier is zero" ) {
                CHECK( pocket_sock.item_size_modifier() == 0_ml );
            }
        }

        WHEN( "pocket is full" ) {
            expect_can_insert( pocket_sock, item( itype_test_rock ) );
            expect_can_insert( pocket_sock, item( itype_test_rock ) );

            REQUIRE_FALSE( pocket_sock.empty() );
            REQUIRE( pocket_sock.full( true ) );

            THEN( "it cannot contain another item" ) {
                expect_cannot_contain( pocket_sock, rock, "not enough space",
                                       item_pocket::contain_code::ERR_NO_SPACE );
            }

            THEN( "item_size_modifier is still zero" ) {
                CHECK( pocket_sock.item_size_modifier() == 0_ml );
            }
        }
    }
}

// Corpses
// -------
//
// Functions:
// item_pocket::can_contain
// item_pocket::insert_item
//
TEST_CASE( "corpse_can_contain_anything", "[pocket][corpse]" )
{
    item rock( itype_test_rock );
    item glock( itype_test_glock );

    pocket_data data_corpse( pocket_type::CORPSE );

    GIVEN( "a corpse" ) {
        item_pocket pocket_corpse( &data_corpse );

        // For corpses, can_contain is true to prevent unnecessary "spilling"
        THEN( "it can contain items" ) {
            expect_can_contain( pocket_corpse, rock );
            expect_can_contain( pocket_corpse, glock );
        }

        THEN( "items can be added to the corpse" ) {
            expect_can_insert( pocket_corpse, rock );
            expect_can_insert( pocket_corpse, glock );
        }
    }
}

// Sealed pockets
// --------------
//
// Functions:
// item_pocket::seal
// item_pocket::unseal
// item_pocket::sealed
// item_pocket::sealable
//
TEST_CASE( "sealed_containers", "[pocket][seal]" )
{
    item water( itype_water );

    GIVEN( "sealable can" ) {
        item can( itype_test_can_drink );

        // Ensure it has exactly one contained pocket, and get that pocket for testing
        std::vector<item_pocket *> can_pockets = can.contents.get_pockets( pocket_type::CONTAINER );
        REQUIRE( !can_pockets.empty() );
        REQUIRE( can_pockets.size() == 1 );
        item_pocket &pocket = *can_pockets.front();
        // Must be sealable, but not sealed initially
        REQUIRE( pocket.sealable() );
        REQUIRE_FALSE( pocket.sealed() );

        // Sealing does not work when empty
        WHEN( "pocket is empty" ) {
            REQUIRE( pocket.empty() );
            THEN( "it cannot be sealed" ) {
                CHECK_FALSE( pocket.seal() );
                CHECK_FALSE( pocket.sealed() );
                // Remains sealable
                CHECK( pocket.sealable() );
            }
        }

        // But after inserting something, sealing can succeed
        WHEN( "pocket contains something" ) {
            REQUIRE( pocket.insert_item( water ).success() );
            THEN( "it can be sealed" ) {
                CHECK( pocket.seal() );
                CHECK( pocket.sealed() );
                // Remains sealable
                CHECK( pocket.sealable() );
            }
        }

        GIVEN( "pocket is already sealed" ) {
            REQUIRE( pocket.insert_item( water ).success() );
            REQUIRE( pocket.seal() );
            REQUIRE( pocket.sealed() );

            WHEN( "it becomes unsealed" ) {
                pocket.unseal();
                THEN( "it is no longer sealed" ) {
                    CHECK_FALSE( pocket.sealed() );
                    // Remains sealable
                    CHECK( pocket.sealable() );
                }
            }
        }
    }

    GIVEN( "non-sealable jug" ) {
        item jug( itype_test_jug_plastic );

        // Ensure it has exactly one contained pocket, and get that pocket for testing
        std::vector<item_pocket *>jug_pockets = jug.contents.get_pockets( pocket_type::CONTAINER );
        REQUIRE( !jug_pockets.empty() );
        REQUIRE( jug_pockets.size() == 1 );
        item_pocket &pocket = *jug_pockets.front();
        // Must NOT be sealable
        REQUIRE_FALSE( pocket.sealable() );
        REQUIRE_FALSE( pocket.sealed() );

        THEN( "it cannot be sealed" ) {
            // Sealing fails when empty
            REQUIRE( pocket.empty() );
            CHECK_FALSE( pocket.seal() );
            CHECK_FALSE( pocket.sealed() );
            // Sealing also fails after inserting item
            REQUIRE( pocket.insert_item( water ).success() );
            CHECK_FALSE( pocket.seal() );
            CHECK_FALSE( pocket.sealed() );
        }
    }
}

// Better pockets
// --------------
// Pockets are ranked according to item_pocket::better_pocket, which considers player-defined
// favorites, ammo restrictions, spoilage, watertightness, available volume and other factors.
//
// Functions:
// item_pocket::better_pocket
//
TEST_CASE( "when_one_pocket_is_better_than_another", "[pocket][better]" )
{
    // TODO:
    // settings.is_better_favorite() is top priority
    // settings.priority() takes next priority
    // has_item_stacks_with(it) is better than one that doesn't stack
    // pockets restricted by ammo should try to get filled first
    // pockets restricted by flag should try to get filled first
    // if remaining volume is equal, lower obtain_cost is better
    // pockets with less extra encumbrance should be prioritized (#53162)
    // pockets without ripoff chance should be prioritized (#53162)

    // A and B: Two generic sets of pocket data for comparison
    pocket_data data_a( pocket_type::CONTAINER );
    pocket_data data_b( pocket_type::CONTAINER );

    // Candidate items to compare pockets with
    item liquid( itype_test_liquid );
    item apple( itype_test_apple );
    item rock( itype_test_rock );

    SECTION( "for perishable food, lower spoil_multiplier is better" ) {
        REQUIRE( apple.is_comestible() );
        REQUIRE( apple.get_comestible()->spoils != 0_seconds );
        data_a.spoil_multiplier = 1.0;
        data_b.spoil_multiplier = 0.5;
        CHECK( item_pocket( &data_a ).better_pocket( item_pocket( &data_b ), apple ) );
    }

    SECTION( "for solid item, non-watertight pocket is better" ) {
        REQUIRE( rock.made_of( phase_id::SOLID ) );
        data_a.watertight = true;
        data_b.watertight = false;
        CHECK( item_pocket( &data_a ).better_pocket( item_pocket( &data_b ), rock ) );
    }

    SECTION( "rigid pockets are preferable to non-rigid ones" ) {
        data_a.rigid = false;
        data_b.rigid = true;
        CHECK( item_pocket( &data_a ).better_pocket( item_pocket( &data_b ), rock ) );
    }

    SECTION( "pocket with less remaining volume is better" ) {
        data_a.raw_volume_capacity = 2_liter;
        data_b.raw_volume_capacity = 1_liter;
        CHECK( item_pocket( &data_a ).better_pocket( item_pocket( &data_b ), rock ) );
    }
}

// Pocket favorites
// ----------------
// Item pockets can be configured to only allow certain items automatically.
// The rules defined are also known as pocket autopickup rules.
//
// Functions:
// item_pocket::favorite_settings::accepts_item
// item_pocket::favorite_settings::blacklist_item
// item_pocket::favorite_settings::whitelist_item
// item_pocket::favorite_settings::get_item_whitelist
// item_pocket::favorite_settings::get_item_blacklist
//
TEST_CASE( "pocket_favorites_allow_or_restrict_items", "[pocket][favorite][item]" )
{
    item test_item( itype_test_rock );

    item test_item_same_category( itype_test_rag );
    REQUIRE( test_item.get_category_shallow().id ==
             test_item_same_category.get_category_shallow().id );
    REQUIRE_FALSE( test_item.typeId() == test_item_same_category.typeId() );

    item test_item_different_category( itype_test_apple );
    REQUIRE_FALSE( test_item.get_category_shallow().id ==
                   test_item_different_category.get_category_shallow().id );
    REQUIRE_FALSE( test_item.typeId() == test_item_different_category.typeId() );

    // Default settings allow all items
    SECTION( "no favourites" ) {
        WHEN( "no whitelist or blacklist specified" ) {
            item_pocket::favorite_settings settings;
            THEN( "all items allowed" ) {
                CHECK( settings.accepts_item( test_item ) );
                CHECK( settings.accepts_item( test_item_same_category ) );
                CHECK( settings.accepts_item( test_item_different_category ) );
            }
        }
    }

    SECTION( "blacklists" ) {
        WHEN( "item category blacklisted" ) {
            item_pocket::favorite_settings settings;
            settings.blacklist_category( test_item.get_category_shallow().id );
            THEN( "that category blocked" ) {
                REQUIRE_FALSE( settings.accepts_item( test_item ) );
                REQUIRE_FALSE( settings.accepts_item( test_item_same_category ) );
                REQUIRE( settings.accepts_item( test_item_different_category ) );
            }
        }

        WHEN( "item blacklisted" ) {
            item_pocket::favorite_settings settings;
            settings.blacklist_item( test_item.typeId() );
            THEN( "that item blocked" ) {
                REQUIRE_FALSE( settings.accepts_item( test_item ) );
                REQUIRE( settings.accepts_item( test_item_same_category ) );
                REQUIRE( settings.accepts_item( test_item_different_category ) );
            }
        }
    }

    SECTION( "whitelists" ) {
        WHEN( "item category whitelisted" ) {
            item_pocket::favorite_settings settings;
            settings.whitelist_category( test_item.get_category_shallow().id );
            THEN( "other categories blocked" ) {
                REQUIRE( settings.accepts_item( test_item ) );
                REQUIRE( settings.accepts_item( test_item_same_category ) );
                REQUIRE_FALSE( settings.accepts_item( test_item_different_category ) );
            }
        }

        WHEN( "item whitelisted" ) {
            item_pocket::favorite_settings settings;
            settings.whitelist_item( test_item.typeId() );
            THEN( "other items blocked" ) {
                REQUIRE( settings.accepts_item( test_item ) );
                REQUIRE_FALSE( settings.accepts_item( test_item_same_category ) );
                REQUIRE_FALSE( settings.accepts_item( test_item_different_category ) );
            }
        }
    }

    SECTION( "mixing whitelist and blacklists" ) {
        WHEN( "same item first whitelisted then blacklisted" ) {
            item_pocket::favorite_settings settings;
            settings.whitelist_item( test_item.typeId() );
            REQUIRE( settings.get_item_whitelist().count( test_item.typeId() ) );
            settings.blacklist_item( test_item.typeId() );
            THEN( "item should be removed from the whitelist" ) {
                REQUIRE_FALSE( settings.get_item_whitelist().count( test_item.typeId() ) );
            }
        }

        WHEN( "same item first blacklisted then whitelisted" ) {
            item_pocket::favorite_settings settings;
            settings.blacklist_item( test_item.typeId() );
            REQUIRE( settings.get_item_blacklist().count( test_item.typeId() ) );
            settings.whitelist_item( test_item.typeId() );
            THEN( "item should be removed from the blacklist" ) {
                REQUIRE_FALSE( settings.get_item_blacklist().count( test_item.typeId() ) );
            }
        }

        WHEN( "same category first whitelisted then blacklisted" ) {
            item_pocket::favorite_settings settings;
            settings.whitelist_category( test_item.get_category_shallow().id );
            REQUIRE( settings.get_category_whitelist().count( test_item.get_category_shallow().id ) );
            settings.blacklist_category( test_item.get_category_shallow().id );
            THEN( "category should be removed from the whitelist" ) {
                REQUIRE_FALSE( settings.get_category_whitelist().count( test_item.get_category_shallow().id ) );
            }
        }

        WHEN( "same category first blacklisted then whitelisted" ) {
            item_pocket::favorite_settings settings;
            settings.blacklist_category( test_item.get_category_shallow().id );
            REQUIRE( settings.get_category_blacklist().count( test_item.get_category_shallow().id ) );
            settings.whitelist_category( test_item.get_category_shallow().id );
            THEN( "category should be removed from the blacklist" ) {
                REQUIRE_FALSE( settings.get_category_blacklist().count( test_item.get_category_shallow().id ) );
            }
        }

        WHEN( "category whitelisted but item blacklisted" ) {
            item_pocket::favorite_settings settings;
            settings.whitelist_category( test_item.get_category_shallow().id );
            settings.blacklist_item( test_item.typeId() );
            THEN( "item blacklist override category allownace" ) {
                REQUIRE_FALSE( settings.accepts_item( test_item ) );
                REQUIRE( settings.accepts_item( test_item_same_category ) );
                // Not blacklisted, but not whitelisted either
                REQUIRE_FALSE( settings.accepts_item( test_item_different_category ) );
            }
        }

        WHEN( "category whitelisted and item whitelisted" ) {
            item_pocket::favorite_settings settings;
            settings.whitelist_category( test_item.get_category_shallow().id );
            settings.whitelist_item( test_item.typeId() );
            THEN( "either category or item must match" ) {
                REQUIRE( settings.accepts_item( test_item ) );
                REQUIRE( settings.accepts_item( test_item_same_category ) );
                REQUIRE_FALSE( settings.accepts_item( test_item_different_category ) );
            }
        }

        WHEN( "category blacklisted but item whitelisted" ) {
            item_pocket::favorite_settings settings;
            settings.blacklist_category( test_item.get_category_shallow().id );
            settings.whitelist_item( test_item.typeId() );
            THEN( "item allowance override category restrictions" ) {
                REQUIRE( settings.accepts_item( test_item ) );
                REQUIRE_FALSE( settings.accepts_item( test_item_same_category ) );
                // Not blacklisted, there's no category whitelist
                REQUIRE( settings.accepts_item( test_item_different_category ) );
            }
        }

        WHEN( "category blacklisted and item blacklisted" ) {
            item_pocket::favorite_settings settings;
            settings.blacklist_category( test_item.get_category_shallow().id );
            settings.blacklist_item( test_item.typeId() );
            THEN( "both category and item are blocked" ) {
                REQUIRE_FALSE( settings.accepts_item( test_item ) );
                REQUIRE_FALSE( settings.accepts_item( test_item_same_category ) );
                // Not blacklisted
                REQUIRE( settings.accepts_item( test_item_different_category ) );
            }
        }
    }
}

TEST_CASE( "pocket_favorites_allow_or_restrict_containers", "[pocket][favorite][item]" )
{
    item_pocket::favorite_settings settings;

    GIVEN( "item container is empty" ) {
        item item_plastic_bag( itype_bag_plastic );
        REQUIRE( item_plastic_bag.contents.empty() );

        WHEN( "item container is whitelisted" ) {
            settings.whitelist_item( item_plastic_bag.typeId() );

            THEN( "item container should be accepted" ) {
                REQUIRE( settings.accepts_item( item_plastic_bag ) );
            }
        }
        WHEN( "item container is blacklisted" ) {
            settings.blacklist_item( item_plastic_bag.typeId() );

            THEN( "item container should not be accepted" ) {
                REQUIRE_FALSE( settings.accepts_item( item_plastic_bag ) );
            }
        }
        WHEN( "item container category is whitelisted" ) {
            settings.whitelist_category( item_plastic_bag.get_category_shallow().id );

            THEN( "item container should be accepted" ) {
                REQUIRE( settings.accepts_item( item_plastic_bag ) );
            }
        }
        WHEN( "item container category is blacklisted" ) {
            settings.blacklist_category( item_plastic_bag.get_category_shallow().id );

            THEN( "item container should not be accepted" ) {
                REQUIRE_FALSE( settings.accepts_item( item_plastic_bag ) );
            }
        }
        WHEN( "item container is not listed in rules" ) {
            THEN( "item container should be accepted" ) {
                REQUIRE( settings.accepts_item( item_plastic_bag ) );
            }
        }
    }

    GIVEN( "item container is not empty" ) {
        item item_plastic_bag = item( itype_bag_plastic );
        item item_paper = item( itype_paper );
        item item_pencil = item( itype_pencil );

        item_plastic_bag.force_insert_item( item::spawn( item_paper ), pocket_container );
        item_plastic_bag.force_insert_item( item::spawn( item_pencil ), pocket_container );

        WHEN( "all items in container are whitelisted" ) {
            settings.whitelist_item( item_paper.typeId() );
            settings.whitelist_item( item_pencil.typeId() );

            THEN( "container should be accepted" ) {
                REQUIRE( settings.accepts_item( item_plastic_bag ) );
                REQUIRE_FALSE( settings.accepts_item( item( itype_bag_plastic ) ) );
            }
            WHEN( "item container is blacklisted" ) {
                settings.blacklist_item( item_plastic_bag.typeId() );

                THEN( "item container should not be accepted" ) {
                    REQUIRE_FALSE( settings.accepts_item( item_plastic_bag ) );
                }
            }
        }
        WHEN( "only some items in container are whitelisted" ) {
            settings.whitelist_item( item_paper.typeId() );

            THEN( "container should not be accepted" ) {
                REQUIRE_FALSE( settings.accepts_item( item_plastic_bag ) );
            }
            WHEN( "item container is whitelisted" ) {
                settings.whitelist_item( item_plastic_bag.typeId() );

                THEN( "item container should be accepted" ) {
                    REQUIRE( settings.accepts_item( item_plastic_bag ) );
                }
            }
        }
        WHEN( "all item categories in container are whitelisted" ) {
            settings.whitelist_category( item_paper.get_category_shallow().id );
            settings.whitelist_category( item_pencil.get_category_shallow().id );

            THEN( "container should be accepted" ) {
                REQUIRE( settings.accepts_item( item_plastic_bag ) );
                REQUIRE_FALSE( settings.accepts_item( item( itype_bag_plastic ) ) );
            }
            WHEN( "item container category is blacklisted" ) {
                settings.blacklist_category( item_plastic_bag.get_category_shallow().id );

                THEN( "item container should not be accepted" ) {
                    REQUIRE_FALSE( settings.accepts_item( item_plastic_bag ) );
                }
            }
        }
        WHEN( "only some item categories in container are whitelisted" ) {
            settings.whitelist_category( item_paper.get_category_shallow().id );

            THEN( "container should not be accepted" ) {
                REQUIRE_FALSE( settings.accepts_item( item_plastic_bag ) );
            }
            WHEN( "item container category is whitelisted" ) {
                settings.whitelist_category( item_plastic_bag.get_category_shallow().id );

                THEN( "item container should be accepted" ) {
                    REQUIRE( settings.accepts_item( item_plastic_bag ) );
                }
            }
        }
        WHEN( "no items in container are listed in rules" ) {
            THEN( "container should be accepted" ) {
                REQUIRE( settings.accepts_item( item_plastic_bag ) );
            }
        }
    }

    GIVEN( "item container contains liquid" ) {
        item item_clean_water = item( itype_water_clean );
        item item_bottled_water = item( itype_bottle_plastic );
        REQUIRE( !item_bottled_water.fill_with( item::spawn( item_clean_water ) ) );

        WHEN( "liquid item is whitelisted" ) {
            settings.whitelist_item( item_clean_water.typeId() );
            REQUIRE( settings.accepts_item( item_clean_water ) );

            THEN( "item container containing liquid item should be accepted" ) {
                REQUIRE( settings.accepts_item( item_bottled_water ) );
                REQUIRE_FALSE( settings.accepts_item( item( itype_bottle_plastic ) ) );
            }
        }
        WHEN( "liquid item is blacklisted" ) {
            settings.blacklist_item( item_clean_water.typeId() );
            REQUIRE_FALSE( settings.accepts_item( item_clean_water ) );

            THEN( "item container containing liquid item should not be accepted" ) {
                REQUIRE_FALSE( settings.accepts_item( item_bottled_water ) );
                REQUIRE( settings.accepts_item( item( itype_bottle_plastic ) ) );
            }
        }
        WHEN( "liquid item is not listed in rules" ) {
            settings.whitelist_item( itype_paper );

            THEN( "item container containing liquid item should not be accepted" ) {
                REQUIRE_FALSE( settings.accepts_item( item_bottled_water ) );
            }
        }
    }
}

// E1-10a (PC-E1): DDA tests/item_pocket_test.cpp:1890, copied as is. The case was CUT while
// usb_drive was a BN GENERIC item (digital storage) and software_math a BN GENERIC item; both are
// the DDA e-file items now (data/json/items/tool/electronics.json, items/software.json).
TEST_CASE( "usb_drives_and_software", "[pocket][software]" )
{
    item usb( itype_usb_drive );
    item software( itype_software_math );
    // USB drives aren't containers, and cannot "contain" software, but software can be inserted
    CHECK_FALSE( usb.can_contain( software ).success() );
    CHECK( usb.put_in( software, pocket_type::E_FILE_STORAGE ).success() );
}

// CUT pocket_leak: needs DDA's item temperature model (item::set_item_temperature,
// item::get_freeze_point, item::is_frozen_liquid with units::temperature) which BN does not have,
// and item::only_item (J3). Not hidden with [.]: removed with this reason.
