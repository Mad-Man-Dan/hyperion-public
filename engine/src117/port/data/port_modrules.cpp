// port_modrules.cpp -- a mod's race's building rules and its advisor (port_modrules.h).
//
// The classic game decides some of a race's ways by unit type or by race where a unit's own
// fields cannot say: how a worker places a building (the order its build buttons give), which
// units give power (the Pylon), which spread creep and make larva (the Hatchery's family), and
// which advisor speaks (the race of the unit the line is about). Each hook here answers as the
// classic game does in a game without a mod, and in a game with one lets the race's rules and
// its units' fields decide.
#include <string.h>
#include <string>
#include "port_modrules.h"
#include "port_moddata.h"  // g_hxModGame, ModRaceOf, ModRaceRulesOf, ModRaceBase
#include "../platform/port_json.h"
#include "../../h/typelimits.h"
#include "../../h/CUnit.h"
#include "../../h/CImage.h"  // ImageScriptAnim
#include "../../h/net_cmds.h"  // set_allowed_err
#include "../../gen/globals117.h"

#ifdef OC_DLL_MODE
#ifndef gubUnitOrderAI
#define gubUnitOrderAI (reinterpret_cast<BYTE *>(OC_VA(0x520D50u)))
#endif
#else
extern order_id_t gubUnitOrderAI[228] OC_UNALIGNED;  // units.dat's computer idle order
#endif

namespace {

// The orders a worker places a building with, by race: the Drone's, the SCV's, the Probe's.
const int kBuildOrders[3] = {0x19, 0x1E, 0x1F};
// The classic workers, by race.
const int kWorkers[3] = {0x29, 0x07, 0x40};

const int kOrderInitCreep = 0x65;  // init_creep_growth
const int kOrderInitPsi = 0xA4;    // init_psi_provider

// The advisor's lines: the first of three sounds, the Zerg's, the Terran's and the Protoss's,
// and the first of three texts when the text names the race, else 0.
struct AdvisorLine {
  int nSound;
  int nString;
};
const AdvisorLine kModAdvisorLines[MOD_ADVISOR_LINES] = {
    {0x93, 0},      // minerals: not enough minerals
    {0x96, 0},      // gas: not enough gas
    {0x99, 0x34C},  // supply: more supply needed (spawn more Overlords, ...)
    {0x9C, 0x360},  // energy: not enough energy
    {0x75, 0},      // base_attacked: the base is under attack
    {0x78, 0},      // forces_attacked: the forces are under attack
    {0x7B, 0},      // research_done: research complete
    {0x83, 0},      // upgrade_done: upgrade complete
    {0x72, 0},      // unpowered: a unit without power, selected
    {0x01, 0x34F},  // error: the error cue, and the most units a player may have
    {0x8A, 0},      // cannot: an order that cannot be given
    {0x8D, 0},      // cannot_place: a building that cannot go there
    {0x90, 0},      // cannot_land: a building that cannot land there
    {0x9F, 0},      // cannot_build_here: the worker cannot build there
    {0xA2, 0},      // build_error: the worker cannot build
};

int LineOfSound(int nSound, int *pnRace) {
  for (int i = 0; i < MOD_ADVISOR_LINES; ++i)
    if (nSound >= kModAdvisorLines[i].nSound && nSound < kModAdvisorLines[i].nSound + 3) {
      *pnRace = nSound - kModAdvisorLines[i].nSound;
      return i;
    }
  return -1;
}

int RaceNamed(const std::string &s) {
  return s == "zerg" ? 0 : s == "terran" ? 1 : s == "protoss" ? 2 : -1;
}

// The rules of a player's race of the mod, or null for one of the three.
const ModRaceRules *RulesOf(int nPlayer) {
  if (!g_hxModGame || nPlayer < 0 || nPlayer >= 12)
    return nullptr;
  return ModRaceRulesOf(ModRaceOf(nPlayer));
}

// A worker's way of building as the classic game has it: its looks' race, -1 for no worker.
int ClassicWay(int nType) {
  const int nLooks = static_cast<int>(UnitTypeLooks(static_cast<unsigned>(nType)));
  for (int r = 0; r < 3; ++r)
    if (nLooks == kWorkers[r])
      return r;
  return -1;
}

// The way a worker builds: its race's rule when the worker builds its race's base's way.
int WorkerWay(const CUnit *pWorker) {
  const int nWay = ClassicWay(pWorker->wType);
  if (nWay < 0)
    return -1;
  const int nRace = ModRaceOf(pWorker->bPlayer);
  const ModRaceRules *pRules = RulesOf(pWorker->bPlayer);
  if (pRules && pRules->nBuild >= 0 && nWay == ModRaceBase(nRace))
    return pRules->nBuild;
  return nWay;
}

}  // namespace

