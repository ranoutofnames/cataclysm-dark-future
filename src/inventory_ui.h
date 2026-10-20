#pragma once

#include <cassert>
#include <climits>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <array>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "color.h"
#include "cursesdef.h"
#include "detached_ptr.h"
#include "input.h"
#include "item_handling_util.h"
#include "item_pocket.h"
#include "memory_fast.h"
#include "pimpl.h"
#include "units.h"
#include "pickup_token.h"
#include "type_id.h"

class Character;
class player;
class item;
class item_category;
class string_input_popup;
struct tripoint;
class ui_adaptor;

using excluded_stack = std::pair<item *, int>;
using excluded_stacks = std::map<item *, int>;

enum class navigation_mode : int {
    ITEM = 0,
    CATEGORY
};

enum class scroll_direction : int {
    FORWARD = 1,
    BACKWARD = -1
};

struct navigation_mode_data;
struct inventory_input;

/// Display-only key hint. Doesn't participate in inventory-letter lookup.
struct inventory_invlet_hint {
    int invlet;
    nc_color color;
};

/// Per DDA inventory_ui.h:60-75. CDF: item * instead of item_location.
struct collation_meta_t {
    item *tip;
    bool collapsed = true;
    bool enabled = true;
};

class inventory_entry
{
    public:
        std::vector<item *> locations;

        size_t chosen_count = 0;
        /// Topmost visible parent in an unbroken chain (hierarchy mode), per DDA inventory_ui.h:190.
        const item *topmost_parent = nullptr;
        /// For collation: true = header, false = entry.
        /// True when the entry has visible contents listed below it (hierarchy mode).
        bool chevron = false;
        /// Collation metadata shared by a header and its entries (per DDA inventory_ui.h:186-212).
        std::shared_ptr<collation_meta_t> collation_meta;
        /// Indent of a contained entry (hierarchy mode).
        int indent = 0;
        /// Additional items selected by the automatic drop preview.
        size_t automatic_drop_count = 0;
        /// Position of this stack in the predicted drop sequence.
        std::optional<size_t> drop_order;
        int custom_invlet = INT_MIN;
        /// Drawn when the entry has no letter of its own.
        std::optional<inventory_invlet_hint> invlet_hint;
        std::string cached_name;

        inventory_entry() = default;

        inventory_entry( const item_category *custom_category ) :
            custom_category( custom_category )
        {}

        // Copy with new category.  Used to copy entries into the "selected"
        // category when they are selected.
        inventory_entry( const inventory_entry &entry, const item_category *custom_category ) :
            inventory_entry( entry ) {
            this->custom_category = custom_category;
        }

        inventory_entry( const std::vector<item *> &locations,
                         const item_category *custom_category = nullptr,
                         bool enabled = true ) :
            locations( locations ),
            custom_category( custom_category ),
            enabled( enabled )
        {}

        bool operator==( const inventory_entry &other ) const;
        bool operator!=( const inventory_entry &other ) const {
            return !( *this == other );
        }

        operator bool() const {
            return !is_null();
        }
        /** Whether the entry is null (dummy) */
        bool is_null() const {
            return get_category_ptr() == nullptr;
        }
        /**
         * Whether the entry is an item.
         */
        bool is_item() const {
            return !locations.empty();
        }
        /** Whether the entry is a category */
        bool is_category() const {
            return !is_null() && !is_item();
        }
        /** Whether the entry can be selected */
        bool is_selectable() const {
            return is_item() && enabled;
        }
        /// Per DDA inventory_ui.h:148-162.
        bool is_collated() const {
            return static_cast<bool>( collation_meta );
        }

        bool is_collation_header() const {
            return is_collated() && chevron;
        }

        bool is_collation_entry() const {
            return is_collated() && !chevron;
        }

        /// Per DDA inventory_ui.h:59-64.
        void reset_collation() {
            if( is_collation_header() ) {
                chevron = false;
            }
            collation_meta.reset();
        }

        item *any_item() const {
            assert( !locations.empty() );
            return locations.front();
        }

        /** Pointer to first item in relevant stack on character. */
        item *item_stack_on_character() const {
            assert( !locations.empty() );
            return locations.front();
        }

        size_t get_stack_size() const {
            return locations.size();
        }

