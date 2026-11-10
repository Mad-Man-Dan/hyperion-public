// studio_modpage.cpp -- the mod's own page: mod.json (its id, which stays, its name, version, author,
// what it is in a line, what it holds, its rules and the planet behind its home screen), the mods it
// depends on chosen among the installed ones, how many records it brings of each kind, the files of
// its data folder with what names each, and the cinematics it replaces (docs/mod-data.md, "Where it
// goes" and "Cinematics"). mod.json's changes are edits of the "mod" document, undone as any other.
//
// Commands of its own:
//   studio_cinematic {name, from}       a video (or .srt subtitles) copied in as a cinematic of the
//                                       classic game's name: smk/<name><its end>
//   studio_mod_summary                  the records the mod brings and edits, by table, and its files
//   studio_mod_show {section?}          the page shown, at a section: about, depends, brings, cinematics, files
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../port/data/port_mods.h"
#include "../port/tools/port_control.h"
#include "studio_draw.h"
#include "studio_media.h"

namespace {

using namespace studio_draw;

// The classic game's cinematics, by the names a mod's files take in smk/, in groups.
struct Cine {
  const char *pszName;
  const char *pszWords;
};
const Cine kCinematics[] = {
    {"blizzard", "Blizzard's logo"},     {"starintr", "StarCraft's opening"}, {"starXintr", "Brood War's opening"},
    {"PreTerran_1", "Terran, before 1"}, {"PreTerran_2", "Terran, before 2"}, {"PreTerran_3", "Terran, before 3"},
    {"PreTerran_4", "Terran, before 4"}, {"Terran1", "Terran 1"},             {"Terran2", "Terran 2"},
    {"Terran3", "Terran 3"},             {"Terran4", "Terran 4"},             {"PreZerg_2", "Zerg, before 2"},
    {"PreZerg_3", "Zerg, before 3"},     {"PreZerg_4", "Zerg, before 4"},     {"Zerg1", "Zerg 1"},
    {"Zerg2", "Zerg 2"},                 {"Zerg3", "Zerg 3"},                 {"Zerg4", "Zerg 4"},
    {"PreProtoss_1", "Protoss, before 1"}, {"PreProtoss_2", "Protoss, before 2"}, {"PreProtoss_3", "Protoss, before 3"},
    {"Protoss1", "Protoss 1"},           {"Protoss2", "Protoss 2"},           {"Protoss3", "Protoss 3"},
    {"Protoss4", "Protoss 4"},           {"xProtoss", "Brood War, Protoss"},  {"xTerran", "Brood War, Terran"},
    {"xZerg", "Brood War, Zerg"},
};

// The planets a mod's home screen may show (port_world_table.h).
const char *const kWorlds[][2] = {
    {"char", "Char"},         {"korhal", "Korhal IV"},          {"aiur", "Aiur"},
    {"aiur:fallen", "Aiur, fallen"}, {"shakuras", "Shakuras"},  {"braxis", "Braxis"},
    {"moria", "Moria"},       {"marsara", "Mar Sara"},          {"marsara:glassed", "Mar Sara, glassed"},
    {"chausara", "Chau Sara"}, {"antiga", "Antiga Prime"},      {"antiga:glassed", "Antiga Prime, glassed"},
    {"tarsonis", "Tarsonis"}, {"tarsonis:fallen", "Tarsonis, fallen"},
};
// What a mod may say it holds, as the Mods screen shows it.
const char *const kHolds[] = {"units", "maps", "campaign", "looks", "sounds", "tools"};

std::string Back(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}

void Put(const std::string &key, const std::string &valueJson) {
  StudioAsk("studio_put", "\"doc\":\"mod\",\"path\":[" + Q(key) + "]" + (valueJson.empty() ? std::string() : ",\"value\":" + valueJson));
}

// A line of text mod.json keeps, typed and set when the field is left.
void TextField(const char *pszLabel, const char *pszKey, const char *pszHint, float fW, bool bMany = false) {
  static std::string s_editing;
  static char s_sz[1024];
  const PortJson &mod = StudioDoc("mod");
  const std::string now = mod[pszKey].Str();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(p.x + P(130.0f), p.y));
  ImGui::PushID(pszKey);
  if (s_editing != pszKey)
    snprintf(s_sz, sizeof s_sz, "%s", now.c_str());
  bool bActive;
  if (bMany) {
    StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, C(TK_WELL));
    ImGui::InputTextMultiline("##t", s_sz, sizeof s_sz, ImVec2(fW - P(130.0f), P(64.0f)), ImGuiInputTextFlags_WordWrap);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    bActive = ImGui::IsItemActive();
  } else {
    StudioInput("##t", s_sz, sizeof s_sz, (fW - P(130.0f)) / StudioPx(1.0f), 26.0f, pszHint);
    bActive = ImGui::IsItemActive();
  }
  if (bActive)
    s_editing = pszKey;
  if (s_editing == pszKey && !bActive) {
    if (std::string(s_sz) != now)
      Put(pszKey, s_sz[0] ? Q(s_sz) : std::string());
    s_editing.clear();
  }
  ImGui::PopID();
  ImGui::Dummy(ImVec2(1, P(4.0f)));
}

