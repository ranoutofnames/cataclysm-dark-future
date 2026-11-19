#pragma once

#include "itype.h"

struct islot_brewable {
    /** What are the results of fermenting this item? */
    std::vector<itype_id> results;

    /** How long for this brew to ferment. */
    time_duration time = 0_turns;
};
