#include "item.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

// CDF: LINKUP1b; get_avatar().sees() in item::reset_link().
#include "active_tile_data.h"
#include "active_tile_data_def.h"
#include "avatar.h"
#include "character.h"
#include "enums.h"
#include "flag.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
// CDF: LINKUP1a; item::link_to()/update_link_traits() read the link_up actor.
#include "iuse_actor.h"
// CDF: needed for the free get_map() used as the dummy map argument below.
#include "map/map.h"
// CDF: LINKUP1b; add_msg() in item::reset_link().
#include "messages.h"
// CDF: LINKUP1a; vehicle::battery_power_level()/avail_linkable_part() for the link branches.
#include "vehicle/vehicle.h"
// CDF: LINKUP1b; vehicle_part, vpart_info::get_all() and the get_any_parts() range in the
// two-vehicle link_to() branch.
#include "vehicle/veh_type.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vpart_range.h"
// CDF: LINKUP1a; optional_vpart_position::mount() in item::link_to().
#include "vehicle/vpart_position.h"

// CDF: LINKUP1b; the battery ammo item used when a linked battery is charged or discharged.
static const itype_id itype_battery( "battery" );

// CDF: LINKUP1a, DDA item_gun_tool_ammo.cpp:123
static constexpr float MIN_LINK_EFFICIENCY = 0.001f;

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

