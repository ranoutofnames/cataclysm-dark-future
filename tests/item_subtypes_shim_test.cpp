// CDF: P3 COMPAT3 (PL-49 item A3, AUTO-239 q2, AUTO-240). DDA item objects are written as
// "type": "ITEM" with a "subtypes" array. Item_factory::load_item dispatches each subtype to the
// same slot loaders as its legacy per-type object (load_tool, load_gun, ...); the slots union on
// one itype. Unknown subtypes are an error, unknown keys still report (strict, AUTO-240).
// Fixtures: tests/data/item_compat/. The DDA armor array is not read here (A4).
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "catch/catch.hpp"
#include <memory>

#include "debug.h"
#include "init.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "type_id.h"
#include "itype_slots/islot_tool.h"
#include "itype_slots/islot_comestible.h"
#include "itype_slots/islot_book.h"
#include "itype_slots/islot_gun.h"

namespace {

const std::string COMPAT_DIR = "tests/data/item_compat/";
// "bn" is the strict core source (is_strict_enabled), the same src the core data uses
const std::string COMPAT_SRC = "bn";


// find_template is private; the public find() is the lookup the existing item factory test uses
const itype *find_item( Item_factory &factory, const std::string &id )
{
    const std::vector<const itype *> hits = factory.find( [&id]( const itype & t ) {
        return t.get_id() == itype_id( id );
    } );
    return hits.empty() ? nullptr : hits.front();
}

// Loads one fixture object through the loader its "type" names (the same entry points init.cpp
// registers). The JsonObject is destroyed at the end of this call, so unknown-key reports land
// inside a capture_debugmsg_during() around this call.
void load_fixture( Item_factory &factory, const std::string &file )
{
    std::ifstream in( COMPAT_DIR + file, std::ios::binary );
    REQUIRE( in.good() );
    JsonIn jsin( in );
    JsonObject jo = jsin.get_object();
    const std::string type = jo.get_string( "type" );
    if( type == "ITEM" ) {
        factory.load_item( jo, COMPAT_SRC );
    } else if( type == "TOOL" ) {
        factory.load_tool( jo, COMPAT_SRC );
    } else if( type == "COMESTIBLE" ) {
        factory.load_comestible( jo, COMPAT_SRC );
    } else if( type == "AMMO" ) {
        factory.load_ammo( jo, COMPAT_SRC );
    } else if( type == "BOOK" ) {
        factory.load_book( jo, COMPAT_SRC );
    } else if( type == "GUN" ) {
        factory.load_gun( jo, COMPAT_SRC );
    } else {
        FAIL( "fixture " << file << " has an unexpected type " << type );
    }
}

} // namespace

TEST_CASE("ITEM subtype TOOL loads the same slots as the legacy TOOL object", "[item][compat][p3]")
{
    Item_factory factory;
    load_fixture( factory, "legacy_tool.json" );
    load_fixture( factory, "item_tool.json" );
    factory.finalize();

    const itype *legacy = find_item( factory, "compat_legacy_tool" );
    const itype *item = find_item( factory, "compat_item_tool" );
    REQUIRE( legacy != nullptr );
    REQUIRE( item != nullptr );
    REQUIRE( legacy->tool );
    REQUIRE( item->tool );

    // pinned fixture values, then the slot values compared field by field
    CHECK( item->tool->max_charges == 100 );
    CHECK( item->tool->def_charges == 40 );
    CHECK( item->tool->charges_per_use == 2 );
    CHECK( item->tool->power_draw == 5 );
    CHECK( item->tool->ammo_id == legacy->tool->ammo_id );
    CHECK( item->tool->max_charges == legacy->tool->max_charges );
    CHECK( item->tool->def_charges == legacy->tool->def_charges );
    CHECK( item->tool->charges_per_use == legacy->tool->charges_per_use );
    CHECK( item->tool->power_draw == legacy->tool->power_draw );
    // only the TOOL slot: no other slot leaks in from the ITEM shim
    CHECK_FALSE( item->armor );
    CHECK_FALSE( item->gun );
}

TEST_CASE("ITEM subtypes COMESTIBLE, AMMO, BOOK and GUN each load their slot", "[item][compat][p3]")
{
    Item_factory factory;
    load_fixture( factory, "item_comestible.json" );
    load_fixture( factory, "item_ammo.json" );
    load_fixture( factory, "item_book.json" );
    load_fixture( factory, "item_gun.json" );
    factory.finalize();

    const itype *food = find_item( factory, "compat_item_food" );
    REQUIRE( food != nullptr );
    REQUIRE( food->comestible );
    CHECK( food->comestible->quench == 5 );
    CHECK( food->stack_size == 5 );

    const itype *ammo = find_item( factory, "compat_item_ammo" );
    REQUIRE( ammo != nullptr );
    REQUIRE( ammo->ammo );
    CHECK( ammo->stack_size == 20 );

    const itype *book = find_item( factory, "compat_item_book" );
    REQUIRE( book != nullptr );
    REQUIRE( book->book );
    CHECK( book->book->level == 3 );
    CHECK( book->book->req == 1 );

    const itype *gun = find_item( factory, "compat_item_gun" );
    REQUIRE( gun != nullptr );
    REQUIRE( gun->gun );
    CHECK( gun->gun->range == 10 );
}

TEST_CASE("ITEM with TOOL and ARMOR subtypes yields both slots on one itype", "[item][compat][p3]")
{
    Item_factory factory;
    load_fixture( factory, "item_tool_armor.json" );
    factory.finalize();

    const itype *both = find_item( factory, "compat_item_tool_armor" );
    REQUIRE( both != nullptr );
    REQUIRE( both->tool );
    REQUIRE( both->armor );
    CHECK( both->tool->max_charges == 50 );
    CHECK( both->tool->def_charges == 50 );
}

