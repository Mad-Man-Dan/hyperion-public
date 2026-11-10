// port_mapmark.cpp -- the marks a mod's scripts draw on the map (port_mapmark.h): kept here as
// the scripts set them, and drawn over the game as a layer of the overlay, in the window's own
// resolution and through the camera, so a mark sits on the ground and moves and scales with it.
//
// A mark is drawn as its edge cut into pieces, each in map pixels and turned into the target's
// where it is drawn: that is what lets a dash, a fade and a glow round one unit fall on parts of
// the same edge. A shape with neither, which is most of them, is drawn in one piece instead, so
// a plain circle is as round as the kit can draw it.
//
// Presentation only. The simulation never reads a mark, none of this is saved or sent, and a
// game plays and records the same with the marks or without them. The fade is measured against
// what the player watching can see, so each seat draws its own and nothing shows through fog.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_mapmark.h"
#include "port_overlay.h"            // PORT_OVERLAY_LAYER
#include "port_view.h"               // the camera, and the room the map is drawn in
#include "port_teamcolor.h"          // a player's colour in full RGB
#include "port_scene.h"              // the ground's paint, as a primitive of the scene
#include "port_gpu.h"                // whether the pass drew the map in full colour
#include "../front/port_ui.h"        // UiFill, UiRing, UiLine
#include "../hud/port_hud.h"         // PortHudSides, a design's side columns
#include "CUnit.h"                   // CUnit::GetUsedList, the units that reveal a mark
#include "CSprite.h"                 // their places
#include "CUnitMisc.h"               // IsEnemyUnit, for a fade the allies share
#include "playervision.h"            // SpriteSeenBy
#include "playertables.h"            // PlayerSlots
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

const int kMarksMax = 32;
const double kPieceMost = 256;     // the pieces an edge is cut into at most
const double kPiecePixels = 6.0;   // and how long each one is on the screen, in pixels
const double kRoundLeast = 12;     // the pieces a circle is cut into at least, so it stays round
const int kFillRows = 480;         // the rows a polygon's inside is filled in at most
const Uint64 kGoMs = 500;          // how long a mark cleared takes to fade away
const double kPi = 3.14159265358979323846;

const char *const kShapeNames[MAP_MARK_SHAPES] = {"circle", "ring", "rect", "polygon", "line"};

const int kEventsMost = 8;        // the crossings a mark remembers
const Uint64 kEventMs = 2000;     // and how long each is answered for

// A unit crossing the mark's edge: where on the edge it happened, how far along the edge that
// is, when, whether the unit was going out, and the way it was going -- its step's direction in
// map pixels, and how much of that step ran along the edge rather than through it, from -1
// against the edge's own direction to 1 with it. The blocks that answer a crossing are turned by
// those, so a unit leaving and a unit arriving read differently at any angle round the shape.
// Presentation only, like the rest of a mark: it is noticed by watching the units the player can
// see, frame to frame.
struct Event {
  double fAt = 0, x = 0, y = 0;
  double wx = 0, wy = 0, fWay = 0;
  Uint64 nAt = 0;
  bool bOut = false;
};

// A unit the mark is watching: which side of the edge it was on when last looked at, and where
// it stood, from which the step it took is worked out when it changes sides.
struct Watch {
  unsigned int nId;
  double x, y;
  bool bIn;
};

struct Mark {
  PortMapMark m;
  Uint64 nAt = 0;  // when it was set, which its pulse is measured from
  Uint64 nGo = 0;  // when it was cleared, over which it fades away; 0 while it stands
  std::vector<Watch> watch;    // the units on each side of its edge, for the crossings
  std::vector<Event> events;   // and the crossings it is still answering
};

std::vector<Mark> g_marks;

// A unit that can reveal a mark: its place and its owner, gathered once a frame.
struct Near {
  double x, y;
  unsigned int nId;  // the unit itself, to tell it from frame to frame (the crossings)
  int nPlayer;
  bool bSeen;   // the player watching can see it
  bool bAlly;   // and is not at war with its owner
};

std::vector<Near> g_near;
Uint64 g_nNearAt;  // the tick they were gathered at: a frame gathers them once, for every pass

// The player watching, or -1 for a viewer of no side (an observer, a replay watched whole), for
// whom every unit reveals and nothing is hidden.
int Viewer(void)
{
  const int n = static_cast<int>(g_LocalNationID);
  return n >= 0 && n < PlayerSlots() ? n : -1;
}

// ---- the camera -----------------------------------------------------------------------------

// Where the map is drawn and how it is scaled, for a frame: the game's box in the target
// (fX, fY, fW), the camera over it, and the part of the box the map shows in.
struct Room {
  double fCamX, fCamY;  // the map pixel at the game view's top left
  double fZoom;         // map pixels a pixel of the game's view
  double fPer;          // the target's pixels a map pixel
  double fX, fY;        // where the game view's top left sits in the target
  float afClip[4];      // the map's own box in the target: x0, y0, x1, y1
};

bool RoomOf(float fX, float fY, float fW, float fH, Room *pRoom)
{
  if (g_portView.nWidth <= 0 || g_portView.nHeight <= 0 || fW <= 0.0f || fH <= 0.0f)
    return false;
  PortViewCamera(&pRoom->fCamX, &pRoom->fCamY, &pRoom->fZoom);
  if (pRoom->fZoom <= 0.0)
    return false;
  const double fScale = fW / g_portView.nWidth;  // the target's pixels a game pixel
  pRoom->fPer = fScale / pRoom->fZoom;
  pRoom->fX = fX;
  pRoom->fY = fY;
  // the map's room: beside a HUD design's side columns, and down to the rows the map is drawn
  // in, which a classic console ends above and a design across the foot keeps to as well
  double fLeft = 0, fRight = 0;
  PortHudSides(&fLeft, &fRight);
  const double k = g_portView.nHeight / 1080.0;
  fLeft *= k;
  fRight *= k;
  const double fRows = fLeft + fRight > 0.0 ? g_portView.nMapRows : g_portView.nViewRows;
  pRoom->afClip[0] = static_cast<float>(fX + fLeft * fScale);
  pRoom->afClip[1] = static_cast<float>(fY);
  pRoom->afClip[2] = static_cast<float>(fX + (g_portView.nWidth - fRight) * fScale);
  pRoom->afClip[3] = static_cast<float>(fY + fRows * fScale);
  return pRoom->afClip[2] > pRoom->afClip[0] && pRoom->afClip[3] > pRoom->afClip[1];
}

// A map pixel where the camera puts it in the target.
float AtX(const Room &room, double mx) { return static_cast<float>(room.fX + (mx - room.fCamX) * room.fPer); }
float AtY(const Room &room, double my) { return static_cast<float>(room.fY + (my - room.fCamY) * room.fPer); }

// ---- a mark's edge --------------------------------------------------------------------------

// A piece of an edge, in map pixels, with how far along the edge its middle lies (a dash).
struct Piece {
  double x0, y0, x1, y1, fAt;
};

int Pieces(double fLength, double fPer)
{
  const double n = fLength * fPer / kPiecePixels;
  return static_cast<int>(SDL_clamp(n, 1.0, kPieceMost));
}

void CutLine(double x0, double y0, double x1, double y1, double fPer, double *pfAt,
             std::vector<Piece> *pOut)
{
  const double fLength = SDL_sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
  if (fLength <= 0.0)
    return;
  const int n = Pieces(fLength, fPer);
  for (int i = 0; i < n; ++i) {
    const double a = static_cast<double>(i) / n, b = static_cast<double>(i + 1) / n;
    Piece p;
    p.x0 = x0 + (x1 - x0) * a;
    p.y0 = y0 + (y1 - y0) * a;
    p.x1 = x0 + (x1 - x0) * b;
    p.y1 = y0 + (y1 - y0) * b;
    p.fAt = *pfAt + fLength * (a + b) * 0.5;
    pOut->push_back(p);
  }
  *pfAt += fLength;
}

