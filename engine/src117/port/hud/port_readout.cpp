// port_readout.cpp -- a mod's unit readout: classic's parts, readout.json, and what it was read
// as (port_readout.h, docs/mod-data.md, "readout.json").
//
// The parts' places are rez\statdata.bin's own controls, not a measurement of the console's art:
// the panel is a dialog, and a part is the rectangle of the control classic draws it in. So a
// readout that changes nothing is classic's panel, pixel for pixel, and a part a mod moves still
// lands where the console's art has a window for it.
#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include "CUnit.h"
#include "../../gen/globals117.h"       // gpStatUnit, the unit the panel describes
#include "../../h/handicap.h"           // HandicapShields: the most shields at the owner's handicap
#include "port_readout.h"
#include "../render/port_console.h"     // PortConsoleHudOn: a design draws the readout itself
#include "../render/port_present.h"     // PortPresentPaletteRGB: the colour codes' own colours
#include "../render/port_teamcolor.h"   // PortTeamColorGet: a part drawn in the player's colour
#include "../data/port_modlua.h"        // ModLuaHasReadout, ModLuaReadout: the hook
#include "../data/port_modstatus.h"     // ModStatusOnUnit: the statuses a line names
#include "../data/port_mods.h"          // PortMods, PortModActive: the active mod's folder
#include "../data/port_mode.h"          // PortModeFolderOfGame: the game's mode's folder
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../front/port_text.h"         // PortText, PortTextF: the tooltips' own words
#include "../platform/port_settings.h"  // Reduce motion
#include "../platform/port_util.h"      // PortReadFile

namespace {

const PortLogger Log("hyperion_mods.log");

// ---- classic's parts -----------------------------------------------------------------------------
// Each part's name, the control of rez\statdata.bin that draws it, and that control's rectangle in
// the dialog's own coordinates. The slots part covers all four stat icons; one of them is
// kSlotX plus kSlotStep.
struct Part {
  const char *pszName;
  int nControl;
  int x, y, w, h;
};
const Part kParts[PORT_READOUT_PARTS] = {
    {"wire", 1, 30, 0, 64, 64},
    {"name", -5, 102, 0, 152, 14},
    {"status", -20, 106, 16, 144, 14},
    {"kills", -21, 106, 30, 144, 14},
    {"slots", 9, 104, 51, 153, 36},
    {"life", -7, 22, 64, 80, 12},
    {"energy", -8, 22, 76, 80, 12},
};
const int kSlotX = 104, kSlotY = 51, kSlotW = 36, kSlotH = 36, kSlotStep = 39;

const char *const kOps[PORT_READOUT_OPS] = {"flash", "float", "roll", "slide", "light", "blink",
                                            "scan",  "type",  "beat", "glow",  "shimmer"};
// The ops that move text, which the player's Reduce motion turns off. Every other op is carried
// by light alone.
const bool kbMoves[PORT_READOUT_OPS] = {false, true, false, true, false, false, true, true, false, false, true};
const char *const kEvents[PORT_READOUT_EVENTS] = {"selected", "damaged", "healed", "status_added",
                                                  "stat_changed", "kill"};
const char *const kStats[PORT_READOUT_STATS] = {"armor", "shields", "damage", "cooldown", "life", "speed"};

// ---- how a status shows itself ---------------------------------------------------------------------
// The fourteen kinds, strongest first: a status with several fields takes the first its fields
// match. hue is the kind's own colour, pattern what it lays over a wireframe, and nIcon the
// classic command icon that says the same thing (upgrades.dat's and techdata.dat's numbers).
struct Kind {
  const char *pszName;
  const char *pszWords;
  unsigned int nHue;
  const char *pszPattern;
  int nIcon;
};
const Kind kKinds[PORT_READOUT_KINDS] = {
    {"root", "Can't move", 0x7FD4FFu, "bars", 240},
    {"disarm", "Can't attack", 0xC9A2FFu, "cross", 369},
    {"dot", "Damage over time", 0xFF8A3Au, "embers", 265},
    {"heal", "Heals", 0x5FE07Au, "pluses", 365},
    {"armor_down", "Less armour", 0xB6E04Au, "cracks", 344},
    {"armor_up", "More armour", 0xF2C94Cu, "hex", 292},
    {"haste", "Attacks faster", 0xFFB347u, "streaks", 237},
    {"jam", "Attacks slower", 0x8FB4FFu, "static", 241},
    {"slow", "Slower", 0x9FE6FFu, "frost", 267},
    {"swift", "Faster", 0x4FD2C2u, "chevrons", 263},
    {"might", "Deals more", 0xFF9A3Cu, "stripes", 288},
    {"weak", "Deals less", 0xA0A8B8u, "sparse", 373},
    {"frail", "Takes more", 0xFF5A7Au, "cracks", 242},
    {"tough", "Takes less", 0xD8C39Au, "hex", 247},
};
// Which status the one mark on a wireframe shows: what stops the unit first, then what hurts it,
// then the rest. The place in this list is the order; the number falls out of it, so a mod's own
// "priority" can sit between two of them.
const int kanRanking[PORT_READOUT_KINDS] = {
    PORT_KIND_ROOT,  PORT_KIND_DISARM, PORT_KIND_DOT,      PORT_KIND_ARMOR_DOWN, PORT_KIND_FRAIL,
    PORT_KIND_JAM,   PORT_KIND_SLOW,   PORT_KIND_WEAK,     PORT_KIND_HEAL,       PORT_KIND_ARMOR_UP,
    PORT_KIND_TOUGH, PORT_KIND_HASTE,  PORT_KIND_SWIFT,    PORT_KIND_MIGHT};

// The glyphs, one per kind: a run of strokes in a 16 by 16 square, each stroke its count of
// points and then that many x, y pairs, the run ending in a count of 0. The drawing's curves are
// written as short straight runs, which is what a curve comes to at this size.
#define G_END 0
const signed char kGlyphRoot[] = {  // a chain: two links, each a ring of six
    7, 2,8, 3,6, 6,5, 9,6, 9,9, 6,10, 2,8,
    7, 7,8, 8,6, 11,5, 14,6, 14,9, 11,10, 7,8, G_END};
const signed char kGlyphDisarm[] = {  // a blade, struck through
    2, 4,12, 11,5,
    2, 10,3, 13,6,
    2, 3,11, 5,13,
    2, 3,3, 13,13, G_END};
const signed char kGlyphDot[] = {  // a flame
    8, 8,2, 11,6, 12,10, 11,14, 8,15, 5,14, 4,10, 6,6,
    3, 6,6, 7,9, 8,2, G_END};
const signed char kGlyphHeal[] = {  // a cross
    2, 8,3, 8,13,
    2, 3,8, 13,8, G_END};
const signed char kGlyphArmorUp[] = {  // a shield, a chevron inside it
    8, 8,2, 13,4, 13,9, 11,12, 8,15, 5,12, 3,9, 3,4,
    2, 3,4, 8,2,
    3, 5,10, 8,7, 11,10, G_END};
const signed char kGlyphArmorDown[] = {  // a shield, split
    8, 8,2, 13,4, 13,9, 11,12, 8,15, 5,12, 3,9, 3,4,
    2, 3,4, 8,2,
    4, 9,3, 7,7, 9,9, 7,13, G_END};
const signed char kGlyphHaste[] = {  // a dial, its hand swept, speed lines behind
    9, 8,3, 11,4, 13,7, 13,10, 11,13, 8,14, 5,13, 3,10, 3,6,
    2, 3,6, 8,3,
    2, 8,9, 8,5,
    2, 8,9, 11,10,
    2, 1,4, 3,4,
    2, 1,7, 2,7, G_END};
const signed char kGlyphJam[] = {  // an hourglass
    2, 4,3, 12,3,
    2, 4,14, 12,14,
    2, 5,3, 12,14,
    2, 12,3, 5,14, G_END};
const signed char kGlyphSlow[] = {  // a snowflake, three bars
    2, 2,8, 14,8,
    2, 5,3, 11,13,
    2, 11,3, 5,13, G_END};
const signed char kGlyphSwift[] = {  // two chevrons
    3, 3,4, 7,8, 3,12,
    3, 9,4, 13,8, 9,12, G_END};
const signed char kGlyphMight[] = {  // a blade, upright
    2, 8,2, 8,11,
    2, 5,11, 11,11,
    2, 8,11, 8,14,
    3, 7,4, 8,2, 10,4, G_END};
const signed char kGlyphWeak[] = {  // a blade, broken
    2, 8,2, 8,6,
    2, 8,9, 8,11,
    2, 5,11, 11,11,
    2, 8,11, 8,14,
    2, 6,8, 10,7, G_END};
const signed char kGlyphFrail[] = {  // a heart, cracked
    9, 8,14, 4,10, 2,6, 3,3, 6,3, 8,5, 10,3, 13,3, 14,6,
    3, 14,6, 12,10, 8,14,
    4, 8,5, 7,8, 9,10, 8,12, G_END};
const signed char kGlyphTough[] = {  // a wall: two courses of block
    5, 3,4, 13,4, 13,12, 3,12, 3,4,
    2, 3,8, 13,8,
    2, 8,4, 8,8,
    2, 6,8, 6,12,
    2, 11,8, 11,12, G_END};
const signed char *const kapGlyphs[PORT_READOUT_KINDS] = {
    kGlyphRoot, kGlyphDisarm, kGlyphDot,   kGlyphHeal, kGlyphArmorDown, kGlyphArmorUp, kGlyphHaste,
    kGlyphJam,  kGlyphSlow,   kGlyphSwift, kGlyphMight, kGlyphWeak,     kGlyphFrail,   kGlyphTough};

// ---- reading -------------------------------------------------------------------------------------
// A problem leaves its parameter classic's and is said once, with the line it was on; the rest of
// the file still reads. So a mod with one key wrong loses that key, not its readout.
struct Reader {
  const char *pszText = nullptr;          // for the line numbers
  std::vector<std::string> *pWarnings = nullptr;

  void Say(const PortJson &v, const std::string &what) {
    char sz[32] = "";
    const int nLine = pszText != nullptr ? PortJsonLineAt(pszText, v.nAt) : 0;
    if (nLine > 0)
      snprintf(sz, sizeof sz, " (line %d)", nLine);
    const std::string s = "readout." + what + sz;
    if (pWarnings != nullptr)
      pWarnings->push_back(s);
    Log("readout: %s", s.c_str());
  }

  // Every key an object has that the list does not name.
  void Only(const PortJson &o, const std::vector<const char *> &keys, const std::string &where) {
    if (o.kind != PortJson::OBJECT)
      return;
    for (const PortJsonMember &m : o.members) {
      bool bKnown = m.first == "comment";
      for (const char *psz : keys)
        bKnown = bKnown || m.first == psz;
      if (!bKnown)
        Say(m.second, where + " has no field " + m.first);
    }
  }

  // A number within its band; the value is left alone when there is none or it is wrong.
  void Number(const PortJson &o, const char *pszKey, double dMin, double dMax, double *pd,
              const std::string &where) {
    const PortJson &v = o[pszKey];
    if (v.kind == PortJson::NONE)
      return;
    if (v.kind != PortJson::NUMBER || v.d < dMin || v.d > dMax) {
      char sz[96];
      snprintf(sz, sizeof sz, " takes a number from %g to %g", dMin, dMax);
      Say(v, where + "." + pszKey + sz);
      return;
    }
    *pd = v.d;
  }

  void Flag(const PortJson &o, const char *pszKey, bool *pb, const std::string &where) {
    const PortJson &v = o[pszKey];
    if (v.kind == PortJson::NONE)
      return;
    if (v.kind != PortJson::BOOL) {
      Say(v, where + "." + pszKey + " takes true or false");
      return;
    }
    *pb = v.b;
  }

  void Choice(const PortJson &o, const char *pszKey, const char *const *ppsz, int n, int *pn,
              const std::string &where) {
    const PortJson &v = o[pszKey];
    if (v.kind == PortJson::NONE)
      return;
    for (int i = 0; i < n; ++i)
      if (v.kind == PortJson::STRING && v.s == ppsz[i]) {
        *pn = i;
        return;
      }
    std::string list;
    for (int i = 0; i < n; ++i)
      list += std::string(i ? ", " : "") + ppsz[i];
    Say(v, where + "." + pszKey + " is one of " + list);
  }

  void Text(const PortJson &o, const char *pszKey, std::string *ps, const std::string &where) {
    const PortJson &v = o[pszKey];
    if (v.kind == PortJson::NONE)
      return;
    if (v.kind != PortJson::STRING) {
      Say(v, where + "." + pszKey + " takes a line of text");
      return;
    }
    *ps = v.s;
  }

};

// The colour names each parameter takes, in the order their inks are written below.
const char *const kInkClassic[] = {"classic", "rank", "team"};
const int kanInkClassic[] = {PORT_READOUT_INK_CLASSIC, PORT_READOUT_INK_RANK, PORT_READOUT_INK_TEAM};
const char *const kInkLife[] = {"damage", "life", "classic"};
const int kanInkLife[] = {PORT_READOUT_INK_DAMAGE, PORT_READOUT_INK_LIFE, PORT_READOUT_INK_CLASSIC};
const char *const kInkRank[] = {"rank", "classic", "team"};
const int kanInkRank[] = {PORT_READOUT_INK_RANK, PORT_READOUT_INK_CLASSIC, PORT_READOUT_INK_TEAM};

// A colour parameter: its names and what each one means, so the names can read in the order the
// parameter lists them while the ink keeps its own number.
void ReadColour(Reader &r, const PortJson &o, const char *pszKey, const char *const *ppsz,
                const int *pan, int n, PortReadoutColour *pc, const std::string &where) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE)
    return;
  if (v.kind == PortJson::STRING && v.s.size() == 7 && v.s[0] == '#') {
    unsigned int rgb = 0;
    if (sscanf(v.s.c_str() + 1, "%6x", &rgb) == 1) {
      pc->nInk = PORT_READOUT_INK_RGB;
      pc->nRgb = rgb;
      return;
    }
  }
  for (int i = 0; i < n; ++i)
    if (v.kind == PortJson::STRING && v.s == ppsz[i]) {
      pc->nInk = pan[i];
      return;
    }
  std::string list;
  for (int i = 0; i < n; ++i)
    list += std::string(i ? ", " : "") + ppsz[i];
  r.Say(v, where + "." + pszKey + " is #rrggbb or one of " + list);
}

