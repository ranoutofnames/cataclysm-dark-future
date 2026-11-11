// MAGPOCKET: item::magazine_default / magazine_compatible / magazine_integral follow the live pockets
// (DDA item_gun_tool_ammo.cpp:2253-2283), legacy `magazines` data keeps its answers.
// CDF-only tests; fixture: data/mods/TEST_DATA/battery_mod_p3.json (diving_flashlight_small_hipower).
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"

namespace
{

const itype_id flashlight_id( "diving_flashlight_small_hipower" );
const itype_id light_cell( "light_battery_cell" );
const itype_id light_minus_cell( "light_minus_battery_cell" );
const itype_id medium_cell( "medium_battery_cell" );
const itype_id heavy_cell( "heavy_battery_cell" );
const itype_id light_mod( "magazine_battery_light_mod" );
const itype_id medium_mod( "magazine_battery_medium_mod" );
const itype_id heavy_mod( "magazine_battery_heavy_mod" );
const itype_id storage_mod( "magazine_battery_mod" );

const ammotype ammo_battery( "battery" );

auto flashlight_with( const itype_id &mod ) -> detached_ptr<item>
{
    detached_ptr<item> tool = item::spawn( flashlight_id );
    REQUIRE( tool->has_pocket_type( pocket_type::MOD ) );
    tool->put_in( item::spawn( mod ), pocket_type::MOD );
    REQUIRE_FALSE( tool->toolmods().empty() );
    return tool;
}

auto can_take( const item &tool, const itype_id &cell ) -> bool
{
    detached_ptr<item> c = item::spawn( cell );
    return tool.can_reload_with( *c, true );
}

auto well_of( const item &it ) -> const item_pocket *
{
    for( const item_pocket &pocket : it.get_contents().get_pockets() ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            return &pocket;
        }
    }
    return nullptr;
}

// CDF: the id lists of all live MAGAZINE_WELL pockets, united (DDA item_contents::magazine_compatible,
// item_gun_tool_ammo.cpp:2280-2283). A well restricted only by flag contributes nothing.
auto live_well_ids( const item &it ) -> std::set<itype_id>
{
    std::set<itype_id> ids;
    for( const item_pocket &pocket : it.get_contents().get_pockets() ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            ids.insert( pocket.get_data().item_id_restriction.begin(),
                        pocket.get_data().item_id_restriction.end() );
        }
    }
    return ids;
}

// CDF: a well with a flag restriction comes from the item's own pocket_data (DDA data); the wells the loader builds
// from `magazines` (item_factory.cpp:1029) carry id lists only.
auto has_flag_restricted_well( const item &it ) -> bool
{
    for( const item_pocket &pocket : it.get_contents().get_pockets() ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) &&
            !pocket.get_data().get_flag_restrictions().empty() ) {
            return true;
        }
    }
    return false;
}

// CDF: guard G of MAGPOCKET: an installed mod with ammo_modifier or magazine_adaptor keeps the BN body.
// Some retool items (hk_ump45_retool_*, ar15_retool_300blk) spawn with such a mod built in; they are
// covered by T6, not by the legacy loops (reported through the skipped count).
auto has_guard_mod( const item &it ) -> bool
{
    const std::vector<const item *> mods = it.is_gun() ? it.gunmods() : it.toolmods();
    return std::any_of( mods.begin(), mods.end(), []( const item * m ) {
        return !m->type->mod->ammo_modifier.empty() || !m->type->mod->magazine_adaptor.empty();
    } );
}

} // namespace

// T1
TEST_CASE( "magazine_default_follows_the_live_well", "[p3][magazine][pocket]" )
{
    detached_ptr<item> tool = item::spawn( flashlight_id );
    // CDF: the fixture well has default_magazine light_battery_cell (battery_mod_p3.json)
    CHECK( tool->magazine_default( false ) == light_cell );

    detached_ptr<item> mod = item::spawn( medium_mod );
    item *mod_ptr = &*mod;
    tool->put_in( std::move( mod ), pocket_type::MOD );
    REQUIRE_FALSE( tool->toolmods().empty() );
    CHECK( tool->magazine_default( false ) == medium_cell );

    tool->remove_item( *mod_ptr );
    REQUIRE( tool->toolmods().empty() );
    CHECK( tool->magazine_default( false ) == light_cell );
}

