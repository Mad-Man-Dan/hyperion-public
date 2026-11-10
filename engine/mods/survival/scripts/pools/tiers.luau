-- the tiers the waves draw their units from, and the ranks a rolled unit may hold.
--
-- This file is the run's shape. Nothing in it is a unit: it says which archetypes a wave may
-- reach as the run deepens, how far a rolled unit's stats may stray from the row it is cloned
-- from, and how much stronger each rank is than the one below it. The archetypes themselves are
-- in terran.luau, protoss.luau and zerg.luau, and each one's `tier` is its place here.
--
-- Every bound is a percentage of the base row, as a whole number, and the roll is one of them
-- over a hundred. Whole numbers because the roll must come out the same on every machine, and
-- the run's seed is what it is rolled from: two games of one seed send the same units.
--
-- Arrays only, walked by index, so the roll does not depend on table order.

LS = LS or {}

-- The ranks, weakest first. The order is the point and it is written here rather than worked out
-- anywhere: a line unit is the floor, a unique group stands a little above it, an elite above
-- that, and a mid-boss and a boss above both. Nothing may be rolled stronger than its rank.
--
--   life     what the base row's hit points and shields are multiplied by, as a percentage
--   affixes  how many of the mod's affixes it may carry at once
--   spell    what a rolled spell of its own may cost; 0 for a rank that gets none
--   name     how a sentence names it ("an elite rides this wave")
--   short    how a label names it, where there is no room for a sentence: the readout's own
--            line, where it stands before the archetype ("Elite Gorger")
--   cap      how far, as a percentage, its shields, damage, speed and cooldown may end up from
--            the base row once the rank, the roll and its affixes are all on; its life too, unless
--            cap_life says otherwise. None for a rank left uncapped. A unique group walks in with
--            the line, so it stands apart without being a wall.
--   cap_life how far, as a percentage, its life may end up from the rank's own (life above), so
--            a boss is still a boss and only what its roll and affixes stack on top is held back
--   cap_armor how many points of armour it may have above the base row's, 1 when not given
LS.ranks = {
  { id = "unit",    name = "a line unit",   short = "Line unit", life = 100, affixes = 1, spell = 0 },
  { id = "unique",  name = "a unique group", short = "Unique",   life = 135, affixes = 2, spell = 0, cap = 25 },
  { id = "elite",   name = "an elite",      short = "Elite",    life = 180, affixes = 2, spell = 10 },
  { id = "midboss", name = "a mid-boss",    short = "Mid-boss", life = 340, affixes = 3, spell = 16,
    cap = 35, cap_life = 50, cap_armor = 2 },
  { id = "boss",    name = "a boss",        short = "Boss",     life = 700, affixes = 4, spell = 26,
    cap = 50, cap_life = 75, cap_armor = 3 }
}

function LS.rankOf(id)
  for i = 1, #LS.ranks do
    if LS.ranks[i].id == id then return LS.ranks[i] end
  end
  return LS.ranks[1]
end

-- a rank's label, for a line with no room for a sentence
function LS.rankName(id)
  return LS.rankOf(id).short
end

-- The tiers, shallowest first.
--
--   from   the share of the run at which the tier opens, as a percentage of its waves. A wave
--          buys from its own tier and every one below it, so `from` 0 on the first tier means
--          the line units are there from the start.
--   stats  the bounds a rolled unit's own numbers are drawn between, as percentages of the row
--          it clones. A deeper tier strays further, so a late unit is less predictable than an
--          early one. cooldown below 100 is a faster attack, so its bounds lean the other way.
--   affix  how likely a line unit of this tier is to carry an affix at all, in hundredths.
--          A rolled unit always rolls its stats; this is only the extra effect on top.
--   born   how likely a unit of this tier is to walk in with a mark of its own, in hundredths.
--          That is the one roll made in the game rather than here (LS.bornMark).
LS.tiers = {
  { id = "vanguard", name = "Vanguard", from = 0,
    stats = { life = { 85, 115 }, shields = { 90, 115 }, damage = { 90, 110 },
              speed = { 95, 108 }, cooldown = { 95, 105 }, armor = { 0, 1 } },
    affix = 25, born = 20 },

  { id = "line", name = "Line", from = 30,
    stats = { life = { 90, 125 }, shields = { 92, 125 }, damage = { 92, 118 },
              speed = { 95, 112 }, cooldown = { 92, 105 }, armor = { 0, 2 } },
    affix = 35, born = 28 },

  { id = "spearhead", name = "Spearhead", from = 65,
    stats = { life = { 95, 140 }, shields = { 95, 135 }, damage = { 95, 128 },
              speed = { 96, 115 }, cooldown = { 90, 104 }, armor = { 0, 3 } },
    affix = 50, born = 36 }
}

-- the marks a unit may be born with, as statuses of the mod (types.json). Pairs: one for the
-- unit and one against it, so a wave's strength stays where its groups' rolls put it and the
-- odd unit still stands a little apart. Arrays only, picked at even odds.
LS.born = { "ls_born_tough", "ls_born_frail", "ls_born_quick", "ls_born_slow",
            "ls_born_savage", "ls_born_blunt" }

