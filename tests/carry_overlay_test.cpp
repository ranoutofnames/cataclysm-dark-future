// CDF CAP1c: overlay pockets and strap pockets follow the CARRY1 rule: holster pockets are not
// carried storage. carried_capacity() is the carry-pocket sum; volume_capacity() stays the DDA sum
// of every non-forbidden CONTAINER pocket (DDA: item_pocket.cpp:2548).
#include <climits>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "json.h"
#include "character.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace
{

avatar &fresh_avatar()
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
    u.unset_mutation( trait_id( "PACKMULE" ) );
    u.unset_mutation( trait_id( "HOARDER" ) );
    u.clear_morale();
    return u;
}

void wear( avatar &u, const char *id )
{
    detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
    // CDF: wear_item returns the item back on failure and an empty pointer on success
    REQUIRE( !u.wear_item( std::move( it ), false ) );
}

// CDF: counts of CONTAINER pockets of a freshly spawned item
struct pocket_counts {
    int carry = 0;
    int holster = 0;
    int holster_carry = 0;
    int total = 0;
};

pocket_counts count_pockets( const char *id )
{
    detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
    pocket_counts c;
    for( const item_pocket *p : static_cast<const item &>( *it ).get_container_pockets() ) {
        c.total++;
        if( p->is_carry() ) {
            c.carry++;
        }
        if( p->is_holster() ) {
            c.holster++;
            if( p->is_carry() ) {
                c.holster_carry++;
            }
        }
    }
    return c;
}

struct overlay_row {
    const char *id;
    int all_ml;   // every CONTAINER pocket (DDA volume_capacity)
    int carry_ml; // all minus the holster pockets
};

// CDF: all_ml from the item overlay data/json/items/cdf_overlay_garment_pockets.json; carry_ml is
// all_ml minus the pockets with "holster": true (re-derived in CAP1c step 1)
const std::vector<overlay_row> &rows()
{
    static const std::vector<overlay_row> r = {
        { "backpack", 35510, 25000 },
        { "backpack_leather", 30000, 25000 },
        { "backpack_tactical_large", 81000, 66000 },
        { "bigback", 89000, 81000 },
        { "boxpack", 25000, 15000 },
        { "daypack", 34510, 24000 },
        { "duffelbag", 64000, 60000 },
        { "makeshift_knapsack", 10000, 5000 },
        { "rucksack", 74250, 54250 },
        { "survivor_duffel_bag", 78000, 66000 },
        { "survivor_rucksack", 63000, 40000 },
        { "travelpack", 39510, 31000 },
    };
    return r;
}

} // namespace

TEST_CASE( "cap1c_backpack_carries_25_l", "[cap1c][pocket]" )
{
    avatar &u = fresh_avatar();
    const units::volume cap0 = u.carried_capacity();
    const units::volume vcap0 = u.volume_capacity();
    wear( u, "backpack" );
    // CDF: overlay backpack: 25 L carry + 510 ml bottle + 5 L back + 5 L carabiner (holsters)
    CHECK( u.carried_capacity() == cap0 + 25000_ml );
    // DDA: item_pocket.cpp:2548 counts holster pockets too
    CHECK( u.volume_capacity() == vcap0 + 35510_ml );

    const pocket_counts c = count_pockets( "backpack" );
    CHECK( c.total == 4 );
    CHECK( c.carry == 1 );
    CHECK( c.holster == 3 );
    CHECK( c.holster_carry == 0 );
}

TEST_CASE( "cap1c_overlay_items_table", "[cap1c][pocket]" )
{
    for( const overlay_row &row : rows() ) {
        DYNAMIC_SECTION( row.id ) {
            avatar &u = fresh_avatar();
            const units::volume cap0 = u.carried_capacity();
            const units::volume vcap0 = u.volume_capacity();
            wear( u, row.id );
            CHECK( u.carried_capacity() == cap0 + units::from_milliliter( row.carry_ml ) );
            CHECK( u.volume_capacity() == vcap0 + units::from_milliliter( row.all_ml ) );
            const pocket_counts c = count_pockets( row.id );
            CHECK( c.holster_carry == 0 );
            CHECK( c.carry >= 1 );
        }
    }
}

