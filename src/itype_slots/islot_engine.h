#pragma once

#include "itype.h"

struct islot_engine {
        friend Item_factory;
        friend item;

    public:
        /** for combustion engines the displacement (cc) */
        int displacement = 0;
};
