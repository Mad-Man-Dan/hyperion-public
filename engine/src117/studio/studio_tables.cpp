// studio_tables.cpp -- the Tables workspace (the design's mock, cs-unit.js mountTables): every record of
// a table (units, weapons, upgrades, techs and the rest), the mod's and classic, as one table; column sets by category, sort by any column, a
// filter by name, race or base, "showing 3 of 231". The mod's rows are edited in place, a cell at a
// time; a classic row offers Edit in the mod; selected rows take one value for a column at once.
// Worked-out columns (life in all, damage a second...) are read only. Every change goes through
// studio_set, as the documents' do.
#include <algorithm>
#include <set>
#include <vector>
#include "imgui_internal.h"
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// ---- the columns ------------------------------------------------------------------------------------

// A column: a field by its key, or a worked-out value by its "#" key.
struct ColDef {
  const char *pszKey;
  const char *pszLabel;  // a worked-out column's; a field's label comes from fields.json
};
struct ColSet {
  const char *pszLabel;
  std::vector<ColDef> cols;  // empty: every field
};
const std::vector<ColSet> &Sets(int t) {
  static const std::vector<ColSet> kUnits = {
      {"Life and armour", {{"hit_points"}, {"shields"}, {"armor"}, {"size"}, {"armor_upgrade"}, {"#life", "Life in all"}}},
      {"Cost and supply", {{"minerals"}, {"gas"}, {"build_time"}, {"supply_required"}, {"supply_provided"}, {"space_required"}, {"#worth", "Worth"}}},
      {"Weapons and sight", {{"ground_weapon"}, {"air_weapon"}, {"#dps", "A second"}, {"#range", "Range, tiles"}, {"max_ground_hits"}, {"sight_range"}, {"target_range"}}},
      {"Movement", {{"flingy"}, {"#pxs", "px a second"}, {"elevation"}, {"right_click"}, {"direction"}}},
      {"Everything", {}}};
  static const std::vector<ColSet> kWeapons = {
      {"Damage", {{"damage"}, {"damage_bonus"}, {"damage_factor"}, {"damage_type"}, {"explosion"}, {"#wdps", "A second"}}},
      {"Range and splash", {{"max_range"}, {"#wtiles", "Range, tiles"}, {"min_range"}, {"inner_splash"}, {"middle_splash"}, {"outer_splash"}}},
      {"Timing", {{"cooldown"}, {"attack_angle"}, {"remove_after"}}},
      {"Everything", {}}};
  if (t == ST_UNITS)
    return kUnits;
  if (t == ST_WEAPONS)
    return kWeapons;
  // the other tables: a set for each of fields.json's categories that has a column, then all
  static std::vector<ColSet> s_sets[ST_TABLES];
  std::vector<ColSet> &sets = s_sets[t];
  if (sets.empty()) {
    for (const StudioCategory &c : StudioCategories(t)) {
      ColSet set{c.label.c_str(), {}};
      for (const StudioField &f : StudioFields(t))
        if (f.cat == c.id && f.nColumn >= 0 && f.nCount == 1)
          set.cols.push_back({f.key.c_str(), nullptr});
      if (!set.cols.empty())
        sets.push_back(set);
    }
    sets.push_back({"Everything", {}});
  }
  return sets;
}

struct Col {
  std::string key;
  std::string label;
  const StudioField *pf = nullptr;  // null for a worked-out column
};

bool Inline(const StudioField &f) {
  return f.nColumn >= 0 && f.nCount == 1 && !f.bPath && f.editor.compare(0, 6, "flags:") != 0 && f.editor != "readonly" &&
         f.editor != "req" && f.editor != "sound";
}

std::vector<Col> Columns(int t, int nSet) {
  std::vector<Col> out;
  const ColSet &set = Sets(t)[static_cast<size_t>(nSet)];
  auto field = [&](const StudioField &f) {
    std::string label = f.label;
    if (f.editor == "frames")
      label += " (s)";
    out.push_back({f.key, label, &f});
  };
  if (set.cols.empty()) {
    for (const StudioField &f : StudioFields(t))
      if (f.nColumn >= 0 && f.nCount == 1 && f.key != "name" && f.key != "label")
        field(f);
    return out;
  }
  for (const ColDef &c : set.cols) {
    if (c.pszKey[0] == '#')
      out.push_back({c.pszKey, c.pszLabel, nullptr});
    else if (const StudioField *pf = StudioFieldOf(t, c.pszKey))
      field(*pf);
  }
  return out;
}

