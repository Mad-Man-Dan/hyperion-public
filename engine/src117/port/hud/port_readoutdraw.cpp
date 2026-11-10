// port_readoutdraw.cpp -- the readout's full-colour drawing: what classic's own controls cannot
// carry. The words go in the controls themselves (port_readout.cpp, PortReadoutPartText), which
// keeps classic's font and places; the statuses' framed icons, the mark on the wireframe and the
// rank's pips are drawn here, in colour, over the panel.
//
// One routine draws into whatever surface it is given (port_hud.h, PortReadoutSurface), in the
// readout's own pixels with (0, 0) at classic's panel's top left, so the two consoles cannot
// drift: a design's selection slot makes a surface over its own layer, which comes out right
// under a panel the design turns, and the classic console makes one here over PortHudReadoutPlace.
// SDL's renderer draws no overlay at all, so there the readout is the words alone.
//
// Presentation only: nothing here is read by a game.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "CUnit.h"
#include "../../gen/globals117.h"      // gpStatUnit, the unit the panel describes
#include "port_readout.h"
#include "port_hud.h"                  // PortReadoutSurface, PortHudReadoutPlace, the registry
#include "../front/port_text.h"        // PortText: the stat a rolled change names
#include "../front/port_ui.h"          // the kit, for the classic console's own surface
#include "../render/port_console.h"    // PortConsoleHudOn: a design draws its own surface
#include "../render/port_overlay.h"
#include "../render/port_view.h"       // g_portView: the game's pixels in the target's

