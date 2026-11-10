// studio_model.cpp -- the mod open in the studio: its documents, its records and the mods it
// depends on, saving and the autosave, and the edits with their undo and history
// (studio_model.h). A record's values and their sources are worked out in studio_resolve.cpp.
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "../port/data/port_mods.h"      // PortMods, PortModChain
#include "../port/platform/port_json.h"
#include "../port/platform/port_settings.h"
#include "../port/platform/port_util.h"  // PortReadFile
#include "studio_model.h"

namespace {

// A mod's data as the studio reads it: the open one, which it changes, and each it depends on,
// which it reads only.
struct ModData {
  std::string id;
  std::string folder;  // the mod's folder, ending in a backslash
  std::string data;    // its data folder, ending in a backslash
  PortJson mod;        // mod.json
  PortJson types;      // types.json
  PortJson looks;      // looks.json
  PortJson hud;        // hud.json, the console's, the minimap's and the selection's looks
  PortJson readout;    // readout.json, the one unit's readout: its parts, what it adds and its effects
  std::string script;  // iscript.txt
  PortJson lang;       // lang\<code>.json, each language's translations, by its code
  std::map<std::string, std::string> diskLang;
  PortJson scripts;    // scripts\*.lua and *.luau of its data folder, each file's text by its name
  std::map<std::string, std::string> diskScripts;
  // the files' text as last read or saved, so a save writes only what changed
  std::string diskMod, diskTypes, diskLooks, diskHud, diskReadout, diskScript;
};

bool g_bOpen;
ModData g_open;
std::vector<ModData> g_depends;  // in load order, the open mod's own last and not here

std::vector<StudioEdit> g_history;
int g_nAt;          // edits before this are done
int g_nSavedAt;     // g_nAt at the last save; -1 when that state can no longer be reached
int g_nGroup;       // the group being made, 0 for none
int g_nGroups;      // groups made
std::string g_groupSender, g_groupLabel, g_groupMerge, g_lastMerge;
ULONGLONG g_qwLastMerge;
unsigned g_nChange = 1;
ULONGLONG g_qwAutosaved;
unsigned g_nAutosavedChange;

// Claude's asks: while one runs, the edits sent as "claude" join one group; the pending group is
// its proposal, waiting to be accepted
bool g_bJoinClaude, g_bJoinPending;
int g_nClaudeGroup;
int g_nPending;
int g_nAsideGroup;  // the group whose edits are taken off the documents for now, 0 for none

// Where the proposal's edits start while it is done and the last group, else -1.
int PendingFrom(void) {
  if (g_nPending == 0 || g_nAt == 0 || g_history[static_cast<size_t>(g_nAt) - 1].nGroup != g_nPending)
    return -1;
  int i = g_nAt;
  while (i > 0 && g_history[static_cast<size_t>(i) - 1].nGroup == g_nPending)
    --i;
  return i;
}

const unsigned kMergeMs = 4000;     // a merge joins the group before it within this
const unsigned kAutosaveMs = 60000;

std::string Slash(std::string s) {
  if (!s.empty() && s.back() != '\\' && s.back() != '/')
    s += '\\';
  for (char &c : s)
    if (c == '/')
      c = '\\';
  return s;
}

bool ReadJson(const std::string &path, PortJson *pOut, std::string *pText, std::string *pWhy) {
  pText->clear();
  *pOut = PortJson();
  if (!PortReadFile(path, pText))
    return true;  // a file the mod does not have is an empty document
  std::string error;
  if (!PortJsonParse(pText->c_str(), pOut, &error)) {
    *pWhy = path + ": " + StudioJsonWhere(*pText, error);
    return false;
  }
  return true;
}

bool ReadMod(const std::string &folder, ModData *pOut, std::string *pWhy) {
  ModData m;
  m.folder = Slash(folder);
  if (!ReadJson(m.folder + "mod.json", &m.mod, &m.diskMod, pWhy))
    return false;
  if (m.mod.kind != PortJson::OBJECT) {
    *pWhy = m.folder + " has no mod.json";
    return false;
  }
  m.id = m.mod["id"].Str();
  const std::string data = m.mod["data"].Str();
  if (data.find("..") != std::string::npos || data.find(':') != std::string::npos ||
      (!data.empty() && (data[0] == '/' || data[0] == '\\'))) {
    *pWhy = m.folder + "mod.json's data is a folder inside the mod";
    return false;
  }
  m.data = data.empty() ? m.folder : Slash(m.folder + data);
  if (!ReadJson(m.data + "types.json", &m.types, &m.diskTypes, pWhy) ||
      !ReadJson(m.data + "looks.json", &m.looks, &m.diskLooks, pWhy) ||
      !ReadJson(m.data + "hud.json", &m.hud, &m.diskHud, pWhy) ||
      !ReadJson(m.data + "readout.json", &m.readout, &m.diskReadout, pWhy))
    return false;
  if (m.hud.kind == PortJson::NONE)
    m.hud = StudioJsonObject();
  if (m.readout.kind == PortJson::NONE)
    m.readout = StudioJsonObject();
  if (m.looks.kind == PortJson::NONE)
    m.looks = StudioJsonObject();
  if (m.types.kind == PortJson::NONE)
    m.types = StudioJsonObject();
  // the languages, a file each in the mod's lang\; one that does not read is left out
  m.lang = StudioJsonObject();
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA((m.folder + "lang\\*.json").c_str(), &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      const std::string file = fd.cFileName;
      PortJson j;
      std::string text, why;
      if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && ReadJson(m.folder + "lang\\" + file, &j, &text, &why) &&
          j.kind == PortJson::OBJECT) {
        StudioJsonPut(m.lang, file.substr(0, file.size() - 5), j);
        m.diskLang[file.substr(0, file.size() - 5)] = text;
      }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
  }
  PortReadFile(m.data + "iscript.txt", &m.script);
  m.diskScript = m.script;
  // the scripts, a file each in the data folder's scripts\ (docs/mod-data.md, Scripts)
  m.scripts = StudioJsonObject();
  for (const char *pszExt : {"*.lua", "*.luau"}) {
    HANDLE hScripts = FindFirstFileA((m.data + "scripts\\" + pszExt).c_str(), &fd);
    if (hScripts == INVALID_HANDLE_VALUE)
      continue;
    do {
      const std::string file = fd.cFileName;
      std::string text;
      const size_t n = file.size();
      const bool bLua = (n > 4 && file.compare(n - 4, 4, ".lua") == 0) || (n > 5 && file.compare(n - 5, 5, ".luau") == 0);
      if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && bLua && PortReadFile(m.data + "scripts\\" + file, &text)) {
        StudioJsonPut(m.scripts, file, StudioJsonString(text));
        m.diskScripts[file] = text;
      }
    } while (FindNextFileA(hScripts, &fd));
    FindClose(hScripts);
  }
  *pOut = m;
  return true;
}

