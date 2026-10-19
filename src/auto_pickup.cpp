#include "auto_pickup.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "avatar.h"
#include "character.h"
#include "color.h"
#include "coordinates.h"
#include "cursesdef.h"
#include "debug.h"
#include "filesystem.h"
#include "flag.h"
#include "fstream_utils.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_stack.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "material.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "pocket_type.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units.h"
#include "world.h"
#include "item_search.h"

using namespace auto_pickup;

static const ammotype ammo_battery( "battery" );

// CDF: DDA declares check_special_rule here (auto_pickup.cpp:53) for the `m:` / `M:` material
// rule prefixes used by rule_list::create_rule and refresh_map_items. Not ported: CDF rules are
// filter based (filter_from_string / wildcard_item_filter) and have no material prefix syntax.

bool auto_pickup::test_pattern_function( const itype &type, std::string filter )
{
    auto func = filter_from_string<itype>( filter, wildcard_itype_filter );
    return func( type );
}

bool auto_pickup::autopickup_item_function( const item &object, std::string filter )
{
    auto func = filter_from_string<item>( filter, wildcard_item_filter );
    return func( object );
}

auto_pickup::player_settings &get_auto_pickup()
{
    static auto_pickup::player_settings single_instance;
    return single_instance;
}

/**
 * The function will return `true` if the user has set all limits to a value of 0.
 * @param pickup_item item to check.
 * @return `true` if given item's weight and volume is within auto pickup user configured limits.
 */
static bool within_autopickup_limits( const item *pickup_item )
{
    // CDF: DDA passes cata::flat_set<flag_id>{ ZERO_WEIGHT, NO_DROP } to has_any_flag; CDF's
    // flat_set has no initializer-list constructor, so the two flags are tested separately.
    bool valid_item = !pickup_item->has_flag( flag_ZERO_WEIGHT ) &&
                      !pickup_item->has_flag( flag_NO_DROP );

    int weight_limit = get_option<int>( "AUTO_PICKUP_WEIGHT_LIMIT" );
    // CDF: DDA calls this option AUTO_PICKUP_VOLUME_LIMIT; CDF keeps its AUTO_PICKUP_VOL_LIMIT name.
    int volume_limit = get_option<int>( "AUTO_PICKUP_VOL_LIMIT" );

    // CDF: no _ml literal used; units::from_milliliter is what the previous pickup.cpp code used.
    bool valid_volume = pickup_item->volume() <= units::from_milliliter( volume_limit * 50 );
    bool valid_weight = pickup_item->weight() <= weight_limit * 50_gram;

    return valid_item && ( volume_limit <= 0 || valid_volume ) && ( weight_limit <= 0 || valid_weight );
}

/**
 * @param pickup_item item to get the auto pickup rule for.
 * @return `rule_state` associated with the given item.
 */
static rule_state get_autopickup_rule( const item *pickup_item )
{
    return get_auto_pickup().check_item( *pickup_item );
}

/**
 * @return `true` if the item is a tool or other container that takes batteries.
 * CDF: item::ammo_capacity( const ammotype & ) does not exist on item; the contents route
 * (item_contents::ammo_capacity, magazine pockets only) is what DDA's item version forwards to.
 * Legacy tools (e.g. flashlight: "ammo": "battery" + "magazines") get a MAGAZINE_WELL pocket,
 * which ammo_capacity() ignores (it counts MAGAZINE pockets only; those are the pockets of
 * magazine items themselves). So a tool with a well that takes battery ammo is powered too.
 */
static bool is_battery_powered( const item &it )
{
    const item_contents &it_contents = it.get_contents();
    return it_contents.ammo_capacity( ammo_battery ) != 0 ||
           ( !it_contents.magazine_compatible().empty() &&
             it.ammo_types( false ).count( ammo_battery ) > 0 );
}

/**
 * @return the items directly inside the given item that auto pickup looks at.
 * CDF: item::all_items_top() covers the standard pockets only (container, magazine, magazine well).
 * DDA corpses hold their contents in a container pocket; CDF corpses keep them in the CORPSE pocket,
 * so for a corpse those items are included.
 */
static std::vector<item *> autopickup_contents( item &what )
{
    std::vector<item *> result = what.all_items_top();
    if( what.is_corpse() ) {
        const std::vector<item *> corpse_items = what.all_items_top( pocket_type::CORPSE );
        result.insert( result.end(), corpse_items.begin(), corpse_items.end() );
    }
    return result;
}

// CDF: DDA's item::is_container() counts every container pocket; CDF's item::is_container()
// ignores `carry` (worn storage) pockets, so a ground backpack was never looked into by
// auto pickup (DDA auto_pickup.cpp uses is_container() at the same places). Auto pickup
// therefore uses this check, which includes carry pockets.
static bool is_autopickup_container( const item &what )
{
    for( const item_pocket &pocket : what.get_contents().get_pockets() ) {
        if( pocket.is_type( pocket_type::CONTAINER ) ) {
            return true;
        }
    }
    return false;
}

/**
 * @return `true` if the item has contents that auto pickup should look into: a container with
 * items, or a corpse with items (CDF: item::is_container() is false for a corpse).
 */
