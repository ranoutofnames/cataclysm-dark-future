// DDA source: tests/multimag_matrix_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASEs (8-7a): multimag_matrix_display_name_structural (DDA:328),
// multimag_matrix_load_then_display (DDA:350), multimag_matrix_tool_consume_drains_each_pocket
// (DDA:385), multimag_matrix_reload_pocket_index_loads_target (DDA:451).
// The harness (synth_registry, install_all_synth_types, load_pocket, ...) is shared with the
// 8-7b cases that are appended to this file.

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ammo.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "color.h"
#include "coordinates.h"
#include "debug.h"
#include "examine_item_menu.h"
#include "flag.h"
#include "gun_mode.h"
#include "item.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "output.h"
#include "player_helpers.h"
#include "ranged.h"
#include "pocket_type.h"
#include "point.h"
#include "translations.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"

// Cartesian product of 1-3 pockets over the ammotype palette, installed
// as runtime itypes. Same-ammo tuples are intentional: they exercise
// sibling-pocket-borrow regressions.

static const ammotype ammo_9mm( "9mm" );
static const ammotype ammo_battery( "battery" );
static const ammotype ammo_lamp_oil( "lamp_oil" );
static const ammotype ammo_oxygen( "oxygen" );
static const ammotype ammo_test_graphite( "test_graphite" );
static const ammotype ammo_water( "water" );

static const gun_mode_id gun_mode_DEFAULT( "DEFAULT" );
static const gun_mode_id gun_mode_MODE_X( "MODE_X" );

static const material_id material_steel( "steel" );

static const skill_id skill_pistol( "pistol" );

