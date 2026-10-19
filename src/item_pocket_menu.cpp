#include "item_pocket_menu.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "character.h"
#include "color.h"
#include "input.h"
#include "item.h"
#include "item_category.h"
#include "item_parent_checks.h"
#include "iteminfo_format_utils.h"
#include "itype.h"
#include "output.h"
#include "pocket_type.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "temp_crafting_inventory.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "visitable.h"

// Ported from DDA item_contents.cpp:52-531 and 2982 (pocket_favorite_callback, pocket_management_menu).
// CDF: item* replaces item_location, and the data side is split from the drawing so it can be tested.

pocket_settings_model::pocket_settings_model( std::vector<item *> roots ) : roots_( std::move(
                roots ) )
{
    rebuild();
}

void pocket_settings_model::rebuild()
{
    rows_.clear();
    for( item *it : roots_ ) {
        add_pockets( *it, "" );
    }
}

void pocket_settings_model::add_pockets( item &it, const std::string &depth )
{
    const std::vector<item_pocket *> pockets = it.get_container_pockets();
    if( pockets.empty() ) {
        return;
    }
    pocket_row heading;
    heading.holder = &it;
    heading.text = depth + it.display_name();
    rows_.push_back( heading );

    int pocket_num = 1;
    for( item_pocket *pk : pockets ) {
        pocket_row row;
        row.pocket = pk;
        row.pocket_num = pocket_num;
        row.holder = &it;
        row.enabled = true;
        row.text = string_format( "%s%d - %s/%s", depth, pocket_num,
                                  vol_to_info( "", "", pk->contents_volume() ).sValue,
                                  vol_to_info( "", "", pk->volume_capacity() ).sValue );
        rows_.push_back( row );
        pocket_num++;
        for( item *contained : pk->all_items_top() ) {
            add_pockets( *contained, depth + "  " );
        }
    }
}

item_pocket *pocket_settings_model::pocket_at( int row ) const
{
    if( row < 0 || static_cast<size_t>( row ) >= rows_.size() ) {
        return nullptr;
    }
    return rows_[row].pocket;
}

int pocket_settings_model::pocket_number_at( int row ) const
{
    return pocket_at( row ) == nullptr ? 0 : rows_[row].pocket_num;
}

void pocket_settings_model::set_priority( item_pocket &pk, int priority ) const
{
    pk.settings.set_priority( priority );
    pk.settings.set_was_edited();
}

void pocket_settings_model::toggle_auto_pickup( item_pocket &pk ) const
{
    pk.settings.set_disabled( !pk.settings.is_disabled() );
    pk.settings.set_was_edited();
}

void pocket_settings_model::toggle_auto_unload( item_pocket &pk ) const
{
    pk.settings.set_unloadable( !pk.settings.is_unloadable() );
    pk.settings.set_was_edited();
}

void pocket_settings_model::toggle_item( item_pocket &pk, const itype_id &id ) const
{
    if( whitelist ) {
        pk.settings.whitelist_item( id );
    } else {
        pk.settings.blacklist_item( id );
    }
    pk.settings.set_was_edited();
}

void pocket_settings_model::toggle_category( item_pocket &pk, const item_category_id &id ) const
{
    if( whitelist ) {
        pk.settings.whitelist_category( id );
    } else {
        pk.settings.blacklist_category( id );
    }
    pk.settings.set_was_edited();
}

bool pocket_settings_model::whitelist_contents( item_pocket &pk ) const
{
    bool modified = false;
    for( item *it : pk.all_items_top() ) {
        if( !pk.settings.get_item_whitelist().count( it->typeId() ) ) {
            pk.settings.whitelist_item( it->typeId() );
            modified = true;
        }
    }
    if( modified ) {
        pk.settings.set_was_edited();
    }
    return modified;
}

void pocket_settings_model::clear( item_pocket &pk ) const
{
    pk.settings.clear();
    pk.settings.set_was_edited();
}

std::vector<item *> pocket_settings_model::movable_items( item_pocket &pk )
{
    std::vector<item *> ret = pk.all_items_top();
    std::sort( ret.begin(), ret.end() );
    return ret;
}

