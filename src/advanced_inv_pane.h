#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "advanced_inv_area.h"
#include "advanced_inv_listitem.h"
#include "cursesdef.h"
#include "units.h"

class item;
struct advanced_inv_pane_save_state;

enum aim_location : char;

enum advanced_inv_sortby {
    SORTBY_NONE,
    SORTBY_NAME,
    SORTBY_WEIGHT,
    SORTBY_VOLUME,
    SORTBY_CHARGES,
    SORTBY_CATEGORY,
    SORTBY_DAMAGE,
    SORTBY_AMMO,
    SORTBY_SPOILAGE,
    SORTBY_PRICE
};

/**
 * P3-UI5: what ITEMS_PARENT does for a pane that shows a container (DDA advanced_inv.cpp:1384-1428).
 */
enum class aim_parent_kind {
    // the pane shows no container
    none,
    // show @ref aim_parent_target::container, the container that holds the current one
    container,
    // leave the container view and show @ref aim_parent_target::area
    area,
    // the parent is the container the other pane shows: swap the panes
    swap_panes
};

struct aim_parent_target {
    aim_parent_kind kind = aim_parent_kind::none;
    item *container = nullptr;
    aim_location area = NUM_AIM_LOCATIONS;
};

/**
 * Displayed pane, what is shown on the screen.
 */
class advanced_inventory_pane
{
    private:
        aim_location area = NUM_AIM_LOCATIONS;
        aim_location prev_area = area;
        // pointer to the square this pane is pointing to
        bool viewing_cargo = false;
        bool prev_viewing_cargo = false;
    public:
        // set the pane's area via its square, and whether it is viewing a vehicle's cargo
        void set_area( const advanced_inv_area &square, bool in_vehicle_cargo = false ) {
            prev_area = area;
            prev_viewing_cargo = viewing_cargo;
            area = square.id;
            viewing_cargo = square.can_store_in_vehicle() && in_vehicle_cargo;
        }
        void restore_area() {
            area = prev_area;
            viewing_cargo = prev_viewing_cargo;
        }
        aim_location get_area() const {
            return area;
        }
        bool prev_in_vehicle() const {
            return prev_viewing_cargo;
        }
        bool in_vehicle() const {
            return viewing_cargo;
        }
        /**
         * P3-UI5: the container this pane shows when its area is AIM_CONTAINER (DDA
         * advanced_inv_pane.h:78-86).  It is per pane, so each pane can be inside its own container.
         */
        item *get_container() const;
        /** The area the pane was showing before it entered its (outermost) container. */
        aim_location get_container_base_loc() const;
        /**
         * Show the contents of @p target in this pane.  The pane remembers the area it came from
         * the first time it enters a container.  @p container_square is the AIM_CONTAINER square.
         */
        void enter_container( item *target, const advanced_inv_area &container_square );
        /** Forget the container (the caller switches the area). */
        void leave_container();
        /** What ITEMS_PARENT does now.  @p other_pane_container is the other pane's container. */
        aim_parent_target parent_target( const item *other_pane_container ) const;
        /** Select this item after the next recalculation (the container we just left). */
        item *target_item_after_recalc = nullptr;
        advanced_inv_pane_save_state *save_state;
        void save_settings();
        void load_settings( int saved_area_idx,
                            const std::array<advanced_inv_area, NUM_AIM_LOCATIONS> &squares, bool is_re_enter );
        /**
         * Index of the selected item (index of @ref items),
         */
        int index = 0;
        advanced_inv_sortby sortby = advanced_inv_sortby::SORTBY_NONE;
        catacurses::window window;
        std::vector<advanced_inv_listitem> items;
        /**
         * The current filter string.
         */
        std::string filter;
        /**
         * Whether to recalculate the content of this pane.
         */
        bool recalc = false;

        void add_items_from_area( advanced_inv_area &square, bool vehicle_override = false );
        /**
         * Makes sure the @ref index is valid (if possible).
         */
        void fix_index();
        /**
         * @param it The item to check, oly the name member is examined.
         * @return Whether the item should be filtered (and not shown).
         */
        bool is_filtered( const advanced_inv_listitem &it ) const;
        /**
         * Same as the other, but checks the real item.
         */
        bool is_filtered( const item &it ) const;
        /**
         * Scroll @ref index, by given offset,
         * @param offset Must not be 0.
         */
        void scroll_by( int offset );
        /**
         * Scroll the index in category mode by given offset.
         * @param offset Must be either +1 or -1
         */
        void scroll_category( int offset );
        /**
         * Scroll by a page of @p linesPerPage rows (DDA advanced_inv_pane.cpp:338-395).  Stops at the
         * first or last row, and wraps when it already is there.
         * @param offset Must be either +1 or -1
         */
        void scroll_page( int linesPerPage, int offset );
        /** HOME: the first row. */
        void scroll_to_start();
        /** END: the last row. */
        void scroll_to_end();
        /**
         * Free volume of what this pane shows: the container, the inventory or the square.
         * @param square The square of this pane's area (not AIM_ALL).
         */
        units::volume free_volume( const advanced_inv_area &square ) const;
        /** Free weight capacity of the container or the inventory, no limit for a square. */
        units::mass free_weight_capacity() const;
        /**
         * @return either null, if @ref index is invalid, or the selected
         * item in @ref items.
         */
        advanced_inv_listitem *get_cur_item_ptr();
        /**
         * Set the filter string, disables filtering when the filter string is empty.
         */
        void set_filter( const std::string &new_filter );
        /**
         * Insert additional category headers on the top of each page.
         */
        void paginate( size_t itemsPerPage );
    private:
        item *container = nullptr;
        aim_location container_base_loc = NUM_AIM_LOCATIONS;
        /** Scroll to next non-header entry */
        void skip_category_headers( int offset );
        /** Only add offset to index, but wrap around! */
        void mod_index( int offset );

        mutable std::map<std::string, std::function<bool( const item & )>> filtercache;
};
