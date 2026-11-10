// studio_races_cards.cpp -- command cards (types.json's "cards", docs/mod-data.md): a unit's card as
// the game deals it, the classic buttons read from the engine's own tables (statcmd.cpp's cards)
// with the mod's put among them by place; a button's editor; hotkey clashes and buttons that share
// a place; where a unit is made; a unit's Card view and a tech's or an upgrade's head. Every change
// is one of the commands at the end (studio_card_set, studio_card_move, studio_card_train), built
// on StudioPut, so the panels and Claude edit alike and the history undoes it.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include "statcmd.h"    // statcmd_btn_t
#include "order.h"      // the buttons' actions, status_get_str
#include "imgui_internal.h"
#include "../port/data/port_moddata.h"  // MOD_STRING_FIRST, kModAiFinders
#include "../port/platform/port_json.h"
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_races.h"

// statcmd.cpp's card record, as it declares it there: the count, the buttons, a linked card.
struct statcmd_card_t {
  WORD wBtnCount;
  BYTE _unmapped02[2];
  statcmd_btn_t *pBtns;
  WORD wLink;
  BYTE _unmapped0A[2];
};
statcmd_card_t *StatcmdCard(WORD wCard);
const statcmd_card_t *StatcmdClassicCard(WORD wCard);
// The loaded stat_txt.tbl (status.cpp), the buttons' texts: there once a game's console is up.
extern void *sgpStatStrs;


namespace {

using namespace studio_draw;
using namespace studio_races;

// ---- a card's buttons -------------------------------------------------------------------------------

struct Btn {
  int nPos = 0;
  bool bClassic = true;
  int nMod = -1;           // its place in this mod's list for the card; -1 for a classic button or a dependency's
  std::string dep;         // the mod it depends on that adds it
  std::string what;        // train, morph, build, addon, research, upgrade, cast, or a classic command's word
  int t = -1;              // the table its target is in
  std::string target;      // the record it makes, researches, upgrades or casts
  char cKey = 0;           // its hotkey, in lower case
  std::string text;        // with the hotkey's letter written <K>
  std::string disabled;    // the text while what it needs is missing
  int nIcon = -1;          // a command icon of its own, -1 for its target's
  bool bAimed = false;     // a cast aimed at a unit or a place
  std::string ai;          // how a computer casts it
  bool bAlt = false;       // a button before it has its place: shown when that one is not
  // a classic button whose words a mod changes ("cards" entry with "classic"): the mod's entry
  // (its place in this mod's list, -1 none or a dependency's) and the classic words
  int nWords = -1;
  bool bReworded = false;
  std::string classicText, classicDisabled;
  char cClassicKey = 0;
};

struct Card {
  std::string unit;        // the unit asked for
  std::string key;         // the name "cards" files its buttons under
  int nClassic = -1;       // the classic card it starts from
  bool bModUnit = false;   // a mod's unit
  bool bOwnCard = false;   // a mod's unit the mod gives buttons: its looks' card and them
  bool bOk = false;        // the unit is there
  bool bMenu = false;      // a worker's build menu ("terran_basic_structures"): its buttons build
  std::vector<Btn> btns;   // in the order the game deals them
};

std::string Lc(std::string s) {
  return Lower(s);
}

// A classic text: the hotkey's letter, which the game highlights, as <K>; the other colour codes left out.
std::string FromGame(const char *psz) {
  std::string s;
  for (; psz && *psz; ++psz) {
    const unsigned char c = static_cast<unsigned char>(*psz);
    if (c == 3 && psz[1] && static_cast<unsigned char>(psz[1]) >= 0x20) {
      // a letter in the highlight's colour is the hotkey's; a longer run is a word shown bright
      size_t n = 1;
      while (psz[n] && static_cast<unsigned char>(psz[n]) >= 0x20)
        ++n;
      if (n == 2)
        s += std::string("<") + psz[1] + ">";
      else
        s.append(psz + 1, n - 1);
      psz += n - 1;
    } else if (c >= 0x20 || c == '\n') {
      s += static_cast<char>(c);
    }
  }
  return PortJsonUtf8(s);
}

// The text without its marks: "Warp in Hybrid Reaver".
std::string Plain(const std::string &text) {
  std::string s;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '<' && i + 2 < text.size() && text[i + 2] == '>') {
      s += text[i + 1];
      i += 2;
    } else if (text[i] != '\n') {
      s += text[i];
    } else {
      s += ' ';
    }
  }
  return s;
}

// The letter marked in a text, or 0.
char Marked(const std::string &text) {
  for (size_t i = 0; i + 2 < text.size(); ++i)
    if (text[i] == '<' && text[i + 2] == '>')
      return static_cast<char>(tolower(static_cast<unsigned char>(text[i + 1])));
  return 0;
}

// The text with key's letter marked: its first in the text, or put before it.
std::string HotText(const std::string &text, char key) {
  const std::string plain = Plain(text);
  if (key == 0)
    return plain;
  const size_t at = Lc(plain).find(static_cast<char>(tolower(static_cast<unsigned char>(key))));
  if (at == std::string::npos)
    return std::string("<") + static_cast<char>(toupper(static_cast<unsigned char>(key))) + ">" + plain;
  return plain.substr(0, at) + "<" + plain[at] + ">" + plain.substr(at + 1);
}

struct Act {
  void(__fastcall *pfn)(WORD, BYTE);
  const char *pszWhat;
  int t;
};

// What a classic button does, by its action, and the table its argument names.
void Classify(const statcmd_btn_t &b, Btn *pOut) {
  static const Act kActs[] = {
      {order_build, "train", ST_UNITS},          {order_morph, "morph", ST_UNITS},
      {order_upgrade_bldg, "morph", ST_UNITS},   {order_place, "build", ST_UNITS},
      {order_zerg_place, "build", ST_UNITS},     {order_place_protoss, "build", ST_UNITS},
      {order_place_addon, "addon", ST_UNITS},    {order_research, "research", ST_TECHS},
      {order_upgrade, "upgrade", ST_UPGRADES},   {order_tech, "cast", ST_TECHS},
      {order_land, "land", -1},                  {order_move, "move", -1},
      {order_stop, "stop", -1},                  {order_attack, "attack", -1},
      {order_patrol, "patrol", -1},              {order_hold_pos, "hold position", -1},
      {order_harvest, "gather", -1},             {order_return, "return cargo", -1},
      {order_repair, "repair", -1},              {order_burrow, "burrow", -1},
      {order_deburrow, "unburrow", -1},          {order_cloak, "cloak", -1},
      {order_decloak, "decloak", -1},            {order_siege, "siege mode", -1},
      {order_tank_mode, "tank mode", -1},        {order_stim, "stim packs", -1},
      {order_unload, "unload", -1},              {order_bldg_liftoff, "lift off", -1},
      {order_rally, "rally", -1},                {order_heal, "heal", -1},
      {order_summon_archon, "merge", -1},        {order_summon_darchon, "merge", -1},
      {order_cancel_research, "cancel", -1},     {order_cancel_upgrade, "cancel", -1},
      {order_cancel_addon, "cancel", -1},        {order_stop_build, "cancel", -1},
      {order_build_fighter, "train", -1},        {order_nuke_paint, "nuke", -1},
      {order_recharge, "recharge shields", -1},  {order_select_larva, "select larva", -1},
      {order_kill, "cancel", -1},                {order_kill_egg, "cancel", -1},
  };
  pOut->what = "command";
  for (const Act &a : kActs)
    if (b.pfnAction == a.pfn) {
      pOut->what = a.pszWhat;
      pOut->t = a.t;
      break;
    }
  if (pOut->t >= 0) {
    const char *psz = StudioClassicName(pOut->t, b.w0E);
    if (psz)
      pOut->target = psz;
    else
      pOut->t = -1;
  }
}

// A classic card's buttons, the mod's that a game put among them left out (their texts are the
// mod's strings); kept once the game's strings are there to name them.
const std::vector<Btn> &ClassicButtons(int nCard) {
  static std::map<int, std::vector<Btn>> s_kept;
  static std::vector<Btn> s_now;
  auto it = s_kept.find(nCard);
  if (it != s_kept.end())
    return it->second;
  std::vector<Btn> out;
  if (nCard >= 0 && nCard < 250) {
    const statcmd_card_t *pCard = StatcmdClassicCard(static_cast<WORD>(nCard));
    for (int i = 0; pCard && pCard->pBtns && i < pCard->wBtnCount; ++i) {
      const statcmd_btn_t &b = pCard->pBtns[i];
      if (b.wStatusStrId >= MOD_STRING_FIRST)
        continue;
      Btn o;
      o.nPos = b.wMinIndex;
      o.nIcon = b.wIcon;
      Classify(b, &o);
      if (sgpStatStrs != NULL) {
        const char *psz = status_get_str(b.wStatusStrId);
        // the hotkey, the kind of cost its tooltip shows (0 for none, which ends no string here), the text
        if (psz && psz[0]) {
          o.cKey = static_cast<char>(tolower(static_cast<unsigned char>(psz[0])));
          o.text = FromGame(psz + 2);
        }
        if (b.wDisabledStrId != 0 && b.wDisabledStrId < MOD_STRING_FIRST)
          o.disabled = FromGame(status_get_str(b.wDisabledStrId));
      }
      if (o.text.empty())
        o.text = o.target.empty() ? o.what : StudioShowName(o.t, o.target);
      out.push_back(o);
    }
  }
  if (sgpStatStrs == NULL) {
    s_now = out;
    return s_now;
  }
  return s_kept[nCard] = out;
}

bool IsMod(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && (r.kind == SK_OWN || r.kind == SK_DEPENDENCY);
}

bool Mine(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && (r.kind == SK_OWN || r.kind == SK_EDIT);
}

// What a button of the mod's does; "build" is a worker's build menu's, and its only kind.
const char *const kKinds[] = {"train", "upgrade", "research", "cast", "build"};
const int kKindTable[] = {ST_UNITS, ST_UPGRADES, ST_TECHS, ST_ORDERS, ST_UNITS};
const int KIND_BUILD = 4, KINDS = 5;

Btn FromJson(const PortJson &j, int nIndex, const std::string &dep) {
  Btn o;
  o.bClassic = false;
  o.nMod = dep.empty() ? nIndex : -1;
  o.dep = dep;
  o.nPos = static_cast<int>(j["position"].Num(0));
  for (int k = 0; k < KINDS; ++k)
    if (j[kKinds[k]].kind != PortJson::NONE) {
      o.what = kKinds[k];
      o.t = kKindTable[k];
      o.target = NameIn(o.t, j[kKinds[k]]);
      break;
    }
  const std::string key = j["hotkey"].Str();
  o.cKey = key.empty() ? 0 : static_cast<char>(tolower(static_cast<unsigned char>(key[0])));
  o.text = j["text"].Str();
  if (o.text.empty() && !o.target.empty())
    o.text = StudioShowName(o.t, o.target);
  o.disabled = j["disabled"].Str();
  o.nIcon = j["icon"].kind == PortJson::NUMBER ? static_cast<int>(j["icon"].d) : -1;
  o.bAimed = j["target"].kind == PortJson::BOOL && j["target"].b;
  o.ai = j["ai"].Str();
  return o;
}

// A mod unit's classic row whose looks it borrows: its looks, else its base's, as far as needed.
int LooksRow(const std::string &unit, const char *pszField) {
  std::string at = unit;
  for (int n = 0; n < 32; ++n) {
    const int nRow = StudioClassicRow(ST_UNITS, at);
    if (nRow >= 0)
      return nRow;
    StudioRecord r;
    if (!StudioRecordOf(ST_UNITS, at, &r))
      return -1;
    if (r.kind == SK_EDIT)
      return r.nId;
    const PortJson &j = StudioRecordJson(ST_UNITS, at);
    if (pszField && j[pszField].kind != PortJson::NONE) {
      const std::string to = NameIn(ST_UNITS, j[pszField]);
      if (StudioClassicRow(ST_UNITS, to) >= 0)
        return StudioClassicRow(ST_UNITS, to);
    }
    if (j["looks"].kind != PortJson::NONE) {
      at = NameIn(ST_UNITS, j["looks"]);
      pszField = nullptr;
      continue;
    }
    at = NameIn(ST_UNITS, j["base"]);
    pszField = nullptr;
    if (at.empty())
      return -1;
  }
  return -1;
}

// The buttons the mod and the mods it depends on add to a card, by its key.
void ModButtons(const std::string &key, std::vector<Btn> *pOut) {
  for (int i = 0; i < StudioDependencies(); ++i) {
    const PortJson &list = StudioDependencyTypes(i)["cards"][key.c_str()];
    for (size_t k = 0; k < list.size(); ++k)
      if (list.At(k)["classic"].kind == PortJson::NONE)
        pOut->push_back(FromJson(list.At(k), static_cast<int>(k), StudioDependencyId(i)));
  }
  const PortJson &list = StudioDoc("types")["cards"][key.c_str()];
  for (size_t k = 0; k < list.size(); ++k)
    if (list.At(k)["classic"].kind == PortJson::NONE)
      pOut->push_back(FromJson(list.At(k), static_cast<int>(k), std::string()));
}

// The words a mod and the mods it depends on give the card's classic buttons, the mod's own last.
void ClassicWords(const std::string &key, std::vector<Btn> *pBtns) {
  auto apply = [&](const PortJson &list, bool bOwn) {
    for (size_t k = 0; k < list.size(); ++k) {
      const PortJson &e = list.At(k);
      if (e["classic"].kind == PortJson::NONE)
        continue;
      const int nPos = static_cast<int>(e["position"].Num(0));
      const std::string target = e["classic"].kind == PortJson::STRING ? Lower(e["classic"].s) : std::string();
      for (Btn &o : *pBtns)
        if (o.bClassic && o.nPos == nPos && (target.empty() || Lower(o.target) == target)) {
          if (!o.bReworded) {
            o.classicText = o.text;
            o.classicDisabled = o.disabled;
            o.cClassicKey = o.cKey;
          }
          o.bReworded = true;
          if (e["text"].kind == PortJson::STRING)
            o.text = e["text"].s;
          if (e["hotkey"].kind == PortJson::STRING && !e["hotkey"].s.empty())
            o.cKey = static_cast<char>(tolower(static_cast<unsigned char>(e["hotkey"].s[0])));
          if (e["disabled"].kind == PortJson::STRING)
            o.disabled = e["disabled"].s;
          if (bOwn)
            o.nWords = static_cast<int>(k);
          break;
        }
    }
  };
  for (int i = 0; i < StudioDependencies(); ++i)
    apply(StudioDependencyTypes(i)["cards"][key.c_str()], false);
  apply(StudioDoc("types")["cards"][key.c_str()], true);
}

bool HasModButtons(const std::string &key) {
  if (StudioDoc("types")["cards"][key.c_str()].size() > 0)
    return true;
  for (int i = 0; i < StudioDependencies(); ++i)
    if (StudioDependencyTypes(i)["cards"][key.c_str()].size() > 0)
      return true;
  return false;
}

// The card's key in "cards": a classic unit's own name, for a classic row and an edit of one.
std::string CardKey(const std::string &unit) {
  StudioRecord r;
  if (!StudioRecordOf(ST_UNITS, unit, &r))
    return unit;
  if (r.kind == SK_EDIT || r.kind == SK_CLASSIC) {
    const char *psz = StudioClassicName(ST_UNITS, r.nId);
    return psz ? psz : unit;
  }
  return unit;
}

// The workers' build menus, classic cards 238 to 243, by the names "cards" files them under; the
// worker whose card opens one (Drone, SCV, Probe).
const char *const kMenuKeys[6] = {"zerg_basic_structures",    "terran_basic_structures",    "protoss_basic_structures",
                                  "zerg_advanced_structures", "terran_advanced_structures", "protoss_advanced_structures"};
