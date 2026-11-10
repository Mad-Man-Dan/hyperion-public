// studio_modes.cpp -- the Mode workspace: a mod that is a game mode (docs/mod-data.md, "A game
// mode"), its mode block read and changed as the other workspaces change records, and its
// generate rolled to see what a run comes out as.
//
// Everything here edits mod.json's "mode" block through StudioPut, so every change is one edit
// the undo takes back. Nothing names a mode: the views are drawn from the block itself, as the
// game's own Game Mode tab is drawn from a mode's declaration.
//
//   studio_mode {}                                   the mod's mode block, its settings and presets
//   studio_mode_set {field, value}                   a field of the block (line, about, sim, enemy_player, needs,
//                                                    players, notes, preview, game_type), or the director, which is
//                                                    types.json's "game" block (director, director_every)
//   studio_mode_setting {id, field?, value?, new?, kind?, rename?, move?}   a setting made or changed
//   studio_mode_setting_delete {id}                  a setting taken away
//   studio_mode_preset {id, field?, value?, new?, values?}                  a preset made or changed
//   studio_mode_preset_delete {id}                   a preset taken away
//   studio_mode_roll {seed?, map?, settings?, race?}  generate run for a seed, what it gives back
//   studio_mode_ai {}                                what the mode's units and cards call to think, and chose
//   studio_mode_test {map?, settings?, seed?, race?, computers?}   the mode's game started in the studio
//   studio_mode_test_stop {}                         that game ended
//   studio_modes_show {view?, setting?, preset?}     the workspace shown at a view
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <windows.h>
#include "../design/design_engine.h"        // DesignCall, DesignInGame
#include "../port/data/port_mode.h"         // PortModeDecl, the set-up's own calls
#include "../port/data/port_modcheck.h"     // ModDataModeRoll
#include "../port/data/port_mods.h"         // PortMods, for the mod's place
#include "../port/platform/port_paths.h"    // PortDataFolder, PortMapFolderCount
#include "../port/tools/port_control.h"     // PortControlCommand
#include "studio_draw.h"
#include "studio_modes.h"

namespace {

using namespace studio_draw;

// The views, in the order their tabs are drawn.
enum { VIEW_ABOUT, VIEW_SETTINGS, VIEW_PRESETS, VIEW_SEEN, VIEW_PREVIEW, VIEW_ROLL, VIEW_AI, VIEW_TEST, VIEWS };
const char *const kViews[VIEWS] = {"Overview", "Settings", "Presets", "Set-up", "Preview", "Roll", "AI", "Test"};

// The kinds a setting may be (port_modedecl.cpp), and the fields each one uses.
const char *const kKinds[] = {"choice", "number", "toggle", "list", "seed"};

// The classic game types a mode may be played as, spelled as the set-up writes them
// (port_modedecl.cpp's own list). "" is the block leaving plays out, which plays Melee.
const char *const kPlays[] = {"melee", "free for all", "one on one", "capture the flag", "greed",
                              "slaughter", "sudden death", "ladder", "use map settings", "team melee",
                              "team free for all", "team capture the flag", "top vs bottom"};
const int kPlaysCount = 13;

int g_nView = VIEW_ABOUT;
std::string g_setting, g_preset;  // the one picked in Settings and in Presets

// ---- the mode block -------------------------------------------------------------------------

const PortJson &Mod(void) {
  return StudioDoc("mod");
}

const PortJson &Mode(void) {
  return Mod()["mode"];
}

bool IsMode(void) {
  return Mod()["kind"].Str() == "mode";
}

// A member of the mode block set or taken away. Edits with the same merge key, one after the
// other, are one group, so a line typed is one step of the undo and not a step a letter.
bool Put(const std::vector<std::string> &path, const PortJson &value, const std::string &label,
         const std::string &sender, std::string *pWhy, const std::string &merge = std::string()) {
  std::vector<std::string> whole;
  whole.push_back("mode");
  for (const std::string &s : path)
    whole.push_back(s);
  StudioGroupBegin(sender, label, merge);
  const bool bOk = StudioPut("mod", whole, value, sender, label, pWhy);
  StudioGroupEnd();
  return bOk;
}

// What a field's edits merge under: the place in the block they write.
std::string MergeOf(const std::vector<std::string> &path) {
  std::string s = "mode";
  for (const std::string &p : path)
    s += "/" + p;
  return s;
}

void Ask(const std::string &cmd, const std::string &fields) {
  StudioAsk(cmd.c_str(), fields);
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

// An array of the ids of a block's members, in the order the file has them: the order the game's
// tab draws them in.
std::vector<std::string> Ids(const PortJson &list) {
  std::vector<std::string> out;
  for (size_t i = 0; i < list.size(); ++i) {
    const std::string id = list.At(i)["id"].Str();
    if (!id.empty())
      out.push_back(id);
  }
  return out;
}

int At(const PortJson &list, const std::string &id) {
  for (size_t i = 0; i < list.size(); ++i)
    if (list.At(i)["id"].Str() == id)
      return static_cast<int>(i);
  return -1;
}

// A setting's or a preset's place as a path piece ("settings", "2").
std::vector<std::string> ItemPath(const char *pszWhich, int nAt, const char *pszField = nullptr) {
  std::vector<std::string> path;
  path.push_back(pszWhich);
  path.push_back(std::to_string(nAt));
  if (pszField != nullptr)
    path.push_back(pszField);
  return path;
}

// What a setting's value is when the player sets nothing, by its kind: the first choice, false,
// the least, nothing turned off, a run drawn as the game starts.
PortJson BaseFor(const PortJson &setting) {
  const std::string kind = setting["kind"].Str();
  if (kind == "choice") {
    const PortJson &choices = setting["choices"];
    if (choices.size() > 0)
      return Str(choices.At(0).kind == PortJson::STRING ? choices.At(0).s : choices.At(0)["id"].Str());
    return Str("");
  }
  if (kind == "toggle")
    return Bool(false);
  if (kind == "number")
    return Num(setting["min"].Num(0));
  if (kind == "list") {
    PortJson j;
    j.kind = PortJson::ARRAY;
    return j;
  }
  return Num(0);
}

// Every setting at its base, as the roll and a preset start from.
std::string BasesJson(void) {
  const PortJson &settings = Mode()["settings"];
  std::string out = "{";
  for (size_t i = 0; i < settings.size(); ++i) {
    const std::string id = settings.At(i)["id"].Str();
    if (id.empty())
      continue;
    const PortJson &base = settings.At(i)["base"];
    const PortJson value = base.kind == PortJson::NONE ? BaseFor(settings.At(i)) : base;
    out += (out.size() > 1 ? "," : "") + Q(id) + ":" + PortJsonWrite(value);
  }
  return out + "}";
}

// ---- the block read as a declaration ----------------------------------------------------------
// The set-up's own calls take a PortModeDecl, which the engine reads from an installed mod's
// mod.json. The studio reads the same shape out of its model instead, so the Set-up and Preview
// views answer for the mod as the modder has it rather than as the folder last saw it. Only the
// Luau stays the folder's: the calls build a VM over the mod's scripts, which is why a change to
// a script shows once it is saved.

// One setting, as port_modedecl.cpp's own reading of it: every field, and the base as JSON text.
bool DeclSetting(const PortJson &j, PortModeSetting *pOut, std::string *pWhy) {
  PortModeSetting s;
  s.id = j["id"].Str();
  s.kind = j["kind"].Str();
  if (s.id.empty()) {
    *pWhy = "a setting has no id";
    return false;
  }
  bool bKind = false;
  for (const char *psz : kKinds)
    bKind = bKind || s.kind == psz;
  if (!bKind) {
    *pWhy = s.id + " is a choice, a toggle, a number, a list or a seed, not " +
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
    const PortJson &one = items.At(i);
    PortModeItem it;
    if (one.kind == PortJson::STRING) {
      it.id = it.name = one.s;
    } else {
      it.id = one["id"].Str();
      it.name = one["name"].Str(it.id.c_str());
      it.line = one["line"].Str();
      it.bHard = one["hard"].kind == PortJson::BOOL && one["hard"].b;
    }
    if (it.id.empty()) {
      *pWhy = s.id + " has a choice with no id";
      return false;
    }
    s.items.push_back(it);
    s.choices.push_back(it.id);
  }
  if ((s.kind == "choice" || s.kind == "list") && s.items.empty()) {
    *pWhy = s.id + " has no choices";
    return false;
  }
  if (s.kind == "number") {
    s.min = j["min"].Num(0);
    s.max = j["max"].Num(100);
    s.step = j["step"].Num(1);
    s.unit = j["unit"].Str();
    if (s.step <= 0 || s.max < s.min) {
      *pWhy = s.id + " takes a min at or below its max and a step above 0";
      return false;
    }
  }
  const PortJson &base = j["base"];
  if (base.kind != PortJson::NONE)
    s.base = PortJsonWrite(base);
  else
    s.base = PortJsonWrite(BaseFor(j));
  *pOut = s;
  return true;
}

// The mod's place in PortMods, which is where the calls find its folder and its scripts; -1 when
// the mod is not among the installed ones (a folder opened by path).
int ModPlace(const std::string &id) {
  const std::vector<PortMod> &mods = PortMods();
  for (size_t i = 0; i < mods.size(); ++i)
    if (mods[i].id == id)
      return static_cast<int>(i);
  return -1;
}

// The classic type's number for a name, as PlaysOf gives it; Melee for a name there is none of.
int PlaysNumber(const std::string &name) {
  static const int kNumbers[kPlaysCount] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                            0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0F};
  const std::string low = Lower(name);
  for (int i = 0; i < kPlaysCount; ++i)
    if (low == kPlays[i])
      return kNumbers[i];
  return 0x02;
}

// ---- the maps the roll may be read against --------------------------------------------------

typedef StudioModeMap MapFile;
std::vector<MapFile> g_maps;
bool g_bMapsRead = false;

void WalkMaps(const std::string &dir, int nDepth, std::set<std::string> *pSeen) {
  if (nDepth > 2)
    return;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  std::vector<std::string> folders;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..")
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      folders.push_back(name);
      continue;
    }
    const size_t dot = name.find_last_of('.');
    const std::string ext = dot == std::string::npos ? std::string() : Lower(name.substr(dot));
    if (ext != ".scm" && ext != ".scx")
      continue;
    if (pSeen->insert(Lower(name)).second)
      g_maps.push_back({name, dir + name});
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  for (const std::string &f : folders)
    WalkMaps(dir + f + "\\", nDepth + 1, pSeen);
}

