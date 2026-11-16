#pragma once
#ifndef CATA_SRC_CLOTHING_LINK_H
#define CATA_SRC_CLOTHING_LINK_H

class Character;
class item;

/// Reserve storage for inventory items linked to this particular clothing item.
auto can_link_to_clothing( const Character &owner, const item &candidate,
                           const item &clothing ) -> bool;

#endif // CATA_SRC_CLOTHING_LINK_H
