#include "inventory_ui.h"

#include "avatar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "carried_stacks.h"
#include "character.h"
#include "debug.h"
#include "detached_ptr.h"
#include "drop_preview.h"
#include "flag.h"
#include "game.h"
#include "game_inventory.h"
#include "ime.h"
#include "inventory.h"
#include "item.h"
#include "item_category.h"
#include "item_search.h"
#include "item_stack.h"
#include "itype.h"
#include "line.h"
#include "map/map.h"
#include "map/map_selector.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "point.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units_utility.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vehicle_selector.h"
#include "vehicle/vpart_position.h"
#include "visitable.h"

#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <tuple>
#include <vector>

/** The maximum distance from the screen edge, to snap a window to it */
static const size_t max_win_snap_distance = 4;
/** The minimal gap between two cells */
static const int min_cell_gap = 2;
/** The gap between two cells when screen space is limited*/
static const int normal_cell_gap = 4;
/** The minimal gap between the first cell and denial */
static const int min_denial_gap = 2;
/** The minimal gap between two columns */
static const int min_column_gap = 2;
/** The gap between two columns when there's enough space, but they are not centered */
static const int normal_column_gap = 8;
/**
 * The minimal occupancy ratio to align columns to the center
 * @see inventory_selector::get_columens_occupancy_ratio()
 */
static const double min_ratio_to_center = 0.85;

/** These categories should keep their original order and can't be re-sorted by inventory presets */
static const std::set<std::string> ordered_categories = {{ "ITEMS_WORN" }};

constexpr int max_chosen_count = std::numeric_limits<int>::max();

struct navigation_mode_data {
    navigation_mode next_mode;
    translation name;
    nc_color color;
};

struct inventory_input {
    std::string action;
    int ch;
    inventory_entry *entry;
};

bool inventory_entry::operator==( const inventory_entry &other ) const
{
    return get_category_ptr() == other.get_category_ptr() && locations == other.locations;
}

class selection_column_preset : public inventory_selector_preset
{
    public:
        selection_column_preset() = default;

        auto sort_compare( const inventory_entry &lhs, const inventory_entry &rhs ) const -> bool override {
            if( lhs.drop_order != rhs.drop_order ) {
                return lhs.drop_order < rhs.drop_order;
            }
            return inventory_selector_preset::sort_compare( lhs, rhs );
        }

        std::string get_caption( const inventory_entry &entry ) const override {
            std::string res;
            const size_t available_count = entry.get_available_count();
            const auto dropping_count = entry.chosen_count + entry.automatic_drop_count;
            const item *item = entry.any_item();

            if( entry.automatic_drop_count > 0 ) {
                res += colorize( "\\", c_light_gray ) + " ";
            }
            if( dropping_count > 0 && dropping_count < available_count ) {
                //~ %1$d: chosen count, %2$d: available count
                res += string_format( pgettext( "count", "%1$d of %2$d" ), dropping_count,
                                      available_count ) + " ";
            } else if( available_count != 1 ) {
                res += string_format( "%d ", available_count );
            }
            if( item->is_money() ) {
                assert( available_count == entry.get_stack_size() );
                if( dropping_count > 0 && dropping_count < available_count ) {
                    auto selected_entry = entry;
                    selected_entry.chosen_count = dropping_count;
                    res += item->display_money( available_count, entry.get_total_charges(),
                                                selected_entry.get_selected_charges() );
                } else {
                    res += item->display_money( available_count, entry.get_total_charges() );
                }
            } else {
                res += item->display_name( available_count );
            }
            return res;
        }

        nc_color get_color( const inventory_entry &entry ) const override {
            if( entry.is_item() ) {
                if( get_player_character().is_wielding( *entry.any_item() ) ) {
                    return c_light_blue;
                } else if( g->u.is_worn( *entry.any_item() ) ) {
                    return c_cyan;
                }
            }
            return inventory_selector_preset::get_color( entry );
        }
};

static const selection_column_preset selection_preset{};

std::string pickup_inventory_preset::get_denial( const item *loc ) const
{
    if( !p.has_item( *loc ) ) {
        if( loc->made_of( LIQUID ) ) {
            return _( "Can't pick up spilt liquids" );
        }
        // else if( !p.can_pick_volume( *loc ) && p.is_armed() ) {
        //     return _( "Too big to pick up" );
        // } else if( !p.can_pick_weight( *loc, !get_option<bool>( "DANGEROUS_PICKUPS" ) ) ) {
        //     return _( "Too heavy to pick up" );
        // }
    }

    return std::string();
}

int inventory_entry::get_total_charges() const
{
    int result = 0;
    for( const item *location : locations ) {
        result += location->charges;
    }
    return result;
}

int inventory_entry::get_selected_charges() const
{
    assert( chosen_count <= locations.size() );
    int result = 0;
    for( size_t i = 0; i < chosen_count; ++i ) {
        const item *location = locations[i];
        result += location->charges;
    }
    return result;
}

size_t inventory_entry::get_available_count() const
{
    if( locations.size() == 1 ) {
        return any_item()->count();
    } else {
        return locations.size();
    }
}

int inventory_entry::get_invlet() const
{
    if( custom_invlet != INT_MIN ) {
        return custom_invlet;
    }
    if( !is_item() ) {
        return '\0';
    }
    return any_item()->invlet;
}

nc_color inventory_entry::get_invlet_color() const
{
    if( !is_selectable() ) {
        return c_dark_gray;
    } else if( g->u.invlet_is_assigned( get_invlet() ) ) {
        return c_yellow;
    } else {
        return c_white;
    }
}

void inventory_entry::update_cache()
{
    cached_name = any_item()->tname( 1 );
}

const item_category *inventory_entry::get_category_ptr() const
{
    if( custom_category != nullptr ) {
        return custom_category;
    }
    if( !is_item() ) {
        return nullptr;
    }
    return &any_item()->get_category();
}

bool inventory_column::activatable() const
{
    return std::ranges::any_of( entries, []( const inventory_entry & e ) {
        return e.is_selectable();
    } );
}

inventory_entry *inventory_column::find_by_invlet( int invlet ) const
{
    for( const auto &elem : entries ) {
        if( elem.is_item() && elem.get_invlet() == invlet ) {
            return const_cast<inventory_entry *>( &elem );
        }
    }
    return nullptr;
}

size_t inventory_column::get_width() const
{
    return std::max( get_cells_width(), reserved_width );
}

size_t inventory_column::get_height() const
{
    return std::min( entries.size(), height );
}

inventory_selector_preset::inventory_selector_preset()
{
    append_cell(
    std::function<std::string( const inventory_entry & )>( [ this ]( const inventory_entry & entry ) {
        return get_caption( entry );
    } ) );
}

bool inventory_selector_preset::sort_compare( const inventory_entry &lhs,
        const inventory_entry &rhs ) const
{
    // Place items with an assigned inventory letter first, since the player cared enough to assign them
    const bool left_fav  = g->u.invlet_is_assigned( lhs.any_item()->invlet );
    const bool right_fav = g->u.invlet_is_assigned( rhs.any_item()->invlet );
    if( left_fav == right_fav ) {
        return lhs.cached_name.compare( rhs.cached_name ) < 0; // Simple alphabetic order
    } else if( left_fav ) {
        return true;
    }
    return false;
}

nc_color inventory_selector_preset::get_color( const inventory_entry &entry ) const
{
    return entry.is_item() ? entry.any_item()->color_in_inventory() : c_magenta;
}

std::function<bool( const inventory_entry & )> inventory_selector_preset::get_filter(
    const std::string &filter ) const
{
    auto item_filter = basic_item_filter( filter );

    return [item_filter]( const inventory_entry & e ) {
        return item_filter( *e.any_item() );
    };
}

std::string inventory_selector_preset::get_caption( const inventory_entry &entry ) const
{
    const size_t count = entry.get_stack_size();
    std::string disp_name;
    if( entry.any_item()->is_money() ) {
        disp_name = entry.any_item()->display_money( count, entry.get_total_charges() );
    } else {
        disp_name = entry.any_item()->display_name( count );
    }

    return ( count > 1 ) ? string_format( "%d %s", count, disp_name ) : disp_name;
}

std::string inventory_selector_preset::get_denial( const inventory_entry &entry ) const
{
    return entry.is_item() ? get_denial( entry.any_item() ) : std::string();
}

std::string inventory_selector_preset::get_cell_text( const inventory_entry &entry,
        size_t cell_index ) const
{
    if( cell_index >= cells.size() ) {
        debugmsg( "Invalid cell index %d.", cell_index );
        return "it's a bug!";
    }
    if( !entry ) {
        return std::string();
    } else if( entry.is_item() ) {
        return cells[cell_index].get_text( entry );
    } else if( cell_index != 0 ) {
        return replace_colors( cells[cell_index].title );
    } else {
        return entry.get_category_ptr()->name();
    }
}

bool inventory_selector_preset::is_stub_cell( const inventory_entry &entry,
        size_t cell_index ) const
{
    if( !entry.is_item() ) {
        return false;
    }
    const std::string &text = get_cell_text( entry, cell_index );
    return text.empty() || text == cells[cell_index].stub;
}

void inventory_selector_preset::append_cell( const
        std::function<std::string( const item * )> &func,
        const std::string &title, const std::string &stub )
{
    // Don't capture by reference here. The func should be able to die earlier than the object itself
    append_cell( std::function<std::string( const inventory_entry & )>( [ func ](
    const inventory_entry & entry ) {
        return func( entry.any_item() );
    } ), title, stub );
}

void inventory_selector_preset::append_cell( const
        std::function<std::string( const inventory_entry & )> &func,
        const std::string &title, const std::string &stub )
{
    const auto iter = std::find_if( cells.begin(), cells.end(), [ &title ]( const cell_t &cell ) {
        return cell.title == title;
    } );
    if( iter != cells.end() ) {
        debugmsg( "Tried to append a duplicate cell \"%s\": ignored.", title.c_str() );
        return;
    }
    cells.emplace_back( func, title, stub );
}

std::string  inventory_selector_preset::cell_t::get_text( const inventory_entry &entry ) const
{
    return replace_colors( func( entry ) );
}

void inventory_column::select( size_t new_index, scroll_direction dir )
{
    if( new_index < entries.size() ) {
        if( !entries[new_index].is_selectable() ) {
            new_index = next_selectable_index( new_index, dir );
        }

        selected_index = new_index;
        page_offset = ( new_index == static_cast<size_t>( -1 ) ) ?
                      0 : selected_index - selected_index % entries_per_page;
    }
}

size_t inventory_column::next_selectable_index( size_t index, scroll_direction dir ) const
{
    if( entries.empty() ) {
        return index;
    }
    // limit index to the space of the size of entries
    index = index % entries.size();
    size_t new_index = index;
    do {
        // 'new_index' incremented by 'dir' using division remainder (number of entries) to loop over the entries.
        // Negative step '-k' (backwards) is equivalent to '-k + N' (forward), where:
        //     N = entries.size()  - number of elements,
        //     k = |step|          - absolute step (k <= N).
        new_index = ( new_index + static_cast<int>( dir ) + entries.size() ) % entries.size();
    } while( new_index != index && !entries[new_index].is_selectable() );

    if( !entries[new_index].is_selectable() ) {
        return static_cast<size_t>( -1 );
    }

    return new_index;
}

void inventory_column::move_selection( scroll_direction dir )
{
    size_t index = selected_index;

    do {
        index = next_selectable_index( index, dir );
    } while( index != selected_index && is_selected_by_category( entries[index] ) );

    select( index, dir );
}

void inventory_column::move_selection_page( scroll_direction dir )
{
    size_t index = selected_index;

    do {
        const size_t next_index = next_selectable_index( index, dir );
        const bool flipped = next_index == selected_index ||
                             ( next_index > selected_index ) != ( static_cast<int>( dir ) > 0 );

        if( flipped && page_of( next_index ) == page_index() ) {
            break; // If flipped and still on the same page - no need to flip
        }

        index = next_index;
    } while( page_of( next_selectable_index( index, dir ) ) == page_index() );

    select( index, dir );
}

size_t inventory_column::get_entry_cell_width( size_t index, size_t cell_index ) const
{
    size_t res = utf8_width( get_entry_cell_cache( index ).text[cell_index], true );

    if( cell_index == 0 ) {
        res += get_entry_indent( entries[index] );
    }

    return res;
}

size_t inventory_column::get_entry_cell_width( const inventory_entry &entry,
        size_t cell_index ) const
{
    size_t res = utf8_width( preset.get_cell_text( entry, cell_index ), true );

    if( cell_index == 0 ) {
        res += get_entry_indent( entry );
    }

    return res;
}

size_t inventory_column::get_cells_width() const
{
    return std::accumulate( cells.begin(), cells.end(), static_cast<size_t>( 0 ), []( size_t lhs,
    const cell_t &cell ) {
        return lhs + cell.current_width;
    } );
}

void inventory_column::set_filter( const std::string &filter )
{
    entries_cell_cache.clear();
    paging_is_valid = false;
    prepare_paging( filter );
}

inventory_column::entry_cell_cache_t inventory_column::make_entry_cell_cache(
    const inventory_entry &entry ) const
{
    entry_cell_cache_t result;

    result.assigned = true;
    result.color = preset.get_color( entry );
    result.denial = preset.get_denial( entry );
    result.text.resize( preset.get_cells_count() );

    for( size_t i = 0, n = preset.get_cells_count(); i < n; ++i ) {
        result.text[i] = preset.get_cell_text( entry, i );
    }

    return result;
}

const inventory_column::entry_cell_cache_t &inventory_column::get_entry_cell_cache(
    size_t index ) const
{
    assert( index < entries.size() );

    if( entries_cell_cache.size() < entries.size() ) {
        entries_cell_cache.resize( entries.size() );
    }

    if( !entries_cell_cache[index].assigned ) {
        entries_cell_cache[index] = make_entry_cell_cache( entries[index] );
    }

    return entries_cell_cache[index];
}

void inventory_column::refresh_entry_cell_caches()
{
    entries_cell_cache.clear();
    for( size_t i = 0; i < entries.size(); ++i ) {
        entries_cell_cache.push_back( make_entry_cell_cache( entries[i] ) );
    }
}

void inventory_column::set_width( const size_t new_width,
                                  const std::vector<inventory_column *> &all_columns )
{
    reset_width( all_columns );
    int width_gap = get_width() - new_width;
    // Now adjust the width if we must
    while( width_gap != 0 ) {
        const int step = width_gap > 0 ? -1 : 1;
        // Should return true when lhs < rhs
        const auto cmp_for_expansion = []( const cell_t &lhs, const cell_t &rhs ) {
            return lhs.visible() && lhs.gap() < rhs.gap();
        };
        // Should return true when lhs < rhs
        const auto cmp_for_shrinking = []( const cell_t &lhs, const cell_t &rhs ) {
            if( !lhs.visible() ) {
                return false;
            }
            if( rhs.gap() <= min_cell_gap ) {
                return lhs.current_width < rhs.current_width;
            } else {
                return lhs.gap() < rhs.gap();
            }
        };

        const auto &cell = step > 0
                           ? std::min_element( cells.begin(), cells.end(), cmp_for_expansion )
                           : std::max_element( cells.begin(), cells.end(), cmp_for_shrinking );

        if( cell == cells.end() || !cell->visible() ) {
            break; // This is highly unlikely to happen, but just in case
        }

        cell->current_width += step;
        width_gap += step;
    }
    reserved_width = new_width;
}

