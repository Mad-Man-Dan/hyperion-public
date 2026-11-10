// studio_shell.cpp -- Hyperion Studio's frame (studio_ui.h), laid out as the design's mock lays it out
// (design/56-studio/, cs-shell.css): the menu bar and the workspace toolbar along the top, the
// explorer of the mod's records on the left, the open records as documents in the middle with
// their view tabs, the preview and the inspector on the right, the dock along the foot with the
// canvas game in its Game tab, and the status bar. The game shows through the Game tab's place
// (DesignRoomSet), and clicks there reach it. The panels read the model straight
// (studio_model.h) and change it only through the studio's commands (StudioAsk), so what a
// panel does Claude can do the same way.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "../port/data/port_mods.h"
#include "../port/platform/port_json.h"
#include "../port/tools/port_control.h"  // PortControlPort
#include "../port/platform/port_settings.h"
#include "../port/render/port_camera.h"  // PortCameraZoomTarget
#include "../design/design_engine.h"  // DesignCall
#include "../design/design_hooks.h"   // DesignInGame
#include "../design/design_ui.h"      // DesignRoomSet
#include "studio_draw.h"
#include "studio_model.h"
#include "studio_ui.h"

namespace {

using namespace studio_draw;

// ---- state ---------------------------------------------------------------------------------------------

std::string g_said;
struct OutLine {
  std::string time, level, text;
};
std::vector<OutLine> g_out;  // the Output tab, newest last

// the documents: the home page, then the records opened, the current one in g_current
std::vector<StudioOpenRecord> g_docs;
StudioOpenRecord g_current;    // t -1: the home page
int g_nLevel = 0;              // Simple, Detailed, Raw
int g_nView = 0;               // the record's view tab
int g_nWs = 0;                 // the workspace: Make, Tables, Art, Looks, Effects, Cards and races, Test, Checks
int g_nDockTab = 0;            // Game, Problems, History, Output
bool g_bDock = true;           // the dock open, or down to its tabs
char g_szFind[96];             // the explorer's search
char g_szFilter[64];           // the fields' filter
bool g_bFocusFind, g_bFocusFilter;
std::set<std::string> g_shut;  // explorer sections folded, by table name
std::set<std::string> g_classic;  // explorer sections with their classic rows open
int g_nTurn = 0, g_nColour = 0, g_nTileset = 0;  // the preview's choices
int g_nBeside = 0;                               // what Numbers shows beside each value
std::string g_rawFor;                            // the record the Raw view's text is of
std::string g_raw;                               // the Raw view's text
std::string g_rawWhy;                            // why it was refused

// the test loop: a push, then the unit and a scenario spawned in the canvas game
struct Scenario {
  const char *pszLabel;
  const char *pszUnit;
  int nUnit;
  int nCount;
};
const Scenario kScenarios[] = {{"vs 4 Zealots", "Zealot", 65, 4}, {"vs 6 Marines", "Marine", 0, 6}, {"vs 1 Ultralisk", "Ultralisk", 39, 1}};
int g_nScenario = 0;
std::string g_trying;       // the mod's unit tried
bool g_bPaused;
bool g_bAuto;
unsigned g_nAutoSeen;       // the model's change Auto last saw
double g_dAutoAt;           // when it changed
std::string g_tried;        // what the game holds now, in words
// where the game shows: in the dock's Game tab, in the preview, or large in the documents' place
enum { GAME_DOCK, GAME_PREVIEW, GAME_LARGE };
int g_nGameAt = GAME_DOCK;
// the panes' sizes in the design's pixels, as the modder dragged them; 0 for the layout's own
float g_fExW, g_fRightW, g_fInW, g_fDockH, g_fPvH;
bool g_bSizesRead;
float g_fRoomH;             // the game's room's height last frame, in the window's pixels
bool g_bWasInGame;

// ---- small helpers ---------------------------------------------------------------------------------------

std::string Clock(long long nTime) {
  const time_t t = static_cast<time_t>(nTime ? nTime : ::time(nullptr));
  struct tm tmv;
  localtime_s(&tmv, &t);
  char sz[16];
  snprintf(sz, sizeof sz, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return sz;
}

void Out(const std::string &level, const std::string &text) {
  g_out.push_back({Clock(0), level, text});
  if (g_out.size() > 400)
    g_out.erase(g_out.begin());
}

bool Mine(const StudioRecord &r) {
  return r.kind == SK_OWN || r.kind == SK_EDIT;
}

// The unsaved edits that touch a record, or one of its fields.
int ChangedSinceSave(int t, const std::string &name, const std::string &key = std::string()) {
  const std::vector<StudioEdit> &h = StudioHistory();
  const int at = StudioHistoryAt();
  int n = 0;
  for (int i = SDL_max(0, at - StudioUnsaved()); i < at && i < static_cast<int>(h.size()); ++i) {
    const StudioEdit &e = h[static_cast<size_t>(i)];
    if (e.doc != "types" || e.path.size() < 2 || e.path[0] != Table(t) || e.path[1] != name)
      continue;
    // a field counts only its own edits: a record made or written whole does not mark each field
    if (key.empty() || (e.path.size() > 2 && e.path[2] == key))
      ++n;
  }
  return n;
}

// ---- the kinds of record the explorer lists ------------------------------------------------------------

struct Kind {
  int t;
  const char *pszLabel;
  const char *pszGlyph;
  bool bAlways;  // listed when the mod has none of its kind too
};
const Kind kKinds[] = {{ST_UNITS, "Units", "unit", true},         {ST_WEAPONS, "Weapons", "weapon", true},
                       {ST_ORDERS, "Actions", "action", true},    {ST_UPGRADES, "Upgrades", "upgrade", true},
                       {ST_TECHS, "Research", "tech", true},      {ST_SPRITES, "Sprites", "sprite", true},
                       {ST_IMAGES, "Images", "image", false},     {ST_FLINGY, "Movement", "back", false},
                       {ST_SOUNDS, "Sounds", "sound", true},      {ST_PORTRAITS, "Portraits", "unit", false}};

const Kind &KindOf(int t) {
  for (const Kind &k : kKinds)
    if (k.t == t)
      return k;
  return kKinds[0];
}

// ---- documents --------------------------------------------------------------------------------------------

void OpenDoc(int t, const std::string &name) {
  g_current.t = t;
  g_current.name = name;
  if (t < 0)
    return;
  for (const StudioOpenRecord &d : g_docs)
    if (d.t == t && d.name == name)
      return;
  g_docs.push_back(g_current);
}

void CloseDoc(size_t i) {
  const bool bCurrent = g_docs[i].t == g_current.t && g_docs[i].name == g_current.name;
  g_docs.erase(g_docs.begin() + static_cast<long>(i));
  if (bCurrent) {
    if (g_docs.empty())
      g_current = StudioOpenRecord();
    else
      g_current = g_docs[SDL_min(i, g_docs.size() - 1)];
  }
}

// ---- asking -------------------------------------------------------------------------------------------------

std::string RecordFields(int t, const std::string &name) {
  return "\"table\":" + Q(Table(t)) + ",\"name\":" + Q(name);
}

void SetField(int t, const std::string &name, const std::string &key, const std::string &valueJson, bool bMerge) {
  StudioAsk("studio_set", RecordFields(t, name) + ",\"field\":" + Q(key) + ",\"value\":" + valueJson +
                              (bMerge ? ",\"merge\":" + Q(name + "." + key) : std::string()));
}

void Revert(int t, const std::string &name, const std::string &key) {
  StudioAsk("studio_set", RecordFields(t, name) + ",\"field\":" + Q(key) + ",\"revert\":true");
}

void Push(void) {
  if (!StudioIsOpen())
    return;
  if (!DesignInGame()) {
    StudioSay("the game is not on yet");
    return;
  }
  StudioAsk("studio_push", "\"replace\":\"all\"");
}

// A try: the Test workspace's (studio_try), so the dock, the toolbar and Test show one try and
// Again puts back the same fight; the mod is pushed, then the unit and the scenario's side spawned.
void TryIt(const std::string &unit) {
  if (unit.empty() || !DesignInGame())
    return;
  g_trying = unit;
  static const char *const kAgainst[] = {"zealots", "marines", "ultralisk"};
  DesignCall("studio_try", "\"unit\":" + Q(unit) + ",\"against\":" + Q(kAgainst[g_nScenario]), nullptr);
  g_tried = StudioShowName(ST_UNITS, g_trying) + " " + kScenarios[g_nScenario].pszLabel;
}

void Again(void) {
  DesignCall("studio_test", "\"again\":true", nullptr);
}

// Each frame: Auto's push once the edits rest.
void Tick(void) {
  const double dNow = ImGui::GetTime();
  if (StudioChange() != g_nAutoSeen) {
    g_nAutoSeen = StudioChange();
    g_dAutoAt = dNow;
  }
  if (g_bAuto && g_dAutoAt > 0.0 && dNow - g_dAutoAt > 0.7 && StudioIsOpen() && DesignInGame()) {
    g_dAutoAt = 0.0;
    if (!g_trying.empty())
      TryIt(g_trying);
    else
      Push();
  }
}

// The state of the game against the model: every edit pushed, some not, none pushed yet.
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
      if (nChange == StudioChange()) {
        *pWords = "the game has every edit";
        return CH_GOOD;
      }
      *pWords = "edits not pushed";
      return CH_WARN;
    }
  }
  *pWords = n ? "the last push was refused" : "not pushed yet";
  return n ? CH_BAD : CH_PLAIN;
}

// ---- the problems ---------------------------------------------------------------------------------------------

struct Problem {
  const char *pszKind;  // needs, clash, engine, note
  std::string text;
  int t = -1;
  std::string name;
};

std::vector<Problem> Problems(void) {
  std::vector<Problem> out;
  if (!StudioFieldsError().empty())
    out.push_back({"engine", "fields.json: " + StudioFieldsError()});
  if (!StudioIsOpen())
    return out;
  // the records that do not resolve, and the rest Checks finds, are its (StudioChecksList)
  const int n = StudioPushCount();
  if (n > 0) {
    std::string state, answer;
    unsigned nChange = 0;
    StudioPushInfo(n, &state, &answer, &nChange);
    if (state == "refused") {
      PortJson a;
      std::string err;
      PortJsonParse(answer.c_str(), &a, &err);
      out.push_back({"engine", "the game refused push " + std::to_string(n) + ": " + a["error"].Str("no reason given")});
    }
  }
  std::string when;
  if (StudioRecoverWaiting(&when))
    out.push_back({"needs", "unsaved work from " + when + " was kept: File, Take back unsaved work"});
  if (StudioUnsaved() > 0)
    out.push_back({"note", std::to_string(StudioUnsaved()) + " edits are not saved yet"});
  // each language's missing and stale strings (studio_languages.cpp), a click showing them
  for (const StudioLanguageProblem &l : StudioLanguagesProblems())
    out.push_back({l.pszKind, l.text, -2, l.code});
  return out;
}

const char *ProblemGlyph(const char *pszKind) {
  return strcmp(pszKind, "clash") == 0 ? "clash" : strcmp(pszKind, "engine") == 0 ? "engine"
                                             : strcmp(pszKind, "needs") == 0    ? "needs"
                                                                                : "note";
}

int ProblemTok(const char *pszKind) {
  return strcmp(pszKind, "clash") == 0 ? TK_BAD : strcmp(pszKind, "engine") == 0 ? TK_LABEL
                                                : strcmp(pszKind, "needs") == 0    ? TK_WARN
                                                                                   : TK_INK3;
}

// the facing a unit's picture is drawn at: turning, or still at the classic's front three-quarter
int Facing(void) {
  return g_nTurn == 0 ? static_cast<int>(ImGui::GetTime() * 5.0) % 32 : 12;
}

// ---- what a unit's numbers work out to -------------------------------------------------------------------------

long long Num(int t, const std::string &name, const char *pszKey, long long nDefault = 0) {
  return StudioNumber(t, name, pszKey, nDefault);
}

// a unit's weapon: its name, or empty for none (130)
std::string WeaponOf(const std::string &unit, bool bAir = false) {
  return StudioUnitWeapon(unit, bAir);
}

const char *SizeWord(long long n) {
  switch (n) {
  case 1: return "small";
  case 2: return "medium";
  case 3: return "large";
  default: return "independent";
  }
}

const char *SizeRule(long long n) {
  switch (n) {
  case 1: return "explosive does half, concussive full";
  case 2: return "explosive does three quarters, concussive half";
  case 3: return "explosive does full, concussive a quarter";
  default: return "every kind of damage does full";
  }
}

std::string RaceOf(const std::string &unit) {
  const long long g = Num(ST_UNITS, unit, "group_flags");
  return (g & 1) ? "Zerg" : (g & 2) ? "Terran" : (g & 4) ? "Protoss" : "Independent";
}

bool Flies(const std::string &unit) {
  const std::vector<std::string> &names = StudioFlags("units");
  const long long f = Num(ST_UNITS, unit, "flags");
  for (size_t i = 0; i < names.size(); ++i)
    if (Lower(names[i]).find("flyer") != std::string::npos || Lower(names[i]) == "air")
      return ((f >> i) & 1) != 0;
  return (f & 4) != 0;
}

struct Fight {
  std::string weapon;
  long long damage = 0, hits = 1, cooldown = 0, range = 0;
  double perSecond = 0.0;
};

Fight FightOf(const std::string &unit) {
  Fight f;
  f.weapon = WeaponOf(unit);
  if (f.weapon.empty())
    f.weapon = WeaponOf(unit, true);
  if (f.weapon.empty())
    return f;
  f.damage = Num(ST_WEAPONS, f.weapon, "damage");
  f.hits = SDL_max(1LL, Num(ST_WEAPONS, f.weapon, "damage_factor", 1));  // an attack's hits are the weapon's
  f.cooldown = Num(ST_WEAPONS, f.weapon, "cooldown");
  f.range = Num(ST_WEAPONS, f.weapon, "max_range");
  f.perSecond = StudioDamagePerSecond(f.weapon);
  return f;
}

std::string RangeWords(long long px) {
  if (px <= 32)
    return "melee";
  return Fmt(px / 32.0) + " tiles";
}

// A unit's shields as the game counts them: none unless its shields are on, whatever the column says
// (a Terran or Zerg row keeps a number there that the game never uses).
long long Shields(int t, const std::string &name) {
  return t == ST_UNITS && Num(t, name, "shields_enabled") != 0 ? Num(t, name, "shields") : 0;
}

// A unit in a sentence, as the overview and the inspector say it.
std::string Sentence(const std::string &unit) {
  const long long hp = Num(ST_UNITS, unit, "hit_points"), sh = Shields(ST_UNITS, unit);
  const Fight f = FightOf(unit);
  std::string s = std::string("A ") + SizeWord(Num(ST_UNITS, unit, "size")) + " " + RaceOf(unit) + " " +
                  (Flies(unit) ? "air" : "ground") + " unit";
  s += f.weapon.empty() ? " with no weapon. " : (f.range <= 32 ? " that fights up close. " : " that fights at range. ");
  s += std::to_string(hp) + " life";
  if (sh > 0)
    s += " and " + std::to_string(sh) + " shields";
  if (!f.weapon.empty()) {
    s += ", " + std::to_string(f.damage * f.hits) + " a hit";
    if (f.perSecond > 0.0)
      s += " (" + Fmt(f.perSecond) + " a second)";
  }
  s += ". Costs " + std::to_string(Num(ST_UNITS, unit, "minerals")) + " minerals and " +
       std::to_string(Num(ST_UNITS, unit, "gas")) + " gas, made in " + Fmt(Num(ST_UNITS, unit, "build_time") / 24.0) +
       " seconds.";
  return s;
}

// What a field's value works out to, in words, beside it in Numbers.
std::string Work(int t, const std::string &name, const StudioField &f, const StudioValue &v) {
  if (v.n.empty())
    return f.help;
  const long long n = v.n[0];
  if (t == ST_UNITS) {
    const std::string &k = f.key;
    if (k == "hit_points") {
      const long long sh = Shields(t, name);
      return sh > 0 ? std::to_string(n + sh) + " with its shields" : std::to_string(n) + " life, no shields";
    }
    if (k == "shields")
      return Num(t, name, "shields_enabled") == 0 ? "not used: its shields are off"
             : n > 0                              ? "taken first, and they come back"
                                                  : "none";
    if (k == "armor")
      return "a Marine's 6 a shot does " + Fmt(SDL_max(0.5, 6.0 - static_cast<double>(n))) + " to it";
    if (k == "size")
      return SizeRule(n);
    if (k == "minerals") {
      const long long gas = Num(t, name, "gas");
      return "worth " + Fmt(n + gas * 1.5) + " minerals in all, with gas at 1.5";
    }
    if (k == "gas") {
      const double worth = Num(t, name, "minerals") + n * 1.5;
      const double life = static_cast<double>(Num(t, name, "hit_points") + Shields(t, name));
      return worth > 0 ? Fmt(life / worth, 2) + " life a mineral spent; a Zealot has 1.6" : std::string("free");
    }
    if (k == "build_time")
      return Fmt(n / 24.0) + " seconds at Fastest";
    if (k == "supply_required" || k == "supply_provided")
      return Fmt(n / 2.0) + " supply";
    if (k == "sight_range" || k == "target_range")
      return std::to_string(n) + " tiles";
    if (k == "ground_weapon" || k == "air_weapon") {
      if (n == 130)
        return "none";
      const std::string w = StudioRefName(ST_WEAPONS, n);
      const long long cd = Num(ST_WEAPONS, w, "cooldown");
      return std::to_string(Num(ST_WEAPONS, w, "damage")) + " a hit, every " + Fmt(cd / 24.0, 2) + " s, " +
             RangeWords(Num(ST_WEAPONS, w, "max_range"));
    }
  }
  if (t == ST_WEAPONS) {
    if (f.key == "cooldown")
      return Fmt(n / 24.0, 2) + " seconds between shots";
    if (f.key == "max_range" || f.key == "min_range")
      return RangeWords(n);
  }
  if (f.editor == "frames")
    return Fmt(n / 24.0) + " seconds";
  return f.help;
}