bool item::is_reloadable() const
{
    if( ( has_flag( flag_NO_RELOAD ) && !has_flag( flag_VEHICLE ) ) ||
        ( is_gun() && !ammo_default() ) ) {
        // turrets ignore NO_RELOAD flag
        // don't show guns without default ammo defined in reload ui
        return false;
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
    // CDF: a magazine parked outside a well (MIGRATION) still counts,
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

    // CDF: cable link block (DDA 1474-1484). LINKUP1b: the linked vehicle's whole cable graph
    // counts, as in DDA (DDA vehicle.cpp:5560-5573).
    if( include_linked && link_length() >= 0 && link().efficiency >= MIN_LINK_EFFICIENCY ) {
        if( link().t_veh ) {
            ret += link().t_veh->connected_battery_power_level().first;
        } else {
            const optional_vpart_position vp = here.veh_at( link().t_abs_pos );
            if( vp ) {
                ret += vp->vehicle().connected_battery_power_level().first;
            }
        }
    }

    // Non ammo using item that uses charges (DDA 1486-1489).
    // CDF: is_ammo() excluded: BN returned 0 for an ammo item's own charges (iteminfo "Ammunition type").
    if( ammo.empty() && !is_ammo() ) {
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
    // CDF: cable link block (DDA 1722-1726). LINKUP1b: whole connected graph.
    if( include_linked && has_link_data() ) {
        return link().t_veh ? link().t_veh->connected_battery_power_level().second : 0;
    }

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
    // CDF: DDA's single energy term is split: get_gun_ups_drain() is an int count of UPS charges (AUTO-30 OD-2), get_gun_energy_drain() is DDA's
    return ammo_required() > 0 || get_gun_energy_drain() > 0_kJ || get_gun_ups_drain() > 0 || uses_firing_requirements();
}

units::energy item::get_gun_energy_drain() const
{
    units::energy draincount = 0_kJ;
    if( type->gun ) {
        units::energy modifier = 0_kJ;
        float multiplier = 1.0f;
        for( const item *mod : gunmods() ) {
            modifier += mod->type->gunmod->energy_drain_modifier;
            multiplier *= mod->type->gunmod->energy_drain_multiplier;
        }
        draincount = ( type->gun->energy_drain * multiplier ) + modifier;
    }
    return draincount;
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

    return shots_remaining( here, carrier ) >= qty;
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

    // CDF: cable link discharge (DDA 1861-1870). LINKUP1a.
    if( has_link_data() ) {
        if( link().t_veh && link().efficiency >= MIN_LINK_EFFICIENCY ) {
            // CDF: CDF's discharge_battery( int, bool ) takes no map (DDA's map-taking form is
            // newer) and returns the unfulfilled remainder, like DDA's.
            qty = link().t_veh->discharge_battery( qty, true );
        } else {
            const optional_vpart_position vp = here.veh_at( link().t_abs_pos );
            if( vp ) {
                qty = vp->vehicle().discharge_battery( qty, true );
            }
        }
    }

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
    const bool is_battery = pocket_accepts_battery( p );

    float multiplier = 1.0f;
    int modifier_int = 0;
    units::energy modifier_energy = 0_kJ;
    if( is_gun() ) {
        for( const item *mod : gunmods() ) {
            if( !mod->type || !mod->type->gunmod ) {
                continue;
            }
            if( is_primary ) {
                multiplier *= mod->type->gunmod->ammo_to_fire_multiplier;
                modifier_int += mod->type->gunmod->ammo_to_fire_modifier;
            } else if( is_battery ) {
                multiplier *= mod->type->gunmod->energy_drain_multiplier;
                modifier_energy += mod->type->gunmod->energy_drain_modifier;
            }
        }
    }

    double scaled = static_cast<double>( e.qty ) * multiplier
                    + static_cast<double>( modifier_int )
                    + units::to_kilojoule( modifier_energy );

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
        if( type && type->gun ) {
            const int qty = ammo_required() * scale * uses;
            const units::energy energy = get_gun_energy_drain() * scale * uses;
            std::string out = string_format( _( "%d charges" ), qty );
            if( energy > 0_kJ ) {
                out += string_format( _( ", %d kJ" ), units::to_kilojoule( energy ) );
            }
            return out;
        }
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
    // CDF: DDA item::is_battery() body (magazine with battery capacity) inlined here; CDF is_battery() (item.cpp ~8004) stays type-only (open question 1)
    const bool battery_cell = is_magazine() && ammo_capacity( ammo_battery ) > 0;
    if( is_battery() || battery_cell || fuel_efficiency >= 0 ) {
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

int item::tool_uses_remaining( map &here, const Character *carrier ) const
{
    if( !uses_firing_requirements() ) {
        return shots_remaining( here, carrier );
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

int item::shots_remaining( const map &here, const Character *carrier ) const
{
    if( uses_firing_requirements() ) {
        const gun_mode_id mode = is_gun() ? gun_get_mode_id() :
                                 gun_mode_id( "DEFAULT" ); // CDF: gun_mode_DEFAULT absent
        if( firing_mode_blocked( mode ) ) {
            return 0;
        }
        const std::vector<pocket_consumption_entry> *entries =
            resolve_firing_entries( *this, mode );
        if( entries == nullptr || entries->empty() ) {
            return 0;
        }
        map &mut_here = const_cast<map &>( here );
        const int pool = available_cable_charges( mut_here )
                         + available_ups_charges( carrier )
                         + available_bionic_charges( carrier );
        return feasible_uses( *this, *entries, INT_MAX, pool );
    }
    int ret = 1000; // Arbitrary large number for things that do not require ammo.
    if( ammo_required() ) {
        ret = std::min( ammo_remaining_linked( here,  carrier ) / ammo_required(), ret );
    }
    if( get_gun_energy_drain() > 0_kJ ) {
        ret = std::min( static_cast<int>( energy_remaining( carrier ) / get_gun_energy_drain() ), ret );
    }
    return ret;
}

bool item::uses_energy() const
{
    if( is_vehicle_battery() ) {
        return true;
    }
    for( const item *mag : magazines_current() ) {
        if( mag && mag->uses_energy() ) {
            return true;
        }
    }
    return has_flag( flag_USES_BIONIC_POWER ) ||
           has_flag( flag_USE_UPS ) ||
           ( is_magazine() && ammo_capacity( ammo_battery ) > 0 );
}

bool item::is_chargeable() const
{
    if( !uses_energy() ) {
        return false;
    }
    // Bionic-powered items have ammo_capacity tied to player bionic storage,
    // so they can't actually receive charge unless they also carry a magazine.
    if( has_flag( flag_USES_BIONIC_POWER ) && magazines_current().empty() ) {
        return false;
    }
    // Compare battery-flavored stock vs battery-flavored capacity so a
    // mixed projectile + battery multimag tool doesn't read full just
    // because its non-battery wells are loaded.
    int battery_charges = 0;
    int battery_capacity = 0;
    for( const item *mag : magazines_current() ) {
        if( mag && mag->ammo_capacity( ammo_battery ) > 0 ) {
            battery_charges += mag->ammo_remaining();
            battery_capacity += mag->ammo_capacity( ammo_battery );
        }
    }
    if( battery_capacity == 0 ) {
        battery_capacity = ammo_capacity( ammo_battery );
        battery_charges = ammo_remaining();
    }
    return battery_charges < battery_capacity;
}

units::energy item::energy_remaining( const Character *carrier ) const
{
    return energy_remaining( carrier, false );
}

units::energy item::energy_remaining( const Character *carrier, bool ignoreExternalSources ) const
{
    units::energy ret = 0_kJ;

    // Sum across every loaded MAGAZINE_WELL; mixed projectile+battery
    // multimag items would otherwise read only the first well's energy.
    for( const item *mag : magazines_current() ) {
        if( mag ) {
            ret += mag->energy_remaining( carrier );
        }
    }

    if( !ignoreExternalSources ) {

        // Future energy based batteries
        if( is_vehicle_battery() ) {
            ret += energy;
        }

        // Power from bionic
        if( carrier != nullptr && has_flag( flag_USES_BIONIC_POWER ) ) {
            ret += carrier->get_power_level();
        }

        // CDF: UPS tail deferred to 4-b2 (Character::available_ups absent)
    };

    // Battery(ammo) contained within
    if( is_magazine() ) {
        ret += energy;
        for( const item *e : contents.all_items_top( pocket_type::MAGAZINE ) ) {
            if( e->ammo_type() == ammo_battery ) {
                ret += units::from_kilojoule( static_cast<std::int64_t>( e->charges ) );
            }
        }
    }

    return ret;
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
    const units::energy required_energy = get_gun_energy_drain() * shots;
    if( required_ammo > 0 ) {
        const int consumed_ammo = ammo_consume( required_ammo, here, pos, carrier );
        if( consumed_ammo != required_ammo ) {
            return 0;
        }
    }
    if( required_energy > 0_kJ ) {
        const units::energy drained = energy_consume( required_energy, &here, pos, carrier );
        if( drained < required_energy ) {
            return 0;
        }
    }
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
    // A mode a gunmod hides must not fire even when other modes survive.
    for( const item *mod : gunmods() ) {
        if( mod->type && mod->type->gunmod && mod->type->gunmod->hide_modes.count( mode ) ) {
            return true;
        }
    }
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

// CDF: LINKUP1a. DDA item_gun_tool_ammo.cpp:3610-3672 and 3674-3693 (the two links that only
// need the vehicle/part the caller already picked), 3850-3905 (traits and lengths) and 3014
// (item::cables). The two-vehicle cable connection (DDA :3695-3848), process_link,
// charge_linked_batteries and reset_link arrive with LINKUP1b.

item::link_data &item::link()
{
    if( link_ ) {
        return *link_;
    }
    if( !can_link_up() ) {
        debugmsg( "%s is creating link_data even though it doesn't have a link_up action!  can_link_up() should be checked before using link().",
                  tname() );
    }
    return *( link_ = cata::make_value<item::link_data>() );
}

const item::link_data &item::link() const
{
    if( link_ ) {
        return *link_;
    }
    if( !can_link_up() ) {
        debugmsg( "%s is creating link_data even though it doesn't have a link_up action!  can_link_up() should be checked before using link().",
                  tname() );
    }
    return *( link_ = cata::make_value<item::link_data>() );
}

bool item::has_link_data() const
{
    return !!link_;
}

bool item::can_link_up() const
{
    const bool can_link = has_link_data() || type->can_use( "link_up" );
    if( can_link && !get_use( "link_up" ) ) {
        debugmsg( "can_link_up() found no link_up use function for %s. Most likely missing link_up in item transform",
                  type_name() );
        return false;
    }
    return can_link;
}

bool item::link_has_state( link_state state ) const
{
    return !has_link_data() ? state == link_state::no_link :
           link_->source == state || link_->target == state;
}

bool item::link_has_states( link_state source, link_state target ) const
{
    return !has_link_data() ? source == link_state::no_link && target == link_state::no_link :
           ( link_->source == source || link_->source == link_state::automatic ) &&
           ( link_->target == target || link_->target == link_state::automatic );
}

bool item::has_no_links() const
{
    return !has_link_data() ? true :
           link_->source == link_state::no_link && link_->target == link_state::no_link;
}

std::string item::link_name() const
{
    return has_flag( flag_CABLE_SPOOL ) ? label( 1 ) : string_format( _( " %s's cable" ), label( 1 ) );
}

namespace
{
// CDF: GRIDLINK1. Installs the cable as a vehicle part that joins `veh` to the distribution-grid
// connector at `grid_pos`, the way the BN grid cable did after both cable ends were set
// (5123360d7b^:src/iuse.cpp:8711-8726): a POWER_TRANSFER part with the `targets_grid` flag, the
// connector position in both target points, and the vehicle listed in the connector's
// connected_vehicles. The grid backend (distribution_graph/distribution_grid) is unchanged.
auto link_grid_vehicle( item &cable, vehicle &veh, const tripoint_mnt_veh &mount,
                        const tripoint_abs_ms &grid_pos ) -> ret_val<void>
{
    vehicle_connector_tile *connector = active_tiles::furn_at<vehicle_connector_tile>( grid_pos );
    if( connector == nullptr ) {
        return ret_val<void>::make_failure( _( "There's no grid connector there." ) );
    }

    if( rl_dist( grid_pos, veh.abs_ms_location() + veh.coord_translate( mount ) ) >
        cable.link().max_length ) {
        return ret_val<void>::make_failure( _( "The %s can't stretch that far!" ), cable.type_name() );
    }

    const itype_id item_id = cable.typeId();
    vpart_id vpid = vpart_id::NULL_ID();
    for( const vpart_info &e : vpart_info::get_all() ) { // CDF: DDA vehicles::parts::get_all()
        if( e.item == item_id ) {
            vpid = e.id;
            break;
        }
    }
    if( vpid.is_null() ) {
        debugmsg( "item %s is not base item of any vehicle part!", item_id.c_str() );
        return ret_val<void>::make_failure();
    }
    // CDF: CDF's can_mount() returns bool (DDA returns ret_val<void>).
    if( !veh.can_mount( mount, vpid ) ) {
        return ret_val<void>::make_failure( _( "You can't attach the %1$s there." ), cable.type_name() );
    }

    // The cable stops being an item link: drop any linked_flag left on the part it was attached to.
    for( const int idx : veh.parts_at_relative( mount, false ) ) {
        veh.part( idx ).remove_flag( vehicle_part::linked_flag );
    }

    vehicle_part cable_part( vpid, mount, item::spawn( cable ), &veh );
    cable_part.target.first = grid_pos;
    cable_part.target.second = grid_pos;
    cable_part.set_flag( vehicle_part::targets_grid );
    veh.install_part( mount, std::move( cable_part ) );
    veh.precalc_mounts( 1, veh.pivot_rotation[1], veh.pivot_anchor[1] );

    if( std::ranges::find( connector->connected_vehicles, veh.abs_ms_location() ) ==
        connector->connected_vehicles.end() ) {
        connector->connected_vehicles.emplace_back( veh.abs_ms_location() );
    }

    cable.reset_link();
    //~ %1$s - vehicle name
    return ret_val<void>::make_success( _( "You connect the %1$s to the electric grid." ),
                                        veh.name );
}
} // namespace

// CDF: GRIDLINK1. The grid end of a link-up cable (BN's grid-cable state_grid entry).
auto item::link_to( const tripoint_abs_ms &grid_pos, link_state link_type ) -> ret_val<void>
{
    if( !can_link_up() ) {
        return ret_val<void>::make_failure( _( "The %s doesn't have a cable!" ), type_name() );
    }
    // CDF: GRIDLINK1. Only the distribution-grid end is routed here; the vehicle ends use the
    // DDA overloads above.
    if( link_type != link_state::vehicle_grid ) {
        return ret_val<void>::make_failure();
    }
    const link_up_actor *it_actor = static_cast<const link_up_actor *>
                                    ( get_use( "link_up" )->get_actor_ptr() );
    if( it_actor->targets.find( link_state::vehicle_grid ) == it_actor->targets.end() ) {
        return ret_val<void>::make_failure( _( "The %s can't connect to that." ), type_name() );
    }
    if( active_tiles::furn_at<vehicle_connector_tile>( grid_pos ) == nullptr ) {
        return ret_val<void>::make_failure( _( "There's no grid connector there." ) );
    }

    // The other end already holds a vehicle: this end closes the link.
    if( link_has_state( link_state::vehicle_port ) ||
        link_has_state( link_state::vehicle_battery ) ) {
        vehicle *veh = link().t_veh ? link().t_veh : vehicle::find_vehicle( link().t_abs_pos );
        if( veh == nullptr ) {
            return ret_val<void>::make_failure( _( "There's no vehicle there." ) );
        }
        return link_grid_vehicle( *this, *veh, link().t_mount, grid_pos );
    }

    if( !has_no_links() ) {
        // CDF: GRIDLINK1. BN let the grid end pair with a CBM or a vehicle only; the DDA link
        // model's UPS/solar pack ends stay a one-cable-to-one-device link.
        return ret_val<void>::make_failure( _( "The %s is already connected to something else." ),
                                            type_name() );
    }

    // Free end: remember the connector, the vehicle end closes the link later.
    link().source = link_state::vehicle_grid;
    link().target = link_state::no_link;
    link().t_abs_pos = grid_pos;
    link().t_mount = tripoint_mnt_veh::zero();
    link().s_abs_pos = tripoint_abs_ms::min();
    update_link_traits();
    //~ %1$s - cable name
    return ret_val<void>::make_success( _( "You attach the %1$s to the grid connector." ),
                                        type_name() );
}

ret_val<void> item::link_to( const optional_vpart_position &linked_vp, link_state link_type )
{
    return linked_vp ? link_to( linked_vp->vehicle(), linked_vp->mount(), link_type ) :
           ret_val<void>::make_failure();
}

ret_val<void> item::link_to( const optional_vpart_position &first_linked_vp,
                             const optional_vpart_position &second_linked_vp, link_state link_type )
{
    if( !first_linked_vp || !second_linked_vp ) {
        return ret_val<void>::make_failure();
    }
    // Link up the second vehicle first so, if it's a tow cable, the first vehicle will tow the second.
    ret_val<void> result = link_to( second_linked_vp->vehicle(), second_linked_vp->mount(),
                                    link_type );
    if( !result.success() ) {
        return result;
    }
    return link_to( first_linked_vp->vehicle(), first_linked_vp->mount(), link_type );
}

ret_val<void> item::link_to( vehicle &veh, const tripoint_mnt_veh &mount, link_state link_type )
{
    if( !can_link_up() ) {
        return ret_val<void>::make_failure( _( "The %s doesn't have a cable!" ), type_name() );
    }

    // First, check if the desired link is actually possible.

    if( link_type == link_state::vehicle_tow ) {
        if( veh.has_tow_attached() || veh.is_towed() || veh.is_towing() ) {
            return ret_val<void>::make_failure( _( "That vehicle already has a tow-line attached." ) );
            // CDF: CDF's is_external_part() takes a bub position, not a mount.
        } else if( !veh.is_external_part( veh.bub_ms_location() + veh.coord_translate( mount ) ) ) {
            return ret_val<void>::make_failure( _( "You can't attach a tow-line to an internal part." ) );
        } else {
            const int part_at = veh.part_at( veh.coord_translate( mount ) );
            // CDF: DDA tests carried_stack (racked items); CDF carries vehicles, tracked by carry_names.
            if( part_at != -1 && !veh.part( part_at ).carry_names.empty() ) {
                return ret_val<void>::make_failure( _( "You can't attach a tow-line to a racked part." ) );
            }
        }
    } else {
        const link_up_actor *it_actor = static_cast<const link_up_actor *>
                                        ( get_use( "link_up" )->get_actor_ptr() );
        const bool can_link_port = ( link_type == link_state::vehicle_port ||
                                     link_type == link_state::automatic ) &&
                                   it_actor->targets.find( link_state::vehicle_port ) != it_actor->targets.end() &&
                                   veh.avail_linkable_part( mount, true ) != -1;

        const bool can_link_battery = ( link_type == link_state::vehicle_battery ||
                                        link_type == link_state::automatic ) &&
                                      it_actor->targets.find( link_state::vehicle_battery ) != it_actor->targets.end() &&
                                      veh.avail_linkable_part( mount, false ) != -1;

        if( !can_link_port && !can_link_battery ) {
            return ret_val<void>::make_failure( _( "The %s can't connect to that." ), type_name() );
        } else if( link_type == link_state::automatic ) {
            link_type = can_link_port ? link_state::vehicle_port : link_state::vehicle_battery;
        }
    }

    // CDF: GRIDLINK1. The cable's other end is on a distribution-grid connector: this end joins
    // the vehicle to that grid instead of to a second vehicle.
    if( link_has_state( link_state::vehicle_grid ) ) {
        return link_grid_vehicle( *this, veh, mount, link().t_abs_pos );
    }

    const bool bio_link = link_has_states( link_state::no_link, link_state::bio_cable );
    if( bio_link || has_no_links() ) {

        // No link exists, so start a new link to a vehicle/appliance.

        link().source = bio_link ? link_state::bio_cable : link_state::no_link;
        link().target = link_type;
        link().t_veh = &veh;
        // CDF: CDF's vehicle has no pos_abs(); bub_to_abs() is the map helper (DDA vehicle::pos_abs()).
        link().t_abs_pos = bub_to_abs( veh.bub_ms_location() );
        link().t_mount = mount;
        link().s_abs_pos = tripoint_abs_ms::min(); // Forces item to check length during process_link

        update_link_traits();
        return ret_val<void>::make_success();
    }

    // There's already a link, so connect the previous vehicle/appliance to the new one.

    if( !link_has_state( link_state::no_link ) ) {
        debugmsg( "Failed to connect the %s, both ends are already connected!", tname() );
        return ret_val<void>::make_failure();
    }
    if( !link().t_veh ) {
        vehicle *found_veh = vehicle::find_vehicle( link().t_abs_pos );
        if( found_veh ) {
            link().t_veh = found_veh;
        } else {
            debugmsg( "Failed to connect the %s, it lost its vehicle pointer!", tname() );
            return ret_val<void>::make_failure();
        }
    }
    vehicle *prev_veh = link().t_veh;
    if( prev_veh == &veh ) {
        return ret_val<void>::make_failure( _( "You cannot connect the %s to itself." ), prev_veh->name );
    }

    // Prepare target tripoints for the cable parts that'll be added to the selected/previous vehicles
    const std::pair<tripoint_abs_ms, tripoint_abs_ms> prev_part_target = std::make_pair(
                veh.abs_ms_location() + veh.coord_translate( mount ),
                veh.abs_ms_location() );
    const std::pair<tripoint_abs_ms, tripoint_abs_ms> sel_part_target = std::make_pair(
                link().t_abs_pos + prev_veh->coord_translate( link().t_mount ),
                link().t_abs_pos );

    for( const vpart_reference &vpr : prev_veh->get_any_parts( "POWER_TRANSFER" ) ) {
        if( vpr.part().target.first == prev_part_target.first &&
            vpr.part().target.second == prev_part_target.second ) {
            return ret_val<void>::make_failure( _( "The %1$s and %2$s are already connected." ),
                                                veh.name, prev_veh->name );
        }
    }

    if( rl_dist( prev_part_target.first, sel_part_target.first ) > link().max_length ) {
        return ret_val<void>::make_failure( _( "The %s can't stretch that far!" ), type_name() );
    }

    const itype_id item_id = typeId();
    vpart_id vpid = vpart_id::NULL_ID();
    for( const vpart_info &e : vpart_info::get_all() ) { // CDF: DDA vehicles::parts::get_all()
        if( e.item == item_id ) {
            vpid = e.id;
            break;
        }
    }

    if( vpid.is_null() ) {
        debugmsg( "item %s is not base item of any vehicle part!", item_id.c_str() );
        return ret_val<void>::make_failure();
    }

    const tripoint_mnt_veh prev_mount = link().t_mount;

    // CDF: vehicle::can_mount() returns bool (DDA returns ret_val<void>).
    if( !prev_veh->can_mount( prev_mount, vpid ) || !veh.can_mount( mount, vpid ) ) {
        //~ %1$s - cable name
        return ret_val<void>::make_failure( _( "You can't attach the %1$s there." ), type_name() );
    }

    vehicle_part prev_veh_part( vpid, prev_mount, item::spawn( *this ), prev_veh );
    prev_veh_part.target.first = prev_part_target.first;
    prev_veh_part.target.second = prev_part_target.second;
    prev_veh->install_part( prev_mount, std::move( prev_veh_part ) );
    prev_veh->precalc_mounts( 1, prev_veh->pivot_rotation[1], prev_veh->pivot_anchor[1] );

    vehicle_part sel_veh_part( vpid, mount, item::spawn( *this ), &veh );
    sel_veh_part.target.first = sel_part_target.first;
    sel_veh_part.target.second = sel_part_target.second;
    veh.install_part( mount, std::move( sel_veh_part ) );
    veh.precalc_mounts( 1, veh.pivot_rotation[1], veh.pivot_anchor[1] );

    if( link_type == link_state::vehicle_tow ) {
        if( link().source == link_state::vehicle_tow ) {
            veh.tow_data.set_towing( prev_veh, &veh ); // Previous vehicle is towing.
        } else {
            veh.tow_data.set_towing( &veh, prev_veh ); // Previous vehicle is being towed.
        }
    }

    if( typeId() != itype_id( "power_cord" ) ) {
        // Remove linked_flag from attached parts - the just-added cable vehicle parts do the same thing.
        reset_link();
    }
    //~ %1$s - first vehicle name, %2$s - second vehicle name - %3$s - cable name,
    return ret_val<void>::make_success( _( "You connect the %1$s and %2$s with the %3$s." ),
                                        prev_veh->disp_name(), veh.disp_name(), type_name() );
}

void item::update_link_traits()
{
    if( !can_link_up() ) {
        return;
    }

    const link_up_actor *it_actor = static_cast<const link_up_actor *>
                                    ( get_use( "link_up" )->get_actor_ptr() );
    link().max_length = it_actor->cable_length == -1 ? type->maximum_charges() : it_actor->cable_length;
    link().efficiency = it_actor->efficiency < MIN_LINK_EFFICIENCY ? 0.0f : it_actor->efficiency;
    // reset s_abs_pos to force a length check in process_link
    link().s_abs_pos = tripoint_abs_ms::min();
    link().last_processed = calendar::turn;

    for( const item *cable : cables() ) {
        if( !cable->can_link_up() ) {
            continue;
        }
        const link_up_actor *actor = static_cast<const link_up_actor *>
                                     ( cable->get_use( "link_up" )->get_actor_ptr() );
        link().max_length += actor->cable_length == -1 ? cable->type->maximum_charges() :
                             actor->cable_length;
        link().efficiency = link().efficiency < MIN_LINK_EFFICIENCY ? 0.0f :
                            link().efficiency * actor->efficiency;
    }

    if( link().efficiency >= MIN_LINK_EFFICIENCY ) {
        link().charge_rate = it_actor->charge_rate;
        // Convert charge_rate to how long it takes to charge 1 kW, the unit batteries use.
        // 0 means batteries won't be charged, but it can still provide power to devices unless efficiency is also 0.
        if( it_actor->charge_rate != 0 ) {
            double charge_rate = std::floor( 1000000.0 / std::abs( it_actor->charge_rate ) + 0.5 );
            link().charge_interval = std::max( 1, static_cast<int>( charge_rate ) );
        }
    }
}

int item::link_length() const
{
    return has_no_links() ? -2 :
           link_has_state( link_state::needs_reeling ) ? -1 : link().length;
}

int item::max_link_length() const
{
    return !has_link_data() ? -2 :
           link().max_length != -1 ? link().max_length : type->maximum_charges();
}

int item::link_sort_key() const
{
    const int length = link_length();
    int key = length >= 0 ? -1000000000 : length == -1 ? 0 : 1000000000;
    key += max_link_length() * 100000;
    return key - length;
}

std::vector<const item *> item::cables() const
{
    return contents.cables();
}

// CDF: LINKUP1b, DDA item_gun_tool_ammo.cpp:3907-4103 (process_link),
// :4100-4224 (charge_linked_batteries) and :4225-4300 (reset_link), adapted to CDF's
// coordinates (bub/abs helpers, no vehicle::pos_abs) and to the existing distribution graph.
auto item::process_link( map &here, Character *carrier, const tripoint_bub_ms &pos ) -> bool
{
    if( link_length() < 0 ) {
        return false;
    }

    const bool is_cable_item = has_flag( flag_CABLE_SPOOL );

    // CDF: LINKUP1d, DDA item_gun_tool_ammo.cpp:3915-3932: a cable in the inventory whose source
    // end is a solar pack or a UPS breaks loose when that item is gone or no longer connected.
    if( link().source == link_state::solarpack ) {
        if( carrier == nullptr || !carrier->worn_with_flag( flag_SOLARPACK_ON ) ) {
            // CDF: CDF has no add_msg_if_player_sees(); report only when the avatar sees the cable.
            if( get_avatar().sees( pos ) ) {
                add_msg( m_bad, _( "The %s has come loose from the solar pack." ), link_name() );
            }
            reset_link( true, carrier );
            return false;
        }
    }
    const auto used_ups = []( const item & itm ) {
        return itm.get_var( "cable" ) == "plugged_in";
    };
    if( link().source == link_state::ups ) {
        // CDF: DDA's cache_has_item_with( flag_IS_UPS, used_ups ) has no CDF counterpart.
        const bool has_ups = carrier != nullptr && carrier->has_item_with( [&used_ups]( const item & itm ) {
            return itm.has_flag( flag_IS_UPS ) && used_ups( itm );
        } );
        if( !has_ups ) {
            // CDF: CDF has no add_msg_if_player_sees(); report only when the avatar sees the cable.
            if( get_avatar().sees( pos ) ) {
                add_msg( m_bad, _( "The %s has come loose from the UPS." ), link_name() );
            }
            reset_link( true, carrier );
            return false;
        }
    }

    // Certain cable states should skip processing and also become inactive if dropped.
    if( ( link().target == link_state::no_link && link().source != link_state::vehicle_tow ) ||
        link().target == link_state::bio_cable ) {
        if( carrier == nullptr ) {
            return reset_link( true, nullptr, -1, true, pos );
        }
        return false;
    }

    const bool last_t_abs_pos_is_oob = !here.inbounds( link().t_abs_pos );

    // Lambda function for checking if a cable has been stretched too long, resetting it if so.
    // @return True if the cable is disconnected.
    // CDF: DDA's add_msg_debug( debugmode::DF_IUSE, ... ) call is dropped: CDF has no debugmode.
    const auto check_length = [this, carrier, is_cable_item]() {
        if( link().length > link().max_length ) {
            const std::string cable_name = is_cable_item ? string_format( _( "over-extended %s" ), label( 1 ) ) :
                                           string_format( _( "%s's over-extended cable" ), label( 1 ) );
            if( carrier != nullptr ) {
                carrier->add_msg_if_player( m_bad, _( "Your %s breaks loose!" ), cable_name );
            }
            return true;
        } else if( link().length + M_SQRT2 >= link().max_length + 1 && carrier != nullptr ) {
            carrier->add_msg_if_player( m_warning, _( "Your %s is stretched to its limit!" ), link_name() );
        }
        return false;
    };

    // check if the item moved since last length check
    bool length_check_needed = false;
    const tripoint_abs_ms abs_pos = bub_to_abs( pos );
    if( link().s_abs_pos != abs_pos ) {
        link().s_abs_pos = abs_pos;
        length_check_needed = true;
    }

    // Re-establish vehicle pointer if it got lost or if this item just got loaded.
    if( !link().t_veh ) {
        vehicle *found_veh = vehicle::find_vehicle( link().t_abs_pos );
        if( !found_veh ) {
            if( last_t_abs_pos_is_oob ) {
                // Target is outside the reality bubble -- its submap is probably
                // just unloaded. Skip processing this turn; the cable will
                // reconnect when the target submap loads back in.
                return false;
            }
            return reset_link( true, carrier, -2, true, pos );
        }
        // CDF: DDA's re-established-link debug message is dropped (no debugmode in CDF).
        link().t_veh = found_veh;
    }

    // Regular pointers are faster, so make one now that we know the reference is valid.
    vehicle *t_veh = link().t_veh;

    // We should skip processing if the last saved target point is out of bounds, since vehicles give innacurate absolute coordinates when out of bounds.
    // However, if the linked vehicle is moving fast enough, we should always do processing to avoid erroneously skipping linked items riding inside of it.
    if( last_t_abs_pos_is_oob && t_veh->velocity < HALF_MAPSIZE_X * 400 ) {
        if( !length_check_needed ) {
            return false;
        }
        link().length = rl_dist( abs_pos,
                                 link().t_abs_pos + t_veh->coord_translate( link().t_mount ) );
        if( check_length() ) {
            return reset_link( true, carrier );
        }
        return false;
    }

    // Find the vp_part index the cable is linked to.
    // CDF: DDA looks the part up in cable_ports/batteries; CDF has no appliance part lists, so
    // the part at the recorded mount is used (a lost part disconnects the cable).
    const int link_vp_index = t_veh->part_at( t_veh->coord_translate( link().t_mount ) );
    if( link_vp_index == -1 ) {
        // The part with cable ports was lost, so disconnect the cable.
        return reset_link( true, carrier, -2, true, pos );
    }

    // The cable was detached from the vehicle side since this link last processed.
    if( link().last_processed <= t_veh->part( link_vp_index ).last_disconnected ) {
        const std::string message = string_format( _( "You detached the %s." ), type_name() );
        if( carrier != nullptr ) {
            carrier->add_msg_if_player( m_warning, message );
        } else if( get_avatar().sees( pos ) ) {
            add_msg( m_warning, message );
        }
        return reset_link( true, carrier, -2 );
    }
    t_veh->part( link_vp_index ).set_flag( vehicle_part::linked_flag );

    const int turns_elapsed = to_turns<int>( calendar::turn - link().last_processed );
    link().last_processed = calendar::turn;

    // Set the new absolute position to the vehicle's origin.
    const tripoint_abs_ms new_t_abs_pos = bub_to_abs( t_veh->bub_ms_location() );
    if( link().t_abs_pos != new_t_abs_pos ) {
        link().t_abs_pos = new_t_abs_pos;
        length_check_needed = true;
    }

    // If either of the link's connected sides moved, check the cable's length.
    if( length_check_needed ) {
        link().length = rl_dist( pos, t_veh->bub_part_location( link_vp_index ) );
        if( check_length() ) {
            return reset_link( true, carrier, link_vp_index );
        }
    }

    // Extra behaviors for the cabled item.
    if( !is_cable_item ) {
        if( link().efficiency < MIN_LINK_EFFICIENCY ) {
            return false;
        }
        // Recharge or charge linked batteries.
        charge_linked_batteries( *t_veh, turns_elapsed );
        // CDF: DDA also reports the tool's power draw to vehicle::linked_item_epower_this_turn
        // for the vehicle's power display; CDF has no such field, so the draw is not shown.
    } else if( has_flag( flag_NO_DROP ) ) {
        debugmsg( "%s shouldn't exist outside of an item or vehicle part.", tname() );
        return true;
    }

    return false;
}

auto item::charge_linked_batteries( vehicle &linked_veh, int turns_elapsed ) -> int
{
    if( !has_link_data() || link().charge_rate == 0 || link().charge_interval < 1 ) {
        return 0;
    }

    // Find a battery-flavored magazine to charge; on multimag items only
    // the battery well is meaningful here.
    item *parent_mag = nullptr;
    for( item *mag : magazines_current() ) {
        if( mag && mag->ammo_capacity( ammo_battery ) > 0 ) {
            parent_mag = mag;
            break;
        }
    }
    if( !is_battery() ) {
        if( !parent_mag || !parent_mag->has_flag( flag_RECHARGE ) ) {
            return 0;
        }
    }

    const int battery_charges = parent_mag ? parent_mag->ammo_remaining() : ammo_remaining();
    const int battery_capacity = parent_mag
                                 ? parent_mag->ammo_capacity( ammo_battery )
                                 : ammo_capacity( ammo_battery );
    const bool power_in = link().charge_rate > 0;
    if( power_in ? battery_charges >= battery_capacity : battery_charges <= 0 ) {
        return 0;
    }
    // Direct receiver for ammo_set so multimag items don't add charges to
    // the wrong well.
    item *charge_target = parent_mag ? parent_mag : this;

    // Normally efficiency is the chance to get a charge every charge_interval, but if we're catching up from
    // time spent outside the reality bubble it should be applied as a percentage of the total instead.
    const bool short_time_passed = turns_elapsed <= link().charge_interval;

    if( turns_elapsed < 1 || ( short_time_passed &&
                               !calendar::once_every( time_duration::from_turns( link().charge_interval ) ) ) ) {
        // Too early to get any charge
        return link().charge_rate;
    }

    if( power_in ) {
        const int available_charges = linked_veh.connected_battery_power_level().first;
        const int wanted_charges = battery_capacity - battery_charges;

        // Nothing to charge or nothing to charge with
        if( wanted_charges == 0 || available_charges == 0 ) {
            return link().charge_rate;
        }

        if( short_time_passed ) {
            // Single charge - subtract one from source, roll against efficiency to add one to destination
            const int deficit = linked_veh.discharge_battery( 1, true );
            if( deficit == 0 && rng_float( 0.0, 1.0 ) <= link().efficiency ) {
                charge_target->ammo_set( itype_battery, charge_target->ammo_remaining() + 1 );
            }
        } else {
            // Multiple charges - get minimum from available charges, possible transfer in given time and
            // amount of charges destination needs to charge itself to full with given efficiency.
            // Subtract that value from the source and after reduction by efficiency add it to the destination.
            const int possible_transfer =
                static_cast<int>( turns_elapsed * 1.0f / link().charge_interval );
            const int required_charges = static_cast<int>( wanted_charges / link().efficiency );
            const int spent_charges = std::min( { possible_transfer, available_charges, required_charges } );
            const int deficit = linked_veh.discharge_battery( spent_charges, true );
            const int actual_spent = spent_charges - deficit;
            // Avoid rounding error on full charge
            const int received_charges = actual_spent >= required_charges
                                         ? wanted_charges
                                         : static_cast<int>( actual_spent * link().efficiency );

            charge_target->ammo_set( itype_battery,
                                     charge_target->ammo_remaining() + received_charges );
        }
    } else {
        const auto [bat_remaining, bat_capacity] = linked_veh.connected_battery_power_level();
        const int available_charges = battery_charges;
        const int wanted_charges = bat_capacity - bat_remaining;

        // Nothing to charge or nothing to charge with
        if( wanted_charges == 0 || available_charges == 0 ) {
            return link().charge_rate;
        }

        if( short_time_passed ) {
            // Single charge - subtract one from source, roll against efficiency to add one to destination
            charge_target->ammo_set( itype_battery, charge_target->ammo_remaining() - 1 );
            if( rng_float( 0.0, 1.0 ) <= link().efficiency ) {
                const int surplus = linked_veh.charge_battery( 1, true );
                if( surplus != 0 ) {
                    // Battery couldn't accept the charge, refund it
                    charge_target->ammo_set( itype_battery, charge_target->ammo_remaining() + 1 );
                }
            }
        } else {
            // Multiple charges - get minimum from available charges, possible transfer in given time and
            // amount of charges destination needs to charge itself to full with given efficiency.
            // Subtract that value from the source and after reduction by efficiency add it to the destination.
            const int possible_transfer =
                static_cast<int>( turns_elapsed * 1.0f / link().charge_interval );
            const int required_charges = static_cast<int>( wanted_charges / link().efficiency );
            const int spent_charges = std::min( { possible_transfer, available_charges, required_charges } );
            // Avoid rounding error on full charge
            const int received_charges = spent_charges == required_charges
                                         ? wanted_charges
                                         : static_cast<int>( spent_charges * link().efficiency );

            const int surplus = linked_veh.charge_battery( received_charges, true );
            const int actual_received = received_charges - surplus;
            // Only deduct from source what was actually accepted (accounting for efficiency)
            int actual_spent = actual_received == wanted_charges
                               ? required_charges
                               : static_cast<int>( actual_received / link().efficiency );
            // Ensure we don't spend more than planned
            actual_spent = std::min( actual_spent, spent_charges );

            charge_target->ammo_set( itype_battery,
                                     charge_target->ammo_remaining() - actual_spent );
        }
    }
    return link().charge_rate;
}

auto item::reset_link( bool unspool_if_too_long, Character *p, int vpart_index,
                       const bool loose_message, const tripoint_bub_ms cable_position ) -> bool
{
    if( !has_link_data() ) {
        return has_flag( flag_NO_DROP );
    }
    if( unspool_if_too_long && link_has_state( link_state::needs_reeling ) ) {
        // Cables that need reeling should be reset with a reel_cable_activity_actor instead.
        return false;
    }

    if( vpart_index != -2 && link().t_veh ) {
        if( vpart_index == -1 ) {
            // CDF: DDA looks the mount up in cable_ports/batteries; CDF looks up the part itself.
            vpart_index = link().t_veh->part_at( link().t_veh->coord_translate( link().t_mount ) );
        }
        if( vpart_index != -1 ) {
            link().t_veh->part( vpart_index ).remove_flag( vehicle_part::linked_flag );
        }
    }

    if( loose_message ) {
        if( p != nullptr ) {
            p->add_msg_if_player( m_warning, _( "Your %s has come loose." ), link_name() );
        } else {
            // CDF: CDF has no add_msg_if_player_sees(); report only when the avatar sees the cable.
            if( get_avatar().sees( cable_position ) ) {
                add_msg( m_warning, _( "The %s has come loose." ), link_name() );
            }
        }
    }

    // CDF: DDA unspools an over-long cable (link_state::needs_reeling) and requires
    // reel_cable_activity_actor to wind it back; that actor is not in LINKUP1b's or LINKUP1d's
    // file list, so CDF always resets the link fully.
    if( loose_message && p ) {
        p->add_msg_if_player( m_info, _( "You reel in the %s and wind it up." ), link_name() );
    }

    link_.reset();
    if( !cables().empty() ) {
        // If there are extensions, keep link active to maintain max_length.
        update_link_traits();
    }
    return has_flag( flag_NO_DROP );
}
