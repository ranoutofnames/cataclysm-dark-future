#pragma once

#include "itype.h"

struct islot_seed {
    /**
     * Time it takes for a seed to grow (based of off a season length of 91 days).
     */
    time_duration grow = 0_turns;
    /**
     * Amount of harvested charges of fruits is divided by this number.
     */
    int fruit_div = 1;
    /**
     * Name of the plant.
     */
    translation plant_name;
    /**
     * Type id of the fruit item.
     */
    itype_id fruit_id;
    /**
     * Whether to spawn seed items additionally to the fruit items.
     */
    bool spawn_seeds = true;
    /**
     * Additionally items (a list of their item ids) that will spawn when harvesting the plant.
     */
    std::vector<itype_id> byproducts;
    /**
     * Terrain tag required to plant the seed.
     */
    std::string required_terrain_flag = "PLANTABLE";
    /// Flags copied from the planted item to harvested fruits and seeds, excluding byproducts.
    std::vector<flag_id> harvest_inherit_flags;
    /// Copy the planted item's poison value to harvested fruits and seeds.
    bool harvest_inherit_poison = false;
    islot_seed() = default;

    /**
     * Time it takes to grow from one stage to another. There are 4 plant stages:
     * seed, seedling, mature and harvest. Non-seed items return 0.
     */
    time_duration get_plant_epoch() const;
};