std::string ValueWords(const StudioField &f, const StudioValue &v) {
  if (f.bPath)
    return v.own.Str();
  if (f.nColumn < 0)
    return v.own.kind == PortJson::NONE ? std::string() : PortJsonWrite(v.own);
  std::string out;
  for (size_t i = 0; i < v.n.size(); ++i) {
    if (i)
      out += ", ";
    if (f.editor.compare(0, 5, "enum:") == 0) {
      std::string label;
      for (const auto &e : StudioEnum(f.editor.substr(5)))
        if (e.first == v.n[i])
          label = e.second;
      out += label.empty() ? std::to_string(v.n[i]) : label;
    } else if (f.nRef >= 0) {
      // the row past the classic table's last is its "none" (130 for weapons, 228 for units)
      out += v.n[i] == StudioClassicRows(f.nRef) ? std::string("none") : StudioShowName(f.nRef, StudioRefName(f.nRef, v.n[i]));
    } else {
      out += std::to_string(v.n[i]);
    }
  }
  return out;
}

// ---- the pieces of the frame ---------------------------------------------------------------------------------

// A child region at a place, scrolling, with no ground of its own.
bool Region(const char *pszId, ImVec2 a, ImVec2 b, bool bScroll = true) {
  ImGui::SetCursorScreenPos(a);
  return ImGui::BeginChild(pszId, ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoBackground | (bScroll ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
}

// a small label in wide capitals (cs-lab)
void Lab(const char *psz) {
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), psz, 0.1f, true);
}

// a row's ground when hovered or chosen
bool RowHit(const char *pszId, ImVec2 a, float w, float h, bool bSel, bool *pbHover = nullptr) {
  ImGui::SetCursorScreenPos(a);
  const bool b = ImGui::InvisibleButton(pszId, ImVec2(w, h));
  const bool bHover = ImGui::IsItemHovered();
  if (pbHover)
    *pbHover = bHover;
  ImDrawList *d = ImGui::GetWindowDrawList();
  if (bSel)
    d->AddRectFilled(a, ImVec2(a.x + w, a.y + h), C(TK_SEL));
  else if (bHover)
    d->AddRectFilled(a, ImVec2(a.x + w, a.y + h), C(TK_ROW));
  return b;
}

// ---- the menu bar ---------------------------------------------------------------------------------------------

void NewRecordOpen(int t);
void MenuItems(const char *pszMenu);

void MenuBar(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float h = b.y - a.y;
  float x = a.x + P(10.0f);
  StudioGlyph(d, "race", ImVec2(x, Mid(a.y, h, 16.0f)), P(16.0f), C(TK_LABEL));
  x += P(24.0f);
  x += Wide(d, 11.0f, x, Mid(a.y, h, 11.0f), C(TK_HEAD), "Hyperion Studio", 0.08f) + P(8.0f);
  if (StudioIsOpen()) {
    const PortJson &mod = StudioDoc("mod");
    const std::string em = mod["name"].Str(StudioModId().c_str()) + " " + mod["version"].Str();
    x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_CHANGED), em);
  }
  x += P(18.0f);
  // the menus: a click opens one, and with one open the pointer moves between them
  static const char *const kMenus[] = {"File", "Edit", "View", "Workspace", "Test", "Help"};
  static int s_nOpen = -1;
  bool bAnyOpen = false;
  for (int i = 0; i < 6; ++i)
    bAnyOpen |= ImGui::IsPopupOpen((std::string("##menu") + kMenus[i]).c_str());
  if (!bAnyOpen)
    s_nOpen = -1;
  for (int i = 0; i < 6; ++i) {
    const float w = TW(STUDIO_FONT_TEXT, kStudioFsS, kMenus[i]) + P(20.0f);
    const ImVec2 p(x, a.y + (h - P(24.0f)) / 2.0f);
    ImGui::SetCursorScreenPos(p);
    ImGui::PushID(i);
    const bool bClick = ImGui::InvisibleButton("##m", ImVec2(w, P(24.0f)));
    const bool bHover = ImGui::IsItemHovered();
    ImGui::PopID();
    const std::string id = std::string("##menu") + kMenus[i];
    if (bClick || (bAnyOpen && bHover && s_nOpen != i)) {
      if (s_nOpen >= 0 && s_nOpen != i)
        ImGui::ClosePopupToLevel(0, true);
      ImGui::OpenPopup(id.c_str());
      s_nOpen = i;
    }
    const bool bLit = bHover || s_nOpen == i;
    if (bLit)
      d->AddRectFilled(p, ImVec2(p.x + w, p.y + P(24.0f)), C(TK_MENU_SEL), P(2.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x + P(10.0f), Mid(a.y, h, kStudioFsS), bLit ? C(TK_HI) : C(TK_BUTTON), kMenus[i]);
    ImGui::SetNextWindowPos(ImVec2(p.x, b.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(4.0f), P(4.0f)));
    if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoMove)) {
      ImGui::PopStyleVar();
      MenuItems(kMenus[i]);
      ImGui::EndPopup();
    } else {
      ImGui::PopStyleVar();
    }
    x += w + P(2.0f);
  }
  // the right: the palette's field and the chips
  float r = b.x - P(10.0f);
  const bool bRules = StudioIsOpen() && StudioDoc("types").kind == PortJson::OBJECT && !StudioDoc("types").members.empty();
  if (bRules)
    r -= StudioChipW("Changes the rules") + P(6.0f);
  const std::string claude = "Claude on " + std::to_string(PortControlPort());
  r -= StudioChipW(claude.c_str()) + P(6.0f);
  const float fy = a.y + (h - P(18.0f)) / 2.0f;
  float cx = r + P(6.0f);
  cx += StudioChip(d, ImVec2(cx, fy), claude.c_str(), CH_CLAUDE) + P(6.0f);
  if (bRules)
    StudioChip(d, ImVec2(cx, fy), "Changes the rules", CH_GOLD);
  const float fw = 170.0f;
  ImGui::SetCursorScreenPos(ImVec2(r - P(fw + 8.0f), a.y + (h - P(22.0f)) / 2.0f));
  static char s_szAnything[64];
  StudioInput("##anything", s_szAnything, sizeof s_szAnything, fw, 22.0f, "Find anything", "search", "Ctrl K", false,
              kStudioFsXs);
  if (ImGui::IsItemActivated() || s_szAnything[0]) {
    // the explorer's search does the finding for now
    snprintf(g_szFind, sizeof g_szFind, "%s", s_szAnything);
  }
  d->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), C(TK_LINE));
}

void MenuItems(const char *pszMenu) {
  const std::string m = pszMenu;
  if (m == "File") {
    if (StudioItem("New mod"))
      StudioNewModOpen();
    if (ImGui::BeginMenu("Open a mod")) {
      for (const PortMod &mod : PortMods())
        if (!mod.bBuiltIn && !mod.data.empty() && StudioItem((mod.name + "  (" + mod.id + ")").c_str(), mod.id == StudioModId()))
          StudioAsk("studio_open", "\"mod\":" + Q(mod.id));
      ImGui::EndMenu();
    }
    if (StudioItem("New unit", false, nullptr, StudioIsOpen()))
      NewRecordOpen(ST_UNITS);
    if (StudioItem("The mod's page", g_nWs == 8, nullptr, StudioIsOpen()))
      g_nWs = 8;
    StudioItemSeparator();
    if (StudioItem("Save", false, "Ctrl S", StudioIsOpen()))
      StudioAsk("studio_save", "");
    std::string when;
    if (StudioItem("Take back unsaved work", false, nullptr, StudioRecoverWaiting(&when)))
      StudioAsk("studio_recover", "");
    StudioItemSeparator();
    if (StudioItem("Quit"))
      DesignCall("design_quit", "", nullptr);
  } else if (m == "Edit") {
    if (StudioItem("Undo", false, "Ctrl Z", StudioHistoryAt() > 0))
      StudioAsk("studio_undo", "");
    if (StudioItem("Redo", false, "Ctrl Y", StudioHistoryAt() < static_cast<int>(StudioHistory().size())))
      StudioAsk("studio_redo", "");
    StudioItemSeparator();
    if (StudioItem("Find in the mod", false, "Ctrl K"))
      g_bFocusFind = true;
    if (StudioItem("Filter the fields", false, "Ctrl F", g_current.t >= 0))
      g_bFocusFilter = true;
  } else if (m == "View") {
    for (int pc : {100, 125, 150, 175, 200}) {
      char sz[24];
      snprintf(sz, sizeof sz, "Scale %d %%", pc);
      if (StudioItem(sz, static_cast<int>(StudioScale() * 100.0f + 0.5f) == pc))
        StudioScaleSet(pc / 100.0f);
    }
    StudioItemSeparator();
    if (StudioItem("Light", StudioLight()))
      StudioLightSet(!StudioLight());
    if (StudioItem("The dock", g_bDock))
      g_bDock = !g_bDock;
    if (StudioItem("Ask Claude", g_bDock && g_nDockTab == 4)) {
      g_nDockTab = 4;
      g_bDock = true;
      StudioClaudeFocus();
    }
  } else if (m == "Workspace") {
    static const char *const kWs[] = {"Make", "Tables", "Art", "Looks", "Effects", "Cards and races", "Test", "Checks", "The mod's page",
                                      "Strings", "Balance", "Statuses", "Tilesets", "Scripts", "Mode"};
    for (int i = 0; i < 15; ++i)
      if (StudioItem(kWs[i], g_nWs == i))
        g_nWs = i;
  } else if (m == "Test") {
    if (StudioItem("Push", false, "F5", StudioIsOpen() && DesignInGame()))
      Push();
    if (StudioItem("Push as I edit", g_bAuto))
      g_bAuto = !g_bAuto;
    if (StudioItem("Try it", false, nullptr, g_current.t == ST_UNITS && DesignInGame()))
      TryIt(g_current.name);
    if (StudioItem("Again", false, "F9", !g_trying.empty()))
      Again();
    if (StudioItem(g_bPaused ? "Go on" : "Pause", false, nullptr, DesignInGame())) {
      DesignCall(g_bPaused ? "resume" : "pause", "", nullptr);
      g_bPaused = !g_bPaused;
    }
  } else if (m == "Help") {
    StudioItem("Ctrl S saves, Ctrl Z undoes, F5 pushes into the game", false, nullptr, false);
    StudioItem("Claude edits the same mod through the hyperion-studio tools", false, nullptr, false);
  }
}

// ---- the toolbar ----------------------------------------------------------------------------------------------

bool Tool(float *px, float y, float h, const char *pszLabel, const char *pszGlyph, bool bPressed, int nBadge = 0,
          int nInk = -1, bool bEnabled = true) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float w = fmaxf(P(58.0f), TW(STUDIO_FONT_TEXT, kStudioFsS, pszLabel) + P(20.0f));
  const ImVec2 a(*px, y), b(*px + w, y + h);
  ImGui::SetCursorScreenPos(a);
  ImGui::PushID(pszLabel);
  if (!bEnabled)
    ImGui::BeginDisabled();
  const bool bClick = ImGui::InvisibleButton("##tool", ImVec2(w, h));
  const bool bHover = ImGui::IsItemHovered();
  if (!bEnabled)
    ImGui::EndDisabled();
  ImGui::PopID();
  const float fAlpha = bEnabled ? 1.0f : 0.38f;
  if (bPressed) {
    d->AddRectFilledMultiColor(a, b, IM_COL32(0x00, 0x2A, 0x55, 255), IM_COL32(0x00, 0x2A, 0x55, 255),
                               IM_COL32(0x00, 0x56, 0xA0, 255), IM_COL32(0x00, 0x56, 0xA0, 255));
    d->AddLine(ImVec2(a.x + 1, b.y - P(1.0f)), ImVec2(b.x - 1, b.y - P(1.0f)), C(TK_SEL_LINE2), P(2.0f));
    d->AddRect(a, b, C(TK_BTN_RIM_HI), P(3.0f));
  } else if (bHover && bEnabled) {
    d->AddRect(a, b, C(TK_LINE2), P(3.0f));
  }
  const ImU32 ink = nInk >= 0 ? C(nInk, fAlpha) : (bPressed || (bHover && bEnabled)) ? C(TK_HI) : C(TK_BUTTON, fAlpha);
  const float gy = y + (h - P(18.0f + 3.0f) - P(kStudioFsS)) / 2.0f;
  StudioGlyph(d, pszGlyph, ImVec2(a.x + (w - P(18.0f)) / 2.0f, gy), P(18.0f), ink);
  const float tw = TW(STUDIO_FONT_TEXT, kStudioFsS, pszLabel);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + (w - tw) / 2.0f, gy + P(21.0f), ink, pszLabel);
  if (nBadge > 0) {
    const std::string n = std::to_string(nBadge);
    const float bw = fmaxf(P(15.0f), TW(STUDIO_FONT_MONO, 10.0f, n) + P(8.0f));
    const ImVec2 p(b.x - P(4.0f) - bw, a.y + P(2.0f));
    d->AddRectFilled(p, ImVec2(p.x + bw, p.y + P(15.0f)), C(TK_GOLD_BOT), P(8.0f));
    d->AddRect(p, ImVec2(p.x + bw, p.y + P(15.0f)), C(TK_GOLD_RIM), P(8.0f));
    T(d, STUDIO_FONT_MONO, 10.0f, p.x + (bw - TW(STUDIO_FONT_MONO, 10.0f, n)) / 2.0f, Mid(p.y, P(15.0f), 10.0f),
      C(TK_CHANGED), n);
  }
  *px += w + P(4.0f);
  return bClick && bEnabled;
}

void ToolSep(float *px, float y, float h) {
  ImGui::GetWindowDrawList()->AddLine(ImVec2(*px + P(6.0f), y + P(6.0f)), ImVec2(*px + P(6.0f), y + h - P(6.0f)), C(TK_LINE));
  *px += P(13.0f) + P(4.0f);
}

void Toolbar(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddRectFilledMultiColor(a, b, C(TK_TOOL_TOP), C(TK_TOOL_TOP), C(TK_TOOL_BOT), C(TK_TOOL_BOT));
  d->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), C(TK_LINE));
  const float y = a.y + P(4.0f), h = b.y - a.y - P(8.0f);
  float x = a.x + P(10.0f);
  const bool bOpen = StudioIsOpen();
  if (Tool(&x, y, h, "Save", "save", false, StudioUnsaved(), -1, bOpen))
    StudioAsk("studio_save", "");
  if (Tool(&x, y, h, "Undo", "undo", false, 0, -1, StudioHistoryAt() > 0))
    StudioAsk("studio_undo", "");
  if (Tool(&x, y, h, "Redo", "redo", false, 0, -1, StudioHistoryAt() < static_cast<int>(StudioHistory().size())))
    StudioAsk("studio_redo", "");
  ToolSep(&x, y, h);
  static const char *const kWs[] = {"Make", "Tables", "Art", "Looks", "Effects", "Cards and races", "Test", "Checks", "", "Strings", "Balance", "Statuses", "Tilesets", "Scripts", "Mode"};
  static const char *const kWsGlyph[] = {"make", "table", "image", "look", "fx", "cards", "game", "check", "", "text", "chart", "status", "tiles", "pen", "engine"};
  static const int kWsView[] = {-1, -1, 3, 4, -1, 6, -1, -1, -1, -1, -1};
  // the toolbar's order: making and the mod's data, then testing and judging it
  static const int kWsOrder[] = {0, 1, 9, 2, 3, 4, 11, 13, 14, 5, 12, 6, 10, 7};
  for (int i : kWsOrder) {
    if (i == 6)
      ToolSep(&x, y, h);
    const int nBadge = i == 7 ? StudioCheckProblems(nullptr, nullptr) : 0;
    if (Tool(&x, y, h, kWs[i], kWsGlyph[i], g_nWs == i, nBadge)) {
      g_nWs = i;
      if (kWsView[i] >= 0)
        g_nView = kWsView[i];
      if (i == 6) {
        g_nDockTab = 0;
        g_bDock = true;
      }
    }
  }
  ToolSep(&x, y, h);
  if (Tool(&x, y, h, "Push", "push", false, 0, -1, bOpen && DesignInGame()))
    Push();
  if (Tool(&x, y, h, "Auto", "again", g_bAuto))
    g_bAuto = !g_bAuto;
  const bool bCanTry = g_current.t == ST_UNITS && DesignInGame();
  if (Tool(&x, y, h, "Try it", "try", false, 0, TK_GOOD, bCanTry))
    TryIt(g_current.name);
  ToolSep(&x, y, h);
  if (Tool(&x, y, h, "Claude", "claude", g_bDock && g_nDockTab == 4, StudioClaudeBadge(), TK_CLAUDE)) {
    g_nDockTab = 4;
    g_bDock = true;
    StudioClaudeFocus();
  }
  // what is being tried, at the right
  if (!g_tried.empty()) {
    const float rx = b.x - P(10.0f);
    const float fy = a.y + P(8.0f);
    const std::string unit = StudioShowName(ST_UNITS, g_trying), vs = kScenarios[g_nScenario].pszLabel;
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, rx - TW(STUDIO_FONT_TEXT, kStudioFsXs, "Trying"), fy, C(TK_INK3), "Trying");
    T(d, STUDIO_FONT_BOLD, kStudioFsXs, rx - TW(STUDIO_FONT_BOLD, kStudioFsXs, unit), fy + P(13.0f), C(TK_INK), unit);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, rx - TW(STUDIO_FONT_TEXT, kStudioFsXs, vs), fy + P(26.0f), C(TK_INK3), vs);
  }
}

