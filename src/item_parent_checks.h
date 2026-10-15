#pragma once

#include "ret_val.h"

class item;

/**
 * DDA item_location::parents_can_contain_recursive (item_location.cpp:1224) for the container
 * @p owner: whether @p it, put into @p owner, still fits every container above it.
 */
ret_val<void> parents_can_contain_recursive( const item &owner, const item &it );
/**
 * DDA item_location::max_charges_by_parent_recursive (item_location.cpp:1273) for the container
 * @p owner: how many charges of @p it all the containers above @p owner accept.
 */
ret_val<int> max_charges_by_parent_recursive( const item &owner, const item &it );