// A version as its parts, compared part by part as the loader compares them ("1.10" after "1.9").
int VersionCmp(const std::string &a, const std::string &b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    long x = 0, y = 0;
    while (i < a.size() && a[i] != '.')
      x = x * 10 + (isdigit(static_cast<unsigned char>(a[i])) ? a[i] - '0' : 0), ++i;
    while (j < b.size() && b[j] != '.')
      y = y * 10 + (isdigit(static_cast<unsigned char>(b[j])) ? b[j] - '0' : 0), ++j;
    if (x != y)
      return x < y ? -1 : 1;
    ++i;
    ++j;
  }
  return 0;
}

const PortMod *Installed(const std::string &id) {
  for (const PortMod &m : PortMods())
    if (m.id == id && !m.bBuiltIn)
      return &m;
  return nullptr;
}

// The depends list as the file has it: each an id, or {id, version}.
struct Dep {
  std::string id, version;
};
std::vector<Dep> Depends(void) {
  std::vector<Dep> out;
  for (const PortJson &d : StudioDoc("mod")["depends"].items)
    out.push_back({d.kind == PortJson::STRING ? d.s : d["id"].Str(), d.kind == PortJson::STRING ? std::string() : d["version"].Str()});
  return out;
}

void PutDepends(const std::vector<Dep> &deps) {
  if (deps.empty()) {
    Put("depends", std::string());
    return;
  }
  std::string arr = "[";
  for (size_t i = 0; i < deps.size(); ++i)
    arr += (i ? "," : "") + std::string("{\"id\":") + Q(deps[i].id) + (deps[i].version.empty() ? std::string() : ",\"version\":" + Q(deps[i].version)) + "}";
  Put("depends", arr + "]");
}

// What names a file of the data folder: the documents' text, read again when the mod changes.
const std::string &NamedText(void) {
  static std::string s_text;
  static unsigned s_nChange = ~0u;
  if (s_nChange != StudioChange()) {
    s_nChange = StudioChange();
    s_text = Lower(PortJsonWrite(StudioDoc("types")) + PortJsonWrite(StudioDoc("hud")) + PortJsonWrite(StudioDoc("looks")));
    std::replace(s_text.begin(), s_text.end(), '\\', '/');
  }
  return s_text;
}

// Whether the mod names a file: by its path, without its end (a portrait's videos), or without
// its number and end; a cinematic and the documents themselves are named by being there.
std::string NamedBy(const std::string &rel) {
  const std::string l = Lower(rel);
  static const char *const kDocs[] = {"types.json", "iscript.txt", "looks.json", "hud.json"};
  for (const char *psz : kDocs)
    if (l == psz)
      return "the mod's document";
  if (l.compare(0, 4, "smk/") == 0)
    return "a cinematic";
  const std::string &text = NamedText();
  if (text.find("\"" + l + "\"") != std::string::npos)
    return "named";
  const size_t dot = l.find_last_of('.');
  std::string stem = dot == std::string::npos ? l : l.substr(0, dot);
  if (text.find("\"" + stem + "\"") != std::string::npos)
    return "named";
  while (!stem.empty() && isdigit(static_cast<unsigned char>(stem.back())))
    stem.pop_back();
  if (text.find("\"" + stem + "\"") != std::string::npos)
    return "named";
  // a sheet's levels and parts name their files inside an object
  if (text.find(l) != std::string::npos)
    return "named";
  return std::string();
}

std::string Size(long long n) {
  if (n < 0)
    return "";
  if (n < 1024)
    return std::to_string(n) + " B";
  if (n < 1024 * 1024)
    return Fmt(n / 1024.0) + " KB";
  return Fmt(n / (1024.0 * 1024.0)) + " MB";
}

std::string g_scrollTo;  // a section to bring into view, once

// A section's frame: a heading in wide capitals over a pane.
void Section(const char *pszTitle, float fW, const char *pszId) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  if (g_scrollTo == pszId) {
    ImGui::SetScrollHereY(0.0f);
    g_scrollTo.clear();
  }
  Wide(d, 10.5f, o.x, o.y, C(TK_HEAD), pszTitle, 0.1f);
  d->AddLine(ImVec2(o.x, o.y + P(20.0f)), ImVec2(o.x + fW, o.y + P(20.0f)), C(TK_LINE));
  ImGui::Dummy(ImVec2(fW, P(30.0f)));
}