void inventory_column::set_height( size_t new_height )
{
    if( height != new_height ) {
        if( new_height <= 1 ) {
            debugmsg( "Unable to assign height <= 1 (was %zd).", new_height );
            return;
        }
        height = new_height;
        entries_per_page = new_height;
        paging_is_valid = false;
    }
}

void inventory_column::expand_to_fit( const inventory_entry &entry )
{
    if( !entry ) {
        return;
    }

    // Don't use cell cache here since the entry may not yet be placed into the vector of entries.
    const std::string denial = preset.get_denial( entry );

    for( size_t i = 0, num = denial.empty() ? cells.size() : 1; i < num; ++i ) {
        auto &cell = cells[i];

        cell.real_width = std::max( cell.real_width, get_entry_cell_width( entry, i ) );

        // Don't reveal the cell for headers and stubs
        if( cell.visible() || ( entry.is_item() && !preset.is_stub_cell( entry, i ) ) ) {
            const size_t cell_gap = i > 0 ? normal_cell_gap : 0;
            cell.current_width = std::max( cell.current_width, cell_gap + cell.real_width );
        }
    }

    if( !denial.empty() ) {
        reserved_width = std::max( get_entry_cell_width( entry, 0 ) + min_denial_gap + utf8_width( denial,
                                   true ),
                                   reserved_width );
    }
}

void inventory_column::reset_width( const std::vector<inventory_column *> & )
{
    for( auto &elem : cells ) {
        elem = cell_t();
    }
    reserved_width = 0;
    for( auto &elem : entries ) {
        expand_to_fit( elem );
    }
}

size_t inventory_column::page_of( size_t index ) const
{
    assert( entries_per_page ); // To appease static analysis
    // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
    return index / entries_per_page;
}

size_t inventory_column::page_of( const inventory_entry &entry ) const
{
    return page_of( std::distance( entries.begin(), std::find( entries.begin(), entries.end(),
                                   entry ) ) );
}
bool inventory_column::has_available_choices() const
{
    if( !allows_selecting() || !activatable() ) {
        return false;
    }
    for( size_t i = 0; i < entries.size(); ++i ) {
        if( entries[i].is_item() && get_entry_cell_cache( i ).denial.empty() ) {
            return true;
        }
    }
    return false;
}

bool inventory_column::is_selected( const inventory_entry &entry ) const
{
    return entry == get_selected() || ( multiselect && is_selected_by_category( entry ) );
}

bool inventory_column::is_selected_by_category( const inventory_entry &entry ) const
{
    return entry.is_item() && mode == navigation_mode::CATEGORY
           && entry.get_category_ptr() == get_selected().get_category_ptr()
           && page_of( entry ) == page_index();
}

const inventory_entry &inventory_column::get_selected() const
{
    if( selected_index >= entries.size() || !entries[selected_index].is_item() ) {
        // clang complains if we use the default constructor here
        static const inventory_entry dummy( nullptr );
        return dummy;
    }
    return entries[selected_index];
}

std::vector<inventory_entry *> inventory_column::get_all_selected() const
{
    const auto filter_to_selected = [&]( const inventory_entry & entry ) {
        return is_selected( entry );
    };
    return get_entries( filter_to_selected );
}

std::vector<inventory_entry *> inventory_column::get_entries(
    const std::function<bool( const inventory_entry &entry )> &filter_func ) const
{
    std::vector<inventory_entry *> res;

    for( const auto &elem : entries ) {
        if( filter_func( elem ) ) {
            res.push_back( const_cast<inventory_entry *>( &elem ) );
        }
    }

    return res;
}

std::vector<inventory_entry *> inventory_column::get_all_entries(
    const std::function<bool( const inventory_entry &entry )> &filter_func ) const
{
    std::vector<inventory_entry *> res;

    for( const auto &elem : entries ) {
        if( filter_func( elem ) ) {
            res.push_back( const_cast<inventory_entry *>( &elem ) );
        }
    }

    for( const auto &elem : entries_hidden ) {
        if( filter_func( elem ) ) {
            res.push_back( const_cast<inventory_entry *>( &elem ) );
        }
    }

    return res;
}

std::vector<inventory_entry *> inventory_column::get_all_entries() const
{
    auto func = []( const inventory_entry & /*entry*/ ) { return true; };
    return get_all_entries( func );
}

void inventory_column::set_stack_favorite( const item *location, bool favorite )
{
    const item *selected_item = location;
    std::list<item *> to_favorite;

    if( location->where() == item_location_type::character ) {
        int position = g->u.get_item_position( selected_item );

        if( position < 0 ) {
            g->u.i_at( position ).set_favorite( !selected_item->is_favorite ); // worn/wielded
        } else {
            g->u.inv_set_stack_favorite( position, !selected_item->is_favorite ); // in inventory
        }
    } else if( location->where() == item_location_type::map ) {
        auto items = g->m.i_at( location->bub_pos() );

        for( auto &item : items ) {
            if( item->stacks_with( *selected_item ) ) {
                to_favorite.push_back( item );
            }
        }
        for( auto &item : to_favorite ) {
            item->set_favorite( favorite );
        }
    } else if( location->where() == item_location_type::vehicle ) {
        const std::optional<vpart_reference> vp = g->m.veh_at(
                    location->abs_pos() ).part_with_feature( "CARGO", true );
        assert( vp );

        auto items = vp->vehicle().get_items( vp->part_index() );

        for( auto &item : items ) {
            if( item->stacks_with( *selected_item ) ) {
                to_favorite.push_back( item );
            }
        }
        for( auto *item : to_favorite ) {
            item->set_favorite( favorite );
        }
    }
}

void inventory_column::on_input( const inventory_input &input )
{
    if( empty() || !active ) {
        return; // ignore
    }

    if( input.action == "DOWN" ) {
        move_selection( scroll_direction::FORWARD );
    } else if( input.action == "UP" ) {
        move_selection( scroll_direction::BACKWARD );
    } else if( input.action == "PAGE_DOWN" ) {
        move_selection_page( scroll_direction::FORWARD );
    } else if( input.action == "PAGE_UP" ) {
        move_selection_page( scroll_direction::BACKWARD );
    } else if( input.action == "HOME" ) {
        select( 0, scroll_direction::FORWARD );
    } else if( input.action == "EXAMINE" ) {
        const auto &highlighed =  get_selected().any_item();

        std::vector<iteminfo> this_item = highlighed->info();
        item_info_data dummy( highlighed->display_name(), {}, this_item, {} );
        dummy.handle_scrolling = true;
        draw_item_info( []() -> catacurses::window {
            const int width = std::min( TERMX, FULL_SCREEN_WIDTH ) + std::max( 0, TERMX - FULL_SCREEN_WIDTH ) * ( 1.0 / 4 );
            const int height = std::min( TERMY, FULL_SCREEN_HEIGHT ) + std::max( 0, TERMY - FULL_SCREEN_HEIGHT ) * ( 2.0 / 3 );
            return catacurses::newwin( height, width, point( ( TERMX - width ) / 2, ( TERMY - height ) / 2 ) );
        }, dummy );

        // recalc = true;
        // keepline = true;
    } else if( input.action == "END" ) {
        select( entries.size() - 1, scroll_direction::BACKWARD );
    } else if( input.action == "TOGGLE_FAVORITE" ) {
        auto selected_entries = get_all_selected();
        for( auto &entry_ptr : selected_entries ) {
            const item *loc = entry_ptr->any_item();
            set_stack_favorite( loc, !loc->is_favorite );
        }
        refresh_entry_cell_caches();
    }
}

void inventory_column::add_entry( const inventory_entry &entry )
{
    if( std::find( entries.begin(), entries.end(), entry ) != entries.end() ) {
        debugmsg( "Tried to add a duplicate entry." );
        return;
    }
    const auto iter = std::find_if( entries.rbegin(), entries.rend(),
    [ &entry ]( const inventory_entry & cur ) {
        const item_category *cur_cat = cur.get_category_ptr();
        const item_category *new_cat = entry.get_category_ptr();

        return cur_cat == new_cat || ( cur_cat != nullptr && new_cat != nullptr
                                       && ( *cur_cat == *new_cat || *cur_cat < *new_cat ) );
    } );
    entries.insert( iter.base(), entry );
    entries_cell_cache.clear();
    expand_to_fit( entry );
    paging_is_valid = false;
}

void inventory_column::move_entries_to( inventory_column &dest )
{
    for( const auto &elem : entries ) {
        if( elem.is_item() &&
            // this column already has this entry, no need to try to add it again
            std::find( dest.entries.begin(), dest.entries.end(), elem ) == dest.entries.end() ) {
            dest.add_entry( elem );
        }
    }
    dest.prepare_paging();
    clear();
}

// CDF: Q2 flat view; carried-item rows are hidden while the holding garment is collapsed.
// Per DDA inventory_ui.cpp:585-600 (adapted: item * instead of item_location)
static auto carried_holder( const inventory_entry &entry ) -> item *
{
    if( !entry.is_item() || entry.get_category_ptr() == nullptr ||
        entry.get_category_ptr()->get_id().str().rfind( "CARRY_GROUP:", 0 ) != 0 ) {
        return nullptr;
    }
    return entry.any_item()->parent_item();
}

static auto hidden_by_collapse( const inventory_entry &entry ) -> bool
{
    // Per DDA inventory_ui.cpp:575-600: hierarchy rows under a collapsed pocket of their parents
    if( entry.is_item() && entry.topmost_parent != nullptr ) {
        const item *it = entry.any_item();
        while( it->parent_item() != nullptr && it != entry.topmost_parent ) {
            const item *parent = it->parent_item();
            const item_pocket *pocket = parent->contents.pocket_of( it );
            if( pocket != nullptr && pocket->settings.is_collapsed() ) {
                return true;
            }
            it = parent;
        }
    }
    const item *holder = carried_holder( entry );
    return holder != nullptr && holder->is_collapsed();
}

// Per DDA inventory_ui.cpp:139,1432 (path_to_top, indented_sort_compare), item * instead of item_location
static auto path_to_top( const inventory_entry &e,
                         const inventory_selector_preset &pr ) -> std::vector<item *>
{
    item *it = e.any_item();
    std::vector<item *> path{ it };
    while( it->parent_item() != nullptr ) {
        it = const_cast<item *>( it->parent_item() );
        if( pr.is_shown( it ) ) {
            path.emplace_back( it );
        }
    }
    return path;
}

static auto indented_sort_compare( const inventory_entry &lhs, const inventory_entry &rhs,
                                   const inventory_selector_preset &preset ) -> bool
{
    // place children below all parents
    const std::vector<item *> path_lhs = path_to_top( lhs, preset );
    const std::vector<item *> path_rhs = path_to_top( rhs, preset );
    const size_t common_depth = std::min( path_lhs.size(), path_rhs.size() );
    size_t li = path_lhs.size() - common_depth;
    size_t ri = path_rhs.size() - common_depth;
    item *p_lhs = path_lhs[li];
    item *p_rhs = path_rhs[ri];
    if( p_lhs == p_rhs ) {
        return path_lhs.size() < path_rhs.size();
    }
    // otherwise sort the entries below their lowest common ancestor
    while( li < path_lhs.size() && path_lhs[li] != path_rhs[ri] ) {
        p_lhs = path_lhs[li++];
        p_rhs = path_rhs[ri++];
    }
    inventory_entry ep_lhs( { p_lhs }, nullptr, true );
    inventory_entry ep_rhs( { p_rhs }, nullptr, true );
    ep_lhs.update_cache();
    ep_rhs.update_cache();
    return preset.sort_compare( ep_lhs, ep_rhs );
}

void inventory_column::prepare_paging( const std::string &filter )
{
    if( paging_is_valid ) {
        return;
    }

    entries_cell_cache.clear();

    const auto filter_fn = filter_from_string<inventory_entry>(
    filter, [this]( const std::string & filter ) {
        return preset.get_filter( filter );
    } );

    // First, remove all non-items
    for( size_t i = 0; i < entries_hidden.size(); ++i ) {
        entries.push_back( entries_hidden[i] );
    }

    entries_hidden.clear();
    for( size_t i = 0; i < entries.size(); ++i ) {
        if( entries[i].is_item() && ( !filter_fn( entries[i] ) || hidden_by_collapse( entries[i] ) ) ) {
            entries_hidden.push_back( entries[i] );
        }
    }

    const auto new_end = std::remove_if( entries.begin(),
    entries.end(), [&filter_fn]( const inventory_entry & entry ) {
        return !entry.is_item() || !filter_fn( entry ) || hidden_by_collapse( entry );
    } );
    entries.erase( new_end, entries.end() );
    // don't sort with stale names
    std::ranges::for_each( entries, &inventory_entry::update_cache );
    // Then sort them with respect to categories
    auto sort_function = [this]( const inventory_entry & lhs, const inventory_entry & rhs ) {
        if( *lhs.get_category_ptr() != *rhs.get_category_ptr() ) {
            return *lhs.get_category_ptr() < *rhs.get_category_ptr();
        } else if( hierarchy ) {
            return indented_sort_compare( lhs, rhs, preset );
        } else {
            return preset.sort_compare( lhs, rhs );
        }
    };
    std::ranges::stable_sort( entries, sort_function );

    // Recover categories
    const item_category *current_category = nullptr;
    for( auto iter = entries.begin(); iter != entries.end(); ++iter ) {
        if( iter->get_category_ptr() == current_category ) {
            continue;
        }
        current_category = iter->get_category_ptr();
        iter = entries.insert( iter, inventory_entry( current_category ) );
        expand_to_fit( *iter );
    }
    // Determine the new height.
    entries_per_page = height;
    if( entries.size() > entries_per_page ) {
        entries_per_page -= 1;  // Make room for the page number.
        for( size_t i = entries_per_page - 1; i < entries.size(); i += entries_per_page ) {
            auto iter = std::next( entries.begin(), i );
            if( iter->is_category() ) {
                // The last item on the page must not be a category.
                entries.insert( iter, inventory_entry() );
            } else {
                // The first item on the next page must be a category.
                iter = std::next( iter );
                if( iter != entries.end() && iter->is_item() ) {
                    entries.insert( iter, inventory_entry( iter->get_category_ptr() ) );
                }
            }
        }
    }
    entries_cell_cache.clear();
    paging_is_valid = true;
    // Select the uppermost possible entry
    select( selected_index, selected_index ? scroll_direction::BACKWARD : scroll_direction::FORWARD );
}

void inventory_column::clear()
{
    entries.clear();
    entries_cell_cache.clear();
    paging_is_valid = false;
}

