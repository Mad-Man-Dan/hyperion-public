// studio_races_tree.cpp -- the Cards and races workspace's tech tree: a race's buildings, units,
// techs and upgrades in tiers by what each needs, read from the classic trees (TechTree.cpp's and
// Allowed.cpp's) and a mod's "requires" (docs/mod-data.md), with a line from each thing needed
// and from where it is made. The mod's records are marked and a link one misses is flagged: a unit
// nothing makes, a tech or an upgrade researched nowhere, a need the race is without. The page
// only reads; a node opens its record.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "platform.h"  // OC_UNALIGNED
#include "imgui_internal.h"
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_races.h"

// The classic trees (TechTree.cpp, Allowed.cpp): runs of [id, its requirements..., 0xFFFF] from
// index 1, ended by 0xFFFF where an id would be.
extern WORD sgTree[546] OC_UNALIGNED;
extern WORD sgUpgradeTree[420] OC_UNALIGNED;
extern WORD sgResearchTree[160];
// Who may use each classic tech (Allowed.cpp): its casters and that it is researched.
extern WORD sgTechUseTree[344];
// The loaded stat_txt.tbl (status.cpp): the classic names come with it.
extern void *sgpStatStrs;

namespace {

using namespace studio_draw;
using namespace studio_races;

// ---- what a record needs ----------------------------------------------------------------------------

enum { NEED_AT, NEED_UNIT, NEED_ADDON, NEED_TECH };

struct Need {
  int kind = NEED_UNIT;
  int t = ST_UNITS;
  std::string name;
  int nGroup = 0;  // needs of one group are alternatives; every group is needed
};

// A classic tree's run for an id: the last one, as the game's index keeps the last.
bool ClassicNeeds(const WORD *pTree, int nLen, int nId, std::vector<Need> *pOut) {
  int nStart = -1, nEnd = -1;
  for (int i = 1; i < nLen && pTree[i] != 0xFFFF;) {
    const int id = pTree[i], s = i + 1;
    while (i < nLen && pTree[i] != 0xFFFF)
      ++i;
    if (id == nId) {
      nStart = s;
      nEnd = i;
    }
    ++i;
  }
  if (nStart < 0)
    return false;
  int g = 0;
  bool bOr = false;
  for (int k = nStart; k < nEnd;) {
    const WORD op = pTree[k++];
    if (op == 0xFF01) {
      bOr = true;
      continue;
    }
    if (!bOr)
      ++g;
    bOr = false;
    Need n;
    n.nGroup = g;
    if (op == 0xFF02 || op == 0xFF03 || op == 0xFF04 || op == 0xFF25) {
      if (k >= nEnd)
        break;
      const WORD x = pTree[k++];
      n.kind = op == 0xFF02 ? NEED_AT : op == 0xFF04 ? NEED_ADDON : op == 0xFF25 ? NEED_TECH : NEED_UNIT;
      n.t = op == 0xFF25 ? ST_TECHS : ST_UNITS;
      n.name = StudioRefName(n.t, x);
      pOut->push_back(n);
    } else if (op == 0xFF20 || op == 0xFF21) {
      break;  // an upgrade's later levels: the first level's needs place it
    } else if (op < 0xFF00) {
      n.name = StudioRefName(ST_UNITS, op);
      pOut->push_back(n);
    }
    // the other opcodes check the unit or the game (an expansion game, a unit not busy): no link
  }
  return true;
}

void JsonNeed(const PortJson &v, int g, std::vector<Need> *pOut) {
  Need n;
  n.nGroup = g;
  if (v.kind == PortJson::OBJECT) {
    if (v["at"].kind != PortJson::NONE) {
      n.kind = NEED_AT;
      n.name = NameIn(ST_UNITS, v["at"]);
    } else if (v["tech"].kind != PortJson::NONE) {
      n.kind = NEED_TECH;
      n.t = ST_TECHS;
      n.name = NameIn(ST_TECHS, v["tech"]);
    } else {
      return;
    }
  } else if (v.kind == PortJson::STRING || v.kind == PortJson::NUMBER) {
    if (v.Str() == "broodwar" || v.Str() == "researched")
      return;
    n.name = NameIn(ST_UNITS, v);
  } else {
    return;
  }
  if (!n.name.empty())
    pOut->push_back(n);
}

void JsonNeeds(const PortJson &req, std::vector<Need> *pOut) {
  for (size_t i = 0; i < req.size(); ++i) {
    const PortJson &v = req.At(i);
    if (v.kind == PortJson::ARRAY)
      for (size_t k = 0; k < v.size(); ++k)
        JsonNeed(v.At(k), static_cast<int>(i) + 1, pOut);
    else
      JsonNeed(v, static_cast<int>(i) + 1, pOut);
  }
}

// ---- the tree ---------------------------------------------------------------------------------------

struct Node {
  int t = ST_UNITS;
  std::string name;     // its name in the tables ("terran_barracks"); a classic row's when an edit changes it
  std::string open;     // the record a click opens: the edit's own name for an edited row
  bool bMod = false;    // a record of the mod's (its own or a dependency's)
  bool bEdit = false;   // a classic row the mod's edit gives other needs
  bool bBuilding = false;
  bool bWorker = false;
  std::vector<Need> needs;
  std::vector<std::string> builtBy;  // the workers it is made at, which the tiers leave out
  std::string missing;  // the link it misses, in words
  std::string fixUnit;  // the unit the race goes without that it misses, when the race has a stand-in for it
  std::string fixBy;    // that stand-in
  int nTier = 0;
  float fRow = 0.0f;    // its place down its tier
  float fCol = 0.0f;    // its column: a tall tier takes more than one
};

struct Tree {
  std::string race;     // "zerg", "terran", "protoss" or a race of the mod's
  int nBase = 2;
  std::vector<Node> nodes;
  std::map<std::string, int> at;  // "t:name" to its node
  int nMissing = 0;
  int nTiers = 0;
  int nRows = 0;
  int nCols = 0;
  std::vector<int> tierCol;  // each tier's first column
};

bool IsRecord(int t, const std::string &name) {
  StudioRecord r;
  return !name.empty() && StudioRecordOf(t, name, &r);
}

std::string Key(int t, const std::string &name) {
  return std::to_string(t) + ":" + name;
}

const char *const kBaseKeys[] = {"zerg", "terran", "protoss"};

// The mod's races, its own and its dependencies': id and record.
std::vector<std::pair<std::string, const PortJson *>> ModRaces(void) {
  std::vector<std::pair<std::string, const PortJson *>> out;
  std::set<std::string> seen;
  auto add = [&](const PortJson &races) {
    for (const PortJsonMember &m : races.members)
      if (m.second.kind == PortJson::OBJECT && !seen.count(m.first)) {
        seen.insert(m.first);
        out.push_back({m.first, &m.second});
      }
  };
  add(StudioDoc("types")["races"]);
  for (int i = 0; i < StudioDependencies(); ++i)
    add(StudioDependencyTypes(i)["races"]);
  return out;
}

int BaseOf(const PortJson &race) {
  const std::string b = Lower(race["base"].Str());
  for (int i = 0; i < 3; ++i)
    if (b == kBaseKeys[i])
      return i;
  return 2;
}

// A record's needs: its own "requires" for a mod's record and an edit that gives one, else the
// classic tree's run. False when it has none at all (a unit trained nowhere).
bool NeedsOf(int t, const std::string &name, bool bMod, Node *pNode) {
  if (bMod) {
    StudioValue v;
    if (StudioGet(t, name, "requires", &v) && v.own.kind == PortJson::ARRAY) {
      JsonNeeds(v.own, &pNode->needs);
      return true;
    }
    return false;
  }
  const int nRow = StudioClassicRow(t, name);
  for (const StudioRecord &r : StudioRecords(t))
    if (r.kind == SK_EDIT && r.nId == nRow && StudioRecordJson(t, r.name)["requires"].kind == PortJson::ARRAY) {
      JsonNeeds(StudioRecordJson(t, r.name)["requires"], &pNode->needs);
      pNode->bEdit = true;
      pNode->open = r.name;
      return true;
    }
  if (t == ST_UNITS)
    return ClassicNeeds(sgTree, 546, nRow, &pNode->needs);
  if (t == ST_TECHS)
    return ClassicNeeds(sgResearchTree, 160, nRow, &pNode->needs);
  return ClassicNeeds(sgUpgradeTree, 420, nRow, &pNode->needs);
}

long long UnitFlags(const std::string &u) {
  return StudioNumber(ST_UNITS, u, "flags");
}

Tree Build(const std::string &race) {
  Tree tr;
  tr.race = race;
  const PortJson *pRace = nullptr;
  tr.nBase = -1;
  for (int i = 0; i < 3; ++i)
    if (race == kBaseKeys[i])
      tr.nBase = i;
  // the units each race of the mod's names as its own, which no other race's players have
  std::map<std::string, std::string> owner;
  for (const auto &r : ModRaces()) {
    if (r.first == race) {
      pRace = r.second;
      tr.nBase = BaseOf(*r.second);
    }
    for (size_t i = 0; i < (*r.second)["units"].size(); ++i)
      owner[NameIn(ST_UNITS, (*r.second)["units"].At(i))] = r.first;
  }
  if (tr.nBase < 0)
    tr.nBase = 2;
  std::set<std::string> without;
  if (pRace)
    for (size_t i = 0; i < (*pRace)["without"].size(); ++i)
      without.insert(NameIn(ST_UNITS, (*pRace)["without"].At(i)));

  auto add = [&](int t, const std::string &name, bool bMod) -> Node * {
    const std::string k = Key(t, name);
    if (name.empty() || tr.at.count(k))
      return nullptr;
    Node n;
    n.t = t;
    n.name = name;
    n.open = name;
    n.bMod = bMod;
    const bool bHas = NeedsOf(t, name, bMod, &n);
    if (t == ST_UNITS) {
      const long long f = UnitFlags(name);
      n.bBuilding = (f & 1) != 0;
      n.bWorker = (f & 8) != 0;
    }
    if (!bHas && !bMod && t != ST_UNITS)
      return nullptr;  // a classic tech or upgrade no tree researches
    tr.at[k] = static_cast<int>(tr.nodes.size());
    tr.nodes.push_back(n);
    return &tr.nodes.back();
  };

  // units: the classic ones of the race with a tree, the mod's of its race its players have
  for (int id = 0; id < StudioClassicRows(ST_UNITS); ++id) {
    const char *psz = StudioClassicName(ST_UNITS, id);
    if (!psz || without.count(psz) || UnitRace(psz) != tr.nBase || (UnitFlags(psz) & 0x40))
      continue;
    std::vector<Need> probe;
    if (ClassicNeeds(sgTree, 546, id, &probe))
      add(ST_UNITS, psz, false);
  }
  for (const StudioRecord &r : StudioRecords(ST_UNITS)) {
    if (r.kind != SK_OWN && r.kind != SK_DEPENDENCY)
      continue;
    const auto o = owner.find(r.name);
    const bool bTheirs = o != owner.end() ? o->second == race : UnitRace(r.name) == tr.nBase;
    if (bTheirs && !without.count(r.name))
      add(ST_UNITS, r.name, true);
  }
  // what the race's units are made at and need that it has but no tree makes (the Larva)
  for (size_t i = 0; i < tr.nodes.size(); ++i) {
    const std::vector<Need> needs = tr.nodes[i].needs;  // a copy: adding a node moves the nodes
    for (const Need &n : needs)
      if (n.t == ST_UNITS && !without.count(n.name) && UnitRace(n.name) == tr.nBase && StudioClassicRow(ST_UNITS, n.name) >= 0 &&
          !(UnitFlags(n.name) & 0x40) && !tr.at.count(Key(ST_UNITS, n.name)))
        add(ST_UNITS, n.name, false);
  }
  // techs and upgrades: researched at one of the race's units, or the mod's of its race with no place
  for (int t : {ST_TECHS, ST_UPGRADES}) {
    std::vector<std::pair<std::string, bool>> all;
    for (int id = 0; id < StudioClassicRows(t); ++id)
      if (const char *psz = StudioClassicName(t, id))
        all.push_back({psz, false});
    for (const StudioRecord &r : StudioRecords(t))
      if ((r.kind == SK_OWN || r.kind == SK_DEPENDENCY) && StudioRecordJson(t, r.name)["requires"].kind != PortJson::NONE)
        all.push_back({r.name, true});
    for (const auto &a : all) {
      Node probe;
      if (!NeedsOf(t, a.first, a.second, &probe) && !a.second)
        continue;
      bool bHere = false, bAt = false;
      for (const Need &n : probe.needs)
        if (n.kind == NEED_AT) {
          bAt = true;
          bHere = bHere || tr.at.count(Key(ST_UNITS, n.name)) != 0;
        }
      if (!bAt && a.second)
        bHere = StudioNumber(t, a.first, "race", -1) == tr.nBase;
      if (bHere)
        add(t, a.first, a.second);
    }
  }

  // the workers a node is built by, which the tiers and lines leave out: every building has one
  for (Node &n : tr.nodes)
    for (const Need &d : n.needs)
      if (d.kind == NEED_AT && tr.at.count(Key(ST_UNITS, d.name)) && tr.nodes[static_cast<size_t>(tr.at[Key(ST_UNITS, d.name)])].bWorker)
        n.builtBy.push_back(d.name);

  // the links the records miss: a group of needs none of whose alternatives the race has, as a
  // Pylon's Probe for a race without it; the stand-in its "ai" names for it, when it has one
  std::map<std::string, std::string> standIn;
  if (pRace)
    for (const PortJsonMember &m : (*pRace)["ai"].members)
      if (without.count(m.first) && IsRecord(ST_UNITS, m.second.Str()))
        standIn[m.first] = m.second.Str();
  for (Node &n : tr.nodes) {
    std::map<int, std::vector<const Need *>> groups;
    for (const Need &d : n.needs)
      groups[d.nGroup].push_back(&d);
    for (const auto &g : groups) {
      bool bMet = false;
      std::string why, fix;
      for (const Need *pd : g.second) {
        StudioRecord r;
        if (pd->name.empty() || tr.at.count(Key(pd->t, pd->name))) {
          bMet = true;
        } else if (!StudioRecordOf(pd->t, pd->name, &r)) {
          why = "needs " + pd->name + ", which is no record";
        } else if (without.count(pd->name)) {
          why = "needs the " + StudioShowName(pd->t, pd->name) + ", which this race is without";
          if (standIn.count(pd->name))
            fix = pd->name;
        } else {
          bMet = true;  // a unit of the game the tree does not draw (a hero)
        }
      }
      if (!bMet && !why.empty() && n.missing.empty()) {
        n.missing = why;
        n.fixUnit = fix;
        n.fixBy = fix.empty() ? std::string() : standIn[fix];
      }
    }
    if (!n.bMod || !n.missing.empty())
      continue;
    bool bAt = false;
    for (const Need &d : n.needs)
      bAt = bAt || d.kind == NEED_AT;
    const char *pszWhat = n.t == ST_UNITS ? "make" : n.t == ST_TECHS ? "research" : "upgrade";
    if (!bAt)
      n.missing = n.t == ST_UNITS ? "nothing makes it: its Needs name no place it is made at"
                                  : "researched nowhere: its Needs name no place it is researched at";
    else if (ButtonsFor(pszWhat, n.name).empty())
      n.missing = n.t == ST_UNITS ? "nothing makes it: no card has a button for it" : "researched nowhere: no card has a button for it";
  }
  for (const Node &n : tr.nodes)
    tr.nMissing += n.missing.empty() ? 0 : 1;

  // tiers: one past the deepest thing it needs, the workers left out (a Command Center is made at
  // an SCV made at a Command Center), and a loop of needs broken where it closes
  std::vector<int> state(tr.nodes.size(), 0);
  std::vector<int> tier(tr.nodes.size(), 0);
  std::function<int(int)> depth = [&](int i) -> int {
    if (state[static_cast<size_t>(i)] == 2)
      return tier[static_cast<size_t>(i)];
    if (state[static_cast<size_t>(i)] == 1)
      return -1;
    state[static_cast<size_t>(i)] = 1;
    int best = 0;
    const Node &n = tr.nodes[static_cast<size_t>(i)];
    for (const Need &d : n.needs) {
      const auto it = tr.at.find(Key(d.t, d.name));
      if (it == tr.at.end() || it->second == i)
        continue;
      if (d.kind == NEED_AT && tr.nodes[static_cast<size_t>(it->second)].bWorker)
        continue;
      const int k = depth(it->second);
      if (k >= 0)
        best = std::max(best, k + 1);
    }
    state[static_cast<size_t>(i)] = 2;
    tier[static_cast<size_t>(i)] = best;
    return best;
  };
  for (size_t i = 0; i < tr.nodes.size(); ++i)
    tr.nodes[i].nTier = depth(static_cast<int>(i));

  // rows: buildings, units, techs, upgrades down each tier, then put near what they need
  std::map<int, std::vector<int>> cols;
  for (size_t i = 0; i < tr.nodes.size(); ++i)
    cols[tr.nodes[i].nTier].push_back(static_cast<int>(i));
  auto rank = [&](const Node &n) {
    return n.t == ST_UNITS ? (n.bBuilding ? 0 : 1) : n.t == ST_TECHS ? 2 : 3;
  };
  for (auto &c : cols) {
    std::stable_sort(c.second.begin(), c.second.end(), [&](int a, int b) {
      const Node &x = tr.nodes[static_cast<size_t>(a)], &y = tr.nodes[static_cast<size_t>(b)];
      if (rank(x) != rank(y))
        return rank(x) < rank(y);
      return x.name < y.name;
    });
    for (size_t k = 0; k < c.second.size(); ++k)
      tr.nodes[static_cast<size_t>(c.second[k])].fRow = static_cast<float>(k);
  }
  for (int pass = 0; pass < 2; ++pass)
    for (auto &c : cols) {
      if (c.first == 0)
        continue;
      std::vector<std::pair<float, int>> order;
      for (int i : c.second) {
        const Node &n = tr.nodes[static_cast<size_t>(i)];
        float sum = 0.0f;
        int cnt = 0;
        for (const Need &d : n.needs) {
          const auto it = tr.at.find(Key(d.t, d.name));
          if (it != tr.at.end() && tr.nodes[static_cast<size_t>(it->second)].nTier < n.nTier) {
            sum += tr.nodes[static_cast<size_t>(it->second)].fRow;
            ++cnt;
          }
        }
        order.push_back({cnt ? sum / static_cast<float>(cnt) : n.fRow, i});
      }
      std::stable_sort(order.begin(), order.end(), [](const std::pair<float, int> &a, const std::pair<float, int> &b) {
        return a.first < b.first;
      });
      for (size_t k = 0; k < order.size(); ++k)
        tr.nodes[static_cast<size_t>(order[k].second)].fRow = static_cast<float>(k);
    }
  // a tier taller than a dozen rows goes on in the next column, so the tree stays wide rather than tall
  const int kMaxRows = 12;
  for (const auto &c : cols)
    tr.nTiers = std::max(tr.nTiers, c.first + 1);
  int col = 0;
  for (int k = 0; k < tr.nTiers; ++k) {
    tr.tierCol.push_back(col);
    const int n = cols.count(k) ? static_cast<int>(cols[k].size()) : 0;
    col += std::max(1, (n + kMaxRows - 1) / kMaxRows);
  }
  tr.nCols = col;
  for (Node &n : tr.nodes) {
    const int r = static_cast<int>(n.fRow);
    n.fCol = static_cast<float>(tr.tierCol[static_cast<size_t>(n.nTier)] + r / kMaxRows);
    n.fRow = static_cast<float>(r % kMaxRows);
    tr.nRows = std::max(tr.nRows, static_cast<int>(n.fRow) + 1);
  }
  return tr;
}

// ---- the page's state -------------------------------------------------------------------------------

std::string g_treeRace = "protoss";
ImVec2 g_pan(0.0f, 0.0f);
float g_fZoom = 1.0f;
bool g_bFit = true;
bool g_bDragged = false;

const Tree &TreeOf(const std::string &race) {
  static Tree s_tree;
  static unsigned s_nChange = ~0u;
  static std::string s_race;
  static bool s_bStrings = false;
  if (s_nChange != StudioChange() || s_race != race || s_bStrings != (sgpStatStrs != NULL)) {
    s_nChange = StudioChange();
    s_race = race;
    s_bStrings = sgpStatStrs != NULL;
    s_tree = Build(race);
  }
  return s_tree;
}

const float kNodeW = 184.0f, kNodeH = 52.0f, kColW = 250.0f, kRowH = 64.0f;

const char *KindWord(const Node &n) {
  if (n.t == ST_TECHS)
    return "tech";
  if (n.t == ST_UPGRADES)
    return "upgrade";
  return n.bBuilding ? "building" : "unit";
}

std::string Tip(const Tree &tr, const Node &n) {
  std::string s = StudioShowName(n.t, n.name) + ", " + KindWord(n) + (n.bMod ? ", the mod's" : n.bEdit ? ", classic, the mod's needs" : ", classic");
  std::map<int, std::vector<std::string>> groups;
  for (const Need &d : n.needs) {
    std::string w = StudioShowName(d.t, d.name);
    if (d.kind == NEED_AT)
      w = (n.t == ST_UNITS ? "made at the " : "researched at the ") + w;
    else if (d.kind == NEED_ADDON)
      w = "with the add-on " + w;
    else if (d.kind == NEED_TECH)
      w = w + " researched";
    else
      w = "a " + w;
    groups[d.nGroup].push_back(w);
  }
  for (const auto &g : groups) {
    std::string line;
    for (size_t i = 0; i < g.second.size(); ++i)
      line += (i ? " or " : "") + g.second[i];
    s += "\n  " + line;
  }
  if (n.needs.empty())
    s += n.t == ST_UNITS ? "\n  needs nothing a tree names" : "\n  needs nothing";
  if (!n.missing.empty())
    s += "\nmissing: " + n.missing;
  s += "\nclick to open it";
  (void)tr;
  return s;
}

ImVec2 NodeAt(const Node &n, ImVec2 o, float z) {
  return ImVec2(o.x + (P(24.0f) + n.fCol * P(kColW)) * z, o.y + (P(16.0f) + n.fRow * P(kRowH)) * z);
}

void Fit(const Tree &tr, ImVec2 size) {
  const float w = P(48.0f) + static_cast<float>(std::max(tr.nCols, 1)) * P(kColW);
  const float h = P(32.0f) + static_cast<float>(std::max(tr.nRows, 1)) * P(kRowH);
  g_fZoom = std::max(0.35f, std::min(1.0f, std::min(size.x / w, size.y / h)));
  g_pan = ImVec2(std::max(0.0f, (size.x - w * g_fZoom) / 2.0f), std::max(0.0f, (size.y - h * g_fZoom) / 2.0f));
}

void Arrow(ImDrawList *d, ImVec2 a, ImVec2 b, ImU32 col, float th, float z) {
  const float dx = std::max(P(30.0f) * z, (b.x - a.x) * 0.5f);
  d->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, th);
  const float s = P(5.0f) * z;
  d->AddTriangleFilled(b, ImVec2(b.x - s * 1.4f, b.y - s), ImVec2(b.x - s * 1.4f, b.y + s), col);
}

