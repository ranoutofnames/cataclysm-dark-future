#pragma once

#include <algorithm>
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
#include "cuboid_rectangle.h"
#include "cursesdef.h"
#include "detached_ptr.h"
#include "enum_traits.h"
#include "input.h"
#include "item_handling_util.h"
#include "item_pocket.h"
#include "memory_fast.h"
#include "pimpl.h"
#include "units.h"
#include "pickup_token.h"
#include "translations.h"
#include "type_id.h"

class Character;
class player;
class item;
class item_category;
class string_input_popup;
struct tripoint;
class ui_adaptor;
class JsonObject;
class JsonOut;
struct inventory_selector_save_state;

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
class inventory_entry;

struct inventory_input {
    std::string action;
    int ch;
    inventory_entry *entry;
    bool mouse = false; // entry was found by screen position, not by inventory letter
};

/// Display-only key hint. Doesn't participate in inventory-letter lookup.
struct inventory_invlet_hint {
    int invlet;
    nc_color color;
};

/// Screen rectangle (window-relative text cells) of one drawn row, for mouse lookup. DDA keeps a
/// vector of std::pair; CDF uses a named struct. Filled by inventory_column::draw.
struct inventory_entry_rect {
    inclusive_rectangle<point> rect;
    inventory_entry *entry = nullptr;
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
        /// Per DDA inventory_ui.h:186-187. Set by inventory_selector::highlight() for the rows
        /// that hold the selected item (parent) or sit inside it (child); only drawn.
        bool highlight_as_parent = false;
        bool highlight_as_child = false;

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
        /// Per DDA inventory_ui.h:148-150. With skip_unselectable off the cursor may rest on a
        /// disabled row (to read its denial); with it on, only selectable rows are highlightable.
        bool is_highlightable( bool skip_unselectable ) const {
            return is_item() && ( enabled || !skip_unselectable );
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
        /**
         * CDF: P3-UI3. The container this preset inserts into, or nullptr. DDA reads it through
         * inventory_holster_preset::get_holster() (inventory_ui.h:290-310); CDF has one preset class
         * per call site, so the insert selector asks the preset. A preset that fills it makes
         * inventory_insert_selector show the used and total space of that container.
         */
        virtual const item *get_holster() const {
            return nullptr;
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

        /** Per DDA inventory_ui.h:275-281: the pocket types the contained-items walk lists. */
        std::vector<pocket_type> get_pocket_type() const {
            return _pk_type;
        }
        bool has_pocket_type( pocket_type pt ) const {
            return std::find( _pk_type.begin(), _pk_type.end(), pt ) != _pk_type.end();
        }

        virtual std::function<bool( const inventory_entry & )> get_filter( const std::string &filter )
        const;

        /// Per DDA inventory_ui.h:294: the saved view mode of this preset's selector.
        inventory_selector_save_state *save_state = nullptr;

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
        /**
         * Per DDA inventory_ui.h:317: what add_contained_items lists. The default is the container
         * pocket alone; { pocket_type::LAST } asks for the standard pockets (container, magazine,
         * magazine well).
         */
        std::vector<pocket_type> _pk_type = { pocket_type::CONTAINER };

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
        pickup_inventory_preset( const player &p );
        std::string get_denial( const item *loc ) const override;

    private:
        const player &p;
};

class inventory_column
{
    public:
        // CDF: defined in the .cpp; takes hide_entries_override from uistate (per DDA inventory_ui.cpp:691).
        inventory_column( const inventory_selector_preset &preset = default_preset );

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
        // CDF: P3-5-2b, DDA inventory_column::get_highlighted() non-const twin (inventory_ui.h 413).
        // DDA's highlighted entry is CDF's selected one, so this is the mutable get_selected();
        // ammo_inventory_selector::execute (5-2c) steps the count of it.
        inventory_entry &get_highlighted();
        // CDF: P3-5-2b, in place of DDA inventory_entry::make_entry_cell_cache( preset ) (DDA
        // inventory_ui.cpp 1072), which refreshes one entry's cached cells. CDF caches the cells in
        // the column, by index into entries; this recomputes the slot of the given entry, an
        // element of entries (any other entry is ignored).
        void update_entry_cell_cache( const inventory_entry &entry );
        std::vector<inventory_entry *> get_all_selected() const;
        std::vector<inventory_entry *> get_entries(
            const std::function<bool( const inventory_entry &entry )> &filter_func ) const;
        std::vector<inventory_entry *> get_all_entries(
            const std::function<bool( const inventory_entry &entry )> &filter_func ) const;
        std::vector<inventory_entry *> get_all_entries() const;
        inventory_entry *find_by_invlet( int invlet ) const;

        /// @param rects when set, receives the rectangle of every drawn row (DDA rect_entry_map).
        void draw( const catacurses::window &win, point pos,
                   std::vector<inventory_entry_rect> *rects = nullptr ) const;

        void add_entry( const inventory_entry &entry );
        void move_entries_to( inventory_column &dest );
        // CDF: E1-1, DDA inventory_ui.cpp:3599. Keeps one entry per itype (a stub until E1-6).
        void remove_duplicate_itypes( bool include_variants );
        // Per DDA inventory_ui.h:510-518.
        void collate();
        void uncollate();
        void set_collapsed( inventory_entry &entry, bool collapse );
        /// Per DDA inventory_ui.cpp:3585-3597: cycles false -> true -> nullopt and saves it in uistate.
        virtual void cycle_hide_override();
        /// nullopt: pocket settings decide; true: contents of all containers hidden; false: all shown.
        std::optional<bool> hide_entries_override = std::nullopt;
        /// Per DDA inventory_ui.cpp:737-740.
        void toggle_skip_unselectable( bool skip );
        bool get_skip_unselectable() const {
            return skip_unselectable;
        }
        /// Per DDA inventory_ui.cpp:972-996. Next entry in dir that can be highlighted.
        size_t next_highlightable_index( size_t index, scroll_direction dir ) const;
        void clear();
        void set_stack_favorite( const item *location, bool favorite );

        /** Selects the specified location. */
        bool select( const item *loc );
        /// Per DDA inventory_ui.cpp:1636-1675 (highlight). CDF: item * instead of item_location.
        bool select( const item *loc, bool front_only );
        auto select_position_if_item_type( size_t new_index, const itype_id &type ) -> bool;

        /**
         * Change the selection.
         * @param new_index Index of the entry to select.
         * @param dir If the entry is not selectable, move in the specified direction
         */
        void select( size_t new_index, scroll_direction dir );

        /// Per DDA inventory_ui.h:538-543.
        bool sort_compare( inventory_entry const &lhs, inventory_entry const &rhs );
        bool indented_sort_compare( inventory_entry const &lhs, inventory_entry const &rhs );
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
        /// Per DDA inventory_ui.cpp:1027 (mouse wheel): select the first row of the next/previous page.
        void scroll_selection_page( scroll_direction dir );
        size_t get_width() const;
        size_t get_height() const;
        /** Expands the column to fit the new entry. */
        void expand_to_fit( const inventory_entry &entry, bool with_denial = true );
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
        /// Per DDA inventory_ui.h:489-506: an optional override of the hierarchy setting above.
        bool indent_entries() const {
            if( indent_entries_override ) {
                return *indent_entries_override;
            }
            return hierarchy;
        }
        void set_indent_entries_override( bool entry_override ) {
            indent_entries_override = entry_override;
        }
        void clear_indent_entries_override() {
            indent_entries_override = std::nullopt;
        }
        bool hierarchy = false;
        std::optional<bool> indent_entries_override;
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

        /// CDF: the cursor skips disabled rows unless the player turned the skip off (DDA's
        /// default is off; CDF keeps its old behaviour as the default).
        bool skip_unselectable = true;

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

        size_t selected_index = std::numeric_limits<size_t>::max();
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
        // Per DDA inventory_ui.cpp:3627-3630: the selection column never hides entries.
        void cycle_hide_override() override {}

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
        // CDF: E1-1, DDA inventory_ui.h:655-665: add_efiles lists the files inside e-devices too. The
        // parameter is threaded through the walk and ignored until E1-6.
        void add_character_items( Character &character, bool add_efiles = false );
        void add_map_items( const tripoint_bub_ms &target, bool add_efiles = false );
        void add_vehicle_items( const tripoint_bub_ms &target, bool add_efiles = false );
        void add_nearby_items( int radius = 1, bool add_efiles = false );
        // CDF: E1-1, DDA inventory_ui.cpp:2165-2220. Stubs that add nothing until E1-6.
        /** Adds the e-books stored on @p container. */
        bool add_contained_ebooks( item *container );
        /** Adds the e-files stored on @p container. */
        bool add_contained_efiles( item *container );
        /** Adds the e-books of every e-device the character carries. */
        void add_character_ebooks( Character &character );
        // CDF: E1-1, DDA inventory_ui.cpp:3599, inventory_ui.h:524, 718. Keeps one entry per itype
        // (per variant when include_variants). A stub until E1-6.
        void remove_duplicate_itypes( bool include_variants );
        void add_fake_items( Character &character );
        /** Per DDA inventory_ui.cpp:3821: switch between category columns and the container tree. */
        virtual void toggle_categorize_contained();
        /** Registers VIEW_CATEGORY_MODE for this selector. The footer label is always drawn. */
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
        const std::string &get_hint() const {
            return hint;
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
        // CDF: P3-5-3a, DDA inventory_ui.h 686 (uint64_t there). Counts the item entries, hidden ones
        // included, of the columns that allow selecting; the 5-1 primitives have get_only_choice
        // with its own count and no item_entry_count.
        size_t item_entry_count() const;

        /** Apply filter string to all columns */
        void set_filter( const std::string &str );
        /** Per DDA inventory_ui.cpp:3671-3673. Clears the filter. Does not lay out the window. */
        void reset_filter();
        /** The part of set_filter( str ) that does not lay out the window. */
        void apply_filter( const std::string &str );
        /**
         * CDF: UI1. Collapses or expands the pockets of the selected row itself (DDA
         * inventory_ui.cpp:1340-1342). The row stays listed; on a holder garment row
         * the carried rows under it hide. Does not lay out the window.
         */
        void toggle_selected_contents();
        /** Per DDA inventory_ui.cpp:3688-3692. Cycles the hide override of all columns. */
        void cycle_hide_override_all();
        /** Per DDA inventory_ui.cpp:3744-3750. Toggles skipping of disabled rows. */
        void toggle_skip_unselectable();
        static bool get_skip_unselectable() {
            return skip_unselectable;
        }
        /**
         * Per DDA inventory_ui.cpp:3636-3639. Handles NEXT_COLUMN / PREV_COLUMN.
         * @return true when the action was one of them.
         */
        bool handle_column_action( const std::string &action );
        /**
         * Per DDA inventory_ui.cpp:4335-4371. Marks the rows that hold the selected item
         * (highlight_as_parent) and the rows inside it (highlight_as_child); clears the old marks.
         */
        void highlight();
        /**
         * Activates either previous or next column.
         * @param dir Forward - next column, backward - previous.
         */
        void toggle_active_column( scroll_direction dir );
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
                            int indent, bool add_efiles = false );
        /** Per DDA inventory_ui.cpp:2105. Adds the items inside container to column. */
        bool add_contained_items( item *container, inventory_column &column,
                                  const item_category *custom_category = nullptr,
                                  const item *topmost_parent = nullptr, int indent = 0,
                                  bool add_efiles = false );
    public:
        /** Per DDA inventory_ui.cpp:2485. The row drawn under a window-relative text cell, or nullptr. */
        inventory_entry *find_entry_by_coordinate( const point &coordinate ) const;
        /** Rows drawn by the last draw_columns (DDA rect_entry_map). Public so tests can fill it. */
        mutable std::vector<inventory_entry_rect> rect_entry_map;
        /**
         * Per DDA inventory_ui.cpp:2049 drag_drop_item: store @p source in a pocket of @p dest through
         * the "Select pocket" menu (game_menus::inv::select_pocket_and_store).
         * @return true when the item was stored.
         */
        bool drag_drop_item( item *source, item *dest );
        /** Test hook: when set, drag_drop_item calls it instead of the menu (tests reset it). */
        static std::function<bool( item &, item & )> drag_drop_test_hook;
        /**
         * Test hook: lays the columns out once before a scripted run. ui_manager::redraw() is a
         * no-op in test mode, so the frame pump never calls prepare_layout() and every column would
         * keep selected_index = entries_per_page = SIZE_MAX (src/ui_manager.cpp:328).
         */
        void test_prepare_layout() {
            prepare_layout();
        }
        /** Per DDA inventory_ui.cpp:2143. Adds the items inside container to the own inventory column. */
        void add_contained_items( item *container );
        /** Per DDA inventory_ui.cpp:4315. Opens the item info window of sitem (full screen width, scrolling). */
        void action_examine( const item *sitem );
    protected:

        /**
         * CDF: PK1/G36, DDA inventory_ui.cpp:2337-2358 (_add_map_items). add_items calls this after
         * the rows of the stacks of a map square or of vehicle cargo. roots are the items of those
         * stacks, custom_category their square header. It adds the rows for what sits nested in
         * them and marks the parent row with a chevron. The ammo selector overrides it with its own
         * walk (P3-NESTAMMO2).
         */
        virtual void add_nested_map_items( inventory_column &target_column,
                                           const std::vector<item *> &roots,
                                           const item_category *custom_category,
                                           bool add_efiles = false );

        uimode _uimode = uimode::categories;


        void add_items( inventory_column &target_column,
                        const std::function<item*( item * )> &locator,
                        const std::vector<std::list<item *>> &stacks,
                        const item_category *custom_category = nullptr, bool add_efiles = false );

        inventory_input get_input();

        /** Per DDA inventory_ui.cpp:3416-3418: the "< [key] categories >" footer label. */
        std::string view_mode_footer_text() const;
        /** Per DDA inventory_ui.cpp:2716-2718: "Numpad ON" while number-row navigation is on. */
        std::string header_numpad_text() const;
        /** Per DDA inventory_ui.cpp:2565: numeric invlets are off while numpad navigation is on. */
        void reassign_invlets();

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
        /** CDF: P3-UI3. The pocket overview of @p container (DDA inventory_ui.cpp:3987-4016), shared
         * by container_inventory_selector and inventory_insert_selector. Returns the lines only. */
        static header_stats get_container_header_stats( const item &container );
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
        /// Per DDA inventory_ui.cpp:3551-3583: the entry under the mouse for the mouse actions.
        inventory_input process_input( const std::string &action, int ch );
        /// Registers COORDINATE, MOUSE_MOVE, SELECT and CLICK_AND_DRAG (DDA 3452-3453, 3468-3469).
        /// The base constructor calls it, so every selector receives mouse events; each execute()
        /// loop decides what a hover, a click and a drag mean.
        void register_mouse_actions();

        // Called from resizing/redraw callbacks of ui_adaptor and from the pick selectors
        // mouse handling.
        void prepare_layout( size_t client_width, size_t client_height );
        void prepare_layout();

        const std::vector<inventory_column *> &get_all_columns() const {
            return columns;
        }
        std::vector<inventory_column *> get_visible_columns() const;

    private:
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
        /// Per DDA inventory_ui.cpp:2408-2460. Reveals a collapsed collation entry first.
        bool select( const item *loc, bool hidden, bool front_only );
        /// Per DDA inventory_ui.cpp:447-466. Returns the sibling collated item, or nullptr.
        item *get_collation_next() const;
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

        void refresh_active_column() {
            if( !get_active_column().activatable() ) {
                toggle_active_column( scroll_direction::FORWARD );
            }
        }
        void toggle_navigation_mode();

        const navigation_mode_data &get_navigation_data( navigation_mode m ) const;
        std::unique_ptr<string_input_popup> spopup;
        weak_ptr_fast<ui_adaptor> ui;
        /** Per DDA inventory_ui.h:883. A window size or origin of -1 means automatic. */
        point _fixed_origin{ -1, -1 };
        point _fixed_size{ -1, -1 };

    private:
        catacurses::window w_inv;

        // PL-62 AT3: the headless draw-dump tests size this window and call the four private draw
        // functions in refresh_window() order, without a live ui_adaptor.
        friend struct inventory_selector_test_access;

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
        int nearby_radius = -1;
        bool fake_items_added = false;
        // CDF: the sources a selector was built from, so toggle_categorize_contained() can rebuild
        // the same rows. DDA re-categorizes in place; CDF clears and re-adds.
        bool character_items_added = false;
        std::vector<tripoint_bub_ms> added_squares;
        // CDF: E1-12, the rows of the e-file menus: the devices whose books / files were added
        // by add_contained_ebooks / add_contained_efiles, whether any adder listed the files of
        // devices (add_efiles), and the remove_duplicate_itypes call, so the view-mode key
        // rebuilds them.
        std::vector<item *> ebook_sources;
        std::vector<item *> efile_sources;
        bool efiles_in_rows = false;
        bool duplicates_removed = false;
        bool duplicates_include_variants = false;
        /** add_contained_efiles without remembering the container (the add_efiles walk redoes it). */
        bool add_contained_efile_rows( item *container );
        // Per DDA inventory_ui.h:1007. Shared by all selectors; CDF default is on (old behaviour).
        static bool skip_unselectable;

    protected:
        /**
         * CDF: P3-UI1 fix (A-L2). The "hide all contents" override (uistate.hide_entries_override) is for
         * the full inventory screens. A selector that calls this in its constructor ignores it: its
         * columns show everything, and SHOW_HIDE_CONTENTS_ALL does nothing, so a saved `true` cannot
         * silently hide rows in a screen whose hint does not show the key.
         */
        void ignore_hide_override();
        bool hide_override_ignored = false;

    public:
        std::string action_bound_to_key( char key ) const;
        /** Returns all keys in the current context which are bound to an action. Warning: may contain duplicates. */
        std::vector<char> all_bound_keys( ) const;
};

inventory_selector::stat display_stat( const std::string &caption, int cur_value, int max_value,
                                       const std::function<std::string( int )> &disp_func );

/** CDF: P3-UI3. True when `it` is inside `container` at any depth (DDA item_location::eventually_contains). */
bool eventually_contains( const item &container, const item &it );

/**
 * What a pick selector's handle_action() wants its execute() loop to do. This replaces the old
 * bool close signal: DDA's execute returns directly from every branch, while CDF needs to say
 * whether the key was used at all. pick_action_result::not_handled falls through to on_input().
 */
enum class pick_action_result {
    not_handled,   // not a handler action: run on_input()
    stay_open,     // handled; keep the screen open and do not run on_input()
    close_screen,  // handled; return nullptr, the caller closes (or reopens) the screen
    return_item,   // handled; return the selected item
};

class inventory_pick_selector : public inventory_selector
{
    public:
        inventory_pick_selector( player &p,
                                 const inventory_selector_preset &preset = default_preset ) :
            inventory_selector( p, preset ) {}

        item *execute();

        /// Per DDA inventory_ui.h:1032: drag and drop between rows is off unless a caller opts in.
        bool drag_enabled = false;

        /// What one mouse event did (DDA 3938-3963, factored out of execute so tests can drive it).
        struct mouse_outcome {
            item *picked = nullptr;      // the row was clicked: execute returns this item
            bool toggle_contents = false; // chevron click: the caller runs on_input( SHOW_HIDE_CONTENTS )
        };
        /// Handles CLICK_AND_DRAG (drag start), MOUSE_MOVE (hover), SELECT (click or drag end) and the
        /// chevron click (SHOW_HIDE_CONTENTS) on @p entry.
        mouse_outcome handle_mouse_action( const std::string &action, inventory_entry &entry );
        bool is_drag_active() const {
            return drag_active;
        }
        /// CDF: P3-UI6 fix (B-M1). Called with every input before it is acted on: a drag only lives
        /// across CLICK_AND_DRAG and MOUSE_MOVE events and ends on a mouse click on a row. Any other
        /// input (a release over no row, a key press) cancels it, so the next click is a plain click.
        void end_stale_drag( const inventory_input &input );
        /// CDF: P3-UI1 fix (A-M1). The row CONFIRM picks: the selected item, but only when its row
        /// is selectable (with TOGGLE_SKIP_UNSELECTABLE off the cursor can rest on a disabled row).
        item *confirmed_item() const;

    protected:
        item *drag_source = nullptr;
        bool drag_active = false;

        virtual auto handle_action( const std::string &/*action*/ ) -> pick_action_result {
            return pick_action_result::not_handled;
        }
};

/** The choice made in the unload screen: the item to unload (nullptr if cancelled) and the Auto flag. */
struct unload_selection {
    item *to_unload = nullptr;
    bool auto_contain = false;
};

// CDF: P3-5-4, DDA inventory_ui.h:1215-1222. The result is a named struct in place of
// std::pair<item_location, bool> (item * for item_location, D1; AGENT_RULES: no std::pair in
// interfaces). hint_string is protected, not private, so a test can read it through a subclass.
class unload_selector : public inventory_pick_selector
{
    public:
        explicit unload_selector( player &p, const inventory_selector_preset &preset = default_preset );
        auto execute() -> unload_selection;
    protected:
        auto hint_string() -> std::string;
};

class inventory_multiselector : public inventory_selector
{
    public:
        /// Per DDA inventory_ui.h toggle_mode: which entries toggle_entries() works on.
        enum class toggle_mode {
            SELECTED,
            NON_FAVORITE_NON_WORN
        };

        // CDF: E1-1, DDA inventory_ui.h:1112-1117. The chosen entries and counts go to the callback
        // that builds the stats bar (D: DDA's std::pair list is the named drop_locations). The
        // callback and allow_select_contained are stored; E1-6 makes get_raw_stats use them.
        using GetStats = std::function<header_stats( const drop_locations & )>;
        inventory_multiselector( player &p, const inventory_selector_preset &preset = default_preset,
                                 const std::string &selection_column_title = "",
                                 const GetStats &stats = {}, bool allow_select_contained = false );

        /// Per DDA inventory_ui.cpp:4702-4758: an entry whose item sits inside another chosen
        /// entry's item (at any depth) is unmarked; marking the container covers its contents.
        void deselect_contained_items();
        /// Per DDA inventory_multiselector::toggle_entry (4399-4404).
        void toggle_entry( inventory_entry &entry, size_t count );
        /// Per DDA inventory_multiselector::toggle_entries (4463-4562). count 0 toggles, count > 0
        /// marks that amount; count is reset to 0 afterwards.
        void toggle_entries( int &count, toggle_mode mode = toggle_mode::SELECTED );
        /// Per DDA inventory_multiselector::on_input (4774-4811), the mark actions only:
        /// TOGGLE_ENTRY, MARK_WITH_COUNT, TOGGLE_NON_FAVORITE, INCREASE_COUNT, DECREASE_COUNT,
        /// the number keys and VIEW_CATEGORY_MODE. Returns true when the input was used.
        bool handle_mark_action( const std::string &action, int ch = 0 );
        /// Per DDA inventory_multiselector::on_input (4774-4781): the row under the mouse is
        /// highlighted; SELECT or ANY_INPUT marks/unmarks it, and any other mouse event goes to the
        /// normal selector handler (chevron clicks expand, the wheel scrolls). Returns false when
        /// the input carries no row.
        bool handle_entry_action( const inventory_input &input );
        /// A-L1 fix: a digit key seeds the count prompt (checked before the invlet lookup).
        static bool is_count_digit( int ch );
        /**
         * Test hook: when set, query_count asks this for the entered text instead of opening the
         * prompt. The argument is the text the prompt starts with; nullopt means cancelled.
         */
        static std::function<std::optional<std::string>( const std::string & )> query_count_test_hook;
        /// Test hook: when set, the "not an integer" message goes here instead of the popup.
        static std::function<void( const std::string & )> query_count_error_test_hook;
        /// Per DDA inventory_ui.cpp:4760-4772: the chosen counts survive the view switch.
        void toggle_categorize_contained() override;
        /// Whether an entry may be marked together with the container that holds it (DDA
        /// allow_select_contained). False for every CDF multiselector.
        bool allow_select_contained = false;

    protected:
        void rearrange_columns( size_t client_width ) override;
        /// Per DDA inventory_ui.cpp:3305-3323. @param init the digit the prompt starts with.
        /// @return the typed amount, or -1 when the prompt was cancelled or the text is not an integer.
        int query_count( char init = 0, bool end_with_toggle = false );
        /// Per DDA inventory_ui.cpp:4669-4675: no callback, no stats bar.
        header_stats get_raw_stats() const override;
        /** CDF: E1-1: the stats callback (see GetStats), set by the constructor. */
        GetStats stats_callback;
        virtual void set_chosen_count( inventory_entry &entry, size_t count );
        /// Runs after every batch of marks, once the contained items are unmarked.
        virtual void on_toggle() {}
        /// Common tail of a batch of marks: pocket-aware unmarking, paging, on_toggle().
        void end_mark_batch();
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
        void set_chosen_count( inventory_entry &entry, size_t count ) override;

    private:
        GetStats get_stats;
        std::map<const item *, std::vector<iuse_location>> to_use;
};

/** CDF: P3-UI3, DDA inventory_ui.h:1038. Pick one item; the header shows the space inside `loc`. */
class container_inventory_selector : public inventory_pick_selector
{
    public:
        explicit container_inventory_selector( player &p, item *loc,
                                               const inventory_selector_preset &preset = default_preset ) :
            inventory_pick_selector( p, preset ), loc( loc ) {
            ignore_hide_override(); // A-L2 fix
        }

    protected:
        header_stats get_raw_stats() const override;

    private:
        item *loc;
};

class inventory_drop_selector : public inventory_multiselector
{
    public:
        inventory_drop_selector( player &p,
                                 const inventory_selector_preset &preset = default_preset );
        /** CDF: P3-UI3. The same with another title for the selection column. */
        inventory_drop_selector( player &p, const inventory_selector_preset &preset,
                                 const std::string &selection_column_title );
        drop_locations execute();

    protected:
        /**
         * Opens prompt that asks for number of items to select
         *
         * @param count Number that the prompt starts out with
         * @return size_t Number the prompt returns
         */
        header_stats get_raw_stats() const override;
        /** CDF: P3-UI3. The marked stacks and their counts, for the insert selector's header. */
        auto get_marked() const -> const excluded_stacks & { return dropping; } // *NOPAD*
        /** Toggle item dropping */
        void set_chosen_count( inventory_entry &entry, size_t count ) override;
        /** CDF: refresh the drop preview right after a batch of marks. */
        void on_toggle() override;
        /** CDF: kept for the drop preview tests; applies count to the selected entries. */
        void process_selected( int &count, const std::vector<inventory_entry *> &selected );
        auto update_drop_preview() -> void;
        auto get_drop_preview() const -> const drop_locations & { return predicted_drops; } // *NOPAD*

    private:
        excluded_stacks dropping;
        excluded_stacks predicted_counts;
        drop_locations predicted_drops;
        bool preview_dirty = true;
};


/**
 * CDF: P3-UI3, DDA inventory_ui.h:1181-1192. The drop selector as the insert menu: the header also shows
 * the used and total volume and weight of the target container (preset.get_holster()) and the
 * volume and weight of the marked items. Without a holster it behaves as the drop selector.
 */
class inventory_insert_selector : public inventory_drop_selector
{
    public:
        explicit inventory_insert_selector( player &p, const inventory_selector_preset &preset,
                                            const std::string &selection_column_title = _( "ITEMS TO INSERT" ),
                                            bool warn_liquid = true );
    protected:
        header_stats get_raw_stats() const override;
};

/**
 * CDF: P3-UI8, DDA inventory_ui.h pickup_selector (4993-5186). CDF keeps its own pickup pipeline: the
 * result is still the optimize_pickup list that pickup.cpp turns into an activity. What comes from
 * DDA is the menu: its hint line, the refusal text of an empty selection, the "Selected" header
 * line and wield/wear that move the highlight to the next item.
 */
class inventory_pickup_selector : public inventory_multiselector
{
    public:
        /// One marked entry stack: the item and how many of it (charges for stacks) are taken.
        struct marked_pickup {
            item *target = nullptr;
            int count = 0;
        };

        inventory_pickup_selector( player &p, const inventory_selector_preset &preset = default_preset );
        std::vector<pickup::pick_drop_selection> execute();
        /// The marked items with their counts, in column order (DDA to_use).
        std::vector<marked_pickup> get_marked_pickups() const;
        /// The marks as the pickup list (optimize_pickup of get_marked_pickups). Empty when nothing is marked.
        std::vector<pickup::pick_drop_selection> build_selection() const;
        /// The text of the popup shown when CONFIRM is pressed with nothing marked (names TOGGLE_ENTRY).
        std::string no_selection_text() const;
        /// DDA get_item_to_highlight_after_use: the item listed after `it` in the active column.
        item *item_after_in_active_column( const item &it ) const;
        /// DDA remove_from_to_use plus highlight-next: unlists the item (and its marks) and
        /// moves the highlight to the item that followed it.
        void remove_used_item( item &it );
        /// DDA pickup_selector::wield / wear for the highlighted row. True when it was done.
        bool wield_highlighted();
        bool wear_highlighted();
    protected:
        header_stats get_raw_stats() const override;
};

// CDF: P3-5-2a, DDA inventory_ui.h 1052-1069. DDA's item_location fields are plain item
// pointers here (D1). They own nothing and are valid only while the items stay where they
// are, as for item_reload_option: keep the target and ammo items alive and in place while a
// reload_target is in use.
// One reload destination: either a MAGAZINE_WELL slot (kind::well) or an
// already-loaded magazine accepting loose ammo (kind::loaded_mag). owner is
// the gun/gunmod containing the well; ui_well_index groups entries in the UI.
struct reload_target {
    enum class kind {
        well,
        loaded_mag,
        // Integral MAGAZINE on a multimag host: target is the host item,
        // pocket_index points at the MAGAZINE pocket for direct deposit.
        integral_magazine,
    };

    item *target = nullptr;
    item *owner = nullptr;
    int pocket_index = -1;
    int ui_well_index = -1;
    kind kind = kind::well;
};

// CDF: P3-5-3a. DDA's item_location::can_reload_with( ammo, now ) (item_location.cpp 1524-1537)
// as a free function on items. P3-5-2a defined it file-static in inventory_ui.cpp; the select_ammo
// preset (game_inventory.cpp, DDA 3250-3257) needs the same two guards, so it is exported.
bool reload_loc_can_reload_with( const item &reloadable, const item &ammo, bool now );

// CDF: P3-5-2a, DDA inventory_ui.h 1071-1080. item & and const item & in place of DDA's
// item_location (D1); 5-2b and 5-3 pass *loc.
std::vector<reload_target> get_possible_reload_targets( item &target );

// First reload_target accepting the given ammo, or nullptr.
const reload_target *find_matching_reload_target(
    const std::vector<reload_target> &targets, const item &ammo );

// Every reload_target accepting the given ammo. Used to detect ambiguous
// reloads (multiple matching wells / loaded mags) for disambiguation prompts.
std::vector<const reload_target *> find_all_matching_reload_targets(
    const std::vector<reload_target> &targets, const item &ammo );

// CDF: P3-5-2b, DDA inventory_ui.h 1081-1104. `player &` for DDA's `Character &` (ADV-14 ruling 4,
// the CDF selector base takes player &) and `item &` for the item_location reload_loc (D1; 5-3
// passes *loc): the host and every item added to the selector must stay alive and in place while
// the selector exists. player_adjusted_ammo_locs holds item pointers. The destructor is out of
// line so that std::list<item_category> (item_category is only forward declared here) stays out
// of every includer. mod_chosen_count is protected, not private, so a test can drive it through
// a subclass. execute() is declared here and defined by 5-2c.
class ammo_inventory_selector : public inventory_selector
{
    public:
        explicit ammo_inventory_selector( player &you, item &reload_loc,
                                          const inventory_selector_preset &preset = default_preset );
        ~ammo_inventory_selector() override;

        drop_location execute();
        void set_all_entries_chosen_count();
        // True if the user changed the count for the entry returned by
        // the most recent execute().
        bool last_choice_count_player_adjusted() const {
            return last_player_adjusted;
        }
    protected:
        void mod_chosen_count( inventory_entry &entry, int val );
        /** CDF: P3-NESTAMMO2, DDA inventory_ui.cpp:2337-2358 (_add_map_items) for ground and vehicle stacks. */
        void add_nested_map_items( inventory_column &target_column, const std::vector<item *> &roots,
                                   const item_category *custom_category, bool add_efiles = false ) override;
    private:
        /** The map walk: a row for everything below container that the preset shows. */
        void add_rows_below( inventory_column &column, item &container,
                             const std::function<const item_category *()> &category );
        item &reload_loc;
        // Keyed on the item so the signal survives column rebuilds
        // caused by filtering during the menu session.
        std::vector<const item *> player_adjusted_ammo_locs;
        bool last_player_adjusted = false;
        // Per-well synthetic categories. std::list keeps pointers stable for
        // inventory_entry::custom_category.
        std::list<item_category> reload_well_categories;
};

/**
 * Per DDA inventory_ui.h:1019-1021: reading the enum back from its JSON name needs its last member.
 */
template <>
struct enum_traits<inventory_selector::uimode> {
    static constexpr inventory_selector::uimode last = inventory_selector::uimode::last;
};

/**
 * Per DDA inventory_ui.h:1308-1317: the view mode (categories or the container hierarchy) that a
 * selector remembers between openings and, through uistate, between sessions.
 */
struct inventory_selector_save_state {
    public:
        inventory_selector::uimode uimode = inventory_selector::uimode::categories;

        void serialize( JsonOut &json ) const;
        void deserialize( JsonObject const &jo );
};

// CDF: DDA keeps inventory_sel_default_state file-static; it is extern here so the tests can put the
// fallback state back after a test that toggles the view mode (FM7).
extern inventory_selector_save_state inventory_sel_default_state;
extern inventory_selector_save_state inventory_ui_default_state;
extern inventory_selector_save_state pickup_sel_default_state;
extern inventory_selector_save_state pickup_ui_default_state;
