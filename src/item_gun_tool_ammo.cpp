#include "item.h"

#include <algorithm>
#include <set>
#include <vector>

#include "character.h"
#include "enums.h"
#include "flag.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
// CDF: needed for the free get_map() used as the dummy map argument below.
#include "map/map.h"

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

// CDF: single removal point for the AUTO-26 O1 / REPLAN D14 legacy-charges guard. BN kept ammo
// in direct `charges` for tools and guns without a MAGAZINE pocket; DDA counts only magazines.
// Tools leave this in 4-b2, guns at the last 7-R2 group; 9-4 checks that no item still uses it.
// A MAGAZINE_WELL pocket alone does not end the legacy path: BN guns with an optional `magazines`
// list (e.g. sw_619 with a speedloader) get a well yet still store loaded rounds in `charges`
// while magazine_integral() holds.
static bool cdf_legacy_direct_charges( const item &it )
{
    if( !it.is_tool() && !it.is_gun() ) {
        return false;
    }
    const item_contents &c = it.get_contents();
    // CDF: a loaded magazine wins and the item's own `charges` are ignored (BN
    // item::ammo_remaining: `if( mag ) return mag->ammo_remaining();`). magazine_current() also
    // sees a magazine parked in MIGRATION: a smartphone has magazine_well but no `magazines`
    // list, so it gets no well pocket and its battery lands in MIGRATION, while max_charges and
    // initial_charges keep it magazine_integral().
    return !c.has_pocket_type( pocket_type::MAGAZINE ) &&
           ( !c.has_pocket_type( pocket_type::MAGAZINE_WELL ) || it.magazine_integral() ) &&
           it.magazine_current() == nullptr;
}

int item::ammo_remaining( const map &here, const std::set<ammotype> &ammo, const Character *carrier,
                          const bool include_linked ) const
{
    const bool is_tool_with_carrier = carrier != nullptr && is_tool();

    // CDF: NEARBY_AMMO block (DDA 1439-1443) omitted: flag_USES_NEARBY_AMMO is
    // absent from CDF (flag.h/cpp, flags.json) and Character::crafting_inventory()
    // is non-const here. Unblock: 7-R4/R5.

    int ret = 0;

    // Sum ammo across every loaded MAGAZINE_WELL pocket.
    // TODO(multimag): aggregate; per-well callers use the indexed overload.
    for( const item *mag : magazines_current() ) {
        ret += mag->ammo_remaining( );
    }
    // CDF: a magazine parked outside a well (MIGRATION, see cdf_legacy_direct_charges) still counts,
    // as in BN's `if( mag ) return mag->ammo_remaining();`.
    if( ( is_tool() || is_gun() ) && magazines_current().empty() ) {
        if( const item *parked = magazine_current() ) {
            ret += parked->ammo_remaining( );
        }
    }

    // Integral MAGAZINE pockets sitting alongside wells (multimag).
    // is_magazine() hosts are covered by the dedicated branch below.
    if( !is_magazine() ) {
        for( const item_pocket *p : get_pockets( []( const item_pocket & q ) {
        return q.is_type( pocket_type::MAGAZINE );
        } ) ) {
            for( const item *e : p->all_items_top() ) {
                if( e->has_flag( flag_CASING ) ) {
                    continue;
                }
                if( !e->is_ammo() ) {
                    continue;
                }
                if( !ammo.empty() && ammo.find( e->ammo_type() ) == ammo.end() ) {
                    continue;
                }
                ret += e->charges > 0 ? e->charges : 1;
            }
        }
    }

    // CDF: cable link block (DDA 1474-1484) omitted: CDF has no link API
    // (link(), link_length(), connected_battery_power_level() are absent).
    // Unblock: 4-b2 (cable tail) or an owner decision on a cable port.
    // `here` and `include_linked` are kept for signature parity.
    static_cast<void>( here );
    static_cast<void>( include_linked );

    // Non ammo using item that uses charges (DDA 1486-1489), plus the CDF legacy guard.
    // CDF: is_ammo() excluded: BN returned 0 for an ammo item's own charges (iteminfo "Ammunition type").
    if( ( ammo.empty() && !is_ammo() ) || cdf_legacy_direct_charges( *this ) ) {
        ret += charges;
    }

    // Magazines and integral magazines on their own
    if( is_magazine() ) {
        for( const item *e : contents.all_items_top( pocket_type::MAGAZINE ) ) {
            if( e->is_ammo() ) {
                ret += e->charges;
            }
        }
    }

    // Handle non-magazines with ammo_restriction in a CONTAINER type pocket (like quivers)
    if( magazines_current().empty() && !is_magazine() && !ammo.empty() ) {
        for( const item *e : contents.all_items_top( pocket_type::CONTAINER ) ) {
            if( e->is_ammo() && ammo.find( e->ammo_type() ) != ammo.end() ) {
                ret += e->charges;
            }
        }
    }

    // UPS/bionic power can replace ammo requirement.
    // Only for tools. Guns should always use energy_drain for electricity use.
    if( is_tool_with_carrier ) {
        if( has_flag( flag_USES_BIONIC_POWER ) ) {
            ret += units::to_kilojoule( carrier->get_power_level() );
        }
        // CDF: UPS tail deferred to 4-b2 (Character::available_ups absent).
    }

    return ret;
}