void Canvas(const Tree &tr, ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 size(b.x - a.x, b.y - a.y);
  if (g_bFit) {
    Fit(tr, size);
    g_bFit = false;
  }
  d->AddRectFilled(a, b, C(TK_WELL), P(4.0f));
  d->AddRect(a, b, C(TK_LINE), P(4.0f));
  ImGui::SetCursorScreenPos(a);
  const bool bClick = ImGui::InvisibleButton("##treecanvas", size);
  ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
  const bool bHover = ImGui::IsItemHovered();
  ImGuiIO &io = ImGui::GetIO();
  if (ImGui::IsItemActivated())
    g_bDragged = false;
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
    g_pan.x += io.MouseDelta.x;
    g_pan.y += io.MouseDelta.y;
    g_bDragged = true;
  }
  if (bHover && io.MouseWheel != 0.0f) {
    const float z0 = g_fZoom;
    g_fZoom = std::max(0.3f, std::min(2.0f, g_fZoom * powf(1.12f, io.MouseWheel)));  // a notch a step
    // the place under the pointer stays under it
    const ImVec2 m(io.MousePos.x - a.x - g_pan.x, io.MousePos.y - a.y - g_pan.y);
    g_pan.x -= m.x * (g_fZoom / z0 - 1.0f);
    g_pan.y -= m.y * (g_fZoom / z0 - 1.0f);
  }
  const float z = g_fZoom;
  const ImVec2 o(a.x + g_pan.x, a.y + g_pan.y);
  const float nw = P(kNodeW) * z, nh = P(kNodeH) * z;
  // the node under the pointer
  int nOver = -1;
  if (bHover)
    for (size_t i = 0; i < tr.nodes.size(); ++i) {
      const ImVec2 p = NodeAt(tr.nodes[i], o, z);
      if (io.MousePos.x >= p.x && io.MousePos.x < p.x + nw && io.MousePos.y >= p.y && io.MousePos.y < p.y + nh)
        nOver = static_cast<int>(i);
    }
  d->PushClipRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), true);
  // the tiers' heads
  for (int k = 0; k < tr.nTiers; ++k) {
    const float x = o.x + (P(24.0f) + static_cast<float>(tr.tierCol[static_cast<size_t>(k)]) * P(kColW)) * z;
    Wide(d, std::max(6.0f, 8.5f * z), x, o.y + P(2.0f) * z, C(TK_INK3), k == 0 ? "needs nothing" : "tier " + std::to_string(k), 0.08f);
  }
  // the lines: from what a node needs and where it is made, to it
  for (int pass = 0; pass < 2; ++pass)
    for (size_t i = 0; i < tr.nodes.size(); ++i) {
      const Node &n = tr.nodes[i];
      const ImVec2 p = NodeAt(n, o, z);
      for (const Need &dn : n.needs) {
        const auto it = tr.at.find(Key(dn.t, dn.name));
        if (it == tr.at.end() || it->second == static_cast<int>(i))
          continue;
        const Node &s = tr.nodes[static_cast<size_t>(it->second)];
        if (dn.kind == NEED_AT && s.bWorker)
          continue;
        const bool bLit = nOver >= 0 && (nOver == static_cast<int>(i) || nOver == it->second);
        if ((pass == 1) != bLit)
          continue;
        const ImVec2 sp = NodeAt(s, o, z);
        ImU32 col = dn.kind == NEED_AT ? C(TK_INK2, 0.75f) : C(TK_INK3, 0.55f);
        if (n.bMod || s.bMod)
          col = C(TK_MOD, 0.8f);
        if (bLit)
          col = C(TK_HI);
        Arrow(d, ImVec2(sp.x + nw, sp.y + nh / 2.0f), ImVec2(p.x, p.y + nh / 2.0f), col,
              (dn.kind == NEED_AT ? P(2.0f) : P(1.2f)) * std::max(0.6f, z), z);
      }
    }
  // the nodes
  for (size_t i = 0; i < tr.nodes.size(); ++i) {
    const Node &n = tr.nodes[i];
    const ImVec2 p = NodeAt(n, o, z), q(p.x + nw, p.y + nh);
    if (q.x < a.x || p.x > b.x || q.y < a.y || p.y > b.y)
      continue;
    const bool bOver = nOver == static_cast<int>(i);
    d->AddRectFilledMultiColor(p, q, C(TK_BTN_TOP), C(TK_BTN_TOP), C(TK_BTN_BOT), C(TK_BTN_BOT));
    const ImU32 rim = !n.missing.empty() ? C(TK_BAD) : n.bMod ? C(TK_MOD) : n.bEdit ? C(TK_CHANGED_RIM) : bOver ? C(TK_BTN_RIM_HI) : C(TK_LINE2);
    d->AddRect(p, q, rim, P(3.0f) * z, 0, (n.bMod || !n.missing.empty() || n.bEdit ? P(2.0f) : P(1.0f)) * std::max(0.7f, z));
    if (bOver)
      d->AddRectFilled(p, q, C(TK_HI, 0.07f), P(3.0f) * z);
    // its picture: a unit's sprite, else its command icon, else its initials
    const float pad = P(5.0f) * z, box = nh - pad * 2.0f;
    const ImVec2 ia(p.x + pad, p.y + pad), ib(ia.x + box, ia.y + box);
    bool bPic = false;
    if (n.t == ST_UNITS) {
      bPic = StudioUnitPicture(d, ia, ib, n.name, 12, 0);
      if (!bPic)
        bPic = StudioUnitIconPicture(d, ia, ib, n.name);
    } else {
      bPic = StudioIconPicture(d, ia, ib, static_cast<int>(StudioNumber(n.t, n.name, "icon", -1)));
    }
    if (!bPic) {
      std::string ini;
      bool bStart = true;
      for (char ch : Short(n.t, n.name)) {
        if (bStart && isalnum(static_cast<unsigned char>(ch)) && ini.size() < 2)
          ini += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        bStart = ch == ' ' || ch == '-';
      }
      const float tw = TW(STUDIO_FONT_BOLD, 14.0f * z, ini);
      T(d, STUDIO_FONT_BOLD, 14.0f * z, ia.x + (box - tw) / 2.0f, ia.y + (box - P(14.0f) * z) / 2.0f, C(TK_INK2), ini);
    }
    if (z >= 0.45f) {
      const float tx = ib.x + P(7.0f) * z, tw = q.x - tx - P(6.0f) * z;
      T(d, STUDIO_FONT_BOLD, 11.0f * z, tx, p.y + P(9.0f) * z, n.bMod ? C(TK_HI) : C(TK_INK), Short(n.t, n.name), tw);
      std::string sub = KindWord(n);
      if (n.bMod)
        sub += ", the mod's";
      else if (n.bEdit)
        sub += ", needs edited";
      if (!n.builtBy.empty())
        sub += ", by the " + Short(ST_UNITS, n.builtBy[0]);
      T(d, STUDIO_FONT_TEXT, 9.0f * z, tx, p.y + P(28.0f) * z, !n.missing.empty() ? C(TK_BAD) : C(TK_INK3), n.missing.empty() ? sub : "missing link", tw);
    }
    if (!n.missing.empty()) {
      const float r = P(7.0f) * z;
      const ImVec2 cc(q.x - r * 0.4f, p.y + r * 0.4f);
      d->AddCircleFilled(cc, r, C(TK_BAD));
      const float tw = TW(STUDIO_FONT_BOLD, 10.0f * z, "!");
      T(d, STUDIO_FONT_BOLD, 10.0f * z, cc.x - tw / 2.0f, cc.y - P(6.5f) * z, C(TK_BLACK), "!");
    }
  }
  d->PopClipRect();
  if (nOver >= 0 && !ImGui::IsItemActive())
    ImGui::SetTooltip("%s", Tip(tr, tr.nodes[static_cast<size_t>(nOver)]).c_str());
  if (bClick && !g_bDragged && nOver >= 0) {
    const Node &n = tr.nodes[static_cast<size_t>(nOver)];
    StudioOpenDoc(n.t, n.open, 0);
  }
}

