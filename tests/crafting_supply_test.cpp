#include "../src/map/map.h"

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "craft_command.h"
#include "crafting.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "player_helpers.h"
#include "requirements.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "itype_slots/islot_tool.h"

#include <string>
#include <vector>

namespace
{

// CDF: item::find_type() does not exist here; string_id::obj() resolves the itype.
// The itype is shared between tests, so the old factor is restored by the guard.
class charge_factor_guard
{
    public:
        charge_factor_guard( const itype_id &type, int factor )
            : type_( type ), old_( type.obj().tool->charge_factor ) {
            const_cast<itype &>( type.obj() ).tool->charge_factor = factor;
        }
        ~charge_factor_guard() {
            const_cast<itype &>( type_.obj() ).tool->charge_factor = old_;
        }
        charge_factor_guard( const charge_factor_guard & ) = delete;
        charge_factor_guard &operator=( const charge_factor_guard & ) = delete;
    private:
        itype_id type_;
        int old_;
};

} // namespace

// DDA crafting.cpp:5120 multiplies the consumed quantity by itype::charge_factor();
// CDF dropped that multiplication.
TEST_CASE( "consume_tools honours itype charge_factor", "[crafting][supply]" )
{
    clear_all_state();
    clear_avatar();
    avatar &you = get_avatar();
    const itype_id tool_type( "airhorn" );

    you.i_add( item::spawn( tool_type, calendar::start_of_cataclysm, 50 ) );
    REQUIRE( you.charges_of( tool_type ) == 50 );

    SECTION( "charge_factor 1 spends one charge per required charge" ) {
        comp_selection<tool_comp> sel( usage_from::player, tool_comp( tool_type, 5 ) );
        you.consume_tools( sel, 1 );
        CHECK( get_remaining_charges( "airhorn" ) == 45 );
    }

    SECTION( "charge_factor multiplies the consumed quantity" ) {
        const charge_factor_guard guard( tool_type, 3 );
        comp_selection<tool_comp> sel( usage_from::player, tool_comp( tool_type, 5 ) );
        you.consume_tools( sel, 1 );
        CHECK( get_remaining_charges( "airhorn" ) == 50 - 5 * 3 );
    }

    SECTION( "charge_factor applies together with the batch multiplier" ) {
        const charge_factor_guard guard( tool_type, 2 );
        comp_selection<tool_comp> sel( usage_from::player, tool_comp( tool_type, 5 ) );
        you.consume_tools( sel, 2 );
        CHECK( get_remaining_charges( "airhorn" ) == 50 - 5 * 2 * 2 );
    }
}

// DDA crafting.cpp:4519 asks the tool pool for charge_factor * count * batch charges;
// CDF asked for count * batch only.
TEST_CASE( "tool selection cost honours itype charge_factor", "[crafting][supply]" )
{
    clear_all_state();
    const itype_id tool_type( "airhorn" );
    std::vector<tool_comp> tools{ tool_comp( tool_type, 50 ) };

    SECTION( "without a charge factor the listed charges are enough" ) {
        temp_crafting_inventory map_inv;
        map_inv.add_item_loc( *item::spawn_temporary( tool_type, calendar::start_of_cataclysm, 50 ) );
        const comp_selection<tool_comp> sel = crafting::select_tool_component(
                tools, 1, map_inv, nullptr, false, DEFAULT_HOTKEYS, cost_adjustment::none );
        CHECK( sel.use_from == usage_from::map );
    }

    SECTION( "a charge factor raises the charges the tool must hold" ) {
        const charge_factor_guard guard( tool_type, 2 );
        temp_crafting_inventory map_inv;
        map_inv.add_item_loc( *item::spawn_temporary( tool_type, calendar::start_of_cataclysm, 50 ) );
        const comp_selection<tool_comp> sel = crafting::select_tool_component(
                tools, 1, map_inv, nullptr, false, DEFAULT_HOTKEYS, cost_adjustment::none );
        CHECK( sel.use_from == usage_from::none );
    }
}
