// studio_balance.cpp -- the Balance workspace: a matchup table of the mod's units against each other
// and classic ones, worked out by the classic damage rules (studio_calc.cpp). Each cell is the row's
// unit against the column's, one against one or as many of each as a budget buys, fought out frame
// by frame: every unit swings when its cooldown is up, at the other side's first unit standing,
// shields first, then armour and size. Who wins, how much of the winner is left, and the time each
// takes to kill the other. A cell clicked shows the pair and plays the fight in the canvas game
// (studio_try). The numbers are made again when the mod changes.
//
//   studio_matchup {units?, against?, per_cost?, budget?, level?, closing?, show?}   the grid as JSON
//   studio_matchup_against {add? | remove? | reset?, rows?}                            the picked units
#include <algorithm>
#include <climits>
#include <map>
#include <vector>
#include "imgui_internal.h"                // TableGetHoveredRow
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_ab.h"
#include "studio_draw.h"

// The animation player's run of an image's animation (studio_art_anim.cpp, as studio_art_anim answers
// it): each tick's events, an attack an "a".
namespace studio_art {
bool AnimDescribe(const std::string &image, const std::string &anim, int nFacing, std::string *pAnswer);
}

namespace {

using namespace studio_draw;

// ---- a unit's numbers, read once for each change of the mod ---------------------------------------

struct Weapon {
  std::string name;
  long long nDamage = 0, nBonus = 0;
  int nHits = 1;    // hits an attack: the weapon's factor times the swings of the attack's animation
  int nSwings = 1;
  int nType = 3, nCooldown = 0;
  long long nRange = 0;
  bool Can(void) const {
    return !name.empty() && nCooldown > 0 && nDamage > 0;
  }
};

struct Fighter {
  std::string unit;
  std::string show;   // its name as the grid shows it
  StudioKind kind = SK_CLASSIC;
  std::string base;   // what an edit is of, or the unit its own starts from
  long long nLife = 1, nShields = 0, nArmour = 0;
  int nSize = 1;
  bool bFlyer = false, bBuilding = false, bWorker = false, bSubunit = false;
  long long nMinerals = 0, nGas = 0;
  double dCost = 0.0;  // minerals and gas for one, half a pair's for those made two from an egg
  double dSpeed = 0.0; // pixels a second at Fastest
  bool bSpeedGuess = false;  // walked by a script with no moves: the infantry's pace taken
  Weapon ground, air;
  const Weapon &Against(const Fighter &target) const {
    return target.bFlyer ? air : ground;
  }
  bool Fights(void) const {
    return ground.Can() || air.Can();
  }
};

// A unit walked by its image script (movement control 2) whose Walking animation makes no moves;
// the closing in takes it at the classic infantry's pace, 4 pixels a frame.
const int kByScript = 2;
const double kScriptPace = 96.0;

const int kFlagBuilding = 1 << 0, kFlagFlyer = 1 << 2, kFlagWorker = 1 << 3, kFlagSubunit = 1 << 4, kFlagTwoInEgg = 1 << 10;

// The weapons of the open mod that edit a classic row, by that row: a mod's unit fires the edit.
std::map<long long, std::string> g_weaponEdits;

std::string RefOf(int t, const std::string &name, const char *pszKey, int nRef) {
  const long long n = StudioNumber(t, name, pszKey, -1);
  return n < 0 || n == StudioClassicRows(nRef) ? std::string() : StudioRefName(nRef, n);
}

// The swings of a unit's attack: the attacks its image's attack animation makes before it repeats
// (two for the Zealot, which strikes with each blade). One when the animation makes none of its own,
// as when an overlay or a turret fires.
int SwingsOf(const std::string &unit, bool bAir) {
  const std::string flingy = RefOf(ST_UNITS, unit, "flingy", ST_FLINGY);
  const std::string sprite = flingy.empty() ? std::string() : RefOf(ST_FLINGY, flingy, "sprite", ST_SPRITES);
  const std::string image = sprite.empty() ? std::string() : RefOf(ST_SPRITES, sprite, "image", ST_IMAGES);
  std::string out;
  if (image.empty() || !studio_art::AnimDescribe(image, bAir ? "AirAttkRpt" : "GndAttkRpt", 12, &out))
    return 1;
  PortJson j;
  std::string err;
  if (!PortJsonParse(("{" + out + "}").c_str(), &j, &err))
    return 1;
  int n = 0;
  for (const PortJson &k : j["ticks"].items)
    if (k.kind == PortJson::ARRAY && k.items.size() >= 3)
      n += static_cast<int>(std::count(k.items[2].s.begin(), k.items[2].s.end(), 'a'));
  return std::max(1, std::min(8, n));
}

Weapon WeaponOf(const std::string &unit, const char *pszKey, bool bMod) {
  Weapon w;
  StudioValue v;
  long long n = -1;
  std::string firer = unit;
  if (StudioGet(ST_UNITS, unit, pszKey, &v) && !v.n.empty())
    n = v.n[0];
  if (n < 0 || n == StudioClassicRows(ST_WEAPONS)) {
    // a unit that fires from its turret (the Goliath, the Siege Tank)
    const long long nSub = StudioNumber(ST_UNITS, unit, "subunit", -1);
    if (nSub < 0 || nSub == StudioClassicRows(ST_UNITS))
      return w;
    firer = StudioRefName(ST_UNITS, nSub);
    if (!StudioGet(ST_UNITS, firer, pszKey, &v) || v.n.empty() || v.n[0] < 0 || v.n[0] == StudioClassicRows(ST_WEAPONS))
      return w;
    n = v.n[0];
  }
  auto it = g_weaponEdits.find(n);
  w.name = bMod && it != g_weaponEdits.end() ? it->second : StudioRefName(ST_WEAPONS, n);
  w.nDamage = StudioNumber(ST_WEAPONS, w.name, "damage");
  w.nBonus = StudioNumber(ST_WEAPONS, w.name, "damage_bonus");
  w.nSwings = SwingsOf(firer, strcmp(pszKey, "air_weapon") == 0);
  w.nHits = static_cast<int>(std::max(1LL, StudioNumber(ST_WEAPONS, w.name, "damage_factor", 1))) * w.nSwings;
  w.nType = static_cast<int>(StudioNumber(ST_WEAPONS, w.name, "damage_type", 3));
  w.nCooldown = static_cast<int>(StudioNumber(ST_WEAPONS, w.name, "cooldown"));
  w.nRange = StudioNumber(ST_WEAPONS, w.name, "max_range");
  return w;
}

Fighter Read(const std::string &unit) {
  Fighter f;
  f.unit = unit;
  StudioRecord r;
  if (StudioRecordOf(ST_UNITS, unit, &r)) {
    f.kind = r.kind;
    f.base = r.base;
  }
  f.show = Short(ST_UNITS, f.kind == SK_EDIT && !f.base.empty() ? f.base : unit);
  f.nLife = std::max(1LL, StudioNumber(ST_UNITS, unit, "hit_points"));
  f.nShields = StudioNumber(ST_UNITS, unit, "shields_enabled") ? StudioNumber(ST_UNITS, unit, "shields") : 0;
  f.nArmour = StudioNumber(ST_UNITS, unit, "armor");
  f.nSize = static_cast<int>(StudioNumber(ST_UNITS, unit, "size", 1));
  const long long nFlags = StudioNumber(ST_UNITS, unit, "flags");
  f.bFlyer = (nFlags & kFlagFlyer) != 0;
  f.bBuilding = (nFlags & kFlagBuilding) != 0;
  f.bWorker = (nFlags & kFlagWorker) != 0;
  f.bSubunit = (nFlags & kFlagSubunit) != 0;
  f.nMinerals = StudioNumber(ST_UNITS, unit, "minerals");
  f.nGas = StudioNumber(ST_UNITS, unit, "gas");
  f.dCost = static_cast<double>(f.nMinerals + f.nGas) / ((nFlags & kFlagTwoInEgg) ? 2.0 : 1.0);
  f.dSpeed = StudioUnitSpeed(unit);
  const std::string flingy = RefOf(ST_UNITS, unit, "flingy", ST_FLINGY);
  if (f.dSpeed <= 0.0 && !flingy.empty() && StudioNumber(ST_FLINGY, flingy, "movement_control") == kByScript) {
    // walked by its image script, whose Walking animation has no moves to measure
    f.dSpeed = kScriptPace;
    f.bSpeedGuess = true;
  }
  const bool bMod = f.kind != SK_CLASSIC;
  f.ground = WeaponOf(unit, "ground_weapon", bMod);
  f.air = WeaponOf(unit, "air_weapon", bMod);
  return f;
}

unsigned g_nReadAt = ~0u;
std::string g_readMod;
std::map<std::string, Fighter> g_fighters;
std::vector<std::string> g_modUnits;  // the mod's units that fight, its own first, then its edits

void Refresh(void) {
  if (g_nReadAt == StudioChange() && g_readMod == (StudioIsOpen() ? StudioModId() : std::string()))
    return;
  g_nReadAt = StudioChange();
  g_readMod = StudioIsOpen() ? StudioModId() : std::string();
  g_fighters.clear();
  g_modUnits.clear();
  g_weaponEdits.clear();
  if (!StudioIsOpen())
    return;
  for (const StudioRecord &r : StudioRecords(ST_WEAPONS))
    if (r.kind == SK_EDIT && r.nId >= 0)
      g_weaponEdits[r.nId] = r.name;
  std::vector<std::string> edits;
  for (const StudioRecord &r : StudioRecords(ST_UNITS)) {
    if (r.kind != SK_OWN && r.kind != SK_EDIT)
      continue;
    const Fighter f = Read(r.name);
    g_fighters[r.name] = f;
    if (f.Fights() && !f.bBuilding && !f.bWorker && !f.bSubunit)
      (r.kind == SK_OWN ? g_modUnits : edits).push_back(r.name);
  }
  g_modUnits.insert(g_modUnits.end(), edits.begin(), edits.end());
}

const Fighter &FighterOf(const std::string &unit) {
  Refresh();
  auto it = g_fighters.find(unit);
  if (it == g_fighters.end())
    it = g_fighters.emplace(unit, Read(unit)).first;
  return it->second;
}

// ---- the fight ------------------------------------------------------------------------------------

// One side's attack on the other, its numbers ready for StudioStrikeNumbers.
struct Arm {
  bool bCan = false;
  long long nPer = 128;
  int nHits = 1, nType = 3, nShare = 256, nCooldown = 1;
  long long nArmour = 0, nShieldUp = 0;
  int nDelay = 0;  // frames before its first swing, walking in when closing in counts
  long long nRange = 0;
};

Arm ArmOf(const Fighter &a, const Fighter &target, int nLevel) {
  Arm m;
  const Weapon &w = a.Against(target);
  if (!w.Can())
    return m;
  m.bCan = true;
  m.nPer = std::max((w.nDamage + w.nBonus * nLevel) * 256, 128LL);
  m.nHits = w.nHits;
  m.nType = w.nType;
  m.nShare = static_cast<int>(StudioSizeShare(w.nType, target.nSize) * 256.0 + 0.5);
  m.nCooldown = std::max(1, w.nCooldown);
  m.nArmour = (target.nArmour + nLevel) * 256;
  m.nShieldUp = static_cast<long long>(nLevel) * 256;
  m.nRange = w.nRange;
  return m;
}

void Strike(const Arm &m, long long *pnLife, long long *pnShields) {
  StudioStrikeNumbers(m.nPer, m.nHits, m.nType, m.nShare, m.nArmour, m.nShieldUp, pnLife, pnShields);
}

// The attacks one of a takes to kill one of b alone, 0 when it cannot.
int AttacksAlone(const Arm &m, const Fighter &b) {
  if (!m.bCan)
    return 0;
  long long nLife = b.nLife * 256, nShields = b.nShields * 256;
  for (int n = 1; n <= 999; ++n) {
    Strike(m, &nLife, &nShields);
    if (nLife <= 0)
      return n;
  }
  return 0;
}

// What an attack takes off a target's life, its shields gone, after armour and size.
double PerAttack(const Arm &m) {
  if (!m.bCan)
    return 0.0;
  const long long nFull = 1LL << 40;
  long long nLife = nFull, nShields = 0;
  Strike(m, &nLife, &nShields);
  return (nFull - nLife) / 256.0;
}

enum { W_ROW, W_COL, W_BOTH, W_NEITHER };

struct Outcome {
  int nWinner = W_NEITHER;
  double dTime = 0.0;        // seconds until one side is gone
  int anCount[2] = {1, 1};   // how many of each fought
  int anLeft[2] = {0, 0};    // how many stand at the end
  double adShare[2] = {0, 0};  // the side's life and shields left, of what it had
  int anAttacks[2] = {0, 0};   // attacks one of it takes to kill one of the other alone
  double adAlone[2] = {0, 0};  // the seconds that takes, walking in when closing in counts
  double adPerAttack[2] = {0, 0};
  double adDps[2] = {0, 0};
  double adRange[2] = {0, 0};  // tiles
  bool abCan[2] = {false, false};
  double Margin(void) const {
    return nWinner == W_ROW ? adShare[0] : nWinner == W_COL ? adShare[1] : 0.0;
  }
};

const int kMaxSide = 64;
// A weapon that reaches no further than this is melee, and no more than kMeleeReach melee units
// stand round one of the other side's.
const long long kMeleeRange = 32;
const int kMeleeReach = 4;
const int kMaxFrames = 24 * 60 * 10;

Outcome Fight(const Fighter &a, int na, const Fighter &b, int nb, int nLevel, bool bClosing) {
  Outcome o;
  const Fighter *f[2] = {&a, &b};
  int n[2] = {std::max(1, std::min(kMaxSide, na)), std::max(1, std::min(kMaxSide, nb))};
  Arm arm[2] = {ArmOf(a, b, nLevel), ArmOf(b, a, nLevel)};
  // closing in: the side that reaches less far walks the difference before its first swing
  if (bClosing && arm[0].bCan && arm[1].bCan)
    for (int s = 0; s < 2; ++s) {
      const long long nGap = arm[1 - s].nRange - arm[s].nRange;
      if (nGap > 0)
        arm[s].nDelay = f[s]->dSpeed > 0.0 ? static_cast<int>(ceil(nGap / (f[s]->dSpeed / 24.0))) : kMaxFrames + 1;
    }
  for (int s = 0; s < 2; ++s) {
    o.anCount[s] = n[s];
    o.abCan[s] = arm[s].bCan;
    o.anAttacks[s] = AttacksAlone(arm[s], *f[1 - s]);
    o.adAlone[s] = o.anAttacks[s] ? (arm[s].nDelay + (o.anAttacks[s] - 1) * arm[s].nCooldown) / 24.0 : 0.0;
    o.adPerAttack[s] = PerAttack(arm[s]);
    o.adDps[s] = arm[s].bCan ? o.adPerAttack[s] * 24.0 / arm[s].nCooldown : 0.0;
    o.adRange[s] = arm[s].nRange / 32.0;
  }
  std::vector<long long> life[2], shields[2];
  std::vector<int> next[2];
  for (int s = 0; s < 2; ++s) {
    life[s].assign(n[s], f[s]->nLife * 256);
    shields[s].assign(n[s], f[s]->nShields * 256);
    next[s].assign(n[s], arm[s].nDelay);
  }
  int front[2] = {0, 0};
  auto alive = [&](int s) {
    int k = 0;
    for (long long l : life[s])
      k += l > 0;
    return k;
  };
  int nFrame = 0;
  if (arm[0].bCan || arm[1].bCan) {
    for (;;) {
      int nNext = INT_MAX;
      for (int s = 0; s < 2; ++s)
        if (arm[s].bCan)
          for (int i = 0; i < n[s]; ++i)
            if (life[s][i] > 0)
              nNext = std::min(nNext, next[s][i]);
      if (nNext > kMaxFrames)
        break;
      nFrame = nNext;
      // both sides' swings this frame are chosen before either lands, so a unit killed this frame
      // still swings
      // a melee side reaches the other with as many as stand round its units, kMeleeReach each; the
      // rest wait their turn
      std::vector<int> who[2];
      for (int s = 0; s < 2; ++s) {
        if (!arm[s].bCan)
          continue;
        const int nReach = arm[s].nRange <= kMeleeRange ? kMeleeReach * alive(1 - s) : INT_MAX;
        int nEngaged = 0;
        for (int i = 0; i < n[s]; ++i) {
          if (life[s][i] <= 0)
            continue;
          const bool bInReach = nEngaged++ < nReach;
          if (next[s][i] == nFrame) {
            if (bInReach)
              who[s].push_back(i);
            else
              next[s][i] = nFrame + 1;
          }
        }
      }
      for (int s = 0; s < 2; ++s) {
        const int t = 1 - s;
        for (int i : who[s]) {
          while (front[t] < n[t] && life[t][front[t]] <= 0)
            ++front[t];
          if (front[t] < n[t])
            Strike(arm[s], &life[t][front[t]], &shields[t][front[t]]);
          next[s][i] += arm[s].nCooldown;
        }
      }
      if (alive(0) == 0 || alive(1) == 0)
        break;
    }
  }
  o.dTime = nFrame / 24.0;
  for (int s = 0; s < 2; ++s) {
    o.anLeft[s] = alive(s);
    double dLeft = 0.0;
    for (int i = 0; i < n[s]; ++i)
      dLeft += static_cast<double>(std::max(0LL, life[s][i]) + std::max(0LL, shields[s][i]));
    o.adShare[s] = dLeft / (static_cast<double>(f[s]->nLife + f[s]->nShields) * 256.0 * n[s]);
  }
  if (o.anLeft[0] == 0 && o.anLeft[1] == 0)
    o.nWinner = W_BOTH;
  else if (o.anLeft[1] == 0)
    o.nWinner = W_ROW;
  else if (o.anLeft[0] == 0)
    o.nWinner = W_COL;
  else
    o.nWinner = W_NEITHER;
  return o;
}

// ---- the grid -------------------------------------------------------------------------------------

const int kBudgets[] = {600, 1200, 2400};
const char *const kDefaultAgainst[] = {"protoss_zealot", "protoss_dragoon", "terran_marine",
                                       "terran_firebat", "zerg_zergling",   "zerg_hydralisk"};

struct Settings {
  bool bPerCost = false;
  int nBudget = 1200;
  int nLevel = 0;  // both sides' upgrades, 0 or 3
  bool bClosing = false;
};

Settings g_set;
std::vector<std::string> g_against(std::begin(kDefaultAgainst), std::end(kDefaultAgainst));  // the picked columns
std::vector<std::string> g_rowsPicked;                                                       // the picked rows
std::string g_selRow, g_selCol;

int CountFor(const Fighter &f, const Settings &s) {
  if (!s.bPerCost)
    return 1;
  if (f.dCost <= 0.0)
    return 1;
  return std::max(1, std::min(kMaxSide, static_cast<int>(s.nBudget / f.dCost)));
}

struct Grid {
  Settings set;
  std::vector<std::string> rows, cols;
  std::vector<Outcome> cells;  // rows by columns
  const Outcome &At(size_t r, size_t c) const {
    return cells[r * cols.size() + c];
  }
};

void AddOnce(std::vector<std::string> *pv, const std::string &s) {
  if (std::find(pv->begin(), pv->end(), s) == pv->end())
    pv->push_back(s);
}

std::vector<std::string> RowsNow(void) {
  Refresh();
  std::vector<std::string> v = g_modUnits;
  for (const std::string &s : g_rowsPicked)
    AddOnce(&v, s);
  return v;
}

std::vector<std::string> ColsNow(void) {
  Refresh();
  std::vector<std::string> v = g_modUnits;
  for (const std::string &s : g_against)
    AddOnce(&v, s);
  return v;
}

void Work(Grid *pg) {
  pg->cells.clear();
  for (const std::string &r : pg->rows) {
    const Fighter &a = FighterOf(r);
    for (const std::string &c : pg->cols) {
      const Fighter &b = FighterOf(c);
      pg->cells.push_back(Fight(a, CountFor(a, pg->set), b, CountFor(b, pg->set), pg->set.nLevel, pg->set.bClosing));
    }
  }
}

// The grid the workspace shows, worked out again when the mod, the units or the settings change.
const Grid &Shown(void) {
  static Grid s_grid;
  static unsigned s_nChange = ~0u;
  static std::string s_mod;
  Refresh();
  const std::vector<std::string> rows = RowsNow(), cols = ColsNow();
  const Settings &s = g_set;
  if (s_nChange != StudioChange() || s_mod != g_readMod || rows != s_grid.rows || cols != s_grid.cols ||
      s.bPerCost != s_grid.set.bPerCost || s.nBudget != s_grid.set.nBudget || s.nLevel != s_grid.set.nLevel ||
      s.bClosing != s_grid.set.bClosing) {
    s_nChange = StudioChange();
    s_mod = g_readMod;
    s_grid.rows = rows;
    s_grid.cols = cols;
    s_grid.set = s;
    Work(&s_grid);
  }
  return s_grid;
}

// What a unit's weapon does a second against each size at armour 0.
void SizeDps(const Fighter &f, int nLevel, double ad[3]) {
  const Weapon &w = f.ground.Can() ? f.ground : f.air;
  for (int nSize = 1; nSize <= 3; ++nSize) {
    ad[nSize - 1] = 0.0;
    if (!w.Can())
      continue;
    Arm m;
    m.bCan = true;
    m.nPer = std::max((w.nDamage + w.nBonus * nLevel) * 256, 128LL);
    m.nHits = w.nHits;
    m.nType = w.nType;
    m.nShare = static_cast<int>(StudioSizeShare(w.nType, nSize) * 256.0 + 0.5);
    m.nCooldown = std::max(1, w.nCooldown);
    ad[nSize - 1] = PerAttack(m) * 24.0 / m.nCooldown;
  }
}

double LifeForCost(const Fighter &f) {
  return f.dCost > 0.0 ? (f.nLife + f.nShields) * 100.0 / f.dCost : 0.0;
}

const char *SizeWord(int nSize) {
  static const char *const kSize[] = {"independent", "small", "medium", "large"};
  return kSize[std::max(0, std::min(3, nSize))];
}

std::string Pct(double d) {
  return std::to_string(static_cast<int>(d * 100.0 + 0.5)) + "%";
}

std::string Secs(double d) {
  return Fmt(d, d < 10.0 ? 1 : 0) + " s";
}

std::string KindWords(const Fighter &f) {
  switch (f.kind) {
  case SK_OWN: return f.base.empty() ? "the mod's own" : "the mod's, from the " + Short(ST_UNITS, f.base);
  case SK_EDIT: return "the mod's edit";
  case SK_DEPENDENCY: return "a dependency's";
  default: return "classic";
  }
}

// The fight in a sentence, from the row's side.
std::string Sentence(const Grid &g, size_t r, size_t c) {
  const Fighter &a = FighterOf(g.rows[r]), &b = FighterOf(g.cols[c]);
  const Outcome &o = g.At(r, c);
  auto side = [&](int s) {
    const Fighter &f = s == 0 ? a : b;
    return o.anCount[s] == 1 ? "the " + f.show : std::to_string(o.anCount[s]) + " " + f.show + (f.show.back() == 's' ? "" : "s");
  };
  std::string s;
  switch (o.nWinner) {
  case W_ROW:
  case W_COL: {
    const int w = o.nWinner == W_ROW ? 0 : 1;
    s = side(w) + " win" + (o.anCount[w] == 1 ? "s" : "") + " in " + Secs(o.dTime) + ", " + Pct(o.adShare[w]) + " of " +
        (o.anCount[w] == 1 ? "its" : "their") + " life and shields left";
    if (o.anCount[w] > 1)
      s += ", " + std::to_string(o.anLeft[w]) + " standing";
    break;
  }
  case W_BOTH: s = "Both fall together, in " + Secs(o.dTime); break;
  default:
    s = !o.abCan[0] && !o.abCan[1] ? "Neither can hurt the other" : "Neither falls in ten minutes";
    break;
  }
  s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
  return s + ".";
}

// ---- the commands' JSON ---------------------------------------------------------------------------

std::string Num(double d) {
  char sz[32];
  snprintf(sz, sizeof sz, "%.3f", d);
  std::string s = sz;
  while (!s.empty() && s.back() == '0')
    s.pop_back();
  if (!s.empty() && s.back() == '.')
    s.pop_back();
  return s.empty() || s == "-" ? "0" : s;
}

std::string GridJson(const Grid &g) {
  static const char *const kWinner[] = {"unit", "against", "both", "neither"};
  std::string rows, cols, cells;
  std::vector<int> beats(g.cols.size(), 0);
  for (size_t r = 0; r < g.rows.size(); ++r) {
    const Fighter &a = FighterOf(g.rows[r]);
    int nWins = 0;
    for (size_t c = 0; c < g.cols.size(); ++c) {
      const Outcome &o = g.At(r, c);
      nWins += o.nWinner == W_ROW;
      beats[c] += o.nWinner == W_COL;
      cells += std::string(cells.empty() ? "" : ",") + "{\"unit\":" + Q(g.rows[r]) + ",\"against\":" + Q(g.cols[c]) +
               ",\"winner\":" + Q(kWinner[o.nWinner]) + ",\"margin\":" + Num(o.Margin()) + ",\"time\":" + Num(o.dTime) +
               ",\"count\":" + std::to_string(o.anCount[0]) + ",\"against_count\":" + std::to_string(o.anCount[1]) +
               ",\"left\":" + std::to_string(o.anLeft[0]) + ",\"against_left\":" + std::to_string(o.anLeft[1]) +
               ",\"attacks\":" + std::to_string(o.anAttacks[0]) + ",\"against_attacks\":" + std::to_string(o.anAttacks[1]) +
               ",\"time_to_kill\":" + Num(o.adAlone[0]) + ",\"against_time_to_kill\":" + Num(o.adAlone[1]) +
               ",\"dps\":" + Num(o.adDps[0]) + ",\"against_dps\":" + Num(o.adDps[1]) + "}";
    }
    double ad[3];
    SizeDps(a, g.set.nLevel, ad);
    rows += std::string(rows.empty() ? "" : ",") + "{\"unit\":" + Q(g.rows[r]) + ",\"show\":" + Q(a.show) + ",\"kind\":" +
            Q(KindWords(a)) + ",\"wins\":" + std::to_string(nWins) + ",\"of\":" + std::to_string(g.cols.size()) +
            ",\"cost\":" + Num(a.dCost) + ",\"speed\":" + Num(a.dSpeed) + ",\"life_per_cost\":" + Num(LifeForCost(a)) + ",\"dps\":[" + Num(ad[0]) + "," +
            Num(ad[1]) + "," + Num(ad[2]) + "]}";
  }
  for (size_t c = 0; c < g.cols.size(); ++c) {
    const Fighter &b = FighterOf(g.cols[c]);
    const bool bPicked = std::find(g_against.begin(), g_against.end(), g.cols[c]) != g_against.end();
    cols += std::string(cols.empty() ? "" : ",") + "{\"unit\":" + Q(g.cols[c]) + ",\"show\":" + Q(b.show) + ",\"classic\":" +
            (b.kind == SK_CLASSIC ? "true" : "false") + ",\"picked\":" + (bPicked ? "true" : "false") + ",\"beats\":" +
            std::to_string(beats[c]) + "}";
  }
  return "\"per_cost\":" + std::string(g.set.bPerCost ? "true" : "false") + ",\"budget\":" + std::to_string(g.set.nBudget) +
         ",\"level\":" + std::to_string(g.set.nLevel) + ",\"closing\":" + (g.set.bClosing ? "true" : "false") + ",\"rows\":[" +
         rows + "],\"columns\":[" + cols + "],\"cells\":[" + cells + "]";
}

// ---- drawing --------------------------------------------------------------------------------------

ImU32 WinColour(int nWinner, double dMargin, float fBase = 0.16f) {
  const float a = fBase + 0.42f * static_cast<float>(std::max(0.0, std::min(1.0, dMargin)));
  switch (nWinner) {
  case W_ROW: return C(TK_GOOD, a);
  case W_COL: return C(TK_BAD, a);
  case W_BOTH: return C(TK_WARN, 0.18f);
  default: return C(TK_WELL);
  }
}

// A unit's picture, or its initials in a box when it has none to draw.
void Picture(ImDrawList *d, ImVec2 a, ImVec2 b, const Fighter &f, int nFacing, int nColour) {
  d->AddRectFilled(a, b, C(TK_WELL), P(3.0f));
  if (!StudioUnitPicture(d, a, b, f.unit, nFacing, nColour)) {
    const std::string s = f.show.substr(0, 2);
    const float tw = TW(STUDIO_FONT_BOLD, kStudioFsS, s);
    T(d, STUDIO_FONT_BOLD, kStudioFsS, floorf((a.x + b.x - tw) / 2.0f), Mid(a.y, b.y - a.y, kStudioFsS), C(TK_INK3), s);
  }
  d->AddRect(a, b, C(TK_LINE), P(3.0f));
}

void Remove(std::vector<std::string> *pv, const std::string &s) {
  pv->erase(std::remove(pv->begin(), pv->end(), s), pv->end());
}

float Bar(ImDrawList *d, ImVec2 o, float W) {
  const float h = P(30.0f);
  float x = o.x;
  Wide(d, 11.0f, x, Mid(o.y, h, 11.0f), C(TK_HEAD), "Balance");
  x += TW(STUDIO_FONT_WIDE, 11.0f, "Balance", 0.1f, true) + P(18.0f);
  static const char *const kMode[] = {"One against one", "For the same cost"};
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  g_set.bPerCost = StudioSeg("##mode", kMode, 2, g_set.bPerCost ? 1 : 0) == 1;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("One of each, or as many of each as the budget buys (minerals and gas alike)");
  x += StudioSegWidth(kMode, 2) + P(10.0f);
  if (g_set.bPerCost) {
    static const char *const kBudget[] = {"600", "1200", "2400"};
    int nSel = 1;
    for (int i = 0; i < 3; ++i)
      if (kBudgets[i] == g_set.nBudget)
        nSel = i;
    ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
    g_set.nBudget = kBudgets[StudioSeg("##budget", kBudget, 3, nSel)];
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("What each side spends: as many as it buys, %d at most", kMaxSide);
    x += StudioSegWidth(kBudget, 3) + P(10.0f);
  }
  static const char *const kLevel[] = {"No upgrades", "Upgrades at 3"};
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  g_set.nLevel = StudioSeg("##level", kLevel, 2, g_set.nLevel ? 1 : 0) == 1 ? 3 : 0;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Both sides' weapons, armour and shields upgraded alike");
  x += StudioSegWidth(kLevel, 2) + P(14.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  StudioCheck("##closing", "Closing in", &g_set.bClosing);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The side that reaches less far walks the difference in range before its first swing,\n"
                      "at its top speed (for a unit its walking animation moves, that animation's speed);\n"
                      "off, both start in reach");
  (void)W;
  return h;
}

// The pickers and the defaults, at the right of the second line.
void Pickers(ImVec2 o, float W, float h) {
  const float pw = 170.0f;
  float x = o.x + W - P(pw) * 2.0f - P(8.0f) - StudioButtonSize("Defaults", "again", 26.0f).x - P(8.0f);
  std::string picked;
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  if (StudioRecordPick("##addcol", ST_UNITS, "", pw, &picked, false, 26.0f, "Add a column") && !picked.empty())
    AddOnce(&g_against, picked);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A unit to fight against, as a column");
  x += P(pw) + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  picked.clear();
  if (StudioRecordPick("##addrow", ST_UNITS, "", pw, &picked, false, 26.0f, "Add a row") && !picked.empty())
    AddOnce(&g_rowsPicked, picked);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A unit to see against every column, as a row");
  x += P(pw) + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  if (StudioButton("##defaults", "Defaults", "again", SB_FLAT, 26.0f)) {
    g_against.assign(std::begin(kDefaultAgainst), std::end(kDefaultAgainst));
    g_rowsPicked.clear();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The columns back to two units of each race, the picked rows gone");
}

void GridTable(const Grid &g, ImVec2 o, float W, float H) {
  const size_t nRows = g.rows.size(), nCols = g.cols.size();
  const float rowH = P(48.0f), headH = P(62.0f), cellW = P(112.0f);
  ImGui::SetCursorScreenPos(o);
  const ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(P(4.0f), P(4.0f)));
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, C(TK_ROW));
  ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, C(TK_LINE));
  if (!ImGui::BeginTable("##matchups", static_cast<int>(nCols) + 4, flags, ImVec2(W, H))) {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    return;
  }
  ImGui::TableSetupScrollFreeze(2, 1);
  ImGui::TableSetupColumn("unit", ImGuiTableColumnFlags_WidthFixed, P(196.0f));
  ImGui::TableSetupColumn("wins", ImGuiTableColumnFlags_WidthFixed, P(58.0f));
  for (size_t c = 0; c < nCols; ++c)
    ImGui::TableSetupColumn(g.cols[c].c_str(), ImGuiTableColumnFlags_WidthFixed, cellW);
  ImGui::TableSetupColumn("life", ImGuiTableColumnFlags_WidthFixed, P(84.0f));
  ImGui::TableSetupColumn("dps", ImGuiTableColumnFlags_WidthFixed, P(136.0f));
  ImDrawList *td = ImGui::GetWindowDrawList();
  // the head: each column's unit, its picture, where it comes from, and a picked one's x
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers, headH);
  for (int i = 0; i < static_cast<int>(nCols) + 4; ++i) {
    ImGui::TableSetColumnIndex(i);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, C(TK_PANE2));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float cw = ImGui::GetContentRegionAvail().x;
    if (i == 0) {
      Wide(td, 9.0f, p.x + P(4.0f), p.y + P(6.0f), C(TK_INK3), "Row against column");
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(4.0f), p.y + P(24.0f), C(TK_INK3),
        g.set.bPerCost ? "as many as " + std::to_string(g.set.nBudget) + " buys" : "one against one", cw - P(8.0f));
      continue;
    }
    if (i == 1) {
      Wide(td, 9.0f, p.x + P(2.0f), p.y + P(6.0f), C(TK_INK3), "Wins");
      continue;
    }
    if (i == static_cast<int>(nCols) + 2) {
      Wide(td, 9.0f, p.x + P(2.0f), p.y + P(6.0f), C(TK_INK3), "Life");
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(2.0f), p.y + P(24.0f), C(TK_INK3), "for 100 spent", cw);
      continue;
    }
    if (i == static_cast<int>(nCols) + 3) {
      Wide(td, 9.0f, p.x + P(2.0f), p.y + P(6.0f), C(TK_INK3), "A second");
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(2.0f), p.y + P(24.0f), C(TK_INK3), "small / medium / large", cw);
      continue;
    }
    const size_t c = static_cast<size_t>(i - 2);
    const Fighter &b = FighterOf(g.cols[c]);
    const bool bSel = g_selCol == g.cols[c];
    Picture(td, ImVec2(p.x, p.y + P(2.0f)), ImVec2(p.x + P(30.0f), p.y + P(32.0f)), b, 24, 1);
    T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x + P(36.0f), p.y + P(2.0f), bSel ? C(TK_HI) : C(TK_INK), b.show, cw - P(36.0f));
    T(td, STUDIO_FONT_MONO, kStudioFsXs, p.x + P(36.0f), p.y + P(19.0f), C(TK_INK3),
      Fmt(b.dCost) + (g.set.bPerCost ? " x" + std::to_string(CountFor(b, g.set)) : std::string()), cw - P(36.0f));
    StudioChip(td, ImVec2(p.x, p.y + P(38.0f)), b.kind == SK_CLASSIC ? "classic" : "mod", b.kind == SK_CLASSIC ? CH_GOLD : CH_GOOD, 16.0f,
               8.5f);
    ImGui::PushID(static_cast<int>(c));
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##colhead", ImVec2(cw, headH - P(8.0f)));
    const bool bHover = ImGui::IsItemHovered();
    if (bHover)
      ImGui::SetTooltip("%s: %lld life, %lld shields, armour %lld, %s; %s\n%s", StudioShowName(ST_UNITS, b.unit).c_str(), b.nLife,
                        b.nShields, b.nArmour, SizeWord(b.nSize), KindWords(b).c_str(),
                        std::find(g_against.begin(), g_against.end(), b.unit) != g_against.end() ? "Its x takes the column away"
                                                                                                : "The mod's units are always here");
    if (std::find(g_against.begin(), g_against.end(), b.unit) != g_against.end() && (bHover || ImGui::IsItemActive())) {
      ImGui::SetCursorScreenPos(ImVec2(p.x + cw - P(20.0f), p.y + P(36.0f)));
      if (StudioButton("##x", "", "x", SB_FLAT, 20.0f, 20.0f))
        Remove(&g_against, b.unit);
    }
    ImGui::PopID();
  }
  // the rows
  std::vector<int> beats(nCols, 0);
  for (size_t r = 0; r < nRows; ++r)
    for (size_t c = 0; c < nCols; ++c)
      beats[c] += g.At(r, c).nWinner == W_COL;
  for (size_t r = 0; r < nRows; ++r) {
    const Fighter &a = FighterOf(g.rows[r]);
    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
    ImGui::PushID(static_cast<int>(r));
    const bool bRowSel = g_selRow == g.rows[r];
    // the unit
    ImGui::TableSetColumnIndex(0);
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float cw = ImGui::GetContentRegionAvail().x;
      ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, bRowSel ? C(TK_SEL) : C(TK_PANE));
      if (a.kind != SK_CLASSIC)
        td->AddRectFilled(ImVec2(p.x - P(4.0f), p.y - P(4.0f)), ImVec2(p.x - P(1.0f), p.y + rowH - P(4.0f)), C(TK_MOD));
      Picture(td, ImVec2(p.x + P(2.0f), p.y + P(2.0f)), ImVec2(p.x + P(38.0f), p.y + P(38.0f)), a, 8, 0);
      ImGui::InvisibleButton("##open", ImVec2(cw, rowH - P(8.0f)));
      const bool bHover = ImGui::IsItemHovered();
      if (ImGui::IsItemClicked())
        StudioOpenDoc(ST_UNITS, a.unit, 0);
      if (bHover)
        ImGui::SetTooltip("Open %s", StudioShowName(ST_UNITS, a.unit).c_str());
      T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x + P(46.0f), p.y + P(3.0f), bHover ? C(TK_HI) : C(TK_INK), a.show, cw - P(70.0f));
      std::string sub = KindWords(a) + ", " + Fmt(a.dCost);
      if (g.set.bPerCost)
        sub += " x" + std::to_string(CountFor(a, g.set));
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(46.0f), p.y + P(21.0f), C(TK_INK3), sub, cw - P(50.0f));
      const bool bPicked = std::find(g_rowsPicked.begin(), g_rowsPicked.end(), a.unit) != g_rowsPicked.end() &&
                           std::find(g_modUnits.begin(), g_modUnits.end(), a.unit) == g_modUnits.end();
      if (bPicked && ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex()) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + cw - P(22.0f), p.y + P(1.0f)));
        if (StudioButton("##xrow", "", "x", SB_FLAT, 20.0f, 20.0f))
          Remove(&g_rowsPicked, a.unit);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Take the row away");
      }
    }
    // its wins
    int nWins = 0;
    for (size_t c = 0; c < nCols; ++c)
      nWins += g.At(r, c).nWinner == W_ROW;
    ImGui::TableSetColumnIndex(1);
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, C(TK_PANE));
      T(td, STUDIO_FONT_BOLD, 17.0f, p.x + P(2.0f), p.y + P(2.0f), nWins * 2 >= static_cast<int>(nCols) ? C(TK_GOOD) : C(TK_INK2),
        std::to_string(nWins));
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(2.0f), p.y + P(24.0f), C(TK_INK3), "of " + std::to_string(nCols));
    }
    // the cells
    for (size_t c = 0; c < nCols; ++c) {
      ImGui::TableSetColumnIndex(static_cast<int>(c) + 2);
      const Outcome &o = g.At(r, c);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float cw = ImGui::GetContentRegionAvail().x;
      const ImVec2 ca(p.x - P(2.0f), p.y - P(2.0f)), cb(p.x + cw + P(2.0f), p.y + rowH - P(6.0f));
      ImGui::PushID(static_cast<int>(c));
      ImGui::SetCursorScreenPos(ca);
      if (ImGui::InvisibleButton("##cell", ImVec2(cb.x - ca.x, cb.y - ca.y))) {
        g_selRow = g.rows[r];
        g_selCol = g.cols[c];
      }
      const bool bHover = ImGui::IsItemHovered();
      const bool bSel = bRowSel && g_selCol == g.cols[c];
      td->AddRectFilled(ca, cb, WinColour(o.nWinner, o.Margin()), P(3.0f));
      if (bSel || bHover)
        td->AddRect(ca, cb, bSel ? C(TK_SEL_LINE) : C(TK_BTN_RIM_HI), P(3.0f), 0, bSel ? P(2.0f) : P(1.0f));
      std::string top, sub;
      ImU32 col = C(TK_INK2);
      switch (o.nWinner) {
      case W_ROW: top = "Wins " + Pct(o.adShare[0]); col = C(TK_HI); break;
      case W_COL: top = "Loses " + Pct(o.adShare[1]); col = C(TK_HI); break;
      case W_BOTH: top = "Even"; col = C(TK_WARN); break;
      default: top = !o.abCan[0] && !o.abCan[1] ? "No fight" : "Stands off"; col = C(TK_INK3); break;
      }
      if (g.set.bPerCost)
        sub = std::to_string(o.anCount[0]) + " v " + std::to_string(o.anCount[1]) + (o.nWinner <= W_BOTH ? ", " + Secs(o.dTime) : "");
      else
        sub = (o.anAttacks[0] ? Secs(o.adAlone[0]) : std::string("never")) + " / " + (o.anAttacks[1] ? Secs(o.adAlone[1]) : std::string("never"));
      T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x + P(4.0f), p.y + P(3.0f), col, top, cw - P(6.0f));
      T(td, STUDIO_FONT_MONO, kStudioFsXs, p.x + P(4.0f), p.y + P(22.0f), C(TK_INK2), sub, cw - P(6.0f));
      if (bHover)
        ImGui::SetTooltip("%s against %s\n%s\n%s\nClick for the fight", a.show.c_str(), FighterOf(g.cols[c]).show.c_str(),
                          Sentence(g, r, c).c_str(),
                          g.set.bPerCost ? "Both sides as many as the budget buys"
                                         : "Below: the seconds the row's unit takes to kill the column's alone, then the column's");
      ImGui::PopID();
    }
    // life for its cost and damage a second against each size
    ImGui::TableSetColumnIndex(static_cast<int>(nCols) + 2);
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, C(TK_PANE));
      T(td, STUDIO_FONT_MONO, kStudioFs, p.x + P(2.0f), p.y + P(3.0f), C(TK_INK), a.dCost > 0.0 ? Fmt(LifeForCost(a), 0) : std::string("free"));
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(2.0f), p.y + P(22.0f), C(TK_INK3),
        std::to_string(a.nLife + a.nShields) + " for " + Fmt(a.dCost));
    }
    ImGui::TableSetColumnIndex(static_cast<int>(nCols) + 3);
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, C(TK_PANE));
      double ad[3];
      SizeDps(a, g.set.nLevel, ad);
      T(td, STUDIO_FONT_MONO, kStudioFs, p.x + P(2.0f), p.y + P(3.0f), C(TK_INK),
        Fmt(ad[0]) + " / " + Fmt(ad[1]) + " / " + Fmt(ad[2]));
      const Weapon &w = a.ground.Can() ? a.ground : a.air;
      T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(2.0f), p.y + P(22.0f), C(TK_INK3),
        w.Can() ? Short(ST_WEAPONS, w.name) : std::string("no weapon"), ImGui::GetContentRegionAvail().x);
    }
    ImGui::PopID();
  }
  // the foot: how many rows each column beats
  ImGui::TableNextRow(ImGuiTableRowFlags_None, P(34.0f));
  for (int i = 0; i < static_cast<int>(nCols) + 2; ++i) {
    ImGui::TableSetColumnIndex(i);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, C(TK_PANE2));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (i == 0) {
      T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x + P(4.0f), p.y + P(4.0f), C(TK_LABEL), "The column beats");
      continue;
    }
    if (i == 1)
      continue;
    const int n = beats[static_cast<size_t>(i - 2)];
    T(td, STUDIO_FONT_MONO, kStudioFsS, p.x + P(4.0f), p.y + P(4.0f), n * 2 > static_cast<int>(nRows) ? C(TK_BAD) : C(TK_INK2),
      std::to_string(n) + " of " + std::to_string(nRows));
  }
  ImGui::EndTable();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

