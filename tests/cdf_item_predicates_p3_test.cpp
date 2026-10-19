#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "itype.h"

TEST_CASE( "item predicates identify magazines", "[p3][ammo]" ) {
    // CDF: stanag30 is a magazine (data/json/items/magazine/223.json), m4a1 a gun
    // and backpack a container (no MAGAZINE pocket).
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> backpack = item::spawn( itype_id( "backpack" ) );

    CHECK( mag->is_magazine() );
    CHECK_FALSE( gun->is_magazine() );
    CHECK_FALSE( backpack->is_magazine() );
}

TEST_CASE( "item predicates identify ammo belts", "[p3][ammo]" ) {
    // CDF: belt40mm carries MAG_BELT (data/json/items/magazine/40mm.json), stanag30 does not.
    detached_ptr<item> belt = item::spawn( itype_id( "belt40mm" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );

    CHECK( belt->is_ammo_belt() );
    CHECK_FALSE( mag->is_ammo_belt() );
}

TEST_CASE( "item predicates identify ammo", "[p3][ammo]" ) {
    // CDF: 556 is an ammo item (data/json/items/ammo/223.json), m4a1 is a gun.
    detached_ptr<item> round = item::spawn( itype_id( "556" ), calendar::turn, 1 );
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );

    CHECK( round->is_ammo() );
    CHECK_FALSE( gun->is_ammo() );
}

TEST_CASE( "item predicates identify toolmods", "[p3][ammo]" ) {
    // CDF: battery_ups is a TOOLMOD (data/json/items/toolmod.json),
    // arrowrest a GUNMOD (data/json/items/gunmod/accessories.json).
    detached_ptr<item> toolmod = item::spawn( itype_id( "battery_ups" ) );
    detached_ptr<item> gunmod = item::spawn( itype_id( "arrowrest" ) );

    CHECK( toolmod->is_toolmod() );
    CHECK_FALSE( gunmod->is_toolmod() );
}

TEST_CASE( "item predicates identify vehicle batteries", "[p3][ammo]" ) {
    // CDF: type->battery is set only for "type": "BATTERY" items; the only one in CDF
    // data is battery_test (data/json/items/battery.json). light_battery_cell and the
    // other cells are MAGAZINEs, so is_battery() is false for them (auto_pickup.cpp:291).
    detached_ptr<item> battery = item::spawn( itype_id( "battery_test" ) );
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );

    CHECK( battery->is_vehicle_battery() );
    CHECK( battery->is_vehicle_battery() == battery->is_battery() );
    CHECK_FALSE( gun->is_vehicle_battery() );
}

TEST_CASE( "item predicates identify medical tools", "[p3][ammo]" ) {
    // CDF: bandages has a heal use_action (data/json/items/comestibles/med.json).
    detached_ptr<item> bandages = item::spawn( itype_id( "bandages" ) );
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );

    CHECK( bandages->is_medical_tool() );
    CHECK_FALSE( gun->is_medical_tool() );
}
