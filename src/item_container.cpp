/**
* This file is for container-related functions strictly in the `item` class.
*
* This file is NOT for `item_pocket` or `item_contents` function definitions;
* many of this file's functions forward to identically-named `item_pocket` or `item_contents`
* functions, which is intended.
*
* Transplant of DDA src/item_container.cpp (b4b62254ae2), J3. Differences from DDA are tagged
* `// CDF:`. The CDF ownership rules (plans/P1-transplant/COMMON.md T1-T5) apply: item_location
* handles are plain `item *`, and copies are `item::spawn( *it )`.
*/

#include "item.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "calendar.h"
#include "character.h"
#include "coordinates.h"
#include "debug.h"
#include "digital_media.h"
#include "enums.h"
#include "flag.h"
#include "flat_set.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "item_pocket_menu.h"
#include "game.h"
#include "output.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"

static const item_category_id item_category_container( "container" );

detached_ptr<item> item::in_its_container( detached_ptr<item> &&self, int qty )
{
    const itype *type = self->type;
    return in_container( type->default_container.value_or( itype_id::NULL_ID() ), std::move( self ),
                         qty, type->default_container_sealed,
                         type->default_container_variant.value_or( "" ) );
}

detached_ptr<item> item::in_container( const itype_id &cont, detached_ptr<item> &&self, int qty,
                                       bool sealed, const std::string &/*variant*/ )
{
    if( cont.is_null() ) {
        return std::move( self );
    }

    if( qty <= 0 ) {
        qty = self->count();
    }
    detached_ptr<item> container = item::spawn( cont, self->birthday() );
    // CDF: BN items have no variants, so the container variant is not applied
    // CDF: any container pocket, carry or not (DDA is_container())
    if( container->has_pocket_type( pocket_type::CONTAINER ) ) {
        // CDF: BN containers that are sealed by their type (unseals_into) are sealed from the start,
        // so spawning has to be allowed to fill them (DDA seals after filling)
        container->fill_with( *self, qty, false, /*allow_sealed=*/true );
        if( container->typeId() != cont ) {
            // filling at spawn does not open a container sealed by its type: fill_with's
            // on_contents_changed converted it to its opened type (BN unseals_into)
            container->convert( cont );
        }
        container->invlet = self->invlet;
        if( sealed ) {
            container->seal();
        }
        if( !container->has_item_with_id( self->typeId() ) ) {
            debugmsg( "ERROR: failed to put %s in its container %s", self->typeId().c_str(), cont.c_str() );
            return std::move( self );
        }
        container->add_automatic_whitelist();
        return container;
    }
    return std::move( self );
}

void item::add_automatic_whitelist()
{
    std::vector<item_pocket *> pkts = get_container_pockets();
    if( pkts.size() == 1 && contents_only_one_type() ) {
        pkts.front()->settings.whitelist_item( contents.first_item().typeId() );
        pkts.front()->settings.set_priority( 100 );
        pkts.front()->settings.set_collapse( true );
    }
}

void item::clear_automatic_whitelist()
{
    std::vector<item_pocket *> pkts = get_container_pockets();
    if( pkts.size() == 1 && !pkts.front()->settings.was_edited() ) {
        cata::flat_set<itype_id> const &wl = pkts.front()->settings.get_item_whitelist();
        if( !wl.empty() ) {
            pkts.front()->settings.clear_item( *wl.begin() );
            pkts.front()->settings.set_priority( 0 );
        }
    }
}

// CDF: contents-local walk, no dependency on 3-5a (item::pocket_by_id)
static const item_pocket *pocket_with_id( const item &host, const std::string &id )
{
    if( id.empty() ) {
        return nullptr;
    }
    const std::vector<const item_pocket *> found = host.get_pockets( [&id]( const item_pocket & q ) {
        return q.get_pocket_data() && q.get_pocket_data()->pocket_id == id;
    } );
    return found.empty() ? nullptr : found.front();
}

