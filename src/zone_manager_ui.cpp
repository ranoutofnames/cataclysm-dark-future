#include "clzones.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cached_options.h"
#include "cata_utility.h"
#include "cursesdef.h"
#include "faction.h"
#include "game.h"
#include "game_constants.h"
#include "input.h"
#include "line.h"
#include "map/map.h"
#include "options.h"
#include "output.h"
#include "overmap/overmap_ui.h"
#include "panels.h"
#include "popup.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"

namespace
{

/// Copy `text` into `line` starting at `column`, clipping at the end of the line.
auto put_text( std::string &line, int column, const std::string &text ) -> void
{
    if( column < 0 || column >= static_cast<int>( line.size() ) ) {
        return;
    }
    const auto start = static_cast<size_t>( column );
    const auto count = std::min( text.size(), line.size() - start );
    line.replace( start, count, text, 0, count );
}

auto count_vehicle_zones( const std::vector<zone_manager::ref_zone_data> &zones ) -> int
{
    return static_cast<int>( std::ranges::count_if( zones, []( const auto & ref ) {
        return ref.get().get_is_vehicle();
    } ) );
}

} // namespace

auto zone_manager_ui::visible_zones( zone_manager &mgr, const faction_id &fac, bool show_all,
                                     const tripoint_abs_ms &from, bool debug )
-> std::vector<zone_manager::ref_zone_data>
{
    std::vector<zone_manager::ref_zone_data> zones;
    if( show_all ) {
        zones = mgr.get_zones( fac );
    } else {
        for( auto &ref : mgr.get_zones( fac ) ) {
            const auto zone_abs_pos = ref.get().get_center_point();
            if( rl_dist( from, zone_abs_pos ) <= MAX_VIEW_DISTANCE ) {
                zones.emplace_back( ref );
            }
        }
    }
    std::erase_if( zones, [debug]( const auto &it ) {
        const auto type = it.get().get_type();
        return !debug && type.is_valid() && type->hidden;
    } );
    return zones;
}

auto zone_manager_ui::zone_row_text( const zone_data &zone, const tripoint_abs_ms &from,
                                     int width ) -> std::string
{
    // The columns match the old in-window drawing; the last one (41) is the
    // vehicle marker, so always leave room for it.
    const auto columns = std::max( width, 42 );
    auto line = std::string( static_cast<size_t>( columns ), ' ' );

    put_text( line, 3, trim_by_length( zone.get_name(), 15 ) );

    const auto type = zone.get_type();
    put_text( line, 20, type.is_valid() ? type->name() : _( "Unknown Type" ) );

    const auto center = zone.get_center_point();
    put_text( line, 32, string_format( "%*d %s", 5,
                                       static_cast<int>( trig_dist( from, center ) ),
                                       direction_name_short( direction_from( from, center ) ) ) );

    if( zone.get_is_vehicle() ) {
        put_text( line, 41, "*" );
    }
    return line;
}

auto zone_manager_ui::cursor_after_add( int zone_cnt, int vehicle_zones_before,
                                        int vehicle_zones_after ) -> int
{
    auto index = zone_cnt - 1;
    if( vehicle_zones_after == vehicle_zones_before ) {
        index -= vehicle_zones_after;
    }
    return index;
}

auto zone_manager_ui::set_enabled_where( const std::vector<zone_manager::ref_zone_data> &zones,
        const std::function<bool( const zone_data & )> &predicate, bool enabled ) -> bool
{
    auto changed = false;
    for( const auto &ref : zones ) {
        auto &zone = ref.get();
        if( zone.get_enabled() == enabled ) {
            continue;
        }
        if( !predicate( zone ) ) {
            continue;
        }
        zone.set_enabled( enabled );
        changed = true;
    }
    return changed;
}

auto zone_manager_ui::move_zone_index( int active_index, int zone_cnt, bool up ) -> int
{
    if( zone_cnt <= 1 ) {
        return active_index;
    }
    if( up ) {
        return std::max( active_index - 1, 0 );
    }
    return std::min( active_index + 1, zone_cnt - 1 );
}