void ReadWire(Reader &r, const PortJson &o, PortReadoutParts *p) {
  const std::string where = "parts.wire";
  r.Only(o, {"tint", "states", "frame", "frame_colour", "art"}, where);
  static const char *const kTint[] = {"life", "rank", "team", "none"};
  r.Choice(o, "tint", kTint, 4, &p->nTint, where);
  static const char *const kFrame[] = {"none", "rank", "corners"};
  r.Choice(o, "frame", kFrame, 3, &p->nFrame, where);
  ReadColour(r, o, "frame_colour", kInkRank, kanInkRank, 3, &p->frameColour, where);
  const PortJson &st = o["states"];
  if (st.kind == PortJson::ARRAY && st.size() == 2 && st.At(0).kind == PortJson::NUMBER &&
      st.At(1).kind == PortJson::NUMBER && st.At(0).d > st.At(1).d && st.At(0).d <= 100 && st.At(1).d >= 0) {
    p->anStates[0] = static_cast<int>(st.At(0).d);
    p->anStates[1] = static_cast<int>(st.At(1).d);
  } else if (st.kind != PortJson::NONE) {
    r.Say(st, where + ".states takes two percentages, the higher first");
  }
  static const char *const kArt[] = {"unit", "mod"};
  int nArt = 0;
  r.Choice(o, "art", kArt, 2, &nArt, where);
  p->bModArt = nArt == 1;
}

void ReadName(Reader &r, const PortJson &o, PortReadoutParts *p) {
  const std::string where = "parts.name";
  r.Only(o, {"text", "colour", "align"}, where);
  r.Text(o, "text", &p->name, where);
  ReadColour(r, o, "colour", kInkClassic, kanInkClassic, 3, &p->nameColour, where);
  static const char *const kAlign[] = {"centre", "left"};
  r.Choice(o, "align", kAlign, 2, &p->nNameAlign, where);
}

void ReadStatus(Reader &r, const PortJson &o, PortReadoutParts *p) {
  const std::string where = "parts.status";
  r.Only(o, {"lines", "rotate", "rank_pips"}, where);
  static const char *const kLines[] = {"classic", "classic then mod", "mod"};
  r.Choice(o, "lines", kLines, 3, &p->nStatusLines, where);
  r.Number(o, "rotate", 0.5, 30, &p->fRotate, where);
  r.Flag(o, "rank_pips", &p->bRankPips, where);
}

void ReadKills(Reader &r, const PortJson &o, PortReadoutParts *p) {
  const std::string where = "parts.kills";
  r.Only(o, {"show", "text"}, where);
  static const char *const kShow[] = {"auto", "always", "never"};
  r.Choice(o, "show", kShow, 3, &p->nKillsShow, where);
  r.Text(o, "text", &p->kills, where);
}

void ReadSlots(Reader &r, const PortJson &o, PortReadoutParts *p) {
  const std::string where = "parts.slots";
  r.Only(o, {"order", "level", "delta", "tooltip"}, where);
  r.Text(o, "level", &p->level, where);
  static const char *const kDelta[] = {"off", "on the icon", "in the tooltip"};
  r.Choice(o, "delta", kDelta, 3, &p->nDelta, where);
  const PortJson &order = o["order"];
  if (order.kind == PortJson::ARRAY) {
    p->slots.clear();
    for (size_t i = 0; i < order.size(); ++i) {
      if (order.At(i).kind != PortJson::STRING) {
        r.Say(order.At(i), where + ".order names its icons, each as a line of text");
        p->slots.clear();
        break;
      }
      p->slots.push_back(order.At(i).s);
    }
  } else if (order.kind != PortJson::NONE) {
    r.Say(order, where + ".order is a list of names");
  }
  const PortJson &tip = o["tooltip"];
  if (tip.kind == PortJson::ARRAY) {
    p->tooltip.clear();
    for (size_t i = 0; i < tip.size(); ++i) {
      if (tip.At(i).kind != PortJson::STRING) {
        r.Say(tip.At(i), where + ".tooltip is a line of text for each line");
        p->tooltip.clear();
        break;
      }
      p->tooltip.push_back(tip.At(i).s);
    }
  } else if (tip.kind == PortJson::STRING) {
    p->tooltip.assign(1, tip.s);
  } else if (tip.kind != PortJson::NONE) {
    r.Say(tip, where + ".tooltip is a line of text, or a list of them");
  }
}

void ReadLife(Reader &r, const PortJson &o, PortReadoutParts *p) {
  const std::string where = "parts.life";
  r.Only(o, {"text", "colour"}, where);
  r.Text(o, "text", &p->life, where);
  ReadColour(r, o, "colour", kInkLife, kanInkLife, 3, &p->lifeColour, where);
}

void ReadEnergy(Reader &r, const PortJson &o, PortReadoutParts *p) {
  const std::string where = "parts.energy";
  r.Only(o, {"text", "show"}, where);
  r.Text(o, "text", &p->energy, where);
  static const char *const kShow[] = {"classic", "every caster", "never"};
  r.Choice(o, "show", kShow, 3, &p->nEnergyShow, where);
}

void ReadParts(Reader &r, const PortJson &o, PortReadoutParts *p) {
  r.Only(o, {"wire", "name", "status", "kills", "slots", "life", "energy"}, "parts");
  if (o["wire"].kind == PortJson::OBJECT)
    ReadWire(r, o["wire"], p);
  if (o["name"].kind == PortJson::OBJECT)
    ReadName(r, o["name"], p);
  if (o["status"].kind == PortJson::OBJECT)
    ReadStatus(r, o["status"], p);
  if (o["kills"].kind == PortJson::OBJECT)
    ReadKills(r, o["kills"], p);
  if (o["slots"].kind == PortJson::OBJECT)
    ReadSlots(r, o["slots"], p);
  if (o["life"].kind == PortJson::OBJECT)
    ReadLife(r, o["life"], p);
  if (o["energy"].kind == PortJson::OBJECT)
    ReadEnergy(r, o["energy"], p);
}

void ReadAdd(Reader &r, const PortJson &o, PortReadoutAdd *p) {
  r.Only(o, {"lines"}, "add");
  const PortJson &lines = o["lines"];
  if (lines.kind == PortJson::NONE)
    return;
  if (lines.kind != PortJson::OBJECT) {
    r.Say(lines, "add.lines is an object");
    return;
  }
  const std::string where = "add.lines";
  r.Only(lines, {"each", "each_short", "bar", "in_huds"}, where);
  p->bLines = true;
  r.Text(lines, "each", &p->each, where);
  r.Text(lines, "each_short", &p->eachShort, where);
  r.Flag(lines, "bar", &p->bBar, where);
  static const char *const kInHuds[] = {"in turn", "a line each"};
  int nIn = 1;
  r.Choice(lines, "in_huds", kInHuds, 2, &nIn, where);
  p->bLinePerStatus = nIn == 1;
}

// "ranks": a rank's id to its colour, or to an object with the colour and the words a player
// reads. The order they are written in is the order they rank, since the classic panel draws a
// colour as the palette's nearest and two ranks may land on one entry: what carries a rank there
// is its place, which the pip's size shows.
void ReadRanks(Reader &r, const PortJson &o, std::vector<PortReadoutRank> *pRanks) {
  if (o.kind != PortJson::OBJECT) {
    r.Say(o, "ranks is an object: a rank's name to its colour");
    return;
  }
  for (const PortJsonMember &m : o.members) {
    const PortJson &v = m.second;
    std::string colour = v.Str("");
    PortReadoutRank rank;
    rank.id = m.first;
    rank.name = m.first;
    if (v.kind == PortJson::OBJECT) {
      r.Only(v, {"colour", "name"}, "ranks." + m.first);
      colour = v["colour"].Str("");
      rank.name = v["name"].Str(m.first.c_str());
    }
    unsigned int rgb = 0;
    if (colour.size() != 7 || colour[0] != '#' || sscanf(colour.c_str() + 1, "%6x", &rgb) != 1) {
      r.Say(v, "ranks." + m.first + " takes a colour, #rrggbb, or {colour, name}");
      continue;
    }
    rank.nRgb = rgb;
    pRanks->push_back(rank);
  }
}

// "status_look": where and how a status shows itself.
void ReadStatusLook(Reader &r, const PortJson &o, PortReadoutStatusLook *p) {
  const std::string where = "status_look";
  if (o.kind != PortJson::OBJECT) {
    r.Say(o, where + " is an object");
    return;
  }
  r.Only(o, {"line3", "icon", "wire_mark", "group_mark"}, where);
  static const char *const kIcon[] = {"framed", "glyph", "game", "none"};
  r.Choice(o, "icon", kIcon, 4, &p->nIcon, where);
  static const char *const kLine3[] = {"in turn", "a row"};
  r.Choice(o, "line3", kLine3, 2, &p->nLine3, where);
  static const char *const kWire[] = {"corner", "tint", "pattern", "none"};
  r.Choice(o, "wire_mark", kWire, 4, &p->nWireMark, where);
  static const char *const kGroup[] = {"glyphs", "dots", "edge", "none"};
  r.Choice(o, "group_mark", kGroup, 4, &p->nGroupMark, where);
}

// One effect. An effect whose op or part has no name is left out whole, since nothing of it can
// be drawn.
bool ReadFx(Reader &r, const PortJson &o, const std::vector<PortReadoutRank> &ranks, PortReadoutFx *pFx) {
  const std::string where = "fx";
  r.Only(o, {"on", "while", "if", "part", "do", "colour", "text", "time", "hold", "rate", "every",
             "size", "light", "glint", "glow", "from"},
         where);
  const PortJson &op = o["do"];
  int nOp = -1;
  for (int i = 0; i < PORT_READOUT_OPS; ++i)
    if (op.kind == PortJson::STRING && op.s == kOps[i])
      nOp = i;
  if (nOp < 0) {
    std::string list;
    for (int i = 0; i < PORT_READOUT_OPS; ++i)
      list += std::string(i ? ", " : "") + kOps[i];
    r.Say(op.kind == PortJson::NONE ? o : op, where + ".do is one of " + list);
    return false;
  }
  pFx->nOp = nOp;

  // "part": a part's name, or "slots.{stat}", the icon of the stat the event names
  const PortJson &part = o["part"];
  std::string name = part.Str("");
  if (name.compare(0, 6, "slots.") == 0) {
    pFx->slot = name.substr(6);
    name = "slots";
  }
  const int nPart = PortReadoutPartOf(name.c_str());
  if (nPart < 0) {
    r.Say(part.kind == PortJson::NONE ? o : part,
          where + ".part is wire, name, status, kills, lines, slots, life or energy, "
                  "or slots.<stat>");
    return false;
  }
  pFx->nPart = nPart;
  // "lines" is not one part: it is wherever a status's line is drawn, which the row the panel
  // shows them on settles as the game runs (LinesPart), since the file may be read before the
  // block that decides it
  pFx->bLines = name == "lines";

  // "on": one event or several; or "while": a test that holds it on. Never both.
  const PortJson &on = o["on"];
  int nOn = 0;
  for (size_t i = 0; i < (on.kind == PortJson::ARRAY ? on.size() : 1); ++i) {
    const PortJson &e = on.kind == PortJson::ARRAY ? on.At(i) : on;
    if (e.kind == PortJson::NONE)
      break;
    bool bFound = false;
    for (int k = 0; k < PORT_READOUT_EVENTS; ++k)
      if (e.kind == PortJson::STRING && e.s == kEvents[k]) {
        pFx->abOn[k] = true;
        bFound = true;
        ++nOn;
      }
    if (!bFound) {
      std::string list;
      for (int k = 0; k < PORT_READOUT_EVENTS; ++k)
        list += std::string(k ? ", " : "") + kEvents[k];
      r.Say(e, where + ".on is one of " + list + ", or a list of them");
      return false;
    }
  }
  const PortJson &hold = o["while"];
  if (hold.kind == PortJson::STRING) {
    if (nOn > 0) {
      r.Say(hold, where + " takes on or while, not both");
      return false;
    }
    // "life < 0.33", "rank >= elite", "status.left < 2"
    const std::string s = hold.s;
    double d = 0;
    char szWord[32] = "";
    if (sscanf(s.c_str(), "life < %lf", &d) == 1 && d > 0 && d <= 1) {
      pFx->nWhile = PORT_READOUT_WHILE_LIFE;
      pFx->fAt = d;
    } else if (sscanf(s.c_str(), "status.left < %lf", &d) == 1 && d > 0) {
      pFx->nWhile = PORT_READOUT_WHILE_STATUS;
      pFx->fAt = d;
    } else if (sscanf(s.c_str(), "rank >= %31s", szWord) == 1) {
      bool bKnown = false;
      for (const PortReadoutRank &rank : ranks)
        bKnown = bKnown || rank.id == szWord;
      if (!bKnown) {
        r.Say(hold, where + ".while names the rank " + szWord + ", which ranks does not");
        return false;
      }
      pFx->nWhile = PORT_READOUT_WHILE_RANK;
      pFx->rank = szWord;
    } else {
      r.Say(hold, where + ".while is \"life < <part>\", \"rank >= <rank>\" or \"status.left < <seconds>\"");
      return false;
    }
  } else if (hold.kind != PortJson::NONE) {
    r.Say(hold, where + ".while is a test, as a line of text");
    return false;
  }
  if (nOn == 0 && pFx->nWhile == PORT_READOUT_WHILE_NONE) {
    r.Say(o, where + " needs on, an event, or while, a test");
    return false;
  }

  ReadColour(r, o, "colour", kInkRank, kanInkRank, 3, &pFx->colour, where);
  r.Text(o, "if", &pFx->ifRank, where);
  r.Text(o, "text", &pFx->text, where);
  r.Number(o, "time", 0.05, 4, &pFx->fTime, where);
  r.Number(o, "hold", 0, 8, &pFx->fHold, where);
  r.Number(o, "rate", 0.1, 8, &pFx->fRate, where);
  r.Number(o, "every", 0.5, 20, &pFx->fEvery, where);
  r.Number(o, "size", 0, 16, &pFx->fSize, where);
  r.Number(o, "light", 0, 1, &pFx->fLight, where);
  r.Flag(o, "glint", &pFx->bGlint, where);
  r.Flag(o, "glow", &pFx->bGlow, where);
  static const char *const kFrom[] = {"right", "left"};
  int nFrom = 0;
  r.Choice(o, "from", kFrom, 2, &nFrom, where);
  pFx->bFromRight = nFrom == 0;
  // A blink is capped at three a second whichever way the file sets it, and the player's Reduce
  // motion caps it again (PortReadoutReduceMotion).
  if (pFx->nOp == PORT_READOUT_BLINK && pFx->fRate > 3.0)
    pFx->fRate = 3.0;
  // A roll counts a line's figures, and the life and the energy are the only lines with figures
  // to count, so one anywhere else would never show. Said here, where the mod is read, rather
  // than left to be noticed in a game.
  if (pFx->nOp == PORT_READOUT_ROLL && pFx->nPart != PORT_READOUT_LIFE &&
      pFx->nPart != PORT_READOUT_ENERGY)
    r.Say(o, where + ".do roll counts a line's figures, so it draws on life or energy, not " +
                 PortReadoutPartName(pFx->nPart));
  return true;
}

