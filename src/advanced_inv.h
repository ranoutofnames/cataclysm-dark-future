#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "advanced_inv_area.h"
#include "advanced_inv_listitem.h"
#include "advanced_inv_pane.h"
#include "cursesdef.h"

class Character;
class input_context;
class item;
struct advanced_inv_save_state;

void create_advanced_inv();

/**
 * Cancels ongoing move all action.
 * TODO: Make this not needed.
 */
void cancel_aim_processing();

// CDF: CAP1b, header numbers of the inventory/worn panes (volume paired with carried_capacity())
struct advanced_inv_inventory_head_data {
    double weight_carried = 0.0;
    double weight_capacity = 0.0;
    std::string volume_carried;
    std::string volume_capacity;
    bool volume_over = false;
};

advanced_inv_inventory_head_data advanced_inv_inventory_head( const Character &who );

// FM6 a1 (DDA advanced_inv.cpp:298-304): true when @p it is the other pane's container or an
// ancestor of it.  Such a row is highlighted and the two panes are joined with a pointer.
bool advanced_inv_is_other_pane_container( const item &it, const item *other_pane_container );
/**
 * FM6 a1: the page-relative line of the other pane's container in @p items, or -1.  @p page_start
 * is the index of the first row of the page and @p lines the number of rows on it.
 */
int advanced_inv_other_cont_line( const std::vector<advanced_inv_listitem> &items, int page_start,
                                  int lines, const item *other_pane_container );

/**
 * FM6 a2/a3 (DDA advanced_inv.cpp:317-373): which header line print_items draws on row 4.
 * The container branch applies only when the container pane really has a container; otherwise the
 * square branch prints the totals of the placeholder square, as DDA does.
 */
enum class advanced_inv_head_kind {
    character,
    container,
    all,
    square
};
advanced_inv_head_kind advanced_inv_head_kind_for( aim_location area, bool has_container );

/** FM6 a2: the container header of DDA advanced_inv.cpp:317-348, split for the two colours. */
struct advanced_inv_container_head_data {
    std::string weight_contained;
    std::string weight_capacity;
    bool weight_over = false;
    std::string volume_contained;
    std::string volume_capacity;
    bool volume_over = false;
};
advanced_inv_container_head_data advanced_inv_container_head( const item &container );
/** FM6 a2: red when over capacity, light green otherwise (DDA advanced_inv.cpp:340-346). */
nc_color advanced_inv_capacity_color( bool over_capacity );

/**
 * FM6 a7 (DDA advanced_inv.cpp:853-865): the page line is drawn on the active pane even for a
 * single page, so an empty list still reads "page 1 of 1".
 */
int advanced_inv_page_count( size_t item_count, int items_per_page );

/**
 * FM6, G46 (DDA advanced_inv.cpp:1127-1156): how much of the source pane's load does not fit in
 * the destination.  @ref over_volume and @ref over_weight are the source totals minus what the
 * destination has free, so they are positive only when something is short.  @ref limitation is
 * DDA's wording for the prompt, empty when everything fits.
 */
struct advanced_inv_move_all_limit {
    units::volume over_volume = 0_ml;
    units::mass over_weight = 0_gram;
    std::string limitation;
};

/**
 * FM6, G46: the move-all pre-check.  @p check_weight is true for the destinations that carry
 * weight (the inventory and a container), as in DDA; a map square has no weight limit.
 */
advanced_inv_move_all_limit advanced_inv_move_all_limit_for(
    units::volume src_volume, units::mass src_weight,
    units::volume dest_volume_free, units::mass dest_weight_free, bool check_weight );

/** FM6 b (DDA advanced_inv.cpp:211-244): the "Sort:" header and menu label of a sort mode. */
std::string advanced_inv_sort_name( advanced_inv_sortby sortby );

/**
 * P3-UI5: the items UNLOAD_CONTAINER takes out of @p container (the top items of its container
 * pockets; no liquids, no favorites).  Split out so a test can see the list.
 */
std::vector<item *> advanced_inv_unload_targets( item &container );

class advanced_inventory
{
    public:
        advanced_inventory();
        ~advanced_inventory();

        void display();

        /**
         * Converts from screen relative location to game-space relative location
         * for control rotation in isometric mode.
        */
        aim_location screen_relative_location( aim_location area );
        std::string get_location_key( aim_location area );

        advanced_inv_area &get_one_square( const aim_location &loc ) {
            return squares[loc];
        }
    private:
        /**
         * Refers to the two panes, used as index into @ref panes.
         */
        enum side {
            left  = 0,
            right = 1,
            NUM_PANES = 2
        };
        static constexpr int head_height = 5;

        // swap the panes and windows via std::swap()
        void swap_panes();

        // minimap that displays things around character
        catacurses::window minimap;
        catacurses::window mm_border;
        const int minimap_width  = 3;
        const int minimap_height = 3;
        void draw_minimap();
        void refresh_minimap();
        char get_minimap_sym( side p ) const;

        bool inCategoryMode = false;

