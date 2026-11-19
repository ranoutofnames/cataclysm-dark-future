#pragma once

#include "itype.h"

struct islot_armor {
    // thresholds for an item to count as hard / comfortable to wear (DDA itype.h)
    static constexpr int test_threshold = 40;
    /**
    * Whether this item can be worn on either side of the body
    */
    bool sided = false;
    /**
     * Multiplier on resistances provided by armor's materials.
     * Damaged armors have lower effective thickness, low capped at 1.
     * Note: 1 thickness means item retains full resistance when damaged.
     */
    int thickness = 0;
    /**
     * Damage negated by this armor. Usually calculated from materials+thickness.
     */
    resistances resistance;
    /**
     * Resistance to environmental effects.
     */
    int env_resist = 0;
    /**
     * Environmental protection of a gas mask with installed filter.
     */
    int env_resist_w_filter = 0;
    /**
     * How much warmth this item provides.
     */
    int warmth = 0;
    /**
     * How much storage this items provides when worn.
     */
    units::volume storage = 0_ml;
    /**
    * Factor modifying weight capacity
    */
    float weight_capacity_modifier = 1.0;
    /**
    * Bonus to weight capacity
    */
    units::mass weight_capacity_bonus = 0_gram;

    /**
    * Sound dampening in dB spl provided by the item when worn.
    * This value decreased the volume of all heard sounds.
    */
    int hearing_protection = 0;
    /**
    * Advanced sound dampening in dB spl provided by the item when worn.
    * This value only decreases heard volume for purposes of determining deafness,
    * allowing the wearer to hear other sounds normally.
    */
    int adv_hearing_protection = 0;

    bool was_loaded;
    /**
     * What this armor turns into when it is destroyed while worn inside a carrier (an armor
     * plate that shatters leaves a "compromised" plate). Null: it is simply destroyed.
     */
    itype_id non_functional = itype_id::NULL_ID();
    /** The verb in "Your %1$s %2$s!" when the plate turns into @ref non_functional. */
    translation damage_verb;
    /**
     * Whitelisted clothing mods.
     * Restricted clothing mods must be listed here by id to be compatible.
     */
    std::vector<std::string> valid_mods;
    // Layer, encumbrance and coverage information.
    std::vector<armor_portion_data> data;
    /**
     * Portions given per sub-location (`specifically_covers`). Each has `sub_coverage` set.
     * `data` holds the per-body-part values derived from these, plus any portions that
     * name no sub-locations, so every reader of `data` keeps working. Empty for items
     * without sub-location data.
     */
    std::vector<armor_portion_data> sub_data;
    /** Set once `data` has been derived from `sub_data` (Item_factory::finalize_pre). */
    bool sub_data_derived = false;

    // DDA islot_armor fields (J1 declarations only; loaded and used by J3-J5)
    bool ablative = false;
    bool additional_pocket_enc = false;
    /** Some pocket of this item makes noise while its wearer moves (DDA `activity_noise`). */
    bool noisy = false;
    bool ripoff_chance = false;
    bool rigid = false;
    bool comfortable = true;

    // NOTE: Theoretically this will one day become clothing modifications
    // Today is not that day, but this will set a suitable framework
    std::map<flag_id, int> provides_slot;
    std::map<flag_id, int> consumes_slot;
};
