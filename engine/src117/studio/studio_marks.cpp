// studio_marks.cpp -- the Marks page of the Looks workspace: types.json's "marks", the looks a
// mod's scripts put on the ground marks the engine draws (docs/mod-data.md, "The looks a mark
// wears").
//
// A look is blocks of light stacked on a mark's edge: the field standing up from it, the cells
// drifting in the field, the foot glowing on the ground, the tear a crossing leaves, the ripple it
// sends along the edge, the light under a unit standing outside, and the mark on the minimap.
// Beside them a look asks for some of the mark's own values, which hx.area wins over.
//
// Every field here carries the range the loader accepts, and the editor holds a value inside it,
// because a value outside stops the whole mod as it loads rather than being passed over. The
// loader's own reading is mirrored in Blocks below, one table for the editor and for the look the
// preview builds, so the two cannot drift apart within this file; what the loader refuses it
// refuses by the line of types.json the value is on (PortJsonLineAt), which Problems shows.
//
// The preview puts a real mark in the studio's canvas game, which runs in this process, so the
// look is seen as a game draws it: PortMapMarkSet through a channel command, which runs on the
// game's thread. The mark is set again while the page is open, so an edit shows at once and a game
// started again gets it back.
//
//   studio_marks {}                                      the looks declared, each with its blocks
//   studio_mark {name}                                   one look, as the file has it
//   studio_mark_set {name, block?, field?, value?, remove?}  a look made, a field set, a block or the look taken away
//   studio_mark_show {name?, off?, shape?, radius?, fade?, crossing?, minimap?, at?}  the mark in the canvas game
#define NOMINMAX
#include <algorithm>
#include <math.h>
#include <string>
#include <vector>
#include "../design/design_engine.h"         // DesignCall
#include "../design/design_hooks.h"          // DesignInGame
#include "../port/render/port_mapmark.h"     // PortMapMark, PortMarkLook, PortMapMarkSet
#include "../port/tools/port_control.h"      // PortControlCommand
#include "studio_draw.h"
#include "studio_marks.h"
#include "studio_media.h"                    // StudioMediaPara, StudioPalette

