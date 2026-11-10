// port_modstatus.cpp -- a mod's statuses, auras and action blocks (port_modstatus.h;
// docs/mod-data.md, "Statuses" and "Action blocks").
//
// The loader's part keeps what each submission brought, by its number, so a push that does
// not load and the put-away after a game drop it again (ModStatusTrim). A game builds its own
// tables from it (ModStatusForGame): the statuses by name, newest first, in the order of their
// names, so a given version of a mod numbers them the same in a game, its replay and its save.
//
// The game's part is two blocks of state: a record a unit, by its place in the unit table, of
// the statuses on it, and the programs of blocks running. A unit's record carries the unit's id
// when it was written, so a slot the unit left and another took is read as empty. Every change
// happens on the game's frame (ModStatusFrame), in the units' list order, or at a hit or a cast
// the game makes itself, so a replay plays it again as it went.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "port_modstatus.h"
#include "port_moddata.h"  // g_hxModGame, ModTypeByName
#include "port_modlua.h"    // ModLuaHasFunction, ModLuaBlock
#include "port_iscript.h"   // ImageScriptDamages117, SpriteScriptDamages117
#include "../hud/port_readout.h"  // PortReadoutKindOf: a status's "look" names its kind
#include "port_formats.h"  // PORT_SAVE_ID, PortSaveSection, PortLoadSectionIf
#include "../platform/port_json.h"
#include "../tools/port_control.h"     // PortControlCommand
#include "../replays/port_snap.h"      // PORT_SNAP_KEEP
#include "../gameplay/port_trigrun.h"  // PortTrigCreate
#include "../../h/typelimits.h"
#include "../../h/poollimits.h"   // POOL_UNITS_STORE, g_pools
#include "../../h/saveload.h"     // pfwrite, pfread, compress, decompress, get_upgrade_level
#include "../../h/CUnit.h"
#include "../../h/CSprite.h"
#include "../../h/CFlingy.h"
#include "../../h/CThingy.h"
#include "../../h/CBullet.h"
#include "../../h/TrigEnum.h"      // find_foreach_unit
#include "../../h/CUnitMisc.h"     // IsEnemyUnit
#include "../../h/CUnitZSpells.h"  // FindPlaceForUnit
#include "../../h/damage.h"        // compute_damage_from_types
#include "../../h/gamesnd.h"       // gamesnd_play_mtx
#include "../../h/handicap.h"      // HandicapShields
#include "../../gen/globals117.h"

#ifndef gubUnitArmor_g
extern BYTE gubUnitArmor_g[228] OC_UNALIGNED;
#endif

namespace {

// units.dat's flags this file reads
const DWORD kBuilding = 0x1, kSubunit = 0x10, kSpellcaster = 0x200000;
const DWORD kInvincible = 0x4000000;  // CUnit::dwFlags
const int kFps = 24;                  // game frames a second at Fastest
const int kSlots = 6;                 // statuses a unit holds at once
const int kRunsMax = 256;             // programs running at once
const int kDepthMax = 8;              // repeats inside repeats
const int kOpsMax = 256;              // blocks an order's program holds, nested ones counted

// ---- what the mod gives, by submission ------------------------------------------------------------

enum { STACK_REFRESH, STACK_STACK, STACK_IGNORE };
enum { WHO_ENEMIES, WHO_ALLIES, WHO_OWN, WHO_ALL };
enum { AT_TARGET, AT_CASTER };
enum {
  OP_FIRE, OP_DAMAGE, OP_HEAL, OP_ENERGY, OP_STATUS, OP_REMOVE, OP_SPAWN, OP_TELEPORT, OP_SOUND, OP_SHOW, OP_WAIT,
  OP_REPEAT, OP_SCRIPT, OP_KINDS
};
const char *const kOpNames[OP_KINDS] = {"fire",  "damage",   "heal",  "energy", "status", "remove_statuses",
                                        "spawn", "teleport", "sound", "show",   "wait",   "repeat",
                                        "script"};
const char *const kWho[] = {"enemies", "allies", "own", "all"};
// the damage types, weapons.dat's: independent, explosive, concussive, normal
const char *const kDamageTypes[] = {"independent", "explosive", "concussive", "normal"};

struct Def {
  std::string name;   // its key in "statuses"
  std::string label;  // "name", what a player reads
  int nSub = 0;
  int nIcon = -1;
  int nFrames = 0;
  int nStacking = STACK_REFRESH;
  int nMaxStacks = 1;
  int nSpeed = 0, nArmor = 0, nDamage = 0, nTaken = 0, nCooldown = 0;  // percents, and armour in points
  bool bNoMove = false, bNoAttack = false;
  int nDps = 0, nRegen = 0;  // a second, in 1/256 of a point
  int nOverlay = -1, nStart = -1, nEnd = -1;
  bool bBySize = false;  // the overlay is the image plus the unit's size (0 to 2), as Ensnare's are
  std::string end;       // the script its end calls, with the unit (on_end)
  int nSide = 0;         // "side": 1 a status that helps, -1 one that hurts, 0 worked out
  // "look": how the status shows itself, where the kind worked out from its fields reads wrong
  // (port_readout.h). -1 and "" leave each to the kind.
  int nKind = -1;
  int nPriority = -1;
  int nLookIcon = -1;
  unsigned int nHue = 0;
  bool bHue = false;
  std::string pattern;   // "" the kind's own, "none" no pattern
  bool bShowReadout = true, bShowWire = true, bShowGroup = true;
};

// Which side a status is on: what the mod said, else the way its fields point. A status that
// gives armour, speed, damage or life helps; one that takes them, hurts, burns or holds the unit
// still does not.
int SideOf(const Def &d) {
  if (d.nSide != 0)
    return d.nSide;
  int n = 0;
  n += d.nSpeed > 0 ? 1 : d.nSpeed < 0 ? -1 : 0;
  n += d.nArmor > 0 ? 1 : d.nArmor < 0 ? -1 : 0;
  n += d.nDamage > 0 ? 1 : d.nDamage < 0 ? -1 : 0;
  n += d.nTaken > 0 ? -1 : d.nTaken < 0 ? 1 : 0;
  n += d.nCooldown > 0 ? -1 : d.nCooldown < 0 ? 1 : 0;
  n += d.nRegen > 0 ? 1 : 0;
  n -= d.nDps > 0 ? 1 : 0;
  n -= d.bNoMove ? 1 : 0;
  n -= d.bNoAttack ? 1 : 0;
  return n >= 0 ? 1 : -1;
}

struct Op {
  int nKind = OP_WAIT;
  int nAt = AT_TARGET;
  int nWho = WHO_ENEMIES;
  bool bBuildings = false;
  int nRadius = 0;
  int nAmount = 0;
  int nShare = 0;     // damage: a share of each unit's most life, in percent, past its amount
  int nRef = -1;      // a weapon, unit, sound or sprite; a status by its index in the game
  int nFrames = 0;    // wait, a spawned unit's time
  int nType = 3;      // damage's type
  int nEnd = 0;       // past its nested blocks
  std::string status;
  std::string call;   // a script block's function (port_modlua.cpp)
};

struct WeaponLink {
  int nWeapon, nSub;
  std::string status;
};
// how many auras one unit may carry
enum { AURAS_PER_UNIT = 3 };
struct Aura {
  int nUnit = 0, nSub = 0;
  std::string status;
  int nRadius = 160, nWho = WHO_ALLIES, nEvery = kFps;
  bool bSelf = true, bBuildings = false;
  int nStatus = -1;  // in the game
};
struct Program {
  int nOrder, nSub;
  std::vector<Op> ops;
};

std::vector<Def> g_defs;
std::vector<WeaponLink> g_weaponLinks;
std::vector<Aura> g_auras;
std::vector<Program> g_programs;

// ---- the game's tables ---------------------------------------------------------------------------

struct Game {
  bool bOn = false;
  std::vector<Def> statuses;  // by index, in the order of their names
  std::vector<short> weaponStatus;
  std::vector<short> unitAura;   // the first of a unit's auras in auras, -1 for none
  std::vector<BYTE> unitAuras;   // how many it has, up to AURAS_PER_UNIT
  std::vector<Aura> auras;
  std::vector<short> orderProgram;  // an index into programs
  std::vector<std::vector<Op>> programs;
};
Game g_game;

int GameStatus(const std::string &name) {
  for (size_t i = 0; i < g_game.statuses.size(); ++i)
    if (g_game.statuses[i].name == name)
      return static_cast<int>(i);
  return -1;
}

// ---- the game's state -----------------------------------------------------------------------------

#pragma pack(push, 1)
struct Slot {
  WORD wStatus;  // 1 + its index in the game's statuses, 0 for none
  WORD wFrames;  // left
  BYTE bStacks;
  BYTE bPlayer;  // who put it on
  WORD wCarry;   // the life a tick could not give whole, in 1/256 of a point over 3
};
struct UnitState {
  DWORD dwId;  // the unit's id as it was written; any other unit in the slot reads it as empty
  WORD awAura[AURAS_PER_UNIT];  // frames to each of its auras' next pulse
  BYTE bAny;                    // statuses on it
  BYTE bPad;
  Slot a[kSlots];
};
struct RunFrame {
  WORD wOp;    // the repeat
  WORD wLeft;  // times still to go
};
struct Run {
  WORD wOrder;
  BYTE bPlayer;
  BYTE bDepth;
  DWORD dwCaster, dwTarget;
  int x, y;
  WORD wPc;
  WORD wWait;
  RunFrame stack[kDepthMax];
};
#pragma pack(pop)

UnitState g_units[POOL_UNITS_STORE];
PORT_SNAP_KEEP_POOL(g_units, g_pools.units);
Run g_runs[kRunsMax];
PORT_SNAP_KEEP(g_runs);
int g_nRuns;
PORT_SNAP_KEEP(g_nRuns);

const ULONG kSection = PORT_SAVE_ID('H', 'X', 'S', 'U');

// A fire block's weapon is fired as the cast fires its own (CUnit::FireSpell), which is no new cast.
bool g_bFiring;

// ---- reading the mod -------------------------------------------------------------------------------

bool Refuse(std::string *pWhy, const std::string &s) {
  *pWhy = s;
  return false;
}

// A number field, or dDefault when it is not there; false when it is there and not a number in
// [dMin, dMax].
bool Number(const PortJson &o, const char *pszKey, double dMin, double dMax, double dDefault, double *pd,
            const std::string &where, std::string *pWhy) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE) {
    *pd = dDefault;
    return true;
  }
  if (v.kind != PortJson::NUMBER || v.d < dMin || v.d > dMax) {
    char sz[96];
    snprintf(sz, sizeof sz, " takes a number from %g to %g", dMin, dMax);
    return Refuse(pWhy, where + "." + pszKey + sz);
  }
  *pd = v.d;
  return true;
}

