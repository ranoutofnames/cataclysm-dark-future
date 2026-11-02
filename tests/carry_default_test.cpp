// CDF CARRY1: container pockets of worn armor with explicit pocket_data default to `carry`.
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace
{

// CDF: id -> sum of the CONTAINER non-holster pockets' max_contains_volume, from the item JSON
// under data/json/items/armor (re-derived in CARRY1 step 1), and the DDA CONTAINER non-holster
// total from PL-10b table-242-shared-armor.tsv (dda_cont_nonholster_ml), -1 where the id is
// not in that table.
struct volume_id {
    const char *id;
    int cdf_ml;
    int dda_ml;
};

const std::vector<volume_id> &volume_ids()
{
    static const std::vector<volume_id> ids = {
        // CDF: storage.json:1143-1180: one 60 L pocket plus two 2500 ml sheath holster pockets. carried_capacity()
    // sums every CONTAINER pocket (item_pocket::ok_default_containers), carry or not, so it already
    // rises by 65 L before CARRY1; free_carry_volume() uses the carry pockets only and rises by 60 L. (the sheath holsters are holster pockets)
        { "survivor_hiking_backpack", 60000, -1 },
        { "jerrypack", 10000, 10000 }, // DDA: table-242 jerrypack
        { "javelin_bag", 6250, 6250 }, // DDA: table-242 javelin_bag
        { "chestrig", 3000, 3000 }, // DDA: table-242 chestrig
        { "grenadebandolier", 3000, 3000 }, // DDA: table-242 grenadebandolier
        { "power_armor_grenadebandolier", 3000, -1 },
        { "ammo_satchel", 1500, 1500 }, // DDA: table-242 ammo_satchel
        { "power_armor_ammo_satchel", 1500, -1 },
        { "legpouch_large", 1000, 1000 }, // DDA: table-242 legpouch_large
        { "power_armor_legpouch_large", 1000, -1 },
        { "chestpouch", 750, 750 }, // DDA: table-242 chestpouch
        { "shoulder_holster", 750, 750 }, // DDA: table-242 shoulder_holster
        { "power_armor_shoulder_holster", 750, -1 },
        { "boots_western", 500, 500 }, // DDA: table-242 boots_western
        { "drinking_hat", 500, 500 }, // DDA: table-242 drinking_hat
        { "bootstrap", 400, 400 }, // DDA: table-242 bootstrap
        { "legpouch", 250, 250 }, // DDA: table-242 legpouch
    };
    return ids;
}

// CDF: the 20 ids whose explicit CONTAINER pocket is restricted (ammo_restriction) and has no
// max_contains_volume in JSON (CARRY1 step 1).
const std::vector<const char *> &zero_volume_ids()
{
    static const std::vector<const char *> ids = {
        "ammo_pouch", "quiver", "quiver_birchbark", "quiver_large", "quiver_large_birchbark",
        "quiver_atlatl", "quiver_birchbark_atlatl", "ammo_satchel_leg", "bandolier_pistol",
        "bandolier_rifle", "bandolier_shotgun", "torso_bandolier_shotgun", "flintlock_pouch",
        "bandolier_wrist", "grenade_pouch", "stone_pouch", "bandolier_grenade",
        "power_armor_ammo_pouch", "power_armor_ammo_satchel_leg", "power_armor_grenade_pouch",
    };
    return ids;
}

bool counts_as_container( const pocket_data &pd )
{
    return pd.type == pocket_type::CONTAINER && !pd.holster && !pd.ablative;
}

} // namespace

TEST_CASE( "carry1_volume_ids_have_storage", "[carry1][pocket]" )
{
    clear_all_state();
    for( const volume_id &v : volume_ids() ) {
        INFO( v.id );
        detached_ptr<item> it = item::spawn( itype_id( v.id ), calendar::turn );
        CHECK( it->get_storage() == v.cdf_ml * 1_ml );
        if( v.dda_ml >= 0 ) {
            CHECK( it->get_storage() == v.dda_ml * 1_ml );
        }
    }
}

TEST_CASE( "carry1_zero_volume_pockets_carry", "[carry1][pocket]" )
{
    clear_all_state();
    for( const char *id : zero_volume_ids() ) {
        INFO( id );
        detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
        const itype &type = *it->type;
        REQUIRE( type.explicit_pockets );
        int carry_pockets = 0;
        for( const item_pocket &pocket : it->get_contents().get_pockets() ) {
            if( pocket.get_type() == pocket_type::CONTAINER && !pocket.is_holster() ) {
                CHECK( pocket.is_carry() );
                carry_pockets++;
            }
        }
        CHECK( carry_pockets >= 1 );
        // the restricted pocket accepts an item it is meant for
        const pocket_data *restricted = nullptr;
        for( const pocket_data &pd : type.pockets ) {
            if( counts_as_container( pd ) && !pd.ammo_restriction.empty() ) {
                restricted = &pd;
                break;
            }
        }
        REQUIRE( restricted != nullptr );
        const ammotype &ammo = restricted->ammo_restriction.begin()->first;
        const itype *ammo_type = nullptr;
        for( const itype *t : item_controller->all() ) {
            if( t->ammo && t->ammo->type == ammo ) {
                ammo_type = t;
                break;
            }
        }
        INFO( ammo.str() );
        REQUIRE( ammo_type != nullptr );
        detached_ptr<item> round = item::spawn( ammo_type->get_id(), calendar::turn, 1 );
        bool accepted = false;
        for( const item_pocket &pocket : it->get_contents().get_pockets() ) {
            if( pocket.is_carry() && pocket.can_contain( *round ).success() ) {
                accepted = true;
            }
        }
        CHECK( accepted );
    }
}

