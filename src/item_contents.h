#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "coordinates.h"
#include "enums.h"
#include "item_pocket.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"
#include "visitable.h"
#include "location_vector.h"

class Character;
class JsonIn;
class JsonObject;
class JsonOut;
class item;
class iteminfo_query;
class map;
struct itype;
struct tripoint;

/// NEW!ness to player, if they seen such item already (DDA item_contents.h)
enum class content_newness {
    NEW,  // at least one item is new
    MIGHT_BE_HIDDEN,  // at least one item might be hidden and none are NEW
    SEEN
};

/**
 * One entry of a MIGRATION's `contents`: an item type and how many of it the migrated item
 * holds (CDF P1e step 28).
 */
struct migration_content {
    itype_id id;
    int count = 1;
};

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
        /**
         * DDA item_contents.h: contents that belong to no item, built from pocket data (the data
         * must outlive the contents). Used by item::combine and tests.
         */
        explicit item_contents( const std::vector<pocket_data> &pockets );
        ~item_contents();

        /** Return true if contents are empty (ignoring item mods, since they aren't contents). */
        bool empty() const;
        /** Return true if contents are empty of everything including mods. */
        bool empty_with_no_mods() const;
        /** Check if contents is empty. Checking only CONTAINER pockets. */
        bool empty_container() const;
        /** Check if CONTAINER pockets are all full. */
        bool full( bool allow_bucket ) const;
        /** Check if MAGAZINE pockets are all full. */
        bool is_magazine_full() const;
        /** Are any CONTAINER pockets bigger on the inside than the container's volume? */
        bool bigger_on_the_inside( const units::volume &container_volume ) const;
        /** Number of pockets. */
        size_t size() const;
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
         * Attempts to insert an item into these item contents, stipulating that it must go into a
         * pocket of the given type (DDA item_contents.h). Fails if the contents have no pocket with
         * that type. T2: the detached_ptr form is only consumed on success; the const item &
         * spelling inserts a copy.
         *
         * With CONTAINER, MAGAZINE, or MAGAZINE_WELL pocket types, items must fit the pocket's
         * volume, length, weight, ammo type, and all other physical restrictions. This is
         * synonymous with the success of item_contents::can_contain with that item.
         * If ignore_contents is true, will disregard other pocket contents for these checks.
         *
         * For the MOD, CORPSE, SOFTWARE and MIGRATION pocket types, if contents have such a
         * pocket, items will be successfully inserted without regard to volume, length, or any
         * other restrictions, since these pockets are not considered to be normal "containers".
         */
        ret_val<item *> insert_item( detached_ptr<item> &&it, pocket_type pk_type,
                                     bool ignore_contents = false, bool unseal_pockets = false );
        ret_val<item *> insert_item( const item &it, pocket_type pk_type, bool ignore_contents = false,
                                     bool unseal_pockets = false );
        /** DDA item_contents.h: an unchecked insert into the first pocket of the type. */
        void force_insert_item( detached_ptr<item> &&it, pocket_type pk_type );
        void force_insert_item( const item &it, pocket_type pk_type );
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

        void handle_liquid_or_spill( Character &guy, const item *avoid = nullptr );
        void casings_handle( const std::function < detached_ptr<item>( detached_ptr<item> && ) > &func );

        item *get_item_with( const std::function<bool( const item & )> &filter );

        bool has_any_with( const std::function<bool( const item & )> &filter ) const;

        /**
         * Adds the missing part of a MIGRATION's contents to @p obj (the item that owns this):
         * for each entry, as many items as are not yet inside, up to its count.
         */
        void migrate_item( item &obj, const std::vector<migration_content> &migrations );
        bool item_has_uses_recursive() const;
        bool stacks_with( const item_contents &rhs, int depth = 0, int maxdepth = 2 ) const;
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
        void deserialize( const JsonObject &data );

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
        /**
         * Like @ref route, but nullptr instead of the MIGRATION pocket. Plate slots (ablative
         * pockets) are only considered with @p allow_ablative: spawns and migrations place
         * plates in them, ordinary overflow does not.
         */
        item_pocket *fitting_pocket( const item &it, bool allow_ablative = true );
        /**
         * CDF/Lua: whether @p it fits in a pocket of type @p type right now. On failure the
         * message names the reason, e.g. "it has 0.25 L left" (DDA #44356). Sealed pockets
         * refuse (D23).
         */
        ret_val<item_pocket::contain_code> can_contain( const item &it, pocket_type type ) const;
        /**
         * returns whether any of the pockets contained is compatible with the specified item.
         * Does not check if the item actually fits volume/weight wise
         * Ignores mod, migration, corpse pockets
         */
        ret_val<void> is_compatible( const item &it ) const;
        /**
         * returns whether an item can be physically stored within these item contents (DDA).
         * @param ignore_pkt_settings whether to ignore pocket autoinsert settings
         * @param ignore_non_container_pocket ignore magazine pockets, such as weapon magazines
         * @param remaining_parent_volume if we are nesting things without concern for rigidity we need
         * to be careful about overfilling pockets: the remaining volume of any parent pockets
         */
        ret_val<void> can_contain( const item &it, bool ignore_pkt_settings = true,
                                   bool ignore_non_container_pocket = false,
                                   units::volume remaining_parent_volume = 10000000_ml ) const;
        ret_val<void> can_contain( const item &it, int &copies_remaining, bool ignore_pkt_settings = true,
                                   bool ignore_non_container_pocket = false,
                                   units::volume remaining_parent_volume = 10000000_ml ) const;
        ret_val<void> can_contain_rigid( const item &it, bool ignore_pkt_settings = true,
                                         bool ignore_non_container_pocket = false ) const;
        ret_val<void> can_contain_rigid( const item &it, int &copies_remaining,
                                         bool ignore_pkt_settings = true,
                                         bool ignore_non_container_pocket = false ) const;
        bool can_contain_liquid( bool held_or_ground ) const;
        bool contains_no_solids() const;

        /**
         * returns whether any of the pockets can be reloaded with the specified item.
         * @param ammo item to be loaded in
         * @param now whether the currently contained ammo/magazine should be taken into account
         */
        bool can_reload_with( const item &ammo, bool now ) const;
        bool can_unload_liquid() const;

        /**
         * The best pocket for @p it, as DDA item_contents::best_pocket (T5: no item_location).
         * @param avoid item that will be avoided in recursive lookup item pocket
         * @param allow_sealed allow use sealed pocket
         * @param ignore_settings ignore pocket setting
         * @param nested whether the current call is nested (used recursively)
         * @param ignore_rigidity ignore pocket rigid
         * @param allow_nested whether nested pockets should be checked
         * @return the item owning the pocket, and the pocket; pocket is nullptr if nothing fits
         */
        pocket_choice best_pocket( const item &it, const item *avoid = nullptr, bool allow_sealed = false,
                                   bool ignore_settings = false, bool nested = false,
                                   bool ignore_rigidity = false, bool allow_nested = true );

        units::length max_containable_length( bool unrestricted_pockets_only = false ) const;
        units::length min_containable_length() const;
        units::volume max_containable_volume( bool unrestricted_pockets_only = false ) const;
        std::set<flag_id> magazine_flag_restrictions() const;
        /** Check this speedloader is compatible with pockets. */
        bool allows_speedloader( const itype_id &speedloader_id ) const;

        units::length item_length_modifier() const;

        /** Per-pocket item info text (DDA item_contents.cpp:2886), gated by DESCRIPTION_POCKETS. */
        void info( std::vector<iteminfo> &info, const iteminfo_query *parts ) const;

        /** Get the total weight capacity of all pockets. */
        units::mass total_container_weight_capacity( bool unrestricted_pockets_only = false ) const;
        /** Get the total volume capacity of pockets belonging to this item. */
        units::volume volume_capacity( const std::function<bool( const item_pocket & )> &include_pocket =
                                           item_pocket::ok_default_containers ) const;
        /** Get the total capacity of pockets belonging to or nested inside this item (DDA). */
        units::volume volume_capacity_recursive( const std::function<bool( const item_pocket & )>
                &include_pocket, const std::function<bool( const item_pocket & )> &check_pocket_tree,
                units::volume &out_volume_expansion ) const;
        /** Get the sum of volumes of items in pockets belonging to this item. */
        units::volume contents_volume( const std::function<bool( const item_pocket & )> &include_pocket =
                                           item_pocket::ok_default_containers ) const;
        /** Get the remaining volume of pockets belonging to this item. */
        units::volume remaining_volume( const std::function<bool( const item_pocket & )> &include_pocket =
                                            item_pocket::ok_default_containers ) const;
        units::volume remaining_volume_recursive( const std::function<bool( const item_pocket & )>
                &include_pocket, const std::function<bool( const item_pocket & )> &check_pocket_tree,
                units::volume &out_volume_expansion ) const;
        /** Capacity of the biggest pocket. Ignores blacklist restrictions etc. */
        units::volume biggest_pocket_capacity() const;
        units::mass remaining_container_capacity_weight( bool unrestricted_pockets_only = false ) const;
        units::mass total_contained_weight( bool unrestricted_pockets_only = false ) const;

        /** Holsters */
        int get_used_holsters() const;
        int get_total_holsters() const;
        units::volume get_total_holster_volume() const;
        units::volume get_used_holster_volume() const;
        units::mass get_total_holster_weight() const;
        units::mass get_used_holster_weight() const;

        /** CONTAINER / standard / ablative pockets that are part of this item (DDA). */
        std::vector<const item_pocket *> get_container_pockets() const;
        std::vector<item_pocket *> get_container_pockets();
        std::vector<const item_pocket *> get_standard_pockets() const;
        std::vector<item_pocket *> get_standard_pockets();
        std::vector<const item_pocket *> get_ablative_pockets() const;
        std::vector<item_pocket *> get_ablative_pockets();
        std::vector<const item_pocket *> get_container_and_mod_pockets() const;
        std::vector<item_pocket *> get_container_and_mod_pockets();
        std::vector<const item_pocket *>
        get_pockets( const std::function<bool( const item_pocket & )> &filter ) const;
        std::vector<item_pocket *>
        get_pockets( const std::function<bool( const item_pocket & )> &filter );
        /** All CONTAINER/MAGAZINE/MAGAZINE WELL pockets in this item. */
        std::vector<const item_pocket *> get_all_reloadable_pockets() const;
        /** The pocket holding @p contained (DDA name of @ref pocket_of). */
        item_pocket *contained_where( const item &contained ) {
            return pocket_of( &contained );
        }
        const item_pocket *contained_where( const item &contained ) const {
            return pocket_of( &contained );
        }

        /**
         * @name DDA additional pockets (MOLLE `additional_pockets`)
         * Pouches attached to this item. Only the attach and detach actions add or remove them,
         * no insert or rebuild ever does. CDF: pockets are not copyable, so @ref add_pocket builds
         * new pockets owned by this item from the pouch's pocket data and moves the pouch's items
         * across; the emptied pouch stays in the additional pocket list and is handed back by
         * @ref remove_pocket. Added pockets sit just before the MIGRATION pocket.
         */
        /*@{*/
        bool has_additional_pockets() const;
        /** The pouches attached to this item (DDA get_added_pockets). */
        std::vector<const item *> get_added_pockets() const;
        std::vector<item *> get_added_pockets_mutable();
        /** The first pocket that came from the attached pouch @p index, nullptr if none. */
        const item_pocket *get_added_pocket( int index ) const;
        /** Attaches @p pouch_item, items and all; consumed. */
        void add_pocket( detached_ptr<item> &&pouch_item );
        /** DDA spelling: attaches a copy of @p pouch_item (T2). */
        void add_pocket( const item &pouch_item );
        /** Takes the pouch @p index off, with the items that are in its pockets. */
        detached_ptr<item> remove_pocket( int index );
        int get_additional_pocket_encumbrance( float mod ) const;
        /** PALS size taken up by the attached pouches. */
        int get_additional_space_used() const;
        units::mass get_additional_weight() const;
        units::volume get_additional_volume() const;
        /*@}*/

        /** Get the number of charges of liquid that can fit into the rest of the space. */
        int remaining_capacity_for_liquid( const item &liquid ) const;
        /**
         * If contents should contribute to encumbrance, return a value between 0 and 1 indicating
         * the position between minimum and maximum contribution it's currently making.
         */
        float relative_encumbrance() const;
        /** True if every pocket is rigid or we have no pockets */
        bool all_pockets_rigid() const;
        bool container_type_pockets_empty() const;

        /** @return the move cost of taking @p it out of this container. */
        int obtain_cost( const item &it ) const;
        /** @return the move cost of storing @p it into this container's CONTAINER pocket. */
        int insert_cost( const item &it ) const;

        /** Whether contents has a pocket with @p pk_type type. */
        bool has_pocket_type( pocket_type pk_type ) const;
        bool has_unrestricted_pockets() const;
        bool has_any_with( const std::function<bool( const item & )> &filter, pocket_type pk_type ) const;
        bool is_restricted_container() const;
        bool is_single_restricted_container() const;
        bool same_contents( const item_contents &rhs ) const;
        /** Get the item contained IFF one item is contained in a CONTAINER pocket, otherwise a null item reference. */
        item &only_item();
        item &first_item();
        const item &only_item() const;
        const item &first_item() const;
        const item *get_item_with( const std::function<bool( const item & )> &filter ) const;
        void remove_items_if( const std::function<bool( item & )> &filter );
        /** DDA name of the front()/back() artifacts: the first top-level item or a null reference. */
        item &legacy_front();
        const item &legacy_front() const;

        /** returns a list of pointers to all top-level items from pockets that match the predicate (T6) */
        std::vector<item *> all_items_top( const std::function<bool( item_pocket & )> &filter );
        std::vector<const item *> all_items_top( const std::function<bool( const item_pocket & )> &filter )
        const;
        /** if unloading is true it ignores items in pockets that are flagged to not unload */
        std::vector<item *> all_items_top( pocket_type pk_type, bool unloading = false );
        std::vector<const item *> all_items_top( pocket_type pk_type ) const;
        /** returns a list of pointers to all top-level items in container-like pockets */
        std::vector<item *> all_items_container_top();
        std::vector<const item *> all_items_container_top() const;
        /** returns a list of pointers to all visible or remembered content items that are not mods */
        std::vector<item *> all_known_contents();
        std::vector<const item *> all_known_contents() const;
        std::vector<item *> all_holstered_items();
        std::vector<const item *> all_holstered_items() const;
        std::vector<item *> all_ablative_armor();
        std::vector<const item *> all_ablative_armor() const;
        /// don't check top level item, do check its pockets, do check all nested
        content_newness get_content_newness( const std::set<itype_id> &read_items ) const;
        std::vector<const item *> mods() const;

        /**
         * CDF: true if removing @p mod would drop a pocket that still holds items: some pocket of
         * the three matchable types (CONTAINER, MAGAZINE, MAGAZINE_WELL) is not kept by the layout
         * the owner would have without @p mod, and is not empty. Modifies nothing.
         */
        bool mod_pockets_in_use( const item &mod ) const;

        /**
         * Restamps every pocket's capacity_mult from the installed mods' capacity_mods: reset to 1,
         * then the product over mods, MAGAZINE pockets only (DDA item_container.cpp 181-200).
         * Not serialized: rebuild_pockets and copy_from re-derive it.
         */
        void restamp_capacity_mults();
        /** all magazines compatible with any pockets. This only checks MAGAZINE_WELL */
        std::set<itype_id> magazine_compatible() const;
        /** The default magazine of the first MAGAZINE_WELL pocket, NULL_ID if there is none. */
        itype_id magazine_default() const;
        std::vector<itype_id> magazines_default() const;

        void on_pickup( Character &guy, item *avoid = nullptr );
        /** Spill items that don't fit in the container (DDA overflow( map &, pos, loc ), T5/T7). */
        void overflow( const tripoint_bub_ms &pos, Character *carrier = nullptr );
        /** Clear all items from magazine type pockets. */
        void clear_magazines();
        void clear_pockets_if( const std::function<bool( item_pocket const & )> &filter );
        void update_open_pockets();
        /** Return true if any pocket was sealed. */
        bool seal_all_pockets();
        bool all_pockets_sealed() const;
        bool any_pockets_sealed() const;
        /** Return the amount of ammo consumed. */
        int ammo_consume( int qty, const tripoint_bub_ms &pos, float fuel_efficiency = -1.0 );
        int ammo_consume( int qty, map *here, const tripoint_bub_ms &pos,
                          float fuel_efficiency = -1.0 );
        int ammo_consume_in_pocket( const std::string &id, int qty, map *here,
                                    const tripoint_bub_ms &pos );
        item *magazine_current();
        const item *magazine_current() const;
        std::vector<item *> magazines_current();
        std::vector<const item *> magazines_current() const;
        std::set<ammotype> ammo_types() const;
        int ammo_capacity( const ammotype &ammo ) const;
        item &first_ammo();
        const item &first_ammo() const;
        /** Return true if any of the pockets will spill if placed into a pocket. */
        bool will_spill() const;
        bool will_spill_if_unsealed() const;
        bool spill_open_pockets( Character &guy, const item *avoid = nullptr );
        bool is_funnel_container( units::volume &bigger_than ) const;
        /** Read the items in the MOD pocket only. */
        void read_mods( const item_contents &read_input );
        void combine( const item_contents &read_input, bool convert = false, bool into_bottom = false,
                      bool restack_charges = true, bool ignore_contents = false );

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
        /** The type's pockets with an installed magazine_adaptor applied; empty if none is.
         *  CDF: @p skip is an installed mod to leave out of the layout (a removal dry run). */
        std::vector<pocket_data> adapted_layout( const item *skip = nullptr ) const;
        /** Number of pockets the attached pouches contributed. */
        size_t added_pocket_count() const;
        /** Number of pockets in the attached pouch @p index. */
        static size_t pouch_pocket_count( const item &pouch );
        /** The iterator one past the last pocket that is not the MIGRATION pocket. */
        std::list<item_pocket>::iterator end_of_real_pockets();
        std::list<item_pocket>::const_iterator end_of_real_pockets() const;
        /** The first pocket of the attached pouch @p index. */
        std::list<item_pocket>::iterator first_pocket_of_pouch( int index );
        /** Drops all attached pouches and their pockets, items and all. */
        void clear_added_pockets();
        bool has_adaptor_mod() const;
        auto update_processing_cache() const -> void;
        /** Find the best pocket of type pk_type the item will fit in (DDA). */
        ret_val<item_pocket *> find_pocket_for( const item &it,
                                                pocket_type pk_type = pocket_type::CONTAINER );
        ret_val<const item_pocket *> find_pocket_for( const item &it,
                pocket_type pk_type = pocket_type::CONTAINER ) const;
        /**
         * Builds the pockets if the owner's type changed since they were built.
         * Pockets are derived from the type, so this is allowed on const contents.
         */
        void ensure_pockets() const;
        void refresh_top_cache() const;
        void contents_changed();

        item *owner = nullptr;
        std::list<item_pocket> pockets;
        /** Pocket data when a magazine_adaptor mod changed the type's layout; else empty. */
        std::vector<pocket_data> adapted_pockets;
        /** The type the pockets were built for. */
        const itype *pockets_type = nullptr;
        /** Attached pouches: emptied, their pockets live in `pockets` (see add_pocket). */
        location_vector<item> additional_pockets;
        units::volume additional_pockets_volume = 0_ml;
        units::volume additional_pockets_effective_volume = 0_ml;
        int additional_pockets_space_used = 0;
        mutable std::vector<item *> top_cache;
        bool pending_overflow = false;
        mutable bool processing_cache_dirty = true;
        mutable std::vector<item *> cached_processing_items;
};
