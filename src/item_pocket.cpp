// Transplant of DDA src/item_pocket.cpp (pin b4b62254ae2), adapted to BN ownership (rules T1-T12
// of plans/P1-transplant/COMMON.md). Differences from DDA are tagged `// CDF:`; pieces that
// wait for a later job are marked as such in the job report.
//
// Not transplanted here: pocket presets (P2), e-file pockets (E1), `can_reload_with` (P3),
// `heat_up`, `process` and `leak` (the BN item::process tick model stays the driver, T12; the
// accessors spoil_multiplier(), insulation(), can_contain_liquid() are provided for it).
#include "item_pocket.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

#include "ammo.h"
#include "bodypart.h"
#include "calendar.h"
#include "avatar.h"
#include "character.h"
#include "color.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "fstream_utils.h"
#include "handle_liquid.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "iteminfo_format_utils.h"
#include "itype.h"
#include "json.h"
#include "locations.h"
#include "map/map.h"
#include "material.h"
#include "messages.h"
#include "output.h"
#include "path_info.h"
#include "string_formatter.h"
#include "subbodypart.h"
#include "translations.h"
#include "units.h"
#include "units_utility.h"

static const itype_id itype_water( "water" );

namespace io
{

template<>
std::string enum_to_string<pocket_type>( pocket_type data )
{
    switch( data ) {
        // *INDENT-OFF*
        case pocket_type::CONTAINER: return "CONTAINER";
        case pocket_type::MAGAZINE: return "MAGAZINE";
        case pocket_type::MAGAZINE_WELL: return "MAGAZINE_WELL";
        case pocket_type::MOD: return "MOD";
        case pocket_type::CORPSE: return "CORPSE";
        case pocket_type::SOFTWARE: return "SOFTWARE";
        case pocket_type::MIGRATION: return "MIGRATION";
        // *INDENT-ON*
        case pocket_type::LAST:
            break;
    }
    debugmsg( "Invalid pocket_type" );
    abort();
}

} // namespace io

// CDF: DDA `item_pocket( const pocket_data * )` has no owner; items inside report no parent
item_pocket::item_pocket( item *owner, const pocket_data *data ) :
    data( data ), contents( data->carry ? new carry_pocket_item_location( owner ) :
                            new contents_item_location( owner ) )
{
}

item_pocket::item_pocket( const pocket_data *data ) : item_pocket( nullptr, data )
{
}

item_pocket::~item_pocket() = default;

bool item_pocket::operator==( const item_pocket &rhs ) const
{
    return *data == *rhs.data;
}

namespace
{

// A stack's identity for same_contents: type, charges and what the item holds in turn.
bool same_item_contents( const item &a, const item &b )
{
    if( a.typeId() != b.typeId() || a.charges != b.charges ) {
        return false;
    }
    const std::list<item_pocket> &pockets_a = a.contents.get_pockets();
    const std::list<item_pocket> &pockets_b = b.contents.get_pockets();
    return pockets_a.size() == pockets_b.size() &&
           std::equal( pockets_a.begin(), pockets_a.end(), pockets_b.begin(),
    []( const item_pocket & pa, const item_pocket & pb ) {
        return pa.same_contents( pb );
    } );
}

} // namespace

bool item_pocket::same_contents( const item_pocket &rhs ) const
{
    if( contents.size() != rhs.contents.size() ) {
        return false;
    }
    return std::equal( contents.begin(), contents.end(),
                       rhs.contents.begin(), rhs.contents.end(),
    []( const item * a, const item * b ) {
        return same_item_contents( *a, *b );
    } );
}

void item_pocket::restack()
{
    if( contents.size() <= 1 ) {
        return;
    }
    restack( nullptr );
}

item *item_pocket::restack( /*const*/ item *it )
{
    item *ret = it;
    if( contents.size() <= 1 ) {
        return ret;
    }
    // CDF: combining is detach + merge_charges (T1), DDA's `combine( *next )` + erase. The item
    // list is a vector, so the loops work on indices and re-read it after every merge.
    const auto combine_into = [this, &ret]( size_t outer_index, size_t inner_index ) {
        item *outer = contents.as_vector()[outer_index];
        item *inner = contents.as_vector()[inner_index];
        // inner was placed in outer, check if inner was the item that we track
        if( inner == ret ) {
            ret = outer;
        }
        detached_ptr<item> det = contents.remove( inner );
        outer->merge_charges( std::move( det ), true );
    };
    if( is_type( pocket_type::MAGAZINE ) ) {
        // Restack magazine contents in a way that preserves order of items
        size_t i = 0;
        while( i < contents.size() ) {
            // CDF: DDA loops forever here on an item that is not counted by charges
            if( !contents.as_vector()[i]->count_by_charges() ) {
                ++i;
                continue;
            }
            if( i + 1 >= contents.size() ) {
                break;
            }
            if( contents.as_vector()[i]->can_combine( *contents.as_vector()[i + 1] ) ) {
                combine_into( i, i + 1 );
                // DDA continues after the item that was merged into
                ++i;
            } else {
                ++i;
            }
        }
        invalidate_cache();
        return ret;
    }
    bool merged = true;
    while( merged ) {
        merged = false;
        for( size_t outer = 0; outer < contents.size() && !merged; ++outer ) {
            if( !contents.as_vector()[outer]->count_by_charges() ) {
                continue;
            }
            for( size_t inner = 0; inner < contents.size(); ++inner ) {
                if( outer == inner || !contents.as_vector()[inner]->count_by_charges() ) {
                    continue;
                }
                if( contents.as_vector()[outer]->can_combine( *contents.as_vector()[inner] ) ) {
                    combine_into( outer, inner );
                    // DDA starts over after each merge
                    merged = true;
                    break;
                }
            }
        }
    }
    invalidate_cache();
    return ret;
}

bool item_pocket::has_item_stacks_with( const item &it ) const
{
    return std::ranges::any_of( contents, [&it]( const item * inside ) {
        return it.stacks_with( *inside );
    } );
}

bool item_pocket::better_pocket( const item_pocket &rhs, const item &it, bool nested ) const
{
    if( this->settings.priority() != rhs.settings.priority() ) {
        // Priority overrides all other factors.
        return rhs.settings.priority() > this->settings.priority();
    }

    // if we've somehow bypassed the pocket autoinsert rules, check them here
    if( !rhs.settings.accepts_item( it ) ) {
        return false;
    } else if( !this->settings.accepts_item( it ) ) {
        return true;
    }

    const bool rhs_whitelisted = !rhs.settings.get_item_whitelist().empty() ||
                                 !rhs.settings.get_category_whitelist().empty();
    const bool lhs_whitelisted = !this->settings.get_item_whitelist().empty() ||
                                 !this->settings.get_category_whitelist().empty();
    if( rhs_whitelisted != lhs_whitelisted ) {
        return rhs_whitelisted;
    }

    const bool rhs_it_stack = rhs.has_item_stacks_with( it );
    if( has_item_stacks_with( it ) != rhs_it_stack ) {
        return rhs_it_stack;
    }

    if( data->ammo_restriction.empty() != rhs.data->ammo_restriction.empty() ) {
        // pockets restricted by ammo should try to get filled first
        return !rhs.data->ammo_restriction.empty();
    }

    if( data->get_flag_restrictions().empty() != rhs.data->get_flag_restrictions().empty() ) {
        // pockets restricted by flag should try to get filled first
        return !rhs.data->get_flag_restrictions().empty();
    }

    if( it.is_comestible() && it.get_comestible()->spoils != 0_seconds ) {
        // a lower spoil multiplier is better
        return rhs.spoil_multiplier() < spoil_multiplier();
    }

    if( it.made_of( phase_id::SOLID ) && !it.is_frozen_liquid() ) {
        if( data->watertight != rhs.data->watertight ) {
            return !rhs.data->watertight;
        }
    }

    if( it.is_frozen_liquid() ) {
        if( data->watertight != rhs.data->watertight ) {
            return rhs.data->watertight;
        }
    }

    //Skip irrelevant properties of nested containers
    if( nested ) {
        return false;
    }

    if( rhs.data->extra_encumbrance != data->extra_encumbrance ) {
        // pockets with less extra encumbrance should be prioritized
        return rhs.data->extra_encumbrance < data->extra_encumbrance;
    }

    if( data->ripoff != rhs.data->ripoff ) {
        // pockets without ripoff chance should be prioritized
        return rhs.data->ripoff < data->ripoff;
    }

    if( data->rigid != rhs.data->rigid ) {
        // Prefer rigid containers, as their fixed volume is wasted unless filled
        return rhs.data->rigid;
    }

    if( remaining_volume() != rhs.remaining_volume() ) {
        // we want the least amount of remaining volume
        return rhs.remaining_volume() < remaining_volume();
    }

    return rhs.obtain_cost( it ) < obtain_cost( it );
}

bool item_pocket::stacks_with( const item_pocket &rhs, int depth, int maxdepth ) const
{
    if( _sealed != rhs._sealed ) {
        return false;
    }
    return ( empty() && rhs.empty() ) || std::equal( contents.begin(), contents.end(),
            rhs.contents.begin(), rhs.contents.end(),
    [depth, maxdepth]( const item * a, const item * b ) {
        return depth < maxdepth && a->charges == b->charges &&
               a->stacks_with( *b, false, false, depth + 1 );
    } );
}

bool item_pocket::is_funnel_container( units::volume &bigger_than ) const
{
    // should not be static, since after reloading some members of the item objects,
    // such as item types, may be invalidated.
    item *water = item::spawn_temporary( itype_water, calendar::turn_zero, 1 );
    if( !data->watertight ) {
        return false;
    }
    if( sealable() && _sealed ) {
        return false;
    }
    if( can_contain( *water ).success() ) {
        bigger_than = remaining_volume();
        return true;
    }
    return false;
}

bool item_pocket::is_restricted() const
{
    return !data->get_flag_restrictions().empty() || !data->ammo_restriction.empty();
}

std::vector<item *> item_pocket::all_items_top()
{
    return std::vector<item *>( contents.begin(), contents.end() );
}

std::vector<const item *> item_pocket::all_items_top() const
{
    return std::vector<const item *>( contents.begin(), contents.end() );
}

std::vector<item *> item_pocket::all_items_ptr( pocket_type pk_type )
{
    if( !is_type( pk_type ) ) {
        return std::vector<item *>();
    }
    std::vector<item *> all_items_top_level{ all_items_top() };
    // the vector grows while it is walked
    for( size_t i = 0; i < all_items_top_level.size(); i++ ) {
        std::vector<item *> all_items_internal{ all_items_top_level[i]->contents.all_items_ptr() };
        all_items_top_level.insert( all_items_top_level.end(), all_items_internal.begin(),
                                    all_items_internal.end() );
    }
    return all_items_top_level;
}

std::vector<const item *> item_pocket::all_items_ptr( pocket_type pk_type ) const
{
    if( !is_type( pk_type ) ) {
        return std::vector<const item *>();
    }
    std::vector<const item *> all_items_top_level{ all_items_top() };
    for( size_t i = 0; i < all_items_top_level.size(); i++ ) {
        const std::vector<const item *> all_items_internal{ all_items_top_level[i]->contents.all_items_ptr() };
        all_items_top_level.insert( all_items_top_level.end(), all_items_internal.begin(),
                                    all_items_internal.end() );
    }
    return all_items_top_level;
}