void Header(float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson &mod = StudioDoc("mod");
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 28.0f, o.x, o.y, C(TK_HI), mod["name"].Str(StudioModId().c_str()), W - P(420.0f));
  float cx = o.x;
  const float cy = o.y + P(42.0f);
  cx += StudioChip(d, ImVec2(cx, cy), ("id " + StudioModId()).c_str(), CH_PLAIN) + P(6.0f);
  cx += StudioChip(d, ImVec2(cx, cy), ("version " + mod["version"].Str("0")).c_str(), CH_PLAIN) + P(6.0f);
  if (mod["rules"].Str("standard") == "changed")
    cx += StudioChip(d, ImVec2(cx, cy), "changes the rules", CH_GOLD) + P(6.0f);
  const int nUnsaved = StudioUnsaved();
  StudioChip(d, ImVec2(cx, cy), nUnsaved ? (std::to_string(nUnsaved) + (nUnsaved == 1 ? " change not saved" : " changes not saved")).c_str() : "saved",
             nUnsaved ? CH_CHANGED : CH_GOOD);
  const float b1 = StudioButtonSize("Save", "save", 30.0f, 13.0f).x, b2 = StudioButtonSize("Check it", "check", 30.0f, 13.0f).x,
              b3 = StudioButtonSize("Show the folder", "dock", 30.0f, 13.0f).x;
  ImGui::SetCursorScreenPos(ImVec2(o.x + W - b1 - b2 - b3 - P(16.0f), o.y));
  if (StudioButton("##save", "Save", "save", SB_GOOD, 30.0f, 0.0f, 13.0f, nUnsaved > 0))
    StudioAsk("studio_save", "");
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##check", "Check it", "check", SB_PLAIN, 30.0f, 0.0f, 13.0f))
    StudioWorkspaceSet(7);
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##folder", "Show the folder", "dock", SB_PLAIN, 30.0f, 0.0f, 13.0f))
    ShellExecuteA(nullptr, "open", StudioModFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(W, P(80.0f)));
}

void About(float W) {
  Section("About it", W, "about");
  const PortJson &mod = StudioDoc("mod");
  const float fw = std::min(W, P(720.0f));
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), "Id");
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_MONO, kStudioFsS, p.x + P(130.0f), p.y + P(5.0f), C(TK_INK), StudioModId());
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(140.0f) + TW(STUDIO_FONT_MONO, kStudioFsS, StudioModId()),
      p.y + P(6.0f), C(TK_INK3), "replays, saves and the mods that build on it name it by this, so it stays");
    ImGui::Dummy(ImVec2(1, P(30.0f)));
  }
  TextField("Name", "name", "as the Mods screen shows it", fw);
  TextField("Version", "version", "0.1", std::min(fw, P(330.0f)));
  TextField("By", "by", "who made it", fw);
  TextField("What it is", "line", "a sentence of what it brings", fw, true);
  // what it holds, as chips that turn on and off
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), "Holds");
    std::set<std::string> holds;
    for (const PortJson &h : mod["holds"].items)
      holds.insert(h.Str());
    std::set<std::string> all(holds);
    all.insert(std::begin(kHolds), std::end(kHolds));
    float x = p.x + P(130.0f);
    std::string toggled;
    for (const char *psz : kHolds) {
      ImGui::SetCursorScreenPos(ImVec2(x, p.y));
      const bool bOn = holds.count(psz) > 0;
      if (StudioButton((std::string("##h") + psz).c_str(), psz, bOn ? "done" : nullptr, bOn ? SB_ON : SB_PLAIN, 26.0f, 0.0f, kStudioFsXs))
        toggled = psz;
      x += StudioButtonSize(psz, bOn ? "done" : nullptr, 26.0f, kStudioFsXs).x + P(6.0f);
    }
    if (!toggled.empty()) {
      if (holds.count(toggled))
        holds.erase(toggled);
      else
        holds.insert(toggled);
      std::string arr = "[";
      for (const PortJson &h : mod["holds"].items)  // the file's order kept, the new one last
        if (holds.count(h.Str()))
          arr += (arr.size() > 1 ? "," : "") + Q(h.Str());
      bool bThere = false;
      for (const PortJson &h : mod["holds"].items)
        bThere = bThere || h.Str() == toggled;
      if (!bThere && holds.count(toggled))
        arr += (arr.size() > 1 ? "," : "") + Q(toggled);
      Put("holds", arr + "]");
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(1, P(34.0f)));
  }
  // the rules: standard or changed
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), "Rules");
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(130.0f), p.y));
    static const char *const kRules[] = {"The classic rules", "Changed"};
    const int n = mod["rules"].Str("standard") == "changed" ? 1 : 0;
    const int m = StudioSeg("##rules", kRules, 2, n);
    if (m != n)
      Put("rules", m ? "\"changed\"" : "\"standard\"");
    ImGui::SameLine(0.0f, P(10.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "what the Mods screen says of how its games play");
    ImGui::Dummy(ImVec2(1, P(4.0f)));
  }
  // the planet behind its home screen
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), "World");
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(130.0f), p.y));
    const std::string now = mod["world"].Str("char");
    std::string shown = now;
    for (const auto &w : kWorlds)
      if (now == w[0])
        shown = w[1];
    if (StudioSelectBegin("##world", shown.c_str(), 240.0f)) {
      for (const auto &w : kWorlds)
        if (StudioItem(w[1], now == w[0]))
          Put("world", Q(w[0]));
      StudioSelectEnd();
    }
    ImGui::SameLine(0.0f, P(10.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "the planet behind its home screen");
    ImGui::Dummy(ImVec2(1, P(4.0f)));
  }
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), "Data folder");
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_MONO, kStudioFsXs, p.x + P(130.0f), p.y + P(6.0f), C(TK_INK), mod["data"].Str("(none)"));
    ImGui::Dummy(ImVec2(1, P(30.0f)));
  }
  ImGui::Dummy(ImVec2(1, P(14.0f)));
}