// The missing links, each opening its record, with the stand-in that mends it when the race has one.
void MissingList(const Tree &tr, ImVec2 a, ImVec2 b) {
  const float fW = b.x - a.x - P(10.0f);
  ImGui::SetCursorScreenPos(a);
  if (!ImGui::BeginChild("##missing", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::EndChild();
    return;
  }
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), "Missing links", 0.1f, true);
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  int nFix = 0;
  for (const Node &n : tr.nodes)
    nFix += n.fixUnit.empty() ? 0 : 1;
  if (nFix > 0) {
    if (StudioButton("##fixall", ("Fix all: let the stand-ins in (" + std::to_string(nFix) + ")").c_str(), "done", SB_GOOD, 24.0f, 0.0f,
                     kStudioFsXs))
      StudioAsk("studio_race_fix", "\"race\":" + Q(tr.race));
    ImGui::Dummy(ImVec2(1, P(4.0f)));
  }
  if (tr.nMissing == 0)
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "none: every record of the mod's here is made or researched somewhere");
  int k = 0;
  for (const Node &n : tr.nodes) {
    if (n.missing.empty())
      continue;
    ImGui::PushID(k++);
    if (StudioButton("##miss", StudioShowName(n.t, n.name).c_str(), "clash", SB_FLAT, 22.0f, 0.0f, kStudioFsS))
      StudioOpenDoc(n.t, n.open, 0);
    ImDrawList *d = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(4.0f), p.y, C(TK_BAD), n.missing, fW - P(8.0f));
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
    if (!n.fixUnit.empty()) {
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + P(4.0f), ImGui::GetCursorScreenPos().y));
      if (StudioButton("##fix", ("Let the " + Short(ST_UNITS, n.fixBy) + " stand in for the " + Short(ST_UNITS, n.fixUnit)).c_str(), "link",
                       SB_GOLD, 20.0f, 0.0f, kStudioFsXs))
        StudioAsk("studio_race_fix", "\"race\":" + Q(tr.race) + ",\"unit\":" + Q(n.fixUnit));
      ImGui::Dummy(ImVec2(1, P(4.0f)));
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
}

