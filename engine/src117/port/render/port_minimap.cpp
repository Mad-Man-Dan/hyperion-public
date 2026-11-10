// port_minimap.cpp -- the minimap's look (port_minimap.h): the looks over 1.17's minimap, kept in
// the order they were added, and the one the active mod's hud.json gives, its dots in a colour
// of the mod's by the owner's relation to the player. Presentation only: a look changes no dot's
// place and no step of the dots' walk, whose counts the multiplayer game's hash takes.
#include <windows.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "port_minimap.h"
#include "port_mapmark.h"   // the marks a mod set, and the shapes' numbering
#include "port_teamcolor.h" // PortTeamMinimapShown
#include "../../h/game.h"   // CPalette::find_closest_palette_entry
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../../h/playerlimits.h"  // PlayerSlots, PLAYER_NEUTRAL
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

const int kLooksMax = 16;
const PortMinimapLook *g_apLook[kLooksMax];  // in the order added; zero before any is
int g_nLooks;

const PortLogger Log("hyperion_mods.log");

// hud.json's dots: a palette number for each relation to the player, -1 to leave it
enum { OWN, ALLIED, ENEMY, NEUTRAL, RELATIONS };
const char *const kapszRelation[RELATIONS] = {"own", "allied", "enemy", "neutral"};
int g_anDot[RELATIONS] = {-1, -1, -1, -1};

// The owner's relation to the player, in a game (a replay has no player of its own): -1 in a
// replay.
int Relation(int nOwner)
{
  const int nLocal = static_cast<int>(g_LocalNationID);
  const int nSlots = PlayerSlots();
  if (gPlayback != 0 || nLocal < 0 || nLocal >= nSlots || nOwner < 0 || nOwner >= nSlots)
    return -1;
  if (nOwner == PLAYER_NEUTRAL)
    return NEUTRAL;
  if (nOwner == nLocal)
    return OWN;
  return sgbAlliances_g[nLocal][nOwner] != 0 ? ALLIED : ENEMY;
}

unsigned char HudDotColour(int nOwner, int nType, unsigned char bColour)
{
  (void)nType;
  const int r = Relation(nOwner);
  return r >= 0 && g_anDot[r] >= 0 ? static_cast<unsigned char>(g_anDot[r]) : bColour;
}

// ---- the marks a mod sets, on the minimap's picture ------------------------------------------

// The minimap's size and scale as it settles them, for a mod asking (PortMinimapSized); zero
// before a game's minimap is made and again once it is taken down.
int g_nMiniW, g_nMiniH, g_nMiniScale;

// The picture a mark is drawn into: palette indices, nW by nH. Every length below is in these
// pixels, the map's having been scaled on the way in.
struct Mini {
  unsigned char *pb;
  int nW, nH;
};

// A mark as this pass draws it: the shape already in minimap pixels, the palette entry its
// colour came to, and the opacities its inside and its edge are dithered at.
struct Draw {
  int nShape = MAP_MARK_CIRCLE;
  double x = 0, y = 0, w = 0, h = 0, r = 0, x2 = 0, y2 = 0;
  std::vector<double> points;
  unsigned char bPen = 0;
  double fWidth = 1, fDash = 0, fFill = 0, fEdge = 1;
};