PortJson *DocOf(const std::string &doc) {
  if (doc == "types")
    return &g_open.types;
  if (doc == "mod")
    return &g_open.mod;
  if (doc == "looks")
    return &g_open.looks;
  if (doc == "hud")
    return &g_open.hud;
  if (doc == "readout")
    return &g_open.readout;
  if (doc == "lang")
    return &g_open.lang;
  if (doc == "scripts")
    return &g_open.scripts;
  return nullptr;
}

std::string StudioFolder(void) {
  return g_open.folder + ".studio\\";
}

// A write that leaves the old file whole until the new one is: written beside it, then moved
// over it.
bool WriteFile(const std::string &path, const std::string &text, std::string *pWhy) {
  const std::string tmp = path + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (f == NULL) {
    *pWhy = tmp + " does not open to write";
    return false;
  }
  const bool bOk = fwrite(text.data(), 1, text.size(), f) == text.size();
  if (fclose(f) != 0 || !bOk) {
    DeleteFileA(tmp.c_str());
    *pWhy = tmp + " was not written whole";
    return false;
  }
  if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileA(tmp.c_str());
    *pWhy = path + " could not be replaced";
    return false;
  }
  return true;
}

// The text a document is saved as: a tree in the studio's layout, unchanged from the file's own
// when it has not changed since it was read, so a save leaves the files it does not change byte
// for byte as they were (the fingerprint hashes their bytes).
std::string SaveText(const PortJson &now, const std::string &disk) {
  if (!disk.empty()) {
    PortJson was;
    if (PortJsonParse(disk.c_str(), &was) && PortJsonWrite(was) == PortJsonWrite(now))
      return disk;
  }
  if (now.kind == PortJson::NONE)
    return std::string();
  return StudioJsonText(now);
}

void HistoryAppend(const StudioEdit &e) {
  CreateDirectoryA(StudioFolder().c_str(), NULL);
  FILE *f = fopen((StudioFolder() + "history.jsonl").c_str(), "ab");
  if (f == NULL)
    return;
  std::string path;
  for (const std::string &p : e.path)
    path += (path.empty() ? "" : ",") + PortJsonQuote(p);
  const std::string line = "{\"time\":" + std::to_string(e.nTime) + ",\"group\":" + std::to_string(e.nGroup) +
                           ",\"sender\":" + PortJsonQuote(e.sender) + ",\"label\":" + PortJsonQuote(e.label) +
                           ",\"doc\":" + PortJsonQuote(e.doc) + ",\"path\":[" + path + "],\"before\":" +
                           PortJsonWrite(e.before) + ",\"after\":" + PortJsonWrite(e.after) + "}\n";
  fwrite(line.data(), 1, line.size(), f);
  fclose(f);
}

