// studio_testloop.cpp -- the Test workspace (the design's mock, the game pane and Try it): the working
// mod pushed into the canvas game (studio_push, mod_push), the pushes with each one's state and
// answer, and tries: the mod's unit and a scenario's units spawned facing each other, the start
// point taken there (design_start_here) so Again (F9) puts the same fight back, with pause and step.
// A refused push names the field the loader stopped at, and opens it; the shell places the game.
// The modder's own scenarios are files of the mod, scenarios\<name>.json, written once saved.
//
//   studio_try {unit, against?, count?, yours?, units?}   the mod pushed, then the unit tried against a scenario
//   studio_test {again?, pause?, resume?, step?}          the try's state; Again, pause, go on or a step
//   studio_scenarios {}                                   the mod's own scenarios
//   studio_scenario_save {name, sides?, tried?}           one saved, from its sides or from the last try
//   studio_scenario_delete {name}                         one taken away
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <map>
#include <vector>
#include "../design/design_engine.h"     // DesignCall
#include "../design/design_hooks.h"      // DesignInGame, DesignGamePaused, DesignGameFrame
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_ab.h"
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// ---- tries ---------------------------------------------------------------------------------------------------

struct Side {
  std::string unit;
  int nCount = 1;
  int nPlayer = 1;      // 0 the mod's side, 1 the other
  bool bAttack = true;  // sent at the other side as it starts
};

struct Scenario {
  const char *pszKey;
  const char *pszLabel;
  const char *pszUnit;
  int nCount;
};
const Scenario kScenarios[] = {{"zealots", "vs 4 Zealots", "protoss_zealot", 4},
                               {"marines", "vs 6 Marines", "terran_marine", 6},
                               {"ultralisk", "vs 1 Ultralisk", "zerg_ultralisk", 1}};
enum { SC_CUSTOM = 3 };

// Where the sides stand on the canvas: the mod's side at the left, facing the other at the right.
const int kLeftX = 960, kRightX = 1180, kY = 1024;

struct Try {
  int nGen = 0;          // a try started after drops the answers still on their way to an older one
  std::string unit;      // the unit tried
  int nYours = 1;
  bool bYoursAttack = false;  // the unit tried sent at the other side too
  std::vector<Side> sides;
  std::string label;     // in words
  std::string state;     // "", "pushing", "refused", "missing", "playing", "no game"
  std::string why;
  int nPush = 0;
  unsigned nChange = 0;  // the model's change it pushed
  bool bStart = false;   // a start point taken, so Again puts it back
  unsigned nStartFrame = 0;
  int nAgains = 0;
} g_try;

std::string FieldsOf(const PortJson &a) {
  return a["error"].Str("refused");
}

void Spawned(int nGen);

// Each poll of the channel until the push is answered: one small request a round, so the wait
// needs no frame of its own.
void WaitPush(int nGen) {
  if (nGen != g_try.nGen)
    return;
  std::string state, answer;
  unsigned nChange = 0;
  if (!StudioPushInfo(g_try.nPush, &state, &answer, &nChange)) {
    g_try.state = "refused";
    g_try.why = "the push is not in the list";
    return;
  }
  if (state == "asked") {
    DesignCall("studio_status", "", [nGen](const PortJson &) { WaitPush(nGen); });
    return;
  }
  PortJson a;
  std::string err;
  PortJsonParse(answer.c_str(), &a, &err);
  if (state == "refused") {
    g_try.state = "refused";
    g_try.why = a["error"].Str("refused");
    return;
  }
  g_try.nChange = nChange;
  // a unit of the mod's own is in the push by its name
  StudioRecord r;
  if (StudioRecordOf(ST_UNITS, g_try.unit, &r) && r.kind == SK_OWN) {
    bool bIn = false;
    for (const PortJson &u : a["units"].items)
      bIn = bIn || u["name"].Str() == g_try.unit;
    if (!bIn) {
      g_try.state = "missing";
      g_try.why = g_try.unit + " was not in the push";
      return;
    }
  }
  // the field cleared, both sides spawned by name (a name finds the newest push's record), the
  // camera between them, and the start point taken once they stand
  DesignCall("clear", "\"player\":0");
  DesignCall("clear", "\"player\":1");
  DesignCall("spawn", "\"unit\":" + Q(g_try.unit) + ",\"x\":" + std::to_string(kLeftX) + ",\"y\":" + std::to_string(kY) +
                          ",\"player\":0,\"count\":" + std::to_string(std::max(1, g_try.nYours)) +
                          (g_try.bYoursAttack ? ",\"attack\":[" + std::to_string(kRightX) + "," + std::to_string(kY) + "]" : std::string()),
             [nGen](const PortJson &s) {
               if (nGen == g_try.nGen && !(s["ok"].kind == PortJson::BOOL && s["ok"].b))
                 g_try.why = "the unit did not spawn: " + s["error"].Str("refused");
             });
  int nRow[2] = {0, 0};
  for (const Side &s : g_try.sides) {
    const int p = s.nPlayer == 0 ? 0 : 1;
    // rows put above and below the middle in turn
    const int k = nRow[p]++;
    const int x = p == 0 ? kLeftX - 64 : kRightX, y = kY + ((k + 1) / 2) * 56 * (k % 2 ? 1 : -1);
    const int tx = p == 0 ? kRightX : kLeftX;
    DesignCall("spawn", "\"unit\":" + Q(s.unit) + ",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"player\":" +
                            std::to_string(p) + ",\"count\":" + std::to_string(std::max(1, std::min(64, s.nCount))) +
                            (s.bAttack ? ",\"attack\":[" + std::to_string(tx) + "," + std::to_string(kY) + "]" : std::string()));
  }
  DesignCall("camera", "\"x\":" + std::to_string((kLeftX + kRightX) / 2) + ",\"y\":" + std::to_string(kY));
  DesignCall("design_start_here", "", [nGen](const PortJson &s) {
    if (nGen != g_try.nGen)
      return;
    g_try.bStart = s["ok"].kind == PortJson::BOOL && s["ok"].b;
    g_try.nStartFrame = static_cast<unsigned>(s["start"].Num(0));
    Spawned(nGen);
  });
  g_try.state = "playing";
}

void Spawned(int nGen) {
  if (nGen == g_try.nGen)
    StudioLog("info", "trying " + g_try.label + (g_try.bStart ? "; F9 puts it back" : ""));
}