bool inventory_column::select( const item *loc )
{
    for( size_t index = 0; index < entries.size(); ++index ) {
        if( entries[index].is_selectable() && entries[index].any_item() == loc ) {
            select( index, scroll_direction::FORWARD );
            return true;
        }
    }
    return false;
}

auto inventory_column::select_position_if_item_type( const size_t new_index,
        const itype_id &type ) -> bool
{
    if( new_index >= entries.size() || !entries[new_index].is_selectable() ||
        entries[new_index].any_item()->typeId() != type ) {
        return false;
    }
    select( new_index, scroll_direction::FORWARD );
    return true;
}

size_t inventory_column::get_entry_indent( const inventory_entry &entry ) const
{
    if( !entry.is_item() ) {
        return 0;
    }

    size_t res = 2;
    if( hierarchy ) {
        res += entry.indent;
    }
    if( get_option<bool>( "ITEM_SYMBOLS" ) ) {
        res += 2;
    }
    if( allows_selecting() && activatable() && multiselect ) {
        res += 2;
    }
    return res;
}

int inventory_column::reassign_custom_invlets( const player &p, int min_invlet, int max_invlet )
{
    int cur_invlet = min_invlet;
    for( auto &elem : entries ) {
        // Only items on map/in vehicles: those that the player does not possess.
        if( elem.is_selectable() && !p.has_item( *elem.any_item() ) ) {
            elem.custom_invlet = cur_invlet <= max_invlet ? cur_invlet++ : '\0';
        }
    }
    return cur_invlet;
}

void inventory_column::draw( const catacurses::window &win, point pos ) const
{
    if( !visible() ) {
        return;
    }

    const auto available_cell_width = [ this ]( size_t index, size_t cell_index ) {
        const size_t displayed_width = cells[cell_index].current_width;
        const size_t real_width = get_entry_cell_width( index, cell_index );

        return displayed_width > real_width ? displayed_width - real_width : 0;
    };

    // Do the actual drawing
    for( size_t index = page_offset, line = 0; index < entries.size() &&
         line < entries_per_page; ++index, ++line ) {
        const auto &entry = entries[index];
        const auto &entry_cell_cache = get_entry_cell_cache( index );

        if( !entry ) {
            continue;
        }

        int x1 = pos.x + get_entry_indent( entry );
        int x2 = pos.x + std::max( static_cast<int>( reserved_width - get_cells_width() ), 0 );
        int yy = pos.y + line;

        const bool selected = active && is_selected( entry );

        if( selected && visible_cells() > 1 ) {
            for( int hx = x1, hx_max = pos.x + get_width(); hx < hx_max; ++hx ) {
                mvwputch( win, point( hx, yy ), h_white, ' ' );
            }
        }

        const std::string &denial = entry_cell_cache.denial;

        if( !denial.empty() ) {
            const size_t max_denial_width = std::max( static_cast<int>( get_width() - ( min_denial_gap +
                                            get_entry_cell_width( index, 0 ) ) ), 0 );
            const size_t denial_width = std::min( max_denial_width, static_cast<size_t>( utf8_width( denial,
                                                  true ) ) );

            trim_and_print( win, point( pos.x + get_width() - denial_width, yy ), denial_width, c_red, denial );
        }

        const size_t count = denial.empty() ? cells.size() : 1;

        for( size_t cell_index = 0; cell_index < count; ++cell_index ) {
            if( !cells[cell_index].visible() ) {
                continue; // Don't show empty cells
            }

            if( line != 0 && cell_index != 0 && entry.is_category() ) {
                break; // Don't show duplicated titles
            }

            x2 += cells[cell_index].current_width;

            size_t text_width = utf8_width( entry_cell_cache.text[cell_index], true );
            size_t text_gap = cell_index > 0 ? std::max( cells[cell_index].gap(), min_cell_gap ) : 0;
            size_t available_width = x2 - x1 - text_gap;

            if( text_width > available_width ) {
                // See if we can steal some of the needed width from an adjacent cell
                if( cell_index == 0 && count >= 2 ) {
                    available_width += available_cell_width( index, 1 );
                } else if( cell_index > 0 ) {
                    available_width += available_cell_width( index, cell_index - 1 );
                }
                text_width = std::min( text_width, available_width );
            }

            if( text_width > 0 ) {
                const int text_x = cell_index == 0 ? x1 : x2 -
                                   text_width; // Align either to the left or to the right
                const std::string &text = entry_cell_cache.text[cell_index];

                // Per DDA inventory_ui.cpp:1800: chevron before an entry that has visible contents
                if( cell_index == 0 && entry.chevron && entry.is_item() ) {
                    trim_and_print( win, point( text_x - 1, yy ), 1, c_dark_gray,
                                    entry.any_item()->is_collapsed() ? "▶" : "▼" );
                }
                if( entry.is_item() && ( selected || !entry.is_selectable() ) ) {
                    trim_and_print( win, point( text_x, yy ), text_width, selected ? h_white : c_dark_gray,
                                    remove_color_tags( text ) );
                } else {
                    trim_and_print( win, point( text_x, yy ), text_width, entry_cell_cache.color, text );
                }
            }

            x1 = x2;
        }

        if( entry.is_item() ) {
            int xx = pos.x;
            if( entry.get_invlet() != '\0' ) {
                mvwputch( win, point( pos.x, yy ), entry.get_invlet_color(), entry.get_invlet() );
            } else if( entry.invlet_hint ) {
                mvwputch( win, point( pos.x, yy ), entry.invlet_hint->color,
                          entry.invlet_hint->invlet );
            }
            xx += 2;
            if( get_option<bool>( "ITEM_SYMBOLS" ) ) {
                const nc_color color = entry.any_item()->color();
                mvwputch( win, point( xx, yy ), color, entry.any_item()->symbol() );
                xx += 2;
            }
            if( allows_selecting() && activatable() && multiselect ) {
                if( entry.automatic_drop_count > 0 ) {
                    mvwputch( win, point( xx, yy ), c_light_gray, '\\' );
                } else if( entry.chosen_count == 0 ) {
                    mvwputch( win, point( xx, yy ), c_dark_gray, '-' );
                } else if( entry.chosen_count >= entry.get_available_count() ) {
                    mvwputch( win, point( xx, yy ), c_light_green, '+' );
                } else {
                    mvwputch( win, point( xx, yy ), c_light_green, '#' );
                }
            }
        }
    }

    if( pages_count() > 1 ) {
        mvwprintw( win, pos + point( 0, height - 1 ), _( "Page %d/%d" ), page_index() + 1, pages_count() );
    }
}

size_t inventory_column::visible_cells() const
{
    return std::count_if( cells.begin(), cells.end(), []( const cell_t &elem ) {
        return elem.visible();
    } );
}

selection_column::selection_column( const std::string &id, const std::string &name ) :
    inventory_column( selection_preset ),
    selected_cat( id, no_translation( name ), 0 ) {}

selection_column::~selection_column() = default;

void selection_column::reset_width( const std::vector<inventory_column *> &all_columns )
{
    inventory_column::reset_width( all_columns );

    const auto always_yes = []( const inventory_entry & ) {
        return true;
    };

    for( const inventory_column *const col : all_columns ) {
        if( col && !dynamic_cast<const selection_column *>( col ) ) {
            for( const inventory_entry *const ent : col->get_entries( always_yes ) ) {
                if( ent ) {
                    expand_to_fit( *ent );
                }
            }
        }
    }
}

void selection_column::prepare_paging( const std::string &filter )
{
    inventory_column::prepare_paging( filter );

    if( entries.empty() ) { // Category must always persist
        entries.emplace_back( &*selected_cat );
        expand_to_fit( entries.back() );
    }

    if( !last_changed.is_null() ) {
        const auto iter = std::find( entries.begin(), entries.end(), last_changed );
        if( iter != entries.end() ) {
            select( std::distance( entries.begin(), iter ), scroll_direction::FORWARD );
        }
        last_changed = inventory_entry();
    }
}

void selection_column::on_change( const inventory_entry &entry )
{
    inventory_entry my_entry( entry, &*selected_cat );

    auto iter = std::find( entries.begin(), entries.end(), my_entry );

    if( iter == entries.end() ) {
        if( my_entry.chosen_count == 0 && my_entry.automatic_drop_count == 0 ) {
            return; // Not interested.
        }
        add_entry( my_entry );
        last_changed = my_entry;
    } else if( iter->chosen_count != my_entry.chosen_count ||
               iter->automatic_drop_count != my_entry.automatic_drop_count ||
               iter->drop_order != my_entry.drop_order ) {
        if( my_entry.chosen_count > 0 || my_entry.automatic_drop_count > 0 ) {
            iter->chosen_count = my_entry.chosen_count;
            iter->automatic_drop_count = my_entry.automatic_drop_count;
            iter->drop_order = my_entry.drop_order;
            expand_to_fit( my_entry );
        } else {
            iter = entries.erase( iter );
        }
        paging_is_valid = false;

        if( iter != entries.end() ) {
            last_changed = *iter;
        }
        refresh_entry_cell_caches();
    }
}

// TODO: Move it into some 'item_stack' class.
static std::vector<std::list<item *>> restack_items( const item_stack::const_iterator &from,
                                   const item_stack::const_iterator &to, bool check_components = false )
{
    std::vector<std::list<item *>> res;

    for( auto it = from; it != to; ++it ) {
        auto match = std::find_if( res.begin(), res.end(),
        [ &it, check_components ]( const std::list<item *> &e ) {
            return ( *it )->display_stacked_with( *const_cast<item *>( e.back() ), check_components );
        } );

        if( match != res.end() ) {
            match->push_back( const_cast<item *>( *it ) );
        } else {
            res.emplace_back( 1, const_cast<item *>( *it ) );
        }
    }

    return res;
}

static std::vector<std::list<item *>> restack_items( const std::vector<item *>::const_iterator
                                   &from,
                                   const std::vector<item *>::const_iterator &to, bool check_components = false )
{
    std::vector<std::list<item *>> res;

    for( auto it = from; it != to; ++it ) {
        auto match = std::find_if( res.begin(), res.end(),
        [ &it, check_components ]( const std::list<item *> &e ) {
            return ( *it )->display_stacked_with( *const_cast<item *>( e.back() ), check_components );
        } );

        if( match != res.end() ) {
            match->push_back( const_cast<item *>( *it ) );
        } else {
            res.emplace_back( 1, const_cast<item *>( *it ) );
        }
    }

    return res;
}

const item_category *inventory_selector::naturalize_category( const item_category &category,
        const tripoint_bub_ms &pos )
{
    const auto find_cat_by_id = [ this ]( const item_category_id & id ) {
        const auto iter = std::find_if( categories.begin(),
        categories.end(), [ &id ]( const item_category & cat ) {
            return cat.get_id() == id;
        } );
        return iter != categories.end() ? &*iter : nullptr;
    };

    const int dist = rl_dist( u.bub_pos(), pos );

    if( dist != 0 ) {
        const std::string suffix = direction_suffix( u.bub_pos().raw(), pos.raw() );
        const item_category_id id = item_category_id( string_format( "%s_%s", category.get_id().c_str(),
                                    suffix.c_str() ) );

        const auto existing = find_cat_by_id( id );
        if( existing != nullptr ) {
            return existing;
        }

        const std::string name = string_format( "%s %s", category.name(), suffix.c_str() );
        const int sort_rank = category.sort_rank() + dist;
        const item_category new_category( id, no_translation( name ), sort_rank );

        categories.push_back( new_category );
    } else {
        const item_category *const existing = find_cat_by_id( category.get_id() );
        if( existing != nullptr ) {
            return existing;
        }

        categories.push_back( category );
    }

    return &categories.back();
}

void inventory_selector::add_entry( inventory_column &target_column,
                                    std::vector<item *> &&locations,
                                    const item_category *custom_category )
{
    if( !preset.is_shown( locations.front() ) ) {
        return;
    }

    is_empty = false;
    inventory_entry entry( locations, custom_category,
                           preset.get_denial( locations.front() ).empty() );

    target_column.add_entry( entry );

    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->mark_resize();
    }
}

void inventory_selector::add_item( inventory_column &target_column,
                                   item *location,
                                   const item_category *custom_category )
{
    add_entry( target_column,
               std::vector<item *>( 1, location ),
               custom_category );
}

void inventory_selector::add_items( inventory_column &target_column,
                                    const std::function<item*( item * )> &locator,
                                    const std::vector<std::list<item *>> &stacks,
                                    const item_category *custom_category )
{
    const item_category *nat_category = nullptr;

    for( const auto &elem : stacks ) {
        std::vector<item *> locations;
        std::transform( elem.begin(), elem.end(), std::back_inserter( locations ), locator );
        item *const &loc = locations.front();

        if( custom_category == nullptr ) {
            nat_category = &loc->get_category();
        } else if( nat_category == nullptr && preset.is_shown( loc ) ) {
            nat_category = naturalize_category( *custom_category, loc->bub_pos() );
        }

        add_entry( target_column, std::move( locations ), nat_category );
    }
}

void inventory_selector::remove_item( item *location )
{
    for( auto col_ptr : columns ) {
        std::vector<inventory_entry *> entries = col_ptr->get_all_entries();
        for( auto entry_ptr : entries ) {
            if( entry_ptr->is_item() ) {
                auto iter = std::remove( entry_ptr->locations.begin(), entry_ptr->locations.end(), location );
                entry_ptr->locations.erase( iter, entry_ptr->locations.end() );
            }
        }

        auto remove_func = []( const inventory_entry & e ) {
            return e.locations.empty();
        };

        auto iter = std::remove_if( col_ptr->entries.begin(), col_ptr->entries.end(), remove_func );
        col_ptr->entries.erase( iter, col_ptr->entries.end() );

        auto iter_hidden = std::remove_if( col_ptr->entries_hidden.begin(), col_ptr->entries_hidden.end(),
                                           remove_func );
        col_ptr->entries_hidden.erase( iter_hidden, col_ptr->entries_hidden.end() );

        col_ptr->paging_is_valid = false;
        col_ptr->prepare_paging();
    }

}


// Per DDA inventory_ui.cpp:208 (is_worn_ablative), adapted: item * and pocket_of() instead of
// item_location::parent_pocket()
static auto is_worn_ablative( const Character &who, const item *container,
                              const item *child ) -> bool
{
    if( container == nullptr || !container->is_ablative() || !who.is_worn( *container ) ) {
        return false;
    }
    const item_pocket *slot = container->contents.pocket_of( child );
    return slot != nullptr && slot->get_data().ablative;
}

// Per DDA inventory_ui.cpp:132
static auto get_topmost_parent( const item *topmost, const item *loc,
                                bool loc_shown ) -> const item *
{
    return loc_shown ? ( topmost != nullptr ? topmost : loc ) : nullptr;
}

// Per DDA inventory_ui.cpp:189 (wielded_worn_category)
static auto wielded_worn_category( const item *loc, const Character &u ) -> const item_category *
{
    if( loc == &u.primary_weapon() ) {
        return &item_category_id( "WEAPON_HELD" ).obj();
    }
    if( u.is_worn( *loc ) || is_worn_ablative( u, loc->parent_item(), loc ) ) {
        // CDF: no BIONIC_FUEL_SOURCE / INTEGRATED categories or flags exist here
        return &item_category_id( "ITEMS_WORN" ).obj();
    }
    return nullptr;
}

