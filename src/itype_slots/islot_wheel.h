#pragma once

#include "itype.h"

struct islot_wheel {
    public:
        /** diameter of wheel (millimeters); integer JSON values are legacy inches */
        int diameter = 0;

        /** width of wheel (millimeters); integer JSON values are legacy inches */
        int width = 0;
};