// ---- the explorer ---------------------------------------------------------------------------------------------

int g_nNewTable = -1;
char g_szNewName[64], g_szNewBase[64];

void NewRecordOpen(int t) {
  if (t == ST_UNITS) {
    StudioNewUnitOpen(std::string());
    return;
  }
  g_nNewTable = t;
  g_szNewName[0] = '\0';
  snprintf(g_szNewBase, sizeof g_szNewBase, "%s", t == ST_UNITS ? "protoss_zealot" : "");
}

void Explorer(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioPane(d, a, b, 30.0f);
  Wide(d, 10.5f, a.x + P(8.0f), Mid(a.y, P(30.0f), 10.5f), C(TK_HEAD), "Explorer");
  // the search
  const float sy = a.y + P(30.0f);
  ImGui::SetCursorScreenPos(ImVec2(a.x + P(8.0f), sy + P(6.0f)));
  StudioGlyph(d, "search", ImVec2(a.x + P(8.0f), sy + P(10.0f)), P(15.0f), C(TK_INK3));
  ImGui::SetCursorScreenPos(ImVec2(a.x + P(29.0f), sy + P(6.0f)));
  if (g_bFocusFind) {
    ImGui::SetKeyboardFocusHere();
    g_bFocusFind = false;
  }
  StudioInput("##find", g_szFind, sizeof g_szFind, (b.x - a.x) / P(1.0f) - 37.0f, 24.0f, "Find in the mod and classic");
  const float ly = sy + P(36.0f);
  d->AddLine(ImVec2(a.x, ly - 0.5f), ImVec2(b.x, ly - 0.5f), C(TK_LINE));
  if (!Region("##explorer", ImVec2(a.x + 1, ly), ImVec2(b.x - 1, b.y - 1))) {
    ImGui::EndChild();
    return;
  }
  d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  if (!StudioIsOpen()) {
    ImGui::Dummy(ImVec2(w, P(8.0f)));
    ImGui::SetCursorPosX(P(10.0f));
    ImGui::PushTextWrapPos(w - P(10.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK2));
    ImGui::TextWrapped("No mod is open. File, Open a mod, or studio_open over the channel.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    return;
  }
  for (const Kind &k : kKinds) {
    const std::vector<StudioRecord> recs = StudioRecords(k.t);
    if (recs.empty() && !k.bAlways && !g_szFind[0])
      continue;
    const std::string table = Table(k.t);
    const bool bOpen = g_shut.count(table) == 0 || g_szFind[0];
    ImGui::PushID(k.t);
    // the section's head
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float hh = P(32.0f);
    const float nw = StudioButtonSize("New", "plus", 22.0f, kStudioFsXs).x;
    bool bHover = false;
    if (RowHit("##sec", p, w - nw - P(10.0f), hh, false, &bHover)) {
      if (g_shut.count(table))
        g_shut.erase(table);
      else
        g_shut.insert(table);
    }
    StudioGlyph(d, bOpen ? "down" : "caret", ImVec2(p.x + P(4.0f), Mid(p.y, hh, 12.0f)), P(12.0f), C(TK_INK3));
    StudioGlyph(d, k.pszGlyph, ImVec2(p.x + P(20.0f), Mid(p.y, hh, 15.0f)), P(15.0f), C(TK_INK2));
    float x = p.x + P(41.0f);
    x += Wide(d, 10.5f, x, Mid(p.y, hh, 10.5f), C(TK_HEAD), k.pszLabel, 0.08f) + P(6.0f);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, x, Mid(p.y, hh, kStudioFsXs) + P(1.0f), C(TK_INK3), std::to_string(recs.size()));
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - nw - P(6.0f), p.y + (hh - P(22.0f)) / 2.0f));
    if (StudioButton("##new", "New", "plus", SB_PLAIN, 22.0f, 0.0f, kStudioFsXs))
      NewRecordOpen(k.t);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hh));
    if (bOpen) {
      for (const StudioRecord &r : recs) {
        const std::string show = StudioShowName(k.t, r.name);
        if (!Has(show, g_szFind) && !Has(r.name, g_szFind))
          continue;
        p = ImGui::GetCursorScreenPos();
        const float rh = P(28.0f);
        const bool bSel = g_current.t == k.t && g_current.name == r.name;
        ImGui::PushID(r.name.c_str());
        if (RowHit("##r", p, w, rh, bSel))
          OpenDoc(k.t, r.name);
        if (ImGui::BeginPopupContextItem("##ctx")) {
          if (StudioItem("Open"))
            OpenDoc(k.t, r.name);
          if (StudioItem("Duplicate", false, nullptr, Mine(r)))
            StudioAsk("studio_create", RecordFields(k.t, r.name + "_copy") + ",\"base\":" + Q(r.name));
          if (StudioItem("Take it out of the mod", false, nullptr, Mine(r)))
            StudioAsk("studio_delete", RecordFields(k.t, r.name));
          ImGui::EndPopup();
        }
        ImGui::PopID();
        StudioGlyph(d, k.pszGlyph, ImVec2(p.x + P(26.0f), Mid(p.y, rh, 15.0f)), P(15.0f), bSel ? C(TK_LABEL) : C(TK_INK3));
        std::string from = r.kind == SK_EDIT ? "edit" : r.kind == SK_DEPENDENCY ? "from " + r.mod
                                                       : r.base.empty()            ? ""
                                                                                   : "from " + Short(k.t, r.base);
        const bool bChanged = ChangedSinceSave(k.t, r.name) > 0;
        float right = p.x + w - P(8.0f);
        if (bChanged) {
          d->AddCircleFilled(ImVec2(right - P(3.5f), p.y + rh / 2.0f), P(4.5f), C(TK_BLACK));
          d->AddCircleFilled(ImVec2(right - P(3.5f), p.y + rh / 2.0f), P(3.5f), C(TK_CHANGED));
          right -= P(13.0f);
        }
        const float nx = p.x + P(47.0f);
        const float fw = from.empty() ? 0.0f : fminf(TW(STUDIO_FONT_TEXT, kStudioFsXs, from), (right - nx) * 0.45f);
        if (!from.empty())
          T(d, STUDIO_FONT_TEXT, kStudioFsXs, right - fw, Mid(p.y, rh, kStudioFsXs), C(TK_INK3), from, fw + 1.0f);
        T(d, STUDIO_FONT_TEXT, 13.0f, nx, Mid(p.y, rh, 13.0f), r.kind == SK_DEPENDENCY ? C(TK_INK2) : C(TK_INK), show,
          right - fw - P(8.0f) - nx);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rh));
      }
      // the classic rows, folded and read only
      p = ImGui::GetCursorScreenPos();
      const float ch = P(26.0f);
      const bool bClassic = g_classic.count(table) != 0 || (g_szFind[0] && strlen(g_szFind) >= 2);
      bool bHov = false;
      if (RowHit("##classic", p, w, ch, false, &bHov)) {
        if (g_classic.count(table))
          g_classic.erase(table);
        else
          g_classic.insert(table);
      }
      const ImU32 ink = bHov ? C(TK_INK) : C(TK_INK3);
      StudioGlyph(d, bClassic ? "down" : "caret", ImVec2(p.x + P(24.0f), Mid(p.y, ch, 12.0f)), P(12.0f), ink);
      float cx = p.x + P(42.0f);
      cx += T(d, STUDIO_FONT_TEXT, kStudioFsXs, cx, Mid(p.y, ch, kStudioFsXs), ink, "Classic") + P(6.0f);
      T(d, STUDIO_FONT_MONO, 10.0f, cx, Mid(p.y, ch, 10.0f) + P(1.0f), C(TK_INK3), std::to_string(StudioClassicRows(k.t)));
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + w - P(8.0f) - TW(STUDIO_FONT_TEXT, kStudioFsXs, "read-only"),
        Mid(p.y, ch, kStudioFsXs), ink, "read-only");
      ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ch));
      if (bClassic) {
        int nShown = 0;
        for (int n = 0; n < StudioClassicRows(k.t) && nShown < 300; ++n) {
          const char *psz = StudioClassicName(k.t, n);
          if (psz == nullptr)
            continue;
          const std::string show = StudioShowName(k.t, psz);
          if (!Has(show, g_szFind) && !Has(psz, g_szFind))
            continue;
          ++nShown;
          p = ImGui::GetCursorScreenPos();
          const float rh = P(26.0f);
          ImGui::PushID(n);
          const bool bSel = g_current.t == k.t && g_current.name == psz;
          if (RowHit("##c", p, w, rh, bSel))
            OpenDoc(k.t, psz);
          ImGui::PopID();
          StudioGlyph(d, k.pszGlyph, ImVec2(p.x + P(42.0f), Mid(p.y, rh, 13.0f)), P(13.0f), C(TK_INK3));
          T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(61.0f), Mid(p.y, rh, kStudioFsS), C(TK_INK2), show, w - P(70.0f));
          ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rh));
        }
      }
      ImGui::Dummy(ImVec2(w, P(6.0f)));
    }
    p = ImGui::GetCursorScreenPos();
    d->AddLine(ImVec2(p.x, p.y - 0.5f), ImVec2(p.x + w, p.y - 0.5f), C(TK_LINE));
    ImGui::PopID();
  }
  ImGui::EndChild();
}

// ---- the documents ---------------------------------------------------------------------------------------------

const char *const kUnitViews[] = {"Overview", "Numbers", "Actions", "Art", "Look", "Sounds", "Card and tech"};
const char *const kRecordViews[] = {"Numbers"};
const char *const kWeaponViews[] = {"Weapon"};
// the view a table's record opens on: its head above its numbers
const char *HeadView(int t) {
  switch (t) {
  case ST_WEAPONS: return "Weapon";
  case ST_IMAGES: return "Image";
  case ST_SPRITES: return "Sprite";
  case ST_SOUNDS: return "Sound";
  case ST_PORTRAITS: return "Portrait";
  case ST_ORDERS: return "Action";
  case ST_TECHS: return "Research";
  case ST_UPGRADES: return "Upgrade";
  default: return "Numbers";
  }
}

void Placeholder(ImVec2 a, ImVec2 b, const char *pszGlyph, const std::string &line, const std::string &fine) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioDashed(d, a, b, C(TK_LINE2));
  const float cy = (a.y + b.y) / 2.0f;
  StudioGlyph(d, pszGlyph, ImVec2((a.x + b.x) / 2.0f - P(13.0f), cy - P(40.0f)), P(26.0f), C(TK_INK3));
  const float lw = TW(STUDIO_FONT_TEXT, kStudioFs, line);
  T(d, STUDIO_FONT_TEXT, kStudioFs, (a.x + b.x - lw) / 2.0f, cy - P(4.0f), C(TK_INK2), line);
  if (!fine.empty()) {
    const float fw = TW(STUDIO_FONT_TEXT, kStudioFsS, fine);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, (a.x + b.x - fw) / 2.0f, cy + P(18.0f), C(TK_INK3), fine);
  }
}

// one tile of the overview's numbers
bool Tile(const char *pszId, ImVec2 a, float w, float h, const std::string &label, const std::string &value,
          const std::string &unit, const std::string &work) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImGui::SetCursorScreenPos(a);
  const bool b = ImGui::InvisibleButton(pszId, ImVec2(w, h));
  const bool bHover = ImGui::IsItemHovered();
  d->AddRectFilled(a, ImVec2(a.x + w, a.y + h), IM_COL32(8, 28, 52, 107), P(3.0f));
  d->AddRect(a, ImVec2(a.x + w, a.y + h), bHover ? C(TK_BTN_RIM_HI) : C(TK_LINE), P(3.0f));
  const float x = a.x + P(13.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, a.y + P(11.0f), C(TK_INK3), label);
  const float vw = T(d, STUDIO_FONT_BOLD, 26.0f, x, a.y + P(28.0f), C(TK_HI), value);
  T(d, STUDIO_FONT_TEXT, 13.0f, x + vw + P(6.0f), a.y + P(39.0f), C(TK_INK2), unit, w - vw - P(32.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, a.y + P(62.0f), C(TK_INK2), work, w - P(26.0f));
  return b;
}