void inventory_selector::add_shown_entry( inventory_column &target_column,
        std::vector<item *> &&locations, const item_category *custom_category,
        const item *topmost_parent, bool chevron, int indent )
{
    is_empty = false;
    inventory_entry entry( locations, custom_category,
                           preset.get_denial( locations.front() ).empty() );
    entry.topmost_parent = topmost_parent;
    entry.chevron = chevron;
    entry.indent = indent;
    target_column.add_entry( entry );

    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->mark_resize();
    }
}

bool inventory_selector::add_entry_rec( inventory_column &entry_column,
                                        inventory_column &children_column, item *loc,
                                        const item_category *entry_category,
                                        const item_category *children_category,
                                        const item *topmost_parent, int indent )
{
    // is_shown can be costly, so ask once per item
    const bool shown = preset.is_shown( loc );
    inventory_column temp_children( preset );
    bool vis_contents = add_contained_items( loc, temp_children, children_category,
                        get_topmost_parent( topmost_parent, loc, shown ),
                        shown ? indent + 2 : indent );
    if( shown ) {
        add_shown_entry( entry_column, std::vector<item *>( 1, loc ), entry_category,
                         topmost_parent, vis_contents, indent );
        vis_contents = true;
    }
    temp_children.move_entries_to( children_column );
    return vis_contents;
}

bool inventory_selector::add_contained_items( item *container, inventory_column &column,
        const item_category *custom_category, const item *topmost_parent, int indent )
{
    if( container->has_flag( flag_NO_UNLOAD ) ) {
        return false;
    }
    bool vis_top = false;
    inventory_column temp( preset );
    for( item *it : container->all_items_top() ) {
        const item_category *hacked_cat = custom_category;
        inventory_column *hacked_col = &column;
        if( is_worn_ablative( u, container, it ) ) {
            hacked_cat = &item_category_id( "ITEMS_WORN" ).obj();
            hacked_col = &own_gear_column;
        }
        vis_top |= add_entry_rec( *hacked_col, temp, it, hacked_cat, custom_category,
                                  topmost_parent, indent );
    }
    temp.move_entries_to( column );
    return vis_top;
}

void inventory_selector::add_character_items( Character &character )
{
    // Per DDA inventory_ui.cpp:2196: hierarchy mode lists gear with its contents as a tree
    if( _uimode == uimode::hierarchy ) {
        item &weapon = character.primary_weapon();
        if( !weapon.is_null() ) {
            const item_category *cat = &item_category_id( "WEAPON_HELD" ).obj();
            add_entry_rec( own_gear_column, own_gear_column, &weapon, cat, cat, nullptr, 0 );
        }
        for( item *worn_item : character.top_items_loc() ) {
            const item_category *cat = wielded_worn_category( worn_item, u );
            add_entry_rec( own_gear_column, own_gear_column, worn_item, cat, cat, nullptr, 0 );
        }
        return;
    }
    character.visit_items( [ this, &character ]( item * it ) {
        if( character.is_wielding( *it ) ) {
            add_item( own_gear_column,  it,
                      &item_category_id( "WEAPON_HELD" ).obj() );
        } else if( character.is_worn( *it ) ) {
            add_item( own_gear_column, it,
                      &item_category_id( "ITEMS_WORN" ).obj() );
        } else if( const item *carrier = it->parent_item() ) {
            // armor plates in a worn carrier are listed with the worn armor (DDA #55978)
            const item_pocket *slot = carrier->contents.pocket_of( it );
            if( slot != nullptr && slot->get_data().ablative && character.is_worn( *carrier ) ) {
                add_item( own_gear_column, it,
                          &item_category_id( "ITEMS_WORN" ).obj() );
            }
        }
        return VisitResponse::NEXT;
    } );
    // Visitable interface does not support stacks so it has to be here
    // CDF P1c step 22: carried items are grouped under a header per garment (or the body storage)
    for( const carried_stack &elem : carried_item_stacks( character ) ) {
        const std::vector<std::list<item *>> stacks = restack_items( elem.begin(), elem.end(),
                preset.get_checking_components() );
        for( const std::list<item *> &stack : stacks ) {
            const item *holder = stack.front()->parent_item();
            const item_category *group = nullptr;
            if( holder != nullptr ) {
                units::volume used = 0_ml;
                units::volume cap = 0_ml;
                for( const item_pocket &pocket : holder->contents.get_pockets() ) {
                    if( pocket.is_carry() && pocket.is_type( pocket_type::CONTAINER ) ) {
                        cap += pocket.volume_capacity();
                        used += pocket.volume_capacity() - pocket.remaining_volume();
                    }
                }
                const std::string title = string_format( "%s (%s / %s)",
                                          to_upper_case( holder->tname( 1, false ) ),
                                          format_volume( used ), format_volume( cap ) );
                const item_category_id gid( "CARRY_GROUP:" + title );
                const auto found = std::find_if( categories.begin(), categories.end(),
                [&gid]( const item_category & c ) {
                    return c.get_id() == gid;
                } );
                if( found != categories.end() ) {
                    group = &*found;
                } else {
                    categories.emplace_back( gid, no_translation( title ), 100 );
                    group = &categories.back();
                }
            }
            std::vector<item *> locations( stack.begin(), stack.end() );
            add_entry( own_inv_column, std::move( locations ), group );
        }
    }
}

void inventory_selector::add_map_items( const tripoint_bub_ms &target )
{
    if( g->m.accessible_items( target ) ) {
        const auto items = g->m.i_at( target );
        const std::string name = to_upper_case( g->m.name( target ) );
        const item_category map_cat( name, no_translation( name ), 100 );

        add_items( map_column, []( item * it ) {
            return it;
        }, restack_items( items.begin(), items.end(), preset.get_checking_components() ), &map_cat );
    }
}

void inventory_selector::add_vehicle_items( const tripoint_bub_ms &target )
{
    const std::optional<vpart_reference> vp = g->m.veh_at( target ).part_with_feature( "CARGO", true );
    if( !vp ) {
        return;
    }
    vehicle *const veh = &vp->vehicle();
    const int part = vp->part_index();
    vehicle_stack items = veh->get_items( part );
    const std::string name = to_upper_case( remove_color_tags( veh->part( part ).name() ) );
    const item_category vehicle_cat( name, no_translation( name ), 200 );

    const auto check_components = this->preset.get_checking_components();

    add_items( map_column, []( item * it ) {
        return it;
    }, restack_items( items.begin(), items.end(), check_components ), &vehicle_cat );
}

void inventory_selector::add_nearby_items( int radius )
{
    nearby_radius = radius;
    if( radius >= 0 ) {
        for( const tripoint_bub_ms &pos : closest_points_first( u.bub_pos(), radius ) ) {
            // can not reach this -> can not access its contents
            if( u.bub_pos() != pos &&
                !g->m.clear_path( u.bub_pos(), pos, rl_dist( u.bub_pos(), pos ), 1, 100 ) ) {
                continue;
            }
            add_map_items( pos );
            add_vehicle_items( pos );
        }
    }
}

void inventory_selector::add_fake_items( Character &character )
{
    fake_items_added = true;
    for( bionic bio : character.get_bionic_collection() ) {
        const itype_id fake = bio.info().fake_item;
        if( bio.info().has_flag( flag_BIONIC_TOOLS ) && !fake.is_null() && fake.str() != "" ) {
            item *fakeitem = g->add_fake_item( item::spawn( fake ) );
            add_entry( own_gear_column, std::vector<item *>( 1, fakeitem ),
                       &item_category_id( "MISC_USABLES" ).obj() );
        }
    }
    for( const itype_id &fake : character.get_enchantment_fake_items() ) {
        item *fakeitem = g->add_fake_item( item::spawn( fake ) );
        add_entry( own_gear_column, std::vector<item *>( 1, fakeitem ),
                   &item_category_id( "MISC_USABLES" ).obj() );
    }
}

void inventory_selector::enable_view_mode_toggle()
{
    view_toggle_enabled = true;
    ctxt.register_action( "VIEW_CATEGORY_MODE", to_translation( "Switch view mode" ) );
}

void inventory_selector::toggle_categorize_contained()
{
    // Per DDA inventory_ui.cpp:3821. DDA re-labels the existing entries; CDF's category mode has no
    // contained rows, so the entries are rebuilt from the same sources in the other mode.
    const item *highlighted = nullptr;
    if( has_available_choices() && get_selected().is_item() ) {
        highlighted = get_selected().any_item();
    }
    _uimode = _uimode == uimode::hierarchy ? uimode::categories : uimode::hierarchy;
    clear_items();
    for( inventory_column *col : columns ) {
        col->set_hierarchy( _uimode == uimode::hierarchy );
    }
    add_character_items( u );
    if( nearby_radius >= 0 ) {
        add_nearby_items( nearby_radius );
    }
    if( fake_items_added ) {
        add_fake_items( u );
    }
    if( highlighted != nullptr ) {
        select( highlighted );
    }
    prepare_layout();
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->invalidate_ui();
    }
}

void inventory_selector::clear_items()
{
    is_empty = true;
    for( auto &column : columns ) {
        column->clear();
    }
    own_inv_column.clear();
    own_gear_column.clear();
    map_column.clear();
}

bool inventory_selector::select( const item *loc )
{
    bool res = false;

    for( size_t i = 0; i < columns.size(); ++i ) {
        auto elem = columns[i];
        if( elem->visible() && elem->select( loc ) ) {
            if( !res && elem->activatable() ) {
                set_active_column( i );
                res = true;
            }
        }
    }

    return res;
}

auto inventory_selector::select_item_type( const itype_id &type ) -> bool
{
    return select_item_type( type, 0 );
}

auto inventory_selector::select_item_type( const itype_id &type,
        const size_t preferred_column ) -> bool
{
    namespace ranges = std::ranges;

    prepare_layout();
    auto search_order = std::views::iota( size_t{}, columns.size() ) | ranges::to<std::vector>();
    if( preferred_column < columns.size() ) {
        std::erase( search_order, preferred_column );
        search_order.insert( search_order.begin(), preferred_column );
    }

    for( const auto index : search_order ) {
        auto *column = columns[index];
        if( !column->visible() || !column->activatable() ) {
            continue;
        }
        const auto entries = column->get_entries( []( const auto & entry ) { return entry.is_selectable(); } );
        const auto iter = ranges::find_if( entries, [&type]( const auto * entry ) {
            return entry->any_item()->typeId() == type;
        } );
        if( iter != entries.end() && column->select( ( *iter )->any_item() ) ) {
            set_active_column( index );
            return true;
        }
    }
    return false;
}

auto inventory_selector::select_position_if_item_type( const std::pair<size_t, size_t> position,
        const itype_id &type ) -> bool
{
    prepare_layout();
    if( position.first >= columns.size() ) {
        return false;
    }

    auto *column = columns[position.first];
    if( !column->visible() || !column->activatable() ||
        !column->select_position_if_item_type( position.second, type ) ) {
        return false;
    }

    set_active_column( position.first );
    return true;
}

auto inventory_selector::restore_selection( const std::pair<size_t, size_t> position,
        const itype_id &type ) -> bool
{
    const auto restored = select_position_if_item_type( position, type ) ||
                          select_item_type( type, position.first );
    if( !restored ) {
        prepare_layout();
        for( const auto index : std::views::iota( size_t{}, columns.size() ) ) {
            auto *column = columns[index];
            if( !column->visible() || !column->activatable() ) {
                continue;
            }
            column->select( 0, scroll_direction::FORWARD );
            if( column->get_selected() ) {
                set_active_column( index );
                break;
            }
        }
    }
    return restored;
}

inventory_entry *inventory_selector::find_entry_by_invlet( int invlet ) const
{
    for( const auto elem : columns ) {
        const auto res = elem->find_by_invlet( invlet );
        if( res != nullptr ) {
            return res;
        }
    }
    return nullptr;
}

// FIXME: if columns are merged due to low screen width, they will not be splitted
// once screen width becomes enough for the columns.
void inventory_selector::rearrange_columns( size_t client_width )
{
    // CDF: single-column mode, DDA cpp:2521; off by default
    if( force_single_column ) {
        if( !own_gear_column.empty() ) {
            own_gear_column.move_entries_to( own_inv_column );
        }
        if( !map_column.empty() ) {
            map_column.move_entries_to( own_inv_column );
        }
    }
    while( is_overflown( client_width ) ) {
        if( !own_gear_column.empty() ) {
            own_gear_column.move_entries_to( own_inv_column );
        } else if( !map_column.empty() ) {
            map_column.move_entries_to( own_inv_column );
        } else {
            break;  // There's nothing we can do about it.
        }
    }
}

void inventory_selector::prepare_layout( size_t client_width, size_t client_height )
{
    // This block adds categories and should go before any width evaluations
    for( auto &elem : columns ) {
        elem->set_height( client_height );
        elem->reset_width( columns );
        elem->prepare_paging( filter );
    }
    // Handle screen overflow
    rearrange_columns( client_width );
    // If we have a single column and it occupies more than a half of
    // the available with -> expand it
    auto visible_columns = get_visible_columns();
    if( visible_columns.size() == 1 && are_columns_centered( client_width ) ) {
        visible_columns.front()->set_width( client_width, columns );
    }

    int custom_invlet = '0';
    for( auto &elem : columns ) {
        elem->prepare_paging();
        custom_invlet = elem->reassign_custom_invlets( u, custom_invlet, '9' );
    }
    assign_invlet_hints();

    refresh_active_column();
}

void inventory_selector::prepare_layout()
{
    const auto snap = []( size_t cur_dim, size_t max_dim ) {
        return cur_dim + 2 * max_win_snap_distance >= max_dim ? max_dim : cur_dim;
    };

    const int nc_width = 2 * ( 1 + border );
    const int nc_height = get_header_height() + 1 + 2 * border;

    prepare_layout( TERMX - nc_width, TERMY - nc_height );

    const int win_width  = snap( get_layout_width() + nc_width, TERMX );
    const int win_height = snap( std::max<int>( get_layout_height() + nc_height, FULL_SCREEN_HEIGHT ),
                                 TERMY );

    prepare_layout( win_width - nc_width, win_height - nc_height );

    resize_window( win_width, win_height );
}

shared_ptr_fast<ui_adaptor> inventory_selector::create_or_get_ui_adaptor()
{
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( !current_ui ) {
        ui = current_ui = make_shared_fast<ui_adaptor>();
        current_ui->on_screen_resize( [this]( ui_adaptor & ) {
            prepare_layout();
        } );
        current_ui->mark_resize();

        current_ui->on_redraw( [this]( const ui_adaptor & ) {
            refresh_window();
        } );
    }
    return current_ui;
}