void DependsOn(float W) {
  Section("Builds on", W, "depends");
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioMediaPara("The mods whose data loads before its own, each the least version it needs. Its records name theirs and may edit them; "
                  "a dependency that is not installed, is older than the version named or leads back to this mod stops the load.",
                  W, C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  std::vector<Dep> deps = Depends();
  bool bChanged = false;
  int nOut = -1;
  for (size_t i = 0; i < deps.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = P(34.0f);
    d->AddRectFilled(p, ImVec2(p.x + W, p.y + h), C(TK_PANE2), P(3.0f));
    const PortMod *pm = Installed(deps[i].id);
    T(d, STUDIO_FONT_BOLD, 14.0f, p.x + P(12.0f), Mid(p.y, h, 14.0f), C(TK_INK), pm ? pm->name : deps[i].id);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x + P(220.0f), Mid(p.y, h, kStudioFsXs), C(TK_INK3), deps[i].id);
    std::string state;
    int nKind = CH_GOOD;
    if (pm == nullptr) {
      state = "not installed";
      nKind = CH_BAD;
    } else if (pm->data.empty()) {
      state = pm->version + ": brings no data";
      nKind = CH_WARN;
    } else if (!deps[i].version.empty() && VersionCmp(pm->version, deps[i].version) < 0) {
      state = pm->version + " installed, older";
      nKind = CH_BAD;
    } else {
      state = pm->version + " installed";
    }
    StudioChip(d, ImVec2(p.x + P(380.0f), p.y + P(8.0f)), state.c_str(), nKind);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + W - P(330.0f), Mid(p.y, h, kStudioFsXs), C(TK_INK3), "at least");
    ImGui::SetCursorScreenPos(ImVec2(p.x + W - P(270.0f), p.y + P(4.0f)));
    static char s_sz[8][24];
    static std::string s_editing;
    const std::string key = deps[i].id;
    char *psz = s_sz[i % 8];
    if (s_editing != key)
      snprintf(psz, 24, "%s", deps[i].version.c_str());
    StudioInput("##ver", psz, 24, 110.0f, 26.0f, "any version");
    if (ImGui::IsItemActive())
      s_editing = key;
    else if (s_editing == key) {
      s_editing.clear();
      if (deps[i].version != psz) {
        deps[i].version = psz;
        bChanged = true;
      }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + W - P(140.0f), p.y + P(4.0f)));
    if (pm && pm->version != deps[i].version &&
        StudioButton("##now", "its version", nullptr, SB_FLAT, 26.0f, 0.0f, kStudioFsXs)) {
      deps[i].version = pm->version;
      bChanged = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + W - P(30.0f), p.y + P(4.0f)));
    if (StudioButton("##x", "", "x", SB_FLAT, 26.0f, 24.0f))
      nOut = static_cast<int>(i);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Build on it no more");
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(W, h + P(6.0f)));
    ImGui::PopID();
  }
  if (deps.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "Nothing: it builds on the classic game alone.");
  if (nOut >= 0) {
    deps.erase(deps.begin() + nOut);
    bChanged = true;
  }
  // more, from the installed mods that bring data
  if (StudioSelectBegin("##adddep", "+ a mod it builds on", 230.0f, 26.0f)) {
    bool bAny = false;
    for (const PortMod &m : PortMods()) {
      if (m.bBuiltIn || m.data.empty() || m.id == StudioModId())
        continue;
      bool bHas = false;
      for (const Dep &dp : deps)
        bHas = bHas || dp.id == m.id;
      if (bHas)
        continue;
      bAny = true;
      if (StudioItem((m.name + "  " + m.version).c_str())) {
        deps.push_back({m.id, m.version});
        bChanged = true;
      }
    }
    if (!bAny)
      StudioItem("no other installed mod brings data", false, nullptr, false);
    StudioSelectEnd();
  }
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##rescan", "Look for mods again", "again", SB_FLAT, 26.0f, 0.0f, kStudioFsXs))
    PortModsRescan();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Reads the mods folder again, for a mod installed since the studio started");
  if (bChanged)
    PutDepends(deps);
  // what the open model loaded: changed dependencies load when the mod is opened again
  std::string loaded;
  for (int i = 0; i < StudioDependencies(); ++i)
    loaded += (i ? ", " : "") + StudioDependencyId(i);
  std::string want;
  for (const Dep &dp : Depends())
    want += (want.empty() ? "" : ", ") + dp.id;
  if (loaded != want) {
    ImGui::Dummy(ImVec2(1, P(4.0f)));
    StudioMediaPara("The studio has " + (loaded.empty() ? std::string("no mod") : loaded) +
                        " loaded under this one; save, and open the mod again, to build on what mod.json now names.",
                    W, C(TK_WARN), kStudioFsXs);
  }
  ImGui::Dummy(ImVec2(1, P(18.0f)));
}

