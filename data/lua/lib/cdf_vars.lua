-- CDF helper: variable access with naming parity to the DDA EOC `u_val` / `global_val` / `context_val`.
-- Pure Lua; touches no game state at require time.
local M = {}

---@param name string
---@param type_var string?
---@param context string?
---@return string
M.dialogue_key = function(name, type_var, context)
  return "npctalk_var" .. ((type_var and type_var ~= "") and ("_" .. type_var) or "")
    .. ((context and context ~= "") and ("_" .. context) or "") .. "_" .. name
end

--- Assert that every named key is present in a handler's args table.
---@param args table?
M.expects = function(args, ...)
  for _, k in ipairs({ ... }) do
    if args == nil or args[k] == nil then error("missing required var '" .. tostring(k) .. "'", 2) end
  end
end

--- Variables stored in a plain table (typically a mod_storage subtable); values keep their Lua type.
---@param tbl table
local function wrap_table(tbl)
  local V = {}
  V.get = function(name, default)
    local v = tbl[name]
    if v == nil then return default end
    return v
  end
  V.set = function(name, v)
    tbl[name] = v
    return v
  end
  V.has = function(name) return tbl[name] ~= nil end
  V.clear = function(name) tbl[name] = nil end
  V.add = function(name, delta)
    local n = (tonumber(tbl[name]) or 0) + (delta or 0)
    tbl[name] = n
    return n
  end
  V.inc = function(name) return V.add(name, 1) end
  V.get_num = function(name, default) return tonumber(tbl[name]) or default end
  V.get_str = function(name, default)
    local v = tbl[name]
    if v == nil then return default end
    return tostring(v)
  end
  V.get_bool = function(name, default)
    local v = tbl[name]
    if v == nil then return default end
    return v == true or v == "true" or v == 1
  end
  return V
end

--- Mod-global variables, persisted in game.mod_storage[mod_id]._vars.
---@param mod_id string
M.global = function(mod_id)
  local store = game.mod_storage[mod_id]
  if store._vars == nil then store._vars = {} end
  return wrap_table(store._vars)
end

--- Same API as `global` over an arbitrary table (used for tests and custom stores).
M.from_table = wrap_table

--- Per-creature variables stored via Creature:set_value (shared with JSON dialogue vars).
--- Values are string encoded. Missing keys read back as "" and map to the default.
---@param c Creature
M.of = function(c)
  local V = {}
  V.has = function(name) return c:get_value(name) ~= "" end
  V.get_str = function(name, default)
    local s = c:get_value(name)
    if s == "" then return default end
    return s
  end
  V.get_num = function(name, default)
    local s = c:get_value(name)
    if s == "" then return default end
    return tonumber(s) or default
  end
  V.get_bool = function(name, default)
    local s = c:get_value(name)
    if s == "" then return default end
    return s == "true" or s == "1"
  end
  V.get = function(name, default)
    local s = c:get_value(name)
    if s == "" then return default end
    return tonumber(s) or s
  end
  V.set = function(name, v)
    c:set_value(name, tostring(v))
    return v
  end
  V.clear = function(name) c:remove_value(name) end
  V.add = function(name, delta)
    local n = (V.get_num(name, 0) or 0) + (delta or 0)
    c:set_value(name, tostring(n))
    return n
  end
  V.inc = function(name) return V.add(name, 1) end
  return V
end

return M
