-- what every brain and every cast decision is built from: finding units, reading the shape of a
-- fight, and working out where to stand. Everything here goes through hx.units and hx.distance,
-- so it is the game's own view and nothing else.
--
-- Places are whole pixels, 32 to a tile. Life is in points.

LS = LS or {}
LS.ai = LS.ai or {}
local A = LS.ai

A.TILE = 32

-- the workers of the three races, by their names in types.json: a worker is worth more than its
-- life says, so an assassin goes for one
local WORKERS = { "terran_scv", "zerg_drone", "protoss_probe" }

function A.at(u)
  local pX, pY = hx.position(u)
  return pX, pY
end

function A.dist(a, b)
  return hx.distance(a, b)
end

-- hx.units' enemies_of and allies_of take a player, not a unit, so every finder here asks on
-- behalf of the unit's owner. A unit that has died has no owner, and then nothing is found
-- rather than everything: without the side named, the game's find returns both sides.
function A.sideOf(u)
  return hx.owner(u)
end

-- the enemies of u within radius pixels of it
function A.enemies(u, radius)
  local p = A.sideOf(u)
  if p == nil then return {} end
  local x, y = A.at(u)
  return hx.units({ x = x, y = y, radius = radius, enemies_of = p, max = 64 })
end

-- of a list, the ones u can attack: its weapons can hit them, and it can walk to them. A ground
-- unit with no air weapon sent at a flyer, or at a unit up a cliff it cannot climb, would stand
-- under it holding an order it can never carry out.
function A.hittable(u, list)
  local out = {}
  for i = 1, #list do
    if hx.can_target(u, list[i]) and hx.can_reach(u, list[i]) then out[#out + 1] = list[i] end
  end
  return out
end

-- the enemies u can attack within radius pixels of it
function A.foes(u, radius)
  return A.hittable(u, A.enemies(u, radius))
end

-- the enemies of u within radius of a place
function A.enemiesAt(u, x, y, radius)
  local p = A.sideOf(u)
  if p == nil then return {} end
  return hx.units({ x = x, y = y, radius = radius, enemies_of = p, max = 64 })
end

function A.allies(u, radius)
  local p = A.sideOf(u)
  if p == nil then return {} end
  local x, y = A.at(u)
  return hx.units({ x = x, y = y, radius = radius, allies_of = p, max = 64 })
end

function A.alliesAt(u, x, y, radius)
  local p = A.sideOf(u)
  if p == nil then return {} end
  return hx.units({ x = x, y = y, radius = radius, allies_of = p, max = 64 })
end

-- the buildings of a list. hx.units' `buildings` says whether buildings are reached at all,
-- not that they alone are, so what is wanted is filtered here.
function A.onlyBuildings(list)
  local out = {}
  for i = 1, #list do
    if hx.is_building(list[i]) then out[#out + 1] = list[i] end
  end
  return out
end

-- the enemy structures within radius of a place
function A.buildingsOf(u, x, y, radius)
  return A.onlyBuildings(A.enemiesAt(u, x, y, radius))
end

-- every enemy structure on the map. A wave walks at what the defender has built, so this is
-- what a brain falls back on when its own surroundings are empty.
function A.enemyStructures(u, max)
  local p = A.sideOf(u)
  if p == nil then return {} end
  return A.onlyBuildings(hx.units({ enemies_of = p, max = max or 400 }))
end

-- the enemy structure nearest the unit: close by, then anywhere on the map
function A.nearestStructureNear(u, radius)
  local x, y = A.at(u)
  return A.nearest(u, A.hittable(u, A.buildingsOf(u, x, y, radius)))
end

function A.nearestStructureAnywhere(u)
  return A.nearest(u, A.hittable(u, A.enemyStructures(u)))
end

-- the nearest enemy of any kind on the map: the last thing a lost unit can walk at
function A.anyEnemyAnywhere(u)
  local p = A.sideOf(u)
  if p == nil then return nil end
  return A.nearest(u, hx.units({ enemies_of = p, max = 200 }))
end

function A.life(u)
  return hx.life(u) + hx.shields(u)
end

function A.maxLife(u)
  return hx.max_life(u) + hx.max_shields(u)
end

-- how much of its life a unit has left, 0 to 100
function A.share(u)
  local m = A.maxLife(u)
  if m <= 0 then return 100 end
  return math.floor(A.life(u) * 100 / m)
end

function A.isWorker(u)
  for i = 1, #WORKERS do
    if hx.is(u, WORKERS[i]) then return true end
  end
  return false
end

function A.isCaster(u)
  return hx.max_energy(u) > 0
end

-- what a unit is worth as a target: its life, and more for what a fight turns on
function A.value(u)
  local v = A.maxLife(u)
  if A.isCaster(u) then v = v + 300 end
  if A.isWorker(u) then v = v + 200 end
  if hx.is_building(u) then v = math.floor(v / 4) end
  return v
end

-- the worth of a list of units added up: what a wave is walking into
function A.threat(list)
  local t = 0
  for i = 1, #list do t = t + A.value(list[i]) end
  return t
end

function A.nearest(u, list)
  local best, bd = nil, 0
  for i = 1, #list do
    local d = hx.distance(u, list[i])
    if best == nil or d < bd then best, bd = list[i], d end
  end
  return best, bd
end

function A.nearestTo(x, y, list)
  local best, bd = nil, 0
  for i = 1, #list do
    local pX, pY = hx.position(list[i])
    local d = hx.distance(x, y, pX, pY)
    if best == nil or d < bd then best, bd = list[i], d end
  end
  return best, bd
end

-- the unit of a list with the least life left: what to finish
function A.weakest(list)
  local best, bv = nil, 0
  for i = 1, #list do
    local v = A.life(list[i])
    if best == nil or v < bv then best, bv = list[i], v end
  end
  return best, bv
end

-- the unit of a list worth the most: what a caster aims at
function A.richest(list)
  local best, bv = nil, 0
  for i = 1, #list do
    local v = A.value(list[i])
    if best == nil or v > bv then best, bv = list[i], v end
  end
  return best, bv
end

-- the casters of a list, and the workers
function A.casters(list)
  local out = {}
  for i = 1, #list do
    if A.isCaster(list[i]) and not hx.is_building(list[i]) then out[#out + 1] = list[i] end
  end
  return out
end

function A.workers(list)
  local out = {}
  for i = 1, #list do
    if A.isWorker(list[i]) then out[#out + 1] = list[i] end
  end
  return out
end

-- the thickest part of a list: the unit with the most of the others within radius, the place to
-- aim an area spell at, and how many it would reach. The list is walked by index, so the answer
-- does not depend on any table's order.
function A.cluster(list, radius)
  local bx, by, bn, bv = 0, 0, 0, 0
  for i = 1, #list do
    local pX, pY = hx.position(list[i])
    local n, v = 0, 0
    for k = 1, #list do
      local qX, qY = hx.position(list[k])
      if hx.distance(pX, pY, qX, qY) <= radius then
        n = n + 1
        v = v + A.value(list[k])
      end
    end
    if v > bv then bx, by, bn, bv = pX, pY, n, v end
  end
  if bn == 0 then return nil end
  return { x = bx, y = by, count = bn, value = bv }
end

-- how much of the caster's own side a splash would catch at a place: a cast is held back when
-- this is more than it is worth
function A.ownInSplash(u, x, y, radius)
  local mine = A.alliesAt(u, x, y, radius)
  local n = 0
  for i = 1, #mine do
    if not hx.is_building(mine[i]) then n = n + 1 end
  end
  return n
end

-- where to pull back to: away from the mass of what is shooting, a few tiles off
function A.retreat(u, list, tiles)
  local x, y = A.at(u)
  if #list == 0 then return x, y end
  local sx, sy = 0, 0
  for i = 1, #list do
    local pX, pY = hx.position(list[i])
    sx = sx + pX
    sy = sy + pY
  end
  local cx, cy = math.floor(sx / #list), math.floor(sy / #list)
  local dx, dy = x - cx, y - cy
  local d = hx.isqrt(dx * dx + dy * dy)
  if d < 1 then return x + tiles * A.TILE, y end
  local step = tiles * A.TILE
  return x + math.floor(dx * step / d), y + math.floor(dy * step / d)
end

-- a step toward a place, so a brain can keep its distance without stopping
function A.toward(u, x, y, pixels)
  local ux, uy = A.at(u)
  local dx, dy = x - ux, y - uy
  local d = hx.isqrt(dx * dx + dy * dy)
  if d < 1 then return ux, uy end
  return ux + math.floor(dx * pixels / d), uy + math.floor(dy * pixels / d)
end

-- whether a unit has a mod status on it, so a brain does not stack what is already there
function A.hasAny(u, names)
  for i = 1, #names do
    if hx.has_status(u, names[i]) > 0 then return true end
  end
  return false
end

-- the energy a caster keeps back, so it always has one cast in hand for the brain's own
function A.spare(u, keep)
  return hx.energy(u) >= keep
end

-- the mode's own numbers, read from the run where the generator put them
function A.run()
  return hx.run
end