bool item_pocket::has_any_with( const std::function<bool( const item &it )> &filter ) const
{
    return std::ranges::any_of( contents, [&filter]( const item * it ) {
        return filter( *it );
    } );
}

item &item_pocket::back()
{
    return *contents.back();
}

const item &item_pocket::back() const
{
    return *contents.back();
}

item &item_pocket::front()
{
    return *contents.front();
}

const item &item_pocket::front() const
{
    return *contents.front();
}

void item_pocket::pop_back()
{
    contents.erase( contents.end() - 1 );
    invalidate_cache();
}

size_t item_pocket::size() const
{
    return contents.size();
}

units::volume item_pocket::magazine_well() const
{
    return data->magazine_well;
}

units::mass item_pocket::weight_capacity() const
{
    return data->max_contains_weight;
}

units::volume item_pocket::volume_capacity() const
{
    // CDF: the per-instance override (body storage) and the cheap ammo capacity stay here;
    // pocket_data::volume_capacity() scans every item type (J2-F12)
    if( capacity_override ) {
        return *capacity_override;
    }
    if( !data->ammo_restriction.empty() ) {
        // capacity follows the largest accepted ammo, as in DDA
        units::volume best = 0_ml;
        for( const auto &ammo_res : data->ammo_restriction ) {
            const itype_id &def = ammo_res.first->default_ammotype();
            if( def.is_null() || !def.is_valid() || def->stack_size <= 0 ) {
                continue;
            }
            const units::volume v = def->volume * ammo_res.second / def->stack_size;
            best = std::max( best, v );
        }
        return best;
    }
    return data->volume_capacity();
}

units::volume item_pocket::remaining_volume() const
{
    return volume_capacity() - contents_volume();
}

int item_pocket::remaining_capacity_for_item( const item &it ) const
{
    if( it.count_by_charges() ) {
        item *it_copy = item::spawn_temporary( it );
        it_copy->charges = 1;
        if( can_contain( *it_copy ).success() ) {
            return std::min( {
                it.charges,
                charges_per_remaining_volume( it ),
                charges_per_remaining_weight( it )
            } );
        } else {
            return 0;
        }
    } else if( can_contain( it ).success() ) {
        return std::min( {
            charges_per_remaining_volume( it ),
            charges_per_remaining_weight( it )
        } );
    } else {
        return 0;
    }
}

units::volume item_pocket::item_size_modifier() const
{
    if( data->rigid ) {
        return 0_ml;
    }
    units::volume total_vol = 0_ml;
    for( const item *it : contents ) {
        total_vol += it->volume( is_type( pocket_type::MOD ) );
    }
    total_vol -= data->magazine_well;
    total_vol *= data->volume_multiplier;
    return std::max( 0_ml, total_vol );
}

units::mass item_pocket::item_weight_modifier() const
{
    units::mass total_mass = 0_gram;
    for( const item *it : contents ) {
        if( is_type( pocket_type::MOD ) ) {
            total_mass += it->weight( true, true ) * data->weight_multiplier;
        } else {
            total_mass += it->weight() * data->weight_multiplier;
        }
    }
    return total_mass;
}

units::length item_pocket::item_length_modifier() const
{
    units::length total_length = 0_mm;
    for( const item *it : contents ) {
        total_length = std::max( static_cast<units::length>( it->length() * std::cbrt(
                                     data->volume_multiplier ) ),
                                 total_length );
    }
    return total_length;
}

float item_pocket::spoil_multiplier() const
{
    if( sealed() ) {
        return data->sealed_data->spoil_multiplier;
    } else {
        return data->spoil_multiplier;
    }
}

float item_pocket::insulation() const
{
    return data->insulation;
}

int item_pocket::moves() const
{
    if( data ) {
        return data->moves;
    } else {
        return -1;
    }
}

std::vector<item *> item_pocket::gunmods()
{
    std::vector<item *> mods;
    for( item *it : contents ) {
        if( it->is_gunmod() ) {
            mods.push_back( it );
        }
    }
    return mods;
}

std::vector<const item *> item_pocket::gunmods() const
{
    std::vector<const item *> mods;
    for( const item *it : contents ) {
        if( it->is_gunmod() ) {
            mods.push_back( it );
        }
    }
    return mods;
}

cata::flat_set<itype_id> item_pocket::item_type_restrictions() const
{
    return data->item_id_restriction;
}

item *item_pocket::magazine_current()
{
    auto iter = std::ranges::find_if( contents, []( const item * it ) {
        return !it->is_null();
    } );
    return iter != contents.end() ? *iter : nullptr;
}

const item *item_pocket::magazine_current() const
{
    auto iter = std::ranges::find_if( contents, []( const item * it ) {
        return !it->is_null();
    } );
    return iter != contents.end() ? *iter : nullptr;
}

itype_id item_pocket::magazine_default() const
{
    if( is_type( pocket_type::MAGAZINE_WELL ) ) {
        return data->default_magazine;
    }
    return itype_id::NULL_ID();
}

int item_pocket::ammo_consume( int qty )
{
    int need = qty;
    int used = 0;
    for( auto it = contents.begin(); it != contents.end(); ) {
        if( ( *it )->has_flag( flag_CASING ) ) {
            ++it;
            continue;
        }
        if( need >= ( *it )->charges ) {
            need -= ( *it )->charges;
            used += ( *it )->charges;
            it = contents.erase( it );
        } else {
            ( *it )->charges -= need;
            used += need;
            break;
        }
    }
    invalidate_cache();
    return used;
}

int item_pocket::ammo_capacity( const ammotype &ammo ) const
{
    const auto found_ammo = data->ammo_restriction.find( ammo );
    if( found_ammo == data->ammo_restriction.end() ) {
        return 0;
    } else {
        return std::lround( found_ammo->second * capacity_mult );
    }
}

int item_pocket::remaining_ammo_capacity( const ammotype &ammo ) const
{
    int total_capacity = ammo_capacity( ammo );
    if( total_capacity == 0 ) {
        return 0;
    }
    int ammo_count = 0;
    for( const item *it : contents ) {
        if( it->has_flag( flag_CASING ) ) {
            continue;
        } else if( ammo != it->ammo_type() ) {
            return 0;
        }
        ammo_count += it->count();
    }
    // A capacity-shrinking mod (or removing a boost mod) can leave a pocket
    // overfilled; grandfather the overflow rather than report negative space.
    return std::max( 0, total_capacity - ammo_count );
}

std::set<ammotype> item_pocket::ammo_types() const
{
    std::set<ammotype> ret;
    for( const std::pair<const ammotype, int> &type_pair : data->ammo_restriction ) {
        ret.emplace( type_pair.first );
    }
    return ret;
}

bool item_pocket::accepts_some_ammo( const item &it ) const
{
    // CDF: bandolier test. D12: ammunition mixes by ammotype, as in DDA
    if( data->ammo_restriction.empty() || data->holster || !is_compatible( it ).success() ) {
        return false;
    }
    return remaining_ammo_capacity( it.ammo_type() ) > 0;
}

bool item_pocket::type_can_holster( const itype &t, const item &it )
{
    for( const pocket_data &d : t.pockets ) {
        if( d.holster && item_pocket( nullptr, &d ).can_contain( it, true ).success() ) {
            return true;
        }
    }
    return false;
}

bool item_pocket::is_overfull() const
{
    if( !is_standard_type() || contents.empty() ) {
        return false;
    }
    if( data->holster && contents.size() > 1 ) {
        return true;
    }
    if( !data->ammo_restriction.empty() ) {
        const item *first = nullptr;
        int total = 0;
        for( const item *it : contents ) {
            if( it->has_flag( flag_CASING ) ) {
                continue;
            }
            first = first == nullptr ? it : first;
            total += it->count();
        }
        return first != nullptr && total > ammo_capacity( first->ammo_type() );
    }
    return contents_volume() > volume_capacity() || contains_weight() > weight_capacity();
}

void item_pocket::casings_handle( const std::function < detached_ptr<item>( detached_ptr<item> && )
                                  > &func )
{
    // CDF: T1, the functor consumes the casing and hands back what was not handled
    contents.remove_with( [&func]( detached_ptr<item> &&it ) {
        if( it->has_flag( flag_CASING ) ) {
            it->unset_flag( flag_CASING );
            it = func( std::move( it ) );
            if( it ) {
                // didn't handle the casing so reset the flag ready for next call
                it->set_flag( flag_CASING );
            }
        }
        return std::move( it );
    } );
    invalidate_cache();
}

void item_pocket::handle_liquid_or_spill( Character &guy, const item *avoid )
{
    if( guy.is_npc() ) {
        spill_contents( guy.bub_pos() );
        return;
    }

    // CDF: handling a liquid can move the item out of this pocket (pouring it on the ground
    // detaches it), so the loop works on a snapshot and looks the item up again each time.
    const std::vector<item *> snapshot( contents.begin(), contents.end() );
    const auto still_here = [this]( const item * it ) {
        return std::ranges::find( contents, it ) != contents.end();
    };
    for( item *current : snapshot ) {
        if( !still_here( current ) ) {
            continue;
        }
        if( current->made_of( phase_id::LIQUID ) ) {
            // CDF: BN's handle_liquid has no destination cache or avoid item
            while( still_here( current ) && current->charges > 0 &&
                   liquid_handler::handle_liquid( *current, 1 ) ) {
                // keep handing out charges
            }
            if( still_here( current ) ) {
                if( current->charges == 0 ) {
                    auto iter = std::ranges::find( contents, current );
                    contents.erase( iter );
                } else {
                    invalidate_cache();
                    return;
                }
            }
        } else {
            // per DDA item_pocket.cpp:848: the item is added or dropped as a copy, avoiding this
            // container, and then taken out of the pocket
            const std::string name = current->display_name();
            guy.i_add_or_drop( *current, 1, avoid, current );
            contents.erase( std::ranges::find( contents, current ) );
            guy.add_msg_if_player( m_warning, _( "The %1$s falls out of the %2$s." ), name,
                                   get_name().translated() );
        }
    }
    invalidate_cache();
}

bool item_pocket::use_amount( const itype_id &it, int &quantity,
                              std::vector<detached_ptr<item>> &used )
{
    bool used_item = false;
    for( auto a = contents.begin(); a != contents.end() && quantity > 0; ) {
        // CDF: T3, BN's use_amount takes the item and hands back what is left of it
        detached_ptr<item> det;
        a = contents.erase( a, &det );
        det = item::use_amount( std::move( det ), it, quantity, used );
        if( det ) {
            a = contents.insert( a, std::move( det ) );
            ++a;
        } else {
            used_item = true;
        }
    }
    restack();
    invalidate_cache();
    return used_item;
}

bool item_pocket::will_explode_in_a_fire() const
{
    if( data->fire_protection ) {
        return false;
    }
    return std::ranges::any_of( contents, []( const item * it ) {
        return it->will_explode_in_fire();
    } );
}