// T2
TEST_CASE( "pocket_mod_replaces_the_well", "[p3][magazine][pocket]" )
{
    detached_ptr<item> tool = flashlight_with( medium_mod );
    const std::set<flag_id> expected = { flag_id( "BATTERY_MEDIUM" ) };
    CHECK( tool->get_contents().magazine_flag_restrictions() == expected );
    CHECK_FALSE( can_take( *tool, light_cell ) );
    CHECK( can_take( *tool, medium_cell ) );
    CHECK( tool->magazine_compatible().count( light_cell ) == 0 );
}

// T3
TEST_CASE( "magazine_integral_follows_pockets", "[p3][magazine][pocket]" )
{
    SECTION( "false" ) {
        CHECK_FALSE( item::spawn( medium_mod )->magazine_integral() );
        // CDF: suppressor is a gunmod with no MAGAZINE pocket (data/json/items/gunmod/muzzle.json)
        CHECK_FALSE( item::spawn( itype_id( "suppressor" ) )->magazine_integral() );
        // CDF: jeans = armor, apple = food, neither has a MAGAZINE pocket
        CHECK_FALSE( item::spawn( itype_id( "jeans" ) )->magazine_integral() );
        CHECK_FALSE( item::spawn( itype_id( "apple" ) )->magazine_integral() );
    }
    SECTION( "true" ) {
        // a magazine item (MAGAZINE pocket)
        CHECK( item::spawn( light_cell )->magazine_integral() );
        CHECK( item::spawn( itype_id( "glockmag" ) )->magazine_integral() );
        // CDF: colt_army has a legacy clip (gun branch, kept until the gun data conversion)
        detached_ptr<item> colt = item::spawn( itype_id( "colt_army" ) );
        REQUIRE( colt->type->gun->clip > 0 );
        CHECK( colt->magazine_integral() );
        // CDF: sewing_kit is the tool cdf_ammo_p3_test.cpp:22 uses; max_charges > 0 (tool branch)
        detached_ptr<item> kit = item::spawn( itype_id( "sewing_kit" ) );
        REQUIRE( kit->type->tool->max_charges > 0 );
        CHECK( kit->magazine_integral() );
        // CDF: no gunmod in data/json/items/gunmod has a MAGAZINE pocket, so the
        // "gunmod with a MAGAZINE pocket is integral" line has no data to run on; dropped.
    }
}

// T4
TEST_CASE( "legacy_defaults_unchanged", "[p3][magazine][pocket]" )
{
    static const flag_id flag_speedloader( "SPEEDLOADER" );
    int checked = 0;
    int skipped = 0;
    std::vector<std::string> bad;
    for( const itype *t : item_controller->all() ) {
        if( !( t->gun || t->tool ) || t->magazines.empty() ) {
            continue;
        }
        // CDF: the BN formula, computed from the itype (not by calling the function under test)
        const std::set<ammotype> &ats = t->gun ? t->gun->ammo : t->tool->ammo_id;
        itype_id expected = itype_id::NULL_ID();
        if( !ats.empty() ) {
            auto found = t->magazine_default.find( *ats.begin() );
            if( found != t->magazine_default.end() && !found->second->has_flag( flag_speedloader ) ) {
                expected = found->second;
            }
        }
        detached_ptr<item> it = item::spawn( t->get_id() );
        if( has_guard_mod( *it ) ) {
            ++skipped;
            continue;
        }
        ++checked;
        const itype_id got = it->magazine_default();
        if( got != expected ) {
            bad.push_back( t->get_id().str() + ": " + got.str() + " != " + expected.str() );
        }
    }
    CHECK( checked > 100 );
    CAPTURE( bad );
    CHECK( bad.empty() );
    WARN( "T4 items with a guard-G mod built in, skipped: " << skipped );
}

