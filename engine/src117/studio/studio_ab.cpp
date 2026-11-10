// studio_ab.cpp -- fights played out in the canvas game, and A and B: a unit as the mod has it (A)
// and a version of it with some fields changed (B), each fought against the same units from the
// same start with the same seeds, many seeds in a row; win share, time to win, life left and damage
// dealt side by side, and Keep B, which writes B's changes into the mod as one undo group.
//
// Both versions are in the game at once: one push of the working mod with B added as records of
// its own (the unit as <unit>__b, based on the unit, and each weapon B changes as <weapon>__b), so
// no push comes between A's fights and B's and both start from one snapshot. A run starts the
// canvas game again, paused on its first frame, so the start is the same for every run: a field
// that played on carries its unit slots' order and its frame count into what a fight does. Each
// fight puts the start back, seeds the game's generator, spawns the sides and steps the game fast
// until one side has nothing left or the time is up. B lives in the studio until it is kept; the
// mod's files never see the __b records.
//
//   studio_ab_variant {unit, field?, value? | revert?, table?, name?, clear?}   B's changes for a unit
//   studio_ab_run {unit, against?, count?, yours?, units?, seeds?, first_seed?, seconds?, attack?, b?}
//   studio_ab {show?}                               the run: its state, each fight, the sums for A and B
//   studio_ab_stop {}                               the run stopped where it is
//   studio_ab_keep {unit}                           B's changes into the mod, one undo group
//   studio_ab_mark {}, studio_ab_restore {seed}     the run's own steps, in the channel's poll
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <map>
#include <vector>
#include "cursor.h"    // cursor_forget_target
#include "gamemap.h"   // gamemap_force_redraw
#include "net_cmds.h"  // status_set_update
#include "game.h"      // net_srand
#include "snd.h"       // stop_digital
#include "status.h"    // status_get_new_selection
#include "../design/design_engine.h"         // DesignCall
#include "../design/design_hooks.h"          // DesignInGame, DesignGameFrame
#include "../port/hud/port_hudstate.h"       // PortHudRewound
#include "../port/render/port_present.h"     // PortPresentRedraw
#include "../port/replays/port_snap.h"       // PortSnapTake, PortSnapRestore
#include "../port/tools/port_control.h"      // PortControlCommand
#include "studio_ab.h"
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// The snapshots' line the run's start is kept on, apart from the scenarios' (0xD5E1), the
// trainer's and a replay's seek.
const int kAbLine = 0xAB0B;
// Frames a step runs before the field is looked at again: a third of a second at Fastest.
const int kStepFrames = 8;
// Where the sides stand: the unit's at the left, the others at the right, as Try it has them.
const int kLeftX = 960, kRightX = 1180, kY = 1024;

// ---- version B ----------------------------------------------------------------------------------------

struct Change {
  int t = ST_UNITS;
  std::string name;   // the unit, or one of its weapons, by its types.json name
  std::string field;
  PortJson value;
};
std::map<std::string, std::vector<Change>> g_variants;  // by unit
bool g_bShown = false;  // Balance shows the page

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

// What the game spawns a unit by: an edit of a classic row stands in for that row.
std::string SpawnName(const std::string &unit) {
  StudioRecord r;
  if (StudioRecordOf(ST_UNITS, unit, &r) && r.kind == SK_EDIT) {
    const PortJson &e = StudioRecordJson(ST_UNITS, unit)["edit"];
    if (e.kind == PortJson::STRING)
      return e.s;
    const char *psz = StudioClassicName(ST_UNITS, static_cast<int>(e.Num(-1)));
    return psz ? psz : unit;
  }
  return unit;
}

// The record of the mod's that gives a row's values: the row itself when it is a record, else the
// mod's edit of it; "" when the mod has neither.
std::string EditKey(int t, const std::string &name) {
  StudioRecord r;
  if (StudioRecordOf(t, name, &r) && r.kind != SK_CLASSIC)
    return name;
  const int nRow = StudioClassicRow(t, name);
  for (const StudioRecord &e : StudioRecords(t)) {
    if (e.kind != SK_EDIT)
      continue;
    const PortJson &v = StudioRecordJson(t, e.name)["edit"];
    if ((v.kind == PortJson::STRING && v.s == name) || (v.kind == PortJson::NUMBER && static_cast<int>(v.d) == nRow))
      return e.name;
  }
  return std::string();
}

// A field's value as the mod has it now (A's).
double AValue(int t, const std::string &name, const std::string &field) {
  const std::string key = EditKey(t, name);
  return static_cast<double>(StudioNumber(t, key.empty() ? name : key, field, 0));
}

// The records B may change: the unit and the weapons it fires.
std::vector<std::pair<int, std::string>> Changeable(const std::string &unit) {
  std::vector<std::pair<int, std::string>> v = {{ST_UNITS, unit}};
  for (bool bAir : {false, true}) {
    const std::string w = StudioUnitWeapon(unit, bAir);
    if (!w.empty() && std::find(v.begin(), v.end(), std::make_pair(static_cast<int>(ST_WEAPONS), w)) == v.end())
      v.push_back({ST_WEAPONS, w});
  }
  return v;
}

bool NumberField(int t, const std::string &field) {
  const StudioField *f = StudioFieldOf(t, field);
  if (f == nullptr || f->nColumn < 0 || f->nRef >= 0 || f->nCount != 1)
    return false;
  return f->editor == "int" || f->editor == "hp" || f->editor == "frames" || f->editor == "px" || f->editor == "half" ||
         f->editor == "bool" || f->editor.compare(0, 5, "enum:") == 0;
}

bool SetChange(const std::string &unit, int t, const std::string &name, const std::string &field, const PortJson &value,
               bool bRevert, std::string *pWhy) {
  StudioRecord r;
  if (!StudioRecordOf(ST_UNITS, unit, &r)) {
    *pWhy = unit + " is no unit";
    return false;
  }
  const auto ok = Changeable(unit);
  if (std::find(ok.begin(), ok.end(), std::make_pair(t, name)) == ok.end()) {
    *pWhy = std::string(StudioTableName(t)) + " " + name + " is not " + unit + " or a weapon it fires";
    return false;
  }
  if (!NumberField(t, field)) {
    *pWhy = std::string(StudioTableName(t)) + "." + field + " is no number field B can change";
    return false;
  }
  std::vector<Change> &v = g_variants[unit];
  auto it = std::find_if(v.begin(), v.end(), [&](const Change &c) { return c.t == t && c.name == name && c.field == field; });
  if (bRevert) {
    if (it != v.end())
      v.erase(it);
    return true;
  }
  if (value.kind != PortJson::NUMBER) {
    *pWhy = "value takes a number";
    return false;
  }
  if (it == v.end()) {
    v.push_back(Change());
    it = v.end() - 1;
  }
  it->t = t;
  it->name = name;
  it->field = field;
  it->value = value;
  return true;
}

// A record's name in the types the run pushes: name + "__b", or the next free.
std::string FreeName(const PortJson &types, int t, const std::string &name) {
  std::string key = name + "__b";
  for (int i = 2; types[StudioTableName(t)][key.c_str()].kind != PortJson::NONE; ++i)
    key = name + "__b" + std::to_string(i);
  return key;
}

PortJson *Table(PortJson &types, int t) {
  PortJson *p = StudioJsonMember(types, StudioTableName(t));
  if (p == nullptr) {
    StudioJsonPut(types, StudioTableName(t), StudioJsonObject());
    p = StudioJsonMember(types, StudioTableName(t));
  }
  return p;
}

// The working types with B added: its unit, and each weapon it changes, as records of their own.
PortJson TypesWithB(const std::string &unit, const std::vector<Change> &changes, std::string *pB) {
  PortJson types = StudioDoc("types");
  if (types.kind != PortJson::OBJECT)
    types = StudioJsonObject();
  std::map<std::string, std::string> weapons;  // a weapon B changes, to B's record of it
  for (const Change &c : changes)
    if (c.t == ST_WEAPONS && !weapons.count(c.name))
      weapons[c.name] = FreeName(types, ST_WEAPONS, c.name);
  for (const auto &w : weapons) {
    PortJson rec = StudioJsonObject();
    StudioJsonPut(rec, "base", StudioJsonString(w.first));
    for (const Change &c : changes)
      if (c.t == ST_WEAPONS && c.name == w.first)
        StudioJsonPut(rec, c.field, c.value);
    StudioJsonPut(*Table(types, ST_WEAPONS), w.second, rec);
  }
  *pB = FreeName(types, ST_UNITS, unit);
  PortJson rec = StudioJsonObject();
  StudioJsonPut(rec, "base", StudioJsonString(SpawnName(unit)));
  for (const Change &c : changes)
    if (c.t == ST_UNITS)
      StudioJsonPut(rec, c.field, c.value);
  for (bool bAir : {false, true}) {
    const auto it = weapons.find(StudioUnitWeapon(unit, bAir));
    if (it != weapons.end())
      StudioJsonPut(rec, bAir ? "air_weapon" : "ground_weapon", StudioJsonString(it->second));
  }
  StudioJsonPut(*Table(types, ST_UNITS), *pB, rec);
  return types;
}

