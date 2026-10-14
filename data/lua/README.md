# data/lua

This directory contains shared Lua libraries organized under `lib/` subdirectory.

## Directory structure

```
data/lua/
└── lib/                # Standard library namespace
    ├── ui.lua          # Shared UI utilities
    ├── pl/             # Penlight library (example)
    │   ├── utils.lua
    │   ├── path.lua
    │   └── init.lua
    └── mylib/
        └── helper.lua
```

## Importing from mods

Mods must use `lib.` or `bn.lib.` prefix to import from standard library:

```lua
-- In any mod's .lua file
local ui = require("lib.ui")                  -- loads data/lua/lib/ui.lua
local pl_utils = require("lib.pl.utils")      -- loads data/lua/lib/pl/utils.lua
local helper = require("bn.lib.mylib.helper") -- bn.lib. prefix also works
```

This makes it explicit when importing from shared library vs mod-local modules.

## Installing third-party libraries

Install pure-Lua libraries under `lib/` subdirectory:

```bash
# Example: Installing Penlight
git clone https://github.com/lunarmodules/Penlight.git
cp -r Penlight/lua/pl data/lua/lib/
```

Then use in your mods:

```lua
local stringx = require("lib.pl.stringx")
local pretty = require("lib.pl.pretty")
```

## CDF helper library

`lib/cdf_sched.lua` (persistent timers), `lib/cdf_vars.lua` (persistent variables),
`lib/cdf_query.lua` (condition queries), `lib/cdf_time.lua` (time conversions) and
`lib/cdf_mission.lua` (create, assign, complete and fail missions) are pure Lua helpers
replacing common EOC features. Use `require("lib.cdf_sched")` etc. See
`docs/en/mod/lua/guides/helper_library.md`; `data/mods/lua_helper_demo` is a working sample.

## Terrain transforms and update mapgen

Lua can apply JSON `ter_furn_transform`s and run JSON `update_mapgen`s (replacing the EOC effects
`u_transform_radius`, `transform_line` and `mapgen_update`). Ids are plain strings:

- `Map:transform_at(id, pos)`, `Map:transform_radius(id, radius, center)`, `Map:transform_line(id, a, b)`
- `MapgenConstructor:transform_at(id, pos)` (for maps being generated)
- `gapi.transform_radius_abs(id, radius, center_abs_ms)` (works outside the reality bubble)
- `gapi.has_update_mapgen(id)`, `gapi.run_mapgen_update(id, omt, opts?)`

See `docs/en/mod/lua/guides/terrain_transform_mapgen_update.md`.