// T5
TEST_CASE( "legacy_compatible_unchanged", "[p3][magazine][pocket]" )
{
    int checked = 0;
    int skipped = 0;
    int with_well = 0;
    std::vector<std::string> bad;
    std::vector<std::string> bad_well;
    std::vector<std::string> unfireable; // magazines naming an ammo the item cannot fire: reported
    for( const itype *t : item_controller->all() ) {
        if( !( t->gun || t->tool ) || t->magazines.empty() ) {
            continue;
        }
        const std::set<ammotype> &ats = t->gun ? t->gun->ammo : t->tool->ammo_id;
        std::set<itype_id> expected;
        for( const ammotype &a : ats ) {
            auto m = t->magazines.find( a );
            if( m != t->magazines.end() ) {
                expected.insert( m->second.begin(), m->second.end() );
            }
        }
        for( const auto &kv : t->magazines ) {
            if( ats.count( kv.first ) == 0 ) {
                unfireable.push_back( t->get_id().str() + ":" + kv.first.str() );
            }
        }
        detached_ptr<item> it = item::spawn( t->get_id() );
        if( has_guard_mod( *it ) ) {
            ++skipped;
            continue;
        }
        if( has_flag_restricted_well( *it ) ) {
            // CDF: the loader builds a well with the id lists of `magazines` for every other gun and tool, so the
            // two answers agree there. A data-authored well restricted by flag (FILT1: rebreather, masks,
            // game_watch...) answers with its id lists (DDA item_gun_tool_ammo.cpp:2280-2283), not the legacy
            // table; compared with the well
            ++with_well;
            if( it->magazine_compatible() != live_well_ids( *it ) ) {
                bad_well.push_back( t->get_id().str() );
            }
            continue;
        }
        ++checked;
        if( it->magazine_compatible() != expected ) {
            bad.push_back( t->get_id().str() );
        }
    }
    // CDF: P3-BATT1 moved 72 tools off the legacy `magazines` key (157 -> 85 items with one here);
    // the rest of the population is unchanged and the per-item comparison below stays strict
    CHECK( checked > 80 );
    CAPTURE( bad );
    CHECK( bad.empty() );
    CAPTURE( bad_well );
    CHECK( bad_well.empty() );
    WARN( "T5 items with a flag-restricted MAGAZINE_WELL, compared with the well: " << with_well );
    // CDF: the unfireable count and list go to the REPORT
    WARN( "T5 items with a guard-G mod built in, skipped: " << skipped );
    WARN( "magazines naming an ammo the item cannot fire: " << unfireable.size() );
    for( const std::string &u : unfireable ) {
        INFO( "unfireable " << u );
    }
}

// T5b
TEST_CASE( "flag_restricted_wells_answer_from_the_well", "[p3][magazine][pocket]" )
{
    // CDF: FILT1 gives these items DDA flag-restricted wells (no id list): the live answer is the union of the
    // id lists (empty), the legacy `magazines` list is no longer what magazine_compatible() returns.
    struct expectation {
        const char *id;
        const char *flag;
        const char *default_mag;
    };
    const std::vector<expectation> cases = {
        { "rebreather", "REBREATHER_CART", "rebreather_cartridge_o2" },
        { "mask_gas", "GASFILTER_MED", "gasfilter_med" },
        { "game_watch", "BATTERY_ULTRA_LIGHT", "light_minus_battery_cell" },
    };
    for( const expectation &c : cases ) {
        CAPTURE( c.id );
        detached_ptr<item> it = item::spawn( itype_id( c.id ) );
        REQUIRE_FALSE( it->type->magazines.empty() );
        const item_pocket *well = well_of( *it );
        REQUIRE( well != nullptr );
        CHECK( well->get_data().get_flag_restrictions().count( flag_id( c.flag ) ) == 1 );
        CHECK( well->get_data().item_id_restriction.empty() );
        CHECK( it->magazine_compatible() == live_well_ids( *it ) );
        CHECK( it->magazine_compatible().empty() );
        CHECK( it->magazine_default() == itype_id( c.default_mag ) );
        // the legacy answer would have been the table, which this must differ from
        std::set<itype_id> legacy;
        for( const auto &kv : it->type->magazines ) {
            legacy.insert( kv.second.begin(), kv.second.end() );
        }
        REQUIRE_FALSE( legacy.empty() );
        CHECK( it->magazine_compatible() != legacy );
    }
}

