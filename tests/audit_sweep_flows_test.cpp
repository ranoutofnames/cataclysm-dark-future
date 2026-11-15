// PL-62 AT5a: scripted flow sweep over every armor and container type in the loaded data.
//
// One row per (item type, flow) goes to report/sweep-flows.tsv.  The flows are the ones of the
// task draft: spawn/wear (F1), accepted insert (F2), refused insert (F3), unload (F4), drop and
// pick up (F5), save/load round trip (F6), plus the plate rule over every ABLATIVE* plate.
// Rows that fail must be in report/sweep-flows-allow.tsv; the allowlist is a file so DT2 can
// shrink it without editing this test.
//
// The item list is armor, tool-armor, and every type with pockets, in id order.
// CDF_SWEEP_LIMIT=N cuts the list to N items for a quick run; CDF_SWEEP_SHARD=i/n keeps every
// n-th item starting at i-1 (i is 1-based), so shardrun.py can split the sweep.

#include "ablative_coverage.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "detached_ptr.h"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "units.h"
#include "units_mass.h"
#include "units_volume.h"

#include "audit_support.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

const char *const sweep_report_path = "report/sweep-flows.tsv";
const char *const sweep_allow_path = "report/sweep-flows-allow.tsv";
const char *const sweep_limit_env = "CDF_SWEEP_LIMIT";
const char *const sweep_shard_env = "CDF_SWEEP_SHARD";

auto all_types_sorted() -> const std::vector<const itype *> &
{
    static const std::vector<const itype *> types = [] {
        std::vector<const itype *> ret;
        for( const itype *t : item_controller->all() ) {
            if( t != nullptr && !t->get_id().is_null() ) {
                ret.push_back( t );
            }
        }
        std::sort( ret.begin(), ret.end(), []( const itype *a, const itype *b ) {
            return a->get_id().str() < b->get_id().str();
        } );
        return ret;
    }();
    return types;
}

struct shard_spec {
    std::size_t index = 0;
    std::size_t count = 1;
};

auto sweep_shard() -> shard_spec
{
    const char *const value = std::getenv( sweep_shard_env );
    if( value == nullptr || *value == '\0' ) {
        return {};
    }
    const std::string spec( value );
    const std::size_t slash = spec.find( '/' );
    if( slash == std::string::npos ) {
        return {};
    }
    const long index = std::strtol( spec.substr( 0, slash ).c_str(), nullptr, 10 );
    const long count = std::strtol( spec.substr( slash + 1 ).c_str(), nullptr, 10 );
    if( index <= 0 || count <= 0 || index > count ) {
        return {};
    }
    return { static_cast<std::size_t>( index - 1 ), static_cast<std::size_t>( count ) };
}

auto sweep_limit() -> std::size_t
{
    const char *const value = std::getenv( sweep_limit_env );
    if( value == nullptr || *value == '\0' ) {
        return 0;
    }
    const long limit = std::strtol( value, nullptr, 10 );
    return limit > 0 ? static_cast<std::size_t>( limit ) : 0;
}

// Armor, tool-armor and every type with pockets, in id order.
auto sweep_items() -> std::vector<const itype *>
{
    const std::vector<const itype *> &all = all_types_sorted();
    const shard_spec shard = sweep_shard();
    const std::size_t limit = sweep_limit();
    std::vector<const itype *> ret;
    for( std::size_t i = shard.index; i < all.size(); i += shard.count ) {
        const itype &t = *all[i];
        if( t.armor || !t.pockets.empty() ) {
            ret.push_back( &t );
        }
        if( limit > 0 && ret.size() >= limit ) {
            break;
        }
    }
    return ret;
}

auto to_ml( units::volume volume ) -> std::string
{
    return std::to_string( units::to_milliliter( volume ) );
}

auto to_g( units::mass mass ) -> std::string
{
    return std::to_string( static_cast<long long>( units::to_gram( mass ) ) );
}

