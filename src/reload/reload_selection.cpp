#include "reload_selection.h"

#include "ammo.h"
#include "item.h"
#include "itype.h"
#include "messages.h"
#include "output.h"
#include "player.h"
#include "translations.h"

#include <algorithm>
#include <string>
#include <utility>

namespace reload_selection {
auto order_ammo(std::vector<item_reload_option>& options) -> void {
    std::ranges::
        stable_sort(options, [](const item_reload_option& lhs, const item_reload_option& rhs) {
            return lhs.ammo->ammo_remaining() > rhs.ammo->ammo_remaining();
        });
    std::ranges::
        stable_sort(options, [](const item_reload_option& lhs, const item_reload_option& rhs) {
            return lhs.moves() < rhs.moves();
        });
    std::ranges::
        stable_sort(options, [](const item_reload_option& lhs, const item_reload_option& rhs) {
            return (lhs.ammo->ammo_remaining() != 0) > (rhs.ammo->ammo_remaining() != 0);
        });
}

auto prepare(const player& who, item& base, selection_options options) -> selection_result {
    auto discovery = reload::discover_ammo(who, base, options.discovery);
    if (discovery.options.empty() && !base.is_holster()) {
        if (!base.is_magazine() && !base.magazine_integral() && !base.magazine_current()) {
            return {.outcome = selection_outcome::missing_magazine};
        }
        if (discovery.ammo_match_found) {
            return {.outcome = selection_outcome::nothing_to_reload};
        }
        return {.outcome = selection_outcome::missing_ammunition};
    }

    order_ammo(discovery.options);
    if (!options.prompt && discovery.options.size() == 1) {
        return {.outcome = selection_outcome::automatic, .selected = discovery.options.front()};
    }

    return prepare(who, std::move(discovery.options));
}

auto prepare(const player& who, std::vector<item_reload_option> options) -> selection_result {
    if (options.empty()) { return {.outcome = selection_outcome::empty_supplied}; }
    if (who.is_npc()) {
        return {.outcome = selection_outcome::automatic, .selected = options.front()};
    }
    return {.outcome = selection_outcome::interaction_required, .options = std::move(options)};
}

auto report(const player& who, const item& base, selection_outcome outcome) -> void {
    switch (outcome) {
        case selection_outcome::empty_supplied:
            who.add_msg_if_player(m_info, _("Never mind."));
            break;

        case selection_outcome::missing_magazine:
            who.add_msg_if_player(
                m_info, _("You need a compatible magazine to reload the %s!"), base.tname());
            break;

        case selection_outcome::nothing_to_reload:
            who.add_msg_if_player(m_info, _("Nothing to reload!"));
            break;

        case selection_outcome::missing_ammunition: {
            auto name = std::string{};
            if (base.ammo_data()) {
                name = base.ammo_data()->nname(1);
            } else if (base.is_watertight_container()) {
                name = base.is_container_empty() ? "liquid" : base.contents.front().tname();
            } else if (base.is_container()) {
                name = base.is_container_empty() ? "items" : base.contents.front().tname();
            } else {
                const auto base_ammo = base.ammo_types();
                name = enumerate_as_string(
                    base_ammo.begin(), base_ammo.end(),
                    [](const ammotype& ammo_type) { return ammo_type->name(); },
                    enumeration_conjunction::none);
            }
            who.add_msg_if_player(
                m_info, _("You don't have any %s to reload your %s!"), name, base.tname());
            break;
        }

        case selection_outcome::automatic:
        case selection_outcome::interaction_required:
            break;
    }
}
} // namespace reload_selection
