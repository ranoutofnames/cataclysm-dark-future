#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "color.h"
#include "coordinates.h"
#include "cuboid_rectangle.h"
#include "cursesdef.h"
#include "memory_fast.h"
#include "pimpl.h"
#include "point.h"
#include "string_formatter.h"

class translation;

////////////////////////////////////////////////////////////////////////////////////
/**
 * uilist constants
 */
const int UILIST_ERROR = -1024;
const int UILIST_WAIT_INPUT = -1025;
const int UILIST_UNBOUND = -1026;
const int UILIST_CANCEL = -1027;
const int UILIST_TIMEOUT = -1028;
const int UILIST_ADDITIONAL = -1029;
const int MENU_AUTOASSIGN = -1;

class input_context;
class string_input_popup;
class ui_adaptor;
struct input_event;

catacurses::window new_centered_win( int nlines, int ncols );

/**
 * mvwzstr: line of text with horizontal offset and color
 */

struct mvwzstr {
    int left = 0;
    nc_color color = c_unset;
    std::string txt;
    int sym = 0;
};

/**
 * uilist_entry: entry line for uilist
 */
struct uilist_entry {
    // Return this int
    int retval = -1;
    // Darken, and forbid scrolling if hilight_disabled is false
    bool enabled = true;
    // If true, will never darken this option, even when disabled
    bool force_color = false;
    // Keycode from (int)getch(). Setting to MENU_AUTOASSIGN will automagically pick first free character: 1-9 a-z A-Z
    int hotkey = MENU_AUTOASSIGN;
    // Entry text
    std::string txt;
    // Optional, possibly longer, description
    std::string desc;
    // Second column text
    std::string ctxt;
    // Hotkey color
    nc_color hotkey_color;
    // Text color
    nc_color text_color = c_red_red;
    mvwzstr extratxt;
    // Use 'hilite_color' when selected instead of uilist's 'hilight_color'
    bool override_hilite_color = false;
    nc_color hilite_color;

    uilist_entry( std::string T ) : txt( T ) {}

    // Verbose constuctors. Try to avoid using these in favor of more readable builder methods.
    uilist_entry( std::string T, std::string D ) : txt( T ), desc( D ) {}
    uilist_entry( std::string T, int K ) : hotkey( K ), txt( T ) {}
    uilist_entry( int R, bool E, int K, std::string T ) : retval( R ), enabled( E ), hotkey( K ),
        txt( T ) {}
    uilist_entry( int R, bool E, int K, std::string T, std::string D ) : retval( R ), enabled( E ),
        hotkey( K ), txt( T ), desc( D ) {}
    uilist_entry( int R, bool E, int K, std::string T, std::string D, std::string C ) : retval( R ),
        enabled( E ), hotkey( K ), txt( T ), desc( D ), ctxt( C ) {}
    uilist_entry( int R, bool E, int K, std::string T, nc_color H, nc_color C ) : retval( R ),
        enabled( E ), hotkey( K ), txt( T ), hotkey_color( H ), text_color( C ) {}

    uilist_entry &with_retval( int R ) {
        retval = R;
        return *this;
    }

    uilist_entry &with_hotkey( int K ) {
        hotkey = K;
        return *this;
    }

    uilist_entry &with_enabled( bool E ) {
        enabled = E;
        return *this;
    }

    uilist_entry &with_descr( std::string s ) {
        desc = std::move( s );
        return *this;
    }

    uilist_entry &with_ctxt( std::string s ) {
        ctxt = std::move( s );
        return *this;
    }

    uilist_entry &with_hk_color( nc_color c ) {
        hotkey_color = c;
        return *this;
    }

    uilist_entry &with_txt_color( nc_color c ) {
        text_color = c;
        return *this;
    }

    std::optional<inclusive_rectangle<point>> drawn_rect;
};

/**
 * Generic multi-function callback for highlighted items, key presses, and window control. Example:
 *
 * class monmenu_cb: public uilist_callback {
 *   public:
 *   bool key(int ch, int num, uilist * menu) {
 *     if ( ch == 'k' && num > 0 ) {
 *       std::vector<monster> * game_z=static_cast<std::vector<monster>*>(myptr);
 *       game_z[num]->dead = true;
 *     }
 *   }
 *   void refresh( uilist *menu ) {
 *       if( menu->selected >= 0 && static_cast<size_t>( menu->selected ) < game_z.size() ) {
 *           mvwprintz( menu->window, 0, 0, c_red, "( %s )",game_z[menu->selected]->name() );
 *           wnoutrefresh( menu->window );
 *       }
 *   }
 * }
 * uilist monmenu;
 * for( size_t i = 0; i < z.size(); ++i ) {
 *   monmenu.addentry( z[i].name );
 * }
 * monmenu_cb * cb;
 * cb->setptr( &g->z );
 * monmenu.callback = cb
 * monmenu.query();
 *
 */
