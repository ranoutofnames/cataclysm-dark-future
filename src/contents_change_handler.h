#pragma once

#include <vector>

#include "safe_reference.h"

class Character;
class JsonIn;
class JsonOut;
class item;

/**
 * Records a batch of unsealed containers and handles spilling at once. This
 * is preferred over handling containers right after unsealing because the latter
 * can cause items to be invalidated when later code still needs to access them.
 * See @ref `Character::handle_contents_changed` for more detail.
 * Transplanted from DDA src/contents_change_handler.h (b4b62254ae2); CDF: containers
 * are tracked with safe_reference<item> instead of item_location.
 */
class contents_change_handler
{
    public:
        contents_change_handler() = default;
        /**
         * Add an already unsealed container to the list. The item
         * should remain valid when `handle_by` is called.
         */
        void add_unsealed( item &container );
        /**
         * Unseal the pocket containing @p contained and add its parent to the list.
         * Does nothing if @p contained does not have a parent container. The parent
         * should remain valid when `handle_by` is called, but @p contained only
         * needs to be valid here (for example, the item may be consumed afterwards).
         */
        void unseal_pocket_containing( item &contained );
        /**
         * Let the guy handle any container that needs spilling. This may invalidate
         * items in and out of the list of containers. The list is cleared after handling.
         */
        void handle_by( Character &guy );
        /**
         * Serialization for activities
         */
        void serialize( JsonOut &jsout ) const;
        /**
         * Deserialization for activities
         */
        void deserialize( JsonIn &jsin );
    private:
        std::vector<safe_reference<item>> unsealed;
};