static bool has_autopickup_contents( const item &what )
{
    if( what.is_corpse() ) {
        return !what.is_container_empty();
    }
    return is_autopickup_container( what ) && !what.is_container_empty();
}

/**
 * Drop all items from the given container that match special auto pickup rules.
 * The items will be removed from the container and dropped in the designated location.
 *
 * @param what container to drop items from.
 * @param where location on the map to drop items to.
 */
// CDF: item_location is replaced by a plain item pointer (post-IV handles); DDA copies the item
// and removes the original, CDF moves the detached_ptr out of the container onto the tile.
static void empty_autopickup_target( item *what, const tripoint_bub_ms &where )
{
    bool is_rigid = what->all_pockets_rigid();
    // CDF: all_items_top() returns a copy (std::vector), so removing entries below is safe.
    const std::vector<item *> entries = what->all_items_top();
    for( item *entry : entries ) {
        const rule_state ap_rule = get_autopickup_rule( entry );
        // rigid containers want to keep as much items as possible so drop only blacklisted items
        // non-rigid containers want to drop as much items as possible so keep only whitelisted items
        if( is_rigid ? ap_rule == RULE_BLACKLISTED : ap_rule != RULE_WHITELISTED ) {
            detached_ptr<item> removed = what->get_contents().remove_top( entry );
            // drop the items on the same tile the container is on
            if( removed ) {
                get_map().add_item( where, std::move( removed ) );
            }
        }
    }
}

/**
 * Iterate through every item inside the container to find items that match auto pickup rules.
 * In most cases whitelisted items will be included and blacklisted one will be excluded however
 * there are special cases. Below is an overview of selection rules for container auto pickup.
 *
 * Containers and items will **never** be picked up when:
 *
 * - they are owned by player and `AUTO_PICKUP_OWNED` game option is disabled.
 * - they exceed volume or weight user auto pickup limitations.
 * - they are blacklisted in auto pickup rules.
 *
 * Containers will **always** be picked up when:
 *
 * - the container is sealed.
 * - there is any liquids stored in container pockets.
 * - all items inside the container are marked for pickup.
 *
 * Containers will **never** be picked up when:
 *
 * - only batteries were selected and the container is battery powered.
 * - the container is a non-whitelisted corpse.
 *
 * Items will **NOT** be picked up when:
 *
 * - the parent container is non-rigid and the item is not whitelisted.
 *
 * @param from item to search for items to auto pickup from.
 * @param where map tile the top level container is on (empty_autopickup_target drops there).
 * CDF: DDA derives it with from.pos_bub( here ); without item_location it is passed down.
 * @return sequence of items to auto pickup from given container.
 */
static std::vector<item *> get_autopickup_items( item *from, const tripoint_bub_ms &where )
{
    item *container_item = from;
    // items sealed in containers should never be unsealed by auto pickup
    bool force_pick_container = container_item->any_pockets_sealed();
    bool pick_all_items = true;

    std::vector<item *> result;
    // do not auto pickup owned containers or items
    if( !get_option<bool>( "AUTO_PICKUP_OWNED" ) &&
        container_item->is_owned_by( get_player_character() ) ) {
        return result;
    }
    // CDF: all_items_top() returns std::vector<item *> (DDA: std::list<item *>), by value
    const std::vector<item *> contents = autopickup_contents( *container_item );
    result.reserve( contents.size() );

    bool any_whitelisted = false;
    for( item *item_entry : contents ) {
        // CDF: DDA skips items reserved by a craft here (craft_reservation::contains_reserved,
        // auto_pickup.cpp:164) before empty_autopickup_target turns a container out. Not ported:
        // CDF has no craft_reservation subsystem.
        if( !within_autopickup_limits( item_entry ) ) {
            pick_all_items = false;
            continue;
        }
        const rule_state pickup_state = get_autopickup_rule( item_entry );
        if( pickup_state == RULE_WHITELISTED ) {
            any_whitelisted = true;
            if( !force_pick_container ) {
                // CDF: is_autopickup_container also covers carry-only containers (backpack)
                if( is_autopickup_container( *item_entry ) ) {
                    // whitelisted containers should exclude contained blacklisted items
                    empty_autopickup_target( item_entry, where );
                } else if( item_entry->made_of_from_type( LIQUID ) ) {
                    // liquid items should never be picked up without container
                    force_pick_container = true;
                    break;
                }
                // pick up the whitelisted item
                result.push_back( item_entry );
            }
        } else if( !force_pick_container && has_autopickup_contents( *item_entry ) ) {
            // get pickup list from nested item container
            std::vector<item *> result_nested = get_autopickup_items( item_entry, where );

            // container with content was NOT marked for pickup
            if( result_nested.size() != 1 || result_nested[0] != item_entry ) {
                pick_all_items = false;
            }
            result.reserve( result_nested.size() + result.size() );
            result.insert( result.end(), result_nested.begin(), result_nested.end() );
        } else {
            // skip not whitelisted items that are not containers with items
            pick_all_items = false;
        }
    }
    // all items in container were approved for pickup
    if( !contents.empty() && ( pick_all_items || force_pick_container ) ) {
        // only auto pickup corpses if they are whitelisted
        // blacklisted containers should still have their contents picked up but themselves should be excluded.
        // If all items inside blacklisted container match then just pickup the items without the container
        rule_state pickup_state = get_autopickup_rule( container_item );
        if( pickup_state == RULE_BLACKLISTED || !any_whitelisted ||
            ( container_item->is_corpse() && pickup_state != RULE_WHITELISTED ) ) {
            return result;
        }
        bool all_batteries = true;
        const bool powered_container = is_battery_powered( *container_item );
        if( powered_container ) {
            // when dealing with battery powered tools there should only be one pocket
            // and one battery inside but this could change in future so account for that here
            // CDF: item::is_battery() is `!!type->battery` (energy-storage items) and is false for
            // MAGAZINE items such as light_battery_cell, which is what tools actually hold.
            all_batteries = std::all_of( result.begin(), result.end(),
            []( const item * il ) {
                return il->is_battery() ||
                       ( il->is_magazine() && il->ammo_types( false ).count( ammo_battery ) > 0 );
            } );
        }
        bool batteries_from_tool = powered_container && all_batteries;
        // make sure container is allowed to be picked up
        // when picking up batteries from powered containers don't pick container
        if( within_autopickup_limits( container_item ) && !batteries_from_tool ) {
            result.clear();
            result.push_back( from );
        } else if( force_pick_container ) {
            // when force picking never pick individual items
            result.clear();
        }
    }
    return result;
}