void item::validate_mod_pocket_refs() const
{
    if( !is_gun() && !is_tool() ) {
        return;
    }
    std::set<std::string> seen;
    for( const item_pocket *p : get_pockets( []( const item_pocket & q ) {
    return q.get_pocket_data() && !q.get_pocket_data()->pocket_id.empty();
    } ) ) {
        const std::string &id = p->get_pocket_data()->pocket_id;
        if( !seen.insert( id ).second ) {
            debugmsg( "%s has duplicate pocket id \"%s\" after mod merge", tname(), id );
        }
    }
    const auto check = [this]( const std::string & id, const char *what ) {
        if( pocket_with_id( *this, id ) == nullptr ) {
            debugmsg( "%s: mod %s references unknown pocket id \"%s\"", tname(), what, id );
        }
    };
    // CDF: mods() is contents.mods()
    for( const item *mod : contents.mods() ) {
        if( mod->type == nullptr ) {
            continue;
        }
        if( mod->type->gunmod ) {
            for( const std::pair<const gun_mode_id, std::vector<pocket_consumption_entry>> &mp :
                 mod->type->gunmod->firing_requirements.per_mode ) {
                for( const pocket_consumption_entry &e : mp.second ) {
                    check( e.pocket, "mode_firing_requirements" );
                }
            }
        }
        if( mod->type->mod ) {
            for( const pocket_consumption_mod &cm : mod->type->mod->consumption_mods ) {
                check( cm.pocket, "consumption_mods" );
            }
            for( const pocket_capacity_mod &cm : mod->type->mod->capacity_mods ) {
                const item_pocket *p = pocket_with_id( *this, cm.pocket );
                if( p == nullptr ) {
                    debugmsg( "%s: mod capacity_mods references unknown pocket id \"%s\"",
                              tname(), cm.pocket );
                } else if( !p->is_type( pocket_type::MAGAZINE ) ) {
                    debugmsg( "%s: capacity_mods pocket \"%s\" is not an integral MAGAZINE; "
                              "well capacity is the loaded magazine's", tname(), cm.pocket );
                }
            }
        }
    }
}

bool item::same_contents( const item &rhs ) const
{
    return get_contents().same_contents( rhs.get_contents() );
}

int item::obtain_cost( const item &it ) const
{
    return contents.obtain_cost( it );
}

int item::insert_cost( const item &it ) const
{
    return contents.insert_cost( it );
}

void item::force_insert_item( const item &it, pocket_type pk_type )
{
    contents.force_insert_item( it, pk_type );
    on_contents_changed();
}

void item::favorite_settings_menu()
{
    pocket_management_menu( remove_color_tags( display_name() ), { this }, get_player_character() );
}

bool item::is_collapsed() const
{
    return !contents.empty() && !contents.get_pockets( []( item_pocket const & pocket ) {
        return pocket.settings.is_collapsed() && pocket.is_standard_type();
    } ).empty();
}

bool item::seal()
{
    if( is_container_full() ) {
        return contents.seal_all_pockets();
    } else {
        return false;
    }
}

bool item::all_pockets_sealed() const
{
    return contents.all_pockets_sealed();
}

bool item::any_pockets_sealed() const
{
    return contents.any_pockets_sealed();
}

bool item::is_container_with_restriction() const
{
    if( !is_container() ) {
        return false;
    }
    return contents.is_restricted_container();
}

bool item::is_single_container_with_restriction() const
{
    return contents.is_single_restricted_container();
}

bool item::has_pocket_type( pocket_type pk_type ) const
{
    return contents.has_pocket_type( pk_type );
}

bool item::has_any_with( const std::function<bool( const item & )> &filter,
                         pocket_type pk_type ) const
{
    return contents.has_any_with( filter, pk_type );
}

bool item::all_pockets_rigid() const
{
    return contents.all_pockets_rigid();
}

bool item::container_type_pockets_empty() const
{
    return contents.container_type_pockets_empty();
}

bool item::has_unrestricted_pockets() const
{
    return contents.has_unrestricted_pockets();
}

bool item::contains_no_solids() const
{
    return contents.contains_no_solids();
}

std::vector<item_pocket *> item::get_pockets( const std::function<bool( const item_pocket &pocket )>
        &include_pocket )
{
    return contents.get_pockets( include_pocket );
}

std::vector<const item_pocket *> item::get_pockets( const
        std::function<bool( const item_pocket &pocket )> &include_pocket ) const
{
    return contents.get_pockets( include_pocket );
}

std::vector<const item_pocket *> item::get_container_pockets() const
{
    return contents.get_container_pockets();
}

bool item::has_container_pockets() const
{
    return !get_container_pockets().empty();
}

std::vector<item_pocket *> item::get_container_pockets()
{
    return contents.get_container_pockets();
}

std::vector<const item_pocket *> item::get_standard_pockets() const
{
    return contents.get_standard_pockets();
}

std::vector<item_pocket *> item::get_standard_pockets()
{
    return contents.get_standard_pockets();
}