// One edit done (bForward) or undone, on the open mod's documents. The path's members on the way
// are there: an edit that needs one made makes it with an edit of its own first.
bool Apply(const StudioEdit &e, bool bForward, std::string *pWhy) {
  const PortJson &to = bForward ? e.after : e.before;
  if (e.doc == "iscript") {
    g_open.script = to.Str();
    return true;
  }
  PortJson *pDoc = DocOf(e.doc);
  if (pDoc == nullptr) {
    *pWhy = "no document " + e.doc;
    return false;
  }
  if (e.path.empty()) {
    *pDoc = to;
    return true;
  }
  PortJson *p = pDoc;
  for (size_t i = 0; i + 1 < e.path.size(); ++i) {
    p = StudioJsonMember(*p, e.path[i]);
    if (p == nullptr) {
      *pWhy = e.doc + " has no " + e.path[i];
      return false;
    }
  }
  if (p->kind != PortJson::OBJECT && p->kind != PortJson::NONE) {
    *pWhy = e.path[e.path.size() - 2] + " is not an object";
    return false;
  }
  if (to.kind == PortJson::NONE)
    StudioJsonErase(*p, e.path.back());
  else
    StudioJsonPut(*p, e.path.back(), to, bForward ? -1 : e.nIndex);
  return true;
}

int IndexOf(const PortJson &obj, const std::string &key) {
  for (size_t i = 0; i < obj.members.size(); ++i)
    if (obj.members[i].first == key)
      return static_cast<int>(i);
  return -1;
}

// The member a path reaches, or NONE.
const PortJson &At(const std::string &doc, const std::vector<std::string> &path) {
  static const PortJson kNone;
  if (doc == "iscript")
    return kNone;
  const PortJson *p = DocOf(doc);
  for (const std::string &k : path) {
    if (p == nullptr)
      return kNone;
    p = &(*p)[k.c_str()];
  }
  return p ? *p : kNone;
}

// An edit of one member: what it was before, and its place, filled from the document.
StudioEdit MemberEdit(const std::string &doc, const std::vector<std::string> &path, const PortJson &after) {
  StudioEdit e;
  e.doc = doc;
  e.path = path;
  e.after = after;
  if (!path.empty()) {
    std::vector<std::string> up(path.begin(), path.end() - 1);
    const PortJson &parent = At(doc, up);
    e.before = parent[path.back().c_str()];
    e.nIndex = IndexOf(parent, path.back());
  } else if (const PortJson *p = DocOf(doc)) {
    e.before = *p;
  }
  return e;
}

// The members a path goes through, made as objects when they are not there, each an edit.
bool MakeParents(const std::string &doc, const std::vector<std::string> &path, std::string *pWhy) {
  for (size_t n = 1; n < path.size(); ++n) {
    std::vector<std::string> up(path.begin(), path.begin() + static_cast<long long>(n));
    if (At(doc, up).kind == PortJson::NONE && !StudioEditApply(MemberEdit(doc, up, StudioJsonObject()), pWhy))
      return false;
  }
  return true;
}

// One group of one command's edits.
struct Group {
  bool bOwn;
  Group(const std::string &sender, const std::string &label, const std::string &merge = std::string()) {
    bOwn = g_nGroup == 0;
    if (bOwn)
      StudioGroupBegin(sender, label, merge);
  }
  ~Group() {
    if (bOwn)
      StudioGroupEnd();
  }
};

bool TypesPath(int t, const std::string &name, std::string *pWhy) {
  if (!g_bOpen) {
    *pWhy = "no mod is open";
    return false;
  }
  if (t < 0 || t >= ST_TABLES) {
    *pWhy = "no such table";
    return false;
  }
  if (name.empty()) {
    *pWhy = "a record needs a name";
    return false;
  }
  return true;
}

// Ids as the loader gives them: each submission's records of a table in the order of their names
// (byte order), from the table's first mod id, the dependencies' first, then the open mod's.
// Edits take none. Keyed by the submission's place (the open mod's is the dependencies' count)
// and the name.
void Ids(int t, std::map<std::pair<int, std::string>, int> *pOut) {
  int nNext = StudioModFirst(t);
  auto take = [&](const ModData &m, int nMod) {
    std::vector<std::string> names;
    for (const PortJsonMember &r : m.types[StudioTableName(t)].members)
      if (r.second["edit"].kind == PortJson::NONE)
        names.push_back(r.first);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names)
      (*pOut)[{nMod, n}] = nNext++;
  };
  for (size_t i = 0; i < g_depends.size(); ++i)
    take(g_depends[i], static_cast<int>(i));
  take(g_open, static_cast<int>(g_depends.size()));
}

}  // namespace

// ---- the open mod ------------------------------------------------------------------------------------

bool StudioIsOpen(void) {
  return g_bOpen;
}

const std::string &StudioModId(void) {
  return g_open.id;
}

const std::string &StudioModFolder(void) {
  return g_open.folder;
}

const std::string &StudioDataFolder(void) {
  return g_open.data;
}

const PortJson &StudioDoc(const std::string &doc) {
  static const PortJson kNone;
  const PortJson *p = DocOf(doc);
  return p ? *p : kNone;
}

const std::string &StudioScript(void) {
  return g_open.script;
}

int StudioDependencies(void) {
  return g_bOpen ? static_cast<int>(g_depends.size()) : 0;
}

const std::string &StudioDependencyId(int i) {
  static const std::string kNone;
  return i >= 0 && i < StudioDependencies() ? g_depends[static_cast<size_t>(i)].id : kNone;
}

