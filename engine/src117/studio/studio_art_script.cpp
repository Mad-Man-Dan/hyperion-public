// studio_art_script.cpp -- the image script, iscript.txt, in the Art workspace: its scripts by the
// Name of their headers, each opened in an editor on its own or the whole file at once, checked as
// the loader compiles it, applied through studio_script; and the frame a script shows first, which
// the art's pictures start at (studio_art.cpp).
#include <map>
#include <vector>
#include "../port/data/port_iscript.h"
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// A script of the mod's text, by its header's Name: where its header starts and ends, and each
// animation's label.
struct TextScript {
  std::string name;
  size_t nStart = 0, nEnd = 0;  // the header's .headerstart and the next one's (or the end)
  int nLine = 0;
  std::map<std::string, std::string> anims;
};

std::string Trim(std::string s) {
  while (!s.empty() && isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && isspace(static_cast<unsigned char>(s[i])))
    ++i;
  return s.substr(i);
}

std::vector<TextScript> ScriptsOf(const std::string &text) {
  std::vector<TextScript> out;
  size_t at = 0;
  int nLine = 1;
  bool bIn = false;
  while (at <= text.size()) {
    const size_t eol = std::min(text.find('\n', at), text.size());
    const std::string line = Trim(text.substr(at, eol - at));
    if (line.compare(0, 12, ".headerstart") == 0) {
      if (!out.empty() && out.back().nEnd == 0)
        out.back().nEnd = at;
      TextScript s;
      s.nStart = at;
      s.nLine = nLine;
      out.push_back(s);
      bIn = true;
    } else if (line.compare(0, 10, ".headerend") == 0) {
      bIn = false;
    } else if (bIn && !out.empty() && !line.empty() && line[0] != '#') {
      const size_t sp = line.find_first_of(" \t");
      if (sp != std::string::npos) {
        const std::string k = line.substr(0, sp), v = Trim(line.substr(sp));
        if (k == "Name")
          out.back().name = v;
        else if (k != "Type")
          out.back().anims[k] = v;
      }
    }
    if (eol >= text.size())
      break;
    at = eol + 1;
    ++nLine;
  }
  for (TextScript &s : out)
    if (s.nEnd == 0)
      s.nEnd = text.size();
  return out;
}

// The frame a text script's Init plays first: its label followed, gotos taken, to its first
// playfram.
int TextFirstFrame(const std::string &text, const std::string &name) {
  std::string label;
  for (const TextScript &s : ScriptsOf(text))
    if (s.name == name && s.anims.count("Init"))
      label = s.anims.at("Init");
  if (label.empty() || label == "[NONE]")
    return 0;
  std::vector<std::string> lines;
  size_t at = 0;
  while (at <= text.size()) {
    const size_t eol = std::min(text.find('\n', at), text.size());
    std::string l = text.substr(at, eol - at);
    const size_t hash = l.find('#');
    if (hash != std::string::npos)
      l = l.substr(0, hash);
    lines.push_back(Trim(l));
    if (eol >= text.size())
      break;
    at = eol + 1;
  }
  auto find = [&](const std::string &lab) -> int {
    for (size_t i = 0; i < lines.size(); ++i)
      if (lines[i] == lab + ":")
        return static_cast<int>(i);
    return -1;
  };
  int i = find(label);
  for (int nStep = 0; i >= 0 && nStep < 256 && i + 1 < static_cast<int>(lines.size()); ++nStep) {
    const std::string &l = lines[static_cast<size_t>(++i)];
    if (l.empty() || l.back() == ':')
      continue;
    const size_t sp = l.find_first_of(" \t");
    const std::string op = Lower(l.substr(0, sp)), arg = sp == std::string::npos ? std::string() : Trim(l.substr(sp));
    if (op == "playfram" || op == "playframtile")
      return static_cast<int>(strtol(arg.c_str(), nullptr, 0));
    if (op == "goto")
      i = find(arg);
    else if (op == "end" || op == "return")
      break;
    if (i < 0)
      break;
  }
  return 0;
}

std::string ImageScriptText(const std::string &name) {
  for (const TextScript &s : ScriptsOf(StudioScript()))
    if (s.name == name)
      return StudioScript();
  for (int i = 0; i < StudioDependencies(); ++i)
    for (const TextScript &s : ScriptsOf(StudioDependencyScript(i)))
      if (s.name == name)
        return StudioDependencyScript(i);
  return std::string();
}

// ---- the image script -------------------------------------------------------------------------------

struct ScriptEditor {
  std::string sel;      // the script shown, by Name; empty for the whole file
  std::string text;     // what the editor holds
  std::string loaded;   // the text it was loaded from, to know an edit
  unsigned nChange = ~0u;
  std::string mod;
  std::string check;    // what the compiler says
  unsigned nChecked = ~0u;
  char szFind[48] = {};
} g_script;

int TextResize(ImGuiInputTextCallbackData *p) {
  if (p->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    std::string *s = static_cast<std::string *>(p->UserData);
    s->resize(static_cast<size_t>(p->BufTextLen));
    p->Buf = &(*s)[0];
  }
  return 0;
}