const std::vector<MapFile> &Maps(void) {
  if (g_bMapsRead)
    return g_maps;
  g_bMapsRead = true;
  std::set<std::string> seen;
  WalkMaps(std::string(PortDataFolder()) + "maps\\", 0, &seen);
  if (const char *pszGame = getenv("HYPERION_GAME"))
    WalkMaps(std::string(pszGame) + "maps\\", 0, &seen);
  char sz[MAX_PATH];
  for (int i = 0; i < PortMapFolderCount(); ++i)
    if (PortMapFolderPath(i, "maps\\", sz, sizeof sz))
      WalkMaps(sz, 0, &seen);
  std::stable_sort(g_maps.begin(), g_maps.end(),
                   [](const MapFile &a, const MapFile &b) { return Lower(a.name) < Lower(b.name); });
  return g_maps;
}

// ---- the roll -------------------------------------------------------------------------------

// What the last roll gave: the records generate made and its run, both as read trees, with the
// seed and the map it was rolled for.
struct Roll {
  bool bDone = false;
  bool bOk = false;
  unsigned dwSeed = 0;
  std::string map, race, settings, why;
  PortJson types, run;
};
Roll g_roll;
char g_szSeed[16] = "";
std::string g_rollMap, g_rollRace = "terran";

unsigned DrawnSeed(void) {
  return static_cast<unsigned>(GetTickCount()) % 900000u + 100000u;
}

// generate run for a seed, a map and the settings as they stand. The mod must be pushed into the
// studio's game first: generate is the loader's, and reads the scripts the push loaded.
bool RunRoll(unsigned dwSeed, const std::string &map, const std::string &settings, const std::string &race,
             std::string *pWhy) {
  g_roll = Roll();
  g_roll.bDone = true;
  g_roll.dwSeed = dwSeed;
  g_roll.map = map;
  g_roll.race = race;
  g_roll.settings = settings;
  std::string types, run, why;
  if (!ModDataModeRoll(map.c_str(), settings.c_str(), dwSeed, race.c_str(), &types, &run, &why)) {
    g_roll.why = why;
    *pWhy = why;
    return false;
  }
  std::string err;
  PortJsonParse(types.c_str(), &g_roll.types, &err);
  PortJsonParse(run.c_str(), &g_roll.run, &err);
  g_roll.bOk = true;
  return true;
}

// ---- the views ------------------------------------------------------------------------------

// A row of a form: its label on the left, the editor on the right, the cursor left below it.
float LabelW(void) {
  return P(150.0f);
}

void Row(const char *pszLabel) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y + P(6.0f), C(TK_LABEL), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(at.x + LabelW(), at.y));
}

// A line of text set as one edit when it changed.
void TextRow(const char *pszLabel, const std::vector<std::string> &path, const std::string &now, float fW,
             const char *pszHint = nullptr) {
  Row(pszLabel);
  char sz[512];
  snprintf(sz, sizeof sz, "%s", now.c_str());
  ImGui::PushID(pszLabel);
  if (StudioInput("##v", sz, sizeof sz, fW, P(26.0f), pszHint)) {
    std::string why;
    Put(path, Str(sz), std::string("set the mode's ") + pszLabel, "you", &why, MergeOf(path));
  }
  ImGui::PopID();
  ImGui::Dummy(ImVec2(0, P(4.0f)));
}

void NumberRow(const char *pszLabel, const std::vector<std::string> &path, double now, float fW) {
  Row(pszLabel);
  double d = now;
  ImGui::PushID(pszLabel);
  if (StudioNumber("##v", &d, fW, P(26.0f), true)) {
    std::string why;
    Put(path, Num(d), std::string("set the mode's ") + pszLabel, "you", &why, MergeOf(path));
  }
  ImGui::PopID();
  ImGui::Dummy(ImVec2(0, P(4.0f)));
}

void CheckRow(const char *pszLabel, const std::vector<std::string> &path, bool bNow, const char *pszSays) {
  Row(pszLabel);
  bool b = bNow;
  ImGui::PushID(pszLabel);
  if (StudioCheck("##v", pszSays, &b)) {
    std::string why;
    Put(path, Bool(b), std::string("set the mode's ") + pszLabel, "you", &why);
  }
  ImGui::PopID();
  ImGui::Dummy(ImVec2(0, P(4.0f)));
}

// The director: types.json's "game" block, not mod.json's, so it is put through the types
// document. A mode with no player of its own may still have one, as a game master.
void DirectorRows(float fW) {
  const PortJson &game = StudioDoc("types")["game"];
  const PortJson &dir = game["director"];
  Row("Director");
  char sz[256];
  snprintf(sz, sizeof sz, "%s", dir["call"].Str().c_str());
  if (StudioInput("##dircall", sz, sizeof sz, fW, P(26.0f), "a function of the mode's scripts")) {
    std::string why;
    StudioGroupBegin("you", "set the mode's director", "types/game/director/call");
    StudioPut("types", {"game", "director", "call"}, sz[0] == '\0' ? PortJson() : Str(sz), "you",
              "set the mode's director", &why);
    StudioGroupEnd();
  }
  ImGui::SameLine();
  if (!dir["call"].Str().empty() && StudioButton("diropen", "Open it", "link", SB_FLAT, 26.0f))
    Ask("studio_script_show", "\"function\":" + Q(dir["call"].Str()));
  ImGui::Dummy(ImVec2(0, P(4.0f)));
  if (!dir["call"].Str().empty()) {
    Row("every");
    double dEvery = dir["every"].Num(0);
    if (StudioNumber("##direvery", &dEvery, P(90.0f), P(26.0f), true)) {
      std::string why;
      StudioGroupBegin("you", "set how often the director runs", "types/game/director/every");
      StudioPut("types", {"game", "director", "every"}, Num(dEvery), "you",
                "set how often the director runs", &why);
      StudioGroupEnd();
    }
    ImGui::SameLine();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, at.x + P(8.0f), at.y + P(8.0f), C(TK_INK3),
      "seconds between its calls");
    ImGui::Dummy(ImVec2(0, P(26.0f)));
  }
}