        int itemsPerPage = 0;
        int w_height = 0;
        int w_width = 0;

        int headstart = 0;
        int colstart = 0;

        bool recalc = false;
        /**
         * Which panels is active (item moved from there).
         */
        side src;
        /**
         * Which panel is the destination (items want to go to there).
         */
        side dest;
        /**
         * True if (and only if) the filter of the active panel is currently
         * being edited.
         */
        bool filter_edit = false;
        /**
         * Two panels (left and right) showing the items, use a value of @ref side
         * as index.
         */
        std::array<advanced_inventory_pane, NUM_PANES> panes;
        static const advanced_inventory_pane null_pane;
        std::array<advanced_inv_area, NUM_AIM_LOCATIONS> squares;

        catacurses::window head;

        bool exit = false;

        advanced_inv_save_state *save_state;

        /**
         * registers all the ctxt for display()
         */
        input_context register_ctxt() const;
        /**
         *  a smaller chunk of display()
         */
        void start_activity( aim_location destarea, advanced_inv_listitem *sitem, int &amount_to_move,
                             bool from_vehicle, bool to_vehicle ) const;

        /**
         * returns whether the display loop exits or not
         */
        bool action_move_item( advanced_inv_listitem *sitem,
                               advanced_inventory_pane &dpane, const advanced_inventory_pane &spane,
                               const std::string &action );

        void action_examine( advanced_inv_listitem *sitem, advanced_inventory_pane &spane );

        /** Unload the selected container (DDA advanced_inv.cpp:1823-1841); returns true if it did. */
        bool action_unload( advanced_inv_listitem *sitem, advanced_inventory_pane &spane,
                            advanced_inventory_pane &dpane );

        // store/load settings (such as index, filter, etc)
        void save_settings( bool only_panes );
        void load_settings();
        // used to return back to AIM when other activities queued are finished
        void do_return_entry();
        // returns true if currently processing a routine
        // (such as `MOVE_ALL_ITEMS' with `AIM_ALL' source)
        bool is_processing() const;

        static std::string get_sortname( advanced_inv_sortby sortby );
        bool move_all_items( bool nested_call = false );
        /**
         * FM6, G46 (DDA advanced_inv.cpp:1127-1189): does the destination have room and weight
         * capacity for everything in the source pane?  If not, ask whether to move as much as
         * fits, naming what is short.  Move-all may proceed only if the player agrees.
         */
        bool move_all_fits( advanced_inventory_pane &spane, advanced_inventory_pane &dpane );
        /**
         * P3-UI5: move_all_items for a pane that shows a container (as the source or as the
         * destination).  The list is built from the rows of the source pane, not from a map tile.
         * @return true if an activity was queued
         */
        bool move_all_container_items( advanced_inventory_pane &spane, advanced_inventory_pane &dpane,
                                       bool nested_call );
        void print_items( advanced_inventory_pane &pane, bool active );
        void recalc_pane( side p );
        /**
         * P3-UI5: a container shown by a pane can be destroyed or moved while the advanced
         * inventory is open.  Panes whose container is gone fall back to the inventory.
         * @return true if a pane was reset
         */
        bool validate_pane_containers();
        void redraw_pane( side p );
        void redraw_sidebar();
        // Returns the x coordinate where the header started. The header is
        // displayed right of it, everything left of it is till free.
        int print_header( advanced_inventory_pane &pane, aim_location sel );
        void init();
        /**
         * Translate an action ident from the input context to an aim_location.
         * @param action Action ident to translate
         * @param ret If the action ident referred to a location, its id is stored
         * here. Only valid when the function returns true.
         * @return true if the action did refer to an location (which has been
         * stored in ret), false otherwise.
         */
        bool get_square( const std::string &action, aim_location &ret );
        void change_square( aim_location changeSquare, advanced_inventory_pane &dpane,
                            advanced_inventory_pane &spane );
        /**
         * Show the sort-by menu and change the sorting of this pane accordingly.
         * @return whether the sort order was actually changed.
         */
        bool show_sort_menu( advanced_inventory_pane &pane );
        /**
         * Checks whether one can put items into the supplied location.
         * If the supplied location is AIM_ALL, query for the actual location
         * (stores the result in def) and check that destination.
         * @return false if one can not put items in the destination, true otherwise.
         * The result true also indicates the def is not AIM_ALL (because the
         * actual location has been queried).
         */
        bool query_destination( aim_location &def );
        /**
         * Setup how many items/charges (if counted by charges) should be moved.
         * @param destarea Where to move to. This must not be AIM_ALL.
         * @param sitem The source item, it must contain a valid reference to an item!
         * @param action The action we are querying
         * @param amount The input value is ignored, contains the amount that should
         *      be moved. Only valid if this returns true.
         * @return false if nothing should/can be moved. True only if there can and
         *      should be moved. A return value of true indicates that amount now contains
         *      a valid item count to be moved.
         */
        bool query_charges( aim_location destarea, const advanced_inv_listitem &sitem,
                            const std::string &action, int &amount );
};
