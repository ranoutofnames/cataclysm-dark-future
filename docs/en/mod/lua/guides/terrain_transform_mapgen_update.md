# Terrain transforms and update mapgen from Lua

These bindings replace the EOC effects `u_transform_radius`, `transform_line` and `mapgen_update`.
All ids are strings; an unknown id returns `false` / `0` (it never throws).

## Transforms

```lua
local map = gapi.get_map()
local here = gapi.abs_to_bub(gapi.get_avatar():global_square_location())

map:transform_at("my_transform", here)                 -- bool
map:transform_radius("my_transform", 3, here)          -- number of tiles visited
map:transform_line("my_transform", here, TripointBubMs.new(here.x + 5, here.y, here.z))   -- number of tiles visited
```

- `transform_radius` affects a disc (`trig_dist <= radius`) on the centre's z-level. Tiles outside
  the loaded map are skipped silently.
- `transform_line` follows the line from the first point to the second: the first tile is excluded
  and the last one included (as in DDA). If both points are the same, that single tile is
  transformed. A line may cross z-levels; the caches of every touched level are refreshed.
- `transform_at` returns `true` only if the terrain or furniture actually changed.
- `gapi.transform_radius_abs(id, radius, center_abs_ms)` does the same for an absolute position,
  including far away from the player. Outside the reality bubble each overlapped overmap terrain
  is loaded, and generated if it does not exist yet. That cost grows with the square of the radius,
  so keep radii small.
- `MapgenConstructor:transform_at(id, pos)` works on the map being generated (for example in an
  `on_mapgen_postprocess` hook). Use it only on the constructor you were given; the `gapi.*`
  functions are main-thread only.

## Update mapgen

```lua
local omt = coords.project_to_omt(gapi.get_avatar():global_square_location())
if gapi.has_update_mapgen("my_update") then
  gapi.run_mapgen_update("my_update", omt, { cancel_on_collision = true })
end
```

Returns `false` for an unknown id or an invalid location. Map caches are refreshed on success.

## Delayed variants

There is no `time_in_future` argument. Use the scheduler from `lib.cdf_sched`:

```lua
S.handler("do_update", function(ctx)
  gapi.run_mapgen_update("my_update", TripointAbsOmt.new(ctx.args.x, ctx.args.y, ctx.args.z))
end)
S.run_after(TimeDuration.from_minutes(10), "do_update", { x = omt.x, y = omt.y, z = omt.z })
```