// The mark's edge, in map pixels: a circle's and a ring's round their centre, a rectangle's
// four sides, a polygon's sides closed, a line's one stroke.
void Edge(const PortMapMark &m, double fPer, std::vector<Piece> *pOut)
{
  pOut->clear();
  double fAt = 0;
  switch (m.nShape) {
    case MAP_MARK_CIRCLE:
    case MAP_MARK_RING: {
      if (m.r <= 0.0)
        return;
      const int n = SDL_max(static_cast<int>(kRoundLeast), Pieces(2.0 * kPi * m.r, fPer));
      for (int i = 0; i < n; ++i) {
        const double a = 2.0 * kPi * i / n, b = 2.0 * kPi * (i + 1) / n;
        Piece p;
        p.x0 = m.x + SDL_cos(a) * m.r;
        p.y0 = m.y + SDL_sin(a) * m.r;
        p.x1 = m.x + SDL_cos(b) * m.r;
        p.y1 = m.y + SDL_sin(b) * m.r;
        p.fAt = m.r * (a + b) * 0.5;
        pOut->push_back(p);
      }
      break;
    }
    case MAP_MARK_RECT: {
      if (m.w <= 0.0 || m.h <= 0.0)
        return;
      const double x1 = m.x + m.w, y1 = m.y + m.h;
      CutLine(m.x, m.y, x1, m.y, fPer, &fAt, pOut);
      CutLine(x1, m.y, x1, y1, fPer, &fAt, pOut);
      CutLine(x1, y1, m.x, y1, fPer, &fAt, pOut);
      CutLine(m.x, y1, m.x, m.y, fPer, &fAt, pOut);
      break;
    }
    case MAP_MARK_POLYGON: {
      const size_t n = m.points.size() / 2;
      if (n < 2)
        return;
      for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        CutLine(m.points[i * 2], m.points[i * 2 + 1], m.points[j * 2], m.points[j * 2 + 1], fPer, &fAt, pOut);
      }
      break;
    }
    default:
      CutLine(m.x, m.y, m.x2, m.y2, fPer, &fAt, pOut);
      break;
  }
}

// How long the whole edge is, in map pixels, which a ripple's distance along it wraps on.
double EdgeLen(const std::vector<Piece> &pieces)
{
  double f = 0;
  for (const Piece &p : pieces)
    f += SDL_sqrt((p.x1 - p.x0) * (p.x1 - p.x0) + (p.y1 - p.y0) * (p.y1 - p.y0));
  return f;
}

// Whether the shape is one with an inside, so crossing its edge means going in or coming out. A
// line's sides are its own: one of them counts as in, the side its stroke faces away from.
bool Closed(int nShape) { return nShape != MAP_MARK_LINE; }

// How far outside the mark a point is, in map pixels, under 0 inside it.
double Outside(const PortMapMark &m, double x, double y)
{
  switch (m.nShape) {
    case MAP_MARK_CIRCLE:
    case MAP_MARK_RING:
      return SDL_sqrt((x - m.x) * (x - m.x) + (y - m.y) * (y - m.y)) - m.r;
    case MAP_MARK_RECT: {
      const double hw = m.w * 0.5, hh = m.h * 0.5;
      const double qx = SDL_fabs(x - (m.x + hw)) - hw, qy = SDL_fabs(y - (m.y + hh)) - hh;
      const double fOut = SDL_sqrt(SDL_max(qx, 0.0) * SDL_max(qx, 0.0) + SDL_max(qy, 0.0) * SDL_max(qy, 0.0));
      return fOut + SDL_min(SDL_max(qx, qy), 0.0);
    }
    case MAP_MARK_POLYGON: {
      const size_t n = m.points.size() / 2;
      if (n < 3)
        return 1.0;
      double d = 1e9;
      bool bIn = false;
      for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        const double ax = m.points[i * 2], ay = m.points[i * 2 + 1];
        const double bx = m.points[j * 2], by = m.points[j * 2 + 1];
        const double ex = bx - ax, ey = by - ay;
        const double t = SDL_clamp((ex * (x - ax) + ey * (y - ay)) / SDL_max(ex * ex + ey * ey, 1e-6), 0.0, 1.0);
        const double px = x - (ax + ex * t), py = y - (ay + ey * t);
        d = SDL_min(d, SDL_sqrt(px * px + py * py));
        if ((ay > y) != (by > y) && x < ax + (bx - ax) * (y - ay) / (by - ay))
          bIn = !bIn;
      }
      return bIn ? -d : d;
    }
    default: {
      // a line: which side of the stroke, so a unit walking over it crosses once
      const double ex = m.x2 - m.x, ey = m.y2 - m.y;
      const double fLen = SDL_sqrt(ex * ex + ey * ey);
      if (fLen <= 0.0)
        return 1.0;
      return ((x - m.x) * -ey + (y - m.y) * ex) / fLen;
    }
  }
}

// The point of the edge nearest x, y, how far along the edge it lies, and which way the edge
// runs there, as a direction of its own length (0, 0 for an edge of no length at all).
void NearestOn(const std::vector<Piece> &pieces, double x, double y, double *pfAt, double *pfX, double *pfY,
               double *pfDx, double *pfDy)
{
  double fBest = 1e9;
  *pfAt = 0, *pfX = x, *pfY = y, *pfDx = 0, *pfDy = 0;
  for (const Piece &p : pieces) {
    const double ex = p.x1 - p.x0, ey = p.y1 - p.y0;
    const double t = SDL_clamp((ex * (x - p.x0) + ey * (y - p.y0)) / SDL_max(ex * ex + ey * ey, 1e-6), 0.0, 1.0);
    const double qx = p.x0 + ex * t, qy = p.y0 + ey * t;
    const double d = (x - qx) * (x - qx) + (y - qy) * (y - qy);
    if (d < fBest) {
      fBest = d;
      *pfAt = p.fAt;
      *pfX = qx;
      *pfY = qy;
      const double fLen = SDL_sqrt(ex * ex + ey * ey);
      *pfDx = fLen > 0.0 ? ex / fLen : 0.0;
      *pfDy = fLen > 0.0 ? ey / fLen : 0.0;
    }
  }
}

// ---- the fade -------------------------------------------------------------------------------

// Whether a unit's owner reveals this mark.
bool Reveals(const PortMapMark &m, const Near &u, int nViewer)
{
  if (!u.bSeen)
    return false;
  switch (m.nWho) {
    case MAP_MARK_WHO_ALL:
      return true;
    case MAP_MARK_WHO_ALLIES:
      return u.bAlly;
    case MAP_MARK_WHO_LIST:
      return std::find(m.who.begin(), m.who.end(), u.nPlayer) != m.who.end();
    default:
      return nViewer < 0 || u.nPlayer == nViewer;
  }
}

// The light at a part of the fade's distance, 1 on the unit and 0 at the far end of it: smooth
// eases away, even falls in a straight line, and sharp holds full past half the way and then
// drops, which keeps a mark dark until a unit is nearly on it.
double Falloff(double x, int nCurve)
{
  if (x >= 1.0)
    return 0.0;
  if (x <= 0.0)
    return 1.0;
  if (nCurve == MAP_MARK_CURVE_EVEN)
    return 1.0 - x;
  if (nCurve == MAP_MARK_CURVE_SHARP) {
    if (x < 0.55)
      return 1.0;
    const double t = 1.0 - (x - 0.55) / 0.45;
    return t * t;
  }
  const double t = 1.0 - x;
  return t * t * (3.0 - 2.0 * t);
}

// How much a point of the edge is revealed, 0 to 1: 1 with a unit on it, falling away over the
// fade's distance by its curve.
double Reveal(const PortMapMark &m, const std::vector<const Near *> &units, double x, double y)
{
  if (m.fNear <= 0.0)
    return units.empty() ? 0.0 : 1.0;
  double fBest = 0.0;
  for (const Near *p : units) {
    const double dx = p->x - x, dy = p->y - y;
    const double d = SDL_sqrt(dx * dx + dy * dy);
    if (d >= m.fNear)
      continue;
    fBest = SDL_max(fBest, Falloff(d / m.fNear, m.nCurve));
    if (fBest >= 1.0)
      break;
  }
  return fBest;
}

// The opacity a reveal of 0 to 1 gives: the fade's least with nothing near, its most on the unit.
double Opacity(const PortMapMark &m, double fReveal)
{
  if (!m.bFade)
    return 1.0;
  return m.fLeast + (m.fMost - m.fLeast) * fReveal;
}

// A mark cleared fades away rather than going at once: 1 while it stands, falling to 0 over
// half a second, and below 0 it is drawn no more and dropped.
double Going(const Mark &mark, Uint64 nNow)
{
  if (mark.nGo == 0)
    return 1.0;
  const Uint64 nSince = nNow - mark.nGo;
  return nSince >= kGoMs ? 0.0 : 1.0 - static_cast<double>(nSince) / kGoMs;
}

// The swell of a pulse at a point of the edge, fAt along it: 1 at the beat's fullest, falling
// to one depth less at its lowest. It travels along the edge, so a long edge shows the swell as
// a wave running round it and a short one swells all together.
double Pulse(const Mark &mark, Uint64 nNow, double fAt)
{
  if (mark.m.fPulse <= 0.0 || mark.m.fDepth <= 0.0)
    return 1.0;
  const double t = (nNow - mark.nAt) / 1000.0;
  return 1.0 - mark.m.fDepth * 0.5 * (1.0 + SDL_sin(t * 2.0 * kPi / mark.m.fPulse - fAt / 90.0));
}

// How far one point of the edge lies from another along it, signed: over 0 where it lies further
// along the edge's own direction. It wraps round a closed shape, so a ripple leaving one side
// arrives at the other, and the side it arrives on is the near one either way.
double Along(double a, double b, double fLen, bool bClosed)
{
  double d = a - b;
  if (bClosed && fLen > 0.0) {
    if (d > fLen * 0.5)
      d -= fLen;
    else if (d < -fLen * 0.5)
      d += fLen;
  }
  return d;
}

