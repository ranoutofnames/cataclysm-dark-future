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

## `lib.cdf_mission` - create, assign, complete and fail missions

A mission type is still JSON (`mission_definition`); Lua drives instances and can run Lua in the
`start`, `end` and `fail` phases. There is no runtime creation of mission types and no Lua-checked
goal: pick a goal that a script can satisfy (for example `MGOAL_KILL_MONSTERS`, which is complete
once `mission:step_complete(1)` is called).

JSON, phases written as `"lua:<id>"` (or `{ "lua": "<id>" }`, which can be combined with the other
phase effects such as `update_mapgen`):

```json
{
  "id": "MY_MISSION", "type": "mission_definition", "name": "My mission",
  "goal": "MGOAL_KILL_MONSTERS", "difficulty": 1, "value": 0,
  "start": "lua:my_mission_start", "end": "lua:my_mission_end", "fail": "lua:my_mission_fail"
}
```

Lua, in `main.lua` (register on every load; saves store only the mission type id and uid):

```lua
local missions = require("lib.cdf_mission")
-- gapi.register_mission_phase(id, fn) stores fn in game.mission_functions[id]
-- (re-registering an id replaces it). Same as assigning the table directly.
gapi.register_mission_phase("my_mission_start", function(params)
  -- params.mission (Mission), params.mission_type (MissionType)
end)

local m = missions.create("MY_MISSION", npc)  -- npc: Npc, CharacterId or nil
missions.assign(m)                             -- runs the start phase
m:step_complete(1)                             -- the goal is met
missions.complete(m)                           -- runs the end phase; or missions.fail(m)
```

- The start phase runs while `mission:get_status()` is still `"yet_to_start"`; end runs with
  `"success"` and fail with `"failure"`. A missing callback or a Lua error is a debug message and the
  phase is skipped.
- `create(type, giver)` returns the mission, or `nil, reason` for an unknown type.
  `assign(m)`, `complete(m, opts)` and `fail(m)` return `true`, or `false, reason` when the mission
  is in the wrong state. `complete` needs the goal met (`m:is_complete(opts.npc_id)`) unless
  `opts.force` is set. `find(uid)` returns the mission or `nil`.
- Bindings behind it: `gapi.register_mission_phase(id, fn)` (errors if `fn` is not a function),
  `Mission.find(uid)`, `Mission.get_all()` (every world mission, including
  unassigned and finished ones), `mission:get_status()` (`"yet_to_start"`, `"in_progress"`,
  `"success"`, `"failure"`), `mission:get_uid()`, `mission:get_assigned_player_id()`, and the
  existing `Mission.reserve_new`, `assign`, `wrap_up`, `fail`, `step_complete`.
- `data/mods/cdf_lua_mission_demo` is a working sample.
