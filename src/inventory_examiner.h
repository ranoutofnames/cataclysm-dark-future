#pragma once

#include "cursesdef.h"
#include "inventory_ui.h"

class item;
class player;

// CDF: P3-UI3, DDA inventory_ui.h:1232-1300 (inventory_examiner), with item * for item_location (D1).
// DDA defines the three results as macros; they are constants here with the same values.
constexpr int EXAMINED_CONTENTS_UNCHANGED = 0;
constexpr int EXAMINED_CONTENTS_WITH_CHANGES = 1;
constexpr int NO_CONTENTS_TO_EXAMINE = 2;

/**
 * Open a container and quickly examine the items contained within.
 *
 * List the contents in a pane on the left, and show the item info of the selected item on the right.
 * To use, construct this, add_contained_items( container ), then execute().
 */
class inventory_examiner : public inventory_selector
{
    private:
        int examine_window_scroll = 0;
        int scroll_item_info_lines = 0;

        void force_max_window_size();

    protected:
        item *parent_item = nullptr;
        const item *selected_item = nullptr;
        catacurses::window w_examine;
        bool changes_made = false;
        bool parent_was_collapsed = false;

    public:
        explicit inventory_examiner( player &p, item *item_to_look_inside,
                                     const inventory_selector_preset &preset = default_preset );

        /**
         * Return false if parent_item has no contents or is otherwise unsuitable for inventory_examiner.
         * Otherwise, return true.
         */
        bool check_parent_item();

        /**
         * Determine the appropriate return value for execute(): EXAMINED_CONTENTS_WITH_CHANGES when
         * the user changed something (collapsed or expanded a container), else
         * EXAMINED_CONTENTS_UNCHANGED.
         */
        int cleanup() const;

        /** Draw the details of sitem in the w_examine window. */
        void draw_item_details( const item *sitem );

        /**
         * Display the inventory_examiner menu. Returns EXAMINED_CONTENTS_WITH_CHANGES or
         * EXAMINED_CONTENTS_UNCHANGED when the window was shown, NO_CONTENTS_TO_EXAMINE when
         * parent_item has no contents, so the caller examines the item with action_examine().
         */
        int execute();

        /** Do initial setup work prior to displaying the window. */
        void setup();

        /** CDF: tests set this to drive cleanup() without a window. */
        void set_changes_made( bool changed ) {
            changes_made = changed;
        }
};