// ---- the run ------------------------------------------------------------------------------------------

struct Row {
  std::string unit;
  int nCount = 1;
  int nPlayer = 1;
};

struct Fight {
  int nVersion = 0;  // 0 A, 1 B
  int nSeed = 0;
  bool bDone = false;
  int nWinner = -1;  // 0 the unit's side, 1 the other, 2 neither in the time
  unsigned nFrames = 0;
  long long anLife[2][2] = {{0, 0}, {0, 0}};  // each side's life and shields at the start and the end
  int anUnits[2][2] = {{0, 0}, {0, 0}};       // each side's units at the start and the end
  std::string why;
};

struct Run {
  int nGen = 0;
  std::string state;  // "", "starting", "pushing", "playing", "done", "stopped", "refused"
  std::string why;
  std::string unit, spawn[2];
  bool bB = false;
  int nYours = 1;
  bool bYoursAttack = false, bTheirsAttack = true;
  std::vector<Row> rows;
  std::vector<Change> changes;
  int nSeeds = 10, nFirst = 1, nSeconds = 60;
  std::vector<Fight> fights;
  size_t nAt = 0;
  unsigned nBase = 0;
  bool bBase = false;
  unsigned nFightStart = 0;
  DWORD dwAsked = 0;  // when the run last asked the game, for the watchdog
  std::string label;
  std::string types;  // what it pushes
} g_run;

// the page's fight, as the modder sets it, and as the last run had it
std::string g_unit;
int g_nYours = 4;
std::vector<Row> g_rows = {{"zerg_zergling", 6, 1}};
int g_nSeeds = 10, g_nFirst = 1, g_nSeconds = 60;
bool g_bYoursAttack = false, g_bTheirsAttack = true;

bool Going(void) {
  return g_run.state == "starting" || g_run.state == "pushing" || g_run.state == "playing";
}

void Ask(const char *pszCmd, const std::string &fields, DesignAnswerFn fn = DesignAnswerFn()) {
  g_run.dwAsked = GetTickCount();
  DesignCall(pszCmd, fields, fn);
}

bool Ok(const PortJson &a) {
  return a["ok"].kind == PortJson::BOOL && a["ok"].b;
}

void Stop(const std::string &state, const std::string &why) {
  if (!Going())
    return;
  g_run.state = state;
  g_run.why = why;
  ++g_run.nGen;  // the answers still on their way are dropped
  // the canvas game as Try it wants it: going, and Again resuming
  DesignCall("design_scenario", "\"paused\":false");
  if (DesignInGame())
    DesignCall("resume", "");
  StudioLog(state == "done" ? "info" : "warn", "played out: " + (why.empty() ? state : why));
}

void NextFight(int nGen);
void Pushed(int nGen, const PortJson &a);

// Whether a unit of the units answer has shields: a unit without keeps a number there it never uses.
bool HasShields(const PortJson &u) {
  static std::map<std::string, bool> s_known;
  static unsigned s_nChange = ~0u;
  if (s_nChange != StudioChange()) {
    s_known.clear();
    s_nChange = StudioChange();
  }
  std::string name = u["name"].Str();
  if (name.empty()) {
    const char *psz = StudioClassicName(ST_UNITS, static_cast<int>(u["type"].Num(-1)));
    name = psz ? psz : "";
  }
  // B's records are the run's, not the mod's: they have the shields of the unit they are made from
  if (g_run.bB && name == g_run.spawn[1])
    name = g_run.unit;
  const auto it = s_known.find(name);
  if (it != s_known.end())
    return it->second;
  const std::string key = EditKey(ST_UNITS, name);
  const bool b = !name.empty() && StudioNumber(ST_UNITS, key.empty() ? name : key, "shields_enabled", 0) != 0;
  s_known[name] = b;
  return b;
}

// Each side's units and their life and shields, from units' answer.
void Count(const PortJson &a, int anUnits[2], long long anLife[2]) {
  anUnits[0] = anUnits[1] = 0;
  anLife[0] = anLife[1] = 0;
  for (const PortJson &u : a["units"].items) {
    const int p = static_cast<int>(u["player"].Num(-1));
    if (p != 0 && p != 1)
      continue;
    const long long life = static_cast<long long>(u["hp"].Num(0)) + (HasShields(u) ? static_cast<long long>(u["shields"].Num(0)) : 0);
    if (u["hp"].Num(0) <= 0)
      continue;
    ++anUnits[p];
    anLife[p] += life;
  }
}

void Watch(int nGen) {
  Ask("step", "\"frames\":" + std::to_string(kStepFrames) + ",\"fast\":true", [nGen](const PortJson &s) {
    if (nGen != g_run.nGen)
      return;
    if (!Ok(s)) {
      Stop("stopped", "the game would not step: " + s["error"].Str("refused"));
      return;
    }
    Ask("units", "\"max\":2000", [nGen](const PortJson &a) {
      if (nGen != g_run.nGen)
        return;
      Fight &f = g_run.fights[g_run.nAt];
      int an[2];
      long long al[2];
      Count(a, an, al);
      const unsigned nFrame = static_cast<unsigned>(a["frame"].Num(0));
      f.nFrames = nFrame > g_run.nFightStart ? nFrame - g_run.nFightStart : 0;
      const bool bOver = an[0] == 0 || an[1] == 0 || f.nFrames >= static_cast<unsigned>(g_run.nSeconds) * 24u;
      if (!bOver) {
        Watch(nGen);
        return;
      }
      for (int p = 0; p < 2; ++p) {
        f.anUnits[p][1] = an[p];
        f.anLife[p][1] = al[p];
      }
      f.nWinner = an[0] > 0 && an[1] == 0 ? 0 : an[1] > 0 && an[0] == 0 ? 1 : 2;
      f.bDone = true;
      ++g_run.nAt;
      NextFight(nGen);
    });
  });
}

// One fight: the start put back, the generator seeded, the sides spawned, the start counted.
void NextFight(int nGen) {
  if (nGen != g_run.nGen)
    return;
  if (g_run.nAt >= g_run.fights.size()) {
    Stop("done", "");
    return;
  }
  const Fight &f = g_run.fights[g_run.nAt];
  Ask("studio_ab_restore", "\"seed\":" + std::to_string(f.nSeed), [nGen](const PortJson &r) {
    if (nGen != g_run.nGen)
      return;
    if (!Ok(r)) {
      Stop("stopped", "the start could not be put back: " + r["error"].Str("refused"));
      return;
    }
    g_run.nFightStart = static_cast<unsigned>(r["frame"].Num(0));
    const Fight &f = g_run.fights[g_run.nAt];
    Ask("spawn", "\"unit\":" + Q(g_run.spawn[f.nVersion]) + ",\"x\":" + std::to_string(kLeftX) + ",\"y\":" + std::to_string(kY) +
                     ",\"player\":0,\"count\":" + std::to_string(g_run.nYours) +
                     (g_run.bYoursAttack ? ",\"attack\":[" + std::to_string(kRightX) + "," + std::to_string(kY) + "]" : std::string()));
    int nRow[2] = {0, 0};
    for (const Row &s : g_run.rows) {
      const int p = s.nPlayer == 0 ? 0 : 1;
      const int k = nRow[p]++;
      const int x = p == 0 ? kLeftX - 64 : kRightX, y = kY + ((k + 1) / 2) * 56 * (k % 2 ? 1 : -1);
      const bool bAttack = p == 0 ? g_run.bYoursAttack : g_run.bTheirsAttack;
      Ask("spawn", "\"unit\":" + Q(s.unit) + ",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"player\":" +
                       std::to_string(p) + ",\"count\":" + std::to_string(s.nCount) +
                       (bAttack ? ",\"attack\":[" + std::to_string(p == 0 ? kRightX : kLeftX) + "," + std::to_string(kY) + "]" : std::string()));
    }
    Ask("units", "\"max\":2000", [nGen](const PortJson &a) {
      if (nGen != g_run.nGen)
        return;
      Fight &f = g_run.fights[g_run.nAt];
      int an[2];
      long long al[2];
      Count(a, an, al);
      for (int p = 0; p < 2; ++p) {
        f.anUnits[p][0] = an[p];
        f.anLife[p][0] = al[p];
      }
      if (an[0] == 0 || an[1] == 0) {
        Stop("stopped", an[0] == 0 ? g_run.spawn[f.nVersion] + " did not spawn" : "the other side did not spawn");
        return;
      }
      Watch(nGen);
    });
  });
}