void Summary(float W) {
  Section("What it brings", W, "brings");
  ImDrawList *d = ImGui::GetWindowDrawList();
  static const char *const kWords[] = {"Units", "Weapons", "Movement", "Sprites", "Images", "Orders", "Research", "Upgrades", "Sounds",
                                       "Portraits"};
  const int nCols = W > P(900.0f) ? 5 : W > P(560.0f) ? 4 : 2;
  const float cw = (W - P(8.0f) * (nCols - 1)) / nCols, ch = P(62.0f);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  for (int t = 0; t < ST_TABLES; ++t) {
    int nOwn = 0, nEdit = 0;
    for (const StudioRecord &r : StudioRecords(t))
      nOwn += r.kind == SK_OWN, nEdit += r.kind == SK_EDIT;
    const ImVec2 a(o.x + (t % nCols) * (cw + P(8.0f)), o.y + (t / nCols) * (ch + P(8.0f)));
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(t);
    if (ImGui::InvisibleButton("##t", ImVec2(cw, ch))) {
      StudioTablesShow(t);
      StudioWorkspaceSet(1);
    }
    const bool bHover = ImGui::IsItemHovered();
    ImGui::PopID();
    d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), bHover ? C(TK_ROW_HI) : C(TK_PANE2), P(3.0f));
    d->AddRect(a, ImVec2(a.x + cw, a.y + ch), C(TK_LINE), P(3.0f));
    Wide(d, 9.0f, a.x + P(12.0f), a.y + P(10.0f), C(TK_INK3), kWords[t], 0.1f);
    const float x = a.x + P(12.0f) + T(d, STUDIO_FONT_BOLD, 22.0f, a.x + P(12.0f), a.y + P(26.0f), nOwn ? C(TK_HI) : C(TK_INK3), std::to_string(nOwn));
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x + P(8.0f), a.y + P(34.0f), C(TK_INK2), nEdit ? "new, " + std::to_string(nEdit) + " edited" : "new");
  }
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(W, ((ST_TABLES + nCols - 1) / nCols) * (ch + P(8.0f))));
  const PortJson &looks = StudioDoc("looks")["looks"];
  const PortJson &hud = StudioDoc("hud");
  std::string more = std::to_string(looks.size()) + (looks.size() == 1 ? " look" : " looks") + " in looks.json; hud.json " +
                     (hud.kind == PortJson::OBJECT && !hud.members.empty() ? "changes the console" : "changes nothing") + "; " +
                     std::to_string(StudioDoc("types")["races"].size()) + " races, " + std::to_string(StudioDoc("types")["cards"].size()) +
                     " command cards.";
  StudioMediaPara(more, W, C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(18.0f)));
}

std::string g_fileFilter;
char g_szFiles[64];

void Files(float W) {
  Section("Its files", W, "files");
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioMediaPara("The data folder, " + StudioDataFolder() + ". A file nothing in the mod names is marked: the loader never reads it.", W,
                  C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  StudioInput("##ff", g_szFiles, sizeof g_szFiles, 240.0f, 26.0f, "Find a file", "search");
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##bring", "Bring in a file", "plus", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs))
    StudioPickFile("modpage:file", "Files", "wav,ogg,opus,mp3,flac,png,grp,pcx,webp,tga,webm,mkv,mp4,m4v,mov,smk,srt");
  std::string path;
  if (StudioPickedFile("modpage:file", &path)) {
    const std::string l = Lower(path);
    const char *pszFolder = "art";
    for (const char *psz : {".wav", ".ogg", ".opus", ".mp3", ".flac"})
      if (l.size() > strlen(psz) && l.compare(l.size() - strlen(psz), strlen(psz), psz) == 0)
        pszFolder = "sounds";
    for (const char *psz : {".webm", ".mkv", ".mp4", ".m4v", ".mov", ".smk", ".srt"})
      if (l.size() > strlen(psz) && l.compare(l.size() - strlen(psz), strlen(psz), psz) == 0)
        pszFolder = "portraits";
    StudioAsk("studio_import", "\"from\":" + Q(path) + ",\"folder\":" + Q(pszFolder));
  }
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  // the folder's own files first, then each folder's
  std::vector<std::string> files = StudioFiles();
  std::stable_sort(files.begin(), files.end(), [](const std::string &a, const std::string &b) {
    return (a.find('/') == std::string::npos) > (b.find('/') == std::string::npos);
  });
  int nShown = 0, nUnnamed = 0;
  long long nBytes = 0;
  std::string folder = "\x01";
  for (const std::string &f : files) {
    const long long n = StudioDataFileSize(f);
    nBytes += n > 0 ? n : 0;
    const std::string by = NamedBy(f);
    nUnnamed += by.empty();
    if (g_szFiles[0] && !Has(f, g_szFiles))
      continue;
    if (++nShown > 400)
      continue;
    const size_t cut = f.find_last_of('/');
    const std::string here = cut == std::string::npos ? std::string() : f.substr(0, cut + 1);
    if (here != folder) {
      folder = here;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x, p.y + P(6.0f), C(TK_INK3), here.empty() ? std::string("the folder itself") : here);
      ImGui::Dummy(ImVec2(1, P(24.0f)));
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = P(24.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(14.0f), Mid(p.y, h, kStudioFsS), C(TK_INK), cut == std::string::npos ? f : f.substr(cut + 1),
      W - P(360.0f));
    T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x + W - P(330.0f), Mid(p.y, h, kStudioFsXs), C(TK_INK3), Size(n));
    if (by.empty())
      StudioChip(d, ImVec2(p.x + W - P(240.0f), p.y + P(3.0f)), "nothing names it", CH_WARN);
    else if (by != "named")
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + W - P(240.0f), Mid(p.y, h, kStudioFsXs), C(TK_INK3), by);
    const std::string l = Lower(f);
    bool bSound = false;
    for (const char *psz : {".wav", ".ogg", ".opus", ".mp3", ".flac"})
      bSound = bSound || (l.size() > strlen(psz) && l.compare(l.size() - strlen(psz), strlen(psz), psz) == 0);
    if (bSound) {
      ImGui::SetCursorScreenPos(ImVec2(p.x + W - P(30.0f), p.y));
      ImGui::PushID(f.c_str());
      if (StudioButton("##play", "", "play", SB_FLAT, 24.0f, 24.0f)) {
        StudioSoundInfo info;
        std::string why;
        if (!StudioPlayDataFile(f, &info, &why))
          StudioSay(why);
      }
      ImGui::PopID();
    }
    d->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + W, p.y + h - 0.5f), C(TK_ROW));
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(W, h));
  }
  if (nShown > 400)
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), ("and " + std::to_string(nShown - 400) + " more: find narrows them").c_str());
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3),
              (std::to_string(files.size()) + " files, " + Size(nBytes) + (nUnnamed ? "; " + std::to_string(nUnnamed) + " nothing names" : std::string()))
                  .c_str());
  ImGui::Dummy(ImVec2(1, P(18.0f)));
}