const char *const kModAdvisorLineNames[MOD_ADVISOR_LINES] = {
    "minerals", "gas",      "supply",       "energy",     "base_attacked",     "forces_attacked",
    "research_done", "upgrade_done", "unpowered", "error", "cannot", "cannot_place", "cannot_land",
    "cannot_build_here", "build_error"};

bool ModRulesRead(const PortJson &race, const std::string &where,
                  const std::function<int(const char *, const PortJson &)> &resolve, ModRaceRules *pRules,
                  std::string *pWhy) {
  const PortJson &rules = race["rules"];
  if (rules.kind != PortJson::NONE && rules.kind != PortJson::OBJECT) {
    *pWhy = where + ": its rules are an object";
    return false;
  }
  for (const PortJsonMember &m : rules.members) {
    const std::string &k = m.first;
    const PortJson &v = m.second;
    if (k == "build") {
      const std::string s = v.Str();
      pRules->nBuild = s == "morph" ? MOD_BUILD_MORPH : s == "construct" ? MOD_BUILD_CONSTRUCT
                                                      : s == "warp"      ? MOD_BUILD_WARP
                                                                         : -2;
      if (pRules->nBuild == -2) {
        *pWhy = where + ": its rules' build is morph, construct or warp";
        return false;
      }
    } else if (k == "power" || k == "creep" || k == "shields" || k == "regenerate") {
      if (v.kind != PortJson::BOOL) {
        *pWhy = where + ": its rules' " + k + " is true or false";
        return false;
      }
    } else if (k == "power_from" || k == "creep_from") {
      bool bUnits = v.kind == PortJson::ARRAY;
      for (size_t i = 0; bUnits && i < v.size(); ++i)
        bUnits = resolve("units", v.At(i)) >= 0;
      if (!bUnits) {
        *pWhy = where + ": its rules' " + k + " are units";
        return false;
      }
    } else if (k != "comment") {
      *pWhy = where + ": its rules have no " + k + " (build, power, power_from, creep, creep_from, shields, regenerate)";
      return false;
    }
  }
  const PortJson &adv = race["advisor"];
  if (adv.kind == PortJson::NONE)
    return true;
  const PortJson &named = adv.kind == PortJson::OBJECT ? adv["race"] : adv;
  if (named.kind != PortJson::NONE) {
    pRules->nAdvisor = RaceNamed(named.Str());
    if (pRules->nAdvisor < 0) {
      *pWhy = where + ": its advisor is zerg, terran or protoss";
      return false;
    }
  }
  const PortJson &lines = adv["lines"];
  if (adv.kind == PortJson::OBJECT && lines.kind != PortJson::NONE && lines.kind != PortJson::OBJECT) {
    *pWhy = where + ": its advisor's lines are an object of sounds by line";
    return false;
  }
  for (const PortJsonMember &m : lines.members) {
    int i = 0;
    while (i < MOD_ADVISOR_LINES && m.first != kModAdvisorLineNames[i])
      ++i;
    if (i == MOD_ADVISOR_LINES) {
      *pWhy = where + ": its advisor has no line " + m.first;
      return false;
    }
    const int nSound = resolve("sounds", m.second);
    if (nSound <= 0) {
      *pWhy = where + ": its advisor's line " + m.first + " is a sound";
      return false;
    }
    pRules->anLines[i] = nSound;
  }
  return true;
}

int ModRulesWorkerOrder(const CUnit *pWorker, int nOrder) {
  if (!g_hxModGame || pWorker == nullptr)
    return nOrder;
  const int nWay = WorkerWay(pWorker);
  return nWay < 0 ? nOrder : kBuildOrders[nWay];
}

int ModRulesOrderAllowed(int nOrder, CUnit *pUnit) {
  if (!g_hxModGame || (nOrder != 0x19 && nOrder != 0x1E && nOrder != 0x1F))
    return -1;
  const int nWay = WorkerWay(pUnit);
  if (nWay < 0)
    return -1;
  if (nOrder != kBuildOrders[nWay]) {
    set_allowed_err(0x19);
    return 0;
  }
  // the end of the requirements' walk, which the tree would have reached
  if (pUnit->dwFlags & 0x40000000) {
    set_allowed_err(0x16);
    return 0;
  }
  return 1;
}

bool ModRulesNoAnimation(const CImage *pImage, int nAnim) {
  return g_hxModGame && pImage != nullptr && ImageScriptAnim(pImage->wIscriptHeader, nAnim) == 0;
}

bool ModRulesBuildsItself(const CUnit *pBuilding) {
  // a building warping in has the Protoss's own build order; no classic Terran one does
  return pBuilding->bOrderId == 0x30;
}