PortReadoutFile g_file;
int g_nRoomW = PORT_READOUT_W, g_nRoom = PORT_READOUT_H;

// ---- the effects running ---------------------------------------------------------------------------
// One run of an effect: which of the file's effects it is, when it began on the readout's own
// clock, and the values the event or the script gave it. Nothing here is read by the game: it is
// dropped when a game starts, goes in no save and in no hash, and a script's own call draws for
// this viewer alone.
struct Run {
  int nFx = -1;         // the file's effect, -1 for one a script set off itself
  int nOp = PORT_READOUT_FLASH;
  int nPart = PORT_READOUT_WIRE;
  double fAt = 0;       // when it began, in seconds of the readout's clock
  double fTime = 0.25;
  double fHold = 0;
  int nAmount = 0;      // the life a hit took, or the stat a status moved
  std::string text;
  PortReadoutColour colour;
  // what the drawing needs of the effect, so a run a script set off carries as much as one of
  // the file's and the drawing asks the file for nothing
  double fRate = 1.0, fEvery = 0, fSize = 0, fLight = 0.5;
  bool bGlint = false, bGlow = false, bFromRight = true;
  std::string slot;     // the stat whose icon it is on, "" for the whole part
  // what a roll counts the line from, as the panel showed it the frame before
  int nFromLife = -1, nFromShields = -1, nFromEnergy = -1;
  std::string status;   // the status that set it off, for the line that slides in
};
// At most this many runs at once; the oldest goes when a new one has no room, so a unit under
// fire cannot grow the list without end.
const size_t kRunsMost = 24;
std::vector<Run> g_runs;
double g_fClock = 0;      // the readout's clock, in seconds, which the game's frames move on
// The line that came last takes the place at once: a status that lands, or a kill, shows for its
// own turn and the rotation carries on from it, so a status of a few seconds is read rather than
// waiting out a cycle of the lines before it.
// The status the kills' row shows now, by name rather than as a pointer to its line: the lines
// are built again whenever the view is asked for, so a name is what still means the same line
// afterwards, and a status that has ended names none.
std::string g_strRow;
double g_fTook = -1e9;    // when it took the place, in the clock's seconds
std::string g_strTook;    // the status that landed, "" for the kills' own line
bool g_bTookKill = false;
void TookTheLine(const std::string &status, bool bKill) {
  g_fTook = g_fClock;
  g_strTook = status;
  g_bTookKill = bKill;
}
unsigned g_dwUnit = 0;    // the unit the runs belong to; another unit drops them

// The frames a second the game counts at Fastest, which a status's duration was read in.
const double kFps = 24.0;

// What the unit the panel describes was last frame, so the frame's events can be seen without
// a hook anywhere in the game: the readout watches the unit and raises its own. A hit and a heal
// in one frame come out as one event each, which is what a flash wants, and a replay and a
// watcher see the same events from the same game.
struct Was {
  unsigned dwUnit = 0;
  int nLife = 0, nShields = 0, nKills = 0;
  // the same figures as the panel showed them, which is where a roll counts from: classic rounds
  // the life up and the shields down, so a roll that began at the raw number would start a point
  // out from the line the player was reading
  int nLifeShown = 0, nEnergy = 0;
  std::vector<std::string> statuses;
  int anStat[PORT_READOUT_STATS] = {0, 0, 0, 0, 0};
  bool bAny = false;
};
Was g_was;
void Events();

// What the hook gave for the unit the panel describes: what is given back, with its statuses
// filled in from the game each time it is asked for, and the lines as the hook gave them, so a
// line whose status has run out comes back if the status does.
PortReadoutView g_view;
std::vector<PortReadoutLine> g_said;
unsigned g_dwViewUnit = 0;   // the unit the view describes
unsigned g_dwViewMark = 0;   // a digest of the values the call was made for
bool g_bViewAsked = false;   // a call has been made for this unit and these values
std::vector<std::string> g_keysSaid;  // the keys of the answer said to be read by nothing (SayUnknown)
// The words the panel was last given for each part, and which line of how many the kills' row is
// showing, as the words settled it. The drawing reads both (PortReadoutPartWords,
// PortReadoutRowTurn), so the rank's pips, the row's bar and its turn index land on the words the
// player is reading rather than on a guess at them.
std::string g_astrWords[PORT_READOUT_PARTS];
int g_nRowAt = 0, g_nRowOf = 0;

}  // namespace

const char *PortReadoutPartName(int nPart) {
  return nPart >= 0 && nPart < PORT_READOUT_PARTS ? kParts[nPart].pszName : "";
}

int PortReadoutPartOf(const char *pszName) {
  if (pszName == nullptr)
    return -1;
  for (int i = 0; i < PORT_READOUT_PARTS; ++i)
    if (strcmp(pszName, kParts[i].pszName) == 0)
      return i;
  // "lines" is wherever a status's line is drawn: the room's rows in a HUD slot, and on the panel
  // the row that is showing that line, which is the kills' row where a mod adds lines and the
  // status's own row where it does not. The design writes that name in an effect.
  if (strcmp(pszName, "lines") == 0)
    return g_file.add.bLines ? PORT_READOUT_KILLS : PORT_READOUT_STATUS;
  return -1;
}

PortReadoutRect PortReadoutPartRect(int nPart) {
  PortReadoutRect rc;
  if (nPart >= 0 && nPart < PORT_READOUT_PARTS) {
    rc.x = kParts[nPart].x;
    rc.y = kParts[nPart].y;
    rc.w = kParts[nPart].w;
    rc.h = kParts[nPart].h;
  }
  return rc;
}

PortReadoutRect PortReadoutSlotRect(int nSlot) {
  PortReadoutRect rc;
  if (nSlot >= 0 && nSlot < PORT_READOUT_SLOTS_MOST) {
    rc.x = kSlotX + kSlotStep * nSlot;
    rc.y = kSlotY;
    rc.w = kSlotW;
    rc.h = kSlotH;
  }
  return rc;
}

int PortReadoutPartControl(int nPart) {
  return nPart >= 0 && nPart < PORT_READOUT_PARTS ? kParts[nPart].nControl : 0;
}

const char *PortReadoutOpName(int nOp) { return nOp >= 0 && nOp < PORT_READOUT_OPS ? kOps[nOp] : ""; }

const char *PortReadoutEventName(int nEvent) {
  return nEvent >= 0 && nEvent < PORT_READOUT_EVENTS ? kEvents[nEvent] : "";
}

bool PortReadoutOpMoves(int nOp) { return nOp >= 0 && nOp < PORT_READOUT_OPS && kbMoves[nOp]; }

const char *PortReadoutStatName(int nStat) {
  return nStat >= 0 && nStat < PORT_READOUT_STATS ? kStats[nStat] : "";
}

const char *PortReadoutStatWords(int nStat) {
  // the English is the catalog's key; what shows it translates it (port_text.h)
  static const char *const kWords[PORT_READOUT_STATS] = {PT_("Armour"), PT_("Shields"), PT_("Damage"),
                                                         PT_("Cooldown"), PT_("Life"), PT_("Speed")};
  return nStat >= 0 && nStat < PORT_READOUT_STATS ? kWords[nStat] : "";
}

bool PortReadoutStatLowerIsBetter(int nStat) { return nStat == PORT_READOUT_STAT_COOLDOWN; }

bool PortReadoutStatIsCount(int nStat) { return nStat == PORT_READOUT_STAT_ARMOR; }

const char *PortReadoutKindName(int nKind) {
  return nKind >= 0 && nKind < PORT_READOUT_KINDS ? kKinds[nKind].pszName : "";
}

const char *PortReadoutKindWords(int nKind) {
  return nKind >= 0 && nKind < PORT_READOUT_KINDS ? kKinds[nKind].pszWords : "";
}

unsigned int PortReadoutKindHue(int nKind) {
  return nKind >= 0 && nKind < PORT_READOUT_KINDS ? kKinds[nKind].nHue : 0xFFFFFFu;
}

const char *PortReadoutKindPattern(int nKind) {
  return nKind >= 0 && nKind < PORT_READOUT_KINDS ? kKinds[nKind].pszPattern : "";
}

int PortReadoutKindIcon(int nKind) {
  return nKind >= 0 && nKind < PORT_READOUT_KINDS ? kKinds[nKind].nIcon : -1;
}

int PortReadoutKindFrom(int nKindNamed, bool bNoMove, bool bNoAttack, int nDps, int nRegen, int nArmor,
                        int nCooldown, int nSpeed, int nDamage, int nTaken) {
  if (nKindNamed >= 0 && nKindNamed < PORT_READOUT_KINDS)
    return nKindNamed;  // the kind its "look" names
  // the first its fields match, strongest first: what stops the unit, then what hurts it, then
  // the rest, which is the order the kinds are declared in
  if (bNoMove)
    return PORT_KIND_ROOT;
  if (bNoAttack)
    return PORT_KIND_DISARM;
  if (nDps > 0 && !(nCooldown < 0))
    return PORT_KIND_DOT;
  if (nRegen > 0)
    return PORT_KIND_HEAL;
  if (nArmor < 0)
    return PORT_KIND_ARMOR_DOWN;
  if (nArmor > 0)
    return PORT_KIND_ARMOR_UP;
  if (nCooldown < 0)
    return PORT_KIND_HASTE;
  if (nCooldown > 0)
    return PORT_KIND_JAM;
  if (nSpeed < 0)
    return PORT_KIND_SLOW;
  if (nSpeed > 0)
    return PORT_KIND_SWIFT;
  if (nDamage > 0)
    return PORT_KIND_MIGHT;
  if (nDamage < 0)
    return PORT_KIND_WEAK;
  if (nTaken > 0)
    return PORT_KIND_FRAIL;
  if (nTaken < 0)
    return PORT_KIND_TOUGH;
  return PORT_KIND_MIGHT;
}

int PortReadoutKindOf(const char *pszName) {
  if (pszName == nullptr)
    return -1;
  for (int i = 0; i < PORT_READOUT_KINDS; ++i)
    if (strcmp(pszName, kKinds[i].pszName) == 0)
      return i;
  return -1;
}

int PortReadoutKindPriority(int nKind) {
  for (int i = 0; i < PORT_READOUT_KINDS; ++i)
    if (kanRanking[i] == nKind)
      return 100 - i * 5;
  return 0;
}

const signed char *PortReadoutGlyph(int nKind) {
  return nKind >= 0 && nKind < PORT_READOUT_KINDS ? kapGlyphs[nKind] : kapGlyphs[PORT_KIND_MIGHT];
}

bool PortReadoutCheck(const std::string &text, PortReadoutFile *pFile, std::vector<std::string> *pWarnings,
                      std::string *pWhy) {
  *pFile = PortReadoutFile();
  PortJson j;
  std::string why;
  if (!PortJsonParse(text.c_str(), &j, &why)) {
    if (pWhy != nullptr)
      *pWhy = "readout.json does not read: " + why;
    return false;
  }
  const PortJson &o = j["readout"];
  if (o.kind != PortJson::OBJECT) {
    if (pWhy != nullptr)
      *pWhy = "readout.json holds one object, \"readout\"";
    return false;
  }
  Reader r;
  r.pszText = text.c_str();
  r.pWarnings = pWarnings;
  r.Only(o, {"from", "in_huds", "parts", "add", "ranks", "status_look", "fx"}, "");
  // "from": the template the mod starts from. Classic's panel is the only one.
  const PortJson &from = o["from"];
  if (from.kind != PortJson::NONE && from.Str("") != "classic")
    r.Say(from, "from is classic, the only template there is");
  pFile->bOn = true;
  r.Flag(o, "in_huds", &pFile->bInHuds, "");
  if (o["ranks"].kind != PortJson::NONE)
    ReadRanks(r, o["ranks"], &pFile->ranks);
  if (o["status_look"].kind != PortJson::NONE)
    ReadStatusLook(r, o["status_look"], &pFile->statusLook);
  if (o["parts"].kind != PortJson::NONE)
    ReadParts(r, o["parts"], &pFile->parts);
  if (o["add"].kind != PortJson::NONE) {
    if (o["add"].kind == PortJson::OBJECT)
      ReadAdd(r, o["add"], &pFile->add);
    else
      r.Say(o["add"], "add is an object");
  }
  const PortJson &fx = o["fx"];
  if (fx.kind == PortJson::ARRAY) {
    for (size_t i = 0; i < fx.size(); ++i) {
      PortReadoutFx one;
      if (fx.At(i).kind != PortJson::OBJECT) {
        r.Say(fx.At(i), "fx holds an object for each effect");
        continue;
      }
      if (ReadFx(r, fx.At(i), pFile->ranks, &one))
        pFile->fx.push_back(one);
    }
  } else if (fx.kind != PortJson::NONE) {
    r.Say(fx, "fx is a list of effects");
  }
  return true;
}