bool Bool(const PortJson &o, const char *pszKey, bool bDefault, bool *pb, const std::string &where, std::string *pWhy) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE) {
    *pb = bDefault;
    return true;
  }
  if (v.kind != PortJson::BOOL)
    return Refuse(pWhy, where + "." + pszKey + " takes true or false");
  *pb = v.b;
  return true;
}

// A row of another table by name or number; -1 with why when there is none.
bool Row(const PortJson &o, const char *pszKey, const char *pszTable, bool bNeeded, int *pn, const std::string &where,
         std::string *pWhy) {
  const PortJson &v = o[pszKey];
  *pn = -1;
  if (v.kind == PortJson::NONE)
    return !bNeeded || Refuse(pWhy, where + " needs " + pszKey);
  if (v.kind == PortJson::NUMBER && v.d >= 0)
    *pn = static_cast<int>(v.d);
  else if (v.kind == PortJson::STRING)
    *pn = ModTypeByName(pszTable, v.s.c_str());
  if (*pn < 0)
    return Refuse(pWhy, where + "." + pszKey + " names no " + std::string(pszTable) + " " +
                            (v.kind == PortJson::STRING ? v.s : PortJsonWrite(v)) + " the mod or the original has");
  return true;
}

bool Choice(const PortJson &o, const char *pszKey, const char *const *ppsz, int n, int nDefault, int *pn,
            const std::string &where, std::string *pWhy) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE) {
    *pn = nDefault;
    return true;
  }
  for (int i = 0; i < n; ++i)
    if (v.kind == PortJson::STRING && v.s == ppsz[i]) {
      *pn = i;
      return true;
    }
  std::string list;
  for (int i = 0; i < n; ++i)
    list += std::string(i ? ", " : "") + ppsz[i];
  return Refuse(pWhy, where + "." + pszKey + " is one of " + list);
}

bool Only(const PortJson &o, const std::vector<const char *> &keys, const std::string &where, std::string *pWhy) {
  if (o.kind != PortJson::OBJECT)
    return Refuse(pWhy, where + " is an object");
  for (const PortJsonMember &m : o.members) {
    bool bKnown = m.first == "comment";
    for (const char *psz : keys)
      bKnown = bKnown || m.first == psz;
    if (!bKnown)
      return Refuse(pWhy, where + " has no field " + m.first);
  }
  return true;
}

int Frames(double dSeconds) {
  return static_cast<int>(floor(dSeconds * kFps + 0.5));
}

bool ReadDef(const std::string &name, const PortJson &o, int nSub, Def *pDef, std::string *pWhy) {
  const std::string where = "statuses " + name;
  if (!Only(o, {"name", "icon", "side", "look", "duration", "stacking", "max_stacks", "move_speed", "armor", "damage_dealt",
                "damage_taken", "attack_cooldown", "cannot_move", "cannot_attack", "damage_per_second",
                "life_per_second", "overlay", "overlay_by_size", "start_image", "end_image", "on_end"},
            where, pWhy))
    return false;
  Def d;
  d.name = name;
  d.label = o["name"].Str(name.c_str());
  d.nSub = nSub;
  double v = 0;
  if (!Number(o, "icon", 0, 65535, -1, &v, where, pWhy))
    return false;
  d.nIcon = static_cast<int>(v);
  if (o["duration"].kind == PortJson::NONE)
    return Refuse(pWhy, where + " needs a duration, in seconds");
  if (!Number(o, "duration", 1.0 / kFps, 2700, 0, &v, where, pWhy))
    return false;
  d.nFrames = std::max(1, Frames(v));
  static const char *const kStacking[] = {"refresh", "stack", "ignore"};
  if (!Choice(o, "stacking", kStacking, 3, STACK_REFRESH, &d.nStacking, where, pWhy) ||
      !Number(o, "max_stacks", 1, 255, d.nStacking == STACK_STACK ? 5 : 1, &v, where, pWhy))
    return false;
  d.nMaxStacks = d.nStacking == STACK_STACK ? static_cast<int>(v) : 1;
  if (!Number(o, "move_speed", -100, 400, 0, &v, where, pWhy))
    return false;
  d.nSpeed = static_cast<int>(v);
  if (!Number(o, "armor", -100, 100, 0, &v, where, pWhy))
    return false;
  d.nArmor = static_cast<int>(v);
  if (!Number(o, "damage_dealt", -100, 400, 0, &v, where, pWhy))
    return false;
  d.nDamage = static_cast<int>(v);
  if (!Number(o, "damage_taken", -100, 400, 0, &v, where, pWhy))
    return false;
  d.nTaken = static_cast<int>(v);
  if (!Number(o, "attack_cooldown", -90, 400, 0, &v, where, pWhy))
    return false;
  d.nCooldown = static_cast<int>(v);
  if (!Bool(o, "cannot_move", false, &d.bNoMove, where, pWhy) || !Bool(o, "cannot_attack", false, &d.bNoAttack, where, pWhy) ||
      !Bool(o, "overlay_by_size", false, &d.bBySize, where, pWhy))
    return false;
  if (!Number(o, "damage_per_second", 0, 5000, 0, &v, where, pWhy))
    return false;
  d.nDps = static_cast<int>(floor(v * 256 + 0.5));
  if (!Number(o, "life_per_second", -5000, 5000, 0, &v, where, pWhy))
    return false;
  d.nRegen = static_cast<int>(floor(v * 256 + 0.5));
  if (!Row(o, "overlay", "images", false, &d.nOverlay, where, pWhy) || !Row(o, "start_image", "images", false, &d.nStart, where, pWhy) ||
      !Row(o, "end_image", "images", false, &d.nEnd, where, pWhy))
    return false;
  // A weapon's own picture damages in its death animation, which is how a shot applies its
  // damage. A unit's death plays that animation on every picture the unit carries, so one of
  // these worn as a status would be asked to damage with no shot behind it: nothing happens,
  // and the picture is a shot's rather than a mark on a unit. Said, not refused, so a mod with
  // one still loads.
  static const char *const kaFields[3] = {"overlay", "start_image", "end_image"};
  const int anImages[3] = {d.nOverlay, d.nStart, d.nEnd};
  for (int i = 0; i < 3; ++i) {
    const int nAnim = ImageScriptDamages117(anImages[i]);
    if (nAnim >= 0)
      ModDataNote((where + "'s " + kaFields[i] + " is a weapon's own picture: it damages in its " +
                   IscriptAnimName(nAnim) + " animation" +
                   (nAnim == 0 ? ", as soon as it is drawn" : ", which a unit's death plays") +
                   ". It wants a picture meant to sit on a unit").c_str());
  }
  d.end = o["on_end"].Str("");
  if (!d.end.empty() && !ModLuaHasFunction(d.end))
    return Refuse(pWhy, where + "'s on_end names " + d.end +
                            ", which no script of the mod gives (a function at a script's top)");
  // "side": which side the status is on, where the way its fields point says it wrong. It is
  // read by what shows the status, the unit readout and the status's own look.
  static const char *const kSide[] = {"good", "bad"};
  int nSide = -1;
  if (o["side"].kind != PortJson::NONE) {
    if (!Choice(o, "side", kSide, 2, -1, &nSide, where, pWhy))
      return false;
    d.nSide = nSide == 0 ? 1 : -1;
  }
  // "look": how the status shows itself on the readout, where the kind worked out from its
  // fields reads wrong. Every key may be left out and what is left out is the kind's own.
  const PortJson &look = o["look"];
  if (look.kind != PortJson::NONE) {
    const std::string lw = where + "'s look";
    if (look.kind != PortJson::OBJECT)
      return Refuse(pWhy, lw + " is an object");
    if (!Only(look, {"kind", "icon", "colour", "hue", "pattern", "show", "priority"}, lw, pWhy))
      return false;
    if (look["kind"].kind != PortJson::NONE) {
      d.nKind = PortReadoutKindOf(look["kind"].Str("").c_str());
      if (d.nKind < 0) {
        std::string list;
        for (int i = 0; i < PORT_READOUT_KINDS; ++i)
          list += std::string(i ? ", " : "") + PortReadoutKindName(i);
        return Refuse(pWhy, lw + "'s kind is one of " + list);
      }
    }
    if (!Number(look, "icon", 0, 65535, -1, &v, lw, pWhy))
      return false;
    d.nLookIcon = static_cast<int>(v);
    if (!Number(look, "priority", 0, 1000, -1, &v, lw, pWhy))
      return false;
    d.nPriority = static_cast<int>(v);
    // "hue" and "colour" are the same thing: the colour the glyph and the pattern are drawn in.
    // A readout's own words for a side ("for", "against") leave it to the side's colour.
    const std::string hue = look["hue"].Str(look["colour"].Str("").c_str());
    if (!hue.empty() && hue != "for" && hue != "against") {
      unsigned int rgb = 0;
      if (hue.size() != 7 || hue[0] != '#' || sscanf(hue.c_str() + 1, "%6x", &rgb) != 1)
        return Refuse(pWhy, lw + "'s hue is #rrggbb, or for or against to take its side's colour");
      d.nHue = rgb;
      d.bHue = true;
    }
    d.pattern = look["pattern"].Str("");
    const PortJson &show = look["show"];
    if (show.kind != PortJson::NONE) {
      if (show.kind != PortJson::ARRAY)
        return Refuse(pWhy, lw + "'s show is a list of readout, wire and group");
      d.bShowReadout = d.bShowWire = d.bShowGroup = false;
      for (size_t i = 0; i < show.size(); ++i) {
        const std::string one = show.At(i).Str("");
        if (one == "readout")
          d.bShowReadout = true;
        else if (one == "wire")
          d.bShowWire = true;
        else if (one == "group")
          d.bShowGroup = true;
        else
          return Refuse(pWhy, lw + "'s show names " + one + ", which is not readout, wire or group");
      }
    }
  }
  *pDef = d;
  return true;
}

