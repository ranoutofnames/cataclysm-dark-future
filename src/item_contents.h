#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "item_pocket.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"
#include "visitable.h"
#include "location_vector.h"

class Character;
class JsonIn;
class JsonOut;
class item;
struct itype;
struct tripoint;

/**
 * The contents of an item, held in pockets (CDF P1a, DDA #39406).
 *
 * The pockets come from the owner's type (`itype::pockets`). Items that have no
 * proper home in them go to a MIGRATION pocket, so nothing is ever lost.
 * The pre-pocket API is kept as shims over all pockets, so code written for the
 * flat BN contents list keeps working.
 */
class item_contents
{
    public:
        item_contents( item *container );
        ~item_contents();

        bool empty() const;
        auto has_processing_items() const -> bool;
        auto processing_items() const -> const std::vector<item *> &; // *NOPAD*
        auto invalidate_processing_cache() const -> void;
        /** Drops the cached volume and weight of every pocket (this level only). */
        void invalidate_pocket_caches() const;

        /** returns a list of pointers to all top-level items, in pocket order */
        const std::vector<item *> &all_items_top() const;

        /** removes a top-level item */
        detached_ptr<item> remove_top( item *it );

        // returns a list of pointers to all items inside recursively
        std::vector<item *> all_items_ptr();
        // returns a list of pointers to all items inside recursively
        std::vector<const item *> all_items_ptr() const;

        /** gets all gunmods in the item */
        std::vector<item *> gunmods();
        /** gets all gunmods in the item */
        std::vector<const item *> gunmods() const;

        /**
         * this is an artifact of the previous code using
         * front() everywhere for contents. this is to aid
         * migration to pockets. please do not use for new functions
         */
        item &front();
        const item &front() const;
        /**
         * this is an artifact of the previous code using
         * back() everywhere for contents. this is to aid
         * migration to pockets. please do not use for new functions
         */
        item &back();
        const item &back() const;

        units::volume item_size_modifier() const;
        units::mass item_weight_modifier() const;

        int best_quality( const quality_id &id ) const;

        /**
         * Puts an item into the pocket it belongs in. Never fails: an item with no
         * proper pocket goes to the MIGRATION pocket (DDA migrate_content_item rules).
         */
        /**
         * Copies the contents of @p source, keeping each item in the pocket (and each
         * pocket's sealed state) it was in when the layouts match.
         */
        void copy_from( const item_contents &source );

        ret_val<bool> insert_item( detached_ptr<item> &&it );
        /**
         * Puts @p n copies of @p proto into the contents, exactly as @p n calls of
         * @ref insert_item with copies would (DDA #67349), but the pocket is only chosen
         * again when it stops being the only candidate or fills up.
         */
        void insert_copies( const item &proto, int n );

        /**
         * returns the number of items stacks in contents
         * each item that is not count_by_charges,
         * plus whole stacks of items that are
         */
        size_t num_item_stacks() const;

        bool spill_contents( const tripoint_bub_ms &pos );
        std::vector<detached_ptr<item>> clear_items();

        /**
         * Sets the items contained to their defaults.
         */
        void set_item_defaults();

        void handle_liquid_or_spill( Character &guy );
        void casings_handle( const std::function < detached_ptr<item>( detached_ptr<item> && ) > &func );

        item *get_item_with( const std::function<bool( const item & )> &filter );

        bool has_any_with( const std::function<bool( const item & )> &filter ) const;

        void migrate_item( item &obj, const std::set<itype_id> &migrations );
        bool item_has_uses_recursive() const;
        bool stacks_with( const item_contents &rhs, int depth = 0 ) const;
        /**
         * @relates visitable
         * NOTE: upon expansion, this may need to be filtered by type enum depending on accessibility
         */
        VisitResponse visit_contents( const std::function<VisitResponse( item *, item * )> &func,
                                      item *parent = nullptr, bool skip_carry = false );
        void remove_top_items_with( const std::function < detached_ptr<item>( detached_ptr<item> && ) >
                                    &filter );

        void remove_items_with( const std::function < VisitResponse( detached_ptr<item> && ) >
                                &filter );

