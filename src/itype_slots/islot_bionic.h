#pragma once

#include "itype.h"

struct islot_bionic {
    /**
     * Arbitrary difficulty scale, see bionics.cpp for its usage.
     */
    int difficulty = 0;
    /**
     * Id of the bionic, see bionics.cpp for its usage.
     */
    bionic_id id;
    /**
     * Whether this CBM is an upgrade of another.
     */
    bool is_upgrade = false;
    /**
    * Item with installation data that can be used to provide almost guaranteed successful install of corresponding bionic.
    */
    itype_id installation_data;
};