void Overview(int t, const std::string &name, const StudioRecord &rec) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(16.0f);
  const float W = w - P(44.0f);
  // the picture
  const ImVec2 pa = o, pb(o.x + P(230.0f), o.y + P(190.0f));
  d->AddRectFilled(pa, pb, C(TK_WELL), P(3.0f));
  d->AddRect(pa, pb, C(TK_LINE), P(3.0f));
  if (!StudioUnitPicture(d, ImVec2(pa.x + 1, pa.y + 1), ImVec2(pb.x - 1, pb.y - P(24.0f)), name, Facing(), g_nColour)) {
    d->AddCircle(ImVec2((pa.x + pb.x) / 2.0f, (pa.y + pb.y) / 2.0f - P(8.0f)), P(54.0f), C(TK_SEL_LINE2, 0.35f), 48, P(1.0f));
    StudioGlyph(d, "unit", ImVec2((pa.x + pb.x) / 2.0f - P(24.0f), (pa.y + pb.y) / 2.0f - P(32.0f)), P(48.0f), C(TK_INK3));
  }
  const std::string art = StudioUnitArtWords(name);
  const float aw = TW(STUDIO_FONT_TEXT, kStudioFsXs, art);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, (pa.x + pb.x - aw) / 2.0f, pb.y - P(20.0f), C(TK_INK3), art);
  // the title, its chips, the sentence and the buttons
  float x = pb.x + P(22.0f);
  float y = o.y;
  T(d, STUDIO_FONT_BOLD, 28.0f, x, y, C(TK_HI), StudioShowName(t, name));
  y += P(38.0f);
  float cx = x;
  cx += StudioChip(d, ImVec2(cx, y), RaceOf(name).c_str()) + P(6.0f);
  if (!rec.base.empty())
    cx += StudioChip(d, ImVec2(cx, y), ("Based on the " + Short(t, rec.base)).c_str()) + P(6.0f);
  if (ChangedSinceSave(t, name) > 0)
    StudioChip(d, ImVec2(cx, y), "Changed since the save", CH_CHANGED);
  y += P(26.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  ImGui::PushTextWrapPos(o.x + W - ImGui::GetWindowPos().x);
  StudioPushFont(STUDIO_FONT_TEXT, 16.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK));
  ImGui::TextWrapped("%s", Sentence(name).c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
  y = ImGui::GetCursorScreenPos().y + P(10.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  const std::string vs = std::string("Try it against ") + (kScenarios[g_nScenario].pszLabel + 3);
  if (StudioButton("##try", vs.c_str(), "play", SB_GOOD, 28.0f, 0.0f, 13.0f, DesignInGame()))
    TryIt(name);
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##nums", "Edit its numbers", "table", SB_PLAIN, 28.0f, 0.0f, 13.0f))
    g_nView = 1;
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##dup", "Duplicate", "copy", SB_PLAIN, 28.0f, 0.0f, 13.0f, Mine(rec)))
    StudioAsk("studio_create", RecordFields(t, name + "_copy") + ",\"base\":" + Q(name));
  y = fmaxf(pb.y, ImGui::GetCursorScreenPos().y) + P(22.0f);
  // its numbers in tiles, and what it still needs beside them
  const float side = W > P(1100.0f) ? P(460.0f) : P(360.0f);
  const bool bSide = W > P(760.0f);
  const float tw = bSide ? W - side - P(26.0f) : W;
  float hx = o.x;
  hx += T(d, STUDIO_FONT_BOLD, 16.0f, hx, y, C(TK_HEAD), "Its numbers") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, hx, y + P(3.0f), C(TK_INK3), "in plain words, with what they work out to");
  const float ty = y + P(30.0f);
  const Fight f = FightOf(name);
  const long long hp = Num(t, name, "hit_points"), sh = Shields(t, name);
  const long long m = Num(t, name, "minerals"), g = Num(t, name, "gas"), bt = Num(t, name, "build_time");
  const long long sup = Num(t, name, "supply_required");
  struct TileDef {
    std::string label, value, unit, work;
  };
  const TileDef tiles[] = {
      {"Life", std::to_string(hp), "life", sh > 0 ? "+ " + std::to_string(sh) + " shields, " + std::to_string(hp + sh) + " in all" : "no shields"},
      {"Armour", std::to_string(Num(t, name, "armor")), "armour", "a Marine's 6 a shot does " + Fmt(SDL_max(0.5, 6.0 - Num(t, name, "armor")))},
      {"Size", SizeWord(Num(t, name, "size")), "", SizeRule(Num(t, name, "size"))},
      {"Cost", std::to_string(m) + " / " + std::to_string(g), "minerals / gas", "worth " + Fmt(m + g * 1.5) + " with gas at 1.5"},
      {"Time to make", Fmt(bt / 24.0), "seconds", std::to_string(bt) + " game frames at Fastest"},
      {"Supply", Fmt(sup / 2.0), "supply", sup > 0 ? Fmt(400.0 / sup) + " of them fill 200" : "uses none"},
      {"Damage", f.weapon.empty() ? "none" : Fmt(f.perSecond), f.weapon.empty() ? "" : "a second",
       f.weapon.empty() ? "it has no weapon" : std::to_string(f.damage * f.hits) + " a hit, every " + Fmt(f.cooldown / 24.0, 2) + " s"},
      {"Range", f.weapon.empty() ? "none" : RangeWords(f.range), "", f.weapon.empty() ? "" : std::to_string(f.range) + " px to its target"},
      {"Sight", std::to_string(Num(t, name, "sight_range")), "tiles", "how far it sees"},
  };
  const int nCols = SDL_max(1, static_cast<int>((tw + P(8.0f)) / P(204.0f)));
  const float cw = (tw - P(8.0f) * (nCols - 1)) / nCols, chh = P(94.0f);
  for (int i = 0; i < 9; ++i) {
    const ImVec2 at(o.x + (i % nCols) * (cw + P(8.0f)), ty + (i / nCols) * (chh + P(8.0f)));
    ImGui::PushID(i);
    if (Tile("##tile", at, cw, chh, tiles[i].label, tiles[i].value, tiles[i].unit, tiles[i].work))
      g_nView = 1;
    ImGui::PopID();
  }
  float bottom = ty + ((9 + nCols - 1) / nCols) * (chh + P(8.0f));
  // still needs
  const float sx = bSide ? o.x + tw + P(26.0f) : o.x;
  float sy = bSide ? y : bottom + P(16.0f);
  struct Need {
    std::string text;
    int state;  // 0 done, 1 needs, 2 optional
    std::string fine;
  };
  auto own = [&](const char *pszKey) {
    StudioValue v;
    return StudioGet(t, name, pszKey, &v) && v.source == SS_OWN;
  };
  const bool bNums = own("hit_points") || own("armor") || own("minerals") || own("ground_weapon");
  std::vector<Need> needs = {
      {"Numbers set", bNums ? 0 : 1, bNums ? "" : "every number is its base's"},
      {"Art of its own", own("flingy") ? 0 : 2, own("flingy") ? "" : "borrows the " + Short(t, rec.base.empty() ? name : rec.base) + "'s"},
      {"A look", own("looks") ? 0 : 2, ""},
      {"Sounds of its own", own("ready_sound") || own("yes_sound_first") ? 0 : 2, ""},
      {"A button that makes it", own("requires") || own("buttons") ? 0 : 1, ""},
      {"A portrait", own("portrait") ? 0 : 2, own("portrait") ? "" : "borrows its base's"},
  };
  int nDone = 0, nOpt = 0;
  for (const Need &n : needs) {
    nDone += n.state == 0;
    nOpt += n.state == 2;
  }
  const float sh2 = P(52.0f) + needs.size() * P(36.0f) + P(12.0f);
  d->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + (bSide ? side : W), sy + sh2), IM_COL32(8, 28, 52, 107), P(3.0f));
  d->AddRect(ImVec2(sx, sy), ImVec2(sx + (bSide ? side : W), sy + sh2), C(TK_LINE), P(3.0f));
  float nx = sx + P(14.0f);
  nx += T(d, STUDIO_FONT_BOLD, 16.0f, nx, sy + P(12.0f), C(TK_HEAD), "Still needs") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, nx, sy + P(15.0f),
    C(TK_INK3), std::to_string(nDone) + " of " + std::to_string(needs.size() - nOpt) + " done, " + std::to_string(nOpt) + " optional");
  float ny = sy + P(46.0f);
  for (const Need &n : needs) {
    const int tok = n.state == 0 ? TK_GOOD : n.state == 1 ? TK_WARN : TK_INK3;
    StudioGlyph(d, n.state == 0 ? "done" : n.state == 1 ? "needs" : "note", ImVec2(sx + P(14.0f), ny + P(9.0f)), P(18.0f), C(tok));
    T(d, STUDIO_FONT_TEXT, 13.5f, sx + P(44.0f), ny + P(4.0f), n.state == 0 ? C(TK_INK2) : C(TK_INK), n.text);
    if (!n.fine.empty())
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, sx + P(44.0f), ny + P(20.0f), C(TK_INK3), n.fine);
    const char *pszState = n.state == 0 ? "done" : n.state == 1 ? "needs it" : "optional";
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, sx + (bSide ? side : W) - P(14.0f) - TW(STUDIO_FONT_TEXT, kStudioFsXs, pszState),
      ny + P(8.0f), C(TK_INK3), pszState);
    ny += P(36.0f);
  }
  bottom = fmaxf(bottom, sy + sh2);
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), bottom + P(28.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

void Numbers(int t, const std::string &name, const StudioRecord &rec) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const bool bMine = Mine(rec);
  const bool bBig = g_nLevel == 0;
  const float w = ImGui::GetContentRegionAvail().x;
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(14.0f);
  const float W = w - P(44.0f);
  float y = o.y;
  if (rec.kind == SK_CLASSIC) {
    // a classic record: read only until it is edited in the mod or made into one of its own
    const ImVec2 a(o.x, y), b(o.x + W, y + P(46.0f));
    d->AddRectFilledMultiColor(a, b, C(TK_GOLD_TOP), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), C(TK_GOLD_TOP));
    d->AddRect(a, b, C(TK_GOLD_RIM), P(3.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(12.0f), Mid(a.y, P(46.0f), kStudioFsS), C(TK_INK),
      "A classic " + std::string(KindOf(t).pszLabel).substr(0, strlen(KindOf(t).pszLabel) - 1) +
          ": read only. Edit it in the mod, or make one of your own from it.",
      W - P(330.0f));
    ImGui::SetCursorScreenPos(ImVec2(b.x - P(320.0f), a.y + P(9.0f)));
    if (StudioButton("##edit", "Edit in the mod", "pen", SB_GOLD, 28.0f))
      StudioAsk("studio_create", RecordFields(t, name + "_edit") + ",\"edit\":" + Q(name));
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##from", "Make one from it", "plus", SB_PLAIN, 28.0f)) {
      if (t == ST_UNITS) {
        StudioNewUnitOpen(name);
      } else {
        NewRecordOpen(t);
        snprintf(g_szNewBase, sizeof g_szNewBase, "%s", name.c_str());
      }
    }
    y += P(60.0f);
  }
  // the marks' legend and what goes beside each value
  {
    float x = o.x;
    const float lh = P(24.0f);
    d->AddRectFilled(ImVec2(x, y + P(5.0f)), ImVec2(x + P(4.0f), y + P(19.0f)), C(TK_MOD), P(2.0f));
    x += P(10.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(y, lh, kStudioFsXs), C(TK_INK2), "its own value") + P(16.0f);
    const std::string base = rec.base.empty() ? "classic" : Short(t, rec.base);
    StudioDashed(d, ImVec2(x, y + P(4.0f)), ImVec2(x + P(30.0f), y + P(19.0f)), C(TK_INK3));
    T(d, STUDIO_FONT_TEXT, 10.0f, x + P(4.0f), Mid(y, lh, 10.0f), C(TK_INK3), "from");
    x += P(36.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(y, lh, kStudioFsXs), C(TK_INK2), "inherited from the " + base) + P(16.0f);
    d->AddCircleFilled(ImVec2(x + P(4.5f), y + lh / 2.0f), P(4.5f), C(TK_CHANGED));
    x += P(14.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(y, lh, kStudioFsXs), C(TK_INK2), "changed since the save") + P(16.0f);
    StudioGlyph(d, "undo", ImVec2(x, y + P(5.0f)), P(14.0f), C(TK_INK2));
    x += P(20.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(y, lh, kStudioFsXs), C(TK_INK2), "back to the inherited value");
    static const char *const kBeside[] = {"nothing", "its base's value", "the classic Zealot's"};
    ImGui::SetCursorScreenPos(ImVec2(o.x + W - P(136.0f), y));
    if (StudioSelectBegin("##beside", kBeside[g_nBeside], 136.0f, 24.0f)) {
      for (int i = 0; i < 3; ++i)
        if (StudioItem(kBeside[i], g_nBeside == i))
          g_nBeside = i;
      StudioSelectEnd();
    }
    const float bw = TW(STUDIO_FONT_TEXT, kStudioFsS, "Beside each value");
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + W - P(144.0f) - bw, Mid(y, lh, kStudioFsS), C(TK_INK2), "Beside each value");
    y += lh + P(10.0f);
    d->AddLine(ImVec2(o.x, y), ImVec2(o.x + W, y), C(TK_LINE));
    y += P(14.0f);
  }
  std::vector<StudioValue> row;
  std::string why;
  if (!StudioRow(t, name, &row, &why)) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, y, C(TK_BAD), "It does not resolve: " + why);
    ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(30.0f)));
    ImGui::Dummy(ImVec2(1, 1));
    return;
  }
  // the fields by category, as the level asks
  const std::vector<StudioField> &fields = StudioFields(t);
  const float colLabel = bBig ? 160.0f : 180.0f, colEd = bBig ? 320.0f : 250.0f;
  const float rh = P(bBig ? 54.0f : 36.0f);
  int nShown = 0;
  for (const StudioCategory &cat : StudioCategories(t)) {
    bool bHead = false;
    for (const StudioField &f : fields) {
      if (f.cat != cat.id || (bBig && !f.bSimple))
        continue;
      if (g_szFilter[0] && !Has(f.label, g_szFilter) && !Has(f.key, g_szFilter))
        continue;
      StudioValue v;
      if (!StudioGet(t, name, f.key, &v))
        continue;
      if (!bHead) {
        y += P(bBig ? 8.0f : 12.0f);
        T(d, STUDIO_FONT_BOLD, bBig ? 14.0f : 15.0f, o.x, y, bBig ? C(TK_LABEL) : C(TK_HEAD), cat.label);
        y += P(bBig ? 26.0f : 28.0f);
        bHead = true;
      }
      ++nShown;
      const bool bOwn = v.source == SS_OWN;
      const bool bChanged = bMine && ChangedSinceSave(t, name, f.key) > 0;
      ImGui::PushID(f.key.c_str());
      ImGui::SetCursorScreenPos(ImVec2(o.x, y));
      ImGui::SetNextItemAllowOverlap();
      ImGui::InvisibleButton("##row", ImVec2(W, rh));
      if (ImGui::IsItemHovered())
        d->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + W, y + rh), C(TK_ROW));
      if (!f.help.empty() && ImGui::IsItemHovered() && ImGui::GetIO().MousePos.x < o.x + P(colLabel + 14.0f))
        ImGui::SetTooltip("%s", f.help.c_str());
      d->AddLine(ImVec2(o.x, y + rh - 0.5f), ImVec2(o.x + W, y + rh - 0.5f), C(TK_ROW));
      if (bOwn)
        d->AddRectFilled(ImVec2(o.x, y + P(4.0f)), ImVec2(o.x + P(4.0f), y + rh - P(4.0f)), C(TK_MOD), P(2.0f));
      float x = o.x + P(14.0f);
      const float lpx = bBig ? 16.0f : kStudioFsS;
      T(d, STUDIO_FONT_TEXT, lpx, x, Mid(y, rh, lpx), bChanged ? C(TK_CHANGED) : (bOwn || bBig) ? C(TK_INK) : C(TK_INK2),
        f.label, P(colLabel));
      x += P(colLabel + 10.0f);
      const float eh = P(bBig ? 36.0f : 26.0f);
      ImGui::SetCursorScreenPos(ImVec2(x, y + (rh - eh) / 2.0f));
      StudioFieldEditor(t, name, f, v, bMine, bBig);
      // an editor of chips or files may need more than the row: the row grows to it
      const float edBottom = ImGui::GetCursorScreenPos().y;
      const bool bWideEd = f.editor == "req" || f.editor.compare(0, 6, "flags:") == 0 || f.editor == "picture" ||
                           f.editor == "soundfile" || f.editor == "video" || f.editor == "grp" || f.editor == "sheetpath" || f.bPath || f.key == "comment";
      const float rowH = std::max(rh, edBottom - y + P(6.0f));
      x += P(colEd + 10.0f);
      // the marks and the revert at the right, the base's value before them, the work in the middle
      const float rx = o.x + W;
      const float revX = rx - P(28.0f);
      if (bOwn && bMine && rec.kind != SK_CLASSIC) {
        ImGui::SetCursorScreenPos(ImVec2(revX, y + (rh - P(24.0f)) / 2.0f));
        if (StudioButton("##rev", "", "undo", SB_FLAT, 24.0f, 24.0f))
          Revert(t, name, f.key);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Back to the inherited value");
      }
      if (bChanged) {
        d->AddCircleFilled(ImVec2(revX - P(12.0f), y + rh / 2.0f), P(6.5f), C(TK_BLACK));
        d->AddCircleFilled(ImVec2(revX - P(12.0f), y + rh / 2.0f), P(4.5f), C(TK_CHANGED));
      }
      const float cmpW = P(bBig ? 170.0f : 150.0f);
      const float cmpX = revX - P(34.0f) - cmpW;
      std::string cmp;
      bool bItalic = false;
      if (bOwn && !rec.base.empty() && g_nBeside != 2) {
        StudioValue bv;
        if (StudioGet(t, rec.base, f.key, &bv) && !ValueWords(f, bv).empty())
          cmp = Short(t, rec.base) + ": " + ValueWords(f, bv);
      } else if (!bOwn && v.source != SS_NONE && !v.from.empty()) {
        cmp = "from the " + Short(t, v.from);
        bItalic = true;
      }
      if (g_nBeside == 2 && t == ST_UNITS) {
        StudioValue zv;
        if (StudioGet(t, "protoss_zealot", f.key, &zv))
          cmp = "Zealot: " + ValueWords(f, zv);
      }
      if (!cmp.empty())
        T(d, STUDIO_FONT_TEXT, bItalic ? kStudioFsXs : kStudioFsXs, cmpX, Mid(y, rh, kStudioFsXs), bItalic ? C(TK_INK3) : C(TK_INK2),
          cmp, cmpW);
      const std::string work = Work(t, name, f, v);
      if (!bWideEd && x < cmpX - P(10.0f))
        T(d, STUDIO_FONT_TEXT, bBig ? 15.0f : kStudioFsS, x, Mid(y, rh, bBig ? 15.0f : kStudioFsS),
          work == f.help ? C(TK_INK3) : C(TK_INK2), work, cmpX - P(10.0f) - x);
      ImGui::PopID();
      y += rowH;
    }
  }
  if (nShown == 0)
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, y + P(10.0f), C(TK_INK3),
      g_szFilter[0] ? "No field matches the filter." : "No fields at this level: Detailed shows them all.");
  if (bBig)
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, y + P(22.0f), C(TK_INK3), "Detailed shows every field; Raw the record as the mod's file has it.");
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), y + P(56.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

void Raw(int t, const std::string &name, const StudioRecord &rec) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x, hAvail = ImGui::GetContentRegionAvail().y;
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(14.0f);
  const float W = w - P(44.0f);
  const std::string key = Table(t) + "." + name + "." + std::to_string(StudioChange());
  const PortJson &own = StudioRecordJson(t, name);
  if (g_rawFor != key) {
    g_raw = own.kind == PortJson::NONE ? std::string() : StudioJsonText(own);
    g_rawFor = key;
    g_rawWhy.clear();
  }
  const float lw = W * 0.55f;
  T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, o.y, C(TK_INK2), Mine(rec) ? "Its record, as types.json keeps it" : "A classic row has no record of its own");
  ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + P(22.0f)));
  const float ch = fmaxf(P(300.0f), hAvail - P(100.0f));
  StudioPushFont(STUDIO_FONT_MONO, 13.0f);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, C(TK_WELL));
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_HI));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_Border, C(TK_DD_RIM));
  if (!Mine(rec))
    ImGui::BeginDisabled();
  ImGui::InputTextMultiline("##raw", const_cast<char *>(g_raw.c_str()), g_raw.capacity() + 1, ImVec2(lw, ch), ImGuiInputTextFlags_CallbackResize,
                            [](ImGuiInputTextCallbackData *p) {
                              if (p->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                g_raw.resize(static_cast<size_t>(p->BufTextLen));
                                p->Buf = const_cast<char *>(g_raw.c_str());
                              }
                              return 0;
                            });
  if (!Mine(rec))
    ImGui::EndDisabled();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + P(30.0f) + ch));
  if (StudioButton("##apply", "Apply", "done", SB_ON, 28.0f, 0.0f, kStudioFsS, Mine(rec))) {
    DesignCall("studio_raw", RecordFields(t, name) + ",\"text\":" + Q(g_raw.c_str()) + ",\"sender\":\"you\"",
               [](const PortJson &a) {
                 g_rawWhy = a["ok"].kind == PortJson::BOOL && a["ok"].b ? "applied" : a["error"].Str("refused");
               });
  }
  if (!g_rawWhy.empty()) {
    ImGui::SameLine(0.0f, P(10.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, g_rawWhy == "applied" ? C(TK_GOOD) : C(TK_BAD), g_rawWhy.c_str());
  }
  // what it inherits, beside it
  const float rx = o.x + lw + P(16.0f), rw = W - lw - P(16.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, rx, o.y, C(TK_INK2), "What it inherits");
  ImGui::SetCursorScreenPos(ImVec2(rx, o.y + P(22.0f)));
  StudioDashed(d, ImVec2(rx, o.y + P(22.0f)), ImVec2(rx + rw, o.y + P(22.0f) + ch), C(TK_LINE2));
  if (ImGui::BeginChild("##inherits", ImVec2(rw, ch), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::Dummy(ImVec2(1, P(6.0f)));
    for (const StudioField &f : StudioFields(t)) {
      StudioValue v;
      if (!StudioGet(t, name, f.key, &v) || v.source == SS_OWN || v.source == SS_NONE)
        continue;
      ImGui::SetCursorPosX(P(10.0f));
      const std::string line = "\"" + f.key + "\": " + ValueWords(f, v);
      StudioLabel(STUDIO_FONT_MONO, 12.0f, C(TK_INK3), line.c_str());
    }
  }
  ImGui::EndChild();
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), o.y + ch + P(70.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

void Home(void) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(18.0f);
  o.y += P(16.0f);
  const float W = w - P(36.0f);
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_WIDE, 22.0f, o.x, o.y, C(TK_HI), "No mod open");
    T(d, STUDIO_FONT_TEXT, 15.0f, o.x, o.y + P(40.0f), C(TK_INK), "Start a mod of your own, or open one of those installed.");
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + P(76.0f)));
    if (StudioButton("##newmod", "New mod", "plus", SB_GOOD, 32.0f))
      StudioNewModOpen();
    float y = o.y + P(126.0f);
    bool bAny = false;
    for (const PortMod &mod : PortMods()) {
      if (mod.bBuiltIn || mod.data.empty())
        continue;
      if (!bAny)
        Wide(d, 10.0f, o.x, y, C(TK_LABEL), "Installed mods with data");
      if (!bAny)
        y += P(22.0f);
      bAny = true;
      ImGui::SetCursorScreenPos(ImVec2(o.x, y));
      ImGui::PushID(mod.id.c_str());
      if (StudioButton("##open", (mod.name + "  " + mod.version).c_str(), "home", SB_PLAIN, 28.0f, std::min(W, P(420.0f)) / StudioPx(1.0f)))
        StudioAsk("studio_open", "\"mod\":" + Q(mod.id));
      ImGui::PopID();
      T(d, STUDIO_FONT_MONO, kStudioFsXs, o.x + std::min(W, P(420.0f)) + P(12.0f), Mid(y, P(28.0f), kStudioFsXs), C(TK_INK3), mod.id);
      y += P(34.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    ImGui::Dummy(ImVec2(1, 1));
    return;
  }
  const PortJson &mod = StudioDoc("mod");
  // the mod's card
  const float ch = P(150.0f);
  d->AddRectFilled(o, ImVec2(o.x + W, o.y + ch), C(TK_PANE2), P(3.0f));
  d->AddRect(o, ImVec2(o.x + W, o.y + ch), C(TK_LINE), P(3.0f));
  float x = o.x + P(16.0f);
  const float tw = Wide(d, 22.0f, x, o.y + P(14.0f), C(TK_HI), mod["name"].Str(StudioModId().c_str()), 0.06f, false);
  StudioChip(d, ImVec2(x + tw + P(12.0f), o.y + P(18.0f)), ("Version " + mod["version"].Str("?")).c_str());
  T(d, STUDIO_FONT_TEXT, 15.0f, x, o.y + P(52.0f), C(TK_INK), mod["line"].Str("A mod with no line of its own yet."), W - P(32.0f));
  Wide(d, 10.5f, x, o.y + P(86.0f), C(TK_HEAD), "What it holds");
  float hx = x;
  for (const Kind &k : kKinds) {
    const size_t n = StudioRecords(k.t).size();
    if (!n)
      continue;
    const std::string label = std::string(k.pszLabel) + " " + std::to_string(n);
    const float bw = TW(STUDIO_FONT_TEXT, kStudioFsS, label) + P(40.0f);
    d->AddRectFilled(ImVec2(hx, o.y + P(108.0f)), ImVec2(hx + bw, o.y + P(136.0f)), C(TK_WELL), P(2.0f));
    d->AddRect(ImVec2(hx, o.y + P(108.0f)), ImVec2(hx + bw, o.y + P(136.0f)), C(TK_LINE2), P(2.0f));
    StudioGlyph(d, k.pszGlyph, ImVec2(hx + P(10.0f), o.y + P(115.0f)), P(14.0f), C(TK_INK2));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, hx + P(30.0f), Mid(o.y + P(108.0f), P(28.0f), kStudioFsS), C(TK_INK2), label);
    hx += bw + P(6.0f);
  }
  // making something new
  float y = o.y + ch + P(20.0f);
  Wide(d, 10.5f, o.x, y, C(TK_HEAD), "Make something");
  y += P(24.0f);
  struct Make {
    const char *pszTitle, *pszFine, *pszGlyph;
    int t;
  };
  const Make makes[] = {{"A unit", "from a classic unit, its numbers and art to change", "unit", ST_UNITS},
                        {"A weapon", "damage, range and how often it fires", "weapon", ST_WEAPONS},
                        {"An upgrade", "levels, their cost and their time", "upgrade", ST_UPGRADES},
                        {"Research", "a tech and what it costs", "tech", ST_TECHS}};
  const float mw = (W - P(24.0f)) / 4.0f;
  for (int i = 0; i < 4; ++i) {
    const ImVec2 a(o.x + i * (mw + P(8.0f)), y), b(a.x + mw, y + P(92.0f));
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(i);
    const bool bClick = ImGui::InvisibleButton("##make", ImVec2(mw, P(92.0f)));
    const bool bHover = ImGui::IsItemHovered();
    ImGui::PopID();
    d->AddRectFilledMultiColor(a, b, C(TK_BTN_TOP), C(TK_BTN_TOP), C(TK_BTN_BOT), C(TK_BTN_BOT));
    d->AddRect(a, b, bHover ? C(TK_BTN_RIM_HI) : C(TK_BTN_RIM), P(3.0f));
    StudioGlyph(d, makes[i].pszGlyph, ImVec2(a.x + P(12.0f), a.y + P(12.0f)), P(22.0f), C(TK_LABEL));
    T(d, STUDIO_FONT_BOLD, 15.0f, a.x + P(12.0f), a.y + P(40.0f), C(TK_INK), makes[i].pszTitle);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(12.0f), a.y + P(62.0f), C(TK_INK2), makes[i].pszFine, mw - P(24.0f));
    if (bClick)
      NewRecordOpen(makes[i].t);
  }
  y += P(112.0f);
  // the last edits
  Wide(d, 10.5f, o.x, y, C(TK_HEAD), "Last edits");
  y += P(22.0f);
  const std::vector<StudioEdit> &h = StudioHistory();
  int nGroup = -1, nRows = 0;
  for (size_t i = h.size(); i-- > 0 && nRows < 8;) {
    const StudioEdit &e = h[i];
    if (e.nGroup == nGroup)
      continue;
    nGroup = e.nGroup;
    ++nRows;
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, o.x + P(4.0f), Mid(y, P(30.0f), kStudioFsXs), C(TK_INK3), Clock(e.nTime));
    StudioChip(d, ImVec2(o.x + P(52.0f), y + P(6.0f)), e.sender.c_str(), e.sender == "claude" ? CH_CLAUDE : CH_PLAIN);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + P(122.0f), Mid(y, P(30.0f), kStudioFsS), C(TK_INK), e.label, W - P(130.0f));
    d->AddLine(ImVec2(o.x, y + P(30.0f)), ImVec2(o.x + W, y + P(30.0f)), C(TK_ROW));
    y += P(30.0f);
  }
  if (nRows == 0)
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, y, C(TK_INK3), "No edits yet.");
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(30.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

void Documents(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioPane(d, a, b);
  // a workspace in the documents' place, each its area's (studio_ui.h)
  switch (g_nWs) {
  case 1: StudioTables(a, b); return;
  case 2: StudioArtWorkspace(a, b); return;
  case 3: StudioLooksWorkspace(a, b); return;
  case 4: StudioEffectsWorkspace(a, b); return;
  case 5: StudioRacesWorkspace(a, b); return;
  case 6: StudioTestWorkspace(a, b); return;
  case 7: StudioChecksWorkspace(a, b); return;
  case 8: StudioModWorkspace(a, b); return;
  case 9: StudioStringsWorkspace(a, b); return;
  case 10: StudioBalanceWorkspace(a, b); return;
  case 11: StudioStatusesWorkspace(a, b); return;
  case 12: StudioTilesetsWorkspace(a, b); return;
  case 13: StudioScriptsWorkspace(a, b); return;
  case 14: StudioModesWorkspace(a, b); return;
  default: break;
  }
  // the tabs: home, then each record open
  const float th = P(32.0f);
  d->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + th), C(TK_BAR));
  d->AddLine(ImVec2(a.x, a.y + th - 0.5f), ImVec2(b.x, a.y + th - 0.5f), C(TK_LINE));
  float x = a.x + 1;
  auto tab = [&](int t, const std::string &name, size_t i) {
    const bool bHome = t < 0;
    const std::string label = bHome ? "Home" : StudioShowName(t, name);
    const bool bOn = g_current.t == t && g_current.name == name;
    const bool bChanged = !bHome && ChangedSinceSave(t, name) > 0;
    const float w = fminf(P(250.0f), P(12.0f + 14.0f + 7.0f) + TW(STUDIO_FONT_TEXT, kStudioFsS, label) +
                                         (bChanged ? P(14.0f) : 0.0f) + (bHome ? P(12.0f) : P(32.0f)));
    const ImVec2 p(x, a.y + 1);
    ImGui::SetCursorScreenPos(p);
    ImGui::PushID(bHome ? "home" : (Table(t) + name).c_str());
    const bool bClick = ImGui::InvisibleButton("##tab", ImVec2(w, th - 1));
    const bool bHover = ImGui::IsItemHovered();
    if (bClick)
      g_current = bHome ? StudioOpenRecord() : g_docs[i];
    if (bOn)
      d->AddRectFilled(p, ImVec2(p.x + w, p.y + th - 1), C(TK_TAB_ON));
    if (bOn)
      d->AddLine(ImVec2(p.x, p.y + th - P(2.0f)), ImVec2(p.x + w, p.y + th - P(2.0f)), C(TK_SEL_LINE), P(2.0f));
    d->AddLine(ImVec2(p.x + w - 0.5f, p.y), ImVec2(p.x + w - 0.5f, p.y + th - 1), C(TK_LINE));
    const ImU32 ink = bOn ? C(TK_HI) : bHover ? C(TK_INK) : C(TK_INK3);
    StudioGlyph(d, bHome ? "home" : KindOf(t).pszGlyph, ImVec2(p.x + P(12.0f), Mid(p.y, th, 14.0f)), P(14.0f), ink);
    float tx = p.x + P(33.0f);
    tx += T(d, STUDIO_FONT_TEXT, kStudioFsS, tx, Mid(p.y, th, kStudioFsS), ink, label, w - P(33.0f) - (bHome ? 0.0f : P(32.0f)) - (bChanged ? P(14.0f) : 0.0f) + P(1.0f));
    if (bChanged) {
      d->AddCircleFilled(ImVec2(tx + P(9.0f), p.y + th / 2.0f), P(3.5f), C(TK_CHANGED));
    }
    bool bClose = false;
    if (!bHome && (bOn || bHover)) {
      ImGui::SetCursorScreenPos(ImVec2(p.x + w - P(26.0f), p.y + (th - P(20.0f)) / 2.0f));
      bClose = StudioButton("##x", "", "x", SB_FLAT, 20.0f, 20.0f);
    }
    ImGui::PopID();
    x += w;
    return bClose;
  };
  tab(-1, std::string(), 0);
  for (size_t i = 0; i < g_docs.size(); ++i) {
    if (tab(g_docs[i].t, g_docs[i].name, i)) {
      CloseDoc(i);
      break;
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + 1));
  if (StudioButton("##plus", "", "plus", SB_FLAT, 30.0f, 32.0f) && StudioIsOpen())
    NewRecordOpen(ST_UNITS);
  float y = a.y + th;
  if (g_current.t < 0) {
    if (Region("##home", ImVec2(a.x + 1, y), ImVec2(b.x - 1, b.y - 1)))
      Home();
    ImGui::EndChild();
    return;
  }
  const int t = g_current.t;
  const std::string name = g_current.name;
  StudioRecord rec;
  if (!StudioRecordOf(t, name, &rec)) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, a.x + P(20.0f), y + P(20.0f), C(TK_INK3), name + " is no longer in the mod.");
    return;
  }
  // the record's head
  const float hh = P(46.0f);
  d->AddRectFilled(ImVec2(a.x + 1, y), ImVec2(b.x - 1, y + hh), C(TK_PANE2));
  d->AddLine(ImVec2(a.x, y + hh - 0.5f), ImVec2(b.x, y + hh - 0.5f), C(TK_LINE));
  x = a.x + P(12.0f);
  StudioGlyph(d, KindOf(t).pszGlyph, ImVec2(x, Mid(y, hh, 20.0f)), P(20.0f), C(TK_LABEL));
  x += P(30.0f);
  // the right side first, so the left knows its room
  float r = b.x - P(12.0f);
  const float tryW = StudioButtonSize("Try it", nullptr, 28.0f).x;
  const bool bUnit = t == ST_UNITS;
  if (bUnit) {
    r -= tryW;
    ImGui::SetCursorScreenPos(ImVec2(r, y + (hh - P(28.0f)) / 2.0f));
    if (StudioButton("##tryit", "Try it", nullptr, SB_GOOD, 28.0f, 0.0f, kStudioFsS, DesignInGame()))
      TryIt(name);
    r -= P(10.0f);
  }
  const float fw = (b.x - a.x) > P(1100.0f) ? 150.0f : 112.0f;
  r -= P(fw + 70.0f);
  ImGui::SetCursorScreenPos(ImVec2(r, y + (hh - P(28.0f)) / 2.0f));
  if (g_bFocusFilter) {
    ImGui::SetKeyboardFocusHere();
    g_bFocusFilter = false;
  }
  StudioInput("##filter", g_szFilter, sizeof g_szFilter, fw + 70.0f, 28.0f, "Filter fields", "filter", "Ctrl F");
  r -= P(10.0f);
  static const char *const kLevels[] = {"Simple", "Detailed", "Raw"};
  r -= StudioSegWidth(kLevels, 3);
  ImGui::SetCursorScreenPos(ImVec2(r, y + (hh - P(26.0f)) / 2.0f));
  g_nLevel = StudioSeg("##level", kLevels, 3, g_nLevel);
  r -= P(14.0f);
  // the left: name, chips, based on
  x += Wide(d, 15.0f, x, Mid(y, hh, 15.0f), C(TK_HEAD), StudioShowName(t, name), 0.04f, false) + P(10.0f);
  const float cy = y + (hh - P(18.0f)) / 2.0f;
  const char *pszKind = rec.kind == SK_OWN ? "own" : rec.kind == SK_EDIT ? "edit" : rec.kind == SK_DEPENDENCY ? "read only" : "classic";
  if (x + StudioChipW(pszKind) < r) {
    x += StudioChip(d, ImVec2(x, cy), pszKind, rec.kind == SK_CLASSIC ? CH_GOLD : CH_PLAIN) + P(10.0f);
  }
  if (!rec.base.empty()) {
    const std::string pre = "based on ", base = Short(t, rec.base);
    if (x + TW(STUDIO_FONT_TEXT, kStudioFsS, pre + base) < r) {
      x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(y, hh, kStudioFsS), C(TK_INK2), pre);
      ImGui::SetCursorScreenPos(ImVec2(x, y + (hh - P(22.0f)) / 2.0f));
      const float bw = TW(STUDIO_FONT_BOLD, kStudioFsS, base);
      if (ImGui::InvisibleButton("##based", ImVec2(bw, P(22.0f))))
        OpenDoc(t, rec.base);
      x += T(d, STUDIO_FONT_BOLD, kStudioFsS, x, Mid(y, hh, kStudioFsS), ImGui::IsItemHovered() ? C(TK_HI) : C(TK_INK), base) + P(12.0f);
    }
  }
  const int nChanged = ChangedSinceSave(t, name);
  if (nChanged > 0) {
    const std::string c = std::to_string(nChanged) + (nChanged == 1 ? " change since the save" : " changes since the save");
    if (x + StudioChipW(c.c_str()) < r)
      StudioChip(d, ImVec2(x, cy), c.c_str(), CH_GOLD);
  }
  y += hh;
  // the view tabs
  const char *apszHead[] = {HeadView(t)};
  const char *const *ppViews = bUnit ? kUnitViews : apszHead;
  const int nViews = bUnit ? 7 : 1;
  if (g_nView >= nViews)
    g_nView = 0;
  const float vh = P(34.0f);
  d->AddLine(ImVec2(a.x, y + vh - 0.5f), ImVec2(b.x, y + vh - 0.5f), C(TK_LINE));
  x = a.x + P(8.0f);
  for (int i = 0; i < nViews; ++i) {
    const float w = TW(STUDIO_FONT_WIDE, 10.5f, ppViews[i], 0.1f, true) + P(24.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##view", ImVec2(w, vh)))
      g_nView = i;
    const bool bHover = ImGui::IsItemHovered();
    ImGui::PopID();
    const bool bOn = g_nView == i;
    if (bOn)
      d->AddLine(ImVec2(x, y + vh - P(1.0f)), ImVec2(x + w, y + vh - P(1.0f)), C(TK_SEL_LINE), P(2.0f));
    Wide(d, 10.5f, x + P(12.0f), Mid(y, vh, 10.5f), bOn ? C(TK_HI) : bHover ? C(TK_INK) : C(TK_INK3), ppViews[i]);
    x += w + P(2.0f);
  }
  y += vh;
  if (Region("##view", ImVec2(a.x + 1, y), ImVec2(b.x - 1, b.y - 1))) {
    const char *pszView = ppViews[g_nView];
    if (g_nLevel == 2) {
      Raw(t, name, rec);
    } else if (strcmp(pszView, "Overview") == 0) {
      Overview(t, name, rec);
    } else if (strcmp(pszView, "Numbers") == 0) {
      Numbers(t, name, rec);
    } else if (bUnit) {
      // a unit's other views, each its area's (studio_ui.h)
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + P(22.0f), ImGui::GetCursorScreenPos().y + P(16.0f)));
      ImGui::BeginGroup();
      if (strcmp(pszView, "Actions") == 0)
        StudioUnitActions(name);
      else if (strcmp(pszView, "Art") == 0)
        StudioUnitArt(name);
      else if (strcmp(pszView, "Look") == 0)
        StudioUnitLook(name);
      else if (strcmp(pszView, "Sounds") == 0)
        StudioUnitSounds(name);
      else
        StudioUnitCard(name);
      ImGui::EndGroup();
      ImGui::Dummy(ImVec2(1, P(24.0f)));
    } else {
      // another record: its head, then its numbers
      if (t == ST_WEAPONS)
        StudioWeaponHead(name);
      else if (t == ST_IMAGES || t == ST_SPRITES || t == ST_FLINGY)
        StudioArtHead(t, name);
      else if (t == ST_SOUNDS || t == ST_PORTRAITS)
        StudioSoundHead(t, name);
      else if (t == ST_ORDERS)
        StudioOrderHead(name);
      else if (t == ST_TECHS || t == ST_UPGRADES)
        StudioTechHead(t, name);
      Numbers(t, name, rec);
    }
  }
  ImGui::EndChild();
}