int item::ammo_remaining_linked( const map &here, const Character *carrier ) const
{
    std::set<ammotype> ammo = ammo_types();
    return ammo_remaining( here, ammo, carrier, true );
}
int item::ammo_remaining( const Character *carrier ) const
{
    std::set<ammotype> ammo = ammo_types();
    return ammo_remaining( get_map(), ammo, carrier, false ); // get_map() is a dummy parameter here.
}

int item::ammo_remaining_linked( const map &here ) const
{
    return ammo_remaining_linked( here, nullptr );
}

int item::ammo_remaining( ) const
{
    return ammo_remaining( nullptr );
}

int item::remaining_ammo_capacity() const
{
    if( ammo_types().empty() ) {
        return 0;
    }

    // First-loaded-mag fallback; per-pocket callers use the dedicated overload.
    if( const item *mag = magazine_current() ) {
        return mag->remaining_ammo_capacity();
    }

    // Integral mag: ammo_remaining() excludes MAGAZINE_WELL here.
    const itype *loaded_ammo = ammo_data();
    if( loaded_ammo == nullptr ) {
        // CDF: item::find_type() does not exist here; string_id::operator-> resolves the itype.
        const itype_id default_ammo = ammo_default();
        loaded_ammo = default_ammo.is_null() ? nullptr : &*default_ammo;
    }
    if( !loaded_ammo || !loaded_ammo->ammo ) {
        return 0;
    }
    // Clamp: a capacity-shrinking mod can leave an integral pocket overfilled.
    return std::max( 0, ammo_capacity( loaded_ammo->ammo->type ) - ammo_remaining( ) );
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
        return mag->ammo_capacity( adata->ammo->type );
    }
    const itype_id default_ammo = mag->ammo_default();
    if( !default_ammo.is_null() && default_ammo->ammo ) {
        return mag->ammo_capacity( default_ammo->ammo->type );
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
    return mag ? mag->remaining_ammo_capacity() : 0;
}