std::vector<item_pocket *> item::get_ablative_pockets()
{
    return contents.get_ablative_pockets();
}

std::vector<const item_pocket *> item::get_ablative_pockets() const
{
    return contents.get_ablative_pockets();
}

std::vector<const item_pocket *> item::get_container_and_mod_pockets() const
{
    return contents.get_container_and_mod_pockets();
}

std::vector<item_pocket *> item::get_container_and_mod_pockets()
{
    return contents.get_container_and_mod_pockets();
}

void item::begin_bulk_fill()
{
    // CDF: the pocket level is still a no-op, the P1d size cache covers the sums
    contents.begin_bulk_fill();
}

void item::end_bulk_fill()
{
    contents.end_bulk_fill();
}

item_pocket *item::contained_where( const item &contained )
{
    return contents.contained_where( contained );
}

const item_pocket *item::contained_where( const item &contained ) const
{
    return const_cast<item *>( this )->contained_where( contained );
}

units::length item::max_containable_length( const bool unrestricted_pockets_only ) const
{
    return contents.max_containable_length( unrestricted_pockets_only );
}

units::length item::min_containable_length() const
{
    return contents.min_containable_length();
}

units::volume item::max_containable_volume() const
{
    return contents.max_containable_volume();
}

ret_val<void> item::is_compatible( const item &it ) const
{
    if( this == &it ) {
        // does the set of all sets contain itself?
        return ret_val<void>::make_failure();
    }
    // disallow putting portable holes into bags of holding
    if( contents.bigger_on_the_inside( volume() ) &&
        it.contents.bigger_on_the_inside( it.volume() ) ) {
        return ret_val<void>::make_failure();
    }

    return contents.is_compatible( it );
}

ret_val<void> item::can_contain_directly( const item &it ) const
{
    return can_contain( it, false, false, true, false, nullptr, 10000000_ml, false );
}

ret_val<void> item::can_contain( const item &it, const bool nested, const bool ignore_rigidity,
                                 const bool ignore_pkt_settings, const bool ignore_non_container_pocket,
                                 const item *parent_it, units::volume remaining_parent_volume,
                                 const bool allow_nested ) const
{
    int copies = 1;
    return can_contain( it, copies, nested, ignore_rigidity,
                        ignore_pkt_settings, ignore_non_container_pocket, parent_it,
                        remaining_parent_volume, allow_nested );
}

ret_val<void> item::can_contain( const item &it, int &copies_remaining, const bool nested,
                                 const bool ignore_rigidity, const bool ignore_pkt_settings,
                                 const bool ignore_non_container_pocket, const item *parent_it,
                                 units::volume remaining_parent_volume,
                                 const bool allow_nested ) const
{
    if( copies_remaining <= 0 ) {
        return ret_val<void>::make_success();
    }
    if( this == &it || ( parent_it != nullptr && this == parent_it ) ) {
        // does the set of all sets contain itself?
        // or does this already contain it?
        return ret_val<void>::make_failure( "can't put a container into itself" );
    }
    // BN: digital files only go into digital storage media, and digital storage takes no other cargo.
    // CDF: magazines and ammo still reach the battery pocket or well of a digital reader
    // (laptops, cameras, e-readers carry DIGITAL_STORAGE and a P3 magazine pocket).
    const bool digital_storage = has_flag( flag_id( "DIGITAL_STORAGE" ) );
    if( is_digital_file( it ) || ( digital_storage && !it.is_magazine() && !it.is_ammo() ) ) {
        return digital_storage && is_digital_file( it ) ?
               ret_val<void>::make_success() :
               ret_val<void>::make_failure( "digital files need digital storage media" );
    }
    // CDF: any container pocket, carry or not (DDA is_container())
    if( nested && !has_pocket_type( pocket_type::CONTAINER ) ) {
        return ret_val<void>::make_failure();
    }
    // disallow putting portable holes into bags of holding
    if( contents.bigger_on_the_inside( volume() ) &&
        it.contents.bigger_on_the_inside( it.volume() ) ) {
        return ret_val<void>::make_failure();
    }

    if( allow_nested ) {
        for( const item_pocket *pkt : contents.get_container_pockets() ) {
            if( pkt->empty() ) {
                continue;
            }

            // early exit for max length no nested item is gonna fix this
            if( pkt->max_containable_length() < it.length() ) {
                continue;
            }

            // If the current pocket has restrictions or blacklists the item or is a holster,
            // try the nested pocket regardless of whether it's soft or rigid.
            const bool ignore_nested_rigidity =
                !pkt->settings.accepts_item( it ) ||
                !pkt->get_pocket_data()->get_flag_restrictions().empty() || pkt->is_holster();
            units::volume remaining_volume = pkt->remaining_volume();
            for( const item *internal_it : pkt->all_items_top() ) {
                const int copies_remaining_before = copies_remaining;
                auto ret = internal_it->can_contain( it, copies_remaining, true, ignore_nested_rigidity,
                                                     ignore_pkt_settings, ignore_non_container_pocket, parent_it,
                                                     remaining_volume );
                if( copies_remaining <= 0 ) {
                    return ret;
                }
                if( copies_remaining_before != copies_remaining ) {
                    remaining_volume = pkt->remaining_volume();
                }
            }
        }
    }

    return nested && !ignore_rigidity
           ? contents.can_contain_rigid( it, copies_remaining, ignore_pkt_settings,
                                         ignore_non_container_pocket )
           : contents.can_contain( it, copies_remaining, ignore_pkt_settings, ignore_non_container_pocket,
                                   remaining_parent_volume );
}