// A try: the working mod pushed (each earlier push's units turned into the newest), then spawned.
void TryStart(const std::string &unit, int nYours, const std::vector<Side> &sides, const std::string &label, bool bYoursAttack = false) {
  const int nGen = g_try.nGen + 1;
  g_try = Try();
  g_try.nGen = nGen;
  g_try.unit = unit;
  g_try.nYours = nYours;
  g_try.bYoursAttack = bYoursAttack;
  g_try.sides = sides;
  g_try.label = label;
  if (unit.empty()) {
    g_try.state = "refused";
    g_try.why = "no unit to try";
    return;
  }
  if (!DesignInGame() || !StudioIsOpen()) {
    g_try.state = "no game";
    g_try.why = StudioIsOpen() ? "the canvas game is not on yet" : "no mod is open";
    return;
  }
  g_try.state = "pushing";
  DesignCall("studio_push", "\"replace\":\"all\",\"sender\":\"you\"", [nGen](const PortJson &a) {
    if (nGen != g_try.nGen)
      return;
    if (!(a["ok"].kind == PortJson::BOOL && a["ok"].b)) {
      g_try.state = "refused";
      g_try.why = FieldsOf(a);
      return;
    }
    g_try.nPush = static_cast<int>(a["push"].Num(0));
    WaitPush(nGen);
  });
}

// Again: the start point put back while nothing was pushed since, else the try made again.
void Again(void) {
  if (g_try.unit.empty())
    return;
  if (g_try.state == "playing" && g_try.bStart && StudioPushCount() == g_try.nPush && StudioChange() == g_try.nChange) {
    ++g_try.nAgains;
    DesignCall("design_again", "", [](const PortJson &a) {
      if (!(a["ok"].kind == PortJson::BOOL && a["ok"].b))
        StudioSay("Again: " + a["error"].Str("refused"));
    });
    return;
  }
  const Try was = g_try;
  TryStart(was.unit, was.nYours, was.sides, was.label, was.bYoursAttack);
}

// ---- the modder's own scenarios ---------------------------------------------------------------------------------

// A scenario of the mod's: its sides' units, player 0 the side the unit tried joins, and whether
// each side is sent at the other as it starts. In its file the sides are in the modder's order,
// "tried" naming the one the unit joins:
//   {"name": "...", "tried": 0, "sides": [{"units": [{"unit": "terran_marine", "count": 6}], "order": "attack"}, {...}]}
struct Saved {
  std::string name;
  std::string file;  // its file's name in scenarios\, without .json
  std::vector<Side> sides;
  bool bAttack[2] = {true, true};
};

std::string ScenarioFolder(void) {
  return StudioModFolder() + "scenarios\\";
}

// a file's name from a scenario's: letters and digits kept, the rest a dash
std::string FileOf(const std::string &name) {
  std::string out;
  for (char ch : name) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (isalnum(c))
      out += static_cast<char>(tolower(c));
    else if (!out.empty() && out.back() != '-')
      out += '-';
  }
  while (!out.empty() && out.back() == '-')
    out.pop_back();
  return out.substr(0, 60);
}

bool ReadText(const std::string &path, std::string *pText) {
  FILE *f = fopen(path.c_str(), "rb");
  if (f == nullptr)
    return false;
  char buf[4096];
  size_t n;
  pText->clear();
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    pText->append(buf, n);
  fclose(f);
  return true;
}

// A scenario from its JSON; false with why.
bool SavedFrom(const PortJson &j, const std::string &file, Saved *pOut, std::string *pWhy) {
  Saved sc;
  sc.file = file;
  sc.name = j["name"].Str(file.c_str());
  const PortJson &sides = j["sides"];
  if (sides.kind != PortJson::ARRAY || sides.size() < 1 || sides.size() > 2) {
    *pWhy = "sides is a list of one or two sides, each {units: [{unit, count}], order?}";
    return false;
  }
  const int nTried = static_cast<int>(j["tried"].Num(0));
  if (nTried < 0 || nTried >= static_cast<int>(sides.size())) {
    *pWhy = "tried is the place of the side the unit joins, 0 or 1";
    return false;
  }
  for (size_t i = 0; i < sides.size(); ++i) {
    const int nPlayer = static_cast<int>(i) == nTried ? 0 : 1;
    const PortJson &side = sides.At(i);
    const std::string order = side["order"].Str();
    if (!order.empty() && order != "attack" && order != "none") {
      *pWhy = "a side's order is \"attack\" or none";
      return false;
    }
    sc.bAttack[nPlayer] = order == "attack";
    if (side["units"].size() > 8) {
      *pWhy = "a side has eight rows at most";
      return false;
    }
    for (const PortJson &u : side["units"].items) {
      StudioRecord r;
      const std::string unit = u["unit"].Str();
      if (!StudioRecordOf(ST_UNITS, unit, &r)) {
        *pWhy = unit.empty() ? "each row is {unit, count}, its unit by name" : "no unit " + unit;
        return false;
      }
      const int nCount = static_cast<int>(u["count"].Num(1));
      if (nCount < 1 || nCount > 64) {
        *pWhy = "a row's count is 1 to 64";
        return false;
      }
      sc.sides.push_back({unit, nCount, nPlayer, sc.bAttack[nPlayer]});
    }
  }
  *pOut = sc;
  return true;
}

// Its JSON as the file holds it: the side the unit joins first.
PortJson SavedJson(const Saved &sc) {
  PortJson j = StudioJsonObject();
  StudioJsonPut(j, "name", StudioJsonString(sc.name));
  StudioJsonPut(j, "tried", StudioJsonNumber(0));
  PortJson sides;
  sides.kind = PortJson::ARRAY;
  for (int p = 0; p < 2; ++p) {
    PortJson side = StudioJsonObject(), units;
    units.kind = PortJson::ARRAY;
    for (const Side &s : sc.sides)
      if (s.nPlayer == p) {
        PortJson u = StudioJsonObject();
        StudioJsonPut(u, "unit", StudioJsonString(s.unit));
        StudioJsonPut(u, "count", StudioJsonNumber(s.nCount));
        units.items.push_back(u);
      }
    StudioJsonPut(side, "units", units);
    if (sc.bAttack[p])
      StudioJsonPut(side, "order", StudioJsonString("attack"));
    sides.items.push_back(side);
  }
  StudioJsonPut(j, "sides", sides);
  return j;
}