void PortReadoutLoad(void) {
  g_file = PortReadoutFile();
  g_nRoomW = PORT_READOUT_W;
  g_nRoom = PORT_READOUT_H;
  // the game's mode first, since a mode plays beside the content mod and its readout is the
  // game's own; then the active mod's. Each folder is the mod's own with its "data" after it,
  // as every other reader of a mod's files has it.
  std::string text;
  const std::string mode = PortModeFolderOfGame();
  if (mode.empty() || !PortReadFile(mode + "readout.json", &text)) {
    const std::vector<PortMod> &mods = PortMods();
    const int nActive = PortModActive();
    if (nActive < 0 || nActive >= static_cast<int>(mods.size()))
      return;
    const std::string folder = mods[nActive].folder + mods[nActive].data;
    if (folder.empty() || !PortReadFile(folder + "readout.json", &text))
      return;
  }
  std::string why;
  if (!PortReadoutCheck(text, &g_file, nullptr, &why)) {
    Log("readout: %s", why.c_str());
    g_file = PortReadoutFile();
  }
}

const PortReadoutFile *PortReadoutOf(void) { return g_file.bOn ? &g_file : nullptr; }

void PortReadoutRoomSet(int nWidth, int nHeight) {
  g_nRoomW = nWidth < PORT_READOUT_W ? PORT_READOUT_W : nWidth;
  g_nRoom = nHeight < PORT_READOUT_H ? PORT_READOUT_H : nHeight;
}

void PortReadoutRoom(int *pnWidth, int *pnHeight) {
  if (pnWidth != nullptr)
    *pnWidth = g_nRoomW;
  if (pnHeight != nullptr)
    *pnHeight = g_nRoom;
}

bool PortReadoutRoomBox(double *pfX, double *pfY, double *pfW, double *pfH) {
  // the design's own rule (design/60-mod-readout, extraRoom): beside the panel where a slot gives
  // 110 spare columns, since a column narrower than that holds no line worth reading; under it
  // where it gives 18 spare rows instead; and neither for a slot that gives too little, which is
  // the classic console
  const bool bColumn = g_nRoomW - PORT_READOUT_W >= 110;
  if (!bColumn && g_nRoom - PORT_READOUT_H - 4 < 18)
    return false;
  const double x = bColumn ? PORT_READOUT_W + 4.0 : 6.0;
  const double y = bColumn ? 4.0 : PORT_READOUT_H + 4.0;
  const double w = bColumn ? g_nRoomW - PORT_READOUT_W - 8.0 : g_nRoomW - 12.0;
  const double h = bColumn ? g_nRoom - 8.0 : g_nRoom - PORT_READOUT_H - 8.0;
  if (w < 60 || h < 14)
    return false;  // no room to write a line in
  if (pfX != nullptr)
    *pfX = x;
  if (pfY != nullptr)
    *pfY = y;
  if (pfW != nullptr)
    *pfW = w;
  if (pfH != nullptr)
    *pfH = h;
  return true;
}

bool PortReadoutRoomLists(void) {
  return g_file.bOn && g_file.add.bLines && g_file.add.bLinePerStatus &&
         PortReadoutRoomBox(nullptr, nullptr, nullptr, nullptr);
}

bool PortReadoutReduceMotion(void) {
  double d = 0;
  return PortSettingNumber("hyperion", "reduceMotion", &d) && d != 0;
}

void PortReadoutForGame(void) {
  g_runs.clear();
  g_fClock = 0;
  g_fTook = -1e9;
  g_strRow.clear();
  g_strTook.clear();
  g_bTookKill = false;
  g_dwUnit = 0;
  g_view = PortReadoutView();
  g_said.clear();
  g_dwViewUnit = 0;
  g_dwViewMark = 0;
  g_bViewAsked = false;
  g_keysSaid.clear();
  for (int i = 0; i < PORT_READOUT_PARTS; ++i)
    g_astrWords[i].clear();
  g_nRowAt = 0;
  g_nRowOf = 0;
}

void PortReadoutFrame(void) {
  if (!g_file.bOn)
    return;
  g_fClock += 1.0 / kFps;
  // a run that has played out and held for as long as it asked
  size_t n = 0;
  for (size_t i = 0; i < g_runs.size(); ++i)
    if (g_fClock - g_runs[i].fAt < g_runs[i].fTime + g_runs[i].fHold)
      g_runs[n++] = g_runs[i];
  g_runs.resize(n);
  Events();
}

namespace {

// The frame's events for the unit the panel describes, from what changed since the last frame.
// Nothing in the game raises these: a presentation-only readout has no business with a hook in
// CUnit::Damage, and watching the one unit the panel shows costs a handful of reads a frame.
// What the statuses on a unit add up to, for each stat the slots may show, each stack counting
// once.
void StatsOf(const std::vector<ModStatusOn> &on, int anOut[PORT_READOUT_STATS]) {
  for (int i = 0; i < PORT_READOUT_STATS; ++i)
    anOut[i] = 0;
  for (const ModStatusOn &s : on) {
    const int nStacks = s.nStacks > 1 ? s.nStacks : 1;
    anOut[PORT_READOUT_STAT_ARMOR] += s.nArmor * nStacks;
    anOut[PORT_READOUT_STAT_DAMAGE] += s.nDamage * nStacks;
    anOut[PORT_READOUT_STAT_COOLDOWN] += s.nCooldown * nStacks;
    anOut[PORT_READOUT_STAT_SPEED] += s.nSpeed * nStacks;
    anOut[PORT_READOUT_STAT_LIFE] += (s.nRegen - s.nDps) * nStacks;
  }
}

void Events() {
  CUnit *p = gpStatUnit;
  if (p == nullptr) {
    g_was = Was();
    return;
  }
  const unsigned dwId = static_cast<unsigned>(p->GetUnitID());
  std::vector<ModStatusOn> on;
  ModStatusOnUnit(p, &on);
  const int nLife = static_cast<int>(p->lHitPoints) >> 8;
  const int nShields = static_cast<int>(p->lShields) >> 8;
  const int nKills = p->bKillCount;
  const int nLifeShown = (static_cast<int>(p->lHitPoints) + 0xFF) >> 8;  // as the panel shows it
  const int nEnergy = p->wEnergy >> 8;
  if (!g_was.bAny || g_was.dwUnit != dwId) {
    g_was = Was();
    g_was.bAny = true;
    g_was.dwUnit = dwId;
    g_was.nLife = nLife;
    g_was.nShields = nShields;
    g_was.nKills = nKills;
    g_was.nLifeShown = nLifeShown;
    g_was.nEnergy = nEnergy;
    for (const ModStatusOn &s : on)
      g_was.statuses.push_back(s.name);
    StatsOf(on, g_was.anStat);  // what it already carries is not a change
    PortReadoutEvent(p, PORT_READOUT_SELECTED, 0);
    return;
  }
  const int nTook = (g_was.nLife - nLife) + (g_was.nShields - nShields);
  if (nTook > 0)
    PortReadoutEvent(p, PORT_READOUT_DAMAGED, nTook);
  else if (nTook < 0)
    PortReadoutEvent(p, PORT_READOUT_HEALED, -nTook);
  if (nKills > g_was.nKills) {
    PortReadoutEvent(p, PORT_READOUT_KILL, nKills - g_was.nKills);
    TookTheLine(std::string(), true);
  }
  for (const ModStatusOn &s : on) {
    bool bWas = false;
    for (const std::string &name : g_was.statuses)
      bWas = bWas || name == s.name;
    if (!bWas) {
      TookTheLine(s.name, false);  // first, so the effects know which status it was
      PortReadoutEvent(p, PORT_READOUT_STATUS_ADDED, 0);
    }
  }
  // a stat a status moved: one event for each stat that is not what it was. nAmount is the stat,
  // so an effect on "slots.{stat}" lights that icon alone.
  int anStat[PORT_READOUT_STATS];
  StatsOf(on, anStat);
  for (int i = 0; i < PORT_READOUT_STATS; ++i)
    if (anStat[i] != g_was.anStat[i])
      PortReadoutEvent(p, PORT_READOUT_STAT_CHANGED, i);
  g_was.nLife = nLife;
  g_was.nShields = nShields;
  g_was.nKills = nKills;
  g_was.nLifeShown = nLifeShown;
  g_was.nEnergy = nEnergy;
  g_was.statuses.clear();
  for (const ModStatusOn &s : on)
    g_was.statuses.push_back(s.name);
  for (int i = 0; i < PORT_READOUT_STATS; ++i)
    g_was.anStat[i] = anStat[i];
}

// A run put on the list, the oldest dropped when there is no room.
void Start(const Run &run) {
  if (g_runs.size() >= kRunsMost)
    g_runs.erase(g_runs.begin());
  g_runs.push_back(run);
}

// The op that runs for an effect under the player's Reduce motion: its own, or, where that
// leaves a motion op out, the light the same effect carries beside the movement -- a glow, or the
// glint of a light -- so an effect written with both still says what it was for. -1 for an op
// Reduce motion leaves out altogether. A flash runs either way, at half the brightness (the
// drawing halves it).
int Allowed(int nOp, bool bGlow, bool bGlint) {
  if (!PortReadoutReduceMotion() || !PortReadoutOpMoves(nOp))
    return nOp;
  if (bGlow)
    return PORT_READOUT_GLOW;
  if (bGlint)
    return PORT_READOUT_LIGHT;
  return -1;
}

// The part an effect runs on: its own, or, for one written on the lines, the row a status's line
// is drawn on now -- the kills' row where a mod adds lines and the status's own row where it does
// not. In a HUD's room the rows are drawn from the same runs, so one effect reaches both.
int LinesPart(const PortReadoutFx &fx) {
  if (!fx.bLines)
    return fx.nPart;
  return g_file.add.bLines ? PORT_READOUT_KILLS : PORT_READOUT_STATUS;
}

}  // namespace

void PortReadoutEvent(const CUnit *pUnit, int nEvent, int nAmount) {
  if (!g_file.bOn || pUnit == nullptr || nEvent < 0 || nEvent >= PORT_READOUT_EVENTS)
    return;
  const unsigned dwId = static_cast<unsigned>(const_cast<CUnit *>(pUnit)->GetUnitID());
  if (nEvent == PORT_READOUT_SELECTED && dwId != g_dwUnit) {
    // another unit: its own effects start clean
    g_runs.clear();
    g_dwUnit = dwId;
  }
  if (dwId != g_dwUnit)
    return;
  const PortReadoutView *pView = PortReadoutUnit(pUnit);
  for (size_t i = 0; i < g_file.fx.size(); ++i) {
    const PortReadoutFx &fx = g_file.fx[i];
    if (!fx.abOn[nEvent])
      continue;
    // "if": "rank" draws it only for a unit the hook gave a rank the file names
    if (fx.ifRank == "rank" && (pView == nullptr || pView->nRankAt < 0))
      continue;
    const int nOp = Allowed(fx.nOp, fx.bGlow, fx.bGlint);
    if (nOp < 0)
      continue;
    Run run;
    run.nFx = static_cast<int>(i);
    run.nOp = nOp;
    run.nPart = LinesPart(fx);
    run.fAt = g_fClock;
    run.fTime = fx.fTime;
    run.fHold = fx.fHold;
    run.nAmount = nAmount;
    run.text = fx.text;
    run.colour = fx.colour;
    run.fRate = fx.fRate;
    run.fEvery = fx.fEvery;
    run.fSize = fx.fSize;
    run.fLight = fx.fLight;
    run.bGlint = fx.bGlint;
    run.bGlow = fx.bGlow;
    run.bFromRight = fx.bFromRight;
    // "slots.{stat}" is the icon of the stat the event names, which nAmount carries for a stat
    // that moved; any other name is a stat of its own
    if (fx.slot == "{stat}")
      run.slot = nEvent == PORT_READOUT_STAT_CHANGED ? PortReadoutStatName(nAmount) : "";
    else
      run.slot = fx.slot;
    if (nEvent == PORT_READOUT_STATUS_ADDED)
      run.status = g_strTook;  // the status that landed, which took the line in the same frame
    if (run.nOp == PORT_READOUT_ROLL && g_was.bAny && g_was.dwUnit == dwId) {
      // a roll counts the line from what the panel showed before this frame's change; the events
      // are raised before the frame's figures are kept, so those are still the frame before's
      run.nFromLife = g_was.nLifeShown;
      run.nFromShields = g_was.nShields;
      run.nFromEnergy = g_was.nEnergy;
    }
    Start(run);
  }
}

