#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "itype.h"
#include "iuse_actor.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "rng.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

// EOD1: the DDA EOD kit as CDF data (data/json/items/armor/cdf_eod_kit.json, the groups in
// itemgroups/Clothing_Gear/gear.json and military.json, the hosts army_personal_locker and
// cop_armory). DDA b4b62254ae2. Data only.

namespace
{
using spawned_items = std::vector<detached_ptr<item>>;

auto spawn( const std::string &group ) -> spawned_items
{
    return item_group::items_from( item_group_id( group ) );
}

// size of the attach_molle action of an item, or -1 when it has none
auto molle_size( const item &it ) -> int
{
    const use_function *use = it.get_use( "attach_molle" );
    if( use == nullptr ) {
        return -1;
    }
    const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>( use->get_actor_ptr() );
    return actor == nullptr ? -1 : actor->size;
}

// type ids of the items directly inside an item
auto inner_ids( const item &it ) -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for( const item *inner : it.contents.all_items_top() ) {
        ids.push_back( inner->typeId().str() );
    }
    return ids;
}

auto has_id( const std::vector<std::string> &ids, const std::string &id ) -> bool
{
    return std::find( ids.begin(), ids.end(), id ) != ids.end();
}

// type ids of the top-level items of a spawn
auto top_ids( spawned_items &list ) -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for( detached_ptr<item> &it : list ) {
        ids.push_back( it->typeId().str() );
    }
    return ids;
}

// the first top-level item of the given type, or nullptr
auto find_top( spawned_items &list, const std::string &id ) -> item *
{
    for( detached_ptr<item> &it : list ) {
        if( it->typeId().str() == id ) {
            return &*it;
        }
    }
    return nullptr;
}

auto possible_ids( const std::string &group ) -> std::set<std::string>
{
    std::set<std::string> ids;
    for( const itype *type : item_group::every_possible_item_from( item_group_id( group ) ) ) {
        ids.insert( type->get_id().str() );
    }
    return ids;
}

struct armor_row {
    const char *id;
    int weight_g; // CDF: the weight in cdf_eod_kit.json (DDA's)
    int volume_ml; // CDF: the volume in cdf_eod_kit.json (DDA's)
    int molle;     // attach_molle size, -1 for none
};
} // namespace

TEST_CASE( "eod1_items_exist_with_dda_numbers", "[armor][eod1]" )
{
    clear_all_state();
    // CDF: the numbers are the ones written in data/json/items/armor/cdf_eod_kit.json (DDA's)
    const std::vector<armor_row> rows = {
        { "helmet_eod", 3600, 4000, -1 },
        { "helmet_eod_on", 3600, 4000, -1 },
        { "gloves_eod", 610, 1000, -1 },
        { "trousers_eod", 6400, 7000, 4 },
        { "trousers_eod_light", 3000, 7000, 6 },
        { "face_shield_eod", 229, 250, -1 },
        { "foot_protectors_eod", 1220, 3000, -1 },
        { "EOD_hotstick", 3100, 1250, -1 },
    };
    for( const armor_row &row : rows ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        const itype &type = *itype_id( row.id );
        // the itype, not a spawned item: a spawned helmet may hold its default battery
        CHECK( type.weight == units::from_gram( row.weight_g ) );
        CHECK( type.volume == units::from_milliliter( row.volume_ml ) );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        CHECK( molle_size( *it ) == row.molle );
        if( row.molle > 0 ) {
            CHECK( it->get_use( "detach_molle" ) != nullptr );
        }
    }

    SECTION( "the face shield is an attachment that cannot be worn" ) {
        detached_ptr<item> shield = item::spawn( itype_id( "face_shield_eod" ), calendar::turn );
        CHECK( shield->has_flag( flag_id( "HELMET_FACE_SHIELD" ) ) );
        CHECK( shield->has_flag( flag_id( "CANT_WEAR" ) ) );
        CHECK( shield->covers( bodypart_id( "eyes" ) ) );
        CHECK( shield->covers( bodypart_id( "mouth" ) ) );
    }
    SECTION( "the hot stick is a pike pole with its own name" ) {
        // CDF: no longest_side in the JSON (the overlay file has none for it); the melee numbers
        // are those of pike_pole, which the item copies
        detached_ptr<item> stick = item::spawn( itype_id( "EOD_hotstick" ), calendar::turn );
        detached_ptr<item> pole = item::spawn( itype_id( "pike_pole" ), calendar::turn );
        CHECK( stick->damage_melee( damage_type::DT_BASH ) == 20 );
        CHECK( stick->damage_melee( damage_type::DT_CUT ) == 6 );
        CHECK( stick->has_flag( flag_id( "REACH_ATTACK" ) ) );
        CHECK( stick->tname() != pole->tname() );
    }
    SECTION( "the kit pieces cover what DDA says" ) {
        detached_ptr<item> gloves = item::spawn( itype_id( "gloves_eod" ), calendar::turn );
        CHECK( gloves->covers( bodypart_id( "hand_l" ) ) );
        CHECK( gloves->get_coverage( bodypart_id( "hand_l" ) ) == 40 ); // CDF: JSON coverage 40
        detached_ptr<item> feet = item::spawn( itype_id( "foot_protectors_eod" ), calendar::turn );
        CHECK( feet->covers( bodypart_id( "foot_r" ) ) );
        detached_ptr<item> legs = item::spawn( itype_id( "trousers_eod" ), calendar::turn );
        CHECK( legs->covers( bodypart_id( "leg_l" ) ) );
        CHECK( legs->get_coverage( bodypart_id( "leg_l" ) ) == 100 ); // CDF: JSON coverage 100
    }
}

