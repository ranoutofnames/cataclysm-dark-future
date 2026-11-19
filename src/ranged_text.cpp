#include "ranged_text.h"

#include <algorithm>
#include <string>

#include "color.h"
#include "gun_mode.h"
#include "item.h"
#include "itype.h"
#include "translations.h"
#include "string_formatter.h"
#include "vehicle/vehicle.h"
#include "itype_slots/islot_ammo.h"

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

std::pair<std::string, nc_color> turret_row( const multimag_display_pocket &dp, nc_color base )
{
    // DDA ranged.cpp:4362-4387. CDF: the ammo itype is read as `&*itype_id` (as target_ui does for `ammo`).
    const itype *ad = dp.ammo_itype.is_null() ? nullptr : &*dp.ammo_itype;
    const std::string ammo_name = ad ? ad->nname( std::max( dp.effective_qty, 1 ) ) : dp.pocket_id;
    const nc_color ammo_clr = ad ? ad->color : c_light_gray;
    const bool short_of_one_use = dp.effective_qty < dp.per_use_qty;
    const nc_color row_clr = short_of_one_use ? c_red : base;
    std::string ammo_str;
    if( dp.vehicle_bound ) {
        ammo_str = string_format( _( "%s: %s (vehicle %d, need %d)" ), dp.pocket_id,
                                  colorize( ammo_name, ammo_clr ), dp.effective_qty, dp.per_use_qty );
    } else {
        ammo_str = string_format( _( "%s: %s (%d, need %d)" ), dp.pocket_id,
                                  colorize( ammo_name, ammo_clr ), dp.effective_qty, dp.per_use_qty );
    }
    return { ammo_str, row_clr };
}

std::string ras_unload_hint( bool unload_set, const std::string &key, const std::string &gun_name )
{
    // DDA ranged.cpp:4270-4282 (the two sentences are DDA's, key and gun name are %s).
    if( !unload_set ) {
        return string_format( _( "[%s] to unload the %s after quitting." ), key, gun_name );
    }
    return string_format( _( "[%s] to keep the %s loaded after quitting." ), key, gun_name );
}
} // namespace ranged_text
