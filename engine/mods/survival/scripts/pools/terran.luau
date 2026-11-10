-- the Terran pool: the archetypes the generator buys units from, and the affixes it hangs on
-- them. Every number here is a factor on the cloned row (life, damage, speed) or an add
-- (armor, range), applied by scripts/generate.luau. Looks are names in looks.json.
--
-- Arrays only: the generator walks them by index, so the roll does not depend on table order.

LS = LS or {}
LS.pools = LS.pools or {}
LS.archetypes = LS.archetypes or {}
LS.affixes = LS.affixes or {}
LS.names = LS.names or {}

-- role is which brain the unit gets (scripts/ai/brains.luau). cost is supply out of the wave's
-- budget. tags are what the fairness rules read.
LS.archetypes.terran = {
  { id = "trooper", name = "Trooper", unit = "terran_marine", role = "ranged", tier = 1,
    weight = 12, cost = 1, tags = { "ground", "ranged", "cheap", "anti_air" },
    look = nil, line = "a line unit that shoots at both heights" },
  { id = "burner", name = "Burner", unit = "terran_firebat", role = "bruiser", tier = 1,
    weight = 8, cost = 2, tags = { "ground", "melee", "splash", "fire" },
    look = "ls-t-ember", line = "walks in and burns what is packed together" },
  { id = "raider", name = "Raider", unit = "terran_vulture", role = "assassin", tier = 1,
    weight = 7, cost = 2, tags = { "ground", "fast", "ranged" },
    look = nil, line = "fast, and goes round the line for the workers" },
  { id = "walker", name = "Walker", unit = "terran_goliath", role = "ranged", tier = 2,
    weight = 7, cost = 2, tags = { "ground", "ranged", "mechanical", "anti_air" },
    look = "ls-t-ironclad", line = "holds the middle and answers air" },
  { id = "breacher", name = "Breacher", unit = "terran_siege_tank_tank_mode", role = "artillery",
    tier = 2, weight = 5, cost = 3, tags = { "ground", "artillery", "mechanical", "long_range",
    "slow" }, look = "ls-t-ironclad", line = "stands off and shells the line" },
  { id = "corpsman", name = "Corpsman", unit = "terran_medic", role = "support", tier = 2,
    weight = 5, cost = 2, tags = { "ground", "support", "heal", "no_attack" },
    look = "ls-t-weldglow", line = "keeps the wave on its feet" },
  { id = "stalker", name = "Stalker", unit = "terran_ghost", role = "assassin", tier = 2,
    weight = 4, cost = 2, tags = { "ground", "ranged", "cloaked", "assassin" },
    look = nil, line = "unseen until it fires, and it fires at casters" },
  { id = "skyhook", name = "Skyhook", unit = "terran_wraith", role = "ranged", tier = 3,
    weight = 4, cost = 2, tags = { "air", "ranged", "mechanical", "anti_air", "cloaked" },
    look = nil, line = "comes over the wall rather than through it" },
  { id = "tender", name = "Tender", unit = "terran_science_vessel", role = "support", tier = 3,
    weight = 3, cost = 3, tags = { "air", "support", "caster", "detector", "no_attack" },
    look = "ls-t-drone-lume", line = "sees the cloaked and carries the wave's spells" },
  { id = "siren", name = "Siren", unit = "terran_valkyrie", role = "ranged", tier = 3,
    weight = 3, cost = 3, tags = { "air", "ranged", "splash", "anti_air", "mechanical" },
    look = nil, line = "clears massed air and little else" },
  { id = "warden", name = "Warden", unit = "terran_battlecruiser", role = "bruiser", tier = 3,
    weight = 2, cost = 6, tags = { "air", "ranged", "heavy", "mechanical", "slow" },
    look = "ls-t-ironclad", line = "the ceiling of a late Terran wave" },
  { id = "handler", name = "Handler", unit = "terran_scv", role = "summoner", tier = 2,
    weight = 2, cost = 1, tags = { "ground", "summoner", "mechanical", "cheap" },
    look = "ls-t-drone-lume", line = "makes drones and mends the machines" }
}

