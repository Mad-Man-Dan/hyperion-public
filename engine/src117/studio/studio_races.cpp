// studio_races.cpp -- the Cards and races workspace: the mod's races (types.json's "races",
// docs/mod-data.md), each with its name, the classic race it plays as, what its players start a
// melee game with, its units, the classic units it goes without and the computer's stand-ins, with
// what the loader would refuse said in words; and the Cards page (studio_races_cards.cpp). Every
// change is a studio_race_set, built on StudioPut, so it is an edit the history undoes.
#include <algorithm>
#include <functional>
#include <set>
#include <vector>
#include "imgui_internal.h"
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_races.h"


namespace {

using namespace studio_draw;

int g_nPage = 0;       // 0 races, 1 cards, 2 the tech tree
std::string g_race;    // the race shown

const char *const kBases[] = {"zerg", "terran", "protoss"};
const char *const kBaseWords[] = {"Zerg", "Terran", "Protoss"};

int BaseOf(const PortJson &race) {
  const std::string b = race["base"].Str();
  for (int i = 0; i < 3; ++i)
    if (b == kBases[i])
      return i;
  return -1;
}

using studio_races::NameIn;

bool IsModRec(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && (r.kind == SK_OWN || r.kind == SK_DEPENDENCY);
}

bool IsClassic(int t, const std::string &name, int nMax) {
  const int n = StudioClassicRow(t, name);
  return n >= 0 && n < nMax;
}

// The town hall and workers a classic race starts with.
std::vector<std::pair<const char *, int>> BaseStart(int nBase) {
  if (nBase == 0)
    return {{"zerg_hatchery", 1}, {"zerg_drone", 4}, {"zerg_overlord", 1}};
  if (nBase == 1)
    return {{"terran_command_center", 1}, {"terran_scv", 4}};
  return {{"protoss_nexus", 1}, {"protoss_probe", 4}};
}

// The table a name of the computer's mapping is in: a classic unit's, tech's or upgrade's.
int AiTable(const std::string &name) {
  if (IsClassic(ST_UNITS, name, StudioClassicRows(ST_UNITS)))
    return ST_UNITS;
  if (IsClassic(ST_TECHS, name, StudioClassicRows(ST_TECHS)))
    return ST_TECHS;
  if (IsClassic(ST_UPGRADES, name, StudioClassicRows(ST_UPGRADES)))
    return ST_UPGRADES;
  return -1;
}

std::string Count(long long n, const std::string &one, const std::string &more) {
  return std::to_string(n) + " " + (n == 1 ? one : more);
}

// ---- what a race would be refused for, and what it lacks -------------------------------------------

struct Problem {
  bool bRefused;  // the loader refuses the mod; else a warning
  std::string text;
};

// A unit another race of the mod names among its own, and this one does not.
bool OwnedElsewhere(const std::string &unit, const std::string &id, const PortJson &race) {
  for (size_t i = 0; i < race["units"].size(); ++i)
    if (NameIn(ST_UNITS, race["units"].At(i)) == unit)
      return false;
  for (const PortJsonMember &m : StudioDoc("types")["races"].members)
    for (size_t i = 0; i < m.second["units"].size(); ++i)
      if (NameIn(ST_UNITS, m.second["units"].At(i)) == unit && m.first != id)
        return true;
  return false;
}

// The loader's rules for a race (port_moddata.cpp, Races), and what makes one not play well.
std::vector<Problem> Problems(const std::string &id, const PortJson &r) {
  std::vector<Problem> out;
  const int nBase = BaseOf(r);
  if (nBase < 0)
    out.push_back({true, "Its base is zerg, terran or protoss."});
  const PortJson &start = r["start"];
  bool bHall = false;
  if (start.kind != PortJson::NONE && start.kind != PortJson::ARRAY)
    out.push_back({true, "Its start units are a list."});
  for (size_t i = 0; i < start.size(); ++i) {
    const PortJson &s = start.At(i);
    const std::string u = NameIn(ST_UNITS, s.kind == PortJson::OBJECT ? s["unit"] : s);
    const double n = s.kind == PortJson::OBJECT ? s["count"].Num(1) : 1;
    StudioRecord rec;
    if (u.empty() || !StudioRecordOf(ST_UNITS, u, &rec))
      out.push_back({true, "A start unit, " + (u.empty() ? std::string("unnamed") : u) + ", is no unit."});
    else if ((StudioNumber(ST_UNITS, u, "flags") >> 12) & 1)
      bHall = true;
    if (n < 1 || n > 12)
      out.push_back({true, "The count of " + StudioShowName(ST_UNITS, u) + " is 1 to 12."});
  }
  if (start.kind == PortJson::ARRAY && !bHall)
    out.push_back({false, "Its start has no town hall (a resource depot): its players have nowhere to bring what they gather."});
  for (size_t i = 0; i < r["units"].size(); ++i) {
    const std::string u = NameIn(ST_UNITS, r["units"].At(i));
    if (!IsModRec(ST_UNITS, u))
      out.push_back({true, "Its units are the mod's: " + StudioShowName(ST_UNITS, u) + " is not."});
    else if (nBase >= 0 && studio_races::UnitRace(u) >= 0 && studio_races::UnitRace(u) != nBase)
      out.push_back({false, StudioShowName(ST_UNITS, u) + " is " + studio_races::RaceWord(studio_races::UnitRace(u)) + " by its group flags; it builds the " +
                                studio_races::RaceWord(studio_races::UnitRace(u)) + " way in a " + kBaseWords[nBase] + " race."});
  }
  std::set<std::string> without;
  for (size_t i = 0; i < r["without"].size(); ++i) {
    const std::string u = NameIn(ST_UNITS, r["without"].At(i));
    without.insert(u);
    if (!IsClassic(ST_UNITS, u, StudioClassicRows(ST_UNITS)))
      out.push_back({true, "What it goes without is classic units: " + StudioShowName(ST_UNITS, u) + " is not."});
  }
  const PortJson &ai = r["ai"];
  if (ai.kind != PortJson::NONE && ai.kind != PortJson::OBJECT)
    out.push_back({true, "The computer's stand-ins are an object, classic name to the mod's."});
  for (const PortJsonMember &m : ai.members) {
    const int t = AiTable(m.first);
    const std::string to = NameIn(t < 0 ? ST_UNITS : t, m.second);
    StudioRecord rec;
    if (t < 0)
      out.push_back({true, "The computer's " + m.first + " is no classic unit, tech or upgrade."});
    else if (to.empty() || !StudioRecordOf(t, to, &rec))
      out.push_back({true, "The computer's stand-in for " + StudioShowName(t, m.first) + ", " + (to.empty() ? std::string("none") : to) +
                               ", is no " + (t == ST_UNITS ? "unit" : t == ST_TECHS ? "tech" : "upgrade") + "."});
    else if (t == ST_UNITS && OwnedElsewhere(to, id, r))
      out.push_back({false, StudioShowName(ST_UNITS, to) + " stands in for " + StudioShowName(ST_UNITS, m.first) +
                                " but is another race's own unit, which this race's players never train."});
    else if (t == ST_UNITS && rec.kind != SK_CLASSIC) {
      // a stand-in is made only where its needs say, as a player's
      // an "at" anywhere in its needs, an either-or among them too
      std::function<bool(const PortJson &)> hasAt = [&](const PortJson &v) {
        if (v.kind == PortJson::OBJECT)
          return v["at"].kind != PortJson::NONE;
        for (size_t i = 0; v.kind == PortJson::ARRAY && i < v.size(); ++i)
          if (hasAt(v.At(i)))
            return true;
        return false;
      };
      const bool bAt = hasAt(StudioRecordJson(ST_UNITS, to)["requires"]);
      if (!bAt)
        out.push_back({false, StudioShowName(ST_UNITS, to) + " stands in for " + StudioShowName(ST_UNITS, m.first) +
                                  " but is made nowhere: give it where it is made."});
    }
    without.erase(m.first);
  }
  for (const std::string &u : without)
    if (IsClassic(ST_UNITS, u, StudioClassicRows(ST_UNITS)))
      out.push_back({false, "A computer of the race waits for ever where a script asks for " + StudioShowName(ST_UNITS, u) +
                                ", which it goes without: give it a stand-in."});
  return out;
}

bool Refused(const std::string &id, const PortJson &r, std::string *pWhy) {
  for (const Problem &p : Problems(id, r))
    if (p.bRefused) {
      *pWhy = p.text;
      return true;
    }
  return false;
}

// ---- the panels' pieces -----------------------------------------------------------------------------

void Ask(const std::string &race, const std::string &fields) {
  StudioAsk("studio_race_set", "\"race\":" + Q(race) + (fields.empty() ? "" : "," + fields));
}

void SetField(const std::string &race, const char *pszField, const PortJson &v) {
  const bool bNone = v.kind == PortJson::NONE || (v.kind == PortJson::ARRAY && v.items.empty()) ||
                     (v.kind == PortJson::OBJECT && v.members.empty());
  Ask(race, "\"field\":" + Q(pszField) + ",\"value\":" + (bNone ? std::string("null") : PortJsonWrite(v)));
}

void Gap(float f = 8.0f) {
  ImGui::Dummy(ImVec2(1, P(f)));
}

PortJson Arr(void) {
  PortJson a;
  a.kind = PortJson::ARRAY;
  return a;
}

void Head(ImDrawList *d, const char *pszHead, const char *pszFine, float W) {
  ImGui::Dummy(ImVec2(1, P(14.0f)));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  d->AddLine(ImVec2(p.x, p.y - P(6.0f)), ImVec2(p.x + W, p.y - P(6.0f)), C(TK_LINE));
  const float x = p.x + Wide(d, 10.5f, p.x, p.y + P(4.0f), C(TK_HEAD), pszHead) + P(12.0f);
  if (pszFine)
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, p.y + P(2.0f), C(TK_INK3), pszFine, p.x + W - x);
  ImGui::Dummy(ImVec2(W, P(26.0f)));
}

