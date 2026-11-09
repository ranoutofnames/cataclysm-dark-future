#include "reload.h"

#include "character.h"
#include "character_functions.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "game_constants.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "player.h"

#include <algorithm>

namespace {
const auto ammo_plutonium = ammotype("plutonium");

// CDF: P3-4-d-b (DDA character_ammo.cpp:96-167). Per-well targets: one option per
// MAGAZINE_WELL of base and of each gunmod, plus one per loaded magazine, in DDA order.
// DDA carries item_location through the loop; CDF has no item_location handle, so the
// entries are item pointers, `parent` is always the base item, and the well index rides
// in item_reload_option::pocket_index. DDA also skips frozen liquids, skips magazines
// whose parent gun cannot fire them, and honours RELOAD_AND_SHOOT; none of those exist
// in this path here.
auto discover_per_well(const Character& who, item& base, const reload::discovery_options& options)
    -> reload::discovery_result {
    // pocket_index < 0 -> first-compatible-well fallback in item::reload.
    struct opt_entry {
        item* target;
        int pocket_index;
    };
    std::vector<opt_entry> opts;

    auto append_owner_options = [&opts](item* owner) {
        opts.push_back({owner, item_reload_option::POCKET_FALLBACK});
        int idx = 0;
        for (item_pocket* p :
             owner->get_pockets([](const item_pocket&) { return true; })) {
            if (p->is_type(pocket_type::MAGAZINE_WELL)) {
                opts.push_back({owner, idx});
                if (item* mag = p->magazine_current()) {
                    opts.push_back({mag, item_reload_option::POCKET_FALLBACK});
                }
            }
            ++idx;
        }
    };

    append_owner_options(&base);
    for (item* mod : base.gunmods()) {
        append_owner_options(mod);
    }

    auto result = reload::discovery_result{};
    bool ammo_match_found = false;
    const auto ammo_search_range = who.is_mounted() ? -1 : 1;
    for (opt_entry& entry : opts) {
        item* const target = entry.target;
        // For per-well entries, filter compatibility against that specific pocket so
        // magazines that fit a sibling well do not leak through.
        const item_pocket* targeted_well = nullptr;
        if (entry.pocket_index >= 0) {
            const auto pockets =
                target->get_pockets([](const item_pocket&) { return true; });
            if (entry.pocket_index < static_cast<int>(pockets.size()) &&
                pockets[entry.pocket_index]->is_type(pocket_type::MAGAZINE_WELL)) {
                targeted_well = pockets[entry.pocket_index];
            }
        }
        for (item* ammo : character_funcs::find_ammo_items_or_mags(
                 who, *target, options.include_empty_mags, ammo_search_range)) {
            const auto well_accepts = targeted_well != nullptr
                                          ? targeted_well->can_reload_with(*ammo, false)
                                          : target->can_reload_with(*ammo, false);
            if (well_accepts) {
                // Record that there's a matching ammo type, even if something is
                // preventing reloading at the moment.
                ammo_match_found = true;
            } else if ((ammo->has_flag(flag_SPEEDLOADER) || ammo->has_flag(flag_SPEEDLOADER_CLIP)) &&
                       target->allows_speedloader(ammo->typeId()) && ammo->ammo_remaining() > 1 &&
                       target->ammo_remaining() < 1) {
                // Again, this is "are they compatible", the later check handles "can we do
                // it now".
                ammo_match_found = target->can_reload_with(*ammo, false);
            }
            const auto emit = targeted_well != nullptr
                                  ? targeted_well->can_reload_with(*ammo, true)
                                  : who.as_player()->can_reload(*target, ammo);
            if (emit) {
                result.options.emplace_back(
                    who.as_player(), target, &base, *ammo, entry.pocket_index);
            }
        }
    }
    result.ammo_match_found = ammo_match_found;
    return result;
}
} // namespace

item_reload_option::item_reload_option(const item_reload_option&) = default;

auto item_reload_option::operator=(const item_reload_option&) -> item_reload_option& = default;