        int get_total_charges() const;
        int get_selected_charges() const;

        size_t get_available_count() const;
        const item_category *get_category_ptr() const;
        /// Per DDA inventory_ui.h:201. CDF: no cached_category_ptr to reset.
        void set_custom_category( const item_category *category ) {
            custom_category = category;
        }
        int get_invlet() const;
        nc_color get_invlet_color() const;
        void update_cache();

    private:
        const item_category *custom_category = nullptr;
        bool enabled = true;

};

class inventory_selector_preset
{
    public:
        inventory_selector_preset();
        virtual ~inventory_selector_preset() = default;
        // CDF: collation hook (DDA inventory_ui.h:290); body deferred, always false in 5-1.
        virtual bool collate_entries() const {
            return false;
        }

        /** Does this entry satisfy the basic preset conditions? */
        virtual bool is_shown( const item * ) const {
            return true;
        }

        /**
         * The reason why this entry cannot be selected.
         * @return Either the reason of denial or empty string if it's accepted.
         */
        virtual std::string get_denial( const item * ) const {
            return std::string();
        }
        /** Whether the first item is considered to go before the second. */
        virtual bool sort_compare( const inventory_entry &lhs, const inventory_entry &rhs ) const;
        /** Color that will be used to display the entry string. */
        virtual nc_color get_color( const inventory_entry &entry ) const;

        std::string get_denial( const inventory_entry &entry ) const;
        /** Text in the cell */
        std::string get_cell_text( const inventory_entry &entry, size_t cell_index ) const;
        /** @return Whether the cell is a stub */
        bool is_stub_cell( const inventory_entry &entry, size_t cell_index ) const;
        /** Number of cells in the preset. */
        size_t get_cells_count() const {
            return cells.size();
        }
        /** Whether items should make new stacks if components differ */
        bool get_checking_components() const {
            return check_components;
        }

        virtual std::function<bool( const inventory_entry & )> get_filter( const std::string &filter )
        const;

    protected:
        /** Text of the first column (default: item name) */
        virtual std::string get_caption( const inventory_entry &entry ) const;
        /**
         * Append a new cell to the preset.
         * @param func The function that returns text for the cell.
         * @param title Title of the cell.
         * @param stub The cell won't be "revealed" if it contains only this value
         */
        void append_cell( const std::function<std::string( const item * )> &func,
                          const std::string &title = std::string(),
                          const std::string &stub = std::string() );
        void append_cell( const std::function<std::string( const inventory_entry & )> &func,
                          const std::string &title = std::string(),
                          const std::string &stub = std::string() );
        bool check_components = false;

    private:
        class cell_t
        {
            public:
                cell_t( const std::function<std::string( const inventory_entry & )> &func,
                        const std::string &title, const std::string &stub ) :
                    title( title ),
                    stub( stub ),
                    func( func ) {}

                std::string get_text( const inventory_entry &entry ) const;

                std::string title;
                std::string stub;

            private:
                std::function<std::string( const inventory_entry & )> func;
        };

        std::vector<cell_t> cells;
};

const inventory_selector_preset default_preset;

class pickup_inventory_preset : public inventory_selector_preset
{
    public:
        pickup_inventory_preset( const player &p ) : p( p ) {}
        std::string get_denial( const item *loc ) const override;

    private:
        const player &p;
};

class inventory_column
{
    public:
        inventory_column( const inventory_selector_preset &preset = default_preset ) : preset( preset ) {
            cells.resize( preset.get_cells_count() );
        }

        virtual ~inventory_column() = default;

        bool empty() const {
            return entries.empty();
        }
        /**
         * Can this column be activated?
         * @return Whether the column contains selectable entries.
         * Note: independent from 'allows_selecting'
         */
        virtual bool activatable() const;
        /** Is this column visible? */
        bool visible() const {
            return !empty() && visibility && preset.get_cells_count() > 0;
        }
        /**
         * Does this column allow selecting?
         * "Cosmetic" columns (list of selected items) can explicitly prohibit selecting.
         * Note: independent from 'activatable'
         */
        virtual bool allows_selecting() const {
            return true;
        }

        size_t page_index() const {
            return page_of( page_offset );
        }

        size_t pages_count() const {
            return page_of( entries.size() + entries_per_page - 1 );
        }