// The mod's scenarios, read from its folder again at most once a second.
const std::vector<Saved> &SavedList(bool bFresh = false) {
  static std::vector<Saved> s_list;
  static std::string s_folder;
  static double s_dAt = -10.0;
  const double dNow = ImGui::GetTime();
  if (!bFresh && s_folder == (StudioIsOpen() ? StudioModFolder() : std::string()) && dNow - s_dAt < 1.0)
    return s_list;
  s_list.clear();
  s_folder = StudioIsOpen() ? StudioModFolder() : std::string();
  s_dAt = dNow;
  if (s_folder.empty())
    return s_list;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((ScenarioFolder() + "*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return s_list;
  do {
    std::string text, err, why;
    PortJson j;
    const std::string file = std::string(fd.cFileName).substr(0, strlen(fd.cFileName) - 5);
    Saved sc;
    if (ReadText(ScenarioFolder() + fd.cFileName, &text) && PortJsonParse(text.c_str(), &j, &err) && SavedFrom(j, file, &sc, &why))
      s_list.push_back(sc);
    else if (bFresh)
      StudioLog("warning", "scenarios\\" + std::string(fd.cFileName) + " is left out: " + (err.empty() ? why : StudioJsonWhere(text, err)));
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  std::sort(s_list.begin(), s_list.end(), [](const Saved &a, const Saved &b) { return Lower(a.name) < Lower(b.name); });
  return s_list;
}

bool FindSaved(const std::string &name, Saved *pOut) {
  for (const Saved &sc : SavedList())
    if (Lower(sc.name) == Lower(name) || sc.file == FileOf(name)) {
      *pOut = sc;
      return true;
    }
  return false;
}

bool WriteSaved(const Saved &sc, std::string *pPath, std::string *pWhy) {
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  if (FileOf(sc.name).empty()) {
    *pWhy = "a scenario's name needs a letter or a digit";
    return false;
  }
  CreateDirectoryA(ScenarioFolder().c_str(), NULL);
  *pPath = ScenarioFolder() + FileOf(sc.name) + ".json";
  const std::string text = StudioJsonText(SavedJson(sc));
  FILE *f = fopen(pPath->c_str(), "wb");
  if (f == nullptr) {
    *pWhy = "could not write " + *pPath;
    return false;
  }
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
  SavedList(true);
  return true;
}

bool DeleteSaved(const std::string &name, std::string *pWhy) {
  Saved sc;
  if (!FindSaved(name, &sc)) {
    *pWhy = "no scenario " + name;
    return false;
  }
  if (!DeleteFileA((ScenarioFolder() + sc.file + ".json").c_str())) {
    *pWhy = "could not take away scenarios\\" + sc.file + ".json";
    return false;
  }
  SavedList(true);
  return true;
}

// "6 Marines" and so on, a side's rows in words
std::string SideWords(const std::vector<Side> &sides, int nPlayer) {
  std::string vs;
  for (const Side &s : sides)
    if (s.nPlayer == nPlayer && !s.unit.empty())
      vs += (vs.empty() ? "" : ", ") + std::to_string(s.nCount) + " " + Short(ST_UNITS, s.unit);
  return vs;
}

// ---- the workspace's state ------------------------------------------------------------------------------------

int g_nScenario = 0;
std::string g_unit;  // the unit tried
int g_nYours = 1;
std::vector<Side> g_custom = {{"protoss_zealot", 2, 1}, {"terran_marine", 3, 1}};
bool g_customAttack[2] = {true, true};  // each side's order
std::string g_savedName;                // the saved scenario the rows came from, or are saved as
char g_szSaveAs[64];
int g_nReplace = 2;  // the plain push's: off, the push before, every earlier one

std::vector<Side> SidesNow(std::string *pLabel) {
  std::vector<Side> out;
  if (g_nScenario < SC_CUSTOM) {
    const Scenario &s = kScenarios[g_nScenario];
    out.push_back({s.pszUnit, s.nCount, 1});
    *pLabel = StudioShowName(ST_UNITS, g_unit) + (g_nYours > 1 ? " x" + std::to_string(g_nYours) : "") + " " + s.pszLabel;
    return out;
  }
  for (Side s : g_custom)
    if (!s.unit.empty()) {
      s.bAttack = g_customAttack[s.nPlayer == 0 ? 0 : 1];
      out.push_back(s);
    }
  const std::string vs = SideWords(out, 1);
  *pLabel = StudioShowName(ST_UNITS, g_unit) + (g_nYours > 1 ? " x" + std::to_string(g_nYours) : "") + " vs " + (vs.empty() ? "nothing" : vs) +
            (g_savedName.empty() ? std::string() : " (" + g_savedName + ")");
  return out;
}

void TryNow(void) {
  std::string label;
  const std::vector<Side> sides = SidesNow(&label);
  TryStart(g_unit, g_nYours, sides, label, g_nScenario == SC_CUSTOM && g_customAttack[0]);
}

// A saved scenario's rows into the workspace's, to try and to change.
void UseSaved(const Saved &sc) {
  g_custom = sc.sides;
  g_customAttack[0] = sc.bAttack[0];
  g_customAttack[1] = sc.bAttack[1];
  g_savedName = sc.name;
  snprintf(g_szSaveAs, sizeof g_szSaveAs, "%s", sc.name.c_str());
  g_nScenario = SC_CUSTOM;
}

// The workspace's rows saved under the name typed.
void SaveRows(void) {
  Saved sc;
  sc.name = g_szSaveAs;
  while (!sc.name.empty() && sc.name.back() == ' ')
    sc.name.pop_back();
  for (const Side &s : g_custom)
    if (!s.unit.empty())
      sc.sides.push_back(s);
  sc.bAttack[0] = g_customAttack[0];
  sc.bAttack[1] = g_customAttack[1];
  std::string path, why;
  if (WriteSaved(sc, &path, &why)) {
    g_savedName = sc.name;
    StudioSay("saved the scenario " + sc.name + " in scenarios\\");
  } else {
    StudioSay("Save as a scenario: " + why);
  }
}

void Push(void) {
  static const char *const kReplace[] = {"", "\"replace\":\"last\"", "\"replace\":\"all\""};
  if (!StudioIsOpen() || !DesignInGame()) {
    StudioSay(StudioIsOpen() ? "the game is not on yet" : "no mod is open");
    return;
  }
  StudioAsk("studio_push", kReplace[g_nReplace]);
}

// The units on the map, asked for twice a second while the workspace shows.
struct Seen {
  int nPlayer = 0;
  int nType = 0;
  std::string name;  // a pushed unit's name as the game shows it, "" for a classic one
  int nSub = 0;
  int nCount = 0;
  long long nHp = 0, nHpMax = 0, nShields = 0;
};
std::vector<Seen> g_seen;
unsigned g_nSeenFrame;
double g_dAsked = -10.0;
bool g_bAsking;

void AskUnits(void) {
  const double dNow = ImGui::GetTime();
  if (g_bAsking || dNow - g_dAsked < 0.5 || !DesignInGame())
    return;
  g_bAsking = true;
  g_dAsked = dNow;
  DesignCall("units", "\"max\":400", [](const PortJson &a) {
    g_bAsking = false;
    if (!(a["ok"].kind == PortJson::BOOL && a["ok"].b))
      return;
    std::map<std::pair<int, std::string>, Seen> by;
    for (const PortJson &u : a["units"].items) {
      const int nPlayer = static_cast<int>(u["player"].Num(0)), nType = static_cast<int>(u["type"].Num(0));
      if (nPlayer > 7)
        continue;  // the neutral player's: critters, minerals
      const std::string name = u["name"].Str();
      Seen &s = by[{nPlayer, name.empty() ? "#" + std::to_string(nType) : name + "#" + Fmt(u["submission"].Num(0))}];
      s.nPlayer = nPlayer;
      s.nType = nType;
      s.name = name;
      s.nSub = static_cast<int>(u["submission"].Num(0));
      ++s.nCount;
      s.nHp += static_cast<long long>(u["hp"].Num(0));
      s.nHpMax += static_cast<long long>(u["hpMax"].Num(0));
      // a classic unit without shields keeps a stale number there: counted only where it has them
      const char *pszClassic = name.empty() ? StudioClassicName(ST_UNITS, nType) : nullptr;
      if (!pszClassic || StudioNumber(ST_UNITS, pszClassic, "shields_enabled") != 0)
        s.nShields += static_cast<long long>(u["shields"].Num(0));
    }
    g_seen.clear();
    for (auto &e : by)
      g_seen.push_back(e.second);
    g_nSeenFrame = static_cast<unsigned>(a["frame"].Num(0));
  });
}

// The record and field a refused push's error names ("test_zealot.ground_weapon: ..."), when it
// names one of the mod's.
bool ErrorField(const std::string &error, int *pT, std::string *pName, std::string *pField) {
  std::string tok;
  auto check = [&](const std::string &s) {
    const size_t dot = s.find('.');
    if (dot == std::string::npos || dot == 0)
      return false;
    const std::string name = s.substr(0, dot);
    std::string field = s.substr(dot + 1);
    const size_t dot2 = field.find('.');
    if (dot2 != std::string::npos)
      field = field.substr(0, dot2);
    for (int t = 0; t < ST_TABLES; ++t) {
      StudioRecord r;
      if (StudioRecordOf(t, name, &r) && r.kind != SK_CLASSIC) {
        *pT = t;
        *pName = name;
        *pField = field;
        return true;
      }
    }
    return false;
  };
  for (char ch : error + " ") {
    if (isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.') {
      tok += ch;
    } else {
      while (!tok.empty() && tok.back() == '.')
        tok.pop_back();
      if (check(tok))
        return true;
      tok.clear();
    }
  }
  return false;
}

void OpenField(int t, const std::string &name, const std::string &field) {
  DesignCall("studio_show", "\"table\":" + Q(Table(t)) + ",\"name\":" + Q(name) + ",\"view\":\"numbers\",\"level\":\"detailed\",\"filter\":" +
                                Q(field));
}

// The game against the model: every edit pushed, some not, none pushed yet.
int PushState(std::string *pWords) {
  const int n = StudioPushCount();
  for (int i = n; i >= 1; --i) {
    std::string state, answer;
    unsigned nChange = 0;
    StudioPushInfo(i, &state, &answer, &nChange);
    if (state == "asked") {
      *pWords = "pushing";
      return CH_PLAIN;
    }
    if (state == "pushed") {
      *pWords = nChange == StudioChange() ? "the game has every edit" : "edits not pushed";
      return nChange == StudioChange() ? CH_GOOD : CH_WARN;
    }
  }
  *pWords = n ? "the last push was refused" : "not pushed yet";
  return n ? CH_BAD : CH_PLAIN;
}

void Lab(const char *psz) {
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), psz, 0.1f, true);
}

// A choice as a row that is chosen.
bool Choice(const char *pszId, const std::string &label, const std::string &right, bool bOn, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = P(28.0f);
  const bool b = StudioButton(pszId, "", nullptr, bOn ? SB_ON : SB_PLAIN, 28.0f, fW / P(1.0f));
  T(d, STUDIO_FONT_TEXT, 13.0f, p.x + P(10.0f), Mid(p.y, h, 13.0f), bOn ? C(TK_HI) : C(TK_INK), label, fW - P(90.0f));
  if (!right.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + fW - P(10.0f) - TW(STUDIO_FONT_TEXT, kStudioFsXs, right), Mid(p.y, h, kStudioFsXs), C(TK_INK3),
      right);
  return b;
}

// ---- the columns ---------------------------------------------------------------------------------------------

void TryColumn(float fW) {
  Lab("Try");
  // the mod's unit tried, and how many
  const std::vector<StudioRecord> units = StudioRecords(ST_UNITS);
  if (g_unit.empty()) {
    if (StudioCurrent().t == ST_UNITS)
      g_unit = StudioCurrent().name;
    else
      for (const StudioRecord &r : units)
        if (r.kind == SK_OWN) {
          g_unit = r.name;
          break;
        }
  }
  std::string picked;
  if (StudioRecordPick("##tryunit", ST_UNITS, g_unit, fW / P(1.0f) - 70.0f, &picked, false, 26.0f, "a unit to try"))
    g_unit = picked;
  ImGui::SameLine(0.0f, P(6.0f));
  double d = g_nYours;
  if (StudioNumber("##yours", &d, 56.0f, 26.0f, g_nYours > 1, 13.0f))
    g_nYours = std::max(1, std::min(64, static_cast<int>(d)));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How many of it");
  ImGui::Dummy(ImVec2(1, P(2.0f)));
  Lab("Against");
  for (int i = 0; i < 3; ++i) {
    ImGui::PushID(i);
    if (Choice("##sc", kScenarios[i].pszLabel, Short(ST_UNITS, kScenarios[i].pszUnit), g_nScenario == i, fW))
      g_nScenario = i;
    ImGui::PopID();
  }
  // the mod's own, each picked into the rows below, and taken away with its x
  std::string out;
  for (const Saved &sc : SavedList()) {
    ImGui::PushID(sc.file.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool bOn = g_nScenario == SC_CUSTOM && Lower(g_savedName) == Lower(sc.name);
    const std::string vs = SideWords(sc.sides, 1);
    if (Choice("##saved", sc.name, vs.empty() ? "nothing" : vs, bOn, fW - P(30.0f)))
      UseSaved(sc);
    const ImVec2 next = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x + fW - P(26.0f), p.y + P(2.0f)));
    if (StudioButton("##del", "", "x", SB_FLAT, 24.0f, 24.0f))
      out = sc.name;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Take the scenario away (its file in the mod's scenarios folder)");
    ImGui::SetCursorScreenPos(next);
    ImGui::PopID();
  }
  if (!out.empty()) {
    std::string why;
    if (DeleteSaved(out, &why)) {
      StudioSay("took the scenario " + out + " away");
      if (Lower(g_savedName) == Lower(out))
        g_savedName.clear();
    } else {
      StudioSay(why);
    }
  }
  if (Choice("##sccustom", g_savedName.empty() ? std::string("Of your own") : "Of your own: " + g_savedName,
             std::to_string(g_custom.size()) + " rows", g_nScenario == SC_CUSTOM, fW))
    g_nScenario = SC_CUSTOM;
  if (g_nScenario == SC_CUSTOM) {
    // each row: a unit, how many, whose
    static const char *const kOwners[] = {"Yours", "Theirs"};
    int nOut = -1;
    for (size_t i = 0; i < g_custom.size(); ++i) {
      Side &s = g_custom[i];
      ImGui::PushID(static_cast<int>(i) + 100);
      std::string pick;
      if (StudioRecordPick("##cu", ST_UNITS, s.unit, fW / P(1.0f) - 190.0f, &pick, false, 24.0f, "a unit"))
        s.unit = pick;
      ImGui::SameLine(0.0f, P(4.0f));
      double n = s.nCount;
      if (StudioNumber("##cn", &n, 44.0f, 24.0f, true, 12.0f))
        s.nCount = std::max(1, std::min(64, static_cast<int>(n)));
      ImGui::SameLine(0.0f, P(4.0f));
      s.nPlayer = StudioSeg("##co", kOwners, 2, s.nPlayer == 0 ? 0 : 1, 24.0f, kStudioFsXs);
      ImGui::SameLine(0.0f, P(2.0f));
      if (StudioButton("##cx", "", "x", SB_FLAT, 24.0f, 22.0f))
        nOut = static_cast<int>(i);
      ImGui::PopID();
    }
    if (nOut >= 0)
      g_custom.erase(g_custom.begin() + nOut);
    if (StudioButton("##cadd", "Add units", "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, g_custom.size() < 16))
      g_custom.push_back({"protoss_zealot", 1, 1});
    ImGui::SameLine(0.0f, P(12.0f));
    StudioCheck("##yatt", "yours attack", &g_customAttack[0]);
    ImGui::SameLine(0.0f, P(10.0f));
    StudioCheck("##tatt", "theirs attack", &g_customAttack[1]);
    // saved as a file of the mod, at once
    StudioInput("##saveas", g_szSaveAs, sizeof g_szSaveAs, fW / P(1.0f) - 150.0f, 24.0f, "a name for it");
    ImGui::SameLine(0.0f, P(6.0f));
    const bool bSame = Lower(g_szSaveAs) == Lower(g_savedName) && !g_savedName.empty();
    if (StudioButton("##save", bSame ? "Save it" : "Save as a scenario", "save", SB_PLAIN, 24.0f, 144.0f, kStudioFsXs,
                     StudioIsOpen() && !FileOf(g_szSaveAs).empty() && !g_custom.empty()))
      SaveRows();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Written now into the mod's scenarios folder: no edit to undo");
  }
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  const bool bGame = DesignInGame() && StudioIsOpen();
  if (StudioButton("##try", "Try it", "play", SB_GOOD, 28.0f, 0.0f, 13.0f, bGame && !g_unit.empty()))
    TryNow();
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##again", "Again  F9", "again", SB_PLAIN, 28.0f, 0.0f, kStudioFsXs, bGame && !g_try.unit.empty()))
    Again();
  ImGui::SameLine(0.0f, P(6.0f));
  const bool bPaused = DesignInGame() && DesignGamePaused();
  if (StudioButton("##pause", bPaused ? "Go on" : "Pause", bPaused ? "play" : "pause", bPaused ? SB_ON : SB_PLAIN, 28.0f, 0.0f, kStudioFsXs,
                   DesignInGame()))
    DesignCall(bPaused ? "resume" : "pause", "");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##step", "Step", "redo", SB_PLAIN, 28.0f, 0.0f, kStudioFsXs, bPaused))
    DesignCall("step", "\"frames\":1");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("One game frame, while paused; Shift steps eight");
  if (ImGui::IsItemClicked() && ImGui::GetIO().KeyShift && bPaused)
    DesignCall("step", "\"frames\":7");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##ab", "A and B", "copy", SB_PLAIN, 28.0f, 0.0f, kStudioFsXs, !g_unit.empty())) {
    std::string label, rows;
    for (const Side &s : SidesNow(&label))
      rows += (rows.empty() ? "" : ",") + std::string("{\"unit\":") + Q(s.unit) + ",\"count\":" + std::to_string(s.nCount) +
              ",\"player\":" + std::to_string(s.nPlayer) + "}";
    StudioAbOpen(g_unit, g_nYours, "[" + rows + "]");
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("This fight played out over many seeds in Balance, against a version B of the unit");
  // the try's state
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  if (!g_try.state.empty()) {
    const int kind = g_try.state == "playing" ? CH_GOOD : g_try.state == "pushing" ? CH_PLAIN : CH_BAD;
    const float cw = StudioChip(dl, ImVec2(p.x, p.y + P(4.0f)), g_try.state.c_str(), kind);
    std::string s = g_try.state == "playing" ? g_try.label + (g_try.bStart ? ", from frame " + std::to_string(g_try.nStartFrame) : std::string())
                                             : g_try.why.empty() ? g_try.label : g_try.why;
    if (g_try.nAgains > 0)
      s += ", again " + std::to_string(g_try.nAgains);
    T(dl, STUDIO_FONT_TEXT, kStudioFsXs, p.x + cw + P(8.0f), p.y + P(5.0f), g_try.state == "playing" ? C(TK_INK2) : C(TK_BAD), s,
      fW - cw - P(8.0f));
    int t = 0;
    std::string name, field;
    if (g_try.state == "refused" && ErrorField(g_try.why, &t, &name, &field)) {
      ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(26.0f)));
      if (StudioButton("##openf", ("Open " + name + "." + field).c_str(), "link", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
        OpenField(t, name, field);
    } else {
      ImGui::Dummy(ImVec2(1, P(24.0f)));
    }
  }
}

void GameColumn(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  Lab("In the game now");
  if (!DesignInGame()) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "The canvas game is starting.");
    return;
  }
  AskUnits();
  // each side's life and shields, then its units by kind, a pushed unit with its push
  for (int nSide = 0; nSide < 2; ++nSide) {
    long long hp = 0, hpMax = 0, sh = 0;
    int nCount = 0;
    for (const Seen &s : g_seen)
      if ((nSide == 0) == (s.nPlayer == 0)) {
        hp += s.nHp;
        hpMax += s.nHpMax;
        sh += s.nShields;
        nCount += s.nCount;
      }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bw = fW - P(150.0f);
    T(d, STUDIO_FONT_BOLD, 13.0f, p.x, p.y + P(2.0f), C(nSide == 0 ? TK_GOOD : TK_BAD), nSide == 0 ? "The mod's side" : "The other side");
    d->AddRectFilled(ImVec2(p.x + P(130.0f), p.y + P(7.0f)), ImVec2(p.x + P(130.0f) + bw, p.y + P(15.0f)), C(TK_WELL), P(3.0f));
    if (hpMax > 0)
      d->AddRectFilled(ImVec2(p.x + P(130.0f), p.y + P(7.0f)),
                       ImVec2(p.x + P(130.0f) + bw * static_cast<float>(std::min(1.0, hp / static_cast<double>(hpMax))), p.y + P(15.0f)),
                       C(nSide == 0 ? TK_GOOD : TK_BAD), P(3.0f));
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
    const ImVec2 q = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, q.x, q.y, C(TK_INK3),
      nCount == 0 ? std::string("none on the map")
                  : std::to_string(nCount) + (nCount == 1 ? " unit, " : " units, ") + std::to_string(hp) + " of " + std::to_string(hpMax) +
                        " life" + (sh > 0 ? ", " + std::to_string(sh) + " shields" : std::string()));
    ImGui::Dummy(ImVec2(fW, P(18.0f)));
    for (const Seen &s : g_seen) {
      if ((nSide == 0) != (s.nPlayer == 0))
        continue;
      const ImVec2 r = ImGui::GetCursorScreenPos();
      const std::string show = s.name.empty() ? (StudioClassicName(ST_UNITS, s.nType) ? StudioShowName(ST_UNITS, StudioClassicName(ST_UNITS, s.nType))
                                                                                      : "unit " + std::to_string(s.nType))
                                              : s.name;  // a pushed unit's name as the game shows it
      float x = r.x + P(10.0f);
      x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, r.y + P(3.0f), C(TK_INK), std::to_string(s.nCount) + " " + show) + P(8.0f);
      if (s.nSub > 0)
        x += StudioChip(d, ImVec2(x, r.y + P(2.0f)), ("push " + std::to_string(s.nSub) + ", row " + std::to_string(s.nType)).c_str(), CH_GOOD) +
             P(8.0f);
      T(d, STUDIO_FONT_MONO, kStudioFsXs, x, r.y + P(4.0f), C(TK_INK3),
        std::to_string(s.nHp) + " / " + std::to_string(s.nHpMax) + (s.nShields > 0 ? " +" + std::to_string(s.nShields) : std::string()));
      ImGui::Dummy(ImVec2(fW, P(22.0f)));
    }
    ImGui::Dummy(ImVec2(1, P(6.0f)));
  }
  const ImVec2 p = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x, p.y, C(TK_INK3),
    "Frame " + std::to_string(DesignGameFrame()) + (DesignGamePaused() ? ", paused" : "") + ". The game shows in the dock below.", fW);
  ImGui::Dummy(ImVec2(fW, P(20.0f)));
  if (StudioButton("##below", "The game below", "dock", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
    DesignCall("studio_show", "\"dock\":\"game\",\"game\":\"dock\"");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##large", "Large", "game", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
    DesignCall("studio_show", "\"game\":\"large\"");
}

void PushColumn(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  Lab("Pushes");
  if (StudioButton("##push", "Push  F5", "push", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, StudioIsOpen() && DesignInGame()))
    Push();
  ImGui::SameLine(0.0f, P(8.0f));
  static const char *const kReplace[] = {"Keep", "Replace last", "Replace all"};
  g_nReplace = StudioSeg("##replace", kReplace, 3, g_nReplace, 26.0f, kStudioFsXs);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Whether the units of earlier pushes turn into the newest records of their names");
  ImGui::Dummy(ImVec2(1, P(2.0f)));
  const int n = StudioPushCount();
  if (n == 0)
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "None yet: Push or Try it sends the working mod in.");
  for (int i = n; i >= 1 && i > n - 40; --i) {
    std::string state, answer;
    unsigned nChange = 0;
    StudioPushInfo(i, &state, &answer, &nChange);
    PortJson a;
    std::string err;
    PortJsonParse(answer.c_str(), &a, &err);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x, p.y + P(4.0f), C(TK_INK3), std::to_string(i));
    const int kind = state == "pushed" ? CH_GOOD : state == "refused" ? CH_BAD : CH_PLAIN;
    const float cw = StudioChip(d, ImVec2(p.x + P(22.0f), p.y + P(2.0f)), state.c_str(), kind, 16.0f, 8.5f);
    std::string what;
    if (state == "pushed") {
      const PortJson &rc = a["records"];
      what = "push " + Fmt(a["submission"].Num(0)) + ": ";
      int nParts = 0;
      for (const PortJsonMember &m : rc.members)
        if (m.second.Num(0) > 0)
          what += std::string(nParts++ ? ", " : "") + Fmt(m.second.Num(0)) + " " + m.first;
      if (nParts == 0)
        what += "no records";
      if (nChange != StudioChange() && i == n)
        what += "; edited since";
    } else if (state == "refused") {
      what = a["error"].Str("refused");
    } else {
      what = "waiting for the game";
    }
    const float tx = p.x + P(28.0f) + cw;
    ImGui::SetCursorScreenPos(ImVec2(tx, p.y + P(3.0f)));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fW - (tx - p.x));
    StudioPushFont(STUDIO_FONT_TEXT, kStudioFsXs);
    ImGui::PushStyleColor(ImGuiCol_Text, state == "refused" ? C(TK_BAD) : C(TK_INK));
    ImGui::TextWrapped("%s", what.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    // its units, by name and row
    if (state == "pushed" && a["units"].size() > 0 && i == n) {
      std::string us;
      for (const PortJson &u : a["units"].items)
        us += (us.empty() ? "" : ", ") + u["name"].Str() + " " + Fmt(u["id"].Num(0));
      ImGui::SetCursorScreenPos(ImVec2(tx, ImGui::GetCursorScreenPos().y));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fW - (tx - p.x));
      StudioPushFont(STUDIO_FONT_MONO, kStudioFsXs);
      ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK3));
      ImGui::TextWrapped("%s", us.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::PopTextWrapPos();
    }
    int t = 0;
    std::string name, field;
    if (state == "refused" && ErrorField(what, &t, &name, &field)) {
      ImGui::SetCursorScreenPos(ImVec2(tx, ImGui::GetCursorScreenPos().y + P(2.0f)));
      ImGui::PushID(i);
      if (StudioButton("##open", ("Open " + name + "." + field).c_str(), "link", SB_PLAIN, 22.0f, 0.0f, kStudioFsXs))
        OpenField(t, name, field);
      ImGui::PopID();
    }
    const float y = ImGui::GetCursorScreenPos().y + P(4.0f);
    d->AddLine(ImVec2(p.x, y), ImVec2(p.x + fW, y), C(TK_ROW));
    ImGui::SetCursorScreenPos(ImVec2(p.x, y + P(4.0f)));
    ImGui::Dummy(ImVec2(fW, 0.0f));
  }
}