int MenuCard(const std::string &key) {
  for (int i = 0; i < 6; ++i)
    if (key == kMenuKeys[i])
      return 238 + i;
  if (key.size() >= 2 && key[0] == '#') {
    const int n = atoi(key.c_str() + 1);
    return n >= 238 && n <= 243 ? n : -1;
  }
  return -1;
}
std::string MenuKey(int nCard) {
  return nCard >= 238 && nCard <= 243 ? kMenuKeys[nCard - 238] : std::string();
}
std::string MenuWorker(int nCard) {
  static const int kWorker[3] = {41, 7, 64};  // zerg_drone, terran_scv, protoss_probe
  const char *psz = nCard >= 238 && nCard <= 243 ? StudioClassicName(ST_UNITS, kWorker[(nCard - 238) % 3]) : nullptr;
  return psz ? psz : std::string();
}

Card Build(const std::string &unit) {
  Card c;
  c.unit = unit;
  StudioRecord r;
  if (MenuCard(unit) >= 0) {
    c.nClassic = MenuCard(unit);
    c.key = MenuKey(c.nClassic);
    c.bMenu = true;
    c.bOk = true;
  } else {
    c.key = CardKey(unit);
    if (!StudioRecordOf(ST_UNITS, unit, &r))
      return c;
    c.bOk = true;
    c.bModUnit = r.kind == SK_OWN || r.kind == SK_DEPENDENCY;
    if (!c.bModUnit) {
      c.nClassic = StudioClassicRow(ST_UNITS, c.key);
    } else {
      c.bOwnCard = HasModButtons(c.key);
      // a card of its own starts from its looks' card; without one, its "buttons" or its looks'
      c.nClassic = LooksRow(unit, c.bOwnCard ? nullptr : "buttons");
    }
  }
  if (c.nClassic >= 0)
    c.btns = ClassicButtons(c.nClassic);
  if (c.bMenu || !c.bModUnit || c.bOwnCard)
    ClassicWords(c.key, &c.btns);
  if (c.bMenu || !c.bModUnit || c.bOwnCard) {
    std::vector<Btn> mod;
    ModButtons(c.key, &mod);
    // each put after the buttons whose places are not after its own, as the game puts them
    for (const Btn &b : mod) {
      size_t at = 0;
      while (at < c.btns.size() && c.btns[at].nPos <= b.nPos)
        ++at;
      c.btns.insert(c.btns.begin() + static_cast<long long>(at), b);
    }
  }
  std::set<int> seen;
  for (Btn &b : c.btns) {
    b.bAlt = seen.count(b.nPos) != 0;
    seen.insert(b.nPos);
  }
  return c;
}

// The cards, made again when the mod changes or the game's strings come.
const Card &CardOf(const std::string &unit) {
  static std::map<std::string, Card> s_cards;
  static unsigned s_nChange = ~0u;
  static bool s_bStrings = false;
  if (s_nChange != StudioChange() || s_bStrings != (sgpStatStrs != NULL)) {
    s_cards.clear();
    s_nChange = StudioChange();
    s_bStrings = sgpStatStrs != NULL;
  }
  auto it = s_cards.find(unit);
  if (it != s_cards.end())
    return it->second;
  return s_cards[unit] = Build(unit);
}

// The first button at a place, -1 for none.
int AtPlace(const Card &c, int nPos) {
  for (size_t i = 0; i < c.btns.size(); ++i)
    if (c.btns[i].nPos == nPos)
      return static_cast<int>(i);
  return -1;
}

int ModIndex(const Card &c, int nMod) {
  for (size_t i = 0; i < c.btns.size(); ++i)
    if (c.btns[i].nMod == nMod && !c.btns[i].bClassic && c.btns[i].dep.empty())
      return static_cast<int>(i);
  return -1;
}

// ---- clashes ----------------------------------------------------------------------------------------

// The races of the mod that go without a unit, by their names.
std::vector<std::string> RacesWithout(const std::string &unit) {
  std::vector<std::string> out;
  auto look = [&](const PortJson &races) {
    for (const PortJsonMember &m : races.members)
      for (size_t i = 0; i < m.second["without"].size(); ++i)
        if (NameIn(ST_UNITS, m.second["without"].At(i)) == unit) {
          const std::string n = m.second["name"].Str();
          out.push_back(n.empty() ? m.first : n);
        }
  };
  look(StudioDoc("types")["races"]);
  for (int i = 0; i < StudioDependencies(); ++i)
    look(StudioDependencyTypes(i)["races"]);
  return out;
}

std::string Joined(const std::vector<std::string> &v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i)
    s += (i == 0 ? "" : i + 1 == v.size() ? " and " : ", ") + v[i];
  return s;
}

// Two buttons with one hotkey at different places; a button of the mod's at a place another is at,
// which moves it on to the next place when both show.
struct Clashes {
  std::map<char, std::vector<int>> keys;
  std::map<int, std::vector<int>> places;
  std::map<int, std::vector<int>> frees;  // a place the mod's button takes for a race without the classic one there
  bool Key(const Btn &b) const {
    return b.cKey && keys.count(b.cKey) != 0;
  }
  bool Place(int nPos) const {
    return places.count(nPos) != 0;
  }
  bool Any() const {
    return !keys.empty() || !places.empty() || !frees.empty();
  }
  bool Bad() const {
    return !keys.empty() || !places.empty();
  }
};

Clashes ClashesOf(const Card &c) {
  Clashes out;
  std::map<char, std::vector<int>> byKey;
  for (size_t i = 0; i < c.btns.size(); ++i)
    if (c.btns[i].cKey && isalnum(static_cast<unsigned char>(c.btns[i].cKey)))
      byKey[c.btns[i].cKey].push_back(static_cast<int>(i));
  for (const auto &k : byKey) {
    std::set<int> places;
    bool bMod = false;
    for (int i : k.second) {
      places.insert(c.btns[static_cast<size_t>(i)].nPos);
      bMod = bMod || !c.btns[static_cast<size_t>(i)].bClassic;
    }
    // the classic cards' own pairs (Siege and Tank Mode) share a place and a key: never both shown
    if (places.size() > 1 && bMod)
      out.keys[k.first] = k.second;
  }
  for (int p = 1; p <= 9; ++p) {
    std::vector<int> here;
    bool bMod = false;
    for (size_t i = 0; i < c.btns.size(); ++i)
      if (c.btns[i].nPos == p) {
        here.push_back(static_cast<int>(i));
        bMod = bMod || !c.btns[i].bClassic;
      }
    if (here.size() > 1 && bMod) {
      // one of the mod's after classic buttons a race of the mod goes without: it shows for that race
      int nMods = 0;
      bool bFreed = true;
      for (int i : here) {
        const Btn &b = c.btns[static_cast<size_t>(i)];
        if (!b.bClassic)
          ++nMods;
        else
          bFreed = bFreed && !b.target.empty() && !RacesWithout(b.target).empty();
      }
      if (nMods == 1 && bFreed && c.btns[static_cast<size_t>(here.front())].bClassic)
        out.frees[p] = here;
      else
        out.places[p] = here;
    }
  }
  return out;
}

// A key no other button of the card has: a letter of the text first.
char FreeKey(const Card &c, const std::string &text, int nSkip) {
  std::set<char> used;
  for (size_t i = 0; i < c.btns.size(); ++i)
    if (static_cast<int>(i) != nSkip && c.btns[i].cKey)
      used.insert(c.btns[i].cKey);
  // the name's letters before the verb's: Hybrid Reaver's, not Warp in's
  std::string name = Plain(text);
  for (const char *psz : {"Warp in ", "Train ", "Research ", "Upgrade ", "Morph to ", "Morph into ", "Mutate into ", "Build "})
    if (name.compare(0, strlen(psz), psz) == 0)
      name = name.substr(strlen(psz)) + " " + name.substr(0, strlen(psz));
  std::string initials;
  bool bStart = true;
  for (char ch : name) {
    if (bStart && isalpha(static_cast<unsigned char>(ch)))
      initials += ch;
    bStart = ch == ' ' || ch == '-';
  }
  const std::string letters = Lc(initials + name) + "abcdefghijklmnopqrstuvwxyz";
  for (char ch : letters)
    if (isalpha(static_cast<unsigned char>(ch)) && !used.count(ch))
      return ch;
  return 'x';
}

int FreePlace(const Card &c) {
  for (int p = 1; p <= 9; ++p)
    if (AtPlace(c, p) < 0)
      return p;
  return 0;
}

std::string KeyCap(char c) {
  if (c == 27)
    return "Esc";
  return c > ' ' ? std::string(1, static_cast<char>(toupper(static_cast<unsigned char>(c)))) : std::string("?");
}

// What a button does, in words.
std::string Does(const Btn &b) {
  if (b.target.empty())
    return b.what == "command" ? std::string("a classic command") : b.what;
  const std::string n = StudioShowName(b.t, b.target);
  if (b.what == "train")
    return "trains the " + n;
  if (b.what == "morph")
    return "morphs into the " + n;
  if (b.what == "build")
    return "builds the " + n;
  if (b.what == "addon")
    return "builds the add-on " + n;
  if (b.what == "research")
    return "researches " + n;
  if (b.what == "upgrade")
    return "upgrades " + n;
  if (b.what == "cast")
    return std::string("casts ") + n + (b.bAimed ? ", aimed" : b.bClassic ? "" : ", where the caster stands");
  return b.what + " " + n;
}

// The verb a race trains with: Train, Warp in, Morph to.
std::string TrainVerb(const std::string &cardUnit) {
  const int r = UnitRace(cardUnit);
  return r == 0 ? "Morph to" : r == 2 ? "Warp in" : "Train";
}

// The verb a build menu's classic buttons use ("Build"), from its first button's text.
std::string BuildVerb(const std::string &menu) {
  const int n = MenuCard(menu);
  if (n >= 0)
    for (const Btn &b : ClassicButtons(n)) {
      const std::string text = Plain(b.text), name = Short(ST_UNITS, b.target);
      const size_t at = name.empty() ? std::string::npos : text.rfind(name);
      if (at != std::string::npos && at > 1 && at + name.size() == text.size())
        return text.substr(0, at - 1);
    }
  return "Build";
}

std::string DefaultText(const std::string &kind, int t, const std::string &target, const std::string &cardUnit) {
  const std::string n = t == ST_UNITS ? Short(ST_UNITS, target) : StudioShowName(t, target);
  if (kind == "train")
    return TrainVerb(cardUnit) + " " + n;
  if (kind == "upgrade")
    return "Upgrade " + n;
  if (kind == "research")
    return "Research " + n;
  if (kind == "build")
    return BuildVerb(cardUnit) + " " + n;
  return n;
}

// Every card a button of which makes, researches, upgrades or casts a record: the classic cards'
// and the mod's, each the unit whose card it is.
struct Where {
  std::string unit;   // whose card ("" for a classic card no unit has, as a worker's build menu)
  int nCard = -1;     // the classic card, when it is one
  bool bClassic = true;
};

std::vector<Where> WhereOn(const char *pszWhat, const std::string &target) {
  std::vector<Where> out;
  std::set<std::string> seen;
  const std::string what = pszWhat;
  auto match = [&](const Btn &b) {
    if (b.target != target)
      return false;
    if (what == "make")
      return b.what == "train" || b.what == "morph" || b.what == "build" || b.what == "addon";
    return b.what == what;
  };
  for (int n = 0; n < 250; ++n)
    for (const Btn &b : ClassicButtons(n))
      if (match(b)) {
        const char *psz = n < StudioClassicRows(ST_UNITS) ? StudioClassicName(ST_UNITS, n) : nullptr;
        // a build menu by its number, as the Cards page shows it
        std::string unit = psz ? psz : MenuKey(n);
        if (!seen.count(unit + "#" + std::to_string(n))) {
          seen.insert(unit + "#" + std::to_string(n));
          out.push_back({unit, n, true});
        }
        break;
      }
  auto mods = [&](const PortJson &cards) {
    for (const PortJsonMember &m : cards.members)
      for (size_t k = 0; k < m.second.size(); ++k) {
        const Btn b = FromJson(m.second.At(k), static_cast<int>(k), std::string());
        if (match(b) && !seen.count(m.first)) {
          seen.insert(m.first);
          out.push_back({m.first, -1, false});
        }
      }
  };
  for (int i = 0; i < StudioDependencies(); ++i)
    mods(StudioDependencyTypes(i)["cards"]);
  mods(StudioDoc("types")["cards"]);
  return out;
}

// A card's name: a unit's, or a build menu's ("SCV: basic structures").
std::string CardName(const std::string &key) {
  const int n = MenuCard(key);
  if (n < 0)
    return StudioShowName(ST_UNITS, key);
  return Short(ST_UNITS, MenuWorker(n)) + (n >= 241 ? ": advanced structures" : ": basic structures");
}
int CardRace(const std::string &key) {
  const int n = MenuCard(key);
  return n >= 0 ? (n - 238) % 3 : UnitRace(key);
}

// The worker whose build menu a classic card past the units' is: Terran, Protoss or Zerg by what it builds.
std::string MenuName(int nCard) {
  for (const Btn &b : ClassicButtons(nCard))
    if (b.what == "build" || b.what == "addon") {
      const int r = UnitRace(b.target);
      return r == 0 ? "a Drone's build menu" : r == 1 ? "an SCV's build menu" : r == 2 ? "a Probe's build menu" : "a build menu";
    }
  return "card " + std::to_string(nCard);
}

// ---- the panels' state ------------------------------------------------------------------------------

std::string g_cardUnit = "protoss_gateway";  // the Cards page's unit
std::string g_selKey;                        // the card whose button is chosen
int g_nSel = -1;                             // its place in the card's buttons as dealt
char g_szFind[64];
int g_nRaceList = 0;                         // the list's race: all, Protoss, Terran, Zerg
std::string g_dragKey;                       // the card a button is dragged on
int g_nAddPos = 0;                           // the place a new button is being picked for
std::string g_addKey;

void Ask(const char *pszCmd, const std::string &fields) {
  StudioAsk(pszCmd, fields);
}

void SetFields(const std::string &key, int nMod, const std::string &fields) {
  Ask("studio_card_set", "\"unit\":" + Q(key) + ",\"index\":" + std::to_string(nMod) + ",\"fields\":{" + fields + "}");
}

// Text typed in a field, given to the model when the field lets go of the keyboard.
struct Typing {
  char sz[512] = {};
  bool bOn = false;
};

}  // namespace

bool studio_races::TextField(const char *pszId, const std::string &key, const std::string &now, float fW, float fH, std::string *pOut,
                             bool bMulti, const char *pszHint, int nMax, float fFs) {
  static std::map<std::string, Typing> s_typing;
  Typing &ty = s_typing[key];
  if (!ty.bOn)
    snprintf(ty.sz, sizeof ty.sz, "%s", now.c_str());
  ImGuiID id = 0;
  if (bMulti) {
    StudioPushFont(STUDIO_FONT_TEXT, fFs);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, C(TK_WELL));
    ImGui::PushStyleColor(ImGuiCol_Text, C(TK_HI));
    ImGui::InputTextMultiline(pszId, ty.sz, static_cast<size_t>(std::min(nMax + 1, 512)), ImVec2(P(fW), P(fH)),
                              ImGuiInputTextFlags_WordWrap);
    id = ImGui::GetItemID();
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
  } else {
    id = ImGui::GetID(pszId);
    StudioInput(pszId, ty.sz, std::min(nMax + 1, 512), fW, fH, pszHint, nullptr, nullptr, false, fFs);
  }
  const bool bActive = ImGui::GetActiveID() == id && id != 0;
  if (bActive) {
    ty.bOn = true;
    return false;
  }
  if (ty.bOn) {
    ty.bOn = false;
    if (now != ty.sz) {
      *pOut = ty.sz;
      return true;
    }
  }
  return false;
}

