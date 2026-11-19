-- Samantha Maxis HUD portrait: OPERATOR[b921894f90835216](ref) is the operator-portrait lookup the HUD themes use
-- (RegisterMaterial(OPERATOR[...](ref))). For Karma's operator (sat_usa_pl_white) or any of her skins, return the
-- loose image maxis_pjs_portrait (images/maxis_pjs_portrait.png) instead.
do
  local function L(s) pcall(KRONOS.log, "maxis portrait: " .. tostring(s)) end
  local IMAGE = "maxis_pjs_portrait"
  -- asset hashes (low 15 hex digits; the top bit differs between stored and runtime forms)
  local KARMA = {
    "2898002338a58f15", -- operator sat_usa_pl_white
    "0aab3a728aa5d68c", "1ca51acdb06a6208", "156a95f45c834ba9", "16a7edaa9ac23a61", "2509a32600948db6",
    "75af8b56a0f352a3", "185a1675abea9ec4", "057b60186525437a", "668914a8ee55241a", "537b4a252e88f1ad",
    "2e7ecaa1466ac1a4", "2975a903d3acd35a", "409b7faac8d00ab8",
  }
  local SET = {}
  for _, h in ipairs(KARMA) do SET[h:sub(-15)] = true end
  local function low15(v)
    if v == nil then return nil end
    local s
    if type(v) == "number" then s = string.format("%x", v) else s = tostring(v):lower():match("(%x+)%s*$") end
    if not s or #s < 15 then return nil end
    return s:sub(-15)
  end
  local seen = {}
  local function isKarma(ref)
    local k = low15(ref)
    local hit = k ~= nil and SET[k] == true
    if k and not seen[k] then seen[k] = true; L("lookup " .. tostring(ref) .. (hit and " -> maxis" or "")) end
    return hit
  end
  local key, wrapper
  local function ensure()
    local op = rawget(_G, "OPERATOR")
    if type(op) ~= "table" then return false end
    key = key or KRONOS.H("b921894f90835216")
    local cur = op[key]
    if cur == wrapper and wrapper ~= nil then return true end
    if type(cur) ~= "function" then return false end
    local orig = cur
    wrapper = function(ref, ...)
      if isKarma(ref) then
        local ok, v = pcall(CLUA.PushXHashAsset, IMAGE)
        if ok and v ~= nil then return v end
      end
      return orig(ref, ...)
    end
    op[key] = wrapper
    L("installed")
    return true
  end
  local ok, e = pcall(ensure)
  if not ok then L("install error " .. tostring(e)) end
  -- OPERATOR can be rebuilt later; re-check whenever a material is registered (HUD build registers many first).
  local RM = rawget(_G, "RegisterMaterial")
  if type(RM) == "function" then
    RegisterMaterial = function(...)
      pcall(ensure)
      return RM(...)
    end
  end
end
