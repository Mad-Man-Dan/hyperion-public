-- when a computer's caster casts each Protoss spell, and at what.

LS = LS or {}
LS.ai = LS.ai or {}
LS.castAi = LS.castAi or {}
local A = LS.ai

-- the lash takes energy, so it goes on their fullest caster
function LsAiMindLash(e)
  local c = A.theirCaster(e, 256, { "p_mind_burned" })
  if c and hx.energy(c) >= 40 then return c end
  return A.one(e, 256, 700, { "p_mind_burned" })
end

-- the rend is the heavy spell: it waits for a clump worth it, and never on our own
function LsAiVoidRend(e)
  local x, y = A.area(e, 256, 80, 600, 0)
  return x, y
end

-- the nova is round the caster, so it waits until the line is on top of it
function LsAiPsiNova(e)
  local x, y = A.onSelf(e, 112, 450, 2)
  return x, y
end

-- the step is both a way in and a way out: out when hurt, in when their casters are reachable
function LsAiWarpStep(e)
  local u = e.unit
  if A.share(u) <= 40 then
    local foes = A.enemies(u, 224)
    local x, y = A.retreat(u, foes, 8)
    return x, y
  end
  local foes = A.enemies(u, 320)
  local cs = A.casters(foes)
  if #cs > 0 then
    local pX, pY = hx.position(cs[1])
    return pX, pY
  end
  local ws = A.workers(foes)
  if #ws > 0 then
    local pX, pY = hx.position(ws[1])
    return pX, pY
  end
  return nil
end

-- the echoes are worth most where they will be shot at instead of the caster
function LsAiSummonEcho(e)
  local u = e.unit
  if not A.spare(u, 35) then return nil end
  local foes = A.enemies(u, 288)
  if A.threat(foes) < 400 then return nil end
  local c = A.cluster(foes, 96)
  if c == nil then return nil end
  local x, y = A.toward(u, c.x, c.y, 96)
  return x, y
end

-- the weave is armour and mending for the wave, cast as the fight starts
function LsAiShieldWeave(e)
  local u = e.unit
  local foes = A.enemies(u, 256)
  if A.threat(foes) < 350 then return nil end
  local x, y = A.ownCluster(e, 120, 120, 3, { "p_shield_woven" })
  if x then return x, y end
  return nil
end

-- the surge gives energy, so it waits until a caster near it is low
function LsAiKhalaSurge(e)
  local u = e.unit
  local mine = A.allies(u, 120)
  local cs = A.casters(mine)
  local low = false
  for i = 1, #cs do
    if hx.energy(cs[i]) < hx.max_energy(cs[i]) / 2 then low = true end
  end
  if not low then
    local hurt = A.hurtAlly(e, 120, 70, { "p_khala_lit" })
    if hurt == nil then return nil end
  end
  local x, y = A.ownCluster(e, 120, 120, 2, nil)
  if x then return x, y end
  return nil
end

-- dilation is laid over what is walking in, not over what is already in the line
function LsAiTimeDilation(e)
  local x, y = A.area(e, 288, 112, 450, 1)
  return x, y
end

-- the rift holds a part of the line, then breaks it; it is the spell worth waiting for
function LsAiChronoRift(e)
  local u = e.unit
  if not A.spare(u, 45) then return nil end
  local x, y = A.area(e, 288, 96, 800, 0)
  return x, y
end

-- the null field is for a line that is shooting, and for their casters above all
function LsAiNullField(e)
  local u = e.unit
  local foes = A.enemies(u, 256)
  local cs = A.casters(foes)
  if #cs >= 2 then
    local c = A.cluster(cs, 96)
    if c and A.ownInSplash(u, c.x, c.y, 96) == 0 then return c.x, c.y end
  end
  local x, y = A.area(e, 256, 96, 700, 0)
  return x, y
end

-- the veil is speed for the wave, and it clears a dilation off it
function LsAiPhaseVeil(e)
  local u = e.unit
  local mine = A.allies(u, 120)
  local held = false
  for i = 1, #mine do
    if A.hasAny(mine[i], { "p_dilated", "p_warp_lag" }) then held = true end
  end
  if held then
    local x, y = A.ownCluster(e, 120, 120, 1, nil)
    if x then return x, y end
  end
  local x, y = A.ownCluster(e, 120, 120, 4, { "p_phased" })
  if x then return x, y end
  return nil
end

-- the tether drags a line apart: it is aimed behind the front, so what it pulls comes alone
function LsAiVoidTether(e)
  local u = e.unit
  if not A.spare(u, 30) then return nil end
  local foes = A.enemies(u, 320)
  if #foes == 0 then return nil end
  local far, fd = nil, 0
  for i = 1, #foes do
    local d = hx.distance(u, foes[i])
    if (not hx.is_building(foes[i])) and d > fd then far, fd = foes[i], d end
  end
  if far == nil or fd < 128 then return nil end
  local pX, pY = hx.position(far)
  return pX, pY
end

LS.castAi.ls_p_mind_lash = "LsAiMindLash"
LS.castAi.ls_p_void_rend = "LsAiVoidRend"
LS.castAi.ls_p_psi_nova = "LsAiPsiNova"
LS.castAi.ls_p_warp_step = "LsAiWarpStep"
LS.castAi.ls_p_summon_echo = "LsAiSummonEcho"
LS.castAi.ls_p_shield_weave = "LsAiShieldWeave"
LS.castAi.ls_p_khala_surge = "LsAiKhalaSurge"
LS.castAi.ls_p_time_dilation = "LsAiTimeDilation"
LS.castAi.ls_p_chrono_rift = "LsAiChronoRift"
LS.castAi.ls_p_null_field = "LsAiNullField"
LS.castAi.ls_p_phase_veil = "LsAiPhaseVeil"
LS.castAi.ls_p_void_tether = "LsAiVoidTether"
