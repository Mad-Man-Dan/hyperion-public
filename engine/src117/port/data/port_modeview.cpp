// port_modeview.cpp -- the parts a game mode shows on screen (port_modeview.h): a countdown or
// timer, a banner with lines, and counters. The mode's scripts set and clear them; this keeps
// them and draws them over the game as a layer of the overlay, in the window's own resolution
// and the front end's fonts, sized by the game's picture as the overlay's counters are.
//
// Presentation only, so the simulation never reads a part, none of this is saved or sent, and a
// game plays the same with the parts or without. A replay and a watched seat show them because
// the mode's scripts run there too and set them again.
#include <windows.h>
#include <stdio.h>
#include <algorithm>
#include <SDL3/SDL.h>
#include "port_modeview.h"
#include "port_mode.h"               // PortModeInGame
#include "../front/port_ui.h"        // UiText, UiFill
#include "../render/port_overlay.h"  // PORT_OVERLAY_LAYER
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

const int kPartsMax = 16;
const int kFps = 24;        // game frames a second at Fastest, as hx.seconds counts them
const int kFadeFrames = 12; // a part with its own length fades away over the last half second

struct Part {
  PortModeViewPart p;
  unsigned nAt;  // the game frame it was set at, which the count and the fade are measured from
};

std::vector<Part> g_parts;

unsigned Now(void)
{
  return oc_dwReplayFrame;
}

// A timer's frames left (a countdown) or gone (counting up), and how full its bar is, 0 to 1.
int Left(const Part &part)
{
  const int nSince = static_cast<int>(Now() - part.nAt);
  if (part.p.nFrames < 0)
    return nSince;
  return SDL_max(0, part.p.nFrames - nSince);
}

float Full(const Part &part)
{
  const int nFull = part.p.nFull > 0 ? part.p.nFull : part.p.nFrames;
  if (nFull <= 0)
    return 1.0f;
  return SDL_clamp(static_cast<float>(Left(part)) / static_cast<float>(nFull), 0.0f, 1.0f);
}

// A part with a length of its own goes when it runs out: 0 while it shows, falling to 0 as it
// fades; below 0 it is drawn no more.
float Alpha(const Part &part)
{
  if (part.p.nShows <= 0)
    return 1.0f;
  const int nLeft = part.p.nShows - static_cast<int>(Now() - part.nAt);
  if (nLeft <= 0)
    return 0.0f;
  return nLeft >= kFadeFrames ? 1.0f : static_cast<float>(nLeft) / kFadeFrames;
}

// 0:45, or 1:02:30 for an hour and more.
std::string Clock(int nFrames)
{
  const int nSeconds = (nFrames + kFps - 1) / kFps;  // the second it is still inside
  char sz[32];
  if (nSeconds >= 3600)
    snprintf(sz, sizeof sz, "%d:%02d:%02d", nSeconds / 3600, nSeconds / 60 % 60, nSeconds % 60);
  else
    snprintf(sz, sizeof sz, "%d:%02d", nSeconds / 60, nSeconds % 60);
  return sz;
}

// A count as a player reads it: whole numbers without a point, the rest with one.
std::string Count(double d)
{
  char sz[40];
  if (d == static_cast<double>(static_cast<long long>(d)) && SDL_fabs(d) < 1e15)
    snprintf(sz, sizeof sz, "%lld", static_cast<long long>(d));
  else
    snprintf(sz, sizeof sz, "%.1f", d);
  return sz;
}

UiColor Ink(int nTone, float fAlpha)
{
  switch (nTone) {
    case MODE_VIEW_TONE_GOOD: return UiRgb(0x8FD98A, fAlpha);
    case MODE_VIEW_TONE_WARN: return UiRgb(0xF2C14E, fAlpha);
    case MODE_VIEW_TONE_BAD: return UiRgb(0xE8573F, fAlpha);
    default: return UiRgb(0xFFFFFF, fAlpha);
  }
}

// ---- one part's block ---------------------------------------------------------------------

// A line of a block: its words, face, size and colour.
struct Row {
  std::string text;
  UiFace face;
  float fSize, fSpacing;
  UiColor c;
};

struct Block {
  std::vector<Row> rows;
  bool bBar = false;
  float fFull = 1.0f;
  UiColor bar;
  float fWidth = 0, fHeight = 0;
};