size_t inventory_selector::get_layout_width() const
{
    const size_t min_hud_width = std::max( get_header_min_width(), get_footer_min_width() );
    const auto visible_columns = get_visible_columns();
    const size_t gaps = visible_columns.size() > 1 ? normal_column_gap * ( visible_columns.size() - 1 )
                        : 0;

    return std::max( get_columns_width( visible_columns ) + gaps, min_hud_width );
}

size_t inventory_selector::get_layout_height() const
{
    const auto visible_columns = get_visible_columns();
    // Find and return the highest column's height.
    const auto iter = std::max_element( visible_columns.begin(), visible_columns.end(),
    []( const inventory_column * lhs, const inventory_column * rhs ) {
        return lhs->get_height() < rhs->get_height();
    } );

    return iter != visible_columns.end() ? ( *iter )->get_height() : 1;
}

size_t inventory_selector::get_header_height() const
{
    // per DDA inventory_ui.cpp:2663
    return std::max<size_t>( { 1,
                               display_stats ? get_stats().size() : 0,
                               !hint.empty() ? 2 + static_cast<size_t>( std::count( hint.begin(), hint.end(), '\n' ) ) : 0 }
                           );
}

size_t inventory_selector::get_header_min_width() const
{
    const size_t titles_width = std::max( utf8_width( title, true ),
                                          utf8_width( hint, true ) );
    if( !display_stats ) {
        return titles_width;
    }

    size_t stats_width = 0;
    for( const std::string &elem : get_stats() ) {
        stats_width = std::max( static_cast<size_t>( utf8_width( elem, true ) ), stats_width );
    }

    return titles_width + stats_width + ( stats_width != 0 ? 3 : 0 );
}

size_t inventory_selector::get_footer_min_width() const
{
    size_t result = 0;
    navigation_mode m = mode;

    do {
        result = std::max( static_cast<size_t>( utf8_width( get_footer( m ).first, true ) ) + 2 + 4,
                           result );
        m = get_navigation_data( m ).next_mode;
    } while( m != mode );

    return result;
}

void inventory_selector::draw_header( const catacurses::window &w ) const
{
    trim_and_print( w, point( border + 1, border ), getmaxx( w ) - 2 * ( border + 1 ), c_white, title );
    fold_and_print( w, point( border + 1, border + 1 ), getmaxx( w ) - 2 * ( border + 1 ), c_dark_gray,
                    hint );

    mvwhline( w, point( border, border + get_header_height() ), LINE_OXOX, getmaxx( w ) - 2 * border );

    if( display_stats ) {
        size_t y = border;
        for( const std::string &elem : get_stats() ) {
            right_print( w, y++, border + 1, c_dark_gray, elem );
        }
    }
}

inventory_selector::stat display_stat( const std::string &caption, int cur_value, int max_value,
                                       const std::function<std::string( int )> &disp_func )
{
    const nc_color color = cur_value > max_value ? c_red : c_light_gray;
    return {{
            caption,
            colorize( disp_func( cur_value ), color ), "/",
            colorize( disp_func( max_value ), c_light_gray )
        }};
}

inventory_selector::stats inventory_selector::get_weight_and_volume_stats(
    units::mass weight_carried, units::mass weight_capacity,
    const units::volume &volume_carried, const units::volume &volume_capacity )
{
    return {
        {
            display_stat( string_format( _( "Weight (%s):" ), weight_units() ),
                          to_gram( weight_carried ),
                          to_gram( weight_capacity ), []( int w )
            {
                return string_format( "%.1f", round_up( convert_weight( units::from_gram( w ) ), 1 ) );
            } ),
            display_stat( string_format( _( "Volume (%s):" ), volume_units_abbr() ),
                          units::to_milliliter( volume_carried ),
                          units::to_milliliter( volume_capacity ), []( int v )
            {
                return format_volume( units::from_milliliter( v ) );
            } )
        }
    };
}

bool inventory_selector::pockets_match( const pocket_with_constraint &a,
                                        const pocket_with_constraint &b )
{
    return a.pocket == b.pocket ||
           ( a.pocket != nullptr
             && a.constraint == b.constraint
             && a.pocket->min_containable_length() == b.pocket->min_containable_length()
             && a.pocket->is_restricted() == b.pocket->is_restricted() );
}

inventory_selector::header_stats inventory_selector::get_pocket_summary_header_stats(
    const units::mass &weight_carried, const units::mass &weight_capacity,
    const units::volume &volume_in_pockets, const units::volume &volume_of_pockets,
    std::vector<pocket_with_constraint> pockets,
    bool show_unconstrained_max_space,
    const std::string *weight_label_override, const std::string *volume_label_override
)
{
    pocket_with_constraint large_free_space;
    int large_free_space_copies = 0;
    pocket_with_constraint large_max_space;
    pocket_with_constraint long_free_space;
    int long_free_space_copies = 0;
    pocket_with_constraint long_max_space;
    std::vector<pocket_with_constraint> pockets_with_space;

    bool show_max_space_opt = get_option<bool>( "SHOW_MAX_SPACE" );
    int show_other_spaces_opt = get_option<int>( "SHOW_OTHER_SPACES" );

    pockets.erase(
        std::remove_if( pockets.begin(), pockets.end(),
    []( pocket_with_constraint & p ) {
        return p.constraint.is_dominated;
    }
                      ),
    pockets.end()
    );

    std::vector<std::string> other_space_strings;

    if( !pockets.empty() ) {
        std::copy_if( pockets.begin(), pockets.end(), std::back_inserter( pockets_with_space ),
        []( const pocket_with_constraint & p ) {
            return p.constraint.remaining_volume > 0_ml && !p.pocket->holster_full();
        }
                    );

        if( !pockets_with_space.empty() ) {
            // determine largest space
            std::sort( pockets_with_space.begin(),
            pockets_with_space.end(), []( const pocket_with_constraint & a, const pocket_with_constraint & b ) {
                return std::tuple( a.pocket->is_restricted(), b.constraint.remaining_volume,
                                   b.constraint.max_containable_length )
                       < std::tuple( b.pocket->is_restricted(), a.constraint.remaining_volume,
                                     a.constraint.max_containable_length );
            } );
            large_free_space = pockets_with_space[0];

            // determine longest space; count large and long space copies
            for( const auto &pwc : pockets_with_space ) {
                if( pockets_match( pwc, large_free_space ) ) {
                    large_free_space_copies++;
                }
                if( long_free_space.pocket == nullptr
                    || std::tuple( pwc.pocket->is_restricted(), long_free_space.constraint.max_containable_length,
                                   long_free_space.constraint.remaining_volume )
                    < std::tuple( long_free_space.pocket->is_restricted(), pwc.constraint.max_containable_length,
                                  pwc.constraint.remaining_volume ) ) {
                    long_free_space = pwc;
                    long_free_space_copies = 1;
                } else if( pockets_match( pwc, long_free_space ) ) {
                    long_free_space_copies++;
                }
            }

            if( show_other_spaces_opt > 0 ) {
                // count other spaces
                std::vector<pocket_with_constraint> other_spaces;
                std::vector<int> other_spaces_copies;
                bool other_spaces_display_truncated = false;
                for( const auto &pwc : pockets_with_space ) {
                    if( !pockets_match( pwc, large_free_space ) && !pockets_match( pwc, long_free_space ) ) {
                        if( other_spaces.empty() || !pockets_match( pwc, other_spaces.back() ) ) {
                            if( other_spaces.size() < static_cast<size_t>( show_other_spaces_opt ) ) {
                                other_spaces.push_back( pwc );
                                other_spaces_copies.push_back( 1 );
                            } else {
                                other_spaces_display_truncated = true;
                                break;
                            }
                        } else {
                            other_spaces_copies.back()++;
                        }
                    }
                }

                for( size_t i = 0; i < other_spaces.size(); i++ ) {
                    const space_stats_text other_t = build_space_stats( other_spaces[i].constraint.remaining_volume,
                                                     other_spaces[i].pocket->min_containable_length(),
                                                     other_spaces[i].constraint.max_containable_length,
                                                     other_spaces[i].pocket->is_restricted()
                                                                      );
                    other_space_strings.push_back( string_format( "%s %s%s", other_t.volume, other_t.length,
                                                   ( other_spaces_copies[i] > 1 ? string_format( " (%d)", other_spaces_copies[i] ) : "" ) )
                                                 );
                }
                if( other_spaces_display_truncated ) {
                    other_space_strings.emplace_back( "…" );
                }
            }
        }


        if( show_max_space_opt ) {
            // largest / longest pockets [not spaces] we have.
            // 'ignore_restrict' check makes it so we don't report a smaller 'max' space than 'free' space if we selected a restricted pocket for that.
            if( show_unconstrained_max_space ) {
                std::sort( pockets.begin(), pockets.end(), [ignore_restrict = large_free_space.pocket &&
                                           large_free_space.pocket->is_restricted()]( const pocket_with_constraint & a,
                const pocket_with_constraint & b ) {
                    return std::tuple( ignore_restrict || a.pocket->is_restricted(), b.pocket->volume_capacity() )
                           < std::tuple( ignore_restrict || b.pocket->is_restricted(), a.pocket->volume_capacity() );
                } );
                large_max_space = pockets[0];

                std::sort( pockets.begin(), pockets.end(), [ignore_restrict = long_free_space.pocket &&
                                           long_free_space.pocket->is_restricted()]( const pocket_with_constraint & a,
                const pocket_with_constraint & b ) {
                    return std::tuple( ignore_restrict ||
                                       a.pocket->is_restricted(), b.pocket->max_containable_length(), b.pocket->volume_capacity() )
                           < std::tuple( ignore_restrict ||
                                         b.pocket->is_restricted(), a.pocket->max_containable_length(), a.pocket->volume_capacity() );
                } );
                long_max_space = pockets[0];
            } else {
                std::sort( pockets.begin(), pockets.end(), [ignore_restrict = large_free_space.pocket &&
                                           large_free_space.pocket->is_restricted()]( const pocket_with_constraint & a,
                const pocket_with_constraint & b ) {
                    return std::tuple( ignore_restrict || a.pocket->is_restricted(), b.constraint.volume_capacity )
                           < std::tuple( ignore_restrict || b.pocket->is_restricted(), a.constraint.volume_capacity );
                } );
                large_max_space = pockets[0];

                std::sort( pockets.begin(), pockets.end(), [ignore_restrict = long_free_space.pocket &&
                                           long_free_space.pocket->is_restricted()]( const pocket_with_constraint & a,
                const pocket_with_constraint & b ) {
                    return std::tuple( ignore_restrict ||
                                       a.pocket->is_restricted(), b.constraint.max_containable_length, b.constraint.volume_capacity )
                           < std::tuple( ignore_restrict ||
                                         b.pocket->is_restricted(), a.constraint.max_containable_length, a.constraint.volume_capacity );
                } );
                long_max_space = pockets[0];
            }
        }
    }

    inventory_selector::header_stats ret = {
        build_weight_and_volume_stats_line( weight_carried,
                                            weight_capacity,
                                            weight_label_override ? *weight_label_override : _( "Total Weight:" ),
                                            volume_in_pockets,
                                            volume_of_pockets,
                                            volume_label_override ? *volume_label_override : _( "Total Volume:" )
                                          )
    };
    bool show_large_max = show_max_space_opt &&
                          large_free_space.constraint.remaining_volume != large_max_space.constraint.volume_capacity;
    bool show_long_max = show_max_space_opt &&
                         long_free_space.constraint.remaining_volume != long_max_space.constraint.volume_capacity;
    if( !show_large_max ) {
        ret.push_back( build_pocket_stats_line( _( "Largest Space" ),
                                                large_free_space.pocket,
                                                large_free_space.constraint.remaining_volume,
                                                large_free_space.constraint.max_containable_length,
                                                ( show_other_spaces_opt > 0 ) ? large_free_space_copies : 1
                                              ) );
    } else {
        ret.push_back( build_pocket_stats_line( _( "Largest Space" ),
                                                large_free_space.pocket,
                                                large_free_space.constraint.remaining_volume,
                                                large_free_space.constraint.max_containable_length,
                                                ( show_other_spaces_opt > 0 ) ? large_free_space_copies : 1,
                                                large_max_space.pocket,
                                                show_unconstrained_max_space ? large_max_space.pocket->volume_capacity() :
                                                large_max_space.constraint.volume_capacity,
                                                show_unconstrained_max_space ? large_max_space.pocket->max_containable_length() :
                                                large_max_space.constraint.max_containable_length
                                              ) );
    }
    if( large_free_space.pocket != long_free_space.pocket || ( show_long_max != show_large_max ) ) {
        if( !show_long_max ) {
            ret.push_back( build_pocket_stats_line( _( "Longest Space" ),
                                                    long_free_space.pocket,
                                                    long_free_space.constraint.remaining_volume,
                                                    long_free_space.constraint.max_containable_length,
                                                    ( show_other_spaces_opt > 0 ) ? long_free_space_copies : 1
                                                  )
                         );
        } else {
            ret.push_back( build_pocket_stats_line( _( "Longest Space" ),
                                                    long_free_space.pocket,
                                                    long_free_space.constraint.remaining_volume,
                                                    long_free_space.constraint.max_containable_length,
                                                    ( show_other_spaces_opt > 0 ) ? long_free_space_copies : 1,
                                                    long_max_space.pocket,
                                                    show_unconstrained_max_space ? long_max_space.pocket->volume_capacity() :
                                                    long_max_space.constraint.volume_capacity,
                                                    show_unconstrained_max_space ? long_max_space.pocket->max_containable_length() :
                                                    long_max_space.constraint.max_containable_length
                                                  )
                         );
        }
    }
    if( ( show_other_spaces_opt > 0 ) && !other_space_strings.empty() ) {
        ret.push_back( { _( "Other: " ) + join( other_space_strings, ", " ) } );
    }
    return ret;
}

inventory_selector::header_stats_line inventory_selector::build_weight_and_volume_stats_line(
    units::mass weight_carried, units::mass weight_capacity, const std::string &weight_label,
    const units::volume &volume_in_pockets, const units::volume &volume_of_pockets,
    const std::string &volume_label
)
{
    const nc_color &color_numeral = c_light_gray;
    const nc_color &color_unit = c_light_gray;
    const nc_color &color_label = c_dark_gray;
    const nc_color &color_weight = weight_carried > weight_capacity ? c_red : color_numeral;
    return { string_format( "%s %s/%s %s %s %s/%s %s",
                            colorize( volume_label, color_label ),
                            colorize( format_volume( volume_in_pockets ), color_numeral ),
                            colorize( format_volume( volume_of_pockets ), color_numeral ),
                            colorize( volume_units_abbr(), color_unit ),
                            colorize( weight_label, color_label ),
                            colorize( string_format( "%.1f", round_up( convert_weight( weight_carried ), 1 ) ), color_weight ),
                            colorize( string_format( "%.1f", round_up( convert_weight( weight_capacity ), 1 ) ), color_numeral ),
                            colorize( weight_units(), color_unit )
                          ) };
}