// A cheap pre-filter before an item probe is spawned: the type has to match the pocket's
// flag/item/material/ammo restriction at all.
auto type_may_match( const pocket_data &p, const itype &t ) -> bool
{
    if( !p.item_id_restriction.empty() && p.item_id_restriction.count( t.get_id() ) == 0 ) {
        return false;
    }
    const pocket_data::FlagsSetType &flags = p.get_flag_restrictions();
    if( !flags.empty() && !std::ranges::any_of( flags, [&t]( const flag_id & flag ) {
    return t.has_flag( flag );
    } ) ) {
        return false;
    }
    if( !p.ammo_restriction.empty() ) {
        if( t.ammo == nullptr || p.ammo_restriction.count( t.ammo->type ) == 0 ) {
            return false;
        }
    }
    if( !p.material_restriction.empty() &&
        !std::ranges::any_of( t.materials, [&p]( const material_id & material ) {
    return p.material_restriction.count( material ) > 0;
    } ) ) {
        return false;
    }
    return true;
}

auto is_ablative_flag( const flag_id &flag ) -> bool
{
    return flag.str().rfind( "ABLATIVE", 0 ) == 0;
}

auto is_plate_type( const itype &t ) -> bool
{
    return std::ranges::any_of( t.get_flags(), is_ablative_flag );
}

// The container-like pockets the flows use (plate slots have their own rule).
auto is_flow_pocket( const pocket_data &p ) -> bool
{
    return p.type == pocket_type::CONTAINER || p.type == pocket_type::MAGAZINE ||
           p.type == pocket_type::MAGAZINE_WELL;
}

struct pocket_plan {
    item_pocket *pocket = nullptr;
    std::size_t index = 0;
    std::vector<itype_id> accepted;
    std::optional<itype_id> refused;
};

// A probe of a charge stack is one charge: a direct pocket insert never splits a stack, so a
// default-charge stack (20 rounds into a 7-round magazine) is refused by the pocket rules and
// says nothing about whether the pocket accepts that ammo type.
auto spawn_probe( const itype_id &id ) -> detached_ptr<item>
{
    return item::spawn( id, calendar::turn, id->count_by_charges() ? 1 : -1 );
}

auto find_liquid_probe( item_pocket &pocket ) -> std::optional<itype_id>
{
    const pocket_data &p = pocket.get_data();
    if( !p.watertight || p.type != pocket_type::CONTAINER ) {
        return std::nullopt;
    }
    for( const itype *t : all_types_sorted() ) {
        if( t->phase != phase_id::LIQUID || !type_may_match( p, *t ) ) {
            continue;
        }
        detached_ptr<item> probe = spawn_probe( t->get_id() );
        if( probe && pocket.can_contain( *probe ).success() ) {
            return t->get_id();
        }
    }
    return std::nullopt;
}

auto find_ammo_probe( item_pocket &pocket, const std::vector<itype_id> &already ) -> std::optional<itype_id>
{
    const pocket_data &p = pocket.get_data();
    if( p.ammo_restriction.empty() ) {
        return std::nullopt;
    }
    for( const itype *t : all_types_sorted() ) {
        if( t->ammo != nullptr && p.ammo_restriction.count( t->ammo->type ) > 0 &&
            std::find( already.begin(), already.end(), t->get_id() ) == already.end() ) {
            detached_ptr<item> probe = spawn_probe( t->get_id() );
            // Only an ammo probe the pocket really accepts belongs in the accepted-insert plan.
            if( probe && pocket.can_contain( *probe ).success() ) {
                return t->get_id();
            }
        }
    }
    return std::nullopt;
}