// What the crossings add at a point of the edge, 0 to 1, and in *pfWarm how much of that came
// from a unit going out, which is drawn in the warning colour: the ripple block's wave running
// away from where it happened, and its flash at the place itself, each dying over its own life.
double Boost(const Mark &mark, double fAt, double fLen, Uint64 nNow, double *pfWarm)
{
  const PortMarkRipple &r = mark.m.look.ripple;
  double fBoost = 0, fWarm = 0;
  if (mark.m.nCross == MAP_MARK_CROSS_RIPPLE)
    for (const Event &e : mark.events) {
      const double tau = (nNow - e.nAt) / 1000.0;
      const double fFrom = Along(fAt, e.fAt, fLen, Closed(mark.m.nShape));
      const double ds = SDL_fabs(fFrom);
      const double a = (ds - tau * r.fSpeed) / SDL_max(r.fWide, 1e-3);
      const double b = ds / SDL_max(r.fFlash, 1e-3);
      // the wave runs away from the place both ways alike; the flash at the place leans to the
      // side the unit was heading for, so a crossing taken at an angle shows which way it went
      const double fSide = SDL_max(0.0, 1.0 + r.fSide * e.fWay * (fFrom >= 0.0 ? 1.0 : -1.0));
      const double f = SDL_exp(-a * a) * SDL_max(0.0, 1.0 - tau / SDL_max(r.fLife, 1e-3)) +
                       SDL_exp(-b * b) * SDL_max(0.0, 1.0 - tau / SDL_max(r.fFlashLife, 1e-3)) * fSide;
      fBoost = SDL_max(fBoost, f);
      if (e.bOut)
        fWarm = SDL_max(fWarm, f);
    }
  if (pfWarm != NULL)
    *pfWarm = SDL_min(fWarm, 1.0);
  return SDL_min(fBoost, 1.0);
}

// Whether the pulse runs along the edge rather than lifting it all together, which is any pulse
// of a depth: such a mark is drawn piece by piece, since each piece is at its own point of the
// wave.
bool Wave(const PortMapMark &m) { return m.fPulse > 0.0 && m.fDepth > 0.0; }

// ---- the colour -----------------------------------------------------------------------------

// the slots' colours as the game gives them, where no colour of a player's own is set (the
// benchmark's minimap draws them the same way)
const unsigned int kSlots[12] = {0xf40404, 0x0c48cc, 0x2cb494, 0x88409c, 0xf88c14, 0x703014,
                                 0xcce0d0, 0xfcfc38, 0x10fc18, 0x7cacfc, 0x00e4fc, 0x006400};

// Two colours mixed, as the paint mixes them: the warning colour over the mark's own by how much
// a unit on its way out has lit this stretch of edge.
unsigned int Mix(unsigned int nFrom, unsigned int nTo, double f)
{
  const double t = SDL_clamp(f, 0.0, 1.0);
  unsigned int n = 0;
  for (int i = 16; i >= 0; i -= 8) {
    const double a = (nFrom >> i) & 0xFF, b = (nTo >> i) & 0xFF;
    n |= static_cast<unsigned int>(a + (b - a) * t + 0.5) << i;
  }
  return n;
}

unsigned int Rgb(const PortMapMark &m)
{
  if (m.nPlayer < 0)
    return m.nRgb;
  unsigned char abRgb[3];
  if (PortTeamColorGet(m.nPlayer, abRgb) != 0)
    return (static_cast<unsigned int>(abRgb[0]) << 16) | (static_cast<unsigned int>(abRgb[1]) << 8) | abRgb[2];
  return m.nPlayer < 12 ? kSlots[m.nPlayer] : 0xb0b0b0u;
}

// ---- the drawing ----------------------------------------------------------------------------

// A polygon's inside, as rows of it: each row's crossings in pairs. The kit fills rectangles,
// so a shape of any outline is filled in strips a few pixels tall.
void FillPoly(const Room &room, const PortMapMark &m, UiColor c)
{
  const size_t n = m.points.size() / 2;
  if (n < 3)
    return;
  float fTop = 0, fFoot = 0;
  for (size_t i = 0; i < n; ++i) {
    const float y = AtY(room, m.points[i * 2 + 1]);
    fTop = i == 0 ? y : SDL_min(fTop, y);
    fFoot = i == 0 ? y : SDL_max(fFoot, y);
  }
  fTop = SDL_max(fTop, room.afClip[1]);
  fFoot = SDL_min(fFoot, room.afClip[3]);
  if (fFoot <= fTop)
    return;
  const float fStep = SDL_max(2.0f, (fFoot - fTop) / kFillRows);
  std::vector<float> afCross;
  for (float y = fTop; y < fFoot; y += fStep) {
    const float fMid = y + fStep * 0.5f;
    afCross.clear();
    for (size_t i = 0; i < n; ++i) {
      const size_t j = (i + 1) % n;
      const float ay = AtY(room, m.points[i * 2 + 1]), by = AtY(room, m.points[j * 2 + 1]);
      if ((ay <= fMid) == (by <= fMid) || ay == by)
        continue;
      const float ax = AtX(room, m.points[i * 2]), bx = AtX(room, m.points[j * 2]);
      afCross.push_back(ax + (bx - ax) * (fMid - ay) / (by - ay));
    }
    std::sort(afCross.begin(), afCross.end());
    for (size_t k = 0; k + 1 < afCross.size(); k += 2)
      if (afCross[k + 1] > afCross[k])
        UiFill(afCross[k], y, afCross[k + 1] - afCross[k], fStep, c);
  }
}

// A mark's inside: a circle as a round fill, a rectangle as a plain one, a polygon in strips. A
// ring and a line have none.
void FillMark(const Room &room, const PortMapMark &m, UiColor c)
{
  if (m.nShape == MAP_MARK_CIRCLE && m.r > 0.0) {
    const float r = static_cast<float>(m.r * room.fPer);
    UiFill(AtX(room, m.x) - r, AtY(room, m.y) - r, r * 2.0f, r * 2.0f, c, r);
  } else if (m.nShape == MAP_MARK_RECT && m.w > 0.0 && m.h > 0.0) {
    UiFill(AtX(room, m.x), AtY(room, m.y), static_cast<float>(m.w * room.fPer),
           static_cast<float>(m.h * room.fPer), c);
  } else if (m.nShape == MAP_MARK_POLYGON) {
    FillPoly(room, m, c);
  }
}

// A circle's, a ring's or a rectangle's edge in one piece, which is rounder and crisper than
// pieces of it: for a mark with no dash and no glow of its own round a unit.
bool EdgeWhole(const Room &room, const PortMapMark &m, UiColor c, float fWidth)
{
  if (m.nShape == MAP_MARK_CIRCLE || m.nShape == MAP_MARK_RING) {
    if (m.r <= 0.0)
      return true;
    const float r = static_cast<float>(m.r * room.fPer);
    UiRing(AtX(room, m.x) - r, AtY(room, m.y) - r, r * 2.0f, r * 2.0f, c, r, fWidth);
    return true;
  }
  if (m.nShape == MAP_MARK_RECT) {
    if (m.w <= 0.0 || m.h <= 0.0)
      return true;
    UiRing(AtX(room, m.x), AtY(room, m.y), static_cast<float>(m.w * room.fPer),
           static_cast<float>(m.h * room.fPer), c, 0.0f, fWidth);
    return true;
  }
  return false;
}

// A piece of the edge as it is drawn: where it lies in the target, how far along the edge it is,
// how lit it is at this moment, and how much of that light is a unit on its way out. Worked out
// once a mark a frame, since the plain edge, a look's field and its cells all read it.
//
// A look that stands light up off the edge wants the piece's lie as well: which way it runs,
// which way is out of the mark, and how much of its face a wall standing on it would turn
// toward the viewer -- 1 where the piece runs across the screen and 0 where it runs up it. The
// view is oblique and never turns, so the screen's own down is where the viewer is: a piece's
// facing is as much as its own run is across that.
struct Lit {
  float x0, y0, x1, y1;
  double fAt, fLight, fWarm;
  double fDx, fDy;  // the way it runs, and which way is out, each of its own length
  double fNx, fNy;
  double fFace;
};

// Which side of the piece is outside the mark: the perpendicular, turned to the side the shape
// says is out. A step of a couple of map pixels is enough to tell, and it is the shape's own
// reckoning rather than a winding, so every shape a mark can be answers the same way.
void OutwardOf(const PortMapMark &m, const Piece &p, double fDx, double fDy, double *pfNx, double *pfNy)
{
  const double kStep = 2.0;
  double nx = fDy, ny = -fDx;
  const double mx = (p.x0 + p.x1) * 0.5, my = (p.y0 + p.y1) * 0.5;
  if (Outside(m, mx + nx * kStep, my + ny * kStep) < Outside(m, mx - nx * kStep, my - ny * kStep)) {
    nx = -nx;
    ny = -ny;
  }
  *pfNx = nx;
  *pfNy = ny;
}