bool Known(const std::vector<Def> &defs, const std::string &name) {
  for (const Def &d : defs)
    if (d.name == name)
      return true;
  return false;
}

bool StatusName(const PortJson &o, const char *pszKey, bool bNeeded, const std::vector<Def> &defs, std::string *pName,
                const std::string &where, std::string *pWhy) {
  const PortJson &v = o[pszKey];
  if (v.kind == PortJson::NONE)
    return !bNeeded || Refuse(pWhy, where + " needs " + pszKey + ", a status by name");
  if (v.kind != PortJson::STRING || !Known(defs, v.s))
    return Refuse(pWhy, where + "." + pszKey + " names no status of the mod's (" +
                            (v.kind == PortJson::STRING ? v.s : PortJsonWrite(v)) + ")");
  *pName = v.s;
  return true;
}

std::string BlockList(void) {
  std::string s;
  for (int i = 0; i < OP_KINDS; ++i)
    s += std::string(i ? ", " : "") + kOpNames[i];
  return s;
}

// An order's blocks into ops, nested blocks after the block that holds them.
bool ReadBlocks(const PortJson &list, const std::string &where, int nDepth, const std::vector<Def> &defs, std::vector<Op> *pOps,
                std::string *pWhy) {
  if (list.kind != PortJson::ARRAY)
    return Refuse(pWhy, where + " is a list of blocks");
  if (nDepth > kDepthMax)
    return Refuse(pWhy, where + " nests blocks deeper than 8");
  for (size_t i = 0; i < list.items.size(); ++i) {
    const PortJson &b = list.items[i];
    const std::string at = where + "[" + std::to_string(i) + "]";
    if (b.kind != PortJson::OBJECT)
      return Refuse(pWhy, at + " is a block, an object with \"do\"");
    const std::string what = b["do"].Str();
    int k = 0;
    while (k < OP_KINDS && what != kOpNames[k])
      ++k;
    if (k == OP_KINDS)
      return Refuse(pWhy, at + " does \"" + what + "\", which is no block the engine runs; a block does one of " + BlockList());
    if (pOps->size() >= static_cast<size_t>(kOpsMax))
      return Refuse(pWhy, where + " has more than 256 blocks");
    Op op;
    op.nKind = k;
    double v = 0;
    const char *pszWhoDefault = k == OP_DAMAGE || k == OP_STATUS ? "enemies" : "allies";
    int nWhoDefault = 0;
    for (int w = 0; w < 4; ++w)
      if (strcmp(kWho[w], pszWhoDefault) == 0)
        nWhoDefault = w;
    static const char *const kAt[] = {"target", "caster"};
    const std::vector<const char *> area = {"do", "at", "radius", "targets", "buildings"};
    auto areaOf = [&](std::vector<const char *> extra) {
      std::vector<const char *> keys = area;
      keys.insert(keys.end(), extra.begin(), extra.end());
      if (!Only(b, keys, at, pWhy) || !Choice(b, "at", kAt, 2, AT_TARGET, &op.nAt, at, pWhy) ||
          !Number(b, "radius", 0, 2048, 0, &v, at, pWhy))
        return false;
      op.nRadius = static_cast<int>(v);
      return Choice(b, "targets", kWho, 4, nWhoDefault, &op.nWho, at, pWhy) &&
             Bool(b, "buildings", false, &op.bBuildings, at, pWhy);
    };
    switch (k) {
    case OP_FIRE:
      if (!Only(b, {"do", "weapon"}, at, pWhy) || !Row(b, "weapon", "weapons", false, &op.nRef, at, pWhy))
        return false;
      break;
    case OP_DAMAGE:
      if (!areaOf({"amount", "share", "damage_type"}) || !Number(b, "amount", 1, 10000, 0, &v, at, pWhy))
        return false;
      op.nAmount = static_cast<int>(v);
      if (!Number(b, "share", 1, 100, 0, &v, at, pWhy))
        return false;
      op.nShare = static_cast<int>(v);
      if (b["amount"].kind == PortJson::NONE && op.nShare == 0)
        return Refuse(pWhy, at + " needs amount, the damage, or share, a share of the unit's most life in percent");
      if (!Choice(b, "damage_type", kDamageTypes, 4, 3, &op.nType, at, pWhy))
        return false;
      break;
    case OP_HEAL:
    case OP_ENERGY:
      if (!areaOf({"amount"}) || !Number(b, "amount", k == OP_HEAL ? 1 : -250, 10000, 0, &v, at, pWhy))
        return false;
      if (b["amount"].kind == PortJson::NONE)
        return Refuse(pWhy, at + " needs amount");
      op.nAmount = static_cast<int>(v);
      break;
    case OP_STATUS:
      if (!areaOf({"status"}) || !StatusName(b, "status", true, defs, &op.status, at, pWhy))
        return false;
      break;
    case OP_REMOVE:
      if (!areaOf({"status"}) || !StatusName(b, "status", false, defs, &op.status, at, pWhy))
        return false;
      break;
    case OP_SPAWN:
      if (!Only(b, {"do", "at", "unit", "count", "seconds"}, at, pWhy) || !Choice(b, "at", kAt, 2, AT_TARGET, &op.nAt, at, pWhy) ||
          !Row(b, "unit", "units", true, &op.nRef, at, pWhy) || !Number(b, "count", 1, 64, 1, &v, at, pWhy))
        return false;
      op.nAmount = static_cast<int>(v);
      if (!Number(b, "seconds", 0, 2700, 0, &v, at, pWhy))
        return false;
      op.nFrames = Frames(v);
      break;
    case OP_TELEPORT:
      if (!Only(b, {"do"}, at, pWhy))
        return false;
      break;
    case OP_SOUND:
      if (!Only(b, {"do", "at", "sound"}, at, pWhy) || !Choice(b, "at", kAt, 2, AT_TARGET, &op.nAt, at, pWhy) ||
          !Row(b, "sound", "sounds", true, &op.nRef, at, pWhy))
        return false;
      break;
    case OP_SHOW:
      if (!Only(b, {"do", "at", "sprite"}, at, pWhy) || !Choice(b, "at", kAt, 2, AT_TARGET, &op.nAt, at, pWhy) ||
          !Row(b, "sprite", "sprites", true, &op.nRef, at, pWhy))
        return false;
      // the same for a picture shown on its own, for the same reason (ReadDef's note)
      {
        const int nShown = ImageScriptDamages117(ModDataSpriteImage(op.nRef));
        if (nShown >= 0)
          ModDataNote((at + "'s sprite is a weapon's own picture: it damages in its " +
                       IscriptAnimName(nShown) + " animation" +
                       (nShown == 0 ? ", as soon as it is shown" : ", if that animation is reached") +
                       ". It wants a picture meant to be shown on its own").c_str());
      }
      break;
    case OP_WAIT:
      if (!Only(b, {"do", "seconds", "blocks"}, at, pWhy) || !Number(b, "seconds", 1.0 / kFps, 2700, 1, &v, at, pWhy))
        return false;
      op.nFrames = std::max(1, Frames(v));
      break;
    case OP_SCRIPT:
      if (!Only(b, {"do", "call"}, at, pWhy))
        return false;
      op.call = b["call"].Str();
      if (op.call.empty())
        return Refuse(pWhy, at + " needs call, a function of the mod's scripts");
      if (!ModLuaHasFunction(op.call))
        return Refuse(pWhy, at + ".call names " + op.call + ", which no script of the mod gives (a function at a script's top)");
      break;
    case OP_REPEAT:
      if (!Only(b, {"do", "times", "blocks"}, at, pWhy) || !Number(b, "times", 1, 1000, 2, &v, at, pWhy))
        return false;
      op.nAmount = static_cast<int>(v);
      if (b["blocks"].kind == PortJson::NONE || b["blocks"].items.empty())
        return Refuse(pWhy, at + " needs blocks, the blocks it repeats");
      break;
    }
    const size_t nAt = pOps->size();
    pOps->push_back(op);
    if ((k == OP_WAIT || k == OP_REPEAT) && b["blocks"].kind != PortJson::NONE &&
        !ReadBlocks(b["blocks"], at + ".blocks", nDepth + 1, defs, pOps, pWhy))
      return false;
    (*pOps)[nAt].nEnd = static_cast<int>(pOps->size());
  }
  return true;
}