namespace {

using namespace studio_draw;
using namespace studio_races;

// ---- drawing a card ---------------------------------------------------------------------------------

std::string Initials(const std::string &text) {
  std::string s, plain = Plain(text);
  for (const char *psz : {"Warp in ", "Train ", "Research ", "Upgrade ", "Morph to ", "Morph into ", "Mutate into ", "Build ", "Develop ",
                          "Set "})
    if (plain.compare(0, strlen(psz), psz) == 0 && plain.size() > strlen(psz))
      plain = plain.substr(strlen(psz));
  bool bStart = true;
  for (char ch : plain) {
    if (ch == ' ' || ch == '-')
      bStart = true;
    else if (bStart && isalnum(static_cast<unsigned char>(ch))) {
      s += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
      bStart = false;
      if (s.size() == 3)
        break;
    }
  }
  return s.empty() ? std::string("?") : s;
}

struct GridHit {
  int nClick = -1;    // a button clicked, by its place in the card
  int nEmpty = 0;     // an empty place clicked
  int nMoveMod = -1;  // the mod's button dropped
  int nMoveTo = 0;    // on this place
};

struct GridOpts {
  float cell = 86.0f;
  bool bEdit = false;           // the mod's buttons move, and empty places take one
  std::string mark;             // the unit whose button is drawn with a double edge
  std::string markWhat;         // what that button does to it ("make", "research", "upgrade")
  bool bText = true;            // the text under each face
};

bool MarkMatch(const Btn &b, const GridOpts &o) {
  if (o.mark.empty() || b.target != o.mark)
    return false;
  if (o.markWhat == "make")
    return b.what == "train" || b.what == "morph" || b.what == "build" || b.what == "addon";
  return b.what == o.markWhat;
}

// A button's command icon: its own, else what the game gives it (a tech's or an upgrade's icon, a
// trained unit's); false when there is none to draw and the caller draws its initials.
bool Face(ImDrawList *d, ImVec2 a, ImVec2 b, const Btn &bt) {
  if (bt.nIcon >= 0)
    return StudioIconPicture(d, a, b, bt.nIcon, false);
  if (bt.t == ST_UNITS && !bt.target.empty())
    return StudioUnitIconPicture(d, a, b, bt.target, false);
  std::string tech = bt.t == ST_TECHS ? bt.target : std::string();
  if (bt.t == ST_ORDERS && !bt.target.empty()) {
    const long long n = StudioNumber(ST_ORDERS, bt.target, "tech", -1);
    if (n >= 0 && n < 0xFFFF)
      tech = StudioRefName(ST_TECHS, n);
  }
  if (!tech.empty())
    return StudioIconPicture(d, a, b, static_cast<int>(StudioNumber(ST_TECHS, tech, "icon", -1)), false);
  if (bt.t == ST_UPGRADES && !bt.target.empty())
    return StudioIconPicture(d, a, b, static_cast<int>(StudioNumber(ST_UPGRADES, bt.target, "icon", -1)), false);
  return false;
}

// The card as the game shows it, three by three; true when something was done to it.
GridHit Grid(const char *pszId, const Card &c, const GridOpts &o) {
  GridHit hit;
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o0 = ImGui::GetCursorScreenPos();
  const float cell = P(o.cell), gap = P(o.cell > 60.0f ? 8.0f : 5.0f), pad = P(o.cell > 60.0f ? 10.0f : 6.0f);
  const float W = cell * 3 + gap * 2 + pad * 2;
  d->AddRectFilled(o0, ImVec2(o0.x + W, o0.y + W), C(TK_WELL), P(4.0f));
  d->AddRect(o0, ImVec2(o0.x + W, o0.y + W), C(TK_LINE), P(4.0f));
  const Clashes cl = ClashesOf(c);
  ImGui::PushID(pszId);
  for (int p = 1; p <= 9; ++p) {
    const ImVec2 a(o0.x + pad + ((p - 1) % 3) * (cell + gap), o0.y + pad + ((p - 1) / 3) * (cell + gap));
    const ImVec2 b(a.x + cell, a.y + cell);
    // the place's first button, or the one marked when it shares the place
    int i = AtPlace(c, p);
    for (size_t k = 0; !o.mark.empty() && k < c.btns.size(); ++k)
      if (c.btns[k].nPos == p && MarkMatch(c.btns[k], o))
        i = static_cast<int>(k);
    ImGui::PushID(p);
    ImGui::SetCursorScreenPos(a);
    const bool bClick = ImGui::InvisibleButton("##cell", ImVec2(cell, cell));
    const bool bHover = ImGui::IsItemHovered();
    // a drag is the cell's, so it is begun before anything else (a picture, a tooltip) is the last item:
    // the mod's own buttons are dragged to another place
    if (o.bEdit && i >= 0 && c.btns[static_cast<size_t>(i)].nMod >= 0 &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      const Btn &bt = c.btns[static_cast<size_t>(i)];
      g_dragKey = c.key;
      ImGui::SetDragDropPayload("STUDIO_CARD_BUTTON", &bt.nMod, sizeof(int));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_HI), ("Move " + Plain(bt.text)).c_str());
      ImGui::EndDragDropSource();
    }
    if (o.bEdit && ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("STUDIO_CARD_BUTTON"))
        if (g_dragKey == c.key) {
          hit.nMoveMod = *static_cast<const int *>(pl->Data);
          hit.nMoveTo = p;
        }
      ImGui::EndDragDropTarget();
    }
    if (i < 0) {
      StudioDashed(d, ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), bHover && o.bEdit ? C(TK_BTN_RIM_HI) : C(TK_LINE));
      if (o.bEdit) {
        const float g = P(o.cell > 60.0f ? 16.0f : 11.0f);
        StudioGlyph(d, "plus", ImVec2(a.x + (cell - g) / 2.0f, a.y + (cell - g) / 2.0f - (o.bText ? P(6.0f) : 0.0f)), g,
                    bHover ? C(TK_HI) : C(TK_INK3));
        if (o.bText && o.cell > 60.0f) {
          const char *psz = !g_selKey.empty() && g_selKey == c.key && g_nSel >= 0 && g_nSel < static_cast<int>(c.btns.size()) &&
                                    c.btns[static_cast<size_t>(g_nSel)].nMod >= 0
                                ? "move here"
                                : "add";
          const float tw = TW(STUDIO_FONT_TEXT, kStudioFsXs, psz);
          T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + (cell - tw) / 2.0f, b.y - P(22.0f), C(TK_INK3), psz);
        }
        if (bClick)
          hit.nEmpty = p;
      }
    } else {
      const Btn &bt = c.btns[static_cast<size_t>(i)];
      const bool bSel = g_selKey == c.key && g_nSel == i;
      const bool bClash = cl.Key(bt) || cl.Place(p);
      ImU32 top = C(TK_BTN_TOP), bot = C(TK_BTN_BOT);
      d->AddRectFilledMultiColor(a, b, top, top, bot, bot);
      const ImU32 rim = bSel ? C(TK_SEL_LINE) : bClash ? C(TK_BAD) : bt.bClassic ? C(TK_LINE2) : C(TK_MOD);
      d->AddRect(a, b, rim, P(3.0f), 0, bSel || !bt.bClassic ? P(2.0f) : P(1.0f));
      if (MarkMatch(bt, o))
        d->AddRect(ImVec2(a.x - P(4.0f), a.y - P(4.0f)), ImVec2(b.x + P(4.0f), b.y + P(4.0f)), C(TK_GOOD), P(4.0f), 0, P(2.0f));
      if (bHover)
        d->AddRectFilled(a, b, C(TK_HI, 0.06f), P(3.0f));
      // the face: the unit it makes, else the text's initials
      const float fs = o.cell > 60.0f ? 22.0f : 13.0f;
      const ImVec2 fa(a.x + P(6.0f), a.y + P(6.0f)), fb(b.x - P(6.0f), b.y - (o.bText && o.cell > 60.0f ? P(22.0f) : P(6.0f)));
      if (!Face(d, fa, fb, bt)) {
        const std::string ini = Initials(bt.text);
        const float tw = TW(STUDIO_FONT_BOLD, fs, ini);
        T(d, STUDIO_FONT_BOLD, fs, floorf((fa.x + fb.x - tw) / 2.0f), floorf((fa.y + fb.y - P(fs)) / 2.0f),
          bt.bClassic ? C(TK_INK2) : C(TK_HI), ini);
      }
      StudioKbd(d, ImVec2(a.x + P(4.0f), a.y + P(4.0f)), KeyCap(bt.cKey).c_str(), o.cell > 60.0f ? 18.0f : 14.0f);
      if (bClash) {
        const float r = P(o.cell > 60.0f ? 8.0f : 6.0f);
        const ImVec2 cc(b.x - r - P(3.0f), a.y + r + P(3.0f));
        d->AddCircleFilled(cc, r, C(TK_BAD));
        const float tw = TW(STUDIO_FONT_BOLD, o.cell > 60.0f ? 11.0f : 9.0f, "!");
        T(d, STUDIO_FONT_BOLD, o.cell > 60.0f ? 11.0f : 9.0f, cc.x - tw / 2.0f, cc.y - P(o.cell > 60.0f ? 7.0f : 5.5f), C(TK_BLACK), "!");
      }
      // more buttons at this place: shown when the first is not
      int nMore = 0;
      for (const Btn &x : c.btns)
        nMore += x.nPos == p ? 1 : 0;
      if (nMore > 1 && o.cell > 60.0f) {
        const std::string s = "+" + std::to_string(nMore - 1);
        const float tw = TW(STUDIO_FONT_MONO, kStudioFsXs, s);
        T(d, STUDIO_FONT_MONO, kStudioFsXs, b.x - tw - P(5.0f), b.y - P(38.0f), C(TK_INK2), s);
      }
      if (o.bText && o.cell > 60.0f)
        T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(5.0f), b.y - P(19.0f), bt.bClassic ? C(TK_INK2) : C(TK_INK), Plain(bt.text),
          cell - P(10.0f));
      if (bHover) {
        std::string tip = Plain(bt.text) + " (" + KeyCap(bt.cKey) + ")\n" + Does(bt) + "\n" +
                          (bt.bClassic ? "a classic button" : bt.dep.empty() ? "the mod's" : "from " + bt.dep);
        for (const Btn &x : c.btns)
          if (x.nPos == p && &x != &bt)
            tip += "\nalso here: " + Plain(x.text) + (x.bClassic ? "" : ", the mod's");
        ImGui::SetTooltip("%s", tip.c_str());
      }
      if (bClick)
        hit.nClick = i;
    }
    ImGui::PopID();
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(o0);
  ImGui::Dummy(ImVec2(W, W));
  return hit;
}

// ---- adding a button --------------------------------------------------------------------------------

bool HasRequires(const std::string &tech) {
  return StudioRecordJson(ST_TECHS, tech)["requires"].kind != PortJson::NONE;
}

// An order the mod can put on a card as a cast: one of the mod's, whose tech is the mod's.
bool CastOk(const std::string &order) {
  if (!IsMod(ST_ORDERS, order))
    return false;
  const std::string tech = StudioRefName(ST_TECHS, StudioNumber(ST_ORDERS, order, "tech", -1));
  return IsMod(ST_TECHS, tech);
}

bool ModUnitOk(const std::string &u) {
  return IsMod(ST_UNITS, u);
}
bool ModUpgradeOk(const std::string &u) {
  return IsMod(ST_UPGRADES, u);
}
bool ModResearchOk(const std::string &u) {
  return IsMod(ST_TECHS, u) && HasRequires(u);
}

// The records of a kind a button of the mod can name: for a build menu, the mod's buildings of its
// race first, then its other units (the loader takes any of them).
std::vector<std::string> Choices(int k, const Card *pCard = nullptr) {
  std::vector<std::string> out, later;
  const int t = kKindTable[k];
  const int nRace = pCard && pCard->bMenu ? (pCard->nClassic - 238) % 3 : -1;
  for (const StudioRecord &r : StudioRecords(t)) {
    if (r.kind != SK_OWN && r.kind != SK_DEPENDENCY)
      continue;
    if (k == KIND_BUILD) {
      if (!ModUnitOk(r.name))
        continue;
      const bool bFits = (StudioNumber(ST_UNITS, r.name, "flags") & 1) && (nRace < 0 || UnitRace(r.name) == nRace);
      (bFits ? out : later).push_back(r.name);
    } else if ((k == 0 && ModUnitOk(r.name)) || (k == 1 && ModUpgradeOk(r.name)) || (k == 2 && ModResearchOk(r.name)) ||
               (k == 3 && CastOk(r.name))) {
      out.push_back(r.name);
    }
  }
  out.insert(out.end(), later.begin(), later.end());
  return out;
}

std::string NewButton(const Card &c, int nPos, int k, const std::string &target) {
  const std::string text0 = DefaultText(kKinds[k], kKindTable[k], target, c.unit);
  const char key = FreeKey(c, text0, -1);
  return "{\"position\":" + std::to_string(nPos) + "," + Q(kKinds[k]) + ":" + Q(target) + ",\"hotkey\":" +
         Q(std::string(1, key)) + ",\"text\":" + Q(HotText(text0, key)) + "}";
}

// The list of what a new button at a place can do, in a popup: true when one was made.
void AddPopup(const Card &c) {
  if (!ImGui::BeginPopup("##addbutton"))
    return;
  StudioLabel(STUDIO_FONT_BOLD, kStudioFsS, C(TK_HEAD), ("A new button at place " + std::to_string(g_nAddPos)).c_str());
  static const char *const kHeads[] = {"Train one of the mod's units", "Upgrade one of the mod's upgrades",
                                       "Research one of the mod's techs", "Cast one of the mod's actions",
                                       "Build one of the mod's buildings"};
  bool bAny = false;
  for (int k = c.bMenu ? KIND_BUILD : 0; k < (c.bMenu ? KINDS : KIND_BUILD); ++k) {
    const std::vector<std::string> list = Choices(k, &c);
    if (list.empty())
      continue;
    bAny = true;
    ImGui::Dummy(ImVec2(1, P(4.0f)));
    StudioLabel(STUDIO_FONT_WIDE, 9.0f, C(TK_INK3), kHeads[k], 0.08f, true);
    for (const std::string &n : list) {
      ImGui::PushID((std::to_string(k) + n).c_str());
      if (StudioItem(StudioShowName(kKindTable[k], n).c_str())) {
        Ask("studio_card_set", "\"unit\":" + Q(c.key) + ",\"button\":" + NewButton(c, g_nAddPos, k, n));
        g_selKey = c.key;
        g_nSel = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopID();
    }
  }
  if (!bAny && c.bMenu) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "The mod has no unit a worker could build yet.");
  } else if (!bAny) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "The mod has nothing a button can name yet:");
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "a unit, an upgrade, a tech with Needs, or an action.");
  }
  ImGui::EndPopup();
}

// ---- a button's editor ------------------------------------------------------------------------------

void Row(const char *pszLabel, float lw) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_LABEL), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(p.x + P(lw), p.y));
}

void Gap(float f = 8.0f) {
  ImGui::Dummy(ImVec2(1, P(f)));
}

// Words wrapped at a width, small.
void Words(const std::string &s, float fW, ImU32 col) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushTextWrapPos(p.x + fW - ImGui::GetWindowPos().x);
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsXs);
  ImGui::PushStyleColor(ImGuiCol_Text, col);
  ImGui::TextWrapped("%s", s.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
}

// A text with its marked letter drawn bright, as the game's tooltip shows it.
void MarkedText(ImDrawList *d, float x, float y, const std::string &text, float fs, float maxw) {
  float cx = x;
  for (size_t i = 0; i < text.size(); ++i) {
    if (cx - x > maxw)
      break;
    if (text[i] == '\n') {
      cx += TW(STUDIO_FONT_TEXT, fs, " ");
      continue;
    }
    const bool bMark = text[i] == '<' && i + 2 < text.size() && text[i + 2] == '>';
    const std::string ch(1, bMark ? text[i + 1] : text[i]);
    cx += T(d, bMark ? STUDIO_FONT_BOLD : STUDIO_FONT_TEXT, fs, cx, y, bMark ? C(TK_WARN) : C(TK_INK), ch);
    if (bMark)
      i += 2;
  }
}