/**
 * Select which items on the map tile should be auto-picked up.
 * The return value represents the selected items on the map.
 *
 * @param from stack of item entries on a map tile.
 * @param location where the stack of items is located on the map.
 * @return sequence of selected items on the map.
 */
// CDF: returns item pointers instead of drop_locations (item_location + count). DDA's count is
// always 0 (TODO there), i.e. the whole item, which pointers express as is. DDA builds a
// map_cursor for the item_locations; not needed here.
std::vector<item *> auto_pickup::select_items(
    const std::vector<item_stack::iterator> &from, const tripoint_bub_ms &location )
{
    bool picking_up_itype_id = true;
    itype_id picking_up_last_itype_id = itype_id::NULL_ID();
    std::vector<item *> result;

    // CDF: location_vector iterators are invalidated when empty_autopickup_target adds the
    // emptied items to the same tile (DDA's list-backed stacks are not), so only the stable
    // item pointers are kept from here on.
    std::vector<item *> entries;
    entries.reserve( from.size() );
    for( const item_stack::iterator &stack : from ) {
        entries.push_back( *stack );
    }

    // iterate over all item stacks found in location
    for( item *item_entry : entries ) {
        if( item_entry->typeId() != picking_up_last_itype_id ) {
            picking_up_itype_id = false;
            picking_up_last_itype_id = item_entry->typeId();
        } else if( !picking_up_itype_id ) {
            // Don't bother checking if we didn't pick up the last item that had this itype_id
            continue;
        }

        // do not auto pickup owned containers or items
        if( !get_option<bool>( "AUTO_PICKUP_OWNED" ) &&
            item_entry->is_owned_by( get_player_character() ) ) {
            continue;
        }
        // do not auto pickup spilt liquids
        if( item_entry->made_of( LIQUID ) ) {
            continue;
        }
        // CDF: DDA skips craft-reserved items here (craft_reservation::contains_reserved,
        // auto_pickup.cpp:277) before empty_autopickup_target turns a container out. Not ported:
        // CDF has no craft_reservation subsystem.
        rule_state pickup_state = get_autopickup_rule( item_entry );
        // CDF: has_autopickup_contents also covers a corpse (DDA: corpses are containers)
        bool is_container = has_autopickup_contents( *item_entry );

        // before checking contents check if item is on pickup list
        if( pickup_state == RULE_WHITELISTED ) {
            // CDF: is_autopickup_container also covers carry-only containers (backpack)
            if( is_autopickup_container( *item_entry ) ) {
                empty_autopickup_target( item_entry, location );
            }
            // skip if the container is still above the limit after emptying it
            if( !within_autopickup_limits( item_entry ) ) {
                continue;
            }
            // TODO (DDA): factor in auto pickup max_quantity here
            picking_up_itype_id = true;
            result.push_back( item_entry );
        } else if( is_container || is_battery_powered( *item_entry ) ) {
            // CDF: is_battery_powered also covers tools with a magazine well, see there
            picking_up_itype_id = true; // Always check containers or tools with batteries
            for( item *add_item : get_autopickup_items( item_entry, location ) ) {
                // TODO (DDA): factor in auto pickup max_quantity here
                result.push_back( add_item );
            }
        }
    }
    return result;
}

