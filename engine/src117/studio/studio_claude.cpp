// studio_claude.cpp -- the Claude panel: the modder's own Claude Code asked in words. It runs
// headless as a child process (claude -p, stream-json) with one MCP server, hyperion-studio
// (tools/control/hyperion_mcp.py) pointed at this studio's channel and giving only the studio's
// tools Claude may use here; its answer and tool calls stream into the dock's Claude tab. Claude's
// edits of one ask are one group (StudioClaudeJoin): in Show me first that group is a proposal,
// shown as a diff to accept or undo, which no save or push carries until it is accepted; in Let it
// edit it is made at once and undone as one. Conversations are kept per mod in .studio\claude\ and
// continued with --resume.
#include <windows.h>
#include <float.h>
#include <time.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include "../design/design_engine.h"      // DesignCall
#include "../port/platform/port_paths.h"  // PortProgramFolder
#include "../port/platform/port_settings.h"
#include "../port/platform/port_util.h"   // PortReadFile
#include "../port/tools/port_control.h"   // PortControlCommand, PortControlPort
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// ---- what Claude may use ------------------------------------------------------------------------------------

// The studio's tools the panel's Claude is given: reading the mod, editing it through the same
// commands as the panels, and pointing the window at a record. Not saving, pushing, undoing the
// modder's work, starting games or reading and writing files outside the mod.
const char *const kAllowed[] = {
    "studio_status", "studio_list", "studio_get", "studio_history", "studio_files", "studio_damage", "studio_actions",
    "studio_statuses", "studio_scenarios", "studio_matchup", "studio_races", "studio_card", "studio_strings",
    "studio_languages", "studio_translations", "studio_unit_art", "studio_art_anim", "studio_race_rules",
    "studio_tilesets", "studio_tileset", "studio_mod_summary", "studio_verify", "studio_unit_portrait",
    "studio_set", "studio_create", "studio_delete", "studio_raw", "studio_put", "studio_make_unit",
    "studio_make_action", "studio_delete_action", "studio_action_research", "studio_status_new", "studio_status_set",
    "studio_status_delete", "studio_weapon_status", "studio_unit_aura", "studio_action_blocks", "studio_block_add",
    "studio_block_set", "studio_block_remove", "studio_block_move", "studio_unit_sound_remove", "studio_look_set",
    "studio_look_unit", "studio_hud_set", "studio_race_set", "studio_card_set", "studio_card_move",
    "studio_card_train", "studio_card_build", "studio_string_set", "studio_strings_replace",
    "studio_translation_set", "studio_race_fix", "studio_tile_set", "studio_show", "studio_races_show",
    "studio_looks_show", "studio_mod_show", "studio_tilesets_show", "studio_art_view", "studio_art_frame",
    "studio_mode", "studio_mode_ai", "studio_mode_seen", "studio_mode_preview", "studio_mode_roll",
    "studio_mode_set", "studio_mode_setting", "studio_mode_setting_delete", "studio_mode_preset",
    "studio_mode_preset_delete", "studio_modes_show",
    "studio_marks", "studio_mark", "studio_mark_set",
    "studio_readout", "studio_readout_set", "studio_readout_fx",
};
// Shown to Claude but refused when it calls them, so it says plainly that the modder does these.
const char *const kShownOnly[] = {"studio_save", "studio_push", "studio_undo", "studio_redo"};
const char *const kServer = "hyperion-studio";

const char *const kSystem =
    "You are Claude, working inside Hyperion Studio, the program modders make Hyperion mods in. Hyperion is a "
    "port of the classic StarCraft engine. A mod's data is a types.json of records in tables (units, weapons, "
    "flingy, sprites, images, orders, techs, upgrades, sounds, portraits): each record is the mod's own, made "
    "from a base it starts from, or an edit of a classic row. You see and change the open mod only through the "
    "studio's tools (mcp__hyperion-studio__studio_*), the same commands the modder's panels use; each edit you "
    "make is marked as yours in the studio's history. Read a record with studio_get before changing it: it gives "
    "each field's value and where the value comes from. Set a field with studio_set, in the units studio_get "
    "shows, as JSON numbers (54, not \"54\"), and round to whole numbers. Do only what is asked, with as few tool calls as it takes. Do not save, "
    "push or start games: the modder does that. Some tools are not given to you here; when one is refused, say "
    "so plainly and do not look for a way round it. Each message starts with what the studio shows now, in "
    "brackets. Answer in a few plain sentences: what you changed, from what to what.";

// ---- the conversation as the panel shows it -----------------------------------------------------------------

enum { IT_YOU, IT_CLAUDE, IT_TOOL, IT_NOTE, IT_ERROR, IT_DONE };
enum { TS_RUNNING, TS_DONE, TS_REFUSED, TS_NOT_GIVEN };
const char *const kKinds[] = {"you", "claude", "tool", "note", "error", "done"};
const char *const kStates[] = {"running", "done", "refused", "not given"};

struct Item {
  int kind = IT_NOTE;
  std::string text;    // the words; a tool call's line
  std::string tool;    // a tool call's name, without the server's prefix
  std::string id;      // its tool_use id
  int state = TS_RUNNING;
  std::string detail;  // why it was refused
  bool bLive = false;  // Claude's words still coming
};

struct Conversation {
  std::string id;  // Claude Code's session id
  std::string title;
  long long nStarted = 0, nLast = 0;
  int nAsks = 0;
};

// what the reader thread hands the panel
struct Pipe {
  std::mutex m;
  std::deque<std::string> lines;
  bool bEof = false;
};

std::vector<Item> g_items;
std::vector<Conversation> g_convs;
std::string g_conv;       // the conversation shown and continued, "" for a new one
std::string g_modFolder;  // the mod whose conversations these are
int g_nMode = 0;          // 0 Show me first, 1 Let it edit
int g_nModel = 0;         // 0 Claude Code's own, then the aliases below
const char *const kModels[] = {"Its default", "Sonnet", "Opus", "Haiku"};
const char *const kModelArgs[] = {"", "sonnet", "opus", "haiku"};
bool g_abChip[3] = {true, true, true};  // what Claude sees: the mod, the open record, the workspace
std::string g_input;
std::string g_program;  // the Claude Code run instead of the one found (studio_claude_use), "" for none
bool g_bFocusInput;

// the ask running
HANDLE g_hProcess, g_hJob;
std::shared_ptr<Pipe> g_pipe;
bool g_bRunning, g_bStopped, g_bResult, g_bExited;
ULONGLONG g_qwAsked;
int g_nAskGroupFrom;  // the history's size when the ask began
int g_nTools;
std::string g_lastText;  // a line of the program's that was not JSON, said when it ends without an answer

const char *const kWsNames[] = {"Make", "Tables", "Art", "Looks", "Effects", "Cards and races", "Test", "Checks",
                                "The mod's page", "Strings", "Balance", "Statuses", "Tilesets"};

std::string Utf8(const std::wstring &w) {
  if (w.empty())
    return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
  return s;
}

std::wstring W16(const std::string &s) {
  if (s.empty())
    return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), NULL, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