// Chips laid out left to right, wrapping at the right edge: true when one's x is clicked.
struct Chips {
  float x0, x, y, right;
  explicit Chips(float fRight) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    x0 = x = p.x;
    y = p.y;
    right = fRight;
  }
  void Place(float w) {
    if (x + w > right && x > x0) {
      x = x0;
      y += P(30.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    x += w + P(6.0f);
  }
  bool Chip(const std::string &text, const char *pszGlyph, bool bX, int nKind = SB_PLAIN, bool *pbOpen = nullptr) {
    const float bw = StudioButtonSize(text.c_str(), pszGlyph, 24.0f, kStudioFsXs).x;
    const float w = bw + (bX ? P(24.0f) : 0.0f);
    Place(w);
    const ImVec2 a(x - w - P(6.0f), y);
    ImGui::SetCursorScreenPos(a);
    const bool bOpen = StudioButton("##chip", text.c_str(), pszGlyph, nKind, 24.0f, 0.0f, kStudioFsXs);
    if (pbOpen)
      *pbOpen = bOpen;
    bool bOut = false;
    if (bX) {
      ImGui::SetCursorScreenPos(ImVec2(a.x + bw, a.y));
      bOut = StudioButton("##x", "", "x", SB_FLAT, 24.0f, 22.0f);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Take it out");
    }
    return bOut;
  }
  void End() {
    ImGui::SetCursorScreenPos(ImVec2(x0, y + P(24.0f)));
    ImGui::Dummy(ImVec2(1, P(4.0f)));
  }
};

// A list of units as chips, each opened with a click and taken out with its x; one added from a
// picker. The list's new value, when it changed.
bool UnitChips(const char *pszId, const PortJson &list, int nWhich, int nRace, float W, PortJson *pNew, const char *pszAdd) {
  ImGui::PushID(pszId);
  Chips row(ImGui::GetCursorScreenPos().x + W);
  int nOut = -1;
  for (size_t i = 0; i < list.size(); ++i) {
    const std::string u = NameIn(ST_UNITS, list.At(i));
    ImGui::PushID(static_cast<int>(i));
    bool bOpen = false;
    if (row.Chip(StudioShowName(ST_UNITS, u), "unit", true, SB_PLAIN, &bOpen))
      nOut = static_cast<int>(i);
    if (bOpen)
      StudioOpenDoc(ST_UNITS, u, 0);
    ImGui::PopID();
  }
  row.Place(P(210.0f));
  std::string picked;
  const bool bPicked = studio_races::Pick("##add", ST_UNITS, std::string(), 200.0f, &picked, nWhich, 24.0f, pszAdd, nRace, nullptr);
  row.End();
  ImGui::PopID();
  if (nOut >= 0) {
    *pNew = list.kind == PortJson::ARRAY ? list : Arr();
    pNew->items.erase(pNew->items.begin() + nOut);
    return true;
  }
  if (bPicked && !picked.empty()) {
    for (size_t i = 0; i < list.size(); ++i)
      if (NameIn(ST_UNITS, list.At(i)) == picked)
        return false;
    *pNew = list.kind == PortJson::ARRAY ? list : Arr();
    pNew->items.push_back(StudioJsonString(picked));
    return true;
  }
  return false;
}

void Fine(const std::string &s, float W) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushTextWrapPos(p.x + W - ImGui::GetWindowPos().x);
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsXs);
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK3));
  ImGui::TextWrapped("%s", s.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
}