void user_interface::show()
{
    if( tabs.empty() ) {
        return;
    }

    const int iHeaderHeight = 4;
    int iContentHeight = 0;
    const int iTotalCols = 2;

    catacurses::window w_border;
    catacurses::window w_header;
    catacurses::window w;

    ui_adaptor ui;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        iContentHeight = FULL_SCREEN_HEIGHT - 2 - iHeaderHeight;
        const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                             TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );

        w_border = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                       iOffset );
        w_header = catacurses::newwin( iHeaderHeight, FULL_SCREEN_WIDTH - 2,
                                       iOffset + point_south_east );
        w = catacurses::newwin( iContentHeight, FULL_SCREEN_WIDTH - 2,
                                iOffset + point( 1, iHeaderHeight + 1 ) );

        ui.position_from_window( w_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    size_t iTab = 0;
    int iLine = 0;
    int iColumn = 1;
    int iStartPos = 0;

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // Redraw the border
        draw_border( w_border, BORDER_COLOR, title );
        // |-
        mvwputch( w_border, point( 0, 3 ), c_light_gray, LINE_XXXO );
        // -|
        mvwputch( w_border, point( 79, 3 ), c_light_gray, LINE_XOXX );
        // _|_
        mvwputch( w_border, point( 5, FULL_SCREEN_HEIGHT - 1 ), c_light_gray, LINE_XXOX );
        mvwputch( w_border, point( 51, FULL_SCREEN_HEIGHT - 1 ), c_light_gray, LINE_XXOX );
        mvwputch( w_border, point( 61, FULL_SCREEN_HEIGHT - 1 ), c_light_gray, LINE_XXOX );
        wnoutrefresh( w_border );

        // Redraw the header
        int tmpx = 0;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<A>dd" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<R>emove" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<C>opy" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<M>ove" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<E>nable" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<D>isable" ) ) + 2;
        if( !g->u.name.empty() ) {
            shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<T>est" ) );
        }
        tmpx = 0;
        tmpx += shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green,
                                _( "<+-> Move up/down" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green,
                                _( "<Enter>-Edit" ) ) + 2;
        shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green, _( "<Tab>-Switch Page" ) );

        for( int i = 0; i < 78; i++ ) {
            if( i == 4 || i == 50 || i == 60 ) {
                mvwputch( w_header, point( i, 2 ), c_light_gray, LINE_OXXX );
                mvwputch( w_header, point( i, 3 ), c_light_gray, LINE_XOXO );
            } else {
                // Draw line under header
                mvwputch( w_header, point( i, 2 ), c_light_gray, LINE_OXOX );
            }
        }
        mvwprintz( w_header, point( 1, 3 ), c_white, "#" );
        mvwprintz( w_header, point( 8, 3 ), c_white, _( "Rules" ) );
        mvwprintz( w_header, point( 52, 3 ), c_white, _( "I/E" ) );

        rule_list &cur_rules = tabs[iTab].new_rules;
        int locx = 17;
        for( size_t i = 0; i < tabs.size(); i++ ) {
            const auto color = iTab == i ? hilite( c_white ) : c_white;
            locx += shortcut_print( w_header, point( locx, 2 ), c_white, color, tabs[i].title ) + 1;
        }

        locx = 55;
        mvwprintz( w_header, point( locx, 0 ), c_white, _( "Auto pickup enabled:" ) );
        locx += shortcut_print( w_header, point( locx, 1 ),
                                get_option<bool>( "AUTO_PICKUP" ) ? c_light_green : c_light_red, c_white,
                                get_option<bool>( "AUTO_PICKUP" ) ? _( "True" ) : _( "False" ) );
        locx += shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, "  " );
        locx += shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, _( "<S>witch" ) );
        shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, "  " );

        wnoutrefresh( w_header );

        // Clear the lines
        for( int i = 0; i < iContentHeight; i++ ) {
            for( int j = 0; j < 79; j++ ) {
                if( j == 4 || j == 50 || j == 60 ) {
                    mvwputch( w, point( j, i ), c_light_gray, LINE_XOXO );
                } else {
                    mvwputch( w, point( j, i ), c_black, ' ' );
                }
            }
        }

        draw_scrollbar( w_border, iLine, iContentHeight, cur_rules.size(), point( 0, 5 ) );
        wnoutrefresh( w_border );

        calcStartPos( iStartPos, iLine, iContentHeight, cur_rules.size() );

        // display auto pickup
        for( int i = iStartPos; i < static_cast<int>( cur_rules.size() ); i++ ) {
            if( i >= iStartPos &&
                i < iStartPos + ( iContentHeight > static_cast<int>( cur_rules.size() ) ?
                                  static_cast<int>( cur_rules.size() ) : iContentHeight ) ) {
                nc_color cLineColor = cur_rules[i].bActive ? c_white : c_light_gray;

                mvwprintz( w, point( 1, i - iStartPos ), cLineColor, "%d", i + 1 );
                mvwprintz( w, point( 5, i - iStartPos ), cLineColor, "" );

                if( iLine == i ) {
                    wprintz( w, c_yellow, ">> " );
                } else {
                    wprintz( w, c_yellow, "   " );
                }

                wprintz( w, iLine == i && iColumn == 1 ? hilite( cLineColor ) : cLineColor, "%s",
                         cur_rules[i].sRule.empty() ? _( "<empty rule>" ) : cur_rules[i].sRule );

                mvwprintz( w, point( 52, i - iStartPos ), iLine == i && iColumn == 2 ?
                           hilite( cLineColor ) : cLineColor, "%s",
                           cur_rules[i].bExclude ? _( "Exclude" ) :  _( "Include" ) );
            }
        }

        wnoutrefresh( w );
    } );

    bStuffChanged = false;
    input_context ctxt( "AUTO_PICKUP" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    if( tabs.size() > 1 ) {
        ctxt.register_action( "NEXT_TAB" );
        ctxt.register_action( "PREV_TAB" );
    }
    ctxt.register_action( "ADD_RULE" );
    ctxt.register_action( "REMOVE_RULE" );
    ctxt.register_action( "COPY_RULE" );
    ctxt.register_action( "ENABLE_RULE" );
    ctxt.register_action( "DISABLE_RULE" );
    ctxt.register_action( "MOVE_RULE_UP" );
    ctxt.register_action( "MOVE_RULE_DOWN" );
    ctxt.register_action( "TEST_RULE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    const bool allow_swapping = tabs.size() == 2;
    if( allow_swapping ) {
        ctxt.register_action( "SWAP_RULE_GLOBAL_CHAR" );
    }

    if( is_autopickup ) {
        ctxt.register_action( "SWITCH_AUTO_PICKUP_OPTION" );
    }

    while( true ) {
        rule_list &cur_rules = tabs[iTab].new_rules;

        const bool currentPageNonEmpty = !cur_rules.empty();

        ui_manager::redraw();

        const std::string action = ctxt.handle_input();

        if( action == "NEXT_TAB" ) {
            iTab++;
            if( iTab >= tabs.size() ) {
                iTab = 0;
            }
            iLine = 0;
        } else if( action == "PREV_TAB" ) {
            if( iTab > 0 ) {
                iTab--;
            } else {
                iTab = tabs.size() - 1;
            }
            iLine = 0;
        } else if( action == "QUIT" ) {
            break;
        } else if( action == "DOWN" ) {
            iLine++;
            iColumn = 1;
            if( iLine >= static_cast<int>( cur_rules.size() ) ) {
                iLine = 0;
            }
        } else if( action == "UP" ) {
            iLine--;
            iColumn = 1;
            if( iLine < 0 ) {
                iLine = cur_rules.size() - 1;
            }
        } else if( action == "REMOVE_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules.erase( cur_rules.begin() + iLine );
            if( iLine > static_cast<int>( cur_rules.size() ) - 1 ) {
                iLine--;
            }
            if( iLine < 0 ) {
                iLine = 0;
            }
        } else if( action == "COPY_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules.push_back( cur_rules[iLine] );
            iLine = cur_rules.size() - 1;
        } else if( allow_swapping && action == "SWAP_RULE_GLOBAL_CHAR" && currentPageNonEmpty ) {
            const size_t other_iTab = ( iTab + 1 ) % 2;
            rule_list &other_rules = tabs[other_iTab].new_rules;
            bStuffChanged = true;
            //copy over
            other_rules.push_back( cur_rules[iLine] );
            //remove old
            cur_rules.erase( cur_rules.begin() + iLine );
            iTab = other_iTab;
            iLine = other_rules.size() - 1;
        } else if( action == "ADD_RULE" || ( action == "CONFIRM" && currentPageNonEmpty ) ) {
            const int old_iLine = iLine;
            if( action == "ADD_RULE" ) {
                cur_rules.push_back( rule( "", true, false ) );
                iLine = cur_rules.size() - 1;
            }
            ui_manager::redraw();

            if( iColumn == 1 || action == "ADD_RULE" ) {
                ui_adaptor help_ui;
                catacurses::window w_help;
                const auto init_help_window = [&]( ui_adaptor & help_ui ) {
                    const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                                         TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );
                    w_help = catacurses::newwin( FULL_SCREEN_HEIGHT * ( 2.0 / 3 ) + 2,
                                                 FULL_SCREEN_WIDTH * 3 / 4,
                                                 iOffset + point( 19 / 2, 7 + FULL_SCREEN_HEIGHT / 2 / 2 ) );
                    help_ui.position_from_window( w_help );
                };
                init_help_window( help_ui );
                help_ui.on_screen_resize( init_help_window );

                help_ui.on_redraw( [&]( const ui_adaptor & ) {
                    // NOLINTNEXTLINE(cata-use-named-point-constants)
                    fold_and_print( w_help, point( 1, 1 ), 999, c_white,
                                    _(
                                        "* is used as a Wildcard.  A few Examples:\n"
                                        "\n"
                                        "wooden arrow    matches the itemname exactly\n"
                                        "wooden ar*      matches items beginning with wood ar\n"
                                        "*rrow           matches items ending with rrow\n"
                                        "*avy fle*fi*arrow     multiple * are allowed\n"
                                        "heAVY*woOD*arrOW      case insensitive search\n"
                                        "\n"
                                        "Pickup using:\n"
                                        "c:food          matches item in category food\n"
                                        "m:kevlar        matches items made of Kevlar\n"
                                        "M:copper        matches items made purely of copper\n"
                                        "q:drilling      matches items with drilling qualites\n"
                                        "k:fabrication   matches books that teach fabrication\n"
                                        "b:c:food;*meat* matches conditions c:food and *meat*" )
                                  );

                    draw_border( w_help );
                    wnoutrefresh( w_help );
                } );
                const std::string r = string_input_popup()
                                      .title( _( "Pickup Rule:" ) )
                                      .width( 30 )
                                      .text( cur_rules[iLine].sRule )
                                      .query_string();
                // If r is empty, then either (1) The player ESC'ed from the window (changed their mind), or
                // (2) Explicitly entered an empty rule- which isn't allowed since "*" should be used
                // to include/exclude everything
                if( !r.empty() ) {
                    cur_rules[iLine].sRule = wildcard_trim_rule( r );
                    bStuffChanged = true;
                } else if( action == "ADD_RULE" ) {
                    cur_rules.pop_back();
                    iLine = old_iLine;
                }
            } else if( iColumn == 2 ) {
                bStuffChanged = true;
                cur_rules[iLine].bExclude = !cur_rules[iLine].bExclude;
            }
        } else if( action == "ENABLE_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules[iLine].bActive = true;
        } else if( action == "DISABLE_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules[iLine].bActive = false;
        } else if( action == "LEFT" ) {
            iColumn--;
            if( iColumn < 1 ) {
                iColumn = iTotalCols;
            }
        } else if( action == "RIGHT" ) {
            iColumn++;
            if( iColumn > iTotalCols ) {
                iColumn = 1;
            }
        } else if( action == "MOVE_RULE_UP" && currentPageNonEmpty ) {
            bStuffChanged = true;
            if( iLine < static_cast<int>( cur_rules.size() ) - 1 ) {
                std::swap( cur_rules[iLine], cur_rules[iLine + 1] );
                iLine++;
                iColumn = 1;
            }
        } else if( action == "MOVE_RULE_DOWN" && currentPageNonEmpty ) {
            bStuffChanged = true;
            if( iLine > 0 ) {
                std::swap( cur_rules[iLine], cur_rules[iLine - 1] );
                iLine--;
                iColumn = 1;
            }
        } else if( action == "TEST_RULE" && currentPageNonEmpty && !g->u.name.empty() ) {
            cur_rules[iLine].test_pattern();
        } else if( action == "SWITCH_AUTO_PICKUP_OPTION" ) {
            // TODO: Now that NPCs use this function, it could be used for them too
            get_options().get_option( "AUTO_PICKUP" ).setNext();
            get_options().save();
        }
    }

    if( !bStuffChanged ) {
        return;
    }

    if( !query_yn( _( "Save changes?" ) ) ) {
        return;
    }

    for( tab &t : tabs ) {
        t.rules.get() = t.new_rules;
    }
}