// ---- the rows -------------------------------------------------------------------------------------

struct Row {
  std::string name;
  StudioKind kind = SK_CLASSIC;
  std::string base;
  std::string show;
  std::string race;
};

bool Editable(const Row &r) {
  return r.kind == SK_OWN || r.kind == SK_EDIT;
}

std::string RaceOf(const std::string &unit) {
  const long long g = StudioNumber(ST_UNITS, unit, "group_flags");
  return (g & 1) ? "Zerg" : (g & 2) ? "Terran" : (g & 4) ? "Protoss" : "Independent";
}

// The mod's rows, then the classic rows its edits leave, gathered again when the mod changes.
const std::vector<Row> &AllRows(int t) {
  static std::vector<Row> s_rows;
  static int s_t = -1;
  static unsigned s_nChange = ~0u;
  static bool s_bOpen = false;
  if (s_t == t && s_nChange == StudioChange() && s_bOpen == StudioIsOpen())
    return s_rows;
  s_rows.clear();
  s_t = t;
  s_nChange = StudioChange();
  s_bOpen = StudioIsOpen();
  std::set<int> edited;
  for (const StudioRecord &r : StudioRecords(t)) {
    Row row;
    row.name = r.name;
    row.kind = r.kind;
    row.base = r.base;
    if (r.kind == SK_EDIT) {
      const char *psz = StudioClassicName(t, r.nId);
      row.base = psz ? psz : "";
    }
    if (r.kind == SK_EDIT)
      edited.insert(r.nId);
    s_rows.push_back(row);
  }
  for (int n = 0; n < StudioClassicRows(t); ++n) {
    const char *psz = StudioClassicName(t, n);
    if (psz == nullptr || edited.count(n))
      continue;
    Row row;
    row.name = psz;
    s_rows.push_back(row);
  }
  for (Row &r : s_rows) {
    r.show = StudioShowName(t, r.kind == SK_EDIT && !r.base.empty() ? r.base : r.name);
    if (t == ST_UNITS)
      r.race = RaceOf(r.name);
  }
  return s_rows;
}

// ---- the values -----------------------------------------------------------------------------------

std::string EnumWord(const std::string &name, long long n) {
  for (const auto &e : StudioEnum(name))
    if (e.first == n)
      return e.second;
  return std::to_string(n);
}

bool Derived(int t, const std::string &name, const std::string &key, double *pd) {
  if (t == ST_UNITS) {
    const std::string w = !StudioUnitWeapon(name, false).empty() ? StudioUnitWeapon(name, false) : StudioUnitWeapon(name, true);
    if (key == "#life")
      *pd = static_cast<double>(StudioNumber(t, name, "hit_points") +
                                (StudioNumber(t, name, "shields_enabled") ? StudioNumber(t, name, "shields") : 0));
    else if (key == "#worth")
      *pd = StudioNumber(t, name, "minerals") + StudioNumber(t, name, "gas") * 1.5;
    else if (key == "#dps")
      *pd = StudioDamagePerSecond(w);
    else if (key == "#range")
      *pd = w.empty() ? 0.0 : StudioNumber(ST_WEAPONS, w, "max_range") / 32.0;
    else if (key == "#pxs")
      *pd = StudioUnitSpeed(name);
    else
      return false;
    return true;
  }
  if (key == "#wdps")
    *pd = StudioDamagePerSecond(name);
  else if (key == "#wtiles")
    *pd = StudioNumber(t, name, "max_range") / 32.0;
  else
    return false;
  return true;
}

// A cell: its number to sort by, its words, and whether the row gives it itself.
struct Cell {
  double d = 0.0;
  std::string text;
  bool bOwn = false;
  bool bText = false;  // sorted by its words
};

Cell CellOf(int t, const std::string &name, const Col &c) {
  Cell cell;
  if (c.pf == nullptr) {
    if (Derived(t, name, c.key, &cell.d))
      cell.text = Fmt(cell.d);
    return cell;
  }
  StudioValue v;
  if (!StudioGet(t, name, c.key, &v) || v.n.empty())
    return cell;
  const StudioField &f = *c.pf;
  const long long n = v.n[0];
  cell.d = static_cast<double>(n);
  cell.bOwn = v.source == SS_OWN;
  if (f.editor == "frames") {
    cell.d = n / 24.0;
    cell.text = Fmt(cell.d, 2);
  } else if (f.editor == "half") {
    cell.d = n / 2.0;
    cell.text = Fmt(cell.d);
  } else if (f.editor == "bool") {
    cell.text = n ? "yes" : "no";
  } else if (f.editor.compare(0, 5, "enum:") == 0) {
    cell.text = EnumWord(f.editor.substr(5), n);
    cell.bText = true;
  } else if (f.nRef >= 0) {
    cell.text = n == StudioClassicRows(f.nRef) ? std::string("none") : Short(f.nRef, StudioRefName(f.nRef, n));
    cell.bText = true;
  } else {
    cell.text = std::to_string(n);
  }
  return cell;
}