// The new game set up, paused on its first frame: the working mod and B pushed into it.
void WaitReady(int nGen) {
  if (nGen != g_run.nGen)
    return;
  Ask("design_status", "", [nGen](const PortJson &s) {
    if (nGen != g_run.nGen)
      return;
    const PortJson &sc = s["scenario"];
    const bool bReady = sc["stage"].Str() == "ready" && sc["its_game"].kind == PortJson::BOOL && sc["its_game"].b &&
                        sc["start"].kind == PortJson::NUMBER && s["game"]["paused"].kind == PortJson::BOOL && s["game"]["paused"].b;
    if (!bReady) {
      WaitReady(nGen);
      return;
    }
    g_run.state = "pushing";
    Ask("mod_push",
        "\"mod\":" + Q(StudioModId()) + ",\"folder\":" + Q(StudioDataFolder()) + ",\"types\":" + g_run.types +
            ",\"iscript\":" + Q(StudioScript()) + ",\"replace\":\"all\"",
        [nGen](const PortJson &a) { Pushed(nGen, a); });
  });
}

// The push answered: the field cleared, the start taken, the first fight.
void Pushed(int nGen, const PortJson &a) {
  if (nGen != g_run.nGen)
    return;
  if (!Ok(a)) {
    g_run.state = "refused";
    g_run.why = a["error"].Str("refused");
    StudioLog("warn", "played out: the push was refused: " + g_run.why);
    return;
  }
  // a unit of the mod's own and B are in the push by name
  for (int v = 0; v < (g_run.bB ? 2 : 1); ++v) {
    StudioRecord r;
    if (v == 0 && !(StudioRecordOf(ST_UNITS, g_run.unit, &r) && r.kind == SK_OWN))
      continue;
    bool bIn = false;
    for (const PortJson &u : a["units"].items)
      bIn = bIn || u["name"].Str() == g_run.spawn[v];
    if (!bIn) {
      g_run.state = "refused";
      g_run.why = g_run.spawn[v] + " was not in the push";
      return;
    }
  }
  g_run.state = "playing";
  Ask("pause", "");
  Ask("clear", "\"player\":0");
  Ask("clear", "\"player\":1");
  Ask("camera", "\"x\":" + std::to_string((kLeftX + kRightX) / 2) + ",\"y\":" + std::to_string(kY));
  Ask("studio_ab_mark", "", [nGen](const PortJson &m) {
    if (nGen != g_run.nGen)
      return;
    if (!Ok(m)) {
      Stop("stopped", "the start could not be taken: " + m["error"].Str("refused"));
      return;
    }
    NextFight(nGen);
  });
}

std::string RowWords(const std::vector<Row> &rows) {
  std::string s;
  for (const Row &r : rows)
    if (r.nPlayer != 0)
      s += (s.empty() ? "" : ", ") + std::to_string(r.nCount) + " " + Short(ST_UNITS, r.unit);
  return s.empty() ? std::string("nothing") : s;
}

bool Start(const std::string &unit, int nYours, const std::vector<Row> &rows, int nSeeds, int nFirst, int nSeconds, bool bYoursAttack,
           bool bTheirsAttack, bool bB, std::string *pWhy) {
  if (Going()) {
    *pWhy = "a run is going: studio_ab_stop first";
    return false;
  }
  if (!StudioIsOpen() || !DesignInGame()) {
    *pWhy = StudioIsOpen() ? "the canvas game is not on yet" : "no mod is open";
    return false;
  }
  StudioRecord r;
  if (unit.empty() || !StudioRecordOf(ST_UNITS, unit, &r)) {
    *pWhy = "unit names no unit";
    return false;
  }
  bool bOther = false;
  for (const Row &row : rows)
    bOther = bOther || row.nPlayer != 0;
  if (!bOther) {
    *pWhy = "nothing to fight: give against, or units with a row of player 1";
    return false;
  }
  const auto it = g_variants.find(unit);
  if (bB && (it == g_variants.end() || it->second.empty())) {
    *pWhy = "B has no changes yet: studio_ab_variant gives them";
    return false;
  }
  const int nGen = g_run.nGen + 1;
  g_run = Run();
  g_run.nGen = nGen;
  g_run.unit = unit;
  g_run.nYours = std::max(1, std::min(64, nYours));
  g_run.rows = rows;
  g_run.bYoursAttack = bYoursAttack;
  g_run.bTheirsAttack = bTheirsAttack;
  g_run.nSeeds = std::max(1, std::min(200, nSeeds));
  g_run.nFirst = nFirst;
  g_run.nSeconds = std::max(5, std::min(600, nSeconds));
  g_run.bB = bB;
  if (bB)
    g_run.changes = it->second;
  g_run.spawn[0] = SpawnName(unit);
  PortJson types = bB ? TypesWithB(unit, g_run.changes, &g_run.spawn[1]) : StudioDoc("types");
  for (int s = 0; s < g_run.nSeeds; ++s)
    for (int v = 0; v < (bB ? 2 : 1); ++v) {
      Fight f;
      f.nVersion = v;
      f.nSeed = nFirst + s;
      g_run.fights.push_back(f);
    }
  g_run.label = std::to_string(g_run.nYours) + " " + Short(ST_UNITS, unit) + " against " + RowWords(rows);
  // the page shows the fight that plays
  g_unit = unit;
  g_nYours = g_run.nYours;
  g_rows = rows;
  g_nSeeds = g_run.nSeeds;
  g_nFirst = nFirst;
  g_nSeconds = g_run.nSeconds;
  g_bYoursAttack = bYoursAttack;
  g_bTheirsAttack = bTheirsAttack;
  g_run.types = PortJsonWrite(types);
  g_run.state = "starting";
  StudioAutosave(true);
  // the canvas game started again on its tileset and seed, paused once its first frame has run
  Ask("design_status", "", [nGen](const PortJson &s) {
    if (nGen != g_run.nGen)
      return;
    const std::string game = PortJsonWrite(s["scenario"]["game"]);
    Ask("design_scenario", "\"new\":true,\"paused\":true,\"launch\":true,\"game\":" + (game == "null" ? std::string("{\"canvas\":\"jungle\"}") : game),
        [nGen](const PortJson &a) {
          if (nGen != g_run.nGen)
            return;
          if (!Ok(a)) {
            Stop("stopped", "the canvas game could not start again: " + a["error"].Str("refused"));
            return;
          }
          WaitReady(nGen);
        });
  });
  StudioLog("info", "playing out " + g_run.label + (bB ? ", A and B" : "") + ", " + std::to_string(g_run.nSeeds) + " seeds");
  return true;
}

// ---- the sums -----------------------------------------------------------------------------------------

struct Sum {
  int nFights = 0, nWins = 0, nLosses = 0, nDraws = 0;
  double dShare = 0.0;     // wins over fights
  double dTimeToWin = 0.0; // seconds, over the wins
  double dTime = 0.0;      // seconds, over every fight
  double dLeft = 0.0;      // the unit's side's life and shields left, share of its start, over every fight
  double dDealt = 0.0;     // the other side's life and shields taken, a fight
  double dTaken = 0.0;     // the unit's side's taken, a fight
  double dUnitsLeft = 0.0; // the unit's side's units left, a fight
};

Sum SumOf(int nVersion) {
  Sum s;
  double dWinTime = 0.0;
  for (const Fight &f : g_run.fights) {
    if (f.nVersion != nVersion || !f.bDone)
      continue;
    ++s.nFights;
    s.nWins += f.nWinner == 0;
    s.nLosses += f.nWinner == 1;
    s.nDraws += f.nWinner == 2;
    const double t = f.nFrames / 24.0;
    s.dTime += t;
    if (f.nWinner == 0)
      dWinTime += t;
    s.dLeft += f.anLife[0][0] > 0 ? static_cast<double>(f.anLife[0][1]) / static_cast<double>(f.anLife[0][0]) : 0.0;
    s.dDealt += static_cast<double>(f.anLife[1][0] - f.anLife[1][1]);
    s.dTaken += static_cast<double>(f.anLife[0][0] - f.anLife[0][1]);
    s.dUnitsLeft += f.anUnits[0][1];
  }
  if (s.nFights > 0) {
    s.dShare = static_cast<double>(s.nWins) / s.nFights;
    s.dTime /= s.nFights;
    s.dLeft /= s.nFights;
    s.dDealt /= s.nFights;
    s.dTaken /= s.nFights;
    s.dUnitsLeft /= s.nFights;
  }
  s.dTimeToWin = s.nWins > 0 ? dWinTime / s.nWins : 0.0;
  return s;
}

