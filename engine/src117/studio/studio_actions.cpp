// studio_actions.cpp -- actions: a unit's Actions view, an order's head, and the Effects workspace
// (the design's mock, cs-actions.js). A spell is one thing to the modder: a button on its caster's card,
// a target, a cost, what it needs, and what it does as a list of blocks. In the mod's files it is an
// order, a tech and a weapon of the same name and a "cast" button on the caster's card
// (docs/mod-data.md, Cards): a mod's order runs as the classic spell order does
// (DispatchClickAbility), taking its tech's energy and firing its weapon, whose explosion is the
// status it puts on what it hits. An order may have blocks of its own (docs/mod-data.md, "Action
// blocks"), run in its weapon's place as it is cast: fire, damage, heal, energy, a status put on or
// taken off, units spawned, the caster teleported, a sound, a sprite, wait and repeat, nested. The
// few past them (if, push or pull, a hallucination) need the engine's scripting and are shown as
// waiting. A unit's passive aura is edited in its Actions view.
//
//   studio_actions {unit?, show?, blocks?} the mod's actions: each order with its tech, weapon and casts
//   studio_make_action {unit, from, label?, name?, values?}   an action made from a classic spell, one edit
//   studio_delete_action {name}         an action's order, tech, weapon and buttons taken away, one edit
//   studio_action_research {name, at?}  an action's research set or taken away, with its button, one edit
//   studio_action_blocks {name, blocks}  an action's blocks, the whole list, one edit
//   studio_block_add {name, block, path?, at?} · studio_block_set {name, path, field, value} ·
//   studio_block_remove {name, path} · studio_block_move {name, path, by}   one block, one edit
#include <algorithm>
#include <map>
#include <vector>
#include "../design/design_engine.h"     // DesignCall
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_media.h"     // StudioPlayButton
#include "studio_statuses.h"  // StudioStatusPick, StudioStatusLabel

namespace {

using namespace studio_draw;

// ---- the classic spells an action starts from -------------------------------------------------------------

enum { AIM_SELF, AIM_UNIT, AIM_PLACE };

struct Spell {
  const char *pszKey;     // its id in studio_make_action's "from"
  const char *pszLabel;
  const char *pszTech;    // the classic rows the new records start from, by name
  const char *pszOrder;
  const char *pszWeapon;
  int nAim;
  const char *pszAi;      // the computer's finder (docs/mod-data.md), "" for none
  const char *pszGlyph;
  const char *pszAbout;
  const char *pszWork;    // what the classic order does itself, for a spell that fires no weapon;
                          // null when the weapon it fires does the work
};

// The spells whose classic order is the spell order that fires a weapon (DispatchClickAbility), the
// weapon doing the work; then the ones whose classic order does its own work, which a mod's order
// made from that order runs as well (docs/mod-data.md). Stim Packs has no order: a mod's order whose
// tech is made from it stims the caster.
const Spell kSpells[] = {
    {"blocks", "Blocks of its own", "psionic_storm", "cast_psionic_storm", "psionic_storm", AIM_PLACE, "", "fx",
     "made of blocks: damage, heal, statuses, units, teleport, waits and repeats", nullptr},
    {"psionic_storm", "Psionic Storm", "psionic_storm", "cast_psionic_storm", "psionic_storm", AIM_PLACE, "psionic_storm", "fx",
     "a storm at a place that hurts everything under it", nullptr},
    {"lockdown", "Lockdown", "lockdown", "cast_lockdown", "lockdown", AIM_UNIT, "lockdown", "weapon",
     "a shot that locks a mechanical unit down", nullptr},
    {"yamato_gun", "Yamato Gun", "yamato_gun", "fire_yamato_gun", "yamato_gun", AIM_UNIT, "yamato_gun", "weapon",
     "one heavy shot at a unit from far off", nullptr},
    {"burst", "Burst around itself", "psionic_storm", "cast_psionic_storm", "psionic_shockwave", AIM_SELF, "enemy_cluster", "fx",
     "a flash on the caster that hurts and stuns enemies near it", nullptr},
    {"emp_shockwave", "EMP Shockwave", "emp_shockwave", "cast_emp_shockwave", "emp_shockwave", AIM_PLACE, "emp_shockwave", "fx",
     "takes shields and energy at a place", nullptr},
    {"irradiate", "Irradiate", "irradiate", "cast_irradiate", "irradiate", AIM_UNIT, "irradiate", "fx",
     "organic units near the target lose life", nullptr},
    {"plague", "Plague", "plague", "cast_plague", "plague", AIM_PLACE, "plague", "fx",
     "life drains from units at a place, never below 1", nullptr},
    {"ensnare", "Ensnare", "ensnare", "cast_ensnare", "ensnare", AIM_PLACE, "ensnare", "fx",
     "units at a place move and attack slower", nullptr},
    {"stasis_field", "Stasis Field", "stasis_field", "cast_stasis_field", "stasis_field", AIM_PLACE, "stasis_field", "fx",
     "freezes units at a place: they cannot act or be hurt", nullptr},
    {"dark_swarm", "Dark Swarm", "dark_swarm", "cast_dark_swarm", "dark_swarm", AIM_PLACE, "dark_swarm", "fx",
     "a cloud under which ranged attacks miss", nullptr},
    {"disruption_web", "Disruption Web", "disruption_web", "cast_disruption_web", "disruption_web", AIM_PLACE, "disruption_web", "fx",
     "units under it cannot attack", nullptr},
    {"maelstrom", "Maelstrom", "maelstrom", "cast_maelstrom", "maelstrom", AIM_PLACE, "maelstrom", "fx",
     "organic units at a place cannot act", nullptr},
    {"optical_flare", "Optical Flare", "optical_flare", "cast_optical_flare", "optical_flare", AIM_UNIT, "optical_flare", "weapon",
     "blinds a unit", nullptr},
    {"restoration", "Restoration", "restoration", "cast_restoration", "restoration", AIM_UNIT, "restoration", "fx",
     "takes the statuses off a unit", nullptr},
    {"parasite", "Parasite", "parasite", "cast_parasite", "parasite", AIM_UNIT, "parasite", "weapon",
     "the caster's side sees what the unit sees", nullptr},
    {"spawn_broodlings", "Spawn Broodlings", "spawn_broodlings", "cast_spawn_broodlings", "spawn_broodlings", AIM_UNIT,
     "spawn_broodlings", "weapon", "kills a ground unit and hatches broodlings", nullptr},
    {"consume", "Consume", "consume", "cast_consume", "consume", AIM_UNIT, "", "weapon",
     "takes one of its own side's units for energy", nullptr},
    {"stim_packs", "Stim Packs", "stim_packs", "cast_psionic_storm", nullptr, AIM_SELF, "", "try",
     "trades life for speed for a while",
     "the caster gives 10 life to move and attack faster for a while"},
    {"defensive_matrix", "Defensive Matrix", "defensive_matrix", "cast_defensive_matrix", nullptr, AIM_UNIT, "", "fx",
     "a unit takes 250 damage before its life", "a matrix on the unit takes 250 damage before its life does"},
    {"recall", "Recall", "recall", "cast_recall", nullptr, AIM_PLACE, "", "fx", "brings units at a place to the caster",
     "the caster's units near the place are brought to it"},
    {"hallucination", "Hallucination", "hallucination", "cast_hallucination", nullptr, AIM_UNIT, "", "fx",
     "makes copies of a unit that do no harm", "two copies of the unit, that do no harm and die of any spell"},
    {"mind_control", "Mind Control", "mind_control", "cast_mind_control", nullptr, AIM_UNIT, "", "fx",
     "takes a unit over", "an enemy unit becomes the caster's, and the caster loses its shields"},
    {"feedback", "Feedback", "feedback", "cast_feedback", nullptr, AIM_UNIT, "", "fx",
     "a unit's energy burns it", "the unit loses all its energy and takes as much damage"},
    {"scanner_sweep", "Scanner Sweep", "scanner_sweep", "cast_scanner_sweep", nullptr, AIM_PLACE, "", "fx",
     "shows a place for a while", "a sweep shows the place, hidden units too, for a while"},
};
const int kSpellCount = static_cast<int>(sizeof kSpells / sizeof kSpells[0]);

const Spell *SpellOf(const std::string &key) {
  for (const Spell &s : kSpells)
    if (key == s.pszKey)
      return &s;
  return nullptr;
}

// The blocks the engine does not run yet, which need its scripting, shown as waiting.
struct Waiting {
  const char *pszGroup;
  const char *pszLabel;
  const char *pszGlyph;
};
const Waiting kWaiting[] = {
    {"Flow", "If", "filter"},         {"Flow", "For each in an area", "filter"}, {"Move", "Pull or push", "back"},
    {"Make", "A hallucination", "unit"}, {"Flow", "Held while it channels", "pause"}, {"Flow", "Charges", "again"},
};

// What a weapon's explosion puts on what it hits, in words.
const char *ExplosionWords(long long n) {
  switch (n) {
  case 0: return "does nothing more where it lands";
  case 1: return "hits the one unit";
  case 2: return "splashes everything near where it lands";
  case 3: return "splashes the enemies near where it lands";
  case 4: return "locks a mechanical unit down: it cannot move, attack or cast";
  case 5: return "a nuclear blast";
  case 6: return "plants a parasite: the caster's side sees what the unit sees";
  case 7: return "kills a ground unit and hatches broodlings from it";
  case 8: return "takes shields and energy from the units there";
  case 9: return "irradiates: organic units near it lose life";
  case 10: return "ensnares: the units there move and attack slower";
  case 11: return "plagues: the units there lose life, never below 1";
  case 12: return "puts the units there in stasis: frozen, they cannot act or be hurt";
  case 13: return "drops a dark swarm, under which ranged attacks miss";
  case 14: return "consumes one of its own side's units for energy";
  case 15: return "a Yamato blast";
  case 16: return "restores the unit: its statuses taken off";
  case 17: return "drops a disruption web: units under it cannot attack";
  case 18: return "corrosive acid: armour lowered";
  case 19: return "takes control of the unit";
  case 20: return "feedback: the unit's energy burns it";
  case 21: return "blinds the unit";
  case 22: return "holds the organic units there in a maelstrom";
  case 24: return "splashes the flyers near where it lands";
  case 25: return "splashes enemies and stuns those on the ground";
  default: return "an explosion of its own";
  }
}

// How the computer finds a cast's target (docs/mod-data.md, a card's "ai"), in words.
std::string AiWords(const std::string &ai) {
  if (ai.empty())
    return "never casts it";
  if (ai == "enemy_cluster")
    return "casts it at a cluster of enemies in its splash";
  std::string s = ai;
  for (size_t i = 0; i < s.size(); ++i)
    if (s[i] == '_')
      s[i] = ' ';
    else if (i == 0 || s[i - 1] == ' ')
      s[i] = static_cast<char>(toupper(static_cast<unsigned char>(s[i])));
  return "casts it where the classic " + s + " would be cast";
}

// How long the status of an explosion lasts, in game frames, where the classic game fixes it:
// the status timers count down every 8 frames (CUnit::SpellUpdate) from the time the hit gives
// (CBullet, UnitApplyLockdown 131, UnitApplyStasis 131, Ensnare and Plague 75, Maelstrom 22, a
// mod's stun 8); Irradiate's is the classic Irradiate weapon's cooldown. 0 for none known.
long long StatusFrames(long long nExplosion) {
  switch (nExplosion) {
  case 4: return 131 * 8;
  case 9: return StudioNumber(ST_WEAPONS, "irradiate", "cooldown") * 8;
  case 10: return 75 * 8;
  case 11: return 75 * 8;
  case 12: return 131 * 8;
  case 22: return 22 * 8;
  case 25: return 8 * 8;
  default: return 0;
  }
}

// ---- small helpers ------------------------------------------------------------------------------------------

std::string Fields(int t, const std::string &name) {
  return "\"table\":" + Q(Table(t)) + ",\"name\":" + Q(name);
}

void Set(int t, const std::string &name, const std::string &key, const std::string &json) {
  StudioAsk("studio_set", Fields(t, name) + ",\"field\":" + Q(key) + ",\"value\":" + json + ",\"merge\":" + Q(name + "." + key));
}

bool Mine(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && (r.kind == SK_OWN || r.kind == SK_EDIT);
}

bool Own(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && r.kind == SK_OWN;
}

// A ref column's record by name, "" for the table's "none" row.
std::string Ref(int t, const std::string &name, const char *pszKey, int nRef) {
  const long long n = StudioNumber(t, name, pszKey, StudioClassicRows(nRef));
  return n == StudioClassicRows(nRef) ? std::string() : StudioRefName(nRef, n);
}

// "Void Lance" -> void_lance
std::string KeyOf(const std::string &label) {
  std::string k;
  for (char ch : label) {
    if (isalnum(static_cast<unsigned char>(ch)))
      k += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    else if (!k.empty() && k.back() != '_')
      k += '_';
  }
  while (!k.empty() && k.back() == '_')
    k.pop_back();
  if (k.empty() || isdigit(static_cast<unsigned char>(k[0])))
    k = "action_" + k;
  return k;
}

std::string Tiles(long long px) {
  if (px >= 4096)
    return "anywhere";
  return px <= 32 ? std::string("next to it") : Fmt(px / 32.0) + " tiles";
}

// The unit whose card a unit's record names: an edit's row, else the record itself.
std::string CardOf(const std::string &unit) {
  StudioRecord r;
  if (StudioRecordOf(ST_UNITS, unit, &r) && r.kind == SK_EDIT)
    return r.base;
  return unit;
}

bool UnitFlag(const std::string &unit, const char *pszFlag) {
  const std::vector<std::string> &names = StudioFlags("units");
  const long long f = StudioNumber(ST_UNITS, unit, "flags");
  for (size_t i = 0; i < names.size(); ++i)
    if (names[i] == pszFlag)
      return ((f >> i) & 1) != 0;
  return false;
}

// "Psionic <L>ance": the text with its hotkey's letter marked
std::string Marked(const std::string &text, char key) {
  const std::string lower = Lower(text);
  const size_t i = lower.find(static_cast<char>(tolower(static_cast<unsigned char>(key))));
  if (i == std::string::npos)
    return "<" + std::string(1, static_cast<char>(toupper(static_cast<unsigned char>(key)))) + ">" + text;
  return text.substr(0, i) + "<" + text[i] + ">" + text.substr(i + 1);
}

// A card's places and hotkeys the mod's buttons take; the classic basic commands' keys and places
// count as taken (move, stop, attack, patrol, hold).
void Taken(const std::string &card, unsigned *puPlaces, std::string *pKeys, const std::string &except = std::string()) {
  *puPlaces = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);
  *pKeys = "msaph";
  for (const PortJson &b : StudioDoc("types")["cards"][card.c_str()].items) {
    if (!except.empty() && (b["cast"].Str() == except || b["research"].Str() == except))
      continue;
    const int n = static_cast<int>(b["position"].Num(0));
    if (n >= 1 && n <= 9)
      *puPlaces |= 1u << n;
    if (!b["hotkey"].Str().empty())
      *pKeys += static_cast<char>(tolower(static_cast<unsigned char>(b["hotkey"].Str()[0])));
  }
}

int FreePlace(const std::string &card, const std::string &except = std::string()) {
  unsigned u = 0;
  std::string keys;
  Taken(card, &u, &keys, except);
  for (int n : {7, 8, 9, 6})  // the classic cards keep their spells on the bottom row
    if (!(u & (1u << n)))
      return n;
  for (int n = 9; n >= 1; --n)
    if (!(u & (1u << n)))
      return n;
  return 0;  // the card is full
}

char FreeKey(const std::string &card, const std::string &label, const std::string &except = std::string()) {
  unsigned u = 0;
  std::string keys;
  Taken(card, &u, &keys, except);
  for (char ch : label + "qwertyuiopasdfghjklzxcvbnm") {
    const char lo = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    if (isalpha(static_cast<unsigned char>(lo)) && keys.find(lo) == std::string::npos)
      return lo;
  }
  return 'q';
}

// ---- the mod's actions --------------------------------------------------------------------------------------

struct Cast {
  std::string card;   // the card's unit
  size_t nIndex = 0;  // the button's place in the card's list
  PortJson button;
};

struct Action {
  std::string order, tech, weapon;
  const Spell *pWork = nullptr;  // the spell whose classic order's own work it runs, or null
  StudioKind kind = SK_OWN;
  std::vector<Cast> casts;
  std::string research;  // the card with its research button, "" for none
  PortJson blocks;       // the order's own blocks, run in its weapon's place
};

// The classic row a record descends from, through its bases; "" for none.
std::string ClassicOf(int t, const std::string &name) {
  std::string n = name;
  for (int i = 0; i < 16 && !n.empty(); ++i) {
    StudioRecord r;
    if (!StudioRecordOf(t, n, &r))
      return std::string();
    if (r.kind == SK_CLASSIC)
      return n;
    n = r.base;
  }
  return std::string();
}

// The spell whose classic order's own work an order of the mod runs, by the engine's rule
// (ModOrderHandler): its order descends from that spell's order, or for Stim Packs, which has no
// order, its tech from Stim Packs. Null for an order that fires its weapon.
const Spell *WorkOf(const std::string &order, const std::string &tech) {
  const std::string root = ClassicOf(ST_ORDERS, order);
  for (const Spell &s : kSpells)
    if (s.pszWork && strcmp(s.pszKey, "stim_packs") != 0 && root == s.pszOrder)
      return &s;
  if (!tech.empty() && ClassicOf(ST_TECHS, tech) == "stim_packs")
    return SpellOf("stim_packs");
  return nullptr;
}