ret_val<void> item::can_contain( const itype &tp ) const
{
    const item tmp( &tp );
    return can_contain( tmp );
}

ret_val<void> item::can_contain_partial( const item &it ) const
{
    detached_ptr<item> i_copy = item::spawn( it );
    if( i_copy->count_by_charges() ) {
        i_copy->charges = 1;
    }
    return can_contain( *i_copy );
}

ret_val<void> item::can_contain_partial( const item &it, int &copies_remaining,
        bool nested ) const
{
    detached_ptr<item> i_copy = item::spawn( it );
    if( i_copy->count_by_charges() ) {
        i_copy->charges = 1;
    }
    return can_contain( *i_copy, copies_remaining, nested );
}

ret_val<void> item::can_contain_partial_directly( const item &it ) const
{
    detached_ptr<item> i_copy = item::spawn( it );
    if( i_copy->count_by_charges() ) {
        i_copy->charges = 1;
    }
    return can_contain( *i_copy, false, false, true, false, nullptr, 10000000_ml, false );
}

pocket_choice item::best_pocket( const item &it, const item *avoid, const bool allow_sealed,
                                 const bool ignore_settings, const bool nested, bool ignore_rigidity,
                                 bool allow_nested )
{
    return contents.best_pocket( it, avoid, allow_sealed, ignore_settings,
                                 nested, ignore_rigidity, allow_nested );
}

bool item::spill_contents( Character &c )
{
    // BN: digital storage media hold files, nothing to spill
    if( has_flag( flag_id( "DIGITAL_STORAGE" ) ) ) {
        return true;
    }
    // CDF: any container pocket, carry or not (DDA is_container())
    if( ( !has_pocket_type( pocket_type::CONTAINER ) && !is_magazine() &&
          !has_pocket_type( pocket_type::MAGAZINE_WELL ) ) ||
        is_container_empty() ) {
        return true;
    }

    if( c.is_npc() ) {
        return spill_contents( c.bub_pos() );
    }

    contents.handle_liquid_or_spill( c, /*avoid=*/this );
    on_contents_changed();

    return is_container_empty();
}

bool item::spill_contents( const tripoint_bub_ms &pos )
{
    // BN: digital storage media hold files, nothing to spill
    if( has_flag( flag_id( "DIGITAL_STORAGE" ) ) ) {
        return true;
    }
    // CDF: any container pocket, carry or not (DDA is_container())
    if( ( !has_pocket_type( pocket_type::CONTAINER ) && !is_magazine() &&
          !has_pocket_type( pocket_type::MAGAZINE_WELL ) ) ||
        is_container_empty() ) {
        return true;
    }
    const bool spilled = contents.spill_contents( pos );
    on_contents_changed();
    return spilled;
}

bool item::spill_open_pockets( Character &guy, const item *avoid )
{
    return contents.spill_open_pockets( guy, avoid );
}

void item::overflow( const tripoint_bub_ms &pos, Character *carrier )
{
    contents.overflow( pos, carrier );
}

void item::handle_liquid_or_spill( Character &guy, const item *avoid )
{
    contents.handle_liquid_or_spill( guy, avoid );
}

bool item::will_spill() const
{
    return contents.will_spill();
}

bool item::will_spill_if_unsealed() const
{
    return contents.will_spill_if_unsealed();
}