const std::string &StudioDependencyScript(int i) {
  static const std::string kNone;
  return i >= 0 && i < StudioDependencies() ? g_depends[static_cast<size_t>(i)].script : kNone;
}

const PortJson &StudioDependencyLang(int i) {
  static const PortJson kNone;
  return i >= 0 && i < StudioDependencies() ? g_depends[static_cast<size_t>(i)].lang : kNone;
}

const PortJson &StudioDependencyTypes(int i) {
  static const PortJson kNone;
  return i >= 0 && i < StudioDependencies() ? g_depends[static_cast<size_t>(i)].types : kNone;
}

std::vector<StudioRecord> StudioRecords(int t) {
  std::vector<StudioRecord> out;
  if (!g_bOpen || t < 0 || t >= ST_TABLES)
    return out;
  std::map<std::pair<int, std::string>, int> ids;
  Ids(t, &ids);
  auto add = [&](const ModData &m, int nMod) {
    const bool bOpenMod = nMod == static_cast<int>(g_depends.size());
    for (const PortJsonMember &r : m.types[StudioTableName(t)].members) {
      StudioRecord rec;
      rec.t = t;
      rec.name = r.first;
      rec.mod = m.id;
      const bool bEdit = r.second["edit"].kind != PortJson::NONE;
      rec.kind = !bOpenMod ? SK_DEPENDENCY : bEdit ? SK_EDIT : SK_OWN;
      const PortJson &base = bEdit ? r.second["edit"] : r.second["base"];
      rec.base = base.kind == PortJson::STRING ? base.s
                 : base.kind == PortJson::NUMBER ? StudioRefName(t, static_cast<long long>(base.d))
                                                 : std::string();
      auto it = ids.find({nMod, r.first});
      rec.nId = bEdit ? (base.kind == PortJson::NUMBER ? static_cast<int>(base.d) : StudioClassicRow(t, rec.base))
                      : it != ids.end() ? it->second : -1;
      out.push_back(rec);
    }
  };
  add(g_open, static_cast<int>(g_depends.size()));
  for (size_t i = g_depends.size(); i-- > 0;)
    add(g_depends[i], static_cast<int>(i));
  return out;
}

bool StudioRecordOf(int t, const std::string &name, StudioRecord *pOut) {
  for (const StudioRecord &r : StudioRecords(t))
    if (r.name == name) {
      *pOut = r;
      return true;
    }
  const int nRow = StudioClassicRow(t, name);
  if (nRow < 0)
    return false;
  StudioRecord r;
  r.t = t;
  r.name = name;
  r.kind = SK_CLASSIC;
  r.nId = nRow;
  *pOut = r;
  return true;
}

const PortJson &StudioRecordJson(int t, const std::string &name) {
  static const PortJson kNone;
  if (!g_bOpen || t < 0 || t >= ST_TABLES)
    return kNone;
  const PortJson &own = g_open.types[StudioTableName(t)][name.c_str()];
  if (own.kind != PortJson::NONE)
    return own;
  for (size_t i = g_depends.size(); i-- > 0;) {
    const PortJson &dep = g_depends[i].types[StudioTableName(t)][name.c_str()];
    if (dep.kind != PortJson::NONE)
      return dep;
  }
  return kNone;
}

bool StudioOpen(const std::string &modOrFolder, std::string *pWhy) {
  const std::vector<PortMod> &mods = PortMods();
  int nMod = -1;
  for (size_t i = 0; i < mods.size(); ++i)
    if (!mods[i].bBuiltIn && mods[i].id == modOrFolder)
      nMod = static_cast<int>(i);
  std::string folder = modOrFolder;
  std::vector<ModData> depends;
  if (nMod >= 0) {
    folder = mods[nMod].folder;
    std::vector<int> chain;
    std::string why;
    if (!PortModChain(nMod, &chain, &why)) {
      *pWhy = why;
      return false;
    }
    for (int c : chain) {
      if (c == nMod || mods[c].data.empty())
        continue;
      ModData d;
      if (!ReadMod(mods[c].folder, &d, pWhy))
        return false;
      depends.push_back(d);
    }
  }
  ModData m;
  if (!ReadMod(folder, &m, pWhy))
    return false;
  g_open = m;
  g_depends = depends;
  g_bOpen = true;
  g_history.clear();
  g_nAt = g_nSavedAt = 0;
  g_nGroup = 0;
  g_nPending = g_nClaudeGroup = g_nAsideGroup = 0;
  g_lastMerge.clear();
  g_qwAutosaved = GetTickCount64();
  ++g_nChange;
  g_nAutosavedChange = g_nChange;
  PortSettingSetString("studio", "mod", nMod >= 0 ? modOrFolder.c_str() : g_open.folder.c_str());
  return true;
}

bool StudioRecoverWaiting(std::string *pWhen) {
  if (!g_bOpen)
    return false;
  std::string text;
  PortJson j;
  if (!PortReadFile(StudioFolder() + "autosave\\autosave.json", &text) || !PortJsonParse(text.c_str(), &j))
    return false;
  if (pWhen)
    *pWhen = j["when"].Str();
  return j["unsaved"].kind == PortJson::BOOL && j["unsaved"].b;
}

