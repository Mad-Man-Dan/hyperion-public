// studio_readout.cpp -- the Looks workspace's Unit readout page: readout.json, the one unit's
// panel as a mod changes it (docs/mod-data.md, "readout.json").
//
// The template is classic's own panel, seven parts in the order it draws them, and a mod changes
// the parameters it wants and leaves the rest. So the page is written as a diff: each parameter
// shows classic's value, the mod's over it when there is one, and an x that gives classic's back.
//
// The loader is the judge. Rather than mirror its reading, the page writes the model's readout
// document out as text and hands it to PortReadoutCheck, which is what a game's console loads
// with: what comes back is the file in force, the warnings naming each key it could not read with
// the line it was on, and why a whole file does not read. So nothing here can drift from the
// loader, and the Problems rows are the loader's own words.
//
// The preview is the real drawing. In the studio's canvas game, which runs in this process, the
// panel's own picture is drawn by PortReadoutDraw into the readout's 8-bit pixels; the page shows
// those pixels in the game's palette, beside what the hook returned for that unit. Both are the
// pushed readout's, since the loader reads the file at a push.
//
//   studio_readout {}                                  the file in force, its parts and what the loader says
//   studio_readout_set {part?, field, value?, revert?}  one parameter of the mod's, or classic's back
//   studio_readout_fx {at?, field?, value?, new?, remove?}  an effect made, changed or taken away
//   studio_readout_show {}                             the panel's words now, and what the hook returned
#define NOMINMAX
#include "../h/platform.h"
#include <algorithm>
#include <math.h>
#include <string>
#include <vector>
#include "../h/CUnit.h"
#include "../design/design_engine.h"        // DesignCall
#include "../design/design_hooks.h"         // DesignInGame
#include "../port/hud/port_readout.h"
#include "../port/tools/port_benchinfo.h"   // PortBenchSelection
#include "../port/tools/port_control.h"     // PortControlCommand
#include "studio_draw.h"
#include "studio_media.h"                   // StudioMediaPara
#include "studio_readout.h"