bool item_pocket::will_spill() const
{
    if( sealed() ) {
        return false;
    } else {
        return data->open_container;
    }
}

bool item_pocket::will_spill_if_unsealed() const
{
    return data->open_container;
}

bool item_pocket::seal()
{
    if( !sealable() || empty() ) {
        return false;
    }
    _sealed = true;
    return true;
}

void item_pocket::unseal()
{
    _sealed = false;
}

bool item_pocket::sealed() const
{
    if( !sealable() ) {
        return false;
    } else {
        // CDF: a pocket of a `sealed_by_type` item (BN unseals_into) is sealed while it has the type
        return _sealed || data->sealed_by_type;
    }
}

bool item_pocket::sealable() const
{
    // if it has sealed data it is understood that the
    // data is different when sealed than when not
    // In other words, a pocket is sealable IFF it has sealed_data.
    return data->sealed_data.has_value();
}

std::string item_pocket::translated_sealed_prefix() const
{
    if( sealed() ) {
        return _( "sealed" );
    } else {
        return _( "open" );
    }
}

bool item_pocket::detonate( const tripoint_bub_ms &pos, std::vector<detached_ptr<item>> &drops )
{
    // CDF: T3, BN's detonate takes the item and hands it back if it did not explode
    bool exploded = false;
    for( auto iter = contents.begin(); iter != contents.end(); ) {
        detached_ptr<item> det;
        iter = contents.erase( iter, &det );
        det = item::detonate( std::move( det ), pos, drops );
        if( det ) {
            iter = contents.insert( iter, std::move( det ) );
            ++iter;
        } else {
            exploded = true;
        }
    }
    invalidate_cache();
    // If any of the contents explodes, so does the container
    return exploded;
}

void item_pocket::remove_all_ammo( Character &guy )
{
    for( auto iter = contents.begin(); iter != contents.end(); ) {
        if( ( *iter )->is_irremovable() ) {
            ++iter;
            continue;
        }
        detached_ptr<item> det;
        iter = contents.erase( iter, &det );
        // DDA drop_or_handle: liquids are poured out, everything else goes to the character
        if( det->made_of( phase_id::LIQUID ) ) {
            liquid_handler::handle_all_liquid( std::move( det ), PICKUP_RANGE );
        } else {
            guy.i_add_or_drop( std::move( det ) );
        }
    }
    invalidate_cache();
}

void item_pocket::remove_all_mods( Character &guy )
{
    for( auto iter = contents.begin(); iter != contents.end(); ) {
        if( ( *iter )->is_toolmod() ) {
            detached_ptr<item> det;
            iter = contents.erase( iter, &det );
            guy.i_add_or_drop( std::move( det ) );
        } else {
            ++iter;
        }
    }
    on_contents_changed();
}

namespace
{

// DDA item_info.cpp:3752
std::vector<std::pair<const item *, int>> get_item_duplicate_counts(
        const std::vector<const item *> &items )
{
    std::vector<std::pair<const item *, int>> counted_items;
    for( const item *contents_item : items ) {
        bool found = false;
        for( std::pair<const item *, int> &content : counted_items ) {
            if( content.first->display_stacked_with( *contents_item ) ) {
                content.second += 1;
                found = true;
            }
        }
        if( !found ) {
            counted_items.emplace_back( contents_item, 1 );
        }
    }
    return counted_items;
}

} // namespace

void item_pocket::general_info( std::vector<iteminfo> &info, int pocket_number,
                                bool disp_pocket_number ) const
{
    if( disp_pocket_number ) {
        const std::string pocket_num = string_format( _( "Pocket %d:" ), pocket_number );
        info.emplace_back( "DESCRIPTION", pocket_num );
    }

    if( !get_description().empty() ) {
        info.emplace_back( "DESCRIPTION", string_format( "<info>%s</info>",
                           get_description().translated() ) );
    } else if( name_as_description ) {
        // fallback to the original items name as a description
        info.emplace_back( "DESCRIPTION", string_format( "<info>%s</info>",
                           get_name().translated() ) );
    }

    // NOLINTNEXTLINE(cata-translate-string-literal)
    const std::string cont_type_str = string_format( "{%d}CONTAINER", pocket_number );
    // NOLINTNEXTLINE(cata-translate-string-literal)
    const std::string base_type_str = string_format( "{%d}BASE", pocket_number );

    // Show volume/weight for normal containers, or ammo capacity if ammo_restriction is defined if its an ablative pocket don't display any info
    if( is_ablative() ) {
        // its an ablative pocket volume and weight maxes don't matter
        info.emplace_back( "DESCRIPTION",
                           _( "Holds a <info>single armored plate</info>." ) );
    } else if( data->ammo_restriction.empty() ) {
        info.push_back( vol_to_info( cont_type_str, _( "Volume: " ),
                                     volume_capacity(), 2, false ) );
        info.push_back( weight_to_info( cont_type_str, _( "  Weight: " ),
                                        weight_capacity(), 2, false ) );
        info.back().bNewLine = true;
    } else {
        std::vector<std::pair<std::string, int>> fmts;
        for( const ammotype &at : ammo_types() ) {
            int capacity = ammo_capacity( at );
            fmts.emplace_back( string_format( vgettext( "<num> round of %s",
                                              "<num> rounds of %s", capacity ),
                                              at->name() ),
                               capacity );
        }
        std::sort( fmts.begin(), fmts.end(), localized_compare );
        for( const std::pair<std::string, int> &fmt : fmts ) {
            // NOLINTNEXTLINE(cata-translate-string-literal)
            info.emplace_back( string_format( "{%d}MAGAZINE", pocket_number ), _( "Holds: " ), fmt.first,
                               iteminfo::no_flags,
                               fmt.second );
        }
    }

    if( data->max_item_length != 0_mm && !is_ablative() ) {
        info.back().bNewLine = true;
        // if the item has no minimum length then keep it in the same units as max
        if( data->min_item_length == 0_mm ) {
            info.emplace_back( base_type_str, _( "Item length: " ),
                               string_format( "<num> %s", length_units( data->max_item_length ) ),
                               iteminfo::no_newline,
                               0 );
        } else {
            info.emplace_back( base_type_str, _( "Item length: " ),
                               string_format( "<num> %s", length_units( data->min_item_length ) ),
                               iteminfo::no_newline,
                               convert_length( data->min_item_length ) );
        }
        info.emplace_back( base_type_str, _( " to " ),
                           string_format( "<num> %s", length_units( data->max_item_length ) ),
                           iteminfo::no_flags,
                           convert_length( data->max_item_length ) );
    } else if( data->min_item_length > 0_mm && !is_ablative() ) {
        info.back().bNewLine = true;
        info.emplace_back( base_type_str, _( "Minimum item length: " ),
                           string_format( "<num> %s", length_units( data->min_item_length ) ),
                           iteminfo::no_flags,
                           convert_length( data->min_item_length ) );
    }

    if( data->min_item_volume > 0_ml ) {
        std::string fmt = string_format( "<num> %s", volume_units_abbr() );
        info.emplace_back( base_type_str, _( "Minimum item volume: " ), fmt,
                           iteminfo::lower_is_better | iteminfo::is_three_decimal,
                           convert_volume( data->min_item_volume.value(), nullptr ) );
    }

    if( data->max_item_volume ) {
        std::string fmt = string_format( "<num> %s", volume_units_abbr() );
        info.emplace_back( base_type_str, _( "Maximum item volume: " ), fmt,
                           iteminfo::is_three_decimal, convert_volume( data->max_item_volume.value().value(), nullptr ) );
    }

    info.emplace_back( base_type_str, _( "Base moves to remove item: " ),
                       "<num>", iteminfo::lower_is_better, data->moves );

    if( !data->material_restriction.empty() ) {
        std::string materials;
        bool first = true;
        for( const material_id &mat : data->material_restriction ) {
            if( first ) {
                materials += mat.obj().name();
                first = false;
            } else {
                materials += ", " + mat.obj().name();
            }
        }
        info.emplace_back( "DESCRIPTION", string_format( _( "Allowed materials: %s" ), materials ) );
    }

    if( data->rigid ) {
        info.emplace_back( "DESCRIPTION", _( "This pocket is <info>rigid</info>." ) );
    }

    if( data->holster ) {
        info.emplace_back( "DESCRIPTION",
                           _( "This is a <info>holster</info>, it only holds <info>one item at a time</info>." ) );
    }

    if( data->extra_encumbrance > 0 ) {
        info.emplace_back( "DESCRIPTION",
                           string_format( _( "Causes %d <bad>additional encumbrance</bad> while in use." ),
                                          data->extra_encumbrance ) ) ;
    }

    if( data->ripoff > 0 ) {
        info.emplace_back( "DESCRIPTION",
                           _( "Items may <bad>fall out</bad> if grabbed." ) );
    }

    if( data->activity_noise.chance > 0 ) {
        info.emplace_back( "DESCRIPTION",
                           _( "Items will <bad>make noise</bad> when you move." ) );
    }

    if( data->watertight ) {
        info.emplace_back( "DESCRIPTION",
                           _( "This pocket can <info>contain a liquid</info>." ) );
    }
    if( data->airtight ) {
        info.emplace_back( "DESCRIPTION",
                           _( "This pocket can <info>contain a gas</info>." ) );
    }
    if( will_spill() ) {
        info.emplace_back( "DESCRIPTION",
                           _( "This pocket will <bad>spill</bad> if placed into another item or worn." ) );
    }
    if( data->fire_protection ) {
        info.emplace_back( "DESCRIPTION",
                           _( "This pocket <info>protects its contents from fire</info>." ) );
    }
    if( spoil_multiplier() != 1.0f ) {
        if( spoil_multiplier() != 0.0f ) {
            info.emplace_back( "DESCRIPTION",
                               string_format( _( "Contained items spoil at <neutral>%.0f%%</neutral> their original rate." ),
                                              spoil_multiplier() * 100 ) );
        } else {
            info.emplace_back( "DESCRIPTION", _( "Contained items <info>won't spoil</info>." ) );
        }
    }
    if( data->weight_multiplier != 1.0f ) {
        info.emplace_back( "DESCRIPTION",
                           string_format( _( "Items in this pocket weigh <neutral>%.0f%%</neutral> their original weight." ),
                                          data->weight_multiplier * 100 ) );
    }
    if( data->volume_multiplier != 1.0f ) {
        info.emplace_back( "DESCRIPTION",
                           string_format(
                               _( "This pocket expands at <neutral>%.0f%%</neutral> of the rate of volume of items inside." ),
                               data->volume_multiplier * 100 ) );
    }

    // Display flags items need to be stored in this pocket
    if( !data->get_flag_restrictions().empty() ) {
        info.emplace_back( "DESCRIPTION", _( "<bold>Restrictions</bold>:" ) );
        bool first = true;
        for( const flag_id &e : data->get_flag_restrictions() ) {
            const json_flag &f = *e;
            if( !f.restriction().empty() ) {
                if( first ) {
                    info.emplace_back( "DESCRIPTION", string_format( "* %s", f.restriction() ) );
                    first = false;
                } else {
                    info.emplace_back( "DESCRIPTION", string_format( _( "* <bold>or</bold> %s" ), f.restriction() ) );
                }
            }
        }
    }
    if( !no_rigid.empty() ) {
        // CDF: DDA consolidates left/right names with body_part_type::consolidate, not ported
        // Filter to the viewing character's actual anatomy
        const Character &viewer = get_player_character();
        std::set<std::string, localized_comparator> to_print;
        const std::vector<bodypart_id> viewer_parts = viewer.get_all_body_parts();
        for( const sub_bodypart_id &sbp : no_rigid ) {
            if( std::ranges::find( viewer_parts, sbp->parent.id() ) != viewer_parts.end() ) {
                to_print.insert( sbp->name.translated() );
            }
        }
        if( !to_print.empty() ) {
            std::string bps = enumerate_as_string( to_print.begin(),
            to_print.end(), []( const std::string & t ) {
                return t;
            } );
            info.emplace_back( "DESCRIPTION", string_format( _( "<bold>Can't put hard armor on: %s</bold>." ),
                               bps ) );
        }
    }
}

