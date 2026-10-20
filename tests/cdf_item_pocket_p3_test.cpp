#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "player_helpers.h"
#include "test_data.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// CDF: three DDA fixtures have no MOD pocket here: a plain TOOL without `ammo` gets none, and
// explicit pocket_data suppresses the generated one. P-1 runs on stand-ins that have one
// (data/mods/TEST_DATA/pocket_mods_p3.json).
auto p3_base_id( const itype_id &base ) -> itype_id
{
    if( base == itype_id( "debug_tool_no_pockets" ) ) {
        return itype_id( "p3_tool_modbay" );
    }
    if( base == itype_id( "debug_tool_one_pocket" ) ) {
        return itype_id( "p3_tool_modbay_one_pocket" );
    }
    if( base == itype_id( "debug_gun_with_mag_well" ) ) {
        return itype_id( "p3_gun_with_mag_well" );
    }
    return base;
}

// DDA tests/item_pocket_test.cpp:2975-3006
void compare_pockets( item &it, pocket_mod_test_data &pocket_mod_data, bool mod_inserted )
{
    std::vector<item_pocket *> new_pockets( it.get_contents().get_pockets( []( const item_pocket & ) {
        return true;
    } ) );

    for( std::pair<const pocket_type, std::vector<uint64_t>> &expected :
         pocket_mod_data.expected_pockets ) {
        const pocket_type type = expected.first;
        uint64_t count = expected.second[mod_inserted ? 1 : 0];
        std::vector<item_pocket *> pockets( it.get_contents().get_pockets( [type]( const item_pocket & pock ) {
            return pock.is_type( type );
        } ) );
        CAPTURE( type );
        CHECK( count == pockets.size() );
    }

    bool same_pocket_data = new_pockets.size() == it.type->pockets.size();
    if( same_pocket_data ) {
        for( const item_pocket *pocket : new_pockets ) {
            if( std::find( it.type->pockets.begin(), it.type->pockets.end(),
                           *pocket->get_pocket_data() ) == it.type->pockets.end() ) {
                same_pocket_data = false;
                break;
            }
        }
    }

    CHECK( same_pocket_data ^ mod_inserted );
}

auto count_of( const item &it, pocket_type type ) -> size_t
{
    return it.get_contents().get_pockets( type ).size();
}

auto holds( const item_pocket &pocket, const itype_id &id ) -> bool
{
    const std::vector<item *> &top = pocket.items().as_vector();
    return std::ranges::any_of( top, [&id]( const item * e ) {
        return e->typeId() == id;
    } );
}

const itype_id jack( "test_jack_small" );
const itype_id iron( "test_soldering_iron" );
const itype_id container_mod( "debug_retool_container_no_mag_well" );

// A p3_tool_modbay carrying `mods` container toolmods; each mod's pocket is a 1 L CONTAINER.
auto modbay_with( const itype_id &base, int mods, std::vector<item *> &mod_ptrs ) -> detached_ptr<item>
{
    detached_ptr<item> tool = item::spawn( base );
    for( int i = 0; i < mods; ++i ) {
        detached_ptr<item> mod = item::spawn( container_mod );
        mod_ptrs.push_back( &*mod );
        REQUIRE( tool->put_in( std::move( mod ), pocket_type::MOD ).success() );
    }
    return tool;
}

// CDF: copied from tests/item_contents_migration_test.cpp:23-33
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