// The section the editor shows: a script's header to the next header, or the whole file.
bool SectionOf(const std::string &text, const std::string &sel, size_t *pFrom, size_t *pTo) {
  if (sel.empty()) {
    *pFrom = 0;
    *pTo = text.size();
    return true;
  }
  for (const TextScript &s : ScriptsOf(text))
    if (s.name == sel) {
      *pFrom = s.nStart;
      *pTo = s.nEnd;
      return true;
    }
  return false;
}

void ScriptLoad(const std::string &sel) {
  g_script.sel = sel;
  size_t from = 0, to = 0;
  const std::string &all = StudioScript();
  if (!SectionOf(all, sel, &from, &to)) {
    g_script.sel.clear();
    from = 0, to = all.size();
  }
  g_script.text = all.substr(from, to - from);
  g_script.loaded = g_script.text;
  g_script.nChange = StudioChange();
  g_script.mod = StudioModId();
}

// The whole file with the editor's section put in its place.
std::string ScriptWhole(void) {
  const std::string &all = StudioScript();
  size_t from = 0, to = 0;
  if (!SectionOf(all, g_script.sel, &from, &to))
    return g_script.sel.empty() ? g_script.text : all + (all.empty() || all.back() == '\n' ? "" : "\n") + g_script.text;
  return all.substr(0, from) + g_script.text + all.substr(to);
}

// The text compiled as the loader compiles it, its names the mod's records: "" when it compiles.
std::string ScriptCheck(const std::string &text) {
  IscriptNames names = [](const char *pszKind, const std::string &name) {
    const int t = strcmp(pszKind, "image") == 0    ? ST_IMAGES
                  : strcmp(pszKind, "sprite") == 0 ? ST_SPRITES
                  : strcmp(pszKind, "weapon") == 0 ? ST_WEAPONS
                  : strcmp(pszKind, "sound") == 0  ? ST_SOUNDS
                                                   : ST_FLINGY;
    StudioRecord r;
    return StudioRecordOf(t, name, &r) ? r.nId : -1;
  };
  std::vector<unsigned char> block;
  std::vector<IscriptHeader> headers;
  std::string err;
  if (IscriptCompile(text, 0, names, &block, &headers, &err))
    return std::string();
  return err.empty() ? std::string("does not compile") : err;
}

// A new script for an image: Init plays its first frame and waits, Walking turns through its sets.
std::string NewScriptText(const std::string &name) {
  std::string label;
  bool bUp = true;
  for (char c : name) {
    if (c == '_') {
      bUp = true;
      continue;
    }
    label += bUp ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : c;
    bUp = false;
  }
  return "\n# " + name + "\n.headerstart\nName          " + name + "\nType          12\nInit          " + label + "Init\nDeath         " + label +
         "Death\nWalking       " + label + "Walking\nWalkingToIdle " + label + "Init\n.headerend\n\n" + label +
         "Init:\n        playfram      0x00\n        goto          " + label + "Local\n\n" + label + "Local:\n        wait          125\n        goto          " +
         label + "Local\n\n" + label + "Walking:\n        move          4\n        wait          1\n        goto          " + label + "Walking\n\n" +
         label + "Death:\n        playsnd       0\n        wait          1\n        end\n";
}

}  // namespace