        bool has_available_choices() const;
        bool is_selected( const inventory_entry &entry ) const;

        /**
         * Does this entry belong to the selected category?
         * When @ref navigation_mode::ITEM is used it's equivalent to @ref is_selected().
         */
        bool is_selected_by_category( const inventory_entry &entry ) const;

        const inventory_entry &get_selected() const;
        std::vector<inventory_entry *> get_all_selected() const;
        std::vector<inventory_entry *> get_entries(
            const std::function<bool( const inventory_entry &entry )> &filter_func ) const;
        std::vector<inventory_entry *> get_all_entries(
            const std::function<bool( const inventory_entry &entry )> &filter_func ) const;
        std::vector<inventory_entry *> get_all_entries() const;
        inventory_entry *find_by_invlet( int invlet ) const;

        void draw( const catacurses::window &win, point pos ) const;

        void add_entry( const inventory_entry &entry );
        void move_entries_to( inventory_column &dest );
        // Per DDA inventory_ui.h:510-518.
        void collate();
        void uncollate();
        void set_collapsed( inventory_entry &entry, bool collapse );
        void clear();
        void set_stack_favorite( const item *location, bool favorite );

        /** Selects the specified location. */
        bool select( const item *loc );
        auto select_position_if_item_type( size_t new_index, const itype_id &type ) -> bool;

        /**
         * Change the selection.
         * @param new_index Index of the entry to select.
         * @param dir If the entry is not selectable, move in the specified direction
         */
        void select( size_t new_index, scroll_direction dir );

        /// Per DDA inventory_ui.h:538-543.
        bool collated_sort_compare( inventory_entry const &lhs, inventory_entry const &rhs );

        size_t get_selected_index() {
            return selected_index;
        }

        void set_multiselect( bool multiselect ) {
            this->multiselect = multiselect;
        }

        void set_visibility( bool visibility ) {
            this->visibility = visibility;
        }

        void set_width( size_t new_width, const std::vector<inventory_column *> &all_columns );
        void set_height( size_t new_height );
        size_t get_width() const;
        size_t get_height() const;
        /** Expands the column to fit the new entry. */
        void expand_to_fit( const inventory_entry &entry );
        /** Resets width to original (unchanged). */
        virtual void reset_width( const std::vector<inventory_column *> &all_columns );
        /** Returns next custom inventory letter. */
        int reassign_custom_invlets( const player &p, int min_invlet, int max_invlet );
        /** Reorder entries, repopulate titles, adjust to the new height. */
        virtual void prepare_paging( const std::string &filter = "" );
        /**
         * Event handlers
         */
        virtual void on_input( const inventory_input &input );
        /** The entry has been changed. */
        virtual void on_change( const inventory_entry & ) {}
        /** The column has been activated. */
        virtual void on_activate() {
            active = true;
        }
        /** The column has been deactivated. */
        virtual void on_deactivate() {
            active = false;
        }
        /** Selection mode has been changed. */
        virtual void on_mode_change( navigation_mode mode ) {
            this->mode = mode;
        }

        void set_filter( const std::string &filter );
        void invalidate_paging() {
            paging_is_valid = false;
        }

        std::vector<inventory_entry> entries;
        std::vector<inventory_entry> entries_hidden;
        bool paging_is_valid = false;
        /** Hierarchy mode (per DDA indent_entries): children sort under their parents, indented. */
        void set_hierarchy( bool value ) {
            hierarchy = value;
            invalidate_paging();
        }
        bool hierarchy = false;
        // Per DDA inventory_ui.h:494-500.
        bool collate_entries() const {
            return preset.collate_entries();
        }

    protected:
        struct entry_cell_cache_t {
            bool assigned = false;
            nc_color color = c_unset;
            std::string denial;
            std::vector<std::string> text;
        };

        /**
         * Move the selection.
         */
        void move_selection( scroll_direction dir );
        void move_selection_page( scroll_direction dir );

        size_t next_selectable_index( size_t index, scroll_direction dir ) const;