bool StudioRecover(const std::string &sender, std::string *pWhy) {
  if (!StudioRecoverWaiting(nullptr)) {
    *pWhy = "no autosave is waiting";
    return false;
  }
  const std::string dir = StudioFolder() + "autosave\\";
  Group g(sender, "the autosave taken back");
  for (const char *pszDoc : {"types", "mod", "looks", "hud", "readout", "lang", "scripts"}) {
    PortJson j;
    std::string text;
    if (!ReadJson(dir + pszDoc + ".json", &j, &text, pWhy))
      return false;
    if (text.empty())
      continue;
    if (PortJsonWrite(j) != PortJsonWrite(StudioDoc(pszDoc)) &&
        !StudioEditApply(MemberEdit(pszDoc, {}, j), pWhy))
      return false;
  }
  std::string script;
  if (PortReadFile(dir + "iscript.txt", &script) && script != g_open.script &&
      !StudioSetScript(script, sender, pWhy))
    return false;
  return true;
}

static bool SaveDocs(std::string *pWhy) {
  if (!g_bOpen) {
    *pWhy = "no mod is open";
    return false;
  }
  const std::string types = SaveText(g_open.types, g_open.diskTypes);
  const std::string mod = SaveText(g_open.mod, g_open.diskMod);
  const std::string looks = SaveText(g_open.looks, g_open.diskLooks);
  const std::string hud = SaveText(g_open.hud, g_open.diskHud);
  const std::string readout = SaveText(g_open.readout, g_open.diskReadout);
  // a hud.json and a readout.json are each written once they have something in them, or when one
  // was there: a mod with no readout leaves the panel classic's, pixel for pixel
  const bool bHud = hud != g_open.diskHud && (!g_open.diskHud.empty() || !g_open.hud.members.empty());
  const bool bReadout =
      readout != g_open.diskReadout && (!g_open.diskReadout.empty() || !g_open.readout.members.empty());
  const bool bLooks = looks != g_open.diskLooks && (!g_open.diskLooks.empty() || !g_open.looks.members.empty());
  CreateDirectoryA(g_open.data.c_str(), NULL);
  if ((types != g_open.diskTypes && !WriteFile(g_open.data + "types.json", types, pWhy)) ||
      (mod != g_open.diskMod && !WriteFile(g_open.folder + "mod.json", mod, pWhy)) ||
      (bLooks && !WriteFile(g_open.data + "looks.json", looks, pWhy)) ||
      (bHud && !WriteFile(g_open.data + "hud.json", hud, pWhy)) ||
      (bReadout && !WriteFile(g_open.data + "readout.json", readout, pWhy)) ||
      (g_open.script != g_open.diskScript && !WriteFile(g_open.data + "iscript.txt", g_open.script, pWhy)))
    return false;
  // each language's file that changed, and the files of languages taken away
  for (const PortJsonMember &l : g_open.lang.members) {
    auto it = g_open.diskLang.find(l.first);
    const std::string text = SaveText(l.second, it == g_open.diskLang.end() ? std::string() : it->second);
    if (it != g_open.diskLang.end() && text == it->second)
      continue;
    CreateDirectoryA((g_open.folder + "lang").c_str(), NULL);
    if (!WriteFile(g_open.folder + "lang\\" + l.first + ".json", text, pWhy))
      return false;
    g_open.diskLang[l.first] = text;
  }
  for (auto it = g_open.diskLang.begin(); it != g_open.diskLang.end();) {
    if (g_open.lang[it->first.c_str()].kind == PortJson::NONE) {
      DeleteFileA((g_open.folder + "lang\\" + it->first + ".json").c_str());
      it = g_open.diskLang.erase(it);
    } else {
      ++it;
    }
  }
  // each script that changed, as its text is, and the files of scripts taken away
  for (const PortJsonMember &f : g_open.scripts.members) {
    auto it = g_open.diskScripts.find(f.first);
    if (f.second.kind != PortJson::STRING || (it != g_open.diskScripts.end() && it->second == f.second.s))
      continue;
    CreateDirectoryA((g_open.data + "scripts").c_str(), NULL);
    if (!WriteFile(g_open.data + "scripts\\" + f.first, f.second.s, pWhy))
      return false;
    g_open.diskScripts[f.first] = f.second.s;
  }
  for (auto it = g_open.diskScripts.begin(); it != g_open.diskScripts.end();) {
    if (g_open.scripts[it->first.c_str()].kind == PortJson::NONE) {
      DeleteFileA((g_open.data + "scripts\\" + it->first).c_str());
      it = g_open.diskScripts.erase(it);
    } else {
      ++it;
    }
  }
  g_open.diskTypes = types;
  g_open.diskMod = mod;
  if (bLooks)
    g_open.diskLooks = looks;
  if (bHud)
    g_open.diskHud = hud;
  g_open.diskScript = g_open.script;
  g_nSavedAt = g_nAt;
  // what the autosave kept is in the files now
  WriteFile(StudioFolder() + "autosave\\autosave.json", "{\"unsaved\": false}\n", pWhy);
  pWhy->clear();
  ++g_nChange;
  return true;
}

