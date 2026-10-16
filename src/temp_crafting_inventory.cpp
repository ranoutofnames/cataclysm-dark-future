#include "temp_crafting_inventory.h"

#include <algorithm>

#include "calendar.h"
#include "character.h"
#include "inventory.h"
#include "make_static.h"
#include "map/map.h"
#include "map/map_selector.h"
#include "vehicle/vehicle_selector.h"

size_t temp_crafting_inventory::size() const
{
    return items.size();
}

void temp_crafting_inventory::clear()
{
    items.clear();
}

void temp_crafting_inventory::add_item_loc( item &it )
{
    items.push_back( &it );
}

void temp_crafting_inventory::add_all_ref( const temp_crafting_inventory &other )
{
    items.insert( items.end(), other.items.begin(), other.items.end() );
}

void temp_crafting_inventory::add_all_ref( Character &guy )
{
    guy.visit_items( [this]( item * it, item * parent ) {
        if( parent == nullptr ) {
            items.push_back( it );
        }
        return VisitResponse::SKIP;
    } );
}

void temp_crafting_inventory::add_all_ref( map_cursor &cur )
{
    cur.visit_items( [this]( item * it, item * parent ) {
        if( parent == nullptr ) {
            items.push_back( it );
        }
        return VisitResponse::SKIP;
    } );
}

void temp_crafting_inventory::add_all_ref( vehicle_cursor &cur )
{
    cur.visit_items( [this]( item * it, item * parent ) {
        if( parent == nullptr ) {
            items.push_back( it );
        }
        return VisitResponse::SKIP;
    } );
}

item &temp_crafting_inventory::add_item_copy( const item &it )
{
    // CDF: BN temporaries are owned by the global pool, not by this container
    item &copy = *item::spawn_temporary( it );
    items.push_back( &copy );
    return copy;
}

item &temp_crafting_inventory::add_pseudo_item( const itype_id &id )
{
    return add_pseudo_item( item( id, calendar::turn, 0 ) );
}

item &temp_crafting_inventory::add_pseudo_item( const item &it )
{
    item &copy = add_item_copy( it );
    copy.set_flag( STATIC( flag_id( "PSEUDO" ) ) );
    return copy;
}

int temp_crafting_inventory::count_item( const itype_id &item_type ) const
{
    int num = 0;
    for( const item *it : items ) {
        if( it->typeId() == item_type ) {
            num += it->count();
        }
    }
    return num;
}

void temp_crafting_inventory::form_from_zone( map &m, std::unordered_set<tripoint_abs_ms> &zone_pts,
        const Character *pl )
{
    // CDF: delegate the map/furniture/vehicle scan to BN's inventory, then keep the references
    inventory inv;
    inv.form_from_zone( m, zone_pts, pl, false );
    items.clear();
    inv.dump( items );
}

void temp_crafting_inventory::form_from_map( const tripoint_bub_ms &origin, int range,
        const Character *pl, bool clear_path )
{
    inventory inv;
    inv.form_from_map( origin, range, pl, false, clear_path );
    items.clear();
    inv.dump( items );
}

void temp_crafting_inventory::form_from_map( map &m, const tripoint_bub_ms &origin, int range,
        const Character *pl, bool clear_path )
{
    inventory inv;
    inv.form_from_map( m, origin, range, pl, false, clear_path );
    items.clear();
    inv.dump( items );
}

void temp_crafting_inventory::form_from_map( map &m, std::vector<tripoint_bub_ms> pts,
        const Character *pl )
{
    inventory inv;
    inv.form_from_map( m, std::move( pts ), pl, false );
    items.clear();
    inv.dump( items );
}

std::vector<item *> temp_crafting_inventory::remove_items_with( const
        std::function<bool( const item & )> &filter, int count )
{
    std::vector<item *> res;
    for( auto it = items.begin(); it != items.end() && count > 0; ) {
        if( filter( **it ) ) {
            res.push_back( *it );
            it = items.erase( it );
            count--;
        } else {
            ++it;
        }
    }
    return res;
}

void temp_crafting_inventory::dump( std::vector<item *> &dest ) const
{
    dest.insert( dest.end(), items.begin(), items.end() );
}
