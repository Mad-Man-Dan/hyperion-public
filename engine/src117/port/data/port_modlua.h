// port_modlua.h -- a mod's Lua: the mechanics its blocks cannot say, written as scripts (docs/mod-data.md,
// "Scripts").
//
// A mod's data folder may hold scripts/*.lua (or .luau), Luau compiled as the mod loads. Their
// functions are named by the mod's records: an order's block {"do": "script", "call": ...}, a
// unit's "scripts" (on_hit, on_damaged, on_death, on_made and a tick), an order's "toggle",
// "channel", "autocast", "cooldown" and "charges", and the game's "game" (start and a tick). The
// game calls them at fixed points of its frame, and they act on it through the hx table.
//
// A game plays the same on every machine and in its replay and save: the scripts read the game
// and the game's own random only, a function keeps nothing between calls but what it hands the
// game to keep (hx.set, hx.game_set, saved as HXLU), and every call has a budget of steps that is
// the same everywhere. An error stops that call alone and is logged, on every machine alike.
#ifndef PORT_MODLUA_H
#define PORT_MODLUA_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

class CUnit;
struct PortJson;
struct ModStatusRecord;  // port_modstatus.h
struct ModeMapRead;      // port_modemap.h

// ---- reading a script (port_modlua_lint.cpp) ---------------------------------------------------------
// A problem where the parser or the rules put it: lines from 1, columns from 0.
struct ModLuaProblem {
  int nLine = 0, nCol = 0, nEndLine = 0, nEndCol = 0;
  std::string what;
};
// A function the script's top makes a global: its name, its parameters as written, its line.
struct ModLuaFunction {
  std::string name;
  std::string args;
  int nLine = 0;
};
// The text parsed and checked; true when it has no problem. Its top's functions are listed
// whether or not it does.
bool ModLuaLint(const std::string &text, std::vector<ModLuaProblem> *pProblems, std::vector<ModLuaFunction> *pFunctions);
// The text compiled; false with "chunk:line: why".
bool ModLuaCompile(const std::string &chunk, const std::string &text, std::string *pBytecode, std::string *pWhy);
// The whole check the loader makes of one file, the top run once apart from any game as well:
// its problems, and its functions. chunk is how messages name it ("scripts/main.lua").
bool ModLuaCheckText(const std::string &chunk, const std::string &text, std::vector<ModLuaProblem> *pProblems,
                     std::vector<ModLuaFunction> *pFunctions);

// ---- what the scripts are given (the studio's completion and help) ------------------------------------
struct ModLuaName {
  const char *pszName;  // "hx.damage", "on_hit"
  const char *pszArgs;  // "(u, amount, opts?)"
  const char *pszHelp;  // a sentence
};
const ModLuaName *ModLuaApi(int *pnCount);
// The hooks: where each is named in types.json and when the game calls it.
const ModLuaName *ModLuaHooks(int *pnCount);

// ---- the loader (port_moddata.cpp) ---------------------------------------------------------------------
// A record's field this file reads: a unit's "scripts"; an order's "toggle", "channel", "autocast",
// "cooldown" and "charges". t is the loader's table (0 units, 5 orders).
bool ModLuaField(int t, const std::string &key);
// A push's scripts, by file name under scripts/, in place of the folder's for the next load; null
// takes them away again (port_control.cpp's mod_push).
void ModLuaPushTexts(const PortJson *pScripts);
// One submission's scripts read from folder\scripts\, checked and compiled, and its records' and
// types.json's "game" hooks read. pFiles gets each file's name and text for the fingerprint. False
// with why ("scripts/main.lua:12: ...") when one does not load; then nothing of it is kept.
bool ModLuaLoad(const PortJson &types, const std::string &folder, int nSub, const std::vector<ModStatusRecord> &records,
                std::vector<std::pair<std::string, std::string>> *pFiles, std::string *pWhy);
// Whether a function of that name is given by the scripts loaded so far, the submission being
// loaded among them (an order's script block, port_modstatus.cpp).
bool ModLuaHasFunction(const std::string &name);
void ModLuaTrim(int nSubs);
// For each game, beside ModStatusForGame: the scripts' state built for a game that has the mod.
void ModLuaForGame();

// ---- a mode's own (port_mode.h) ------------------------------------------------------------------------
// Before ModLuaForGame, for a game with a mode: the mode's id, its settings as JSON object text
// and the run its generate returned, which the scripts read as hx.setting and hx.run.
void ModLuaModeForGame(const std::string &id, const std::string &settings, const std::string &run);
// Whether types.json named a "generate" function.
bool ModLuaHasGenerate();

// What generate is given as ctx: the seed, the settings, the slots that play and which of them
// the mode treats as the enemy, the map's read, and a reader for the row a record would clone.
struct ModLuaGenPlayer {
  int nPlayer = 0;
  std::string race;  // "terran", "zerg", "protoss" or a mod race's name, "random" already drawn
  std::string kind;  // "human", "computer" or "none"
  int x = 0, y = 0;  // its start, map pixels
};
struct ModLuaGenCtx {
  unsigned dwSeed = 0;
  const PortJson *pSettings = nullptr;
  std::vector<ModLuaGenPlayer> players;
  int nEnemy = -1;  // the place in players of the enemy base, -1 for none
  const ModeMapRead *pMap = nullptr;
  // a row of the loader's tables by its record or classic name, as a JSON object; false for a
  // name nothing has
  std::function<bool(const std::string &table, const std::string &name, PortJson *pRow)> row;
};
// generate run once as a game loads, in a state of its own with no game to reach and a budget
// of its own: the records it returns as types.json text and its run as JSON, both empty when
// types.json names no generate. False with why, which names the function and its line.
bool ModLuaGenerate(const ModLuaGenCtx &ctx, std::string *pTypes, std::string *pRun, std::string *pWhy);

