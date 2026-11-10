-- what the unit in front of you rolled.
--
-- Every kind a run rolls is a unit type of its own, named after its number ("gen_7_spitter"),
-- and generate.luau writes the rolls into the run as a list indexed by that number
-- (hx.run.rolled). The mode stamps the number on every unit it makes of the kind, as it makes
-- it ("ls_roll" in mode.luau's put, carried to the halves a Splitting unit leaves behind), so
-- reading a unit's roll is one value off the unit and one step into the list.
--
-- It is kept on the unit rather than in a map of our own because a script can keep nothing
-- between calls: its own locals cannot be written from inside a call, and in a game every table
-- a script can reach is read only. The unit's values are the engine's, so they go into a save
-- and a snapshot and come back with it.
--
-- Read only, and nothing here is rolled: it reads what the run already decided, so two players
-- watching the same unit read the same thing and a replay reads it again.

LS = LS or {}

-- the unit's roll, or nil for a unit no run rolled: a player's own, the enemy base's buildings,
-- and anything a spell made.
--
--   name     the name it was rolled with ("Vile Spitter"), which the readout shows
--   rank     "unit", "unique", "elite", "midboss" or "boss" (LS.ranks)
--   arch     what it is, by the archetype's name ("Spitter")
--   role     which brain it plays with ("ranged")
--   tier     where in the run it was rolled (LS.tiers)
--   affixes  what it carries, each a name and the line the mod gives it
--   spell    the rolled spell's name, nil for a kind that casts none
--   stats    life, shields, damage, speed, cooldown as percentages of the row it clones, and
--            armour in points. A cooldown under 100 is a faster attack
--   from     where each of those came from, in order: the rank, the roll, then each affix by
--            name, so a player can be shown why a number is what it is
function LS.rolled(unit)
  local n = hx.get(unit, "ls_roll")
  if n == nil then return nil end
  local rolled = hx.run and hx.run.rolled
  if rolled == nil then return nil end
  return rolled[n]
end

-- the rank's label and the run's own words for a roll, for a line with no room for a sentence
function LS.rollLine(roll)
  if roll == nil then return nil end
  local line = LS.rankName(roll.rank)
  if roll.arch then line = line .. " " .. roll.arch end
  return line
end