// The game large: a pane in the documents' place, its head the way back.
void LargeGame(ImVec2 a, ImVec2 b, const ImVec4 *pRoom) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioPane(d, a, b, 30.0f, pRoom);
  float x = a.x + P(8.0f);
  x += Wide(d, 10.5f, x, Mid(a.y, P(30.0f), 10.5f), C(TK_HEAD), "Game") + P(12.0f);
  if (!g_tried.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, P(30.0f), kStudioFsS), C(TK_INK2), g_tried + (g_bPaused ? " \xC2\xB7 paused" : ""));
  const float bw = StudioButtonSize("Back to the documents", "back", 22.0f, kStudioFsXs).x;
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(8.0f) - bw, a.y + P(4.0f)));
  if (StudioButton("##back", "Back to the documents", "back", SB_PLAIN, 22.0f, 0.0f, kStudioFsXs))
    g_nGameAt = GAME_DOCK;
  if (!DesignInGame()) {
    d->AddRectFilled(ImVec2(a.x + 1, a.y + P(30.0f)), ImVec2(b.x - 1, b.y - 1), C(TK_WELL));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(12.0f), a.y + P(42.0f), C(TK_INK3), "The canvas game is starting");
  }
}

// ---- the preview and the inspector -------------------------------------------------------------------------------

