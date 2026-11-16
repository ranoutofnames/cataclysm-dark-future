#include "clothing_link.h"

#include "carried_stacks.h"
#include "character.h"
#include "item.h"
#include "units_volume.h"

auto can_link_to_clothing( const Character &owner, const item &candidate,
                           const item &clothing ) -> bool
{
    auto remaining = clothing.get_storage();
    if( &candidate == &clothing || remaining <= 0_ml ) {
        return false;
    }
    const auto clothing_id = clothing.get_var( "DROP_WITH_CLOTHING_ID", 0 );
    if( clothing_id > 0 ) {
        for( const auto &stack : carried_item_stacks( owner ) ) {
            for( const auto *linked : stack ) {
                if( linked != &candidate &&
                    linked->get_var( "DROP_WITH_CLOTHING_TARGET", 0 ) == clothing_id ) {
                    remaining -= linked->volume();
                }
            }
        }
    }
    return candidate.volume() <= remaining;
}
