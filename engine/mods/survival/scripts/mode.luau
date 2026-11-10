-- Survival itself: what happens each second of a game. types.json's "game" names LsStart,
-- LsTick (every second) and LsOnDeath.
--
-- Nothing here rolls anything: the run was rolled whole in scripts/generate.luau and is read
-- back as hx.run. What is left to do is send the waves it lists, hold the perimeter, keep the
-- HUD, and end the game.
--
-- What lasts between calls is kept with hx.game_set, which a save and the state hash carry.

LS = LS or {}
LS.ai = LS.ai or {}
local A = LS.ai

local TILE = 32

local function g(name) return hx.game_get(name) end
local function gset(name, v) hx.game_set(name, v) end

-- the perimeter's radius in tiles, by ring. One ring with a shared perimeter, one a player
-- otherwise.
local function ringTiles(i)
  return g("ls_r" .. i) or hx.run.rings[i].tiles
end

local function ringAt(i)
  local r = hx.run.rings[i]
  return r.x, r.y, ringTiles(i)
end

-- ---------------------------------------------------------------------------------------------
-- how the perimeter looks on the ground (hx.area): a wall of light standing on the edge in the
-- player's own colour, dark until one of their own units comes near it, answering a unit that
-- crosses and lighting the ground under the ones left outside. The picture itself is a recipe
-- in types.json's "marks", which this names; what is left here is what belongs to this ring
-- rather than to the picture. Lengths are map pixels, 32 to a tile.

local LOOK = {
  -- the look the ring wears, by the name types.json's "marks" gives it: "ward" for the wall of
  -- light, "line" for the plain ring it wore before. How either is drawn -- its edge, its beat,
  -- the cells in it, the light under a unit left outside -- is a value there, not here, so a
  -- change to the picture is a change to that file. Nothing here draws the ring as the engine
  -- draws an unnamed mark.
  look = "ward",
  -- the band's own look, worn by a ring that draws nothing on the ground and shows on the
  -- minimap alone. The band on the ground is the light under a unit standing in it, which is a
  -- light and not a shape, so the minimap has nothing of it to draw without this.
  band = "band",
  -- what a look cannot say, because it belongs to this ring and not to the picture: how near a
  -- unit comes before the light answers, and how far it rises.
  near = 8 * TILE,  -- how near a unit comes before it shows
  most = 0.85,      -- how far it rises with a unit on it
  least = 0.05,     -- what it sits at with nobody near
  glow = true,      -- only the stretch near a unit lights, not the whole ring
  -- nothing here leaves hx.area its own layer, the ground, where units stand on the ring and
  -- the light of the map falls on it. "over" draws it above everything instead, which is what
  -- to set if a base is found to swallow its own ring. A ward is both of its own accord: its
  -- foot is painted on the ground and its field is drawn over the units.
  layer = nil,
}

-- ---------------------------------------------------------------------------------------------
-- what the mode shows on screen (hx.timer, hx.banner, hx.counter). Presentation: every one of
-- these is drawn only, so a game plays the same with them or without, and the hx.hud values
-- stay beside them for a HUD design that draws its own.

local K = "mode/survival/hud/"

-- the count of waves, top right, and the wave's own count of enemies under it
local function waveCounter(n)
  hx.counter("wave", { label = K .. "wave", value = n, of = hx.run.waves, anchor = "top_right" })
end

-- the countdown to the next wave, or to the end of the build-up before the first
local function nextTimer(key)
  local at = g("ls_next_at") or 0
  local close = (at - hx.frame()) <= hx.seconds(10)
  hx.timer("next", { label = K .. key, ends = at, style = "both",
    full = hx.seconds(key == "build" and hx.run.build or hx.run.gap),
    tone = close and "warn" or "plain" })
end

-- The run started part-way, for trying a wave without playing up to it (the start_wave
-- setting). The waves before it count as cleared, so the perimeter stands where it would and
-- the next wave is the one asked for, a gap away. Nothing of the run is rolled again, so the
-- game and its replay play the same.
local function enterAtWave(n)
  local run = hx.run
  if n == nil or n <= 1 then return end
  if n > run.waves then n = run.waves end
  local done = n - 1
  gset("ls_wave", done)
  gset("ls_cleared", done)
  local grown = math.floor(done / run.growEvery)
  gset("ls_grown", grown)
  for i = 1, #run.rings do
    local r = run.rings[i]
    gset("ls_r" .. i, math.min(r.reach, r.base + run.growTiles * grown))
  end
  gset("ls_next_at", hx.frame() + hx.seconds(run.gap))
  hx.hud("wave", done)
  hx.hud("cleared", done)
  hx.hud("phase", "The waves")
  hx.hud("next_in", run.gap)
  waveCounter(done)
  nextTimer("next_wave")
  LsPreview(n)
  hx.message("The run starts at wave " .. n .. ": " .. done
    .. (done == 1 and " wave counts as cleared." or " waves count as cleared."))