// The pair side by side: a picture and name over each side's column, then a line for each number,
// the row's unit at the left.
float Compare(ImDrawList *d, ImVec2 o, float w, const Fighter &fa, const Fighter &fb, const Outcome &out) {
  const float lw = P(96.0f), vw = (w - lw) / 2.0f;
  const Fighter *f[2] = {&fa, &fb};
  float y = o.y;
  for (int s = 0; s < 2; ++s) {
    const float x = o.x + lw + s * vw;
    Picture(d, ImVec2(x, y), ImVec2(x + P(44.0f), y + P(44.0f)), *f[s], s == 0 ? 8 : 24, s);
    T(d, STUDIO_FONT_BOLD, 14.0f, x + P(50.0f), y + P(4.0f), C(TK_HI), f[s]->show, vw - P(54.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x + P(50.0f), y + P(24.0f), C(TK_INK3),
      out.anCount[s] > 1 ? std::to_string(out.anCount[s]) + " of it" : KindWords(*f[s]), vw - P(54.0f));
  }
  y += P(52.0f);
  struct Line {
    const char *pszLabel;
    std::string v[2];
    int nBetter = -1;  // the side whose number is the better one, drawn in green
  };
  std::vector<Line> lines(11);
  for (int s = 0; s < 2; ++s) {
    const Fighter &me = *f[s];
    const Weapon &wp = me.Against(*f[1 - s]);
    lines[0].pszLabel = "Life";
    lines[0].v[s] = std::to_string(me.nLife) + (me.nShields ? " + " + std::to_string(me.nShields) : std::string());
    lines[1].pszLabel = "Armour, size";
    lines[1].v[s] = std::to_string(me.nArmour) + ", " + SizeWord(me.nSize);
    lines[2].pszLabel = "Cost";
    lines[2].v[s] = std::to_string(me.nMinerals) + " + " + std::to_string(me.nGas) +
                    (me.dCost * 2.0 == static_cast<double>(me.nMinerals + me.nGas) ? ", a pair" : "");
    lines[3].pszLabel = "Weapon";
    lines[3].v[s] = wp.Can() ? Short(ST_WEAPONS, wp.name) : std::string("none against it");
    lines[4].pszLabel = "Hits an attack";
    lines[4].v[s] = wp.Can() ? std::to_string(wp.nHits) + (wp.nSwings > 1 ? ", in " + std::to_string(wp.nSwings) + " swings" : std::string())
                             : std::string("-");
    lines[5].pszLabel = "An attack takes";
    lines[5].v[s] = out.abCan[s] ? Fmt(out.adPerAttack[s]) + " off life" : std::string("-");
    lines[6].pszLabel = "Attacks to kill";
    lines[6].v[s] = out.anAttacks[s] ? std::to_string(out.anAttacks[s]) : std::string("cannot");
    lines[7].pszLabel = "Time to kill";
    lines[7].v[s] = out.anAttacks[s] ? Secs(out.adAlone[s]) : std::string("never");
    lines[8].pszLabel = "Damage a second";
    lines[8].v[s] = out.abCan[s] ? Fmt(out.adDps[s]) : std::string("-");
    lines[9].pszLabel = "Range";
    lines[9].v[s] = !out.abCan[s] ? std::string("-") : wp.nRange <= 32 ? std::string("melee") : Fmt(out.adRange[s]) + " tiles";
    lines[10].pszLabel = "Speed";
    lines[10].v[s] = me.bSpeedGuess ? "about " + Fmt(me.dSpeed, 0) + " px/s" : Fmt(me.dSpeed, 0) + " px/s";
  }
  const double t0 = out.anAttacks[0] ? out.adAlone[0] : 1e9, t1 = out.anAttacks[1] ? out.adAlone[1] : 1e9;
  if (t0 != t1)
    lines[7].nBetter = t0 < t1 ? 0 : 1;
  const float rh = P(21.0f);
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i % 2 == 0)
      d->AddRectFilled(ImVec2(o.x - P(4.0f), y - P(2.0f)), ImVec2(o.x + w + P(4.0f), y + rh - P(2.0f)), C(TK_ROW, 0.55f), P(2.0f));
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, o.x, y + P(1.5f), C(TK_INK3), lines[i].pszLabel, lw - P(6.0f));
    for (int s = 0; s < 2; ++s)
      T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + lw + s * vw, y, lines[i].nBetter == s ? C(TK_GOOD) : C(TK_INK), lines[i].v[s], vw - P(8.0f));
    y += rh;
  }
  return y;
}