int item::ammo_capacity( const ammotype &ammo, bool include_linked ) const
{
    // CDF: cable link block (DDA 1722-1726) omitted: CDF has no link API.
    // Unblock: 4-b2 (cable tail) or an owner decision on a cable port.
    // `include_linked` is kept for signature parity.
    static_cast<void>( include_linked );

    // Route per-ammotype lookup through the matching pocket; magazine_current()
    // returns the first loaded mag regardless of ammotype and misreports here.
    if( const item_pocket *p = pocket_for_ammo( ammo ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            if( const item *mag = p->magazine_current() ) {
                return mag->ammo_capacity( ammo );
            }
            // Multimag hosts surface the default mag's capacity for unloaded
            // wells so item info shows the projected per-well number. Single-
            // well guns keep returning 0 so "is this loaded?" stays correct.
            if( uses_firing_requirements() ) {
                const itype_id default_mag = p->magazine_default();
                if( !default_mag.is_null() && default_mag->magazine ) {
                    return default_mag->magazine->capacity;
                }
            }
        } else {
            const auto &restrictions = p->get_pocket_data()
                                       ? p->get_pocket_data()->ammo_restriction
                                       : std::map<ammotype, int> {};
            if( restrictions.find( ammo ) != restrictions.end() ) {
                // pocket-level applies any installed capacity_mods scaling
                return p->ammo_capacity( ammo );
            }
        }
    }

    if( const item *mag = magazine_current() ) {
        return mag->ammo_capacity( ammo );
    }
    if( has_flag( flag_USES_BIONIC_POWER ) ) {
        return units::to_kilojoule( get_player_character().get_max_power_level() );
    }

    if( contents.has_pocket_type( pocket_type::MAGAZINE ) ) {
        return contents.ammo_capacity( ammo );
    }
    if( is_magazine() ) {
        return type->magazine->capacity;
    }
    return 0;
}

const item_pocket *item::pocket_for_ammo( const ammotype &at ) const
{
    if( at.is_null() ) {
        return nullptr;
    }
    const std::vector<const item_pocket *> wells = get_pockets(
    []( const item_pocket & p ) {
        return p.is_type( pocket_type::MAGAZINE_WELL ) ||
               p.is_type( pocket_type::MAGAZINE );
    } );
    auto matches = [&at]( const item_pocket * p ) {
        for( const ammotype &pt : p->ammo_types() ) {
            if( pt == at ) {
                return true;
            }
        }
        for( const itype_id &allowed : p->item_type_restrictions() ) {
            if( !allowed->magazine ) {
                continue;
            }
            for( const ammotype &slot : allowed->magazine->type ) {
                if( slot == at ) {
                    return true;
                }
            }
        }
        return false;
    };
    for( const item_pocket *p : wells ) {
        if( !matches( p ) ) {
            continue;
        }
        if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            if( p->magazine_current() != nullptr ) {
                return p;
            }
        } else if( !p->empty() ) {
            return p;
        }
    }
    for( const item_pocket *p : wells ) {
        if( matches( p ) ) {
            return p;
        }
    }
    return nullptr;
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

// CDF: host wells only; mod-supplied wells are folded in by 3-7
std::vector<itype_id> item::magazines_default() const
{
    return contents.magazines_default();
}

void item::dress_magazine_wells( bool insert_default_mag, bool fill_with_default_ammo )
{
    for( item_pocket *well : all_magazine_well_pockets() ) {
        item *current_mag = well->magazine_current();
        if( current_mag != nullptr ) {
            if( fill_with_default_ammo && current_mag->ammo_remaining() == 0 &&
                !current_mag->ammo_default().is_null() ) {
                current_mag->ammo_set( current_mag->ammo_default() );
            }
            continue;
        }
        if( !insert_default_mag ) {
            continue;
        }
        const itype_id mag_id = well->magazine_default();
        if( mag_id.is_null() ) {
            continue;
        }
        // CDF: detached overload instead of DDA's copy `item mag( mag_id, birthday() )`
        detached_ptr<item> mag = item::spawn( mag_id, birthday() );
        if( fill_with_default_ammo && !mag->ammo_default().is_null() ) {
            mag->ammo_set( mag->ammo_default() );
        }
        // CDF: the detached insert leaves the item with us on failure; ~detached_ptr destroys it
        const ret_val<item *> inserted = well->insert_item( std::move( mag ) );
        if( !inserted.success() ) {
            debugmsg( "dress_magazine_wells: cannot insert default magazine '%s' into '%s'",
                      mag_id.str(), typeId().str() );
        }
    }
}

