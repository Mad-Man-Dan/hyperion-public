// design_paths.cpp -- where units are going: each one's route as pathfinding left it, drawn over
// the map in the window (design_paths) and given as points to a tool (paths). A route is the
// unit's place, the point it steers to now (CUnit::nextWaypoint), the waypoints of its path still
// ahead (sai_path's packed points from c1F up to cRegionCount), the middles of the regions it is to
// cross after them (the run of region ids that follows, from cStepIndex), and its goal (the
// path's pt08, or the move target of a unit with no path: a flyer goes straight). The waypoints
// are exact; the regions' middles only show which way the rest of the route goes, since the
// game finds the waypoints through them when it gets there. Presentation only: this reads the
// game and writes nothing of it.
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "CUnit.h"
#include "sai.h"       // GetRegionPosition
#include "sai_path.h"
#include "../port/platform/port_json.h"
#include "../port/tools/port_benchinfo.h"  // PortBenchSelected
#include "../port/tools/port_control.h"
#include "design_engine.h"
#include "design_hooks.h"

namespace {

enum PointKind { AT_UNIT = 0, AT_STEER = 1, AT_WAYPOINT = 2, AT_REGION = 3, AT_GOAL = 4, AT_TARGET = 5 };
const char *const kKindNames[] = {"unit", "steer", "waypoint", "region", "goal", "target"};

struct Point {
  int x, y, kind;
};
struct Route {
  int nIndex, nType, nPlayer;
  std::vector<Point> points;
};

enum Which { WHICH_SELECTED, WHICH_ALL, WHICH_LIST };

bool g_bOn;
Which g_which = WHICH_SELECTED;
std::vector<int> g_list;
int g_nPlayer = -1;  // every player's

void Add(std::vector<Point> *pPoints, int x, int y, int kind) {
  if (!pPoints->empty() && pPoints->back().x == x && pPoints->back().y == y)
    return;
  pPoints->push_back({x, y, kind});
}

// One unit's route, false when it is not going anywhere.
bool RouteOf(const CUnit *pUnit, Route *pRoute) {
  pRoute->nIndex = static_cast<int>(pUnit - CUnit::Head());
  pRoute->nType = pUnit->wType;
  pRoute->nPlayer = pUnit->bPlayer;
  std::vector<Point> &pts = pRoute->points;
  pts.clear();
  const int nX = pUnit->wPositionX, nY = pUnit->wPositionY;
  const int nGoalX = pUnit->moveTarget.x, nGoalY = pUnit->moveTarget.y;
  const sai_path *pPath = pUnit->pPath;
  if (pPath == NULL && nGoalX == nX && nGoalY == nY)
    return false;
  Add(&pts, nX, nY, AT_UNIT);
  Add(&pts, pUnit->nextWaypoint.x, pUnit->nextWaypoint.y, AT_STEER);
  if (pPath != NULL) {
    const int nCount = pPath->cRegionCount, nAt = pPath->c1F;
    const int nMost = static_cast<int>(sizeof pPath->adwRegions / sizeof pPath->adwRegions[0]);
    if (nCount >= 0 && nCount <= nMost && nAt >= 0) {
      for (int i = nAt; i < nCount; ++i)
        Add(&pts, PosPackedX(pPath->adwRegions[i]), PosPackedY(pPath->adwRegions[i]), AT_WAYPOINT);
      // When the waypoints stop short of the goal, the regions the way goes on through, from the
      // region ids after the waypoints (as many as the node holds). They run from the path's
      // start, so the ones before the region nearest the last waypoint are left out.
      const Point last = pts.back();
      const WORD *pwSteps = reinterpret_cast<const WORD *>(&pPath->adwRegions[nCount]);
      const int nSteps = pPath->cStepCount, nRoom = (nMost - nCount) * 2;
      const int nEnd = nSteps < nRoom ? nSteps : nRoom;
      if (last.x != pPath->pt08.x || last.y != pPath->pt08.y) {
        static mpoint_t s_centres[(sizeof pPath->adwRegions / sizeof pPath->adwRegions[0]) * 2];
        int nNearest = 0;
        long long nBest = -1;
        for (int i = 0; i < nEnd; ++i) {
          GetRegionPosition(pwSteps[i], &s_centres[i]);
          const long long dx = s_centres[i].x - last.x, dy = s_centres[i].y - last.y;
          if (nBest < 0 || dx * dx + dy * dy < nBest) {
            nBest = dx * dx + dy * dy;
            nNearest = i;
          }
        }
        for (int i = nNearest + 1; i < nEnd; ++i)
          Add(&pts, s_centres[i].x, s_centres[i].y, AT_REGION);
      }
    }
    Add(&pts, pPath->pt08.x, pPath->pt08.y, AT_GOAL);
  } else {
    Add(&pts, nGoalX, nGoalY, AT_GOAL);
  }
  pts.back().kind = AT_GOAL;
  // what the order aims at, when it is a unit
  if (pUnit->pOrderTarget != NULL)
    pts.push_back({pUnit->pOrderTarget->wPositionX, pUnit->pOrderTarget->wPositionY, AT_TARGET});
  return pts.size() >= 2;
}

void Routes(Which which, const std::vector<int> &list, int nPlayer, std::vector<Route> *pOut) {
  pOut->clear();
  if (!DesignInGame())
    return;
  Route r;
  auto take = [&](const CUnit *pUnit) {
    if (pUnit != NULL && (nPlayer < 0 || pUnit->bPlayer == nPlayer) && RouteOf(pUnit, &r))
      pOut->push_back(r);
  };
  if (which == WHICH_ALL) {
    for (const CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
      take(pUnit);
  } else if (which == WHICH_SELECTED) {
    static PortBenchUnit s_units[PORT_BENCH_SELECT_MAX];
    const int n = PortBenchSelected(s_units, PORT_BENCH_SELECT_MAX);
    for (int i = 0; i < n; ++i)
      take(CUnit::Head() + s_units[i].nIndex);
  } else {
    PortBenchUnit u;
    for (int nIndex : list)
      if (PortBenchUnitRead(nIndex, &u))
        take(CUnit::Head() + nIndex);
  }
}

// units: "selected", "all" or a list of unit ids; player: one player's only.
bool Choice(const PortJson &j, Which *pWhich, std::vector<int> *pList, int *pnPlayer, std::string *pError) {
  const PortJson &u = j["units"];
  if (u.kind == PortJson::STRING) {
    if (u.s == "selected") {
      *pWhich = WHICH_SELECTED;
    } else if (u.s == "all") {
      *pWhich = WHICH_ALL;
    } else {
      *pError = "units is \"selected\", \"all\" or a list of unit ids";
      return false;
    }
  } else if (u.kind == PortJson::ARRAY) {
    *pWhich = WHICH_LIST;
    pList->clear();
    for (const PortJson &id : u.items)
      pList->push_back(static_cast<int>(id.Num(-1)));
  }
  if (j["player"].kind == PortJson::NUMBER)
    *pnPlayer = static_cast<int>(j["player"].d);
  return true;
}

std::string WhichName(void) {
  if (g_which == WHICH_ALL)
    return "\"all\"";
  if (g_which == WHICH_SELECTED)
    return "\"selected\"";
  std::string s = "[";
  for (size_t i = 0; i < g_list.size(); ++i)
    s += (i ? "," : "") + std::to_string(g_list[i]);
  return s + "]";
}

bool CmdDesignPaths(const PortJson &j, std::string *pAnswer) {
  // with nothing given, only the state; given units or a player, on unless it says otherwise
  if (j["on"].kind == PortJson::NONE && j["units"].kind == PortJson::NONE && j["player"].kind == PortJson::NONE) {
    *pAnswer = DesignPathsFields();
    return true;
  }
  if (!Choice(j, &g_which, &g_list, &g_nPlayer, pAnswer))
    return false;
  g_bOn = j["on"].kind == PortJson::BOOL ? j["on"].b : true;
  *pAnswer = DesignPathsFields();
  return true;
}

bool CmdPaths(const PortJson &j, std::string *pAnswer) {
  Which which = WHICH_SELECTED;
  std::vector<int> list;
  int nPlayer = -1;
  if (!Choice(j, &which, &list, &nPlayer, pAnswer))
    return false;
  if (!DesignInGame()) {
    *pAnswer = "paths needs a single-player game";
    return false;
  }
  std::vector<Route> routes;
  Routes(which, list, nPlayer, &routes);
  std::string s = "\"units\":[";
  for (size_t i = 0; i < routes.size(); ++i) {
    const Route &r = routes[i];
    s += std::string(i ? "," : "") + "{\"id\":" + std::to_string(r.nIndex) + ",\"type\":" + std::to_string(r.nType) +
         ",\"player\":" + std::to_string(r.nPlayer) + ",\"points\":[";
    for (size_t k = 0; k < r.points.size(); ++k) {
      const Point &p = r.points[k];
      s += std::string(k ? "," : "") + "[" + std::to_string(p.x) + "," + std::to_string(p.y) + ",\"" +
           kKindNames[p.kind] + "\"]";
    }
    s += "]}";
  }
  *pAnswer = s + "]";
  return true;
}

// A line in the player's colour over a dark edge, solid or dashed, in the target's pixels.
void Line(std::vector<DesignPrim> *pOut, const DesignCamera &cam, float x0, float y0, float x1, float y1,
          const float *pafRgb, float fWidth, bool bDashed) {
  const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f)
    return;
  const float on = bDashed ? 7.0f * cam.fSx : len, off = bDashed ? 5.0f * cam.fSx : 0.0f;
  for (float at = 0.0f; at < len; at += on + off) {
    const float a = at / len, b = fminf(at + on, len) / len;
    DesignPrim p;
    DesignPrimClear(&p, DESIGN_PRIM_LINE, false, cam.afMap);
    p.head[3] = 3.0f;  // round at both ends, so the bends of a route close
    p.at[0] = x0 + dx * a;
    p.at[1] = y0 + dy * a;
    p.at[2] = x0 + dx * b;
    p.at[3] = y0 + dy * b;
    p.axes[0] = fWidth;
    p.axes[2] = fWidth + 2.5f;
    for (int c = 0; c < 3; ++c)
      p.c1[c] = pafRgb[c];
    p.c1[3] = 0.95f;
    p.c2[3] = 0.5f;  // the edge black, so a line shows on any ground
    pOut->push_back(p);
  }
}

// A ring flat on the ground, as the game's art draws it, or a dot with fInner 0.
void Circle(std::vector<DesignPrim> *pOut, const DesignCamera &cam, float x, float y, float fRadius, float fInner,
            const float *pafRgb, float fAlpha) {
  DesignPrim p;
  DesignPrimClear(&p, DESIGN_PRIM_RING, false, cam.afMap);
  p.at[0] = x;
  p.at[1] = y;
  p.axes[0] = fRadius;
  p.axes[3] = fInner > 0.0f ? fRadius * 0.6f : fRadius;
  p.ring[0] = fInner;
  for (int c = 0; c < 3; ++c)
    p.c0[c] = pafRgb[c];
  p.c0[3] = fAlpha;
  pOut->push_back(p);
}

}  // namespace