item_reload_option::item_reload_option(
    const player* who, item* target, const item* parent, item& ammo, int pocket_index)
    : who(who),
      target(target),
      ammo(&ammo),
      pocket_index(pocket_index),
      parent(parent) {
    if (this->target->is_ammo_belt()) {
        const auto& linkage = this->target->type->magazine->linkage;
        if (linkage) { max_qty = this->who->charges_of(*linkage); }
    }
    qty(max_qty);
}

auto item_reload_option::moves() const -> int {
    auto mv = ammo->obtain_cost(*who, qty()) + who->item_reload_cost(*target, *ammo, qty());
    if (parent != target) {
        if (parent->is_gun()) {
            mv += parent->get_reload_time();
        } else if (parent->is_tool()) {
            mv += 100;
        }
    }
    return mv;
}

void item_reload_option::qty(int val) {
    // CDF: DDA item::reload_option::qty integral branch (AUTO-30 OD-2); pocket_index is the 3-6 member
    item_pocket* integral_pocket = nullptr;
    if (pocket_index >= 0) {
        // MAGAZINE_WELL targeted reload swaps one magazine. Integral
        // MAGAZINE targeted reload loads loose ammo and follows the
        // general capacity math below, scoped to that pocket so sibling
        // wells do not poison the count on multimag hosts.
        const std::vector<item_pocket*> pockets =
            target->get_pockets([](const item_pocket&) { return true; });
        if (pocket_index < static_cast<int>(pockets.size()) &&
            pockets[pocket_index]->is_type(pocket_type::MAGAZINE)) {
            integral_pocket = pockets[pocket_index];
        } else {
            qty_ = 1;
            return;
        }
    }

    const auto ammo_in_ammo_container = ammo->is_ammo_container();
    const auto ammo_in_container = ammo->is_container();
    auto& ammo_obj = (ammo_in_ammo_container || ammo_in_container) ? ammo->contents.front() : *ammo;

    if (ammo_in_ammo_container && !ammo_obj.is_ammo()) {
        debugmsg("Invalid reload option: %s", ammo_obj.tname());
        return;
    }

    // Checking ammo capacity implicitly limits guns with removable magazines to capacity 0.
    // This gets rounded up to 1 later.
    auto remaining_capacity = 0;
    // CDF: DDA 3335-3340 integral arm, added as the first arm of CDF's capacity chain (ammo_obj is CDF's contents.front() form)
    if (integral_pocket != nullptr) {
        remaining_capacity =
            ammo_obj.is_ammo() ? integral_pocket->remaining_ammo_capacity(ammo_obj.ammo_type()) : 0;
    } else if (target->is_watertight_container() && ammo_obj.made_of(LIQUID)) {
        remaining_capacity = target->get_remaining_capacity_for_liquid(ammo_obj, true);
    } else if (target->is_container() && (ammo_obj.is_ammo() || ammo_obj.is_comestible())) {
        remaining_capacity = ammo_obj.charges_per_volume(target->get_container_capacity());
        if (!target->is_container_empty()) { remaining_capacity -= target->ammo_remaining(); }
    } else {
        remaining_capacity = target->ammo_capacity() - target->ammo_remaining();
    }
    if (target->has_flag(flag_RELOAD_ONE) && !ammo->has_flag(flag_SPEEDLOADER)) {
        remaining_capacity = 1;
    }
    if (ammo_obj.type->ammo && ammo_obj.ammo_type() == ammo_plutonium) { // Why a specific case just
                                                                         // for plutonium? Answer:
                                                                         // Because "it's ooold".
                                                                         // Should be addressed
                                                                         // later...
        remaining_capacity =
            remaining_capacity / PLUTONIUM_CHARGES + (remaining_capacity % PLUTONIUM_CHARGES != 0);
    }

    const auto ammo_by_charges = ammo_obj.is_ammo() || ammo_in_container || ammo->is_comestible();
    int available_ammo;
    if (ammo->has_flag(flag_SPEEDLOADER) || ammo->has_flag(flag_SPEEDLOADER_CLIP)) {
        available_ammo = ammo_obj.ammo_remaining();
    } else {
        available_ammo = ammo_by_charges ? ammo_obj.charges : ammo_obj.count();
    }
    // Constrain by available ammo, target capacity and other external factors (max_qty).
    // @ref max_qty is currently set when reloading ammo belts and limits to available linkages.
    qty_ = std::min({val, available_ammo, remaining_capacity, max_qty});

    // Always expect to reload at least one charge.
    qty_ = std::max(qty_, 1);
}