units::volume item::get_volume_capacity( const std::function<bool( const item_pocket & )>
        &include_pocket ) const
{
    return contents.volume_capacity( include_pocket );
}

units::volume item::get_volume_capacity_recursive( const std::function<bool( const item_pocket & )>
        &include_pocket,
        const std::function<bool( const item_pocket & )> &check_pocket_tree,
        units::volume &out_volume_expansion ) const
{
    return contents.volume_capacity_recursive( include_pocket, check_pocket_tree,
            out_volume_expansion );
}

units::mass item::get_total_weight_capacity( const bool unrestricted_pockets_only ) const
{
    return contents.total_container_weight_capacity( unrestricted_pockets_only );
}

units::volume item::get_remaining_volume( const std::function<bool( const item_pocket & )> &
        include_pocket ) const
{
    return contents.remaining_volume( include_pocket );
}

units::volume item::get_remaining_volume_recursive( const std::function<bool( const item_pocket & )>
        &include_pocket,
        const std::function<bool( const item_pocket & )> &check_pocket_tree,
        units::volume &out_volume_expansion ) const
{
    return contents.remaining_volume_recursive( include_pocket, check_pocket_tree,
            out_volume_expansion );
}

units::mass item::get_remaining_weight_capacity( const bool unrestricted_pockets_only ) const
{
    return contents.remaining_container_capacity_weight( unrestricted_pockets_only );
}

units::volume item::get_contents_volume( const std::function<bool( const item_pocket & )>
        &include_pocket ) const
{
    return contents.contents_volume( include_pocket );
}

units::mass item::get_total_contained_weight( const bool unrestricted_pockets_only ) const
{
    return contents.total_contained_weight( unrestricted_pockets_only );
}

units::volume item::get_biggest_pocket_capacity() const
{
    return contents.biggest_pocket_capacity();
}

int item::fill_with( const item &contained, const int amount,
                     const bool unseal_pockets,
                     const bool allow_sealed,
                     const bool ignore_settings,
                     const bool into_bottom,
                     const bool allow_nested,
                     Character *carrier )
{
    if( amount <= 0 ) {
        return 0;
    }

    // CDF: T2, the DDA by-value copy is a spawned temporary
    item &contained_item = *item::spawn_temporary( contained );
    const bool count_by_charges = contained.count_by_charges();
    if( count_by_charges ) {
        contained_item.charges = 1;
    }
    item_pocket *pocket = nullptr;

    int num_contained = 0;
    while( amount > num_contained ) {
        if( count_by_charges || pocket == nullptr ||
            !pocket->can_contain( contained_item ).success() ) {
            if( count_by_charges ) {
                contained_item.charges = 1;
            }
            pocket = best_pocket( contained_item, /*avoid=*/nullptr, allow_sealed,
                                  ignore_settings, false, false, allow_nested ).pocket;
        }
        if( pocket == nullptr ) {
            break;
        }
        if( count_by_charges ) {
            ammotype ammo = contained.ammo_type();
            if( pocket->ammo_capacity( ammo ) ) {
                contained_item.charges = std::min( amount - num_contained,
                                                   pocket->remaining_ammo_capacity( ammo ) );
            } else {
                contained_item.charges = std::min( { amount - num_contained,
                                                     pocket->charges_per_remaining_volume( contained_item ),
                                                     pocket->charges_per_remaining_weight( contained_item )
                                                   } );
            }

            if( contained_item.charges == 0 ) {
                break;
            }
        }

        ret_val<item *> result = pocket->insert_item( contained_item, into_bottom );
        if( !result.success() ) {
            if( count_by_charges ) {
                debugmsg( "charges per remaining pocket volume does not fit in that very volume: %s",
                          result.str() );
            } else {
                debugmsg( "best pocket for item cannot actually contain the item: %s", result.str() );
            }
            break;
        }
        if( count_by_charges ) {
            num_contained += contained_item.charges;
        } else {
            num_contained++;
        }
        if( unseal_pockets ) {
            pocket->unseal();
        }
        if( carrier ) {
            result.value()->on_pickup( *carrier );
        }
    }
    if( num_contained == 0 ) {
        debugmsg( "tried to put an item (%s, amount %d) in a container (%s) that cannot contain it",
                  contained_item.typeId().str(), contained_item.charges, typeId().str() );
    }
    on_contents_changed();
    return num_contained;
}

std::vector<const item *> item::all_items_top() const
{
    return contents.all_items_top( []( const item_pocket & pocket ) {
        return pocket.is_standard_type();
    } );
}

