// studio_races.h -- what the Cards and races workspace's files share (studio_races.cpp,
// studio_races_cards.cpp, studio_races_tree.cpp): the record picker, a unit's race, the text
// field that edits on letting go, and each page.
#ifndef STUDIO_RACES_H
#define STUDIO_RACES_H

#include <set>
#include <string>
#include <vector>
#include "imgui.h"
#include "../port/platform/port_json.h"
#include "studio_ui.h"

namespace studio_races {

// A record picked by name with a search, among the mod's (its own and its dependencies'), the
// classic rows or both; a race's rows first when nRace is one; only those pfnOk takes when given.
enum { PICK_ANY, PICK_MOD, PICK_CLASSIC };
bool Pick(const char *pszId, int t, const std::string &now, float fW, std::string *pPicked, int nWhich, float fH,
          const char *pszEmpty, int nRace, bool (*pfnOk)(const std::string &));

// A unit's race by its group flags: 0 Zerg, 1 Terran, 2 Protoss, -1 none; the race's word.
int UnitRace(const std::string &unit);
std::string RaceWord(int nRace);
// A name a field gives, by name or by number.
std::string NameIn(int t, const PortJson &v);

// Text typed in a field, given back once the field lets go of the keyboard and it changed.
bool TextField(const char *pszId, const std::string &key, const std::string &now, float fW, float fH, std::string *pOut,
               bool bMulti = false, const char *pszHint = nullptr, int nMax = 511, float fFs = kStudioFsS);

// The units whose cards (the classic and the mod's) have a button that does pszWhat ("make",
// "research", "upgrade", "cast") to a record; a worker's build menu is given as the worker.
std::vector<std::string> ButtonsFor(const char *pszWhat, const std::string &target);

// The pages: races (studio_races.cpp), cards (studio_races_cards.cpp), the tech tree
// (studio_races_tree.cpp); each drawn in a to b. Showing a race, a unit's card, a tree's race.
void CardsPage(ImVec2 a, ImVec2 b);
void TreePage(ImVec2 a, ImVec2 b);
void ShowRace(const std::string &race);
void CardsShow(const std::string &unit);
void TreeShow(const std::string &race);
// A unit a race goes without and the mod's unit its "ai" puts in its place, with the records of the
// race's tree whose needs name the classic one and do not take the stand-in yet.
struct StandIn {
  std::string unit;
  std::string by;
  int nRecords = 0;
};
std::vector<StandIn> RaceStandIns(const std::string &race);
// The classic buttons' text while what they need is missing, for the records given, naming each
// unit a race goes without beside its stand-in ("Barracks or Iron Forge"): the mod's words for
// them made or changed, inside the caller's group. Their count, -1 on a refusal (pWhy).
int StandInWords(const std::vector<std::pair<std::string, std::string>> &pairs, const std::set<std::string> &records,
                 const std::string &sender, std::string *pWhy);
// A classic row's requirements as a mod's record writes them, from the classic trees: what it needs,
// or with bUse a tech's use requirements (its casters); null where the row has none.
PortJson ClassicRequires(int t, int nRow, bool bUse);
// The tree's command (studio_race_fix), registered once.
void TreeInit(void);
// The tree shown, as JSON: its race and each record with its tier, what it needs and the link it misses.
std::string TreeJson(void);
void CardsInit(void);

}  // namespace studio_races

#endif