void LitEdge(const Room &room, const Mark &mark, const std::vector<Piece> &pieces, double fLen,
             const std::vector<const Near *> &units, double fWhole, bool bGlow, bool bLie, Uint64 nNow,
             std::vector<Lit> *pOut)
{
  const PortMapMark &m = mark.m;
  const double fGoing = Going(mark, nNow);
  pOut->clear();
  pOut->reserve(pieces.size());
  for (const Piece &p : pieces) {
    Lit one;
    one.fAt = p.fAt;
    one.x0 = AtX(room, p.x0);
    one.y0 = AtY(room, p.y0);
    one.x1 = AtX(room, p.x1);
    one.y1 = AtY(room, p.y1);
    one.fDx = one.fDy = one.fNx = one.fNy = 0.0;
    one.fFace = 1.0;
    if (bLie) {
      const double ex = p.x1 - p.x0, ey = p.y1 - p.y0;
      const double fPiece = SDL_sqrt(ex * ex + ey * ey);
      if (fPiece > 0.0) {
        one.fDx = ex / fPiece;
        one.fDy = ey / fPiece;
        one.fFace = SDL_fabs(one.fDx);
        OutwardOf(m, p, one.fDx, one.fDy, &one.fNx, &one.fNy);
      }
    }
    const double fBoost = Boost(mark, p.fAt, fLen, nNow, &one.fWarm);
    const double fReveal = bGlow ? Reveal(m, units, (p.x0 + p.x1) * 0.5, (p.y0 + p.y1) * 0.5) : fWhole;
    one.fLight = Opacity(m, SDL_max(fReveal, fBoost)) * fGoing * Pulse(mark, nNow, p.fAt);
    pOut->push_back(one);
  }
}

// ---- the blocks that stand above the ground --------------------------------------------------
//
// The field standing up the screen from the edge, the cells drifting in it and a tear where a
// unit came through: the blocks of the look the mod's files gave the mark, each with its own
// values. They are drawn over the units, the ground's paint having no way to stand light up off
// the ground; the foot is the mark's own edge and is drawn with it.

const int kTearSteps = 8;    // the rows a tear's fading column is drawn in
const int kCellsMost = 256;  // the sets of cells one mark is drawn with, however long its edge

// How a block standing on a piece of edge leans, for a height of one pixel: up the screen, and
// out along the piece's own normal by the look's lean. The lean is taken across the screen only.
// Up the screen is both "tall" and "north" in this view, so a wall on an edge running up the
// screen has no width of its own to be drawn with and a wall on one running across it already
// has its whole face: leaning across gives the first its face back and leaves the second alone.
void LeanOf(const Lit &one, double fLean, double *pux, double *puy)
{
  *pux = one.fNx * fLean;
  *puy = -1.0;
}

// How wide a sweep of (ux, uy) is across a piece running (fDx, fDy) of its own length, in the
// target's pixels: the ground the two span, which is the stroke that covers the same as the
// parallelogram they make. It falls away as the two line up, which is what thins a face turning
// away from the viewer, and it is the whole height's sweep, so a band takes its share of it.
float Across(double fDx, double fDy, double ux, double uy)
{
  return static_cast<float>(SDL_fabs(fDx * uy - fDy * ux));
}

// How much of its light a face turned this far toward the viewer is drawn with: its own where it
// faces the viewer, the look's edge_on where it is seen edge-on. Light seen along its own length
// piles up, so a look may ask for more there rather than less.
double Shade(const PortMarkField &field, double fFace)
{
  return field.fEdgeOn + (1.0 - field.fEdgeOn) * fFace;
}

// The cells: an outline above each step of the edge, one for every row the look names, each row
// moved along by its share of the step, flickering on its own beat and taking the warning colour
// where a unit went out.
//
// A cell is drawn on the field's face rather than facing the viewer: its width runs along the
// edge and its height up the face, so a face turning away squashes and leans its cells with it,
// down to slivers where the face is edge-on.
void DrawCells(const Room &room, const PortMarkLook &look, const std::vector<Lit> &lit, double fLen,
               double fHigh, Uint64 nNow, unsigned int nRgb)
{
  const PortMarkCells &cells = look.cells;
  if (lit.empty() || fLen <= 0.0 || cells.fStep <= 0.0 || cells.rows.empty())
    return;
  const double t = nNow / 1000.0;
  const double fR = cells.fRadius * room.fPer;
  // an edge far longer than the step is drawn with the sets spread out rather than with more of
  // them, so the cost of a mark does not grow with the map
  const double fStep = SDL_max(cells.fStep, fLen / kCellsMost);
  for (double s = 0; s < fLen; s += fStep) {
    const size_t i = SDL_min(lit.size() - 1, static_cast<size_t>(s / fLen * lit.size()));
    const Lit &one = lit[i];
    if (one.fLight < cells.fLeast)
      continue;
    const float x = (one.x0 + one.x1) * 0.5f, y = (one.y0 + one.y1) * 0.5f;
    double lx = 0, ly = 0;
    LeanOf(one, look.field.fLean, &lx, &ly);
    const double fShade = Shade(look.field, one.fFace);
    for (size_t nRow = 0; nRow < cells.rows.size(); ++nRow) {
      const double fFlick = cells.fBase + cells.fSwing * SDL_sin(cells.fRate * t + cells.fAlong * s +
                                                                 cells.fRow * static_cast<double>(nRow));
      const float fAlpha = static_cast<float>(one.fLight * fFlick * cells.fAlpha * fShade);
      if (fAlpha <= 0.004f)
        continue;
      const UiColor c = UiRgb(one.fWarm > cells.fWarm ? look.nWarn : nRgb, fAlpha);
      // its place on the face: along the edge by its row's share of the step, and up the face to
      // its row's own height
      const double fOver = fStep * room.fPer * cells.fStagger * static_cast<double>(nRow);
      const double fUp = fHigh * cells.rows[nRow];
      const float cx = x + static_cast<float>(one.fDx * fOver + lx * fUp);
      const float cy = y + static_cast<float>(one.fDy * fOver + ly * fUp);
      float ax = 0, ay = 0;
      for (int v = 0; v <= cells.nSides; ++v) {
        const double a = 2.0 * kPi / cells.nSides * v + kPi / cells.nSides;
        const double p = SDL_cos(a) * fR, q = SDL_sin(a) * fR * cells.fSquash;
        const float bx = cx + static_cast<float>(one.fDx * p + lx * q);
        const float by = cy + static_cast<float>(one.fDy * p + ly * q);
        if (v != 0)
          UiLine(ax, ay, bx, by, c, SDL_max(1.0f, static_cast<float>(cells.fWidth * room.fPer)));
        ax = bx;
        ay = by;
      }
    }
  }
}

// A tear where a unit came through: a column of light at the place, widening and fading over its
// own life and fading away toward the top, in the warning colour where the unit went out.
//
// It is turned by the crossing itself. It stands at the point the unit went through, as wide as
// the face it tore -- along the edge, not across the screen -- and it leans the way the unit was
// going, which is the edge's normal on the side it crossed to and whatever it had of a run along
// the edge. So a unit leaving leans the tear out over the ground outside, a unit arriving leans
// it in over the inside, and the two read apart wherever on the shape it happened.
void DrawTears(const Room &room, const Mark &mark, const std::vector<Lit> &lit, double fLen, double fHigh,
               Uint64 nNow, unsigned int nRgb)
{
  const PortMarkTear &tear = mark.m.look.tear;
  if (tear.fLife <= 0.0 || lit.empty())
    return;
  for (const Event &e : mark.events) {
    const double tau = (nNow - e.nAt) / 1000.0;
    if (tau > tear.fLife)
      continue;
    const size_t i = fLen > 0.0 ? SDL_min(lit.size() - 1, static_cast<size_t>(e.fAt / fLen * lit.size())) : 0;
    const Lit &one = lit[i];
    const float x = AtX(room, e.x), y = AtY(room, e.y);
    const float fWide = static_cast<float>((tear.fWide + tear.fGrow * tau) * room.fPer);
    const double fTall = fHigh * tear.fHigh;
    const double ax = e.wx * fTall * tear.fLean, ay = -fTall + e.wy * fTall * tear.fLean;
    const double fFoot = tear.fAlpha * (1.0 - tau / tear.fLife);
    const UiColor c0 = UiRgb(e.bOut ? mark.m.look.nWarn : nRgb, 1.0f);
    const float w = SDL_max(1.0f, Across(one.fDx, one.fDy, ax, ay) / kTearSteps);
    for (int nStep = 0; nStep < kTearSteps; ++nStep) {
      const double a = fFoot * (1.0 - static_cast<double>(nStep) / kTearSteps);
      if (a <= 0.004)
        continue;
      const double f = (nStep + 0.5) / kTearSteps;
      const float cx = x + static_cast<float>(ax * f), cy = y + static_cast<float>(ay * f);
      const float hx = static_cast<float>(one.fDx) * fWide * 0.5f;
      const float hy = static_cast<float>(one.fDy) * fWide * 0.5f;
      UiColor c = c0;
      c.a = static_cast<float>(a);
      UiLine(cx - hx, cy - hy, cx + hx, cy + hy, c, w);
    }
  }
}

