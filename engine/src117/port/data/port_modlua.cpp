// port_modlua.cpp -- a mod's Lua scripts in a game (port_modlua.h; docs/mod-data.md, "Scripts").
//
// The loader's part keeps each submission's scripts as bytecode, its records' hooks and the
// functions the scripts' tops give, by its number, so a push that does not load and the put-away
// after a game drop them again (ModLuaTrim). A game builds one Luau state from them
// (ModLuaForGame): every file's top run once, in the order of the submissions and of their names,
// and then every table and function they hold frozen, so nothing a call does outlives it but what
// it gives the game to keep.
//
// The game's part is plain state of fixed size: a record a unit, by its place in the unit table
// (its values, its orders' charges and toggles, its tick), the game's values and the names the
// values are kept under, the events of the frame not yet given to the scripts, and the channels
// running. It is kept with the trainer's snapshots and saved as HXLU. The Luau state itself holds
// nothing of the game's, so a replay or a save loaded builds it again as it was.
//
// Every call has a budget of the VM's steps (its interrupt, which it takes at each call and each
// jump back), and so has each frame, the same on every machine; a call past either stops with an
// error, as a script's own error stops it. Numbers are doubles: sums, differences, products and
// quotients and square roots are exact to IEEE and so the same everywhere, and the math library's
// other functions are this file's own, made of those. The power operator is refused as the script
// is read (port_modlua_lint.cpp).
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "lua.h"
#include "lualib.h"

#include "port_modlua.h"
#include "port_modstatus.h"
#include "port_moddata.h"  // g_hxModGame, ModTypeByName, ModCardButtons
#include "port_mode.h"     // PortModeEnemySlot: the player a mode plays itself (the director)
#include "port_modemap.h"  // the map read a mode's generate is given
#include "port_modeview.h"  // the parts a mode shows: hx.timer, hx.banner, hx.counter
#include "../render/port_mapmark.h"  // the marks a mod draws on the map: hx.area
#include "../render/port_minimap.h"  // the minimap's own size: hx.minimap
#include "../hud/port_readout.h"     // the unit readout: hx.readout.fx, and readout(unit)
#include "port_modlang.h"  // ModLangModeText: a mode's lines in the player's language
#include "port_formats.h"  // PORT_SAVE_ID, PortSaveSection, PortLoadSectionIf
#include "../platform/port_json.h"
#include "../platform/port_paths.h"  // PortCommandOptions: --mode-trace
#include "../platform/port_log.h"
#include "../tools/port_control.h"     // PortControlCommand
#include "../replays/port_snap.h"      // PORT_SNAP_KEEP
#include "../gameplay/port_trigrun.h"  // PortTrigCreate
#include "../gameplay/port_launch.h"   // PortLaunchIsCanvas
#include "../tools/port_benchinfo.h"  // PortBenchSelection
#include "../../h/typelimits.h"
#include "../../h/poollimits.h"   // POOL_UNITS_STORE, g_pools
#include "../../h/saveload.h"     // pfwrite, pfread, compress, decompress, get_upgrade_level
#include "../../h/CUnit.h"
#include "../../h/CUnitSearches.h"  // UnitCanReachGroundTarget: hx.can_reach
#include "../../h/CSprite.h"
#include "../../h/CFlingy.h"
#include "../../h/CThingy.h"
#include "../../h/CBullet.h"
#include "../../h/TrigEnum.h"      // find_foreach_unit
#include "../../h/TrigAct.h"       // net_rand
#include "../../h/CUnitMisc.h"     // IsEnemyUnit
#include "../../h/CUnitZSpells.h"  // FindPlaceForUnit
#include "../../h/CUnitInit.h"     // get_order_tech_energy_cost
#include "../../h/damage.h"        // compute_damage_from_types
#include "../../h/gamesnd.h"       // gamesnd_play_mtx
#include "../../h/order.h"         // gamesnd_error
#include "../../h/handicap.h"      // HandicapLife, HandicapShields
#include "../../h/playerlimits.h"  // PLAYER_NEUTRAL, PLAYER_LOBBY_117
#include "../../h/net_cmds.h"      // PlayerData
#include "../../h/playertables.h"  // PlayerSlots, gPlayerData
#include "../../h/playervision.h"  // VisionHxHidden, VisionHxUnexplored
#include "../../h/minimap.h"       // do_minimap_ping
#include "../../gen/globals117.h"

#ifndef gubUnitArmor_g
extern BYTE gubUnitArmor_g[228] OC_UNALIGNED;
#endif