std::string Num(double d, int nPlaces = 2) {
  char sz[32];
  snprintf(sz, sizeof sz, "%.*f", nPlaces, d);
  return sz;
}

std::string SumJson(const Sum &s) {
  return "{\"fights\":" + std::to_string(s.nFights) + ",\"wins\":" + std::to_string(s.nWins) + ",\"losses\":" + std::to_string(s.nLosses) +
         ",\"draws\":" + std::to_string(s.nDraws) + ",\"win_share\":" + Num(s.dShare, 3) + ",\"time_to_win\":" + Num(s.dTimeToWin) +
         ",\"time\":" + Num(s.dTime) + ",\"life_left\":" + Num(s.dLeft, 3) + ",\"damage_dealt\":" + Num(s.dDealt, 1) +
         ",\"damage_taken\":" + Num(s.dTaken, 1) + ",\"units_left\":" + Num(s.dUnitsLeft) + "}";
}

const char *WinnerWord(int n) {
  return n == 0 ? "yours" : n == 1 ? "theirs" : n == 2 ? "neither" : "";
}

std::string ChangeJson(const Change &c) {
  return "{\"table\":" + Q(StudioTableName(c.t)) + ",\"name\":" + Q(c.name) + ",\"field\":" + Q(c.field) +
         ",\"a\":" + Num(AValue(c.t, c.name, c.field), 0) + ",\"b\":" + PortJsonWrite(c.value) + "}";
}

std::string VariantJson(const std::string &unit) {
  std::string s;
  const auto it = g_variants.find(unit);
  if (it != g_variants.end())
    for (const Change &c : it->second)
      s += (s.empty() ? "" : ",") + ChangeJson(c);
  return "\"unit\":" + Q(unit) + ",\"changes\":[" + s + "]";
}

std::string RunJson(void) {
  std::string fights;
  for (const Fight &f : g_run.fights) {
    if (!f.bDone)
      continue;
    const double dLeft = f.anLife[0][0] > 0 ? static_cast<double>(f.anLife[0][1]) / f.anLife[0][0] : 0.0;
    fights += std::string(fights.empty() ? "" : ",") + "{\"version\":\"" + (f.nVersion ? "B" : "A") + "\",\"seed\":" +
              std::to_string(f.nSeed) + ",\"winner\":\"" + WinnerWord(f.nWinner) + "\",\"frames\":" + std::to_string(f.nFrames) +
              ",\"seconds\":" + Num(f.nFrames / 24.0) + ",\"units_left\":" + std::to_string(f.anUnits[0][1]) + ",\"their_units_left\":" +
              std::to_string(f.anUnits[1][1]) + ",\"life_left\":" + std::to_string(f.anLife[0][1]) + ",\"life_start\":" +
              std::to_string(f.anLife[0][0]) + ",\"life_left_share\":" + Num(dLeft, 3) + ",\"damage_dealt\":" +
              std::to_string(f.anLife[1][0] - f.anLife[1][1]) + ",\"damage_taken\":" + std::to_string(f.anLife[0][0] - f.anLife[0][1]) + "}";
  }
  std::string changes;
  for (const Change &c : g_run.changes)
    changes += (changes.empty() ? "" : ",") + ChangeJson(c);
  std::string rows;
  for (const Row &r : g_run.rows)
    rows += (rows.empty() ? "" : ",") + std::string("{\"unit\":") + Q(r.unit) + ",\"count\":" + std::to_string(r.nCount) +
            ",\"player\":" + std::to_string(r.nPlayer) + "}";
  size_t nDone = 0;
  for (const Fight &f : g_run.fights)
    nDone += f.bDone;
  std::string s = "\"state\":" + Q(g_run.state) + ",\"why\":" + Q(g_run.why) + ",\"unit\":" + Q(g_run.unit) + ",\"label\":" +
                  Q(g_run.label) + ",\"yours\":" + std::to_string(g_run.nYours) + ",\"units\":[" + rows + "],\"b\":" +
                  (g_run.bB ? "true" : "false") + ",\"spawned\":[" + Q(g_run.spawn[0]) + (g_run.bB ? "," + Q(g_run.spawn[1]) : "") +
                  "],\"seeds\":" + std::to_string(g_run.nSeeds) + ",\"first_seed\":" + std::to_string(g_run.nFirst) +
                  ",\"seconds\":" + std::to_string(g_run.nSeconds) + ",\"fights_done\":" + std::to_string(nDone) +
                  ",\"fights_all\":" + std::to_string(g_run.fights.size()) + ",\"changes\":[" + changes + "],\"fights\":[" + fights +
                  "],\"sums\":{\"A\":" + SumJson(SumOf(0));
  if (g_run.bB)
    s += ",\"B\":" + SumJson(SumOf(1));
  return s + "}";
}

// ---- Keep B ---------------------------------------------------------------------------------------------

// The mod's record to write a change into: its own, its edit of a classic row, or an edit made.
bool KeyFor(int t, const std::string &name, const std::string &sender, std::string *pKey, std::string *pWhy) {
  StudioRecord r;
  if (!StudioRecordOf(t, name, &r)) {
    *pWhy = name + " is no record of " + StudioTableName(t);
    return false;
  }
  if (r.kind == SK_DEPENDENCY) {
    *pWhy = name + " is a mod's this one depends on: change it there";
    return false;
  }
  const std::string edit = EditKey(t, name);
  if (!edit.empty()) {
    *pKey = edit;
    return true;
  }
  std::string key = name + "_edit";
  for (int i = 2; StudioRecordOf(t, key, &r); ++i)
    key = name + "_edit" + std::to_string(i);
  PortJson rec = StudioJsonObject();
  StudioJsonPut(rec, "edit", StudioJsonString(name));
  if (!StudioCreate(t, key, rec, sender, pWhy))
    return false;
  *pKey = key;
  return true;
}

bool Keep(const std::string &unit, const std::string &sender, std::string *pWhy) {
  const auto it = g_variants.find(unit);
  if (it == g_variants.end() || it->second.empty()) {
    *pWhy = "B has no changes to keep";
    return false;
  }
  const int nAt = StudioHistoryAt();
  StudioGroupBegin(sender, "B of " + StudioShowName(ST_UNITS, unit) + " kept");
  bool bOk = true;
  for (const Change &c : it->second) {
    std::string key;
    if (!KeyFor(c.t, c.name, sender, &key, pWhy) || !StudioSetField(c.t, key, c.field, c.value, sender, "", pWhy)) {
      bOk = false;
      break;
    }
  }
  StudioGroupEnd();
  if (!bOk) {
    // the group made so far taken back, so a half kept B leaves nothing behind
    std::string why;
    if (StudioHistoryAt() > nAt)
      StudioUndo(&why);
    return false;
  }
  g_variants.erase(it);
  return true;
}

// ---- the commands -------------------------------------------------------------------------------------

bool CmdVariant(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str();
  StudioRecord r;
  if (!StudioRecordOf(ST_UNITS, unit, &r)) {
    *pAnswer = "unit names no unit";
    return false;
  }
  if (j["clear"].kind == PortJson::BOOL && j["clear"].b)
    g_variants.erase(unit);
  if (j["field"].kind == PortJson::STRING) {
    int t = ST_UNITS;
    if (j["table"].kind == PortJson::STRING && (t = StudioTableOf(j["table"].s)) < 0) {
      *pAnswer = "table names no table";
      return false;
    }
    std::string name = j["name"].Str();
    if (name.empty())
      name = t == ST_UNITS ? unit : StudioUnitWeapon(unit, false);
    if (!SetChange(unit, t, name, j["field"].s, j["value"], j["revert"].kind == PortJson::BOOL && j["revert"].b, pAnswer))
      return false;
  }
  std::string can;
  for (const auto &c : Changeable(unit))
    can += (can.empty() ? "" : ",") + std::string("{\"table\":") + Q(StudioTableName(c.first)) + ",\"name\":" + Q(c.second) + "}";
  *pAnswer = VariantJson(unit) + ",\"records\":[" + can + "]";
  return true;
}