bool There(const std::string &path) {
  const DWORD a = GetFileAttributesW(W16(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string Env(const char *pszName) {
  char sz[1024];
  const DWORD n = GetEnvironmentVariableA(pszName, sz, sizeof sz);
  return n > 0 && n < sizeof sz ? std::string(sz) : std::string();
}

std::string OnPath(const wchar_t *pszExe) {
  wchar_t sz[MAX_PATH];
  return SearchPathW(NULL, pszExe, NULL, MAX_PATH, sz, NULL) ? Utf8(sz) : std::string();
}

// The modder's Claude Code: the stand-in given, else claude on the PATH or where its installers put it.
std::string ClaudeProgram(void) {
  if (g_program == "none")
    return std::string();  // as if none were installed, to see what the panel says then
  if (!g_program.empty())
    return There(g_program) ? g_program : std::string();
  std::string p = OnPath(L"claude.exe");
  if (!p.empty())
    return p;
  for (const std::string &c : {Env("USERPROFILE") + "\\.local\\bin\\claude.exe", Env("APPDATA") + "\\npm\\claude.cmd",
                               Env("LOCALAPPDATA") + "\\Programs\\claude\\claude.exe"})
    if (There(c))
      return c;
  return OnPath(L"claude.cmd");
}

// The studio's MCP server, from the studio's folder up: beside it, or in the engine's tools.
std::string McpScript(void) {
  std::string dir = PortProgramFolder();
  if (!dir.empty() && dir.back() != '\\')
    dir += '\\';
  for (int i = 0; i < 6 && dir.size() > 3; ++i) {
    for (const char *psz : {"tools\\control\\hyperion_mcp.py", "engine\\tools\\control\\hyperion_mcp.py"})
      if (There(dir + psz))
        return dir + psz;
    const size_t at = dir.find_last_of('\\', dir.size() - 2);
    if (at == std::string::npos)
      break;
    dir = dir.substr(0, at + 1);
  }
  return std::string();
}

std::string Python(void) {
  const std::string p = OnPath(L"python.exe");
  return p.empty() ? OnPath(L"py.exe") : p;
}

// What stops an ask before it starts, in words for the panel; "" when nothing does.
std::string MissingNow(void) {
  if (ClaudeProgram().empty())
    return g_program.empty() || g_program == "none" ? "Claude Code was not found"
                                                    : "the Claude Code given (" + g_program + ") is not there";
  if (McpScript().empty())
    return "the studio's MCP server, tools\\control\\hyperion_mcp.py, is not beside the studio";
  if (Python().empty())
    return "Python was not found: the studio's MCP server runs in it (with its mcp package)";
  return std::string();
}

// looked for again every two seconds, so an install is seen without a restart
std::string Missing(void) {
  static std::string s_missing, s_program = "-";
  static ULONGLONG s_qw;
  if (s_program != g_program || GetTickCount64() - s_qw > 2000) {
    s_missing = MissingNow();
    s_program = g_program;
    s_qw = GetTickCount64();
  }
  return s_missing;
}

std::string Folder(void) {
  return StudioModFolder() + ".studio\\claude\\";
}

std::string NewId(void) {
  std::random_device rd;
  unsigned char b[16];
  for (unsigned char &c : b)
    c = static_cast<unsigned char>(rd() & 0xFF);
  b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
  b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
  char sz[40];
  snprintf(sz, sizeof sz, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3],
           b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return sz;
}

bool WriteText(const std::string &path, const std::string &text) {
  FILE *f = _wfopen(W16(path).c_str(), L"wb");
  if (f == nullptr)
    return false;
  const bool bOk = fwrite(text.data(), 1, text.size(), f) == text.size();
  return fclose(f) == 0 && bOk;
}

// ---- conversations, kept per mod ------------------------------------------------------------------------------

std::string ItemsJson(void) {
  std::string out;
  for (const Item &it : g_items)
    out += std::string(out.empty() ? "" : ",") + "{\"kind\":" + Q(kKinds[it.kind]) + ",\"text\":" + Q(it.text) +
           (it.kind == IT_TOOL ? ",\"tool\":" + Q(it.tool) + ",\"state\":" + Q(kStates[it.state]) +
                                     (it.detail.empty() ? std::string() : ",\"detail\":" + Q(it.detail))
                               : std::string()) +
           "}";
  return "[" + out + "]";
}

void SaveConversations(void) {
  if (!StudioIsOpen())
    return;
  CreateDirectoryW(W16(StudioModFolder() + ".studio").c_str(), NULL);
  CreateDirectoryW(W16(Folder()).c_str(), NULL);
  std::string list;
  for (const Conversation &c : g_convs)
    list += std::string(list.empty() ? "" : ",\n    ") + "{\"id\":" + Q(c.id) + ",\"title\":" + Q(c.title) +
            ",\"started\":" + std::to_string(c.nStarted) + ",\"last\":" + std::to_string(c.nLast) +
            ",\"asks\":" + std::to_string(c.nAsks) + "}";
  WriteText(Folder() + "conversations.json", "{\"conversations\": [\n    " + list + "]}\n");
  if (!g_conv.empty())
    WriteText(Folder() + g_conv + ".json", "{\"items\": " + ItemsJson() + "}\n");
}

void LoadItems(const std::string &id) {
  g_items.clear();
  std::string text;
  PortJson j;
  if (id.empty() || !PortReadFile(Folder() + id + ".json", &text) || !PortJsonParse(text.c_str(), &j))
    return;
  for (const PortJson &e : j["items"].items) {
    Item it;
    for (int k = 0; k < 6; ++k)
      if (e["kind"].Str() == kKinds[k])
        it.kind = k;
    it.text = e["text"].Str();
    it.tool = e["tool"].Str();
    it.detail = e["detail"].Str();
    it.state = TS_DONE;
    for (int s = 0; s < 4; ++s)
      if (e["state"].Str() == kStates[s])
        it.state = s;
    g_items.push_back(it);
  }
}

void LoadConversations(void) {
  g_convs.clear();
  g_items.clear();
  g_conv.clear();
  std::string text;
  PortJson j;
  if (!StudioIsOpen() || !PortReadFile(Folder() + "conversations.json", &text) || !PortJsonParse(text.c_str(), &j))
    return;
  for (const PortJson &e : j["conversations"].items) {
    Conversation c;
    c.id = e["id"].Str();
    c.title = e["title"].Str();
    c.nStarted = static_cast<long long>(e["started"].Num(0));
    c.nLast = static_cast<long long>(e["last"].Num(0));
    c.nAsks = static_cast<int>(e["asks"].Num(0));
    if (!c.id.empty())
      g_convs.push_back(c);
  }
}

Conversation *ConvOf(const std::string &id) {
  for (Conversation &c : g_convs)
    if (c.id == id)
      return &c;
  return nullptr;
}

// One conversation taken away, its transcript with it; Claude Code keeps its own copy of the session.
// The id is a copy: it may be the list's own, which the erase takes away.
void ClearConversation(const std::string id) {
  for (size_t i = 0; i < g_convs.size(); ++i)
    if (g_convs[i].id == id) {
      g_convs.erase(g_convs.begin() + static_cast<long long>(i));
      break;
    }
  DeleteFileW(W16(Folder() + id + ".json").c_str());
  if (g_conv == id) {
    g_conv.clear();
    g_items.clear();
  }
  SaveConversations();
}

// ---- what a tool call did, in words ------------------------------------------------------------------------------

std::string Bare(const std::string &tool) {
  const std::string pre = std::string("mcp__") + kServer + "__";
  return tool.compare(0, pre.size(), pre) == 0 ? tool.substr(pre.size()) : tool;
}

std::string RecordWords(const std::string &table, const std::string &name) {
  const int t = StudioTableOf(table.empty() ? "units" : table);
  return t >= 0 && !name.empty() ? StudioShowName(t, name) : name;
}

std::string ValueWords(const PortJson &v) {
  if (v.kind == PortJson::NONE)
    return "none";
  if (v.kind == PortJson::STRING)
    return v.s;
  std::string s = PortJsonWrite(v);
  if (s.size() > 60)
    s = s.substr(0, 57) + "...";
  return s;
}

std::string Spaced(std::string s) {
  for (char &c : s)
    if (c == '_')
      c = ' ';
  return s;
}

std::string ToolWords(const std::string &tool, const PortJson &in) {
  const std::string table = in["table"].Str("units"), name = in["name"].Str();
  if (tool == "studio_get")
    return "read " + RecordWords(table, name);
  if (tool == "studio_list")
    return "listed the mod's " + table;
  if (tool == "studio_set") {
    const std::string field = Spaced(in["field"].Str());
    if (in["revert"].kind == PortJson::BOOL && in["revert"].b)
      return "put " + RecordWords(table, name) + "'s " + field + " back to its base's";
    return "set " + RecordWords(table, name) + "'s " + field + " to " + ValueWords(in["value"]);
  }
  if (tool == "studio_create")
    return "made " + table + " " + name + (in["base"].Str().empty() ? std::string() : " from " + in["base"].Str());
  if (tool == "studio_delete")
    return "took away " + table + " " + name;
  if (tool == "studio_raw")
    return "rewrote " + RecordWords(table, name) + " as text";
  if (tool == "studio_make_unit")
    return "made the unit " + in["name"].Str() + " from " + RecordWords("units", in["base"].Str());
  if (tool == "studio_put") {
    std::string path;
    for (const PortJson &p : in["path"].items)
      path += (path.empty() ? "" : ".") + p.Str();
    return "set " + in["doc"].Str("types") + " " + path;
  }
  if (tool == "studio_show")
    return "showed " + (name.empty() ? in["workspace"].Str("the studio") : RecordWords(table, name));
  std::string words = Spaced(tool.compare(0, 7, "studio_") == 0 ? tool.substr(7) : tool);
  std::string args = in.kind == PortJson::OBJECT && !in.members.empty() ? PortJsonWrite(in) : std::string();
  if (args.size() > 90)
    args = args.substr(0, 87) + "...";
  return words + (args.empty() ? "" : " " + args);
}

// ---- Claude's edits as rows: record, field, before, after -------------------------------------------------------

struct Row {
  std::string doc;
  std::vector<std::string> path;
  PortJson before, after;
  std::string record, field, beforeWords, afterWords;
  std::string problem;  // why the record does not load as it is now, "" when it does
};

const PortJson &DocAt(const std::string &doc, const std::vector<std::string> &path) {
  static const PortJson kNone;
  static PortJson script;
  if (doc == "iscript") {
    script = StudioJsonString(StudioScript());
    return script;
  }
  const PortJson *p = &StudioDoc(doc);
  for (const std::string &k : path)
    p = &(*p)[k.c_str()];
  return *p;
}

bool Prefix(const std::vector<std::string> &a, const std::vector<std::string> &b) {
  return a.size() < b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// A field's value in words: the record's own, or, when it gives none, its base's with whose it is.
std::string Resolved(const Row &r, const PortJson &own) {
  if (r.doc == "types" && r.path.size() == 3 && own.kind == PortJson::NONE) {
    const int t = StudioTableOf(r.path[0]);
    StudioRecord rec;
    StudioValue v;
    if (t >= 0 && StudioRecordOf(t, r.path[1], &rec) && !rec.base.empty() && StudioGet(t, rec.base, r.path[2], &v) &&
        StudioFieldOf(t, r.path[2]) && StudioFieldOf(t, r.path[2])->nRef < 0 && !v.n.empty()) {
      std::string s;
      for (long long n : v.n)
        s += (s.empty() ? "" : " ") + std::to_string(n);
      return s + " (" + Short(t, rec.base) + "'s)";
    }
    if (t >= 0 && StudioRecordOf(t, r.path[1], &rec) && !rec.base.empty())
      return "its base's";
  }
  if (r.doc == "types" && r.path.size() == 3 && own.kind == PortJson::STRING) {
    const int t = StudioTableOf(r.path[0]);
    const StudioField *f = t >= 0 ? StudioFieldOf(t, r.path[2]) : nullptr;
    if (f && f->nColumn >= 0 && f->nRef < 0)
      return "\"" + own.s + "\", text";  // a number given as text, which the loader refuses
  }
  if (r.doc == "iscript") {
    int n = 1;
    for (char c : own.Str())
      n += c == '\n';
    return std::to_string(n) + " lines";
  }
  if (r.doc == "types" && r.path.size() == 2 && own.kind == PortJson::OBJECT) {
    std::string s;
    for (const PortJsonMember &m : own.members)
      s += (s.empty() ? "" : ", ") + Spaced(m.first) + " " + ValueWords(m.second);
    return s.size() > 120 ? s.substr(0, 117) + "..." : s;
  }
  return ValueWords(own);
}

// The changes a group made, the last done: each place it changed once, with what was there before it.
std::vector<Row> GroupRows(int nGroup) {
  std::vector<Row> rows;
  const std::vector<StudioEdit> &h = StudioHistory();
  const int nAt = StudioHistoryAt();
  if (nGroup == 0 || nAt == 0 || h[static_cast<size_t>(nAt) - 1].nGroup != nGroup)
    return rows;
  int nFrom = nAt;
  while (nFrom > 0 && h[static_cast<size_t>(nFrom) - 1].nGroup == nGroup)
    --nFrom;
  for (int i = nFrom; i < nAt; ++i) {
    const StudioEdit &e = h[static_cast<size_t>(i)];
    bool bHave = false;
    for (const Row &r : rows)
      bHave |= r.doc == e.doc && r.path == e.path;
    if (!bHave) {
      Row r;
      r.doc = e.doc;
      r.path = e.path;
      rows.push_back(r);
    }
  }
  // a place inside another the group changed is part of that one's row
  std::vector<Row> kept;
  for (const Row &r : rows) {
    bool bInside = false;
    for (const Row &o : rows)
      bInside |= o.doc == r.doc && Prefix(o.path, r.path);
    if (!bInside)
      kept.push_back(r);
  }
  for (Row &r : kept) {
    r.after = DocAt(r.doc, r.path);
    r.afterWords = Resolved(r, r.after);
    // a record the change leaves the loader refusing, named before it is accepted
    const int t = r.doc == "types" && r.path.size() >= 2 ? StudioTableOf(r.path[0]) : -1;
    std::vector<StudioValue> values;
    std::string why;
    if (t >= 0 && StudioRecordJson(t, r.path[1]).kind != PortJson::NONE && !StudioRow(t, r.path[1], &values, &why))
      r.problem = why.empty() ? "it does not load" : why;
  }
  // a record's problem on the row of the field it names, else on its first row
  for (Row &r : kept) {
    if (r.problem.empty() || r.path.size() < 3 || r.problem.find("." + r.path[2]) != std::string::npos)
      continue;
    bool bNamed = false, bFirst = true;
    for (const Row &o : kept)
      if (&o != &r && o.doc == r.doc && o.path.size() >= 3 && o.path[0] == r.path[0] && o.path[1] == r.path[1]) {
        bNamed |= o.problem.find("." + o.path[2]) != std::string::npos;
        if (&o < &r)
          bFirst = false;
      }
    if (bNamed || !bFirst)
      r.problem.clear();
  }
  StudioGroupAside(nGroup, true);
  for (Row &r : kept) {
    r.before = DocAt(r.doc, r.path);
    r.beforeWords = Resolved(r, r.before);
  }
  StudioGroupAside(nGroup, false);
  std::vector<Row> out;
  for (Row &r : kept) {
    if (PortJsonWrite(r.before) == PortJsonWrite(r.after))
      continue;
    if (r.doc == "types" && r.path.size() >= 2) {
      r.record = RecordWords(r.path[0], r.path[1]);
      for (size_t k = 2; k < r.path.size(); ++k)
        r.field += (k > 2 ? "." : "") + Spaced(r.path[k]);
      if (r.path.size() == 2)
        r.field = r.before.kind == PortJson::NONE ? "made" : r.after.kind == PortJson::NONE ? "taken away" : "the whole record";
      if (StudioTableOf(r.path[0]) < 0)
        r.record = Spaced(r.path[0]) + " of " + RecordWords("units", r.path[1]);
    } else if (r.doc == "iscript") {
      r.record = "the image script";
    } else {
      r.record = r.doc + (r.path.empty() ? "" : " " + r.path[0]);
      for (size_t k = 1; k < r.path.size(); ++k)
        r.field += (k > 1 ? "." : "") + r.path[k];
    }
    out.push_back(r);
  }
  return out;
}

// the rows, worked out again only when the model changes
const std::vector<Row> &Rows(int *pnGroup) {
  static std::vector<Row> s_rows;
  static unsigned s_nChange;
  static int s_nGroup = -1;
  // the proposal; else, once an ask is over, Claude's last edits while they are the last done
  const int nGroup = StudioPending() ? StudioPending() : !g_bRunning ? StudioClaudeGroup() : 0;
  if (s_nChange != StudioChange() || s_nGroup != nGroup) {
    s_nChange = StudioChange();
    s_nGroup = nGroup;
    s_rows = GroupRows(nGroup);
  }
  if (pnGroup)
    *pnGroup = s_rows.empty() ? 0 : nGroup;
  return s_rows;
}

bool PutRow(const Row &r, const std::string &label, std::string *pWhy) {
  if (r.doc == "iscript")
    return StudioSetScript(r.after.Str(), "claude", pWhy);
  return StudioPut(r.doc, r.path, r.after, "claude", label, pWhy);
}

// Some of the proposal's rows accepted (made a group of their own, done), the rest kept waiting:
// the proposal undone and made again from its rows. Rows whose place is the whole document cannot
// be split from the rest.
bool Split(const std::vector<Row> &rows, const std::vector<int> &accepted, const std::vector<int> &dropped, std::string *pWhy) {
  for (const Row &r : rows)
    if (r.path.empty() && r.doc != "iscript") {
      *pWhy = "this proposal replaces a whole document: accept it or undo it all";
      return false;
    }
  auto has = [](const std::vector<int> &v, int i) { return std::find(v.begin(), v.end(), i) != v.end(); };
  std::string label = "Claude's proposal";
  for (const StudioEdit &e : StudioHistory())
    if (e.nGroup == StudioPending())
      label = e.label;
  if (!StudioUndo(pWhy))
    return false;
  StudioPendingSet(0);
  bool bOk = true;
  if (!accepted.empty()) {
    StudioGroupBegin("claude", "Claude's change accepted: " + rows[static_cast<size_t>(accepted[0])].record);
    for (int i : accepted)
      bOk = bOk && PutRow(rows[static_cast<size_t>(i)], "Claude's change accepted", pWhy);
    StudioGroupEnd();
  }
  bool bAny = false;
  StudioGroupBegin("claude", label);
  for (size_t i = 0; i < rows.size(); ++i)
    if (!has(accepted, static_cast<int>(i)) && !has(dropped, static_cast<int>(i))) {
      bOk = bOk && PutRow(rows[i], label, pWhy);
      bAny = true;
    }
  StudioGroupEnd();
  if (bAny && StudioHistoryAt() > 0)
    StudioPendingSet(StudioHistory()[static_cast<size_t>(StudioHistoryAt()) - 1].nGroup);
  return bOk;
}

// ---- the child process ------------------------------------------------------------------------------------------

// An argument as the C runtime reads a command line back (CommandLineToArgvW's rules).
std::wstring Arg(const std::string &s) {
  const std::wstring w = W16(s);
  if (!w.empty() && w.find_first_of(L" \t\n\"") == std::wstring::npos)
    return w;
  std::wstring out = L"\"";
  size_t nSlash = 0;
  for (wchar_t c : w) {
    if (c == L'\\') {
      ++nSlash;
      continue;
    }
    if (c == L'"')
      out.append(nSlash * 2 + 1, L'\\');
    else
      out.append(nSlash, L'\\');
    nSlash = 0;
    out += c;
  }
  out.append(nSlash * 2, L'\\');
  return out + L"\"";
}

void Reader(std::shared_ptr<Pipe> pipe, HANDLE hRead) {
  std::string buf;
  char chunk[4096];
  DWORD n = 0;
  while (ReadFile(hRead, chunk, sizeof chunk, &n, NULL) && n > 0) {
    buf.append(chunk, n);
    size_t at;
    while ((at = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, at);
      buf.erase(0, at + 1);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      std::lock_guard<std::mutex> lock(pipe->m);
      pipe->lines.push_back(line);
    }
  }
  CloseHandle(hRead);
  std::lock_guard<std::mutex> lock(pipe->m);
  if (!buf.empty())
    pipe->lines.push_back(buf);
  pipe->bEof = true;
}

// Claude's words as the panel draws them: Markdown's emphasis and code marks left out.
std::string Plain(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if ((s[i] == '*' || s[i] == '_') && i + 1 < s.size() && s[i + 1] == s[i]) {
      ++i;
      continue;
    }
    if (s[i] == '`')
      continue;
    out += s[i];
  }
  return out;
}

void Note(int kind, const std::string &text) {
  Item it;
  it.kind = kind;
  it.text = text;
  g_items.push_back(it);
}

std::string Context(void) {
  std::string c;
  const PortJson &mod = StudioDoc("mod");
  if (g_abChip[0])
    c += "mod " + mod["name"].Str(StudioModId().c_str()) + " (" + StudioModId() + ")";
  const StudioOpenRecord &cur = StudioCurrent();
  if (g_abChip[1] && cur.t >= 0)
    c += std::string(c.empty() ? "" : "; ") + "open: " + StudioTableName(cur.t) + " " + cur.name + " (" +
         StudioShowName(cur.t, cur.name) + ")";
  const int nWs = StudioWorkspaceShown();
  if (g_abChip[2] && nWs >= 0 && nWs < 13)
    c += std::string(c.empty() ? "" : "; ") + "workspace: " + kWsNames[nWs];
  c += std::string(c.empty() ? "" : "; ") +
       (g_nMode == 0 ? "Show me first: your edits wait as one proposal the modder accepts or undoes"
                     : "Let it edit: your edits are made at once, undone as one");
  return "[The studio now: " + c + ".]\n\n";
}

bool Ask(const std::string &textIn, bool bNew, std::string *pWhy) {
  std::string text = textIn;
  while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
    text.pop_back();
  if (text.empty()) {
    *pWhy = "ask in words";
    return false;
  }
  if (g_bRunning) {
    *pWhy = "Claude is still answering: Stop it first";
    return false;
  }
  if (!StudioIsOpen()) {
    *pWhy = "no mod is open";
    return false;
  }
  if (g_nMode == 1 && StudioPending()) {
    *pWhy = "Claude's proposal is waiting: accept it or undo it before letting Claude edit";
    return false;
  }
  const std::string missing = Missing();
  if (!missing.empty()) {
    *pWhy = missing;
    return false;
  }
  if (bNew) {
    g_conv.clear();
    g_items.clear();
  }
  CreateDirectoryW(W16(StudioModFolder() + ".studio").c_str(), NULL);
  CreateDirectoryW(W16(Folder()).c_str(), NULL);
  // the MCP server, on this studio's channel, with only the tools above to show
  std::string tools;
  for (const char *psz : kAllowed)
    tools += std::string(tools.empty() ? "" : ",") + psz;
  for (const char *psz : kShownOnly)
    tools += std::string(",") + psz;
  const std::string config = "{\"mcpServers\": {" + Q(kServer) + ": {\"command\": " + Q(Python()) + ", \"args\": [" +
                             (Python().size() > 6 && Lower(Python()).substr(Python().size() - 6) == "py.exe" ? "\"-3\", " : "") +
                             Q(McpScript()) + "], \"env\": {\"HYPERION_CONTROL_PORT\": " + Q(std::to_string(PortControlPort())) +
                             ", \"HYPERION_MCP_TOOLS\": " + Q(tools) + "}}}}\n";
  if (!WriteText(Folder() + "mcp.json", config)) {
    *pWhy = "could not write " + Folder() + "mcp.json";
    return false;
  }
  std::string allowed;
  for (const char *psz : kAllowed)
    allowed += std::string(allowed.empty() ? "" : ",") + "mcp__" + kServer + "__" + psz;
  const bool bResume = !g_conv.empty();
  const std::string id = bResume ? g_conv : NewId();
  const std::string program = ClaudeProgram();
  std::wstring line;
  const size_t nDot = program.find_last_of(".\\/");
  const std::string ext = Lower(nDot != std::string::npos && program[nDot] == '.' ? program.substr(nDot) : std::string());
  if (ext == ".cmd" || ext == ".bat")
    line = L"cmd.exe /d /s /c \"" + Arg(program);
  else if (ext == ".py")
    line = Arg(Python()) + L" " + Arg(program);
  else
    line = Arg(program);
  std::vector<std::string> args = {"-p", "--output-format", "stream-json", "--verbose", "--include-partial-messages",
                                   "--mcp-config", Folder() + "mcp.json", "--strict-mcp-config", "--tools", "",
                                   "--allowedTools", allowed, "--permission-mode", "dontAsk", "--setting-sources", "project",
                                   "--disable-slash-commands", "--system-prompt", kSystem, bResume ? "--resume" : "--session-id", id};
  if (kModelArgs[g_nModel][0]) {
    args.push_back("--model");
    args.push_back(kModelArgs[g_nModel]);
  }
  for (const std::string &a : args)
    line += L" " + (a.empty() ? std::wstring(L"\"\"") : Arg(a));
  if (ext == ".cmd" || ext == ".bat")
    line += L"\"";
  // its pipes: the prompt in, its events (and its errors) out; only those handles go to it
  SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
  HANDLE hOutR = NULL, hOutW = NULL, hInR = NULL, hInW = NULL;
  if (!CreatePipe(&hOutR, &hOutW, &sa, 0) || !CreatePipe(&hInR, &hInW, &sa, 0)) {
    *pWhy = "could not make the pipes to Claude Code";
    return false;
  }
  SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(hInW, HANDLE_FLAG_INHERIT, 0);
  SIZE_T nAttr = 0;
  InitializeProcThreadAttributeList(NULL, 1, 0, &nAttr);
  std::vector<char> attr(nAttr);
  LPPROC_THREAD_ATTRIBUTE_LIST pAttr = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr.data());
  HANDLE ahInherit[2] = {hInR, hOutW};
  InitializeProcThreadAttributeList(pAttr, 1, 0, &nAttr);
  UpdateProcThreadAttribute(pAttr, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, ahInherit, sizeof ahInherit, NULL, NULL);
  STARTUPINFOEXW si = {};
  si.StartupInfo.cb = sizeof si;
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = hInR;
  si.StartupInfo.hStdOutput = hOutW;
  si.StartupInfo.hStdError = hOutW;
  si.lpAttributeList = pAttr;
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> cmd(line.begin(), line.end());
  cmd.push_back(L'\0');
  const std::wstring cwd = W16(Folder());
  const BOOL bStarted = CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, NULL,
                                       cwd.c_str(), &si.StartupInfo, &pi);
  const DWORD dwError = GetLastError();
  DeleteProcThreadAttributeList(pAttr);
  CloseHandle(hInR);
  CloseHandle(hOutW);
  if (!bStarted) {
    CloseHandle(hOutR);
    CloseHandle(hInW);
    *pWhy = "Claude Code did not start (error " + std::to_string(dwError) + "): " + program;
    return false;
  }
  // in a job, so Stop ends it with the MCP server it starts, and nothing outlives the studio
  g_hJob = CreateJobObjectA(NULL, NULL);
  if (g_hJob != NULL) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(g_hJob, JobObjectExtendedLimitInformation, &li, sizeof li) ||
        !AssignProcessToJobObject(g_hJob, pi.hProcess)) {
      CloseHandle(g_hJob);
      g_hJob = NULL;
    }
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  g_hProcess = pi.hProcess;
  // the prompt, with what the studio shows now
  const std::string prompt = Context() + text + "\n";
  DWORD nWritten = 0;
  WriteFile(hInW, prompt.data(), static_cast<DWORD>(prompt.size()), &nWritten, NULL);
  CloseHandle(hInW);
  g_pipe = std::make_shared<Pipe>();
  std::thread(Reader, g_pipe, hOutR).detach();
  g_bRunning = true;
  g_bStopped = g_bResult = g_bExited = false;
  g_qwAsked = GetTickCount64();
  g_nAskGroupFrom = static_cast<int>(StudioHistory().size());
  g_nTools = 0;
  g_lastText.clear();
  // the ask's edits one group; in Show me first, the waiting proposal grows
  StudioClaudeJoin(true, g_nMode == 0, g_nMode == 0 ? StudioPending() : 0);
  if (!bResume) {
    g_conv = id;
    Conversation c;
    c.id = id;
    c.title = text.size() > 70 ? text.substr(0, 67) + "..." : text;
    c.nStarted = static_cast<long long>(time(nullptr));
    g_convs.insert(g_convs.begin(), c);
  }
  if (Conversation *c = ConvOf(g_conv)) {
    ++c->nAsks;
    c->nLast = static_cast<long long>(time(nullptr));
  }
  Item you;
  you.kind = IT_YOU;
  you.text = text;
  g_items.push_back(you);
  SaveConversations();
  StudioLog("claude", "asked Claude Code (process " + std::to_string(pi.dwProcessId) + ")");
  return true;
}