void player_settings::show()
{
    user_interface ui;

    ui.title = _( " AUTO PICKUP MANAGER " );
    ui.tabs.emplace_back( _( "[<Global>]" ), global_rules );
    if( !g->u.name.empty() ) {
        ui.tabs.emplace_back( _( "[<Character>]" ), character_rules );
    }
    ui.is_autopickup = true;

    ui.show();

    if( !ui.bStuffChanged ) {
        return;
    }

    save_global();
    if( !g->u.name.empty() ) {
        save_character();
    }
    invalidate();
}

void rule::test_pattern() const
{
    std::vector<std::string> vMatchingItems;

    if( sRule.empty() ) {
        return;
    }

    //Loop through all itemfactory items
    //APU now ignores prefixes, bottled items and suffix combinations still not generated
    for( const itype *e : item_controller->all() ) {
        const std::string sItemName = e->nname( 1 );
        if( !test_pattern_function( *e, sRule ) && !wildcard_match( sItemName, sRule ) ) {
            continue;
        }

        vMatchingItems.push_back( sItemName );
    }

    int iStartPos = 0;
    int iContentHeight = 0;
    int iContentWidth = 0;

    catacurses::window w_test_rule_border;
    catacurses::window w_test_rule_content;

    ui_adaptor ui;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        const point iOffset( 15 + ( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 ),
                             5 + ( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 :
                                   0 ) );
        iContentHeight = FULL_SCREEN_HEIGHT - 8;
        iContentWidth = FULL_SCREEN_WIDTH - 30;

        w_test_rule_border = catacurses::newwin( iContentHeight + 2, iContentWidth,
                             iOffset );
        w_test_rule_content = catacurses::newwin( iContentHeight,
                              iContentWidth - 2,
                              iOffset + point_south_east );

        ui.position_from_window( w_test_rule_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    int nmatch = vMatchingItems.size();
    const std::string buf = string_format( vgettext( "%1$d item matches: %2$s",
                                           "%1$d items match: %2$s",
                                           nmatch ), nmatch, sRule );

    int iLine = 0;

    input_context ctxt( "AUTO_PICKUP_TEST" );
    ctxt.register_updown();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        draw_border( w_test_rule_border, BORDER_COLOR, buf, hilite( c_white ) );
        center_print( w_test_rule_border, iContentHeight + 1, red_background( c_white ),
                      _( "Won't display content or suffix matches" ) );
        wnoutrefresh( w_test_rule_border );

        // Clear the lines
        for( int i = 0; i < iContentHeight; i++ ) {
            for( int j = 0; j < 79; j++ ) {
                mvwputch( w_test_rule_content, point( j, i ), c_black, ' ' );
            }
        }

        calcStartPos( iStartPos, iLine, iContentHeight, vMatchingItems.size() );

        // display auto pickup
        for( int i = iStartPos; i < static_cast<int>( vMatchingItems.size() ); i++ ) {
            if( i >= iStartPos &&
                i < iStartPos + ( iContentHeight > static_cast<int>( vMatchingItems.size() ) ?
                                  static_cast<int>( vMatchingItems.size() ) : iContentHeight ) ) {
                nc_color cLineColor = c_white;

                mvwprintz( w_test_rule_content, point( 0, i - iStartPos ), cLineColor, "%d", i + 1 );
                mvwprintz( w_test_rule_content, point( 4, i - iStartPos ), cLineColor, "" );

                if( iLine == i ) {
                    wprintz( w_test_rule_content, c_yellow, ">> " );
                } else {
                    wprintz( w_test_rule_content, c_yellow, "   " );
                }

                wprintz( w_test_rule_content, iLine == i ? hilite( cLineColor ) : cLineColor, vMatchingItems[i] );
            }
        }

        wnoutrefresh( w_test_rule_content );
    } );

    while( true ) {
        ui_manager::redraw();

        const std::string action = ctxt.handle_input();
        if( action == "DOWN" ) {
            iLine++;
            if( iLine >= static_cast<int>( vMatchingItems.size() ) ) {
                iLine = 0;
            }
        } else if( action == "UP" ) {
            iLine--;
            if( iLine < 0 ) {
                iLine = vMatchingItems.size() - 1;
            }
        } else if( action == "QUIT" ) {
            break;
        }
    }
}