// A classic button's words given by the mod: set (its entry made or changed) or taken back.
void SetWords(const Card &c, const Btn &b, const std::string &fields) {
  if (b.nWords >= 0) {
    SetFields(c.key, b.nWords, fields);
    return;
  }
  Ask("studio_card_set", "\"unit\":" + Q(c.key) + ",\"button\":{\"position\":" + std::to_string(b.nPos) + ",\"classic\":" +
                             (b.target.empty() ? std::string("true") : Q(b.target)) + "," + fields + "}");
}

// A classic button: what it does, and its words, which the mod may change (its text and hotkey, and
// its text while what it needs is missing); the button stays where it is and does what it did.
void ClassicWordsPane(const Card &c, const Btn &b, int i, float fW, float lw, float fw) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const std::string id = c.key + "#c" + std::to_string(i);
  Words("A classic button: the mod does not move it or change what it does, but may change its words. A button of the mod's at "
        "its place shows there for a race that goes without what this one makes.",
        fW, C(TK_INK3));
  Gap(6.0f);
  std::string out;
  Row("Text", lw);
  if (TextField("##ctext", id + "text", b.text, std::min(fw, 300.0f), 26.0f, &out, false, "Build <A>cademy")) {
    const char m = Marked(out);
    const char key = m ? m : b.cKey;
    SetWords(c, b, "\"text\":" + Q(m ? out : HotText(out, key)) + (key != b.cKey && key ? ",\"hotkey\":" + Q(std::string(1, key)) : ""));
  }
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    MarkedText(d, q.x + P(lw), q.y + P(2.0f), b.text, kStudioFsS, fW - P(lw));
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
  }
  Row("Hotkey", lw);
  if (TextField("##ckey", id + "key", KeyCap(b.cKey), 44.0f, 26.0f, &out, false, nullptr, 1) && !out.empty() &&
      isalnum(static_cast<unsigned char>(out[0]))) {
    const char key = static_cast<char>(tolower(static_cast<unsigned char>(out[0])));
    SetWords(c, b, "\"hotkey\":" + Q(std::string(1, key)) + ",\"text\":" + Q(HotText(b.text, key)));
  }
  Gap(4.0f);
  Row("When it cannot", lw);
  if (TextField("##cdis", id + "dis", b.disabled, std::min(fw, 300.0f), 48.0f, &out, true))
    SetWords(c, b, "\"disabled\":" + (out.empty() ? std::string("null") : Q(out)));
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + P(lw), ImGui::GetCursorScreenPos().y));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "greyed out with this text while what it needs is missing");
  if (b.bReworded) {
    Gap(6.0f);
    Words("The classic words: " + Plain(b.classicText) + (b.classicDisabled.empty() ? "" : "; when it cannot: " + Plain(b.classicDisabled)),
          fW, C(TK_INK3));
    if (b.nWords >= 0 &&
        StudioButton("##cback", "Back to the classic words", "undo", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      Ask("studio_card_set", "\"unit\":" + Q(c.key) + ",\"index\":" + std::to_string(b.nWords) + ",\"remove\":true");
  }
  if (!b.target.empty()) {
    Gap(4.0f);
    if (StudioButton("##open", ("Open " + StudioShowName(b.t, b.target)).c_str(), "link", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioOpenDoc(b.t, b.target, b.t == ST_UNITS ? 6 : 0);
  }
}

// The words for a classic button, or the mod's button's editor; w the editor's width.
void ButtonPane(const Card &c, int i, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Btn &b = c.btns[static_cast<size_t>(i)];
  const float lw = 104.0f, fw = std::max(160.0f, fW / StudioPx(1.0f) - lw - 8.0f);
  ImGui::PushID(i);
  ImVec2 p = ImGui::GetCursorScreenPos();
  float x = p.x + T(d, STUDIO_FONT_BOLD, 17.0f, p.x, p.y, C(TK_HI), Plain(b.text), fW - P(140.0f)) + P(10.0f);
  StudioChip(d, ImVec2(x, p.y + P(2.0f)), b.bClassic ? "classic" : b.dep.empty() ? "the mod's" : ("from " + b.dep).c_str(),
             b.bClassic ? CH_GOLD : b.dep.empty() ? CH_GOOD : CH_PLAIN);
  ImGui::Dummy(ImVec2(fW, P(26.0f)));
  p = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y, C(TK_INK2),
    "Place " + std::to_string(b.nPos) + " (row " + std::to_string((b.nPos + 2) / 3) + ", column " + std::to_string((b.nPos - 1) % 3 + 1) +
        "), hotkey " + KeyCap(b.cKey) + "; " + Does(b),
    fW);
  ImGui::Dummy(ImVec2(fW, P(22.0f)));
  if (b.nMod < 0 && b.bClassic) {
    ClassicWordsPane(c, b, i, fW, lw, fw);
    ImGui::PopID();
    return;
  }
  if (b.nMod < 0) {
    // a classic button, or a dependency's: what it is, and how to change it
    if (!b.disabled.empty()) {
      Row("When it cannot", lw);
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), Plain(b.disabled).c_str());
    }
    if (b.nIcon >= 0) {
      Row("Icon", lw);
      StudioLabel(STUDIO_FONT_MONO, kStudioFsS, C(TK_INK2), ("command icon " + std::to_string(b.nIcon)).c_str());
    }
    Gap(4.0f);
    Words(b.bClassic ? "A classic button: the mod does not move it. A button of the mod's at its place shows there for a race that goes "
                       "without what this one makes, and goes on to the next place when both show."
                     : "A button of a mod this one depends on: change it in that mod.",
          fW, C(TK_INK3));
    if (!b.target.empty()) {
      Gap(4.0f);
      if (StudioButton("##open", ("Open " + StudioShowName(b.t, b.target)).c_str(), "link", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
        StudioOpenDoc(b.t, b.target, b.t == ST_UNITS ? 6 : 0);
    }
    ImGui::PopID();
    return;
  }
  const std::string id = c.key + "#" + std::to_string(b.nMod);
  // at a place a classic button has: when it shows there
  for (const Btn &o : c.btns)
    if (o.bClassic && o.nPos == b.nPos) {
      const std::vector<std::string> without = o.target.empty() ? std::vector<std::string>() : RacesWithout(o.target);
      const std::string n = o.target.empty() ? Plain(o.text) : StudioShowName(o.t, o.target);
      const std::string place = "Place " + std::to_string(b.nPos) + " is the " + n + "'s: ";
      Words(without.empty() ? place + "this shows there only for a race that goes without the " + n +
                                  ", and no race of the mod does, so it goes on to the next place."
                            : place + "this shows in its place for " + Joined(without) + ", which go" + (without.size() == 1 ? "es" : "") +
                                  " without the " + n + "; for the other races it goes on to the next place.",
            fW, without.empty() ? C(TK_WARN) : C(TK_INK2));
      Gap(4.0f);
      break;
    }
  // what it does: the kind, and the record it names
  int k = c.bMenu ? KIND_BUILD : 0;
  for (int n = 0; n < KINDS; ++n)
    if (b.what == kKinds[n])
      k = n;
  static const char *const kSeg[] = {"Train", "Upgrade", "Research", "Cast"};
  static int s_nKind = -1;
  static std::string s_kindFor;
  if (s_kindFor != id) {
    s_kindFor = id;
    s_nKind = k;
  }
  Row("Does", lw);
  if (c.bMenu) {
    s_nKind = KIND_BUILD;
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Builds: a build menu's buttons each build one of the mod's units");
  } else {
    s_nKind = StudioSeg("##kind", kSeg, 4, std::min(s_nKind, 3), 26.0f);
  }
  Gap(4.0f);
  Row(s_nKind == 0 ? "Trains" : s_nKind == 1 ? "Upgrades" : s_nKind == 2 ? "Researches" : s_nKind == 3 ? "Casts" : "Builds", lw);
  {
    const std::vector<std::string> list = Choices(s_nKind, &c);
    const std::string now = s_nKind == k ? b.target : std::string();
    const std::string show = now.empty() ? std::string(list.empty() ? "the mod has none" : "pick one...") : StudioShowName(kKindTable[s_nKind], now);
    if (StudioSelectBegin("##target", show.c_str(), std::min(fw, 260.0f), 26.0f)) {
      for (const std::string &n : list) {
        ImGui::PushID(n.c_str());
        if (StudioItem(StudioShowName(kKindTable[s_nKind], n).c_str(), n == now)) {
          // the kind and its record replaced in one change; the text follows when it was the default
          std::string f = "\"train\":null,\"upgrade\":null,\"research\":null,\"cast\":null,\"build\":null," + Q(kKinds[s_nKind]) +
                          ":" + Q(n);
          if (s_nKind != 3)
            f += ",\"target\":null,\"ai\":null";
          const std::string def0 = DefaultText(b.what, b.t, b.target, c.unit);
          if (Plain(b.text) == Plain(def0) || b.text.empty())
            f += ",\"text\":" + Q(HotText(DefaultText(kKinds[s_nKind], kKindTable[s_nKind], n, c.unit), b.cKey));
          SetFields(c.key, b.nMod, f);
        }
        ImGui::PopID();
      }
      StudioSelectEnd();
    }
    if (!now.empty()) {
      ImGui::SameLine(0.0f, P(4.0f));
      if (StudioButton("##opentarget", "", "link", SB_FLAT, 26.0f, 26.0f))
        StudioOpenDoc(kKindTable[s_nKind], now, kKindTable[s_nKind] == ST_UNITS ? 6 : 0);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open %s", StudioShowName(kKindTable[s_nKind], now).c_str());
    }
    if (list.empty() || s_nKind == 2) {
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + P(lw), ImGui::GetCursorScreenPos().y));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3),
                  s_nKind == 0   ? "The mod's units only: a classic unit is trained where the classic game has it."
                  : s_nKind == 1 ? "The mod's upgrades only."
                  : s_nKind == 2 ? "The mod's techs that have Needs to research (where it is researched)."
                  : s_nKind == 3 ? "The mod's actions whose tech is the mod's."
                                 : "The mod's units, its buildings of the menu's race first: built while its Needs are met.");
    }
  }
  if (s_nKind == 3 && k == 3) {
    Gap(4.0f);
    Row("Aimed", lw);
    bool bAimed = b.bAimed;
    if (StudioCheck("##aimed", "at a unit or a place, as the classic spells are", &bAimed))
      SetFields(c.key, b.nMod, std::string("\"target\":") + (bAimed ? "true" : "null"));
    Row("Computer", lw);
    const std::string show = b.ai.empty() ? std::string("does not cast it") : b.ai;
    if (StudioSelectBegin("##ai", show.c_str(), std::min(fw, 260.0f), 26.0f)) {
      if (StudioItem("does not cast it", b.ai.empty()))
        SetFields(c.key, b.nMod, "\"ai\":null");
      for (int f = 0; f < MOD_AI_FINDERS; ++f) {
        std::string label = kModAiFinders[f];
        for (char &ch : label)
          if (ch == '_')
            ch = ' ';
        label = std::string(f == MOD_AI_FINDERS - 1 ? "" : "finds targets as ") + label;
        ImGui::PushID(f);
        if (StudioItem(label.c_str(), b.ai == kModAiFinders[f]))
          SetFields(c.key, b.nMod, "\"ai\":" + Q(kModAiFinders[f]));
        ImGui::PopID();
      }
      StudioSelectEnd();
    }
  }
  Gap(8.0f);
  // the text, with its hotkey's letter marked
  Row("Text", lw);
  std::string out;
  if (TextField("##text", id + "text", b.text, std::min(fw, 300.0f), 26.0f, &out, false, "Warp in Hybrid <R>eaver")) {
    const char m = Marked(out);
    const char key = m ? m : b.cKey;
    SetFields(c.key, b.nMod, "\"text\":" + Q(m ? out : HotText(out, key)) + (key != b.cKey && key ? ",\"hotkey\":" + Q(std::string(1, key)) : ""));
  }
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    MarkedText(d, q.x + P(lw), q.y + P(2.0f), b.text, kStudioFsS, fW - P(lw));
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
  }
  // the hotkey, its clash, a free one in a click
  Row("Hotkey", lw);
  if (TextField("##key", id + "key", KeyCap(b.cKey), 44.0f, 26.0f, &out, false, nullptr, 1) && !out.empty() &&
      isalnum(static_cast<unsigned char>(out[0]))) {
    const char key = static_cast<char>(tolower(static_cast<unsigned char>(out[0])));
    SetFields(c.key, b.nMod, "\"hotkey\":" + Q(std::string(1, key)) + ",\"text\":" + Q(HotText(b.text, key)));
  }
  ImGui::SameLine(0.0f, P(8.0f));
  {
    std::vector<std::string> also;
    for (size_t n = 0; n < c.btns.size(); ++n)
      if (static_cast<int>(n) != i && c.btns[n].cKey == b.cKey && b.cKey && c.btns[n].nPos != b.nPos)
        also.push_back(Plain(c.btns[n].text));
    if (also.empty()) {
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_GOOD), "no clash on this card");
    } else {
      const char free = FreeKey(c, b.text, i);
      if (StudioButton("##freekey", ("Use " + KeyCap(free)).c_str(), "clash", SB_GOLD, 26.0f, 0.0f, kStudioFsXs))
        SetFields(c.key, b.nMod, "\"hotkey\":" + Q(std::string(1, free)) + ",\"text\":" + Q(HotText(b.text, free)));
      ImGui::SameLine(0.0f, P(8.0f));
      std::string s = "also ";
      for (size_t n = 0; n < also.size(); ++n)
        s += (n ? ", " : "") + also[n];
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_BAD), s.c_str());
    }
  }
  // the place: a small card to pick it on
  Gap(4.0f);
  Row("Place", lw);
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    const float s = P(20.0f), g = P(3.0f);
    for (int n = 1; n <= 9; ++n) {
      const ImVec2 a(q.x + ((n - 1) % 3) * (s + g), q.y + ((n - 1) / 3) * (s + g));
      ImGui::SetCursorScreenPos(a);
      ImGui::PushID(n + 100);
      const bool bClick = ImGui::InvisibleButton("##place", ImVec2(s, s));
      const bool bHover = ImGui::IsItemHovered();
      ImGui::PopID();
      const bool bOn = n == b.nPos, bTaken = AtPlace(c, n) >= 0;
      d->AddRectFilled(a, ImVec2(a.x + s, a.y + s), bOn ? C(TK_SEL_LINE) : bTaken ? C(TK_PANE2) : C(TK_WELL), P(2.0f));
      d->AddRect(a, ImVec2(a.x + s, a.y + s), bHover ? C(TK_BTN_RIM_HI) : C(TK_LINE), P(2.0f));
      const std::string sn = std::to_string(n);
      T(d, STUDIO_FONT_MONO, kStudioFsXs, a.x + (s - TW(STUDIO_FONT_MONO, kStudioFsXs, sn)) / 2.0f, a.y + P(3.0f),
        bOn ? C(TK_BLACK) : C(TK_INK3), sn);
      if (bClick && !bOn)
        Ask("studio_card_move", "\"unit\":" + Q(c.key) + ",\"index\":" + std::to_string(b.nMod) + ",\"position\":" + std::to_string(n));
    }
    ImGui::SetCursorScreenPos(ImVec2(q.x + 3 * (s + g) + P(10.0f), q.y));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "or drag it on the card; a button of the mod's");
    ImGui::SetCursorScreenPos(ImVec2(q.x + 3 * (s + g) + P(10.0f), q.y + P(18.0f)));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "there changes places with it");
    ImGui::SetCursorScreenPos(ImVec2(q.x - P(lw), q.y + 3 * (s + g)));
    ImGui::Dummy(ImVec2(1, P(4.0f)));
  }
  // the text while what it needs is missing
  Row("When it cannot", lw);
  if (TextField("##disabled", id + "dis", b.disabled, std::min(fw, 300.0f), 48.0f, &out, true))
    SetFields(c.key, b.nMod, "\"disabled\":" + (out.empty() ? std::string("null") : Q(out)));
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + P(lw), ImGui::GetCursorScreenPos().y));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "greyed out with this text; empty: the button shows only once it can");
  // the icon: its target's own, or a command icon by number
  Gap(4.0f);
  Row("Icon", lw);
  bool bOwnIcon = b.nIcon >= 0;
  if (StudioCheck("##ownicon", "a command icon of its own", &bOwnIcon))
    SetFields(c.key, b.nMod, std::string("\"icon\":") + (bOwnIcon ? "0" : "null"));
  if (bOwnIcon) {
    ImGui::SameLine(0.0f, P(8.0f));
    double dIcon = b.nIcon;
    if (StudioNumber("##icon", &dIcon, 70.0f, 26.0f, true, 13.0f) && dIcon >= 0)
      SetFields(c.key, b.nMod, "\"icon\":" + Fmt(floor(dIcon + 0.5)));
  } else {
    ImGui::SameLine(0.0f, P(8.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), b.what == "train" ? "the unit's own, or its looks'" : "its tech's or upgrade's");
  }
  // what it needs: its target's requirements
  if (!b.target.empty() && (b.what != "cast" || !StudioRefName(ST_TECHS, StudioNumber(ST_ORDERS, b.target, "tech", -1)).empty())) {
    Gap(8.0f);
    const int t = b.what == "cast" ? ST_TECHS : b.t;
    const std::string rec = b.what == "cast" ? StudioRefName(ST_TECHS, StudioNumber(ST_ORDERS, b.target, "tech", -1)) : b.target;
    const char *pszKey = b.what == "cast" ? "use_requires" : "requires";
    Row(b.what == "cast" ? "Needs to cast" : "Needs", lw);
    ImGui::BeginGroup();
    StudioRequirements(t, rec, pszKey, Mine(t, rec), std::max(160.0f, fW / StudioPx(1.0f) - lw));
    ImGui::EndGroup();
  }
  Gap(12.0f);
  if (StudioButton("##remove", "Take the button off", "x", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs)) {
    Ask("studio_card_set", "\"unit\":" + Q(c.key) + ",\"index\":" + std::to_string(b.nMod) + ",\"remove\":true");
    g_nSel = -1;
  }
  ImGui::PopID();
}