void Detail(const Grid &g, ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddRectFilled(a, b, C(TK_PANE2), P(4.0f));
  d->AddRect(a, b, C(TK_LINE), P(4.0f));
  const float x = a.x + P(14.0f), w = b.x - a.x - P(28.0f);
  float y = a.y + P(12.0f);
  size_t r = g.rows.size(), c = g.cols.size();
  for (size_t i = 0; i < g.rows.size(); ++i)
    if (g.rows[i] == g_selRow)
      r = i;
  for (size_t i = 0; i < g.cols.size(); ++i)
    if (g.cols[i] == g_selCol)
      c = i;
  Wide(d, 9.5f, x, y, C(TK_INK3), "The fight");
  y += P(22.0f);
  if (r >= g.rows.size() || c >= g.cols.size()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_INK3), "Click a cell to see its fight.", w);
    return;
  }
  const Fighter &fa = FighterOf(g.rows[r]), &fb = FighterOf(g.cols[c]);
  const Outcome &o = g.At(r, c);
  T(d, STUDIO_FONT_BOLD, 17.0f, x, y, C(TK_HI), fa.show + " against " + fb.show, w);
  y += P(26.0f);
  // the outcome, in its colour
  const float oh = P(46.0f);
  d->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + oh), WinColour(o.nWinner, o.Margin(), 0.22f), P(3.0f));
  ImGui::SetCursorScreenPos(ImVec2(x + P(10.0f), y + P(6.0f)));
  ImGui::PushTextWrapPos(x + w - P(10.0f) - ImGui::GetWindowPos().x);
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK));
  ImGui::TextWrapped("%s", Sentence(g, r, c).c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
  y = std::max(y + oh, ImGui::GetCursorScreenPos().y + P(6.0f)) + P(14.0f);
  // the two side by side
  const float y0 = y;
  y = Compare(d, ImVec2(x, y), w, fa, fb, o) + P(12.0f);
  if (y + P(90.0f) > b.y)
    y = std::max(y0, b.y - P(90.0f));
  // play it, open either side
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (StudioButton("##play", "Play this fight", "try", SB_GOOD, 28.0f)) {
    // a mod's edit takes the classic row's place in the game, so it is spawned by that row
    auto spawnName = [](const Fighter &f) { return f.kind == SK_EDIT && !f.base.empty() ? f.base : f.unit; };
    StudioAsk("studio_try", "\"unit\":" + Q(spawnName(fa)) + ",\"yours\":" + std::to_string(o.anCount[0]) + ",\"units\":[{\"unit\":" +
                                Q(spawnName(fb)) + ",\"count\":" + std::to_string(o.anCount[1]) + ",\"player\":1}]");
    StudioSay("the fight plays in the Game tab: " + fa.show + " against " + fb.show + "; F9 puts it back");
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The mod pushed into the canvas game, the field cleared, and both sides spawned facing each other;\n"
                      "the game walks them in, so range and speed count there");
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##playout", "Play it out", "again", SB_PLAIN, 28.0f)) {
    auto spawnName = [](const Fighter &f) { return f.kind == SK_EDIT && !f.base.empty() ? f.base : f.unit; };
    StudioAbOpen(fa.unit, o.anCount[0], "[{\"unit\":" + Q(spawnName(fb)) + ",\"count\":" + std::to_string(o.anCount[1]) + ",\"player\":1}]");
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("This fight played out in the game over many seeds, and against a version B of the row's unit");
  ImGui::SameLine(0.0f, P(8.0f));
  const std::string openA = "Open " + fa.show, openB = "Open " + fb.show;
  if (StudioButton("##opena", openA.c_str(), "unit", SB_FLAT, 28.0f))
    StudioOpenDoc(ST_UNITS, fa.unit, 0);
  if (x + w > ImGui::GetCursorScreenPos().x + StudioButtonSize(openB.c_str(), "unit", 28.0f).x + P(8.0f))
    ImGui::SameLine(0.0f, P(4.0f));
  else
    ImGui::SetCursorScreenPos(ImVec2(x, y + P(34.0f)));
  if (fb.unit != fa.unit && StudioButton("##openb", openB.c_str(), "unit", SB_FLAT, 28.0f))
    StudioOpenDoc(ST_UNITS, fb.unit, 0);
  y = ImGui::GetCursorScreenPos().y + P(6.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, std::min(y, b.y - P(20.0f)), C(TK_INK3),
    "In the game a mod's edit stands in for the classic unit on both sides.", w);
}