std::vector<item *> item::all_items_top()
{
    return contents.all_items_top( []( item_pocket & pocket ) {
        return pocket.is_standard_type();
    } );
}

std::vector<const item *> item::all_items_container_top() const
{
    return contents.all_items_container_top();
}

std::vector<item *> item::all_items_container_top()
{
    return contents.all_items_container_top();
}

std::vector<const item *> item::all_items_top( pocket_type pk_type ) const
{
    return contents.all_items_top( pk_type );
}

std::vector<item *> item::all_items_top( pocket_type pk_type, bool unloading )
{
    return contents.all_items_top( pk_type, unloading );
}

const item *item::this_or_single_content() const
{
    return type->category_force == item_category_container && contents_only_one_type()
           ? &contents.first_item()
           : this;
}

bool item::contents_only_one_type() const
{
    const std::vector<const item *> items = contents.all_items_top( []( item_pocket const & pkt ) {
        return pkt.is_type( pocket_type::CONTAINER );
    } );
    return items.size() == 1 ||
           ( items.size() > 1 &&
    std::all_of( ++items.begin(), items.end(), [&items]( item const * e ) {
        return e->stacks_with( *items.front() );
    } ) );
}

namespace
{
// CDF: DDA's aggregate_t also carries a stacking_info for the tname segments, which BN does not
// have; only what get_category_of_contents needs is kept.
struct aggregate_t {
    item const *header = nullptr;
    unsigned int count = 0;

    aggregate_t() = default;
    explicit aggregate_t( item const *i ) : header( i ), count( 1 ) {}

    explicit operator bool() const noexcept {
        return header != nullptr;
    }
};

template <class C>
void _aggregate( C &container, typename C::key_type const &key, item const *it,
                 aggregate_t *&running_max, aggregate_t *&max_type )
{
    auto const [iter, emplaced] = container.emplace( key, aggregate_t( it ) );
    if( emplaced && running_max == nullptr ) {
        running_max = &iter->second;
        max_type = running_max;
    } else if( !emplaced ) {
        ++iter->second.count;

        if constexpr( std::is_same_v<itype_id, typename C::key_type> ) {
            max_type = max_type->count < iter->second.count ? &iter->second : max_type;
        }

        running_max = running_max->count < iter->second.count ? &iter->second : running_max;
    }
}

aggregate_t aggregated_contents( const item &it, int depth, int maxdepth )
{
    constexpr double cutoff = 0.5;

    std::unordered_map<itype_id, aggregate_t> types;
    std::unordered_map<item_category_id, aggregate_t> cats;
    unsigned int total{};
    aggregate_t *running_max{};
    aggregate_t *max_type{};
    auto const cont_and_soft = []( item_pocket const & pkt ) {
        return pkt.is_type( pocket_type::CONTAINER );
    };

    for( item_pocket const *pk : it.get_contents().get_pockets( cont_and_soft ) ) {
        for( item const *pkit : pk->all_items_top() ) {
            total++;
            item_category_id const cat = pkit->get_category_of_contents( depth, maxdepth ).get_id();
            bool const type_ok = pkit->type->category_force != item_category_container ||
                                 cat == item_category_container;
            if( type_ok ) {
                _aggregate( types, pkit->typeId(), pkit, running_max, max_type );
            }
            _aggregate( cats, cat, pkit, running_max, max_type );
        }
    }
    if( running_max == nullptr ) {
        return {};
    }

    unsigned int const cutoff_check =
        total < 3 ? total - 1 : static_cast<int>( std::floor( total * cutoff ) );

    // alow a type to still dominate over its own category
    if( max_type->count > cutoff_check &&
        max_type->header->type->category_force != item_category_container &&
        max_type->header->get_category_shallow().get_id() ==
        running_max->header->get_category_shallow().get_id() ) {
        return *max_type;
    }

    return running_max->count > cutoff_check ? *running_max : aggregate_t{};
}
} // namespace

// DDA item.cpp:4103. CDF: no cached_category (the cache only saves time).
const item_category &item::get_category_of_contents( int depth, int maxdepth ) const
{
    if( depth < maxdepth && type->category_force == item_category_container ) {
        ++depth;
        if( aggregate_t const aggi = aggregated_contents( *this, depth, maxdepth ); aggi ) {
            return aggi.header->get_category_of_contents( depth, maxdepth );
        }
    }
    return this->get_category_shallow();
}

