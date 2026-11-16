#pragma once

#include <optional>
#include <string>

#include "translations.h"
#include "type_id.h"

class JsonIn;
class JsonObject;
class JsonOut;
class spell;

struct fake_spell {
    spell_id id;
    // max level this spell can be
    // if null pointer, spell can be up to its own max level
    std::optional<int> max_level;
    // level for things that need it
    int level = 0;
    // target tripoint is source (true) or target (false)
    bool self = false;
    // a chance to trigger the enchantment spells
    int trigger_once_in = 1;
    // a message when the enchantment is triggered
    translation trigger_message;
    // a message when the enchantment is triggered and is on npc
    translation npc_trigger_message;

    fake_spell() = default;
    fake_spell(
        const spell_id& sp_id, bool hit_self = false,
        const std::optional<int>& max_level = std::nullopt)
        : id(sp_id),
          max_level(max_level),
          self(hit_self) {}

    // gets the spell with an additional override for minimum level (default 0)
    auto get_spell(int min_level_override = 0) const -> spell;

    auto operator==(const fake_spell& rhs) const -> bool;

    // Borrowed from LUA_TYPE_OPS, catalua_type_operators.h
    inline auto operator<(const fake_spell& rhs) const -> bool { return (id) < rhs.id; }

    void load(const JsonObject& jo);
    void serialize(JsonOut& json) const;
    void deserialize(JsonIn& jsin);
};