void item_pocket::contents_info( std::vector<iteminfo> &info, int pocket_number,
                                 bool disp_pocket_number ) const
{
    if( is_forbidden() ) {
        return;
    }
    const std::string space = "  ";

    insert_separation_line( info );
    if( disp_pocket_number ) {
        if( sealable() ) {
            info.emplace_back( "DESCRIPTION", string_format( _( "<bold>%s pocket %d</bold>" ),
                               translated_sealed_prefix(),
                               pocket_number ) );
        } else if( is_ablative() && !contents.empty() ) {
            info.emplace_back( "DESCRIPTION", string_format( _( "<bold>Pocket %d</bold>: %s" ),
                               pocket_number, contents.front()->display_name() ) );
        } else {
            info.emplace_back( "DESCRIPTION", string_format( _( "<bold>Pocket %d</bold>" ),
                               pocket_number ) );
        }
    }
    if( contents.empty() ) {
        info.emplace_back( "DESCRIPTION", _( "This pocket is empty." ) );
        return;
    }
    if( sealed() ) {
        info.emplace_back( "DESCRIPTION", _( "This pocket is <info>sealed</info>." ) );
    }

    // NOLINTNEXTLINE(cata-translate-string-literal)
    const std::string cont_type_str = string_format( "{%d}CONTAINER", pocket_number );

    // NOLINTNEXTLINE(cata-translate-string-literal)
    const std::string arm_type_str = string_format( "{%d}ARMOR", pocket_number );

    if( is_ablative() ) {
        // if we have contents for an ablative pocket display the armor data
        if( !contents.empty() ) {
            const item &ablative_armor = *contents.front();
            info.emplace_back( arm_type_str, string_format( "%s%s", _( "Coverage:" ), space ), "",
                               iteminfo::no_newline,
                               ablative_armor.get_avg_coverage() );
            //~ (M)elee coverage
            info.emplace_back( arm_type_str, string_format( "%s%s%s", space, _( "(M):" ), space ), "",
                               iteminfo::no_newline,
                               ablative_armor.get_avg_coverage( item::cover_type::COVER_MELEE ) );
            //~ (R)anged coverage
            info.emplace_back( arm_type_str, string_format( "%s%s%s", space, _( "(R):" ), space ), "",
                               iteminfo::no_newline,
                               ablative_armor.get_avg_coverage( item::cover_type::COVER_RANGED ) );
            //~ (V)itals coverage
            info.emplace_back( arm_type_str, string_format( "%s%s%s", space, _( "(V):" ), space ), "",
                               iteminfo::no_flags,
                               ablative_armor.get_avg_coverage( item::cover_type::COVER_VITALS ) );

            info.back().bNewLine = true;

            // CDF: BN damage types are an enum, DDA walks damage_info_order::get_all( ABLATE )
            info.emplace_back( arm_type_str, _( "<bold>Protection</bold>: Bash: " ), "",
                               iteminfo::no_newline | iteminfo::is_decimal, ablative_armor.bash_resist() );
            info.emplace_back( arm_type_str, space + _( "Cut: " ), "",
                               iteminfo::no_newline | iteminfo::is_decimal, ablative_armor.cut_resist() );
            info.emplace_back( arm_type_str, space + _( "Ballistic: " ), "",
                               iteminfo::is_decimal, ablative_armor.bullet_resist() );
        }
    } else if( data->ammo_restriction.empty() ) {
        // With no ammo_restriction defined, show current volume/weight, and total capacity
        info.emplace_back( vol_to_info( cont_type_str, _( "Volume: " ),
                                        contents_volume() ) );
        info.emplace_back( vol_to_info( cont_type_str, _( " of " ),
                                        volume_capacity() ) );
        info.back().bNewLine = true;
        info.emplace_back( weight_to_info( cont_type_str, _( "Weight: " ),
                                           contains_weight() ) );
        info.emplace_back( weight_to_info( cont_type_str, _( " of " ),
                                           weight_capacity() ) );
        info.back().bNewLine = true;
    } else {
        // With ammo_restriction, total capacity does not matter, but show current volume/weight
        info.emplace_back( vol_to_info( cont_type_str, _( "Volume: " ),
                                        contents_volume() ) );
        info.back().bNewLine = true;
        info.emplace_back( weight_to_info( cont_type_str, _( "Weight: " ),
                                           contains_weight() ) );
    }

    if( remaining_volume() < 0_ml || remaining_weight() < 0_gram ) {
        info.emplace_back( "DESCRIPTION",
                           colorize( _( "This pocket is over capacity and will spill if moved!" ), c_red ) );
    }

    // ablative pockets have their contents displayed earlier in the UI
    if( !is_ablative() ) {
        const std::vector<std::pair<const item *, int>> counted_contents = get_item_duplicate_counts(
                    all_items_top() );
        bool contents_header = false;

        for( const std::pair<const item *, int> &content : counted_contents ) {
            const item *contents_item = content.first;
            const int count = content.second;

            if( !contents_header ) {
                info.emplace_back( "DESCRIPTION", _( "<bold>Contents of this pocket</bold>:" ) );
                contents_header = true;
            }

            const translation &desc = contents_item->type->description;

            if( contents_item->made_of_from_type( phase_id::LIQUID ) ) {
                info.emplace_back( "DESCRIPTION",
                                   colorize( space + contents_item->display_name(), contents_item->color_in_inventory() ) );
                info.emplace_back( vol_to_info( cont_type_str, desc + space, contents_item->volume() ) );
            } else {
                if( count > 1 ) {
                    info.emplace_back( "DESCRIPTION",
                                       space + std::to_string( count ) + " " + colorize( contents_item->display_name(
                                                   count ), contents_item->color_in_inventory() ) );
                } else {
                    info.emplace_back( "DESCRIPTION",
                                       space + colorize( contents_item->display_name(), contents_item->color_in_inventory() ) );
                }
            }
        }
    }
}

void item_pocket::favorite_info( std::vector<iteminfo> &info ) const
{
    settings.info( info );
}

namespace
{

// for soft containers check all content items to see if they all would fit
int charges_per_volume_recursive( const units::volume &max_item_volume, const item &it )
{
    int min_charges = item::INFINITE_CHARGES;

    if( !it.is_soft() ) {
        return it.charges_per_volume( max_item_volume );
    }

    for( const item *content : it.contents.all_items_top() ) {
        min_charges = std::min( min_charges, charges_per_volume_recursive( max_item_volume, *content ) );
        if( min_charges == 0 ) {
            return 0;
        }
    }

    return min_charges;
}

// Volume of one unit: one charge of a count-by-charges item, otherwise the whole item.
units::volume unit_volume( const item &it )
{
    if( it.count_by_charges() && it.charges > 0 ) {
        return it.volume() / it.charges;
    }
    return it.volume();
}

} // namespace

ret_val<item_pocket::contain_code> item_pocket::is_compatible( const item &it ) const
{
    if( data->type == pocket_type::MIGRATION ) {
        // migration pockets need to always succeed
        return ret_val<item_pocket::contain_code>::make_success();
    }

    if( data->type == pocket_type::MOD ) {
        if( it.is_toolmod() || it.is_gunmod() ) {
            return ret_val<item_pocket::contain_code>::make_success();
        } else {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_MOD, _( "only mods can go into mod pocket" ) );
        }
    }

    if( data->type == pocket_type::CORPSE ) {
        // corpses can't have items stored in them the normal way,
        // we simply don't want them to "spill"
        return ret_val<item_pocket::contain_code>::make_success();
    }

    if( it.has_flag( flag_NO_UNWIELD ) ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_MOD, _( "cannot unwield item" ) );
    }

    if( data->ablative && data->get_flag_restrictions().empty() && data->item_id_restriction.empty() ) {
        // CDF: a plate slot that names no plate takes nothing (check_definition reports it)
        return ret_val<item_pocket::contain_code>::make_failure( contain_code::ERR_FLAG,
                _( "holster does not accept this item type or form factor" ) );
    }

    // CDF: P-2 (plan P1f): holster pockets of items with the holster use action refuse active
    // items (DDA holster_actor::store); the BN skill gate is gone (D19)
    if( data->no_active_items && it.is_active() ) {
        return ret_val<item_pocket::contain_code>::make_failure( contain_code::ERR_FLAG,
                _( "it is active" ) );
    }

    if( !data->item_id_restriction.empty() || !data->get_flag_restrictions().empty() ||
        !data->material_restriction.empty() ) {
        if( data->item_id_restriction.count( it.typeId() ) == 0 &&
            !it.has_any_flag( data->get_flag_restrictions() ) &&
            !data->material_restriction.count( it.get_base_material().ident() ) ) {
            return ret_val<item_pocket::contain_code>::make_failure( contain_code::ERR_FLAG,
                    _( "holster does not accept this item type or form factor" ) );
        }
    }

    // ammo restriction overrides item volume/weight and watertight/airtight data
    if( !data->ammo_restriction.empty() ) {
        if( !it.is_ammo() ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_AMMO, _( "item is not ammunition" ) );
        }

        const auto ammo_restriction_iter = data->ammo_restriction.find( it.ammo_type() );

        if( ammo_restriction_iter == data->ammo_restriction.end() ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_AMMO, _( "item is not the correct ammo type" ) );
        }

        return ret_val<item_pocket::contain_code>::make_success();
    }

    if( it.made_of( phase_id::LIQUID ) ) {
        if( !data->watertight && !it.has_flag( flag_FROM_FROZEN_LIQUID ) ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_LIQUID, _( "can't contain liquid" ) );
        }
    }
    if( it.made_of( phase_id::GAS ) ) {
        if( !data->airtight ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_GAS, _( "can't contain gas" ) );
        }
    }

    // liquids and gases avoid the size limit altogether
    // soft items also avoid the size limit
    // count_by_charges items check volume of single charge
    if( !it.made_of( phase_id::LIQUID ) && !it.made_of( phase_id::GAS ) &&
        !it.is_frozen_liquid() && data->max_item_volume &&
        !charges_per_volume_recursive( *data->max_item_volume, it ) ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_TOO_BIG, _( "item is too big" ) );
    }
    if( it.length() > data->max_item_length ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_TOO_BIG, _( "item is too long" ) );
    }

    // CDF: item::length() is -1 mm for a volume-less item (a disintegrating ammo belt, DDA length
    // sentinel); that is "no size", not "too short", so it still fits a pocket whose minimum is 0.
    if( std::max( it.length(), 0_mm ) < data->min_item_length ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_TOO_SMALL, _( "item is too short" ) );
    }

    // CDF: DDA's `it.volume( false, false, 1 )` (volume of one charge) is unit_volume
    if( unit_volume( it ) < data->min_item_volume ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_TOO_SMALL, _( "item is too small" ) );
    }
    return ret_val<item_pocket::contain_code>::make_success();
}