bool ModRulesPowerSource(int nType) {
  if (nType == 0x9C)
    return true;
  return g_hxModGame && UnitTypeValid(static_cast<unsigned>(nType)) &&
         (gubUnitOrderInitialize[nType] == kOrderInitPsi || gubUnitOrderAI[nType] == kOrderInitPsi);
}

int ModRulesAiPowerFirst(int nPlayer, int nType) {
  if (!g_hxModGame || nPlayer < 0 || nPlayer >= 12 || !UnitTypeValid(static_cast<unsigned>(nType)) ||
      !(gulUnitTypeFlags[nType] & 0x80000))
    return nType;
  const int nRace = ModRaceOf(nPlayer);
  if (nRace == 0 || ModRaceBase(nRace) == 2)
    return nType;
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != nullptr; pUnit = pUnit->pPlayerUnitNext)
    if (!pUnit->IsDeadOrDying() && ModRulesPowerSource(pUnit->wType))
      return nType;
  // the stand-in for its base's supply building, when that gives power
  static const int kSupply[3] = {0x2A, 0x6D, 0x9C};
  const int nMaker = ModRaceAiUnit(nPlayer, kSupply[ModRaceBase(nRace)]);
  return ModRulesPowerSource(nMaker) ? nMaker : nType;
}

bool ModRulesPowered(const CUnit *pBuilding) {
  if (const_cast<CUnit *>(pBuilding)->GetRace() == 2)
    return true;
  return g_hxModGame && (gulUnitTypeFlags[pBuilding->wType] & 0x80000) != 0;
}

bool ModRulesPowerShown(int nType) {
  return g_hxModGame && (gulUnitTypeFlags[nType] & 0x80000) != 0;
}

int ModRulesCreepType(int nType) {
  if (!g_hxModGame || !UnitTypeMod(static_cast<unsigned>(nType)))
    return nType;
  const int nLooks = static_cast<int>(UnitTypeLooks(static_cast<unsigned>(nType)));
  if ((nLooks >= 0x83 && nLooks <= 0x85) || nLooks == 0x8F || nLooks == 0x90 || nLooks == 0x92)
    return nLooks;
  if (gubUnitOrderInitialize[nType] != kOrderInitCreep && gubUnitOrderAI[nType] != kOrderInitCreep)
    return nType;
  return (gulUnitTypeFlags[nType] & 0x1000) ? 0x83 : 0x8F;
}

int ModRulesGasType(int nType) {
  if (!UnitTypeMod(static_cast<unsigned>(nType)))
    return nType;
  const int nLooks = static_cast<int>(UnitTypeLooks(static_cast<unsigned>(nType)));
  if (nLooks == 0x6E || nLooks == 0x9D || nLooks == 0x95)
    return nLooks;
  // a building that holds a resource and is no mineral field: a gas building, as the Refinery
  const DWORD f = gulUnitTypeFlags[nType];
  return (f & 0x2000) && (f & 1) && !(nLooks >= 0xB0 && nLooks <= 0xB2) ? 0x6E : nType;
}

int ModRulesOwnType(const CUnit *pUnit, int nType) {
  if (pUnit == nullptr || !UnitTypeMod(pUnit->wType))
    return nType;
  return static_cast<int>(UnitTypeLooks(pUnit->wType)) == nType ? pUnit->wType : nType;
}

bool ModRulesModGas(int nType) {
  return UnitTypeMod(static_cast<unsigned>(nType)) && ModRulesGasType(nType) != nType;
}

point16_t ModRulesAddOnOffset(int nType) {
  // the table's rows are the buildings 106 to 201
  unsigned t = static_cast<unsigned>(nType);
  if (t - 106 >= 96 && UnitTypeMod(t))
    t = UnitTypeLooks(t);
  if (t - 106 >= 96) {
    const point16_t none = {0, 0};
    return none;
  }
  return gUnitAddOnOffset[t - 106];
}

int ModRulesAdvisorSound(int nSound) {
  const ModRaceRules *pRules = RulesOf(static_cast<int>(g_LocalNationID));
  if (pRules == nullptr)
    return nSound;
  int nRace = 0;
  const int nLine = LineOfSound(nSound, &nRace);
  if (nLine < 0)
    return nSound;
  if (pRules->anLines[nLine] != 0)
    return pRules->anLines[nLine];
  return pRules->nAdvisor >= 0 ? kModAdvisorLines[nLine].nSound + pRules->nAdvisor : nSound;
}

int ModRulesAdvisorString(int nString) {
  const ModRaceRules *pRules = RulesOf(static_cast<int>(g_LocalNationID));
  if (pRules == nullptr || pRules->nAdvisor < 0)
    return nString;
  for (const AdvisorLine &l : kModAdvisorLines)
    if (l.nString != 0 && nString >= l.nString && nString < l.nString + 3)
      return l.nString + pRules->nAdvisor;
  return nString;
}
