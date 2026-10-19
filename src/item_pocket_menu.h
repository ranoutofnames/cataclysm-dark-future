#pragma once
#ifndef CATA_SRC_ITEM_POCKET_MENU_H
#define CATA_SRC_ITEM_POCKET_MENU_H

#include <string>
#include <vector>

#include "item_pocket.h"
#include "type_id.h"

class Character;
class item;

/** One line of the pocket management menu: an item heading (pocket == nullptr) or a pocket. */
struct pocket_row {
    item_pocket *pocket = nullptr;
    // 1-based number shown to the player (only for pockets)
    int pocket_num = 0;
    // the item the pocket belongs to, or the item itself for headings
    item *holder = nullptr;
    std::string text;
    bool enabled = false;
};

/**
 * The data side of DDA's pocket_favorite_callback (item_contents.cpp:52-531), without any drawing:
 * the row list, the pocket settings edits, moving items and presets.
 */
class pocket_settings_model
{
    public:
        explicit pocket_settings_model( std::vector<item *> roots );

        /** Rebuilds the row list (after any change to the contents). */
        void rebuild();
        const std::vector<pocket_row> &rows() const {
            return rows_;
        }
        /** The pocket on @p row, nullptr for item headings and out of range rows. */
        item_pocket *pocket_at( int row ) const;
        int pocket_number_at( int row ) const;

        /** true: edits go to the whitelists, false: to the blacklists. */
        bool whitelist = true;

        void set_priority( item_pocket &pk, int priority ) const;
        void toggle_auto_pickup( item_pocket &pk ) const;
        void toggle_auto_unload( item_pocket &pk ) const;
        void toggle_item( item_pocket &pk, const itype_id &id ) const;
        void toggle_category( item_pocket &pk, const item_category_id &id ) const;
        /** Whitelists the item types inside the pocket. Returns true if the list changed. */
        bool whitelist_contents( item_pocket &pk ) const;
        void clear( item_pocket &pk ) const;

        /** Items inside @p pk that can be picked for moving. */
        static std::vector<item *> movable_items( item_pocket &pk );
        /** Whether the moved item may go to the pocket on @p row (DDA move_item :257-285). */
        bool can_move_to( const item &moved, int row ) const;
        /**
         * Moves @p moved into @p dest as the character would store it.
         * @return an empty string on success, else why it failed.
         */
        static std::string move_item( Character &who, item &moved, item_pocket &dest );

        /** Saves the pocket settings as a preset, false if the name exists and !overwrite. */
        static bool save_preset( item_pocket &pk, const std::string &name, bool overwrite );
        static void apply_preset( item_pocket &pk, int index );
        static void delete_preset( int index );

    private:
        std::vector<item *> roots_;
        std::vector<pocket_row> rows_;
        void add_pockets( item &it, const std::string &depth );
};

/** The menu itself (DDA pocket_management_menu, item_contents.cpp:2982). */
void pocket_management_menu( const std::string &title, const std::vector<item *> &to_organize,
                             Character &who );

#endif // CATA_SRC_ITEM_POCKET_MENU_H
