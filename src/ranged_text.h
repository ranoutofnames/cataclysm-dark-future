#pragma once

#include <string>
#include <utility>

#include "color.h"

class gun_mode;
class item;
struct itype;
struct multimag_display_pocket;

namespace ranged_text
{
// DDA ranged.cpp:4265-4266: the "[%c] to reload/switch ammo." line is shown unless the gun is
// RELOAD_AND_SHOOT and loaded. // CDF: 8-6a, the caller adds the Fire/TurretManual gate.
bool switch_ammo_hint_shown( bool gun_is_ras, int gun_ammo_remaining );
// DDA ranged.cpp:4388-4396: the player's "Ammo:" row, colourised, ready for print_colored_text.
// `is_favorite` is DDA's `relevant->is_ammo_container() && relevant->first_ammo().is_favorite`,
// computed by the caller (ranged.cpp). CDF: 8-6a, `ammo` is the itype target_ui keeps.
std::string aim_ammo_row( const gun_mode &m, bool is_favorite, const itype &ammo );
// DDA ranged.cpp:4362-4387: one aim-panel row per ammo pocket of a manual multimag turret. Red when
// the pocket holds less than one shot needs, `base` otherwise; "(vehicle %d, need %d)" when the supply
// is vehicle-bound, "(%d, need %d)" when not. CDF: 8-6b, the loop and the print stay in target_ui.
std::pair<std::string, nc_color> turret_row( const multimag_display_pocket &dp, nc_color base );
// DDA ranged.cpp:4270-4282: the line under the ammo hint of a loaded RELOAD_AND_SHOOT gun. It names what the
// key does next: with unloading off (`unload_set` false) "unload", with it on "keep ... loaded". Both
// are the caller's col_enabled, line order 3. CDF: 8-6c, `key` is the bound key as text.
std::string ras_unload_hint( bool unload_set, const std::string &key, const std::string &gun_name );
} // namespace ranged_text