ret_val<item_pocket::contain_code> item_pocket::can_contain( const item &it,
        int &copies_remaining, bool ignore_contents ) const
{
    return _can_contain( it, copies_remaining, ignore_contents );
}

ret_val<item_pocket::contain_code> item_pocket::can_contain( const item &it,
        bool ignore_contents ) const
{
    int copies = 1;
    return _can_contain( it, copies, ignore_contents );
}

ret_val<item_pocket::contain_code> item_pocket::_can_contain( const item &it,
        int &copies_remaining, const bool ignore_contents ) const
{
    ret_val<item_pocket::contain_code> compatible = is_compatible( it );

    if( copies_remaining <= 0 ) {
        return ret_val<item_pocket::contain_code>::make_success();
    }
    // To prevent debugmsg. Casings can only be inserted in a magazine during firing.
    if( data->type == pocket_type::MAGAZINE && it.has_flag( flag_CASING ) ) {
        copies_remaining = 0;
        return ret_val<item_pocket::contain_code>::make_success();
    }

    if( !compatible.success() ) {
        return compatible;
    }
    if( !is_standard_type() ) {
        copies_remaining = 0;
        return ret_val<item_pocket::contain_code>::make_success();
    }

    if( it.made_of( phase_id::LIQUID ) ) {
        if( size() != 0 && !front().can_combine( it ) && data->watertight ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_LIQUID, _( "can't mix liquid with contained item" ) );
        }
    } else if( size() == 1 && !it.is_frozen_liquid() &&
               front().made_of( phase_id::LIQUID ) && data->watertight ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_LIQUID, _( "can't put non liquid into pocket with liquid" ) );
    }

    if( it.is_frozen_liquid() ) {
        if( size() != 0 && !front().can_combine( it ) && data->watertight ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_LIQUID,
                       _( "can't mix frozen liquid with contained item in the watertight container" ) );
        }
    } else if( data->watertight ) {
        if( size() == 1 && front().is_frozen_liquid() && !front().can_combine( it ) ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_LIQUID,
                       _( "can't mix item with contained frozen liquid in the watertight container" ) );
        }
    }

    if( it.made_of( phase_id::GAS ) ) {
        if( size() != 0 && !front().can_combine( it ) ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_GAS, _( "can't mix gas with contained item" ) );
        }
    } else if( size() == 1 && front().made_of( phase_id::GAS ) ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_GAS, _( "can't put non gas into pocket with gas" ) );
    }

    if( ignore_contents ) {
        // Skip all the checks against other pocket contents.
        if( it.weight() > weight_capacity() ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_TOO_HEAVY, _( "item is too heavy" ) );
        }
        if( it.volume() > volume_capacity() ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_TOO_BIG, _( "item is too big" ) );
        }
        return ret_val<item_pocket::contain_code>::make_success();
    }

    if( data->type == pocket_type::MAGAZINE && !empty() ) {
        for( const item *contained : contents ) {
            if( contained->has_flag( flag_CASING ) ) {
                continue;
            } else if( contained->ammo_type() != it.ammo_type() ) {
                return ret_val<item_pocket::contain_code>::make_failure(
                           contain_code::ERR_NO_SPACE, _( "can't mix different ammo" ) );
            }
        }
    }

    if( data->ablative ) {
        if( !contents.empty() ) {
            if( front().can_combine( it ) ) {
                // Only items with charges can succeed here.
                return ret_val<item_pocket::contain_code>::make_success();
            } else {
                return ret_val<item_pocket::contain_code>::make_failure(
                           contain_code::ERR_NO_SPACE, _( "ablative pocket already contains a plate" ) );
            }
        }

        // D7: a plate that is rigid cannot go in while rigid armor is worn over its sub body parts
        if( it.is_rigid() ) {
            for( const sub_bodypart_id &sbp : it.get_covered_sub_body_parts() ) {
                if( it.is_bp_rigid( sbp ) && std::count( no_rigid.begin(), no_rigid.end(), sbp ) != 0 ) {
                    return ret_val<item_pocket::contain_code>::make_failure(
                               contain_code::ERR_NO_SPACE,
                               _( "ablative pocket is being worn with hard armor can't support hard plate" ) );
                }
            }
        }
        copies_remaining = std::max( 0, copies_remaining - 1 );
        return ret_val<item_pocket::contain_code>::make_success();
    }

    if( data->holster && !contents.empty() ) {
        if( front().can_combine( it ) ) {
            // Only items with charges can succeed here.
            return ret_val<item_pocket::contain_code>::make_success();
        } else {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_NO_SPACE, _( "holster already contains an item" ) );
        }
    }

    if( !data->ammo_restriction.empty() ) {
        const ammotype it_ammo = it.ammo_type();
        if( it.count() > remaining_ammo_capacity( it_ammo ) ) {
            return ret_val<item_pocket::contain_code>::make_failure(
                       contain_code::ERR_NO_SPACE, _( "tried to put too many charges of ammo in item" ) );
        }
        // ammo restriction overrides item volume/weight and watertight/airtight data
        copies_remaining = std::max( 0, copies_remaining - remaining_ammo_capacity( it_ammo ) );
        return ret_val<item_pocket::contain_code>::make_success();
    }

    units::mass weight = it.weight();
    if( weight > weight_capacity() ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_TOO_HEAVY, _( "item is too heavy" ) );
    }
    units::volume volume = it.volume();
    if( volume > volume_capacity() ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_TOO_BIG, _( "item is too big" ) );
    }

    int fallback_capacity = it.count_by_charges() ? it.charges : copies_remaining;
    int copy_weight_capacity = weight <= 0_gram ? fallback_capacity :
                               charges_per_remaining_weight( it );
    int copy_volume_capacity = volume <= 0_ml ? fallback_capacity :
                               charges_per_remaining_volume( it );

    if( copy_weight_capacity < it.count() ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_CANNOT_SUPPORT, _( "pocket is holding too much weight" ) );
    }
    if( copy_volume_capacity < it.count() ) {
        return ret_val<item_pocket::contain_code>::make_failure(
                   contain_code::ERR_NO_SPACE, _( "not enough space" ) );
    }

    if( copies_remaining > 1 ) {
        int how_many_copies_fit = std::min( { data->holster ? 1 : copies_remaining, copy_weight_capacity, copy_volume_capacity } );
        copies_remaining -= how_many_copies_fit;
    } else {
        copies_remaining = 0;
    }

    return ret_val<item_pocket::contain_code>::make_success();
}

bool item_pocket::can_contain_liquid( bool held_or_ground ) const
{
    if( held_or_ground ) {
        return data->watertight;
    } else {
        if( will_spill() ) {
            return false;
        }
        return data->watertight;
    }
}

bool item_pocket::contains_phase( phase_id phase ) const
{
    return !empty() && front().made_of( phase );
}

detached_ptr<item> item_pocket::remove_item( item *it )
{
    // CDF: T3, the removed item itself comes back (DDA copies it and erases the original)
    const std::vector<item *> &stored = contents.as_vector();
    if( std::find( stored.begin(), stored.end(), it ) == stored.end() ) {
        return detached_ptr<item>();
    }
    detached_ptr<item> ret = contents.remove( it );
    invalidate_cache();
    return ret;
}

namespace
{

// DDA item_pocket.cpp:1781, T4: the item is detached by the caller and either goes into a pocket
// of an item further up (`holder` is the item whose pocket it came out of) or to the ground; it
// never gets cloned. Returns what could not be placed anywhere.
detached_ptr<item> move_to_parent_pocket_recursive( const tripoint_bub_ms &pos,
        detached_ptr<item> &&it, item *holder, Character *carrier )
{
    // walk up through the containers holding `holder`
    while( holder != nullptr ) {
        item *grand = holder->parent_item();
        if( grand == nullptr ) {
            break;
        }
        item_pocket *parent_pocket = grand->contents.pocket_of( holder );
        if( parent_pocket != nullptr && parent_pocket->can_contain( *it ).success() ) {
            if( carrier ) {
                carrier->add_msg_if_player( m_bad, _( "Your %1$s falls into your %2$s." ),
                                            it->display_name(), grand->label( 1 ) );
            }
            // checked above, so this cannot refuse
            ret_val<item *> placed = parent_pocket->insert_item( std::move( it ) );
            if( !placed.success() ) {
                debugmsg( "pocket refused an item it said it could contain: %s", placed.str() );
            }
            return it;
        }
        holder = grand;
    }

    if( carrier ) {
        carrier->add_msg_player_or_npc( m_bad, _( "Your %s falls to the ground." ),
                                        _( "<npcname>'s %s falls to the ground." ), it->display_name() );
    } else {
        if( get_avatar().sees( pos ) ) {
            add_msg( m_bad, _( "The %s falls to the ground." ), it->display_name() );
        }
    }
    return get_map().add_item_or_charges( pos, std::move( it ) );
}

} // namespace

