-- when a computer's caster casts each Terran spell, and at what. One function a spell, named by
-- the card's "ai". Each is given e.unit and e.order and returns a unit, or x and y, or nothing.

LS = LS or {}
LS.ai = LS.ai or {}
LS.castAi = LS.castAi or {}
local A = LS.ai

-- fire on a clump: it is the cheapest area spell, so it goes off on three units
function LsAiScorchRound(e)
  local x, y = A.area(e, 224, 72, 300, 1)
  return x, y
end

-- the pulse is for casters and shields, not for a line of marines
function LsAiNullPulse(e)
  local u = e.unit
  local c = A.theirCaster(e, 256, { "t_jammed" })
  if c then
    local pX, pY = hx.position(c)
    if A.ownInSplash(u, pX, pY, 112) <= 1 then return pX, pY end
  end
  -- else the thickest clump, but only a thick one: it does no damage
  local x, y = A.area(e, 256, 112, 900, 1)
  return x, y
end

-- drones go out before the fight, not in it
function LsAiSentryDrone(e)
  local u = e.unit
  if not A.spare(u, 30) then return nil end
  local foes = A.enemies(u, 320)
  if #foes > 2 then return nil end
  local x, y = A.at(u)
  return x, y
end

-- the barrage is for buildings and for anything standing still
function LsAiBarrageCall(e)
  local u = e.unit
  if not A.spare(u, 40) then return nil end
  local x, y = A.at(u)
  local b = A.buildingsOf(u, x, y, 384)
  if #b > 0 then
    local c = A.cluster(b, 72)
    if c and A.ownInSplash(u, c.x, c.y, 72) == 0 then return c.x, c.y end
  end
  local ax, ay = A.area(e, 384, 72, 700, 0)
  return ax, ay
end

-- armour for the wave: only when the wave is in a fight
function LsAiBulwarkField(e)
  local u = e.unit
  local foes = A.enemies(u, 256)
  if A.threat(foes) < 400 then return nil end
  local x, y = A.ownCluster(e, 128, 128, 3, { "t_braced" })
  if x then return x, y end
  return nil
end

-- the mark is for the one unit the wave should all shoot
function LsAiFlareMark(e)
  return A.one(e, 288, 400, { "t_marked" })
end

-- thermite comes off the caster, so it waits until something is close
function LsAiThermiteSpray(e)
  local x, y = A.onSelf(e, 80, 250, 3)
  return x, y
end

-- the swarm mends what is hurt, and only where two or more are
function LsAiRepairSwarm(e)
  local u = e.unit
  local hurt = A.hurtAlly(e, 160, 70, { "t_mending" })
  if hurt == nil then return nil end
  local x, y = A.ownCluster(e, 120, 120, 2, { "t_mending" })
  if x then return x, y end
  local pX, pY = hx.position(hurt)
  return pX, pY
end

-- the clamp holds the one thing that must not reach the line
function LsAiShockClamp(e)
  local u = e.unit
  local w = A.theirWorker(e, 256)
  if w and not A.hasAny(w, { "t_clamped" }) then return w end
  return A.one(e, 256, 600, { "t_clamped" })
end

-- overclock when the fight is on and it has not been cast
function LsAiOverclock(e)
  local u = e.unit
  if A.hasAny(u, { "t_overclocked" }) then return nil end
  local x, y = A.onSelf(e, 192, 300, 99)
  return x, y
end

-- the veil is for walking in under fire
function LsAiSmokeVeil(e)
  local u = e.unit
  if A.hasAny(u, { "t_veiled" }) then return nil end
  local foes = A.enemies(u, 224)
  if A.threat(foes) < 500 then return nil end
  local x, y = A.ownCluster(e, 120, 120, 3, { "t_veiled" })
  if x then return x, y end
  return nil
end

-- the anchor is worth it only where the caster can reach something from where it stands
function LsAiSiegeAnchor(e)
  local u = e.unit
  if A.hasAny(u, { "t_anchored" }) then return nil end
  local x, y = A.at(u)
  local foes = A.enemiesAt(u, x, y, 288)
  if A.threat(foes) < 400 then return nil end
  return x, y
end

LS.castAi.ls_t_scorch_round = "LsAiScorchRound"
LS.castAi.ls_t_null_pulse = "LsAiNullPulse"
LS.castAi.ls_t_sentry_drone = "LsAiSentryDrone"
LS.castAi.ls_t_barrage_call = "LsAiBarrageCall"
LS.castAi.ls_t_bulwark_field = "LsAiBulwarkField"
LS.castAi.ls_t_flare_mark = "LsAiFlareMark"
LS.castAi.ls_t_thermite_spray = "LsAiThermiteSpray"
LS.castAi.ls_t_repair_swarm = "LsAiRepairSwarm"
LS.castAi.ls_t_shock_clamp = "LsAiShockClamp"
LS.castAi.ls_t_overclock = "LsAiOverclock"
LS.castAi.ls_t_smoke_veil = "LsAiSmokeVeil"
LS.castAi.ls_t_siege_anchor = "LsAiSiegeAnchor"
