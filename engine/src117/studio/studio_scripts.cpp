// studio_scripts.cpp -- the Scripts workspace: a mod's Lua (docs/mod-data.md, "Scripts"), each file of
// its data folder's scripts\ a member of the model's "scripts" document. A code editor with the
// language's colours, line numbers, the loader's problems underlined where its parser and rules put
// them, completion of the API's names with their signatures, Find, and every change an edit the
// studio's undo takes back; the functions each file gives and where the mod names them; the hooks
// and what calls them; and a function called in the test game against the unit selected there, with
// the game's log of the scripts.
//
// The check is the loader's own (ModLuaCheckText, port_modlua.cpp), so what it underlines is what
// --mod-check refuses.
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include "../design/design_engine.h"     // DesignCall
#include "../design/design_hooks.h"      // DesignInGame
#include "../port/data/port_modlua.h"    // ModLuaCheckText, ModLuaApi, ModLuaHooks
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// ---- the files --------------------------------------------------------------------------------------

bool FileNameOk(const std::string &name) {
  const size_t n = name.size();
  if (!((n > 4 && name.compare(n - 4, 4, ".lua") == 0) || (n > 5 && name.compare(n - 5, 5, ".luau") == 0)))
    return false;
  for (char c : name)
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
      return false;
  return true;
}

std::vector<std::string> Files() {
  std::vector<std::string> out;
  for (const PortJsonMember &m : StudioDoc("scripts").members)
    if (m.second.kind == PortJson::STRING)
      out.push_back(m.first);
  std::sort(out.begin(), out.end());
  return out;
}

std::string TextOf(const std::string &file) {
  return StudioDoc("scripts")[file.c_str()].Str();
}

// Each file's check, made again when its text changes.
struct Check {
  std::string text;
  bool bDone = false;
  std::vector<ModLuaProblem> problems;
  std::vector<ModLuaFunction> fns;
};
std::map<std::string, Check> g_checks;

const Check &CheckOf(const std::string &file, const std::string &text) {
  Check &c = g_checks[file];
  if (!c.bDone || c.text != text) {
    c = Check();
    c.text = text;
    c.bDone = true;
    ModLuaCheckText("scripts/" + file, text, &c.problems, &c.fns);
  }
  return c;
}

const Check &CheckFile(const std::string &file) {
  return CheckOf(file, TextOf(file));
}

// Where the mod names a function: "units void_warrior: on_hit", "orders void_spark: block 1".
std::vector<std::string> UsesOf(const std::string &fn) {
  std::vector<std::string> out;
  const PortJson &types = StudioDoc("types");
  for (const PortJsonMember &u : types["units"].members)
    for (const PortJsonMember &h : u.second["scripts"].members)
      if (h.second.kind == PortJson::STRING && h.second.s == fn)
        out.push_back("units " + u.first + ": " + h.first);
  for (const PortJsonMember &o : types["orders"].members) {
    for (const char *psz : {"toggle", "channel", "autocast"})
      for (const PortJsonMember &h : o.second[psz].members)
        if (h.second.kind == PortJson::STRING && h.second.s == fn)
          out.push_back("orders " + o.first + ": " + psz + " " + h.first);
    // blocks, nested ones too
    std::vector<std::pair<const PortJson *, std::string>> lists = {{&o.second["blocks"], "block "}};
    while (!lists.empty()) {
      auto l = lists.back();
      lists.pop_back();
      for (size_t i = 0; i < l.first->items.size(); ++i) {
        const PortJson &b = l.first->items[i];
        const std::string at = l.second + std::to_string(i + 1);
        if (b["do"].Str() == "script" && b["call"].Str() == fn)
          out.push_back("orders " + o.first + ": " + at);
        if (b["blocks"].kind == PortJson::ARRAY)
          lists.push_back({&b["blocks"], at + "."});
      }
    }
  }
  for (const PortJsonMember &h : types["game"].members)
    if (h.second.kind == PortJson::STRING && h.second.s == fn)
      out.push_back("game: " + h.first);
  return out;
}

// ---- the editor's text ---------------------------------------------------------------------------------

struct Pos {
  int line = 0, col = 0;
};
bool Before(Pos a, Pos b) {
  return a.line < b.line || (a.line == b.line && a.col < b.col);
}
bool Same(Pos a, Pos b) {
  return a.line == b.line && a.col == b.col;
}

struct Editor {
  std::string file;
  std::vector<std::string> lines = {""};
  Pos cur, anchor;
  bool bSel = false;
  bool bFocus = false;
  bool bScrollToCursor = false;
  bool bScrollTop = false;  // a document just opened, so the editor starts at its first line
  bool bDragging = false;
  std::string committed;  // the text as the model has it, or as last sent to it
  int nPending = 0;       // edits sent and not answered
  float fWantX = -1;      // the column's x kept on up and down
  // completion
  bool bComplete = false;
  int nCompleteSel = 0;
  std::vector<std::pair<std::string, std::string>> matches;  // a name, its signature and help
  std::string prefix;
  // find
  bool bFind = false, bFindFocus = false;
  char szFind[128] = {0};
};
Editor g_ed;
std::string g_sel;  // the file shown

std::vector<std::string> Split(const std::string &text) {
  std::vector<std::string> out;
  size_t at = 0;
  while (true) {
    const size_t nl = text.find('\n', at);
    std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    out.push_back(line);
    if (nl == std::string::npos)
      break;
    at = nl + 1;
  }
  return out;
}

std::string Join(const std::vector<std::string> &lines) {
  std::string s;
  for (size_t i = 0; i < lines.size(); ++i)
    s += (i ? "\n" : "") + lines[i];
  return s;
}

Pos Clamp(Pos p) {
  p.line = std::max(0, std::min(p.line, static_cast<int>(g_ed.lines.size()) - 1));
  p.col = std::max(0, std::min(p.col, static_cast<int>(g_ed.lines[p.line].size())));
  return p;
}

void SelRange(Pos *pA, Pos *pB) {
  *pA = Before(g_ed.anchor, g_ed.cur) ? g_ed.anchor : g_ed.cur;
  *pB = Before(g_ed.anchor, g_ed.cur) ? g_ed.cur : g_ed.anchor;
}

bool HasSel() {
  return g_ed.bSel && !Same(g_ed.anchor, g_ed.cur);
}

std::string TextIn(Pos a, Pos b) {
  if (a.line == b.line)
    return g_ed.lines[a.line].substr(a.col, b.col - a.col);
  std::string s = g_ed.lines[a.line].substr(a.col);
  for (int l = a.line + 1; l < b.line; ++l)
    s += "\n" + g_ed.lines[l];
  return s + "\n" + g_ed.lines[b.line].substr(0, b.col);
}

void Erase(Pos a, Pos b) {
  std::string &first = g_ed.lines[a.line];
  const std::string tail = g_ed.lines[b.line].substr(b.col);
  first = first.substr(0, a.col) + tail;
  g_ed.lines.erase(g_ed.lines.begin() + a.line + 1, g_ed.lines.begin() + b.line + 1);
  g_ed.cur = a;
  g_ed.bSel = false;
}

void DeleteSel() {
  if (!HasSel()) {
    g_ed.bSel = false;
    return;
  }
  Pos a, b;
  SelRange(&a, &b);
  Erase(a, b);
}

void Insert(const std::string &s) {
  DeleteSel();
  const std::vector<std::string> parts = Split(s);
  std::string &line = g_ed.lines[g_ed.cur.line];
  const std::string tail = line.substr(g_ed.cur.col);
  line = line.substr(0, g_ed.cur.col) + parts[0];
  int nLine = g_ed.cur.line;
  for (size_t i = 1; i < parts.size(); ++i)
    g_ed.lines.insert(g_ed.lines.begin() + (++nLine), parts[i]);
  g_ed.cur.line = nLine;
  g_ed.cur.col = static_cast<int>(g_ed.lines[nLine].size());
  g_ed.lines[nLine] += tail;
  g_ed.bScrollToCursor = true;
}

// ---- colours ---------------------------------------------------------------------------------------

enum { K_TEXT, K_KEY, K_STR, K_NUM, K_COM, K_API, K_FN, K_OP, K_HOOKARG };
struct Span {
  int a, b, k;
};

const char *const kKeywords[] = {"and",   "break", "continue", "do",   "else", "elseif", "end",   "false", "for",   "function", "if",
                                 "in",    "local", "nil",      "not",  "or",   "repeat", "return", "then", "true",  "until",    "while",
                                 "type",  "export"};

bool IsKeyword(const std::string &w) {
  for (const char *k : kKeywords)
    if (w == k)
      return true;
  return false;
}