void item_pocket::overflow( const tripoint_bub_ms &pos, item *parent_item, Character *carrier )
{
    if( is_type( pocket_type::MOD ) || is_type( pocket_type::CORPSE ) ) {
        return;
    }
    if( empty() ) {
        // no items to overflow
        return;
    }

    // what could not be placed anywhere stays here (CDF: nothing is ever lost, T4)
    const auto shed = [&]( detached_ptr<item> &&det ) {
        detached_ptr<item> left = move_to_parent_pocket_recursive( pos, std::move( det ), parent_item,
                                  carrier );
        if( left ) {
            add( std::move( left ) );
        }
    };

    // overflow recursively
    for( item *it : std::vector<item *>( contents.as_vector() ) ) {
        it->contents.overflow( pos, carrier );
    }

    // first remove items that shouldn't be in there anyway
    std::unordered_map<itype_id, bool> contained_type_validity;
    for( auto iter = contents.begin(); iter != contents.end(); ) {

        // if item has any contents, check it individually
        if( !( *iter )->contents.empty() ) {
            if( !is_type( pocket_type::MIGRATION ) && can_contain( **iter, true ).success() ) {
                ++iter;
            } else {
                detached_ptr<item> det;
                iter = contents.erase( iter, &det );
                shed( std::move( det ) );
            }
            continue;
        }

        // otherwise, use cached results per item type
        auto cont_copy_type = contained_type_validity.emplace( ( *iter )->typeId(), true );
        if( cont_copy_type.second ) {
            cont_copy_type.first->second = !is_type( pocket_type::MIGRATION ) &&
                                           can_contain( **iter, true ).success();
        }
        if( cont_copy_type.first->second ) {
            ++iter;
        } else {
            detached_ptr<item> det;
            iter = contents.erase( iter, &det );
            shed( std::move( det ) );
        }
    }

    if( empty() ) {
        // we've removed the migration pockets and items that didn't fit
        invalidate_cache();
        return;
    }

    if( !data->ammo_restriction.empty() ) {
        const ammotype contained_ammotype = front().ammo_type();
        const auto ammo_iter = data->ammo_restriction.find( contained_ammotype );
        if( ammo_iter == data->ammo_restriction.end() ) {
            // only one ammotype is allowed in an ammo restricted pocket
            // so if the first one is wrong, they're all wrong
            spill_contents( pos );
            return;
        }
        int total_qty = 0;
        for( auto iter = contents.begin(); iter != contents.end(); ) {
            item &ammo = **iter;
            total_qty += ammo.count();
            const int overflow_count = total_qty - ammo_iter->second;
            if( overflow_count > 0 ) {
                // CDF: only the surplus leaves the pocket (DDA builds `dropped_ammo` and then
                // moves the whole stack)
                detached_ptr<item> dropped_ammo = item::spawn( ammo );
                dropped_ammo->charges = overflow_count;
                ammo.charges -= overflow_count;
                shed( std::move( dropped_ammo ) );
                total_qty -= overflow_count;
            }
            if( ammo.count() == 0 ) {
                iter = contents.erase( iter );
            } else {
                ++iter;
            }
        }
        // return early, the rest of this function checks against volume and weight
        // and ammo_restriction is an override
        invalidate_cache();
        return;
    }

    // largest first
    if( remaining_volume() < 0_ml ) {
        std::vector<item *> by_volume( contents.as_vector() );
        std::stable_sort( by_volume.begin(), by_volume.end(), []( const item * left, const item * right ) {
            return left->volume() > right->volume();
        } );
        for( item *largest : by_volume ) {
            if( remaining_volume() >= 0_ml ) {
                break;
            }
            shed( contents.remove( largest ) );
        }
    }
    if( remaining_weight() < 0_gram ) {
        std::vector<item *> by_weight( contents.as_vector() );
        std::stable_sort( by_weight.begin(), by_weight.end(), []( const item * left, const item * right ) {
            return left->weight() > right->weight();
        } );
        for( item *heaviest : by_weight ) {
            if( remaining_weight() >= 0_gram ) {
                break;
            }
            shed( contents.remove( heaviest ) );
        }
    }
    invalidate_cache();
}

void item_pocket::on_pickup( Character &guy, item *avoid )
{
    if( will_spill() ) {
        while( !empty() ) {
            handle_liquid_or_spill( guy, avoid );
        }
        restack();
    }
}

void item_pocket::on_contents_changed()
{
    unseal();
    restack();
    invalidate_cache();
}

bool item_pocket::spill_contents( const tripoint_bub_ms &pos )
{
    if( is_type( pocket_type::CORPSE ) ) {
        return false;
    }

    // CDF: T4, the items are detached and handed to the map, not cloned
    map &here = get_map();
    for( detached_ptr<item> &it : contents.clear() ) {
        here.add_item_or_charges( pos, std::move( it ) );
    }
    invalidate_cache();
    return true;
}

std::vector<detached_ptr<item>> item_pocket::clear_items()
{
    invalidate_cache();
    return contents.clear();
}

bool item_pocket::has_item( const item &it ) const
{
    return std::ranges::any_of( contents, [&it]( const item * e ) {
        return &it == e || std::ranges::any_of( e->contents.get_pockets(), [&it]( const item_pocket & p ) {
            return p.has_item( it );
        } );
    } );
}

item *item_pocket::get_item_with( const std::function<bool( const item & )> &filter )
{
    for( item *it : contents ) {
        if( filter( *it ) ) {
            return it;
        }
    }
    return nullptr;
}

const item *item_pocket::get_item_with( const std::function<bool( const item & )> &filter ) const
{
    for( const item *it : contents ) {
        if( filter( *it ) ) {
            return it;
        }
    }
    return nullptr;
}

void item_pocket::remove_items_if( const std::function<bool( item & )> &filter )
{
    contents.remove_with( [&filter]( detached_ptr<item> &&it ) {
        if( filter( *it ) ) {
            return detached_ptr<item>();
        }
        return std::move( it );
    } );
    on_contents_changed();
}

bool item_pocket::is_default_state() const
{
    return empty() && settings.is_null() && !sealable();
}

bool item_pocket::empty() const
{
    return contents.empty();
}

bool item_pocket::full( bool allow_bucket ) const
{
    if( contents.empty() ) {
        return false;
    }

    if( !allow_bucket && will_spill() ) {
        return true;
    }

    if( is_type( pocket_type::MAGAZINE ) && !data->ammo_restriction.empty() ) {
        // Fullness from remaining ammo capacity instead of volume
        for( const item *it : contents ) {
            if( it->has_flag( flag_CASING ) ) {
                continue;
            } else {
                return remaining_ammo_capacity( it->ammo_type() ) == 0;
            }
        }
        return false;
    }

    if( remaining_volume() == 0_ml ) {
        return true;
    }

    if( remaining_capacity_for_item( front() ) == 0 ) {
        bool has_only_one_type = true;
        // maybe there is a better way?
        for( const item *it : contents ) {
            if( it->typeId() != front().typeId() ) {
                has_only_one_type = false;
                break;
            }
        }
        return has_only_one_type;
    }

    return false;
}

bool item_pocket::rigid() const
{
    return data->rigid;
}

bool item_pocket::watertight() const
{
    return data->watertight;
}

bool item_pocket::transparent() const
{
    return data->transparent || ( data->open_container && !sealed() );
}

bool item_pocket::is_standard_type() const
{
    return data->type == pocket_type::CONTAINER ||
           data->type == pocket_type::MAGAZINE ||
           data->type == pocket_type::MAGAZINE_WELL;
}

bool item_pocket::is_container_like_type() const
{
    // CDF: no E_FILE_STORAGE pockets (E1)
    return is_standard_type();
}

bool item_pocket::is_forbidden() const
{
    return data->forbidden;
}

bool item_pocket::airtight() const
{
    return data->airtight;
}

bool item_pocket::inherits_flags() const
{
    return data->inherits_flags;
}

bool item_pocket::can_reload_with( const item &ammo, const bool now ) const
{
    if( is_type( pocket_type::CONTAINER ) ) {
        // Only watertight container pockets are reloadable
        if( !watertight() ) {
            return false;
        }

        // CONTAINER pockets can reload liquids only
        if( !ammo.made_of( phase_id::LIQUID ) ) {
            return false;
        }
    }

    if( ammo.has_flag( flag_SPEEDLOADER ) ) {
        // The speedloader needs to be compatible,
        // The ammo in it needs to be compatible,
        // and the opcket needs to have enough space (except casings)
        return allows_speedloader( ammo.typeId() ) &&
               is_compatible( ammo.loaded_ammo() ).success() &&
               ( remaining_ammo_capacity( ammo.loaded_ammo().ammo_type() ) >= ammo.ammo_remaining( ) );
    }

    if( ammo.has_flag( flag_SPEEDLOADER_CLIP ) ) {
        // The speedloader clip needs to be compatible,
        // The ammo in it needs to be compatible,
        // and the pocket don't needs have enough space (except casings(if any))
        return allows_speedloader( ammo.typeId() ) &&
               is_compatible( ammo.loaded_ammo() ).success() &&
               ( remaining_ammo_capacity( ammo.loaded_ammo().ammo_type() ) + ammo_capacity(
                     ammo.loaded_ammo().ammo_type() ) > ammo_capacity( ammo.loaded_ammo().ammo_type() ) );
    }

    if( !is_compatible( ammo ).success() ) {
        return false;
    }

    if( !now ) {
        return true;
    } else {
        // Require that the new ammo can fit in with the old ammo.

        // No need to combine if empty.
        if( empty() ) {
            return true;
        }

        if( is_type( pocket_type::MAGAZINE ) ) {
            // Reloading is refused if
            // Pocket is full of ammo (casings are ignored)
            // Ammos are of different ammo type
            // If either of ammo is liquid while the other is not or they are different types

            if( full( false ) ) {
                return false;
            }

            for( const item *loaded : all_items_top() ) {
                if( loaded->has_flag( flag_CASING ) ) {
                    continue;
                }
                if( loaded->ammo_type() != ammo.ammo_type() ) {
                    return false;
                }
                if( loaded->made_of( phase_id::LIQUID ) || ammo.made_of( phase_id::LIQUID ) ) {
                    bool cant_combine = !loaded->made_of( phase_id::LIQUID ) || !ammo.made_of( phase_id::LIQUID ) ||
                                        loaded->type != ammo.type;
                    if( cant_combine ) {
                        return false;
                    }
                }
            }

            return true;

        } else if( is_type( pocket_type::MAGAZINE_WELL ) ) {
            // Reloading is refused if there already is full magazine here
            // Reloading with another identical mag with identical contents is also pointless so it is not allowed
            // Pocket can't know what ammo are compatible with the item so that is checked elsewhere

            // CDF: no item::is_magazine_full(); the front item's contents carries it.
            return !front().contents.is_magazine_full() && !( front().typeId() == ammo.typeId() &&
                    front().same_contents( ammo ) );
        } else if( is_type( pocket_type::CONTAINER ) ) {
            // Reloading is possible if liquid combines with old liquid

            if( front().can_combine( ammo ) ) {
                return true;
            }
        }
    }
    return false;
}

bool item_pocket::allows_speedloader( const itype_id &speedloader_id ) const
{
    if( data->allowed_speedloaders.empty() ) {
        return false;
    } else {
        return data->allowed_speedloaders.count( speedloader_id );
    }
}

const pocket_data *item_pocket::get_pocket_data() const
{
    return data;
}

void item_pocket::add( detached_ptr<item> &&it, item **ret )
{
    if( !it ) {
        return;
    }
    contents.push_back( std::move( it ) );
    if( ret == nullptr ) {
        restack();
    } else {
        *ret = restack( contents.back() );
    }
    invalidate_cache();
}

void item_pocket::add( const item &it, item **ret )
{
    add( item::spawn( it ), ret );
}

void item_pocket::add( const item &it, const int copies, std::vector<item *> &added )
{
    for( int i = 0; i < copies; i++ ) {
        contents.push_back( item::spawn( it ) );
        added.push_back( contents.back() );
    }
    invalidate_cache();
}

void item_pocket::insert( detached_ptr<item> &&it )
{
    if( !it ) {
        return;
    }
    if( it->count_by_charges() ) {
        for( item *check : contents ) {
            // NOLINTNEXTLINE(bugprone-use-after-move)
            if( check->merge_charges( std::move( it ) ) ) {
                invalidate_cache();
                return;
            }
        }
    }
    // NOLINTNEXTLINE(bugprone-use-after-move)
    contents.push_back( std::move( it ) );
    invalidate_cache();
}