/** true if @p needle is (eventually) inside @p haystack. */
static bool is_pocket_in_item( const item *haystack, const item_pocket *needle )
{
    for( const item_pocket *pocket : haystack->get_standard_pockets() ) {
        if( needle == pocket ) {
            return true;
        }
        for( const item *itm : pocket->all_items_top() ) {
            if( is_pocket_in_item( itm, needle ) ) {
                return true;
            }
        }
    }
    return false;
}

bool pocket_settings_model::can_move_to( const item &moved, int row ) const
{
    const item_pocket *pk = pocket_at( row );
    if( pk == nullptr ) {
        return false;
    }
    return pk->can_contain( moved ).success() && !is_pocket_in_item( &moved, pk ) &&
           parents_can_contain_recursive( *rows_[row].holder, moved ).success();
}

std::string pocket_settings_model::move_item( Character &who, item &moved, item_pocket &dest )
{
    const ret_val<item_pocket::contain_code> contain = dest.can_contain( moved );
    if( dest.get_pocket_data()->_no_reload ) {
        return _( "destination container can't be reloaded." );
    }
    if( !contain.success() ) {
        return contain.str();
    }
    // storage should mimic the character inserting
    who.store( &dest, moved, true, INVENTORY_HANDLING_PENALTY );
    return std::string();
}

bool pocket_settings_model::save_preset( item_pocket &pk, const std::string &name, bool overwrite )
{
    item_pocket::load_presets();
    if( item_pocket::has_preset( name ) ) {
        if( !overwrite ) {
            return false;
        }
        item_pocket::delete_preset( item_pocket::find_preset( name ) );
    }
    pk.settings.set_preset_name( name );
    item_pocket::add_preset( pk.settings );
    return true;
}

void pocket_settings_model::apply_preset( item_pocket &pk, int index )
{
    pk.settings = item_pocket::pocket_presets.at( index );
}

void pocket_settings_model::delete_preset( int index )
{
    item_pocket::delete_preset( item_pocket::pocket_presets.begin() + index );
}

namespace
{
const std::string remove_prefix = "<color_light_red>-</color> ";
const std::string add_prefix = "<color_green>+</color> ";

class pocket_favorite_callback : public uilist_callback
{
    public:
        pocket_favorite_callback( pocket_settings_model &model, const std::string &text,
                                  Character &who ) : model( model ), uilist_text( text ), who( who ) {}
        void refresh( uilist *menu ) override;
        bool key( const input_context &ctxt, const input_event &event, int entnum,
                  uilist *menu ) override;
        void rebuild_menu( uilist &menu );

    private:
        pocket_settings_model &model;
        std::string uilist_text;
        Character &who;
        // the item picked by the first press of "move item"
        item *item_to_move = nullptr;