// The blocks that stand above the ground. True when anything was drawn.
bool DrawStanding(const Room &room, const Mark &mark, const std::vector<Lit> &lit, double fLen,
                  Uint64 nNow, unsigned int nRgb)
{
  const PortMarkLook &look = mark.m.look;
  const PortMarkField &field = look.field;
  const double fHigh = field.fHigh * room.fPer;
  bool bDrew = false;
  // the field: a band at a time from the foot upward, each fainter. A band is its piece of edge
  // swept up the wall, so it stands along the edge at whatever angle the edge runs, as wide as
  // that sweep covers and as bright as the face it is on turns toward the viewer.
  if (field.bOn && field.nBands > 0)
    for (int nBand = 0; nBand < field.nBands; ++nBand) {
      const double fFade = field.fAlpha *
                           SDL_max(0.0, 1.0 - field.fFade * nBand / field.nBands);
      const double fUp = (nBand + 0.5) / field.nBands;
      for (const Lit &one : lit) {
        const float fAlpha = static_cast<float>(one.fLight * fFade * Shade(field, one.fFace));
        if (fAlpha <= 0.004f || one.fLight < 0.02)
          continue;
        double lx = 0, ly = 0;
        LeanOf(one, field.fLean, &lx, &ly);
        const double ux = lx * fHigh, uy = ly * fHigh;
        const float w = SDL_max(1.0f, Across(one.fDx, one.fDy, ux, uy) / field.nBands);
        const float x0 = one.x0 + static_cast<float>(ux * fUp), y0 = one.y0 + static_cast<float>(uy * fUp);
        const float x1 = one.x1 + static_cast<float>(ux * fUp), y1 = one.y1 + static_cast<float>(uy * fUp);
        // added where the block asks for it, so the bands pile up where the edge doubles back
        // on itself and the light brightens the ground rather than covering it
        (field.bAdd ? UiLineAdd : UiLine)(x0, y0, x1, y1, UiRgb(nRgb, fAlpha), w);
        bDrew = true;
      }
    }
  if (look.cells.bOn)
    DrawCells(room, look, lit, fLen, fHigh, nNow, nRgb);
  if (look.tear.bOn)
    DrawTears(room, mark, lit, fLen, fHigh, nNow, nRgb);
  return bDrew;
}

// ---- the ground outside ----------------------------------------------------------------------
//
// A light under each of the player's own units standing outside a closed mark, beating: the
// warning colour within the band outside the edge, the danger colour past it. It is drawn on the
// ground, under the unit, and it is the player's own units only -- a mark says where they must
// not be, and nothing is shown about anyone else's.

const int kOutSteps = 5;  // the rings the layer draws a light in, having no gradient

// One light: where it is, in map pixels, its colour, how brightly it beats at this moment and
// how far it reaches.
struct OutLight {
  double x, y, fAlpha, fRadius;
  unsigned int nRgb;
};

void OutLights(const PortMapMark &m, int nViewer, Uint64 nNow, std::vector<OutLight> *pOut)
{
  const PortMarkOutside &out = m.look.outside;
  pOut->clear();
  // a viewer of no side has no units of their own, so there is nothing to warn
  if (out.nStyle != MAP_MARK_OUT_TINT || nViewer < 0 || !Closed(m.nShape))
    return;
  const double t = nNow / 1000.0;
  for (const Near &u : g_near) {
    if (u.nPlayer != nViewer)
      continue;
    const double d = Outside(m, u.x, u.y);
    if (d <= 0.0)
      continue;
    const bool bPast = d > out.fBand;  // past the band the warning becomes the danger
    OutLight one;
    one.x = u.x;
    one.y = u.y;
    one.fRadius = out.fRadius;
    one.nRgb = bPast ? out.nFar : out.nNear;
    one.fAlpha = out.fAlpha * (out.fBase + out.fSwing * SDL_sin(t * (bPast ? out.fPast : out.fBeat)));
    pOut->push_back(one);
  }
}

// The lights, drawn in rings of their own opacity since the kit fills shapes and draws no
// gradient: the rings add up to the light at the middle.
void DrawOutside(const Room &room, const std::vector<OutLight> &lights)
{
  for (const OutLight &one : lights) {
    const double a = one.fAlpha / kOutSteps;
    if (a <= 0.004)
      continue;
    for (int i = kOutSteps; i-- > 0;) {
      const float r = static_cast<float>(one.fRadius * room.fPer * (i + 1) / kOutSteps);
      UiFill(AtX(room, one.x) - r, AtY(room, one.y) - r, r * 2.0f, r * 2.0f,
             UiRgb(one.nRgb, static_cast<float>(a)), r);
    }
  }
}

// One mark. bFoot is false for a mark whose ground the pass has painted already, which is then
// drawn here for the parts that stand above the ground: a ward's field and cells. True when
// anything of it was drawn.
bool DrawMark(const Room &room, const Mark &mark, Uint64 nNow, int nViewer, bool bFoot)
{
  const PortMapMark &m = mark.m;
  const PortMarkLook &look = m.look;
  // the units that can reveal this one, and the box it cares about
  std::vector<const Near *> units;
  if (m.bFade) {
    for (const Near &u : g_near)
      if (Reveals(m, u, nViewer))
        units.push_back(&u);
  }
  std::vector<Piece> pieces;
  Edge(m, room.fPer, &pieces);
  if (pieces.empty())
    return false;
  const double fLen = EdgeLen(pieces);
  // what the whole mark is revealed by: the nearest a unit comes to its edge
  double fWhole = m.bFade ? 0.0 : 1.0;
  if (m.bFade)
    for (const Piece &p : pieces)
      fWhole = SDL_max(fWhole, Reveal(m, units, (p.x0 + p.x1) * 0.5, (p.y0 + p.y1) * 0.5));
  const double fSwell = Going(mark, nNow);
  const unsigned int nRgb = Rgb(m);
  const float fWidth = SDL_max(1.0f, static_cast<float>(m.fWidth * room.fPer));
  const bool bGlow = m.bFade && m.bGlow;
  std::vector<Lit> lit;
  LitEdge(room, mark, pieces, fLen, units, fWhole, bGlow, look.Stands(), nNow, &lit);
  bool bDrew = false;
  if (look.Stands())
    bDrew = DrawStanding(room, mark, lit, fLen, nNow, nRgb);
  // the light outside, where its block asks to be drawn over the game or the pass is not
  // painting the ground at all
  if (look.outside.nStyle != MAP_MARK_OUT_NONE && (bFoot || look.outside.nLayer == MAP_MARK_OVER)) {
    std::vector<OutLight> lights;
    OutLights(m, nViewer, nNow, &lights);
    DrawOutside(room, lights);
    bDrew = bDrew || !lights.empty();
  }
  if (!bFoot)
    return bDrew;
  // the inside takes the swell at the edge's beginning, having no point of the edge of its own
  const double fFill = m.fFill * Opacity(m, fWhole) * fSwell * Pulse(mark, nNow, 0.0);
  if (fFill > 0.004) {
    FillMark(room, m, UiRgb(nRgb, static_cast<float>(fFill)));
    bDrew = true;
  }
  if (m.fEdge <= 0.0)
    return bDrew;
  // the foot block carries a glow of its own width and opacity, where a mark without one has
  // what soft asks for
  const float fGlowWide = look.foot.bOn ? fWidth * static_cast<float>(look.foot.fGlow)
                                        : fWidth * (1.0f + 2.0f * static_cast<float>(m.fSoft));
  const double fGlowAlpha = look.foot.bOn ? look.foot.fAlpha : 0.3;
  const bool bGlowUnder = look.foot.bOn || m.fSoft > 0.0;
  // the edge whole only where every piece of it is drawn alike: a crossing being answered lights
  // its own stretch, as the fade's glow and the pulse's wave do
  if (m.fDash <= 0.0 && !bGlow && !Wave(m) && mark.events.empty()) {
    const double fEdge = m.fEdge * Opacity(m, fWhole) * fSwell;
    if (fEdge <= 0.004)
      return bDrew;
    if (bGlowUnder)
      EdgeWhole(room, m, UiRgb(nRgb, static_cast<float>(fEdge * fGlowAlpha)), fGlowWide);
    if (EdgeWhole(room, m, UiRgb(nRgb, static_cast<float>(fEdge)), fWidth))
      return true;
  }
  for (const Lit &one : lit) {
    if (m.fDash > 0.0 && SDL_fmod(one.fAt, m.fDash * 2.0) >= m.fDash)
      continue;
    const double fEdge = m.fEdge * one.fLight;
    if (fEdge <= 0.004)
      continue;
    // a unit on its way out turns this stretch toward the warning colour, as the paint turns it:
    // the two must agree, since which of them draws the edge is the renderer's business
    const unsigned int nStroke = one.fWarm > 0.004 ? Mix(nRgb, look.nWarn, one.fWarm) : nRgb;
    if (bGlowUnder)
      UiLine(one.x0, one.y0, one.x1, one.y1, UiRgb(nStroke, static_cast<float>(fEdge * fGlowAlpha)), fGlowWide);
    UiLine(one.x0, one.y0, one.x1, one.y1, UiRgb(nStroke, static_cast<float>(fEdge)), fWidth);
    bDrew = true;
  }
  return bDrew;
}