auto zone_manager_ui::zones_manager_shortcuts( const catacurses::window &w_info,
        const faction_id &faction, bool show_all_zones, const input_context &ctxt, int width ) -> void
{
    werase( w_info );

    std::vector<std::string> act_descs;
    const auto show_zones_text = show_all_zones ? _( "Showing all zones" ) : _( "Hiding distant zones" );
    const auto zone_faction = string_format( _( "Shown faction: %s" ), faction.str() );
    const auto add_action_desc = [&]( const std::string &act, const std::string &txt ) {
        act_descs.emplace_back( ctxt.get_desc( act, txt, input_context::allow_all_keys ) );
    };
    const auto add_colored_action_desc = [&]( const std::string &act, const std::string &txt,
                                              bool on ) {
        act_descs.emplace_back( string_format( "<color_%s>%s</color>", on ? "light_green" : "white",
                                              ctxt.get_desc( act, txt, input_context::allow_all_keys ) ) );
    };

    add_action_desc( "ADD_ZONE", pgettext( "zones manager", "Add" ) );
    add_action_desc( "REMOVE_ZONE", pgettext( "zones manager", "Remove" ) );
    add_action_desc( "ENABLE_ZONE", pgettext( "zones manager", "Enable" ) );
    add_action_desc( "DISABLE_ZONE", pgettext( "zones manager", "Disable" ) );
    add_action_desc( "ENABLE_VEHICLE_ZONES", pgettext( "zones manager", "Enable vehicle" ) );
    add_action_desc( "DISABLE_VEHICLE_ZONES", pgettext( "zones manager", "Disable vehicle" ) );
    add_action_desc( "MOVE_ZONE_UP", pgettext( "zones manager", "Move up" ) );
    add_action_desc( "MOVE_ZONE_DOWN", pgettext( "zones manager", "Move down" ) );
    add_action_desc( "CONFIRM", pgettext( "zones manager", "Edit" ) );
    add_action_desc( "SHOW_ALL_ZONES", pgettext( "zones manager", show_zones_text ) );
    add_action_desc( "SHOW_ZONE_ON_MAP", pgettext( "zones manager", "Map" ) );
    add_colored_action_desc( "TOGGLE_ZONE_OVERLAY", pgettext( "zones manager", "Overlays" ),
                             g->show_zone_overlay );
    add_colored_action_desc( "debug_submap_grid", pgettext( "zones manager", "Submap grid" ),
                             g->debug_submap_grid_overlay || g->zone_submap_grid_overlay );
    if( debug_mode ) {
        add_action_desc( "CHANGE_FACTION", pgettext( "zones manager", zone_faction.c_str() ) );
    }

    const auto keybinding_tips = foldstring( enumerate_as_string( act_descs,
                                  enumeration_conjunction::none ), width - 2 );
    for( size_t i = 0; i < keybinding_tips.size(); ++i ) {
        nc_color dummy = c_white;
        print_colored_text( w_info, point( 1, 1 + i ), dummy, c_white, keybinding_tips[i] );
    }

    wnoutrefresh( w_info );
}

