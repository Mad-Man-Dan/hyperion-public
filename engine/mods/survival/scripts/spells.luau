-- what the action blocks cannot say, written as Luau. Each of these is named by a
-- {"do": "script", "call": ...} block in types.json, or by a unit's on_hit, on_death or
-- on_damaged where an affix asked for one.
--
-- A block script is given e.caster, e.target, e.x, e.y, e.order and e.player. Places are whole
-- pixels; life is in points.

LS = LS or {}
LS.ai = LS.ai or {}
local A = LS.ai

-- the numbers of a generated spell, found by the order the cast came from: hx.run.spells holds
-- them by the order's name. Nothing is kept between calls, so every machine reads the same run
-- the same way.
local function spellOf(order)
  local run = hx.run
  if run and run.spells then
    for i = 1, #run.spells do
      if hx.id("orders", run.spells[i].order) == order then return run.spells[i] end
    end
  end
  return false
end

-- ---------------------------------------------------------------------------------------------
-- the library spells' own scripts

-- Null Pulse: the shields go, which no block says
function LsNullPulseShields(e)
  local foes = hx.units({ x = e.x, y = e.y, radius = 112, enemies_of = hx.owner(e.caster), max = 48 })
  for i = 1, #foes do
    if hx.max_shields(foes[i]) > 0 then hx.set_shields(foes[i], 0) end
  end
end

-- Flare Mark: the ground round the mark is shown for eight seconds, so the wave can shoot into
-- the fog it just lit
function LsFlareReveal(e)
  hx.reveal(e.x, e.y, 320, 8)
end

-- Thermite Spray: a cone in front of the caster. The blocks only have circles, so the shape is
-- worked out here: everything within six tiles whose direction from the caster is within a
-- third of a turn of where it is facing.
function LsThermiteCone(e)
  local u = e.caster
  local pX, pY = hx.position(u)
  local tx, ty = e.x, e.y
  local dx, dy = tx - pX, ty - pY
  local d = hx.isqrt(dx * dx + dy * dy)
  if d < 1 then dx, dy, d = 1, 0, 1 end
  local foes = hx.units({ x = pX, y = pY, radius = 192, enemies_of = hx.owner(u), max = 48 })
  for i = 1, #foes do
    local qX, qY = hx.position(foes[i])
    local ex, ey = qX - pX, qY - pY
    local ed = hx.isqrt(ex * ex + ey * ey)
    if ed >= 1 then
      -- the dot product of the two directions, in whole numbers: over half the product of the
      -- lengths is within about a third of a turn
      local dot = ex * dx + ey * dy
      if dot * 2 > ed * d then
        hx.damage(foes[i], 16, { type = "explosive", source = u })
        hx.status(foes[i], "t_scorched")
      end
    end
  end
end

-- Infest Husk: a husk is made out of a kill, so the spell finishes what is nearly dead and
-- puts an infested unit in its place
function LsInfestHusk(e)
  local foes = hx.units({ x = e.x, y = e.y, radius = 96, enemies_of = hx.owner(e.caster), max = 16 })
  local weak = A.weakest(foes)
  if weak == nil then return end
  local pX, pY = hx.position(weak)
  local share = A.share(weak)
  if share <= 35 then
    hx.kill(weak)
    local t = hx.id("units", "infested_terran")
    if t then hx.make(t, hx.owner(e.caster), pX, pY, 1, 0) end
    hx.show("thingy_zbgexplo", pX, pY)
  else
    hx.damage(weak, 30, { source = e.caster })
    hx.status(weak, "z_bile_rot")
  end
end

-- Devour: the caster feeds on the bite, which no block says
function LsDevourFeed(e)
  local u, t = e.caster, e.target
  if t == nil then return end
  hx.heal(u, 25)
  hx.status(u, "z_gorged")
end

-- Shield Weave: the shields come back, which heal does not touch
function LsShieldWeave(e)
  local pX, pY = hx.position(e.caster)
  local mine = hx.units({ x = pX, y = pY, radius = 120, allies_of = hx.owner(e.caster), max = 32 })
  for i = 1, #mine do
    local m = mine[i]
    local most = hx.max_shields(m)
    if most > 0 then
      hx.set_shields(m, math.min(most, hx.shields(m) + math.floor(most / 4)))
    end
  end
end

