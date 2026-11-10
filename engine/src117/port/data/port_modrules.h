// port_modrules.h -- a mod's race's building rules and its advisor (docs/mod-data.md, "Races"):
// how its workers build, read from the race's "rules", and whose advisor its players hear,
// from its "advisor". The rest of a race's rules are its units' own fields (requires_psi,
// requires_creep, shields_enabled, idle orders), which the studio sets; the game reads them
// as it reads a classic unit's, and these hooks let a mod's units take them up where the
// classic game decides by unit type or by race. A game without a mod never reaches them.
#ifndef PORT_MODRULES_H
#define PORT_MODRULES_H

#include <stddef.h>
#include <functional>
#include <string>
#include "../../h/point16.h"

struct PortJson;
class CUnit;

// How a race's workers build: as the Drone (morph into the building), the SCV (construct it,
// staying until it is done) or the Probe (warp it in and go on). The numbers are the races'.
enum { MOD_BUILD_MORPH = 0, MOD_BUILD_CONSTRUCT = 1, MOD_BUILD_WARP = 2 };

// The advisor's lines, each three sounds in a row, the Zerg's, the Terran's and the Protoss's
// (kModAdvisorLines in port_modrules.cpp).
enum { MOD_ADVISOR_LINES = 15 };
extern const char *const kModAdvisorLineNames[MOD_ADVISOR_LINES];

struct ModRaceRules {
  int nBuild = -1;    // MOD_BUILD_*, or -1 its base's way
  int nAdvisor = -1;  // the race whose advisor speaks, 0 zerg, 1 terran, 2 protoss; -1 its base's
  int anLines[MOD_ADVISOR_LINES] = {};  // a sound in place of a line, 0 the advisor's own
};

// A race record's "rules" and "advisor" into *pRules; false with *pWhy when they are not of
// the format. resolve gives a unit or a sound by name ("units", "sounds"), -1 for none.
bool ModRulesRead(const PortJson &race, const std::string &where,
                  const std::function<int(const char *, const PortJson &)> &resolve, ModRaceRules *pRules,
                  std::string *pWhy);

// The order a worker places a building with: nOrder (place_building, place_protoss_building or
// drone_start_build) for a classic worker's way, that of its race's rules for a worker of the
// player's race.
int ModRulesWorkerOrder(const CUnit *pWorker, int nOrder);

// OrderAllowed's word on a build order for a worker of a game with a mod: 1 when the order is
// the worker's way of building, 0 (with the error set) when it is another, -1 when the
// classic requirements decide.
int ModRulesOrderAllowed(int nOrder, CUnit *pUnit);

// In a game with a mod, an image whose script has no such animation, as a Terran building's has
// no warp-in: the order waiting for it goes on.
class CImage;
bool ModRulesNoAnimation(const CImage *pImage, int nAnim);

// A building that builds itself (warping in), which no worker resumes.
bool ModRulesBuildsItself(const CUnit *pBuilding);

// A unit type that gives power: the Pylon, and in a game with a mod a type whose idle order
// is init_psi_provider.
bool ModRulesPowerSource(int nType);

// The building a computer of a mod's race builds for nType: its power maker first, when nType
// needs power and the player has no power source yet, as a Protoss script builds a Pylon first.
int ModRulesAiPowerFirst(int nPlayer, int nType);

// The power grid's test of a building: a Protoss building, and in a game with a mod any
// building whose type needs power.
bool ModRulesPowered(const CUnit *pBuilding);

// Whether placing a building of the type shows where there is power: in a game with a mod, a
// building that needs it, whatever the race of the worker placing it.
bool ModRulesPowerShown(int nType);

// The classic creep source a type spreads creep as: the type itself, or for a mod's unit the
// classic unit it looks like when that spreads creep, else a Hatchery for a town hall and a
// Creep Colony for any other whose idle order is init_creep_growth.
int ModRulesCreepType(int nType);

// The local player's race's advisor: a line's sound for the race whose advisor it hears, or
// the mod's sound in its place; and the line's text, for the lines whose text names the race.
int ModRulesAdvisorSound(int nSound);
int ModRulesAdvisorString(int nString);

// The classic gas building a type gathers and places as: the type itself, or for a mod's
// unit the classic Refinery, Assimilator or Extractor it looks like, and the Refinery for one
// that holds a resource (the resource_container flag) and is a building but no mineral field.
int ModRulesGasType(int nType);

// The type a button of the unit's looks' card names for the unit itself (the Land button's
// building): the mod's unit when it looks like nType, else nType.
int ModRulesOwnType(const CUnit *pUnit, int nType);
// Whether a type is a mod's gas building, which a worker builds on a geyser as the classic
// ones, whichever way its race builds.
bool ModRulesModGas(int nType);

// units.dat's add-on position of a type, a table of the buildings from 106: a mod's type
// takes the row of the classic unit it looks like, and a type with no row none.
point16_t ModRulesAddOnOffset(int nType);

#endif