std::string g_cineShown;  // the cinematic being watched, "" for none
StudioVideoState g_cineState;  // what it was as it was last drawn, for studio_watch

// The cinematic being watched, the mod's video or the classic one, at up to 640 wide, and what it is.
void CinePlayer(float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const std::string own = StudioFirstVideo("smk/" + g_cineShown);
  const bool bClassic = own.empty();
  const std::string where = bClassic ? "smk\\" + g_cineShown + ".smk" : own;
  const float bw = std::min(W, P(640.0f)), bh = bw * 3.0f / 4.0f;
  d->AddRectFilled(o, ImVec2(o.x + bw, o.y + bh), C(TK_BLACK), P(4.0f));
  StudioVideoState st;
  const bool bOn = StudioVideoDraw(where, bClassic, o, ImVec2(o.x + bw, o.y + bh), &st, true);
  g_cineState = st;
  if (!bOn)
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + P(20.0f), o.y + P(20.0f), st.bReading ? C(TK_INK2) : C(TK_WARN), st.why, bw - P(40.0f));
  d->AddRect(o, ImVec2(o.x + bw, o.y + bh), C(TK_LINE2), P(4.0f));
  // its subtitles under it, a line each, as the game puts them over it
  std::string subsFrom;
  const std::string said = bOn ? StudioSubtitleAt(g_cineShown, !bClassic, st.dAt, &subsFrom) : std::string();
  float sy = o.y + bh + P(8.0f);
  for (size_t at = 0; at <= said.size() && !said.empty();) {
    const size_t nl = said.find('\n', at);
    const std::string line = said.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + (bw - TW(STUDIO_FONT_TEXT, kStudioFsS, line.c_str())) / 2.0f, sy, C(TK_INK), line, bw);
    sy += P(20.0f);
    if (nl == std::string::npos)
      break;
    at = nl + 1;
  }
  const float x = o.x + bw + P(18.0f), tw = W - bw - P(18.0f);
  float y = o.y;
  if (tw > P(160.0f)) {
    T(d, STUDIO_FONT_BOLD, 15.0f, x, y, C(TK_INK), g_cineShown);
    y += P(26.0f);
    if (bOn) {
      T(d, STUDIO_FONT_MONO, kStudioFsXs, x, y, C(TK_INK2), st.from, tw);
      y += P(22.0f);
      char sz[160];
      snprintf(sz, sizeof sz, "%d by %d, frame %u of %u, %.0f seconds", st.w, st.h, st.nFrame + 1, st.nFrames, st.dSeconds);
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y, C(TK_INK3), sz, tw);
      y += P(22.0f);
      const std::string sound = !st.bHasSound ? "It has no sound." : StudioVideoMuted() ? "Its sound is muted." : "Its sound plays with it.";
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y, C(TK_INK3),
        (subsFrom.empty() ? std::string("No subtitles. ") : "Subtitles from " + subsFrom + ". ") + sound +
            " Plays here in a loop; the game plays it whole.",
        tw);
      y += P(40.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (StudioButton("##cinestop", "Stop", "pause", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs))
      g_cineShown.clear();
    if (st.bHasSound) {
      ImGui::SameLine(0.0f, P(8.0f));
      if (StudioButton("##cinemute", StudioVideoMuted() ? "Sound on" : "Mute", "sound", StudioVideoMuted() ? SB_ON : SB_FLAT, 26.0f, 0.0f,
                       kStudioFsXs))
        StudioVideoMute(!StudioVideoMuted());
    }
  }
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(1, bh + P(52.0f)));
}