// Up to 3 accepted types per pocket, one refused type for a restricted pocket, and one liquid or
// ammo probe where the pocket allows it.
auto build_plans( item &container ) -> std::vector<pocket_plan>
{
    std::vector<pocket_plan> plans;
    std::size_t index = 0;
    for( item_pocket &pocket : container.contents.get_pockets() ) {
        const std::size_t this_index = index++;
        const pocket_data &p = pocket.get_data();
        if( !is_flow_pocket( p ) || p.ablative || p.forbidden ) {
            continue;
        }
        pocket_plan plan;
        plan.pocket = &pocket;
        plan.index = this_index;

        for( const itype *t : all_types_sorted() ) {
            if( plan.accepted.size() >= 3 ) {
                break;
            }
            if( t->get_id() == container.typeId() || !type_may_match( p, *t ) ) {
                continue;
            }
            detached_ptr<item> probe = spawn_probe( t->get_id() );
            if( !probe ) {
                continue;
            }
            if( ( probe->made_of( phase_id::LIQUID ) || probe->made_of( phase_id::GAS ) ) &&
                !p.watertight ) {
                continue;
            }
            // Not ignore_contents: that skips the volume/weight/ammo checks against the pocket's
            // contents (src/item_pocket.cpp:1454-1465), so it accepts inserts that then refuse.
            if( pocket.can_contain( *probe ).success() ) {
                plan.accepted.push_back( t->get_id() );
            }
        }

        if( const std::optional<itype_id> liquid = find_liquid_probe( pocket ) ) {
            if( std::find( plan.accepted.begin(), plan.accepted.end(), *liquid ) == plan.accepted.end() ) {
                plan.accepted.push_back( *liquid );
            }
        }
        if( const std::optional<itype_id> ammo = find_ammo_probe( pocket, plan.accepted ) ) {
            if( plan.accepted.size() < 4 ) {
                plan.accepted.push_back( *ammo );
            }
        }

        if( pocket.is_restricted() ) {
            for( const itype *t : all_types_sorted() ) {
                if( t->get_id() == container.typeId() ) {
                    continue;
                }
                detached_ptr<item> probe = item::spawn( t->get_id() );
                if( !probe ) {
                    continue;
                }
                const ret_val<item_pocket::contain_code> res = pocket.can_contain( *probe );
                if( !res.success() && !res.str().empty() ) {
                    plan.refused = t->get_id();
                    break;
                }
            }
        }
        plans.push_back( plan );
    }
    return plans;
}

struct insert_result {
    bool inserted = false;
    std::string reason;
    bool restored = false;
    std::string restore_reason;
};

// Insert one probe into exactly this pocket, measure the growth, then remove it again.
auto insert_and_unload( item_pocket &pocket, const itype_id &id ) -> insert_result
{
    insert_result result;
    detached_ptr<item> probe = spawn_probe( id );
    if( !probe ) {
        result.reason = "cannot spawn " + id.str();
        result.restore_reason = result.reason;
        return result;
    }
    item *const raw = probe.get();
    const units::volume item_volume = raw->volume();
    const units::mass item_weight = raw->weight();
    const int item_count = raw->count();
    const units::volume before_volume = pocket.contains_volume();
    const units::mass before_weight = pocket.contains_weight();

    ret_val<item *> inserted = ret_val<item *>::make_success( nullptr );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        inserted = pocket.insert_item( std::move( probe ), false, false );
    } );
    if( !inserted.success() ) {
        result.reason = inserted.str().empty() ? "insert refused" : inserted.str();
        if( !dmsg.empty() ) {
            result.reason += " (debugmsg: " + audit::compact( dmsg ) + ")";
        }
        result.restore_reason = "nothing to unload";
        return result;
    }

    result.inserted = true;
    const units::volume grew_volume = pocket.contains_volume() - before_volume;
    const units::mass grew_weight = pocket.contains_weight() - before_weight;
    if( grew_volume != item_volume || grew_weight != item_weight ) {
        result.inserted = false;
        result.reason = string_format( "growth volume %s ml (want %s), weight %s g (want %s)",
                                       to_ml( grew_volume ), to_ml( item_volume ),
                                       to_g( grew_weight ), to_g( item_weight ) );
    }
    if( !dmsg.empty() ) {
        result.inserted = false;
        result.reason += " debugmsg: " + audit::compact( dmsg );
    }

    detached_ptr<item> removed = pocket.remove_item( inserted.value() );
    const bool unchanged = pocket.contains_volume() == before_volume &&
                           pocket.contains_weight() == before_weight;
    result.restored = removed && removed->typeId() == id && removed->count() == item_count && unchanged;
    if( !result.restored ) {
        result.restore_reason = string_format( "removed %s, volume %s ml (want %s), weight %s g (want %s)",
                                               removed ? removed->typeId().str() : "nothing",
                                               to_ml( pocket.contains_volume() ), to_ml( before_volume ),
                                               to_g( pocket.contains_weight() ), to_g( before_weight ) );
    }
    return result;
}

struct refuse_result {
    bool refused = false;
    std::string reason;
    bool unchanged = true;
    std::string detail;
};