inventory_selector::header_stats_line inventory_selector::build_selection_stats_line(
    units::volume volume, units::mass weight
)
{
    const nc_color &color_numeral = ( volume > 0_ml || weight > 0_gram ) ? c_yellow : c_dark_gray;
    return { string_format( "%s %s %s %s %s",
                            colorize( _( "Selected" ), c_dark_gray ),
                            colorize( format_volume( volume ), color_numeral ),
                            volume_units_abbr(),
                            colorize( string_format( "%.1f", round_up( convert_weight( weight ), 1 ) ), color_numeral ),
                            weight_units()
                          ) };
}

inventory_selector::space_stats_text inventory_selector::build_invalid_space_stats()
{
    return { "", _( "None" ) };
}

inventory_selector::space_stats_text inventory_selector::build_space_stats( units::volume size,
        units::length min_length, units::length max_length, bool is_restricted )
{
    const nc_color &color_unit = c_light_gray;
    const nc_color &color_numeral = c_light_gray;
    std::string vol_str = string_format( "%s %s%s",
                                         colorize( format_volume( size ), color_numeral ),
                                         colorize( volume_units_abbr(), color_unit ),
                                         is_restricted ? "*" : "" // possibly try to give more info on the restriction(s)?
                                       );
    if( min_length > max_length ) { //you need a weird object for this, but it's not illegal
        max_length = 0_mm;
        min_length = 0_mm;
    }
    std::string length_str = colorize( std::to_string( convert_length( max_length ) ),
                                       color_numeral ) + " " + colorize( length_units( max_length ), color_unit );
    if( min_length > 0_mm ) {
        length_str = colorize( std::to_string( convert_length( min_length ) ),
                               color_numeral ) + " " + colorize( length_units( min_length ), color_unit ) + "-" + length_str;
    }
    return { vol_str, length_str };
}

inventory_selector::header_stats_line inventory_selector::build_pocket_stats_line(
    const std::string &prefix,
    const item_pocket *free_pocket,
    units::volume free_pocket_volume,
    units::length free_pocket_length,
    int free_pocket_copies
)
{
    const nc_color &color_label = c_dark_gray;
    const space_stats_text free_pocket_t = free_pocket ? build_space_stats( free_pocket_volume,
                                           free_pocket->min_containable_length(), free_pocket_length,
                                           free_pocket->is_restricted() ) : build_invalid_space_stats();
    return {
        prefix.empty() ? "" : ( prefix + " " ) + colorize( _( "Free: " ), color_label ),
        header_stats_tab_stop,
        free_pocket_t.volume + " ",
        header_stats_tab_stop,
        free_pocket_t.length,
        header_stats_tab_stop,
        ( free_pocket_copies > 1 ? string_format( " (%d)", free_pocket_copies ) : "" ),
        header_stats_tab_stop, // extra stops to align with the max-space printing version
        header_stats_tab_stop,
        header_stats_tab_stop
    };
}

inventory_selector::header_stats_line inventory_selector::build_pocket_stats_line(
    const std::string &prefix,
    const item_pocket *free_pocket,
    units::volume free_pocket_volume,
    units::length free_pocket_length,
    int free_pocket_copies,
    const item_pocket *max_pocket,
    units::volume max_pocket_volume,
    units::length max_pocket_length
)
{
    const nc_color &color_label = c_dark_gray;
    const space_stats_text free_pocket_t = free_pocket ? build_space_stats( free_pocket_volume,
                                           free_pocket->min_containable_length(), free_pocket_length,
                                           free_pocket->is_restricted() ) : build_invalid_space_stats();
    const space_stats_text max_pocket_t = max_pocket ? build_space_stats( max_pocket_volume,
                                          max_pocket->min_containable_length(), max_pocket_length,
                                          max_pocket->is_restricted() ) : build_invalid_space_stats();
    return {
        prefix.empty() ? "" : ( prefix + " " ) + colorize( _( "Free: " ), color_label ),
        header_stats_tab_stop,
        free_pocket_t.volume + " ",
        header_stats_tab_stop,
        free_pocket_t.length,
        header_stats_tab_stop,
        ( free_pocket_copies > 1 ? string_format( " (%d)", free_pocket_copies ) : "" ),
        header_stats_tab_stop,
        " | ",
        colorize( _( "Max: " ), color_label ),
        header_stats_tab_stop,
        max_pocket_t.volume + " ",
        header_stats_tab_stop,
        max_pocket_t.length
    };
}

inventory_selector::header_stats inventory_selector::get_raw_stats() const
{
    // pockets it's okay to insert items into
    std::function<bool( const item_pocket & )> check_pocket_tree = []( const item_pocket & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER )
               && !pocket.is_forbidden()
               && !pocket.settings.is_disabled();
    };

    // general-purpose bulk pocket criteria
    std::function<bool( const item_pocket & )> include_pocket_capacity = [](
    const item_pocket & pocket ) {
        return !pocket.is_restricted()
               && !pocket.is_holster()
               && !pocket.is_ablative()
               && pocket.min_containable_length() == 0_mm
               && pocket.get_pocket_data()->min_item_volume == 0_ml
               && !pocket.get_pocket_data()->max_item_volume
               && pocket.settings.get_category_whitelist().empty()
               && pocket.settings.get_item_whitelist().empty();
    };

    // space check has extra criteria to make pockets report as 'full' when they can't be used for storing most items.
    std::function<bool( const item_pocket & )> include_pocket_space = [&](
    const item_pocket & pocket ) {
        return include_pocket_capacity( pocket )
               && item_pocket::ok_for_solids( pocket )
               && !pocket.sealed();
    };
    units::volume free_space = u.free_space( include_pocket_space, check_pocket_tree );
    units::volume capacity = u.volume_capacity_recursive( include_pocket_capacity, check_pocket_tree );

    // pockets we want to list as 'spaces'
    std::vector<pocket_with_constraint> pockets = u.get_all_pockets_with_constraints(
    []( const item_pocket & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER )
               && !pocket.sealed()
               && pocket.settings.get_category_whitelist().empty()
               && pocket.settings.get_item_whitelist().empty()
               && item_pocket::ok_for_solids( pocket );
    },
    check_pocket_tree
            );

    std::string volume_label_override = _( "Bulk Volume:" );
    return get_pocket_summary_header_stats(
               u.weight_carried(), u.weight_capacity(),
               capacity - free_space, capacity,
               pockets, false,
               nullptr, &volume_label_override
           );
}

std::vector<std::string> inventory_selector::get_stats() const
{
    header_stats stats = get_raw_stats();
    std::vector<std::string> lines( stats.size() );
    if( lines.empty() ) {
        return lines;
    }
    std::vector<size_t> line_rindex( stats.size() );
    size_t blockEnd = 0;
    for( size_t blockStart = 0; blockStart < stats.size(); blockStart = blockEnd ) {
        ptrdiff_t num_stops = std::count_if( stats[blockStart].begin(),
        stats[blockStart].end(), []( const auto & x ) {
            return x == header_stats_tab_stop;
        } );
        blockEnd = stats.size();
        for( size_t i = blockStart + 1; i < stats.size(); i++ ) {
            if( std::count_if( stats[i].begin(), stats[i].end(), []( const auto & x ) {
            return x == header_stats_tab_stop;
        } ) != num_stops ) {
                blockEnd = i;
                break;
            }
        }

        for( int tab_stop = 0; tab_stop < num_stops; tab_stop++ ) {
            // each line in the block writes out its parts until its next tab stop
            for( size_t i = blockStart; i < blockEnd; i++ ) {
                for( ; line_rindex[i] < stats[i].size(); line_rindex[i]++ ) {
                    int line_index = stats[i].size() - 1 - line_rindex[i];
                    if( stats[i][line_index] != header_stats_tab_stop ) {
                        lines[i] = stats[i][line_index] + lines[i];
                    } else {
                        line_rindex[i]++; // eat the tab-stop
                        break;
                    }
                }
            }
            // Determine current max width of the block
            std::vector<size_t> widths( blockEnd - blockStart );
            std::transform( lines.begin() + blockStart, lines.begin() + blockEnd, widths.begin(),
            []( const std::string & part ) {
                return utf8_width( part, true );
            } );
            size_t max_width = *std::max_element( widths.begin(), widths.end() );
            // leftpad all lines in block to the same length
            for( size_t i = blockStart; i < blockEnd; i++ ) {
                if( max_width > widths[i - blockStart] ) {
                    lines[i] = std::string( max_width - widths[i - blockStart], ' ' ) + lines[i];
                }
            }
        }
    }

    // add remaining elements
    for( size_t i = 0; i < lines.size(); i++ ) {
        for( int line_index = stats[i].size() - 1 - line_rindex[i]; line_index >= 0; line_index-- ) {
            lines[i] = stats[i][line_index] + lines[i];
        }
    }
    // Determine max width of the whole header
    std::vector<size_t> widths( lines.size() );
    std::transform( lines.begin(), lines.end(), widths.begin(),
    []( const std::string & part ) {
        return utf8_width( part, true );
    } );
    size_t max_width = *std::max_element( widths.begin(), widths.end() );
    // leftpad all lines to the same length, right-justifying the block
    for( size_t i = 0; i < lines.size(); i++ ) {
        if( max_width > widths[i] ) {
            lines[i] = std::string( max_width - widths[i], ' ' ) + lines[i];
        }
    }

    return lines;
}

inventory_selector::header_stats inventory_selector::convert_stats_to_header_stats(
    const stats &old_stats )
{
    header_stats new_stats;
    // Stats consist of arrays of cells.
    // Add first cells and spaces after them.
    for( const inventory_selector::stat &old_stat : old_stats ) {
        header_stats_line line;
        line.push_back( old_stat[0] + " " ); //First cell gets a space inserted after it
        for( size_t i = 1; i < old_stat.size(); i++ ) {
            line.emplace_back( header_stats_tab_stop ); //The rest get put in columns
            line.push_back( old_stat[i] );
        }
        new_stats.push_back( line );
    }
    return new_stats;
}


void inventory_selector::resize_window( int width, int height )
{
    w_inv = catacurses::newwin( height, width,
                                point( ( TERMX - width ) / 2, ( TERMY - height ) / 2 ) );
    if( spopup ) {
        spopup->window( w_inv, point( 4, getmaxy( w_inv ) - 1 ), ( getmaxx( w_inv ) / 2 ) - 4 );
    }
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->position_from_window( w_inv );
    }
}

void inventory_selector::refresh_window() const
{
    assert( w_inv );

    werase( w_inv );

    draw_frame( w_inv );
    draw_header( w_inv );
    draw_columns( w_inv );
    draw_footer( w_inv );

    wnoutrefresh( w_inv );
}

void inventory_selector::set_filter()
{
    spopup = std::make_unique<string_input_popup>();
    spopup->max_length( 256 ).identifier( "inventory" )
    .text( filter );

    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->mark_resize();
    }

    ime_sentry sentry;

    do {
        ui_manager::redraw();
        spopup->query_string( /*loop=*/false );
    } while( !spopup->confirmed() && !spopup->canceled() );

    if( spopup->confirmed() ) {
        filter = spopup->text();
        for( const auto elem : columns ) {
            elem->set_filter( filter );
        }
        if( current_ui ) {
            current_ui->mark_resize();
        }
    }

    spopup.reset();
}

void inventory_selector::set_filter( const std::string &str )
{
    prepare_layout();
    filter = str;
    for( const auto elem : columns ) {
        elem->set_filter( filter );
    }
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->mark_resize();
    }
}

std::string inventory_selector::get_filter() const
{
    return filter;
}

bool inventory_selector::wield( inventory_entry &entry )
{
    if( !entry.is_item() ) {
        return false;
    }

    item *item = entry.any_item();
    bool wield_result = u.can_wield( *item ).success();
    if( wield_result ) {
        remove_item( item );
        u.wield( *item );
    } else {
        popup_getkey( u.can_wield( *item ).c_str() );
    }

    return wield_result;
}

bool inventory_selector::wear( inventory_entry &entry )
{
    if( !entry.is_item() ) {
        return false;
    }

    item *item = entry.any_item();
    bool wear_result = u.can_wear( *item ).success();
    if( wear_result ) {
        remove_item( item );
        auto to_move = item->detach();
        u.wear_item( std::move( to_move ) );
    } else {
        popup_getkey( u.can_wear( *item ).c_str() );
    }

    return wear_result;
}

void inventory_selector::draw_columns( const catacurses::window &w ) const
{
    const auto columns = get_visible_columns();

    const int screen_width = getmaxx( w ) - 2 * ( border + 1 );
    const bool centered = are_columns_centered( screen_width );

    const int free_space = screen_width - get_columns_width( columns );
    const int max_gap = ( columns.size() > 1 ) ? free_space / ( static_cast<int>
                        ( columns.size() ) - 1 ) :
                        free_space;
    const int gap = centered ? max_gap : std::min<int>( max_gap, normal_column_gap );
    const int gap_rounding_error = centered && columns.size() > 1
                                   ? free_space % ( columns.size() - 1 ) : 0;

    size_t x = border + 1;
    size_t y = get_header_height() + border + 1;
    size_t active_x = 0;

    for( const auto &elem : columns ) {
        if( &elem == &columns.back() ) {
            x += gap_rounding_error;
        }

        if( !is_active_column( *elem ) ) {
            elem->draw( w, point( x, y ) );
        } else {
            active_x = x;
        }

        x += elem->get_width() + gap;
    }

    get_active_column().draw( w, point( active_x, y ) );
    if( empty() ) {
        center_print( w, getmaxy( w ) / 2, c_dark_gray, _( "Your inventory is empty." ) );
    }
}

void inventory_selector::draw_frame( const catacurses::window &w ) const
{
    draw_border( w );

    const int y = border + get_header_height();
    mvwhline( w, point( 0, y ), LINE_XXXO, 1 );
    mvwhline( w, point( getmaxx( w ) - border, y ), LINE_XOXX, 1 );
}

std::pair<std::string, nc_color> inventory_selector::get_footer( navigation_mode m ) const
{
    if( has_available_choices() ) {
        return std::make_pair( get_navigation_data( m ).name.translated(),
                               get_navigation_data( m ).color );
    }
    return std::make_pair( _( "There are no available choices" ), i_red );
}