// The mod's orders, each an action, with its tech and weapon and the buttons that cast it; looked
// for again when the mod changes.
const std::vector<Action> &Actions(void) {
  static std::vector<Action> s_list;
  static unsigned s_nChange = ~0u;
  static std::string s_mod;
  if (s_nChange == StudioChange() && s_mod == StudioModId())
    return s_list;
  s_nChange = StudioChange();
  s_mod = StudioModId();
  s_list.clear();
  for (const StudioRecord &r : StudioRecords(ST_ORDERS)) {
    if (r.kind == SK_EDIT)
      continue;
    Action a;
    a.order = r.name;
    a.kind = r.kind;
    a.tech = Ref(ST_ORDERS, r.name, "tech", ST_TECHS);
    a.weapon = Ref(ST_ORDERS, r.name, "weapon", ST_WEAPONS);
    a.pWork = WorkOf(a.order, a.tech);
    a.blocks = StudioRecordJson(ST_ORDERS, r.name)["blocks"];
    if (a.pWork)
      a.weapon.clear();  // its classic work fires none
    for (const PortJsonMember &card : StudioDoc("types")["cards"].members)
      for (size_t i = 0; i < card.second.items.size(); ++i) {
        const PortJson &b = card.second.items[i];
        if (b["cast"].Str() == r.name)
          a.casts.push_back({card.first, i, b});
        if (!a.tech.empty() && b["research"].Str() == a.tech)
          a.research = card.first;
      }
    s_list.push_back(a);
  }
  return s_list;
}

const Action *ActionOf(const std::string &order) {
  for (const Action &a : Actions())
    if (a.order == order)
      return &a;
  return nullptr;
}

std::string CasterOf(const Action &a) {
  return a.casts.empty() ? std::string() : a.casts[0].card;
}

// What the tech asks for research: where, and its cost, "" for none.
std::string ResearchAt(const std::string &tech) {
  const PortJson &req = StudioRecordJson(ST_TECHS, tech)["requires"];
  for (const PortJson &q : req.items)
    if (q.kind == PortJson::OBJECT && q["at"].kind == PortJson::STRING)
      return q["at"].s;
  return std::string();
}

std::string Cap(std::string s) {
  if (!s.empty())
    s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
  return s;
}

// ---- an order's blocks (docs/mod-data.md, "Action blocks"): what a cast does, run in its weapon's place ---

struct BlockKind {
  const char *pszDo;
  const char *pszGroup;
  const char *pszLabel;
  const char *pszGlyph;
  const char *pszNew;  // the block a new one starts as
};
const BlockKind kBlockKinds[] = {
    {"fire", "Hit", "Fire a weapon", "weapon", "{\"do\":\"fire\"}"},
    {"damage", "Hit", "Damage in an area", "fx", "{\"do\":\"damage\",\"amount\":20,\"radius\":64,\"targets\":\"enemies\"}"},
    {"heal", "Change", "Heal", "plus", "{\"do\":\"heal\",\"amount\":20,\"radius\":64,\"targets\":\"allies\"}"},
    {"energy", "Change", "Restore energy", "tech", "{\"do\":\"energy\",\"amount\":25,\"targets\":\"allies\"}"},
    {"status", "Change", "Apply a status", "look", "{\"do\":\"status\",\"radius\":64,\"targets\":\"enemies\"}"},
    {"remove_statuses", "Change", "Remove statuses", "x", "{\"do\":\"remove_statuses\",\"targets\":\"allies\"}"},
    {"spawn", "Make", "Spawn units", "unit", "{\"do\":\"spawn\",\"unit\":\"zerg_zergling\",\"count\":2,\"seconds\":20}"},
    {"teleport", "Move", "Teleport the caster", "back", "{\"do\":\"teleport\"}"},
    {"sound", "Show", "Play a sound", "sound", "{\"do\":\"sound\",\"sound\":\"protoss_arbiter_pabfol00\"}"},
    {"show", "Show", "Show a sprite", "image", "{\"do\":\"show\",\"sprite\":\"thingy_recall\"}"},
    {"wait", "Flow", "Wait", "pause", "{\"do\":\"wait\",\"seconds\":1}"},
    {"repeat", "Flow", "Repeat", "again", "{\"do\":\"repeat\",\"times\":3,\"blocks\":[{\"do\":\"wait\",\"seconds\":1}]}"},
    {"script", "Script", "Call a script", "pen", "{\"do\":\"script\",\"call\":\"\"}"},
};

const BlockKind *BlockKindOf(const std::string &what) {
  for (const BlockKind &k : kBlockKinds)
    if (what == k.pszDo)
      return &k;
  return nullptr;
}

std::string WhoWords(const std::string &who) {
  return who == "allies" ? "its allies" : who == "own" ? "its own units" : who == "all" ? "every unit" : "the enemies";
}

// The units a block reaches, in words: the one aimed at, or those of a side near a place.
std::string ReachWords(const PortJson &b, const char *pszWho) {
  const bool bCaster = b["at"].Str() == "caster";
  const double r = b["radius"].Num(0);
  if (r <= 0)
    return bCaster ? "the caster" : "the unit aimed at";
  return WhoWords(b["targets"].Str(pszWho)) + std::string(b["buildings"].kind == PortJson::BOOL && b["buildings"].b ? ", buildings too," : "") +
         " within " + Fmt(r / 32.0) + " tiles of " + (bCaster ? "the caster" : "the place aimed at");
}

std::string BlockWords(const PortJson &b, const std::string &orderWeapon);

std::string BlocksWords(const PortJson &list, const std::string &orderWeapon) {
  std::string s;
  for (size_t i = 0; i < list.items.size(); ++i)
    s += (i == 0 ? "" : i + 1 == list.items.size() ? " and " : ", ") + BlockWords(list.items[i], orderWeapon);
  return s.empty() ? std::string("does nothing") : s;
}

std::string BlockWords(const PortJson &b, const std::string &orderWeapon) {
  const std::string what = b["do"].Str();
  const double n = b["amount"].Num(0);
  if (what == "fire") {
    const std::string w = b["weapon"].kind == PortJson::NONE ? orderWeapon : b["weapon"].kind == PortJson::STRING ? b["weapon"].s
                                                                                                                 : StudioRefName(ST_WEAPONS, static_cast<long long>(b["weapon"].d));
    return "fires " + (w.empty() ? std::string("its weapon") : StudioShowName(ST_WEAPONS, w));
  }
  if (what == "damage")
    return "deals " + Fmt(n) + (b["damage_type"].kind == PortJson::STRING ? " " + b["damage_type"].s : std::string()) + " damage to " +
           ReachWords(b, "enemies");
  if (what == "heal")
    return "heals " + Fmt(n) + " life of " + ReachWords(b, "allies");
  if (what == "energy")
    return (n >= 0 ? "gives " + Fmt(n) + " energy to " : "takes " + Fmt(-n) + " energy from ") + ReachWords(b, "allies");
  if (what == "status")
    return "puts " + (b["status"].Str().empty() ? std::string("a status not picked yet") : StudioStatusLabel(b["status"].Str())) + " on " +
           ReachWords(b, "enemies");
  if (what == "remove_statuses")
    return "takes " + (b["status"].Str().empty() ? std::string("every status of the mod's") : StudioStatusLabel(b["status"].Str())) + " off " +
           ReachWords(b, "allies");
  if (what == "spawn") {
    const double sec = b["seconds"].Num(0);
    const std::string unit = b["unit"].Str();
    const int nCount = static_cast<int>(b["count"].Num(1));
    return "makes " + std::to_string(nCount) + " " + (unit.empty() ? std::string("units") : Short(ST_UNITS, unit)) + (nCount == 1 ? "" : "s") + " at " +
           (b["at"].Str() == "caster" ? "the caster" : "the place aimed at") + (sec > 0 ? " for " + Fmt(sec) + " s" : std::string(" for good"));
  }
  if (what == "teleport")
    return "moves the caster to the place aimed at";
  if (what == "sound")
    return "plays " + StudioShowName(ST_SOUNDS, b["sound"].Str());
  if (what == "show")
    return "shows " + StudioShowName(ST_SPRITES, b["sprite"].Str()) + " at " + (b["at"].Str() == "caster" ? "the caster" : "the place aimed at");
  if (what == "wait") {
    const std::string after = b["blocks"].items.empty() ? std::string() : ", then " + BlocksWords(b["blocks"], orderWeapon);
    return "waits " + Fmt(b["seconds"].Num(1), 2) + " s" + after;
  }
  if (what == "repeat")
    return "repeats " + std::to_string(static_cast<int>(b["times"].Num(2))) + " times: " + BlocksWords(b["blocks"], orderWeapon);
  if (what == "script")
    return b["call"].Str().empty() ? std::string("calls a script not picked yet") : "calls " + b["call"].s + " of the scripts";
  return "does \"" + what + "\", which the engine does not run";
}

// The action in a sentence, from its blocks.
std::string Sentence(const Action &a) {
  std::string s;
  const bool bAimed = !a.casts.empty() && a.casts[0].button["target"].kind == PortJson::BOOL && a.casts[0].button["target"].b;
  const std::string &w = a.weapon;
  const long long nRange = w.empty() ? 0 : StudioNumber(ST_WEAPONS, w, "max_range");
  const long long nBehave = w.empty() ? 0 : StudioNumber(ST_WEAPONS, w, "behavior");
  if (a.pWork)
    s = std::string(!bAimed ? "On itself" : a.pWork->nAim == AIM_PLACE ? "Aimed at a place" : "Aimed at a unit") + ": " + a.pWork->pszWork +
        ", the classic " + a.pWork->pszLabel + "'s own work";
  else if (!bAimed || nBehave == 5)
    s = "Around itself";
  else
    s = std::string(nBehave == 2 ? "Aimed at a unit" : "Aimed at a unit or a place") + (nRange >= 4096 ? " anywhere" : " within " + Tiles(nRange));
  if (a.pWork) {
  } else if (!a.blocks.items.empty()) {
    s += ": it " + BlocksWords(a.blocks, a.weapon);
  } else if (!w.empty()) {
    const long long nDamage = StudioNumber(ST_WEAPONS, w, "damage");
    const long long nOuter = StudioNumber(ST_WEAPONS, w, "outer_splash");
    if (nDamage > 0)
      s += ": " + std::to_string(nDamage) + " damage" + (nOuter > 0 ? " in " + Fmt(nOuter / 32.0) + " tiles of where it lands" : std::string());
    const long long nExplosion = StudioNumber(ST_WEAPONS, w, "explosion");
    s += std::string(nDamage > 0 ? ", and " : ": ") + ExplosionWords(nExplosion);
    if (StatusFrames(nExplosion) > 0)
      s += ", for " + Fmt(StatusFrames(nExplosion) / 24.0) + " s";
  } else {
    s += ": it fires no weapon, so it does nothing yet";
  }
  if (!a.tech.empty()) {
    const long long nEnergy = StudioNumber(ST_TECHS, a.tech, "energy");
    s += ". Costs " + (nEnergy > 0 ? std::to_string(nEnergy) + " energy" : std::string("no energy"));
    const std::string at = ResearchAt(a.tech);
    if (!at.empty())
      s += "; researched at the " + Short(ST_UNITS, at) + " for " + std::to_string(StudioNumber(ST_TECHS, a.tech, "minerals")) + " / " +
           std::to_string(StudioNumber(ST_TECHS, a.tech, "gas")) + " in " + Fmt(StudioNumber(ST_TECHS, a.tech, "research_time") / 24.0) + " s";
  }
  return s + ".";
}

// ---- drawing pieces -----------------------------------------------------------------------------------------

void Lab(const char *psz) {
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), psz, 0.1f, true);
}

// Text that wraps at a width, at the cursor.
void Wrapped(const std::string &s, float fW, float fPx, int tok, int nFont = STUDIO_FONT_TEXT) {
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fW);
  StudioPushFont(nFont, fPx);
  ImGui::PushStyleColor(ImGuiCol_Text, C(tok));
  ImGui::TextWrapped("%s", s.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
}

// A field's label at the left of a row, the cursor put where its editor goes.
void RowLabel(const char *psz, float fW = 120.0f) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), psz, P(fW - 6.0f));
  ImGui::SetCursorScreenPos(ImVec2(p.x + P(fW), p.y));
}

// Words after an editor on its row, set in its middle.
void After(const std::string &s, int tok = TK_INK3) {
  const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
  ImGui::SameLine(0.0f, P(8.0f));
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float w = TW(STUDIO_FONT_TEXT, kStudioFsXs, s);
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, at.x, Mid(lo.y, hi.y - lo.y, kStudioFsXs), C(tok), s);
  ImGui::Dummy(ImVec2(w, hi.y - lo.y));
}

// A field of a record through the shared editor, with its label.
void FieldRow(int t, const std::string &name, const char *pszKey, const char *pszLabel) {
  const StudioField *f = StudioFieldOf(t, pszKey);
  StudioValue v;
  if (f == nullptr || !StudioGet(t, name, pszKey, &v))
    return;
  RowLabel(pszLabel);
  ImGui::PushID(pszKey);
  ImGui::BeginGroup();
  StudioFieldEditor(t, name, *f, v, Mine(t, name), false);
  ImGui::EndGroup();
  ImGui::PopID();
}

// A number of a record in the units a modder thinks in (tiles, seconds): scale is the file's
// value over the one shown.
void NumberRow(int t, const std::string &name, const char *pszKey, const char *pszLabel, double dScale, const char *pszUnit) {
  RowLabel(pszLabel);
  ImGui::PushID(pszKey);
  double d = StudioNumber(t, name, pszKey) / dScale;
  if (StudioNumber("##n", &d, 86.0f, 24.0f, Mine(t, name), 13.0f))
    Set(t, name, pszKey, Fmt(floor(d * dScale + 0.5)));
  if (pszUnit != nullptr)
    After(pszUnit);
  ImGui::PopID();
}

// A block: its group and title at its head, its fields inside, its ground drawn once its height is
// known (behind, on the draw list's first channel).
struct Block {
  ImDrawList *d = nullptr;
  ImVec2 a;
  float w = 0.0f;
  bool bWaiting = false;
};

void BlockBegin(Block &k, const char *pszGroup, const char *pszGlyph, const std::string &title, const std::string &words, float fW,
                bool bWaiting = false) {
  k.d = ImGui::GetWindowDrawList();
  k.a = ImGui::GetCursorScreenPos();
  k.w = fW;
  k.bWaiting = bWaiting;
  k.d->ChannelsSplit(2);
  k.d->ChannelsSetCurrent(1);
  const float x = k.a.x + P(12.0f), y = k.a.y + P(10.0f);
  StudioGlyph(k.d, pszGlyph, ImVec2(x, y), P(16.0f), C(bWaiting ? TK_INK_OFF : TK_LABEL));
  const float gx = x + P(24.0f) + Wide(k.d, 9.0f, x + P(24.0f), y + P(3.0f), C(TK_INK3), pszGroup, 0.1f) + P(10.0f);
  T(k.d, STUDIO_FONT_BOLD, 14.0f, gx, y, C(bWaiting ? TK_INK3 : TK_HI), title, fW - (gx - k.a.x) - P(12.0f));
  float ty = y + P(24.0f);
  if (!words.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(x, ty));
    Wrapped(words, fW - P(24.0f), kStudioFsS, bWaiting ? TK_INK3 : TK_INK2);
    ty = ImGui::GetCursorScreenPos().y + P(4.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(x, ty));
  ImGui::BeginGroup();
}

void BlockEnd(Block &k) {
  ImGui::Dummy(ImVec2(1, 1));
  ImGui::EndGroup();
  const float bottom = ImGui::GetItemRectMax().y + P(10.0f);
  k.d->ChannelsSetCurrent(0);
  const ImVec2 b(k.a.x + k.w, bottom);
  k.d->AddRectFilled(k.a, b, C(k.bWaiting ? TK_WELL : TK_PANE2), P(4.0f));
  k.d->AddRect(k.a, b, C(TK_LINE), P(4.0f));
  if (!k.bWaiting)
    k.d->AddRectFilled(k.a, ImVec2(k.a.x + P(3.0f), bottom), C(TK_SEL_LINE), P(4.0f), ImDrawFlags_RoundCornersLeft);
  k.d->ChannelsMerge();
  ImGui::SetCursorScreenPos(ImVec2(k.a.x, bottom + P(8.0f)));
  ImGui::Dummy(ImVec2(k.w, 0.0f));
}

// ---- the blocks' editor ------------------------------------------------------------------------------------

typedef std::vector<int> BlockPath;

PortJson EmptyList(void) {
  PortJson a;
  a.kind = PortJson::ARRAY;
  return a;
}

// The list the first nDepth steps of a path lead into (a repeat's or a wait's own blocks, made as
// needed), null when the path names none.
PortJson *ListAt(PortJson &blocks, const BlockPath &path, size_t nDepth) {
  PortJson *pList = &blocks;
  for (size_t i = 0; i < nDepth; ++i) {
    if (pList->kind != PortJson::ARRAY || path[i] < 0 || path[i] >= static_cast<int>(pList->items.size()))
      return nullptr;
    PortJson &b = pList->items[static_cast<size_t>(path[i])];
    if (StudioJsonMember(b, "blocks") == nullptr)
      StudioJsonPut(b, "blocks", EmptyList());
    pList = StudioJsonMember(b, "blocks");
  }
  return pList->kind == PortJson::ARRAY ? pList : nullptr;
}

PortJson *BlockAt(PortJson &blocks, const BlockPath &path) {
  if (path.empty())
    return nullptr;
  PortJson *pList = ListAt(blocks, path, path.size() - 1);
  if (pList == nullptr || path.back() < 0 || path.back() >= static_cast<int>(pList->items.size()))
    return nullptr;
  return &pList->items[static_cast<size_t>(path.back())];
}