void AboutView(float fW) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const PortJson &m = Mode();
  TextRow("Line", {"line"}, m["line"].Str(), fW * 1.6f, "what it is, in one line");
  TextRow("About", {"about"}, m["about"].Str(), fW * 2.0f, "a paragraph for the panel");
  TextRow("Needs", {"needs"}, m["needs"].Str(), fW, "any race, or a mod's id");
  CheckRow("Simulation", {"sim"}, m["sim"].kind == PortJson::NONE ? true : m["sim"].b,
           "it changes what the game computes");
  CheckRow("Its own enemy", {"enemy_player"}, m["enemy_player"].kind == PortJson::BOOL && m["enemy_player"].b,
           "the mode plays an enemy player itself, and no classic AI starts for it");
  const PortJson &players = m["players"];
  NumberRow("Players, least", {"players", "0"}, players.size() > 0 ? players.At(0).Num(1) : 1, P(90.0f));
  NumberRow("Players, most", {"players", "1"}, players.size() > 1 ? players.At(1).Num(8) : 8, P(90.0f));

  // the Game type list's row: what it is called there, the mod it says it comes from, and the
  // classic type a game of it is launched as
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  ImVec2 pos = ImGui::GetCursorScreenPos();
  Wide(dl, 11.0f, pos.x, pos.y, C(TK_HEAD), "In the Game type list", 0.08f);
  ImGui::Dummy(ImVec2(0, P(20.0f)));
  const PortJson &type = m["game_type"];
  TextRow("Name", {"game_type", "name"}, type["name"].Str(), fW, Mod()["name"].Str("the mod's name").c_str());
  TextRow("Tag", {"game_type", "tag"}, type["tag"].Str(), fW, "the mod its row names, \"from <tag> mod\"");
  Row("Plays as");
  const std::string plays = Lower(type["plays"].Str());
  std::string shown = plays.empty() ? std::string("melee (its own)") : plays;
  if (StudioSelectBegin("plays", shown.c_str(), P(200.0f))) {
    for (int i = 0; i < kPlaysCount; ++i)
      if (StudioItem(kPlays[i], plays == kPlays[i])) {
        std::string why;
        Put({"game_type", "plays"}, Str(kPlays[i]), "set the type the mode plays as", "you", &why);
      }
    StudioSelectEnd();
  }
  ImGui::SameLine();
  pos = ImGui::GetCursorScreenPos();
  T(dl, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(8.0f), C(TK_INK3),
    "the classic type a game of it is launched as");
  ImGui::Dummy(ImVec2(0, P(28.0f)));

  // the two functions it names of its own: the picture beside its settings, and the player it plays
  pos = ImGui::GetCursorScreenPos();
  Wide(dl, 11.0f, pos.x, pos.y, C(TK_HEAD), "What it calls", 0.08f);
  ImGui::Dummy(ImVec2(0, P(20.0f)));
  Row("Preview");
  char szPrev[256];
  snprintf(szPrev, sizeof szPrev, "%s", m["preview"].Str().c_str());
  if (StudioInput("##prev", szPrev, sizeof szPrev, fW, P(26.0f), "a function: what it draws of a map")) {
    std::string why;
    Put({"preview"}, szPrev[0] == '\0' ? PortJson() : Str(szPrev), "set the mode's preview", "you", &why,
        "mode/preview");
  }
  ImGui::SameLine();
  if (!m["preview"].Str().empty() && StudioButton("prevopen", "Open it", "link", SB_FLAT, 26.0f))
    Ask("studio_script_show", "\"function\":" + Q(m["preview"].Str()));
  ImGui::Dummy(ImVec2(0, P(4.0f)));
  DirectorRows(fW);

  ImGui::Dummy(ImVec2(0, P(10.0f)));
  ImVec2 at = ImGui::GetCursorScreenPos();
  Wide(dl, 11.0f, at.x, at.y, C(TK_HEAD), "Notes", 0.08f);
  ImGui::Dummy(ImVec2(0, P(20.0f)));
  const PortJson &notes = m["notes"];
  for (size_t i = 0; i < notes.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    char sz[512];
    snprintf(sz, sizeof sz, "%s", notes.At(i).Str().c_str());
    if (StudioInput("##n", sz, sizeof sz, fW * 2.0f, P(26.0f))) {
      std::string why;
      Put({"notes", std::to_string(i)}, Str(sz), "set a note of the mode's", "you", &why,
          "mode/notes/" + std::to_string(i));
    }
    ImGui::SameLine();
    if (StudioButton("##x", "", "x", SB_FLAT, 26.0f)) {
      std::string why;
      Put({"notes", std::to_string(i)}, PortJson(), "took away a note of the mode's", "you", &why);
    }
    ImGui::PopID();
  }
  if (StudioButton("note+", "A note", "plus", SB_PLAIN, 26.0f)) {
    std::string why;
    Put({"notes", std::to_string(notes.size())}, Str(""), "added a note to the mode", "you", &why);
  }
}

// The list on the left of Settings and Presets: each id, the one picked lit; New and Delete.
float ListW(void) {
  return P(220.0f);
}

bool ListColumn(const char *pszWhich, const PortJson &list, std::string *pSel, float fH) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  bool bChanged = false;
  float y = a.y;
  const float rh = P(28.0f);
  for (size_t i = 0; i < list.size(); ++i) {
    const std::string id = list.At(i)["id"].Str();
    const bool bOn = id == *pSel;
    const ImVec2 ra(a.x, y), rb(a.x + ListW() - P(10.0f), y + rh - P(2.0f));
    ImGui::SetCursorScreenPos(ra);
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::InvisibleButton("row", ImVec2(rb.x - ra.x, rb.y - ra.y))) {
      *pSel = id;
      bChanged = true;
    }
    const bool bHot = ImGui::IsItemHovered();
    ImGui::PopID();
    if (bOn || bHot)
      d->AddRectFilled(ra, rb, C(bOn ? TK_SEL : TK_ROW_HI));
    T(d, bOn ? STUDIO_FONT_BOLD : STUDIO_FONT_TEXT, kStudioFsS, ra.x + P(8.0f), Mid(ra.y, rh - P(2.0f), kStudioFsS),
      C(bOn ? TK_INK : TK_INK2), id.empty() ? std::string("(no id)") : id, ListW() - P(70.0f));
    const std::string kind = list.At(i)["kind"].Str();
    if (!kind.empty())
      StudioChip(d, ImVec2(rb.x - P(56.0f), Mid(ra.y, rh - P(2.0f), 9.0f) - P(2.0f)), kind.c_str());
    y += rh;
  }
  ImGui::SetCursorScreenPos(ImVec2(a.x, y + P(6.0f)));
  if (StudioButton("new", pszWhich, "plus", SB_PLAIN, 26.0f, ListW() - P(10.0f)))
    ImGui::OpenPopup("newid");
  if (ImGui::BeginPopup("newid")) {
    static char szId[64] = "";
    StudioInput("##newid", szId, sizeof szId, P(160.0f), P(26.0f), "its id");
    ImGui::SameLine();
    if (StudioButton("make", "Make", nullptr, SB_GOOD, 26.0f) && szId[0] != '\0') {
      Ask(std::string("studio_mode_") + (strcmp(pszWhich, "A setting") == 0 ? "setting" : "preset"),
          "\"id\":" + Q(szId) + ",\"new\":true,\"sender\":\"you\"");
      *pSel = szId;
      szId[0] = '\0';
      bChanged = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  (void)fH;
  return bChanged;
}

void SettingForm(int nAt, float fW) {
  const PortJson &s = Mode()["settings"].At(nAt);
  const std::string kind = s["kind"].Str();
  TextRow("Id", ItemPath("settings", nAt, "id"), s["id"].Str(), P(200.0f));
  Row("Kind");
  int nKind = 0;
  for (int i = 0; i < 5; ++i)
    if (kind == kKinds[i])
      nKind = i;
  const int nPick = StudioSeg("kind", kKinds, 5, nKind);
  if (nPick != nKind) {
    std::string why;
    StudioGroupBegin("you", "set the setting's kind");
    Put(ItemPath("settings", nAt, "kind"), Str(kKinds[nPick]), "set the setting's kind", "you", &why);
    // the base goes with the kind
    Put(ItemPath("settings", nAt, "base"), BaseFor(s), "set the setting's base", "you", &why);
    StudioGroupEnd();
  }
  ImGui::Dummy(ImVec2(0, P(6.0f)));
  TextRow("Name", ItemPath("settings", nAt, "name"), s["name"].Str(), fW);
  TextRow("Group", ItemPath("settings", nAt, "group"), s["group"].Str(), fW, "the heading it sits under");
  TextRow("Gist", ItemPath("settings", nAt, "gist"), s["gist"].Str(), fW * 1.6f, "what it does, for the briefing");
  if (kind == "number") {
    NumberRow("Least", ItemPath("settings", nAt, "min"), s["min"].Num(0), P(90.0f));
    NumberRow("Most", ItemPath("settings", nAt, "max"), s["max"].Num(0), P(90.0f));
    NumberRow("Step", ItemPath("settings", nAt, "step"), s["step"].Num(1), P(90.0f));
    TextRow("Unit", ItemPath("settings", nAt, "unit"), s["unit"].Str(), P(140.0f), "seconds, tiles");
  }
  if (kind == "choice" || kind == "list") {
    ImGui::Dummy(ImVec2(0, P(8.0f)));
    ImDrawList *d = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos();
    Wide(d, 11.0f, at.x, at.y, C(TK_HEAD), kind == "choice" ? "Choices" : "Items", 0.08f);
    ImGui::Dummy(ImVec2(0, P(20.0f)));
    const char *const pszWhich = kind == "choice" ? "choices" : "items";
    const PortJson &items = s[pszWhich].kind == PortJson::NONE ? s["choices"] : s[pszWhich];
    for (size_t i = 0; i < items.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      const PortJson &one = items.At(i);
      const bool bPlain = one.kind == PortJson::STRING;
      char szId[64], szName[128];
      snprintf(szId, sizeof szId, "%s", bPlain ? one.s.c_str() : one["id"].Str().c_str());
      snprintf(szName, sizeof szName, "%s", bPlain ? "" : one["name"].Str().c_str());
      std::vector<std::string> path = ItemPath("settings", nAt, pszWhich);
      path.push_back(std::to_string(i));
      if (StudioInput("##id", szId, sizeof szId, P(140.0f), P(24.0f), "id")) {
        std::vector<std::string> p = path;
        if (!bPlain)
          p.push_back("id");
        std::string why;
        Put(p, Str(szId), "set a choice's id", "you", &why, MergeOf(p));
      }
      ImGui::SameLine();
      if (StudioInput("##name", szName, sizeof szName, P(180.0f), P(24.0f), "its name")) {
        std::vector<std::string> p = path;
        p.push_back("name");
        std::string why;
        if (bPlain) {
          // a plain id becomes a record with its name
          PortJson rec;
          rec.kind = PortJson::OBJECT;
          Put(path, rec, "gave a choice its name", "you", &why);
          std::vector<std::string> pid = path;
          pid.push_back("id");
          Put(pid, Str(szId), "gave a choice its name", "you", &why);
        }
        Put(p, Str(szName), "gave a choice its name", "you", &why);
      }
      ImGui::SameLine();
      if (StudioButton("##xc", "", "x", SB_FLAT, 24.0f)) {
        std::string why;
        Put(path, PortJson(), "took away a choice", "you", &why);
      }
      ImGui::PopID();
    }
    if (StudioButton("choice+", kind == "choice" ? "A choice" : "An item", "plus", SB_PLAIN, 24.0f)) {
      std::vector<std::string> path = ItemPath("settings", nAt, pszWhich);
      path.push_back(std::to_string(items.size()));
      PortJson rec;
      rec.kind = PortJson::OBJECT;
      std::string why;
      StudioGroupBegin("you", "added a choice");
      Put(path, rec, "added a choice", "you", &why);
      std::vector<std::string> pid = path;
      pid.push_back("id");
      Put(pid, Str("new"), "added a choice", "you", &why);
      StudioGroupEnd();
    }
  }
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 at = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, at.x, at.y, C(TK_HEAD), "Its own words", 0.08f);
  ImGui::Dummy(ImVec2(0, P(20.0f)));
  TextRow("effect", ItemPath("settings", nAt, "effect"), s["effect"].Str(), fW, "a function: what the value does");
  TextRow("read", ItemPath("settings", nAt, "read"), s["read"].Str(), fW, "a function: what the map makes of it");
  TextRow("shown", ItemPath("settings", nAt, "shown"), s["shown"].Str(), fW, "a function: whether it is drawn");
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  if (StudioButton("del", "Take this setting away", "x", SB_PLAIN, 26.0f))
    Ask("studio_mode_setting_delete", "\"id\":" + Q(s["id"].Str()) + ",\"sender\":\"you\"");
}

