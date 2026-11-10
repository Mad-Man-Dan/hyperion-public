// port_modstatus.h -- a mod's statuses, auras and action blocks (docs/mod-data.md, "Statuses"
// and "Action blocks").
//
// A status is a record of types.json's "statuses": how long it lasts, how a new one stacks with
// one there already, and what it changes on the unit it is on (speed, armour, damage dealt,
// attack cooldown, moving and attacking, life lost or gained a second) and how it looks there. A
// weapon's "status" puts it on every unit the weapon damages, a unit's "aura" on the units near
// it every so often, and an order's "blocks" when the order is cast: a small program of blocks
// (damage, heal, energy, statuses, spawn, teleport, sound, sprite, wait, repeat) run frame by
// frame.
//
// Only a game that has the mod reaches any of it (g_hxModGame); the game's state of it, the
// statuses on each unit and the programs running, is saved in a Hyperion save (HXSU) and kept
// with the trainer's snapshots.
#ifndef PORT_MODSTATUS_H
#define PORT_MODSTATUS_H

#include <string>
#include <vector>

class CUnit;
struct PortJson;

// ---- the loader (port_moddata.cpp) ---------------------------------------------------------------
// A field of a table's record that this file reads, which the loader's columns do not have:
// a weapon's "status", a unit's "aura", an order's "blocks". t is the loader's table (0 units,
// 1 weapons, 5 orders).
bool ModStatusField(int t, const std::string &key);
// A record of the submission being loaded that may give one: its table, the row it takes or
// edits, and its JSON.
struct ModStatusRecord {
  int t;
  int id;
  std::string name;
  const PortJson *pJson;
};
// At the end of a submission's load: its "statuses" and its records' fields read, after every
// table's names are known. False with why when one does not load; then nothing of it is kept.
bool ModStatusLoad(const PortJson &types, int nSub, const std::vector<ModStatusRecord> &records, std::string *pWhy);
// The submissions past nSubs put away, as the loader puts back what it kept.
void ModStatusTrim(int nSubs);
// For each game, after the mod's rows are written (ModDataForGame): the game's tables of
// statuses, auras and programs, and every unit's statuses and every program cleared, for a game
// that has the mod; off for any other. A save's state is read after it (ModStatusLoadGame).
void ModStatusForGame();

// ---- the game's hooks (lang/*.cpp) ------------------------------------------------------------------
// Each is called only in a game that has the mod.
// A unit's speed (CUnit::ModifyGivenMovementSpeed, CUnit::GetModifiedSpeed) and its weapon's
// cooldown (CUnit::GetWeaponCD) as its statuses change them.
int ModStatusSpeed(CUnit *pUnit, int nSpeed);
int ModStatusCooldown(CUnit *pUnit, int nCooldown);
// In CUnit::Damage: the damage pSource deals, its statuses' share applied, 8.8 fixed point; the
// armour of the unit hit, 8.8, its statuses' added; and after the hit, the status the weapon
// puts on what it damages.
int ModStatusDamageDealt(CUnit *pSource, int nDamage);
// The damage a unit takes, by its own statuses (damage_taken).
int ModStatusDamageTaken(CUnit *pUnit, int nDamage);
int ModStatusArmor(CUnit *pUnit, int nArmor);
void ModStatusWeaponHit(CUnit *pUnit, int nWeapon, CUnit *pSource, int nSourcePlayer);
// As a unit is made (CUnit::sub_425710): its record cleared, so it takes nothing from the unit
// that had its place, whose id it may share.
void ModStatusUnitMade(CUnit *pUnit);
// In CUnit::FireSpell, as a mod's order is cast (the image script's castspell): true when the
// order has blocks, which then run in place of the weapon.
bool ModStatusCast(CUnit *pCaster);
// Each game frame, after the units' update (gameloop_update_game): the programs, the auras and
// the statuses' time.
void ModStatusFrame();

// ---- a save (port_hxsave.cpp) --------------------------------------------------------------------
int ModStatusSaveGame(void *pFile);
int ModStatusLoadGame(void *pFile);

// ---- the scripts (port_modlua.cpp) ----------------------------------------------------------------
// A status of the game's by its name: whether there is one; put on a unit for nPlayer, false when
// there is none or the unit does not live; taken off, every one for null; its stacks on a unit.
bool ModStatusHasNamed(const char *pszName);
bool ModStatusApplyNamed(CUnit *pUnit, const char *pszName, int nPlayer);
void ModStatusRemoveNamed(CUnit *pUnit, const char *pszName);
int ModStatusStacksNamed(CUnit *pUnit, const char *pszName);

// One status on a unit, as a reader wants it: its name, the words a player reads, how long it
// has left and of how long, its stacks, its own icon (a cmdicons frame, -1 for none) and which
// side it is on. A status's side is worked out from the way its fields point, which a mod may
// settle itself with "side".
struct ModStatusOn {
  std::string name, label;
  int nFramesLeft = 0, nFrames = 0;
  int nStacks = 0;
  int nIcon = -1;
  bool bGood = false;
  // what it changes on the unit, as its record gives them: percents, armour in points, and life
  // a second in 1/256 of a point. One stack's worth, which the stacks multiply.
  int nSpeed = 0, nArmor = 0, nDamage = 0, nTaken = 0, nCooldown = 0, nDps = 0, nRegen = 0;
  bool bNoMove = false, bNoAttack = false;
  // its "look", where the record gives one: the kind it names (-1 for the one its fields come
  // to), the colour its glyph is drawn in, the pattern over a wireframe ("" the kind's own,
  // "none" none), how strongly it asks for the one mark a wireframe has room for (-1 for the
  // kind's), and where it shows at all.
  int nKind = -1, nPriority = -1;
  unsigned int nHue = 0;
  bool bHue = false;
  std::string pattern;
  bool bShowReadout = true, bShowWire = true, bShowGroup = true;
};
// The statuses on a unit, the longest-held first; none for a unit with no status and in a game
// with no mod. Reading only: nothing of the game changes (the readout's hook, the live channel).
void ModStatusOnUnit(const CUnit *pUnit, std::vector<ModStatusOn> *pOut);
// The kind and the side a status comes to, from the status as types.json writes it: its "look"
// where that names either, else what its own fields come to (PortReadoutKindFrom, and the sum of
// the fields for the side). For a tool showing a definition with no game and no unit wearing it
// (Hyperion Studio), so the rule is asked rather than copied. False for a record that will not
// read at all, and the two answers are then left alone.
bool ModStatusKindOfDef(const PortJson &status, int *pnKind, bool *pbGood);

// ---- the live channel ------------------------------------------------------------------------------
// A unit's statuses as JSON members (",\"statuses\":[...]"), "" for none.
std::string ModStatusUnitJson(const CUnit *pUnit);

#endif