class uilist;

/**
* uilist::query() handles most input events first,
* and then passes the event to the callback if it can't handle it.
*
* The callback returninig a boolean false signifies that the callback can't "handle the
* event completely". This is unchanged before or after the PR.
* @{
*/
class uilist_callback
{
    public:

        /**
        * After a new item is selected, call this once
        */
        virtual void select( uilist * ) {}
        virtual bool key( const input_context &, const input_event &/*key*/, int /*entnum*/,
                          uilist * ) {
            return false;
        }
        virtual void refresh( uilist * ) {}
        virtual ~uilist_callback() = default;
};
/*@}*/
/**
 * uilist: scrolling vertical list menu
 */

class uilist // NOLINT(cata-xy)
{
    public:
        class size_scalar
        {
            public:
                struct auto_assign {
                };

                size_scalar &operator=( auto_assign );
                size_scalar &operator=( int val );
                size_scalar &operator=( const std::function<int()> &fun );

                friend class uilist;

            private:
                std::function<int()> fun;
        };

        class pos_scalar
        {
            public:
                struct auto_assign {
                };

                pos_scalar &operator=( auto_assign );
                pos_scalar &operator=( int val );
                // the parameter to the function is the corresponding size vector element
                // (width for x, height for y)
                pos_scalar &operator=( const std::function<int( int )> &fun );

                friend class uilist;

            private:
                std::function<int( int )> fun;
        };

        uilist();
        uilist( const std::string &hotkeys_override );
        // query() will be called at the end of these convenience constructors
        uilist( const std::string &msg, const std::vector<uilist_entry> &opts );
        uilist( const std::string &msg, const std::vector<std::string> &opts );
        uilist( const std::string &msg, std::initializer_list<const char *const> opts );

        ~uilist();

        // whether to report invalid color tag with debug message.
        void color_error( bool report );

        void init();
        void setup();
        // initialize the window or reposition it after screen size change.
        void reposition( ui_adaptor &ui );
        void show( ui_adaptor &ui );
        bool scrollby( int scrollby );
        void query( bool loop = true, int timeout = -1 );

        /**
         * Repopulate filtered entries list (fentries) and set fselected accordingly.
         */
        void filterlist();
        /**
         * Filter by predicate. Index of uilist_entry in a vector will be passed in.
         */
        void filterpredicate( const std::function<bool( int )> &predicate );
        /**
         * Clear current filter string and repopulate filtered entries.
         */
        void clear_filter();
        /**
         * Override current filter string and repopulate filtered entries.
         * Note that uilist has inbuilt "query for filter string and apply it" functionality,
         * but this method can be used for creating uilists with some filter pre-applied.
         */
        void set_filter( const std::string &fstr );
        /**
         * Get current filter string.
         */
        const std::string &get_filter() const {
            return filter;
        }

        /**
         * Get filtered list of entries.
         * Elements are indices for 'entries'.
         */
        const std::vector<int> &get_filtered() const {
            return fentries;
        }

        /**
         * Move selection to given entry.
         * @param sel Entry to select (index for 'entries').
         * @returns 'false' if entry does not exist / is not available with current filter.
         */
        bool set_selected( int sel );
        /// Updates an entry's displayed hotkey and active dispatch mapping together.
        auto set_entry_hotkey( std::size_t entry_index, int hotkey ) -> bool;

        void addentry( const std::string &str );
        void addentry( int r, bool e, int k, const std::string &str );
        // K is templated so it matches a `char` literal and a `int` value.
        // Using a fixed type (either `char` or `int`) will lead to ambiguity with the
        // other overload when called with the wrong type.
        template<typename K, typename ...Args>
        void addentry( const int r, const bool e, K k, const char *const format, Args &&... args ) {
            return addentry( r, e, k, string_format( format, std::forward<Args>( args )... ) );
        }
        void addentry_desc( const std::string &str, const std::string &desc );
        void addentry_desc( int r, bool e, int k, const std::string &str, const std::string &desc );
        void addentry_col( int r, bool e, int k, const std::string &str, const std::string &column,
                           const std::string &desc = "" );
        void settext( const std::string &str );