namespace reload {
auto discover_ammo(const Character& who, item& base, discovery_options options)
    -> discovery_result {
    if (options.per_well_targets) { return discover_per_well(who, base, options); }
    auto result = discovery_result{};
    auto targets = base.gunmods();
    targets.push_back(&base);

    if (base.magazine_current()) { targets.push_back(base.magazine_current()); }

    for (const auto mod : base.gunmods()) {
        if (mod->magazine_current()) { targets.push_back(mod->magazine_current()); }
    }

    const auto ammo_search_range = who.is_mounted() ? -1 : 1;
    for (item* target : targets) {
        for (item* ammo : character_funcs::find_ammo_items_or_mags(
                 who, *target, options.include_empty_mags, ammo_search_range,
                 // CDF: now = false. DDA list_ammo (character_ammo.cpp:87-167) calls find_ammo with
                 // now = true but documents ammo_match_found as set "even if something is
                 // preventing reloading at the moment", and game::reload (game.cpp:7078-7089)
                 // handles full guns before listing. CDF has no such pre-check: prepare() maps
                 // ammo_match_found to nothing_to_reload and include_potential (ranged.cpp) needs
                 // ammo for a full target, so the now-filter is applied by can_reload below.
                 false)) {
            // Don't try to unload frozen liquids.
            if (ammo->is_watertight_container() && ammo->contents_normally_made_of(LIQUID)
                && ammo->contents_made_of(SOLID)) {
                continue;
            }
            const auto id =
                (ammo->is_ammo_container() || ammo->is_container())
                    ? ammo->contents.front().typeId()
                    : ammo->typeId();
            // CDF: find_ammo hands back the container for contained ammo, where DDA's
            // item_location is the ammo itself, so unwrap it for the item-pointer can_reload.
            const item* reload_ammo =
                (ammo->is_ammo_container() || ammo->is_container())
                    ? &ammo->contents.front()
                    : ammo;
            const auto can_reload_with = target->can_reload_with(id);
            if (can_reload_with) {
                // Skip if the magazine is inside a gun/mod that can't fire its ammunition.
                if (target->is_magazine() && target->parent_item()
                && (target->parent_item()->is_gun() || target->parent_item()->is_gunmod())) {
                    const auto ammo_type =
                        (ammo->is_ammo_container() || ammo->is_container())
                            ? ammo->contents.front().ammo_type()
                            : ammo->ammo_type();
                    const auto& supported_ammo = target->parent_item()->ammo_types();
                    const auto gun_supports =
                        std::ranges::any_of(supported_ammo, [&](const ammotype& candidate) {
                            return candidate == ammo_type;
                        });
                    if (!gun_supports) { continue; }
                }
                // Speedloaders require an empty target.
                if (options.include_potential || !ammo->has_flag(flag_SPEEDLOADER)
                    || target->ammo_remaining() < 1) {
                    result.ammo_match_found = true;
                }
            }
            if ((options.include_potential && can_reload_with)
                || who.as_player()->can_reload(*target, reload_ammo)
                || target->has_flag(flag_RELOAD_AND_SHOOT)) {
                result.options.emplace_back(who.as_player(), target, &base, *ammo);
            }
        }
    }
    return result;
}
} // namespace reload

// CDF: P3-4-d-b (DDA character_ammo.cpp:87-168 list_ammo body). The default path keeps CDF
// discovery order and rules and every option carries POCKET_FALLBACK.
auto Character::list_ammo(const item& base, std::vector<item_reload_option>& ammo_list, bool empty,
                          bool per_well_targets) const -> bool {
    // CDF: discover_ammo takes item&; the list built here is read-only.
    auto r = reload::discover_ammo(*this, const_cast<item&>(base),
                                   {.include_empty_mags = empty,
                                    .include_potential = false,
                                    .per_well_targets = per_well_targets});
    ammo_list.insert(ammo_list.end(), r.options.begin(), r.options.end());
    return r.ammo_match_found;
}