// The threshold an opacity is dithered through, the sixteen steps in the order that spreads them
// most evenly. The picture has no alpha, so a part opacity is drawn as a part of the pixels: at
// the minimap's size that reads as a weaker colour rather than as a pattern.
const int kDither[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

bool Lit(int x, int y, double fAlpha)
{
  return fAlpha >= 1.0 || (fAlpha > 0.0 && fAlpha * 16.0 > kDither[(y & 3) * 4 + (x & 3)]);
}

// The first pixel a length reaches, a pixel counting as reached once its middle is past the
// length. A mark is placed in map pixels, so a shape may be a great deal larger than the
// minimap and its ceiling is no int at all: anything past nMost is held there, where every loop
// below stops at once, and a length that is not a number lands one short of the picture.
int At(double f, int nMost)
{
  const double g = std::ceil(f - 0.5);
  if (!(g > -1.0))
    return -1;
  return g < nMost ? static_cast<int>(g) : nMost;
}

// A pixel, and a run of them from fLeft up to fRight, clipped to the picture.
void Dot(const Mini &m, int x, int y, unsigned char bPen, double fAlpha)
{
  if (x >= 0 && y >= 0 && x < m.nW && y < m.nH && Lit(x, y, fAlpha))
    m.pb[y * m.nW + x] = bPen;
}

void Span(const Mini &m, double fLeft, double fRight, int y, unsigned char bPen, double fAlpha)
{
  if (y < 0 || y >= m.nH)
    return;
  int x0 = At(fLeft, m.nW);
  const int x1 = At(fRight, m.nW) - 1;
  if (x0 < 0)
    x0 = 0;
  for (int x = x0; x <= x1; ++x)
    if (Lit(x, y, fAlpha))
      m.pb[y * m.nW + x] = bPen;
}

// The insides: a rectangle's, a circle's a row at a time, and a polygon's by the even-odd rule,
// every row's crossings sorted and the runs between them in turn filled.
void FillRect(const Mini &m, double x, double y, double w, double h, unsigned char bPen, double fAlpha)
{
  if (w <= 0.0 || h <= 0.0)
    return;
  int y0 = At(y, m.nH);
  const int y1 = At(y + h, m.nH) - 1;
  if (y0 < 0)
    y0 = 0;
  for (int r = y0; r <= y1; ++r)
    Span(m, x, x + w, r, bPen, fAlpha);
}

void FillCircle(const Mini &m, double cx, double cy, double r, unsigned char bPen, double fAlpha)
{
  if (r <= 0.0)
    return;
  int y0 = At(cy - r, m.nH);
  const int y1 = At(cy + r, m.nH) - 1;
  if (y0 < 0)
    y0 = 0;
  for (int y = y0; y <= y1; ++y) {
    const double dy = y + 0.5 - cy;
    if (std::fabs(dy) >= r)
      continue;
    const double dx = std::sqrt(r * r - dy * dy);
    Span(m, cx - dx, cx + dx, y, bPen, fAlpha);
  }
}

void FillPoly(const Mini &m, const std::vector<double> &pts, unsigned char bPen, double fAlpha)
{
  const size_t n = pts.size() / 2;
  if (n < 3)
    return;
  double fTop = pts[1], fBot = pts[1];
  for (size_t i = 1; i < n; ++i) {  // std::min and std::max are windows.h's macros here
    const double f = pts[i * 2 + 1];
    if (f < fTop)
      fTop = f;
    if (f > fBot)
      fBot = f;
  }
  int y0 = At(fTop, m.nH);
  const int y1 = At(fBot, m.nH) - 1;
  if (y0 < 0)
    y0 = 0;
  std::vector<double> at;
  for (int y = y0; y <= y1; ++y) {
    const double fy = y + 0.5;
    at.clear();
    for (size_t i = 0; i < n; ++i) {
      const size_t j = (i + 1) % n;
      const double ay = pts[i * 2 + 1], by = pts[j * 2 + 1];
      if ((ay <= fy) == (by <= fy))  // the row crosses this side, once
        continue;
      at.push_back(pts[i * 2] + (fy - ay) * (pts[j * 2] - pts[i * 2]) / (by - ay));
    }
    std::sort(at.begin(), at.end());
    for (size_t k = 0; k + 1 < at.size(); k += 2)
      Span(m, at[k], at[k + 1], y, bPen, fAlpha);
  }
}

// A square of the edge's width centred on a point of it.
void Stamp(const Mini &m, double x, double y, double fWidth, unsigned char bPen, double fAlpha)
{
  if (fWidth > 1.0) {
    FillRect(m, x - fWidth / 2, y - fWidth / 2, fWidth, fWidth, bPen, fAlpha);
    return;
  }
  // the one pixel the point falls in, so the thinnest edge a mod can ask for is still a line.
  // The test is the cast's guard as well as the clip: off the picture the floor is no int.
  if (x > -1.0 && x < m.nW && y > -1.0 && y < m.nH)
    Dot(m, static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), bPen, fAlpha);
}

// Liang and Barsky's clip, a boundary at a time: p is how fast the segment leaves the box over
// its length and q how far inside the boundary it starts. False once the two meet, which is a
// segment the box has none of.
bool ClipT(double p, double q, double *pfFrom, double *pfTo)
{
  if (p == 0.0)
    return q >= 0.0;  // along the boundary: in or out for the whole length
  const double t = q / p;
  if (p < 0.0) {  // coming in
    if (t > *pfTo)
      return false;
    if (t > *pfFrom)
      *pfFrom = t;
  } else {  // going out
    if (t < *pfFrom)
      return false;
    if (t < *pfTo)
      *pfTo = t;
  }
  return true;
}