TEST_CASE( "eod1_helmet_takes_only_medium_batteries", "[armor][eod1]" )
{
    clear_all_state();
    detached_ptr<item> spawned = item::spawn( itype_id( "helmet_eod" ), calendar::turn );
    item &helmet = *spawned;

    SECTION( "the explicit well is there and holds a medium cell by default" ) {
        // CDF: JSON default_magazine medium_battery_cell, flag_restriction BATTERY_MEDIUM
        REQUIRE( helmet.has_pocket_type( pocket_type::MAGAZINE_WELL ) );
        CHECK( helmet.is_reloadable() );
        const std::vector<item_pocket *> wells = helmet.contents.get_pockets( pocket_type::MAGAZINE_WELL );
        REQUIRE( wells.size() == 1 );
        CHECK( wells.front()->get_pocket_data()->get_flag_restrictions().count( flag_id( "BATTERY_MEDIUM" ) ) == 1 );
        // CDF: the default cell comes from the well's default_magazine and, redundantly, from the
        // item-level magazines key (item_factory.cpp:1111-1122); with both gone neither answers
        CHECK( helmet.magazine_default() == itype_id( "medium_battery_cell" ) );
        CHECK( wells.front()->get_pocket_data()->default_magazine == itype_id( "medium_battery_cell" ) );
    }
    SECTION( "only the medium cell fits" ) {
        CHECK( helmet.can_reload_with( item( itype_id( "medium_battery_cell" ) ), true ) );
        CHECK_FALSE( helmet.can_reload_with( item( itype_id( "light_battery_cell" ) ), true ) );
        CHECK_FALSE( helmet.can_reload_with( item( itype_id( "heavy_battery_cell" ) ), true ) );
    }
    SECTION( "the headlamps turn on and off" ) {
        item cell( itype_id( "medium_battery_cell" ) );
        const int charges = cell.ammo_capacity( ammotype( "battery" ) );
        cell.ammo_set( cell.ammo_default(), charges );
        REQUIRE( cell.ammo_remaining() == charges );
        // CDF: an installed cell may already sit in the well from the default magazine; remove it
        // first so the charged cell is the one under test
        helmet.contents.clear_items();
        REQUIRE( helmet.put_in( cell, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( helmet.ammo_remaining() == charges );

        clear_avatar();
        Character &guy = get_avatar();
        const use_function *on = helmet.type->get_use( "transform" );
        REQUIRE( on != nullptr );
        const iuse_transform *on_actor = dynamic_cast<const iuse_transform *>( on->get_actor_ptr() );
        REQUIRE( on_actor != nullptr );
        on_actor->use( get_avatar(), helmet, false, guy.bub_pos() );
        CHECK( helmet.typeId().str() == "helmet_eod_on" );
        CHECK( helmet.ammo_remaining() == charges );

        const use_function *off = helmet.type->get_use( "transform" );
        REQUIRE( off != nullptr );
        const iuse_transform *off_actor = dynamic_cast<const iuse_transform *>( off->get_actor_ptr() );
        REQUIRE( off_actor != nullptr );
        off_actor->use( get_avatar(), helmet, false, guy.bub_pos() );
        CHECK( helmet.typeId().str() == "helmet_eod" );
    }
    SECTION( "a helmet with no cell does not turn on" ) {
        // CDF: need_charges 1 in the JSON
        helmet.contents.clear_items();
        REQUIRE( helmet.ammo_remaining() == 0 );
        clear_avatar();
        const use_function *on = helmet.type->get_use( "transform" );
        REQUIRE( on != nullptr );
        const iuse_transform *on_actor = dynamic_cast<const iuse_transform *>( on->get_actor_ptr() );
        REQUIRE( on_actor != nullptr );
        on_actor->use( get_avatar(), helmet, false, get_avatar().bub_pos() );
        CHECK( helmet.typeId().str() == "helmet_eod" );
    }
}

TEST_CASE( "eod1_helmet_covers_head_mouth_eyes", "[armor][eod1]" )
{
    clear_all_state();
    detached_ptr<item> spawned = item::spawn( itype_id( "helmet_eod" ), calendar::turn );
    const item &helmet = *spawned;

    // CDF: the three portions of cdf_eod_kit.json: head 100, mouth 100 (encumbrance 15, rigid
    // layer only), eyes 100 (encumbrance 5, rigid layer only)
    for( const char *bp : { "head", "mouth", "eyes" } ) {
        CAPTURE( bp );
        CHECK( helmet.covers( bodypart_id( bp ) ) );
        CHECK( helmet.get_coverage( bodypart_id( bp ) ) == 100 );
    }
    CHECK_FALSE( helmet.covers( bodypart_id( "torso" ) ) );
    // rigid_layer_only is set on the mouth and eyes portions and not on the head one. The portion
    // is rigid because its materials (kevlar_rigid, plastic, plastic_pad) are not soft.
    CHECK( helmet.is_bp_rigid_selective( bodypart_id( "mouth" ) ) );
    CHECK( helmet.is_bp_rigid_selective( bodypart_id( "eyes" ) ) );
    CHECK_FALSE( helmet.is_bp_rigid_selective( bodypart_id( "head" ) ) );
}

TEST_CASE( "eod1_groups_yield_the_kit", "[armor][eod1][spawn]" )
{
    clear_all_state();
    rng_set_engine_seed( 20261103 );

    SECTION( "eod_duffle" ) {
        // CDF: DDA gear.json:228; always trousers, an EOD jacket holding a plate, a helmet; the
        // foot protectors at 80 percent
        int protectors = 0;
        constexpr int draws = 400;
        for( int i = 0; i < draws; ++i ) {
            spawned_items list = spawn( "eod_duffle" );
            const std::vector<std::string> ids = top_ids( list );
            CHECK( has_id( ids, "trousers_eod" ) );
            CHECK( has_id( ids, "helmet_eod" ) );
            item *jacket = find_top( list, "jacket_eod" );
            REQUIRE( jacket != nullptr );
            CHECK( has_id( inner_ids( *jacket ), "eod_plate" ) );
            if( has_id( ids, "foot_protectors_eod" ) ) {
                ++protectors;
            }
        }
        // 80 percent of 400 is 320, sd 8: five sd either side
        CHECK( protectors > 280 );
        CHECK( protectors < 360 );
    }
    SECTION( "eod_duffle_light" ) {
        // CDF: DDA gear.json:236; the light jacket holds the whole plate set
        for( int i = 0; i < 100; ++i ) {
            spawned_items list = spawn( "eod_duffle_light" );
            const std::vector<std::string> ids = top_ids( list );
            CHECK( has_id( ids, "trousers_eod_light" ) );
            CHECK( has_id( ids, "face_shield_eod" ) );
            item *jacket = find_top( list, "jacket_eod_light" );
            REQUIRE( jacket != nullptr );
            const std::vector<std::string> inside = inner_ids( *jacket );
            CHECK( has_id( inside, "eod_plate" ) );
            CHECK( has_id( inside, "deltoid_plates" ) );
        }
    }
    SECTION( "gear_eod_light_or_heavy is a duffel bag with one of the two kits, 90 to 10" ) {
        // CDF: DDA gear.json:219; the bag is the group's container-item. Spilled items (the group
        // says on_overflow spill) are counted with the bag's contents.
        int heavy = 0;
        constexpr int draws = 400;
        for( int i = 0; i < draws; ++i ) {
            spawned_items list = spawn( "gear_eod_light_or_heavy" );
            item *bag = find_top( list, "duffelbag" );
            REQUIRE( bag != nullptr );
            std::vector<std::string> ids = top_ids( list );
            const std::vector<std::string> inside = inner_ids( *bag );
            ids.insert( ids.end(), inside.begin(), inside.end() );
            const bool is_heavy = has_id( ids, "trousers_eod" );
            const bool is_light = has_id( ids, "trousers_eod_light" );
            CHECK( is_heavy != is_light );
            if( is_heavy ) {
                ++heavy;
            }
        }
        // 90 percent of 400 is 360, sd 6: five sd either side
        CHECK( heavy > 330 );
        CHECK( heavy < 390 );
    }
    SECTION( "eod_armor is the full kit or the light kit" ) {
        // CDF: DDA Clothing_Gear/military.json:274; kit one at 100, kit two at 20, so 5 in 6 draws
        // give the full kit (400 draws: about 333, sd 7)
        int full = 0;
        int max_charges = 0;
        int powered = 0;
        bool gloves_seen = false;
        constexpr int draws = 400;
        for( int i = 0; i < draws; ++i ) {
            spawned_items list = spawn( "eod_armor" );
            const std::vector<std::string> ids = top_ids( list );
            if( has_id( ids, "helmet_eod" ) ) {
                ++full;
                CHECK( has_id( ids, "jacket_eod" ) );
                CHECK( has_id( ids, "trousers_eod" ) );
                CHECK( has_id( ids, "foot_protectors_eod" ) );
                const item *helmet = find_top( list, "helmet_eod" );
                CHECK( helmet->ammo_remaining() >= 0 );
                CHECK( helmet->ammo_remaining() <= 500 ); // CDF: JSON charges [ 0, 500 ]
                // CDF: the charges are the cell's, not the helmet's own: the real helmet_eod has
                // a magazines key, so a spawn with power has a cell and reports its charge
                CHECK( helmet->charges == 0 );
                if( helmet->ammo_remaining() > 0 ) {
                    const item *cell = helmet->magazine_current();
                    REQUIRE( cell != nullptr );
                    CHECK( cell->typeId().str() == "medium_battery_cell" );
                    CHECK( helmet->ammo_remaining() == cell->ammo_remaining() );
                    ++powered;
                }
                max_charges = std::max( max_charges, static_cast<int>( helmet->ammo_remaining() ) );
                gloves_seen = gloves_seen || has_id( ids, "gloves_eod" );
            } else {
                CHECK( has_id( ids, "jacket_eod_light" ) );
                CHECK( has_id( ids, "face_shield_eod" ) );
                CHECK( has_id( ids, "trousers_eod_light" ) );
                CHECK_FALSE( has_id( ids, "gloves_eod" ) );
            }
        }
        CHECK( full > 296 );
        CHECK( full < 370 );
        CHECK( gloves_seen );
        // the charges are drawn, not fixed at zero
        CHECK( max_charges > 0 );
        // 500 charges: most draws of [ 0, 500 ] are above zero, so cells really are exercised
        CHECK( powered > 200 );
    }
    SECTION( "the supply groups gear_eod needs hold their items" ) {
        CHECK( possible_ids( "new_laptops" ) == std::set<std::string>( { "laptop" } ) );
        // CDF: every_item() lists the spawned items, not a group's container-item, so the spool
        // is checked on a real spawn instead
        CHECK( possible_ids( "cable_roll_part" ) == std::set<std::string>( { "cable" } ) );
        CHECK( possible_ids( "copper_wire_roll_part" ) == std::set<std::string>( { "copper_wire" } ) );
        for( const std::string group : { "cable_roll_part", "copper_wire_roll_part" } ) {
            CAPTURE( group );
            spawned_items list = spawn( group );
            REQUIRE( list.size() == 1 );
            CHECK( list.front()->typeId().str() == "spool_small" );
            CHECK( inner_ids( *list.front() ) ==
                   std::vector<std::string>( { group == "cable_roll_part" ? "cable" : "copper_wire" } ) );
        }
        const std::set<std::string> all = possible_ids( "gear_eod" );
        for( const char *id : { "EOD_hotstick", "laptop", "cable", "copper_wire", "trousers_eod",
                                "trousers_eod_light", "helmet_eod", "face_shield_eod", "eod_plate" } ) {
            CAPTURE( id );
            CHECK( all.count( id ) == 1 );
        }
    }
}

TEST_CASE( "eod1_army_locker_can_hold_eod_armor", "[armor][eod1][spawn]" )
{
    clear_all_state();
    // CDF: group_contains_item does not follow nested groups, so the whole tree is walked
    const std::set<std::string> all = possible_ids( "army_personal_locker" );
    CHECK( all.count( "helmet_eod" ) == 1 );
    CHECK( all.count( "jacket_eod_light" ) == 1 );
    // an entry that was there before is still there
    CHECK( all.count( "pants_army" ) == 1 );
}

TEST_CASE( "eod1_cop_armory_can_hold_eod_gear", "[armor][eod1][spawn]" )
{
    clear_all_state();
    const std::set<std::string> all = possible_ids( "cop_armory" );
    CHECK( all.count( "EOD_hotstick" ) == 1 );
    CHECK( all.count( "helmet_eod" ) == 1 );
    // an entry that was there before is still there
    CHECK( all.count( "survnote" ) == 1 );
}