void Preview(ImVec2 a, ImVec2 b, const ImVec4 *pRoom) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioPane(d, a, b, 30.0f, pRoom);
  float lx = a.x + P(8.0f);
  lx += Wide(d, 10.5f, lx, Mid(a.y, P(30.0f), 10.5f), C(TK_HEAD), "Preview") + P(10.0f);
  // a picture of the unit, or the game itself in this pane
  static const char *const kShow[] = {"Picture", "Game"};
  ImGui::SetCursorScreenPos(ImVec2(lx, a.y + P(4.0f)));
  const int nShow = StudioSeg("##show", kShow, 2, g_nGameAt == GAME_PREVIEW ? 1 : 0, 22.0f, kStudioFsXs);
  if (nShow == 1 && g_nGameAt != GAME_PREVIEW)
    g_nGameAt = GAME_PREVIEW;
  else if (nShow == 0 && g_nGameAt == GAME_PREVIEW)
    g_nGameAt = GAME_DOCK;
  lx += StudioSegWidth(kShow, 2, kStudioFsXs) + P(8.0f);
  if (g_nGameAt == GAME_PREVIEW) {
    if (!DesignInGame()) {
      const ImVec2 sa(a.x + 1, a.y + P(30.0f)), sb(b.x - 1, b.y - 1);
      d->AddRectFilled(sa, sb, C(TK_WELL));
      T(d, STUDIO_FONT_TEXT, kStudioFsS, sa.x + P(10.0f), sa.y + P(10.0f), C(TK_INK3), "The canvas game is starting");
    } else if (!g_tried.empty()) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(9.0f), b.y - P(19.0f), IM_COL32(0, 0, 0, 230), g_tried);
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(8.0f), b.y - P(20.0f), C(TK_INK), g_tried);
    }
    return;
  }
  float r = b.x - P(8.0f);
  static const char *const kColours[] = {"Red", "Blue", "Teal", "Purple", "Orange", "Brown", "White", "Yellow"};
  static const char *const kTilesets[] = {"Jungle", "Badlands", "Space", "Ash", "Desert", "Ice", "Twilight", "Installation"};
  const float sw = fmaxf(56.0f, fminf(112.0f, ((r - lx) / P(1.0f) - 90.0f) / 2.0f));
  r -= P(sw);
  ImGui::SetCursorScreenPos(ImVec2(r, a.y + P(3.0f)));
  if (StudioSelectBegin("##tileset", kTilesets[g_nTileset], sw, 24.0f, kStudioFsXs)) {
    for (int i = 0; i < 8; ++i)
      if (StudioItem(kTilesets[i], g_nTileset == i))
        g_nTileset = i;
    StudioSelectEnd();
  }
  r -= P(sw + 6.0f);
  ImGui::SetCursorScreenPos(ImVec2(r, a.y + P(3.0f)));
  if (StudioSelectBegin("##colour", kColours[g_nColour], sw, 24.0f, kStudioFsXs)) {
    for (int i = 0; i < 8; ++i)
      if (StudioItem(kColours[i], g_nColour == i))
        g_nColour = i;
    StudioSelectEnd();
  }
  static const char *const kTurn[] = {"Turn", "Still"};
  r -= StudioSegWidth(kTurn, 2, kStudioFsXs) + P(6.0f);
  ImGui::SetCursorScreenPos(ImVec2(r, a.y + P(4.0f)));
  g_nTurn = StudioSeg("##turn", kTurn, 2, g_nTurn, 22.0f, kStudioFsXs);
  // the stage
  const ImVec2 sa(a.x + 1, a.y + P(30.0f)), sb(b.x - 1, b.y - 1);
  d->AddRectFilled(sa, sb, C(TK_WELL));
  const ImVec2 c((sa.x + sb.x) / 2.0f, (sa.y + sb.y) / 2.0f - P(8.0f));
  if (g_current.t == ST_UNITS) {
    const Fight f = FightOf(g_current.name);
    const float ring = fminf((sb.y - sa.y) * 0.4f, P(40.0f) + f.range * P(0.8f));
    for (int i = 0; i < 48; i += 2) {
      const float a0 = 2.0f * 3.14159265f * i / 48.0f, a1 = 2.0f * 3.14159265f * (i + 1) / 48.0f;
      d->AddLine(ImVec2(c.x + ring * cosf(a0), c.y + ring * sinf(a0) * 0.62f),
                 ImVec2(c.x + ring * cosf(a1), c.y + ring * sinf(a1) * 0.62f), C(TK_SEL_LINE2, 0.7f), P(1.2f));
    }
    const bool bDrawn = StudioUnitPicture(d, ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y - P(24.0f)), g_current.name, Facing(), g_nColour);
    if (!bDrawn)
      StudioGlyph(d, "unit", ImVec2(c.x - P(28.0f), c.y - P(30.0f)), P(56.0f), C(TK_INK3));
    const std::string read = StudioShowName(ST_UNITS, g_current.name) + " \xC2\xB7 " + kColours[g_nColour] +
                             " \xC2\xB7 range " + Fmt(f.range / 32.0) + " tiles" + (bDrawn ? "" : " \xC2\xB7 no frames to draw");
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, sa.x + P(8.0f), sb.y - P(20.0f), C(TK_INK), read, sb.x - sa.x - P(16.0f));
  } else {
    const char *psz = "Open a unit to see it here";
    T(d, STUDIO_FONT_TEXT, kStudioFsS, c.x - TW(STUDIO_FONT_TEXT, kStudioFsS, psz) / 2.0f, c.y, C(TK_INK3), psz);
  }
}

void Inspector(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioPane(d, a, b, 30.0f);
  Wide(d, 10.5f, a.x + P(8.0f), Mid(a.y, P(30.0f), 10.5f), C(TK_HEAD), "Inspector");
  const float rw = StudioButtonSize("The record", nullptr, 22.0f, kStudioFsXs).x;
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(8.0f) - rw, a.y + P(4.0f)));
  if (StudioButton("##record", "The record", nullptr, SB_PLAIN, 22.0f, 0.0f, kStudioFsXs, g_current.t >= 0))
    g_nLevel = 2;
  // the marks' legend at the foot
  const float lh = P(52.0f);
  const float ly = b.y - lh;
  d->AddLine(ImVec2(a.x + P(8.0f), ly), ImVec2(b.x - P(8.0f), ly), C(TK_LINE));
  float x = a.x + P(10.0f);
  x += Wide(d, 9.5f, x, ly + P(10.0f), C(TK_INK3), "Marks") + P(14.0f);
  d->AddRectFilled(ImVec2(x, ly + P(9.0f)), ImVec2(x + P(3.0f), ly + P(23.0f)), C(TK_MOD), P(1.0f));
  x += P(8.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, ly + P(10.0f), C(TK_INK3), "set in the mod") + P(14.0f);
  StudioDashed(d, ImVec2(x, ly + P(8.0f)), ImVec2(x + P(64.0f), ly + P(24.0f)), C(TK_LINE2));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x + P(5.0f), ly + P(10.0f), C(TK_INK3), "from Zealot");
  x += P(72.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, ly + P(10.0f), C(TK_INK3), "inherited");
  d->AddCircleFilled(ImVec2(a.x + P(14.0f), ly + P(37.0f)), P(3.5f), C(TK_CHANGED));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(24.0f), ly + P(30.0f), C(TK_INK3), "changed since the save");
  if (!Region("##inspector", ImVec2(a.x + 1, a.y + P(30.0f)), ImVec2(b.x - 1, ly))) {
    ImGui::EndChild();
    return;
  }
  d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  const ImVec2 o(ImGui::GetCursorScreenPos().x + P(10.0f), ImGui::GetCursorScreenPos().y + P(10.0f));
  const float W = w - P(20.0f);
  if (g_current.t < 0) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, o.y, C(TK_INK3), "Open a record to see what it is made of.");
    ImGui::EndChild();
    return;
  }
  const int t = g_current.t;
  const std::string name = g_current.name;
  StudioRecord rec;
  if (!StudioRecordOf(t, name, &rec)) {
    ImGui::EndChild();
    return;
  }
  Wide(d, 13.0f, o.x, o.y, C(TK_HEAD), StudioShowName(t, name), 0.05f, false);
  ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + P(26.0f)));
  if (t == ST_UNITS) {
    ImGui::PushTextWrapPos(o.x + W - ImGui::GetWindowPos().x);
    StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
    ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK2));
    ImGui::TextWrapped("%s", Sentence(name).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
  }
  float y = ImGui::GetCursorScreenPos().y + P(10.0f);
  auto kv = [&](const char *pszK, const std::string &v) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, y, C(TK_INK3), pszK);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + P(92.0f), y, C(TK_INK), v, W - P(92.0f));
    y += P(19.0f);
  };
  if (t == ST_UNITS)
    kv("Race", RaceOf(name));
  kv("What it is", rec.kind == SK_OWN ? "the mod's own" : rec.kind == SK_EDIT ? "an edit of a classic row"
                   : rec.kind == SK_DEPENDENCY ? "from " + rec.mod + ", read only"
                                               : "classic, read only");
  if (!rec.base.empty())
    kv("Based on", "the " + Short(t, rec.base));
  kv("In the tables", Table(t) + ", row " + std::to_string(rec.nId));
  if (t == ST_UNITS) {
    const Fight f = FightOf(name);
    kv("Weapon", f.weapon.empty() ? "none" : StudioShowName(ST_WEAPONS, f.weapon));
  }
  const int nChanged = ChangedSinceSave(t, name);
  if (nChanged)
    kv("Since the save", std::to_string(nChanged) + (nChanged == 1 ? " edit" : " edits"));
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(10.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  ImGui::EndChild();
}

// ---- the dock --------------------------------------------------------------------------------------------------------

void GameTab(ImVec2 a, ImVec2 b, float fSide) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float sideX = a.x + P(fSide);
  d->AddLine(ImVec2(sideX - 0.5f, a.y), ImVec2(sideX - 0.5f, b.y), C(TK_LINE));
  // the side: what is tried, against what, and the pushes
  if (Region("##gameside", ImVec2(a.x, a.y), ImVec2(sideX - 1, b.y))) {
    ImDrawList *d = ImGui::GetWindowDrawList();  // the side's own
    const float w = ImGui::GetContentRegionAvail().x - P(20.0f);
    ImGui::SetCursorPos(ImVec2(P(10.0f), P(8.0f)));
    Lab("Trying");
    ImGui::SetCursorPosX(P(10.0f));
    std::vector<StudioRecord> units = StudioRecords(ST_UNITS);
    if (g_trying.empty() && !units.empty())
      g_trying = g_current.t == ST_UNITS ? g_current.name : units[0].name;
    if (StudioSelectBegin("##trying", g_trying.empty() ? "a unit of the mod" : StudioShowName(ST_UNITS, g_trying).c_str(), w / P(1.0f))) {
      for (const StudioRecord &r : units)
        if (StudioItem(StudioShowName(ST_UNITS, r.name).c_str(), r.name == g_trying))
          g_trying = r.name;
      StudioSelectEnd();
    }
    ImGui::SetCursorPosX(P(10.0f));
    Lab("Against");
    for (int i = 0; i < 3; ++i) {
      ImGui::SetCursorPosX(P(10.0f));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(i);
      if (StudioButton("##vs", "", nullptr, g_nScenario == i ? SB_ON : SB_PLAIN, 28.0f, w / P(1.0f))) {
        g_nScenario = i;
        if (!g_trying.empty())
          TryIt(g_trying);
      }
      ImGui::PopID();
      T(d, STUDIO_FONT_TEXT, 13.0f, p.x + P(10.0f), Mid(p.y, P(28.0f), 13.0f), g_nScenario == i ? C(TK_HI) : C(TK_INK), kScenarios[i].pszLabel);
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + w - P(10.0f) - TW(STUDIO_FONT_TEXT, kStudioFsXs, kScenarios[i].pszUnit),
        Mid(p.y, P(28.0f), kStudioFsXs), C(TK_INK3), kScenarios[i].pszUnit);
    }
    ImGui::SetCursorPosX(P(10.0f));
    if (StudioButton("##again", "Again", "again", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, !g_trying.empty() && DesignInGame()))
      Again();
    ImGui::SameLine(0.0f, P(6.0f));
    if (StudioButton("##pause", g_bPaused ? "Go on" : "Pause", g_bPaused ? "play" : "pause", g_bPaused ? SB_ON : SB_PLAIN, 24.0f,
                     0.0f, kStudioFsXs, DesignInGame())) {
      DesignCall(g_bPaused ? "resume" : "pause", "", nullptr);
      g_bPaused = !g_bPaused;
    }
    ImGui::SetCursorPosX(P(10.0f));
    if (StudioButton("##push", "Push  F5", "push", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, StudioIsOpen() && DesignInGame()))
      Push();
    ImGui::SameLine(0.0f, P(8.0f));
    StudioCheck("##auto", "Auto", &g_bAuto);
    ImGui::SetCursorPosX(P(10.0f));
    Lab("Pushes");
    ImGui::SetCursorPosX(P(10.0f));
    const ImVec2 la = ImGui::GetCursorScreenPos();
    const float lh = fmaxf(P(60.0f), ImGui::GetContentRegionAvail().y - P(8.0f));
    d->AddRectFilled(la, ImVec2(la.x + w, la.y + lh), C(TK_WELL), P(2.0f));
    d->AddRect(la, ImVec2(la.x + w, la.y + lh), C(TK_LINE), P(2.0f));
    float py = la.y + P(2.0f);
    for (int n = StudioPushCount(); n >= 1 && py < la.y + lh - P(20.0f); --n) {
      std::string state, answer;
      unsigned nChange = 0;
      StudioPushInfo(n, &state, &answer, &nChange);
      PortJson j;
      std::string err;
      PortJsonParse(answer.c_str(), &j, &err);
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, la.x + P(6.0f), py + P(4.0f), C(TK_INK3), std::to_string(n));
      const int kind = state == "pushed" ? CH_GOOD : state == "refused" ? CH_BAD : CH_PLAIN;
      const float cw = StudioChip(d, ImVec2(la.x + P(26.0f), py + P(3.0f)), state.c_str(), kind, 15.0f, 8.5f);
      std::string what = state == "refused" ? j["error"].Str("refused")
                                            : Fmt(j["records"]["units"].Num(static_cast<double>(j["units"].items.size()))) + " units, " +
                                                  Fmt(j["records"]["weapons"].Num(0)) + " weapons";
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, la.x + P(32.0f) + cw, py + P(4.0f), state == "refused" ? C(TK_BAD) : C(TK_INK), what,
        w - cw - P(40.0f));
      py += P(22.0f);
      d->AddLine(ImVec2(la.x, py), ImVec2(la.x + w, py), C(TK_ROW));
    }
    if (StudioPushCount() == 0)
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, la.x + P(8.0f), la.y + P(6.0f), C(TK_INK3), "None yet: Push or Try it sends the mod in.");
    ImGui::Dummy(ImVec2(w, lh));
  }
  ImGui::EndChild();
  // the game's foot: what it holds against the model
  const float fh = P(28.0f);
  d->AddLine(ImVec2(sideX, b.y - fh + 0.5f), ImVec2(b.x, b.y - fh + 0.5f), C(TK_LINE));
  std::string words;
  const int kind = PushState(&words);
  float fx = sideX + P(10.0f);
  fx += StudioChip(d, ImVec2(fx, b.y - fh + (fh - P(18.0f)) / 2.0f), words.c_str(), kind) + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, fx, Mid(b.y - fh, fh, kStudioFsXs), C(TK_INK3),
    "The game is the real engine: pushed data plays by the classic rules, in a canvas of its own.", b.x - fx - P(10.0f));
  // the game itself shows through here; a line over it says what it holds
  const ImVec2 ga(sideX, a.y), gb(b.x - 1, b.y - fh);
  if (g_nGameAt != GAME_DOCK) {
    d->AddRectFilled(ga, gb, C(TK_WELL));
    const char *psz = g_nGameAt == GAME_PREVIEW ? "The game shows in the preview" : "The game shows large, in the documents' place";
    const float tw = TW(STUDIO_FONT_TEXT, kStudioFsS, psz);
    const ImVec2 c((ga.x + gb.x) / 2.0f, (ga.y + gb.y) / 2.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, c.x - tw / 2.0f, c.y - P(24.0f), C(TK_INK2), psz);
    const float bw = StudioButtonSize("Show it here", "game", 28.0f).x;
    ImGui::SetCursorScreenPos(ImVec2(c.x - bw / 2.0f, c.y));
    if (StudioButton("##here", "Show it here", "game", SB_PLAIN, 28.0f))
      g_nGameAt = GAME_DOCK;
  } else if (!DesignInGame()) {
    d->AddRectFilled(ga, gb, C(TK_WELL));
    const char *psz = "The canvas game is starting";
    T(d, STUDIO_FONT_TEXT, kStudioFsS, (ga.x + gb.x) / 2.0f - TW(STUDIO_FONT_TEXT, kStudioFsS, psz) / 2.0f, (ga.y + gb.y) / 2.0f,
      C(TK_INK3), psz);
  } else if (!g_tried.empty()) {
    const std::string hud = g_tried + (g_bPaused ? " \xC2\xB7 paused" : "");
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ga.x + P(9.0f), ga.y + P(7.0f), IM_COL32(0, 0, 0, 230), hud);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ga.x + P(8.0f), ga.y + P(6.0f), C(TK_INK), hud);
  }
}