namespace
{

// Liquid/gas projectile pockets exist in shipping data (e.g. watercannon).
// ensure_synth marks those pockets watertight/airtight to match.
const std::vector<ammotype> &mm_mx_palette()
{
    static const std::vector<ammotype> r = {
        ammo_9mm, ammo_battery, ammo_water, ammo_oxygen, ammo_lamp_oil, ammo_test_graphite
    };
    return r;
}

// Bullets fit one per chamber; everything else gets tank-like capacity.
int mm_mx_pocket_cap_for( const ammotype &at )
{
    return at == ammo_9mm ? 1 : 50;
}

// Battery draws are larger per use to mimic the railgun pattern.
int mm_mx_pocket_qty_for( const ammotype &at )
{
    return at == ammo_battery ? 5 : 1;
}

// CDF: itype::id and itype::name are protected (only Item_factory and item are friends), so the
// test builds its runtime types through a derived struct that sets them; add_item_type copies
// the itype base.
struct mm_mx_itype : itype {
    void set_identity( const itype_id &new_id, const translation &new_name,
                       const translation &new_desc ) {
        id = new_id;
        name = new_name;
        description = new_desc;
    }
};

struct mm_mx_synth_spec {
    itype_id id;
    bool is_gun;
    std::vector<ammotype> pockets;
};

itype_id mm_mx_synth_id_for( bool is_gun, const std::vector<ammotype> &pockets )
{
    std::string s = is_gun ? "mm_mx_gun" : "mm_mx_tool";
    for( const ammotype &a : pockets ) {
        s += "__" + a.str();
    }
    return itype_id( s );
}

// item ctor's qty maps to charges only for count_by_charges itypes;
// set charges explicitly so both representations carry the same amount.
// CDF: items are detached_ptr<item> (item has no move constructor), so this returns one.
detached_ptr<item> mm_mx_make_synth_ammo( const itype_id &id, int qty )
{
    detached_ptr<item> r = item::spawn( id, calendar::turn, qty );
    if( r->count_by_charges() ) {
        r->charges = qty;
    }
    return r;
}

// Sum of live charges across a pocket's top items, excluding spent
// casings (those remain in the pocket after fire but should not count
// toward loaded ammo for matrix invariants).
int mm_mx_pocket_live_charges( const item_pocket *p )
{
    int n = 0;
    for( const item *e : p->all_items_top() ) {
        if( e->has_flag( flag_CASING ) ) {
            continue;
        }
        n += e->charges > 0 ? e->charges : 1;
    }
    return n;
}

// CDF: DDA item::type_is_defined( id ) is itype_id::is_valid() here (has_template; it also
// finds runtime types).
const itype *mm_mx_ensure_synth( const mm_mx_synth_spec &spec )
{
    if( spec.id.is_valid() ) {
        return &*spec.id;
    }
    // CDF: DDA item_controller->add_runtime( id, name, desc ) returns the stored const itype *.
    // CDF add_item_type copies a finished itype (and runs finalize_pre/post), so the type is
    // built locally first and fetched through the itype_id afterwards.
    mm_mx_itype t;
    t.set_identity( spec.id, no_translation( "synth multimag matrix item" ),
                    no_translation( "Runtime-built multimag item for matrix tests." ) );

    t.weight = 1000_gram;
    t.volume = 1500_ml;
    // finalize_pre is skipped for runtime types in DDA; populate the fields it
    // would have set so generic all-items iteration tests don't trip.
    // CDF: itype has no mat_portion_total / using_legacy_to_hit, and materials is a
    // vector<material_id>.
    t.longest_side = 200_mm;
    t.materials.push_back( material_steel );
    t.sym = "(";
    t.color = c_light_gray;

    if( spec.is_gun ) {
        t.gun = cata::make_value<islot_gun>();
        t.gun->skill_used = skill_pistol;
        for( const ammotype &a : spec.pockets ) {
            t.gun->ammo.insert( a );
        }
        // CDF: gun_modifier_data takes a plain std::string name, not a translation.
        t.gun->modes.emplace( gun_mode_DEFAULT,
                              gun_modifier_data( "default", 1, std::set<std::string> {} ) );
    } else {
        t.tool = cata::make_value<islot_tool>();
        // CDF: item::display_name / ammo_types() read tool->ammo_id (DDA derives the tool's ammo
        // types elsewhere); without it an empty one-pocket tool renders no "(...)" segment at all.
        for( const ammotype &a : spec.pockets ) {
            t.tool->ammo_id.insert( a );
        }
    }

    int idx = 0;
    for( const ammotype &a : spec.pockets ) {
        pocket_data pd( pocket_type::MAGAZINE );
        pd.rigid = true;
        pd.pocket_id = "p" + std::to_string( idx );
        pd.ammo_restriction[a] = mm_mx_pocket_cap_for( a );
        // Liquid into a non-watertight pocket spills, leaving stragglers
        // that surface later as remove_item debug noise. Gas needs
        // airtight for the same reason.
        if( a == ammo_water || a == ammo_lamp_oil ) {
            pd.watertight = true;
        }
        if( a == ammo_oxygen ) {
            pd.airtight = true;
        }
        t.pockets.push_back( pd );
        ++idx;
    }

    for( size_t i = 0; i < spec.pockets.size(); ++i ) {
        pocket_consumption_entry e;
        e.pocket = "p" + std::to_string( i );
        e.qty = mm_mx_pocket_qty_for( spec.pockets[i] );
        t.firing_requirements.per_mode[gun_mode_DEFAULT].push_back( e );
    }

    // CDF: explicit_pockets keeps Item_factory::synthesize_pockets from replacing the pockets.
    t.explicit_pockets = true;

    // MOD pocket so the mod-matrix cases can attach a synth mod by force-insert.
    t.pockets.emplace_back( pocket_type::MOD );

    item_controller->add_item_type( t );
    return &*spec.id;
}

// Runtime gun-mod / tool-mod for the mod-matrix cases. configure() sets the
// slot fields under test (gunmod->mode_modifier/firing_requirements/hide_modes,
// or mod->consumption_mods/capacity_mods), keyed by the synth pocket ids p0..pN.
// Kept for the 8-7b mod cases.
const itype *mm_mx_ensure_synth_mod( const itype_id &id,
                                     const std::function<void( itype & )> &configure )
{
    if( id.is_valid() ) {
        return &*id;
    }
    mm_mx_itype t;
    t.set_identity( id, no_translation( "synth multimag matrix mod" ),
                    no_translation( "Runtime-built mod for matrix tests." ) );
    t.weight = 100_gram;
    t.volume = 100_ml;
    t.longest_side = 50_mm;
    t.materials.push_back( material_steel );
    t.sym = ":";
    t.color = c_light_gray;
    configure( t );
    item_controller->add_item_type( t );
    return &*id;
}

const std::vector<mm_mx_synth_spec> &mm_mx_synth_registry()
{
    static const std::vector<mm_mx_synth_spec> r = []() {
        std::vector<mm_mx_synth_spec> out;
        const std::vector<ammotype> &pal = mm_mx_palette();
        for( bool is_gun : {
                 true, false
             } ) {
            for( const ammotype &a : pal ) {
                out.push_back( { mm_mx_synth_id_for( is_gun, { a } ), is_gun, { a } } );
            }
            for( const ammotype &a : pal ) {
                for( const ammotype &b : pal ) {
                    std::vector<ammotype> pockets = { a, b };
                    out.push_back( { mm_mx_synth_id_for( is_gun, pockets ), is_gun, pockets } );
                }
            }
            for( const ammotype &a : pal ) {
                for( const ammotype &b : pal ) {
                    for( const ammotype &c : pal ) {
                        std::vector<ammotype> pockets = { a, b, c };
                        out.push_back( { mm_mx_synth_id_for( is_gun, pockets ), is_gun, pockets } );
                    }
                }
            }
        }
        return out;
    }
    ();
    return r;
}

void mm_mx_install_all_synth_types()
{
    static bool installed = false;
    if( installed ) {
        return;
    }
    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        mm_mx_ensure_synth( spec );
    }
    installed = true;
}

