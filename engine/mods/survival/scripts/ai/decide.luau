-- the three decisions every cast makes, so each spell's own function is only its own rules:
-- is it worth casting now, what is it aimed at, and would it hurt our own side.
--
-- A cast-AI function is a card's "ai": it is given e.unit and e.order and returns a unit, or x
-- and y, or nothing, which means do not cast now.

LS = LS or {}
LS.ai = LS.ai or {}
local A = LS.ai

-- every cast keeps a reserve, so a caster never spends its last energy on a cheap spell and
-- then cannot answer with its own
A.RESERVE = 10

-- an area cast: the thickest clump of enemies within reach, worth at least minValue, where no
-- more than spare of our own stand. Returns x, y.
function A.area(e, reach, radius, minValue, spare)
  local u = e.unit
  if not A.spare(u, A.RESERVE) then return nil end
  local foes = A.enemies(u, reach)
  if #foes == 0 then return nil end
  local c = A.cluster(foes, radius)
  if c == nil or c.value < minValue then return nil end
  if A.ownInSplash(u, c.x, c.y, radius) > (spare or 0) then return nil end
  return c.x, c.y
end

-- a cast where the unit stands: the same test, round itself
function A.onSelf(e, radius, minValue, spare)
  local u = e.unit
  if not A.spare(u, A.RESERVE) then return nil end
  local x, y = A.at(u)
  local foes = A.enemiesAt(u, x, y, radius)
  if A.threat(foes) < minValue then return nil end
  if A.ownInSplash(u, x, y, radius) > (spare or 99) then return nil end
  return x, y
end

-- one enemy worth the spell: the richest within reach, with none of the statuses it would only
-- stack on top of
function A.one(e, reach, minValue, avoid)
  local u = e.unit
  if not A.spare(u, A.RESERVE) then return nil end
  local foes = A.enemies(u, reach)
  local best, bv = nil, 0
  for i = 1, #foes do
    local f = foes[i]
    local ok = not hx.is_building(f)
    if ok and avoid and A.hasAny(f, avoid) then ok = false end
    if ok then
      local v = A.value(f)
      if best == nil or v > bv then best, bv = f, v end
    end
  end
  if best == nil or bv < minValue then return nil end
  return best
end

-- an ally that needs the spell: the one lowest on life under share percent, within reach
function A.hurtAlly(e, reach, share, avoid)
  local u = e.unit
  if not A.spare(u, A.RESERVE) then return nil end
  local mine = A.allies(u, reach)
  local best, bs = nil, 101
  for i = 1, #mine do
    local m = mine[i]
    local ok = not hx.is_building(m)
    if ok and avoid and A.hasAny(m, avoid) then ok = false end
    if ok then
      local sh = A.share(m)
      if sh <= share and sh < bs then best, bs = m, sh end
    end
  end
  return best
end

-- our own side gathered: where to drop a spell that helps, and how many it would reach
function A.ownCluster(e, reach, radius, least, avoid)
  local u = e.unit
  if not A.spare(u, A.RESERVE) then return nil end
  local mine = A.allies(u, reach)
  local want = {}
  for i = 1, #mine do
    if (not hx.is_building(mine[i])) and ((not avoid) or (not A.hasAny(mine[i], avoid))) then
      want[#want + 1] = mine[i]
    end
  end
  if #want < least then return nil end
  local c = A.cluster(want, radius)
  if c == nil or c.count < least then return nil end
  return c.x, c.y
end

-- a caster of theirs, which is what a spell that takes energy or silences is for
function A.theirCaster(e, reach, avoid)
  local u = e.unit
  if not A.spare(u, A.RESERVE) then return nil end
  local foes = A.enemies(u, reach)
  local cs = A.casters(foes)
  local best, bv = nil, 0
  for i = 1, #cs do
    if (not avoid) or (not A.hasAny(cs[i], avoid)) then
      local v = hx.energy(cs[i])
      if best == nil or v > bv then best, bv = cs[i], v end
    end
  end
  return best
end

-- a worker of theirs, which is what an assassin's spell is for
function A.theirWorker(e, reach)
  local u = e.unit
  if not A.spare(u, A.RESERVE) then return nil end
  local foes = A.enemies(u, reach)
  local ws = A.workers(foes)
  local best = A.nearest(u, ws)
  return best
end

-- a generated spell casts on the thickest clump its own splash would reach, and on nothing when
-- the clump is thin. Its numbers come from the order's own weapon, which the engine knows, so
-- one function serves every rolled spell.
function LsAiGenerated(e)
  local x, y = A.area(e, 320, 96, 400, 1)
  if x then return x, y end
  local one = A.one(e, 288, 500, nil)
  return one
end
