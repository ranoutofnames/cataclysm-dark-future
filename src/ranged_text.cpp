#include "ranged_text.h"

#include <algorithm>
#include <string>

#include "color.h"
#include "gun_mode.h"
#include "item.h"
#include "itype.h"
#include "translations.h"
#include "string_formatter.h"

namespace ranged_text
{
bool switch_ammo_hint_shown( bool gun_is_ras, int gun_ammo_remaining )
{
    // DDA ranged.cpp:4265-4266
    return !gun_ammo_remaining || !gun_is_ras;
}

std::string aim_ammo_row( const gun_mode &m, bool is_favorite, const itype &ammo )
{
    // DDA ranged.cpp:4388-4396. CDF: the star is a literal, not translated (as in DDA).
    return string_format( m->ammo_remaining() ? _( "Ammo: %s%s (%d/%d)" ) : _( "Ammo: %s%s" ),
                          colorize( ammo.nname( std::max( m->ammo_remaining(), 1 ) ), ammo.color ),
                          colorize( is_favorite ? " *" : "", ammo.color ), m->ammo_remaining(),
                          m->ammo_capacity( ammo.ammo->type ) );
}
} // namespace ranged_text