// What a typed value is in the record: seconds to frames, supply to halves, numbers whole.
bool Typed(const StudioField &f, const char *psz, std::string *pJson) {
  char *pEnd = nullptr;
  const double d = strtod(psz, &pEnd);
  if (pEnd == psz)
    return false;
  const double v = f.editor == "frames" ? d * 24.0 : f.editor == "half" ? d * 2.0 : d;
  *pJson = Fmt(floor(v + 0.5));
  return true;
}

// ---- the state ------------------------------------------------------------------------------------

int g_nKind;            // the table shown (ST_UNITS, ST_WEAPONS...)
int g_anSet[ST_TABLES]; // the column set each shows
char g_szQ[64];         // the filter
int g_nRace;            // every race, Protoss, Terran, Zerg, Independent
bool g_bClassic = true; // the classic rows shown
bool g_bUnsaved;        // only rows with unsaved changes
std::set<std::string> g_sel;  // the mod's rows selected
std::string g_editRow, g_editKey;
char g_szEdit[64];
bool g_bEditFocus;
std::string g_pickRow, g_pickKey;  // a cell whose choices are open
char g_szPickQ[48];
int g_nSetCol;          // the column the selection is set in
char g_szSetVal[64];
std::string g_lastClicked;  // for shift-click runs

void Set(int t, const std::string &name, const std::string &key, const std::string &json) {
  StudioAsk("studio_set", "\"table\":" + Q(Table(t)) + ",\"name\":" + Q(name) + ",\"field\":" + Q(key) + ",\"value\":" + json);
}

void EditInMod(int t, const std::string &name) {
  StudioAsk("studio_create", "\"table\":" + Q(Table(t)) + ",\"name\":" + Q(name + "_edit") + ",\"edit\":" + Q(name));
  StudioSay("the classic " + StudioShowName(t, name) + " is the mod's to edit, as " + name + "_edit");
}