bool IdStart(char c) {
  return isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool IdChar(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// A long bracket's level at s[i] ("[[" 0, "[=[" 1), or -1.
int LongOpen(const std::string &s, size_t i) {
  if (i >= s.size() || s[i] != '[')
    return -1;
  size_t j = i + 1;
  while (j < s.size() && s[j] == '=')
    ++j;
  return j < s.size() && s[j] == '[' ? static_cast<int>(j - i - 1) : -1;
}

// Where a long bracket of a level closes from i, the index past it, or npos.
size_t LongClose(const std::string &s, size_t i, int nLevel) {
  const std::string close = "]" + std::string(static_cast<size_t>(nLevel), '=') + "]";
  const size_t at = s.find(close, i);
  return at == std::string::npos ? at : at + close.size();
}

// One line's spans; *pState carries a long string or comment into the next (0 none, else
// 1 + level * 2 + comment).
std::vector<Span> Lex(const std::string &s, int *pState, const std::set<std::string> &fns) {
  std::vector<Span> out;
  size_t i = 0;
  if (*pState > 0) {
    const int nLevel = (*pState - 1) / 2;
    const int k = ((*pState - 1) & 1) ? K_COM : K_STR;
    const size_t e = LongClose(s, 0, nLevel);
    if (e == std::string::npos) {
      out.push_back({0, static_cast<int>(s.size()), k});
      return out;
    }
    out.push_back({0, static_cast<int>(e), k});
    *pState = 0;
    i = e;
  }
  while (i < s.size()) {
    const char c = s[i];
    const size_t st = i;
    if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
      const int lv = LongOpen(s, i + 2);
      if (lv >= 0) {
        const size_t e = LongClose(s, i + 4 + lv, lv);
        if (e == std::string::npos) {
          out.push_back({static_cast<int>(st), static_cast<int>(s.size()), K_COM});
          *pState = 1 + lv * 2 + 1;
          return out;
        }
        out.push_back({static_cast<int>(st), static_cast<int>(e), K_COM});
        i = e;
        continue;
      }
      out.push_back({static_cast<int>(st), static_cast<int>(s.size()), K_COM});
      return out;
    }
    if (c == '[') {
      const int lv = LongOpen(s, i);
      if (lv >= 0) {
        const size_t e = LongClose(s, i + 2 + lv, lv);
        if (e == std::string::npos) {
          out.push_back({static_cast<int>(st), static_cast<int>(s.size()), K_STR});
          *pState = 1 + lv * 2;
          return out;
        }
        out.push_back({static_cast<int>(st), static_cast<int>(e), K_STR});
        i = e;
        continue;
      }
    }
    if (c == '"' || c == '\'' || c == '`') {
      ++i;
      while (i < s.size() && s[i] != c) {
        if (s[i] == '\\')
          ++i;
        ++i;
      }
      i = std::min(i + 1, s.size());
      out.push_back({static_cast<int>(st), static_cast<int>(i), K_STR});
      continue;
    }
    if (isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < s.size() && isdigit(static_cast<unsigned char>(s[i + 1])))) {
      while (i < s.size() && (IdChar(s[i]) || s[i] == '.' ||
                              ((s[i] == '-' || s[i] == '+') && (s[i - 1] == 'e' || s[i - 1] == 'E') && !(st + 1 < s.size() && (s[st + 1] == 'x' || s[st + 1] == 'X')))))
        ++i;
      out.push_back({static_cast<int>(st), static_cast<int>(i), K_NUM});
      continue;
    }
    if (IdStart(c)) {
      while (i < s.size() && IdChar(s[i]))
        ++i;
      const std::string w = s.substr(st, i - st);
      int k = IsKeyword(w) ? K_KEY : K_TEXT;
      // hx.name and math.name as one
      if ((w == "hx" || w == "math") && i + 1 < s.size() && s[i] == '.' && IdStart(s[i + 1])) {
        size_t j = i + 1;
        while (j < s.size() && IdChar(s[j]))
          ++j;
        out.push_back({static_cast<int>(st), static_cast<int>(j), K_API});
        i = j;
        continue;
      }
      if (k == K_TEXT && fns.count(w))
        k = K_FN;
      else if (k == K_TEXT && w == "e" && i < s.size() && s[i] == '.')
        k = K_HOOKARG;
      out.push_back({static_cast<int>(st), static_cast<int>(i), k});
      continue;
    }
    if (isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    ++i;
    out.push_back({static_cast<int>(st), static_cast<int>(i), K_OP});
  }
  return out;
}

ImU32 KindCol(int k) {
  switch (k) {
  case K_KEY: return C(TK_LABEL);
  case K_STR: return C(TK_GOOD);
  case K_NUM: return C(TK_WARN);
  case K_COM: return C(TK_INK3);
  case K_API: return C(TK_CLAUDE);
  case K_FN: return C(TK_HI);
  case K_OP: return C(TK_INK2);
  case K_HOOKARG: return C(TK_CHANGED);
  default: return C(TK_INK);
  }
}

// ---- measuring --------------------------------------------------------------------------------------

const float kCodePx = 13.5f;
float g_cw = 8.0f;  // a cell's width, the face's "M"

int CellsOf(const std::string &s, int nCol) {
  int n = 0;
  for (int i = 0; i < nCol && i < static_cast<int>(s.size()); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '\t')
      n = (n / 2 + 1) * 2;
    else if ((c & 0xC0) != 0x80)
      ++n;
  }
  return n;
}

float ColX(const std::string &s, int nCol) {
  return CellsOf(s, nCol) * g_cw;
}

int ColAt(const std::string &s, float fX) {
  int best = 0;
  float bestD = 1e9f;
  for (int c = 0; c <= static_cast<int>(s.size()); ++c) {
    if (c < static_cast<int>(s.size()) && (static_cast<unsigned char>(s[c]) & 0xC0) == 0x80)
      continue;
    const float d = fabsf(ColX(s, c) - fX);
    if (d < bestD) {
      bestD = d;
      best = c;
    }
  }
  return best;
}

// The text shown for a line, tabs as spaces to the next even cell.
std::string Shown(const std::string &s) {
  if (s.find('\t') == std::string::npos)
    return s;
  std::string o;
  int n = 0;
  for (char c : s) {
    if (c == '\t') {
      const int to = (n / 2 + 1) * 2;
      o.append(static_cast<size_t>(to - n), ' ');
      n = to;
    } else {
      o += c;
      if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
        ++n;
    }
  }
  return o;
}

// ---- completion --------------------------------------------------------------------------------------

// The word before the cursor, with hx. or math. before it when there is one.
std::string WordBefore() {
  const std::string &s = g_ed.lines[g_ed.cur.line];
  int i = g_ed.cur.col;
  while (i > 0 && (IdChar(s[i - 1]) || s[i - 1] == '.'))
    --i;
  return s.substr(i, g_ed.cur.col - i);
}

void CompleteFind(const std::vector<std::string> &fns) {
  g_ed.matches.clear();
  const std::string w = WordBefore();
  g_ed.prefix = w;
  if (w.empty() || (w.size() < 2 && w.find('.') == std::string::npos)) {
    g_ed.bComplete = false;
    return;
  }
  int n = 0;
  const ModLuaName *api = ModLuaApi(&n);
  for (int i = 0; i < n; ++i)
    if (strncmp(api[i].pszName, w.c_str(), w.size()) == 0 && w != api[i].pszName)
      g_ed.matches.push_back({api[i].pszName, std::string(api[i].pszArgs) + "  " + api[i].pszHelp});
  if (w.find('.') == std::string::npos) {
    if (std::string("hx").compare(0, w.size(), w) == 0)
      g_ed.matches.insert(g_ed.matches.begin(), {"hx", "the game: units, life, damage, orders, values"});
    for (const std::string &f : fns)
      if (f.compare(0, w.size(), w) == 0 && f != w)
        g_ed.matches.push_back({f, "a function of the mod's scripts"});
    for (const char *k : kKeywords)
      if (strncmp(k, w.c_str(), w.size()) == 0 && w != k)
        g_ed.matches.push_back({k, ""});
  } else if (w.compare(0, 2, "e.") == 0) {
    static const char *const kFields[] = {"unit", "target", "caster", "source", "weapon", "damage", "taken", "x", "y", "order",
                                          "player", "owner", "type", "reason", "ticks", "frame"};
    for (const char *f : kFields) {
      const std::string full = std::string("e.") + f;
      if (full.compare(0, w.size(), w) == 0 && full != w)
        g_ed.matches.push_back({full, "a field of the hook's e, as the hook gives it"});
    }
  }
  g_ed.bComplete = !g_ed.matches.empty();
  g_ed.nCompleteSel = std::min(g_ed.nCompleteSel, std::max(0, static_cast<int>(g_ed.matches.size()) - 1));
}

void CompleteAccept() {
  if (!g_ed.bComplete || g_ed.matches.empty())
    return;
  const std::string &m = g_ed.matches[static_cast<size_t>(g_ed.nCompleteSel)].first;
  const std::string w = WordBefore();
  g_ed.bSel = false;
  g_ed.lines[g_ed.cur.line].erase(static_cast<size_t>(g_ed.cur.col - static_cast<int>(w.size())), w.size());
  g_ed.cur.col -= static_cast<int>(w.size());
  Insert(m);
  g_ed.bComplete = false;
}

// ---- the edits -------------------------------------------------------------------------------------------

void Commit() {
  const std::string text = Join(g_ed.lines);
  if (text == g_ed.committed || g_ed.file.empty())
    return;
  g_ed.committed = text;
  ++g_ed.nPending;
  DesignCall("studio_script_set", "\"file\":" + Q(g_ed.file) + ",\"text\":" + Q(text) + ",\"merge\":true,\"sender\":\"you\"",
             [](const PortJson &a) {
               if (g_ed.nPending > 0)
                 --g_ed.nPending;
               if (!(a["ok"].kind == PortJson::BOOL && a["ok"].b))
                 StudioSay(a["error"].Str("the script was not taken"));
             });
}

// The editor on a file, or the model's text taken again when it changed there (an undo, Claude).
void Sync(const std::string &file) {
  const std::string text = TextOf(file);
  if (g_ed.file != file) {
    g_ed = Editor();
    g_ed.file = file;
    g_ed.lines = Split(text);
    g_ed.committed = text;
    g_ed.bScrollTop = true;  // after the reset, which clears it
    return;
  }
  if (g_ed.nPending == 0 && text != g_ed.committed) {
    g_ed.lines = Split(text);
    g_ed.committed = text;
    g_ed.cur = Clamp(g_ed.cur);
    g_ed.anchor = Clamp(g_ed.anchor);
  }
}

void GoTo(int nLine, int nCol = 0) {
  g_ed.cur = Clamp(Pos{nLine - 1, nCol});
  g_ed.bSel = false;
  g_ed.bScrollToCursor = true;
  g_ed.bFocus = true;
}

// ---- find ----------------------------------------------------------------------------------------------

bool FindNext(bool bBack) {
  const std::string what = g_ed.szFind;
  if (what.empty())
    return false;
  const std::string low = Lower(what);
  const int nLines = static_cast<int>(g_ed.lines.size());
  Pos from = g_ed.cur;
  if (HasSel() && !bBack) {
    Pos a, b;
    SelRange(&a, &b);
    from = b;
  } else if (HasSel()) {
    Pos a, b;
    SelRange(&a, &b);
    from = a;
  }
  for (int k = 0; k <= nLines; ++k) {
    const int l = ((bBack ? from.line - k : from.line + k) % nLines + nLines) % nLines;
    const std::string line = Lower(g_ed.lines[l]);
    size_t at = std::string::npos;
    if (!bBack) {
      at = line.find(low, k == 0 ? static_cast<size_t>(from.col) : 0);
    } else {
      const size_t lim = k == 0 ? static_cast<size_t>(std::max(0, from.col - 1)) : line.size();
      at = k == 0 && from.col == 0 ? std::string::npos : line.rfind(low, lim);
    }
    if (at != std::string::npos) {
      g_ed.anchor = Pos{l, static_cast<int>(at)};
      g_ed.cur = Pos{l, static_cast<int>(at + what.size())};
      g_ed.bSel = true;
      g_ed.bScrollToCursor = true;
      return true;
    }
  }
  return false;
}

int FindCount() {
  const std::string low = Lower(g_ed.szFind);
  if (low.empty())
    return 0;
  int n = 0;
  for (const std::string &line : g_ed.lines) {
    const std::string l = Lower(line);
    for (size_t at = l.find(low); at != std::string::npos; at = l.find(low, at + low.size()))
      ++n;
  }
  return n;
}

// ---- keys -----------------------------------------------------------------------------------------------

bool Key(ImGuiKey k) {
  return ImGui::IsKeyPressed(k, true);
}