namespace {

using namespace studio_draw;

// ---- the loader's own reading, as one table --------------------------------------------------
// Each field of each block: the key types.json writes, the words the page shows, what kind of
// value it takes, and for a number the range the loader holds it to with the value it has when a
// block leaves it out. The ranges are port_moddata.cpp's MarkField and the rest, and the defaults
// port_mapmark.h's own, which is what a block that leaves a field out draws with.

enum FieldKind { FK_NUM, FK_INT, FK_BOOL, FK_RGB, FK_NAME, FK_ROWS, FK_SHAPE };

struct Field {
  const char *pszKey;
  const char *pszLabel;
  int nKind;
  double dMin, dMax, dBase;
  const char *pszNote;
};

struct Block {
  const char *pszKey;    // "" for the look's own values, which are no block and never taken away
  const char *pszLabel;
  const char *pszWhat;   // what it draws, in a line
  const Field *pFields;
  int nFields;
  bool bStands;          // it stands up from the edge, so its layer can only be "over"
};

// The mark's own values a look asks for, which hx.area wins over, and the colour its blocks
// answer a unit on its way out in.
const Field kAsks[] = {
    {"warn", "Warning colour", FK_RGB, 0, 0, 0, "what the blocks answer a unit on its way out in"},
    {"fill", "Fill", FK_NUM, 0, 1, 0, "the inside's opacity"},
    {"edge", "Edge", FK_NUM, 0, 1, 1, "the edge's opacity"},
    {"edge_width", "Edge width", FK_NUM, 0, 1024, 2, "map pixels, so it thins as the player zooms out"},
    {"dash", "Dash", FK_NUM, 0, 4096, 0, "a dash and its gap; 0 draws the edge whole"},
    {"soft", "Soft", FK_NUM, 0, 4, 0, "a glow under the edge, this many of its widths wide"},
    {"pulse", "Pulse", FK_NUM, 0, 60, 0, "seconds a beat; 0 is steady"},
    {"pulse_depth", "Pulse depth", FK_NUM, 0, 1, 0.30, "how deep the swell goes"},
    {"curve", "Curve", FK_NAME, 0, 0, 0, "how the fade's light falls off: smooth, even or sharp"},
    {"crossing", "Crossing", FK_NAME, 0, 0, 0, "what a unit crossing the edge stirs up"},
};

const Field kField[] = {
    {"height", "Height", FK_NUM, 0, 4096, 26, "how far it stands up, in map pixels"},
    {"bands", "Bands", FK_INT, 1, 32, 6, "the bands it is drawn in"},
    {"alpha", "Alpha", FK_NUM, 0, 1, 0.42, "the band at the foot's opacity"},
    {"fade", "Fade", FK_NUM, 0, 1, 1, "how much of that the band at the top has lost"},
    {"add", "Add", FK_BOOL, 0, 0, 1, "its light adds to the ground rather than covering it"},
    {"lean", "Lean", FK_NUM, 0, 4, 0.35, "how far the top leans out along the edge's normal"},
    {"edge_on", "Edge-on", FK_NUM, 0, 4, 1.20, "a face turned edge-on, as a part of a face-on one"},
};

const Field kCells[] = {
    {"shape", "Shape", FK_SHAPE, 0, 0, 0, "hex, the only cell this build draws"},
    {"spacing", "Spacing", FK_NUM, 1, 4096, 13, "the edge between one set of cells and the next"},
    {"rows", "Rows", FK_ROWS, 0, 4, 0, "each row's height, as a part of the field's; 1 to 8"},
    {"stagger", "Stagger", FK_NUM, 0, 1, 0.5, "how far along the spacing each row after the first moves"},
    {"radius", "Radius", FK_NUM, 0, 256, 4.2, "the outline's radius"},
    {"squash", "Squash", FK_NUM, 0, 4, 0.8, "how flat it is drawn"},
    {"width", "Width", FK_NUM, 0, 64, 1, "the line's width"},
    {"alpha", "Alpha", FK_NUM, 0, 1, 0.7, "its opacity"},
    {"least", "Least", FK_NUM, 0, 1, 0.12, "the light a stretch of edge needs before its cells are drawn"},
    {"base", "Flicker's middle", FK_NUM, 0, 1, 0.55, "the middle the flicker swings about"},
    {"swing", "Flicker's swing", FK_NUM, 0, 1, 0.45, "how far it swings"},
    {"rate", "Rate", FK_NUM, 0, 64, 3.1, "its beat by the second"},
    {"along", "Along", FK_NUM, 0, 64, 0.37, "its beat by the edge gone along"},
    {"row", "Row", FK_NUM, 0, 64, 1.7, "its beat by the row"},
    {"warm", "Warm", FK_NUM, 0, 1, 0.2, "the warmth past which a cell takes the warning colour"},
};

const Field kFoot[] = {
    {"glow", "Glow", FK_NUM, 1, 64, 4, "how many of the edge's widths the glow reaches"},
    {"alpha", "Alpha", FK_NUM, 0, 1, 0.22, "its opacity, as a part of the edge's"},
};

const Field kTear[] = {
    {"life", "Life", FK_NUM, 0, 60, 0.9, "the seconds it lasts"},
    {"width", "Width", FK_NUM, 0, 4096, 10, "how wide it begins"},
    {"grow", "Grow", FK_NUM, 0, 4096, 30, "how much wider it is at the end"},
    {"height", "Height", FK_NUM, 0, 16, 1.6, "how tall it is, in the field's heights"},
    {"alpha", "Alpha", FK_NUM, 0, 1, 0.9, "its opacity at the foot, falling to nothing at the top"},
    {"lean", "Lean", FK_NUM, 0, 4, 0.5, "how far it leans the way the unit was going"},
};

const Field kRipple[] = {
    {"speed", "Speed", FK_NUM, 0, 16384, 384, "how fast the wave travels along the edge"},
    {"width", "Width", FK_NUM, 1, 4096, 46, "how wide it is"},
    {"life", "Life", FK_NUM, 0.01, 60, 1.6, "the seconds it lasts"},
    {"flash", "Flash", FK_NUM, 1, 4096, 50, "the flash at the place itself"},
    {"flash_life", "Flash's life", FK_NUM, 0.01, 60, 0.5, "its own seconds, which die sooner"},
    {"side", "Side", FK_NUM, 0, 1, 0.5, "how far the flash leans the way the unit went"},
};

const Field kOutside[] = {
    {"style", "Style", FK_NAME, 0, 0, 0, "tint, ring, tether or none; only tint draws for now"},
    {"band", "Band", FK_NUM, 0, 1 << 20, 0, "how far outside the edge the warning reaches"},
    {"radius", "Radius", FK_NUM, 1, 4096, 26, "how far the light reaches under the unit"},
    {"alpha", "Alpha", FK_NUM, 0, 1, 0.55, "its opacity at the middle"},
    {"base", "Beat's middle", FK_NUM, 0, 1, 0.65, "the middle the beat swings about"},
    {"swing", "Beat's swing", FK_NUM, 0, 1, 0.35, "how far it swings"},
    {"beat", "Beat", FK_NUM, 0, 64, 4, "its rate within the band"},
    {"past", "Past", FK_NUM, 0, 64, 9, "and past it"},
    {"color", "Warning colour", FK_RGB, 0, 0, 0, "within the band"},
    {"far_color", "Danger colour", FK_RGB, 0, 0, 0, "past it"},
    {"layer", "Layer", FK_NAME, 0, 0, 0, "ground or over; the light lies flat, so it takes either"},
};

const Field kMini[] = {
    {"on", "On", FK_BOOL, 0, 0, 1, "drawn on the minimap's own picture"},
    {"fill", "Fill", FK_NUM, 0, 1, 0, "the inside's opacity, as a part of the pixels drawn"},
    {"edge", "Edge", FK_NUM, 0, 1, 1, "the edge's"},
    {"width", "Width", FK_NUM, 0, 64, 1, "the edge's width, in minimap pixels"},
    {"dash", "Dash", FK_NUM, 0, 4096, 0, "its dash, in minimap pixels; 0 draws it whole"},
    {"player", "Player", FK_INT, 0, 31, -1, "a player whose colour it takes"},
    {"color", "Colour", FK_RGB, 0, 0, 0, "or one of its own; the mark's own without either"},
};

#define NBLOCK(a) (sizeof(a) / sizeof((a)[0]))
const Block kBlocks[] = {
    {"", "The mark's own values", "what the look asks of the mark, which a script's own values win over",
     kAsks, static_cast<int>(NBLOCK(kAsks)), false},
    {"field", "Field", "light standing up the screen from the edge", kField, static_cast<int>(NBLOCK(kField)), true},
    {"cells", "Cells", "outlines drifting in the field along the edge", kCells, static_cast<int>(NBLOCK(kCells)), true},
    {"foot", "Foot", "the mark's own edge with a glow spilling onto the ground", kFoot,
     static_cast<int>(NBLOCK(kFoot)), false},
    {"tear", "Tear", "a column of light where a unit came through, which wants a crossing", kTear,
     static_cast<int>(NBLOCK(kTear)), true},
    {"ripple", "Ripple", "the wave a crossing sends along the edge, which the other blocks take", kRipple,
     static_cast<int>(NBLOCK(kRipple)), false},
    {"outside", "Outside", "light under one of the player's own units standing outside the mark", kOutside,
     static_cast<int>(NBLOCK(kOutside)), false},
    {"minimap", "Minimap", "the mark on the minimap's own picture, which neither fades nor beats", kMini,
     static_cast<int>(NBLOCK(kMini)), false},
};
const int kBlockCount = static_cast<int>(NBLOCK(kBlocks));

// The names a FK_NAME field takes, by its key.
const char *const kCurves[] = {"smooth", "even", "sharp"};
const char *const kCrossings[] = {"ripple", "flash", "stamp", "arc", "none"};
const char *const kStyles[] = {"tint", "ring", "tether", "none"};
const char *const kLayers[] = {"ground", "over"};

const char *const *NamesOf(const char *pszKey, int *pnCount) {
  if (strcmp(pszKey, "curve") == 0) {
    *pnCount = 3;
    return kCurves;
  }
  if (strcmp(pszKey, "crossing") == 0) {
    *pnCount = 5;
    return kCrossings;
  }
  if (strcmp(pszKey, "style") == 0) {
    *pnCount = 4;
    return kStyles;
  }
  *pnCount = 2;
  return kLayers;
}

// ---- the file --------------------------------------------------------------------------------

const PortJson &Marks(void) {
  return StudioDoc("types")["marks"];
}

const PortJson &Look(const std::string &name) {
  return Marks()[name.c_str()];
}

PortJson Str(const std::string &s) {
  PortJson j;
  j.kind = PortJson::STRING;
  j.s = s;
  return j;
}

PortJson Num(double d) {
  PortJson j;
  j.kind = PortJson::NUMBER;
  j.d = d;
  return j;
}

PortJson Bool(bool b) {
  PortJson j;
  j.kind = PortJson::BOOL;
  j.b = b;
  return j;
}

// A member of a look set or taken away, through the types document, so every change is one edit
// the undo takes back. Edits with the same merge key one after the other are one group, so a
// number typed is one step and not a step a letter. inside is the pieces past the look: the
// block, then the field, or a row's place in the cells' rows.
bool PutIn(const std::string &name, const std::vector<std::string> &inside, const PortJson &value,
           const std::string &label, const std::string &sender, std::string *pWhy) {
  std::vector<std::string> path;
  path.push_back("marks");
  path.push_back(name);
  std::string merge = "marks/" + name;
  for (const std::string &s : inside) {
    path.push_back(s);
    merge += "/" + s;
  }
  StudioGroupBegin(sender, label, merge);
  const bool bOk = StudioPut("types", path, value, sender, label, pWhy);
  StudioGroupEnd();
  return bOk;
}

// The block, then the field: the usual two pieces, with the block left out for the look's own.
bool Put(const std::string &name, const char *pszBlock, const char *pszField, const PortJson &value,
         const std::string &label, const std::string &sender, std::string *pWhy) {
  std::vector<std::string> inside;
  if (pszBlock != nullptr && pszBlock[0] != '\0')
    inside.push_back(pszBlock);
  if (pszField != nullptr)
    inside.push_back(pszField);
  return PutIn(name, inside, value, label, sender, pWhy);
}

// A value as the look has it, or NONE: the look's own fields sit on the look, a block's inside it.
const PortJson &Value(const std::string &name, const Block &b, const Field &f) {
  const PortJson &look = Look(name);
  const PortJson &in = b.pszKey[0] == '\0' ? look : look[b.pszKey];
  const PortJson &v = in[f.pszKey];
  // the two spellings of a colour, as the loader takes both
  if (v.kind == PortJson::NONE && f.nKind == FK_RGB && strcmp(f.pszKey, "color") == 0)
    return in["colour"];
  if (v.kind == PortJson::NONE && f.nKind == FK_RGB && strcmp(f.pszKey, "far_color") == 0)
    return in["far_colour"];
  return v;
}

bool HasBlock(const std::string &name, const Block &b) {
  return b.pszKey[0] == '\0' || Look(name)[b.pszKey].kind == PortJson::OBJECT;
}

int BlocksOf(const std::string &name) {
  int n = 0;
  for (int i = 1; i < kBlockCount; ++i)
    if (HasBlock(name, kBlocks[i]))
      ++n;
  return n;
}

unsigned RgbOf(const PortJson &v, unsigned nBase) {
  unsigned n = nBase;
  if (v.kind == PortJson::STRING)
    PortMapMarkRgbOf(v.s.c_str(), &n);
  return n;
}

std::string RgbText(unsigned n) {
  char sz[10];
  snprintf(sz, sizeof sz, "#%06x", n & 0xffffffu);
  return sz;
}

// ---- the look built, as the loader would build it ---------------------------------------------
// The editor holds every value inside its range, so this is the look a game would draw; what a
// hand-edited file gets wrong the Problems list says, by the line.

void FillAsks(const std::string &name, PortMarkLook *p) {
  const PortJson &look = Look(name);
  p->nWarn = RgbOf(look["warn"], p->nWarn);
  p->asks.fFill = look["fill"].Num(p->asks.fFill);
  p->asks.fEdge = look["edge"].Num(p->asks.fEdge);
  p->asks.fWidth = look["edge_width"].Num(p->asks.fWidth);
  p->asks.fDash = look["dash"].Num(p->asks.fDash);
  p->asks.fSoft = look["soft"].Num(p->asks.fSoft);
  p->asks.fPulse = look["pulse"].Num(p->asks.fPulse);
  p->asks.fDepth = look["pulse_depth"].Num(p->asks.fDepth);
  const int nCurve = PortMapMarkCurveOf(look["curve"].Str().c_str());
  if (nCurve >= 0)
    p->asks.nCurve = nCurve;
  const int nCross = PortMapMarkCrossingOf(look["crossing"].Str().c_str());
  if (nCross >= 0)
    p->asks.nCross = nCross;
}

PortMarkLook LookOf(const std::string &name) {
  PortMarkLook out;
  const PortJson &look = Look(name);
  FillAsks(name, &out);
  const PortJson &f = look["field"];
  if (f.kind == PortJson::OBJECT) {
    out.field.bOn = true;
    out.field.fHigh = f["height"].Num(out.field.fHigh);
    out.field.nBands = static_cast<int>(f["bands"].Num(out.field.nBands));
    out.field.fAlpha = f["alpha"].Num(out.field.fAlpha);
    out.field.fFade = f["fade"].Num(out.field.fFade);
    if (f["add"].kind == PortJson::BOOL)
      out.field.bAdd = f["add"].b;
    out.field.fLean = f["lean"].Num(out.field.fLean);
    out.field.fEdgeOn = f["edge_on"].Num(out.field.fEdgeOn);
  }
  const PortJson &c = look["cells"];
  if (c.kind == PortJson::OBJECT) {
    out.cells.bOn = true;
    out.cells.fStep = c["spacing"].Num(out.cells.fStep);
    out.cells.fStagger = c["stagger"].Num(out.cells.fStagger);
    out.cells.fRadius = c["radius"].Num(out.cells.fRadius);
    out.cells.fSquash = c["squash"].Num(out.cells.fSquash);
    out.cells.fWidth = c["width"].Num(out.cells.fWidth);
    out.cells.fAlpha = c["alpha"].Num(out.cells.fAlpha);
    out.cells.fLeast = c["least"].Num(out.cells.fLeast);
    out.cells.fBase = c["base"].Num(out.cells.fBase);
    out.cells.fSwing = c["swing"].Num(out.cells.fSwing);
    out.cells.fRate = c["rate"].Num(out.cells.fRate);
    out.cells.fAlong = c["along"].Num(out.cells.fAlong);
    out.cells.fRow = c["row"].Num(out.cells.fRow);
    out.cells.fWarm = c["warm"].Num(out.cells.fWarm);
    const PortJson &rows = c["rows"];
    if (rows.kind == PortJson::ARRAY && !rows.items.empty()) {
      out.cells.rows.clear();
      for (const PortJson &one : rows.items)
        out.cells.rows.push_back(one.Num(0));
    }
  }
  const PortJson &ft = look["foot"];
  if (ft.kind == PortJson::OBJECT) {
    out.foot.bOn = true;
    out.foot.fGlow = ft["glow"].Num(out.foot.fGlow);
    out.foot.fAlpha = ft["alpha"].Num(out.foot.fAlpha);
  }
  const PortJson &t = look["tear"];
  if (t.kind == PortJson::OBJECT) {
    out.tear.bOn = true;
    out.tear.fLife = t["life"].Num(out.tear.fLife);
    out.tear.fWide = t["width"].Num(out.tear.fWide);
    out.tear.fGrow = t["grow"].Num(out.tear.fGrow);
    out.tear.fHigh = t["height"].Num(out.tear.fHigh);
    out.tear.fAlpha = t["alpha"].Num(out.tear.fAlpha);
    out.tear.fLean = t["lean"].Num(out.tear.fLean);
  }
  const PortJson &rp = look["ripple"];
  if (rp.kind == PortJson::OBJECT) {
    out.ripple.fSpeed = rp["speed"].Num(out.ripple.fSpeed);
    out.ripple.fWide = rp["width"].Num(out.ripple.fWide);
    out.ripple.fLife = rp["life"].Num(out.ripple.fLife);
    out.ripple.fFlash = rp["flash"].Num(out.ripple.fFlash);
    out.ripple.fFlashLife = rp["flash_life"].Num(out.ripple.fFlashLife);
    out.ripple.fSide = rp["side"].Num(out.ripple.fSide);
  }
  const PortJson &o = look["outside"];
  if (o.kind == PortJson::OBJECT) {
    const int nStyle = PortMapMarkOutsideOf(o["style"].Str().c_str());
    if (nStyle >= 0)
      out.outside.nStyle = nStyle;
    const int nLayer = PortMapMarkLayerOf(o["layer"].Str().c_str());
    if (nLayer >= 0)
      out.outside.nLayer = nLayer;
    out.outside.fBand = o["band"].Num(out.outside.fBand);
    out.outside.fRadius = o["radius"].Num(out.outside.fRadius);
    out.outside.fAlpha = o["alpha"].Num(out.outside.fAlpha);
    out.outside.fBase = o["base"].Num(out.outside.fBase);
    out.outside.fSwing = o["swing"].Num(out.outside.fSwing);
    out.outside.fBeat = o["beat"].Num(out.outside.fBeat);
    out.outside.fPast = o["past"].Num(out.outside.fPast);
    out.outside.nNear = RgbOf(o["color"].kind != PortJson::NONE ? o["color"] : o["colour"], out.outside.nNear);
    out.outside.nFar = RgbOf(o["far_color"].kind != PortJson::NONE ? o["far_color"] : o["far_colour"], out.outside.nFar);
  }
  const PortJson &mn = look["minimap"];
  if (mn.kind == PortJson::OBJECT) {
    out.mini.bOn = mn["on"].kind != PortJson::BOOL || mn["on"].b;
    out.mini.fFill = mn["fill"].Num(out.mini.fFill);
    out.mini.fEdge = mn["edge"].Num(out.mini.fEdge);
    out.mini.fWidth = mn["width"].Num(out.mini.fWidth);
    out.mini.fDash = mn["dash"].Num(out.mini.fDash);
    out.mini.nPlayer = static_cast<int>(mn["player"].Num(out.mini.nPlayer));
    const PortJson &col = mn["color"].kind != PortJson::NONE ? mn["color"] : mn["colour"];
    out.mini.bRgb = col.kind != PortJson::NONE;
    out.mini.nRgb = RgbOf(col, out.mini.nRgb);
  }
  return out;
}

// What the loader would refuse: a block or a field there is none of, a number outside its range, a
// value of the wrong kind. The editor never writes one, so every row here comes of a file edited by
// hand or by another tool. Each row names the path rather than the line of types.json, which is
// what the loader's own refusal names (PortJsonLineAt) and the model, holding a read tree, no
// longer has the text for.
struct Problem {
  std::string what;
};

const Field *FieldOf(const Block &b, const std::string &key) {
  for (int i = 0; i < b.nFields; ++i)
    if (key == b.pFields[i].pszKey)
      return &b.pFields[i];
  // the loader takes both spellings of a colour
  if (key == "colour")
    return FieldOf(b, "color");
  if (key == "far_colour")
    return FieldOf(b, "far_color");
  return nullptr;
}

void CheckOne(const PortJson &in, const Block &b, const std::string &at, std::vector<Problem> *pOut) {
  for (const PortJsonMember &m : in.members) {
    if (m.first == "comment" || m.first == "about")
      continue;
    // the look's own members hold the blocks as well as its asks
    if (b.pszKey[0] == '\0') {
      bool bBlock = false;
      for (int i = 1; i < kBlockCount; ++i)
        bBlock = bBlock || m.first == kBlocks[i].pszKey;
      if (bBlock)
        continue;
    }
    const Field *pf = FieldOf(b, m.first);
    if (pf == nullptr) {
      pOut->push_back({at + " has no field " + m.first});
      continue;
    }
    if (pf->nKind == FK_NUM || pf->nKind == FK_INT) {
      if (m.second.kind != PortJson::NUMBER || m.second.d < pf->dMin || m.second.d > pf->dMax) {
        char sz[96];
        snprintf(sz, sizeof sz, " takes a number from %g to %g", pf->dMin, pf->dMax);
        pOut->push_back({at + "." + m.first + sz});
      }
    } else if (pf->nKind == FK_BOOL) {
      if (m.second.kind != PortJson::BOOL)
        pOut->push_back({at + "." + m.first + " is true or false"});
    } else if (pf->nKind == FK_RGB) {
      unsigned n = 0;
      if (m.second.kind != PortJson::STRING || !PortMapMarkRgbOf(m.second.s.c_str(), &n))
        pOut->push_back({at + "." + m.first + " is a colour, \"#rrggbb\""});
    } else if (pf->nKind == FK_NAME) {
      int nCount = 0;
      const char *const *pp = NamesOf(pf->pszKey, &nCount);
      bool bOne = false;
      for (int i = 0; i < nCount; ++i)
        bOne = bOne || m.second.Str() == pp[i];
      if (!bOne)
        pOut->push_back({at + "." + m.first + " is one of the names it takes"});
    } else if (pf->nKind == FK_SHAPE) {
      if (m.second.Str() != "hex")
        pOut->push_back({at + ".shape is \"hex\", the only cell this build draws"});
    } else if (pf->nKind == FK_ROWS) {
      if (m.second.kind != PortJson::ARRAY || m.second.items.empty() || m.second.items.size() > 8) {
        pOut->push_back({at + ".rows is a list of 1 to 8 heights"});
      } else {
        for (const PortJson &one : m.second.items)
          if (one.kind != PortJson::NUMBER || one.d < 0.0 || one.d > 4.0)
            pOut->push_back({at + ".rows takes a number from 0 to 4"});
      }
    }
  }
}

std::vector<Problem> Problems(const std::string &name) {
  std::vector<Problem> out;
  const PortJson &look = Look(name);
  if (look.kind != PortJson::OBJECT) {
    out.push_back({"marks." + name + " is an object"});
    return out;
  }
  CheckOne(look, kBlocks[0], "marks." + name, &out);
  for (int i = 1; i < kBlockCount; ++i) {
    const PortJson &in = look[kBlocks[i].pszKey];
    if (in.kind == PortJson::NONE)
      continue;
    const std::string at = "marks." + name + "." + kBlocks[i].pszKey;
    if (in.kind != PortJson::OBJECT) {
      out.push_back({at + " is an object"});
      continue;
    }
    CheckOne(in, kBlocks[i], at, &out);
    // a block that stands up from the edge cannot be painted into the ground
    if (kBlocks[i].bStands && in["layer"].Str() == "ground")
      out.push_back({at + " stands up from the edge, so its layer is \"over\""});
  }
  return out;
}

// ---- the mark in the canvas game -------------------------------------------------------------
// The preview's own values, which are the mark's and not the look's: a script says these itself
// and the look only asks for some of them.

const char *const kShapes[] = {"ring", "circle", "rect"};
struct Preview {
  bool bOn = false;
  std::string look;         // the look it wears, "" for none
  int nShape = 0;           // kShapes
  double fRadius = 192.0;
  bool bFade = true;
  double fNear = 320.0;
  bool bCrossing = true;    // the mark's crossing, so a tear and a ripple can be seen
  bool bMinimap = true;
  int x = 0, y = 0;         // map pixels; 0 takes the canvas game's own middle
  double dSet = 0;          // when it was last set, so the page sets it again
};
Preview g_pre;
// where a mark goes when the page has no place of its own: the canvas game's own field, which the
// Test workspace spawns into (studio_testloop.cpp)
const int kAtX = 1070, kAtY = 1024;
const char *const kMarkId = "studio_mark_preview";

// The mark as the preview wants it, with the look the editor holds. Built on the game's thread.
PortMapMark MarkOf(void) {
  PortMapMark m;
  m.id = kMarkId;
  m.nShape = PortMapMarkShapeOf(kShapes[g_pre.nShape < 0 || g_pre.nShape > 2 ? 0 : g_pre.nShape]);
  m.x = g_pre.x != 0 ? g_pre.x : kAtX;
  m.y = g_pre.y != 0 ? g_pre.y : kAtY;
  m.r = g_pre.fRadius;
  m.w = g_pre.fRadius * 2.0;
  m.h = g_pre.fRadius * 2.0;
  if (m.nShape == MAP_MARK_RECT) {
    m.x -= g_pre.fRadius;
    m.y -= g_pre.fRadius;
  }
  m.nPlayer = 0;  // the canvas game's own player, so the fade and the light outside answer its units
  m.bFade = g_pre.bFade;
  m.fNear = g_pre.fNear;
  m.nLayer = MAP_MARK_GROUND;
  if (!g_pre.look.empty()) {
    m.look = LookOf(g_pre.look);
    // the mark takes what the look asks for, as hx.area does when nothing of its own is said
    m.fFill = m.look.asks.fFill;
    m.fEdge = m.look.asks.fEdge;
    m.fWidth = m.look.asks.fWidth;
    m.fDash = m.look.asks.fDash;
    m.fSoft = m.look.asks.fSoft;
    m.fPulse = m.look.asks.fPulse;
    m.fDepth = m.look.asks.fDepth;
    m.nCurve = m.look.asks.nCurve;
    m.nCross = m.look.asks.nCross;
    if (m.look.Stands())
      m.nLayer = MAP_MARK_OVER;
  }
  if (g_pre.bCrossing && m.nCross == MAP_MARK_CROSS_NONE)
    m.nCross = MAP_MARK_CROSS_RIPPLE;
  if (!g_pre.bMinimap)
    m.look.mini.bOn = false;
  return m;
}

// ---- the page ---------------------------------------------------------------------------------

int g_nBlockOpen = 1;  // the block whose fields are written out, 0 the look's own values

float LabelW(void) {
  return P(170.0f);
}

void Row(const char *pszLabel) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y + P(6.0f), C(TK_LABEL), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(at.x + LabelW(), at.y));
}

