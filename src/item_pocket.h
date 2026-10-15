#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "coordinates.h"
#include "detached_ptr.h"
#include "enums.h"
#include "flat_set.h"
#include "location_vector.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"

class Character;
class JsonArray;
class JsonIn;
class JsonObject;
class JsonOut;
class item;
class item_pocket;
struct iteminfo;
struct itype;

/**
 * Pockets: transplant of DDA's `item_pocket` (src/item_pocket.h, DDA pin b4b62254ae2).
 *
 * DDA stores `std::list<item>` per pocket. CDF keeps BN's ownership model (rules T1-T4 of
 * plans/P1-transplant/COMMON.md): every pocket owns a `location_vector<item>` whose location
 * points at the item owning the pocket, so contained items still report the owner as their
 * parent. Anything that differs from DDA is tagged `// CDF:` in the implementation.
 */

/** The pocket chosen to take an item, and the item that owns it (DDA: `std::pair<item_location, item_pocket *>`). */
struct pocket_choice {
    item *container = nullptr;
    item_pocket *pocket = nullptr;
};

class item_pocket
{
    public:
        enum class contain_code : int {
            SUCCESS,
            // only mods can go into the pocket for mods
            ERR_MOD,
            // trying to put a liquid into a non-watertight container
            ERR_LIQUID,
            // trying to put a gas into a non-airtight container
            ERR_GAS,
            // trying to store an item that wouldn't fit even if the container were empty
            ERR_TOO_BIG,
            // trying to store an item that would be too heavy even if the container were empty
            ERR_TOO_HEAVY,
            // trying to store an item that's below the minimum size or length
            ERR_TOO_SMALL,
            // pocket doesn't have sufficient volume capacity left
            ERR_NO_SPACE,
            // pocket doesn't have sufficient weight capacity left
            ERR_CANNOT_SUPPORT,
            // requires a flag the item doesn't have
            ERR_FLAG,
            // requires the item to be a specific ammotype
            ERR_AMMO
        };

        class favorite_settings
        {
            public:
                void clear();

                void set_priority( int priority );
                int priority() const {
                    return priority_rating;
                }

                // have these settings been modified by the player?
                bool is_null() const;

                void whitelist_item( const itype_id &id );
                void blacklist_item( const itype_id &id );
                void clear_item( const itype_id &id );

                const cata::flat_set<itype_id> &get_item_whitelist() const;
                const cata::flat_set<itype_id> &get_item_blacklist() const;
                const cata::flat_set<item_category_id> &get_category_whitelist() const;
                const cata::flat_set<item_category_id> &get_category_blacklist() const;

                void whitelist_category( const item_category_id &id );
                void blacklist_category( const item_category_id &id );
                void clear_category( const item_category_id &id );

                /** Whether an item passes the current whitelist and blacklist filters. */
                bool accepts_item( const item &it ) const;

                /** Are pocket contents hidden?*/
                bool is_collapsed() const;
                /** Flag to show or hide the pocket contents in 'i'nventory screen. */
                void set_collapse( bool );

                // functions for setting if the pocket is completely disabled for normal pickup
                bool is_disabled() const;
                void set_disabled( bool );

                // functions for setting if the pocket will unload during normal actions
                bool is_unloadable() const;
                void set_unloadable( bool );

                const std::optional<std::string> &get_preset_name() const;
                void set_preset_name( const std::string & );

                void set_was_edited();
                bool was_edited() const;

                void info( std::vector<iteminfo> &info ) const;

                // CDF: defined with the pocket (de)serialization in savegame_json.cpp (J3)
                void serialize( JsonOut &json ) const;
                void deserialize( JsonIn &jsin );
                void deserialize( const JsonObject &data );
            private:
                std::optional<std::string> preset_name;
                int priority_rating = 0;
                cata::flat_set<itype_id> item_whitelist;
                cata::flat_set<itype_id> item_blacklist;
                cata::flat_set<item_category_id> category_whitelist;
                cata::flat_set<item_category_id> category_blacklist;
                bool collapsed = false;
                bool disabled = false;
                bool unload = true;
                bool player_edited = false;
        };

