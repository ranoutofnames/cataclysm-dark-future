// Character storage: where items go when picked up, what fits, and what it costs.
// Transplanted from DDA src/character_inventory.cpp (DDA commit b4b62254ae2) and, for the
// stash helpers, src/character_attire.cpp (outfit::add_stash, get_all_pocket_with_parent).
// Adapted to BN's ownership model (rules T1-T12 of orchestration/plans/P1-transplant/COMMON.md):
//  * item_location handles become `item *`; the parent is `item::parent_item()`.
//  * insert paths take `detached_ptr<item> &&` and only consume it on success.
// Every difference from DDA is tagged `// CDF:`.
#include "character.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "activity_handlers.h"
#include "bodypart.h"
#include "calendar.h"
#include "character_encumbrance.h"
#include "character_martial_arts.h"
#include "debug.h"
#include "effect.h"
#include "flag.h"
#include "game_constants.h"
#include "item.h"
#include "item_contents.h"
#include "item_parent_checks.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "messages.h"
#include "npc.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "profile.h"
#include "ret_val.h"
#include "skill.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"

static const efftype_id effect_grabbed( "grabbed" );

static const trait_id trait_DEBUG_STORAGE( "DEBUG_STORAGE" );

static const itype_id itype_e_handcuffs( "e_handcuffs" );

// ---------------------------------------------------------------------------------------------
// Wielded and worn items
// ---------------------------------------------------------------------------------------------

std::vector<item *> Character::top_items() const
{
    std::vector<item *> ret;
    ret.reserve( worn.size() );
    for( const item * const &worn_it : worn ) {
        ret.push_back( const_cast<item *>( worn_it ) );
    }
    return ret;
}

units::length Character::max_single_item_length() const
{
    units::length ret = primary_weapon().is_null() ? 0_mm :
                        primary_weapon().max_containable_length();
    for( const item * const &worn_it : worn ) {
        ret = std::max( ret, worn_it->max_containable_length() );
    }
    return ret;
}

units::volume Character::max_single_item_volume() const
{
    units::volume ret = primary_weapon().is_null() ? 0_ml :
                        primary_weapon().max_containable_volume();
    for( const item * const &worn_it : worn ) {
        ret = std::max( ret, worn_it->max_containable_volume() );
    }
    return ret;
}

// ---------------------------------------------------------------------------------------------
// best_pocket and the add family
// ---------------------------------------------------------------------------------------------

pocket_choice Character::best_pocket( const item &it, const item *avoid, bool ignore_settings ) const
{
    pocket_choice ret;
    // CDF: the pockets are the character's own items', so the const_cast only hands out what
    // the const method promises not to change itself.
    item &weapon = primary_weapon();
    if( !weapon.is_null() && &weapon != &it && &weapon != avoid ) {
        ret = weapon.best_pocket( it, avoid, false, ignore_settings );
    }
    const auto consider = [&]( item & holder ) {
        if( &holder == &it || &holder == avoid ) {
            return;
        }
        const pocket_choice internal_pocket = holder.best_pocket( it, avoid, false, ignore_settings );
        if( internal_pocket.pocket != nullptr &&
            ( ret.pocket == nullptr || ret.pocket->better_pocket( *internal_pocket.pocket, it ) ) ) {
            ret = internal_pocket;
        }
    };
    for( const item * const &worn_it : worn ) {
        consider( *const_cast<item *>( worn_it ) );
    }
    // CDF: the body storage (bionic and shell compartments) comes after the worn items
    if( item *storage = get_body_storage() ) {
        consider( *storage );
    }
    return ret;
}

ret_val<item *> Character::try_add( detached_ptr<item> &&it, const item *avoid,
                                    const item *original_inventory_item, const bool allow_wield,
                                    bool ignore_pkt_settings )
{
    ( void )original_inventory_item; // CDF: items keep their identity, so they keep their invlet
    if( !it ) {
        return ret_val<item *>::make_failure( nullptr, "no item" );
    }
    invalidate_inventory_validity_cache();
    // CDF: the pocket choice, invlets and insertion are shared with inv_add_item
    ret_val<item *> res = add_to_pocket( std::move( it ), avoid, false, true, ignore_pkt_settings );
    if( res.success() && res.value() != nullptr ) {
        last_item = res.value()->typeId();
        res.value()->on_pickup( *this );
        clear_npc_ai_info_cache( npc_ai_info::reloadables );
        clear_npc_ai_info_cache( npc_ai_info::reloadable_cbms );
        return res;
    }
    if( it ) {
        last_item = it->typeId();
        if( allow_wield && !has_weapon() ) {
            // the item is only consumed if the hands take it
            it = wield( std::move( it ) );
            if( !it ) {
                clear_npc_ai_info_cache( npc_ai_info::reloadables );
                clear_npc_ai_info_cache( npc_ai_info::reloadable_cbms );
                return ret_val<item *>::make_success( &primary_weapon() );
            }
        }
    }
    return res;
}

// Remaining depth of containers inside @p it, DDA item_location.cpp:1215.
static int dfs_nesting( const item *it )
{
    int max_nest = 0;
    for( const item *son : it->all_items_top() ) {
        max_nest = std::max( max_nest, 1 + dfs_nesting( son ) );
    }
    return max_nest;
}

