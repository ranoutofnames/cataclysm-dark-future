#pragma once

#include "itype.h"

struct islot_pet_armor {
    /**
     * TODO: document me.
     */
    int thickness = 0;
    /**
     * Resistance to environmental effects.
     */
    int env_resist = 0;
    /**
     * Environmental protection of a gas mask with installed filter.
     */
    int env_resist_w_filter = 0;
    /**
     * How much storage this items provides when worn.
     */
    units::volume storage = 0_ml;
    /**
     * The maximum volume a pet can be and wear this armor (exclusive)
     */
    units::volume max_vol = 0_ml;
    /**
     * The minimum volume a pet can be and wear this armor
     */
    units::volume min_vol = 0_ml;
    /**
     * What animal bodytype can wear this armor
     */
    std::string bodytype = "none";
};