std::vector<const item *> item::all_items_ptr() const
{
    std::vector<const item *> all_items_internal;
    for( int i = static_cast<int>( pocket_type::CONTAINER );
         i < static_cast<int>( pocket_type::LAST ); i++ ) {
        std::vector<const item *> inserted{ all_items_top_recursive( static_cast<pocket_type>( i ) ) };
        all_items_internal.insert( all_items_internal.end(), inserted.begin(), inserted.end() );
    }
    return all_items_internal;
}

std::vector<item *> item::all_items_ptr()
{
    std::vector<item *> all_items_internal;
    for( int i = static_cast<int>( pocket_type::CONTAINER );
         i < static_cast<int>( pocket_type::LAST ); i++ ) {
        std::vector<item *> inserted{ all_items_top_recursive( static_cast<pocket_type>( i ) ) };
        all_items_internal.insert( all_items_internal.end(), inserted.begin(), inserted.end() );
    }
    return all_items_internal;
}

std::vector<const item *> item::all_items_ptr( pocket_type pk_type ) const
{
    return all_items_top_recursive( pk_type );
}

std::vector<item *> item::all_items_ptr( pocket_type pk_type )
{
    return all_items_top_recursive( pk_type );
}

std::vector<const item *> item::all_items_top_recursive( pocket_type pk_type ) const
{
    std::vector<const item *> contained = contents.all_items_top( pk_type );
    std::vector<const item *> all_items_internal{ contained };
    for( const item *it : contained ) {
        std::vector<const item *> recursion_items = it->all_items_top_recursive( pk_type );
        all_items_internal.insert( all_items_internal.end(), recursion_items.begin(),
                                   recursion_items.end() );
    }

    return all_items_internal;
}

std::vector<item *> item::all_items_top_recursive( pocket_type pk_type )
{
    std::vector<item *> contained = contents.all_items_top( pk_type );
    std::vector<item *> all_items_internal{ contained };
    for( item *it : contained ) {
        std::vector<item *> recursion_items = it->all_items_top_recursive( pk_type );
        all_items_internal.insert( all_items_internal.end(), recursion_items.begin(),
                                   recursion_items.end() );
    }

    return all_items_internal;
}

std::vector<item *> item::all_known_contents()
{
    return contents.all_known_contents();
}

std::vector<const item *> item::all_known_contents() const
{
    return contents.all_known_contents();
}

std::vector<item *> item::all_holstered_items()
{
    return contents.all_holstered_items();
}

std::vector<const item *> item::all_holstered_items() const
{
    return contents.all_holstered_items();
}

void item::clear_items()
{
    // CDF: the detached items are destroyed (DDA erases them)
    contents.clear_items();
}

size_t item::num_item_stacks() const
{
    return contents.num_item_stacks();
}

bool item::empty() const
{
    return contents.empty();
}

bool item::empty_container() const
{
    return contents.empty_container();
}

item &item::only_item()
{
    return contents.only_item();
}

const item &item::only_item() const
{
    return contents.only_item();
}

item *item::get_item_with( const std::function<bool( const item & )> &filter )
{
    return contents.get_item_with( filter );
}

const item *item::get_item_with( const std::function<bool( const item & )> &filter ) const
{
    return contents.get_item_with( filter );
}

item &item::legacy_front()
{
    return contents.legacy_front();
}

const item &item::legacy_front() const
{
    return contents.legacy_front();
}

void item::combine( const item_contents &read_input, bool convert )
{
    contents.combine( read_input, convert );
    on_contents_changed();
}

// ---------------------------------------------------------------------------------------------
// The BN container functions, now over the pockets (the pockets of BN containers are synthesized
// from their container_data by item_factory, so BN items behave as before; DDA-style items
// that only have pocket_data are containers too).
// ---------------------------------------------------------------------------------------------

bool item::is_container() const
{
    // CDF: worn storage (`carry` pockets) doesn't make a garment a container, as before pockets
    for( const item_pocket &pocket : contents.get_pockets() ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && !pocket.is_carry() ) {
            return true;
        }
    }
    return false;
}

bool item::is_container_full( bool allow_bucket ) const
{
    // DDA item_container.cpp:460
    return contents.full( allow_bucket );
}

units::volume item::get_container_capacity() const
{
    if( !is_container() ) {
        return 0_ml;
    }
    return type->container ? type->container->contains : get_volume_capacity();
}