// ---- the ground: the marks painted into the map itself --------------------------------------
//
// A ground mark is a primitive of the scene recorded where the sprite pass begins, so every
// sprite drawn after it stands on the paint (SCENE_PRIM_MARK, port_scene.h). The shape, its
// style and the units that reveal it go into the pass's marks bytes as 32-bit words, which the
// shader reads by the primitive's own offset into them; the lengths are map pixels, as
// everything else about a mark is, and the shader has the view's scroll to place them by.
//
// The paint needs the full colour, which the GPU presenter draws by default. Where it is not
// drawn -- the exact classic colours, SDL's renderer -- nothing is recorded and the overlay
// draws the ground marks over the game instead, so a mark is never lost.

const int kMarkUnitsMost = 24;  // the revealing units a ground mark carries

std::vector<unsigned int> g_bytes;  // the pass's marks bytes
bool g_bPainted;                    // the last pass painted the ground marks

void Word(unsigned int n) { g_bytes.push_back(n); }
void Real(double d) {
  const float f = static_cast<float>(d);
  unsigned int n = 0;
  SDL_memcpy(&n, &f, sizeof n);
  g_bytes.push_back(n);
}

// A mark's numbers, as the shader reads them in this order (scene.hlsl's MARK_AT_ offsets, which
// move with this). The places are the view's pixels, the map's less the pass's scroll, as a
// look's light records them, so the shader compares them with the pixel it is drawing; the
// lengths are map pixels, which are the view's.
void MarkWords(const Mark &mark, const std::vector<const Near *> &units, Uint64 nNow, double fScrollX,
               double fScrollY)
{
  const PortMapMark &m = mark.m;
  const size_t nPoints = m.nShape == MAP_MARK_POLYGON ? m.points.size() / 2 : 0;
  // only a mark that glows round each unit is faded per pixel, and so needs the units at all.
  // A mark that rises whole rises by the nearest a unit comes to its edge, which is one number
  // for the mark: it is worked out here, as the overlay works it out, and the pass is handed
  // the opacity it comes to.
  const bool bGlow = m.bFade && m.bGlow;
  const int nUnits = bGlow ? static_cast<int>(SDL_min(units.size(), static_cast<size_t>(kMarkUnitsMost))) : 0;
  std::vector<Piece> pieces;
  Edge(m, 1.0, &pieces);
  double fWhole = 1.0;
  if (m.bFade && !m.bGlow) {
    fWhole = 0.0;
    for (const Piece &p : pieces)
      fWhole = SDL_max(fWhole, Reveal(m, units, (p.x0 + p.x1) * 0.5, (p.y0 + p.y1) * 0.5));
  }
  const double fWholeAlpha = Opacity(m, fWhole);
  Word(static_cast<unsigned int>(m.nShape));
  Word(Rgb(m));
  Real(m.fFill * Going(mark, nNow));
  Real(m.fEdge * Going(mark, nNow));
  Real(m.fWidth);
  Real(m.fDash);
  Real(m.fSoft);
  Real(m.x - fScrollX);
  Real(m.y - fScrollY);
  Real(m.nShape == MAP_MARK_RECT ? m.w : m.r);
  Real(m.nShape == MAP_MARK_RECT ? m.h : m.x2 - fScrollX);
  Real(m.y2 - fScrollY);
  Real(bGlow ? m.fNear : 0.0);  // 0: the pass fades nothing, and takes the opacity below
  Real(bGlow ? m.fMost : fWholeAlpha);
  Real(bGlow ? m.fLeast : fWholeAlpha);
  Word(static_cast<unsigned int>(nUnits));
  Word(static_cast<unsigned int>(nPoints));
  // the fade's curve, and the pulse: its beat, its depth and how long the mark has stood, from
  // which the pass works the swell out at each pixel's own point of the edge
  Word(static_cast<unsigned int>(m.nCurve));
  Real(m.fPulse);
  Real(m.fDepth);
  Real((nNow - mark.nAt) / 1000.0);
  // the crossings being answered, and how long the edge is, which a ripple's distance along it
  // wraps on: each one is where on the edge it happened, how long ago, whether the unit was
  // leaving, and how much of its step ran along the edge, which leans the flash
  const int nEvents = m.nCross == MAP_MARK_CROSS_RIPPLE ? static_cast<int>(mark.events.size()) : 0;
  Word(static_cast<unsigned int>(nEvents));
  Real(EdgeLen(pieces));
  Word(static_cast<unsigned int>(Closed(m.nShape) ? 1 : 0));
  // the look's numbers the paint has a use for: the foot's glow in the edge's widths and its
  // opacity, the colour a unit on its way out is answered in, and the ripple's wave and flash
  Real(m.look.foot.bOn ? m.look.foot.fGlow : 0.0);
  Real(m.look.foot.fAlpha);
  Word(m.look.nWarn);
  Real(m.look.ripple.fSpeed);
  Real(m.look.ripple.fWide);
  Real(m.look.ripple.fLife);
  Real(m.look.ripple.fFlash);
  Real(m.look.ripple.fFlashLife);
  Real(m.look.ripple.fSide);
  for (int i = 0; i < nUnits; ++i) {
    Real(units[static_cast<size_t>(i)]->x - fScrollX);
    Real(units[static_cast<size_t>(i)]->y - fScrollY);
  }
  for (size_t i = 0; i < nPoints; ++i) {
    Real(m.points[i * 2] - fScrollX);
    Real(m.points[i * 2 + 1] - fScrollY);
  }
  for (int i = 0; i < nEvents; ++i) {
    const Event &e = mark.events[static_cast<size_t>(i)];
    Real(e.fAt);
    Real((nNow - e.nAt) / 1000.0);
    Real(e.bOut ? 1.0 : 0.0);
    Real(e.fWay);
  }
}

// A light outside, as the shader reads it: a kind of its own past the shapes a mark can be, and
// then only the numbers it has -- its colour, how bright it is, where it is and how far it
// reaches -- at the places the shader looks for them.
void OutWords(const OutLight &one, double fScrollX, double fScrollY)
{
  Word(static_cast<unsigned int>(MAP_MARK_SHAPES));
  Word(one.nRgb);
  Real(one.fAlpha);
  Real(0);  // the edge's opacity, its width, its dash and its glow, which a light has none of
  Real(0);
  Real(0);
  Real(0);
  Real(one.x - fScrollX);
  Real(one.y - fScrollY);
  Real(one.fRadius);
}

// The box a mark covers, in the view's pixels, which the scene bins it by: its own reach plus
// the fade's and the glow's, clipped to the view by the scene itself.
void MarkBox(const PortMapMark &m, int nScrollX, int nScrollY, int *px, int *py, int *pw, int *ph)
{
  double x0 = m.x, y0 = m.y, x1 = m.x, y1 = m.y;
  if (m.nShape == MAP_MARK_CIRCLE || m.nShape == MAP_MARK_RING) {
    x0 = m.x - m.r, y0 = m.y - m.r, x1 = m.x + m.r, y1 = m.y + m.r;
  } else if (m.nShape == MAP_MARK_RECT) {
    x1 = m.x + m.w, y1 = m.y + m.h;
  } else if (m.nShape == MAP_MARK_POLYGON) {
    // a polygon's own corners, which its x and y have nothing to do with
    if (m.points.size() >= 2)
      x0 = x1 = m.points[0], y0 = y1 = m.points[1];
    for (size_t i = 0; i * 2 + 1 < m.points.size(); ++i) {
      x0 = SDL_min(x0, m.points[i * 2]);
      x1 = SDL_max(x1, m.points[i * 2]);
      y0 = SDL_min(y0, m.points[i * 2 + 1]);
      y1 = SDL_max(y1, m.points[i * 2 + 1]);
    }
  } else {
    x0 = SDL_min(m.x, m.x2), x1 = SDL_max(m.x, m.x2);
    y0 = SDL_min(m.y, m.y2), y1 = SDL_max(m.y, m.y2);
  }
  // the edge's own reach: its width and the glow under it, which the foot block widens. The
  // glow is measured from the edge's middle, so the box reaches half a width past the glow's
  // own, or the paint is cut off where the box ends and the cut shows as a ring
  const double fEdge = m.look.foot.bOn ? SDL_max(m.fWidth * (m.look.foot.fGlow + 0.5), 2.0)
                                       : SDL_max(m.fWidth * (1.0 + 2.0 * m.fSoft), 2.0);
  *px = static_cast<int>(SDL_floor(x0 - fEdge)) - nScrollX;
  *py = static_cast<int>(SDL_floor(y0 - fEdge)) - nScrollY;
  *pw = static_cast<int>(SDL_ceil(x1 + fEdge)) - nScrollX - *px + 1;
  *ph = static_cast<int>(SDL_ceil(y1 + fEdge)) - nScrollY - *py + 1;
}