void Move(Pos to, bool bShift) {
  if (bShift && !g_ed.bSel) {
    g_ed.anchor = g_ed.cur;
    g_ed.bSel = true;
  } else if (!bShift) {
    g_ed.bSel = false;
  }
  g_ed.cur = Clamp(to);
  g_ed.bScrollToCursor = true;
  g_ed.bComplete = false;
}

int WordEdge(const std::string &s, int c, int dir) {
  if (dir < 0) {
    while (c > 0 && !IdChar(s[c - 1]))
      --c;
    while (c > 0 && IdChar(s[c - 1]))
      --c;
  } else {
    while (c < static_cast<int>(s.size()) && !IdChar(s[c]))
      ++c;
    while (c < static_cast<int>(s.size()) && IdChar(s[c]))
      ++c;
  }
  return c;
}

std::string Indent(const std::string &s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
    ++i;
  return s.substr(0, i);
}

// Whether a line opens a block that Enter should indent under.
bool Opens(const std::string &line) {
  std::string t = line;
  const size_t com = t.find("--");
  if (com != std::string::npos)
    t = t.substr(0, com);
  while (!t.empty() && isspace(static_cast<unsigned char>(t.back())))
    t.pop_back();
  auto ends = [&](const char *w) {
    const size_t n = strlen(w);
    return t.size() >= n && t.compare(t.size() - n, n, w) == 0 && (t.size() == n || !IdChar(t[t.size() - n - 1]));
  };
  if (ends("then") || ends("do") || ends("else") || ends("repeat") || (!t.empty() && (t.back() == '{' || t.back() == '(')))
    return true;
  const size_t f = t.find("function");
  return f != std::string::npos && t.back() == ')' && t.find("end", f) == std::string::npos;
}

// The editor's keys while it has the keyboard; true when the text changed.
bool Keys(const std::vector<std::string> &fns, float fPage) {
  ImGuiIO &io = ImGui::GetIO();
  const bool bCtrl = io.KeyCtrl, bShift = io.KeyShift;
  bool bChanged = false;
  Pos p = g_ed.cur;
  // completion's own keys first
  if (g_ed.bComplete) {
    if (Key(ImGuiKey_Escape)) {
      g_ed.bComplete = false;
      return false;
    }
    if (Key(ImGuiKey_DownArrow)) {
      g_ed.nCompleteSel = std::min(g_ed.nCompleteSel + 1, static_cast<int>(g_ed.matches.size()) - 1);
      return false;
    }
    if (Key(ImGuiKey_UpArrow)) {
      g_ed.nCompleteSel = std::max(g_ed.nCompleteSel - 1, 0);
      return false;
    }
    if (Key(ImGuiKey_Tab) || Key(ImGuiKey_Enter) || Key(ImGuiKey_KeypadEnter)) {
      CompleteAccept();
      return true;
    }
  }
  if (Key(ImGuiKey_Escape)) {
    g_ed.bSel = false;
    return false;
  }
  if (Key(ImGuiKey_LeftArrow)) {
    if (HasSel() && !bShift) {
      Pos a, b;
      SelRange(&a, &b);
      Move(a, false);
    } else if (p.col > 0) {
      p.col = bCtrl ? WordEdge(g_ed.lines[p.line], p.col, -1) : p.col - 1;
      while (p.col > 0 && (static_cast<unsigned char>(g_ed.lines[p.line][p.col]) & 0xC0) == 0x80)
        --p.col;
      Move(p, bShift);
    } else if (p.line > 0) {
      Move(Pos{p.line - 1, static_cast<int>(g_ed.lines[p.line - 1].size())}, bShift);
    }
    g_ed.fWantX = -1;
  }
  if (Key(ImGuiKey_RightArrow)) {
    if (HasSel() && !bShift) {
      Pos a, b;
      SelRange(&a, &b);
      Move(b, false);
    } else if (p.col < static_cast<int>(g_ed.lines[p.line].size())) {
      p.col = bCtrl ? WordEdge(g_ed.lines[p.line], p.col, 1) : p.col + 1;
      while (p.col < static_cast<int>(g_ed.lines[p.line].size()) && (static_cast<unsigned char>(g_ed.lines[p.line][p.col]) & 0xC0) == 0x80)
        ++p.col;
      Move(p, bShift);
    } else if (p.line + 1 < static_cast<int>(g_ed.lines.size())) {
      Move(Pos{p.line + 1, 0}, bShift);
    }
    g_ed.fWantX = -1;
  }
  auto vertical = [&](int nBy) {
    if (g_ed.fWantX < 0)
      g_ed.fWantX = ColX(g_ed.lines[p.line], p.col);
    const int l = std::max(0, std::min(static_cast<int>(g_ed.lines.size()) - 1, p.line + nBy));
    Move(Pos{l, ColAt(g_ed.lines[l], g_ed.fWantX)}, bShift);
  };
  const int nPage = std::max(1, static_cast<int>(fPage));
  if (Key(ImGuiKey_UpArrow))
    vertical(-1);
  if (Key(ImGuiKey_DownArrow))
    vertical(1);
  if (Key(ImGuiKey_PageUp))
    vertical(-nPage);
  if (Key(ImGuiKey_PageDown))
    vertical(nPage);
  if (Key(ImGuiKey_Home)) {
    if (bCtrl) {
      Move(Pos{0, 0}, bShift);
    } else {
      const int nInd = static_cast<int>(Indent(g_ed.lines[p.line]).size());
      Move(Pos{p.line, p.col == nInd ? 0 : nInd}, bShift);
    }
    g_ed.fWantX = -1;
  }
  if (Key(ImGuiKey_End)) {
    if (bCtrl)
      Move(Pos{static_cast<int>(g_ed.lines.size()) - 1, static_cast<int>(g_ed.lines.back().size())}, bShift);
    else
      Move(Pos{p.line, static_cast<int>(g_ed.lines[p.line].size())}, bShift);
    g_ed.fWantX = -1;
  }
  if (bCtrl && Key(ImGuiKey_A)) {
    g_ed.anchor = Pos{0, 0};
    g_ed.cur = Pos{static_cast<int>(g_ed.lines.size()) - 1, static_cast<int>(g_ed.lines.back().size())};
    g_ed.bSel = true;
  }
  if (bCtrl && (Key(ImGuiKey_C) || Key(ImGuiKey_X)) && HasSel()) {
    Pos a, b;
    SelRange(&a, &b);
    ImGui::SetClipboardText(TextIn(a, b).c_str());
    if (ImGui::IsKeyPressed(ImGuiKey_X)) {
      DeleteSel();
      bChanged = true;
    }
  }
  if (bCtrl && Key(ImGuiKey_V)) {
    const char *psz = ImGui::GetClipboardText();
    if (psz && psz[0]) {
      Insert(psz);
      bChanged = true;
    }
  }
  // Ctrl+/ comments the lines out, or back in
  if (bCtrl && Key(ImGuiKey_Slash)) {
    Pos a = g_ed.cur, b = g_ed.cur;
    if (HasSel())
      SelRange(&a, &b);
    bool bAll = true;
    for (int l = a.line; l <= b.line; ++l) {
      const std::string &s = g_ed.lines[l];
      const size_t n = Indent(s).size();
      bAll = bAll && s.compare(n, 2, "--") == 0;
    }
    for (int l = a.line; l <= b.line; ++l) {
      std::string &s = g_ed.lines[l];
      const size_t n = Indent(s).size();
      if (bAll)
        s.erase(n, s.compare(n, 3, "-- ") == 0 ? 3 : 2);
      else
        s.insert(n, "-- ");
    }
    g_ed.cur = Clamp(g_ed.cur);
    g_ed.anchor = Clamp(g_ed.anchor);
    bChanged = true;
  }
  if (bCtrl)
    return bChanged;
  if (Key(ImGuiKey_Backspace)) {
    if (HasSel()) {
      DeleteSel();
    } else if (p.col > 0) {
      int from = p.col - 1;
      while (from > 0 && (static_cast<unsigned char>(g_ed.lines[p.line][from]) & 0xC0) == 0x80)
        --from;
      // a run of spaces back to the indent's step
      const std::string &s = g_ed.lines[p.line];
      if (s[from] == ' ' && Indent(s).size() >= static_cast<size_t>(p.col) && p.col % 2 == 0 && from > 0 && s[from - 1] == ' ')
        --from;
      Erase(Pos{p.line, from}, p);
    } else if (p.line > 0) {
      Erase(Pos{p.line - 1, static_cast<int>(g_ed.lines[p.line - 1].size())}, p);
    }
    bChanged = true;
    g_ed.bScrollToCursor = true;
  }
  if (Key(ImGuiKey_Delete)) {
    if (HasSel()) {
      DeleteSel();
    } else if (p.col < static_cast<int>(g_ed.lines[p.line].size())) {
      int to = p.col + 1;
      while (to < static_cast<int>(g_ed.lines[p.line].size()) && (static_cast<unsigned char>(g_ed.lines[p.line][to]) & 0xC0) == 0x80)
        ++to;
      Erase(p, Pos{p.line, to});
    } else if (p.line + 1 < static_cast<int>(g_ed.lines.size())) {
      Erase(p, Pos{p.line + 1, 0});
    }
    bChanged = true;
  }
  if (Key(ImGuiKey_Enter) || Key(ImGuiKey_KeypadEnter)) {
    std::string ind = Indent(g_ed.lines[p.line].substr(0, static_cast<size_t>(p.col)));
    if (Opens(g_ed.lines[p.line].substr(0, static_cast<size_t>(p.col))))
      ind += "  ";
    Insert("\n" + ind);
    bChanged = true;
  }
  if (Key(ImGuiKey_Tab)) {
    if (HasSel() || bShift) {
      Pos a = g_ed.cur, b = g_ed.cur;
      if (HasSel())
        SelRange(&a, &b);
      for (int l = a.line; l <= b.line; ++l) {
        std::string &s = g_ed.lines[l];
        if (bShift) {
          const size_t n = std::min<size_t>(2, Indent(s).size());
          s.erase(0, n);
        } else {
          s.insert(0, "  ");
        }
      }
      g_ed.cur = Clamp(Pos{g_ed.cur.line, g_ed.cur.col + (bShift ? -2 : 2)});
      g_ed.anchor = Clamp(Pos{g_ed.anchor.line, g_ed.anchor.col + (bShift ? -2 : 2)});
    } else {
      Insert(std::string(static_cast<size_t>(2 - CellsOf(g_ed.lines[p.line], p.col) % 2), ' '));
    }
    bChanged = true;
  }
  // characters typed
  bool bTyped = false;
  for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
    const unsigned c = io.InputQueueCharacters[i];
    if (c < 32 && c != '\t')
      continue;
    if (c == '\t')
      continue;  // Tab is handled above
    char sz[5] = {0};
    if (c < 0x80) {
      sz[0] = static_cast<char>(c);
    } else if (c < 0x800) {
      sz[0] = static_cast<char>(0xC0 | (c >> 6));
      sz[1] = static_cast<char>(0x80 | (c & 0x3F));
    } else {
      sz[0] = static_cast<char>(0xE0 | (c >> 12));
      sz[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      sz[2] = static_cast<char>(0x80 | (c & 0x3F));
    }
    Insert(sz);
    // "end" typed on a line of its own steps back to the block's indent
    const std::string &s = g_ed.lines[g_ed.cur.line];
    const std::string t = s.substr(Indent(s).size());
    if ((t == "end" || t == "else" || t == "elseif" || t == "until") && g_ed.cur.line > 0 && Indent(s).size() >= 2 &&
        Indent(s) == Indent(g_ed.lines[g_ed.cur.line - 1]) && !Opens(g_ed.lines[g_ed.cur.line - 1])) {
      g_ed.lines[g_ed.cur.line].erase(0, 2);
      g_ed.cur.col -= 2;
    }
    bTyped = true;
    bChanged = true;
  }
  if (bTyped)
    CompleteFind(fns);
  else if (bChanged)
    g_ed.bComplete = false;
  return bChanged;
}

