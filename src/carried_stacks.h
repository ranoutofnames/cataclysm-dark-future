#pragma once
#ifndef CATA_SRC_CARRIED_STACKS_H
#define CATA_SRC_CARRIED_STACKS_H

#include <vector>

class Character;
class item;

/** One group of carried items that stack with each other, as the inventory UI shows them. */
using carried_stack = std::vector<item *>;

/**
 * The carried items (top level of the carry pockets) grouped by stacks_with, in the order they
 * were first met. Replaces the position based inv_const_slice() for UI and NPC code.
 */
std::vector<carried_stack> carried_item_stacks( const Character &who );

/** The group of carried items that stack with `it`, or just `it` if it is not carried. */
carried_stack carried_stack_of( const Character &who, item *it );

#endif // CATA_SRC_CARRIED_STACKS_H