// ---- stand-ins in what needs a unit the race goes without --------------------------------------------

// A classic run as a "requires" list: each group one item or an array of alternatives. What the
// list cannot say (a check of the unit's state, as not busy) is left out, as a mod's record has it.
// In a tech's use requirements the unit it names is its caster, not where it is made.
PortJson RunJson(const WORD *pTree, int nLen, int nId, bool bUse = false) {
  PortJson out;
  int nStart = -1, nEnd = -1;
  for (int i = 1; i < nLen && pTree[i] != 0xFFFF;) {
    const int id = pTree[i], s = i + 1;
    while (i < nLen && pTree[i] != 0xFFFF)
      ++i;
    if (id == nId) {
      nStart = s;
      nEnd = i;
    }
    ++i;
  }
  if (nStart < 0)
    return out;
  out.kind = PortJson::ARRAY;
  PortJson group;
  group.kind = PortJson::ARRAY;
  auto close = [&]() {
    if (group.items.size() == 1)
      out.items.push_back(group.items[0]);
    else if (group.items.size() > 1)
      out.items.push_back(group);
    group.items.clear();
  };
  bool bOr = false;
  for (int k = nStart; k < nEnd;) {
    const WORD op = pTree[k++];
    if (op == 0xFF01) {
      bOr = true;
      continue;
    }
    if (!bOr)
      close();
    bOr = false;
    if (op == 0xFF20 || op == 0xFF21)
      break;
    if (op == 0xFF02 || op == 0xFF03 || op == 0xFF04 || op == 0xFF25) {
      if (k >= nEnd)
        break;
      const WORD x = pTree[k++];
      if (op == 0xFF02) {
        PortJson at = StudioJsonObject();
        StudioJsonPut(at, bUse ? "caster" : "at", StudioJsonString(StudioRefName(ST_UNITS, x)));
        group.items.push_back(at);
      } else if (op == 0xFF25) {
        PortJson t = StudioJsonObject();
        StudioJsonPut(t, "tech", StudioJsonString(StudioRefName(ST_TECHS, x)));
        group.items.push_back(t);
      } else if (op == 0xFF03) {
        group.items.push_back(StudioJsonString(StudioRefName(ST_UNITS, x)));
      }
    } else if (op == 0xFF24) {
      group.items.push_back(StudioJsonString("broodwar"));
    } else if (op == 0xFF0F) {
      group.items.push_back(StudioJsonString("researched"));
    } else if (op < 0xFF00) {
      group.items.push_back(StudioJsonString(StudioRefName(ST_UNITS, op)));
    }
  }
  close();
  return out;
}