auto zone_manager_ui::zones_manager_draw_borders( const catacurses::window &w_border,
        const catacurses::window &w_info_border, int i_info_height, int width ) -> void
{
    wattron( w_border, c_light_gray );
    // NOLINTNEXTLINE(cata-use-named-point-constants)
    mvwhline( w_border, point( 1, 0 ), LINE_OXOX, width - 1 ); // -
    mvwhline( w_border, point( 1, TERMY - i_info_height - 1 ), LINE_OXOX, width - 1 ); // -
    // NOLINTNEXTLINE(cata-use-named-point-constants)
    mvwvline( w_border, point( 0, 1 ), LINE_XOXO, TERMY - i_info_height - 1 ); // |
    mvwvline( w_border, point( width - 1, 1 ), LINE_XOXO, TERMY - i_info_height - 1 ); // |

    mvwaddch( w_border, point_zero, LINE_OXXO ); // |^
    mvwaddch( w_border, point( width - 1, 0 ), LINE_OOXX ); // ^|

    mvwaddch( w_border, point( 0, TERMY - i_info_height - 1 ), LINE_XXXO ); // |-
    mvwaddch( w_border, point( width - 1, TERMY - i_info_height - 1 ), LINE_XOXX ); // -|
    wattroff( w_border, c_light_gray );

    mvwprintz( w_border, point( 2, 0 ), c_white, _( "Zones manager" ) );

    wnoutrefresh( w_border );

    wattron( w_info_border, c_light_gray );
    // NOLINTNEXTLINE(cata-use-named-point-constants)
    mvwvline( w_info_border, point( 0, 0 ), LINE_XOXO, i_info_height - 1 );
    mvwvline( w_info_border, point( width - 1, 0 ), LINE_XOXO, i_info_height - 1 );
    mvwhline( w_info_border, point( 0, i_info_height - 1 ), LINE_OXOX, width - 1 );

    mvwaddch( w_info_border, point( 0, i_info_height - 1 ), LINE_XXOO );
    mvwaddch( w_info_border, point( width - 1, i_info_height - 1 ), LINE_XOOX );
    wattroff( w_info_border, c_light_gray );

    wnoutrefresh( w_info_border );
}

