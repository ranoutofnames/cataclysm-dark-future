#pragma once

#include "itype.h"

struct islot_fuel {
    public:
        /** Energy of the fuel (kilojoules per charge) */
        float energy = 0.0f;
        struct fuel_explosion explosion_data;
        bool has_explode_data = false;
        ter_id pump_terrain = t_null;
};