bool PortReadoutScriptFx(const CUnit *pUnit, const char *pszOp, const PortJson &args, std::string *pWhy) {
  if (pUnit == nullptr || pszOp == nullptr)
    return false;
  int nOp = -1;
  for (int i = 0; i < PORT_READOUT_OPS; ++i)
    if (strcmp(pszOp, kOps[i]) == 0)
      nOp = i;
  if (nOp < 0) {
    if (pWhy != nullptr)
      *pWhy = std::string("hx.readout.fx: no effect is called ") + pszOp;
    return false;
  }
  std::string name = args["part"].Str("wire");
  if (name.compare(0, 6, "slots.") == 0)
    name = "slots";
  const int nPart = PortReadoutPartOf(name.c_str());
  if (nPart < 0) {
    if (pWhy != nullptr)
      *pWhy = "hx.readout.fx: no part is called " + args["part"].Str("");
    return false;
  }
  const bool bGlow = args["glow"].kind == PortJson::BOOL && args["glow"].b;
  const bool bGlint = args["glint"].kind == PortJson::BOOL && args["glint"].b;
  const int nRan = Allowed(nOp, bGlow, bGlint);
  if (static_cast<unsigned>(const_cast<CUnit *>(pUnit)->GetUnitID()) != g_dwUnit || nRan < 0)
    return true;  // not the unit the readout describes, or an op the player turned off
  Run run;
  run.nOp = nRan;
  run.nPart = nPart;
  run.fAt = g_fClock;
  run.fTime = args["time"].Num(0.25);
  if (run.fTime < 0.05)
    run.fTime = 0.05;
  if (run.fTime > 4.0)
    run.fTime = 4.0;
  run.fHold = args["hold"].Num(0);
  run.text = args["text"].Str("");
  run.fRate = args["rate"].Num(1.0);
  run.fEvery = args["every"].Num(0);
  run.fSize = args["size"].Num(0);
  run.fLight = args["light"].Num(0.5);
  run.bGlint = bGlint;
  run.bGlow = bGlow;
  run.bFromRight = args["from"].Str("right") != "left";
  if (args["part"].Str("").compare(0, 6, "slots.") == 0)
    run.slot = args["part"].Str("").substr(6);
  run.status = args["status"].Str("");
  if (run.nOp == PORT_READOUT_ROLL) {
    // a script's roll counts from where the line stands as it is called, so a script that sets
    // it off before it changes something has the line count the change
    CUnit *p = const_cast<CUnit *>(pUnit);
    run.nFromLife = (static_cast<int>(p->lHitPoints) + 0xFF) >> 8;
    run.nFromShields = static_cast<int>(p->lShields) >> 8;
    run.nFromEnergy = p->wEnergy >> 8;
  }
  const std::string colour = args["colour"].Str("");
  unsigned int rgb = 0;
  if (colour == "rank") {
    run.colour.nInk = PORT_READOUT_INK_RANK;
  } else if (colour == "team") {
    run.colour.nInk = PORT_READOUT_INK_TEAM;
  } else if (colour.size() == 7 && colour[0] == '#' && sscanf(colour.c_str() + 1, "%6x", &rgb) == 1) {
    run.colour.nInk = PORT_READOUT_INK_RGB;
    run.colour.nRgb = rgb;
  }
  Start(run);
  return true;
}

// ---- the hook ---------------------------------------------------------------------------------------
// readout(unit) is called in the presentation VM (port_modlua.h), and what it gives back is kept
// until the selection or the unit's own values change. The statuses a line names are read from
// the game each time the view is asked for, so a status counting down costs no call.
namespace {

// The values a new call is made for: the unit's life, shields, energy, kills and type, and the
// statuses on it with their stacks. A readout that shows something else of the unit still
// follows it, since a hit or a status moves one of these.
unsigned MarkOf(CUnit *pUnit, const std::vector<ModStatusOn> &on) {
  unsigned dw = static_cast<unsigned>(pUnit->lHitPoints);
  dw = dw * 31 + static_cast<unsigned>(pUnit->lShields);
  dw = dw * 31 + static_cast<unsigned>(pUnit->wEnergy);
  dw = dw * 31 + static_cast<unsigned>(pUnit->bKillCount);
  dw = dw * 31 + static_cast<unsigned>(pUnit->wType);
  for (const ModStatusOn &s : on) {
    for (size_t i = 0; i < s.name.size(); ++i)
      dw = dw * 31 + static_cast<unsigned char>(s.name[i]);
    dw = dw * 31 + static_cast<unsigned>(s.nStacks);
  }
  return dw;
}

// A rank's place in the file's own order, -1 for one it does not name.
int RankAt(const std::string &id) {
  for (size_t i = 0; i < g_file.ranks.size(); ++i)
    if (g_file.ranks[i].id == id)
      return static_cast<int>(i);
  return -1;
}

// What a status does, in words, from the fields its record gives: the readout's third line and
// the tooltips both read it, so one status reads the same wherever it shows.
std::string EffectWords(const ModStatusOn &s) {
  struct Field {
    int nValue;
    const char *pszWord;
    const char *pszUnit;
  };
  const int nStacks = s.nStacks > 1 ? s.nStacks : 1;
  const Field aFields[] = {{s.nArmor * nStacks, "armour", ""},
                           {s.nSpeed * nStacks, "speed", "%"},
                           {s.nCooldown * nStacks, "cooldown", "%"},
                           {s.nDamage * nStacks, "damage", "%"},
                           {s.nTaken * nStacks, "damage taken", "%"},
                           {s.nRegen * nStacks / 256, "life/s", ""},
                           {-s.nDps * nStacks / 256, "life/s", ""}};
  std::string out;
  for (const Field &f : aFields) {
    if (f.nValue == 0)
      continue;
    char sz[64];
    snprintf(sz, sizeof sz, "%s%d%s %s", f.nValue > 0 ? "+" : "-", f.nValue > 0 ? f.nValue : -f.nValue,
             f.pszUnit, f.pszWord);
    out += (out.empty() ? "" : ", ") + std::string(sz);
  }
  if (s.bNoMove)
    out += (out.empty() ? "" : ", ") + std::string("cannot move");
  if (s.bNoAttack)
    out += (out.empty() ? "" : ", ") + std::string("cannot attack");
  return out;
}

// A status's kind: the one its "look" names, else the first its fields match (PortReadoutKindFrom,
// the rule itself, which a tool showing a definition with no game asks as well).
int KindOf(const ModStatusOn &s) {
  return PortReadoutKindFrom(s.nKind, s.bNoMove, s.bNoAttack, s.nDps, s.nRegen, s.nArmor, s.nCooldown, s.nSpeed,
                             s.nDamage, s.nTaken);
}

// A key of the hook's answer that nothing reads, said once a game with where it was. A mod's own
// readout.json refuses an unknown key loudly as it is read (Reader::Only); the hook's answer is
// the half that was silent, and a key that does nothing looked exactly like one that works. The
// answer is read only when the unit or its values change, so this walk costs a few members beside
// the Luau call that has just run.
void SayUnknown(const PortJson &o, const char *const *ppszKnown, int nKnown, const char *pszWhere) {
  if (o.kind != PortJson::OBJECT)
    return;
  for (size_t i = 0; i < o.members.size(); ++i) {
    const std::string &key = o.members[i].first;
    bool bKnown = false;
    for (int k = 0; k < nKnown && !bKnown; ++k)
      bKnown = key == ppszKnown[k];
    if (bKnown)
      continue;
    const std::string said = std::string(pszWhere) + "." + key;
    bool bAlready = false;
    for (size_t k = 0; k < g_keysSaid.size() && !bAlready; ++k)
      bAlready = g_keysSaid[k] == said;
    if (bAlready)
      continue;
    g_keysSaid.push_back(said);
    Log("readout: %s is read by nothing, so it does nothing", said.c_str());
  }
}

// A line of the hook's "lines": a line of text, or an object with its own text or a status's name.
void ReadLine(const PortJson &v, PortReadoutLine *pLine) {
  if (v.kind == PortJson::STRING) {
    pLine->text = v.s;
    return;
  }
  if (v.kind != PortJson::OBJECT)
    return;
  static const char *const kKeys[] = {"text", "colour", "status"};
  SayUnknown(v, kKeys, 3, "readout(unit).lines");
  pLine->text = v["text"].Str("");
  pLine->status = v["status"].Str("");
  const std::string colour = v["colour"].Str("");
  unsigned int rgb = 0;
  if (colour == "rank")
    pLine->colour.nInk = PORT_READOUT_INK_RANK;
  else if (colour == "team")
    pLine->colour.nInk = PORT_READOUT_INK_TEAM;
  else if (colour.size() == 7 && colour[0] == '#' && sscanf(colour.c_str() + 1, "%6x", &rgb) == 1) {
    pLine->colour.nInk = PORT_READOUT_INK_RGB;
    pLine->colour.nRgb = rgb;
  }
}

// The lines the hook gave, each one that names a status given its name, its side, its time and
// its stacks from the status's own definition and the unit's state as it is now. A line naming a
// status the unit no longer carries is left out, so a readout cannot show one that has run out,
// and the hook's own lines are kept whole so the line comes back if the status does.
void FillStatuses(const std::vector<ModStatusOn> &on) {
  g_view.lines.clear();
  for (const PortReadoutLine &said : g_said) {
    PortReadoutLine line = said;
    if (line.status.empty()) {
      g_view.lines.push_back(line);
      continue;
    }
    const ModStatusOn *pOn = nullptr;
    for (const ModStatusOn &s : on)
      if (s.name == line.status)
        pOn = &s;
    if (pOn == nullptr)
      continue;
    line.label = pOn->label;
    line.bGood = pOn->bGood;
    line.fLeft = pOn->nFramesLeft / kFps;
    line.fOf = pOn->nFrames / kFps;
    line.nStacks = pOn->nStacks;
    line.nKind = KindOf(*pOn);
    line.nHue = pOn->bHue ? pOn->nHue : PortReadoutKindHue(line.nKind);
    line.bHue = pOn->bHue;
    // a status's own icon, then its look's, else the classic one its kind stands for
    line.nIcon = pOn->nIcon >= 0 ? pOn->nIcon : PortReadoutKindIcon(line.nKind);
    line.effect = EffectWords(*pOn);
    line.nPriority = pOn->nPriority >= 0 ? pOn->nPriority : PortReadoutKindPriority(line.nKind);
    line.bShowWire = pOn->bShowWire;
    line.bShowGroup = pOn->bShowGroup;
    if (!pOn->bShowReadout)
      continue;  // a status its look keeps off the readout, as a born trait is
    g_view.lines.push_back(line);
  }
}

}  // namespace

const PortReadoutView *PortReadoutUnit(const CUnit *pUnit) {
  if (!g_file.bOn || pUnit == nullptr || !ModLuaHasReadout())
    return nullptr;
  CUnit *p = const_cast<CUnit *>(pUnit);
  std::vector<ModStatusOn> on;
  ModStatusOnUnit(p, &on);
  const unsigned dwId = static_cast<unsigned>(p->GetUnitID());
  const unsigned dwMark = MarkOf(p, on);
  if (dwId != g_dwViewUnit || dwMark != g_dwViewMark || !g_bViewAsked) {
    g_dwViewUnit = dwId;
    g_dwViewMark = dwMark;
    g_bViewAsked = true;
    g_view = PortReadoutView();
    g_said.clear();
    PortJson j;
    std::string why;
    if (ModLuaReadout(p, &j, &why)) {
      g_view.bOn = true;
      // what the answer is read for, and the stats the two tables by stat are read for: a name
      // outside the six is read by nothing, which is how a tooltip keyed "armour" would be
      // accepted and never shown
      static const char *const kKeys[] = {"name", "rank", "lines", "deltas", "tooltips"};
      SayUnknown(j, kKeys, 5, "readout(unit)");
      SayUnknown(j["deltas"], kStats, PORT_READOUT_STATS, "readout(unit).deltas");
      SayUnknown(j["tooltips"], kStats, PORT_READOUT_STATS, "readout(unit).tooltips");
      g_view.name = j["name"].Str("");
      g_view.rank = j["rank"].Str("");
      g_view.nRankAt = RankAt(g_view.rank);
      if (g_view.nRankAt >= 0) {
        g_view.rankName = g_file.ranks[g_view.nRankAt].name;
        g_view.nRankRgb = g_file.ranks[g_view.nRankAt].nRgb;
      } else {
        g_view.rankName = g_view.rank;
      }
      const PortJson &lines = j["lines"];
      for (size_t i = 0; i < lines.size(); ++i) {
        PortReadoutLine line;
        ReadLine(lines.At(i), &line);
        if (!line.text.empty() || !line.status.empty())
          g_said.push_back(line);
      }
      const PortJson &deltas = j["deltas"];
      for (int s = 0; s < PORT_READOUT_STATS; ++s)
        if (deltas[kStats[s]].kind == PortJson::NUMBER) {
          g_view.abDelta[s] = true;
          g_view.anDelta[s] = static_cast<int>(deltas[kStats[s]].d);
        }
      const PortJson &tips = j["tooltips"];
      for (size_t i = 0; i < tips.members.size(); ++i) {
        PortReadoutTip tip;
        tip.stat = tips.members[i].first;
        const PortJson &v = tips.members[i].second;
        if (v.kind == PortJson::STRING)
          tip.lines.assign(1, v.s);
        else
          for (size_t k = 0; k < v.size(); ++k)
            if (v.At(k).kind == PortJson::STRING)
              tip.lines.push_back(v.At(k).s);
        g_view.tips.push_back(tip);
      }
    } else if (!why.empty()) {
      Log("readout: %s", why.c_str());
    }
  }
  if (!g_view.bOn)
    return nullptr;
  FillStatuses(on);
  return &g_view;
}

namespace {

// A pattern's braces filled in: {stat}, {value}, {title} and the rest, from the pairs given. A
// brace naming nothing is left as it was written, so a mod sees its own text and not a hole.
std::string Fill(const std::string &pattern, const std::vector<std::pair<std::string, std::string>> &values) {
  std::string out;
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] != '{') {
      out += pattern[i];
      continue;
    }
    const size_t nEnd = pattern.find('}', i);
    if (nEnd == std::string::npos) {
      out += pattern[i];
      continue;
    }
    const std::string key = pattern.substr(i + 1, nEnd - i - 1);
    bool bFound = false;
    for (const std::pair<std::string, std::string> &v : values)
      if (v.first == key) {
        out += v.second;
        bFound = true;
        break;
      }
    if (!bFound)
      out += pattern.substr(i, nEnd - i + 1);
    i = nEnd;
  }
  return out;
}

// Which stat one of the panel's four icons carries now. The panel fills its icons in
// statdraw_draw_stats's order, leaving out what the unit has not got, so the stat an icon shows
// depends on the unit: armour, then shields, then the ground and air weapons, then the special
// one (interceptors, mines, a nuke), which no stat of the readout's names.
// What a rolled change says in words: the stat, how far it moved and which way that reads. The
// three stats with no icon of their own are read here and nowhere else on the classic console.
std::string DeltaWords(int nStat, int nBy) {
  char szBy[32] = "";
  snprintf(szBy, sizeof szBy, "%s%d%s", nBy > 0 ? "+" : "", nBy,
           PortReadoutStatIsCount(nStat) ? "" : "%");
  const std::string stat = PortText(PortReadoutStatWords(nStat));
  // whole lines, not a line and a word: the cooldown is the one stat where the number falling is
  // the good news, so it says which way it reads rather than leaving the sign to be worked out
  if (PortReadoutStatLowerIsBetter(nStat))
    return nBy < 0 ? PortTextF("{0}: {1} on its base, a faster attack", {stat, szBy})
                   : PortTextF("{0}: {1} on its base, a slower attack", {stat, szBy});
  return PortTextF("{0}: {1} on its base", {stat, szBy});
}