void zone_manager_ui::display_zone_manager()
{
    auto &u = get_avatar();

    const auto stored_view_offset = u.view_offset;

    u.view_offset = tripoint_rel_ms::zero();

    const auto zone_ui_height = 14;
    const auto zone_options_height = debug_mode ? 6 : 7;
    const auto minimum_zone_ui_width = 45;
    auto zone_ui_width = minimum_zone_ui_width;

    auto offset_x = 0;
    auto max_rows = 0;

    catacurses::window w_zones;
    catacurses::window w_zones_border;
    catacurses::window w_zones_info;
    catacurses::window w_zones_info_border;
    catacurses::window w_zones_options;

    auto show = true;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        if( !show ) {
            ui.position( point_zero, point_zero );
            return;
        }
        const auto sidebar_on_right = get_option<std::string>( "SIDEBAR_POSITION" ) == "right";
        const auto sidebar_width = sidebar_on_right ?
                                   panel_manager::get_manager().get_width_right() :
                                   panel_manager::get_manager().get_width_left();
        zone_ui_width = std::max( sidebar_width, minimum_zone_ui_width );
        offset_x = get_option<std::string>( "SIDEBAR_POSITION" ) != "left" ?
                   TERMX - zone_ui_width : 0;
        const auto w_zone_height = TERMY - zone_ui_height;
        max_rows = w_zone_height - 2;
        w_zones = catacurses::newwin( w_zone_height - 2, zone_ui_width - 2,
                                      point( offset_x + 1, 1 ) );
        w_zones_border = catacurses::newwin( w_zone_height, zone_ui_width,
                                             point( offset_x, 0 ) );
        w_zones_info = catacurses::newwin( zone_ui_height - zone_options_height - 1,
                                           zone_ui_width - 2, point( offset_x + 1, w_zone_height ) );
        w_zones_info_border = catacurses::newwin( zone_ui_height, zone_ui_width,
                               point( offset_x, w_zone_height ) );
        w_zones_options = catacurses::newwin( zone_options_height - 1, zone_ui_width - 2,
                                              point( offset_x + 1, TERMY - zone_options_height ) );

        ui.position( point( offset_x, 0 ), point( zone_ui_width, TERMY ) );
    } );
    ui.mark_resize();

    input_context ctxt( "ZONES_MANAGER" );
    ctxt.register_navigate_ui_list();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ADD_ZONE" );
    ctxt.register_action( "REMOVE_ZONE" );
    ctxt.register_action( "MOVE_ZONE_UP" );
    ctxt.register_action( "MOVE_ZONE_DOWN" );
    ctxt.register_action( "SHOW_ZONE_ON_MAP" );
    ctxt.register_action( "ENABLE_ZONE" );
    ctxt.register_action( "DISABLE_ZONE" );
    ctxt.register_action( "ENABLE_VEHICLE_ZONES" );
    ctxt.register_action( "DISABLE_VEHICLE_ZONES" );
    ctxt.register_action( "SHOW_ALL_ZONES" );
    ctxt.register_action( "TOGGLE_ZONE_OVERLAY" );
    ctxt.register_action( "debug_submap_grid" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    if( debug_mode ) {
        ctxt.register_action( "CHANGE_FACTION" );
    }

    auto &mgr = zone_manager::get_manager();
    auto start_index = 0;
    auto active_index = 0;
    auto blink = false;
    auto stuff_changed = false;
    auto show_all_zones = false;
    auto zone_cnt = 0;
    auto zones_faction = your_fac;

    // get zones within MAX_VIEW_DISTANCE of the player or all zones, if
    // show_all_zones is true
    auto get_zones = [&]() {
        auto zones = visible_zones( mgr, zones_faction, show_all_zones, u.abs_pos(), debug_mode );
        zone_cnt = static_cast<int>( zones.size() );
        return zones;
    };

    auto zones = get_zones();

    auto zones_manager_options = [&]() {
        werase( w_zones_options );

        if( zone_cnt > 0 ) {
            const auto &zone = zones[active_index].get();

            if( zone.has_options() ) {
                const auto &descriptions = zone.get_options().get_descriptions();

                // NOLINTNEXTLINE(cata-use-named-point-constants)
                mvwprintz( w_zones_options, point( 1, 0 ), c_white, _( "Options" ) );

                auto y = 1;
                for( const auto &desc : descriptions ) {
                    mvwprintz( w_zones_options, point( 3, y ), c_white, desc.first );
                    mvwprintz( w_zones_options, point( 20, y ), c_white, desc.second );
                    y++;
                }
            }
        }

        wnoutrefresh( w_zones_options );
    };

    std::optional<tripoint_abs_ms> zone_start;
    std::optional<tripoint_abs_ms> zone_end;
    auto zone_blink = false;
    auto zone_cursor = false;
    auto current_zone_type = zone_type_id();
    shared_ptr_fast<const blueprint_options> current_bp_options;
    static const auto zone_construction_blueprint = zone_type_id( "CONSTRUCTION_BLUEPRINT" );
    auto zone_point_generator =
    [&]( const tripoint_abs_ms & start, const tripoint_abs_ms & end ) -> std::vector<tripoint_bub_ms> {
        if( current_zone_type == zone_construction_blueprint ) {
            if( current_bp_options ) {
                const auto covered_points = current_bp_options->get_covered_points( start, end );
                auto points = covered_points
                | std::views::transform( []( const tripoint_abs_ms & p ) {
                    return abs_to_bub( p );
                } )
                | std::ranges::to<std::vector>();
                return points;
            }
        }
        return std::vector<tripoint_bub_ms>();
    };
    auto zone_cb = create_zone_callback( zone_callback_options{
        .zone_start = zone_start,
        .zone_end = zone_end,
        .zone_blink = zone_blink,
        .zone_cursor = zone_cursor,
        .point_generator = zone_point_generator,
    } );
    g->add_draw_callback( zone_cb );

    auto query_position =
    [&]() -> std::optional<std::pair<tripoint_abs_ms, tripoint_abs_ms>> {
        on_out_of_scope invalidate_current_ui( [&]()
        {
            ui.mark_resize();
        } );
        restore_on_out_of_scope<bool> show_prev( show );
        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_start_prev( zone_start );
        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_end_prev( zone_end );
        restore_on_out_of_scope<bool> zone_cursor_prev( zone_cursor );
        show = false;
        zone_start = std::nullopt;
        zone_end = std::nullopt;
        zone_cursor = true;
        ui.mark_resize();

        static_popup popup;
        popup.on_top( true );
        popup.message( "%s", _( "Select first point." ) );

        auto center = u.bub_pos() + u.view_offset;

        const auto first = g->look_around( /*show_window=*/false, center, center, false,
                                           true, false );
        if( first.position ) {
            popup.message( "%s", _( "Select second point." ) );

            const auto second = g->look_around( /*show_window=*/false, center, *first.position,
                                                true, true, false );
            if( second.position ) {
                auto first_abs = bub_to_abs( tripoint_bub_ms( std::min( first.position->x(),
                                             second.position->x() ),
                                             std::min( first.position->y(), second.position->y() ),
                                             std::min( first.position->z(),
                                                       second.position->z() ) ) );
                auto second_abs = bub_to_abs( tripoint_bub_ms( std::max( first.position->x(),
                                              second.position->x() ),
                                              std::max( first.position->y(), second.position->y() ),
                                              std::max( first.position->z(),
                                                        second.position->z() ) ) );
                return std::pair<tripoint_abs_ms, tripoint_abs_ms>( first_abs, second_abs );
            }
        }

        return std::nullopt;
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( !show ) {
            return;
        }
        zones_manager_draw_borders( w_zones_border, w_zones_info_border, zone_ui_height,
                                    zone_ui_width );
        zones_manager_shortcuts( w_zones_info, zones_faction, show_all_zones, ctxt, zone_ui_width );

        if( zone_cnt == 0 ) {
            werase( w_zones );
            mvwprintz( w_zones, point( 2, 5 ), c_white, _( "No Zones defined." ) );

        } else {
            werase( w_zones );

            calcStartPos( start_index, active_index, max_rows, zone_cnt );

            draw_scrollbar( w_zones_border, active_index, max_rows, zone_cnt, point_south );
            wnoutrefresh( w_zones_border );

            const auto player_absolute_pos = u.abs_pos();
            const auto rows = std::min( max_rows, zone_cnt );

            //Display saved zones
            for( auto i = start_index; i < start_index + rows; ++i ) {
                const auto &zone = zones[i].get();
                const auto row = i - start_index;

                auto color_line = zone.get_enabled() ? c_white : c_light_gray;
                if( i == active_index ) {
                    color_line = zone.get_enabled() ? c_light_green : c_green;
                }

                mvwprintz( w_zones, point( 0, row ), color_line, "%s",
                           zone_row_text( zone, player_absolute_pos, zone_ui_width - 2 ) );

                if( i == active_index ) {
                    mvwprintz( w_zones, point( 0, row ), c_yellow, "%s", ">>" );
                }
            }

            // Display zone options
            zones_manager_options();
        }

        wnoutrefresh( w_zones );
    } );

    const auto scroll_rate = zone_cnt > 20 ? 10 : 3;
    auto quit = false;
    auto save = false;
    g->set_zones_manager_open( true );
    zone_manager::get_manager().save_zones( "zmgr-temp" );
    while( !quit ) {
        if( zone_cnt > 0 ) {
            blink = !blink;
            const auto &zone = zones[active_index].get();
            zone_start = zone.get_start_point();
            zone_end = zone.get_end_point();
            current_zone_type = zone.get_type();
            current_bp_options = std::dynamic_pointer_cast<const blueprint_options>(
                                     zone.get_options_ptr() );
            ctxt.set_timeout( get_option<int>( "BLINK_SPEED" ) );
        } else {
            blink = false;
            zone_start = zone_end = std::nullopt;
            current_zone_type = zone_type_id();
            current_bp_options = nullptr;
            ctxt.reset_timeout();
        }

        // Actually accessed from the terrain overlay callback `zone_cb` in the
        // call to `ui_manager::redraw`.
        //NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
        zone_blink = blink;
        g->invalidate_main_ui_adaptor();

        ui_manager::redraw();

        //Wait for input
        const std::string action = ctxt.handle_input();

        if( action == "ADD_ZONE" ) {
            do { // not a loop, just for quick bailing out if canceled
                const auto maybe_id = mgr.query_type();
                if( !maybe_id.has_value() ) {
                    break;
                }

                const zone_type_id &id = maybe_id.value();
                auto options = zone_options::create( id );

                if( !options->query_at_creation() ) {
                    break;
                }

                auto default_name = options->get_zone_name_suggestion();
                if( default_name.empty() ) {
                    default_name = mgr.get_name_from_type( id );
                }
                const auto maybe_name = mgr.query_name( default_name );
                if( !maybe_name.has_value() ) {
                    break;
                }
                const std::string &name = maybe_name.value();

                current_zone_type = id;
                current_bp_options = std::dynamic_pointer_cast<const blueprint_options>( options );
                const auto position = query_position();
                if( !position ) {
                    break;
                }

                const auto vehicle_zones_before = count_vehicle_zones( get_zones() );
                mgr.add( name, id, u.get_faction()->id, false, true, position->first,
                         position->second, options );

                zones = get_zones();
                const auto vehicle_zones_after = count_vehicle_zones( zones );
                active_index = cursor_after_add( zone_cnt, vehicle_zones_before,
                                                 vehicle_zones_after );

                stuff_changed = true;
            } while( false );
        } else if( action == "SHOW_ALL_ZONES" ) {
            show_all_zones = !show_all_zones;
            zones = get_zones();
            active_index = 0;
        } else if( action == "CHANGE_FACTION" ) {
            ui.invalidate_ui();
            auto facname = zones_faction.str();
            string_input_popup()
            .description( _( "Show zones for faction:" ) )
            .width( 55 )
            .max_length( 256 )
            .edit( facname );
            zones_faction = faction_id( facname );
            zones = get_zones();
        } else if( action == "QUIT" ) {
            if( stuff_changed ) {
                switch( query_ynq( _( "Save changes?" ) ) ) {
                    case query_ynq_result::quit:
                        break;
                    case query_ynq_result::no:
                        save = false;
                        quit = true;
                        break;
                    case query_ynq_result::yes:
                        save = true;
                        quit = true;
                        break;
                }
            } else {
                save = false;
                quit = true;
            }
        } else if( action == "TOGGLE_ZONE_OVERLAY" ) {
            g->show_zone_overlay = !g->show_zone_overlay;
        } else if( action == "debug_submap_grid" ) {
            g->zone_submap_grid_overlay = !g->zone_submap_grid_overlay;
        } else if( zone_cnt > 0 ) {
            if( navigate_ui_list( action, active_index, scroll_rate, zone_cnt, true ) ) {
                // The cursor moved; the selected zone is updated at the top of the loop.
            } else if( action == "REMOVE_ZONE" ) {
                if( active_index < zone_cnt ) {
                    mgr.remove( zones[active_index] );
                    zones = get_zones();
                    active_index--;

                    active_index = std::max( active_index, 0 );
                }
                stuff_changed = true;

            } else if( action == "CONFIRM" ) {
                auto &zone = zones[active_index].get();

                uilist as_m;
                as_m.text = _( "What do you want to change:" );
                as_m.entries.emplace_back( 1, true, '1', _( "Edit name" ) );
                as_m.entries.emplace_back( 2, true, '2', _( "Edit type" ) );
                as_m.entries.emplace_back( 3, zone.get_options().has_options(), '3',
                                           zone.get_type() == zone_type_id( "LOOT_CUSTOM" ) ? _( "Edit filter" ) : _( "Edit options" ) );
                as_m.entries.emplace_back( 4, !zone.get_is_vehicle(), '4', _( "Edit position" ) );
                // TODO: Enable moving vzone after vehicle zone can be bigger than 1*1
                as_m.entries.emplace_back( 5, !zone.get_is_vehicle(), '5', _( "Move position" ) );
                as_m.query();

                switch( as_m.ret ) {
                    case 1:
                        if( zone.set_name() ) {
                            stuff_changed = true;
                        }
                        break;
                    case 2:
                        if( zone.set_type() ) {
                            stuff_changed = true;
                        }
                        break;
                    case 3:
                        if( zone.get_options().query() ) {
                            stuff_changed = true;
                        }
                        break;
                    case 4: {
                        const auto pos = query_position();
                        if( pos && ( pos->first != zone.get_start_point() ||
                                     pos->second != zone.get_end_point() ) ) {
                            zone.set_position( *pos );
                            stuff_changed = true;
                        }
                        break;
                    }
                    case 5: {
                        on_out_of_scope invalidate_current_ui( [&]() {
                            ui.mark_resize();
                        } );
                        restore_on_out_of_scope<bool> show_prev( show );
                        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_start_prev( zone_start );
                        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_end_prev( zone_end );
                        restore_on_out_of_scope<bool> zone_cursor_prev( zone_cursor );
                        show = false;
                        zone_start = std::nullopt;
                        zone_end = std::nullopt;
                        zone_cursor = true;
                        ui.mark_resize();
                        static_popup message_pop;
                        message_pop.on_top( true );
                        message_pop.message( "%s", _( "Moving zone." ) );
                        const auto zone_local_start_point = abs_to_bub( zone.get_start_point() );
                        const auto zone_local_end_point = abs_to_bub( zone.get_end_point() );
                        // local position of the zone center, used to calculate the u.view_offset,
                        // could center the screen to the position it represents
                        auto view_center = abs_to_bub( zone.get_center_point() );
                        const auto result_local = g->look_around( false, view_center,
                                                 zone_local_start_point, false, false,
                                                 false, true, zone_local_end_point );
                        if( result_local.position ) {
                            const auto new_start_point = bub_to_abs( *result_local.position );
                            if( new_start_point == zone.get_start_point() ) {
                                break; // Nothing changed, don't save
                            }

                            const auto new_end_point = zone.get_end_point() - zone.get_start_point() + new_start_point;
                            zone.set_position( std::pair<tripoint_abs_ms, tripoint_abs_ms>( new_start_point, new_end_point ) );
                            stuff_changed = true;
                        }
                    }
                    break;
                    default:
                        break;
                }

            } else if( action == "MOVE_ZONE_UP" && zone_cnt > 1 ) {
                const auto target = move_zone_index( active_index, zone_cnt, true );
                if( target != active_index ) {
                    mgr.swap( zones[active_index], zones[target] );
                    zones = get_zones();
                    active_index = target;
                }
                stuff_changed = true;

            } else if( action == "MOVE_ZONE_DOWN" && zone_cnt > 1 ) {
                const auto target = move_zone_index( active_index, zone_cnt, false );
                if( target != active_index ) {
                    mgr.swap( zones[active_index], zones[target] );
                    zones = get_zones();
                    active_index = target;
                }
                stuff_changed = true;

            } else if( action == "SHOW_ZONE_ON_MAP" ) {
                //show zone position on overmap;
                auto player_overmap_position = u.abs_omt_pos();
                // TODO: fix point types
                auto zone_overmap = tripoint_abs_omt( project_to<coords::omt>
                                                      ( zones[active_index].get().get_center_point() ) );

                ui::omap::display_zones( player_overmap_position, zone_overmap, active_index );
            } else if( action == "ENABLE_ZONE" ) {
                zones[active_index].get().set_enabled( true );

                stuff_changed = true;

            } else if( action == "DISABLE_ZONE" ) {
                zones[active_index].get().set_enabled( false );

                stuff_changed = true;

            } else if( action == "ENABLE_VEHICLE_ZONES" ) {
                stuff_changed |= set_enabled_where( zones, []( const zone_data & zone ) {
                    return zone.get_is_vehicle();
                }, true );

            } else if( action == "DISABLE_VEHICLE_ZONES" ) {
                stuff_changed |= set_enabled_where( zones, []( const zone_data & zone ) {
                    return zone.get_is_vehicle();
                }, false );
            }
        }

        if( action != "TIMEOUT" ) {
            blink = false;
        }
    }
    g->set_zones_manager_open( false );
    ctxt.reset_timeout();
    zone_cb = nullptr;

    if( stuff_changed ) {
        auto &manager = zone_manager::get_manager();
        if( !save ) {
            manager.load_zones( "zmgr-temp" );
        } else {
            manager.save_zones();
        }

        manager.cache_data();
    }

    u.view_offset = stored_view_offset;
}