void PresetForm(int nAt, float fW) {
  const PortJson &p = Mode()["presets"].At(nAt);
  TextRow("Id", ItemPath("presets", nAt, "id"), p["id"].Str(), P(200.0f));
  TextRow("Name", ItemPath("presets", nAt, "name"), p["name"].Str(), fW);
  TextRow("Line", ItemPath("presets", nAt, "line"), p["line"].Str(), fW * 1.6f, "what the preset is, in a line");
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 at = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, at.x, at.y, C(TK_HEAD), "What it sets", 0.08f);
  ImGui::Dummy(ImVec2(0, P(20.0f)));
  const PortJson &settings = Mode()["settings"];
  const PortJson &values = p["values"];
  for (size_t i = 0; i < settings.size(); ++i) {
    const std::string id = settings.At(i)["id"].Str();
    if (id.empty())
      continue;
    const PortJson &v = values[id.c_str()];
    const bool bSet = v.kind != PortJson::NONE;
    ImGui::PushID(static_cast<int>(i));
    Row(id.c_str());
    char sz[256];
    snprintf(sz, sizeof sz, "%s", bSet ? PortJsonWrite(v).c_str() : "");
    if (StudioInput("##pv", sz, sizeof sz, fW, P(24.0f), "the mode's own")) {
      std::string why;
      if (sz[0] == '\0') {
        Put({"presets", std::to_string(nAt), "values", id}, PortJson(), "took a value out of the preset", "you", &why);
      } else {
        PortJson value;
        std::string err;
        if (PortJsonParse(sz, &value, &err))
          Put({"presets", std::to_string(nAt), "values", id}, value, "set a value of the preset", "you", &why);
        else
          StudioSay("that is not a value: " + err);
      }
    }
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  if (StudioButton("del", "Take this preset away", "x", SB_PLAIN, 26.0f))
    Ask("studio_mode_preset_delete", "\"id\":" + Q(p["id"].Str()) + ",\"sender\":\"you\"");
}

// A tree of numbers, strings and tables drawn as rows, for what a roll gives back.
void TreeRows(const PortJson &j, const std::string &name, int nDepth) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float x = at.x + nDepth * P(14.0f);
  if (j.kind == PortJson::OBJECT || j.kind == PortJson::ARRAY) {
    const size_t n = j.kind == PortJson::ARRAY ? j.size() : j.members.size();
    ImGui::SetCursorScreenPos(ImVec2(x, at.y));
    ImGui::PushID(name.c_str());
    const bool bOpen = ImGui::TreeNodeEx("##n", nDepth < 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0, " ");
    ImGui::PopID();
    const ImVec2 row = ImVec2(x + P(22.0f), ImGui::GetItemRectMin().y + P(2.0f));
    float w = T(d, STUDIO_FONT_BOLD, kStudioFsS, row.x, row.y, C(TK_INK), name);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, row.x + w + P(8.0f), row.y + P(1.0f), C(TK_INK_OFF),
      (j.kind == PortJson::ARRAY ? std::to_string(n) + " in a row" : std::to_string(n) + " in it"));
    if (bOpen) {
      if (j.kind == PortJson::ARRAY) {
        for (size_t i = 0; i < n && i < 200; ++i)
          TreeRows(j.At(i), std::to_string(i + 1), nDepth + 1);
      } else {
        for (const PortJsonMember &m : j.members)
          TreeRows(m.second, m.first, nDepth + 1);
      }
      ImGui::TreePop();
    }
    return;
  }
  ImGui::SetCursorScreenPos(ImVec2(x, at.y));
  ImGui::Dummy(ImVec2(P(10.0f), P(18.0f)));
  const float w = T(d, STUDIO_FONT_TEXT, kStudioFsS, x + P(22.0f), at.y + P(2.0f), C(TK_INK2), name);
  T(d, STUDIO_FONT_MONO, kStudioFsS, x + P(22.0f) + w + P(10.0f), at.y + P(2.0f), C(TK_HI), PortJsonWrite(j));
}

void RollView(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  Row("Map");
  const std::vector<MapFile> &maps = Maps();
  std::string shown = g_rollMap.empty() ? std::string("a map to read") : g_rollMap;
  const size_t at = shown.find_last_of("\\/");
  if (at != std::string::npos)
    shown = shown.substr(at + 1);
  if (StudioSelectBegin("map", shown.c_str(), P(280.0f))) {
    for (size_t i = 0; i < maps.size() && i < 400; ++i)
      if (StudioItem(maps[i].name.c_str(), maps[i].path == g_rollMap))
        g_rollMap = maps[i].path;
    StudioSelectEnd();
  }
  ImGui::Dummy(ImVec2(0, P(4.0f)));
  Row("Enemy race");
  static const char *const kRaces[] = {"terran", "zerg", "protoss"};
  int nRace = g_rollRace == "zerg" ? 1 : g_rollRace == "protoss" ? 2 : 0;
  const int nPick = StudioSeg("race", kRaces, 3, nRace);
  if (nPick != nRace)
    g_rollRace = kRaces[nPick];
  ImGui::Dummy(ImVec2(0, P(4.0f)));
  Row("Run");
  StudioInput("##seed", g_szSeed, sizeof g_szSeed, P(140.0f), P(26.0f), "drawn", nullptr, nullptr, true);
  ImGui::SameLine();
  const bool bRoll = StudioButton("roll", g_roll.bDone ? "Roll again" : "Roll", "again", SB_GOOD, 26.0f);
  ImGui::SameLine();
  if (StudioButton("draw", "Draw a seed", nullptr, SB_PLAIN, 26.0f))
    snprintf(g_szSeed, sizeof g_szSeed, "%u", DrawnSeed());
  if (bRoll) {
    const unsigned dwSeed = g_szSeed[0] != '\0' ? static_cast<unsigned>(strtoul(g_szSeed, nullptr, 10)) : DrawnSeed();
    snprintf(g_szSeed, sizeof g_szSeed, "%u", dwSeed);
    std::string why;
    if (!RunRoll(dwSeed, g_rollMap, BasesJson(), g_rollRace, &why))
      StudioSay("the roll did not run: " + why);
  }
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  ImVec2 pos = ImGui::GetCursorScreenPos();
  if (!g_roll.bDone) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_INK3),
      "Roll runs the mode's generate for a seed and the settings as they stand, and shows what it gives back: "
      "the records it makes and its run.");
    return;
  }
  if (!g_roll.bOk) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_BAD), g_roll.why);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y + P(20.0f), C(TK_INK3),
      "Push the mod into the game first (F5): the roll runs the scripts the push loaded.");
    return;
  }
  T(d, STUDIO_FONT_MONO, kStudioFsS, pos.x, pos.y, C(TK_INK_OFF),
    "seed " + std::to_string(g_roll.dwSeed) + "  ·  " + (g_roll.map.empty() ? std::string("no map") : g_roll.map));
  ImGui::Dummy(ImVec2(0, P(24.0f)));
  ImGui::BeginChild("rollout", ImVec2(fW * 2.2f, 0), false);
  TreeRows(g_roll.run, "run", 0);
  for (const PortJsonMember &m : g_roll.types.members)
    TreeRows(m.second, m.first, 0);
  ImGui::EndChild();
}

// ---- what the mode thinks with ----------------------------------------------------------------

// A call the mode makes to play a unit: a unit's brain, or a card button's own finder. The game's
// own finders (a button whose "ai" is a name, not a call) are the classic ones and are named as
// such, since a mode does not write them.
struct AiCall {
  std::string kind;    // "brain", "cast" or "finder"
  std::string of;      // the unit or the card it belongs to
  std::string what;    // what it casts, for a button
  std::string call;    // the function, or the game's finder
  double dEvery = 0;   // how often, in seconds
};

