-- when a computer's caster casts each Zerg spell, and at what.

LS = LS or {}
LS.ai = LS.ai or {}
LS.castAi = LS.castAi or {}
local A = LS.ai

-- acid eats armour, so it goes on whatever has the most of it to lose: the thick clump
function LsAiAcidBloom(e)
  local x, y = A.area(e, 224, 80, 350, 1)
  return x, y
end

-- the rot is worth most on a clump that will live a while, and on buildings
function LsAiBileSpray(e)
  local u = e.unit
  local x, y = A.area(e, 256, 96, 500, 1)
  if x then return x, y end
  local ux, uy = A.at(u)
  local b = A.buildingsOf(u, ux, uy, 256)
  if #b >= 3 then
    local c = A.cluster(b, 96)
    if c then return c.x, c.y end
  end
  return nil
end

-- the cloud is laid in front of the line, so it slows what is walking into it
function LsAiSporeCloud(e)
  local x, y = A.area(e, 288, 128, 400, 2)
  return x, y
end

-- the snare holds what is running: a worker first, else the clump
function LsAiRootSnare(e)
  local u = e.unit
  local w = A.theirWorker(e, 224)
  if w and not A.hasAny(w, { "z_rooted" }) then
    local pX, pY = hx.position(w)
    return pX, pY
  end
  local x, y = A.area(e, 224, 88, 300, 1)
  return x, y
end

-- brood goes where the line is thin behind, so the spawn is among the workers
function LsAiSpawnBrood(e)
  local u = e.unit
  if not A.spare(u, 35) then return nil end
  local ux, uy = A.at(u)
  local foes = A.enemiesAt(u, ux, uy, 224)
  local ws = A.workers(foes)
  if #ws > 0 then
    local pX, pY = hx.position(ws[1])
    return pX, pY
  end
  local x, y = A.area(e, 224, 64, 250, 1)
  return x, y
end

-- a husk is made out of a kill, so it is cast where our own have just been fighting
function LsAiInfestHusk(e)
  local u = e.unit
  if not A.spare(u, 40) then return nil end
  local ux, uy = A.at(u)
  local hurt = A.enemiesAt(u, ux, uy, 192)
  local weak = A.weakest(hurt)
  if weak == nil then return nil end
  if A.share(weak) > 35 then return nil end
  local pX, pY = hx.position(weak)
  return pX, pY
end

-- the knit mends the wave, and only when something in it is hurt
function LsAiCarapaceKnit(e)
  local hurt = A.hurtAlly(e, 160, 75, { "z_knit" })
  if hurt == nil then return nil end
  local x, y = A.ownCluster(e, 120, 120, 2, { "z_knit" })
  if x then return x, y end
  local pX, pY = hx.position(hurt)
  return pX, pY
end

-- a molt is the caster saving itself: half life, and something on it worth shedding
function LsAiMolt(e)
  local u = e.unit
  if A.share(u) > 45 then return nil end
  if A.hasAny(u, { "z_molting" }) then return nil end
  local x, y = A.at(u)
  return x, y
end

-- the pheromone is for the moment the wave meets the line
function LsAiFrenzyPheromone(e)
  local u = e.unit
  local foes = A.enemies(u, 224)
  if A.threat(foes) < 400 then return nil end
  local x, y = A.ownCluster(e, 128, 128, 3, { "z_frenzied" })
  if x then return x, y end
  return nil
end

-- devour feeds the caster, so it bites what is already hurt and close
function LsAiDevour(e)
  local u = e.unit
  if A.share(u) > 85 and not A.spare(u, 60) then return nil end
  local foes = A.enemies(u, 160)
  local weak = A.weakest(foes)
  if weak == nil or hx.is_building(weak) then return nil end
  return weak
end

-- the burrow strike is a way in: it goes where their casters and workers are, behind the line
function LsAiBurrowStrike(e)
  local u = e.unit
  if not A.spare(u, 30) then return nil end
  local foes = A.enemies(u, 320)
  local cs = A.casters(foes)
  if #cs > 0 then
    local pX, pY = hx.position(cs[1])
    return pX, pY
  end
  local ws = A.workers(foes)
  if #ws > 0 then
    local c = A.cluster(ws, 80)
    if c then return c.x, c.y end
  end
  return nil
end

-- the hive link is worth it with a wave round the caster, not alone
function LsAiHiveMind(e)
  local u = e.unit
  local x, y = A.ownCluster(e, 144, 144, 4, { "z_hive_linked" })
  if x == nil then return nil end
  local foes = A.enemies(u, 288)
  if A.threat(foes) < 300 then return nil end
  return x, y
end

LS.castAi.ls_z_acid_bloom = "LsAiAcidBloom"
LS.castAi.ls_z_bile_spray = "LsAiBileSpray"
LS.castAi.ls_z_spore_cloud = "LsAiSporeCloud"
LS.castAi.ls_z_root_snare = "LsAiRootSnare"
LS.castAi.ls_z_spawn_brood = "LsAiSpawnBrood"
LS.castAi.ls_z_infest_husk = "LsAiInfestHusk"
LS.castAi.ls_z_carapace_knit = "LsAiCarapaceKnit"
LS.castAi.ls_z_molt = "LsAiMolt"
LS.castAi.ls_z_frenzy_pheromone = "LsAiFrenzyPheromone"
LS.castAi.ls_z_devour = "LsAiDevour"
LS.castAi.ls_z_burrow_strike = "LsAiBurrowStrike"
LS.castAi.ls_z_hive_mind = "LsAiHiveMind"