        size_t page_of( size_t index ) const;
        size_t page_of( const inventory_entry &entry ) const;
        /**
         * Indentation of the entry.
         * @param entry The entry to check
         * @returns Either left indent when it's zero, or a gap between cells.
         */
        size_t get_entry_indent( const inventory_entry &entry ) const;
        /**
         *  Overall cell width.
         *  If corresponding cell is not empty (its width is greater than zero),
         *  then a value returned by  inventory_column::get_entry_indent() is added to the result.
         */
        size_t get_entry_cell_width( size_t index, size_t cell_index ) const;
        size_t get_entry_cell_width( const inventory_entry &entry, size_t cell_index ) const;
        /** Sum of the cell widths */
        size_t get_cells_width() const;

        entry_cell_cache_t make_entry_cell_cache( const inventory_entry &entry ) const;
        const entry_cell_cache_t &get_entry_cell_cache( size_t index ) const;

        void refresh_entry_cell_caches();
        const inventory_selector_preset &preset;
        navigation_mode mode = navigation_mode::ITEM;
        bool active = false;
        bool multiselect = false;
        bool visibility = true;

        size_t selected_index = 0;
        size_t page_offset = 0;
        size_t entries_per_page = std::numeric_limits<size_t>::max();
        size_t height = std::numeric_limits<size_t>::max();
        size_t reserved_width = 0;

    private:
        struct cell_t {
            size_t current_width = 0;   /// Current cell widths (can be affected by set_width())
            size_t real_width = 0;      /// Minimal cell widths (to embrace all the entries nicely)

            bool visible() const {
                return current_width > 0;
            }
            /** @return Gap before the cell. Negative value means the cell is shrunk */
            int gap() const {
                return current_width - real_width;
            }
        };

        std::vector<cell_t> cells;
        mutable std::vector<entry_cell_cache_t> entries_cell_cache;

        /** @return Number of visible cells */
        size_t visible_cells() const;

        // Per DDA inventory_ui.h:596-605.
        static void _reset_collation( std::vector<inventory_entry> &entries );
        bool _collated = false;
};

class selection_column : public inventory_column
{
    public:
        selection_column( const std::string &id, const std::string &name );
        ~selection_column() override;

        bool activatable() const override {
            return inventory_column::activatable() && pages_count() > 1;
        }

        bool allows_selecting() const override {
            return false;
        }

        void reset_width( const std::vector<inventory_column *> &all_columns ) override;

        void prepare_paging( const std::string &filter = "" ) override;

        void on_change( const inventory_entry &entry ) override;
        void on_mode_change( navigation_mode ) override {
            // Intentionally ignore mode change.
        }

    private:
        const pimpl<item_category> selected_cat;
        inventory_entry last_changed;
};

class inventory_selector
{
    public:
        inventory_selector( player &u, const inventory_selector_preset &preset = default_preset );
        virtual ~inventory_selector();
        /** These functions add items from map / vehicles. */
        void add_character_items( Character &character );
        void add_map_items( const tripoint_bub_ms &target );
        void add_vehicle_items( const tripoint_bub_ms &target );
        void add_nearby_items( int radius = 1 );
        void add_fake_items( Character &character );
        /** Per DDA inventory_ui.cpp:3821: switch between category columns and the container tree. */
        void toggle_categorize_contained();
        /** Registers VIEW_CATEGORY_MODE and shows its footer label (opt-in per selector). */
        void enable_view_mode_toggle();

        /** Per DDA inventory_ui.h:941: category columns (default) or the container hierarchy. */
        enum class uimode : int {
            categories = 0,
            hierarchy,
            last,
        };
        /** Remove all items */
        void clear_items();
        /** Assigns a title that will be shown on top of the menu. */
        void set_title( const std::string &title ) {
            this->title = title;
        }
        /** Assigns a hint. */
        void set_hint( const std::string &hint ) {
            this->hint = hint;
        }
        /** Specify whether the header should show stats (weight and volume). */
        void set_display_stats( bool display_stats ) {
            this->display_stats = display_stats;
        }
        /** @return true when the selector is empty. */
        bool empty() const;
        /** @return true when there are enabled entries to select. */
        bool has_available_choices() const;
        /** Per DDA inventory_ui.h:687. The single available choice, if there is exactly one. */
        drop_location get_only_choice() const;

        /** Apply filter string to all columns */
        void set_filter( const std::string &str );
        /** Get last filter string set by set_filter or entered by player */
        std::string get_filter() const;

