#include "inventory_ui.h"

#include "avatar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "carried_stacks.h"
#include "character.h"
#include "debug.h"
#include "detached_ptr.h"
#include "drop_preview.h"
#include "enum_conversions.h"
#include "flag.h"
#include "avatar_action.h"
#include "game.h"
#include "game_inventory.h"
#include "ime.h"
#include "inventory.h"
#include "inventory_examiner.h"
#include "item.h"
#include "item_category.h"
#include "item_search.h"
#include "item_stack.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "map/map.h"
#include "map/map_selector.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "point.h"
#include "reload/reload.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "uistate.h"
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
#include <cmath>
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

namespace
{
// Per DDA inventory_ui.cpp:183-187. CDF has no INTEGRATED or BIONIC_FUEL_SOURCE categories.
bool is_worn_id( const item_category_id &id )
{
    return id == item_category_id( "ITEMS_WORN" );
}

// CDF: DDA's unit_to_string/vol_to_string (DDA units_utility.cpp:367-446) is not ported, so
// these build the same compact strings ("1.5L", "0.2kg", "30cm") from the CDF unit helpers.
std::string format_volume_compact( const units::volume &volume )
{
    int scale = 0;
    const double value = convert_volume( units::to_milliliter( volume ), &scale );
    return string_format( "%g%s", std::round( value * 1000.0 ) / 1000.0, volume_units_abbr() );
}

std::string format_weight_compact( const units::mass &weight )
{
    return string_format( "%g%s", std::round( convert_weight( weight ) * 100.0 ) / 100.0,
                          weight_units() );
}

// Per DDA inventory_ui.cpp:250-265.
struct container_data {
    units::volume actual_capacity;
    units::volume total_capacity;
    units::mass actual_capacity_weight;
    units::mass total_capacity_weight;
    units::length max_containable_length;

    std::string to_formatted_string() const {
        return string_format( _( "(remains %s, %s) max length %s" ),
                              format_volume_compact( total_capacity - actual_capacity ),
                              format_weight_compact( total_capacity_weight - actual_capacity_weight ),
                              string_format_length( max_containable_length, true ) );
    }
};
} // namespace

// Per DDA inventory_ui.cpp:318-328.
void inventory_selector_save_state::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "uimode", uimode );
    json.end_object();
}

void inventory_selector_save_state::deserialize( JsonObject const &jo )
{
    jo.read( "uimode", uimode );
}

namespace io
{
// Per DDA inventory_ui.cpp:332-342: the footer's view-mode wording, with DDA's translation context.
template<>
std::string enum_to_string<inventory_selector::uimode>( inventory_selector::uimode mode )
{
    switch( mode ) {
        case inventory_selector::uimode::hierarchy:
            return pgettext( "inventory ui mode", "hierarchy" );
        case inventory_selector::uimode::last:
        case inventory_selector::uimode::categories:
            break;
    }
    return pgettext( "inventory ui mode", "categories" );
}
} // namespace io

// Per DDA inventory_ui.cpp:345-348. inventory_sel_default_state is the fallback of every preset that
// carries no state pointer; inventory_ui_default_state is the plain inventory menu's, and the pickup
// menu has its own pair, which opens in hierarchy mode.
inventory_selector_save_state inventory_sel_default_state{ inventory_selector::uimode::categories };
inventory_selector_save_state inventory_ui_default_state{ inventory_selector::uimode::categories };
inventory_selector_save_state pickup_sel_default_state{ inventory_selector::uimode::categories };
inventory_selector_save_state pickup_ui_default_state{ inventory_selector::uimode::hierarchy };

// Per DDA inventory_ui.cpp:350-364. CDF: uistatedata::serialize/deserialize live in savegame_json.cpp
// and have to call these (FM7).
void save_inv_state( JsonOut &json )
{
    json.member( "inventory_sel_default_state", inventory_sel_default_state );
    json.member( "inventory_ui_state", inventory_ui_default_state );
    json.member( "pickup_sel_state", pickup_sel_default_state );
    json.member( "pickup_ui_state", pickup_ui_default_state );
}

void load_inv_state( const JsonObject &jo )
{
    jo.read( "inventory_sel_default_state", inventory_sel_default_state );
    jo.read( "inventory_ui_state", inventory_ui_default_state );
    jo.read( "pickup_sel_state", pickup_sel_default_state );
    jo.read( "pickup_ui_state", pickup_ui_default_state );
}

constexpr int max_chosen_count = std::numeric_limits<int>::max();

struct navigation_mode_data {
    navigation_mode next_mode;
    translation name;
    nc_color color;
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

pickup_inventory_preset::pickup_inventory_preset( const player &p ) : p( p )
{
    // Per DDA game_inventory.cpp:562-564: the pickup menu keeps its own view mode.
    save_state = &pickup_sel_default_state;
    // Per DDA game_inventory.cpp:565: the pickup menu lists the standard pockets, not only the
    // container pocket.
    _pk_type = { pocket_type::LAST };
}

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

inventory_column::inventory_column( const inventory_selector_preset &preset ) : preset( preset )
{
    hide_entries_override = uistate.hide_entries_override;
    cells.resize( preset.get_cells_count() );
}

bool inventory_column::activatable() const
{
    return std::ranges::any_of( entries, [this]( const inventory_entry & e ) {
        return e.is_highlightable( skip_unselectable );
    } );
}

// Per DDA inventory_ui.cpp:737-740.
void inventory_column::toggle_skip_unselectable( const bool skip )
{
    skip_unselectable = skip;
}

// Per DDA inventory_ui.cpp:3585-3597.
void inventory_column::cycle_hide_override()
{
    if( hide_entries_override ) {
        if( *hide_entries_override ) {
            hide_entries_override = std::nullopt;
        } else {
            hide_entries_override = true;
        }
    } else {
        hide_entries_override = false;
    }
    uistate.hide_entries_override = hide_entries_override;
    paging_is_valid = false;
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
    // Per DDA inventory_ui.cpp:755-763: the localized compare ignores case.
    // CDF: the entry keeps one cached name (`tname( 1 )`) only, so there are no
    // contents-count, unprefixed-name, link key or generation tie-breaks to add here.
    return localized_compare( lhs.cached_name, rhs.cached_name );
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
    size_t count = entry.get_stack_size();
    std::string disp_name;
    if( entry.any_item()->is_money() ) {
        disp_name = entry.any_item()->display_money( count, entry.get_total_charges() );
    } else if( entry.is_collation_header() && entry.any_item()->count_by_charges() ) {
        // Per DDA inventory_ui.cpp:786-796. CDF: display_name has no with_prefix parameter.
        item temp( *entry.any_item() );
        temp.charges = entry.get_total_charges();
        disp_name = temp.display_name( 1 );
        count = 1;
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
        std::string text = cells[cell_index].get_text( entry );
        const item &actual_item = *entry.any_item();
        const std::string info_display = get_option<std::string>( "DETAILED_CONTAINERS" );
        // if we want no additional info skip this
        if( info_display != "NONE" ) {
            // if we want additional info for all items or it is worn then add the additional info
            if( info_display == "ALL" || ( info_display == "WORN" &&
                                           is_worn_id( entry.get_category_ptr()->get_id() ) &&
                                           get_avatar().is_worn( actual_item ) ) ) {
                if( cell_index == 0 && !text.empty() &&
                    actual_item.is_container() && actual_item.has_unrestricted_pockets() ) {
                    auto ok_unrestricted_container = []( const item_pocket & pocket ) {
                        return item_pocket::ok_default_containers( pocket ) && !pocket.is_restricted();
                    };
                    const units::volume total_capacity = actual_item.get_volume_capacity( ok_unrestricted_container );
                    const units::mass total_capacity_weight = actual_item.get_total_weight_capacity( true );
                    const units::length max_containable_length = actual_item.max_containable_length( true );

                    const units::volume actual_capacity = actual_item.get_contents_volume( ok_unrestricted_container );
                    const units::mass actual_capacity_weight = actual_item.get_total_contained_weight( true );

                    const container_data container_data = {
                        actual_capacity,
                        total_capacity,
                        actual_capacity_weight,
                        total_capacity_weight,
                        max_containable_length
                    };
                    text = text + string_format( " %s", container_data.to_formatted_string() );
                }
            }
        }
        return text;
    } else if( cell_index != 0 ) {
        return replace_colors( cells[cell_index].title );
    } else {
        // CDF: item_category has one name field; item_category.json carries DDA's header text.
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
        if( !entries[new_index].is_highlightable( skip_unselectable ) ) {
            new_index = next_highlightable_index( new_index, dir );
        }

        selected_index = new_index;
        page_offset = ( new_index == static_cast<size_t>( -1 ) ) ?
                      0 : selected_index - selected_index % entries_per_page;
    }
}

// Per DDA inventory_ui.cpp:972-996. CDF: was next_selectable_index.
size_t inventory_column::next_highlightable_index( size_t index, scroll_direction dir ) const
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
    } while( new_index != index && !entries[new_index].is_highlightable( skip_unselectable ) );

    if( !entries[new_index].is_highlightable( skip_unselectable ) ) {
        return static_cast<size_t>( -1 );
    }

    return new_index;
}

void inventory_column::move_selection( scroll_direction dir )
{
    size_t index = selected_index;

    do {
        index = next_highlightable_index( index, dir );
    } while( index != selected_index && is_selected_by_category( entries[index] ) );

    select( index, dir );
}

void inventory_column::move_selection_page( scroll_direction dir )
{
    size_t index = selected_index;

    do {
        const size_t next_index = next_highlightable_index( index, dir );
        const bool flipped = next_index == selected_index ||
                             ( next_index > selected_index ) != ( static_cast<int>( dir ) > 0 );

        if( flipped && page_of( next_index ) == page_index() ) {
            break; // If flipped and still on the same page - no need to flip
        }

        index = next_index;
    } while( page_of( next_highlightable_index( index, dir ) ) == page_index() );

    select( index, dir );
}

