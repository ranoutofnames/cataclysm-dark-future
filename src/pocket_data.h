// Transplanted from DDA src/item_pocket.h (pocket_data, sealable_data, pocket_noise; DDA pin
// b4b62254ae2). CDF differences are marked // CDF:.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>

#include "flat_set.h"
#include "pocket_type.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"

class JsonObject;

/**
 * Which side of the wearer a plate pocket protects (CDF `sector` pocket field). Parsed and
 * stored for the directional plate model (docs/directional-facing.md); no code reads it
 * yet, the DDA slice model ignores it.
 */
enum class plate_sector : int {
    ANY,
    FRONT,
    BACK,
    LEFT,
    RIGHT,
};

/**
 * Static pocket definitions (split from item_pocket.h so itype.h doesn't have to include the
 * runtime pocket class and its ownership headers).
 */

/** Data that applies to a pocket while it is sealed (DDA `sealed_data`). */
struct sealable_data {
    // required for generic_factory (DDA)
    bool was_loaded = false;
    /** Multiplier for the spoilage rate of contained items while sealed. */
    float spoil_multiplier = 1.0f;

    void load( const JsonObject &jo );
};

/** Noise a pocket makes when moving (DDA `pocket_noise`). */
struct pocket_noise {
    // required for generic_factory (DDA)
    bool was_loaded = false;
    /** Volume of the noise. */
    int volume = 0;
    /** Chance of the noise. */
    int chance = 0;

    void load( const JsonObject &jo );
};

/**
 * Static definition of one pocket of an item type. The JSON schema is a
 * DDA-compatible subset; see docs/en/mod/json/reference/items/item_creation.md.
 */
class pocket_data
{
    public:
        using FlagsSetType = cata::flat_set<flag_id>;

        /** Volume and weight of pockets that don't define a limit. */
        static constexpr units::volume max_volume_for_container = 200000000_ml;
        static constexpr units::mass max_weight_for_container = 2000000_kilogram;

        // required for generic_factory (DDA)
        bool was_loaded = false;

        pocket_data() = default;
        /** Used for special pockets that aren't loaded from JSON (MIGRATION etc.). */
        explicit pocket_data( pocket_type pk );

        pocket_type type = pocket_type::CONTAINER;
        /**
         * Max volume of everything in the pocket as written in JSON (DDA `raw_volume_capacity`).
         * Loaders and definition checks use this field; game code uses volume_capacity().
         */
        units::volume raw_volume_capacity = max_volume_for_container;
        /** Max volume of everything in the pocket (DDA `volume_capacity()`). */
        units::volume volume_capacity() const;
        /** Max weight of everything in the pocket. */
        units::mass max_contains_weight = max_weight_for_container;
        /** Max volume of a single item (or of one charge of a count-by-charges item). */
        std::optional<units::volume> max_item_volume;
        /** Min volume of a single item. */
        units::volume min_item_volume = 0_ml;
        /**
         * Longest and shortest item (DDA `max_item_length`/`min_item_length`). As in DDA the
         * JSON loader fills in the default (cbrt( capacity ) * sqrt( 2 )) when the key is missing,
         * and a pocket built in C++ starts at 0 mm, so it must set a limit or call
         * apply_default_length().
         */
        units::length max_item_length = 0_mm;
        units::length min_item_length = 0_mm;
        /** Sets max_item_length to DDA's default for raw_volume_capacity (see pocket_length_factor). */
        void apply_default_length();

