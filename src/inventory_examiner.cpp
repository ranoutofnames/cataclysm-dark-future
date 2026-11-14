#include "inventory_examiner.h"

#include <algorithm>
#include <vector>

#include "color.h"
#include "item.h"
#include "output.h"
#include "player.h"
#include "point.h"
#include "ui_manager.h"

// CDF: P3-UI3, DDA inventory_ui.cpp:5188-5300. item * for item_location (D1); get_selected() for
// get_highlighted(); select() for highlight( loc ).

inventory_examiner::inventory_examiner( player &p, item *item_to_look_inside,
                                        const inventory_selector_preset &preset ) :
    inventory_selector( p, preset ), parent_item( item_to_look_inside )
{
    force_max_window_size();

    // Space in inventory isn't particularly relevant, so don't display it.
    set_display_stats( false );

    setup();
}

bool inventory_examiner::check_parent_item()
{
    return parent_item != nullptr && !( parent_item->is_container_empty() || empty() );
}

int inventory_examiner::cleanup() const
{
    if( changes_made ) {
        return EXAMINED_CONTENTS_WITH_CHANGES;
    } else {
        return EXAMINED_CONTENTS_UNCHANGED;
    }
}

void inventory_examiner::draw_item_details( const item *sitem )
{
    std::vector<iteminfo> info = sitem->info();
    const std::vector<iteminfo> dummy;

    item_info_data data( sitem->tname( 1, false ), sitem->type_name(), info, dummy,
                         examine_window_scroll );
    data.without_getch = true;

    draw_item_info( w_examine, data );
}

void inventory_examiner::force_max_window_size()
{
    constexpr int border_width = 1;
    _fixed_size = { TERMX / 3 + 2 * border_width, TERMY };
    _fixed_origin = point_zero;
}

int inventory_examiner::execute()
{
    if( !check_parent_item() ) {
        return NO_CONTENTS_TO_EXAMINE;
    }
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();

    ui_adaptor ui_examine;

    ui_examine.on_screen_resize( [&]( ui_adaptor & ui_examine ) {
        force_max_window_size();
        ui->mark_resize();

        const int width = TERMX - _fixed_size.x;
        const int height = TERMY;
        const point start_position( TERMX - width, 0 );

        scroll_item_info_lines = TERMY / 2;

        w_examine = catacurses::newwin( height, width, start_position );
        ui_examine.position_from_window( w_examine );
    } );
    ui_examine.mark_resize();

    ui_examine.on_redraw( [&]( const ui_adaptor & ) {
        const inventory_entry &selected = get_active_column().get_selected();
        if( selected ) {
            if( selected_item != selected.any_item() ) {
                // A new item has been selected, reset scrolling
                examine_window_scroll = 0;
                selected_item = selected.any_item();
            }
            draw_item_details( selected_item );
        }
    } );

    while( true ) {
        /* Since ui_examine is the most recently created ui_adaptor, it will always be redrawn.
         The item list will only be redrawn when specifically invalidated */
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.entry != nullptr ) {
            if( select( input.entry->any_item() ) ) {
                ui_manager::redraw();
            }
            if( input.action == "SELECT" ) {
                return cleanup();
            }
        }

        if( input.action == "QUIT" || input.action == "CONFIRM" ) {
            return cleanup();
        } else if( input.action == "PAGE_UP" || input.action == "SCROLL_UP" ) {
            examine_window_scroll -= scroll_item_info_lines;
        } else if( input.action == "PAGE_DOWN" || input.action == "SCROLL_DOWN" ) {
            examine_window_scroll += scroll_item_info_lines;
        } else {
            ui->invalidate_ui(); // The player is probably doing something that requires updating the base window
            if( input.action == "SHOW_HIDE_CONTENTS" ) {
                changes_made = true;
            }
            on_input( input );
        }
    }
}

void inventory_examiner::setup()
{
    if( parent_item == nullptr ) {
        set_title( "ERROR: Item not found" );
    } else {
        set_title( parent_item->display_name() );
    }
}