void Stop(void) {
  if (!g_bRunning)
    return;
  g_bStopped = true;
  if (g_hJob != NULL)
    TerminateJobObject(g_hJob, 1);
  else if (g_hProcess != NULL)
    TerminateProcess(g_hProcess, 1);
}

Item *ToolItem(const std::string &id) {
  for (size_t i = g_items.size(); i-- > 0;)
    if (g_items[i].kind == IT_TOOL && g_items[i].id == id)
      return &g_items[i];
  return nullptr;
}

std::string ResultText(const PortJson &content) {
  if (content.kind == PortJson::STRING)
    return content.s;
  std::string s;
  for (const PortJson &b : content.items)
    if (b["type"].Str() == "text")
      s += b["text"].Str();
  return s;
}

void Event(const std::string &line) {
  PortJson j;
  if (line.empty() || line[0] != '{' || !PortJsonParse(line.c_str(), &j)) {
    if (!line.empty())
      g_lastText = line;
    return;
  }
  const std::string type = j["type"].Str();
  if (type == "system" && j["subtype"].Str() == "init") {
    for (const PortJson &s : j["mcp_servers"].items)
      if (s["name"].Str() == kServer && s["status"].Str() == "failed")
        Note(IT_ERROR, "The studio's tools did not connect (" + s["status"].Str() +
                           "): the MCP server needs Python with its mcp package, and this studio's channel.");
  } else if (type == "stream_event") {
    const PortJson &e = j["event"];
    if (e["type"].Str() == "content_block_start" && e["content_block"]["type"].Str() == "text") {
      Note(IT_CLAUDE, std::string());
      g_items.back().bLive = true;
    } else if (e["type"].Str() == "content_block_delta" && e["delta"]["type"].Str() == "text_delta") {
      if (g_items.empty() || !g_items.back().bLive) {
        Note(IT_CLAUDE, std::string());
        g_items.back().bLive = true;
      }
      g_items.back().text += e["delta"]["text"].Str();
    }
  } else if (type == "assistant") {
    for (const PortJson &b : j["message"]["content"].items) {
      if (b["type"].Str() == "text") {
        Item *pLive = nullptr;
        for (size_t i = g_items.size(); i-- > 0;)
          if (g_items[i].kind == IT_CLAUDE && g_items[i].bLive) {
            pLive = &g_items[i];
            break;
          }
        if (pLive) {
          pLive->text = b["text"].Str();
          pLive->bLive = false;
        } else if (!b["text"].Str().empty()) {
          Note(IT_CLAUDE, b["text"].Str());
        }
      } else if (b["type"].Str() == "tool_use") {
        Item it;
        it.kind = IT_TOOL;
        it.tool = Bare(b["name"].Str());
        it.id = b["id"].Str();
        it.text = ToolWords(it.tool, b["input"]);
        g_items.push_back(it);
        ++g_nTools;
      }
    }
  } else if (type == "user") {
    for (const PortJson &b : j["message"]["content"].items) {
      if (b["type"].Str() != "tool_result")
        continue;
      Item *it = ToolItem(b["tool_use_id"].Str());
      if (it == nullptr)
        continue;
      const std::string text = ResultText(b["content"]);
      PortJson answer;
      if (b["is_error"].kind == PortJson::BOOL && b["is_error"].b) {
        const bool bNotGiven = text.find("ermission") != std::string::npos;
        it->state = bNotGiven ? TS_NOT_GIVEN : TS_REFUSED;
        it->detail = bNotGiven ? "not given to Claude here: the modder does this" : text.substr(0, 300);
      } else if (PortJsonParse(text.c_str(), &answer) && answer["ok"].kind == PortJson::BOOL && !answer["ok"].b) {
        it->state = TS_REFUSED;
        it->detail = answer["error"].Str("refused");
      } else {
        it->state = TS_DONE;
      }
    }
  } else if (type == "system" && j["subtype"].Str() == "permission_denied") {
    if (Item *it = ToolItem(j["tool_use_id"].Str())) {
      it->state = TS_NOT_GIVEN;
      it->detail = "not given to Claude here: the modder does this";
    }
  } else if (type == "result") {
    g_bResult = true;
    for (Item &it : g_items)
      it.bLive = false;
    const bool bError = (j["is_error"].kind == PortJson::BOOL && j["is_error"].b) || j["subtype"].Str("success") != "success";
    if (bError) {
      std::string why = j["result"].Str();
      for (const PortJson &e : j["errors"].items)
        why += (why.empty() ? "" : "; ") + e.Str();
      Note(IT_ERROR, "Claude Code stopped: " + (why.empty() ? j["subtype"].Str("an error") : why));
    }
    char sz[96];
    const double dCost = j["total_cost_usd"].Num(0);
    snprintf(sz, sizeof sz, "%.0f s, %d tool call%s", j["duration_ms"].Num(0) / 1000.0, g_nTools, g_nTools == 1 ? "" : "s");
    std::string done = sz;
    if (dCost > 0.0) {
      snprintf(sz, sizeof sz, ", $%.3f", dCost);
      done += sz;
    }
    Note(IT_DONE, done);
  }
}