bool player_settings::has_rule( const item *it )
{
    const std::string &name = it->tname( 1 );
    for( auto &elem : character_rules ) {
        if( name.length() == elem.sRule.length() && ci_find_substr( name, elem.sRule ) != -1 ) {
            return true;
        }
    }
    return false;
}

void player_settings::add_rule( const item *it, bool include )
{
    // CDF: DDA add_rule( const item *, bool include ); include == false adds a blacklist rule.
    // The default include = true (header) keeps the one-argument behaviour of existing callers.
    character_rules.push_back( rule( it->tname( 1, false ), true, !include ) );
    create_rule( it );

    if( !get_option<bool>( "AUTO_PICKUP" ) &&
        query_yn( _( "Autopickup is not enabled in the options.  Enable it now?" ) ) ) {
        get_options().get_option( "AUTO_PICKUP" ).setNext();
        get_options().save();
    }
}

void player_settings::remove_rule( const item *it )
{
    const std::string sRule = it->tname( 1, false );
    for( rule_list::iterator candidate = character_rules.begin();
         candidate != character_rules.end(); ++candidate ) {
        if( sRule.length() == candidate->sRule.length() &&
            ci_find_substr( sRule, candidate->sRule ) != -1 ) {
            character_rules.erase( candidate );
            invalidate();
            break;
        }
    }
}

