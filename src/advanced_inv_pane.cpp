#include "advanced_inv_pane.h"

#include "advanced_inv_area.h"
#include "avatar.h"
#include "carried_stacks.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_search.h"
#include "make_static.h"
#include "map/map.h"
#include "options.h"
#include "player.h"
#include "safe_reference.h"
#include "translations.h"
#include "uistate.h"
#include "units.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"

#include <algorithm>
#include <cassert>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#   include <SDL3/SDL.h>
#endif
namespace
{
// P3-UI5: the container a pane shows has to survive the activity that re-enters the advanced
// inventory (DDA keeps an item_location in advanced_inv_pane_save_state, which is outside this
// packet's file lock).  Keyed by the pane's save state, which follows the pane when the panes swap.
// A safe_reference, so a container that was destroyed meanwhile simply is not restored.
struct aim_container_memory {
    safe_reference<item> container;
    aim_location base_loc = NUM_AIM_LOCATIONS;
};

std::map<const advanced_inv_pane_save_state *, aim_container_memory> &aim_container_memories()
{
    // never destroyed on purpose: safe_reference must not outlive its registry at exit
    static auto *memories = new std::map<const advanced_inv_pane_save_state *, aim_container_memory>();
    return *memories;
}
} // namespace

void advanced_inventory_pane::save_settings()
{
    save_state->in_vehicle = in_vehicle();
    save_state->area_idx = get_area();
    save_state->selected_idx = index;
    save_state->filter = filter;
    save_state->sort_idx = sortby;
    if( get_container() != nullptr ) {
        aim_container_memories()[save_state] = { safe_reference<item>( get_container() ), container_base_loc };
    } else {
        aim_container_memories().erase( save_state );
    }
}

void advanced_inventory_pane::load_settings( int saved_area_idx,
        const std::array<advanced_inv_area, NUM_AIM_LOCATIONS> &squares, bool is_re_enter )
{
    const int i_location = ( get_option<bool>( "OPEN_DEFAULT_ADV_INV" ) &&
                             !is_re_enter ) ? saved_area_idx :
                           save_state->area_idx;
    aim_location location = static_cast<aim_location>( i_location );
    container = nullptr;
    container_base_loc = NUM_AIM_LOCATIONS;
    const auto remembered = aim_container_memories().find( save_state );
    if( remembered != aim_container_memories().end() && remembered->second.container ) {
        container = remembered->second.container.get();
        container_base_loc = remembered->second.base_loc;
    }
    if( location != AIM_CONTAINER ) {
        // the container is only shown in the container area
        container = nullptr;
        container_base_loc = NUM_AIM_LOCATIONS;
    }
    if( location == AIM_PARENT || ( location == AIM_CONTAINER && container == nullptr ) ) {
        // the container is gone: show the inventory instead of an empty container pane
        location = AIM_INVENTORY;
    }
    auto square = squares[location];
    // determine the square's vehicle/map item presence
    bool has_veh_items = square.can_store_in_vehicle() ?
                         !square.veh->get_items( square.vstor ).empty() : false;
    bool has_map_items = !get_map().i_at( square.pos ).empty();
    // determine based on map items and settings to show cargo
    bool show_vehicle = is_re_enter ?
                        save_state->in_vehicle : has_veh_items ? true :
                        has_map_items ? false : square.can_store_in_vehicle();
    set_area( square, show_vehicle );
    sortby = static_cast<advanced_inv_sortby>( save_state->sort_idx );
    index = save_state->selected_idx;
    filter = save_state->filter;
}

bool advanced_inventory_pane::is_filtered( const advanced_inv_listitem &it ) const
{
    return is_filtered( *it.items.front() );
}

bool advanced_inventory_pane::is_filtered( const item &it ) const
{
    if( it.has_flag( STATIC( flag_id( "HIDDEN_ITEM" ) ) ) ) {
        return true;
    }
    if( filter.empty() ) {
        return false;
    }

    const std::string str = it.tname();
    if( !filtercache.contains( str ) ) {
        const auto filter_fn = item_filter_from_string( filter );
        filtercache[str] = filter_fn;

        return !filter_fn( it );
    }

    return !filtercache[str]( it );
}