std::vector<AiCall> AiCalls(void) {
  std::vector<AiCall> out;
  for (const PortJsonMember &m : StudioDoc("types")["units"].members) {
    const PortJson &scripts = m.second["scripts"];
    if (scripts["brain"].Str().empty())
      continue;
    AiCall c;
    c.kind = "brain";
    c.of = m.first;
    c.call = scripts["brain"].Str();
    c.dEvery = scripts["every"].Num(0);
    out.push_back(c);
  }
  for (const PortJsonMember &card : StudioDoc("types")["cards"].members) {
    // a card is its buttons in a row, or an object that holds them
    const PortJson &buttons = card.second.kind == PortJson::ARRAY ? card.second : card.second["buttons"];
    for (size_t i = 0; i < buttons.size(); ++i) {
      const PortJson &b = buttons.At(i);
      const PortJson &ai = b["ai"];
      if (ai.kind == PortJson::NONE)
        continue;
      AiCall c;
      c.of = card.first;
      c.what = b["cast"].Str(b["build"].Str().c_str());
      if (ai.kind == PortJson::STRING) {
        c.kind = "finder";
        c.call = ai.s;
      } else {
        c.kind = "cast";
        c.call = ai["call"].Str();
        c.dEvery = ai["every"].Num(0);
      }
      out.push_back(c);
    }
  }
  // the brains first, then the cards' own calls, then the game's finders: the view's sections
  auto nOrder = [](const std::string &kind) { return kind == "brain" ? 0 : kind == "cast" ? 1 : 2; };
  std::stable_sort(out.begin(), out.end(),
                   [&](const AiCall &a, const AiCall &b) { return nOrder(a.kind) < nOrder(b.kind); });
  return out;
}

// A call the game made, as the channel's script_trace gives it: a brain or a card's call that
// ran, what it chose, and why it chose nothing. Read only: the game plays the same whether these
// are read or not.
struct AiRow {
  unsigned dwFrame = 0;
  int nUnit = -1, nType = -1, nPlayer = -1;
  std::string kind, fn, why;
  bool bChose = false;
  int nOrder = -1, nTarget = -1, x = -1, y = -1;
};
std::vector<AiRow> g_rows;   // the newest last
bool g_bRowsOn = false;      // the view asks the game for them while it is open
enum { kRowsKept = 400 };

void ReadRows(void) {
  std::string text;
  if (!StudioTraceRows(&text))
    return;
  PortJson rows;
  std::string err;
  if (!PortJsonParse(("[" + text + "]").c_str(), &rows, &err) || rows.kind != PortJson::ARRAY)
    return;
  for (size_t i = 0; i < rows.size(); ++i) {
    const PortJson &r = rows.At(i);
    AiRow row;
    row.dwFrame = static_cast<unsigned>(r["frame"].Num(0));
    row.nUnit = static_cast<int>(r["unit"].Num(-1));
    row.nType = static_cast<int>(r["type"].Num(-1));
    row.nPlayer = static_cast<int>(r["player"].Num(-1));
    row.kind = r["kind"].Str(r["act"].Str().c_str());
    row.fn = r["fn"].Str(r["from"].Str().c_str());
    row.why = r["why"].Str();
    row.bChose = r["chose"].kind == PortJson::BOOL ? r["chose"].b : r["detail"].kind != PortJson::NONE;
    row.nOrder = static_cast<int>(r["order"].Num(-1));
    row.nTarget = static_cast<int>(r["target"].Num(-1));
    row.x = static_cast<int>(r["x"].Num(-1));
    row.y = static_cast<int>(r["y"].Num(-1));
    g_rows.push_back(row);
  }
  if (g_rows.size() > kRowsKept)
    g_rows.erase(g_rows.begin(), g_rows.begin() + (g_rows.size() - kRowsKept));
}

// A unit's name from the type the game gave it: one of the mod's own records by the row the
// loader gave it, else a classic row, else its number.
std::string UnitWord(int nType) {
  if (nType < 0)
    return "a unit";
  for (const StudioRecord &r : StudioRecords(ST_UNITS))
    if (r.nId == nType && r.kind != SK_CLASSIC)
      return StudioShowName(ST_UNITS, r.name);
  const char *psz = StudioClassicName(ST_UNITS, nType);
  return psz != nullptr ? StudioShowName(ST_UNITS, psz) : "type " + std::to_string(nType);
}

// What a row chose, in words: the order it gave on a unit or at a place, or why it gave none.
std::string ChoseWord(const AiRow &r) {
  if (!r.bChose)
    return r.why.empty() ? std::string("nothing") : r.why;
  std::string s = r.nOrder >= 0 ? "order " + std::to_string(r.nOrder) : std::string("it acted");
  if (r.nTarget >= 0)
    s += " on unit " + std::to_string(r.nTarget);
  else if (r.x >= 0)
    s += " at " + std::to_string(r.x / 32) + "," + std::to_string(r.y / 32);
  return s;
}

// What the mod declares: every brain, every card's own call, every finder.
void AiDeclared(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const std::vector<AiCall> calls = AiCalls();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  if (calls.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_INK3),
      "Nothing of the mod's thinks for itself yet. A unit's scripts may name a brain, and a card's button may "
      "name a call of its own, which run for the units the mode owns and for a computer's.");
    return;
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_INK2),
    "What the mode plays its units with: a unit's brain and a card button's own call, each run for the units the "
    "mode owns and for a computer's. A row opens its function in Scripts.");
  ImGui::Dummy(ImVec2(0, P(28.0f)));
  const float rh = P(26.0f);
  const char *pszWas = "";
  for (size_t i = 0; i < calls.size(); ++i) {
    const AiCall &c = calls[i];
    if (c.kind != pszWas) {
      pszWas = c.kind.c_str();
      pos = ImGui::GetCursorScreenPos();
      Wide(d, 11.0f, pos.x, pos.y + P(6.0f), C(TK_HEAD),
           c.kind == "brain" ? "Brains" : c.kind == "cast" ? "The cards' own calls" : "The game's own finders", 0.08f);
      ImGui::Dummy(ImVec2(0, P(28.0f)));
    }
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + fW * 3.4f, a.y + rh - P(2.0f));
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::InvisibleButton("row", ImVec2(b.x - a.x, b.y - a.y)) && c.kind != "finder")
      Ask("studio_script_show", "\"function\":" + Q(c.call));
    const bool bHot = ImGui::IsItemHovered();
    ImGui::PopID();
    if (bHot)
      d->AddRectFilled(a, b, C(TK_ROW_HI));
    const float fMid = Mid(a.y, rh - P(2.0f), kStudioFsS);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(4.0f), fMid, C(TK_INK2), StudioShowName(ST_UNITS, c.of), fW * 0.8f);
    T(d, STUDIO_FONT_MONO, kStudioFsS, a.x + fW * 0.9f, fMid, C(TK_HI), c.call, fW);
    if (!c.what.empty())
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + fW * 2.0f, fMid + P(1.0f), C(TK_INK_OFF), c.what, fW * 0.9f);
    if (c.dEvery > 0)
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + fW * 3.0f, fMid + P(1.0f), C(TK_INK3),
        std::string("every ") + Fmt(c.dEvery) + "s");
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + P(2.0f)));
  }
  ImGui::Dummy(ImVec2(0, P(16.0f)));
}

// What they chose, in the game the Test view started.
void AiLive(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ReadRows();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y + P(6.0f), C(TK_HEAD), "What they chose", 0.08f);
  ImGui::Dummy(ImVec2(0, P(30.0f)));
  const bool bGame = StudioPlayingWith(nullptr) != nullptr;
  if (!bGame) {
    pos = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_INK3),
      "Play the mode's game in Test and each call is drawn here as it is made: the frame, the unit, and what "
      "it chose or why it chose nothing.");
    ImGui::Dummy(ImVec2(0, P(20.0f)));
    return;
  }
  if (StudioButton("clear", "Clear", nullptr, SB_PLAIN, 24.0f))
    g_rows.clear();
  ImGui::SameLine();
  pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, pos.x + P(8.0f), pos.y + P(6.0f), C(TK_INK_OFF),
    g_rows.empty() ? std::string("nothing yet") : std::to_string(g_rows.size()) + " kept, the newest last");
  ImGui::Dummy(ImVec2(0, P(28.0f)));
  for (size_t i = 0; i < g_rows.size(); ++i) {
    const AiRow &r = g_rows[g_rows.size() - 1 - i];
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float fMid = a.y + P(2.0f);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, a.x + P(4.0f), fMid, C(TK_INK3), std::to_string(r.dwFrame));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + fW * 0.3f, fMid - P(1.0f), C(TK_INK2),
      UnitWord(r.nType),
      fW * 0.7f);
    T(d, STUDIO_FONT_MONO, kStudioFsS, a.x + fW * 1.05f, fMid - P(1.0f), C(TK_HI), r.fn, fW);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + fW * 2.1f, fMid, r.bChose ? C(TK_INK_OFF) : C(TK_INK3), ChoseWord(r),
      fW * 1.2f);
    ImGui::Dummy(ImVec2(fW * 3.4f, P(22.0f)));
  }
  ImGui::Dummy(ImVec2(0, P(20.0f)));
}