// The fight's rows from a command: against (a unit's name) and count, or units.
bool RowsOf(const PortJson &j, std::vector<Row> *pRows, std::string *pWhy) {
  if (j["units"].kind == PortJson::ARRAY) {
    for (const PortJson &u : j["units"].items) {
      Row r;
      r.unit = u["unit"].Str();
      r.nCount = std::max(1, std::min(64, static_cast<int>(u["count"].Num(1))));
      r.nPlayer = static_cast<int>(u["player"].Num(1)) == 0 ? 0 : 1;
      StudioRecord rec;
      if (!StudioRecordOf(ST_UNITS, r.unit, &rec)) {
        *pWhy = "units: " + r.unit + " is no unit";
        return false;
      }
      r.unit = SpawnName(r.unit);
      pRows->push_back(r);
    }
    return true;
  }
  Row r;
  r.unit = j["against"].Str("zerg_zergling");
  r.nCount = std::max(1, std::min(64, static_cast<int>(j["count"].Num(6))));
  StudioRecord rec;
  if (!StudioRecordOf(ST_UNITS, r.unit, &rec)) {
    *pWhy = "against: " + r.unit + " is no unit";
    return false;
  }
  r.unit = SpawnName(r.unit);
  pRows->push_back(r);
  return true;
}

bool CmdRun(const PortJson &j, std::string *pAnswer) {
  std::vector<Row> rows;
  if (!RowsOf(j, &rows, pAnswer))
    return false;
  const auto Flag = [&](const char *psz, bool bDefault) { return j[psz].kind == PortJson::BOOL ? j[psz].b : bDefault; };
  const std::string unit = j["unit"].Str();
  const bool bB = Flag("b", g_variants.count(unit) && !g_variants[unit].empty());
  if (!Start(unit, static_cast<int>(j["yours"].Num(1)), rows, static_cast<int>(j["seeds"].Num(10)), static_cast<int>(j["first_seed"].Num(1)),
             static_cast<int>(j["seconds"].Num(60)), Flag("attack", false), Flag("theirs_attack", true), bB, pAnswer))
    return false;
  *pAnswer = RunJson();
  return true;
}

bool CmdState(const PortJson &j, std::string *pAnswer) {
  if (j["show"].kind == PortJson::BOOL) {
    g_bShown = j["show"].b;
    if (g_bShown)
      StudioWorkspaceSet(10);
  }
  *pAnswer = RunJson();
  return true;
}

bool CmdStop(const PortJson &, std::string *pAnswer) {
  Stop("stopped", "stopped");
  *pAnswer = RunJson();
  return true;
}

bool CmdKeep(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str(g_run.unit.c_str());
  const std::string before = VariantJson(unit);
  if (!Keep(unit, Sender(j), pAnswer))
    return false;
  *pAnswer = "\"kept\":{" + before + "},\"at\":" + std::to_string(StudioHistoryAt());
  return true;
}

// The run's start: the field as it is now (pushed, cleared, paused) as a snapshot of its own line.
bool CmdMark(const PortJson &, std::string *pAnswer) {
  if (!DesignInGame()) {
    *pAnswer = "no single-player game";
    return false;
  }
  PortSnapDropLine(kAbLine);
  g_run.nBase = DesignGameFrame();
  PortSnapTake(kAbLine, g_run.nBase);
  g_run.bBase = PortSnapHas(kAbLine, g_run.nBase);
  if (!g_run.bBase) {
    *pAnswer = "the snapshot could not be taken";
    return false;
  }
  *pAnswer = "\"start\":" + std::to_string(g_run.nBase);
  return true;
}

// A fight's start: the run's snapshot put back, as Again puts a scenario's, and the game's
// generator seeded, so each fight with a seed plays the same way.
bool CmdRestore(const PortJson &j, std::string *pAnswer) {
  if (!DesignInGame() || !g_run.bBase || !PortSnapHas(kAbLine, g_run.nBase)) {
    *pAnswer = "there is no start to put back";
    return false;
  }
  const unsigned nAt = PortSnapRestore(kAbLine, g_run.nBase, false);
  if (nAt == ~0u) {
    *pAnswer = "the start could not be put back";
    return false;
  }
  status_get_new_selection();
  status_set_update();
  cursor_forget_target();
  PortHudRewound();
  gamemap_force_redraw();
  stop_digital();
  PortPresentRedraw();
  net_srand(static_cast<int>(j["seed"].Num(1)));
  *pAnswer = "\"frame\":" + std::to_string(nAt);
  return true;
}

}  // namespace

void StudioAbTick(void) {
  // a step the modder's input ended never answers: the run stops rather than wait for ever
  if (Going() && g_run.dwAsked != 0 && GetTickCount() - g_run.dwAsked > 15000)
    Stop("stopped", "the game stopped answering (a click in the game ends a step)");
}

void StudioAbInit(void) {
  PortControlCommand("studio_ab_variant", CmdVariant);
  PortControlCommand("studio_ab_run", CmdRun);
  PortControlCommand("studio_ab", CmdState);
  PortControlCommand("studio_ab_stop", CmdStop);
  PortControlCommand("studio_ab_keep", CmdKeep);
  PortControlCommand("studio_ab_mark", CmdMark);
  PortControlCommand("studio_ab_restore", CmdRestore);
}

// ---- the page ---------------------------------------------------------------------------------------------

namespace {

// the change being added: its record and field
int g_nAddRec = 0;
std::string g_addField;

void Heading(ImDrawList *d, float x, float &y, const char *psz) {
  Wide(d, 9.5f, x, y, C(TK_INK3), psz);
  y += P(20.0f);
}

void Label(float x, float y, float h, const std::string &s) {
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, x, Mid(y, h, kStudioFsS), C(TK_LABEL), s);
}

std::string RecordWords(int t, const std::string &name, const std::string &unit) {
  if (t == ST_UNITS)
    return Short(ST_UNITS, name);
  const bool bG = StudioUnitWeapon(unit, false) == name, bA = StudioUnitWeapon(unit, true) == name;
  return Short(ST_WEAPONS, name) + (bG && bA ? " (its weapon)" : bA ? " (air)" : " (ground)");
}

std::string FieldLabel(int t, const std::string &field) {
  const StudioField *f = StudioFieldOf(t, field);
  return f && !f->label.empty() ? f->label : field;
}

// A quick change: a field of the unit or of its weapons made a share bigger, rounded, at least one more.
void Quick(const std::string &unit, int t, const char *pszField, double dBy, double dAdd) {
  std::string why;
  for (const auto &c : Changeable(unit)) {
    if (c.first != t)
      continue;
    const double a = AValue(c.first, c.second, pszField);
    double b = dAdd != 0.0 ? a + dAdd : floor(a * dBy + 0.5);
    if (dAdd == 0.0 && b == a)
      b = a + (dBy > 1.0 ? 1.0 : -1.0);
    if (!SetChange(unit, c.first, c.second, pszField, StudioJsonNumber(std::max(0.0, b)), false, &why))
      StudioSay(why);
  }
}

