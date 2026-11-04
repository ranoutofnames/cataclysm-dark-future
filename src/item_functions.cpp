#include "item_functions.h"

#include "character.h"
#include "debug.h"
#include "item.h"
#include "itype.h"
#include "map/map.h"
#include "units.h"

static flag_id flag_NO_UNLOAD( "NO_UNLOAD" );

static const itype_id itype_UPS( "UPS" );

namespace item_funcs
{

bool can_be_unloaded( const item &itm )
{
    if( ( itm.is_container() || !itm.get_container_pockets().empty() ||
          itm.type->can_use( "holster" ) ) &&
        !itm.contents.empty() &&
        itm.can_unload_liquid() ) {
        return true;
    }

    if( itm.has_flag( flag_NO_UNLOAD ) ) {
        return false;
    }

    if( itm.magazine_current() ) {
        return true;
    }

    for( const item *mod : itm.gunmods() ) {
        if( can_be_unloaded( *mod ) ) {
            return true;
        }
    }

    if( itm.ammo_types().empty() ) {
        return false;
    }

    return itm.ammo_remaining() > 0 || itm.casings_count() > 0;
}

auto is_invisible_installed_gunmod( const item &itm ) -> bool
{
    if( !itm.is_gunmod() ) {
        return false;
    }
    const item *parent = itm.parent_item();
    const bool installed = parent != nullptr && parent->is_gun();
    return installed && !itm.type->gunmod->is_visible_when_installed;
}

// TODO: Add consideration for BIONIC_GUNS when their fake_items get USES_BIONIC_POWER
int shots_remaining( const Character &who, const item &it )
{
    if( !it.is_gun() ) {
        return 0;
    }

    int ammo_drain = it.ammo_required();
    int energy_drain = it.get_gun_ups_drain();
    int power = who.charges_of( itype_UPS );

    if( ammo_drain > 0 && energy_drain > 0 ) {
        // Both UPS and ammo, lower is limiting.
        return std::min( it.ammo_remaining() / ammo_drain, power / energy_drain );
    } else if( energy_drain > 0 ) {
        //Only one of the two, it is limiting.
        return power / energy_drain;
    } else if( ammo_drain > 0 ) {
        return it.ammo_remaining() / ammo_drain;
    } else {
        // Effectively infinite ammo.
        return item::INFINITE_CHARGES;
    }
}

// CDF: shared helper, see item_functions.h
auto use_charges_via_tool_uses( item &it, int qty, const tripoint_bub_ms &pos,
                                Character *carrier ) -> int
{
    const int factor = it.type ? it.type->legacy_charges_per_use_factor : 1;
    if( factor < 1 || qty % factor != 0 ) {
        debugmsg( "use_charges: %s requested %d charges with legacy_charges_per_use_factor %d "
                  "(not a multiple); recipe / caller needs to be re-baselined to uses",
                  it.tname(), qty, factor );
        return 0;
    }
    const int wanted_uses = qty / factor;
    const int got_uses = it.consume_tool_uses( wanted_uses, get_map(), pos, carrier );
    return got_uses * factor;
}

} // namespace item_funcs