// The mark set again in the canvas game, which is how an edit shows at once.
void Send(void) {
  if (!g_pre.bOn || !DesignInGame())
    return;
  DesignCall("studio_mark_show", "\"name\":" + Q(g_pre.look));
  g_pre.dSet = ImGui::GetTime();
}

// One field's editor, with its range beside it. A number is held inside the range, since a value
// outside stops the mod as it loads.
void FieldRow(const std::string &name, const Block &b, const Field &f, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson &v = Value(name, b, f);
  const bool bSet = v.kind != PortJson::NONE;
  ImGui::PushID(f.pszKey);
  Row(f.pszLabel);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  bool bChanged = false;
  if (f.nKind == FK_NUM || f.nKind == FK_INT) {
    double dNow = v.Num(f.dBase);
    if (StudioNumber("##v", &dNow, P(100.0f), P(26.0f), true)) {
      dNow = dNow < f.dMin ? f.dMin : dNow > f.dMax ? f.dMax : dNow;
      if (f.nKind == FK_INT)
        dNow = floor(dNow + 0.5);
      std::string why;
      Put(name, b.pszKey, f.pszKey, Num(dNow), std::string("set the look's ") + f.pszKey, "you", &why);
      bChanged = true;
    }
  } else if (f.nKind == FK_BOOL) {
    bool bNow = bSet ? (v.kind == PortJson::BOOL && v.b) : f.dBase != 0;
    if (StudioCheck("##v", bNow ? "yes" : "no", &bNow)) {
      std::string why;
      Put(name, b.pszKey, f.pszKey, Bool(bNow), std::string("set the look's ") + f.pszKey, "you", &why);
      bChanged = true;
    }
  } else if (f.nKind == FK_RGB) {
    char sz[16];
    snprintf(sz, sizeof sz, "%s", bSet ? v.Str().c_str() : "");
    if (StudioInput("##v", sz, sizeof sz, P(100.0f), P(26.0f), "#rrggbb")) {
      unsigned n = 0;
      std::string why;
      if (sz[0] == '\0')
        Put(name, b.pszKey, f.pszKey, PortJson(), std::string("cleared the look's ") + f.pszKey, "you", &why);
      else if (PortMapMarkRgbOf(sz, &n))
        Put(name, b.pszKey, f.pszKey, Str(RgbText(n)), std::string("set the look's ") + f.pszKey, "you", &why);
      else
        StudioSay(std::string(f.pszKey) + " is a colour, \"#rrggbb\"");
      bChanged = true;
    }
    // the colour as a swatch beside its text
    if (bSet) {
      const unsigned n = RgbOf(v, 0xffffff);
      const ImVec2 s(at.x + P(106.0f), at.y + P(5.0f));
      d->AddRectFilled(s, ImVec2(s.x + P(16.0f), s.y + P(16.0f)),
                       IM_COL32((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF, 255), P(2.0f));
      d->AddRect(s, ImVec2(s.x + P(16.0f), s.y + P(16.0f)), C(TK_LINE), P(2.0f));
    }
  } else if (f.nKind == FK_NAME || f.nKind == FK_SHAPE) {
    int nCount = 1;
    const char *const kHex[] = {"hex"};
    const char *const *pp = f.nKind == FK_SHAPE ? kHex : NamesOf(f.pszKey, &nCount);
    if (f.nKind == FK_SHAPE)
      nCount = 1;
    const std::string now = v.Str();
    if (StudioSelectBegin("##v", now.empty() ? "its own" : now.c_str(), P(140.0f))) {
      if (StudioItem("its own", now.empty())) {
        std::string why;
        Put(name, b.pszKey, f.pszKey, PortJson(), std::string("cleared the look's ") + f.pszKey, "you", &why);
        bChanged = true;
      }
      for (int i = 0; i < nCount; ++i)
        if (StudioItem(pp[i], now == pp[i])) {
          std::string why;
          Put(name, b.pszKey, f.pszKey, Str(pp[i]), std::string("set the look's ") + f.pszKey, "you", &why);
          bChanged = true;
        }
      StudioSelectEnd();
    }
  } else if (f.nKind == FK_ROWS) {
    // the cells' rows: 1 to 8 heights, each a part of the field's
    const PortJson &rows = v;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    float x = o.x;
    for (size_t i = 0; i < rows.size() && i < 8; ++i) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetCursorScreenPos(ImVec2(x, o.y));
      double dOne = rows.At(i).Num(0);
      if (StudioNumber("##r", &dOne, P(70.0f), P(26.0f), true)) {
        dOne = dOne < 0 ? 0 : dOne > 4 ? 4 : dOne;
        std::string why;
        PutIn(name, {b.pszKey, "rows", std::to_string(i)}, Num(dOne), "set a row of the look's cells", "you",
              &why);
        bChanged = true;
      }
      ImGui::PopID();
      x += P(76.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, o.y));
    if (rows.size() < 8 && StudioButton("##r+", "", "plus", SB_FLAT, 26.0f)) {
      std::string why;
      PutIn(name, {b.pszKey, "rows", std::to_string(rows.size())}, Num(0.5),
            "added a row to the look's cells", "you", &why);
      bChanged = true;
    }
    if (rows.size() > 1) {
      ImGui::SameLine();
      if (StudioButton("##r-", "", "x", SB_FLAT, 26.0f)) {
        std::string why;
        PutIn(name, {b.pszKey, "rows", std::to_string(rows.size() - 1)}, PortJson(),
              "took a row off the look's cells", "you", &why);
        bChanged = true;
      }
    }
    ImGui::Dummy(ImVec2(P(10.0f), P(26.0f)));
  }
  // the range the loader holds it to, and what the block draws with when it is left out
  const float fRight = at.x + P(180.0f);
  std::string says;
  if (f.nKind == FK_NUM || f.nKind == FK_INT) {
    char sz[80];
    snprintf(sz, sizeof sz, "%g to %g", f.dMin, f.dMax);
    says = sz;
    if (!bSet)
      says += "  ·  " + Fmt(f.dBase, 2) + " without one";
  } else if (!bSet) {
    says = "left out";
  }
  if (!says.empty())
    T(d, STUDIO_FONT_MONO, 9.5f, fRight, at.y + P(8.0f), C(bSet ? TK_INK_OFF : TK_INK3), says);
  if (f.pszNote[0] != '\0')
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, fRight + P(120.0f), at.y + P(7.0f), C(TK_INK3), f.pszNote,
      fW - (fRight + P(120.0f) - at.x) - P(60.0f));
  // a value set may be taken away again, which leaves the block drawing with its own
  if (bSet && f.nKind != FK_ROWS) {
    ImGui::SetCursorScreenPos(ImVec2(at.x + fW - LabelW() - P(40.0f), at.y));
    if (StudioButton("##clr", "", "x", SB_FLAT, 24.0f)) {
      std::string why;
      Put(name, b.pszKey, f.pszKey, PortJson(), std::string("cleared the look's ") + f.pszKey, "you", &why);
      bChanged = true;
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(at.x - LabelW(), at.y + P(30.0f)));
  ImGui::Dummy(ImVec2(fW, P(1.0f)));
  ImGui::PopID();
  if (bChanged)
    Send();
}