void ProblemsTab(ImVec2 a, ImVec2 b) {
  if (!Region("##problems", a, b)) {
    ImGui::EndChild();
    return;
  }
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  const std::vector<Problem> probs = Problems();
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.x += P(8.0f);
  p.y += P(6.0f);
  if (probs.empty() && StudioCheckProblems(nullptr, nullptr) == 0) {
    StudioGlyph(d, "done", ImVec2(p.x + P(4.0f), p.y + P(6.0f)), P(16.0f), C(TK_GOOD));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(28.0f), p.y + P(7.0f), C(TK_INK2), "Nothing to fix.");
  }
  for (size_t i = 0; i < probs.size(); ++i) {
    const Problem &pr = probs[i];
    const float rh = P(30.0f);
    ImGui::PushID(static_cast<int>(i));
    const bool bHit = RowHit("##p", p, w - P(16.0f), rh, false);
    if (bHit && pr.t >= 0)
      OpenDoc(pr.t, pr.name);
    else if (bHit && pr.t == -2)
      StudioLanguagesShow(pr.name);
    ImGui::PopID();
    const int tok = ProblemTok(pr.pszKind);
    StudioGlyph(d, ProblemGlyph(pr.pszKind), ImVec2(p.x + P(6.0f), Mid(p.y, rh, 16.0f)), P(16.0f), C(tok));
    Wide(d, 9.5f, p.x + P(32.0f), Mid(p.y, rh, 9.5f), C(tok), pr.pszKind, 0.08f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(110.0f), Mid(p.y, rh, kStudioFsS), C(TK_INK), pr.text, w - P(130.0f));
    d->AddLine(ImVec2(p.x, p.y + rh - 0.5f), ImVec2(p.x + w - P(16.0f), p.y + rh - 0.5f), C(TK_ROW));
    p.y += rh;
  }
  ImGui::SetCursorScreenPos(p);
  StudioChecksList(w - P(16.0f));
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  ImGui::EndChild();
}

void HistoryTab(ImVec2 a, ImVec2 b) {
  if (!Region("##history", a, b)) {
    ImGui::EndChild();
    return;
  }
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  const std::vector<StudioEdit> &h = StudioHistory();
  ImGui::SetCursorPos(ImVec2(P(8.0f), P(6.0f)));
  if (StudioButton("##undo", "Undo", "undo", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, StudioHistoryAt() > 0))
    StudioAsk("studio_undo", "");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##redo", "Redo", "redo", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, StudioHistoryAt() < static_cast<int>(h.size())))
    StudioAsk("studio_redo", "");
  ImGui::SameLine(0.0f, P(10.0f));
  const int nUndone = static_cast<int>(h.size()) - StudioHistoryAt();
  const std::string sum = std::to_string(h.size()) + " edits" + (nUndone ? ", " + std::to_string(nUndone) + " undone" : "") +
                          "; Claude's are marked";
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + P(5.0f));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), sum.c_str());
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.x = ImGui::GetWindowPos().x + P(8.0f);
  int nGroup = -1;
  for (size_t i = h.size(); i-- > 0;) {
    const StudioEdit &e = h[i];
    if (e.nGroup == nGroup)
      continue;
    nGroup = e.nGroup;
    const bool bDone = static_cast<int>(i) < StudioHistoryAt();
    const float rh = P(30.0f);
    ImGui::PushID(static_cast<int>(i));
    if (RowHit("##h", p, w - P(16.0f), rh, false) && e.doc == "types" && e.path.size() >= 2)
      OpenDoc(StudioTableOf(e.path[0]), e.path[1]);
    ImGui::PopID();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(4.0f), Mid(p.y, rh, kStudioFsXs), C(TK_INK3), Clock(e.nTime));
    StudioChip(d, ImVec2(p.x + P(54.0f), p.y + (rh - P(18.0f)) / 2.0f), e.sender.c_str(), e.sender == "claude" ? CH_CLAUDE : CH_PLAIN);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(124.0f), Mid(p.y, rh, kStudioFsS), bDone ? C(TK_INK) : C(TK_INK3), e.label,
      w - P(220.0f));
    if (!bDone)
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + w - P(70.0f), Mid(p.y, rh, kStudioFsXs), C(TK_INK3), "undone");
    else if (e.nGroup == StudioPending())
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + w - P(70.0f), Mid(p.y, rh, kStudioFsXs), C(TK_CHANGED), "waiting");
    d->AddLine(ImVec2(p.x, p.y + rh - 0.5f), ImVec2(p.x + w - P(16.0f), p.y + rh - 0.5f), C(TK_ROW));
    p.y += rh;
  }
  if (h.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(4.0f), p.y + P(6.0f), C(TK_INK3), "No edits yet.");
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(8.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  ImGui::EndChild();
}

void OutputTab(ImVec2 a, ImVec2 b) {
  if (!Region("##output", a, b)) {
    ImGui::EndChild();
    return;
  }
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.x += P(10.0f);
  p.y += P(6.0f);
  for (const OutLine &l : g_out) {
    const int tok = l.level == "error" ? TK_BAD : l.level == "warn" ? TK_WARN : l.level == "claude" ? TK_CLAUDE : TK_INK2;
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x, p.y, C(TK_INK3), l.time);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(64.0f), p.y, C(tok), l.level);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(122.0f), p.y, C(TK_INK2), l.text, b.x - p.x - P(140.0f));
    p.y += P(18.0f);
  }
  if (g_out.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x, p.y, C(TK_INK3), "Nothing yet: saves, pushes and refusals are said here.");
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(8.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}

void Dock(ImVec2 a, ImVec2 b, const ImVec4 *pRoom, float fSide) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioPane(d, a, b, 30.0f, pRoom);
  // the tabs
  const int nProblems = static_cast<int>(Problems().size()) + StudioCheckProblems(nullptr, nullptr);
  const int nHistory = static_cast<int>(StudioHistory().size());
  struct DockTab {
    const char *psz;
    int n;
  };
  const DockTab tabs[] = {{"Game", 0}, {"Problems", nProblems}, {"History", nHistory}, {"Output", 0}, {"Claude", StudioClaudeBadge()}};
  float x = a.x + P(8.0f);
  const float th = P(30.0f);
  for (int i = 0; i < 5; ++i) {
    const std::string n = tabs[i].n ? std::to_string(tabs[i].n) : std::string();
    const float cw = n.empty() ? 0.0f : TW(STUDIO_FONT_MONO, 10.0f, n) + P(16.0f);
    const float w = TW(STUDIO_FONT_WIDE, 10.5f, tabs[i].psz, 0.1f, true) + P(20.0f) + cw;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##dt", ImVec2(w, th))) {
      g_nDockTab = i;
      g_bDock = true;
    }
    const bool bHover = ImGui::IsItemHovered();
    ImGui::PopID();
    const bool bOn = g_nDockTab == i && g_bDock;
    if (bOn)
      d->AddLine(ImVec2(x, a.y + th - P(1.0f)), ImVec2(x + w, a.y + th - P(1.0f)), C(TK_SEL_LINE), P(2.0f));
    const float tw = Wide(d, 10.5f, x + P(10.0f), Mid(a.y, th, 10.5f), bOn ? C(TK_HI) : bHover ? C(TK_INK) : C(TK_INK3), tabs[i].psz);
    if (!n.empty()) {
      const ImVec2 cp(x + P(16.0f) + tw, a.y + (th - P(15.0f)) / 2.0f);
      d->AddRectFilled(cp, ImVec2(cp.x + cw - P(6.0f), cp.y + P(15.0f)), C(TK_KBD_BG), P(8.0f));
      T(d, STUDIO_FONT_MONO, 10.0f, cp.x + P(5.0f), Mid(cp.y, P(15.0f), 10.0f), C(TK_INK2), n);
    }
    x += w;
  }
  // where the game shows, beside the tabs
  {
    static const char *const kAt[] = {"In the dock", "In the preview", "Large"};
    ImGui::SetCursorScreenPos(ImVec2(x + P(12.0f), a.y + P(4.0f)));
    if (x + P(12.0f) + StudioSegWidth(kAt, 3, kStudioFsXs) < b.x - P(240.0f))
      g_nGameAt = StudioSeg("##gameat", kAt, 3, g_nGameAt, 22.0f, kStudioFsXs);
  }
  // the right: the game's state and the dock's own button
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(32.0f), a.y + P(4.0f)));
  if (StudioButton("##dock", "", "dock", SB_PLAIN, 22.0f, 22.0f))
    g_bDock = !g_bDock;
  std::string words;
  PushState(&words);
  words[0] = static_cast<char>(toupper(static_cast<unsigned char>(words[0])));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, b.x - P(42.0f) - TW(STUDIO_FONT_TEXT, kStudioFsXs, words), Mid(a.y, th, kStudioFsXs),
    C(TK_INK3), words);
  if (!g_bDock)
    return;
  const ImVec2 ba(a.x + 1, a.y + th), bb(b.x - 1, b.y - 1);
  switch (g_nDockTab) {
  case 0: GameTab(ba, bb, fSide); break;
  case 1: ProblemsTab(ba, bb); break;
  case 2: HistoryTab(ba, bb); break;
  case 4: StudioClaudeTab(ba, bb); break;
  default: OutputTab(ba, bb); break;
  }
}

// ---- the status bar ------------------------------------------------------------------------------------------------

void Status(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddRectFilled(a, b, C(TK_BAR));
  d->AddLine(ImVec2(a.x, a.y + 0.5f), ImVec2(b.x, a.y + 0.5f), C(TK_LINE));
  const float h = b.y - a.y;
  float x = a.x + P(12.0f);
  if (StudioIsOpen()) {
    const PortJson &mod = StudioDoc("mod");
    x += T(d, STUDIO_FONT_BOLD, kStudioFsXs, x, Mid(a.y, h, kStudioFsXs), C(TK_INK),
           mod["name"].Str(StudioModId().c_str()) + " " + mod["version"].Str());
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, h, kStudioFsXs), C(TK_INK2), " \xC2\xB7 " + StudioModId()) + P(18.0f);
    const int n = StudioUnsaved();
    const std::string c = n ? std::to_string(n) + (n == 1 ? " change not saved" : " changes not saved") : std::string("saved");
    x += StudioChip(d, ImVec2(x, a.y + (h - P(17.0f)) / 2.0f), c.c_str(), n ? CH_GOLD : CH_GOOD, 17.0f) + P(18.0f);
    const int nPush = StudioPushCount();
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, h, kStudioFsXs), C(TK_INK2),
           nPush ? "Pushed: push " + std::to_string(nPush) : std::string("Not pushed yet")) + P(18.0f);
  } else {
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, h, kStudioFsXs), C(TK_INK2), "No mod open") + P(18.0f);
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, h, kStudioFsXs), C(TK_INK2),
    DesignInGame() ? "Game: " + (g_tried.empty() ? std::string("the canvas") : g_tried) : std::string("Game: starting"));
  // the right: the last thing said, and the window's size
  const ImGuiViewport *v = ImGui::GetMainViewport();
  char sz[64];
  snprintf(sz, sizeof sz, "%d by %d \xC2\xB7 %d %%", static_cast<int>(v->Size.x * ImGui::GetIO().DisplayFramebufferScale.x),
           static_cast<int>(v->Size.y * ImGui::GetIO().DisplayFramebufferScale.y), static_cast<int>(StudioScale() * 100.0f + 0.5f));
  float r = b.x - P(12.0f) - TW(STUDIO_FONT_BOLD, kStudioFsXs, sz);
  T(d, STUDIO_FONT_BOLD, kStudioFsXs, r, Mid(a.y, h, kStudioFsXs), C(TK_INK), sz);
  if (!g_said.empty()) {
    r -= P(18.0f) + TW(STUDIO_FONT_TEXT, kStudioFsXs, g_said);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, r, Mid(a.y, h, kStudioFsXs), C(TK_INK2), g_said);
  }
}

// ---- the New record dialog ---------------------------------------------------------------------------------------

void NewDialog(void) {
  if (g_nNewTable < 0)
    return;
  ImGui::OpenPopup("##newrecord");
  const ImGuiViewport *v = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(v->Pos.x + v->Size.x / 2.0f, v->Pos.y + v->Size.y * 0.3f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(P(520.0f), 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(18.0f), P(16.0f)));
  if (!ImGui::BeginPopupModal("##newrecord", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PopStyleVar();
    return;
  }
  ImGui::PopStyleVar();
  const char *pszKind = KindOf(g_nNewTable).pszLabel;
  StudioLabel(STUDIO_FONT_WIDE, 13.0f, C(TK_HEAD), ("New " + std::string(pszKind).substr(0, strlen(pszKind) - 1)).c_str(), 0.06f, true);
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Its name in the mod: letters, digits and _");
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  StudioInput("##name", g_szNewName, sizeof g_szNewName, 484.0f, 28.0f, "hybrid_reaver");
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Starts from: a classic row's name, or one of the mod's; empty for none");
  StudioInput("##base", g_szNewBase, sizeof g_szNewBase, 484.0f, 28.0f, "protoss_zealot");
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  if (StudioButton("##make", "Make it", "plus", SB_GOOD, 28.0f, 0.0f, kStudioFsS, g_szNewName[0] != '\0') ||
      (ImGui::IsKeyPressed(ImGuiKey_Enter) && g_szNewName[0])) {
    const int t = g_nNewTable;
    const std::string name = g_szNewName;
    DesignCall("studio_create",
               RecordFields(t, name) + (g_szNewBase[0] ? ",\"base\":" + Q(g_szNewBase) : std::string()) + ",\"sender\":\"you\"",
               [t, name](const PortJson &a) {
                 if (a["ok"].kind == PortJson::BOOL && a["ok"].b) {
                   OpenDoc(t, name);
                   g_nView = 0;
                 } else {
                   g_said = a["error"].Str("refused");
                   Out("error", g_said);
                 }
               });
    g_nNewTable = -1;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##cancel", "Cancel", nullptr, SB_PLAIN, 28.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    g_nNewTable = -1;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void Keys(void) {
  const ImGuiInputFlags kGlobal = ImGuiInputFlags_RouteGlobal;
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, kGlobal))
    StudioAsk("studio_save", "");
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, kGlobal))
    StudioAsk("studio_undo", "");
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, kGlobal) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, kGlobal))
    StudioAsk("studio_redo", "");
  if (ImGui::Shortcut(ImGuiKey_F5, kGlobal))
    Push();
  if (ImGui::Shortcut(ImGuiKey_F9, kGlobal))
    Again();
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_K, kGlobal))
    g_bFocusFind = true;
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, kGlobal)) {
    if (g_nWs == 9)
      StudioStringsFocusFind();
    else if (g_nWs == 13)
      StudioScriptsFocusFind();
    else
      g_bFocusFilter = true;
  }
}