// DDA tests/multimag_test.cpp:1267-1277
// CDF: item::spawn and detached_ptr forms
auto make_multimag_consume_gun( int bb_count = 15, int batt = 100 ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "p3_multimag_gun_consume" ) );
    detached_ptr<item> bb_mag = item::spawn( itype_id( "glockmag" ) );
    bb_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, bb_count ), pocket_type::MAGAZINE );
    detached_ptr<item> batt_mag = item::spawn( itype_id( "heavy_battery_cell" ) );
    batt_mag->put_in( item::spawn( itype_id( "battery" ), calendar::turn, batt ),
                      pocket_type::MAGAZINE );
    REQUIRE( gun->put_in( std::move( bb_mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->put_in( std::move( batt_mag ), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}

// CDF: the DDA fixtures test_multimag_gun_integral_ammo and test_multimag_gun_consume have no MOD
// pocket here (explicit pocket_data suppresses it): the p3_ copies in
// data/mods/TEST_DATA/multimag_p3.json add one, everything else is inherited.
const itype_id integral_gun( "p3_multimag_gun_integral_ammo" );
const itype_id capacity_mod( "test_multimag_capacity_gunmod" );

} // namespace

TEST_CASE( "pocket_mods", "[p3][pocket][toolmod][gunmod]" )
{
    REQUIRE( !test_data::pocket_mod_data.empty() );
    for( std::pair<const std::string, pocket_mod_test_data> &pocket_mod_data :
         test_data::pocket_mod_data ) {
        SECTION( pocket_mod_data.first ) {
            detached_ptr<item> base = item::spawn( p3_base_id( pocket_mod_data.second.base_item ) );
            detached_ptr<item> mod = item::spawn( pocket_mod_data.second.mod_item );
            item *mod_ptr = &*mod;
            REQUIRE( base->put_in( std::move( mod ), pocket_type::MOD ).success() );

            SECTION( "after inserting the mod" ) {
                compare_pockets( *base, pocket_mod_data.second, true );
            }

            SECTION( "after removing the mod" ) {
                detached_ptr<item> removed = base->remove_item( *mod_ptr );
                REQUIRE( removed );
                compare_pockets( *base, pocket_mod_data.second, false );
            }
        }
    }
}

TEST_CASE( "removing a pocket mod keeps the items of the pocket that is matched by value",
           "[p3][pocket]" )
{
    for( int attempt = 0; attempt < 2; ++attempt ) {
        CAPTURE( attempt );
        std::vector<item *> mods;
        detached_ptr<item> tool = modbay_with( itype_id( "p3_tool_modbay" ), 2, mods );
        REQUIRE( count_of( *tool, pocket_type::CONTAINER ) == 2 );
        std::vector<item_pocket *> containers = tool->get_contents().get_pockets( pocket_type::CONTAINER );
        containers[0]->insert( item::spawn( jack ) );
        containers[1]->insert( item::spawn( iron ) );

        detached_ptr<item> removed = tool->remove_item( *mods[0] );
        REQUIRE( removed );

        containers = tool->get_contents().get_pockets( pocket_type::CONTAINER );
        REQUIRE( containers.size() == 1 );
        CHECK( holds( *containers[0], jack ) );
        CHECK_FALSE( holds( *containers[0], iron ) );
        // 750 ml + 500 ml do not fit the remaining 1 L pocket: the iron is re-homed, not lost
        const item_pocket *migration = tool->get_contents().migration_pocket();
        REQUIRE( migration != nullptr );
        CHECK( holds( *migration, iron ) );
    }
}

TEST_CASE( "removing a pocket mod re-homes its items and raises no debugmsg", "[p3][pocket]" )
{
    std::vector<item *> mods;
    detached_ptr<item> tool = modbay_with( itype_id( "p3_tool_modbay" ), 1, mods );
    REQUIRE( count_of( *tool, pocket_type::CONTAINER ) == 1 );
    tool->get_contents().get_pockets( pocket_type::CONTAINER )[0]->insert( item::spawn( jack ) );

    detached_ptr<item> removed;
    const std::string msg = capture_debugmsg_during( [&]() {
        removed = tool->remove_item( *mods[0] );
    } );
    CHECK( msg.empty() );
    REQUIRE( removed );
    CHECK( count_of( *tool, pocket_type::CONTAINER ) == 0 );
    const std::vector<item *> top = tool->get_contents().all_items_top();
    CHECK( std::ranges::any_of( top, []( const item * e ) {
        return e->typeId() == jack;
    } ) );
}

TEST_CASE( "an own pocket keeps its items across installing and removing a pocket mod",
           "[p3][pocket]" )
{
    detached_ptr<item> tool = item::spawn( itype_id( "p3_tool_modbay_one_pocket" ) );
    REQUIRE( count_of( *tool, pocket_type::CONTAINER ) == 1 );
    tool->get_contents().get_pockets( pocket_type::CONTAINER )[0]->insert( item::spawn( jack ) );

    detached_ptr<item> mod = item::spawn( container_mod );
    item *mod_ptr = &*mod;
    detached_ptr<item> removed;
    const std::string msg = capture_debugmsg_during( [&]() {
        REQUIRE( tool->put_in( std::move( mod ), pocket_type::MOD ).success() );
        REQUIRE( count_of( *tool, pocket_type::CONTAINER ) == 2 );
        removed = tool->remove_item( *mod_ptr );
    } );
    CHECK( msg.empty() );
    REQUIRE( removed );
    const std::vector<item_pocket *> containers = tool->get_contents().get_pockets( pocket_type::CONTAINER );
    REQUIRE( containers.size() == 1 );
    CHECK( holds( *containers[0], jack ) );
}

TEST_CASE( "a copy of an item with a pocket mod has the same pockets", "[p3][pocket]" )
{
    std::vector<item *> mods;
    detached_ptr<item> tool = modbay_with( itype_id( "p3_tool_modbay_one_pocket" ), 1, mods );
    detached_ptr<item> copy = item::spawn( *tool );
    for( const pocket_type type : { pocket_type::CONTAINER, pocket_type::MAGAZINE,
                                    pocket_type::MAGAZINE_WELL, pocket_type::MOD } ) {
        CAPTURE( type );
        CHECK( count_of( *copy, type ) == count_of( *tool, type ) );
    }
    CHECK( count_of( *copy, pocket_type::CONTAINER ) == 2 );
}

// DDA tests/multimag_test.cpp:1536
TEST_CASE( "capacity_mods_survive_save_load_roundtrip", "[p3][pocket]" )
{
    detached_ptr<item> gun = item::spawn( integral_gun );
    REQUIRE( gun->put_in( item::spawn( capacity_mod ), pocket_type::MOD ).success() );
    const item_pocket *ammo_p = gun->pocket_by_id( "ammo" );
    REQUIRE( ammo_p != nullptr );
    const ammotype at = *ammo_p->ammo_types().begin();
    REQUIRE( ammo_p->ammo_capacity( at ) == 2 );

    // capacity_mult is not serialized: it must re-derive from the saved mod on load
    detached_ptr<item> loaded = load_item( save_item( *gun ) );
    const item_pocket *ammo2 = loaded->pocket_by_id( "ammo" );
    REQUIRE( ammo2 != nullptr );
    CHECK( ammo2->ammo_capacity( at ) == 2 );
}

// DDA tests/multimag_test.cpp:1560
TEST_CASE( "capacity_mod_overflow_grandfathered_on_removal", "[p3][pocket]" )
{
    detached_ptr<item> gun = item::spawn( integral_gun );
    REQUIRE( gun->put_in( item::spawn( capacity_mod ), pocket_type::MOD ).success() );
    item_pocket *ammo = gun->pocket_by_id( "ammo" );
    REQUIRE( ammo != nullptr );
    const ammotype at = *ammo->ammo_types().begin();
    REQUIRE( ammo->ammo_capacity( at ) == 2 );
    REQUIRE( ammo->insert_item( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ) ).success() );
    REQUIRE( ammo->insert_item( item::spawn( itype_id( "9mm" ), calendar::turn, 1 ) ).success() );
    REQUIRE( ammo->remaining_ammo_capacity( at ) == 0 );

    // the boost mod is removed while the pocket is at the boosted fill
    REQUIRE( !gun->gunmods().empty() );
    detached_ptr<item> removed = gun->remove_item( *gun->gunmods().front() );
    REQUIRE( removed );

    // capacity drops but contents are grandfathered and remaining clamps to 0
    const item_pocket *ammo2 = gun->pocket_by_id( "ammo" );
    REQUIRE( ammo2 != nullptr );
    CHECK( ammo2->ammo_capacity( at ) == 1 );
    CHECK( ammo2->remaining_ammo_capacity( at ) == 0 );
    // item-level API: capacity drops and remaining clamps (no -1)
    CHECK( gun->ammo_capacity( at ) == 1 );
    CHECK( gun->remaining_ammo_capacity() == 0 );
    int loaded = 0;
    for( const item *it : ammo2->all_items_top() ) {
        loaded += it->count();
    }
    CHECK( loaded == 2 );
}

// DDA tests/multimag_test.cpp:1597
TEST_CASE( "capacity_mods_on_magazine_well_diagnosed", "[p3][pocket]" )
{
    detached_ptr<item> gun = item::spawn( integral_gun ); // "power" is a MAGAZINE_WELL
    const std::string dmsg = capture_debugmsg_during( [&]() {
        gun->put_in( item::spawn( itype_id( "test_multimag_capacity_well_gunmod" ) ),
                     pocket_type::MOD );
    } );
    CHECK( dmsg.find( "not an integral MAGAZINE" ) != std::string::npos );
}

// DDA tests/multimag_test.cpp:1635
TEST_CASE( "mod_unknown_pocket_id_diagnosed_at_install", "[p3][pocket]" )
{
    detached_ptr<item> gun = make_multimag_consume_gun( 15, 100 );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        gun->put_in( item::spawn( itype_id( "test_multimag_bad_pocket_gunmod" ) ), pocket_type::MOD );
    } );
    CHECK( dmsg.find( "unknown pocket id" ) != std::string::npos );
    CHECK( dmsg.find( "nonexistent" ) != std::string::npos );
}