        void serialize( JsonOut &json ) const;
        void deserialize( JsonIn &jsin );

        void on_destroy();

        /** @name Pockets */
        /*@{*/
        /** All pockets, in order. The MIGRATION pocket, if any, is last. */
        std::list<item_pocket> &get_pockets();
        const std::list<item_pocket> &get_pockets() const;
        /** Pockets of one type. */
        std::vector<item_pocket *> get_pockets( pocket_type type );
        std::vector<const item_pocket *> get_pockets( pocket_type type ) const;
        /** The pocket holding a top-level item, or nullptr. */
        item_pocket *pocket_of( const item *it );
        const item_pocket *pocket_of( const item *it ) const;
        /** The MIGRATION pocket, created if @p create is set. */
        item_pocket *migration_pocket( bool create = false );
        const item_pocket *migration_pocket() const;
        /**
         * Rebuilds the pockets from the owner's type and re-homes every item.
         * Called when the owner's type changes (item::convert) or its mods change.
         */
        void rebuild_pockets();
        /** Puts @p it into @p pocket (one of ours) without checks. */
        void insert_into( item_pocket &pocket, detached_ptr<item> &&it );
        /** The pocket @ref insert_item would choose for @p it. */
        item_pocket *route( const item &it );
        /** Like @ref route, but nullptr instead of the MIGRATION pocket. */
        item_pocket *fitting_pocket( const item &it );
        /**
         * Whether @p it fits in a pocket of type @p type right now. On failure the
         * message names the reason, e.g. "it has 0.25 L left" (DDA #44356).
         */
        ret_val<item_pocket::contain_code> can_contain( const item &it,
                pocket_type type = pocket_type::CONTAINER ) const;
        /**
         * The best unsealed CONTAINER pocket for @p it (DDA #40286). Pockets of
         * contained items are only considered if no own pocket fits (#79913).
         * @return the item owning the pocket, and the pocket; nullptr if nothing fits
         */
        std::pair<item *, item_pocket *> best_pocket( const item &it, const item *avoid = nullptr );

        /**
         * Moves out what doesn't belong (DDA #65518, #67631): items over a pocket's
         * capacity or not accepted by it, and MIGRATION items that belong in a pocket
         * of this item. Each goes, in order, to another pocket of this item, the
         * @p parent container, the @p carrier, then the ground at @p pos, with a
         * message for each item dropped. Items that never had a pocket here (spent
         * casings, an RC car's charge) stay in MIGRATION. Nothing is ever lost: what
         * can't be placed anywhere stays in MIGRATION.
         * @return number of items moved
         */
        int overflow( Character *carrier, const tripoint_bub_ms &pos, item *parent = nullptr );
        /** True if @ref overflow would move something. */
        bool needs_overflow() const;
        /**
         * Overflow is event-driven: a type change, mod removal or save migration
         * marks the contents, and the next item processing runs @ref overflow.
         */
        bool overflow_pending() const {
            return pending_overflow;
        }
        void check_overflow();
        /*@}*/
    private:
        /** The type's pockets with an installed magazine_adaptor applied; empty if none is. */
        std::vector<pocket_data> adapted_layout() const;
        bool has_adaptor_mod() const;
        auto update_processing_cache() const -> void;
        std::pair<item *, item_pocket *> best_pocket_impl( const item &it, const item *avoid,
                bool nested, units::volume parent_room );
        /**
         * Builds the pockets if the owner's type changed since they were built.
         * Pockets are derived from the type, so this is allowed on const contents.
         */
        void ensure_pockets() const;
        void refresh_top_cache() const;
        void contents_changed();

        item *owner;
        std::list<item_pocket> pockets;
        /** Pocket data when a magazine_adaptor mod changed the type's layout; else empty. */
        std::vector<pocket_data> adapted_pockets;
        /** The type the pockets were built for. */
        const itype *pockets_type = nullptr;
        mutable std::vector<item *> top_cache;
        bool pending_overflow = false;
        mutable bool processing_cache_dirty = true;
        mutable std::vector<item *> cached_processing_items;
};
