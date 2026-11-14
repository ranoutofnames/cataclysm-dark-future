#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "inventory_examiner.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "string_formatter.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "units_utility.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// CDF: P3-UI3. The contents examiner (DDA inventory_ui.cpp:5188-5300), the insert selector header
// (DDA 4690-4700, 4941-4991), the container selector header (DDA 3987-4016) and
// eventually_contains. Pure state: no window is drawn and no key is read. Expected numbers come from
// the item data (volumes, weights, capacities of the items), not from the code under test.
// ---------------------------------------------------------------------------

namespace
{
// A preset that names the container to insert into, as game_inventory.cpp's insert preset does.
class holster_test_preset : public inventory_selector_preset
{
    public:
        explicit holster_test_preset( const item &holster ) : holster( holster ) {}

        const item *get_holster() const override {
            return &holster;
        }

        std::string get_denial( const item *it ) const override {
            detached_ptr<item> copy = item::spawn( *it );
            copy->charges = 1;
            const ret_val<void> ret = holster.can_contain( *copy, false, false, true, false,
                                      it->parent_item() );
            if( !ret.success() ) {
                return !ret.str().empty() ? ret.str() : "item can't be stored there";
            }
            return {};
        }

    private:
        const item &holster;
};

class insert_probe : public inventory_insert_selector
{
    public:
        using inventory_insert_selector::inventory_insert_selector;
        using inventory_insert_selector::get_raw_stats;
        using inventory_insert_selector::set_chosen_count;
};

class container_probe : public container_inventory_selector
{
    public:
        using container_inventory_selector::container_inventory_selector;
        using container_inventory_selector::get_raw_stats;
};

// All cells of all header lines, one string per line.
auto header_lines( const inventory_selector::header_stats &stats ) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    for( const inventory_selector::header_stats_line &line : stats ) {
        std::string joined;
        for( const std::string &cell : line ) {
            joined += cell;
        }
        lines.push_back( joined );
    }
    return lines;
}

auto has_text( const std::string &haystack, const std::string &needle ) -> bool
{
    return haystack.find( needle ) != std::string::npos;
}

auto row_of( const inventory_selector &sel, const item *it ) -> inventory_entry *
{
    for( const inventory_column *col : { &sel.own_gear_column, &sel.own_inv_column, &sel.map_column } ) {
        for( const inventory_entry &e : col->entries ) {
            if( e.is_item() && e.any_item() == it ) {
                return const_cast<inventory_entry *>( &e );
            }
        }
    }
    return nullptr;
}

// A worn hiking pack holding a small box (with a rock) and a blanket, as the inventory_ui tests do.
struct pack_fixture {
    static auto fresh_avatar() -> avatar & {
        clear_all_state();
        return get_avatar();
    }
    avatar &u = fresh_avatar();
    item *pack = nullptr;
    item *box = nullptr;
    item *blanket = nullptr;
    item *rock = nullptr;

    pack_fixture() {
        clear_character( u, true );
        u.wear_item( item::spawn( itype_id( "backpack_hiking" ), calendar::turn ), false );
        pack = u.worn.front();
        blanket = &u.i_add( item::spawn( itype_id( "blanket" ), calendar::turn ) );
        box = &u.i_add( item::spawn( itype_id( "box_small" ), calendar::turn ) );
        REQUIRE( box->try_put_in( item::spawn( itype_id( "test_rock" ), calendar::turn ) ).success() );
        rock = box->contents.all_items_top().front();
        REQUIRE( box->parent_item() == pack );
        REQUIRE( blanket->parent_item() == pack );
        REQUIRE( rock->parent_item() == box );
    }
};

// A worn hiking pack and nothing else: carried items stay at the top level of the pack.
auto bare_pack_avatar() -> avatar &
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, true );
    u.wear_item( item::spawn( itype_id( "backpack_hiking" ), calendar::turn ), false );
    return u;
}
} // namespace

// T1
TEST_CASE( "examiner check_parent_item needs a container with contents", "[inventory][ui][p3]" )
{
    pack_fixture f;
    SECTION( "a bag with an item" ) {
        inventory_examiner ex( f.u, f.box );
        ex.add_contained_items( f.box );
        CHECK( ex.check_parent_item() );
    }
    SECTION( "an empty bag" ) {
        detached_ptr<item> bag = item::spawn( itype_id( "backpack_hiking" ), calendar::turn );
        inventory_examiner ex( f.u, bag.get() );
        ex.add_contained_items( bag.get() );
        CHECK_FALSE( ex.check_parent_item() );
        CHECK( ex.execute() == NO_CONTENTS_TO_EXAMINE );
    }
}

// T2
TEST_CASE( "examiner cleanup returns the changes flag", "[inventory][ui][p3]" )
{
    pack_fixture f;
    inventory_examiner ex( f.u, f.box );
    ex.add_contained_items( f.box );
    CHECK( ex.cleanup() == EXAMINED_CONTENTS_UNCHANGED );
    ex.set_changes_made( true );
    CHECK( ex.cleanup() == EXAMINED_CONTENTS_WITH_CHANGES );
    ex.set_changes_made( false );
    CHECK( ex.cleanup() == EXAMINED_CONTENTS_UNCHANGED );
}