void Legend(ImDrawList *d, ImVec2 o, float W, float h) {
  float x = o.x;
  auto swatch = [&](ImU32 col, const char *psz) {
    d->AddRectFilled(ImVec2(x, o.y + h / 2.0f - P(6.0f)), ImVec2(x + P(16.0f), o.y + h / 2.0f + P(6.0f)), col, P(2.0f));
    x += P(22.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(o.y, h, kStudioFsXs), C(TK_INK2), psz) + P(16.0f);
  };
  swatch(WinColour(W_ROW, 0.7), "the row's unit wins");
  swatch(WinColour(W_COL, 0.7), "the column's wins");
  swatch(WinColour(W_BOTH, 0.0), "both fall together");
  swatch(WinColour(W_NEITHER, 0.0), "no fight");
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(o.y, h, kStudioFsXs), C(TK_INK3),
    "Deeper: more of the winner left. Classic damage rules; each side fires at the other's first unit standing, "
    "four melee units round each; no spells, splash, healing or regeneration.",
    o.x + W - x);
}

}  // namespace

namespace {

void Workspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = b.x - a.x - P(32.0f);
  const ImVec2 o(a.x + P(16.0f), a.y + P(12.0f));
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, o.y + P(10.0f), C(TK_INK3), "Open a mod to see its units against each other.");
    return;
  }
  float y = o.y;
  y += Bar(d, ImVec2(o.x, y), W) + P(6.0f);
  const float lh = P(30.0f);
  const Grid &g = Shown();
  T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, Mid(y, lh, kStudioFsS), C(TK_INK2),
    std::to_string(g.rows.size()) + (g.rows.size() == 1 ? " unit" : " units") + " against " + std::to_string(g.cols.size()) +
        ", " + (g.set.bPerCost ? "as many of each as " + std::to_string(g.set.nBudget) + " buys" : std::string("one against one")) +
        (g.set.nLevel ? ", upgrades at 3" : "") + (g.set.bClosing ? ", closing in" : "") + ". Click a cell for the fight.",
    W - P(470.0f));
  Pickers(ImVec2(o.x, y), W, lh);
  y += lh + P(8.0f);
  const float legendH = P(28.0f);
  const float bottom = b.y - legendH - P(8.0f);
  if (g.rows.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, y + P(10.0f), C(TK_INK3),
      "The mod has no unit of its own that fights yet. Add a row to see a unit against the columns.", W);
    Legend(d, ImVec2(o.x, bottom + P(8.0f)), W, legendH);
    return;
  }
  // a cell to show at first: the first row against the first classic column
  if (std::find(g.rows.begin(), g.rows.end(), g_selRow) == g.rows.end() ||
      std::find(g.cols.begin(), g.cols.end(), g_selCol) == g.cols.end()) {
    g_selRow = g.rows[0];
    g_selCol = g.cols[std::min(g.cols.size() - 1, g_modUnits.size())];
  }
  const bool bSide = W >= P(1000.0f);
  const float dw = bSide ? std::min(P(380.0f), W * 0.34f) : 0.0f;
  const float dh = bSide ? 0.0f : P(330.0f);
  const float gw = bSide ? W - dw - P(12.0f) : W;
  const float gh = bottom - y - (bSide ? 0.0f : dh + P(10.0f));
  GridTable(g, ImVec2(o.x, y), gw, std::max(P(120.0f), gh));
  if (bSide)
    Detail(g, ImVec2(o.x + gw + P(12.0f), y), ImVec2(o.x + W, bottom));
  else
    Detail(g, ImVec2(o.x, bottom - dh), ImVec2(o.x + W, bottom));
  Legend(d, ImVec2(o.x, bottom + P(8.0f)), W, legendH);
}

}  // namespace