bool NamesUnit(const PortJson &v, const std::string &unit) {
  if (v.kind == PortJson::OBJECT) {
    const PortJson &at = v["at"].kind != PortJson::NONE ? v["at"] : v["caster"];
    return at.kind != PortJson::NONE && NameIn(ST_UNITS, at) == unit;
  }
  return (v.kind == PortJson::STRING || v.kind == PortJson::NUMBER) && NameIn(ST_UNITS, v) == unit;
}

PortJson LikeFor(const PortJson &v, const std::string &unit) {
  if (v.kind == PortJson::OBJECT) {
    PortJson at = StudioJsonObject();
    StudioJsonPut(at, v["caster"].kind != PortJson::NONE ? "caster" : "at", StudioJsonString(unit));
    return at;
  }
  return StudioJsonString(unit);
}

// A requires list with the stand-in put beside each need of the unit: true when it changed.
bool StandBeside(PortJson *pReq, const std::string &unit, const std::string &by) {
  bool bChanged = false;
  for (PortJson &v : pReq->items) {
    if (v.kind == PortJson::ARRAY) {
      const PortJson *pFound = nullptr;
      bool bHas = false;
      for (const PortJson &x : v.items) {
        if (NamesUnit(x, unit))
          pFound = &x;
        bHas = bHas || NamesUnit(x, by);
      }
      if (pFound && !bHas) {
        const PortJson add = LikeFor(*pFound, by);
        v.items.push_back(add);
        bChanged = true;
      }
    } else if (NamesUnit(v, unit)) {
      PortJson alt;
      alt.kind = PortJson::ARRAY;
      alt.items.push_back(v);
      alt.items.push_back(LikeFor(v, by));
      v = alt;
      bChanged = true;
    }
  }
  return bChanged;
}