-- Void Tether: the pull. The blocks have no pull, so each unit is put three tiles nearer, which
-- hx.move does through the game's own placement.
function LsVoidTetherPull(e)
  LS.pullToward(e.caster, e.x, e.y, 96, 96)
end

-- ---------------------------------------------------------------------------------------------
-- the pieces the affixes and the spell parts share

-- everything within radius of a place, pulled pixels nearer to it. Ground units only: a flyer
-- is not dragged.
function LS.pullToward(caster, x, y, radius, pixels)
  local foes = hx.units({ x = x, y = y, radius = radius, enemies_of = hx.owner(caster), max = 32 })
  for i = 1, #foes do
    local f = foes[i]
    if not hx.is_building(f) then
      local pX, pY = hx.position(f)
      local dx, dy = x - pX, y - pY
      local d = hx.isqrt(dx * dx + dy * dy)
      if d > 8 then
        local step = math.min(pixels, d - 8)
        hx.move(f, pX + math.floor(dx * step / d), pY + math.floor(dy * step / d))
      end
    end
  end
end

-- the pull payload, on any generated spell that rolled it
function LsSpellPull(e)
  local s = spellOf(e.order)
  local r = (s and s.radius) or 96
  LS.pullToward(e.caster, e.x, e.y, r, 96)
end