// The stretch of a segment that can reach the picture, as parts of its length. A mark may be
// far larger than the minimap -- a perimeter is placed in map pixels and the map is the size the
// mod makes it -- so the walk below is held to what is drawable rather than to the shape.
bool Reach(const Mini &m, double ax, double ay, double bx, double by, double fPad,
           double *pfFrom, double *pfTo)
{
  const double dx = bx - ax, dy = by - ay;
  *pfFrom = 0.0;
  *pfTo = 1.0;
  return ClipT(-dx, ax + fPad, pfFrom, pfTo) && ClipT(dx, m.nW + fPad - ax, pfFrom, pfTo) &&
         ClipT(-dy, ay + fPad, pfFrom, pfTo) && ClipT(dy, m.nH + fPad - ay, pfFrom, pfTo);
}

// The edge: the path walked a pixel at a time with a square of its width stamped at each step,
// and the dash counted in the distance gone along the whole path, so that it runs on round a
// corner rather than starting again at each side. A closed shape's path ends where it began.
void Edge(const Mini &m, const std::vector<double> &pts, double fWidth, double fDash,
          unsigned char bPen, double fAlpha)
{
  const size_t n = pts.size() / 2;
  if (n < 2)
    return;
  const double fPad = fWidth / 2 + 1;
  double fGone = 0.0;
  for (size_t i = 0; i + 1 < n; ++i) {
    const double ax = pts[i * 2], ay = pts[i * 2 + 1], bx = pts[i * 2 + 2], by = pts[i * 2 + 3];
    const double fLen = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    double fFrom = 0.0, fTo = 1.0;
    // the whole length still counts toward the dash, so a shape drawn partly off the picture
    // keeps the dashes it would have had whole
    if (fLen > 0.0 && Reach(m, ax, ay, bx, by, fPad, &fFrom, &fTo)) {
      const int nSteps = static_cast<int>(std::ceil(fLen * (fTo - fFrom)));
      for (int s = 0; s <= nSteps; ++s) {
        const double t = fFrom + (nSteps > 0 ? (fTo - fFrom) * s / nSteps : 0.0);
        if (fDash <= 0.0 || std::fmod(fGone + t * fLen, fDash * 2.0) < fDash)
          Stamp(m, ax + (bx - ax) * t, ay + (by - ay) * t, fWidth, bPen, fAlpha);
      }
    }
    fGone += fLen;
  }
}

// The path a shape's edge runs along, in minimap pixels. A circle is walked a pixel of its
// circumference at a time, up to a cap: past that the arc crossing a picture this size is
// nearly straight, so its chords draw the same line.
void Path(const Draw &d, std::vector<double> *pOut)
{
  pOut->clear();
  switch (d.nShape) {
  case MAP_MARK_CIRCLE:
  case MAP_MARK_RING: {
    if (d.r <= 0.0)
      return;
    int nSides = static_cast<int>(std::ceil(2.0 * 3.14159265358979 * d.r));
    if (nSides < 12)
      nSides = 12;
    if (nSides > 4096)
      nSides = 4096;
    for (int i = 0; i <= nSides; ++i) {
      const double a = 2.0 * 3.14159265358979 * i / nSides;
      pOut->push_back(d.x + d.r * std::cos(a));
      pOut->push_back(d.y + d.r * std::sin(a));
    }
    break;
  }
  case MAP_MARK_RECT: {
    const double ax[5] = {d.x, d.x + d.w, d.x + d.w, d.x, d.x};
    const double ay[5] = {d.y, d.y, d.y + d.h, d.y + d.h, d.y};
    for (int i = 0; i < 5; ++i) {
      pOut->push_back(ax[i]);
      pOut->push_back(ay[i]);
    }
    break;
  }
  case MAP_MARK_POLYGON:
    if (d.points.size() < 6)
      return;
    *pOut = d.points;
    pOut->push_back(d.points[0]);
    pOut->push_back(d.points[1]);
    break;
  case MAP_MARK_LINE:
    pOut->push_back(d.x);
    pOut->push_back(d.y);
    pOut->push_back(d.x2);
    pOut->push_back(d.y2);
    break;
  default:
    break;
  }
}

void DrawMark(const Mini &m, const Draw &d)
{
  if (d.fFill > 0.0) {
    switch (d.nShape) {
    case MAP_MARK_CIRCLE:
      FillCircle(m, d.x, d.y, d.r, d.bPen, d.fFill);
      break;
    case MAP_MARK_RECT:
      FillRect(m, d.x, d.y, d.w, d.h, d.bPen, d.fFill);
      break;
    case MAP_MARK_POLYGON:
      FillPoly(m, d.points, d.bPen, d.fFill);
      break;
    default:
      break;  // a ring and a line have no inside
    }
  }
  if (d.fEdge > 0.0 && d.fWidth > 0.0) {
    std::vector<double> path;
    Path(d, &path);
    Edge(m, path, d.fWidth, d.fDash, d.bPen, d.fEdge);
  }
}