// DDA item_location::parents_can_contain_recursive (item_location.cpp:1224), with the location of
// the container named by @p owner (the item holding the pocket the item would go into).
ret_val<void> parents_can_contain_recursive( const item &owner, const item &it )
{
    units::mass it_weight = it.weight();
    units::volume it_volume = it.volume();
    units::length it_length = it.length();
    const item *current = &owner;
    // The pocket of the innermost container is not known to the location: DDA uses its first
    // pocket for the weight and volume multipliers.
    const std::vector<const item_pocket *> standard = current->get_standard_pockets();
    if( standard.empty() ) {
        return ret_val<void>::make_success();
    }
    const item_pocket *current_pocket = standard.front();
    const pocket_data *current_pocket_data = current_pocket->get_pocket_data();

    int recursion_depth = dfs_nesting( &it );
    // Repeat until top-most container reached
    while( current->parent_item() != nullptr ) {
        ++recursion_depth;
        const item *parent = current->parent_item();
        const item_pocket *parent_pocket = parent->contained_where( *current );
        if( parent_pocket == nullptr ) {
            break;
        }

        // Ignore volume and length after innermost rigid container
        if( current_pocket->rigid() ) {
            it_volume = 0_ml;
            it_length = 0_mm;
        }

        // Multiply weight and volume multipliers for each container
        it_weight = it_weight * current_pocket_data->weight_multiplier;
        it_volume = it_volume * current_pocket_data->volume_multiplier;
        it_length = it_length * std::cbrt( current_pocket_data->volume_multiplier );

        if( it_weight > parent_pocket->remaining_weight() ) {
            return ret_val<void>::make_failure( _( "item is too heavy for a parent pocket" ) );
        } else if( it_volume > parent_pocket->remaining_volume() ) {
            return ret_val<void>::make_failure( _( "item is too big for a parent pocket" ) );
        } else if( it_length > parent_pocket->get_pocket_data()->max_item_length ) {
            return ret_val<void>::make_failure( _( "item is too long for a parent pocket" ) );
        }

        // Move up one level of containers
        current_pocket = parent_pocket;
        current_pocket_data = current_pocket->get_pocket_data();
        current = parent;
    }
    // Deep nesting would lead to stack overflow error on loading
    if( recursion_depth > 10 ) {
        return ret_val<void>::make_failure( _( "containers are nested too deeply" ) );
    }
    return ret_val<void>::make_success();
}

// DDA item_location::max_charges_by_parent_recursive (item_location.cpp:1273) for the container
// @p owner; `weight_capacity()` and `volume_capacity()` of the DDA location are the room left in
// the pocket that holds @p owner.
ret_val<int> max_charges_by_parent_recursive( const item &owner, const item &it )
{
    if( owner.parent_item() == nullptr ) {
        return ret_val<int>::make_success( item::INFINITE_CHARGES );
    }

    float weight_multiplier = 1.0f;
    float volume_multiplier = 1.0f;
    units::mass max_weight = 1000_kilogram;
    units::volume max_volume = pocket_data::max_volume_for_container;
    const item *current = &owner;
    const std::vector<const item_pocket *> standard = current->get_standard_pockets();
    if( standard.empty() ) {
        return ret_val<int>::make_success( item::INFINITE_CHARGES );
    }
    const item_pocket *current_pocket = standard.front();
    const pocket_data *current_pocket_data = current_pocket->get_pocket_data();

    // Repeat until top-most container reached
    while( current->parent_item() != nullptr ) {
        const item *parent = current->parent_item();
        const item_pocket *parent_pocket = parent->contained_where( *current );
        if( parent_pocket == nullptr ) {
            break;
        }
        // Multiply weight and volume multipliers for each container
        weight_multiplier = weight_multiplier * current_pocket_data->weight_multiplier;
        volume_multiplier = volume_multiplier * current_pocket_data->volume_multiplier;
        // Inserting into rigid pockets will not affect parent volume so stop keeping track
        if( current_pocket->rigid() ) {
            volume_multiplier = 0.0f;
        }

        // Weight multiplier of zero means parent containers are no longer affected
        if( weight_multiplier > 0 ) {
            const units::mass temp_weight = parent_pocket->remaining_weight() / weight_multiplier;
            max_weight = std::min( max_weight, temp_weight );
        }
        // Volume multiplier of zero means parent containers are no longer affected
        if( volume_multiplier > 0 ) {
            const units::volume temp_volume = parent_pocket->remaining_volume() / volume_multiplier;
            max_volume = std::min( max_volume, temp_volume );
        }

        // Move up one level of containers
        current_pocket = parent_pocket;
        current_pocket_data = current_pocket->get_pocket_data();
        current = parent;
    }

    const int charges_weight = it.charges_per_weight( max_weight, true );
    if( charges_weight == 0 ) {
        return ret_val<int>::make_failure( 0, _( "item is too heavy for a parent pocket" ) );
    }
    const int charges_volume = it.charges_per_volume( max_volume, true );
    if( charges_volume == 0 ) {
        return ret_val<int>::make_failure( 0, _( "item is too big for a parent pocket" ) );
    }

    return ret_val<int>::make_success( std::min( charges_weight, charges_volume ) );
}

