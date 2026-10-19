#pragma once
#ifndef CATA_TESTS_PLAYER_HELPERS_H
#    define CATA_TESTS_PLAYER_HELPERS_H

#    include "coordinates.h"
#    include "detached_ptr.h"
#    include "type_id.h"

#    include <cstddef>
#    include <initializer_list>
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
// CDF: keeps player& (CDF signature); pass_time as DDA.
void process_activity(player& dummy, bool pass_time = false);
// same as process_activity, but gives up after max_turns turns or max_dispatches
// activity calls. for tests whose failure mode is an activity that never ends:
// the inner loop of process_activity spins forever when no moves are spent.
// returns true only when the activity ended inside both budgets
// CDF: player& because do_turn takes player&.
auto process_activity_bounded(player& dummy, const int max_turns, const int max_dispatches) -> bool;
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

// Clear player traits and give them a single trait by name
void set_single_trait(Character& dummy, const std::string& trait_name);

// CDF: detached_ptr return, item move ctor deleted.
auto tool_with_ammo(const itype_id& tool, const int qty) -> detached_ptr<item>;

void arm_character(
    player& shooter, const std::string& gun_type, const std::vector<std::string>& mods = {},
    const std::string& ammo_type = "");

// CDF: Character::clear_worn member absent; test helper used.
void equip_shooter(npc& shooter, const std::vector<itype_id>& apparel);
// CDF: ranged_balance_test.cpp still declares its own equip_shooter(npc&, const std::vector<std::string>&),
// so an empty braced call equip_shooter( shooter, {} ) is ambiguous between the two vector overloads.
// This initializer_list overload is an exact match for a braced list, so it wins and forwards to the vector one.
void equip_shooter(npc& shooter, std::initializer_list<itype_id> apparel);

#endif // CATA_TESTS_PLAYER_HELPERS_H