void DesignPathsInit(void) {
  PortControlCommand("design_paths", CmdDesignPaths);
  PortControlCommand("paths", CmdPaths);
}

std::string DesignPathsFields(void) {
  return "\"paths\":{\"on\":" + std::string(g_bOn ? "true" : "false") + ",\"units\":" + WhichName() +
         ",\"player\":" + std::to_string(g_nPlayer) + "}";
}

void DesignPathsBuild(const DesignCamera &cam, std::vector<DesignPrim> *pOut) {
  if (!g_bOn)
    return;
  static std::vector<Route> s_routes;
  Routes(g_which, g_list, g_nPlayer, &s_routes);
  const float fWidth = fmaxf(1.5f, 1.25f * cam.fSx);
  const float white[3] = {1.0f, 1.0f, 1.0f};
  for (const Route &r : s_routes) {
    float rgb[3];
    DesignPlayerRgb(r.nPlayer, rgb);
    float px = 0.0f, py = 0.0f;
    bool bEstimate = false;
    for (size_t i = 0; i < r.points.size(); ++i) {
      const Point &q = r.points[i];
      float x, y;
      DesignProject(cam, q.x, q.y, 0.0, &x, &y);
      if (q.kind == AT_TARGET) {
        // what the order aims at, from the unit, thin and dashed
        float ux, uy;
        DesignProject(cam, r.points[0].x, r.points[0].y, 0.0, &ux, &uy);
        Line(pOut, cam, ux, uy, x, y, white, fWidth * 0.6f, true);
        Circle(pOut, cam, x, y, 7.0f * cam.fPerMap, 0.7f, white, 0.8f);
        continue;
      }
      if (i > 0) {
        bEstimate = bEstimate || q.kind == AT_REGION;
        Line(pOut, cam, px, py, x, y, rgb, fWidth, bEstimate);
      }
      if (q.kind == AT_WAYPOINT)
        Circle(pOut, cam, x, y, 2.2f * cam.fSx, 0.0f, rgb, 0.95f);
      else if (q.kind == AT_GOAL)
        Circle(pOut, cam, x, y, 8.0f * cam.fPerMap, 0.72f, rgb, 0.95f);
      px = x;
      py = y;
    }
  }
}