struct Mend {
  int t = ST_UNITS;
  std::string name;    // the record mended, a classic row by its name
  std::string record;  // the record the change goes in: the mod's own, its edit of the row, or a new edit
  bool bNew = false;
  PortJson req;                    // its needs, when they change
  PortJson use;                    // a tech's use requirements, when they change
  std::vector<std::string> units;  // the units it now takes a stand-in for
};

// The mends a race's stand-ins make: each record of its tree whose needs name a unit it goes without
// and not the stand-in. Only unit, when given; the mod's own records and its edits are changed, a
// classic row is given an edit, a dependency's record is left as it is.
std::vector<Mend> Mends(const std::string &race, const std::string &onlyUnit, std::string *pWhy,
                        std::vector<std::pair<std::string, std::string>> *pPairs = nullptr) {
  std::vector<Mend> out;
  const PortJson *pRace = nullptr;
  for (const auto &r : ModRaces())
    if (r.first == race)
      pRace = r.second;
  if (!pRace) {
    *pWhy = "no race of the mod's is " + race;
    return out;
  }
  std::set<std::string> without;
  for (size_t i = 0; i < (*pRace)["without"].size(); ++i)
    without.insert(NameIn(ST_UNITS, (*pRace)["without"].At(i)));
  std::vector<std::pair<std::string, std::string>> pairs;
  for (const PortJsonMember &m : (*pRace)["ai"].members)
    if (without.count(m.first) && IsRecord(ST_UNITS, m.second.Str()) && (onlyUnit.empty() || onlyUnit == m.first))
      pairs.push_back({m.first, m.second.Str()});
  if (pPairs)
    *pPairs = pairs;
  if (pairs.empty()) {
    *pWhy = onlyUnit.empty() ? "the race has no stand-in for a unit it goes without (its \"ai\")"
                             : "the race goes without no " + onlyUnit + " with a stand-in for it in its \"ai\"";
    return out;
  }
  const Tree tr = Build(race);
  std::set<std::string> names;
  for (int t = 0; t < ST_TABLES; ++t)
    for (const StudioRecord &r : StudioRecords(t))
      names.insert(r.name);
  for (const Node &n : tr.nodes) {
    Mend m;
    m.t = n.t;
    m.name = n.name;
    StudioRecord rec;
    StudioRecordOf(n.t, n.name, &rec);
    if (n.bMod) {
      if (rec.kind != SK_OWN)
        continue;
      StudioValue v;
      StudioGet(n.t, n.name, "requires", &v);
      m.req = v.own;
      m.record = n.name;
    } else {
      // the mod's own edit of the row, when it has one, else a new one
      const int nRow = StudioClassicRow(n.t, n.name);
      for (const StudioRecord &r : StudioRecords(n.t))
        if (r.kind == SK_EDIT && r.nId == nRow && r.mod == StudioModId())
          m.record = r.name;
      if (!m.record.empty() && StudioRecordJson(n.t, m.record)["requires"].kind == PortJson::ARRAY)
        m.req = StudioRecordJson(n.t, m.record)["requires"];
      else
        m.req = RunJson(n.t == ST_UNITS ? sgTree : n.t == ST_TECHS ? sgResearchTree : sgUpgradeTree,
                        n.t == ST_UNITS ? 546 : n.t == ST_TECHS ? 160 : 420, nRow);
      if (m.record.empty()) {
        m.bNew = true;
        m.record = n.name + "_" + race;
        for (int k = 2; names.count(m.record); ++k)
          m.record = n.name + "_" + race + "_" + std::to_string(k);
      }
    }
    if (m.req.kind != PortJson::ARRAY)
      continue;
    for (const auto &p : pairs)
      if (StandBeside(&m.req, p.first, p.second))
        m.units.push_back(p.first);
    if (!m.units.empty()) {
      names.insert(m.record);
      out.push_back(m);
    }
  }
  // who may cast each tech: a classic tech's casters (its use tree's run, or the mod's edit's) and
  // the mod's own techs', so a stand-in casts what the unit it replaces cast
  std::vector<std::pair<std::string, int>> techs;  // name, its classic row or -1
  for (const StudioRecord &r : StudioRecords(ST_TECHS))
    if (r.kind == SK_OWN && r.mod == StudioModId())
      techs.push_back({r.name, -1});
  for (int id = 0; id < StudioClassicRows(ST_TECHS); ++id)
    techs.push_back({StudioRefName(ST_TECHS, id), id});
  for (const auto &tch : techs) {
    Mend m;
    m.t = ST_TECHS;
    m.name = tch.first;
    for (const Mend &x : out)
      if (x.t == ST_TECHS && x.name == tch.first)
        m = x;
    const bool bHad = !m.record.empty();
    PortJson use;
    if (tch.second < 0) {
      m.record = tch.first;
      use = StudioRecordJson(ST_TECHS, tch.first)["use_requires"];
    } else {
      if (m.record.empty())
        for (const StudioRecord &r : StudioRecords(ST_TECHS))
          if (r.kind == SK_EDIT && r.nId == tch.second && r.mod == StudioModId())
            m.record = r.name;
      if (!m.record.empty() && !m.bNew && StudioRecordJson(ST_TECHS, m.record)["use_requires"].kind == PortJson::ARRAY)
        use = StudioRecordJson(ST_TECHS, m.record)["use_requires"];
      else
        use = RunJson(sgTechUseTree, 344, tch.second, true);
    }
    if (use.kind != PortJson::ARRAY)
      continue;
    std::vector<std::string> took;
    for (const auto &p : pairs)
      if (StandBeside(&use, p.first, p.second))
        took.push_back(p.first);
    if (took.empty())
      continue;
    m.use = use;
    for (const std::string &u : took)
      if (!std::count(m.units.begin(), m.units.end(), u))
        m.units.push_back(u);
    if (m.record.empty()) {
      m.bNew = true;
      m.record = tch.first + "_" + race;
      for (int k = 2; names.count(m.record); ++k)
        m.record = tch.first + "_" + race + "_" + std::to_string(k);
      names.insert(m.record);
    }
    if (bHad) {
      for (Mend &x : out)
        if (x.t == ST_TECHS && x.name == tch.first)
          x = m;
    } else {
      out.push_back(m);
    }
  }
  return out;
}

