-- the parts a generated spell is put together from, Noita's way: one carrier, one payload and up
-- to three modifiers. The generator turns a pick into an order's weapon and its blocks
-- (scripts/generate.luau, LsBuildSpell), so a rolled spell is the same kind of thing as one
-- written by hand and can be opened in the studio.
--
-- A carrier says where the cast lands and how it is paced. A payload says what happens where it
-- lands. A modifier changes the numbers, or, where the blocks cannot say it, adds a script block
-- naming one of the helpers in scripts/spells.luau.

LS = LS or {}
LS.parts = LS.parts or {}

-- at: "target" (behavior 4, aimed) or "caster" (behavior 5, where it stands)
-- pulses and gap: how a repeat paces it. radius: pixels. range: pixels, 0 for the map's own.
-- sprite: what the cast shows where it lands, and the generator shows it on its own, with no
-- shot behind it. So it is never a shot's own picture: a shot does its damage in its animation,
-- and a picture shown alone would ask for that damage with nothing to take it from. The thingy
-- pictures are what to reach for; a weapon's name is the warning sign, though not the test --
-- the dark swarm shares its name with a weapon and is a thingy, the psionic shockwave does not
-- and damages twice over.
LS.parts.carriers = {
  { id = "bolt", name = "bolt", weight = 12, cost = 3, tags = { "single", "ranged" },
    at = "target", range = 224, radius = 24, pulses = 1, gap = 0, cooldown = 30,
    sprite = "thingy_dbl_exp", line = "one shot at what it was aimed at" },
  { id = "lance", name = "lance", weight = 8, cost = 4, tags = { "single", "long_range" },
    at = "target", range = 352, radius = 32, pulses = 1, gap = 0, cooldown = 40,
    sprite = "thingy_spotrail", line = "a long shot from out of the line's reach" },
  { id = "nova", name = "nova", weight = 10, cost = 4, tags = { "area", "self_centred" },
    at = "caster", range = 0, radius = 112, pulses = 1, gap = 0, cooldown = 44,
    sprite = "thingy_maelhit", line = "a ring round the caster" },
  { id = "beam", name = "beam", weight = 7, cost = 5, tags = { "line", "ranged" },
    at = "target", range = 288, radius = 40, pulses = 3, gap = 0.25, cooldown = 46,
    sprite = "thingy_plasma", line = "three quick strikes down one line" },
  { id = "storm", name = "storm", weight = 8, cost = 6, tags = { "area", "lingering" },
    at = "target", range = 256, radius = 96, pulses = 5, gap = 0.75, cooldown = 56,
    sprite = "psionic_storm", line = "a patch of ground held for four seconds" },
  { id = "mine", name = "mine", weight = 6, cost = 4, tags = { "area", "delayed" },
    at = "target", range = 192, radius = 80, pulses = 1, gap = 2.5, cooldown = 44,
    sprite = "thingy_bdust", line = "it waits two and a half seconds, then goes off" },
  { id = "lob", name = "lob", weight = 7, cost = 5, tags = { "area", "artillery", "long_range" },
    at = "target", range = 384, radius = 72, pulses = 3, gap = 1, cooldown = 60,
    sprite = "thingy_explo2", line = "three shells walked over the same ground" },
  { id = "summon", name = "summon", weight = 6, cost = 6, tags = { "spawn", "summoner" },
    at = "target", range = 224, radius = 48, pulses = 1, gap = 0, cooldown = 56,
    sprite = "thingy_recall", line = "something arrives where it was aimed" }
}

