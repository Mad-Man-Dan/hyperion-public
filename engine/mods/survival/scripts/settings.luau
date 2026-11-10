-- what the Game Mode tab says under each setting, and what a setting works out to on the map
-- the lobby has chosen. mod.json's settings name these.
--
-- effect(v, values) is the sentence under the control; read(v, map) is what the map read gives
-- for the choice, before it is taken; shown(values) greys a setting another has made
-- meaningless; preview(values, map) gives the shapes drawn over the map beside the settings.
-- They run in the set-up, not in a game, so they read the map and nothing else.

LS = LS or {}

local LINES = {
  recruit = "Ten waves, two minutes apart, and elites only at the end. Room to learn the mode.",
  veteran = "Fifteen waves. A game a good player holds with one base and a tight line.",
  hardened = "Twenty waves, about forty minutes, and the perimeter has to grow.",
  brutal = "Twenty-five waves, and they overlap: a wave cleared late is fought on top of the next.",
  nightmare = "Thirty waves, a minute each, elites from the second, and no quiet to expand in.",
  insane = "Forty waves, the first already elite. Co-op, or a bracket."
}

local AI_NAME = { very_easy = "Very Easy", easy = "Easy", medium = "Medium", hard = "Hard",
  very_hard = "Very Hard", elite = "Elite" }

function LsEffectDifficulty(v, values)
  local lvl = LS.levelOf(v)
  local line = LINES[v] or ""
  return line .. " The enemy base plays at " .. (AI_NAME[lvl.ai] or lvl.ai)
    .. " after the lift."
end

function LsEffectPerimeter(v, values)
  if v == "tight" then
    return "The home base and six tiles round it. One base against every wave."
  elseif v == "wide" then
    return "Forty-five per cent of the way to the enemy, at most forty tiles. Room for three or"
      .. " four bases, and a long line to hold."
  end
  return "Out to the nearest other base, at most twenty-six tiles. On most maps the natural is"
    .. " inside; where it is further out, the edge reaches it as it grows."
end

-- what the read gives on the map the lobby has: the radius in tiles and how many gates. It is
-- the same measurement the generator makes for the game (LS.ringFor, LS.gatesOn), so the tab
-- and the game agree.
function LsReadPerimeter(v, map)
  if map == nil or map.starts == nil or #map.starts < 2 then return "" end
  local home = map.starts[1]
  local far, fd = map.starts[2], -1
  for i = 2, #map.starts do
    local s = map.starts[i]
    local dx, dy = s.x - home.x, s.y - home.y
    local d = dx * dx + dy * dy
    if d > fd then far, fd = s, d end
  end
  local r = LS.ringFor(map, home, far, v)
  local gates = LS.gatesOn(map, r, far)
  local air = #gates == 1 and gates[1].air
  return r.tiles .. " tiles, " .. #gates .. (#gates == 1 and " gate" or " gates")
    .. (air and ", air only" or "")
end

function LsEffectAlliesPerimeter(v, values)
  if v == "each" then
    return "One perimeter round each home. Nobody can walk into an ally's ground to fight, which"
      .. " is how the Holdout preset plays."
  end
  return "One perimeter round every home. Allies hold one line together."
end

-- with one player there is nothing to share
function LsShownAlliesPerimeter(values)
  return (values.players or 1) > 1
end

function LsEffectPush(v, values)
  if v then
    return "Clearing the last wave lifts the perimeter. The enemy base is then an ordinary"
      .. " computer player with everything it kept back, and its last building ends the game."
  end
  return "Clearing the last wave is the win. Nothing is left to take."
end

-- the wave a run starts at is for trying a wave without playing up to it, so the tab never
-- shows it: it is set from the command line (--mode-settings start_wave=12) or by a tool
function LsShownStartWave(values)
  return false
end

