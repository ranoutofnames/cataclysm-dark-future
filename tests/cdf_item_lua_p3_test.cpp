// CDF: P3-4-f-a; Lua tests for the P3 item ammo bindings (ammo_remaining( idx ),
// magazines()). DDA has no Lua test for these bindings (4-f lookup section 8), so
// these cases are CDF-only. The Lua scripts are inline; only the position
// coordinate of the ammo_consume case is built on the Lua side.
//
// Patterns: tests/ablative_lua_test.cpp (includes, make_lua_state()) and
// tests/catalua_test.cpp (inline safe_script with sol::script_pass_on_error,
// an item pointer handed to Lua).

#include "catalua_impl.h"
#include "catalua_sol.h"
#include "catch/catch.hpp"
#include "calendar.h"
#include "coordinates.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// CDF: copied from cdf_item_ammo_p3_test.cpp
// Index of the MAGAZINE_WELL pocket in the order well_pocket_at uses: every
// pocket returned by item::get_pockets( all ), wells included.
auto pocket_index( const item &it, bool want_well ) -> int {
    int idx = 0;
    for( const item_pocket *p : it.get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) == want_well ) {
            return idx;
        }
        ++idx;
    }
    return -1;
}

// CDF: copied from cdf_item_ammo_p3_test.cpp
auto loaded_m4a1( int rounds ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, rounds ) );
    gun->put_in( std::move( mag ) );
    return gun;
}