// A refused probe must leave the pocket exactly as it was, with a non-empty reason.
auto refuse_probe( item_pocket &pocket, const itype_id &id ) -> refuse_result
{
    refuse_result result;
    detached_ptr<item> probe = item::spawn( id );
    if( !probe ) {
        result.detail = "cannot spawn " + id.str();
        return result;
    }
    const units::volume before_volume = pocket.contains_volume();
    const units::mass before_weight = pocket.contains_weight();
    const std::size_t before_size = pocket.size();

    ret_val<item *> inserted = ret_val<item *>::make_success( nullptr );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        inserted = pocket.insert_item( std::move( probe ), false, false );
    } );
    if( inserted.success() ) {
        // The defect case: the probe went in, so remove it and report the refused row as failed.
        detached_ptr<item> back = pocket.remove_item( inserted.value() );
        result.refused = false;
        result.reason = "accepted";
        result.detail = "the pocket accepted " + id.str();
        return result;
    }
    result.refused = true;
    result.reason = inserted.str();
    if( result.reason.empty() ) {
        result.refused = false;
        result.detail = "refusal reason is empty";
    }
    result.unchanged = pocket.contains_volume() == before_volume &&
                       pocket.contains_weight() == before_weight && pocket.size() == before_size;
    if( !result.unchanged ) {
        result.refused = false;
        result.detail = "refusal changed the pocket";
    }
    if( !dmsg.empty() ) {
        result.detail += " debugmsg: " + audit::compact( dmsg );
    }
    return result;
}

auto add_row( std::vector<audit::row> &rows, const itype_id &item, const std::string &flow,
              bool ok, const std::string &reason = std::string() )
{
    rows.push_back( { item.str(), flow, ok, audit::compact( reason ) } );
}

// F1: spawn and wear (armor) or wield (everything else), with no debugmsg.
auto flow_spawn_and_wear( const itype &t, avatar &u, std::vector<audit::row> &rows )
{
    clear_character( u, true );
    detached_ptr<item> item_ptr = item::spawn( t.get_id() );
    if( !item_ptr ) {
        add_row( rows, t.get_id(), "F1", false, "cannot spawn" );
        return;
    }
    detached_ptr<item> left;
    const std::string dmsg = capture_debugmsg_during( [&]() {
        if( t.armor ) {
            left = u.wear_item( std::move( item_ptr ), false );
        } else {
            left = u.wield( std::move( item_ptr ) );
        }
        if( left ) {
            left = u.wield( std::move( left ) );
        }
    } );
    std::string reason;
    if( left ) {
        reason = "could not wear or wield";
    }
    if( !dmsg.empty() ) {
        reason += " debugmsg: " + audit::compact( dmsg );
    }
    add_row( rows, t.get_id(), "F1", reason.empty(), reason );
    u.remove_items_if( { []( const item & ) {
        return true;
    } } );
}

// F5: drop a container with contents on the ground and pick it up again.
auto flow_drop_and_pickup( const itype_id &container_id, const std::optional<itype_id> &candidate,
                           std::vector<audit::row> &rows )
{
    detached_ptr<item> container = item::spawn( container_id );
    if( !container ) {
        add_row( rows, container_id, "F5", false, "cannot spawn" );
        return;
    }
    if( candidate ) {
        container->put_in( item::spawn( *candidate ) );
    }
    const std::size_t before_count = container->contents.all_items_ptr().size() + 1;
    const units::mass before_weight = container->weight();
    const tripoint_bub_ms pos( 60, 60, 0 );
    get_map().add_item( pos, std::move( container ) );

    item *on_ground = nullptr;
    for( item *it : get_map().i_at( pos ) ) {
        if( it->typeId() == container_id ) {
            on_ground = it;
            break;
        }
    }
    if( on_ground == nullptr ) {
        add_row( rows, container_id, "F5", false, "container not on the ground" );
        return;
    }
    const std::size_t ground_count = on_ground->contents.all_items_ptr().size() + 1;
    const units::mass ground_weight = on_ground->weight();
    detached_ptr<item> picked = get_map().i_rem( pos, on_ground );
    const std::size_t picked_count = picked ? picked->contents.all_items_ptr().size() + 1 : 0;
    const units::mass picked_weight = picked ? picked->weight() : 0_gram;

    std::string reason;
    if( before_count != ground_count || before_count != picked_count ) {
        reason = string_format( "count %d/%d/%d", static_cast<int>( before_count ),
                                static_cast<int>( ground_count ), static_cast<int>( picked_count ) );
    }
    if( before_weight != ground_weight || before_weight != picked_weight ) {
        reason += " weight " + to_g( before_weight ) + "/" + to_g( ground_weight ) + "/" +
                  to_g( picked_weight );
    }
    add_row( rows, container_id, "F5", reason.empty(), reason );
}