// ---- the layer ------------------------------------------------------------------------------

bool InGame(void) { return gwGameMode == 3; }

bool LayerShown(void)
{
  return InGame() && !g_marks.empty();
}

// The units that can reveal a mark: their places, their owners, and whether the player watching
// sees them, which is what keeps a mark from showing through fog. Gathered once a frame, however
// many passes ask for them (NearOnce).
void NearGather(int nViewer, Uint64 nNow)
{
  g_nNearAt = nNow;
  g_near.clear();
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->pSprite == NULL || pUnit->lHitPoints <= 0 || pUnit->IsDeadOrDying())
      continue;
    if ((pUnit->pSprite->flags.b & 0x20) != 0)  // in a transport or otherwise off the map
      continue;
    Near u;
    u.x = pUnit->pSprite->wPosX;
    u.y = pUnit->pSprite->wPosY;
    u.nId = static_cast<unsigned int>(pUnit->GetUnitID());
    u.nPlayer = pUnit->bPlayer;
    u.bSeen = nViewer < 0 || u.nPlayer == nViewer || SpriteSeenBy(pUnit->pSprite, nViewer);
    u.bAlly = nViewer < 0 || u.nPlayer == nViewer || IsEnemyUnit(nViewer, pUnit) == 0;
    g_near.push_back(u);
  }
}

// The units for this frame, gathered if they have not been: the crossings are watched where the
// sprites begin, the ground's marks are recorded there as well and the layer draws later, and
// all three want the same list.
void NearOnce(int nViewer, Uint64 nNow)
{
  if (g_nNearAt != nNow || g_nNearAt == 0)
    NearGather(nViewer, nNow);
}

bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  Room room;
  if (!InGame() || g_marks.empty() || !RoomOf(fX, fY, fW, fH, &room))
    return false;
  const int nViewer = Viewer();
  // the units are gathered for the marks this layer fades itself: a mark the pass painted is
  // faded per pixel there, but a block that stands above the ground is drawn and faded here
  bool bFade = false;
  for (const Mark &mark : g_marks)
    bFade = bFade || (mark.m.bFade && !(mark.m.nLayer == MAP_MARK_GROUND && g_bPainted &&
                                        !mark.m.look.Stands()));
  const Uint64 nNow = SDL_GetTicks();
  if (bFade)
    NearOnce(nViewer, nNow);
  UiPushClip(room.afClip[0], room.afClip[1], room.afClip[2], room.afClip[3]);
  bool bDrew = false;
  for (const Mark &mark : g_marks) {
    // a mark of one player's own ground is drawn on that player's screen alone; a viewer of no
    // side sees every one
    if (nViewer >= 0 && (mark.m.nPlayers & (1u << (nViewer & 31))) == 0)
      continue;
    if (Going(mark, nNow) <= 0.0)
      continue;
    // the ground's marks are painted into the map itself unless the pass did not paint them. A
    // look's standing blocks are drawn here even then, so only the painted parts are left out
    const bool bPainted = mark.m.nLayer == MAP_MARK_GROUND && g_bPainted;
    if (bPainted && !mark.m.look.Stands() && mark.m.look.outside.nLayer != MAP_MARK_OVER)
      continue;
    bDrew = DrawMark(room, mark, nNow, nViewer, !bPainted) || bDrew;
  }
  UiPopClip();
  // the marks that have finished fading away
  for (size_t i = g_marks.size(); i-- != 0;)
    if (g_marks[i].nGo != 0 && Going(g_marks[i], nNow) <= 0.0)
      g_marks.erase(g_marks.begin() + static_cast<long>(i));
  return bDrew;
}

const PortOverlayLayer kLayer = {
    "marks", PORT_OVERLAY_MARKS, 0, LayerShown, NULL, LayerDraw, NULL, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);

// ---- the crossings --------------------------------------------------------------------------
//
// A mark that answers a crossing watches the units that light it, once a frame, and remembers
// which side of its edge each was on. A unit that has changed sides since the last look crossed,
// and the crossing is kept with the place on the edge, the moment and the way it went, for as
// long as it is answered. A unit seen for the first time raises nothing, so units already
// standing about when a mark is set do not all cross at once.
//
// Presentation, like the rest of a mark: only the units the player watching can see are looked
// at, so a crossing in the fog is not answered, and each seat notices its own.

void MarkWatch(Mark *pMark, int nViewer, Uint64 nNow)
{
  const PortMapMark &m = pMark->m;
  if (m.nCross == MAP_MARK_CROSS_NONE || pMark->nGo != 0) {
    pMark->watch.clear();
    pMark->events.clear();
    return;
  }
  for (size_t i = pMark->events.size(); i-- != 0;)
    if (nNow - pMark->events[i].nAt >= kEventMs)
      pMark->events.erase(pMark->events.begin() + static_cast<long>(i));
  std::vector<Piece> pieces;
  Edge(m, 1.0, &pieces);
  if (pieces.empty()) {
    pMark->watch.clear();
    return;
  }
  std::vector<Watch> now;
  now.reserve(g_near.size());
  for (const Near &u : g_near) {
    if (!Reveals(m, u, nViewer))
      continue;
    Watch w;
    w.nId = u.nId;
    w.x = u.x;
    w.y = u.y;
    w.bIn = Outside(m, u.x, u.y) < 0.0;
    bool bWas = w.bIn;  // a unit seen for the first time has crossed nothing
    double fWasX = u.x, fWasY = u.y;
    for (const Watch &was : pMark->watch)
      if (was.nId == u.nId) {
        bWas = was.bIn;
        fWasX = was.x;
        fWasY = was.y;
        break;
      }
    if (bWas != w.bIn) {
      Event e;
      double fDx = 0, fDy = 0;
      NearestOn(pieces, u.x, u.y, &e.fAt, &e.x, &e.y, &fDx, &fDy);
      // the way it went: the step it took to cross, and how much of that step ran along the
      // edge. A step of no length at all -- a unit put there, or a shape moved under it -- is
      // read as straight through, out or in by the side it came to.
      const double sx = u.x - fWasX, sy = u.y - fWasY;
      const double fStep = SDL_sqrt(sx * sx + sy * sy);
      const double nx = -fDy, ny = fDx;  // one way through the edge, either one will do
      const double fThrough = w.bIn == (Outside(m, e.x + nx * 2.0, e.y + ny * 2.0) < 0.0) ? 1.0 : -1.0;
      e.wx = fStep > 0.0 ? sx / fStep : nx * fThrough;
      e.wy = fStep > 0.0 ? sy / fStep : ny * fThrough;
      e.fWay = SDL_clamp(e.wx * fDx + e.wy * fDy, -1.0, 1.0);
      e.nAt = nNow;
      e.bOut = !w.bIn;
      if (static_cast<int>(pMark->events.size()) >= kEventsMost)
        pMark->events.erase(pMark->events.begin());
      pMark->events.push_back(e);
    }
    now.push_back(w);
  }
  pMark->watch.swap(now);
}

// Every mark's crossings, once a frame, before either layer draws: the record pass runs at the
// head of the sprites whether the ground is painted or not, so this is noticed the same way on
// every renderer.
void MarksWatch(Uint64 nNow)
{
  bool bWatch = false;
  for (const Mark &mark : g_marks)
    bWatch = bWatch || (mark.m.nCross != MAP_MARK_CROSS_NONE && mark.nGo == 0);
  if (!bWatch)
    return;
  const int nViewer = Viewer();
  NearOnce(nViewer, nNow);
  for (Mark &mark : g_marks)
    MarkWatch(&mark, nViewer, nNow);
}

// A name a script writes, in a table of its own.
int NameIn(const char *const *apsz, int n, const char *pszName)
{
  for (int i = 0; i < n; ++i)
    if (SDL_strcasecmp(apsz[i], pszName) == 0)
      return i;
  return -1;
}

}  // namespace