bool player_settings::empty() const
{
    return global_rules.empty() && character_rules.empty();
}

void npc_settings::create_rule( const std::string &to_match )
{
    rules.create_rule( map_items, to_match );
}

void rule_list::create_rule( cache &map_items, const std::string &to_match )
{
    for( const rule &elem : *this ) {
        if( !elem.bActive || !wildcard_match( to_match, elem.sRule ) ) {
            continue;
        }

        map_items[ to_match ] = elem.bExclude ? RULE_BLACKLISTED : RULE_WHITELISTED;
    }
}

// DDA auto_pickup.cpp:794
// CDF: returns the unscoped RULE_* enum (DDA: enum class rule_state).
rule_state player_settings::check_item( const item &it )
{
    const std::string item_name = it.tname( 1, false );
    const rule_state cached_state = base_settings::check_item( item_name );

    // RULE_NONE = uncached OR cached unmatched.
    if( cached_state != RULE_NONE || map_items.find( item_name ) != map_items.end() ) {
        return cached_state;
    }

    create_rule( &it );
    return map_items.try_emplace( item_name, RULE_NONE ).first->second;
}

void player_settings::create_rule( const item *it )
{
    // TODO: change it to be a reference
    global_rules.create_rule( map_items, *it );
    character_rules.create_rule( map_items, *it );
}

void rule_list::create_rule( cache &map_items, const item &it )
{
    const std::string to_match = it.tname( 1, false );

    for( const rule &elem : *this ) {
        if( !elem.bActive ) {
            continue;
        } else if( !autopickup_item_function( it, elem.sRule ) &&
                   !wildcard_match( to_match, elem.sRule ) ) {
            continue;
        }

        map_items[ to_match ] = elem.bExclude ? RULE_BLACKLISTED : RULE_WHITELISTED;
    }
}