void Cinematics(float W) {
  Section("Cinematics", W, "cinematics");
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioMediaPara("A video at a classic cinematic's name in smk/ plays in its place: WebM, Matroska, MP4, QuickTime or Smacker, at any "
                  "size; AV1 or VP9 plays everywhere. Subtitles of its own go beside it as .srt, in UTF-8.",
                  W, C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  if (!g_cineShown.empty())
    CinePlayer(W);
  for (const Cine &c : kCinematics) {
    ImGui::PushID(c.pszName);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = P(30.0f);
    const std::string base = std::string("smk/") + c.pszName;
    const std::string video = StudioFirstVideo(base);
    const bool bSrt = StudioFileExists(base + ".srt");
    T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x, Mid(p.y, h, kStudioFsS), video.empty() ? C(TK_INK2) : C(TK_HI), c.pszWords);
    T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x + P(180.0f), Mid(p.y, h, kStudioFsXs), C(TK_INK3), c.pszName);
    if (!video.empty())
      StudioChip(d, ImVec2(p.x + P(320.0f), p.y + P(6.0f)), (video.substr(video.find_last_of('.') + 1) + ", " + Size(StudioDataFileSize(video))).c_str(),
                 CH_GOOD);
    else
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(320.0f), Mid(p.y, h, kStudioFsXs), C(TK_INK3), "the classic one");
    if (bSrt)
      StudioChip(d, ImVec2(p.x + P(450.0f), p.y + P(6.0f)), "subtitles", CH_PLAIN);
    const float bw = StudioButtonSize(video.empty() ? "Bring in a video" : "Another video", "plus", 24.0f, kStudioFsXs).x;
    const float sw = StudioButtonSize("Subtitles", "plus", 24.0f, kStudioFsXs).x;
    const float ww = StudioButtonSize("Watch", "play", 24.0f, kStudioFsXs).x;
    const bool bWatching = g_cineShown == c.pszName;
    ImGui::SetCursorScreenPos(ImVec2(p.x + W - bw - sw - ww - P(16.0f), p.y + P(3.0f)));
    if (StudioButton("##w", "Watch", "play", bWatching ? SB_ON : SB_FLAT, 24.0f, 0.0f, kStudioFsXs))
      g_cineShown = bWatching ? std::string() : std::string(c.pszName);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(video.empty() ? "The classic one, from the game's files" : "The mod's video");
    ImGui::SameLine(0.0f, P(8.0f));
    const std::string key = std::string("cine:") + c.pszName;
    if (StudioButton("##v", video.empty() ? "Bring in a video" : "Another video", "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioPickFile(key.c_str(), "Videos", "webm,mkv,mp4,m4v,mov,smk");
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##s", "Subtitles", "plus", SB_FLAT, 24.0f, 0.0f, kStudioFsXs, !video.empty()))
      StudioPickFile((key + "#srt").c_str(), "Subtitles", "srt");
    std::string path;
    if (StudioPickedFile(key.c_str(), &path) || StudioPickedFile((key + "#srt").c_str(), &path))
      StudioAsk("studio_cinematic", "\"name\":" + Q(c.pszName) + ",\"from\":" + Q(path));
    d->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + W, p.y + h - 0.5f), C(TK_ROW));
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(W, h));
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(1, P(18.0f)));
}

// ---- the commands ----------------------------------------------------------------------------------------

bool CmdCinematic(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str(), from = j["from"].Str();
  const Cine *pc = nullptr;
  for (const Cine &c : kCinematics)
    if (Lower(name) == Lower(c.pszName))
      pc = &c;
  if (pc == nullptr) {
    *pAnswer = "no cinematic " + name + ": the classic names are blizzard, starintr, starXintr, PreTerran_1 to _4, Terran1 to 4, "
               "PreZerg_2 to _4, Zerg1 to 4, PreProtoss_1 to _3, Protoss1 to 4, xProtoss, xTerran, xZerg";
    return false;
  }
  const size_t dot = from.find_last_of('.');
  const std::string end = Lower(dot == std::string::npos ? std::string() : from.substr(dot));
  bool bOk = end == ".srt";
  for (const char *psz : kStudioVideoEnds)
    bOk = bOk || end == psz;
  if (!bOk) {
    *pAnswer = "a cinematic is a .webm, .mkv, .mp4, .m4v, .mov or .smk video, or .srt subtitles";
    return false;
  }
  const std::string rel = std::string("smk/") + pc->pszName + end;
  std::string why;
  if (!StudioCopyAs(from, rel, &why)) {
    *pAnswer = why;
    return false;
  }
  // a video of another end at the same name is tried before it, or passed over: say which plays
  std::string plays;
  if (end != ".srt")
    plays = StudioFirstVideo(std::string("smk/") + pc->pszName);
  *pAnswer = "\"file\":" + Q(rel) + (plays.empty() || plays == rel ? std::string() : ",\"plays\":" + Q(plays));
  if (!plays.empty() && plays != rel)
    StudioLog("warn", plays + " is there too, and the game plays it before " + rel);
  return true;
}