// studio_race_fix {race, unit?}: the race's stand-ins put beside the units it goes without in what
// its tree needs (only unit's when given): the mod's records' needs changed, a classic record given
// an edit whose needs take either; one group.
bool CmdRaceFix(const PortJson &j, std::string *pAnswer) {
  const std::string race = j["race"].Str();
  std::string why;
  std::vector<std::pair<std::string, std::string>> pairs;
  const std::vector<Mend> mends = Mends(race, j["unit"].Str(), &why, &pairs);
  if (!why.empty()) {
    *pAnswer = why;
    return false;
  }
  const std::string sender = j["sender"].Str("claude");
  // the records whose needs take a stand-in, now or once mended: each in the race's tree that names
  // the classic unit beside its stand-in
  std::set<std::string> records;
  for (const Mend &m : mends)
    records.insert(m.name);
  {
    const Tree tr = Build(race);
    for (const Node &n : tr.nodes) {
      StudioValue v;
      if (n.bMod) {
        StudioGet(n.t, n.name, "requires", &v);
        if (v.own.kind == PortJson::ARRAY)
          for (const auto &p : pairs)
            if (PortJsonWrite(v.own).find(Q(p.second)) != std::string::npos && PortJsonWrite(v.own).find(Q(p.first)) != std::string::npos)
              records.insert(n.name);
      } else {
        for (const StudioRecord &r : StudioRecords(n.t))
          if (r.kind == SK_EDIT && r.nId == StudioClassicRow(n.t, n.name) && r.mod == StudioModId()) {
            const std::string req = PortJsonWrite(StudioRecordJson(n.t, r.name)["requires"]);
            for (const auto &p : pairs)
              if (req.find(Q(p.second)) != std::string::npos)
                records.insert(n.name);
          }
      }
    }
  }
  const std::string label = mends.empty() ? "name " + race + "'s stand-ins where the classic buttons say what they need"
                                          : "let " + race + "'s stand-ins into " + std::to_string(mends.size()) + " records' needs";
  const int nAt = StudioHistoryAt();
  StudioGroupBegin(sender, label);
  bool bOk = true;
  std::string list;
  for (const Mend &m : mends) {
    if (m.bNew) {
      PortJson rec = StudioJsonObject();
      StudioJsonPut(rec, "edit", StudioJsonString(m.name));
      if (m.req.kind == PortJson::ARRAY)
        StudioJsonPut(rec, "requires", m.req);
      if (m.use.kind == PortJson::ARRAY)
        StudioJsonPut(rec, "use_requires", m.use);
      bOk = StudioCreate(m.t, m.record, rec, sender, &why);
    } else {
      if (m.req.kind == PortJson::ARRAY)
        bOk = StudioSetField(m.t, m.record, "requires", m.req, sender, std::string(), &why);
      if (bOk && m.use.kind == PortJson::ARRAY)
        bOk = StudioSetField(m.t, m.record, "use_requires", m.use, sender, std::string(), &why);
    }
    if (!bOk)
      break;
    list += std::string(list.empty() ? "" : ",") + "{\"table\":" + Q(StudioTableName(m.t)) + ",\"name\":" + Q(m.name) + ",\"record\":" +
            Q(m.record) + ",\"new_edit\":" + (m.bNew ? "true" : "false") +
            ",\"casters\":" + (m.use.kind == PortJson::ARRAY ? "true" : "false") + "}";
  }
  int nWords = 0;
  if (bOk) {
    nWords = studio_races::StandInWords(pairs, records, sender, &why);
    bOk = nWords >= 0;
  }
  StudioGroupEnd();
  if (!bOk) {
    if (StudioHistoryAt() > nAt) {
      std::string ignore;
      StudioUndo(&ignore);
    }
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"race\":" + Q(race) + ",\"mended\":[" + list + "],\"words\":" + std::to_string(nWords) +
             ",\"missing\":" + std::to_string(Build(race).nMissing) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

}  // namespace

namespace studio_races {

void TreeShow(const std::string &race) {
  if (race != g_treeRace)
    g_bFit = true;
  g_treeRace = Lower(race);
  for (const auto &r : ModRaces())
    if (Lower(r.first) == g_treeRace)
      g_treeRace = r.first;
}

std::vector<StandIn> RaceStandIns(const std::string &race) {
  static std::vector<StandIn> s_list;
  static unsigned s_nChange = ~0u;
  static std::string s_race;
  if (s_nChange == StudioChange() && s_race == race)
    return s_list;
  s_nChange = StudioChange();
  s_race = race;
  s_list.clear();
  const PortJson *pRace = nullptr;
  for (const auto &r : ModRaces())
    if (r.first == race)
      pRace = r.second;
  if (!pRace)
    return s_list;
  std::string why;
  const std::vector<Mend> mends = Mends(race, std::string(), &why);
  std::set<std::string> without;
  for (size_t i = 0; i < (*pRace)["without"].size(); ++i)
    without.insert(NameIn(ST_UNITS, (*pRace)["without"].At(i)));
  for (const PortJsonMember &m : (*pRace)["ai"].members) {
    if (!without.count(m.first) || !IsRecord(ST_UNITS, m.second.Str()))
      continue;
    StandIn s;
    s.unit = m.first;
    s.by = m.second.Str();
    for (const Mend &x : mends)
      s.nRecords += std::count(x.units.begin(), x.units.end(), m.first) ? 1 : 0;
    s_list.push_back(s);
  }
  return s_list;
}

PortJson ClassicRequires(int t, int nRow, bool bUse) {
  if (nRow < 0)
    return PortJson();
  if (bUse)
    return t == ST_TECHS ? RunJson(sgTechUseTree, 344, nRow, true) : PortJson();
  return RunJson(t == ST_UNITS ? sgTree : t == ST_TECHS ? sgResearchTree : sgUpgradeTree,
                 t == ST_UNITS ? 546 : t == ST_TECHS ? 160 : 420, nRow);
}

void TreeInit(void) {
  PortControlCommand("studio_race_fix", CmdRaceFix);
}

std::string TreeJson(void) {
  const Tree &tr = TreeOf(g_treeRace);
  std::string s = "{\"race\":" + Q(tr.race) + ",\"base\":" + Q(kBaseKeys[tr.nBase]) + ",\"tiers\":" + std::to_string(tr.nTiers) +
                  ",\"missing\":" + std::to_string(tr.nMissing) + ",\"records\":[";
  for (size_t i = 0; i < tr.nodes.size(); ++i) {
    const Node &n = tr.nodes[i];
    s += std::string(i ? "," : "") + "{\"table\":" + Q(StudioTableName(n.t)) + ",\"name\":" + Q(n.name) + ",\"tier\":" +
         std::to_string(n.nTier) + ",\"mod\":" + (n.bMod ? "true" : "false") + ",\"needs\":[";
    for (size_t k = 0; k < n.needs.size(); ++k) {
      static const char *const kKind[] = {"at", "unit", "addon", "tech"};
      s += std::string(k ? "," : "") + "{" + Q(kKind[n.needs[k].kind]) + ":" + Q(n.needs[k].name) + ",\"group\":" +
           std::to_string(n.needs[k].nGroup) + "}";
    }
    s += "]" + (n.missing.empty() ? std::string() : ",\"missing\":" + Q(n.missing)) + "}";
  }
  return s + "]}";
}

void TreePage(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  // the races: the classic three, then the mod's
  std::vector<std::string> keys = {"zerg", "terran", "protoss"};
  std::vector<std::string> words = {"Zerg", "Terran", "Protoss"};
  for (const auto &r : ModRaces()) {
    keys.push_back(r.first);
    const std::string name = (*r.second)["name"].Str();
    words.push_back(name.empty() ? StudioShowName(ST_UNITS, r.first) : name);
  }
  std::vector<const char *> labels;
  for (const std::string &w : words)
    labels.push_back(w.c_str());
  int nSel = 2;
  for (size_t i = 0; i < keys.size(); ++i)
    if (keys[i] == g_treeRace)
      nSel = static_cast<int>(i);
  ImGui::SetCursorScreenPos(a);
  const int nNow = StudioSeg("##treerace", labels.data(), static_cast<int>(labels.size()), nSel, 26.0f);
  if (nNow != nSel || keys[static_cast<size_t>(nSel)] != g_treeRace) {
    g_treeRace = keys[static_cast<size_t>(nNow)];
    g_bFit = true;
  }
  const Tree &tr = TreeOf(g_treeRace);
  float x = a.x + StudioSegWidth(labels.data(), static_cast<int>(labels.size())) + P(14.0f);
  if (nNow >= 3)
    x += StudioChip(d, ImVec2(x, a.y + P(4.0f)), ("plays as " + RaceWord(tr.nBase)).c_str()) + P(6.0f);
  std::string count = std::to_string(tr.nodes.size()) + " records in " + std::to_string(tr.nTiers) + " tiers";
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, a.y + P(7.0f), C(TK_INK3), count) + P(10.0f);
  if (tr.nMissing > 0)
    StudioChip(d, ImVec2(x, a.y + P(4.0f)), (std::to_string(tr.nMissing) + (tr.nMissing == 1 ? " missing link" : " missing links")).c_str(), CH_BAD);
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(84.0f), a.y));
  if (StudioButton("##fit", "Fit", "search", SB_PLAIN, 26.0f, 84.0f, kStudioFsXs))
    g_bFit = true;
  // the legend
  float lx = a.x, ly = a.y + P(36.0f);
  auto key = [&](ImU32 col, float th, const char *psz) {
    d->AddLine(ImVec2(lx, ly + P(8.0f)), ImVec2(lx + P(22.0f), ly + P(8.0f)), col, th);
    lx += P(28.0f);
    lx += T(d, STUDIO_FONT_TEXT, kStudioFsXs, lx, ly + P(1.0f), C(TK_INK2), psz) + P(16.0f);
  };
  key(C(TK_INK2, 0.75f), P(2.0f), "made or researched at");
  key(C(TK_INK3, 0.8f), P(1.2f), "needs");
  key(C(TK_MOD), P(2.0f), "the mod's");
  d->AddCircleFilled(ImVec2(lx + P(6.0f), ly + P(8.0f)), P(6.0f), C(TK_BAD));
  lx += P(16.0f);
  lx += T(d, STUDIO_FONT_TEXT, kStudioFsXs, lx, ly + P(1.0f), C(TK_INK2), "missing link") + P(16.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, lx, ly + P(1.0f), C(TK_INK3), "drag to move, wheel to zoom; a worker's buildings say who builds them",
    b.x - lx);
  // the canvas, and the missing links beside it when there are any
  const float top = ly + P(26.0f);
  const float side = tr.nMissing > 0 ? P(280.0f) : 0.0f;
  const ImVec2 ca(a.x, top), cb(b.x - (side > 0.0f ? side + P(14.0f) : 0.0f), b.y);
  ImGui::SetCursorScreenPos(ca);
  if (ImGui::BeginChild("##treechild", ImVec2(cb.x - ca.x, cb.y - ca.y), ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    Canvas(tr, ca, cb);
  ImGui::EndChild();
  if (side > 0.0f)
    MissingList(tr, ImVec2(b.x - side, top), b);
}

}  // namespace studio_races
