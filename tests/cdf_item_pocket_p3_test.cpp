#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "test_data.h"

#include <algorithm>
#include <map>
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