// Bypasses item::reload and item::ammo_set on purpose: those are
// host-level APIs that can route to a different pocket or reset others
// on multimag hosts. The matrix wants each pocket exercised in
// isolation against its own ammo_restriction.
// CDF: item_location host is an item &.
bool mm_mx_load_pocket( item &host, int pocket_idx,
                        const ammotype &at, int qty, std::string &api_used )
{
    const itype_id ammo_id = at->default_ammotype();
    if( ammo_id.is_null() || !ammo_id.is_valid() ) {
        api_used = "no_default_ammo";
        return false;
    }
    std::vector<item_pocket *> pockets = host.get_pockets(
    []( const item_pocket & ) {
        return true;
    } );
    if( pocket_idx >= static_cast<int>( pockets.size() ) ) {
        api_used = "pocket_index_out_of_range";
        return false;
    }
    // CDF: detached_ptr insert; the item is only consumed on success.
    detached_ptr<item> synth = mm_mx_make_synth_ammo( ammo_id, qty );
    ret_val<item *> ins = pockets[pocket_idx]->insert_item( std::move( synth ) );
    if( ins.success() ) {
        api_used = "pocket_insert";
        return true;
    }
    api_used = "pocket_insert_rejected:" + std::string( ins.str() );
    return false;
}

int mm_mx_pocket_count_for( const item &it )
{
    int n = 0;
    for( const item_pocket *p : it.get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE ) || p->is_type( pocket_type::MAGAZINE_WELL ) ) {
            ++n;
        }
    }
    return n;
}

int mm_mx_segments_in_display( std::string_view display )
{
    const std::size_t lparen = display.rfind( '(' );
    const std::size_t rparen = display.rfind( ')' );
    if( lparen == std::string_view::npos || rparen == std::string_view::npos ||
        rparen <= lparen ) {
        return 0;
    }
    int commas = 0;
    for( std::size_t i = lparen + 1; i < rparen; ++i ) {
        if( display[i] == ',' ) {
            ++commas;
        }
    }
    return commas + 1;
}

} // namespace

TEST_CASE( "multimag_matrix_display_name_structural", "[p3][multimag][matrix][display]" )
{
    mm_mx_install_all_synth_types();
    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        CAPTURE( spec.id.str() );
        // CDF: item is built through item::spawn (detached_ptr).
        detached_ptr<item> it = item::spawn( spec.id );
        const int pc = mm_mx_pocket_count_for( *it );
        REQUIRE( pc == static_cast<int>( spec.pockets.size() ) );

        const std::string name_empty = remove_color_tags( it->display_name() );
        INFO( "empty: " << name_empty );
        // 1-pocket multimag tnames may render a single bare "(X/Y)" or
        // skip the parens entirely depending on display_name code paths,
        // so segments_in_display >= 1 is the conservative assertion.
        if( pc == 1 ) {
            CHECK( mm_mx_segments_in_display( name_empty ) >= 1 );
        } else {
            CHECK( mm_mx_segments_in_display( name_empty ) == pc );
        }
        CHECK( name_empty.find( "invalid" ) == std::string::npos );
    }
}