namespace {

using namespace studio_draw;

// ---- the parts and their parameters ----------------------------------------------------------
// Each parameter as readout.json writes it, with classic's own value in words and what it takes.
// The order is the order classic draws the parts in, which is the order the page writes them out.

enum ParamKind { PK_TEXT, PK_CHOICE, PK_BOOL, PK_NUM, PK_COLOUR, PK_LINES, PK_STATES };

struct Param {
  const char *pszKey;
  const char *pszLabel;
  int nKind;
  const char *pszChoices;  // the names it takes, separated by '|'
  const char *pszClassic;  // classic's own value, in words
  const char *pszNote;
};

struct Part {
  const char *pszKey;    // "" for the readout's own keys, which are no part
  const char *pszLabel;
  const char *pszWhere;  // where classic puts it, in the readout's own pixels
  const Param *pParams;
  int nParams;
};

const Param kOwn[] = {
    {"from", "Template", PK_CHOICE, "classic", "classic", "the only template there is"},
    {"in_huds", "In HUD designs", PK_BOOL, nullptr, "yes",
     "no keeps the readout to the classic console, a HUD slot showing classic's panel"},
};

const Param kWire[] = {
    {"tint", "Tint", PK_CHOICE, "life|rank|team|none", "life", "green, then yellow and red"},
    {"states", "Turns at", PK_STATES, nullptr, "66 and 33", "the two percentages of life where the tint turns"},
    {"frame", "Frame", PK_CHOICE, "none|rank|corners", "none", "a mark round the wireframe"},
    {"frame_colour", "Frame colour", PK_COLOUR, "classic|rank|team", "classic", ""},
    {"art", "Art", PK_CHOICE, "unit|mod", "unit", "mod for the mod's own wireframes"},
};

const Param kName[] = {
    {"text", "Text", PK_TEXT, nullptr, "{name}", "a pattern: a value in braces, the rest as written"},
    {"colour", "Colour", PK_COLOUR, "classic|rank|team", "classic", ""},
    {"align", "Align", PK_CHOICE, "centre|left", "centre", ""},
};

const Param kStatus[] = {
    {"lines", "Lines", PK_CHOICE, "classic|classic then mod|mod", "classic",
     "classic's own statuses keep their place first, so a parasite is never hidden"},
    {"rotate", "Rotate", PK_NUM, nullptr, "2.5", "seconds each line shows, with more than one"},
    {"rank_pips", "Rank pips", PK_BOOL, nullptr, "no",
     "the rank as pips beside the line, one for each rank above the first the file names: that first "
     "rank takes none on the panel, its colour on the name saying it. A group tile marks every ranked "
     "unit, so the two differ on purpose"},
};

const Param kKills[] = {
    {"show", "Show", PK_CHOICE, "auto|always|never", "auto", "auto is classic's rule: a unit that can attack or has killed"},
    {"text", "Text", PK_TEXT, nullptr, "{title} {kills}", "its word is the game's own text"},
};

const Param kSlots[] = {
    {"order", "Order", PK_LINES, nullptr, "armor, shields, weapon_ground, weapon_air, special",
     "a line each; a mod may add its own names after these"},
    {"level", "Level", PK_TEXT, nullptr, "{level}", ""},
    {"delta", "Rolled change", PK_CHOICE, "off|on the icon|in the tooltip", "off",
     "on the icon is a tab: a chevron the way the figure went and how far, green where the unit gains "
     "by it and red where it loses, a faster cooldown counting as a gain though its number falls. The "
     "armour and the shields take a tab on their own icon; the damage and that weapon's cooldown share "
     "one on the weapon's icon; the life takes one at the right end of its row, since classic centres "
     "that row's figures; the speed takes none, and is read in the name's tooltip and in a HUD's room. "
     "All six reach a tooltip whichever is set"},
    {"tooltip", "Tooltip", PK_LINES, nullptr, "classic's own",
     "a line each, under classic's own or instead of it. The hook's own tooltips reach the same places: "
     "its life the wireframe, its cooldown that weapon's icon, and every rolled change one to a line on "
     "the name, which is where the speed is read"},
};

const Param kLife[] = {
    {"text", "Text", PK_TEXT, nullptr, "{shields}/{shields_max} {life}/{life_max}", ""},
    {"colour", "Colour", PK_COLOUR, "damage|life|classic", "damage",
     "damage is classic's: yellow while it smoulders, red while it burns"},
};

const Param kEnergy[] = {
    {"text", "Text", PK_TEXT, nullptr, "{energy}/{energy_max}", ""},
    {"show", "Show", PK_CHOICE, "classic|every caster|never", "classic", "classic shows it for your own casters only"},
};

const Param kStatusLook[] = {
    {"icon", "Icon", PK_CHOICE, "framed|glyph|game|none", "framed",
     "framed is a glyph in a frame: a disc for a status that helps the unit, a diamond for one that hurts"},
    {"line3", "Line 3", PK_CHOICE, "in turn|a row", "in turn",
     "in turn shares classic's third line with the kills; a row wants the spare rows a HUD slot gives"},
    {"wire_mark", "On the wireframe", PK_CHOICE, "corner|tint|pattern|none", "corner",
     "corner puts the most telling status's icon in the wireframe's top left"},
    {"group_mark", "In the group view", PK_CHOICE, "glyphs|dots|edge|none", "glyphs",
     "up to three down a group tile's left"},
};

const Param kAdd[] = {
    {"each", "Each line", PK_TEXT, nullptr, "{name} {left}s {effects}",
     "the pattern for one line, on the panel's row and in a slot's room alike"},
    {"each_short", "Where the room is short", PK_TEXT, nullptr, "the status's own words, cut",
     "the classic console's row is 144 pixels, where a line naming a status and all it does is half as "
     "long again; a slot's room takes it below 120"},
    {"bar", "Bar", PK_BOOL, nullptr, "yes", "a bar for a status's time left"},
    {"in_huds", "In HUD designs", PK_CHOICE, "a line each|in turn", "a line each",
     "a line each gives the room the statuses, a row apiece, and the panel's kills row keeps to the "
     "kills; in turn keeps the room to the hook's lines and rotates the statuses through that row. "
     "Nothing is drawn twice either way"},
};

#define NP(a) static_cast<int>(sizeof(a) / sizeof((a)[0]))
const Part kParts[] = {
    {"", "The file's own", "", kOwn, NP(kOwn)},
    {"wire", "Wireframe", "(30, 0) 64 x 64", kWire, NP(kWire)},
    {"name", "Name", "(102, 0) 152 x 14", kName, NP(kName)},
    {"status", "Status line", "(106, 16) 144 x 14", kStatus, NP(kStatus)},
    {"kills", "Kills", "(106, 30) 144 x 14", kKills, NP(kKills)},
    {"slots", "Stat icons", "(104, 51) four of 36 x 36, 39 apart", kSlots, NP(kSlots)},
    {"life", "Life", "(22, 64) 80 x 12", kLife, NP(kLife)},
    {"energy", "Energy", "(22, 76) 80 x 12", kEnergy, NP(kEnergy)},
    {"status_look", "How a status shows", "before the name, on the wireframe, in the group view",
     kStatusLook, NP(kStatusLook)},
    {"lines", "What it adds", "the spare rows, or the kills' line in turn", kAdd, NP(kAdd)},
};
const int kPartCount = NP(kParts);

// The values a pattern may name, for the hint under a text field.
const char *const kValues =
    "{name} {rank} {kills} {life} {life_max} {shields} {shields_max} {energy} {energy_max} "
    "{armor} {level} {stat} {value} {title}, and on a status's line {left} {stacks} {effects}. "
    "A line's own values go first, so {name} on a status's line is the status's name and not the "
    "unit's";

// ---- the file and the loader's reading -------------------------------------------------------

const PortJson &Doc(void) {
  return StudioDoc("readout");
}

const PortJson &Readout(void) {
  return Doc()["readout"];
}

// Where a part's parameters sit: the readout's own keys on the block itself, a part's under
// "parts", and what it adds under "add"."lines".
std::vector<std::string> PathOf(const Part &p, const char *pszKey) {
  std::vector<std::string> path;
  path.push_back("readout");
  if (strcmp(p.pszKey, "lines") == 0) {
    path.push_back("add");
    path.push_back("lines");
  } else if (strcmp(p.pszKey, "status_look") == 0) {
    path.push_back("status_look");
  } else if (p.pszKey[0] != '\0') {
    path.push_back("parts");
    path.push_back(p.pszKey);
  }
  if (pszKey != nullptr)
    path.push_back(pszKey);
  return path;
}

const PortJson &Value(const Part &p, const Param &f) {
  const PortJson *pAt = &Doc();
  const std::vector<std::string> path = PathOf(p, f.pszKey);
  for (const std::string &s : path)
    pAt = &(*pAt)[s.c_str()];
  return *pAt;
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

bool Put(const std::vector<std::string> &path, const PortJson &value, const std::string &label,
         const std::string &sender, std::string *pWhy) {
  std::string merge;
  for (const std::string &s : path)
    merge += "/" + s;
  StudioGroupBegin(sender, label, "readout" + merge);
  const bool bOk = StudioPut("readout", path, value, sender, label, pWhy);
  StudioGroupEnd();
  return bOk;
}

// The document as the loader reads it: written out as text and handed to PortReadoutCheck, which
// is the same call a game's console and --mod-check make. Kept by the text, so the page asking
// every frame costs one reading for each change.
struct Read {
  std::string of;
  bool bOk = false;
  PortReadoutFile file;
  std::vector<std::string> warnings;
  std::string why;
};
Read g_read;

const Read &Reading(void) {
  const std::string text = PortJsonWrite(Doc());
  if (g_read.of == text)
    return g_read;
  g_read = Read();
  g_read.of = text;
  g_read.bOk = PortReadoutCheck(text, &g_read.file, &g_read.warnings, &g_read.why);
  return g_read;
}

bool HasAny(void) {
  return Readout().kind == PortJson::OBJECT && !Readout().members.empty();
}

// How many parameters the mod sets, past classic's: what the page's head counts.
int Changed(void) {
  int n = 0;
  for (int i = 0; i < kPartCount; ++i)
    for (int k = 0; k < kParts[i].nParams; ++k)
      if (Value(kParts[i], kParts[i].pParams[k]).kind != PortJson::NONE)
        ++n;
  return n;
}

// ---- the panel's words -----------------------------------------------------------------------
// Filled on the game's thread by the channel command, read by the page on the next frame it
// draws: what each part answers for the unit the panel describes, and the lines the hook gave.

struct Shot {
  bool bDraws = false;                        // the readout is writing the panel's words at all
  std::string aWords[PORT_READOUT_PARTS];     // what each part answers, "" for classic's own
  std::string row;                            // the status whose line the kills' row shows now
  std::string runs;                           // the effects running on each part, a row each
  std::string room;                           // the room the added lines go in, and who lists the statuses
  std::string aTips[PORT_READOUT_SLOTS_MOST]; // each stat icon's tooltip, where the readout gives one
  std::string aPartTips[PORT_READOUT_PARTS];  // the wireframe's and the name's, which carry the stats
                                              // that have no icon
  std::string lines;                          // the lines the hook returned, one a row
  std::string name, rank;
  std::string why;
  bool bRead = false;
};
Shot g_shot;

// ---- the page --------------------------------------------------------------------------------

int g_nPart = 1;  // the part written out, 0 the file's own keys

float LabelW(void) {
  return P(160.0f);
}

void Row(const char *pszLabel) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y + P(6.0f), C(TK_LABEL), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(at.x + LabelW(), at.y));
}

std::vector<std::string> Choices(const char *psz) {
  std::vector<std::string> out;
  if (psz == nullptr)
    return out;
  std::string one;
  for (const char *p = psz;; ++p) {
    if (*p == '|' || *p == '\0') {
      out.push_back(one);
      one.clear();
      if (*p == '\0')
        break;
      continue;
    }
    one += *p;
  }
  return out;
}