-- the chain modifier: it jumps to the next units from where it landed, each hop weaker
function LsSpellChain(e)
  local s = spellOf(e.order)
  if not s then return end
  local hops = s.chainHops or 3
  local share = s.chainShare or 70
  local damage = s.damage or 10
  local x, y = e.x, e.y
  local hit = {}
  for h = 1, hops do
    damage = math.floor(damage * share / 100)
    if damage < 1 then return end
    local foes = hx.units({ x = x, y = y, radius = 128, enemies_of = hx.owner(e.caster), max = 24 })
    local pick = nil
    for i = 1, #foes do
      local f = foes[i]
      local seen = false
      for k = 1, #hit do
        if hit[k] == f then seen = true end
      end
      if (not seen) and (not hx.is_building(f)) and pick == nil then pick = f end
    end
    if pick == nil then return end
    hit[#hit + 1] = pick
    local pX, pY = hx.position(pick)
    hx.damage(pick, damage, { source = e.caster })
    hx.show("thingy_esfhit", pX, pY)
    x, y = pX, pY
  end
end

-- the split modifier: the first thing it hits throws two weaker copies at what is beside it
function LsSpellSplit(e)
  local s = spellOf(e.order)
  if not s then return end
  local into = s.splitInto or 2
  local share = s.splitShare or 50
  local damage = math.floor((s.damage or 10) * share / 100)
  if damage < 1 then return end
  local foes = hx.units({ x = e.x, y = e.y, radius = 160, enemies_of = hx.owner(e.caster), max = 24 })
  local done = 0
  for i = 1, #foes do
    if done >= into then break end
    local f = foes[i]
    if hx.distance(e.x, e.y, hx.position(f).x, hx.position(f).y) > 24 then
      hx.damage(f, damage, { source = e.caster })
      done = done + 1
    end
  end
end

-- the homing modifier: the cast lands on the nearest unit of where it was aimed rather than on
-- the ground, so a spell that misses a moving line still catches it
function LsSpellHoming(e)
  local s = spellOf(e.order)
  local r = (s and s.radius) or 96
  local foes = hx.units({ x = e.x, y = e.y, radius = r + 96, enemies_of = hx.owner(e.caster), max = 24 })
  local near = A.nearestTo(e.x, e.y, foes)
  if near == nil then return end
  local pX, pY = hx.position(near)
  hx.damage(near, math.floor((s and s.damage or 10) / 2), { source = e.caster })
  hx.show("thingy_spotrail", pX, pY)
end

-- the bounce modifier: it goes off once more a few tiles on
function LsSpellBounce(e)
  local s = spellOf(e.order)
  if not s then return end
  local tiles = s.bounceTiles or 5
  local share = s.bounceShare or 60
  local damage = math.floor((s.damage or 10) * share / 100)
  if damage < 1 then return end
  local foes = hx.units({ x = e.x, y = e.y, radius = tiles * 32 + 64, enemies_of = hx.owner(e.caster),
    max = 24 })
  local far, fd = nil, 0
  for i = 1, #foes do
    local pX, pY = hx.position(foes[i])
    local d = hx.distance(e.x, e.y, pX, pY)
    if d > fd then far, fd = foes[i], d end
  end
  if far == nil then return end
  local pX, pY = hx.position(far)
  hx.show("thingy_dbl_exp", pX, pY)
  local near = hx.units({ x = pX, y = pY, radius = (s.radius or 64), enemies_of = hx.owner(e.caster),
    max = 24 })
  for i = 1, #near do
    hx.damage(near[i], damage, { source = e.caster })
  end
end

-- the on-death modifier: what the spell kills goes off again where it stood. The kill is not
-- seen here, so the spell marks what it hit and the mark is read on the unit's death.
function LsSpellOnDeath(e)
  local s = spellOf(e.order)
  local damage = math.floor((s and s.damage or 10) / 2)
  local foes = hx.units({ x = e.x, y = e.y, radius = (s and s.radius or 64),
    enemies_of = hx.owner(e.caster), max = 24 })
  for i = 1, #foes do
    hx.set(foes[i], "ls_last_word", damage)
  end
end

-- the feeding modifier: half of what it does comes back as the caster's life
function LsSpellFeed(e)
  local s = spellOf(e.order)
  if not s then return end
  local share = s.feedShare or 50
  local foes = hx.units({ x = e.x, y = e.y, radius = (s.radius or 64), enemies_of = hx.owner(e.caster),
    max = 24 })
  if #foes == 0 then return end
  hx.heal(e.caster, math.floor((s.damage or 10) * #foes * share / 100))
end

-- the unmaking payload: shields first, and they do not come back soon
function LsSpellUnmakeShields(e)
  local s = spellOf(e.order)
  local foes = hx.units({ x = e.x, y = e.y, radius = (s and s.radius or 64),
    enemies_of = hx.owner(e.caster), max = 24 })
  for i = 1, #foes do
    local f = foes[i]
    if hx.max_shields(f) > 0 then
      hx.set_shields(f, 0)
      hx.status(f, "p_rent")
    end
  end
end

-- ---------------------------------------------------------------------------------------------
-- the affixes' own hooks

-- Splitting: two smaller copies when it dies, each at a share of its life
function LsSplitOnDeath(e)
  local keep = hx.get(e.unit, "ls_no_split")
  if keep then return end
  local t = e.type
  -- the halves are the same kind as the unit that died, so they carry its roll: the readout
  -- reads a unit's roll by the number the mode stamps on it (mode.luau, "ls_roll")
  local roll = hx.get(e.unit, "ls_roll")
  local made = hx.make(t, e.owner, e.x, e.y, 2, 0)
  if made == nil then return end
  for i = 1, #made do
    local m = made[i]
    hx.set(m, "ls_no_split", true)
    if roll then hx.set(m, "ls_roll", roll) end
    hx.set_life(m, math.max(1, math.floor(hx.max_life(m) / 3)))
    if hx.max_shields(m) > 0 then hx.set_shields(m, math.floor(hx.max_shields(m) / 3)) end
  end
  hx.show("thingy_zbgexplo", e.x, e.y)
end

-- Gorging: it feeds on what it hurts
function LsGorgeOnHit(e)
  local back = math.floor((e.taken or e.damage or 0) / 2)
  if back > 0 then hx.heal(e.unit, back) end
  hx.status(e.unit, "z_gorged")
end

-- of the Void: it blinks clear when a hit takes a tenth of its life
function LsBlinkWhenHurt(e)
  local u = e.unit
  local most = hx.max_life(u)
  if most > 0 and (e.damage or 0) * 10 >= most then
    local foes = A.enemies(u, 224)
    local x, y = A.retreat(u, foes, 4)
    local pX, pY = hx.position(u)
    hx.move(u, x, y)
    hx.show("thingy_recall", pX, pY)
  end
  return e.damage
end

-- the last word: a unit the on-death modifier marked goes off where it fell. Hung on the
-- game's own unit-died event (types.json's game.on_death), so it works for any type.
function LS.lastWord(e)
  local d = hx.get(e.unit, "ls_last_word")
  if d == nil or d < 1 then return end
  local foes = hx.units({ x = e.x, y = e.y, radius = 64, owner = nil, max = 16 })
  for i = 1, #foes do
    if hx.owner(foes[i]) == e.owner then hx.damage(foes[i], d, {}) end
  end
  hx.show("thingy_explo2", e.x, e.y)
end