// F6: serialize and load the container; its contents must survive.
auto flow_roundtrip( const itype_id &container_id, const std::optional<itype_id> &candidate,
                     std::vector<audit::row> &rows )
{
    detached_ptr<item> container = item::spawn( container_id );
    if( !container ) {
        add_row( rows, container_id, "F6", false, "cannot spawn" );
        return;
    }
    const units::mass base_weight = container->weight();
    if( candidate ) {
        container->put_in( item::spawn( *candidate ) );
    }
    const std::size_t before_count = container->contents.all_items_ptr().size();
    const units::mass before_weight = container->weight();

    std::ostringstream buffer;
    {
        JsonOut jsout( buffer );
        container->serialize( jsout );
    }
    std::istringstream input( buffer.str() );
    JsonIn jsin( input );
    detached_ptr<item> loaded = item::spawn( jsin );

    std::string reason;
    // CDF: item::spawn( JsonIn ) applies item migrations, so a migrated container loads under its
    // replacement id (all 40 F6 failures are MIGRATION ids in data/json/obsoletion).
    const itype_id expected_id = item_controller->migrate_id( container_id );
    // CDF: a migrated id loads as its replacement, whose own weight differs from the obsoleted
    // item's (a BN plate is heavier than the DDA plate it retires into). What F6 checks is that
    // the inserted contents survive, so the expected weight is the replacement's base weight plus
    // the contents weight the spawned item carried.
    const units::mass expected_base = expected_id == container_id ? base_weight :
                                      item::spawn( expected_id )->weight();
    const units::mass expected_weight = expected_base + ( before_weight - base_weight );
    if( !loaded ) {
        reason = "load returned nothing";
    } else if( loaded->typeId() != expected_id ) {
        reason = "loaded type " + loaded->typeId().str() + " (want " + expected_id.str() + ")";
    } else if( loaded->contents.all_items_ptr().size() != before_count ) {
        reason = string_format( "count %d (want %d)",
                                static_cast<int>( loaded->contents.all_items_ptr().size() ),
                                static_cast<int>( before_count ) );
    } else if( loaded->weight() != expected_weight ) {
        reason = "weight " + to_g( loaded->weight() ) + " g (want " + to_g( expected_weight ) + ")";
    }
    add_row( rows, container_id, "F6", reason.empty(), reason );
}