bool item_pocket::can_unload_liquid() const
{
    if( contents.size() != 1 ) {
        return true;
    }

    const item &cts = front();
    bool cts_is_frozen_liquid = cts.made_of_from_type( phase_id::LIQUID ) &&
                                cts.made_of( phase_id::SOLID );
    return will_spill() || !cts_is_frozen_liquid;
}

int item_pocket::fill_with( const item &contained, Character &guy, int amount,
                            bool allow_unseal, bool ignore_settings )
{
    int num_contained = 0;

    if( !contained.count_by_charges() || amount <= 0 ) {
        return 0;
    }
    if( !ignore_settings && !settings.accepts_item( contained ) ) {
        return 0;
    }
    if( !allow_unseal && sealed() ) {
        return 0;
    }

    detached_ptr<item> contained_item = item::spawn( contained );
    ammotype ammo = contained.ammo_type();
    if( ammo_capacity( ammo ) ) {
        contained_item->charges = std::min( amount - num_contained,
                                            remaining_ammo_capacity( ammo ) );
    } else {
        contained_item->charges = std::min( { amount - num_contained,
                                              charges_per_remaining_volume( *contained_item ),
                                              charges_per_remaining_weight( *contained_item )
                                            } );
    }
    if( contained_item->charges == 0 ) {
        return 0;
    }

    contained_item->handle_pickup_ownership( guy );
    const int added_charges = contained_item->charges;
    ret_val<item *> result = insert_item( std::move( contained_item ) );
    if( !result.success() ) {
        debugmsg( "charges per remaining pocket volume does not fit in that very volume" );
        return 0;
    }

    num_contained += added_charges;
    if( allow_unseal ) {
        unseal();
    }
    result.value()->on_pickup( guy );
    return num_contained;
}

ret_val<item *> item_pocket::insert_item( detached_ptr<item> &&it, const bool into_bottom,
        bool restack_charges, bool ignore_contents )
{
    if( !it ) {
        return ret_val<item *>::make_failure( nullptr, "no item" );
    }
    ret_val<item_pocket::contain_code> containable = can_contain( *it, ignore_contents );

    if( !containable.success() ) {
        return ret_val<item *>::make_failure( nullptr, containable.str() );
    }

    const bool by_charges = it->count_by_charges();
    item *inserted = nullptr;
    if( !into_bottom ) {
        contents.insert( contents.begin(), std::move( it ) );
        inserted = contents.front();
    } else {
        contents.push_back( std::move( it ) );
        inserted = contents.back();
    }
    if( restack_charges && by_charges ) {
        inserted = restack( inserted );
    }
    invalidate_cache();
    return ret_val<item *>::make_success( inserted );
}

ret_val<item *> item_pocket::insert_item( const item &it, const bool into_bottom,
        bool restack_charges, bool ignore_contents )
{
    // CDF: T2, the DDA spelling inserts a copy (identity is lost, never use it to move an item)
    return insert_item( item::spawn( it ), into_bottom, restack_charges, ignore_contents );
}

pocket_choice item_pocket::best_pocket_in_contents( item &this_item, const item &it,
        const item *avoid, const bool allow_sealed, const bool ignore_settings )
{
    pocket_choice ret{ &this_item, nullptr };
    // If the current pocket has restrictions or blacklists the item or is a holster,
    // try the nested pocket regardless of whether it's soft or rigid.
    const bool ignore_rigidity =
        !settings.accepts_item( it ) ||
        !get_pocket_data()->get_flag_restrictions().empty() ||
        settings.priority() > 0 || is_holster();

    for( item *contained_item : contents ) {
        if( contained_item == &it || contained_item == avoid ) {
            continue;
        }
        pocket_choice nested_pocket = contained_item->best_pocket( it, avoid, allow_sealed,
                                      ignore_settings, /*nested=*/true, ignore_rigidity );
        if( nested_pocket.pocket != nullptr &&
            ( ret.pocket == nullptr ||
              ret.pocket->better_pocket( *nested_pocket.pocket, it, /*nested=*/true ) ) ) {
            ret = nested_pocket;
        }
    }
    return ret;
}

int item_pocket::obtain_cost( const item &it ) const
{
    if( has_item( it ) ) {
        return moves();
    }
    return 0;
}

bool item_pocket::is_type( pocket_type ptype ) const
{
    return ptype == data->type;
}

bool item_pocket::is_type( const std::set<pocket_type> &ptype ) const
{
    return ptype.count( data->type ) == 1;
}

bool item_pocket::is_ablative() const
{
    return get_pocket_data()->ablative;
}

bool item_pocket::is_holster() const
{
    return get_pocket_data()->holster;
}

const translation &item_pocket::get_description() const
{
    return get_pocket_data()->description;
}

const translation &item_pocket::get_name() const
{
    return get_pocket_data()->name;
}

bool item_pocket::holster_full() const
{
    const pocket_data *p_data = get_pocket_data();
    return p_data->holster && !contents.empty();
}

units::length item_pocket::max_containable_length() const
{
    if( data ) {
        return data->max_item_length;
    }
    return 0_mm;
}

units::length item_pocket::min_containable_length() const
{
    if( data ) {
        return data->min_item_length;
    }
    return 0_mm;
}

void item_pocket::refresh_cache() const
{
    // Cache key: item count plus the sum of item pointers. This catches inserts and removals
    // by any route (location_vector access, visitable removal, item locations) without a
    // call. It does NOT catch in-place size changes: every mutation route must call
    // item_contents::contents_changed() (which invalidates this cache) for those.
    uintptr_t signature = 0;
    for( const item *it : contents ) {
        signature += reinterpret_cast<uintptr_t>( it );
    }
    const size_t count = contents.size();
    if( cache.state != size_cache::state_t::NONE && cache.count == count &&
        cache.signature == signature ) {
        return;
    }
    const bool stable = std::ranges::all_of( contents, []( const item * it ) {
        return it->size_is_stable();
    } );
    cache = size_cache();
    cache.state = stable ? size_cache::state_t::STABLE : size_cache::state_t::UNSTABLE;
    cache.count = count;
    cache.signature = signature;
}

units::volume item_pocket::contents_volume() const
{
    // CDF: the P1d size cache stands in for DDA's bulk-fill running total
    if( contents.empty() ) {
        return 0_ml;
    }
    refresh_cache();
    if( cache.state == size_cache::state_t::STABLE && cache.volume_known ) {
        return cache.volume;
    }
    units::volume vol = 0_ml;
    for( const item *it : contents ) {
        vol += it->volume();
    }
    if( cache.state == size_cache::state_t::STABLE ) {
        cache.volume = vol;
        cache.volume_known = true;
    }
    return vol;
}

units::mass item_pocket::contains_weight() const
{
    if( contents.empty() ) {
        return 0_gram;
    }
    refresh_cache();
    if( cache.state == size_cache::state_t::STABLE && cache.weight_known ) {
        return cache.weight;
    }
    units::mass wt = 0_gram;
    for( const item *it : contents ) {
        wt += it->weight();
    }
    if( cache.state == size_cache::state_t::STABLE ) {
        cache.weight = wt;
        cache.weight_known = true;
    }
    return wt;
}

void item_pocket::begin_bulk_fill()
{
    // CDF: no-op (DEVIATION, perf only): the P1d size cache covers the repeated sums
}

void item_pocket::end_bulk_fill()
{
}

units::mass item_pocket::remaining_weight() const
{
    return weight_capacity() - contains_weight();
}

int item_pocket::charges_per_remaining_volume( const item &it ) const
{
    if( !it.count_by_charges() ) {
        return it.charges_per_volume( remaining_volume(), true );
    }
    // Single pass: subtract the volume of every item that doesn't stack with
    // `it`, and tally charges of those that do. When nothing stacks this
    // reduces to volume_capacity() - contents_volume() == remaining_volume().
    units::volume non_it_volume = volume_capacity();
    int contained_charges = 0;
    for( const item *contained : contents ) {
        if( contained->typeId() == it.typeId() && contained->stacks_with( it ) ) {
            contained_charges += contained->charges;
        } else {
            non_it_volume -= contained->volume();
        }
    }
    return it.charges_per_volume( non_it_volume, true ) - contained_charges;
}

int item_pocket::charges_per_remaining_weight( const item &it ) const
{
    if( !it.count_by_charges() ) {
        return it.charges_per_weight( remaining_weight(), true );
    }
    // Single pass: subtract the weight of every item that doesn't stack with
    // `it`, and tally charges of those that do. When nothing stacks this
    // reduces to weight_capacity() - contains_weight() == remaining_weight().
    units::mass non_it_weight = weight_capacity();
    int contained_charges = 0;
    for( const item *contained : contents ) {
        if( contained->typeId() == it.typeId() && contained->stacks_with( it ) ) {
            contained_charges += contained->charges;
        } else {
            non_it_weight -= contained->weight();
        }
    }
    return it.charges_per_weight( non_it_weight, true ) - contained_charges;
}

int item_pocket::best_quality( const quality_id &id ) const
{
    int ret = 0;
    for( const item *it : contents ) {
        ret = std::max( ret, it->get_quality( id ) );
    }
    return ret;
}

void item_pocket::set_no_rigid( const std::set<sub_bodypart_id> &is_no_rigid )
{
    no_rigid = is_no_rigid;
}

bool item_pocket::ok_all_containers( const item_pocket &pocket )
{
    return pocket.is_type( pocket_type::CONTAINER );
}

bool item_pocket::ok_default_containers( const item_pocket &pocket )
{
    return pocket.is_type( pocket_type::CONTAINER )
           && !pocket.is_forbidden();
}

bool item_pocket::ok_for_solids( const item_pocket &pocket )
{
    return pocket.empty() || pocket.contains_phase( phase_id::SOLID );
}

void item_pocket::favorite_settings::clear()
{
    preset_name = std::nullopt;
    priority_rating = 0;
    item_whitelist.clear();
    item_blacklist.clear();
    category_whitelist.clear();
    category_blacklist.clear();
}

void item_pocket::favorite_settings::set_priority( const int priority )
{
    priority_rating = priority;
}

bool item_pocket::favorite_settings::is_null() const
{
    return item_whitelist.empty() && item_blacklist.empty() &&
           category_whitelist.empty() && category_blacklist.empty() &&
           priority() == 0 && !collapsed && !disabled && unload;
}

void item_pocket::favorite_settings::whitelist_item( const itype_id &id )
{
    // whitelisting twice removes the item from the list
    if( item_whitelist.count( id ) ) {
        item_whitelist.erase( id );
        return;
    }
    // remove the item from the blacklist if listed
    if( item_blacklist.count( id ) ) {
        item_blacklist.erase( id );
    }
    item_whitelist.insert( id );
}

void item_pocket::favorite_settings::blacklist_item( const itype_id &id )
{
    // blacklisting twice removes the item from the list
    if( item_blacklist.count( id ) ) {
        item_blacklist.erase( id );
        return;
    }
    // remove the item from the whitelist if listed
    if( item_whitelist.count( id ) ) {
        item_whitelist.erase( id );
    }
    item_blacklist.insert( id );
}

