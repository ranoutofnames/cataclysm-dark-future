#include "item.h"

#include <algorithm>
#include <vector>

#include "enums.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"

bool item::allows_speedloader( const itype_id &speedloader_id ) const
{
    return contents.allows_speedloader( speedloader_id );
}

// Returns the MAGAZINE_WELL pocket at well_idx in contents, or nullptr if
// out of range or the pocket at that index is not a MAGAZINE_WELL.
static const item_pocket *well_pocket_at( const item &it, int well_idx )
{
    if( well_idx < 0 ) {
        return nullptr;
    }
    const std::vector<const item_pocket *> all = it.get_pockets(
    []( const item_pocket & ) {
        return true;
    } );
    if( well_idx >= static_cast<int>( all.size() ) ) {
        return nullptr;
    }
    const item_pocket *p = all[well_idx];
    if( !p->is_type( pocket_type::MAGAZINE_WELL ) ) {
        return nullptr;
    }
    return p;
}

int item::ammo_remaining( int well_idx ) const
{
    const item_pocket *p = well_pocket_at( *this, well_idx );
    if( p == nullptr ) {
        return 0;
    }
    const item *mag = p->magazine_current();
    return mag ? mag->ammo_remaining() : 0;
}

int item::ammo_capacity( int well_idx ) const
{
    const item_pocket *p = well_pocket_at( *this, well_idx );
    if( p == nullptr ) {
        return 0;
    }
    const item *mag = p->magazine_current();
    if( !mag ) {
        return 0;
    }
    const itype *adata = mag->ammo_data();
    if( adata ) {
        // CDF: no item::ammo_capacity( const ammotype & ); use the magazine's contents.
        return mag->get_contents().ammo_capacity( adata->ammo->type );
    }
    const itype_id default_ammo = mag->ammo_default();
    if( !default_ammo.is_null() && default_ammo->ammo ) {
        // CDF: as above, via the magazine's contents.
        return mag->get_contents().ammo_capacity( default_ammo->ammo->type );
    }
    return 0;
}

int item::remaining_ammo_capacity( int well_idx ) const
{
    const item_pocket *p = well_pocket_at( *this, well_idx );
    if( p == nullptr ) {
        return 0;
    }
    const item *mag = p->magazine_current();
    if( !mag ) {
        return 0;
    }
    // CDF: no no-arg item::remaining_ammo_capacity(); derive it from the
    // per-well capacity and the loaded count.
    return std::max( 0, ammo_capacity( well_idx ) - ammo_remaining( well_idx ) );
}

std::vector<item_pocket *> item::all_magazine_well_pockets()
{
    return get_pockets(
    []( const item_pocket & p ) {
        return p.is_type( pocket_type::MAGAZINE_WELL );
    } );
}

std::vector<const item_pocket *> item::all_magazine_well_pockets() const
{
    return get_pockets(
    []( const item_pocket & p ) {
        return p.is_type( pocket_type::MAGAZINE_WELL );
    } );
}