TEST_CASE( "cap1c_strap_pocket_is_not_carried", "[cap1c][pocket]" )
{
    avatar &u = fresh_avatar();
    const units::volume cap0 = u.carried_capacity();
    const units::volume vcap0 = u.volume_capacity();
    wear( u, "armor_nomad" );
    // CDF: armor_nomad has "storage": "20 L": a 20 L main pocket plus the 10 L strap (holster)
    CHECK( u.carried_capacity() == cap0 + 20000_ml );
    CHECK( u.volume_capacity() == vcap0 + 30000_ml );

    const pocket_counts c = count_pockets( "armor_nomad" );
    CHECK( c.total == 2 );
    CHECK( c.carry == 1 );
    CHECK( c.holster == 1 );
    CHECK( c.holster_carry == 0 );
}

// CDF: O4. The CAP1c default sits in Item_factory::load_item_overlay, whose result
// (overlay_pockets) is private and only consumed at finalize, so a one-off overlay cannot be
// observed from a test. What a test can pin is the loader half the rule depends on: pocket_data
// reports whether "carry" was given, so an explicit value is kept and an absent one is defaulted.
TEST_CASE( "cap1c_explicit_carry_in_overlay_is_kept", "[cap1c][pocket]" )
{
    const auto load = []( const std::string & text ) {
        std::istringstream is( text );
        JsonIn jsin( is );
        JsonObject jo = jsin.get_object();
        pocket_data pd;
        pd.load( jo );
        return pd;
    };
    const pocket_data given = load( R"({"pocket_type":"CONTAINER","holster":true,"carry":true})" );
    CHECK( given.carry_given );
    CHECK( given.carry );
    CHECK( given.holster );
    const pocket_data given_false = load( R"({"pocket_type":"CONTAINER","carry":false})" );
    CHECK( given_false.carry_given );
    CHECK_FALSE( given_false.carry );
    const pocket_data absent = load( R"({"pocket_type":"CONTAINER","holster":true})" );
    CHECK_FALSE( absent.carry_given );
}

// CDF: carry decides capacity only, never visibility (orchestrator 21:10). A gun the wearer puts
// into the backpack lands in a holster pocket: it must still be found by the position lookup
// and by the item visitor, while the carried capacity stays the 25 L of the main bag.
TEST_CASE( "cap1c_holstered_item_is_reachable", "[cap1c][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "backpack" );
    const units::volume cap = u.carried_capacity();
    const units::volume vcap = u.volume_capacity();

    detached_ptr<item> det = item::spawn( itype_id( "glock_19" ), calendar::turn );
    item &gun = *det;
    u.i_add( std::move( det ) );

    bool in_holster = false;
    for( item * const &worn_it : u.worn ) {
        for( const item_pocket *p : static_cast<const item &>( *worn_it ).get_container_pockets() ) {
            for( const item * const &it : p->items() ) {
                if( it == &gun ) {
                    in_holster = p->is_holster() && !p->is_carry();
                }
            }
        }
    }
    REQUIRE( in_holster );

    const int pos = u.inv_position_by_type( itype_id( "glock_19" ) );
    REQUIRE( pos != INT_MIN );
    CHECK( &u.inv_find_item( pos ) == &gun );
    int seen = 0;
    u.visit_items( [&gun, &seen]( item * it, item * ) {
        if( it == &gun ) {
            seen++;
        }
        return VisitResponse::NEXT;
    } );
    CHECK( seen == 1 );

    // the negative cases: a type that is not carried is not found, and the capacity did not move
    CHECK( u.inv_position_by_type( itype_id( "calico" ) ) == INT_MIN );
    CHECK( u.carried_capacity() == cap );
    CHECK( u.volume_capacity() == vcap );
}