end

-- ---------------------------------------------------------------------------------------------
-- the start

function LsStart(e)
  local run = hx.run
  gset("ls_wave", 0)
  gset("ls_cleared", 0)
  gset("ls_in_wave", false)
  gset("ls_grown", 0)
  gset("ls_lifted", false)
  gset("ls_ended", 0)
  gset("ls_next_at", hx.seconds(run.build))
  gset("ls_foe", run.foe and run.foe.player or 1)
  for i = 1, #run.rings do
    gset("ls_r" .. i, run.rings[i].tiles)
  end

  -- a first aim, before the first tick picks one: aimWave moves it onto the defender's nearest
  -- structure each second, and lift moves it to the enemy base for the push
  if #run.homes > 0 then
    gset("ls_goal_x", run.homes[1].x)
    gset("ls_goal_y", run.homes[1].y)
  end

  hx.hud("mode", "Survival")
  hx.hud("difficulty", run.difficultyName)
  hx.hud("seed", run.seed)
  hx.hud("wave", 0)
  hx.hud("of", run.waves)
  hx.hud("phase", "Build-up")
  hx.hud("next_in", run.build)
  waveCounter(0)
  nextTimer("build")
  LsPreview(1)

  hx.message("Survival, " .. run.difficultyName .. ": " .. run.waves .. " waves, "
    .. run.build .. " seconds to build. Run " .. run.seed .. ".")
  enterAtWave(run.startWave or 1)
  for i = 1, #run.rings do
    local x, y, t = ringAt(i)
    hx.ping(x, y)
  end
end

-- what the HUD says about the wave to come, when the setting is on
function LsPreview(n)
  local run = hx.run
  if not run.preview then
    hx.hud("next", "")
    return
  end
  local w = run.wave[n]
  if w == nil then
    hx.hud("next", "")
    return
  end
  local text = (w.assault and "Assault: " or "")
  text = text .. (waveLine(w, true) or (w.supply .. " supply"))
  hx.hud("next", text)
end

