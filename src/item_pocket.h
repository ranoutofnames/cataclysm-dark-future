#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "detached_ptr.h"
#include "location_vector.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"

class JsonObject;
class item;
struct itype;

/**
 * Pockets: CDF re-implementation of DDA's item pockets (DDA #39406 and follow-ups).
 *
 * DDA stores `std::list<item>` per pocket. CDF keeps BN's ownership model:
 * every pocket owns a `location_vector<item>` whose location points at the item
 * owning the pocket, so contained items still report the owner as their parent.
 */

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
            // the item wouldn't fit even if the pocket were empty
            ERR_TOO_BIG,
            // the item would be too heavy even if the pocket were empty
            ERR_TOO_HEAVY,
            // the item is below the minimum size
            ERR_TOO_SMALL,
            // not enough volume left
            ERR_NO_SPACE,
            // not enough weight capacity left
            ERR_CANNOT_SUPPORT,
            // the item doesn't match the item/flag/skill restrictions
            ERR_FLAG,
            // the item isn't an accepted ammo type
            ERR_AMMO,
            // the pocket is sealed
            ERR_SEALED,
            // the holster already holds an item
            ERR_HOLSTER_FULL,
            // the item is active and the pocket refuses active items
            ERR_ACTIVE,
        };

        item_pocket( item *owner, const pocket_data *data );
        item_pocket( const item_pocket & ) = delete;
        item_pocket &operator=( const item_pocket & ) = delete;
        ~item_pocket();

        const pocket_data &get_data() const {
            return *data;
        }
        /** Points the pocket at new data of the same type, keeping its items. */
        void set_data( const pocket_data *new_data ) {
            data = new_data;
        }
        pocket_type get_type() const {
            return data->type;
        }
        bool is_type( pocket_type t ) const {
            return data->type == t;
        }
        bool is_holster() const {
            return data->holster;
        }
        bool watertight() const {
            return data->watertight;
        }
        bool airtight() const {
            return data->airtight;
        }
        bool rigid() const {
            return data->rigid;
        }
        int moves() const {
            return data->moves;
        }
        /** MIGRATION, MOD, CORPSE and SOFTWARE pockets have no capacity rules. */
        bool is_standard_type() const;

        /** Sealed pockets accept nothing and are skipped by best_pocket. */
        bool sealed() const;
        void seal();
        void unseal();
        /** Saved per-pocket sealed state (only for pockets with sealed_data). */
        bool saved_sealed() const {
            return _sealed;
        }

        units::volume volume_capacity() const;
        units::mass weight_capacity() const;
        units::volume contains_volume() const;
        units::mass contains_weight() const;
        units::volume remaining_volume() const;
        units::mass remaining_weight() const;
        /** Rounds of the given ammo type the pocket holds when full (0 if it can't hold it). */
        int ammo_capacity( const ammotype &ammo ) const;
        int remaining_ammo_capacity( const ammotype &ammo ) const;

        bool empty() const;
        size_t size() const;
        /** A holster pocket holding an item. */
        bool holster_full() const;
        /**
         * True if this is an ammo-restricted pocket (bandolier) that takes at least
         * one round of @p it: the right kind of ammo, no other ammo mixed in, room left.
         */
        bool accepts_some_ammo( const item &it ) const;
        /** True if a holster pocket of @p t accepts @p it when empty. */
        static bool type_can_holster( const itype &t, const item &it );
        /** Holds more than it may: over volume/weight/ammo capacity, or a full holster with more. */
        bool is_overfull() const;

        /** Type checks: phase, restrictions. Ignores what the pocket already holds. */
        ret_val<contain_code> is_compatible( const item &it ) const;
        /**
         * Full check (#44356). If @p ignore_contents is set, only checks whether
         * the item could fit in the empty pocket.
         */
        ret_val<contain_code> can_contain( const item &it, bool ignore_contents = false,
                                           bool ignore_sealed = false ) const;
        /**
         * True if @p rhs is a better place for @p it than this pocket (DDA #40286).
         * @param nested true if both pockets are inside a contained item
         */
        bool better_pocket( const item_pocket &rhs, const item &it, bool nested = false ) const;
        /** An item in the pocket that @p it would stack with. */
        bool has_item_stacks_with( const item &it ) const;
        float spoil_multiplier() const;

        /**
         * Puts an item in this pocket without checks. Count-by-charges items are
         * merged into an existing stack where possible (#87582, #69378).
         */
        void insert( detached_ptr<item> &&it );

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

        location_vector<item> &items() {
            return contents;
        }
        const location_vector<item> &items() const {
            return contents;
        }

    private:
        const pocket_data *data;
        location_vector<item> contents;
        bool _sealed = false;

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
};

template<>
struct ret_val<item_pocket::contain_code>::default_success
    : public std::integral_constant<item_pocket::contain_code,
      item_pocket::contain_code::SUCCESS> {};

template<>
struct ret_val<item_pocket::contain_code>::default_failure
    : public std::integral_constant<item_pocket::contain_code,
      item_pocket::contain_code::ERR_NO_SPACE> {};