        /** @p owner is the item the pocket belongs to; nullptr for a pocket outside any item. */
        item_pocket( item *owner, const pocket_data *data );
        /**
         * CDF: the ownerless constructor that DDA's `item_pocket( &data )` needs (tests build
         * standalone pockets). The pocket holds items but has no owner to report as their parent.
         */
        explicit item_pocket( const pocket_data *data );
        item_pocket( const item_pocket & ) = delete;
        item_pocket &operator=( const item_pocket & ) = delete;
        ~item_pocket();

        bool stacks_with( const item_pocket &rhs, int depth = 0, int maxdepth = 2 ) const;
        bool is_funnel_container( units::volume &bigger_than ) const;
        bool is_restricted() const;
        bool has_any_with( const std::function<bool( const item & )> &filter ) const;

        bool is_type( pocket_type ptype ) const;
        bool is_type( const std::set<pocket_type> &ptype ) const;
        bool is_ablative() const;
        bool is_holster() const;
        // checks if the pocket is a holster and if it has something in it
        bool holster_full() const;
        bool empty() const;
        bool full( bool allow_bucket ) const;

        // Convenience accessors for pocket data attributes with the same name
        bool rigid() const;
        bool watertight() const;
        bool airtight() const;
        bool inherits_flags() const;
        // open pockets are always considered transparent
        bool transparent() const;
        // is this speedloader compatible with this pocket (if any speedloaders are whitelisted)
        bool allows_speedloader( const itype_id &speedloader_id ) const;

        // is this pocket one of the standard types?
        // exceptions are MOD, CORPSE, MIGRATION, etc.
        bool is_standard_type() const;
        // is this pocket a type that acts like a container in its underlying implementation?
        bool is_container_like_type() const;

        bool is_forbidden() const;

        const translation &get_description() const;
        const translation &get_name() const;

        const pocket_data *get_pocket_data() const;
        /** CDF: the pocket's data by reference (older BN/CDF spelling of get_pocket_data()). */
        const pocket_data &get_data() const {
            return *data;
        }
        /** CDF: points the pocket at new data of the same type, keeping its items. */
        void set_data( const pocket_data *new_data ) {
            data = new_data;
        }
        /** CDF: the pocket's type (older spelling of the DDA `is_type` test). */
        pocket_type get_type() const {
            return data->type;
        }
        /** CDF: worn storage (see pocket_data::carry). */
        bool is_carry() const {
            return data->carry;
        }
        int moves() const;

        // CDF: T6, the DDA `std::list<item *>` results are vectors
        std::vector<item *> all_items_top();
        std::vector<const item *> all_items_top() const;
        std::vector<item *> all_items_ptr( pocket_type pk_type );
        std::vector<const item *> all_items_ptr( pocket_type pk_type ) const;

        item &back();
        const item &back() const;
        item &front();
        const item &front() const;
        /** Number of items in this pocket. */
        size_t size() const;
        void pop_back();

        /**
         * Is the pocket compatible with the specified item?
         * Does not check if the item actually fits volume/weight wise
         * @param it the item being put in
         */
        ret_val<contain_code> is_compatible( const item &it ) const;

        /**
         * Can the pocket contain the specified item?
         * @param it The item being put in
         * @param ignore_contents If true, only check for compatible phase, size, and weight, skipping the more CPU-intensive checks against other contents. Optional, default false.
         * @param copies_remaining An optional integer reference that will be set to the number of item copies that won't fit
         */
        ret_val<contain_code> can_contain( const item &it, bool ignore_contents = false ) const;
        ret_val<contain_code> can_contain( const item &it, int &copies_remaining,
                                           bool ignore_contents = false ) const;

        bool can_contain_liquid( bool held_or_ground ) const;
        bool contains_phase( phase_id phase ) const;

        units::length max_containable_length() const;
        units::length min_containable_length() const;

        units::volume volume_capacity() const;
        // combined volume of contained items
        units::volume contents_volume() const;
        /** CDF: older spelling of contents_volume(). */
        units::volume contains_volume() const {
            return contents_volume();
        }
        units::volume remaining_volume() const;