-- the mark a unit of this tier walks in with, or nil for none. This is the one roll the run
-- does not make before the first frame: it is drawn as the unit is made, from hx.random, which
-- is the game's own draw -- the save carries it, the state hash covers it, and every machine and
-- the replay draw the same marks in the same order. hx.random is refused before a game, which
-- is why everything else is rolled from the run's seed and this is not.
function LS.bornMark(tier)
  local t = LS.tiers[tier] or LS.tiers[1]
  local at = t.born or 0
  if at <= 0 or #LS.born == 0 then return nil end
  if hx.random(100) > at then return nil end
  return LS.born[hx.random(#LS.born)]
end

-- the tier a wave of a run buys from: the deepest one the wave has reached
function LS.tierAt(lvl, wave)
  local n = math.max(1, lvl.waves - 1)
  local at = math.floor((wave - 1) * 100 / n)
  local tier = 1
  for i = 1, #LS.tiers do
    if at >= LS.tiers[i].from then tier = i end
  end
  return tier
end

-- the bounds a tier names, in the order the generator reads them
LS.statNames = { "life", "shields", "damage", "speed", "cooldown", "armor" }

-- the pools whose archetypes are checked below
local kRaces = { "zerg", "protoss", "terran" }

local function pairFault(name, b)
  if type(b) ~= "table" or #b ~= 2 or type(b[1]) ~= "number" or type(b[2]) ~= "number" then
    return name .. " is no pair of numbers"
  end
  if b[1] > b[2] then
    return name .. " runs from " .. b[1] .. " down to " .. b[2] .. ", the wrong way round"
  end
  return nil
end

-- What is wrong with the tables above, as a sentence, or nil when nothing is: the ranks out of
-- order, a bound written the wrong way round, a tier opening outside the run, or an archetype
-- naming a tier that is not there. generate.luau asks before it rolls anything and refuses the
-- run with the answer, so a mistake in the data says what it is in hyperion_mods.log instead of
-- coming out as a strange run or an error from somewhere further in.
function LS.dataFault()
  -- the ranks, weakest first: nothing stands below the rank under it
  for i = 1, #LS.ranks do
    local r = LS.ranks[i]
    if type(r.id) ~= "string" or r.id == "" then return "rank " .. i .. " has no id" end
    if type(r.short) ~= "string" or r.short == "" then
      return "rank " .. r.id .. " has no short name, which a label has no room to do without"
    end
    local under = LS.ranks[i - 1]
    if under and (r.life < under.life or r.affixes < under.affixes or r.spell < under.spell) then
      return "rank " .. r.id .. " is weaker than " .. under.id .. ", and the ranks go weakest first"
    end
  end

  -- the tiers, shallowest first, each opening at a share of the run
  for i = 1, #LS.tiers do
    local t = LS.tiers[i]
    if type(t.id) ~= "string" or t.id == "" then return "tier " .. i .. " has no id" end
    if type(t.from) ~= "number" or t.from < 0 or t.from > 100 then
      return "tier " .. t.id .. " opens at " .. tostring(t.from) .. ", not a share of the run from 0 to 100"
    end
    local above = LS.tiers[i - 1]
    if above and t.from < above.from then
      return "tier " .. t.id .. " opens before " .. above.id .. ", and the tiers go shallowest first"
    end
    if type(t.affix) ~= "number" or t.affix < 0 or t.affix > 100 then
      return "tier " .. t.id .. "'s affix chance is " .. tostring(t.affix) .. ", not 0 to 100"
    end
    if type(t.born) ~= "number" or t.born < 0 or t.born > 100 then
      return "tier " .. t.id .. "'s born chance is " .. tostring(t.born) .. ", not 0 to 100"
    end
    for j = 1, #LS.statNames do
      local fault = pairFault("tier " .. t.id .. "'s " .. LS.statNames[j],
                              (t.stats or {})[LS.statNames[j]])
      if fault then return fault end
    end
  end
  if LS.tiers[1] and LS.tiers[1].from ~= 0 then
    return "the first tier opens at " .. LS.tiers[1].from .. ", so the first wave has nothing to buy"
  end

  -- the marks are status names; an empty list is no fault, it only means no unit is born marked
  for i = 1, #LS.born do
    if type(LS.born[i]) ~= "string" or LS.born[i] == "" then
      return "the mark at " .. i .. " of LS.born is no status name"
    end
  end

  -- every archetype's tier is a place in the list above
  for i = 1, #kRaces do
    local pool = (LS.archetypes or {})[kRaces[i]]
    for j = 1, #(pool or {}) do
      local a = pool[j]
      if type(a.tier) ~= "number" or a.tier < 1 or a.tier > #LS.tiers or
         a.tier ~= math.floor(a.tier) then
        return kRaces[i] .. "'s " .. tostring(a.id) .. " is tier " .. tostring(a.tier) ..
               ", and there are " .. #LS.tiers .. " tiers"
      end
    end
  end
  return nil
end