// ---- the game's tables from what loaded --------------------------------------------------------------

void Build(void) {
  // the statuses by name, the newest submission's, in the order of their names
  std::vector<std::string> before;
  for (const Def &d : g_game.statuses)
    before.push_back(d.name);
  Game g;
  g.bOn = true;
  for (const Def &d : g_defs) {
    bool bDone = false;
    for (Def &e : g.statuses)
      if (e.name == d.name) {
        if (d.nSub >= e.nSub)
          e = d;
        bDone = true;
      }
    if (!bDone)
      g.statuses.push_back(d);
  }
  std::sort(g.statuses.begin(), g.statuses.end(), [](const Def &a, const Def &b) { return a.name < b.name; });
  auto index = [&](const std::string &name) {
    for (size_t i = 0; i < g.statuses.size(); ++i)
      if (g.statuses[i].name == name)
        return static_cast<int>(i);
    return -1;
  };
  g.weaponStatus.assign(WEAPON_TYPES_MAX, -1);
  for (const WeaponLink &l : g_weaponLinks)
    if (l.nWeapon >= 0 && l.nWeapon < WEAPON_TYPES_MAX)
      g.weaponStatus[l.nWeapon] = static_cast<short>(index(l.status));
  // a unit's auras lie together, so the frame reads them as a run from the first
  g.unitAura.assign(UNIT_TYPES_MAX, -1);
  g.unitAuras.assign(UNIT_TYPES_MAX, 0);
  for (const Aura &a : g_auras)
    if (a.nUnit >= 0 && a.nUnit < UNIT_TYPES_MAX && g.unitAuras[a.nUnit] < AURAS_PER_UNIT) {
      Aura b = a;
      b.nStatus = index(a.status);
      if (g.unitAuras[a.nUnit] == 0)
        g.unitAura[a.nUnit] = static_cast<short>(g.auras.size());
      else if (g.unitAura[a.nUnit] + g.unitAuras[a.nUnit] != static_cast<short>(g.auras.size()))
        continue;  // another unit's aura came between: this one is passed over
      ++g.unitAuras[a.nUnit];
      g.auras.push_back(b);
    }
  g.orderProgram.assign(ORDER_TYPES_MAX, -1);
  for (const Program &p : g_programs)
    if (p.nOrder >= 0 && p.nOrder < ORDER_TYPES_MAX) {
      std::vector<Op> ops = p.ops;
      for (Op &op : ops)
        if (!op.status.empty())
          op.nRef = index(op.status);
      g.orderProgram[p.nOrder] = static_cast<short>(g.programs.size());
      g.programs.push_back(ops);
    }
  // a push in a game: the statuses on the units keep theirs by name
  if (g_game.bOn && !before.empty()) {
    for (int u = 0; u < g_pools.units; ++u)
      for (Slot &s : g_units[u].a)
        if (s.wStatus != 0) {
          const int n = s.wStatus - 1 < static_cast<int>(before.size()) ? index(before[s.wStatus - 1]) : -1;
          s.wStatus = static_cast<WORD>(n + 1);
        }
  }
  g_game = g;
}

// ---- the units' statuses --------------------------------------------------------------------------------

int IndexOf(const CUnit *pUnit) {
  return static_cast<int>(pUnit - CUnit::Head());
}

// The unit a subunit (a turret) is part of: its statuses are the whole unit's.
CUnit *Whole(CUnit *pUnit) {
  if ((gulUnitTypeFlags[pUnit->wType] & kSubunit) && pUnit->subunit)
    return pUnit->subunit;
  return pUnit;
}

// The unit's record, null when it has none or another unit's.
UnitState *StateOf(CUnit *pUnit) {
  const int i = IndexOf(pUnit);
  if (i < 0 || i >= g_pools.units)
    return nullptr;
  UnitState &s = g_units[i];
  if (s.dwId == 0 || s.dwId != static_cast<DWORD>(pUnit->GetUnitID()))
    return nullptr;
  return &s;
}

// The unit's record, claimed for it.
UnitState *Claim(CUnit *pUnit) {
  const int i = IndexOf(pUnit);
  if (i < 0 || i >= g_pools.units)
    return nullptr;
  UnitState &s = g_units[i];
  const DWORD id = static_cast<DWORD>(pUnit->GetUnitID());
  if (s.dwId != id) {
    memset(&s, 0, sizeof s);
    s.dwId = id;
  }
  return &s;
}

const Def *DefOf(const Slot &s) {
  return s.wStatus != 0 && s.wStatus <= g_game.statuses.size() ? &g_game.statuses[s.wStatus - 1] : nullptr;
}

bool Alive(CUnit *pUnit) {
  return pUnit != nullptr && pUnit->pSprite != nullptr && pUnit->lHitPoints > 0 && !pUnit->IsDeadOrDying();
}

bool Hidden(CUnit *pUnit) {
  return (pUnit->pSprite->flags.b & 0x20) != 0 || (pUnit->dwFlags & CUnit::UF_Burrowed) != 0;
}

int OverlayImage(CUnit *pUnit, const Def &d) {
  return d.bBySize ? d.nOverlay + pUnit->GetSizeType() : d.nOverlay;
}

void StatusEnd(CUnit *pUnit, UnitState &st, Slot &s) {
  const Def *d = DefOf(s);
  memset(&s, 0, sizeof s);
  if (st.bAny)
    --st.bAny;
  if (d == nullptr)
    return;
  if (d->nOverlay >= 0) {
    bool bShared = false;  // another status on it shows the same
    for (const Slot &o : st.a)
      if (const Def *e = DefOf(o))
        bShared = bShared || (e->nOverlay == d->nOverlay && e->bBySize == d->bBySize);
    if (!bShared)
      pUnit->RemoveImageOverlay(OverlayImage(pUnit, *d), OverlayImage(pUnit, *d));
  }
  if (d->nEnd >= 0 && !Hidden(pUnit))
    pUnit->CreateOverlay(d->nEnd, 0, 0, 0);
  if (d->nSpeed != 0 || d->bNoMove)
    pUnit->ResetMovementSpeed();
  if (!d->end.empty())
    ModLuaStatusEnded(pUnit, d->end, d->name);
}