void advanced_inventory_pane::add_items_from_area( advanced_inv_area &square,
        bool vehicle_override )
{
    assert( square.id != AIM_ALL );
    square.volume = 0_ml;
    square.weight = 0_gram;
    // the container area is valid whenever the pane has a container, see below
    if( square.id != AIM_CONTAINER && !square.canputitems() ) {
        return;
    }
    map &m = get_map();
    avatar &u = get_avatar();
    // Existing items are *not* cleared on purpose, this might be called
    // several times in case all surrounding squares are to be shown.
    if( square.id == AIM_INVENTORY ) {
        const std::vector<carried_stack> stacks = carried_item_stacks( u );
        for( size_t x = 0; x < stacks.size(); ++x ) {
            std::list<item *> item_pointers;
            for( item * const &i : stacks[x] ) {
                item_pointers.push_back( i );
            }
            advanced_inv_listitem it( item_pointers, x, square.id, false );
            if( is_filtered( *it.items.front() ) ) {
                continue;
            }
            // CDF P1c: carried items live in the pockets of garments, say which one
            if( const item *holder = it.items.front()->parent_item() ) {
                it.holder = holder->tname( 1, false );
                for( const item *stacked : it.items ) {
                    if( stacked->parent_item() != holder ) {
                        it.holder += "+";
                        break;
                    }
                }
            }
            square.volume += it.volume;
            square.weight += it.weight;
            items.push_back( it );
        }
    } else if( square.id == AIM_WORN ) {
        auto iter = u.worn.begin();
        for( size_t i = 0; i < u.worn.size(); ++i, ++iter ) {
            advanced_inv_listitem it( *iter, i, 1, square.id, false );
            if( is_filtered( *it.items.front() ) ) {
                continue;
            }
            square.volume += it.volume;
            square.weight += it.weight;
            items.push_back( it );
        }
    } else if( square.id == AIM_CONTAINER ) {
        // P3-UI5: everything in the pane's container, stacked like a square's items (DDA
        // advanced_inv_pane.cpp:184-300).  Filtering does not make sense for liquid in container,
        // but a filter that hides every row is what the player asked for.
        if( container != nullptr ) {
            const bool in_vehicle_cargo = container->where() == item_location_type::vehicle;
            const advanced_inv_area::itemstack stacks = square.i_stacked( container->all_items_top() );
            for( size_t x = 0; x < stacks.size(); ++x ) {
                advanced_inv_listitem it( stacks[x], x, square.id, in_vehicle_cargo );
                if( is_filtered( *it.items.front() ) ) {
                    continue;
                }
                square.volume += it.volume;
                square.weight += it.weight;
                items.push_back( it );
            }
            square.desc[0] = container->tname( 1, false );
        } else {
            square.desc[0] = _( "Invalid container!" );
        }
    } else {
        bool is_in_vehicle = square.can_store_in_vehicle() && ( in_vehicle() || vehicle_override );
        const advanced_inv_area::itemstack &stacks = is_in_vehicle ?
                square.i_stacked( square.veh->get_items( square.vstor ) ) :
                square.i_stacked( m.i_at( square.pos ) );

        for( size_t x = 0; x < stacks.size(); ++x ) {
            advanced_inv_listitem it( stacks[x], x, square.id, is_in_vehicle );
            if( is_filtered( *it.items.front() ) ) {
                continue;
            }
            square.volume += it.volume;
            square.weight += it.weight;
            items.push_back( it );
        }
    }
}

void advanced_inventory_pane::paginate( size_t itemsPerPage )
{
    // not needed as there are no category entries here.
    if( sortby != SORTBY_CATEGORY ) {
        return;
    }
    // first, we insert all the items, then we sort the result
    for( size_t i = 0; i < items.size(); ++i ) {
        if( i % itemsPerPage == 0 ) {
            // first entry on the page, should be a category header
            if( items[i].is_item_entry() ) {
                items.insert( items.begin() + i, advanced_inv_listitem( items[i].cat ) );
            }
        }
        if( ( i + 1 ) % itemsPerPage == 0 && i + 1 < items.size() ) {
            // last entry of the page, but not the last entry at all!
            // Must *not* be a category header!
            if( items[i].is_category_header() ) {
                items.insert( items.begin() + i, advanced_inv_listitem() );
            }
        }
    }
}

void advanced_inventory_pane::fix_index()
{
    if( items.empty() ) {
        index = 0;
        return;
    }
    if( index < 0 ) {
        index = 0;
    } else if( static_cast<size_t>( index ) >= items.size() ) {
        index = static_cast<int>( items.size() ) - 1;
    }
    skip_category_headers( +1 );
}

void advanced_inventory_pane::skip_category_headers( int offset )
{
    // 0 would make no sense
    assert( offset != 0 );
    // valid index is required
    assert( static_cast<size_t>( index ) < items.size() );
    // only those two offsets are allowed
    assert( offset == -1 || offset == +1 );
    // index would not be valid, and this would be an endless loop
    assert( !items.empty() );
    while( !items[index].is_item_entry() ) {
        mod_index( offset );
    }
}

void advanced_inventory_pane::mod_index( int offset )
{
    // 0 would make no sense
    assert( offset != 0 );
    assert( !items.empty() );
    index += offset;
    if( index < 0 ) {
        index = static_cast<int>( items.size() ) - 1;
    } else if( static_cast<size_t>( index ) >= items.size() ) {
        index = 0;
    }
}

void advanced_inventory_pane::scroll_by( int offset )
{
    // 0 would make no sense
    assert( offset != 0 );
    if( items.empty() ) {
        return;
    }
    mod_index( offset );
    skip_category_headers( offset > 0 ? +1 : -1 );
}