void inventory_selector::draw_footer( const catacurses::window &w ) const
{
    if( spopup ) {
        mvwprintz( w_inv, point( 2, getmaxy( w_inv ) - 1 ), c_cyan, "< " );
        mvwprintz( w_inv, point( ( getmaxx( w_inv ) / 2 ) - 4, getmaxy( w_inv ) - 1 ), c_cyan, " >" );

        spopup->query_string( /*loop=*/false, /*draw_only=*/true );
    } else {
        int filter_offset = 0;
        if( has_available_choices() || !filter.empty() ) {
            std::string text = string_format( filter.empty() ? _( "[%s] Filter" ) : _( "[%s] Filter: " ),
                                              ctxt.get_desc( "INVENTORY_FILTER" ) );
            filter_offset = utf8_width( text + filter ) + 6;

            mvwprintz( w, point( 2, getmaxy( w ) - border ), c_light_gray, "< " );
            wprintz( w, c_light_gray, text );
            wprintz( w, c_white, filter );
            wprintz( w, c_light_gray, " >" );
        }

        if( view_toggle_enabled ) {
            // Per DDA inventory_ui.cpp:3416
            right_print( w, getmaxy( w ) - border, border + 1, c_light_gray,
                         string_format( "< [%s] %s >", ctxt.get_desc( "VIEW_CATEGORY_MODE" ),
                                        _uimode == uimode::hierarchy ? _( "hierarchy" ) : _( "categories" ) ) );
        }
        const auto footer = get_footer( mode );
        if( !footer.first.empty() ) {
            const int string_width = utf8_width( footer.first );
            const int x1 = filter_offset + std::max( getmaxx( w ) - string_width - filter_offset, 0 ) / 2;
            const int x2 = x1 + string_width - 1;
            const int y = getmaxy( w ) - border;

            mvwprintz( w, point( x1, y ), footer.second, footer.first );
            mvwputch( w, point( x1 - 1, y ), c_light_gray, ' ' );
            mvwputch( w, point( x2 + 1, y ), c_light_gray, ' ' );
            mvwputch( w, point( x1 - 2, y ), c_light_gray, LINE_XOXX );
            mvwputch( w, point( x2 + 2, y ), c_light_gray, LINE_XXXO );
        }
    }
}

inventory_selector::inventory_selector( player &u, const inventory_selector_preset &preset )
    : u( u )
    , preset( preset )
    , ctxt( "INVENTORY" )
    , own_gear_column( preset )
    , active_column_index( 0 )
    , mode( navigation_mode::ITEM )
    , own_inv_column( preset )
    , map_column( preset )
{
    ctxt.register_action( "DOWN", to_translation( "Next item" ) );
    ctxt.register_action( "UP", to_translation( "Previous item" ) );
    ctxt.register_action( "PAGE_DOWN", to_translation( "Page down" ) );
    ctxt.register_action( "PAGE_UP", to_translation( "Page up" ) );
    ctxt.register_action( "RIGHT", to_translation( "Next column" ) );
    ctxt.register_action( "LEFT", to_translation( "Previous column" ) );
    ctxt.register_action( "CONFIRM", to_translation( "Confirm your selection" ) );
    ctxt.register_action( "QUIT", to_translation( "Cancel" ) );
    ctxt.register_action( "CATEGORY_SELECTION", to_translation( "Switch category selection mode" ) );
    ctxt.register_action( "TOGGLE_FAVORITE", to_translation( "Toggle favorite" ) );
    ctxt.register_action( "HOME", to_translation( "Home" ) );
    ctxt.register_action( "END", to_translation( "End" ) );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "INVENTORY_FILTER" );
    ctxt.register_action( "EXAMINE" );
    ctxt.register_action( "WIELD" );
    ctxt.register_action( "WEAR" );
    ctxt.register_action( "ANY_INPUT" ); // For invlets
    // Per DDA inventory_ui.cpp:3481; registered here so auto-assigned invlets skip its key.
    ctxt.register_action( "ORGANIZE_MENU" );
    ctxt.register_action( "SHOW_HIDE_CONTENTS", to_translation( "Show/hide contents" ) );


    append_column( own_inv_column );
    append_column( map_column );
    append_column( own_gear_column );
}

inventory_selector::~inventory_selector() = default;

bool inventory_selector::empty() const
{
    return is_empty;
}

bool inventory_selector::has_available_choices() const
{
    return std::any_of( columns.begin(), columns.end(), []( const inventory_column * element ) {
        return element->has_available_choices();
    } );
}

drop_location inventory_selector::get_only_choice() const
{
    // CDF: DDA (inventory_ui.cpp:3532) counts item_entry_count() and filters with
    // return_item. CDF has neither, so reuse the test of
    // inventory_column::has_available_choices() (inventory_ui.cpp:634).
    const auto is_available = [this]( const inventory_entry & entry ) {
        return entry.is_item() && preset.get_denial( entry ).empty();
    };
    std::vector<inventory_entry *> choices;
    for( const inventory_column *const col : columns ) {
        if( !col->allows_selecting() || !col->activatable() ) {
            continue;
        }
        const std::vector<inventory_entry *> entries = col->get_entries( is_available );
        std::ranges::copy( entries, std::back_inserter( choices ) );
    }

    if( choices.size() != 1 ) {
        debugmsg( "inventory_selector::get_only_choice called with more than one choice" );
        return drop_location();
    }

    // CDF: any_item() is a bare pointer rather than DDA's item_location.
    item *const chosen = choices.front()->any_item();
    if( chosen == nullptr ) {
        return drop_location();
    }
    return drop_location( *chosen, static_cast<int>( choices.front()->chosen_count ) );
}

inventory_input inventory_selector::get_input()
{
    inventory_input res;

    res.action = ctxt.handle_input();
    res.ch = ctxt.get_raw_input().get_first_input();
    res.entry = find_entry_by_invlet( res.ch );

    if( res.entry != nullptr && !res.entry->is_selectable() ) {
        res.entry = nullptr;
    }

    return res;
}

void inventory_selector::on_input( const inventory_input &input )
{
    if( input.action == "CATEGORY_SELECTION" ) {
        toggle_navigation_mode();
    } else if( input.action == "LEFT" ) {
        toggle_active_column( scroll_direction::BACKWARD );
    } else if( input.action == "RIGHT" ) {
        toggle_active_column( scroll_direction::FORWARD );
    } else if( input.action == "INVENTORY_FILTER" ) {
        set_filter();
    } else if( input.action == "SHOW_HIDE_CONTENTS" ) {
        // Per DDA inventory_ui.cpp:1295-1310 (set_collapsed), adapted to the flat view:
        // a row of a carried group toggles its holder, any other row toggles itself.
        if( has_available_choices() ) {
            const inventory_entry &sel = get_selected();
            if( sel.is_item() ) {
                item *target = carried_holder( sel );
                if( target == nullptr ) {
                    target = sel.any_item();
                }
                const bool collapse = !target->is_collapsed();
                for( item_pocket *pocket : target->get_standard_pockets() ) {
                    pocket->settings.set_collapse( collapse );
                }
                for( inventory_column *elem : columns ) {
                    elem->invalidate_paging();
                }
                refresh_active_column();
                prepare_layout();
            }
        }
    } else if( input.action == "TOGGLE_FAVORITE" ) {
        if( has_available_choices() ) {
            for( inventory_column *elem : columns ) {
                elem->on_input( input );
            }
        }
        refresh_active_column(); // Columns can react to actions by losing their activation capacity
        prepare_layout();
    } else if( input.action == "WIELD" ) {
        auto &entry = const_cast<inventory_entry &>( get_selected() );
        wield( entry );
        assign_invlet_hints();
    } else if( input.action == "WEAR" ) {
        auto &entry = const_cast<inventory_entry &>( get_selected() );
        wear( entry );
        assign_invlet_hints();
    } else {
        if( has_available_choices() ) {
            for( inventory_column *elem : columns ) {
                elem->on_input( input );
            }
        }
        refresh_active_column(); // Columns can react to actions by losing their activation capacity
    }
}

void inventory_selector::on_change( const inventory_entry &entry )
{
    for( auto &elem : columns ) {
        elem->on_change( entry );
    }
    refresh_active_column(); // Columns can react to changes by losing their activation capacity
}

std::vector<inventory_column *> inventory_selector::get_visible_columns() const
{
    std::vector<inventory_column *> res( columns.size() );
    const auto iter = std::copy_if( columns.begin(), columns.end(), res.begin(),
    []( const inventory_column * e ) {
        return e->visible();
    } );
    res.resize( std::distance( res.begin(), iter ) );
    return res;
}

inventory_column &inventory_selector::get_column( size_t index ) const
{
    if( index >= columns.size() ) {
        static inventory_column dummy( preset );
        return dummy;
    }
    return *columns[index];
}

void inventory_selector::set_active_column( size_t index )
{
    if( index < columns.size() && index != active_column_index && get_column( index ).activatable() ) {
        get_active_column().on_deactivate();
        active_column_index = index;
        get_active_column().on_activate();
    }
}

size_t inventory_selector::get_columns_width( const std::vector<inventory_column *> &columns ) const
{
    return std::accumulate( columns.begin(), columns.end(), static_cast< size_t >( 0 ),
    []( const size_t &lhs, const inventory_column * column ) {
        return lhs + column->get_width();
    } );
}

double inventory_selector::get_columns_occupancy_ratio( size_t client_width ) const
{
    const auto visible_columns = get_visible_columns();
    const int free_width = client_width - get_columns_width( visible_columns )
                           - min_column_gap * std::max( static_cast<int>( visible_columns.size() ) - 1, 0 );
    return 1.0 - static_cast<double>( free_width ) / client_width;
}

bool inventory_selector::are_columns_centered( size_t client_width ) const
{
    return get_columns_occupancy_ratio( client_width ) >= min_ratio_to_center;
}

bool inventory_selector::is_overflown( size_t client_width ) const
{
    return get_columns_occupancy_ratio( client_width ) > 1.0;
}

void inventory_selector::toggle_active_column( scroll_direction dir )
{
    if( columns.empty() ) {
        return;
    }

    size_t index = active_column_index;

    do {
        switch( dir ) {
            case scroll_direction::FORWARD:
                index = index + 1 < columns.size() ? index + 1 : 0;
                break;
            case scroll_direction::BACKWARD:
                index = index > 0 ? index - 1 : columns.size() - 1;
                break;
        }
    } while( index != active_column_index && !get_column( index ).activatable() );

    set_active_column( index );
}

void inventory_selector::toggle_navigation_mode()
{
    mode = get_navigation_data( mode ).next_mode;
    for( auto &elem : columns ) {
        elem->on_mode_change( mode );
    }
}

void inventory_selector::append_column( inventory_column &column )
{
    column.on_mode_change( mode );

    if( columns.empty() ) {
        column.on_activate();
    }

    columns.push_back( &column );
}

const navigation_mode_data &inventory_selector::get_navigation_data( navigation_mode m ) const
{
    static const std::map<navigation_mode, navigation_mode_data> mode_data = {
        { navigation_mode::ITEM,     { navigation_mode::CATEGORY, translation(),                               c_light_gray } },
        { navigation_mode::CATEGORY, { navigation_mode::ITEM,     to_translation( "Category selection mode" ), h_white  } }
    };

    return mode_data.at( m );
}

std::string inventory_selector::action_bound_to_key( char key ) const
{
    for( const std::string &action_descriptor : ctxt.get_registered_actions_copy() ) {
        for( char bound_key : ctxt.keys_bound_to( action_descriptor ) ) {
            if( key == bound_key ) {
                return action_descriptor;
            }
        }
    }
    return std::string();
}

std::vector<char> inventory_selector::all_bound_keys() const
{
    std::vector<char> retv;
    for( const std::string &action_descriptor : ctxt.get_registered_actions_copy() ) {
        std::vector<char> to_add = ctxt.keys_bound_to( action_descriptor );
        retv.insert( retv.end(), to_add.begin(), to_add.end() );
    }
    return retv;
}

item *inventory_pick_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.entry != nullptr ) {
            if( select( input.entry->any_item() ) ) {
                ui_manager::redraw();
            }
            return input.entry->any_item();
        } else if( input.action == "QUIT" ) {
            return nullptr;
        } else if( input.action == "CONFIRM" ) {
            const inventory_entry &selected = get_active_column().get_selected();
            if( selected ) {
                return selected.any_item();
            }
        } else if( handle_action( input.action ) ) {
            return nullptr;
        } else {
            auto *const picked = input.action == "ANY_INPUT"
                                 ? pick_by_unbound_key( input.ch )
                                 : nullptr;
            if( picked != nullptr ) {
                if( select( picked ) ) {
                    ui_manager::redraw();
                }
                return picked;
            }
            on_input( input );
        }

    }
}

inventory_multiselector::inventory_multiselector( player &p,
        const inventory_selector_preset &preset,
        const std::string &selection_column_title ) :
    inventory_selector( p, preset ),
    selection_col( new selection_column( "SELECTION_COLUMN", selection_column_title ) )
{
    ctxt.register_action( "RIGHT", to_translation( "Mark/unmark selected item" ) );
    ctxt.register_action( "DROP_NON_FAVORITE", to_translation( "Mark/unmark non-favorite items" ) );

    for( auto &elem : get_all_columns() ) {
        elem->set_multiselect( true );
    }
    append_column( *selection_col );
}

void inventory_multiselector::rearrange_columns( size_t client_width )
{
    selection_col->set_visibility( true );
    inventory_selector::rearrange_columns( client_width );
    selection_col->set_visibility( !is_overflown( client_width ) );
}

size_t inventory_multiselector::query_count( size_t count = 0 )
{
    std::string count_str;
    if( count == 0 ) {
        count_str = "";
    } else {
        count_str = std::to_string( count );
    }

    spopup = std::make_unique<string_input_popup>();
    spopup->max_length( 256 ).identifier( "inventory" )
    .text( count_str );

    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->mark_resize();
    }

    do {
        ui_manager::redraw();
        spopup->query_string( /*loop=*/false );
    } while( !spopup->confirmed() && !spopup->canceled() );

    if( spopup->confirmed() ) {
        count_str = spopup->text();
        try {
            count = std::stoull( count_str );
        } catch( const std::exception &e ) {
            count = 0;
        }

        if( current_ui ) {
            current_ui->mark_resize();
        }
    }

    spopup.reset();
    return count;
}

void inventory_multiselector::set_chosen_count( inventory_entry &entry, size_t count )
{
    if( entry.is_item() ) {
        entry.chosen_count = std::min( count, entry.get_available_count() );
        on_change( entry );
    }
}

std::vector<inventory_entry *> inventory_multiselector::get_selection_column_items() const
{
    auto func = []( const inventory_entry & e ) { return e.is_item();};
    return selection_col->get_entries( func );
}

inventory_compare_selector::inventory_compare_selector( player &p ) :
    inventory_multiselector( p, default_preset, _( "ITEMS TO COMPARE" ) ) {}

std::pair<const item *, const item *> inventory_compare_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        inventory_entry *just_selected = nullptr;

        if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            toggle_entry( input.entry );
            just_selected = input.entry;
        } else if( input.action == "RIGHT" ) {
            const auto selection( get_active_column().get_all_selected() );

            for( auto &elem : selection ) {
                if( elem->chosen_count == 0 || selection.size() == 1 ) {
                    toggle_entry( elem );
                    just_selected = elem;
                    if( compared.size() == 2 ) {
                        break;
                    }
                }
            }
        } else if( input.action == "CONFIRM" ) {
            popup_getkey( _( "You need two items for comparison.  Use %s to select them." ),
                          ctxt.get_desc( "RIGHT" ) );
        } else if( input.action == "QUIT" ) {
            return std::make_pair( nullptr, nullptr );
        } else {
            on_input( input );
        }

        if( compared.size() == 2 ) {
            const auto res = std::make_pair( compared[0], compared[1] );
            // Clear second selected entry to prevent comparison reopening too
            // soon
            if( just_selected ) {
                toggle_entry( just_selected );
            }
            return res;
        }
    }
}