        auto set_categories(
            std::vector<std::string> category_names,
            std::function < auto( const uilist_entry &, std::size_t ) -> bool > filter,
            std::size_t requested_category = 0 ) -> void;
        auto enable_dynamic_categories() -> void;
        auto refresh_category_filter() -> void;
        auto get_current_category() const -> std::size_t;

        void reset();

        // Can be called before `uilist::query` to keep the uilist on UI stack after
        // `uilist::query` returns. The returned `ui_adaptor` is cleared when the
        // `uilist` is deconstructed.
        //
        // Example:
        //     shared_ptr_fast<ui_adaptor> ui = menu.create_or_get_ui_adaptor();
        //     menu.query()
        //     // before `ui` or `menu` is deconstructed, the menu will always be
        //     // displayed on screen.
        shared_ptr_fast<ui_adaptor> create_or_get_ui_adaptor();

        operator int() const;

    private:
        int scroll_amount_from_action( const std::string &action );
        void apply_scrollbar();
        auto cycle_category( bool forward ) -> void;
        // This function assumes it's being called from `query` and should
        // not be made public.
        void inputfilter();

    public:
        // Parameters
        // TODO change to setters
        std::string title;
        std::string text;
        // basically the same as desc, except it doesn't change based on selection
        std::string footer_text;
        std::vector<uilist_entry> entries;

        std::string input_category;
        std::vector<std::pair<std::string, translation>> additional_actions;

        nc_color border_color;
        nc_color text_color;
        nc_color title_color;
        nc_color hilight_color;
        nc_color hotkey_color;
        nc_color disabled_color;

        uilist_callback *callback;

        pos_scalar w_x_setup;
        pos_scalar w_y_setup;
        size_scalar w_width_setup;
        size_scalar w_height_setup;

        int textwidth;

        size_scalar pad_left_setup;
        size_scalar pad_right_setup;

        // Maximum number of lines to be allocated for displaying descriptions.
        // This only serves as a hint, not a hard limit, so the number of lines
        // may still exceed this value when for example the description text is
        // long enough.
        int desc_lines_hint;
        bool desc_enabled;

        bool filtering;
        bool filtering_igncase;

        // return on selecting disabled entry, default false
        bool allow_disabled = false;
        // return UILIST_UNBOUND on keys unbound & unhandled by callback, default false
        bool allow_anykey = false;
        // return UILIST_CANCEL on "QUIT" action, default true
        bool allow_cancel = true;
        // return UILIST_ADDITIONAL if the input action is inside `additional_actions`
        // and unhandled by callback, default false.
        bool allow_additional = false;
        // allow confirming with the "CONFIRM" action, default true (per DDA uilist.h:487)
        bool allow_confirm = true;
        bool hilight_disabled = false;

    private:
        std::string hotkeys;
        report_color_error _color_error = report_color_error::yes;
        input_context create_main_input_context() const;
        input_context create_filter_input_context() const;

    public:
        // Iternal states
        // TODO make private
        std::vector<std::string> textformatted;

        catacurses::window window;
        int w_x;
        int w_y;
        int w_width;
        int w_height;

        int pad_left;
        int pad_right;

        int vshift = 0;

        int fselected = 0;

    private:
        std::vector<int> fentries;
        std::map<int, int> keymap;

        weak_ptr_fast<ui_adaptor> ui;

        std::unique_ptr<string_input_popup> filter_popup;
        std::string filter;

        std::vector<std::string> categories;
        std::function < auto( const uilist_entry &, std::size_t ) -> bool > category_filter;
        std::size_t current_category = 0;
        bool dynamic_categories = false;

        int max_entry_len;
        int max_column_len;

        int vmax = 0;

        int desc_lines;

        bool started = false;

    public:
        // Results
        // TODO change to getters
        std::string ret_act;
        int ret;
        int keypress;
        int selected;
};

/**
 * Callback for uilist that pairs menu entries with points
 * When an entry is selected, view will be centered on the paired point
 */
class pointmenu_cb : public uilist_callback
{
    private:
        struct impl_t;
        pimpl<impl_t> impl;
    public:
        pointmenu_cb( const std::vector<tripoint_bub_ms> &pts );
        ~pointmenu_cb() override;
        void select( uilist *menu ) override;
};

