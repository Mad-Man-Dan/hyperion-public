// studio_model.h -- Hyperion Studio's model: the mod open in it, its records with each field's
// value and where the value comes from, and every change as an edit that can be undone
// (docs/modules/studio.md). The panels read it straight each frame; every change goes through
// the studio's channel commands (studio_commands.cpp), so a panel, Claude and an outside tool
// change the mod the same way and each sees the others' edits in the history.
#ifndef STUDIO_MODEL_H
#define STUDIO_MODEL_H

#include <string>
#include <vector>
#include "../port/platform/port_json.h"

// The tables of types.json, numbered as the loader numbers them (port_moddata.cpp).
enum {
  ST_UNITS, ST_WEAPONS, ST_FLINGY, ST_SPRITES, ST_IMAGES, ST_ORDERS, ST_TECHS, ST_UPGRADES, ST_SOUNDS,
  ST_PORTRAITS, ST_TABLES
};
const char *StudioTableName(int t);
int StudioTableOf(const std::string &name);  // -1 for none

// ---- the fields ------------------------------------------------------------------------------------
// A field as the studio shows it: the loader's column, when it is one, and how fields.json files
// and labels it (engine/studio/fields.json).
struct StudioField {
  std::string key;
  std::string cat;     // its category's id
  std::string editor;  // int, hp, frames, px, half, bool, text, enum:<k>, flags:<k>, link:<table>, ...
  std::string label;
  std::string help;
  bool bSimple = false;  // one of the few the Simple level shows
  int nColumn = -1;      // the loader's column, -1 for a field that is no column (name, looks, requires...)
  int nRef = -1;         // the table its values name, -1 for numbers
  int nScale = 1;        // the file's value is this many times the record's (hit points)
  int nCount = 1;        // its values a row (bounds 4, placement 2)
  bool bPath = false;    // a path in the mod's data folder: a sound's file, a portrait's videos
  bool bScript = false;  // an image script, by its Name in iscript.txt or its number
};
struct StudioCategory {
  std::string id;
  std::string label;
};
const std::vector<StudioField> &StudioFields(int t);
const std::vector<StudioCategory> &StudioCategories(int t);
const StudioField *StudioFieldOf(int t, const std::string &key);
// An enum's choices (fields.json's enums), value and label each; empty for none.
const std::vector<std::pair<int, std::string>> &StudioEnum(const std::string &name);
// A flags editor's names, bit 0 first (units, targets, group).
const std::vector<std::string> &StudioFlags(const std::string &name);
// Where fields.json was read from, and why not when it was not.
const std::string &StudioFieldsError(void);

// ---- the classic game's rows ------------------------------------------------------------------------
int StudioClassicRows(int t);                     // the original's rows in the table
int StudioModFirst(int t);                        // the first id a mod's record takes
const char *StudioClassicName(int t, int nRow);   // its name in types.json ("terran_marine"), or null
int StudioClassicRow(int t, const std::string &name);  // -1 for none
// A name to show: a classic unit's as the game gives it once its strings are loaded, else the
// name made readable ("Terran Marine" from terran_marine).
std::string StudioShowName(int t, const std::string &name);

// ---- the open mod -----------------------------------------------------------------------------------
// What a record is in the open mod: its own (a new row), an edit of a row already there, or a
// record of a mod it depends on, which it reads and does not change.
enum StudioKind { SK_OWN, SK_EDIT, SK_DEPENDENCY, SK_CLASSIC };
struct StudioRecord {
  int t = -1;
  std::string name;  // its key in types.json; a classic row's types.json name
  StudioKind kind = SK_CLASSIC;
  int nId = -1;      // the row it takes, as the loader numbers it
  std::string mod;   // the mod whose types.json holds it
  std::string base;  // what it starts from, as its record names it ("" for a classic row)
};

bool StudioIsOpen(void);
const std::string &StudioModId(void);
const std::string &StudioModFolder(void);  // the mod's folder, ending in a backslash
const std::string &StudioDataFolder(void); // its data's folder (mod.json "data"), ending in a backslash
// The open mod's documents as they are now: types.json, mod.json, looks.json, hud.json and
// readout.json as trees, the image script as text; "lang", the languages of its lang\ folder, each
// file's tree by its code (port/data/port_modlang.h). Read only; change them through edits.
const PortJson &StudioDoc(const std::string &doc);
const std::string &StudioScript(void);
// The mods it depends on that bring data, in load order: their ids and types.json, read only.
int StudioDependencies(void);
const std::string &StudioDependencyId(int i);
const PortJson &StudioDependencyTypes(int i);
const std::string &StudioDependencyScript(int i);
const PortJson &StudioDependencyLang(int i);  // its "lang"
// The mod's records of a table, its own and its edits first, then its dependencies'.
std::vector<StudioRecord> StudioRecords(int t);
bool StudioRecordOf(int t, const std::string &name, StudioRecord *pOut);
// A record's own JSON in its mod's types.json, NONE for a classic row.
const PortJson &StudioRecordJson(int t, const std::string &name);