// ---- a race's form ----------------------------------------------------------------------------------

void StartSection(ImDrawList *d, const std::string &id, const PortJson &r, int nBase, float W) {
  Head(d, "Start", "what each of its players has when a melee game starts", W);
  const PortJson &start = r["start"];
  bool bOwn = start.kind != PortJson::NONE;
  if (StudioCheck("##ownstart", "Its own start units, in place of its base's town hall and workers", &bOwn)) {
    if (bOwn) {
      PortJson list = Arr();
      for (const auto &s : BaseStart(nBase < 0 ? 2 : nBase)) {
        PortJson o = StudioJsonObject();
        StudioJsonPut(o, "unit", StudioJsonString(s.first));
        if (s.second > 1)
          StudioJsonPut(o, "count", StudioJsonNumber(s.second));
        list.items.push_back(o);
      }
      SetField(id, "start", list);
    } else {
      Ask(id, "\"field\":\"start\",\"value\":null");
    }
  }
  std::string words;
  if (!bOwn) {
    for (const auto &s : BaseStart(nBase < 0 ? 2 : nBase))
      words += (words.empty() ? "" : ", ") + (s.second > 1 ? std::to_string(s.second) + " " + Short(ST_UNITS, s.first) + "s" : An(Short(ST_UNITS, s.first)));
    Gap(4.0f);
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), ("It starts as the classic " + std::string(nBase < 0 ? "Protoss" : kBaseWords[nBase]) +
                                                           " do: " + words + ".").c_str());
    return;
  }
  // each start unit: which, how many, taken out
  int nOut = -1;
  PortJson list = start.kind == PortJson::ARRAY ? start : Arr();
  bool bChanged = false;
  for (size_t i = 0; i < list.items.size(); ++i) {
    PortJson &s = list.items[i];
    const bool bObject = s.kind == PortJson::OBJECT;
    const std::string u = NameIn(ST_UNITS, bObject ? s["unit"] : s);
    double n = bObject ? s["count"].Num(1) : 1;
    ImGui::PushID(static_cast<int>(i));
    std::string picked;
    if (studio_races::Pick("##unit", ST_UNITS, u, 260.0f, &picked, studio_races::PICK_ANY, 26.0f, "a unit...", nBase, nullptr) && !picked.empty()) {
      PortJson o = StudioJsonObject();
      StudioJsonPut(o, "unit", StudioJsonString(picked));
      if (n > 1)
        StudioJsonPut(o, "count", StudioJsonNumber(n));
      s = o;
      bChanged = true;
    }
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioNumber("##count", &n, 56.0f, 26.0f, true, 13.0f)) {
      n = std::max(1.0, std::min(12.0, floor(n + 0.5)));
      PortJson o = StudioJsonObject();
      StudioJsonPut(o, "unit", StudioJsonString(u));
      if (n > 1)
        StudioJsonPut(o, "count", StudioJsonNumber(n));
      s = o;
      bChanged = true;
    }
    ImGui::SameLine(0.0f, P(6.0f));
    const ImVec2 q = ImGui::GetCursorScreenPos();
    const bool bHall = !u.empty() && ((StudioNumber(ST_UNITS, u, "flags") >> 12) & 1);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, q.x, q.y + P(6.0f), C(TK_INK3), bHall ? "where the town hall goes" : "at the start location");
    ImGui::Dummy(ImVec2(P(150.0f), P(26.0f)));
    ImGui::SameLine(0.0f, P(6.0f));
    if (StudioButton("##out", "", "x", SB_FLAT, 26.0f, 26.0f))
      nOut = static_cast<int>(i);
    ImGui::PopID();
  }
  if (nOut >= 0) {
    list.items.erase(list.items.begin() + nOut);
    bChanged = true;
  }
  std::string picked;
  if (studio_races::Pick("##addstart", ST_UNITS, std::string(), 260.0f, &picked, studio_races::PICK_ANY, 26.0f, "+ a start unit", nBase,
                         nullptr) &&
      !picked.empty()) {
    PortJson o = StudioJsonObject();
    StudioJsonPut(o, "unit", StudioJsonString(picked));
    list.items.push_back(o);
    bChanged = true;
  }
  if (bChanged)
    Ask(id, "\"field\":\"start\",\"value\":" + PortJsonWrite(list));  // an empty list is a start of nothing
  for (const PortJson &s : start.items) {
    const bool bObject = s.kind == PortJson::OBJECT;
    const std::string u = NameIn(ST_UNITS, bObject ? s["unit"] : s);
    const long long n = static_cast<long long>(bObject ? s["count"].Num(1) : 1);
    words += (words.empty() ? "" : ", ") + (n > 1 ? std::to_string(n) + " " + Short(ST_UNITS, u) + "s" : An(Short(ST_UNITS, u)));
  }
  Gap(4.0f);
  Fine(words.empty() ? std::string("Its players start with nothing.") : "Each player of it starts with " + words + ".", W);
}