        /** Actions you can take from inventory menus */
        bool wield( inventory_entry &entry );
        bool wear( inventory_entry &entry );

        // An array of cells for the stat lines. Example: ["Weight (kg)", "10", "/", "20"].
        using stat = std::array<std::string, 4>;
        using stats = std::array<stat, 2>; // legacy
        // Per DDA inventory_ui.h:721: a header line is a list of cells; header_stats_tab_stop
        // cells mark where the lines of a block are aligned.
        using header_stats_line = std::vector<std::string>;
        using header_stats = std::vector<header_stats_line>;
        static header_stats convert_stats_to_header_stats( const stats &old_stats );

        bool keep_open = false;

        void add_item( inventory_column &target_column,
                       item *location,
                       const item_category *custom_category = nullptr );

        void remove_item( item *location );

    protected:
        player &u;
        const inventory_selector_preset &preset;
        /// Per DDA inventory_ui.h:885. Off by default.
        bool force_single_column = false;

        /**
         * The input context for navigation, already contains some actions for movement.
         */
        input_context ctxt;

        const item_category *naturalize_category( const item_category &category,
                const tripoint_bub_ms &pos );

        void add_entry( inventory_column &target_column,
                        std::vector<item *> &&locations,
                        const item_category *custom_category = nullptr );
        /** Per DDA inventory_ui.cpp:1995, add_shown_entry: the preset check was already done. */
        void add_shown_entry( inventory_column &target_column, std::vector<item *> &&locations,
                              const item_category *custom_category, const item *topmost_parent,
                              bool chevron, int indent );
        /** Per DDA inventory_ui.cpp:2018. Adds loc and its contents; true if anything is visible. */
        bool add_entry_rec( inventory_column &entry_column, inventory_column &children_column,
                            item *loc, const item_category *entry_category,
                            const item_category *children_category, const item *topmost_parent,
                            int indent );
        /** Per DDA inventory_ui.cpp:2105. Adds the items inside container to column. */
        bool add_contained_items( item *container, inventory_column &column,
                                  const item_category *custom_category = nullptr,
                                  const item *topmost_parent = nullptr, int indent = 0 );

        uimode _uimode = uimode::categories;


        void add_items( inventory_column &target_column,
                        const std::function<item*( item * )> &locator,
                        const std::vector<std::list<item *>> &stacks,
                        const item_category *custom_category = nullptr );

        inventory_input get_input();

        /** Given an action from the input_context, try to act according to it. */
        void on_input( const inventory_input &input );
        /** Entry has been changed */
        void on_change( const inventory_entry &entry );

        shared_ptr_fast<ui_adaptor> create_or_get_ui_adaptor();

        size_t get_layout_width() const;
        size_t get_layout_height() const;

        void set_filter();

        /** Tackles screen overflow */
        virtual void rearrange_columns( size_t client_width );

        /// Refreshes display-only key hints after layout or wield/wear actions.
        virtual auto assign_invlet_hints() -> void {}

        static stats get_weight_and_volume_stats(
            units::mass weight_carried, units::mass weight_capacity,
            const units::volume &volume_carried, const units::volume &volume_capacity );

        // Per DDA inventory_ui.h:766-835.
        static header_stats_line build_weight_and_volume_stats_line(
            units::mass weight_carried, units::mass weight_capacity, const std::string &weight_label,
            const units::volume &volume_in_pockets, const units::volume &volume_of_pockets,
            const std::string &volume_label );
        static header_stats_line build_selection_stats_line( units::volume volume, units::mass weight );
        static header_stats_line build_pocket_stats_line(
            const std::string &prefix, const item_pocket *free_pocket,
            units::volume free_pocket_volume, units::length free_pocket_length,
            int free_pocket_copies );
        static header_stats_line build_pocket_stats_line(
            const std::string &prefix, const item_pocket *free_pocket,
            units::volume free_pocket_volume, units::length free_pocket_length,
            int free_pocket_copies, const item_pocket *max_pocket,
            units::volume max_pocket_volume, units::length max_pocket_length );
        static bool pockets_match( const pocket_with_constraint &a, const pocket_with_constraint &b );
        struct space_stats_text {
            std::string volume;
            std::string length;
        };
        /** Strings describing the volume and length of a space. */
        static space_stats_text build_space_stats( units::volume size, units::length min_length,
                units::length max_length, bool is_restricted );
        /** Strings to use when such a space doesn't exist. */
        static space_stats_text build_invalid_space_stats();
        /** A multiline overview of the space available in @p pockets, with the constraints
         * from their outer pockets. If @p show_unconstrained_max_space is set the "max" lines
         * report the pocket's own size, ignoring outer constraints. */
        static header_stats get_pocket_summary_header_stats(
            const units::mass &weight_carried, const units::mass &weight_capacity,
            const units::volume &volume_in_pockets, const units::volume &volume_of_pockets,
            std::vector<pocket_with_constraint> pockets, bool show_unconstrained_max_space,
            const std::string *weight_label_override = nullptr,
            const std::string *volume_label_override = nullptr );
        // This cell marks where text rows are aligned.
        // NOLINTNEXTLINE(cata-text-style): the tab is not printed.
        static constexpr const char *header_stats_tab_stop = "	";

