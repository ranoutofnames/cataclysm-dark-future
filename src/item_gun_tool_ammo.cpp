#include "item.h"

#include <algorithm>
#include <set>
#include <vector>

#include "enums.h"
#include "flag.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"

bool item::allows_speedloader( const itype_id &speedloader_id ) const
{
    return contents.allows_speedloader( speedloader_id );
}

bool item::can_reload_with( const item &ammo, bool now ) const
{
    if( has_flag( flag_NO_RELOAD ) && !has_flag( flag_VEHICLE ) ) {
        return false; // turrets ignore NO_RELOAD flag
    }

    if( now && ammo.is_magazine() && !ammo.empty() ) {
        const ammotype loaded_at = ammo.contents.first_ammo().ammo_type();
        const bool tool_match = is_tool() && type->tool->ammo_id.count( loaded_at );
        const bool gun_match = !is_tool() && ammo_types().count( loaded_at );
        if( !tool_match && !gun_match ) {
            // Sibling MAGAZINE_WELLs may take auxiliary ammotypes outside the
            // host's declared `ammo` field. Accept if any well's allowed mags
            // have a MAGAZINE pocket restricted to this ammo type.
            bool well_match = false;
            for( const item_pocket *p : contents.get_all_reloadable_pockets() ) {
                if( !p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    continue;
                }
                for( const itype_id &mag_id : p->item_type_restrictions() ) {
                    if( !mag_id->magazine ) {
                        continue;
                    }
                    for( const pocket_data &mp : mag_id->pockets ) {
                        if( mp.type == pocket_type::MAGAZINE &&
                            mp.ammo_restriction.count( loaded_at ) ) {
                            well_match = true;
                            break;
                        }
                    }
                    if( well_match ) {
                        break;
                    }
                }
                if( well_match ) {
                    break;
                }
            }
            if( !well_match ) {
                return false;
            }
        }
    }

    // Check if the item is in general compatible with any reloadable pocket.
    return contents.can_reload_with( ammo, now );
}

bool item::is_magazine() const
{
    // CDF: the pocket clause is true only for items with a MAGAZINE pocket (none in CDF data today).
    return !!type->magazine || contents.has_pocket_type( pocket_type::MAGAZINE );
}

// CDF: body is today's BN item::is_battery (type->battery); DDA's is_battery stays BN.
bool item::is_vehicle_battery() const
{
    return !!type->battery;
}

bool item::is_ammo_belt() const
{
    return is_magazine() && has_flag( flag_MAG_BELT );
}

bool item::is_ammo() const
{
    return !!type->ammo;
}

bool item::is_medical_tool() const
{
    return type->get_use( "heal" ) != nullptr;
}

bool item::is_toolmod() const
{
    return !is_gunmod() && type->mod;
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

int item::ammo_consume_in_pocket( const std::string &id, int qty, map &here,
                                  const tripoint_bub_ms &pos )
{
    return contents.ammo_consume_in_pocket( id, qty, &here, pos );
}

bool item::uses_firing_requirements() const
{
    // itype-level firing_requirements is the item's own consumption, empty for a bare gunmod.
    return type && !type->firing_requirements.empty();
}

const item_pocket *item::primary_ammo_pocket() const
{
    // MAGAZINE pockets count too: the schema accepts integral-mag items as
    // multimag primaries (e.g. integral cartridge well + battery well).
    const std::vector<const item_pocket *> wells = get_pockets(
    []( const item_pocket & p ) {
        return p.is_type( pocket_type::MAGAZINE_WELL ) ||
               p.is_type( pocket_type::MAGAZINE );
    } );
    if( wells.empty() ) {
        return nullptr;
    }
    const std::set<ammotype> *gun_ammo = nullptr;
    if( type && type->gun ) {
        gun_ammo = &type->gun->ammo;
    }
    auto matches_gun_ammo = [&gun_ammo]( const item_pocket * p ) {
        if( gun_ammo == nullptr || gun_ammo->empty() ) {
            return false;
        }
        for( const ammotype &at : p->ammo_types() ) {
            if( gun_ammo->count( at ) ) {
                return true;
            }
        }
        for( const itype_id &allowed : p->item_type_restrictions() ) {
            if( !allowed->magazine ) {
                continue;
            }
            for( const ammotype &slot : allowed->magazine->type ) {
                if( gun_ammo->count( slot ) ) {
                    return true;
                }
            }
        }
        return false;
    };
    auto loaded = []( const item_pocket * p ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            return p->magazine_current() != nullptr;
        }
        return !p->empty();
    };
    for( const item_pocket *p : wells ) {
        if( matches_gun_ammo( p ) && loaded( p ) ) {
            return p;
        }
    }
    for( const item_pocket *p : wells ) {
        if( matches_gun_ammo( p ) ) {
            return p;
        }
    }
    for( const item_pocket *p : wells ) {
        if( loaded( p ) ) {
            return p;
        }
    }
    return wells.front();
}

const item *item::ammo_identity_mag() const
{
    if( uses_firing_requirements() && type && type->gun ) {
        const item_pocket *p = primary_ammo_pocket();
        if( p == nullptr ) {
            return nullptr;
        }
        if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            return p->magazine_current();
        }
        // Integral MAGAZINE pocket: the ammo itself drives identity.
        // cycle_action leaves spent casings in the pocket; skip them.
        for( const item *e : p->all_items_top() ) {
            if( e->has_flag( flag_CASING ) ) {
                continue;
            }
            return e;
        }
        return nullptr;
    }
    return magazine_current();
}