void UnitsSection(ImDrawList *d, const std::string &id, const PortJson &r, int nBase, float W) {
  Head(d, "Its units", "the mod's units only its players train; a mod unit no race names is any player's", W);
  PortJson now;
  if (UnitChips("##units", r["units"], studio_races::PICK_MOD, nBase, W, &now, "+ one of the mod's units"))
    SetField(id, "units", now);
  // the mod's units no race names, offered
  std::set<std::string> named;
  for (const PortJsonMember &m : StudioDoc("types")["races"].members)
    for (size_t i = 0; i < m.second["units"].size(); ++i)
      named.insert(NameIn(ST_UNITS, m.second["units"].At(i)));
  std::vector<std::string> free;
  for (const StudioRecord &rec : StudioRecords(ST_UNITS))
    if ((rec.kind == SK_OWN || rec.kind == SK_DEPENDENCY) && !named.count(rec.name) &&
        (nBase < 0 || studio_races::UnitRace(rec.name) == nBase || studio_races::UnitRace(rec.name) < 0))
      free.push_back(rec.name);
  if (!free.empty()) {
    Gap(2.0f);
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "Any player's now:");
    Chips row(ImGui::GetCursorScreenPos().x + W);
    int n = 0;
    for (const std::string &u : free) {
      if (++n > 10)
        break;
      ImGui::PushID(u.c_str());
      bool bClick = false;
      row.Chip("+ " + StudioShowName(ST_UNITS, u), nullptr, false, SB_FLAT, &bClick);
      if (bClick) {
        PortJson list = r["units"].kind == PortJson::ARRAY ? r["units"] : Arr();
        list.items.push_back(StudioJsonString(u));
        SetField(id, "units", list);
      }
      ImGui::PopID();
    }
    row.End();
  }
}

void WithoutSection(ImDrawList *d, const std::string &id, const PortJson &r, int nBase, float W) {
  Head(d, "Goes without", "classic units its players do not have, taken out as a map takes units out", W);
  PortJson now;
  if (UnitChips("##without", r["without"], studio_races::PICK_CLASSIC, nBase, W, &now, "+ a classic unit"))
    SetField(id, "without", now);
}

void AiSection(ImDrawList *d, const std::string &id, const PortJson &r, int nBase, float W) {
  Head(d, "The computer", "it plays the classic AI scripts of its base; where one names a classic unit, tech or upgrade, it uses the mod's", W);
  const PortJson &ai = r["ai"];
  PortJson next = ai.kind == PortJson::OBJECT ? ai : StudioJsonObject();
  bool bChanged = false;
  int nOut = -1;
  for (size_t i = 0; i < ai.members.size(); ++i) {
    const PortJsonMember &m = ai.members[i];
    const int t = AiTable(m.first);
    ImGui::PushID(static_cast<int>(i));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    StudioChip(d, ImVec2(p.x, p.y + P(4.0f)), t == ST_UNITS ? "unit" : t == ST_TECHS ? "tech" : t == ST_UPGRADES ? "upgrade" : "?",
               t < 0 ? CH_BAD : CH_PLAIN);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(84.0f), p.y + P(4.0f), t < 0 ? C(TK_BAD) : C(TK_INK), t < 0 ? m.first : StudioShowName(t, m.first),
      P(196.0f));
    StudioGlyph(d, "link", ImVec2(p.x + P(290.0f), p.y + P(5.0f)), P(14.0f), C(TK_INK3));
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(316.0f), p.y));
    std::string picked;
    if (t >= 0 && studio_races::Pick("##to", t, NameIn(t, m.second), 240.0f, &picked, studio_races::PICK_ANY, 26.0f, "pick...", nBase, nullptr) &&
        !picked.empty()) {
      next.members[i].second = StudioJsonString(picked);
      bChanged = true;
    }
    ImGui::SameLine(0.0f, P(6.0f));
    if (StudioButton("##out", "", "x", SB_FLAT, 26.0f, 26.0f))
      nOut = static_cast<int>(i);
    ImGui::PopID();
  }
  if (ai.members.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "No stand-ins: a computer of it plays its base's scripts as they are.");
  // a new stand-in: a classic unit, tech or upgrade, then the mod's
  static int s_nKind = 0;
  static std::string s_from, s_fromFor;
  if (s_fromFor != id) {
    s_fromFor = id;
    s_from.clear();
  }
  static const char *const kKinds[] = {"Unit", "Tech", "Upgrade"};
  static const int kTables[] = {ST_UNITS, ST_TECHS, ST_UPGRADES};
  Gap(6.0f);
  const int nKind = StudioSeg("##kind", kKinds, 3, s_nKind, 26.0f);
  if (nKind != s_nKind) {
    s_nKind = nKind;
    s_from.clear();
  }
  ImGui::SameLine(0.0f, P(8.0f));
  std::string picked;
  if (studio_races::Pick("##from", kTables[s_nKind], s_from, 220.0f, &picked, studio_races::PICK_CLASSIC, 26.0f, "the classic one...", nBase, nullptr))
    s_from = picked;
  ImGui::SameLine(0.0f, P(6.0f));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "becomes");
  ImGui::SameLine(0.0f, P(6.0f));
  if (studio_races::Pick("##tonew", kTables[s_nKind], std::string(), 220.0f, &picked, studio_races::PICK_ANY, 26.0f, "the mod's...", nBase, nullptr) &&
      !picked.empty() && !s_from.empty()) {
    StudioJsonPut(next, s_from, StudioJsonString(picked));
    s_from.clear();
    bChanged = true;
  }
  if (nOut >= 0) {
    next.members.erase(next.members.begin() + nOut);
    bChanged = true;
  }
  // what it goes without and has no stand-in for, each offered one
  std::set<std::string> mapped;
  for (const PortJsonMember &m : next.members)
    mapped.insert(m.first);
  for (size_t i = 0; i < r["without"].size(); ++i) {
    const std::string u = NameIn(ST_UNITS, r["without"].At(i));
    if (mapped.count(u))
      continue;
    ImGui::PushID(u.c_str());
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_WARN), ("! It waits for ever for " + StudioShowName(ST_UNITS, u) + ": a stand-in").c_str());
    ImGui::SameLine(0.0f, P(8.0f));
    std::string to;
    if (studio_races::Pick("##standin", ST_UNITS, std::string(), 220.0f, &to, studio_races::PICK_MOD, 24.0f, "one of the mod's units...", nBase, nullptr) &&
        !to.empty()) {
      StudioJsonPut(next, u, StudioJsonString(to));
      bChanged = true;
    }
    ImGui::PopID();
  }
  if (bChanged)
    SetField(id, "ai", next);
  Gap(2.0f);
  Fine("A stand-in unit is made where its needs say, as a player's: what it needs must be built by the time the script asks, "
       "and the gas there. A computer researches a mod's tech or upgrade only as a stand-in.",
       W);
}