-- an affix's unit block is read as: life, damage, speed and cooldown are factors, armor,
-- sight and range are adds. aura puts a status on what is near, weapon on what it hits,
-- onHit and onDeath name Luau functions the engine calls for the unit (a unit's "scripts").
LS.affixes.terran = {
  { id = "searing", name = "Searing", weight = 10, cost = 3, tags = { "fire", "dot" },
    look = "ls-t-ember", namePre = "Searing",
    unit = { life = 1.15 }, weapon = { status = "t_scorched" },
    line = "what it hits burns for four seconds" },
  { id = "slagging", name = "Slagging", weight = 9, cost = 3, tags = { "fire", "slow", "cc" },
    look = "ls-t-slag", namePre = "Slagging",
    unit = { life = 1.2, speed = 0.9 }, weapon = { status = "t_slagged" },
    line = "its hits leave the ground's heat in a unit: slower, and burning a little" },
  { id = "jamming", name = "Jamming", weight = 7, cost = 4, tags = { "emp", "cc" },
    look = "ls-t-arc", namePost = "the Jammer",
    unit = { life = 1.1 }, weapon = { status = "t_jammed" },
    line = "what it hits fires half as often for five seconds" },
  { id = "ironclad", name = "Ironclad", weight = 10, cost = 3, tags = { "armour", "tanky" },
    look = "ls-t-ironclad", namePre = "Ironclad",
    unit = { life = 1.45, armor = 3, speed = 0.9 },
    line = "half again the life and three more armour, a little slower" },
  { id = "warded", name = "Warded", weight = 8, cost = 4, tags = { "aura", "support", "armour" },
    look = "ls-t-anchorplate", namePost = "the Warden",
    unit = { life = 1.2 },
    aura = { status = "t_braced", radius = 160, targets = "allies", every = 2 },
    line = "everything round it has three more armour" },
  { id = "mending", name = "Mending", weight = 7, cost = 4, tags = { "aura", "support", "heal" },
    look = "ls-t-weldglow", namePost = "the Mender",
    unit = { life = 1.15 },
    aura = { status = "t_mending", radius = 160, targets = "allies", every = 2 },
    line = "it mends the wave round it, eight life a second" },
  { id = "overclocked", name = "Overclocked", weight = 8, cost = 4, tags = { "haste", "damage" },
    look = "ls-t-overclock", namePre = "Overclocked",
    unit = { cooldown = 0.7, speed = 1.2, life = 0.9 },
    line = "it fires and walks faster, and has less life for it" },
  { id = "droning", name = "Droning", weight = 6, cost = 5, tags = { "summoner", "drones" },
    look = "ls-t-drone-lume", namePost = "the Handler",
    unit = { life = 1.1 }, spell = "ls_t_sentry_drone",
    line = "it sends out sentry drones that see and sting" },
  { id = "ranging", name = "Ranging", weight = 6, cost = 4, tags = { "long_range", "artillery" },
    look = "ls-t-rangefinder", namePost = "the Ranger",
    unit = { range = 2, life = 0.95 },
    line = "two tiles more reach, and a little thinner for it" },
  { id = "veiled", name = "Veiled", weight = 5, cost = 4, tags = { "defensive", "aura" },
    look = "ls-t-smokeveil", namePre = "Veiled",
    unit = { life = 1.1 },
    aura = { status = "t_veiled", radius = 128, targets = "allies", every = 2 },
    line = "a haze round it: two more armour and a step more speed for the wave" },
  { id = "thermal", name = "Thermal", weight = 5, cost = 5, tags = { "fire", "splash", "damage" },
    look = "ls-t-thermite", namePre = "Thermal",
    unit = { damage = 1.25 }, spell = "ls_t_thermite_spray",
    line = "it sprays thermite over everything in front of it" },
  { id = "anchoring", name = "Anchoring", weight = 4, cost = 4, tags = { "artillery", "slow",
    "tanky" }, look = "ls-t-anchorplate", namePost = "the Anchor",
    unit = { life = 1.3, speed = 0.75 }, spell = "ls_t_siege_anchor",
    line = "it digs in: it will not move, and it hits half again as hard" }
}

-- words a generated Terran name is built from, beside the affixes' own parts
LS.names.terran = {
  pre = { "Iron", "Ash", "Cinder", "Bulwark", "Vector", "Hollow", "Tungsten", "Blackline" },
  core = { "Marshal", "Sergeant", "Breaker", "Lancer", "Warden", "Reclaimer", "Hound", "Sentinel" },
  post = { "of the Furnace", "of Red Dust", "of the Long Line", "of the Ninth", "of Cold Iron" }
}
