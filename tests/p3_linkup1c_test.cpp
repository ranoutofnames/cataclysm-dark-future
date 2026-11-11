#include "catch/catch.hpp"
#include "enums.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

// CDF: LINKUP1c tests. Spec: PL-33e TASK-draft-LINKUP1-v4.md, LINKUP1c test table L10-L12.
// The expected link_up values are the DDA twin's, pinned in the linkup1c_pin.json fixture.
// The five live hold ids whose DDA twin has no link_up are the pin's `deferred` set (REPORT,
// Blocked); the retired hold ids belong to MIG1 and keep their legacy wells (L11's analogue).

namespace
{

const std::string LINKUPC_PIN = "tests/data/dda_staged/linkup1c_pin.json";

struct linkupc_row {
    std::string id;
    int cable_length = 0;
    int charge_rate_mw = 0;
};

struct linkupc_pin {
    std::vector<linkupc_row> converted;
    std::vector<std::string> deferred;
};

auto read_pin() -> linkupc_pin
{
    std::ifstream file( LINKUPC_PIN, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    linkupc_pin pin;
    for( JsonObject jo : root.get_array( "converted" ) ) {
        jo.allow_omitted_members();
        pin.converted.push_back( { .id = jo.get_string( "id" ),
                                   .cable_length = jo.get_int( "cable_length" ),
                                   .charge_rate_mw = jo.get_int( "charge_rate_mw" ) } );
    }
    for( JsonObject jo : root.get_array( "deferred" ) ) {
        jo.allow_omitted_members();
        pin.deferred.push_back( jo.get_string( "id" ) );
    }
    return pin;
}

// A battery store is a MAGAZINE pocket or a MAGAZINE_WELL; a link_up tool may have neither.
auto has_battery_slot( const itype &type ) -> bool
{
    return std::ranges::any_of( type.pockets, []( const pocket_data & pocket ) {
        return pocket.type == pocket_type::MAGAZINE || pocket.type == pocket_type::MAGAZINE_WELL;
    } );
}

} // namespace

// L10: each converted tool has no battery pocket and a link_up action equal to its twin's
// (cable_length and charge_rate from the TSV hold rows). Mutation: leave `magazine_well` on
// coffeemaker -> the MAGAZINE_WELL is synthesized from its `magazines` and this fails.
TEST_CASE( "linkupc_converted_tools_draw_from_a_cable", "[battery][linkup1c]" ) {
    const linkupc_pin pin = read_pin();
    REQUIRE( pin.converted.size() == 20 );
    for( const linkupc_row &row : pin.converted ) {
        INFO( "id " << row.id );
        const itype_id id( row.id );
        REQUIRE( id.is_valid() );
        const itype &type = id.obj();
        CHECK( type.get_use( "link_up" ) != nullptr );
        CHECK_FALSE( has_battery_slot( type ) );

        item tool( id );
        REQUIRE( tool.can_link_up() );
        tool.update_link_traits();
        CHECK( tool.link().max_length == row.cable_length );
        CHECK( tool.link().charge_rate == row.charge_rate_mw );
    }
}

// L11: the conversion set and the deferred set are disjoint and cover the packet's live ids, and
// every deferred id is really uncabled. L11 is the hold list's analogue: BATT4 (its
// tests/data/battery_hold.json and the T2 exemption) never staged, so the hold set is pinned here.
// Mutation: move one converted id into `deferred` -> it has a link_up action.
TEST_CASE( "linkupc_hold_list_is_exactly_the_uncabled_twins", "[battery][linkup1c]" ) {
    const linkupc_pin pin = read_pin();
    const std::vector<std::string> expected_deferred = {
        "autoclave", "crt_mess_kit", "hydraulic_press", "mess_kit", "voltmeter"
    };
    CHECK( pin.deferred == expected_deferred );
    // the 25 live hold ids of battery-hold.json: 20 converted plus these 5
    CHECK( pin.converted.size() + pin.deferred.size() == 25 );
    for( const linkupc_row &row : pin.converted ) {
        INFO( "converted id " << row.id );
        CHECK( std::ranges::find( pin.deferred, row.id ) == pin.deferred.end() );
    }
    for( const std::string &id : pin.deferred ) {
        INFO( "deferred id " << id );
        if( !itype_id( id ).is_valid() ) {
            // CDF: crt_mess_kit is in the CRT_EXPANSION mod, which the test data does not load; the
            // set equality above and the disjointness above still pin it.
            continue;
        }
        CHECK( itype_id( id )->get_use( "link_up" ) == nullptr );
    }
}

// L12: an unlinked converted tool reports 0 charges, owns no cell store and refuses use (L4 on
// real data). Mutation: restore `magazines` on one converted tool -> its MAGAZINE_WELL is back.
TEST_CASE( "linkupc_unlinked_converted_tool_has_no_charges", "[battery][linkup1c]" ) {
    clear_all_state();
    for( const std::string id : { "coffeemaker", "welder", "elec_shears" } ) {
        INFO( "id " << id );
        item tool{ itype_id( id ) };
        CHECK_FALSE( has_battery_slot( *tool.type ) );
        CHECK( tool.magazine_current() == nullptr );
        CHECK( tool.ammo_remaining( nullptr ) == 0 );
        CHECK( tool.ammo_remaining_linked( get_map(), nullptr ) == 0 );
        CHECK_FALSE( tool.ammo_sufficient( nullptr, 1 ) );
    }
}
