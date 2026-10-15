#pragma once
#ifndef CATA_TESTS_PLAYER_HELPERS_H
#    define CATA_TESTS_PLAYER_HELPERS_H

#    include "coordinates.h"
#    include "type_id.h"

#    include <cstddef>
#    include <string>
#    include <vector>

class Character;
class npc;
class player;

auto get_remaining_charges(const std::string& tool_id) -> int;
auto player_has_item_of_type(const std::string&) -> bool;
void clear_character(player&, bool debug_storage = true);
// Transplanted from DDA tests/player_helpers.h: Character overload (DDA signature, skip_nutrition).
// Player-only state is cleared through the player overload; see reports/P1-transplant.md "J1 fix-ups".
void clear_character(Character&, bool skip_nutrition = false);
// Transplanted from DDA rng.cpp random_string, test-side only (no rng.h change).
std::string random_string(size_t length);
void clear_avatar();
// Transplanted from DDA Character::clear_worn (b4b62254ae2), as a test helper: DDA member form
// needs an engine change, see report.
void clear_worn(Character& dummy);
void process_activity(player& dummy);
// Transplanted from DDA tests/player_helpers.cpp: true if the character carries, wields or wears an
// item with get_var( var ) equal to val.
bool character_has_item_with_var_val(const Character& they, const std::string& var,
                                     const std::string& val);
// Assigns CDF's pickup activity for the given items (quantity 0 picks the whole stack) and, if
// autopickup is set, marks it as auto-resuming like autopickup does. Run it with process_activity.
// This is the entry point the DDA pickup tests (Character::pick_up) go through in CDF.
class item;
void assign_pickup_activity(player& they, const std::vector<item*>& items,
                            const std::vector<int>& quantities, bool autopickup = false);

auto spawn_npc(const tripoint_bub_ms&, const std::string& npc_class) -> npc&; // *NOPAD*
void give_and_activate_bionic(player& p, const bionic_id& bioid);

void arm_character(
    player& shooter, const std::string& gun_type, const std::vector<std::string>& mods = {},
    const std::string& ammo_type = "");

#endif // CATA_TESTS_PLAYER_HELPERS_H