std::string PathKey(const BlockPath &path) {
  std::string s;
  for (int n : path)
    s += "." + std::to_string(n);
  return s;
}

void PutBlocks(const std::string &order, const PortJson &blocks, const std::string &merge = std::string()) {
  StudioAsk("studio_action_blocks", "\"name\":" + Q(order) + ",\"blocks\":" + (blocks.items.empty() ? std::string("null") : PortJsonWrite(blocks)) +
                                        (merge.empty() ? std::string() : ",\"merge\":" + Q(merge)));
}

// One field of one block set (NONE takes it off), the whole list put as one edit.
void PutBlockField(const std::string &order, const PortJson &all, const BlockPath &path, const char *pszKey, const PortJson &v) {
  PortJson blocks = all;
  PortJson *pB = BlockAt(blocks, path);
  if (pB == nullptr)
    return;
  if (v.kind == PortJson::NONE)
    StudioJsonErase(*pB, pszKey);
  else
    StudioJsonPut(*pB, pszKey, v);
  PutBlocks(order, blocks, order + PathKey(path) + "." + pszKey);
}

// A number of a block, in the units a modder thinks in (scale is the file's over the one shown).
void BlockNumber(const std::string &order, const PortJson &all, const BlockPath &path, const PortJson &b, const char *pszKey,
                 const char *pszLabel, double dDefault, double dScale, double dMin, double dMax, const char *pszAfter, bool bMine) {
  RowLabel(pszLabel);
  ImGui::PushID(pszKey);
  double d = b[pszKey].Num(dDefault) / dScale;
  if (StudioNumber("##n", &d, 76.0f, 24.0f, bMine && b[pszKey].kind != PortJson::NONE, 13.0f)) {
    const double v = std::max(dMin, std::min(dMax, floor(d * dScale * 1000.0 + 0.5) / 1000.0));
    PutBlockField(order, all, path, pszKey, StudioJsonNumber(v));
  }
  if (pszAfter)
    After(pszAfter);
  ImGui::PopID();
}

// Where a block reaches: at the place aimed at or the caster, within a radius, which side.
void BlockReach(const std::string &order, const PortJson &all, const BlockPath &path, const PortJson &b, const char *pszWho, bool bMine) {
  static const char *const kAt[] = {"the place aimed at", "the caster"};
  RowLabel("Where");
  const int nAt = b["at"].Str() == "caster" ? 1 : 0;
  const int nNewAt = StudioSeg("##at", kAt, 2, nAt, 24.0f, kStudioFsXs);
  if (nNewAt != nAt)
    PutBlockField(order, all, path, "at", nNewAt ? StudioJsonString("caster") : PortJson());
  BlockNumber(order, all, path, b, "radius", "Radius", 0, 32.0, 0, 2048, "tiles; 0 is the one unit aimed at, or the caster", bMine);
  if (b["radius"].Num(0) > 0) {
    static const char *const kWho[] = {"enemies", "allies", "own", "all"};
    static const char *const kWhoShown[] = {"enemies", "allies", "its own", "every unit"};
    RowLabel("Whose units");
    const std::string who = b["targets"].Str(pszWho);
    int nWho = 0;
    for (int i = 0; i < 4; ++i)
      if (who == kWho[i])
        nWho = i;
    const int nNewWho = StudioSeg("##who", kWhoShown, 4, nWho, 24.0f, kStudioFsXs);
    if (nNewWho != nWho)
      PutBlockField(order, all, path, "targets", StudioJsonString(kWho[nNewWho]));
    ImGui::SameLine(0.0f, P(12.0f));
    bool bBuildings = b["buildings"].kind == PortJson::BOOL && b["buildings"].b;
    if (StudioCheck("##bld", "buildings too", &bBuildings))
      PutBlockField(order, all, path, "buildings", bBuildings ? PortJson{PortJson::BOOL, true} : PortJson());
  }
}

void BlockRecord(const std::string &order, const PortJson &all, const BlockPath &path, const PortJson &b, const char *pszKey, int t,
                 const char *pszLabel, const char *pszNone, bool bNone) {
  RowLabel(pszLabel);
  ImGui::PushID(pszKey);
  std::string now = b[pszKey].kind == PortJson::NUMBER ? StudioRefName(t, static_cast<long long>(b[pszKey].d)) : b[pszKey].Str();
  std::string picked;
  if (StudioRecordPick("##rec", t, now, 280.0f, &picked, bNone, 24.0f, pszNone))
    PutBlockField(order, all, path, pszKey, picked.empty() ? PortJson() : StudioJsonString(picked));
  ImGui::PopID();
}

void BlockAdd(const std::string &order, const PortJson &all, const BlockPath &inside, const char *pszId, const char *pszLabel, float fH);