bool CmdSummary(const PortJson &, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string tables;
  for (int t = 0; t < ST_TABLES; ++t) {
    int nOwn = 0, nEdit = 0;
    for (const StudioRecord &r : StudioRecords(t))
      nOwn += r.kind == SK_OWN, nEdit += r.kind == SK_EDIT;
    tables += std::string(t ? "," : "") + Q(StudioTableName(t)) + ":{\"new\":" + std::to_string(nOwn) + ",\"edited\":" + std::to_string(nEdit) + "}";
  }
  std::string unnamed, cines;
  const std::vector<std::string> files = StudioFiles();
  for (const std::string &f : files)
    if (NamedBy(f).empty())
      unnamed += (unnamed.empty() ? "" : ",") + Q(f);
  for (const Cine &c : kCinematics) {
    const std::string v = StudioFirstVideo(std::string("smk/") + c.pszName);
    if (!v.empty())
      cines += (cines.empty() ? "" : ",") + Q(v);
  }
  *pAnswer = "\"mod\":" + PortJsonWrite(StudioDoc("mod")) + ",\"tables\":{" + tables + "},\"files\":" + std::to_string(files.size()) +
             ",\"unnamed\":[" + unnamed + "],\"cinematics\":[" + cines + "],\"looks\":" + std::to_string(StudioDoc("looks")["looks"].size());
  return true;
}

// studio_watch {name?, stop?, mute?}: a cinematic watched on the mod's page, stopped, or its sound
// muted; the answer is the player as it was last drawn, with when frame 0 and its sound began.
bool CmdWatch(const PortJson &j, std::string *pAnswer) {
  if (j["mute"].kind == PortJson::BOOL)
    StudioVideoMute(j["mute"].b);
  if (j["stop"].kind == PortJson::BOOL && j["stop"].b) {
    g_cineShown.clear();
  } else if (!j["name"].Str().empty()) {
    const Cine *pc = nullptr;
    for (const Cine &c : kCinematics)
      if (Lower(j["name"].Str()) == Lower(c.pszName))
        pc = &c;
    if (pc == nullptr) {
      *pAnswer = "no cinematic " + j["name"].Str();
      return false;
    }
    if (g_cineShown != pc->pszName)
      g_cineState = StudioVideoState();
    g_cineShown = pc->pszName;
    g_scrollTo = "cinematics";
    StudioWorkspaceSet(8);
  }
  const StudioVideoState &s = g_cineState;
  *pAnswer = "\"watching\":" + Q(g_cineShown) + ",\"from\":" + Q(s.from) + ",\"why\":" + Q(s.why) +
             ",\"reading\":" + (s.bReading ? "true" : "false") + ",\"frame\":" + std::to_string(s.nFrame) +
             ",\"frames\":" + std::to_string(s.nFrames) + ",\"sound\":{\"has\":" + (s.bHasSound ? "true" : "false") +
             ",\"on\":" + (s.bSoundOn ? "true" : "false") + ",\"muted\":" + (StudioVideoMuted() ? "true" : "false") +
             ",\"frame0At\":" + Fmt(s.dFrame0At, 3) + ",\"soundAt\":" + Fmt(s.dSoundAt, 3) + "}";
  return true;
}

bool CmdShow(const PortJson &j, std::string *pAnswer) {
  g_scrollTo = j["section"].Str();
  StudioWorkspaceSet(8);
  *pAnswer = "\"section\":" + Q(g_scrollTo);
  return true;
}

}  // namespace

void StudioModWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, a.x + P(20.0f), a.y + P(20.0f), C(TK_INK3), "Open a mod to see its page.");
    return;
  }
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + 1));
  if (ImGui::BeginChild("##modpage", ImVec2(b.x - a.x - 2, b.y - a.y - 2), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(o.x + P(28.0f), o.y + P(20.0f)));
    ImGui::BeginGroup();
    const float W = std::min(ImGui::GetContentRegionAvail().x - P(28.0f), P(1100.0f));
    Header(W);
    About(W);
    DependsOn(W);
    Summary(W);
    Cinematics(W);
    Files(W);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, P(24.0f)));
  }
  ImGui::EndChild();
}

void StudioModPageInit(void) {
  PortControlCommand("studio_cinematic", CmdCinematic);
  PortControlCommand("studio_mod_summary", CmdSummary);
  PortControlCommand("studio_mod_show", CmdShow);
  PortControlCommand("studio_watch", CmdWatch);
}