TEST_CASE( "multimag_matrix_load_then_display", "[p3][multimag][matrix][load][display]" )
{
    mm_mx_install_all_synth_types();
    clear_avatar();
    Character &chr = get_player_character();

    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        CAPTURE( spec.id.str() );
        // CDF: i_add takes a detached_ptr and returns item &.
        item &host = chr.i_add( item::spawn( spec.id ) );
        REQUIRE( !host.is_null() );

        for( size_t i = 0; i < spec.pockets.size(); ++i ) {
            CAPTURE( i );
            CAPTURE( spec.pockets[i].str() );
            std::string api_used;
            const bool loaded = mm_mx_load_pocket( host, static_cast<int>( i ),
                                                   spec.pockets[i], 1, api_used );
            CAPTURE( api_used );
            CHECK( loaded );
        }

        const std::string name_loaded = remove_color_tags( host.display_name() );
        INFO( "loaded: " << name_loaded );
        const int pc = mm_mx_pocket_count_for( host );
        if( pc == 1 ) {
            CHECK( mm_mx_segments_in_display( name_loaded ) >= 1 );
        } else {
            CHECK( mm_mx_segments_in_display( name_loaded ) == pc );
        }
        CHECK( name_loaded.find( "invalid" ) == std::string::npos );
        // "0/0" segment indicates the per-pocket renderer fell back to a
        // zero-capacity branch; never legitimate for a loaded host.
        CHECK( name_loaded.find( "0/0" ) == std::string::npos );
    }
}

TEST_CASE( "multimag_matrix_tool_consume_drains_each_pocket",
           "[p3][multimag][matrix][consume]" )
{
    mm_mx_install_all_synth_types();
    map &here = get_map();

    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        if( spec.is_gun ) {
            continue;
        }
        CAPTURE( spec.id.str() );
        // Fresh avatar per shape: accumulating synth tools loaded with
        // comestible ammo seeds active_item_cache stragglers that
        // surface later as remove_item debug noise.
        // TODO: active_item_cache holds raw pointers; pocket/item
        // destruction doesn't deregister, so stale refs linger.
        clear_avatar();
        Character &chr = get_player_character();
        // CDF: DDA chr.pos_bub( here ) is bub_pos().
        const tripoint_bub_ms pos = chr.bub_pos();
        item &host = chr.i_add( item::spawn( spec.id ) );
        REQUIRE( !host.is_null() );

        for( size_t i = 0; i < spec.pockets.size(); ++i ) {
            const int cap = mm_mx_pocket_cap_for( spec.pockets[i] );
            std::string api_used;
            const bool loaded = mm_mx_load_pocket( host, static_cast<int>( i ),
                                                   spec.pockets[i], cap, api_used );
            CAPTURE( i );
            CAPTURE( spec.pockets[i].str() );
            CAPTURE( api_used );
            REQUIRE( loaded );
        }

        std::vector<int> before;
        for( const item_pocket *p : host.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE ) ) {
                before.push_back( mm_mx_pocket_live_charges( p ) );
            }
        }
        REQUIRE( before.size() == spec.pockets.size() );

        const int got = host.consume_tool_uses( 1, here, pos, &chr );
        CHECK( got == 1 );

        std::vector<int> after;
        for( const item_pocket *p : host.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE ) ) {
                after.push_back( mm_mx_pocket_live_charges( p ) );
            }
        }
        for( size_t i = 0; i < before.size(); ++i ) {
            CAPTURE( i );
            CAPTURE( spec.pockets[i].str() );
            CAPTURE( before[i] );
            CAPTURE( after[i] );
            CHECK( after[i] < before[i] );
        }
    }
}