namespace {

// The two sides' colours, the design's own: a status working for the unit and one against it.
const unsigned int kFor = 0x6FE08A, kAgainst = 0xFF7A6A;
// what a framed icon is filled with, whichever side it is: its edge and its glyph carry the side
const unsigned int kIconFill = 0x060A0E;
// the panel's own grey for a word that is not a value: the design's line colour
const unsigned int kLine = 0xB9BFD0;

// How many statuses the wireframe's corner shows before the rest become "+n", and the icons'
// sizes there: the one that asks loudest, then a smaller one under it.
const int kCornerMost = 2;
const double kCornerBig = 15, kCornerSmall = 11;

// The statuses to mark, the one that asks loudest first: by its own priority, and among equals
// the one with least time left. Only the lines a status's look lets onto the wireframe count.
void MarksOf(const PortReadoutView &view, std::vector<const PortReadoutLine *> *pOut, int *pnAll) {
  *pnAll = 0;
  for (const PortReadoutLine &line : view.lines) {
    if (line.status.empty() || !line.bShowWire)
      continue;
    ++*pnAll;
    size_t at = 0;
    while (at < pOut->size() && ((*pOut)[at]->nPriority > line.nPriority ||
                                 ((*pOut)[at]->nPriority == line.nPriority && (*pOut)[at]->fLeft <= line.fLeft)))
      ++at;
    pOut->insert(pOut->begin() + static_cast<long>(at), &line);
  }
}

// A filled diamond of z across at (x, y), a row at a time: the surface fills rectangles, and a
// diamond is what the design draws for a status working against the unit.
void Diamond(const PortReadoutSurface *s, double x, double y, double z, unsigned int nRgb, double fAlpha) {
  const int nRows = static_cast<int>(z);
  for (int i = 0; i < nRows; ++i) {
    const double fy = i + 0.5 - z / 2, fAway = fy < 0 ? -fy : fy;
    const double fHalf = z / 2 - fAway;
    if (fHalf <= 0)
      continue;
    s->pfnFill(s->pCtx, x + z / 2 - fHalf, y + i, fHalf * 2, 1, nRgb, fAlpha, 0);
  }
}

// One status's icon in a box of z at (x, y): its side's frame, a disc for a status that helps the
// unit and a diamond for one that hurts, the kind's glyph inside it in the kind's hue, the time
// left running out round it, and the stacks in the corner. This is the icon the user picked; a
// mod that asks for the glyph alone, or for a classic command icon, says so in "status_look".
void FramedIcon(const PortReadoutSurface *s, const PortReadoutLine &line, double x, double y, double z) {
  const int nStyle = PortReadoutOf() != NULL ? PortReadoutOf()->statusLook.nIcon : PORT_READOUT_ICON_FRAMED;
  if (nStyle == PORT_READOUT_ICON_NONE)
    return;
  // the frame is near-black whichever side it is, and its edge and its glyph carry the colour: a
  // colour the status named for itself where it named one, else the side's green or red. A kind's
  // own hue is not drawn here, since the side is what an icon has to say first.
  const unsigned int nSide = line.bHue ? line.nHue : (line.bGood ? kFor : kAgainst);
  if (nStyle == PORT_READOUT_ICON_FRAMED) {
    const double fEdge = z / 14 > 0.6 ? z / 14 : 0.6;
    if (line.bGood) {
      s->pfnFill(s->pCtx, x, y, z, z, kIconFill, 0.92, z / 2);
      // the disc's edge, drawn as the arc below is since the surface fills and lines only
      const int nSteps = 20;
      const double fR = z / 2 - 0.5, fCx = x + z / 2, fCy = y + z / 2;
      for (int i = 0; i < nSteps; ++i) {
        const double a0 = 6.2831853 * i / nSteps, a1 = 6.2831853 * (i + 1) / nSteps;
        s->pfnLine(s->pCtx, fCx + cos(a0) * fR, fCy + sin(a0) * fR, fCx + cos(a1) * fR,
                   fCy + sin(a1) * fR, nSide, fEdge, 0.45);
      }
    } else {
      Diamond(s, x, y, z, kIconFill, 0.92);
      const double fCx = x + z / 2, fCy = y + z / 2, fR = z / 2 - 0.3;
      s->pfnLine(s->pCtx, fCx, fCy - fR, fCx + fR, fCy, nSide, fEdge, 0.45);
      s->pfnLine(s->pCtx, fCx + fR, fCy, fCx, fCy + fR, nSide, fEdge, 0.45);
      s->pfnLine(s->pCtx, fCx, fCy + fR, fCx - fR, fCy, nSide, fEdge, 0.45);
      s->pfnLine(s->pCtx, fCx - fR, fCy, fCx, fCy - fR, nSide, fEdge, 0.45);
    }
    s->pfnGlyph(s->pCtx, line.nKind, x + z * 0.22, y + z * 0.22, z * 0.56, nSide, 1.0);
  } else {
    // the glyph alone, at the box's own size: there is no frame to sit inside
    s->pfnGlyph(s->pCtx, line.nKind, x, y, z, nSide, 1.0);
  }
  // the time left as an arc round the frame, running out clockwise from the top. Left off where
  // the icon is fewer than twelve of the surface's own pixels across, which is where an arc
  // becomes a smudge: the room's rows say the time in their words and on their bar as well.
  if (line.fOf > 0 && z * s->fPx >= 12) {
    const double fPart = line.fLeft / line.fOf > 1 ? 1 : (line.fLeft > 0 ? line.fLeft / line.fOf : 0);
    const int nSteps = 20;
    // round the frame's own edge, not past it: the gap classic leaves beside the kills' row is
    // twelve pixels and an arc wider than the icon laps the wireframe and the words
    const double fR = z / 2, fCx = x + z / 2, fCy = y + z / 2;
    const double fWide = z / 9 > 0.9 ? z / 9 : 0.9;  // the design's own, thicker than the edge
    const int nDraw = static_cast<int>(fPart * nSteps);
    for (int i = 0; i < nDraw; ++i) {
      const double a0 = -1.5707963 + 6.2831853 * i / nSteps, a1 = -1.5707963 + 6.2831853 * (i + 1) / nSteps;
      s->pfnLine(s->pCtx, fCx + cos(a0) * fR, fCy + sin(a0) * fR, fCx + cos(a1) * fR, fCy + sin(a1) * fR, nSide,
                 fWide, 0.9);
    }
  }
  // the stacks, which say what a status is worth: a four-deep one takes four times as much, so
  // the count goes on the smaller icon as well, drawn to its box. Left off where the box would
  // show a digit of less than six of the surface's own pixels, which is no longer a digit.
  if (line.nStacks > 1 && z >= kCornerSmall) {
    const double fSize = z * 0.55;
    if (fSize * s->fPx >= 6) {
      char sz[16] = "";
      snprintf(sz, sizeof sz, "%d", line.nStacks);
      s->pfnText(s->pCtx, sz, x + z, y + z - 1, fSize, 0xFFFFFF, 1.0, 2, 1);
    }
  }
}

// The rolled changes of a part as a dark tab, where the file asks for them there ("delta": "on
// the icon"): a chevron for the way each figure went and how far it moved, green where the unit
// gains by it and red where it loses -- a faster cooldown being a gain, though the number falls.
// This is the design's own form, and the only place the figures show on the classic console.
//
// A tab carries one figure or two: a weapon's icon carries the damage and that weapon's cooldown,
// so both of an attack's rolled figures are read in one place. bRight puts the tab at the rect's
// right end rather than its left, for the life row, whose own figures classic centres.
const int kFiguresMost = 2;
void DeltaTab(const PortReadoutSurface *s, const PortReadoutRect &rc, const int *panStats,
              const int *panBy, int nFigures, bool bRight) {
  const double fPx = 7, fLead = 2, fArrow = 3, fToText = 1, fTail = 1.5, fBetween = 4;
  int anStat[kFiguresMost], anBy[kFiguresMost];
  char aszBy[kFiguresMost][24];
  double afWide[kFiguresMost];
  int n = 0;
  for (int i = 0; i < nFigures && i < kFiguresMost; ++i) {
    if (panBy[i] == 0)
      continue;
    anStat[n] = panStats[i];
    anBy[n] = panBy[i];
    snprintf(aszBy[n], sizeof aszBy[0], "%d%s", panBy[i] < 0 ? -panBy[i] : panBy[i],
             PortReadoutStatIsCount(panStats[i]) ? "" : "%");
    afWide[n] = s->pfnTextWidth(s->pCtx, aszBy[n], fPx, 1);
    ++n;
  }
  if (n == 0)
    return;
  double fWide = fTail;
  for (int i = 0; i < n; ++i)
    fWide += (i > 0 ? fBetween : fLead) + fArrow + fToText + afWide[i];
  const double x0 = bRight ? rc.x + rc.w - fWide - 1 : rc.x + 1, y0 = rc.y + 1;
  s->pfnFill(s->pCtx, x0, y0, fWide, fPx + 2.5, 0x060A0E, 0.72, 1);
  double x = x0;
  for (int i = 0; i < n; ++i) {
    const bool bGood = PortReadoutStatLowerIsBetter(anStat[i]) ? anBy[i] < 0 : anBy[i] > 0;
    const unsigned int nInk = bGood ? kFor : kAgainst;
    x += i > 0 ? fBetween : fLead;
    // the chevron: up where the number rose, down where it fell
    const double y = y0 + 1.5;
    if (anBy[i] > 0) {
      s->pfnLine(s->pCtx, x, y + 4, x + 1.5, y + 1, nInk, 1.0, 1.0);
      s->pfnLine(s->pCtx, x + 1.5, y + 1, x + 3, y + 4, nInk, 1.0, 1.0);
    } else {
      s->pfnLine(s->pCtx, x, y + 1, x + 1.5, y + 4, nInk, 1.0, 1.0);
      s->pfnLine(s->pCtx, x + 1.5, y + 4, x + 3, y + 1, nInk, 1.0, 1.0);
    }
    x += fArrow + fToText;
    s->pfnText(s->pCtx, aszBy[i], x, y0 + 1.0 + fPx / 2, fPx, nInk, 1.0, 0, 1);
    x += afWide[i];
  }
}

// A kind's pattern over the wireframe, where the look asks for one: the pattern's name decides
// how it is laid down, since each kind's own name ("bars", "cracks", "hex") says a shape rather
// than a picture. Drawn thin and faint, so the wireframe under it is still read first.
void Pattern(const PortReadoutSurface *s, const PortReadoutLine &line, const PortReadoutRect &rc) {
  const char *pszPattern = PortReadoutKindPattern(line.nKind);
  const unsigned int nHue = line.nHue != 0xFFFFFFu ? line.nHue : (line.bGood ? kFor : kAgainst);
  if (pszPattern == NULL || *pszPattern == '\0' || strcmp(pszPattern, "none") == 0)
    return;
  const double fStep = 7, fAlpha = 0.28;
  if (strcmp(pszPattern, "bars") == 0) {
    for (double x = rc.x + 3; x < rc.x + rc.w; x += fStep)
      s->pfnLine(s->pCtx, x, rc.y + 1, x, rc.y + rc.h - 1, nHue, 1.0, fAlpha);
    return;
  }
  if (strcmp(pszPattern, "cross") == 0 || strcmp(pszPattern, "pluses") == 0) {
    for (double y = rc.y + 4; y < rc.y + rc.h; y += fStep)
      for (double x = rc.x + 4; x < rc.x + rc.w; x += fStep) {
        s->pfnLine(s->pCtx, x - 2, y, x + 2, y, nHue, 1.0, fAlpha);
        s->pfnLine(s->pCtx, x, y - 2, x, y + 2, nHue, 1.0, fAlpha);
      }
    return;
  }
  if (strcmp(pszPattern, "embers") == 0 || strcmp(pszPattern, "static") == 0 ||
      strcmp(pszPattern, "sparse") == 0 || strcmp(pszPattern, "frost") == 0) {
    for (double y = rc.y + 3; y < rc.y + rc.h; y += fStep)
      for (double x = rc.x + 3 + (static_cast<int>(y) % 2 ? fStep / 2 : 0); x < rc.x + rc.w; x += fStep)
        s->pfnFill(s->pCtx, x, y, 1.5, 1.5, nHue, fAlpha, 0.75);
    return;
  }
  if (strcmp(pszPattern, "cracks") == 0) {
    for (double y = rc.y + 5; y < rc.y + rc.h; y += fStep + 2)
      for (double x = rc.x + 4; x < rc.x + rc.w; x += fStep + 3)
        s->pfnLine(s->pCtx, x, y, x + 3, y + 4, nHue, 1.0, fAlpha);
    return;
  }
  // the rest read as a slant: stripes, streaks, chevrons and the hex of armour
  for (double x = rc.x - rc.h; x < rc.x + rc.w; x += fStep)
    s->pfnLine(s->pCtx, x, rc.y + rc.h - 1, x + rc.h, rc.y + 1, nHue, 1.0, fAlpha);
}

// ---- the effects' light --------------------------------------------------------------------------
// Every op is drawn as light over the part's own rectangle. The words in the panel are classic's
// own eight-bit pixels and cannot be dimmed from here, so a blink and a beat are light laid on
// rather than the words going out; only a float's own text moves, by the user's rule for the HUD.

double Ease(double p) { return 1 - (1 - p) * (1 - p) * (1 - p); }

// How bright a run is now: one that holds stays up, one with a hold rises and then falls over it,
// and the rest fade from full.
double Fade(const PortReadoutRun &run) {
  if (run.bHeld)
    return 1.0;
  if (run.fHold > 0) {
    if (run.fAge < run.fTime)
      return run.fThrough;
    const double f = (run.fAge - run.fTime) / run.fHold;
    return f < 1 ? 1 - f : 0;
  }
  return 1 - run.fThrough;
}

// A band of light round a rectangle rather than over it, so the part under it still reads: the
// four sides, each a row at a time, fainter the further out it goes.
void Halo(const PortReadoutSurface *s, const PortReadoutRect &rc, unsigned int nRgb, double fAlpha,
          double fSize) {
  const int nRings = static_cast<int>(fSize < 1 ? 3 : (fSize > 6 ? 6 : fSize));
  for (int i = 1; i <= nRings; ++i) {
    const double a = fAlpha * (1.0 - static_cast<double>(i - 1) / nRings);
    if (a <= 0.01)
      continue;
    const double x = rc.x - i, y = rc.y - i, w = rc.w + i * 2.0, h = rc.h + i * 2.0;
    s->pfnFill(s->pCtx, x, y, w, 1, nRgb, a, 0);
    s->pfnFill(s->pCtx, x, y + h - 1, w, 1, nRgb, a, 0);
    s->pfnFill(s->pCtx, x, y + 1, 1, h - 2, nRgb, a, 0);
    s->pfnFill(s->pCtx, x + w - 1, y + 1, 1, h - 2, nRgb, a, 0);
  }
}

// A narrow band of light across a rectangle, at fx of its width: three fills, the middle one
// brightest, which is what a glint and a shimmer both come to.
void Streak(const PortReadoutSurface *s, const PortReadoutRect &rc, unsigned int nRgb, double fAlpha,
            double fx) {
  const double x = rc.x + fx * (rc.w + 12.0) - 6.0;
  s->pfnFill(s->pCtx, x - 2, rc.y, 2, rc.h, nRgb, fAlpha * 0.3, 0);
  s->pfnFill(s->pCtx, x, rc.y, 3, rc.h, nRgb, fAlpha, 0);
  s->pfnFill(s->pCtx, x + 3, rc.y, 2, rc.h, nRgb, fAlpha * 0.3, 0);
}

// One run's light over a rectangle, nOwn the colour where the effect names none. With bText only
// the text an op draws of its own, which goes over everything else.
void RunLight(const PortReadoutSurface *s, const PortReadoutRun &run, const PortReadoutRect &rc,
              unsigned int nOwn, bool bText) {
  const unsigned int nRgb = run.bColour ? run.nRgb : nOwn;
  const bool bQuiet = PortReadoutReduceMotion();
  const double fFade = Fade(run);
  if (fFade <= 0.01 && !run.bHeld)
    return;
  if (bText) {
    if (run.nOp == PORT_READOUT_FLOAT && run.szText[0] != '\0')
      s->pfnText(s->pCtx, run.szText, rc.x + rc.w / 2.0,
                 rc.y + rc.h / 2.0 - 16.0 * Ease(run.fThrough), 9, nRgb,
                 1.0 - run.fThrough * run.fThrough, 1, 1);
    return;
  }
  switch (run.nOp) {
  case PORT_READOUT_FLASH:
    // half as bright for a player who asked for less motion, as the player's rule has it
    s->pfnFill(s->pCtx, rc.x, rc.y, rc.w, rc.h, nRgb, (bQuiet ? 0.22 : 0.45) * fFade, 1);
    break;
  case PORT_READOUT_LIGHT: {
    const double a = 0.45 * (0.3 + 0.7 * run.fLight) * fFade;
    s->pfnFill(s->pCtx, rc.x, rc.y, rc.w, rc.h, nRgb, a, 1);
    if (run.bGlint)
      Streak(s, rc, nRgb, 0.5 * fFade, run.fThrough);
    break;
  }
  case PORT_READOUT_BEAT: {
    // light that comes and goes at its rate, and never a part that grows
    const double fBeat = sin(6.2831853 * run.fRate * run.fPhase);
    const double fUp = fBeat > 0 ? pow(fBeat, 6) : 0;
    if (fUp > 0.02) {
      s->pfnFill(s->pCtx, rc.x, rc.y, rc.w, rc.h, nRgb, 0.5 * run.fLight * fUp * fFade, 1);
      Halo(s, rc, nRgb, 0.4 * fUp * fFade, 3);
    }
    break;
  }
  case PORT_READOUT_BLINK: {
    const double fRate = run.fRate > 3.0 ? 3.0 : run.fRate;  // three a second for everyone
    const double fTurn = fmod(run.fPhase * fRate * 2.0, 2.0);
    if (fTurn < 1.0)
      s->pfnFill(s->pCtx, rc.x, rc.y, rc.w, rc.h, nRgb, 0.3 * fFade, 1);
    break;
  }
  case PORT_READOUT_GLOW: {
    const double fBreath = 0.55 + 0.45 * sin(6.2831853 * run.fRate * run.fPhase);
    Halo(s, rc, nRgb, 0.5 * fBreath * fFade, run.fSize);
    break;
  }
  case PORT_READOUT_SCAN: {
    // a line down the part, its tail behind it
    const double y = rc.y + rc.h * Ease(run.fThrough);
    for (int i = 1; i <= 3; ++i) {
      const double yy = y - i * 2.0;
      if (yy >= rc.y)
        s->pfnFill(s->pCtx, rc.x, yy, rc.w, 2, nRgb, 0.14 * (4 - i) * fFade, 0);
    }
    s->pfnFill(s->pCtx, rc.x, y, rc.w, 1, nRgb, 0.9 * fFade, 0);
    break;
  }
  case PORT_READOUT_TYPE: {
    // the words are classic's own and cannot be typed in from here, so the light fills the line
    // from its start as they would, and goes out as the last of them lands
    const double fOut = run.fThrough < 0.85 ? 1.0 : 1.0 - (run.fThrough - 0.85) / 0.15;
    s->pfnFill(s->pCtx, rc.x, rc.y, rc.w * run.fThrough, rc.h, nRgb, 0.18 * fOut, 1);
    break;
  }
  case PORT_READOUT_SHIMMER: {
    const double fEvery = run.fEvery > 0.5 ? run.fEvery : 3.0;
    const double fOne = run.fTime > 0.05 ? run.fTime : 0.5;
    const double f = fmod(run.fPhase, fEvery);
    if (f < fOne)
      Streak(s, rc, nRgb, 0.55, f / fOne);
    break;
  }
  case PORT_READOUT_SLIDE: {
    // a band of light in from its side, where the line itself cannot move
    const double f = Ease(run.fThrough);
    const double x = run.bFromRight ? rc.x + rc.w - f * rc.w : rc.x + f * rc.w;
    s->pfnFill(s->pCtx, run.bFromRight ? x : rc.x, rc.y, run.bFromRight ? rc.w - (x - rc.x) : x - rc.x,
               rc.h, nRgb, 0.16 * (1 - run.fThrough), 1);
    s->pfnFill(s->pCtx, x - 1, rc.y, 2, rc.h, nRgb, 0.7 * (1 - run.fThrough), 0);
    break;
  }
  default:
    break;  // roll counts the panel's own numbers and float draws its own text
  }
}

// The light of every effect running on classic's seven parts. The slots part is asked for each
// icon by the stat that icon carries, so an effect on "slots.{stat}" lights that icon alone.
void Effects(const PortReadoutSurface *s, const PortReadoutView &view, CUnit *pUnit, bool bText) {
  const unsigned int nOwn = view.nRankAt >= 0 ? view.nRankRgb : 0xE4EEF6;
  PortReadoutRun aRuns[PORT_READOUT_RUNS_MOST];
  for (int nPart = 0; nPart < PORT_READOUT_PARTS; ++nPart) {
    const PortReadoutRect rc = PortReadoutPartRect(nPart);
    const int n = PortReadoutRuns(nPart, NULL, aRuns, PORT_READOUT_RUNS_MOST);
    for (int i = 0; i < n; ++i)
      RunLight(s, aRuns[i], rc, nOwn, bText);
    if (nPart != PORT_READOUT_SLOTS)
      continue;
    for (int nSlot = 0; nSlot < PORT_READOUT_SLOTS_MOST; ++nSlot) {
      const int nStat = PortReadoutSlotStat(pUnit, nSlot);
      if (nStat < 0)
        continue;
      const int nOne = PortReadoutRuns(nPart, PortReadoutStatName(nStat), aRuns, PORT_READOUT_RUNS_MOST);
      for (int i = 0; i < nOne; ++i)
        RunLight(s, aRuns[i], PortReadoutSlotRect(nSlot), nOwn, bText);
    }
  }
}

// How far a status's line has still to slide in, 0 for one that is not sliding: a slide on the
// line parts names the status that landed, and the rows in a HUD's room slide with it.
double SlideOf(const char *pszStatus, bool *pbFromRight) {
  if (pszStatus == NULL || *pszStatus == '\0' || PortReadoutReduceMotion())
    return 0;
  const int anParts[2] = {PORT_READOUT_STATUS, PORT_READOUT_KILLS};
  PortReadoutRun aRuns[PORT_READOUT_RUNS_MOST];
  for (int k = 0; k < 2; ++k) {
    const int n = PortReadoutRuns(anParts[k], NULL, aRuns, PORT_READOUT_RUNS_MOST);
    for (int i = 0; i < n; ++i)
      if (aRuns[i].nOp == PORT_READOUT_SLIDE && strcmp(aRuns[i].szStatus, pszStatus) == 0) {
        *pbFromRight = aRuns[i].bFromRight;
        return 1.0 - Ease(aRuns[i].fThrough);
      }
  }
  return 0;
}

// How bright a status's own row in the room is: 1, or the dim half of a blink held while that
// status runs out. The effect is written on the lines ("part": "lines", which is the row the
// panel shows them on), so in the room it reaches the row of the status the test is holding for.
double BlinkOf(const PortReadoutLine &line) {
  // either line row: an effect on the lines runs on the row the panel shows them on, which is one
  // or the other depending on whether the mod adds lines at all
  const int anParts[2] = {PORT_READOUT_STATUS, PORT_READOUT_KILLS};
  for (int k = 0; k < 2; ++k) {
    PortReadoutRun aRuns[PORT_READOUT_RUNS_MOST];
    const int n = PortReadoutRuns(anParts[k], NULL, aRuns, PORT_READOUT_RUNS_MOST);
    for (int i = 0; i < n; ++i) {
      const PortReadoutRun &run = aRuns[i];
      if (run.nOp != PORT_READOUT_BLINK || !run.bHeld)
        continue;
      // matched to the status, as a slide is: the test's own figure says which rows it holds for,
      // so a row never blinks for a status two rows down
      if (run.fAt > 0 && !(line.fOf > 0 && line.fLeft < run.fAt))
        continue;
      const double fRate = run.fRate > 0 ? run.fRate : 1.0;
      if (static_cast<long long>(run.fPhase * fRate * 2) % 2)
        return 0.35;  // the design's own dim half
    }
  }
  return 1.0;
}

void WireMark(const PortReadoutSurface *s, const PortReadoutFile &file, const PortReadoutView &view);

// The whole drawing, in the readout's own pixels.
void Paint(const PortReadoutSurface *s) {
  const PortReadoutFile *pFile = PortReadoutOf();
  CUnit *pUnit = gpStatUnit;
  if (s == NULL || pFile == NULL || pUnit == NULL)
    return;
  const PortReadoutView *pView = PortReadoutUnit(pUnit);
  if (pView == NULL || !pView->bOn)
    return;

  // the effects' light, under the icons and the marks so those still read
  Effects(s, *pView, pUnit, false);

  // the rank's pips: none for the first rank the file names, whose own colour on the name already
  // says it, one for the next and one more for each above that, in the rank's own colour, which is
  // exact here as it cannot be in the panel. Diamonds in a row just
  // before the rank's own words and centred on their line, as the design has them, so they read
  // as part of that line and leave the row below to the status's icon. The panel centres a line,
  // so the words themselves are what says where to start (PortReadoutPartWords).
  if (pFile->parts.bRankPips && pView->nRankAt > 0) {
    const PortReadoutRect rc = PortReadoutPartRect(PORT_READOUT_STATUS);
    const int nPips = pView->nRankAt;
    const double fWords = s->pfnTextWidth(s->pCtx, PortReadoutPartWords(PORT_READOUT_STATUS), 8, 0);
    const double fCy = rc.y + rc.h / 2.0;
    double fAt = rc.x + rc.w / 2.0 - fWords / 2.0 - 6.0;
    for (int i = 0; i < nPips && i < 6 && fAt > rc.x - 10.0; ++i) {
      Diamond(s, fAt - 3.25, fCy - 3.25, 6.5, 0x000000, 0.9);
      Diamond(s, fAt - 2.5, fCy - 2.5, 5.0, pView->nRankRgb, 1.0);
      fAt -= 5.5;
    }
  }

  // the rolled changes in each stat icon's corner, where the file puts them there. A weapon's
  // icon takes the cooldown beside the damage, since the cooldown has no icon of its own and is
  // that weapon's; the life takes a tab of its own at the right end of its row, clear of the
  // figures classic centres there. The speed is left to the words: there is no place for it here.
  if (pFile->parts.nDelta == PORT_READOUT_DELTA_ICON) {
    for (int nSlot = 0; nSlot < PORT_READOUT_SLOTS_MOST; ++nSlot) {
      const int nStat = PortReadoutSlotStat(pUnit, nSlot);
      if (nStat < 0)
        continue;
      int anStats[kFiguresMost] = {nStat, PORT_READOUT_STAT_COOLDOWN};
      int anBy[kFiguresMost] = {pView->abDelta[nStat] ? pView->anDelta[nStat] : 0, 0};
      int nFigures = 1;
      if (nStat == PORT_READOUT_STAT_DAMAGE && pView->abDelta[PORT_READOUT_STAT_COOLDOWN]) {
        anBy[1] = pView->anDelta[PORT_READOUT_STAT_COOLDOWN];
        nFigures = 2;
      }
      DeltaTab(s, PortReadoutSlotRect(nSlot), anStats, anBy, nFigures, false);
    }
    if (pView->abDelta[PORT_READOUT_STAT_LIFE]) {
      const int nStat = PORT_READOUT_STAT_LIFE, nBy = pView->anDelta[PORT_READOUT_STAT_LIFE];
      DeltaTab(s, PortReadoutPartRect(PORT_READOUT_LIFE), &nStat, &nBy, 1, true);
    }
  }

  // the kills' row, which the lines a mod adds take in turn with it: the status's own icon at the
  // line's start, its time left as a bar under the words, and which line of how many is showing
  const PortReadoutRect row = PortReadoutPartRect(PORT_READOUT_KILLS);
  const PortReadoutLine *pRow = PortReadoutRowLine();
  if (pRow != NULL && !pRow->status.empty()) {
    // the icon in the gap classic leaves between the wireframe and its rows, so the words keep
    // their own place: the gap's own twelve pixels, 94 to 106
    FramedIcon(s, *pRow, row.x - 12.0, row.y + 1.0, 12);
    // the bar: the design's own place for it, under the line and the width of the words' room
    if (pFile->add.bBar && pRow->fOf > 0) {
      const double fPart = pRow->fLeft / pRow->fOf > 1 ? 1 : (pRow->fLeft > 0 ? pRow->fLeft / pRow->fOf : 0);
      const unsigned int nInk = pRow->bGood ? kFor : kAgainst;
      s->pfnFill(s->pCtx, 123, row.y + 11.5, 110, 1.2, 0xFFFFFF, 0.12, 0);
      s->pfnFill(s->pCtx, 123, row.y + 11.5, 110 * fPart, 1.2, nInk, 1.0, 0);
    }
  }
  // which of how many, where the row has more than one line to show: at the row's own right end,
  // in the panel's grey, as the design has it
  int nTurnAt = 0, nTurnOf = 0;
  double fTurnPhase = 0, fTurnEach = 0;
  const bool bTurn = PortReadoutRowTurn(&nTurnAt, &nTurnOf, &fTurnPhase, &fTurnEach);
  if (bTurn) {
    char sz[24] = "";
    snprintf(sz, sizeof sz, "%d/%d", nTurnAt + 1, nTurnOf);
    s->pfnText(s->pCtx, sz, 254, row.y + 8.0, 6, kLine, 0.55, 2, 0);
    // the design fades the row out over the last 0.22 s of a turn and in over the first. The
    // words are classic's own palettized pixels and cannot be dimmed from here, so the overlay
    // lays a dark veil over the row instead, which reads as the same dip; a player who asked for
    // no motion keeps the row steady, as the design does.
    if (!PortReadoutReduceMotion()) {
      const double fDip = fTurnPhase > fTurnEach - 0.22 ? (fTurnPhase - (fTurnEach - 0.22)) / 0.22
                          : fTurnPhase < 0.22          ? 1.0 - fTurnPhase / 0.22
                                                       : 0.0;
      if (fDip > 0.01)
        s->pfnFill(s->pCtx, row.x - 12.0, row.y, row.w + 12.0, row.h, 0x05080C, 0.7 * fDip, 0);
    }
  }

  // the lines a mod adds, in the room the readout has past classic's panel: a row each, its
  // status's framed icon at the start, the words beside it and the time left as a bar under them
  double fRoomX = 0, fRoomY = 0, fRoomW = 0, fRoomH = 0;
  if (pFile->add.bLines && PortReadoutRoomBox(&fRoomX, &fRoomY, &fRoomW, &fRoomH)) {
    // the design's own sizes, in the readout's pixels: an 11-pixel row for a status, an
    // 8-pixel icon at its start, and words of 7
    const double fRow = 11, fIcon = 8, fText = 7;
    // a narrow room takes the file's short line, as classic's own row does
    const bool bShort = fRoomW < 120;
    double y = fRoomY;
    // the design's own order: a row for each status first, since those are what changes and what
    // runs out, then the hook's own lines under them, then what the unit rolled. A status's row
    // shows where the file asks the room to list them ("in_huds"); with "in turn" they rotate on
    // the panel's own row instead and the room keeps to the lines below.
    for (const PortReadoutLine &line : pView->lines) {
      if (line.status.empty() || !PortReadoutRoomLists() || y + fRow > fRoomY + fRoomH)
        continue;
      // a status that has just landed slides its row in from its side, with light behind it in
      // the side's colour; the row itself keeps its place once the slide is through
      bool bFromRight = true;
      const double fIn = SlideOf(line.status.c_str(), &bFromRight);
      const double fDx = fIn * (bFromRight ? 26.0 : -26.0);
      // the colour it named for itself where it named one, as its icon takes it, else its side's
      const unsigned int nInk = line.bHue ? line.nHue : (line.bGood ? kFor : kAgainst);
      // a blink held while this status runs out dims its words and its bar; the icon keeps its
      // own frame, since a frame going half-lit reads as another status rather than as one blink
      const double fLit = BlinkOf(line);
      if (fIn > 0)
        s->pfnFill(s->pCtx, fRoomX + fDx - 2, y, fRoomW, fRow - 2, nInk, 0.2 * fIn, 2);
      FramedIcon(s, line, fRoomX + fDx, y + 1, fIcon);
      // the file's own line for a status ("each", or "each_short" where the row is narrow)
      s->pfnText(s->pCtx, PortReadoutLineText(&line, bShort), fRoomX + fDx + fIcon + 3,
                 y + fIcon / 2, fText, nInk, fLit, 0, 0);
      // the bar: how much of its time is left, under the words
      if (pFile->add.bBar && line.fOf > 0) {
        const double fPart = line.fLeft / line.fOf > 1 ? 1 : (line.fLeft > 0 ? line.fLeft / line.fOf : 0);
        const double fBarX = fRoomX + fDx, fBarW = fRoomW - 2;
        s->pfnFill(s->pCtx, fBarX, y + fIcon + 1, fBarW, 1.5, 0xFFFFFF, 0.18 * fLit, 1);
        s->pfnFill(s->pCtx, fBarX, y + fIcon + 1, fBarW * fPart, 1.5, nInk, 0.9 * fLit, 1);
      }
      y += fRow;
    }
    // then the hook's own lines, in the order it wrote them: the rank, what the unit is, its
    // affixes. A line of the hook's has no icon and no time of its own, so it takes a shorter row.
    for (const PortReadoutLine &line : pView->lines) {
      if (!line.status.empty() || y + 9 > fRoomY + fRoomH)
        continue;
      const unsigned int nInk = line.colour.nInk == PORT_READOUT_INK_RGB      ? line.colour.nRgb
                                : line.colour.nInk == PORT_READOUT_INK_RANK ? pView->nRankRgb
                                                                            : 0xE4EEF6;
      // its words through the readout's values, as the panel writes them, so a brace in a line
      // of the hook's reads the same in both places
      s->pfnText(s->pCtx, PortReadoutLineText(&line, bShort), fRoomX, y + 4, fText, nInk, 1.0, 0, 0);
      y += 9;
    }
    // last, what the unit rolled against the base row, where the hook gave changes: each stat and
    // how far it moved, green the way the unit gains and red the way it loses, along the row and
    // wrapped. The tabs carry the same figures on the console; here there is room to name them.
    double fAt = fRoomX;
    for (int nStat = 0; nStat < PORT_READOUT_STATS && y + 10 <= fRoomY + fRoomH; ++nStat) {
      if (!pView->abDelta[nStat] || pView->anDelta[nStat] == 0)
        continue;
      const int nBy = pView->anDelta[nStat];
      char szBy[24] = "";
      snprintf(szBy, sizeof szBy, "%s%d%s", nBy > 0 ? "+" : "", nBy,
               PortReadoutStatIsCount(nStat) ? "" : "%");
      const char *pszWords = PortText(PortReadoutStatWords(nStat));
      const double fName = s->pfnTextWidth(s->pCtx, pszWords, fText, 0);
      const double fWide = fName + s->pfnTextWidth(s->pCtx, szBy, fText, 1) + 8.0;
      if (fAt > fRoomX && fAt + fWide > fRoomX + fRoomW) {
        fAt = fRoomX;
        y += 9;
        if (y + 9 > fRoomY + fRoomH)
          break;
      }
      const bool bGood = PortReadoutStatLowerIsBetter(nStat) ? nBy < 0 : nBy > 0;
      s->pfnText(s->pCtx, pszWords, fAt, y + 4, fText, 0xA8B4C2, 1.0, 0, 0);
      s->pfnText(s->pCtx, szBy, fAt + fName + 3.0, y + 4, fText, bGood ? kFor : kAgainst, 1.0, 0, 1);
      fAt += fWide;
    }
  }

  WireMark(s, *pFile, *pView);
  // the text an op draws of its own, over everything else
  Effects(s, *pView, pUnit, true);
}

// The wireframe's mark: the statuses that ask loudest, in the way the look asks for.
void WireMark(const PortReadoutSurface *s, const PortReadoutFile &file, const PortReadoutView &view) {
  std::vector<const PortReadoutLine *> marks;
  int nAll = 0;
  MarksOf(view, &marks, &nAll);
  if (marks.empty() || file.statusLook.nWireMark == PORT_READOUT_WIRE_MARK_NONE)
    return;
  const PortReadoutRect wire = PortReadoutPartRect(PORT_READOUT_WIRE);
  if (file.statusLook.nWireMark == PORT_READOUT_WIRE_TINT) {
    const unsigned int nHue = marks[0]->nHue != 0xFFFFFFu ? marks[0]->nHue : (marks[0]->bGood ? kFor : kAgainst);
    s->pfnFill(s->pCtx, wire.x, wire.y, wire.w, wire.h, nHue, 0.22, 2);
    return;
  }
  if (file.statusLook.nWireMark == PORT_READOUT_WIRE_PATTERN) {
    Pattern(s, *marks[0], wire);
    return;
  }
  // the corner: the loudest status's icon in the wireframe's top left, the next smaller and
  // indented under it, and what is left over as a count under both -- the design's own places
  for (size_t i = 0; i < marks.size() && i < static_cast<size_t>(kCornerMost); ++i) {
    if (i == 0)
      FramedIcon(s, *marks[0], wire.x + 1, wire.y + 1, kCornerBig);
    else
      FramedIcon(s, *marks[1], wire.x + 3, wire.y + 18, kCornerSmall);
  }
  if (nAll > kCornerMost) {
    char sz[16] = "";
    snprintf(sz, sizeof sz, "+%d", nAll - kCornerMost);
    s->pfnText(s->pCtx, sz, wire.x + 8.5, wire.y + 35, 6, kLine, 1.0, 1, 0);
  }
}

// ---- the classic console's own surface -----------------------------------------------------------
// The panel there is the StatData dialog's rectangle in the game's pixels, which the overlay
// draws over through the kit. A design's slot makes its own surface and this layer stays off.
struct Sink {
  double fX = 0, fY = 0;   // the game's box in the target
  double fK = 1;           // the target's pixels a game pixel
  double fPanelX = 0, fPanelY = 0, fScale = 1;  // the panel in the game's pixels
  double X(double x) const { return fX + (fPanelX + x * fScale) * fK; }
  double Y(double y) const { return fY + (fPanelY + y * fScale) * fK; }
  double Z(double z) const { return z * fScale * fK; }
};

void SinkFill(void *pCtx, double x, double y, double w, double h, unsigned int nRgb, double fAlpha,
              double fRadius) {
  const Sink *p = static_cast<const Sink *>(pCtx);
  UiFill(static_cast<float>(p->X(x)), static_cast<float>(p->Y(y)), static_cast<float>(p->Z(w)),
         static_cast<float>(p->Z(h)), UiRgb(nRgb, static_cast<float>(fAlpha)), static_cast<float>(p->Z(fRadius)));
}

void SinkLine(void *pCtx, double x0, double y0, double x1, double y1, unsigned int nRgb, double fWidth,
              double fAlpha) {
  const Sink *p = static_cast<const Sink *>(pCtx);
  const double fW = p->Z(fWidth);
  UiLine(static_cast<float>(p->X(x0)), static_cast<float>(p->Y(y0)), static_cast<float>(p->X(x1)),
         static_cast<float>(p->Y(y1)), UiRgb(nRgb, static_cast<float>(fAlpha)),
         static_cast<float>(fW < 1 ? 1 : fW));
}

void SinkGlyph(void *pCtx, int nKind, double x, double y, double z, unsigned int nRgb, double fAlpha) {
  const signed char *p = PortReadoutGlyph(nKind);
  if (p == NULL)
    return;
  const double k = z / 16.0;
  for (; *p != 0;) {
    const int nPoints = *p++;
    double px = 0, py = 0;
    for (int i = 0; i < nPoints; ++i, p += 2) {
      const double qx = x + p[0] * k, qy = y + p[1] * k;
      if (i > 0)
        SinkLine(pCtx, px, py, qx, qy, nRgb, k * 1.6, fAlpha);
      px = qx, py = qy;
    }
  }
}

void SinkText(void *pCtx, const char *pszUtf8, double x, double y, double fSize, unsigned int nRgb,
              double fAlpha, int nAlign, int nFace) {
  const Sink *p = static_cast<const Sink *>(pCtx);
  const UiFace face = nFace ? UI_FACE_BOLD : UI_FACE_TEXT;
  const float fPx = static_cast<float>(p->Z(fSize));
  const float fWide = UiTextWidth(face, fPx, 0, pszUtf8);
  float fLeft = static_cast<float>(p->X(x));
  if (nAlign == 1)
    fLeft -= fWide / 2;
  else if (nAlign == 2)
    fLeft -= fWide;
  // y is the text's middle; the kit draws from a baseline, which its own metrics give, so that
  // this surface and a slot's agree through a font override
  float fUp = 0, fDown = 0, fGap = 0;
  UiFontMetrics(face, fPx, &fUp, &fDown, &fGap);
  UiText(fLeft, static_cast<float>(p->Y(y)) + (fUp + fDown) / 2, face, fPx, 0,
         UiRgb(nRgb, static_cast<float>(fAlpha)), pszUtf8);
}

double SinkTextWidth(void *pCtx, const char *pszUtf8, double fSize, int nFace) {
  const Sink *p = static_cast<const Sink *>(pCtx);
  const float fPx = static_cast<float>(p->Z(fSize));
  const double fWide = UiTextWidth(nFace ? UI_FACE_BOLD : UI_FACE_TEXT, fPx, 0, pszUtf8);
  return p->Z(1) > 0 ? fWide / p->Z(1) : fWide;  // back into readout pixels
}

// The one gate both consoles take, and not over a design, which draws the readout in its own
// selection slot's layer instead.
bool Shown(void) { return PortReadoutDraws() && !PortConsoleHudOn(); }

bool Draw(int nWidth, int nHeight, float fX, float fY, float fW, float fH) {
  (void)nWidth;
  (void)nHeight;
  (void)fH;
  PortReadoutPlace place;
  if (!Shown() || !PortHudReadoutPlace(&place) || !place.bOn || g_portView.nWidth <= 0)
    return false;
  Sink sink;
  sink.fX = fX;
  sink.fY = fY;
  sink.fK = fW / static_cast<double>(g_portView.nWidth);
  sink.fPanelX = place.x;
  sink.fPanelY = place.y;
  sink.fScale = place.fScale;
  PortReadoutSurface surface;
  surface.pCtx = &sink;
  surface.bFull = true;
  surface.fPx = sink.fScale * sink.fK;
  surface.pfnFill = SinkFill;
  surface.pfnLine = SinkLine;
  surface.pfnGlyph = SinkGlyph;
  surface.pfnText = SinkText;
  surface.pfnTextWidth = SinkTextWidth;
  Paint(&surface);
  return true;
}

const PortOverlayLayer kReadoutLayer = {"readout", PORT_OVERLAY_READOUT, 0, Shown, NULL, Draw,
                                        NULL,      NULL,                 NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kReadoutLayer);

// The drawing registered for whatever surface is made for it: a design's selection slot makes
// one over its own layer (port_hudinput.cpp), and the layer above makes one for the classic
// console. Registered here rather than asked for, so the two sides land in either order.
bool Register(void) {
  PortHudReadoutSurfaceSet(Paint);
  return true;
}
const bool s_bRegistered = Register();

}  // namespace