// ---- the editor drawn ------------------------------------------------------------------------------------

std::string g_hoverTip;

void DrawEditor(ImVec2 a, ImVec2 b, const Check &check, const std::vector<std::string> &fns) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  g_cw = TW(STUDIO_FONT_MONO, kCodePx, "M");
  const float lh = floorf(P(kCodePx) * 1.5f);
  const float gutter = P(14.0f) + g_cw * std::max(3.0f, floorf(log10f(static_cast<float>(g_ed.lines.size()))) + 1.0f) + P(14.0f);
  d->AddRectFilled(a, b, C(TK_WELL));
  ImGui::SetCursorScreenPos(a);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, 0);
  const bool bOpen = ImGui::BeginChild("##code", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None,
                                       ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);
  ImGui::PopStyleColor();
  if (!bOpen) {
    ImGui::EndChild();
    return;
  }
  ImDrawList *cd = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  float maxW = 0;
  for (const std::string &l : g_ed.lines)
    maxW = std::max(maxW, CellsOf(l, static_cast<int>(l.size())) * g_cw);
  const float contentW = gutter + maxW + P(80.0f);
  const float contentH = lh * static_cast<float>(g_ed.lines.size()) + (b.y - a.y) * 0.5f;
  ImGui::InvisibleButton("##codearea", ImVec2(std::max(contentW, b.x - a.x - P(16.0f)), contentH));
  const bool bHover = ImGui::IsItemHovered();
  // a document just opened starts at the top: the scroll belongs to the window, which is the
  // same window whatever file is shown, so a short file opened from a long one scrolled down
  // would otherwise be drawn from past its end. The scroll a Set asks for arrives next frame,
  // so this frame reads 0 rather than the scroll that is still there.
  bool bTop = false;
  if (g_ed.bScrollTop) {
    g_ed.bScrollTop = false;
    bTop = true;
    ImGui::SetScrollY(0.0f);
    ImGui::SetScrollX(0.0f);
  }
  const float scrollY = bTop ? 0.0f : ImGui::GetScrollY(), scrollX = bTop ? 0.0f : ImGui::GetScrollX();
  const float viewH = b.y - a.y;
  // both ends clamped to the lines there are, as nLast is: the scroll is the one thing here the
  // document does not settle, so a stale one must not reach lines[]
  const int nFirst = std::max(0, std::min(static_cast<int>(g_ed.lines.size()) - 1,
                                          static_cast<int>(scrollY / lh) - 1));
  const int nLast = std::min(static_cast<int>(g_ed.lines.size()) - 1, static_cast<int>((scrollY + viewH) / lh) + 1);
  const float tx = origin.x + gutter;
  // the mouse: a click puts the cursor, a drag selects, a double click takes the word
  ImGuiIO &io = ImGui::GetIO();
  auto posAt = [&](ImVec2 m) {
    const int l = std::max(0, std::min(static_cast<int>(g_ed.lines.size()) - 1, static_cast<int>((m.y - origin.y) / lh)));
    return Pos{l, ColAt(g_ed.lines[l], m.x - tx)};
  };
  if (bHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    g_ed.bFocus = true;
    g_ed.bComplete = false;
    const Pos at = posAt(io.MousePos);
    if (io.KeyShift) {
      if (!g_ed.bSel) {
        g_ed.anchor = g_ed.cur;
        g_ed.bSel = true;
      }
      g_ed.cur = at;
    } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      const std::string &s = g_ed.lines[at.line];
      int c0 = at.col, c1 = at.col;
      while (c0 > 0 && IdChar(s[c0 - 1]))
        --c0;
      while (c1 < static_cast<int>(s.size()) && IdChar(s[c1]))
        ++c1;
      g_ed.anchor = Pos{at.line, c0};
      g_ed.cur = Pos{at.line, c1};
      g_ed.bSel = true;
    } else {
      g_ed.cur = g_ed.anchor = at;
      g_ed.bSel = true;
      g_ed.bDragging = true;
    }
    g_ed.fWantX = -1;
  }
  if (g_ed.bDragging) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
      g_ed.cur = posAt(io.MousePos);
    else
      g_ed.bDragging = false;
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bHover && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    g_ed.bFocus = g_ed.bFind && g_ed.bFindFocus ? false : (ImGui::IsAnyItemHovered() ? false : g_ed.bFocus && false);
  // the lines
  std::set<std::string> fnSet(fns.begin(), fns.end());
  int nState = 0;
  for (int l = 0; l < nFirst; ++l)
    Lex(g_ed.lines[l], &nState, fnSet);
  Pos sa, sb;
  SelRange(&sa, &sb);
  const bool bSel = HasSel();
  const std::string findLow = g_ed.bFind ? Lower(g_ed.szFind) : std::string();
  g_hoverTip.clear();
  for (int l = nFirst; l <= nLast; ++l) {
    const std::string &s = g_ed.lines[l];
    const float y = origin.y + lh * l;
    const float ty = y + (lh - P(kCodePx)) / 2.0f;
    if (l == g_ed.cur.line && g_ed.bFocus)
      cd->AddRectFilled(ImVec2(origin.x + scrollX, y), ImVec2(origin.x + scrollX + b.x - a.x, y + lh), C(TK_ROW_HI, 0.6f));
    // the find's matches, then the selection
    if (!findLow.empty()) {
      const std::string low = Lower(s);
      for (size_t at = low.find(findLow); at != std::string::npos; at = low.find(findLow, at + findLow.size()))
        cd->AddRectFilled(ImVec2(tx + ColX(s, static_cast<int>(at)), y + P(1.0f)),
                          ImVec2(tx + ColX(s, static_cast<int>(at + findLow.size())), y + lh - P(1.0f)), C(TK_WARN, 0.28f), P(2.0f));
    }
    if (bSel && l >= sa.line && l <= sb.line) {
      const float x0 = tx + (l == sa.line ? ColX(s, sa.col) : 0.0f);
      const float x1 = tx + (l == sb.line ? ColX(s, sb.col) : ColX(s, static_cast<int>(s.size())) + g_cw * 0.6f);
      cd->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + lh), C(TK_SEL, 0.9f));
    }
    // the words, coloured
    const std::vector<Span> spans = Lex(s, &nState, fnSet);
    for (const Span &sp : spans) {
      const std::string piece = Shown(s.substr(static_cast<size_t>(sp.a), static_cast<size_t>(sp.b - sp.a)));
      const float x = tx + ColX(s, sp.a);
      StudioText(cd, STUDIO_FONT_MONO, kCodePx, ImVec2(x, ty), KindCol(sp.k), piece.c_str());
      if (bHover && sp.k == K_API && io.MousePos.y >= y && io.MousePos.y < y + lh && io.MousePos.x >= x &&
          io.MousePos.x < tx + ColX(s, sp.b)) {
        int n = 0;
        const ModLuaName *api = ModLuaApi(&n);
        const std::string w = s.substr(static_cast<size_t>(sp.a), static_cast<size_t>(sp.b - sp.a));
        for (int i = 0; i < n; ++i)
          if (w == api[i].pszName)
            g_hoverTip = w + std::string(api[i].pszArgs) + "\n" + api[i].pszHelp;
      }
    }
    // the problems, underlined where the check puts them
    for (const ModLuaProblem &pr : check.problems) {
      if (pr.nLine - 1 != l)
        continue;
      int c0 = std::min(pr.nCol, static_cast<int>(s.size()));
      int c1 = pr.nEndLine == pr.nLine ? std::min(pr.nEndCol, static_cast<int>(s.size())) : static_cast<int>(s.size());
      if (c1 <= c0) {
        c1 = c0;
        while (c1 < static_cast<int>(s.size()) && IdChar(s[c1]))
          ++c1;
        if (c1 == c0) {
          c0 = static_cast<int>(Indent(s).size());
          c1 = std::max(c0 + 1, static_cast<int>(s.size()));
        }
      }
      const float x0 = tx + ColX(s, c0), x1 = std::max(x0 + g_cw, tx + ColX(s, c1));
      const float wy = y + lh - P(3.0f);
      for (float x = x0; x < x1; x += P(4.0f)) {
        const float xe = std::min(x + P(2.0f), x1);
        cd->AddLine(ImVec2(x, wy), ImVec2(xe, wy - P(2.0f)), C(TK_BAD), P(1.2f));
        cd->AddLine(ImVec2(xe, wy - P(2.0f)), ImVec2(std::min(xe + P(2.0f), x1), wy), C(TK_BAD), P(1.2f));
      }
      if (bHover && io.MousePos.y >= y && io.MousePos.y < y + lh && io.MousePos.x >= x0 && io.MousePos.x <= x1)
        g_hoverTip = pr.what;
    }
    // the cursor
    if (l == g_ed.cur.line && g_ed.bFocus && fmod(ImGui::GetTime(), 1.0) < 0.6) {
      const float cx = tx + ColX(s, g_ed.cur.col);
      cd->AddLine(ImVec2(cx, y + P(2.0f)), ImVec2(cx, y + lh - P(2.0f)), C(TK_HI), P(1.5f));
    }
  }
  // the gutter over the text that scrolls under it, with each line's number and its problem's dot
  const float gx = origin.x + scrollX;
  cd->AddRectFilled(ImVec2(gx, origin.y + scrollY), ImVec2(gx + gutter - P(6.0f), origin.y + scrollY + viewH), C(TK_PANE2));
  cd->AddLine(ImVec2(gx + gutter - P(6.0f), origin.y + scrollY), ImVec2(gx + gutter - P(6.0f), origin.y + scrollY + viewH), C(TK_LINE));
  std::set<int> bad;
  for (const ModLuaProblem &pr : check.problems)
    bad.insert(pr.nLine - 1);
  for (int l = nFirst; l <= nLast; ++l) {
    const std::string n = std::to_string(l + 1);
    const float y = origin.y + lh * l + (lh - P(kCodePx * 0.92f)) / 2.0f;
    const float nw = TW(STUDIO_FONT_MONO, kCodePx * 0.92f, n);
    StudioText(cd, STUDIO_FONT_MONO, kCodePx * 0.92f, ImVec2(gx + gutter - P(14.0f) - nw, y),
               l == g_ed.cur.line ? C(TK_INK) : C(TK_INK3), n.c_str());
    if (bad.count(l))
      cd->AddCircleFilled(ImVec2(gx + P(8.0f), origin.y + lh * l + lh / 2.0f), P(3.5f), C(TK_BAD));
  }
  // the cursor kept in view
  if (g_ed.bScrollToCursor) {
    g_ed.bScrollToCursor = false;
    const float cy = lh * g_ed.cur.line;
    if (cy < scrollY + lh)
      ImGui::SetScrollY(std::max(0.0f, cy - lh * 3));
    else if (cy + lh > scrollY + viewH - lh * 2)
      ImGui::SetScrollY(cy + lh * 4 - viewH);
    const float cx = gutter + ColX(g_ed.lines[g_ed.cur.line], g_ed.cur.col);
    if (cx < scrollX + gutter + g_cw * 4)
      ImGui::SetScrollX(std::max(0.0f, cx - gutter - g_cw * 8));
    else if (cx > scrollX + (b.x - a.x) - g_cw * 6)
      ImGui::SetScrollX(cx - (b.x - a.x) + g_cw * 12);
  }
  // completion, under the cursor
  if (g_ed.bComplete && g_ed.bFocus && !g_ed.matches.empty()) {
    const std::string &s = g_ed.lines[g_ed.cur.line];
    const float cx = tx + ColX(s, g_ed.cur.col - static_cast<int>(g_ed.prefix.size()));
    const float cy = origin.y + lh * (g_ed.cur.line + 1) + P(2.0f);
    ImDrawList *fd = ImGui::GetForegroundDrawList();
    const int nShow = std::min(static_cast<int>(g_ed.matches.size()), 9);
    const int nTop = std::max(0, std::min(g_ed.nCompleteSel - nShow / 2, static_cast<int>(g_ed.matches.size()) - nShow));
    float w = P(260.0f);
    for (int i = nTop; i < nTop + nShow; ++i)
      w = std::max(w, TW(STUDIO_FONT_MONO, kStudioFsS, g_ed.matches[static_cast<size_t>(i)].first) + P(40.0f));
    const float rh = P(24.0f), helpH = P(40.0f);
    const ImVec2 pa(cx - P(6.0f), cy), pb(cx - P(6.0f) + w + P(260.0f), cy + rh * nShow + helpH);
    fd->AddRectFilled(ImVec2(pa.x + P(3.0f), pa.y + P(4.0f)), ImVec2(pb.x + P(3.0f), pb.y + P(4.0f)), C(TK_SHADOW, 0.5f), P(4.0f));
    fd->AddRectFilled(pa, pb, C(TK_MENU), P(4.0f));
    fd->AddRect(pa, pb, C(TK_MENU_RIM), P(4.0f));
    for (int i = nTop; i < nTop + nShow; ++i) {
      const auto &m = g_ed.matches[static_cast<size_t>(i)];
      const float ry = pa.y + rh * (i - nTop);
      const ImVec2 ra(pa.x + P(3.0f), ry + P(2.0f)), rb(pb.x - P(3.0f), ry + rh - P(1.0f));
      if (i == g_ed.nCompleteSel)
        fd->AddRectFilled(ra, rb, C(TK_MENU_SEL), P(3.0f));
      if (io.MousePos.x >= ra.x && io.MousePos.x < rb.x && io.MousePos.y >= ra.y && io.MousePos.y < rb.y && ImGui::IsMouseClicked(0)) {
        g_ed.nCompleteSel = i;
        CompleteAccept();
        Commit();
        break;
      }
      StudioText(fd, STUDIO_FONT_MONO, kStudioFsS, ImVec2(ra.x + P(8.0f), Mid(ry, rh, kStudioFsS)), C(TK_INK), m.first.c_str());
      const size_t sp = m.second.find("  ");
      const std::string sig = sp == std::string::npos ? m.second : m.second.substr(0, sp);
      StudioText(fd, STUDIO_FONT_MONO, kStudioFsXs, ImVec2(ra.x + w, Mid(ry, rh, kStudioFsXs)), C(TK_INK3), sig.c_str(), 0.0f, false,
                 P(250.0f));
    }
    if (g_ed.nCompleteSel < static_cast<int>(g_ed.matches.size())) {
      const auto &m = g_ed.matches[static_cast<size_t>(g_ed.nCompleteSel)];
      const size_t sp = m.second.find("  ");
      const std::string help = sp == std::string::npos ? std::string() : m.second.substr(sp + 2);
      fd->AddLine(ImVec2(pa.x, pb.y - helpH), ImVec2(pb.x, pb.y - helpH), C(TK_LINE));
      StudioText(fd, STUDIO_FONT_TEXT, kStudioFsXs, ImVec2(pa.x + P(10.0f), pb.y - helpH + P(6.0f)), C(TK_INK2),
                 (m.first + (sp == std::string::npos ? "" : m.second.substr(0, sp))).c_str(), 0.0f, false, pb.x - pa.x - P(20.0f));
      StudioText(fd, STUDIO_FONT_TEXT, kStudioFsXs, ImVec2(pa.x + P(10.0f), pb.y - helpH + P(21.0f)), C(TK_INK3), help.c_str(), 0.0f,
                 false, pb.x - pa.x - P(20.0f));
    }
  }
  if (!g_hoverTip.empty())
    ImGui::SetTooltip("%s", g_hoverTip.c_str());
  ImGui::EndChild();
}