void AiView(float fW) {
  // while a game plays, what it is deciding now is what the modder is watching, so it goes first
  if (StudioPlayingWith(nullptr) != nullptr) {
    AiLive(fW);
    AiDeclared(fW);
    return;
  }
  AiDeclared(fW);
  AiLive(fW);
}

// ---- the mode's own game --------------------------------------------------------------------

// What the Test view plays with: a value a setting, as the mode's own JSON, and the seed. They
// start at the settings' bases and are the player's to change, as the game's tab lets a player.
std::map<std::string, std::string> g_test;
char g_szTestSeed[16] = "";
std::string g_testMap, g_testRace = "terran";
int g_nTestComputers = 1;
std::string g_testSaid;

void TestFromBases(void) {
  g_test.clear();
  const PortJson &settings = Mode()["settings"];
  for (size_t i = 0; i < settings.size(); ++i) {
    const std::string id = settings.At(i)["id"].Str();
    if (id.empty())
      continue;
    const PortJson &base = settings.At(i)["base"];
    g_test[id] = PortJsonWrite(base.kind == PortJson::NONE ? BaseFor(settings.At(i)) : base);
  }
}

// The values as one object, for --mode-settings.
std::string TestSettingsJson(void) {
  std::string out = "{";
  for (const std::pair<const std::string, std::string> &v : g_test) {
    PortJson value;
    std::string err;
    if (!PortJsonParse(v.second.c_str(), &value, &err))
      continue;
    out += (out.size() > 1 ? "," : "") + Q(v.first) + ":" + PortJsonWrite(value);
  }
  return out + "}";
}

// A value as --mode-settings writes it: a choice's id, a number, a toggle's on or off, a list's
// items joined by a plus. The command line takes no quotes of its own inside the ones around it,
// so the values go as this list and not as JSON.
std::string ValueWord(const PortJson &v) {
  if (v.kind == PortJson::STRING)
    return v.s;
  if (v.kind == PortJson::BOOL)
    return v.b ? "on" : "off";
  if (v.kind == PortJson::NUMBER) {
    char sz[40];
    snprintf(sz, sizeof sz, "%.10g", v.d);
    return sz;
  }
  if (v.kind == PortJson::ARRAY) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i)
      out += (out.empty() ? "" : "+") + v.At(i).Str();
    return out.empty() ? "none" : out;
  }
  return std::string();
}

std::string TestSettingsList(void) {
  std::string out;
  for (const std::pair<const std::string, std::string> &v : g_test) {
    PortJson value;
    std::string err;
    if (!PortJsonParse(v.second.c_str(), &value, &err))
      continue;
    const std::string word = ValueWord(value);
    if (word.empty() || word.find(',') != std::string::npos || word.find('"') != std::string::npos)
      continue;
    out += (out.empty() ? "" : ",") + v.first + "=" + word;
  }
  return out;
}

// The mode's game started: the mod the studio has open, the map and the computers the view says,
// and the mode with its settings and its seed on the command line.
bool TestPlay(std::string *pWhy) {
  if (!IsMode()) {
    *pWhy = "the mod is not a game mode";
    return false;
  }
  const std::string id = Mod()["id"].Str(StudioModId().c_str());
  std::string extra = "--mode \"" + id + "\" --mode-trace";
  const std::string list = TestSettingsList();
  if (!list.empty())
    extra += " --mode-settings \"" + list + "\"";
  if (g_szTestSeed[0] != '\0')
    extra += std::string(" --mode-seed ") + g_szTestSeed;
  g_rows.clear();  // the calls kept are the game being started's, not the one before it
  return StudioPlayWith(g_testRace, g_testMap, g_nTestComputers, std::string(), extra, pWhy);
}

void TestView(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  if (g_test.empty())
    TestFromBases();
  Row("Map");
  const std::vector<MapFile> &maps = Maps();
  std::string shown = g_testMap.empty() ? std::string("a map to play") : g_testMap;
  const size_t at = shown.find_last_of("\\/");
  if (at != std::string::npos)
    shown = shown.substr(at + 1);
  if (StudioSelectBegin("tmap", shown.c_str(), P(280.0f))) {
    for (size_t i = 0; i < maps.size() && i < 400; ++i)
      if (StudioItem(maps[i].name.c_str(), maps[i].path == g_testMap))
        g_testMap = maps[i].path;
    StudioSelectEnd();
  }
  ImGui::Dummy(ImVec2(0, P(4.0f)));
  Row("Your race");
  static const char *const kRaces[] = {"terran", "zerg", "protoss"};
  int nRace = g_testRace == "zerg" ? 1 : g_testRace == "protoss" ? 2 : 0;
  const int nPick = StudioSeg("trace", kRaces, 3, nRace);
  if (nPick != nRace)
    g_testRace = kRaces[nPick];
  ImGui::Dummy(ImVec2(0, P(4.0f)));
  Row("Computers");
  double dN = g_nTestComputers;
  if (StudioNumber("##tcomp", &dN, P(90.0f), P(26.0f), true))
    g_nTestComputers = static_cast<int>(dN < 1 ? 1 : dN > 7 ? 7 : dN);
  ImGui::Dummy(ImVec2(0, P(4.0f)));
  Row("Run");
  StudioInput("##tseed", g_szTestSeed, sizeof g_szTestSeed, P(140.0f), P(26.0f), "drawn", nullptr, nullptr, true);

  ImGui::Dummy(ImVec2(0, P(10.0f)));
  ImVec2 pos = ImGui::GetCursorScreenPos();
  Wide(d, 11.0f, pos.x, pos.y, C(TK_HEAD), "What it is played with", 0.08f);
  ImGui::Dummy(ImVec2(0, P(20.0f)));
  const PortJson &presets = Mode()["presets"];
  if (presets.size() > 0) {
    Row("A preset");
    if (StudioSelectBegin("tpreset", "take a preset's values", P(220.0f))) {
      for (size_t i = 0; i < presets.size(); ++i)
        if (StudioItem(presets.At(i)["name"].Str(presets.At(i)["id"].Str().c_str()).c_str())) {
          TestFromBases();
          for (const PortJsonMember &m : presets.At(i)["values"].members)
            g_test[m.first] = PortJsonWrite(m.second);
        }
      StudioSelectEnd();
    }
    ImGui::Dummy(ImVec2(0, P(4.0f)));
  }
  const PortJson &settings = Mode()["settings"];
  // a mode that can start a game part way through declares a wave to start at, which no player is
  // shown; the studio is where it is for, so it is drawn first and in words
  const int nWave = At(settings, "start_wave");
  if (nWave >= 0) {
    Row("Start at wave");
    double dWave = atof(g_test["start_wave"].c_str());
    if (StudioNumber("##twave", &dWave, P(90.0f), P(26.0f), true)) {
      const double dLeast = settings.At(nWave)["min"].Num(1), dMost = settings.At(nWave)["max"].Num(99);
      dWave = dWave < dLeast ? dLeast : dWave > dMost ? dMost : dWave;
      g_test["start_wave"] = Fmt(dWave);
    }
    ImGui::SameLine();
    ImVec2 at = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, at.x + P(8.0f), at.y + P(8.0f), C(TK_INK3),
      "the waves before it count as cleared");
    ImGui::Dummy(ImVec2(0, P(28.0f)));
  }
  for (size_t i = 0; i < settings.size(); ++i) {
    const std::string id = settings.At(i)["id"].Str();
    if (id.empty() || static_cast<int>(i) == nWave)
      continue;
    ImGui::PushID(static_cast<int>(i));
    Row(id.c_str());
    char sz[256];
    snprintf(sz, sizeof sz, "%s", g_test[id].c_str());
    if (StudioInput("##tv", sz, sizeof sz, fW, P(24.0f)))
      g_test[id] = sz;
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  int nPort = 0;
  const char *pszPlaying = StudioPlayingWith(&nPort);
  if (StudioButton("play", pszPlaying != nullptr ? "Play it again" : "Play it", "game", SB_GOOD, 28.0f)) {
    std::string why;
    g_testSaid = TestPlay(&why) ? std::string() : why;
  }
  ImGui::SameLine();
  if (StudioButton("bases", "Back to the bases", nullptr, SB_PLAIN, 28.0f))
    TestFromBases();
  pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(0, P(28.0f)));
  if (!g_testSaid.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y + P(6.0f), C(TK_BAD), g_testSaid);
  } else if (pszPlaying != nullptr) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y + P(6.0f), C(TK_INK_OFF),
      std::string("playing ") + pszPlaying + " on channel " + std::to_string(nPort));
  } else {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y + P(6.0f), C(TK_INK3),
      "Play it saves the mod and starts the mode's own game, with these settings and this seed. A mode that "
      "starts a game part way through says so with a setting of its own.");
  }
}

// ---- the commands ---------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::string ItemsJson(const PortJson &list) {
  std::string s;
  for (size_t i = 0; i < list.size(); ++i)
    s += std::string(s.empty() ? "" : ",") + PortJsonWrite(list.At(i));
  return "[" + s + "]";
}