// The ask's end: the process gone and its output read.
void Finish(void) {
  DWORD dwCode = 0;
  GetExitCodeProcess(g_hProcess, &dwCode);
  CloseHandle(g_hProcess);
  g_hProcess = NULL;
  if (g_hJob != NULL) {
    CloseHandle(g_hJob);  // ends what the ask left running
    g_hJob = NULL;
  }
  g_bRunning = false;
  for (Item &it : g_items) {
    it.bLive = false;
    if (it.kind == IT_TOOL && it.state == TS_RUNNING) {
      it.state = TS_REFUSED;
      it.detail = g_bStopped ? "stopped before it answered" : "no answer came";
    }
  }
  if (g_bStopped)
    Note(IT_NOTE, "Stopped. What Claude changed before it stopped is kept, as one change.");
  else if (!g_bResult)
    Note(IT_ERROR, "Claude Code ended without an answer (exit " + std::to_string(dwCode) + ")" +
                       (g_lastText.empty() ? std::string(".") : ": " + g_lastText.substr(0, 300)));
  StudioClaudeJoin(false, false, 0);
  int nGroup = 0;
  const std::vector<Row> &rows = Rows(&nGroup);
  if (StudioPending() && !rows.empty())
    Note(IT_NOTE, std::to_string(rows.size()) + (rows.size() == 1 ? " change waits" : " changes wait") +
                      " for you: Accept or Undo them at the right.");
  SaveConversations();
  StudioLog("claude", g_bStopped ? "Claude Code stopped" : "Claude Code answered");
}