// Which weapon one of the four icons carries, WEAPON_NONE for an icon that is not a weapon's:
// statdraw fills them in this order, the ground's and then the air's where the two differ, so
// the icon that carries the damage carries that weapon's cooldown as well.
int WeaponOfSlot(CUnit *pUnit, int nSlot) {
  int anWeapons[PORT_READOUT_SLOTS_MOST];
  int n = 0;
  for (int i = 0; i < PORT_READOUT_SLOTS_MOST; ++i)
    anWeapons[i] = WEAPON_NONE;
  if (gubUnitArmorType[pUnit->GetTypeEx()] != 0x3C && n < PORT_READOUT_SLOTS_MOST)
    ++n;
  if (gbUnitHasShields[pUnit->wType] && n < PORT_READOUT_SLOTS_MOST)
    ++n;
  if (pUnit->GetWeaponGnd() != WEAPON_NONE && n < PORT_READOUT_SLOTS_MOST)
    anWeapons[n++] = pUnit->GetWeaponGnd();
  if (pUnit->GetWeaponAir() != WEAPON_NONE && pUnit->GetWeaponAir() != pUnit->GetWeaponGnd() &&
      n < PORT_READOUT_SLOTS_MOST)
    anWeapons[n++] = pUnit->GetWeaponAir();
  return nSlot >= 0 && nSlot < PORT_READOUT_SLOTS_MOST ? anWeapons[nSlot] : WEAPON_NONE;
}

int StatOfSlot(CUnit *pUnit, int nSlot) {
  int anStats[PORT_READOUT_SLOTS_MOST];
  int n = 0;
  for (int i = 0; i < PORT_READOUT_SLOTS_MOST; ++i)
    anStats[i] = -1;
  if (gubUnitArmorType[pUnit->GetTypeEx()] != 0x3C && n < PORT_READOUT_SLOTS_MOST)
    anStats[n++] = PORT_READOUT_STAT_ARMOR;
  if (gbUnitHasShields[pUnit->wType] && n < PORT_READOUT_SLOTS_MOST)
    anStats[n++] = PORT_READOUT_STAT_SHIELDS;
  if (pUnit->GetWeaponGnd() != WEAPON_NONE && n < PORT_READOUT_SLOTS_MOST)
    anStats[n++] = PORT_READOUT_STAT_DAMAGE;
  if (pUnit->GetWeaponAir() != WEAPON_NONE && pUnit->GetWeaponAir() != pUnit->GetWeaponGnd() &&
      n < PORT_READOUT_SLOTS_MOST)
    anStats[n++] = PORT_READOUT_STAT_DAMAGE;
  return nSlot >= 0 && nSlot < PORT_READOUT_SLOTS_MOST ? anStats[nSlot] : -1;
}

}  // namespace

bool PortReadoutSlotTip(const CUnit *pUnit, int nSlot, std::string *pOut, bool *pbWhole) {
  if (pOut != nullptr)
    pOut->clear();
  if (pbWhole != nullptr)
    *pbWhole = false;
  if (!g_file.bOn || pUnit == nullptr || nSlot < 0 || nSlot >= PORT_READOUT_SLOTS_MOST)
    return false;
  const PortReadoutView *pView = PortReadoutUnit(pUnit);
  if (pView == nullptr)
    return false;
  const int nStat = StatOfSlot(const_cast<CUnit *>(pUnit), nSlot);
  const char *pszStat = nStat >= 0 ? PortReadoutStatName(nStat) : "";
  std::vector<std::string> lines;

  // the hook's own lines for this stat
  for (const PortReadoutTip &tip : pView->tips)
    if (nStat >= 0 && tip.stat == pszStat)
      for (const std::string &s : tip.lines)
        lines.push_back(s);

  // the rolled change, where the file asks for it in the tooltip rather than on the icon
  if (g_file.parts.nDelta == PORT_READOUT_DELTA_TIP && nStat >= 0 && pView->abDelta[nStat])
    lines.push_back(DeltaWords(nStat, pView->anDelta[nStat]));

  // the attack's cooldown, on the icon that carries that weapon's damage: the panel has four
  // icons and none of them is the cooldown's, so this is where it is read
  const int nWeapon = WeaponOfSlot(const_cast<CUnit *>(pUnit), nSlot);
  if (nStat == PORT_READOUT_STAT_DAMAGE && nWeapon != WEAPON_NONE) {
    const int nFrames = gnWeapCooldown[nWeapon];
    const int nBy = pView->abDelta[PORT_READOUT_STAT_COOLDOWN]
                        ? pView->anDelta[PORT_READOUT_STAT_COOLDOWN]
                        : 0;
    char sz[128] = "";
    if (nBy != 0 && 100 + nBy > 0)
      // the row it was rolled from, which the change is a part of
      snprintf(sz, sizeof sz, "%s", PortTextF("Cooldown: {0} frames ({1} base)",
                                              {std::to_string(nFrames),
                                               std::to_string(nFrames * 100 / (100 + nBy))})
                                        .c_str());
    else
      snprintf(sz, sizeof sz, "%s",
               PortTextF("Cooldown: {0} frames", {std::to_string(nFrames)}).c_str());
    lines.push_back(sz);
    if (nBy != 0)
      lines.push_back(DeltaWords(PORT_READOUT_STAT_COOLDOWN, nBy));
    for (const PortReadoutTip &tip : pView->tips)
      if (tip.stat == PortReadoutStatName(PORT_READOUT_STAT_COOLDOWN))
        for (const std::string &s : tip.lines)
          lines.push_back(s);
  }

  // what the statuses on the unit do to this stat
  for (const PortReadoutLine &line : pView->lines) {
    if (line.status.empty() || line.effect.empty())
      continue;
    // the weapon's icon answers for the cooldown too, since that stat has no icon of its own
    const bool bMine = nStat == PORT_READOUT_STAT_ARMOR ? line.nKind == PORT_KIND_ARMOR_UP ||
                                                              line.nKind == PORT_KIND_ARMOR_DOWN
                       : nStat == PORT_READOUT_STAT_DAMAGE
                           ? line.nKind == PORT_KIND_MIGHT || line.nKind == PORT_KIND_WEAK ||
                                 line.nKind == PORT_KIND_HASTE || line.nKind == PORT_KIND_JAM
                           : nStat == PORT_READOUT_STAT_COOLDOWN
                                 ? line.nKind == PORT_KIND_HASTE || line.nKind == PORT_KIND_JAM
                                 : false;
    if (bMine)
      lines.push_back(line.label + ": " + line.effect);
  }

  // the file's own patterns, which replace classic's words rather than adding to them
  if (!g_file.parts.tooltip.empty()) {
    std::vector<std::pair<std::string, std::string>> values;
    values.push_back(std::make_pair("stat", pszStat));
    values.push_back(std::make_pair("title", pView->name));
    values.push_back(std::make_pair("name", pView->name));
    values.push_back(std::make_pair("rank", pView->rankName));
    char szValue[32] = "";
    if (nStat >= 0 && pView->abDelta[nStat])
      snprintf(szValue, sizeof szValue, "%d", pView->anDelta[nStat]);
    values.push_back(std::make_pair("value", szValue));
    std::vector<std::string> whole;
    for (const std::string &pattern : g_file.parts.tooltip)
      whole.push_back(Fill(pattern, values));
    for (const std::string &s : lines)
      whole.push_back(s);
    lines = whole;
    if (pbWhole != nullptr)
      *pbWhole = true;
  }
  if (lines.empty())
    return false;
  if (pOut != nullptr)
    for (size_t i = 0; i < lines.size(); ++i)
      *pOut += (i ? "\n" : "") + lines[i];
  return true;
}

bool PortReadoutDraws(void) {
  // one gate for both consoles: a design's selection slot makes a surface for the readout and
  // places classic's panel at the slot's edge by this, and the classic console's overlay draws by
  // it, so no build can have the readout on one console and not the other.
  if (!g_file.bOn || gpStatUnit == nullptr || byte_6AB4C0 != 1)
    return false;  // no readout, nothing selected, or a selection of several units
  if (PortConsoleHudOn() && !g_file.bInHuds)
    return false;  // the mod keeps its readout to the classic console
  const PortReadoutView *pView = PortReadoutUnit(gpStatUnit);
  return pView != nullptr && pView->bOn;
}

// ---- classic's controls, in the mod's words ------------------------------------------------------
namespace {

// Classic's colour codes (gds_font.cpp, font_set_color_tbl) and the font palette each one takes.
// A row's second entry is the glyph's own colour and its first the shadow under it, so a code
// reads as the colour of its row's second entry. Classic writes these codes into its own text
// (statdraw_draw_hp's damage states), which is why a part's colour goes in as one of them.
const struct {
  unsigned char bCode;
  int nRow;
} kCodes[] = {{0x02, 0},  {0x03, 1},  {0x04, 2},  {0x05, 3},  {0x06, 4},  {0x07, 5},  {0x08, 8},
              {0x0E, 9},  {0x0F, 10}, {0x10, 11}, {0x11, 12}, {0x15, 13}, {0x16, 14}, {0x17, 15},
              {0x18, 16}, {0x19, 17}, {0x1B, 18}, {0x1C, 19}, {0x1D, 20}, {0x1E, 21}, {0x1F, 23}};

// The code whose colour is nearest nRgb. The weights are the eye's, as the palette's own finder
// has them, so a gold lands on gold rather than on the nearest number.
unsigned char CodeNearest(unsigned int nRgb) {
  unsigned char abPal[768];
  PortPresentPaletteRGB(abPal);
  const int nR = static_cast<int>((nRgb >> 16) & 0xFF), nG = static_cast<int>((nRgb >> 8) & 0xFF),
            nB = static_cast<int>(nRgb & 0xFF);
  unsigned char bBest = 0;
  long lBest = 0;
  for (size_t i = 0; i < sizeof kCodes / sizeof kCodes[0]; ++i) {
    const int nEntry = fontpalette_g[kCodes[i].nRow][1];
    const long dr = abPal[nEntry * 3 + 0] - nR, dg = abPal[nEntry * 3 + 1] - nG,
               db = abPal[nEntry * 3 + 2] - nB;
    const long l = dr * dr * 3 + dg * dg * 6 + db * db;
    if (bBest == 0 || l < lBest) {
      lBest = l;
      bBest = kCodes[i].bCode;
    }
  }
  return bBest;
}

// The code a part's colour comes to, 0 for classic's own ink, which the control keeps. The
// damage state is classic's own three codes, so a part that follows the life is drawn as
// classic draws it.
unsigned char CodeOf(const PortReadoutColour &c, const PortReadoutView &view, CUnit *pUnit) {
  switch (c.nInk) {
  case PORT_READOUT_INK_RGB:
    return CodeNearest(c.nRgb);
  case PORT_READOUT_INK_RANK:
    return view.nRankAt >= 0 ? CodeNearest(view.nRankRgb) : 0;
  case PORT_READOUT_INK_TEAM: {
    unsigned char abRgb[3] = {0xFF, 0xFF, 0xFF};
    if (!PortTeamColorGet(pUnit->bPlayer, abRgb))
      return 0;
    return CodeNearest((static_cast<unsigned>(abRgb[0]) << 16) | (static_cast<unsigned>(abRgb[1]) << 8) |
                       abRgb[2]);
  }
  case PORT_READOUT_INK_LIFE:
  case PORT_READOUT_INK_DAMAGE:
    return pUnit->IsBurning() ? 6 : pUnit->IsOnFire() ? 3 : 7;
  default:
    return 0;
  }
}

// A part's pattern's values: the readout's own words and the unit's figures, the maximums at the
// owner's handicap as the panel's own lines have them.
std::vector<std::pair<std::string, std::string>> PartValues(CUnit *pUnit, const PortReadoutView &view,
                                                            const char *pszClassic) {
  std::vector<std::pair<std::string, std::string>> v;
  char sz[32] = "";
  v.push_back(std::make_pair("name", view.name));
  v.push_back(std::make_pair("rank", view.rankName));
  v.push_back(std::make_pair("title", pszClassic != nullptr ? std::string(pszClassic) : std::string()));
  snprintf(sz, sizeof sz, "%d", pUnit->bKillCount);
  v.push_back(std::make_pair("kills", sz));
  snprintf(sz, sizeof sz, "%d", (static_cast<int>(pUnit->lHitPoints) + 0xFF) >> 8);
  v.push_back(std::make_pair("life", sz));
  snprintf(sz, sizeof sz, "%d", pUnit->GetLife());
  v.push_back(std::make_pair("life_max", sz));
  snprintf(sz, sizeof sz, "%d", static_cast<int>(pUnit->lShields) >> 8);
  v.push_back(std::make_pair("shields", sz));
  snprintf(sz, sizeof sz, "%d", HandicapShields(guwUnitEnergy[pUnit->GetTypeEx()], pUnit->bPlayer));
  v.push_back(std::make_pair("shields_max", sz));
  snprintf(sz, sizeof sz, "%d", pUnit->wEnergy >> 8);
  v.push_back(std::make_pair("energy", sz));
  snprintf(sz, sizeof sz, "%d", pUnit->GetMaxEnergyReal());
  v.push_back(std::make_pair("energy_max", sz));
  snprintf(sz, sizeof sz, "%d", pUnit->ArmorUpgrades());
  v.push_back(std::make_pair("armor", sz));
  v.push_back(std::make_pair("level", sz));
  return v;
}

// One line's words: its own pattern, or a status's own name with its stacks and the time left.
std::string LineWords(const PortReadoutLine &line, const std::string &pattern,
                      const std::vector<std::pair<std::string, std::string>> &values) {
  // the line's own values first and the readout's after them, since a pattern is filled from the
  // first value that names its key: on a status's line {name} is the status, not the unit
  std::vector<std::pair<std::string, std::string>> v;
  char sz[32] = "";
  snprintf(sz, sizeof sz, "%d", line.fLeft > 0 ? static_cast<int>(line.fLeft) + 1 : 0);
  v.push_back(std::make_pair("left", line.fOf > 0 ? std::string(sz) : std::string()));
  snprintf(sz, sizeof sz, "%d", line.nStacks > 1 ? line.nStacks : 1);
  v.push_back(std::make_pair("stacks", sz));
  v.push_back(std::make_pair("effects", line.effect));
  if (!line.label.empty())
    v.push_back(std::make_pair("name", line.label));
  v.insert(v.end(), values.begin(), values.end());
  if (!line.text.empty())
    return Fill(line.text, v);
  if (!pattern.empty())
    return Fill(pattern, v);
  // a status with no words of its own: its name, the stacks on it and the time it has left
  std::string out = line.label;
  if (line.nStacks > 1) {
    snprintf(sz, sizeof sz, " x%d", line.nStacks);
    out += sz;
  }
  if (line.fOf > 0) {
    snprintf(sz, sizeof sz, " %ds", static_cast<int>(line.fLeft) + 1);
    out += sz;
  }
  return out;
}

// The parts as classic has them, to tell a part the mod set from one it left alone: a part left
// alone keeps classic's own drawing, which is not always what its pattern comes to (classic's
// life line leaves out the shields a unit has not got, and colours each half of it).
const PortReadoutParts kBase;

// A line cut to a row of nWide pixels, on a word's boundary and never through one: the panel's
// rows are 144 pixels and classic's small font runs about five to a letter, so a line that names
// a status and everything it does is half as long again as the row. The words that do not fit go
// rather than being cut in the middle of a number. A file's "each_short" is the better answer,
// since the mod chooses what survives; this is what a file without one gets.
std::string Shorten(const std::string &text, int nWide) {
  const double fLetter = 5.0;  // classic's small font, its own average advance
  const size_t nMost = static_cast<size_t>(nWide / fLetter);
  if (nMost < 4 || text.size() <= nMost)
    return text;
  size_t nCut = text.rfind(' ', nMost);
  if (nCut == std::string::npos || nCut < nMost / 2)
    nCut = nMost;  // one long word: it keeps the row rather than losing the line
  while (nCut > 0 && (text[nCut - 1] == ' ' || text[nCut - 1] == ','))
    --nCut;
  return text.substr(0, nCut);
}

// A roll live on a part now: the newest run of it still playing, how far through it is, and what
// the panel showed before it began (nFrom*). False for none, which leaves the line classic's. A
// roll moves no text, so the player's Reduce motion leaves it alone.
bool RollOn(int nPart, double *pfThrough, const Run **ppRun) {
  for (size_t k = 0; k < g_runs.size(); ++k) {
    const Run &run = g_runs[g_runs.size() - 1 - k];
    if (run.nOp != PORT_READOUT_ROLL || run.nPart != nPart || run.nFromLife < 0 || run.fTime <= 0)
      continue;
    const double fAge = g_fClock - run.fAt;
    if (fAge < 0)
      continue;
    *pfThrough = fAge < run.fTime ? fAge / run.fTime : 1.0;
    *ppRun = &run;
    return true;
  }
  return false;
}

// A number counted from one value to its new one, as far as the run has gone: what a roll draws
// instead of letting the line jump. It lands exactly on the new value as the run ends.
int Counted(int nFrom, int nTo, double fThrough) {
  if (nFrom < 0 || fThrough >= 1)
    return nTo;
  const double f = 1 - (1 - fThrough) * (1 - fThrough) * (1 - fThrough);
  return static_cast<int>(nFrom + (nTo - nFrom) * f + 0.5);
}

// Which of several lines shows now: each in turn, the file's "rotate" seconds each, counted from
// the last time a line took the place, so the rotation carries on from the one that came last.
// apFrom names each line's own status, null for a line that is not one; where the line that came
// last is among them it shows for its own turn first.
size_t Turn(size_t nLines, const std::vector<const PortReadoutLine *> *pFrom, bool bKillsFirst) {
  const double fEach = g_file.parts.fRotate > 0.5 ? g_file.parts.fRotate : 0.5;
  if (nLines < 2)
    return 0;
  if (g_fClock - g_fTook < fEach) {
    if (g_bTookKill && bKillsFirst)
      return 0;  // the kills' own line, which is first where it is drawn at all
    if (!g_strTook.empty() && pFrom != NULL)
      for (size_t i = 0; i < pFrom->size() && i < nLines; ++i)
        if ((*pFrom)[i] != NULL && (*pFrom)[i]->status == g_strTook)
          return i;
  }
  // never from before the clock itself: a new game sets the clock back to nothing
  double fSince = g_fTook > 0 && g_fClock > g_fTook ? g_fClock - g_fTook : g_fClock;
  if (fSince < 0)
    fSince = 0;
  return static_cast<size_t>(static_cast<long long>(fSince / fEach) % static_cast<long long>(nLines));
}

}  // namespace