bool CmdMode(const PortJson &, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const PortJson &m = Mode();
  const PortJson &dir = StudioDoc("types")["game"]["director"];
  const PortJson &players = m["players"];
  *pAnswer = "\"is_mode\":" + std::string(IsMode() ? "true" : "false") + ",\"line\":" + Q(m["line"].Str()) +
             ",\"about\":" + Q(m["about"].Str()) + ",\"needs\":" + Q(m["needs"].Str()) +
             ",\"sim\":" + std::string(m["sim"].kind == PortJson::NONE || m["sim"].b ? "true" : "false") +
             ",\"enemy_player\":" +
             std::string(m["enemy_player"].kind == PortJson::BOOL && m["enemy_player"].b ? "true" : "false") +
             ",\"players\":[" + Fmt(players.size() > 0 ? players.At(0).Num(1) : 1) + "," +
             Fmt(players.size() > 1 ? players.At(1).Num(8) : 8) + "]" + ",\"game_type\":{\"name\":" +
             Q(m["game_type"]["name"].Str()) + ",\"tag\":" + Q(m["game_type"]["tag"].Str()) + ",\"plays\":" +
             Q(m["game_type"]["plays"].Str()) + "}" + ",\"preview\":" + Q(m["preview"].Str()) +
             ",\"director\":{\"call\":" + Q(dir["call"].Str()) + ",\"every\":" + Fmt(dir["every"].Num(0)) + "}" +
             ",\"settings\":" + ItemsJson(m["settings"]) + ",\"presets\":" + ItemsJson(m["presets"]);
  return true;
}

