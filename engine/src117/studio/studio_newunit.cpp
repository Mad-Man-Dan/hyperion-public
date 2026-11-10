// studio_newunit.cpp -- the New unit dialog (the design's mock, cs-unit.js openNewUnit): a unit started
// from a classic unit (its numbers copied, its art borrowed), a role (the race's classic unit of
// that kind) or blank (starting numbers, no weapon, the art borrowed until the modder adds some);
// then its name, race and where it is made, its own copy of the weapon, and a button that makes it
// on its maker's card. Before Make the dialog lists what will be made; Make runs studio_make_unit,
// which makes it all as one edit, undone together.
#include <algorithm>
#include <vector>
#include "../design/design_engine.h"  // DesignCall
#include "studio_draw.h"

namespace {

using namespace studio_draw;

enum { RACE_PROTOSS, RACE_TERRAN, RACE_ZERG };
const char *const kRaceWords[] = {"Protoss", "Terran", "Zerg"};
const char *const kRaceKeys[] = {"protoss", "terran", "zerg"};

struct Role {
  const char *pszLabel;
  const char *pszAbout;
  int anBase[3];  // the classic unit it starts from, by race
  const char *pszName;
};
const Role kRoles[] = {
    {"Melee ground", "fights up close", {65, 32, 37}, "Striker"},
    {"Ranged ground", "fires from a distance", {66, 0, 38}, "Lancer"},
    {"Flyer", "flies over everything", {70, 8, 43}, "Glider"},
    {"Caster", "energy and spells", {67, 1, 46}, "Seer"},
    {"Worker", "gathers and builds", {64, 7, 41}, "Drudge"},
    {"Building", "placed by a worker", {160, 111, 142}, "Spire"},
};
// where a race's units are made, and the worker that places its buildings
const int kMakers[3][4] = {{160, 167, 155, 154}, {111, 113, 114, 106}, {35, -1, -1, -1}};
const int kWorker[3] = {64, 7, 41};

struct State {
  bool bOpen = false;
  bool bAppearing = false;
  int nWay = 0;  // a classic unit, a role, blank
  std::string pick = "protoss_zealot";
  int nRole = 0;
  int nRace = RACE_PROTOSS;
  char szName[64] = "";
  char szFind[48] = "";
  int nAt = 0;  // a maker of the race, or the last for nowhere
  bool bCopyWeapon = true;
  bool bButton = true;
  int nPlace = 0;  // 0 for the first free
  char szKey[4] = "";
  bool bScrollPick = false;  // the list scrolled to the unit picked, once
} g;

std::string Classic(int n) {
  const char *psz = n >= 0 ? StudioClassicName(ST_UNITS, n) : nullptr;
  return psz ? psz : "";
}

int RaceOfUnit(const std::string &unit) {
  const long long f = StudioNumber(ST_UNITS, unit, "group_flags");
  return (f & 1) ? RACE_ZERG : (f & 2) ? RACE_TERRAN : RACE_PROTOSS;
}

bool Flag(const std::string &unit, const char *pszFlag) {
  const std::vector<std::string> &names = StudioFlags("units");
  const long long f = StudioNumber(ST_UNITS, unit, "flags");
  for (size_t i = 0; i < names.size(); ++i)
    if (names[i] == pszFlag)
      return ((f >> i) & 1) != 0;
  return false;
}

bool Building(const std::string &unit) {
  return Flag(unit, "building");
}

// What the unit starts from: the classic unit picked, the role's for the race, or blank's
// stand-in for its art (the race's melee unit).
std::string BaseOf(void) {
  if (g.nWay == 0)
    return g.pick;
  if (g.nWay == 1)
    return Classic(kRoles[g.nRole].anBase[g.nRace]);
  return Classic(kRoles[0].anBase[g.nRace]);
}

bool IsBuilding(void) {
  return g.nWay == 1 ? g.nRole == 5 : g.nWay == 0 && Building(g.pick);
}

std::string DefaultName(void) {
  if (g.nWay == 0)
    return "New " + Short(ST_UNITS, g.pick);
  if (g.nWay == 1)
    return std::string("New ") + kRoles[g.nRole].pszName;
  return "New Unit";
}

std::string NameNow(void) {
  return g.szName[0] ? std::string(g.szName) : DefaultName();
}

// "Hybrid Striker" -> hybrid_striker
std::string KeyOf(const std::string &name) {
  std::string k;
  for (char ch : name) {
    if (isalnum(static_cast<unsigned char>(ch)))
      k += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    else if (!k.empty() && k.back() != '_')
      k += '_';
  }
  while (!k.empty() && k.back() == '_')
    k.pop_back();
  if (k.empty() || isdigit(static_cast<unsigned char>(k[0])))
    k = "unit_" + k;
  return k;
}

// The makers on offer: the race's, or the worker for a building; the last choice is nowhere.
std::vector<std::string> Makers(void) {
  std::vector<std::string> out;
  if (IsBuilding()) {
    out.push_back(Classic(kWorker[g.nRace]));
  } else {
    for (int n : kMakers[g.nRace])
      if (!Classic(n).empty())
        out.push_back(Classic(n));
  }
  return out;
}

std::string MakerNow(void) {
  const std::vector<std::string> m = Makers();
  return g.nAt < static_cast<int>(m.size()) ? m[static_cast<size_t>(g.nAt)] : std::string();
}

// The mod's own buttons on a card: their places and hotkeys.
void ModButtons(const std::string &card, unsigned *puPlaces, std::string *pKeys) {
  *puPlaces = 0;
  pKeys->clear();
  for (const PortJson &b : StudioDoc("types")["cards"][card.c_str()].items) {
    const int n = static_cast<int>(b["position"].Num(0));
    if (n >= 1 && n <= 9)
      *puPlaces |= 1u << n;
    if (!b["hotkey"].Str().empty())
      *pKeys += static_cast<char>(tolower(static_cast<unsigned char>(b["hotkey"].Str()[0])));
  }
}

int PlaceNow(const std::string &card) {
  if (g.nPlace > 0)
    return g.nPlace;
  unsigned u = 0;
  std::string keys;
  ModButtons(card, &u, &keys);
  for (int n = 9; n >= 1; --n)  // the classic cards fill from the top: the last free place first
    if (!(u & (1u << n)))
      return n;
  return 9;
}

char KeyNow(const std::string &card) {
  if (g.szKey[0])
    return static_cast<char>(tolower(static_cast<unsigned char>(g.szKey[0])));
  unsigned u = 0;
  std::string keys;
  ModButtons(card, &u, &keys);
  for (char ch : NameNow() + "abcdefghijklmnopqrstuvwxyz") {
    const char lo = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    if (isalpha(static_cast<unsigned char>(lo)) && keys.find(lo) == std::string::npos)
      return lo;
  }
  return 'q';
}

// "Warp in Hybrid <S>triker": the button's words with its hotkey marked
std::string ButtonText(char key) {
  const char *pszVerb = g.nRace == RACE_TERRAN ? "Train " : g.nRace == RACE_ZERG ? "Morph to " : "Warp in ";
  const std::string name = NameNow();
  const std::string lower = Lower(name);
  const size_t i = lower.find(key);
  if (i == std::string::npos)
    return std::string(pszVerb) + "<" + static_cast<char>(toupper(static_cast<unsigned char>(key))) + ">" + name;
  return std::string(pszVerb) + name.substr(0, i) + "<" + name[i] + ">" + name.substr(i + 1);
}

struct Line {
  const char *pszGlyph;
  std::string text;
  int nTok;
};

std::vector<Line> Plan(void) {
  std::vector<Line> out;
  const std::string base = BaseOf(), key = KeyOf(NameNow()), at = MakerNow();
  const std::string baseShort = Short(ST_UNITS, base);
  if (g.nWay == 2)
    out.push_back({"unit", "A unit, " + key + ", with starting numbers of its own: 100 life, 100 minerals, no weapon.", TK_INK});
  else
    out.push_back({"unit", "A unit, " + key + ", based on the " + baseShort + ": its numbers taken from it, marked as inherited until you change them.", TK_INK});
  out.push_back({"sprite", "Its art, sounds and portrait borrowed from the " + baseShort + " until you make its own.", TK_INK2});
  const std::string w = g.nWay == 2 ? std::string() : StudioUnitWeapon(base, false);
  if (!w.empty())
    out.push_back({"weapon", g.bCopyWeapon ? "A weapon, " + key + "_weapon, copied from " + StudioShowName(ST_WEAPONS, w) +
                                                 ", so changing it changes only this unit."
                                           : "It fires the " + baseShort + "'s " + StudioShowName(ST_WEAPONS, w) + ", shared with it.",
                   TK_INK});
  if (!at.empty()) {
    out.push_back({"make", "Made at the " + Short(ST_UNITS, at) + ": its requirements start there.", TK_INK});
    if (g.bButton && !IsBuilding()) {
      const char k = KeyNow(at);
      out.push_back({"cards", "A button on the " + Short(ST_UNITS, at) + "'s card at place " + std::to_string(PlaceNow(at)) +
                                  ", hotkey " + static_cast<char>(toupper(static_cast<unsigned char>(k))) + ": " + ButtonText(k) + ".",
                     TK_INK});
      out.push_back({"note", "A classic button at that place shares it; the Card view shows the whole card.", TK_INK3});
    }
  } else {
    out.push_back({"needs", "Made nowhere yet: nothing trains it until its requirements say where.", TK_WARN});
  }
  if (StudioRecordJson(ST_UNITS, key).kind != PortJson::NONE || StudioClassicRow(ST_UNITS, key) >= 0)
    out.push_back({"needs", key + " is taken: give it another name.", TK_BAD});
  out.push_back({"note", "Still to do after: art, sounds, a portrait. Its page lists them.", TK_INK3});
  return out;
}

void Make(void) {
  const std::string base = BaseOf(), name = NameNow(), key = KeyOf(name), at = MakerNow();
  std::string f = "\"name\":" + Q(key) + ",\"label\":" + Q(name) + ",\"base\":" + Q(base) + ",\"race\":" + Q(kRaceKeys[g.nRace]);
  if (!at.empty())
    f += ",\"at\":" + Q(at);
  if (g.nWay == 2)
    f += ",\"values\":{\"hit_points\":100,\"shields\":0,\"armor\":0,\"size\":2,\"minerals\":100,\"gas\":0,\"build_time\":480,"
         "\"supply_required\":2,\"sight_range\":7,\"ground_weapon\":" +
         std::to_string(StudioClassicRows(ST_WEAPONS)) + ",\"air_weapon\":" + std::to_string(StudioClassicRows(ST_WEAPONS)) + "}";
  else if (g.bCopyWeapon)
    f += ",\"copy_weapon\":true";
  if (!at.empty() && g.bButton && !IsBuilding()) {
    const char k = KeyNow(at);
    f += ",\"button\":{\"card\":" + Q(at) + ",\"position\":" + std::to_string(PlaceNow(at)) + ",\"hotkey\":" + Q(std::string(1, k)) +
         ",\"text\":" + Q(ButtonText(k)) + "}";
  }
  DesignCall("studio_make_unit", f + ",\"sender\":\"you\"", [key, name](const PortJson &a) {
    if (a["ok"].kind == PortJson::BOOL && a["ok"].b) {
      StudioOpenDoc(ST_UNITS, key, 0);
      StudioSay("made the " + name + "; its page lists what it still needs");
      StudioLog("info", "made units " + key);
    } else {
      StudioSay(a["error"].Str("refused"));
      StudioLog("error", "studio_make_unit: " + a["error"].Str("refused"));
    }
  });
}

// One of the three ways to start, as a card that is chosen.
bool Way(const char *pszId, ImVec2 p, float w, float h, const char *pszLabel, const char *pszAbout, bool bOn) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImGui::SetCursorScreenPos(p);
  const bool b = ImGui::InvisibleButton(pszId, ImVec2(w, h));
  const bool bHover = ImGui::IsItemHovered();
  d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bOn ? C(TK_SEL) : C(TK_PANE2), P(3.0f));
  d->AddRect(p, ImVec2(p.x + w, p.y + h), bOn ? C(TK_SEL_LINE) : bHover ? C(TK_BTN_RIM_HI) : C(TK_LINE), P(3.0f), 0, bOn ? P(1.5f) : P(1.0f));
  T(d, STUDIO_FONT_BOLD, 14.0f, p.x + P(12.0f), p.y + P(9.0f), bOn ? C(TK_HI) : C(TK_INK), pszLabel, w - P(24.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(12.0f), p.y + P(30.0f), C(TK_INK2), pszAbout, w - P(24.0f));
  return b;
}