// A save writes the mod without Claude's proposal, which waits to be accepted.
bool StudioSave(std::string *pWhy) {
  const int nFrom = PendingFrom();
  if (nFrom < 0)
    return SaveDocs(pWhy);
  const int nGroup = g_nPending;
  StudioGroupAside(nGroup, true);
  const int nAt = g_nAt;
  g_nAt = nFrom;  // the state written is the one before the proposal
  const bool bOk = SaveDocs(pWhy);
  g_nAt = nAt;
  StudioGroupAside(nGroup, false);
  return bOk;
}

static void AutosaveDocs(bool bNow);

void StudioAutosave(bool bNow) {
  const int nGroup = PendingFrom() >= 0 ? g_nPending : 0;
  StudioGroupAside(nGroup, true);
  AutosaveDocs(bNow);
  StudioGroupAside(nGroup, false);
}

static void AutosaveDocs(bool bNow) {
  if (!g_bOpen)
    return;
  const ULONGLONG now = GetTickCount64();
  if (!bNow && now - g_qwAutosaved < kAutosaveMs)
    return;
  g_qwAutosaved = now;
  if (g_nAutosavedChange == g_nChange || StudioUnsaved() == 0)
    return;
  g_nAutosavedChange = g_nChange;
  const std::string dir = StudioFolder() + "autosave\\";
  CreateDirectoryA(StudioFolder().c_str(), NULL);
  CreateDirectoryA(dir.c_str(), NULL);
  std::string why;
  WriteFile(dir + "types.json", StudioJsonText(g_open.types), &why);
  WriteFile(dir + "mod.json", StudioJsonText(g_open.mod), &why);
  if (!g_open.looks.members.empty())
    WriteFile(dir + "looks.json", StudioJsonText(g_open.looks), &why);
  if (!g_open.hud.members.empty())
    WriteFile(dir + "hud.json", StudioJsonText(g_open.hud), &why);
    WriteFile(dir + "readout.json", StudioJsonText(g_open.readout), &why);
  if (!g_open.lang.members.empty())
    WriteFile(dir + "lang.json", StudioJsonText(g_open.lang), &why);
  if (!g_open.scripts.members.empty())
    WriteFile(dir + "scripts.json", StudioJsonText(g_open.scripts), &why);
  WriteFile(dir + "iscript.txt", g_open.script, &why);
  char szWhen[32];
  const time_t t = time(nullptr);
  struct tm local;
  localtime_s(&local, &t);
  strftime(szWhen, sizeof szWhen, "%Y-%m-%d %H:%M:%S", &local);
  WriteFile(dir + "autosave.json", std::string("{\"unsaved\": true, \"when\": \"") + szWhen + "\"}\n", &why);
}

// ---- edits --------------------------------------------------------------------------------------------

const std::vector<StudioEdit> &StudioHistory(void) {
  return g_history;
}

int StudioHistoryAt(void) {
  return g_nAt;
}

int StudioUnsaved(void) {
  if (g_nSavedAt < 0)
    return g_nAt > 0 ? g_nAt : 1;
  return g_nAt > g_nSavedAt ? g_nAt - g_nSavedAt : g_nSavedAt - g_nAt;
}

unsigned StudioChange(void) {
  return g_nChange;
}

void StudioGroupBegin(const std::string &sender, const std::string &label, const std::string &merge) {
  const ULONGLONG now = GetTickCount64();
  // a merge joins the last group when nothing was undone or made since
  const bool bJoin = !merge.empty() && merge == g_lastMerge && now - g_qwLastMerge < kMergeMs && g_nAt > 0 &&
                     g_nAt == static_cast<int>(g_history.size()) && g_history.back().nGroup == g_nGroups;
  // while an ask runs, Claude's edits join its group as long as that is the last one done
  const bool bClaude = g_bJoinClaude && sender == "claude";
  const bool bAsk = bClaude && g_nClaudeGroup > 0 && g_nAt > 0 && g_nAt == static_cast<int>(g_history.size()) &&
                    g_history.back().nGroup == g_nClaudeGroup;
  g_nGroup = bAsk ? g_nClaudeGroup : bJoin ? g_nGroups : ++g_nGroups;
  if (bClaude) {
    g_nClaudeGroup = g_nGroup;
    if (g_bJoinPending)
      g_nPending = g_nGroup;
  }
  g_groupSender = sender.empty() ? "you" : sender;
  g_groupLabel = label;
  g_groupMerge = merge;
}