void Apply(CUnit *pUnit, int nStatus, int nPlayer) {
  if (nStatus < 0 || nStatus >= static_cast<int>(g_game.statuses.size()) || !Alive(pUnit))
    return;
  pUnit = Whole(pUnit);
  if (pUnit->dwFlags & kInvincible)
    return;
  const Def &d = g_game.statuses[nStatus];
  UnitState *st = Claim(pUnit);
  if (st == nullptr)
    return;
  for (Slot &s : st->a)
    if (s.wStatus == nStatus + 1) {
      if (d.nStacking == STACK_IGNORE)
        return;
      s.wFrames = static_cast<WORD>(d.nFrames);
      s.bPlayer = static_cast<BYTE>(nPlayer);
      if (d.nStacking == STACK_STACK && s.bStacks < d.nMaxStacks) {
        ++s.bStacks;
        if (d.nSpeed != 0)
          pUnit->ResetMovementSpeed();
      }
      return;
    }
  Slot *pFree = nullptr;
  for (Slot &s : st->a)
    if (s.wStatus == 0 && pFree == nullptr)
      pFree = &s;
  if (pFree == nullptr)
    return;  // the unit holds as many as it can
  pFree->wStatus = static_cast<WORD>(nStatus + 1);
  pFree->wFrames = static_cast<WORD>(d.nFrames);
  pFree->bStacks = 1;
  pFree->bPlayer = static_cast<BYTE>(nPlayer);
  pFree->wCarry = 0;
  ++st->bAny;
  if (!Hidden(pUnit)) {
    if (d.nStart >= 0)
      pUnit->CreateOverlay(d.nStart, 0, 0, 0);
    if (d.nOverlay >= 0 && !pUnit->pSprite->OverlayGetImage(OverlayImage(pUnit, d), OverlayImage(pUnit, d)) &&
        !(pUnit->subunit && pUnit->subunit->pSprite->OverlayGetImage(OverlayImage(pUnit, d), OverlayImage(pUnit, d))))
      pUnit->CreateOverlay(OverlayImage(pUnit, d), 0, 0, 0);
  }
  if (d.nSpeed != 0 || d.bNoMove)
    pUnit->ResetMovementSpeed();
}

// One status taken off, or every one when nStatus is -1.
void Remove(CUnit *pUnit, int nStatus) {
  pUnit = Whole(pUnit);
  UnitState *st = StateOf(pUnit);
  if (st == nullptr)
    return;
  for (Slot &s : st->a)
    if (s.wStatus != 0 && (nStatus < 0 || s.wStatus == nStatus + 1))
      StatusEnd(pUnit, *st, s);
}

// The sum of a share over a unit's statuses, each times its stacks.
template <typename F>
int Sum(CUnit *pUnit, F f, bool *pbAny = nullptr) {
  UnitState *st = StateOf(Whole(pUnit));
  if (st == nullptr || st->bAny == 0)
    return 0;
  int n = 0;
  for (const Slot &s : st->a)
    if (const Def *d = DefOf(s)) {
      n += f(*d) * s.bStacks;
      if (pbAny)
        *pbAny = *pbAny || f(*d) != 0;
    }
  return n;
}

bool Any(CUnit *pUnit, bool Def::*pb) {
  UnitState *st = StateOf(Whole(pUnit));
  if (st == nullptr || st->bAny == 0)
    return false;
  for (const Slot &s : st->a)
    if (const Def *d = DefOf(s))
      if ((*d).*pb)
        return true;
  return false;
}

// Life lost or given over the 8 frames since the last tick: the second's amount over 3, the
// remainder kept for the next.
int Tick(int nPerSecond, int nStacks, WORD *pwCarry) {
  const long long n = static_cast<long long>(nPerSecond) * nStacks;
  const long long nAbs = (n < 0 ? -n : n) + *pwCarry;
  *pwCarry = static_cast<WORD>(nAbs % 3);
  return static_cast<int>(n < 0 ? -(nAbs / 3) : nAbs / 3);
}

// A unit's statuses for one frame: their time, their life a second, what they hold still.
void UnitFrame(CUnit *pUnit, UnitState &st) {
  bool bNoMove = false, bNoAttack = false;
  for (Slot &s : st.a) {
    const Def *d = DefOf(s);
    if (d == nullptr)
      continue;
    if (s.wFrames > 0)
      --s.wFrames;
    if ((s.wFrames & 7) == 0 && !Hidden(pUnit) && pUnit->lHitPoints > 0) {
      if (d->nDps > 0) {
        const int n = Tick(d->nDps, s.bStacks, &s.wCarry);
        if (n > 0)
          pUnit->DamageApply(n, nullptr, s.bPlayer, 0);
      }
      if (d->nRegen != 0 && pUnit->lHitPoints > 0) {
        const int n = Tick(d->nRegen, s.bStacks, &s.wCarry);
        // a loss leaves at least 1 point of life, as Plague's does
        const int nNew = n < 0 ? std::max<int>(pUnit->lHitPoints + n, std::min<int>(pUnit->lHitPoints, 256)) : pUnit->lHitPoints + n;
        if (nNew != pUnit->lHitPoints)
          pUnit->SetLife(nNew);
      }
    }
    if (s.wFrames == 0) {
      StatusEnd(pUnit, st, s);
      continue;
    }
    bNoMove = bNoMove || d->bNoMove;
    bNoAttack = bNoAttack || d->bNoAttack;
  }
  if (bNoMove) {
    CFlingy *f = reinterpret_cast<CFlingy *>(pUnit);
    f->lCurrentSpeed = 0;
    f->lTargetSpeed = 0;
    f->lVelocityX = 0;
    f->lVelocityY = 0;
  }
  if (bNoAttack)
    for (CUnit *p : {pUnit, pUnit->subunit})
      if (p != nullptr)
        for (int i = 0; i < 2; ++i)
          if ((&p->bUnion54)[i] < 2)
            (&p->bUnion54)[i] = 2;
}

// ---- areas ------------------------------------------------------------------------------------------

struct Area {
  int x, y, r;
  int nPlayer, nWho;
  bool bBuildings;
  std::vector<CUnit *> *pOut;
};

bool Picks(CUnit *pUnit, int nPlayer, int nWho, bool bBuildings) {
  if (!Alive(pUnit) || (pUnit->pSprite->flags.b & 0x20) || (gulUnitTypeFlags[pUnit->wType] & kSubunit))
    return false;
  if (!bBuildings && (gulUnitTypeFlags[pUnit->wType] & kBuilding))
    return false;
  switch (nWho) {
  case WHO_ENEMIES: return IsEnemyUnit(nPlayer, pUnit) != 0;
  case WHO_ALLIES: return IsEnemyUnit(nPlayer, pUnit) == 0;
  case WHO_OWN: return pUnit->bPlayer == nPlayer;
  default: return true;
  }
}

int __fastcall AreaPick(CUnit *pUnit, void *pv) {
  Area &a = *static_cast<Area *>(pv);
  const long long dx = static_cast<int>(pUnit->pSprite->wPosX) - a.x, dy = static_cast<int>(pUnit->pSprite->wPosY) - a.y;
  if (dx * dx + dy * dy <= static_cast<long long>(a.r) * a.r && Picks(pUnit, a.nPlayer, a.nWho, a.bBuildings))
    a.pOut->push_back(pUnit);
  return 0;
}

// The units within r of x, y that nWho picks for nPlayer, in the finder's order.
std::vector<CUnit *> UnitsNear(int x, int y, int r, int nPlayer, int nWho, bool bBuildings) {
  std::vector<CUnit *> out;
  Area a = {x, y, r, nPlayer, nWho, bBuildings, &out};
  mrect_t rc;
  rc.left = x - r;
  rc.top = y - r;
  rc.right = x + r;
  rc.bottom = y + r;
  find_foreach_unit(&rc, AreaPick, &a);
  return out;
}

void AuraPulse(CUnit *pUnit, const Aura &a) {
  if (a.nStatus < 0)
    return;
  for (CUnit *p : UnitsNear(pUnit->pSprite->wPosX, pUnit->pSprite->wPosY, a.nRadius, pUnit->bPlayer, a.nWho, a.bBuildings))
    if (a.bSelf || p != pUnit)
      Apply(p, a.nStatus, pUnit->bPlayer);
}

// ---- the blocks -------------------------------------------------------------------------------------

CUnit *UnitOf(DWORD dwId) {
  if (dwId == 0)
    return nullptr;
  CUnit *p = CUnit::FromIdAlive(dwId);
  return Alive(p) ? p : nullptr;
}