TEST_CASE( "multimag_matrix_reload_pocket_index_loads_target",
           "[p3][multimag][matrix][reload]" )
{
    mm_mx_install_all_synth_types();

    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        if( !spec.is_gun ) {
            continue;
        }
        CAPTURE( spec.id.str() );
        for( size_t target_idx = 0; target_idx < spec.pockets.size(); ++target_idx ) {
            CAPTURE( target_idx );
            CAPTURE( spec.pockets[target_idx].str() );

            // item::reload(pocket_index) is the bullet/charge-style path.
            // Liquid (water, lamp_oil) and gas (oxygen) pockets load via
            // item::fill_with; their default ammotype itype is a
            // comestible whose is_ammo() is false, so dispatching them
            // through reload refuses and leaks the source comestible.
            const itype_id ammo_id = spec.pockets[target_idx]->default_ammotype();
            if( ammo_id.is_null() || !ammo_id.is_valid() ) {
                continue;
            }
            // CDF: item( ammo_id ) by value becomes a spawned item. BN data gives the liquid and
            // gas comestibles (water, lamp_oil, oxygen) an ammo slot, so is_ammo() is true for them
            // here and the DDA filter alone no longer skips them (their loose charges cannot even
            // be i_add'ed). The filter keeps its DDA intent by also skipping liquid/gas phases.
            detached_ptr<item> ammo_probe = item::spawn( ammo_id );
            if( !ammo_probe->is_ammo() || ammo_probe->made_of( phase_id::LIQUID ) ||
                ammo_probe->made_of( phase_id::GAS ) ) {
                continue;
            }

            // Fresh avatar per iteration so prior stacked ammo doesn't
            // invalidate the per-call item handle.
            clear_avatar();
            Character &chr = get_player_character();
            item &host = chr.i_add( item::spawn( spec.id ) );
            REQUIRE( !host.is_null() );

            const int qty = mm_mx_pocket_cap_for( spec.pockets[target_idx] );
            item &ammo_loc = chr.i_add( mm_mx_make_synth_ammo( ammo_id, qty ) );
            REQUIRE( !ammo_loc.is_null() );

            const bool reloaded = host.reload( chr, ammo_loc, qty, target_idx );
            CAPTURE( reloaded );

            std::vector<item_pocket *> pockets = host.get_pockets(
            []( const item_pocket & ) {
                return true;
            } );
            REQUIRE( target_idx < pockets.size() );
            const int loaded_target = mm_mx_pocket_live_charges( pockets[target_idx] );

            for( size_t i = 0; i < pockets.size(); ++i ) {
                if( i == target_idx ) {
                    continue;
                }
                const int sibling = mm_mx_pocket_live_charges( pockets[i] );
                CAPTURE( i );
                CAPTURE( sibling );
                CHECK( sibling == 0 );
            }

            if( reloaded ) {
                CHECK( loaded_target >= 1 );
            } else {
                // Reload refused must leave the pocket untouched - no
                // partial state.
                CHECK( loaded_target == 0 );
            }
        }
    }
}

// ---- 8-7b: DDA tests/multimag_matrix_test.cpp:520-773 ----

TEST_CASE( "multimag_matrix_unload_action_rated_good_when_loaded",
           "[p3][multimag][matrix][unload]" )
{
    mm_mx_install_all_synth_types();
    clear_avatar();
    // CDF: rate_action_unload is a free function taking the avatar and an item.
    avatar &chr = get_avatar();

    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        CAPTURE( spec.id.str() );
        // CDF: item is built through item::spawn (detached_ptr).
        detached_ptr<item> host = item::spawn( spec.id );
        std::vector<item_pocket *> pockets = host->get_pockets(
        []( const item_pocket & ) {
            return true;
        } );

        const examine_item_menu::hint_rating empty_rating = examine_item_menu::rate_action_unload( chr, *host );
        CAPTURE( static_cast<int>( empty_rating ) );
        CHECK( empty_rating != examine_item_menu::hint_rating::good );

        for( size_t i = 0; i < spec.pockets.size(); ++i ) {
            ret_val<item *> ins = pockets[i]->insert_item(
                                      mm_mx_make_synth_ammo( spec.pockets[i]->default_ammotype(), 1 ) );
            REQUIRE( ins.success() );
        }
        const examine_item_menu::hint_rating loaded_rating = examine_item_menu::rate_action_unload( chr, *host );
        CAPTURE( static_cast<int>( loaded_rating ) );
        CHECK( loaded_rating == examine_item_menu::hint_rating::good );
    }
}

