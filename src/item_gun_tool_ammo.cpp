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

// CDF: defined below (AUTO-26 O1 / D14 legacy direct-charges guard).
static bool cdf_legacy_direct_charges( const item &it );

bool item::is_reloadable() const
{
    if( ( has_flag( flag_NO_RELOAD ) && !has_flag( flag_VEHICLE ) ) ||
        ( is_gun() && !ammo_default() ) ) {
        // turrets ignore NO_RELOAD flag
        // don't show guns without default ammo defined in reload ui
        return false;
    }

    // CDF: legacy direct-charges guns (D14) have no MAGAZINE pocket, so DDA's pocket loop
    // would skip them; keep BN's answer for them. Remove with cdf_legacy_direct_charges.
    if( cdf_legacy_direct_charges( *this ) && !ammo_types().empty() ) {
        return true;
    }

    for( const item_pocket *pocket : contents.get_all_reloadable_pockets() ) {
        if( pocket->is_type( pocket_type::MAGAZINE_WELL ) ) {
            // CDF: no item::is_magazine_full(); the front item's contents carries it.
            if( pocket->empty() || !pocket->front().contents.is_magazine_full() ) {
                return true;
            }
        } else if( pocket->is_type( pocket_type::MAGAZINE ) ) {
            // Parent aggregate stays correct when the integral mag is full
            // of casings (not counted against ammo_remaining()).
            if( remaining_ammo_capacity() > 0 ) {
                return true;
            }
        } else if( pocket->is_type( pocket_type::CONTAINER ) ) {
            // Container pockets are reloadable only if they are watertight, not full and do not contain non-liquid item
            if( pocket->full( false ) || !pocket->watertight() ) {
                continue;
            }
            if( pocket->empty() || pocket->front().made_of( phase_id::LIQUID ) ) {
                return true;
            }
        }
    }

    for( const item *gunmod : gunmods() ) {
        if( gunmod->is_reloadable() ) {
            return true;
        }
    }

    return false;
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
// Tools left this in 4-b2 (integral tools now get a MAGAZINE pocket from the loader); guns leave it
// at the last 7-R2 group; 9-4 checks that no item still uses it.
// A MAGAZINE_WELL pocket alone does not end the legacy path: BN guns with an optional `magazines`
// list (e.g. sw_619 with a speedloader) get a well yet still store loaded rounds in `charges`
// while magazine_integral() holds.
static bool cdf_legacy_direct_charges( const item &it )
{
    // CDF: tools are out since 4-b2; only guns without a MAGAZINE pocket still keep direct charges.
    if( !it.is_gun() ) {
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
    // as in BN's `if( mag ) return mag->ammo_remaining();`. For a tool that also owns an integral
    // MAGAZINE pocket (smart_phone_music: a magazine_well without a `magazines` list) the parked
    // magazine wins and the integral rounds are ignored, as BN ignored the tool's own charges.
    // CDF: interim precedence (ADV-15 #3); revisit when phone data follows DDA's battery-well model (9-4).
    const bool parked_mag_wins = is_tool() && magazines_current().empty() && magazine_current();
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
    if( is_magazine() && !parked_mag_wins ) {
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

bool item::ammo_sufficient( const Character *carrier, const std::string &method, int qty ) const
{
    auto iter = type->ammo_scale.find( method );
    if( iter != type->ammo_scale.end() ) {
        qty *= iter->second;
    }

    return ammo_sufficient( carrier, qty );
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
    // CDF: a tool with a parked magazine drains that magazine only (see ammo_remaining).
    // CDF: interim precedence (ADV-15 #3), same rule as ammo_remaining; see 9-4.
    const bool parked_mag_wins = is_tool() && contents.magazines_current().empty() && magazine_current();
    if( ( is_magazine() || uses_magazine() ) && !parked_mag_wins ) {
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

// Resolve firing entries for a mode. Base-gun modes are required to have
// explicit entries (see Item_factory::check_definitions), so a missing
// entry here means the mode came from a gunmod (mode_modifier) and
// inherits DEFAULT cost.
static const std::vector<pocket_consumption_entry> *resolve_firing_entries(
    const item &host, const gun_mode_id &mode )
{
    const firing_requirement_set &set = host.type->firing_requirements;
    // Cost comes from the item owning the winning mode-def: a gunmod owning a
    // mode supplies its cost, else inherits host DEFAULT. host[M] is read only
    // for host-owned modes, so a same-id override never splits def from cost.
    bool mod_owns = false;
    if( host.is_gun() ) {
        for( const item *mod : host.gunmods() ) {
            if( mod->type == nullptr || !mod->type->gunmod ) {
                continue;
            }
            if( !mod->type->gunmod->mode_modifier.count( mode ) ) {
                continue;
            }
            // mode_modifier owns the mode; gunmod->firing_requirements (key
            // "mode_firing_requirements") carries its per-pocket cost.
            const firing_requirement_set &ms = mod->type->gunmod->firing_requirements;
            const std::vector<pocket_consumption_entry> *me = ms.for_mode( mode );
            if( me != nullptr && !me->empty() ) {
                return me;
            }
            mod_owns = true; // owns the mode but gave no cost: inherit host DEFAULT
            break;
        }
    }
    if( !mod_owns ) {
        const std::vector<pocket_consumption_entry> *entries = set.for_mode( mode );
        if( entries != nullptr && !entries->empty() ) {
            return entries;
        }
        if( host.type->gun && host.type->gun->modes.count( mode ) ) {
            return nullptr; // base gun mode without explicit entry: validation failure
        }
    }
    return set.for_mode( gun_mode_id( "DEFAULT" ) ); // CDF: gun_mode_DEFAULT absent
}

static int method_scale( const itype *type, const std::string &method )
{
    if( !type || method.empty() ) {
        return 1;
    }
    auto it = type->ammo_scale.find( method );
    return it == type->ammo_scale.end() ? 1 : it->second;
}

int item::effective_qty( const pocket_consumption_entry &e ) const
{
    const item_pocket *p = pocket_by_id( e.pocket );
    const bool is_primary = pocket_is_primary_ammo( p );

    float multiplier = 1.0f;
    int modifier_int = 0;
    // CDF: DDA energy_drain_modifier / energy_drain_multiplier (pocket-battery branch) dropped: no CDF field
    if( is_gun() ) {
        for( const item *mod : gunmods() ) {
            if( !mod->type || !mod->type->gunmod ) {
                continue;
            }
            if( is_primary ) {
                multiplier *= mod->type->gunmod->ammo_to_fire_multiplier;
                modifier_int += mod->type->gunmod->ammo_to_fire_modifier;
            }
        }
    }

    double scaled = static_cast<double>( e.qty ) * multiplier
                    + static_cast<double>( modifier_int );

    // Explicit per-pocket overrides from installed mods (gunmod or toolmod, both
    // through the mod slot). Order-independent: the smallest `set` replaces the
    // resolved value, then the product of all `multiply` scales it.
    bool has_set = false;
    int min_set = 0;
    float mod_multiply = 1.0f;
    // CDF: item::mods() is contents.mods()
    for( const item *mod : contents.mods() ) {
        if( !mod->type || !mod->type->mod ) {
            continue;
        }
        for( const pocket_consumption_mod &cm : mod->type->mod->consumption_mods ) {
            if( cm.pocket != e.pocket ) {
                continue;
            }
            if( cm.set ) {
                min_set = has_set ? std::min( min_set, *cm.set ) : *cm.set;
                has_set = true;
            }
            mod_multiply *= cm.multiply;
        }
    }
    if( has_set ) {
        scaled = min_set;
    }
    scaled *= mod_multiply;

    if( scaled <= 0.0 ) {
        return 0;
    }
    if( scaled < 1.0 ) {
        return 1;
    }
    return static_cast<int>( scaled );
}

static std::string format_entry_name( const item &host, const pocket_consumption_entry &e,
                                      int qty )
{
    const item_pocket *p = host.pocket_by_id( e.pocket );
    if( p == nullptr ) {
        return e.pocket;
    }
    if( const item *mag = p->magazine_current() ) {
        if( const itype *adata = mag->ammo_data() ) {
            return adata->nname( qty );
        }
    }
    const pocket_data *pd = p->get_pocket_data();
    if( pd ) {
        const std::string display = pd->pocket_name.translated();
        if( !display.empty() ) {
            return display;
        }
        // is_null compares to NULL_ID ("null"); the generic optional()
        // JSON loader resets string_id fields to default-constructed
        // empty string when the JSON field is absent, which slips past
        // is_null. Treat empty the same as null so we don't deref a
        // missing itype.
        if( !pd->default_magazine.is_null() && !pd->default_magazine.is_empty() &&
            pd->default_magazine->magazine ) {
            const itype_id &default_ammo = pd->default_magazine->magazine->default_ammo;
            if( !default_ammo.is_null() && !default_ammo.is_empty() && default_ammo->ammo ) {
                return default_ammo->nname( qty );
            }
        }
    }
    std::string fallback = e.pocket;
    std::replace( fallback.begin(), fallback.end(), '_', ' ' );
    return fallback;
}

std::string item::format_consumption_requirements(
    const std::string &method, const gun_mode_id &mode, int uses ) const
{
    const int scale = method_scale( type, method );
    if( !uses_firing_requirements() ) {
        // CDF: DDA get_gun_energy_drain() kJ text dropped (no CDF energy API); charges text kept
        const int qty = ammo_required() * scale * uses;
        return string_format( _( "%d charges" ), qty );
    }

    const std::vector<pocket_consumption_entry> *entries =
        resolve_firing_entries( *this, mode );
    if( entries == nullptr || entries->empty() ) {
        return std::string();
    }
    std::vector<std::string> parts;
    for( const pocket_consumption_entry &e : *entries ) {
        const int qty = effective_qty( e ) * scale * uses;
        if( qty <= 0 ) {
            continue;
        }
        parts.emplace_back( string_format( "%d %s", qty, format_entry_name( *this, e, qty ) ) );
    }
    if( parts.empty() ) {
        return std::string();
    }
    std::string joined;
    bool first = true;
    for( const std::string &part : parts ) {
        if( !first ) {
            joined += " + ";
        }
        first = false;
        joined += part;
    }
    return joined;
}

int item::expected_cost_per_use( const std::string &method ) const
{
    const int scale = method_scale( type, method );
    if( !uses_firing_requirements() ) {
        // ammo_required() already routes guns to ammo_to_fire and tools to
        // charges_per_use, so it's the right scalar for both legacy paths.
        return ammo_required() * scale;
    }
    const std::vector<pocket_consumption_entry> *entries =
        type->firing_requirements.for_mode( gun_mode_id( "DEFAULT" ) ); // CDF: gun_mode_DEFAULT absent
    if( entries == nullptr ) {
        return 0;
    }
    int total = 0;
    for( const pocket_consumption_entry &e : *entries ) {
        total += effective_qty( e );
    }
    return total * scale;
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

// CDF: P3-3-6; DDA item::reload (item_gun_tool_ammo.cpp:3400-3486), targeted path only. The null
// and qty guards live in item::reload, which dispatches here. DDA's item_location becomes
// item &loc, the source is detached (D1) and the weight-cache calls become on_contents_changed().
bool item::reload_targeted( Character &u, item &loc, int qty, int pocket_index )
{
    // Targeted reload: MAGAZINE_WELL swap, or loose ammo into integral MAGAZINE.
    std::vector<item_pocket *> all_pockets = get_pockets(
    []( const item_pocket & ) {
        return true;
    } );
    if( pocket_index >= static_cast<int>( all_pockets.size() ) ) {
        debugmsg( "reload: pocket_index %d out of range on %s", pocket_index, tname() );
        return false;
    }
    item_pocket *target_pocket = all_pockets[pocket_index];
    const bool integral_magazine = target_pocket->is_type( pocket_type::MAGAZINE );
    if( !target_pocket->is_type( pocket_type::MAGAZINE_WELL ) && !integral_magazine ) {
        debugmsg( "reload: pocket_index %d on %s is not a MAGAZINE_WELL or MAGAZINE",
                  pocket_index, tname() );
        return false;
    }
    item *src = &loc; // CDF: DDA re-points an item_location; here a plain pointer
    if( integral_magazine ) {
        // Eject casings before the compatibility gate; a spent shell
        // fills the pocket and would make can_reload_with refuse.
        // CDF: BN casings_handle lambda form (detached_ptr), as in item::reload
        casings_handle( [&u]( detached_ptr<item> &&e ) {
            return u.i_add_or_drop( std::move( e ) );
        } );
    }
    if( !target_pocket->can_reload_with( *src, true ) ) {
        return false;
    }

    // CDF: DDA ammo_from_map / held_by dropped; CDF has no carried-weight cache
    if( src->has_flag( flag_SPEEDLOADER ) || src->has_flag( flag_SPEEDLOADER_CLIP ) ) {
        src = &src->first_ammo();
    }

    if( integral_magazine ) {
        const ammotype loose_type = src->ammo_type();
        const int limit = target_pocket->remaining_ammo_capacity( loose_type );
        if( limit <= 0 ) {
            return false;
        }
        const int take = std::min( { qty, src->charges, limit } );
        if( take <= 0 ) {
            return false;
        }
        // CDF: D1, the pocket takes a detached_ptr; the copy is made with item::spawn( const item & )
        detached_ptr<item> item_copy = item::spawn( *src );
        item_copy->charges = take;
        ret_val<item *> ins = target_pocket->insert_item( std::move( item_copy ) );
        if( !ins.success() ) {
            return false;
        }
        src->charges -= take;
        if( src->charges == 0 ) {
            // CDF: D1, detach() replaces DDA remove_item(); the result is discarded (destroys the empty source)
            src->detach();
        }
        // CDF: replaces DDA invalidate_weight_carried_cache()
        on_contents_changed();
        return true;
    }

    detached_ptr<item> old; // CDF: DDA item magazine_removed
    bool allow_wield = false;
    if( !target_pocket->empty() ) {
        item *existing = target_pocket->magazine_current();
        if( existing != nullptr ) {
            allow_wield = ( !u.is_wielding( *src ) && !u.is_wielding( *this ) );
            old = target_pocket->remove_item( existing );
        }
    }

    detached_ptr<item> incoming = src->detach(); // CDF: D1, DDA copies *ammo then remove_item()
    ret_val<item *> ins = target_pocket->insert_item( std::move( incoming ) );
    if( !ins.success() ) {
        // Restore the ejected magazine on failure.
        if( old ) {
            target_pocket->insert_item( std::move( old ) );
            if( old ) {
                u.i_add( std::move( old ) );
            }
        }
        // CDF: DDA leaves the ammo in place; here it is returned to the carrier
        u.i_add( std::move( incoming ), false );
        return false;
    }
    // CDF: replaces DDA invalidate_weight_carried_cache()
    on_contents_changed();
    if( old ) {
        u.i_add( std::move( old ), true, nullptr, nullptr, true, allow_wield );
    }
    return true;
}

// Per-mode feasibility for multimag. Local pocket stock is non-fungible
// across pocket ids; only the external pool (cable + UPS + bionic) is
// shared, and it's usable only by battery-flavored entries.
static int feasible_uses( const item &host,
                          const std::vector<pocket_consumption_entry> &entries,
                          int requested, int external_pool )
{
    int n_max = INT_MAX;
    struct battery_entry {
        int qty;
        int stock;
    };
    std::vector<battery_entry> battery;
    for( const pocket_consumption_entry &e : entries ) {
        const int q = host.effective_qty( e );
        if( q <= 0 ) {
            continue;
        }
        const int local = host.ammo_remaining_in_pocket( e.pocket );
        const item_pocket *p = host.pocket_by_id( e.pocket );
        if( host.pocket_accepts_battery( p ) ) {
            battery.push_back( { q, local } );
        } else {
            n_max = std::min( n_max, local / q );
        }
    }

    if( battery.empty() ) {
        return std::min( requested, n_max );
    }

    // Pool need(n) = sum_{i past breakpoint} (n*qty_i - stock_i) is
    // piecewise linear; walk breakpoints in ascending stock_i / qty_i order.
    std::sort( battery.begin(), battery.end(),
    []( const battery_entry & a, const battery_entry & b ) {
        return static_cast<int64_t>( a.stock ) * b.qty <
               static_cast<int64_t>( b.stock ) * a.qty;
    } );

    int qty_sum_past = 0;
    int stock_sum_past = 0;
    int n = 0;
    bool exhausted = false;
    for( const battery_entry &b : battery ) {
        const int n_break = b.stock / b.qty;
        if( qty_sum_past > 0 ) {
            const int n_max_segment =
                ( external_pool + stock_sum_past ) / qty_sum_past;
            if( n_max_segment < n_break ) {
                n = std::max( n, n_max_segment );
                exhausted = true;
                break;
            }
            n = std::max( n, n_break );
        } else {
            n = std::max( n, n_break );
        }
        qty_sum_past += b.qty;
        stock_sum_past += b.stock;
    }
    if( !exhausted && qty_sum_past > 0 ) {
        const int n_after = ( external_pool + stock_sum_past ) / qty_sum_past;
        n = std::max( n, n_after );
    }
    return std::min( { requested, n_max, n } );
}

int item::available_cable_charges( map & /* here */ ) const
{
    // CDF: cable link API absent (no has_link_data, link or battery_left); stub until the cable owner rules (AUTO-26 O2)
    return 0;
}

int item::available_ups_charges( const Character * /* carrier */ ) const
{
    // CDF: Character::available_ups() absent until 4-b2
    return 0;
}

int item::available_bionic_charges( const Character *carrier ) const
{
    if( !carrier || !has_flag( flag_USES_BIONIC_POWER ) ) {
        return 0;
    }
    return units::to_kilojoule( carrier->get_power_level() );
}

// CDF: until 3-4d, legacy ammo part of DDA shots_remaining 1424-1427 only (energy part 1428-1430 dropped)
static int legacy_shots_interim( const item &it, map &here, const Character *carrier )
{
    int ret = 1000;
    if( it.ammo_required() ) {
        ret = std::min( it.ammo_remaining_linked( here, carrier ) / it.ammo_required(), ret );
    }
    return ret;
}

int item::tool_uses_remaining( map &here, const Character *carrier ) const
{
    if( !uses_firing_requirements() ) {
        // CDF: until 3-4d
        return legacy_shots_interim( *this, here, carrier );
    }
    const std::vector<pocket_consumption_entry> *entries =
        type->firing_requirements.for_mode( gun_mode_id( "DEFAULT" ) ); // CDF: gun_mode_DEFAULT absent
    if( entries == nullptr || entries->empty() ) {
        return 0;
    }
    const int pool = available_cable_charges( here )
                     + available_ups_charges( carrier )
                     + available_bionic_charges( carrier );
    return feasible_uses( *this, *entries, INT_MAX, pool );
}

int item::tool_uses_remaining_local() const
{
    if( !uses_firing_requirements() ) {
        return 0;
    }
    const std::vector<pocket_consumption_entry> *entries =
        type->firing_requirements.for_mode( gun_mode_id( "DEFAULT" ) ); // CDF: gun_mode_DEFAULT absent
    if( entries == nullptr || entries->empty() ) {
        return 0;
    }
    return feasible_uses( *this, *entries, INT_MAX, /*external_pool=*/0 );
}

int item::feasible_tool_uses( int external_pool ) const
{
    if( !uses_firing_requirements() ) {
        return 0;
    }
    const std::vector<pocket_consumption_entry> *entries =
        type->firing_requirements.for_mode( gun_mode_id( "DEFAULT" ) ); // CDF: gun_mode_DEFAULT absent
    if( entries == nullptr || entries->empty() ) {
        return 0;
    }
    return feasible_uses( *this, *entries, INT_MAX, std::max( 0, external_pool ) );
}

namespace
{
// Multimag drain in cable -> local -> UPS -> bionic order; applies
// external deltas exactly once. Returns shots / uses actually consumed.
// TODO(multimag): cable-before-local order can misallocate when 2+ battery
// pockets share cable and have mixed local stocks. Drain local first per
// pocket, then bill external for the residual.
int multimag_drain( item &host, const std::vector<pocket_consumption_entry> &entries,
                    int requested, map &here, const tripoint_bub_ms &pos,
                    Character *carrier )
{
    const int cable_initial = host.available_cable_charges( here );
    const int ups_initial = host.available_ups_charges( carrier );
    const int bionic_initial = host.available_bionic_charges( carrier );
    const int pool = cable_initial + ups_initial + bionic_initial;

    const int allowed = feasible_uses( host, entries, requested, pool );
    if( allowed <= 0 ) {
        return 0;
    }

    int cable_left = cable_initial;
    int ups_left = ups_initial;
    int bionic_left = bionic_initial;

    for( const pocket_consumption_entry &e : entries ) {
        int qty = host.effective_qty( e ) * allowed;
        if( qty <= 0 ) {
            continue;
        }
        const item_pocket *p = host.pocket_by_id( e.pocket );
        const bool battery = host.pocket_accepts_battery( p );
        if( battery ) {
            const int from_cable = std::min( qty, cable_left );
            cable_left -= from_cable;
            qty        -= from_cable;
        }
        if( qty > 0 ) {
            const int from_local = host.ammo_consume_in_pocket( e.pocket, qty, here, pos );
            qty -= from_local;
        }
        if( battery && qty > 0 ) {
            const int from_ups = std::min( qty, ups_left );
            ups_left -= from_ups;
            qty      -= from_ups;
            const int from_bionic = std::min( qty, bionic_left );
            bionic_left -= from_bionic;
            qty         -= from_bionic;
        }
        if( qty > 0 ) {
            debugmsg( "consume_shots: shortfall on pocket %s after feasibility check (%s)",
                      e.pocket, host.tname() );
        }
    }

    const int bionic_used = bionic_initial - bionic_left;
    // CDF: cable link API absent (AUTO-26 O2)
    if( carrier ) {
        // CDF: Character::consume_ups absent, job 4-b2 (AUTO-30 OD-4)
        if( bionic_used > 0 ) {
            carrier->mod_power_level(
                -units::from_kilojoule( static_cast<int64_t>( bionic_used ) ) );
        }
    }
    return allowed;
}
} // namespace

int item::consume_shots( const gun_mode_id &mode, int shots, map &here,
                         const tripoint_bub_ms &pos, Character *carrier )
{
    if( shots <= 0 ) {
        return 0;
    }
    if( firing_mode_blocked( mode ) ) {
        const gun_mode gm = gun_get_mode( mode );
        if( gm && gm.target != this ) {
            debugmsg( "multimag: aux-gunmod mode %s fired on host %s is unsupported "
                      "(would mis-bill parent pockets)", mode.str(), tname() );
        }
        return 0;
    }
    if( uses_firing_requirements() ) {
        const std::vector<pocket_consumption_entry> *entries =
            resolve_firing_entries( *this, mode );
        if( entries == nullptr || entries->empty() ) {
            debugmsg( "consume_shots: %s has no firing_requirements entries for mode %s",
                      tname(), mode.str() );
            return 0;
        }
        return multimag_drain( *this, *entries, shots, here, pos, carrier );
    }
    // Legacy path: ammo_consume + energy_consume per shot.
    const int required_ammo = ammo_required() * shots;
    if( required_ammo > 0 ) {
        const int consumed_ammo = ammo_consume( required_ammo, here, pos, carrier );
        if( consumed_ammo != required_ammo ) {
            return 0;
        }
    }
    // CDF: energy tail, job 8-1d (get_gun_energy_drain absent)
    return shots;
}

int item::consume_one_shot( map &here, const tripoint_bub_ms &pos, Character *carrier )
{
    // CDF: gun_mode_DEFAULT absent
    const gun_mode_id mode = is_gun() ? gun_get_mode_id() : gun_mode_id( "DEFAULT" );
    return consume_shots( mode, 1, here, pos, carrier );
}

bool item::firing_mode_blocked( const gun_mode_id &mode ) const
{
    if( !is_gun() ) {
        return false;
    }
    // CDF: hide_modes loop dropped (islot_gunmod::hide_modes absent, 3-7 OD-7)
    const std::map<gun_mode_id, gun_mode> modes = gun_all_modes();
    if( modes.empty() ) {
        return true; // every mode hidden by a gunmod: gun is unfireable
    }
    // Aux gunmod modes target the aux item, not this host; billing them here
    // would drain the parent's pockets, so block.
    const auto it = modes.find( mode );
    if( it != modes.end() && it->second.target != this && uses_firing_requirements() ) {
        return true;
    }
    return false;
}

int item::consume_tool_uses( int uses, map &here, const tripoint_bub_ms &pos,
                             Character *carrier )
{
    if( uses <= 0 ) {
        return 0;
    }
    if( uses_firing_requirements() ) {
        const std::vector<pocket_consumption_entry> *entries =
            type->firing_requirements.for_mode( gun_mode_id( "DEFAULT" ) ); // CDF: gun_mode_DEFAULT absent
        if( entries == nullptr || entries->empty() ) {
            debugmsg( "consume_tool_uses: %s has no DEFAULT firing_requirements entries",
                      tname() );
            return 0;
        }
        return multimag_drain( *this, *entries, uses, here, pos, carrier );
    }
    // Legacy path: ammo_consume(charges_to_use * uses).
    // CDF: charges_to_use() is 1 for non-tools (3-4b OD-10 precedent)
    if( !is_tool() ) {
        return uses;
    }
    const int per_use = type ? type->charges_to_use() : 0;
    if( per_use <= 0 ) {
        return uses;
    }
    const int required = per_use * uses;
    const int consumed = ammo_consume( required, here, pos, carrier );
    if( consumed != required ) {
        return consumed / per_use;
    }
    return uses;
}

int item::activation_consume( int qty, const tripoint_bub_ms &pos, Character *carrier )
{
    if( uses_firing_requirements() ) {
        return consume_tool_uses( qty, get_map(), pos, carrier );
    }
    return ammo_consume( qty * ammo_required(), pos, carrier );
}