// The plate rule: every ABLATIVE* plate goes into every carrier with a matching slot, the slots
// fill, and one more plate is refused.  A plate with no carrier gets a no-carrier row.
// @p suppressed is the seeded-defect hook: that plate is treated as if its ABLATIVE flag were
// removed, and the rule reports the missing flag as a row.
auto run_plate_rule( std::vector<audit::row> &rows,
                     const std::optional<itype_id> &suppressed = std::nullopt ) -> void
{
    std::map<std::string, bool> has_carrier;
    std::vector<const itype *> plates;
    for( const itype *t : all_types_sorted() ) {
        if( is_plate_type( *t ) ) {
            plates.push_back( t );
            has_carrier[t->get_id().str()] = false;
        }
    }

    for( const itype *carrier_type : all_types_sorted() ) {
        const bool has_slots = std::ranges::any_of( carrier_type->pockets, []( const pocket_data & p ) {
            return p.ablative;
        } );
        if( !has_slots ) {
            continue;
        }
        for( const itype *plate_type : plates ) {
            if( suppressed && *suppressed == plate_type->get_id() ) {
                continue;
            }
            detached_ptr<item> carrier = item::spawn( carrier_type->get_id() );
            if( !carrier ) {
                continue;
            }
            detached_ptr<item> probe = item::spawn( plate_type->get_id() );
            if( !probe ) {
                continue;
            }
            int slots = 0;
            for( item_pocket &pocket : carrier->contents.get_pockets() ) {
                if( !pocket.get_data().ablative ) {
                    continue;
                }
                if( pocket.can_contain( *probe, true ).success() ) {
                    slots++;
                }
            }
            if( slots == 0 ) {
                continue;
            }
            has_carrier[plate_type->get_id().str()] = true;
            const std::string flow = "PLATE:" + carrier_type->get_id().str();
            for( int i = 0; i < slots; ++i ) {
                const ret_val<bool> filled = carrier->try_put_in( item::spawn( plate_type->get_id() ) );
                add_row( rows, plate_type->get_id(), flow + ":fill" + std::to_string( i ),
                         filled.success(), filled.str() );
            }
            const ret_val<bool> extra = carrier->try_put_in( item::spawn( plate_type->get_id() ) );
            const std::string extra_reason = extra.str();
            // Any non-empty refusal text is a reason; the wording ("taken", "plate", "too heavy")
            // comes from the pocket, not from this rule.
            const bool refused_with_reason = !extra.success() && !extra_reason.empty();
            add_row( rows, plate_type->get_id(), flow + ":extra", refused_with_reason, extra_reason );
            if( !extra.success() && extra_reason.empty() ) {
                // A refusal without any reason is a finding of its own (stab_plate,
                // test_plate_broken): the player would see no explanation.
                add_row( rows, plate_type->get_id(), flow + ":extra-no-reason", false,
                         "refusal has no reason" );
            }
        }
    }

    for( const itype *plate_type : plates ) {
        const std::string id = plate_type->get_id().str();
        if( suppressed && *suppressed == plate_type->get_id() ) {
            add_row( rows, plate_type->get_id(), "PLATE-detect", false,
                     "seeded defect: ABLATIVE flag removed" );
        } else if( !has_carrier[id] ) {
            add_row( rows, plate_type->get_id(), "PLATE", true, "no-carrier" );
        }
    }
}

auto rows_to_cells( const std::vector<audit::row> &rows ) -> std::vector<std::vector<std::string>>
{
    std::vector<std::vector<std::string>> cells;
    cells.reserve( rows.size() );
    for( const audit::row &r : rows ) {
        cells.push_back( { r.item, r.flow, r.ok ? "1" : "0", r.reason } );
    }
    return cells;
}

auto report_failures( const std::vector<audit::row> &rows ) -> void
{
    const std::vector<std::string> allow = audit::read_keys( sweep_allow_path );
    std::vector<audit::row> unexpected;
    for( const audit::row &r : rows ) {
        if( !r.ok && !audit::allows( r, allow ) ) {
            unexpected.push_back( r );
        }
    }
    if( unexpected.empty() ) {
        return;
    }
    std::string text = string_format( "%d sweep rows failed and are not in %s:\n",
                                      static_cast<int>( unexpected.size() ), sweep_allow_path );
    const std::size_t shown = std::min<std::size_t>( unexpected.size(), 40 );
    for( std::size_t i = 0; i < shown; ++i ) {
        text += unexpected[i].item + '\t' + unexpected[i].flow + '\t' + unexpected[i].reason + '\n';
    }
    if( unexpected.size() > shown ) {
        text += string_format( "... and %d more\n",
                               static_cast<int>( unexpected.size() - shown ) );
    }
    FAIL( text );
}

} // namespace