void PortMapMarkSet(const PortMapMark &mark)
{
  for (Mark &one : g_marks)
    if (one.m.id == mark.id) {
      // a mark set again keeps the beat of its pulse, so one moved or grown every tick does not
      // start its swell over; one set again while it fades away comes back
      const Uint64 nAt = one.nAt;
      one.m = mark;
      one.nAt = nAt;
      one.nGo = 0;
      return;
    }
  if (static_cast<int>(g_marks.size()) >= kMarksMax)
    return;
  Mark one;
  one.m = mark;
  one.nAt = SDL_GetTicks();
  g_marks.push_back(one);
}

// A mark cleared fades away over half a second and is dropped at the end of it, so a perimeter
// lifted leaves the screen as a perimeter would. Cleared again while it fades, it keeps the
// fade it is in.
void PortMapMarkClear(const char *pszId)
{
  for (Mark &one : g_marks)
    if (one.m.id == pszId) {
      if (one.nGo == 0)
        one.nGo = SDL_max(static_cast<Uint64>(1), SDL_GetTicks());
      return;
    }
}

void PortMapMarkClearAll(void)
{
  g_marks.clear();
  g_near.clear();
}

void PortMapMarkRecord(void)
{
  const Uint64 nNow = SDL_GetTicks();
  // the crossings are watched here, at the head of the sprites, whether the ground is painted
  // or not, so the paint and the layer answer the same events on every renderer
  if (InGame() && !g_marks.empty())
    MarksWatch(nNow);
  g_bytes.clear();
  g_bPainted = false;
  // the paint is the scene's full colour; without it the overlay draws these marks instead
  if (!InGame() || g_marks.empty() || !PortGpuFullColourShown()) {
    PortSceneMarks(NULL, 0);
    return;
  }
  const int nViewer = Viewer();
  bool bFade = false;
  for (const Mark &mark : g_marks)
    bFade = bFade || (mark.m.nLayer == MAP_MARK_GROUND &&
                      (mark.m.bFade || mark.m.look.outside.nStyle != MAP_MARK_OUT_NONE));
  if (bFade)
    NearOnce(nViewer, nNow);
  const double fScrollX = glScrollPixelX, fScrollY = glScrollPixelY;
  for (const Mark &mark : g_marks) {
    const PortMapMark &m = mark.m;
    if (m.nLayer != MAP_MARK_GROUND || Going(mark, nNow) <= 0.0)
      continue;
    if (nViewer >= 0 && (m.nPlayers & (1u << (nViewer & 31))) == 0)
      continue;
    std::vector<const Near *> units;
    if (m.bFade)
      for (const Near &u : g_near)
        if (Reveals(m, u, nViewer))
          units.push_back(&u);
    int x = 0, y = 0, w = 0, h = 0;
    MarkBox(m, static_cast<int>(fScrollX), static_cast<int>(fScrollY), &x, &y, &w, &h);
    const unsigned int nAt = static_cast<unsigned int>(g_bytes.size() * 4);
    MarkWords(mark, units, nNow, fScrollX, fScrollY);
    PortSceneMark(x, y, w, h, nAt);
    g_bPainted = true;
    // the ground under the player's own units outside: a primitive each, since one out past the
    // band may be anywhere on the map and the mark's own box would then be the whole of it
    std::vector<OutLight> lights;
    if (m.look.outside.nLayer == MAP_MARK_GROUND)
      OutLights(m, nViewer, nNow, &lights);
    for (const OutLight &one : lights) {
      const unsigned int nAtOne = static_cast<unsigned int>(g_bytes.size() * 4);
      OutWords(one, fScrollX, fScrollY);
      const int r = static_cast<int>(one.fRadius) + 1;
      PortSceneMark(static_cast<int>(one.x - fScrollX) - r, static_cast<int>(one.y - fScrollY) - r,
                    r * 2, r * 2, nAtOne);
    }
  }
  PortSceneMarks(g_bytes.empty() ? NULL : reinterpret_cast<const unsigned char *>(&g_bytes[0]),
                 static_cast<unsigned int>(g_bytes.size() * 4));
}

int PortMapMarkShapeOf(const char *pszName)
{
  return NameIn(kShapeNames, MAP_MARK_SHAPES, pszName);
}

int PortMapMarkWhoOf(const char *pszName)
{
  static const char *const kapsz[MAP_MARK_WHOS] = {"me", "allies", "all", "list"};
  return NameIn(kapsz, MAP_MARK_WHOS, pszName);
}

int PortMapMarkLayerOf(const char *pszName)
{
  static const char *const kapsz[MAP_MARK_LAYERS] = {"ground", "over"};
  return NameIn(kapsz, MAP_MARK_LAYERS, pszName);
}

int PortMapMarkCurveOf(const char *pszName)
{
  static const char *const kapsz[MAP_MARK_CURVES] = {"smooth", "even", "sharp"};
  return NameIn(kapsz, MAP_MARK_CURVES, pszName);
}

int PortMapMarkCrossingOf(const char *pszName)
{
  static const char *const kapsz[MAP_MARK_CROSSES] = {"none", "ripple", "flash", "stamp", "arc"};
  return NameIn(kapsz, MAP_MARK_CROSSES, pszName);
}

int PortMapMarkOutsideOf(const char *pszName)
{
  static const char *const kapsz[MAP_MARK_OUTS] = {"none", "tint", "ring", "tether"};
  return NameIn(kapsz, MAP_MARK_OUTS, pszName);
}

bool PortMapMarkRgbOf(const char *pszName, unsigned int *pnRgb)
{
  unsigned char abRgb[3];
  if (PortTeamColorParse(pszName, abRgb) == 0)
    return false;
  *pnRgb = (static_cast<unsigned int>(abRgb[0]) << 16) | (static_cast<unsigned int>(abRgb[1]) << 8) | abRgb[2];
  return true;
}

// The marks the minimap draws. The viewer is read here, as the layers read it, so a mark of one
// player's own ground is on that player's minimap alone; nothing else of the fade is, since the
// minimap has no alpha to fade with. Outside a game nothing is answered: the marks of the last
// game stand until the next one starts, and the lobby's map preview draws on the same picture.
int PortMapMarkOnMinimap(std::vector<PortMarkMinimap> *pOut)
{
  pOut->clear();
  if (!InGame())
    return 0;
  const Uint64 nNow = SDL_GetTicks();
  const int nViewer = Viewer();
  for (const Mark &mark : g_marks) {
    const PortMapMark &m = mark.m;
    const PortMarkMini &mini = m.look.mini;
    if (!mini.bOn || Going(mark, nNow) <= 0.0)
      continue;
    if (nViewer >= 0 && (m.nPlayers & (1u << (nViewer & 31))) == 0)
      continue;
    PortMarkMinimap one;
    one.nShape = m.nShape;
    one.x = m.x;
    one.y = m.y;
    one.w = m.w;
    one.h = m.h;
    one.r = m.r;
    one.x2 = m.x2;
    one.y2 = m.y2;
    one.points = m.nShape == MAP_MARK_POLYGON ? &m.points : NULL;
    // its own colour where the block names one, else the mark's
    one.nPlayer = mini.nPlayer >= 0 ? mini.nPlayer : (mini.bRgb ? -1 : m.nPlayer);
    one.nRgb = mini.bRgb ? mini.nRgb : m.nRgb;
    one.fWidth = mini.fWidth;
    one.fFill = mini.fFill;
    one.fEdge = mini.fEdge;
    one.fDash = mini.fDash;
    pOut->push_back(one);
  }
  return static_cast<int>(pOut->size());
}

int PortMapMarkCount(void)
{
  return static_cast<int>(g_marks.size());
}

std::string PortMapMarkLine(int i)
{
  if (i < 0 || i >= static_cast<int>(g_marks.size()))
    return std::string();
  const PortMapMark &m = g_marks[i].m;
  char sz[256];
  const char *pszShape = kShapeNames[SDL_clamp(m.nShape, 0, MAP_MARK_SHAPES - 1)];
  if (m.nShape == MAP_MARK_CIRCLE || m.nShape == MAP_MARK_RING)
    snprintf(sz, sizeof sz, "%s %s at %d, %d radius %d", m.id.c_str(), pszShape, static_cast<int>(m.x),
             static_cast<int>(m.y), static_cast<int>(m.r));
  else if (m.nShape == MAP_MARK_RECT)
    snprintf(sz, sizeof sz, "%s %s at %d, %d by %d, %d", m.id.c_str(), pszShape, static_cast<int>(m.x),
             static_cast<int>(m.y), static_cast<int>(m.w), static_cast<int>(m.h));
  else if (m.nShape == MAP_MARK_POLYGON)
    snprintf(sz, sizeof sz, "%s %s of %d points", m.id.c_str(), pszShape,
             static_cast<int>(m.points.size() / 2));
  else
    snprintf(sz, sizeof sz, "%s %s from %d, %d to %d, %d", m.id.c_str(), pszShape, static_cast<int>(m.x),
             static_cast<int>(m.y), static_cast<int>(m.x2), static_cast<int>(m.y2));
  std::string line = sz;
  if (m.bFade)
    line += ", fading";
  return line;
}