// One parameter as a diff from classic: classic's value, the mod's over it, and an x that gives
// classic's back. A parameter the mod does not set is drawn greyed with classic's own.
void ParamRow(const Part &part, const Param &f, float fW, bool bMine) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson &v = Value(part, f);
  const bool bSet = v.kind != PortJson::NONE;
  ImGui::PushID(f.pszKey);
  Row(f.pszLabel);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  if (!bMine)
    ImGui::BeginDisabled();
  if (f.nKind == PK_TEXT) {
    char sz[512];
    snprintf(sz, sizeof sz, "%s", bSet ? v.Str().c_str() : "");
    if (StudioInput("##v", sz, sizeof sz, fW * 0.5f, P(26.0f), f.pszClassic)) {
      std::string why;
      Put(PathOf(part, f.pszKey), sz[0] == '\0' ? PortJson() : Str(sz),
          std::string("set the readout's ") + f.pszKey, "you", &why);
    }
  } else if (f.nKind == PK_CHOICE || f.nKind == PK_COLOUR) {
    const std::vector<std::string> choices = Choices(f.pszChoices);
    const std::string now = v.Str();
    const bool bRgb = f.nKind == PK_COLOUR && now.size() == 7 && now[0] == '#';
    if (StudioSelectBegin("##v", bSet ? now.c_str() : (std::string(f.pszClassic) + "  (classic)").c_str(),
                          P(200.0f))) {
      if (StudioItem((std::string(f.pszClassic) + "  (classic)").c_str(), !bSet)) {
        std::string why;
        Put(PathOf(part, f.pszKey), PortJson(), std::string("gave the readout's ") + f.pszKey + " back to classic",
            "you", &why);
      }
      for (const std::string &one : choices)
        if (StudioItem(one.c_str(), bSet && now == one)) {
          std::string why;
          Put(PathOf(part, f.pszKey), Str(one), std::string("set the readout's ") + f.pszKey, "you", &why);
        }
      if (f.nKind == PK_COLOUR && StudioItem("a colour of its own", bRgb)) {
        std::string why;
        Put(PathOf(part, f.pszKey), Str("#ffffff"), std::string("set the readout's ") + f.pszKey, "you", &why);
      }
      StudioSelectEnd();
    }
    // a colour of its own, typed beside the pulldown, with its swatch
    if (bRgb) {
      ImGui::SameLine();
      char sz[16];
      snprintf(sz, sizeof sz, "%s", now.c_str());
      if (StudioInput("##rgb", sz, sizeof sz, P(90.0f), P(26.0f), "#rrggbb")) {
        unsigned n = 0;
        if (strlen(sz) == 7 && sz[0] == '#' && sscanf(sz + 1, "%6x", &n) == 1) {
          std::string why;
          Put(PathOf(part, f.pszKey), Str(sz), std::string("set the readout's ") + f.pszKey, "you", &why);
        } else {
          StudioSay(std::string(f.pszKey) + " is a colour, \"#rrggbb\", or one of the names it takes");
        }
      }
      unsigned n = 0xFFFFFF;
      sscanf(now.c_str() + 1, "%6x", &n);
      const ImVec2 s = ImGui::GetCursorScreenPos();
      d->AddRectFilled(ImVec2(s.x + P(6.0f), s.y + P(5.0f)), ImVec2(s.x + P(22.0f), s.y + P(21.0f)),
                       IM_COL32((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF, 255), P(2.0f));
      ImGui::Dummy(ImVec2(P(30.0f), P(26.0f)));
    }
  } else if (f.nKind == PK_BOOL) {
    const bool bNow = bSet ? (v.kind == PortJson::BOOL && v.b) : strcmp(f.pszClassic, "yes") == 0;
    bool b = bNow;
    if (StudioCheck("##v", b ? "yes" : "no", &b)) {
      std::string why;
      Put(PathOf(part, f.pszKey), Bool(b), std::string("set the readout's ") + f.pszKey, "you", &why);
    }
  } else if (f.nKind == PK_NUM) {
    double dNow = v.Num(atof(f.pszClassic));
    if (StudioNumber("##v", &dNow, P(90.0f), P(26.0f), true)) {
      std::string why;
      Put(PathOf(part, f.pszKey), Num(dNow), std::string("set the readout's ") + f.pszKey, "you", &why);
    }
  } else if (f.nKind == PK_STATES) {
    // the two percentages of life where the wireframe's tint turns, the higher first
    const ImVec2 o = ImGui::GetCursorScreenPos();
    for (int i = 0; i < 2; ++i) {
      ImGui::PushID(i);
      ImGui::SetCursorScreenPos(ImVec2(o.x + i * P(84.0f), o.y));
      double dOne = v.size() > static_cast<size_t>(i) ? v.At(i).Num(i == 0 ? 66 : 33) : (i == 0 ? 66 : 33);
      if (StudioNumber("##s", &dOne, P(78.0f), P(26.0f), true)) {
        dOne = dOne < 0 ? 0 : dOne > 100 ? 100 : dOne;
        PortJson out;
        out.kind = PortJson::ARRAY;
        for (int k = 0; k < 2; ++k) {
          const double d = k == i ? dOne
                                  : (v.size() > static_cast<size_t>(k) ? v.At(k).Num(k == 0 ? 66 : 33)
                                                                       : (k == 0 ? 66 : 33));
          out.items.push_back(Num(d));
        }
        std::string why;
        Put(PathOf(part, f.pszKey), out, "set where the wireframe's tint turns", "you", &why);
      }
      ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x + P(170.0f), o.y));
    ImGui::Dummy(ImVec2(P(10.0f), P(26.0f)));
  } else if (f.nKind == PK_LINES) {
    // a list of names or lines, one a row
    const ImVec2 o = ImGui::GetCursorScreenPos();
    float y = o.y;
    for (size_t i = 0; i < v.size() && i < 12; ++i) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetCursorScreenPos(ImVec2(o.x, y));
      char sz[256];
      snprintf(sz, sizeof sz, "%s", v.At(i).Str().c_str());
      std::vector<std::string> path = PathOf(part, f.pszKey);
      path.push_back(std::to_string(i));
      if (StudioInput("##l", sz, sizeof sz, fW * 0.4f, P(24.0f))) {
        std::string why;
        Put(path, sz[0] == '\0' ? PortJson() : Str(sz), std::string("set a line of the readout's ") + f.pszKey, "you",
            &why);
      }
      ImGui::SameLine();
      if (StudioButton("##lx", "", "x", SB_FLAT, 24.0f)) {
        std::string why;
        Put(path, PortJson(), std::string("took a line off the readout's ") + f.pszKey, "you", &why);
      }
      ImGui::PopID();
      y += P(26.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    if (v.size() < 12 && StudioButton("##l+", "A line", "plus", SB_FLAT, 24.0f)) {
      std::vector<std::string> path = PathOf(part, f.pszKey);
      path.push_back(std::to_string(v.size()));
      std::string why;
      Put(path, Str(""), std::string("added a line to the readout's ") + f.pszKey, "you", &why);
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(28.0f)));
    ImGui::Dummy(ImVec2(fW * 0.4f, P(1.0f)));
  }
  if (!bMine)
    ImGui::EndDisabled();
  // classic's own value, and the x that gives it back
  const float fRight = at.x + fW * 0.55f;
  if (bSet) {
    T(d, STUDIO_FONT_MONO, 9.5f, fRight, at.y + P(3.0f), C(TK_INK_OFF),
      std::string("classic: ") + f.pszClassic);
    ImGui::SetCursorScreenPos(ImVec2(fRight + P(170.0f), at.y));
    if (StudioButton("##rev", "", "x", SB_FLAT, 24.0f, 0.0f, kStudioFsXs, bMine)) {
      std::string why;
      Put(PathOf(part, f.pszKey), PortJson(), std::string("gave the readout's ") + f.pszKey + " back to classic",
          "you", &why);
    }
  }
  if (f.pszNote[0] != '\0')
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, fRight, at.y + (bSet ? P(16.0f) : P(7.0f)), C(TK_INK3), f.pszNote,
      fW * 0.44f);
  if (f.nKind == PK_TEXT)
    T(d, STUDIO_FONT_MONO, 9.0f, at.x, at.y + P(28.0f), C(TK_INK3), kValues, fW * 0.5f);
  ImGui::SetCursorScreenPos(
      ImVec2(at.x - LabelW(), std::max(ImGui::GetCursorScreenPos().y, at.y + (f.nKind == PK_TEXT ? P(44.0f) : P(30.0f)))));
  ImGui::Dummy(ImVec2(fW, P(1.0f)));
  ImGui::PopID();
}