void Drain(void) {
  if (!g_bRunning || !g_pipe)
    return;
  std::deque<std::string> lines;
  bool bEof = false;
  {
    std::lock_guard<std::mutex> lock(g_pipe->m);
    lines.swap(g_pipe->lines);
    bEof = g_pipe->bEof;
  }
  for (const std::string &l : lines)
    Event(l);
  if (!g_bExited && WaitForSingleObject(g_hProcess, 0) == WAIT_OBJECT_0) {
    g_bExited = true;
    if (g_hJob != NULL)
      TerminateJobObject(g_hJob, 0);  // what it started holds its output open
  }
  if (bEof && g_bExited)
    Finish();
}

// Stopped, and a second at most for it to go.
void StopAndWait(void) {
  Stop();
  for (int i = 0; i < 50 && g_bRunning; ++i) {
    Sleep(20);
    Drain();
  }
}

void Tick(void) {
  // the mode last chosen, once the settings can be read (not yet at the start)
  static bool s_bMode;
  if (!s_bMode) {
    s_bMode = true;
    double d = 0.0;
    if (PortSettingNumber("studio", "claudeMode", &d))
      g_nMode = d > 0.5 ? 1 : 0;
  }
  // another mod opened: the ask stopped, and that mod's own conversations
  if (StudioModFolder() != g_modFolder) {
    StopAndWait();
    g_modFolder = StudioModFolder();
    LoadConversations();
    if (!g_convs.empty()) {
      g_conv = g_convs[0].id;
      LoadItems(g_conv);
    }
  }
  Drain();
}

// ---- drawing ---------------------------------------------------------------------------------------------------

float WrapH(const std::string &s, float fW, float fPx, int nFont = STUDIO_FONT_TEXT) {
  StudioPushFont(nFont, fPx);
  const float h = ImGui::CalcTextSize(s.c_str(), nullptr, false, fW).y;
  ImGui::PopFont();
  return h;
}

void WrapText(ImDrawList *d, ImVec2 p, float fW, const std::string &s, ImU32 col, float fPx, int nFont = STUDIO_FONT_TEXT) {
  StudioPushFont(nFont, fPx);
  d->AddText(ImGui::GetFont(), ImGui::GetFontSize(), p, col, s.c_str(), nullptr, fW);
  ImGui::PopFont();
}