// Opening a mod: by its id among the installed ones, or a folder that holds a mod.json. False,
// with why, when it does not open.
bool StudioOpen(const std::string &modOrFolder, std::string *pWhy);
// Whether an autosave newer than the files is waiting (StudioRecover), and when it was made.
bool StudioRecoverWaiting(std::string *pWhen);
bool StudioRecover(const std::string &sender, std::string *pWhy);
// Saving: each document that changed written whole, through a temporary file. False with why.
bool StudioSave(std::string *pWhy);
// The working state into the mod's .studio\autosave\, when it changed since the last; done each
// minute by the studio's poll and before each push.
void StudioAutosave(bool bNow);

// ---- values and where they come from ----------------------------------------------------------------
enum StudioSource {
  SS_OWN,      // the record gives it
  SS_BASE,     // a mod's record it starts from gives it
  SS_EDIT,     // an edit of a classic row gives it
  SS_CLASSIC,  // the classic row it starts from has it
  SS_NONE      // a field that is no column and no record gives
};
struct StudioValue {
  std::vector<long long> n;  // a column's values as the loader stores them, the scale taken off
  PortJson own;              // what the record itself gives, NONE when it gives nothing
  StudioSource source = SS_NONE;
  std::string from;          // the record or classic row it comes from, by name
};
// A record's field: for a column, what the loader builds for it and from where; for a field
// that is no column (name, looks, requires), the record's or its base's own value.
bool StudioGet(int t, const std::string &name, const std::string &key, StudioValue *pOut);
// The whole row: every column, in the loader's order. False when the record does not resolve,
// with why (a base that is not there, a base that is its own base).
bool StudioRow(int t, const std::string &name, std::vector<StudioValue> *pOut, std::string *pWhy);
// A column's first value, or dDefault.
long long StudioNumber(int t, const std::string &name, const std::string &key, long long nDefault = 0);
// A value a ref column holds, as the row's name ("gauss_rifle"), or its number when it has none.
std::string StudioRefName(int nRef, long long nRow);

// ---- edits ------------------------------------------------------------------------------------------
// One change: a member of a document set, added or taken away (NONE before: added; NONE after:
// taken away), or the image script's text replaced. Edits made together are one group, undone
// together.
struct StudioEdit {
  std::string doc;                // "types", "mod", "looks", "hud", "readout", "lang" or "iscript"
  std::vector<std::string> path;  // members from the document's root; empty for the whole document
  PortJson before;
  PortJson after;
  int nIndex = -1;                // the member's place in its object before, so undo puts it back there
  std::string sender;             // "you", "claude", or the outside tool's name
  std::string label;              // what it did, in words
  long long nTime = 0;            // unix seconds
  int nGroup = 0;
};
// The edits so far and the place undo has reached: edits before it are done, from it undone.
const std::vector<StudioEdit> &StudioHistory(void);
int StudioHistoryAt(void);
// The edits since the last save that are done (the unsaved count).
int StudioUnsaved(void);
// The last change's number: each edit, undo and redo makes it one more, so a pane knows to look
// again.
unsigned StudioChange(void);

// A group of edits: begun, each edit made, ended; or one edit as its own group. merge, when not
// empty, joins this group to the last one when that had the same merge and was made in the last
// few seconds (a drag, a stroke, typing in one field).
void StudioGroupBegin(const std::string &sender, const std::string &label, const std::string &merge = std::string());
bool StudioEditApply(StudioEdit edit, std::string *pWhy);
void StudioGroupEnd(void);

// The changes the commands make, each one group.
bool StudioSetField(int t, const std::string &name, const std::string &key, const PortJson &value,
                    const std::string &sender, const std::string &merge, std::string *pWhy);
bool StudioRevertField(int t, const std::string &name, const std::string &key, const std::string &sender,
                       std::string *pWhy);
bool StudioCreate(int t, const std::string &name, const PortJson &record, const std::string &sender,
                  std::string *pWhy);
bool StudioDelete(int t, const std::string &name, const std::string &sender, std::string *pWhy);
// The record's JSON replaced whole (the Raw view), parsed from text; a parse error says its line.
bool StudioSetRaw(int t, const std::string &name, const std::string &text, const std::string &sender,
                  std::string *pWhy);
bool StudioSetScript(const std::string &text, const std::string &sender, std::string *pWhy);
bool StudioUndo(std::string *pWhy);
bool StudioRedo(std::string *pWhy);

// A member of a document set (its parents made as it needs them), or taken away when value is
// NONE: for what is no record of a table, as "cards".
bool StudioPut(const std::string &doc, const std::vector<std::string> &path, const PortJson &value,
               const std::string &sender, const std::string &label, std::string *pWhy);