        // While bulk-fill is active, contents_volume()/contains_weight() return
        // a running total updated on each insertion rather than re-summing all
        // contents. CDF: no-ops (DEVIATION, perf only): the P1d size cache covers this.
        void begin_bulk_fill();
        void end_bulk_fill();
        // how many more of @it can this pocket hold?
        int remaining_capacity_for_item( const item &it ) const;
        // the amount of space this pocket can hold before it starts expanding
        units::volume magazine_well() const;

        units::mass weight_capacity() const;
        // combined weight of contained items
        units::mass contains_weight() const;
        units::mass remaining_weight() const;
        // these avoid rounding errors and are preferred over
        // `it.charges_per_volume( pocket.remaining_volume() )` or
        // `it.charges_per_weight( pocket.remaining_weight() )`
        int charges_per_remaining_volume( const item &it ) const;
        int charges_per_remaining_weight( const item &it ) const;

        units::volume item_size_modifier() const;
        units::mass item_weight_modifier() const;
        units::length item_length_modifier() const;

        /** gets the spoilage multiplier depending on sealed data */
        float spoil_multiplier() const;

        float insulation() const;

        int best_quality( const quality_id &id ) const;

        // returns a list of pointers of all gunmods in the pocket
        std::vector<item *> gunmods();
        // returns a list of pointers of all gunmods in the pocket
        std::vector<const item *> gunmods() const;
        cata::flat_set<itype_id> item_type_restrictions() const;
        item *magazine_current();
        const item *magazine_current() const;
        // returns the default magazine if MAGAZINE_WELL, otherwise NULL_ID
        itype_id magazine_default() const;
        // returns amount of ammo consumed
        int ammo_consume( int qty );
        // returns all allowable ammotypes
        std::set<ammotype> ammo_types() const;
        int ammo_capacity( const ammotype &ammo ) const;
        int remaining_ammo_capacity( const ammotype &ammo ) const;
        /** CDF: true if this is an ammo-restricted pocket that takes at least one round of @p it. */
        bool accepts_some_ammo( const item &it ) const;
        /** CDF: true if a holster pocket of @p t accepts @p it when empty. */
        static bool type_can_holster( const itype &t, const item &it );
        /** CDF: holds more than it may: over volume/weight/ammo capacity, or a full holster with more. */
        bool is_overfull() const;
        /** CDF: per-instance capacity of the body storage pocket (enchantment sized). */
        void set_capacity_override( const std::optional<units::volume> &cap ) {
            capacity_override = cap;
        }
        const std::optional<units::volume> &get_capacity_override() const {
            return capacity_override;
        }

        void casings_handle( const std::function < detached_ptr<item>( detached_ptr<item> && ) > &func );
        // CDF: T2/T3, the DDA `std::list<item> &used` is a vector of detached items
        bool use_amount( const itype_id &it, int &quantity, std::vector<detached_ptr<item>> &used );
        bool will_explode_in_a_fire() const;
        // will the items inside this pocket fall out of this pocket if it is placed into another item?
        bool will_spill() const;
        bool will_spill_if_unsealed() const;
        // seal the pocket. returns false if it fails (pocket does not seal)
        bool seal();
        // unseal the pocket.
        void unseal();
        /**
         * A pocket is sealable IFF it has "sealed_data". Sealable pockets are sealed by calling seal().
         * If a pocket is not sealable, it is never considered "sealed".
         */
        bool sealable() const;
        /**
         * A pocket is sealed when item spawns if pocket has "sealed_data".
         * A pocket can never be sealed unless it is sealable (having "sealed_data").
         * If a pocket is sealed, you cannot interact with it until the seal is broken with unseal().
         * CDF: a pocket of a `sealed_by_type` item (BN unseals_into) is sealed for as long as the
         * item has that type.
         */
        bool sealed() const;

        std::string translated_sealed_prefix() const;
        /** Detonates what explodes; returns true if the container explodes with it. */
        bool detonate( const tripoint_bub_ms &p, std::vector<detached_ptr<item>> &drops );

        void remove_all_ammo( Character &guy );
        void remove_all_mods( Character &guy );