TEST_CASE( "an unmodded integral gun keeps its capacity and a removed mod restores it",
           "[p3][pocket]" )
{
    detached_ptr<item> gun = item::spawn( integral_gun );
    const item_pocket *ammo = gun->pocket_by_id( "ammo" );
    REQUIRE( ammo != nullptr );
    const ammotype at = *ammo->ammo_types().begin();
    CHECK( ammo->ammo_capacity( at ) == 1 );
    CHECK( ammo->get_capacity_mult() == 1.0f );

    const std::string dmsg = capture_debugmsg_during( [&]() {
        item *mod = nullptr;
        {
            detached_ptr<item> m = item::spawn( capacity_mod );
            mod = &*m;
            REQUIRE( gun->put_in( std::move( m ), pocket_type::MOD ).success() );
        }
        CHECK( gun->pocket_by_id( "ammo" )->ammo_capacity( at ) == 2 );
        detached_ptr<item> removed = gun->remove_item( *mod );
        REQUIRE( removed );
        CHECK( gun->pocket_by_id( "ammo" )->ammo_capacity( at ) == 1 );
        CHECK( gun->pocket_by_id( "ammo" )->get_capacity_mult() == 1.0f );
    } );
    CHECK( dmsg.empty() );
}

TEST_CASE( "a copy of a gun with a capacity mod keeps the boosted capacity", "[p3][pocket]" )
{
    detached_ptr<item> gun = item::spawn( integral_gun );
    REQUIRE( gun->put_in( item::spawn( capacity_mod ), pocket_type::MOD ).success() );
    const ammotype at = *gun->pocket_by_id( "ammo" )->ammo_types().begin();
    REQUIRE( gun->pocket_by_id( "ammo" )->ammo_capacity( at ) == 2 );

    // CDF: item::spawn( *gun ) is the copy form (the same copy constructor, location-owned)
    detached_ptr<item> copy = item::spawn( *gun );
    const item_pocket *ammo = copy->pocket_by_id( "ammo" );
    REQUIRE( ammo != nullptr );
    CHECK( ammo->ammo_capacity( at ) == 2 );
}

