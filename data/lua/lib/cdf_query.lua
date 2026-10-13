-- CDF helper: common condition queries (parity with frequently used DDA EOC conditions).
-- Pure Lua; touches no game state at require time. String ids are converted lazily.
local M = {}

local id_cache = { effect = {}, trait = {}, flag = {} }

local function conv(kind, ctor_name, id)
  if type(id) ~= "string" then return id end
  local c = id_cache[kind]
  local v = c[id]
  if v == nil then
    v = _G[ctor_name].new(id)
    c[id] = v
  end
  return v
end

local function effect_id(id) return conv("effect", "EffectTypeId", id) end
local function trait_id(id) return conv("trait", "MutationBranchId", id) end
local function flag_id(id) return conv("flag", "JsonFlagId", id) end

---@param c Creature
---@param id string|EffectTypeId
---@return boolean
M.has_effect = function(c, id) return c:has_effect(effect_id(id)) end

--- Effect intensity, 0 if the effect is absent (EOC `u_effect_intensity`).
---@return integer
M.effect_intensity = function(c, id)
  local eid = effect_id(id)
  if not c:has_effect(eid) then return 0 end
  return c:get_effect_int(eid)
end

---@param c Character
---@param id string|MutationBranchId
---@return boolean
M.has_trait = function(c, id) return c:has_trait(trait_id(id)) end

---@param c Character
---@param flag string|JsonFlagId
---@return boolean
M.has_item_with_flag = function(c, flag) return c:has_item_with_flag(flag_id(flag), false) end

--- First item in the character's inventory (worn, wielded, carried) satisfying pred(item), or nil.
---@param c Character
---@param pred fun(it: Item): boolean
M.inventory_find = function(c, pred)
  for _, it in ipairs(c:all_items(false)) do
    if pred(it) then return it end
  end
  return nil
end

--- Level of tool quality `quality_id` provided by item `it` (its item type's qualities), or 0.
local function item_quality_level(it, quality_id)
  local best = 0
  for k, lvl in pairs(it:get_type():obj().qualities) do
    if k:str() == quality_id and lvl > best then best = lvl end
  end
  return best
end

--- True if any item the character has (worn, wielded, carried) provides quality `quality_id`
--- (a string such as "HAMMER") at `level` or higher (default 1). Uses each item type's static
--- qualities; there is no per-item quality binding.
---@param c Character
---@param quality_id string
---@param level integer?
---@return boolean
M.has_quality = function(c, quality_id, level)
  level = level or 1
  for _, it in ipairs(c:all_items(false)) do
    if item_quality_level(it, quality_id) >= level then return true end
  end
  return false
end

--- Distance between two creatures or coordinates. `kind`: "rl" (default, rectilinear) or "square" (Chebyshev).
---@param a Creature|TripointBubMs
---@param b Creature|TripointBubMs
---@param kind string?
---@return integer?
M.distance = function(a, b, kind)
  local pa = a.get_pos_ms and a:get_pos_ms() or a
  local pb = b.get_pos_ms and b:get_pos_ms() or b
  if kind == "square" then return coords.square_dist(pa, pb) end
  return coords.rl_dist(pa, pb)
end

---@param center Creature|TripointBubMs
---@param list table array of creatures
---@param radius integer
local function within(center, list, radius, excl)
  local out = {}
  for _, cr in ipairs(list) do
    if cr ~= excl then
      local d = M.distance(center, cr, "square")
      if d ~= nil and d <= radius then out[#out + 1] = cr end
    end
  end
  return out
end

--- Active monsters within `radius` (Chebyshev) of `center`. Optional filters:
--- `type_ids` (array of monster type id strings), `faction_ids` (array of monster faction id
--- strings, matched against `monster.faction`), `species_ids` (array of species id strings, matched
--- with `monster:in_species`), `pred` (function(monster) -> boolean), `exclude` (a creature to skip).
---@param center Creature|TripointBubMs
---@param radius integer
---@param filters table?
---@return Monster[]
M.monsters_nearby = function(center, radius, filters)
  local out = within(center, gapi.get_all_monsters(), radius, filters and filters.exclude)
  if filters and (filters.type_ids or filters.faction_ids or filters.species_ids or filters.pred) then
    local function set_of(list)
      if list == nil then return nil end
      local w = {}
      for _, v in ipairs(list) do w[tostring(v)] = true end
      return w
    end
    local want_type, want_fac = set_of(filters.type_ids), set_of(filters.faction_ids)
    local species = {}
    for _, sp in ipairs(filters.species_ids or {}) do species[#species + 1] = SpeciesTypeId.new(sp) end
    local kept = {}
    for _, m in ipairs(out) do
      local ok = (want_type == nil or want_type[m:get_type():str()])
        and (want_fac == nil or want_fac[m.faction:str_id():str()])
        and (filters.pred == nil or filters.pred(m))
      if ok and #species > 0 then
        ok = false
        for _, sp in ipairs(species) do
          if m:in_species(sp) then
            ok = true
            break
          end
        end
      end
      if ok then kept[#kept + 1] = m end
    end
    out = kept
  end
  return out
end

--- Active NPCs within `radius` of `center`.
---@return Npc[]
M.npcs_nearby = function(center, radius, filters)
  return within(center, gapi.get_all_npcs(), radius, filters and filters.exclude)
end

--- Count of monsters near `center` (EOC `u_monsters_nearby`-style).
M.count_nearby = function(center, radius, filters) return #M.monsters_nearby(center, radius, filters) end

--- `x_in_y_chance`: true with probability x/y.
---@param x integer
---@param y integer
M.chance = function(x, y)
  if y <= 0 then return false end
  if x >= y then return true end
  return gapi.rng(1, y) <= x
end

--- `one_in_chance`: true with probability 1/n.
M.one_in = function(n)
  if n <= 1 then return true end
  return gapi.rng(1, n) == 1
end

return M