// The stand-ins in what needs the units it goes without: the classic records that need a Probe
// taking the race's own worker too, each in one edit, or all at once.
void StandInSection(ImDrawList *d, const std::string &id, float W) {
  const std::vector<studio_races::StandIn> list = studio_races::RaceStandIns(id);
  int nAll = 0;
  for (const studio_races::StandIn &s : list)
    nAll += s.nRecords;
  if (list.empty())
    return;
  Head(d, "What needs what it goes without", "a building or unit of its base that needs one, as a Pylon needs a Probe, takes the stand-in too", W);
  for (const studio_races::StandIn &s : list) {
    ImGui::PushID(s.unit.c_str());
    const std::string what = s.nRecords == 0 ? "nothing it has needs the " + StudioShowName(ST_UNITS, s.unit) + " alone"
                                             : Count(s.nRecords, "record needs", "records need") + " the " + StudioShowName(ST_UNITS, s.unit);
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, s.nRecords ? C(TK_WARN) : C(TK_INK3), what.c_str());
    if (s.nRecords > 0) {
      ImGui::SameLine(0.0f, P(10.0f));
      if (StudioButton("##fix", ("Let the " + StudioShowName(ST_UNITS, s.by) + " stand in").c_str(), "link", SB_GOLD, 22.0f, 0.0f, kStudioFsXs))
        StudioAsk("studio_race_fix", "\"race\":" + Q(id) + ",\"unit\":" + Q(s.unit));
    }
    ImGui::PopID();
  }
  if (nAll > 0) {
    Gap(4.0f);
    if (StudioButton("##fixall", "Fix all", "done", SB_GOOD, 26.0f, 0.0f, kStudioFsXs))
      StudioAsk("studio_race_fix", "\"race\":" + Q(id));
    ImGui::SameLine(0.0f, P(10.0f));
    if (StudioButton("##seetree", "See its tech tree", "cards", SB_FLAT, 26.0f, 0.0f, kStudioFsXs)) {
      studio_races::TreeShow(id);
      g_nPage = 2;
    }
  }
  Gap(2.0f);
  Fine("Each classic record gets an edit in the mod whose needs take either one, so every race keeps the classic way; "
       "one change, undone as one.",
       W);
}

// What players see and hear of it: the console it shows and the music it plays, its base's or
// another race's.
void LookSection(ImDrawList *d, const std::string &id, const PortJson &r, int nBase, float W) {
  Head(d, "Its console and music", "a race's own look and sound, its base's or another race's", W);
  static const char *const kLooks[] = {"Its base's", "Zerg", "Terran", "Protoss"};
  static const char *const kWords[] = {"console", "music"};
  for (const char *pszField : kWords) {
    const std::string now = r[pszField].Str();
    int n = 0;
    for (int k = 0; k < 3; ++k)
      if (now == kBases[k])
        n = k + 1;
    const ImVec2 q = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsS, q.x, q.y + P(5.0f), C(TK_INK2), std::string(pszField) == "console" ? "Console" : "Music");
    ImGui::SetCursorScreenPos(ImVec2(q.x + P(110.0f), q.y));
    const int nNew = StudioSeg((std::string("##look") + pszField).c_str(), kLooks, 4, n, 26.0f, kStudioFsXs);
    if (nNew != n && nNew >= 0)
      Ask(id, std::string("\"field\":\"") + pszField + "\",\"value\":" + (nNew == 0 ? std::string("null") : Q(kBases[nNew - 1])));
    ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + P(32.0f)));
  }
  // what a player of it gets, the picks read back in words
  auto whose = [&](const char *pszField) -> std::string {
    const std::string v = r[pszField].Str();
    for (int k = 0; k < 3; ++k)
      if (v == kBases[k])
        return kBaseWords[k];
    return nBase < 0 ? std::string("?") : std::string(kBaseWords[nBase]);
  };
  const std::string console = whose("console"), music = whose("music");
  Fine("A player of it sees the " + console + " console and hears " + (music == console ? "its" : "the " + music) +
           " music; its advisor is picked below.",
       W);
  Gap(4.0f);
}