void Transcript(ImVec2 a, ImVec2 b) {
  ImGui::SetCursorScreenPos(a);
  if (!ImGui::BeginChild("##claudelog", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::EndChild();
    return;
  }
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x - P(20.0f);
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.x += P(10.0f);
  p.y += P(8.0f);
  if (g_items.empty()) {
    WrapText(d, p, w,
             "Ask in words: a unit made or changed, a weapon balanced, why a fight goes the way it does. Claude reads "
             "and edits this mod through the studio's own commands, using your Claude Code. In Show me first its edits "
             "wait at the right as one change for you to accept; nothing is saved or pushed until you do.",
             C(TK_INK3), kStudioFsS);
    p.y += WrapH("x", w, kStudioFsS) * 4.0f;
  }
  for (const Item &it : g_items) {
    if (it.kind == IT_YOU || it.kind == IT_CLAUDE) {
      const bool bYou = it.kind == IT_YOU;
      const std::string text = it.text.empty() && it.bLive ? std::string("...") : bYou ? it.text : Plain(it.text);
      const float bw = bYou ? fminf(w * 0.86f, fmaxf(TW(STUDIO_FONT_TEXT, kStudioFsS, text) + P(22.0f), P(140.0f))) : w * 0.94f;
      const float th = WrapH(text, bw - P(20.0f), kStudioFsS);
      const ImVec2 ba(bYou ? p.x + w - bw : p.x, p.y), bb(ba.x + bw, p.y + th + P(30.0f));
      d->AddRectFilled(ba, bb, bYou ? C(TK_SEL, 0.55f) : C(TK_CLAUDE, 0.10f), P(4.0f));
      d->AddRect(ba, bb, bYou ? C(TK_LINE2) : C(TK_CLAUDE_RIM, 0.6f), P(4.0f));
      Wide(d, 8.5f, ba.x + P(10.0f), ba.y + P(6.0f), bYou ? C(TK_INK3) : C(TK_CLAUDE), bYou ? "You" : "Claude");
      WrapText(d, ImVec2(ba.x + P(10.0f), ba.y + P(21.0f)), bw - P(20.0f), text, C(TK_INK), kStudioFsS);
      p.y = bb.y + P(8.0f);
    } else if (it.kind == IT_TOOL) {
      static const int kInk[] = {TK_INK3, TK_GOOD, TK_BAD, TK_WARN};
      static const char *const kGlyph[] = {"again", "done", "clash", "x"};
      StudioGlyph(d, kGlyph[it.state], ImVec2(p.x + P(4.0f), p.y + P(1.0f)), P(13.0f), C(kInk[it.state]));
      const float tw = T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(24.0f), p.y, it.state == TS_RUNNING ? C(TK_INK3) : C(TK_INK2), it.text,
                         w - P(24.0f));
      if (it.state == TS_NOT_GIVEN || it.state == TS_REFUSED) {
        const std::string why = (it.state == TS_NOT_GIVEN ? "" : "refused: ") + it.detail;
        if (tw + P(40.0f) + TW(STUDIO_FONT_TEXT, kStudioFsXs, why) < w) {
          T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(36.0f) + tw, p.y, C(kInk[it.state]), why);
        } else {
          p.y += P(16.0f);
          const float h = WrapH(why, w - P(24.0f), kStudioFsXs);
          WrapText(d, ImVec2(p.x + P(24.0f), p.y), w - P(24.0f), why, C(kInk[it.state]), kStudioFsXs);
          p.y += h - P(16.0f);
        }
      }
      p.y += P(19.0f);
    } else {
      const int tok = it.kind == IT_ERROR ? TK_BAD : TK_INK3;
      const float h = WrapH(it.text, w, kStudioFsXs);
      if (it.kind == IT_DONE) {
        d->AddLine(ImVec2(p.x, p.y + P(7.0f)), ImVec2(p.x + P(16.0f), p.y + P(7.0f)), C(TK_LINE2));
        T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(22.0f), p.y, C(TK_INK3), it.text);
      } else {
        WrapText(d, p, w, it.text, C(tok), kStudioFsXs);
      }
      p.y += h + P(10.0f);
    }
  }
  if (g_bRunning) {
    const int nDots = static_cast<int>((GetTickCount64() / 400) % 4);
    const std::string s = "Claude is working" + std::string(static_cast<size_t>(nDots), '.') + "   " +
                          std::to_string((GetTickCount64() - g_qwAsked) / 1000) + " s";
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x, p.y, C(TK_CLAUDE), s);
    p.y += P(20.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  static size_t s_nSeen;
  static size_t s_nLen;
  const size_t nLen = g_items.empty() ? 0 : g_items.back().text.size();
  if (g_items.size() != s_nSeen || nLen != s_nLen || g_bRunning) {
    if (g_items.size() != s_nSeen || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - P(40.0f))
      ImGui::SetScrollHereY(1.0f);
    s_nSeen = g_items.size();
    s_nLen = nLen;
  }
  ImGui::EndChild();
}

int InputResize(ImGuiInputTextCallbackData *p) {
  if (p->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    std::string *s = static_cast<std::string *>(p->UserData);
    s->resize(static_cast<size_t>(p->BufTextLen));
    p->Buf = &(*s)[0];
  }
  return 0;
}

void AskNow(void) {
  if (g_input.find_first_not_of(" \n\r\t") == std::string::npos)
    return;
  StudioAsk("studio_claude_ask", "\"text\":" + Q(g_input));
  g_input.clear();
}

void Composer(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddLine(ImVec2(a.x, a.y + 0.5f), ImVec2(b.x, a.y + 0.5f), C(TK_ROW));
  const std::string missing = Missing();
  if (missing == "Claude Code was not found") {
    float y = a.y + P(8.0f);
    T(d, STUDIO_FONT_BOLD, kStudioFsS, a.x + P(10.0f), y, C(TK_WARN), "Claude Code was not found on this computer.");
    y += P(20.0f);
    WrapText(d, ImVec2(a.x + P(10.0f), y), b.x - a.x - P(140.0f),
             "The panel uses your own Claude Code. Install it from claude.com/claude-code (or: npm install -g "
             "@anthropic-ai/claude-code), run claude once to sign in, then look again. The rest of the studio works without it.",
             C(TK_INK2), kStudioFsXs);
    ImGui::SetCursorScreenPos(ImVec2(b.x - P(118.0f), a.y + P(10.0f)));
    StudioButton("##look", "Look again", "again", SB_PLAIN, 26.0f, 108.0f, kStudioFsXs);  // Missing() looks each frame
    return;
  }
  // what Claude will see, each a chip that is left out when clicked
  float x = a.x + P(10.0f);
  const float cy = a.y + P(7.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, cy + P(2.0f), C(TK_INK3), "Claude sees") + P(8.0f);
  const PortJson &mod = StudioDoc("mod");
  const StudioOpenRecord &cur = StudioCurrent();
  const int nWs = StudioWorkspaceShown();
  const std::string chips[3] = {
      StudioIsOpen() ? mod["name"].Str(StudioModId().c_str()) : std::string("no mod"),
      cur.t >= 0 ? StudioShowName(cur.t, cur.name) : std::string("no record open"),
      nWs >= 0 && nWs < 13 ? std::string(kWsNames[nWs]) : std::string("the studio")};
  for (int i = 0; i < 3; ++i) {
    const float cw = StudioChipW(chips[i].c_str(), 18.0f, 9.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, cy));
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##chip", ImVec2(cw, P(18.0f))))
      g_abChip[i] = !g_abChip[i];
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", g_abChip[i] ? "Claude is told this; click to leave it out" : "Left out; click to tell Claude");
    ImGui::PopID();
    StudioChip(d, ImVec2(x, cy), chips[i].c_str(), g_abChip[i] ? (i == 0 ? CH_CLAUDE : CH_PLAIN) : CH_PLAIN);
    if (!g_abChip[i])
      d->AddLine(ImVec2(x + P(4.0f), cy + P(9.0f)), ImVec2(x + cw - P(4.0f), cy + P(9.0f)), C(TK_INK3));
    x += cw + P(6.0f);
  }
  const char *pszNote = "Uses your Claude Code";
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, b.x - P(10.0f) - TW(STUDIO_FONT_TEXT, kStudioFsXs, pszNote), cy + P(2.0f), C(TK_INK3), pszNote);
  // the words, and Ask or Stop
  const float bw = P(96.0f);
  const ImVec2 ia(a.x + P(10.0f), a.y + P(32.0f)), ib(b.x - P(18.0f) - bw, b.y - P(8.0f));
  ImGui::SetCursorScreenPos(ia);
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, C(TK_WELL));
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK));
  ImGui::PushStyleColor(ImGuiCol_Border, C(TK_CLAUDE_RIM));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(8.0f), P(6.0f)));
  if (g_input.capacity() < 16)
    g_input.reserve(16);
  if (g_bFocusInput) {
    ImGui::SetKeyboardFocusHere();
    g_bFocusInput = false;
  }
  const bool bEnter = ImGui::InputTextMultiline("##ask", &g_input[0], g_input.capacity() + 1, ImVec2(ib.x - ia.x, ib.y - ia.y),
                                                ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue |
                                                    ImGuiInputTextFlags_CtrlEnterForNewLine,
                                                InputResize, &g_input);
  g_input.resize(strlen(g_input.c_str()));
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
  ImGui::PopFont();
  if (g_input.empty() && !ImGui::IsItemActive())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ia.x + P(9.0f), ia.y + P(7.0f), C(TK_INK3),
      "Ask Claude to make or change something in the mod.  Enter asks, Ctrl Enter starts a new line");
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(10.0f) - bw, ia.y));
  if (g_bRunning) {
    if (StudioButton("##stop", "Stop", "x", SB_PLAIN, 30.0f, 96.0f))
      StudioAsk("studio_claude_stop", "");
  } else if (StudioButton("##send", "Ask", "claude", SB_CLAUDE, 30.0f, 96.0f, kStudioFsS, !g_input.empty() && missing.empty()) ||
             (bEnter && missing.empty())) {
    AskNow();
  }
  if (!missing.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, b.x - P(10.0f) - bw, ia.y + P(36.0f), C(TK_WARN), missing, bw);
}