// T3
TEST_CASE( "insert selector stats show the remaining volume and the marked items",
           "[inventory][ui][p3]" )
{
    avatar &u = bare_pack_avatar();
    // a free medium box (3 L) holding one rock; a second rock waits in the pack
    detached_ptr<item> target = item::spawn( itype_id( "box_medium" ), calendar::turn );
    REQUIRE( target->try_put_in( item::spawn( itype_id( "test_rock" ), calendar::turn ) ).success() );
    const units::volume used = target->get_contents_volume();
    const units::volume capacity = 3_liter;
    item &second_rock = u.i_add( item::spawn( itype_id( "test_rock" ), calendar::turn ) );

    const holster_test_preset preset( *target );
    insert_probe sel( u, preset );
    sel.add_character_items( u );
    sel.own_inv_column.prepare_paging();
    sel.own_gear_column.prepare_paging();
    inventory_entry *row = row_of( sel, &second_rock );
    REQUIRE( row != nullptr );
    REQUIRE( row->is_selectable() );

    std::vector<std::string> lines = header_lines( sel.get_raw_stats() );
    REQUIRE( lines.size() >= 2 );
    // first line: used/total volume of the target
    CHECK( has_text( lines.front(), format_volume( used ) ) );
    CHECK( has_text( lines.front(), format_volume( capacity ) ) );
    // last line: nothing marked yet
    CHECK( has_text( lines.back(), format_volume( 0_ml ) ) );

    sel.set_chosen_count( *row, 1 );
    lines = header_lines( sel.get_raw_stats() );
    REQUIRE( lines.size() >= 2 );
    CHECK( has_text( lines.front(), format_volume( used ) ) ); // the target is not touched by marking
    CHECK( has_text( lines.back(), format_volume( second_rock.volume() ) ) );
}

// T4
TEST_CASE( "insert selector does not offer a plate when both large slots are full",
           "[inventory][ui][p3]" )
{
    avatar &u = bare_pack_avatar();
    detached_ptr<item> vest = item::spawn( itype_id( "ballistic_vest_esapi" ), calendar::turn );
    for( int i = 0; i < 2; i++ ) {
        REQUIRE( vest->put_in( item::spawn( itype_id( "esapi_plate" ), calendar::turn ),
                               pocket_type::CONTAINER ).success() );
    }
    item &third = u.i_add( item::spawn( itype_id( "esapi_plate" ), calendar::turn ) );

    const holster_test_preset preset( *vest );
    insert_probe sel( u, preset );
    sel.add_character_items( u );
    sel.own_inv_column.prepare_paging();
    sel.own_gear_column.prepare_paging();
    inventory_entry *row = row_of( sel, &third );
    REQUIRE( row != nullptr );
    CHECK_FALSE( row->is_selectable() );
    CHECK( has_text( preset.get_denial( &third ), "already contains a plate" ) );

    // the denied plate adds nothing to the marked volume
    const std::vector<std::string> lines = header_lines( sel.get_raw_stats() );
    REQUIRE( lines.size() >= 2 );
    CHECK( has_text( lines.back(), format_volume( 0_ml ) ) );
}

// T5
TEST_CASE( "container selector stats match the pockets of the container", "[inventory][ui][p3]" )
{
    pack_fixture f;
    // a free hiking pack: several soft pockets (data/json/items/armor/storage.json)
    detached_ptr<item> box = item::spawn( itype_id( "backpack_hiking" ), calendar::turn );
    REQUIRE( box->try_put_in( item::spawn( itype_id( "test_rock" ), calendar::turn ) ).success() );
    item *rock = box->contents.all_items_top().front();

    units::volume capacity = 0_ml;
    for( const item_pocket *p : box->get_container_pockets() ) {
        capacity += p->volume_capacity();
    }
    REQUIRE( box->get_container_pockets().size() >= 2 );
    container_probe sel( f.u, box.get() );
    const std::vector<std::string> lines = header_lines( sel.get_raw_stats() );
    REQUIRE_FALSE( lines.empty() );
    CHECK( has_text( lines.front(), format_volume( capacity ) ) );
    CHECK( has_text( lines.front(), format_volume( rock->volume() ) ) );
    CHECK( has_text( lines.front(), "Contents Weight:" ) );
    CHECK( has_text( lines.front(), string_format( "%.1f", round_up( convert_weight( rock->weight() ),
                                   1 ) ) ) );
}

// T6
TEST_CASE( "eventually_contains follows the parents at any depth", "[inventory][ui][p3]" )
{
    pack_fixture f;
    CHECK( eventually_contains( *f.box, *f.rock ) );
    CHECK( eventually_contains( *f.pack, *f.rock ) ); // two levels deep
    CHECK( eventually_contains( *f.pack, *f.blanket ) );
    CHECK_FALSE( eventually_contains( *f.blanket, *f.rock ) ); // a sibling of the box
    CHECK_FALSE( eventually_contains( *f.rock, *f.pack ) );
    CHECK_FALSE( eventually_contains( *f.box, *f.box ) );
}
