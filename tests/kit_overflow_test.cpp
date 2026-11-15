// PL-62 pr2: the two defects the kit and item-group sweeps found.
//
// (1) The pocket router stored a child item that no pocket accepts in the MIGRATION pocket
//     (sweep K1/G2: "cu_pipe is in the MIGRATION pocket" on an mbag). DDA has no router: a live
//     insert asks find_pocket_for and is refused (DDA item_contents.cpp:905-925), and only a load
//     or a migration keeps an item there (DDA combine(), item_contents.cpp:813; apply_migration(),
//     item_factory.cpp:4758-4769).
// (2) newcharacter::add_profession_items ignored the item wield() hands back, so an AUTO_WIELD
//     kit item that cannot be wielded was destroyed, and it had neither DDA's second attempt nor
//     DDA's "wield the single item that is still left" step (sweep K2: the kit count changes
//     between the spawn and the character). DDA newcharacter.cpp:632-720.
//
// Everything here is headless: no UI, no mapgen, no manual steps.

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "newcharacter.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "profession.h"
#include "ret_val.h"
#include "scenario.h"
#include "state_helpers.h"
#include "type_id.h"

#include <string>
#include <vector>

namespace
{

int total_count( const item &it )
{
    int n = it.count();
    for( const item *child : it.contents.all_items_top() ) {
        n += total_count( *child );
    }
    return n;
}

// What the kit audit (K2) counts: the worn tree, the wielded item and the overflow list.
int carried_count( const avatar &ch, const std::vector<detached_ptr<item>> &overflow )
{
    int after = 0;
    for( const item *it : ch.top_items_loc() ) {
        after += total_count( *it );
    }
    if( !ch.primary_weapon().is_null() ) {
        after += total_count( ch.primary_weapon() );
    }
    for( const detached_ptr<item> &it : overflow ) {
        if( it ) {
            after += total_count( *it );
        }
    }
    return after;
}

bool migration_pocket_holds( const item &holder )
{
    const item_pocket *mig = holder.contents.migration_pocket();
    return mig != nullptr && !mig->empty();
}

const item *worn_item_of_type( const avatar &ch, const itype_id &id )
{
    for( const item *it : ch.top_items_loc() ) {
        if( it->typeId() == id ) {
            return it;
        }
    }
    return nullptr;
}

const item *contained_item_of_type( const item &holder, const itype_id &id )
{
    for( const item *it : holder.contents.all_items_top() ) {
        if( it->typeId() == id ) {
            return it;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE( "a spawned item that no pocket accepts is refused", "[kits][overflow][pocket]" )
{
    clear_all_state();

    SECTION( "the router finds no pocket for it and offers no MIGRATION pocket" ) {
        detached_ptr<item> mbag = item::spawn( itype_id( "mbag" ) );
        detached_ptr<item> pipe = item::spawn( itype_id( "cu_pipe" ) );
        // 60 cm of copper tubing; the mbag's only pocket takes 50 cm
        REQUIRE( mbag->contents.fitting_pocket( *pipe ) == nullptr );
        CHECK( mbag->contents.route( *pipe ) == nullptr );
    }

    SECTION( "a run of copies (what a spawn group inserts) is refused, not parked in MIGRATION" ) {
        detached_ptr<item> mbag = item::spawn( itype_id( "mbag" ) );
        detached_ptr<item> pipe = item::spawn( itype_id( "cu_pipe" ) );
        const std::string dmsg = capture_debugmsg_during( [&]() {
            mbag->put_in_copies( *pipe, 2 );
        } );
        CHECK( mbag->contents.num_item_stacks() == 0 );
        CHECK_FALSE( migration_pocket_holds( *mbag ) );
        // refusing is not silent: the spawn reports it, as DDA's checked put_in does
        CHECK_FALSE( dmsg.empty() );
    }

    SECTION( "a run that the pockets cannot take leaves what is already there alone" ) {
        detached_ptr<item> mbag = item::spawn( itype_id( "mbag" ) );
        detached_ptr<item> pipe = item::spawn( itype_id( "cu_pipe" ) );
        REQUIRE( mbag->try_put_in( item::spawn( itype_id( "rock" ) ) ).success() );
        REQUIRE( mbag->contents.num_item_stacks() == 1 );
        capture_debugmsg_during( [&]() {
            mbag->put_in_copies( *pipe, 2 );
        } );
        CHECK( mbag->contents.num_item_stacks() == 1 );
        CHECK_FALSE( migration_pocket_holds( *mbag ) );
    }

    SECTION( "the checked player insert refuses it and leaves it in hand" ) {
        detached_ptr<item> mbag = item::spawn( itype_id( "mbag" ) );
        detached_ptr<item> pipe = item::spawn( itype_id( "cu_pipe" ) );
        CHECK_FALSE( mbag->try_put_in( std::move( pipe ) ).success() );
        CHECK( static_cast<bool>( pipe ) );
        CHECK( mbag->contents.num_item_stacks() == 0 );
        CHECK( mbag->contents.migration_pocket() == nullptr );
    }

    SECTION( "a migration can still put what it has to place into MIGRATION" ) {
        detached_ptr<item> mbag = item::spawn( itype_id( "mbag" ) );
        // DDA apply_migration (item_factory.cpp:4766-4768): the MIGRATION pocket is the home of
        // a migrated item that fits no real pocket.
        CHECK( mbag->put_in( item::spawn( itype_id( "cu_pipe" ) ), pocket_type::MIGRATION,
                             true ).success() );
        CHECK( migration_pocket_holds( *mbag ) );
    }
}

TEST_CASE( "a starting kit keeps every item it spawns", "[kits][overflow][new_character]" )
{
    clear_all_state();
    avatar &ch = get_avatar();
    // a real inventory (no DEBUG_STORAGE) and no clothes: only a carried container holds a kit
    clear_character( ch, false );
    ch.worn.clear();
    ch.set_body();
    ch.recalc_hp();
    ch.prof = profession::generic();
    g->scen = scenario::generic();

    SECTION( "an AUTO_WIELD item that cannot be wielded is carried, not destroyed" ) {
        // DDA newcharacter.cpp:659-668 wields and keeps what wield() hands back. CDF's old code
        // ignored the returned item, so the kit lost it (a liquid cannot be wielded).
        detached_ptr<item> water = item::spawn( itype_id( "water_clean" ), calendar::turn, 2 );
        water->set_flag( flag_id( "auto_wield" ) );
        std::vector<detached_ptr<item>> kit;
        kit.push_back( std::move( water ) );
        std::vector<detached_ptr<item>> overflow;
        newcharacter::add_profession_items( ch, std::move( kit ), overflow );
        CHECK( carried_count( ch, overflow ) == 2 );
    }

    SECTION( "two items that fit nowhere are both kept" ) {
        std::vector<detached_ptr<item>> kit;
        kit.push_back( item::spawn( itype_id( "e_handcuffs" ) ) );
        kit.push_back( item::spawn( itype_id( "e_handcuffs" ) ) );
        std::vector<detached_ptr<item>> overflow;
        newcharacter::add_profession_items( ch, std::move( kit ), overflow );
        CHECK( carried_count( ch, overflow ) == 2 );
    }

    SECTION( "the one item that is still left is wielded" ) {
        std::vector<detached_ptr<item>> kit;
        kit.push_back( item::spawn( itype_id( "rock" ) ) );
        std::vector<detached_ptr<item>> overflow;
        newcharacter::add_profession_items( ch, std::move( kit ), overflow );
        CHECK( overflow.empty() );
        REQUIRE_FALSE( ch.primary_weapon().is_null() );
        CHECK( ch.primary_weapon().typeId() == itype_id( "rock" ) );
    }

    SECTION( "a kit item is stored in a container that comes later in the list" ) {
        // The worn mbag takes 18 kg and refuses the 54 kg anvil; the pressure cooker (rigid,
        // 6 L, 100 kg) is carried later in the same kit and takes it on the second attempt.
        detached_ptr<item> mbag = item::spawn( itype_id( "mbag" ) );
        REQUIRE( ch.wear_item( std::move( mbag ), false ) == nullptr );

        std::vector<detached_ptr<item>> kit;
        kit.push_back( item::spawn( itype_id( "anvil" ) ) );
        kit.push_back( item::spawn( itype_id( "pressure_cooker" ) ) );
        std::vector<detached_ptr<item>> overflow;
        newcharacter::add_profession_items( ch, std::move( kit ), overflow );

        CHECK( overflow.empty() );
        CHECK( ch.primary_weapon().is_null() );
        const item *bag = worn_item_of_type( ch, itype_id( "mbag" ) );
        REQUIRE( bag != nullptr );
        const item *pot = contained_item_of_type( *bag, itype_id( "pressure_cooker" ) );
        REQUIRE( pot != nullptr );
        CHECK( contained_item_of_type( *pot, itype_id( "anvil" ) ) != nullptr );
    }
}