void player_settings::refresh_map_items( cache &map_items ) const
{
    //process include/exclude in order of rules, global first, then character specific
    //if a specific item is being added, all the rules need to be checked now
    //may have some performance issues since exclusion needs to check all items also
    global_rules.refresh_map_items( map_items );
    character_rules.refresh_map_items( map_items );
}

void rule_list::refresh_map_items( cache &map_items ) const
{
    for( const rule &elem : *this ) {
        if( elem.sRule.empty() || !elem.bActive ) {
            continue;
        }

        if( !elem.bExclude ) {
            //Check include patterns against all itemfactory items
            for( const itype *e : item_controller->all() ) {
                const std::string &cur_item = e->nname( 1 );

                if( !test_pattern_function( *e, elem.sRule ) && !wildcard_match( cur_item, elem.sRule ) ) {
                    continue;
                }

                map_items[ cur_item ] = RULE_WHITELISTED;
                map_items.temp_items[ cur_item ] = e;
            }
        } else {
            //only re-exclude items from the existing mapping for now
            //new exclusions will process during pickup attempts
            for( auto &map_item : map_items ) {
                if( !test_pattern_function( *( map_items.temp_items[ map_item.first ] ), elem.sRule ) &&
                    !wildcard_match( map_item.first, elem.sRule ) ) {
                    continue;
                }

                map_items[ map_item.first ] = RULE_BLACKLISTED;
            }
        }
    }
}

rule_state base_settings::check_item( const std::string &sItemName ) const
{
    if( !map_items.ready ) {
        recreate();
    }

    const auto iter = map_items.find( sItemName );
    if( iter != map_items.end() ) {
        return iter->second;
    }

    return RULE_NONE;
}

void player_settings::clear_character_rules()
{
    character_rules.clear();
    invalidate();
}

bool player_settings::save_character()
{
    return save( true );
}

bool player_settings::save_global()
{
    return save( false );
}

bool player_settings::save( const bool bCharacter )
{
    if( bCharacter ) {
        //Character not saved yet.
        if( !g->get_active_world()->player_file_exist( ".sav" ) ) {
            return true;
        }

        return g->get_active_world()->write_to_player_file( ".apu.json", [&]( std::ostream & fout ) {
            JsonOut jout( fout, true );
            ( bCharacter ? character_rules : global_rules ).serialize( jout );
        }, _( "autopickup configuration" ) );
    } else {
        return write_to_file( PATH_INFO::autopickup(), [&]( std::ostream & fout ) {
            JsonOut jout( fout, true );
            ( bCharacter ? character_rules : global_rules ).serialize( jout );
        }, _( "autopickup configuration" ) );
    }
}

void player_settings::load_character()
{
    load( true );
}

void player_settings::load_global()
{
    load( false );
}

void player_settings::load( const bool bCharacter )
{
    if( bCharacter ) {
        g->get_active_world()->read_from_player_file_json( ".apu.json", [&]( JsonIn & jsin ) {
            ( bCharacter ? character_rules : global_rules ).deserialize( jsin );
        }, true );
    } else {
        read_from_file_json( PATH_INFO::autopickup(), [&]( JsonIn & jsin ) {
            ( bCharacter ? character_rules : global_rules ).deserialize( jsin );
        }, true );
    }
    invalidate();
}

void rule::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "rule", sRule );
    jsout.member( "active", bActive );
    jsout.member( "exclude", bExclude );
    jsout.end_object();
}

void rule_list::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    for( const rule &elem : *this ) {
        elem.serialize( jsout );
    }
    jsout.end_array();
}

void rule::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    sRule = jo.get_string( "rule" );
    bActive = jo.get_bool( "active" );
    bExclude = jo.get_bool( "exclude" );
}

void rule_list::deserialize( JsonIn &jsin )
{
    clear();

    jsin.start_array();
    while( !jsin.end_array() ) {
        rule tmp;
        tmp.deserialize( jsin );
        push_back( tmp );
    }
}

void npc_settings::show( const std::string &name )
{
    user_interface ui;
    ui.title = string_format( _( "Pickup rules for %s" ), name );
    ui.tabs.emplace_back( name, rules );
    ui.show();
    // Don't need to save the rules here, it will be save along with the NPC object itself.
    if( !ui.bStuffChanged ) {
        return;
    }
    invalidate();
}

void npc_settings::serialize( JsonOut &jsout ) const
{
    rules.serialize( jsout );
}

void npc_settings::deserialize( JsonIn &jsin )
{
    rules.deserialize( jsin );
}

void npc_settings::refresh_map_items( cache &map_items ) const
{
    rules.refresh_map_items( map_items );
}

bool npc_settings::empty() const
{
    return rules.empty();
}

void base_settings::recreate() const
{
    map_items.clear();
    map_items.temp_items.clear();
    refresh_map_items( map_items );
    map_items.ready = true;
    map_items.temp_items.clear();
}

void base_settings::invalidate()
{
    map_items.ready = false;
}