// The card's clashes in words, each with its fix.
void ClashList(const Card &c, float fW) {
  const Clashes cl = ClashesOf(c);
  if (!cl.Any())
    return;
  for (const auto &k : cl.keys) {
    std::string s = "! " + KeyCap(k.first) + ": ";
    int nFix = -1;
    for (size_t n = 0; n < k.second.size(); ++n) {
      const Btn &b = c.btns[static_cast<size_t>(k.second[n])];
      s += (n ? " and " : "") + Plain(b.text);
      if (b.nMod >= 0)
        nFix = k.second[n];
    }
    s += " share a hotkey; the first dealt wins.";
    ImGui::PushID(k.first);
    Words(s, fW, C(TK_BAD));
    if (nFix >= 0) {
      const Btn &b = c.btns[static_cast<size_t>(nFix)];
      const char free = FreeKey(c, b.text, nFix);
      if (StudioButton("##fix", ("Give " + Plain(b.text) + " " + KeyCap(free)).c_str(), nullptr, SB_GOLD, 20.0f, 0.0f, kStudioFsXs))
        SetFields(c.key, b.nMod, "\"hotkey\":" + Q(std::string(1, free)) + ",\"text\":" + Q(HotText(b.text, free)));
    }
    ImGui::PopID();
  }
  for (const auto &p : cl.frees) {
    std::string s = "Place " + std::to_string(p.first) + ": ";
    std::string races;
    for (size_t n = 0; n < p.second.size(); ++n) {
      const Btn &b = c.btns[static_cast<size_t>(p.second[n])];
      s += (n ? " then " : "") + Plain(b.text);
      if (b.bClassic)
        races = Joined(RacesWithout(b.target)) + ", without the " + StudioShowName(b.t, b.target);
    }
    Words(s + "; the mod's shows there for " + races + ".", fW, C(TK_INK2));
  }
  for (const auto &p : cl.places) {
    std::string s = "! Place " + std::to_string(p.first) + ": ";
    for (size_t n = 0; n < p.second.size(); ++n)
      s += (n ? " and " : "") + Plain(c.btns[static_cast<size_t>(p.second[n])].text);
    s += "; when both show, the later goes to the next place.";
    ImGui::PushID(p.first + 1000);
    Words(s, fW, C(TK_WARN));
    int nFix = -1;
    for (int n : p.second)
      if (c.btns[static_cast<size_t>(n)].nMod >= 0)
        nFix = n;
    const int nFree = FreePlace(c);
    if (nFix >= 0 && nFree > 0) {
      if (StudioButton("##fixp", ("Move it to " + std::to_string(nFree)).c_str(), nullptr, SB_GOLD, 20.0f, 0.0f, kStudioFsXs))
        Ask("studio_card_move", "\"unit\":" + Q(c.key) + ",\"index\":" + std::to_string(c.btns[static_cast<size_t>(nFix)].nMod) +
                                    ",\"position\":" + std::to_string(nFree));
    }
    ImGui::PopID();
  }
  Gap(8.0f);
}

// A card with its editing: the grid, its legend and clashes, every button as a row, and the chosen
// button's pane beside or below. bEdit when the mod may add to it.
void CardEditor(const Card &c, float fW, bool bEdit) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  if (g_selKey != c.key)
    g_nSel = -1;
  if (g_nSel >= static_cast<int>(c.btns.size()))
    g_nSel = -1;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  GridOpts go;
  go.bEdit = bEdit;
  go.cell = fW > P(900.0f) ? 92.0f : 80.0f;
  const GridHit hit = Grid("##grid", c, go);
  const float gw = ImGui::GetItemRectSize().x;
  if (hit.nClick >= 0) {
    g_selKey = c.key;
    g_nSel = hit.nClick;
  }
  if (hit.nEmpty > 0) {
    if (g_selKey == c.key && g_nSel >= 0 && c.btns[static_cast<size_t>(g_nSel)].nMod >= 0) {
      Ask("studio_card_move", "\"unit\":" + Q(c.key) + ",\"index\":" + std::to_string(c.btns[static_cast<size_t>(g_nSel)].nMod) +
                                  ",\"position\":" + std::to_string(hit.nEmpty));
    } else {
      g_nAddPos = hit.nEmpty;
      g_addKey = c.key;
      ImGui::OpenPopup("##addbutton");
    }
  }
  if (hit.nMoveMod >= 0 && hit.nMoveTo > 0)
    Ask("studio_card_move", "\"unit\":" + Q(c.key) + ",\"index\":" + std::to_string(hit.nMoveMod) + ",\"position\":" + std::to_string(hit.nMoveTo));
  float y = o.y + gw + P(10.0f);
  // the legend
  {
    float x = o.x;
    d->AddRect(ImVec2(x, y + P(3.0f)), ImVec2(x + P(12.0f), y + P(15.0f)), C(TK_LINE2), P(2.0f));
    x += P(16.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y + P(2.0f), C(TK_INK2), "classic") + P(14.0f);
    d->AddRect(ImVec2(x, y + P(3.0f)), ImVec2(x + P(12.0f), y + P(15.0f)), C(TK_MOD), P(2.0f), 0, P(2.0f));
    x += P(16.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y + P(2.0f), C(TK_INK2), "the mod's") + P(14.0f);
    d->AddCircleFilled(ImVec2(x + P(6.0f), y + P(9.0f)), P(6.0f), C(TK_BAD));
    x += P(16.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y + P(2.0f), C(TK_INK2), "clash");
  }
  y += P(24.0f);
  // every button, alternates too: the grid shows a place's first
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::BeginGroup();
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), "Its buttons, in order", 0.1f, true);
  for (size_t n = 0; n < c.btns.size(); ++n) {
    const Btn &b = c.btns[n];
    ImGui::PushID(static_cast<int>(n) + 5000);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float rh = P(24.0f);
    const bool bSel = g_selKey == c.key && g_nSel == static_cast<int>(n);
    if (ImGui::InvisibleButton("##row", ImVec2(gw, rh))) {
      g_selKey = c.key;
      g_nSel = static_cast<int>(n);
    }
    const bool bHover = ImGui::IsItemHovered();
    if (bSel)
      d->AddRectFilled(p, ImVec2(p.x + gw, p.y + rh), C(TK_SEL));
    else if (bHover)
      d->AddRectFilled(p, ImVec2(p.x + gw, p.y + rh), C(TK_ROW));
    T(d, STUDIO_FONT_MONO, kStudioFsXs, p.x + P(4.0f), Mid(p.y, rh, kStudioFsXs), C(TK_INK3), std::to_string(b.nPos));
    StudioKbd(d, ImVec2(p.x + P(18.0f), p.y + P(4.0f)), KeyCap(b.cKey).c_str(), 16.0f);
    T(d, b.bClassic ? STUDIO_FONT_TEXT : STUDIO_FONT_BOLD, kStudioFsXs, p.x + P(42.0f), Mid(p.y, rh, kStudioFsXs),
      b.bAlt ? C(TK_INK3) : b.bClassic ? C(TK_INK2) : C(TK_INK), Plain(b.text) + (b.bAlt ? "  (when the one before is not shown)" : ""),
      gw - P(46.0f));
    if (!b.bClassic)
      d->AddRectFilled(ImVec2(p.x, p.y + P(4.0f)), ImVec2(p.x + P(2.0f), p.y + rh - P(4.0f)), C(TK_MOD));
    ImGui::PopID();
  }
  if (c.btns.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "no buttons");
  ImGui::EndGroup();
  const float leftBottom = ImGui::GetItemRectMax().y;
  // the pane: beside the grid when there is room, else below
  const bool bSide = fW - gw > P(380.0f);
  const float px = bSide ? o.x + gw + P(24.0f) : o.x;
  const float pw = bSide ? fW - gw - P(24.0f) : fW;
  ImGui::SetCursorScreenPos(ImVec2(px, bSide ? o.y : leftBottom + P(16.0f)));
  ImGui::BeginGroup();
  ClashList(c, pw);
  if (g_selKey == c.key && g_nSel >= 0) {
    ButtonPane(c, g_nSel, pw);
  } else {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3),
                bEdit ? "Pick a button to see it; the mod's change here. An empty place takes a new one." : "Pick a button to see it.");
    if (bEdit) {
      const int nFree = FreePlace(c);
      Gap(6.0f);
      if (nFree > 0 && StudioButton("##add", ("Add a button at place " + std::to_string(nFree)).c_str(), "plus", SB_PLAIN, 26.0f)) {
        g_nAddPos = nFree;
        g_addKey = c.key;
        ImGui::OpenPopup("##addbutton");
      }
    }
  }
  ImGui::EndGroup();
  if (g_addKey == c.key)
    AddPopup(c);
  const float rightBottom = ImGui::GetItemRectMax().y;
  ImGui::SetCursorScreenPos(ImVec2(o.x, std::max(leftBottom, rightBottom) + P(8.0f)));
  ImGui::Dummy(ImVec2(fW, 1));
}

// What the card is, in a line.
std::string CardAbout(const Card &c) {
  if (c.bMenu)
    return "a worker's build menu: the classic buildings, and the mod's put among them by place; the "
           + Short(ST_UNITS, MenuWorker(c.nClassic)) + " places the mod's as its race places its own";
  if (!c.bOk)
    return "no unit by that name";
  const char *pszFrom = c.nClassic >= 0 && c.nClassic < StudioClassicRows(ST_UNITS) ? StudioClassicName(ST_UNITS, c.nClassic) : nullptr;
  const std::string from = pszFrom ? StudioShowName(ST_UNITS, pszFrom) : std::string("no classic unit");
  if (!c.bModUnit)
    return "the classic card" + std::string(HasModButtons(c.key) ? " with the mod's buttons put among its own" : "") +
           "; the mod adds buttons here and does not move the classic ones";
  if (c.bOwnCard)
    return "a card of its own: the " + from + "'s, which its looks are, with the mod's buttons";
  return "the " + from + "'s card, shared with it; a button the mod gives it makes it a card of its own";
}

// ---- the Cards page ---------------------------------------------------------------------------------

struct ListRow {
  std::string unit;
  const char *pszHead = nullptr;
};

void CardsList(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = b.x - a.x;
  ImGui::SetCursorScreenPos(a);
  StudioInput("##findunit", g_szFind, sizeof g_szFind, W / StudioPx(1.0f), 28.0f, "Find a unit", "search");
  ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + P(34.0f)));
  static const char *const kRaces[] = {"All", "Protoss", "Terran", "Zerg"};
  g_nRaceList = StudioSeg("##racelist", kRaces, 4, g_nRaceList, 24.0f, kStudioFsXs);
  const float top = a.y + P(66.0f);
  // the rows: the mod's units, the classic cards it adds to, then the classic units
  static std::vector<ListRow> s_rows;
  static unsigned s_nChange = ~0u;
  static std::string s_find;
  static int s_nRace = -1;
  if (s_nChange != StudioChange() || s_find != g_szFind || s_nRace != g_nRaceList) {
    s_nChange = StudioChange();
    s_find = g_szFind;
    s_nRace = g_nRaceList;
    s_rows.clear();
    static const int kRaceOf[] = {-1, 2, 1, 0};
    auto ok = [&](const std::string &u) {
      if (g_nRaceList > 0 && UnitRace(u) != kRaceOf[g_nRaceList])
        return false;
      return g_szFind[0] == '\0' || Has(StudioShowName(ST_UNITS, u), g_szFind) || Has(u, g_szFind);
    };
    std::vector<std::string> mods, added, classic;
    for (const StudioRecord &r : StudioRecords(ST_UNITS))
      if ((r.kind == SK_OWN || r.kind == SK_DEPENDENCY) && ok(r.name))
        mods.push_back(r.name);
    std::set<std::string> addedSet;
    for (const PortJsonMember &m : StudioDoc("types")["cards"].members)
      if (StudioClassicRow(ST_UNITS, m.first) >= 0 && ok(m.first) && !addedSet.count(m.first)) {
        added.push_back(m.first);
        addedSet.insert(m.first);
      }
    for (int n = 0; n < StudioClassicRows(ST_UNITS); ++n)
      if (const char *psz = StudioClassicName(ST_UNITS, n))
        if (ok(psz) && !addedSet.count(psz) && !ClassicButtons(n).empty())
          classic.push_back(psz);
    s_rows.push_back({std::string(), "The mod's units"});
    for (const std::string &u : mods)
      s_rows.push_back({u, nullptr});
    if (!added.empty()) {
      s_rows.push_back({std::string(), "Classic cards the mod adds to"});
      for (const std::string &u : added)
        s_rows.push_back({u, nullptr});
    }
    // the workers' build menus, Protoss, Terran and Zerg as the list's races go
    std::vector<std::string> menus;
    for (int n : {240, 243, 239, 242, 238, 241}) {
      const std::string k = MenuKey(n);
      if ((g_nRaceList == 0 || CardRace(k) == kRaceOf[g_nRaceList]) &&
          (g_szFind[0] == '\0' || Has(CardName(k), g_szFind)))
        menus.push_back(k);
    }
    if (!menus.empty()) {
      s_rows.push_back({std::string(), "Workers' build menus"});
      for (const std::string &u : menus)
        s_rows.push_back({u, nullptr});
    }
    s_rows.push_back({std::string(), "Classic"});
    for (const std::string &u : classic)
      s_rows.push_back({u, nullptr});
  }
  ImGui::SetCursorScreenPos(ImVec2(a.x, top));
  if (ImGui::BeginChild("##cardlist", ImVec2(W, b.y - top), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImDrawList *ld = ImGui::GetWindowDrawList();
    const float rh = P(28.0f);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(s_rows.size()), rh);
    while (clip.Step())
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const ListRow &r = s_rows[static_cast<size_t>(i)];
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(i);
        if (r.pszHead) {
          Wide(ld, 9.0f, p.x + P(4.0f), p.y + P(10.0f), C(TK_INK3), r.pszHead, 0.08f);
          ImGui::Dummy(ImVec2(w, rh));
          ImGui::PopID();
          continue;
        }
        const bool bSel = r.unit == g_cardUnit;
        if (ImGui::InvisibleButton("##u", ImVec2(w, rh)))
          g_cardUnit = r.unit;
        const bool bHover = ImGui::IsItemHovered();
        if (bSel)
          ld->AddRectFilled(p, ImVec2(p.x + w, p.y + rh), C(TK_SEL));
        else if (bHover)
          ld->AddRectFilled(p, ImVec2(p.x + w, p.y + rh), C(TK_ROW));
        const Card &c = CardOf(r.unit);
        const bool bMod = c.bModUnit || HasModButtons(c.key);
        if (bMod)
          ld->AddRectFilled(ImVec2(p.x, p.y + P(5.0f)), ImVec2(p.x + P(3.0f), p.y + rh - P(5.0f)), C(TK_MOD), P(1.5f));
        T(ld, bMod ? STUDIO_FONT_BOLD : STUDIO_FONT_TEXT, kStudioFsS, p.x + P(10.0f), Mid(p.y, rh, kStudioFsS),
          bSel || bHover ? C(TK_HI) : C(TK_INK), CardName(r.unit), w - P(40.0f));
        if (bMod && ClashesOf(c).Bad())
          ld->AddCircleFilled(ImVec2(p.x + w - P(12.0f), p.y + rh / 2.0f), P(4.0f), C(TK_BAD));
        ImGui::PopID();
      }
  }
  ImGui::EndChild();
  (void)d;
}

}  // namespace

