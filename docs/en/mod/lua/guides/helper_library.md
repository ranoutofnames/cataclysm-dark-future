# CDF Lua helper library

Pure-Lua modules in `data/lua/lib/`, loaded with `require("lib.<name>")`. None of them touch `game`
or `gapi` at require time. One game turn is one second; durations may be ints (turns) or `TimeDuration`.

## `lib.cdf_sched` - persistent scheduler

```lua
local S = require("lib.cdf_sched").for_mod(game.current_mod)
S.handler("my_tick", function(ctx) --[[ ctx.id, ctx.args, ctx.target, ctx.late ]] end)
S.run_every("tick", TimeDuration.from_minutes(1), "my_tick", {})
S.run_after(TimeDuration.from_minutes(5), "my_alarm", { n = 1 }, { id = "alarm" }) -- queued until the world loads
```

- **Load order.** `main.lua` runs at data load, before the world's saved storage is loaded and before a
  new game's start turn exists. `run_every` / `run_after` / `run_after_range` called while no world is
  live are therefore queued, and applied from the library's own `on_game_load` / `on_game_started`
  hooks, after `_sched` has been loaded. Consequences: calling `run_every` from `main.lua` is correct
  and keeps the saved phase (new games anchor to the real start turn); a queued `run_after` returns the
  given `opts.id` (or `nil` if none) and, when its `id` is already scheduled in the loaded save, is
  left alone. One-shots that must run once per game belong in your own `on_game_started` hook.
  `S.count()` / `S.list()` reflect only applied tasks. Handlers get `ctx.scheduled_for` (TimePoint) and
  `ctx.late` (TimeDuration).
- Saved tasks reference handlers by **name**; functions cannot be saved. Call `S.handler` from `main.lua`
  on every load. Task `args` must be plain serialisable data.
- `run_every` is idempotent by id (keeps its phase), so call it on every load. Interval may be a
  duration, `{lo, hi}` re-rolled each cycle, or a function. Options: `first`, `catch_up`
  (`"skip"` default, or `"all"` capped by `max_catch_up`), `target` (avatar or NPC), `expire`.
- A recurring handler returning `false` cancels itself. A throwing handler is logged and retried later.
- Other API: `run_after_range`, `cancel`, `cancel_all`, `exists`, `time_left`, `list`, `pump`.
- State lives in `game.mod_storage[mod_id]._sched`. Monster-scoped timers are not supported.
- Reloading Lua (`gdebug.reload_lua_code()`) is safe: a single pump hook is installed per Lua state.

## `lib.cdf_vars`

`vars.global(mod_id)` (stored in `mod_storage`) and `vars.of(creature)` (creature values) both provide
`get/set/add/inc/clear/has`. `vars.dialogue_key(name, type, context)` builds the `npctalk_var_*` key used by
JSON dialogue variables.

## `lib.cdf_query`

`effect_intensity`, `has_effect`, `has_trait`, `has_item_with_flag`, `distance`, `monsters_nearby`,
`npcs_nearby`, `inventory_find`, `has_quality(character, "HAMMER", level)` (item type qualities only),
`chance`, `one_in`. `monsters_nearby` filters: `type_ids`, `faction_ids`, `species_ids`, `pred`, `exclude`.

## `lib.cdf_time`

`now`, `turns`, `to_duration`, `to_turns`, `since`.