// One block, at its depth, with its fields inline; its own blocks after it, one step in.
void BlockCard(const Action &a, const PortJson &all, const BlockPath &path, const PortJson &b, float x0, float fW, bool bMine,
               size_t nSiblings) {
  const std::string what = b["do"].Str();
  const BlockKind *pK = BlockKindOf(what);
  const float indent = P(28.0f) * static_cast<float>(path.size() - 1);
  ImGui::SetCursorScreenPos(ImVec2(x0 + indent, ImGui::GetCursorScreenPos().y));
  const float w = fW - indent;
  ImGui::PushID(PathKey(path).c_str());
  Block k;
  BlockBegin(k, pK ? pK->pszGroup : "Refused", pK ? pK->pszGlyph : "needs", pK ? pK->pszLabel : what.c_str(),
             Cap(BlockWords(b, a.weapon)) + ".", w - P(96.0f), pK == nullptr);
  k.w = w;
  // its order and its removal, at its head's right
  if (bMine) {
    const ImVec2 back = ImGui::GetCursorScreenPos();
    const float hx = k.a.x + w - P(10.0f) - P(22.0f) * 3 - P(8.0f);
    ImGui::SetCursorScreenPos(ImVec2(hx, k.a.y + P(8.0f)));
    const int nAt = path.back();
    if (StudioButton("##up", "", "up", SB_FLAT, 22.0f, 0.0f, kStudioFsXs, nAt > 0))
      StudioAsk("studio_block_move", "\"name\":" + Q(a.order) + ",\"path\":[" + PathKey(path).substr(1) + "],\"by\":-1");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Earlier");
    ImGui::SameLine(0.0f, P(4.0f));
    if (StudioButton("##down", "", "down", SB_FLAT, 22.0f, 0.0f, kStudioFsXs, nAt + 1 < static_cast<int>(nSiblings)))
      StudioAsk("studio_block_move", "\"name\":" + Q(a.order) + ",\"path\":[" + PathKey(path).substr(1) + "],\"by\":1");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Later");
    ImGui::SameLine(0.0f, P(4.0f));
    if (StudioButton("##rm", "", "x", SB_FLAT, 22.0f, 0.0f, kStudioFsXs))
      StudioAsk("studio_block_remove", "\"name\":" + Q(a.order) + ",\"path\":[" + PathKey(path).substr(1) + "]");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Take this block away, with what is inside it");
    ImGui::SetCursorScreenPos(back);
  }
  if (!bMine)
    ImGui::BeginDisabled();
  const std::string &o = a.order;
  if (what == "fire") {
    BlockRecord(o, all, path, b, "weapon", ST_WEAPONS, "Weapon", "the order's own weapon", true);
  } else if (what == "damage") {
    BlockNumber(o, all, path, b, "amount", "Damage", 20, 1, 1, 10000, "before armour and size", bMine);
    static const char *const kTypes[] = {"normal", "explosive", "concussive", "independent"};
    RowLabel("Type");
    const std::string type = b["damage_type"].Str("normal");
    int nType = 0;
    for (int i = 0; i < 4; ++i)
      if (type == kTypes[i])
        nType = i;
    const int nNewType = StudioSeg("##type", kTypes, 4, nType, 24.0f, kStudioFsXs);
    if (nNewType != nType)
      PutBlockField(o, all, path, "damage_type", nNewType ? StudioJsonString(kTypes[nNewType]) : PortJson());
    BlockReach(o, all, path, b, "enemies", bMine);
  } else if (what == "heal" || what == "energy") {
    BlockNumber(o, all, path, b, "amount", what == "heal" ? "Life" : "Energy", 20, 1, what == "heal" ? 1 : -250, 10000,
                what == "heal" ? "up to its most" : "below 0 takes energy away", bMine);
    BlockReach(o, all, path, b, "allies", bMine);
  } else if (what == "status" || what == "remove_statuses") {
    RowLabel("Status");
    std::string picked;
    if (StudioStatusPick("##st", b["status"].Str(), 280.0f, &picked, what == "remove_statuses"))
      PutBlockField(o, all, path, "status", picked.empty() ? PortJson() : StudioJsonString(picked));
    if (what == "remove_statuses" && b["status"].Str().empty())
      After("none: every status of the mod's");
    if (!b["status"].Str().empty()) {
      ImGui::SameLine(0.0f, P(8.0f));
      if (StudioButton("##open", "Open it", "link", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
        StudioStatusesShow(b["status"].Str());
    }
    BlockReach(o, all, path, b, what == "status" ? "enemies" : "allies", bMine);
  } else if (what == "spawn") {
    BlockRecord(o, all, path, b, "unit", ST_UNITS, "Unit", "pick a unit", false);
    BlockNumber(o, all, path, b, "count", "How many", 1, 1, 1, 64, nullptr, bMine);
    BlockNumber(o, all, path, b, "seconds", "For", 0, 1, 0, 2700, "seconds; 0 keeps them for good", bMine);
    static const char *const kAt[] = {"the place aimed at", "the caster"};
    RowLabel("Where");
    const int nAt = b["at"].Str() == "caster" ? 1 : 0;
    const int nNewAt = StudioSeg("##at", kAt, 2, nAt, 24.0f, kStudioFsXs);
    if (nNewAt != nAt)
      PutBlockField(o, all, path, "at", nNewAt ? StudioJsonString("caster") : PortJson());
  } else if (what == "teleport") {
    Wrapped("The caster goes to the nearest free place to where it was aimed, with Recall's flash; a building stays.", w - P(30.0f),
            kStudioFsXs, TK_INK3);
  } else if (what == "sound") {
    BlockRecord(o, all, path, b, "sound", ST_SOUNDS, "Sound", "pick a sound", false);
    if (!b["sound"].Str().empty()) {
      ImGui::SameLine(0.0f, P(8.0f));
      StudioPlayButton("##play", b["sound"].Str());
    }
  } else if (what == "show") {
    BlockRecord(o, all, path, b, "sprite", ST_SPRITES, "Sprite", "pick a sprite", false);
    static const char *const kAt[] = {"the place aimed at", "the caster"};
    RowLabel("Where");
    const int nAt = b["at"].Str() == "caster" ? 1 : 0;
    const int nNewAt = StudioSeg("##at", kAt, 2, nAt, 24.0f, kStudioFsXs);
    if (nNewAt != nAt)
      PutBlockField(o, all, path, "at", nNewAt ? StudioJsonString("caster") : PortJson());
  } else if (what == "wait") {
    BlockNumber(o, all, path, b, "seconds", "Waits", 1, 1, 1.0 / 24.0, 2700, "seconds at Fastest; the blocks after it, and inside it, follow",
                bMine);
  } else if (what == "repeat") {
    BlockNumber(o, all, path, b, "times", "Times", 2, 1, 1, 1000, "the blocks inside it, in turn; a wait inside paces them", bMine);
  } else if (what == "script") {
    // a function of the mod's scripts, picked from those their tops give (studio_scripts.cpp)
    RowLabel("Function");
    const std::string now = b["call"].Str();
    const std::vector<std::string> fns = StudioScriptFunctions();
    if (StudioSelectBegin("##fn", now.empty() ? "pick a function" : now.c_str(), 240.0f, 24.0f, kStudioFsXs)) {
      for (const std::string &f : fns)
        if (StudioItem(f.c_str(), f == now))
          PutBlockField(o, all, path, "call", StudioJsonString(f));
      if (fns.empty())
        StudioItem("the scripts give no function yet", false, nullptr, false);
      StudioSelectEnd();
    }
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##openfn", now.empty() ? "Write one" : "Open it", "link", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioScriptsOpenFunction(now);
    Wrapped("Called where the cast's program has it, with e.caster, e.target, e.x, e.y and e.order.", w - P(30.0f),
            kStudioFsXs, TK_INK3);
  } else {
    Wrapped("The engine runs no block \"" + what + "\": the loader refuses the mod until it is taken away.", w - P(30.0f), kStudioFsXs, TK_BAD);
  }
  if (!bMine)
    ImGui::EndDisabled();
  BlockEnd(k);
  // its own blocks, one step in
  if (what == "repeat" || what == "wait") {
    const PortJson &in = b["blocks"];
    for (size_t i = 0; i < in.items.size(); ++i) {
      BlockPath sub = path;
      sub.push_back(static_cast<int>(i));
      BlockCard(a, all, sub, in.items[i], x0, fW, bMine, in.items.size());
    }
    if (bMine) {
      ImGui::SetCursorScreenPos(ImVec2(x0 + indent + P(28.0f), ImGui::GetCursorScreenPos().y));
      BlockAdd(o, all, path, "##addin", what == "repeat" ? "Add a block to repeat" : "Add a block after the wait", 24.0f);
      ImGui::Dummy(ImVec2(1, P(6.0f)));
    }
  }
  ImGui::PopID();
}

// A pulldown of the blocks there are, by group: the one picked added at the end of the list a path
// leads into.
void BlockAdd(const std::string &order, const PortJson &all, const BlockPath &inside, const char *pszId, const char *pszLabel, float fH) {
  ImGui::PushID(pszId);
  if (StudioButton("##add", pszLabel, "plus", SB_GOOD, fH, 0.0f, kStudioFsXs))
    ImGui::OpenPopup("##kinds");
  if (ImGui::BeginPopup("##kinds")) {
    const char *pszGroup = "";
    for (const BlockKind &k : kBlockKinds) {
      if (strcmp(pszGroup, k.pszGroup) != 0) {
        if (pszGroup[0])
          StudioItemSeparator();
        StudioLabel(STUDIO_FONT_WIDE, 9.0f, C(TK_INK3), k.pszGroup, 0.1f, true);
        pszGroup = k.pszGroup;
      }
      if (StudioItem(k.pszLabel)) {
        PortJson blocks = all.kind == PortJson::ARRAY ? all : EmptyList();
        PortJson *pList = ListAt(blocks, inside, inside.size());
        PortJson nb;
        PortJsonParse(k.pszNew, &nb);
        if (pList) {
          pList->items.push_back(nb);
          PutBlocks(order, blocks);
        }
      }
    }
    StudioItemSeparator();
    StudioLabel(STUDIO_FONT_WIDE, 9.0f, C(TK_INK3), "Need scripting, not built yet", 0.1f, true);
    for (const Waiting &wt : kWaiting)
      StudioItem((std::string(wt.pszGroup) + ": " + wt.pszLabel).c_str(), false, nullptr, false);
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

// An action's blocks: what the cast does when it is cast, in place of its weapon, whose range it keeps.
bool g_bScrollBlocks = false;  // studio_actions {show, blocks: true}: the blocks brought into view once

void BlocksSection(const Action &a, float fW, bool bMine) {
  const PortJson &all = a.blocks;
  if (g_bScrollBlocks) {
    ImGui::SetScrollHereY(0.0f);
    g_bScrollBlocks = false;
  }
  Block k;
  BlockBegin(k, "Range", "try", "Cast within " + (a.weapon.empty() ? std::string("its weapon's range") : Tiles(StudioNumber(ST_WEAPONS, a.weapon, "max_range"))),
             "Its weapon now gives only the range the caster walks into and its cooldown; the blocks below do the work, run in turn "
             "as the cast goes off.",
             fW);
  ImGui::PushID("range");
  if (!a.weapon.empty())
    NumberRow(ST_WEAPONS, a.weapon, "max_range", "Range", 32.0, "tiles; 128 or more reaches across the map");
  ImGui::PopID();
  BlockEnd(k);
  const float x0 = ImGui::GetCursorScreenPos().x;
  for (size_t i = 0; i < all.items.size(); ++i)
    BlockCard(a, all, BlockPath{static_cast<int>(i)}, all.items[i], x0, fW, bMine, all.items.size());
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  if (bMine) {
    BlockAdd(a.order, all, BlockPath(), "##addtop", "Add a block", 28.0f);
    ImGui::SameLine(0.0f, P(10.0f));
    if (StudioButton("##noblocks", "Fire its weapon instead", "weapon", SB_PLAIN, 28.0f, 0.0f, kStudioFsXs))
      PutBlocks(a.order, EmptyList());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The blocks taken away: the cast fires its weapon again, as the classic spell does");
  }
  ImGui::Dummy(ImVec2(1, P(8.0f)));
}

// A unit's passive aura: a status put on the units near it every so often.
void AuraEditor(const std::string &unit, float fW) {
  const PortJson &aura = StudioRecordJson(ST_UNITS, unit)["aura"];
  const bool bHas = aura.kind == PortJson::OBJECT;
  StudioRecord ru;
  const bool bMine = StudioRecordOf(ST_UNITS, unit, &ru) && ru.kind != SK_DEPENDENCY;
  Block k;
  std::string words;
  if (bHas) {
    const double every = aura["every"].Num(1), r = aura["radius"].Num(160);
    const bool bSelf = !(aura["self"].kind == PortJson::BOOL && !aura["self"].b);
    words = "Every " + Fmt(every, 2) + " s it puts " + StudioStatusLabel(aura["status"].Str()) + " on " + WhoWords(aura["targets"].Str("allies")) +
            (aura["buildings"].kind == PortJson::BOOL && aura["buildings"].b ? ", buildings too," : "") + " within " + Fmt(r / 32.0) + " tiles" +
            (bSelf ? ", itself among them." : ", not on itself.") + " " + StudioStatusSentence(aura["status"].Str());
  } else {
    words = "No aura: a passive puts a status on the units near it every so often, while it is finished and in the open.";
  }
  BlockBegin(k, "Passive", "look", bHas ? "Aura of " + StudioStatusLabel(aura["status"].Str()) : std::string("No aura"), words, fW);
  ImGui::PushID("aura");
  auto put = [&](const char *pszKey, const PortJson &v) {
    PortJson na = bHas ? aura : StudioJsonObject();
    if (v.kind == PortJson::NONE)
      StudioJsonErase(na, pszKey);
    else
      StudioJsonPut(na, pszKey, v);
    if (na["status"].Str().empty())
      return;
    StudioAsk("studio_unit_aura", "\"unit\":" + Q(unit) + ",\"aura\":" + PortJsonWrite(na));
  };
  RowLabel("Status");
  std::string picked;
  if (StudioStatusPick("##st", aura["status"].Str(), 280.0f, &picked, false))
    put("status", StudioJsonString(picked));
  if (bHas) {
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##open", "Open it", "link", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioStatusesShow(aura["status"].Str());
    RowLabel("Radius");
    double r = aura["radius"].Num(160) / 32.0;
    if (StudioNumber("##r", &r, 76.0f, 24.0f, aura["radius"].kind != PortJson::NONE, 13.0f))
      put("radius", StudioJsonNumber(std::max(1.0, std::min(2048.0, floor(r * 32.0 + 0.5)))));
    After("tiles");
    RowLabel("Every");
    double e = aura["every"].Num(1);
    if (StudioNumber("##e", &e, 76.0f, 24.0f, aura["every"].kind != PortJson::NONE, 13.0f))
      put("every", StudioJsonNumber(std::max(1.0 / 24.0, std::min(600.0, e))));
    After("seconds");
    static const char *const kWho[] = {"allies", "enemies", "own", "all"};
    static const char *const kWhoShown[] = {"allies", "enemies", "its own", "every unit"};
    RowLabel("Whose units");
    int nWho = 0;
    for (int i = 0; i < 4; ++i)
      if (aura["targets"].Str("allies") == kWho[i])
        nWho = i;
    const int nNewWho = StudioSeg("##who", kWhoShown, 4, nWho, 24.0f, kStudioFsXs);
    if (nNewWho != nWho)
      put("targets", nNewWho ? StudioJsonString(kWho[nNewWho]) : PortJson());
    RowLabel("Also");
    bool bSelf = !(aura["self"].kind == PortJson::BOOL && !aura["self"].b);
    if (StudioCheck("##self", "on itself", &bSelf))
      put("self", bSelf ? PortJson() : PortJson{PortJson::BOOL, false});
    ImGui::SameLine(0.0f, P(14.0f));
    bool bB = aura["buildings"].kind == PortJson::BOOL && aura["buildings"].b;
    if (StudioCheck("##bld", "on buildings", &bB))
      put("buildings", bB ? PortJson{PortJson::BOOL, true} : PortJson());
    ImGui::Dummy(ImVec2(1, P(4.0f)));
    if (StudioButton("##rm", "No aura", "x", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, bMine))
      StudioAsk("studio_unit_aura", "\"unit\":" + Q(unit) + ",\"remove\":true");
  }
  ImGui::PopID();
  BlockEnd(k);
}

// ---- the action's editor ------------------------------------------------------------------------------------

// A card's list with one button changed (or taken away when b is NONE), put as one edit.
void PutButton(const std::string &card, size_t nIndex, const PortJson &b) {
  PortJson list = StudioDoc("types")["cards"][card.c_str()];
  if (list.kind != PortJson::ARRAY || nIndex >= list.items.size())
    return;
  if (b.kind == PortJson::NONE)
    list.items.erase(list.items.begin() + static_cast<long>(nIndex));
  else
    list.items[nIndex] = b;
  std::string fields = "\"doc\":\"types\",\"path\":[\"cards\"," + Q(card) + "]";
  if (!list.items.empty())
    fields += ",\"value\":" + PortJsonWrite(list);
  StudioAsk("studio_put", fields);
}

void ButtonBlock(const Action &a, float fW, bool bMine) {
  Block k;
  if (a.casts.empty()) {
    BlockBegin(k, "Button", "cards", "No button casts it", "Nothing casts this order yet: add it to a unit's card with Add an ability.", fW);
    BlockEnd(k);
    return;
  }
  const Cast &c = a.casts[0];
  const PortJson &b = c.button;
  const std::string key = b["hotkey"].Str("?");
  BlockBegin(k, "Button", "cards", "On the " + StudioShowName(ST_UNITS, c.card) + "'s card",
             "Place " + Fmt(b["position"].Num(0)) + ", key " + static_cast<char>(toupper(static_cast<unsigned char>(key[0]))) +
                 (a.casts.size() > 1 ? "; " + std::to_string(a.casts.size() - 1) + " more cards cast it too" : std::string()),
             fW);
  if (!bMine)
    ImGui::BeginDisabled();
  ImGui::PushID("button");
  RowLabel("Place");
  static const char *const kPlaces[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
  const int nPlace = static_cast<int>(b["position"].Num(1)) - 1;
  const int nNew = StudioSeg("##place", kPlaces, 9, nPlace, 24.0f, kStudioFsXs);
  if (nNew != nPlace) {
    PortJson nb = b;
    StudioJsonPut(nb, "position", StudioJsonNumber(nNew + 1));
    PutButton(c.card, c.nIndex, nb);
  }
  RowLabel("Hotkey");
  static char s_szKey[4];
  static std::string s_keyFor;
  if (s_keyFor != a.order + c.card)
    snprintf(s_szKey, sizeof s_szKey, "%s", key.c_str()), s_keyFor = a.order + c.card;
  StudioInput("##key", s_szKey, 2, 40.0f, 24.0f, "?", nullptr, nullptr, true);
  if (ImGui::IsItemDeactivated() && s_szKey[0] && Lower(s_szKey) != Lower(key)) {
    PortJson nb = b;
    const char ch = static_cast<char>(tolower(static_cast<unsigned char>(s_szKey[0])));
    StudioJsonPut(nb, "hotkey", StudioJsonString(std::string(1, ch)));
    // the text's marked letter follows the key
    std::string text = b["text"].Str();
    std::string plain;
    for (size_t i = 0; i < text.size(); ++i)
      if (text[i] == '<' && i + 2 < text.size() && text[i + 2] == '>')
        plain += text[i + 1], i += 2;
      else
        plain += text[i];
    StudioJsonPut(nb, "text", StudioJsonString(Marked(plain, ch)));
    PutButton(c.card, c.nIndex, nb);
  }
  unsigned u = 0;
  std::string keys;
  Taken(c.card, &u, &keys, a.order);
  if (keys.find(static_cast<char>(tolower(static_cast<unsigned char>(key[0])))) != std::string::npos)
    After("another button of the card has this key", TK_BAD);
  RowLabel("Text");
  static char s_szText[128];
  static std::string s_textFor;
  if (s_textFor != a.order + c.card + b["text"].Str())
    snprintf(s_szText, sizeof s_szText, "%s", b["text"].Str().c_str()), s_textFor = a.order + c.card + b["text"].Str();
  StudioInput("##text", s_szText, sizeof s_szText, 300.0f, 24.0f, "the button's words, its key as <K>");
  if (ImGui::IsItemDeactivated() && b["text"].Str() != s_szText) {
    PortJson nb = b;
    StudioJsonPut(nb, "text", StudioJsonString(s_szText));
    PutButton(c.card, c.nIndex, nb);
  }
  RowLabel("Aimed");
  bool bAimed = b["target"].kind == PortJson::BOOL && b["target"].b;
  if (StudioCheck("##aim", bAimed ? "the player aims it at a unit or a place" : "cast at once where the caster stands", &bAimed)) {
    PortJson nb = b;
    if (bAimed)
      StudioJsonPut(nb, "target", PortJson{PortJson::BOOL, true});
    else
      StudioJsonErase(nb, "target");
    PutButton(c.card, c.nIndex, nb);
  }
  RowLabel("The computer");
  const std::string ai = b["ai"].Str();
  if (StudioSelectBegin("##ai", AiWords(ai).c_str(), 360.0f, 24.0f)) {
    if (StudioItem("never casts it", ai.empty())) {
      PortJson nb = b;
      StudioJsonErase(nb, "ai");
      PutButton(c.card, c.nIndex, nb);
    }
    static const char *const kAi[] = {"enemy_cluster", "psionic_storm", "yamato_gun", "plague", "dark_swarm", "ensnare",
                                      "spawn_broodlings", "parasite", "stasis_field", "emp_shockwave", "irradiate",
                                      "defensive_matrix", "disruption_web", "restoration", "optical_flare", "feedback",
                                      "mind_control", "maelstrom", "lockdown", "hallucination"};
    for (const char *psz : kAi)
      if (StudioItem(AiWords(psz).c_str(), ai == psz)) {
        PortJson nb = b;
        StudioJsonPut(nb, "ai", StudioJsonString(psz));
        PutButton(c.card, c.nIndex, nb);
      }
    StudioSelectEnd();
  }
  ImGui::PopID();
  if (!bMine)
    ImGui::EndDisabled();
  BlockEnd(k);
}

void CostBlock(const Action &a, float fW) {
  Block k;
  if (a.tech.empty()) {
    BlockBegin(k, "Cost", "tech", "No tech", "The order names no tech: it costs nothing and needs nothing.", fW);
    BlockEnd(k);
    return;
  }
  const bool bMine = Mine(ST_TECHS, a.tech);
  const std::string at = ResearchAt(a.tech);
  BlockBegin(k, "Cost and needs", "tech", StudioShowName(ST_TECHS, a.tech),
             at.empty() ? "Needs no research: a caster has it from the start." : "Researched at the " + Short(ST_UNITS, at) + " first.", fW);
  ImGui::PushID("cost");
  NumberRow(ST_TECHS, a.tech, "energy", "Energy", 1.0, "of the caster's, taken as it casts");
  RowLabel("Research");
  if (!bMine)
    ImGui::BeginDisabled();
  std::string picked;
  if (StudioRecordPick("##at", ST_UNITS, at, 220.0f, &picked, true, 24.0f, "none: no research"))
    StudioAsk("studio_action_research", "\"name\":" + Q(a.order) + ",\"at\":" + Q(picked));
  if (!bMine)
    ImGui::EndDisabled();
  if (!at.empty()) {
    NumberRow(ST_TECHS, a.tech, "minerals", "Minerals", 1.0, nullptr);
    NumberRow(ST_TECHS, a.tech, "gas", "Gas", 1.0, nullptr);
    NumberRow(ST_TECHS, a.tech, "research_time", "Time", 24.0, "seconds at Fastest");
    if (a.research.empty()) {
      const ImVec2 q = ImGui::GetCursorScreenPos();
      T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, q.x, q.y, C(TK_WARN),
        "No button researches it yet: the Card view puts one on the " + Short(ST_UNITS, at) + "'s card.");
      ImGui::Dummy(ImVec2(1, P(18.0f)));
    }
  }
  ImGui::PopID();
  BlockEnd(k);
}

void HitBlock(const Action &a, float fW) {
  Block k;
  if (a.pWork) {
    BlockBegin(k, "Hit", "fx", std::string("Do as ") + a.pWork->pszLabel + " does",
               std::string(a.pWork->pszWork) + ". The classic order does this itself, with the classic numbers; the action's tech "
               "gives its energy and who may cast it.",
               fW);
    BlockEnd(k);
    return;
  }
  if (a.weapon.empty()) {
    BlockBegin(k, "Hit", "weapon", "Fires nothing", "The order names no weapon, so the cast does nothing: give it one in Detailed.", fW);
    BlockEnd(k);
    return;
  }
  const std::string &w = a.weapon;
  const long long nDamage = StudioNumber(ST_WEAPONS, w, "damage");
  BlockBegin(k, "Hit", "weapon", "Fire " + StudioShowName(ST_WEAPONS, w),
             nDamage > 0 ? std::to_string(nDamage) + " damage a hit, " + Tiles(StudioNumber(ST_WEAPONS, w, "max_range")) + " away at most"
                         : std::string("It does no damage: its explosion is what it does."),
             fW);
  ImGui::PushID("hit");
  NumberRow(ST_WEAPONS, w, "damage", "Damage", 1.0, nullptr);
  FieldRow(ST_WEAPONS, w, "damage_type", "Damage type");
  NumberRow(ST_WEAPONS, w, "max_range", "Range", 32.0, "tiles; 128 or more reaches across the map");
  RowLabel("Splash");
  const bool bMine = Mine(ST_WEAPONS, w);
  static const char *const kRings[] = {"inner_splash", "middle_splash", "outer_splash"};
  for (int i = 0; i < 3; ++i) {
    if (i)
      ImGui::SameLine(0.0f, P(6.0f));
    ImGui::PushID(i);
    double d = static_cast<double>(StudioNumber(ST_WEAPONS, w, kRings[i]));
    if (StudioNumber("##ring", &d, 64.0f, 24.0f, bMine, 13.0f))
      Set(ST_WEAPONS, w, kRings[i], Fmt(floor(d + 0.5)));
    ImGui::PopID();
  }
  After("px: full damage, half, a quarter");
  FieldRow(ST_WEAPONS, w, "behavior", "It travels");
  FieldRow(ST_WEAPONS, w, "target_flags", "It can hit");
  RowLabel("On a hit");
  {
    std::string picked;
    const std::string now = StudioRecordJson(ST_WEAPONS, w)["status"].Str();
    if (!bMine)
      ImGui::BeginDisabled();
    if (StudioStatusPick("##onhit", now, 260.0f, &picked, true))
      StudioAsk("studio_weapon_status", "\"weapon\":" + Q(w) + ",\"status\":" + Q(picked));
    if (!bMine)
      ImGui::EndDisabled();
    After(now.empty() ? "no status on what it damages" : "put on every unit it damages");
  }
  ImGui::PopID();
  BlockEnd(k);
}

void StatusBlock(const Action &a, float fW) {
  if (a.weapon.empty())
    return;
  Block k;
  const long long n = StudioNumber(ST_WEAPONS, a.weapon, "explosion");
  const long long nFrames = StatusFrames(n);
  BlockBegin(k, "Change", "look", "Then it " + std::string(ExplosionWords(n)),
             nFrames > 0 ? "For " + Fmt(nFrames / 24.0) + " s at Fastest (" + std::to_string(nFrames) +
                               " frames), the classic time; the weapon's explosion is the status it puts on what it hits."
                         : std::string("The weapon's explosion is the status it puts on what it hits, for the classic time."),
             fW);
  ImGui::PushID("status");
  FieldRow(ST_WEAPONS, a.weapon, "explosion", "Explosion");
  ImGui::PopID();
  BlockEnd(k);
}

// Blocks for an action that fires its weapon: what they add, and the switch to them.
void UseBlocks(const Action &a, float fW) {
  Block k;
  BlockBegin(k, "Blocks", "fx", "Do more than fire its weapon",
             "Blocks do what the cast does, in turn: damage or heal in an area, put on or take off statuses, spawn units for a while, "
             "teleport the caster, play a sound or show a sprite, wait and repeat. The first block fires this weapon, so the cast does "
             "what it does now until more are added.",
             fW);
  if (StudioButton("##useblocks", "Use blocks", "plus", SB_GOOD, 26.0f, 0.0f, kStudioFsS)) {
    PortJson blocks = EmptyList(), fire;
    PortJsonParse("{\"do\":\"fire\"}", &fire);
    blocks.items.push_back(fire);
    PutBlocks(a.order, blocks);
  }
  ImGui::SameLine(0.0f, P(12.0f));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "Not yet: if, for each, pull or push, a hallucination (they need scripting)");
  BlockEnd(k);
}

void LinkButton(const char *pszId, int t, const std::string &name, const char *pszGlyph, const char *pszAs) {
  if (name.empty())
    return;
  const std::string label = std::string(pszAs) + ": " + StudioShowName(t, name);
  if (StudioButton(pszId, label.c_str(), pszGlyph, SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
    StudioOpenDoc(t, name, 0);
  ImGui::SameLine(0.0f, P(6.0f));
}

// The whole of an action: its sentence, its blocks, the records it is made of.
void ActionEditor(const std::string &order, float fW) {
  const Action *pA = ActionOf(order);
  if (pA == nullptr) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "No such action in the mod.");
    return;
  }
  const Action a = *pA;  // a copy: an edit made below looks the list up again
  ImDrawList *d = ImGui::GetWindowDrawList();
  const bool bMine = a.kind == SK_OWN;
  ImVec2 p = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 22.0f, p.x, p.y, C(TK_HI), StudioShowName(ST_ORDERS, order), fW);
  float cx = p.x;
  const float cy = p.y + P(32.0f);
  StudioRecord ro;
  const bool bRec = StudioRecordOf(ST_ORDERS, order, &ro);
  if (!bMine)
    cx += StudioChip(d, ImVec2(cx, cy), ("from " + ro.mod + ", read only").c_str(), CH_GOLD) + P(6.0f);
  if (bRec && !ro.base.empty())
    cx += StudioChip(d, ImVec2(cx, cy), ("from " + Short(ST_ORDERS, ro.base)).c_str()) + P(6.0f);
  if (!a.casts.empty())
    cx += StudioChip(d, ImVec2(cx, cy), ("cast by the " + Short(ST_UNITS, CasterOf(a))).c_str(), CH_GOOD) + P(6.0f);
  ImGui::SetCursorScreenPos(ImVec2(p.x, cy + P(28.0f)));
  Wrapped(Sentence(a), fW, 15.0f, TK_INK);
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  ButtonBlock(a, fW, bMine);
  CostBlock(a, fW);
  if (!a.blocks.items.empty()) {
    BlocksSection(a, fW, bMine);
  } else {
    HitBlock(a, fW);
    StatusBlock(a, fW);
    if (bMine && !a.pWork && !a.weapon.empty())
      UseBlocks(a, fW);
  }
  // what it is made of in the tables
  Lab("Made of");
  LinkButton("##lo", ST_ORDERS, a.order, "action", "order");
  LinkButton("##lt", ST_TECHS, a.tech, "tech", "tech");
  LinkButton("##lw", ST_WEAPONS, a.weapon, "weapon", "weapon");
  ImGui::NewLine();
  if (bMine) {
    ImGui::Dummy(ImVec2(1, P(4.0f)));
    if (StudioButton("##del", "Delete the action", "x", SB_PLAIN, 26.0f, 0.0f, kStudioFsS))
      StudioAsk("studio_delete_action", "\"name\":" + Q(order));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Its order, tech, weapon and buttons, as one edit: Undo puts them back");
  }
}

// ---- the action maker ---------------------------------------------------------------------------------------

struct Maker {
  int nSpell = 0;
  char szLabel[64] = "";
  char szFind[48] = "";
  std::string caster;
  std::string at;  // where it is researched, "" for no research
  double dEnergy = -1, dDamage = -1, dRange = -1, dMinerals = 100, dGas = 100, dTime = 60;
  int nPlace = 0;  // 0 for the first free
  bool bAi = true;
  bool bEnergy = true;
} g_make;

void MakerReset(const std::string &caster) {
  g_make = Maker();
  g_make.caster = caster;
}

std::string MakerLabel(void) {
  return g_make.szLabel[0] ? std::string(g_make.szLabel) : "New " + std::string(kSpells[g_make.nSpell].pszLabel);
}

bool Taken(const std::string &key) {
  for (int t : {ST_ORDERS, ST_TECHS, ST_WEAPONS}) {
    StudioRecord r;
    if (StudioRecordOf(t, key, &r))
      return true;
  }
  return false;
}

double SpellNumber(int t, const char *pszName, const char *pszKey) {
  return pszName ? static_cast<double>(StudioNumber(t, pszName, pszKey)) : 0.0;
}

std::string MakeFields(void) {
  const Spell &s = kSpells[g_make.nSpell];
  const std::string label = MakerLabel(), card = CardOf(g_make.caster);
  const char key = FreeKey(card, label);
  std::string v = "\"energy\":" + Fmt(g_make.dEnergy >= 0 ? g_make.dEnergy : SpellNumber(ST_TECHS, s.pszTech, "energy"));
  if (g_make.dDamage >= 0)
    v += ",\"damage\":" + Fmt(g_make.dDamage);
  if (g_make.dRange >= 0)
    v += ",\"max_range\":" + Fmt(floor(g_make.dRange * 32.0 + 0.5));
  if (!g_make.at.empty())
    v += ",\"at\":" + Q(g_make.at) + ",\"minerals\":" + Fmt(g_make.dMinerals) + ",\"gas\":" + Fmt(g_make.dGas) +
         ",\"research_time\":" + Fmt(floor(g_make.dTime * 24.0 + 0.5));
  v += ",\"hotkey\":" + Q(std::string(1, key)) + ",\"position\":" + std::to_string(g_make.nPlace ? g_make.nPlace : FreePlace(card)) +
       ",\"text\":" + Q(Marked(label, key));
  if (!g_make.bAi)
    v += ",\"ai\":\"\"";
  if (!g_make.bEnergy)
    v += ",\"give_energy\":false";
  return "\"unit\":" + Q(g_make.caster) + ",\"from\":" + Q(s.pszKey) + ",\"label\":" + Q(label) + ",\"name\":" + Q(KeyOf(label)) +
         ",\"values\":{" + v + "}";
}

std::string g_selOrder;  // the action chosen in the Effects workspace
int g_nSelT = -1;        // or a record of another kind
std::string g_selName;
bool g_bMaking;          // the maker shown in the workspace's right

struct PlanLine {
  const char *pszGlyph;
  std::string text;
  int nTok;
};

std::vector<PlanLine> MakePlan(void) {
  std::vector<PlanLine> out;
  const Spell &s = kSpells[g_make.nSpell];
  const std::string label = MakerLabel(), key = KeyOf(label), card = CardOf(g_make.caster);
  if (s.pszWork)
    out.push_back({"action", "An order, " + key + ", from " + Short(ST_ORDERS, s.pszOrder) + ": " + s.pszWork + ".", TK_INK});
  else
    out.push_back({"action", "An order, " + key + ", from " + Short(ST_ORDERS, s.pszOrder) + ": the spell order runs it, firing its weapon.", TK_INK});
  const double dEnergy = g_make.dEnergy >= 0 ? g_make.dEnergy : SpellNumber(ST_TECHS, s.pszTech, "energy");
  out.push_back({"tech", "A tech, " + key + ", from " + StudioShowName(ST_TECHS, s.pszTech) + ": " +
                             Fmt(dEnergy) + " energy, cast by the " +
                             (g_make.caster.empty() ? std::string("caster") : Short(ST_UNITS, g_make.caster)) +
                             (g_make.at.empty() ? ", with no research." : ", once researched at the " + Short(ST_UNITS, g_make.at) + "."),
                 TK_INK});
  if (strcmp(s.pszKey, "blocks") == 0)
    out.push_back({"weapon", "A weapon, " + key + ", from " + StudioShowName(ST_WEAPONS, s.pszWeapon) +
                                 ": its range only. Its blocks do the work, a damage block to start with.", TK_INK});
  else if (!s.pszWork)
    out.push_back({"weapon", "A weapon, " + key + ", from " + StudioShowName(ST_WEAPONS, s.pszWeapon) + ": what the cast fires.", TK_INK});
  if (!g_make.caster.empty() && !g_make.nPlace && !FreePlace(card)) {
    out.push_back({"needs", "The " + Short(ST_UNITS, card) + "'s card is full: pick a place for the button to share.", TK_BAD});
  } else if (!g_make.caster.empty()) {
    const char k = FreeKey(card, label);
    out.push_back({"cards", "A button on the " + Short(ST_UNITS, card) + "'s card at place " +
                                std::to_string(g_make.nPlace ? g_make.nPlace : FreePlace(card)) + ", key " +
                                static_cast<char>(toupper(static_cast<unsigned char>(k))) + ": " + Marked(label, k) +
                                (s.nAim == AIM_SELF ? ", cast where it stands." : ", aimed by the player."),
                   TK_INK});
    if (!g_make.at.empty())
      out.push_back({"cards", "A Research button on the " + Short(ST_UNITS, g_make.at) + "'s card.", TK_INK});
    if (dEnergy > 0 && !UnitFlag(g_make.caster, "spellcaster")) {
      if (Own(ST_UNITS, g_make.caster) && g_make.bEnergy)
        out.push_back({"plus", "The " + Short(ST_UNITS, g_make.caster) + " marked a spellcaster, so it has energy.", TK_INK});
      else
        out.push_back({"needs", "The " + Short(ST_UNITS, g_make.caster) + " has no energy: it is no spellcaster, so it cannot cast this.",
                       TK_WARN});
    }
  } else {
    out.push_back({"needs", "Pick the unit that casts it.", TK_BAD});
  }
  if (Taken(key))
    out.push_back({"needs", key + " is taken among the orders, techs or weapons: give it another name.", TK_BAD});
  return out;
}

bool MakeReady(void) {
  return !g_make.caster.empty() && (g_make.nPlace || FreePlace(CardOf(g_make.caster))) && !Taken(KeyOf(MakerLabel())) && StudioIsOpen();
}

void MakeNow(void) {
  const std::string key = KeyOf(MakerLabel()), label = MakerLabel();
  DesignCall("studio_make_action", MakeFields() + ",\"sender\":\"you\"", [key, label](const PortJson &a) {
    if (a["ok"].kind == PortJson::BOOL && a["ok"].b) {
      g_selOrder = key;
      g_nSelT = -1;
      g_bMaking = false;
      StudioSay("made " + label + ": its blocks are open");
      StudioLog("info", "made the action " + key);
    } else {
      StudioSay(a["error"].Str("refused"));
      StudioLog("error", "studio_make_action: " + a["error"].Str("refused"));
    }
  });
}

void SpellCard(int i, ImVec2 p, float w, float h) {
  const Spell &s = kSpells[i];
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImGui::SetCursorScreenPos(p);
  ImGui::PushID(i);
  if (ImGui::InvisibleButton("##spell", ImVec2(w, h)))
    g_make.nSpell = i, g_make.dEnergy = g_make.dDamage = g_make.dRange = -1;
  const bool bHover = ImGui::IsItemHovered();
  ImGui::PopID();
  const bool bOn = g_make.nSpell == i;
  d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bOn ? C(TK_SEL) : C(TK_PANE2), P(3.0f));
  d->AddRect(p, ImVec2(p.x + w, p.y + h), bOn ? C(TK_SEL_LINE) : bHover ? C(TK_BTN_RIM_HI) : C(TK_LINE), P(3.0f), 0, bOn ? P(1.5f) : P(1.0f));
  StudioGlyph(d, s.pszGlyph, ImVec2(p.x + P(10.0f), p.y + P(10.0f)), P(16.0f), C(TK_LABEL));
  T(d, STUDIO_FONT_BOLD, 13.0f, p.x + P(32.0f), p.y + P(8.0f), bOn ? C(TK_HI) : C(TK_INK), s.pszLabel, w - P(40.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(10.0f), p.y + P(30.0f), C(TK_INK3), s.pszAbout,
    w - P(20.0f));
}

void MakerDraw(float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioLabel(STUDIO_FONT_WIDE, 13.0f, C(TK_HEAD), "New action", 0.06f, true);
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  Wrapped("An action starts from a classic spell: the studio makes its order, its tech, the weapon it fires if it fires one, and the "
          "button on its caster's card, "
          "as one edit that Undo takes back whole.",
          fW, kStudioFsS, TK_INK2);
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  Lab("Start from");
  StudioInput("##find", g_make.szFind, sizeof g_make.szFind, 260.0f, 26.0f, "Find a classic spell", "search");
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const int nCols = std::max(1, static_cast<int>((fW + P(8.0f)) / P(210.0f)));
    const float cw = (fW - P(8.0f) * (nCols - 1)) / nCols, ch = P(50.0f);
    int n = 0;
    for (int i = 0; i < kSpellCount; ++i) {
      if (!Has(kSpells[i].pszLabel, g_make.szFind) && !Has(kSpells[i].pszAbout, g_make.szFind))
        continue;
      SpellCard(i, ImVec2(p.x + (n % nCols) * (cw + P(8.0f)), p.y + (n / nCols) * (ch + P(8.0f))), cw, ch);
      ++n;
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ((n + nCols - 1) / nCols) * (ch + P(8.0f)) + P(4.0f)));
    ImGui::Dummy(ImVec2(fW, 1));
  }
  const Spell &s = kSpells[g_make.nSpell];
  Lab("Its name and caster");
  RowLabel("Name");
  StudioInput("##label", g_make.szLabel, sizeof g_make.szLabel, 260.0f, 26.0f, MakerLabel().c_str());
  After("in the files: " + KeyOf(MakerLabel()));
  RowLabel("Cast by");
  std::string picked;
  if (StudioRecordPick("##caster", ST_UNITS, g_make.caster, 260.0f, &picked, false, 26.0f, "pick a unit"))
    g_make.caster = picked;
  {
    Lab("Its numbers");
    RowLabel("Energy");
    double e = g_make.dEnergy >= 0 ? g_make.dEnergy : SpellNumber(ST_TECHS, s.pszTech, "energy");
    if (StudioNumber("##energy", &e, 86.0f, 24.0f, g_make.dEnergy >= 0, 13.0f))
      g_make.dEnergy = std::max(0.0, e);
    After("classic " + Fmt(SpellNumber(ST_TECHS, s.pszTech, "energy")));
  }
  if (!s.pszWork) {
    RowLabel("Damage");
    double dm = g_make.dDamage >= 0 ? g_make.dDamage : SpellNumber(ST_WEAPONS, s.pszWeapon, "damage");
    if (StudioNumber("##damage", &dm, 86.0f, 24.0f, g_make.dDamage >= 0, 13.0f))
      g_make.dDamage = std::max(0.0, dm);
    After("classic " + Fmt(SpellNumber(ST_WEAPONS, s.pszWeapon, "damage")));
    if (s.nAim != AIM_SELF) {
      RowLabel("Range");
      double r = g_make.dRange >= 0 ? g_make.dRange : SpellNumber(ST_WEAPONS, s.pszWeapon, "max_range") / 32.0;
      if (StudioNumber("##range", &r, 86.0f, 24.0f, g_make.dRange >= 0, 13.0f))
        g_make.dRange = std::max(0.0, r);
      After("tiles; classic " + Fmt(SpellNumber(ST_WEAPONS, s.pszWeapon, "max_range") / 32.0));
    }
  }
  {
    RowLabel("Research");
    std::string at;
    if (StudioRecordPick("##at", ST_UNITS, g_make.at, 260.0f, &at, true, 24.0f, "none: no research"))
      g_make.at = at;
    if (!g_make.at.empty()) {
      RowLabel("Its cost");
      StudioNumber("##min", &g_make.dMinerals, 70.0f, 24.0f, true, 13.0f);
      After("minerals");
      ImGui::SameLine(0.0f, P(10.0f));
      StudioNumber("##gas", &g_make.dGas, 70.0f, 24.0f, true, 13.0f);
      After("gas");
      ImGui::SameLine(0.0f, P(10.0f));
      StudioNumber("##time", &g_make.dTime, 70.0f, 24.0f, true, 13.0f);
      After("seconds");
    }
    RowLabel("Place");
    static const char *const kPlaces[] = {"auto", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    g_make.nPlace = StudioSeg("##place", kPlaces, 10, g_make.nPlace, 24.0f, kStudioFsXs);
    if (s.pszAi[0]) {
      RowLabel("The computer");
      StudioCheck("##ai", AiWords(s.pszAi).c_str(), &g_make.bAi);
    }
    if (!g_make.caster.empty() && Own(ST_UNITS, g_make.caster) && !UnitFlag(g_make.caster, "spellcaster") &&
        (g_make.dEnergy >= 0 ? g_make.dEnergy : SpellNumber(ST_TECHS, s.pszTech, "energy")) > 0) {
      RowLabel("Energy");
      StudioCheck("##give", "make the caster a spellcaster, so it has energy", &g_make.bEnergy);
    }
  }
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  Lab("What Make makes");
  {
    const std::vector<PlanLine> plan = MakePlan();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float y = p.y + P(6.0f);
    const float lineH = P(24.0f);
    const float h = plan.size() * lineH + P(12.0f);
    d->AddRectFilled(p, ImVec2(p.x + fW, p.y + h), C(TK_WELL), P(3.0f));
    d->AddRect(p, ImVec2(p.x + fW, p.y + h), C(TK_LINE), P(3.0f));
    for (const PlanLine &l : plan) {
      StudioGlyph(d, l.pszGlyph, ImVec2(p.x + P(10.0f), y + P(4.0f)), P(14.0f), C(l.nTok == TK_INK ? TK_LABEL : l.nTok));
      T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(32.0f), Mid(y, lineH, kStudioFsS), C(l.nTok), l.text, fW - P(44.0f));
      y += lineH;
    }
    ImGui::Dummy(ImVec2(fW, h + P(6.0f)));
  }
  if (StudioButton("##make", ("Make " + MakerLabel()).c_str(), "plus", SB_GOOD, 30.0f, 0.0f, kStudioFsS, MakeReady()))
    MakeNow();
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##cancel", "Cancel", nullptr, SB_PLAIN, 30.0f))
    g_bMaking = false;
}