void RaceForm(const std::string &id, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson &r = StudioDoc("types")["races"][id.c_str()];
  if (r.kind != PortJson::OBJECT) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFs, C(TK_INK3), "Pick a race, or make one.");
    return;
  }
  const int nBase = BaseOf(r);
  // the head: its name as players see it, its record's name, its base, and away with it
  ImVec2 p = ImGui::GetCursorScreenPos();
  std::string out;
  if (studio_races::TextField("##name", id + "#name", r["name"].Str(id.c_str()), 300.0f, 36.0f, &out, false, "the name players see", 64, 18.0f) &&
      !out.empty())
    Ask(id, "\"field\":\"name\",\"value\":" + Q(out));
  ImGui::SetCursorScreenPos(ImVec2(p.x + P(312.0f), p.y + P(5.0f)));
  static const char *const kSeg[] = {"Zerg", "Terran", "Protoss"};
  const int nNew = StudioSeg("##base", kSeg, 3, nBase, 26.0f);
  if (nNew != nBase && nNew >= 0)
    Ask(id, "\"field\":\"base\",\"value\":" + Q(kBases[nNew]));
  ImGui::SetCursorScreenPos(ImVec2(p.x + W - P(120.0f), p.y + P(5.0f)));
  if (StudioButton("##remove", "Take it away", "x", SB_PLAIN, 26.0f, 120.0f, kStudioFsXs))
    Ask(id, "\"remove\":true");
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(44.0f)));
  T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(46.0f), C(TK_INK2),
    "It plays as the " + std::string(nBase < 0 ? "?" : kBaseWords[nBase]) +
        " do: their way of building, supply, sounds, and their number in a replay's header; their console and music unless another's is picked below.",
    W);
  ImGui::Dummy(ImVec2(W, P(24.0f)));
  ImGui::SetCursorScreenPos(ImVec2(p.x, ImGui::GetCursorScreenPos().y));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_LABEL), "Its record's name");
  ImGui::SameLine(0.0f, P(8.0f));
  if (studio_races::TextField("##id", id + "#id", id, 180.0f, 24.0f, &out, false, nullptr, 48, kStudioFsXs) && !out.empty())
    StudioAsk("studio_race_set", "\"race\":" + Q(id) + ",\"rename\":" + Q(out));
  ImGui::SameLine(0.0f, P(12.0f));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3),
              ("--race " + (r["name"].Str(id.c_str()).find(' ') == std::string::npos ? r["name"].Str(id.c_str()) : "\"" + r["name"].Str(id.c_str()) + "\"") +
               " chooses it; so does the skirmish set-up").c_str());
  StartSection(d, id, r, nBase, W);
  LookSection(d, id, r, nBase, W);
  UnitsSection(d, id, r, nBase, W);
  WithoutSection(d, id, r, nBase, W);
  AiSection(d, id, r, nBase, W);
  StandInSection(d, id, W);
  // playing it on a real map (studio_racetest.cpp)
  StudioBuildRulesPanel(id, W);
  StudioRaceTestPanel(id, W);
  // what the loader would refuse, and what is missing
  Head(d, "Checks", nullptr, W);
  const std::vector<Problem> problems = Problems(id, r);
  if (problems.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_GOOD), "Nothing to fix: the loader takes it as it is.");
  for (const Problem &pr : problems) {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    const float cw = StudioChip(d, ImVec2(q.x, q.y + P(3.0f)), pr.bRefused ? "refused" : "warning", pr.bRefused ? CH_BAD : CH_WARN);
    ImGui::SetCursorScreenPos(ImVec2(q.x + cw + P(10.0f), q.y + P(4.0f)));
    Fine(pr.text, W - cw - P(14.0f));
    ImGui::Dummy(ImVec2(1, P(4.0f)));
  }
  Head(d, "Note", "for the modder; the loader skips it", W);
  if (studio_races::TextField("##comment", id + "#comment", r["comment"].Str(), std::min(W / StudioPx(1.0f), 640.0f), 54.0f, &out, true))
    Ask(id, "\"field\":\"comment\",\"value\":" + (out.empty() ? std::string("null") : Q(out)));
  Gap(20.0f);
}