        void move_item( uilist *menu, item_pocket &pk );
        void pick_item( item_pocket &pk );
        void pick_category( item_pocket &pk );
        bool handle_action( const input_context &ctxt, const std::string &action, uilist *menu );
};

void pocket_favorite_callback::rebuild_menu( uilist &menu )
{
    model.rebuild();
    menu.entries.clear();
    for( const pocket_row &row : model.rows() ) {
        menu.addentry( row.pocket != nullptr ? 0 : -1, row.enabled, 0, row.text );
    }
    menu.setup();
}

void pocket_favorite_callback::refresh( uilist *menu )
{
    item_pocket *pk = model.pocket_at( menu->selected );
    if( pk == nullptr || pk->is_forbidden() ) {
        return;
    }
    const int startx = menu->w_width - menu->pad_right;
    const std::string padding( menu->pad_right, ' ' );
    for( int y = 2; y < menu->w_height - 1; y++ ) {
        mvwprintw( menu->window, point( startx - 1, y ), padding );
    }
    const int pocket_num = model.pocket_number_at( menu->selected );
    std::string out = colorize( _( "Currently modifying " ), c_light_gray ) +
                      colorize( model.whitelist ? _( "whitelist" ) : _( "blacklist" ),
                                c_light_blue ) + "\n\n";
    std::vector<iteminfo> info;
    pk->general_info( info, pocket_num, true );
    out += format_item_info( info, {} );
    info.clear();
    pk->favorite_info( info );
    out += format_item_info( info, {} );
    info.clear();
    pk->contents_info( info, pocket_num, true );
    out += format_item_info( info, {} );

    int line_y = 2;
    for( const std::string &line : foldstring( out, menu->pad_right - 1 ) ) {
        if( line_y >= menu->w_height - 1 ) {
            break;
        }
        nc_color cur_color = c_light_gray;
        print_colored_text( menu->window, point( startx, line_y ), cur_color, c_light_gray, line );
        ++line_y;
    }
    wnoutrefresh( menu->window );
}

void pocket_favorite_callback::move_item( uilist *menu, item_pocket &pk )
{
    if( item_to_move == nullptr ) {
        if( pk.get_pocket_data()->_no_unload ) {
            popup( std::string( _( "Can't unload that." ) ), PF_GET_KEY );
            return;
        }
        const std::vector<item *> item_list = pocket_settings_model::movable_items( pk );
        if( item_list.empty() ) {
            popup( std::string( _( "No items in the pocket." ) ), PF_GET_KEY );
            return;
        }
        uilist selector_menu;
        selector_menu.title = _( "Select an item from the pocket" );
        for( const item *it : item_list ) {
            selector_menu.addentry( it->display_name() );
        }
        selector_menu.query();
        if( selector_menu.ret < 0 ) {
            return;
        }
        item_to_move = item_list[selector_menu.ret];
        menu->settext( string_format( "%s: %s", _( "Moving" ), item_to_move->display_name() ) );
        rebuild_menu( *menu );
        for( size_t i = 0; i < menu->entries.size(); ++i ) {
            if( menu->entries[i].enabled && !model.can_move_to( *item_to_move, i ) ) {
                menu->entries[i].enabled = false;
            }
        }
    } else {
        // with no enabled entries uilist still lets the selection move, so check again
        const std::string err = pocket_settings_model::move_item( who, *item_to_move, pk );
        if( !err.empty() ) {
            popup( string_format( _( "Cannot put item in pocket because %s" ), err ) );
        }
        item_to_move = nullptr;
        menu->settext( uilist_text );
        rebuild_menu( *menu );
    }
}

void pocket_favorite_callback::pick_item( item_pocket &pk )
{
    const cata::flat_set<itype_id> &listed_itypes = model.whitelist
            ? pk.settings.get_item_whitelist() : pk.settings.get_item_blacklist();
    cata::flat_set<itype_id> nearby_itypes;
    uilist selector_menu;
    selector_menu.title = _( "Select an item from nearby" );
    who.crafting_inventory().visit_items( [&]( const item * node ) {
        nearby_itypes.insert( node->typeId() );
        return VisitResponse::NEXT;
    } );

    std::vector<std::pair<itype_id, std::string>> listed_names;
    std::vector<std::pair<itype_id, std::string>> nearby_names;
    for( const itype_id &id : listed_itypes ) {
        listed_names.emplace_back( id, id->nname( 1 ) );
    }
    for( const itype_id &id : nearby_itypes ) {
        if( !listed_itypes.count( id ) &&
            pk.is_compatible( *item::spawn_temporary( id, calendar::turn ) ).success() ) {
            nearby_names.emplace_back( id, id->nname( 1 ) );
        }
    }
    const auto compare_names = []( const std::pair<itype_id, std::string> &lhs,
    const std::pair<itype_id, std::string> &rhs ) {
        return localized_compare( lhs.second, rhs.second );
    };
    std::sort( listed_names.begin(), listed_names.end(), compare_names );
    std::sort( nearby_names.begin(), nearby_names.end(), compare_names );
    for( const auto &it : listed_names ) {
        selector_menu.addentry( remove_prefix + it.second );
    }
    for( const auto &it : nearby_names ) {
        selector_menu.addentry( add_prefix + it.second );
    }
    if( selector_menu.entries.empty() ) {
        popup( std::string( _( "No nearby items would fit here." ) ), PF_GET_KEY );
        return;
    }
    selector_menu.query();
    if( selector_menu.ret < 0 ) {
        return;
    }
    size_t idx = selector_menu.ret;
    if( idx < listed_names.size() ) {
        model.toggle_item( pk, listed_names[idx].first );
    } else if( idx - listed_names.size() < nearby_names.size() ) {
        model.toggle_item( pk, nearby_names[idx - listed_names.size()].first );
    }
}

void pocket_favorite_callback::pick_category( item_pocket &pk )
{
    std::vector<item_category> all_cat = item_category::get_all();
    const cata::flat_set<item_category_id> &listed_cat = model.whitelist
            ? pk.settings.get_category_whitelist() : pk.settings.get_category_blacklist();
    std::sort( all_cat.begin(), all_cat.end(), [&]( const item_category & lhs,
    const item_category & rhs ) {
        const bool lhs_in_list = listed_cat.count( lhs.get_id() );
        const bool rhs_in_list = listed_cat.count( rhs.get_id() );
        if( lhs_in_list != rhs_in_list ) {
            return lhs_in_list;
        }
        return localized_compare( lhs.name(), rhs.name() );
    } );
    uilist selector_menu;
    for( const item_category &cat : all_cat ) {
        selector_menu.addentry( ( listed_cat.count( cat.get_id() ) ? remove_prefix : add_prefix ) +
                                cat.name() );
    }
    selector_menu.query();
    if( selector_menu.ret >= 0 ) {
        model.toggle_category( pk, all_cat.at( selector_menu.ret ).get_id() );
    }
}

bool pocket_favorite_callback::key( const input_context &ctxt, const input_event &event, int,
                                    uilist *menu )
{
    if( model.pocket_at( menu->selected ) == nullptr ) {
        return false;
    }
    return handle_action( ctxt, ctxt.input_to_action( event ), menu );
}

bool pocket_favorite_callback::handle_action( const input_context &ctxt, const std::string &action,
        uilist *menu )
{
    item_pocket *pk = model.pocket_at( menu->selected );
    if( pk == nullptr ) {
        return false;
    }
    if( action == "FAV_WHITELIST" ) {
        model.whitelist = true;
    } else if( action == "FAV_BLACKLIST" ) {
        model.whitelist = false;
    } else if( action == "FAV_PRIORITY" ) {
        string_input_popup popup;
        popup.title( _( "Enter Priority" ) ).width( 34 ).only_digits( true )
        .text( string_format( "%d", pk->settings.priority() ) );
        const int ret = popup.query_int();
        if( !popup.canceled() ) {
            model.set_priority( *pk, ret );
        }
    } else if( action == "FAV_AUTO_PICKUP" ) {
        model.toggle_auto_pickup( *pk );
    } else if( action == "FAV_AUTO_UNLOAD" ) {
        model.toggle_auto_unload( *pk );
    } else if( action == "FAV_MOVE_ITEM" ) {
        move_item( menu, *pk );
        pk->settings.set_was_edited();
    } else if( action == "FAV_ITEM" ) {
        pick_item( *pk );
    } else if( action == "FAV_CATEGORY" ) {
        pick_category( *pk );
    } else if( action == "FAV_SAVE_PRESET" ) {
        string_input_popup popup;
        popup.title( _( "Enter a preset name:" ) ).width( 34 )
        .text( pk->settings.get_preset_name().value_or( "" ) );
        const std::string name = popup.query_string();
        if( !popup.canceled() && !pocket_settings_model::save_preset( *pk, name, false ) &&
            query_yn( _( "Preset already exists, overwrite?" ) ) ) {
            pocket_settings_model::save_preset( *pk, name, true );
        }
    } else if( action == "FAV_APPLY_PRESET" ) {
        item_pocket::load_presets();
        uilist selector_menu;
        selector_menu.title = _( "Select a preset" );
        for( const item_pocket::favorite_settings &preset : item_pocket::pocket_presets ) {
            selector_menu.addentry( preset.get_preset_name().value() );
        }
        selector_menu.query();
        if( selector_menu.ret >= 0 ) {
            pocket_settings_model::apply_preset( *pk, selector_menu.ret );
        }
    } else if( action == "FAV_DEL_PRESET" ) {
        item_pocket::load_presets();
        uilist selector_menu;
        for( const item_pocket::favorite_settings &preset : item_pocket::pocket_presets ) {
            selector_menu.addentry( preset.get_preset_name().value() );
        }
        selector_menu.query();
        if( selector_menu.ret >= 0 &&
            query_yn( _( "Are you sure you wish to delete preset %s?" ),
                      item_pocket::pocket_presets[selector_menu.ret].get_preset_name().value() ) ) {
            pocket_settings_model::delete_preset( selector_menu.ret );
        }
    } else if( action == "FAV_WHITELIST_CONTENTS" ) {
        model.whitelist_contents( *pk );
    } else if( action == "FAV_CLEAR" ) {
        if( query_yn( _( "Are you sure you want to clear settings for pocket %d?" ),
                      model.pocket_number_at( menu->selected ) ) ) {
            model.clear( *pk );
        }
    } else if( action == "FAV_CONTEXT_MENU" ) {
        static const std::vector<std::string> ids = {
            "FAV_MOVE_ITEM", "FAV_ITEM", "FAV_CATEGORY", "FAV_WHITELIST", "FAV_BLACKLIST",
            "FAV_PRIORITY", "FAV_AUTO_PICKUP", "FAV_AUTO_UNLOAD", "FAV_SAVE_PRESET",
            "FAV_APPLY_PRESET", "FAV_DEL_PRESET", "FAV_WHITELIST_CONTENTS", "FAV_CLEAR"
        };
        uilist cmenu( _( "Action to take on this pocket" ), {} );
        for( size_t i = 0; i < ids.size(); ++i ) {
            cmenu.addentry( i, true, inp_mngr.get_first_char_for_action( ids[i], "INVENTORY" ),
                            ctxt.get_action_name( ids[i] ) );
        }
        cmenu.query();
        if( cmenu.ret >= 0 && static_cast<size_t>( cmenu.ret ) < ids.size() ) {
            handle_action( ctxt, ids[cmenu.ret], menu );
        }
    } else {
        return false;
    }
    return true;
}
} // namespace