void advanced_inventory_pane::scroll_category( int offset )
{
    // 0 would make no sense
    assert( offset != 0 );
    // only those two offsets are allowed
    assert( offset == -1 || offset == +1 );
    if( items.empty() ) {
        return;
    }
    // index must already be valid!
    assert( get_cur_item_ptr() != nullptr );
    auto cur_cat = items[index].cat;
    if( offset > 0 ) {
        while( items[index].cat == cur_cat ) {
            index++;
            // wrap to begin, stop there.
            if( static_cast<size_t>( index ) >= items.size() ) {
                index = 0;
                break;
            }
        }
    } else {
        while( items[index].cat == cur_cat ) {
            index--;
            // wrap to end, stop there.
            if( index < 0 ) {
                index = static_cast<int>( items.size() ) - 1;
                break;
            }
        }
    }
    // Make sure we land on an item entry.
    skip_category_headers( offset > 0 ? +1 : -1 );
}

advanced_inv_listitem *advanced_inventory_pane::get_cur_item_ptr()
{
    if( static_cast<size_t>( index ) >= items.size() ) {
        return nullptr;
    }
    return &items[index];
}

void advanced_inventory_pane::set_filter( const std::string &new_filter )
{
    if( filter == new_filter ) {
        return;
    }
    filter = new_filter;
    filtercache.clear();
    recalc = true;
}

item *advanced_inventory_pane::get_container() const
{
    return container;
}

aim_location advanced_inventory_pane::get_container_base_loc() const
{
    return container_base_loc;
}

void advanced_inventory_pane::enter_container( item *target, const advanced_inv_area &container_square )
{
    assert( target != nullptr );
    assert( container_square.id == AIM_CONTAINER );
    // only the first container remembers where we came from, going deeper keeps it
    if( get_area() != AIM_CONTAINER ) {
        container_base_loc = get_area();
    }
    container = target;
    set_area( container_square, false );
    index = 0;
    recalc = true;
}

void advanced_inventory_pane::leave_container()
{
    container = nullptr;
    container_base_loc = NUM_AIM_LOCATIONS;
}

aim_parent_target advanced_inventory_pane::parent_target( const item *other_pane_container ) const
{
    aim_parent_target result;
    if( container == nullptr ) {
        return result;
    }
    item *parent = container->parent_item();
    if( parent == nullptr ) {
        // an outermost container: back to the area it was found in
        result.kind = aim_parent_kind::area;
        result.area = container_base_loc >= AIM_INVENTORY && container_base_loc < NUM_AIM_LOCATIONS &&
                      container_base_loc != AIM_CONTAINER && container_base_loc != AIM_PARENT ?
                      container_base_loc : AIM_INVENTORY;
    } else if( container_base_loc == AIM_INVENTORY && parent->parent_item() == nullptr ) {
        // DDA advanced_inv.cpp:1387-1391: from a container in a worn garment or the body storage,
        // skip past the individual garments straight to the full inventory view
        result.kind = aim_parent_kind::area;
        result.area = AIM_INVENTORY;
    } else if( parent == other_pane_container ) {
        result.kind = aim_parent_kind::swap_panes;
    } else {
        result.kind = aim_parent_kind::container;
        result.container = parent;
    }
    return result;
}

void advanced_inventory_pane::scroll_page( int linesPerPage, int offset )
{
    // only those two offsets are allowed
    assert( offset == -1 || offset == +1 );
    if( items.empty() ) {
        return;
    }
    const int size = static_cast<int>( items.size() );
    const int target = index + offset * std::max( linesPerPage, 1 );
    if( target < 0 ) {
        // scroll to the top, or wrap when already there
        index = index > 0 ? 0 : size - 1;
    } else if( target >= size ) {
        // scroll to the bottom, or wrap when already there
        index = index < size - 1 ? size - 1 : 0;
    } else {
        index = target;
    }
    skip_category_headers( index == size - 1 ? -1 : +1 );
}

void advanced_inventory_pane::scroll_to_start()
{
    if( items.empty() ) {
        return;
    }
    index = 0;
    skip_category_headers( +1 );
}

void advanced_inventory_pane::scroll_to_end()
{
    if( items.empty() ) {
        return;
    }
    index = static_cast<int>( items.size() ) - 1;
    skip_category_headers( -1 );
}

units::volume advanced_inventory_pane::free_volume( const advanced_inv_area &square ) const
{
    // should be a specific location instead
    assert( area != AIM_ALL );
    if( area == AIM_CONTAINER ) {
        return container == nullptr ? 0_ml : container->get_remaining_volume();
    }
    return square.free_volume( in_vehicle() );
}

units::mass advanced_inventory_pane::free_weight_capacity() const
{
    // should be a specific location instead
    assert( area != AIM_ALL );
    if( area == AIM_CONTAINER ) {
        return container == nullptr ? 0_gram : container->get_remaining_weight_capacity();
    }
    if( area == AIM_INVENTORY || area == AIM_WORN ) {
        return get_avatar().weight_capacity() - get_avatar().weight_carried();
    }
    return units::mass_max;
}
