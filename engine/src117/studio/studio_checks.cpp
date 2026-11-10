// studio_checks.cpp -- the Checks workspace: the mod checked twice. The studio's own checks run as the
// modder edits: a record that does not resolve, a field that names a record no table has, a file a
// record names that the data folder does not have, a dependency missing or too old, a look or a
// console setting the game would pass over. The game's check is Hyperion.exe --mod-check on the
// saved mod (docs/mod-data.md, "Checking a mod"), run on a thread of its own so the studio goes on;
// its report's lines are shown as they are. Each problem opens the record and field it is about.
//
// Commands of its own:
//   studio_check {run?, save?}          the studio's problems and the game's check as it stands; run
//                                       starts the game's check (save first saves the mod), and the
//                                       answer's game.state is "running" until it ends
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "../design/design_engine.h"  // DesignCall
#include "../port/data/port_mods.h"
#include "../port/data/port_modlua.h"  // ModLuaField
#include "../port/tools/port_control.h"
#include "studio_draw.h"
#include "studio_media.h"

namespace {

using namespace studio_draw;

enum { LV_ERROR, LV_WARNING, LV_NOTE };
const char *const kLevelNames[] = {"error", "warning", "note"};

// A problem, and what it is about: a record and its field, or a document ("looks", "hud", "mod").
struct Problem {
  int nLevel = LV_WARNING;
  std::string text;
  int t = -1;
  std::string name;
  std::string field;
  std::string doc;
  std::string look;  // a look's name, for a looks.json problem
};

// ---- the studio's own checks ------------------------------------------------------------------------

bool HasEnd(const std::string &s) {
  const size_t slash = s.find_last_of("/\\");
  return s.find('.', slash == std::string::npos ? 0 : slash) != std::string::npos;
}

// Fields a record may give that are no column, read where they are used (port_moddata.cpp).
bool KnownNoColumn(int t, const std::string &key) {
  static const char *const kAny[] = {"base", "edit", "name", "comment", "requires", "use_requires"};
  for (const char *psz : kAny)
    if (key == psz)
      return true;
  if (t == ST_UNITS)
    for (const char *psz : {"looks", "buttons", "icon", "wireframe", "transport_wireframe"})
      if (key == psz)
        return true;
  if (t == ST_IMAGES)
    for (const char *psz : {"grp", "team", "frame", "origin", "scale", "frames", "directions"})
      if (key == psz)
        return true;
  if (t == ST_PORTRAITS && key == "idle_chances")
    return true;
  if (ModLuaField(t, key))  // the scripts' hooks
    return true;
  return false;
}

int RefOf(const StudioField *pf) {
  if (pf == nullptr)
    return -1;
  if (pf->nRef >= 0)
    return pf->nRef;
  if (pf->editor.compare(0, 5, "link:") == 0)
    return StudioTableOf(pf->editor.substr(5));
  return -1;
}

void CheckRecord(int t, const StudioRecord &r, std::vector<Problem> *pOut) {
  auto add = [&](int nLevel, const std::string &field, const std::string &text) {
    Problem p;
    p.nLevel = nLevel;
    p.text = text;
    p.t = t;
    p.name = r.name;
    p.field = field;
    pOut->push_back(p);
  };
  const std::string where = std::string(StudioTableName(t)) + " " + r.name;
  std::vector<StudioValue> row;
  std::string why;
  if (!StudioRow(t, r.name, &row, &why)) {
    // the field the loader's words name: "units trooper.ready_sound names ..."
    std::string field;
    if (why.compare(0, where.size() + 1, where + ".") == 0) {
      const size_t e = why.find_first_of(" :,", where.size() + 1);
      field = why.substr(where.size() + 1, e == std::string::npos ? std::string::npos : e - where.size() - 1);
    }
    add(LV_ERROR, field, why.empty() ? where + " does not resolve" : why);
    return;
  }
  const PortJson &j = StudioRecordJson(t, r.name);
  for (const PortJsonMember &m : j.members) {
    const StudioField *pf = StudioFieldOf(t, m.first);
    if (pf == nullptr && !KnownNoColumn(t, m.first)) {
      add(LV_ERROR, m.first, where + "." + m.first + " is no field of " + StudioTableName(t) + ": the load stops at it");
      continue;
    }
    const int nRef = RefOf(pf);
    if (nRef >= 0 && m.second.kind == PortJson::STRING) {
      StudioRecord to;
      const bool bThere = StudioRecordOf(nRef, m.second.s, &to) || StudioClassicRow(nRef, m.second.s) >= 0;
      // a unit's looks and buttons are passed over when they name nothing; a column stops the load
      if (!bThere)
        add(m.first == "looks" || m.first == "buttons" ? LV_WARNING : LV_ERROR, m.first,
            where + "." + m.first + " names no " + StudioTableName(nRef) + " " + m.second.s);
    }
    if (m.second.kind != PortJson::STRING || m.second.s.empty())
      continue;
    const std::string &v = m.second.s;
    if (t == ST_SOUNDS && m.first == "file") {
      StudioSoundInfo info;
      if (!StudioFileExists(v))
        add(LV_WARNING, m.first, where + ".file names " + v + ", which the data folder does not have: it plays nothing");
      else if (!StudioSoundInfoOf(r.name, &info))
        add(LV_ERROR, m.first, where + ".file: " + v + " does not decode: a sound is a WAV, Ogg (Vorbis or Opus), MP3 or FLAC file");
    }
    if (t == ST_PORTRAITS && (m.first == "idle" || m.first == "talking") && StudioFirstVideo(v + "0").empty())
      add(LV_WARNING, m.first, where + "." + m.first + ": the data folder has no " + v + "0 video (.webm, .mkv, .mp4, .m4v, .mov or .smk)");
    if (t == ST_IMAGES && m.first == "grp" && !StudioFileExists(v))
      add(LV_WARNING, m.first, where + ".grp names " + v + ", which the data folder does not have: the image keeps its base's frames");
    if (t == ST_UNITS && (m.first == "icon" || m.first == "wireframe" || m.first == "transport_wireframe")) {
      if (HasEnd(v) && !StudioFileExists(v))
        add(LV_WARNING, m.first, where + "." + m.first + " names " + v + ", which the data folder does not have");
      else if (!HasEnd(v) && StudioClassicRow(ST_UNITS, v) < 0) {
        StudioRecord u;
        if (!StudioRecordOf(ST_UNITS, v, &u))
          add(LV_ERROR, m.first, where + "." + m.first + " is a PNG or GRP file in the data folder, or a unit: no unit " + v);
      }
    }
  }
  // a unit's voice lines, each a range from its first to its last
  if (t == ST_UNITS) {
    static const char *const kPairs[][2] = {{"what_sound_first", "what_sound_last"},
                                            {"pissed_sound_first", "pissed_sound_last"},
                                            {"yes_sound_first", "yes_sound_last"}};
    for (const auto &pr : kPairs)
      if (j[pr[0]].kind != PortJson::NONE || j[pr[1]].kind != PortJson::NONE) {
        const long long a = StudioNumber(ST_UNITS, r.name, pr[0]), b = StudioNumber(ST_UNITS, r.name, pr[1]);
        if (a > 0 && b > 0 && b < a)
          add(LV_WARNING, pr[1], where + "." + pr[1] + " is a row before " + pr[0] + "'s: the line runs backwards");
        else if (b - a > 32)
          add(LV_WARNING, pr[1], where + ": its line runs from row " + std::to_string(a) + " to " + std::to_string(b) +
                                     ", sounds that are not its own among them");
      }
  }
}

bool HexColour(const PortJson &v) {
  if (v.kind != PortJson::STRING || v.s.size() != 7 || v.s[0] != '#')
    return false;
  for (size_t i = 1; i < 7; ++i)
    if (!isxdigit(static_cast<unsigned char>(v.s[i])))
      return false;
  return true;
}

void CheckLooks(std::vector<Problem> *pOut) {
  const PortJson &looks = StudioDoc("looks")["looks"];
  std::set<std::string> names;
  auto add = [&](int nLevel, const std::string &look, const std::string &text) {
    Problem p;
    p.nLevel = nLevel;
    p.text = text;
    p.doc = "looks";
    p.look = look;
    pOut->push_back(p);
  };
  if (StudioDoc("looks").kind == PortJson::OBJECT && StudioDoc("looks")["looks"].kind != PortJson::NONE && looks.kind != PortJson::ARRAY)
    add(LV_ERROR, std::string(), "looks.json's looks is no list: the game reads none of it");
  for (const PortJson &l : looks.items) {
    const std::string name = l["name"].Str();
    const std::string at = "looks.json's " + (name.empty() ? std::string("look without a name") : name);
    if (name.empty())
      add(LV_ERROR, name, at + ": a look needs a name");
    else if (!names.insert(name).second)
      add(LV_WARNING, name, at + " is there twice: the second changes the first");
    if (l["rule"].kind != PortJson::NONE && l["rule"].Str() != "blues" && l["rule"].Str() != "greens" && l["rule"].Str() != "all")
      add(LV_WARNING, name, at + ": rule is \"blues\", \"greens\" or \"all\" in the game (the design engine reads more)");
    if (l["material"].kind != PortJson::NONE && l["material"].Str() != "crystal" && l["material"].Str() != "gas")
      add(LV_WARNING, name, at + ": material is \"crystal\" or \"gas\" in the game");
    if (l["ramp"].kind != PortJson::NONE) {
      bool bOk = l["ramp"].kind == PortJson::ARRAY && l["ramp"].size() == 6;
      for (const PortJson &c : l["ramp"].items)
        bOk = bOk && HexColour(c);
      if (!bOk)
        add(LV_ERROR, name, at + ": the ramp is six colours, \"#rrggbb\" each; the game passes the look over");
    }
    if (l["light"]["color"].kind != PortJson::NONE && !HexColour(l["light"]["color"]))
      add(LV_ERROR, name, at + ": light.color is \"#rrggbb\"");
    // units and images by number or by a record's name; one the game cannot find makes it pass the look over
    for (int t : {ST_UNITS, ST_IMAGES}) {
      const char *pszKey = t == ST_UNITS ? "units" : "images";
      for (const PortJson &u : l[pszKey].items) {
        StudioRecord rec;
        if (u.kind == PortJson::STRING) {
          if (!StudioRecordOf(t, u.s, &rec))
            add(LV_ERROR, name, at + "'s " + pszKey + " name " + u.s + ", which no record has: the game passes the look over");
          continue;
        }
        const long long n = static_cast<long long>(u.Num(-1));
        const std::string row = StudioRefName(t, n);
        if (n < 0 || (!StudioRecordOf(t, row, &rec) && StudioClassicRow(t, row) < 0))
          add(LV_WARNING, name, at + "'s " + pszKey + " give row " + std::to_string(n) + ", which no record has");
      }
    }
  }
}

void CheckHud(std::vector<Problem> *pOut) {
  const PortJson &hud = StudioDoc("hud");
  auto add = [&](int nLevel, const std::string &text) {
    Problem p;
    p.nLevel = nLevel;
    p.text = text;
    p.doc = "hud";
    pOut->push_back(p);
  };
  for (const PortJsonMember &m : hud["minimap"]["dots"].members) {
    const double d = m.second.Num(-1);
    if (m.second.kind != PortJson::NUMBER || d < 0 || d > 255 || d != floor(d))
      add(LV_WARNING, "hud.json's minimap.dots." + m.first + " is a palette entry, 0 to 255: the dots keep the game's colour");
    else if (m.first != "own" && m.first != "allied" && m.first != "enemy" && m.first != "neutral")
      add(LV_WARNING, "hud.json's minimap.dots." + m.first + " is no relation: own, allied, enemy or neutral");
  }
  for (const PortJsonMember &m : hud["console"]["art"].members) {
    if (m.first != "zerg" && m.first != "terran" && m.first != "protoss" && m.first != "replay")
      add(LV_WARNING, "hud.json's console.art." + m.first + " is no console: zerg, terran, protoss or replay");
    else if (!StudioFileExists(m.second.Str()))
      add(LV_WARNING, "hud.json's console.art." + m.first + " names " + m.second.Str() + ", which the data folder does not have");
  }
}

void CheckDepends(std::vector<Problem> *pOut) {
  for (const PortJson &d : StudioDoc("mod")["depends"].items) {
    const std::string id = d.kind == PortJson::STRING ? d.s : d["id"].Str();
    const std::string want = d.kind == PortJson::STRING ? std::string() : d["version"].Str();
    const PortMod *pm = nullptr;
    for (const PortMod &m : PortMods())
      if (m.id == id && !m.bBuiltIn)
        pm = &m;
    Problem p;
    p.doc = "mod";
    p.nLevel = LV_ERROR;
    if (id == StudioModId())
      p.text = "mod.json's depends names the mod itself";
    else if (pm == nullptr)
      p.text = "mod.json's depends names " + id + ", which is not installed: the load stops";
    else if (pm->data.empty())
      p.text = "mod.json's depends names " + id + ", which brings no data", p.nLevel = LV_WARNING;
    else
      continue;
    pOut->push_back(p);
  }
}

std::vector<Problem> g_studio;
unsigned g_nStudioAt = ~0u;
double g_dStudioTime;

const std::vector<Problem> &StudioProblems(void) {
  // looked at again when the mod changes, at most a few times a second
  if (g_nStudioAt != StudioChange() && (ImGui::GetTime() - g_dStudioTime > 0.3 || g_nStudioAt == ~0u)) {
    g_nStudioAt = StudioChange();
    g_dStudioTime = ImGui::GetTime();
    g_studio.clear();
    if (StudioIsOpen()) {
      for (int t = 0; t < ST_TABLES; ++t)
        for (const StudioRecord &r : StudioRecords(t))
          if (r.kind == SK_OWN || r.kind == SK_EDIT)
            CheckRecord(t, r, &g_studio);
      CheckLooks(&g_studio);
      CheckHud(&g_studio);
      CheckDepends(&g_studio);
      std::stable_sort(g_studio.begin(), g_studio.end(), [](const Problem &a, const Problem &b) { return a.nLevel < b.nLevel; });
    }
  }
  return g_studio;
}

// ---- the game's check -----------------------------------------------------------------------------------

struct GameCheck {
  std::mutex lock;
  int nState = 0;  // 0 not run, 1 running, 2 done
  int nExit = -1;
  std::vector<std::string> lines;
  std::string why;        // why it could not run
  std::string mod;        // the mod it checked
  long long nStarted = 0; // GetTickCount64 when it started
  long long nTook = 0;
  int nUnsavedThen = 0;
};
GameCheck g_game;

std::string Folder(std::string s) {
  while (!s.empty() && (s.back() == '\\' || s.back() == '/'))
    s.pop_back();
  const size_t cut = s.find_last_of("\\/");
  return cut == std::string::npos ? std::string() : s.substr(0, cut);
}

std::string LeafOf(std::string s) {
  while (!s.empty() && (s.back() == '\\' || s.back() == '/'))
    s.pop_back();
  const size_t cut = s.find_last_of("\\/");
  return cut == std::string::npos ? s : s.substr(cut + 1);
}

bool FileThere(const std::string &path) {
  const DWORD a = GetFileAttributesA(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Hyperion.exe: beside the studio, or in the build's x64 folder above it.
std::string FindHyperion(void) {
  char sz[MAX_PATH];
  GetModuleFileNameA(nullptr, sz, MAX_PATH);
  std::string dir = Folder(sz);
  for (int i = 0; i < 6 && !dir.empty(); ++i, dir = Folder(dir))
    for (const char *psz : {"\\Hyperion.exe", "\\x64\\Hyperion.exe", "\\build\\x64\\Hyperion.exe", "\\src117\\build\\x64\\Hyperion.exe",
                            "\\engine\\src117\\build\\x64\\Hyperion.exe"})
      if (FileThere(dir + psz))
        return dir + psz;
  return std::string();
}

void RunThread(std::string cmd, std::string cwd, std::string report) {
  // the game's own variables for a run with no window and no questions, on top of the studio's
  std::string env;
  if (LPCH pEnv = GetEnvironmentStringsA()) {
    for (const char *p = pEnv; *p; p += strlen(p) + 1)
      if (_strnicmp(p, "HYPERION_QUIET=", 15) != 0 && _strnicmp(p, "HYPERION_WINDOW=", 16) != 0)
        env.append(p, strlen(p) + 1);
    FreeEnvironmentStringsA(pEnv);
  }
  env.append("HYPERION_QUIET=1", 17);
  env.append("HYPERION_WINDOW=background", 27);
  env.push_back('\0');
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  std::vector<char> line(cmd.begin(), cmd.end());
  line.push_back('\0');
  DeleteFileA(report.c_str());
  int nExit = -1;
  std::string why;
  if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, &env[0], cwd.c_str(), &si, &pi)) {
    why = "Hyperion.exe did not start (error " + std::to_string(GetLastError()) + ")";
  } else {
    if (WaitForSingleObject(pi.hProcess, 600000) == WAIT_TIMEOUT) {
      TerminateProcess(pi.hProcess, 3);
      why = "the check ran past ten minutes and was stopped";
    }
    DWORD dw = 0;
    GetExitCodeProcess(pi.hProcess, &dw);
    nExit = static_cast<int>(dw);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  std::vector<std::string> lines;
  if (FILE *f = fopen(report.c_str(), "rb")) {
    char buf[4096];
    while (fgets(buf, sizeof buf, f)) {
      std::string s = buf;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
      if (!s.empty())
        lines.push_back(s);
    }
    fclose(f);
  } else if (why.empty()) {
    why = "Hyperion.exe wrote no report (it ended with " + std::to_string(nExit) + ")";
  }
  std::lock_guard<std::mutex> g(g_game.lock);
  g_game.lines = lines;
  g_game.nExit = nExit;
  g_game.why = why;
  g_game.nTook = static_cast<long long>(GetTickCount64()) - g_game.nStarted;
  g_game.nState = 2;
}

// The game's check started on the saved mod; false with why when it cannot be.
bool StartGameCheck(std::string *pWhy) {
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  {
    std::lock_guard<std::mutex> g(g_game.lock);
    if (g_game.nState == 1) {
      *pWhy = "the game's check is running already";
      return false;
    }
  }
  const std::string exe = FindHyperion();
  if (exe.empty()) {
    *pWhy = "Hyperion.exe is not beside the studio, nor in the build's x64 folder";
    return false;
  }
  // the check reads the installed mods, from the folder that holds mods\ as its data folder
  const std::string parent = Folder(StudioModFolder());
  if (_stricmp(LeafOf(parent).c_str(), "mods") != 0) {
    *pWhy = "the game's check reads the mods in a mods folder, and " + StudioModFolder() + " is not in one";
    return false;
  }
  const std::string root = Folder(parent);
  char sz[MAX_PATH];
  GetModuleFileNameA(nullptr, sz, MAX_PATH);
  const std::string game = Folder(sz);
  const std::string studio = StudioModFolder() + ".studio";
  CreateDirectoryA(studio.c_str(), nullptr);
  const std::string report = studio + "\\modcheck.txt";
  std::string cmd = "\"" + exe + "\" --mod-check " + StudioModId() + " \"" + report + "\" \"--data=" + root + "\"";
  if (FileThere(game + "\\StarDat.mpq"))
    cmd += " --install classic --install-folder \"" + game + "\"";
  {
    std::lock_guard<std::mutex> g(g_game.lock);
    g_game.nState = 1;
    g_game.lines.clear();
    g_game.why.clear();
    g_game.nExit = -1;
    g_game.mod = StudioModId();
    g_game.nStarted = static_cast<long long>(GetTickCount64());
    g_game.nUnsavedThen = StudioUnsaved();
  }
  std::thread(RunThread, cmd, root, report).detach();
  StudioLog("info", "the game's check: " + cmd);
  return true;
}

// What a line of the report is about: "<table> <name>.<field>" where it names a record, else the
// record whose file it names.
Problem ProblemOf(const std::string &line) {
  Problem p;
  std::string text = line;
  for (int k = 0; k < 3; ++k) {
    const std::string head = std::string(kLevelNames[k]) + ": ";
    if (text.compare(0, head.size(), head) == 0) {
      p.nLevel = k;
      text = text.substr(head.size());
    }
  }
  p.text = text;
  for (int t = 0; t < ST_TABLES && p.t < 0; ++t) {
    const std::string word = std::string(StudioTableName(t)) + " ";
    for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
      if (at > 0 && (isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_'))
        continue;
      size_t e = at + word.size();
      while (e < text.size() && (isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_' || text[e] == '-'))
        ++e;
      const std::string name = text.substr(at + word.size(), e - at - word.size());
      StudioRecord r;
      if (name.empty() || !StudioRecordOf(t, name, &r))
        continue;
      p.t = t;
      p.name = name;
      if (e < text.size() && text[e] == '.') {
        size_t f = e + 1;
        while (f < text.size() && (isalnum(static_cast<unsigned char>(text[f])) || text[f] == '_'))
          ++f;
        p.field = text.substr(e + 1, f - e - 1);
      }
      break;
    }
  }
  if (p.t < 0) {
    // a file: the record of the mod's that names it
    const std::string l = Lower(text);
    for (int t = 0; t < ST_TABLES && p.t < 0; ++t)
      for (const StudioRecord &r : StudioRecords(t)) {
        if (r.kind != SK_OWN && r.kind != SK_EDIT)
          continue;
        for (const PortJsonMember &m : StudioRecordJson(t, r.name).members)
          if (m.second.kind == PortJson::STRING && m.second.s.size() > 4 && HasEnd(m.second.s) &&
              l.find(Lower(m.second.s)) != std::string::npos) {
            p.t = t;
            p.name = r.name;
            p.field = m.first;
          }
        if (p.t >= 0)
          break;
      }
    if (p.t < 0 && l.find("smk/") != std::string::npos)
      p.doc = "mod";
  }
  return p;
}

void Open(const Problem &p) {
  if (p.t >= 0) {
    // the record at its numbers, filtered to the field, or a unit's Sounds for its voice
    const bool bVoice = p.t == ST_UNITS && (p.field.find("sound") != std::string::npos || p.field == "portrait");
    DesignCall("studio_show", "\"table\":" + Q(StudioTableName(p.t)) + ",\"name\":" + Q(p.name) + ",\"view\":" +
                                  Q(bVoice ? "sounds" : "numbers") + ",\"filter\":" + Q(bVoice ? std::string() : p.field));
  } else if (p.doc == "looks") {
    DesignCall("studio_looks_show", p.look.empty() ? std::string("\"look\":\"rich-gold\"") : "\"look\":" + Q(p.look));
  } else if (p.doc == "hud") {
    DesignCall("studio_looks_show", "\"page\":" + Q(p.text.find("minimap") != std::string::npos ? "minimap" : "console"));
  } else if (p.doc == "mod") {
    StudioWorkspaceSet(8);
  }
}

std::string ProblemJson(const Problem &p, const char *pszSource) {
  return "{\"level\":" + Q(kLevelNames[p.nLevel]) + ",\"source\":" + Q(pszSource) + ",\"text\":" + Q(p.text) +
         (p.t >= 0 ? ",\"table\":" + Q(StudioTableName(p.t)) + ",\"name\":" + Q(p.name) + ",\"field\":" + Q(p.field) : std::string()) +
         (p.doc.empty() ? std::string() : ",\"doc\":" + Q(p.doc)) + (p.look.empty() ? std::string() : ",\"look\":" + Q(p.look)) + "}";
}

// The game's check as it stands: its state, and its lines as problems.
struct GameNow {
  int nState = 0, nExit = -1;
  std::string why, verdict, mod;
  std::vector<Problem> problems;
  long long nTook = 0, nRunning = 0;
  int nUnsavedThen = 0;
};

GameNow GameState(void) {
  GameNow g;
  std::vector<std::string> lines;
  {
    std::lock_guard<std::mutex> l(g_game.lock);
    g.nState = g_game.nState;
    g.nExit = g_game.nExit;
    g.why = g_game.why;
    g.mod = g_game.mod;
    g.nTook = g_game.nTook;
    g.nUnsavedThen = g_game.nUnsavedThen;
    g.nRunning = static_cast<long long>(GetTickCount64()) - g_game.nStarted;
    lines = g_game.lines;
  }
  for (const std::string &s : lines) {
    if (s.compare(0, 6, "plays:") == 0 || s.compare(0, 6, "plays ") == 0 || s.compare(0, 8, "refused:") == 0) {
      g.verdict = s;
      continue;
    }
    g.problems.push_back(ProblemOf(s));
  }
  return g;
}

bool CmdCheck(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string why;
  if (j["save"].kind == PortJson::BOOL && j["save"].b && StudioUnsaved() > 0 && !StudioSave(&why)) {
    *pAnswer = why;
    return false;
  }
  if (j["run"].kind == PortJson::BOOL && j["run"].b && !StartGameCheck(&why)) {
    *pAnswer = why;
    return false;
  }
  g_nStudioAt = ~0u;  // the studio's looked at again now, whatever the time
  std::string studio;
  int anCount[3] = {0, 0, 0};
  for (const Problem &p : StudioProblems()) {
    studio += (studio.empty() ? "" : ",") + ProblemJson(p, "studio");
    ++anCount[p.nLevel];
  }
  const GameNow g = GameState();
  std::string game;
  for (const Problem &p : g.problems)
    game += (game.empty() ? "" : ",") + ProblemJson(p, "game");
  static const char *const kStates[] = {"none", "running", "done"};
  *pAnswer = "\"errors\":" + std::to_string(anCount[0]) + ",\"warnings\":" + std::to_string(anCount[1]) + ",\"studio\":[" + studio +
             "],\"game\":{\"state\":" + Q(kStates[g.nState]) + ",\"exit\":" + std::to_string(g.nExit) + ",\"verdict\":" + Q(g.verdict) +
             ",\"why\":" + Q(g.why) + ",\"problems\":[" + game + "]}";
  return true;
}

// ---- the workspace ---------------------------------------------------------------------------------------

int g_nShow;           // all, errors, warnings, notes
bool g_bNotes;         // the game's notes shown
bool g_bSaveThenRun;   // a save asked for, the check to start when it is done

ImU32 LevelCol(int n) {
  return n == LV_ERROR ? C(TK_BAD) : n == LV_WARNING ? C(TK_WARN) : C(TK_INK3);
}

// A problem's row: its level, its words wrapped, and where it is as a button that opens it.
void Row(const Problem &p, float W, int i) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const std::string where = p.t >= 0 ? std::string(StudioTableName(p.t)) + " " + p.name + (p.field.empty() ? "" : "." + p.field)
                            : p.doc == "looks" ? "looks.json"
                            : p.doc == "hud"   ? "hud.json"
                            : p.doc == "mod"   ? "the mod's page"
                                               : std::string();
  const float ww = where.empty() ? 0.0f : std::min(P(300.0f), StudioButtonSize(where.c_str(), "link", 24.0f, kStudioFsXs).x);
  const float cw = StudioChipW("warning") + P(20.0f);
  const float tx = o.x + cw, tw = W - cw - ww - P(16.0f);
  StudioChip(d, ImVec2(o.x + P(8.0f), o.y + P(7.0f)), kLevelNames[p.nLevel], p.nLevel == LV_ERROR ? CH_BAD : p.nLevel == LV_WARNING ? CH_WARN : CH_PLAIN);
  ImGui::SetCursorScreenPos(ImVec2(tx, o.y + P(7.0f)));
  StudioMediaPara(p.text, tw, p.nLevel == LV_NOTE ? C(TK_INK2) : C(TK_INK), kStudioFsS);
  const float h = std::max(P(34.0f), ImGui::GetCursorScreenPos().y - o.y + P(6.0f));
  if (!where.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(o.x + W - ww, o.y + P(5.0f)));
    ImGui::PushID(i);
    if (StudioButton("##open", where.c_str(), "link", SB_PLAIN, 24.0f, ww / StudioPx(1.0f), kStudioFsXs))
      Open(p);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Open %s", where.c_str());
    ImGui::PopID();
  }
  d->AddLine(ImVec2(o.x, o.y + h - 0.5f), ImVec2(o.x + W, o.y + h - 0.5f), C(TK_ROW));
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(W, h));
}

// A problem of the game's check counts when it is of the mod open now.
bool GameCounts(const GameNow &g) {
  return g.nState == 2 && g.mod == StudioModId();
}

bool Shown(const Problem &p) {
  return g_nShow == 0 || g_nShow == p.nLevel + 1;
}

}  // namespace

void StudioChecksWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, a.x + P(20.0f), a.y + P(20.0f), C(TK_INK3), "Open a mod to check it.");
    return;
  }
  // a save asked for before the check: the check starts once it is done
  if (g_bSaveThenRun && StudioUnsaved() == 0) {
    g_bSaveThenRun = false;
    std::string why;
    if (!StartGameCheck(&why))
      StudioSay(why);
  }
  const std::vector<Problem> &studio = StudioProblems();
  const GameNow game = GameState();
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + 1));
  if (ImGui::BeginChild("##checks", ImVec2(b.x - a.x - 2, b.y - a.y - 2), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    const ImVec2 o0 = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(o0.x + P(28.0f), o0.y + P(20.0f)));
    ImGui::BeginGroup();
    const float W = std::min(ImGui::GetContentRegionAvail().x - P(28.0f), P(1200.0f));
    const ImVec2 o = ImGui::GetCursorScreenPos();
    int anStudio[3] = {0, 0, 0}, anGame[3] = {0, 0, 0};
    for (const Problem &p : studio)
      ++anStudio[p.nLevel];
    for (const Problem &p : game.problems)
      ++anGame[p.nLevel];
    T(d, STUDIO_FONT_BOLD, 28.0f, o.x, o.y, C(TK_HI), "Checks");
    float cx = o.x;
    const float cy = o.y + P(42.0f);
    const int nErrors = anStudio[0] + (game.nState == 2 ? anGame[0] : 0), nWarnings = anStudio[1] + (game.nState == 2 ? anGame[1] : 0);
    cx += StudioChip(d, ImVec2(cx, cy), (std::to_string(nErrors) + (nErrors == 1 ? " error" : " errors")).c_str(), nErrors ? CH_BAD : CH_GOOD) + P(6.0f);
    cx += StudioChip(d, ImVec2(cx, cy), (std::to_string(nWarnings) + (nWarnings == 1 ? " warning" : " warnings")).c_str(),
                     nWarnings ? CH_WARN : CH_PLAIN) + P(6.0f);
    if (game.nState == 2 && !game.verdict.empty())
      StudioChip(d, ImVec2(cx, cy), game.verdict.substr(0, game.verdict.find(':')).c_str(),
                 game.nExit == 0 ? CH_GOOD : CH_BAD);
    // the game's check: run, or save and run
    const int nUnsaved = StudioUnsaved();
    const char *pszRun = game.nState == 1 ? "Checking" : nUnsaved ? "Save and run the game's check" : "Run the game's check";
    const float bw = StudioButtonSize(pszRun, "check", 30.0f, 13.0f).x;
    ImGui::SetCursorScreenPos(ImVec2(o.x + W - bw, o.y));
    if (StudioButton("##run", pszRun, "check", SB_GOOD, 30.0f, 0.0f, 13.0f, game.nState != 1 && !g_bSaveThenRun)) {
      if (nUnsaved) {
        g_bSaveThenRun = true;
        StudioAsk("studio_save", "");
      } else {
        std::string why;
        if (!StartGameCheck(&why))
          StudioSay(why);
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Hyperion.exe --mod-check: the mod loaded as a game loads it, and every file it names decoded");
    ImGui::SetCursorScreenPos(o);
    ImGui::Dummy(ImVec2(W, P(76.0f)));
    static const char *const kShow[] = {"Everything", "Errors", "Warnings", "Notes"};
    g_nShow = StudioSeg("##show", kShow, 4, g_nShow);
    ImGui::Dummy(ImVec2(1, P(12.0f)));
    // the studio's own, as the mod is now
    StudioMediaHeading("The studio's checks", "as the mod is now, looked at again with each edit", W);
    int nShown = 0;
    for (size_t i = 0; i < studio.size(); ++i)
      if (Shown(studio[i])) {
        Row(studio[i], W, static_cast<int>(i));
        ++nShown;
      }
    if (nShown == 0)
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_GOOD), studio.empty() ? "Every record resolves, and every name and file it gives is there."
                                                                           : "Nothing of this kind.");
    ImGui::Dummy(ImVec2(1, P(22.0f)));
    // the game's, as Hyperion.exe read the saved files
    std::string what;
    if (game.nState == 0)
      what = "not run yet: Hyperion.exe reads the saved files as a game would, and decodes each one they name";
    else if (game.nState == 1)
      what = "Hyperion.exe is reading the mod, " + Fmt(game.nRunning / 1000.0) + " s";
    else
      what = "the files as saved, " + Fmt(game.nTook / 1000.0) + " s" +
             (game.nUnsavedThen || nUnsaved ? "; edits since are not in it" : std::string());
    StudioMediaHeading("The game's check", what.c_str(), W);
    if (game.nState == 2 && game.mod != StudioModId())
      StudioMediaPara("This was the check of " + game.mod + ", before this mod was opened.", W, C(TK_INK3), kStudioFsXs);
    if (!game.why.empty())
      StudioMediaPara(game.why, W, C(TK_BAD), kStudioFsS);
    if (game.nState == 2) {
      if (!game.verdict.empty()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        d->AddRectFilled(p, ImVec2(p.x + W, p.y + P(34.0f)), game.nExit == 0 ? C(TK_GOOD, 0.12f) : C(TK_BAD, 0.12f), P(3.0f));
        T(d, STUDIO_FONT_BOLD, 14.0f, p.x + P(12.0f), Mid(p.y, P(34.0f), 14.0f), game.nExit == 0 ? C(TK_GOOD) : C(TK_BAD), game.verdict, W - P(24.0f));
        ImGui::Dummy(ImVec2(W, P(40.0f)));
      }
      nShown = 0;
      for (size_t i = 0; i < game.problems.size(); ++i) {
        const Problem &p = game.problems[i];
        if (!Shown(p) || (p.nLevel == LV_NOTE && !g_bNotes && g_nShow != 3))
          continue;
        Row(p, W, 10000 + static_cast<int>(i));
        ++nShown;
      }
      if (anGame[LV_NOTE] > 0 && g_nShow == 0) {
        ImGui::Dummy(ImVec2(1, P(6.0f)));
        StudioCheck("##notes", ("Show what loaded: " + std::to_string(anGame[LV_NOTE]) + " notes").c_str(), &g_bNotes);
      }
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, P(24.0f)));
  }
  ImGui::EndChild();
}

