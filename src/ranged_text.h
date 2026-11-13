#pragma once

#include <string>

class gun_mode;
class item;
struct itype;

namespace ranged_text
{
// DDA ranged.cpp:4265-4266: the "[%c] to reload/switch ammo." line is shown unless the gun is
// RELOAD_AND_SHOOT and loaded. // CDF: 8-6a, the caller adds the Fire/TurretManual gate.
bool switch_ammo_hint_shown( bool gun_is_ras, int gun_ammo_remaining );
// DDA ranged.cpp:4388-4396: the player's "Ammo:" row, colourised, ready for print_colored_text.
// `is_favorite` is DDA's `relevant->is_ammo_container() && relevant->first_ammo().is_favorite`,
// computed by the caller (ranged.cpp). CDF: 8-6a, `ammo` is the itype target_ui keeps.
std::string aim_ammo_row( const gun_mode &m, bool is_favorite, const itype &ammo );
} // namespace ranged_text
