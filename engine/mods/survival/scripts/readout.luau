-- what the readout shows of the unit in front of you.
--
-- readout.json keeps classic's panel and changes the parts this fills in: the rolled name in the
-- rank's colour, the rank's pips, the line that says what the unit is, a line for every status on
-- it with its time left, and how far each of its numbers has moved from the row its kind clones.
-- None of it can be worked out from the unit: the record the game holds is already summed, so the
-- run's own notes (hx.run.rolled, scripts/rolled.luau) are where it comes from.
--
-- The engine calls this for a unit the viewer sees, when the selection changes and when the
-- unit's own values do. It runs in a state that can only read the game, so an enemy's rolls read
-- the same for a player, a watcher and a replay, and nothing here can change what is played.

-- the marks a unit is born with, which the rank's line names rather than counting among the
-- statuses: they last the whole run, so there is nothing to count down
local BORN = {}
for i = 1, #LS.born do BORN[LS.born[i]] = true end

-- the ranks readout.json gives a colour. A line unit is left without one, so its name keeps
-- classic's colour and the effects that ask for a rank pass it by.
local RANKS = { unique = true, elite = true, midboss = true, boss = true }

-- the six stats the readout shows a change for, with the words their tooltip opens on. The
-- armour, the shields and the damage have icons of their own; the cooldown is read on the
-- weapon's icon, the life on the wireframe and the speed on the name, which carries the whole
-- roll. Five are percentages of the base row, armour is points; a cooldown under a hundred is a
-- faster attack, so its change is negative when the unit is quicker, which the readout knows to
-- read as good news.
local TIPS = {
  { stat = "life", title = "Life" },
  { stat = "shields", title = "Shields" },
  { stat = "damage", title = "Damage" },
  { stat = "cooldown", title = "Attack cooldown" },
  { stat = "speed", title = "Move speed" },
  { stat = "armor", title = "Armour", points = true }
}

local function whole(n)
  return math.floor(tonumber(n) or 0)
end

-- what the run wrote down beside a number, in a player's words
local function source(name)
  if name == "rank" then return "its rank" end
  if name == "roll" then return "its roll" end
  if name == "cap" then return "its rank's limit" end
  return name
end

-- the change against the base row, by stat, for the icons. A stat that did not move is left out
-- so its icon says nothing.
local function deltasOf(roll)
  local st = roll.stats
  local out = {}
  if st == nil then return out end
  if st.life and st.life ~= 100 then out.life = whole(st.life) - 100 end
  -- a kind that clones a row with no shields rolls the number anyway and it comes to nothing;
  -- the icon is only there for a unit that has them
  if st.shields and st.shields ~= 100 then out.shields = whole(st.shields) - 100 end
  if st.damage and st.damage ~= 100 then out.damage = whole(st.damage) - 100 end
  if st.cooldown and st.cooldown ~= 100 then out.cooldown = whole(st.cooldown) - 100 end
  if st.speed and st.speed ~= 100 then out.speed = whole(st.speed) - 100 end
  if st.armor and st.armor ~= 0 then out.armor = whole(st.armor) end
  return out
end

-- a stat's tooltip: what it comes to, then every part that moved it, in the order they were
-- taken -- the rank, the roll, then each mark by name. A stat the roll left where it was has no
-- tooltip, since its icon shows no change either, and a part that moved nothing is left out.
local function tipsOf(roll, deltas)
  local out = {}
  for i = 1, #TIPS do
    local t = TIPS[i]
    local total = roll.stats and roll.stats[t.stat]
    if total ~= nil and deltas[t.stat] ~= nil then
      local lines = {}
      if t.points then
        lines[1] = string.format("%s %+d on its base", t.title, whole(total))
      else
        lines[1] = string.format("%s %d%% of its base", t.title, whole(total))
      end
      local from = roll.from and roll.from[t.stat]
      for k = 1, (from and #from or 0) do
        local one, by = from[k], whole(from[k][2])
        if t.points and by ~= 0 then
          lines[#lines + 1] = string.format("%s %+d", source(one[1]), by)
        elseif not t.points and by ~= 100 then
          lines[#lines + 1] = string.format("%s %+d%%", source(one[1]), by - 100)
        end
      end
      out[t.stat] = lines
    end
  end
  return out
end

-- the rank's own line: what the unit is, with the rank in front of it and the mark it was born
-- with after. A line unit is named by its archetype alone, since every wave is full of them.
local function rankLine(roll, trait)
  local text = roll.arch or ""
  if RANKS[roll.rank] then
    local rank = LS.rankName(roll.rank)
    text = (text == "" and rank) or (rank .. " " .. text)
  end
  if trait ~= nil and text ~= "" then
    text = text .. " - " .. trait
  elseif trait ~= nil then
    text = trait
  end
  return text
end

function readout(unit)
  local roll = LS.rolled(unit)
  local on = hx.statuses(unit)
  local trait, lines = nil, {}
  for i = 1, #on do
    if BORN[on[i].id] then
      trait = on[i].name
    else
      -- the status's name, its side, its time left and its stacks come from its own record, and
      -- the line counts down between calls
      lines[#lines + 1] = { status = on[i].id }
    end
  end
  if roll == nil then
    -- a unit no run rolled: a player's own, the enemy base's buildings, anything a spell made.
    -- Its statuses are still worth the room -- a marine knows how long it is spored -- and the
    -- rest of the panel stays classic's.
    if #lines == 0 then return nil end
    return { lines = lines }
  end
  local text = rankLine(roll, trait)
  if text ~= "" then table.insert(lines, 1, text) end
  local deltas = deltasOf(roll)
  return {
    name = roll.name,
    rank = RANKS[roll.rank] and roll.rank or nil,
    lines = lines,
    deltas = deltas,
    tooltips = tipsOf(roll, deltas)
  }
end