ret_val<item *> Character::try_add( detached_ptr<item> &&it, int &copies_remaining,
                                    const item *avoid, const item *original_inventory_item,
                                    const bool allow_wield, bool ignore_pkt_settings )
{
    ( void )original_inventory_item; // CDF: items keep their identity, so they keep their invlet
    if( !it ) {
        return ret_val<item *>::make_failure( nullptr, "no item" );
    }
    invalidate_inventory_validity_cache();
    last_item = it->typeId();

    // item copy for test can contain
    detached_ptr<item> temp_holder = item::spawn( *it );
    item &temp_it = *temp_holder;
    temp_it.charges = 1;

    item *first_item_added = nullptr;
    // the first copy placed gets the invlet the item had or was assigned
    while( copies_remaining > 0 ) {
        const pocket_choice pocket = best_pocket( *it, avoid, ignore_pkt_settings );
        if( pocket.pocket == nullptr ) {
            break;
        }

        int max_copies;
        if( !temp_it.is_null() ) {
            const int pocket_max_copies = pocket.pocket->remaining_capacity_for_item( temp_it );
            const int parent_max_copies = pocket.container->parent_item() == nullptr ? INT_MAX :
                                          !parents_can_contain_recursive( *pocket.container,
                                                  temp_it ).success() ? 0 :
                                          max_charges_by_parent_recursive( *pocket.container, temp_it ).value();
            max_copies = std::min( { copies_remaining, pocket_max_copies, parent_max_copies } );
        } else {
            max_copies = copies_remaining;
        }

        std::vector<item *> newits;
        pocket.pocket->add( *it, max_copies, newits );

        if( newits.empty() ) {
            // Failed to insert into best pocket - break out to prevent infinite loop.
            break;
        }

        // Give invlet to the first item created.
        if( first_item_added == nullptr ) {
            first_item_added = newits.front();
            if( first_item_added->invlet == '\0' ) {
                inv_update_invlet( *first_item_added );
            }
        }

        copies_remaining -= max_copies;

        for( item *added : newits ) {
            added->on_pickup( *this );
        }
        pocket.container->on_contents_changed();
        pocket.pocket->on_contents_changed();
        carried_dirty = true;
    }

    if( copies_remaining > 0 && allow_wield && !has_weapon() ) {
        // CDF: the prototype itself is wielded; it is consumed only if the hands take it
        detached_ptr<item> wield_copy = item::spawn( *it );
        wield_copy = wield( std::move( wield_copy ) );
        if( !wield_copy ) {
            copies_remaining--;
            if( first_item_added == nullptr ) {
                first_item_added = &primary_weapon();
            }
        }
    }
    if( first_item_added != nullptr ) {
        clear_npc_ai_info_cache( npc_ai_info::reloadables );
        clear_npc_ai_info_cache( npc_ai_info::reloadable_cbms );
        it = detached_ptr<item>();
        return ret_val<item *>::make_success( first_item_added );
    }
    return ret_val<item *>::make_failure( nullptr, "no pocket takes the item" );
}

bool Character::can_add( const item &it, const item *avoid, bool allow_wield,
                         bool ignore_pkt_settings ) const
{
    // CDF: DDA marks the validity cache here; the method is const, so the cache is left alone
    const pocket_choice pocket = best_pocket( it, avoid, ignore_pkt_settings );
    if( pocket.pocket == nullptr ) {
        return !has_weapon() && allow_wield && can_wield( it ).success();
    }
    return true;
}

item &Character::i_add( detached_ptr<item> &&it, bool should_stack, const item *avoid,
                        const item *original_inventory_item, const bool allow_drop,
                        const bool allow_wield, bool ignore_pkt_settings )
{
    ( void )should_stack;
    if( !it ) {
        return null_item_reference();
    }
    invalidate_inventory_validity_cache();
    const itype_id item_type_id = it->typeId();
    last_item = item_type_id;

    // CDF: the flat inventory view re-sorts when something is added (see inventory::unsort)
    if( it->is_food() || it->is_ammo() || it->is_gun() || it->is_armor() ||
        it->is_book() || it->is_tool() || it->is_melee() || it->is_food_container() ) {
        inv.unsort();
    }

    ret_val<item *> added = try_add( std::move( it ), avoid, original_inventory_item, allow_wield,
                                     ignore_pkt_settings );
    if( added.success() && added.value() != nullptr ) {
        return *added.value();
    }
    // try_add leaves the item untouched when it fails
    if( !it ) {
        return null_item_reference();
    }
    if( allow_drop ) {
        // CDF: a drop is announced, an item never gets lost (see drop_for_lack_of_room)
        return drop_for_lack_of_room( std::move( it ) );
    }
    return null_item_reference();
}

ret_val<item *> Character::i_add_or_fill( detached_ptr<item> &&it, bool should_stack,
        const item *avoid, const item *original_inventory_item, const bool allow_drop,
        const bool allow_wield, bool ignore_pkt_settings )
{
    if( !it ) {
        return ret_val<item *>::make_failure( nullptr, "no item" );
    }
    item *loc = nullptr;
    bool success = false;
    if( it->count_by_charges() && it->charges >= 2 && !ignore_pkt_settings ) {
        const int last_charges = it->charges;
        int new_charge = last_charges;
        add_stash( *it, new_charge, false );

        if( new_charge < last_charges ) {
            it->charges = new_charge;
            success = true;
        } else {
            success = false;
        }
        if( new_charge >= 1 ) {
            // CDF: what the pockets did not take stays in `it`; hands or floor only if allowed
            if( allow_wield && !has_weapon() ) {
                it = wield( std::move( it ) );
                if( !it ) {
                    loc = &primary_weapon();
                }
            }
            if( it && allow_drop ) {
                const itype_id type = it->typeId();
                map &here = get_map();
                it = here.add_item_or_charges( bub_pos(), std::move( it ) );
                loc = nullptr;
                for( item * const &on_ground : here.i_at( bub_pos() ) ) {
                    if( on_ground->typeId() == type ) {
                        loc = on_ground;
                        break;
                    }
                }
            }
        } else {
            // everything went into pockets: nothing is left to hand back
            it = detached_ptr<item>();
        }
        if( success ) {
            return ret_val<item *>::make_success( loc );
        }
        return ret_val<item *>::make_failure( loc );
    }
    item &added = i_add( std::move( it ), should_stack, avoid, original_inventory_item, allow_drop,
                         allow_wield, ignore_pkt_settings );
    return ret_val<item *>::make_success( added.is_null() ? nullptr : &added );
}