TEST_CASE("ITEM with an unknown subtype is an error that names the subtype", "[item][compat][p3]")
{
    Item_factory factory;
    std::string what;
    try {
        load_fixture( factory, "item_bad_subtype.json" );
    } catch( const JsonError &e ) {
        what = e.what();
    }
    CHECK_FALSE( what.empty() );
    CHECK( what.find( "NOT_A_TYPE" ) != std::string::npos );
}

TEST_CASE("legacy per-type item objects still load unchanged", "[item][compat][p3]")
{
    Item_factory factory;
    load_fixture( factory, "legacy_tool.json" );
    factory.finalize();

    const itype *legacy = find_item( factory, "compat_legacy_tool" );
    REQUIRE( legacy != nullptr );
    REQUIRE( legacy->tool );
    CHECK( legacy->tool->max_charges == 100 );
    CHECK( legacy->tool->def_charges == 40 );
    CHECK( legacy->tool->charges_per_use == 2 );
    CHECK_FALSE( legacy->armor );
}

TEST_CASE("ITEM with an unknown key still reports it (strict, AUTO-240)", "[item][compat][p3]")
{
    Item_factory factory;
    const std::string captured = capture_debugmsg_during( [&]() {
        load_fixture( factory, "item_unknown_key.json" );
    } );
    CHECK( captured.find( "not_a_cdf_key" ) != std::string::npos );

    // control: the same ITEM without the unknown key reports nothing. Without this, a shim that
    // leaves every key unread would also pass the check above, since the unread keys are reported too.
    Item_factory clean_factory;
    const std::string clean = capture_debugmsg_during( [&]() {
        load_fixture( clean_factory, "item_tool.json" );
    } );
    CHECK( clean.find( "not_a_cdf_key" ) == std::string::npos );
    CHECK( clean.empty() );
}

namespace {

// CDF: the type table dispatches "ITEM" to the global item_controller, which is frozen after game
// data load, so these cases swap in a fresh Item_factory for their scope and restore the original.
struct scoped_item_controller {
    std::unique_ptr<Item_factory> saved;

    scoped_item_controller() : saved( std::move( item_controller ) ) {
        item_controller = std::make_unique<Item_factory>();
    }
    ~scoped_item_controller() {
        item_controller = std::move( saved );
    }
};

// Feeds one JSON object through the real type table (DynamicDataLoader::load_object), as a mod file would.
void load_inline( const std::string &json )
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    JsonObject jo = jsin.get_object();
    DynamicDataLoader::get_instance().load_object( jo, COMPAT_SRC );
}

} // namespace

TEST_CASE("ITEM TOOL through the type table loads the same slots as legacy TOOL", "[item][compat][p3]")
{
    scoped_item_controller scope;
    load_inline( R"({"type": "TOOL", "id": "compat3b_legacy_tool", "name": "compat3b legacy tool",
        "max_charges": 100, "initial_charges": 40, "charges_per_use": 2, "power_draw": 5,
        "ammo": "battery"})" );
    load_inline( R"({"type": "ITEM", "subtypes": [ "TOOL" ], "id": "compat3b_tool",
        "name": "compat3b tool", "max_charges": 100, "initial_charges": 40, "charges_per_use": 2,
        "power_draw": 5, "ammo": "battery"})" );
    item_controller->finalize();

    const itype *legacy = find_item( *item_controller, "compat3b_legacy_tool" );
    const itype *item = find_item( *item_controller, "compat3b_tool" );
    REQUIRE( legacy != nullptr );
    REQUIRE( item != nullptr );
    REQUIRE( legacy->tool );
    REQUIRE( item->tool );

    CHECK( item->tool->max_charges == 100 );
    CHECK( item->tool->def_charges == 40 );
    CHECK( item->tool->charges_per_use == 2 );
    CHECK( item->tool->power_draw == 5 );
    CHECK( item->tool->ammo_id == legacy->tool->ammo_id );
    CHECK( item->tool->max_charges == legacy->tool->max_charges );
    CHECK( item->tool->def_charges == legacy->tool->def_charges );
    CHECK( item->tool->charges_per_use == legacy->tool->charges_per_use );
    CHECK( item->tool->power_draw == legacy->tool->power_draw );
    CHECK_FALSE( item->armor );
    CHECK_FALSE( item->gun );
}

TEST_CASE("ITEM COMESTIBLE through the type table loads the same slot as legacy COMESTIBLE", "[item][compat][p3]")
{
    scoped_item_controller scope;
    load_inline( R"({"type": "COMESTIBLE", "id": "compat3b_legacy_food", "name": "compat3b legacy food",
        "comestible_type": "FOOD", "quench": 5, "stack_size": 5})" );
    load_inline( R"({"type": "ITEM", "subtypes": [ "COMESTIBLE" ], "id": "compat3b_food",
        "name": "compat3b food", "comestible_type": "FOOD", "quench": 5, "stack_size": 5})" );
    item_controller->finalize();

    const itype *legacy = find_item( *item_controller, "compat3b_legacy_food" );
    const itype *item = find_item( *item_controller, "compat3b_food" );
    REQUIRE( legacy != nullptr );
    REQUIRE( item != nullptr );
    REQUIRE( legacy->comestible );
    REQUIRE( item->comestible );

    CHECK( item->comestible->quench == 5 );
    CHECK( item->comestible->quench == legacy->comestible->quench );
    CHECK( item->comestible->comesttype == legacy->comestible->comesttype );
    CHECK( item->stack_size == legacy->stack_size );
    CHECK_FALSE( item->tool );
}