        /** Get stats to display in top right.
         *
         * By default, computes the pocket summary for @c u */
        virtual header_stats get_raw_stats() const;

        std::vector<std::string> get_stats() const;
        std::pair<std::string, nc_color> get_footer( navigation_mode m ) const;

        size_t get_header_height() const;
        size_t get_header_min_width() const;
        size_t get_footer_min_width() const;

        /** @return an entry from all entries by its invlet */
        inventory_entry *find_entry_by_invlet( int invlet ) const;

        const std::vector<inventory_column *> &get_all_columns() const {
            return columns;
        }
        std::vector<inventory_column *> get_visible_columns() const;

    private:
        // These functions are called from resizing/redraw callbacks of ui_adaptor
        // and should not be made protected or public.
        void prepare_layout( size_t client_width, size_t client_height );
        void prepare_layout();

        void resize_window( int width, int height );
        void refresh_window() const;

        void draw_header( const catacurses::window &w ) const;
        void draw_footer( const catacurses::window &w ) const;
        void draw_columns( const catacurses::window &w ) const;
        void draw_frame( const catacurses::window &w ) const;

    public:
        /**
         * Select a location
         * @param loc Location to select
         * @return true on success.
         */
        bool select( const item *loc );
        auto select_item_type( const itype_id &type ) -> bool;
        auto select_item_type( const itype_id &type, size_t preferred_column ) -> bool;
        auto select_position_if_item_type( std::pair<size_t, size_t> position,
                                           const itype_id &type ) -> bool;
        auto restore_selection( std::pair<size_t, size_t> position, const itype_id &type ) -> bool;

        const inventory_entry &get_selected() {
            return get_active_column().get_selected();
        }

        void select_position( std::pair<size_t, size_t> position ) {
            prepare_layout();
            set_active_column( position.first );
            get_active_column().select( position.second, scroll_direction::BACKWARD );
        }

        std::pair<size_t, size_t> get_selection_position() {
            std::pair<size_t, size_t> position;
            position.first = active_column_index;
            position.second = get_active_column().get_selected_index();
            return position;
        }

        inventory_column &get_column( size_t index ) const;
        inventory_column &get_active_column() const {
            return get_column( active_column_index );
        }

        void set_active_column( size_t index );

        inventory_column own_gear_column;    // Column for own gear (weapon, armor) items
        inventory_column own_inv_column;     // Column for own inventory items
        inventory_column map_column;         // Column for map and vehicle items
    protected:
        size_t get_columns_width( const std::vector<inventory_column *> &columns ) const;
        /** @return Percentage of the window occupied by columns */
        double get_columns_occupancy_ratio( size_t client_width ) const;
        /** @return Do the visible columns need to be center-aligned */
        bool are_columns_centered( size_t client_width ) const;
        /** @return Are visible columns wider than available width */
        bool is_overflown( size_t client_width ) const;

        bool is_active_column( const inventory_column &column ) const {
            return &column == &get_active_column();
        }

        void append_column( inventory_column &column );

        /**
         * Activates either previous or next column.
         * @param dir Forward - next column, backward - previous.
         */
        void toggle_active_column( scroll_direction dir );

        void refresh_active_column() {
            if( !get_active_column().activatable() ) {
                toggle_active_column( scroll_direction::FORWARD );
            }
        }
        void toggle_navigation_mode();