        // removes and returns the item from the pocket (T3). Null if the item is not directly in it.
        detached_ptr<item> remove_item( item *it );
        // spills any contents that can't fit into the pocket, largest items first
        // @p parent_item is the item owning the pocket, @p carrier whoever holds it (T5, T7)
        void overflow( const tripoint_bub_ms &pos, item *parent_item, Character *carrier );
        bool spill_contents( const tripoint_bub_ms &pos );
        void on_pickup( Character &guy, item *avoid = nullptr );
        void on_contents_changed();
        void handle_liquid_or_spill( Character &guy, const item *avoid = nullptr );
        // CDF: returns the detached items so the owner decides where they go
        std::vector<detached_ptr<item>> clear_items();
        bool has_item( const item &it ) const;
        item *get_item_with( const std::function<bool( const item & )> &filter );
        const item *get_item_with( const std::function<bool( const item & )> &filter ) const;
        void remove_items_if( const std::function<bool( item & )> &filter );

        pocket_type saved_type() const {
            return _saved_type;
        }

        bool saved_sealed() const {
            return _saved_sealed;
        }

        float get_capacity_mult() const {
            return capacity_mult;
        }
        void set_capacity_mult( float m ) {
            capacity_mult = m;
        }

        /**
         * Tries to put an item in the pocket (T2). The item is only consumed on success.
         * The checked path: on failure the result holds the reason and @p it is untouched.
         */
        ret_val<item *> insert_item( detached_ptr<item> &&it, bool into_bottom = false,
                                     bool restack_charges = true, bool ignore_contents = false );
        /** The DDA spelling: inserts a copy of @p it. Never use it to move an existing item. */
        ret_val<item *> insert_item( const item &it, bool into_bottom = false,
                                     bool restack_charges = true, bool ignore_contents = false );
        /**
          * adds an item to the pocket with no checks
          * may create a new pocket
          */
        void add( detached_ptr<item> &&it, item **ret = nullptr );
        void add( const item &it, item **ret = nullptr );
        void add( const item &it, int copies, std::vector<item *> &added );
        /**
         * CDF: older name of the unchecked insert. Puts an item in this pocket without checks.
         * Count-by-charges items are merged into an existing stack where possible.
         */
        void insert( detached_ptr<item> &&it );
        bool can_unload_liquid() const;

        int fill_with( const item &contained, Character &guy, int amount = 0,
                       bool allow_unseal = false, bool ignore_settings = false );

        /**
        * @brief Check contents of pocket to see if it contains a valid item/pocket to store the given item.
        * @param this_item The item owning this pocket (DDA: the `item_location` of it, T5).
        * @param it The item to try and store.
        * @param avoid Item to avoid trying to search for/inside of.
        *
        * @returns A non-nullptr pocket if a suitable pocket is found.
        */
        pocket_choice best_pocket_in_contents( item &this_item, const item &it, const item *avoid,
                                               bool allow_sealed, bool ignore_settings );

        // cost of getting an item from this pocket
        // @TODO: make move cost vary based on other contained items
        int obtain_cost( const item &it ) const;

        void general_info( std::vector<iteminfo> &info, int pocket_number, bool disp_pocket_number ) const;
        void contents_info( std::vector<iteminfo> &info, int pocket_number, bool disp_pocket_number ) const;
        void favorite_info( std::vector<iteminfo> &info ) const;

        // CDF: defined with the item serialization in savegame_json.cpp (J3)
        void serialize( JsonOut &json ) const;
        void deserialize( const JsonObject &data );

        // true if pocket state is the same as if freshly created from the pocket type
        bool is_default_state() const;

        bool same_contents( const item_pocket &rhs ) const;
        /** stacks like items inside the pocket */
        void restack();
        /** same as restack(), except returns the stack where input item was placed */
        item *restack( /*const*/ item *it );
        bool has_item_stacks_with( const item &it ) const;

        /**
         * Whether another pocket is a better fit for an item.
         *
         * This assumes that both pockets are able to and allowed to contain the item.
         */
        bool better_pocket( const item_pocket &rhs, const item &it, bool nested = false ) const;

        bool operator==( const item_pocket &rhs ) const;

        favorite_settings settings;

        // Set whether rigid items are blocked in the pocket
        void set_no_rigid( const std::set<sub_bodypart_id> &is_no_rigid );

        // should the name of this pocket be used as a description
        bool name_as_description = false; // NOLINT(cata-serialize)