int StudioCheckProblems(int *pnErrors, int *pnWarnings) {
  int an[3] = {0, 0, 0};
  if (StudioIsOpen()) {
    for (const Problem &p : StudioProblems())
      ++an[p.nLevel];
    const GameNow g = GameState();
    if (GameCounts(g))
      for (const Problem &p : g.problems)
        ++an[p.nLevel];
  }
  if (pnErrors)
    *pnErrors = an[LV_ERROR];
  if (pnWarnings)
    *pnWarnings = an[LV_WARNING];
  return an[LV_ERROR] + an[LV_WARNING];
}

void StudioChecksList(float fW) {
  if (!StudioIsOpen())
    return;
  ImDrawList *d = ImGui::GetWindowDrawList();
  std::vector<std::pair<Problem, const char *>> rows;
  for (const Problem &p : StudioProblems())
    if (p.nLevel != LV_NOTE)
      rows.push_back({p, "studio"});
  const GameNow g = GameState();
  if (GameCounts(g))
    for (const Problem &p : g.problems)
      if (p.nLevel != LV_NOTE)
        rows.push_back({p, "game"});
  std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first.nLevel < b.first.nLevel; });
  const float rh = P(30.0f);
  for (size_t i = 0; i < rows.size(); ++i) {
    const Problem &p = rows[i].first;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::PushID(static_cast<int>(i) + 50000);
    const bool bClick = ImGui::InvisibleButton("##row", ImVec2(fW, rh));
    const bool bHover = ImGui::IsItemHovered();
    ImGui::PopID();
    if (bHover)
      d->AddRectFilled(o, ImVec2(o.x + fW, o.y + rh), C(TK_ROW));
    if (bClick)
      Open(p);
    const ImU32 col = LevelCol(p.nLevel);
    StudioGlyph(d, p.nLevel == LV_ERROR ? "clash" : "needs", ImVec2(o.x + P(6.0f), Mid(o.y, rh, 16.0f)), P(16.0f), col);
    Wide(d, 9.5f, o.x + P(32.0f), Mid(o.y, rh, 9.5f), col, kLevelNames[p.nLevel], 0.08f);
    const std::string where = p.t >= 0 ? std::string(StudioTableName(p.t)) + " " + p.name + (p.field.empty() ? "" : "." + p.field)
                              : p.doc == "looks" ? "looks.json"
                              : p.doc == "hud"   ? "hud.json"
                              : p.doc == "mod"   ? "mod.json"
                                                 : std::string(rows[i].second) + "'s check";
    const float ww = std::min(P(260.0f), TW(STUDIO_FONT_MONO, kStudioFsXs, where) + P(10.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + P(110.0f), Mid(o.y, rh, kStudioFsS), C(TK_INK), p.text, fW - P(130.0f) - ww);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, o.x + fW - ww, Mid(o.y, rh, kStudioFsXs), C(TK_INK3), where, ww);
    if (bHover)
      ImGui::SetTooltip("%s\n%s", p.text.c_str(), (std::string(strcmp(rows[i].second, "game") == 0 ? "Hyperion.exe --mod-check" : "the studio's check") +
                                                       (where.empty() ? std::string() : ": opens " + where)).c_str());
    d->AddLine(ImVec2(o.x, o.y + rh - 0.5f), ImVec2(o.x + fW, o.y + rh - 0.5f), C(TK_ROW));
  }
}

void StudioChecksInit(void) {
  PortControlCommand("studio_check", CmdCheck);
}