TEST_CASE( "carry1_backpack_raises_capacity", "[carry1][pocket]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    const units::volume cap0 = u.carried_capacity();
    const units::volume free0 = u.free_carry_volume();

    u.wear_item( item::spawn( itype_id( "survivor_hiking_backpack" ), calendar::turn ), false );
    // CDF: storage.json:1143-1180: one 60 L pocket plus two 2500 ml sheath holster pockets. carried_capacity()
    // sums every CONTAINER pocket (item_pocket::ok_default_containers), carry or not, so it already
    // rises by 65 L before CARRY1; free_carry_volume() uses the carry pockets only and rises by 60 L.
    CHECK( u.carried_capacity() == cap0 + 65000_ml );
    CHECK( u.free_carry_volume() == free0 + 60000_ml );
    CHECK( u.carried_volume() == 0_ml );

    // 80 test_rock of 250 ml (TEST_DATA items.json) are 20 L
    for( int i = 0; i < 80; i++ ) {
        u.i_add( item::spawn( itype_id( "test_rock" ), calendar::turn ) );
    }
    CHECK( u.carried_volume() == 20000_ml );
    CHECK( u.free_carry_volume() == free0 + 40000_ml );

    clear_character( u, false );
    CHECK( u.carried_capacity() == cap0 );
    CHECK( u.free_carry_volume() == free0 );
}

TEST_CASE( "carry1_content_pockets_stay_non_carry", "[carry1][pocket]" )
{
    clear_all_state();
    // plate pockets of a vest are ablative, never carry; the esapi vest has no other container pocket
    {
        detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
        int plates = 0;
        for( const item_pocket &pocket : vest->get_contents().get_pockets() ) {
            if( pocket.is_ablative() ) {
                plates++;
                CHECK_FALSE( pocket.is_carry() );
            }
        }
        CHECK( plates > 0 );
        // CDF: cdf_ballistic_vests.json, the esapi vest has only plate pockets: storage 0
        CHECK( vest->get_storage() == 0_ml );
    }
    // the holster pocket of the shoulder holster stays non-carry, the container pocket carries
    {
        detached_ptr<item> holster = item::spawn( itype_id( "shoulder_holster" ), calendar::turn );
        int holsters = 0;
        int carry = 0;
        for( const item_pocket &pocket : holster->get_contents().get_pockets() ) {
            if( pocket.is_holster() ) {
                holsters++;
                CHECK_FALSE( pocket.is_carry() );
            } else if( pocket.is_carry() ) {
                carry++;
            }
        }
        CHECK( holsters == 1 );
        CHECK( carry == 1 );
    }
    // magazine pockets of a gun are not CONTAINER pockets and not carry
    {
        detached_ptr<item> gun = item::spawn( itype_id( "glock_19" ), calendar::turn );
        int mags = 0;
        for( const item_pocket &pocket : gun->get_contents().get_pockets() ) {
            if( pocket.get_type() == pocket_type::MAGAZINE ||
                pocket.get_type() == pocket_type::MAGAZINE_WELL ) {
                mags++;
                CHECK_FALSE( pocket.is_carry() );
            }
        }
        CHECK( mags > 0 );
    }
}

TEST_CASE( "carry1_all_armor_with_container_volume_carries", "[carry1][pocket]" )
{
    clear_all_state();
    // CDF: ids allowed to have container volume and no storage. Both are TEST_DATA fixtures of
    // ported DDA tests (dda_pocket_items.json) that model a DDA container as armor and opt out
    // with "carry": false, so the DDA tests keep their meaning (a waterskin is a container, a
    // liquid pours into the rig's reservoirs). test_quiver (dda_quiver_items.json) is the fixture of
    // display_name_includes_item_contents, which expects the "(10/20)" non-carry display, so it opts
    // out as well. test_backpack (items.json) is the plain container the ported item spawn and
    // recipe tests fill as a recipe/group container (an armor item with only carry pockets is not
    // a container, so in_container leaves the contents outside); it opts out too (the carry
    // fixture is test_backpack_carry). No game item is listed.
    const std::set<std::string> exceptions = { "dda_test_waterskin", "test_robofac_armor_rig",
                                               "test_quiver", "test_backpack" };
    int checked = 0;
    for( const itype *t : item_controller->all() ) {
        if( !t->armor || !t->explicit_pockets ) {
            continue;
        }
        units::volume sum = 0_ml;
        for( const pocket_data &pd : t->pockets ) {
            if( counts_as_container( pd ) ) {
                sum += pd.raw_volume_capacity;
            }
        }
        if( sum <= 0_ml || exceptions.count( t->get_id().str() ) > 0 ) {
            continue;
        }
        checked++;
        INFO( t->get_id().str() );
        detached_ptr<item> it = item::spawn( t->get_id(), calendar::turn );
        CHECK( it->get_storage() > 0_ml );
    }
    CHECK( checked > 0 );
}

TEST_CASE( "carry1_non_armor_unchanged", "[carry1][pocket]" )
{
    clear_all_state();
    // CDF: 1st_aid (med.json, GENERIC, 2700 ml) and pot_canning (dining_kitchen.json, 20 L)
    // have explicit container pockets and are not armor
    for( const char *id : { "1st_aid", "pot_canning" } ) {
        INFO( id );
        detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
        REQUIRE( it->type->explicit_pockets );
        REQUIRE_FALSE( it->type->armor );
        int containers = 0;
        for( const item_pocket &pocket : it->get_contents().get_pockets() ) {
            if( pocket.get_type() == pocket_type::CONTAINER ) {
                containers++;
                CHECK_FALSE( pocket.is_carry() );
            }
        }
        CHECK( containers > 0 );
        CHECK( it->get_storage() == 0_ml );
    }
}