bool StudioEditApply(StudioEdit edit, std::string *pWhy) {
  if (!g_bOpen) {
    *pWhy = "no mod is open";
    return false;
  }
  const bool bOwn = g_nGroup == 0;
  if (bOwn)
    StudioGroupBegin(edit.sender, edit.label);
  if (g_nGroup != g_nPending && PendingFrom() >= 0) {
    *pWhy = "Claude's proposal is waiting: accept it or undo it first, in the Claude tab";
    if (bOwn)
      StudioGroupEnd();
    return false;
  }
  edit.nGroup = g_nGroup;
  if (edit.sender.empty())
    edit.sender = g_groupSender;
  if (edit.label.empty())
    edit.label = g_groupLabel;
  edit.nTime = static_cast<long long>(time(nullptr));
  const bool bOk = Apply(edit, true, pWhy);
  if (bOk) {
    // what was undone is gone once something new is done
    if (g_nAt < static_cast<int>(g_history.size())) {
      g_history.resize(static_cast<size_t>(g_nAt));
      if (g_nSavedAt > g_nAt)
        g_nSavedAt = -1;
    }
    g_history.push_back(edit);
    g_nAt = static_cast<int>(g_history.size());
    HistoryAppend(edit);
    ++g_nChange;
  }
  if (bOwn)
    StudioGroupEnd();
  return bOk;
}

void StudioGroupEnd(void) {
  g_lastMerge = g_groupMerge;
  g_qwLastMerge = GetTickCount64();
  g_nGroup = 0;
}

bool StudioUndo(std::string *pWhy) {
  if (g_nAt == 0) {
    *pWhy = "nothing to undo";
    return false;
  }
  const int nGroup = g_history[static_cast<size_t>(g_nAt) - 1].nGroup;
  while (g_nAt > 0 && g_history[static_cast<size_t>(g_nAt) - 1].nGroup == nGroup) {
    if (!Apply(g_history[static_cast<size_t>(g_nAt) - 1], false, pWhy))
      return false;
    --g_nAt;
  }
  g_lastMerge.clear();
  ++g_nChange;
  return true;
}

bool StudioRedo(std::string *pWhy) {
  if (g_nAt >= static_cast<int>(g_history.size())) {
    *pWhy = "nothing to redo";
    return false;
  }
  const int nGroup = g_history[static_cast<size_t>(g_nAt)].nGroup;
  while (g_nAt < static_cast<int>(g_history.size()) && g_history[static_cast<size_t>(g_nAt)].nGroup == nGroup) {
    if (!Apply(g_history[static_cast<size_t>(g_nAt)], true, pWhy))
      return false;
    ++g_nAt;
  }
  g_lastMerge.clear();
  ++g_nChange;
  return true;
}

bool StudioSetField(int t, const std::string &name, const std::string &key, const PortJson &value,
                    const std::string &sender, const std::string &merge, std::string *pWhy) {
  if (!TypesPath(t, name, pWhy))
    return false;
  if (key.empty() || key == "base" || key == "edit") {
    *pWhy = key.empty() ? "a field needs a name" : key + " is set by making the record, not as a field";
    return false;
  }
  StudioRecord rec;
  if (!StudioRecordOf(t, name, &rec) || (rec.kind != SK_OWN && rec.kind != SK_EDIT)) {
    *pWhy = std::string(StudioTableName(t)) + " " + name +
            (rec.kind == SK_DEPENDENCY ? " is a mod's this one depends on: edit it in the mod" : " is not the mod's: edit it in the mod first");
    return false;
  }
  const std::vector<std::string> path = {StudioTableName(t), name, key};
  if (PortJsonWrite(At("types", path)) == PortJsonWrite(value))
    return true;
  Group g(sender, name + "." + key + " set", merge);
  return StudioEditApply(MemberEdit("types", path, value), pWhy);
}

bool StudioRevertField(int t, const std::string &name, const std::string &key, const std::string &sender,
                       std::string *pWhy) {
  if (!TypesPath(t, name, pWhy))
    return false;
  const std::vector<std::string> path = {StudioTableName(t), name, key};
  if (At("types", path).kind == PortJson::NONE)
    return true;
  Group g(sender, name + "." + key + " back to its base's");
  return StudioEditApply(MemberEdit("types", path, PortJson()), pWhy);
}

bool StudioCreate(int t, const std::string &name, const PortJson &record, const std::string &sender,
                  std::string *pWhy) {
  if (!TypesPath(t, name, pWhy))
    return false;
  if (record.kind != PortJson::OBJECT) {
    *pWhy = "a record is an object";
    return false;
  }
  for (char c : name)
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_')) {
      *pWhy = "a record's name takes letters, digits and _";
      return false;
    }
  if (StudioRecordJson(t, name).kind != PortJson::NONE) {
    *pWhy = std::string(StudioTableName(t)) + " has a " + name + " already";
    return false;
  }
  if (record["edit"].kind == PortJson::NONE && StudioClassicRow(t, name) >= 0) {
    *pWhy = name + " is a classic row's name: a record of that name would hide it";
    return false;
  }
  Group g(sender, std::string("made ") + StudioTableName(t) + " " + name);
  const std::vector<std::string> path = {StudioTableName(t), name};
  return MakeParents("types", path, pWhy) && StudioEditApply(MemberEdit("types", path, record), pWhy);
}

