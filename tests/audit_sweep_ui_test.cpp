// PL-62 AT5b: scripted selector sweep over every wearable container in the loaded data.
//
// One row per (item type, flow) goes to report/sweep-ui.tsv.  The item list is every armor or
// tool-armor type with a CONTAINER pocket, in id order.  Each type is worn, up to two accepted
// items are put inside it, and then the selector is driven through the AT5b flows:
//
//   wear      spawn and wear the type, with no debugmsg
//   collapse  select the worn row, collapse it: the contents rows leave the list and the row
//             itself stays; select it again and the contents rows return
//   hide-all  the SHOW_HIDE_CONTENTS_ALL cycle: from the pocket settings to "show all" to
//             "hide all" (every container's contents row gone) and back to the pocket settings
//   highlight select the row: its contents carry the child mark; select a content: the row
//             carries the parent mark (DDA inventory_ui.cpp:4335-4371)
//   insert    the insert selector over the worn container lists the candidate and its header
//             volume line is non-empty
//
// A failing row fails the test unless report/sweep-ui-allow.tsv lists it (item TAB flow), the
// same allowlist policy as AT5a.  CDF_SWEEP_LIMIT=N cuts the list; CDF_SWEEP_SHARD=i/n keeps
// every n-th type starting at shard i (1-based).
//
// AT5b step 5 (force holds_contents() to is_container()) has no test hook: holds_contents() is
// a file-static helper in inventory_ui.cpp, and the source is locked for this packet.  That
// seeded-defect proof is recorded as manual in the report; the collapse/hide-all rows below are
// what would report the defect (a worn container whose contents do not hide).

#include "audit_support.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "flag.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "uistate.h"
#include "units.h"
#include "units_mass.h"
#include "units_volume.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{

const char *const sweep_report_path = "report/sweep-ui.tsv";
const char *const sweep_allow_path = "report/sweep-ui-allow.tsv";
const char *const sweep_limit_env = "CDF_SWEEP_LIMIT";
const char *const sweep_shard_env = "CDF_SWEEP_SHARD";

// Two accepted items are enough to see a contents group collapse and return; the packet caps
// the candidates at 6 per container.
constexpr std::size_t max_candidates = 2;

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

auto has_container_pocket( const itype &t ) -> bool
{
    return std::ranges::any_of( t.pockets, []( const pocket_data & p ) {
        return p.type == pocket_type::CONTAINER && !p.forbidden;
    } );
}

// A container-like type that can be worn: armor data (tool-armor carries it too) and a
// CONTAINER pocket.  id order, with the shard and limit applied as in AT5a.
auto sweep_items() -> std::vector<const itype *>
{
    const std::vector<const itype *> &all = all_types_sorted();
    const shard_spec shard = sweep_shard();
    const std::size_t limit = sweep_limit();
    std::vector<const itype *> ret;
    for( std::size_t i = shard.index; i < all.size(); i += shard.count ) {
        const itype &t = *all[i];
        if( t.armor && has_container_pocket( t ) ) {
            ret.push_back( &t );
        }
        if( limit > 0 && ret.size() >= limit ) {
            break;
        }
    }
    return ret;
}

