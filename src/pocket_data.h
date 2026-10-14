#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>

#include "pocket_type.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"

class JsonObject;

/**
 * Static pocket definitions (split from item_pocket.h so itype.h doesn't have to include the
 * runtime pocket class and its ownership headers).
 */

/** Data that applies to a pocket while it is sealed (DDA `sealed_data`). */
struct sealable_data {
    /** Multiplier for the spoilage rate of contained items while sealed. */
    float spoil_multiplier = 1.0f;

    void load( const JsonObject &jo );
};

/**
 * Static definition of one pocket of an item type. The JSON schema is a
 * DDA-compatible subset; see docs/en/mod/json/reference/items/item_creation.md.
 */
class pocket_data
{
    public:
        /** Volume and weight of pockets that don't define a limit. */
        static constexpr units::volume max_volume_for_container = 200000000_ml;
        static constexpr units::mass max_weight_for_container = 2000000_kilogram;

        pocket_data() = default;
        /** Used for special pockets that aren't loaded from JSON (MIGRATION etc.). */
        explicit pocket_data( pocket_type pk );

        pocket_type type = pocket_type::CONTAINER;
        /** Max volume of everything in the pocket. */
        units::volume volume_capacity = max_volume_for_container;
        /** Max weight of everything in the pocket. */
        units::mass max_contains_weight = max_weight_for_container;
        /** Max volume of a single item (or of one charge of a count-by-charges item). */
        std::optional<units::volume> max_item_volume;
        /** Min volume of a single item. */
        units::volume min_item_volume = 0_ml;
        /**
         * Longest and shortest item (DDA `max_item_length`/`min_item_length`). If unset,
         * pocket_data::effective_max_item_length() applies the pocket-type default.
         */
        std::optional<units::length> max_item_length;
        units::length min_item_length = 0_mm;

        /** Can hold liquids. */
        bool watertight = false;
        /** Can hold gases. */
        bool airtight = false;
        /** Pocket size doesn't change with its contents. */
        bool rigid = false;
        /** Holds one item (or one stack of a count-by-charges item). */
        bool holster = false;
        /**
         * CDF: personal storage of a worn garment (made from `storage`, a garment layout
         * overlay or the pocketed clothing mod). Dormant until the carried inventory moves
         * into pockets (P1c step 20): nothing can be put in, and item_pocket::is_compatible
         * refuses everything. Never set by JSON `pocket_data`. Ablative pockets are never carry.
         */
        bool carry = false;
        /** Holds armor plates. Stored now, used by ablative armor (P1e). */
        bool ablative = false;
        /** Contents spill when the owning item is put in another container. */
        bool open_container = false;
        /** Contents are visible. Stored for pocket settings (P2). */
        bool transparent = false;
        /** Protects contents from fire. */
        bool fire_protection = false;
        /** Base moves to take an item out of the pocket. */
        int moves = 100;
        float spoil_multiplier = 1.0f;
        float weight_multiplier = 1.0f;
        float volume_multiplier = 1.0f;
        /** Encumbrance contribution factor. Stored now, used by worn pockets (P1c). */
        float volume_encumber_modifier = 1.0f;
        /** Volume that contents may fill before the owning item grows. */
        units::volume magazine_well = 0_ml;

        /** If set, the pocket can be sealed and this data applies while it is. */
        std::optional<sealable_data> sealed_data;
        /**
         * CDF: synthesised from BN `unseals_into`. The pocket is sealed for as long as
         * the item has this type; opening it converts the item, which unseals it.
         */
        bool sealed_by_type = false;

        /** Only items with at least one of these flags (or matching another restriction) fit. */
        std::set<flag_id> flag_restriction;
        /** Only these ammo types fit, up to the given number of rounds each. */
        std::map<ammotype, int> ammo_restriction;
        /** Only these item types fit (or items matching another restriction). */
        std::set<itype_id> item_restriction;
        /** CDF extension: guns using one of these skills fit (BN holster `skills`). */
        std::set<skill_id> skill_restriction;
        /** CDF: refuse active items (BN holster behaviour). */
        bool no_active_items = false;

        translation description;
        translation name;

        /** True if any of the item/flag/skill restrictions is set. */
        bool has_item_restrictions() const;

        void load( const JsonObject &jo );
        /** Returns an error description, or an empty string if the data is consistent. */
        std::string check_definition() const;

        /**
         * The longest item this pocket accepts, nullopt for no limit. An explicit
         * `max_item_length` is used as written. Otherwise container pockets default to
         * cbrt( capacity ) * sqrt( 2 ) * 1.5 (DDA's default, made more generous for CDF) and
         * holster, magazine, magazine well, mod, corpse, software and migration pockets have none.
         */
        std::optional<units::length> effective_max_item_length() const;

        /** Keys DDA defines that CDF accepts and ignores. */
        static const std::set<std::string> &ignored_keys();
        /** Ignored keys seen while loading, for one warning in check_definitions. */
        static std::set<std::string> &ignored_keys_seen();
};

/** The single shared data used by every MIGRATION pocket. */
const pocket_data &migration_pocket_data();