const PortReadoutLine *PortReadoutRowLine(void) {
  if (g_strRow.empty())
    return nullptr;  // the kills' own line, or a part that is rotating no status
  for (const PortReadoutLine &line : g_view.lines)
    if (line.status == g_strRow)
      return &line;
  return nullptr;
}

const char *PortReadoutPartWords(int nPart) {
  return nPart >= 0 && nPart < PORT_READOUT_PARTS ? g_astrWords[nPart].c_str() : "";
}

bool PortReadoutRowTurn(int *pnAt, int *pnOf, double *pfPhase, double *pfEach) {
  if (!g_file.bOn || g_nRowOf < 2)
    return false;
  const double fEach = g_file.parts.fRotate > 0.5 ? g_file.parts.fRotate : 0.5;
  // a status that has just landed, or a kill, holds the row out of turn (Turn), and while it does
  // the row is not in its rotation: no index for it and nothing to fade between
  if (g_fClock - g_fTook < fEach && (g_bTookKill || !g_strTook.empty()))
    return false;
  double fSince = g_fTook > 0 && g_fClock > g_fTook ? g_fClock - g_fTook : g_fClock;
  if (fSince < 0)
    fSince = 0;
  if (pnAt != nullptr)
    *pnAt = g_nRowAt;
  if (pnOf != nullptr)
    *pnOf = g_nRowOf;
  if (pfPhase != nullptr)
    *pfPhase = fSince - static_cast<double>(static_cast<long long>(fSince / fEach)) * fEach;
  if (pfEach != nullptr)
    *pfEach = fEach;
  return true;
}

const char *PortReadoutPartText(int nPart, const char *pszClassic) {
  static char s_asz[PORT_READOUT_PARTS][0x104];
  CUnit *pUnit = gpStatUnit;
  if (!g_file.bOn || pUnit == nullptr || nPart < 0 || nPart >= PORT_READOUT_PARTS)
    return nullptr;
  const PortReadoutView *pView = PortReadoutUnit(pUnit);
  if (pView == nullptr || !pView->bOn)
    return nullptr;
  const PortReadoutParts &parts = g_file.parts;
  // a roll puts its counted figures at the front of these, since a pattern is filled from the
  // first value that names the key
  std::vector<std::pair<std::string, std::string>> values = PartValues(pUnit, *pView, pszClassic);
  std::string text;
  unsigned char bCode = 0;
  bool bKeep = false;  // the part stays classic's
  switch (nPart) {
  case PORT_READOUT_NAME:
    text = Fill(parts.name, values);
    bKeep = text.empty();  // a readout that names no unit keeps the unit's own name
    bCode = CodeOf(parts.nameColour, *pView, pUnit);
    break;
  case PORT_READOUT_STATUS: {
    // classic's own line keeps its place first, so a parasite is never hidden by a mod's line;
    // with more than one the lines take the place in turn
    std::vector<std::string> lines;
    std::vector<const PortReadoutLine *> apFrom;
    if (parts.nStatusLines != PORT_READOUT_LINES_MOD && pszClassic != nullptr && *pszClassic != '\0') {
      lines.push_back(pszClassic);
      apFrom.push_back(nullptr);
    }
    if (parts.nStatusLines != PORT_READOUT_LINES_CLASSIC)
      for (const PortReadoutLine &line : pView->lines) {
        // a status's own line goes to the kills' row where the mod adds lines, so the statuses
        // are not drawn in both rotations; a mod with no "add" block keeps them here
        if (g_file.add.bLines && !line.status.empty())
          continue;
        lines.push_back(LineWords(line, std::string(), values));
        apFrom.push_back(&line);
      }
    if (lines.empty()) {
      bKeep = parts.nStatusLines == PORT_READOUT_LINES_CLASSIC;
      break;  // nothing to say: the line is blank, as classic's is for a unit with no rank
    }
    const size_t at = Turn(lines.size(), &apFrom, false);
    text = lines[at];
    bCode = apFrom[at] != nullptr ? CodeOf(apFrom[at]->colour, *pView, pUnit) : 0;
    if (bCode == 0 && apFrom[at] != nullptr && apFrom[at]->nHue != 0xFFFFFFu)
      bCode = CodeNearest(apFrom[at]->nHue);
    break;
  }
  case PORT_READOUT_KILLS: {
    // the kills' line, and the lines a mod adds taking it in turn, which is the room the classic
    // panel leaves free
    g_nRowOf = 0;
    std::vector<std::string> lines;
    if (parts.kills == kBase.kills && parts.nKillsShow == PORT_READOUT_SHOW_AUTO && !g_file.add.bLines) {
      bKeep = true;  // classic's own line, and its own rule for hiding it
      break;
    }
    const bool bKills = parts.nKillsShow == PORT_READOUT_SHOW_ALWAYS ||
                        (parts.nKillsShow == PORT_READOUT_SHOW_AUTO &&
                         (pUnit->CanAttack() || pUnit->bKillCount != 0));
    if (bKills)
      lines.push_back(Fill(parts.kills, values));
    std::vector<const PortReadoutLine *> apFrom;
    if (bKills)
      apFrom.push_back(NULL);
    // the statuses rotate here unless the room past the panel is listing them itself, so a
    // status is never read twice over ("in_huds")
    if (g_file.add.bLines && !PortReadoutRoomLists())
      for (const PortReadoutLine &line : pView->lines)
        if (!line.status.empty()) {
          // the shorter line where the room is no wider than classic's panel, which is the
          // classic console and a slot that gives the readout no room of its own: the file's
          // "each_short", or the status's own words, which say the time themselves
          const bool bShort = g_nRoomW <= PORT_READOUT_W;
          lines.push_back(LineWords(line, bShort ? g_file.add.eachShort : g_file.add.each, values));
          if (bShort)
            lines.back() = Shorten(lines.back(), PortReadoutPartRect(PORT_READOUT_KILLS).w);
          apFrom.push_back(&line);
        }
    if (lines.empty())
      break;  // an empty answer keeps the line off, as classic keeps it off
    const size_t at = Turn(lines.size(), &apFrom, bKills);
    // the status the row shows now, for its icon and for a test held while it runs out
    g_strRow = apFrom[at] != nullptr ? apFrom[at]->status : std::string();
    g_nRowAt = static_cast<int>(at);
    g_nRowOf = static_cast<int>(lines.size());
    text = lines[at];
    break;
  }
  case PORT_READOUT_LIFE: {
    // a roll counts the line to its new value, so the readout writes the line while the run
    // lasts and classic has it back as soon as the count is over. Not where classic blanks the
    // line itself: a unit whose figures do not fit, which it leaves empty.
    double fRoll = 0;
    const Run *pRoll = nullptr;
    const bool bRoll = RollOn(PORT_READOUT_LIFE, &fRoll, &pRoll) && pUnit->GetLife() <= 9999 &&
                       HandicapShields(guwUnitEnergy[pUnit->GetTypeEx()], pUnit->bPlayer) <= 9999 &&
                       (pUnit->dwFlags & 0x4000000) == 0;
    const int nLife = bRoll ? Counted(pRoll->nFromLife, (static_cast<int>(pUnit->lHitPoints) + 0xFF) >> 8,
                                      fRoll)
                            : 0;
    const int nShields =
        bRoll ? Counted(pRoll->nFromShields, static_cast<int>(pUnit->lShields) >> 8, fRoll) : 0;
    if (bRoll) {
      char sz[16] = "";
      snprintf(sz, sizeof sz, "%d", nShields);
      values.insert(values.begin(), std::make_pair("shields", std::string(sz)));
      snprintf(sz, sizeof sz, "%d", nLife);
      values.insert(values.begin(), std::make_pair("life", std::string(sz)));
    }
    if (parts.life == kBase.life && parts.lifeColour.nInk == kBase.lifeColour.nInk) {
      if (!bRoll) {
        bKeep = true;  // classic's own line, with its own two halves and their colours
        break;
      }
      // classic's own line with the counted figures, written as statdraw_draw_hp writes it: the
      // shields half in the control's own ink and the life half in the damage state's colour
      char sz[64] = "";
      const int nCode = pUnit->IsBurning() ? 6 : pUnit->IsOnFire() ? 3 : 7;
      if (!gbUnitHasShields[pUnit->wType])
        snprintf(sz, sizeof sz, "%c%d/%d", nCode, nLife, pUnit->GetLife());
      else
        snprintf(sz, sizeof sz, "%d/%d %c%d/%d", nShields,
                 HandicapShields(guwUnitEnergy[pUnit->GetTypeEx()], pUnit->bPlayer), nCode, nLife,
                 pUnit->GetLife());
      text = sz;
      break;  // the codes are in the words themselves, as classic's are
    }
    text = Fill(parts.life, values);
    bCode = CodeOf(parts.lifeColour, *pView, pUnit);
    break;
  }
  case PORT_READOUT_ENERGY: {
    // the same count on the energy, for a caster of your own: not a hallucination's line, which
    // is the time it has left and not a figure a roll was given
    double fRoll = 0;
    const Run *pRoll = nullptr;
    const bool bRoll = RollOn(PORT_READOUT_ENERGY, &fRoll, &pRoll) && pUnit->GetMaxEnergyReal() > 0 &&
                       (pUnit->dwFlags & 0x40000000) == 0;
    const int nEnergy = bRoll ? Counted(pRoll->nFromEnergy, pUnit->wEnergy >> 8, fRoll) : 0;
    if (bRoll) {
      char sz[16] = "";
      snprintf(sz, sizeof sz, "%d", nEnergy);
      values.insert(values.begin(), std::make_pair("energy", std::string(sz)));
    }
    if (parts.nEnergyShow == PORT_READOUT_SHOW_NEVER)
      break;  // off: the line is blank
    if (parts.energy == kBase.energy && parts.nEnergyShow == kBase.nEnergyShow) {
      if (!bRoll) {
        bKeep = true;  // classic's own line, shown for a caster of your own as ever
        break;
      }
      char sz[32] = "";
      snprintf(sz, sizeof sz, "%d/%d", nEnergy, pUnit->GetMaxEnergyReal());
      text = sz;
      bCode = 4;  // classic's own ink for the energy
      break;
    }
    if (parts.nEnergyShow == PORT_READOUT_SHOW_AUTO && pUnit->GetMaxEnergyReal() <= 0) {
      bKeep = true;  // classic's rule: the energy shows for a caster of your own
      break;
    }
    text = Fill(parts.energy, values);
    bCode = 4;  // classic's own ink for the energy
    break;
  }
  default:
    bKeep = true;  // the wireframe and the icons are not words
    break;
  }
  // the words the panel is about to draw, for the drawing to measure: classic's own where the
  // part stays classic's, and without the colour codes, which are bytes in the text itself
  {
    const std::string words = bKeep && pszClassic != nullptr ? std::string(pszClassic) : bKeep ? std::string() : text;
    g_astrWords[nPart].clear();
    for (size_t i = 0; i < words.size(); ++i)
      if (static_cast<unsigned char>(words[i]) >= 0x20)
        g_astrWords[nPart].push_back(words[i]);
  }
  if (bKeep)
    return nullptr;
  char *psz = s_asz[nPart];
  int n = 0;
  if (bCode != 0 && !text.empty())
    psz[n++] = static_cast<char>(bCode);
  int nText = static_cast<int>(text.size());
  if (nText > static_cast<int>(sizeof s_asz[0]) - n - 1)
    nText = static_cast<int>(sizeof s_asz[0]) - n - 1;
  while (nText > 0 && (static_cast<unsigned char>(text[nText]) & 0xC0) == 0x80)
    --nText;  // a cut falls on a letter's boundary, since the text is UTF-8
  memcpy(psz + n, text.c_str(), static_cast<size_t>(nText));
  psz[n + nText] = '\0';
  return psz;
}