// The part as rows, sized by k, the game picture's rows over 480.
Block Shape(const Part &part, float k, float fAlpha)
{
  const PortModeViewPart &p = part.p;
  const UiColor dim = UiRgb(0x9ECCF3, fAlpha);
  const UiColor ink = Ink(p.nTone, fAlpha);
  Block b;
  if (p.nKind == MODE_VIEW_BANNER) {
    if (!p.label.empty())
      b.rows.push_back({p.label, UI_FACE_WIDE, 14.0f * k, 0.6f * k, ink});
    for (const std::string &line : p.lines)
      b.rows.push_back({line, UI_FACE_TEXT, 9.5f * k, 0.0f, UiRgb(0xF2F0E6, fAlpha)});
  } else {
    if (!p.label.empty())
      b.rows.push_back({p.label, UI_FACE_WIDE, 6.5f * k, 0.5f * k, dim});
    std::string value;
    if (p.nKind == MODE_VIEW_TIMER) {
      value = Clock(Left(part));
    } else {
      value = Count(p.dValue);
      if (p.dOf != 0.0)
        value += " / " + Count(p.dOf);
    }
    // the countdown is a part a player watches, so it is drawn larger than a count
    if (p.nStyle != MODE_VIEW_BAR || p.nKind == MODE_VIEW_COUNTER)
      b.rows.push_back({value, UI_FACE_MONO, (p.nKind == MODE_VIEW_TIMER ? 12.0f : 10.0f) * k, 0.0f, ink});
    b.bBar = p.nStyle == MODE_VIEW_BAR || p.nStyle == MODE_VIEW_BOTH;
    b.fFull = p.nKind == MODE_VIEW_TIMER
                  ? Full(part)
                  : (p.dOf > 0.0 ? SDL_clamp(static_cast<float>(p.dValue / p.dOf), 0.0f, 1.0f) : 1.0f);
    b.bar = ink;
  }
  for (const Row &r : b.rows) {
    b.fWidth = SDL_max(b.fWidth, UiTextWidth(r.face, r.fSize, r.fSpacing, r.text.c_str()));
    b.fHeight += r.fSize * 1.25f;
  }
  if (b.bBar) {
    b.fWidth = SDL_max(b.fWidth, 90.0f * k);
    b.fHeight += 6.0f * k;
  }
  return b;
}

// Draws the block with its left at x (0 left, 1 centre, 2 right against x) and its top at y.
void Paint(const Block &b, float x, float y, int nAlign, float k, float fAlpha)
{
  const float fPad = 5.0f * k;
  const float fLeft = nAlign == 1 ? x - b.fWidth * 0.5f : nAlign == 2 ? x - b.fWidth : x;
  UiFill(fLeft - fPad, y - fPad, b.fWidth + fPad * 2.0f, b.fHeight + fPad * 2.0f,
         UiRgb(0x000000, 0.5f * fAlpha), 3.0f * k);
  float fRow = y;
  for (const Row &r : b.rows) {
    float fAscent, fDescent, fGap;
    UiFontMetrics(r.face, r.fSize, &fAscent, &fDescent, &fGap);
    const float w = UiTextWidth(r.face, r.fSize, r.fSpacing, r.text.c_str());
    const float fTextX = nAlign == 1 ? x - w * 0.5f : nAlign == 2 ? x - w : x;
    UiText(fTextX + 1.0f * k, fRow + fAscent * 0.95f + 1.0f * k, r.face, r.fSize, r.fSpacing,
           UiRgb(0x000000, 0.7f * fAlpha), r.text.c_str());
    UiText(fTextX, fRow + fAscent * 0.95f, r.face, r.fSize, r.fSpacing, r.c, r.text.c_str());
    fRow += r.fSize * 1.25f;
  }
  if (b.bBar) {
    const float fTall = 3.0f * k;
    UiFill(fLeft, fRow + 1.5f * k, b.fWidth, fTall, UiRgb(0xFFFFFF, 0.2f * fAlpha), fTall * 0.5f);
    if (b.fFull > 0.0f)
      UiFill(fLeft, fRow + 1.5f * k, b.fWidth * b.fFull, fTall, b.bar, fTall * 0.5f);
  }
}

// ---- the layer ----------------------------------------------------------------------------

bool LayerShown(void)
{
  if (PortModeInGame()[0] == '\0' || g_parts.empty())
    return false;
  for (const Part &part : g_parts)
    if (Alpha(part) > 0.0f)
      return true;
  return false;
}

// Where an anchor's parts start in the game's box, and how they are aligned. The top keeps
// clear of the resource bar and of the overlay's own counters at the top left; there is no
// bottom anchor, which the console and a HUD design own.
void Place(int nAnchor, float fX, float fY, float fW, float fH, float k, float *px, float *py, int *pnAlign)
{
  const float fEdge = 8.0f * k;
  switch (nAnchor) {
    case MODE_VIEW_TOP_LEFT:
      *px = fX + fEdge, *py = fY + 64.0f * k, *pnAlign = 0;
      break;
    case MODE_VIEW_TOP_RIGHT:
      *px = fX + fW - fEdge, *py = fY + 30.0f * k, *pnAlign = 2;
      break;
    case MODE_VIEW_LEFT:
      *px = fX + fEdge, *py = fY + fH * 0.34f, *pnAlign = 0;
      break;
    case MODE_VIEW_RIGHT:
      *px = fX + fW - fEdge, *py = fY + fH * 0.34f, *pnAlign = 2;
      break;
    case MODE_VIEW_MIDDLE:
      *px = fX + fW * 0.5f, *py = fY + fH * 0.30f, *pnAlign = 1;
      break;
    default:
      *px = fX + fW * 0.5f, *py = fY + 26.0f * k, *pnAlign = 1;
      break;
  }
}

bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  const float k = fH / 480.0f;  // the game's 480 rows, as the overlay's counters are sized
  float afY[MODE_VIEW_ANCHORS];
  for (int i = 0; i < MODE_VIEW_ANCHORS; ++i)
    afY[i] = -1.0f;
  bool bDrew = false;
  for (const Part &part : g_parts) {
    const float fAlpha = Alpha(part);
    if (fAlpha <= 0.0f)
      continue;
    const int nAnchor = SDL_clamp(part.p.nAnchor, 0, MODE_VIEW_ANCHORS - 1);
    float x = 0, y = 0;
    int nAlign = 0;
    Place(nAnchor, fX, fY, fW, fH, k, &x, &y, &nAlign);
    if (afY[nAnchor] >= 0.0f)
      y = afY[nAnchor];
    const Block b = Shape(part, k, fAlpha);
    Paint(b, x, y, nAlign, k, fAlpha);
    afY[nAnchor] = y + b.fHeight + 16.0f * k;  // the next part of this anchor under it
    bDrew = true;
  }
  return bDrew;
}

const PortOverlayLayer kLayer = {
    "mode", PORT_OVERLAY_MODE, 0, LayerShown, NULL, LayerDraw, NULL, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);

// A name a script writes, in a table of its own.
int NameIn(const char *const *apsz, int n, const char *pszName)
{
  for (int i = 0; i < n; ++i)
    if (SDL_strcasecmp(apsz[i], pszName) == 0)
      return i;
  return -1;
}

}  // namespace

void PortModeViewSet(const PortModeViewPart &part)
{
  for (Part &p : g_parts)
    if (p.p.id == part.id && p.p.nKind == part.nKind) {
      // A timer set again with the same end goes on counting from where it was, and its bar
      // keeps emptying over the span it was given; the words and the tone take the new ones.
      const bool bSameEnd = part.nKind == MODE_VIEW_TIMER && part.nFrames >= 0 && p.p.nFrames >= 0 &&
                            static_cast<int>(p.nAt) + p.p.nFrames == static_cast<int>(Now()) + part.nFrames;
      const unsigned nAt = p.nAt;
      const int nFrames = p.p.nFrames, nFull = p.p.nFull;
      p.p = part;
      if (bSameEnd) {
        p.p.nFrames = nFrames;
        p.p.nFull = nFull;
        p.nAt = nAt;
      } else {
        p.nAt = Now();
      }
      return;
    }
  if (static_cast<int>(g_parts.size()) >= kPartsMax)
    return;
  Part p;
  p.p = part;
  p.nAt = Now();
  g_parts.push_back(p);
}

bool PortModeViewCounterSet(const char *pszId, double dValue)
{
  for (Part &p : g_parts)
    if (p.p.nKind == MODE_VIEW_COUNTER && p.p.id == pszId) {
      p.p.dValue = dValue;
      return true;
    }
  return false;
}

void PortModeViewClear(const char *pszId, int nKind)
{
  for (size_t i = 0; i < g_parts.size(); ++i)
    if (g_parts[i].p.id == pszId && g_parts[i].p.nKind == nKind) {
      g_parts.erase(g_parts.begin() + static_cast<long>(i));
      return;
    }
}

void PortModeViewClearAll(void)
{
  g_parts.clear();
}

int PortModeViewAnchorOf(const char *pszName)
{
  static const char *const kapsz[MODE_VIEW_ANCHORS] = {"top", "top_left", "top_right", "left", "right", "middle"};
  return NameIn(kapsz, MODE_VIEW_ANCHORS, pszName);
}

int PortModeViewStyleOf(const char *pszName)
{
  static const char *const kapsz[] = {"plain", "clock", "bar", "both"};
  return NameIn(kapsz, 4, pszName);
}

int PortModeViewToneOf(const char *pszName)
{
  static const char *const kapsz[] = {"plain", "good", "warn", "bad"};
  return NameIn(kapsz, 4, pszName);
}

int PortModeViewCount(void)
{
  return static_cast<int>(g_parts.size());
}

std::string PortModeViewLine(int i)
{
  if (i < 0 || i >= static_cast<int>(g_parts.size()))
    return std::string();
  const Part &part = g_parts[i];
  const PortModeViewPart &p = part.p;
  std::string line = p.nKind == MODE_VIEW_TIMER ? "timer " : p.nKind == MODE_VIEW_BANNER ? "banner " : "counter ";
  line += p.id + ": ";
  if (!p.label.empty())
    line += p.label + " ";
  if (p.nKind == MODE_VIEW_TIMER) {
    line += Clock(Left(part));
  } else if (p.nKind == MODE_VIEW_COUNTER) {
    line += Count(p.dValue);
    if (p.dOf != 0.0)
      line += " / " + Count(p.dOf);
  } else {
    for (size_t k = 0; k < p.lines.size(); ++k)
      line += (k == 0 ? "" : " | ") + p.lines[k];
  }
  return line;
}