// ---- 3-7c: removal guard for pocket mods (ADV-9) with the gunmod cascade dry run (ADV-10) ----
// CDF: CDF-only tests; the fixtures are data/mods/TEST_DATA/pocket_mods_test.json and pocket_mods_p3.json.

namespace
{
const itype_id guard_gun( "debug_gun_no_mag_well" );
const itype_id guard_mod_x( "debug_retool_mag_well" );
const itype_id guard_mod_y( "p3_mod_y" );
const itype_id guard_mag( "stanag30" );

auto guard_gun_with( const std::vector<itype_id> &mods, std::vector<item *> &mod_ptrs ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( guard_gun );
    for( const itype_id &id : mods ) {
        detached_ptr<item> mod = item::spawn( id );
        mod_ptrs.push_back( &*mod );
        REQUIRE( gun->put_in( std::move( mod ), pocket_type::MOD ).success() );
    }
    return gun;
}
} // namespace

TEST_CASE( "a gunmod whose magazine well holds a magazine cannot be removed", "[p3][pocket]" )
{
    clear_avatar();
    std::vector<item *> mods;
    detached_ptr<item> gun = guard_gun_with( { guard_mod_x }, mods );
    REQUIRE( count_of( *gun, pocket_type::MAGAZINE_WELL ) == 1 );
    REQUIRE( gun->put_in( item::spawn( guard_mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->gunmods().size() == 1 );

    SECTION( "R-1: refused while the well holds a magazine" ) {
        CHECK_FALSE( avatar_funcs::gunmod_remove( get_avatar(), *gun, *mods[0] ) );
        CHECK( gun->gunmods().size() == 1 );
        CHECK( count_of( *gun, pocket_type::MAGAZINE_WELL ) == 1 );
        const std::vector<item_pocket *> wells = gun->get_contents().get_pockets( pocket_type::MAGAZINE_WELL );
        REQUIRE( wells.size() == 1 );
        CHECK( holds( *wells[0], guard_mag ) );
    }

    SECTION( "R-2: removed once the well is empty" ) {
        const std::vector<item_pocket *> wells = gun->get_contents().get_pockets( pocket_type::MAGAZINE_WELL );
        REQUIRE( wells.size() == 1 );
        detached_ptr<item> mag = wells[0]->remove_item( wells[0]->items().as_vector().front() );
        REQUIRE( mag );
        CHECK( avatar_funcs::gunmod_remove( get_avatar(), *gun, *mods[0] ) );
        CHECK( gun->gunmods().empty() );
        CHECK( count_of( *gun, pocket_type::MAGAZINE_WELL ) == 0 );
    }
}

TEST_CASE( "mod_pockets_in_use reports a toolmod whose pocket holds an item", "[p3][pocket]" )
{
    std::vector<item *> mods;
    detached_ptr<item> tool = modbay_with( itype_id( "p3_tool_modbay" ), 1, mods );
    REQUIRE( count_of( *tool, pocket_type::CONTAINER ) == 1 );
    // R-3: false when empty, true with the jack inside; nothing is modified by the question
    CHECK_FALSE( tool->get_contents().mod_pockets_in_use( *mods[0] ) );
    tool->get_contents().get_pockets( pocket_type::CONTAINER )[0]->insert( item::spawn( jack ) );
    CHECK( tool->get_contents().mod_pockets_in_use( *mods[0] ) );
    CHECK( tool->get_contents().mod_pockets_in_use( *mods[0] ) );
    CHECK( count_of( *tool, pocket_type::CONTAINER ) == 1 );
    // a mod that adds no pocket is never blocking
    detached_ptr<item> plain = item::spawn( itype_id( "test_jack_small" ) );
    CHECK_FALSE( tool->get_contents().mod_pockets_in_use( *plain ) );
}

TEST_CASE( "a cascade that would drop a filled pocket refuses the whole gunmod removal",
           "[p3][pocket]" )
{
    clear_avatar();
    std::vector<item *> mods;
    // X (debug_retool_mag_well) adds the sights slot that Y (p3_mod_y) sits in
    detached_ptr<item> gun = guard_gun_with( { guard_mod_x, guard_mod_y }, mods );
    REQUIRE( gun->gunmods().size() == 2 );
    REQUIRE( count_of( *gun, pocket_type::CONTAINER ) == 1 );

    SECTION( "R-4: Y's pocket holds an item, X and Y both stay" ) {
        gun->get_contents().get_pockets( pocket_type::CONTAINER )[0]->insert( item::spawn( jack ) );
        const size_t carried = get_avatar().all_items_loc().size();
        CHECK_FALSE( avatar_funcs::gunmod_remove( get_avatar(), *gun, *mods[0] ) );
        CHECK( gun->gunmods().size() == 2 );
        const std::vector<item_pocket *> containers = gun->get_contents().get_pockets( pocket_type::CONTAINER );
        REQUIRE( containers.size() == 1 );
        CHECK( holds( *containers[0], jack ) );
        CHECK( get_avatar().all_items_loc().size() == carried );
    }

    SECTION( "R-5: Y's pocket is empty, Y goes with X" ) {
        CHECK( avatar_funcs::gunmod_remove( get_avatar(), *gun, *mods[0] ) );
        CHECK( gun->gunmods().empty() );
    }
}