// The choices of an enum or a link, picked into json; true when one is picked.
bool Choices(const StudioField &f, long long nNow, std::string *pJson) {
  bool bPicked = false;
  if (f.editor.compare(0, 5, "enum:") == 0) {
    for (const auto &e : StudioEnum(f.editor.substr(5)))
      if (StudioItem(e.second.c_str(), e.first == nNow)) {
        *pJson = std::to_string(e.first);
        bPicked = true;
      }
    return bPicked;
  }
  if (ImGui::IsWindowAppearing()) {
    g_szPickQ[0] = '\0';
    ImGui::SetKeyboardFocusHere();
  }
  StudioInput("##pickq", g_szPickQ, sizeof g_szPickQ, 280.0f, 24.0f, "Find", "search");
  ImGui::BeginChild("##picks", ImVec2(P(280.0f), P(280.0f)));
  if (StudioItem("none", nNow == StudioClassicRows(f.nRef))) {
    *pJson = std::to_string(StudioClassicRows(f.nRef));
    bPicked = true;
  }
  for (const StudioRecord &r : StudioRecords(f.nRef)) {
    const std::string show = StudioShowName(f.nRef, r.name) + "  (mod)";
    if (r.kind != SK_EDIT && Has(show, g_szPickQ) && StudioItem(show.c_str(), r.nId == nNow)) {
      *pJson = Q(r.name);
      bPicked = true;
    }
  }
  for (int n = 0; n < StudioClassicRows(f.nRef); ++n) {
    const char *psz = StudioClassicName(f.nRef, n);
    if (psz == nullptr)
      continue;
    const std::string show = StudioShowName(f.nRef, psz);
    ImGui::PushID(n);
    if (Has(show, g_szPickQ) && StudioItem(show.c_str(), n == nNow)) {
      *pJson = Q(psz);
      bPicked = true;
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  return bPicked;
}

// ---- the bar ----------------------------------------------------------------------------------------

float Bar(ImDrawList *d, ImVec2 a, float W, int t, int nShown, int nAll) {
  const float h = P(30.0f);
  float x = a.x;
  // units and weapons a click away, the other tables in a pulldown beside them
  static const char *const kKinds[] = {"Units", "Weapons"};
  static const char *const kTables[ST_TABLES] = {"Units",   "Weapons", "Flingy",   "Sprites", "Images",
                                                 "Orders",  "Techs",   "Upgrades", "Sounds",  "Portraits"};
  int nKind = g_nKind;
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
  const int nSeg = StudioSeg("##kind", kKinds, 2, g_nKind <= ST_WEAPONS ? g_nKind : -1);
  if (nSeg >= 0 && nSeg <= ST_WEAPONS)
    nKind = nSeg;
  x += StudioSegWidth(kKinds, 2) + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
  if (StudioSelectBegin("##table", g_nKind > ST_WEAPONS ? kTables[g_nKind] : "More tables", 120.0f)) {
    for (int i = ST_FLINGY; i < ST_TABLES; ++i)
      if (StudioItem(kTables[i], g_nKind == i))
        nKind = i;
    StudioSelectEnd();
  }
  x += P(120.0f) + P(14.0f);
  if (nKind != g_nKind) {
    g_nKind = nKind;
    g_sel.clear();
    g_editRow.clear();
  }
  const std::vector<ColSet> &sets = Sets(t);
  std::vector<const char *> labels;
  for (const ColSet &s : sets)
    labels.push_back(s.pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
  g_anSet[g_nKind] = StudioSeg("##set", labels.data(), static_cast<int>(labels.size()), g_anSet[g_nKind]);
  x += StudioSegWidth(labels.data(), static_cast<int>(labels.size())) + P(14.0f);
  // the right side: the count, the checks, the race, the filter
  const std::string count = "showing " + std::to_string(nShown) + " of " + std::to_string(nAll);
  float r = a.x + W - TW(STUDIO_FONT_TEXT, kStudioFsS, count);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, r, Mid(a.y, h, kStudioFsS), C(TK_INK3), count);
  r -= P(14.0f) + P(118.0f);
  ImGui::SetCursorScreenPos(ImVec2(r, a.y + (h - P(20.0f)) / 2.0f));
  StudioCheck("##unsaved", "Unsaved only", &g_bUnsaved);
  r -= P(112.0f);
  ImGui::SetCursorScreenPos(ImVec2(r, a.y + (h - P(20.0f)) / 2.0f));
  StudioCheck("##classic", "Classic rows", &g_bClassic);
  if (t == ST_UNITS) {
    static const char *const kRaces[] = {"Every race", "Protoss", "Terran", "Zerg", "Independent"};
    r -= P(130.0f);
    ImGui::SetCursorScreenPos(ImVec2(r, a.y + (h - P(26.0f)) / 2.0f));
    if (StudioSelectBegin("##race", kRaces[g_nRace], 120.0f)) {
      for (int i = 0; i < 5; ++i)
        if (StudioItem(kRaces[i], g_nRace == i))
          g_nRace = i;
      StudioSelectEnd();
    }
  }
  const float fw = std::min(P(240.0f), r - x - P(10.0f));
  if (fw > P(90.0f)) {
    ImGui::SetCursorScreenPos(ImVec2(r - P(10.0f) - fw, a.y + (h - P(28.0f)) / 2.0f));
    StudioInput("##q", g_szQ, sizeof g_szQ, fw / StudioPx(1.0f), 28.0f, "Filter by name, race or base", "filter");
  }
  return h;
}

// Setting one column for every selected row.
float MultiBar(ImDrawList *d, ImVec2 a, float W, int t, const std::vector<Col> &cols) {
  std::vector<const Col *> editable;
  for (const Col &c : cols)
    if (c.pf && Inline(*c.pf))
      editable.push_back(&c);
  const float h = P(40.0f);
  d->AddRectFilled(a, ImVec2(a.x + W, a.y + h), C(TK_SEL), P(3.0f));
  d->AddRect(a, ImVec2(a.x + W, a.y + h), C(TK_SEL_LINE2), P(3.0f));
  float x = a.x + P(12.0f);
  const std::string n = std::to_string(g_sel.size()) + (g_sel.size() == 1 ? " row selected" : " rows selected");
  x += T(d, STUDIO_FONT_BOLD, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_HI), n) + P(14.0f);
  if (editable.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_INK2), "no column of this set is set in place");
    return h + P(8.0f);
  }
  g_nSetCol = std::min(g_nSetCol, static_cast<int>(editable.size()) - 1);
  const Col &c = *editable[static_cast<size_t>(g_nSetCol)];
  x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_INK2), "Set") + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
  if (StudioSelectBegin("##setcol", c.label.c_str(), 170.0f)) {
    for (size_t i = 0; i < editable.size(); ++i)
      if (StudioItem(editable[i]->label.c_str(), static_cast<int>(i) == g_nSetCol)) {
        g_nSetCol = static_cast<int>(i);
        g_szSetVal[0] = '\0';
      }
    StudioSelectEnd();
  }
  x += P(178.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_INK2), "to") + P(8.0f);
  std::string json;
  bool bApply = false;
  const StudioField &f = *c.pf;
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
  if (f.editor.compare(0, 5, "enum:") == 0 || f.nRef >= 0 || f.editor == "bool") {
    // a choice sets the rows at once
    if (StudioSelectBegin("##setval", "pick one", 170.0f)) {
      if (f.editor == "bool") {
        if (StudioItem("yes")) {
          json = "1";
          bApply = true;
        }
        if (StudioItem("no")) {
          json = "0";
          bApply = true;
        }
      } else {
        bApply = Choices(f, -1, &json);
      }
      StudioSelectEnd();
    }
    x += P(178.0f);
  } else {
    StudioInput("##setval", g_szSetVal, sizeof g_szSetVal, 110.0f, 26.0f, f.editor == "frames" ? "seconds" : "value",
                nullptr, nullptr, true);
    const bool bEnter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    x += P(118.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
    if ((StudioButton("##set", "Set", nullptr, SB_GOOD, 26.0f) || bEnter) && Typed(f, g_szSetVal, &json))
      bApply = true;
    x += StudioButtonSize("Set", nullptr, 26.0f).x + P(8.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
  if (StudioButton("##clearsel", "Clear the selection", nullptr, SB_FLAT, 26.0f))
    g_sel.clear();
  if (bApply) {
    for (const std::string &name : g_sel)
      Set(t, name, f.key, json);
    StudioSay(f.label + " set for " + std::to_string(g_sel.size()) + (g_sel.size() == 1 ? " row" : " rows") + "; Undo takes each back");
  }
  return h + P(8.0f);
}

}  // namespace

void StudioTables(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const int t = g_nKind;
  const float W = b.x - a.x - P(32.0f);
  ImVec2 o(a.x + P(16.0f), a.y + P(12.0f));
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, o.y + P(10.0f), C(TK_INK3), "Open a mod to see its tables.");
    return;
  }
  const std::vector<Col> cols = Columns(t, g_anSet[g_nKind]);
  // the rows the filter and the checks leave, the mod's first, each part in the sort's order
  const std::vector<Row> &all = AllRows(t);
  static const char *const kRaceWords[] = {"", "Protoss", "Terran", "Zerg", "Independent"};
  std::vector<const Row *> mods, classic;
  for (const Row &r : all) {
    if (!g_bClassic && r.kind == SK_CLASSIC)
      continue;
    if (t == ST_UNITS && g_nRace > 0 && r.race != kRaceWords[g_nRace])
      continue;
    if (g_szQ[0] && !Has(r.show, g_szQ) && !Has(r.name, g_szQ) && !Has(r.race, g_szQ) && !Has(r.base, g_szQ))
      continue;
    if (g_bUnsaved && (!Editable(r) || StudioChangedSinceSave(t, r.name) == 0))
      continue;
    (r.kind == SK_CLASSIC ? classic : mods).push_back(&r);
  }
  for (auto it = g_sel.begin(); it != g_sel.end();) {
    bool bThere = false;
    for (const Row &r : all)
      bThere = bThere || (r.name == *it && Editable(r));
    it = bThere ? std::next(it) : g_sel.erase(it);
  }
  float y = o.y;
  y += Bar(d, ImVec2(o.x, y), W, t, static_cast<int>(mods.size() + classic.size()), static_cast<int>(all.size())) + P(10.0f);
  if (!g_sel.empty())
    y += MultiBar(d, ImVec2(o.x, y), W, t, cols);
  // the table: a check, the mark, the name, what it is based on, then the set's columns
  const float footH = P(30.0f);
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  const ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit |
                                ImGuiTableFlags_NoBordersInBodyUntilResize;
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(P(8.0f), P(5.0f)));
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, C(TK_PANE2));
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, C(TK_ROW));
  const int nCols = 4 + static_cast<int>(cols.size());
  if (ImGui::BeginTable("##table", nCols, flags, ImVec2(W, b.y - y - footH - P(8.0f)))) {
    ImGui::TableSetupScrollFreeze(3, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize, P(22.0f), 1000);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, P(200.0f), 1001);
    ImGui::TableSetupColumn(t == ST_UNITS ? "Race, based on" : "Based on", ImGuiTableColumnFlags_NoSort, P(190.0f), 1002);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize, P(1.0f), 1003);
    for (size_t i = 0; i < cols.size(); ++i) {
      const bool bWide = cols[i].pf && (cols[i].pf->nRef >= 0 || cols[i].pf->editor.compare(0, 5, "enum:") == 0);
      ImGui::TableSetupColumn(cols[i].label.c_str(), cols[i].pf ? 0 : ImGuiTableColumnFlags_None,
                              P(bWide ? 150.0f : std::max(84.0f, TW(STUDIO_FONT_TEXT, kStudioFsS, cols[i].label) / StudioPx(1.0f) + 26.0f)),
                              static_cast<ImGuiID>(i));
    }
    ImGui::TableHeadersRow();
    // the sort: by the column asked, each part on its own
    if (ImGuiTableSortSpecs *pSpecs = ImGui::TableGetSortSpecs()) {
      if (pSpecs->SpecsCount > 0) {
        const ImGuiTableColumnSortSpecs &s = pSpecs->Specs[0];
        const bool bDown = s.SortDirection == ImGuiSortDirection_Descending;
        auto sort = [&](std::vector<const Row *> &v) {
          if (s.ColumnUserID == 1001) {
            std::stable_sort(v.begin(), v.end(), [&](const Row *x, const Row *y2) {
              return bDown ? x->show > y2->show : x->show < y2->show;
            });
            return;
          }
          if (s.ColumnUserID >= cols.size())
            return;
          const Col &c = cols[s.ColumnUserID];
          std::vector<std::pair<Cell, const Row *>> keyed;
          for (const Row *r : v)
            keyed.push_back({CellOf(t, r->name, c), r});
          std::stable_sort(keyed.begin(), keyed.end(), [&](const std::pair<Cell, const Row *> &x, const std::pair<Cell, const Row *> &y2) {
            if (x.first.bText || y2.first.bText)
              return bDown ? x.first.text > y2.first.text : x.first.text < y2.first.text;
            return bDown ? x.first.d > y2.first.d : x.first.d < y2.first.d;
          });
          for (size_t i = 0; i < v.size(); ++i)
            v[i] = keyed[i].second;
        };
        sort(mods);
        sort(classic);
      }
    }
    // the rows: a heading for each part, then its rows, only those in view drawn
    struct Item {
      const Row *pRow;
      const char *pszHead;
    };
    std::vector<Item> items;
    const std::string modHead = "In the mod: " + std::to_string(mods.size()) + (mods.size() == 1 ? " row" : " rows") + ", edited in place";
    const std::string clsHead = "Classic: " + std::to_string(classic.size()) + " rows, read only; Edit in the mod to change one for this mod";
    items.push_back({nullptr, modHead.c_str()});
    for (const Row *r : mods)
      items.push_back({r, nullptr});
    if (g_bClassic) {
      items.push_back({nullptr, clsHead.c_str()});
      for (const Row *r : classic)
        items.push_back({r, nullptr});
    }
    const float rowH = P(30.0f);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(items.size()), rowH);
    while (clip.Step()) {
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const Item &it = items[static_cast<size_t>(i)];
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
        if (it.pRow == nullptr) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, C(TK_PANE2));
          ImGui::TableSetColumnIndex(1);
          const ImVec2 p = ImGui::GetCursorScreenPos();
          ImDrawList *td = ImGui::GetWindowDrawList();
          // the heading runs on past its column, over the empty cells beside it
          td->PushClipRect(ImVec2(p.x, p.y - P(5.0f)), ImVec2(p.x + W, p.y + rowH), false);
          T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x, Mid(p.y - P(5.0f), rowH, kStudioFsS), C(TK_LABEL), it.pszHead);
          td->PopClipRect();
          continue;
        }
        const Row &r = *it.pRow;
        const bool bEdit = Editable(r);
        ImGui::PushID(r.name.c_str());
        ImDrawList *td = ImGui::GetWindowDrawList();
        // the check
        ImGui::TableSetColumnIndex(0);
        if (bEdit) {
          bool bOn = g_sel.count(r.name) != 0;
          if (StudioCheck("##sel", "", &bOn)) {
            if (ImGui::GetIO().KeyShift && !g_lastClicked.empty()) {
              // a run from the last row clicked to this one, among the mod's rows as shown
              int i0 = -1, i1 = -1;
              for (size_t k = 0; k < mods.size(); ++k) {
                if (mods[k]->name == g_lastClicked)
                  i0 = static_cast<int>(k);
                if (mods[k]->name == r.name)
                  i1 = static_cast<int>(k);
              }
              if (i0 >= 0 && i1 >= 0)
                for (int k = std::min(i0, i1); k <= std::max(i0, i1); ++k)
                  g_sel.insert(mods[static_cast<size_t>(k)]->name);
            } else if (bOn) {
              g_sel.insert(r.name);
            } else {
              g_sel.erase(r.name);
            }
            g_lastClicked = r.name;
          }
        }
        // the name, which opens it, with the mod's mark and an unsaved dot
        ImGui::TableSetColumnIndex(1);
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float cw = ImGui::GetContentRegionAvail().x;
          if (bEdit)
            td->AddRectFilled(ImVec2(p.x - P(8.0f), p.y - P(3.0f)), ImVec2(p.x - P(5.0f), p.y + rowH - P(7.0f)), C(TK_MOD), P(1.5f));
          const bool bChanged = bEdit && StudioChangedSinceSave(t, r.name) > 0;
          if (ImGui::InvisibleButton("##open", ImVec2(std::max(cw, 1.0f), rowH - P(10.0f))))
            StudioOpenDoc(t, r.name, 0);
          const bool bHover = ImGui::IsItemHovered();
          if (bHover)
            ImGui::SetTooltip("Open %s", r.show.c_str());
          const float tw = T(td, bEdit ? STUDIO_FONT_BOLD : STUDIO_FONT_TEXT, kStudioFsS, p.x, Mid(p.y - P(5.0f), rowH, kStudioFsS),
                             bHover ? C(TK_HI) : bEdit ? C(TK_INK) : C(TK_INK2), r.show, cw - (bChanged ? P(14.0f) : 0.0f));
          if (bChanged)
            td->AddCircleFilled(ImVec2(p.x + tw + P(8.0f), p.y + rowH / 2.0f - P(5.0f)), P(3.5f), C(TK_CHANGED));
        }
        // its race and base; a classic row's way into the mod
        ImGui::TableSetColumnIndex(2);
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float cw = ImGui::GetContentRegionAvail().x;
          if (r.kind == SK_CLASSIC) {
            const bool bRowHover = ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex();
            if (bRowHover) {
              ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(2.0f)));
              if (StudioButton("##edit", "Edit in the mod", "pen", SB_FLAT, 22.0f, 0.0f, kStudioFsXs))
                EditInMod(t, r.name);
              if (t == ST_UNITS) {
                ImGui::SameLine(0.0f, P(2.0f));
                if (StudioButton("##copy", "Copy", "copy", SB_FLAT, 22.0f, 0.0f, kStudioFsXs))
                  StudioNewUnitOpen(r.name);
              }
            } else {
              T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x, Mid(p.y - P(5.0f), rowH, kStudioFsXs), C(TK_INK3),
                (t == ST_UNITS ? r.race + ", " : std::string()) + "classic", cw);
            }
          } else {
            std::string s = t == ST_UNITS ? r.race : std::string();
            if (r.kind == SK_EDIT)
              s += (s.empty() ? "" : ", ") + std::string("an edit of the classic row");
            else if (!r.base.empty())
              s += (s.empty() ? "" : ", ") + std::string("from the ") + Short(t, r.base);
            if (r.kind == SK_DEPENDENCY)
              s += ", read only";
            T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x, Mid(p.y - P(5.0f), rowH, kStudioFsXs), C(TK_INK2), s, cw);
          }
        }
        // the set's columns
        for (size_t ci = 0; ci < cols.size(); ++ci) {
          ImGui::TableSetColumnIndex(4 + static_cast<int>(ci));
          const Col &c = cols[ci];
          const Cell cell = CellOf(t, r.name, c);
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float cw = ImGui::GetContentRegionAvail().x;
          ImGui::PushID(static_cast<int>(ci));
          const bool bCanEdit = bEdit && c.pf && Inline(*c.pf);
          const bool bEditing = bCanEdit && g_editRow == r.name && g_editKey == c.key;
          if (bEditing) {
            ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(3.0f)));
            if (g_bEditFocus) {
              ImGui::SetKeyboardFocusHere();
              g_bEditFocus = false;
            }
            ImGui::SetNextItemWidth(cw + P(8.0f));
            const bool bDone = ImGui::InputText("##ed", g_szEdit, sizeof g_szEdit,
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool bEscape = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool bTab = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Tab);
            if (bEscape) {
              g_editRow.clear();
            } else if (bDone || bTab || ImGui::IsItemDeactivated()) {
              std::string json;
              if (Typed(*c.pf, g_szEdit, &json) && std::string(g_szEdit) != cell.text)
                Set(t, r.name, c.key, json);
              g_editRow.clear();
              if (bTab) {
                // on to the next column that edits in place
                for (size_t k = ci + 1; k < cols.size(); ++k)
                  if (cols[k].pf && Inline(*cols[k].pf) && cols[k].pf->nRef < 0 && cols[k].pf->editor.compare(0, 5, "enum:") != 0 &&
                      cols[k].pf->editor != "bool") {
                    g_editRow = r.name;
                    g_editKey = cols[k].key;
                    snprintf(g_szEdit, sizeof g_szEdit, "%s", CellOf(t, r.name, cols[k]).text.c_str());
                    g_bEditFocus = true;
                    break;
                  }
              }
            }
          } else {
            const bool bNum = !cell.bText;
            const ImU32 col = c.pf == nullptr ? C(TK_LABEL) : cell.bOwn && bEdit ? C(TK_HI) : bEdit ? C(TK_INK2) : C(TK_INK2);
            const int font = bNum ? STUDIO_FONT_MONO : STUDIO_FONT_TEXT;
            const float tw = TW(font, kStudioFsS, cell.text);
            const float tx = bNum ? p.x + cw - std::min(tw, cw) : p.x;
            if (bCanEdit) {
              ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(3.0f)));
              if (ImGui::InvisibleButton("##cell", ImVec2(cw + P(8.0f), rowH - P(4.0f)))) {
                if (c.pf->editor == "bool") {
                  Set(t, r.name, c.key, cell.d != 0.0 ? "0" : "1");
                } else if (c.pf->nRef >= 0 || c.pf->editor.compare(0, 5, "enum:") == 0) {
                  g_pickRow = r.name;
                  g_pickKey = c.key;
                  ImGui::OpenPopup("##pick");
                } else {
                  g_editRow = r.name;
                  g_editKey = c.key;
                  snprintf(g_szEdit, sizeof g_szEdit, "%s", cell.text.c_str());
                  g_bEditFocus = true;
                }
              }
              if (ImGui::IsItemHovered())
                td->AddRect(ImVec2(p.x - P(4.0f), p.y - P(3.0f)), ImVec2(p.x + cw + P(4.0f), p.y + rowH - P(7.0f)), C(TK_BTN_RIM_HI), P(2.0f));
            }
            if (cell.bOwn && bEdit)
              td->AddRectFilled(ImVec2(p.x - P(5.0f), p.y + P(2.0f)), ImVec2(p.x - P(3.0f), p.y + rowH - P(12.0f)), C(TK_MOD));
            if (bEdit && c.pf && StudioChangedSinceSave(t, r.name, c.key) > 0)
              td->AddCircleFilled(ImVec2(p.x + P(1.0f), p.y + P(1.0f)), P(3.0f), C(TK_CHANGED));
            T(td, font, kStudioFsS, tx, Mid(p.y - P(5.0f), rowH, kStudioFsS), col, cell.text, cw);
            if (g_pickRow == r.name && g_pickKey == c.key && ImGui::BeginPopup("##pick")) {
              std::string json;
              if (Choices(*c.pf, static_cast<long long>(cell.d), &json)) {
                Set(t, r.name, c.key, json);
                ImGui::CloseCurrentPopup();
              }
              ImGui::EndPopup();
            }
          }
          ImGui::PopID();
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
  ImGui::PopFont();
  // the foot: the marks and how to edit
  const float fy = b.y - footH;
  float x = o.x;
  d->AddRectFilled(ImVec2(x, fy + P(8.0f)), ImVec2(x + P(3.0f), fy + P(22.0f)), C(TK_MOD), P(1.5f));
  x += P(9.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK2), "the mod's row, or its own value") + P(16.0f);
  d->AddCircleFilled(ImVec2(x + P(4.0f), fy + footH / 2.0f), P(3.5f), C(TK_CHANGED));
  x += P(12.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK2), "unsaved change") + P(16.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK3),
    "Click a cell of the mod's row to edit it: Enter keeps it, Esc leaves it, Tab goes on. Shift-click a check selects a run.",
    o.x + W - x);
}

void StudioTablesShow(int t) {
  if (t >= 0 && t < ST_TABLES && t != g_nKind) {
    g_nKind = t;
    g_sel.clear();
    g_editRow.clear();
  }
}