-- damage is a share of the spell's budget; status is one of types.json's. A payload with
-- spawnOf names which side's unit the summon carrier makes.
LS.parts.payloads = {
  { id = "fire", name = "fire", weight = 11, cost = 3, tags = { "fire", "damage", "dot" },
    damage = 18, damage_type = "explosive", status = { terran = "t_scorched", zerg = "z_bile_rot",
      protoss = "p_mind_burned" }, look = "ls-t-ember", namePre = "Searing",
    line = "it burns what it touches" },
  { id = "acid", name = "acid", weight = 10, cost = 3, tags = { "acid", "damage",
    "armour_break" }, damage = 14, damage_type = "normal",
    status = { terran = "t_marked", zerg = "z_corroded", protoss = "p_rent" },
    look = "ls-z-acid", namePre = "Corroding", line = "it eats armour" },
  { id = "void", name = "void", weight = 8, cost = 5, tags = { "void", "damage", "pierce" },
    damage = 26, damage_type = "independent",
    status = { terran = "t_marked", zerg = "z_corroded", protoss = "p_void_marked" },
    look = "ls-p-void", namePost = "of the Void", line = "damage nothing reduces" },
  { id = "shock", name = "shock", weight = 9, cost = 4, tags = { "emp", "damage", "cc" },
    damage = 12, damage_type = "concussive",
    status = { terran = "t_jammed", zerg = "z_hobbled", protoss = "p_warp_lag" },
    look = "ls-t-arc", namePre = "Shocking", line = "it slows what a unit can do" },
  { id = "root", name = "root", weight = 7, cost = 5, tags = { "cc", "root" },
    damage = 6, damage_type = "normal",
    status = { terran = "t_clamped", zerg = "z_rooted", protoss = "p_rifted" },
    look = "ls-z-tendril", namePost = "the Binder", line = "what it reaches cannot move" },
  { id = "silence", name = "silence", weight = 6, cost = 5, tags = { "cc", "silence" },
    damage = 8, damage_type = "normal",
    status = { terran = "t_blinded", zerg = "z_molting", protoss = "p_nulled" },
    look = "ls-p-null", namePost = "the Null", line = "what it reaches will not fire" },
  { id = "slow", name = "slow", weight = 9, cost = 3, tags = { "cc", "slow" },
    damage = 8, damage_type = "normal",
    status = { terran = "t_slagged", zerg = "z_spored", protoss = "p_dilated" },
    look = "ls-p-dilation", namePre = "Dragging", line = "it holds a line back" },
  { id = "drain", name = "drain", weight = 6, cost = 4, tags = { "energy", "damage" },
    damage = 16, damage_type = "independent", energy = -60,
    status = { terran = "t_jammed", zerg = "z_bile_rot", protoss = "p_mind_burned" },
    look = "ls-p-psi", namePost = "the Drinker", line = "it takes a caster's energy with it" },
  { id = "pull", name = "pull", weight = 5, cost = 5, tags = { "cc", "pull", "void" },
    damage = 10, damage_type = "normal",
    status = { terran = "t_clamped", zerg = "z_rooted", protoss = "p_tethered" },
    script = "LsSpellPull", look = "ls-p-tether", namePost = "the Tether",
    line = "it drags ground units in" },
  { id = "brood", name = "brood", weight = 7, cost = 6, tags = { "spawn", "summoner" },
    damage = 0, spawn = { terran = "ls_sentry_drone", zerg = "zerg_broodling",
      protoss = "ls_warp_echo" }, count = 3, seconds = 25,
    look = "ls-z-brood", namePost = "the Mother", line = "it leaves something behind" }
}

-- each modifier gives a factor on the spell's cost, so three of them make a wild spell dear.
-- pulsesAdd, radiusX, damageX and gapX change the blocks' numbers; script names a helper for
-- what the blocks cannot say.
LS.parts.modifiers = {
  { id = "multicast", name = "multicast", weight = 10, costX = 1.8, tags = { "more" },
    pulsesAdd = 2, damageX = 0.75, gapX = 0.5, namePre = "Threefold",
    line = "it goes off three times instead of one, each a little softer" },
  { id = "wider", name = "wider", weight = 11, costX = 1.4, tags = { "area" },
    radiusX = 1.6, damageX = 0.85, namePre = "Broad",
    line = "half again the ground, a little less on each unit" },
  { id = "heavier", name = "heavier", weight = 11, costX = 1.5, tags = { "damage" },
    damageX = 1.7, radiusX = 0.8, namePre = "Heavy",
    line = "much more on each unit, over less ground" },
  { id = "lingering", name = "lingering", weight = 8, costX = 1.6, tags = { "lingering" },
    pulsesAdd = 3, gapX = 1.5, damageX = 0.6, namePost = "the Long Hour",
    line = "it holds the ground for twice as long" },
  { id = "delayed", name = "delayed", weight = 7, costX = 1.2, tags = { "delayed" },
    delay = 1.5, damageX = 1.25, namePost = "the Late Hour",
    line = "a second and a half late, and harder for the wait" },
  { id = "chaining", name = "chaining", weight = 8, costX = 1.7, tags = { "more", "chain" },
    script = "LsSpellChain", chainHops = 3, chainShare = 70, namePost = "the Chain",
    line = "it jumps to three more units, each hop weaker" },
  { id = "splitting", name = "splitting", weight = 8, costX = 1.6, tags = { "more", "split" },
    script = "LsSpellSplit", splitInto = 2, splitShare = 50, namePre = "Splitting",
    line = "it splinters in two on the first thing it hits" },
  { id = "homing", name = "homing", weight = 7, costX = 1.3, tags = { "accurate" },
    script = "LsSpellHoming", namePre = "Seeking",
    line = "it finds the nearest unit of the place it was aimed at" },
  { id = "bouncing", name = "bouncing", weight = 6, costX = 1.5, tags = { "more", "chain" },
    script = "LsSpellBounce", bounceTiles = 5, bounceShare = 60, namePost = "the Rebound",
    line = "it goes off a second time five tiles on" },
  { id = "ondeath", name = "on death", weight = 6, costX = 1.4, tags = { "ondeath" },
    script = "LsSpellOnDeath", namePost = "the Last Word",
    line = "whatever it kills goes off again where it stood" }
}