// ---- the Effects workspace's list ---------------------------------------------------------------------------

bool ListRow(const char *pszId, const char *pszGlyph, const std::string &title, const std::string &sub, bool bOn, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = P(sub.empty() ? 28.0f : 42.0f);
  const bool b = ImGui::InvisibleButton(pszId, ImVec2(fW, h));
  const bool bHover = ImGui::IsItemHovered();
  if (bOn || bHover)
    d->AddRectFilled(p, ImVec2(p.x + fW, p.y + h), C(bOn ? TK_SEL : TK_ROW_HI), P(3.0f));
  if (bOn)
    d->AddRectFilled(p, ImVec2(p.x + P(3.0f), p.y + h), C(TK_SEL_LINE));
  StudioGlyph(d, pszGlyph, ImVec2(p.x + P(8.0f), p.y + P(6.0f)), P(16.0f), C(TK_LABEL));
  T(d, STUDIO_FONT_BOLD, 13.0f, p.x + P(30.0f), p.y + P(5.0f), bOn ? C(TK_HI) : C(TK_INK), title, fW - P(36.0f));
  if (!sub.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(30.0f), p.y + P(24.0f), C(TK_INK3), sub, fW - P(36.0f));
  return b;
}

// Who names a tech or a weapon: the orders, the units, the cards.
std::string UsersOf(int t, const std::string &name) {
  std::vector<std::string> by;
  StudioRecord r;
  if (!StudioRecordOf(t, name, &r))
    return std::string();
  for (const StudioRecord &o : StudioRecords(ST_ORDERS))
    if (StudioNumber(ST_ORDERS, o.name, t == ST_TECHS ? "tech" : "weapon", -1) == r.nId)
      by.push_back("order " + o.name);
  if (t == ST_WEAPONS)
    for (const StudioRecord &u : StudioRecords(ST_UNITS))
      if (StudioNumber(ST_UNITS, u.name, "ground_weapon", -1) == r.nId || StudioNumber(ST_UNITS, u.name, "air_weapon", -1) == r.nId)
        by.push_back(Short(ST_UNITS, u.name));
  if (t == ST_TECHS)
    for (const PortJsonMember &card : StudioDoc("types")["cards"].members)
      for (const PortJson &b : card.second.items)
        if (b["research"].Str() == name)
          by.push_back("researched at the " + Short(ST_UNITS, card.first));
  std::string s;
  for (size_t i = 0; i < by.size() && i < 3; ++i)
    s += (i ? ", " : "") + by[i];
  if (by.size() > 3)
    s += " and " + std::to_string(by.size() - 3) + " more";
  return s.empty() ? std::string("nothing names it") : "used by " + s;
}