void Label(const char *psz) {
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), psz, 0.1f, true);
}

}  // namespace

void StudioNewUnitOpen(const std::string &base) {
  g.bOpen = true;
  g.bAppearing = true;
  g.szName[0] = g.szFind[0] = g.szKey[0] = '\0';
  g.nAt = 0;
  g.nPlace = 0;
  g.bCopyWeapon = g.bButton = true;
  if (!base.empty() && StudioClassicRow(ST_UNITS, base) >= 0) {
    g.nWay = 0;
    g.pick = base;
    g.nRace = RaceOfUnit(base);
  }
  g.bScrollPick = true;
}

void StudioNewUnitDraw(void) {
  if (!g.bOpen)
    return;
  if (g.bAppearing) {
    ImGui::OpenPopup("##newunit");
    g.bAppearing = false;
  }
  const ImGuiViewport *v = ImGui::GetMainViewport();
  const float W = std::min(P(860.0f), v->Size.x - P(80.0f));
  ImGui::SetNextWindowPos(ImVec2(v->Pos.x + v->Size.x / 2.0f, v->Pos.y + v->Size.y / 2.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(W, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(20.0f), P(18.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(P(8.0f), P(6.0f)));
  const bool bShown = ImGui::BeginPopupModal("##newunit", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar(2);
  if (!bShown) {
    g.bOpen = false;
    return;
  }
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float inner = W - P(40.0f);
  StudioLabel(STUDIO_FONT_WIDE, 14.0f, C(TK_HEAD), "New unit", 0.06f, true);
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  Label("Start from");
  // the three ways
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float ww = (inner - P(16.0f)) / 3.0f, wh = P(52.0f);
    static const char *const kWays[][2] = {{"A classic unit", "its numbers copied, its art borrowed"},
                                           {"A role", "melee, ranged, flyer, caster, worker, building"},
                                           {"Blank", "starting numbers, no weapon; art borrowed for now"}};
    for (int i = 0; i < 3; ++i) {
      ImGui::PushID(i);
      if (Way("##way", ImVec2(p.x + i * (ww + P(8.0f)), p.y), ww, wh, kWays[i][0], kWays[i][1], g.nWay == i)) {
        g.nWay = i;
        g.nAt = 0;
      }
      ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + wh + P(10.0f)));
  }
  // what it starts from
  if (g.nWay == 0) {
    StudioInput("##find", g.szFind, sizeof g.szFind, inner / StudioPx(1.0f), 26.0f, "Find a classic unit", "search");
    ImGui::BeginChild("##classic", ImVec2(inner, P(200.0f)), ImGuiChildFlags_Borders);
    const float cw = (inner - P(24.0f)) / 3.0f;
    for (int r = 0; r < 3; ++r) {
      ImGui::SetCursorPos(ImVec2(P(6.0f) + r * cw, P(6.0f)));
      ImGui::BeginGroup();
      StudioLabel(STUDIO_FONT_WIDE, 9.0f, C(TK_LABEL), kRaceWords[r], 0.1f, true);
      // the race's units that are made for a cost, by name, its buildings after
      std::vector<std::pair<std::string, std::string>> list[2];
      for (int n = 0; n < StudioClassicRows(ST_UNITS); ++n) {
        const std::string u = Classic(n);
        const long long nGroup = StudioNumber(ST_UNITS, u, "group_flags");
        if (u.empty() || (nGroup & 7) == 0 || RaceOfUnit(u) != r || (nGroup & 64) || Flag(u, "hero") || Flag(u, "subunit") ||
            StudioNumber(ST_UNITS, u, "minerals") + StudioNumber(ST_UNITS, u, "gas") == 0 || u.find("_egg") != std::string::npos ||
            u.find("_larva") != std::string::npos || u.find("_cocoon") != std::string::npos)
          continue;
        const std::string show = Short(ST_UNITS, u);
        bool bSeen = false;  // a campaign's copy of a unit has its name: the first row stands for both
        for (const auto &l : list)
          for (const auto &e : l)
            bSeen = bSeen || e.first == show;
        if (Has(show, g.szFind) && !bSeen)
          list[Building(u) ? 1 : 0].push_back({show, u});
      }
      for (auto &l : list) {
        std::sort(l.begin(), l.end());
        for (const auto &e : l) {
          ImGui::PushID(e.second.c_str());
          if (StudioItem(e.first.c_str(), g.pick == e.second)) {
            g.pick = e.second;
            g.nRace = r;
            g.nAt = 0;
          }
          if (g.pick == e.second && g.bScrollPick)
            ImGui::SetScrollHereY(0.3f);
          ImGui::PopID();
        }
      }
      ImGui::EndGroup();
    }
    ImGui::EndChild();
    g.bScrollPick = false;
  } else if (g.nWay == 1) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float rw = (inner - P(16.0f)) / 3.0f, rh = P(50.0f);
    for (int i = 0; i < 6; ++i) {
      ImGui::PushID(100 + i);
      const std::string from = std::string(kRoles[i].pszAbout) + "; from the " + Short(ST_UNITS, Classic(kRoles[i].anBase[g.nRace]));
      if (Way("##role", ImVec2(p.x + (i % 3) * (rw + P(8.0f)), p.y + (i / 3) * (rh + P(8.0f))), rw, rh, kRoles[i].pszLabel,
              from.c_str(), g.nRole == i)) {
        g.nRole = i;
        g.nAt = 0;
      }
      ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 2 * (rh + P(8.0f)) + P(2.0f)));
  }
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  // its name, race and maker on one line
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(p);
    Label("Its name");
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    StudioInput("##name", g.szName, sizeof g.szName, 300.0f, 30.0f, DefaultName().c_str());
    T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x, ImGui::GetCursorScreenPos().y, C(TK_INK3), "in the files: " + KeyOf(NameNow()));
    const float x2 = p.x + P(320.0f), x3 = p.x + P(320.0f + 170.0f);
    ImGui::SetCursorScreenPos(ImVec2(x2, p.y));
    ImGui::BeginGroup();
    Label("Race");
    if (StudioSelectBegin("##race", kRaceWords[g.nRace], 150.0f, 30.0f)) {
      for (int r = 0; r < 3; ++r)
        if (StudioItem(kRaceWords[r], g.nRace == r)) {
          g.nRace = r;
          g.nAt = 0;
        }
      StudioSelectEnd();
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(x3, p.y));
    ImGui::BeginGroup();
    Label(IsBuilding() ? "Placed by" : "Made at");
    const std::vector<std::string> makers = Makers();
    const std::string now = g.nAt < static_cast<int>(makers.size()) ? "the " + Short(ST_UNITS, makers[static_cast<size_t>(g.nAt)]) : "nowhere yet";
    if (StudioSelectBegin("##at", now.c_str(), 200.0f, 30.0f)) {
      for (size_t i = 0; i < makers.size(); ++i)
        if (StudioItem(("the " + Short(ST_UNITS, makers[i])).c_str(), g.nAt == static_cast<int>(i)))
          g.nAt = static_cast<int>(i);
      if (StudioItem("nowhere yet", g.nAt >= static_cast<int>(makers.size())))
        g.nAt = static_cast<int>(makers.size());
      StudioSelectEnd();
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(78.0f)));
  }
  // the options
  if (g.nWay != 2 && !StudioUnitWeapon(BaseOf(), false).empty()) {
    StudioCheck("##copyw", "Copy its weapon too, so a change to it changes only this unit", &g.bCopyWeapon);
  }
  if (!MakerNow().empty() && !IsBuilding()) {
    StudioCheck("##button", "Put its button on the maker's card", &g.bButton);
    if (g.bButton) {
      ImGui::SameLine(0.0f, P(18.0f));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "place");
      ImGui::SameLine(0.0f, P(6.0f));
      static const char *const kPlaces[] = {"auto", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
      g.nPlace = StudioSeg("##place", kPlaces, 10, g.nPlace, 24.0f, kStudioFsXs);
      ImGui::SameLine(0.0f, P(14.0f));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "hotkey");
      ImGui::SameLine(0.0f, P(6.0f));
      StudioInput("##key", g.szKey, 2, 40.0f, 24.0f, std::string(1, static_cast<char>(toupper(static_cast<unsigned char>(KeyNow(MakerNow()))))).c_str());
    }
  }
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  // what will be made
  Label("What Make makes");
  {
    const std::vector<Line> plan = Plan();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float y = p.y + P(6.0f);
    const float lineH = P(24.0f);
    const float h = plan.size() * lineH + P(12.0f);
    d->AddRectFilled(p, ImVec2(p.x + inner, p.y + h), C(TK_WELL), P(3.0f));
    d->AddRect(p, ImVec2(p.x + inner, p.y + h), C(TK_LINE), P(3.0f));
    for (const Line &l : plan) {
      StudioGlyph(d, l.pszGlyph, ImVec2(p.x + P(10.0f), y + P(4.0f)), P(14.0f), C(l.nTok == TK_INK ? TK_LABEL : l.nTok));
      T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(32.0f), Mid(y, lineH, kStudioFsS), C(l.nTok), l.text, inner - P(44.0f));
      y += lineH;
    }
    ImGui::Dummy(ImVec2(inner, h + P(6.0f)));
  }
  // make, or leave
  const std::string key = KeyOf(NameNow());
  const bool bFree = StudioRecordJson(ST_UNITS, key).kind == PortJson::NONE && StudioClassicRow(ST_UNITS, key) < 0;
  const bool bMake = StudioButton("##make", ("Make the " + NameNow()).c_str(), "plus", SB_GOOD, 30.0f, 0.0f, kStudioFsS, bFree && StudioIsOpen());
  ImGui::SameLine(0.0f, P(8.0f));
  const bool bCancel = StudioButton("##cancel", "Cancel", nullptr, SB_PLAIN, 30.0f);
  if ((bMake || (ImGui::IsKeyPressed(ImGuiKey_Enter) && bFree && !ImGui::IsAnyItemActive())) && StudioIsOpen()) {
    Make();
    g.bOpen = false;
    ImGui::CloseCurrentPopup();
  } else if (bCancel || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    g.bOpen = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}