// Damage as a weapon of type nType does it: shields first, less their upgrade, then armour, then
// the type's share against the unit's size (CUnit::Damage).
void DamageUnit(CUnit *pUnit, int nAmount, int nType, CUnit *pSource, int nPlayer) {
  if (!Alive(pUnit) || (pUnit->dwFlags & kInvincible))
    return;
  int nTotal = nAmount << 8;
  if (pSource != nullptr)
    nTotal = ModStatusDamageDealt(pSource, nTotal);
  nTotal = ModStatusDamageTaken(pUnit, nTotal);
  if (nTotal < 0x80)
    nTotal = 0x80;
  int nShields = 0;
  if (gbUnitHasShields[pUnit->wType] && static_cast<LONG>(pUnit->lShields) >= 0x100) {
    if (nType != 0) {
      const int nUp = get_upgrade_level(pUnit->bPlayer, 0xF) << 8;
      nTotal = nUp >= nTotal ? 0x80 : nTotal - nUp;
    }
    nShields = nTotal > static_cast<LONG>(pUnit->lShields) ? static_cast<LONG>(pUnit->lShields) : nTotal;
    nTotal -= nShields;
  }
  if (nType != 0) {
    const int nArmor = ModStatusArmor(pUnit, (pUnit->ArmorUpgrades() + gubUnitArmor_g[pUnit->wType]) << 8);
    nTotal -= nTotal > nArmor ? nArmor : nTotal;
  }
  int nHit = compute_damage_from_types(nTotal, static_cast<BYTE>(nType), gubUnitArmorClass_g[pUnit->wType]);
  if (nShields == 0 && nHit < 0x80)
    nHit = 0x80;
  pUnit->DamageApply(nHit, pSource, nPlayer, 1);
  if (nShields)
    pUnit->lShields -= nShields;
}

void Teleport(CUnit *pUnit, int x, int y) {
  const mpoint_t old = MapPoint(pUnit->pSprite->wPosX, pUnit->pSprite->wPosY);
  mpoint_t dest = MapPoint(x, y);
  pUnit->SetUnitPosition(dest.x, dest.y);
  mpoint_t pos;
  if (!FindPlaceForUnit(pUnit, &dest, &pos, nullptr, 0, 0)) {
    pUnit->SetUnitPosition(old.x, old.y);
    return;
  }
  if (!(pUnit->pSprite->flags.b & 0x20)) {
    pUnit->RemoveTargetReferences(FALSE);
    CBullet::RemoveTargetReferences(pUnit);
    pUnit->UntrackCollision();
    pUnit->pSprite->ForceInvalid();
    if (pUnit->subunit)
      pUnit->subunit->pSprite->ForceInvalid();
    pUnit->ResetMovementState();
  }
  pUnit->pOrderTarget = nullptr;
  pUnit->SetUnitPosition(pos.x, pos.y);
  pUnit->sub_430430();
  pUnit->SetOrder(gubUnitOrderGuard[pUnit->wType]);
  // Recall's flash where it arrives
  if (CThingy *pFlash = CThingy::AllocThingy(0x17B, pos.x, pos.y, 0)) {
    pFlash->pSprite->bElevationLevel = pUnit->pSprite->bElevationLevel + 1;
    pFlash->UpdateVisibility();
  }
}

// What a block reaches: its place, and its units (the target alone with no radius).
void Place(const Run &r, const Op &op, int *px, int *py) {
  CUnit *p = op.nAt == AT_CASTER ? UnitOf(r.dwCaster) : UnitOf(r.dwTarget);
  if (p != nullptr) {
    *px = p->pSprite->wPosX;
    *py = p->pSprite->wPosY;
  } else {
    *px = r.x;
    *py = r.y;
  }
}

std::vector<CUnit *> Reached(const Run &r, const Op &op) {
  if (op.nRadius > 0) {
    int x, y;
    Place(r, op, &x, &y);
    return UnitsNear(x, y, op.nRadius, r.bPlayer, op.nWho, op.bBuildings);
  }
  CUnit *p = op.nAt == AT_CASTER ? UnitOf(r.dwCaster) : UnitOf(r.dwTarget);
  if (p == nullptr && op.nAt == AT_TARGET && r.dwTarget == 0)
    p = UnitOf(r.dwCaster);  // a cast with no unit aimed at reaches its caster
  // the one unit aimed at, whose side the order's targeting chose
  std::vector<CUnit *> out;
  if (p != nullptr && !(p->pSprite->flags.b & 0x20))
    out.push_back(p);
  return out;
}

void Do(Run &r, const Op &op) {
  CUnit *pCaster = UnitOf(r.dwCaster);
  switch (op.nKind) {
  case OP_FIRE: {
    if (pCaster == nullptr)
      return;
    const int nWeapon = op.nRef >= 0 ? op.nRef : gubOrderWeapon[r.wOrder];
    if (!WeaponTypeValid(nWeapon))
      return;
    // aimed where the cast was, whatever the caster does now
    CUnit *pWas = pCaster->pOrderTarget;
    const mpoint_t was = MapPoint(pCaster->wOrderTargetX, pCaster->wOrderTargetY);
    pCaster->pOrderTarget = UnitOf(r.dwTarget);
    pCaster->wOrderTargetX = r.x;
    pCaster->wOrderTargetY = r.y;
    g_bFiring = true;
    pCaster->FireSpell(static_cast<weapon_id_t>(nWeapon));
    g_bFiring = false;
    pCaster->pOrderTarget = pWas;
    pCaster->wOrderTargetX = was.x;
    pCaster->wOrderTargetY = was.y;
    return;
  }
  case OP_DAMAGE:
    for (CUnit *p : Reached(r, op)) {
      // a share is of the unit's most life at its owner's handicap, so the same cast takes the
      // same part of every unit it reaches
      const int nShare = op.nShare == 0 ? 0
                                        : static_cast<int>(HandicapLife(gxUnitLife[p->wType], p->bPlayer) >> 8) *
                                              op.nShare / 100;
      DamageUnit(p, std::max(1, op.nAmount + nShare), op.nType, pCaster, r.bPlayer);
    }
    return;
  case OP_HEAL:
    for (CUnit *p : Reached(r, op))
      if (Alive(p) && !(gulUnitTypeFlags[p->wType] & kBuilding && !op.bBuildings))
        p->SetLife(p->lHitPoints + (op.nAmount << 8));
    return;
  case OP_ENERGY:
    for (CUnit *p : Reached(r, op))
      if (gulUnitTypeFlags[p->wType] & kSpellcaster) {
        const int nNew = std::max(0, std::min<int>(p->wEnergy + (op.nAmount << 8), p->GetMaxEnergy()));
        p->wEnergy = static_cast<WORD>(nNew);
      }
    return;
  case OP_STATUS:
    for (CUnit *p : Reached(r, op))
      Apply(p, op.nRef, r.bPlayer);
    return;
  case OP_REMOVE:
    for (CUnit *p : Reached(r, op))
      Remove(p, op.status.empty() ? -1 : op.nRef);
    return;
  case OP_SPAWN: {
    int x, y;
    Place(r, op, &x, &y);
    std::vector<int> made;
    PortTrigCreate(op.nRef, x, y, r.bPlayer, op.nAmount, &made, false);
    for (int n : made)
      if (op.nFrames > 0) {
        CUnit *p = CUnit::Head() + n;
        p->wTimeToLive = static_cast<WORD>(std::min(op.nFrames, 0xFFFF));
      }
    return;
  }
  case OP_TELEPORT:
    if (pCaster != nullptr && !(gulUnitTypeFlags[pCaster->wType] & kBuilding))
      Teleport(pCaster, r.x, r.y);
    return;
  case OP_SOUND: {
    int x, y;
    Place(r, op, &x, &y);
    snd_pos_t pos;
    pos.x = x;
    pos.y = y;
    gamesnd_play_mtx(op.nRef, 1, pos, 0);
    return;
  }
  case OP_SCRIPT:
    ModLuaBlock(op.call, r.dwCaster, r.dwTarget, r.x, r.y, r.wOrder, r.bPlayer);
    return;
  case OP_SHOW: {
    int x, y;
    Place(r, op, &x, &y);
    if (CThingy *pThingy = CThingy::AllocThingy(op.nRef, x, y, 0)) {
      pThingy->pSprite->bElevationLevel = pCaster ? pCaster->pSprite->bElevationLevel + 1 : 4;
      pThingy->UpdateVisibility();
    }
    return;
  }
  }
}

// A program on until it waits or ends; false once it has ended.
bool Step(Run &r) {
  if (r.wWait > 0 && --r.wWait > 0)
    return true;
  const int nProgram = r.wOrder < g_game.orderProgram.size() ? g_game.orderProgram[r.wOrder] : -1;
  if (nProgram < 0)
    return false;
  const std::vector<Op> &ops = g_game.programs[nProgram];
  for (int nGuard = 0; nGuard < 4096; ++nGuard) {
    if (r.bDepth > 0 && r.wPc >= ops[r.stack[r.bDepth - 1].wOp].nEnd) {
      RunFrame &f = r.stack[r.bDepth - 1];
      if (--f.wLeft > 0)
        r.wPc = static_cast<WORD>(f.wOp + 1);
      else
        --r.bDepth;
      continue;
    }
    if (r.wPc >= ops.size())
      return false;
    const Op &op = ops[r.wPc];
    if (op.nKind == OP_REPEAT) {
      if (r.bDepth >= kDepthMax) {
        r.wPc = static_cast<WORD>(op.nEnd);
        continue;
      }
      r.stack[r.bDepth].wOp = r.wPc;
      r.stack[r.bDepth].wLeft = static_cast<WORD>(op.nAmount);
      ++r.bDepth;
      ++r.wPc;
      continue;
    }
    ++r.wPc;
    if (op.nKind == OP_WAIT) {
      r.wWait = static_cast<WORD>(op.nFrames);
      return true;
    }
    Do(r, op);
  }
  return true;  // the rest goes on next frame
}