// ---- the runs, for the drawing -------------------------------------------------------------------
// The drawing asks what is running on a part and draws the light each run carries, so the two
// consoles cannot differ and nothing of the running belongs to the drawing. An effect an event set
// off is a run on the list; one with a "while" test is not kept at all, since its test is the
// whole of it: it is read off the unit here, each time the part is asked for.
namespace {

// The colour an effect asks for, as a number. False where it names none, which leaves the
// drawing the part's own colour.
bool RunRgb(const PortReadoutColour &c, const PortReadoutView &view, CUnit *pUnit, unsigned int *pnRgb) {
  switch (c.nInk) {
  case PORT_READOUT_INK_RGB:
    *pnRgb = c.nRgb;
    return true;
  case PORT_READOUT_INK_RANK:
    if (view.nRankAt < 0)
      return false;
    *pnRgb = view.nRankRgb;
    return true;
  case PORT_READOUT_INK_TEAM: {
    unsigned char abRgb[3] = {0xFF, 0xFF, 0xFF};
    if (!PortTeamColorGet(pUnit->bPlayer, abRgb))
      return false;
    *pnRgb = (static_cast<unsigned>(abRgb[0]) << 16) | (static_cast<unsigned>(abRgb[1]) << 8) | abRgb[2];
    return true;
  }
  case PORT_READOUT_INK_LIFE:
  case PORT_READOUT_INK_DAMAGE:
    *pnRgb = pUnit->IsBurning() ? 0xFF5A4Au : pUnit->IsOnFire() ? 0xF2C94Cu : 0x6FE08Au;
    return true;
  default:
    return false;
  }
}

// Whether a "while" test holds for the unit the panel describes now.
bool Holds(const PortReadoutFx &fx, CUnit *pUnit, const PortReadoutView &view, int nPart) {
  switch (fx.nWhile) {
  case PORT_READOUT_WHILE_LIFE: {
    const int nMost = pUnit->GetLife();
    const int nNow = static_cast<int>(pUnit->lHitPoints) >> 8;
    return nMost > 0 && static_cast<double>(nNow) / nMost < fx.fAt;
  }
  case PORT_READOUT_WHILE_RANK: {
    const int nAt = RankAt(fx.rank);
    return nAt >= 0 && view.nRankAt >= nAt;
  }
  case PORT_READOUT_WHILE_STATUS:
    // on a line, the status the line is showing now, so it runs out where the player can read it
    // running out; anywhere else, any status of the unit's with that little left
    if (nPart == PORT_READOUT_STATUS || nPart == PORT_READOUT_KILLS) {
      const PortReadoutLine *pRow = PortReadoutRowLine();
      if (pRow != nullptr)
        return pRow->fOf > 0 && pRow->fLeft < fx.fAt;
    }
    for (const PortReadoutLine &line : view.lines)
      if (!line.status.empty() && line.fOf > 0 && line.fLeft < fx.fAt)
        return true;
    return false;
  default:
    return false;
  }
}

// The view as it stands for the unit the panel describes. The drawing asks for every part in
// one frame and the panel's own gate has already brought the view up to date for that frame
// (PortReadoutDraws), so asking again here would read the unit's statuses seven times over.
const PortReadoutView *ViewNow(CUnit *pUnit) {
  if (g_bViewAsked && g_dwViewUnit == static_cast<unsigned>(pUnit->GetUnitID()) && g_view.bOn)
    return &g_view;
  return PortReadoutUnit(pUnit);
}

// A line of text into one of the run's own buffers, cut on a letter's boundary.
void Words(char *psz, size_t nRoom, const std::string &text) {
  size_t n = text.size() < nRoom - 1 ? text.size() : nRoom - 1;
  while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
    --n;
  memcpy(psz, text.c_str(), n);
  psz[n] = '\0';
}

}  // namespace

int PortReadoutRuns(int nPart, const char *pszSlot, PortReadoutRun *paOut, int nMost) {
  if (paOut == nullptr || nMost <= 0 || !g_file.bOn || nPart < 0 || nPart >= PORT_READOUT_PARTS)
    return 0;
  CUnit *pUnit = gpStatUnit;
  if (pUnit == nullptr)
    return 0;
  const PortReadoutView *pView = ViewNow(pUnit);
  if (pView == nullptr || !pView->bOn)
    return 0;
  const std::string want = pszSlot != nullptr ? pszSlot : "";
  std::vector<std::pair<std::string, std::string>> values;
  bool bValues = false;
  int n = 0;

  // what an event set off, the newest first
  for (size_t k = 0; k < g_runs.size() && n < nMost; ++k) {
    const Run &run = g_runs[g_runs.size() - 1 - k];
    if (run.nPart != nPart || run.slot != want)
      continue;
    PortReadoutRun &out = paOut[n++];
    out = PortReadoutRun();
    out.nOp = run.nOp;
    out.fAge = g_fClock - run.fAt;
    out.fPhase = out.fAge;
    out.fThrough = run.fTime > 0 && out.fAge < run.fTime ? out.fAge / run.fTime : 1.0;
    out.fTime = run.fTime;
    out.fHold = run.fHold;
    out.fRate = run.fRate;
    out.fEvery = run.fEvery;
    out.fSize = run.fSize;
    out.fLight = run.fLight;
    out.bGlint = run.bGlint;
    out.bGlow = run.bGlow;
    out.bFromRight = run.bFromRight;
    out.nAmount = run.nAmount;
    out.bColour = RunRgb(run.colour, *pView, pUnit, &out.nRgb);
    Words(out.szStatus, sizeof out.szStatus, run.status);
    if (!run.text.empty()) {
      if (!bValues) {
        values = PartValues(pUnit, *pView, nullptr);
        bValues = true;
      }
      std::vector<std::pair<std::string, std::string>> v = values;
      char sz[32] = "";
      snprintf(sz, sizeof sz, "%d", run.nAmount);
      v.push_back(std::make_pair("value", sz));
      v.push_back(std::make_pair("stat", run.slot));
      Words(out.szText, sizeof out.szText, Fill(run.text, v));
    }
    if (out.nOp == PORT_READOUT_BLINK && out.fRate > 3.0)
      out.fRate = 3.0;
  }

  // then what a test holds on, in the file's own order. A held effect has no beginning of its
  // own, so its rate counts in the readout's clock and it is through as long as it holds.
  for (size_t i = 0; i < g_file.fx.size() && n < nMost; ++i) {
    const PortReadoutFx &fx = g_file.fx[i];
    if (fx.nWhile == PORT_READOUT_WHILE_NONE || LinesPart(fx) != nPart)
      continue;
    if ((fx.slot == "{stat}" ? std::string() : fx.slot) != want)
      continue;
    if (fx.ifRank == "rank" && pView->nRankAt < 0)
      continue;
    const int nOp = Allowed(fx.nOp, fx.bGlow, fx.bGlint);
    if (nOp < 0 || !Holds(fx, pUnit, *pView, nPart))
      continue;
    PortReadoutRun &out = paOut[n++];
    out = PortReadoutRun();
    out.nOp = nOp;
    out.bHeld = true;
    out.fThrough = 1.0;
    out.fPhase = g_fClock;
    out.fTime = fx.fTime;
    out.fRate = nOp == PORT_READOUT_BLINK && fx.fRate > 3.0 ? 3.0 : fx.fRate;
    out.fAt = fx.fAt;
    out.fEvery = fx.fEvery;
    out.fSize = fx.fSize;
    out.fLight = fx.fLight;
    out.bGlint = fx.bGlint;
    out.bGlow = fx.bGlow;
    out.bFromRight = fx.bFromRight;
    out.bColour = RunRgb(fx.colour, *pView, pUnit, &out.nRgb);
    if (!fx.text.empty()) {
      if (!bValues) {
        values = PartValues(pUnit, *pView, nullptr);
        bValues = true;
      }
      Words(out.szText, sizeof out.szText, Fill(fx.text, values));
    }
  }
  return n;
}

bool PortReadoutPartTip(const CUnit *pUnit, int nPart, std::string *pOut, bool *pbWhole) {
  if (pOut != nullptr)
    pOut->clear();
  if (pbWhole != nullptr)
    *pbWhole = true;  // neither part has a tooltip of classic's to add to
  if (!g_file.bOn || pUnit == nullptr)
    return false;
  CUnit *p = const_cast<CUnit *>(pUnit);
  const PortReadoutView *pView = PortReadoutUnit(p);
  if (pView == nullptr || !pView->bOn)
    return false;
  std::vector<std::string> lines;
  if (nPart == PORT_READOUT_WIRE) {
    // the life, which has no icon of its own: the figure, then how far the roll moved it
    lines.push_back(PortTextF("Life {0} of {1}", {std::to_string((static_cast<int>(p->lHitPoints) + 0xFF) >> 8),
                                                  std::to_string(p->GetLife())}));
    if (pView->abDelta[PORT_READOUT_STAT_LIFE])
      lines.push_back(DeltaWords(PORT_READOUT_STAT_LIFE, pView->anDelta[PORT_READOUT_STAT_LIFE]));
    for (const PortReadoutTip &tip : pView->tips)
      if (tip.stat == PortReadoutStatName(PORT_READOUT_STAT_LIFE))
        for (const std::string &s : tip.lines)
          lines.push_back(s);
  } else if (nPart == PORT_READOUT_NAME) {
    // what the unit was rolled from, what it is, and every change it rolled, one to a line: the
    // whole of it in one place, since three of the six stats have no icon to carry them
    if (!pView->name.empty() && pView->name != p->GetName())
      lines.push_back(PortTextF("Rolled from {0}", {p->GetName()}));
    for (const PortReadoutLine &line : pView->lines)
      if (line.status.empty() && !line.text.empty()) {
        lines.push_back(LineWords(line, std::string(), PartValues(p, *pView, nullptr)));
        break;  // the hook's first line, which is the one that says what it is
      }
    for (int nStat = 0; nStat < PORT_READOUT_STATS; ++nStat)
      if (pView->abDelta[nStat] && pView->anDelta[nStat] != 0)
        lines.push_back(DeltaWords(nStat, pView->anDelta[nStat]));
    // the speed's own lines, which have no icon to hang on either
    for (const PortReadoutTip &tip : pView->tips)
      if (tip.stat == PortReadoutStatName(PORT_READOUT_STAT_SPEED))
        for (const std::string &s : tip.lines)
          lines.push_back(s);
  }
  if (lines.size() < 2)
    return false;  // nothing the panel does not already say
  if (pOut != nullptr)
    for (size_t i = 0; i < lines.size(); ++i)
      *pOut += (i ? "\n" : "") + lines[i];
  return true;
}

const char *PortReadoutLineText(const PortReadoutLine *pLine, bool bShort) {
  static char s_sz[0x104];
  s_sz[0] = '\0';
  CUnit *pUnit = gpStatUnit;
  if (!g_file.bOn || pLine == nullptr || pUnit == nullptr)
    return s_sz;
  const PortReadoutView *pView = ViewNow(pUnit);
  if (pView == nullptr)
    return s_sz;
  // a line of the hook's own carries its text and takes no pattern; a status's line takes the
  // short one where the row is narrow and the file gives one
  std::string pattern;
  if (!pLine->status.empty())
    pattern = bShort && !g_file.add.eachShort.empty() ? g_file.add.eachShort : g_file.add.each;
  Words(s_sz, sizeof s_sz, LineWords(*pLine, pattern, PartValues(pUnit, *pView, nullptr)));
  return s_sz;
}

int PortReadoutSlotStat(const CUnit *pUnit, int nSlot) {
  return pUnit != nullptr ? StatOfSlot(const_cast<CUnit *>(pUnit), nSlot) : -1;
}