// ---- the test game's run and log ----------------------------------------------------------------------------

struct Run {
  std::string function;
  std::string state;  // "", "pushing", "calling", "done", "refused"
  std::string answer; // script_call's answer, or why
  int nPush = 0;
  bool bSelected = true;
};
Run g_run;

std::vector<std::string> g_gameLog;
unsigned g_nLogSeen;  // the game's lines read so far
double g_dLastPoll;
bool g_bPolling;
std::string g_gameState;  // the scripts' state as the game has them, raw

void Poll() {
  if (g_bPolling || !DesignInGame() || ImGui::GetTime() - g_dLastPoll < 0.5)
    return;
  g_dLastPoll = ImGui::GetTime();
  g_bPolling = true;
  DesignCall("scripts", "\"since\":" + std::to_string(g_nLogSeen), [](const PortJson &a) {
    g_bPolling = false;
    if (!(a["ok"].kind == PortJson::BOOL && a["ok"].b))
      return;
    const unsigned nLogged = static_cast<unsigned>(a["logged"].Num(0));
    if (nLogged < g_nLogSeen)
      g_gameLog.push_back("-- a new game --");
    for (const PortJson &l : a["log"].items)
      g_gameLog.push_back(l.Str());
    while (g_gameLog.size() > 300)
      g_gameLog.erase(g_gameLog.begin());
    g_nLogSeen = nLogged;
    g_gameState = PortJsonWrite(a);
  });
}

// The function called in the test game once the push it waits on is taken: each answer of a small
// request looks again, so it goes on whether or not the workspace is drawn.
void RunStep() {
  if (g_run.state != "pushing" || g_run.nPush == 0)
    return;
  std::string state, answer;
  unsigned nChange = 0;
  if (!StudioPushInfo(g_run.nPush, &state, &answer, &nChange)) {
    g_run.state = "refused";
    g_run.answer = "the push is not in the list";
    return;
  }
  if (state == "asked") {
    DesignCall("studio_status", "", [](const PortJson &) { RunStep(); });
    return;
  }
  if (state != "pushed") {
    g_run.state = "refused";
    PortJson a;
    PortJsonParse(answer.c_str(), &a);
    g_run.answer = "the push was refused: " + a["error"].Str(answer.c_str());
    return;
  }
  g_run.state = "calling";
  std::string fields = "\"function\":" + Q(g_run.function);
  if (g_run.bSelected)
    fields += ",\"unit\":\"selected\",\"target\":\"second\"";
  DesignCall("script_call", fields, [](const PortJson &a) {
    const bool bOk = a["ok"].kind == PortJson::BOOL && a["ok"].b;
    g_run.state = bOk && a["called"].kind == PortJson::BOOL && a["called"].b ? "done" : "refused";
    g_run.answer = bOk ? PortJsonWrite(a) : a["error"].Str("refused");
  });
}

bool StartRun(const std::string &fn, bool bSelected, std::string *pWhy) {
  if (!DesignInGame()) {
    *pWhy = "the test game is not running: start one in Test";
    return false;
  }
  if (g_run.state == "pushing" || g_run.state == "calling") {
    *pWhy = "a run is going: wait for it";
    return false;
  }
  g_run = Run();
  g_run.function = fn;
  g_run.bSelected = bSelected;
  g_run.state = "pushing";
  g_run.nPush = 0;
  DesignCall("studio_push", "\"sender\":\"you\"", [](const PortJson &a) {
    if (!(a["ok"].kind == PortJson::BOOL && a["ok"].b)) {
      g_run.state = "refused";
      g_run.answer = "the push was refused: " + a["error"].Str("refused");
      return;
    }
    g_run.nPush = static_cast<int>(a["push"].Num(0));
    RunStep();
  });
  return true;
}

// ---- the commands -----------------------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::string ProblemsJson(const Check &c) {
  std::string s;
  for (const ModLuaProblem &p : c.problems)
    s += std::string(s.empty() ? "" : ",") + "{\"line\":" + std::to_string(p.nLine) + ",\"col\":" + std::to_string(p.nCol) +
         ",\"end_col\":" + std::to_string(p.nEndCol) + ",\"what\":" + Q(p.what) + "}";
  return "[" + s + "]";
}

std::string FileJson(const std::string &file) {
  const std::string text = TextOf(file);
  const Check &c = CheckOf(file, text);
  std::string fns;
  for (const ModLuaFunction &f : c.fns) {
    std::string uses;
    for (const std::string &u : UsesOf(f.name))
      uses += std::string(uses.empty() ? "" : ",") + Q(u);
    fns += std::string(fns.empty() ? "" : ",") + "{\"name\":" + Q(f.name) + ",\"args\":" + Q(f.args) + ",\"line\":" +
           std::to_string(f.nLine) + ",\"used_by\":[" + uses + "]}";
  }
  return "{\"file\":" + Q(file) + ",\"lines\":" + std::to_string(Split(text).size()) + ",\"problems\":" + ProblemsJson(c) +
         ",\"functions\":[" + fns + "]}";
}

