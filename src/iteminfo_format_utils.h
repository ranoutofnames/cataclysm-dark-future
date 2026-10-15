#pragma once

#include "item.h"

/**
 * Inserts a separation line into the given iteminfo vector if the last entry is not a separation.
 */
void insert_separation_line( std::vector<iteminfo> &info );

/** DDA item.h:191: a volume entry of an item info block (cf. item_info.cpp:4568). */
iteminfo vol_to_info( const std::string &type, const std::string &left,
                      const units::volume &vol, int decimal_places = 2, bool lower_is_better = true );
/** DDA item.h:193: a weight entry of an item info block (cf. item_info.cpp:4585). */
iteminfo weight_to_info( const std::string &type, const std::string &left,
                         const units::mass &weight, int decimal_places = 2, bool lower_is_better = true );
