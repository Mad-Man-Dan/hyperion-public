// port_mode.h -- a game mode: a mod whose mod.json says "kind": "mode" (docs/mod-data.md,
// "Modes"). The set-up's Game type list carries an entry for every mode installed, and the
// chosen one's presets and settings are drawn with the game's options; the game is played with
// the mode chosen, beside the active content mod. A game with a mode plays by
// Hyperion's rules and records the mode's id, version, fingerprint, settings and seed.
//
// Two halves: the declarations (each mode's settings, presets and needs, read from its
// mod.json for the tab, port_modedecl.cpp) and the choice the game starts with (port_mode.cpp,
// which loads the mode's data and scripts and runs it).
#ifndef PORT_MODE_H
#define PORT_MODE_H

#include <cstdint>
#include <string>
#include <vector>

// A choice of a "choice" setting, or an item of a "list" one, in the order declared.
struct PortModeItem {
  std::string id, name, line;
  bool bHard = false;  // a choice that makes the game harder, drawn in the mutators' dark red
};

// One setting of a mode, as its mod.json's "mode" block declares it. kind is "choice",
// "number", "toggle", "list" or "seed"; the rest are the fields that kind uses.
struct PortModeSetting {
  std::string id, kind, name, group, gist;
  std::vector<std::string> choices;  // choice; list: the items, each on or off
  std::vector<PortModeItem> items;   // the same, with each one's name, line and whether it is hard
  double min = 0, max = 0, step = 1; // number
  std::string unit;                  // number: the unit its value is shown in
  std::string base;                  // the default, as JSON text ("\"veteran\"", "true", "40")
  // the mode's own Luau functions, called in the set-up and not in a game (PortModeEffect):
  // the sentence under the control, what the value works out to on the map, and whether the
  // setting is drawn at all
  std::string effect, read, shown;
};

struct PortModePreset {
  std::string id, name, line;
  std::string values;  // JSON object text: setting id -> value
};

// A shape a mode's preview draws over the map's picture in the set-up, in map pixels, with the
// fields a mark on the map takes. shape is the name the mode wrote ("circle", "ring", "rect",
// "polygon" or "line") and the rest are the fields that shape uses, so nothing here has to know
// the renderer's own numbering.
struct PortModeShape {
  std::string shape;
  double x = 0, y = 0;        // a circle's and a ring's centre, a rectangle's top left, a line's first point
  double w = 0, h = 0;        // a rectangle's size
  double r = 0;               // a circle's and a ring's radius
  double x2 = 0, y2 = 0;      // a line's second point
  std::vector<double> points; // a polygon's corners, x then y
  unsigned int nRgb = 0xffffff;
  double fFill = 0, fEdge = 1, fWidth = 1, fDash = 0;
};

// A mode mod as its mod.json declares it.
struct PortModeDecl {
  std::string id, name, version, line, about;
  // how the set-up's Game type list shows the mode, from the "game_type" block: the name it is
  // listed as (its own name when the block says none), the mod its row names (the mod's own
  // name) and the classic type a game of it is launched as, by the game header's number
  // (Melee). The two words are the mode's own language keys, filled in like the rest.
  std::string typeName, typeTag;
  int typePlays = 2;
  std::string needs;            // "any race", or a mod's id
  int playersMin = 1, playersMax = 8;
  bool enemyPlayer = false;     // "enemy_player": the mode plays an enemy player itself
  bool sim = true;
  std::vector<std::string> notes;
  std::vector<PortModeSetting> settings;
  std::vector<PortModePreset> presets;
  std::string preview;          // "preview": the mode's own Luau drawing the map, PortModePreview
  bool needsMet = true;         // false: the tab greys it, saying why
  std::string whyNot;
  std::string folder;           // the mod's folder, ending in a backslash
  int nMod = -1;                // its place in PortMods
};

// The installed modes, read again when the mods folder changes (port_modedecl.cpp).
const std::vector<PortModeDecl> &PortModeList();
const PortModeDecl *PortModeFind(const std::string &id);
// A mode's settings with every one at its base, as JSON object text; and c's settings with
// the ones it leaves out filled in from the bases, which is what the game is given.
std::string PortModeSettingsBase(const PortModeDecl &d);
std::string PortModeSettingsWhole(const PortModeDecl &d, const std::string &settings);