TEST_CASE( "multimag_matrix_fire_gun_no_invalid_or_shortage",
           "[p3][multimag][matrix][fire]" )
{
    mm_mx_install_all_synth_types();

    // Fire is the most expensive setup (clear_map + clear_avatar +
    // wield + capture_debugmsg per case), so sample a representative
    // subset instead of the full Cartesian: every 1-pocket gun shape,
    // plus the two-pocket bullet+power "railgun" pattern, plus one
    // 3-pocket exotic. The shape-coverage axes (display, load, reload,
    // unload) stay exhaustive in the other TEST_CASEs.
    std::set<itype_id> fire_shapes;
    for( const ammotype &a : mm_mx_palette() ) {
        fire_shapes.insert( mm_mx_synth_id_for( /*is_gun=*/true, { a } ) );
    }
    for( const ammotype &a : mm_mx_palette() ) {
        fire_shapes.insert( mm_mx_synth_id_for( true, { a, ammo_battery } ) );
    }
    fire_shapes.insert( mm_mx_synth_id_for( true, { ammo_9mm, ammo_battery, ammo_water } ) );

    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        if( !spec.is_gun || fire_shapes.find( spec.id ) == fire_shapes.end() ) {
            continue;
        }
        CAPTURE( spec.id.str() );

        // Fresh avatar + map per shape so prior fire effects (catching
        // fire, recoil-induced unwield) don't carry over and drop the
        // synth gun mid-shot in the next iteration.
        clear_map();
        clear_avatar();
        avatar &dummy = get_avatar();
        dummy.set_body();
        clear_worn( dummy );  // CDF: free function in player_helpers.h
        // CDF: remove_weapon is unwield; setpos takes the tripoint only.
        dummy.unwield();
        dummy.setpos( tripoint_bub_ms( 60, 60, 0 ) );  // CDF: DDA (0,0,0); the fixed in-map tile other CDF fire tests use

        detached_ptr<item> gun = item::spawn( spec.id );
        std::vector<item_pocket *> pockets = gun->get_pockets(
        []( const item_pocket & p ) {
            return p.is_type( pocket_type::MAGAZINE ) || p.is_type( pocket_type::MAGAZINE_WELL );
        } );
        REQUIRE( pockets.size() == spec.pockets.size() );
        for( size_t i = 0; i < spec.pockets.size(); ++i ) {
            const int cap = mm_mx_pocket_cap_for( spec.pockets[i] );
            ret_val<item *> ins = pockets[i]->insert_item(
                                      mm_mx_make_synth_ammo( spec.pockets[i]->default_ammotype(), cap ) );
            REQUIRE( ins.success() );
        }
        // CDF: wield takes the detached_ptr and hands it back when the wield is refused.
        REQUIRE( !dummy.wield( std::move( gun ) ) );
        REQUIRE( dummy.is_armed() );

        const tripoint_bub_ms target = dummy.bub_pos() + point( 5, 0 );
        const std::string dmsg = capture_debugmsg_during( [&]() {
            ranged::fire_gun( dummy, target, 1, dummy.primary_weapon(), nullptr );
        } );
        INFO( "debugmsg: " << dmsg );
        CHECK( dmsg.find( "invalid recipe id" ) == std::string::npos );
        CHECK( dmsg.find( "Unexpected shortage of ammo" ) == std::string::npos );
        CHECK( dmsg.find( "Unexpected shortage of energy" ) == std::string::npos );
    }
}

TEST_CASE( "multimag_matrix_tool_uses_remaining_local_after_load",
           "[p3][multimag][matrix][charges]" )
{
    mm_mx_install_all_synth_types();
    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        if( spec.is_gun ) {
            continue;
        }
        CAPTURE( spec.id.str() );
        detached_ptr<item> tool = item::spawn( spec.id );

        std::vector<item_pocket *> pockets = tool->get_pockets(
        []( const item_pocket & p ) {
            return p.is_type( pocket_type::MAGAZINE ) || p.is_type( pocket_type::MAGAZINE_WELL );
        } );
        REQUIRE( pockets.size() == spec.pockets.size() );

        // Canonical multimag math: floor(local / qty) per pocket, then
        // min across pockets. tool_uses_remaining_local() recomputes the
        // same invariant from the host's own state.
        int expected = INT_MAX;
        for( size_t i = 0; i < spec.pockets.size(); ++i ) {
            const int cap = mm_mx_pocket_cap_for( spec.pockets[i] );
            const int qty = mm_mx_pocket_qty_for( spec.pockets[i] );
            ret_val<item *> ins = pockets[i]->insert_item(
                                      mm_mx_make_synth_ammo( spec.pockets[i]->default_ammotype(), cap ) );
            REQUIRE( ins.success() );
            expected = std::min( expected, cap / qty );
        }
        if( expected == INT_MAX ) {
            expected = 0;
        }

        const int local = tool->tool_uses_remaining_local();
        CAPTURE( expected );
        CAPTURE( local );
        CHECK( local == expected );
    }
}

namespace
{
// Live MAGAZINE-pocket charge counts in pocket order, for before/after diffs.
std::vector<int> mm_mx_magazine_live_charges( const item &host )
{
    std::vector<int> out;
    for( const item_pocket *p : host.get_pockets( []( const item_pocket & q ) {
    return q.is_type( pocket_type::MAGAZINE );
} ) ) {
        out.push_back( mm_mx_pocket_live_charges( p ) );
    }
    return out;
}
} // namespace