-- Elites is one dial over two things, which the text has to say plainly or a player will read
-- it as a count. Nothing about which elites ride is a choice any more: every one is rolled.
function LsEffectElites(v, values)
  local pct = math.floor(tonumber(v) or 100)
  if pct <= 0 then
    return "No elites, and no boss escort. The waves are their own units alone, which leaves"
      .. " the mid-bosses and the boss to break the line."
  end
  local line
  if pct < 100 then
    line = "Fewer elites than the run's schedule asks for, and each one rolled weaker: " .. pct
      .. " per cent of its rank. A wave the schedule gave an elite still gets one."
  elseif pct == 100 then
    line = "The run's own schedule, each elite rolled at its rank: how many ride is the"
      .. " difficulty's to say."
  else
    line = "More elites than the schedule asks for, and each one rolled stronger: " .. pct
      .. " per cent of its rank, which costs the wave more of its budget in return."
    if pct >= 150 then
      line = line .. " This high each one carries an affix beyond its rank as well."
    end
  end
  return line .. " Every elite is rolled either way -- its stats inside its tier's bounds, its"
    .. " affixes, its spell and its name -- so no two runs send the same one."
end

function LsEffectUniqueGroups(v, values)
  local pct = math.floor(tonumber(v) or 0)
  if pct <= 0 then
    return "No unique groups: every group that walks in is the line of its own tier, rolled"
      .. " but unnamed."
  end
  local line = "About " .. pct .. " in every hundred groups walks in as a unique group: all of"
    .. " its units share one rolled set of effects, under a name and a look of their own, so a"
    .. " player can tell them apart at a glance. They stand above the line they come with and"
    .. " below an elite."
  if pct >= 60 then
    line = line .. " This high, most of what a wave carries is named, which leaves an elite"
      .. " less to stand out against."
  end
  return line
end

-- ---------------------------------------------------------------------------------------------
-- the picture beside the settings: the perimeter as this map and these settings give it, so a
-- player sees which ground is theirs, and whether the allies hold one line together or one
-- each, without starting a game. Lengths are map pixels, 32 to a tile.
--
-- It is drawn and nothing else: the simulation never reads it, it is in no record, and a game
-- plays the same whatever this gives back. It is still worked out by the call the game itself
-- uses, LS.ringsFor, so the picture is the ground and not an impression of it.

-- the line, the band just outside it where a unit is warned, and the base the waves come from
local EDGE_COLOUR, BAND_COLOUR, FOE_COLOUR = 0xffd166, 0xffb347, 0xe0453a

function LsPreview(values, map)
  if map == nil or map.starts == nil or #map.starts < 2 then return {} end

  -- the enemy base is the start farthest from the first, which is how the run picks it when no
  -- slot is marked as the mode's own. The set-up knows no seating, so this is the usual case
  -- and not a promise: a game whose enemy sits elsewhere draws its perimeter elsewhere.
  local home = map.starts[1]
  local foe, fd = nil, -1
  for i = 2, #map.starts do
    local st = map.starts[i]
    local dx, dy = st.x - home.x, st.y - home.y
    local d = dx * dx + dy * dy
    if d > fd then foe, fd = st, d end
  end
  if foe == nil then return {} end

  -- the count leaves out the seat this mode claims for its own enemy, so it is the players
  -- holding a line and the homes are that many, taken in the map's own order of starts. A game
  -- seating no computer at all is the one case it runs over: the run would give the farthest
  -- person's start to the enemy, and the picture draws a ring there as well.
  local want = math.max(1, values.players or 1)
  local homes = {}
  for i = 1, #map.starts do
    local st = map.starts[i]
    if st ~= foe and #homes < want then homes[#homes + 1] = st end
  end

  local shared = (values.allies_perimeter or "shared") == "shared" or #homes == 1
  local rings = LS.ringsFor(map, homes, foe, values.perimeter or "standard", shared)

  local out = {}
  for i = 1, #rings do
    local r = rings[i]
    -- the band first, so the edge is drawn over it
    out[#out + 1] = { shape = "ring", x = r.x, y = r.y,
      r = (r.tiles + LS.BAND_TILES) * 32,
      colour = BAND_COLOUR, edge = 0.4, width = 24, dash = 96 }
    out[#out + 1] = { shape = "ring", x = r.x, y = r.y, r = r.tiles * 32,
      colour = EDGE_COLOUR, edge = 0.95, width = 48 }
  end

  -- where the waves come from
  out[#out + 1] = { shape = "circle", x = foe.x, y = foe.y, r = 5 * 32,
    colour = FOE_COLOUR, fill = 0.3, edge = 0.9, width = 32 }
  return out
end

