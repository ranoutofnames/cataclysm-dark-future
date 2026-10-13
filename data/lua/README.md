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
`lib/cdf_query.lua` (condition queries) and `lib/cdf_time.lua` (time conversions) are pure Lua helpers
replacing common EOC features. Use `require("lib.cdf_sched")` etc. See
`docs/en/mod/lua/guides/helper_library.md`; `data/mods/lua_helper_demo` is a working sample.