// A mark's colour as a palette entry: a player's is the very dot colour the minimap draws that
// player's units in, and any other is the game's own nearest entry. That finder leaves out the
// entries the tileset cycles, so a mark never takes a colour that walks off under it as the
// water moves, and it leaves out entry 0, which a picture does not draw.
unsigned char Pen(int nPlayer, unsigned int nRgb)
{
  if (nPlayer >= 0)
    return PortTeamMinimapShown(nPlayer);
  palentry_t want;
  want.bRed = static_cast<BYTE>((nRgb >> 16) & 0xFF);
  want.bGreen = static_cast<BYTE>((nRgb >> 8) & 0xFF);
  want.bBlue = static_cast<BYTE>(nRgb & 0xFF);
  want.bFlags = 0;
  return static_cast<unsigned char>(CPalette::find_closest_palette_entry(sgGamePalette_g, want));
}

}  // namespace

bool PortMinimapAdd(const PortMinimapLook *pLook)
{
  if (g_nLooks == kLooksMax)
    return false;
  g_apLook[g_nLooks++] = pLook;
  return true;
}

unsigned char PortMinimapDotColour(int nOwner, int nType, unsigned char bColour)
{
  for (int i = 0; i < g_nLooks; ++i)
    if (g_apLook[i]->DotColour != NULL)
      bColour = g_apLook[i]->DotColour(nOwner, nType, bColour);
  return bColour;
}

void PortMinimapHud(const PortJson &minimap)
{
  for (int &n : g_anDot)
    n = -1;
  const PortJson &dots = minimap["dots"];
  for (int r = 0; r < RELATIONS; ++r) {
    const PortJson &v = dots[kapszRelation[r]];
    if (v.kind == PortJson::NONE)
      continue;
    if (v.kind == PortJson::NUMBER && v.d >= 0.0 && v.d <= 255.0 && v.d == static_cast<int>(v.d))
      g_anDot[r] = static_cast<int>(v.d);
    else
      Log("minimap: dots' %s is a palette number, 0 to 255", kapszRelation[r]);
  }
}

void PortMinimapSized(int nWidth, int nHeight, int nScale)
{
  g_nMiniW = nWidth;
  g_nMiniH = nHeight;
  g_nMiniScale = nScale;
}

bool PortMinimapSize(int *pnWidth, int *pnHeight, int *pnScale)
{
  if (g_nMiniW <= 0 || g_nMiniH <= 0 || g_nMiniScale <= 0)
    return false;
  if (pnWidth != NULL)
    *pnWidth = g_nMiniW;
  if (pnHeight != NULL)
    *pnHeight = g_nMiniH;
  if (pnScale != NULL)
    *pnScale = g_nMiniScale;
  return true;
}

void PortMinimapMarks(unsigned char *pbPixels, int nWidth, int nHeight, int nScale)
{
  if (pbPixels == NULL || nWidth <= 0 || nHeight <= 0 || nScale <= 0)
    return;
  // the marks to draw here are port_mapmark.cpp's answer: those with a minimap block, in a game,
  // that the player watching is among the players of and that have not finished fading away
  std::vector<PortMarkMinimap> marks;
  if (PortMapMarkOnMinimap(&marks) <= 0)
    return;
  const Mini m = {pbPixels, nWidth, nHeight};
  const double k = 1.0 / nScale;
  for (const PortMarkMinimap &a : marks) {
    Draw d;
    d.nShape = a.nShape;
    d.x = a.x * k;
    d.y = a.y * k;
    d.w = a.w * k;
    d.h = a.h * k;
    d.r = a.r * k;
    d.x2 = a.x2 * k;
    d.y2 = a.y2 * k;
    if (a.points != NULL)
      for (size_t i = 0; i < a.points->size(); ++i)
        d.points.push_back((*a.points)[i] * k);
    d.bPen = Pen(a.nPlayer, a.nRgb);
    // the width and the dash are the block's own, in minimap pixels: an edge asked for in map
    // pixels would be a fraction of one here and draw nothing
    d.fWidth = a.fWidth;
    d.fDash = a.fDash;
    d.fFill = a.fFill;
    d.fEdge = a.fEdge;
    DrawMark(m, d);
  }
}

// hud.json's look.
static const PortMinimapLook kHudLook = {"hud.json", HudDotColour};
PORT_MINIMAP_LOOK(kHudLook);