void inventory_compare_selector::toggle_entry( inventory_entry *entry )
{
    const item *it = &*entry->any_item();
    const auto iter = std::find( compared.begin(), compared.end(), it );

    entry->chosen_count = iter == compared.end() ? 1 : 0;

    if( entry->chosen_count != 0 ) {
        compared.push_back( it );
    } else {
        compared.erase( iter );
    }

    on_change( *entry );
}

inventory_iuse_selector::inventory_iuse_selector(
    player &p,
    const std::string &selector_title,
    const inventory_selector_preset &preset,
    const GetStats &get_st
) :
    inventory_multiselector( p, preset, selector_title ),
    get_stats( get_st )
{}

std::vector<iuse_location> inventory_iuse_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();

    int count = 0;
    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.ch >= '0' && input.ch <= '9' ) {
            count = std::min( count, INT_MAX / 10 - 10 );
            count *= 10;
            count += input.ch - '0';
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            if( count == 0 && input.entry->chosen_count == 0 ) {
                count = max_chosen_count;
            }
            set_chosen_count( *input.entry, count );
            count = 0;
        } else if( input.action == "RIGHT" ) {
            const auto selected( get_active_column().get_all_selected() );

            if( count == 0 ) {
                const bool clear = std::none_of( selected.begin(), selected.end(),
                []( const inventory_entry * elem ) {
                    return elem->chosen_count > 0;
                } );

                if( clear ) {
                    count = max_chosen_count;
                }
            }

            for( const auto &elem : selected ) {
                set_chosen_count( *elem, count );
            }
            count = 0;
        } else if( input.action == "CONFIRM" ) {
            if( to_use.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "RIGHT" ) );
                continue;
            }
            break;
        } else if( input.action == "QUIT" ) {
            return std::vector<iuse_location>();
        } else {
            on_input( input );
            count = 0;
        }
    }

    std::vector<iuse_location> ret;
    for( const auto &entry : to_use ) {
        for( const iuse_location &loc : entry.second ) {
            ret.push_back( loc );
        }
    }
    return ret;
}

void inventory_iuse_selector::set_chosen_count( inventory_entry &entry, size_t count )
{
    const item *it = &*entry.any_item();

    if( count == 0 ) {
        entry.chosen_count = 0;
        const auto iter = to_use.find( it );
        if( iter != to_use.end() ) {
            to_use.erase( iter );
        }
    } else {
        entry.chosen_count = std::min( std::min( count, static_cast<size_t>( max_chosen_count ) ),
                                       entry.get_available_count() );
        to_use[it].clear();
        if( entry.locations.size() == 1 ) {
            to_use[it].emplace_back( *entry.locations[0], static_cast<int>( entry.chosen_count ) );
        } else {
            for( size_t i = 0; i < entry.chosen_count; i++ ) {
                to_use[it].emplace_back( *entry.locations[i], 1 );
            }
        }
    }

    on_change( entry );
}

inventory_selector::header_stats inventory_iuse_selector::get_raw_stats() const
{
    if( get_stats ) {
        std::map<const item *, int> tmp;
        for( const auto &elem : to_use ) {
            int num = 0;
            for( const iuse_location &loc : elem.second ) {
                num += static_cast<int>( loc.count );
            }
            tmp.insert( std::make_pair( elem.first, num ) );
        }
        return convert_stats_to_header_stats( get_stats( tmp ) );
    }
    return convert_stats_to_header_stats( stats{{ stat{{ "", "", "", "" }}, stat{{ "", "", "", "" }} }} );
}

inventory_drop_selector::inventory_drop_selector( player &p,
        const inventory_selector_preset &preset ) :
    inventory_multiselector( p, preset, _( "ITEMS TO DROP" ) )
{
#if defined(__ANDROID__)
    // allow user to type a drop number without dismissing virtual keyboard after each keypress
    ctxt.allow_text_entry = true;
#endif
}

void inventory_drop_selector::process_selected( int &count,
        const std::vector<inventory_entry *> &selected )
{
    if( count == 0 ) {
        const bool clear = std::none_of( selected.begin(), selected.end(),
        []( const inventory_entry * elem ) {
            return elem->chosen_count > 0;
        } );

        if( clear ) {
            count = max_chosen_count;
        }
    }

    for( const auto &elem : selected ) {
        set_chosen_count( *elem, count );
    }
    count = 0;
}

drop_locations inventory_drop_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    this->keep_open = false;
    preview_dirty = true;

    // if we favorited an item, we exited this function and entered it again
    // the selected items are in "dropping", so we fetch and display them in the UI
    std::vector<std::pair<inventory_entry *, int>> selected_entries;
    const auto always_yes = []( const inventory_entry & ) {
        return true;
    };

    for( const std::pair<const item *const, int> drop_pair : dropping ) {
        for( auto &col : get_all_columns() ) {
            for( const auto &entry : col->get_entries( always_yes ) ) {
                if( entry->any_item() == drop_pair.first ) {
                    selected_entries.emplace_back( entry, drop_pair.second );
                }
            }
        }
    }

    // empty the dropping variable, in case of 2 stacks of items being merged on unfavorite/favorite
    dropping.clear();
    for( auto selected_entry : selected_entries ) {
        set_chosen_count( *selected_entry.first, selected_entry.second );
    }

    // main multidrop selection loop
    int count = 0;
    while( true ) {
        update_drop_preview();
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.ch >= '0' && input.ch <= '9' ) {
            const auto selected( get_active_column().get_all_selected() );

            count = query_count( input.ch - '0' );
            count = std::min( count, max_chosen_count );
            for( const auto &elem : selected ) {
                set_chosen_count( *elem, count );
            }

            count = 0;
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            if( count == 0 && input.entry->chosen_count == 0 ) {
                count = max_chosen_count;
            }
            set_chosen_count( *input.entry, count );
            count = 0;
        } else if( input.action == "DROP_NON_FAVORITE" ) {
            const auto filter_to_nonfavorite_and_nonworn = []( const inventory_entry & entry ) {
                return entry.is_item() &&
                       !entry.any_item()->is_favorite &&
                       !g->u.is_worn( *entry.any_item() );
            };

            const auto selected( get_active_column().get_entries( filter_to_nonfavorite_and_nonworn ) );
            process_selected( count, selected );
        } else if( input.action == "RIGHT" ) {
            const auto selected( get_active_column().get_all_selected() );

            // No amount entered, select all
            if( count == 0 ) {
                count = max_chosen_count;

                // Any non favorite item to select?
                const bool select_nonfav = std::any_of( selected.begin(), selected.end(),
                []( const inventory_entry * elem ) {
                    return ( !elem->any_item()->is_favorite ) && elem->chosen_count == 0;
                } );

                // Otherwise, any favorite item to select?
                const bool select_fav = !select_nonfav && std::any_of( selected.begin(), selected.end(),
                []( const inventory_entry * elem ) {
                    return elem->any_item()->is_favorite && elem->chosen_count == 0;
                } );

                for( const auto &elem : selected ) {
                    const bool is_favorite = elem->any_item()->is_favorite;
                    if( ( select_nonfav && !is_favorite ) || ( select_fav && is_favorite ) ) {
                        set_chosen_count( *elem, count );
                    } else if( !select_nonfav && !select_fav ) {
                        // Every element is selected, unselect all
                        set_chosen_count( *elem, 0 );
                    }
                }
                // Select the entered amount
            } else {
                for( const auto &elem : selected ) {
                    set_chosen_count( *elem, count );
                }
            }

            count = 0;
        } else if( input.action == "CONFIRM" ) {
            if( dropping.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "RIGHT" ) );
                continue;
            }
            break;
        } else if( input.action == "QUIT" ) {
            return drop_locations();
        } else {
            on_input( input );
            if( input.action == "TOGGLE_FAVORITE" || input.action == "WEAR" || input.action == "WIELD" ) {
                preview_dirty = true;
            }
            count = 0;
        }
    }

    return predicted_drops;
}

auto inventory_drop_selector::update_drop_preview() -> void
{
    if( !preview_dirty ) {
        return;
    }
    auto selected = drop_locations{};
    for( const auto &[target, count] : dropping ) {
        selected.emplace_back( *target, count );
    }
    auto preview = make_drop_preview( u, selected );
    predicted_drops = std::move( preview.items );
    predicted_counts = std::move( preview.counts );
    preview_dirty = false;

    auto drop_order = std::unordered_map<const item *, size_t> {};
    for( const auto &drop : predicted_drops ) {
        auto *target = &*drop.loc;
        if( !u.is_worn( *target ) && !u.is_wielding( *target ) ) {
            target = carried_stack_of( u, target ).front();
        }
        drop_order.try_emplace( target, drop_order.size() );
    }

    for( auto *column : get_all_columns() ) {
        if( column == selection_col.get() ) {
            continue;
        }
        for( auto *entry : column->get_all_entries( []( const auto & candidate ) { return candidate.is_item(); } ) ) {
            const auto found = predicted_counts.find( entry->item_stack_on_character() );
            const auto total = found == predicted_counts.end() ? size_t{ 0 } :
                               static_cast<size_t>( found->second );
            entry->automatic_drop_count = total > entry->chosen_count ? total - entry->chosen_count : 0;
            const auto order = drop_order.find( entry->item_stack_on_character() );
            entry->drop_order = order == drop_order.end() ? std::nullopt :
                                std::optional<size_t> { order->second };
            on_change( *entry );
        }
    }
    // Redraw does not prepare paging.  Sort after the entire selection batch has been updated.
    selection_col->prepare_paging( get_filter() );
}

void inventory_drop_selector::set_chosen_count( inventory_entry &entry, size_t count )
{
    preview_dirty = true;
    item *it = entry.item_stack_on_character();

    if( count == 0 ) {
        entry.chosen_count = 0;
        const auto iter = dropping.find( it );
        if( iter != dropping.end() ) {
            dropping.erase( iter );
        }
    } else {
        entry.chosen_count = std::min( std::min( count, static_cast<size_t>( max_chosen_count ) ),
                                       entry.get_available_count() );
        dropping[it] = entry.chosen_count;
    }

    on_change( entry );
}

inventory_selector::header_stats inventory_drop_selector::get_raw_stats() const
{
    return convert_stats_to_header_stats( get_weight_and_volume_stats(
            u.weight_carried_reduced_by( predicted_counts ),
            u.weight_capacity(),
            u.volume_carried_reduced_by( predicted_counts ),
            u.volume_capacity_reduced_by( 0_ml, predicted_counts ) ) );
}

inventory_pickup_selector::inventory_pickup_selector( player &p,
        const inventory_selector_preset &preset ) :
    inventory_multiselector( p, preset, "ITEMS TO PICKUP" ) {}

std::vector<pickup::pick_drop_selection> inventory_pickup_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();

    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.ch >= '0' && input.ch <= '9' ) {
            const auto selected( get_active_column().get_all_selected() );

            size_t count = query_count( input.ch - '0' );
            for( const auto &elem : selected ) {
                set_chosen_count( *elem, count );
            }
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            if( input.entry->chosen_count == 0 ) {
                set_chosen_count( *input.entry, max_chosen_count );
            }
        } else if( input.action == "RIGHT" ) {
            const auto selected( get_active_column().get_all_selected() );

            size_t count = max_chosen_count;

            // Any non favorite item to select?
            const bool select_nonfav = std::any_of( selected.begin(), selected.end(),
            []( const inventory_entry * elem ) {
                return ( !elem->any_item()->is_favorite ) && elem->chosen_count == 0;
            } );

            // Otherwise, any favorite item to select?
            const bool select_fav = !select_nonfav && std::any_of( selected.begin(), selected.end(),
            []( const inventory_entry * elem ) {
                return elem->any_item()->is_favorite && elem->chosen_count == 0;
            } );

            for( const auto &elem : selected ) {
                const bool is_favorite = elem->any_item()->is_favorite;
                if( ( select_nonfav && !is_favorite ) || ( select_fav && is_favorite ) ) {
                    set_chosen_count( *elem, count );
                } else if( !select_nonfav && !select_fav ) {
                    // Every element is selected, unselect all
                    set_chosen_count( *elem, 0 );
                }
            }
        } else if( input.action == "CONFIRM" ) {
            std::vector<pickup::pick_drop_selection> result;
            std::vector<item *> locations;
            std::vector<int> counts;

            for( auto entry_ptr : map_column.get_all_entries() ) {
                int count = 0;
                int chosen_count = entry_ptr->chosen_count;
                for( size_t i = 0; i < entry_ptr->locations.size() && count < chosen_count &&
                     count < max_chosen_count; ++i ) {
                    item *item = entry_ptr->locations[i];
                    int needed_count = std::max( 0, chosen_count - count );
                    int to_add = std::min( needed_count, item->count() );
                    if( to_add > 0 ) {
                        locations.push_back( entry_ptr->locations[i] );
                        counts.push_back( to_add );
                        count += to_add;
                    }
                }
            }

            result = pickup::optimize_pickup( locations, counts );
            if( result.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "RIGHT" ) );
                continue;
            } else {
                return result;
            }
        } else if( input.action == "QUIT" ) {
            return std::vector<pickup::pick_drop_selection>();
        } else {
            on_input( input );
        }

        bool no_items = true;
        for( auto col_ptr : get_visible_columns() ) {
            if( col_ptr->allows_selecting() && !col_ptr->empty() ) {
                no_items = false;
                break;
            }
        }

        if( no_items && ( input.action == "WIELD" || input.action == "WEAR" ) ) {
            return std::vector<pickup::pick_drop_selection>();
        }
    }

    return std::vector<pickup::pick_drop_selection>();
}

inventory_selector::header_stats inventory_pickup_selector::get_raw_stats() const
{
    units::mass weight_carried = u.weight_carried();
    units::volume volume_carried = u.carried_volume();
    auto func = []( const inventory_entry & entry ) {
        return entry.is_item() && entry.chosen_count > 0;
    };
    std::vector<inventory_entry *> selected_items = selection_col->get_all_entries( func );

    //Add the weights and volumes of selected items
    //which might be picked up
    for( auto entry_ptr : selected_items ) {
        int count = 0;
        int chosen_count = entry_ptr->chosen_count;
        for( size_t i = 0; i < entry_ptr->locations.size() && count < chosen_count &&
             count < max_chosen_count; ++i ) {
            item *item = entry_ptr->locations[i];
            int needed_count = std::max( 0, chosen_count - count );
            int to_add = std::min( needed_count, item->count() );
            count += to_add;
            //WARNING: This specific order of operations and casts are needed
            //to ensure volume and weight are accurate, because of the fact the game
            //works in base volumes of 1ml and 1mg
            weight_carried += item->weight() * ( static_cast<double>( to_add ) / item->count() );
            volume_carried += item->volume() * ( static_cast<double>( to_add ) / item->count() );
        }
    }

    return convert_stats_to_header_stats( get_weight_and_volume_stats(
            weight_carried,
            u.weight_capacity(),
            volume_carried,
            u.volume_capacity() ) );
}