void FightColumn(ImVec2 a, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  float x = a.x, y = a.y;
  const float h = P(26.0f);
  Heading(d, x, y, "The fight");
  if (g_unit.empty()) {
    for (const StudioRecord &r : StudioRecords(ST_UNITS))
      if (r.kind == SK_OWN && !StudioUnitWeapon(r.name, false).empty()) {
        g_unit = r.name;
        break;
      }
  }
  // the unit and how many
  Label(x, y, h, "Unit");
  ImGui::SetCursorScreenPos(ImVec2(x + P(84.0f), y));
  std::string picked;
  if (StudioRecordPick("##abunit", ST_UNITS, g_unit, (W - P(84.0f) - P(70.0f)) / P(1.0f), &picked, false, 26.0f, "a unit"))
    g_unit = picked;
  ImGui::SetCursorScreenPos(ImVec2(x + W - P(60.0f), y));
  double dn = g_nYours;
  if (StudioNumber("##abyours", &dn, 60.0f, 26.0f, true, 13.0f))
    g_nYours = std::max(1, std::min(64, static_cast<int>(dn)));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How many of it");
  y += h + P(8.0f);
  // what it fights
  for (size_t i = 0; i < g_rows.size() && i < 4; ++i) {
    ImGui::PushID(static_cast<int>(i));
    Row &r = g_rows[i];
    Label(x, y, h, i == 0 ? "Against" : "and");
    ImGui::SetCursorScreenPos(ImVec2(x + P(84.0f), y));
    picked.clear();
    const float pw = W - P(84.0f) - P(70.0f) - (g_rows.size() > 1 ? P(28.0f) : 0.0f);
    if (StudioRecordPick("##abvs", ST_UNITS, r.unit, pw / P(1.0f), &picked, false, 26.0f, "a unit"))
      r.unit = picked;
    ImGui::SetCursorScreenPos(ImVec2(x + P(84.0f) + pw + P(10.0f), y));
    double dc = r.nCount;
    if (StudioNumber("##abvsn", &dc, 60.0f, 26.0f, true, 13.0f))
      r.nCount = std::max(1, std::min(64, static_cast<int>(dc)));
    if (g_rows.size() > 1) {
      ImGui::SetCursorScreenPos(ImVec2(x + W - P(24.0f), y + P(2.0f)));
      if (StudioButton("##abx", "", "x", SB_FLAT, 22.0f, 22.0f)) {
        g_rows.erase(g_rows.begin() + static_cast<long>(i));
        ImGui::PopID();
        break;
      }
    }
    ImGui::PopID();
    y += h + P(6.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(x + P(84.0f), y));
  if (StudioButton("##abaddrow", "Add units", "plus", SB_FLAT, 24.0f, 0.0f, kStudioFsXs, g_rows.size() < 4))
    g_rows.push_back({"terran_marine", 4, 1});
  // the classic tests, a click each
  static const struct {
    const char *pszLabel, *pszUnit;
    int n;
  } kQuick[] = {{"6 Zerglings", "zerg_zergling", 6}, {"4 Zealots", "protoss_zealot", 4}, {"6 Marines", "terran_marine", 6},
                {"1 Ultralisk", "zerg_ultralisk", 1}};
  float qx = ImGui::GetItemRectMax().x + P(10.0f);
  for (const auto &q : kQuick) {
    const float bw = StudioButtonSize(q.pszLabel, nullptr, 24.0f, kStudioFsXs).x;
    if (qx + bw > x + W)
      break;
    ImGui::SetCursorScreenPos(ImVec2(qx, y));
    if (StudioButton((std::string("##abq") + q.pszUnit).c_str(), q.pszLabel, nullptr, SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      g_rows = {{q.pszUnit, q.n, 1}};
    qx += bw + P(4.0f);
  }
  y += P(24.0f) + P(12.0f);
  // who attacks, the seeds, the time
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  StudioCheck("##abya", "Yours walk at them", &g_bYoursAttack);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Off: the unit holds where it stands and fires at what comes in reach");
  ImGui::SetCursorScreenPos(ImVec2(x + W * 0.5f, y));
  StudioCheck("##abta", "Theirs walk at yours", &g_bTheirsAttack);
  y += P(30.0f);
  Label(x, y, h, "Seeds");
  ImGui::SetCursorScreenPos(ImVec2(x + P(84.0f), y));
  double ds = g_nSeeds;
  if (StudioNumber("##abseeds", &ds, 60.0f, 26.0f, true, 13.0f))
    g_nSeeds = std::max(1, std::min(200, static_cast<int>(ds)));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How many fights each version plays, each from the same start with its own seed");
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x + P(154.0f), Mid(y, h, kStudioFsS), C(TK_LABEL), "from");
  ImGui::SetCursorScreenPos(ImVec2(x + P(190.0f), y));
  double df = g_nFirst;
  if (StudioNumber("##abfirst", &df, 60.0f, 26.0f, true, 13.0f))
    g_nFirst = static_cast<int>(df);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The first seed: the same seeds play the same fights again");
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x + P(262.0f), Mid(y, h, kStudioFsS), C(TK_LABEL), "at most");
  ImGui::SetCursorScreenPos(ImVec2(x + P(318.0f), y));
  double dt = g_nSeconds;
  if (StudioNumber("##absecs", &dt, 56.0f, 26.0f, true, 13.0f))
    g_nSeconds = std::max(5, std::min(600, static_cast<int>(dt)));
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x + P(380.0f), Mid(y, h, kStudioFsS), C(TK_LABEL), "s");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A fight with both sides still standing then is no one's");
  y += h + P(20.0f);

  // version B
  Heading(d, x, y, "Version B");
  if (g_unit.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_INK3), "Pick a unit first.", W);
    ImGui::SetCursorScreenPos(ImVec2(x, y + P(24.0f)));
    return;
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y, C(TK_INK3), "A is " + Short(ST_UNITS, g_unit) + " as the mod has it. B is A with these, kept here until you keep it.",
    W);
  y += P(22.0f);
  const auto it = g_variants.find(g_unit);
  int nOut = -1;
  if (it != g_variants.end())
    for (size_t i = 0; i < it->second.size(); ++i) {
      const Change &c = it->second[i];
      ImGui::PushID(static_cast<int>(i) + 50);
      d->AddRectFilled(ImVec2(x, y), ImVec2(x + W, y + P(34.0f)), C(TK_WELL), P(3.0f));
      T(d, STUDIO_FONT_BOLD, kStudioFsS, x + P(8.0f), y + P(2.0f), C(TK_INK), FieldLabel(c.t, c.field), W * 0.45f);
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, x + P(8.0f), y + P(18.0f), C(TK_INK3), RecordWords(c.t, c.name, g_unit), W * 0.45f);
      const double av = AValue(c.t, c.name, c.field);
      const float vx = x + W * 0.5f;
      T(d, STUDIO_FONT_MONO, kStudioFs, vx, Mid(y, P(34.0f), kStudioFs), C(TK_INK2), Fmt(av));
      T(d, STUDIO_FONT_TEXT, kStudioFs, vx + P(48.0f), Mid(y, P(34.0f), kStudioFs), C(TK_INK3), "to");
      ImGui::SetCursorScreenPos(ImVec2(vx + P(72.0f), y + P(4.0f)));
      double bv = c.value.Num(0);
      std::string why;
      if (StudioNumber("##abbv", &bv, 70.0f, 26.0f, true, 13.0f))
        SetChange(g_unit, c.t, c.name, c.field, StudioJsonNumber(bv), false, &why);
      if (av != 0.0) {
        const double pct = (c.value.Num(0) - av) / fabs(av) * 100.0;
        T(d, STUDIO_FONT_MONO, kStudioFsXs, vx + P(150.0f), Mid(y, P(34.0f), kStudioFsXs), pct >= 0 ? C(TK_GOOD) : C(TK_BAD),
          (pct >= 0 ? "+" : "") + Fmt(pct, 0) + "%");
      }
      ImGui::SetCursorScreenPos(ImVec2(x + W - P(28.0f), y + P(6.0f)));
      if (StudioButton("##abcx", "", "x", SB_FLAT, 22.0f, 22.0f))
        nOut = static_cast<int>(i);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Take this change out of B");
      ImGui::PopID();
      y += P(38.0f);
    }
  if (nOut >= 0)
    it->second.erase(it->second.begin() + nOut);
  if (it == g_variants.end() || it->second.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(4.0f), C(TK_INK3), "No changes yet: B is the same as A.", W);
    y += P(28.0f);
  }
  // a change added: a record of the unit's, a field of it
  const auto recs = Changeable(g_unit);
  g_nAddRec = std::min(g_nAddRec, static_cast<int>(recs.size()) - 1);
  const float rw = W * 0.42f;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  const std::string recNow = RecordWords(recs[g_nAddRec].first, recs[g_nAddRec].second, g_unit);
  if (StudioSelectBegin("##abrec", recNow.c_str(), rw / P(1.0f), 26.0f)) {
    for (size_t i = 0; i < recs.size(); ++i)
      if (StudioItem(RecordWords(recs[i].first, recs[i].second, g_unit).c_str(), static_cast<int>(i) == g_nAddRec)) {
        g_nAddRec = static_cast<int>(i);
        g_addField.clear();
      }
    StudioSelectEnd();
  }
  ImGui::SetCursorScreenPos(ImVec2(x + rw + P(6.0f), y));
  const int at = recs[g_nAddRec].first;
  if (StudioSelectBegin("##abfield", "Change a field", (W - rw - P(6.0f)) / P(1.0f), 26.0f)) {
    for (const StudioField &f : StudioFields(at)) {
      if (!NumberField(at, f.key))
        continue;
      const std::string item = f.label + "  (" + Fmt(AValue(at, recs[g_nAddRec].second, f.key)) + ")";
      if (StudioItem(item.c_str())) {
        std::string why;
        SetChange(g_unit, at, recs[g_nAddRec].second, f.key, StudioJsonNumber(AValue(at, recs[g_nAddRec].second, f.key)), false, &why);
      }
    }
    StudioSelectEnd();
  }
  y += h + P(8.0f);
  // the usual changes, a click each
  struct QuickChange {
    const char *pszLabel, *pszTip;
    int t;
    const char *pszField;
    double dBy, dAdd;
  };
  static const QuickChange kQuickB[] = {{"+10% damage", "The weapons' damage a tenth more, to the nearest whole point", ST_WEAPONS, "damage", 1.1, 0.0},
                                        {"+10% life", "Hit points a tenth more", ST_UNITS, "hit_points", 1.1, 0.0},
                                        {"-10% cooldown", "The weapons fire a tenth sooner", ST_WEAPONS, "cooldown", 0.9, 0.0},
                                        {"+1 armour", "One more armour", ST_UNITS, "armor", 1.0, 1.0}};
  float bx = x;
  for (const QuickChange &q : kQuickB) {
    const float bw = StudioButtonSize(q.pszLabel, nullptr, 24.0f, kStudioFsXs).x;
    if (bx + bw > x + W) {
      bx = x;
      y += P(28.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(bx, y));
    if (StudioButton((std::string("##abqb") + q.pszLabel).c_str(), q.pszLabel, nullptr, SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      Quick(g_unit, q.t, q.pszField, q.dBy, q.dAdd);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", q.pszTip);
    bx += bw + P(4.0f);
  }
  y += P(24.0f) + P(18.0f);
  // play, stop, keep
  const bool bGoing = Going();
  const bool bHasB = g_variants.count(g_unit) && !g_variants[g_unit].empty();
  const bool bCan = DesignInGame() && StudioIsOpen() && !bGoing;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  auto play = [&](bool bB) {
    std::string why;
    if (!Start(g_unit, g_nYours, g_rows, g_nSeeds, g_nFirst, g_nSeconds, g_bYoursAttack, g_bTheirsAttack, bB, &why))
      StudioSay(why);
  };
  if (StudioButton("##abplay", "Play A and B", "play", SB_GOOD, 30.0f, 0.0f, 13.0f, bCan && bHasB))
    play(true);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Each seed played twice from the same start, A then B, in the canvas game, as fast as it runs");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##abplaya", "Play A only", "play", SB_PLAIN, 30.0f, 0.0f, kStudioFsS, bCan))
    play(false);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The fight played out over the seeds as the mod has it: its win share and time");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##abstop", "Stop", "pause", SB_PLAIN, 30.0f, 0.0f, kStudioFsS, bGoing)) {
    std::string a;
    CmdStop(PortJson(), &a);
  }
  y += P(38.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (StudioButton("##abkeep", "Keep B", "done", SB_GOLD, 28.0f, 0.0f, kStudioFsS, bHasB && StudioIsOpen())) {
    std::string why;
    if (Keep(g_unit, "you", &why))
      StudioSay("B's changes written into the mod; Ctrl+Z takes them back");
    else
      StudioSay(why);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("B's changes written into the mod's records as one edit, so one undo takes them back");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##abclear", "Clear B", "x", SB_FLAT, 28.0f, 0.0f, kStudioFsS, bHasB))
    g_variants.erase(g_unit);
  ImGui::SetCursorScreenPos(ImVec2(x, y + P(34.0f)));
}

// A version's sums as a card: win share large, then time, life left, damage.
void SumCard(ImDrawList *d, ImVec2 a, ImVec2 b, const char *pszTitle, const std::string &sub, const Sum &s, const Sum *pA) {
  d->AddRectFilled(a, b, C(TK_PANE2), P(4.0f));
  d->AddRect(a, b, C(TK_LINE), P(4.0f));
  const float x = a.x + P(14.0f), w = b.x - a.x - P(28.0f);
  float y = a.y + P(10.0f);
  T(d, STUDIO_FONT_BOLD, 17.0f, x, y, C(TK_HI), pszTitle);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x + P(24.0f), y + P(4.0f), C(TK_INK3), sub, w - P(24.0f));
  y += P(30.0f);
  if (s.nFights == 0) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_INK3), "Not played yet.", w);
    return;
  }
  // the win share, with a bar
  T(d, STUDIO_FONT_MONO, 30.0f, x, y, s.dShare >= 0.5 ? C(TK_GOOD) : C(TK_BAD), Fmt(s.dShare * 100.0, 0) + "%");
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x + P(96.0f), y + P(4.0f), C(TK_INK2), "won " + std::to_string(s.nWins) + " of " + std::to_string(s.nFights));
  if (s.nDraws)
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x + P(96.0f), y + P(22.0f), C(TK_INK3), std::to_string(s.nDraws) + " with both standing at the end");
  if (pA && pA->nFights) {
    const double dd = (s.dShare - pA->dShare) * 100.0;
    const std::string sd = fabs(dd) < 0.5 ? std::string("same share") : (dd > 0 ? "+" : "") + Fmt(dd, 0) + " points";
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x + w - TW(STUDIO_FONT_TEXT, kStudioFsS, sd), y + P(6.0f),
      dd >= 0.5 ? C(TK_GOOD) : dd <= -0.5 ? C(TK_BAD) : C(TK_INK3), sd);
  }
  y += P(42.0f);
  d->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + P(6.0f)), C(TK_WELL), P(3.0f));
  d->AddRectFilled(ImVec2(x, y), ImVec2(x + w * static_cast<float>(s.dShare), y + P(6.0f)), C(TK_GOOD), P(3.0f));
  y += P(16.0f);
  // each line: its words, its value, A's, whether more is better, and how a difference is put
  enum { D_PLAIN, D_SHARE, D_WINS };
  struct Line {
    const char *pszLabel;
    std::string v;
    double dv, da;
    bool bMoreBetter;
    int nKind;
  };
  const Line lines[] = {
      {"Time to win", s.nWins ? Fmt(s.dTimeToWin) + " s" : std::string("never won"), s.dTimeToWin, pA ? pA->dTimeToWin : 0.0, false, D_WINS},
      {"Life left", Fmt(s.dLeft * 100.0, 0) + "%", s.dLeft, pA ? pA->dLeft : 0.0, true, D_SHARE},
      {"Units left", Fmt(s.dUnitsLeft) + " a fight", s.dUnitsLeft, pA ? pA->dUnitsLeft : 0.0, true, D_PLAIN},
      {"Damage dealt", Fmt(s.dDealt, 0) + " a fight", s.dDealt, pA ? pA->dDealt : 0.0, true, D_PLAIN},
      {"Damage taken", Fmt(s.dTaken, 0) + " a fight", s.dTaken, pA ? pA->dTaken : 0.0, false, D_PLAIN},
      {"Time a fight", Fmt(s.dTime) + " s", s.dTime, pA ? pA->dTime : 0.0, false, D_PLAIN},
  };
  for (const Line &l : lines) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_LABEL), l.pszLabel);
    T(d, STUDIO_FONT_MONO, kStudioFsS, x + P(120.0f), y, C(TK_INK), l.v, w - P(200.0f));
    const bool bCompare = pA && pA->nFights && !(l.nKind == D_WINS && (s.nWins == 0 || pA->nWins == 0));
    const double dd = l.dv - l.da;
    if (bCompare && fabs(dd) > 1e-9) {
      const bool bBetter = (dd > 0) == l.bMoreBetter;
      const std::string sd = (dd > 0 ? "+" : "") + (l.nKind == D_SHARE ? Fmt(dd * 100.0, 1) + "%" : Fmt(dd));
      T(d, STUDIO_FONT_MONO, kStudioFsXs, x + w - P(70.0f), y + P(1.0f), bBetter ? C(TK_GOOD) : C(TK_BAD), sd);
    }
    y += P(22.0f);
  }
}