bool StudioPut(const std::string &doc, const std::vector<std::string> &path, const PortJson &value,
               const std::string &sender, const std::string &label, std::string *pWhy) {
  if (!g_bOpen) {
    *pWhy = "no mod is open";
    return false;
  }
  if (path.empty() || DocOf(doc) == nullptr) {
    *pWhy = "studio_put needs a document and a path in it";
    return false;
  }
  Group g(sender, label);
  return (value.kind == PortJson::NONE || MakeParents(doc, path, pWhy)) && StudioEditApply(MemberEdit(doc, path, value), pWhy);
}

bool StudioDelete(int t, const std::string &name, const std::string &sender, std::string *pWhy) {
  if (!TypesPath(t, name, pWhy))
    return false;
  const std::vector<std::string> path = {StudioTableName(t), name};
  if (At("types", path).kind == PortJson::NONE) {
    *pWhy = std::string(StudioTableName(t)) + " " + name + " is not the mod's";
    return false;
  }
  Group g(sender, std::string("took away ") + StudioTableName(t) + " " + name);
  return StudioEditApply(MemberEdit("types", path, PortJson()), pWhy);
}

bool StudioSetRaw(int t, const std::string &name, const std::string &text, const std::string &sender,
                  std::string *pWhy) {
  if (!TypesPath(t, name, pWhy))
    return false;
  PortJson j;
  std::string error;
  if (!PortJsonParse(text.c_str(), &j, &error)) {
    *pWhy = StudioJsonWhere(text, error);
    return false;
  }
  if (j.kind != PortJson::OBJECT) {
    *pWhy = "a record is an object";
    return false;
  }
  const std::vector<std::string> path = {StudioTableName(t), name};
  if (At("types", path).kind == PortJson::NONE) {
    *pWhy = std::string(StudioTableName(t)) + " " + name + " is not the mod's";
    return false;
  }
  if (PortJsonWrite(At("types", path)) == PortJsonWrite(j))
    return true;
  Group g(sender, name + " written as text");
  return StudioEditApply(MemberEdit("types", path, j), pWhy);
}

bool StudioSetScript(const std::string &text, const std::string &sender, std::string *pWhy) {
  if (!g_bOpen) {
    *pWhy = "no mod is open";
    return false;
  }
  if (text == g_open.script)
    return true;
  StudioEdit e;
  e.doc = "iscript";
  e.before = StudioJsonString(g_open.script);
  e.after = StudioJsonString(text);
  Group g(sender, "the image script written", "iscript");
  return StudioEditApply(e, pWhy);
}

void StudioClaudeJoin(bool bOn, bool bPending, int nJoin) {
  g_bJoinClaude = bOn;
  g_bJoinPending = bOn && bPending;
  if (bOn)
    g_nClaudeGroup = nJoin;
}

int StudioClaudeGroup(void) {
  return g_nClaudeGroup;
}

int StudioPending(void) {
  return PendingFrom() >= 0 ? g_nPending : 0;
}

void StudioPendingSet(int nGroup) {
  g_nPending = nGroup;
}

void StudioGroupAside(int nGroup, bool bAside) {
  if (nGroup == 0 || g_nAt == 0 || g_history[static_cast<size_t>(g_nAt) - 1].nGroup != nGroup ||
      bAside == (g_nAsideGroup == nGroup))
    return;
  int nFrom = g_nAt;
  while (nFrom > 0 && g_history[static_cast<size_t>(nFrom) - 1].nGroup == nGroup)
    --nFrom;
  std::string why;
  if (bAside)
    for (int i = g_nAt; i-- > nFrom;)
      Apply(g_history[static_cast<size_t>(i)], false, &why);
  else
    for (int i = nFrom; i < g_nAt; ++i)
      Apply(g_history[static_cast<size_t>(i)], true, &why);
  g_nAsideGroup = bAside ? nGroup : 0;
}

const PortJson &StudioDocAccepted(const std::string &doc) {
  const int nGroup = StudioPending();
  if (nGroup == 0)
    return StudioDoc(doc);
  // worked out again when the model changes or another proposal waits
  struct Kept {
    unsigned nChange = 0;
    int nGroup = 0;
    PortJson doc;
  };
  static std::map<std::string, Kept> s_kept;
  Kept &k = s_kept[doc];
  if (k.nChange != g_nChange || k.nGroup != nGroup) {
    StudioGroupAside(nGroup, true);
    k.doc = StudioDoc(doc);
    StudioGroupAside(nGroup, false);
    k.nChange = g_nChange;
    k.nGroup = nGroup;
  }
  return k.doc;
}

void StudioModelInit(void) {
}

void StudioPoll(void) {
  // the mod open when the studio last closed, opened again once the game's files are there
  static bool bTried;
  if (!bTried && !g_bOpen && !StudioFields(ST_UNITS).empty()) {
    bTried = true;
    char sz[512] = "";
    std::string why;
    if (PortSettingString("studio", "mod", sz, sizeof sz) && sz[0] != '\0')
      StudioOpen(sz, &why);
  }
  StudioAutosave(false);
}