TEST_CASE( "sweep flows", "[audit][sweep]" )
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, true );
    clear_map();

    const std::vector<const itype *> items = sweep_items();
    WARN( "sweep flows: " << items.size() << " item types" );

    std::vector<audit::row> rows;
    int f1_total = 0;
    int f1_failed = 0;

    for( const itype *t : items ) {
        const std::size_t before = rows.size();
        flow_spawn_and_wear( *t, u, rows );
        for( std::size_t i = before; i < rows.size(); ++i ) {
            if( rows[i].flow == "F1" ) {
                ++f1_total;
                if( !rows[i].ok ) {
                    ++f1_failed;
                }
            }
        }

        detached_ptr<item> container = item::spawn( t->get_id() );
        if( !container ) {
            continue;
        }
        const std::vector<pocket_plan> plans = build_plans( *container );
        std::optional<itype_id> first_candidate;
        for( const pocket_plan &plan : plans ) {
            if( !first_candidate && !plan.accepted.empty() ) {
                first_candidate = plan.accepted.front();
            }
            for( std::size_t k = 0; k < plan.accepted.size(); ++k ) {
                const std::string suffix = string_format( "F%d:p%d:c%d", 2,
                                          static_cast<int>( plan.index ), static_cast<int>( k ) );
                const insert_result inserted = insert_and_unload( *plan.pocket, plan.accepted[k] );
                add_row( rows, t->get_id(), suffix, inserted.inserted, inserted.reason );
                const std::string unload_flow = string_format( "F%d:p%d:c%d", 4,
                                               static_cast<int>( plan.index ), static_cast<int>( k ) );
                add_row( rows, t->get_id(), unload_flow, inserted.restored, inserted.restore_reason );
            }
            if( plan.refused ) {
                const refuse_result refused = refuse_probe( *plan.pocket, *plan.refused );
                const std::string flow = string_format( "F%d:p%d", 3,
                                          static_cast<int>( plan.index ) );
                add_row( rows, t->get_id(), flow, refused.refused,
                         refused.reason.empty() ? refused.detail : refused.reason + " " + refused.detail );
            }
        }
        flow_drop_and_pickup( t->get_id(), first_candidate, rows );
        flow_roundtrip( t->get_id(), first_candidate, rows );
    }

    run_plate_rule( rows );

    const std::vector<std::string> header = { "item", "flow", "ok", "reason" };
    audit::write_tsv( sweep_report_path, header, rows_to_cells( rows ) );

    INFO( "rows: " << rows.size() );
    // Stop condition of the task draft: more than 5% F1 failures is a loader problem.
    const double f1_failure_rate = f1_total == 0 ? 0.0 :
                                   static_cast<double>( f1_failed ) / f1_total;
    CHECK( f1_failure_rate <= 0.05 );
    report_failures( rows );
}

TEST_CASE( "sweep flows detect seeded defects", "[audit][sweep]" )
{
    // Defect 1: a flag-restricted pocket loses the restriction; F3 has to report the accepted probe.
    const itype_id pouch_id( "ammo_satchel" );
    detached_ptr<item> pouch = item::spawn( pouch_id );
    REQUIRE( pouch );
    item_pocket *restricted = nullptr;
    for( item_pocket &pocket : pouch->contents.get_pockets() ) {
        if( !pocket.get_data().get_flag_restrictions().empty() ) {
            restricted = &pocket;
            break;
        }
    }
    REQUIRE( restricted != nullptr );
    const itype_id probe_id( "test_rock" );

    SECTION( "the pocket refuses before the seed and F3 reports it" ) {
        const refuse_result normal = refuse_probe( *restricted, probe_id );
        CHECK( normal.refused );
        CHECK_FALSE( normal.reason.empty() );
    }

    SECTION( "clearing the restriction makes F3 report the acceptance" ) {
        const pocket_data *const original = restricted->get_pocket_data();
        const pocket_data &old = *original;
        pocket_data cleared( old.type );
        cleared.raw_volume_capacity = old.raw_volume_capacity;
        cleared.max_contains_weight = old.max_contains_weight;
        cleared.max_item_volume = old.max_item_volume;
        cleared.min_item_volume = old.min_item_volume;
        cleared.max_item_length = old.max_item_length;
        cleared.min_item_length = old.min_item_length;
        cleared.watertight = old.watertight;
        cleared.airtight = old.airtight;
        cleared.rigid = old.rigid;
        cleared.holster = old.holster;
        cleared.moves = old.moves;
        restricted->set_data( &cleared );

        const refuse_result defect = refuse_probe( *restricted, probe_id );
        restricted->set_data( original );
        CHECK_FALSE( defect.refused );
    }

    // Defect 2: a plate with its ABLATIVE flag masked is reported by the plate rule.
    SECTION( "a plate without its ABLATIVE flag is reported" ) {
        std::vector<const itype *> plates;
        for( const itype *t : all_types_sorted() ) {
            if( is_plate_type( *t ) ) {
                plates.push_back( t );
            }
        }
        REQUIRE_FALSE( plates.empty() );
        std::vector<audit::row> rows;
        run_plate_rule( rows, plates.front()->get_id() );
        const bool reported = std::ranges::any_of( rows, []( const audit::row & r ) {
            return r.flow == "PLATE-detect" && !r.ok &&
                   r.reason.find( "ABLATIVE" ) != std::string::npos;
        } );
        CHECK( reported );
    }
}