// ---- the effects ------------------------------------------------------------------------------

const PortJson &Fx(void) {
  return Readout()["fx"];
}

std::string EventsOf(const PortJson &one) {
  const PortJson &on = one["on"];
  if (on.kind == PortJson::STRING)
    return on.s;
  std::string out;
  for (size_t i = 0; i < on.size(); ++i)
    out += (out.empty() ? "" : ", ") + on.At(i).Str();
  return out;
}

// One effect as a row: what sets it off, the op on the part, and its own values. Motion is marked,
// since the player's Reduce motion leaves those out.
void FxRow(size_t nAt, const PortJson &one, float fW, bool bMine) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const std::string op = one["do"].Str(), part = one["part"].Str("wire");
  const int nOp = [&]() {
    for (int i = 0; i < PORT_READOUT_OPS; ++i)
      if (op == PortReadoutOpName(i))
        return i;
    return -1;
  }();
  const bool bMoves = nOp >= 0 && PortReadoutOpMoves(nOp);
  const std::string says = one["while"].Str();
  const std::string sets = says.empty() ? EventsOf(one) : "while " + says;
  float x = a.x;
  x += T(d, STUDIO_FONT_MONO, kStudioFsS, x, a.y + P(4.0f), C(TK_HI), sets.empty() ? "(nothing)" : sets, fW * 0.28f);
  x = a.x + fW * 0.30f;
  x += T(d, STUDIO_FONT_BOLD, kStudioFsS, x, a.y + P(4.0f), C(nOp < 0 ? TK_BAD : TK_INK), op.empty() ? "(no op)" : op) +
       P(8.0f);
  if (bMoves)
    x += StudioChip(d, ImVec2(x, a.y + P(3.0f)), "motion", CH_WARN) + P(8.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + fW * 0.52f, a.y + P(4.0f), C(TK_INK2), "on " + part, fW * 0.16f);
  // its own values, as the file gives them
  std::string vals;
  for (const PortJsonMember &m : one.members) {
    if (m.first == "on" || m.first == "while" || m.first == "do" || m.first == "part")
      continue;
    vals += (vals.empty() ? "" : "  ") + m.first + " " + (m.second.kind == PortJson::STRING ? m.second.s
                                                                                            : PortJsonWrite(m.second));
  }
  T(d, STUDIO_FONT_MONO, 9.5f, a.x + fW * 0.70f, a.y + P(5.0f), C(TK_INK_OFF), vals, fW * 0.24f);
  ImGui::SetCursorScreenPos(ImVec2(a.x + fW - P(30.0f), a.y));
  ImGui::PushID(static_cast<int>(nAt));
  if (StudioButton("##fxx", "", "x", SB_FLAT, 24.0f, 0.0f, kStudioFsXs, bMine))
    StudioAsk("studio_readout_fx", "\"at\":" + std::to_string(nAt) + ",\"remove\":true,\"sender\":\"you\"");
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + P(26.0f)));
  ImGui::Dummy(ImVec2(fW, P(1.0f)));
}