// A column's child region, scrolling, with its ground.
bool Column(const char *pszId, ImVec2 a, ImVec2 b) {
  ImGui::SetCursorScreenPos(a);
  if (!ImGui::BeginChild(pszId, ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
    return false;
  ImGui::SetCursorPos(ImVec2(P(12.0f), P(10.0f)));
  ImGui::BeginGroup();
  return true;
}

void ColumnEnd(void) {
  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(1, P(12.0f)));
}

}  // namespace

// ---- the contract (studio_ui.h) ------------------------------------------------------------------------------

// Each frame, whatever is shown: the field watched while a try is going, so studio_test answers alike
// with the Test workspace shown or not.
void StudioTestTick(void) {
  if (!g_try.state.empty())
    AskUnits();
}

void StudioTestWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float hh = P(40.0f);
  d->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + hh), C(TK_BAR));
  d->AddLine(ImVec2(a.x, a.y + hh - 0.5f), ImVec2(b.x, a.y + hh - 0.5f), C(TK_LINE));
  float x = a.x + P(14.0f);
  x += Wide(d, 13.0f, x, Mid(a.y, hh, 13.0f), C(TK_HEAD), "Test", 0.06f) + P(16.0f);
  std::string words;
  const int kind = PushState(&words);
  x += StudioChip(d, ImVec2(x, a.y + (hh - P(18.0f)) / 2.0f), words.c_str(), kind) + P(12.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, hh, kStudioFsXs), C(TK_INK3),
    "The canvas game is the real engine: what is pushed plays by the classic rules. F5 pushes, F9 puts the try back.", b.x - x - P(14.0f));
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(20.0f), a.y + hh + P(20.0f), C(TK_INK3), "Open a mod to try it.");
    return;
  }
  // three columns: the try, the game now, the pushes
  const float W = b.x - a.x;
  const float c1 = std::max(P(340.0f), W * 0.32f), c3 = std::max(P(320.0f), W * 0.32f);
  const ImVec2 ta(a.x + 1, a.y + hh), tb(a.x + c1, b.y - 1);
  const ImVec2 ga(tb.x + 1, a.y + hh), gb(b.x - c3, b.y - 1);
  const ImVec2 pa(gb.x + 1, a.y + hh), pb(b.x - 1, b.y - 1);
  d->AddLine(ImVec2(tb.x + 0.5f, ta.y), ImVec2(tb.x + 0.5f, tb.y), C(TK_LINE));
  d->AddLine(ImVec2(gb.x + 0.5f, ga.y), ImVec2(gb.x + 0.5f, gb.y), C(TK_LINE));
  if (Column("##testtry", ta, tb)) {
    TryColumn(tb.x - ta.x - P(26.0f));
    ColumnEnd();
  }
  ImGui::EndChild();
  if (Column("##testgame", ga, gb)) {
    GameColumn(std::max(P(160.0f), gb.x - ga.x - P(26.0f)));
    ColumnEnd();
  }
  ImGui::EndChild();
  if (Column("##testpush", pa, pb)) {
    PushColumn(pb.x - pa.x - P(26.0f));
    ColumnEnd();
  }
  ImGui::EndChild();
}