-- parts only one race rolls, added to the shared list for that race
LS.parts.byRace = {
  terran = {
    carriers = {
      { id = "barrage", name = "barrage", weight = 6, cost = 6, tags = { "area", "artillery",
        "long_range" }, at = "target", range = 448, radius = 64, pulses = 4, gap = 0.75,
        cooldown = 64, sprite = "thingy_explo2", line = "four shells, walked in" }
    },
    payloads = {
      { id = "incendiary", name = "incendiary", weight = 7, cost = 4, tags = { "fire", "damage",
        "dot", "ground" }, damage = 14, damage_type = "explosive", status = { terran = "t_slagged",
        zerg = "t_slagged", protoss = "t_slagged" }, look = "ls-t-thermite",
        namePre = "Thermal", line = "the ground stays hot after it" }
    },
    modifiers = {
      { id = "ranging", name = "ranging", weight = 7, costX = 1.25, tags = { "long_range" },
        rangeAdd = 96, namePost = "the Ranger", line = "three tiles more reach" }
    }
  },
  zerg = {
    carriers = {
      { id = "burrowcast", name = "burrow", weight = 6, cost = 5, tags = { "area", "self_centred",
        "burrow" }, at = "caster", range = 0, radius = 88, pulses = 1, gap = 0, cooldown = 44,
        sprite = "thingy_zdvhit", teleport = true,
        line = "it goes under and comes up where it was aimed" }
    },
    payloads = {
      { id = "parasite", name = "parasite", weight = 7, cost = 4, tags = { "dot", "spawn" },
        damage = 10, damage_type = "normal", status = { terran = "z_brood_touched",
        zerg = "z_brood_touched", protoss = "z_brood_touched" }, look = "ls-z-brood",
        namePost = "the Seeder", line = "it leaves a thing growing in the wound" }
    },
    modifiers = {
      { id = "feeding", name = "feeding", weight = 7, costX = 1.35, tags = { "lifesteal" },
        script = "LsSpellFeed", feedShare = 50, namePost = "the Gorged",
        line = "half of what it does comes back as the caster's life" }
    }
  },
  protoss = {
    carriers = {
      { id = "warpstep", name = "warp step", weight = 6, cost = 5, tags = { "single", "blink" },
        at = "target", range = 320, radius = 72, pulses = 1, gap = 0, cooldown = 36,
        sprite = "thingy_recall", teleport = true,
        line = "the caster arrives with the spell" }
    },
    payloads = {
      { id = "unmaking", name = "unmaking", weight = 6, cost = 6, tags = { "void", "damage",
        "shields" }, damage = 30, damage_type = "independent",
        status = { terran = "t_marked", zerg = "z_corroded", protoss = "p_rent" },
        script = "LsSpellUnmakeShields", look = "ls-p-void", namePre = "Unmaking",
        line = "shields first, and they do not come back soon" }
    },
    modifiers = {
      { id = "prismatic", name = "prismatic", weight = 6, costX = 1.55, tags = { "more", "area" },
        pulsesAdd = 1, radiusX = 1.3, damageX = 0.9, namePre = "Prismatic",
        line = "it breaks into two wider rings" }
    }
  }
}

-- words for a spell's own name, built with the parts' name pieces
LS.names.spells = {
  head = { "Fan", "Lash", "Volley", "Bloom", "Rend", "Knell", "Tide", "Thresh", "Arc", "Cascade" },
  tail = { "of Ash", "of the Deep", "of Nine Hours", "of the Turning", "of Still Air" }
}

-- the fairness rules the roll is checked against. tags that may not meet on one unit, and the
-- most of a tag a wave may hold at each difficulty.
LS.rules = {
  exclude = {
    { "cloaked", "long_range" },
    { "cloaked", "artillery" },
    { "suicide", "tanky" },
    { "no_attack", "assassin" },
    { "summoner", "summoner" },
    { "root", "pull" },
    { "silence", "root" }
  },
  caps = {
    -- the most units in one wave carrying a tag, by difficulty
    recruit  = { cc = 1, cloaked = 1, summoner = 1, artillery = 1, long_range = 2 },
    veteran  = { cc = 1, cloaked = 2, summoner = 1, artillery = 2, long_range = 3 },
    hardened = { cc = 2, cloaked = 2, summoner = 2, artillery = 2, long_range = 4 },
    brutal   = { cc = 3, cloaked = 3, summoner = 2, artillery = 3, long_range = 5 },
    nightmare = { cc = 4, cloaked = 4, summoner = 3, artillery = 4, long_range = 6 },
    insane   = { cc = 6, cloaked = 5, summoner = 4, artillery = 5, long_range = 8 }
  },
  -- how many affixes each rank carries is with the ranks themselves, in tiers.luau,
  -- beside the life each one is worth: one list rather than two that must agree.
  -- a boss may not hold two parts with these tags
  bossExclude = { { "summoner", "summoner" }, { "silence", "silence" } }
}