void FightsTable(ImVec2 o, float W, float H) {
  ImGui::SetCursorScreenPos(o);
  const int nCols = g_run.bB ? 4 : 2;
  const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV |
                                ImGuiTableFlags_SizingStretchProp;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(P(6.0f), P(4.0f)));
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, C(TK_ROW));
  if (!ImGui::BeginTable("##abfights", nCols, flags, ImVec2(W, H))) {
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return;
  }
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("seed", ImGuiTableColumnFlags_WidthFixed, P(60.0f));
  ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  if (g_run.bB) {
    ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("diff", ImGuiTableColumnFlags_WidthFixed, P(150.0f));
  }
  ImDrawList *td = ImGui::GetWindowDrawList();
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers, P(26.0f));
  static const char *const kHeads[] = {"Seed", "A", "B", "B against A"};
  for (int i = 0; i < nCols; ++i) {
    ImGui::TableSetColumnIndex(i);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, C(TK_PANE2));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    Wide(td, 9.0f, p.x, p.y + P(4.0f), C(TK_INK3), kHeads[i]);
  }
  // a seed's fights, A's and B's
  std::map<int, const Fight *> fa, fb;
  std::vector<int> seeds;
  for (const Fight &f : g_run.fights) {
    if (std::find(seeds.begin(), seeds.end(), f.nSeed) == seeds.end())
      seeds.push_back(f.nSeed);
    (f.nVersion ? fb : fa)[f.nSeed] = &f;
  }
  auto cell = [&](const Fight *f) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float cw = ImGui::GetContentRegionAvail().x;
    if (f == nullptr || !f->bDone) {
      const bool bNow = f && g_run.nAt < g_run.fights.size() && &g_run.fights[g_run.nAt] == f &&
                        Going();
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x, p.y + P(4.0f), bNow ? C(TK_HI) : C(TK_INK3), bNow ? "playing..." : "to play", cw);
      return;
    }
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, f->nWinner == 0 ? C(TK_GOOD, 0.16f) : f->nWinner == 1 ? C(TK_BAD, 0.16f) : C(TK_WARN, 0.12f));
    const std::string top = f->nWinner == 0 ? "Won in " + Fmt(f->nFrames / 24.0) + " s" : f->nWinner == 1 ? "Lost in " + Fmt(f->nFrames / 24.0) + " s"
                                                                                                           : "Both stand at " + Fmt(f->nFrames / 24.0) + " s";
    T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x, p.y + P(2.0f), C(TK_INK), top, cw);
    const double dLeft = f->anLife[0][0] > 0 ? static_cast<double>(f->anLife[0][1]) / f->anLife[0][0] : 0.0;
    T(td, STUDIO_FONT_MONO, kStudioFsXs, p.x, p.y + P(20.0f), C(TK_INK2),
      std::to_string(f->anUnits[0][1]) + " of " + std::to_string(f->anUnits[0][0]) + " left, " + Fmt(dLeft * 100.0, 0) + "% life; dealt " +
          std::to_string(f->anLife[1][0] - f->anLife[1][1]),
      cw);
  };
  for (int seed : seeds) {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, P(40.0f));
    ImGui::TableSetColumnIndex(0);
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      T(td, STUDIO_FONT_MONO, kStudioFs, p.x, p.y + P(8.0f), C(TK_INK2), std::to_string(seed));
    }
    ImGui::TableSetColumnIndex(1);
    cell(fa.count(seed) ? fa[seed] : nullptr);
    if (g_run.bB) {
      ImGui::TableSetColumnIndex(2);
      cell(fb.count(seed) ? fb[seed] : nullptr);
      ImGui::TableSetColumnIndex(3);
      const Fight *a = fa.count(seed) ? fa[seed] : nullptr, *b = fb.count(seed) ? fb[seed] : nullptr;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (a && b && a->bDone && b->bDone) {
        std::string s;
        ImU32 col = C(TK_INK2);
        if (a->nWinner != b->nWinner) {
          const bool bBetter = b->nWinner == 0 || (b->nWinner == 2 && a->nWinner == 1);
          s = bBetter ? "B turns it" : "B loses it";
          col = bBetter ? C(TK_GOOD) : C(TK_BAD);
        } else {
          const long long dl = b->anLife[0][1] - a->anLife[0][1];
          s = dl == 0 ? "same life left" : (dl > 0 ? "+" : "") + std::to_string(dl) + " life left";
          col = dl > 0 ? C(TK_GOOD) : dl < 0 ? C(TK_BAD) : C(TK_INK3);
        }
        T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x, p.y + P(2.0f), col, s);
        const double dt = (static_cast<double>(b->nFrames) - a->nFrames) / 24.0;
        T(td, STUDIO_FONT_MONO, kStudioFsXs, p.x, p.y + P(20.0f), C(TK_INK3),
          dt == 0.0 ? std::string("as long") : Fmt(fabs(dt)) + " s " + (dt > 0 ? "longer" : "sooner"));
      }
    }
  }
  ImGui::EndTable();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void ResultsColumn(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float x = a.x, W = b.x - a.x;
  float y = a.y;
  Heading(d, x, y, "Played out");
  if (g_run.state.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, x, y + P(4.0f), C(TK_INK3),
      "Set the fight at the left, give B a change or two, and Play A and B. Each seed is fought twice in the canvas game from the "
      "same start, once with A and once with B, so only B's changes tell them apart.",
      W);
    return;
  }
  // the state, and how far it is
  size_t nDone = 0;
  for (const Fight &f : g_run.fights)
    nDone += f.bDone;
  const bool bGoing = Going();
  const int kind = g_run.state == "done" ? CH_GOOD : bGoing ? CH_PLAIN : CH_BAD;
  const float cw = StudioChip(d, ImVec2(x, y + P(1.0f)), g_run.state.c_str(), kind);
  std::string s = g_run.label + ", " + std::to_string(g_run.nSeeds) + (g_run.nSeeds == 1 ? " seed" : " seeds") + " from " +
                  std::to_string(g_run.nFirst) + ": " + std::to_string(nDone) + " of " + std::to_string(g_run.fights.size()) + " fights";
  if (!g_run.why.empty() && g_run.state != "done")
    s += "; " + g_run.why;
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x + cw + P(8.0f), y + P(1.0f), g_run.state == "refused" || g_run.state == "stopped" ? C(TK_BAD) : C(TK_INK2), s,
    W - cw - P(8.0f));
  y += P(24.0f);
  d->AddRectFilled(ImVec2(x, y), ImVec2(x + W, y + P(4.0f)), C(TK_WELL), P(2.0f));
  if (!g_run.fights.empty())
    d->AddRectFilled(ImVec2(x, y), ImVec2(x + W * static_cast<float>(nDone) / g_run.fights.size(), y + P(4.0f)), C(TK_HI), P(2.0f));
  y += P(14.0f);
  // the two versions' sums
  const Sum sa = SumOf(0), sb = SumOf(1);
  const float ch = P(250.0f);
  if (g_run.bB) {
    const float w2 = (W - P(12.0f)) / 2.0f;
    SumCard(d, ImVec2(x, y), ImVec2(x + w2, y + ch), "A", "as the mod has it", sa, nullptr);
    std::string sub;
    for (const Change &c : g_run.changes)
      sub += (sub.empty() ? "" : ", ") + FieldLabel(c.t, c.field) + " " + Fmt(AValue(c.t, c.name, c.field)) + " to " + Fmt(c.value.Num(0));
    SumCard(d, ImVec2(x + w2 + P(12.0f), y), ImVec2(x + W, y + ch), "B", sub, sb, &sa);
  } else {
    SumCard(d, ImVec2(x, y), ImVec2(x + std::min(W, P(520.0f)), y + ch), "A", "as the mod has it", sa, nullptr);
  }
  y += ch + P(12.0f);
  FightsTable(ImVec2(x, y), W, std::max(P(80.0f), b.y - y));
}

}  // namespace