        const navigation_mode_data &get_navigation_data( navigation_mode m ) const;
        std::unique_ptr<string_input_popup> spopup;
        weak_ptr_fast<ui_adaptor> ui;

    private:
        catacurses::window w_inv;


        std::vector<inventory_column *> columns;

        std::string title;
        std::string hint;
        size_t active_column_index;
        std::list<item_category> categories;
        navigation_mode mode;

        const int border = 1;                // Width of the window border
        std::string filter;

        bool is_empty = true;
        bool display_stats = true;
        bool view_toggle_enabled = false;
        int nearby_radius = -1;
        bool fake_items_added = false;

    public:
        std::string action_bound_to_key( char key ) const;
        /** Returns all keys in the current context which are bound to an action. Warning: may contain duplicates. */
        std::vector<char> all_bound_keys( ) const;
};

inventory_selector::stat display_stat( const std::string &caption, int cur_value, int max_value,
                                       const std::function<std::string( int )> &disp_func );

class inventory_pick_selector : public inventory_selector
{
    public:
        inventory_pick_selector( player &p,
                                 const inventory_selector_preset &preset = default_preset ) :
            inventory_selector( p, preset ) {}

        item *execute();

    protected:
        virtual auto handle_action( const std::string &/*action*/ ) -> bool {
            return false;
        }

        /// Receives a key that matched no selectable letter and no picker action.
        /// @return The item to pick, or nullptr to treat the key as before.
        virtual auto pick_by_unbound_key( int /*key*/ ) -> item * { return nullptr; } // *NOPAD*
};

class inventory_multiselector : public inventory_selector
{
    public:
        inventory_multiselector( player &p, const inventory_selector_preset &preset = default_preset,
                                 const std::string &selection_column_title = "" );
    protected:
        void rearrange_columns( size_t client_width ) override;
        size_t query_count( size_t count );
        void set_chosen_count( inventory_entry &entry, size_t count );
        std::vector<inventory_entry *> get_selection_column_items() const;
        std::unique_ptr<inventory_column> selection_col;
};

class inventory_compare_selector : public inventory_multiselector
{
    public:
        inventory_compare_selector( player &p );
        std::pair<const item *, const item *> execute();

    protected:
        std::vector<const item *> compared;

        void toggle_entry( inventory_entry *entry );
};

// This and inventory_drop_selectors should probably both inherit from a higher-abstraction "action selector".
// Should accept a function to calculate dummy values.

class inventory_iuse_selector : public inventory_multiselector
{
    public:
        using GetStats = std::function<stats( const std::map<const item *, int> & )>;
        inventory_iuse_selector( player &p,
                                 const std::string &selector_title,
                                 const inventory_selector_preset &preset = default_preset,
                                 const GetStats & = {} );
        std::vector<iuse_location> execute();

    protected:
        header_stats get_raw_stats() const override;
        void set_chosen_count( inventory_entry &entry, size_t count );

    private:
        GetStats get_stats;
        std::map<const item *, std::vector<iuse_location>> to_use;
};

class inventory_drop_selector : public inventory_multiselector
{
    public:
        inventory_drop_selector( player &p,
                                 const inventory_selector_preset &preset = default_preset );
        drop_locations execute();

    protected:
        /**
         * Opens prompt that asks for number of items to select
         *
         * @param count Number that the prompt starts out with
         * @return size_t Number the prompt returns
         */
        header_stats get_raw_stats() const override;
        /** Toggle item dropping */
        void set_chosen_count( inventory_entry &entry, size_t count );
        void process_selected( int &count, const std::vector<inventory_entry *> &selected );
        auto update_drop_preview() -> void;
        auto get_drop_preview() const -> const drop_locations & { return predicted_drops; } // *NOPAD*

    private:
        excluded_stacks dropping;
        excluded_stacks predicted_counts;
        drop_locations predicted_drops;
        bool preview_dirty = true;
};


class inventory_pickup_selector : public inventory_multiselector
{
    public:
        inventory_pickup_selector( player &p, const inventory_selector_preset &preset = default_preset );
        std::vector<pickup::pick_drop_selection> execute();
    protected:
        header_stats get_raw_stats() const override;
};