void RacesPage(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float lw = P(250.0f);
  const PortJson &races = StudioDoc("types")["races"];
  if (g_race.empty() && races.size() > 0)
    g_race = races.members[0].first;
  // the list: the mod's races, then the classic ones every game has
  float y = a.y;
  Wide(d, 9.5f, a.x + P(4.0f), y + P(4.0f), C(TK_INK3), "The mod's races", 0.1f);
  y += P(24.0f);
  const float rh = P(40.0f);
  for (const PortJsonMember &m : races.members) {
    ImGui::PushID(m.first.c_str());
    ImGui::SetCursorScreenPos(ImVec2(a.x, y));
    if (ImGui::InvisibleButton("##race", ImVec2(lw, rh)))
      g_race = m.first;
    const bool bHover = ImGui::IsItemHovered(), bSel = g_race == m.first;
    if (bSel)
      d->AddRectFilled(ImVec2(a.x, y), ImVec2(a.x + lw, y + rh), C(TK_SEL));
    else if (bHover)
      d->AddRectFilled(ImVec2(a.x, y), ImVec2(a.x + lw, y + rh), C(TK_ROW));
    d->AddRectFilled(ImVec2(a.x, y + P(6.0f)), ImVec2(a.x + P(3.0f), y + rh - P(6.0f)), C(TK_MOD), P(1.5f));
    StudioGlyph(d, "race", ImVec2(a.x + P(10.0f), y + P(12.0f)), P(16.0f), C(TK_INK2));
    T(d, STUDIO_FONT_BOLD, kStudioFs, a.x + P(34.0f), y + P(4.0f), bSel || bHover ? C(TK_HI) : C(TK_INK), m.second["name"].Str(m.first.c_str()), lw - P(40.0f));
    const int nBase = BaseOf(m.second);
    std::string fine = (nBase < 0 ? std::string("no base") : std::string("plays as ") + kBaseWords[nBase]) + ", " +
                       Count(static_cast<long long>(m.second["units"].size()), "unit", "units");
    bool bRefused = false;
    for (const Problem &pr : Problems(m.first, m.second))
      bRefused = bRefused || pr.bRefused;
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(34.0f), y + P(22.0f), bRefused ? C(TK_BAD) : C(TK_INK3), bRefused ? fine + ", refused" : fine,
      lw - P(40.0f));
    ImGui::PopID();
    y += rh;
  }
  if (races.size() == 0) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(6.0f), y, C(TK_INK3), "None yet.");
    y += P(24.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(a.x, y + P(8.0f)));
  if (StudioButton("##new", "New race", "plus", SB_GOOD, 28.0f, 0.0f, kStudioFsS, StudioIsOpen())) {
    std::string id = "new_race";
    for (int n = 2; races[id.c_str()].kind != PortJson::NONE; ++n)
      id = "new_race_" + std::to_string(n);
    Ask(id, "\"value\":{\"name\":\"New race\",\"base\":\"protoss\"}");
    g_race = id;
  }
  y += P(52.0f);
  Wide(d, 9.5f, a.x + P(4.0f), y, C(TK_INK3), "Classic, in every game", 0.1f);
  y += P(22.0f);
  for (int i = 0; i < 3; ++i) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(10.0f), y, C(TK_INK2), kBaseWords[2 - i]);
    y += P(22.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(a.x, y + P(6.0f)));
  ImGui::PushTextWrapPos(a.x + lw - ImGui::GetWindowPos().x);
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsXs);
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK3));
  ImGui::TextWrapped("A race of the mod's plays as one of these: their buildings and rules, unless its page picks its own.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
  d->AddLine(ImVec2(a.x + lw + P(12.0f), a.y), ImVec2(a.x + lw + P(12.0f), b.y), C(TK_LINE));
  // the race's form
  const ImVec2 ra(a.x + lw + P(28.0f), a.y);
  ImGui::SetCursorScreenPos(ra);
  if (ImGui::BeginChild("##raceform", ImVec2(b.x - ra.x, b.y - ra.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    const float W = std::min(ImGui::GetContentRegionAvail().x - P(12.0f), P(980.0f));
    ImGui::BeginGroup();
    RaceForm(g_race, W);
    ImGui::EndGroup();
  }
  ImGui::EndChild();
}

// ---- the commands -----------------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::string RaceJson(const std::string &id, const PortJson &r) {
  std::string problems;
  for (const Problem &p : Problems(id, r))
    problems += (problems.empty() ? "" : ",") + std::string("{\"refused\":") + (p.bRefused ? "true" : "false") + ",\"text\":" + Q(p.text) + "}";
  return "{\"race\":" + Q(id) + ",\"record\":" + PortJsonWrite(r) + ",\"problems\":[" + problems + "]}";
}

// studio_races: the mod's races, each its record and what the loader would refuse or warn of.
bool CmdRaces(const PortJson &, std::string *pAnswer) {
  std::string list;
  for (const PortJsonMember &m : StudioDoc("types")["races"].members)
    list += (list.empty() ? "" : ",") + RaceJson(m.first, m.second);
  *pAnswer = "\"races\":[" + list + "]";
  return true;
}

bool Done(bool bOk, const std::string &id, const std::string &why, std::string *pAnswer) {
  if (!bOk) {
    *pAnswer = why;
    return false;
  }
  const PortJson &r = StudioDoc("types")["races"][id.c_str()];
  *pAnswer = "\"race\":" + (r.kind == PortJson::NONE ? std::string("null") : RaceJson(id, r)) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool NameOk(const std::string &s) {
  if (s.empty() || s.size() > 48)
    return false;
  for (char c : s)
    if (!(islower(static_cast<unsigned char>(c)) || isdigit(static_cast<unsigned char>(c)) || c == '_'))
      return false;
  return true;
}

// studio_race_set {race, value?, field?, rename?, remove?}: a race of the mod's set whole (value),
// one of its fields set (field and value; value null takes it away), its record renamed, or the
// race taken away (remove). A change the loader would refuse is refused, with why.
bool CmdRaceSet(const PortJson &j, std::string *pAnswer) {
  const std::string id = j["race"].Str(), sender = Sender(j);
  const PortJson &races = StudioDoc("types")["races"];
  const bool bThere = races[id.c_str()].kind != PortJson::NONE;
  std::string why;
  if (id.empty()) {
    *pAnswer = "studio_race_set needs race, its record's name";
    return false;
  }
  if (j["remove"].kind == PortJson::BOOL && j["remove"].b) {
    if (!bThere) {
      *pAnswer = "the mod has no race " + id;
      return false;
    }
    const std::string label = "took away races " + id;
    StudioGroupBegin(sender, label);
    bool bOk = StudioPut("types", {"races", id}, PortJson(), sender, label, &why);
    if (bOk && StudioDoc("types")["races"].size() == 0)
      bOk = StudioPut("types", {"races"}, PortJson(), sender, label, &why);
    StudioGroupEnd();
    if (bOk && g_race == id)
      g_race.clear();
    return Done(bOk, id, why, pAnswer);
  }
  if (j["rename"].kind == PortJson::STRING) {
    const std::string to = j["rename"].s;
    if (!bThere || !NameOk(to) || races[to.c_str()].kind != PortJson::NONE) {
      *pAnswer = !bThere ? "the mod has no race " + id
                         : !NameOk(to) ? std::string("a race's record name is lower case letters, digits and _")
                                       : "the mod has a race " + to + " already";
      return false;
    }
    const PortJson r = races[id.c_str()];
    const std::string label = "renamed races " + id + " to " + to;
    StudioGroupBegin(sender, label);
    const bool bOk = StudioPut("types", {"races", id}, PortJson(), sender, label, &why) &&
                     StudioPut("types", {"races", to}, r, sender, label, &why);
    StudioGroupEnd();
    if (bOk && g_race == id)
      g_race = to;
    return Done(bOk, to, why, pAnswer);
  }
  PortJson r;
  std::string label;
  if (j["field"].kind == PortJson::STRING) {
    if (!bThere) {
      *pAnswer = "the mod has no race " + id;
      return false;
    }
    r = races[id.c_str()];
    const std::string field = j["field"].s;
    static const char *const kFields[] = {"name", "base", "start", "units", "without", "ai", "console", "music", "comment"};
    bool bKnown = false;
    for (const char *psz : kFields)
      bKnown = bKnown || field == psz;
    if (!bKnown) {
      *pAnswer = "a race's fields are name, base, start, units, without, ai, console, music and comment";
      return false;
    }
    const PortJson &v = j["value"];
    if ((field == "console" || field == "music") && v.kind != PortJson::NONE && v.kind != PortJson::NULLV &&
        !(v.kind == PortJson::STRING && (v.s == "zerg" || v.s == "terran" || v.s == "protoss"))) {
      *pAnswer = "a race's " + field + " is zerg, terran or protoss, or null for its base's";
      return false;
    }
    if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV)
      StudioJsonErase(r, field);
    else
      StudioJsonPut(r, field, v);
    label = "set races " + id + " " + field;
  } else if (j["value"].kind == PortJson::OBJECT) {
    if (!NameOk(id)) {
      *pAnswer = "a race's record name is lower case letters, digits and _";
      return false;
    }
    r = j["value"];
    label = (bThere ? "set races " : "made races ") + id;
  } else {
    *pAnswer = "studio_race_set needs value (the race), field and value, rename, or remove";
    return false;
  }
  if (Refused(id, r, &why)) {
    *pAnswer = "races " + id + ": " + why;
    return false;
  }
  return Done(StudioPut("types", {"races", id}, r, sender, label, &why), id, why, pAnswer);
}

// studio_races_show {page?, race?, unit?}: the workspace shown at its races (a race picked), its
// cards (a unit's card picked) or its tech tree (page "tree", for race: zerg, terran, protoss or a
// race of the mod's).
bool CmdShow(const PortJson &j, std::string *pAnswer) {
  const bool bTree = j["page"].Str() == "tree";
  if (j["race"].kind == PortJson::STRING) {
    if (bTree)
      studio_races::TreeShow(j["race"].s);
    else
      g_race = j["race"].s;
    g_nPage = 0;
  }
  if (j["unit"].kind == PortJson::STRING) {
    studio_races::CardsShow(j["unit"].s);
    g_nPage = 1;
  }
  if (j["page"].Str() == "races")
    g_nPage = 0;
  else if (j["page"].Str() == "cards")
    g_nPage = 1;
  else if (bTree)
    g_nPage = 2;
  StudioWorkspaceSet(5);
  static const char *const kPage[] = {"races", "cards", "tree"};
  *pAnswer = "\"page\":" + Q(kPage[g_nPage]) + ",\"race\":" + Q(g_race);
  if (g_nPage == 2)
    *pAnswer += ",\"tree\":" + studio_races::TreeJson();
  return true;
}

}  // namespace

