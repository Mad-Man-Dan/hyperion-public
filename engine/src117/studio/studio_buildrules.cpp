// studio_buildrules.cpp -- a race's building rules and its advisor, on the race's page
// (docs/mod-data.md, "Races"): how its workers build, whether its buildings need power or creep and
// what makes them, shields and healing, each a block with what it does in words; the units' fields
// each rule asks for, offered as one fix; and whose advisor its players hear, line by line, with a
// file of the mod's own in place of any line. The engine reads "build" and "advisor"
// (port/data/port_modrules.cpp); the other rules are kept for the studio, and the game reads them
// from the units' own fields (requires_psi, requires_creep, shields_enabled, regenerate, the idle
// orders) that the fix sets.
#include <algorithm>
#include <set>
#include <vector>
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_media.h"
#include "studio_races.h"

namespace {

using namespace studio_draw;

const char *const kBases[] = {"zerg", "terran", "protoss"};
const char *const kBaseWords[] = {"Zerg", "Terran", "Protoss"};
const char *const kWorkers[] = {"zerg_drone", "terran_scv", "protoss_probe"};
const char *const kWays[] = {"morph", "construct", "warp"};

// units.dat's flags and orders the rules set
const long long kBuilding = 0x1, kAddon = 0x2, kWorker = 0x8, kRegenerate = 0x80, kHall = 0x1000, kGas = 0x2000,
                kNeedsCreep = 0x20000, kNeedsPower = 0x80000, kTrains = 0x80000000LL;
const int kInitCreep = 101, kInitPsi = 164;

// The advisor's lines as port_modrules.cpp has them: the line's name, the first of its three
// sounds (the Zerg's, the Terran's, the Protoss's), and what it says.
struct Line {
  const char *pszName;
  int nSound;
  const char *pszWords;
};
const Line kLines[] = {
    {"minerals", 0x93, "Not enough minerals"},
    {"gas", 0x96, "Not enough Vespene gas"},
    {"supply", 0x99, "More supply needed (\"construct additional pylons\")"},
    {"energy", 0x9C, "Not enough energy"},
    {"base_attacked", 0x75, "Your base is under attack"},
    {"forces_attacked", 0x78, "Your forces are under attack"},
    {"research_done", 0x7B, "Research complete"},
    {"upgrade_done", 0x83, "Upgrade complete"},
    {"unpowered", 0x72, "A unit without power, picked"},
    {"error", 0x01, "The error cue; the most units a player may have"},
    {"cannot", 0x8A, "An order that cannot be given"},
    {"cannot_place", 0x8D, "A building that cannot go there"},
    {"cannot_land", 0x90, "A building that cannot land there"},
    {"cannot_build_here", 0x9F, "The worker cannot build there"},
    {"build_error", 0xA2, "The worker cannot build that"},
};
const int kLineCount = static_cast<int>(sizeof kLines / sizeof kLines[0]);

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

int BaseOf(const PortJson &race) {
  const std::string b = race["base"].Str();
  for (int i = 0; i < 3; ++i)
    if (b == kBases[i])
      return i;
  return -1;
}

const PortJson &RaceRec(const std::string &id) {
  return StudioDoc("types")["races"][id.c_str()];
}

long long Flags(const std::string &u) {
  return StudioNumber(ST_UNITS, u, "flags") & 0xFFFFFFFFLL;
}

bool Own(const std::string &u) {
  StudioRecord r;
  return StudioRecordOf(ST_UNITS, u, &r) && (r.kind == SK_OWN || r.kind == SK_EDIT);
}

bool IsModUnit(const std::string &u) {
  StudioRecord r;
  return StudioRecordOf(ST_UNITS, u, &r) && (r.kind == SK_OWN || r.kind == SK_DEPENDENCY);
}

std::vector<std::string> Names(const PortJson &list) {
  std::vector<std::string> out;
  for (size_t i = 0; i < list.size(); ++i)
    out.push_back(studio_races::NameIn(ST_UNITS, list.At(i)));
  return out;
}

bool In(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

std::string List(const std::vector<std::string> &units, size_t nMax = 4) {
  std::string s;
  for (size_t i = 0; i < units.size() && i < nMax; ++i)
    s += (i ? (i + 1 == units.size() ? " and " : ", ") : "") + StudioShowName(ST_UNITS, units[i]);
  if (units.size() > nMax)
    s += " and " + std::to_string(units.size() - nMax) + " more";
  return s;
}

// ---- what a race's rules are, and what they ask of its units ------------------------------------------

// A rule's state: its own value when the race gives one, else its base's way.
struct Rules {
  int nBase = -1;
  int nBuild = -1;      // 0 morph, 1 construct, 2 warp
  bool bBuildOwn = false;
  bool abOn[4] = {};    // power, creep, shields, regenerate
  bool abOwn[4] = {};   // given by the race, not its base's
  std::vector<std::string> powerFrom, creepFrom;
};
const char *const kToggles[] = {"power", "creep", "shields", "regenerate"};

Rules RulesOf(const PortJson &r) {
  Rules o;
  o.nBase = BaseOf(r);
  const PortJson &rules = r["rules"];
  o.nBuild = o.nBase;
  const std::string b = rules["build"].Str();
  for (int i = 0; i < 3; ++i)
    if (b == kWays[i]) {
      o.nBuild = i;
      o.bBuildOwn = true;
    }
  // each toggle's base default: the Protoss's power and shields, the Zerg's creep and healing
  const bool abBase[4] = {o.nBase == 2, o.nBase == 0, o.nBase == 2, o.nBase == 0};
  for (int i = 0; i < 4; ++i) {
    const PortJson &v = rules[kToggles[i]];
    o.abOwn[i] = v.kind == PortJson::BOOL;
    o.abOn[i] = o.abOwn[i] ? v.b : abBase[i];
  }
  o.powerFrom = Names(rules["power_from"]);
  o.creepFrom = Names(rules["creep_from"]);
  return o;
}

// One change the rules ask of a unit: a field and the value it takes.
struct Change {
  std::string unit, field;
  PortJson to;
  std::string why;
};

PortJson Num(long long n) {
  return StudioJsonNumber(static_cast<double>(n));
}

// A unit's flags with bits set or cleared, as a change when they differ.
void FlagChange(std::vector<Change> *pOut, const std::string &u, long long nSet, long long nClear, const std::string &why) {
  const long long f = Flags(u);
  const long long g = (f | nSet) & ~nClear;
  if (g == f)
    return;
  for (Change &c : *pOut)
    if (c.unit == u && c.field == "flags") {
      const long long h = (static_cast<long long>(c.to.d) | nSet) & ~nClear;
      c.to = Num(h);
      c.why += ", " + why;
      return;
    }
  pOut->push_back({u, "flags", Num(g), why});
}

void OrderChange(std::vector<Change> *pOut, const std::string &u, int nOrder, const char *pszOrder, const std::string &why) {
  for (const char *pszField : {"idle_order", "ai_idle_order"})
    if (StudioNumber(ST_UNITS, u, pszField) != nOrder)
      pOut->push_back({u, pszField, StudioJsonString(pszOrder), why});
}

// The race's own units, as its record names them.
std::vector<std::string> RaceUnits(const PortJson &r) {
  return Names(r["units"]);
}

// A unit made in the mod, not an edit of a classic one.
bool MadeHere(const std::string &u) {
  StudioRecord rec;
  return StudioRecordOf(ST_UNITS, u, &rec) && rec.kind == SK_OWN;
}

// The units the fix changes: the race's own, and the mod's units it names as makers of power
// or creep, which no other player has unless another race names them too.
std::vector<std::string> FixedUnits(const PortJson &r) {
  std::vector<std::string> out = RaceUnits(r);
  for (const char *pszList : {"power_from", "creep_from"})
    for (const std::string &u : Names(r["rules"][pszList]))
      if (!In(out, u) && MadeHere(u))
        out.push_back(u);
  return out;
}

// What each rule the race gives asks of its own units. The classic units it shares with its
// base's players are left as they are: their fields are every player's.
std::vector<Change> Changes(const PortJson &r) {
  const Rules o = RulesOf(r);
  std::vector<Change> out;
  for (const std::string &u : FixedUnits(r)) {
    if (!Own(u))
      continue;
    const long long f = Flags(u);
    const bool bBuilding = (f & kBuilding) != 0, bPlain = bBuilding && !(f & (kHall | kGas | kAddon));
    if (o.abOwn[0]) {
      const bool bMaker = In(o.powerFrom, u);
      if (o.abOn[0] && bPlain && !bMaker)
        FlagChange(&out, u, kNeedsPower, 0, "needs power");
      if (!o.abOn[0] || bMaker || !bPlain)
        FlagChange(&out, u, 0, kNeedsPower, bMaker ? "a power maker stands anywhere" : "stands anywhere");
      if (o.abOn[0] && bMaker)
        OrderChange(&out, u, kInitPsi, "init_psi_provider", "gives power");
    }
    if (o.abOwn[1]) {
      const bool bMaker = In(o.creepFrom, u), bHall = bBuilding && (f & kHall);
      if (o.abOn[1] && bPlain)
        FlagChange(&out, u, kNeedsCreep, 0, "needs creep");
      if (!o.abOn[1] || !bPlain)
        FlagChange(&out, u, 0, kNeedsCreep, bHall ? "a town hall stands anywhere" : "stands anywhere");
      if (o.abOn[1] && bMaker)
        OrderChange(&out, u, kInitCreep, "init_creep_growth", bHall ? "spreads creep and makes larva" : "spreads creep");
    }
    if (o.abOwn[2]) {
      const bool bHas = StudioNumber(ST_UNITS, u, "shields_enabled") != 0;
      if (o.abOn[2] && !bHas) {
        out.push_back({u, "shields_enabled", Num(1), "has shields"});
        if (StudioNumber(ST_UNITS, u, "shields") <= 0) {
          const long long hp = StudioNumber(ST_UNITS, u, "hit_points");
          out.push_back({u, "shields", Num(std::max(10LL, hp / 2)), "shields of half its life"});
        }
      } else if (!o.abOn[2] && bHas) {
        out.push_back({u, "shields_enabled", Num(0), "has no shields"});
      }
    }
    if (o.abOwn[3])
      FlagChange(&out, u, o.abOn[3] ? kRegenerate : 0, o.abOn[3] ? 0 : kRegenerate, o.abOn[3] ? "heals" : "does not heal");
  }
  return out;
}

struct Conflict {
  bool bError;
  std::string text;
};

// What does not fit together: a building needing power with no power maker, and the like.
std::vector<Conflict> Conflicts(const std::string &id, const PortJson &r) {
  (void)id;
  const Rules o = RulesOf(r);
  std::vector<Conflict> out;
  const std::vector<std::string> own = RaceUnits(r), without = Names(r["without"]);
  if (o.abOn[0] && o.abOwn[0]) {
    if (o.powerFrom.empty())
      out.push_back({true, "Its buildings need power, but it has no power maker: none of them could be placed. Name one of its "
                           "buildings as a power maker."});
    for (const std::string &u : o.powerFrom) {
      const long long f = Flags(u);
      if (!In(own, u) && !MadeHere(u))
        out.push_back({false, StudioShowName(ST_UNITS, u) + " is not one of the race's units: the fix leaves it as it is, so it "
                                                           "gives no power unless its idle orders are init_psi_provider."});
      if (!(f & kBuilding))
        out.push_back({true, StudioShowName(ST_UNITS, u) + " is not a building: power comes from buildings, as from a Pylon."});
      if (f & kTrains)
        out.push_back({true, StudioShowName(ST_UNITS, u) + " trains units: a power maker keeps its link to the power grid where "
                                                          "a rally point is kept, so it cannot also train."});
    }
  }
  if (o.abOn[1] && o.abOwn[1]) {
    bool bHall = false;
    for (const std::string &u : o.creepFrom)
      bHall = bHall || (Flags(u) & kHall);
    if (!bHall)
      out.push_back({true, "Its buildings need creep, but none of its town halls spreads it: its first buildings would have no "
                           "creep to stand on. Name its town hall among the creep makers."});
  }
  // the classic buildings of its base it keeps are every player of the base's
  if (o.nBase >= 0 && ((o.abOwn[0] && o.abOn[0] != (o.nBase == 2)) || (o.abOwn[1] && o.abOn[1] != (o.nBase == 0)))) {
    std::vector<std::string> shared;
    for (int n = 0; n < StudioClassicRows(ST_UNITS); ++n) {
      const char *psz = StudioClassicName(ST_UNITS, n);
      if (!psz || In(without, psz) || studio_races::UnitRace(psz) != o.nBase)
        continue;
      const long long f = Flags(psz);
      if (!(f & kBuilding) || (f & (kHall | kGas | kAddon)))
        continue;
      const std::vector<std::string> by = studio_races::ButtonsFor("make", psz);
      if (In(by, kWorkers[o.nBase]))
        shared.push_back(psz);
    }
    if (!shared.empty())
      out.push_back({false, List(shared) + (shared.size() == 1 ? " is" : " are") + " the classic " + kBaseWords[o.nBase] +
                                " players' as well: the race builds " + (shared.size() == 1 ? "it" : "them") +
                                " as they are. For a building that follows its rules, give it one of its own in its place."});
  }
  if (o.bBuildOwn && o.nBuild == 0 && o.nBase != 0)
    out.push_back({false, "A building its worker morphed into gives back its cost when cancelled, but not the worker, which "
                          "only a Zerg building does."});
  return out;
}

// The race's workers: its own and the classic one of its base it keeps.
std::vector<std::string> Workers(const PortJson &r) {
  std::vector<std::string> out;
  for (const std::string &u : RaceUnits(r))
    if (Flags(u) & kWorker)
      out.push_back(u);
  const int nBase = BaseOf(r);
  if (nBase >= 0 && !In(Names(r["without"]), kWorkers[nBase]))
    out.push_back(kWorkers[nBase]);
  return out;
}

// The sound a line says for a race: the mod's own, else the advisor's.
std::string LineSound(const PortJson &r, int nLine, int nAdvisor) {
  const PortJson &own = r["advisor"]["lines"][kLines[nLine].pszName];
  if (own.kind == PortJson::STRING)
    return own.s;
  const char *psz = StudioClassicName(ST_SOUNDS, kLines[nLine].nSound + nAdvisor);
  return psz ? psz : "";
}

int AdvisorOf(const PortJson &r) {
  const PortJson &a = r["advisor"];
  const std::string s = a.kind == PortJson::OBJECT ? a["race"].Str() : a.Str();
  for (int i = 0; i < 3; ++i)
    if (s == kBases[i])
      return i;
  return -1;
}

// ---- the edits -----------------------------------------------------------------------------------------

bool PutRace(const std::string &id, const PortJson &r, const std::string &sender, const std::string &label, std::string *pWhy) {
  return StudioPut("types", {"races", id}, r, sender, label, pWhy);
}

bool ApplyChanges(const std::vector<Change> &changes, const std::string &sender, std::string *pWhy) {
  for (const Change &c : changes)
    if (!StudioSetField(ST_UNITS, c.unit, c.field, c.to, sender, std::string(), pWhy))
      return false;
  return true;
}

// The race's rules with a field set, or taken away for null.
PortJson WithRule(const PortJson &r, const std::string &field, const PortJson &v) {
  PortJson out = r;
  PortJson rules = r["rules"].kind == PortJson::OBJECT ? r["rules"] : StudioJsonObject();
  if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV)
    StudioJsonErase(rules, field);
  else
    StudioJsonPut(rules, field, v);
  if (rules.members.empty())
    StudioJsonErase(out, "rules");
  else
    StudioJsonPut(out, "rules", rules);
  return out;
}

// The advisor's record: a race's word, or an object when it has lines of its own.
PortJson WithAdvisor(const PortJson &r, int nAdvisor, const PortJson *pLines) {
  PortJson out = r;
  const PortJson lines = pLines ? *pLines : r["advisor"]["lines"];
  if (lines.kind == PortJson::OBJECT && !lines.members.empty()) {
    PortJson a = StudioJsonObject();
    if (nAdvisor >= 0)
      StudioJsonPut(a, "race", StudioJsonString(kBases[nAdvisor]));
    StudioJsonPut(a, "lines", lines);
    StudioJsonPut(out, "advisor", a);
  } else if (nAdvisor >= 0) {
    StudioJsonPut(out, "advisor", StudioJsonString(kBases[nAdvisor]));
  } else {
    StudioJsonErase(out, "advisor");
  }
  return out;
}

// ---- the commands -------------------------------------------------------------------------------------

std::string RulesJson(const std::string &id) {
  const PortJson &r = RaceRec(id);
  const Rules o = RulesOf(r);
  std::string changes, conflicts, workers, lines;
  for (const Change &c : Changes(r))
    changes += (changes.empty() ? "" : ",") + std::string("{\"unit\":") + Q(c.unit) + ",\"field\":" + Q(c.field) +
               ",\"to\":" + PortJsonWrite(c.to) + ",\"why\":" + Q(c.why) + "}";
  for (const Conflict &c : Conflicts(id, r))
    conflicts += (conflicts.empty() ? "" : ",") + std::string("{\"error\":") + (c.bError ? "true" : "false") +
                 ",\"text\":" + Q(c.text) + "}";
  for (const std::string &w : Workers(r))
    workers += (workers.empty() ? "" : ",") + Q(w);
  const int nAdvisor = AdvisorOf(r);
  const int nHeard = nAdvisor >= 0 ? nAdvisor : o.nBase;
  for (int i = 0; i < kLineCount; ++i) {
    const bool bOwn = r["advisor"]["lines"][kLines[i].pszName].kind == PortJson::STRING;
    lines += (lines.empty() ? "" : ",") + std::string("{\"line\":") + Q(kLines[i].pszName) + ",\"says\":" + Q(kLines[i].pszWords) +
             ",\"sound\":" + Q(nHeard >= 0 ? LineSound(r, i, nHeard) : std::string()) + ",\"own\":" + (bOwn ? "true" : "false") + "}";
  }
  std::string s = "\"race\":" + Q(id) + ",\"rules\":" + PortJsonWrite(r["rules"]) + ",\"build\":" +
                  Q(o.nBuild >= 0 ? kWays[o.nBuild] : "") + ",\"workers\":[" + workers + "]";
  for (int i = 0; i < 4; ++i)
    s += ",\"" + std::string(kToggles[i]) + "\":" + (o.abOn[i] ? "true" : "false");
  s += ",\"changes\":[" + changes + "],\"conflicts\":[" + conflicts + "],\"advisor\":" +
       Q(nAdvisor >= 0 ? kBases[nAdvisor] : "") + ",\"lines\":[" + lines + "],\"unsaved\":" + std::to_string(StudioUnsaved());
  return s;
}

bool Fail(std::string *pAnswer, const std::string &why) {
  *pAnswer = why;
  return false;
}

// studio_race_rules {race, build?, power?, power_from?, creep?, creep_from?, shields?, regenerate?, fix?}:
// a race's building rules set (each null for its base's way), and with fix the fields of its units
// that the rules ask for set too, all one edit; the rules, the changes still to make, what does not
// fit together and its advisor's lines in the answer.
bool CmdRules(const PortJson &j, std::string *pAnswer) {
  const std::string id = j["race"].Str(), sender = Sender(j);
  PortJson r = RaceRec(id);
  if (r.kind != PortJson::OBJECT)
    return Fail(pAnswer, id.empty() ? "studio_race_rules needs race, its record's name" : "the mod has no race " + id);
  std::vector<std::string> set;
  for (const PortJsonMember &m : j.members) {
    const std::string &k = m.first;
    const PortJson &v = m.second;
    if (k == "race" || k == "sender" || k == "fix" || k == "cmd" || k == "id")
      continue;
    const bool bNull = v.kind == PortJson::NULLV;
    if (k == "build") {
      if (!bNull && !(v.kind == PortJson::STRING && (v.s == "morph" || v.s == "construct" || v.s == "warp")))
        return Fail(pAnswer, "build is morph, construct or warp, or null for its base's way");
    } else if (k == "power" || k == "creep" || k == "shields" || k == "regenerate") {
      if (!bNull && v.kind != PortJson::BOOL)
        return Fail(pAnswer, k + " is true or false, or null for its base's way");
    } else if (k == "power_from" || k == "creep_from") {
      if (!bNull && v.kind != PortJson::ARRAY)
        return Fail(pAnswer, k + " is a list of units");
      for (size_t i = 0; !bNull && i < v.size(); ++i) {
        StudioRecord rec;
        if (!StudioRecordOf(ST_UNITS, studio_races::NameIn(ST_UNITS, v.At(i)), &rec))
          return Fail(pAnswer, k + " names no unit " + studio_races::NameIn(ST_UNITS, v.At(i)));
      }
    } else {
      return Fail(pAnswer, "a race's rules are build, power, power_from, creep, creep_from, shields and regenerate");
    }
    r = WithRule(r, k, (bNull || (v.kind == PortJson::ARRAY && v.items.empty())) ? PortJson() : v);
    set.push_back(k);
  }
  const bool bFix = j["fix"].kind == PortJson::BOOL && j["fix"].b;
  std::string why;
  if (!set.empty() || bFix) {
    std::string label = "set races " + id + " rules";
    for (const std::string &k : set)
      label += " " + k;
    if (bFix)
      label = set.empty() ? "set " + id + "'s units to its rules" : label + ", and its units to them";
    StudioGroupBegin(sender, label);
    bool bOk = set.empty() || PutRace(id, r, sender, label, &why);
    if (bOk && bFix)
      bOk = ApplyChanges(Changes(RaceRec(id)), sender, &why);
    StudioGroupEnd();
    if (!bOk)
      return Fail(pAnswer, why);
  }
  *pAnswer = RulesJson(id);
  return true;
}

int LineIndex(const std::string &s) {
  for (int i = 0; i < kLineCount; ++i)
    if (s == kLines[i].pszName)
      return i;
  return -1;
}

// studio_race_advisor {race, advisor?, line?, from? | sound? | revert?}: whose advisor its players hear
// (zerg, terran or protoss, null for its base's); a line said by a file brought in as a sound of
// the mod's (from), by a sound there is (sound), or by the advisor again (revert).
bool CmdAdvisor(const PortJson &j, std::string *pAnswer) {
  const std::string id = j["race"].Str(), sender = Sender(j);
  PortJson r = RaceRec(id);
  if (r.kind != PortJson::OBJECT)
    return Fail(pAnswer, id.empty() ? "studio_race_advisor needs race, its record's name" : "the mod has no race " + id);
  int nAdvisor = AdvisorOf(r);
  const PortJson &adv = j["advisor"];
  bool bChange = false;
  std::string label = "set races " + id + " advisor";
  if (adv.kind != PortJson::NONE) {
    if (adv.kind == PortJson::NULLV) {
      nAdvisor = -1;
    } else {
      nAdvisor = -2;
      for (int i = 0; i < 3; ++i)
        if (adv.Str() == kBases[i])
          nAdvisor = i;
      if (nAdvisor == -2)
        return Fail(pAnswer, "advisor is zerg, terran or protoss, or null for its base's");
    }
    bChange = true;
  }
  PortJson lines = r["advisor"]["lines"].kind == PortJson::OBJECT ? r["advisor"]["lines"] : StudioJsonObject();
  std::string why, made;
  if (j["line"].kind == PortJson::STRING)
    label = "set races " + id + " advisor's " + j["line"].s + " line";
  StudioGroupBegin(sender, label);
  bool bOk = true;
  if (j["line"].kind == PortJson::STRING) {
    const int n = LineIndex(j["line"].s);
    if (n < 0) {
      StudioGroupEnd();
      std::string all;
      for (const Line &l : kLines)
        all += (all.empty() ? "" : ", ") + std::string(l.pszName);
      return Fail(pAnswer, "line is one of " + all);
    }
    const std::string line = kLines[n].pszName;
    if (j["revert"].kind == PortJson::BOOL && j["revert"].b) {
      StudioJsonErase(lines, line);
    } else if (j["sound"].kind == PortJson::STRING) {
      StudioRecord rec;
      if (!StudioRecordOf(ST_SOUNDS, j["sound"].s, &rec)) {
        StudioGroupEnd();
        return Fail(pAnswer, "the mod has no sound " + j["sound"].s);
      }
      StudioJsonPut(lines, line, StudioJsonString(j["sound"].s));
    } else if (j["from"].kind == PortJson::STRING) {
      std::string rel;
      bOk = StudioImport(j["from"].s, "sounds/advisor", &rel, &why);
      if (bOk) {
        made = id + "_advisor_" + line;
        PortJson rec = StudioJsonObject();
        StudioJsonPut(rec, "file", StudioJsonString(rel));
        StudioRecord there;
        if (StudioRecordOf(ST_SOUNDS, made, &there) && there.kind == SK_OWN)
          bOk = StudioSetField(ST_SOUNDS, made, "file", StudioJsonString(rel), sender, std::string(), &why);
        else
          bOk = StudioCreate(ST_SOUNDS, made, rec, sender, &why);
        StudioJsonPut(lines, line, StudioJsonString(made));
      }
    } else {
      StudioGroupEnd();
      return Fail(pAnswer, "a line takes from (a file), sound or revert");
    }
    bChange = true;
  }
  if (bOk && bChange)
    bOk = PutRace(id, WithAdvisor(r, nAdvisor, &lines), sender, label, &why);
  StudioGroupEnd();
  if (!bOk)
    return Fail(pAnswer, why);
  *pAnswer = RulesJson(id) + (made.empty() ? std::string() : ",\"made\":" + Q(made));
  return true;
}

// ---- the panel's pieces -------------------------------------------------------------------------------

void Gap(float f) {
  ImGui::Dummy(ImVec2(1, P(f)));
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

// Wrapped text at the cursor, fW wide.
void Para(const std::string &s, float fW, ImU32 col, float fPx = kStudioFsS, int nFont = STUDIO_FONT_TEXT) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushTextWrapPos(p.x + fW - ImGui::GetWindowPos().x + ImGui::GetScrollX());
  StudioPushFont(nFont, fPx);
  ImGui::PushStyleColor(ImGuiCol_Text, col);
  ImGui::TextWrapped("%s", s.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
}

void Ask(const std::string &race, const std::string &fields) {
  StudioAsk("studio_race_rules", "\"race\":" + Q(race) + (fields.empty() ? "" : "," + fields));
}

bool OkUnit(const std::string &) {
  return true;
}

// A list of units as chips, each opened with a click and taken out with its x, one added from a
// picker of the mod's units; the new list as JSON when it changed.
bool UnitList(const char *pszId, const std::vector<std::string> &now, int nRace, float W, std::string *pNew) {
  ImGui::PushID(pszId);
  const float x0 = ImGui::GetCursorScreenPos().x, right = x0 + W;
  float x = x0, y = ImGui::GetCursorScreenPos().y;
  int nOut = -1;
  for (size_t i = 0; i < now.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const std::string label = StudioShowName(ST_UNITS, now[i]);
    const float bw = StudioButtonSize(label.c_str(), "unit", 24.0f, kStudioFsXs).x, w = bw + P(24.0f);
    if (x + w > right && x > x0) {
      x = x0;
      y += P(30.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (StudioButton("##u", label.c_str(), "unit", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioOpenDoc(ST_UNITS, now[i], 0);
    ImGui::SetCursorScreenPos(ImVec2(x + bw, y));
    if (StudioButton("##x", "", "x", SB_FLAT, 24.0f, 22.0f))
      nOut = static_cast<int>(i);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Take it out");
    x += w + P(6.0f);
    ImGui::PopID();
  }
  if (x + P(190.0f) > right && x > x0) {
    x = x0;
    y += P(30.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  std::string picked;
  const bool bPicked = studio_races::Pick("##add", ST_UNITS, std::string(), 180.0f, &picked, studio_races::PICK_MOD, 24.0f,
                                          "Add one of its units", nRace, OkUnit);
  ImGui::SetCursorScreenPos(ImVec2(x0, y + P(30.0f)));
  ImGui::PopID();
  std::vector<std::string> list = now;
  if (nOut >= 0)
    list.erase(list.begin() + nOut);
  else if (bPicked && !picked.empty() && !In(now, picked))
    list.push_back(picked);
  else
    return false;
  std::string s = "[";
  for (const std::string &u : list)
    s += (s.size() > 1 ? "," : "") + Q(u);
  *pNew = s + "]";
  return true;
}

// A block: its frame drawn behind what is drawn inside, from a to the bottom the content reaches.
struct Block {
  ImDrawList *d;
  ImVec2 a;
  float w;
  explicit Block(ImDrawList *pDraw, ImVec2 at, float fW, const char *pszTitle, const char *pszGlyph) : d(pDraw), a(at), w(fW) {
    d->ChannelsSplit(2);
    d->ChannelsSetCurrent(1);
    StudioGlyph(d, pszGlyph, ImVec2(a.x + P(12.0f), a.y + P(12.0f)), P(16.0f), C(TK_HEAD));
    T(d, STUDIO_FONT_BOLD, kStudioFs, a.x + P(36.0f), a.y + P(10.0f), C(TK_INK), pszTitle);
    ImGui::SetCursorScreenPos(ImVec2(a.x + P(12.0f), a.y + P(36.0f)));
    ImGui::BeginGroup();
  }
  float Inner() const {
    return w - P(24.0f);
  }
  // the block's height
  float End(float fMinH) {
    ImGui::EndGroup();
    const float h = std::max(fMinH, ImGui::GetItemRectMax().y - a.y + P(12.0f));
    d->ChannelsSetCurrent(0);
    d->AddRectFilled(a, ImVec2(a.x + w, a.y + h), C(TK_PANE2), P(6.0f));
    d->AddRect(a, ImVec2(a.x + w, a.y + h), C(TK_LINE), P(6.0f));
    d->ChannelsMerge();
    return h;
  }
};

// The units a change touches, put in words: "Iron Forge needs power; Iron Pylon gives power".
std::vector<std::string> ChangeWords(const std::vector<Change> &changes, const char *pszKind) {
  std::vector<std::string> out;
  for (const Change &c : changes) {
    if (c.why.find(pszKind) == std::string::npos)
      continue;
    const std::string s = StudioShowName(ST_UNITS, c.unit) + " " + c.why;
    if (!In(out, s))
      out.push_back(s);
  }
  return out;
}

// The changes a block's rule asks for, as a line and the fix.
void ChangesLine(const std::string &id, const std::vector<Change> &all, const char *const *ppszKinds, int nKinds, bool bGiven,
                 float W) {
  std::vector<std::string> words;
  for (int k = 0; k < nKinds; ++k)
    for (const std::string &s : ChangeWords(all, ppszKinds[k]))
      if (!In(words, s))
        words.push_back(s);
  if (!bGiven) {
    Para("Its base's way: its units keep the fields they have.", W, C(TK_INK3), kStudioFsXs);
    return;
  }
  if (words.empty()) {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    StudioChip(ImGui::GetWindowDrawList(), q, "its units follow it", CH_GOOD);
    ImGui::Dummy(ImVec2(W, P(20.0f)));
    return;
  }
  std::string s;
  for (size_t i = 0; i < words.size() && i < 5; ++i)
    s += (i ? "; " : "") + words[i];
  if (words.size() > 5)
    s += "; and " + std::to_string(words.size() - 5) + " more";
  Para("To follow it: " + s + ".", W, C(TK_WARN), kStudioFsXs);
  Gap(2.0f);
  if (StudioButton("##fix", "Set its units", "done", SB_GOLD, 22.0f, 0.0f, kStudioFsXs))
    Ask(id, "\"fix\":true");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Every rule's changes to the race's own units, one edit");
}

// ---- the blocks ---------------------------------------------------------------------------------------

float BuildBlock(ImDrawList *d, ImVec2 a, float w, const std::string &id, const PortJson &r, const Rules &o) {
  Block b(d, a, w, "How its workers build", "make");
  static const char *const kSeg[] = {"Its base's", "Morph", "Construct", "Warp in"};
  const int nNow = o.bBuildOwn ? o.nBuild + 1 : 0;
  const int nNew = StudioSeg("##build", kSeg, 4, nNow, 26.0f, kStudioFsXs);
  if (nNew != nNow && nNew >= 0)
    Ask(id, nNew == 0 ? std::string("\"build\":null") : "\"build\":" + Q(kWays[nNew - 1]));
  Gap(4.0f);
  static const char *const kSay[] = {
      "Its workers turn into the building, as Drones do: the worker is spent, and the building grows by itself.",
      "Its workers construct the building and stay with it until it is done, as SCVs do; another of its workers can take "
      "over one left unfinished.",
      "Its workers start the building and go back to work, as Probes do: the building warps itself in."};
  if (o.nBuild >= 0)
    Para(kSay[o.nBuild], b.Inner(), C(TK_INK));
  const std::vector<std::string> workers = Workers(r);
  Gap(2.0f);
  Para(workers.empty() ? std::string("It has no worker yet: give it one among its units.")
                       : "Its workers: " + List(workers) + ". Its computer builds the same way.",
       b.Inner(), workers.empty() ? C(TK_WARN) : C(TK_INK3), kStudioFsXs);
  return b.End(P(150.0f));
}

float ToggleBlock(ImDrawList *d, ImVec2 a, float w, const std::string &id, const PortJson &r, const Rules &o, int nRule,
                  const std::vector<Change> &all) {
  static const char *const kTitles[] = {"Power", "Creep", "Shields", "Healing"};
  static const char *const kGlyphs[] = {"engine", "fx", "look", "plus"};
  static const char *const kChecks[] = {"Its buildings need power", "Its buildings need creep", "Its units have shields",
                                        "Its units heal over time"};
  Block b(d, a, w, kTitles[nRule], kGlyphs[nRule]);
  bool bOn = o.abOn[nRule];
  ImGui::PushID(nRule);
  if (StudioCheck("##on", kChecks[nRule], &bOn))
    Ask(id, "\"" + std::string(kToggles[nRule]) + "\":" + (bOn ? "true" : "false"));
  if (o.abOwn[nRule]) {
    ImGui::SameLine(0.0f, P(10.0f));
    if (StudioButton("##base", "Its base's way", "back", SB_FLAT, 22.0f, 0.0f, kStudioFsXs))
      Ask(id, "\"" + std::string(kToggles[nRule]) + "\":null");
  }
  Gap(2.0f);
  const float iw = b.Inner();
  if (nRule == 0) {
    Para(bOn ? "Each of its buildings must stand in the field of one of its power makers, as a Protoss building near a Pylon, "
               "and stops working when the last one near it is lost. Town halls, gas buildings and the power makers stand anywhere."
             : "Its buildings stand anywhere; none needs power.",
         iw, C(TK_INK));
    if (bOn && o.abOwn[0]) {
      Gap(4.0f);
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_LABEL), "Its power makers");
      std::string list;
      if (UnitList("##pf", o.powerFrom, o.nBase, iw, &list))
        Ask(id, "\"power_from\":" + list);
    }
    static const char *const kKinds[] = {"power", "stands anywhere"};
    ChangesLine(id, all, kKinds, 2, o.abOwn[0], iw);
  } else if (nRule == 1) {
    Para(bOn ? "Each of its buildings must stand on creep, which its creep makers spread, as Zerg buildings do; a town hall among "
               "them spreads creep as a Hatchery does and makes larva."
             : "Its buildings stand off creep, as Terran and Protoss buildings do.",
         iw, C(TK_INK));
    if (bOn && o.abOwn[1]) {
      Gap(4.0f);
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_LABEL), "Its creep makers");
      std::string list;
      if (UnitList("##cf", o.creepFrom, o.nBase, iw, &list))
        Ask(id, "\"creep_from\":" + list);
    }
    static const char *const kKinds[] = {"creep", "town hall stands", "larva"};
    ChangesLine(id, all, kKinds, 3, o.abOwn[1], iw);
  } else if (nRule == 2) {
    Para(bOn ? "Its units have shields over their life that recharge on their own, as Protoss units' do."
             : "Its units have no shields: damage goes to their life.",
         iw, C(TK_INK));
    static const char *const kKinds[] = {"shields"};
    ChangesLine(id, all, kKinds, 1, o.abOwn[2], iw);
  } else {
    Para(bOn ? "Its units heal a little at a time, as Zerg units do." : "Its units heal only when repaired or healed.", iw, C(TK_INK));
    static const char *const kKinds[] = {"heal"};
    ChangesLine(id, all, kKinds, 1, o.abOwn[3], iw);
  }
  ImGui::PopID();
  (void)r;
  return b.End(P(150.0f));
}

float StaysBlock(ImDrawList *d, ImVec2 a, float w, const Rules &o) {
  Block b(d, a, w, "What stays its base's", "note");
  const std::string base = o.nBase >= 0 ? kBaseWords[o.nBase] : "?";
  Para("Its supply counts as the " + base + "'s: each unit's supply_provided and supply_required, in the " + base +
           " count of a player's units. Its buildings burn or not as " + base +
           " buildings do, by their group flags. Add-ons come with a building's card: a building with an add-on button "
           "builds one (Cards).",
       b.Inner(), C(TK_INK3), kStudioFsXs);
  return b.End(P(100.0f));
}

void RulesSection(ImDrawList *d, const std::string &id, const PortJson &r, float W) {
  Head(d, "How it builds", "its ways of building, each a block it picks; the game reads them from its units' fields", W);
  const Rules o = RulesOf(r);
  const std::vector<Change> all = Changes(r);
  const std::vector<Conflict> conflicts = Conflicts(id, r);
  for (const Conflict &c : conflicts) {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    const float cw = StudioChip(d, ImVec2(q.x, q.y + P(2.0f)), c.bError ? "conflict" : "note", c.bError ? CH_BAD : CH_WARN);
    ImGui::SetCursorScreenPos(ImVec2(q.x + cw + P(10.0f), q.y + P(2.0f)));
    Para(c.text, W - cw - P(14.0f), c.bError ? C(TK_BAD) : C(TK_INK2));
    Gap(4.0f);
  }
  const bool bTwo = W > P(820.0f);
  const float gap = P(12.0f), bw = bTwo ? (W - gap) / 2.0f : W;
  const ImVec2 o0 = ImGui::GetCursorScreenPos();
  float y = o0.y;
  // the blocks, two to a row when the page is wide
  int nCol = 0;
  float rowH = 0.0f;
  auto place = [&](float h) {
    rowH = std::max(rowH, h);
    if (!bTwo || nCol == 1) {
      y += rowH + gap;
      rowH = 0.0f;
      nCol = 0;
    } else {
      nCol = 1;
    }
  };
  auto at = [&]() { return ImVec2(o0.x + (nCol == 1 ? bw + gap : 0.0f), y); };
  place(BuildBlock(d, at(), bw, id, r, o));
  for (int i = 0; i < 4; ++i)
    place(ToggleBlock(d, at(), bw, id, r, o, i, all));
  place(StaysBlock(d, at(), bw, o));
  if (nCol == 1)
    y += rowH + gap;
  ImGui::SetCursorScreenPos(ImVec2(o0.x, y));
  if (!all.empty()) {
    if (StudioButton("##fixall", ("Set its units to its rules (" + std::to_string(all.size()) + (all.size() == 1 ? " change)" : " changes)")).c_str(),
                     "done", SB_GOOD, 26.0f, 0.0f, kStudioFsXs))
      Ask(id, "\"fix\":true");
    ImGui::SameLine(0.0f, P(12.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "one edit, undone as one; only the race's own units change");
  }
  Gap(4.0f);
}

void AdvisorSection(ImDrawList *d, const std::string &id, const PortJson &r, float W) {
  Head(d, "Its advisor", "the voice that tells its players what they lack and what is done", W);
  const int nBase = BaseOf(r), nAdvisor = AdvisorOf(r), nHeard = nAdvisor >= 0 ? nAdvisor : nBase;
  const std::string baseWord = nBase >= 0 ? std::string("Its base's (") + kBaseWords[nBase] + ")" : std::string("Its base's");
  const char *const kSeg[] = {baseWord.c_str(), "Zerg", "Terran", "Protoss"};
  const int nNow = nAdvisor + 1;
  const int nNew = StudioSeg("##advisor", kSeg, 4, nNow, 26.0f, kStudioFsXs);
  if (nNew != nNow && nNew >= 0)
    StudioAsk("studio_race_advisor", "\"race\":" + Q(id) + ",\"advisor\":" + (nNew == 0 ? std::string("null") : Q(kBases[nNew - 1])));
  Gap(4.0f);
  Para("Its players hear the " + std::string(nHeard >= 0 ? kBaseWords[nHeard] : "?") +
           " advisor, and read its words where they name the race (\"" +
           (nHeard == 0 ? "Spawn more Overlords" : nHeard == 1 ? "Build more Supply Depots" : "Construct additional Pylons") +
           "\"). A line of the mod's own takes the place of the advisor's. Only what is heard and read changes.",
       W, C(TK_INK2));
  Gap(6.0f);
  // the lines, one to a row: what it says, a play, whose it is, and a file of the mod's in its place
  const float lw = std::min(W * 0.42f, P(380.0f));
  for (int i = 0; i < kLineCount; ++i) {
    ImGui::PushID(i);
    const ImVec2 q = ImGui::GetCursorScreenPos();
    if (i % 2 == 0)
      d->AddRectFilled(ImVec2(q.x - P(4.0f), q.y - P(2.0f)), ImVec2(q.x + W, q.y + P(28.0f)), C(TK_ROW));
    const std::string sound = nHeard >= 0 ? LineSound(r, i, nHeard) : std::string();
    const bool bOwn = r["advisor"]["lines"][kLines[i].pszName].kind == PortJson::STRING;
    ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + P(1.0f)));
    StudioPlayButton("##play", sound, 24.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, q.x + P(36.0f), q.y + P(5.0f), C(TK_INK), kLines[i].pszWords, lw - P(40.0f));
    const float x = q.x + lw;
    if (bOwn) {
      // the file it says, by its name alone; the sound's record when it names no file
      const float cw = StudioChip(d, ImVec2(x, q.y + P(5.0f)), "its own", CH_CHANGED);
      std::string file = StudioDoc("types")["sounds"][sound.c_str()]["file"].Str();
      file = file.empty() ? sound : file.substr(file.find_last_of("/\\") + 1);
      T(d, STUDIO_FONT_MONO, kStudioFsXs, x + cw + P(8.0f), q.y + P(6.0f), C(TK_INK2), file, P(230.0f) - cw);
    } else {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, q.y + P(6.0f), C(TK_INK3),
        (nHeard >= 0 ? std::string("the ") + kBaseWords[nHeard] + " advisor's" : std::string("none")), P(220.0f));
    }
    const std::string key = "advisor:" + id + "#" + kLines[i].pszName;
    ImGui::SetCursorScreenPos(ImVec2(x + P(240.0f), q.y + P(1.0f)));
    if (StudioButton("##file", bOwn ? "Another file" : "A file of its own", "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioPickFile(key.c_str(), "Sounds", "wav,ogg,opus,mp3,flac");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("A file brought in as a sound of the mod's, said in place of this line");
    std::string path;
    if (StudioPickedFile(key.c_str(), &path))
      StudioAsk("studio_race_advisor", "\"race\":" + Q(id) + ",\"line\":" + Q(kLines[i].pszName) + ",\"from\":" + Q(path));
    if (bOwn) {
      ImGui::SameLine(0.0f, P(6.0f));
      if (StudioButton("##revert", "The advisor's", "undo", SB_FLAT, 24.0f, 0.0f, kStudioFsXs))
        StudioAsk("studio_race_advisor", "\"race\":" + Q(id) + ",\"line\":" + Q(kLines[i].pszName) + ",\"revert\":true");
    }
    ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + P(30.0f)));
    ImGui::PopID();
  }
  Gap(4.0f);
}

}  // namespace

void StudioBuildRulesPanel(const std::string &race, float fW) {
  const PortJson &r = RaceRec(race);
  if (r.kind != PortJson::OBJECT)
    return;
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImGui::PushID("buildrules");
  RulesSection(d, race, r, fW);
  AdvisorSection(d, race, r, fW);
  ImGui::PopID();
}

void StudioBuildRulesInit(void) {
  PortControlCommand("studio_race_rules", CmdRules);
  PortControlCommand("studio_race_advisor", CmdAdvisor);
}