// The minimap's own picture of the mark: a palettized bitmap with no alpha that redraws on a
// timer of its own, so nothing of the fade or the pulse reaches it and the shape is drawn in the
// palette entry nearest its colour. Drawn here at the minimap's own scale.
void MiniPreview(const std::string &name, ImVec2 a, float fSide) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortMarkLook look = LookOf(name);
  const ImVec2 b(a.x + fSide, a.y + fSide);
  d->AddRectFilled(a, b, C(TK_BLACK));
  d->AddRect(a, b, C(TK_LINE));
  if (!look.mini.bOn) {
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(8.0f), a.y + fSide / 2.0f - P(6.0f), C(TK_INK3),
      "no minimap block");
    return;
  }
  // a block that names a player takes that player's colour in a game, which is the game's to
  // say and not the look's: the picture here is drawn in the look's own colour and says so
  unsigned nRgb = look.mini.bRgb ? look.mini.nRgb : 0xFFFFFF;
  // the colour drawn as the nearest palette entry, as the minimap's picture has no colour of its own
  const std::vector<unsigned> &pal = StudioPalette();
  if (!pal.empty()) {
    long long nBest = -1;
    unsigned nNear = nRgb;
    for (size_t i = 1; i < pal.size(); ++i) {
      const long long dr = static_cast<long long>((pal[i] >> 16) & 0xFF) - ((nRgb >> 16) & 0xFF);
      const long long dg = static_cast<long long>((pal[i] >> 8) & 0xFF) - ((nRgb >> 8) & 0xFF);
      const long long db = static_cast<long long>(pal[i] & 0xFF) - (nRgb & 0xFF);
      const long long dd = dr * dr + dg * dg + db * db;
      if (nBest < 0 || dd < nBest) {
        nBest = dd;
        nNear = pal[i];
      }
    }
    nRgb = nNear;
  }
  const ImU32 fill = IM_COL32((nRgb >> 16) & 0xFF, (nRgb >> 8) & 0xFF, nRgb & 0xFF,
                              static_cast<int>(look.mini.fFill * 255.0 + 0.5));
  const ImU32 edge = IM_COL32((nRgb >> 16) & 0xFF, (nRgb >> 8) & 0xFF, nRgb & 0xFF,
                              static_cast<int>(look.mini.fEdge * 255.0 + 0.5));
  // the mark is drawn as the preview's shape, at the minimap's own scale: 16 map pixels a pixel
  // on a 64-tile map, which is what hx.minimap gives
  const float k = fSide / 128.0f;  // the box stands for 128 minimap pixels
  const ImVec2 c((a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f);
  const float r = static_cast<float>(g_pre.fRadius / 16.0) * k;
  const float fWidth = std::max(1.0f, static_cast<float>(look.mini.fWidth) * k);
  const bool bRect = g_pre.nShape == 2;
  if (look.mini.fFill > 0.004) {
    if (bRect)
      d->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fill);
    else
      d->AddCircleFilled(c, r, fill);
  }
  if (look.mini.fEdge > 0.004) {
    if (bRect)
      d->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), edge, 0, 0, fWidth);
    else if (look.mini.fDash <= 0)
      d->AddCircle(c, r, edge, 0, fWidth);
    else {
      const int n = std::max(4, std::min(48, static_cast<int>(6.2831853f * r / std::max(1.0f, static_cast<float>(look.mini.fDash) * k * 2.0f))));
      for (int i = 0; i < n * 2; i += 2) {
        const float a0 = 3.1415927f * i / n, a1 = 3.1415927f * (i + 1) / n;
        d->AddLine(ImVec2(c.x + r * cosf(a0), c.y + r * sinf(a0)), ImVec2(c.x + r * cosf(a1), c.y + r * sinf(a1)),
                   edge, fWidth);
      }
    }
  }
  T(d, STUDIO_FONT_TEXT, 9.0f, a.x + P(6.0f), b.y - P(14.0f), C(TK_INK3),
    look.mini.nPlayer >= 0 ? "128 minimap pixels  ·  in a game, player " + std::to_string(look.mini.nPlayer) +
                                 "'s own colour"
                           : std::string("128 minimap pixels"));
}