// ---- what studio_races_cards.cpp shares -------------------------------------------------------------

namespace studio_races {

std::string NameIn(int t, const PortJson &v) {
  if (v.kind == PortJson::STRING)
    return v.s;
  if (v.kind == PortJson::NUMBER)
    return StudioRefName(t, static_cast<long long>(v.d));
  return std::string();
}

int UnitRace(const std::string &unit) {
  StudioRecord r;
  if (unit.empty() || !StudioRecordOf(ST_UNITS, unit, &r))
    return -1;
  const long long g = StudioNumber(ST_UNITS, unit, "group_flags");
  return (g & 1) ? 0 : (g & 2) ? 1 : (g & 4) ? 2 : -1;
}

std::string RaceWord(int nRace) {
  return nRace >= 0 && nRace < 3 ? kBaseWords[nRace] : "Independent";
}

void ShowRace(const std::string &race) {
  g_race = race;
  g_nPage = 0;
}

// A record picked by name with a search: the mod's (its own and its dependencies'), the classic
// rows, or both; a race's first when given; only those pfnOk takes when given.
bool Pick(const char *pszId, int t, const std::string &now, float fW, std::string *pPicked, int nWhich, float fH, const char *pszEmpty,
          int nRace, bool (*pfnOk)(const std::string &)) {
  static char s_szFind[48];
  bool bPicked = false;
  const std::string show = now.empty() ? std::string(pszEmpty) : StudioShowName(t, now);
  if (StudioSelectBegin(pszId, show.c_str(), fW, fH)) {
    if (ImGui::IsWindowAppearing()) {
      s_szFind[0] = '\0';
      ImGui::SetKeyboardFocusHere();
    }
    StudioInput("##find", s_szFind, sizeof s_szFind, 300.0f, 24.0f, "Find", "search");
    ImGui::BeginChild("##list", ImVec2(P(300.0f), P(300.0f)));
    auto item = [&](const std::string &name, const std::string &label) {
      ImGui::PushID(name.c_str());
      if (StudioItem(label.c_str(), name == now)) {
        *pPicked = name;
        bPicked = true;
      }
      ImGui::PopID();
    };
    if (nWhich != PICK_CLASSIC)
      for (const StudioRecord &r : StudioRecords(t)) {
        if (r.kind != SK_OWN && r.kind != SK_DEPENDENCY)
          continue;
        const std::string s = StudioShowName(t, r.name) + "  (mod)";
        if ((pfnOk == nullptr || pfnOk(r.name)) && (Has(s, s_szFind) || Has(r.name, s_szFind)))
          item(r.name, s);
      }
    if (nWhich != PICK_MOD) {
      // the race's rows first, then the rest
      auto raceOf = [&](const char *psz) {
        if (t == ST_UNITS)
          return UnitRace(psz);
        const long long n = StudioNumber(t, psz, "race", -1);
        return n == 0 ? 0 : n == 1 ? 1 : n == 2 ? 2 : -1;
      };
      for (int pass = nRace >= 0 ? 0 : 1; pass < 2; ++pass) {
        if (nRace >= 0) {
          StudioLabel(STUDIO_FONT_WIDE, 9.0f, C(TK_INK3), pass == 0 ? RaceWord(nRace).c_str() : "The others", 0.08f, true);
        }
        for (int n = 0; n < StudioClassicRows(t); ++n) {
          const char *psz = StudioClassicName(t, n);
          if (!psz || (pfnOk && !pfnOk(psz)))
            continue;
          if (nRace >= 0 && (raceOf(psz) == nRace) != (pass == 0))
            continue;
          const std::string s = StudioShowName(t, psz);
          if (s_szFind[0] == '\0' || Has(s, s_szFind) || Has(psz, s_szFind))
            item(psz, s);
        }
      }
    }
    ImGui::EndChild();
    StudioSelectEnd();
  }
  return bPicked;
}

}  // namespace studio_races

void StudioRacesWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o(a.x + P(16.0f), a.y + P(12.0f));
  float x = o.x + Wide(d, 13.0f, o.x, o.y + P(6.0f), C(TK_HEAD), "Cards and races", 0.06f) + P(18.0f);
  static const char *const kPages[] = {"Races", "Cards", "Tech tree"};
  ImGui::SetCursorScreenPos(ImVec2(x, o.y));
  g_nPage = StudioSeg("##page", kPages, 3, g_nPage, 28.0f);
  x += StudioSegWidth(kPages, 3) + P(16.0f);
  static const char *const kAbout[] = {"the races a player can choose, beside the classic three",
                                       "every unit's command card: the classic buttons and the mod's",
                                       "what each building, unit, tech and upgrade of a race needs, in tiers"};
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, o.y + P(6.0f), C(TK_INK3), kAbout[g_nPage], b.x - x - P(16.0f));
  const float top = o.y + P(44.0f);
  d->AddLine(ImVec2(a.x, top - P(6.0f)), ImVec2(b.x, top - P(6.0f)), C(TK_LINE));
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, top + P(10.0f), C(TK_INK3), "Open a mod to see its races and cards.");
    return;
  }
  const ImVec2 pa(o.x, top + P(6.0f)), pb(b.x - P(16.0f), b.y - P(10.0f));
  if (g_nPage == 0)
    RacesPage(pa, pb);
  else if (g_nPage == 1)
    studio_races::CardsPage(pa, pb);
  else
    studio_races::TreePage(pa, pb);
}

void StudioRacesInit(void) {
  PortControlCommand("studio_races", CmdRaces);
  PortControlCommand("studio_race_set", CmdRaceSet);
  PortControlCommand("studio_races_show", CmdShow);
  studio_races::CardsInit();
  studio_races::TreeInit();
}