void FxSection(float fW, bool bMine) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "Its effects", 0.08f);
  ImGui::Dummy(ImVec2(0, P(24.0f)));
  StudioMediaPara("Each effect is one op on one part, set off by an event or held while a test is true, never both. "
                  "Every one is carried by light: no part shakes, bounces or changes size. The ops marked motion move "
                  "text, which the player's Reduce motion leaves out, and a blink is capped at three a second "
                  "whichever way the file sets it.",
                  fW, C(TK_INK3), 11.5f);
  ImGui::Dummy(ImVec2(0, P(8.0f)));
  const PortJson &fx = Fx();
  if (fx.size() == 0) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_INK3), "No effects yet.");
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
  }
  for (size_t i = 0; i < fx.size(); ++i)
    FxRow(i, fx.At(i), fW, bMine);
  ImGui::Dummy(ImVec2(0, P(6.0f)));
  if (StudioButton("##fx+", "An effect", "plus", SB_PLAIN, 26.0f, 0.0f, kStudioFsS, bMine))
    StudioAsk("studio_readout_fx", "\"new\":true,\"sender\":\"you\"");
  ImGui::SameLine();
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(8.0f), C(TK_INK3),
    "a flash on the wireframe when the unit is damaged, which a row then changes");
  ImGui::Dummy(ImVec2(0, P(26.0f)));
  // the events and the ops there are, so a modder need not read the doc for the names
  pos = ImGui::GetCursorScreenPos();
  std::string events, ops;
  for (int i = 0; i < PORT_READOUT_EVENTS; ++i)
    events += std::string(i ? ", " : "") + PortReadoutEventName(i);
  for (int i = 0; i < PORT_READOUT_OPS; ++i)
    ops += std::string(i ? ", " : "") + PortReadoutOpName(i) + (PortReadoutOpMoves(i) ? " (motion)" : "");
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y, C(TK_LABEL), "events");
  T(d, STUDIO_FONT_MONO, 9.5f, pos.x + P(60.0f), pos.y + P(1.0f), C(TK_INK_OFF), events, fW - P(70.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(18.0f), C(TK_LABEL), "ops");
  T(d, STUDIO_FONT_MONO, 9.5f, pos.x + P(60.0f), pos.y + P(19.0f), C(TK_INK_OFF), ops, fW - P(70.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(40.0f), C(TK_LABEL), "while");
  T(d, STUDIO_FONT_MONO, 9.5f, pos.x + P(60.0f), pos.y + P(41.0f), C(TK_INK_OFF),
    "\"life < 0.33\", \"rank >= elite\", \"status.left < 2\"", fW - P(70.0f));
  ImGui::Dummy(ImVec2(fW, P(62.0f)));
}

// ---- the preview ------------------------------------------------------------------------------

// Which parts of the panel come out as words in classic's own controls, and which are painted in
// colour over it. The words go through the controls, so they keep classic's font and places and
// show in eight bits; the colour layer (port_readoutdraw.cpp) paints what a control cannot carry,
// and paints nothing at all where there is no overlay to paint into. Said here because the two
// halves answer differently: a part that is only words will not show a colour a mod asks for, and
// a part that is only painted does not show in the words above.
void WhoPaints(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y, C(TK_LABEL), "the words, in classic's controls");
  T(d, STUDIO_FONT_MONO, 10.0f, pos.x + P(210.0f), pos.y, C(TK_INK2),
    "name, status line, kills (the added lines taking that row in turn, in their short form where it "
    "is narrow), the life and energy where the file names them, and a stat icon's tooltip. While a "
    "roll runs on the life or energy, the readout writes that line itself and counts the figures "
    "from what it said before to what it says now",
    fW - P(220.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(18.0f), C(TK_LABEL), "painted in colour");
  T(d, STUDIO_FONT_MONO, 10.0f, pos.x + P(210.0f), pos.y + P(18.0f), C(TK_INK2),
    "each status's framed icon at its line, the wireframe's mark (its corner with +n, a tint over "
    "it, or the kind's pattern, as status_look's wire_mark asks), the rank's pips in the rank's "
    "exact colour, and, where a design's slot gives the room, the hook's own lines and then a row "
    "per status with its icon, its words and a bar for its time left",
    fW - P(220.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(54.0f), C(TK_LABEL), "roll is the exception");
  T(d, STUDIO_FONT_MONO, 10.0f, pos.x + P(210.0f), pos.y + P(54.0f), C(TK_INK2),
    "it is words and not light: the line it counts is written through the panel's own control, in "
    "classic's format and colours where the mod leaves that line alone and into the mod's pattern "
    "where it gives one, and the line is classic's again when the run ends. So it moves nothing, "
    "which leaves it the one op a player who turned motion off still sees, and the one that shows "
    "on the SDL renderer, where there is no overlay to paint light into",
    fW - P(220.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(90.0f), C(TK_LABEL), "every other op is light");
  T(d, STUDIO_FONT_MONO, 10.0f, pos.x + P(210.0f), pos.y + P(90.0f), C(TK_INK2),
    "flash and light wash the part and fade; beat and glow breathe at their rate with a band of "
    "light round it rather than over it; blink lights it on its beat, capped at three a second; "
    "scan runs a line down it; shimmer crosses it; type fills the line from its start. float is "
    "the one that moves, its own text rising off the part; slide is a band of light in from its "
    "side on the panel, and in a slot's room the status's own row slides with it",
    fW - P(220.0f));
  ImGui::Dummy(ImVec2(fW, P(150.0f)));
  StudioMediaPara("The colour layer wants an overlay to paint into, which the GPU presenter gives; with the SDL "
                  "renderer the readout is the words alone. One routine paints both consoles from the same "
                  "description, so a HUD design's selection slot and the classic console cannot drift.",
                  fW, C(TK_INK3), 11.0f);
  ImGui::Dummy(ImVec2(0, P(8.0f)));
}

void PreviewSection(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "The panel in the game", 0.08f);
  ImGui::Dummy(ImVec2(0, P(24.0f)));
  const bool bGame = DesignInGame();
  if (StudioButton("##shot", "Read it now", "again", SB_PLAIN, 26.0f, 0.0f, kStudioFsS, bGame))
    DesignCall("studio_readout_show", "");
  ImGui::SameLine();
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(8.0f), C(bGame ? TK_INK_OFF : TK_WARN),
    bGame ? "select one unit in the game and read it"
          : "no game is running: Test starts one");
  ImGui::Dummy(ImVec2(0, P(32.0f)));
  StudioMediaPara("A readout draws no panel of its own: it writes its words into the controls classic draws the panel "
                  "with, so the panel keeps classic's font, places and spacing and a part the mod leaves alone is "
                  "drawn as it always was. These are the words the panel asked for, from the readout the last push "
                  "loaded, not from the values above: push the mod and read it again to see an edit.",
                  fW, C(TK_INK3), 11.5f);
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  WhoPaints(fW);
  if (!g_shot.why.empty()) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_WARN), g_shot.why, fW);
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
    return;
  }
  if (!g_shot.bRead) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_INK3), "Nothing read yet.");
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
    return;
  }
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(g_shot.bDraws ? TK_GOOD : TK_INK3),
    g_shot.bDraws ? "The mod's readout is writing the panel's words"
                  : "Classic's own panel: no readout loaded, or the hook gave none for this unit");
  ImGui::Dummy(ImVec2(fW, P(24.0f)));
  if (!g_shot.name.empty() || !g_shot.rank.empty()) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_LABEL), "the unit");
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x + P(110.0f), pos.y, C(TK_INK),
      g_shot.name + (g_shot.rank.empty() ? "" : "   rank " + g_shot.rank));
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
  }
  // each part's words: the mod's, or classic's where the mod leaves the part alone
  for (int i = 0; i < PORT_READOUT_PARTS; ++i) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(3.0f), C(TK_LABEL), PortReadoutPartName(i));
    const bool bOwn = !g_shot.aWords[i].empty();
    T(d, STUDIO_FONT_MONO, 10.5f, pos.x + P(110.0f), pos.y + P(2.0f), C(bOwn ? TK_INK : TK_INK_OFF),
      bOwn ? g_shot.aWords[i] : std::string("classic's own"), fW - P(120.0f));
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
  }
  for (int i = 0; i < PORT_READOUT_PARTS; ++i) {
    if (g_shot.aPartTips[i].empty())
      continue;
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(3.0f), C(TK_LABEL),
      std::string(PortReadoutPartName(i)) + "'s tip");
    T(d, STUDIO_FONT_MONO, 10.5f, pos.x + P(110.0f), pos.y + P(2.0f), C(TK_INK2), g_shot.aPartTips[i],
      fW - P(120.0f));
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
  }
  if (!g_shot.row.empty()) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(3.0f), C(TK_LABEL), "the row shows");
    T(d, STUDIO_FONT_MONO, 10.5f, pos.x + P(110.0f), pos.y + P(2.0f), C(TK_HI),
      g_shot.row + "   (the kills' row, taken in turn)", fW - P(120.0f));
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
  }
  for (int i = 0; i < PORT_READOUT_SLOTS_MOST; ++i) {
    if (g_shot.aTips[i].empty())
      continue;
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(3.0f), C(TK_LABEL),
      std::string("icon ") + std::to_string(i + 1) + "'s tip");
    T(d, STUDIO_FONT_MONO, 10.5f, pos.x + P(110.0f), pos.y + P(2.0f), C(TK_INK2), g_shot.aTips[i],
      fW - P(120.0f));
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
  }
  if (!g_shot.room.empty()) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(3.0f), C(TK_LABEL), "the room");
    T(d, STUDIO_FONT_MONO, 10.5f, pos.x + P(110.0f), pos.y + P(2.0f), C(TK_INK2), g_shot.room,
      fW - P(120.0f));
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
  }
  if (!g_shot.runs.empty()) {
    ImGui::Dummy(ImVec2(0, P(8.0f)));
    pos = ImGui::GetCursorScreenPos();
    Wide(d, 10.0f, pos.x, pos.y, C(TK_HEAD), "The effects running", 0.08f);
    ImGui::Dummy(ImVec2(0, P(20.0f)));
    StudioMediaPara("What an event set off, then what a test holds. The player's Reduce motion is already "
                    "applied, so a motion op missing here is one their own setting left out.",
                    fW, C(TK_INK3), 11.0f);
    ImGui::Dummy(ImVec2(0, P(4.0f)));
    StudioMediaPara(g_shot.runs, fW, C(TK_INK2), 11.5f);
    ImGui::Dummy(ImVec2(0, P(8.0f)));
  }
  if (!g_shot.lines.empty()) {
    ImGui::Dummy(ImVec2(0, P(8.0f)));
    pos = ImGui::GetCursorScreenPos();
    Wide(d, 10.0f, pos.x, pos.y, C(TK_HEAD), "The lines the hook returned", 0.08f);
    ImGui::Dummy(ImVec2(0, P(20.0f)));
    StudioMediaPara("The engine reads name, rank, lines, deltas and tooltips from the hook's answer, a line's "
                    "own text, colour and status, and in deltas and tooltips the six stat names. Anything "
                    "else does nothing, and a game names it once in hyperion_mods.log: that warning wants a "
                    "game, so it is not in the rows above, which read the file and never call the hook.",
                    fW, C(TK_INK3), 11.0f);
    ImGui::Dummy(ImVec2(0, P(4.0f)));
    StudioMediaPara(g_shot.lines, fW, C(TK_INK2), 11.5f);
    ImGui::Dummy(ImVec2(0, P(8.0f)));
  }
}

}  // namespace

