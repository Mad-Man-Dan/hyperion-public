// port_modedecl.cpp -- a mode mod's declaration: the "mode" block of its mod.json read for
// the set-up's Game type list and the game's options (port_mode.h).
//
// The block is read from text each time the list is built, so a mod folder dropped in while
// Hyperion runs shows up after a rescan. Nothing here reaches a game: the chosen mode and
// what it does are port_mode.cpp's.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <utility>

#include "port_mode.h"
#include "port_mods.h"
#include "port_modlang.h"  // ModLangMode
#include "port_modlua.h"   // ModLuaSetupCall: a setting's effect, read and shown
#include "port_modemap.h"  // ModeMapOf: the map a read is given
#include "../platform/port_json.h"
#include "../platform/port_log.h"

namespace {

const PortLogger Log("hyperion_mods.log");

std::vector<PortModeDecl> g_modes;
bool g_bRead;
size_t g_nMods;  // the mods the list was built from, so a rescan builds it again

// A setting's kinds, and what each one's base is when it names none.
const char *const kKinds[] = {"choice", "toggle", "number", "list", "seed"};

// The shapes a mode's preview draws, which is enough for a ring at every start of the largest
// map and some over; the rest are left out rather than drawn over each other.
const size_t kShapesMost = 64;

bool KindKnown(const std::string &kind) {
  for (const char *psz : kKinds)
    if (kind == psz)
      return true;
  return false;
}

// The classic game types a mode may be played as, by the game header's number, which is what
// the set-up hands the launch (port_frontkit.cpp's GameTypeName shows the same list).
const struct {
  const char *psz;
  int n;
} kPlays[] = {{"melee", 0x02},            {"free for all", 0x03},
              {"one on one", 0x04},       {"capture the flag", 0x05},
              {"greed", 0x06},            {"slaughter", 0x07},
              {"sudden death", 0x08},     {"ladder", 0x09},
              {"use map settings", 0x0A}, {"team melee", 0x0B},
              {"team free for all", 0x0C}, {"team capture the flag", 0x0D},
              {"top vs bottom", 0x0F}};

// The number for the name a mode writes, Melee for no name and for one there is none of, which
// it says in the log so a misspelling is not silent.
int PlaysOf(const std::string &id, const std::string &name) {
  if (name.empty())
    return 0x02;
  std::string low = name;
  std::transform(low.begin(), low.end(), low.begin(),
                 [](unsigned char c) { return static_cast<char>(tolower(c)); });
  for (const auto &p : kPlays)
    if (low == p.psz)
      return p.n;
  Log("modes: %s's \"plays\": %s is no game type, so it is played as Melee", id.c_str(),
      name.c_str());
  return 0x02;
}

// One choice or list item: a string is its id and its name both.
PortModeItem ItemOf(const PortJson &j) {
  PortModeItem it;
  if (j.kind == PortJson::STRING) {
    it.id = it.name = j.s;
    return it;
  }
  it.id = j["id"].Str();
  it.name = j["name"].Str(it.id.c_str());
  it.line = j["line"].Str();
  it.bHard = j["hard"].kind == PortJson::BOOL && j["hard"].b;
  return it;
}

bool ReadSetting(const PortJson &j, const std::string &where, PortModeSetting *pOut, std::string *pWhy) {
  PortModeSetting s;
  s.id = j["id"].Str();
  s.kind = j["kind"].Str();
  if (s.id.empty()) {
    *pWhy = where + " has no id";
    return false;
  }
  if (!KindKnown(s.kind)) {
    *pWhy = where + " " + s.id + " is a choice, a toggle, a number, a list or a seed, not " +
            (s.kind.empty() ? "nothing" : s.kind);
    return false;
  }
  s.name = j["name"].Str(s.id.c_str());
  s.group = j["group"].Str();
  s.gist = j["gist"].Str();
  s.effect = j["effect"].Str();
  s.read = j["read"].Str();
  s.shown = j["shown"].Str();
  const PortJson &items = s.kind == "list" ? j["items"] : j["choices"];
  for (size_t i = 0; i < items.size(); ++i) {
    const PortModeItem it = ItemOf(items.At(i));
    if (it.id.empty()) {
      *pWhy = where + " " + s.id + " has a choice with no id";
      return false;
    }
    s.items.push_back(it);
    s.choices.push_back(it.id);
  }
  if ((s.kind == "choice" || s.kind == "list") && s.items.empty()) {
    *pWhy = where + " " + s.id + " has no choices";
    return false;
  }
  if (s.kind == "number") {
    s.min = j["min"].Num(0);
    s.max = j["max"].Num(100);
    s.step = j["step"].Num(1);
    s.unit = j["unit"].Str();
    if (s.step <= 0 || s.max < s.min) {
      *pWhy = where + " " + s.id + " takes a min at or below its max and a step above 0";
      return false;
    }
  }
  // the base, as JSON text; the first choice, false, the min, nothing turned off or 0
  const PortJson &base = j["base"];
  if (base.kind != PortJson::NONE) {
    s.base = PortJsonWrite(base);
  } else if (s.kind == "choice") {
    s.base = PortJsonQuote(s.items.front().id);
  } else if (s.kind == "toggle") {
    s.base = "false";
  } else if (s.kind == "number") {
    char sz[40];
    snprintf(sz, sizeof sz, "%.10g", s.min);
    s.base = sz;
  } else if (s.kind == "list") {
    s.base = "[]";
  } else {
    s.base = "0";
  }
  *pOut = s;
  return true;
}

bool ReadDecl(const PortMod &m, int nMod, PortModeDecl *pOut, std::string *pWhy) {
  PortJson j;
  if (!PortJsonParse(m.modeBlock.c_str(), &j, pWhy) || j.kind != PortJson::OBJECT) {
    *pWhy = "its \"mode\" is an object";
    return false;
  }
  PortModeDecl d;
  d.id = m.id;
  d.name = m.name;
  d.version = m.version;
  d.folder = m.folder;
  d.nMod = nMod;
  d.line = j["line"].Str(m.line.c_str());
  d.about = j["about"].Str();
  // the Game type list's entry: its own name and the mod it comes from unless the mode says
  // otherwise, and Melee unless it names the type a game of it is launched as
  const PortJson &type = j["game_type"];
  d.typeName = type["name"].Str(m.name.c_str());
  d.typeTag = type["tag"].Str(m.name.c_str());
  d.typePlays = PlaysOf(m.id, type["plays"].Str());
  d.needs = j["needs"].Str("any race");
  d.sim = j["sim"].kind != PortJson::BOOL || j["sim"].b;
  d.enemyPlayer = j["enemy_player"].kind == PortJson::BOOL && j["enemy_player"].b;
  const PortJson &players = j["players"];
  if (players.size() == 2) {
    d.playersMin = static_cast<int>(players.At(0).Num(1));
    d.playersMax = static_cast<int>(players.At(1).Num(8));
  }
  if (d.playersMin < 1)
    d.playersMin = 1;
  if (d.playersMax < d.playersMin)
    d.playersMax = d.playersMin;
  for (size_t i = 0; i < j["notes"].size(); ++i)
    d.notes.push_back(j["notes"].At(i).Str());
  d.preview = j["preview"].Str();
  const PortJson &settings = j["settings"];
  for (size_t i = 0; i < settings.size(); ++i) {
    PortModeSetting s;
    if (!ReadSetting(settings.At(i), "a setting of", &s, pWhy))
      return false;
    for (const PortModeSetting &o : d.settings)
      if (o.id == s.id) {
        *pWhy = "two settings use the id " + s.id;
        return false;
      }
    d.settings.push_back(s);
  }
  const PortJson &presets = j["presets"];
  for (size_t i = 0; i < presets.size(); ++i) {
    const PortJson &p = presets.At(i);
    PortModePreset pr;
    pr.id = p["id"].Str();
    pr.name = p["name"].Str(pr.id.c_str());
    pr.line = p["line"].Str();
    pr.values = p["values"].kind == PortJson::OBJECT ? PortJsonWrite(p["values"]) : "{}";
    if (pr.id.empty()) {
      *pWhy = "a preset has no id";
      return false;
    }
    d.presets.push_back(pr);
  }
  // what it needs: a mod's id, or any race at all
  if (d.needs != "any race" && !d.needs.empty()) {
    const std::vector<PortMod> &mods = PortMods();
    bool bHave = false;
    for (const PortMod &o : mods)
      bHave = bHave || o.id == d.needs;
    if (!bHave) {
      d.needsMet = false;
      d.whyNot = "it needs the mod " + d.needs + ", which is not installed";
    }
  }
  // its words in the player's language, where the mod brings them (ModLangMode)
  ModLangMode(d.id, &d);
  *pOut = d;
  return true;
}

void Read() {
  const std::vector<PortMod> &mods = PortMods();
  if (g_bRead && g_nMods == mods.size())
    return;
  g_bRead = true;
  g_nMods = mods.size();
  g_modes.clear();
  for (size_t i = 0; i < mods.size(); ++i) {
    if (mods[i].kind != "mode")
      continue;
    if (mods[i].modeBlock.empty()) {
      Log("modes: %s says \"kind\": \"mode\" and has no \"mode\" block, so it is left out", mods[i].id.c_str());
      continue;
    }
    PortModeDecl d;
    std::string why;
    if (!ReadDecl(mods[i], static_cast<int>(i), &d, &why)) {
      Log("modes: %s is left out: %s", mods[i].id.c_str(), why.c_str());
      continue;
    }
    g_modes.push_back(d);
    Log("modes: %s, %u settings, %u presets, %d to %d players, listed as %s (%s) and played as "
        "type %d%s", d.id.c_str(),
        static_cast<unsigned>(d.settings.size()), static_cast<unsigned>(d.presets.size()), d.playersMin, d.playersMax,
        d.typeName.c_str(), d.typeTag.c_str(), d.typePlays,
        d.needsMet ? "" : " (not available)");
  }
}

}  // namespace