void Proposal(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddRectFilled(a, b, C(TK_PANE2, 0.5f));
  d->AddLine(ImVec2(a.x + 0.5f, a.y), ImVec2(a.x + 0.5f, b.y), C(TK_LINE));
  int nGroup = 0;
  const std::vector<Row> &rows = Rows(&nGroup);
  const bool bPending = StudioPending() != 0 && nGroup == StudioPending();
  const float x = a.x + P(12.0f);
  float y = a.y + P(8.0f);
  const std::string head = bPending ? "Claude's proposal" : nGroup ? "Claude's last edits" : "Claude's changes";
  const float hw = Wide(d, 10.0f, x, y + P(6.0f), C(bPending ? TK_CHANGED : TK_INK2), head);
  if (!rows.empty())
    StudioChip(d, ImVec2(x + hw + P(8.0f), y + P(3.0f)), std::to_string(rows.size()).c_str(), bPending ? CH_CHANGED : CH_CLAUDE);
  // the buttons for all of them
  float bx = b.x - P(10.0f);
  if (bPending) {
    const ImVec2 su = StudioButtonSize("Undo all", "undo", 26.0f, kStudioFsXs);
    bx -= su.x;
    ImGui::SetCursorScreenPos(ImVec2(bx, y));
    if (StudioButton("##undoall", "Undo all", "undo", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, !g_bRunning))
      StudioAsk("studio_claude_undo", "");
    const ImVec2 sa = StudioButtonSize("Accept all", "done", 26.0f, kStudioFsXs);
    bx -= sa.x + P(6.0f);
    ImGui::SetCursorScreenPos(ImVec2(bx, y));
    if (StudioButton("##acceptall", "Accept all", "done", SB_GOOD, 26.0f, 0.0f, kStudioFsXs, !g_bRunning))
      StudioAsk("studio_claude_accept", "");
  } else if (nGroup) {
    const ImVec2 su = StudioButtonSize("Undo them", "undo", 26.0f, kStudioFsXs);
    bx -= su.x;
    ImGui::SetCursorScreenPos(ImVec2(bx, y));
    if (StudioButton("##undothem", "Undo them", "undo", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, !g_bRunning))
      StudioAsk("studio_undo", "");
  }
  y += P(34.0f);
  if (rows.empty()) {
    WrapText(d, ImVec2(x, y), b.x - x - P(14.0f),
             g_nMode == 0 ? "Nothing waits. In Show me first, what Claude changes waits here as one change: each field "
                            "before and after, to accept or undo. Until you accept, a save or a push leaves it out, and "
                            "your own edits wait for it."
                          : "Let it edit: what Claude changes is made at once, as one change in the History, undone "
                            "with one Undo. Its last edits show here.",
             C(TK_INK3), kStudioFsXs);
    return;
  }
  if (!bPending) {
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y - P(6.0f), C(TK_INK3), "Done: one Undo takes them all back.", b.x - x - P(14.0f));
    y += P(14.0f);
  }
  // the columns: record, field, before, after, and a row's own buttons
  const float cBtn = b.x - P(10.0f) - P(bPending ? 112.0f : 0.0f), w = cBtn - x;
  const float cRec = x, cField = x + w * 0.25f, cBefore = x + w * 0.45f, cAfter = x + w * 0.73f;
  const char *const kHeads[] = {"Record", "Field", "Before", "After"};
  const float cols[] = {cRec, cField, cBefore, cAfter};
  for (int i = 0; i < 4; ++i)
    Wide(d, 8.0f, cols[i], y, C(TK_INK3), kHeads[i]);
  y += P(16.0f);
  d->AddLine(ImVec2(x, y), ImVec2(b.x - P(10.0f), y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, y + 1));
  if (!ImGui::BeginChild("##claudediff", ImVec2(b.x - a.x - 2, b.y - y - 2), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::EndChild();
    return;
  }
  d = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float rh = P(30.0f);
  for (size_t i = 0; i < rows.size(); ++i) {
    const Row &r = rows[i];
    const float ty = Mid(p.y, rh, kStudioFsXs);
    T(d, STUDIO_FONT_BOLD, kStudioFsXs, cRec, ty, C(TK_INK), r.record, cField - cRec - P(8.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, cField, ty, C(TK_INK2), r.field, cBefore - cField - P(8.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, cBefore, ty, C(TK_INK3), r.beforeWords, cAfter - cBefore - P(8.0f));
    const float rowH = r.problem.empty() ? rh : rh + P(16.0f);
    T(d, STUDIO_FONT_BOLD, kStudioFsXs, cAfter, ty, C(!r.problem.empty() ? TK_BAD : bPending ? TK_CHANGED : TK_CLAUDE), r.afterWords,
      cBtn - cAfter - P(8.0f));
    if (!r.problem.empty()) {
      StudioGlyph(d, "clash", ImVec2(cRec, p.y + rh - P(4.0f)), P(12.0f), C(TK_BAD));
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, cRec + P(16.0f), p.y + rh - P(5.0f), C(TK_BAD), "does not load: " + r.problem,
        b.x - cRec - P(30.0f));
    }
    if (ImGui::IsMouseHoveringRect(ImVec2(cRec, p.y), ImVec2(cBtn, p.y + rowH)))
      ImGui::SetTooltip("%s\n%s\nbefore: %s\nafter: %s%s", r.record.c_str(), r.field.c_str(), r.beforeWords.c_str(), r.afterWords.c_str(),
                        r.problem.empty() ? "" : ("\nthe loader refuses it: " + r.problem).c_str());
    if (bPending) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetCursorScreenPos(ImVec2(cBtn, p.y + (rh - P(22.0f)) / 2.0f));
      if (StudioButton("##acc", "Accept", nullptr, SB_GOOD, 22.0f, 60.0f, kStudioFsXs, !g_bRunning))
        StudioAsk("studio_claude_accept", "\"row\":" + std::to_string(i));
      ImGui::SetCursorScreenPos(ImVec2(cBtn + P(64.0f), p.y + (rh - P(22.0f)) / 2.0f));
      if (StudioButton("##undo", "", "undo", SB_PLAIN, 22.0f, 46.0f, kStudioFsXs, !g_bRunning))
        StudioAsk("studio_claude_undo", "\"row\":" + std::to_string(i));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Undo this one; the others still wait");
      ImGui::PopID();
    }
    d->AddLine(ImVec2(x, p.y + rowH - 0.5f), ImVec2(b.x - P(10.0f), p.y + rowH - 0.5f), C(TK_ROW));
    p.y += rowH;
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  ImGui::EndChild();
}

void Header(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), C(TK_ROW));
  float x = a.x + P(10.0f);
  const float y = a.y + (b.y - a.y - P(24.0f)) / 2.0f;
  StudioGlyph(d, "claude", ImVec2(x, y + P(4.0f)), P(16.0f), C(TK_CLAUDE));
  x += P(24.0f);
  static const char *const kMode[] = {"Show me first", "Let it edit"};
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  const int nMode = StudioSeg("##claudemode", kMode, 2, g_nMode, 24.0f, kStudioFsXs);
  if (nMode != g_nMode)
    StudioAsk("studio_claude_mode", std::string("\"mode\":") + (nMode == 0 ? "\"show\"" : "\"edit\""));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Show me first: Claude's edits wait as one change to accept.\nLet it edit: they are made at once, one Undo.");
  x += StudioSegWidth(kMode, 2, kStudioFsXs) + P(14.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (StudioSelectBegin("##claudemodel", (std::string("Model: ") + kModels[g_nModel]).c_str(), 150.0f, 24.0f, kStudioFsXs)) {
    for (int i = 0; i < 4; ++i)
      if (StudioItem(kModels[i], g_nModel == i))
        g_nModel = i;
    StudioSelectEnd();
  }
  x += P(158.0f);
  // the conversations: this one, the others to go back to, and clearing them
  const Conversation *c = ConvOf(g_conv);
  const std::string title = c ? c->title : std::string("A new conversation");
  const float cw = fminf(360.0f, (b.x - x) / P(1.0f) - 20.0f);
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(10.0f) - P(cw), y));
  if (cw > 120.0f && StudioSelectBegin("##claudeconv", title.c_str(), cw, 24.0f, kStudioFsXs)) {
    if (StudioItem("New conversation", g_conv.empty(), nullptr, !g_bRunning))
      StudioAsk("studio_claude_conversations", "\"new\":true");
    if (!g_convs.empty())
      StudioItemSeparator();
    for (const Conversation &k : g_convs) {
      char sz[32];
      const time_t t = static_cast<time_t>(k.nLast ? k.nLast : k.nStarted);
      struct tm local;
      localtime_s(&local, &t);
      strftime(sz, sizeof sz, "%d %b %H:%M", &local);
      if (StudioItem((k.title + "   " + sz).c_str(), k.id == g_conv, nullptr, !g_bRunning))
        StudioAsk("studio_claude_conversations", "\"open\":" + Q(k.id));
    }
    StudioItemSeparator();
    if (StudioItem("Clear this conversation", false, nullptr, !g_conv.empty() && !g_bRunning))
      StudioAsk("studio_claude_conversations", "\"clear\":" + Q(g_conv));
    if (StudioItem("Clear them all", false, nullptr, !g_convs.empty() && !g_bRunning))
      StudioAsk("studio_claude_conversations", "\"all\":true");
    StudioSelectEnd();
  }
}

// ---- the commands ---------------------------------------------------------------------------------------------

std::string RowsJson(const std::vector<Row> &rows) {
  std::string out;
  for (size_t i = 0; i < rows.size(); ++i) {
    const Row &r = rows[i];
    std::string path;
    for (const std::string &k : r.path)
      path += (path.empty() ? "" : ",") + Q(k);
    out += std::string(out.empty() ? "" : ",") + "{\"row\":" + std::to_string(i) + ",\"doc\":" + Q(r.doc) + ",\"path\":[" + path +
           "],\"record\":" + Q(r.record) + ",\"field\":" + Q(r.field) + ",\"before\":" +
           (r.before.kind == PortJson::NONE ? std::string("null") : PortJsonWrite(r.before)) + ",\"after\":" +
           (r.after.kind == PortJson::NONE ? std::string("null") : PortJsonWrite(r.after)) + ",\"before_words\":" + Q(r.beforeWords) +
           ",\"after_words\":" + Q(r.afterWords) + (r.problem.empty() ? std::string() : ",\"problem\":" + Q(r.problem)) + "}";
  }
  return "[" + out + "]";
}