// A setting's own words, from the mode's Luau in a VM of the set-up's own: the sentence under
// the control (effect), what the value works out to on the map chosen (read, "" without one)
// and whether the setting is drawn (shown). settings is every setting's value as JSON object
// text, with "players" the lobby's count; pszMap names the map file for read, null for none.
// Each gives "" (or true for shown) when the mode names no function, and says why in
// hyperion_mods.log when the call fails.
std::string PortModeEffect(const PortModeDecl &d, const PortModeSetting &s, const std::string &settings);
std::string PortModeRead(const PortModeDecl &d, const PortModeSetting &s, const std::string &settings,
                         const char *pszMap);
bool PortModeShown(const PortModeDecl &d, const PortModeSetting &s, const std::string &settings);
// What the mode makes of the map chosen, for the picture beside its settings: the mode's
// "preview", called with the settings and the map as read is called with a value and the map,
// giving back the shapes it draws in map pixels. False and nothing for a mode that declares no
// preview, without a map, or when the call fails; a shape whose kind is none of the five is
// left out. Presentation only, as the words are: nothing here is read by a game.
bool PortModePreview(const PortModeDecl &d, const std::string &settings, const char *pszMap,
                     std::vector<PortModeShape> *pOut);
// What those four calls have answered, forgotten. Each answer is kept by the function, the
// settings and the map, which never change under a game; a tool that writes the mode's scripts
// while it reads them (Hyperion Studio) calls this so the next ask runs the Luau again.
void PortModeForget();

// The mode a game starts with: set by the set-up, the command line (--mode <id>,
// --mode-settings <json>, --mode-seed <n>) or a room's match, before the game starts.
struct PortModeChoice {
  std::string id;        // "" for no mode
  std::string settings;  // JSON object text: setting id -> value; a setting left out takes its base
  uint32_t seed = 0;     // the run's seed; 0 draws one from the game's seed as it starts
};
void PortModeChoose(const PortModeChoice &c);
const PortModeChoice &PortModeChosen();
// A room's match (port_lanlobby.cpp): the launch's mode, the same on every seat; forgotten by
// PortModeForGame.
void PortModeChooseNet(const PortModeChoice &c);
// --mode <id> --mode-settings <preset|name=value,...|@file.json> --mode-seed <n>, read once
// into the choice.
void PortModeCommandLine();

// RulesForGame, for a new game: whether it is to have a mode, which makes the game Hyperion's.
bool PortModeChosenThisGame();
// As the game starts (game_init, after RulesForGame): the mode it plays with, the chosen one
// for a new game, the recorded one for a replay or a save with HX_OPT_MODE, else none.
void PortModeForGame();
// The mode the game being played has: its id ("" for none), its declaration (null for none),
// every setting's value as JSON object text, and the run's seed.
const char *PortModeInGame();
const PortModeDecl *PortModeInGameDecl();
const std::string &PortModeSettingsOfGame();
uint32_t PortModeSeedOfGame();
// A setting's value for the game, as JSON text; "" for an id the mode does not declare
// (hx.setting).
std::string PortModeSettingOfGame(const std::string &id);
// The player the mode plays itself, for a mode whose block says "enemy_player": the game's
// first computer, settled once as it is first asked for. The classic AI never starts for that
// player and nothing of its own is ordered for it (AiMeleeScript, AiModeKeeps): the mode's
// scripts play it. -1 without a mode, for a mode that asks for none, and in a game with no
// computer.
int PortModeEnemySlot();
// The mode's data folder for the game, ending in a backslash, "" for none; and the
// fingerprint of the data it loaded (SHA-1, 20 bytes).
const char *PortModeFolderOfGame();
const unsigned char *PortModeFingerprintOfGame();
void PortModeFingerprintSet(const unsigned char *pb);

// The replay's and the save's record, PORT_MODE_RECORD_BYTES: the mode's id, version, the
// fingerprint of its data, the seed, and the settings as JSON text. A record naming a mode
// this build has not installed, or at another version, refuses the replay or the save.
#define PORT_MODE_RECORD_BYTES 640
void PortModeSave(unsigned char *pbOut);
bool PortModeRecorded(const unsigned char *pbIn, int nBytes);
bool PortModeRecordBytesRead(int nBytes);
// Why a recorded mode cannot be played, null when it can.
const char *PortModeRecordRefusal();

#endif