const std::vector<PortModeDecl> &PortModeList() {
  Read();
  return g_modes;
}

const PortModeDecl *PortModeFind(const std::string &id) {
  Read();
  for (const PortModeDecl &d : g_modes)
    if (d.id == id)
      return &d;
  return nullptr;
}

std::string PortModeSettingsBase(const PortModeDecl &d) {
  std::string s = "{";
  for (size_t i = 0; i < d.settings.size(); ++i)
    s += (i ? "," : "") + PortJsonQuote(d.settings[i].id) + ":" + d.settings[i].base;
  return s + "}";
}

// ---- a setting's own words, from the mode's Luau ---------------------------------------------------
// The set-up asks for these every frame it draws the tab, and each call builds a VM and runs the
// mod's scripts, so what each one answered is kept by its arguments until they change.

namespace {

std::string ModeFolder(const PortModeDecl &d) {
  const std::vector<PortMod> &mods = PortMods();
  if (d.nMod < 0 || d.nMod >= static_cast<int>(mods.size()))
    return std::string();
  return mods[d.nMod].folder + mods[d.nMod].data;
}

// What the calls have answered, by the function, the settings and the map (PortModeForget
// empties it, for a tool that has changed the mode's scripts under it).
std::map<std::string, std::pair<std::string, bool>> g_kept;

// The call's answer, kept by what it was asked: the function, the settings and the map.
std::string Answer(const PortModeDecl &d, const std::string &fn, const std::string &id,
                   const std::string &settings, const char *pszMap, bool bMap, bool *pbTrue) {
  if (fn.empty()) {
    if (pbTrue != nullptr)
      *pbTrue = true;
    return std::string();
  }
  const std::string key = d.id + "\n" + fn + "\n" + settings + "\n" + (pszMap != nullptr ? pszMap : "");
  auto it = g_kept.find(key);
  if (it == g_kept.end()) {
    if (g_kept.size() > 256)
      g_kept.clear();
    PortJson values;
    const bool bHave = !settings.empty() && PortJsonParse(settings.c_str(), &values) &&
                       values.kind == PortJson::OBJECT;
    if (!bHave)
      values = PortJson();
    const PortJson *pValue = id.empty() ? nullptr : &values[id.c_str()];
    const ModeMapRead *pMap = bMap ? ModeMapOf(pszMap) : nullptr;
    // a call about one setting is given that setting's value; one about the mode as a whole is
    // given every value. So read(value, map) and preview(values, map) each take two
    const PortJson *pAll = bMap && !id.empty() ? nullptr : &values;
    std::string text, why;
    bool bTrue = true;
    if (!ModLuaSetupCall(ModeFolder(d), fn, pValue, pAll, pMap, &text, &bTrue, &why))
      Log("modes: %s's %s does not run: %s", d.id.c_str(), fn.c_str(), why.c_str());
    it = g_kept.emplace(key, std::make_pair(text, bTrue)).first;
  }
  if (pbTrue != nullptr)
    *pbTrue = it->second.second;
  return it->second.first;
}

}  // namespace