std::string StatusJson(void) {
  int nGroup = 0;
  const std::vector<Row> &rows = Rows(&nGroup);
  return "\"running\":" + std::string(g_bRunning ? "true" : "false") + ",\"mode\":" + Q(g_nMode == 0 ? "show" : "edit") +
         ",\"model\":" + Q(kModelArgs[g_nModel]) + ",\"program\":" + Q(ClaudeProgram()) + ",\"mcp\":" + Q(McpScript()) +
         ",\"python\":" + Q(Python()) + ",\"missing\":" + Q(Missing()) + ",\"conversation\":" + Q(g_conv) +
         ",\"conversations\":" + std::to_string(g_convs.size()) + ",\"items\":" + std::to_string(g_items.size()) +
         ",\"pending\":" + std::to_string(StudioPending() ? rows.size() : 0) + ",\"group\":" + std::to_string(nGroup);
}

bool CmdStatus(const PortJson &, std::string *pAnswer) {
  Tick();
  *pAnswer = StatusJson();
  return true;
}

bool CmdAsk(const PortJson &j, std::string *pAnswer) {
  Tick();
  if (j["mode"].kind == PortJson::STRING)
    g_nMode = j["mode"].s == "edit" ? 1 : 0;
  for (int i = 0; i < 4; ++i)
    if (j["model"].kind == PortJson::STRING && Lower(j["model"].s) == Lower(kModelArgs[i]))
      g_nModel = i;
  std::string why;
  if (!Ask(j["text"].Str(), j["new"].kind == PortJson::BOOL && j["new"].b, &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = StatusJson();
  return true;
}

bool CmdStop(const PortJson &, std::string *pAnswer) {
  Tick();
  if (!g_bRunning) {
    *pAnswer = "Claude is not answering";
    return false;
  }
  StopAndWait();  // so the answer can say it went
  *pAnswer = StatusJson();
  return true;
}

bool CmdTranscript(const PortJson &j, std::string *pAnswer) {
  Tick();
  *pAnswer = "\"conversation\":" + Q(g_conv) + ",\"running\":" + (g_bRunning ? "true" : "false") + ",\"items\":" + ItemsJson();
  (void)j;
  return true;
}

bool CmdPending(const PortJson &, std::string *pAnswer) {
  Tick();
  int nGroup = 0;
  const std::vector<Row> &rows = Rows(&nGroup);
  // the languages a push gives the game's loader, which a waiting proposal leaves as they were
  std::string langs;
  for (const PortJsonMember &m : StudioDocAccepted("lang").members)
    langs += std::string(langs.empty() ? "" : ",") + Q(m.first);
  *pAnswer = "\"waiting\":" + std::string(StudioPending() ? "true" : "false") + ",\"group\":" + std::to_string(nGroup) +
             ",\"mode\":" + Q(g_nMode == 0 ? "show" : "edit") + ",\"rows\":" + RowsJson(rows) + ",\"pushed_languages\":[" + langs + "]";
  return true;
}

bool RowArg(const PortJson &j, size_t nRows, int *pRow, std::string *pAnswer) {
  *pRow = -1;
  if (j["row"].kind == PortJson::NONE)
    return true;
  *pRow = static_cast<int>(j["row"].Num(-1));
  if (*pRow < 0 || static_cast<size_t>(*pRow) >= nRows) {
    *pAnswer = "row is one of the proposal's, 0 to " + std::to_string(nRows) + " less one";
    return false;
  }
  return true;
}

bool CmdAccept(const PortJson &j, std::string *pAnswer) {
  Tick();
  if (!StudioPending()) {
    *pAnswer = "no proposal of Claude's is waiting";
    return false;
  }
  if (g_bRunning) {
    *pAnswer = "Claude is still answering: accept once it is done";
    return false;
  }
  const std::vector<Row> rows = Rows(nullptr);
  int nRow = -1;
  if (!RowArg(j, rows.size(), &nRow, pAnswer))
    return false;
  std::string why;
  if (nRow < 0 || rows.size() == 1) {
    StudioPendingSet(0);
  } else if (!Split(rows, {nRow}, {}, &why)) {
    *pAnswer = why;
    return false;
  }
  Note(IT_NOTE, nRow < 0 || rows.size() == 1 ? "You accepted Claude's changes." : "You accepted " + rows[static_cast<size_t>(nRow)].record + " " +
                                                                                     rows[static_cast<size_t>(nRow)].field + ".");
  SaveConversations();
  return CmdPending(j, pAnswer);
}

bool CmdUndo(const PortJson &j, std::string *pAnswer) {
  Tick();
  if (!StudioPending()) {
    *pAnswer = "no proposal of Claude's is waiting";
    return false;
  }
  if (g_bRunning) {
    *pAnswer = "Claude is still answering: Stop it first";
    return false;
  }
  const std::vector<Row> rows = Rows(nullptr);
  int nRow = -1;
  if (!RowArg(j, rows.size(), &nRow, pAnswer))
    return false;
  std::string why;
  if (nRow < 0 || rows.size() == 1) {
    if (!StudioUndo(&why)) {
      *pAnswer = why;
      return false;
    }
  } else if (!Split(rows, {}, {nRow}, &why)) {
    *pAnswer = why;
    return false;
  }
  Note(IT_NOTE, nRow < 0 || rows.size() == 1 ? "You undid Claude's changes." : "You undid " + rows[static_cast<size_t>(nRow)].record + " " +
                                                                                 rows[static_cast<size_t>(nRow)].field + ".");
  SaveConversations();
  return CmdPending(j, pAnswer);
}

bool CmdMode(const PortJson &j, std::string *pAnswer) {
  if (j["mode"].kind == PortJson::STRING) {
    if (j["mode"].s != "show" && j["mode"].s != "edit") {
      *pAnswer = "mode is show (Show me first) or edit (Let it edit)";
      return false;
    }
    g_nMode = j["mode"].s == "edit" ? 1 : 0;
    PortSettingSetNumber("studio", "claudeMode", g_nMode);
  }
  for (int i = 0; i < 4; ++i)
    if (j["model"].kind == PortJson::STRING && Lower(j["model"].s) == Lower(kModelArgs[i]))
      g_nModel = i;
  *pAnswer = StatusJson();
  return true;
}

bool CmdConversations(const PortJson &j, std::string *pAnswer) {
  Tick();
  const bool bChange = j["new"].kind == PortJson::BOOL || j["open"].kind == PortJson::STRING ||
                       j["clear"].kind == PortJson::STRING || j["all"].kind == PortJson::BOOL;
  if (bChange && g_bRunning) {
    *pAnswer = "Claude is still answering: Stop it first";
    return false;
  }
  if (j["new"].kind == PortJson::BOOL && j["new"].b) {
    g_conv.clear();
    g_items.clear();
  }
  if (j["open"].kind == PortJson::STRING) {
    if (ConvOf(j["open"].s) == nullptr) {
      *pAnswer = "no conversation " + j["open"].s + " in this mod";
      return false;
    }
    g_conv = j["open"].s;
    LoadItems(g_conv);
  }
  if (j["clear"].kind == PortJson::STRING)
    ClearConversation(j["clear"].s);
  if (j["all"].kind == PortJson::BOOL && j["all"].b)
    while (!g_convs.empty())
      ClearConversation(g_convs[0].id);
  std::string list;
  for (const Conversation &c : g_convs)
    list += std::string(list.empty() ? "" : ",") + "{\"id\":" + Q(c.id) + ",\"title\":" + Q(c.title) + ",\"started\":" +
            std::to_string(c.nStarted) + ",\"last\":" + std::to_string(c.nLast) + ",\"asks\":" + std::to_string(c.nAsks) + "}";
  *pAnswer = "\"conversation\":" + Q(g_conv) + ",\"conversations\":[" + list + "],\"folder\":" + Q(StudioIsOpen() ? Folder() : std::string());
  return true;
}

bool CmdUse(const PortJson &j, std::string *pAnswer) {
  g_program = j["program"].Str();
  *pAnswer = StatusJson();
  return true;
}

}  // namespace

void StudioClaudeTick(void) {
  Tick();
}

int StudioClaudeBadge(void) {
  return StudioPending() ? static_cast<int>(Rows(nullptr).size()) : 0;
}

bool StudioClaudeRunning(void) {
  return g_bRunning;
}

void StudioClaudeFocus(void) {
  g_bFocusInput = true;
}

void StudioClaudeTab(ImVec2 a, ImVec2 b) {
  Tick();
  const float fRight = fminf(fmaxf((b.x - a.x) * 0.45f, P(420.0f)), (b.x - a.x) * 0.55f);
  const float hh = P(36.0f), ch = P(112.0f);
  Header(a, ImVec2(b.x - fRight, a.y + hh));
  Transcript(ImVec2(a.x, a.y + hh), ImVec2(b.x - fRight, b.y - ch));
  Composer(ImVec2(a.x, b.y - ch), ImVec2(b.x - fRight, b.y));
  Proposal(ImVec2(b.x - fRight, a.y), b);
}

void StudioClaudeInit(void) {
  PortControlCommand("studio_claude_status", CmdStatus);
  PortControlCommand("studio_claude_ask", CmdAsk);
  PortControlCommand("studio_claude_stop", CmdStop);
  PortControlCommand("studio_claude_transcript", CmdTranscript);
  PortControlCommand("studio_claude_pending", CmdPending);
  PortControlCommand("studio_claude_accept", CmdAccept);
  PortControlCommand("studio_claude_undo", CmdUndo);
  PortControlCommand("studio_claude_mode", CmdMode);
  PortControlCommand("studio_claude_conversations", CmdConversations);
  PortControlCommand("studio_claude_use", CmdUse);
}