// The preview's own controls: the mark a script would set, which the look is put on.
void PreviewRows(const std::string &name, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "See it in the game", 0.08f);
  ImGui::Dummy(ImVec2(0, P(22.0f)));
  StudioMediaPara("The look goes on a real mark in the studio's canvas game, so it is drawn as a game draws it. "
                  "Walk a unit of yours near the edge to see the fade light it up, and across it for the tear and "
                  "the ripple; stand one outside a closed mark for the light outside. The shape, the fade and the "
                  "crossing below are the mark's own, which a script says and the look only asks for.",
                  fW, C(TK_INK3), 11.5f);
  ImGui::Dummy(ImVec2(0, P(8.0f)));
  const bool bGame = DesignInGame();
  bool bOn = g_pre.bOn;
  if (StudioCheck("##preon", "Put the mark in the game", &bOn)) {
    g_pre.bOn = bOn;
    g_pre.look = name;
    if (bOn)
      Send();
    else if (bGame)
      DesignCall("studio_mark_show", "\"off\":true");
  }
  ImGui::SameLine();
  if (StudioButton("##preagain", "Place it again", "again", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, g_pre.bOn && bGame)) {
    g_pre.look = name;
    Send();
  }
  ImGui::SameLine();
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(8.0f), C(bGame ? TK_INK_OFF : TK_WARN),
    bGame ? "in the canvas game, at the field the Test workspace spawns into"
          : "no game is running: Test starts one");
  ImGui::Dummy(ImVec2(0, P(30.0f)));

  Row("Shape");
  const int nShape = StudioSeg("preshape", kShapes, 3, g_pre.nShape);
  if (nShape != g_pre.nShape) {
    g_pre.nShape = nShape;
    Send();
  }
  ImGui::Dummy(ImVec2(0, P(6.0f)));
  Row("Radius");
  double dR = g_pre.fRadius;
  if (StudioNumber("##prer", &dR, P(100.0f), P(26.0f), true)) {
    g_pre.fRadius = dR < 16 ? 16 : dR > 4096 ? 4096 : dR;
    Send();
  }
  ImGui::SameLine();
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(8.0f), C(TK_INK3), "map pixels, 32 to the tile");
  ImGui::Dummy(ImVec2(0, P(30.0f)));
  Row("Fade");
  bool bFade = g_pre.bFade;
  if (StudioCheck("##prefade", "a unit near it lights it up", &bFade)) {
    g_pre.bFade = bFade;
    Send();
  }
  ImGui::Dummy(ImVec2(0, P(6.0f)));
  if (g_pre.bFade) {
    Row("Near");
    double dNear = g_pre.fNear;
    if (StudioNumber("##prenear", &dNear, P(100.0f), P(26.0f), true)) {
      g_pre.fNear = dNear < 1 ? 1 : dNear > 8192 ? 8192 : dNear;
      Send();
    }
    ImGui::SameLine();
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(8.0f), C(TK_INK3),
      "how far off a unit begins to reveal it");
    ImGui::Dummy(ImVec2(0, P(30.0f)));
  }
  Row("Crossing");
  bool bCross = g_pre.bCrossing;
  if (StudioCheck("##precross", "a unit crossing sends a ripple", &bCross)) {
    g_pre.bCrossing = bCross;
    Send();
  }
  ImGui::Dummy(ImVec2(0, P(6.0f)));
  Row("Minimap");
  bool bMini = g_pre.bMinimap;
  if (StudioCheck("##premini", "drawn on the minimap as well", &bMini)) {
    g_pre.bMinimap = bMini;
    Send();
  }
  ImGui::Dummy(ImVec2(0, P(14.0f)));

  // the minimap's own picture beside it, which every renderer draws, SDL's included
  pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "On the minimap", 0.08f);
  ImGui::Dummy(ImVec2(0, P(22.0f)));
  pos = ImGui::GetCursorScreenPos();
  MiniPreview(name, pos, P(150.0f));
  ImGui::SetCursorScreenPos(ImVec2(pos.x + P(162.0f), pos.y));
  StudioMediaPara("The minimap's picture is palettized and has no alpha, and it redraws on a timer of its own: so "
                  "the mark is drawn there in the palette entry nearest its colour, its width and its dash are the "
                  "minimap's own pixels, and nothing of the fade or the pulse reaches it. It is drawn on every "
                  "renderer, SDL's included, where a mark on the ground does not show at all.",
                  fW - P(180.0f), C(TK_INK3), 11.5f);
  ImGui::SetCursorScreenPos(ImVec2(pos.x, std::max(pos.y + P(158.0f), ImGui::GetCursorScreenPos().y)));
  ImGui::Dummy(ImVec2(fW, P(8.0f)));
}

}  // namespace