void StudioReadoutPage(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const bool bMine = StudioIsOpen();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 18.0f, pos.x, pos.y, C(TK_HEAD), "Unit readout");
  float x = pos.x + TW(STUDIO_FONT_BOLD, 18.0f, "Unit readout") + P(12.0f);
  const int nChanged = Changed();
  if (!HasAny())
    StudioChip(d, ImVec2(x, pos.y + P(3.0f)), "classic's own panel", CH_PLAIN);
  else
    x += StudioChip(d, ImVec2(x, pos.y + P(3.0f)),
                    (std::to_string(nChanged) + (nChanged == 1 ? " change" : " changes")).c_str(), CH_GOOD) +
         P(6.0f);
  ImGui::Dummy(ImVec2(fW, P(28.0f)));
  StudioMediaPara("readout.json is the one unit's readout: the panel showing the selected unit's wireframe, name, "
                  "status line, kills, stat icons, life and energy. The template is classic's own panel, and the mod "
                  "changes the parameters it wants and leaves the rest, so every row below shows classic's value with "
                  "the mod's over it. With no readout.json the panel is classic's, pixel for pixel. One readout serves "
                  "both consoles: the classic console puts it where it always does and a HUD design's selection slot "
                  "shows it as its pixels. Presentation only, like a look: in no replay and no save.",
                  fW, C(TK_INK2), 13.0f);
  ImGui::Dummy(ImVec2(0, P(10.0f)));

  // what the loader says of the file as it stands, which is the loader's own reading of it
  const Read &read = Reading();
  if (!read.why.empty() || !read.warnings.empty()) {
    pos = ImGui::GetCursorScreenPos();
    const size_t n = read.warnings.size() + (read.why.empty() ? 0 : 1);
    const bool bBad = !read.why.empty();
    d->AddRectFilled(pos, ImVec2(pos.x + fW, pos.y + P(22.0f) + n * P(18.0f)), C(bBad ? TK_BAD : TK_WARN, 0.12f),
                     P(4.0f));
    T(d, STUDIO_FONT_BOLD, kStudioFsS, pos.x + P(10.0f), pos.y + P(5.0f), C(bBad ? TK_BAD : TK_WARN),
      bBad ? "The loader does not read this file, so the panel stays classic's:"
           : "What the loader passes over, which it names in hyperion_mods.log:");
    float y = pos.y + P(24.0f);
    if (bBad) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(18.0f), y, C(TK_INK2), read.why, fW - P(30.0f));
      y += P(18.0f);
    }
    for (const std::string &w : read.warnings) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(18.0f), y, C(TK_INK2), w, fW - P(30.0f));
      y += P(18.0f);
    }
    ImGui::Dummy(ImVec2(fW, P(30.0f) + n * P(18.0f)));
  }

  // the parts as a row, the one picked written out
  pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "Its parts", 0.08f);
  ImGui::Dummy(ImVec2(0, P(24.0f)));
  for (int i = 0; i < kPartCount; ++i) {
    ImGui::PushID(i);
    int nSet = 0;
    for (int k = 0; k < kParts[i].nParams; ++k)
      if (Value(kParts[i], kParts[i].pParams[k]).kind != PortJson::NONE)
        ++nSet;
    if (StudioButton("##p", kParts[i].pszLabel, nullptr, g_nPart == i ? SB_ON : nSet > 0 ? SB_PLAIN : SB_FLAT, 26.0f))
      g_nPart = i;
    ImGui::PopID();
    if (i + 1 < kPartCount)
      ImGui::SameLine();
  }
  ImGui::Dummy(ImVec2(0, P(16.0f)));
  if (g_nPart < 0 || g_nPart >= kPartCount)
    g_nPart = 0;
  const Part &part = kParts[g_nPart];
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 14.0f, pos.x, pos.y, C(TK_INK), part.pszLabel);
  if (part.pszWhere[0] != '\0')
    T(d, STUDIO_FONT_MONO, 9.5f, pos.x + TW(STUDIO_FONT_BOLD, 14.0f, part.pszLabel) + P(12.0f), pos.y + P(3.0f),
      C(TK_INK3), std::string("classic puts it at ") + part.pszWhere);
  ImGui::Dummy(ImVec2(fW, P(26.0f)));
  if (strcmp(part.pszKey, "lines") == 0) {
    StudioMediaPara("The lines a mod adds past classic's parts, which want add.lines to be there at all. On the "
                    "classic console they take the kills' line in turn with it, since that is the room classic leaves "
                    "free; a HUD slot taller than 92 gives spare rows under the classic parts, and there each line can "
                    "have its own.",
                    fW, C(TK_INK3), 11.5f);
    ImGui::Dummy(ImVec2(0, P(8.0f)));
  }
  for (int i = 0; i < part.nParams; ++i)
    ParamRow(part, part.pParams[i], fW, bMine);
  ImGui::Dummy(ImVec2(0, P(18.0f)));

  // the ranks a hook may give a unit, which "colour": "rank" and "while": "rank >= elite" read
  pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "Its ranks", 0.08f);
  ImGui::Dummy(ImVec2(0, P(22.0f)));
  StudioMediaPara("Each rank the hook may give a unit and the colour it is marked in, which \"colour\": \"rank\" and "
                  "\"while\": \"rank >= elite\" read from, and the words a player reads where the id is not "
                  "them. The order here is the order they compare in.",
                  fW, C(TK_INK3), 11.5f);
  ImGui::Dummy(ImVec2(0, P(6.0f)));
  const PortJson &ranks = Readout()["ranks"];
  for (const PortJsonMember &m : ranks.members) {
    pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(m.first.c_str());
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y + P(5.0f), C(TK_INK), m.first);
    // a rank is its colour, or an object with the colour and the words a player reads
    const bool bWhole = m.second.kind == PortJson::OBJECT;
    const std::string colour = bWhole ? m.second["colour"].Str() : m.second.Str();
    ImGui::SetCursorScreenPos(ImVec2(pos.x + P(160.0f), pos.y));
    char sz[16];
    snprintf(sz, sizeof sz, "%s", colour.c_str());
    if (StudioInput("##rk", sz, sizeof sz, P(100.0f), P(24.0f), "#rrggbb")) {
      std::string why;
      std::vector<std::string> path = {"readout", "ranks", m.first};
      if (bWhole)
        path.push_back("colour");
      Put(path, sz[0] == '\0' ? PortJson() : Str(sz), "set the rank " + m.first + "'s colour", "you", &why);
    }
    unsigned n = 0xFFFFFF;
    if (colour.size() == 7)
      sscanf(colour.c_str() + 1, "%6x", &n);
    d->AddRectFilled(ImVec2(pos.x + P(268.0f), pos.y + P(4.0f)), ImVec2(pos.x + P(284.0f), pos.y + P(20.0f)),
                     IM_COL32((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF, 255), P(2.0f));
    // the words a player reads, where the id is not what to show
    ImGui::SetCursorScreenPos(ImVec2(pos.x + P(292.0f), pos.y));
    char szName[48];
    snprintf(szName, sizeof szName, "%s", bWhole ? m.second["name"].Str().c_str() : "");
    if (StudioInput("##rkn", szName, sizeof szName, P(140.0f), P(24.0f), m.first.c_str())) {
      std::string why;
      if (szName[0] == '\0' && bWhole) {
        Put({"readout", "ranks", m.first}, Str(colour), "the rank " + m.first + " by its colour alone", "you",
            &why);
      } else if (szName[0] != '\0') {
        PortJson rec;
        rec.kind = PortJson::OBJECT;
        PortJsonMember c, nm;
        c.first = "colour";
        c.second = Str(colour.empty() ? "#ffffff" : colour);
        nm.first = "name";
        nm.second = Str(szName);
        rec.members.push_back(c);
        rec.members.push_back(nm);
        Put({"readout", "ranks", m.first}, rec, "set what the rank " + m.first + " reads as", "you", &why);
      }
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x + P(440.0f), pos.y));
    if (StudioButton("##rkx", "", "x", SB_FLAT, 24.0f, 0.0f, kStudioFsXs, bMine)) {
      std::string why;
      Put({"readout", "ranks", m.first}, PortJson(), "took away the rank " + m.first, "you", &why);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + P(28.0f)));
    ImGui::Dummy(ImVec2(fW, P(1.0f)));
  }
  {
    static char szRank[48] = "";
    pos = ImGui::GetCursorScreenPos();
    StudioInput("##newrank", szRank, sizeof szRank, P(140.0f), P(24.0f), "a rank's name");
    ImGui::SameLine();
    if (StudioButton("##rk+", "Add", "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, bMine && szRank[0] != '\0')) {
      std::string why;
      Put({"readout", "ranks", szRank}, Str("#ffffff"), std::string("added the rank ") + szRank, "you", &why);
      szRank[0] = '\0';
    }
    ImGui::Dummy(ImVec2(fW, P(24.0f)));
  }
  ImGui::Dummy(ImVec2(0, P(14.0f)));

  FxSection(fW, bMine);
  PreviewSection(fW);
}