bool Character::i_add_or_drop( const item &it, int qty, const item *avoid,
                               const item *original_inventory_item )
{
    bool retval = true;
    bool drop = it.made_of( LIQUID );
    const bool add = it.is_gun() || !it.is_irremovable();
    map &here = get_map();
    for( int i = 0; i < qty; ++i ) {
        drop |= !can_pickWeight( it, false ) || !can_pickVolume( it );
        if( drop ) {
            retval &= !here.add_item_or_charges( bub_pos(), item::spawn( it ) );
            if( !retval ) {
                // No need to loop now, we already knew that there isn't enough room for the item.
                break;
            }
        } else if( add ) {
            i_add( item::spawn( it ), true, avoid, original_inventory_item, /*allow_drop=*/true,
                   /*allow_wield=*/!has_wield_conflicts( it ) );
        } else {
            retval = false;
            break;
        }
    }

    return retval;
}

// ---------------------------------------------------------------------------------------------
// can_stash and the pick-up gates
// ---------------------------------------------------------------------------------------------

ret_val<bool> Character::can_stash( const item &it, const item *avoid,
                                    bool ignore_pkt_settings ) const
{
    if( has_trait( trait_DEBUG_STORAGE ) ) {
        return ret_val<bool>::make_success( true );
    }
    if( best_pocket( it, avoid, ignore_pkt_settings ).pocket != nullptr ) {
        return ret_val<bool>::make_success( true );
    }
    return stash_refusal( it, avoid );
}

ret_val<bool> Character::can_stash( const item &it, int &copies_remaining,
                                    bool ignore_pkt_settings ) const
{
    bool stashed_any = false;

    for( const item *loc : top_items() ) {
        if( loc->can_contain( it, copies_remaining, false, false, ignore_pkt_settings ).success() ) {
            stashed_any = true;
        }
        if( copies_remaining <= 0 ) {
            break;
        }
    }
    if( stashed_any ) {
        return ret_val<bool>::make_success( true );
    }
    return stash_refusal( it, nullptr );
}

bool Character::can_stash_partial( const item &it, int &copies_remaining,
                                   bool ignore_pkt_settings ) const
{
    detached_ptr<item> copy = item::spawn( it );
    if( it.count_by_charges() ) {
        copy->charges = 1;
    }
    return can_stash( *copy, copies_remaining, ignore_pkt_settings ).success();
}

bool Character::can_stash_partial( const item &it, bool ignore_pkt_settings ) const
{
    detached_ptr<item> copy = item::spawn( it );
    if( it.count_by_charges() ) {
        copy->charges = 1;
    }
    return can_stash( *copy, nullptr, ignore_pkt_settings ).success();
}

bool Character::can_pickVolume( const item &it, bool, const item *avoid,
                                const bool ignore_pkt_settings ) const
{
    // CDF: DEBUG_STORAGE has room for everything, the body storage is searched like worn items
    if( has_trait( trait_DEBUG_STORAGE ) ) {
        return true;
    }
    const item &weapon = primary_weapon();
    if( !weapon.is_null() && ( avoid == nullptr || &weapon != avoid ) &&
        weapon.can_contain( it, false, false, ignore_pkt_settings ).success() ) {
        return true;
    }
    for( const item * const &worn_it : worn ) {
        if( worn_it->can_contain( it, false, false, ignore_pkt_settings ).success() ) {
            return true;
        }
    }
    if( const item *storage = get_body_storage() ) {
        if( storage->can_contain( it, false, false, ignore_pkt_settings ).success() ) {
            return true;
        }
    }
    return false;
}

bool Character::can_pickVolume_partial( const item &it, bool, const item *avoid,
                                        const bool ignore_pkt_settings, const bool ignore_non_container_pocket ) const
{
    if( has_trait( trait_DEBUG_STORAGE ) ) {
        return true;
    }
    detached_ptr<item> copy_holder = item::spawn( it );
    item &copy = *copy_holder;
    if( it.count_by_charges() ) {
        copy.charges = 1;
    }

    const item &weapon = primary_weapon();
    if( !weapon.is_null() && ( avoid == nullptr || &weapon != avoid ) &&
        weapon.can_contain( copy, false, false, ignore_pkt_settings,
                            ignore_non_container_pocket ).success() ) {
        return true;
    }
    for( const item * const &worn_it : worn ) {
        if( worn_it->can_contain( copy, false, false, ignore_pkt_settings,
                                  ignore_non_container_pocket ).success() ) {
            return true;
        }
    }
    if( const item *storage = get_body_storage() ) {
        if( storage->can_contain( copy, false, false, ignore_pkt_settings,
                                  ignore_non_container_pocket ).success() ) {
            return true;
        }
    }
    return false;
}