std::vector<item *> item::magazines_current()
{
    return contents.magazines_current();
}

std::vector<const item *> item::magazines_current() const
{
    return contents.magazines_current();
}

int item::ammo_consume_in_pocket( const std::string &id, int qty, map &here,
                                  const tripoint_bub_ms &pos )
{
    return contents.ammo_consume_in_pocket( id, qty, &here, pos );
}

item_pocket *item::pocket_by_id( const std::string &id )
{
    if( id.empty() ) {
        return nullptr;
    }
    std::vector<item_pocket *> hits = get_pockets(
    [&id]( const item_pocket & p ) {
        return p.get_pocket_data() && p.get_pocket_data()->pocket_id == id;
    } );
    return hits.empty() ? nullptr : hits.front();
}

const item_pocket *item::pocket_by_id( const std::string &id ) const
{
    if( id.empty() ) {
        return nullptr;
    }
    std::vector<const item_pocket *> hits = get_pockets(
    [&id]( const item_pocket & p ) {
        return p.get_pocket_data() && p.get_pocket_data()->pocket_id == id;
    } );
    return hits.empty() ? nullptr : hits.front();
}

int item::ammo_remaining_in_pocket( const std::string &id ) const
{
    const item_pocket *p = pocket_by_id( id );
    if( p == nullptr ) {
        return 0;
    }
    if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
        const item *mag = p->magazine_current();
        return mag ? mag->ammo_remaining( ) : 0;
    }
    if( p->is_type( pocket_type::MAGAZINE ) ) {
        // Exclude spent casings - cycle_action force-inserts them into
        // the integral pocket after a shot, and they shouldn't count as
        // loaded ammo for feasibility / display.
        int total = 0;
        for( const item *e : p->all_items_top() ) {
            if( e->has_flag( flag_CASING ) ) {
                continue;
            }
            total += e->charges;
        }
        return total;
    }
    return 0;
}

bool item::uses_firing_requirements() const
{
    // itype-level firing_requirements is the item's own consumption, empty for a bare gunmod.
    return type && !type->firing_requirements.empty();
}