std::vector<std::string> StudioMarkNames(void) {
  std::vector<std::string> out;
  const PortJson &marks = Marks();
  if (marks.kind != PortJson::OBJECT)
    return out;
  for (const PortJsonMember &m : marks.members)
    if (m.first != "comment" && m.second.kind == PortJson::OBJECT)
      out.push_back(m.first);
  return out;
}

std::string StudioMarkNote(const std::string &name) {
  const int n = BlocksOf(name);
  if (n == 0)
    return "the edge alone";
  return std::to_string(n) + (n == 1 ? " block" : " blocks");
}

void StudioMarksPage(const std::string &name, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  // the mark set again now and then while the page is drawn, so a game started since gets it back
  // and an edit made by another sender shows without a click
  if (g_pre.bOn && !g_pre.look.empty() && DesignInGame() && ImGui::GetTime() - g_pre.dSet >= 1.0)
    Send();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  if (name.empty() || Look(name).kind != PortJson::OBJECT) {
    T(d, STUDIO_FONT_BOLD, 18.0f, pos.x, pos.y, C(TK_HEAD), "Marks on the map");
    ImGui::Dummy(ImVec2(fW, P(28.0f)));
    StudioMediaPara("A mark is ground a script marks out with hx.area: a perimeter to hold, a landing zone, the "
                    "reach of something the mod runs itself. Its edge and its inside are all the engine draws of "
                    "its own; anything past that is a look, which types.json names under marks and a script puts "
                    "on a mark by name. Make one beside the list.",
                    fW, C(TK_INK2), 13.0f);
    return;
  }
  const PortJson &look = Look(name);

  T(d, STUDIO_FONT_BOLD, 18.0f, pos.x, pos.y, C(TK_HEAD), name);
  float x = pos.x + TW(STUDIO_FONT_BOLD, 18.0f, name) + P(12.0f);
  x += StudioChip(d, ImVec2(x, pos.y + P(3.0f)), StudioMarkNote(name).c_str(), CH_GOOD) + P(6.0f);
  const PortMarkLook built = LookOf(name);
  if (built.Stands())
    StudioChip(d, ImVec2(x, pos.y + P(3.0f)), "stands over the units", CH_PLAIN);
  ImGui::Dummy(ImVec2(fW, P(28.0f)));
  // the look's own line, which a mod writes in its "about"
  char szAbout[512];
  snprintf(szAbout, sizeof szAbout, "%s", look["about"].Str().c_str());
  if (StudioInput("##about", szAbout, sizeof szAbout, fW * 0.6f, P(26.0f), "what the look is, in a line")) {
    std::string why;
    Put(name, "", "about", szAbout[0] == '\0' ? PortJson() : Str(szAbout), "set the look's line", "you", &why);
  }
  ImGui::SameLine();
  if (StudioButton("##del", "Take this look away", "x", SB_FLAT, 26.0f)) {
    std::string why;
    StudioGroupBegin("you", "took away the mark look " + name);
    StudioPut("types", {"marks", name}, PortJson(), "you", "took away the mark look " + name, &why);
    StudioGroupEnd();
    if (g_pre.look == name && g_pre.bOn && DesignInGame())
      DesignCall("studio_mark_show", "\"off\":true");
    return;
  }
  ImGui::Dummy(ImVec2(fW, P(14.0f)));

  // what the loader would refuse, which the editor never writes: a file edited by hand
  const std::vector<Problem> problems = Problems(name);
  if (!problems.empty()) {
    pos = ImGui::GetCursorScreenPos();
    d->AddRectFilled(pos, ImVec2(pos.x + fW, pos.y + P(22.0f) + problems.size() * P(18.0f)), C(TK_BAD, 0.12f),
                     P(4.0f));
    T(d, STUDIO_FONT_BOLD, kStudioFsS, pos.x + P(10.0f), pos.y + P(5.0f), C(TK_BAD),
      "The loader would refuse this look, and the mod with it:");
    for (size_t i = 0; i < problems.size(); ++i)
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(18.0f), pos.y + P(24.0f) + i * P(18.0f), C(TK_INK2),
        problems[i].what, fW - P(30.0f));
    ImGui::Dummy(ImVec2(fW, P(30.0f) + problems.size() * P(18.0f)));
  }

  // the blocks as a row of chips, each one turned on or off; the one picked is written out
  pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "Its blocks", 0.08f);
  ImGui::Dummy(ImVec2(0, P(24.0f)));
  for (int i = 0; i < kBlockCount; ++i) {
    ImGui::PushID(i);
    const bool bHas = HasBlock(name, kBlocks[i]);
    if (StudioButton("##b", kBlocks[i].pszLabel, nullptr,
                     g_nBlockOpen == i ? SB_ON : bHas ? SB_PLAIN : SB_FLAT, 26.0f))
      g_nBlockOpen = i;
    ImGui::PopID();
    if (i + 1 < kBlockCount)
      ImGui::SameLine();
  }
  ImGui::Dummy(ImVec2(0, P(14.0f)));
  if (g_nBlockOpen < 0 || g_nBlockOpen >= kBlockCount)
    g_nBlockOpen = 0;
  const Block &b = kBlocks[g_nBlockOpen];
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 14.0f, pos.x, pos.y, C(TK_INK), b.pszLabel);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x + TW(STUDIO_FONT_BOLD, 14.0f, b.pszLabel) + P(12.0f), pos.y + P(2.0f),
    C(TK_INK3), b.pszWhat, fW - P(200.0f));
  ImGui::Dummy(ImVec2(fW, P(24.0f)));
  if (b.pszKey[0] != '\0') {
    const bool bHas = HasBlock(name, b);
    if (StudioButton("##bon", bHas ? "Take the block away" : "Add the block", bHas ? "x" : "plus",
                     bHas ? SB_FLAT : SB_GOOD, 26.0f)) {
      std::string why;
      if (bHas) {
        Put(name, nullptr, b.pszKey, PortJson(), std::string("took the ") + b.pszKey + " off the look", "you", &why);
      } else {
        PortJson rec;
        rec.kind = PortJson::OBJECT;
        Put(name, nullptr, b.pszKey, rec, std::string("added the ") + b.pszKey + " to the look", "you", &why);
      }
      Send();
    }
    ImGui::SameLine();
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(8.0f), C(TK_INK3),
      bHas ? "a field it leaves out draws with the engine's own value"
           : "a block the look leaves out is not drawn at all");
    ImGui::Dummy(ImVec2(fW, P(30.0f)));
    if (!bHas)
      return;
  }
  if (b.bStands) {
    StudioMediaPara("It stands up the screen from the edge, which the ground cannot be painted with, so it is drawn "
                    "over the units and its layer can only be \"over\". The field's height is the look's own, which "
                    "the cells' rows and the tear's height are measured in.",
                    fW, C(TK_INK3), 11.5f);
    ImGui::Dummy(ImVec2(0, P(8.0f)));
  }
  for (int i = 0; i < b.nFields; ++i)
    FieldRow(name, b, b.pFields[i], fW);
  ImGui::Dummy(ImVec2(0, P(18.0f)));
  PreviewRows(name, fW);
}