// TEST_DATA test_multimag_gun (data/mods/TEST_DATA/items.json): a glockmag with
// `glock_rounds` 9mm rounds and a stanag30 with `stanag_rounds` 556 rounds, one
// per MAGAZINE_WELL pocket.
auto loaded_multimag_gun( int glock_rounds, int stanag_rounds ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun" ) );
    detached_ptr<item> glock = item::spawn( itype_id( "glockmag" ) );
    glock->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, glock_rounds ) );
    detached_ptr<item> stanag = item::spawn( itype_id( "stanag30" ) );
    stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, stanag_rounds ) );
    REQUIRE( gun->put_in( std::move( glock ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun->put_in( std::move( stanag ), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}

} // namespace

// ammo_remaining( idx ) is the new 4-f-a overload: idx is 0-based and counts every
// pocket in get_pockets() order, and reads the magazine in that pocket (0 when the
// pocket is not a magazine well or idx is out of range).
TEST_CASE( "lua_item_ammo_remaining_index", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = loaded_multimag_gun( 15, 7 );

    // Walk every pocket in well_pocket_at order and record where each loaded well is.
    int glock_well = -1;
    int stanag_well = -1;
    int pocket_count = 0;
    for( const item_pocket *p : gun->get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            const item *mag = p->magazine_current();
            if( mag != nullptr && mag->typeId() == itype_id( "glockmag" ) ) {
                glock_well = pocket_count;
            } else if( mag != nullptr && mag->typeId() == itype_id( "stanag30" ) ) {
                stanag_well = pocket_count;
            }
        }
        ++pocket_count;
    }
    REQUIRE( glock_well >= 0 );
    REQUIRE( stanag_well >= 0 );
    // The fixture has two MAGAZINE_WELL pockets and nothing else, so index 2 is out of range.
    REQUIRE( pocket_count == 2 );

    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    lua.globals()["glock_well"] = glock_well;
    lua.globals()["stanag_well"] = stanag_well;
    const auto res = lua.safe_script( R"(
        return {
            glock = gun:ammo_remaining(glock_well),
            stanag = gun:ammo_remaining(stanag_well),
            out_of_range = gun:ammo_remaining(2),
            negative = gun:ammo_remaining(-1)
        }
    )", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    const auto out = res.get<sol::table>();
    CHECK( out.get<int>( "glock" ) == 15 );
    CHECK( out.get<int>( "stanag" ) == 7 );
    CHECK( out.get<int>( "out_of_range" ) == 0 );
    CHECK( out.get<int>( "negative" ) == 0 );

    // The index counts every pocket, not only the wells: the m4a1 well is one pocket
    // and its gunmod (MOD) pocket is another, so the MOD pocket's index reads 0.
    detached_ptr<item> m4 = loaded_m4a1( 30 );
    const int m4_well = pocket_index( *m4, true );
    const int m4_other = pocket_index( *m4, false );
    REQUIRE( m4_well >= 0 );
    REQUIRE( m4_other >= 0 );
    lua.globals()["m4"] = &*m4;
    lua.globals()["m4_well"] = m4_well;
    lua.globals()["m4_other"] = m4_other;
    const auto res2 = lua.safe_script( R"(
        return {
            well = m4:ammo_remaining(m4_well),
            other = m4:ammo_remaining(m4_other)
        }
    )", sol::script_pass_on_error );
    REQUIRE( res2.valid() );
    const auto out2 = res2.get<sol::table>();
    CHECK( out2.get<int>( "well" ) == 30 );
    CHECK( out2.get<int>( "other" ) == 0 );
}

// Regression guard for the no-argument ammo_remaining(): it is unchanged by 4-f-a.
// Green before and after the change.
TEST_CASE( "lua_item_ammo_remaining_total_unchanged", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = loaded_m4a1( 30 );

    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    const auto res = lua.safe_script( R"(
        return { total = gun:ammo_remaining() }
    )", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    CHECK( res.get<sol::table>().get<int>( "total" ) == 30 );
}

// The new magazines() binding lists the loaded magazines as item handles.
TEST_CASE( "lua_item_magazines_binding", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = loaded_multimag_gun( 15, 7 );
    detached_ptr<item> unloaded = item::spawn( itype_id( "test_multimag_gun" ) );

    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    lua.globals()["empty_gun"] = &*unloaded;
    const auto res = lua.safe_script( R"(
        local list = gun:magazines()
        local types = {}
        for i, m in ipairs(list) do
            types[i] = m:get_type():str()
        end
        return {
            count = #list,
            first = types[1],
            second = types[2],
            empty_count = #empty_gun:magazines()
        }
    )", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    const auto out = res.get<sol::table>();
    CHECK( out.get<int>( "count" ) == 2 );
    CHECK( out.get<std::string>( "first" ) == "glockmag" );
    CHECK( out.get<std::string>( "second" ) == "stanag30" );
    CHECK( out.get<int>( "empty_count" ) == 0 );
}

// The 2-argument ammo_consume( qty, pos ) binding is unchanged (ADV-13 2g): the
// carrier stays hidden, and the position is built on the Lua side. Green before
// and after the change.
TEST_CASE( "lua_item_ammo_consume_two_arg_unchanged", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = loaded_m4a1( 30 );

    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    const auto res = lua.safe_script( R"(
        local pos = TripointBubMs.new(0, 0, 0)
        local consumed = gun:ammo_consume(3, pos)
        return { consumed = consumed, left = gun:ammo_remaining() }
    )", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    const auto out = res.get<sol::table>();
    CHECK( out.get<int>( "consumed" ) == 3 );
    CHECK( out.get<int>( "left" ) == 27 );
}

namespace {

// CDF: P3-4-f-b; test_multimag_gun_consume (wells "ammo" and "power") with only the "ammo" well loaded (glockmag, 15 9mm).
auto ammo_well_loaded_gun() -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_consume" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
    mag->ammo_set( itype_id( "9mm" ), 15 );
    REQUIRE( gun->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    return gun;
}

} // namespace

// pocket_by_id( id ) returns the pocket whose pocket_data id matches, nil for "" and
// unknown ids.
TEST_CASE( "lua_item_pocket_by_id_resolves_the_named_pocket", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = ammo_well_loaded_gun();

    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    const auto res = lua.safe_script( R"(
        return {
            a_valid = gun:pocket_by_id("ammo"):is_valid(),
            a_items = #gun:pocket_by_id("ammo"):items(),
            p_items = #gun:pocket_by_id("power"):items(),
            first = gun:pocket_by_id("ammo"):items()[1]:get_type():str(),
            empty_nil = gun:pocket_by_id("") == nil,
            unknown_nil = gun:pocket_by_id("nope") == nil
        }
    )", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    const auto out = res.get<sol::table>();
    // CDF: re-derived from TEST_DATA items.json:1608
    CHECK( out.get<bool>( "a_valid" ) == true );
    CHECK( out.get<int>( "a_items" ) == 1 );
    CHECK( out.get<int>( "p_items" ) == 0 );
    CHECK( out.get<std::string>( "first" ) == "glockmag" );
    CHECK( out.get<bool>( "empty_nil" ) == true );
    CHECK( out.get<bool>( "unknown_nil" ) == true );
}

// The handle pocket_by_id returns is the same pocket get_pockets() lists.
TEST_CASE( "lua_item_pocket_by_id_same_pocket_as_get_pockets", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = ammo_well_loaded_gun();
    // The first well is get_pockets()[1] in Lua.
    REQUIRE( pocket_index( *gun, true ) == 0 );

    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    const auto res = lua.safe_script( R"(
        return {
            list_first = #gun:get_pockets()[1]:items(),
            list_second = #gun:get_pockets()[2]:items(),
            by_ammo = #gun:pocket_by_id("ammo"):items(),
            by_power = #gun:pocket_by_id("power"):items()
        }
    )", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    const auto out = res.get<sol::table>();
    CHECK( out.get<int>( "list_first" ) == 1 );
    CHECK( out.get<int>( "list_second" ) == 0 );
    CHECK( out.get<int>( "by_ammo" ) == 1 );
    CHECK( out.get<int>( "by_power" ) == 0 );
}

// A handle from pocket_by_id goes stale like a get_pockets() handle when its item dies.
TEST_CASE( "lua_item_pocket_by_id_handle_goes_stale", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = ammo_well_loaded_gun();

    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    const auto before = lua.safe_script( R"(
        h = gun:pocket_by_id("ammo")
        return h:is_valid()
    )", sol::script_pass_on_error );
    REQUIRE( before.valid() );
    CHECK( before.get<bool>() == true );

    gun = detached_ptr<item>();

    const auto after = lua.safe_script( "return h:is_valid()", sol::script_pass_on_error );
    REQUIRE( after.valid() );
    CHECK( after.get<bool>() == false );
}

// ebook_lua reads the tablet battery through ammo_remaining(), not item.charges.
TEST_CASE( "ebook_lua_battery_arithmetic_uses_ammo_remaining", "[p3][lua]" ) {
    clear_all_state();
    // CDF: literals from electronics.json:440-443 (initial_charges 160, max_charges 200)
    detached_ptr<item> tablet = item::spawn( itype_id( "eink_tablet_pc" ) );

    auto lua = make_lua_state();
    lua.globals()["device"] = &*tablet;
    const auto res = lua.safe_script( R"(
        local out = {}
        out.remaining = device:ammo_remaining()
        out.capacity = device:ammo_capacity(false)
        out.pct = math.floor(device:ammo_remaining() / device:ammo_capacity(false) * 100 + 0.5)
        out.limit = math.floor(device:ammo_remaining() / 10)
        device:ammo_set(device:ammo_current(), device:ammo_remaining() + 20)
        out.after = device:ammo_remaining()
        return out
    )", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    const auto out = res.get<sol::table>();
    CHECK( out.get<int>( "remaining" ) == 160 );
    CHECK( out.get<int>( "capacity" ) == 200 );
    CHECK( out.get<int>( "pct" ) == 80 );
    CHECK( out.get<int>( "limit" ) == 16 );
    CHECK( out.get<int>( "after" ) == 180 );
}

// main.lua must not read the battery from charges; book charges stay.
TEST_CASE( "ebook_lua_main_lua_has_no_battery_charges", "[p3][lua]" ) {
    std::ifstream f( "data/mods/ebook_lua/main.lua" );
    REQUIRE( f.is_open() );
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    CHECK( text.find( "device.charges" ) == std::string::npos );
    CHECK( text.find( "item.charges" ) == std::string::npos );
    CHECK( text.find( "device:ammo_remaining()" ) != std::string::npos );
    CHECK( text.find( "book_in_real.charges" ) != std::string::npos );

    auto lua = make_lua_state();
    sol::load_result lr = lua.load( text );
    REQUIRE( lr.valid() );
}

// CDF: P3-9-2; islot_magazine.mag_jam_mult is a read-only binding of the C++ member
// (default 1.0). slot_magazine() is nil for an itype without a magazine slot.
TEST_CASE( "lua_islot_magazine_mag_jam_mult_default", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
    auto lua = make_lua_state();
    lua.globals()["mag"] = &*mag;
    lua.globals()["gun"] = &*gun;
    const auto res = lua.safe_script( "return mag:get_type():obj():slot_magazine().mag_jam_mult",
                                      sol::script_pass_on_error );
    REQUIRE( res.valid() );
    CHECK( res.get<double>() == Approx( 1.0 ) );
    const auto nil_res = lua.safe_script( "return gun:get_type():obj():slot_magazine() == nil",
                                          sol::script_pass_on_error );
    REQUIRE( nil_res.valid() );
    CHECK( nil_res.get<bool>() );
}

TEST_CASE( "lua_islot_magazine_mag_jam_mult_reads_data", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> mag = item::spawn( itype_id( "test_mag_jam_mult_high" ) );
    auto lua = make_lua_state();
    lua.globals()["mag"] = &*mag;
    const auto res = lua.safe_script( "return mag:get_type():obj():slot_magazine().mag_jam_mult",
                                      sol::script_pass_on_error );
    REQUIRE( res.valid() );
    CHECK( res.get<double>() == Approx( 2.5 ) );
}

TEST_CASE( "lua_islot_magazine_mag_jam_mult_readonly", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    auto lua = make_lua_state();
    lua.globals()["mag"] = &*mag;
    // The member must be readable first, so a missing binding cannot pass as "read-only".
    const auto read = lua.safe_script( "return mag:get_type():obj():slot_magazine().mag_jam_mult",
                                       sol::script_pass_on_error );
    REQUIRE( read.valid() );
    const auto res = lua.safe_script( "mag:get_type():obj():slot_magazine().mag_jam_mult = 9",
                                      sol::script_pass_on_error );
    CHECK( !res.valid() );
    CHECK( itype_id( "stanag30" ).obj().magazine->mag_jam_mult == Approx( 1.0 ) );
}

// CDF: P3-9-2; item:uses_firing_requirements() mirrors the C++ method.
TEST_CASE( "lua_item_uses_firing_requirements_true", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = ammo_well_loaded_gun();
    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    const auto res = lua.safe_script( "return gun:uses_firing_requirements()", sol::script_pass_on_error );
    REQUIRE( res.valid() );
    CHECK( res.get<bool>() );
}

TEST_CASE( "lua_item_uses_firing_requirements_false", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> gun = loaded_m4a1( 30 );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    auto lua = make_lua_state();
    lua.globals()["gun"] = &*gun;
    lua.globals()["mag"] = &*mag;
    const auto res = lua.safe_script( "return { gun = gun:uses_firing_requirements(), mag = mag:uses_firing_requirements() }",
                                      sol::script_pass_on_error );
    REQUIRE( res.valid() );
    const auto out = res.get<sol::table>();
    CHECK( out.get<bool>( "gun" ) == false );
    CHECK( out.get<bool>( "mag" ) == false );
}

TEST_CASE( "lua_item_uses_firing_requirements_matches_cpp", "[p3][lua]" ) {
    clear_all_state();
    detached_ptr<item> fr = ammo_well_loaded_gun();
    detached_ptr<item> m4 = loaded_m4a1( 30 );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    auto lua = make_lua_state();
    lua.globals()["it"] = sol::lua_nil;
    for( item *it : { &*fr, &*m4, &*mag } ) {
        lua.globals()["it"] = it;
        const auto res = lua.safe_script( "return it:uses_firing_requirements()", sol::script_pass_on_error );
        REQUIRE( res.valid() );
        CHECK( res.get<bool>() == it->uses_firing_requirements() );
    }
}