void EffectsList(float fW, const char *pszFind) {
  Lab("Actions");
  const std::vector<Action> &acts = Actions();
  int n = 0;
  for (const Action &a : acts) {
    if (!Has(a.order, pszFind) && !Has(StudioShowName(ST_ORDERS, a.order), pszFind))
      continue;
    ++n;
    const std::string caster = CasterOf(a);
    const std::string sub = (caster.empty() ? std::string("no caster") : "the " + Short(ST_UNITS, caster) + "'s") +
                            (a.tech.empty() ? std::string() : ", " + std::to_string(StudioNumber(ST_TECHS, a.tech, "energy")) + " energy") +
                            (a.kind == SK_DEPENDENCY ? ", read only" : "");
    ImGui::PushID(a.order.c_str());
    if (ListRow("##act", "action", StudioShowName(ST_ORDERS, a.order), sub, !g_bMaking && g_nSelT < 0 && g_selOrder == a.order, fW)) {
      g_selOrder = a.order;
      g_nSelT = -1;
      g_bMaking = false;
    }
    ImGui::PopID();
  }
  if (n == 0)
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), acts.empty() ? "None yet: New action makes one." : "None match.");
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  // the techs and weapons that are no action's, with what names them
  for (int t : {ST_TECHS, ST_WEAPONS}) {
    Lab(t == ST_TECHS ? "Techs" : "Weapons");
    int nShown = 0;
    for (const StudioRecord &r : StudioRecords(t)) {
      if (r.kind == SK_EDIT || (!Has(r.name, pszFind) && !Has(StudioShowName(t, r.name), pszFind)))
        continue;
      ++nShown;
      ImGui::PushID((Table(t) + r.name).c_str());
      if (ListRow("##rec", t == ST_TECHS ? "tech" : "weapon", StudioShowName(t, r.name), UsersOf(t, r.name),
                  !g_bMaking && g_nSelT == t && g_selName == r.name, fW)) {
        g_nSelT = t;
        g_selName = r.name;
        g_bMaking = false;
      }
      ImGui::PopID();
    }
    if (nShown == 0)
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "none");
    ImGui::Dummy(ImVec2(1, P(8.0f)));
  }
}

// A tech or a weapon that is chosen: what it is, what names it, its document a click away.
void RecordSummary(int t, const std::string &name, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 22.0f, p.x, p.y, C(TK_HI), StudioShowName(t, name), fW);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(34.0f)));
  Wrapped(UsersOf(t, name) + ".", fW, kStudioFsS, TK_INK2);
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  if (t == ST_TECHS) {
    NumberRow(ST_TECHS, name, "energy", "Energy", 1.0, nullptr);
    NumberRow(ST_TECHS, name, "minerals", "Minerals", 1.0, nullptr);
    NumberRow(ST_TECHS, name, "gas", "Gas", 1.0, nullptr);
    NumberRow(ST_TECHS, name, "research_time", "Research", 24.0, "seconds");
  } else {
    NumberRow(ST_WEAPONS, name, "damage", "Damage", 1.0, nullptr);
    NumberRow(ST_WEAPONS, name, "cooldown", "Cooldown", 24.0, "seconds");
    NumberRow(ST_WEAPONS, name, "max_range", "Range", 32.0, "tiles");
    FieldRow(ST_WEAPONS, name, "explosion", "Explosion");
  }
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  if (StudioButton("##open", "Open its document", "link", SB_PLAIN, 26.0f, 0.0f, kStudioFsS))
    StudioOpenDoc(t, name, 0);
}

// ---- a unit's Actions view ----------------------------------------------------------------------------------

// The spells the classic casters have on their cards, by their techs' names.
struct ClassicCaster {
  const char *pszUnit;
  const char *apszTechs[4];
};
const ClassicCaster kClassicCasters[] = {
    {"terran_marine", {"stim_packs"}},
    {"terran_firebat", {"stim_packs"}},
    {"terran_ghost", {"personnel_cloaking", "lockdown"}},
    {"terran_wraith", {"cloaking_field"}},
    {"terran_science_vessel", {"defensive_matrix", "emp_shockwave", "irradiate"}},
    {"terran_battlecruiser", {"yamato_gun"}},
    {"terran_medic", {"healing", "restoration", "optical_flare"}},
    {"zerg_queen", {"infestation", "parasite", "spawn_broodlings", "ensnare"}},
    {"zerg_defiler", {"burrowing", "dark_swarm", "plague", "consume"}},
    {"protoss_high_templar", {"psionic_storm", "hallucination", "archon_warp"}},
    {"protoss_arbiter", {"recall", "stasis_field"}},
    {"protoss_dark_archon", {"feedback", "mind_control", "maelstrom"}},
    {"protoss_corsair", {"disruption_web"}},
    {"terran_comsat_station", {"scanner_sweep"}},
};

// The classic unit a unit's record comes from, following its bases; "" when it starts from none.
std::string ClassicRoot(const std::string &unit) {
  std::string u = unit;
  for (int i = 0; i < 16 && !u.empty(); ++i) {
    StudioRecord r;
    if (!StudioRecordOf(ST_UNITS, u, &r))
      return std::string();
    if (r.kind == SK_CLASSIC)
      return u;
    u = r.base;
  }
  return std::string();
}

const ClassicCaster *ClassicSpellsOf(const std::string &unit) {
  const std::string root = ClassicRoot(unit);
  for (const ClassicCaster &c : kClassicCasters)
    if (root == c.pszUnit)
      return &c;
  return nullptr;
}

// The record a unit's aura is on: the unit's own, or the mod's edit of a classic unit; the classic
// unit's name when there is none yet (studio_unit_aura makes the edit).
std::string AuraRecord(const std::string &unit) {
  StudioRecord r;
  if (StudioRecordOf(ST_UNITS, unit, &r) && r.kind != SK_CLASSIC)
    return unit;
  for (const StudioRecord &e : StudioRecords(ST_UNITS))
    if (e.kind == SK_EDIT && e.base == unit)
      return e.name;
  return unit;
}

struct UnitSel {
  std::string unit;
  int nItem = 0;      // 0 attack, 1 orders, 2 an action, 3 a classic spell, 4 its aura
  std::string order;  // the action chosen, or the classic spell's tech
} g_unitSel;

// A value read only, on a row after its label, set on the label's line.
void RowValue(const std::string &v, int nFont = STUDIO_FONT_TEXT) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = T(ImGui::GetWindowDrawList(), nFont, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK), v);
  ImGui::Dummy(ImVec2(w, P(26.0f)));
}

// A classic spell the unit has from its card: what it does, read only, and one like it made.
void ClassicSpellView(const std::string &unit, const std::string &tech, float fW) {
  const Spell *pS = SpellOf(tech);
  Block k;
  std::string about;
  if (pS) {
    about = pS->pszAbout;
    about[0] = static_cast<char>(toupper(static_cast<unsigned char>(about[0])));
    about += ". ";
  }
  BlockBegin(k, "Classic", "tech", StudioShowName(ST_TECHS, tech),
             about + "The " + Short(ST_UNITS, ClassicRoot(unit)) +
                 "'s own, on the card it has from its looks: the classic game runs it, and the mod does not change it here.",
             fW);
  ImGui::PushID("classic");
  const long long nEnergy = StudioNumber(ST_TECHS, tech, "energy");
  RowLabel("Energy");
  RowValue(nEnergy > 0 ? std::to_string(nEnergy) : std::string("none"), STUDIO_FONT_MONO);
  if (pS && pS->pszWork) {
    RowLabel("Does");
    RowValue(pS->pszWork);
  }
  if (pS && pS->pszWeapon) {
    RowLabel("Fires");
    RowValue(StudioShowName(ST_WEAPONS, pS->pszWeapon));
    const long long nFrames = StatusFrames(StudioNumber(ST_WEAPONS, pS->pszWeapon, "explosion"));
    if (nFrames > 0) {
      RowLabel("Lasts");
      RowValue(Fmt(nFrames / 24.0) + " s at Fastest");
    }
  }
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  const bool bCan = pS != nullptr;
  if (StudioButton("##like", "Make one like it", "plus", SB_GOOD, 26.0f, 0.0f, kStudioFsS, bCan && StudioIsOpen())) {
    MakerReset(unit);
    for (int i = 0; i < kSpellCount; ++i)
      if (&kSpells[i] == pS)
        g_make.nSpell = i;
    g_bMaking = true;
    StudioWorkspaceSet(4);
  }
  if (!bCan)
    After("no classic block makes it", TK_INK3);
  ImGui::PopID();
  BlockEnd(k);
}

// A weapon slot of a unit: its sentence, a picker, and its document a click away.
void WeaponSlot(const std::string &unit, bool bAir, float fW) {
  const char *pszKey = bAir ? "air_weapon" : "ground_weapon";
  const std::string w = StudioUnitWeapon(unit, bAir);
  Block k;
  std::string words;
  if (w.empty()) {
    words = bAir ? "It cannot attack flyers." : "It cannot attack the ground.";
  } else {
    const long long nHits = std::max(1LL, StudioNumber(ST_WEAPONS, w, "damage_factor", 1));
    const long long nDamage = StudioNumber(ST_WEAPONS, w, "damage"), nCooldown = StudioNumber(ST_WEAPONS, w, "cooldown");
    words = (nHits > 1 ? std::to_string(nHits) + " hits of " + std::to_string(nDamage) : std::to_string(nDamage) + " a hit") + " every " +
            Fmt(nCooldown / 24.0, 2) + " s, " + Fmt(StudioDamagePerSecond(w)) + " a second, " + Tiles(StudioNumber(ST_WEAPONS, w, "max_range")) +
            " away at most.";
  }
  BlockBegin(k, bAir ? "Air" : "Ground", "weapon", w.empty() ? std::string("No weapon") : StudioShowName(ST_WEAPONS, w), words, fW);
  ImGui::PushID(pszKey);
  FieldRow(ST_UNITS, unit, pszKey, "Fires");
  FieldRow(ST_UNITS, unit, bAir ? "max_air_hits" : "max_ground_hits", "Hits at once");
  if (!w.empty()) {
    const bool bShared = !Own(ST_WEAPONS, w);
    if (bShared) {
      const ImVec2 q = ImGui::GetCursorScreenPos();
      T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, q.x, q.y + P(2.0f), C(TK_INK3),
        "A classic weapon, shared with the classic units: its numbers are changed in a copy of the mod's own.");
      ImGui::Dummy(ImVec2(1, P(18.0f)));
    }
  }
  ImGui::PopID();
  BlockEnd(k);
}

void UnitAttack(const std::string &unit, float fW) {
  WeaponSlot(unit, false, fW);
  WeaponSlot(unit, true, fW);
  Block k;
  BlockBegin(k, "Reach", "try", "When it attacks", "How far it looks for a target, and its armour against what attacks it.", fW);
  ImGui::PushID("reach");
  FieldRow(ST_UNITS, unit, "target_range", "Seeks targets");
  FieldRow(ST_UNITS, unit, "sight_range", "Sight");
  ImGui::PopID();
  BlockEnd(k);
}

void UnitOrders(const std::string &unit, float fW) {
  Block k;
  BlockBegin(k, "Orders", "action", "What it does by itself",
             "The orders the engine gives it: idle, after a fight, when told to attack; and what a right-click does.", fW);
  ImGui::PushID("orders");
  FieldRow(ST_UNITS, unit, "idle_order", "When idle");
  FieldRow(ST_UNITS, unit, "ai_idle_order", "The computer's idle");
  FieldRow(ST_UNITS, unit, "return_to_idle_order", "After a fight");
  FieldRow(ST_UNITS, unit, "attack_unit_order", "Attack a unit");
  FieldRow(ST_UNITS, unit, "attack_move_order", "Attack-move");
  FieldRow(ST_UNITS, unit, "right_click", "Right-click");
  ImGui::PopID();
  BlockEnd(k);
  BlockBegin(k, "Basic", "cards", "The basic commands it keeps",
             "Move (M), Stop (S), Attack (A), Patrol (P) and Hold position (H) come with the card it has from its looks; the Card view shows "
             "the whole card.",
             fW);
  BlockEnd(k);
}

}  // namespace

// ---- the contract (studio_ui.h) ------------------------------------------------------------------------------

void StudioEffectsWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float hh = P(44.0f);
  d->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + hh), C(TK_BAR));
  d->AddLine(ImVec2(a.x, a.y + hh - 0.5f), ImVec2(b.x, a.y + hh - 0.5f), C(TK_LINE));
  float x = a.x + P(14.0f);
  x += Wide(d, 13.0f, x, Mid(a.y, hh, 13.0f), C(TK_HEAD), "Effects and actions", 0.06f) + P(16.0f);
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(20.0f), a.y + hh + P(20.0f), C(TK_INK3), "Open a mod to make its actions.");
    return;
  }
  static char s_szFind[48];
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (hh - P(26.0f)) / 2.0f));
  StudioInput("##fxfind", s_szFind, sizeof s_szFind, 220.0f, 26.0f, "Filter", "search");
  const float bw = StudioButtonSize("New action", "plus", 28.0f).x;
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(14.0f) - bw, a.y + (hh - P(28.0f)) / 2.0f));
  if (StudioButton("##fxnew", "New action", "plus", SB_GOOD, 28.0f)) {
    MakerReset(StudioCurrent().t == ST_UNITS ? StudioCurrent().name : std::string());
    g_bMaking = true;
  }
  const float lw = std::min(P(320.0f), (b.x - a.x) * 0.36f);
  const ImVec2 la(a.x + 1, a.y + hh), lb(a.x + lw, b.y - 1);
  d->AddLine(ImVec2(lb.x + 0.5f, la.y), ImVec2(lb.x + 0.5f, lb.y), C(TK_LINE));
  ImGui::SetCursorScreenPos(la);
  if (ImGui::BeginChild("##fxlist", ImVec2(lb.x - la.x, lb.y - la.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::SetCursorPos(ImVec2(P(10.0f), P(10.0f)));
    ImGui::BeginGroup();
    EffectsList(lw - P(24.0f), s_szFind);
    ImGui::EndGroup();
  }
  ImGui::EndChild();
  // the right: the maker, an action's blocks, or a record's summary
  if (!g_bMaking && g_nSelT < 0 && ActionOf(g_selOrder) == nullptr && !Actions().empty())
    g_selOrder = Actions()[0].order;
  ImGui::SetCursorScreenPos(ImVec2(lb.x + 1, la.y));
  if (ImGui::BeginChild("##fxright", ImVec2(b.x - lb.x - 2, b.y - la.y - 1), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    const float rw = std::min(ImGui::GetContentRegionAvail().x - P(40.0f), P(860.0f));
    ImGui::SetCursorPos(ImVec2(P(20.0f), P(16.0f)));
    ImGui::BeginGroup();
    if (g_bMaking)
      MakerDraw(rw);
    else if (g_nSelT >= 0)
      RecordSummary(g_nSelT, g_selName, rw);
    else if (ActionOf(g_selOrder) != nullptr)
      ActionEditor(g_selOrder, rw);
    else
      Wrapped("The mod has no actions yet. New action makes one from a classic spell, with its order, tech, weapon and button.", rw,
              kStudioFsS, TK_INK3);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, P(24.0f)));
  }
  ImGui::EndChild();
}