bool Open(std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  return true;
}

// studio_scripts {file?}: the files with their problems and functions (each with where the mod
// names it), the hooks, the last run in the test game and the game's log of the scripts.
bool CmdScripts(const PortJson &j, std::string *pAnswer) {
  if (!Open(pAnswer))
    return false;
  Poll();  // the game's log, for the next answer, while the workspace is not drawn
  const std::string only = j["file"].Str();
  std::string files;
  for (const std::string &f : Files())
    if (only.empty() || f == only)
      files += std::string(files.empty() ? "" : ",") + FileJson(f);
  int n = 0;
  const ModLuaName *hooks = ModLuaHooks(&n);
  std::string hs;
  for (int i = 0; i < n; ++i)
    hs += std::string(i ? "," : "") + "{\"hook\":" + Q(hooks[i].pszName) + ",\"named\":" + Q(hooks[i].pszArgs) + ",\"when\":" +
          Q(hooks[i].pszHelp) + "}";
  std::string log;
  for (size_t i = g_gameLog.size() > 40 ? g_gameLog.size() - 40 : 0; i < g_gameLog.size(); ++i)
    log += std::string(log.empty() ? "" : ",") + Q(g_gameLog[i]);
  *pAnswer = "\"files\":[" + files + "],\"hooks\":[" + hs + "],\"run\":{\"function\":" + Q(g_run.function) + ",\"state\":" +
             Q(g_run.state) + ",\"answer\":" + Q(g_run.answer) + "},\"game_log\":[" + log + "],\"shown\":" + Q(g_sel);
  return true;
}