// A fixed probe list: the smallest non-armor, non-container types plus one liquid and one ammo
// type, so general bags, canteens and ammo pouches all find at least one accepted item.  The
// list is built once and does not depend on the container.
auto candidate_pool() -> const std::vector<itype_id> &
{
    static const std::vector<itype_id> pool = [] {
        std::vector<const itype *> smalls;
        const itype *liquid = nullptr;
        const itype *ammo = nullptr;
        for( const itype *t : all_types_sorted() ) {
            if( t->armor || has_container_pocket( *t ) ) {
                continue;
            }
            if( liquid == nullptr && t->phase == phase_id::LIQUID ) {
                liquid = t;
            }
            if( ammo == nullptr && t->ammo != nullptr ) {
                ammo = t;
            }
            if( t->volume > 500_ml || t->weight > 500_gram ) {
                continue;
            }
            smalls.push_back( t );
        }
        std::sort( smalls.begin(), smalls.end(), []( const itype *a, const itype *b ) {
            if( a->volume != b->volume ) {
                return a->volume < b->volume;
            }
            return a->get_id().str() < b->get_id().str();
        } );
        if( smalls.size() > 128 ) {
            smalls.resize( 128 );
        }
        std::vector<itype_id> ids;
        if( liquid != nullptr ) {
            ids.push_back( liquid->get_id() );
        }
        if( ammo != nullptr ) {
            ids.push_back( ammo->get_id() );
        }
        for( const itype *t : smalls ) {
            const itype_id id = t->get_id();
            if( std::find( ids.begin(), ids.end(), id ) == ids.end() ) {
                ids.push_back( id );
            }
        }
        return ids;
    }();
    return pool;
}

// Up to max_candidates items go into the container's own CONTAINER pockets.  The checked
// insert is quiet, so a probe that does not fit does not fill the test log with debugmsg.
auto place_candidates( item &container ) -> std::vector<item *>
{
    std::vector<item *> placed;
    for( const itype_id &id : candidate_pool() ) {
        if( placed.size() >= max_candidates ) {
            break;
        }
        detached_ptr<item> probe = item::spawn( id );
        if( !probe ) {
            continue;
        }
        item *const raw = probe.get();
        if( container.put_in( std::move( probe ), pocket_type::CONTAINER, false, nullptr,
                              true ).success() ) {
            placed.push_back( raw );
        }
    }
    return placed;
}

void refresh_columns( inventory_selector &sel )
{
    sel.own_gear_column.prepare_paging();
    sel.own_inv_column.prepare_paging();
    sel.map_column.prepare_paging();
}

// The visible row of an item in any column; hidden rows are not found (that is the point of the
// collapse checks).
auto row_for( const inventory_selector &sel, const item *it ) -> const inventory_entry *
{
    for( const inventory_column *col : { &sel.own_gear_column, &sel.own_inv_column,
                                         &sel.map_column } ) {
        for( const inventory_entry &entry : col->entries ) {
            if( entry.is_item() && entry.any_item() == it ) {
                return &entry;
            }
        }
    }
    return nullptr;
}

auto contents_visible( const inventory_selector &sel, const std::vector<item *> &candidates ) -> bool
{
    for( const item *candidate : candidates ) {
        if( row_for( sel, candidate ) == nullptr ) {
            return false;
        }
    }
    return true;
}

// CDF: the test-side stand-in for game_inventory.cpp's file-static insert_inventory_preset.  Only
// get_holster() matters here: it makes the insert selector's header show the container's volume.
struct holster_preset : inventory_selector_preset {
    const item *holster = nullptr;
    const item *get_holster() const override {
        return holster;
    }
    bool is_shown( const item *it ) const override {
        return it != holster;
    }
};

// get_raw_stats() is protected; the sweep asks the insert selector for the same header the game
// draws.
struct insert_probe : inventory_insert_selector {
    using inventory_insert_selector::inventory_insert_selector;
    using inventory_insert_selector::get_raw_stats;
};

auto header_has_volume( const inventory_selector::header_stats &stats ) -> bool
{
    if( stats.empty() || stats.front().empty() ) {
        return false;
    }
    const std::string &line = stats.front().front();
    return line.find( "Total Volume:" ) != std::string::npos &&
           line.find( '/' ) != std::string::npos;
}

auto add_row( std::vector<audit::row> &rows, const itype_id &item, const std::string &flow,
              bool ok, const std::string &reason = std::string() )
{
    rows.push_back( { item.str(), flow, ok, audit::compact( reason ) } );
}

struct hide_override_guard {
    hide_override_guard() {
        uistate.hide_entries_override.reset();
    }
    ~hide_override_guard() {
        uistate.hide_entries_override.reset();
    }
};