// ---- the live channel's statuses --------------------------------------------------------------------

bool CmdStatuses(const PortJson &j, std::string *pAnswer) {
  std::string s = "\"on\":" + std::string(g_game.bOn && g_hxModGame ? "true" : "false") + ",\"statuses\":[";
  for (size_t i = 0; i < g_game.statuses.size(); ++i)
    s += std::string(i ? "," : "") + PortJsonQuote(g_game.statuses[i].name);
  s += "],\"runs\":" + std::to_string(g_nRuns) + ",\"units\":[";
  const int nPlayer = static_cast<int>(j["player"].Num(-1));
  bool bFirst = true;
  for (CUnit *p = g_game.bOn ? CUnit::GetUsedList() : nullptr; p != nullptr; p = p->pNext) {
    const std::string sts = ModStatusUnitJson(p);
    if (sts.empty() || (nPlayer >= 0 && p->bPlayer != nPlayer))
      continue;
    s += std::string(bFirst ? "" : ",") + "{\"id\":" + std::to_string(IndexOf(p)) + ",\"type\":" + std::to_string(p->wType) +
         ",\"player\":" + std::to_string(p->bPlayer) + ",\"hp\":" + std::to_string(p->lHitPoints / 256.0) +
         ",\"speed\":" + std::to_string(p->lTopSpeed) + sts + "}";
    bFirst = false;
  }
  *pAnswer = s + "]";
  return true;
}

}  // namespace

// ---- the loader -----------------------------------------------------------------------------------------

bool ModStatusField(int t, const std::string &key) {
  return (t == 1 && key == "status") || (t == 0 && key == "aura") || (t == 5 && key == "blocks");
}

bool ModStatusLoad(const PortJson &types, int nSub, const std::vector<ModStatusRecord> &records, std::string *pWhy) {
  std::vector<Def> defs = g_defs;
  const PortJson &st = types["statuses"];
  if (st.kind != PortJson::NONE && st.kind != PortJson::OBJECT)
    return Refuse(pWhy, "statuses is an object, each status by its name");
  for (const PortJsonMember &m : st.members) {
    Def d;
    if (!ReadDef(m.first, m.second, nSub, &d, pWhy))
      return false;
    defs.push_back(d);
  }
  std::vector<WeaponLink> links;
  std::vector<Aura> auras;
  std::vector<Program> programs;
  for (const ModStatusRecord &r : records) {
    const PortJson &j = *r.pJson;
    if (r.t == 1 && j["status"].kind != PortJson::NONE) {
      std::string name;
      if (!StatusName(j, "status", true, defs, &name, "weapons " + r.name, pWhy))
        return false;
      links.push_back({r.id, nSub, name});
    }
    if (r.t == 0 && j["aura"].kind != PortJson::NONE) {
      // one aura, or a list of up to three: each gives its own status at its own reach and pace
      const PortJson &all = j["aura"];
      const bool bList = all.kind == PortJson::ARRAY;
      const size_t nCount = bList ? all.size() : 1;
      if (bList && (nCount < 1 || nCount > AURAS_PER_UNIT))
        return Refuse(pWhy, "units " + r.name + ".aura is one aura or a list of up to three");
      for (size_t k = 0; k < nCount; k++) {
        const std::string where = "units " + r.name + ".aura" + (bList ? " " + std::to_string(k + 1) : "");
        const PortJson &a = bList ? all.At(k) : all;
        Aura au;
        au.nUnit = r.id;
        au.nSub = nSub;
        double v = 0;
        if (!Only(a, {"status", "radius", "targets", "every", "self", "buildings"}, where, pWhy) ||
            !StatusName(a, "status", true, defs, &au.status, where, pWhy) || !Number(a, "radius", 1, 2048, 160, &v, where, pWhy))
          return false;
        au.nRadius = static_cast<int>(v);
        if (!Choice(a, "targets", kWho, 4, WHO_ALLIES, &au.nWho, where, pWhy) ||
            !Number(a, "every", 1.0 / kFps, 600, 1, &v, where, pWhy))
          return false;
        au.nEvery = std::max(1, Frames(v));
        if (!Bool(a, "self", true, &au.bSelf, where, pWhy) || !Bool(a, "buildings", false, &au.bBuildings, where, pWhy))
          return false;
        auras.push_back(au);
      }
    }
    if (r.t == 5 && j["blocks"].kind != PortJson::NONE) {
      Program p;
      p.nOrder = r.id;
      p.nSub = nSub;
      if (!ReadBlocks(j["blocks"], "orders " + r.name + ".blocks", 1, defs, &p.ops, pWhy))
        return false;
      if (!p.ops.empty())
        programs.push_back(p);
    }
  }
  g_defs = defs;
  g_weaponLinks.insert(g_weaponLinks.end(), links.begin(), links.end());
  g_auras.insert(g_auras.end(), auras.begin(), auras.end());
  g_programs.insert(g_programs.end(), programs.begin(), programs.end());
  if (g_game.bOn)
    Build();  // a push into the game being played
  return true;
}

void ModStatusTrim(int nSubs) {
  auto past = [nSubs](int n) { return n > nSubs; };
  g_defs.erase(std::remove_if(g_defs.begin(), g_defs.end(), [&](const Def &d) { return past(d.nSub); }), g_defs.end());
  g_weaponLinks.erase(std::remove_if(g_weaponLinks.begin(), g_weaponLinks.end(), [&](const WeaponLink &l) { return past(l.nSub); }),
                      g_weaponLinks.end());
  g_auras.erase(std::remove_if(g_auras.begin(), g_auras.end(), [&](const Aura &a) { return past(a.nSub); }), g_auras.end());
  g_programs.erase(std::remove_if(g_programs.begin(), g_programs.end(), [&](const Program &p) { return past(p.nSub); }),
                   g_programs.end());
  if (g_game.bOn)
    Build();
}

void ModStatusForGame() {
  static bool s_bCommand = false;
  if (!s_bCommand) {
    PortControlCommand("statuses", CmdStatuses);
    s_bCommand = true;
  }
  memset(g_units, 0, sizeof(UnitState) * static_cast<size_t>(std::max(0, std::min(g_pools.units, POOL_UNITS_STORE))));
  memset(g_runs, 0, sizeof g_runs);
  g_nRuns = 0;
  g_game = Game();
  if (g_hxModGame && (!g_defs.empty() || !g_programs.empty()))
    Build();
}

// ---- the game's hooks -----------------------------------------------------------------------------------

int ModStatusSpeed(CUnit *pUnit, int nSpeed) {
  if (!g_game.bOn)
    return nSpeed;
  bool bAny = false;
  const int nPercent = Sum(pUnit, [](const Def &d) { return d.nSpeed; }, &bAny);
  if (Any(pUnit, &Def::bNoMove))
    return 0;
  if (!bAny)
    return nSpeed;
  return static_cast<int>(static_cast<long long>(nSpeed) * std::max(0, 100 + nPercent) / 100);
}

int ModStatusCooldown(CUnit *pUnit, int nCooldown) {
  if (!g_game.bOn)
    return nCooldown;
  const int nPercent = Sum(pUnit, [](const Def &d) { return d.nCooldown; });
  return nPercent == 0 ? nCooldown : static_cast<int>(static_cast<long long>(nCooldown) * std::max(10, 100 + nPercent) / 100);
}

int ModStatusDamageDealt(CUnit *pSource, int nDamage) {
  if (!g_game.bOn || pSource == nullptr)
    return nDamage;
  const int nPercent = Sum(pSource, [](const Def &d) { return d.nDamage; });
  return nPercent == 0 ? nDamage : static_cast<int>(static_cast<long long>(nDamage) * std::max(0, 100 + nPercent) / 100);
}

int ModStatusDamageTaken(CUnit *pUnit, int nDamage) {
  if (!g_game.bOn || pUnit == nullptr)
    return nDamage;
  const int nPercent = Sum(pUnit, [](const Def &d) { return d.nTaken; });
  return nPercent == 0 ? nDamage : static_cast<int>(static_cast<long long>(nDamage) * std::max(0, 100 + nPercent) / 100);
}

int ModStatusArmor(CUnit *pUnit, int nArmor) {
  if (!g_game.bOn)
    return nArmor;
  const int n = Sum(pUnit, [](const Def &d) { return d.nArmor; });
  return std::max(0, nArmor + (n << 8));
}

void ModStatusUnitMade(CUnit *pUnit) {
  const int i = IndexOf(pUnit);
  if (g_game.bOn && i >= 0 && i < std::min(g_pools.units, POOL_UNITS_STORE))
    memset(&g_units[i], 0, sizeof g_units[i]);
}