// studio_script_set {file, text, merge?}: a file's text, as one edit (merge: joined to the last
// one of the same file made in the last seconds, as typing is). Its problems in the answer.
bool CmdScriptSet(const PortJson &j, std::string *pAnswer) {
  if (!Open(pAnswer))
    return false;
  const std::string file = j["file"].Str();
  if (!FileNameOk(file)) {
    *pAnswer = "file is a script's name in scripts/: letters, digits, _ and -, ending in .lua or .luau";
    return false;
  }
  if (j["text"].kind != PortJson::STRING) {
    *pAnswer = "text is the file's whole text";
    return false;
  }
  std::string why;
  const bool bMerge = j["merge"].kind == PortJson::BOOL && j["merge"].b;
  if (StudioDoc("scripts")[file.c_str()].kind == PortJson::NONE || TextOf(file) != j["text"].s) {
    if (bMerge) {
      // typing joins the last few seconds' edit of the same file
      StudioGroupBegin(Sender(j), "changed scripts/" + file, "script:" + file);
      const bool bOk = StudioPut("scripts", {file}, StudioJsonString(j["text"].s), Sender(j), "changed scripts/" + file, &why);
      StudioGroupEnd();
      if (!bOk) {
        *pAnswer = why;
        return false;
      }
    } else if (!StudioPut("scripts", {file}, StudioJsonString(j["text"].s), Sender(j), "changed scripts/" + file, &why)) {
      *pAnswer = why;
      return false;
    }
  }
  *pAnswer = "\"file\":" + Q(file) + ",\"problems\":" + ProblemsJson(CheckFile(file)) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

const char *const kNewScript =
    "-- Functions the mod's records name: a unit's scripts, an order's toggle, channel or autocast,\n"
    "-- a script block. Each is called with e, what the game says of the moment (docs/mod-data.md).\n"
    "\n"
    "function hello(e)\n"
    "  hx.log(\"hello from\", e.unit)\n"
    "end\n";

// studio_script_new {file, text?}: a script made, from a short start unless text is given.
bool CmdScriptNew(const PortJson &j, std::string *pAnswer) {
  if (!Open(pAnswer))
    return false;
  std::string file = j["file"].Str();
  if (file.find('.') == std::string::npos)
    file += ".lua";
  if (!FileNameOk(file)) {
    *pAnswer = "file is a script's name in scripts/: letters, digits, _ and -, ending in .lua or .luau";
    return false;
  }
  if (StudioDoc("scripts")[file.c_str()].kind != PortJson::NONE) {
    *pAnswer = "scripts/" + file + " is there already";
    return false;
  }
  std::string why;
  if (!StudioPut("scripts", {file}, StudioJsonString(j["text"].Str(kNewScript)), Sender(j), "made scripts/" + file, &why)) {
    *pAnswer = why;
    return false;
  }
  g_sel = file;
  *pAnswer = "\"made\":" + Q(file) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// studio_script_delete {file}: a script taken away, as one edit; its file goes with the next save.
bool CmdScriptDelete(const PortJson &j, std::string *pAnswer) {
  if (!Open(pAnswer))
    return false;
  const std::string file = j["file"].Str();
  if (StudioDoc("scripts")[file.c_str()].kind == PortJson::NONE) {
    *pAnswer = "no script " + file;
    return false;
  }
  // the functions it gave that the mod still names
  std::string named;
  for (const ModLuaFunction &f : CheckFile(file).fns)
    if (!UsesOf(f.name).empty())
      named += std::string(named.empty() ? "" : ",") + Q(f.name);
  std::string why;
  if (!StudioPut("scripts", {file}, PortJson(), Sender(j), "took away scripts/" + file, &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"deleted\":" + Q(file) + ",\"still_named\":[" + named + "]";
  if (g_sel == file)
    g_sel.clear();
  return true;
}

// studio_script_rename {file, to}: a script under a new name, as one edit.
bool CmdScriptRename(const PortJson &j, std::string *pAnswer) {
  if (!Open(pAnswer))
    return false;
  const std::string file = j["file"].Str();
  std::string to = j["to"].Str();
  if (to.find('.') == std::string::npos)
    to += ".lua";
  if (StudioDoc("scripts")[file.c_str()].kind == PortJson::NONE || !FileNameOk(to) ||
      StudioDoc("scripts")[to.c_str()].kind != PortJson::NONE) {
    *pAnswer = "file names a script and to a new name of none there, ending in .lua or .luau";
    return false;
  }
  const PortJson text = StudioDoc("scripts")[file.c_str()];
  std::string why;
  StudioGroupBegin(Sender(j), "renamed scripts/" + file + " to " + to);
  const bool bOk = StudioPut("scripts", {to}, text, Sender(j), "", &why) && StudioPut("scripts", {file}, PortJson(), Sender(j), "", &why);
  StudioGroupEnd();
  if (!bOk) {
    *pAnswer = why;
    return false;
  }
  if (g_sel == file)
    g_sel = to;
  *pAnswer = "\"renamed\":" + Q(to);
  return true;
}

std::string FileOfFunction(const std::string &fn, int *pnLine) {
  for (const std::string &f : Files())
    for (const ModLuaFunction &m : CheckFile(f).fns)
      if (m.name == fn) {
        *pnLine = m.nLine;
        return f;
      }
  return std::string();
}

int g_nGoLine;  // a line to go to once the file shows
std::string g_pendingFind;
std::string g_runFn;  // the function Try it calls, the one last picked in the list

// studio_script_show {file?, function?, line?, find?}: the Scripts workspace shown at a file, a
// function's line or a line, and Find opened on a word.
bool CmdScriptShow(const PortJson &j, std::string *pAnswer) {
  if (!Open(pAnswer))
    return false;
  StudioWorkspaceSet(13);
  int nLine = static_cast<int>(j["line"].Num(0));
  std::string file = j["file"].Str();
  if (!j["function"].Str().empty()) {
    file = FileOfFunction(j["function"].Str(), &nLine);
    g_runFn = j["function"].Str();
    if (file.empty()) {
      *pAnswer = "no script gives the function " + j["function"].Str();
      return false;
    }
  }
  if (!file.empty()) {
    if (StudioDoc("scripts")[file.c_str()].kind == PortJson::NONE) {
      *pAnswer = "no script " + file;
      return false;
    }
    g_sel = file;
  }
  g_nGoLine = nLine;
  if (j["find"].kind == PortJson::STRING) {
    g_pendingFind = j["find"].s;
  }
  *pAnswer = "\"shown\":" + Q(g_sel) + ",\"line\":" + std::to_string(nLine);
  return true;
}

// studio_script_run {function, unit?}: the mod pushed into the test game and the function called
// there, with e.unit the unit selected in the game and e.target the second (unit "none" for
// neither); the answer comes in studio_scripts' run.
bool CmdScriptRun(const PortJson &j, std::string *pAnswer) {
  if (!Open(pAnswer))
    return false;
  const std::string fn = j["function"].Str();
  int nLine = 0;
  if (FileOfFunction(fn, &nLine).empty()) {
    *pAnswer = "no script gives the function " + fn;
    return false;
  }
  std::string why;
  if (!StartRun(fn, j["unit"].Str("selected") != "none", &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"run\":" + Q(fn) + ",\"state\":\"pushing\"";
  return true;
}

// studio_script_api: the names a script has, with their signatures, and the hooks.
bool CmdScriptApi(const PortJson &, std::string *pAnswer) {
  int n = 0;
  const ModLuaName *api = ModLuaApi(&n);
  std::string s;
  for (int i = 0; i < n; ++i)
    s += std::string(i ? "," : "") + "{\"name\":" + Q(api[i].pszName) + ",\"args\":" + Q(api[i].pszArgs) + ",\"help\":" + Q(api[i].pszHelp) + "}";
  int nh = 0;
  const ModLuaName *hooks = ModLuaHooks(&nh);
  std::string h;
  for (int i = 0; i < nh; ++i)
    h += std::string(i ? "," : "") + "{\"hook\":" + Q(hooks[i].pszName) + ",\"named\":" + Q(hooks[i].pszArgs) + ",\"when\":" +
         Q(hooks[i].pszHelp) + "}";
  *pAnswer = "\"api\":[" + s + "],\"hooks\":[" + h + "]";
  return true;
}

// ---- the workspace's panes -----------------------------------------------------------------------------------

void Lab(const char *psz) {
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), psz, 0.1f, true);
}

void Wrapped(const std::string &s, float fW, float fPx, int tok, int nFont = STUDIO_FONT_TEXT) {
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fW);
  StudioPushFont(nFont, fPx);
  ImGui::PushStyleColor(ImGuiCol_Text, C(tok));
  ImGui::TextWrapped("%s", s.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
}

bool Row(const char *pszId, const char *pszGlyph, const std::string &title, const std::string &sub, bool bOn, float fW, int nBad) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = sub.empty() ? P(30.0f) : P(44.0f);
  const bool b = ImGui::InvisibleButton(pszId, ImVec2(fW, h));
  const bool bHover = ImGui::IsItemHovered();
  if (bOn || bHover)
    d->AddRectFilled(p, ImVec2(p.x + fW, p.y + h), C(bOn ? TK_SEL : TK_ROW_HI), P(3.0f));
  if (bOn)
    d->AddRectFilled(p, ImVec2(p.x + P(3.0f), p.y + h), C(TK_SEL_LINE));
  StudioGlyph(d, pszGlyph, ImVec2(p.x + P(8.0f), p.y + (sub.empty() ? P(7.0f) : P(6.0f))), P(16.0f), C(TK_LABEL));
  float right = fW - P(8.0f);
  if (nBad > 0) {
    const std::string n = std::to_string(nBad);
    const float cw = StudioChipW(n.c_str());
    StudioChip(d, ImVec2(p.x + right - cw, p.y + P(6.0f)), n.c_str(), CH_BAD);
    right -= cw + P(6.0f);
  }
  T(d, STUDIO_FONT_BOLD, 13.0f, p.x + P(30.0f), p.y + (sub.empty() ? Mid(0, h, 13.0f) : P(5.0f)), bOn ? C(TK_HI) : C(TK_INK), title,
    right - P(30.0f));
  if (!sub.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(30.0f), p.y + P(24.0f), C(TK_INK3), sub, fW - P(36.0f));
  return b;
}

char g_szNewName[48];
bool g_bNaming = false;

void LeftPane(ImVec2 a, ImVec2 b) {
  ImGui::SetCursorScreenPos(a);
  if (!ImGui::BeginChild("##scrleft", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::EndChild();
    return;
  }
  const float fW = b.x - a.x - P(24.0f);
  ImGui::SetCursorPos(ImVec2(P(12.0f), P(12.0f)));
  ImGui::BeginGroup();
  const std::vector<std::string> files = Files();
  Lab(("Files  " + std::to_string(files.size())).c_str());
  ImGui::Dummy(ImVec2(1, P(2.0f)));
  for (const std::string &f : files) {
    const Check &c = CheckFile(f);
    ImGui::PushID(f.c_str());
    const std::string sub = std::to_string(c.fns.size()) + (c.fns.size() == 1 ? " function" : " functions") +
                            (c.problems.empty() ? "" : ", " + std::to_string(c.problems.size()) + (c.problems.size() == 1 ? " problem" : " problems"));
    if (Row("##f", "pen", "scripts/" + f, sub, g_sel == f, fW, static_cast<int>(c.problems.size())))
      g_sel = f;
    if (ImGui::BeginPopupContextItem("##fctx")) {
      if (StudioItem("Delete"))
        StudioAsk("studio_script_delete", "\"file\":" + Q(f));
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (files.empty())
    Wrapped("No scripts yet. A script is Lua (Luau) the mod's records call by its functions' names.", fW, kStudioFsS, TK_INK3);
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  if (!g_bNaming) {
    if (StudioButton("##newscr", "New script", "plus", SB_GOOD, 26.0f)) {
      g_bNaming = true;
      snprintf(g_szNewName, sizeof g_szNewName, "%s", files.empty() ? "main" : "more");
    }
  } else {
    const bool bEnter = StudioInput("##newname", g_szNewName, sizeof g_szNewName, (fW - P(70.0f)) / StudioPx(1.0f), 26.0f, "name");
    (void)bEnter;
    ImGui::SameLine(0.0f, P(6.0f));
    if (StudioButton("##mk", "Make", nullptr, SB_GOOD, 26.0f) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      StudioAsk("studio_script_new", "\"file\":" + Q(g_szNewName));
      std::string n = g_szNewName;
      g_sel = n.find('.') == std::string::npos ? n + ".lua" : n;
      g_bNaming = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
      g_bNaming = false;
  }
  // the shown file's functions, each with where the mod names it
  if (!g_sel.empty() && StudioDoc("scripts")[g_sel.c_str()].kind != PortJson::NONE) {
    ImGui::Dummy(ImVec2(1, P(14.0f)));
    const Check &c = CheckOf(g_sel, Join(g_ed.file == g_sel ? g_ed.lines : Split(TextOf(g_sel))));
    Lab(("Functions  " + std::to_string(c.fns.size())).c_str());
    ImGui::Dummy(ImVec2(1, P(2.0f)));
    for (const ModLuaFunction &f : c.fns) {
      const std::vector<std::string> uses = UsesOf(f.name);
      std::string sub = uses.empty() ? std::string("named by nothing yet") : uses[0];
      if (uses.size() > 1)
        sub += " and " + std::to_string(uses.size() - 1) + " more";
      ImGui::PushID(f.name.c_str());
      if (Row("##fn", "action", f.name + "(" + f.args + ")", "line " + std::to_string(f.nLine) + " - " + sub,
              g_ed.file == g_sel && g_ed.cur.line + 1 == f.nLine, fW, 0)) {
        GoTo(f.nLine);
        g_runFn = f.name;
      }
      if (ImGui::IsItemHovered() && uses.size() > 1) {
        std::string all;
        for (const std::string &u : uses)
          all += u + "\n";
        ImGui::SetTooltip("%s", all.c_str());
      }
      ImGui::PopID();
    }
    if (c.fns.empty())
      Wrapped("A function at the script's top (function name(e) ... end) is one the mod's records can name.", fW, kStudioFsXs, TK_INK3);
  }
  // the hooks, and what calls them
  ImGui::Dummy(ImVec2(1, P(14.0f)));
  Lab("Hooks: what calls them");
  ImGui::Dummy(ImVec2(1, P(2.0f)));
  int n = 0;
  const ModLuaName *hooks = ModLuaHooks(&n);
  for (int i = 0; i < n; ++i) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *d = ImGui::GetWindowDrawList();
    T(d, STUDIO_FONT_BOLD, kStudioFsS, p.x, p.y, C(TK_INK), hooks[i].pszName);
    ImGui::Dummy(ImVec2(fW, P(17.0f)));
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(17.0f)));
    Wrapped(hooks[i].pszHelp, fW, kStudioFsXs, TK_INK2);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x, ImGui::GetCursorScreenPos().y, C(TK_INK3), hooks[i].pszArgs, fW);
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
  }
  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(1, P(12.0f)));
  ImGui::EndChild();
}

void RightPane(ImVec2 a, ImVec2 b, const std::vector<std::string> &fns) {
  ImGui::SetCursorScreenPos(a);
  if (!ImGui::BeginChild("##scrright", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::EndChild();
    return;
  }
  const float fW = b.x - a.x - P(28.0f);
  ImGui::SetCursorPos(ImVec2(P(14.0f), P(12.0f)));
  ImGui::BeginGroup();
  Lab("Try it in the test game");
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  std::string &s_fn = g_runFn;
  if (s_fn.empty() || std::find(fns.begin(), fns.end(), s_fn) == fns.end())
    s_fn = fns.empty() ? std::string() : fns[0];
  if (StudioSelectBegin("##runfn", s_fn.empty() ? "no function yet" : s_fn.c_str(), fW / StudioPx(1.0f), 26.0f)) {
    for (const std::string &f : fns)
      if (StudioItem(f.c_str(), f == s_fn))
        s_fn = f;
    StudioSelectEnd();
  }
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  static bool s_bSelected = true;
  StudioCheck("##runsel", "On the units selected in the game", &s_bSelected);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("e.unit and e.caster the first unit selected, e.target the second");
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  const bool bGoing = g_run.state == "pushing" || g_run.state == "calling";
  if (StudioButton("##runit", bGoing ? "Pushing and calling" : "Push and call", "play", SB_GOOD, 28.0f, 0.0f, kStudioFsS,
                   !s_fn.empty() && DesignInGame() && !bGoing)) {
    std::string why;
    if (!StartRun(s_fn, s_bSelected, &why))
      StudioSay(why);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(DesignInGame() ? "The mod pushed into the test game, then the function called there once, outside the game's frame"
                                     : "Start the test game in Test first");
  if (!g_run.state.empty()) {
    ImGui::Dummy(ImVec2(1, P(4.0f)));
    std::string words = g_run.state == "done" ? "called " + g_run.function : g_run.state == "refused" ? "not called" : g_run.state + "...";
    PortJson ans;
    if (g_run.state == "done" && PortJsonParse(g_run.answer.c_str(), &ans)) {
      if (ans["result"].kind == PortJson::NUMBER)
        words += ", it gave " + Fmt(ans["result"].d, 3);
      for (const PortJson &l : ans["log"].items)
        words += "\n" + l.Str();
    } else if (g_run.state == "refused") {
      words += ": " + g_run.answer;
    }
    Wrapped(words, fW, kStudioFsS, g_run.state == "refused" ? TK_BAD : TK_INK2);
  }
  ImGui::Dummy(ImVec2(1, P(16.0f)));
  // the game's log of the scripts
  Lab("The game's log of the scripts");
  ImGui::SameLine(0.0f, P(10.0f));
  if (StudioButton("##clr", "Clear", nullptr, SB_FLAT, 20.0f, 0.0f, kStudioFsXs))
    g_gameLog.clear();
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  if (!DesignInGame() && g_gameLog.empty())
    Wrapped("The test game is not running. Its scripts' lines (hx.log, print) and their errors show here as it plays.", fW,
            kStudioFsXs, TK_INK3);
  else if (g_gameLog.empty())
    Wrapped("Nothing yet.", fW, kStudioFsXs, TK_INK3);
  for (size_t i = g_gameLog.size() > 80 ? g_gameLog.size() - 80 : 0; i < g_gameLog.size(); ++i) {
    const std::string &l = g_gameLog[i];
    const bool bErr = l.find(": ") != std::string::npos && (l.find("scripts/") != std::string::npos || l.find("error") != std::string::npos ||
                                                           l.find("budget") != std::string::npos);
    Wrapped(l, fW, kStudioFsXs, bErr ? TK_BAD : TK_INK2, STUDIO_FONT_MONO);
  }
  ImGui::Dummy(ImVec2(1, P(16.0f)));
  Lab("The same on every machine");
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  Wrapped("A function keeps nothing between calls: values a unit or the game must keep go through hx.set and hx.game_set, which "
          "saves and replays keep. Random is the game's (hx.random). No clock, files or system; no ^ (hx.pow). Each call has a "
          "budget of steps; past it, or at an error, the call stops and the game goes on.",
          fW, kStudioFsXs, TK_INK3);
  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(1, P(12.0f)));
  ImGui::EndChild();
}

std::vector<std::string> AllFunctions() {
  std::vector<std::string> out;
  for (const std::string &f : Files())
    for (const ModLuaFunction &m : CheckOf(f, f == g_ed.file ? Join(g_ed.lines) : TextOf(f)).fns)
      out.push_back(m.name);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace

void StudioScriptsWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float hh = P(44.0f);
  d->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + hh), C(TK_BAR));
  d->AddLine(ImVec2(a.x, a.y + hh - 0.5f), ImVec2(b.x, a.y + hh - 0.5f), C(TK_LINE));
  float x = a.x + P(14.0f);
  x += Wide(d, 13.0f, x, Mid(a.y, hh, 13.0f), C(TK_HEAD), "Scripts", 0.06f) + P(16.0f);
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(20.0f), a.y + hh + P(20.0f), C(TK_INK3), "Open a mod to write its scripts.");
    return;
  }
  Poll();
  RunStep();
  const std::vector<std::string> files = Files();
  if ((g_sel.empty() || std::find(files.begin(), files.end(), g_sel) == files.end()) && !files.empty())
    g_sel = files[0];
  if (!g_sel.empty() && std::find(files.begin(), files.end(), g_sel) != files.end())
    Sync(g_sel);
  else
    g_ed = Editor();
  if (g_nGoLine > 0 && g_ed.file == g_sel) {
    GoTo(g_nGoLine);
    g_nGoLine = 0;
  }
  if (!g_pendingFind.empty()) {
    snprintf(g_ed.szFind, sizeof g_ed.szFind, "%s", g_pendingFind.c_str());
    g_ed.bFind = true;
    g_pendingFind.clear();
    FindNext(false);
  }
  const std::vector<std::string> fns = AllFunctions();
  const std::string text = Join(g_ed.lines);
  const Check &check = g_ed.file.empty() ? Check() : CheckOf(g_ed.file, text);
  // the head's counts
  int nProblems = 0;
  for (const std::string &f : files)
    nProblems += static_cast<int>(CheckOf(f, f == g_ed.file ? text : TextOf(f)).problems.size());
  const std::string counts = std::to_string(files.size()) + (files.size() == 1 ? " file, " : " files, ") + std::to_string(fns.size()) +
                             (fns.size() == 1 ? " function" : " functions");
  x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, hh, kStudioFsS), C(TK_INK3), counts) + P(12.0f);
  if (nProblems > 0) {
    const std::string pr = std::to_string(nProblems) + (nProblems == 1 ? " problem: the loader refuses the mod" : " problems: the loader refuses the mod");
    StudioChip(d, ImVec2(x, Mid(a.y, hh, 18.0f)), pr.c_str(), CH_BAD);
  } else if (!files.empty()) {
    StudioChip(d, ImVec2(x, Mid(a.y, hh, 18.0f)), "checked: the loader takes them", CH_GOOD);
  }
  // the panes
  const float lw = std::min(P(300.0f), (b.x - a.x) * 0.22f), rw = std::min(P(340.0f), (b.x - a.x) * 0.24f);
  const ImVec2 la(a.x + 1, a.y + hh), lb(a.x + lw, b.y - 1);
  const ImVec2 ra(b.x - rw, a.y + hh), rb(b.x - 1, b.y - 1);
  d->AddLine(ImVec2(lb.x + 0.5f, la.y), ImVec2(lb.x + 0.5f, lb.y), C(TK_LINE));
  d->AddLine(ImVec2(ra.x - 0.5f, ra.y), ImVec2(ra.x - 0.5f, rb.y), C(TK_LINE));
  LeftPane(la, lb);
  RightPane(ra, rb, fns);
  // the editor between them: its bar, Find when open, the code, the problem under the cursor
  const ImVec2 ea(lb.x + 1, la.y), eb(ra.x - 1, b.y - 1);
  const float bh = P(36.0f);
  d->AddRectFilled(ea, ImVec2(eb.x, ea.y + bh), C(TK_PANE2));
  d->AddLine(ImVec2(ea.x, ea.y + bh - 0.5f), ImVec2(eb.x, ea.y + bh - 0.5f), C(TK_LINE));
  if (g_ed.file.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ea.x + P(16.0f), ea.y + bh + P(18.0f), C(TK_INK3),
      "New script makes one. Its functions are named by a unit's scripts, an order's toggle, channel or autocast, and the action "
      "editor's script block.",
      eb.x - ea.x - P(32.0f));
    return;
  }
  float bx = ea.x + P(14.0f);
  bx += T(d, STUDIO_FONT_MONO, kStudioFsS, bx, Mid(ea.y, bh, kStudioFsS), C(TK_INK), "scripts/" + g_ed.file) + P(12.0f);
  const bool bUnsaved = StudioUnsaved() > 0;
  if (bUnsaved)
    bx += StudioChip(d, ImVec2(bx, Mid(ea.y, bh, 18.0f)), "unsaved: Ctrl S", CH_CHANGED) + P(10.0f);
  const std::string where = "line " + std::to_string(g_ed.cur.line + 1) + ", column " + std::to_string(CellsOf(g_ed.lines[g_ed.cur.line], g_ed.cur.col) + 1);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, bx, Mid(ea.y, bh, kStudioFsXs), C(TK_INK3), where);
  // Find, Save and the API's names at the bar's right
  const float fb = eb.x - P(12.0f);
  ImGui::SetCursorScreenPos(ImVec2(fb - StudioButtonSize("Find", "search", 26.0f).x, ea.y + (bh - P(26.0f)) / 2.0f));
  if (StudioButton("##find", "Find", "search", g_ed.bFind ? SB_ON : SB_PLAIN, 26.0f)) {
    g_ed.bFind = !g_ed.bFind;
    g_ed.bFindFocus = g_ed.bFind;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Ctrl F");
  float top = ea.y + bh;
  if (g_ed.bFind) {
    const float fh = P(38.0f);
    d->AddRectFilled(ImVec2(ea.x, top), ImVec2(eb.x, top + fh), C(TK_BAR));
    d->AddLine(ImVec2(ea.x, top + fh - 0.5f), ImVec2(eb.x, top + fh - 0.5f), C(TK_LINE));
    ImGui::SetCursorScreenPos(ImVec2(ea.x + P(14.0f), top + (fh - P(26.0f)) / 2.0f));
    if (g_ed.bFindFocus) {
      ImGui::SetKeyboardFocusHere();
      g_ed.bFindFocus = false;
      g_ed.bFocus = false;
    }
    StudioInput("##findtext", g_ed.szFind, sizeof g_ed.szFind, 280.0f, 26.0f, "Find in the script", "search", "Enter");
    const bool bActive = ImGui::IsItemActive();
    if (bActive && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
      FindNext(ImGui::GetIO().KeyShift);
    if (bActive && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      g_ed.bFind = false;
      g_ed.bFocus = true;
    }
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##prev", "", "up", SB_PLAIN, 26.0f))
      FindNext(true);
    ImGui::SameLine(0.0f, P(4.0f));
    if (StudioButton("##next", "", "down", SB_PLAIN, 26.0f))
      FindNext(false);
    ImGui::SameLine(0.0f, P(10.0f));
    const int nFound = FindCount();
    const std::string fw = g_ed.szFind[0] ? (nFound == 0 ? std::string("not found") : std::to_string(nFound) + (nFound == 1 ? " match" : " matches"))
                                         : std::string("Enter for the next, Shift Enter the one before");
    const ImVec2 at = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, at.x, Mid(top, fh, kStudioFsXs), C(nFound == 0 && g_ed.szFind[0] ? TK_BAD : TK_INK3), fw);
    ImGui::NewLine();
    top += fh;
  }
  // the problem line at the foot
  const float ph = check.problems.empty() ? 0.0f : P(30.0f);
  const float fPage = (eb.y - ph - top) / floorf(P(kCodePx) * 1.5f) - 2;
  if (g_ed.bFocus) {
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    if (Keys(fns, fPage))
      Commit();
  }
  DrawEditor(ImVec2(ea.x, top), ImVec2(eb.x, eb.y - ph), check, fns);
  if (ph > 0) {
    const ModLuaProblem &p = check.problems.front();
    d->AddRectFilled(ImVec2(ea.x, eb.y - ph), eb, C(TK_BAD, 0.14f));
    d->AddLine(ImVec2(ea.x, eb.y - ph), ImVec2(eb.x, eb.y - ph), C(TK_BAD, 0.6f));
    ImGui::SetCursorScreenPos(ImVec2(ea.x + P(10.0f), eb.y - ph + P(3.0f)));
    const std::string words = "line " + std::to_string(p.nLine) + ": " + p.what +
                              (check.problems.size() > 1 ? "   (" + std::to_string(check.problems.size() - 1) + " more)" : "");
    if (ImGui::InvisibleButton("##prob", ImVec2(eb.x - ea.x - P(20.0f), ph - P(6.0f))))
      GoTo(p.nLine, p.nCol);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ea.x + P(12.0f), Mid(eb.y - ph, ph, kStudioFsS), C(TK_BAD), words, eb.x - ea.x - P(24.0f));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Go to it");
  }
}