bool item::needs_charges_to_use() const
{
    // CDF: get_gun_ups_drain() is an int in UPS charges, not kJ; DDA get_gun_energy_drain() > 0_kJ is not ported
    return ammo_required() > 0 || get_gun_ups_drain() > 0 || uses_firing_requirements();
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

const itype *item::ammo_data() const
{
    const item *mag = ammo_identity_mag();
    if( mag ) {
        return mag->ammo_data();
    }
    if( uses_firing_requirements() && is_gun() ) {
        return nullptr;
    }

    if( is_ammo() ) {
        return type;
    }

    if( is_magazine() ) {
        return !contents.empty() ? contents.first_ammo().ammo_data() : nullptr;
    }

    auto mods = is_gun() ? gunmods() : toolmods();
    for( const item *e : mods ) {
        // CDF: keeps the BN loop (same result as DDA's has_template/find_template, no extra include)
        if( !e->type->mod->ammo_modifier.empty() && e->ammo_current() &&
            e->ammo_current().is_valid() ) {
            return &*e->ammo_current();
        }
    }

    // CDF: legacy curammo until 4-b2 (DDA returns nullptr)
    return curammo;
}

itype_id item::ammo_current() const
{
    const itype *ammo = ammo_data();
    if( ammo ) {
        return ammo->get_id();
    }
    // CDF: USES_NEARBY_AMMO absent (7-R4/R5)

    return itype_id::NULL_ID();
}

// CDF: file-static here; the item.cpp copy is not visible
static const ammotype ammo_battery( "battery" );

bool item::has_ammo() const
{
    const item *mag = ammo_identity_mag();
    if( mag ) {
        return mag->has_ammo();
    }
    // Multimag gun identity is anchored to the projectile pocket; never
    // borrow sibling-pocket ammo (e.g. battery wells) when it is empty.
    if( uses_firing_requirements() && is_gun() ) {
        return false;
    }

    if( is_ammo() ) {
        return true;
    }

    if( is_magazine() ) {
        return !contents.empty() && contents.first_ammo().has_ammo();
    }

    auto mods = is_gun() ? gunmods() : toolmods();
    for( const item *e : mods ) {
        if( !e->type->mod->ammo_modifier.empty() && e->has_ammo() ) {
            return true;
        }
    }

    return false;
}

bool item::has_ammo_data() const
{
    const item *mag = ammo_identity_mag();
    if( mag ) {
        return mag->has_ammo_data();
    }
    if( uses_firing_requirements() && is_gun() ) {
        return false;
    }

    if( is_ammo() ) {
        return true;
    }

    if( is_magazine() ) {
        return !contents.empty() && contents.first_ammo().has_ammo_data();
    }

    auto mods = is_gun() ? gunmods() : toolmods();
    for( const item *e : mods ) {
        if( !e->type->mod->ammo_modifier.empty() && e->has_ammo_data() ) {
            return true;
        }
    }

    return false;
}

bool item::pocket_accepts_battery( const item_pocket *p ) const
{
    if( p == nullptr ) {
        return false;
    }
    for( const ammotype &at : p->ammo_types() ) {
        if( at == ammo_battery ) {
            return true;
        }
    }
    for( const itype_id &allowed : p->item_type_restrictions() ) {
        if( !allowed->magazine ) {
            continue;
        }
        for( const ammotype &slot : allowed->magazine->type ) {
            if( slot == ammo_battery ) {
                return true;
            }
        }
    }
    return false;
}

bool item::pocket_is_primary_ammo( const item_pocket *p ) const
{
    if( p == nullptr || !type || !type->gun || type->gun->ammo.empty() ) {
        return false;
    }
    for( const ammotype &at : p->ammo_types() ) {
        if( type->gun->ammo.count( at ) ) {
            return true;
        }
    }
    for( const itype_id &allowed : p->item_type_restrictions() ) {
        if( !allowed->magazine ) {
            continue;
        }
        for( const ammotype &slot : allowed->magazine->type ) {
            if( type->gun->ammo.count( slot ) ) {
                return true;
            }
        }
    }
    return false;
}

const item &item::loaded_ammo() const
{
    const item *mag = ammo_identity_mag();
    if( mag ) {
        // Integral MAGAZINE primary returns the bare ammo item itself, not
        // a containing magazine; recursing on bare ammo lands at null.
        if( mag->is_ammo() ) {
            return *mag;
        }
        return mag->loaded_ammo();
    }
    if( uses_firing_requirements() && is_gun() ) {
        return null_item_reference();
    }

    if( is_magazine() ) {
        return !contents.empty() ? contents.first_ammo() : null_item_reference();
    }

    auto mods = is_gun() ? gunmods() : toolmods();
    for( const item *e : mods ) {
        const item &mod_ammo = e->loaded_ammo();
        if( !mod_ammo.is_null() ) {
            return mod_ammo;
        }
    }

    if( is_gun() && ammo_remaining( ) != 0 ) {
        return contents.first_ammo();
    }
    return null_item_reference();
}

int item::ammo_required() const
{
    // CDF: BN gate kept; itype::charges_to_use() returns 1 for every non-tool
    // in CDF (DDA returns 0), see OD-10.
    if( !is_tool() && !is_gun() ) {
        return 0;
    }

    if( is_gun() ) {
        // CDF: BN gun test (DDA uses type->gun->ammo.empty()); OD-10.
        if( ammo_types().empty() ) {
            return 0;
        // CDF: BN's FIRE_100/50/20 branch kept; data-driven and still used by
        // CDF data (OD-10).
        } else if( has_flag( flag_FIRE_100 ) ) {
            return 100;
        } else if( has_flag( flag_FIRE_50 ) ) {
            return 50;
        } else if( has_flag( flag_FIRE_20 ) ) {
            return 20;
        } else {
            int modifier = 0;
            float multiplier = 1.0f;
            for( const item *mod : gunmods() ) {
                modifier += mod->type->gunmod->ammo_to_fire_modifier;
                multiplier *= mod->type->gunmod->ammo_to_fire_multiplier;
            }
            return ( type->gun->ammo_to_fire * multiplier ) + modifier;
        }
    }

    return type->charges_to_use();
}

item &item::first_ammo()
{
    return contents.first_ammo();
}

const item &item::first_ammo() const
{
    return contents.first_ammo();
}

bool item::ammo_sufficient( const Character *carrier, int qty ) const
{
    map &here = get_map();

    if( count_by_charges() ) {
        return ammo_remaining_linked( here, carrier ) >= qty;
    }

    if( is_comestible() ) {
        return true;
    }

    // CDF: until 3-4d (OD-5): shots_remaining( here, carrier ) is absent;
    // this interim matches DDA for guns with no energy drain.
    return ammo_required() == 0 || ammo_remaining( carrier ) >= ammo_required() * qty;
}

int item::ammo_consume( int qty, const tripoint_bub_ms &pos, Character *carrier )
{
    return item::ammo_consume( qty, get_map(), pos, carrier );
}

int item::ammo_consume( int qty, map &here, const tripoint_bub_ms &pos, Character *carrier )
{
    if( qty < 0 ) {
        debugmsg( "Cannot consume negative quantity of ammo for %s", tname() );
        return 0;
    }
    if( uses_firing_requirements() ) {
        debugmsg( "ammo_consume(%d) called on multimag item %s; caller must use "
                  "consume_shots / consume_tool_uses instead", qty, tname() );
        return 0;
    }
    const int wanted_qty = qty;
    const bool is_tool_with_carrier = carrier != nullptr && is_tool();

    // CDF: NEARBY_AMMO block (DDA 1844-1858) deferred: flag_USES_NEARBY_AMMO is
    // absent from CDF (flag.h/cpp, flags.json) and Character::crafting_inventory()
    // is non-const here. Unblock: 7-R4/R5.

    // CDF: cable link discharge (DDA 1861-1870) deferred: CDF has no link API
    // (has_link_data()/link() absent). Unblock: 4-b2 (cable tail) or an owner
    // decision on a cable port.

    // Consume charges loaded in the item or its magazines
    if( is_magazine() || uses_magazine() ) {
        qty -= contents.ammo_consume( qty, &here, pos );
        // CDF: carrier->invalidate_weight_carried_cache() (DDA 1875-1877) cut:
        // CDF has no such method (cached_weight_carried() has no invalidate).
        // Unblock: name the CDF weight-cache call in 4-b2 if one is needed.
    }

    // CDF: a magazine parked outside a well is drained like a loaded one (matches ammo_remaining).
    if( ( is_tool() || is_gun() ) && contents.magazines_current().empty() ) {
        if( item *parked = magazine_current() ) {
            qty -= parked->ammo_consume( qty, here, pos, carrier );
        }
    }

    // CDF: legacy direct charges (D14, same helper as ammo_remaining). Remove with cdf_legacy_direct_charges.
    if( cdf_legacy_direct_charges( *this ) ) {
        int used = std::min( qty, charges );
        qty -= used;
        charges -= used;   // BN: charges is the ammo for legacy tools/guns
        if( charges == 0 ) {
            curammo = nullptr;
        }
    }

    // Dirty fix: activating a container of meds leads here, and used to use up all of the charges.
    if( is_medication() && charges > 1 ) {
        int charg_used = std::min( charges, qty );
        charges -= charg_used;
        qty -= charg_used;
    }

    // Some weird internal non-item charges (used by grenades)
    if( is_tool() && type->tool->ammo_id.empty() ) {
        int charg_used = std::min( charges, qty );
        charges -= charg_used;
        qty -= charg_used;
    }

    bool is_off_grid_powered = has_flag( flag_USE_UPS ) || has_flag( flag_USES_BIONIC_POWER );

    // Modded tools can consume UPS/bionic energy instead of ammo.
    // Guns handle energy in energy_consume()
    if( is_tool_with_carrier && is_off_grid_powered ) {
        units::energy wanted_energy = units::from_kilojoule( static_cast<std::int64_t>( qty ) );

        // CDF: UPS branch (DDA 1901-1903) deferred: Character::consume_ups() is
        // absent; use_charges( itype_UPS, n ) is the substitute. Unblock: 4-b2 (OD-6).

        // CDF: OD-4: the bionic tail runs after the legacy branch and only for a
        // non-null carrier (DDA behaviour); no avatar fallback inside the body.
        if( has_flag( flag_USES_BIONIC_POWER ) ) {
            units::energy bio_used = std::min( carrier->get_power_level(), wanted_energy );
            carrier->mod_power_level( -bio_used );
            wanted_energy -= bio_used;
        }

        // It is possible for this to cause rounding error due to different precision of energy
        // But that can happen only if there was not enough ammo and you shouldn't be able to use without sufficient ammo
        qty = units::to_kilojoule( wanted_energy );
    }
    return wanted_qty - qty;
}

bool item::uses_magazine() const
{
    return contents.has_pocket_type( pocket_type::MAGAZINE_WELL );
}

units::energy item::energy_consume( units::energy qty, const tripoint_bub_ms &pos,
                                    Character *carrier,
                                    float fuel_efficiency )
{
    return item::energy_consume( qty, &get_map(), pos, carrier, fuel_efficiency );
}

units::energy item::energy_consume( units::energy qty, map *here, const tripoint_bub_ms &pos,
                                    Character *carrier,
                                    float fuel_efficiency )
{
    if( qty < 0_kJ ) {
        debugmsg( "Cannot consume negative quantity of energy for %s", tname() );
        return 0_kJ;
    }

    const units::energy wanted_energy = qty;

    // Consume battery(ammo) and other fuel (if allowed)
    if( is_battery() || fuel_efficiency >= 0 ) {
        int consumed_kj = contents.ammo_consume( units::to_kilojoule( qty ), here, pos, fuel_efficiency );
        qty -= units::from_kilojoule( static_cast<std::int64_t>( consumed_kj ) );
        // Either we're out of juice or truncating the value above means we didn't drain quite enough.
        // In the latter case at least this will bump up energy enough to satisfy the remainder,
        // if not it will drain the item all the way.
        // TODO: reconsider what happens with fuel burning, right now this stashes
        // the remainder of energy from burning the fuel in the item in question,
        // which potentially allows it to burn less fuel next time.
        // Do we want an implicit 1kJ battery in the generator to smooth things out?
        if( qty > energy ) {
            int64_t residual_drain = contents.ammo_consume( 1, here, pos, fuel_efficiency );
            energy += units::from_kilojoule( residual_drain );
        }
        if( qty > energy ) {
            qty -= energy;
            energy = 0_J;
        } else {
            energy -= qty;
            qty = 0_J;
        }
    }

    // Consume energy from contained magazine
    if( magazine_current() ) {
        qty -= magazine_current()->energy_consume( qty, here, pos, carrier );
    }

    // CDF: UPS tail deferred to 4-b2 (Character::consume_ups absent)

    // Consume bio energy
    if( carrier != nullptr && has_flag( flag_USES_BIONIC_POWER ) ) {
        units::energy bio_used = std::min( carrier->get_power_level(), qty );
        carrier->mod_power_level( -bio_used );
        qty -= bio_used;
    }

    return wanted_energy - qty;
}