        /** Can hold liquids. */
        bool watertight = false;
        /** Can hold gases. */
        bool airtight = false;
        /** Pocket size doesn't change with its contents. */
        bool rigid = false;
        /** Holds one item (or one stack of a count-by-charges item). */
        bool holster = false;
        /**
         * CDF: personal storage of a worn garment. The carry pockets of the worn garments (and
         * the body storage) form the character's carried inventory. Set for the pockets made
         * from `storage`, a garment layout overlay or the pocketed clothing mod, and by the
         * loader (CARRY1) for the CONTAINER pockets of an armor item with explicit `pocket_data`
         * that are not holster or ablative and whose JSON has no `carry` key. JSON `"carry": false`
         * opts a pocket out. Ablative pockets are never carry.
         */
        bool carry = false;
        /** CDF: the JSON of this pocket had a `carry` key (the loader default does not override it). */
        bool carry_given = false;
        /**
         * Holds one armor plate (DDA `ablative`). A plate slot: it only takes items matching
         * its flag/item restriction, is never a carry pocket, and is skipped by best_pocket,
         * pickup and overflow.
         */
        bool ablative = false;
        /** CDF: side of the wearer the plate in this slot covers. Not used yet, see plate_sector. */
        plate_sector sector = plate_sector::ANY;
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
        /** DDA `extra_encumbrance`: loaded, used by armor code in J5. */
        int extra_encumbrance = 0;
        /** DDA `ripoff`: loaded, no escape hook (D15). */
        int ripoff = 0;
        /** DDA `activity_noise`: loaded, used by armor code in J5. */
        pocket_noise activity_noise;
        /** DDA `insulation`: loaded, no consumer yet. */
        float insulation = 1.0f;
        /** DDA `inherits_flags`: loaded, no consumer yet. */
        bool inherits_flags = false;
        /** DDA `forbidden`: loaded, no consumer yet (owner decision Q8). */
        bool forbidden = false;
        /** DDA `material_restriction`: loaded, no consumer yet. */
        cata::flat_set<material_id> material_restriction;
        /** DDA `allowed_speedloaders`: loaded, no consumer yet (P3). */
        cata::flat_set<itype_id> allowed_speedloaders;
        /** DDA `default_magazine`: loaded, no consumer yet. */
        itype_id default_magazine = itype_id::NULL_ID();
        /** DDA `id`: loaded as pocket_id, no consumer yet. */
        std::string pocket_id;
        /** DDA `_no_unload`: set from the item's NO_UNLOAD flag when the item type is finalized. */
        bool _no_unload = false;
        /** DDA `_no_reload`: set from the item's NO_RELOAD flag when the item type is finalized. */
        bool _no_reload = false;
        /** Volume that contents may fill before the owning item grows. */
        units::volume magazine_well = 0_ml;

        /** If set, the pocket can be sealed and this data applies while it is. */
        std::optional<sealable_data> sealed_data;
        /**
         * CDF: synthesised from BN `unseals_into`. The pocket is sealed for as long as
         * the item has this type; opening it converts the item, which unseals it.
         */
        bool sealed_by_type = false;

        /** Only these ammo types fit, up to the given number of rounds each. */
        std::map<ammotype, int> ammo_restriction;
        /** Only these item types fit (or items matching another restriction). DDA `item_id_restriction`. */
        cata::flat_set<itype_id> item_id_restriction;
        /** CDF: refuse active items (BN holster behaviour). */
        bool no_active_items = false;

        /** A description of the pocket. */
        translation description;
        /** The name of the item the pocket belongs to. */
        translation name;
        /** DDA `pocket_name`: optional short name of the pocket. */
        translation pocket_name;

        /** Only items with at least one of these flags (or matching another restriction) fit. */
        const FlagsSetType &get_flag_restrictions() const;
        /** DDA test hook: adds one flag restriction. */
        void add_flag_restriction( const flag_id &flag );

        /** True if any of the item/flag/skill restrictions is set. */
        bool has_item_restrictions() const;

        void load( const JsonObject &jo );
        /** Returns an error description, or an empty string if the data is consistent. */
        std::string check_definition() const;
        bool operator==( const pocket_data &rhs ) const;


    private:
        FlagsSetType flag_restrictions;
};

/** The single shared data used by every MIGRATION pocket. */
const pocket_data &migration_pocket_data();