void pocket_management_menu( const std::string &title, const std::vector<item *> &to_organize,
                             Character &who )
{
    static const std::string input_category = "INVENTORY";
    input_context ctxt( input_category );
    const std::string uilist_text = string_format(
                                        _( "Modify pocket settings and move items between pockets.  [<color_yellow>%s</color>] Context menu" ),
                                        ctxt.get_desc( "FAV_CONTEXT_MENU", 1 ) );
    pocket_settings_model model( to_organize );
    uilist pocket_selector;
    pocket_favorite_callback cb( model, uilist_text, who );
    pocket_selector.title = title;
    pocket_selector.text = uilist_text;
    pocket_selector.callback = &cb;
    // fullscreen with the info panel on the right
    pocket_selector.w_x_setup = 0;
    pocket_selector.w_y_setup = 0;
    pocket_selector.w_width_setup = []() -> int {
        return TERMX;
    };
    pocket_selector.w_height_setup = []() -> int {
        return TERMY;
    };
    pocket_selector.pad_right_setup = []() -> int {
        return std::max( TERMX / 2, TERMX - 50 );
    };
    pocket_selector.input_category = input_category;
    for( const char *id : {
             "FAV_PRIORITY", "FAV_AUTO_PICKUP", "FAV_AUTO_UNLOAD", "FAV_ITEM",
             "FAV_CATEGORY", "FAV_WHITELIST", "FAV_BLACKLIST", "FAV_CLEAR", "FAV_MOVE_ITEM",
             "FAV_CONTEXT_MENU", "FAV_SAVE_PRESET", "FAV_APPLY_PRESET", "FAV_DEL_PRESET",
             "FAV_WHITELIST_CONTENTS"
         } ) {
        pocket_selector.additional_actions.emplace_back( id, translation() );
    }
    // Enter opens the context menu instead of confirming
    pocket_selector.allow_confirm = false;
    pocket_selector.allow_additional = true;
    for( const pocket_row &row : model.rows() ) {
        pocket_selector.addentry( row.pocket != nullptr ? 0 : -1, row.enabled, 0, row.text );
    }
    pocket_selector.query();
}