namespace {

const PortLogger Log("hyperion_mods.log");

// units.dat's flags this file reads
const DWORD kBuilding = 0x1, kSubunit = 0x10, kSpellcaster = 0x200000;
const DWORD kInvincible = 0x4000000;  // CUnit::dwFlags
const int kFps = 24;                  // game frames a second at Fastest
const int kOrderSlots = 4;            // orders with charges or a toggle a unit keeps
const int kUnitValues = 8;            // values a unit keeps
const int kGameValues = 64;           // values the game keeps
const int kKeys = 256;                // names values are kept under
const int kKeyLen = 32;
const int kEventsMax = 1024;          // events waiting for the frame's end
const int kDyingMax = 256;            // units dead this frame whose values are kept for their hooks
const int kChannelsMax = 64;          // channels running at once
const int kEventsAFrame = 4096;       // events given to the scripts in one frame
const int kRevealsMax = 32;           // the places a mode reveals at once
const long long kCallSteps = 100000;  // a call's budget of the VM's steps
const long long kFrameSteps = 1000000;
const long long kTopSteps = 1000000;  // a file's top, run as the game starts
// a mode's generate, run once as the game loads and not in a frame: a run of forty waves with
// sixty generated kinds does not fit in a call's budget
const long long kGenerateSteps = 10000000;
const size_t kHeapMax = 64u << 20;    // the scripts' memory
const int kDepthMax = 4;              // a hook called inside another (hx.damage's on_damaged)
const int kRandTag = 0xF0;            // net_rand's tag for the scripts' draws (a count only)
const int kLogKept = 400;

// orders by number, as hx.order names them
const int kOrderStop = 0x01, kOrderMove = 0x06, kOrderAttack = 0x0E, kOrderHold = 0x6B, kOrderPatrol = 0x98;
const int kOrderGuard = 0x02, kOrderPlayerGuard = 0x03;

// ---- what the mod gives, by submission ------------------------------------------------------------

struct File {
  std::string name;  // under scripts/, "main.lua"
  std::string bytecode;
  int nSub = 0;
};
struct Function {
  std::string name, file, args;
  int nLine = 0, nSub = 0;
};
struct UnitHooks {
  int nUnit = 0, nSub = 0;
  std::string onHit, onDamaged, onDeath, onMade, tick;
  int nEvery = kFps;
  std::string brain;      // a unit a computer or a mode owns decides for itself
  int nBrainEvery = kFps;
};
struct OrderSpec {
  int nOrder = 0, nSub = 0;
  bool bToggle = false;
  std::string on, off;
  int nDrain = 0;  // energy a second, in 1/256
  bool bChannel = false;
  std::string chTick, chStop;
  int nChEvery = kFps, nChFrames = 0;  // 0: as long as the caster stands
  int nCooldown = 0, nCharges = 0;     // frames, and 0 for none
  std::string choose;
  int nAutoEvery = kFps / 2;
};
struct GameHooks {
  int nSub = 0;
  std::string start, tick;
  int nEvery = kFps;
  std::string onDeath;  // any unit of the game died, whatever its type
  // the director: what the player a mode plays does next, on a beat of its own
  std::string director;
  int nDirectorEvery = kFps;
};

std::vector<File> g_files;
std::vector<Function> g_functions;
std::vector<UnitHooks> g_unitHooks;
std::vector<OrderSpec> g_orderSpecs;
std::vector<GameHooks> g_gameHooks;
std::pair<std::string, int> g_generate;  // types.json's "generate" and its submission
const PortJson *g_pPushTexts;

// ---- the game's tables ----------------------------------------------------------------------------

struct Autocast {
  int nUnit, nOrder;
  bool bTarget;
};
// A card's cast whose "ai" names one of the mod's own functions (a mode's).
struct ModeCast {
  int nUnit, nOrder;
  bool bTarget;
  std::string call;
  int nEvery;
};
struct Game {
  bool bOn = false;
  std::vector<short> unitHooks;   // by unit type, an index into g_unitHooks
  std::vector<short> orderSpecs;  // by order, an index into g_orderSpecs
  GameHooks game;
  std::vector<Autocast> autocasts;
  std::vector<ModeCast> casts;
  bool bAutocastsBuilt = false;
};
Game g_game;

// ---- the game's state ------------------------------------------------------------------------------

#pragma pack(push, 1)
struct Value {
  WORD wKey;   // 1 + its name's index, 0 for none
  BYTE bKind;  // 1 a number, 2 true or false
  BYTE bPad;
  double d;
};
struct OrderSlot {
  WORD wOrder;  // 0 for none
  BYTE bCharges;
  BYTE bFlags;  // kOn, kAutoOff, kCharged
  WORD wRecharge;
  WORD wCarry;  // a toggle's drain not yet taken, in 1/256 over 24
};
const BYTE kOn = 1, kAutoOff = 2, kCharged = 4;
struct UnitState {
  DWORD dwId;  // the unit's id as it was written; any other unit in the slot reads it as empty
  BYTE bMade;  // its on_made called
  BYTE bOwn;   // 1: the mode owns it, so the computer leaves it alone (hx.own)
  WORD wTick;  // frames to its next tick
  OrderSlot o[kOrderSlots];
  Value v[kUnitValues];
};
// A place a mode revealed, while its seconds last (hx.reveal).
struct Reveal {
  int x, y;
  WORD wTiles;   // the radius in tiles
  WORD wLeft;    // frames it has left
  DWORD dwPlayers;  // the players it is shown to, a bit each
};
enum { EV_HIT = 1, EV_DEATH, EV_TOGGLE_ON, EV_TOGGLE_OFF, EV_CHANNEL_STOP };
struct Event {
  BYTE bKind;
  BYTE bPlayer;
  WORD wWeapon;  // a hit's weapon; a toggle's or a channel's order
  DWORD dwUnit, dwOther;
  WORD wType;
  BYTE bReason;  // a toggle's or a channel's stop: kReasons
  BYTE bPad;
  int x, y;
  int nDamage, nTaken;  // 8.8
};
struct Channel {
  DWORD dwCaster, dwTarget;
  WORD wOrder;
  WORD wNext;   // frames to its next tick
  DWORD dwLeft; // frames it has left, 0 for as long as the caster stands
  DWORD dwTicks;
  int x, y;
  WORD wStandX, wStandY;
  BYTE bPlayer;
  BYTE bPad[3];
};
struct GameState {
  DWORD dwFrame;
  WORD wTick;
  BYTE bStarted;
  BYTE bPad;
  WORD nKeys;
  WORD nEvents;
  WORD nChannels;
  WORD nReveals;
  Value v[kGameValues];
  char keys[kKeys][kKeyLen];
};
// A unit dead this frame and what it kept: by the frame's end, where its death hooks are given,
// the unit is gone and its slot may hold another, so hx.get reads its values from here.
struct Dying {
  DWORD dwId;
  Value v[kUnitValues];
};
struct DyingStore {
  DWORD n;
  Dying a[kDyingMax];
};
#pragma pack(pop)

const char *const kReasons[] = {"cast", "energy", "script", "moved", "ordered", "died", "ended"};
enum { R_CAST, R_ENERGY, R_SCRIPT, R_MOVED, R_ORDERED, R_DIED, R_ENDED };

UnitState g_lu[POOL_UNITS_STORE];
PORT_SNAP_KEEP_POOL(g_lu, g_pools.units);
Event g_events[kEventsMax];
PORT_SNAP_KEEP(g_events);
Channel g_channels[kChannelsMax];
PORT_SNAP_KEEP(g_channels);
Reveal g_reveals[kRevealsMax];
PORT_SNAP_KEEP(g_reveals);
// Who damaged each unit last, for the killer a game's on_death is given: written where the
// game's damage lands, read when the unit dies, which may be a frame later.
DWORD g_killedBy[POOL_UNITS_STORE];
PORT_SNAP_KEEP_POOL(g_killedBy, g_pools.units);
GameState g_gs;
PORT_SNAP_KEEP(g_gs);
DyingStore g_dying;
PORT_SNAP_KEEP(g_dying);

const ULONG kSection = PORT_SAVE_ID('H', 'X', 'L', 'U');

// ---- the mode's own, when the game has one (port_mode.h) ---------------------------------------------

std::string g_runJson;      // what generate returned as run, read as hx.run
PortJson g_settings;        // the mode's settings for this game, read by hx.setting
bool g_bSettings;
std::string g_modeId;       // the mode's id, for its language keys
std::vector<std::pair<std::string, std::string>> g_hud;  // what hx.hud set, by key (presentation)

// ---- the run's notes ---------------------------------------------------------------------------------

std::deque<std::string> g_log;  // the last lines, for the live channel
unsigned g_nLogged;             // lines ever, so a reader asks for those past one it saw
int g_nErrors;                  // errors this game, the first of which go to the log file

// the trace below, which every line of the log goes to as well
void Trace(const char *pszAct, const CUnit *pUnit, const char *pszFormat, ...);

void Note(const std::string &line, bool bError) {
  char sz[24];
  snprintf(sz, sizeof sz, "frame %u: ", static_cast<unsigned>(g_gs.dwFrame));
  g_log.push_back(sz + line);
  if (g_log.size() > static_cast<size_t>(kLogKept))
    g_log.pop_front();
  ++g_nLogged;
  if (bError && ++g_nErrors <= 200)
    Log("scripts: %s%s", sz, line);
  Trace(bError ? "error" : "log", nullptr, "%s", line.c_str());
}

// ---- reading the mod ----------------------------------------------------------------------------

bool Refuse(std::string *pWhy, const std::string &s) {
  *pWhy = s;
  return false;
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

int Frames(double dSeconds) {
  return static_cast<int>(floor(dSeconds * kFps + 0.5));
}

bool Known(const std::vector<Function> &fns, const std::string &name) {
  for (const Function &f : fns)
    if (f.name == name)
      return true;
  return false;
}

// A field naming a function of the scripts; "" when it is not there and not needed.
bool FunctionName(const PortJson &o, const char *pszKey, bool bNeeded, const std::vector<Function> &fns, std::string *pName,
                  const std::string &where, std::string *pWhy) {
  const PortJson &v = o[pszKey];
  pName->clear();
  if (v.kind == PortJson::NONE)
    return !bNeeded || Refuse(pWhy, where + " needs " + pszKey + ", a function of the mod's scripts");
  if (v.kind != PortJson::STRING || v.s.empty())
    return Refuse(pWhy, where + "." + pszKey + " names a function of the mod's scripts");
  if (!Known(fns, v.s))
    return Refuse(pWhy, where + "." + pszKey + " names " + v.s + ", which no script of the mod gives (a function at a script's top)");
  *pName = v.s;
  return true;
}

// ---- the deterministic math ------------------------------------------------------------------------

const double kPi = 3.14159265358979323846;

// x reduced to [-pi, pi], then a polynomial: sums and products alone, so the same everywhere
double OwnSin(double x) {
  if (!(x == x) || x > 1e15 || x < -1e15)
    return x - x;  // nothing for a number past any angle, as nothing is the same for it
  x = x - 2 * kPi * floor(x / (2 * kPi) + 0.5);
  // sin(x) = -sin(x - pi) folds [pi/2, pi] onto [-pi/2, 0]
  if (x > kPi / 2)
    x = kPi - x;
  else if (x < -kPi / 2)
    x = -kPi - x;
  const double x2 = x * x;
  double t = x, s = x;
  for (int n = 1; n <= 11; ++n) {
    t = -t * x2 / ((2.0 * n) * (2.0 * n + 1));
    s += t;
  }
  return s;
}

double OwnCos(double x) {
  return OwnSin(x + kPi / 2);
}

// atan on [-1, 1] by its series after two halvings of the angle, then the octant
double OwnAtanUnit(double x) {
  // atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))), twice
  for (int i = 0; i < 2; ++i)
    x = x / (1.0 + sqrt(1.0 + x * x));
  const double x2 = x * x;
  double t = x, s = x;
  for (int n = 1; n <= 12; ++n) {
    t = -t * x2;
    s += t / (2.0 * n + 1);
  }
  return 4.0 * s;
}

double OwnAtan2(double y, double x) {
  if (x == 0 && y == 0)
    return 0;
  const double ax = fabs(x), ay = fabs(y);
  double a = ay <= ax ? OwnAtanUnit(ay / ax) : kPi / 2 - OwnAtanUnit(ax / ay);
  if (x < 0)
    a = kPi - a;
  return y < 0 ? -a : a;
}

long long ISqrt(long long n) {
  if (n <= 0)
    return 0;
  long long r = static_cast<long long>(sqrt(static_cast<double>(n)));
  while (r * r > n)
    --r;
  while ((r + 1) * (r + 1) <= n)
    ++r;
  return r;
}

// ---- the units ------------------------------------------------------------------------------------

int IndexOf(const CUnit *pUnit) {
  return static_cast<int>(pUnit - CUnit::Head());
}

bool Alive(CUnit *pUnit) {
  return pUnit != nullptr && pUnit->pSprite != nullptr && pUnit->lHitPoints > 0 && !pUnit->IsDeadOrDying();
}

bool Hidden(CUnit *pUnit) {
  return (pUnit->pSprite->flags.b & 0x20) != 0;
}

CUnit *UnitOf(DWORD dwId) {
  if (dwId == 0)
    return nullptr;
  CUnit *p = CUnit::FromIdAlive(dwId);
  return Alive(p) ? p : nullptr;
}

DWORD IdOf(CUnit *pUnit) {
  return pUnit ? static_cast<DWORD>(pUnit->GetUnitID()) : 0;
}

UnitState *StateOf(CUnit *pUnit) {
  const int i = IndexOf(pUnit);
  if (i < 0 || i >= g_pools.units)
    return nullptr;
  UnitState &s = g_lu[i];
  if (s.dwId == 0 || s.dwId != IdOf(pUnit))
    return nullptr;
  return &s;
}

UnitState *Claim(CUnit *pUnit) {
  const int i = IndexOf(pUnit);
  if (i < 0 || i >= g_pools.units)
    return nullptr;
  UnitState &s = g_lu[i];
  const DWORD id = IdOf(pUnit);
  if (s.dwId != id) {
    memset(&s, 0, sizeof s);
    s.dwId = id;
  }
  return &s;
}

const UnitHooks *HooksOf(int nType) {
  const int n = nType >= 0 && nType < static_cast<int>(g_game.unitHooks.size()) ? g_game.unitHooks[nType] : -1;
  return n >= 0 ? &g_unitHooks[n] : nullptr;
}

const OrderSpec *SpecOf(int nOrder) {
  const int n = nOrder >= 0 && nOrder < static_cast<int>(g_game.orderSpecs.size()) ? g_game.orderSpecs[nOrder] : -1;
  return n >= 0 ? &g_orderSpecs[n] : nullptr;
}

// The unit's slot for an order, made when bMake (its charges full), or null.
OrderSlot *SlotOf(CUnit *pUnit, int nOrder, bool bMake) {
  UnitState *st = bMake ? Claim(pUnit) : StateOf(pUnit);
  if (st == nullptr)
    return nullptr;
  for (OrderSlot &o : st->o)
    if (o.wOrder == nOrder)
      return &o;
  if (!bMake)
    return nullptr;
  for (OrderSlot &o : st->o)
    if (o.wOrder == 0) {
      memset(&o, 0, sizeof o);
      o.wOrder = static_cast<WORD>(nOrder);
      const OrderSpec *sp = SpecOf(nOrder);
      if (sp && sp->nCharges > 0) {
        o.bCharges = static_cast<BYTE>(sp->nCharges);
        o.bFlags |= kCharged;
      }
      return &o;
    }
  return nullptr;
}

// The whole unit a subunit (a turret) is part of.
CUnit *Whole(CUnit *pUnit) {
  if (pUnit && (gulUnitTypeFlags[pUnit->wType] & kSubunit) && pUnit->subunit)
    return pUnit->subunit;
  return pUnit;
}

bool Idle(CUnit *pUnit) {
  const int o = pUnit->bOrderId;
  return o == gubUnitOrderGuard[pUnit->wType] || o == kOrderGuard || o == kOrderPlayerGuard;
}

// A unit damaged by another: who hit it last, for the killer on_death is given.
void Damaged(CUnit *pUnit, CUnit *pSource) {
  const int i = IndexOf(pUnit);
  if (i >= 0 && i < std::min(g_pools.units, POOL_UNITS_STORE))
    g_killedBy[i] = pSource != nullptr ? IdOf(Whole(pSource)) : 0;
}

void QueueEvent(const Event &e) {
  if (g_gs.nEvents >= kEventsMax) {
    Note("an event was passed over: the frame's queue holds 1024", true);
    return;
  }
  g_events[g_gs.nEvents++] = e;
}

// What a unit dead this frame kept under the key, null for none.
const Value *DyingValue(DWORD dwId, int nKey) {
  for (DWORD i = 0; i < g_dying.n && i < static_cast<DWORD>(kDyingMax); ++i)
    if (g_dying.a[i].dwId == dwId)
      for (int k = 0; k < kUnitValues; ++k)
        if (g_dying.a[i].v[k].wKey == nKey)
          return &g_dying.a[i].v[k];
  return nullptr;
}

// ---- the trace of what the scripts do, and the mode's decisions (presentation) -----------------------
// With the trace on, each act the scripts make on the game is kept with its frame, the unit and
// the hook it was made in, the scripts' log lines among them, and each brain and card "ai" call
// with what it decided: what a mode's AI is doing, for a tool to show (the live channel's
// script_trace, --mode-trace from the first frame). Neither is read by the game or a script, and
// both go in no save and no hash and are dropped as a game starts, so a game plays the same.

const int kTraceKept = 4000;  // the rows kept, the oldest dropped

struct TraceRow {
  unsigned dwAt = 0;  // the row's number, so a reader asks for the rows past one it has
  unsigned dwFrame = 0;
  int nUnit = -1;  // the unit's place in the table, -1 for an act that is no unit's
  int nType = -1, nPlayer = -1;
  std::string act, from, detail;
};

std::deque<TraceRow> g_trace;
unsigned g_nTraced;             // rows ever
bool g_bTrace;                  // whether the trace is on
const char *g_pszTraceWhat;     // the hook being called, named on the rows its call makes
const std::string *g_pTraceFn;

// the mode's own decisions: one row for each brain and each card "ai" call, with what it
// decided (AiOpen, AiChose, AiClose). The row is open while the call runs, so what the call
// does fills it in.
const int kAiKept = 512;
std::deque<ModLuaAiCall> g_ai;
unsigned g_nAiCalls;            // calls ever
ModLuaAiCall g_aiOpen;
bool g_bAiOpen;

void Trace(const char *pszAct, const CUnit *pUnit, const char *pszFormat, ...) {
  if (!g_bTrace)
    return;
  char szDetail[256] = "";
  if (pszFormat != nullptr) {
    va_list args;
    va_start(args, pszFormat);
    vsnprintf(szDetail, sizeof szDetail, pszFormat, args);
    va_end(args);
  }
  TraceRow r;
  r.dwAt = g_nTraced++;
  r.dwFrame = g_gs.dwFrame;
  if (pUnit != nullptr) {
    r.nUnit = IndexOf(pUnit);
    r.nType = pUnit->wType;
    r.nPlayer = pUnit->bPlayer;
  }
  r.act = pszAct;
  if (g_pszTraceWhat != nullptr)
    r.from = g_pTraceFn != nullptr && !g_pTraceFn->empty() ? *g_pTraceFn + " (" + g_pszTraceWhat + ")"
                                                           : g_pszTraceWhat;
  r.detail = szDetail;
  g_trace.push_back(r);
  if (g_trace.size() > static_cast<size_t>(kTraceKept))
    g_trace.pop_front();
  // a call that acts on the game decided something, whatever the act; its own words did not
  if (g_bAiOpen && strcmp(pszAct, "log") != 0 && strcmp(pszAct, "error") != 0)
    g_aiOpen.bChose = true;
}

// A brain's or a card ai's call about to be made, the order it gave (the first, where it gave
// several), and the call done and kept. Off with the trace, so a game that no tool watches
// keeps nothing.
void AiOpen(const char *pszKind, const CUnit *pUnit, const std::string &fn) {
  if (!g_bTrace)
    return;
  g_aiOpen = ModLuaAiCall();
  g_aiOpen.dwAt = g_nAiCalls++;
  g_aiOpen.dwFrame = g_gs.dwFrame;
  if (pUnit != nullptr) {
    g_aiOpen.nUnit = IndexOf(pUnit);
    g_aiOpen.nType = pUnit->wType;
    g_aiOpen.nPlayer = pUnit->bPlayer;
  }
  g_aiOpen.kind = pszKind;
  g_aiOpen.fn = fn;
  g_bAiOpen = true;
}

void AiChose(int nOrder, int nTarget, int x, int y) {
  if (!g_bAiOpen || g_aiOpen.nOrder >= 0)
    return;
  g_aiOpen.bChose = true;
  g_aiOpen.nOrder = nOrder;
  g_aiOpen.nTarget = nTarget;
  g_aiOpen.x = x;
  g_aiOpen.y = y;
}

void AiClose(const char *pszWhy = nullptr) {
  if (!g_bAiOpen)
    return;
  g_bAiOpen = false;
  if (pszWhy != nullptr)
    g_aiOpen.why = pszWhy;
  else if (!g_aiOpen.bChose)
    g_aiOpen.why = "it did nothing";
  g_ai.push_back(g_aiOpen);
  if (g_ai.size() > static_cast<size_t>(kAiKept))
    g_ai.pop_front();
}

// ---- the Lua state ---------------------------------------------------------------------------------

struct Heap {
  size_t n = 0;
};

void *Alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  Heap *h = static_cast<Heap *>(ud);
  if (nsize == 0) {
    free(ptr);
    h->n -= osize;
    return nullptr;
  }
  if (h->n - osize + nsize > kHeapMax)
    return nullptr;
  void *p = realloc(ptr, nsize);
  if (p != nullptr)
    h->n = h->n - osize + nsize;
  return p;
}

lua_State *g_L;
Heap g_heap;
// The presentation VM's own heap and state (Build): the same scripts with the readers alone, for
// a call made for one viewer. Nothing it keeps reaches the game's state.
lua_State *g_LView;
Heap g_viewHeap;
long long g_nCallLeft, g_nFrameLeft;
int g_nDepth;
bool g_bTop;   // the files' tops running: the game is not there to act on
bool g_bSync;  // on_damaged running, inside the game's damage: values only

void Interrupt(lua_State *L, int gc) {
  if (gc >= 0)
    return;
  // once past it, every step after is past it, so a pcall in a loop cannot go on
  if (--g_nCallLeft >= 0 && --g_nFrameLeft >= 0)
    return;
  lua_Debug ar;
  char sz[256];
  if (lua_getinfo(L, 0, "sl", &ar) && ar.currentline > 0)
    snprintf(sz, sizeof sz, "%s:%d: ran past its budget of steps", ar.short_src, ar.currentline);
  else
    snprintf(sz, sizeof sz, "ran past its budget of steps");
  lua_pushstring(L, sz);
  lua_error(L);
}

// ---- JSON and Lua values ---------------------------------------------------------------------------
// A mode's settings, its generated run and what its generate gives back cross between the
// loader and the scripts as JSON, so the loader never holds a Lua value and a run written into a
// replay is text. An object's members keep their order, so the same table gives the same text.

void PushJson(lua_State *L, const PortJson &j) {
  switch (j.kind) {
  case PortJson::BOOL:
    lua_pushboolean(L, j.b);
    return;
  case PortJson::NUMBER:
    lua_pushnumber(L, j.d);
    return;
  case PortJson::STRING:
    lua_pushstring(L, j.s.c_str());
    return;
  case PortJson::ARRAY:
    lua_createtable(L, static_cast<int>(j.items.size()), 0);
    for (size_t i = 0; i < j.items.size(); ++i) {
      PushJson(L, j.items[i]);
      lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return;
  case PortJson::OBJECT:
    lua_createtable(L, 0, static_cast<int>(j.members.size()));
    for (const PortJsonMember &m : j.members) {
      PushJson(L, m.second);
      lua_setfield(L, -2, m.first.c_str());
    }
    return;
  default:
    lua_pushnil(L);
    return;
  }
}

// A Lua value as JSON. A table whose keys are 1 to n is an array, any other table an object
// whose members come out in the order of their names, so the text is the same on every machine
// whatever the hash order. False with why for a value JSON has no shape for.
bool ValueToJson(lua_State *L, int idx, PortJson *pOut, int nDepth, std::string *pWhy) {
  if (nDepth > 16)
    return Refuse(pWhy, "a table inside a table more than sixteen deep");
  idx = lua_absindex(L, idx);
  switch (lua_type(L, idx)) {
  case LUA_TNIL:
  case LUA_TNONE:
    pOut->kind = PortJson::NULLV;
    return true;
  case LUA_TBOOLEAN:
    pOut->kind = PortJson::BOOL;
    pOut->b = lua_toboolean(L, idx) != 0;
    return true;
  case LUA_TNUMBER: {
    const double d = lua_tonumber(L, idx);
    if (!(d == d) || d > 1e300 || d < -1e300)
      return Refuse(pWhy, "a number that is not a number");
    pOut->kind = PortJson::NUMBER;
    pOut->d = d;
    return true;
  }
  case LUA_TSTRING:
    pOut->kind = PortJson::STRING;
    pOut->s = lua_tostring(L, idx);
    return true;
  case LUA_TTABLE:
    break;
  default:
    return Refuse(pWhy, std::string("a ") + lua_typename(L, lua_type(L, idx)) + ", which is not data");
  }
  // an array when every key is 1 to n
  const int n = static_cast<int>(lua_objlen(L, idx));
  size_t nKeys = 0;
  bool bArray = true;
  lua_pushnil(L);
  while (lua_next(L, idx)) {
    ++nKeys;
    if (lua_type(L, -2) != LUA_TNUMBER || lua_tonumber(L, -2) < 1 || lua_tonumber(L, -2) > n ||
        lua_tonumber(L, -2) != floor(lua_tonumber(L, -2)))
      bArray = false;
    lua_pop(L, 1);
  }
  if (bArray && static_cast<size_t>(n) == nKeys) {
    pOut->kind = PortJson::ARRAY;
    for (int i = 1; i <= n; ++i) {
      lua_rawgeti(L, idx, i);
      PortJson item;
      const bool bOk = ValueToJson(L, -1, &item, nDepth + 1, pWhy);
      lua_pop(L, 1);
      if (!bOk)
        return false;
      pOut->items.push_back(item);
    }
    return true;
  }
  pOut->kind = PortJson::OBJECT;
  std::vector<std::string> keys;
  lua_pushnil(L);
  while (lua_next(L, idx)) {
    if (lua_type(L, -2) == LUA_TSTRING)
      keys.push_back(lua_tostring(L, -2));
    else if (lua_type(L, -2) == LUA_TNUMBER) {
      char sz[40];
      snprintf(sz, sizeof sz, "%.17g", lua_tonumber(L, -2));
      keys.push_back(sz);
    } else {
      lua_pop(L, 2);
      return Refuse(pWhy, "a table whose key is neither a name nor a number");
    }
    lua_pop(L, 1);
  }
  std::sort(keys.begin(), keys.end());
  for (const std::string &k : keys) {
    lua_getfield(L, idx, k.c_str());
    PortJsonMember m;
    m.first = k;
    const bool bOk = ValueToJson(L, -1, &m.second, nDepth + 1, pWhy);
    lua_pop(L, 1);
    if (!bOk)
      return false;
    pOut->members.push_back(m);
  }
  return true;
}

// ---- the API ---------------------------------------------------------------------------------------

void NotTop(lua_State *L, const char *pszName) {
  if (g_bTop)
    luaL_error(L, "%s is for hooks: a script's top runs as the game is made, before there is a game", pszName);
}

void World(lua_State *L, const char *pszName) {
  NotTop(L, pszName);
  if (g_bSync)
    luaL_error(L, "%s cannot be called in on_damaged, which reads, keeps values and returns the damage; change the "
                  "game in on_hit", pszName);
}

// The unit argument i: a number from a hook or hx.units; null when it no longer lives.
CUnit *U(lua_State *L, int i) {
  if (lua_type(L, i) != LUA_TNUMBER)
    luaL_error(L, "argument %d is a unit, a number from a hook's e or hx.units", i);
  return UnitOf(static_cast<DWORD>(lua_tonumber(L, i)));
}

// Whether a number is a row the game has of that table. A script's number is checked before
// anything indexes a table with it: a mod that passes a unit number where a type belongs, or a
// row a mod of its own would have added, would otherwise read past the table's end.
// The ten tables a script may name, in types.json's own order.
const char *const kTypeTables[] = {"units",  "weapons",  "flingy", "sprites", "images",
                                   "orders", "techs", "upgrades", "sounds", "portraits"};

bool TableKnown(const char *pszTable) {
  for (const char *psz : kTypeTables)
    if (strcmp(pszTable, psz) == 0)
      return true;
  return false;
}

bool TypeNumberHas(const char *pszTable, int n) {
  const unsigned t = static_cast<unsigned>(n);
  if (n < 0)
    return false;
  if (strcmp(pszTable, "units") == 0)
    return UnitTypeValid(t);
  if (strcmp(pszTable, "weapons") == 0)
    return WeaponTypeValid(t);
  if (strcmp(pszTable, "flingy") == 0)
    return FlingyTypeValid(t);
  if (strcmp(pszTable, "sprites") == 0)
    return SpriteTypeValid(t);
  if (strcmp(pszTable, "images") == 0)
    return ImageTypeValid(t);
  if (strcmp(pszTable, "orders") == 0)
    return OrderTypeValid(t);
  if (strcmp(pszTable, "techs") == 0)
    return TechTypeValid(t);
  if (strcmp(pszTable, "upgrades") == 0)
    return UpgradeTypeValid(t);
  if (strcmp(pszTable, "sounds") == 0)
    return SoundTypeValid(t);
  if (strcmp(pszTable, "portraits") == 0)
    return PortraitTypeValid(t);
  return false;  // a table this build does not know: nothing may index it
}

// The player a script named: a slot of the game being played. A script that passes anything
// else -- a unit number instead of its owner, as a mod has -- would have the game read its
// player tables past their end, so it is told what it passed instead.
int PlayerOf(lua_State *L, const char *pszWhere, double d) {
  const int n = static_cast<int>(d);
  if (static_cast<double>(n) != d || n < 0 || n >= PlayerSlots())
    luaL_error(L, "%s: %g is no player of this game, which has 0 to %d (hx.owner gives a unit's)",
               pszWhere, d, PlayerSlots() - 1);
  return n;
}

int TypeArg(lua_State *L, int i, const char *pszTable) {
  if (lua_type(L, i) == LUA_TNUMBER) {
    const double d = lua_tonumber(L, i);
    const int n = static_cast<int>(d);
    if (static_cast<double>(n) != d || !TypeNumberHas(pszTable, n))
      luaL_error(L, "no %s of this game is number %g (hx.id(\"%s\", name) gives a number, and a "
                    "unit number is not a type)",
                 pszTable, d, pszTable);
    return n;
  }
  if (lua_type(L, i) == LUA_TSTRING) {
    const int n = ModTypeByName(pszTable, lua_tostring(L, i));
    if (n < 0)
      luaL_error(L, "no %s is named %s", pszTable, lua_tostring(L, i));
    return n;
  }
  luaL_error(L, "argument %d names one of the %s, by its name or number", i, pszTable);
  return -1;
}

int OrderArg(lua_State *L, int i) {
  return TypeArg(L, i, "orders");
}

void PushUnit(lua_State *L, DWORD dwId) {
  if (dwId != 0)
    lua_pushnumber(L, static_cast<double>(dwId));
  else
    lua_pushnil(L);
}

// a field of an options table
double OptNum(lua_State *L, int t, const char *pszKey, double d, bool *pbHas = nullptr) {
  if (!lua_istable(L, t))
    return d;
  lua_rawgetfield(L, t, pszKey);
  const bool bHas = lua_isnumber(L, -1) != 0;
  const double v = bHas ? lua_tonumber(L, -1) : d;
  lua_pop(L, 1);
  if (pbHas)
    *pbHas = bHas;
  return v;
}

int OptType(lua_State *L, int t, const char *pszKey, const char *pszTable) {
  if (!lua_istable(L, t))
    return -1;
  lua_rawgetfield(L, t, pszKey);
  int n = -1;
  if (!lua_isnil(L, -1))
    n = TypeArg(L, lua_gettop(L), pszTable);
  lua_pop(L, 1);
  return n;
}

// A player field of an options table: -1 when the table does not give it, and checked when it
// does.
int OptPlayer(lua_State *L, int t, const char *pszKey, const char *pszCall) {
  bool bHas = false;
  const double d = OptNum(L, t, pszKey, -1, &bHas);
  if (!bHas)
    return -1;
  char sz[80];
  snprintf(sz, sizeof sz, "%s's %s", pszCall, pszKey);
  return PlayerOf(L, sz, d);
}

int OptBool(lua_State *L, int t, const char *pszKey, int nDefault) {
  if (!lua_istable(L, t))
    return nDefault;
  lua_rawgetfield(L, t, pszKey);
  const int n = lua_isnil(L, -1) ? nDefault : lua_toboolean(L, -1);
  lua_pop(L, 1);
  return n;
}

struct Find {
  int x, y, r;
  int nType, nOwner, nEnemyOf, nAllyOf, nBuildings;
  int nMax;
  std::vector<CUnit *> *pOut;
};

bool Picks(CUnit *pUnit, const Find &f) {
  if (!Alive(pUnit) || Hidden(pUnit) || (gulUnitTypeFlags[pUnit->wType] & kSubunit))
    return false;
  if (f.nBuildings == 0 && (gulUnitTypeFlags[pUnit->wType] & kBuilding))
    return false;
  if (f.nType >= 0 && pUnit->wType != f.nType)
    return false;
  if (f.nOwner >= 0 && pUnit->bPlayer != f.nOwner)
    return false;
  if (f.nEnemyOf >= 0 && !IsEnemyUnit(f.nEnemyOf, pUnit))
    return false;
  if (f.nAllyOf >= 0 && IsEnemyUnit(f.nAllyOf, pUnit))
    return false;
  return true;
}

int __fastcall FindPick(CUnit *pUnit, void *pv) {
  Find &f = *static_cast<Find *>(pv);
  if (static_cast<int>(f.pOut->size()) >= f.nMax)
    return 0;
  const long long dx = static_cast<int>(pUnit->pSprite->wPosX) - f.x, dy = static_cast<int>(pUnit->pSprite->wPosY) - f.y;
  if (dx * dx + dy * dy <= static_cast<long long>(f.r) * f.r && Picks(pUnit, f))
    f.pOut->push_back(pUnit);
  return 0;
}

int HxUnits(lua_State *L) {
  NotTop(L, "hx.units");
  Find f;
  bool bArea = false;
  f.x = static_cast<int>(OptNum(L, 1, "x", 0, &bArea));
  f.y = static_cast<int>(OptNum(L, 1, "y", 0));
  f.r = static_cast<int>(OptNum(L, 1, "radius", 0));
  bArea = bArea && f.r > 0;
  f.nType = OptType(L, 1, "type", "units");
  // the three player fields: a player of this game, never a unit number (IsEnemyUnit reads the
  // alliance table with it)
  f.nOwner = OptPlayer(L, 1, "owner", "hx.units");
  f.nEnemyOf = OptPlayer(L, 1, "enemies_of", "hx.units");
  f.nAllyOf = OptPlayer(L, 1, "allies_of", "hx.units");
  f.nBuildings = OptBool(L, 1, "buildings", 1);
  f.nMax = static_cast<int>(std::max(0.0, std::min(1700.0, OptNum(L, 1, "max", 1700))));
  std::vector<CUnit *> out;
  f.pOut = &out;
  if (bArea) {
    mrect_t rc;
    rc.left = f.x - f.r;
    rc.top = f.y - f.r;
    rc.right = f.x + f.r;
    rc.bottom = f.y + f.r;
    find_foreach_unit(&rc, FindPick, &f);
  } else {
    for (CUnit *p = CUnit::GetUsedList(); p != nullptr && static_cast<int>(out.size()) < f.nMax; p = p->pNext)
      if (Picks(p, f))
        out.push_back(p);
  }
  lua_createtable(L, static_cast<int>(out.size()), 0);
  for (size_t i = 0; i < out.size(); ++i) {
    lua_pushnumber(L, static_cast<double>(IdOf(out[i])));
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  return 1;
}

int HxAlive(lua_State *L) {
  NotTop(L, "hx.alive");
  lua_pushboolean(L, lua_type(L, 1) == LUA_TNUMBER && U(L, 1) != nullptr);
  return 1;
}

int HxType(lua_State *L) {
  NotTop(L, "hx.type");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushinteger(L, p->wType);
  return 1;
}

int HxIs(lua_State *L) {
  NotTop(L, "hx.is");
  CUnit *p = U(L, 1);
  const int t = TypeArg(L, 2, "units");
  lua_pushboolean(L, p != nullptr && p->wType == t);
  return 1;
}

int HxOwner(lua_State *L) {
  NotTop(L, "hx.owner");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushinteger(L, p->bPlayer);
  return 1;
}

int HxPosition(lua_State *L) {
  NotTop(L, "hx.position");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushinteger(L, p->pSprite->wPosX);
  lua_pushinteger(L, p->pSprite->wPosY);
  return 2;
}

int HxIsBuilding(lua_State *L) {
  NotTop(L, "hx.is_building");
  CUnit *p = U(L, 1);
  lua_pushboolean(L, p != nullptr && (gulUnitTypeFlags[p->wType] & kBuilding) != 0);
  return 1;
}

// Whether a's weapons can hit b at all: an air unit for a unit with no air weapon, a burrowed or
// out-of-play unit, and a unit a's own state forbids are not, as classic's targeting has it.
int HxCanTarget(lua_State *L) {
  NotTop(L, "hx.can_target");
  CUnit *a = U(L, 1);
  CUnit *b = U(L, 2);
  lua_pushboolean(L, a != nullptr && b != nullptr && a->CanTarget(b, 0) != 0);
  return 1;
}

// Whether a can walk to b: always for a flyer or a flying b, else whether the ground between
// them joins up, so a unit on a cliff or an island is not one a ground unit can walk onto.
int HxCanReach(lua_State *L) {
  NotTop(L, "hx.can_reach");
  CUnit *a = U(L, 1);
  CUnit *b = U(L, 2);
  bool bCan = false;
  if (a != nullptr && b != nullptr)
    bCan = (a->dwFlags & 4) != 0 || (b->dwFlags & 4) != 0 || UnitCanReachGroundTarget(a, b) != 0;
  lua_pushboolean(L, bCan);
  return 1;
}

int HxEnemies(lua_State *L) {
  NotTop(L, "hx.enemies");
  CUnit *a = U(L, 1);
  CUnit *b = U(L, 2);
  lua_pushboolean(L, a != nullptr && b != nullptr && IsEnemyUnit(a->bPlayer, b) != 0);
  return 1;
}

int HxLife(lua_State *L) {
  NotTop(L, "hx.life");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushnumber(L, p->lHitPoints / 256.0);
  return 1;
}

int HxMaxLife(lua_State *L) {
  NotTop(L, "hx.max_life");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushnumber(L, HandicapLife(gxUnitLife[p->wType], p->bPlayer) / 256.0);
  return 1;
}

int Fixed(lua_State *L, int i) {
  const double d = luaL_checknumber(L, i);
  return static_cast<int>(floor(std::max(-8388607.0, std::min(8388607.0, d)) * 256 + 0.5));
}

int HxSetLife(lua_State *L) {
  World(L, "hx.set_life");
  CUnit *p = U(L, 1);
  const int n = Fixed(L, 2);
  if (p == nullptr || (p->dwFlags & kInvincible)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  if (n <= 0)
    p->DamageApply(p->lHitPoints, nullptr, p->bPlayer, 0);
  else
    p->SetLife(n);
  lua_pushboolean(L, 1);
  return 1;
}

int HxShields(lua_State *L) {
  NotTop(L, "hx.shields");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushnumber(L, static_cast<LONG>(p->lShields) / 256.0);
  return 1;
}

int MaxShields(CUnit *p) {
  return gbUnitHasShields[p->wType] ? HandicapShields(guwUnitEnergy[p->wType] << 8, p->bPlayer) : 0;
}

int HxMaxShields(lua_State *L) {
  NotTop(L, "hx.max_shields");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushnumber(L, MaxShields(p) / 256.0);
  return 1;
}

int HxSetShields(lua_State *L) {
  World(L, "hx.set_shields");
  CUnit *p = U(L, 1);
  const int n = Fixed(L, 2);
  if (p == nullptr || !gbUnitHasShields[p->wType]) {
    lua_pushboolean(L, 0);
    return 1;
  }
  p->lShields = static_cast<DWORD>(std::max(0, std::min(n, MaxShields(p))));
  lua_pushboolean(L, 1);
  return 1;
}

int HxEnergy(lua_State *L) {
  NotTop(L, "hx.energy");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushnumber(L, p->wEnergy / 256.0);
  return 1;
}

int HxMaxEnergy(lua_State *L) {
  NotTop(L, "hx.max_energy");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  lua_pushnumber(L, (gulUnitTypeFlags[p->wType] & kSpellcaster) ? p->GetMaxEnergy() / 256.0 : 0.0);
  return 1;
}

int HxSetEnergy(lua_State *L) {
  World(L, "hx.set_energy");
  CUnit *p = U(L, 1);
  const int n = Fixed(L, 2);
  if (p == nullptr || !(gulUnitTypeFlags[p->wType] & kSpellcaster)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  p->wEnergy = static_cast<WORD>(std::max(0, std::min(n, p->GetMaxEnergy())));
  lua_pushboolean(L, 1);
  return 1;
}

int CallDamaged(CUnit *pUnit, int nWeapon, CUnit *pSource, int nPlayer, int nDamage);

// Damage as a weapon of type nType does it: shields first, less their upgrade, then armour (both
// but for ignore_armour), then the type's share against the unit's size (CUnit::Damage). What it
// took, 8.8.
int DamageUnit(CUnit *pUnit, int nAmount, int nType, CUnit *pSource, int nPlayer) {
  if (!Alive(pUnit) || (pUnit->dwFlags & kInvincible))
    return 0;
  int nTotal = nAmount;
  if (pSource != nullptr)
    nTotal = ModStatusDamageDealt(pSource, nTotal);
  nTotal = ModStatusDamageTaken(pUnit, nTotal);
  nTotal = CallDamaged(pUnit, -1, pSource, nPlayer, nTotal);
  if (!Alive(pUnit))
    return 0;
  if (nTotal < 0x80)
    nTotal = 0x80;
  int nShields = 0;
  if (gbUnitHasShields[pUnit->wType] && static_cast<LONG>(pUnit->lShields) >= 0x100) {
    if (nType != 4) {
      const int nUp = get_upgrade_level(pUnit->bPlayer, 0xF) << 8;
      nTotal = nUp >= nTotal ? 0x80 : nTotal - nUp;
    }
    nShields = nTotal > static_cast<LONG>(pUnit->lShields) ? static_cast<LONG>(pUnit->lShields) : nTotal;
    nTotal -= nShields;
  }
  if (nType != 4) {
    const int nArmor = ModStatusArmor(pUnit, (pUnit->ArmorUpgrades() + gubUnitArmor_g[pUnit->wType]) << 8);
    nTotal -= nTotal > nArmor ? nArmor : nTotal;
  }
  int nHit = compute_damage_from_types(nTotal, static_cast<BYTE>(nType), gubUnitArmorClass_g[pUnit->wType]);
  if (nShields == 0 && nHit < 0x80)
    nHit = 0x80;
  const int nTaken = std::min<int>(nHit, pUnit->lHitPoints) + nShields;
  Damaged(pUnit, pSource);
  pUnit->DamageApply(nHit, pSource, nPlayer, 1);
  if (nShields)
    pUnit->lShields -= nShields;
  return nTaken;
}

// classic's damage types by their number: independent always takes half a point, as classic's does
const char *const kDamageTypes[] = {"independent", "explosive", "concussive", "normal", "ignore_armour"};

int HxDamage(lua_State *L) {
  World(L, "hx.damage");
  CUnit *p = U(L, 1);
  const int n = Fixed(L, 2);
  int nType = 3;
  CUnit *pSource = nullptr;
  if (lua_istable(L, 3)) {
    lua_rawgetfield(L, 3, "type");
    if (lua_isstring(L, -1)) {
      nType = -1;
      for (int i = 0; i < 5; ++i)
        if (strcmp(lua_tostring(L, -1), kDamageTypes[i]) == 0)
          nType = i;
      if (nType < 0)
        luaL_error(L, "hx.damage's type is normal, explosive, concussive, ignore_armour or independent");
    }
    lua_pop(L, 1);
    lua_rawgetfield(L, 3, "source");
    if (lua_isnumber(L, -1))
      pSource = UnitOf(static_cast<DWORD>(lua_tonumber(L, -1)));
    lua_pop(L, 1);
  }
  if (p == nullptr || n <= 0) {
    lua_pushnumber(L, 0);
    return 1;
  }
  // with no source the death is nobody's kill: neutral takes it, so no player's score or kill
  // count moves (a mode's perimeter penalty)
  const int nPlayer = pSource ? pSource->bPlayer : PLAYER_NEUTRAL;
  const int nTaken = DamageUnit(p, n, nType, pSource, nPlayer);
  Trace("damage", p, "%d of %s damage, %d taken", n / 256, kDamageTypes[nType < 0 ? 3 : nType],
        nTaken / 256);
  lua_pushnumber(L, nTaken / 256.0);
  return 1;
}

int HxHeal(lua_State *L) {
  World(L, "hx.heal");
  CUnit *p = U(L, 1);
  const int n = Fixed(L, 2);
  if (p == nullptr || n <= 0) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const LONG was = p->lHitPoints;
  p->SetLife(std::min<long long>(static_cast<long long>(was) + n, HandicapLife(gxUnitLife[p->wType], p->bPlayer)));
  lua_pushnumber(L, (p->lHitPoints - was) / 256.0);
  return 1;
}

int HxKill(lua_State *L) {
  World(L, "hx.kill");
  CUnit *p = U(L, 1);
  if (p == nullptr || (p->dwFlags & kInvincible)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  Trace("kill", p, nullptr);
  p->DamageApply(p->lHitPoints, nullptr, p->bPlayer, 0);
  lua_pushboolean(L, 1);
  return 1;
}

// The unit put down at the free place nearest x, y, as Recall puts its units (port_modstatus.cpp).
bool Place(CUnit *pUnit, int x, int y) {
  const mpoint_t old = MapPoint(pUnit->pSprite->wPosX, pUnit->pSprite->wPosY);
  mpoint_t dest = MapPoint(x, y);
  pUnit->SetUnitPosition(dest.x, dest.y);
  mpoint_t pos;
  if (!FindPlaceForUnit(pUnit, &dest, &pos, nullptr, 0, 0)) {
    pUnit->SetUnitPosition(old.x, old.y);
    return false;
  }
  if (!Hidden(pUnit)) {
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
  return true;
}

int HxMove(lua_State *L) {
  World(L, "hx.move");
  CUnit *p = U(L, 1);
  const int x = static_cast<int>(luaL_checknumber(L, 2)), y = static_cast<int>(luaL_checknumber(L, 3));
  lua_pushboolean(L, p != nullptr && !(gulUnitTypeFlags[p->wType] & kBuilding) && !Hidden(p) && Place(p, x, y));
  return 1;
}

void EachUnit(lua_State *L, int i, std::vector<CUnit *> *pOut);

// One unit or a list of them, so a wave takes one order rather than forty.
int HxOrder(lua_State *L) {
  World(L, "hx.order");
  std::vector<CUnit *> units;
  EachUnit(L, 1, &units);
  int nOrder = -1;
  if (lua_type(L, 2) == LUA_TSTRING) {
    const char *psz = lua_tostring(L, 2);
    nOrder = strcmp(psz, "move") == 0            ? kOrderMove
             : strcmp(psz, "attack") == 0        ? kOrderAttack
             : strcmp(psz, "attack_move") == 0   ? kOrderAttack
             : strcmp(psz, "patrol") == 0        ? kOrderPatrol
             : strcmp(psz, "stop") == 0          ? kOrderStop
             : strcmp(psz, "hold") == 0          ? kOrderHold
                                                 : -1;
  }
  if (nOrder < 0)
    nOrder = OrderArg(L, 2);
  if (!OrderTypeValid(static_cast<unsigned>(nOrder)))
    luaL_error(L, "hx.order: no order %d", nOrder);
  const bool bPlace = lua_gettop(L) >= 4;
  const int x = bPlace ? static_cast<int>(luaL_checknumber(L, 3)) : 0;
  const int y = bPlace ? static_cast<int>(luaL_checknumber(L, 4)) : 0;
  CUnit *pTarget = !bPlace && lua_gettop(L) >= 3 && !lua_isnil(L, 3) ? U(L, 3) : nullptr;
  if (!bPlace && lua_gettop(L) >= 3 && !lua_isnil(L, 3) && pTarget == nullptr) {
    lua_pushboolean(L, 0);
    return 1;
  }
  int nGiven = 0;
  CUnit *pFirst = nullptr;  // the one the trace's row names, when one alone took the order
  for (CUnit *p : units) {
    if (!Alive(p) || ((gulUnitTypeFlags[p->wType] & kBuilding) && nOrder != kOrderStop))
      continue;
    if (pFirst == nullptr)
      pFirst = p;
    if (bPlace)
      p->SetOrder_xy(static_cast<order_id_t>(nOrder), x, y);
    else if (pTarget != nullptr)
      p->SetOrder_CUnit(static_cast<order_id_t>(nOrder), pTarget);
    else
      p->SetOrder(static_cast<order_id_t>(nOrder));
    ++nGiven;
  }
  if (bPlace)
    Trace("order", nGiven == 1 ? pFirst : nullptr, "order %d to %d, at %d, %d", nOrder, nGiven, x, y);
  else
    Trace("order", nGiven == 1 ? pFirst : nullptr, "order %d to %d, on %d", nOrder, nGiven,
          pTarget != nullptr ? IndexOf(pTarget) : -1);
  if (nGiven > 0)
    AiChose(nOrder, pTarget != nullptr ? IndexOf(pTarget) : -1, bPlace ? x : -1, bPlace ? y : -1);
  lua_pushboolean(L, nGiven > 0);
  return 1;
}

int HxMake(lua_State *L) {
  World(L, "hx.make");
  const int nType = TypeArg(L, 1, "units");
  const int nOwner = PlayerOf(L, "hx.make's owner", luaL_checknumber(L, 2));
  const int x = static_cast<int>(luaL_checknumber(L, 3)), y = static_cast<int>(luaL_checknumber(L, 4));
  const int nCount = static_cast<int>(std::max(1.0, std::min(64.0, luaL_optnumber(L, 5, 1))));
  const int nFrames = Frames(std::max(0.0, std::min(2700.0, luaL_optnumber(L, 6, 0))));
  // the type and the owner are both checked as they are read (TypeArg, PlayerOf)
  std::vector<int> made;
  PortTrigCreate(nType, x, y, nOwner, nCount, &made, false);
  Trace("make", nullptr, "%d of unit %d for player %d, at %d, %d", static_cast<int>(made.size()), nType,
        nOwner, x, y);
  lua_createtable(L, static_cast<int>(made.size()), 0);
  for (size_t i = 0; i < made.size(); ++i) {
    CUnit *p = CUnit::Head() + made[i];
    if (nFrames > 0)
      p->wTimeToLive = static_cast<WORD>(std::min(nFrames, 0xFFFF));
    lua_pushnumber(L, static_cast<double>(IdOf(p)));
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  return 1;
}

int HxStatus(lua_State *L) {
  World(L, "hx.status");
  CUnit *p = U(L, 1);
  const char *psz = luaL_checkstring(L, 2);
  // whose status it is, the unit's own player unless the script says: checked, since the status
  // keeps it and a kill is credited by it
  const int nPlayer = lua_isnoneornil(L, 3) ? (p ? p->bPlayer : 0)
                                            : PlayerOf(L, "hx.status's player", luaL_checknumber(L, 3));
  if (!ModStatusHasNamed(psz))
    luaL_error(L, "hx.status: the mod has no status %s", psz);
  const bool bPut = p != nullptr && ModStatusApplyNamed(p, psz, nPlayer);
  if (bPut)
    Trace("status", p, "%s, of player %d", psz, nPlayer);
  lua_pushboolean(L, bPut);
  return 1;
}

int HxUnstatus(lua_State *L) {
  World(L, "hx.unstatus");
  CUnit *p = U(L, 1);
  const char *psz = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
  if (p != nullptr) {
    ModStatusRemoveNamed(p, psz);
    Trace("unstatus", p, "%s", psz != nullptr ? psz : "every status");
  }
  return 0;
}

int HxHasStatus(lua_State *L) {
  NotTop(L, "hx.has_status");
  CUnit *p = U(L, 1);
  lua_pushinteger(L, p ? ModStatusStacksNamed(p, luaL_checkstring(L, 2)) : 0);
  return 1;
}

// The statuses on a unit, each with the words a player reads, its time and its side, so that
// what shows a status needs no table of its own.
int HxStatuses(lua_State *L) {
  NotTop(L, "hx.statuses");
  CUnit *p = U(L, 1);
  std::vector<ModStatusOn> on;
  if (p != nullptr)
    ModStatusOnUnit(p, &on);
  lua_createtable(L, static_cast<int>(on.size()), 0);
  for (size_t i = 0; i < on.size(); ++i) {
    lua_createtable(L, 0, 7);
    lua_pushstring(L, on[i].name.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, on[i].label.c_str());
    lua_setfield(L, -2, "name");
    lua_pushnumber(L, on[i].nFramesLeft / static_cast<double>(kFps));
    lua_setfield(L, -2, "left");
    lua_pushnumber(L, on[i].nFrames / static_cast<double>(kFps));
    lua_setfield(L, -2, "duration");
    lua_pushinteger(L, on[i].nStacks);
    lua_setfield(L, -2, "stacks");
    lua_pushinteger(L, on[i].nIcon);
    lua_setfield(L, -2, "icon");
    lua_pushboolean(L, on[i].bGood);
    lua_setfield(L, -2, "good");
    lua_rawseti(L, -2, static_cast<int>(i) + 1);
  }
  return 1;
}

// hx.readout.fx(unit, op, args): an effect on the readout, for this viewer. Presentation, and
// only in the presentation VM, so a call cannot reach the game.
int HxReadoutFx(lua_State *L) {
  NotTop(L, "hx.readout.fx");
  CUnit *p = U(L, 1);
  const char *pszOp = luaL_checkstring(L, 2);
  PortJson args;
  if (lua_istable(L, 3)) {
    std::string why;
    if (!ValueToJson(L, 3, &args, 0, &why))
      luaL_error(L, "hx.readout.fx: %s", why.c_str());
  }
  std::string why;
  if (p != nullptr && !PortReadoutScriptFx(p, pszOp, args, &why))
    luaL_error(L, "%s", why.c_str());
  return 0;
}

int HxSound(lua_State *L) {
  World(L, "hx.sound");
  const int n = TypeArg(L, 1, "sounds");
  snd_pos_t pos;
  pos.x = static_cast<int>(luaL_checknumber(L, 2));
  pos.y = static_cast<int>(luaL_checknumber(L, 3));
  gamesnd_play_mtx(n, 1, pos, 0);
  return 0;
}

int HxShow(lua_State *L) {
  World(L, "hx.show");
  const int n = TypeArg(L, 1, "sprites");
  const int x = static_cast<int>(luaL_checknumber(L, 2)), y = static_cast<int>(luaL_checknumber(L, 3));
  if (CThingy *pThingy = CThingy::AllocThingy(n, x, y, 0)) {
    pThingy->pSprite->bElevationLevel = static_cast<BYTE>(luaL_optnumber(L, 4, 4));
    pThingy->UpdateVisibility();
  }
  return 0;
}

int HxOverlay(lua_State *L) {
  World(L, "hx.overlay");
  CUnit *p = U(L, 1);
  const int n = TypeArg(L, 2, "images");
  if (p != nullptr && !Hidden(p))
    p->CreateOverlay(n, 0, 0, 0);
  return 0;
}

// ---- a mode's own calls (port_mode.h) ------------------------------------------------------------------

// Each unit of the argument, which is one unit or a list of them.
void EachUnit(lua_State *L, int i, std::vector<CUnit *> *pOut) {
  if (lua_istable(L, i)) {
    const int n = static_cast<int>(lua_objlen(L, i));
    for (int k = 1; k <= n; ++k) {
      lua_rawgeti(L, i, k);
      if (lua_isnumber(L, -1))
        if (CUnit *p = UnitOf(static_cast<DWORD>(lua_tonumber(L, -1))))
          pOut->push_back(p);
      lua_pop(L, 1);
    }
    return;
  }
  if (CUnit *p = U(L, i))
    pOut->push_back(p);
}

// The players of a list argument, as a bit each; every player when there is no list.
DWORD PlayersOf(lua_State *L, int i) {
  if (!lua_istable(L, i))
    return 0xFFFFFFFFu;
  DWORD dw = 0;
  const int n = static_cast<int>(lua_objlen(L, i));
  for (int k = 1; k <= n; ++k) {
    lua_rawgeti(L, i, k);
    if (!lua_isnumber(L, -1))
      luaL_error(L, "a players list holds the players' numbers, and its %d%s is not a number", k,
                 k == 1 ? "st" : k == 2 ? "nd" : k == 3 ? "rd" : "th");
    // checked, since a player's bit is set by it and the game reads its tables by it
    dw |= 1u << (PlayerOf(L, "a players list", lua_tonumber(L, -1)) & 31);
    lua_pop(L, 1);
  }
  return dw;
}

int HxSetting(lua_State *L) {
  const PortJson &v = g_settings[luaL_checkstring(L, 1)];
  PushJson(L, v);
  return 1;
}

// The order given as the unit's own button would give it: its tech's energy is taken by the
// order's handler, the animation plays and the blocks run.
int HxCast(lua_State *L) {
  World(L, "hx.cast");
  CUnit *p = U(L, 1);
  const int nOrder = OrderArg(L, 2);
  if (!OrderTypeValid(static_cast<unsigned>(nOrder)))
    luaL_error(L, "hx.cast: no order %d", nOrder);
  bool bPlace = false;
  int x = 0, y = 0;
  CUnit *pTarget = nullptr;
  if (lua_gettop(L) >= 4) {
    bPlace = true;
    x = static_cast<int>(luaL_checknumber(L, 3));
    y = static_cast<int>(luaL_checknumber(L, 4));
  } else if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
    pTarget = U(L, 3);
  }
  const bool bCan = p != nullptr && (p->dwFlags & 1) && !Hidden(p) &&
                    p->wEnergy >= get_order_tech_energy_cost(static_cast<order_id_t>(nOrder)) &&
                    p->bOrderId != nOrder && (bPlace || pTarget != nullptr || lua_gettop(L) < 3);
  if (!bCan) {
    lua_pushboolean(L, 0);
    return 1;
  }
  if (bPlace)
    p->SetOrder_xy(static_cast<order_id_t>(nOrder), x, y);
  else if (pTarget != nullptr)
    p->SetOrder_CUnit(static_cast<order_id_t>(nOrder), pTarget);
  else
    p->SetOrder_xy(static_cast<order_id_t>(nOrder), p->pSprite->wPosX, p->pSprite->wPosY);
  if (bPlace)
    Trace("cast", p, "order %d at %d, %d", nOrder, x, y);
  else
    Trace("cast", p, "order %d on %d", nOrder, pTarget != nullptr ? IndexOf(pTarget) : IndexOf(p));
  AiChose(nOrder, pTarget != nullptr ? IndexOf(pTarget) : -1, bPlace ? x : -1, bPlace ? y : -1);
  lua_pushboolean(L, 1);
  return 1;
}

int HxOwn(lua_State *L) {
  World(L, "hx.own");
  std::vector<CUnit *> units;
  EachUnit(L, 1, &units);
  int n = 0;
  for (CUnit *p : units)
    if (UnitState *st = Claim(p)) {
      if (st->bOwn == 0)
        Trace("own", p, nullptr);
      st->bOwn = 1;
      ++n;
    }
  lua_pushinteger(L, n);
  return 1;
}

int HxRelease(lua_State *L) {
  World(L, "hx.release");
  std::vector<CUnit *> units;
  EachUnit(L, 1, &units);
  int n = 0;
  for (CUnit *p : units)
    if (UnitState *st = StateOf(p)) {
      if (st->bOwn != 0)
        Trace("release", p, nullptr);
      n += st->bOwn != 0;
      st->bOwn = 0;
    }
  lua_pushinteger(L, n);
  return 1;
}

int HxOwned(lua_State *L) {
  NotTop(L, "hx.owned");
  CUnit *p = U(L, 1);
  const UnitState *st = p ? StateOf(p) : nullptr;
  lua_pushboolean(L, st != nullptr && st->bOwn != 0);
  return 1;
}

int HxWin(lua_State *L) {
  World(L, "hx.win");
  const DWORD dw = PlayersOf(L, 1);
  Trace("win", nullptr, "players 0x%lX", static_cast<unsigned long>(dw));
  for (int s = 0; s < PlayerSlots(); ++s)
    if (dw & (1u << (s & 31)))
      PortTrigVictory(s);
  return 0;
}

int HxLose(lua_State *L) {
  World(L, "hx.lose");
  const DWORD dw = PlayersOf(L, 1);
  Trace("lose", nullptr, "players 0x%lX", static_cast<unsigned long>(dw));
  for (int s = 0; s < PlayerSlots(); ++s)
    if (dw & (1u << (s & 31)))
      PortTrigDefeat(s);
  return 0;
}

// A line's text: the mode's language file when it has the key, else the string as it is, with
// each %s or %d in it taken by the next of the values given.
std::string ModeText(lua_State *L, const std::string &key, int nValues) {
  std::string text = g_modeId.empty() ? key : ModLangModeText(g_modeId, key);
  if (nValues <= 0 || !lua_istable(L, nValues))
    return text;
  lua_rawgetfield(L, nValues, "values");
  const int t = lua_istable(L, -1) ? lua_gettop(L) : nValues;
  const int n = static_cast<int>(lua_objlen(L, t));
  for (int k = 1; k <= n; ++k) {
    const size_t at = text.find('%');
    if (at == std::string::npos || at + 1 >= text.size())
      break;
    lua_rawgeti(L, t, k);
    std::string one;
    if (lua_isnumber(L, -1)) {
      char sz[40];
      snprintf(sz, sizeof sz, "%.10g", lua_tonumber(L, -1));
      one = sz;
    } else if (lua_isstring(L, -1)) {
      one = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    text = text.substr(0, at) + one + text.substr(at + 2);
  }
  lua_pop(L, 1);
  return text;
}

// Presentation: the game, the replay and the other players are the same with it or without.
int HxMessage(lua_State *L) {
  NotTop(L, "hx.message");
  const std::string key = luaL_checkstring(L, 1);
  const std::string text = ModeText(L, key, lua_istable(L, 2) ? 2 : 0);
  // a list of numbers names the players; a table with values is the key's
  DWORD dw = 0xFFFFFFFFu;
  if (lua_istable(L, 2)) {
    lua_rawgeti(L, 2, 1);
    const bool bPlayers = lua_isnumber(L, -1) != 0;
    lua_pop(L, 1);
    if (bPlayers) {
      dw = PlayersOf(L, 2);
    } else {
      lua_rawgetfield(L, 2, "players");
      if (lua_istable(L, -1))
        dw = PlayersOf(L, lua_gettop(L));
      lua_pop(L, 1);
    }
  }
  if (dw & (1u << (g_LocalNationID & 31)))
    PortTrigText(text.c_str(), 0);
  // what the mode tells the player, in the scripts' log and the file, so a mod's author can
  // follow a game that ran by itself
  Note("the mode says: " + text, false);
  Log("modes: frame %u: %s", static_cast<unsigned>(g_gs.dwFrame), text.c_str());
  return 0;
}

int HxHud(lua_State *L) {
  NotTop(L, "hx.hud");
  const std::string key = luaL_checkstring(L, 1);
  std::string value;
  if (lua_isnumber(L, 2)) {
    char sz[40];
    snprintf(sz, sizeof sz, "%.10g", lua_tonumber(L, 2));
    value = sz;
  } else if (lua_isstring(L, 2)) {
    value = g_modeId.empty() ? lua_tostring(L, 2) : ModLangModeText(g_modeId, lua_tostring(L, 2));
  } else if (lua_isboolean(L, 2)) {
    value = lua_toboolean(L, 2) ? "1" : "0";
  }
  for (auto &h : g_hud)
    if (h.first == key) {
      h.second = value;
      return 0;
    }
  if (g_hud.size() < 64)
    g_hud.push_back({key, value});
  return 0;
}

// ---- the parts a mode shows (port_modeview.h) --------------------------------------------------------

// A part's words: the field's text through the mode's language keys, the places in it taken by
// the part's own values.
std::string PartWords(lua_State *L, int t, const char *pszKey) {
  lua_rawgetfield(L, t, pszKey);
  std::string out;
  if (lua_isstring(L, -1))
    out = ModeText(L, lua_tostring(L, -1), t);
  lua_pop(L, 1);
  return out;
}

// A field naming an anchor, a style or a tone; the part's own error when there is no such name.
int PartName(lua_State *L, int t, const char *pszKey, int (*Of)(const char *), int nDefault,
             const char *pszCall) {
  lua_rawgetfield(L, t, pszKey);
  int n = nDefault;
  if (lua_isstring(L, -1)) {
    n = Of(lua_tostring(L, -1));
    if (n < 0)
      luaL_error(L, "%s: there is no %s named \"%s\"", pszCall, pszKey, lua_tostring(L, -1));
  }
  lua_pop(L, 1);
  return n;
}

// A banner's lines: each a key or its text, or {text = key, values = {...}} for a line with
// places of its own. Eight at most.
void PartLines(lua_State *L, int t, std::vector<std::string> *pOut) {
  lua_rawgetfield(L, t, "lines");
  if (lua_istable(L, -1)) {
    const int nAt = lua_gettop(L);
    const int n = static_cast<int>(lua_objlen(L, nAt));
    for (int i = 1; i <= n && i <= 8; ++i) {
      lua_rawgeti(L, nAt, i);
      const int nLine = lua_gettop(L);
      if (lua_isstring(L, nLine))
        pOut->push_back(ModeText(L, lua_tostring(L, nLine), 0));
      else if (lua_istable(L, nLine))
        pOut->push_back(PartWords(L, nLine, "text"));
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
}

// Presentation: a countdown or a timer counting up, over the game where the anchor puts it.
// Setting one of the same name again replaces it, and a timer with the same end goes on
// counting rather than starting over, so a mode may set it every tick.
int HxTimer(lua_State *L) {
  NotTop(L, "hx.timer");
  const std::string id = luaL_checkstring(L, 1);
  if (lua_gettop(L) < 2 || lua_isnil(L, 2)) {
    PortModeViewClear(id.c_str(), MODE_VIEW_TIMER);
    return 0;
  }
  luaL_checktype(L, 2, LUA_TTABLE);
  PortModeViewPart p;
  p.id = id;
  p.nKind = MODE_VIEW_TIMER;
  p.label = PartWords(L, 2, "label");
  p.nAnchor = PartName(L, 2, "anchor", PortModeViewAnchorOf, MODE_VIEW_TOP, "hx.timer");
  p.nStyle = PartName(L, 2, "style", PortModeViewStyleOf, MODE_VIEW_CLOCK, "hx.timer");
  p.nTone = PartName(L, 2, "tone", PortModeViewToneOf, MODE_VIEW_TONE_PLAIN, "hx.timer");
  bool bEnds = false, bSeconds = false;
  const double dEnds = OptNum(L, 2, "ends", 0, &bEnds);
  const double dSeconds = OptNum(L, 2, "seconds", 0, &bSeconds);
  if (OptBool(L, 2, "up", 0))
    p.nFrames = -1;  // counting up from now
  else if (bEnds)
    p.nFrames = std::max(0, static_cast<int>(dEnds) - static_cast<int>(g_gs.dwFrame));
  else if (bSeconds)
    p.nFrames = std::max(0, Frames(dSeconds));
  else
    luaL_error(L, "hx.timer: a timer takes ends (the frame it ends at), seconds, or up = true");
  p.nFull = std::max(0, static_cast<int>(OptNum(L, 2, "full", p.nFrames)));
  p.nShows = std::max(0, Frames(OptNum(L, 2, "shows", 0)));
  PortModeViewSet(p);
  return 0;
}

// A title with lines under it, which goes by itself after its seconds, five by default.
int HxBanner(lua_State *L) {
  NotTop(L, "hx.banner");
  const std::string id = luaL_checkstring(L, 1);
  if (lua_gettop(L) < 2 || lua_isnil(L, 2)) {
    PortModeViewClear(id.c_str(), MODE_VIEW_BANNER);
    return 0;
  }
  luaL_checktype(L, 2, LUA_TTABLE);
  PortModeViewPart p;
  p.id = id;
  p.nKind = MODE_VIEW_BANNER;
  p.label = PartWords(L, 2, "title");
  PartLines(L, 2, &p.lines);
  p.nAnchor = PartName(L, 2, "anchor", PortModeViewAnchorOf, MODE_VIEW_TOP, "hx.banner");
  p.nTone = PartName(L, 2, "tone", PortModeViewToneOf, MODE_VIEW_TONE_PLAIN, "hx.banner");
  p.nStyle = MODE_VIEW_PLAIN;
  p.nShows = std::max(0, Frames(OptNum(L, 2, "seconds", 5)));
  PortModeViewSet(p);
  return 0;
}

// A count, on its own or out of another, as a wave out of the run's. A number in place of the
// table sets the count of one already there, which is what a tick does.
int HxCounter(lua_State *L) {
  NotTop(L, "hx.counter");
  const std::string id = luaL_checkstring(L, 1);
  if (lua_gettop(L) < 2 || lua_isnil(L, 2)) {
    PortModeViewClear(id.c_str(), MODE_VIEW_COUNTER);
    return 0;
  }
  PortModeViewPart p;
  p.id = id;
  p.nKind = MODE_VIEW_COUNTER;
  p.nStyle = MODE_VIEW_PLAIN;
  // a number alone sets the count of a counter already there, so a tick can keep one up to date
  // without saying the rest again; with none of that name it does nothing
  if (lua_isnumber(L, 2)) {
    PortModeViewCounterSet(id.c_str(), lua_tonumber(L, 2));
    return 0;
  }
  luaL_checktype(L, 2, LUA_TTABLE);
  p.label = PartWords(L, 2, "label");
  p.nAnchor = PartName(L, 2, "anchor", PortModeViewAnchorOf, MODE_VIEW_TOP_RIGHT, "hx.counter");
  p.nStyle = PartName(L, 2, "style", PortModeViewStyleOf, MODE_VIEW_PLAIN, "hx.counter");
  p.nTone = PartName(L, 2, "tone", PortModeViewToneOf, MODE_VIEW_TONE_PLAIN, "hx.counter");
  p.dValue = OptNum(L, 2, "value", 0);
  p.dOf = OptNum(L, 2, "of", 0);
  p.nShows = std::max(0, Frames(OptNum(L, 2, "shows", 0)));
  PortModeViewSet(p);
  return 0;
}

// ---- the marks a mod draws on the map (port_mapmark.h) ----------------------------------------------

// A number from a field, held between two bounds.
double MarkNum(lua_State *L, int t, const char *pszKey, double dDefault, double dMin, double dMax) {
  return std::max(dMin, std::min(dMax, OptNum(L, t, pszKey, dDefault)));
}

// Every name in a table a script hands hx.area must be a field this build knows. A name it has
// none of is a mistake in the mod rather than something to pass over quietly: the mark would be
// drawn without it and nothing would say why. t is an index of its own, not one from the top.
void MarkOnly(lua_State *L, int t, const char *pszWhere, const char *const *apszKeys, int nKeys) {
  lua_pushnil(L);
  while (lua_next(L, t)) {
    // a key that is not a name is a list's number, which the fields that take a list read
    if (lua_type(L, -2) == LUA_TSTRING) {
      const char *psz = lua_tostring(L, -2);
      bool bKnown = false;
      for (int i = 0; i < nKeys; ++i)
        bKnown = bKnown || strcmp(apszKeys[i], psz) == 0;
      if (!bKnown)
        luaL_error(L, "%s: there is no field \"%s\"", pszWhere, psz);
    }
    lua_pop(L, 1);
  }
}

// The look the mod's own files give a mark (types.json's "marks", docs/mod-data.md): the blocks
// of light it wears and the mark values it asks for, which the fields of the table win over.
void MarkLook(lua_State *L, int t, PortMapMark *pm) {
  lua_rawgetfield(L, t, "look");
  if (lua_isstring(L, -1)) {
    const char *psz = lua_tostring(L, -1);
    const PortMarkLook *pLook = ModMarkLook(psz);
    if (pLook == NULL)
      luaL_error(L, "hx.area: there is no look named \"%s\": the mod's types.json names %s", psz,
                 ModMarkLookNames()[0] == '\0' ? "none of its own" : ModMarkLookNames());
    pm->look = *pLook;
    const PortMarkAsks &asks = pLook->asks;
    pm->fFill = asks.fFill;
    pm->fEdge = asks.fEdge;
    pm->fWidth = asks.fWidth;
    pm->fDash = asks.fDash;
    pm->fSoft = asks.fSoft;
    pm->fPulse = asks.fPulse;
    pm->fDepth = asks.fDepth;
    pm->nCurve = asks.nCurve;
    pm->nCross = asks.nCross;
  }
  lua_pop(L, 1);
}

// A mark's colour: a player's number for that player's own colour, or "#rrggbb". Either
// spelling of the field is taken, as a mod's files are written in both.
void MarkColour(lua_State *L, int t, PortMapMark *pm) {
  lua_rawgetfield(L, t, "color");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_rawgetfield(L, t, "colour");
  }
  if (lua_isnumber(L, -1)) {
    // a player of this game, checked as every other player a script names is
    pm->nPlayer = PlayerOf(L, "hx.area's colour", lua_tonumber(L, -1));
  } else if (lua_isstring(L, -1)) {
    unsigned int nRgb = 0;
    if (!PortMapMarkRgbOf(lua_tostring(L, -1), &nRgb))
      luaL_error(L, "hx.area: there is no colour \"%s\": a player's number, or \"#rrggbb\"",
                 lua_tostring(L, -1));
    pm->nPlayer = -1;
    pm->nRgb = nRgb;
  }
  lua_pop(L, 1);
}

// A polygon's points: x and y in turn, 64 points at most.
void MarkPoints(lua_State *L, int t, std::vector<double> *pOut) {
  lua_rawgetfield(L, t, "points");
  if (lua_istable(L, -1)) {
    const int nAt = lua_gettop(L);
    const int n = static_cast<int>(lua_objlen(L, nAt));
    for (int i = 1; i <= n && i <= 128; ++i) {
      lua_rawgeti(L, nAt, i);
      if (lua_isnumber(L, -1))
        pOut->push_back(lua_tonumber(L, -1));
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  if (pOut->size() % 2 != 0)
    pOut->pop_back();
}

// The fade: true for one with every default, or a table saying whose units reveal the mark, how
// near they begin to, how far the opacity rises and whether only the part near a unit shows.
void MarkFade(lua_State *L, int t, PortMapMark *pm) {
  lua_rawgetfield(L, t, "fade");
  if (lua_isboolean(L, -1)) {
    pm->bFade = lua_toboolean(L, -1) != 0;
  } else if (lua_istable(L, -1)) {
    const int f = lua_gettop(L);
    static const char *const kapsz[] = {"near", "most", "least", "glow", "curve", "players"};
    MarkOnly(L, f, "hx.area's fade", kapsz, 6);
    pm->bFade = true;
    pm->fNear = MarkNum(L, f, "near", PORT_MAP_MARK_NEAR, 0, 8192);
    pm->fMost = MarkNum(L, f, "most", 1, 0, 1);
    pm->fLeast = MarkNum(L, f, "least", 0, 0, 1);
    pm->bGlow = OptBool(L, f, "glow", 1) != 0;
    lua_rawgetfield(L, f, "curve");
    if (lua_isstring(L, -1)) {
      pm->nCurve = PortMapMarkCurveOf(lua_tostring(L, -1));
      if (pm->nCurve < 0)
        luaL_error(L, "hx.area: there is no curve named \"%s\": \"smooth\", \"even\" or \"sharp\"",
                   lua_tostring(L, -1));
    }
    lua_pop(L, 1);
    lua_rawgetfield(L, f, "players");
    if (lua_isstring(L, -1)) {
      const int nWho = PortMapMarkWhoOf(lua_tostring(L, -1));
      if (nWho < 0 || nWho == MAP_MARK_WHO_LIST)
        luaL_error(L, "hx.area: there are no players named \"%s\": \"me\", \"allies\", \"all\", or a list of "
                      "player numbers", lua_tostring(L, -1));
      pm->nWho = nWho;
    } else if (lua_istable(L, -1)) {
      const int q = lua_gettop(L);
      pm->nWho = MAP_MARK_WHO_LIST;
      const int n = static_cast<int>(lua_objlen(L, q));
      for (int i = 1; i <= n && i <= 32; ++i) {
        lua_rawgeti(L, q, i);
        if (lua_isnumber(L, -1))
          pm->who.push_back(PlayerOf(L, "hx.area's fade players", lua_tonumber(L, -1)));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

// Presentation: an area marked on the map, which sits on the ground and moves and scales with
// the camera. Setting a mark of the same name again replaces it, which is how one is moved or
// updated, and nothing in place of the table clears it.
int HxArea(lua_State *L) {
  NotTop(L, "hx.area");
  const std::string id = luaL_checkstring(L, 1);
  if (lua_gettop(L) < 2 || lua_isnil(L, 2)) {
    PortMapMarkClear(id.c_str());
    return 0;
  }
  luaL_checktype(L, 2, LUA_TTABLE);
  static const char *const kapszFields[] = {
      "shape", "layer", "x", "y", "w", "h", "radius", "x2", "y2", "points", "color", "colour",
      "players", "fill", "edge", "edge_width", "dash", "soft", "pulse", "pulse_depth", "outside",
      "look", "crossing", "fade", "minimap"};
  MarkOnly(L, 2, "hx.area", kapszFields, static_cast<int>(sizeof kapszFields / sizeof *kapszFields));
  PortMapMark m;
  m.id = id;
  // the look the mod's files give it first, so that every field below may overrule what it asks
  MarkLook(L, 2, &m);
  lua_rawgetfield(L, 2, "shape");
  if (lua_isstring(L, -1)) {
    m.nShape = PortMapMarkShapeOf(lua_tostring(L, -1));
    if (m.nShape < 0)
      luaL_error(L, "hx.area: there is no shape named \"%s\": \"circle\", \"ring\", \"rect\", \"polygon\" or "
                    "\"line\"", lua_tostring(L, -1));
  }
  lua_pop(L, 1);
  lua_rawgetfield(L, 2, "layer");
  if (lua_isstring(L, -1)) {
    m.nLayer = PortMapMarkLayerOf(lua_tostring(L, -1));
    if (m.nLayer < 0)
      luaL_error(L, "hx.area: there is no layer named \"%s\": \"ground\" or \"over\"", lua_tostring(L, -1));
  }
  lua_pop(L, 1);
  m.x = OptNum(L, 2, "x", 0);
  m.y = OptNum(L, 2, "y", 0);
  m.w = MarkNum(L, 2, "w", 0, 0, 1 << 20);
  m.h = MarkNum(L, 2, "h", 0, 0, 1 << 20);
  m.r = MarkNum(L, 2, "radius", 0, 0, 1 << 20);
  m.x2 = OptNum(L, 2, "x2", 0);
  m.y2 = OptNum(L, 2, "y2", 0);
  if (m.nShape == MAP_MARK_POLYGON)
    MarkPoints(L, 2, &m.points);
  MarkColour(L, 2, &m);
  // who sees it: a player, a list of them, or every player when it says nothing. A mode that
  // gives each player their own ground draws each player their own mark.
  lua_rawgetfield(L, 2, "players");
  if (lua_isnumber(L, -1))
    m.nPlayers = 1u << (PlayerOf(L, "hx.area's players", lua_tonumber(L, -1)) & 31);
  else if (lua_isstring(L, -1))
    luaL_error(L, "hx.area: players is a player's number or a list of them, not \"%s\" (\"me\", "
                  "\"allies\" and \"all\" are the fade's players, whose units light the mark)",
               lua_tostring(L, -1));
  else if (lua_istable(L, -1))
    m.nPlayers = PlayersOf(L, lua_gettop(L));
  lua_pop(L, 1);
  // each of these takes what the look asked for where the table does not say
  m.fFill = MarkNum(L, 2, "fill", m.fFill, 0, 1);
  m.fEdge = MarkNum(L, 2, "edge", m.fEdge, 0, 1);
  m.fWidth = MarkNum(L, 2, "edge_width", m.fWidth, 0, 1024);
  m.fDash = MarkNum(L, 2, "dash", m.fDash, 0, 4096);
  m.fSoft = MarkNum(L, 2, "soft", m.fSoft, 0, 4);
  m.fPulse = MarkNum(L, 2, "pulse", m.fPulse, 0, 60);
  m.fDepth = MarkNum(L, 2, "pulse_depth", m.fDepth, 0, 1);
  // the ground outside: {style, band}, or a style on its own. The look's own outside block says
  // how that light is drawn; these two say whether it is drawn at all and how far the band goes.
  lua_rawgetfield(L, 2, "outside");
  if (lua_isstring(L, -1) || lua_istable(L, -1)) {
    const int o = lua_gettop(L);
    const char *psz = NULL;
    if (lua_isstring(L, -1)) {
      psz = lua_tostring(L, -1);
    } else {
      static const char *const kapszOut[] = {"style", "band"};
      MarkOnly(L, o, "hx.area's outside", kapszOut, 2);
      lua_rawgetfield(L, o, "style");
      psz = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
      lua_pop(L, 1);
      m.look.outside.fBand = MarkNum(L, o, "band", m.look.outside.fBand, 0, 1 << 20);
    }
    if (psz != NULL) {
      m.look.outside.nStyle = PortMapMarkOutsideOf(psz);
      if (m.look.outside.nStyle < 0)
        luaL_error(L, "hx.area: nothing is drawn outside in the way \"%s\": \"tint\", \"ring\", "
                      "\"tether\" or \"none\"", psz);
    }
  }
  lua_pop(L, 1);
  // the minimap: true for the block as the look gives it, false for none, or a table of its own.
  // The picture has no alpha and redraws on its own slow timer, so these are its own solid
  // widths in minimap pixels and nothing of the fade or the pulse reaches it.
  lua_rawgetfield(L, 2, "minimap");
  if (lua_isboolean(L, -1)) {
    m.look.mini.bOn = lua_toboolean(L, -1) != 0;
  } else if (lua_istable(L, -1)) {
    const int o = lua_gettop(L);
    static const char *const kapszMini[] = {"on", "fill", "edge", "width", "dash", "color", "colour"};
    MarkOnly(L, o, "hx.area's minimap", kapszMini, 7);
    PortMarkMini &mini = m.look.mini;
    mini.bOn = true;
    lua_rawgetfield(L, o, "on");
    if (lua_isboolean(L, -1))
      mini.bOn = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    mini.fFill = MarkNum(L, o, "fill", mini.fFill, 0, 1);
    mini.fEdge = MarkNum(L, o, "edge", mini.fEdge, 0, 1);
    mini.fWidth = MarkNum(L, o, "width", mini.fWidth, 0, 64);
    mini.fDash = MarkNum(L, o, "dash", mini.fDash, 0, 4096);
    lua_rawgetfield(L, o, "color");
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_rawgetfield(L, o, "colour");
    }
    if (lua_isnumber(L, -1)) {
      mini.nPlayer = PlayerOf(L, "hx.area's minimap colour", lua_tonumber(L, -1));
      mini.bRgb = false;
    } else if (lua_isstring(L, -1)) {
      unsigned int nRgb = 0;
      if (!PortMapMarkRgbOf(lua_tostring(L, -1), &nRgb))
        luaL_error(L, "hx.area's minimap colour is a player's number or \"#rrggbb\", not \"%s\"",
                   lua_tostring(L, -1));
      mini.nRgb = nRgb;
      mini.nPlayer = -1;
      mini.bRgb = true;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  lua_rawgetfield(L, 2, "crossing");
  if (lua_isstring(L, -1)) {
    m.nCross = PortMapMarkCrossingOf(lua_tostring(L, -1));
    if (m.nCross < 0)
      luaL_error(L, "hx.area: nothing answers a crossing in the way \"%s\": \"ripple\", \"flash\", "
                    "\"stamp\", \"arc\" or \"none\"", lua_tostring(L, -1));
  }
  lua_pop(L, 1);
  MarkFade(L, 2, &m);
  PortMapMarkSet(m);
  return 0;
}

int HxPing(lua_State *L) {
  NotTop(L, "hx.ping");
  const int x = static_cast<int>(luaL_checknumber(L, 1)), y = static_cast<int>(luaL_checknumber(L, 2));
  do_minimap_ping(static_cast<ULONG>(x), static_cast<ULONG>(y), g_LocalNationID);
  return 0;
}


// The minimap's own picture, for a mark drawn on it: how many pixels it is across and down, and
// how many map pixels one of them covers, which is what a mod needs to write a length in
// minimap pixels (hx.area's minimap block). The minimap settles these as it sizes itself and no
// other file can read them, so they are had from it rather than worked out here. They follow
// the map, so they are the same on every seat, and false is the answer before a game's minimap
// is made. Presentation: nothing of a game is decided by them.
int HxMinimap(lua_State *L) {
  NotTop(L, "hx.minimap");
  int nWidth = 0, nHeight = 0, nScale = 0;
  if (!PortMinimapSize(&nWidth, &nHeight, &nScale)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_createtable(L, 0, 3);
  lua_pushnumber(L, nWidth);
  lua_setfield(L, -2, "width");
  lua_pushnumber(L, nHeight);
  lua_setfield(L, -2, "height");
  lua_pushnumber(L, nScale);
  lua_setfield(L, -2, "scale");
  return 1;
}

int HxReveal(lua_State *L) {
  World(L, "hx.reveal");
  const int x = static_cast<int>(luaL_checknumber(L, 1)), y = static_cast<int>(luaL_checknumber(L, 2));
  const int nRadius = static_cast<int>(std::max(32.0, std::min(4096.0, luaL_checknumber(L, 3))));
  const int nFrames = Frames(std::max(0.0, std::min(2700.0, luaL_optnumber(L, 4, 10))));
  const DWORD dw = PlayersOf(L, 5);
  if (nFrames <= 0 || g_gs.nReveals >= kRevealsMax) {
    lua_pushboolean(L, 0);
    return 1;
  }
  Reveal &r = g_reveals[g_gs.nReveals++];
  r.x = x;
  r.y = y;
  r.wTiles = static_cast<WORD>(std::max(1, nRadius / 32));
  r.wLeft = static_cast<WORD>(std::min(nFrames, 0xFFFF));
  r.dwPlayers = dw;
  lua_pushboolean(L, 1);
  return 1;
}

// ---- values ------------------------------------------------------------------------------------------

int KeyOf(lua_State *L, int i, bool bMake) {
  size_t n = 0;
  const char *psz = luaL_checklstring(L, i, &n);
  if (n == 0 || n >= static_cast<size_t>(kKeyLen))
    luaL_error(L, "a value's name has 1 to 31 letters");
  for (int k = 0; k < g_gs.nKeys; ++k)
    if (strcmp(g_gs.keys[k], psz) == 0)
      return k + 1;
  if (!bMake)
    return 0;
  if (g_gs.nKeys >= kKeys)
    luaL_error(L, "the game keeps values under 256 names at most");
  memcpy(g_gs.keys[g_gs.nKeys], psz, n + 1);
  return ++g_gs.nKeys;
}

void PushValue(lua_State *L, const Value *v) {
  if (v == nullptr || v->bKind == 0)
    lua_pushnil(L);
  else if (v->bKind == 2)
    lua_pushboolean(L, v->d != 0);
  else
    lua_pushnumber(L, v->d);
}

void SetValue(lua_State *L, Value *pValues, int nValues, int nKey, int i, const char *pszWhat) {
  const int t = lua_type(L, i);
  if (t != LUA_TNIL && t != LUA_TNUMBER && t != LUA_TBOOLEAN)
    luaL_error(L, "%s keeps numbers and true or false; a unit is kept as its number", pszWhat);
  Value *pAt = nullptr;
  for (int k = 0; k < nValues; ++k)
    if (pValues[k].wKey == nKey)
      pAt = &pValues[k];
  if (t == LUA_TNIL) {
    if (pAt)
      memset(pAt, 0, sizeof *pAt);
    return;
  }
  for (int k = 0; k < nValues && pAt == nullptr; ++k)
    if (pValues[k].wKey == 0)
      pAt = &pValues[k];
  if (pAt == nullptr)
    luaL_error(L, "%s keeps %d values at most; set one to nil to free it", pszWhat, nValues);
  pAt->wKey = static_cast<WORD>(nKey);
  pAt->bKind = t == LUA_TBOOLEAN ? 2 : 1;
  pAt->d = t == LUA_TBOOLEAN ? (lua_toboolean(L, i) ? 1.0 : 0.0) : lua_tonumber(L, i);
}

int HxGet(lua_State *L) {
  NotTop(L, "hx.get");
  CUnit *p = U(L, 1);
  const int nKey = KeyOf(L, 2, false);
  UnitState *st = p ? StateOf(p) : nullptr;
  const Value *pv = nullptr;
  for (int k = 0; st && nKey && k < kUnitValues; ++k)
    if (st->v[k].wKey == nKey)
      pv = &st->v[k];
  // a unit that died this frame still answers, for its own death hooks
  if (p == nullptr && nKey)
    pv = DyingValue(static_cast<DWORD>(lua_tonumber(L, 1)), nKey);
  PushValue(L, pv);
  return 1;
}

int HxSet(lua_State *L) {
  NotTop(L, "hx.set");
  CUnit *p = U(L, 1);
  if (p == nullptr)
    return 0;
  const int nKey = KeyOf(L, 2, !lua_isnil(L, 3));
  if (nKey == 0)
    return 0;
  UnitState *st = Claim(p);
  if (st)
    SetValue(L, st->v, kUnitValues, nKey, 3, "a unit");
  return 0;
}

int HxGameGet(lua_State *L) {
  NotTop(L, "hx.game_get");
  const int nKey = KeyOf(L, 1, false);
  const Value *pv = nullptr;
  for (int k = 0; nKey && k < kGameValues; ++k)
    if (g_gs.v[k].wKey == nKey)
      pv = &g_gs.v[k];
  PushValue(L, pv);
  return 1;
}

int HxGameSet(lua_State *L) {
  NotTop(L, "hx.game_set");
  const int nKey = KeyOf(L, 1, !lua_isnil(L, 2));
  if (nKey != 0)
    SetValue(L, g_gs.v, kGameValues, nKey, 2, "the game");
  return 0;
}

// ---- the game's random, time and numbers -------------------------------------------------------------

// A whole number in [0, n): one of the game's draws, two for a range past 32768.
long long Draw(long long n) {
  if (n <= 1)
    return 0;
  if (n <= 32768)
    return (static_cast<long long>(net_rand(kRandTag)) * n) >> 15;
  n = std::min(n, 1LL << 30);
  const long long r = (static_cast<long long>(net_rand(kRandTag)) << 15) | net_rand(kRandTag);
  return (r * n) >> 30;
}

// The three forms Lua's own math.random has, over the game's draws: a part of 1, a whole number
// from 1 to n, and one from a to b. math.random is this function, so a script written the way
// Lua is written gets the game's random and not the host's.
int HxRandom(lua_State *L) {
  World(L, "hx.random");
  if (lua_gettop(L) < 1) {
    const double k = static_cast<double>(1LL << 30);
    lua_pushnumber(L, static_cast<double>(Draw(1LL << 30)) / k);
    return 1;
  }
  const double a = luaL_checknumber(L, 1);
  if (lua_gettop(L) >= 2) {
    const double b = luaL_checknumber(L, 2);
    const long long lo = static_cast<long long>(floor(a)), hi = static_cast<long long>(floor(b));
    if (hi < lo)
      luaL_error(L, "hx.random(a, b) wants a <= b");
    lua_pushnumber(L, static_cast<double>(lo + Draw(hi - lo + 1)));
  } else {
    const long long n = static_cast<long long>(floor(a));
    if (n < 1)
      luaL_error(L, "hx.random(n) wants n >= 1");
    lua_pushnumber(L, static_cast<double>(1 + Draw(n)));
  }
  return 1;
}

int HxFrame(lua_State *L) {
  NotTop(L, "hx.frame");
  lua_pushnumber(L, g_gs.dwFrame);
  return 1;
}

int HxSeconds(lua_State *L) {
  lua_pushnumber(L, Frames(luaL_checknumber(L, 1)));
  return 1;
}

int HxDistance(lua_State *L) {
  long long dx, dy;
  if (lua_gettop(L) >= 4) {
    dx = static_cast<long long>(luaL_checknumber(L, 3)) - static_cast<long long>(luaL_checknumber(L, 1));
    dy = static_cast<long long>(luaL_checknumber(L, 4)) - static_cast<long long>(luaL_checknumber(L, 2));
  } else {
    NotTop(L, "hx.distance");
    CUnit *a = U(L, 1);
    CUnit *b = U(L, 2);
    if (a == nullptr || b == nullptr)
      return 0;
    dx = static_cast<int>(b->pSprite->wPosX) - static_cast<int>(a->pSprite->wPosX);
    dy = static_cast<int>(b->pSprite->wPosY) - static_cast<int>(a->pSprite->wPosY);
  }
  lua_pushnumber(L, static_cast<double>(ISqrt(dx * dx + dy * dy)));
  return 1;
}

int HxIsqrt(lua_State *L) {
  lua_pushnumber(L, static_cast<double>(ISqrt(static_cast<long long>(luaL_checknumber(L, 1)))));
  return 1;
}

int HxPow(lua_State *L) {
  double x = luaL_checknumber(L, 1);
  const double dn = luaL_checknumber(L, 2);
  long long n = static_cast<long long>(dn);
  if (static_cast<double>(n) != dn || n < -1024 || n > 1024)
    luaL_error(L, "hx.pow(x, n) takes a whole n from -1024 to 1024");
  const bool bInverse = n < 0;
  n = bInverse ? -n : n;
  double r = 1;
  while (n > 0) {
    if (n & 1)
      r *= x;
    x *= x;
    n >>= 1;
  }
  lua_pushnumber(L, bInverse ? 1 / r : r);
  return 1;
}

int HxId(lua_State *L) {
  const char *pszTable = luaL_checkstring(L, 1);
  // a table of its own, so a misspelt one says so instead of giving nil for every name
  if (!TableKnown(pszTable))
    luaL_error(L, "hx.id: there is no table \"%s\": units, weapons, flingy, sprites, images, "
                  "orders, techs, upgrades, sounds or portraits",
               pszTable);
  const int n = ModTypeByName(pszTable, luaL_checkstring(L, 2));
  if (n < 0)
    return 0;
  lua_pushinteger(L, n);
  return 1;
}

std::string LineOf(lua_State *L, int nFrom) {
  std::string s;
  for (int i = nFrom; i <= lua_gettop(L); ++i) {
    size_t n = 0;
    const char *psz = nullptr;
    const int t = lua_type(L, i);
    if (t == LUA_TSTRING || t == LUA_TNUMBER)
      psz = lua_tolstring(L, i, &n);
    s += (i > nFrom ? " " : "") + (psz ? std::string(psz, n) : t == LUA_TBOOLEAN ? (lua_toboolean(L, i) ? "true" : "false")
                                                                               : std::string(lua_typename(L, t)));
  }
  return s;
}

int HxLog(lua_State *L) {
  Note(LineOf(L, 1), false);
  return 0;
}

// tostring without the addresses Luau gives tables and functions, which differ between machines
int OwnTostring(lua_State *L) {
  luaL_checkany(L, 1);
  const int t = lua_type(L, 1);
  if (t == LUA_TSTRING || t == LUA_TNUMBER || t == LUA_TBOOLEAN || t == LUA_TNIL) {
    luaL_tolstring(L, 1, nullptr);
    return 1;
  }
  lua_pushstring(L, lua_typename(L, t));
  return 1;
}

// ---- toggles, charges, channels, autocast from a script ---------------------------------------------

int HxToggled(lua_State *L) {
  NotTop(L, "hx.toggled");
  CUnit *p = U(L, 1);
  const int nOrder = OrderArg(L, 2);
  OrderSlot *o = p ? SlotOf(p, nOrder, false) : nullptr;
  lua_pushboolean(L, o != nullptr && (o->bFlags & kOn));
  return 1;
}

void ToggleSet(CUnit *pUnit, int nOrder, bool bOn, int nReason);

int HxToggle(lua_State *L) {
  World(L, "hx.toggle");
  CUnit *p = U(L, 1);
  const int nOrder = OrderArg(L, 2);
  const OrderSpec *sp = SpecOf(nOrder);
  if (sp == nullptr || !sp->bToggle)
    luaL_error(L, "hx.toggle: order %d is no toggle", nOrder);
  if (p != nullptr)
    ToggleSet(p, nOrder, lua_toboolean(L, 3) != 0, R_SCRIPT);
  return 0;
}

int HxCharges(lua_State *L) {
  NotTop(L, "hx.charges");
  CUnit *p = U(L, 1);
  const int nOrder = OrderArg(L, 2);
  const OrderSpec *sp = SpecOf(nOrder);
  if (p == nullptr || sp == nullptr || sp->nCharges == 0)
    return 0;
  OrderSlot *o = SlotOf(p, nOrder, false);
  lua_pushinteger(L, o ? o->bCharges : sp->nCharges);
  lua_pushinteger(L, o && o->bCharges < sp->nCharges ? o->wRecharge : 0);
  return 2;
}

int HxAutocast(lua_State *L) {
  NotTop(L, "hx.autocast");
  CUnit *p = U(L, 1);
  const int nOrder = OrderArg(L, 2);
  if (p == nullptr)
    return 0;
  if (lua_gettop(L) >= 3) {
    World(L, "hx.autocast");
    if (OrderSlot *o = SlotOf(p, nOrder, true)) {
      if (lua_toboolean(L, 3))
        o->bFlags &= ~kAutoOff;
      else
        o->bFlags |= kAutoOff;
    }
    return 0;
  }
  OrderSlot *o = SlotOf(p, nOrder, false);
  lua_pushboolean(L, o == nullptr || !(o->bFlags & kAutoOff));
  return 1;
}

void ChannelEnd(int i, int nReason);

int HxChannelStop(lua_State *L) {
  World(L, "hx.channel_stop");
  CUnit *p = U(L, 1);
  const DWORD id = IdOf(p);
  for (int i = 0; p && i < g_gs.nChannels; ++i)
    if (g_channels[i].dwCaster == id) {
      ChannelEnd(i, R_SCRIPT);
      --i;
    }
  return 0;
}

// ---- the names, with what the studio shows of them ---------------------------------------------------

struct ApiFn {
  const char *pszName;  // under hx
  lua_CFunction pfn;
  const char *pszArgs;
  const char *pszHelp;
};
const ApiFn kApi[] = {
    {"units", HxUnits, "{x, y, radius, type, owner, enemies_of, allies_of, buildings, max}",
     "the units that live, in the area given or on the whole map, as a list of unit numbers"},
    {"alive", HxAlive, "(u)", "whether the unit still lives"},
    {"type", HxType, "(u)", "the unit's type, a number (compare with hx.is or hx.id)"},
    {"is", HxIs, "(u, type)", "whether the unit is of a type, named as types.json names it"},
    {"owner", HxOwner, "(u)", "the player who owns it, 0 to 11"},
    {"position", HxPosition, "(u)", "its place, x and y in pixels"},
    {"is_building", HxIsBuilding, "(u)", "whether it is a building"},
    {"can_target", HxCanTarget, "(a, b)", "whether a's weapons can hit b at all"},
    {"can_reach", HxCanReach, "(a, b)", "whether a can walk to b, true for anything that flies"},
    {"enemies", HxEnemies, "(a, b)", "whether b is an enemy of a's owner"},
    {"life", HxLife, "(u)", "its life in points (a step is 1/256)"},
    {"max_life", HxMaxLife, "(u)", "its most life"},
    {"set_life", HxSetLife, "(u, n)", "its life set, up to its most; 0 kills it"},
    {"shields", HxShields, "(u)", "its shields in points"},
    {"max_shields", HxMaxShields, "(u)", "its most shields"},
    {"set_shields", HxSetShields, "(u, n)", "its shields set, up to their most"},
    {"energy", HxEnergy, "(u)", "its energy"},
    {"max_energy", HxMaxEnergy, "(u)", "its most energy, 0 for a unit that has none"},
    {"set_energy", HxSetEnergy, "(u, n)", "its energy set, up to its most"},
    {"damage", HxDamage, "(u, amount, {type, source})",
     "damage dealt as a weapon deals it (shields, armour, size), its on_damaged called; what it took"},
    {"heal", HxHeal, "(u, amount)", "life given, up to its most; what it gained"},
    {"kill", HxKill, "(u)", "the unit killed"},
    {"move", HxMove, "(u, x, y)", "the unit put at the free place nearest x, y; false when there is none"},
    {"order", HxOrder, "(u, order, target | x, y)",
     "an order given: move, attack, patrol, stop, hold, or any order by name, at a unit or a place"},
    {"make", HxMake, "(type, owner, x, y, count?, seconds?)", "units made as Create Unit makes them; their numbers"},
    {"status", HxStatus, "(u, status, player?)", "one of the mod's statuses put on the unit"},
    {"unstatus", HxUnstatus, "(u, status?)", "a status taken off, or every one"},
    {"has_status", HxHasStatus, "(u, status)", "the stacks of a status it has, 0 for none"},
    {"statuses", HxStatuses, "(u)",
     "the statuses on it, each {id, name, left, duration, stacks, icon, good}, the longest-held first"},
    {"sound", HxSound, "(sound, x, y)", "a sound played at a place"},
    {"show", HxShow, "(sprite, x, y, elevation?)", "a sprite shown at a place, as a block's show"},
    {"overlay", HxOverlay, "(u, image)", "an image played over the unit"},
    {"area", HxArea,
     "(id, {shape, x, y, radius | w, h | x2, y2 | points, color, players, fill, edge, edge_width, dash, soft, pulse, "
     "fade} | nil)",
     "an area marked on the map, which a unit coming near can reveal; set again to move or update "
     "it and nil to clear it; presentation"},
    {"get", HxGet, "(u, name)", "a value kept on the unit, nil for none"},
    {"set", HxSet, "(u, name, value)", "a number or true or false kept on the unit (8 a unit); nil takes it away"},
    {"game_get", HxGameGet, "(name)", "a value the game keeps"},
    {"game_set", HxGameSet, "(name, value)", "a number or true or false the game keeps (64 in all)"},
    {"random", HxRandom, "() | (n) | (a, b)",
     "the game's random, as math.random is written: a part of 1, a whole number from 1 to n, or one from a to b"},
    {"frame", HxFrame, "()", "the game's frame, 24 a second at Fastest"},
    {"seconds", HxSeconds, "(s)", "seconds at Fastest as frames"},
    {"distance", HxDistance, "(a, b) | (x1, y1, x2, y2)", "the distance in whole pixels"},
    {"isqrt", HxIsqrt, "(n)", "the whole square root"},
    {"pow", HxPow, "(x, n)", "x to a whole power n, by products (the ^ operator is not in scripts)"},
    {"id", HxId, "(table, name)", "a type's number by its name: hx.id(\"units\", \"terran_marine\")"},
    {"toggled", HxToggled, "(u, order)", "whether a toggle order is on for the unit"},
    {"toggle", HxToggle, "(u, order, on)", "a toggle turned on or off, its on or off called"},
    {"charges", HxCharges, "(u, order)", "an order's charges the unit has, and frames to its next"},
    {"autocast", HxAutocast, "(u, order, on?)", "whether the unit casts an order by itself; with on, set"},
    {"channel_stop", HxChannelStop, "(u)", "the unit's channel stopped, its stop called"},
    {"log", HxLog, "(...)", "a line in the scripts' log (print is the same)"},
    // a mode's own (port_mode.h)
    {"setting", HxSetting, "(id)", "the mode setting's value for this game; nil for an id it does not declare"},
    {"cast", HxCast, "(u, order, target | x, y)",
     "the unit casts the order as its button would, paying its tech's energy; false when it cannot"},
    {"own", HxOwn, "(units)", "the mode takes the units off their owner's computer, which then leaves them alone"},
    {"release", HxRelease, "(units)", "and gives them back to it"},
    {"owned", HxOwned, "(u)", "whether the mode owns the unit"},
    {"win", HxWin, "(players)", "those players win, as a trigger's Victory does"},
    {"lose", HxLose, "(players)", "those players lose, as Defeat does"},
    {"message", HxMessage, "(text | key, players | {values, players})",
     "a line on the game's message line; presentation, the same game with it or without"},
    {"hud", HxHud, "(key, value)", "a value the HUD format draws as mode.<key>; presentation"},
    {"timer", HxTimer, "(id, {label, ends | seconds | up, style, anchor, tone} | nil)",
     "a countdown over the game, set again to update and nil to clear; presentation"},
    {"banner", HxBanner, "(id, {title, lines, seconds, anchor, tone} | nil)",
     "a title with lines over the game, which goes by itself; presentation"},
    {"counter", HxCounter, "(id, {label, value, of, style, anchor, tone} | n | nil)",
     "a count over the game, as the fifth wave of 25; a number alone sets the count; presentation"},
    {"ping", HxPing, "(x, y)", "a ping on the minimap, as an ally's is"},
    {"reveal", HxReveal, "(x, y, radius, seconds?, players?)", "the ground round the place shown for that long"},
    {"minimap", HxMinimap, "()",
     "the minimap's picture: {width, height, scale}, the map pixels one of its pixels covers; false before there is "
     "one"},
};

const ModLuaName kApiNames[] = {
    {"hx.units", "{x, y, radius, type, owner, enemies_of, allies_of, buildings, max}", "the units that live, in an area or on the map"},
    {"hx.alive", "(u)", "whether the unit still lives"},
    {"hx.type", "(u)", "the unit's type, a number"},
    {"hx.is", "(u, type)", "whether the unit is of a type, by its name"},
    {"hx.owner", "(u)", "the player who owns it, 0 to 11"},
    {"hx.position", "(u)", "its place, x and y in pixels"},
    {"hx.is_building", "(u)", "whether it is a building"},
    {"hx.can_target", "(a, b)", "whether a's weapons can hit b at all"},
    {"hx.can_reach", "(a, b)", "whether a can walk to b"},
    {"hx.enemies", "(a, b)", "whether b is an enemy of a's owner"},
    {"hx.life", "(u)", "its life in points (a step is 1/256)"},
    {"hx.max_life", "(u)", "its most life"},
    {"hx.set_life", "(u, n)", "its life set, up to its most; 0 kills it"},
    {"hx.shields", "(u)", "its shields in points"},
    {"hx.max_shields", "(u)", "its most shields"},
    {"hx.set_shields", "(u, n)", "its shields set, up to their most"},
    {"hx.energy", "(u)", "its energy"},
    {"hx.max_energy", "(u)", "its most energy"},
    {"hx.set_energy", "(u, n)", "its energy set, up to its most"},
    {"hx.damage", "(u, amount, {type, source})",
     "damage dealt as a weapon deals it, its type normal (the default), explosive, concussive, ignore_armour or independent (half a point, as classic's); what it took"},
    {"hx.heal", "(u, amount)", "life given, up to its most"},
    {"hx.kill", "(u)", "the unit killed"},
    {"hx.move", "(u, x, y)", "the unit put at the free place nearest x, y"},
    {"hx.order", "(u, order, target | x, y)", "an order given: move, attack, patrol, stop, hold or any by name"},
    {"hx.make", "(type, owner, x, y, count?, seconds?)", "units made as Create Unit makes them"},
    {"hx.status", "(u, status, player?)", "one of the mod's statuses put on the unit"},
    {"hx.unstatus", "(u, status?)", "a status taken off, or every one"},
    {"hx.has_status", "(u, status)", "the stacks of a status it has"},
    {"hx.statuses", "(u)", "the statuses on it, each with its name, time, stacks, icon and side"},
    {"hx.readout.fx", "(u, op, args)", "an effect on the unit readout, in readout(unit) alone"},
    {"hx.sound", "(sound, x, y)", "a sound played at a place"},
    {"hx.show", "(sprite, x, y, elevation?)", "a sprite shown at a place"},
    {"hx.overlay", "(u, image)", "an image played over the unit"},
    {"hx.area",
     "(id, {shape, x, y, radius | w, h | x2, y2 | points, color, players, fill, edge, edge_width, dash, soft, pulse, "
     "fade} | nil)",
     "an area marked on the map (a circle, ring, rect, polygon or line), which a unit coming near "
     "can reveal; nil clears it"},
    {"hx.get", "(u, name)", "a value kept on the unit"},
    {"hx.set", "(u, name, value)", "a number or true or false kept on the unit"},
    {"hx.game_get", "(name)", "a value the game keeps"},
    {"hx.game_set", "(name, value)", "a number or true or false the game keeps"},
    {"hx.random", "() | (n) | (a, b)", "the game's random: a part of 1, 1 to n, or a to b"},
    {"hx.frame", "()", "the game's frame"},
    {"hx.seconds", "(s)", "seconds as frames"},
    {"hx.distance", "(a, b) | (x1, y1, x2, y2)", "the distance in whole pixels"},
    {"hx.isqrt", "(n)", "the whole square root"},
    {"hx.pow", "(x, n)", "x to a whole power"},
    {"hx.id", "(table, name)", "a type's number by its name"},
    {"hx.toggled", "(u, order)", "whether a toggle is on"},
    {"hx.toggle", "(u, order, on)", "a toggle turned on or off"},
    {"hx.charges", "(u, order)", "an order's charges and frames to the next"},
    {"hx.autocast", "(u, order, on?)", "whether the unit casts it by itself"},
    {"hx.channel_stop", "(u)", "the unit's channel stopped"},
    {"hx.log", "(...)", "a line in the scripts' log"},
    {"hx.setting", "(id)", "a mode setting's value for this game"},
    {"hx.run", "", "the table the mode's generate returned, read only"},
    {"hx.cast", "(u, order, target | x, y)", "the unit casts the order as its button would"},
    {"hx.own", "(units)", "the mode takes the units off their owner's computer"},
    {"hx.release", "(units)", "and gives them back"},
    {"hx.owned", "(u)", "whether the mode owns the unit"},
    {"hx.win", "(players)", "those players win"},
    {"hx.lose", "(players)", "those players lose"},
    {"hx.message", "(text | key, players | {values, players})", "a line on the game's message line"},
    {"hx.hud", "(key, value)", "a value the HUD draws as mode.<key>"},
    {"hx.timer", "(id, {label, ends | seconds | up, style, anchor, tone} | nil)",
     "a countdown over the game, nil to clear"},
    {"hx.banner", "(id, {title, lines, seconds, anchor, tone} | nil)", "a title with lines over the game"},
    {"hx.counter", "(id, {label, value, of, style, anchor, tone} | n | nil)", "a count over the game, as 5 of 25"},
    {"hx.ping", "(x, y)", "a ping on the minimap"},
    {"hx.minimap", "()", "the minimap's picture: {width, height, scale}, false before there is one"},
    {"hx.reveal", "(x, y, radius, seconds?, players?)", "the ground round the place shown"},
    {"math.sin", "(x)", "the sine, the same on every machine"},
    {"math.cos", "(x)", "the cosine"},
    {"math.atan2", "(y, x)", "the angle of a direction"},
    {"math.sqrt", "(x)", "the square root"},
    {"math.floor", "(x)", "the whole number at or below"},
    {"math.ceil", "(x)", "the whole number at or above"},
    {"math.abs", "(x)", "the size of a number"},
    {"math.min", "(a, b, ...)", "the least"},
    {"math.max", "(a, b, ...)", "the most"},
    {"math.clamp", "(x, lo, hi)", "x kept between lo and hi"},
    {"math.random", "() | (n) | (a, b)", "the game's random, as hx.random"},
    {"math.pi", "", "pi"},
};

const ModLuaName kHooks[] = {
    {"block script", "orders.<order>.blocks[{\"do\": \"script\", \"call\": fn}]",
     "where the cast's program has it, with e.caster, e.target, e.x, e.y, e.order, e.player"},
    {"on_hit", "units.<unit>.scripts.on_hit",
     "at the frame's end after a unit of the type hits with a weapon: e.unit, e.target, e.weapon, e.damage, e.taken"},
    {"on_damaged", "units.<unit>.scripts.on_damaged",
     "as a unit of the type is damaged, before shields and armour: e.unit, e.source, e.weapon, e.damage; return the damage to take"},
    {"on_death", "units.<unit>.scripts.on_death", "at the frame's end after a unit of the type dies: e.unit, e.type, e.owner, e.x, e.y; hx.get still reads e.unit's values"},
    {"on_made", "units.<unit>.scripts.on_made", "once, on the first frame a unit of the type is finished: e.unit"},
    {"tick", "units.<unit>.scripts.tick, every", "each unit of the type, every so many seconds: e.unit"},
    {"toggle on, off", "orders.<order>.toggle {on, off, drain}",
     "as the toggle is cast on or off, or runs out of energy: e.unit, e.order, e.reason"},
    {"channel tick, stop", "orders.<order>.channel {tick, every, stop, seconds}",
     "while the caster stands after the cast: e.caster, e.target, e.x, e.y, e.ticks; stop with e.reason"},
    {"autocast choose", "orders.<order>.autocast {choose, every}",
     "for an idle caster that can cast it: e.unit, e.order; return a unit, or x and y, to cast at"},
    {"game start, tick", "game {start, tick, every}", "the game's first frame, and every so many seconds: e.frame"},
    {"game on_death", "game {on_death}",
     "at the frame's end after any unit of the game dies: e.unit, e.type, e.owner, e.x, e.y, e.killer"},
    {"game director", "game {director: {call, every}}",
     "every so many seconds, to play the player a mode plays itself: e.frame, e.seconds, e.player, "
     "e.units, e.buildings, e.minerals, e.gas"},
    {"brain", "units.<unit>.scripts {brain, every}",
     "every so many seconds for each unit of the type a computer or the mode owns: e.unit; it may order and cast"},
    {"cast ai", "cards.<card>[{\"ai\": {\"call\": fn, \"every\": s}}]",
     "for a caster a computer or the mode owns: e.unit, e.order; return a unit, or x and y, or nothing"},
    {"generate", "generate",
     "once as the game loads, with ctx (seed, settings, players, enemy, map, row); returns records and run"},
};

// ---- the state built -----------------------------------------------------------------------------------

int OwnMathSin(lua_State *L) {
  lua_pushnumber(L, OwnSin(luaL_checknumber(L, 1)));
  return 1;
}
int OwnMathCos(lua_State *L) {
  lua_pushnumber(L, OwnCos(luaL_checknumber(L, 1)));
  return 1;
}
int OwnMathTan(lua_State *L) {
  const double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, OwnSin(x) / OwnCos(x));
  return 1;
}
int OwnMathAtan2(lua_State *L) {
  lua_pushnumber(L, OwnAtan2(luaL_checknumber(L, 1), luaL_optnumber(L, 2, 1)));
  return 1;
}
int Refused(lua_State *L) {
  luaL_error(L, "this function is not in a mod's scripts: it is not the same on every machine");
  return 0;
}

void OpenLib(lua_State *L, lua_CFunction f, const char *pszName) {
  lua_pushcfunction(L, f, nullptr);
  lua_pushstring(L, pszName);
  lua_call(L, 1, 0);
}

// The hx functions the presentation VM has: the ones that only read. Anything not named here is
// left out of that state, so a function that acts on the game cannot be reached from a call made
// for one viewer. hx.random is left out with the rest: it draws from the game's own random, and a
// draw taken for one viewer would move every other machine's next draw.
const char *const kaReaders[] = {"units",    "alive",    "type",     "is",       "owner",  "position",
                                 "is_building", "can_target", "can_reach", "enemies", "life", "max_life", "shields", "max_shields",
                                 "energy",   "max_energy", "has_status", "statuses", "get",  "game_get",
                                 "frame",    "seconds",  "distance", "isqrt",    "pow",    "id",
                                 "toggled",  "charges",  "setting",  "owned",    "log",    "minimap"};
bool Reads(const char *pszName) {
  for (const char *psz : kaReaders)
    if (strcmp(pszName, psz) == 0)
      return true;
  return false;
}

// The libraries a script has, the hx table, and the math that is the same everywhere. bView is
// the presentation VM, which has the readers alone.
void Libraries(lua_State *L, bool bView) {
  OpenLib(L, luaopen_base, "");
  OpenLib(L, luaopen_table, LUA_TABLIBNAME);
  OpenLib(L, luaopen_string, LUA_STRLIBNAME);
  OpenLib(L, luaopen_bit32, LUA_BITLIBNAME);
  OpenLib(L, luaopen_utf8, LUA_UTF8LIBNAME);
  OpenLib(L, luaopen_math, LUA_MATHLIBNAME);
  for (const char *psz : {"collectgarbage", "gcinfo", "getfenv", "setfenv", "loadstring", "newproxy"}) {
    lua_pushnil(L);
    lua_setglobal(L, psz);
  }
  lua_pushcfunction(L, HxLog, "print");
  lua_setglobal(L, "print");
  lua_pushcfunction(L, OwnTostring, "tostring");
  lua_setglobal(L, "tostring");
  // math: the exact ones kept, the others this file's own or refused
  lua_getglobal(L, LUA_MATHLIBNAME);
  const struct {
    const char *psz;
    lua_CFunction f;
  } kMath[] = {{"sin", OwnMathSin},     {"cos", OwnMathCos},   {"tan", OwnMathTan},     {"atan2", OwnMathAtan2},
               {"random", bView ? Refused : HxRandom},
               {"asin", Refused},     {"acos", Refused},       {"atan", Refused},
               {"exp", Refused},        {"log", Refused},      {"log10", Refused},      {"pow", Refused},
               {"sinh", Refused},       {"cosh", Refused},     {"tanh", Refused},       {"noise", Refused},
               {"ldexp", Refused},      {"frexp", Refused},    {"randomseed", Refused}};
  for (const auto &m : kMath) {
    lua_pushcfunction(L, m.f, m.psz);
    lua_setfield(L, -2, m.psz);
  }
  lua_pop(L, 1);
  lua_createtable(L, 0, static_cast<int>(sizeof kApi / sizeof kApi[0]) + 1);
  for (const ApiFn &f : kApi) {
    if (bView && !Reads(f.pszName))
      continue;
    lua_pushcfunction(L, f.pfn, f.pszName);
    lua_setfield(L, -2, f.pszName);
  }
  // hx.readout, the one table only the presentation VM has: fx sets an effect off on the readout
  // for this viewer, at a moment the engine raises no event of its own.
  if (bView) {
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, HxReadoutFx, "fx");
    lua_setfield(L, -2, "fx");
    lua_setfield(L, -2, "readout");
  }
  // the mode's run, made again from the seed for a replay or a load; Freeze makes it read only
  if (!g_runJson.empty()) {
    PortJson run;
    if (PortJsonParse(g_runJson.c_str(), &run) && run.kind != PortJson::NONE) {
      PushJson(L, run);
      lua_setfield(L, -2, "run");
    }
  }
  lua_setglobal(L, "hx");
}

// Every table and function the globals reach made read only, so a call can keep nothing.
void Freeze(lua_State *L, int idx, std::set<const void *> *pSeen) {
  const int t = lua_type(L, idx);
  if (t != LUA_TTABLE && t != LUA_TFUNCTION)
    return;
  const void *pv = lua_topointer(L, idx);
  if (!pSeen->insert(pv).second)
    return;
  idx = lua_absindex(L, idx);
  if (t == LUA_TTABLE) {
    if (!lua_getreadonly(L, idx))
      lua_setreadonly(L, idx, 1);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
      Freeze(L, -2, pSeen);
      Freeze(L, -1, pSeen);
      lua_pop(L, 1);
    }
    if (lua_getmetatable(L, idx)) {
      Freeze(L, -1, pSeen);
      lua_pop(L, 1);
    }
  } else {
    for (int n = 1; lua_getupvalue(L, idx, n) != nullptr; ++n) {
      Freeze(L, -1, pSeen);
      lua_pop(L, 1);
    }
  }
}

lua_State *NewState(Heap *pHeap, bool bView = false) {
  lua_State *L = lua_newstate(Alloc, pHeap);
  if (L == nullptr)
    return nullptr;
  Libraries(L, bView);
  return L;
}

// A chunk of bytecode run as its file's top; false with the error.
bool RunTop(lua_State *L, const std::string &chunk, const std::string &bytecode, std::string *pWhy) {
  const int nTop = lua_gettop(L);
  if (luau_load(L, ("=" + chunk).c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
    *pWhy = lua_tostring(L, -1) ? lua_tostring(L, -1) : chunk + ": does not load";
    lua_settop(L, nTop);
    return false;
  }
  g_nCallLeft = kTopSteps;
  g_nFrameLeft = kTopSteps;
  const bool bTopWas = g_bTop;
  g_bTop = true;
  const int nStatus = lua_pcall(L, 0, 0, 0);
  g_bTop = bTopWas;
  if (nStatus != 0) {
    *pWhy = lua_tostring(L, -1) ? lua_tostring(L, -1) : chunk + ": failed";
    lua_settop(L, nTop);
    return false;
  }
  lua_settop(L, nTop);
  return true;
}

// The files given run in a state of their own, as a game would; false with the first error.
bool TryTops(const std::vector<File> &files, std::string *pWhy) {
  Heap heap;
  lua_State *L = NewState(&heap);
  if (L == nullptr)
    return Refuse(pWhy, "no memory for the scripts");
  lua_callbacks(L)->interrupt = Interrupt;
  bool bOk = true;
  for (const File &f : files)
    if (bOk && !RunTop(L, "scripts/" + f.name, f.bytecode, pWhy))
      bOk = false;
  lua_close(L);
  return bOk;
}

void CloseState() {
  if (g_L != nullptr)
    lua_close(g_L);
  g_L = nullptr;
  if (g_LView != nullptr)
    lua_close(g_LView);
  g_LView = nullptr;
}

// The game's tables and state built from what loaded; a push in a game keeps the units' state.
void Build() {
  CloseState();
  Game g;
  g.bOn = true;
  g.unitHooks.assign(UNIT_TYPES_MAX, -1);
  for (size_t i = 0; i < g_unitHooks.size(); ++i)  // later submissions after earlier: the newest is kept
    if (g_unitHooks[i].nUnit >= 0 && g_unitHooks[i].nUnit < UNIT_TYPES_MAX)
      g.unitHooks[g_unitHooks[i].nUnit] = static_cast<short>(i);
  g.orderSpecs.assign(ORDER_TYPES_MAX, -1);
  for (size_t i = 0; i < g_orderSpecs.size(); ++i)
    if (g_orderSpecs[i].nOrder >= 0 && g_orderSpecs[i].nOrder < ORDER_TYPES_MAX)
      g.orderSpecs[g_orderSpecs[i].nOrder] = static_cast<short>(i);
  for (const GameHooks &h : g_gameHooks)
    g.game = h;
  g_game = g;
  if (g_files.empty())
    return;
  g_heap = Heap();
  g_L = NewState(&g_heap);
  if (g_L == nullptr) {
    Note("no memory for the scripts", true);
    return;
  }
  lua_callbacks(g_L)->interrupt = Interrupt;
  for (const File &f : g_files) {
    std::string why;
    if (!RunTop(g_L, "scripts/" + f.name, f.bytecode, &why))
      Note(why, true);
  }
  std::set<const void *> seen;
  luaL_sandbox(g_L);
  lua_pushvalue(g_L, LUA_GLOBALSINDEX);
  Freeze(g_L, -1, &seen);
  lua_pop(g_L, 1);

  // The presentation VM: the same scripts in a state of their own, with the readers alone and no
  // way to act on the game, for the calls made for one viewer (readout(unit)). It is built only
  // when a script of the mod gives one of those functions, so a mod without them pays nothing.
  // Its state is its own, so what one viewer's call keeps cannot reach the game or another
  // machine; nothing of it is saved, hashed or sent.
  if (!Known(g_functions, "readout"))
    return;
  g_viewHeap = Heap();
  g_LView = NewState(&g_viewHeap, true);
  if (g_LView == nullptr) {
    Note("no memory for the readout's scripts", true);
    return;
  }
  lua_callbacks(g_LView)->interrupt = Interrupt;
  for (const File &f : g_files) {
    std::string why;
    if (!RunTop(g_LView, "scripts/" + f.name, f.bytecode, &why))
      Note(why, true);
  }
  std::set<const void *> seenView;
  luaL_sandbox(g_LView);
  lua_pushvalue(g_LView, LUA_GLOBALSINDEX);
  Freeze(g_LView, -1, &seenView);
  lua_pop(g_LView, 1);
}

// ---- calling the scripts --------------------------------------------------------------------------------

struct Field {
  const char *pszKey;
  int nKind;  // 0 a number, 1 a unit, 2 a string, 3 nil
  double d;
  const char *psz;
};

Field Num(const char *k, double d) {
  return {k, 0, d, nullptr};
}
Field Unit(const char *k, DWORD id) {
  return {k, id ? 1 : 3, static_cast<double>(id), nullptr};
}
Field Str(const char *k, const char *s) {
  return {k, 2, 0, s};
}

// A function called with e made of the fields; its first result in *pRet when it gives a number
// (and in *pRet2 a second one). False when it is not there or stops with an error.
bool CallHook(const std::string &fn, const char *pszWhat, std::initializer_list<Field> fields, double *pRet = nullptr,
              double *pRet2 = nullptr, int *pnRets = nullptr) {
  if (g_L == nullptr || fn.empty() || g_nDepth >= kDepthMax)
    return false;
  lua_State *L = g_L;
  const int nTop = lua_gettop(L);
  lua_getglobal(L, fn.c_str());
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, nTop);
    Note(fn + " (" + pszWhat + ") is no function of the scripts", true);
    return false;
  }
  lua_createtable(L, 0, static_cast<int>(fields.size()));
  for (const Field &f : fields) {
    if (f.nKind == 3)
      continue;
    if (f.nKind == 2)
      lua_pushstring(L, f.psz);
    else
      lua_pushnumber(L, f.d);
    lua_setfield(L, -2, f.pszKey);
  }
  if (g_nDepth == 0)
    g_nCallLeft = kCallSteps;
  ++g_nDepth;
  // the trace names this hook on every row the call makes (presentation)
  const char *const pszWasWhat = g_pszTraceWhat;
  const std::string *const pWasFn = g_pTraceFn;
  g_pszTraceWhat = pszWhat;
  g_pTraceFn = &fn;
  const int nStatus = lua_pcall(L, 1, 2, 0);
  g_pszTraceWhat = pszWasWhat;
  g_pTraceFn = pWasFn;
  --g_nDepth;
  if (nStatus != 0) {
    Note(std::string(lua_tostring(L, -1) ? lua_tostring(L, -1) : "an error") + " (" + pszWhat + ", " + fn + ")", true);
    lua_settop(L, nTop);
    return false;
  }
  int nRets = 0;
  if (pRet && lua_isnumber(L, -2)) {
    *pRet = lua_tonumber(L, -2);
    ++nRets;
    if (pRet2 && lua_isnumber(L, -1)) {
      *pRet2 = lua_tonumber(L, -1);
      ++nRets;
    }
  }
  if (pnRets)
    *pnRets = nRets;
  lua_settop(L, nTop);
  return true;
}

int CallDamaged(CUnit *pUnit, int nWeapon, CUnit *pSource, int nPlayer, int nDamage) {
  const UnitHooks *h = HooksOf(pUnit->wType);
  if (h == nullptr || h->onDamaged.empty())
    return nDamage;
  const bool bWas = g_bSync;
  g_bSync = true;
  double d = 0;
  int nRets = 0;
  const bool bOk = CallHook(h->onDamaged, "on_damaged",
                            {Unit("unit", IdOf(pUnit)), Unit("source", IdOf(pSource)), Num("damage", nDamage / 256.0),
                             nWeapon >= 0 ? Num("weapon", nWeapon) : Field{"weapon", 3, 0, nullptr}, Num("player", nPlayer)},
                            &d, nullptr, &nRets);
  g_bSync = bWas;
  if (!bOk || nRets == 0)
    return nDamage;
  return static_cast<int>(floor(std::max(0.0, std::min(8388607.0, d)) * 256 + 0.5));
}

void ToggleSet(CUnit *pUnit, int nOrder, bool bOn, int nReason) {
  OrderSlot *o = SlotOf(pUnit, nOrder, true);
  if (o == nullptr || ((o->bFlags & kOn) != 0) == bOn)
    return;
  if (bOn)
    o->bFlags |= kOn;
  else
    o->bFlags &= ~kOn;
  o->wCarry = 0;
  Event e;
  memset(&e, 0, sizeof e);
  e.bKind = bOn ? EV_TOGGLE_ON : EV_TOGGLE_OFF;
  e.dwUnit = IdOf(pUnit);
  e.wWeapon = static_cast<WORD>(nOrder);
  e.bReason = static_cast<BYTE>(nReason);
  e.bPlayer = pUnit->bPlayer;
  QueueEvent(e);
}

void ChannelEnd(int i, int nReason) {
  Channel c = g_channels[i];
  for (int k = i; k + 1 < g_gs.nChannels; ++k)
    g_channels[k] = g_channels[k + 1];
  --g_gs.nChannels;
  memset(&g_channels[g_gs.nChannels], 0, sizeof(Channel));
  Event e;
  memset(&e, 0, sizeof e);
  e.bKind = EV_CHANNEL_STOP;
  e.dwUnit = c.dwCaster;
  e.dwOther = c.dwTarget;
  e.wWeapon = c.wOrder;
  e.bReason = static_cast<BYTE>(nReason);
  e.bPlayer = c.bPlayer;
  e.x = c.x;
  e.y = c.y;
  e.nTaken = static_cast<int>(c.dwTicks);
  QueueEvent(e);
}

void GiveEvent(const Event &e) {
  switch (e.bKind) {
  case EV_HIT: {
    CUnit *pSource = UnitOf(e.dwUnit);
    const UnitHooks *h = HooksOf(e.wType);
    if (h && !h->onHit.empty())
      CallHook(h->onHit, "on_hit",
               {Unit("unit", pSource ? e.dwUnit : 0), Unit("target", e.dwOther), Num("weapon", e.wWeapon), Num("damage", e.nDamage / 256.0),
                Num("taken", e.nTaken / 256.0), Num("player", e.bPlayer), Num("x", e.x), Num("y", e.y)});
    return;
  }
  case EV_DEATH: {
    const UnitHooks *h = HooksOf(e.wType);
    if (h && !h->onDeath.empty())
      CallHook(h->onDeath, "on_death",
               {Unit("unit", e.dwUnit), Num("type", e.wType), Num("owner", e.bPlayer), Num("x", e.x), Num("y", e.y),
                Unit("killer", e.dwOther)});
    // and the game's own, for any unit of the game whatever its type
    if (!g_game.game.onDeath.empty())
      CallHook(g_game.game.onDeath, "game on_death",
               {Unit("unit", e.dwUnit), Num("type", e.wType), Num("owner", e.bPlayer), Num("x", e.x), Num("y", e.y),
                Unit("killer", e.dwOther)});
    return;
  }
  case EV_TOGGLE_ON:
  case EV_TOGGLE_OFF: {
    const OrderSpec *sp = SpecOf(e.wWeapon);
    if (sp)
      CallHook(e.bKind == EV_TOGGLE_ON ? sp->on : sp->off, e.bKind == EV_TOGGLE_ON ? "toggle on" : "toggle off",
               {Unit("unit", e.dwUnit), Num("order", e.wWeapon), Str("reason", kReasons[e.bReason])});
    return;
  }
  case EV_CHANNEL_STOP: {
    const OrderSpec *sp = SpecOf(e.wWeapon);
    if (sp)
      CallHook(sp->chStop, "channel stop",
               {Unit("caster", e.dwUnit), Unit("target", e.dwOther), Num("x", e.x), Num("y", e.y), Num("order", e.wWeapon),
                Num("ticks", e.nTaken), Str("reason", kReasons[e.bReason])});
    return;
  }
  }
}

void Events() {
  int nGiven = 0;
  for (int i = 0; i < g_gs.nEvents; ++i) {
    if (++nGiven > kEventsAFrame) {
      Note("events past 4096 in one frame were passed over", true);
      break;
    }
    const Event e = g_events[i];
    GiveEvent(e);
  }
  memset(g_events, 0, sizeof(Event) * g_gs.nEvents);
  g_gs.nEvents = 0;
  memset(&g_dying, 0, sizeof g_dying);
}

void BuildAutocasts() {
  g_game.autocasts.clear();
  g_game.casts.clear();
  g_game.bAutocastsBuilt = true;
  int n = 0;
  const ModCardButton *pButtons = ModCardButtons(&n);
  for (int k = 0; k < n; ++k) {
    const ModCardButton &b = pButtons[k];
    if (b.wUnit != 0xFFFF || b.bResearch || b.bUpgrade || b.bWords || b.bPlace)
      continue;
    const OrderSpec *sp = SpecOf(b.wOrder);
    if (sp && !sp->choose.empty())
      g_game.autocasts.push_back({b.wCard, b.wOrder, b.bTarget != 0});
    // a card's "ai" naming one of the mod's own functions, which also serves a unit the mode
    // owns, where classic's finders serve a computer's own
    if (const char *pszCall = ModCardAiCall(b.nAiCall))
      g_game.casts.push_back({b.wCard, b.wOrder, b.bTarget != 0, pszCall,
                              std::max(1, static_cast<int>(b.wAiEvery))});
  }
}

bool HasCharge(CUnit *pUnit, int nOrder);

// A unit a computer plays, or one the mode has taken: the ones a mode's brains and casts are
// for. A human's own units are never theirs.
bool Decides(CUnit *pUnit) {
  const UnitState *st = StateOf(pUnit);
  if (st != nullptr && st->bOwn != 0)
    return true;
  return pUnit->bPlayer < PLAYER_SLOTS_MAX && gPlayerData[pUnit->bPlayer].nType == 1;  // a computer
}

// A cast a mode's own finder chooses for: the same shape as an order's autocast choose, but
// tried for every caster of the card that a computer or the mode owns, idle or not.
void TryModeCast(CUnit *p, const ModeCast &c) {
  if ((g_gs.dwFrame + static_cast<DWORD>(IndexOf(p))) % static_cast<DWORD>(std::max(1, c.nEvery)) != 0)
    return;
  if (!(p->dwFlags & 1) || Hidden(p) || !HasCharge(p, c.nOrder))
    return;
  if (p->wEnergy < get_order_tech_energy_cost(static_cast<order_id_t>(c.nOrder)))
    return;
  if (p->bOrderId == c.nOrder)
    return;
  double x = 0, y = 0;
  int nRets = 0;
  AiOpen("cast", p, c.call);
  if (!CallHook(c.call, "cast ai", {Unit("unit", IdOf(p)), Num("order", c.nOrder)}, &x, &y, &nRets) || nRets == 0) {
    AiClose("it chose no target");
    return;
  }
  if (!Alive(p)) {
    AiClose("the caster is gone");
    return;
  }
  if (nRets == 2) {
    p->SetOrder_xy(static_cast<order_id_t>(c.nOrder), static_cast<int>(x), static_cast<int>(y));
    AiChose(c.nOrder, -1, static_cast<int>(x), static_cast<int>(y));
  } else if (CUnit *t = UnitOf(static_cast<DWORD>(x))) {
    if (c.bTarget)
      p->SetOrder_CUnit(static_cast<order_id_t>(c.nOrder), t);
    else
      p->SetOrder_xy(static_cast<order_id_t>(c.nOrder), p->pSprite->wPosX, p->pSprite->wPosY);
    AiChose(c.nOrder, IndexOf(t), -1, -1);
  } else {
    AiClose("the unit it chose is gone");
    return;
  }
  AiClose();
}

bool HasCharge(CUnit *pUnit, int nOrder) {
  const OrderSpec *sp = SpecOf(nOrder);
  if (sp == nullptr || sp->nCharges == 0)
    return true;
  OrderSlot *o = SlotOf(pUnit, nOrder, false);
  return o == nullptr || o->bCharges > 0;
}

void TryAutocast(CUnit *p, const Autocast &a) {
  const OrderSpec *sp = SpecOf(a.nOrder);
  if (sp == nullptr || g_gs.dwFrame % static_cast<DWORD>(std::max(1, sp->nAutoEvery)) != 0)
    return;
  if (!(p->dwFlags & 1) || Hidden(p) || !Idle(p) || !HasCharge(p, a.nOrder))
    return;
  if (OrderSlot *o = SlotOf(p, a.nOrder, false))
    if (o->bFlags & kAutoOff)
      return;
  if (p->wEnergy < get_order_tech_energy_cost(static_cast<order_id_t>(a.nOrder)))
    return;
  double x = 0, y = 0;
  int nRets = 0;
  if (!CallHook(sp->choose, "autocast choose", {Unit("unit", IdOf(p)), Num("order", a.nOrder)}, &x, &y, &nRets) || nRets == 0)
    return;
  if (!Alive(p))
    return;
  if (nRets == 2) {
    p->SetOrder_xy(static_cast<order_id_t>(a.nOrder), static_cast<int>(x), static_cast<int>(y));
  } else if (CUnit *t = UnitOf(static_cast<DWORD>(x))) {
    if (a.bTarget)
      p->SetOrder_CUnit(static_cast<order_id_t>(a.nOrder), t);
    else
      p->SetOrder_xy(static_cast<order_id_t>(a.nOrder), p->pSprite->wPosX, p->pSprite->wPosY);
  }
}

// A unit's turn of the frame: made, tick, toggles' drain, charges' recharge, autocast.
void UnitFrame(CUnit *p) {
  const UnitHooks *h = HooksOf(p->wType);
  if (h && (p->dwFlags & 1)) {
    if (!h->onMade.empty() || !h->tick.empty()) {
      UnitState *st = Claim(p);
      if (st && !st->bMade) {
        st->bMade = 1;
        st->wTick = static_cast<WORD>(h->nEvery);
        if (!h->onMade.empty())
          CallHook(h->onMade, "on_made", {Unit("unit", IdOf(p))});
      }
    }
    if (!h->tick.empty() && Alive(p) && !Hidden(p)) {
      UnitState *st = StateOf(p);
      if (st && st->wTick > 0 && --st->wTick == 0) {
        st->wTick = static_cast<WORD>(h->nEvery);
        CallHook(h->tick, "tick", {Unit("unit", IdOf(p))});
      }
    }
  }
  if (!Alive(p))
    return;
  if (UnitState *st = StateOf(p))
    for (OrderSlot &o : st->o) {
      if (o.wOrder == 0)
        continue;
      const OrderSpec *sp = SpecOf(o.wOrder);
      if (sp == nullptr)
        continue;
      if ((o.bFlags & kOn) && sp->nDrain > 0) {
        const int nCarry = o.wCarry + sp->nDrain;
        const int nTake = nCarry / kFps;
        o.wCarry = static_cast<WORD>(nCarry % kFps);
        if (p->wEnergy < nTake)
          ToggleSet(p, o.wOrder, false, R_ENERGY);
        else
          p->wEnergy = static_cast<WORD>(p->wEnergy - nTake);
      }
      if ((o.bFlags & kCharged) && sp->nCharges > 0 && o.bCharges < sp->nCharges) {
        if (o.wRecharge == 0)
          o.wRecharge = static_cast<WORD>(std::max(1, sp->nCooldown));
        if (--o.wRecharge == 0) {
          ++o.bCharges;
          if (o.bCharges < sp->nCharges)
            o.wRecharge = static_cast<WORD>(std::max(1, sp->nCooldown));
        }
      }
    }
  for (const Autocast &a : g_game.autocasts)
    if (a.nUnit == p->wType && Alive(p))
      TryAutocast(p, a);
  // the mode's own: a unit's brain and a card's "ai" casts, for a computer's units and the ones
  // the mode has taken; the frame and the unit's place decide when, so no state is kept for it.
  if (!Alive(p) || Hidden(p) || !(p->dwFlags & 1) || !Decides(p))
    return;
  if (h != nullptr && !h->brain.empty() &&
      (g_gs.dwFrame + static_cast<DWORD>(IndexOf(p))) % static_cast<DWORD>(std::max(1, h->nBrainEvery)) == 0) {
    AiOpen("brain", p, h->brain);
    CallHook(h->brain, "brain", {Unit("unit", IdOf(p))});
    AiClose();
  }
  for (const ModeCast &c : g_game.casts)
    if (c.nUnit == p->wType && Alive(p))
      TryModeCast(p, c);
}

// The ground the mode revealed, opened again after each sight refresh put the fog back, until
// each place's seconds run out.
void Reveals() {
  for (int i = 0; i < g_gs.nReveals; ++i) {
    if (g_reveals[i].wLeft > 0 && --g_reveals[i].wLeft == 0) {
      for (int k = i; k + 1 < g_gs.nReveals; ++k)
        g_reveals[k] = g_reveals[k + 1];
      --g_gs.nReveals;
      memset(&g_reveals[g_gs.nReveals], 0, sizeof(Reveal));
      --i;
    }
  }
}

void Channels() {
  for (int i = 0; i < g_gs.nChannels; ++i) {
    Channel &c = g_channels[i];
    CUnit *p = UnitOf(c.dwCaster);
    int nStop = -1;
    if (p == nullptr)
      nStop = R_DIED;
    else if (p->pSprite->wPosX != c.wStandX || p->pSprite->wPosY != c.wStandY)
      nStop = R_MOVED;
    else if (p->bOrderId != c.wOrder && !Idle(p))
      nStop = R_ORDERED;
    if (nStop >= 0) {
      ChannelEnd(i--, nStop);
      continue;
    }
    if (c.wNext > 0 && --c.wNext == 0) {
      const OrderSpec *sp = SpecOf(c.wOrder);
      c.wNext = static_cast<WORD>(sp ? std::max(1, sp->nChEvery) : kFps);
      ++c.dwTicks;
      if (sp)
        CallHook(sp->chTick, "channel tick",
                 {Unit("caster", c.dwCaster), Unit("target", c.dwTarget), Num("x", c.x), Num("y", c.y), Num("order", c.wOrder),
                  Num("ticks", c.dwTicks)});
    }
    if (i < g_gs.nChannels && g_channels[i].dwCaster == c.dwCaster && c.dwLeft > 0 && --c.dwLeft == 0)
      ChannelEnd(i--, R_ENDED);
  }
}

// ---- the live channel --------------------------------------------------------------------------------

std::string NumJson(double d) {
  char sz[40];
  snprintf(sz, sizeof sz, "%.10g", d);
  return sz;
}

std::string ValueJson(const Value &v) {
  return v.bKind == 2 ? (v.d != 0 ? "true" : "false") : NumJson(v.d);
}

bool CmdScripts(const PortJson &j, std::string *pAnswer) {
  std::string s = "\"on\":" + std::string(g_game.bOn && g_L ? "true" : "false") + ",\"files\":[";
  for (size_t i = 0; i < g_files.size(); ++i)
    s += std::string(i ? "," : "") + PortJsonQuote(g_files[i].name);
  s += "],\"functions\":[";
  for (size_t i = 0; i < g_functions.size(); ++i)
    s += std::string(i ? "," : "") + "{\"name\":" + PortJsonQuote(g_functions[i].name) + ",\"file\":" +
         PortJsonQuote(g_functions[i].file) + ",\"line\":" + std::to_string(g_functions[i].nLine) + "}";
  s += "],\"frame\":" + std::to_string(g_gs.dwFrame) + ",\"errors\":" + std::to_string(g_nErrors) +
       ",\"heap\":" + std::to_string(g_heap.n) + ",\"channels\":[";
  for (int i = 0; i < g_gs.nChannels; ++i) {
    const Channel &c = g_channels[i];
    s += std::string(i ? "," : "") + "{\"caster\":" + std::to_string(c.dwCaster) + ",\"order\":" + std::to_string(c.wOrder) +
         ",\"ticks\":" + std::to_string(c.dwTicks) + ",\"left\":" + std::to_string(c.dwLeft) + "}";
  }
  s += "],\"game\":{";
  bool bFirst = true;
  for (const Value &v : g_gs.v)
    if (v.wKey) {
      s += std::string(bFirst ? "" : ",") + PortJsonQuote(g_gs.keys[v.wKey - 1]) + ":" + ValueJson(v);
      bFirst = false;
    }
  s += "},\"units\":[";
  bFirst = true;
  for (CUnit *p = g_game.bOn ? CUnit::GetUsedList() : nullptr; p != nullptr; p = p->pNext) {
    const UnitState *st = StateOf(p);
    if (st == nullptr)
      continue;
    std::string u = "{\"id\":" + std::to_string(IndexOf(p)) + ",\"unit\":" + std::to_string(st->dwId) +
                    ",\"type\":" + std::to_string(p->wType) + ",\"values\":{";
    bool bV = true;
    for (const Value &v : st->v)
      if (v.wKey) {
        u += std::string(bV ? "" : ",") + PortJsonQuote(g_gs.keys[v.wKey - 1]) + ":" + ValueJson(v);
        bV = false;
      }
    u += "},\"orders\":[";
    bool bO = true;
    for (const OrderSlot &o : st->o)
      if (o.wOrder) {
        u += std::string(bO ? "" : ",") + "{\"order\":" + std::to_string(o.wOrder) + ",\"on\":" + ((o.bFlags & kOn) ? "true" : "false") +
             ",\"charges\":" + std::to_string(o.bCharges) + ",\"recharge\":" + std::to_string(o.wRecharge) + "}";
        bO = false;
      }
    s += std::string(bFirst ? "" : ",") + u + "]}";
    bFirst = false;
  }
  // the log's lines past since
  const unsigned nSince = static_cast<unsigned>(j["since"].Num(0));
  const unsigned nFirst = g_nLogged - static_cast<unsigned>(g_log.size());
  s += "],\"logged\":" + std::to_string(g_nLogged) + ",\"log\":[";
  bFirst = true;
  for (size_t i = 0; i < g_log.size(); ++i)
    if (nFirst + i >= nSince) {
      s += std::string(bFirst ? "" : ",") + PortJsonQuote(g_log[i]);
      bFirst = false;
    }
  *pAnswer = s + "]";
  return true;
}

// script_call {function, unit?, target?, x?, y?}: a function called now with e made of those, in
// Debug Tools' canvas game only, for a modder to try a hook; units by their place in the unit table
// as the channel's units gives them, or "selected" and "second" for the selection's first two.
bool CmdScriptCall(const PortJson &j, std::string *pAnswer) {
  if (!PortLaunchIsCanvas()) {
    *pAnswer = "script_call is for Debug Tools' canvas game, as a call outside the game's frame is no replay's";
    return false;
  }
  if (g_L == nullptr) {
    *pAnswer = "the game has no scripts";
    return false;
  }
  const std::string fn = j["function"].Str();
  // "selected" is the first unit of the game's selection, "second" its second
  int anSel[2] = {-1, -1};
  PortBenchSelection(anSel, 2);
  auto idAt = [&anSel](const PortJson &v) -> DWORD {
    double d = v.kind == PortJson::NUMBER ? v.d : -1;
    if (v.kind == PortJson::STRING && v.s == "selected")
      d = anSel[0];
    else if (v.kind == PortJson::STRING && v.s == "second")
      d = anSel[1];
    if (d < 0 || d >= g_pools.units)
      return 0;
    CUnit *p = CUnit::Head() + static_cast<int>(d);
    return Alive(p) ? IdOf(p) : 0;
  };
  const DWORD u = idAt(j["unit"]), t = idAt(j["target"]);
  CUnit *pu = UnitOf(u);
  const int x = static_cast<int>(j["x"].Num(pu ? static_cast<int>(pu->pSprite->wPosX) : 0));
  const int y = static_cast<int>(j["y"].Num(pu ? static_cast<int>(pu->pSprite->wPosY) : 0));
  const unsigned nBefore = g_nLogged;
  double d = 0, d2 = 0;
  int nRets = 0;
  g_nFrameLeft = kFrameSteps;
  const bool bOk = CallHook(fn, "script_call",
                            {Unit("unit", u), Unit("caster", u), Unit("target", t), Num("x", x), Num("y", y), Num("player", pu ? pu->bPlayer : 0),
                             Num("test", 1)},
                            &d, &d2, &nRets);
  Events();
  PortControlChanged();
  std::string lines;
  const unsigned nFirst = g_nLogged - static_cast<unsigned>(g_log.size());
  for (size_t i = 0; i < g_log.size(); ++i)
    if (nFirst + i >= nBefore)
      lines += std::string(lines.empty() ? "" : ",") + PortJsonQuote(g_log[i]);
  *pAnswer = "\"called\":" + std::string(bOk ? "true" : "false") + ",\"unit\":" + std::to_string(u) +
             (nRets > 0 ? ",\"result\":" + NumJson(d) : std::string()) +
             (nRets > 1 ? ",\"result2\":" + NumJson(d2) : std::string()) + ",\"log\":[" + lines + "]";
  return true;
}

// script_trace {on?, since?, ai_since?, run?}: the trace of what the scripts do, its rows from
// "since" on, and the mode's own decisions from "ai_since" on (Trace and AiOpen above), each
// row's "at" being its number. "on" switches both, and switching them off drops what they
// kept; "run" asks for the mode's run beside them. Read only: the game plays the same either
// way.
bool CmdScriptTrace(const PortJson &j, std::string *pAnswer) {
  if (j["on"].kind == PortJson::BOOL)
    ModLuaTraceSet(j["on"].b);
  const unsigned nSince = static_cast<unsigned>(j["since"].Num(0));
  const unsigned nAiSince = static_cast<unsigned>(j["ai_since"].Num(0));
  std::string rows;
  for (const TraceRow &r : g_trace) {
    if (r.dwAt < nSince)
      continue;
    rows += std::string(rows.empty() ? "" : ",") + "{\"at\":" + std::to_string(r.dwAt) +
            ",\"frame\":" + std::to_string(r.dwFrame) + ",\"act\":" + PortJsonQuote(r.act);
    if (r.nUnit >= 0)
      rows += ",\"unit\":" + std::to_string(r.nUnit) + ",\"type\":" + std::to_string(r.nType) +
              ",\"player\":" + std::to_string(r.nPlayer);
    if (!r.from.empty())
      rows += ",\"from\":" + PortJsonQuote(r.from);
    if (!r.detail.empty())
      rows += ",\"detail\":" + PortJsonQuote(r.detail);
    rows += "}";
  }
  // the mode's own decisions, from "ai_since" on
  std::string ai;
  for (const ModLuaAiCall &a : g_ai) {
    if (a.dwAt < nAiSince)
      continue;
    ai += std::string(ai.empty() ? "" : ",") + "{\"at\":" + std::to_string(a.dwAt) +
          ",\"frame\":" + std::to_string(a.dwFrame) + ",\"kind\":" + PortJsonQuote(a.kind) +
          ",\"fn\":" + PortJsonQuote(a.fn) + ",\"chose\":" + (a.bChose ? "true" : "false");
    if (a.nUnit >= 0)
      ai += ",\"unit\":" + std::to_string(a.nUnit) + ",\"type\":" + std::to_string(a.nType) +
            ",\"player\":" + std::to_string(a.nPlayer);
    if (a.nOrder >= 0)
      ai += ",\"order\":" + std::to_string(a.nOrder);
    if (a.nTarget >= 0)
      ai += ",\"target\":" + std::to_string(a.nTarget);
    if (a.x >= 0)
      ai += ",\"x\":" + std::to_string(a.x) + ",\"y\":" + std::to_string(a.y);
    if (!a.why.empty())
      ai += ",\"why\":" + PortJsonQuote(a.why);
    ai += "}";
  }
  *pAnswer = "\"on\":" + std::string(g_bTrace ? "true" : "false") + ",\"traced\":" +
             std::to_string(g_nTraced) + ",\"kept\":" + std::to_string(kTraceKept) + ",\"mode\":" +
             PortJsonQuote(g_modeId) + ",\"frame\":" + std::to_string(g_gs.dwFrame) + ",\"rows\":[" +
             rows + "],\"ai_called\":" + std::to_string(g_nAiCalls) + ",\"ai_kept\":" +
             std::to_string(kAiKept) + ",\"ai\":[" + ai + "]";
  if (j["run"].kind == PortJson::BOOL && j["run"].b)
    *pAnswer += ",\"run\":" + (g_runJson.empty() ? std::string("{}") : g_runJson);
  return true;
}

// ---- reading the mod's files ---------------------------------------------------------------------------

bool ScriptName(const std::string &name) {
  const size_t n = name.size();
  return (n > 4 && name.compare(n - 4, 4, ".lua") == 0) || (n > 5 && name.compare(n - 5, 5, ".luau") == 0);
}

// scripts\ and the folders in it, each file named by its path under scripts\ ("ai/lib.luau"),
// so a mod may put its scripts in folders of its own. Folders four deep at most.
void ReadFolder(const std::string &folder, const std::string &at, int nDepth,
                std::vector<std::pair<std::string, std::string>> *pOut) {
  if (nDepth > 4)
    return;
  std::vector<std::string> folders;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((folder + "scripts\\" + at + "*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    const std::string name = fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (name != "." && name != "..")
        folders.push_back(name);
      continue;
    }
    if (!ScriptName(name))
      continue;
    std::string text;
    FILE *f = fopen((folder + "scripts\\" + at + name).c_str(), "rb");
    if (f == nullptr)
      continue;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
      text.append(buf, n);
    fclose(f);
    std::string under = at + name;
    for (char &ch : under)
      if (ch == '\\')
        ch = '/';
    pOut->push_back({under, text});
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  std::sort(folders.begin(), folders.end());
  for (const std::string &f : folders)
    ReadFolder(folder, at + f + "\\", nDepth + 1, pOut);
}

// ---- a mode's generate and its set-up calls -----------------------------------------------------------
// Both run in a state of their own, with no game to reach: generate once as the game loads,
// the set-up's effect, read and shown while the lobby is open. The map's walk and walkable are
// C functions on the read, and ctx.row reads a row of the loader's tables.

const ModLuaGenCtx *g_pGenCtx;  // the ctx of the call running, for its C functions

int GenRow(lua_State *L) {
  const char *pszTable = luaL_checkstring(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  PortJson row;
  if (g_pGenCtx == nullptr || !g_pGenCtx->row || !g_pGenCtx->row(pszTable, pszName, &row))
    luaL_error(L, "ctx.row: no %s is named %s", pszTable, pszName);
  PushJson(L, row);
  return 1;
}

int GenWalk(lua_State *L) {
  const int x1 = static_cast<int>(luaL_checknumber(L, 1)), y1 = static_cast<int>(luaL_checknumber(L, 2));
  const int x2 = static_cast<int>(luaL_checknumber(L, 3)), y2 = static_cast<int>(luaL_checknumber(L, 4));
  lua_pushinteger(L, ModeMapWalk(x1, y1, x2, y2));
  return 1;
}

int GenWalkable(lua_State *L) {
  lua_pushboolean(L, ModeMapWalkable(static_cast<int>(luaL_checknumber(L, 1)), static_cast<int>(luaL_checknumber(L, 2))));
  return 1;
}

// ctx.map: the starts and the clusters as tables, with walk and walkable beside them.
void PushMap(lua_State *L, const ModeMapRead *pMap) {
  lua_createtable(L, 0, 4);
  lua_createtable(L, pMap ? static_cast<int>(pMap->starts.size()) : 0, 0);
  for (size_t i = 0; pMap && i < pMap->starts.size(); ++i) {
    const ModeMapStart &s = pMap->starts[i];
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, s.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, s.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, s.tx);
    lua_setfield(L, -2, "tx");
    lua_pushnumber(L, s.ty);
    lua_setfield(L, -2, "ty");
    lua_pushnumber(L, s.nSlot);
    lua_setfield(L, -2, "slot");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  lua_setfield(L, -2, "starts");
  lua_createtable(L, pMap ? static_cast<int>(pMap->bases.size()) : 0, 0);
  for (size_t i = 0; pMap && i < pMap->bases.size(); ++i) {
    const ModeMapBase &b = pMap->bases[i];
    lua_createtable(L, 0, 6);
    lua_pushnumber(L, b.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, b.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, b.tx);
    lua_setfield(L, -2, "tx");
    lua_pushnumber(L, b.ty);
    lua_setfield(L, -2, "ty");
    lua_pushnumber(L, b.nFields);
    lua_setfield(L, -2, "fields");
    lua_pushnumber(L, b.nSpread);
    lua_setfield(L, -2, "spread");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  lua_setfield(L, -2, "bases");
  lua_pushnumber(L, pMap ? pMap->w : 0);
  lua_setfield(L, -2, "w");
  lua_pushnumber(L, pMap ? pMap->h : 0);
  lua_setfield(L, -2, "h");
  lua_pushcfunction(L, GenWalk, "walk");
  lua_setfield(L, -2, "walk");
  lua_pushcfunction(L, GenWalkable, "walkable");
  lua_setfield(L, -2, "walkable");
}

void PushCtx(lua_State *L, const ModLuaGenCtx &ctx) {
  lua_createtable(L, 0, 6);
  lua_pushnumber(L, static_cast<double>(ctx.dwSeed));
  lua_setfield(L, -2, "seed");
  if (ctx.pSettings != nullptr)
    PushJson(L, *ctx.pSettings);
  else
    lua_createtable(L, 0, 0);
  lua_setfield(L, -2, "settings");
  lua_createtable(L, static_cast<int>(ctx.players.size()), 0);
  for (size_t i = 0; i < ctx.players.size(); ++i) {
    const ModLuaGenPlayer &p = ctx.players[i];
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, p.nPlayer);
    lua_setfield(L, -2, "player");
    lua_pushstring(L, p.race.c_str());
    lua_setfield(L, -2, "race");
    lua_pushstring(L, p.kind.c_str());
    lua_setfield(L, -2, "kind");
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, p.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, p.y);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "start");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  lua_setfield(L, -2, "players");
  if (ctx.nEnemy >= 0 && ctx.nEnemy < static_cast<int>(ctx.players.size())) {
    const ModLuaGenPlayer &e = ctx.players[ctx.nEnemy];
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, e.nPlayer);
    lua_setfield(L, -2, "player");
    lua_pushstring(L, e.race.c_str());
    lua_setfield(L, -2, "race");
    lua_pushnumber(L, e.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, e.y);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "enemy");
  }
  PushMap(L, ctx.pMap);
  lua_setfield(L, -2, "map");
  lua_pushcfunction(L, GenRow, "row");
  lua_setfield(L, -2, "row");
}

// A state with every file's top run, for a call apart from a game; null with why.
lua_State *StateApart(Heap *pHeap, const std::vector<File> &files, std::string *pWhy) {
  lua_State *L = NewState(pHeap);
  if (L == nullptr) {
    *pWhy = "no memory for the scripts";
    return nullptr;
  }
  lua_callbacks(L)->interrupt = Interrupt;
  for (const File &f : files)
    if (!RunTop(L, "scripts/" + f.name, f.bytecode, pWhy)) {
      lua_close(L);
      return nullptr;
    }
  return L;
}

}  // namespace

bool ModLuaHasGenerate() {
  return !g_generate.first.empty();
}

bool ModLuaGenerate(const ModLuaGenCtx &ctx, std::string *pTypes, std::string *pRun, std::string *pWhy) {
  pTypes->clear();
  pRun->clear();
  if (g_generate.first.empty())
    return true;
  Heap heap;
  lua_State *L = StateApart(&heap, g_files, pWhy);
  if (L == nullptr)
    return false;
  const bool bTopWas = g_bTop;
  g_bTop = true;  // no game to reach: the records come from the seed and the settings alone
  g_pGenCtx = &ctx;
  lua_getglobal(L, g_generate.first.c_str());
  bool bOk = lua_isfunction(L, -1) != 0;
  if (!bOk) {
    *pWhy = g_generate.first + " is no function of the mod's scripts";
  } else {
    PushCtx(L, ctx);
    g_nCallLeft = kGenerateSteps;
    g_nFrameLeft = kGenerateSteps;
    if (lua_pcall(L, 1, 1, 0) != 0) {
      *pWhy = lua_tostring(L, -1) ? lua_tostring(L, -1) : (g_generate.first + " failed");
      bOk = false;
    } else if (!lua_istable(L, -1)) {
      *pWhy = g_generate.first + " returns a table of records, in types.json's own shape";
      bOk = false;
    } else {
      // run comes out on its own: the scripts read it as hx.run, and it is never a record
      lua_getfield(L, -1, "run");
      if (!lua_isnil(L, -1)) {
        PortJson run;
        if (!ValueToJson(L, -1, &run, 0, pWhy)) {
          *pWhy = g_generate.first + "'s run holds " + *pWhy;
          bOk = false;
        } else {
          *pRun = PortJsonWrite(run);
        }
      }
      lua_pop(L, 1);
      if (bOk) {
        lua_pushnil(L);
        lua_setfield(L, -2, "run");
        PortJson types;
        if (!ValueToJson(L, -1, &types, 0, pWhy)) {
          *pWhy = g_generate.first + " returned " + *pWhy;
          bOk = false;
        } else {
          *pTypes = PortJsonWrite(types);
        }
      }
    }
  }
  g_pGenCtx = nullptr;
  g_bTop = bTopWas;
  lua_close(L);
  return bOk;
}

// ---- a mode's set-up calls -------------------------------------------------------------------------------

bool ModLuaSetupCall(const std::string &folder, const std::string &fn, const PortJson *pValue, const PortJson *pValues,
                     const ModeMapRead *pMap, std::string *pText, bool *pbTrue, std::string *pWhy) {
  pText->clear();
  if (pbTrue != nullptr)
    *pbTrue = true;
  if (fn.empty())
    return true;
  // the mod's scripts, read and compiled once a folder
  static std::string s_folder;
  static std::vector<File> s_files;
  static bool s_bRead;
  if (!s_bRead || s_folder != folder) {
    s_folder = folder;
    s_bRead = true;
    s_files.clear();
    std::vector<std::pair<std::string, std::string>> texts;
    ReadFolder(folder, std::string(), 0, &texts);
    std::sort(texts.begin(), texts.end());
    for (const auto &t : texts) {
      File f;
      f.name = t.first;
      std::string why;
      if (ModLuaCompile("scripts/" + t.first, t.second, &f.bytecode, &why))
        s_files.push_back(f);
      else
        Log("modes: %s does not compile: %s", t.first, why);
    }
  }
  Heap heap;
  lua_State *L = StateApart(&heap, s_files, pWhy);
  if (L == nullptr)
    return false;
  const bool bTopWas = g_bTop;
  g_bTop = true;
  lua_getglobal(L, fn.c_str());
  bool bOk = lua_isfunction(L, -1) != 0;
  if (!bOk) {
    *pWhy = fn + " is no function of the mod's scripts";
  } else {
    // the arguments in the order the mode's functions take them: the one setting's value, every
    // setting's value, then the map. A caller leaves out the ones its function does not take,
    // so effect(value, values), read(value, map) and preview(values, map) each take two
    int nArgs = 0;
    if (pValue != nullptr) {
      PushJson(L, *pValue);
      ++nArgs;
    }
    if (pValues != nullptr) {
      PushJson(L, *pValues);
      ++nArgs;
    }
    if (pMap != nullptr) {
      PushMap(L, pMap);
      ++nArgs;
    }
    g_nCallLeft = kCallSteps;
    g_nFrameLeft = kCallSteps;
    if (lua_pcall(L, nArgs, 1, 0) != 0) {
      *pWhy = lua_tostring(L, -1) ? lua_tostring(L, -1) : (fn + " failed");
      bOk = false;
    } else {
      if (lua_isstring(L, -1)) {
        *pText = lua_tostring(L, -1);
      } else if (lua_isnumber(L, -1)) {
        *pText = std::to_string(static_cast<long long>(lua_tonumber(L, -1)));
      } else if (lua_istable(L, -1)) {
        // a function that answers with a table hands it back as JSON text, which the caller
        // reads into whatever it asked for (a mode's preview, PortModePreview)
        PortJson j;
        std::string whyJson;
        if (ValueToJson(L, lua_gettop(L), &j, 0, &whyJson))
          *pText = PortJsonWrite(j);
        else
          Log("modes: %s's table does not read: %s", fn, whyJson);
      }
      if (pbTrue != nullptr)
        *pbTrue = lua_isnil(L, -1) ? true : lua_toboolean(L, -1) != 0;
    }
  }
  g_bTop = bTopWas;
  lua_close(L);
  return bOk;
}

// ---- the names ------------------------------------------------------------------------------------------

const ModLuaName *ModLuaApi(int *pnCount) {
  *pnCount = static_cast<int>(sizeof kApiNames / sizeof kApiNames[0]);
  return kApiNames;
}

const ModLuaName *ModLuaHooks(int *pnCount) {
  *pnCount = static_cast<int>(sizeof kHooks / sizeof kHooks[0]);
  return kHooks;
}

bool ModLuaCheckText(const std::string &chunk, const std::string &text, std::vector<ModLuaProblem> *pProblems,
                     std::vector<ModLuaFunction> *pFunctions) {
  if (!ModLuaLint(text, pProblems, pFunctions))
    return false;
  File f;
  f.name = chunk.compare(0, 8, "scripts/") == 0 ? chunk.substr(8) : chunk;
  std::string why;
  if (!ModLuaCompile(chunk, text, &f.bytecode, &why) || !TryTops({f}, &why)) {
    ModLuaProblem p;
    p.what = why;
    // "chunk:12: message" puts the line in the message
    const size_t c = why.find(':', chunk.size());
    if (why.compare(0, chunk.size(), chunk) == 0 && c != std::string::npos) {
      p.nLine = atoi(why.c_str() + chunk.size() + 1);
      const size_t c2 = why.find(':', c + 1);
      p.what = c2 != std::string::npos ? why.substr(c2 + 1) : why;
      while (!p.what.empty() && p.what[0] == ' ')
        p.what.erase(0, 1);
    }
    p.nEndLine = p.nLine;
    pProblems->push_back(p);
    return false;
  }
  return true;
}

// ---- the loader -----------------------------------------------------------------------------------------

bool ModLuaField(int t, const std::string &key) {
  return (t == 0 && key == "scripts") ||
         (t == 5 && (key == "toggle" || key == "channel" || key == "autocast" || key == "cooldown" || key == "charges"));
}

void ModLuaPushTexts(const PortJson *pScripts) {
  g_pPushTexts = pScripts;
}

bool ModLuaHasFunction(const std::string &name) {
  return Known(g_functions, name);
}

bool ModLuaLoad(const PortJson &types, const std::string &folder, int nSub, const std::vector<ModStatusRecord> &records,
                std::vector<std::pair<std::string, std::string>> *pFiles, std::string *pWhy) {
  // the files, in the order of their names
  std::vector<std::pair<std::string, std::string>> texts;
  if (g_pPushTexts != nullptr) {
    if (g_pPushTexts->kind != PortJson::OBJECT)
      return Refuse(pWhy, "a push's scripts are an object, each file's text by its name");
    for (const PortJsonMember &m : g_pPushTexts->members) {
      if (!ScriptName(m.first) || m.first.find_first_of("\\:") != std::string::npos ||
          m.first.find("..") != std::string::npos || m.second.kind != PortJson::STRING)
        return Refuse(pWhy, "a push's scripts name files of scripts/ or a folder in it, .lua or .luau, each with its text");
      texts.push_back({m.first, m.second.s});
    }
  } else {
    ReadFolder(folder, std::string(), 0, &texts);
  }
  std::sort(texts.begin(), texts.end());
  std::vector<File> files;
  std::vector<Function> fns = g_functions;
  for (const auto &t : texts) {
    const std::string chunk = "scripts/" + t.first;
    std::vector<ModLuaProblem> problems;
    std::vector<ModLuaFunction> found;
    if (!ModLuaLint(t.second, &problems, &found)) {
      const ModLuaProblem &p = problems.front();
      return Refuse(pWhy, chunk + ":" + std::to_string(p.nLine) + ": " + p.what);
    }
    File f;
    f.name = t.first;
    f.nSub = nSub;
    if (!ModLuaCompile(chunk, t.second, &f.bytecode, pWhy))
      return false;
    files.push_back(f);
    for (const ModLuaFunction &m : found)
      fns.push_back({m.name, t.first, m.args, m.nLine, nSub});
    pFiles->push_back({chunk, t.second});
  }
  // every file's top run, the earlier submissions' first, as a game will
  if (!files.empty()) {
    std::vector<File> all = g_files;
    all.insert(all.end(), files.begin(), files.end());
    if (!TryTops(all, pWhy))
      return false;
  }
  // the records' hooks
  std::vector<UnitHooks> units;
  std::vector<OrderSpec> orders;
  for (const ModStatusRecord &r : records) {
    const PortJson &j = *r.pJson;
    if (r.t == 0 && j["scripts"].kind != PortJson::NONE) {
      const std::string where = "units " + r.name + ".scripts";
      const PortJson &s = j["scripts"];
      UnitHooks h;
      h.nUnit = r.id;
      h.nSub = nSub;
      double v = 0;
      if (!Only(s, {"on_hit", "on_damaged", "on_death", "on_made", "tick", "every", "brain"}, where, pWhy) ||
          !FunctionName(s, "on_hit", false, fns, &h.onHit, where, pWhy) ||
          !FunctionName(s, "on_damaged", false, fns, &h.onDamaged, where, pWhy) ||
          !FunctionName(s, "on_death", false, fns, &h.onDeath, where, pWhy) ||
          !FunctionName(s, "on_made", false, fns, &h.onMade, where, pWhy) || !FunctionName(s, "tick", false, fns, &h.tick, where, pWhy) ||
          !FunctionName(s, "brain", false, fns, &h.brain, where, pWhy) ||
          !Number(s, "every", 1.0 / kFps, 600, 1, &v, where, pWhy))
        return false;
      h.nEvery = std::max(1, Frames(v));
      h.nBrainEvery = h.nEvery;  // the brain is asked as often as the tick
      units.push_back(h);
    }
    if (r.t == 5) {
      const std::string where = "orders " + r.name;
      OrderSpec o;
      o.nOrder = r.id;
      o.nSub = nSub;
      bool bAny = false;
      double v = 0;
      if (j["toggle"].kind != PortJson::NONE) {
        const PortJson &t = j["toggle"];
        const std::string at = where + ".toggle";
        if (!Only(t, {"on", "off", "drain"}, at, pWhy) || !FunctionName(t, "on", false, fns, &o.on, at, pWhy) ||
            !FunctionName(t, "off", false, fns, &o.off, at, pWhy) || !Number(t, "drain", 0, 255, 0, &v, at, pWhy))
          return false;
        o.bToggle = bAny = true;
        o.nDrain = static_cast<int>(floor(v * 256 + 0.5));
      }
      if (j["channel"].kind != PortJson::NONE) {
        const PortJson &c = j["channel"];
        const std::string at = where + ".channel";
        if (o.bToggle)
          return Refuse(pWhy, where + " is a toggle or a channel, not both");
        if (!Only(c, {"tick", "every", "stop", "seconds"}, at, pWhy) || !FunctionName(c, "tick", false, fns, &o.chTick, at, pWhy) ||
            !FunctionName(c, "stop", false, fns, &o.chStop, at, pWhy) || !Number(c, "every", 1.0 / kFps, 600, 1, &v, at, pWhy))
          return false;
        o.nChEvery = std::max(1, Frames(v));
        if (!Number(c, "seconds", 0, 2700, 0, &v, at, pWhy))
          return false;
        o.nChFrames = Frames(v);
        o.bChannel = bAny = true;
      }
      if (j["autocast"].kind != PortJson::NONE) {
        const PortJson &a = j["autocast"];
        const std::string at = where + ".autocast";
        if (!Only(a, {"choose", "every"}, at, pWhy) || !FunctionName(a, "choose", true, fns, &o.choose, at, pWhy) ||
            !Number(a, "every", 1.0 / kFps, 60, 0.5, &v, at, pWhy))
          return false;
        o.nAutoEvery = std::max(1, Frames(v));
        bAny = true;
      }
      if (j["cooldown"].kind != PortJson::NONE || j["charges"].kind != PortJson::NONE) {
        if (j["cooldown"].kind == PortJson::NONE)
          return Refuse(pWhy, where + ".charges come back one each cooldown: give cooldown too, in seconds");
        if (!Number(j, "cooldown", 1.0 / kFps, 2700, 0, &v, where, pWhy))
          return false;
        o.nCooldown = std::max(1, Frames(v));
        if (!Number(j, "charges", 1, 255, 1, &v, where, pWhy))
          return false;
        o.nCharges = static_cast<int>(v);
        bAny = true;
      }
      if (bAny)
        orders.push_back(o);
    }
  }
  std::vector<GameHooks> game;
  if (types["game"].kind != PortJson::NONE) {
    const PortJson &gj = types["game"];
    GameHooks h;
    h.nSub = nSub;
    double v = 0;
    if (!Only(gj, {"start", "tick", "every", "on_death", "director"}, "game", pWhy) ||
        !FunctionName(gj, "start", false, fns, &h.start, "game", pWhy) ||
        !FunctionName(gj, "tick", false, fns, &h.tick, "game", pWhy) ||
        !FunctionName(gj, "on_death", false, fns, &h.onDeath, "game", pWhy) ||
        !Number(gj, "every", 1.0 / kFps, 600, 1, &v, "game", pWhy))
      return false;
    h.nEvery = std::max(1, Frames(v));
    // "director": {call, every}, the hook that plays the mode's own player
    if (gj["director"].kind != PortJson::NONE) {
      const PortJson &dj = gj["director"];
      double dEvery = 0;
      if (!Only(dj, {"call", "every"}, "game's director", pWhy) ||
          !FunctionName(dj, "call", true, fns, &h.director, "game's director", pWhy) ||
          !Number(dj, "every", 1.0 / kFps, 600, 1, &dEvery, "game's director", pWhy))
        return false;
      h.nDirectorEvery = std::max(1, Frames(dEvery));
    }
    game.push_back(h);
  }
  // types.json's "generate", called once as a game loads (ModLuaGenerate)
  std::string generate;
  if (types["generate"].kind != PortJson::NONE &&
      !FunctionName(types, "generate", true, fns, &generate, "types.json", pWhy))
    return false;
  g_files.insert(g_files.end(), files.begin(), files.end());
  g_functions = fns;
  g_unitHooks.insert(g_unitHooks.end(), units.begin(), units.end());
  g_orderSpecs.insert(g_orderSpecs.end(), orders.begin(), orders.end());
  g_gameHooks.insert(g_gameHooks.end(), game.begin(), game.end());
  if (!generate.empty())
    g_generate = {generate, nSub};
  // a push into the game being played, which may have had no scripts before it
  if (g_game.bOn || gwGameMode == 3)
    Build();
  return true;
}

void ModLuaTrim(int nSubs) {
  auto past = [nSubs](int n) { return n > nSubs; };
  g_files.erase(std::remove_if(g_files.begin(), g_files.end(), [&](const File &f) { return past(f.nSub); }), g_files.end());
  g_functions.erase(std::remove_if(g_functions.begin(), g_functions.end(), [&](const Function &f) { return past(f.nSub); }),
                    g_functions.end());
  g_unitHooks.erase(std::remove_if(g_unitHooks.begin(), g_unitHooks.end(), [&](const UnitHooks &h) { return past(h.nSub); }),
                    g_unitHooks.end());
  g_orderSpecs.erase(std::remove_if(g_orderSpecs.begin(), g_orderSpecs.end(), [&](const OrderSpec &o) { return past(o.nSub); }),
                     g_orderSpecs.end());
  g_gameHooks.erase(std::remove_if(g_gameHooks.begin(), g_gameHooks.end(), [&](const GameHooks &h) { return past(h.nSub); }),
                    g_gameHooks.end());
  if (past(g_generate.second))
    g_generate = {std::string(), 0};
  if (g_game.bOn)
    Build();
}

void ModLuaModeForGame(const std::string &id, const std::string &settings, const std::string &run) {
  g_modeId = id;
  g_runJson = run;
  g_settings = PortJson();
  g_bSettings = !settings.empty() && PortJsonParse(settings.c_str(), &g_settings) &&
                g_settings.kind == PortJson::OBJECT;
  if (!g_bSettings)
    g_settings = PortJson();
}

int ModLuaHudCount() {
  return static_cast<int>(g_hud.size());
}

const char *ModLuaHudKey(int i) {
  return i >= 0 && i < static_cast<int>(g_hud.size()) ? g_hud[i].first.c_str() : "";
}

const char *ModLuaHudValue(int i) {
  return i >= 0 && i < static_cast<int>(g_hud.size()) ? g_hud[i].second.c_str() : "";
}

const char *ModLuaHudOf(const char *pszKey) {
  for (const auto &h : g_hud)
    if (h.first == pszKey)
      return h.second.c_str();
  return "";
}

void ModLuaForGame() {
  static bool s_bCommand = false;
  if (!s_bCommand) {
    PortControlCommand("scripts", CmdScripts);
    PortControlCommand("script_call", CmdScriptCall);
    PortControlCommand("script_trace", CmdScriptTrace);
    s_bCommand = true;
  }
  memset(g_lu, 0, sizeof(UnitState) * static_cast<size_t>(std::max(0, std::min(g_pools.units, POOL_UNITS_STORE))));
  memset(g_killedBy, 0, sizeof(DWORD) * static_cast<size_t>(std::max(0, std::min(g_pools.units, POOL_UNITS_STORE))));
  memset(g_events, 0, sizeof g_events);
  memset(g_channels, 0, sizeof g_channels);
  memset(g_reveals, 0, sizeof g_reveals);
  memset(&g_gs, 0, sizeof g_gs);
  memset(&g_dying, 0, sizeof g_dying);
  g_hud.clear();
  PortModeViewClearAll();
  PortMapMarkClearAll();
  g_log.clear();
  g_nErrors = 0;
  g_trace.clear();
  g_ai.clear();
  g_bAiOpen = false;
  g_bTrace = PortCommandOptions()->bModeTrace;
  CloseState();
  g_game = Game();
  const bool bAny = !g_files.empty() || !g_unitHooks.empty() || !g_orderSpecs.empty() || !g_gameHooks.empty();
  if (g_hxModGame && bAny)
    Build();
}

bool ModLuaHasReadout() { return g_LView != nullptr; }

bool ModLuaReadout(CUnit *pUnit, PortJson *pOut, std::string *pWhy) {
  *pOut = PortJson();
  if (g_LView == nullptr || pUnit == nullptr)
    return false;
  lua_State *L = g_LView;
  const int nTop = lua_gettop(L);
  lua_getglobal(L, "readout");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, nTop);
    return false;
  }
  PushUnit(L, static_cast<DWORD>(pUnit->GetUnitID()));
  // a budget of this call's own, so a readout cannot take the game's frame away from it
  const long long nCallWas = g_nCallLeft, nFrameWas = g_nFrameLeft;
  g_nCallLeft = kCallSteps;
  g_nFrameLeft = kCallSteps;
  const int nStatus = lua_pcall(L, 1, 1, 0);
  g_nCallLeft = nCallWas;
  g_nFrameLeft = nFrameWas;
  if (nStatus != 0) {
    if (pWhy != nullptr)
      *pWhy = lua_tostring(L, -1) ? lua_tostring(L, -1) : "readout(unit): an error";
    lua_settop(L, nTop);
    return false;
  }
  if (lua_isnil(L, -1)) {  // classic's own panel for this unit
    lua_settop(L, nTop);
    return false;
  }
  if (!lua_istable(L, -1)) {
    if (pWhy != nullptr)
      *pWhy = "readout(unit) gives a table, or nil for the classic panel";
    lua_settop(L, nTop);
    return false;
  }
  std::string why;
  const bool bOk = ValueToJson(L, lua_gettop(L), pOut, 0, &why);
  lua_settop(L, nTop);
  if (!bOk && pWhy != nullptr)
    *pWhy = "readout(unit): " + why;
  return bOk;
}

// ---- the game's hooks -------------------------------------------------------------------------------------

bool ModLuaOrderRefused(CUnit *pUnit) {
  if (!g_game.bOn || pUnit->bOrderState == 2)
    return false;
  const OrderSpec *sp = SpecOf(pUnit->bOrderId);
  if (sp == nullptr || sp->nCharges == 0)
    return false;
  OrderSlot *o = SlotOf(pUnit, pUnit->bOrderId, true);
  if (o == nullptr || o->bCharges > 0)
    return false;
  static char s_sz[96];
  snprintf(s_sz, sizeof s_sz, "Not ready: %d more seconds", (o->wRecharge + kFps - 1) / kFps);
  gamesnd_error(pUnit, s_sz, pUnit->bPlayer);
  pUnit->EndDispatchOrder();
  return true;
}

void ModLuaOrderBegan(CUnit *pUnit) {
  if (!g_game.bOn)
    return;
  const OrderSpec *sp = SpecOf(pUnit->bOrderId);
  if (sp == nullptr || sp->nCharges == 0)
    return;
  if (OrderSlot *o = SlotOf(pUnit, pUnit->bOrderId, true))
    if (o->bCharges > 0) {
      if (o->bCharges == sp->nCharges)
        o->wRecharge = static_cast<WORD>(std::max(1, sp->nCooldown));
      --o->bCharges;
    }
}

bool ModLuaCast(CUnit *pCaster) {
  if (!g_game.bOn)
    return false;
  const int nOrder = pCaster->bOrderId;
  const OrderSpec *sp = SpecOf(nOrder);
  if (sp == nullptr || (!sp->bToggle && !sp->bChannel))
    return false;
  if (sp->bToggle) {
    OrderSlot *o = SlotOf(pCaster, nOrder, true);
    ToggleSet(pCaster, nOrder, o == nullptr || !(o->bFlags & kOn), R_CAST);
    return true;
  }
  // a channel: its blocks first, if it has any, then held while the caster stands
  ModStatusCast(pCaster);
  const DWORD id = IdOf(pCaster);
  for (int i = 0; i < g_gs.nChannels; ++i)
    if (g_channels[i].dwCaster == id)
      ChannelEnd(i--, R_CAST);
  if (g_gs.nChannels >= kChannelsMax)
    return true;
  Channel &c = g_channels[g_gs.nChannels++];
  memset(&c, 0, sizeof c);
  c.dwCaster = id;
  CUnit *pTarget = pCaster->pOrderTarget;
  c.dwTarget = Alive(pTarget) ? IdOf(pTarget) : 0;
  c.x = Alive(pTarget) ? static_cast<int>(pTarget->pSprite->wPosX) : static_cast<int>(pCaster->wOrderTargetX);
  c.y = Alive(pTarget) ? static_cast<int>(pTarget->pSprite->wPosY) : static_cast<int>(pCaster->wOrderTargetY);
  c.wOrder = static_cast<WORD>(nOrder);
  c.wNext = static_cast<WORD>(std::max(1, sp->nChEvery));
  c.dwLeft = static_cast<DWORD>(sp->nChFrames);
  c.wStandX = pCaster->pSprite->wPosX;
  c.wStandY = pCaster->pSprite->wPosY;
  c.bPlayer = pCaster->bPlayer;
  return true;
}

int ModLuaDamaged(CUnit *pUnit, int nWeapon, CUnit *pSource, int nSourcePlayer, int nDamage) {
  if (!g_game.bOn || g_L == nullptr)
    return nDamage;
  return CallDamaged(pUnit, nWeapon, pSource, nSourcePlayer, nDamage);
}

// A status's on_end, as it runs out or is taken off (port_modstatus.cpp). Not for a unit that
// dies with it: its statuses go with it.
void ModLuaStatusEnded(CUnit *pUnit, const std::string &fn, const std::string &status) {
  if (!g_game.bOn || g_L == nullptr || pUnit == nullptr || fn.empty())
    return;
  CallHook(fn, "a status's on_end", {Unit("unit", IdOf(pUnit)), Str("status", status.c_str())});
}

void ModLuaHit(CUnit *pUnit, int nWeapon, CUnit *pSource, int nSourcePlayer, int nDamage, int nTaken) {
  if (!g_game.bOn || pSource == nullptr)
    return;
  Damaged(pUnit, pSource);
  CUnit *pWhole = Whole(pSource);
  const UnitHooks *h = HooksOf(pSource->wType);
  if ((h == nullptr || h->onHit.empty()) && pWhole != pSource) {
    pSource = pWhole;
    h = HooksOf(pSource->wType);
  }
  if (h == nullptr || h->onHit.empty())
    return;
  Event e;
  memset(&e, 0, sizeof e);
  e.bKind = EV_HIT;
  e.dwUnit = IdOf(pSource);
  e.dwOther = IdOf(pUnit);
  e.wType = pSource->wType;
  e.wWeapon = static_cast<WORD>(nWeapon);
  e.bPlayer = static_cast<BYTE>(pSource ? pSource->bPlayer : nSourcePlayer);
  e.x = pUnit->pSprite->wPosX;
  e.y = pUnit->pSprite->wPosY;
  e.nDamage = nDamage;
  e.nTaken = nTaken;
  QueueEvent(e);
}

void ModLuaDeath(CUnit *pUnit) {
  if (!g_game.bOn || pUnit->pSprite == nullptr)
    return;
  const UnitHooks *h = HooksOf(pUnit->wType);
  if ((h == nullptr || h->onDeath.empty()) && g_game.game.onDeath.empty())
    return;
  Event e;
  memset(&e, 0, sizeof e);
  e.bKind = EV_DEATH;
  e.dwUnit = IdOf(pUnit);
  const int iUnit = IndexOf(pUnit);
  const DWORD dwKiller = iUnit >= 0 && iUnit < std::min(g_pools.units, POOL_UNITS_STORE) ? g_killedBy[iUnit] : 0;
  e.dwOther = UnitOf(dwKiller) != nullptr ? dwKiller : 0;
  e.wType = pUnit->wType;
  e.bPlayer = pUnit->bPlayer;
  e.x = pUnit->pSprite->wPosX;
  e.y = pUnit->pSprite->wPosY;
  QueueEvent(e);
  // what it kept, for hx.get in its death hooks
  const UnitState *st = StateOf(pUnit);
  if (st != nullptr && g_dying.n < static_cast<DWORD>(kDyingMax)) {
    Dying &d = g_dying.a[g_dying.n++];
    d.dwId = e.dwUnit;
    memcpy(d.v, st->v, sizeof d.v);
  }
}

void ModLuaUnitMade(CUnit *pUnit) {
  const int i = IndexOf(pUnit);
  if (g_game.bOn && i >= 0 && i < std::min(g_pools.units, POOL_UNITS_STORE))
    memset(&g_lu[i], 0, sizeof g_lu[i]);
}

void ModLuaBlock(const std::string &function, unsigned dwCaster, unsigned dwTarget, int x, int y, int nOrder, int nPlayer) {
  if (!g_game.bOn)
    return;
  CallHook(function, "block script",
           {Unit("caster", dwCaster), Unit("unit", dwCaster), Unit("target", UnitOf(dwTarget) ? dwTarget : 0), Num("x", x),
            Num("y", y), Num("order", nOrder), Num("player", nPlayer)});
}

// The director's call: the player the mode plays and how it stands, counted here rather than
// walked in Luau every beat. A mode with no player of its own still has the hook, with player
// -1: it is the mode's game master, and what it reads beyond this it reads with the hx calls.
void CallDirector() {
  const int nPlayer = PortModeEnemySlot();
  int nUnits = 0, nBuildings = 0;
  if (nPlayer >= 0)
    for (CUnit *p = CUnit::GetPlayerUnitList(nPlayer); p != nullptr; p = p->pPlayerUnitNext) {
      if (!Alive(p) || (gulUnitTypeFlags[p->wType] & kSubunit))
        continue;
      if (gulUnitTypeFlags[p->wType] & kBuilding)
        ++nBuildings;
      else
        ++nUnits;
    }
  CallHook(g_game.game.director, "game director",
           {Num("frame", g_gs.dwFrame), Num("seconds", static_cast<double>(g_gs.dwFrame) / kFps),
            Num("player", nPlayer), Num("units", nUnits), Num("buildings", nBuildings),
            Num("minerals", nPlayer >= 0 ? gPlayerMinerals[nPlayer] : 0),
            Num("gas", nPlayer >= 0 ? gPlayerGas[nPlayer] : 0)});
}

void ModLuaFrame() {
  if (!g_game.bOn)
    return;
  if (!g_game.bAutocastsBuilt)
    BuildAutocasts();
  if (!g_gs.bStarted) {
    g_gs.bStarted = 1;
    g_gs.wTick = static_cast<WORD>(g_game.game.nEvery);
    CallHook(g_game.game.start, "game start", {Num("frame", g_gs.dwFrame)});
  }
  Events();
  CUnit *pNext = nullptr;
  for (CUnit *p = CUnit::GetUsedList(); p != nullptr; p = pNext) {
    pNext = p->pNext;
    if (!Alive(p) || (gulUnitTypeFlags[p->wType] & kSubunit))
      continue;
    UnitFrame(p);
  }
  Channels();
  if (!g_game.game.tick.empty() && g_gs.wTick > 0 && --g_gs.wTick == 0) {
    g_gs.wTick = static_cast<WORD>(g_game.game.nEvery);
    CallHook(g_game.game.tick, "game tick", {Num("frame", g_gs.dwFrame)});
  }
  // the director's beat is the frame itself, so it needs nothing kept and a loaded game takes it
  // up where the save left it
  if (!g_game.game.director.empty() &&
      g_gs.dwFrame % static_cast<DWORD>(std::max(1, g_game.game.nDirectorEvery)) == 0)
    CallDirector();
  Events();  // what the frame's calls raised
  Reveals();
  ++g_gs.dwFrame;
  g_nFrameLeft = kFrameSteps;
}

void ModLuaTraceSet(bool bOn) {
  g_bTrace = bOn;
  if (!bOn) {
    g_trace.clear();
    g_ai.clear();
    g_bAiOpen = false;
  }
}

bool ModLuaTraceOn() {
  return g_bTrace;
}

int ModLuaAiCallCount() {
  return static_cast<int>(g_ai.size());
}

const ModLuaAiCall &ModLuaAiCallAt(int i) {
  static const ModLuaAiCall s_none;
  return i >= 0 && i < static_cast<int>(g_ai.size()) ? g_ai[static_cast<size_t>(i)] : s_none;
}

void ModLuaAiCallsClear() {
  g_ai.clear();
  g_bAiOpen = false;
}

bool ModLuaModeOwns(const CUnit *pUnit) {
  if (!g_game.bOn || pUnit == nullptr)
    return false;
  const UnitState *st = StateOf(const_cast<CUnit *>(pUnit));
  return st != nullptr && st->bOwn != 0;
}

void ModLuaVisionRefresh() {
  if (!g_game.bOn || g_gs.nReveals == 0 || gpSquareMap == NULL)
    return;
  PlayerSet *pHidden = VisionHxHidden();
  PlayerSet *pUnexplored = VisionHxUnexplored();
  for (int i = 0; i < g_gs.nReveals; ++i) {
    const Reveal &r = g_reveals[i];
    const int t = r.wTiles;
    const int cx = r.x / 32, cy = r.y / 32;
    for (int y = std::max(0, cy - t); y <= std::min(static_cast<int>(gwMapHeight) - 1, cy + t); ++y)
      for (int x = std::max(0, cx - t); x <= std::min(static_cast<int>(gwMapWidth) - 1, cx + t); ++x) {
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > t * t)
          continue;
        const int k = y * gwMapWidth + x;
        for (int s = 0; s < PlayerSlots(); ++s) {
          if ((r.dwPlayers & (1u << (s & 31))) == 0)
            continue;
          if (s < PLAYER_LOBBY_117) {
            gpSquareMap[k] &= ~(0x1u << s);    // seen
            gpSquareMap[k] &= ~(0x100u << s);  // and explored
          } else if (pHidden != NULL && pUnexplored != NULL) {
            pHidden[k].Remove(s);
            pUnexplored[k].Remove(s);
          }
        }
      }
  }
}

// ---- a save --------------------------------------------------------------------------------------------------

// Version 2 adds, after the channels, the places a mode revealed and who damaged each unit
// last; version 3 the units dead this frame with what they kept. An older save reads without
// what it did not keep.
int ModLuaSaveGame(void *pFile) {
  if (!g_game.bOn || !g_hxModGame)
    return 1;
  const DWORD adw[4] = {3, static_cast<DWORD>(g_pools.units), sizeof(UnitState), sizeof(GameState)};
  return PortSaveSection(pFile, kSection) && pfwrite(adw, sizeof adw, pFile) &&
         compress(g_lu, static_cast<int>(sizeof(UnitState) * g_pools.units), pFile) && pfwrite(&g_gs, sizeof g_gs, pFile) &&
         (g_gs.nEvents == 0 || pfwrite(g_events, static_cast<int>(sizeof(Event) * g_gs.nEvents), pFile)) &&
         (g_gs.nChannels == 0 || pfwrite(g_channels, static_cast<int>(sizeof(Channel) * g_gs.nChannels), pFile)) &&
         (g_gs.nReveals == 0 || pfwrite(g_reveals, static_cast<int>(sizeof(Reveal) * g_gs.nReveals), pFile)) &&
         compress(g_killedBy, static_cast<int>(sizeof(DWORD) * g_pools.units), pFile) &&
         pfwrite(&g_dying.n, sizeof g_dying.n, pFile) &&
         (g_dying.n == 0 || pfwrite(g_dying.a, static_cast<int>(sizeof(Dying) * g_dying.n), pFile));
}

int ModLuaLoadGame(void *pFile) {
  const int nHave = PortLoadSectionIf(pFile, kSection);
  if (nHave <= 0)
    return nHave == 0;
  DWORD adw[4];
  if (!pfread(adw, sizeof adw, pFile) || adw[0] < 1 || adw[0] > 3 ||
      adw[1] != static_cast<DWORD>(g_pools.units) || adw[2] != sizeof(UnitState) || adw[3] != sizeof(GameState))
    return 0;
  if (!decompress(g_lu, static_cast<int>(sizeof(UnitState) * g_pools.units), pFile) || !pfread(&g_gs, sizeof g_gs, pFile) ||
      g_gs.nEvents > kEventsMax || g_gs.nChannels > kChannelsMax || g_gs.nReveals > kRevealsMax)
    return 0;
  memset(g_events, 0, sizeof g_events);
  memset(g_channels, 0, sizeof g_channels);
  memset(g_reveals, 0, sizeof g_reveals);
  memset(&g_dying, 0, sizeof g_dying);
  memset(g_killedBy, 0, sizeof(DWORD) * static_cast<size_t>(std::max(0, std::min(g_pools.units, POOL_UNITS_STORE))));
  if (!(g_gs.nEvents == 0 || pfread(g_events, static_cast<int>(sizeof(Event) * g_gs.nEvents), pFile)) ||
      !(g_gs.nChannels == 0 || pfread(g_channels, static_cast<int>(sizeof(Channel) * g_gs.nChannels), pFile)))
    return 0;
  if (adw[0] == 1) {
    g_gs.nReveals = 0;
    return 1;
  }
  if (!(g_gs.nReveals == 0 || pfread(g_reveals, static_cast<int>(sizeof(Reveal) * g_gs.nReveals), pFile)) ||
      !decompress(g_killedBy, static_cast<int>(sizeof(DWORD) * g_pools.units), pFile))
    return 0;
  if (adw[0] == 2)
    return 1;
  if (!pfread(&g_dying.n, sizeof g_dying.n, pFile) || g_dying.n > static_cast<DWORD>(kDyingMax)) {
    g_dying.n = 0;
    return 0;
  }
  return g_dying.n == 0 || pfread(g_dying.a, static_cast<int>(sizeof(Dying) * g_dying.n), pFile);
}