void ModStatusWeaponHit(CUnit *pUnit, int nWeapon, CUnit *pSource, int nSourcePlayer) {
  if (!g_game.bOn || nWeapon < 0 || nWeapon >= static_cast<int>(g_game.weaponStatus.size()))
    return;
  const int n = g_game.weaponStatus[nWeapon];
  if (n >= 0 && pUnit->lHitPoints > 0)
    Apply(pUnit, n, pSource ? pSource->bPlayer : nSourcePlayer);
}

bool ModStatusCast(CUnit *pCaster) {
  if (!g_game.bOn || g_bFiring)
    return false;
  const unsigned nOrder = pCaster->bOrderId;
  if (nOrder >= g_game.orderProgram.size() || g_game.orderProgram[nOrder] < 0)
    return false;
  if (g_nRuns >= kRunsMax)
    return true;  // as many run as can: this cast does nothing
  Run &r = g_runs[g_nRuns++];
  memset(&r, 0, sizeof r);
  r.wOrder = static_cast<WORD>(nOrder);
  r.bPlayer = pCaster->bPlayer;
  r.dwCaster = static_cast<DWORD>(pCaster->GetUnitID());
  CUnit *pTarget = pCaster->pOrderTarget;
  r.dwTarget = pTarget && Alive(pTarget) ? static_cast<DWORD>(pTarget->GetUnitID()) : 0;
  r.x = pTarget && Alive(pTarget) ? static_cast<int>(pTarget->pSprite->wPosX) : static_cast<int>(pCaster->wOrderTargetX);
  r.y = pTarget && Alive(pTarget) ? static_cast<int>(pTarget->pSprite->wPosY) : static_cast<int>(pCaster->wOrderTargetY);
  return true;  // it runs on the frame's end, after the units' update (ModStatusFrame)
}

void ModStatusFrame() {
  if (!g_game.bOn)
    return;
  // the programs, each in turn; one cast during another's blocks starts after it
  int nKept = 0;
  const int nRuns = g_nRuns;
  for (int i = 0; i < nRuns; ++i) {
    Run r = g_runs[i];
    const bool bOn = Step(r);
    g_runs[i] = r;
    if (bOn)
      g_runs[nKept++] = g_runs[i];
  }
  for (int i = nRuns; i < g_nRuns; ++i)
    g_runs[nKept++] = g_runs[i];
  g_nRuns = nKept;
  // the units, in the list's order: their auras, then their statuses
  CUnit *pNext = nullptr;
  for (CUnit *p = CUnit::GetUsedList(); p != nullptr; p = pNext) {
    pNext = p->pNext;
    if (!Alive(p) || (gulUnitTypeFlags[p->wType] & kSubunit))
      continue;
    const int nAura = p->wType < g_game.unitAura.size() ? g_game.unitAura[p->wType] : -1;
    const int nAuras = nAura < 0 ? 0 : g_game.unitAuras[p->wType];
    if (nAuras > 0 && (p->dwFlags & 1) && !(p->pSprite->flags.b & 0x20)) {
      UnitState *st = Claim(p);
      for (int k = 0; st != nullptr && k < nAuras; k++) {
        if (st->awAura[k] == 0) {
          st->awAura[k] = static_cast<WORD>(g_game.auras[nAura + k].nEvery);
          AuraPulse(p, g_game.auras[nAura + k]);
        }
        if (st->awAura[k] > 0)
          --st->awAura[k];
      }
    }
    UnitState *st = StateOf(p);
    if (st != nullptr && st->bAny > 0)
      UnitFrame(p, *st);
  }
}

// ---- a save -------------------------------------------------------------------------------------------

int ModStatusSaveGame(void *pFile) {
  if (!g_game.bOn || !g_hxModGame)
    return 1;
  const DWORD adw[3] = {1, static_cast<DWORD>(g_pools.units), static_cast<DWORD>(g_nRuns)};
  return PortSaveSection(pFile, kSection) && pfwrite(adw, sizeof adw, pFile) &&
         compress(g_units, static_cast<int>(sizeof(UnitState) * g_pools.units), pFile) &&
         (g_nRuns == 0 || pfwrite(g_runs, static_cast<int>(sizeof(Run) * g_nRuns), pFile));
}

int ModStatusLoadGame(void *pFile) {
  const int nHave = PortLoadSectionIf(pFile, kSection);
  if (nHave <= 0)
    return nHave == 0;
  DWORD adw[3];
  if (!pfread(adw, sizeof adw, pFile) || adw[0] != 1 || adw[1] != static_cast<DWORD>(g_pools.units) || adw[2] > kRunsMax)
    return 0;
  memset(g_runs, 0, sizeof g_runs);
  g_nRuns = static_cast<int>(adw[2]);
  return decompress(g_units, static_cast<int>(sizeof(UnitState) * g_pools.units), pFile) &&
         (g_nRuns == 0 || pfread(g_runs, static_cast<int>(sizeof(Run) * g_nRuns), pFile));
}

bool ModStatusHasNamed(const char *pszName) {
  return g_game.bOn && GameStatus(pszName) >= 0;
}

bool ModStatusApplyNamed(CUnit *pUnit, const char *pszName, int nPlayer) {
  const int n = g_game.bOn ? GameStatus(pszName) : -1;
  if (n < 0 || !Alive(pUnit))
    return false;
  Apply(pUnit, n, nPlayer);
  return true;
}

void ModStatusRemoveNamed(CUnit *pUnit, const char *pszName) {
  if (!g_game.bOn)
    return;
  const int n = pszName ? GameStatus(pszName) : -1;
  if (pszName == nullptr || n >= 0)
    Remove(pUnit, n);
}

int ModStatusStacksNamed(CUnit *pUnit, const char *pszName) {
  const int n = g_game.bOn ? GameStatus(pszName) : -1;
  UnitState *st = n >= 0 ? StateOf(Whole(pUnit)) : nullptr;
  if (st == nullptr)
    return 0;
  for (const Slot &s : st->a)
    if (s.wStatus == n + 1)
      return s.bStacks;
  return 0;
}

bool ModStatusKindOfDef(const PortJson &status, int *pnKind, bool *pbGood) {
  Def d;
  std::string why;
  if (!ReadDef(std::string(), status, 0, &d, &why))
    return false;
  if (pnKind != nullptr)
    *pnKind = PortReadoutKindFrom(d.nKind, d.bNoMove, d.bNoAttack, d.nDps, d.nRegen, d.nArmor, d.nCooldown,
                                  d.nSpeed, d.nDamage, d.nTaken);
  if (pbGood != nullptr)
    *pbGood = SideOf(d) > 0;
  return true;
}

void ModStatusOnUnit(const CUnit *pUnit, std::vector<ModStatusOn> *pOut) {
  pOut->clear();
  if (!g_game.bOn || pUnit == nullptr)
    return;
  const UnitState *st = StateOf(const_cast<CUnit *>(pUnit));
  if (st == nullptr || st->bAny == 0)
    return;
  for (const Slot &sl : st->a) {
    const Def *d = DefOf(sl);
    if (d == nullptr)
      continue;
    ModStatusOn on;
    on.name = d->name;
    on.label = d->label;
    on.nFramesLeft = sl.wFrames;
    on.nFrames = d->nFrames;
    on.nStacks = sl.bStacks;
    on.nIcon = d->nIcon;
    on.bGood = SideOf(*d) > 0;
    on.nSpeed = d->nSpeed;
    on.nArmor = d->nArmor;
    on.nDamage = d->nDamage;
    on.nTaken = d->nTaken;
    on.nCooldown = d->nCooldown;
    on.nDps = d->nDps;
    on.nRegen = d->nRegen;
    on.bNoMove = d->bNoMove;
    on.bNoAttack = d->bNoAttack;
    on.nKind = d->nKind;
    on.nPriority = d->nPriority;
    on.nHue = d->nHue;
    on.bHue = d->bHue;
    on.pattern = d->pattern;
    on.bShowReadout = d->bShowReadout;
    on.bShowWire = d->bShowWire;
    on.bShowGroup = d->bShowGroup;
    pOut->push_back(on);
  }
}

std::string ModStatusUnitJson(const CUnit *pUnit) {
  if (!g_game.bOn)
    return std::string();
  const UnitState *st = StateOf(const_cast<CUnit *>(pUnit));
  if (st == nullptr || st->bAny == 0)
    return std::string();
  std::string s;
  for (const Slot &sl : st->a)
    if (const Def *d = DefOf(sl))
      s += std::string(s.empty() ? "" : ",") + "{\"name\":" + PortJsonQuote(d->name) + ",\"left\":" + std::to_string(sl.wFrames) +
           ",\"stacks\":" + std::to_string(sl.bStacks) + "}";
  return s.empty() ? s : ",\"statuses\":[" + s + "]";
}