// A mode's setting function called in the set-up, apart from any game: effect(value, values),
// read(value, map) and shown(values), from the scripts of the mod in folder. pValue is the
// first argument, pValues the second where there is no map, pMap the second where there is;
// any may be null. *pText is what it returned as text and *pbTrue whether it was true. False
// with why.
bool ModLuaSetupCall(const std::string &folder, const std::string &fn, const PortJson *pValue,
                     const PortJson *pValues, const ModeMapRead *pMap, std::string *pText, bool *pbTrue,
                     std::string *pWhy);

// What hx.hud set, for the HUD's mode.<key> values: how many, the i-th key and its value, and
// one by its key ("" for a key nothing set). Presentation.
int ModLuaHudCount();
const char *ModLuaHudKey(int i);
const char *ModLuaHudValue(int i);
const char *ModLuaHudOf(const char *pszKey);
// Whether the game's mode has taken the unit (hx.own): the computer that owns it leaves it to
// the mode's scripts (CUnit::DispatchComputerAI). False in a game with no mode.
bool ModLuaModeOwns(const CUnit *pUnit);
// After each sight refresh (gameloop): the ground a mode's hx.reveal opened.
void ModLuaVisionRefresh();

// ---- the presentation VM (port_readout.h) --------------------------------------------------------------
// readout(unit), the unit readout's hook, is called in a state of its own: the same scripts with
// the readers of the hx table alone, hx.random among the functions left out, so a call made for
// one viewer cannot act on the game or move another machine's next draw. It is built with the
// game only when a script gives readout, and nothing it keeps is saved, hashed or sent.
// Whether this game has one, and one call: the unit's readout as JSON, false for a unit the hook
// gives nothing for (which is classic's own panel) and with why when the call fails.
bool ModLuaHasReadout();
bool ModLuaReadout(CUnit *pUnit, PortJson *pOut, std::string *pWhy);

// ---- what a tool may watch (presentation) --------------------------------------------------------------
// With the trace on, each act the scripts make on the game is kept with its frame, the unit it was
// for and the hook it was made in, and so is each brain and card "ai" call with what it decided:
// what a mode's AI is doing, for a tool to show. Both are written and never read by the game or by
// a script, go in no save and in no hash, and are dropped when a game starts, so a game plays the
// same whether a tool is watching or not. The live channel's script_trace switches them and gives
// the rows, for a game of another process; a tool in this one uses the calls below.
// --mode-trace has them on from the first frame.
void ModLuaTraceSet(bool bOn);
bool ModLuaTraceOn();

// One brain or card "ai" call: what ran, for which unit, and what it decided. nOrder is the order
// it gave (-1 for none), on the unit nTarget (-1 for none) or at the place x, y (-1, -1 for none);
// why says why it decided nothing.
struct ModLuaAiCall {
  unsigned dwAt = 0;     // the call's number, so a reader knows which it has seen
  unsigned dwFrame = 0;
  int nUnit = -1, nType = -1, nPlayer = -1;
  std::string kind;      // "brain" or "cast"
  std::string fn;        // the function called
  bool bChose = false;   // whether it did anything at all
  int nOrder = -1, nTarget = -1, x = -1, y = -1;
  std::string why;       // "" when it decided something
};
// The calls kept, the oldest first and the newest last, at most 512. The reference is good until
// the game's next frame, which may drop the oldest.
int ModLuaAiCallCount();
const ModLuaAiCall &ModLuaAiCallAt(int i);
void ModLuaAiCallsClear();

// ---- the game's hooks ---------------------------------------------------------------------------------
// Each is called only in a game that has the mod (g_hxModGame).
// In a mod's order's dispatch (CUnitUpdate2.cpp), before the spell order runs: true when the cast
// is refused, its order ended, as it has no charge left. After it: a cast that began takes one.
bool ModLuaOrderRefused(CUnit *pUnit);
void ModLuaOrderBegan(CUnit *pUnit);
// In CUnit::FireSpell, as the cast goes off: true when the order is a toggle or a channel, which
// then takes the weapon's place.
bool ModLuaCast(CUnit *pCaster);
// In CUnit::Damage: the damage as it comes (8.8, before shields and armour), as the unit's
// on_damaged changes it; and after the hit, its on_hit and the hit unit's state.
int ModLuaDamaged(CUnit *pUnit, int nWeapon, CUnit *pSource, int nSourcePlayer, int nDamage);
// A status's on_end: its script called with the unit and the status's name.
void ModLuaStatusEnded(CUnit *pUnit, const std::string &fn, const std::string &status);
void ModLuaHit(CUnit *pUnit, int nWeapon, CUnit *pSource, int nSourcePlayer, int nDamage, int nTaken);
// In CUnit::Suicide, as a unit dies; in CUnit::sub_425710, as one is made in a slot.
void ModLuaDeath(CUnit *pUnit);
void ModLuaUnitMade(CUnit *pUnit);
// An order's script block (port_modstatus.cpp), where the cast's program has it.
void ModLuaBlock(const std::string &function, unsigned dwCaster, unsigned dwTarget, int x, int y, int nOrder,
                 int nPlayer);
// Each game frame, after ModStatusFrame: the hits and deaths of the frame, the units' made and
// ticks, toggles, charges, autocast and channels, and the game's tick.
void ModLuaFrame();

// ---- a save (port_hxsave.cpp) ----------------------------------------------------------------------
int ModLuaSaveGame(void *pFile);
int ModLuaLoadGame(void *pFile);

#endif