// studio_show {table?, name?, view?, level?, workspace?, dock?, game?, scale?, dialog?, base?, filter?}: what the frame shows, as the
// modder would click it: a record opened (its view and level), a workspace, the dock's tab. For
// Claude's grabs of the studio and for a tool that points the modder at something.
bool CmdShow(const PortJson &j, std::string *pAnswer) {
  const std::string table = j["table"].Str(), name = j["name"].Str();
  if (!name.empty()) {
    const int t = StudioTableOf(table.empty() ? "units" : table);
    StudioRecord rec;
    if (t < 0 || !StudioRecordOf(t, name, &rec)) {
      *pAnswer = "no record " + name + " in " + (table.empty() ? "units" : table);
      return false;
    }
    OpenDoc(t, name);
    g_nWs = 0;  // a record shows among the documents; a workspace asked for below still wins
  } else if (j["home"].kind == PortJson::BOOL && j["home"].b) {
    g_current = StudioOpenRecord();
  } else if (!table.empty() && StudioTableOf(table) >= 0) {
    StudioTablesShow(StudioTableOf(table));  // a table without a record: the one Tables shows
  }
  auto pick = [](const std::string &word, const char *const *ppNames, int n, int *pOut) {
    for (int i = 0; i < n; ++i)
      if (Lower(word) == Lower(ppNames[i]))
        *pOut = i;
  };
  static const char *const kLevels[] = {"simple", "detailed", "raw"};
  static const char *const kWs[] = {"make", "tables", "art", "looks", "effects", "cards", "test", "checks", "mod", "strings", "balance", "statuses", "tilesets", "scripts"};
  static const char *const kDock[] = {"game", "problems", "history", "output", "claude"};
  pick(j["view"].Str(), kUnitViews, 7, &g_nView);
  pick(j["level"].Str(), kLevels, 3, &g_nLevel);
  pick(j["workspace"].Str(), kWs, 14, &g_nWs);
  if (!j["dock"].Str().empty()) {
    g_bDock = j["dock"].Str() != "closed";
    pick(j["dock"].Str(), kDock, 5, &g_nDockTab);
  }
  if (j["scale"].kind == PortJson::NUMBER)
    StudioScaleSet(static_cast<float>(j["scale"].d));
  static const char *const kAt[] = {"dock", "preview", "large"};
  pick(j["game"].Str(), kAt, 3, &g_nGameAt);
  if (j["filter"].kind == PortJson::STRING)
    snprintf(g_szFilter, sizeof g_szFilter, "%s", j["filter"].s.c_str());
  if (j["dialog"].Str() == "new_unit")
    StudioNewUnitOpen(j["base"].Str());
  if (j["dialog"].Str() == "new_mod")
    StudioNewModOpen();
  *pAnswer = "\"view\":" + Q(kUnitViews[g_nView]) + ",\"level\":" + Q(kLevels[g_nLevel]) + ",\"workspace\":" + Q(kWs[g_nWs]);
  return true;
}

}  // namespace

StudioOpenRecord &StudioCurrent(void) {
  return g_current;
}

void StudioWorkspaceSet(int nWs) {
  g_nWs = nWs;
}

int StudioWorkspaceShown(void) {
  return g_nWs;
}

void StudioOpenDoc(int t, const std::string &name, int nView) {
  OpenDoc(t, name);
  g_nView = nView;
  g_nWs = 0;
}

int StudioChangedSinceSave(int t, const std::string &name, const std::string &key) {
  return ChangedSinceSave(t, name, key);
}

void StudioLog(const std::string &level, const std::string &text) {
  Out(level, text);
}

void StudioAsk(const char *pszCmd, const std::string &fields) {
  const std::string cmd = pszCmd;
  DesignCall(pszCmd, fields + (fields.empty() ? "" : ",") + "\"sender\":\"you\"", [cmd](const PortJson &a) {
    if (!(a["ok"].kind == PortJson::BOOL && a["ok"].b)) {
      g_said = a["error"].Str("refused");
      Out("error", cmd + ": " + g_said);
    } else if (cmd == "studio_save") {
      g_said = "saved";
      Out("info", "saved " + StudioModId());
    } else if (cmd == "studio_push") {
      g_said = "pushed";
      Out("info", "push " + std::to_string(static_cast<int>(a["push"].Num(0))) + " asked");
    } else if (cmd == "studio_open") {
      g_docs.clear();
      g_current = StudioOpenRecord();
      Out("info", "opened " + StudioModId());
    }
  });
}

void StudioSay(const std::string &what) {
  g_said = what;
}

const std::string &StudioSaid(void) {
  return g_said;
}

void StudioShellInit(void) {
  PortControlCommand("studio_show", CmdShow);
}

void StudioMenuBar(void) {
  // the studio draws its own menu bar in its window; here only the keys and the display's scale
  StudioStyleCheck();
  Keys();
}

namespace {

// A splitter in the gap between two panes: dragged, it moves the edge by the pointer's travel in
// the design's pixels (fSign -1 for an edge whose pane grows the other way). True when let go.
bool Splitter(const char *pszId, ImVec2 a, ImVec2 b, bool bAcross, float fSign, float *pfSize, float fMin, float fMax) {
  ImGui::SetCursorScreenPos(a);
  ImGui::InvisibleButton(pszId, ImVec2(fmaxf(b.x - a.x, 1.0f), fmaxf(b.y - a.y, 1.0f)));
  const bool bHover = ImGui::IsItemHovered(), bHeld = ImGui::IsItemActive();
  if (bHover || bHeld)
    ImGui::SetMouseCursor(bAcross ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
  if (bHeld) {
    const ImVec2 dm = ImGui::GetIO().MouseDelta;
    *pfSize = SDL_clamp(*pfSize + fSign * (bAcross ? dm.x : dm.y) / P(1.0f), fMin, fMax);
  }
  if (bHover || bHeld) {
    ImDrawList *d = ImGui::GetWindowDrawList();
    if (bAcross)
      d->AddLine(ImVec2((a.x + b.x) / 2.0f, a.y), ImVec2((a.x + b.x) / 2.0f, b.y), C(bHeld ? TK_BTN_RIM_HI : TK_SEL_LINE), P(2.0f));
    else
      d->AddLine(ImVec2(a.x, (a.y + b.y) / 2.0f), ImVec2(b.x, (a.y + b.y) / 2.0f), C(bHeld ? TK_BTN_RIM_HI : TK_SEL_LINE), P(2.0f));
  }
  if (ImGui::IsItemDeactivated())
    return true;
  return false;
}

void SizesRead(void) {
  if (g_bSizesRead)
    return;
  g_bSizesRead = true;
  double d = 0.0;
  if (PortSettingNumber("studio", "explorerWidth", &d)) g_fExW = static_cast<float>(d);
  if (PortSettingNumber("studio", "rightWidth", &d)) g_fRightW = static_cast<float>(d);
  if (PortSettingNumber("studio", "inspectorWidth", &d)) g_fInW = static_cast<float>(d);
  if (PortSettingNumber("studio", "dockHeight", &d)) g_fDockH = static_cast<float>(d);
  if (PortSettingNumber("studio", "previewHeight", &d)) g_fPvH = static_cast<float>(d);
  if (PortSettingNumber("studio", "gameAt", &d)) g_nGameAt = SDL_clamp(static_cast<int>(d), 0, 2);
}

void SizesKeep(void) {
  PortSettingSetNumber("studio", "explorerWidth", g_fExW);
  PortSettingSetNumber("studio", "rightWidth", g_fRightW);
  PortSettingSetNumber("studio", "inspectorWidth", g_fInW);
  PortSettingSetNumber("studio", "dockHeight", g_fDockH);
  PortSettingSetNumber("studio", "previewHeight", g_fPvH);
}

// The camera's zoom as the game's room changes height: a unit stays its size on screen; when the
// game first shows, a map pixel is a screen pixel.
void FollowRoomZoom(const ImVec4 &room) {
  const float fH = room.w * ImGui::GetIO().DisplayFramebufferScale.y;
  if (!DesignInGame() || fH < 32.0f) {
    if (!DesignInGame())
      g_fRoomH = 0.0f;
    return;
  }
  if (g_fRoomH <= 0.0f) {
    DesignCall("design_view", "\"zoom\":" + Fmt(SDL_clamp(fH / 480.0f, 0.5f, 8.0f), 3), nullptr);
  } else if (fabsf(fH - g_fRoomH) >= 1.0f) {
    const double z = PortCameraZoomTarget() * fH / g_fRoomH;
    DesignCall("design_view", "\"zoom\":" + Fmt(SDL_clamp(z, 0.5, 8.0), 3), nullptr);
  }
  g_fRoomH = fH;
}

}  // namespace

void StudioPanels(void) {
  Tick();
  SizesRead();
  const ImGuiViewport *v = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(v->Pos);
  ImGui::SetNextWindowSize(v->Size);
  ImGui::SetNextWindowViewport(v->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("##studio", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float X = v->Pos.x, Y = v->Pos.y, W = v->Size.x, H = v->Size.y;
  const float S = P(1.0f);
  // the layout at the window's width (cs-shell.css): three columns up to 2400 design pixels wide,
  // the preview beside the inspector past that; each edge between panes dragged where the modder
  // wants it
  const bool bQhd = W / S >= 2400.0f && H / S >= 1300.0f;
  const bool bTest = g_nWs == 6;
  const bool bArt = g_nWs == 2;
  const bool bWs = g_nWs == 1 || g_nWs == 4 || g_nWs == 7;
  const float top = Y + P(84.0f), bottom = Y + H - P(28.0f);
  const float pad = P(6.0f), gap = P(6.0f);
  const float fMainH = (bottom - top) / S;
  float exW = g_fExW > 0.0f ? g_fExW : bQhd ? 300.0f : 264.0f;
  float rightW = g_fRightW > 0.0f ? g_fRightW : bQhd ? (bArt ? 600.0f : 470.0f) : bArt ? 460.0f : 420.0f;
  float inW = g_fInW > 0.0f ? g_fInW : 400.0f;
  float dockH = g_fDockH > 0.0f ? g_fDockH : bQhd ? (bTest ? 820.0f : 380.0f) : bTest ? 590.0f : 286.0f;
  float pvH = g_fPvH > 0.0f ? g_fPvH : bArt ? 440.0f : 300.0f;
  if (g_nDockTab == 4 && g_fDockH <= 0.0f)
    dockH = bQhd ? 520.0f : 400.0f;  // the Claude tab: its words and its proposal side by side
  if (!g_bDock)
    dockH = 30.0f;
  const ImVec2 exA(X + pad, top + pad), exB(X + pad + P(exW), bottom - pad);
  ImVec2 docsA, docsB, pvA, pvB, inA, inB, dockA, dockB;
  const float mx = exB.x + gap;
  if (!bQhd) {
    const float rx = X + W - pad - P(rightW);
    docsA = ImVec2(mx, top + pad);
    docsB = ImVec2(rx - gap, bottom - pad - P(dockH) - gap);
    pvA = ImVec2(rx, top + pad);
    pvB = ImVec2(X + W - pad, top + pad + P(pvH));
    inA = ImVec2(rx, pvB.y + gap);
    inB = ImVec2(X + W - pad, bottom - pad);
    dockA = ImVec2(mx, docsB.y + gap);
    dockB = ImVec2(rx - gap, bottom - pad);
    if (bWs) {
      // a workspace: no preview, the inspector the whole column
      inA = pvA;
      pvB = pvA;
    }
  } else {
    const float inX = X + W - pad - P(inW), pvX = inX - gap - P(rightW);
    docsA = ImVec2(mx, top + pad);
    docsB = ImVec2(bWs ? inX - gap : pvX - gap, bottom - pad - P(dockH) - gap);
    pvA = ImVec2(pvX, top + pad);
    pvB = ImVec2(inX - gap, docsB.y);
    inA = ImVec2(inX, top + pad);
    inB = ImVec2(X + W - pad, docsB.y);
    dockA = ImVec2(mx, docsB.y + gap);
    dockB = ImVec2(X + W - pad, bottom - pad);
    if (bWs)
      pvB = pvA;
  }
  if (g_nGameAt == GAME_PREVIEW && pvB.y <= pvA.y)
    g_nGameAt = GAME_DOCK;  // a workspace has no preview
  // the game's room: the dock's Game tab right of its side, the preview's stage, or the documents' pane
  const float side = bTest ? 340.0f : 290.0f;
  ImVec4 room(0, 0, 0, 0);
  bool bRoom = false;
  if (DesignInGame()) {
    if (g_nGameAt == GAME_DOCK && g_bDock && g_nDockTab == 0) {
      room = ImVec4(dockA.x + P(side) + 1, dockA.y + P(30.0f), dockB.x - dockA.x - P(side) - 2,
                    dockB.y - dockA.y - P(30.0f) - P(28.0f));
      bRoom = true;
    } else if (g_nGameAt == GAME_PREVIEW) {
      room = ImVec4(pvA.x + 1, pvA.y + P(30.0f), pvB.x - pvA.x - 2, pvB.y - pvA.y - P(30.0f) - 1);
      bRoom = true;
    } else if (g_nGameAt == GAME_LARGE) {
      room = ImVec4(docsA.x + 1, docsA.y + P(30.0f), docsB.x - docsA.x - 2, docsB.y - docsA.y - P(30.0f) - 1);
      bRoom = true;
    }
  }
  if (bRoom && room.z >= 1.0f && room.w >= 1.0f) {
    DesignRoomSet(room.x, room.y, room.z, room.w);
    FollowRoomZoom(room);
  } else {
    bRoom = false;
  }
  if (DesignInGame() != g_bWasInGame) {
    g_bWasInGame = DesignInGame();
    g_fRoomH = 0.0f;  // a new game: the zoom found again from its room
    if (g_bWasInGame)
      DesignCall("reveal", "", nullptr);  // the canvas a test bench: the whole map seen, no fog
  }
  const ImVec4 *pRoom = bRoom ? &room : nullptr;
  // the ground round the panes, opaque, with the game's place left open
  const float groundY = fminf(Y + H * 0.6f, bRoom ? room.y : Y + H);
  d->AddRectFilledMultiColor(ImVec2(X, Y), ImVec2(X + W, groundY), C(TK_BG_TOP), C(TK_BG_TOP), C(TK_BG), C(TK_BG));
  StudioFill(d, ImVec2(X, groundY), ImVec2(X + W, Y + H), C(TK_BG), pRoom);
  d->AddRectFilled(ImVec2(X, Y), ImVec2(X + W, Y + P(30.0f)), C(TK_BAR));
  MenuBar(ImVec2(X, Y), ImVec2(X + W, Y + P(30.0f)));
  Toolbar(ImVec2(X, Y + P(30.0f)), ImVec2(X + W, top));
  Explorer(exA, exB);
  if (g_nGameAt == GAME_LARGE)
    LargeGame(docsA, docsB, g_nGameAt == GAME_LARGE ? pRoom : nullptr);
  else
    Documents(docsA, docsB);
  if (pvB.y > pvA.y)
    Preview(pvA, pvB, g_nGameAt == GAME_PREVIEW ? pRoom : nullptr);
  Inspector(inA, inB);
  Dock(dockA, dockB, g_nGameAt == GAME_DOCK ? pRoom : nullptr, side);
  Status(ImVec2(X, bottom), ImVec2(X + W, Y + H));
  // the edges between the panes, dragged; a drag keeps the size it reached
  bool bKeep = false;
  auto split = [&](const char *pszId, ImVec2 sa, ImVec2 sb, bool bAcross, float fSign, float *pfLocal, float *pfKept,
                   float fMin, float fMax) {
    bKeep |= Splitter(pszId, sa, sb, bAcross, fSign, pfLocal, fMin, fMax);
    if (ImGui::IsItemActive())
      *pfKept = *pfLocal;
  };
  split("##split-ex", ImVec2(exB.x, top + pad), ImVec2(exB.x + gap, bottom - pad), true, 1.0f, &exW, &g_fExW, 180.0f, 560.0f);
  if (!bQhd) {
    split("##split-right", ImVec2(pvA.x - gap, top + pad), ImVec2(pvA.x, bottom - pad), true, -1.0f, &rightW, &g_fRightW,
          300.0f, (W / S) * 0.5f);
    if (!bWs)
      split("##split-pv", ImVec2(pvA.x, pvB.y), ImVec2(pvB.x, pvB.y + gap), false, 1.0f, &pvH, &g_fPvH, 140.0f, fMainH - 160.0f);
  } else {
    split("##split-pv", ImVec2(pvA.x - gap, top + pad), ImVec2(pvA.x, docsB.y), true, -1.0f, &rightW, &g_fRightW, 300.0f, 1200.0f);
    split("##split-in", ImVec2(inA.x - gap, top + pad), ImVec2(inA.x, docsB.y), true, -1.0f, &inW, &g_fInW, 280.0f, 900.0f);
  }
  if (g_bDock)
    split("##split-dock", ImVec2(dockA.x, docsB.y), ImVec2(dockB.x, dockA.y), false, -1.0f, &dockH, &g_fDockH, 120.0f,
          fMainH - 140.0f);
  if (bKeep)
    SizesKeep();
  static int s_nGameAtKept = -1;
  if (g_nGameAt != s_nGameAtKept) {
    s_nGameAtKept = g_nGameAt;
    PortSettingSetNumber("studio", "gameAt", g_nGameAt);
  }
  NewDialog();
  StudioNewUnitDraw();
  StudioNewModDraw();
  StudioTestTick();
  StudioClaudeTick();
  // a click in the game gives it the keyboard; one anywhere else gives the panels theirs back
  if (bRoom && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    if (m.x >= room.x && m.y >= room.y && m.x < room.x + room.z && m.y < room.y + room.w &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
      ImGui::SetWindowFocus(nullptr);
  }
  ImGui::End();
}

void StudioLayout(ImGuiID dock, const ImVec2 &size) {
  // the studio lays its panes out itself: the dockspace stays one empty node
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, size);
  ImGui::DockBuilderFinish(dock);
}