// Drawn under an id of its own, so its widgets' ids cannot meet the shell's.
void StudioBalanceWorkspace(ImVec2 a, ImVec2 b) {
  ImGui::PushID("##balance");
  if (StudioAbShown())
    StudioAbWorkspace(a, b);
  else
    Workspace(a, b);
  StudioAbPages(a, b);
  ImGui::PopID();
}

namespace {

std::vector<std::string> Names(const PortJson &v) {
  std::vector<std::string> out;
  if (v.kind == PortJson::ARRAY)
    for (const PortJson &x : v.items)
      if (x.kind == PortJson::STRING)
        out.push_back(x.s);
  if (v.kind == PortJson::STRING)
    out.push_back(v.s);
  return out;
}

bool Units(const std::vector<std::string> &v, std::string *pWhy) {
  for (const std::string &s : v) {
    StudioRecord r;
    if (!StudioRecordOf(ST_UNITS, s, &r)) {
      *pWhy = s + " is no unit";
      return false;
    }
  }
  return true;
}

// studio_matchup {units?, against?, per_cost?, budget?, level?, closing?, show?}: the grid worked
// out, the rows the mod's units that fight and the picked ones, or units; the columns those and the
// picked classic ones, or against; the settings the workspace's unless given. With show, the
// workspace shown with these settings.
bool CmdMatchup(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  Grid g;
  g.set = g_set;
  if (j["per_cost"].kind == PortJson::BOOL)
    g.set.bPerCost = j["per_cost"].b;
  if (j["budget"].kind == PortJson::NUMBER)
    g.set.nBudget = std::max(25, std::min(100000, static_cast<int>(j["budget"].d)));
  if (j["level"].kind == PortJson::NUMBER)
    g.set.nLevel = std::max(0, std::min(3, static_cast<int>(j["level"].d)));
  if (j["closing"].kind == PortJson::BOOL)
    g.set.bClosing = j["closing"].b;
  g.rows = j["units"].kind != PortJson::NONE ? Names(j["units"]) : RowsNow();
  g.cols = j["against"].kind != PortJson::NONE ? Names(j["against"]) : ColsNow();
  if (!Units(g.rows, pAnswer) || !Units(g.cols, pAnswer))
    return false;
  if (j["show"].kind == PortJson::BOOL && j["show"].b) {
    g_set = g.set;
    StudioWorkspaceSet(10);
  }
  Work(&g);
  *pAnswer = GridJson(g);
  return true;
}

std::string ListJson(const std::vector<std::string> &v) {
  std::string s;
  for (const std::string &x : v)
    s += std::string(s.empty() ? "" : ",") + Q(x);
  return "[" + s + "]";
}

// studio_matchup_against {add? | remove? | reset?, rows?}: a unit (or a list) added to the picked
// columns or taken away, or the columns put back to their defaults; with rows, the picked rows.
bool CmdAgainst(const PortJson &j, std::string *pAnswer) {
  const bool bRows = j["rows"].kind == PortJson::BOOL && j["rows"].b;
  std::vector<std::string> &v = bRows ? g_rowsPicked : g_against;
  const std::vector<std::string> add = Names(j["add"]), remove = Names(j["remove"]);
  if (!Units(add, pAnswer))
    return false;
  if (j["reset"].kind == PortJson::BOOL && j["reset"].b) {
    if (bRows)
      v.clear();
    else
      v.assign(std::begin(kDefaultAgainst), std::end(kDefaultAgainst));
  }
  for (const std::string &s : add)
    AddOnce(&v, s);
  for (const std::string &s : remove)
    Remove(&v, s);
  *pAnswer = "\"against\":" + ListJson(g_against) + ",\"rows_picked\":" + ListJson(g_rowsPicked) +
             (StudioIsOpen() ? ",\"rows\":" + ListJson(RowsNow()) + ",\"columns\":" + ListJson(ColsNow()) : std::string());
  return true;
}

}  // namespace

void StudioBalanceInit(void) {
  PortControlCommand("studio_matchup", CmdMatchup);
  PortControlCommand("studio_matchup_against", CmdAgainst);
}
