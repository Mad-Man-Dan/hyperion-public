-- a unit's brain, by its role. Each is a unit's "scripts" brain, run once a second for every
-- unit of its type that a computer owns, and for the wave units the mode owns (hx.own), which
-- classic's own AI then leaves alone.
--
-- A brain only orders and casts. It never moves a unit by hand, so everything it does is a
-- command the game would have taken from a player.

LS = LS or {}
LS.ai = LS.ai or {}
local A = LS.ai

-- where the wave is walking: the mode sets it each wave (scripts/mode.luau)
local function goal()
  local x, y = hx.game_get("ls_goal_x"), hx.game_get("ls_goal_y")
  if x == nil or y == nil then return nil end
  return x, y
end

-- Every brain that picks a target out of a list ends on this when the list hands nothing back,
-- so a brain cannot finish a tick having given no order. A unit that stands still for a tick
-- stands still for good, which is the shape of fault this mode is least able to show.
--
-- where a unit goes when nothing is in reach. A structure close by is walked onto rather than
-- past; otherwise the wave's goal, which the mode keeps on the defender's nearest structure.
-- The last two are for a goal that is gone, so a unit is never left standing with no order.
local function pushOn(u)
  local b = A.nearestStructureNear(u, 512)
  if b then
    hx.order(u, "attack_unit", b)
    return
  end
  local gx, gy = goal()
  if gx then
    hx.order(u, "attack_move", gx, gy)
    return
  end
  local s = A.nearestStructureAnywhere(u)
  if s then
    local sx, sy = hx.position(s)
    hx.order(u, "attack_move", sx, sy)
    return
  end
  local any = A.anyEnemyAnywhere(u)
  if any then
    local ax, ay = hx.position(any)
    hx.order(u, "attack_move", ax, ay)
  end
end

-- the brain is quiet while the unit cannot act, so a rooted unit is not given orders it drops
local function held(u)
  return hx.has_status(u, "z_rooted") > 0 or hx.has_status(u, "t_clamped") > 0
    or hx.has_status(u, "p_rifted") > 0 or hx.has_status(u, "t_anchored") > 0
end

-- ---------------------------------------------------------------------------------------------

-- bruiser: walks in and finishes the weakest thing it can reach
function LsBrainBruiser(e)
  local u = e.unit
  if held(u) then return end
  local foes = A.foes(u, 224)
  if #foes == 0 then
    pushOn(u)
    return
  end
  local weak = A.weakest(foes)
  if weak then
    hx.order(u, "attack_unit", weak)
  else
    pushOn(u)
  end
end

-- ranged: keeps its distance. Inside its own reach it steps back and keeps firing, which is
-- what makes a wave of them hard to catch.
function LsBrainRanged(e)
  local u = e.unit
  if held(u) then return end
  local foes = A.foes(u, 256)
  if #foes == 0 then
    pushOn(u)
    return
  end
  local near, nd = A.nearest(u, foes)
  if near and nd < 96 then
    local rx, ry = A.retreat(u, foes, 4)
    hx.order(u, "move", rx, ry)
    return
  end
  local weak = A.weakest(foes)
  if weak then
    hx.order(u, "attack_unit", weak)
  else
    pushOn(u)
  end
end

-- caster: stays behind the wave. Its casts are its card's own (the ai functions); the brain
-- only keeps it where it can cast from.
function LsBrainCaster(e)
  local u = e.unit
  if held(u) then return end
  local foes = A.enemies(u, 288)
  if #foes == 0 then
    pushOn(u)
    return
  end
  local near, nd = A.nearest(u, foes)
  if near and nd < 160 then
    local rx, ry = A.retreat(u, foes, 5)
    hx.order(u, "move", rx, ry)
    return
  end
  -- it holds where it is, so the cast finder has a clear line and the unit does not wander
  hx.order(u, "hold_position")
end

-- support: keeps the hurt alive and pulls back when it is the one being shot
function LsBrainSupport(e)
  local u = e.unit
  if held(u) then return end
  if A.share(u) <= 35 then
    local foes = A.enemies(u, 224)
    local rx, ry = A.retreat(u, foes, 7)
    hx.order(u, "move", rx, ry)
    return
  end
  local mine = A.allies(u, 320)
  local hurt, sh = nil, 101
  for i = 1, #mine do
    if not hx.is_building(mine[i]) then
      local s = A.share(mine[i])
      if s < sh then hurt, sh = mine[i], s end
    end
  end
  if hurt and sh < 80 then
    local pX, pY = hx.position(hurt)
    local tx, ty = A.toward(u, pX, pY, 64)
    hx.order(u, "move", tx, ty)
    return
  end
  pushOn(u)
end

-- summoner: stays back, and keeps what it makes out in front of it
function LsBrainSummoner(e)
  local u = e.unit
  if held(u) then return end
  local foes = A.enemies(u, 288)
  if #foes == 0 then
    pushOn(u)
    return
  end
  local near, nd = A.nearest(u, foes)
  if near and nd < 192 then
    local rx, ry = A.retreat(u, foes, 6)
    hx.order(u, "move", rx, ry)
    return
  end
  hx.order(u, "hold_position")
end

-- assassin: goes round the line for the casters and the workers, and takes the fight it was
-- sent for rather than the one in front of it
function LsBrainAssassin(e)
  local u = e.unit
  if held(u) then return end
  local foes = A.foes(u, 384)
  local cs = A.casters(foes)
  if #cs > 0 then
    local t = A.nearest(u, cs)
    hx.order(u, "attack_unit", t)
    return
  end
  local ws = A.workers(foes)
  if #ws > 0 then
    local t = A.nearest(u, ws)
    hx.order(u, "attack_unit", t)
    return
  end
  local weak = A.weakest(foes)
  if weak then
    hx.order(u, "attack_unit", weak)
    return
  end
  pushOn(u)
end

-- siege: stands off and shells. It stops at the edge of its reach and holds, which is how the
-- Siege Breaker comes to sit in the warning band.
function LsBrainSiege(e)
  local u = e.unit
  if held(u) then return end
  local x, y = A.at(u)
  local foes = A.enemiesAt(u, x, y, 384)
  if #foes == 0 then
    local b = A.buildingsOf(u, x, y, 448)
    if #b > 0 then
      local c = A.cluster(b, 96)
      if c then
        local tx, ty = A.toward(u, c.x, c.y, 32)
        hx.order(u, "attack_move", tx, ty)
        return
      end
    end
    pushOn(u)
    return
  end
  local near, nd = A.nearest(u, foes)
  if near and nd < 160 then
    local rx, ry = A.retreat(u, foes, 5)
    hx.order(u, "move", rx, ry)
    return
  end
  local rich = A.richest(A.hittable(u, foes))
  if rich then
    hx.order(u, "attack_unit", rich)
  else
    pushOn(u)
  end
end