bool Character::can_pickWeight( const item &it, bool safe ) const
{
    return can_pick_weight( it.weight(), safe );
}

bool Character::can_pickWeight_partial( const item &it, bool safe ) const
{
    detached_ptr<item> copy = item::spawn( it );
    if( it.count_by_charges() ) {
        copy->charges = 1;
    }
    return can_pickWeight( *copy, safe );
}

void Character::invalidate_inventory_validity_cache()
{
    cache_inventory_is_valid = false;
}

// ---------------------------------------------------------------------------------------------
// Stashing charges across the pockets (DDA outfit::add_stash)
// ---------------------------------------------------------------------------------------------

static void get_child_pocket_with_parent( const item_pocket *pocket, item *owner,
        const int nested_level, const std::function<bool( const item_pocket * )> &filter,
        std::vector<pocket_data_with_parent> &ret )
{
    if( pocket == nullptr ) {
        return;
    }
    pocket_data_with_parent pocket_data;
    pocket_data.pocket_ptr = pocket;
    // CDF: DDA's parent is the location of the owner, which has a parent only when nested
    pocket_data.parent = nested_level > 0 ? owner : nullptr;
    pocket_data.nested_level = nested_level;
    if( filter( pocket_data.pocket_ptr ) ) {
        ret.push_back( pocket_data );
    }

    for( const item *contained : pocket->all_items_top() ) {
        for( const item_pocket *pocket_nest : contained->get_container_pockets() ) {
            get_child_pocket_with_parent( pocket_nest, const_cast<item *>( contained ),
                                          nested_level + 1, filter, ret );
        }
    }
}

std::vector<pocket_data_with_parent> Character::get_all_pocket_with_parent(
    const std::function<bool( const item_pocket * )> &filter,
    const std::function<bool( const pocket_data_with_parent &a, const pocket_data_with_parent &b )>
    *sort_func ) const
{
    std::vector<pocket_data_with_parent> ret;

    std::list<item *> locs;
    item &carried_item = primary_weapon();
    if( !carried_item.is_null() ) {
        locs.push_back( &carried_item );
    }
    for( item *worn_it : top_items() ) {
        locs.push_back( worn_it );
    }
    // CDF: the body storage is searched after the worn items
    if( item *storage = get_body_storage() ) {
        locs.push_back( storage );
    }

    for( item *loc : locs ) {
        for( const item_pocket *pocket : loc->get_container_pockets() ) {
            get_child_pocket_with_parent( pocket, loc, 0, filter, ret );
        }
    }
    if( sort_func != nullptr ) {
        const auto sort_pockets_func = [sort_func]( const pocket_data_with_parent & l,
        const pocket_data_with_parent & r ) {
            return ( *sort_func )( l, r );
        };
        std::sort( ret.begin(), ret.end(), sort_pockets_func );
    }
    return ret;
}