// Claude's asks (studio_claude.cpp). While bOn, the edits sent as "claude" make one group: they join
// group nJoin while it is the last one done, else the first of them starts a new one. With bPending
// that group is Claude's proposal: while it is done and the last, an edit of any other group is
// refused, and a save, an autosave and a push leave it out, until it is accepted
// (StudioPendingSet(0)) or undone.
void StudioClaudeJoin(bool bOn, bool bPending, int nJoin);
int StudioClaudeGroup(void);        // the group Claude's last edits made, 0 for none
int StudioPending(void);            // the proposal's group while it is done and the last, else 0
void StudioPendingSet(int nGroup);  // a group made the proposal, or 0: none (accepted)
// The documents without group nGroup's edits (bAside), or with them again; nGroup must be the last
// group done. No edit, no change: for reading what was before, a save and a push.
void StudioGroupAside(int nGroup, bool bAside);
// A document as it is without Claude's proposal: what a push gives the game, read when the game
// loads (the languages' translations); the document itself when nothing waits.
const PortJson &StudioDocAccepted(const std::string &doc);

// The pushes into the game so far (studio_push), numbered from 1: each one's state ("asked",
// "pushed" or "refused"), mod_push's answer as JSON text, and the model's change it pushed.
int StudioPushCount(void);
bool StudioPushInfo(int n, std::string *pState, std::string *pAnswer, unsigned *pnChange);

// ---- the mod's files (studio_files.cpp) --------------------------------------------------------------
// The files of the data folder, paths relative to it with / between folders, those whose extension
// is one of exts ("png,grp") when given; whether one is there; a file from anywhere copied into a
// folder of it (made as needed), its path given back, or one already inside named where it is.
// Files are no edits: undo leaves a file copied in.
std::vector<std::string> StudioFiles(const std::string &exts = std::string());
bool StudioFileExists(const std::string &rel);
bool StudioImport(const std::string &from, const std::string &folder, std::string *pRel, std::string *pWhy);

// ---- what the numbers work out to (studio_calc.cpp), by the classic rules (CUnit::Damage) -----------
// The damage type's share against a size (0 independent, 1 small, 2 medium, 3 large), 0 to 1.
double StudioSizeShare(int nType, int nSize);
// A unit's weapon by name, empty for none.
std::string StudioUnitWeapon(const std::string &unit, bool bAir);
// An attack's damage a hit at an upgrade level; one hit on a target's life after its armour and
// size, at least half a point; the attacks that kill a target, its armour and shields upgraded to
// nArmourLevel, 0 when it cannot; damage a second with every hit of an attack.
long long StudioAttackDamage(const std::string &weapon, int nLevel);
double StudioHitDamage(const std::string &weapon, int nLevel, int nSize, int nArmour);
int StudioAttacksToKill(const std::string &weapon, int nLevel, const std::string &target, int nArmourLevel);
double StudioDamagePerSecond(const std::string &weapon, int nLevel = 0);
// One attack on a target as it stands, by the numbers alone (the Balance workspace's fights, and
// StudioAttacksToKill): nPer a hit's damage in 1/256 (at least 128), nHits hits, the damage type,
// its share against the target's size in 1/256, the target's armour and its shields' upgrade in
// 1/256; the target's life and shields, in 1/256, made less.
void StudioStrikeNumbers(long long nPer, int nHits, int nType, int nShare, long long nArmour, long long nShieldUp, long long *pnLife,
                         long long *pnShields);
// A unit's top speed in pixels a second at Fastest: its flingy's, or, for a unit its image script
// walks, its Walking animation's moves (StudioScriptWalkSpeed, studio_art_anim.cpp, in pixels a frame).
double StudioUnitSpeed(const std::string &unit);
double StudioScriptWalkSpeed(const std::string &image);

// ---- JSON --------------------------------------------------------------------------------------------
// A tree as the files the studio writes have it: two spaces in, an object's members a line each,
// an array of numbers or names on one line.
std::string StudioJsonText(const PortJson &v);
PortJson StudioJsonNumber(double d);
PortJson StudioJsonString(const std::string &s);
PortJson StudioJsonObject(void);
// A mutable member of an object, or null.
PortJson *StudioJsonMember(PortJson &obj, const std::string &key);
// The member set, kept in its place or put at nIndex (at the end when -1); taken away, its place
// given back (-1 when it was not there).
void StudioJsonPut(PortJson &obj, const std::string &key, const PortJson &v, int nIndex = -1);
int StudioJsonErase(PortJson &obj, const std::string &key);
// Where a parse error is, as "line N: what" from PortJsonParse's offset.
std::string StudioJsonWhere(const std::string &text, const std::string &error);

// Called once at the start (studio_main.cpp): the commands registered, fields.json read.
void StudioModelInit(void);
void StudioCommandsInit(void);
// Each poll of the channel: the autosave's minute.
void StudioPoll(void);

#endif