namespace {

// ---- the commands -----------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::string LookJson(const std::string &name) {
  return "{\"name\":" + Q(name) + ",\"blocks\":" + std::to_string(BlocksOf(name)) + ",\"look\":" +
         PortJsonWrite(Look(name)) + "}";
}

bool CmdMarks(const PortJson &, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string rows;
  for (const std::string &name : StudioMarkNames()) {
    std::string blocks;
    for (int i = 1; i < kBlockCount; ++i)
      if (HasBlock(name, kBlocks[i]))
        blocks += std::string(blocks.empty() ? "" : ",") + Q(kBlocks[i].pszKey);
    const std::vector<Problem> problems = Problems(name);
    rows += std::string(rows.empty() ? "" : ",") + "{\"name\":" + Q(name) + ",\"about\":" +
            Q(Look(name)["about"].Str()) + ",\"blocks\":[" + blocks + "],\"refused\":" +
            std::to_string(problems.size()) + "}";
  }
  *pAnswer = "\"marks\":[" + rows + "]";
  return true;
}

bool CmdMark(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str();
  if (Look(name).kind != PortJson::OBJECT) {
    *pAnswer = "the mod has no mark look " + name;
    return false;
  }
  std::string problems;
  for (const Problem &p : Problems(name))
    problems += std::string(problems.empty() ? "" : ",") + Q(p.what);
  *pAnswer = "\"mark\":" + LookJson(name) + ",\"refused\":[" + problems + "]";
  return true;
}