void Character::add_stash( const item &newit, int &remaining_charges, bool ignore_pkt_settings )
{
    worn_weight_cache_dirty = true;
    carried_dirty = true;
    if( ignore_pkt_settings ) {
        // Crawl all pockets regardless of priority
        // Crawl First : wielded item
        item &carried_item = primary_weapon();
        if( !carried_item.is_null() && !carried_item.has_pocket_type( pocket_type::MAGAZINE ) &&
            carried_item.can_contain_partial( newit ).success() ) {
            const int used_charges = carried_item.fill_with( newit, remaining_charges,
                                     /*unseal_pockets=*/false, /*allow_sealed=*/false, /*ignore_settings=*/false,
                                     /*into_bottom=*/false, /*allow_nested=*/true, this );
            remaining_charges -= used_charges;
        }
        // Crawl Next : worn items (DDA outfit::pickup_stash)
        for( item * const &worn_it : worn ) {
            if( remaining_charges == 0 ) {
                break;
            }
            if( worn_it->can_contain_partial( newit ).success() ) {
                const int used_charges = worn_it->fill_with( newit, remaining_charges, false, false,
                                         ignore_pkt_settings );
                remaining_charges -= used_charges;
            }
        }
    } else {
        // item copy for test can contain
        detached_ptr<item> temp_holder = item::spawn( newit );
        item &temp_it = *temp_holder;
        temp_it.charges = 1;

        // Collect all pockets
        const auto pocket_filter = [&temp_it]( const item_pocket * pck ) {
            return pck->can_contain( temp_it ).success();
        };
        const std::function<bool( const pocket_data_with_parent &a, const pocket_data_with_parent &b )>
        sort_f = [&temp_it]( const pocket_data_with_parent & a, const pocket_data_with_parent & b ) {
            return b.pocket_ptr->better_pocket( *a.pocket_ptr, temp_it, false );
        };
        const std::vector<pocket_data_with_parent> pockets_with_parent =
            get_all_pocket_with_parent( pocket_filter, &sort_f );

        const int amount = remaining_charges;
        int num_contained = 0;
        for( const pocket_data_with_parent &pocket_data_ptr : pockets_with_parent ) {
            if( amount <= num_contained || remaining_charges <= 0 ) {
                break;
            }
            item_pocket *pocke = const_cast<item_pocket *>( pocket_data_ptr.pocket_ptr );
            if( pocke == nullptr ) {
                continue;
            }
            int max_contain_value = pocke->remaining_capacity_for_item( newit );
            const item *parent_data = pocket_data_ptr.parent;

            if( parent_data != nullptr && parent_data->parent_item() != nullptr ) {
                if( parents_can_contain_recursive( *parent_data, temp_it ).success() ) {
                    max_contain_value = max_charges_by_parent_recursive( *parent_data, temp_it ).value();
                } else {
                    max_contain_value = 0;
                }
            }
            const int charges = std::min( max_contain_value, remaining_charges );
            const int filled_count = pocke->fill_with( newit, *this, charges, false, false );
            num_contained += filled_count;
            remaining_charges -= filled_count;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Capacity (DDA character_inventory.cpp:1842-1897)
// ---------------------------------------------------------------------------------------------

units::volume Character::free_space( const std::function<bool( const item_pocket & )>
                                     &include_pocket,
                                     const std::function<bool( const item_pocket & )> &check_pocket_tree ) const
{
    // CDF: DEBUG_STORAGE has no limit (as in volume_capacity); summing the unlimited pockets
    // would overflow
    if( has_trait( trait_DEBUG_STORAGE ) ) {
        return units::volume_max;
    }
    // discarded, currently don't care if the character's held item would need to get bigger
    units::volume expansion = 0_ml;
    units::volume ret = 0_ml;
    if( !primary_weapon().is_null() ) {
        ret += primary_weapon().get_remaining_volume_recursive( include_pocket, check_pocket_tree,
                expansion );
    }
    for( const item * const &worn_it : worn ) {
        ret += worn_it->get_remaining_volume_recursive( include_pocket, check_pocket_tree, expansion );
    }
    // CDF: the body storage counts like a worn container
    if( const item *storage = get_body_storage() ) {
        ret += storage->get_remaining_volume_recursive( include_pocket, check_pocket_tree, expansion );
    }
    return ret;
}

units::mass Character::free_weight_capacity() const
{
    units::mass weight_capacity = 0_gram;
    if( !primary_weapon().is_null() ) {
        weight_capacity += primary_weapon().get_remaining_weight_capacity();
    }
    for( const item * const &worn_it : worn ) {
        weight_capacity += worn_it->get_remaining_weight_capacity();
    }
    return weight_capacity;
}

units::volume Character::volume_capacity( const std::function<bool( const item_pocket & )>
        &include_pocket ) const
{
    // CDF: DEBUG_STORAGE has no limit
    if( has_trait( trait_DEBUG_STORAGE ) ) {
        return units::volume_max;
    }
    units::volume volume_capacity = 0_ml;
    if( !primary_weapon().is_null() ) {
        volume_capacity += primary_weapon().get_volume_capacity( include_pocket );
    }
    for( const item * const &worn_it : worn ) {
        volume_capacity += worn_it->get_volume_capacity( include_pocket );
    }
    // CDF: the body storage (bionic and shell compartments)
    if( const item *storage = get_body_storage() ) {
        volume_capacity += storage->get_volume_capacity( include_pocket );
    }
    return volume_capacity;
}

units::volume Character::volume_capacity_recursive(
    const std::function<bool( const item_pocket & )> &include_pocket,
    const std::function<bool( const item_pocket & )> &check_pocket_tree ) const
{
    units::volume volume_capacity = 0_ml;
    // discard, currently don't care if inventory has to grow in overall volume
    units::volume expansion = 0_ml;
    if( !primary_weapon().is_null() ) {
        volume_capacity += primary_weapon().get_volume_capacity_recursive( include_pocket,
                           check_pocket_tree, expansion );
    }
    for( const item * const &worn_it : worn ) {
        volume_capacity += worn_it->get_volume_capacity_recursive( include_pocket, check_pocket_tree,
                           expansion );
    }
    return volume_capacity;
}

units::volume Character::volume_carried() const
{
    units::volume volume = 0_ml;
    volume += primary_weapon().is_null() ? 0_ml : primary_weapon().volume();
    for( const item * const &worn_it : worn ) {
        volume += worn_it->volume();
    }
    return volume;
}

// ---------------------------------------------------------------------------------------------
// Handling costs (DDA character_inventory.cpp:1461-1533)
// ---------------------------------------------------------------------------------------------

int Character::item_handling_cost( const item &it, bool penalties, int base_cost,
                                   int charges_in_it, bool bulk_cost ) const
{
    int mv = base_cost;
    if( penalties ) {
        // 50 moves per liter, up to 200 at 4 liters
        units::volume vol = it.volume();
        if( charges_in_it >= 0 && it.count_by_charges() && it.charges > 0 ) {
            // CDF: item::volume has no charges argument, so scale the volume of the stack
            vol = vol * charges_in_it / it.charges;
        }
        const auto volume_moves = vol / 20_ml;
        mv += static_cast<int>( std::min( volume_moves, decltype( volume_moves ) { 200 } ) );
    }

    if( primary_weapon().typeId() == itype_e_handcuffs ) {
        mv *= 4;
    } else if( penalties && has_effect( effect_grabbed ) ) {
        // CDF: DDA scales a GRAB flag per grabbed arm or hand; CDF has the grabbed effect only
        mv *= 2;
    }

    // The following is considered as overhead and is not included in the cost when handling
    // multiple items at once.
    if( !bulk_cost ) {
        // For single handed items use the least encumbered hand
        if( it.is_two_handed( *this ) ) {
            mv += encumb( body_part_hand_l ) + encumb( body_part_hand_r );
        } else {
            mv += std::min( encumb( body_part_hand_l ), encumb( body_part_hand_r ) );
        }
    }

    return std::max( mv, 0 );
}

int Character::item_store_cost( const item &it, const item &/* container */, bool penalties,
                                int base_cost ) const
{
    /** @EFFECT_PISTOL decreases time taken to store a pistol */
    /** @EFFECT_SMG decreases time taken to store an SMG */
    /** @EFFECT_RIFLE decreases time taken to store a rifle */
    /** @EFFECT_SHOTGUN decreases time taken to store a shotgun */
    /** @EFFECT_LAUNCHER decreases time taken to store a launcher */
    /** @EFFECT_STABBING decreases time taken to store a stabbing weapon */
    /** @EFFECT_CUTTING decreases time taken to store a cutting weapon */
    /** @EFFECT_BASHING decreases time taken to store a bashing weapon */
    const float lvl = get_skill_level( it.is_gun() ? it.gun_skill() : it.melee_skill() );
    return item_handling_cost( it, penalties, base_cost ) / ( ( lvl + 10.0f ) / 10.0f );
}

int Character::item_retrieve_cost( const item &it, const item &container, bool penalties,
                                   int base_cost ) const
{
    // Drawing from a holster use the same formula as storing an item for now
    /**
         * @EFFECT_PISTOL decreases time taken to draw pistols from holsters
         * @EFFECT_SMG decreases time taken to draw smgs from holsters
         * @EFFECT_RIFLE decreases time taken to draw rifles from holsters
         * @EFFECT_SHOTGUN decreases time taken to draw shotguns from holsters
         * @EFFECT_LAUNCHER decreases time taken to draw launchers from holsters
         * @EFFECT_STABBING decreases time taken to draw stabbing weapons from sheaths
         * @EFFECT_CUTTING decreases time taken to draw cutting weapons from scabbards
         * @EFFECT_BASHING decreases time taken to draw bashing weapons from holsters
         */
    return item_store_cost( it, container, penalties, base_cost );
}

// ---------------------------------------------------------------------------------------------
// Overflow, contents changes and storing (DDA character_inventory.cpp:112, 788, 3164)
// ---------------------------------------------------------------------------------------------

void Character::drop_invalid_inventory()
{
    ZoneScoped;
    if( cache_inventory_is_valid ) {
        return;
    }
    // D9: what no longer fits goes pocket by pocket (item::overflow), no random shedding
    const tripoint_bub_ms here_pos = bub_pos();
    if( !primary_weapon().is_null() ) {
        primary_weapon().overflow( here_pos, this );
    }
    // overflow can change the worn list, so work on a copy of the pointers
    for( item *worn_it : top_items() ) {
        worn_it->overflow( here_pos, this );
    }
    if( item *storage = get_body_storage() ) {
        storage->overflow( here_pos, this );
    }
    cache_inventory_is_valid = true;
}

// Per DDA character_inventory.cpp:112 (Character::handle_contents_changed), over
// safe_reference<item> (J6, T5/T11): containers are unsealed and handled from the innermost
// to the outermost, so inner containers are always handled before outer ones are removed.
void Character::handle_contents_changed( const std::vector<safe_reference<item>> &containers )
{
    if( containers.empty() ) {
        return;
    }

    struct item_with_depth {
        safe_reference<item> loc;
        int depth = 0;
    };
    const auto depth_of = []( const item &it ) -> int {
        int depth = 0;
        for( const item *ancestor = it.parent_item(); ancestor != nullptr;
             ancestor = ancestor->parent_item() )
        {
            ++depth;
        }
        return depth;
    };

    std::vector<item_with_depth> sorted_containers;
    for( const safe_reference<item> &container : containers ) {
        if( container ) {
            sorted_containers.push_back( { container, depth_of( *container ) } );
        }
    }
    map &m = get_map();

    // unseal and handle containers, from innermost (max depth) to outermost (min depth)
    while( !sorted_containers.empty() ) {
        const auto deepest = std::ranges::max_element( sorted_containers,
        []( const item_with_depth & lhs, const item_with_depth & rhs ) {
            return lhs.depth < rhs.depth;
        } );
        safe_reference<item> loc = deepest->loc;
        sorted_containers.erase( deepest );
        if( !loc ) {
            debugmsg( "invalid item location" );
            continue;
        }
        loc->on_contents_changed();
        const bool handle_drop = loc->where() != item_location_type::map && !is_wielding( *loc );
        bool drop_unhandled = false;
        for( item_pocket *const pocket : loc->get_container_pockets() ) {
            if( pocket && !pocket->sealed() ) {
                // pockets are unsealed but on_contents_changed is not called
                // in contents_change_handler::unseal_pocket_containing
                pocket->on_contents_changed();
            }
            if( pocket && handle_drop && pocket->will_spill() ) {
                // the pocket's contents (with a larger depth value) are not
                // inside `sorted_containers` and can be safely disposed of.
                pocket->handle_liquid_or_spill( *this, /*avoid=*/&*loc );
                // drop the container instead if canceled.
                if( !pocket->empty() ) {
                    // drop later since we still need to access the container item
                    drop_unhandled = true;
                    // canceling one pocket cancels spilling for the whole container
                    break;
                }
            }
        }

        if( item *const parent = loc->parent_item() ) {
            item_pocket *const pocket = parent->contained_where( *loc );
            if( pocket != nullptr ) {
                pocket->unseal();
            }
            const bool exists = std::ranges::any_of( sorted_containers,
            [parent]( const item_with_depth & entry ) {
                return entry.loc == parent;
            } );
            if( !exists ) {
                sorted_containers.push_back( { safe_reference<item>( parent ), depth_of( *parent ) } );
            }
        }

        if( drop_unhandled ) {
            // We can drop the unhandled container now since the container and
            // its contents (with a larger depth) are not inside `sorted_containers`.
            add_msg_player_or_npc(
                _( "To avoid spilling its contents, you set your %1$s on the %2$s." ),
                _( "To avoid spilling its contents, <npcname> sets their %1$s on the %2$s." ),
                loc->display_name(), m.name( bub_pos() )
            );
            // the item moves to the floor; its location is invalid from now on
            m.add_item_or_charges( bub_pos(), loc->detach() );
        }
    }
    on_carried_contents_changed();
}

void Character::store( item &container, item &put, bool penalties, int base_cost,
                       pocket_type pk_type, bool check_best_pkt )
{
    mod_moves( -item_store_cost( put, container, penalties, base_cost ) );
    // CDF (T4): the item moves, it keeps its identity; only a charge stack is poured in
    detached_ptr<item> det = put.detach();
    if( check_best_pkt && pk_type == pocket_type::CONTAINER &&
        container.get_container_pockets().size() > 1 ) {
        // Bypass pocket settings (assuming the item is manually stored)
        if( det->count_by_charges() ) {
            const int charges = det->charges;
            container.fill_with( *det, charges, false, false, true, false, true, this );
            det = detached_ptr<item>();
        } else {
            const pocket_choice target = container.best_pocket( *det, nullptr, false, true );
            if( target.pocket != nullptr ) {
                target.container->contents.insert_into( *target.pocket, std::move( det ) );
                target.container->on_contents_changed();
            }
        }
    }
    if( det ) {
        container.put_in( std::move( det ), pk_type, false, this );
    }
    if( det ) {
        // nothing took it: it never gets lost
        i_add( std::move( det ), true, nullptr, nullptr, true, false );
    }
    on_carried_contents_changed();
    calc_encumbrance();
}

void Character::store( item_pocket *pocket, item &put, bool penalties, int base_cost )
{
    if( !pocket ) {
        return;
    }
    item &null_owner = null_item_reference();
    const pocket_choice pkt_best = pocket->best_pocket_in_contents( null_owner, put, nullptr, false,
                                   false );
    if( pkt_best.pocket != nullptr && pocket->better_pocket( *pkt_best.pocket, put, true ) ) {
        pocket = pkt_best.pocket;
    }
    mod_moves( -std::max( item_store_cost( put, null_owner, penalties, base_cost ),
                          pocket->obtain_cost( put ) ) );
    ret_val<item *> result = pocket->insert_item( put.detach() );
    if( result.value() != nullptr ) {
        result.value()->on_pickup( *this );
    }
    on_carried_contents_changed();
    calc_encumbrance();
}

// ---------------------------------------------------------------------------------------------
// Wielding (DDA character_inventory.cpp:1617, 2985)
// ---------------------------------------------------------------------------------------------

bool Character::has_wield_conflicts( const item &it ) const
{
    return is_wielding( it ) || ( is_armed() && !it.can_combine( primary_weapon() ) );
}

bool Character::wield( item &it, std::optional<int> obtain_cost, bool combat )
{
    invalidate_inventory_validity_cache();

    if( has_wield_conflicts( it ) ) {
        const bool is_unwielding = is_wielding( it );
        const ret_val<bool> ret = can_unwield( it );

        if( !ret.success() ) {
            add_msg_if_player( m_info, "%s", ret.c_str() );
            return false;
        }

        if( !is_unwielding && primary_weapon().has_item( it ) ) {
            add_msg_if_player( m_info,
                               _( "You need to put the bag away before trying to wield something from it." ) );
            return false;
        }

        if( !unwield() ) {
            return false;
        }

        if( is_unwielding ) {
            if( !martial_arts_data->selected_is_none() ) {
                martial_arts_data->martialart_use_message( *this );
            }
            return true;
        }
    }

    if( !can_wield( it ).success() ) {
        return false;
    }

    // after the unwield above only a stack that combines can still be in the hands
    const bool combine_stacks = is_armed() && it.can_combine( primary_weapon() );

    // Wielding from inventory is relatively slow and does not improve with increasing weapon skill.
    // Worn items (including guns with shoulder straps) are faster but still slower
    // than a skilled player with a holster.
    // There is an additional penalty when wielding items from the inventory whilst currently grabbed.
    const bool worn_item = is_worn( it );

    // Ideally the cost is the one of fetching the item from where it is (item::obtain_cost)
    const int mv = obtain_cost.value_or( item_handling_cost( it, true,
                                         INVENTORY_HANDLING_PENALTY / ( worn_item ? 2 : 1 ) ) );
    add_msg( m_debug, "wielding took %d moves", mv );
    mod_moves( -mv );

    clear_npc_ai_info_cache( npc_ai_info::ideal_weapon_value );
    item *wielded = &it;
    const itype_id wielded_type = it.typeId();
    if( combine_stacks ) {
        wielded = &primary_weapon();
        // CDF (T11): the other stack is merged into the wielded one and is gone
        wielded->merge_charges( it.detach(), true );
    } else {
        set_primary_weapon( it.detach() );
    }

    last_item = wielded_type;
    recoil = MAX_RECOIL;
    wielded->on_wield( *as_player(), mv );

    inv_update_invlet( *wielded );
    inv_update_invlet_cache_with_item( *wielded );
    ( void )combat;
    return true;
}