// A field of the mode block, or the director, which is types.json's "game" block and not the
// mode's: director and director_every go there, and the rest into mod.json.
bool CmdModeSet(const PortJson &j, std::string *pAnswer) {
  const std::string field = j["field"].Str();
  if (field == "director" || field == "director_every") {
    std::string why;
    const char *pszKey = field == "director" ? "call" : "every";
    StudioGroupBegin(Sender(j), "set the mode's director");
    const bool bOk = StudioPut("types", {"game", "director", pszKey}, j["value"], Sender(j),
                               "set the mode's director", &why);
    StudioGroupEnd();
    if (!bOk) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"set\":" + Q(field) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  if (field.empty()) {
    *pAnswer = "field is one of line, about, needs, sim, enemy_player, players, notes, preview, "
               "game_type, director, director_every";
    return false;
  }
  std::string why;
  if (!Put({field}, j["value"], "set the mode's " + field, Sender(j), &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"set\":" + Q(field) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// A setting made, renamed, moved or changed. new makes one of the kind given.
bool CmdModeSetting(const PortJson &j, std::string *pAnswer) {
  const std::string id = j["id"].Str(), sender = Sender(j);
  if (id.empty()) {
    *pAnswer = "id is the setting's id";
    return false;
  }
  const PortJson &settings = Mode()["settings"];
  const int nAt = At(settings, id);
  std::string why;
  if (j["new"].b) {
    if (nAt >= 0) {
      *pAnswer = "the mode already has a setting " + id;
      return false;
    }
    const std::string kind = j["kind"].Str("choice");
    const std::string at = std::to_string(settings.size());
    PortJson rec;
    rec.kind = PortJson::OBJECT;
    StudioGroupBegin(sender, "added the setting " + id);
    bool bOk = Put({"settings", at}, rec, "added the setting " + id, sender, &why);
    bOk = bOk && Put({"settings", at, "id"}, Str(id), "added the setting " + id, sender, &why);
    bOk = bOk && Put({"settings", at, "kind"}, Str(kind), "added the setting " + id, sender, &why);
    bOk = bOk && Put({"settings", at, "name"}, Str(j["name"].Str(id.c_str())), "added the setting " + id, sender, &why);
    StudioGroupEnd();
    if (!bOk) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"added\":" + Q(id) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  if (nAt < 0) {
    *pAnswer = "the mode has no setting " + id;
    return false;
  }
  const std::string field = j["field"].Str();
  if (field.empty()) {
    *pAnswer = "\"setting\":" + PortJsonWrite(settings.At(nAt));
    return true;
  }
  if (!Put({"settings", std::to_string(nAt), field}, j["value"], "set " + id + "'s " + field, sender, &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"set\":" + Q(field) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool CmdModeSettingDelete(const PortJson &j, std::string *pAnswer) {
  const std::string id = j["id"].Str();
  const int nAt = At(Mode()["settings"], id);
  if (nAt < 0) {
    *pAnswer = "the mode has no setting " + id;
    return false;
  }
  std::string why;
  if (!Put({"settings", std::to_string(nAt)}, PortJson(), "took away the setting " + id, Sender(j), &why)) {
    *pAnswer = why;
    return false;
  }
  if (g_setting == id)
    g_setting.clear();
  *pAnswer = "\"deleted\":" + Q(id) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool CmdModePreset(const PortJson &j, std::string *pAnswer) {
  const std::string id = j["id"].Str(), sender = Sender(j);
  if (id.empty()) {
    *pAnswer = "id is the preset's id";
    return false;
  }
  const PortJson &presets = Mode()["presets"];
  const int nAt = At(presets, id);
  std::string why;
  if (j["new"].b) {
    if (nAt >= 0) {
      *pAnswer = "the mode already has a preset " + id;
      return false;
    }
    const std::string at = std::to_string(presets.size());
    PortJson rec;
    rec.kind = PortJson::OBJECT;
    StudioGroupBegin(sender, "added the preset " + id);
    bool bOk = Put({"presets", at}, rec, "added the preset " + id, sender, &why);
    bOk = bOk && Put({"presets", at, "id"}, Str(id), "added the preset " + id, sender, &why);
    bOk = bOk && Put({"presets", at, "name"}, Str(j["name"].Str(id.c_str())), "added the preset " + id, sender, &why);
    if (bOk && j["values"].kind == PortJson::OBJECT)
      bOk = Put({"presets", at, "values"}, j["values"], "added the preset " + id, sender, &why);
    StudioGroupEnd();
    if (!bOk) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"added\":" + Q(id) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  if (nAt < 0) {
    *pAnswer = "the mode has no preset " + id;
    return false;
  }
  if (j["values"].kind == PortJson::OBJECT) {
    if (!Put({"presets", std::to_string(nAt), "values"}, j["values"], "set " + id + "'s values", sender, &why)) {
      *pAnswer = why;
      return false;
    }
    *pAnswer = "\"set\":\"values\",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  const std::string field = j["field"].Str();
  if (field.empty()) {
    *pAnswer = "\"preset\":" + PortJsonWrite(presets.At(nAt));
    return true;
  }
  if (!Put({"presets", std::to_string(nAt), field}, j["value"], "set " + id + "'s " + field, sender, &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"set\":" + Q(field) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool CmdModePresetDelete(const PortJson &j, std::string *pAnswer) {
  const std::string id = j["id"].Str();
  const int nAt = At(Mode()["presets"], id);
  if (nAt < 0) {
    *pAnswer = "the mode has no preset " + id;
    return false;
  }
  std::string why;
  if (!Put({"presets", std::to_string(nAt)}, PortJson(), "took away the preset " + id, Sender(j), &why)) {
    *pAnswer = why;
    return false;
  }
  if (g_preset == id)
    g_preset.clear();
  *pAnswer = "\"deleted\":" + Q(id) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool CmdModeRoll(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const unsigned dwSeed = j["seed"].kind == PortJson::NUMBER ? static_cast<unsigned>(j["seed"].Num()) : DrawnSeed();
  const std::string map = j["map"].Str(g_rollMap.c_str());
  const std::string race = j["race"].Str(g_rollRace.c_str());
  const std::string settings = j["settings"].kind == PortJson::OBJECT ? PortJsonWrite(j["settings"]) : BasesJson();
  g_rollMap = map;
  g_rollRace = race;
  snprintf(g_szSeed, sizeof g_szSeed, "%u", dwSeed);
  std::string why;
  if (!RunRoll(dwSeed, map, settings, race, &why)) {
    *pAnswer = why;
    return false;
  }
  std::string records;
  for (const PortJsonMember &m : g_roll.types.members)
    records += std::string(records.empty() ? "" : ",") + Q(m.first) + ":" +
               std::to_string(m.second.kind == PortJson::OBJECT ? m.second.members.size() : m.second.size());
  *pAnswer = "\"seed\":" + std::to_string(dwSeed) + ",\"map\":" + Q(map) + ",\"made\":{" + records +
             "},\"run\":" + PortJsonWrite(g_roll.run);
  return true;
}

bool CmdModeAi(const PortJson &, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string out;
  for (const AiCall &c : AiCalls())
    out += std::string(out.empty() ? "" : ",") + "{\"kind\":" + Q(c.kind) + ",\"of\":" + Q(c.of) + ",\"casts\":" +
           Q(c.what) + ",\"call\":" + Q(c.call) + ",\"every\":" + Fmt(c.dEvery) + "}";
  // and what they chose in the game Test started, newest last, as the view draws them
  StudioTraceWant(true);
  ReadRows();
  std::string chose;
  const size_t nFrom = g_rows.size() > 40 ? g_rows.size() - 40 : 0;
  for (size_t i = nFrom; i < g_rows.size(); ++i) {
    const AiRow &r = g_rows[i];
    chose += std::string(chose.empty() ? "" : ",") + "{\"frame\":" + std::to_string(r.dwFrame) + ",\"kind\":" +
             Q(r.kind) + ",\"fn\":" + Q(r.fn) + ",\"unit\":" + Q(UnitWord(r.nType)) + ",\"chose\":" +
             Q(ChoseWord(r)) + "}";
  }
  *pAnswer = "\"calls\":[" + out + "],\"playing\":" + (StudioPlayingWith(nullptr) != nullptr ? "true" : "false") +
             ",\"kept\":" + std::to_string(g_rows.size()) + ",\"chose\":[" + chose + "]";
  return true;
}

bool CmdModeTest(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  if (g_test.empty())
    TestFromBases();
  if (j["settings"].kind == PortJson::OBJECT) {
    TestFromBases();
    for (const PortJsonMember &m : j["settings"].members)
      g_test[m.first] = PortJsonWrite(m.second);
  }
  if (!j["map"].Str().empty())
    g_testMap = j["map"].Str();
  if (!j["race"].Str().empty())
    g_testRace = j["race"].Str();
  if (j["computers"].kind == PortJson::NUMBER)
    g_nTestComputers = static_cast<int>(j["computers"].Num(1));
  if (j["seed"].kind == PortJson::NUMBER)
    snprintf(g_szTestSeed, sizeof g_szTestSeed, "%u", static_cast<unsigned>(j["seed"].Num()));
  g_nView = VIEW_TEST;
  StudioWorkspaceSet(14);
  std::string why;
  if (!TestPlay(&why)) {
    g_testSaid = why;
    *pAnswer = why;
    return false;
  }
  g_testSaid.clear();
  int nPort = 0;
  const char *pszMap = StudioPlayingWith(&nPort);
  *pAnswer = "\"playing\":" + Q(pszMap != nullptr ? pszMap : "") + ",\"port\":" + std::to_string(nPort) +
             ",\"settings\":" + TestSettingsJson() + ",\"as\":" + Q(TestSettingsList());
  return true;
}

bool CmdModesShow(const PortJson &j, std::string *pAnswer) {
  const std::string view = j["view"].Str();
  for (int i = 0; i < VIEWS; ++i)
    if (view == Lower(kViews[i]))
      g_nView = i;
  if (!j["setting"].Str().empty()) {
    g_setting = j["setting"].Str();
    g_nView = VIEW_SETTINGS;
  }
  if (!j["preset"].Str().empty()) {
    g_preset = j["preset"].Str();
    g_nView = VIEW_PRESETS;
  }
  StudioWorkspaceSet(14);
  *pAnswer = "\"view\":" + Q(Lower(kViews[g_nView]));
  return true;
}

}  // namespace

bool StudioModeDecl(PortModeDecl *pOut, std::string *pWhy) {
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  if (!IsMode()) {
    *pWhy = "the mod is not a game mode";
    return false;
  }
  const PortJson &m = Mode();
  PortModeDecl d;
  d.id = Mod()["id"].Str(StudioModId().c_str());
  d.name = Mod()["name"].Str(d.id.c_str());
  d.version = Mod()["version"].Str();
  d.nMod = ModPlace(d.id);
  d.folder = StudioModFolder();
  d.line = m["line"].Str(Mod()["line"].Str().c_str());
  d.about = m["about"].Str();
  const PortJson &type = m["game_type"];
  d.typeName = type["name"].Str(d.name.c_str());
  d.typeTag = type["tag"].Str(d.name.c_str());
  d.typePlays = PlaysNumber(type["plays"].Str());
  d.needs = m["needs"].Str("any race");
  d.sim = m["sim"].kind != PortJson::BOOL || m["sim"].b;
  d.enemyPlayer = m["enemy_player"].kind == PortJson::BOOL && m["enemy_player"].b;
  const PortJson &players = m["players"];
  if (players.size() == 2) {
    d.playersMin = static_cast<int>(players.At(0).Num(1));
    d.playersMax = static_cast<int>(players.At(1).Num(8));
  }
  if (d.playersMin < 1)
    d.playersMin = 1;
  if (d.playersMax < d.playersMin)
    d.playersMax = d.playersMin;
  for (size_t i = 0; i < m["notes"].size(); ++i)
    d.notes.push_back(m["notes"].At(i).Str());
  d.preview = m["preview"].Str();
  const PortJson &settings = m["settings"];
  for (size_t i = 0; i < settings.size(); ++i) {
    PortModeSetting s;
    if (!DeclSetting(settings.At(i), &s, pWhy))
      return false;
    for (const PortModeSetting &o : d.settings)
      if (o.id == s.id) {
        *pWhy = "two settings use the id " + s.id;
        return false;
      }
    d.settings.push_back(s);
  }
  const PortJson &presets = m["presets"];
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
  *pOut = d;
  return true;
}

std::string StudioModeBases(void) {
  return BasesJson();
}

const std::vector<StudioModeMap> &StudioModeMaps(void) {
  return Maps();
}

void StudioModesWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float hh = P(44.0f);
  d->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + hh), C(TK_BAR));
  d->AddLine(ImVec2(a.x, a.y + hh - 0.5f), ImVec2(b.x, a.y + hh - 0.5f), C(TK_LINE));
  float x = a.x + P(14.0f);
  x += Wide(d, 13.0f, x, Mid(a.y, hh, 13.0f), C(TK_HEAD), "Mode", 0.06f) + P(16.0f);
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(20.0f), a.y + hh + P(20.0f), C(TK_INK3),
      "Open a mod to make it a game mode.");
    return;
  }
  if (!IsMode()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(20.0f), a.y + hh + P(20.0f), C(TK_INK2),
      "This mod is not a game mode. A game mode is a mod whose kind is mode: it brings settings of its own, "
      "and its scripts run the game from its first frame.");
    ImGui::SetCursorScreenPos(ImVec2(a.x + P(20.0f), a.y + hh + P(50.0f)));
    if (StudioButton("make", "Make this mod a game mode", "plus", SB_GOOD, 28.0f)) {
      std::string why;
      StudioGroupBegin("you", "made the mod a game mode");
      PortJson block;
      block.kind = PortJson::OBJECT;
      StudioPut("mod", {"kind"}, Str("mode"), "you", "made the mod a game mode", &why);
      StudioPut("mod", {"mode"}, block, "you", "made the mod a game mode", &why);
      Put({"line"}, Str(""), "made the mod a game mode", "you", &why);
      StudioGroupEnd();
    }
    return;
  }
  // the views
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + P(9.0f)));
  const int nPick = StudioSeg("ws_modeviews", kViews, VIEWS, g_nView);
  if (nPick != g_nView)
    g_nView = nPick;
  const float fW = P(260.0f);
  ImGui::SetCursorScreenPos(ImVec2(a.x + P(20.0f), a.y + hh + P(16.0f)));
  ImGui::BeginChild("modebody", ImVec2(b.x - a.x - P(28.0f), b.y - a.y - hh - P(24.0f)), false);
  if (g_nView == VIEW_ABOUT) {
    AboutView(fW);
  } else if (g_nView == VIEW_SEEN) {
    StudioModeSeenView(fW);
  } else if (g_nView == VIEW_PREVIEW) {
    StudioModePreviewView(fW);
  } else if (g_nView == VIEW_ROLL) {
    RollView(fW);
  } else if (g_nView == VIEW_AI) {
    if (!g_bRowsOn) {
      StudioTraceWant(true);
      g_bRowsOn = true;
    }
    AiView(fW);
  } else if (g_nView == VIEW_TEST) {
    TestView(fW);
  } else {
    const bool bSettings = g_nView == VIEW_SETTINGS;
    const PortJson &list = bSettings ? Mode()["settings"] : Mode()["presets"];
    std::string &sel = bSettings ? g_setting : g_preset;
    if (At(list, sel) < 0 && list.size() > 0)
      sel = list.At(0)["id"].Str();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ListColumn(bSettings ? "A setting" : "A preset", list, &sel, b.y - at.y);
    const int nAt = At(list, sel);
    ImGui::SetCursorScreenPos(ImVec2(at.x + ListW() + P(10.0f), at.y));
    ImGui::BeginChild("modeform", ImVec2(0, 0), false);
    if (nAt >= 0) {
      if (bSettings)
        SettingForm(nAt, fW);
      else
        PresetForm(nAt, fW);
    } else {
      T(d, STUDIO_FONT_TEXT, kStudioFsS, ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y, C(TK_INK3),
        bSettings ? "The mode has no settings yet." : "The mode has no presets yet.");
    }
    ImGui::EndChild();
  }
  ImGui::EndChild();
  if (g_bRowsOn && g_nView != VIEW_AI) {
    StudioTraceWant(false);  // the game keeps the rows; only the asking stops
    g_bRowsOn = false;
  }
}

void StudioModesInit(void) {
  PortControlCommand("studio_mode", CmdMode);
  PortControlCommand("studio_mode_set", CmdModeSet);
  PortControlCommand("studio_mode_setting", CmdModeSetting);
  PortControlCommand("studio_mode_setting_delete", CmdModeSettingDelete);
  PortControlCommand("studio_mode_preset", CmdModePreset);
  PortControlCommand("studio_mode_preset_delete", CmdModePresetDelete);
  PortControlCommand("studio_mode_roll", CmdModeRoll);
  PortControlCommand("studio_mode_ai", CmdModeAi);
  PortControlCommand("studio_mode_test", CmdModeTest);
  PortControlCommand("studio_modes_show", CmdModesShow);
}