// A look made, a field set, or a block or the look taken away. A number outside its range is
// refused here rather than written, since the loader would refuse the whole mod for it.
bool CmdMarkSet(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str(), sender = Sender(j);
  if (name.empty()) {
    *pAnswer = "name is the look's name";
    return false;
  }
  const std::string block = j["block"].Str(), field = j["field"].Str();
  std::string why;
  const bool bRemove = j["remove"].kind == PortJson::BOOL && j["remove"].b;
  if (bRemove) {
    if (field.empty() && block.empty()) {
      StudioGroupBegin(sender, "took away the mark look " + name);
      const bool bOk = StudioPut("types", {"marks", name}, PortJson(), sender,
                                 "took away the mark look " + name, &why);
      StudioGroupEnd();
      if (!bOk) {
        *pAnswer = why;
        return false;
      }
      *pAnswer = "\"deleted\":" + Q(name) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
      return true;
    }
    if (!Put(name, block.c_str(), field.empty() ? nullptr : field.c_str(), PortJson(),
             "took a value off the mark look " + name, sender, &why)) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"cleared\":" + Q(field.empty() ? block : field) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  if (Look(name).kind != PortJson::OBJECT) {
    PortJson rec;
    rec.kind = PortJson::OBJECT;
    StudioGroupBegin(sender, "made the mark look " + name);
    const bool bOk = StudioPut("types", {"marks", name}, rec, sender, "made the mark look " + name, &why);
    StudioGroupEnd();
    if (!bOk) {
      *pAnswer = why;
      return false;
    }
    if (field.empty() && block.empty()) {
      *pAnswer = "\"made\":" + Q(name) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
      return true;
    }
  }
  if (field.empty()) {
    // a block added whole, or set from an object the caller gave
    if (block.empty()) {
      *pAnswer = "field is a field of the look, block the block it is in";
      return false;
    }
    PortJson value = j["value"];
    if (value.kind != PortJson::OBJECT) {
      value = PortJson();
      value.kind = PortJson::OBJECT;
    }
    if (!Put(name, nullptr, block.c_str(), value, "set the mark look " + name + "'s " + block, sender, &why)) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"set\":" + Q(block) + ",\"refused\":" + std::to_string(Problems(name).size()) + ",\"unsaved\":" +
               std::to_string(StudioUnsaved());
    return true;
  }
  // the field named, with the range the loader holds it to
  const Block *pb = nullptr;
  for (int i = 0; i < kBlockCount; ++i)
    if (block == kBlocks[i].pszKey)
      pb = &kBlocks[i];
  if (pb == nullptr) {
    std::string names;
    for (int i = 1; i < kBlockCount; ++i)
      names += std::string(names.empty() ? "" : ", ") + kBlocks[i].pszKey;
    *pAnswer = "block is \"\" for the look's own values, or one of " + names;
    return false;
  }
  const Field *pf = FieldOf(*pb, field);
  if (pf == nullptr) {
    *pAnswer = std::string(pb->pszLabel) + " has no field " + field;
    return false;
  }
  if ((pf->nKind == FK_NUM || pf->nKind == FK_INT) &&
      (j["value"].kind != PortJson::NUMBER || j["value"].d < pf->dMin || j["value"].d > pf->dMax)) {
    char sz[96];
    snprintf(sz, sizeof sz, "%s takes a number from %g to %g", field.c_str(), pf->dMin, pf->dMax);
    *pAnswer = sz;
    return false;
  }
  if (!Put(name, block.c_str(), field.c_str(), j["value"], "set the mark look " + name + "'s " + field, sender,
           &why)) {
    *pAnswer = why;
    return false;
  }
  if (g_pre.bOn && g_pre.look == name && DesignInGame())
    PortMapMarkSet(MarkOf());  // this runs on the game's thread, as the channel's commands do
  *pAnswer = "\"set\":" + Q(field) + ",\"refused\":" + std::to_string(Problems(name).size()) + ",\"unsaved\":" +
             std::to_string(StudioUnsaved());
  return true;
}

// The mark put in the canvas game, or taken out of it. This runs on the game's thread, which is
// where a mark may be set at all.
bool CmdMarkShow(const PortJson &j, std::string *pAnswer) {
  if (j["off"].kind == PortJson::BOOL && j["off"].b) {
    g_pre.bOn = false;
    PortMapMarkClear(kMarkId);
    *pAnswer = "\"off\":true";
    return true;
  }
  if (!DesignInGame()) {
    *pAnswer = "no game is running: the Test workspace starts one";
    return false;
  }
  const std::string name = j["name"].Str(g_pre.look.c_str());
  if (!name.empty() && Look(name).kind != PortJson::OBJECT) {
    *pAnswer = "the mod has no mark look " + name;
    return false;
  }
  if (j["shape"].kind == PortJson::STRING)
    for (int i = 0; i < 3; ++i)
      if (j["shape"].s == kShapes[i])
        g_pre.nShape = i;
  if (j["radius"].kind == PortJson::NUMBER)
    g_pre.fRadius = std::max(16.0, std::min(4096.0, j["radius"].d));
  if (j["fade"].kind == PortJson::BOOL)
    g_pre.bFade = j["fade"].b;
  if (j["crossing"].kind == PortJson::BOOL)
    g_pre.bCrossing = j["crossing"].b;
  if (j["minimap"].kind == PortJson::BOOL)
    g_pre.bMinimap = j["minimap"].b;
  const PortJson &at = j["at"];
  if (at.size() == 2) {
    g_pre.x = static_cast<int>(at.At(0).Num(0));
    g_pre.y = static_cast<int>(at.At(1).Num(0));
  }
  g_pre.look = name;
  g_pre.bOn = true;
  const PortMapMark m = MarkOf();
  PortMapMarkSet(m);
  *pAnswer = "\"shown\":" + Q(name) + ",\"shape\":" + Q(kShapes[g_pre.nShape]) + ",\"at\":[" +
             std::to_string(static_cast<int>(m.x)) + "," + std::to_string(static_cast<int>(m.y)) + "],\"radius\":" +
             Fmt(g_pre.fRadius) + ",\"marks\":" + std::to_string(PortMapMarkCount());
  return true;
}

}  // namespace

void StudioMarksInit(void) {
  PortControlCommand("studio_marks", CmdMarks);
  PortControlCommand("studio_mark", CmdMark);
  PortControlCommand("studio_mark_set", CmdMarkSet);
  PortControlCommand("studio_mark_show", CmdMarkShow);
}