namespace studio_art {

std::string ScriptTextOf(const std::string &name) {
  return ImageScriptText(name);
}

int ScriptFirstFrame(const std::string &name) {
  return TextFirstFrame(ImageScriptText(name), name);
}

void ScriptPane(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  if (g_script.mod != StudioModId() || (g_script.text == g_script.loaded && g_script.nChange != StudioChange()))
    ScriptLoad(g_script.sel);
  const std::vector<TextScript> scripts = ScriptsOf(StudioScript());
  // the scripts by Name at the left, each with the images that name it
  const float lw = P(260.0f);
  StudioPane(d, a, ImVec2(a.x + lw, b.y), 30.0f);
  Wide(d, 10.5f, a.x + P(10.0f), Mid(a.y, P(30.0f), 10.5f), C(TK_HEAD), "Scripts");
  ImGui::SetCursorScreenPos(ImVec2(a.x + P(8.0f), a.y + P(36.0f)));
  StudioInput("##sfind", g_script.szFind, sizeof g_script.szFind, (lw - P(16.0f)) / P(1.0f), 24.0f, "Find a script", "search");
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + P(68.0f)));
  ImGui::BeginChild("##scripts", ImVec2(lw - 2, b.y - a.y - P(108.0f)));
  if (StudioItem("The whole file", g_script.sel.empty()) && g_script.text == g_script.loaded)
    ScriptLoad(std::string());
  for (const TextScript &s : scripts) {
    if (!Has(s.name, g_script.szFind))
      continue;
    ImGui::PushID(s.name.c_str());
    const std::string label = (s.name.empty() ? std::string("(no Name)") : s.name) + "  line " + std::to_string(s.nLine);
    if (StudioItem(label.c_str(), g_script.sel == s.name) && g_script.text == g_script.loaded)
      ScriptLoad(s.name);
    ImGui::PopID();
  }
  if (scripts.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "  No scripts yet.");
  ImGui::EndChild();
  ImGui::SetCursorScreenPos(ImVec2(a.x + P(8.0f), b.y - P(34.0f)));
  static char s_szNew[48];
  StudioInput("##newname", s_szNew, sizeof s_szNew, (lw - P(90.0f)) / P(1.0f), 26.0f, "new script's name");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##add", "Add", "plus", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, s_szNew[0] != '\0' && StudioIsOpen())) {
    const std::string &all = StudioScript();
    StudioAsk("studio_script", "\"text\":" + Q(all + (all.empty() || all.back() == '\n' ? "" : "\n") + NewScriptText(s_szNew)));
    g_script.sel = s_szNew;
    g_script.loaded = g_script.text;  // loaded again once the edit is in
    g_script.nChange = ~0u;
    s_szNew[0] = '\0';
  }
  // the editor at the right
  const ImVec2 ea(a.x + lw + P(10.0f), a.y), eb = b;
  StudioPane(d, ea, eb, 30.0f);
  float x = ea.x + P(10.0f);
  x += Wide(d, 10.5f, x, Mid(ea.y, P(30.0f), 10.5f), C(TK_HEAD), "iscript.txt") + P(12.0f);
  const bool bDirty = g_script.text != g_script.loaded;
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(ea.y, P(30.0f), kStudioFsS), C(TK_INK2),
    (g_script.sel.empty() ? std::string("the whole file") : g_script.sel) + (bDirty ? ", edited" : ""));
  // what it is used by
  if (!g_script.sel.empty()) {
    std::string users;
    int n = 0;
    for (const StudioRecord &r : StudioRecords(ST_IMAGES))
      if (StudioRecordJson(ST_IMAGES, r.name)["script"].Str() == g_script.sel)
        users += std::string(n++ ? ", " : "") + r.name;
    const float ux = x + TW(STUDIO_FONT_TEXT, kStudioFsS, g_script.sel + ", edited") + P(14.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, ux, Mid(ea.y, P(30.0f), kStudioFsXs), C(TK_INK3), users.empty() ? "no image names it" : "named by " + users,
      eb.x - ux - P(240.0f));
  }
  float bx = eb.x - P(8.0f);
  bx -= StudioButtonSize("Apply", "done", 22.0f, kStudioFsXs).x;
  ImGui::SetCursorScreenPos(ImVec2(bx, ea.y + P(4.0f)));
  if (StudioButton("##apply", "Apply", "done", SB_GOOD, 22.0f, 0.0f, kStudioFsXs, bDirty && StudioIsOpen())) {
    StudioAsk("studio_script", "\"text\":" + Q(ScriptWhole()));
    g_script.loaded = g_script.text;
    g_script.nChange = ~0u;
  }
  bx -= StudioButtonSize("Revert", "undo", 22.0f, kStudioFsXs).x + P(6.0f);
  ImGui::SetCursorScreenPos(ImVec2(bx, ea.y + P(4.0f)));
  if (StudioButton("##revert", "Revert", "undo", SB_PLAIN, 22.0f, 0.0f, kStudioFsXs, bDirty))
    ScriptLoad(g_script.sel);
  // the check, under the editor
  const float ch = P(28.0f);
  if (g_script.nChecked != StudioChange() || bDirty) {
    static std::string s_last;
    const std::string whole = ScriptWhole();
    if (whole != s_last || g_script.nChecked != StudioChange()) {
      s_last = whole;
      g_script.check = ScriptCheck(whole);
    }
    g_script.nChecked = StudioChange();
  }
  const ImVec2 ta(ea.x + 1, ea.y + P(30.0f)), tb(eb.x - 1, eb.y - ch);
  ImGui::SetCursorScreenPos(ta);
  StudioPushFont(STUDIO_FONT_MONO, 13.0f);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, C(TK_WELL));
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK));
  if (g_script.text.capacity() < 16)
    g_script.text.reserve(16);
  ImGui::InputTextMultiline("##text", &g_script.text[0], g_script.text.capacity() + 1, ImVec2(tb.x - ta.x, tb.y - ta.y),
                            ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput, TextResize, &g_script.text);
  ImGui::PopStyleColor(2);
  ImGui::PopFont();
  const bool bOk = g_script.check.empty();
  StudioGlyph(d, bOk ? "done" : "clash", ImVec2(ea.x + P(10.0f), tb.y + P(6.0f)), P(16.0f), bOk ? C(TK_GOOD) : C(TK_BAD));
  T(d, STUDIO_FONT_TEXT, kStudioFsS, ea.x + P(32.0f), tb.y + P(6.0f), bOk ? C(TK_INK2) : C(TK_BAD),
    bOk ? std::string(bDirty ? "compiles, with the edit; Apply to keep it" : "compiles as the loader reads it") : g_script.check,
    eb.x - ea.x - P(40.0f));
}

}  // namespace studio_art
