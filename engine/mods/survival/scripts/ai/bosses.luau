-- the mid-boss and boss brains: phases at life thresholds, each one a thing the player has to
-- answer differently. A phase is kept on the unit (hx.set), so a save and a replay carry it.
--
-- A mid-boss has two phases, a boss four. Each phase may turn on a spell, summon, enrage, or set
-- the arena rule the mode reads while the boss lives.

LS = LS or {}
LS.ai = LS.ai or {}
local A = LS.ai

-- what the run rolled for this unit: its name, its generated spell's order and its arena
local function specOf(u)
  local run = hx.run
  if run == nil then return nil end
  for i = 1, #run.wave do
    local w = run.wave[i]
    for k = 1, #w.elites do
      if hx.is(u, w.elites[k].unit) then return w.elites[k], w end
    end
  end
  return nil
end

local function phase(u)
  return hx.get(u, "ls_phase") or 0
end

local function setPhase(u, n)
  hx.set(u, "ls_phase", n)
end

-- a phase starts: say so where the unit stands, and mark it
local function enter(u, n, text)
  setPhase(u, n)
  hx.set(u, "ls_summon_at", hx.frame())
  local pX, pY = hx.position(u)
  hx.ping(pX, pY)
  if text then hx.message(text) end
  hx.show("thingy_dbl_exp", pX, pY)
end

-- its life while it lives, a bar over the game. Presentation: set again each time the brain
-- runs, it fades away three seconds after the boss stops being thought for, which is its death.
local function bossBar(u, name)
  hx.counter("boss", { label = name, value = hx.life(u), of = hx.max_life(u), style = "bar",
    anchor = "top", tone = "bad", shows = 3 })
end

-- its own spell, cast at the best place its card's finder would pick
local function castOwn(u, order)
  if order == nil then return end
  local n = hx.id("orders", order)
  if n == nil then return end
  local e = { unit = u, order = n }
  local x, y = LsAiGenerated(e)
  if x == nil then return end
  if y == nil then
    hx.cast(u, n, x)
  else
    hx.cast(u, n, x, y)
  end
end

-- minions beside it, every so many seconds
local function summon(u, name, count, every)
  local last = hx.get(u, "ls_summon_at") or 0
  if hx.frame() - last < hx.seconds(every) then return end
  hx.set(u, "ls_summon_at", hx.frame())
  local t = hx.id("units", name)
  if t == nil then return end
  local pX, pY = hx.position(u)
  hx.make(t, hx.owner(u), pX, pY, count, 0)
  hx.show("thingy_zbgexplo", pX, pY)
end

-- ---------------------------------------------------------------------------------------------

-- a mid-boss: it fights as its role until half life, then it is angry and keeps its spell up
function LsBrainMidBoss(e)
  local u = e.unit
  local spec = specOf(u)
  local share = A.share(u)
  local ph = phase(u)
  bossBar(u, (spec and spec.name) or "The mid-boss")

  if ph == 0 then
    enter(u, 1, nil)
    ph = 1
  end
  if ph == 1 and share <= 50 then
    enter(u, 2, (spec and spec.name or "The mid-boss") .. " is enraged")
    hx.status(u, "z_frenzied")
    ph = 2
  end

  if spec and spec.spellOrder then castOwn(u, spec.spellOrder) end

  if ph >= 2 then
    hx.status(u, "z_frenzied")
    local foes = A.foes(u, 224)
    local weak = A.weakest(foes)
    if weak then
      hx.order(u, "attack_unit", weak)
      return
    end
  end
  LsBrainBruiser(e)
end

-- a boss: four phases. Half life turns on the summons, a quarter the enrage, and from the
-- first phase the arena rule holds while it lives.
function LsBrainBoss(e)
  local u = e.unit
  local spec, w = specOf(u)
  local share = A.share(u)
  local ph = phase(u)
  local name = (spec and spec.name) or "The boss"
  bossBar(u, name)

  if ph == 0 then
    enter(u, 1, name .. " has arrived")
    -- the arena: the mode reads these while the boss lives
    if w and w.arena then
      hx.game_set("ls_arena", 1)
      hx.game_set("ls_arena_shrink", w.arena.shrink or 4)
    end
    ph = 1
  end
  if ph == 1 and share <= 75 then
    enter(u, 2, name .. " calls its own")
    ph = 2
  end
  if ph == 2 and share <= 50 then
    enter(u, 3, name .. " opens up")
    hx.status(u, "z_carapace_hard")
    ph = 3
  end
  if ph == 3 and share <= 25 then
    enter(u, 4, name .. " is enraged")
    ph = 4
  end

  -- its spell, from the second phase on and every chance after
  if ph >= 2 and spec and spec.spellOrder then castOwn(u, spec.spellOrder) end

  -- its summons, from the second phase: every 20 seconds, and every 12 once enraged
  if ph >= 2 and w and w.arena and w.arena.minion then
    summon(u, w.arena.minion, ph >= 4 and 4 or 2,
      (w.arena.minionsEvery or 20) - (ph >= 4 and 8 or 0))
  end

  -- enraged: faster, harder, and it goes for whatever is nearest rather than the weakest
  if ph >= 4 then
    hx.status(u, "z_frenzied")
    hx.status(u, "t_overclocked")
    local foes = A.foes(u, 256)
    local near = A.nearest(u, foes)
    if near then
      hx.order(u, "attack_unit", near)
      return
    end
  end

  LsBrainBruiser(e)
end