namespace {

// ---- the commands -----------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

const Part *PartOf(const std::string &key) {
  for (int i = 0; i < kPartCount; ++i)
    if (key == kParts[i].pszKey)
      return &kParts[i];
  return nullptr;
}

const Param *ParamOf(const Part &p, const std::string &key) {
  for (int i = 0; i < p.nParams; ++i)
    if (key == p.pParams[i].pszKey)
      return &p.pParams[i];
  return nullptr;
}

bool CmdReadout(const PortJson &, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const Read &read = Reading();
  std::string warnings;
  for (const std::string &w : read.warnings)
    warnings += std::string(warnings.empty() ? "" : ",") + Q(w);
  std::string parts;
  for (int i = 0; i < kPartCount; ++i) {
    std::string set;
    for (int k = 0; k < kParts[i].nParams; ++k) {
      const PortJson &v = Value(kParts[i], kParts[i].pParams[k]);
      if (v.kind != PortJson::NONE)
        set += std::string(set.empty() ? "" : ",") + Q(kParts[i].pParams[k].pszKey) + ":" + PortJsonWrite(v);
    }
    parts += std::string(parts.empty() ? "" : ",") + Q(kParts[i].pszKey[0] == '\0' ? "own" : kParts[i].pszKey) + ":{" +
             set + "}";
  }
  *pAnswer = "\"has_readout\":" + std::string(HasAny() ? "true" : "false") + ",\"changes\":" +
             std::to_string(Changed()) + ",\"reads\":" + std::string(read.bOk ? "true" : "false") + ",\"why\":" +
             Q(read.why) + ",\"warnings\":[" + warnings + "],\"parts\":{" + parts + "},\"ranks\":" +
             PortJsonWrite(Readout()["ranks"]) + ",\"fx\":" + PortJsonWrite(Fx());
  return true;
}

bool CmdReadoutSet(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const std::string key = j["field"].Str(), sender = Sender(j);
  const std::string part = j["part"].Str();
  const Part *pp = PartOf(part);
  if (pp == nullptr) {
    std::string names;
    for (int i = 1; i < kPartCount; ++i)
      names += std::string(names.empty() ? "" : ", ") + kParts[i].pszKey;
    *pAnswer = "part is \"\" for the file's own keys, or one of " + names;
    return false;
  }
  if (key.empty()) {
    std::string names;
    for (int i = 0; i < pp->nParams; ++i)
      names += std::string(names.empty() ? "" : ", ") + pp->pParams[i].pszKey;
    *pAnswer = std::string(pp->pszLabel) + " takes " + names;
    return false;
  }
  if (ParamOf(*pp, key) == nullptr) {
    *pAnswer = std::string(pp->pszLabel) + " has no parameter " + key;
    return false;
  }
  const bool bRevert = j["revert"].kind == PortJson::BOOL && j["revert"].b;
  std::string why;
  if (!Put(PathOf(*pp, key.c_str()), bRevert ? PortJson() : j["value"],
           (bRevert ? "gave the readout's " : "set the readout's ") + key, sender, &why)) {
    *pAnswer = why;
    return false;
  }
  const Read &read = Reading();
  *pAnswer = std::string(bRevert ? "\"reverted\":" : "\"set\":") + Q(key) + ",\"reads\":" +
             (read.bOk ? "true" : "false") + ",\"warnings\":" + std::to_string(read.warnings.size()) +
             ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// An effect made, changed or taken away. A new one is a flash on the wireframe when the unit is
// damaged, which a row then changes.
bool CmdReadoutFx(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const std::string sender = Sender(j);
  std::string why;
  if (j["new"].kind == PortJson::BOOL && j["new"].b) {
    const size_t at = Fx().size();
    PortJson one;
    one.kind = PortJson::OBJECT;
    PortJson rec = j["value"];
    if (rec.kind != PortJson::OBJECT) {
      rec = one;
      PortJsonMember a, b, c;
      a.first = "on";
      a.second = Str("damaged");
      b.first = "part";
      b.second = Str("wire");
      c.first = "do";
      c.second = Str("flash");
      rec.members.push_back(a);
      rec.members.push_back(b);
      rec.members.push_back(c);
    }
    if (!Put({"readout", "fx", std::to_string(at)}, rec, "added an effect to the readout", sender, &why)) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"added\":" + std::to_string(at) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  const int nAt = static_cast<int>(j["at"].Num(-1));
  if (nAt < 0 || nAt >= static_cast<int>(Fx().size())) {
    *pAnswer = "at is an effect's place, 0 to " + std::to_string(Fx().size() ? Fx().size() - 1 : 0);
    return false;
  }
  if (j["remove"].kind == PortJson::BOOL && j["remove"].b) {
    if (!Put({"readout", "fx", std::to_string(nAt)}, PortJson(), "took an effect off the readout", sender, &why)) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"removed\":" + std::to_string(nAt) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  const std::string key = j["field"].Str();
  if (key.empty()) {
    *pAnswer = "\"fx\":" + PortJsonWrite(Fx().At(static_cast<size_t>(nAt)));
    return true;
  }
  if (!Put({"readout", "fx", std::to_string(nAt), key}, j["value"], "set an effect's " + key, sender, &why)) {
    *pAnswer = why;
    return false;
  }
  const Read &read = Reading();
  *pAnswer = "\"set\":" + Q(key) + ",\"reads\":" + (read.bOk ? "true" : "false") + ",\"warnings\":" +
             std::to_string(read.warnings.size()) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// The panel's words now. This runs on the game's thread, which is where a part may be asked for
// its words and the selection read at all.
bool CmdReadoutShow(const PortJson &, std::string *pAnswer) {
  g_shot = Shot();
  if (!DesignInGame()) {
    g_shot.why = "no game is running: the Test workspace starts one";
    *pAnswer = g_shot.why;
    return false;
  }
  int anSel[2] = {-1, -1};
  if (PortBenchSelection(anSel, 2) < 1) {
    g_shot.why = "select one unit in the game: the readout is the one unit's panel";
    *pAnswer = g_shot.why;
    return false;
  }
  g_shot.bRead = true;
  g_shot.bDraws = PortReadoutDraws();
  // each part asked for its words as the panel's own control asks, with no classic words to keep:
  // a part the mod leaves alone answers null, which is drawn as classic's own
  for (int i = 0; i < PORT_READOUT_PARTS; ++i) {
    const char *psz = PortReadoutPartText(i, nullptr);
    g_shot.aWords[i] = psz == nullptr ? std::string() : psz;
  }
  // which status the kills' row is showing at this moment, which the drawing reads too, so the
  // icon it puts at the line's start and these words are always the same status
  const PortReadoutLine *pRow = PortReadoutRowLine();
  if (pRow != nullptr)
    g_shot.row = pRow->label.empty() ? pRow->text : pRow->label;
  const CUnit *pUnit = CUnit::Head() + anSel[0];
  const PortReadoutView *pView = PortReadoutUnit(pUnit);
  if (pView != nullptr) {
    g_shot.name = pView->name;
    g_shot.rank = pView->rank;
    for (size_t i = 0; i < pView->lines.size(); ++i) {
      const PortReadoutLine &l = pView->lines[i];
      char szLeft[32] = "";
      if (l.fLeft > 0)
        snprintf(szLeft, sizeof szLeft, "  %.1fs left", l.fLeft);
      g_shot.lines += (g_shot.lines.empty() ? "" : "\n") + (l.label.empty() ? l.text : l.label) +
                      (l.nStacks > 1 ? " x" + std::to_string(l.nStacks) : "") + szLeft +
                      (l.effect.empty() ? "" : "  (" + l.effect + ")");
    }
    // what a rolled change came to, which the stat icons may carry
    for (int i = 0; i < PORT_READOUT_STATS; ++i)
      if (pView->abDelta[i])
        g_shot.lines += (g_shot.lines.empty() ? "" : "\n") + std::string(PortReadoutStatName(i)) + " rolled " +
                        (pView->anDelta[i] > 0 ? "+" : "") + std::to_string(pView->anDelta[i]);
  }
  for (int i = 0; i < PORT_READOUT_SLOTS_MOST; ++i) {
    std::string tip;
    bool bWhole = false;
    if (PortReadoutSlotTip(pUnit, i, &tip, &bWhole))
      g_shot.aTips[i] = tip + (bWhole ? "   (the whole tooltip)" : "   (under classic's own)");
  }
  // the wireframe's and the name's own tooltips, which carry the life, the speed and the rolled
  // changes that have no stat icon to sit on
  for (int nPart = 0; nPart < PORT_READOUT_PARTS; ++nPart) {
    std::string tip;
    bool bWhole = false;
    if (PortReadoutPartTip(pUnit, nPart, &tip, &bWhole))
      g_shot.aPartTips[nPart] = tip + (bWhole ? "   (the whole tooltip)" : "   (under classic's own)");
  }
  // the room the added lines go in, which a HUD design's slot gives and the classic console does
  // not, and whether that room lists the statuses itself or leaves them to the panel's own row
  double fRx = 0, fRy = 0, fRw = 0, fRh = 0;
  if (PortReadoutRoomBox(&fRx, &fRy, &fRw, &fRh)) {
    char sz[160] = "";
    snprintf(sz, sizeof sz, "%.0f by %.0f pixels at (%.0f, %.0f), %s", fRw, fRh, fRx, fRy,
             PortReadoutRoomLists() ? "listing the statuses a row each"
                                    : "the hook's lines only, the panel's row rotating the statuses");
    g_shot.room = sz;
  } else {
    g_shot.room = "none: the panel has no room beside or under it, so the added lines take the kills' row in turn";
  }
  // the effects running on each part at this moment: what an event set off, then what a test
  // holds. Reduce motion is already applied in the answer, so a motion op missing here is one the
  // player's own setting left out
  for (int nPart = 0; nPart < PORT_READOUT_PARTS; ++nPart) {
    PortReadoutRun aRuns[PORT_READOUT_RUNS_MOST];
    const int nRuns = PortReadoutRuns(nPart, nullptr, aRuns, PORT_READOUT_RUNS_MOST);
    for (int i = 0; i < nRuns; ++i) {
      const PortReadoutRun &r = aRuns[i];
      char sz[192] = "";
      snprintf(sz, sizeof sz, "%s  %s  %s%s%s%s", PortReadoutPartName(nPart), PortReadoutOpName(r.nOp),
               r.bHeld ? "held" : "set off", r.bHeld ? "" : (" " + Fmt(r.fThrough * 100, 0) + "% through").c_str(),
               r.szStatus[0] != '\0' ? "  by " : "", r.szStatus);
      g_shot.runs += (g_shot.runs.empty() ? "" : "\n") + std::string(sz) +
                     (r.szText[0] != '\0' ? std::string("  \"") + r.szText + "\"" : std::string()) +
                     (r.bColour ? "  its own colour" : "") +
                     (PortReadoutOpMoves(r.nOp) ? "  (motion)" : "");
    }
  }
  if (!g_shot.bDraws)
    g_shot.lines = "The readout is not writing the panel: push the mod and read it again.";
  std::string words;
  for (int i = 0; i < PORT_READOUT_PARTS; ++i)
    words += std::string(i ? "," : "") + Q(PortReadoutPartName(i)) + ":" + Q(g_shot.aWords[i]);
  *pAnswer = "\"draws\":" + std::string(g_shot.bDraws ? "true" : "false") + ",\"words\":{" + words +
             "},\"name\":" + Q(g_shot.name) + ",\"rank\":" + Q(g_shot.rank) + ",\"row\":" + Q(g_shot.row) +
             ",\"room\":" + Q(g_shot.room) + ",\"runs\":" + Q(g_shot.runs) + ",\"lines\":" + Q(g_shot.lines);
  return true;
}

}  // namespace

void StudioReadoutInit(void) {
  PortControlCommand("studio_readout", CmdReadout);
  PortControlCommand("studio_readout_set", CmdReadoutSet);
  PortControlCommand("studio_readout_fx", CmdReadoutFx);
  PortControlCommand("studio_readout_show", CmdReadoutShow);
}
