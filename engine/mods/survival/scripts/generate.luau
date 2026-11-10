-- the run, rolled whole from the seed before the first frame.
--
-- LsGenerate is types.json's "generate": the loader calls it once as a game loads, hands it ctx
-- (the seed, the settings, the players and their races, the map's read and a reader for the rows
-- it clones from) and takes back records in types.json's own shape, added to the mod's for that
-- game alone, plus run, which the scripts read during the game as hx.run.
--
-- Everything is rolled here: the waves, their timing, their gates, the generated unit kinds with
-- their affixes, the mid-bosses and bosses with their spells, and every name a player reads. The
-- same seed and settings give the same records on every machine, so a replay or a save keeps only
-- the seed and the settings.
--
-- Randomness comes from streams built from the seed alone, one per purpose, so adding a roll in
-- one place does not shift every other roll. A stream is the game's own generator: an LCG of
-- state * 22695477 + 1 taking bits 16 to 30, worked in two halves so every product stays whole
-- in a double. Only + - * / are used, which are exact on every machine.

LS = LS or {}

local M31 = 2147483648

-- a salt a stream, so each one starts somewhere else. Numbers, not strings: the sandbox has no
-- promise of a string library.
local SALT = { waves = 7919, affixes = 104729, spells = 15485863, names = 2654435, race = 48271,
               stats = 1299721, unique = 2038074743 }