        // pocket predicates
        // non-forbidden, container-type pockets. The most common use-case.
        static bool ok_default_containers( const item_pocket &pocket );
        // *any* container-type pocket, even restricted ones.
        // appropriate for determining how physically full something is.
        static bool ok_all_containers( const item_pocket & );

        // pocket is currently OK with taking on solid (as opposed to liquid/gaseous) items
        static bool ok_for_solids( const item_pocket &pocket );

        /**
         * Drops the cached volume and weight of what the pocket holds. The cache only
         * holds sums of items whose size cannot change on its own (item::size_is_stable)
         * and is also checked against the item list, so adding or removing items by any
         * route is safe. Call it when changing something size-related of a contained item
         * (item::invalidate_pocket_caches_upwards does, for every pocket above it).
         */
        void invalidate_cache() const {
            cache.state = size_cache::state_t::NONE;
        }

        // CDF: T1, the contained items (DDA: `edit_contents()` returning the list)
        location_vector<item> &items() {
            return contents;
        }
        const location_vector<item> &items() const {
            return contents;
        }
    private:
        // the type of pocket, saved to json
        pocket_type _saved_type = pocket_type::LAST; // NOLINT(cata-serialize)
        bool _saved_sealed = false; // NOLINT(cata-serialize)
        const pocket_data *data = nullptr; // NOLINT(cata-serialize)
        // the items inside the pocket (T1)
        location_vector<item> contents;
        bool _sealed = false;
        // Stamped from installed mods' capacity_mods; rederived on rebuild, not serialized.
        float capacity_mult = 1.0f; // NOLINT(cata-serialize)
        // list of sub body parts that can't currently support rigid ablative armor
        std::set<sub_bodypart_id> no_rigid;
        std::optional<units::volume> capacity_override;

        struct size_cache {
            enum class state_t : int {
                NONE, // nothing known
                STABLE, // the sums are valid while the item list is unchanged
                UNSTABLE, // something inside may change size on its own: sum every time
            };
            state_t state = state_t::NONE;
            bool volume_known = false;
            bool weight_known = false;
            size_t count = 0;
            uintptr_t signature = 0;
            units::volume volume = 0_ml;
            units::mass weight = 0_gram;
        };
        mutable size_cache cache;
        /** Revalidates the cache against the current item list. */
        void refresh_cache() const;

        ret_val<contain_code> _can_contain( const item &it, int &copies_remaining,
                                            bool ignore_contents ) const;
};

class pocket_constraint
{
    public:
        units::mass weight_capacity = pocket_data::max_weight_for_container;
        units::length max_containable_length = 200000_m;
        units::volume volume_capacity = pocket_data::max_volume_for_container;
        units::volume remaining_volume = pocket_data::max_volume_for_container;
        units::volume max_item_volume = 0_ml; // literal bottlenecks; 0 = no limit

        // pocket is rigid or constrained by a rigid pocket, volume won't be reduced by further constraints
        bool in_rigid = false;
        // pocket 'X' is dominated by pocket 'Y' if X is nested in Y,
        // any item that could be put into X could be put into Y,
        // and X's volume expands into Y (no rigid pockets in the way).
        // dominated pockets are redundant when describing available spaces.
        bool is_dominated = false;

        pocket_constraint() = default;
        explicit pocket_constraint( const item_pocket *init_pocket );

        // returns whether the constraint is equally severe as 'other'. not a strict field-wise equality.
        bool operator==( const pocket_constraint & ) const;
        bool operator!=( const pocket_constraint & ) const;

        // apply the capacity constraints of an outer pocket
        void constrain_by( const item_pocket *outer );
        // apply the capacity constraints of another, outer constraint to this one.
        // this never dominates the pocket.
        void constrain_by( const pocket_constraint &outer );
};

template<>
struct ret_val<item_pocket::contain_code>::default_success
    : public std::integral_constant<item_pocket::contain_code,
      item_pocket::contain_code::SUCCESS> {};

template<>
struct ret_val<item_pocket::contain_code>::default_failure
    : public std::integral_constant<item_pocket::contain_code,
      item_pocket::contain_code::ERR_NO_SPACE> {};