// ---- the page and the views the shell asks for ------------------------------------------------------

namespace studio_races {

void CardsShow(const std::string &unit) {
  g_cardUnit = unit;
}

void CardsPage(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float lw = P(270.0f);
  CardsList(a, ImVec2(a.x + lw, b.y));
  d->AddLine(ImVec2(a.x + lw + P(12.0f), a.y), ImVec2(a.x + lw + P(12.0f), b.y), C(TK_LINE));
  const ImVec2 ra(a.x + lw + P(24.0f), a.y);
  ImGui::SetCursorScreenPos(ra);
  if (ImGui::BeginChild("##card", ImVec2(b.x - ra.x, b.y - ra.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImDrawList *cd = ImGui::GetWindowDrawList();
    const float W = ImGui::GetContentRegionAvail().x - P(8.0f);
    const Card &c = CardOf(g_cardUnit);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x + T(cd, STUDIO_FONT_BOLD, 22.0f, p.x, p.y, C(TK_HI), CardName(g_cardUnit), W - P(260.0f)) + P(12.0f);
    const int r = CardRace(g_cardUnit);
    if (r >= 0)
      x += StudioChip(cd, ImVec2(x, p.y + P(6.0f)), RaceWord(r).c_str()) + P(6.0f);
    if (c.bModUnit)
      StudioChip(cd, ImVec2(x, p.y + P(6.0f)), "the mod's unit", CH_GOOD);
    ImGui::SetCursorScreenPos(ImVec2(p.x + W - P(150.0f), p.y));
    if (c.bMenu) {
      if (StudioButton("##openunit", ("Open the " + Short(ST_UNITS, MenuWorker(c.nClassic))).c_str(), "unit", SB_PLAIN, 26.0f, 150.0f,
                       kStudioFsXs))
        StudioOpenDoc(ST_UNITS, MenuWorker(c.nClassic), 6);
    } else if (c.bOk && StudioButton("##openunit", "Open the unit", "unit", SB_PLAIN, 26.0f, 150.0f, kStudioFsXs)) {
      StudioOpenDoc(ST_UNITS, g_cardUnit, 6);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(34.0f)));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), CardAbout(c).c_str());
    Gap(10.0f);
    if (c.bOk) {
      // a mod's unit's card is the mod's to add to; a classic unit's too, while it is a unit of the game
      const bool bEdit = StudioIsOpen() && (c.bMenu || (c.bModUnit ? IsMod(ST_UNITS, g_cardUnit) : c.nClassic >= 0));
      CardEditor(c, W, bEdit);
    }
  }
  ImGui::EndChild();
}

}  // namespace studio_races

// A unit's Card view: its card with the editor, where it is made, what it needs and its race.
void StudioUnitCard(const std::string &unit) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = ImGui::GetContentRegionAvail().x - StudioPx(44.0f);
  const Card &c = CardOf(unit);
  Wide(d, 10.5f, ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y, C(TK_HEAD), "Its card");
  Gap(20.0f);
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), CardAbout(c).c_str());
  Gap(8.0f);
  if (!c.bOk)
    return;
  CardEditor(c, W, StudioIsOpen() && (c.bModUnit ? IsMod(ST_UNITS, unit) : c.nClassic >= 0));
  Gap(16.0f);
  // where it is made: each card with a button that makes it, the button marked
  const ImVec2 o = ImGui::GetCursorScreenPos();
  d->AddLine(ImVec2(o.x, o.y - P(8.0f)), ImVec2(o.x + W, o.y - P(8.0f)), C(TK_LINE));
  Wide(d, 10.5f, o.x, o.y, C(TK_HEAD), "Where it is made");
  Gap(22.0f);
  const std::vector<Where> at = WhereOn("make", unit);
  if (at.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_WARN), "No card has a button that makes it yet, so no player can have it.");
  float x = o.x, rowY = ImGui::GetCursorScreenPos().y;
  float yMax = rowY;
  int n = 0;
  for (const Where &w : at) {
    if (n++ >= 6)
      break;
    const std::string on = w.unit.empty() ? MenuName(w.nCard) : CardName(w.unit);
    if (x + P(220.0f) > o.x + W) {
      x = o.x;
      rowY = yMax + P(12.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, rowY));
    ImGui::BeginGroup();
    ImGui::PushID(n);
    if (!w.unit.empty()) {
      if (StudioButton("##on", on.c_str(), "cards", SB_FLAT, 22.0f, 0.0f, kStudioFsS)) {
        CardsShow(w.unit);
        StudioWorkspaceSet(5);
      }
    } else {
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), on.c_str());
    }
    Card shown;
    if (!w.unit.empty()) {
      shown = CardOf(w.unit);
    } else {
      shown.bOk = true;
      shown.key = MenuKey(w.nCard);
      shown.btns = ClassicButtons(w.nCard);
    }
    GridOpts go;
    go.cell = 52.0f;
    go.bText = false;
    go.mark = unit;
    go.markWhat = "make";
    Grid("##made", shown, go);
    for (const Btn &b : shown.btns)
      if (MarkMatch(b, go)) {
        StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK2),
                    ("place " + std::to_string(b.nPos) + ", hotkey " + KeyCap(b.cKey) + (b.bClassic ? ", classic" : ", the mod's")).c_str());
        break;
      }
    ImGui::PopID();
    ImGui::EndGroup();
    x += std::max(ImGui::GetItemRectSize().x, P(200.0f)) + P(20.0f);
    yMax = std::max(yMax, ImGui::GetItemRectMax().y);
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, yMax + P(10.0f)));
  // a button that makes it, in a click, on the card it is made at (or one picked)
  if (c.bModUnit && IsMod(ST_UNITS, unit)) {
    std::string atUnit;
    const PortJson &req = StudioRecordJson(ST_UNITS, unit)["requires"];
    for (size_t i = 0; i < req.size(); ++i)
      if (req.At(i)["at"].kind != PortJson::NONE)
        atUnit = NameIn(ST_UNITS, req.At(i)["at"]);
    static std::string s_on;
    std::string onCard = s_on.empty() ? atUnit : s_on;
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Add a button that makes it on the card of");
    ImGui::SameLine(0.0f, P(8.0f));
    std::string picked;
    if (Pick("##trainat", ST_UNITS, onCard, 220.0f, &picked, PICK_ANY, 26.0f, "a building...", UnitRace(unit), nullptr))
      s_on = picked;
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##train", "Add the button", "plus", SB_GOOD, 26.0f, 0.0f, kStudioFsXs, !onCard.empty()))
      Ask("studio_card_train", "\"unit\":" + Q(unit) + ",\"card\":" + Q(onCard));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3),
                "It goes at the card's first free place with a free hotkey, and \"made at\" goes first in what it needs.");
    // a building: on its race's worker's build menu
    if (StudioNumber(ST_UNITS, unit, "flags") & 1) {
      Gap(8.0f);
      static int s_nRace = -1, s_nMenu = 1;
      static std::string s_for;
      if (s_for != unit) {
        s_for = unit;
        s_nRace = std::max(0, UnitRace(unit));
      }
      static const char *const kWorkers[] = {"Drone", "SCV", "Probe"};
      static const char *const kMenus[] = {"basic", "advanced"};
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Add it to the build menu of");
      ImGui::SameLine(0.0f, P(8.0f));
      s_nRace = StudioSeg("##worker", kWorkers, 3, s_nRace, 26.0f, kStudioFsXs);
      ImGui::SameLine(0.0f, P(8.0f));
      s_nMenu = StudioSeg("##menu", kMenus, 2, s_nMenu, 26.0f, kStudioFsXs);
      ImGui::SameLine(0.0f, P(8.0f));
      const std::string menu = MenuKey(238 + s_nRace + s_nMenu * 3);
      if (StudioButton("##build", "Add the button", "plus", SB_GOOD, 26.0f, 0.0f, kStudioFsXs))
        Ask("studio_card_build", "\"unit\":" + Q(unit) + ",\"menu\":" + Q(menu));
      ImGui::SameLine(0.0f, P(8.0f));
      if (StudioButton("##showmenu", "", "cards", SB_FLAT, 26.0f, 26.0f)) {
        CardsShow(menu);
        StudioWorkspaceSet(5);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show the %s", CardName(menu).c_str());
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3),
                  "The worker places it as its race places its own, at the menu's first free place, and goes first in what it needs.");
    }
  }
  Gap(14.0f);
  // what it needs
  const ImVec2 q = ImGui::GetCursorScreenPos();
  d->AddLine(ImVec2(q.x, q.y - P(6.0f)), ImVec2(q.x + W, q.y - P(6.0f)), C(TK_LINE));
  Wide(d, 10.5f, q.x, q.y, C(TK_HEAD), "What it needs");
  Gap(22.0f);
  StudioRequirements(ST_UNITS, unit, "requires", Mine(ST_UNITS, unit), std::min(W / StudioPx(1.0f), 640.0f));
  Gap(14.0f);
  // its race: the mod's races that name it, or that go without it
  const PortJson &races = StudioDoc("types")["races"];
  if (races.size() > 0) {
    const ImVec2 rq = ImGui::GetCursorScreenPos();
    d->AddLine(ImVec2(rq.x, rq.y - P(6.0f)), ImVec2(rq.x + W, rq.y - P(6.0f)), C(TK_LINE));
    Wide(d, 10.5f, rq.x, rq.y, C(TK_HEAD), "Its races");
    Gap(22.0f);
    for (const PortJsonMember &m : races.members) {
      const std::string race = m.second["name"].Str(m.first.c_str());
      bool bIn = false, bWithout = false;
      for (size_t i = 0; i < m.second["units"].size(); ++i)
        bIn = bIn || NameIn(ST_UNITS, m.second["units"].At(i)) == unit;
      for (size_t i = 0; i < m.second["without"].size(); ++i)
        bWithout = bWithout || NameIn(ST_UNITS, m.second["without"].At(i)) == unit;
      ImGui::PushID(m.first.c_str());
      std::string s;
      if (c.bModUnit)
        s = bIn ? "In the " + race + " race's units: only its players train it." : "Not in the " + race + " race's units.";
      else
        s = bWithout ? "The " + race + " race goes without it." : "The " + race + " race has it.";
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, bIn || (!c.bModUnit && !bWithout) ? C(TK_INK) : C(TK_INK2), s.c_str());
      ImGui::SameLine(0.0f, P(10.0f));
      const char *pszField = c.bModUnit ? "units" : "without";
      const bool bOn = c.bModUnit ? bIn : bWithout;
      const char *pszDo = c.bModUnit ? (bIn ? "Take it out" : "Add it") : (bWithout ? "Give it back" : "Go without it");
      if (StudioButton("##race", pszDo, nullptr, bOn ? SB_PLAIN : SB_GOLD, 22.0f, 0.0f, kStudioFsXs, c.bModUnit ? IsMod(ST_UNITS, unit) : true)) {
        PortJson list = m.second[pszField];
        if (list.kind != PortJson::ARRAY) {
          list = PortJson();
          list.kind = PortJson::ARRAY;
        }
        if (bOn) {
          for (size_t i = list.items.size(); i-- > 0;)
            if (NameIn(ST_UNITS, list.items[i]) == unit)
              list.items.erase(list.items.begin() + static_cast<long long>(i));
        } else {
          list.items.push_back(StudioJsonString(c.key));
        }
        Ask("studio_race_set", "\"race\":" + Q(m.first) + ",\"field\":" + Q(pszField) + ",\"value\":" +
                                   (list.items.empty() ? std::string("null") : PortJsonWrite(list)));
      }
      ImGui::SameLine(0.0f, P(6.0f));
      if (StudioButton("##openrace", "", "race", SB_FLAT, 22.0f, 24.0f)) {
        ShowRace(m.first);
        StudioWorkspaceSet(5);
      }
      ImGui::PopID();
    }
  }
}