/**
 * Helper for typical UI list navigation with wrap-around
 * Add delta to val. If on bounds, wrap to other bound, otherwise clamp to the range [0,size)
 */
template<typename V, typename S>
inline std::enable_if_t < !std::is_enum_v<V>, V >
inc_clamp_wrap( V val, int delta, S size )
{
    if( size == 0 ) {
        return 0;
    }
    // wrap to last
    if( val <= 0 && delta < 0 ) {
        return size - 1;
    }
    // wrap to first
    // Templating of existing `unsigned int` triggers linter rules against `unsigned long`
    // NOLINTNEXTLINE(cata-no-long)
    if( val >= static_cast<V>( size ) - 1 && delta > 0 ) {
        return 0;
    }
    return std::clamp<V>( val + delta, 0, size - 1 );
}

/**
 * Helper for typical arrow key UI list navigation with wrap-around
 * Add 1/-1 to val, then wrap to the range [0,size)
 */
template<typename V, typename S>
inline std::enable_if_t < !std::is_enum_v<V>, V >
inc_clamp_wrap( V val, bool inc, S size )
{
    return inc_clamp_wrap( val, static_cast<int>( inc ? 1 : -1 ), size );
}

/**
 * Helper for typical arrow key UI list navigation with wrap-around
 * Specialized for enum values
 * Add 1/-1 to val, then wrap to the range [0,size)
 */
template<typename T, typename I>
inline std::enable_if_t<std::is_enum_v<T>, T>
inc_clamp_wrap( T val, I inc, T size )
{
    return static_cast<T>( inc_clamp_wrap( static_cast<int>( val ), inc, static_cast<int>( size ) ) );
}

/**
 * Helper for typical UI list navigation without wrap-around
 * Add delta to val, then clamp to the range [min,max]
 */
template<typename V, typename S>
inline std::enable_if_t < !std::is_enum_v<V>, V >
inc_clamp( V val, int delta, S min, S max )
{
    // Templating of existing `unsigned int` triggers linter rules against `unsigned long`
    if constexpr( std::is_unsigned_v<V> ) {
        // NOLINTNEXTLINE(cata-no-long)
        if( delta < 0 && val <= static_cast<V>( -delta ) ) {
            return min;
        }
    }
    return std::clamp<V>( val + delta, min, max );
}

/**
 * Helper for typical arrow key UI list navigation without wrap-around
 * Add 1/-1 to val, then clamp to the range [0,max]
 */
template<typename V, typename S>
inline std::enable_if_t < !std::is_enum_v<V>, V >
inc_clamp( V val, bool inc, S max )
{
    // NOLINTNEXTLINE(cata-no-long)
    return inc_clamp( val, inc ? 1 : -1, static_cast<S>( 0 ), max );
}

/**
 * Helper for typical UI list navigation without wrap-around
 * Add delta to val, then clamp to the range [0,max]
 */
template<typename V, typename S>
inline std::enable_if_t < !std::is_enum_v<V>, V >
inc_clamp( V val, int delta, S max )
{
    // NOLINTNEXTLINE(cata-no-long)
    return inc_clamp( val, delta, static_cast<S>( 0 ), max );
}

/**
 * Helper for typical UI list navigation without wrap-around
 * Add 1/-1 to val, then clamp to the range [min,max]
 */
template<typename V, typename S>
inline std::enable_if_t < !std::is_enum_v<V>, V >
inc_clamp( V val, bool inc, S min, S max )
{
    return inc_clamp( val, inc ? 1 : -1, min, max );
}

/**
 * Helper for typical UI list navigation without wrap-around
 * Specialized for enum values
 * Add 1/-1 to val, then clamp to the range [0,max]
 */
template<typename T, typename I>
inline std::enable_if_t<std::is_enum_v<T>, T>
inc_clamp( T val, I inc, T size )
{
    return static_cast<T>( inc_clamp( static_cast<int>( val ), inc, static_cast<int>( size ) ) );
}

/**
 * Helper for typical UI list navigation with wrap-around
 * Handles up, down, scroll_up and _down, page_up and _down, home, and end
 * Returns true if an action/input was handled
 */
template<typename V, typename S>
bool navigate_ui_list( const std::string &action, V &val, int page_delta, S size, bool wrap );