// ---- the commands ---------------------------------------------------------------------------------------------

namespace {

std::string TryJson(void) {
  std::string seen;
  for (const Seen &s : g_seen)
    seen += std::string(seen.empty() ? "" : ",") + "{\"player\":" + std::to_string(s.nPlayer) + ",\"type\":" + std::to_string(s.nType) +
            ",\"name\":" + Q(s.name) + ",\"submission\":" + std::to_string(s.nSub) + ",\"count\":" + std::to_string(s.nCount) +
            ",\"hp\":" + std::to_string(s.nHp) + "}";
  return "\"try\":{\"unit\":" + Q(g_try.unit) + ",\"label\":" + Q(g_try.label) + ",\"state\":" + Q(g_try.state) + ",\"why\":" +
         Q(g_try.why) + ",\"push\":" + std::to_string(g_try.nPush) + ",\"start\":" +
         (g_try.bStart ? std::to_string(g_try.nStartFrame) : std::string("null")) + ",\"agains\":" + std::to_string(g_try.nAgains) +
         "},\"seen\":[" + seen + "],\"seen_frame\":" + std::to_string(g_nSeenFrame);
}

// studio_try {unit, against?, count?, yours?, units?: [{unit, count, player}]}: the working mod
// pushed, every earlier push's units turned into the newest records, then the unit spawned with
// what it is tried against: against a scenario's key (zealots, marines, ultralisk) or a unit's
// name, count of it, or one of the mod's scenarios by its name; or units, each row's player 0
// (the unit's side) or 1. The answer comes at
// once; studio_test says how it went.
bool CmdTry(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str();
  StudioRecord r;
  if (unit.empty() || !StudioRecordOf(ST_UNITS, unit, &r)) {
    *pAnswer = "studio_try needs unit, a unit by name";
    return false;
  }
  std::vector<Side> sides;
  std::string vs;
  bool bYoursAttack = false;
  if (j["units"].kind == PortJson::ARRAY) {
    for (const PortJson &u : j["units"].items) {
      StudioRecord ru;
      if (!StudioRecordOf(ST_UNITS, u["unit"].Str(), &ru)) {
        *pAnswer = "units: each row's unit is a unit by name";
        return false;
      }
      sides.push_back({u["unit"].Str(), static_cast<int>(u["count"].Num(1)), u["player"].Num(1) == 0 ? 0 : 1});
      if (u["player"].Num(1) != 0)
        vs += (vs.empty() ? "" : ", ") + Fmt(u["count"].Num(1)) + " " + Short(ST_UNITS, u["unit"].Str());
    }
  } else {
    const std::string against = j["against"].Str("zealots");
    bool bFound = false;
    for (const Scenario &s : kScenarios)
      if (against == s.pszKey) {
        sides.push_back({s.pszUnit, static_cast<int>(j["count"].Num(s.nCount)), 1});
        bFound = true;
      }
    Saved sc;
    if (!bFound && FindSaved(against, &sc)) {
      sides = sc.sides;
      bYoursAttack = sc.bAttack[0];
      vs = SideWords(sides, 1);
      vs = (vs.empty() ? "nothing" : vs) + " (" + sc.name + ")";
      UseSaved(sc);
      bFound = true;
    } else if (!bFound) {
      StudioRecord ra;
      if (!StudioRecordOf(ST_UNITS, against, &ra)) {
        *pAnswer = "against is zealots, marines, ultralisk, a scenario of the mod's (studio_scenarios) or a unit by name";
        return false;
      }
      sides.push_back({against, static_cast<int>(j["count"].Num(1)), 1});
    }
    if (vs.empty())
      vs = Fmt(sides[0].nCount) + " " + Short(ST_UNITS, sides[0].unit);
  }
  const int nYours = static_cast<int>(j["yours"].Num(1));
  g_unit = unit;  // the workspace shows what was tried
  TryStart(unit, nYours, sides, StudioShowName(ST_UNITS, unit) + (nYours > 1 ? " x" + std::to_string(nYours) : "") + " vs " + vs, bYoursAttack);
  if (g_try.state != "pushing") {
    *pAnswer = g_try.why;
    return false;
  }
  *pAnswer = TryJson();
  return true;
}

// studio_test {again?, pause?, resume?, step?}: the try's state and the units last seen; with
// again, the try put back at its start point (F9); pause, resume, or step frames while paused.
bool CmdTest(const PortJson &j, std::string *pAnswer) {
  if (j["again"].kind == PortJson::BOOL && j["again"].b)
    Again();
  if (j["pause"].kind == PortJson::BOOL && j["pause"].b)
    DesignCall("pause", "");
  if (j["resume"].kind == PortJson::BOOL && j["resume"].b)
    DesignCall("resume", "");
  if (j["step"].kind == PortJson::NUMBER)
    DesignCall("step", "\"frames\":" + std::to_string(std::max(1, static_cast<int>(j["step"].d))));
  *pAnswer = TryJson() + ",\"game\":" + (DesignInGame() ? "true" : "false") + ",\"paused\":" + (DesignGamePaused() ? "true" : "false") +
             ",\"frame\":" + std::to_string(DesignGameFrame());
  return true;
}

std::string SavedAnswer(const Saved &sc) {
  return "{\"name\":" + Q(sc.name) + ",\"file\":" + Q("scenarios/" + sc.file + ".json") + ",\"json\":" + StudioJsonText(SavedJson(sc)) + "}";
}

// studio_scenarios {}: the mod's own scenarios, each as its file holds it, and the fixed ones' keys.
bool CmdScenarios(const PortJson &, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string list;
  for (const Saved &sc : SavedList(true))
    list += (list.empty() ? "" : ",") + SavedAnswer(sc);
  *pAnswer = "\"folder\":" + Q(ScenarioFolder()) + ",\"fixed\":[\"zealots\",\"marines\",\"ultralisk\"],\"scenarios\":[" + list + "]";
  return true;
}

// studio_scenario_save {name, sides?, tried?}: a scenario written into the mod's scenarios folder
// at once, in place of one of the same name: its sides as the file has them (a list of one or two,
// each {units: [{unit, count}], order?: "attack"}, tried the place of the one the unit tried
// joins), or without sides the last try's.
bool CmdScenarioSave(const PortJson &j, std::string *pAnswer) {
  Saved sc;
  const std::string name = j["name"].Str();
  if (j["sides"].kind == PortJson::ARRAY) {
    if (!SavedFrom(j, FileOf(name), &sc, pAnswer))
      return false;
  } else if (!g_try.unit.empty()) {
    sc.sides = g_try.sides;
    sc.bAttack[0] = g_try.bYoursAttack;
    sc.bAttack[1] = false;
    for (const Side &s : sc.sides)
      sc.bAttack[s.nPlayer == 0 ? 0 : 1] = sc.bAttack[s.nPlayer == 0 ? 0 : 1] || s.bAttack;
  } else {
    *pAnswer = "studio_scenario_save needs sides, or a try made before";
    return false;
  }
  sc.name = name;
  sc.file = FileOf(name);
  std::string path;
  if (!WriteSaved(sc, &path, pAnswer))
    return false;
  *pAnswer = "\"path\":" + Q(path) + ",\"scenario\":" + SavedAnswer(sc);
  return true;
}

// studio_scenario_delete {name}: a scenario's file taken away.
bool CmdScenarioDelete(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str();
  if (!DeleteSaved(name, pAnswer))
    return false;
  if (Lower(g_savedName) == Lower(name) || FileOf(g_savedName) == FileOf(name))
    g_savedName.clear();
  *pAnswer = "\"deleted\":" + Q(name);
  return true;
}

}  // namespace

void StudioTestInit(void) {
  PortControlCommand("studio_try", CmdTry);
  PortControlCommand("studio_test", CmdTest);
  PortControlCommand("studio_scenarios", CmdScenarios);
  PortControlCommand("studio_scenario_save", CmdScenarioSave);
  PortControlCommand("studio_scenario_delete", CmdScenarioDelete);
}