// T6
TEST_CASE( "conversion_and_adaptor_mods_keep_the_old_answer", "[p3][magazine][pocket]" )
{
    SECTION( "gun with an ammo_modifier conversion" ) {
        // CDF: the pair cdf_item_pocket_test.cpp:674 uses
        detached_ptr<item> gun = item::spawn( itype_id( "m4a1" ) );
        detached_ptr<item> mod = item::spawn( itype_id( "retool_ar15_300blk" ) );
        REQUIRE_FALSE( mod->type->mod->ammo_modifier.empty() );
        REQUIRE_FALSE( mod->type->mod->magazine_adaptor.empty() );
        const std::map<ammotype, std::set<itype_id>> adaptor = mod->type->mod->magazine_adaptor;
        gun->put_in( std::move( mod ) );
        const item_pocket *well = well_of( *gun );
        REQUIRE( well != nullptr );
        const std::set<itype_id> well_ids( well->get_data().item_id_restriction.begin(),
                                           well->get_data().item_id_restriction.end() );
        CHECK( gun->magazine_compatible() == well_ids );
        // BN answer: the conversion's adaptor magazine for the first converted ammo type
        const std::set<ammotype> converted = gun->ammo_types( true );
        REQUIRE_FALSE( converted.empty() );
        auto found = adaptor.find( *converted.begin() );
        REQUIRE( found != adaptor.end() );
        CHECK( gun->magazine_default() == *found->second.begin() );
    }
    SECTION( "tool with the storage battery mod" ) {
        detached_ptr<item> tool = flashlight_with( storage_mod );
        const auto &adaptor = tool->toolmods().front()->type->mod->magazine_adaptor;
        auto found = adaptor.find( ammo_battery );
        REQUIRE( found != adaptor.end() );
        CHECK( tool->magazine_compatible() == found->second );
        // conversion = true (the default): the BN body answers with the first adaptor magazine
        const itype_id def = tool->magazine_default();
        CHECK( def == *found->second.begin() );
        CHECK( def != light_cell );
    }
}

// T7
TEST_CASE( "battery_mod_wells_take_any_length", "[p3][magazine][pocket]" )
{
    for( const itype_id &m : { light_mod, medium_mod, heavy_mod, storage_mod } ) {
        CAPTURE( m.str() );
        detached_ptr<item> tool = flashlight_with( m );
        const item_pocket *well = well_of( *tool );
        REQUIRE( well != nullptr );
        // CDF: "2 km" is the explicit length every well carries (pocket_data.cpp:96-113)
        CHECK( well->get_data().max_item_length >= 2_km );
    }
    detached_ptr<item> tool = flashlight_with( heavy_mod );
    detached_ptr<item> cell = item::spawn( heavy_cell );
    CHECK( tool->put_in( *cell, pocket_type::MAGAZINE_WELL ).success() );
}

// T8
TEST_CASE( "battery_mods_exclude_the_other_sizes", "[p3][magazine][pocket]" )
{
    struct row {
        itype_id mod;
        itype_id own;
        itype_id dflt; // CDF: the mod's default_magazine (TASK Data table); the light mod defaults to the ultra-light cell
        std::vector<itype_id> others;
    };
    const std::vector<row> rows = {
        { light_mod, light_cell, light_minus_cell, { medium_cell, heavy_cell } },
        { medium_mod, medium_cell, medium_cell, { light_cell, heavy_cell } },
        { heavy_mod, heavy_cell, heavy_cell, { light_cell, medium_cell } },
    };
    for( const row &r : rows ) {
        CAPTURE( r.mod.str() );
        detached_ptr<item> tool = flashlight_with( r.mod );
        CHECK( can_take( *tool, r.own ) );
        for( const itype_id &o : r.others ) {
            CAPTURE( o.str() );
            CHECK_FALSE( can_take( *tool, o ) );
        }
        CHECK( tool->magazine_default( false ) == r.dflt );
    }
    // CDF: the light mod also accepts the ultra-light cell
    detached_ptr<item> light_tool = flashlight_with( light_mod );
    CHECK( can_take( *light_tool, light_minus_cell ) );
}

// T9: documents; passes on unmodified source (the storage mod keeps its magazine_adaptor)
TEST_CASE( "storage_battery_mod_is_unchanged_in_effect", "[p3][magazine][pocket]" )
{
    detached_ptr<item> tool = flashlight_with( storage_mod );
    CHECK( can_take( *tool, itype_id( "battery_car" ) ) );
    CHECK( can_take( *tool, itype_id( "small_storage_battery" ) ) );
    CHECK_FALSE( can_take( *tool, light_cell ) );
}