// A tech's or an upgrade's head: what it is, where it is researched, its button, what uses it.
void StudioTechHead(int t, const std::string &name) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioRecord rec;
  if ((t != ST_TECHS && t != ST_UPGRADES) || !StudioRecordOf(t, name, &rec))
    return;
  const bool bTech = t == ST_TECHS;
  const float w = ImGui::GetContentRegionAvail().x;
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(16.0f);
  const float W = w - P(44.0f);
  float y = o.y;
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, y, C(TK_HI), StudioShowName(t, name), W);
  y += P(36.0f);
  float cx = o.x;
  cx += StudioChip(d, ImVec2(cx, y), bTech ? "tech" : "upgrade") + P(6.0f);
  if (rec.kind == SK_CLASSIC)
    cx += StudioChip(d, ImVec2(cx, y), "classic, read only", CH_GOLD) + P(6.0f);
  else if (rec.kind == SK_EDIT)
    cx += StudioChip(d, ImVec2(cx, y), "an edit of the classic row", CH_CHANGED) + P(6.0f);
  else if (!rec.base.empty())
    cx += StudioChip(d, ImVec2(cx, y), ("based on " + StudioShowName(t, rec.base)).c_str()) + P(6.0f);
  {
    const long long nRace = StudioNumber(t, name, "race", -1);
    for (const auto &e : StudioEnum("race"))
      if (e.first == nRace)
        StudioChip(d, ImVec2(cx, y), e.second.c_str());
  }
  y += P(30.0f);
  // the cost in a sentence
  std::string s;
  const long long nMin = StudioNumber(t, name, "minerals"), nGas = StudioNumber(t, name, "gas");
  const long long nTime = StudioNumber(t, name, bTech ? "research_time" : "time");
  const bool bResearched = rec.kind == SK_CLASSIC || rec.kind == SK_EDIT || StudioRecordJson(t, name)["requires"].kind != PortJson::NONE;
  if (bTech) {
    s = bResearched ? "Researched for " + std::to_string(nMin) + " minerals and " + std::to_string(nGas) + " gas, " +
                          Fmt(nTime / 24.0, 1) + " s."
                    : "Not researched: every player has it from the start (it has no Needs to research).";
    const long long nEnergy = StudioNumber(t, name, "energy");
    if (nEnergy > 0)
      s += " Costs " + std::to_string(nEnergy) + " energy to cast.";
  } else {
    const long long nMax = StudioNumber(t, name, "max_level", 1);
    const long long fMin = StudioNumber(t, name, "minerals_factor"), fGas = StudioNumber(t, name, "gas_factor"),
                    fTime = StudioNumber(t, name, "time_factor");
    s = std::to_string(nMax) + (nMax == 1 ? " level: " : " levels: ");
    for (long long l = 0; l < nMax && l < 6; ++l)
      s += (l ? "; " : "") + std::string("level ") + std::to_string(l + 1) + ", " + std::to_string(nMin + fMin * l) + " minerals, " +
           std::to_string(nGas + fGas * l) + " gas, " + Fmt((nTime + fTime * l) / 24.0, 0) + " s";
    s += ".";
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::PushTextWrapPos(o.x + W - ImGui::GetWindowPos().x);
  StudioPushFont(STUDIO_FONT_TEXT, 15.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK));
  ImGui::TextWrapped("%s", s.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
  y = ImGui::GetCursorScreenPos().y + P(14.0f);
  // where it is researched, each card with its button marked
  Wide(d, 9.5f, o.x, y, C(TK_INK3), bTech ? "Researched at" : "Upgraded at", 0.1f);
  y += P(20.0f);
  const char *pszWhat = bTech ? "research" : "upgrade";
  const std::vector<Where> at = WhereOn(pszWhat, name);
  float x = o.x, yMax = y;
  int n = 0;
  for (const Where &wh : at) {
    if (n++ >= 6)
      break;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::BeginGroup();
    ImGui::PushID(n);
    const std::string on = wh.unit.empty() ? MenuName(wh.nCard) : CardName(wh.unit);
    if (StudioButton("##at", on.c_str(), "cards", SB_FLAT, 22.0f, 0.0f, kStudioFsS) && !wh.unit.empty()) {
      CardsShow(wh.unit);
      StudioWorkspaceSet(5);
    }
    const Card &c = CardOf(wh.unit);
    GridOpts go;
    go.cell = 52.0f;
    go.bText = false;
    go.mark = name;
    go.markWhat = pszWhat;
    Grid("##at", c, go);
    for (const Btn &b : c.btns)
      if (MarkMatch(b, go)) {
        StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK2), (Plain(b.text) + ": place " + std::to_string(b.nPos) + ", hotkey " + KeyCap(b.cKey)).c_str());
        break;
      }
    ImGui::PopID();
    ImGui::EndGroup();
    x += std::max(ImGui::GetItemRectSize().x, P(200.0f)) + P(20.0f);
    yMax = std::max(yMax, ImGui::GetItemRectMax().y);
    if (x + P(220.0f) > o.x + W) {
      x = o.x;
      y = yMax + P(12.0f);
    }
  }
  if (at.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, y, C(TK_INK3),
      bResearched ? "No card has its button yet." : "Nowhere: give it Needs to research, with where it is made, and a button.");
    yMax = y + P(20.0f);
  }
  y = yMax + P(10.0f);
  // a button for it on the building its Needs name, in a click
  if (IsMod(t, name) && at.empty() && (!bTech || bResearched)) {
    std::string atUnit;
    const PortJson &req = StudioRecordJson(t, name)["requires"];
    for (size_t i = 0; i < req.size(); ++i)
      if (req.At(i)["at"].kind != PortJson::NONE)
        atUnit = NameIn(ST_UNITS, req.At(i)["at"]);
    if (!atUnit.empty()) {
      ImGui::SetCursorScreenPos(ImVec2(o.x, y));
      const Card &c = CardOf(atUnit);
      const int nFree = FreePlace(c);
      if (StudioButton("##addbtn", ("Add its button to the " + StudioShowName(ST_UNITS, atUnit) + "'s card").c_str(), "plus", SB_GOOD, 26.0f,
                       0.0f, kStudioFsXs, nFree > 0))
        Ask("studio_card_set", "\"unit\":" + Q(c.key) + ",\"button\":" + NewButton(c, nFree, bTech ? 2 : 1, name));
      y += P(34.0f);
    }
  }
  // what uses it
  Wide(d, 9.5f, o.x, y, C(TK_INK3), bTech ? "Cast by" : "Raises", 0.1f);
  float ux = o.x + P(90.0f);
  float uy = y - P(5.0f);
  std::vector<std::pair<int, std::string>> users;
  if (bTech) {
    for (const Where &wh : WhereOn("cast", name))
      if (!wh.unit.empty())
        users.push_back({ST_UNITS, wh.unit});
    // a mod's actions whose tech is this, and the units whose cards cast them
    for (const StudioRecord &r : StudioRecords(ST_ORDERS))
      if (r.kind != SK_EDIT && StudioRefName(ST_TECHS, StudioNumber(ST_ORDERS, r.name, "tech", -1)) == name) {
        users.push_back({ST_ORDERS, r.name});
        for (const Where &wh : WhereOn("cast", r.name))
          users.push_back({ST_UNITS, wh.unit});
      }
  } else {
    StudioRecord u;
    if (StudioRecordOf(t, name, &u)) {
      auto check = [&](int tt, const std::string &rn, const char *pszKey) {
        if (StudioNumber(tt, rn, pszKey, -1) == u.nId)
          users.push_back({tt, rn});
      };
      for (const StudioRecord &r : StudioRecords(ST_WEAPONS))
        if (r.kind != SK_EDIT)
          check(ST_WEAPONS, r.name, "damage_upgrade");
      for (int k = 0; k < StudioClassicRows(ST_WEAPONS); ++k)
        if (const char *psz = StudioClassicName(ST_WEAPONS, k))
          check(ST_WEAPONS, psz, "damage_upgrade");
      for (const StudioRecord &r : StudioRecords(ST_UNITS))
        if (r.kind != SK_EDIT)
          check(ST_UNITS, r.name, "armor_upgrade");
      for (int k = 0; k < StudioClassicRows(ST_UNITS); ++k)
        if (const char *psz = StudioClassicName(ST_UNITS, k))
          check(ST_UNITS, psz, "armor_upgrade");
    }
  }
  std::set<std::string> shown;
  int nShown = 0;
  for (const auto &u : users) {
    if (u.second.empty() || shown.count(std::to_string(u.first) + u.second))
      continue;
    shown.insert(std::to_string(u.first) + u.second);
    const std::string label = StudioShowName(u.first, u.second) + (u.first == ST_WEAPONS ? ", its damage" : u.first == ST_UNITS && !bTech ? ", its armour" : "");
    const char *pszGlyph = u.first == ST_WEAPONS ? "weapon" : u.first == ST_ORDERS ? "action" : "unit";
    const float bw = StudioButtonSize(label.c_str(), pszGlyph, 24.0f, kStudioFsXs).x;
    if (ux + bw > o.x + W) {
      ux = o.x + P(90.0f);
      uy += P(30.0f);
    }
    if (++nShown > 14) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, ux, uy + P(5.0f), C(TK_INK3), "and more");
      break;
    }
    ImGui::SetCursorScreenPos(ImVec2(ux, uy));
    ImGui::PushID((std::to_string(u.first) + u.second).c_str());
    if (StudioButton("##user", label.c_str(), pszGlyph, SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioOpenDoc(u.first, u.second, 0);
    ImGui::PopID();
    ux += bw + P(6.0f);
  }
  if (nShown == 0)
    T(d, STUDIO_FONT_TEXT, kStudioFsS, ux, uy + P(5.0f), C(TK_INK3), "nothing yet");
  y = uy + P(38.0f);
  // what it needs, in words
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::BeginGroup();
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), bTech ? "Needs to research" : "Needs to upgrade", 0.1f, true);
  StudioRequirements(t, name, "requires", Mine(t, name), std::min(W / StudioPx(1.0f), 640.0f));
  if (bTech) {
    Gap(6.0f);
    StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), "Needs to cast", 0.1f, true);
    StudioRequirements(t, name, "use_requires", Mine(t, name), std::min(W / StudioPx(1.0f), 640.0f));
  }
  ImGui::EndGroup();
  y = ImGui::GetItemRectMax().y + P(10.0f);
  d->AddLine(ImVec2(o.x, y), ImVec2(o.x + W, y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

// ---- the commands -----------------------------------------------------------------------------------

namespace {

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::string BtnJson(const Btn &b, int nIndex) {
  return "{\"index\":" + std::to_string(nIndex) + ",\"position\":" + std::to_string(b.nPos) + ",\"classic\":" + (b.bClassic ? "true" : "false") +
         ",\"mod_index\":" + std::to_string(b.nMod) + ",\"from\":" + Q(b.dep) + ",\"does\":" + Q(b.what) + ",\"target\":" + Q(b.target) +
         ",\"hotkey\":" + Q(b.cKey ? std::string(1, b.cKey) : std::string()) + ",\"text\":" + Q(b.text) + ",\"disabled\":" + Q(b.disabled) +
         ",\"icon\":" + std::to_string(b.nIcon) + ",\"aimed\":" + (b.bAimed ? "true" : "false") + ",\"ai\":" + Q(b.ai) +
         ",\"shown_when_before_is_not\":" + (b.bAlt ? "true" : "false") + ",\"words\":" + Q(Does(b)) +
         ",\"words_index\":" + std::to_string(b.nWords) + ",\"reworded\":" + (b.bReworded ? "true" : "false") +
         ",\"classic_text\":" + Q(b.classicText) + ",\"classic_disabled\":" + Q(b.classicDisabled) + "}";
}

std::string CardJson(const Card &c) {
  std::string list;
  for (size_t i = 0; i < c.btns.size(); ++i)
    list += (i ? "," : "") + BtnJson(c.btns[i], static_cast<int>(i));
  const Clashes cl = ClashesOf(c);
  std::string keys, places;
  for (const auto &k : cl.keys) {
    std::string idx;
    for (int i : k.second)
      idx += (idx.empty() ? "" : ",") + std::to_string(i);
    keys += (keys.empty() ? "" : ",") + std::string("{\"hotkey\":") + Q(std::string(1, k.first)) + ",\"buttons\":[" + idx + "]}";
  }
  for (const auto &p : cl.places) {
    std::string idx;
    for (int i : p.second)
      idx += (idx.empty() ? "" : ",") + std::to_string(i);
    places += (places.empty() ? "" : ",") + std::string("{\"position\":") + std::to_string(p.first) + ",\"buttons\":[" + idx + "]}";
  }
  const char *pszFrom = c.nClassic >= 0 && c.nClassic < StudioClassicRows(ST_UNITS) ? StudioClassicName(ST_UNITS, c.nClassic) : nullptr;
  return "\"unit\":" + Q(c.unit) + ",\"card\":" + Q(c.key) + ",\"from\":" + Q(pszFrom ? pszFrom : "") + ",\"own_card\":" +
         (c.bOwnCard ? "true" : "false") + ",\"about\":" + Q(CardAbout(c)) + ",\"buttons\":[" + list + "],\"hotkey_clashes\":[" + keys +
         "],\"shared_places\":[" + places + "]";
}

// studio_card {unit | card}: a unit's card as the game deals it (or a classic card by its number),
// with its clashes.
bool CmdCard(const PortJson &j, std::string *pAnswer) {
  if (j["card"].kind == PortJson::NUMBER) {
    Card c;
    c.bOk = true;
    c.nClassic = static_cast<int>(j["card"].d);
    if (!MenuKey(c.nClassic).empty()) {
      *pAnswer = CardJson(CardOf(MenuKey(c.nClassic)));
      return true;
    }
    c.key = "#" + std::to_string(c.nClassic);
    c.btns = ClassicButtons(c.nClassic);
    *pAnswer = CardJson(c);
    return true;
  }
  const Card &c = CardOf(j["unit"].Str());
  if (!c.bOk) {
    *pAnswer = "no unit " + j["unit"].Str();
    return false;
  }
  *pAnswer = CardJson(c);
  return true;
}

// Whether a button is one the loader takes, and why not.
bool ButtonOk(const PortJson &b, bool bMenu, std::string *pWhy) {
  if (b.kind != PortJson::OBJECT) {
    *pWhy = "a button is an object";
    return false;
  }
  const int nPos = static_cast<int>(b["position"].Num(0));
  if (b["position"].kind != PortJson::NUMBER || nPos < 1 || nPos > 9) {
    *pWhy = "a button's position is 1 to 9";
    return false;
  }
  if (b["classic"].kind != PortJson::NONE) {
    // the words of the card's own button at the position
    if (!(b["classic"].kind == PortJson::STRING || (b["classic"].kind == PortJson::BOOL && b["classic"].b))) {
      *pWhy = "classic names what the card's own button makes, researches or upgrades, or is true";
      return false;
    }
    for (int n = 0; n < KINDS; ++n)
      if (b[kKinds[n]].kind != PortJson::NONE) {
        *pWhy = "a button that changes a classic one's words does nothing else";
        return false;
      }
    if (b["text"].kind != PortJson::STRING && b["hotkey"].kind != PortJson::STRING && b["disabled"].kind != PortJson::STRING) {
      *pWhy = "a button that changes a classic one's words gives a text, a hotkey or disabled";
      return false;
    }
    return true;
  }
  int nKinds = 0, k = -1;
  for (int n = 0; n < KINDS; ++n)
    if (b[kKinds[n]].kind != PortJson::NONE && b[kKinds[n]].kind != PortJson::NULLV) {
      ++nKinds;
      k = n;
    }
  if (nKinds != 1) {
    *pWhy = bMenu ? "a build menu's button builds one of the mod's units" : "a button does one thing: train, upgrade, research or cast";
    return false;
  }
  if (bMenu != (k == KIND_BUILD)) {
    *pWhy = bMenu ? "a build menu's buttons each build, and do nothing else" : "only a worker's build menu's buttons build";
    return false;
  }
  const std::string target = NameIn(kKindTable[k], b[kKinds[k]]);
  if (((k == 0 || k == KIND_BUILD) && !ModUnitOk(target)) || (k == 1 && !ModUpgradeOk(target)) || (k == 3 && !CastOk(target)) ||
      (k == 2 && !IsMod(ST_TECHS, target))) {
    *pWhy = std::string("a button's ") + kKinds[k] + " names one of the mod's " +
            (k == 0 || k == KIND_BUILD ? "units" : k == 1 ? "upgrades" : k == 2 ? "techs" : "orders, whose tech is the mod's") + "; " +
            target + " is not";
    return false;
  }
  if (k == 2 && !HasRequires(target)) {
    *pWhy = "a research names a tech with requires (where it is researched); " + target + " has none";
    return false;
  }
  if (b["ai"].kind == PortJson::STRING) {
    bool bOk = false;
    for (int f = 0; f < MOD_AI_FINDERS; ++f)
      bOk = bOk || b["ai"].s == kModAiFinders[f];
    if (!bOk) {
      *pWhy = "a cast's ai names one of the classic spells' finders, as \"psionic_storm\", or \"enemy_cluster\"";
      return false;
    }
  }
  if (b["hotkey"].kind != PortJson::NONE && b["hotkey"].Str().size() != 1) {
    *pWhy = "a hotkey is one letter";
    return false;
  }
  return true;
}

// The card's key for a unit, or why it has none.
bool KeyOf(const std::string &unit, std::string *pKey, std::string *pWhy) {
  if (MenuCard(unit) >= 0) {
    *pKey = MenuKey(MenuCard(unit));
    return true;
  }
  StudioRecord r;
  if (!StudioRecordOf(ST_UNITS, unit, &r)) {
    *pWhy = "no unit " + unit;
    return false;
  }
  *pKey = CardKey(unit);
  if (r.kind != SK_OWN && r.kind != SK_DEPENDENCY && StudioClassicRow(ST_UNITS, *pKey) < 0) {
    *pWhy = "a card is a classic unit's or a mod's unit's";
    return false;
  }
  return true;
}

// The card's list put back, as one group with what else it took: the key taken away when empty,
// and "cards" when it is then empty.
bool PutList(const std::string &key, const PortJson &list, const std::string &sender, const std::string &label, std::string *pWhy) {
  if (list.items.empty()) {
    if (StudioDoc("types")["cards"][key.c_str()].kind == PortJson::NONE)
      return true;
    if (!StudioPut("types", {"cards", key}, PortJson(), sender, label, pWhy))
      return false;
    if (StudioDoc("types")["cards"].size() == 0)
      return StudioPut("types", {"cards"}, PortJson(), sender, label, pWhy);
    return true;
  }
  return StudioPut("types", {"cards", key}, list, sender, label, pWhy);
}

PortJson ListOf(const std::string &key) {
  PortJson list = StudioDoc("types")["cards"][key.c_str()];
  if (list.kind != PortJson::ARRAY) {
    list = PortJson();
    list.kind = PortJson::ARRAY;
  }
  return list;
}

std::string Named(const PortJson &b) {
  const std::string s = Plain(b["text"].Str());
  if (!s.empty())
    return s;
  for (int k = 0; k < KINDS; ++k)
    if (b[kKinds[k]].kind == PortJson::STRING)
      return b[kKinds[k]].s;
  return "a button";
}

bool Answer(bool bOk, const std::string &key, const std::string &why, std::string *pAnswer) {
  if (!bOk) {
    *pAnswer = why;
    return false;
  }
  const PortJson list = ListOf(key);
  *pAnswer = "\"card\":" + Q(key) + ",\"mod_buttons\":" + PortJsonWrite(list) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// studio_card_set {unit, index?, button?, fields?, remove?}: one of the mod's buttons on a unit's card
// (index, its place in the mod's list for the card): set whole (button), its fields merged (fields,
// a field null taken away), or taken off (remove); without index, button is added.
bool CmdCardSet(const PortJson &j, std::string *pAnswer) {
  std::string key, why;
  if (!KeyOf(j["unit"].Str(), &key, &why)) {
    *pAnswer = why;
    return false;
  }
  PortJson list = ListOf(key);
  const bool bIndex = j["index"].kind == PortJson::NUMBER;
  const int nIndex = static_cast<int>(j["index"].Num(-1));
  if (bIndex && (nIndex < 0 || nIndex >= static_cast<int>(list.items.size()))) {
    *pAnswer = "the card of " + key + " has no button " + std::to_string(nIndex) + " of the mod's";
    return false;
  }
  std::string label;
  if (j["remove"].kind == PortJson::BOOL && j["remove"].b) {
    if (!bIndex) {
      *pAnswer = "remove needs index";
      return false;
    }
    label = "took " + Named(list.items[static_cast<size_t>(nIndex)]) + " off the card of " + key;
    list.items.erase(list.items.begin() + nIndex);
  } else if (j["button"].kind == PortJson::OBJECT) {
    if (!ButtonOk(j["button"], MenuCard(key) >= 0, &why)) {
      *pAnswer = why;
      return false;
    }
    if (bIndex) {
      list.items[static_cast<size_t>(nIndex)] = j["button"];
      label = "set " + Named(j["button"]) + " on the card of " + key;
    } else {
      list.items.push_back(j["button"]);
      label = "put " + Named(j["button"]) + " on the card of " + key + " at " + std::to_string(static_cast<int>(j["button"]["position"].Num(0)));
    }
  } else if (j["fields"].kind == PortJson::OBJECT && bIndex) {
    PortJson b = list.items[static_cast<size_t>(nIndex)];
    std::string what;
    for (const PortJsonMember &m : j["fields"].members) {
      if (m.second.kind == PortJson::NULLV || m.second.kind == PortJson::NONE)
        StudioJsonErase(b, m.first);
      else
        StudioJsonPut(b, m.first, m.second);
      if (!(m.second.kind == PortJson::NULLV))
        what += (what.empty() ? "" : ", ") + m.first;
    }
    if (!ButtonOk(b, MenuCard(key) >= 0, &why)) {
      *pAnswer = why;
      return false;
    }
    list.items[static_cast<size_t>(nIndex)] = b;
    label = "set " + (what.empty() ? std::string("a button") : what) + " of " + Named(b) + " on the card of " + key;
  } else {
    *pAnswer = "studio_card_set needs button, fields with index, or remove with index";
    return false;
  }
  const std::string sender = Sender(j);
  StudioGroupBegin(sender, label);
  const bool bOk = PutList(key, list, sender, label, &why);
  StudioGroupEnd();
  return Answer(bOk, key, why, pAnswer);
}

// studio_card_move {unit, index, position}: one of the mod's buttons to a place; a button of the
// mod's at that place takes the place it left.
bool CmdCardMove(const PortJson &j, std::string *pAnswer) {
  std::string key, why;
  if (!KeyOf(j["unit"].Str(), &key, &why)) {
    *pAnswer = why;
    return false;
  }
  PortJson list = ListOf(key);
  const int nIndex = static_cast<int>(j["index"].Num(-1)), nPos = static_cast<int>(j["position"].Num(0));
  if (nIndex < 0 || nIndex >= static_cast<int>(list.items.size()) || nPos < 1 || nPos > 9) {
    *pAnswer = "studio_card_move needs index, one of the mod's buttons on the card, and position 1 to 9";
    return false;
  }
  PortJson &b = list.items[static_cast<size_t>(nIndex)];
  const PortJson was = b["position"];
  if (static_cast<int>(was.Num(0)) == nPos) {
    *pAnswer = "\"card\":" + Q(key) + ",\"unchanged\":true";
    return true;
  }
  for (PortJson &o : list.items)
    if (&o != &b && static_cast<int>(o["position"].Num(0)) == nPos)
      StudioJsonPut(o, "position", was);
  StudioJsonPut(b, "position", StudioJsonNumber(nPos));
  const std::string label = "moved " + Named(b) + " to " + std::to_string(nPos) + " on the card of " + key;
  StudioGroupBegin(Sender(j), label);
  const bool bOk = PutList(key, list, Sender(j), label, &why);
  StudioGroupEnd();
  return Answer(bOk, key, why, pAnswer);
}

// studio_card_train {unit, card, position?, hotkey?, text?}: a button that trains one of the mod's
// units on a card, at its first free place with a free hotkey unless given, and the card's unit put
// first in what the unit needs as where it is made when it names none; one group.
bool MadeAt(const std::string &unit, const std::string &at, const std::string &sender, std::string *pWhy);

bool CmdCardTrain(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str();
  std::string key, why;
  if (!ModUnitOk(unit)) {
    *pAnswer = "studio_card_train trains one of the mod's units; " + unit + " is not";
    return false;
  }
  if (!KeyOf(j["card"].Str(), &key, &why)) {
    *pAnswer = why;
    return false;
  }
  const Card &c = CardOf(j["card"].Str());
  const int nPos = j["position"].kind == PortJson::NUMBER ? static_cast<int>(j["position"].d) : FreePlace(c);
  if (nPos < 1 || nPos > 9) {
    *pAnswer = "the card of " + key + " has no free place: give position";
    return false;
  }
  const std::string text0 = j["text"].Str(DefaultText("train", ST_UNITS, unit, j["card"].Str()).c_str());
  const char key0 = j["hotkey"].Str().empty() ? (Marked(text0) ? Marked(text0) : FreeKey(c, text0, -1))
                                              : static_cast<char>(tolower(static_cast<unsigned char>(j["hotkey"].Str()[0])));
  PortJson b = StudioJsonObject();
  StudioJsonPut(b, "position", StudioJsonNumber(nPos));
  StudioJsonPut(b, "train", StudioJsonString(unit));
  StudioJsonPut(b, "hotkey", StudioJsonString(std::string(1, key0)));
  StudioJsonPut(b, "text", StudioJsonString(Marked(text0) ? text0 : HotText(text0, key0)));
  PortJson list = ListOf(key);
  list.items.push_back(b);
  const std::string sender = Sender(j), label = "a button that makes " + unit + " on the card of " + key;
  const int nAt = StudioHistoryAt();
  StudioGroupBegin(sender, label);
  bool bOk = PutList(key, list, sender, label, &why) && MadeAt(unit, key, sender, &why);
  StudioGroupEnd();
  if (!bOk && StudioHistoryAt() > nAt) {
    std::string ignore;
    StudioUndo(&ignore);
  }
  return Answer(bOk, key, why, pAnswer);
}

// A unit of the mod's own made at a unit: "made at" put first in what it needs, when they name no
// place; part of the caller's group.
bool MadeAt(const std::string &unit, const std::string &at, const std::string &sender, std::string *pWhy) {
  bool bOk = true;
  if (Mine(ST_UNITS, unit)) {
    StudioValue v;
    StudioGet(ST_UNITS, unit, "requires", &v);
    // its needs as it has them now, its own or its base's
    PortJson req = v.own.kind != PortJson::NONE ? v.own : !v.from.empty() ? StudioRecordJson(ST_UNITS, v.from)["requires"] : PortJson();
    if (req.kind != PortJson::ARRAY && req.kind != PortJson::NONE) {
      PortJson one = req;
      req = PortJson();
      req.kind = PortJson::ARRAY;
      req.items.push_back(one);
    }
    bool bAt = false;
    for (const PortJson &q : req.items)
      bAt = bAt || (q.kind == PortJson::OBJECT && q["at"].kind != PortJson::NONE);
    if (!bAt) {
      PortJson one = StudioJsonObject();
      StudioJsonPut(one, "at", StudioJsonString(at));
      req.kind = PortJson::ARRAY;
      req.items.insert(req.items.begin(), one);
      bOk = StudioSetField(ST_UNITS, unit, "requires", req, sender, std::string(), pWhy);
    }
  }
  return bOk;
}

// studio_card_build {unit, menu?, position?, hotkey?, text?}: a button that builds one of the mod's
// units on a worker's build menu (its name, or "basic" or "advanced" of the unit's race; advanced
// when not given), at its first free place with a free hotkey unless given, and the worker put
// first in what the unit needs as where it is made when they name none; one group.
bool CmdCardBuild(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str();
  if (!ModUnitOk(unit)) {
    *pAnswer = "studio_card_build builds one of the mod's units; " + unit + " is not";
    return false;
  }
  std::string menu = j["menu"].Str("advanced");
  if (menu == "basic" || menu == "advanced") {
    const int r = UnitRace(unit);
    if (r < 0) {
      *pAnswer = unit + " has no race to pick a worker by: give menu, as \"protoss_advanced_structures\"";
      return false;
    }
    menu = MenuKey(238 + r + (menu == "advanced" ? 3 : 0));
  }
  if (MenuCard(menu) < 0) {
    *pAnswer = "menu is a worker's build menu, as \"terran_basic_structures\", or basic or advanced";
    return false;
  }
  const Card &c = CardOf(menu);
  const int nPos = j["position"].kind == PortJson::NUMBER ? static_cast<int>(j["position"].d) : FreePlace(c);
  if (nPos < 1 || nPos > 9) {
    *pAnswer = "the " + CardName(menu) + " have no free place: give position (the button shows there for a race that goes without the classic one)";
    return false;
  }
  const std::string text0 = j["text"].Str(DefaultText("build", ST_UNITS, unit, menu).c_str());
  const char key0 = j["hotkey"].Str().empty() ? (Marked(text0) ? Marked(text0) : FreeKey(c, text0, -1))
                                              : static_cast<char>(tolower(static_cast<unsigned char>(j["hotkey"].Str()[0])));
  PortJson b = StudioJsonObject();
  StudioJsonPut(b, "position", StudioJsonNumber(nPos));
  StudioJsonPut(b, "build", StudioJsonString(unit));
  StudioJsonPut(b, "hotkey", StudioJsonString(std::string(1, key0)));
  StudioJsonPut(b, "text", StudioJsonString(Marked(text0) ? text0 : HotText(text0, key0)));
  PortJson list = ListOf(menu);
  list.items.push_back(b);
  const std::string sender = Sender(j), label = "a button that builds " + unit + " on " + menu;
  std::string why;
  const int nAt = StudioHistoryAt();
  StudioGroupBegin(sender, label);
  const bool bOk = PutList(menu, list, sender, label, &why) && MadeAt(unit, MenuWorker(MenuCard(menu)), sender, &why);
  StudioGroupEnd();
  if (!bOk && StudioHistoryAt() > nAt) {
    std::string ignore;
    StudioUndo(&ignore);
  }
  return Answer(bOk, menu, why, pAnswer);
}

}  // namespace

int studio_races::StandInWords(const std::vector<std::pair<std::string, std::string>> &pairs, const std::set<std::string> &records,
                               const std::string &sender, std::string *pWhy) {
  int nMade = 0;
  for (int n = 0; n < 250; ++n) {
    if (ClassicButtons(n).empty())
      continue;
    const char *psz = n < StudioClassicRows(ST_UNITS) ? StudioClassicName(ST_UNITS, n) : nullptr;
    const std::string key = MenuKey(n).empty() ? (psz ? psz : "") : MenuKey(n);
    if (key.empty())
      continue;
    const Card &c = CardOf(key);
    PortJson list = ListOf(key);
    bool bChanged = false;
    for (const Btn &o : c.btns) {
      if (!o.bClassic || o.disabled.empty() || !records.count(o.target))
        continue;
      std::string dis = o.disabled;
      for (const auto &p : pairs) {
        const std::string by = StudioShowName(ST_UNITS, p.second);
        if (dis.find(by) != std::string::npos)
          continue;
        // the classic unit's name as the text gives it: whole, or without its race's word
        std::string name = StudioShowName(ST_UNITS, p.first);
        size_t at = dis.find(name);
        if (at == std::string::npos) {
          for (const char *pszRace : {"Terran ", "Zerg ", "Protoss "})
            if (name.compare(0, strlen(pszRace), pszRace) == 0) {
              name = name.substr(strlen(pszRace));
              break;
            }
          at = dis.find(name);
        }
        if (at != std::string::npos)
          dis = dis.substr(0, at + name.size()) + " or " + by + dis.substr(at + name.size());
      }
      if (dis == o.disabled)
        continue;
      if (o.nWords >= 0) {
        StudioJsonPut(list.items[static_cast<size_t>(o.nWords)], "disabled", StudioJsonString(dis));
      } else {
        PortJson e = StudioJsonObject();
        StudioJsonPut(e, "position", StudioJsonNumber(o.nPos));
        PortJson all;
        all.kind = PortJson::BOOL;
        all.b = true;
        StudioJsonPut(e, "classic", o.target.empty() ? all : StudioJsonString(o.target));
        StudioJsonPut(e, "disabled", StudioJsonString(dis));
        list.items.push_back(e);
      }
      bChanged = true;
      ++nMade;
    }
    if (bChanged && !PutList(key, list, sender, "the stand-ins named where the classic buttons say what they need", pWhy))
      return -1;
  }
  return nMade;
}

std::vector<std::string> studio_races::ButtonsFor(const char *pszWhat, const std::string &target) {
  std::vector<std::string> out;
  for (const Where &w : WhereOn(pszWhat, target)) {
    const std::string u = MenuCard(w.unit) >= 0 ? MenuWorker(MenuCard(w.unit)) : w.unit;
    if (!u.empty() && std::find(out.begin(), out.end(), u) == out.end())
      out.push_back(u);
  }
  return out;
}

void studio_races::CardsInit(void) {
  PortControlCommand("studio_card", CmdCard);
  PortControlCommand("studio_card_set", CmdCardSet);
  PortControlCommand("studio_card_move", CmdCardMove);
  PortControlCommand("studio_card_train", CmdCardTrain);
  PortControlCommand("studio_card_build", CmdCardBuild);
}
