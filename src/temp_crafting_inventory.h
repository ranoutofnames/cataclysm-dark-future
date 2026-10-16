#pragma once
#ifndef CATA_SRC_TEMP_CRAFTING_INVENTORY_H
#define CATA_SRC_TEMP_CRAFTING_INVENTORY_H

#include <climits>
#include <cstddef>
#include <unordered_set>
#include <vector>

#include "coordinates.h"
#include "item.h"
#include "type_id.h"
#include "visitable.h"

class Character;
class map;
class map_cursor;
class vehicle_cursor;

/**
 * A transient add-only list of item references and temporary pseudo-items that can answer
 * crafting queries (see crafting.cpp and requirement_data::can_make_with_inventory).
 * Interface follows DDA src/temp_crafting_inventory.h.
 * CDF: backed by BN's visitable<T> and raw item* instead of DDA's value-type item_location.
 * CDF: no query_cache_scope / provider_quality caches (DDA hooks BN lacks); queries walk live.
 * CDF: liquid container bookkeeping (must_use_liq_container etc.) is not present in BN's
 * inventory, so it is not ported here.
 */
class temp_crafting_inventory : public temp_visitable<temp_crafting_inventory>
{
    public:
        temp_crafting_inventory() = default;
        // Copies the references; owned copies stay owned by the global temporary pool
        temp_crafting_inventory( const temp_crafting_inventory & ) = default;
        temp_crafting_inventory &operator=( const temp_crafting_inventory & ) = default;

        size_t size() const;
        void clear();

        /**
         * Adds item reference to this container.
         * @note container doesn't own the added reference, the item should outlive this container.
         */
        // CDF: takes item& where DDA takes item_location
        void add_item_loc( item &it );

        /** Adds all top-level items from the given source (references, not owned). */
        // CDF: Character / cursors take the place of DDA's read_only_visitable overload
        void add_all_ref( const temp_crafting_inventory &other );
        void add_all_ref( Character &guy );
        void add_all_ref( map_cursor &cur );
        void add_all_ref( vehicle_cursor &cur );

        /** Adds a copy of the item, kept alive as a BN temporary. @return the copy */
        item &add_item_copy( const item &it );
        item &add_pseudo_item( const itype_id &id );
        item &add_pseudo_item( const item &it );

        int count_item( const itype_id &item_type ) const;

        void form_from_zone( map &m, std::unordered_set<tripoint_abs_ms> &zone_pts,
                             const Character *pl = nullptr );
        void form_from_map( const tripoint_bub_ms &origin, int range, const Character *pl = nullptr,
                            bool clear_path = true );
        void form_from_map( map &m, const tripoint_bub_ms &origin, int range,
                            const Character *pl = nullptr, bool clear_path = true );
        void form_from_map( map &m, std::vector<tripoint_bub_ms> pts, const Character *pl );

        // Drops references only; items that do not belong to this inventory are not deleted
        // CDF: DDA returns std::list<item>; BN's temp_visitable returns the removed pointers
        std::vector<item *> remove_items_with( const std::function<bool( const item & )> &filter,
                                               int count = INT_MAX );
        void dump( std::vector<item *> &dest ) const;

        // Top-level references in insertion order (used by the visitable specializations)
        const std::vector<item *> &top_items() const {
            return items;
        }

    private:
        std::vector<item *> items;
};

#endif // CATA_SRC_TEMP_CRAFTING_INVENTORY_H