// Per DDA inventory_ui.cpp:1027-1039 (mouse wheel).
void inventory_column::scroll_selection_page( scroll_direction dir )
{
    size_t index = selected_index;

    while( page_of( index = next_highlightable_index( index, dir ) ) == page_index() ) {
        if( index == selected_index && page_of( index ) == page_index() ) {
            break; // If flipped and still on the same page - no need to flip
        }
    }

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
    // Per DDA inventory_ui.cpp:660-672: keep a collation header's enabled state aligned with denial.
    if( entry.is_collation_header() ) {
        entry.collation_meta->enabled = result.denial.empty();
    }
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

void inventory_column::expand_to_fit( const inventory_entry &entry, bool with_denial )
{
    if( !entry ) {
        return;
    }

    // Don't use cell cache here since the entry may not yet be placed into the vector of entries.
    const std::string denial = preset.get_denial( entry );

    for( size_t i = 0, num = with_denial && denial.empty() ? cells.size() : 1; i < num; ++i ) {
        auto &cell = cells[i];

        cell.real_width = std::max( cell.real_width, get_entry_cell_width( entry, i ) );

        // Don't reveal the cell for headers and stubs
        if( cell.visible() || ( entry.is_item() && !preset.is_stub_cell( entry, i ) ) ) {
            const size_t cell_gap = i > 0 ? normal_cell_gap : 0;
            cell.current_width = std::max( cell.current_width, cell_gap + cell.real_width );
        }
    }

    // Per DDA inventory_ui.cpp:1178-1181: a caller can skip the denial space (the selection column).
    if( with_denial && !denial.empty() ) {
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
    } else if( input.action == "SCROLL_DOWN" ) {
        scroll_selection_page( scroll_direction::FORWARD );
    } else if( input.action == "SCROLL_UP" ) {
        scroll_selection_page( scroll_direction::BACKWARD );
    } else if( input.action == "HOME" ) {
        select( 0, scroll_direction::FORWARD );
    } else if( input.action == "EXAMINE" ) {
        // G31: only act when a row is highlighted (DDA inventory_ui.cpp:3662-3667 guards with
        // if( selected )). get_selected() returns a null dummy for a header or an empty column.
        const inventory_entry &selected = get_selected();
        if( selected.is_item() ) {
            item *const highlighted = selected.any_item();

            std::vector<iteminfo> this_item = highlighted->info();
            item_info_data dummy( highlighted->display_name(), {}, this_item, {} );
            dummy.handle_scrolling = true;
            draw_item_info( []() -> catacurses::window {
                const int width = std::min( TERMX, FULL_SCREEN_WIDTH ) + std::max( 0, TERMX - FULL_SCREEN_WIDTH ) * ( 1.0 / 4 );
                const int height = std::min( TERMY, FULL_SCREEN_HEIGHT ) + std::max( 0, TERMY - FULL_SCREEN_HEIGHT ) * ( 2.0 / 3 );
                return catacurses::newwin( height, width, point( ( TERMX - width ) / 2, ( TERMY - height ) / 2 ) );
            }, dummy );
        }

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
    // Per DDA inventory_ui.cpp:1401-1408: rows hidden by collapse or the filter move with the column.
    for( const auto &elem : entries_hidden ) {
        dest.entries_hidden.push_back( elem );
    }
    entries_hidden.clear();
    dest.prepare_paging();
    clear();
}

// CDF adapters for DDA's item_location::has_parent() / parent_pocket() (PL-61 RESULT.md section 3).
static auto has_parent( const item &it ) -> bool
{
    return it.parent_item() != nullptr;
}

static auto parent_pocket_of( const item &it ) -> const item_pocket *
{
    const item *parent = it.parent_item();
    return parent == nullptr ? nullptr : parent->contents.pocket_of( &it );
}

// UI1: DDA's item::is_container() is "has a container pocket". CDF's is_container() leaves out worn
// storage (`carry` pockets), so a garment holding items would never take the hide-all override.
static auto holds_contents( const item &it ) -> bool
{
    return it.has_pocket_type( pocket_type::CONTAINER );
}

// UI1: hide_entries_override is the column's "hide all contents" state (DDA is_hidden 590-592).
static auto hidden_by_collapse( const inventory_entry &entry,
                                const std::optional<bool> &hide_entries_override = std::nullopt ) -> bool
{
    // Per DDA inventory_ui.cpp:575-585: collated entries are hidden while their header is collapsed.
    if( entry.is_collation_entry() && entry.collation_meta->collapsed ) {
        return true;
    }
    // Per DDA inventory_ui.cpp:590-592: the hide-all override beats the pocket settings.
    if( hide_entries_override && entry.is_item() && entry.topmost_parent != nullptr &&
        holds_contents( *entry.topmost_parent ) ) {
        return *hide_entries_override;
    }
    // Per DDA inventory_ui.cpp:575-600: hierarchy rows under a collapsed pocket of their parents
    if( entry.is_item() && entry.topmost_parent != nullptr ) {
        const item *it = entry.any_item();
        while( has_parent( *it ) && it != entry.topmost_parent ) {
            const item_pocket *pocket = parent_pocket_of( *it );
            if( pocket != nullptr && pocket->settings.is_collapsed() ) {
                return true;
            }
            it = it->parent_item();
        }
    }
    return false;
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

// Per DDA inventory_ui.cpp:1432-1458. CDF: item * instead of item_location.
bool inventory_column::indented_sort_compare( const inventory_entry &lhs,
        const inventory_entry &rhs )
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
    // CDF: the entry constructor has no indent/generation parameters.
    inventory_entry ep_lhs( { p_lhs }, nullptr, true );
    inventory_entry ep_rhs( { p_rhs }, nullptr, true );
    ep_lhs.update_cache();
    ep_rhs.update_cache();
    return sort_compare( ep_lhs, ep_rhs );
}

// Per DDA inventory_ui.cpp:1410-1430: disabled rows last, then assigned letters, then favourites,
// and only then the preset's own order.
bool inventory_column::sort_compare( inventory_entry const &lhs, inventory_entry const &rhs )
{
    if( lhs.is_selectable() != rhs.is_selectable() ) {
        return lhs.is_selectable(); // Disabled items always go last
    }
    avatar &player_character = get_avatar();
    // Place favorite items and items with an assigned inventory letter first,
    // since the player cared enough to assign them
    const bool left_has_invlet = player_character.invlet_is_assigned( lhs.any_item()->invlet );
    const bool right_has_invlet = player_character.invlet_is_assigned( rhs.any_item()->invlet );
    if( left_has_invlet != right_has_invlet ) {
        return left_has_invlet;
    }
    const bool left_fav = lhs.any_item()->is_favorite;
    const bool right_fav = rhs.any_item()->is_favorite;
    if( left_fav != right_fav ) {
        return left_fav;
    }

    return preset.sort_compare( lhs, rhs );
}

// Per DDA inventory_ui.cpp:1459-1548. CDF: item * instead of item_location; no indent/generation.
bool inventory_column::collated_sort_compare( inventory_entry const &lhs,
        inventory_entry const &rhs )
{
    if( lhs.is_collated() && lhs.collation_meta == rhs.collation_meta ) {
        if( lhs.is_collation_header() ) {
            return true;
        }
        if( rhs.is_collation_header() ) {
            return false;
        }
        return sort_compare( lhs, rhs );
    }

    item *p_lhs = lhs.is_collated() ? lhs.collation_meta->tip : lhs.any_item();
    item *p_rhs = rhs.is_collated() ? rhs.collation_meta->tip : rhs.any_item();

    // CDF: the entry constructor has no indent/generation parameters.
    inventory_entry ep_lhs( { p_lhs }, nullptr,
                            lhs.is_collated() ? lhs.collation_meta->enabled : lhs.is_selectable() );
    inventory_entry ep_rhs( { p_rhs }, nullptr,
                            rhs.is_collated() ? rhs.collation_meta->enabled : rhs.is_selectable() );
    ep_lhs.update_cache();
    ep_rhs.update_cache();
    if( indent_entries() ) {
        return indented_sort_compare( ep_lhs, ep_rhs );
    }
    return sort_compare( ep_lhs, ep_rhs );
}

// Per DDA inventory_ui.cpp:1489-1527.
void inventory_column::collate()
{
    for( auto outer = entries.begin(); outer != entries.end(); ++outer ) {
        if( !outer->is_item() || outer->is_collated() || outer->chevron ) {
            continue;
        }
        for( auto e = std::next( outer ); e != entries.end(); ) {
            if( e->is_item() && e->get_category_ptr() == outer->get_category_ptr() &&
                e->any_item()->is_favorite == outer->any_item()->is_favorite &&
                e->any_item()->typeId() == outer->any_item()->typeId() &&
                // CDF: link_length/is_same_relic not in CDF
                ( !indent_entries() || e->any_item()->parent_item() == outer->any_item()->parent_item() ) &&
                ( e->is_collation_header() || !e->chevron ) ) {

                if( !outer->is_collated() ) {
                    outer->collation_meta = std::make_shared<collation_meta_t>(
                                                collation_meta_t{ outer->any_item(), true, outer->is_selectable() } );

                    entries_hidden.emplace_back( *outer );

                    outer->chevron = true;
                    set_collapsed( *outer, true );
                    // CDF: no cell cache
                }
                e->collation_meta = outer->collation_meta;
                std::copy( e->locations.begin(), e->locations.end(),
                           std::back_inserter( outer->locations ) );
                entries_hidden.emplace_back( std::move( *e ) );
                e = entries.erase( e );
            } else {
                ++e;
            }
        }
    }
    _collated = true;
}

// Per DDA inventory_ui.cpp:1529-1539.
void inventory_column::_reset_collation( std::vector<inventory_entry> &entries )
{
    for( auto iter = entries.begin(); iter != entries.end(); ) {
        if( iter->is_collation_header() ) {
            iter = entries.erase( iter );
        } else {
            iter->reset_collation();
            ++iter;
        }
    }
}

// Per DDA inventory_ui.cpp:1541-1548.
void inventory_column::uncollate()
{
    if( _collated ) {
        _reset_collation( entries );
        _reset_collation( entries_hidden );
        _collated = false;
    }
}

// Per DDA inventory_ui.cpp:1294-1316. CDF: no per-entry cell cache.
void inventory_column::set_collapsed( inventory_entry &entry, bool collapse )
{
    bool collapsed = false;
    if( entry.is_collation_header() ) {
        entry.collation_meta->collapsed = collapse;
        collapsed = true;
    } else {
        for( item *const loc : entry.locations ) {
            for( item_pocket *pocket : loc->get_standard_pockets() ) {
                pocket->settings.set_collapse( collapse );
                collapsed = true;
            }
        }
    }

    if( collapsed ) {
        paging_is_valid = false;
    }
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

    // Per DDA inventory_ui.cpp:1561-1564: with a filter typed, rows hidden by a collapsed
    // parent are shown as well, except the entries of a collapsed collation group.
    const auto is_visible = [&filter_fn, &filter, this]( const inventory_entry & it ) {
        return it.is_item() && filter_fn( it ) &&
               ( ( !filter.empty() && !it.is_collation_entry() ) ||
                 !hidden_by_collapse( it, hide_entries_override ) );
    };

    // First, remove all non-items
    for( size_t i = 0; i < entries_hidden.size(); ++i ) {
        entries.push_back( entries_hidden[i] );
    }

    entries_hidden.clear();
    for( size_t i = 0; i < entries.size(); ++i ) {
        // CDF: only item rows move to the hidden list; the category headers are rebuilt below.
        if( entries[i].is_item() && !is_visible( entries[i] ) ) {
            entries_hidden.push_back( entries[i] );
        }
    }

    const auto new_end = std::remove_if( entries.begin(), entries.end(),
    [&is_visible]( const inventory_entry & entry ) {
        return !is_visible( entry );
    } );
    entries.erase( new_end, entries.end() );
    // don't sort with stale names
    std::ranges::for_each( entries, &inventory_entry::update_cache );
    // Then sort them with respect to categories
    auto sort_function = [this]( const inventory_entry & lhs, const inventory_entry & rhs ) {
        if( *lhs.get_category_ptr() != *rhs.get_category_ptr() ) {
            return *lhs.get_category_ptr() < *rhs.get_category_ptr();
        } else if( _collated ) {
            return collated_sort_compare( lhs, rhs );
        } else if( indent_entries() ) {
            return indented_sort_compare( lhs, rhs );
        } else {
            return sort_compare( lhs, rhs );
        }
    };
    std::ranges::stable_sort( entries, sort_function );

    // Per DDA inventory_ui.cpp:1592-1594.
    if( !_collated && collate_entries() ) {
        collate();
    }

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
    // Per DDA inventory_ui.cpp:1625-1626.
    const size_t ind = selected_index >= entries.size() ? 0 : selected_index;
    select( ind, ind ? scroll_direction::BACKWARD : scroll_direction::FORWARD );
}

void inventory_column::clear()
{
    entries.clear();
    entries_cell_cache.clear();
    paging_is_valid = false;
}

bool inventory_column::select( const item *loc )
{
    return select( loc, false );
}

// Per DDA inventory_ui.cpp:1636-1675 (highlight). CDF: item * instead of item_location.
bool inventory_column::select( const item *loc, bool front_only )
{
    for( size_t index = 0; index < entries.size(); ++index ) {
        inventory_entry &ent = entries[index];
        // CDF: DDA extract line 362 has no is_selectable gate; base 6d202a1ce8 required it
        // for the default path.
        // A-M2 fix: with skip off the cursor may rest on a disabled row, so gate on highlightable.
        if( !ent.is_item() || ( !front_only && !ent.is_highlightable( skip_unselectable ) ) ) {
            continue;
        }
        // CDF: guard, entries can have no locations.
        const bool front_matches = !ent.is_collation_header() && !ent.locations.empty() &&
                                   ent.locations.front() == loc;
        if( ( !front_only &&
              std::find( ent.locations.begin(), ent.locations.end(), loc ) !=
              ent.locations.end() ) ||
            front_matches ) {
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
    if( indent_entries() ) {
        res += entry.indent;
    }
    if( get_option<bool>( "ITEM_SYMBOLS" ) ) {
        res += 2;
    }
    if( allows_selecting() && activatable() && multiselect ) {
        res += 2;
    }
    // Per DDA inventory_ui.cpp:383-386.
    if( collate_entries() && entry.is_collation_entry() ) {
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

void inventory_column::draw( const catacurses::window &win, point pos,
                             std::vector<inventory_entry_rect> *rects ) const
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

        if( rects != nullptr ) {
            // DDA inventory_ui.cpp:1733-1737. The map only reads the entry back as non-const.
            const int hx_end = pos.x + get_width();
            rects->push_back( { inclusive_rectangle<point>( point( x1, yy ), point( hx_end - 1, yy ) ),
                                const_cast<inventory_entry *>( &entry ) } );
        }

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
                    // Per DDA inventory_ui.cpp:1801-1807: the hide-all override colours the chevron
                    const bool hide_override = !entry.is_collation_header() && hide_entries_override &&
                                               holds_contents( *entry.any_item() );
                    const nc_color chevron_color = entry.is_collation_header() ? c_light_blue :
                                                   hide_override ? ( *hide_entries_override ? c_red : c_green ) :
                                                   c_dark_gray;
                    const bool collapsed = entry.is_collation_header() ?
                                           entry.collation_meta->collapsed :
                                           hide_override ? *hide_entries_override :
                                           entry.any_item()->is_collapsed();
                    trim_and_print( win, point( text_x - 1, yy ), 1, chevron_color,
                                    collapsed ? "▶" : "▼" );
                }
                const std::string &hl_option = get_option<std::string>( "INVENTORY_HIGHLIGHT" );
                if( entry.is_item() && ( selected || !entry.is_selectable() ) ) {
                    trim_and_print( win, point( text_x, yy ), text_width, selected ? h_white : c_dark_gray,
                                    remove_color_tags( text ) );
                } else if( entry.is_item() && entry.highlight_as_parent ) {
                    if( hl_option == "symbol" ) {
                        trim_and_print( win, point( text_x - 1, yy ), 1, h_white, "<" );
                        trim_and_print( win, point( text_x, yy ), text_width, entry_cell_cache.color, text );
                    } else {
                        trim_and_print( win, point( text_x, yy ), text_width, c_white_white,
                                        remove_color_tags( text ) );
                    }
                } else if( entry.is_item() && entry.highlight_as_child ) {
                    if( hl_option == "symbol" ) {
                        trim_and_print( win, point( text_x - 1, yy ), 1, h_white, ">" );
                        trim_and_print( win, point( text_x, yy ), text_width, entry_cell_cache.color, text );
                    } else {
                        trim_and_print( win, point( text_x, yy ), text_width, c_black_white,
                                        remove_color_tags( text ) );
                    }
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
    selected_cat( id, no_translation( name ), 0 )
{
    // Per DDA inventory_ui.cpp:1877: the selection column always shows every marked entry.
    hide_entries_override = { false };
}

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
                    // Per DDA inventory_ui.cpp:1890: the selection column does not reserve denial space.
                    expand_to_fit( *ent, false );
                }
            }
        }
    }
}

void selection_column::prepare_paging( const std::string & )
{
    // Per DDA inventory_ui.cpp:1897-1900: always show all selected items, filter or not.
    inventory_column::prepare_paging( std::string() );

    if( entries.empty() ) { // Category must always persist
        entries.emplace_back( &*selected_cat );
        // Per DDA inventory_ui.cpp:1904.
        expand_to_fit( entries.back(), false );
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
            // Per DDA inventory_ui.cpp:1929-1939: no denial space in the selection column.
            expand_to_fit( my_entry, false );
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

    // CDF: P3-NESTAMMO2. What sits nested in the items of the stacks (the ammo selector only).
    std::vector<item *> roots;
    for( const std::list<item *> &stack : stacks ) {
        std::transform( stack.begin(), stack.end(), std::back_inserter( roots ), locator );
    }
    add_nested_map_items( target_column, roots, custom_category );
}

// CDF: P3-NESTAMMO2. Nothing nested is listed unless a selector asks for it (the ammo selector does).
void inventory_selector::add_nested_map_items( inventory_column &, const std::vector<item *> &,
        const item_category * )
{
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
    // Per DDA inventory_ui.cpp:2109-2141: the preset's pocket types decide which contents are
    // listed. The default is the container pocket alone, so gun mods, cable items and magazine
    // wells are not child rows; { pocket_type::LAST } lists the standard pockets instead.
    // CDF: the untyped item_contents::all_items_top() returns the items of every pocket (PL-64
    // G32); item::all_items_top() is the standard walk here, so it stands in for DDA's untyped call.
    std::vector<item *> items;
    if( preset.get_pocket_type().size() == 1 && preset.has_pocket_type( pocket_type::LAST ) ) {
        items = container->all_items_top();
    } else {
        for( const pocket_type pt : preset.get_pocket_type() ) {
            const std::vector<item *> pocket_items = container->all_items_top( pt );
            items.insert( items.begin(), pocket_items.begin(), pocket_items.end() );
        }
    }
    bool vis_top = false;
    inventory_column temp( preset );
    for( item *it : items ) {
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

void inventory_selector::add_contained_items( item *container )
{
    add_contained_items( container, own_inv_column );
}

void inventory_selector::action_examine( const item *sitem )
{
    // Per DDA inventory_ui.cpp:4315-4333
    std::vector<iteminfo> info = sitem->info();
    info.insert( info.begin(), iteminfo( "BASE", string_format( _( "Location: %s" ),
                                         sitem->describe_location( &u ) ) ) );
    const std::vector<iteminfo> dummy;
    item_info_data data( sitem->tname( 1, false ), sitem->type_name(), info, dummy );
    data.handle_scrolling = true;
    draw_item_info( []() -> catacurses::window {
        const int width = std::min( TERMX, FULL_SCREEN_WIDTH ) + std::max( 0,
                          TERMX - FULL_SCREEN_WIDTH ) * ( 1.0 / 4 );
        const int height = std::min( TERMY, FULL_SCREEN_HEIGHT ) + std::max( 0,
                           TERMY - FULL_SCREEN_HEIGHT ) * ( 2.0 / 3 );
        return catacurses::newwin( height, width, point( ( TERMX - width ) / 2, ( TERMY - height ) / 2 ) );
    }, data );
}

void inventory_selector::add_character_items( Character &character )
{
    character_items_added = true;
    // Per DDA inventory_ui.cpp:2195-2212: one listing path for both view modes. Every worn/wielded
    // row is made by add_entry_rec, which sets the row's chevron when it has visible contents.
    // CDF: item * instead of DDA's item_location (OPENCODE_RULES).
    item &weapon = character.primary_weapon();
    const bool hierarchy = _uimode == uimode::hierarchy;
    if( !weapon.is_null() ) {
        add_entry_rec( own_gear_column, hierarchy ? own_gear_column : own_inv_column, &weapon,
                       &item_category_id( "WEAPON_HELD" ).obj(),
                       hierarchy ? &item_category_id( "WEAPON_HELD" ).obj() : nullptr, nullptr, 0 );
    }
    for( item *worn_item : character.top_items_loc() ) {
        const item_category *custom_cat = wielded_worn_category( worn_item, u );
        add_entry_rec( own_gear_column, hierarchy ? own_gear_column : own_inv_column, worn_item,
                       custom_cat, hierarchy ? custom_cat : nullptr, nullptr, 0 );
    }
    // Per DDA inventory_ui.cpp:2209-2211: in categories mode the contents of the worn/wielded rows
    // keep their own categories in the inventory column instead of sitting indented under the parent.
    // CDF: no indent override is needed, inventory_column::get_entry_indent only adds entry.indent
    // when the column's own hierarchy flag is set (toggle_categorize_contained).
    // CDF: no flat Character::inv (OPENCODE_RULES), the non-worn, non-wielded loose items live in
    // the body storage. DDA's top_items_loc() lists them at the top level; do the same here so the
    // loose inventory keeps its rows (CDF used to list them in the flat carried-group loop).
    if( item *storage = character.get_body_storage() ) {
        inventory_column &loose_column = hierarchy ? own_gear_column : own_inv_column;
        for( item *it : storage->all_items_top() ) {
            add_entry_rec( loose_column, loose_column, it, nullptr, nullptr, nullptr, 0 );
        }
    }
}

void inventory_selector::add_map_items( const tripoint_bub_ms &target )
{
    // CDF: remember the source so toggle_categorize_contained() can rebuild these rows.
    if( std::find( added_squares.begin(), added_squares.end(), target ) == added_squares.end() ) {
        added_squares.push_back( target );
    }
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
    // CDF: remember the source so toggle_categorize_contained() can rebuild these rows.
    if( std::find( added_squares.begin(), added_squares.end(), target ) == added_squares.end() ) {
        added_squares.push_back( target );
    }
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
    // Per DDA inventory_ui.cpp:3471 the action is registered in the selector constructor, so every
    // screen can toggle the view. Kept as a no-op for callers that used to ask for the key (the
    // footer label was always drawn; DDA inventory_ui.cpp:3416-3418).
}

void inventory_selector::toggle_categorize_contained()
{
    // CDF: DDA re-labels the existing entries; CDF's category mode has no contained rows, so the
    // entries are rebuilt from the same sources. Copy the sources before clear_items() forgets them.
    const item *highlighted = nullptr;
    if( has_available_choices() && get_selected().is_item() ) {
        highlighted = get_selected().any_item();
    }
    const bool had_character_items = character_items_added;
    const std::vector<tripoint_bub_ms> squares = added_squares;
    _uimode = _uimode == uimode::hierarchy ? uimode::categories : uimode::hierarchy;
    // Per DDA inventory_ui.cpp:3497-3503: remember the mode for the next opening. CDF: DDA writes it
    // back in the destructor; here the toggle is the only writer of _uimode, so it writes it back.
    if( preset.save_state == nullptr ) {
        inventory_sel_default_state.uimode = _uimode;
    } else {
        preset.save_state->uimode = _uimode;
    }
    clear_items();
    for( inventory_column *col : columns ) {
        // Per DDA inventory_ui.cpp:3783-3822 (_categorize/_uncategorize tails): drop the
        // collation before regrouping the entries.
        col->uncollate();
        col->set_hierarchy( _uimode == uimode::hierarchy );
    }
    if( had_character_items ) {
        add_character_items( u );
    }
    for( const tripoint_bub_ms &pos : squares ) {
        add_map_items( pos );
        add_vehicle_items( pos );
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
    // A-L5 fix: the rows the map points to are gone.
    rect_entry_map.clear();
    is_empty = true;
    character_items_added = false;
    added_squares.clear();
    for( auto &column : columns ) {
        column->clear();
    }
    own_inv_column.clear();
    own_gear_column.clear();
    map_column.clear();
}

namespace
{
/// CDF: DDA uses inventory_column::find_by_location; this searches visible and hidden rows.
auto find_entry_by_item( inventory_column &column, const item *target,
                         bool front_only ) -> inventory_entry * // *NOPAD*
{
    const auto find_in = [&]( std::vector<inventory_entry> &entries ) -> inventory_entry * {
        for( inventory_entry &entry : entries ) {
            // CDF: DDA has no is_selectable gate here; the default path requires it.
            if( !entry.is_item() || ( !front_only && !entry.is_selectable() ) ) {
                continue;
            }
            // CDF: guard, entries can have no locations.
            const bool front_matches = !entry.is_collation_header() &&
                                       !entry.locations.empty() &&
                                       entry.locations.front() == target;
            if( ( !front_only &&
                  std::find( entry.locations.begin(), entry.locations.end(),
                             target ) != entry.locations.end() ) ||
                front_matches ) {
                return &entry;
            }
        }
        return nullptr;
    };
    if( inventory_entry *found = find_in( column.entries ) ) {
        return found;
    }
    return find_in( column.entries_hidden );
}
} // namespace

bool inventory_selector::select( const item *loc )
{
    return select( loc, false, false );
}

// Per DDA inventory_ui.cpp:2408-2460. CDF: item * instead of item_location and no
// inventory_column::find_by_location, so find_entry_by_item() is used.
bool inventory_selector::select( const item *loc, bool hidden, bool front_only )
{
    bool res = false;

    for( size_t i = 0; i < columns.size(); ++i ) {
        inventory_column *elem = columns[i];
        if( !elem->visible() ) {
            continue;
        }
        bool found = false;
        if( hidden ) {
            if( inventory_entry *ent = find_entry_by_item( *elem, loc, true ) ) {
                const item *parent = ent->is_collation_entry() ? ent->collation_meta->tip :
                                     ent->topmost_parent;
                if( parent == nullptr ) {
                    continue;
                }
                inventory_entry *pent = find_entry_by_item( *elem, parent, false );
                // CDF: DDA checks elem here; pent can be null.
                if( pent != nullptr ) {
                    elem->set_collapsed( *pent, false );
                    prepare_layout();
                    found = elem->select( loc, true );
                }
            }
        } else {
            found = elem->select( loc, front_only );
        }
        if( found && !res && elem->activatable() ) {
            set_active_column( i );
            res = true;
        }
    }

    return res;
}

// Per DDA inventory_ui.cpp:447-466. CDF: item * instead of item_location and no
// inventory_column::find_by_location.
item *inventory_selector::get_collation_next() const
{
    inventory_column &col = get_active_column();
    const inventory_entry &ent = col.get_selected();
    if( !ent.is_collated() ) {
        return nullptr;
    }
    // CDF: guard, entries can have no locations.
    if( ent.locations.empty() ) {
        return nullptr;
    }
    if( ent.is_collation_header() ) {
        return ent.locations.front();
    }

    item *const loc_cur = ent.locations.front();
    const auto p_iter = std::ranges::find_if( col.entries, [&ent]( const inventory_entry & entry ) {
        return entry.is_collation_header() && entry.collation_meta == ent.collation_meta;
    } );
    // CDF: DDA dereferences find_by_location unconditionally.
    if( p_iter == col.entries.end() ) {
        return nullptr;
    }
    const std::vector<item *> &locations = p_iter->locations;
    auto iter = std::find( locations.begin(), locations.end(), loc_cur );
    if( iter == locations.end() ) {
        return nullptr;
    }
    if( iter == std::prev( locations.end() ) ) {
        return *std::prev( iter );
    }

    return *std::next( iter );
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
    // Per DDA inventory_ui.cpp:2510-2533.
    const inventory_entry &prev_entry = get_selected();
    const item *prev_selection = prev_entry.is_item() ? prev_entry.any_item() : nullptr;
    const bool front_only = prev_entry.is_collation_entry();
    // CDF: single-column mode, DDA cpp:2521; off by default
    if( ( is_overflown( client_width ) || force_single_column ) && !own_gear_column.empty() ) {
        if( own_inv_column.empty() ) {
            own_inv_column.set_indent_entries_override( own_gear_column.indent_entries() );
        }
        own_gear_column.move_entries_to( own_inv_column );
        own_inv_column.reset_width( {} );
    }
    if( ( is_overflown( client_width ) || force_single_column ) && !map_column.empty() ) {
        if( own_inv_column.empty() ) {
            own_inv_column.set_indent_entries_override( map_column.indent_entries() );
        }
        map_column.move_entries_to( own_inv_column );
        own_inv_column.reset_width( {} );
    }
    if( prev_selection ) {
        select( prev_selection, false, front_only );
    }
}

void inventory_selector::prepare_layout( size_t client_width, size_t client_height )
{
    // A-L5 fix: paging is rebuilt below, so the entry pointers of the last draw may be stale.
    rect_entry_map.clear();
    // This block adds categories and should go before any width evaluations
    // Per DDA inventory_ui.cpp:2538: the first layout starts the cursor on the first row.
    const bool initial = get_active_column().get_selected_index() == static_cast<size_t>( -1 );
    for( auto &elem : columns ) {
        elem->set_height( client_height );
        elem->prepare_paging( filter );
        elem->reset_width( columns );
    }
    // Handle screen overflow
    rearrange_columns( client_width );
    if( initial ) {
        get_active_column().select( 0, scroll_direction::FORWARD );
    }
    // If we have a single column and it occupies more than a half of
    // the available with -> expand it
    auto visible_columns = get_visible_columns();
    if( visible_columns.size() == 1 && are_columns_centered( client_width ) ) {
        visible_columns.front()->set_width( client_width, columns );
    }

    for( auto &elem : columns ) {
        elem->prepare_paging();
    }
    reassign_invlets();

    refresh_active_column();
}

void inventory_selector::reassign_invlets()
{
    // Per DDA inventory_ui.cpp:2565: number keys move the cursor while numpad navigation is on,
    // so map items get no numeric invlets then.
    const bool use_num_invlet = !uistate.numpad_navigation;
    int custom_invlet = use_num_invlet ? '0' : '\0';
    for( auto &elem : columns ) {
        custom_invlet = elem->reassign_custom_invlets( u, custom_invlet,
                        use_num_invlet ? '9' : '\0' );
    }
    assign_invlet_hints();
}

void inventory_selector::prepare_layout()
{
    const auto snap = []( size_t cur_dim, size_t max_dim ) {
        return cur_dim + 2 * max_win_snap_distance >= max_dim ? max_dim : cur_dim;
    };

    const int nc_width = 2 * ( 1 + border );
    const int nc_height = get_header_height() + 1 + 2 * border;

    prepare_layout( TERMX - nc_width, TERMY - nc_height );

    // Per DDA inventory_ui.cpp:2610-2615: a fixed size (the examiner) replaces the automatic one.
    const int win_width  = _fixed_size.x < 0 ? snap( get_layout_width() + nc_width, TERMX )
                           : _fixed_size.x;
    const int win_height = _fixed_size.y < 0
                           ? snap( std::max<int>( get_layout_height() + nc_height, FULL_SCREEN_HEIGHT ), TERMY )
                           : _fixed_size.y;

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
            highlight();
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

std::string inventory_selector::header_numpad_text() const
{
    // Per DDA inventory_ui.cpp:2716-2718.
    return uistate.numpad_navigation ? _( "Numpad ON" ) : std::string();
}

std::string inventory_selector::view_mode_footer_text() const
{
    // Per DDA inventory_ui.cpp:3416-3418.
    return string_format( "< [%s] %s >", ctxt.get_desc( "VIEW_CATEGORY_MODE" ),
                          _( io::enum_to_string( _uimode ) ) );
}

void inventory_selector::draw_header( const catacurses::window &w ) const
{
    trim_and_print( w, point( border + 1, border ), getmaxx( w ) - 2 * ( border + 1 ), c_white, title );
    fold_and_print( w, point( border + 1, border + 1 ), getmaxx( w ) - 2 * ( border + 1 ), c_dark_gray,
                    hint );

    const int bottom = border + get_header_height();
    mvwhline( w, point( border, bottom ), LINE_OXOX, getmaxx( w ) - 2 * border );

    if( display_stats ) {
        size_t y = border;
        for( const std::string &elem : get_stats() ) {
            right_print( w, y++, border + 1, c_dark_gray, elem );
        }
    }
    const std::string numpad = header_numpad_text();
    if( !numpad.empty() ) {
        right_print( w, bottom, border + 1, c_dark_gray, numpad );
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
    point origin = _fixed_origin;
    if( origin.x < 0 || origin.y < 0 ) {
        origin = point( ( TERMX - width ) / 2, ( TERMY - height ) / 2 );
    }
    w_inv = catacurses::newwin( height, width, origin );
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
    apply_filter( str );
}

void inventory_selector::apply_filter( const std::string &str )
{
    rect_entry_map.clear(); // A-L5 fix: the filter rebuilds the visible rows
    filter = str;
    for( const auto elem : columns ) {
        elem->set_filter( filter );
    }
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) {
        current_ui->mark_resize();
    }
}

// Per DDA inventory_ui.cpp:3671-3673.
void inventory_selector::reset_filter()
{
    apply_filter( std::string() );
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

    rect_entry_map.clear();
    for( const auto &elem : columns ) {
        if( &elem == &columns.back() ) {
            x += gap_rounding_error;
        }

        if( !is_active_column( *elem ) ) {
            elem->draw( w, point( x, y ), &rect_entry_map );
        } else {
            active_x = x;
        }

        x += elem->get_width() + gap;
    }

    get_active_column().draw( w, point( active_x, y ), &rect_entry_map );
    if( empty() ) {
        center_print( w, getmaxy( w ) / 2, c_dark_gray, _( "Your inventory is empty." ) );
    }
}

void inventory_selector::draw_frame( const catacurses::window &w ) const
{
    draw_border( w );

    const int y = border + get_header_height();
    // Per DDA inventory_ui.cpp:3376-3385: the junction glyphs use the frame colour.
    wattron( w, BORDER_COLOR );
    mvwhline( w, point( 0, y ), LINE_XXXO, 1 );
    mvwhline( w, point( getmaxx( w ) - border, y ), LINE_XOXX, 1 );
    wattroff( w, BORDER_COLOR );
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

        // Per DDA inventory_ui.cpp:3416-3418: drawn in every selector.
        right_print( w, getmaxy( w ) - border, border + 1, c_light_gray, view_mode_footer_text() );
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
    , _uimode( preset.save_state == nullptr ? inventory_sel_default_state.uimode :
               preset.save_state->uimode )
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
    // Per DDA inventory_ui.cpp:3456-3457: the mouse wheel moves the cursor a page.
    ctxt.register_action( "SCROLL_UP", to_translation( "Move cursor up" ) );
    ctxt.register_action( "SCROLL_DOWN", to_translation( "Move cursor down" ) );
    // UI1: DDA's column actions (inventory_ui.cpp:3460-3461). LEFT/RIGHT are no longer registered.
    ctxt.register_action( "NEXT_COLUMN", to_translation( "Next column" ) );
    ctxt.register_action( "PREV_COLUMN", to_translation( "Previous column" ) );
    ctxt.register_action( "CONFIRM", to_translation( "Confirm your selection" ) );
    ctxt.register_action( "QUIT", to_translation( "Cancel" ) );
    ctxt.register_action( "CATEGORY_SELECTION", to_translation( "Switch category selection mode" ) );
    // Per DDA inventory_ui.cpp:3472. The keybinding row arrives with FM7; without it the action
    // has no default key.
    ctxt.register_action( "TOGGLE_NUMPAD_NAVIGATION" );
    ctxt.register_action( "TOGGLE_FAVORITE", to_translation( "Toggle favorite" ) );
    ctxt.register_action( "HOME", to_translation( "Home" ) );
    ctxt.register_action( "END", to_translation( "End" ) );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "INVENTORY_FILTER" );
    ctxt.register_action( "EXAMINE" );
    ctxt.register_action( "ANY_INPUT" ); // For invlets
    // Per DDA inventory_ui.cpp:3481; registered here so auto-assigned invlets skip its key.
    ctxt.register_action( "ORGANIZE_MENU" );
    // Per DDA inventory_ui.cpp:3471: the view toggle works in every selector, not only common().
    ctxt.register_action( "VIEW_CATEGORY_MODE", to_translation( "Switch view mode" ) );
    ctxt.register_action( "SHOW_HIDE_CONTENTS", to_translation( "Show/hide contents" ) );
    // Per DDA inventory_ui.cpp:3469; the key comes from UI0.
    ctxt.register_action( "EXAMINE_CONTENTS" );
    // Per DDA inventory_ui.cpp:3470-3480
    ctxt.register_action( "SHOW_HIDE_CONTENTS_ALL", to_translation( "Show/hide all contents" ) );
    ctxt.register_action( "RESET_FILTER", to_translation( "Reset filter" ) );
    ctxt.register_action( "TOGGLE_SKIP_UNSELECTABLE",
                          to_translation( "Toggle skipping of unselectable items" ) );

    append_column( own_inv_column );
    append_column( map_column );
    append_column( own_gear_column );

    // CDF: a selector that starts in hierarchy mode must indent and group the nested rows too. DDA
    // takes the indent from the preset; CDF keeps it in the column flag that the toggle sets.
    for( inventory_column *column : columns ) {
        column->set_hierarchy( _uimode == uimode::hierarchy );
    }
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

// CDF: P3-5-3a, DDA inventory_ui.cpp 3517-3525. get_entries( return_item, true ) becomes
// get_all_entries( is_item_entry ) on the columns that allow selecting (as in 5-2b), and the
// return type is size_t.
size_t inventory_selector::item_entry_count() const
{
    const auto is_item_entry = []( const inventory_entry & entry ) {
        return entry.is_item();
    };
    size_t count = 0;
    for( const inventory_column *col : columns ) {
        if( !col->allows_selecting() ) {
            continue;
        }
        count += col->get_all_entries( is_item_entry ).size();
    }

    return count;
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
    const std::string &action = ctxt.handle_input();
    const int ch = ctxt.get_raw_input().get_first_input();
    return process_input( action, ch );
}

// Per DDA inventory_ui.cpp:3551-3583. CDF window helper: window_contains_point_relative is
// written out (the point is relative to w_inv, as get_coordinates_text returns).
inventory_input inventory_selector::process_input( const std::string &action, int ch )
{
    inventory_input res{ action, ch, nullptr, false };

    if( res.action == "SELECT" || res.action == "MOUSE_MOVE" || res.action == "CLICK_AND_DRAG" ) {
        const std::optional<point> o_p = ctxt.get_coordinates_text( w_inv );
        if( o_p ) {
            point p = *o_p;
            if( p.x >= 0 && p.y >= 0 && p.x < getmaxx( w_inv ) && p.y < getmaxy( w_inv ) ) {
                res.entry = find_entry_by_coordinate( p );
                if( res.entry != nullptr && res.entry->is_selectable() ) {
                    res.mouse = true;
                    return res;
                }
                if( res.entry == nullptr && res.action == "SELECT" ) {
                    p.x++;
                    res.entry = find_entry_by_coordinate( p );
                    // a click just left of a pocket row hits its expander icon
                    if( res.entry != nullptr && res.entry->is_selectable() && res.entry->chevron ) {
                        res.action = "SHOW_HIDE_CONTENTS";
                        res.mouse = true;
                        return res;
                    }
                }
            }
        }
    }

    // G22: a key bound to an action must not be stolen by an item invlet (r/R reset the filter,
    // o examines the contents, 4/6 move the column and mark). DDA looks the invlet up for every
    // key; CDF lets the binding win, so only an unbound key (ANY_INPUT) picks by invlet.
    if( res.action == "ANY_INPUT" ) {
        res.entry = find_entry_by_invlet( res.ch );
        if( res.entry != nullptr && !res.entry->is_selectable() ) {
            res.entry = nullptr;
        }
    }
    return res;
}

// Per DDA inventory_ui.cpp:2485-2493.
inventory_entry *inventory_selector::find_entry_by_coordinate( const point &coordinate ) const
{
    for( const inventory_entry_rect &it : rect_entry_map ) {
        if( it.rect.contains( coordinate ) ) {
            return it.entry;
        }
    }
    return nullptr;
}

std::function<bool( item &, item & )> inventory_selector::drag_drop_test_hook;

// Per DDA inventory_ui.cpp:2049-2102. The menu is game_menus::inv::select_pocket_and_store.
bool inventory_selector::drag_drop_item( item *source, item *dest )
{
    if( source == nullptr || dest == nullptr || source == dest ) {
        return false;
    }
    // B-M2 fix: never store a container in a pocket of something that sits inside it.
    if( eventually_contains( *source, *dest ) ) {
        return false;
    }
    if( drag_drop_test_hook ) {
        return drag_drop_test_hook( *source, *dest );
    }
    return game_menus::inv::select_pocket_and_store( u, *source, *dest );
}

// DDA inventory_ui.cpp:3452-3453, 3468-3469 (COORDINATE is not registered: CDF records the mouse
// position for every mouse event).
void inventory_selector::register_mouse_actions()
{
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "CLICK_AND_DRAG" );
    ctxt.register_action( "SELECT" );
}

void inventory_selector::toggle_selected_contents()
{
    // Per DDA inventory_ui.cpp:1340-1342 and set_collapsed 1294-1316: the selected row toggles its
    // own pockets. CDF's old redirect to the holder garment is gone: it collapsed the holder and so
    // hid the pressed row (see hidden_by_collapse). A holder row collapses its carried group by
    // being the pressed row itself.
    if( !has_available_choices() ) {
        return;
    }
    const inventory_entry &sel = get_selected();
    if( !sel.is_item() ) {
        return;
    }
    const bool collapse = !sel.any_item()->is_collapsed();
    for( item *const loc : sel.locations ) {
        for( item_pocket *pocket : loc->get_standard_pockets() ) {
            pocket->settings.set_collapse( collapse );
        }
    }
    for( inventory_column *elem : columns ) {
        elem->invalidate_paging();
    }
}

// Per DDA inventory_ui.cpp:3688-3692.
void inventory_selector::cycle_hide_override_all()
{
    if( hide_override_ignored ) {
        return;
    }
    for( inventory_column *col : columns ) {
        col->cycle_hide_override();
    }
}

// Per DDA inventory_ui.cpp:3744-3750.
void inventory_selector::toggle_skip_unselectable()
{
    skip_unselectable = !skip_unselectable;
    for( inventory_column *col : columns ) {
        col->toggle_skip_unselectable( skip_unselectable );
    }
}

bool inventory_selector::skip_unselectable = true;

void inventory_selector::ignore_hide_override()
{
    hide_override_ignored = true;
    for( inventory_column *col : columns ) {
        // Per DDA inventory_ui.cpp:1877: the selection column always shows its entries, so it
        // keeps its { false } override.
        if( dynamic_cast<selection_column *>( col ) != nullptr ) {
            continue;
        }
        col->hide_entries_override = std::nullopt;
        col->invalidate_paging();
    }
}

// Per DDA inventory_ui.cpp:3636-3639.
bool inventory_selector::handle_column_action( const std::string &action )
{
    if( action == "PREV_COLUMN" ) {
        toggle_active_column( scroll_direction::BACKWARD );
    } else if( action == "NEXT_COLUMN" ) {
        toggle_active_column( scroll_direction::FORWARD );
    } else {
        return false;
    }
    return true;
}

// Per DDA inventory_ui.cpp:4335-4371. CDF: item * instead of item_location; the old marks are
// cleared here (DDA clears them while drawing).
void inventory_selector::highlight()
{
    for( const inventory_column *column : get_all_columns() ) {
        for( inventory_entry *entry : column->get_all_entries() ) {
            entry->highlight_as_parent = false;
            entry->highlight_as_child = false;
        }
    }
    // Per DDA inventory_ui.cpp:3247-3249: "disable" marks nothing.
    if( get_option<std::string>( "INVENTORY_HIGHLIGHT" ) == "disable" ) {
        return;
    }
    if( get_active_column().empty() ) {
        return;
    }
    const inventory_entry &selected = get_active_column().get_selected();
    if( !selected.is_item() ) {
        return;
    }
    const item *parent = selected.any_item()->parent_item();
    for( const inventory_column *column : get_all_columns() ) {
        for( inventory_entry *entry : column->get_all_entries() ) {
            if( !entry->is_item() ) {
                continue;
            }
            // Find the parent of the selected item (it can be in a stack).
            if( parent != nullptr &&
                std::ranges::find( entry->locations, parent ) != entry->locations.end() ) {
                entry->highlight_as_parent = true;
            }
            // Find the contents of the selected item (a stacked container selects several).
            const item *entry_parent = entry->any_item()->parent_item();
            if( entry_parent != nullptr &&
                std::ranges::find( selected.locations, entry_parent ) != selected.locations.end() ) {
                entry->highlight_as_child = true;
            }
        }
    }
}

void inventory_selector::on_input( const inventory_input &input )
{
    if( input.action == "CATEGORY_SELECTION" ) {
        toggle_navigation_mode();
    } else if( input.action == "VIEW_CATEGORY_MODE" ) {
        // Per DDA inventory_ui.cpp:3640: toggles in every selector and stays open.
        toggle_categorize_contained();
    } else if( input.action == "TOGGLE_NUMPAD_NAVIGATION" ) {
        // Per DDA inventory_ui.cpp:3642-3644.
        uistate.numpad_navigation = !uistate.numpad_navigation;
        reassign_invlets();
    } else if( handle_column_action( input.action ) ) {
        // moved the active column
    } else if( input.action == "INVENTORY_FILTER" ) {
        set_filter();
    } else if( input.action == "RESET_FILTER" ) {
        // Per DDA inventory_ui.cpp:3671-3673
        reset_filter();
    } else if( input.action == "TOGGLE_SKIP_UNSELECTABLE" ) {
        toggle_skip_unselectable();
    } else if( input.action == "SHOW_HIDE_CONTENTS" || input.action == "SHOW_HIDE_CONTENTS_ALL" ) {
        if( has_available_choices() ) {
            const inventory_entry &sel = get_selected();
            item *const kept = sel.is_item() ? sel.any_item() : nullptr;
            if( input.action == "SHOW_HIDE_CONTENTS_ALL" ) {
                cycle_hide_override_all();
            } else {
                toggle_selected_contents();
            }
            for( inventory_column *elem : columns ) {
                elem->invalidate_paging();
            }
            refresh_active_column();
            prepare_layout();
            if( kept != nullptr ) {
                select( kept );
            }
        }
    } else if( input.action == "EXAMINE_CONTENTS" ) {
        // Per DDA inventory_ui.cpp:3645-3661
        const inventory_entry &selected = get_active_column().get_selected();
        if( selected ) {
            item *sitem = selected.locations.front();
            inventory_examiner examine_contents( u, sitem );
            examine_contents.add_contained_items( sitem );
            const int examine_result = examine_contents.execute();
            if( examine_result == EXAMINED_CONTENTS_WITH_CHANGES ) {
                // The user changed something while examining, so rebuild paging
                rect_entry_map.clear(); // A-L5 fix: the rows are rebuilt, the next draw refills it
                for( inventory_column *elem : columns ) {
                    elem->invalidate_paging();
                }
            } else if( examine_result == NO_CONTENTS_TO_EXAMINE ) {
                action_examine( sitem );
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
    column.toggle_skip_unselectable( skip_unselectable );

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

// Per DDA inventory_ui.cpp:3938-3963 (pick) and 5326-5350 (unload), shared. The row under the
// mouse is hovered (MOUSE_MOVE), clicked (SELECT) or dragged from (CLICK_AND_DRAG).
inventory_pick_selector::mouse_outcome inventory_pick_selector::handle_mouse_action(
    const std::string &action, inventory_entry &entry )
{
    mouse_outcome res;
    if( !entry.is_item() ) {
        if( action == "SELECT" ) {
            drag_active = false;
            drag_source = nullptr;
        }
        return res;
    }
    if( action == "CLICK_AND_DRAG" ) {
        if( drag_enabled ) {
            drag_active = true;
            drag_source = entry.locations.front();
        }
    } else if( action == "MOUSE_MOVE" ) {
        if( !drag_active ) {
            select( entry.any_item() );
        }
    } else if( action == "SELECT" ) {
        drag_active = false;
        item *const start = drag_source;
        drag_source = nullptr;
        item *const end = entry.locations.front();
        if( start != nullptr && end != nullptr && start != end ) {
            if( drag_drop_item( start, end ) ) {
                clear_items();
                add_character_items( u );
                prepare_layout();
            }
        } else {
            select( entry.any_item() );
            res.picked = entry.any_item();
        }
    } else if( action == "SHOW_HIDE_CONTENTS" ) {
        select( entry.any_item() );
        res.toggle_contents = true;
    }
    return res;
}

void inventory_pick_selector::end_stale_drag( const inventory_input &input )
{
    if( !drag_active ) {
        return;
    }
    const bool drag_event = input.action == "CLICK_AND_DRAG" || input.action == "MOUSE_MOVE";
    const bool row_click = input.mouse && input.entry != nullptr && input.entry->is_item();
    if( !drag_event && !row_click ) {
        drag_active = false;
        drag_source = nullptr;
    }
}

item *inventory_pick_selector::confirmed_item() const
{
    const inventory_entry &selected = get_active_column().get_selected();
    return selected && selected.is_selectable() ? selected.any_item() : nullptr;
}

item *inventory_pick_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();
        end_stale_drag( input );

        if( input.entry != nullptr && input.mouse ) {
            const mouse_outcome outcome = handle_mouse_action( input.action, *input.entry );
            if( outcome.picked != nullptr ) {
                return outcome.picked;
            }
            if( outcome.toggle_contents ) {
                on_input( input );
            }
        } else if( input.entry != nullptr ) {
            if( select( input.entry->any_item() ) ) {
                ui_manager::redraw();
            }
            return input.entry->any_item();
        } else if( input.action == "ORGANIZE_MENU" ) {
            // Per DDA inventory_ui.cpp:3971-3973: every pick selector organizes and closes.
            u.organize_items_menu();
            return nullptr;
        } else if( input.action == "QUIT" ) {
            return nullptr;
        } else if( input.action == "CONFIRM" ) {
            // A-M1 fix: a disabled row (skip off) is not a choice.
            if( item *const confirmed = confirmed_item() ) {
                return confirmed;
            }
        } else {
            switch( handle_action( input.action ) ) {
                case pick_action_result::close_screen:
                    return nullptr;
                case pick_action_result::return_item:
                    return confirmed_item();
                case pick_action_result::stay_open:
                    // The handler did its work and the screen stays open.
                    break;
                case pick_action_result::not_handled:
                    on_input( input );
                    break;
            }
        }

    }
}

// CDF: P3-5-4, DDA inventory_ui.cpp:5302-5377. item * for item_location (D1), a named struct for
// the pair, get_active_column().get_selected() for get_highlighted(). The mouse branches
// (5325-5350) are in inventory_pick_selector::handle_mouse_action (UI6).
unload_selector::unload_selector( player &p, const inventory_selector_preset &preset ) :
    inventory_pick_selector( p, preset )
{
    ignore_hide_override(); // A-L2 fix
    ctxt.register_action( "CONTAIN_MODE" );
    set_hint( hint_string() );
}

auto unload_selector::hint_string() -> std::string
{
    return string_format(
               _( "<color_yellow>%s</color> Confirm  <color_yellow>%s</color> Cancel  <color_yellow>%s</color> Select destination (<color_yellow>%s</color>)" ),
               ctxt.get_desc( "CONFIRM" ), ctxt.get_desc( "QUIT" ), ctxt.get_desc( "CONTAIN_MODE" ),
               uistate.unload_auto_contain ? _( "Auto" ) : _( "Manual" ) );
}

auto unload_selector::execute() -> unload_selection
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    while( true ) {
        ui_manager::redraw();
        const inventory_input input = get_input();
        end_stale_drag( input );

        if( input.entry != nullptr && input.mouse ) {
            const mouse_outcome outcome = handle_mouse_action( input.action, *input.entry );
            if( outcome.picked != nullptr ) {
                // B-L2 fix: a click is a confirm, so it asks about stealing like CONFIRM does.
                if( !avatar_action::check_stealing( get_player_character(), *outcome.picked ) ) {
                    return { nullptr, uistate.unload_auto_contain };
                }
                return { outcome.picked, uistate.unload_auto_contain };
            }
            if( outcome.toggle_contents ) {
                on_input( input );
            }
        } else if( input.entry != nullptr ) {
            if( select( input.entry->any_item() ) ) {
                ui_manager::redraw();
            }
            return { input.entry->any_item(), uistate.unload_auto_contain };
        } else if( input.action == "QUIT" ) {
            return { nullptr, uistate.unload_auto_contain };
        } else if( input.action == "CONFIRM" ) {
            const inventory_entry &highlighted = get_active_column().get_highlighted();
            if( highlighted && highlighted.is_selectable() ) {
                if( !avatar_action::check_stealing( get_player_character(), *highlighted.any_item() ) ) {
                    return { nullptr, uistate.unload_auto_contain };
                }
                return { highlighted.any_item(), uistate.unload_auto_contain };
            }
        } else if( input.action == "CONTAIN_MODE" ) {
            uistate.unload_auto_contain = !uistate.unload_auto_contain;
            set_hint( hint_string() );
        } else {
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
    // Per DDA inventory_ui.cpp:4383-4388. These replace CDF's RIGHT and DROP_NON_FAVORITE here.
    ctxt.register_action( "TOGGLE_ENTRY", to_translation( "Mark/unmark selected item" ) );
    ctxt.register_action( "MARK_WITH_COUNT",
                          to_translation( "Mark a specific amount of selected item" ) );
    ctxt.register_action( "TOGGLE_NON_FAVORITE", to_translation( "Mark/unmark non-favorite items" ) );
    ctxt.register_action( "INCREASE_COUNT" );
    ctxt.register_action( "DECREASE_COUNT" );

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

std::function<std::optional<std::string>( const std::string & )>
inventory_multiselector::query_count_test_hook;
std::function<void( const std::string & )> inventory_multiselector::query_count_error_test_hook;

int inventory_multiselector::query_count( char init, bool /*end_with_toggle*/ )
{
    // Per DDA inventory_ui.cpp:3305-3323: the prompt starts with the typed digit, otherwise empty.
    const std::string init_str = init != 0 ? std::string( 1, init ) : std::string();
    std::string count_str;
    bool confirmed = false;

    if( query_count_test_hook ) {
        const std::optional<std::string> entered = query_count_test_hook( init_str );
        confirmed = entered.has_value();
        if( confirmed ) {
            count_str = *entered;
        }
    } else {
        spopup = std::make_unique<string_input_popup>();
        spopup->max_length( 256 ).identifier( "inventory" ).text( init_str );

        // CDF: DDA also confirms the prompt on the TOGGLE_ENTRY key (3261-3275). CDF's
        // string_input_popup has no public confirm()/add_callback, so only Enter confirms here;
        // see the report for the files a full port would need.
        shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
        if( current_ui ) {
            current_ui->mark_resize();
        }

        do {
            ui_manager::redraw();
            spopup->query_string( /*loop=*/false );
        } while( !spopup->confirmed() && !spopup->canceled() );

        confirmed = spopup->confirmed();
        if( confirmed ) {
            count_str = spopup->text();
        }
        spopup.reset();

        if( current_ui ) {
            current_ui->mark_resize();
        }
    }

    int result = -1;
    if( confirmed ) {
        try {
            result = std::stoi( count_str );
        } catch( const std::invalid_argument & ) {
            // Per DDA inventory_ui.cpp:3315: tell the player instead of silently marking nothing.
            if( query_count_error_test_hook ) {
                query_count_error_test_hook( _( "That is not an integer." ) );
            } else {
                popup( _( "That is not an integer." ) );
            }
            result = -1;
        } catch( const std::out_of_range & ) {
            result = INT_MAX;
        }
    }

    return result;
}

inventory_selector::header_stats inventory_multiselector::get_raw_stats() const
{
    // Per DDA inventory_ui.cpp:4669-4675: without a stats callback the multiselector shows no
    // stats bar (compare and haul menus).
    return {};
}

void inventory_multiselector::set_chosen_count( inventory_entry &entry, size_t count )
{
    if( entry.is_item() ) {
        entry.chosen_count = std::min( count, entry.get_available_count() );
        on_change( entry );
    }
}

void inventory_multiselector::toggle_entry( inventory_entry &entry, size_t count )
{
    set_chosen_count( entry, count );
    end_mark_batch();
}

void inventory_multiselector::end_mark_batch()
{
    if( !allow_select_contained ) {
        deselect_contained_items();
    }
    selection_col->prepare_paging( get_filter() );
    on_toggle();
}

namespace
{
/// True when `it` sits inside `container` at any depth.
auto is_inside_item( const item *it, const item *container ) -> bool
{
    for( const item *parent = it->parent_item(); parent != nullptr; parent = parent->parent_item() ) {
        if( parent == container ) {
            return true;
        }
    }
    return false;
}

/// The items a chosen entry stands for: all its locations, or only the first chosen_count of a
/// stack of separate items.
auto chosen_holders( const inventory_entry &entry ) -> std::vector<const item *>
{
    std::vector<const item *> res;
    const size_t limit = entry.locations.size() > 1 ?
                         std::min( entry.locations.size(), entry.chosen_count ) : entry.locations.size();
    for( size_t i = 0; i < limit; ++i ) {
        res.push_back( entry.locations[i] );
    }
    return res;
}
} // namespace

void inventory_multiselector::deselect_contained_items()
{
    // Per DDA inventory_ui.cpp:4702-4758 (frozen liquid part not ported: CDF has no such rule).
    // The selection column only mirrors the other columns, so it is skipped.
    const auto is_chosen = []( const inventory_entry & entry ) {
        return entry.is_item() && entry.chosen_count > 0;
    };
    std::vector<inventory_entry *> chosen;
    for( inventory_column *col : get_all_columns() ) {
        if( col == selection_col.get() ) {
            continue;
        }
        const std::vector<inventory_entry *> found = col->get_all_entries( is_chosen );
        chosen.insert( chosen.end(), found.begin(), found.end() );
    }
    std::vector<inventory_entry *> to_clear;
    for( inventory_entry *inner : chosen ) {
        bool covered = false;
        for( inventory_entry *outer : chosen ) {
            if( outer == inner ) {
                continue;
            }
            for( const item *holder : chosen_holders( *outer ) ) {
                for( const item *contained : inner->locations ) {
                    if( is_inside_item( contained, holder ) ) {
                        covered = true;
                        break;
                    }
                }
                if( covered ) {
                    break;
                }
            }
            if( covered ) {
                break;
            }
        }
        if( covered ) {
            to_clear.push_back( inner );
        }
    }
    // Clear after the scan so a container and its contents are judged on the same state.
    for( inventory_entry *entry : to_clear ) {
        set_chosen_count( *entry, 0 );
    }
}

void inventory_multiselector::toggle_entries( int &count, const toggle_mode mode )
{
    std::vector<inventory_entry *> selected;
    switch( mode ) {
        case toggle_mode::SELECTED:
            selected = get_active_column().get_all_selected();
            break;
        case toggle_mode::NON_FAVORITE_NON_WORN: {
            const auto filter_to_nonfavorite_and_nonworn = [this]( const inventory_entry & entry ) {
                return entry.is_selectable() &&
                       !entry.any_item()->is_favorite &&
                       !u.is_worn( *entry.any_item() );
            };

            selected = get_active_column().get_entries( filter_to_nonfavorite_and_nonworn );
            break;
        }
    }

    // Entries that can be highlighted but not marked (disabled rows) are left alone.
    selected.erase( std::remove_if( selected.begin(), selected.end(),
    []( const inventory_entry * elem ) {
        return !elem->is_selectable();
    } ), selected.end() );

    if( selected.empty() ) {
        count = 0;
        return;
    }

    // No amount entered, select all
    if( count == 0 ) {
        bool select_nonfav = true;
        bool select_fav = true;
        switch( mode ) {
            case toggle_mode::SELECTED: {
                count = max_chosen_count;

                // Any non favorite item to select?
                select_nonfav = std::any_of( selected.begin(), selected.end(),
                []( const inventory_entry * elem ) {
                    return ( !elem->any_item()->is_favorite ) && elem->chosen_count == 0;
                } );

                // Otherwise, any favorite item to select?
                select_fav = !select_nonfav && std::any_of( selected.begin(), selected.end(),
                []( const inventory_entry * elem ) {
                    return elem->any_item()->is_favorite && elem->chosen_count == 0;
                } );
                break;
            }
            case toggle_mode::NON_FAVORITE_NON_WORN: {
                const bool clear = std::none_of( selected.begin(), selected.end(),
                []( const inventory_entry * elem ) {
                    return elem->chosen_count > 0;
                } );

                if( clear ) {
                    count = max_chosen_count;
                }
                break;
            }
        }

        for( inventory_entry *elem : selected ) {
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
        for( inventory_entry *elem : selected ) {
            set_chosen_count( *elem, count );
        }
    }

    count = 0;
    end_mark_batch();
}

bool inventory_multiselector::is_count_digit( int ch )
{
    return ch >= '0' && ch <= '9';
}

bool inventory_multiselector::handle_mark_action( const std::string &action, int ch )
{
    int count = 0;
    // G19: only an unbound digit (the test/empty action or ANY_INPUT) opens the count prompt.
    // A digit that is bound to an action keeps that action: 4 moves the column, 6 marks the row.
    const bool unbound_digit = ( action.empty() || action == "ANY_INPUT" ) &&
                               !uistate.numpad_navigation && is_count_digit( ch );
    if( unbound_digit ) {
        // Checked first: UI0 also binds "1" to MARK_WITH_COUNT, and the digit must seed the prompt.
        // Per DDA inventory_ui.cpp:4788-4790: with numpad navigation on the digits move the cursor.
        int query_result = query_count( static_cast<char>( ch ), true );
        if( query_result >= 0 ) {
            toggle_entries( query_result, toggle_mode::SELECTED );
        }
    } else if( action == "TOGGLE_NON_FAVORITE" ) {
        toggle_entries( count, toggle_mode::NON_FAVORITE_NON_WORN );
    } else if( action == "MARK_WITH_COUNT" ) {
        // Set count and mark selected with specific key
        int query_result = query_count();
        if( query_result >= 0 ) {
            toggle_entries( query_result, toggle_mode::SELECTED );
        }
    } else if( action == "TOGGLE_ENTRY" ) {
        toggle_entries( count, toggle_mode::SELECTED );
    } else if( action == "INCREASE_COUNT" || action == "DECREASE_COUNT" ) {
        if( !has_available_choices() ) {
            return true;
        }
        inventory_entry &entry = get_active_column().get_highlighted();
        if( entry.is_item() && entry.is_selectable() ) {
            const size_t chosen = entry.chosen_count;
            const size_t max = entry.get_available_count();
            const size_t newcount = action == "INCREASE_COUNT"
                                    ? chosen < max ? chosen + 1 : max
                                    : chosen > 1 ? chosen - 1 : 0;
            toggle_entry( entry, newcount );
        }
    } else if( action == "VIEW_CATEGORY_MODE" ) {
        toggle_categorize_contained();
    } else {
        return false;
    }
    return true;
}

void inventory_multiselector::toggle_categorize_contained()
{
    // Per DDA inventory_ui.cpp:4760-4772. CDF rebuilds the entries when the view changes, so the
    // chosen counts are read first and put back on the new entries by item.
    std::vector<std::pair<const item *, size_t>> chosen;
    for( inventory_column *col : get_all_columns() ) {
        if( col == selection_col.get() ) {
            continue;
        }
        for( const inventory_entry *entry : col->get_all_entries( []( const inventory_entry & e ) {
        return e.is_item() && e.chosen_count > 0;
    } ) ) {
            chosen.emplace_back( entry->locations.front(), entry->chosen_count );
        }
    }
    selection_col->clear();
    inventory_selector::toggle_categorize_contained();
    for( const std::pair<const item *, size_t> &saved : chosen ) {
        for( inventory_column *col : get_all_columns() ) {
            if( col == selection_col.get() ) {
                continue;
            }
            for( inventory_entry *entry : col->get_all_entries( []( const inventory_entry & e ) {
            return e.is_item();
        } ) ) {
                if( std::ranges::find( entry->locations, saved.first ) != entry->locations.end() ) {
                    set_chosen_count( *entry, saved.second );
                }
            }
        }
    }
    end_mark_batch();
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
        } else if( input.action == "TOGGLE_ENTRY" ) {
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
                          ctxt.get_desc( "TOGGLE_ENTRY" ) );
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

    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( ( input.action.empty() || input.action == "ANY_INPUT" ) &&
            !uistate.numpad_navigation && is_count_digit( input.ch ) ) {
            // A-L1 fix: an unbound digit opens the count prompt even when a row has that digit as
            // invlet. G19: a bound digit (4, 6) keeps its action.
            handle_mark_action( input.action, input.ch );
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            set_chosen_count( *input.entry, input.entry->chosen_count == 0 ? max_chosen_count : 0 );
            end_mark_batch();
        } else if( handle_mark_action( input.action, input.ch ) ) {
            // TOGGLE_ENTRY, MARK_WITH_COUNT, TOGGLE_NON_FAVORITE, count keys: see the base class
        } else if( input.action == "CONFIRM" ) {
            if( to_use.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "TOGGLE_ENTRY" ) );
                continue;
            }
            break;
        } else if( input.action == "QUIT" ) {
            return std::vector<iuse_location>();
        } else {
            on_input( input );
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
    // Per DDA inventory_ui.cpp:4669-4675: no callback, no stats rows (header height 1).
    return {};
}

inventory_drop_selector::inventory_drop_selector( player &p,
        const inventory_selector_preset &preset ) :
    inventory_multiselector( p, preset, _( "ITEMS TO DROP" ) )
{
    ignore_hide_override(); // A-L2 fix
#if defined(__ANDROID__)
    // allow user to type a drop number without dismissing virtual keyboard after each keypress
    ctxt.allow_text_entry = true;
#endif
}

inventory_drop_selector::inventory_drop_selector( player &p,
        const inventory_selector_preset &preset, const std::string &selection_column_title ) :
    inventory_multiselector( p, preset, selection_column_title )
{
    ignore_hide_override(); // A-L2 fix
#if defined(__ANDROID__)
    ctxt.allow_text_entry = true;
#endif
}

// Per DDA inventory_ui.cpp:4690-4700. warn_liquid is DDA's liquid-spill warning of the drop
// selector; CDF's drop selector has none, and the preset hides spilt liquid, so it is unused.
inventory_insert_selector::inventory_insert_selector( player &p,
        const inventory_selector_preset &preset, const std::string &selection_column_title,
        bool /* warn_liquid */ ) :
    inventory_drop_selector( p, preset, selection_column_title )
{
}

bool eventually_contains( const item &container, const item &it )
{
    // Per DDA item_location::eventually_contains: walk up from it.
    for( const item *parent = it.parent_item(); parent != nullptr; parent = parent->parent_item() ) {
        if( parent == &container ) {
            return true;
        }
    }
    return false;
}

// CDF: P3-UI3. DDA item_location::get_pocket_constraints_recursive: the limits that the pockets
// holding `it` put on its own pockets. DDA's pocket_constraint also carries the weight limit; the
// CDF one keeps the field unused, so it is applied here.
static auto outer_pocket_constraint( const item &it ) -> pocket_constraint
{
    pocket_constraint ret;
    const item *child = &it;
    for( const item *parent = it.parent_item(); parent != nullptr;
         child = parent, parent = parent->parent_item() ) {
        const item_pocket *pocket = parent->contents.pocket_of( child );
        if( pocket != nullptr ) {
            ret.constrain_by( pocket );
            ret.weight_capacity = std::min( ret.weight_capacity, pocket->weight_capacity() );
        }
    }
    return ret;
}

inventory_selector::header_stats inventory_selector::get_container_header_stats(
    const item &container )
{
    const pocket_constraint container_constraints = outer_pocket_constraint( container );
    std::vector<pocket_with_constraint> pockets;
    units::volume rigid_capacity = 0_ml;
    units::volume flex_capacity = 0_ml;
    units::volume flex_contents = 0_ml;
    for( const item_pocket *p : container.get_container_pockets() ) {
        pocket_with_constraint pwc{ p, pocket_constraint( p ) };
        pwc.constraint.constrain_by( container_constraints );
        pockets.push_back( pwc );

        if( p->rigid() ) {
            rigid_capacity += p->volume_capacity();
        } else {
            flex_capacity += p->volume_capacity();
            flex_contents += p->contents_volume();
        }
    }

    const std::string weight_label_override = _( "Contents Weight:" );
    return get_pocket_summary_header_stats(
               container.get_total_contained_weight(),
               std::min( container.get_total_weight_capacity(), container_constraints.weight_capacity ),
               container.get_contents_volume(),
               rigid_capacity + std::min( container_constraints.remaining_volume + flex_contents, flex_capacity ),
               pockets, true, &weight_label_override, nullptr );
}

inventory_selector::header_stats container_inventory_selector::get_raw_stats() const
{
    return get_container_header_stats( *loc );
}

inventory_selector::header_stats inventory_insert_selector::get_raw_stats() const
{
    const item *holster = preset.get_holster();
    if( holster == nullptr ) {
        return inventory_drop_selector::get_raw_stats();
    }
    units::mass weight = 0_gram;
    units::volume volume = 0_ml;
    // sum marked items; a marked item and its ancestor are never both marked (deselect_contained_items)
    for( const auto &[marked, count] : get_marked() ) {
        if( marked->count_by_charges() ) {
            detached_ptr<item> copy = item::spawn( *marked );
            copy->charges = count;
            weight += copy->weight();
            volume += copy->volume();
        } else {
            weight += marked->weight() * count;
            volume += marked->volume() * count;
        }
    }
    header_stats stats = get_container_header_stats( *holster );
    stats.push_back( build_selection_stats_line( volume, weight ) );
    return stats;
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

void inventory_drop_selector::on_toggle()
{
    // CDF: the drop preview follows every batch of marks right away, including the unmarking of
    // contained items, so the "will be dropped" counts and the order are never stale.
    update_drop_preview();
}

drop_locations inventory_drop_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
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
    while( true ) {
        update_drop_preview();
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( ( input.action.empty() || input.action == "ANY_INPUT" ) &&
            !uistate.numpad_navigation && is_count_digit( input.ch ) ) {
            // A-L1 fix: an unbound digit opens the count prompt even when a row has that digit as
            // invlet. G19: a bound digit (4, 6) keeps its action.
            handle_mark_action( input.action, input.ch );
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            set_chosen_count( *input.entry, input.entry->chosen_count == 0 ? max_chosen_count : 0 );
            end_mark_batch();
        } else if( handle_mark_action( input.action, input.ch ) ) {
            // TOGGLE_ENTRY, MARK_WITH_COUNT, TOGGLE_NON_FAVORITE, count keys: see the base class
        } else if( input.action == "CONFIRM" ) {
            if( dropping.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "TOGGLE_ENTRY" ) );
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
    // Per DDA inventory_ui.cpp:4917-4939: the carried pocket summary and the Selected line.
    // CDF: Q3 keeps the predicted-drop Weight/Volume numbers as extra lines under the summary.
    header_stats stats = inventory_selector::get_raw_stats();

    header_stats predicted = convert_stats_to_header_stats( get_weight_and_volume_stats(
            u.weight_carried_reduced_by( predicted_counts ),
            u.weight_capacity(),
            u.volume_carried_reduced_by( predicted_counts ),
            u.volume_capacity_reduced_by( 0_ml, predicted_counts ) ) );
    stats.insert( stats.end(), predicted.begin(), predicted.end() );

    units::mass weight = 0_gram;
    units::volume volume = 0_ml;
    // sum the marked items; a marked item and its ancestor are never both marked (deselect_contained_items)
    for( const std::pair<const item *const, int> &drop_pair : dropping ) {
        const item *marked = drop_pair.first;
        const int count = drop_pair.second;
        if( marked->count_by_charges() ) {
            detached_ptr<item> copy = item::spawn( *marked );
            copy->charges = count;
            weight += copy->weight();
            volume += copy->volume();
        } else {
            weight += marked->weight() * count;
            volume += marked->volume() * count;
        }
    }
    stats.push_back( build_selection_stats_line( volume, weight ) );
    return stats;
}

inventory_pickup_selector::inventory_pickup_selector( player &p,
        const inventory_selector_preset &preset ) :
    inventory_multiselector( p, preset, _( "ITEMS TO PICKUP" ) )
{
    // Per DDA inventory_ui.cpp:4993-5016.
    ctxt.register_action( "WEAR" );
    ctxt.register_action( "WIELD" );
#if defined(__ANDROID__)
    // allow user to type a drop number without dismissing virtual keyboard after each keypress
    ctxt.allow_text_entry = true;
#endif

    // Per DDA inventory_ui.cpp:5004-5015: a fixed five-item line. An action with no bound key
    // (SHOW_HIDE_CONTENTS_ALL) prints whatever get_desc returns for it.
    set_hint( string_format(
                  _( "<color_yellow>%s</color> Wield  <color_yellow>%s</color> Wear  <color_yellow>%s</color> Expand  <color_yellow>%s</color> All  <color_yellow>%s</color> Examine\n"
                     "Quantity: <color_yellow>%s</color> to mark selected  <color_yellow>%s</color>/<color_yellow>%s</color> to offset  type number then <color_yellow>%s</color> to set" ),
                  ctxt.get_desc( "WIELD" ),
                  ctxt.get_desc( "WEAR" ),
                  ctxt.get_desc( "SHOW_HIDE_CONTENTS" ),
                  ctxt.get_desc( "SHOW_HIDE_CONTENTS_ALL" ),
                  ctxt.get_desc( "EXAMINE" ),
                  ctxt.get_desc( "MARK_WITH_COUNT" ),
                  ctxt.get_desc( "INCREASE_COUNT" ),
                  ctxt.get_desc( "DECREASE_COUNT" ),
                  ctxt.get_desc( "TOGGLE_ENTRY" ) ) );
}

std::vector<inventory_pickup_selector::marked_pickup> inventory_pickup_selector::get_marked_pickups()
const
{
    std::vector<marked_pickup> result;
    for( const inventory_column *col : get_all_columns() ) {
        if( col == selection_col.get() ) {
            continue;
        }
        const std::vector<inventory_entry *> chosen = col->get_all_entries(
        []( const inventory_entry & entry ) {
            return entry.is_item() && entry.chosen_count > 0;
        } );
        for( const inventory_entry *entry_ptr : chosen ) {
            int count = 0;
            int chosen_count = entry_ptr->chosen_count;
            for( size_t i = 0; i < entry_ptr->locations.size() && count < chosen_count &&
                 count < max_chosen_count; ++i ) {
                item *target = entry_ptr->locations[i];
                int needed_count = std::max( 0, chosen_count - count );
                int to_add = std::min( needed_count, target->count() );
                if( to_add > 0 ) {
                    result.push_back( { target, to_add } );
                    count += to_add;
                }
            }
        }
    }
    return result;
}

std::vector<pickup::pick_drop_selection> inventory_pickup_selector::build_selection() const
{
    std::vector<item *> locations;
    std::vector<int> counts;
    for( const marked_pickup &mark : get_marked_pickups() ) {
        locations.push_back( mark.target );
        counts.push_back( mark.count );
    }
    if( locations.empty() ) {
        return std::vector<pickup::pick_drop_selection>();
    }
    return pickup::optimize_pickup( locations, counts );
}

std::string inventory_pickup_selector::no_selection_text() const
{
    return string_format( _( "No items were selected.  Use %s to select them." ),
                          ctxt.get_desc( "TOGGLE_ENTRY" ) );
}

item *inventory_pickup_selector::item_after_in_active_column( const item &it ) const
{
    bool found = false;
    for( const inventory_entry &entry : get_active_column().entries ) {
        if( !entry.is_item() ) {
            continue;
        }
        for( item *loc : entry.locations ) {
            if( found ) {
                return loc;
            }
            if( loc == &it ) {
                found = true;
            }
        }
    }
    return nullptr;
}

void inventory_pickup_selector::remove_used_item( item &it )
{
    item *next = item_after_in_active_column( it );
    remove_item( &it );
    if( next != nullptr ) {
        select( next );
    }
}

bool inventory_pickup_selector::wield_highlighted()
{
    if( !has_available_choices() ) {
        return false;
    }
    const inventory_entry &selected = get_active_column().get_highlighted();
    if( !selected.is_item() ) {
        return false;
    }
    item *it = selected.any_item();
    const ret_val<bool> can = u.can_wield( *it );
    if( !can.success() ) {
        popup_getkey( can.c_str() );
        return false;
    }
    remove_used_item( *it );
    u.wield( *it );
    return true;
}

bool inventory_pickup_selector::wear_highlighted()
{
    if( !has_available_choices() ) {
        return false;
    }
    const inventory_entry &selected = get_active_column().get_highlighted();
    if( !selected.is_item() ) {
        return false;
    }
    item *it = selected.any_item();
    const ret_val<bool> can = u.can_wear( *it );
    if( !can.success() ) {
        popup_getkey( can.c_str() );
        return false;
    }
    remove_used_item( *it );
    u.wear_item( it->detach() );
    return true;
}

std::vector<pickup::pick_drop_selection> inventory_pickup_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();

    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( ( input.action.empty() || input.action == "ANY_INPUT" ) &&
            !uistate.numpad_navigation && is_count_digit( input.ch ) ) {
            // A-L1 fix: an unbound digit opens the count prompt even when a row has that digit as
            // invlet. G19: a bound digit (4, 6) keeps its action.
            handle_mark_action( input.action, input.ch );
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            if( input.entry->chosen_count == 0 ) {
                set_chosen_count( *input.entry, max_chosen_count );
                end_mark_batch();
            }
        } else if( handle_mark_action( input.action, input.ch ) ) {
            // TOGGLE_ENTRY, MARK_WITH_COUNT, TOGGLE_NON_FAVORITE, count keys: see the base class
        } else if( input.action == "CONFIRM" ) {
            // DDA 5028-5065: nothing marked is refused with a popup naming the mark key.
            // CDF keeps optimize_pickup for the result.
            std::vector<pickup::pick_drop_selection> result = build_selection();
            if( result.empty() ) {
                popup_getkey( "%s", no_selection_text() );
                continue;
            }
            return result;
        } else if( input.action == "WIELD" ) {
            // The menu stays open on the next item (DDA reopens it); it ends when nothing is left.
            if( wield_highlighted() ) {
                assign_invlet_hints();
            }
        } else if( input.action == "WEAR" ) {
            if( wear_highlighted() ) {
                assign_invlet_hints();
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
    // Per DDA pickup_selector::get_raw_stats (5164-5186): the usual carried line, then a
    // "Selected" line with the volume and weight of what is marked.
    units::mass weight;
    units::volume volume;
    for( const marked_pickup &mark : get_marked_pickups() ) {
        // WARNING: the ratio keeps volume and weight exact for stacks (base units are 1 ml, 1 mg)
        const double share = static_cast<double>( mark.count ) / mark.target->count();
        weight += mark.target->weight() * share;
        volume += mark.target->volume() * share;
    }

    header_stats stats = inventory_selector::get_raw_stats();
    stats.push_back( build_selection_stats_line( volume, weight ) );
    return stats;
}

// CDF: P3-5-2a, DDA item_location::can_reload_with( ammo, now ) (item_location.cpp 1524-1537),
// which DDA's reload_target_accepts reaches through rt.owner / rt.target. CDF has no
// item_location handle (D1), so the same two guards run on the item, with
// item::parent_item() for DDA's has_parent() + parent_item().
// P3-5-3a: not static any more, declared in inventory_ui.h for the select_ammo preset.
bool reload_loc_can_reload_with( const item &reloadable, const item &ammo, bool now )
{
    if( reloadable.is_gun() && !reloadable.ammo_default() ) {
        return false;
    } else if( reloadable.is_magazine() ) {
        const item *parent = reloadable.parent_item();
        if( parent != nullptr ) {
            if( parent->is_gun() && !parent->ammo_default() ) {
                return false;
            }
        }
    }
    return reloadable.can_reload_with( ammo, now );
}

// CDF: P3-5-2a, DDA inventory_ui.cpp 4028-4100 and 4104-4151. item & / const item & and
// non-owning item pointers in place of item_location (D1): `owner` is an item, the loaded
// magazine is the non-const pocket's own item * (no const_cast, no item_location( owner, mag )),
// and the two can_reload_with calls go through reload_loc_can_reload_with.
std::vector<reload_target> get_possible_reload_targets( item &target )
{
    std::vector<reload_target> opts;

    auto append_owner = [&opts]( item & owner ) {
        // pocket_index is the position in contents (stable across empty
        // wells), not the position in magazines_current().
        const bool multimag = owner.uses_firing_requirements();
        int idx = 0;
        for( item_pocket *p : owner.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                reload_target well;
                well.target = &owner;
                well.owner = &owner;
                well.pocket_index = idx;
                well.ui_well_index = idx;
                well.kind = reload_target::kind::well;
                opts.push_back( well );

                if( item *mag = p->magazine_current() ) {
                    reload_target mag_target;
                    mag_target.target = mag;
                    mag_target.owner = &owner;
                    mag_target.pocket_index = -1;
                    mag_target.ui_well_index = idx;
                    mag_target.kind = reload_target::kind::loaded_mag;
                    opts.push_back( mag_target );
                }
            } else if( multimag && p->is_type( pocket_type::MAGAZINE ) &&
                       p->get_pocket_data() != nullptr &&
                       !p->get_pocket_data()->ammo_restriction.empty() ) {
                // Integral MAGAZINE on a multimag host: surface as its own
                // target so loose ammo reaches it past sibling wells.
                reload_target mag_pocket;
                mag_pocket.target = &owner;
                mag_pocket.owner = &owner;
                mag_pocket.pocket_index = idx;
                mag_pocket.ui_well_index = idx;
                mag_pocket.kind = reload_target::kind::integral_magazine;
                opts.push_back( mag_pocket );
            }
            ++idx;
        }
        // Owner-level fallback for hosts without a pocket-level target
        // (integral mag, watertight container) so loose ammo discovery
        // still has a destination.
        bool has_dest = false;
        for( const reload_target &rt : opts ) {
            if( rt.owner == &owner ) {
                has_dest = true;
                break;
            }
        }
        if( !has_dest ) {
            reload_target self;
            self.target = &owner;
            self.owner = &owner;
            self.pocket_index = -1;
            self.ui_well_index = -1;
            self.kind = reload_target::kind::loaded_mag;
            opts.push_back( self );
        }
    };

    append_owner( target );
    for( item *mod : target.gunmods() ) {
        append_owner( *mod );
    }

    return opts;
}

// Well and integral-magazine entries gate on owner-level (gun ammo type) AND
// pocket-level (item id, fullness) compatibility. Loaded-mag delegates.
static bool reload_target_accepts( const reload_target &rt, const item &ammo )
{
    if( rt.kind == reload_target::kind::well ||
        rt.kind == reload_target::kind::integral_magazine ) {
        if( !reload_loc_can_reload_with( *rt.owner, ammo, true ) ) {
            return false;
        }
        int idx = 0;
        for( const item_pocket *p : rt.owner->get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( idx == rt.pocket_index ) {
                if( rt.kind == reload_target::kind::well ) {
                    return p->is_type( pocket_type::MAGAZINE_WELL ) &&
                           p->can_reload_with( ammo, true );
                }
                return p->is_type( pocket_type::MAGAZINE ) &&
                       p->can_reload_with( ammo, true );
            }
            ++idx;
        }
        return false;
    }
    return reload_loc_can_reload_with( *rt.target, ammo, true );
}

const reload_target *find_matching_reload_target(
    const std::vector<reload_target> &targets, const item &ammo )
{
    for( const reload_target &rt : targets ) {
        if( reload_target_accepts( rt, ammo ) ) {
            return &rt;
        }
    }
    return nullptr;
}

std::vector<const reload_target *> find_all_matching_reload_targets(
    const std::vector<reload_target> &targets, const item &ammo )
{
    std::vector<const reload_target *> out;
    for( const reload_target &rt : targets ) {
        if( reload_target_accepts( rt, ammo ) ) {
            out.push_back( &rt );
        }
    }
    return out;
}

// CDF: P3-5-2b, DDA inventory_ui.cpp 4018-4026. `player &` and `item &` in place of Character &
// and const item_location & (ADV-14 ruling 4, D1). The register_action lines (DDA 4022-4023)
// are P3-5-2c; the ids are in keybindings.json since the same job.
ammo_inventory_selector::ammo_inventory_selector( player &you, item &reload_loc,
        const inventory_selector_preset &preset ) :
    inventory_selector( you, preset ), reload_loc( reload_loc )
{
    ignore_hide_override(); // A-L2 fix
    ctxt.register_action( "INCREASE_COUNT" );
    ctxt.register_action( "DECREASE_COUNT" );

    force_single_column = true;
}

ammo_inventory_selector::~ammo_inventory_selector() = default;

// CDF: P3-NESTAMMO2, DDA inventory_ui.cpp:2337-2358 (_add_map_items). The map walk into the stacks of
// a square or of vehicle cargo, which CDF's add_items lists only at the top level. Rows are made with
// add_shown_entry, as DDA does, one per item (not restacked), in the own inventory column. A liquid
// stays its own row: select_ammo turns it into the container that holds it when it builds the
// option (CDF reload options carry the container, character_functions.cpp:1083-1087).
// Where DDA's walk and CDF's ammo discovery differ, discovery wins, so the list matches what the
// reload can do: nothing is taken out of a magazine, and the magazine loaded in an item is not offered
// (character_functions.cpp:1113-1120, "do not steal"). Without that rule the rounds in the magazine
// of the gun being reloaded would be offered to that magazine.
void ammo_inventory_selector::add_rows_below( inventory_column &column, item &container,
        const std::function<const item_category *()> &category )
{
    if( container.has_flag( flag_NO_UNLOAD ) || container.is_magazine() ) {
        return;
    }
    for( item *child : container.all_items_top() ) {
        if( child == container.magazine_current() ) {
            continue;
        }
        if( preset.is_shown( child ) ) {
            add_shown_entry( column, std::vector<item *>( 1, child ), category(), nullptr, false, 0 );
        }
        add_rows_below( column, *child, category );
    }
}

// CDF: P3-NESTAMMO2, DDA inventory_ui.cpp 2337-2358 (_add_map_items), which runs add_entry_rec on every
// item of a map square and of vehicle cargo. The roots (the items of the stacks add_items listed) are
// not rows here, add_items made those; this adds what is nested below them. The rows sit under the
// square's header like the loose items of that square (the header is made at the first row, so a
// square with nothing nested adds no category).
void ammo_inventory_selector::add_nested_map_items( inventory_column &target_column,
        const std::vector<item *> &roots, const item_category *custom_category )
{
    for( item *root : roots ) {
        const item_category *header = nullptr;
        add_rows_below( target_column, *root, [&]() -> const item_category * {
            if( header == nullptr && custom_category != nullptr ) {
                header = naturalize_category( *custom_category, root->bub_pos() );
            }
            return header;
        } );
    }
}

// CDF: P3-5-2b, DDA inventory_ui.cpp 4153-4242. Items are item pointers (D1): `rt.owner` and
// `match->owner` are the key, `&reload_loc` replaces `reload_loc`, `get_entries( return_item,
// true )` becomes get_all_entries( is_item_entry ) over the columns that allow selecting,
// item_category takes three arguments (the header is its name), and item_reload_option takes the
// host as its parent argument.
// todo: this should happen when the entries are created, but that's a different refactoring
void ammo_inventory_selector::set_all_entries_chosen_count()
{
    const std::vector<reload_target> targets = get_possible_reload_targets( reload_loc );

    // One synthetic category per (owner, ui_well_index) renders as a header
    // between groups. Build once per selector lifetime; clearing would dangle
    // pointers held by inventory_entry::custom_category.
    std::map<std::pair<const item *, int>, const item_category *> cat_map;
    const bool wants_separators = targets.size() > 1;
    if( wants_separators && reload_well_categories.empty() ) {
        int sort_rank = 0;
        for( const reload_target &rt : targets ) {
            const std::pair<const item *, int> key{ rt.owner, rt.ui_well_index };
            if( cat_map.count( key ) ) {
                continue;
            }
            std::string label;
            if( rt.kind == reload_target::kind::well ) {
                const item_pocket *well = nullptr;
                int idx = 0;
                for( const item_pocket *p : rt.owner->get_pockets( []( const item_pocket & ) {
                return true;
            } ) ) {
                    if( idx == rt.pocket_index ) {
                        well = p;
                        break;
                    }
                    ++idx;
                }
                const pocket_data *pd = well ? well->get_pocket_data() : nullptr;
                const std::string display = pd ? pd->pocket_name.translated() : std::string();
                if( !display.empty() ) {
                    label = display;
                } else if( well != nullptr && !well->magazine_default().is_null() ) {
                    label = string_format( _( "%s well" ), item::nname( well->magazine_default() ) );
                } else {
                    label = _( "magazine well" );
                }
            } else if( rt.target ) {
                label = string_format( _( "top up %s" ), rt.target->tname() );
            }
            if( rt.owner != &reload_loc ) {
                label = string_format( _( "%s: %s" ), rt.owner->tname(), label );
            }
            translation header = no_translation( label );
            reload_well_categories.emplace_back(
                item_category_id( "RELOAD_WELL_" + std::to_string( reload_well_categories.size() ) ),
                header, sort_rank++ );
            cat_map[key] = &reload_well_categories.back();
        }
    } else if( wants_separators ) {
        // Subsequent calls: rebuild the map using existing category pointers.
        std::set<std::pair<const item *, int>> seen;
        auto cat_it = reload_well_categories.begin();
        for( const reload_target &rt : targets ) {
            const std::pair<const item *, int> key{ rt.owner, rt.ui_well_index };
            if( !seen.insert( key ).second ) {
                continue;
            }
            if( cat_it == reload_well_categories.end() ) {
                break;
            }
            cat_map[key] = &*cat_it;
            ++cat_it;
        }
    }

    const auto is_item_entry = []( const inventory_entry & candidate ) {
        return candidate.is_item();
    };
    for( inventory_column *col : get_all_columns() ) {
        if( !col->allows_selecting() ) {
            continue;
        }
        for( inventory_entry *entry : col->get_all_entries( is_item_entry ) ) {
            item *it = entry->any_item();
            const reload_target *match = find_matching_reload_target( targets, *it );
            if( match == nullptr ) {
                continue;
            }
            item_reload_option tmp_opt( &u, match->target, &reload_loc, *it, match->pocket_index );
            int count = entry->get_available_count();
            if( it->has_flag( flag_SPEEDLOADER ) || it->has_flag( flag_SPEEDLOADER_CLIP ) ) {
                count = it->ammo_remaining( );
            }
            tmp_opt.qty( count );
            entry->chosen_count = tmp_opt.qty();
            const std::pair<const item *, int> key{ match->owner, match->ui_well_index };
            const auto cit = cat_map.find( key );
            if( cit != cat_map.end() ) {
                entry->set_custom_category( cit->second );
            }
        }
    }
}

// CDF: P3-5-2b, DDA inventory_ui.cpp 4244-4264. Item pointers for item_location (D1), the host
// as the item_reload_option parent, make_entry_cell_cache( preset ) replaced by
// update_entry_cell_cache on every column, and a non-item entry (the dummy that
// get_highlighted() returns when nothing is highlighted) returns early like a non-ammo one.
void ammo_inventory_selector::mod_chosen_count( inventory_entry &entry, int value )
{
    if( !entry.is_item() || !entry.any_item()->is_ammo() ) {
        return;
    }
    const std::vector<reload_target> targets = get_possible_reload_targets( reload_loc );
    const reload_target *match = find_matching_reload_target( targets, *entry.any_item() );
    if( match != nullptr ) {
        item_reload_option tmp_opt( &u, match->target, &reload_loc, *entry.any_item(),
                                    match->pocket_index );
        tmp_opt.qty( entry.chosen_count + value );
        entry.chosen_count = tmp_opt.qty();
    }
    const item *loc = entry.any_item();
    if( std::find( player_adjusted_ammo_locs.begin(), player_adjusted_ammo_locs.end(), loc ) ==
        player_adjusted_ammo_locs.end() ) {
        player_adjusted_ammo_locs.push_back( loc );
    }

    for( inventory_column *col : get_all_columns() ) {
        col->update_entry_cell_cache( entry );
    }
    on_change( entry );
}

// CDF: P3-5-2c, DDA inventory_ui.cpp 4266-4313. Items are item pointers (D1): `chosen` is an
// item *const and the result is drop_location( *chosen, count ), highlight( loc ) is select( loc ).
// Cut: the MOUSE_MOVE branch (DDA 4275-4278) and the SELECT action of 4279 (D5 keyboard only; the
// CDF selector registers neither) and debug_print_timer (4269, no CDF symbol).
drop_location ammo_inventory_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    while( true ) {
        ui_manager::redraw();
        const inventory_input input = get_input();

        if( input.entry != nullptr ) {
            if( input.action == "ANY_INPUT" ) {
                item *const chosen = input.entry->any_item();
                last_player_adjusted = std::find( player_adjusted_ammo_locs.begin(),
                                                  player_adjusted_ammo_locs.end(),
                                                  chosen ) != player_adjusted_ammo_locs.end();
                return drop_location( *chosen, static_cast<int>( input.entry->chosen_count ) );
            } else {
                if( select( input.entry->any_item() ) ) {
                    ui_manager::redraw();
                }
                on_input( input );
            }
        } else if( input.action == "QUIT" ) {
            last_player_adjusted = false;
            return drop_location();
        } else if( input.action == "CONFIRM" ) {
            const inventory_entry &highlighted = get_active_column().get_highlighted();
            if( highlighted && highlighted.is_selectable() ) {
                item *const chosen = highlighted.any_item();
                last_player_adjusted = std::find( player_adjusted_ammo_locs.begin(),
                                                  player_adjusted_ammo_locs.end(),
                                                  chosen ) != player_adjusted_ammo_locs.end();
                return drop_location( *chosen, static_cast<int>( highlighted.chosen_count ) );
            }
        } else if( input.action == "INCREASE_COUNT" ) {
            inventory_entry &highlighted = get_active_column().get_highlighted();
            mod_chosen_count( highlighted, 1 );
        } else if( input.action == "DECREASE_COUNT" ) {
            inventory_entry &highlighted = get_active_column().get_highlighted();
            mod_chosen_count( highlighted, -1 );
        } else {
            on_input( input );
        }
    }
}

// CDF: P3-5-2b, DDA inventory_ui.cpp 1244-1248 (the non-const twin, same body); the
// const getter is get_selected() in CDF.
inventory_entry &inventory_column::get_highlighted()
{
    return const_cast<inventory_entry &>( const_cast<const inventory_column *>
                                          ( this )->get_selected() );
}

// CDF: P3-5-2b, in place of DDA inventory_entry::make_entry_cell_cache( preset ) (DDA
// inventory_ui.cpp 1072): recompute the cached cells of one element of entries.
void inventory_column::update_entry_cell_cache( const inventory_entry &entry )
{
    const auto iter = std::find_if( entries.begin(), entries.end(),
    [&entry]( const inventory_entry & candidate ) {
        return &candidate == &entry;
    } );
    if( iter == entries.end() ) {
        return;
    }
    if( entries_cell_cache.size() < entries.size() ) {
        entries_cell_cache.resize( entries.size() );
    }
    entries_cell_cache[static_cast<size_t>( std::distance( entries.begin(), iter ) )] =
        make_entry_cell_cache( entry );
}