void PortModeForget() {
  g_kept.clear();
}

std::string PortModeEffect(const PortModeDecl &d, const PortModeSetting &s, const std::string &settings) {
  return Answer(d, s.effect, s.id, settings, nullptr, false, nullptr);
}

std::string PortModeRead(const PortModeDecl &d, const PortModeSetting &s, const std::string &settings,
                         const char *pszMap) {
  if (pszMap == nullptr || pszMap[0] == '\0')
    return std::string();
  return Answer(d, s.read, s.id, settings, pszMap, true, nullptr);
}

bool PortModeShown(const PortModeDecl &d, const PortModeSetting &s, const std::string &settings) {
  bool bTrue = true;
  Answer(d, s.shown, std::string(), settings, nullptr, false, &bTrue);
  return bTrue;
}

// The shapes the mode draws of the map: its preview's table, which the call hands back as JSON
// text, read into the shapes the set-up's picture draws. A kind it does not know is left out
// rather than refused, so a mode written for a later build still draws what this one has.
bool PortModePreview(const PortModeDecl &d, const std::string &settings, const char *pszMap,
                     std::vector<PortModeShape> *pOut) {
  pOut->clear();
  if (d.preview.empty() || pszMap == nullptr || pszMap[0] == '\0')
    return false;
  const std::string text = Answer(d, d.preview, std::string(), settings, pszMap, true, nullptr);
  PortJson list;
  if (text.empty() || !PortJsonParse(text.c_str(), &list) || list.kind != PortJson::ARRAY)
    return false;
  static const char *const kapszShapes[] = {"circle", "ring", "rect", "polygon", "line"};
  // every field a shape may name. One it has none of is a mistake in the mod rather than
  // something to pass over quietly: the shape would be drawn without it and nothing would say
  // why, which is the reasoning hx.area's own whitelist follows. Both spellings of the colour
  // are taken, as a mark's are, since a mod's files are written in both
  static const char *const kapszFields[] = {"shape", "x",      "y",    "w",     "h",    "r",
                                            "x2",    "y2",     "points", "colour", "color",
                                            "fill",  "edge",   "width",  "dash"};
  for (size_t i = 0; i < list.size() && pOut->size() < kShapesMost; ++i) {
    const PortJson &one = list.At(i);
    if (one.kind != PortJson::OBJECT)
      continue;
    PortModeShape s;
    s.shape = one["shape"].Str();
    bool bKnown = false;
    for (const char *psz : kapszShapes)
      bKnown = bKnown || s.shape == psz;
    if (!bKnown) {
      Log("modes: %s's %s gives a shape \"%s\", which is none of circle, ring, rect, polygon or "
          "line, so it is left out", d.id.c_str(), d.preview.c_str(), s.shape.c_str());
      continue;
    }
    for (const PortJsonMember &m : one.members) {
      bool bField = false;
      for (const char *psz : kapszFields)
        bField = bField || m.first == psz;
      if (!bField)
        Log("modes: %s's %s gives a \"%s\" shape a field \"%s\", which there is none of, so it is "
            "passed over", d.id.c_str(), d.preview.c_str(), s.shape.c_str(), m.first.c_str());
    }
    s.x = one["x"].Num(0);
    s.y = one["y"].Num(0);
    s.w = one["w"].Num(0);
    s.h = one["h"].Num(0);
    s.r = one["r"].Num(0);
    s.x2 = one["x2"].Num(0);
    s.y2 = one["y2"].Num(0);
    const PortJson &pts = one["points"];
    for (size_t k = 0; k < pts.size(); ++k)
      s.points.push_back(pts.At(k).Num(0));
    s.points.resize(s.points.size() & ~static_cast<size_t>(1));  // a corner wants both numbers
    const PortJson &col = one["colour"].kind != PortJson::NONE ? one["colour"] : one["color"];
    s.nRgb = static_cast<unsigned int>(col.Num(0xffffff)) & 0xffffffu;
    s.fFill = one["fill"].Num(0);
    s.fEdge = one["edge"].Num(1);
    s.fWidth = one["width"].Num(1);
    s.fDash = one["dash"].Num(0);
    pOut->push_back(s);
  }
  return !pOut->empty();
}

std::string PortModeSettingsWhole(const PortModeDecl &d, const std::string &settings) {
  PortJson j;
  static const PortJson kNone;
  const bool bHave = !settings.empty() && PortJsonParse(settings.c_str(), &j) && j.kind == PortJson::OBJECT;
  if (!settings.empty() && !bHave)
    Log("modes: %s's settings do not read as a JSON object, so every setting takes its base: %s",
        d.id.c_str(), settings.c_str());
  std::string s = "{";
  for (size_t i = 0; i < d.settings.size(); ++i) {
    // a text that did not read leaves kNone, so the base below is taken
    const PortJson &v = bHave ? j[d.settings[i].id.c_str()] : kNone;
    s += (i ? "," : "") + PortJsonQuote(d.settings[i].id) + ":" +
         (v.kind == PortJson::NONE ? d.settings[i].base : PortJsonWrite(v));
  }
  return s + "}";
}