void StudioUnitActions(const std::string &unit) {
  if (g_unitSel.unit != unit) {
    g_unitSel = UnitSel();
    g_unitSel.unit = unit;
  }
  const float W = ImGui::GetContentRegionAvail().x - P(22.0f);
  const float lw = P(250.0f), gap = P(20.0f);
  const float rw = std::min(W - lw - gap, P(820.0f));
  const ImVec2 o = ImGui::GetCursorScreenPos();
  // the list: Attack, its spells, its orders, Add an ability
  std::vector<const Action *> mine;
  const std::string card = CardOf(unit);
  for (const Action &a : Actions())
    for (const Cast &c : a.casts)
      if (c.card == card) {
        mine.push_back(&a);
        break;
      }
  ImGui::BeginGroup();
  Lab("Attack");
  const std::string g = StudioUnitWeapon(unit, false), ai = StudioUnitWeapon(unit, true);
  const std::string attackSub = g.empty() && ai.empty() ? std::string("no weapon")
                                : g == ai                ? StudioShowName(ST_WEAPONS, g) + ", ground and air"
                                                         : (g.empty() ? std::string() : StudioShowName(ST_WEAPONS, g)) +
                                                               (ai.empty() ? std::string() : (g.empty() ? "" : ", ") + StudioShowName(ST_WEAPONS, ai) + " in the air");
  if (ListRow("##attack", "weapon", "Attack  (A)", attackSub, g_unitSel.nItem == 0, lw))
    g_unitSel.nItem = 0;
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  Lab("Spells and abilities");
  for (const Action *pa : mine) {
    std::string key;
    for (const Cast &c : pa->casts)
      if (c.card == card)
        key = c.button["hotkey"].Str();
    const std::string title = StudioShowName(ST_ORDERS, pa->order) +
                              (key.empty() ? std::string() : "  (" + std::string(1, static_cast<char>(toupper(static_cast<unsigned char>(key[0])))) + ")");
    const std::string sub = pa->tech.empty() ? std::string("no tech") : std::to_string(StudioNumber(ST_TECHS, pa->tech, "energy")) + " energy";
    ImGui::PushID(pa->order.c_str());
    if (ListRow("##spell", "action", title, sub, g_unitSel.nItem == 2 && g_unitSel.order == pa->order, lw)) {
      g_unitSel.nItem = 2;
      g_unitSel.order = pa->order;
    }
    ImGui::PopID();
  }
  if (const ClassicCaster *pC = ClassicSpellsOf(unit)) {
    for (const char *pszTech : pC->apszTechs) {
      if (pszTech == nullptr || StudioClassicRow(ST_TECHS, pszTech) < 0)
        continue;
      const long long nEnergy = StudioNumber(ST_TECHS, pszTech, "energy");
      ImGui::PushID(pszTech);
      if (ListRow("##classic", "tech", StudioShowName(ST_TECHS, pszTech),
                  "classic" + (nEnergy > 0 ? ", " + std::to_string(nEnergy) + " energy" : std::string()),
                  g_unitSel.nItem == 3 && g_unitSel.order == pszTech, lw)) {
        g_unitSel.nItem = 3;
        g_unitSel.order = pszTech;
      }
      ImGui::PopID();
    }
  } else if (mine.empty()) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "None yet.");
  }
  if (!UnitFlag(unit, "spellcaster"))
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_WARN), "No energy: it is no spellcaster.");
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  if (StudioButton("##addab", "Add an ability", "plus", SB_GOOD, 28.0f, lw / P(1.0f), kStudioFsS, StudioIsOpen())) {
    MakerReset(unit);
    g_bMaking = true;
    StudioWorkspaceSet(4);
  }
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  Lab("Orders");
  if (ListRow("##orders", "action", "What it does by itself", "idle, after a fight, right-click", g_unitSel.nItem == 1, lw))
    g_unitSel.nItem = 1;
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  Lab("Passive");
  {
    const PortJson &aura = StudioRecordJson(ST_UNITS, AuraRecord(unit))["aura"];
    const bool bAura = aura.kind == PortJson::OBJECT;
    if (ListRow("##aura", "look", bAura ? "Aura of " + StudioStatusLabel(aura["status"].Str()) : std::string("Aura"),
                bAura ? "every " + Fmt(aura["every"].Num(1), 2) + " s, " + Fmt(aura["radius"].Num(160) / 32.0) + " tiles, " +
                            aura["targets"].Str("allies")
                      : std::string("none: a status on the units near it"),
                g_unitSel.nItem == 4, lw))
      g_unitSel.nItem = 4;
  }
  ImGui::EndGroup();
  const float leftBottom = ImGui::GetItemRectMax().y;
  // the item chosen, at the right
  ImGui::SetCursorScreenPos(ImVec2(o.x + lw + gap, o.y));
  ImGui::BeginGroup();
  if (g_unitSel.nItem == 2 && ActionOf(g_unitSel.order) != nullptr)
    ActionEditor(g_unitSel.order, rw);
  else if (g_unitSel.nItem == 3)
    ClassicSpellView(unit, g_unitSel.order, rw);
  else if (g_unitSel.nItem == 1)
    UnitOrders(unit, rw);
  else if (g_unitSel.nItem == 4)
    AuraEditor(AuraRecord(unit), rw);
  else
    UnitAttack(unit, rw);
  ImGui::EndGroup();
  const float bottom = std::max(leftBottom, ImGui::GetItemRectMax().y);
  ImGui::SetCursorScreenPos(ImVec2(o.x, bottom + P(8.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

void StudioOrderHead(const std::string &name) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioRecord rec;
  if (!StudioRecordOf(ST_ORDERS, name, &rec))
    return;
  const float w = ImGui::GetContentRegionAvail().x;
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(16.0f);
  const float W = w - P(44.0f);
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, o.y, C(TK_HI), StudioShowName(ST_ORDERS, name), W);
  float y = o.y + P(36.0f), cx = o.x;
  if (rec.kind == SK_CLASSIC)
    cx += StudioChip(d, ImVec2(cx, y), "classic, read only", CH_GOLD) + P(6.0f);
  else if (!rec.base.empty())
    cx += StudioChip(d, ImVec2(cx, y), ("based on " + Short(ST_ORDERS, rec.base)).c_str()) + P(6.0f);
  const Action *pA = rec.kind == SK_CLASSIC ? nullptr : ActionOf(name);
  if (pA)
    StudioChip(d, ImVec2(cx, y), "a spell of the mod", CH_GOOD);
  y += P(28.0f);
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  std::string words;
  if (pA) {
    words = Sentence(*pA);
  } else if (rec.kind == SK_CLASSIC) {
    const std::string w = Ref(ST_ORDERS, name, "weapon", ST_WEAPONS), t = Ref(ST_ORDERS, name, "tech", ST_TECHS);
    words = "A classic order: the engine runs it with its own code." +
            (t.empty() ? std::string() : " It takes the energy of " + StudioShowName(ST_TECHS, t) + ".") +
            (w.empty() ? std::string() : " It fires " + StudioShowName(ST_WEAPONS, w) + ".");
  } else {
    words = "An edit of a classic order.";
  }
  Wrapped(words, W, 15.0f, TK_INK);
  y = ImGui::GetCursorScreenPos().y + P(10.0f);
  // the units whose order columns name it, and the cards that cast it
  std::vector<std::pair<std::string, std::string>> users;
  static const char *const kCols[] = {"idle_order", "ai_idle_order", "return_to_idle_order", "attack_unit_order", "attack_move_order"};
  static const char *const kWords[] = {"idle", "the computer's idle", "after a fight", "attack a unit", "attack-move"};
  for (const StudioRecord &u : StudioRecords(ST_UNITS))
    for (int i = 0; i < 5; ++i)
      if (u.kind != SK_EDIT && StudioNumber(ST_UNITS, u.name, kCols[i], -1) == rec.nId)
        users.push_back({u.name, kWords[i]});
  if (pA)
    for (const Cast &c : pA->casts)
      users.push_back({c.card, "casts it"});
  Wide(d, 9.5f, o.x, y + P(5.0f), C(TK_INK3), "Used by", 0.1f);
  float ux = o.x + P(78.0f), uy = y;
  if (users.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ux, y + P(2.0f), C(TK_INK3), rec.kind == SK_CLASSIC ? "classic units, and none of the mod's" : "nothing yet");
  for (size_t i = 0; i < users.size() && i < 12; ++i) {
    const std::string label = StudioShowName(ST_UNITS, users[i].first) + ", " + users[i].second;
    const float bw = StudioButtonSize(label.c_str(), "unit", 24.0f, kStudioFsXs).x;
    if (ux + bw > o.x + W) {
      ux = o.x + P(78.0f);
      uy += P(30.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(ux, uy));
    ImGui::PushID(static_cast<int>(i));
    if (StudioButton("##user", label.c_str(), "unit", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioOpenDoc(ST_UNITS, users[i].first, 2);
    ImGui::PopID();
    ux += bw + P(6.0f);
  }
  y = uy + P(34.0f);
  // its links: the weapon it fires, the tech it takes energy from, the order it runs out of sight
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  Wide(d, 9.5f, o.x, y + P(5.0f), C(TK_INK3), "Links", 0.1f);
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(78.0f), y));
  LinkButton("##hw", ST_WEAPONS, Ref(ST_ORDERS, name, "weapon", ST_WEAPONS), "weapon", "fires");
  LinkButton("##ht", ST_TECHS, Ref(ST_ORDERS, name, "tech", ST_TECHS), "tech", "energy of");
  const std::string obscured = Ref(ST_ORDERS, name, "obscured", ST_ORDERS);
  if (obscured != name)
    LinkButton("##ho", ST_ORDERS, obscured, "action", "unseen, runs");
  if (pA && rec.kind != SK_CLASSIC) {
    if (StudioButton("##blocks", "Its blocks", "fx", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs)) {
      g_selOrder = name;
      g_nSelT = -1;
      g_bMaking = false;
      StudioWorkspaceSet(4);
    }
  }
  ImGui::NewLine();
  y = ImGui::GetCursorScreenPos().y + P(8.0f);
  d->AddLine(ImVec2(o.x, y), ImVec2(o.x + W, y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

// ---- the commands ---------------------------------------------------------------------------------------------

namespace {

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::string ActionJson(const Action &a) {
  std::string casts;
  for (const Cast &c : a.casts)
    casts += std::string(casts.empty() ? "" : ",") + "{\"card\":" + Q(c.card) + ",\"button\":" + PortJsonWrite(c.button) + "}";
  return "{\"name\":" + Q(a.order) + ",\"label\":" + Q(StudioShowName(ST_ORDERS, a.order)) + ",\"tech\":" + Q(a.tech) +
         ",\"weapon\":" + Q(a.weapon) + ",\"read_only\":" + (a.kind == SK_OWN ? "false" : "true") + ",\"energy\":" +
         std::to_string(a.tech.empty() ? 0 : StudioNumber(ST_TECHS, a.tech, "energy")) + ",\"research_at\":" + Q(ResearchAt(a.tech)) +
         ",\"casts\":[" + casts + "],\"blocks\":" + (a.blocks.kind == PortJson::ARRAY ? PortJsonWrite(a.blocks) : std::string("[]")) +
         ",\"sentence\":" + Q(Sentence(a)) + "}";
}

// studio_actions {unit?, show?, blocks?}: the mod's actions, those a unit casts when unit is given; and
// the classic spells studio_make_action starts from. show puts an action's blocks in the Effects
// workspace (blocks true scrolls to them), or the New action maker with "new" (its caster unit, its
// spell from).
bool CmdActions(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str();
  const std::string show = j["show"].Str();
  if (show == "new") {
    MakerReset(unit);
    for (int i = 0; i < kSpellCount; ++i)
      if (j["from"].Str() == kSpells[i].pszKey)
        g_make.nSpell = i;
    g_bMaking = true;
    StudioWorkspaceSet(4);
  } else if (!show.empty()) {
    if (ActionOf(show) == nullptr) {
      *pAnswer = "show is one of the mod's actions, or new";
      return false;
    }
    g_selOrder = show;
    g_nSelT = -1;
    g_bMaking = false;
    g_bScrollBlocks = j["blocks"].kind == PortJson::BOOL && j["blocks"].b;
    StudioWorkspaceSet(4);
  }
  std::string list;
  for (const Action &a : Actions()) {
    bool bIn = unit.empty();
    for (const Cast &c : a.casts)
      bIn = bIn || c.card == CardOf(unit);
    if (bIn)
      list += std::string(list.empty() ? "" : ",") + ActionJson(a);
  }
  std::string from;
  for (const Spell &s : kSpells)
    from += std::string(from.empty() ? "" : ",") + "{\"from\":" + Q(s.pszKey) + ",\"label\":" + Q(s.pszLabel) +
            ",\"aimed\":" + (s.nAim == AIM_SELF ? "false" : "true") + ",\"about\":" + Q(s.pszAbout) +
            (s.pszWork ? ",\"work\":" + Q(s.pszWork) : std::string()) + "}";
  *pAnswer = "\"actions\":[" + list + "],\"from\":[" + from + "]";
  return true;
}

PortJson Arr(void) {
  PortJson a;
  a.kind = PortJson::ARRAY;
  return a;
}

PortJson Obj1(const char *pszKey, const std::string &value) {
  PortJson o = StudioJsonObject();
  StudioJsonPut(o, pszKey, StudioJsonString(value));
  return o;
}

// The order of one of the mod's own actions, for the block commands; null with why when it is none.
const Action *OwnAction(const PortJson &j, std::string *pAnswer) {
  const Action *pA = ActionOf(j["name"].Str());
  if (pA == nullptr || pA->kind != SK_OWN) {
    *pAnswer = "name is one of the mod's own actions (studio_actions)";
    return nullptr;
  }
  if (pA->pWork) {
    *pAnswer = std::string("the action does the classic ") + pA->pWork->pszLabel +
               "'s own work, which runs no blocks: make one from a spell that fires a weapon, or from blocks";
    return nullptr;
  }
  return pA;
}

bool KnownBlocks(const PortJson &list, std::string *pWhy) {
  for (const PortJson &b : list.items) {
    if (b.kind != PortJson::OBJECT || BlockKindOf(b["do"].Str()) == nullptr) {
      std::string kinds;
      for (const BlockKind &k : kBlockKinds)
        kinds += std::string(kinds.empty() ? "" : ", ") + k.pszDo;
      *pWhy = "a block is an object whose \"do\" is one of " + kinds + " (\"" + b["do"].Str() + "\" is none the engine runs)";
      return false;
    }
    if (b["blocks"].kind == PortJson::ARRAY && !KnownBlocks(b["blocks"], pWhy))
      return false;
  }
  return true;
}

BlockPath PathOf(const PortJson &v) {
  BlockPath p;
  for (const PortJson &n : v.items)
    p.push_back(static_cast<int>(n.Num(-1)));
  return p;
}

// The order's blocks put as one edit: none (an empty list) takes them off, so the cast fires its weapon.
bool PutBlocksNow(const Action &a, const PortJson &blocks, const PortJson &j, std::string *pAnswer) {
  std::string why;
  if (blocks.kind == PortJson::ARRAY && !KnownBlocks(blocks, &why)) {
    *pAnswer = why;
    return false;
  }
  const bool bNone = blocks.kind != PortJson::ARRAY || blocks.items.empty();
  const bool bOk = bNone ? StudioRevertField(ST_ORDERS, a.order, "blocks", Sender(j), &why)
                         : StudioSetField(ST_ORDERS, a.order, "blocks", blocks, Sender(j), j["merge"].Str(), &why);
  if (!bOk) {
    *pAnswer = why;
    return false;
  }
  const Action *pA = ActionOf(a.order);
  *pAnswer = "\"action\":" + (pA ? ActionJson(*pA) : std::string("null")) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// studio_action_blocks {name, blocks, merge?}: an action's blocks, the whole list; null or [] for none.
bool CmdActionBlocks(const PortJson &j, std::string *pAnswer) {
  const Action *pA = OwnAction(j, pAnswer);
  return pA && PutBlocksNow(*pA, j["blocks"], j, pAnswer);
}

// studio_block_add {name, block, path?, at?}: a block added to the action's list, or inside the repeat
// or wait path names, at a place (the end by default).
bool CmdBlockAdd(const PortJson &j, std::string *pAnswer) {
  const Action *pA = OwnAction(j, pAnswer);
  if (pA == nullptr)
    return false;
  PortJson blocks = pA->blocks.kind == PortJson::ARRAY ? pA->blocks : EmptyList();
  const BlockPath inside = PathOf(j["path"]);
  if (!inside.empty()) {
    const PortJson *pIn = BlockAt(blocks, inside);
    if (pIn == nullptr || ((*pIn)["do"].Str() != "repeat" && (*pIn)["do"].Str() != "wait")) {
      *pAnswer = "path names a repeat or a wait to add inside";
      return false;
    }
  }
  PortJson *pList = ListAt(blocks, inside, inside.size());
  if (pList == nullptr || j["block"].kind != PortJson::OBJECT) {
    *pAnswer = pList ? "block is a block, {\"do\": ...}" : "path names no block";
    return false;
  }
  const int nAt = static_cast<int>(j["at"].Num(static_cast<double>(pList->items.size())));
  pList->items.insert(pList->items.begin() + std::max(0, std::min(nAt, static_cast<int>(pList->items.size()))), j["block"]);
  return PutBlocksNow(*pA, blocks, j, pAnswer);
}

// studio_block_set {name, path, field, value}: one field of one block (null takes it off).
bool CmdBlockSet(const PortJson &j, std::string *pAnswer) {
  const Action *pA = OwnAction(j, pAnswer);
  if (pA == nullptr)
    return false;
  PortJson blocks = pA->blocks;
  PortJson *pB = BlockAt(blocks, PathOf(j["path"]));
  const std::string field = j["field"].Str();
  if (pB == nullptr || field.empty() || field == "blocks") {
    *pAnswer = pB ? "field is a field of the block (not its blocks: studio_block_add and studio_block_remove)" : "path names no block";
    return false;
  }
  if (j["value"].kind == PortJson::NONE || j["value"].kind == PortJson::NULLV)
    StudioJsonErase(*pB, field);
  else
    StudioJsonPut(*pB, field, j["value"]);
  return PutBlocksNow(*pA, blocks, j, pAnswer);
}

// studio_block_remove {name, path}: a block taken away, with the blocks inside it.
bool CmdBlockRemove(const PortJson &j, std::string *pAnswer) {
  const Action *pA = OwnAction(j, pAnswer);
  if (pA == nullptr)
    return false;
  PortJson blocks = pA->blocks;
  const BlockPath path = PathOf(j["path"]);
  PortJson *pList = path.empty() ? nullptr : ListAt(blocks, path, path.size() - 1);
  if (pList == nullptr || path.back() < 0 || path.back() >= static_cast<int>(pList->items.size())) {
    *pAnswer = "path names no block";
    return false;
  }
  pList->items.erase(pList->items.begin() + path.back());
  return PutBlocksNow(*pA, blocks, j, pAnswer);
}

// studio_block_move {name, path, by}: a block moved among its list's, by places (-1 earlier, 1 later).
bool CmdBlockMove(const PortJson &j, std::string *pAnswer) {
  const Action *pA = OwnAction(j, pAnswer);
  if (pA == nullptr)
    return false;
  PortJson blocks = pA->blocks;
  const BlockPath path = PathOf(j["path"]);
  PortJson *pList = path.empty() ? nullptr : ListAt(blocks, path, path.size() - 1);
  const int n = pList ? static_cast<int>(pList->items.size()) : 0;
  if (pList == nullptr || path.back() < 0 || path.back() >= n) {
    *pAnswer = "path names no block";
    return false;
  }
  const int to = std::max(0, std::min(n - 1, path.back() + static_cast<int>(j["by"].Num(0))));
  PortJson b = pList->items[static_cast<size_t>(path.back())];
  pList->items.erase(pList->items.begin() + path.back());
  pList->items.insert(pList->items.begin() + to, b);
  return PutBlocksNow(*pA, blocks, j, pAnswer);
}

// A button added to a card's list, as one edit of the group open.
bool AddButton(const std::string &card, const PortJson &b, const std::string &sender, std::string *pWhy) {
  PortJson list = StudioDoc("types")["cards"][card.c_str()];
  if (list.kind != PortJson::ARRAY)
    list = Arr();
  list.items.push_back(b);
  return StudioPut("types", {"cards", card}, list, sender, "a button on the " + card + "'s card", pWhy);
}

// studio_make_action {unit, from, label?, name?, values?: {energy, damage, max_range, inner_splash,
// middle_splash, outer_splash, at, minerals, gas, research_time, hotkey, position, text, ai, target,
// give_energy}}: an action made from a classic spell (studio_actions lists them): an order, a tech
// and a weapon named name, based on the spell's rows, and the button that casts it on the unit's
// card; with at, a research at that building and its button there. One edit, undone together;
// nothing is made when a part is refused.
bool CmdMakeAction(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const Spell *pS = SpellOf(j["from"].Str());
  if (pS == nullptr) {
    std::string keys;
    for (const Spell &s : kSpells)
      keys += std::string(keys.empty() ? "" : ", ") + s.pszKey;
    *pAnswer = "from is a classic spell an action starts from: " + keys;
    return false;
  }
  const std::string unit = j["unit"].Str();
  StudioRecord ru;
  if (unit.empty() || !StudioRecordOf(ST_UNITS, unit, &ru)) {
    *pAnswer = "unit is the unit that casts it, by name";
    return false;
  }
  const std::string label = j["label"].Str(pS->pszLabel);
  const std::string name = j["name"].Str(KeyOf(label).c_str());
  for (int t : {ST_ORDERS, ST_TECHS, ST_WEAPONS}) {
    StudioRecord r;
    if (StudioRecordOf(t, name, &r)) {
      *pAnswer = name + " is taken among the " + Table(t) + ": give it another name";
      return false;
    }
  }
  const PortJson &v = j["values"];
  const std::string sender = Sender(j), card = CardOf(unit);
  const int nPlace = static_cast<int>(v["position"].Num(FreePlace(card)));
  if (nPlace < 1 || nPlace > 9) {
    *pAnswer = "the " + card + "'s card is full: give values.position, 1 to 9, for the button to share";
    return false;
  }
  const int nAt = StudioHistoryAt();
  std::string why;
  bool bOk = true;
  {
    const bool bBlocksAsked = strcmp(pS->pszKey, "blocks") == 0 || (!pS->pszWork && v["blocks"].kind == PortJson::ARRAY);
    if (bBlocksAsked && v["blocks"].kind == PortJson::ARRAY && !KnownBlocks(v["blocks"], &why)) {
      *pAnswer = why;
      return false;
    }
  }
  StudioGroupBegin(sender, "made the action " + name);
  // the weapon: the spell's, its numbers as given; the burst fires on the caster across the map. A
  // spell whose classic order does its own work has none.
  PortJson w = StudioJsonObject();
  if (pS->pszWeapon)
    StudioJsonPut(w, "base", StudioJsonString(pS->pszWeapon));
  if (strcmp(pS->pszKey, "burst") == 0) {
    StudioJsonPut(w, "behavior", StudioJsonNumber(5));
    StudioJsonPut(w, "explosion", StudioJsonNumber(25));
    StudioJsonPut(w, "target_flags", StudioJsonNumber(66));
    StudioJsonPut(w, "max_range", StudioJsonNumber(8192));
    StudioJsonPut(w, "damage", StudioJsonNumber(25));
    StudioJsonPut(w, "damage_bonus", StudioJsonNumber(0));
    StudioJsonPut(w, "inner_splash", StudioJsonNumber(24));
    StudioJsonPut(w, "middle_splash", StudioJsonNumber(48));
    StudioJsonPut(w, "outer_splash", StudioJsonNumber(80));
  }
  const bool bBlocks = strcmp(pS->pszKey, "blocks") == 0 || (!pS->pszWork && v["blocks"].kind == PortJson::ARRAY);
  if (strcmp(pS->pszKey, "blocks") == 0) {
    // its weapon gives the range only: the blocks do the work
    StudioJsonPut(w, "damage", StudioJsonNumber(0));
    StudioJsonPut(w, "damage_bonus", StudioJsonNumber(0));
    StudioJsonPut(w, "explosion", StudioJsonNumber(0));
  }
  PortJson blocks = v["blocks"];
  if (bBlocks && (blocks.kind != PortJson::ARRAY || blocks.items.empty()))
    PortJsonParse("[{\"do\":\"damage\",\"amount\":20,\"radius\":64,\"targets\":\"enemies\"}]", &blocks);
  for (const char *pszKey : {"damage", "max_range", "inner_splash", "middle_splash", "outer_splash", "damage_type", "explosion"})
    if (v[pszKey].kind == PortJson::NUMBER)
      StudioJsonPut(w, pszKey, v[pszKey]);
  if (!pS->pszWork)
    bOk = StudioCreate(ST_WEAPONS, name, w, sender, &why);
  // the tech: its energy, who casts it, and its research when it has one
  const std::string at = v["at"].Str();
  if (bOk) {
    PortJson t = StudioJsonObject();
    StudioJsonPut(t, "base", StudioJsonString(pS->pszTech));
    StudioJsonPut(t, "energy", StudioJsonNumber(v["energy"].Num(static_cast<double>(StudioNumber(ST_TECHS, pS->pszTech, "energy")))));
    PortJson use = Arr();
    use.items.push_back(Obj1("caster", card));
    if (!at.empty()) {
      use.items.push_back(StudioJsonString("researched"));
      PortJson req = Arr();
      req.items.push_back(Obj1("at", at));
      StudioJsonPut(t, "requires", req);
      for (const char *pszKey : {"minerals", "gas", "research_time"})
        StudioJsonPut(t, pszKey, StudioJsonNumber(v[pszKey].Num(strcmp(pszKey, "research_time") == 0 ? 1440.0 : 100.0)));
    }
    StudioJsonPut(t, "use_requires", use);
    bOk = StudioCreate(ST_TECHS, name, t, sender, &why);
  }
  // the order: the spell's, firing the weapon or doing the classic order's work, with the tech's energy
  if (bOk) {
    PortJson o = StudioJsonObject();
    StudioJsonPut(o, "base", StudioJsonString(pS->pszOrder));
    if (!pS->pszWork)
      StudioJsonPut(o, "weapon", StudioJsonString(name));
    StudioJsonPut(o, "tech", StudioJsonString(name));
    // out of sight the cast goes on as itself, not as the classic spell it started from
    StudioJsonPut(o, "obscured", StudioJsonString(name));
    if (bBlocks)
      StudioJsonPut(o, "blocks", blocks);
    bOk = StudioCreate(ST_ORDERS, name, o, sender, &why);
  }
  // the button that casts it
  if (bOk) {
    const char key = v["hotkey"].Str().empty() ? FreeKey(card, label) : static_cast<char>(tolower(static_cast<unsigned char>(v["hotkey"].Str()[0])));
    PortJson b = StudioJsonObject();
    StudioJsonPut(b, "position", StudioJsonNumber(nPlace));
    StudioJsonPut(b, "cast", StudioJsonString(name));
    const bool bAimed = v["target"].kind == PortJson::BOOL ? v["target"].b : pS->nAim != AIM_SELF;
    if (bAimed)
      StudioJsonPut(b, "target", PortJson{PortJson::BOOL, true});
    const std::string ai = v["ai"].kind == PortJson::STRING ? v["ai"].s : std::string(pS->pszAi);
    if (!ai.empty())
      StudioJsonPut(b, "ai", StudioJsonString(ai));
    StudioJsonPut(b, "hotkey", StudioJsonString(std::string(1, key)));
    StudioJsonPut(b, "text", StudioJsonString(v["text"].Str(Marked(label, key).c_str())));
    bOk = AddButton(card, b, sender, &why);
  }
  // its research's button on the building's card
  if (bOk && !at.empty()) {
    const char key = FreeKey(at, label);
    PortJson b = StudioJsonObject();
    StudioJsonPut(b, "position", StudioJsonNumber(FreePlace(at) ? FreePlace(at) : 9));
    StudioJsonPut(b, "research", StudioJsonString(name));
    StudioJsonPut(b, "hotkey", StudioJsonString(std::string(1, key)));
    StudioJsonPut(b, "text", StudioJsonString("Research " + Marked(label, key)));
    bOk = AddButton(at, b, sender, &why);
  }
  // energy for a caster of the mod's own that has none, when the action costs some
  const double dEnergy = v["energy"].Num(static_cast<double>(StudioNumber(ST_TECHS, pS->pszTech, "energy")));
  if (bOk && dEnergy > 0 && ru.kind == SK_OWN && !UnitFlag(unit, "spellcaster") &&
      !(v["give_energy"].kind == PortJson::BOOL && !v["give_energy"].b)) {
    const std::vector<std::string> &names = StudioFlags("units");
    const long long f = StudioNumber(ST_UNITS, unit, "flags");
    PortJson arr = Arr();
    for (size_t i = 0; i < names.size() && i < 64; ++i)
      if ((((f >> i) & 1) || names[i] == "spellcaster") && !names[i].empty())
        arr.items.push_back(StudioJsonString(names[i]));
    bOk = StudioSetField(ST_UNITS, unit, "flags", arr, sender, std::string(), &why);
  }
  StudioGroupEnd();
  if (!bOk) {
    std::string ignore;
    if (StudioHistoryAt() > nAt)
      StudioUndo(&ignore);  // what was made of it taken back, so a refusal leaves the mod as it was
    *pAnswer = why;
    return false;
  }
  const Action *pA = ActionOf(name);
  *pAnswer = "\"action\":" + (pA ? ActionJson(*pA) : std::string("null")) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// studio_action_research {name, at?, minerals?, gas?, research_time?}: an action's research set,
// as one edit: with at, researched at that building (its tech's requires, "researched" among
// what a caster needs, a Research button on the building's card); without, no research, its
// buttons taken off.
bool CmdActionResearch(const PortJson &j, std::string *pAnswer) {
  const Action *pA = ActionOf(j["name"].Str());
  if (pA == nullptr || pA->kind != SK_OWN || pA->tech.empty() || !Own(ST_TECHS, pA->tech)) {
    *pAnswer = "name is one of the mod's own actions, with a tech of its own";
    return false;
  }
  const Action a = *pA;
  const std::string at = j["at"].Str(), sender = Sender(j);
  if (!at.empty()) {
    StudioRecord r;
    if (!StudioRecordOf(ST_UNITS, at, &r)) {
      *pAnswer = "at is a unit by name";
      return false;
    }
  }
  const int nAt = StudioHistoryAt();
  std::string why;
  bool bOk = true;
  StudioGroupBegin(sender, at.empty() ? "no research for " + a.order : "research of " + a.order + " at " + at);
  PortJson use = StudioRecordJson(ST_TECHS, a.tech)["use_requires"];
  if (use.kind != PortJson::ARRAY)
    use = Arr();
  use.items.erase(std::remove_if(use.items.begin(), use.items.end(),
                                 [](const PortJson &q) { return q.kind == PortJson::STRING && q.s == "researched"; }),
                  use.items.end());
  PortJson req;
  if (!at.empty()) {
    req = Arr();
    req.items.push_back(Obj1("at", at));
    use.items.push_back(StudioJsonString("researched"));
  }
  bOk = StudioPut("types", {"techs", a.tech, "requires"}, req, sender, "where " + a.tech + " is researched", &why);
  bOk = bOk && StudioPut("types", {"techs", a.tech, "use_requires"}, use, sender, "what casting " + a.tech + " needs", &why);
  if (bOk && !at.empty())
    for (const char *pszKey : {"minerals", "gas", "research_time"}) {
      const double dNow = static_cast<double>(StudioNumber(ST_TECHS, a.tech, pszKey));
      const double dNew = j[pszKey].Num(dNow > 0 ? dNow : strcmp(pszKey, "research_time") == 0 ? 1440.0 : 100.0);
      if (bOk && dNew != dNow)
        bOk = StudioSetField(ST_TECHS, a.tech, pszKey, StudioJsonNumber(dNew), sender, std::string(), &why);
    }
  // the research buttons: every one taken off, then one put on the building's card
  std::vector<std::string> cards;
  for (const PortJsonMember &card : StudioDoc("types")["cards"].members)
    cards.push_back(card.first);
  for (const std::string &card : cards) {
    PortJson list = StudioDoc("types")["cards"][card.c_str()];
    const size_t n = list.items.size();
    list.items.erase(std::remove_if(list.items.begin(), list.items.end(), [&](const PortJson &b) { return b["research"].Str() == a.tech; }),
                     list.items.end());
    if (bOk && list.items.size() != n)
      bOk = StudioPut("types", {"cards", card}, list.items.empty() ? PortJson() : list, sender, "a button off the " + card + "'s card", &why);
  }
  if (bOk && !at.empty()) {
    const std::string label = StudioShowName(ST_ORDERS, a.order);
    const char key = FreeKey(at, label);
    PortJson b = StudioJsonObject();
    StudioJsonPut(b, "position", StudioJsonNumber(FreePlace(at) ? FreePlace(at) : 9));
    StudioJsonPut(b, "research", StudioJsonString(a.tech));
    StudioJsonPut(b, "hotkey", StudioJsonString(std::string(1, key)));
    StudioJsonPut(b, "text", StudioJsonString("Research " + Marked(label, key)));
    bOk = AddButton(at, b, sender, &why);
  }
  StudioGroupEnd();
  if (!bOk) {
    std::string ignore;
    if (StudioHistoryAt() > nAt)
      StudioUndo(&ignore);
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"research_at\":" + Q(ResearchAt(a.tech)) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// studio_delete_action {name}: the action's order, tech and weapon, and every button that casts it
// or researches its tech, taken away as one edit.
bool CmdDeleteAction(const PortJson &j, std::string *pAnswer) {
  const Action *pA = ActionOf(j["name"].Str());
  if (pA == nullptr || pA->kind != SK_OWN) {
    *pAnswer = "name is one of the mod's own actions (studio_actions)";
    return false;
  }
  const Action a = *pA;
  const std::string sender = Sender(j);
  const int nAt = StudioHistoryAt();
  std::string why;
  bool bOk = true;
  StudioGroupBegin(sender, "deleted the action " + a.order);
  // the buttons first, each card's list put again without them
  std::vector<std::string> cards;
  for (const PortJsonMember &card : StudioDoc("types")["cards"].members)
    cards.push_back(card.first);
  for (const std::string &card : cards) {
    PortJson list = StudioDoc("types")["cards"][card.c_str()];
    const size_t n = list.items.size();
    list.items.erase(std::remove_if(list.items.begin(), list.items.end(),
                                    [&](const PortJson &b) {
                                      return b["cast"].Str() == a.order || (!a.tech.empty() && b["research"].Str() == a.tech);
                                    }),
                     list.items.end());
    if (bOk && list.items.size() != n)
      bOk = StudioPut("types", {"cards", card}, list.items.empty() ? PortJson() : list, sender, "a button off the " + card + "'s card", &why);
  }
  if (bOk)
    bOk = StudioDelete(ST_ORDERS, a.order, sender, &why);
  if (bOk && !a.tech.empty() && Own(ST_TECHS, a.tech))
    bOk = StudioDelete(ST_TECHS, a.tech, sender, &why);
  if (bOk && !a.weapon.empty() && Own(ST_WEAPONS, a.weapon) && UsersOf(ST_WEAPONS, a.weapon) == "nothing names it")
    bOk = StudioDelete(ST_WEAPONS, a.weapon, sender, &why);
  StudioGroupEnd();
  if (!bOk) {
    std::string ignore;
    if (StudioHistoryAt() > nAt)
      StudioUndo(&ignore);
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"deleted\":" + Q(a.order) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

}  // namespace

void StudioActionsInit(void) {
  PortControlCommand("studio_actions", CmdActions);
  PortControlCommand("studio_make_action", CmdMakeAction);
  PortControlCommand("studio_delete_action", CmdDeleteAction);
  PortControlCommand("studio_action_research", CmdActionResearch);
  PortControlCommand("studio_action_blocks", CmdActionBlocks);
  PortControlCommand("studio_block_add", CmdBlockAdd);
  PortControlCommand("studio_block_set", CmdBlockSet);
  PortControlCommand("studio_block_remove", CmdBlockRemove);
  PortControlCommand("studio_block_move", CmdBlockMove);
}