void StudioScriptsFocusFind(void) {
  g_ed.bFind = true;
  g_ed.bFindFocus = true;
  if (HasSel() && g_ed.anchor.line == g_ed.cur.line) {
    Pos a, b;
    SelRange(&a, &b);
    snprintf(g_ed.szFind, sizeof g_ed.szFind, "%s", TextIn(a, b).c_str());
  }
}

void StudioScriptsOpenFunction(const std::string &function) {
  StudioWorkspaceSet(13);
  int nLine = 0;
  const std::string file = function.empty() ? std::string() : FileOfFunction(function, &nLine);
  if (!file.empty()) {
    g_sel = file;
    g_nGoLine = nLine;
    g_runFn = function;
    return;
  }
  // a function not written yet: its start at the end of the first script, or of a new main.lua
  const std::vector<std::string> files = Files();
  const std::string target = files.empty() ? std::string("main.lua") : files[0];
  const std::string name = function.empty() ? std::string("cast") : function;
  const std::string text = files.empty() ? std::string() : TextOf(target);
  const std::string stub = "\nfunction " + name + "(e)\n  -- e.caster, e.target, e.x, e.y, e.order\n  \nend\n";
  StudioAsk(files.empty() ? "studio_script_new" : "studio_script_set",
            "\"file\":" + Q(target) + ",\"text\":" + Q(text + stub));
  g_sel = target;
  g_nGoLine = static_cast<int>(Split(text + stub).size()) - 2;
}

std::vector<std::string> StudioScriptFunctions(void) {
  return AllFunctions();
}

void StudioScriptsInit(void) {
  PortControlCommand("studio_scripts", CmdScripts);
  PortControlCommand("studio_script_set", CmdScriptSet);
  PortControlCommand("studio_script_new", CmdScriptNew);
  PortControlCommand("studio_script_delete", CmdScriptDelete);
  PortControlCommand("studio_script_rename", CmdScriptRename);
  PortControlCommand("studio_script_show", CmdScriptShow);
  PortControlCommand("studio_script_run", CmdScriptRun);
  PortControlCommand("studio_script_api", CmdScriptApi);
}