int item::get_remaining_capacity_for_liquid( const item &liquid, bool allow_bucket,
        std::string *err ) const
{
    const auto error = [ &err ]( const std::string & message ) {
        if( err != nullptr ) {
            *err = message;
        }
        return 0;
    };

    int remaining_capacity = 0;

    // TODO: (sm) is_reloadable_with and this function call each other and can recurse for
    // watertight containers.
    if( !is_container() && is_reloadable_with( liquid.typeId() ) ) {
        if( ammo_remaining() != 0 && ammo_current() != liquid.typeId() ) {
            return error( string_format( _( "You can't mix loads in your %s." ), tname() ) );
        }
        remaining_capacity = ammo_capacity() - ammo_remaining();
    } else if( is_container() ) {
        // DDA item_container.cpp:747 (messages are BN's; DDA reports them as rem_cap_return)
        if( liquid.made_of( LIQUID ) && !contents.can_contain_liquid( true ) ) {
            return error( string_format( _( "That %s isn't water-tight." ), tname() ) );
        }
        if( can_contain_partial( liquid ).success() ) {
            if( !contents.can_contain_liquid( allow_bucket ) ) {
                return error( string_format( is_bucket() ?
                                             _( "That %s must be on the ground or held to hold contents!" )
                                             : _( "You can't seal that %s!" ), tname() ) );
            }
            remaining_capacity = contents.remaining_capacity_for_liquid( liquid );
        } else {
            return error( string_format( _( "You can't mix loads in your %s." ), tname() ) );
        }
    } else {
        return error( string_format( _( "That %1$s won't hold %2$s." ), tname(),
                                     liquid.tname() ) );
    }

    if( remaining_capacity <= 0 ) {
        return error( string_format( _( "Your %1$s can't hold any more %2$s." ), tname(),
                                     liquid.tname() ) );
    }

    return remaining_capacity;
}

int item::get_remaining_capacity_for_liquid( const item &liquid, const Character &who,
        std::string *err ) const
{
    const bool allow_bucket = who.is_wielding( *this ) || !who.has_item( *this );
    int res = get_remaining_capacity_for_liquid( liquid, allow_bucket, err );

    if( res > 0 && !type->rigid && who.has_item( *this ) ) {
        const units::volume volume_to_expand = std::max( who.free_space(),
                                               0_ml );

        res = std::min( liquid.charges_per_volume( volume_to_expand ), res );

        if( res == 0 && err != nullptr ) {
            *err = string_format( _( "That %s doesn't have room to expand." ), tname() );
        }
    }

    return res;
}

int item::get_remaining_capacity_for_id( const itype_id &liquid, bool allow_buckets ) const
{
    if( !is_container() && is_reloadable_with( liquid ) ) {
        if( ammo_remaining() != 0 && ammo_current() != liquid ) {
            return 0;
        }
        return ammo_capacity() - ammo_remaining();
    } else if( is_container() ) {
        const item &sample = *item::spawn_temporary( liquid );
        return get_remaining_capacity_for_liquid( sample, allow_buckets );
    }
    return 0;
}

detached_ptr<item> item::fill_with( detached_ptr<item> &&liquid, int amount, bool ignore_settings )
{
    if( amount == -1 ) {
        amount = INT_MAX;
    }
    amount = std::min( get_remaining_capacity_for_liquid( *liquid, true ),
                       std::min( amount, liquid->charges ) );
    if( amount <= 0 ) {
        return std::move( liquid );
    }

    if( !is_container() ) {
        if( !is_reloadable_with( liquid->typeId() ) ) {
            debugmsg( "Tried to fill %s which is not a container and can't be reloaded with %s.",
                      tname(), liquid->tname() );
            return std::move( liquid );
        }
        ammo_set( liquid->typeId(), ammo_remaining() + amount );
        liquid->mod_charges( -amount );
    } else {
        // no pocket accepts it (all disabled or filtered): refuse quietly, no debugmsg
        item &probe = *item::spawn_temporary( *liquid );
        probe.charges = 1;
        if( best_pocket( probe, nullptr, false, ignore_settings ).pocket == nullptr ) {
            return std::move( liquid );
        }
        // DDA: the liquid goes into the best pockets, a copy per pocket (T2)
        const int filled = fill_with( *liquid, amount, false, false, ignore_settings );
        if( filled <= 0 ) {
            return std::move( liquid );
        }
        liquid->mod_charges( -filled );
    }

    on_contents_changed();
    if( liquid->charges > 0 ) {
        return std::move( liquid );
    }
    return detached_ptr<item>();
}
