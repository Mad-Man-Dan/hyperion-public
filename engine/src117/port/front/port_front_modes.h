// port_front_modes.h -- the game modes in the skirmish set-up (the modes themselves are
// ../data/port_mode.h). A mode is a game type: the set-up's Game type list carries one entry for
// every mode installed, after the classic types, each tagged with the mod it comes from. The one
// chosen takes a tab of its own in the set-up's right column, named for it, between Game Options
// and Mutators, and Game Options shows a card of what the mode has changed under the Game type
// row. A classic type means no mode and no tab.
//
// Nothing here knows a mode by name. The entries come from the modes installed, greyed with the
// reason where this game cannot be played in one, and the settings from the chosen mode's
// declaration, each drawn as Game Options draws its own rows: a choice and a number as pulldowns,
// a toggle as a check box, a list as a pulldown that opens its things under the row, and a seed as
// a number to type in. So a mode written later, by anyone, needs no work here.
//
// A choice harder than the mode's own default is marked in the Mutators tab's dark red.
#pragma once
#include <string>
#include <vector>

#include "../data/port_mode.h"

namespace FrontKit {

struct MapPicture;

// What the set-up's mode block reads from the set-up: the choice it sets, whether this player
// chooses it (a guest sees the host's), the map (its picture for the tab's own box, the path the
// mode's own read opens, its name and its size) and how many players the game seats.
struct ModeSetUp {
  PortModeChoice *pChoice;
  bool bMine;
  const MapPicture *pMap = NULL;  // NULL until the map's picture is read
  const char *pszMapFile;         // the path the mode's own read opens, "" for none
  const char *pszMapTitle;
  int nMapW = 0, nMapH = 0, nTileset = -1;
  int nPlayers;
  // whether the seats hold the starts they choose (Start positions: By slot). Drawn at random,
  // which is a melee game's own way, no seat holds a known start until the game is launched, so
  // a preview's shapes say the size and the shape of what the mode makes of the map and not the
  // place, and the picture's caption says as much
  bool bStartsFixed = false;
};

// A mode as the Game type list carries it, from the mode's own declaration: its id, the name
// the list shows, the tag beside it (the mod it comes from, unless the mode says another word),
// what it is for its tip, and the classic type a game of it is launched as, by the game header's
// number (2 Melee).
struct ModeType {
  std::string id, name, tag, line, about;
  int plays = 2;
};
// The modes installed, in the order the list shows them after the classic game types.
std::vector<ModeType> ModesTypes(void);
// Why this game cannot be played in the mode, "" when it can: what the mode says of what it
// needs, then the players the set-up seats against its own least and most. nSeated is every
// seat; bEnemySeated says a computer is seated that a mode playing a player of its own would
// claim, and that seat is then not one of the players (ModesPlayers).
std::string ModesWhyNot(const std::string &id, int nSeated, bool bEnemySeated);
// The players a mode is measured by and its own Luau is handed: the seats, less the one a mode
// that plays a player of its own claims, where such a seat is seated. A mode that plays no
// player of its own, and a set-up with no seat its enemy could come out of, take every seat.
int ModesPlayers(const PortModeChoice &c, int nSeated, bool bEnemySeated);
// The choice as the Game type list changes it: a mode's id, or "" for a classic type. The mode
// set aside keeps its settings, so one chosen again comes back as it was left.
void ModesChoose(PortModeChoice *pc, const std::string &id);

// The chosen mode's own tab, x, y, w by h in SC2's units: its head (the mode's name and version,
// the mod it comes from, its line, and its presets as a pulldown), its settings as rows by group
// in a column that scrolls, and its foot (how many settings are the player's own, and the way
// back to the mode's). True when the choice changed; nothing is drawn without a mode.
bool ModesTab(float x, float y, float w, float h, const ModeSetUp &s);
// The box beside the tab: the map the game is played on, its name, starts and size, then the
// setting the player is on -- what it is, what the mode says the value does, what the mode works
// out for this map, and the mode's own notes.
void ModesTabBox(float x, float y, float w, float h, const ModeSetUp &s);
// Game Options' card of what the mode has changed, under the Game type row: the mode in short, a
// chip for every setting off the mode's own (the preset's name where the set is one, or a word
// saying the mode is as its maker set it), and the way to the tab. True when it was clicked, which
// asks for the tab.
bool ModesSummary(const ModeSetUp &s);
// How many of the mode's settings are the player's own: the tab's badge and the card's word.
int ModesChanged(const ModeSetUp &s);
// Its keys while the tab is up: Up and Down through the settings, Left and Right through the
// chosen one's values. True when it took one; *pbChanged when the choice changed.
bool ModesTabKeys(const ModeSetUp &s, bool *pbChanged);
// Take the nAt-th setting, in Up and Down's order (a check without a person).
void ModesTabAt(int nAt);

// The chosen mode's name, for the rules' mark, or "" for none; and the name its own game type
// goes by, which is what the tab and the card are named for ("" for none).
const char *ModesChosenName(const PortModeChoice &c);
const char *ModesTypeName(const PortModeChoice &c);
// Why the Rules row is held at Hyperion's, for its note, or "" when either will do: a mode that
// changes what the game computes requires them.
std::string ModesRulesNote(const PortModeChoice &c);
// Whether a mode is chosen and installed.
bool ModesChosen(const PortModeChoice &c);

// The choice the settings file keeps, so a set-up opens on the mode last played; written
// whenever the set-up changes it. A mode no longer installed comes back as no mode.
void ModesRemember(const PortModeChoice &c);
PortModeChoice ModesRemembered(void);

// Whether any mode is installed at all: with none the Game type list is the classic types alone.
bool ModesAny(void);

}  // namespace FrontKit
