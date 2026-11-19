#pragma once

#include "itype.h"

struct islot_container {
    /**
     * Inner volume of the container.
     */
    units::volume contains = 0_ml;
    /**
     * Can be resealed.
     */
    bool seals = false;
    /**
     * Can hold liquids.
     */
    bool watertight = false;
    /**
     * Contents do not spoil.
     */
    bool preserves = false;
    /**
     * If this is set to anything but "null", changing this container's contents in any way
     * will turn this item into that type.
     */
    itype_id unseals_into = itype_id::NULL_ID();
};