void item_pocket::favorite_settings::clear_item( const itype_id &id )
{
    item_whitelist.erase( id );
    item_blacklist.erase( id );
}

const cata::flat_set<itype_id> &item_pocket::favorite_settings::get_item_whitelist() const
{
    return item_whitelist;
}

const cata::flat_set<itype_id> &item_pocket::favorite_settings::get_item_blacklist() const
{
    return item_blacklist;
}

const cata::flat_set<item_category_id> &
item_pocket::favorite_settings::get_category_whitelist() const
{
    return category_whitelist;
}

const cata::flat_set<item_category_id> &
item_pocket::favorite_settings::get_category_blacklist() const
{
    return category_blacklist;
}

void item_pocket::favorite_settings::whitelist_category( const item_category_id &id )
{
    // whitelisting twice removes the category from the list
    if( category_whitelist.count( id ) ) {
        category_whitelist.erase( id );
        return;
    }
    // remove the category from the blacklist if listed
    if( category_blacklist.count( id ) ) {
        category_blacklist.erase( id );
    }
    category_whitelist.insert( id );
}

void item_pocket::favorite_settings::blacklist_category( const item_category_id &id )
{
    // blacklisting twice removes the category from the list
    if( category_blacklist.count( id ) ) {
        category_blacklist.erase( id );
        return;
    }
    // remove the category from the whitelist if listed
    if( category_whitelist.count( id ) ) {
        category_whitelist.erase( id );
    }
    category_blacklist.insert( id );
}

void item_pocket::favorite_settings::clear_category( const item_category_id &id )
{
    category_blacklist.erase( id );
    category_whitelist.erase( id );
}

/**
 * Check to see if the given item is accepted by this pocket based on player configured rules.
 *
 * The rules are defined in two list:
 *
 * - whitelist - when one or more items are on the list then only those items are accepted.
 * - blacklist - those items listed here are never accepted.
 *
 * When a whitelist is empty and the item is not blacklisted then it will be accepted.
 * Container items will be accepted only when all items inside are accepted.
 *
 * Note that the rules take into account both item id and category which have
 * to be accepted by the rules outlined above for an item to be accepted.
 *
 * @param it item to check.
 * @return true if the item is accepted by this pocket, false otherwise.
 * @see item_pocket::favorite_settings::accepts_container(const item&)
 */
bool item_pocket::favorite_settings::accepts_item( const item &it ) const
{
    // if this pocket is disabled it accepts nothing
    if( disabled ) {
        return false;
    }
    const itype_id &id = it.typeId();
    const item_category_id &cat = category_blacklist.empty() && category_whitelist.empty()
                                  ? item_category_id{} :
                                  it.get_category_of_contents().get_id();

    // if the item is explicitly listed in either of the lists, then it's clear what to do with it
    if( item_blacklist.count( id ) ) {
        return false;
    }
    if( item_whitelist.count( id ) ) {
        return true;
    }

    // otherwise check the category, the same way
    if( category_blacklist.count( cat ) ) {
        return false;
    }
    if( category_whitelist.count( cat ) ) {
        return true;
    }

    // when the item is container then we are actually checking if pocket accepts
    // container content and not the container itself unless container is blacklisted
    if( it.is_container() && !it.is_container_empty() ) {
        const std::vector<item *> &items = it.contents.all_items_top();
        return std::ranges::all_of( items, [this]( const item * content ) {
            return accepts_item( *content );
        } );
    }
    // finally, if no match was found, see if there were any filters at all,
    // and either allow or deny everything that's fallen through to here.
    if( !category_whitelist.empty() ) {
        return false;  // we've whitelisted only some categories, and this item is not out of those.
    }
    if( !item_whitelist.empty() && category_blacklist.empty() ) {
        // whitelisting only certain items, and not as a means to tweak blacklist.
        return false;
    }
    // no whitelist - everything goes.
    return true;
}

std::vector<item_pocket::favorite_settings> item_pocket::pocket_presets;

// per DDA item_pocket.cpp:2460-2541, I/O through BN's read_from_file_json / write_to_file
void item_pocket::add_preset( const favorite_settings &preset )
{
    pocket_presets.emplace_back( preset );
    save_presets();
}

void item_pocket::save_presets()
{
    write_to_file( PATH_INFO::pocket_presets(), [&]( std::ostream & fout ) {
        JsonOut jout( fout, true );
        serialize_presets( jout );
    }, _( "pocket preset configuration" ) );
}

bool item_pocket::has_preset( const std::string &s )
{
    return find_preset( s ) != pocket_presets.end();
}

std::vector<item_pocket::favorite_settings>::iterator item_pocket::find_preset(
    const std::string &s )
{
    return std::find_if( pocket_presets.begin(), pocket_presets.end(),
    [&s]( const favorite_settings & preset ) {
        return preset.get_preset_name().value() == s;
    } );
}

void item_pocket::delete_preset( const std::vector<favorite_settings>::iterator iter )
{
    pocket_presets.erase( iter );
    save_presets();
}

void item_pocket::load_presets()
{
    read_from_file_json( PATH_INFO::pocket_presets(), []( JsonIn & jsin ) {
        deserialize_presets( jsin.get_array() );
    }, true );
}

void item_pocket::serialize_presets( JsonOut &json )
{
    json.start_array();
    for( const favorite_settings &preset : pocket_presets ) {
        preset.serialize( json );
    }
    json.end_array();
}

void item_pocket::deserialize_presets( const JsonArray &ja )
{
    pocket_presets.clear();
    for( JsonObject jo : ja ) {
        favorite_settings preset;
        preset.deserialize( jo );
        pocket_presets.emplace_back( preset );
    }
}

bool item_pocket::favorite_settings::is_collapsed() const
{
    return collapsed;
}

void item_pocket::favorite_settings::set_collapse( bool flag )
{
    collapsed = flag;
}

bool item_pocket::favorite_settings::is_disabled() const
{
    return disabled;
}

void item_pocket::favorite_settings::set_disabled( bool flag )
{
    disabled = flag;
}

bool item_pocket::favorite_settings::is_unloadable() const
{
    return unload;
}

void item_pocket::favorite_settings::set_unloadable( bool flag )
{
    unload = flag;
}

void item_pocket::favorite_settings::set_preset_name( const std::string &s )
{
    preset_name = s;
}

void item_pocket::favorite_settings::set_was_edited()
{
    player_edited = true;
}

bool item_pocket::favorite_settings::was_edited() const
{
    return player_edited;
}

const std::optional<std::string> &item_pocket::favorite_settings::get_preset_name() const
{
    return preset_name;
}

namespace
{

template<typename T>
std::string enumerate( const cata::flat_set<T> &container )
{
    std::vector<std::string> output;
    for( const T &id : container ) {
        output.push_back( id.c_str() );
    }
    return enumerate_as_string( output );
}

} // namespace

void item_pocket::favorite_settings::info( std::vector<iteminfo> &info ) const
{
    if( disabled ) {
        info.emplace_back( "BASE", string_format(
                               _( "Items <bad>won't be inserted</bad> into this pocket unless you manually insert them." ) ) );
    }
    if( !unload ) {
        info.emplace_back( "BASE", string_format(
                               _( "Items in this pocket <bad>won't be unloaded</bad> unless you manually drop them." ) ) );
    }
    if( preset_name.has_value() ) {
        info.emplace_back( "BASE", string_format( _( "Preset Name: %s" ), preset_name.value() ) );
    }

    info.emplace_back( "BASE", string_format( "%s %d", _( "Priority:" ), priority_rating ) );
    info.emplace_back( "BASE", string_format( _( "Item Whitelist: %s" ),
                       item_whitelist.empty() ? _( "(empty)" ) :
    enumerate_as_string( item_whitelist.begin(), item_whitelist.end(), []( const itype_id & id ) {
        return id->nname( 1 );
    } ) ) );
    info.emplace_back( "BASE", string_format( _( "Item Blacklist: %s" ),
                       item_blacklist.empty() ? _( "(empty)" ) :
    enumerate_as_string( item_blacklist.begin(), item_blacklist.end(), []( const itype_id & id ) {
        return id->nname( 1 );
    } ) ) );
    info.emplace_back( "BASE", string_format( _( "Category Whitelist: %s" ),
                       category_whitelist.empty() ? _( "(empty)" ) : enumerate( category_whitelist ) ) );
    info.emplace_back( "BASE", string_format( _( "Category Blacklist: %s" ),
                       category_blacklist.empty() ? _( "(empty)" ) : enumerate( category_blacklist ) ) );
}

void pocket_constraint::constrain_by( const item_pocket *outer )
{
    if( !in_rigid ) {
        volume_capacity = std::min( volume_capacity, outer->volume_capacity() );
        units::volume rem_vol = outer->remaining_volume();
        remaining_volume = std::min( remaining_volume, rem_vol );
        is_dominated = is_dominated || (
                           remaining_volume <= rem_vol
                           && !outer->is_restricted()
                           && !outer->is_holster() //if we're in a holster, the holster can't contain anything else, so can't dominate
                           // NB: we could check if white/blacklists allow a superset of items, but this seems reasonable
                           && outer->settings.get_category_blacklist().empty()
                           && outer->settings.get_item_blacklist().empty()
                           && outer->settings.get_category_whitelist().empty()
                           && outer->settings.get_item_whitelist().empty()
                       );
    }
    in_rigid |= outer->rigid();
    max_containable_length = std::min( max_containable_length, outer->max_containable_length() );
    if( outer->get_pocket_data()->max_item_volume.has_value() ) {
        max_item_volume = ( max_item_volume == 0_ml ) ? outer->get_pocket_data()->max_item_volume.value() :
                          std::min( outer->get_pocket_data()->max_item_volume.value(), max_item_volume );
    }
}

void pocket_constraint::constrain_by( const pocket_constraint &outer )
{
    if( !in_rigid ) {
        volume_capacity = std::min( volume_capacity, outer.volume_capacity );
        units::volume rem_vol = outer.remaining_volume;
        remaining_volume = std::min( remaining_volume, rem_vol );
    }
    in_rigid |= outer.in_rigid;
    max_containable_length = std::min( max_containable_length, outer.max_containable_length );
    if( outer.max_item_volume > 0_ml ) {
        max_item_volume = ( max_item_volume == 0_ml ) ? outer.max_item_volume : std::min(
                              outer.max_item_volume, max_item_volume );
    }
}

pocket_constraint::pocket_constraint( const item_pocket *pocket ) : pocket_constraint{}
{
    if( pocket != nullptr ) {
        constrain_by( pocket );
    }
    is_dominated = false;
}

bool pocket_constraint::operator==( const pocket_constraint &o ) const
{
    return weight_capacity == o.weight_capacity
           && max_containable_length == o.max_containable_length
           && volume_capacity == o.volume_capacity
           && remaining_volume == o.remaining_volume
           && max_item_volume == o.max_item_volume;
}

bool pocket_constraint::operator!=( const pocket_constraint &o ) const
{
    return !( *this == o );
}