TEST_CASE( "multimag_matrix_consumption_mods_override_each_pocket",
           "[p3][multimag][matrix][mod]" )
{
    mm_mx_install_all_synth_types();
    map &here = get_map();
    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        CAPTURE( spec.id.str() );
        clear_avatar();
        Character &chr = get_player_character();
        const tripoint_bub_ms pos = chr.bub_pos();
        item &host = chr.i_add( item::spawn( spec.id ) );
        REQUIRE( !host.is_null() );
        for( size_t i = 0; i < spec.pockets.size(); ++i ) {
            std::string api;
            REQUIRE( mm_mx_load_pocket( host, static_cast<int>( i ), spec.pockets[i],
                                        mm_mx_pocket_cap_for( spec.pockets[i] ), api ) );
        }

        // Override every pocket's per-use cost to 1 via set.
        const itype_id mod_id( "mm_mx_consume_" + std::to_string( spec.pockets.size() ) );
        mm_mx_ensure_synth_mod( mod_id, [&]( itype & t ) {
            t.mod = cata::make_value<islot_mod>();
            for( size_t i = 0; i < spec.pockets.size(); ++i ) {
                pocket_consumption_mod cm;
                cm.pocket = "p" + std::to_string( i );
                cm.set = 1;
                t.mod->consumption_mods.push_back( cm );
            }
        } );
        // CDF: no item::update_modified_pockets; the mod goes in through item::put_in( MOD ).
        REQUIRE( host.put_in( item::spawn( mod_id ), pocket_type::MOD ).success() );

        const std::vector<int> before = mm_mx_magazine_live_charges( host );
        const int got = spec.is_gun
                        ? host.consume_shots( gun_mode_DEFAULT, 1, here, pos, &chr )
                        : host.consume_tool_uses( 1, here, pos, &chr );
        CHECK( got == 1 );
        const std::vector<int> after = mm_mx_magazine_live_charges( host );
        REQUIRE( before.size() == after.size() );
        for( size_t i = 0; i < before.size(); ++i ) {
            CAPTURE( i, spec.pockets[i].str(), before[i], after[i] );
            CHECK( after[i] == before[i] - 1 );  // set 1 per pocket
        }
    }
}

TEST_CASE( "multimag_matrix_capacity_mods_scale_integral_pockets",
           "[p3][multimag][matrix][mod]" )
{
    mm_mx_install_all_synth_types();
    for( const mm_mx_synth_spec &spec : mm_mx_synth_registry() ) {
        CAPTURE( spec.id.str() );
        // CDF: item built through item::spawn (detached_ptr).
        detached_ptr<item> host_owner = item::spawn( spec.id );
        item &host = *host_owner;
        std::vector<int> base;
        for( size_t i = 0; i < spec.pockets.size(); ++i ) {
            const item_pocket *p = host.pocket_by_id( "p" + std::to_string( i ) );
            REQUIRE( p != nullptr );
            base.push_back( p->ammo_capacity( spec.pockets[i] ) );
        }

        // Double only p0; siblings must keep base capacity.
        const itype_id mod_id( "mm_mx_cap_" + std::to_string( spec.pockets.size() ) );
        mm_mx_ensure_synth_mod( mod_id, [&]( itype & t ) {
            t.mod = cata::make_value<islot_mod>();
            pocket_capacity_mod cm;
            cm.pocket = "p0";
            cm.multiply = 2.0f;
            t.mod->capacity_mods.push_back( cm );
        } );
        // CDF: no item::update_modified_pockets; the mod goes in through item::put_in( MOD ).
        REQUIRE( host.put_in( item::spawn( mod_id ), pocket_type::MOD ).success() );

        const item_pocket *p0 = host.pocket_by_id( "p0" );
        REQUIRE( p0 != nullptr );
        CHECK( p0->ammo_capacity( spec.pockets[0] ) == std::lround( base[0] * 2.0 ) );
        for( size_t i = 1; i < spec.pockets.size(); ++i ) {
            const item_pocket *pi = host.pocket_by_id( "p" + std::to_string( i ) );
            REQUIRE( pi != nullptr );
            CAPTURE( i, spec.pockets[i].str() );
            CHECK( pi->ammo_capacity( spec.pockets[i] ) == base[i] );
        }
    }
}