bool StudioAbShown(void) {
  return g_bShown;
}

void StudioAbPages(ImVec2 a, ImVec2 b) {
  static const char *const kPages[] = {"Worked out", "Played out"};
  const float w = StudioSegWidth(kPages, 2);
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(16.0f) - w, a.y + P(12.0f) + (P(30.0f) - P(26.0f)) / 2.0f));
  g_bShown = StudioSeg("##abpages", kPages, 2, g_bShown ? 1 : 0) == 1;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Worked out: every matchup by the numbers.\nPlayed out: a fight played in the canvas game over many seeds, A against B");
}

void StudioAbOpen(const std::string &unit, int nYours, const std::string &unitsJson) {
  g_bShown = true;
  g_unit = unit;
  g_nYours = std::max(1, nYours);
  PortJson j;
  std::string err;
  if (PortJsonParse(unitsJson.c_str(), &j, &err) && j.kind == PortJson::ARRAY) {
    std::vector<Row> rows;
    for (const PortJson &u : j.items)
      rows.push_back({u["unit"].Str(), std::max(1, static_cast<int>(u["count"].Num(1))), static_cast<int>(u["player"].Num(1)) == 0 ? 0 : 1});
    if (!rows.empty())
      g_rows = rows;
  }
  StudioWorkspaceSet(10);
}

void StudioAbWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = b.x - a.x - P(32.0f);
  const ImVec2 o(a.x + P(16.0f), a.y + P(12.0f));
  const float h = P(30.0f);
  Wide(d, 11.0f, o.x, Mid(o.y, h, 11.0f), C(TK_HEAD), "Balance");
  T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + TW(STUDIO_FONT_WIDE, 11.0f, "Balance", 0.1f, true) + P(18.0f), Mid(o.y, h, kStudioFsS), C(TK_INK2),
    "Fights played out in the canvas game: a unit against units over many seeds, and A against B.");
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, o.y + h + P(16.0f), C(TK_INK3), "Open a mod to play its units' fights out.");
    return;
  }
  const float y = o.y + h + P(14.0f);
  const float lw = std::min(P(470.0f), W * 0.38f);
  // the left column scrolls when the window is short
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, 0);
  if (ImGui::BeginChild("##ableft", ImVec2(lw + P(8.0f), b.y - y - P(10.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    FightColumn(ImGui::GetCursorScreenPos(), lw);
    ImGui::Dummy(ImVec2(1, P(8.0f)));
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ResultsColumn(ImVec2(o.x + lw + P(28.0f), y), ImVec2(o.x + W, b.y - P(10.0f)));
}