struct view_mode_state_guard {
    inventory_selector::uimode sel = inventory_sel_default_state.uimode;
    inventory_selector::uimode inv = inventory_ui_default_state.uimode;
    inventory_selector::uimode pickup_sel = pickup_sel_default_state.uimode;
    inventory_selector::uimode pickup_ui = pickup_ui_default_state.uimode;
    ~view_mode_state_guard() {
        inventory_sel_default_state.uimode = sel;
        inventory_ui_default_state.uimode = inv;
        pickup_sel_default_state.uimode = pickup_sel;
        pickup_ui_default_state.uimode = pickup_ui;
    }
};

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

TEST_CASE( "sweep ui", "[audit][sweep][ui]" )
{
    clear_all_state();
    // The highlight check reads this option; pin the default draw mode.
    const override_option highlight_option( "INVENTORY_HIGHLIGHT", "symbol" );
    const hide_override_guard hide_guard;
    const view_mode_state_guard view_mode_guard;
    avatar &u = get_avatar();
    std::vector<audit::row> rows;
    std::map<std::string, int> flow_total;
    std::map<std::string, int> flow_failed;

    const std::vector<const itype *> items = sweep_items();
    WARN( "sweep ui: " << items.size() << " wearable container types" );

    for( const itype *t : items ) {
        const itype_id id = t->get_id();
        clear_character( u, true );
        // A selector copies this state into its columns at construction.
        uistate.hide_entries_override.reset();

        detached_ptr<item> spawned = item::spawn( id );
        if( !spawned ) {
            add_row( rows, id, "wear", false, "cannot spawn" );
            ++flow_total["wear"];
            ++flow_failed["wear"];
            continue;
        }
        item *const container = spawned.get();
        // POWERARMOR_MOD modules and belt-loop gear (CANT_WEAR, e.g. belt_loop_small) are only
        // worn attached to their carrier, so a bare wear_item() cannot succeed for them.
        const bool attached_only = container->has_flag( flag_POWERARMOR_MOD ) ||
                                   container->has_flag( flag_CANT_WEAR );
        // CDF: read can_wear() before the item moves into the worn tree; its text is the reason a
        // wear is refused (DDA Character::wear reports the same string).
        const ret_val<bool> wearable = u.can_wear( *container );
        const std::string wear_refusal = wearable.success() ? std::string() : wearable.str();
        if( attached_only ) {
            const std::string why = wear_refusal.empty() ?
                                    std::string( "attached gear" ) :
                                    "attached gear: " + audit::compact( wear_refusal );
            for( const char *flow : { "wear", "collapse", "hide-all", "highlight", "insert" } ) {
                add_row( rows, id, flow, true, "skipped: " + why );
                ++flow_total[flow];
            }
            continue;
        }
        detached_ptr<item> leftover;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            leftover = u.wear_item( std::move( spawned ), false );
        } );
        std::string wear_reason = dmsg.empty() ? std::string() : "debugmsg: " + audit::compact( dmsg );
        const bool worn = !leftover && u.is_worn( *container );
        if( !worn ) {
            // A debugmsg while wearing stays a failure; a refusal that carries a reason is
            // attached gear the flags above did not name, so it is skipped like the rest.
            if( wear_reason.empty() && !wear_refusal.empty() ) {
                for( const char *flow : { "wear", "collapse", "hide-all", "highlight", "insert" } ) {
                    add_row( rows, id, flow, true,
                             "skipped: attached gear: " + audit::compact( wear_refusal ) );
                    ++flow_total[flow];
                }
                continue;
            }
            if( wear_reason.empty() ) {
                wear_reason = "not worn";
            }
        }
        add_row( rows, id, "wear", worn, wear_reason );
        ++flow_total["wear"];
        if( !worn ) {
            ++flow_failed["wear"];
            continue;
        }
        if( !container->has_pocket_type( pocket_type::CONTAINER ) ||
            container->has_flag( flag_NO_UNLOAD ) ) {
            const char *const why = container->has_flag( flag_NO_UNLOAD ) ?
                                    "skipped: NO_UNLOAD" : "skipped: no container pocket";
            for( const char *flow : { "collapse", "hide-all", "highlight", "insert" } ) {
                add_row( rows, id, flow, true, why );
                ++flow_total[flow];
            }
            continue;
        }

        // The collapse flow starts from the expanded state, whatever the JSON COLLAPSE_CONTENTS
        // flag said.
        for( item_pocket *pocket : container->get_standard_pockets() ) {
            pocket->settings.set_collapse( false );
        }
        const std::vector<item *> candidates = place_candidates( *container );
        if( candidates.empty() ) {
            for( const char *flow : { "collapse", "hide-all", "highlight", "insert" } ) {
                add_row( rows, id, flow, true, "skipped: no accepted candidate" );
                ++flow_total[flow];
            }
            continue;
        }

        // Step 1: collapse the worn row and expand it again.
        std::string collapse_reason;
        bool collapse_ok = false;
        {
            inventory_selector sel( u );
            sel.add_character_items( u );
            refresh_columns( sel );
            collapse_ok = row_for( sel, container ) != nullptr &&
                          contents_visible( sel, candidates );
            if( !collapse_ok ) {
                collapse_reason = "container or contents rows missing before the collapse";
            }
            if( collapse_ok && !sel.select( container ) ) {
                collapse_ok = false;
                collapse_reason = "worn row could not be selected";
            }
            if( collapse_ok ) {
                sel.toggle_selected_contents();
                refresh_columns( sel );
                const bool kept = row_for( sel, container ) != nullptr;
                const bool hidden = !contents_visible( sel, candidates );
                collapse_ok = kept && hidden;
                if( !kept ) {
                    collapse_reason = "the collapsed row left the list";
                } else if( !hidden ) {
                    collapse_reason = "contents rows are still listed after the collapse";
                }
            }
            if( collapse_ok ) {
                if( !sel.select( container ) ) {
                    collapse_ok = false;
                    collapse_reason = "collapsed row could not be selected again";
                } else {
                    sel.toggle_selected_contents();
                    refresh_columns( sel );
                    collapse_ok = contents_visible( sel, candidates );
                    if( !collapse_ok ) {
                        collapse_reason = "contents rows did not return after the expand";
                    }
                }
            }
        }
        add_row( rows, id, "collapse", collapse_ok, collapse_reason );
        ++flow_total["collapse"];
        if( !collapse_ok ) {
            ++flow_failed["collapse"];
        }

        // Step 2: the hide-all cycle, from the pocket settings (nullopt) to show-all (false) to
        // hide-all (true) and back.
        std::string hide_reason;
        bool hide_ok = false;
        {
            inventory_selector sel( u );
            sel.add_character_items( u );
            refresh_columns( sel );
            hide_ok = contents_visible( sel, candidates );
            if( !hide_ok ) {
                hide_reason = "contents rows missing before the cycle";
            }
            if( hide_ok ) {
                sel.cycle_hide_override_all();
                refresh_columns( sel );
                hide_ok = contents_visible( sel, candidates );
                if( !hide_ok ) {
                    hide_reason = "first press (show all) lost the contents rows";
                }
            }
            if( hide_ok ) {
                sel.cycle_hide_override_all();
                refresh_columns( sel );
                const bool kept = row_for( sel, container ) != nullptr;
                const bool hidden = !contents_visible( sel, candidates );
                hide_ok = kept && hidden;
                if( !kept ) {
                    hide_reason = "the hide-all press removed the container row";
                } else if( !hidden ) {
                    hide_reason = "the hide-all press left the contents rows listed";
                }
            }
            if( hide_ok ) {
                sel.cycle_hide_override_all();
                refresh_columns( sel );
                hide_ok = contents_visible( sel, candidates ) &&
                          !uistate.hide_entries_override.has_value();
                if( !hide_ok ) {
                    hide_reason = "the third press did not return to the pocket settings";
                }
            }
        }
        add_row( rows, id, "hide-all", hide_ok, hide_reason );
        ++flow_total["hide-all"];
        if( !hide_ok ) {
            ++flow_failed["hide-all"];
        }

        // Step 3: the DDA parent/child marks in both directions.
        std::string highlight_reason;
        bool highlight_ok = false;
        {
            inventory_selector sel( u );
            sel.add_character_items( u );
            refresh_columns( sel );
            highlight_ok = sel.select( container );
            if( !highlight_ok ) {
                highlight_reason = "worn row could not be selected";
            } else {
                sel.highlight();
                const inventory_entry *parent_row = row_for( sel, container );
                highlight_ok = parent_row != nullptr;
                for( const item *candidate : candidates ) {
                    const inventory_entry *child_row = row_for( sel, candidate );
                    if( child_row == nullptr || !child_row->highlight_as_child ) {
                        highlight_ok = false;
                        highlight_reason = "a contents row has no child mark";
                        break;
                    }
                }
                if( highlight_ok && !sel.select( candidates.front() ) ) {
                    highlight_ok = false;
                    highlight_reason = "a contents row could not be selected";
                }
                if( highlight_ok ) {
                    sel.highlight();
                    parent_row = row_for( sel, container );
                    highlight_ok = parent_row != nullptr && parent_row->highlight_as_parent;
                    if( !highlight_ok ) {
                        highlight_reason = "the worn row has no parent mark";
                    }
                }
            }
        }
        add_row( rows, id, "highlight", highlight_ok, highlight_reason );
        ++flow_total["highlight"];
        if( !highlight_ok ) {
            ++flow_failed["highlight"];
        }

        // Step 4: the insert selector over the worn container lists a loose candidate and its
        // header carries the used/total volume line.
        std::string insert_reason;
        bool insert_ok = false;
        {
            // A solid candidate: u.i_add cannot store a liquid, so its loose copy would be null.
            const item *candidate = nullptr;
            for( const item *placed : candidates ) {
                if( !placed->made_of( phase_id::LIQUID ) ) {
                    candidate = placed;
                    break;
                }
            }
            if( candidate == nullptr ) {
                insert_ok = true;
                insert_reason = "skipped: no solid accepted candidate";
            } else {
                // A loose copy of the candidate: drop and wield are off, so it is either in the
                // inventory (and a row of the selector) or the reference is null.
                item &loose = u.i_add( item::spawn( candidate->typeId(), calendar::turn ),
                                       false, nullptr, nullptr, false, false );
                holster_preset preset;
                preset.holster = container;
                insert_probe sel( u, preset );
                sel.add_character_items( u );
                refresh_columns( sel );
                const bool listed = !loose.is_null() && row_for( sel, &loose ) != nullptr;
                const bool header = header_has_volume( sel.get_raw_stats() );
                insert_ok = listed && header;
                if( loose.is_null() ) {
                    insert_reason = "the loose candidate could not be added to the inventory";
                } else if( !listed ) {
                    insert_reason = "the insert selector does not list the candidate";
                } else if( !header ) {
                    insert_reason = "the insert header has no volume line";
                }
            }
        }
        add_row( rows, id, "insert", insert_ok, insert_reason );
        ++flow_total["insert"];
        if( !insert_ok ) {
            ++flow_failed["insert"];
        }
    }

    for( const auto &[flow, total] : flow_total ) {
        WARN( "sweep ui " << flow << ": " << total << " rows, "
              << flow_failed[flow] << " failed" );
    }

    const std::vector<std::string> header = { "item", "flow", "ok", "reason" };
    audit::write_tsv( sweep_report_path, header, rows_to_cells( rows ) );

    INFO( "rows: " << rows.size() );
    report_failures( rows );
}