local function stream(seed, name)
  local st = { state = (seed * 2654435761 % M31 + (SALT[name] or 1) * 40503) % M31 }
  function st.raw()
    local lo = st.state % 65536
    local hi = (st.state - lo) / 65536
    st.state = (lo * 22695477 + ((hi * 22695477) % 32768) * 65536 + 1) % M31
    return math.floor(st.state / 65536) % 32768
  end
  function st.n(k)
    if k <= 1 then return 0 end
    return st.raw() % k
  end
  function st.range(a, b)
    if b <= a then return a end
    return a + st.n(b - a + 1)
  end
  function st.pick(list)
    local total = 0
    for i = 1, #list do total = total + (list[i].weight or 1) end
    if total <= 0 then return list[1] end
    local r = st.n(total)
    for i = 1, #list do
      r = r - (list[i].weight or 1)
      if r < 0 then return list[i] end
    end
    return list[#list]
  end
  return st
end
LS.stream = stream

-- the difficulty table: how many waves, the first and last wave's supply, the seconds between
-- them, from which wave elites ride and how many, the build-up, and the level the enemy base
-- plays at after the lift. A wave goes out every gap whether the last is cleared or not, two
-- minutes on every level: what a level changes is what walks in, not how long a player has.
LS.levels = {
  { id = "recruit", name = "Recruit", waves = 10, first = 8, last = 40, gap = 120, from = 6,
    elites = { 1, 1 }, build = 480, ai = "very_easy" },
  { id = "veteran", name = "Veteran", waves = 15, first = 10, last = 64, gap = 120, from = 5,
    elites = { 1, 2 }, build = 360, ai = "easy" },
  { id = "hardened", name = "Hardened", waves = 20, first = 12, last = 96, gap = 120, from = 4,
    elites = { 2, 3 }, build = 300, ai = "medium" },
  { id = "brutal", name = "Brutal", waves = 25, first = 16, last = 136, gap = 120, from = 3,
    elites = { 3, 5 }, build = 240, ai = "hard" },
  { id = "nightmare", name = "Nightmare", waves = 30, first = 20, last = 180, gap = 120, from = 2,
    elites = { 5, 7 }, build = 180, ai = "very_hard" },
  { id = "insane", name = "Insane", waves = 40, first = 24, last = 256, gap = 120, from = 1,
    elites = { 7, 10 }, build = 150, ai = "elite" }
}

-- the bodies an elite is rolled onto. They are archetypes like any in the race pools, and
-- what used to make them the six fixed elites -- their names, their stats, the spells they
-- cast -- is rolled now, so no two runs send the same elite. The rows they clone are still
-- the mod's own, which is what keeps their art and their looks; a run borrows the body and
-- writes everything else.
--
-- They are not a race's: an elite of any race may ride any wave, as the mode's notes say.
-- `from` is the wave of the run's own elite schedule each may first ride on.
LS.eliteArchs = {
  { id = "ravager", unit = "ls_ravager", name = "Ravager", role = "bruiser", from = 1,
    weight = 3, cost = 10, look = "ls-t-ember",
    tags = { "ground", "melee", "fire", "heavy" } },
  { id = "phase_templar", unit = "ls_phase_templar", name = "Phase Templar",
    role = "assassin", from = 2, weight = 2, cost = 12, look = "ls-p-void",
    tags = { "ground", "cloaked", "assassin", "blink" } },
  { id = "siege_breaker", unit = "ls_siege_breaker", name = "Siege Breaker",
    role = "artillery", from = 3, weight = 2, cost = 14, look = "ls-t-ironclad",
    tags = { "ground", "artillery", "long_range", "slow" } },
  { id = "aegis_host", unit = "ls_aegis_host", name = "Aegis Host", role = "support",
    from = 4, weight = 2, cost = 14, look = "ls-p-khala",
    tags = { "ground", "support", "aura", "shields" } },
  { id = "stormcaller", unit = "ls_stormcaller", name = "Stormcaller", role = "caster",
    from = 5, weight = 1, cost = 16, look = "ls-p-storm",
    tags = { "ground", "caster", "cc", "area" } },
  { id = "singularity", unit = "ls_singularity", name = "Singularity", role = "caster",
    from = 8, weight = 1, cost = 18, look = "ls-p-void",
    tags = { "air", "caster", "pull", "cc" } }
}

-- which units looks.json already names, so a returned look keeps them when it gains a
-- generated one
LS.lookFixed = {
  { "ls-t-ember", { "ls_ravager" } },
  { "ls-t-ironclad", { "ls_siege_breaker" } },
  { "ls-t-drone-lume", { "ls_sentry_drone" } },
  { "ls-p-void", { "ls_phase_templar", "ls_singularity" } },
  { "ls-p-khala", { "ls_aegis_host" } },
  { "ls-p-storm", { "ls_stormcaller" } },
  { "ls-p-echo", { "ls_warp_echo" } },
  { "ls-hybrid", { "ls_hybrid_reaver" } }
}

local function levelOf(id)
  for i = 1, #LS.levels do
    if LS.levels[i].id == id then return LS.levels[i] end
  end
  return LS.levels[3]
end
LS.levelOf = levelOf

local function has(list, v)
  for i = 1, #list do
    if list[i] == v then return true end
  end
  return false
end
LS.has = has

-- two parts may not meet: the exclusions in scripts/pools/spellparts.luau
local function clashes(tagsA, tagsB)
  local ex = LS.rules.exclude
  for i = 1, #ex do
    local a, b = ex[i][1], ex[i][2]
    if (has(tagsA, a) and has(tagsB, b)) or (has(tagsA, b) and has(tagsB, a)) then return true end
  end
  return false
end

local function floor(x) return math.floor(x) end

-- ---------------------------------------------------------------------------------------------
-- a generated spell: a carrier, a payload and up to n modifiers, written out as an order with
-- its own weapon, tech and blocks.

local function partsFor(race, kind)
  local shared = LS.parts[kind]
  local own = LS.parts.byRace[race] and LS.parts.byRace[race][kind]
  if not own then return shared end
  local out = {}
  for i = 1, #shared do out[#out + 1] = shared[i] end
  for i = 1, #own do out[#out + 1] = own[i] end
  return out
end

local function buildSpell(st, race, id, budget, maxMods, out)
  local carrier = st.pick(partsFor(race, "carriers"))
  local payload = st.pick(partsFor(race, "payloads"))
  local spent = carrier.cost + payload.cost
  local mods = {}
  local tagged = {}
  for i = 1, #carrier.tags do tagged[#tagged + 1] = carrier.tags[i] end
  for i = 1, #payload.tags do tagged[#tagged + 1] = payload.tags[i] end
  local pool = partsFor(race, "modifiers")
  local tries = 0
  while #mods < maxMods and tries < 12 do
    tries = tries + 1
    local m = st.pick(pool)
    local already = false
    for i = 1, #mods do
      if mods[i].id == m.id then already = true end
    end
    if (not already) and (not clashes(tagged, m.tags)) and spent * (m.costX or 1) <= budget then
      mods[#mods + 1] = m
      spent = spent * (m.costX or 1)
      for i = 1, #m.tags do tagged[#tagged + 1] = m.tags[i] end
    end
  end

  local pulses, gap, radius = carrier.pulses, carrier.gap, carrier.radius
  local range, cooldown = carrier.range, carrier.cooldown
  local damage, delay = payload.damage, 0
  for i = 1, #mods do
    local m = mods[i]
    pulses = pulses + (m.pulsesAdd or 0)
    radius = floor(radius * (m.radiusX or 1))
    damage = floor(damage * (m.damageX or 1))
    gap = gap * (m.gapX or 1)
    range = range + (m.rangeAdd or 0)
    delay = delay + (m.delay or 0)
  end
  radius = math.clamp(radius, 16, 256)
  damage = math.max(0, damage)
  pulses = math.clamp(pulses, 1, 8)

  local atCaster = carrier.at == "caster"
  out.weapons[id] = {
    base = atCaster and "psionic_shockwave" or "psionic_storm",
    behavior = atCaster and 5 or 4,
    explosion = damage > 0 and 3 or 1,
    target_flags = atCaster and 66 or 67,
    min_range = 0,
    max_range = atCaster and 8192 or range,
    damage = 0,
    inner_splash = floor(radius * 0.5),
    middle_splash = floor(radius * 0.8),
    outer_splash = radius,
    cooldown = floor(cooldown)
  }
  out.techs[id] = {
    base = "psionic_storm",
    energy = math.clamp(25 + floor(spent * 4), 25, 150),
    icon = 356
  }

  -- the blocks: the payload once a pulse, paced by the carrier's gap
  local hit = {}
  hit[#hit + 1] = { ["do"] = "show", sprite = carrier.sprite }
  local atWord = atCaster and "caster" or nil
  if damage > 0 then
    hit[#hit + 1] = { ["do"] = "damage", amount = damage,
      damage_type = payload.damage_type or "normal", radius = radius, targets = "enemies",
      at = atWord }
  end
  local status = payload.status and payload.status[race]
  if status then
    hit[#hit + 1] = { ["do"] = "status", status = status, radius = radius, targets = "enemies",
      at = atWord }
  end
  if payload.energy then
    hit[#hit + 1] = { ["do"] = "energy", amount = payload.energy, radius = radius,
      targets = "enemies", at = atWord }
  end
  if payload.spawn and payload.spawn[race] then
    hit[#hit + 1] = { ["do"] = "spawn", unit = payload.spawn[race], count = payload.count or 2,
      seconds = payload.seconds or 25 }
  end
  if payload.script then hit[#hit + 1] = { ["do"] = "script", call = payload.script } end
  for i = 1, #mods do
    if mods[i].script then hit[#hit + 1] = { ["do"] = "script", call = mods[i].script } end
  end

  local inner = {}
  if carrier.teleport then inner[#inner + 1] = { ["do"] = "teleport" } end
  local body
  if pulses > 1 then
    body = { { ["do"] = "repeat", times = pulses,
      blocks = { { ["do"] = "wait", seconds = math.max(gap, 0.25), blocks = hit } } } }
  elseif carrier.gap > 0 then
    body = { { ["do"] = "wait", seconds = carrier.gap, blocks = hit } }
  else
    body = hit
  end
  if delay > 0 then
    body = { { ["do"] = "wait", seconds = delay, blocks = body } }
  end
  for i = 1, #body do inner[#inner + 1] = body[i] end

  out.orders[id] = { base = "cast_psionic_storm", weapon = id, tech = id, blocks = inner }

  -- the numbers the modifiers' script helpers read back during the game (hx.run.spells)
  local row = { order = id, radius = radius, damage = damage, pulses = pulses,
    aimed = not atCaster }
  for i = 1, #mods do
    local m = mods[i]
    row.chainHops = m.chainHops or row.chainHops
    row.chainShare = m.chainShare or row.chainShare
    row.splitInto = m.splitInto or row.splitInto
    row.splitShare = m.splitShare or row.splitShare
    row.bounceTiles = m.bounceTiles or row.bounceTiles
    row.bounceShare = m.bounceShare or row.bounceShare
    row.feedShare = m.feedShare or row.feedShare
  end
  out.spellList[#out.spellList + 1] = row

  return { id = id, carrier = carrier, payload = payload, mods = mods, spent = spent,
    radius = radius, damage = damage, pulses = pulses, aimed = not atCaster,
    look = payload.look, namePre = payload.namePre, namePost = payload.namePost }
end

-- ---------------------------------------------------------------------------------------------
-- names

local function spellName(st, spec)
  local head = LS.names.spells.head[st.n(#LS.names.spells.head) + 1]
  local pre, tail = spec.namePre, nil
  for i = 1, #spec.mods do
    pre = spec.mods[i].namePre or pre
    tail = spec.mods[i].namePost or tail
  end
  local s = (pre and (pre .. " ") or "") .. head
  if tail then s = s .. " " .. tail end
  return s
end

local function unitName(st, race, arch, affixes)
  local pre, post = nil, nil
  for i = 1, #affixes do
    pre = affixes[i].namePre or pre
    post = affixes[i].namePost or post
  end
  local core = arch.name
  if #affixes >= 3 then
    core = LS.names[race].core[st.n(#LS.names[race].core) + 1]
  elseif (not pre) and (not post) then
    pre = LS.names[race].pre[st.n(#LS.names[race].pre) + 1]
  end
  local s = (pre and (pre .. " ") or "") .. core
  if post then s = s .. " " .. post end
  return s
end

-- ---------------------------------------------------------------------------------------------
-- a generated unit kind: the archetype's row cloned, with the affixes' numbers over it.
--
-- The factors reach the game as real columns: life and shields as hit_points and shields (read
-- off the base row through ctx.row), damage and cooldown as a cloned weapon the unit points at,
-- and speed as a cloned flingy. Nothing here needs a field the format does not have.

local ROLE_BRAIN = {
  bruiser = "LsBrainBruiser", ranged = "LsBrainRanged", caster = "LsBrainCaster",
  support = "LsBrainSupport", summoner = "LsBrainSummoner", assassin = "LsBrainAssassin",
  artillery = "LsBrainSiege"
}

-- as many affixes as a rank carries, none of them twice and none that clash with what the
-- unit already is. The tries are bounded so the roll cannot run long on a thin pool.
local function pickAffixes(st, pool, arch, want)
  local picked, tries, tags = {}, 0, {}
  for i = 1, #arch.tags do tags[#tags + 1] = arch.tags[i] end
  while #picked < want and tries < 16 do
    tries = tries + 1
    local a = st.pick(pool)
    local already = false
    for i = 1, #picked do
      if picked[i].id == a.id then already = true end
    end
    if (not already) and (not clashes(tags, a.tags)) then
      picked[#picked + 1] = a
      for i = 1, #a.tags do tags[#tags + 1] = a.tags[i] end
    end
  end
  return picked
end

-- a bound from the tier, as a percentage of the base row; 100 where the tier names none
local function rollPct(st, bound)
  if bound == nil then return 100 end
  return st.range(bound[1], bound[2])
end

-- rankId is which rank it is built at (LS.ranks), tier where in the run it was rolled
-- (LS.tiers), and stStat the stream its own numbers are drawn from.
-- boost is a percentage over the rank's own strength, 100 for the rank as written. The
-- elite roll is what turns it, from the player's Elites setting.
local function buildKind(ctx, st, stStat, race, rankId, tier, arch, affixes, spell, out, n,
                         boost)
  local id = "gen_" .. n .. "_" .. arch.id
  local base = ctx.row("units", arch.unit)
  local rank = LS.rankOf(rankId)
  local bounds = (LS.tiers[tier] or LS.tiers[1]).stats
  local strength = math.max(1, math.floor(tonumber(boost) or 100))
  -- every rolled unit draws its own numbers inside its tier's bounds, so two units of one
  -- archetype are not the same unit. The rank's own factor is on the life, which is where a
  -- player reads a rank; the rest is the tier's spread alone.
  --
  -- Each draw is kept as it is made, and every part that moves a number afterwards writes
  -- itself down beside it (`from`), because the readout shows a player where a number came
  -- from -- the rank, the roll, then each affix by name -- and a sum cannot be taken apart
  -- again. The draws are in the order they always were: the stream is the run's, and moving
  -- one would roll a different run from the same seed.
  local rankPct = rank.life * strength / 100
  local rollLife = rollPct(stStat, bounds.life)
  local rollShields = rollPct(stStat, bounds.shields)
  local rollDamage = rollPct(stStat, bounds.damage)
  local rollSpeed = rollPct(stStat, bounds.speed)
  local rollCooldown = rollPct(stStat, bounds.cooldown)
  local rollArmor = stStat.range(bounds.armor[1], bounds.armor[2])
  -- the same arithmetic as before the draws were kept, in the same order, so a seed rolls the
  -- run it always rolled: two divisions by a hundred are not always one by ten thousand
  local life = rank.life * strength / 10000 * rollLife / 100
  local shields = rollShields / 100
  local damage = rollDamage / 100
  local speed = rollSpeed / 100
  local cooldown = rollCooldown / 100
  local armor = rollArmor
  local from = {
    life = { { "rank", floor(rankPct) }, { "roll", rollLife } },
    shields = { { "roll", rollShields } },
    damage = { { "roll", rollDamage } },
    speed = { { "roll", rollSpeed } },
    cooldown = { { "roll", rollCooldown } },
    armor = { { "roll", rollArmor } }
  }
  local range = 0
  local auras, scripts, tags = {}, {}, {}
  for i = 1, #arch.tags do tags[#tags + 1] = arch.tags[i] end
  for i = 1, #affixes do
    local a = affixes[i]
    local u = a.unit or {}
    life = life * (u.life or 1)
    damage = damage * (u.damage or 1)
    speed = speed * (u.speed or 1)
    cooldown = cooldown * (u.cooldown or 1)
    shields = shields * (u.shields or 1)
    armor = armor + (u.armor or 0)
    range = range + (u.range or 0)
    if u.life then from.life[#from.life + 1] = { a.name, floor(u.life * 100 + 0.5) } end
    if u.shields then from.shields[#from.shields + 1] = { a.name, floor(u.shields * 100 + 0.5) } end
    if u.damage then from.damage[#from.damage + 1] = { a.name, floor(u.damage * 100 + 0.5) } end
    if u.speed then from.speed[#from.speed + 1] = { a.name, floor(u.speed * 100 + 0.5) } end
    if u.cooldown then from.cooldown[#from.cooldown + 1] = { a.name, floor(u.cooldown * 100 + 0.5) } end
    if u.armor then from.armor[#from.armor + 1] = { a.name, u.armor } end
    -- a unit carries up to three auras, so each affix that brings one is kept
    if a.aura and #auras < 3 then auras[#auras + 1] = a.aura end
    if a.onHit then scripts.on_hit = a.onHit end
    if a.onDeath then scripts.on_death = a.onDeath end
    if a.onDamaged then scripts.on_damaged = a.onDamaged end
    for k = 1, #a.tags do tags[#tags + 1] = a.tags[k] end
  end
  -- the rank's cap, after everything else: the draws above are made either way, so the stream
  -- rolls the rest of the run as it did. What the cap took is written down beside the rest.
  if rank.cap then
    local function capped(stat, v, mid, pct)
      local c = math.clamp(v, mid * (1 - pct / 100), mid * (1 + pct / 100))
      if c ~= v then from[stat][#from[stat] + 1] = { "cap", floor(c / v * 100 + 0.5) } end
      return c
    end
    if rank.cap_life then
      life = capped("life", life, rank.life * strength / 10000, rank.cap_life)
    else
      life = capped("life", life, 1, rank.cap)
    end
    shields = capped("shields", shields, 1, rank.cap)
    damage = capped("damage", damage, 1, rank.cap)
    speed = capped("speed", speed, 1, rank.cap)
    cooldown = capped("cooldown", cooldown, 1, rank.cap)
    local most = rank.cap_armor or 1
    if armor > most then
      from.armor[#from.armor + 1] = { "cap", most - armor }
      armor = most
    end
  end

  local rec = {
    base = arch.unit,
    name = unitName(st, race, arch, affixes),
    hit_points = math.max(1, floor(base.hit_points * life)),
    armor = math.max(0, base.armor + armor),
    destroy_score = 100 + floor(60 * life)
  }
  if base.shields_enabled == 1 and base.shields > 0 then
    rec.shields_enabled = 1
    rec.shields = math.max(1, floor(base.shields * shields))
  end
  if range ~= 0 then
    rec.target_range = math.max(1, base.target_range + range)
    rec.sight_range = math.max(1, base.sight_range + range)
  end

  -- the weapon, cloned when the damage or the cooldown moved
  if (damage ~= 1 or cooldown ~= 1 or range ~= 0) and base.ground_weapon_name then
    local wid = id .. "_weapon"
    local w = ctx.row("weapons", base.ground_weapon_name)
    out.weapons[wid] = {
      base = base.ground_weapon_name,
      damage = math.max(0, floor(w.damage * damage)),
      cooldown = math.clamp(floor(w.cooldown * cooldown), 1, 255),
      max_range = math.max(w.max_range, w.max_range + range * 32)
    }
    -- what an affix's hits put on, where it asked for one
    for i = 1, #affixes do
      if affixes[i].weapon and affixes[i].weapon.status then
        out.weapons[wid].status = affixes[i].weapon.status
      end
    end
    rec.ground_weapon = wid
    if base.air_weapon_name == base.ground_weapon_name then rec.air_weapon = wid end
  elseif base.ground_weapon_name then
    for i = 1, #affixes do
      if affixes[i].weapon and affixes[i].weapon.status then
        local wid = id .. "_weapon"
        out.weapons[wid] = { base = base.ground_weapon_name,
          status = affixes[i].weapon.status }
        rec.ground_weapon = wid
        if base.air_weapon_name == base.ground_weapon_name then rec.air_weapon = wid end
      end
    end
  end

  -- the flingy, cloned when the speed moved
  if speed ~= 1 and base.flingy_name then
    local fid = id .. "_flingy"
    local f = ctx.row("flingy", base.flingy_name)
    out.flingy[fid] = { base = base.flingy_name,
      top_speed = math.clamp(floor(f.top_speed * speed), 1, 4096) }
    rec.flingy = fid
  end

  if #auras == 1 then rec.aura = auras[1] elseif #auras > 1 then rec.aura = auras end
  scripts.brain = ROLE_BRAIN[arch.role] or "LsBrainBruiser"
  if rankId == "boss" then scripts.brain = "LsBrainBoss" end
  if rankId == "midboss" then scripts.brain = "LsBrainMidBoss" end
  scripts.every = 1
  rec.scripts = scripts
  out.units[id] = rec

  -- the looks: every one its parts bring. The engine paints the unit with the last that paints
  -- it and shows the auras of the others, so an affix's own glow is not lost
  local looks = {}
  local function wants(name)
    if not name then return end
    for i = 1, #looks do
      if looks[i] == name then return end
    end
    looks[#looks + 1] = name
  end
  for i = 1, #affixes do wants(affixes[i].look) end
  if spell then wants(spell.look) end
  wants(arch.look)
  for i = 1, #looks do
    out.lookOf[#out.lookOf + 1] = { looks[i], id }
  end

  -- its card: the affixes' spells first, then the generated one
  local card, pos = {}, 6
  for i = 1, #affixes do
    if affixes[i].spell and pos <= 9 then
      card[#card + 1] = { position = pos, cast = affixes[i].spell, target = true,
        ai = { call = LS.aiOf(affixes[i].spell), every = 1 } }
      pos = pos + 1
    end
  end
  if spell and pos <= 9 then
    card[#card + 1] = { position = pos, cast = spell.id, target = spell.aimed,
      ai = { call = "LsAiGenerated", every = 1 } }
  end
  if #card > 0 then out.cards[id] = card end

  -- what this kind rolled, kept in the run for the scripts to read in the game: it is what the
  -- readout tells a player about the unit in front of them (its rank, what it is, what it
  -- carries and how far every number of it has moved from the row it clones), and none of it
  -- can be worked out again from the record above, where the numbers are already summed. The
  -- percentages are of the base row, armour is points, and a cooldown under 100 is faster.
  --
  -- The list is indexed by the kind's own number, the n in its name, and the mode stamps that
  -- number on every unit it makes of the kind (mode.luau, "ls_roll"), so the readout reads a
  -- unit's roll in one step rather than naming all of them to hx.id.
  local marks = {}
  for i = 1, #affixes do
    marks[#marks + 1] = { name = affixes[i].name, line = affixes[i].line }
  end
  out.rolled[n] = {
    name = rec.name, rank = rankId, arch = arch.name, role = arch.role, tier = tier,
    affixes = marks, spell = spell and spell.name or nil,
    stats = { life = floor(life * 100 + 0.5), shields = floor(shields * 100 + 0.5),
              damage = floor(damage * 100 + 0.5), speed = floor(speed * 100 + 0.5),
              cooldown = floor(cooldown * 100 + 0.5), armor = armor },
    from = from
  }

  return { id = id, name = rec.name, arch = arch, affixes = affixes, spell = spell, tags = tags,
    role = arch.role, cost = arch.cost }
end

-- which cast-AI function serves a library spell; a generated spell always uses LsAiGenerated
function LS.aiOf(order)
  return (LS.castAi and LS.castAi[order]) or "LsAiGenerated"
end

-- ---------------------------------------------------------------------------------------------
-- the perimeter, read from the map rather than set in tiles, so one setting fits every map.
--
-- The edge is a circle, so its radius is measured on straight lines: a natural twenty tiles
-- below the main may be forty round a cliff, and a circle of forty would take in half the map.
--
--   tight     the home base's own fields and six tiles of room
--   standard  out to the nearest other base, at most 26 tiles
--   wide      45% of the straight line to the enemy, at most 40 tiles
--
-- and in every case 12 at the least and never past half the way to the enemy.

local NEAR_CAP, WIDE_CAP, LEAST = 26, 40, 12

local function lineTiles(ax, ay, bx, by)
  return floor(hx.isqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)) / 32)
end

function LS.ringFor(map, home, foe, want)
  local bases = map.bases or {}
  -- the home base's own cluster, and the nearest other one by the walk, which is the base a
  -- player takes next
  local mine, natural, md, nd = nil, nil, -1, -1
  for i = 1, #bases do
    local b = bases[i]
    if (b.fields or 3) >= 3 then
      local d = lineTiles(b.x, b.y, home.x, home.y)
      if md < 0 or d < md then md, mine = d, b end
    end
  end
  for i = 1, #bases do
    local b = bases[i]
    if b ~= mine and (b.fields or 3) >= 3 then
      local d = map.walk and map.walk(home.x, home.y, b.x, b.y) or nil
      if d == nil or d < 0 then d = lineTiles(b.x, b.y, home.x, home.y) end
      if nd < 0 or d < nd then nd, natural = d, b end
    end
  end

  local toFoe = lineTiles(home.x, home.y, foe.x, foe.y)
  local rTight = mine and math.max(13, lineTiles(mine.x, mine.y, home.x, home.y)
    + floor((mine.spread or 0) / 32) + 6) or 14
  local rNear = natural and math.max(rTight, math.min(NEAR_CAP,
    lineTiles(natural.x, natural.y, home.x, home.y) + floor((natural.spread or 0) / 32) + 5))
    or math.max(rTight, math.min(NEAR_CAP, rTight + 8))
  local rWide = math.max(rNear, math.min(WIDE_CAP, floor(toFoe * 45 / 100)))
  local r = rNear
  if want == "tight" then r = rTight elseif want == "wide" then r = rWide end
  r = math.max(LEAST, math.min(WIDE_CAP, r, floor(toFoe / 2)))
  local reach = math.max(r, floor(toFoe * 45 / 100))
  return { x = home.x, y = home.y, tiles = r, base = r, reach = reach, toFoe = toFoe }
end

-- the whole perimeter: a ring round every home, or one round all of them when the allies share
-- a line, with the gates on each. The game works its run's perimeter out here and the set-up's
-- preview draws the same call, so the picture a player is shown before the game is the ground
-- the game gives them.
function LS.ringsFor(map, homes, foe, want, shared)
  if #homes == 0 or foe == nil then return {}, {} end
  if shared then
    -- one circle round every home: its centre is their middle, and its radius the widest of
    -- the reads plus the distance from the middle to the home farthest out
    local sx, sy = 0, 0
    for i = 1, #homes do sx, sy = sx + homes[i].x, sy + homes[i].y end
    local cx, cy = floor(sx / #homes), floor(sy / #homes)
    local widest, spread = nil, 0
    for i = 1, #homes do
      local r = LS.ringFor(map, homes[i], foe, want)
      if widest == nil or r.tiles > widest.tiles then widest = r end
      local s = lineTiles(cx, cy, homes[i].x, homes[i].y)
      if s > spread then spread = s end
    end
    widest.x, widest.y = cx, cy
    -- the caps hold for the shared circle too: it never passes forty tiles, nor half the way
    -- from the middle to the enemy
    local half = floor(lineTiles(cx, cy, foe.x, foe.y) / 2)
    widest.tiles = math.max(LEAST, math.min(WIDE_CAP, widest.tiles + spread, half))
    widest.base = widest.tiles
    widest.reach = math.max(widest.tiles,
      math.min(WIDE_CAP, floor(lineTiles(cx, cy, foe.x, foe.y) * 45 / 100)))
    return { widest }, LS.gatesOn(map, widest, foe)
  end
  local rings, gates = {}, {}
  for i = 1, #homes do
    local r = LS.ringFor(map, homes[i], foe, want)
    r.player = homes[i].player
    rings[#rings + 1] = r
    local g = LS.gatesOn(map, r, foe)
    for k = 1, #g do
      g[k].ring = i
      gates[#gates + 1] = g[k]
    end
  end
  return rings, gates
end

-- the band just outside the edge, in tiles, where a unit is warned and not yet hurt. The run
-- carries its own; this is what it is rolled at, and what the set-up's preview draws.
LS.BAND_TILES = 4

-- the gates: where a ground path from the enemy reaches the edge. The main gate is the point on
-- the circle nearest the enemy; the flanks are a fifth of a turn either side of it, taken only
-- where the ground there can be walked on.
function LS.gatesOn(map, ring, foe)
  local dx, dy = foe.x - ring.x, foe.y - ring.y
  local d = hx.isqrt(dx * dx + dy * dy)
  if d < 1 then d, dx, dy = 1, 1, 0 end
  local out = {}
  local turns = { 0, 0.2, -0.2, 0.4, -0.4 }
  local kinds = { "main", "flank", "flank", "flank", "flank" }
  for i = 1, #turns do
    local a = math.atan2(dy, dx) + turns[i] * 2 * math.pi
    local r = ring.tiles * 32
    local gx = ring.x + floor(math.cos(a) * r)
    local gy = ring.y + floor(math.sin(a) * r)
    local ok = true
    if map.walkable then
      ok = map.walkable(floor(gx / 32), floor(gy / 32))
    end
    if ok and #out < 3 then
      out[#out + 1] = { x = gx, y = gy, kind = kinds[i], air = false }
    end
  end
  if #out == 0 then
    -- an island home: the waves fly, and the gate is the edge nearest the enemy
    out[1] = { x = ring.x + floor(dx * ring.tiles * 32 / d),
      y = ring.y + floor(dy * ring.tiles * 32 / d), kind = "main", air = true }
  end
  return out
end

-- ---------------------------------------------------------------------------------------------
-- the waves

local function budget(lvl, wave)
  local n = math.max(1, lvl.waves)
  local t = (wave - 1) / math.max(1, n - 1)
  if t > 1 then t = 1 end
  -- the curve rises slowly and then steeply: multiplication only, so every machine agrees
  local sup = lvl.first + (lvl.last - lvl.first) * t * (0.35 + 0.65 * t)
  if wave % 5 == 0 then sup = sup * 1.25 end
  if wave >= n then sup = sup * 1.5 end
  return floor(sup + 0.5)
end
LS.budget = budget

-- which tier a wave buys from: pools/tiers.luau says where each one opens, so how far a
-- run reaches is that file's to change and not this one's.
local function tierOf(lvl, wave)
  return LS.tierAt(lvl, wave)
end

-- a wave breaks a rule when it carries more of a tag than its difficulty allows
local CAPPED = { "cc", "cloaked", "summoner", "artillery", "long_range" }
local function capBroken(kinds, counts, caps)
  local seen = {}
  for i = 1, #kinds do
    local tags = kinds[i].tags
    for k = 1, #tags do
      seen[tags[k]] = (seen[tags[k]] or 0) + counts[i]
    end
  end
  for i = 1, #CAPPED do
    local cap = caps[CAPPED[i]]
    if cap and (seen[CAPPED[i]] or 0) > cap then return CAPPED[i] end
  end
  return nil
end

function LsGenerate(ctx)
  -- the tables this reads, checked before anything is rolled: a run off wrong data would come
  -- out strange or fail somewhere further in, so it refuses here and says why
  local fault = LS.dataFault()
  if fault then
    error("the mod's own tables are wrong, so no run can be rolled: " .. fault, 0)
  end

  local out = { units = {}, weapons = {}, orders = {}, techs = {}, flingy = {}, cards = {},
    looks = {}, lookOf = {}, spellList = {}, rolled = {} }
  local s = ctx.settings
  local lvl = levelOf(s.difficulty)
  local startWave = math.floor(tonumber(s.start_wave) or 1)
  if startWave < 1 then startWave = 1 end
  if startWave > lvl.waves then startWave = lvl.waves end
  -- how often a group walks in as a unique group, in hundredths
  local uniqueAt = math.floor(tonumber(s.unique_groups) or 15)
  if uniqueAt < 0 then uniqueAt = 0 end
  if uniqueAt > 100 then uniqueAt = 100 end
  local seed = s.seed
  if seed == nil or seed == 0 then seed = ctx.seed end

  -- which race the waves are: the enemy base's own, Random drawn here so every machine draws
  -- the same one
  local race = (ctx.enemy and ctx.enemy.race) or "zerg"
  if race == "random" then
    local races = { "zerg", "protoss", "terran" }
    race = races[stream(seed, "race").n(3) + 1]
  end
  if not LS.archetypes[race] then race = "zerg" end

  local stWave = stream(seed, "waves")
  local stAffix = stream(seed, "affixes")
  local stSpell = stream(seed, "spells")
  local stName = stream(seed, "names")
  local stStat = stream(seed, "stats")
  local stUniq = stream(seed, "unique")

  local caps = LS.rules.caps[lvl.id] or LS.rules.caps.hardened
  local archs = LS.archetypes[race]
  local affixPool = LS.affixes[race]
  local made, waves, names = 0, {}, {}
  local at = lvl.build

  -- how strong and how often the elites ride, as a percentage: 100 is the run's own
  -- schedule at the rank as written, 0 is a run with none at all.
  local eliteAt = math.floor(tonumber(s.elites) or 100)
  if eliteAt < 0 then eliteAt = 0 end
  if eliteAt > 200 then eliteAt = 200 end
  local elitePool = LS.eliteArchs
  local reaverOn = eliteAt > 0

  for wave = 1, lvl.waves do
    local sup = budget(lvl, wave)
    local tier = tierOf(lvl, wave)
    local assault = wave % 5 == 0
    local last = wave >= lvl.waves
    local plan = { n = wave, at = at, supply = sup, tier = tier, assault = assault, last = last,
      kinds = {}, counts = {}, elites = {}, gates = assault and 2 or 1 }

    -- a third of the budget is kept back for the wave's own units, so an assault is never its
    -- elites alone
    local keep = floor(sup * 35 / 100)

    if wave >= lvl.from and eliteAt > 0 then
      local want = stWave.range(lvl.elites[1], lvl.elites[2])
      if assault then want = lvl.elites[2] end
      -- the setting scales how many ride as well as how strong each one is, and a run at
      -- all of it still sends one where the schedule asked for one
      want = math.max(1, floor(want * eliteAt / 100))
      local ready = {}
      for i = 1, #elitePool do
        if wave >= lvl.from + elitePool[i].from - 1 then ready[#ready + 1] = elitePool[i] end
      end
      if #ready == 0 then ready = { elitePool[1] } end
      local rank = LS.rankOf("elite")
      for i = 1, want do
        local pick = stWave.pick(ready)
        local cost = math.max(1, floor(pick.cost * eliteAt / 100))
        if sup - cost < keep then break end
        -- an elite is rolled like a mid-boss, on a smaller budget: its own stats inside the
        -- tier's bounds, the affixes its rank allows, a spell of its own and a rolled name.
        -- Nothing about it is the fixed elite the body came from.
        local affixes = pickAffixes(stAffix, affixPool, pick,
          rank.affixes + (eliteAt >= 150 and 1 or 0))
        made = made + 1
        local spec = buildSpell(stSpell, race, "gen_spell_" .. made, rank.spell, 2, out)
        spec.name = spellName(stName, spec)
        out.weapons[spec.id].name = spec.name
        out.techs[spec.id].name = spec.name
        local kind = buildKind(ctx, stName, stStat, race, "elite", tier, pick, affixes,
          spec, out, made, eliteAt)
        names[kind.id] = kind.name
        local row = { unit = kind.id, name = kind.name, tier = "elite", spell = spec.name,
          spellOrder = spec.id, affixes = {} }
        for k = 1, #affixes do row.affixes[k] = affixes[k].name end
        plan.elites[#plan.elites + 1] = row
        sup = sup - cost
      end
    end

    -- a mid-boss on every assault from wave 10, a boss on the last wave
    local roll = nil
    if last then roll = "boss"
    elseif assault and wave >= 10 then roll = "midboss" end
    if roll then
      local maxAff = LS.rankOf(roll).affixes
      local arch = stAffix.pick(archs)
      local picked, tries, tags = {}, 0, {}
      for i = 1, #arch.tags do tags[#tags + 1] = arch.tags[i] end
      while #picked < maxAff and tries < 16 do
        tries = tries + 1
        local a = stAffix.pick(affixPool)
        local already = false
        for i = 1, #picked do
          if picked[i].id == a.id then already = true end
        end
        if (not already) and (not clashes(tags, a.tags)) then
          picked[#picked + 1] = a
          for i = 1, #a.tags do tags[#tags + 1] = a.tags[i] end
        end
      end
      made = made + 1
      local spec = buildSpell(stSpell, race, "gen_spell_" .. made,
        LS.rankOf(roll).spell, roll == "boss" and 3 or 2, out)
      spec.name = spellName(stName, spec)
      out.weapons[spec.id].name = spec.name
      out.techs[spec.id].name = spec.name
      local kind = buildKind(ctx, stName, stStat, race, roll, tier, arch, picked, spec, out, made)
      names[kind.id] = kind.name
      local row = { unit = kind.id, name = kind.name, tier = roll, spell = spec.name,
        spellOrder = spec.id, affixes = {} }
      for i = 1, #picked do row.affixes[i] = picked[i].name end
      plan.elites[#plan.elites + 1] = row
      if roll == "boss" then
        plan.boss = kind.id
        -- the arena while the boss lives: the edge comes in four tiles and minions come
        plan.arena = { shrink = 4, minionsEvery = 20, minion = archs[1].unit }
        if reaverOn then
          plan.elites[#plan.elites + 1] = { unit = "ls_hybrid_reaver", name = "Hybrid Reaver",
            tier = "boss" }
        end
      end
      sup = math.max(keep, sup - (roll == "boss" and 30 or 22))
    end

    -- then the wave itself, bought from the archetypes of its tier and below
    local ready = {}
    for i = 1, #archs do
      if archs[i].tier <= tier then ready[#ready + 1] = archs[i] end
    end
    -- one rolled kind an archetype a wave: the first time the wave buys a Trooper it rolls
    -- what this wave's Troopers are, and buying more of them buys more of those. So a group
    -- is a group -- its units share their numbers and their effects -- and a run's records
    -- stay in the hundreds rather than one a unit.
    local kindFor = {}
    local guard = 0
    -- the wave is bought by how many walk in, not by the supply left: a wave of zerglings and a
    -- wave of ultralisks for the same supply are seven units and thirty, and a player reads a
    -- wave by its count. Half the wave's budget is its count, so it climbs wave on wave as the
    -- budget does; each elite riding with it takes two of it, never past four in ten.
    local want = math.max(4, floor(budget(lvl, wave) / 2 + 0.5))
    want = math.max(floor(want * 6 / 10 + 0.5), want - 2 * #plan.elites)
    local have = 0
    while have < want and guard < 60 do
      guard = guard + 1
      local arch = stWave.pick(ready)
      do
        local id, name, tags
        if kindFor[arch.id] then
          id, name, tags = kindFor[arch.id][1], kindFor[arch.id][2], kindFor[arch.id][3]
        else
          -- a unique group: the whole group shares one rolled set of effects, under a name
          -- and a look of its own, so a player reads it as they read an elite. It carries
          -- what its rank allows and no more, which is above the line it walks in with and
          -- below an elite.
          local uniq = #affixPool > 0 and stUniq.n(100) < uniqueAt
          local affixes
          if uniq then
            affixes = pickAffixes(stAffix, affixPool, arch, LS.rankOf("unique").affixes)
          else
            -- otherwise an affix on top of the roll, as often as the tier says
            affixes = {}
            if stAffix.n(100) < (LS.tiers[tier] or LS.tiers[1]).affix then
              local a = stAffix.pick(affixPool)
              if not clashes(arch.tags, a.tags) then affixes[1] = a end
            end
          end
          made = made + 1
          local kind = buildKind(ctx, stName, stStat, race, uniq and "unique" or "unit",
            tier, arch, affixes, nil, out, made)
          id, name, tags = kind.id, kind.name, kind.tags
          names[id] = name
          local words = nil
          if uniq then
            words = {}
            for i = 1, #affixes do words[i] = affixes[i].name end
          end
          kindFor[arch.id] = { id, name, tags, uniq, words }
        end
        -- a group of a dear archetype is smaller than one of a cheap one, so a wave mixes
        local count = math.max(1, math.min(want - have, 12, floor(want / (arch.cost * 2)) + 1))
        -- the same kind bought twice is one row with a bigger count, so a wave reads as a
        -- wave and not as a list
        local at = nil
        for i = 1, #plan.kinds do
          if plan.kinds[i].type == id then at = i end
        end
        if at and plan.counts[at] < 24 then
          plan.counts[at] = plan.counts[at] + count
        elseif at then
          count = 0  -- that kind is as thick as it may be; the budget goes to another
        else
          -- whether this wave's group of that archetype is a unique one, and the words it
          -- is read by, both off the group's own record above
          plan.kinds[#plan.kinds + 1] = { type = id, name = name, tags = tags,
            role = arch.role, unique = kindFor[arch.id][4] or nil,
            affixes = kindFor[arch.id][5] }
          plan.counts[#plan.counts + 1] = count
        end
        have = have + count
      end
    end

    -- the fairness check: a wave over a cap is drawn again from the same stream, so the run
    -- stays the same on every machine
    local redraws = 0
    while redraws < 3 do
      local broke = capBroken(plan.kinds, plan.counts, caps)
      if not broke then break end
      redraws = redraws + 1
      local cheap = nil
      for k = 1, #ready do
        if (not has(ready[k].tags, broke)) and (cheap == nil or ready[k].cost < cheap.cost) then
          cheap = ready[k]
        end
      end
      local kept, keptN = {}, {}
      local dropped = false
      for i = #plan.kinds, 1, -1 do
        if (not dropped) and has(plan.kinds[i].tags, broke) then
          dropped = true
          if cheap then
            -- the group that takes its place is rolled like any other: this wave's own
            -- kind of that archetype where it already bought one, a fresh roll where it
            -- did not. Nothing in a wave is the plain row off the table.
            if not kindFor[cheap.id] then
              made = made + 1
              local sub = buildKind(ctx, stName, stStat, race, "unit", tier, cheap, {},
                nil, out, made)
              names[sub.id] = sub.name
              kindFor[cheap.id] = { sub.id, sub.name, sub.tags }
            end
            local k = kindFor[cheap.id]
            kept[#kept + 1] = { type = k[1], name = k[2], tags = k[3],
              role = cheap.role }
            keptN[#keptN + 1] = plan.counts[i]
          end
        else
          kept[#kept + 1] = plan.kinds[i]
          keptN[#keptN + 1] = plan.counts[i]
        end
      end
      -- put them back the way round they were bought
      plan.kinds, plan.counts = {}, {}
      for i = #kept, 1, -1 do
        plan.kinds[#plan.kinds + 1] = kept[i]
        plan.counts[#plan.counts + 1] = keptN[i]
      end
    end
    plan.redraws = redraws

    waves[#waves + 1] = plan
    at = at + lvl.gap
  end

  -- the looks: one entry a look name, carrying the units looks.json already names and the
  -- generated ones that asked for it
  for i = 1, #LS.lookFixed do
    local name, units = LS.lookFixed[i][1], {}
    for k = 1, #LS.lookFixed[i][2] do units[#units + 1] = LS.lookFixed[i][2][k] end
    for k = 1, #out.lookOf do
      if out.lookOf[k][1] == name then units[#units + 1] = out.lookOf[k][2] end
    end
    out.looks[#out.looks + 1] = { name = name, units = units }
  end
  -- and the look names only the generated kinds use
  local done = {}
  for i = 1, #LS.lookFixed do done[#done + 1] = LS.lookFixed[i][1] end
  for k = 1, #out.lookOf do
    local name = out.lookOf[k][1]
    if not has(done, name) then
      done[#done + 1] = name
      local units = {}
      for j = 1, #out.lookOf do
        if out.lookOf[j][1] == name then units[#units + 1] = out.lookOf[j][2] end
      end
      out.looks[#out.looks + 1] = { name = name, units = units }
    end
  end
  out.lookOf = nil

  -- the rolls are a list indexed by the kind's number, so every number up to the count has to
  -- be there: one gap and the run would carry them as an object rather than a list, and the
  -- readout would read nothing off the units it stamped
  for i = 1, made do
    if out.rolled[i] == nil then
      error("the run rolled " .. made .. " kinds and kept no record of kind " .. i, 0)
    end
  end

  out.run = {
    seed = seed,
    difficulty = lvl.id,
    difficultyName = lvl.name,
    race = race,
    waves = lvl.waves,
    build = lvl.build,
    gap = lvl.gap,
    ai = lvl.ai,
    perimeter = s.perimeter or "standard",
    allies = s.allies_perimeter or "shared",
    push = s.push ~= false,
    preview = s.show_next_wave ~= false,
    -- the wave the run starts at, for trying a wave without playing up to it. It changes
    -- nothing rolled: the run is the same run, entered later.
    startWave = startWave,
    band = LS.BAND_TILES,
    growEvery = 5,
    growTiles = 4,
    penaltyStart = 5,
    penaltyStep = 5,
    penaltyCap = 25,
    wave = waves,
    names = names,
    kinds = made,
    -- what each generated kind rolled, a list indexed by the kind's own number (buildKind), the
    -- number the mode stamps on every unit of it
    rolled = out.rolled,
    spells = out.spellList
  }

  -- the ground: who holds which start, where the enemy base is, the perimeter and its gates.
  -- It is worked out here and recorded in the run, so a better read later never changes an old
  -- replay.
  local homes, foe = {}, nil
  for i = 1, #ctx.players do
    local p = ctx.players[i]
    if ctx.enemy and p.player == ctx.enemy.player then
      foe = { player = p.player, x = p.start.x, y = p.start.y, race = p.race }
    elseif p.kind ~= "none" then
      homes[#homes + 1] = { player = p.player, x = p.start.x, y = p.start.y, race = p.race }
    end
  end
  if foe == nil and #homes > 1 then
    -- without a slot marked, the start farthest from the first home holds the enemy base
    local far, fd = nil, -1
    for i = 2, #homes do
      local d = lineTiles(homes[1].x, homes[1].y, homes[i].x, homes[i].y)
      if d > fd then far, fd = i, d end
    end
    foe = homes[far]
    local kept = {}
    for i = 1, #homes do
      if i ~= far then kept[#kept + 1] = homes[i] end
    end
    homes = kept
  end
  out.run.homes = homes
  out.run.foe = foe

  local shared = out.run.allies == "shared" or #homes == 1
  out.run.shared = shared
  out.run.rings, out.run.gates =
    LS.ringsFor(ctx.map, homes, foe, out.run.perimeter, shared)

  out.spellList = nil
  out.rolled = nil
  return out
end