-- the unique groups a wave carries: the rolled groups whose whole number share one set of
-- effects. A player reads them as they read an elite, by name, so the wave's own line names
-- the first and counts the rest.
local function uniquesOf(w)
  local out = {}
  for i = 1, #w.kinds do
    if w.kinds[i].unique then out[#out + 1] = w.kinds[i] end
  end
  return out
end

-- what a wave is known by: its elites first, then its unique groups, then its weight alone
local function waveLine(w, withSupply)
  if #w.elites > 0 then
    local line = w.elites[1].name
    if w.elites[1].tier ~= "elite" then line = line .. ", " .. w.elites[1].tier end
    local rest = (#w.elites - 1) + #uniquesOf(w)
    if rest > 0 then line = line .. " and " .. rest .. " more" end
    return line
  end
  local uniq = uniquesOf(w)
  if #uniq > 0 then
    local line = uniq[1].name
    if uniq[1].affixes and #uniq[1].affixes > 0 then
      line = line .. " (" .. uniq[1].affixes[1] .. ")"
    end
    if #uniq > 1 then line = line .. " and " .. (#uniq - 1) .. " more" end
    return line
  end
  return withSupply and (w.supply .. " supply") or nil
end

-- ---------------------------------------------------------------------------------------------
-- the waves

-- where a wave walks out from: the enemy base, or two gates on an assault
local function wayIn(w)
  local run = hx.run
  if w.assault and #run.gates > 0 then
    local from = {}
    for i = 1, math.min(w.gates or 2, #run.gates) do
      from[#from + 1] = run.gates[i]
    end
    return from
  end
  if run.foe then return { { x = run.foe.x, y = run.foe.y, kind = "base" } } end
  return { run.gates[1] }
end

-- defined below the waves; sendWave calls them, so they are declared before it
local aliveByWave, leftCounter

local function sendWave(n)
  local run = hx.run
  local w = run.wave[n]
  if w == nil then return end
  local foe = g("ls_foe")
  local from = wayIn(w)
  local made = {}

  local function put(typeName, count, at)
    local t = hx.id("units", typeName)
    if t == nil then return end
    -- the kind's own number, read out of its name ("gen_7_spitter"): the readout reads what the
    -- kind rolled by it (hx.run.rolled), so it is kept on the unit rather than looked for. A
    -- unit of a kind the run did not roll carries none, and its panel stays classic's.
    local digits = string.match(typeName, "^gen_(%d+)_")
    local roll = digits and tonumber(digits) or nil
    local ids = hx.make(t, foe, at.x, at.y, count, 0)
    if ids == nil then return end
    for i = 1, #ids do
      hx.set(ids[i], "ls_wave", n)
      if roll then hx.set(ids[i], "ls_roll", roll) end
      -- the unit's own turn on top of its group's roll, drawn as it walks in: the group is still
      -- the group, and the odd one of it is a little tougher, quicker or harder-hitting than the
      -- rest (scripts/pools/tiers.luau, LS.bornMark)
      local mark = LS.bornMark(w.tier)
      if mark then hx.status(ids[i], mark) end
      made[#made + 1] = ids[i]
    end
  end

  for i = 1, #w.kinds do
    put(w.kinds[i].type, w.counts[i], from[((i - 1) % #from) + 1])
  end
  for i = 1, #w.elites do
    put(w.elites[i].unit, 1, from[((i - 1) % #from) + 1])
  end

  -- the player is the mode's own, so classic's AI is not playing it and these are already
  -- outside its towns and captains. The mark is kept for the game that claimed no player, where
  -- the foe is a person's slot and the mode still has to take what it makes there.
  hx.own(made)
  local gx, gy = g("ls_goal_x"), g("ls_goal_y")
  if gx then hx.order(made, "attack_move", gx, gy) end

  gset("ls_in_wave", true)
  gset("ls_wave", n)
  hx.hud("wave", n)
  hx.hud("phase", w.last and "The last wave" or (w.assault and "An assault" or "The waves"))

  local what = w.last and "The last wave" or (w.assault and ("Assault, wave " .. n)
    or ("Wave " .. n))
  local line = waveLine(w, false)
  if line == nil then
    line = w.supply .. " supply, from "
      .. (from[1].kind == "base" and "the enemy base" or ("the " .. from[1].kind .. " gate"))
  end
  hx.message(what .. ": " .. line)

  -- what it shows: the wave's own banner, the count out of the run's, how many of it live,
  -- and no countdown while it is out
  hx.banner("wave", { title = K .. (w.last and "last_wave" or (w.assault and "assault" or "wave_at")),
    values = { n }, lines = { line }, seconds = 4,
    tone = w.last and "bad" or (w.assault and "warn" or "plain") })
  waveCounter(n)
  local _, all = aliveByWave()
  leftCounter(all)
  -- the countdown runs on to the next wave, since the next comes on the clock
  if n < hx.run.waves then nextTimer("next_wave") else hx.timer("next") end
  for i = 1, #from do
    hx.ping(from[i].x, from[i].y)
  end
end

-- how many of each wave sent still live, by its number, and of every wave together. Waves go
-- out on the clock, so more than one may be out at once.
function aliveByWave()
  local foe = g("ls_foe")
  local list = hx.units({ owner = foe, max = 1700 })
  local by, all = {}, 0
  for i = 1, #list do
    local n = hx.get(list[i], "ls_wave")
    if n then
      by[n] = (by[n] or 0) + 1
      all = all + 1
    end
  end
  return by, all
end

-- the count of the waves' units still alive, top right
function leftCounter(n)
  hx.counter("left", { label = K .. "left", value = n, anchor = "top_right", tone = "bad" })
end

-- ---------------------------------------------------------------------------------------------
-- the perimeter
--
-- Inside the edge: nothing. In the band, four tiles wide just outside it: the unit is marked
-- and counted and nothing else happens, so the mode can ask a player to step out a little and a
-- unit that drifts a tile while fighting is warned, not killed. Past the band: a share of the
-- unit's own most life a second, shields first, growing with the distance out. The death is
-- nobody's kill.

-- the wave's objective: the defender structure nearest the wave, picked again every second.
-- The waves used to walk at the first home's start place for the whole game, which left them
-- heading at ground the defender had left and ignoring every other player.
local function aimWave()
  local run = hx.run
  local foe = g("ls_foe")

  -- where the wave is now: its own units only, since the base keeps a garrison back until the
  -- push and counting that would drag the aim home. Before a wave lands, where it comes from.
  local wave = g("ls_wave") or 0
  local theirs = hx.units({ owner = foe, max = 400 })
  local sx, sy, n = 0, 0, 0
  for i = 1, #theirs do
    local u = theirs[i]
    if hx.get(u, "ls_wave") == wave and not hx.is_building(u) then
      local pX, pY = hx.position(u)
      sx, sy, n = sx + pX, sy + pY, n + 1
    end
  end
  local fromX, fromY
  if n > 0 then
    fromX, fromY = math.floor(sx / n), math.floor(sy / n)
  elseif run.foe then
    fromX, fromY = run.foe.x, run.foe.y
  else
    return
  end

  -- the nearest thing the defenders have standing, structures first. The aim it already has is
  -- watched for at the same time: it is kept only while something of theirs is still there.
  local gx, gy = g("ls_goal_x"), g("ls_goal_y")
  local bestX, bestY, bd = nil, nil, 0
  local held = false
  local function look(player, buildingsOnly)
    local list = hx.units({ owner = player, max = 400 })
    for i = 1, #list do
      local u = list[i]
      if (not buildingsOnly) or hx.is_building(u) then
        local pX, pY = hx.position(u)
        local d = hx.distance(fromX, fromY, pX, pY)
        if bestX == nil or d < bd then bestX, bestY, bd = pX, pY, d end
        if gx and hx.distance(gx, gy, pX, pY) <= 2 * TILE then held = true end
      end
    end
  end
  for i = 1, #run.homes do look(run.homes[i].player, true) end
  -- nothing built anywhere: whatever the defenders have left, so the wave still walks at them
  if bestX == nil then
    for i = 1, #run.homes do look(run.homes[i].player, false) end
  end
  if bestX == nil then return end
  -- the aim moves only for something meaningfully nearer, so two structures a step apart do not
  -- hand the wave a new point every second. An aim with nothing left standing on it always moves.
  if held and hx.distance(fromX, fromY, gx, gy) <= bd + 2 * TILE then return end
  gset("ls_goal_x", bestX)
  gset("ls_goal_y", bestY)
end

-- the perimeter on the ground: a ring each. Set again every second, which is also how a ring
-- that has grown, or an arena pulling it in, moves. On the ground the band outside the edge is
-- no ring of its own -- the ward's outside tint lights what stands in it -- so a second ring is
-- set for the minimap, which draws shapes and not light.
local function ringsShown(run)
  return run.shared and 1 or #run.homes
end

local function drawPerimeter(run)
  for i = 1, ringsShown(run) do
    local cx, cy, rt = ringAt(i)
    local r = rt * TILE
    -- a shared ring is everyone's and any of them lights it. A ring of one player's own is
    -- drawn on that player's screen alone, so the units that light it are already "me", and
    -- nobody is shown an edge that is not the one holding them.
    local seen = run.shared and nil or run.homes[i].player
    local lit = run.shared and "allies" or "me"
    local colour = run.shared and run.homes[1].player or run.homes[i].player
    hx.area("ls_ring_" .. i, { shape = "ring", x = cx, y = cy, radius = r,
      color = colour, players = seen, layer = LOOK.layer, look = LOOK.look,
      -- the band is the run's own, the one the penalty is worked out from, so the light under
      -- a unit turns from warning to danger where the cost begins. The look says how that
      -- light is drawn; this says only that it is drawn and how far the band reaches.
      outside = { style = "tint", band = run.band * TILE },
      fade = { players = lit, near = LOOK.near, most = LOOK.most, least = LOOK.least,
        glow = LOOK.glow } })
    -- and the band, on the minimap alone. The ring itself is what the minimap draws from, since
    -- the band on the ground is a light under the units in it rather than a shape. Nothing of
    -- the ground's beat or fade reaches the minimap, so both rings show there whole: the line in
    -- the player's own colour, the band dashed in the warning colour outside it.
    if run.band > 0 then
      hx.area("ls_band_" .. i, { shape = "ring", x = cx, y = cy, radius = r + run.band * TILE,
        players = seen, look = LOOK.band })
    end
  end
end

local function clearPerimeter(run)
  for i = 1, ringsShown(run) do
    hx.area("ls_ring_" .. i)
    hx.area("ls_band_" .. i)
  end
end

local function penalty(run, tilesOut)
  local step = math.floor(math.max(0, tilesOut) / 4)
  local pct = run.penaltyStart + run.penaltyStep * step
  if pct > run.penaltyCap then pct = run.penaltyCap end
  return pct
end

local function perimeterTick()
  local run = hx.run
  if g("ls_lifted") then
    hx.hud("outside", 0)
    hx.hud("in_band", 0)
    hx.banner("perimeter")
    clearPerimeter(run)
    return
  end
  drawPerimeter(run)
  local out, band = 0, 0
  for i = 1, #run.homes do
    local home = run.homes[i]
    local ri = run.shared and 1 or i
    local cx, cy, rt = ringAt(ri)
    local r = rt * TILE
    local bandEnd = r + run.band * TILE
    local mine = hx.units({ owner = home.player, max = 200 })
    for k = 1, #mine do
      local u = mine[k]
      if not hx.is_building(u) then
        local pX, pY = hx.position(u)
        local d = hx.distance(cx, cy, pX, pY)
        if d <= r then
          hx.set(u, "ls_out", nil)
        elseif d <= bandEnd then
          hx.set(u, "ls_out", 1)
          band = band + 1
        else
          hx.set(u, "ls_out", 2)
          out = out + 1
          local tilesOut = math.floor((d - bandEnd) / TILE)
          local pct = penalty(run, tilesOut)
          local dmg = math.max(1, math.floor(hx.max_life(u) * pct / 100))
          hx.damage(u, dmg, { type = "ignore_armour" })
        end
      end
    end
  end
  hx.hud("outside", out)
  hx.hud("in_band", band)
  -- the warning stands while a unit is outside: set again each second, it fades once they are
  -- back inside
  if out > 0 then
    hx.banner("perimeter", { title = K .. "outside", values = { out }, seconds = 2,
      tone = "bad", anchor = "middle" })
  end
end

-- the edge moves out four tiles every fifth wave cleared, never past 45% of the way to the
-- enemy, so expansions are earned
local function grow()
  local run = hx.run
  local n = (g("ls_grown") or 0) + 1
  gset("ls_grown", n)
  for i = 1, #run.rings do
    local r = run.rings[i]
    local want = math.min(r.reach, r.base + run.growTiles * n)
    gset("ls_r" .. i, want)
  end
  hx.message("The perimeter widens: " .. run.growTiles .. " tiles")
end

-- while a boss lives its arena holds the edge in
local function arenaTick()
  local run = hx.run
  if g("ls_arena") ~= 1 then return end
  local shrink = g("ls_arena_shrink") or 4
  local n = g("ls_grown") or 0
  for i = 1, #run.rings do
    local r = run.rings[i]
    local want = math.max(run.band + 4, math.min(r.reach, r.base + run.growTiles * n) - shrink)
    gset("ls_r" .. i, want)
  end
end

-- ---------------------------------------------------------------------------------------------
-- the ending

local function lift(straightToPush)
  local run = hx.run
  if g("ls_lifted") then return end
  gset("ls_lifted", true)
  gset("ls_lift_at", hx.frame())
  gset("ls_arena", 0)
  hx.hud("phase", run.push and "The push" or "Held")
  if run.push then
    hx.message("The perimeter lifts. The enemy base is yours to take.")
    -- what the enemy base kept back goes to its own computer again
    local foe = g("ls_foe")
    local keep = hx.units({ owner = foe, max = 200 })
    hx.release(keep)
    if run.foe then
      gset("ls_goal_x", run.foe.x)
      gset("ls_goal_y", run.foe.y)
      hx.ping(run.foe.x, run.foe.y)
    end
  else
    hx.message("The map is open. Every wave held.")
  end
end

-- whether a home still has a building standing. hx.units' `buildings` only says whether
-- buildings are reached at all, so the list is one of units and the buildings are counted here.
local function buildingsOwned(player)
  local list = hx.units({ owner = player, max = 400 })
  local n = 0
  for i = 1, #list do
    if hx.is_building(list[i]) then n = n + 1 end
  end
  return n
end

local function stillStanding(home)
  return buildingsOwned(home.player) > 0
end

-- ---------------------------------------------------------------------------------------------
-- a second: what a player reads, and whether the game is over. The waves themselves are the
-- director's, below, on the same second, so the countdown a player watches and the wave that
-- answers it are never out of step.

function LsTick(e)
  local run = hx.run
  if g("ls_ended") ~= 0 then return end

  perimeterTick()
  arenaTick()

  local wave = g("ls_wave") or 0
  local inWave = g("ls_in_wave")
  local lifted = g("ls_lifted")

  if not lifted then
    if inWave then
      local _, all = aliveByWave()
      leftCounter(all)
    end
    if wave < run.waves then
      local secs = math.max(0, math.floor(((g("ls_next_at") or 0) - hx.frame()) / 24))
      hx.hud("next_in", secs)
      nextTimer(wave == 0 and "build" or "next_wave")
    end
  end

  -- a home that has lost every building is out; the last one out ends the game
  local left = 0
  for i = 1, #run.homes do
    local home = run.homes[i]
    if stillStanding(home) then
      left = left + 1
    elseif g("ls_out_" .. home.player) ~= 1 then
      -- said once a player: the game value marks it
      gset("ls_out_" .. home.player, 1)
      hx.lose({ home.player })
      hx.message("A home base was lost")
    end
  end
  if left == 0 then
    gset("ls_ended", 1)
    return
  end

  if lifted then
    local players = {}
    for i = 1, #run.homes do players[#players + 1] = run.homes[i].player end
    if run.push then
      local foe = g("ls_foe")
      if buildingsOwned(foe) == 0 then
        gset("ls_ended", 2)
        hx.win(players)
      end
    elseif hx.frame() - (g("ls_lift_at") or 0) > hx.seconds(10) then
      gset("ls_ended", 2)
      hx.win(players)
    end
  end
end

-- ---------------------------------------------------------------------------------------------
-- the mode's own player
--
-- The mode plays the enemy itself ("enemy_player" in mod.json), so no classic AI starts for it
-- and nothing of its own builds, trains or attacks. This is where everything that player does
-- is decided: when a wave comes, what is in it, where it walks, and when the perimeter lifts.
-- It runs on the game's own frames, so it is part of the simulation like the tick, and a loaded
-- game takes its beat up where the save left it.

function LsDirector(e)
  local run = hx.run
  if g("ls_ended") ~= 0 then return end

  -- the claimed player is told to a script here and nowhere else, so it is kept for the hooks
  -- that spawn and count. Once only, on the first beat: it is the same player the generator was
  -- given, so this changes nothing in an ordinary game, and taking it later would leave a wave
  -- already out belonging to a player nothing counts. With no computer to claim it is -1 and
  -- the generator's own choice of a foe stands, which is a game whose foe is a person.
  if e.player and e.player >= 0 and g("ls_foe_claimed") ~= 1 then
    gset("ls_foe", e.player)
    gset("ls_foe_claimed", 1)
  end

  if g("ls_lifted") then return end

  -- where the waves walk. On the same beat as the tick, so a countdown that reaches zero and
  -- the wave that answers it fall on one second.
  aimWave()

  local wave = g("ls_wave") or 0

  -- the next wave is due: on the clock, whether the last is cleared or not
  if wave < run.waves and hx.frame() >= (g("ls_next_at") or 0) then
    gset("ls_next_at", hx.frame() + hx.seconds(run.gap))
    sendWave(wave + 1)
    LsPreview(wave + 2)
    return
  end

  -- the waves out are cleared in the order they came: the perimeter widens every fifth, and
  -- the last one lifts it
  local by = aliveByWave()
  local cleared = g("ls_cleared") or 0
  while cleared < wave and (by[cleared + 1] or 0) == 0 do
    cleared = cleared + 1
    gset("ls_cleared", cleared)
    hx.hud("cleared", cleared)
    if cleared >= run.waves then
      hx.counter("left")
      gset("ls_in_wave", false)
      lift(false)
      return
    end
    if cleared % run.growEvery == 0 then grow() end
    hx.message("Wave " .. cleared .. " cleared")
    hx.banner("wave", { title = K .. "cleared", values = { cleared }, seconds = 3,
      tone = "good" })
  end
  if cleared >= wave and g("ls_in_wave") then
    hx.counter("left")
    gset("ls_in_wave", false)
    hx.hud("phase", "The waves")
  end
end

-- ---------------------------------------------------------------------------------------------
-- a unit died: anywhere in the game, with its killer

function LsOnDeath(e)
  -- the on-death modifier's mark goes off where the unit fell
  LS.lastWord(e)
end
