// studio_strings.cpp -- the Strings workspace: every text a player sees that the mod gives or could
// give, in one place. The units' names, the names the game's strings give the mod's weapons, techs
// and upgrades, the races' names, each command card button's text and its text while it cannot, and
// the mod's own page (mod.json's name and line). Each shows where it is, its text now, where the
// text comes from and the classic text it stands in for; search, filters and sort; a text edited in
// place, and find and replace across the texts shown as one undo.
//
// The panel edits through the commands: studio_set or studio_create for a unit's name, studio_card_set
// for a button's words (a classic button's by its "classic" entry, docs/mod-data.md, Cards),
// studio_race_set for a race's name and studio_put for mod.json. studio_string_set and
// studio_strings_replace, for Claude and the tools, make the same changes through the model.
//
// The mod's languages (studio_languages.cpp) are a column each beside the mod's own text: a
// string's translation edited in place, a missing one marked, one made from a text that has
// changed since marked stale; a language's menu filters what is missing in it and writes or
// reads a translator's file.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include "statcmd.h"  // statcmd_btn_t
#include "order.h"    // the buttons' actions, status_get_str
#include "imgui_internal.h"
#include "../port/data/port_language.h"  // g_apszPortLanguages
#include "../port/data/port_moddata.h"   // MOD_STRING_FIRST
#include "../port/data/port_modlang.h"   // ModLangCardKey
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_languages.h"
#include "studio_races.h"

// statcmd.cpp's card record, as it declares it there: the count, the buttons, a linked card.
struct statcmd_card_t {
  WORD wBtnCount;
  BYTE _unmapped02[2];
  statcmd_btn_t *pBtns;
  WORD wLink;
  BYTE _unmapped0A[2];
};
const statcmd_card_t *StatcmdClassicCard(WORD wCard);
// The loaded stat_txt.tbl (status.cpp): there once a game's console is up.
extern void *sgpStatStrs;

namespace {

using namespace studio_draw;
using studio_races::NameIn;
using studio_races::UnitRace;

// ---- the texts' marks ---------------------------------------------------------------------------------

// A classic text: the hotkey's letter, which the game highlights, as <K>; the other colour codes left out.
std::string FromGame(const char *psz) {
  std::string s;
  for (; psz && *psz; ++psz) {
    const unsigned char c = static_cast<unsigned char>(*psz);
    if (c == 3 && psz[1] && static_cast<unsigned char>(psz[1]) >= 0x20) {
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

// The text without its mark, and where the marked letter is in it (-1 for none); new lines kept.
std::string Unmark(const std::string &text, int *pnMark) {
  std::string s;
  *pnMark = -1;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '<' && i + 2 < text.size() && text[i + 2] == '>' && *pnMark < 0) {
      *pnMark = static_cast<int>(s.size());
      s += text[i + 1];
      i += 2;
    } else {
      s += text[i];
    }
  }
  return s;
}

std::string Plain(const std::string &text) {
  int n = 0;
  return Unmark(text, &n);
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
  const size_t at = Lower(plain).find(static_cast<char>(tolower(static_cast<unsigned char>(key))));
  if (at == std::string::npos)
    return std::string("<") + static_cast<char>(toupper(static_cast<unsigned char>(key))) + ">" + plain;
  return plain.substr(0, at) + "<" + plain[at] + ">" + plain.substr(at + 1);
}

std::string MarkAt(const std::string &plain, int nAt) {
  if (nAt < 0 || nAt >= static_cast<int>(plain.size()))
    return plain;
  return plain.substr(0, static_cast<size_t>(nAt)) + "<" + plain[static_cast<size_t>(nAt)] + ">" + plain.substr(static_cast<size_t>(nAt) + 1);
}

// Every find in text replaced; a marked letter stays on its letter, or, when the letter was in what was
// replaced, goes to the same key's letter in what replaced it, else its first in the text. The count
// of finds given back.
std::string ReplaceIn(const std::string &text, const std::string &find, const std::string &repl, bool bCase, int *pnCount) {
  *pnCount = 0;
  if (find.empty())
    return text;
  int nMark = -1;
  const std::string plain = Unmark(text, &nMark);
  const char key = nMark >= 0 ? static_cast<char>(tolower(static_cast<unsigned char>(plain[static_cast<size_t>(nMark)]))) : 0;
  const std::string hay = bCase ? plain : Lower(plain), needle = bCase ? find : Lower(find);
  std::string out;
  int nNewMark = -1;
  size_t at = 0;
  while (at <= plain.size()) {
    const size_t hit = hay.find(needle, at);
    const size_t end = hit == std::string::npos ? plain.size() : hit;
    if (nMark >= 0 && nMark >= static_cast<int>(at) && nMark < static_cast<int>(end))
      nNewMark = static_cast<int>(out.size()) + (nMark - static_cast<int>(at));
    out += plain.substr(at, end - at);
    if (hit == std::string::npos)
      break;
    ++*pnCount;
    if (nMark >= static_cast<int>(hit) && nMark < static_cast<int>(hit + needle.size())) {
      const size_t k = Lower(repl).find(key);
      if (k != std::string::npos)
        nNewMark = static_cast<int>(out.size() + k);
    }
    out += repl;
    at = hit + needle.size();
  }
  if (*pnCount == 0)
    return text;
  if (nMark < 0)
    return out;
  return nNewMark >= 0 ? MarkAt(out, nNewMark) : HotText(out, key);
}

// ---- a card's buttons, as the game deals them (studio_races_cards.cpp builds the same) --------------

struct Btn {
  int nPos = 0;
  bool bClassic = true;
  int nMod = -1;          // its place in this mod's list for the card; -1 for a classic button or a dependency's
  std::string dep;        // the mod it depends on that adds it
  int nDep = -1;          // its place in that mod's list
  std::string what;       // train, build, research... or a classic command's word
  int t = -1;             // the table its target is in
  std::string target;     // the record it makes, researches, upgrades or casts
  char cKey = 0;
  std::string text, disabled;
  bool bAimed = false;
  // a classic button's words as the mods give them: whose each is (0 classic, 1 the mod's, 2 a
  // dependency's), the classic words, and the mod's own entry (-1 none)
  int nTextBy = 0, nKeyBy = 0, nDisBy = 0;
  std::string textDep, disDep;
  std::string classicText, classicDisabled;
  char cClassicKey = 0;
  int nWords = -1;
  // the keys a translation names them by: a mod's button's entry, and the entries that give a
  // classic button its text and its text while it cannot (port_modlang.h)
  std::string tbase, textKey, disKey;
};

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

// A classic card's buttons, the mod's that a game put among them left out; kept once the game's
// strings are there to give their words.
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
      Classify(b, &o);
      if (sgpStatStrs != NULL) {
        const char *psz = status_get_str(b.wStatusStrId);
        if (psz && psz[0]) {
          o.cKey = static_cast<char>(tolower(static_cast<unsigned char>(psz[0])));
          o.text = FromGame(psz + 2);
        }
        if (b.wDisabledStrId != 0 && b.wDisabledStrId < MOD_STRING_FIRST)
          o.disabled = FromGame(status_get_str(b.wDisabledStrId));
      }
      if (o.text.empty())
        o.text = o.target.empty() ? o.what : StudioShowName(o.t, o.target);
      o.classicText = o.text;
      o.classicDisabled = o.disabled;
      o.cClassicKey = o.cKey;
      out.push_back(o);
    }
  }
  if (sgpStatStrs == NULL) {
    s_now = out;
    return s_now;
  }
  return s_kept[nCard] = out;
}

const char *const kModKinds[] = {"train", "upgrade", "research", "cast", "build"};
const int kModKindTable[] = {ST_UNITS, ST_UPGRADES, ST_TECHS, ST_ORDERS, ST_UNITS};

Btn FromJson(const PortJson &j, int nIndex, const std::string &dep, const std::string &card) {
  Btn o;
  o.tbase = ModLangCardKey(card, j);
  o.bClassic = false;
  o.nMod = dep.empty() ? nIndex : -1;
  o.nDep = dep.empty() ? -1 : nIndex;
  o.dep = dep;
  o.nPos = static_cast<int>(j["position"].Num(0));
  for (int k = 0; k < 5; ++k)
    if (j[kModKinds[k]].kind != PortJson::NONE) {
      o.what = kModKinds[k];
      o.t = kModKindTable[k];
      o.target = NameIn(o.t, j[kModKinds[k]]);
      break;
    }
  const std::string key = j["hotkey"].Str();
  o.cKey = key.empty() ? 0 : static_cast<char>(tolower(static_cast<unsigned char>(key[0])));
  o.text = j["text"].Str();
  if (o.text.empty() && !o.target.empty())
    o.text = StudioShowName(o.t, o.target);
  o.disabled = j["disabled"].Str();
  o.bAimed = j["target"].kind == PortJson::BOOL && j["target"].b;
  return o;
}

void ModButtons(const std::string &key, std::vector<Btn> *pOut) {
  for (int i = 0; i < StudioDependencies(); ++i) {
    const PortJson &list = StudioDependencyTypes(i)["cards"][key.c_str()];
    for (size_t k = 0; k < list.size(); ++k)
      if (list.At(k)["classic"].kind == PortJson::NONE)
        pOut->push_back(FromJson(list.At(k), static_cast<int>(k), StudioDependencyId(i), key));
  }
  const PortJson &list = StudioDoc("types")["cards"][key.c_str()];
  for (size_t k = 0; k < list.size(); ++k)
    if (list.At(k)["classic"].kind == PortJson::NONE)
      pOut->push_back(FromJson(list.At(k), static_cast<int>(k), std::string(), key));
}

// The classic button an entry with "classic" words: the first at its place that makes what it names,
// or the first there for true.
bool WordsMatch(const PortJson &e, const Btn &o) {
  if (e["classic"].kind == PortJson::NONE || !o.bClassic || o.nPos != static_cast<int>(e["position"].Num(0)))
    return false;
  const std::string target = e["classic"].kind == PortJson::STRING ? Lower(e["classic"].s) : std::string();
  return target.empty() || Lower(o.target) == target;
}

// The words the mods give the card's classic buttons, the mod's own last.
void ClassicWords(const std::string &key, std::vector<Btn> *pBtns) {
  auto apply = [&](const PortJson &list, const std::string &dep) {
    for (size_t k = 0; k < list.size(); ++k) {
      const PortJson &e = list.At(k);
      for (Btn &o : *pBtns)
        if (WordsMatch(e, o)) {
          const int by = dep.empty() ? 1 : 2;
          if (e["text"].kind == PortJson::STRING) {
            o.text = e["text"].s;
            o.nTextBy = by;
            o.textDep = dep;
            o.textKey = ModLangCardKey(key, e);
          }
          if (e["hotkey"].kind == PortJson::STRING && !e["hotkey"].s.empty()) {
            o.cKey = static_cast<char>(tolower(static_cast<unsigned char>(e["hotkey"].s[0])));
            o.nKeyBy = by;
          }
          if (e["disabled"].kind == PortJson::STRING) {
            o.disabled = e["disabled"].s;
            o.nDisBy = by;
            o.disDep = dep;
            o.disKey = ModLangCardKey(key, e);
          }
          if (dep.empty())
            o.nWords = static_cast<int>(k);
          break;
        }
    }
  };
  for (int i = 0; i < StudioDependencies(); ++i)
    apply(StudioDependencyTypes(i)["cards"][key.c_str()], StudioDependencyId(i));
  apply(StudioDoc("types")["cards"][key.c_str()], std::string());
}

bool HasModButtons(const std::string &key) {
  if (StudioDoc("types")["cards"][key.c_str()].size() > 0)
    return true;
  for (int i = 0; i < StudioDependencies(); ++i)
    if (StudioDependencyTypes(i)["cards"][key.c_str()].size() > 0)
      return true;
  return false;
}

// A mod unit's classic row whose card it starts from: its looks, else its base's, as far as needed.
int LooksRow(const std::string &unit) {
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
    at = NameIn(ST_UNITS, j["looks"].kind != PortJson::NONE ? j["looks"] : j["base"]);
    if (at.empty())
      return -1;
  }
  return -1;
}

// The workers' build menus, classic cards 238 to 243, by the names "cards" files them under.
const char *const kMenuKeys[6] = {"zerg_basic_structures",    "terran_basic_structures",    "protoss_basic_structures",
                                  "zerg_advanced_structures", "terran_advanced_structures", "protoss_advanced_structures"};

std::string CardName(const std::string &key, int nCard) {
  if (nCard >= 238 && nCard <= 243) {
    static const char *const kWorkers[3] = {"Drone", "SCV", "Probe"};
    return std::string(kWorkers[(nCard - 238) % 3]) + (nCard >= 241 ? ": advanced structures" : ": basic structures");
  }
  return StudioShowName(ST_UNITS, key);
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
    return "casts " + n + (b.bAimed ? ", aimed" : "");
  return b.what + " " + n;
}

// ---- the strings ----------------------------------------------------------------------------------------

enum { K_NAME, K_LABEL, K_BUTTON, K_DISABLED, K_RACE, K_MOD, K_SUBTITLE, KINDS };
const char *const kKindIds[KINDS] = {"name", "label", "button", "disabled", "race", "mod", "subtitle"};
const char *const kKindWords[KINDS] = {"Unit name", "Label", "Button", "When it cannot", "Race", "Mod page", "Subtitle"};
const char *const kKindGlyphs[KINDS] = {"unit", "weapon", "cards", "needs", "race", "home", "sound"};
enum { SRC_CLASSIC, SRC_MOD, SRC_DEP };
const char *const kSrcIds[3] = {"classic", "mod", "dependency"};

struct Str {
  std::string id;
  std::string alias;      // an id it answers to as well: an edit's name answers to the classic unit's
  int nKind = K_NAME;
  std::string where;      // the record, the card, the race or the file
  std::string place;      // within it
  std::string text;       // as it is now, a button's hotkey marked <K>
  std::string classic;    // the classic text the mod's stands in for; empty when there is none or it is the classic one
  int nSource = SRC_CLASSIC;
  std::string from;       // the dependency, or the game's string
  bool bEdit = false;
  bool bChanged = false;  // since the last save
  bool bMulti = false;    // new lines in it, or a text that may have them
  char cKey = 0;
  std::string note;       // why it cannot be edited, or what an empty one means
  // what it is part of
  std::string rec;        // a unit's record (or the classic row's name), a race, mod.json's member, a card's key
  int nPos = 0;
  std::string target;     // a classic button: what it makes, "" for the first at its place
  bool bClassicBtn = false;
  int nMod = -1;          // a mod's button: its place in the card's list
  char cClassicKey = 0;   // a classic button's own hotkey
  std::string classicOwn; // the classic text of a classic button's field or a classic unit's name
  std::string tkey;       // the key a translation names it by, "" for a text the game gives (port_modlang.h)
};

std::string SourceWords(const Str &s) {
  return s.nSource == SRC_CLASSIC ? "classic" : s.nSource == SRC_MOD ? "the mod's" : s.from;
}

// The name a classic unit's edit takes: its name with _edit, made free.
std::string EditName(const std::string &classic) {
  std::string name = classic + "_edit";
  StudioRecord r;
  for (int n = 2; StudioRecordOf(ST_UNITS, name, &r) && n < 100; ++n)
    name = classic + "_edit" + std::to_string(n);
  return name;
}

// The texts at the last save, by id, to tell what changed since.
std::map<std::string, std::string> g_saved;
std::string g_savedMod;
bool g_bSavedStrings = false;

void AddModPage(std::vector<Str> *pOut) {
  const PortJson &mj = StudioDoc("mod");
  static const char *const kKeys[2] = {"name", "line"};
  static const char *const kPlaces[2] = {"its name, as the list of mods shows it", "its line, under its name"};
  for (int i = 0; i < 2; ++i) {
    Str s;
    s.id = std::string("mod/") + kKeys[i];
    s.nKind = K_MOD;
    s.where = "mod.json";
    s.place = kPlaces[i];
    s.text = mj[kKeys[i]].Str();
    s.nSource = SRC_MOD;
    s.bEdit = true;
    s.rec = kKeys[i];
    s.tkey = std::string("mod/") + kKeys[i];
    if (i == 1 && s.text.empty())
      s.note = "none given";
    pOut->push_back(s);
  }
}

void AddRaces(std::vector<Str> *pOut) {
  auto add = [&](const PortJson &races, const std::string &dep) {
    for (const PortJsonMember &m : races.members) {
      Str s;
      s.id = "race/" + m.first;
      s.nKind = K_RACE;
      s.where = "races " + m.first;
      s.place = "its name, as a player picks it and --race takes it";
      s.text = m.second["name"].Str(m.first.c_str());
      s.nSource = dep.empty() ? SRC_MOD : SRC_DEP;
      s.from = dep;
      s.bEdit = dep.empty();
      s.rec = m.first;
      s.tkey = "race/" + m.first;
      if (m.second["name"].kind != PortJson::STRING)
        s.note = "no name of its own: the game shows its record's";
      if (!dep.empty())
        s.note = "a race of " + dep + ", which this mod builds on; change it there";
      pOut->push_back(s);
    }
  };
  for (int i = 0; i < StudioDependencies(); ++i)
    add(StudioDependencyTypes(i)["races"], StudioDependencyId(i));
  add(StudioDoc("types")["races"], std::string());
}

void AddUnits(std::vector<Str> *pOut) {
  std::set<int> edited;
  std::vector<Str> classic;
  for (const StudioRecord &r : StudioRecords(ST_UNITS)) {
    const PortJson &j = StudioRecordJson(ST_UNITS, r.name);
    Str s;
    s.id = "unit/" + r.name;
    s.nKind = K_NAME;
    s.where = "units " + r.name;
    s.rec = r.name;
    s.nSource = r.kind == SK_DEPENDENCY ? SRC_DEP : SRC_MOD;
    s.from = r.kind == SK_DEPENDENCY ? r.mod : std::string();
    s.bEdit = r.kind == SK_OWN || r.kind == SK_EDIT;
    const bool bOwnName = j["name"].kind == PortJson::STRING;
    // an edit that gives no name shows the game's, which the game translates
    if (r.kind != SK_EDIT || bOwnName)
      s.tkey = "unit/" + r.name;
    if (r.kind == SK_EDIT) {
      const char *psz = StudioClassicName(ST_UNITS, r.nId);
      const std::string cls = psz ? psz : r.name;
      edited.insert(r.nId);
      s.alias = "unit/" + cls;
      s.place = "an edit of the classic " + StudioShowName(ST_UNITS, cls);
      s.classicOwn = StudioShowName(ST_UNITS, cls);
      s.text = bOwnName ? j["name"].s : s.classicOwn;
      if (bOwnName && s.text != s.classicOwn)
        s.classic = s.classicOwn;
      else if (!bOwnName)
        s.nSource = SRC_CLASSIC;
    } else {
      s.place = r.base.empty() ? "the mod's unit" : "the mod's unit, from the " + Short(ST_UNITS, r.base);
      s.text = bOwnName ? j["name"].s : r.name;
      if (!bOwnName)
        s.note = "no name of its own: the game shows its record's name";
    }
    if (r.kind == SK_DEPENDENCY)
      s.note = "a unit of " + r.mod + ", which this mod builds on; change it there";
    pOut->push_back(s);
  }
  for (int n = 0; n < StudioClassicRows(ST_UNITS); ++n) {
    const char *psz = StudioClassicName(ST_UNITS, n);
    if (psz == nullptr || edited.count(n))
      continue;
    Str s;
    s.id = std::string("unit/") + psz;
    s.nKind = K_NAME;
    s.where = std::string("units ") + psz;
    s.place = "a classic unit; a name given here makes an edit of it";
    s.text = StudioShowName(ST_UNITS, psz);
    s.classicOwn = s.text;
    s.nSource = SRC_CLASSIC;
    s.bEdit = true;
    s.rec = psz;
    pOut->push_back(s);
  }
}

// The names of the mod's weapons, techs and upgrades: the record's own "name", else the game's
// string its label numbers.
void AddLabels(std::vector<Str> *pOut) {
  static const int kTables[3] = {ST_WEAPONS, ST_TECHS, ST_UPGRADES};
  static const char *const kKeys[3] = {"weapon/", "tech/", "upgrade/"};
  for (int i = 0; i < 3; ++i) {
    const int t = kTables[i];
    for (const StudioRecord &r : StudioRecords(t)) {
      const long long n = StudioNumber(t, r.name, "label");
      const PortJson &j = StudioRecordJson(t, r.name);
      Str s;
      s.id = "label/" + Table(t) + "/" + r.name;
      s.nKind = K_LABEL;
      s.where = Table(t) + " " + r.name;
      s.rec = r.name;
      if (sgpStatStrs != NULL && n > 0 && n < MOD_STRING_FIRST)
        s.classicOwn = FromGame(status_get_str(static_cast<WORD>(n)));
      s.bEdit = r.kind == SK_OWN || r.kind == SK_EDIT;
      if (j["name"].kind == PortJson::STRING) {
        s.text = j["name"].s;
        s.place = "its name, as the game shows it, in place of the game's string " + std::to_string(n);
        s.nSource = r.kind == SK_DEPENDENCY ? SRC_DEP : SRC_MOD;
        s.from = r.kind == SK_DEPENDENCY ? r.mod : std::string();
        if (s.text != s.classicOwn)
          s.classic = s.classicOwn;
        s.tkey = kKeys[i] + r.name;
      } else {
        s.text = s.classicOwn;
        s.place = "its name, the game's string " + std::to_string(n);
        s.nSource = SRC_CLASSIC;
        s.from = "string " + std::to_string(n);
        s.note = "the game's own string, in the game's language; a name given here is the mod's own";
      }
      if (r.kind == SK_DEPENDENCY)
        s.note = (i == 2 ? "an " : "a ") + Table(t).substr(0,Table(t).size() - 1) + " of " + r.mod + ", which this mod builds on; change it there";
      pOut->push_back(s);
    }
  }
}

// The subtitles of the mod's sounds, the words of a voice line (docs/mod-data.md, Sounds): each sound
// of its own and each edit of a classic one, with a subtitle or without one yet; a dependency's
// that gives one, read only.
void AddSubtitles(std::vector<Str> *pOut) {
  for (const StudioRecord &r : StudioRecords(ST_SOUNDS)) {
    const PortJson &j = StudioRecordJson(ST_SOUNDS, r.name);
    const bool bHas = j["subtitle"].kind == PortJson::STRING;
    if (r.kind == SK_DEPENDENCY && !bHas)
      continue;
    Str s;
    s.id = "subtitle/" + r.name;
    s.nKind = K_SUBTITLE;
    s.where = "sound " + r.name;
    s.rec = r.name;
    s.place = "the words of the line, shown as a unit says it";
    s.text = bHas ? j["subtitle"].s : std::string();
    s.nSource = r.kind == SK_DEPENDENCY ? SRC_DEP : SRC_MOD;
    s.from = r.kind == SK_DEPENDENCY ? r.mod : std::string();
    s.bEdit = r.kind == SK_OWN || r.kind == SK_EDIT;
    s.tkey = "sound/" + r.name;
    if (!bHas)
      s.note = "no subtitle yet";
    if (r.kind == SK_DEPENDENCY)
      s.note = "a sound of " + r.mod + ", which this mod builds on; change it there";
    pOut->push_back(s);
  }
}

// The table a weapon's, a tech's or an upgrade's name is in, by its id label/<table>/<record>.
int LabelTable(const Str &s) {
  const size_t a = s.id.find('/'), b = s.id.find('/', a + 1);
  return StudioTableOf(s.id.substr(a + 1, b - a - 1));
}

// A card's buttons as strings: its text, and its text while it cannot.
void AddCard(const std::string &key, int nCard, std::vector<Btn> btns, std::vector<Str> *pOut) {
  ClassicWords(key, &btns);
  std::vector<Btn> mod;
  ModButtons(key, &mod);
  for (const Btn &b : mod) {
    size_t at = 0;
    while (at < btns.size() && btns[at].nPos <= b.nPos)
      ++at;
    btns.insert(btns.begin() + static_cast<long long>(at), b);
  }
  const std::string cardName = CardName(key, nCard);
  std::set<int> places;
  std::set<std::string> seen;
  for (const Btn &b : btns) {
    std::string slot;
    bool bEdit = true;
    std::string note;
    if (b.bClassic) {
      // the mod's words reach the first at its place that makes what it makes, or the first there
      const bool bFirst = !places.count(b.nPos);
      slot = "c" + std::to_string(b.nPos) + (b.target.empty() ? std::string() : "-" + b.target);
      if (seen.count(slot) || (b.target.empty() && !bFirst)) {
        bEdit = false;
        note = "shares its place with a classic button before it, whose words a mod changes in its place";
        slot += "~" + std::to_string(seen.size());
      }
    } else if (b.dep.empty()) {
      slot = "m" + std::to_string(b.nMod);
    } else {
      slot = "d" + b.dep + "-" + std::to_string(b.nDep);
      bEdit = false;
      note = "a button of " + b.dep + ", which this mod builds on; change it there";
    }
    seen.insert(slot);
    places.insert(b.nPos);
    for (int f = 0; f < 2; ++f) {
      Str s;
      s.nKind = f == 0 ? K_BUTTON : K_DISABLED;
      s.id = "card/" + key + "/" + slot + (f == 0 ? "/text" : "/disabled");
      s.where = cardName;
      s.place = "place " + std::to_string(b.nPos) + ": " + Does(b) + (f == 1 ? ", while it cannot" : "");
      s.text = f == 0 ? b.text : b.disabled;
      s.cKey = f == 0 ? b.cKey : 0;
      s.bMulti = f == 1;
      s.bEdit = bEdit;
      s.note = note;
      s.rec = key;
      s.nPos = b.nPos;
      s.target = b.target;
      s.bClassicBtn = b.bClassic;
      s.nMod = b.nMod;
      s.cClassicKey = b.cClassicKey;
      if (b.bClassic) {
        // a classic button's words the mods give are theirs to translate; the game gives its own
        if (f == 0 ? b.nTextBy != 0 : b.nDisBy != 0)
          s.tkey = (f == 0 ? b.textKey : b.disKey) + (f == 0 ? "/text" : "/disabled");
      } else {
        s.tkey = b.tbase + (f == 0 ? "/text" : "/disabled");
      }
      if (b.bClassic) {
        const int by = f == 0 ? std::max(b.nTextBy, b.nKeyBy) : b.nDisBy;
        s.classicOwn = f == 0 ? b.classicText : b.classicDisabled;
        s.nSource = by == 1 ? SRC_MOD : by == 2 ? SRC_DEP : SRC_CLASSIC;
        s.from = by == 2 ? (f == 0 ? b.textDep : b.disDep) : std::string();
        if (by != 0 && s.text != s.classicOwn)
          s.classic = s.classicOwn;
        // a classic button no condition greys has no text while it cannot
        if (f == 1 && b.classicDisabled.empty() && b.nDisBy == 0)
          continue;
        if (f == 1 && s.text.empty())
          s.note = "none given";
      } else {
        s.nSource = b.dep.empty() ? SRC_MOD : SRC_DEP;
        s.from = b.dep;
        if (f == 1 && s.text.empty() && note.empty())
          s.note = "none given: the button shows its text greyed";
      }
      pOut->push_back(s);
    }
  }
}

void AddCards(std::vector<Str> *pOut) {
  // the mod's units with buttons of their own: their looks' card with the buttons
  for (const StudioRecord &r : StudioRecords(ST_UNITS))
    if ((r.kind == SK_OWN || r.kind == SK_DEPENDENCY) && HasModButtons(r.name)) {
      const int nLooks = LooksRow(r.name);
      AddCard(r.name, -1, nLooks >= 0 ? ClassicButtons(nLooks) : std::vector<Btn>(), pOut);
    }
  // the classic cards: each unit's, then the workers' build menus
  for (int n = 0; n < StudioClassicRows(ST_UNITS); ++n) {
    const char *psz = StudioClassicName(ST_UNITS, n);
    if (psz == nullptr)
      continue;
    const std::vector<Btn> &btns = ClassicButtons(n);
    if (!btns.empty() || HasModButtons(psz))
      AddCard(psz, n, btns, pOut);
  }
  for (int n = 238; n <= 243; ++n)
    AddCard(kMenuKeys[n - 238], n, ClassicButtons(n), pOut);
}

// Every string, gathered again when the mod, its saved state or the game's strings change.
const std::vector<Str> &Strings(unsigned *pnVersion = nullptr) {
  static std::vector<Str> s_rows;
  static unsigned s_nChange = ~0u, s_nVersion = 0;
  static int s_nUnsaved = -1;
  static bool s_bStrings = false, s_bOpen = false;
  static std::string s_mod;
  const bool bStrings = sgpStatStrs != NULL;
  if (s_nChange != StudioChange() || s_nUnsaved != StudioUnsaved() || s_bStrings != bStrings || s_bOpen != StudioIsOpen() ||
      s_mod != StudioModId()) {
    s_nChange = StudioChange();
    s_nUnsaved = StudioUnsaved();
    s_bStrings = bStrings;
    s_bOpen = StudioIsOpen();
    s_mod = StudioModId();
    ++s_nVersion;
    s_rows.clear();
    if (StudioIsOpen()) {
      AddModPage(&s_rows);
      AddRaces(&s_rows);
      AddUnits(&s_rows);
      AddLabels(&s_rows);
      AddCards(&s_rows);
      AddSubtitles(&s_rows);
    }
    for (Str &s : s_rows)
      if (s.text.find('\n') != std::string::npos)
        s.bMulti = true;
    // the texts at a save, to mark what changed after it
    if (StudioIsOpen() && StudioUnsaved() == 0 && (bStrings || g_savedMod != s_mod || !g_bSavedStrings)) {
      g_saved.clear();
      for (const Str &s : s_rows)
        g_saved[s.id] = s.text;
      g_savedMod = s_mod;
      g_bSavedStrings = bStrings;
    }
    const bool bHave = StudioIsOpen() && g_savedMod == s_mod && StudioUnsaved() > 0;
    for (Str &s : s_rows) {
      if (!bHave)
        continue;
      auto it = g_saved.find(s.id);
      if (it == g_saved.end())
        s.bChanged = s.nSource == SRC_MOD;
      else
        s.bChanged = it->second != s.text && !(s.nSource == SRC_CLASSIC && !g_bSavedStrings);
    }
  }
  if (pnVersion)
    *pnVersion = s_nVersion;
  return s_rows;
}

const Str *Find(const std::string &id) {
  for (const Str &s : Strings())
    if (s.id == id)
      return &s;
  for (const Str &s : Strings())
    if (!s.alias.empty() && s.alias == id)
      return &s;
  return nullptr;
}

// ---- its translations -------------------------------------------------------------------------------

bool Translatable(const Str &s) {
  return !s.tkey.empty() && studio_languages::KeyOf(s.tkey) != nullptr;
}

// A row's translation in a language; not wanted for a row no translation names.
studio_languages::Cell LangCell(const Str &s, const std::string &code) {
  if (!Translatable(s)) {
    studio_languages::Cell c;
    c.nState = studio_languages::CELL_EMPTY;
    return c;
  }
  return studio_languages::CellOf(s.tkey, code);
}

bool ToDo(const Str &s, const std::string &code) {
  const int n = LangCell(s, code).nState;
  return n == studio_languages::CELL_MISSING || n == studio_languages::CELL_STALE;
}

// ---- which are shown --------------------------------------------------------------------------------

struct Filter {
  std::string query;
  int nKinds = (1 << KINDS) - 1;  // a bit for each kind
  bool bMine = false;             // the mod's own only
  bool bChanged = false;          // changed since the save only
  std::string missing;            // a language: the strings missing in it, or stale
};

int KindsOf(const std::string &list, std::string *pWhy) {
  if (list.empty() || list == "all")
    return (1 << KINDS) - 1;
  int n = 0;
  size_t s = 0;
  while (s <= list.size()) {
    const size_t e = list.find(',', s);
    const std::string k = list.substr(s, e == std::string::npos ? std::string::npos : e - s);
    int i = 0;
    while (i < KINDS && k != kKindIds[i] && !(k == "names" && i == K_NAME) && !(k == "buttons" && i == K_BUTTON) &&
           !(k == "races" && i == K_RACE))
      ++i;
    if (i == KINDS) {
      *pWhy = "a kind is name, label, button, disabled, race or mod (or a list of them with commas); " + k + " is none";
      return -1;
    }
    n |= 1 << i;
    if (e == std::string::npos)
      break;
    s = e + 1;
  }
  return n;
}

bool Shown(const Str &s, const Filter &f) {
  if (!(f.nKinds & (1 << s.nKind)))
    return false;
  if (f.bMine && s.nSource != SRC_MOD)
    return false;
  if (f.bChanged && !s.bChanged)
    return false;
  if (!f.missing.empty() && !ToDo(s, f.missing))
    return false;
  if (f.query.empty())
    return true;
  const char *q = f.query.c_str();
  if (Has(Plain(s.text), q) || Has(s.text, q) || Has(s.where, q) || Has(s.place, q) || Has(Plain(s.classic), q) ||
      Has(s.id, q) || Has(kKindWords[s.nKind], q))
    return true;
  // the translations are searched too
  if (Translatable(s))
    for (const std::string &code : studio_languages::Languages())
      if (Has(Plain(LangCell(s, code).text), q))
        return true;
  return false;
}

// ---- a change --------------------------------------------------------------------------------------------

// A button's text as typed, with its hotkey: a letter marked <K> becomes its hotkey; a text without a
// mark has the hotkey's letter marked.
std::string ButtonText(const Str &s, const std::string &typed, char *pcKey) {
  const char m = Marked(typed);
  const char now = s.cKey ? s.cKey : s.cClassicKey;
  *pcKey = m ? m : now;
  return m ? typed : HotText(typed, now);
}

// A card's change: its entry set whole at index, added (index -1), or taken away; or nothing to do.
struct CardChange {
  bool bNothing = false;
  bool bRemove = false;
  int nIndex = -1;
  PortJson entry;
  std::string why;
};

PortJson CardList(const std::string &key) {
  PortJson list = StudioDoc("types")["cards"][key.c_str()];
  if (list.kind != PortJson::ARRAY) {
    list = PortJson();
    list.kind = PortJson::ARRAY;
  }
  return list;
}

// The mod's "classic" entry for a classic button, by its place and what it makes, -1 for none.
int WordsEntry(const PortJson &list, int nPos, const std::string &target) {
  for (size_t k = 0; k < list.size(); ++k) {
    const PortJson &e = list.At(k);
    if (e["classic"].kind == PortJson::NONE || static_cast<int>(e["position"].Num(0)) != nPos)
      continue;
    const std::string t = e["classic"].kind == PortJson::STRING ? Lower(e["classic"].s) : std::string();
    if (t == Lower(target))
      return static_cast<int>(k);
  }
  return -1;
}

// What a button's text or its text while it cannot comes to, as the card's list has it now. bKeep keeps
// a classic button's entry when its words come back to the classic ones (a replace, whose ids must
// stay where they are).
CardChange CardChangeFor(const Str &s, const std::string &typed, bool bKeep) {
  CardChange c;
  const PortJson list = CardList(s.rec);
  const bool bText = s.nKind == K_BUTTON;
  char key = 0;
  const std::string text = bText && !typed.empty() ? ButtonText(s, typed, &key) : typed;
  if (!s.bClassicBtn) {
    if (s.nMod < 0 || s.nMod >= static_cast<int>(list.size())) {
      c.why = "the card of " + s.rec + " has no button " + std::to_string(s.nMod) + " of the mod's";
      return c;
    }
    c.nIndex = s.nMod;
    c.entry = list.At(static_cast<size_t>(s.nMod));
    if (text.empty())
      StudioJsonErase(c.entry, bText ? "text" : "disabled");
    else
      StudioJsonPut(c.entry, bText ? "text" : "disabled", StudioJsonString(text));
    if (bText && key && !text.empty())
      StudioJsonPut(c.entry, "hotkey", StudioJsonString(std::string(1, key)));
    return c;
  }
  c.nIndex = WordsEntry(list, s.nPos, s.target);
  if (c.nIndex >= 0) {
    c.entry = list.At(static_cast<size_t>(c.nIndex));
  } else {
    c.entry = StudioJsonObject();
    StudioJsonPut(c.entry, "position", StudioJsonNumber(s.nPos));
    PortJson cls;
    if (s.target.empty()) {
      cls.kind = PortJson::BOOL;
      cls.b = true;
    } else {
      cls = StudioJsonString(s.target);
    }
    StudioJsonPut(c.entry, "classic", cls);
  }
  const char *pszField = bText ? "text" : "disabled";
  // the classic words again, or none: the entry gives the field no more
  if (text.empty() || (text == s.classicOwn && !bKeep))
    StudioJsonErase(c.entry, pszField);
  else
    StudioJsonPut(c.entry, pszField, StudioJsonString(text));
  if (bText) {
    if (text.empty() || !key || key == s.cClassicKey)
      StudioJsonErase(c.entry, "hotkey");
    else
      StudioJsonPut(c.entry, "hotkey", StudioJsonString(std::string(1, key)));
  }
  const bool bEmpty = c.entry["text"].kind == PortJson::NONE && c.entry["hotkey"].kind == PortJson::NONE &&
                      c.entry["disabled"].kind == PortJson::NONE;
  if (bEmpty) {
    c.bRemove = c.nIndex >= 0;
    c.bNothing = c.nIndex < 0;
  }
  return c;
}

std::string Describe(const Str &s) {
  return (s.nKind == K_DISABLED ? "the text while it cannot of " : s.nKind == K_BUTTON ? "the text of " : s.nKind == K_SUBTITLE ? "the subtitle of " : "the name of ") + s.where +
         (s.nKind == K_BUTTON || s.nKind == K_DISABLED ? ", place " + std::to_string(s.nPos) : std::string());
}

// The change made through the model, inside the caller's group when one is open.
bool Execute(const Str &s, const std::string &text, const std::string &sender, bool bKeep, std::string *pWhy) {
  if (!s.bEdit) {
    *pWhy = s.id + " is read only: " + s.note;
    return false;
  }
  const std::string label = "set " + Describe(s);
  switch (s.nKind) {
  case K_MOD:
    if (s.rec == "name" && text.empty()) {
      *pWhy = "a mod's name is not empty";
      return false;
    }
    return StudioPut("mod", {s.rec}, text.empty() ? PortJson() : StudioJsonString(text), sender, label, pWhy);
  case K_RACE: {
    PortJson r = StudioDoc("types")["races"][s.rec.c_str()];
    if (r.kind != PortJson::OBJECT) {
      *pWhy = "the mod has no race " + s.rec;
      return false;
    }
    if (text.empty())
      StudioJsonErase(r, "name");
    else
      StudioJsonPut(r, "name", StudioJsonString(text));
    return StudioPut("types", {"races", s.rec}, r, sender, label, pWhy);
  }
  case K_NAME: {
    StudioRecord r;
    const bool bRecord = StudioRecordOf(ST_UNITS, s.rec, &r) && (r.kind == SK_OWN || r.kind == SK_EDIT);
    if (bRecord) {
      if (text.empty() || (r.kind == SK_EDIT && text == s.classicOwn && !bKeep))
        return StudioRevertField(ST_UNITS, s.rec, "name", sender, pWhy);
      return StudioSetField(ST_UNITS, s.rec, "name", StudioJsonString(text), sender, std::string(), pWhy);
    }
    if (text.empty() || text == s.classicOwn)
      return true;
    PortJson rec = StudioJsonObject();
    StudioJsonPut(rec, "edit", StudioJsonString(s.rec));
    StudioJsonPut(rec, "name", StudioJsonString(text));
    return StudioCreate(ST_UNITS, EditName(s.rec), rec, sender, pWhy);
  }
  case K_SUBTITLE:
    if (text.empty())
      return StudioRevertField(ST_SOUNDS, s.rec, "subtitle", sender, pWhy);
    return StudioSetField(ST_SOUNDS, s.rec, "subtitle", StudioJsonString(text), sender, std::string(), pWhy);
  case K_LABEL: {
    // the game's string again when the text is empty or is the game's
    const int t = LabelTable(s);
    if (text.empty() || (text == s.classicOwn && !bKeep))
      return StudioRevertField(t, s.rec, "name", sender, pWhy);
    return StudioSetField(t, s.rec, "name", StudioJsonString(text), sender, std::string(), pWhy);
  }
  case K_BUTTON:
  case K_DISABLED: {
    const CardChange c = CardChangeFor(s, text, bKeep);
    if (!c.why.empty()) {
      *pWhy = c.why;
      return false;
    }
    if (c.bNothing)
      return true;
    PortJson list = CardList(s.rec);
    if (c.bRemove)
      list.items.erase(list.items.begin() + c.nIndex);
    else if (c.nIndex >= 0)
      list.items[static_cast<size_t>(c.nIndex)] = c.entry;
    else
      list.items.push_back(c.entry);
    if (list.items.empty()) {
      // the card's key taken away when it is empty, and "cards" when that is
      if (!StudioPut("types", {"cards", s.rec}, PortJson(), sender, label, pWhy))
        return false;
      if (StudioDoc("types")["cards"].size() == 0)
        return StudioPut("types", {"cards"}, PortJson(), sender, label, pWhy);
      return true;
    }
    return StudioPut("types", {"cards", s.rec}, list, sender, label, pWhy);
  }
  default:
    *pWhy = s.id + " is read only: " + s.note;
    return false;
  }
}

// The same change asked of the commands, as the modder's: what the panel does.
void Ask(const Str &s, const std::string &text) {
  if (!s.bEdit)
    return;
  switch (s.nKind) {
  case K_MOD:
    if (s.rec == "name" && text.empty()) {
      StudioSay("a mod's name is not empty");
      return;
    }
    StudioAsk("studio_put", "\"doc\":\"mod\",\"path\":[" + Q(s.rec) + "]" + (text.empty() ? std::string() : ",\"value\":" + Q(text)));
    return;
  case K_RACE:
    StudioAsk("studio_race_set", "\"race\":" + Q(s.rec) + ",\"field\":\"name\",\"value\":" + (text.empty() ? std::string("null") : Q(text)));
    return;
  case K_NAME: {
    StudioRecord r;
    if (StudioRecordOf(ST_UNITS, s.rec, &r) && (r.kind == SK_OWN || r.kind == SK_EDIT)) {
      if (text.empty() || (r.kind == SK_EDIT && text == s.classicOwn))
        StudioAsk("studio_set", "\"table\":\"units\",\"name\":" + Q(s.rec) + ",\"field\":\"name\",\"revert\":true");
      else
        StudioAsk("studio_set", "\"table\":\"units\",\"name\":" + Q(s.rec) + ",\"field\":\"name\",\"value\":" + Q(text));
      return;
    }
    if (text.empty() || text == s.classicOwn)
      return;
    const std::string name = EditName(s.rec);
    StudioAsk("studio_create", "\"table\":\"units\",\"name\":" + Q(name) + ",\"record\":{\"edit\":" + Q(s.rec) + ",\"name\":" + Q(text) + "}");
    StudioSay("the classic " + s.classicOwn + " is renamed by an edit of it, " + name);
    return;
  }
  case K_SUBTITLE: {
    const std::string at = "\"table\":\"sounds\",\"name\":" + Q(s.rec) + ",\"field\":\"subtitle\"";
    StudioAsk("studio_set", at + (text.empty() ? std::string(",\"revert\":true") : ",\"value\":" + Q(text)));
    return;
  }
  case K_LABEL: {
    const std::string at = "\"table\":" + Q(StudioTableName(LabelTable(s))) + ",\"name\":" + Q(s.rec) + ",\"field\":\"name\"";
    if (text.empty() || text == s.classicOwn)
      StudioAsk("studio_set", at + ",\"revert\":true");
    else
      StudioAsk("studio_set", at + ",\"value\":" + Q(text));
    return;
  }
  case K_BUTTON:
  case K_DISABLED: {
    const CardChange c = CardChangeFor(s, text, false);
    if (!c.why.empty()) {
      StudioSay(c.why);
      return;
    }
    if (c.bNothing)
      return;
    const std::string unit = "\"unit\":" + Q(s.rec);
    if (c.bRemove)
      StudioAsk("studio_card_set", unit + ",\"index\":" + std::to_string(c.nIndex) + ",\"remove\":true");
    else if (c.nIndex >= 0)
      StudioAsk("studio_card_set", unit + ",\"index\":" + std::to_string(c.nIndex) + ",\"button\":" + PortJsonWrite(c.entry));
    else
      StudioAsk("studio_card_set", unit + ",\"button\":" + PortJsonWrite(c.entry));
    return;
  }
  default:
    return;
  }
}

// ---- find and replace -------------------------------------------------------------------------------------

struct Change {
  const Str *p;
  std::string after;
  int nCount;
};

// The shown strings a find changes, the editable ones; those it finds that cannot change counted.
std::vector<Change> Changes(const std::vector<const Str *> &shown, const std::string &find, const std::string &repl, bool bCase,
                            int *pnReadOnly) {
  std::vector<Change> out;
  *pnReadOnly = 0;
  if (find.empty())
    return out;
  for (const Str *p : shown) {
    int n = 0;
    std::string after = ReplaceIn(p->text, find, repl, bCase, &n);
    if (n == 0)
      continue;
    if (!p->bEdit) {
      ++*pnReadOnly;
      continue;
    }
    if (p->nKind == K_MOD && p->rec == "name" && after.empty())
      continue;
    out.push_back({p, after, n});
  }
  return out;
}

// The changes made as one group: undone whole when one is refused.
bool ReplaceAll(const std::vector<Change> &changes, const std::string &find, const std::string &repl, const std::string &sender,
                std::string *pWhy) {
  if (changes.empty())
    return true;
  // the ids first: each change finds its string again, as the list is gathered again after each edit
  std::vector<std::pair<std::string, std::string>> todo;
  for (const Change &c : changes)
    todo.push_back({c.p->id, c.after});
  const int nAt = StudioHistoryAt();
  StudioGroupBegin(sender, "replaced \"" + find + "\" with \"" + repl + "\" in " + std::to_string(todo.size()) +
                               (todo.size() == 1 ? " string" : " strings"));
  bool bOk = true;
  for (const auto &t : todo) {
    const Str *p = Find(t.first);
    if (p == nullptr) {
      *pWhy = "no string " + t.first;
      bOk = false;
      break;
    }
    const Str s = *p;
    if (!Execute(s, t.second, sender, true, pWhy)) {
      *pWhy = s.id + ": " + *pWhy;
      bOk = false;
      break;
    }
  }
  StudioGroupEnd();
  if (!bOk && StudioHistoryAt() > nAt) {
    std::string why;
    StudioUndo(&why);
  }
  return bOk;
}

// ---- the commands --------------------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

const char *const kCellIds[] = {"missing", "done", "stale", "empty"};

// A row's key and its translation in each of the mod's languages, for its JSON.
std::string TranslationsJson(const Str &s) {
  if (!Translatable(s))
    return std::string();
  std::string out = ",\"key\":" + Q(s.tkey) + ",\"translations\":{";
  bool bFirst = true;
  for (const std::string &code : studio_languages::Languages()) {
    const studio_languages::Cell c = LangCell(s, code);
    out += (bFirst ? "" : ",") + Q(code) + ":{\"text\":" + Q(c.text) + ",\"state\":" + Q(kCellIds[c.nState]) +
           (c.from.empty() ? std::string() : ",\"from\":" + Q(c.from)) + "}";
    bFirst = false;
  }
  return out + "}";
}

std::string StrJson(const Str &s) {
  return "{\"id\":" + Q(s.id) + ",\"kind\":" + Q(kKindIds[s.nKind]) + ",\"where\":" + Q(s.where) + ",\"place\":" + Q(s.place) +
         ",\"text\":" + Q(s.text) + ",\"source\":" + Q(kSrcIds[s.nSource]) + ",\"from\":" + Q(s.from) + ",\"classic\":" + Q(s.classic) +
         ",\"hotkey\":" + Q(s.cKey ? std::string(1, s.cKey) : std::string()) + ",\"editable\":" + (s.bEdit ? "true" : "false") +
         ",\"changed\":" + (s.bChanged ? "true" : "false") + ",\"note\":" + Q(s.note) + TranslationsJson(s) + "}";
}

bool FilterOf(const PortJson &j, Filter *pf, std::string *pWhy) {
  pf->query = j["query"].Str();
  pf->nKinds = KindsOf(j["kind"].Str(), pWhy);
  pf->bMine = j["mine"].kind == PortJson::BOOL && j["mine"].b;
  pf->bChanged = j["changed"].kind == PortJson::BOOL && j["changed"].b;
  if (j["missing"].kind == PortJson::STRING && !j["missing"].s.empty()) {
    for (const std::string &code : studio_languages::Languages())
      if (Lower(code) == Lower(j["missing"].s) || Lower(code.substr(0, 2)) == Lower(j["missing"].s))
        pf->missing = code;
    if (pf->missing.empty()) {
      *pWhy = "the mod has no language " + j["missing"].s + "; studio_languages lists them";
      return false;
    }
  }
  return pf->nKinds >= 0;
}

void ShowFilter(const Filter &f);

// studio_strings {query?, kind?, mine?, changed?, missing?, show?}: the strings, those the filters leave (missing,
// a language: those missing or stale in it), each with its key and translations; show
// puts the same filters on the workspace and shows it.
bool CmdStrings(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  Filter f;
  if (!FilterOf(j, &f, pAnswer))
    return false;
  if (j["show"].kind == PortJson::BOOL && j["show"].b)
    ShowFilter(f);
  const std::vector<Str> &all = Strings();
  std::string list;
  int n = 0;
  for (const Str &s : all)
    if (Shown(s, f)) {
      list += (n ? "," : "") + StrJson(s);
      ++n;
    }
  *pAnswer = "\"count\":" + std::to_string(n) + ",\"total\":" + std::to_string(all.size()) + ",\"game_strings\":" +
             (sgpStatStrs != NULL ? "true" : "false") + ",\"strings\":[" + list + "]";
  return true;
}

// studio_string_set {id, text}: one string set, as one edit; text null or "" gives a classic one its
// classic words back (and a unit's name its record's).
bool CmdStringSet(const PortJson &j, std::string *pAnswer) {
  const Str *p = Find(j["id"].Str());
  if (p == nullptr) {
    *pAnswer = "no string " + j["id"].Str() + "; studio_strings lists them with their ids";
    return false;
  }
  if (j["text"].kind != PortJson::STRING && j["text"].kind != PortJson::NULLV) {
    *pAnswer = "studio_string_set needs text, a string (or null for the classic words)";
    return false;
  }
  const Str s = *p;
  const std::string sender = Sender(j);
  std::string why;
  StudioGroupBegin(sender, "set " + Describe(s));
  const bool bOk = Execute(s, j["text"].Str(), sender, false, &why);
  StudioGroupEnd();
  if (!bOk) {
    *pAnswer = why;
    return false;
  }
  const Str *pNow = Find(s.id);
  *pAnswer = "\"string\":" + (pNow ? StrJson(*pNow) : std::string("null")) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// studio_strings_replace {find, replace, case?, query?, kind?, mine?, changed?, preview?}: every find
// in the strings the filters leave replaced, as one edit; preview answers the changes and makes none.
bool CmdReplace(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const std::string find = j["find"].Str(), repl = j["replace"].Str();
  if (find.empty()) {
    *pAnswer = "studio_strings_replace needs find";
    return false;
  }
  Filter f;
  if (!FilterOf(j, &f, pAnswer))
    return false;
  std::vector<const Str *> shown;
  for (const Str &s : Strings())
    if (Shown(s, f))
      shown.push_back(&s);
  const bool bCase = j["case"].kind == PortJson::BOOL && j["case"].b;
  int nReadOnly = 0;
  const std::vector<Change> changes = Changes(shown, find, repl, bCase, &nReadOnly);
  std::string list;
  int nFinds = 0;
  for (const Change &c : changes) {
    list += (list.empty() ? "" : ",") + std::string("{\"id\":") + Q(c.p->id) + ",\"kind\":" + Q(kKindIds[c.p->nKind]) + ",\"where\":" +
            Q(c.p->where) + ",\"before\":" + Q(c.p->text) + ",\"after\":" + Q(c.after) + ",\"finds\":" + std::to_string(c.nCount) + "}";
    nFinds += c.nCount;
  }
  const bool bPreview = j["preview"].kind == PortJson::BOOL && j["preview"].b;
  const std::string head = "\"changes\":[" + list + "],\"count\":" + std::to_string(changes.size()) + ",\"finds\":" + std::to_string(nFinds) +
                           ",\"read_only\":" + std::to_string(nReadOnly);
  if (bPreview) {
    *pAnswer = head + ",\"done\":false";
    return true;
  }
  std::string why;
  if (!ReplaceAll(changes, find, repl, Sender(j), &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = head + ",\"done\":true,\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// ---- the workspace's state -------------------------------------------------------------------------------

const char *const kKindSeg[] = {"Every kind", "Unit names", "Labels", "Buttons", "When it cannot", "Races", "Mod page", "Subtitles"};
const char *const kSrcSeg[] = {"Everything", "The mod's", "Changed"};

char g_szQ[96];
int g_nKindSeg = 0;  // 0 every kind, else the kind + 1
int g_nSrc = 0;      // everything, the mod's, changed
bool g_bReplace = false;
char g_szFind[96], g_szRepl[96];
bool g_bCase = false;
bool g_bFocusSearch = false, g_bFocusFind = false;  // Ctrl+F's field, focused once it is drawn
std::string g_editId;  // the string being edited in place
char g_szEdit[1024];
bool g_bEditFocus = false;
bool g_bMultiOpen = false;  // its editor is the box for texts of more than one line
bool g_bMultiAsk = false;   // the box to be opened
ImVec2 g_multiAt;
std::string g_nextEdit;  // the string to edit next, once this frame's table is drawn (Tab)
std::string g_missingIn; // the language whose missing and stale strings alone are shown, "" for none
std::string g_editLang;  // the language of the translation being edited, "" for the mod's own text
std::string g_nextLang;  // and of the one to edit next (Tab)

void ShowFilter(const Filter &f) {
  snprintf(g_szQ, sizeof g_szQ, "%s", f.query.c_str());
  g_nKindSeg = 0;
  for (int i = 0; i < KINDS; ++i)
    if (f.nKinds == (1 << i))
      g_nKindSeg = i + 1;
  g_nSrc = f.bChanged ? 2 : f.bMine ? 1 : 0;
  g_missingIn = f.missing;
  StudioWorkspaceSet(9);
}

Filter UiFilter(void) {
  Filter f;
  f.query = g_szQ;
  f.nKinds = g_nKindSeg == 0 ? (1 << KINDS) - 1 : 1 << (g_nKindSeg - 1);
  f.bMine = g_nSrc == 1;
  f.bChanged = g_nSrc == 2;
  // a language taken away leaves no filter
  const std::vector<std::string> langs = studio_languages::Languages();
  if (std::find(langs.begin(), langs.end(), g_missingIn) == langs.end())
    g_missingIn.clear();
  f.missing = g_missingIn;
  return f;
}

void StartEdit(const Str &s, ImVec2 at) {
  g_editId = s.id;
  g_editLang.clear();
  snprintf(g_szEdit, sizeof g_szEdit, "%s", s.text.c_str());
  g_bEditFocus = true;
  g_bMultiOpen = s.bMulti;
  g_bMultiAsk = s.bMulti;
  g_multiAt = at;
}

// A translation edited in place: its text, or nothing yet for a missing one.
void StartEditLang(const Str &s, const std::string &code, ImVec2 at) {
  g_editId = s.id;
  g_editLang = code;
  snprintf(g_szEdit, sizeof g_szEdit, "%s", LangCell(s, code).text.c_str());
  g_bEditFocus = true;
  g_bMultiOpen = s.bMulti;
  g_bMultiAsk = s.bMulti;
  g_multiAt = at;
}

// A translation typed: set when it changed, or set again to say a stale one still holds.
void AskLang(const Str &s, const std::string &code, const std::string &typed) {
  const studio_languages::Cell c = LangCell(s, code);
  if (typed != c.text || (c.nState == studio_languages::CELL_STALE && !typed.empty()))
    studio_languages::AskSet(s.tkey, code, typed);
}

// Where a string goes when it is opened: its record, its card, its race, the mod's page.
void OpenOf(const Str &s) {
  switch (s.nKind) {
  case K_NAME:
    StudioOpenDoc(ST_UNITS, s.rec, 0);
    return;
  case K_LABEL:
    StudioOpenDoc(LabelTable(s), s.rec, 0);
    return;
  case K_BUTTON:
  case K_DISABLED:
    studio_races::CardsShow(s.rec);
    StudioWorkspaceSet(5);
    return;
  case K_RACE:
    studio_races::ShowRace(s.rec);
    StudioWorkspaceSet(5);
    return;
  case K_MOD:
    StudioWorkspaceSet(8);
    return;
  default:
    return;
  }
}

// ---- drawing --------------------------------------------------------------------------------------------

// A text in a row: a button's hotkey letter bright, each new line a small return mark; its width.
float DrawText(ImDrawList *d, float x, float y, const std::string &text, float fs, float maxw, ImU32 col, bool bMarks, ImU32 markCol) {
  float cx = x;
  const float lim = x + maxw;
  std::string run;
  // a run cut short ends the text there
  auto flush = [&](void) {
    if (!run.empty() && cx < lim) {
      const bool bCut = TW(STUDIO_FONT_TEXT, fs, run) > lim - cx;
      cx += T(d, STUDIO_FONT_TEXT, fs, cx, y, col, run, lim - cx);
      if (bCut)
        cx = lim;
    }
    run.clear();
  };
  for (size_t i = 0; i < text.size() && cx < lim; ++i) {
    if (text[i] == '\n') {
      flush();
      // a return mark: a short hook, then on along the line
      const float h = P(fs), mx = cx + P(3.0f), my = y + h * 0.62f;
      d->AddLine(ImVec2(mx + P(7.0f), y + h * 0.25f), ImVec2(mx + P(7.0f), my), C(TK_INK3), P(1.0f));
      d->AddLine(ImVec2(mx + P(7.0f), my), ImVec2(mx, my), C(TK_INK3), P(1.0f));
      d->AddLine(ImVec2(mx, my), ImVec2(mx + P(2.5f), my - P(2.5f)), C(TK_INK3), P(1.0f));
      d->AddLine(ImVec2(mx, my), ImVec2(mx + P(2.5f), my + P(2.5f)), C(TK_INK3), P(1.0f));
      cx += P(14.0f);
      // the spaces a following line starts with, shown as one
      while (i + 1 < text.size() && text[i + 1] == ' ')
        ++i;
      continue;
    }
    if (bMarks && text[i] == '<' && i + 2 < text.size() && text[i + 2] == '>') {
      flush();
      cx += T(d, STUDIO_FONT_BOLD, fs, cx, y, markCol, std::string(1, text[i + 1]));
      i += 2;
      continue;
    }
    run += text[i];
  }
  flush();
  return cx - x;
}

void SourceChip(ImDrawList *d, ImVec2 p, const Str &s) {
  const int kind = s.nSource == SRC_MOD ? CH_GOOD : s.nSource == SRC_DEP ? CH_GOLD : CH_PLAIN;
  // an unsaved change is the dot by its text
  StudioChip(d, p, SourceWords(s).c_str(), kind);
}

// A language's count as a chip: what is missing, else done; its width.
float LangChip(ImDrawList *d, ImVec2 p, const studio_languages::Count &c, bool bDraw) {
  const std::string words = c.nMissing > 0 ? std::to_string(c.nMissing) + " missing"
                            : c.nStale > 0 ? std::to_string(c.nStale) + " stale"
                                           : std::string("complete");
  if (!bDraw)
    return StudioChipW(words.c_str());
  return StudioChip(d, p, words.c_str(), c.nMissing > 0 ? CH_WARN : c.nStale > 0 ? CH_GOLD : CH_GOOD);
}

// The languages: each a menu with its count (what is missing shown, a translator's file written or
// read, the language taken away), a language added, the filter on, and the test game's language.
void LangBar(ImDrawList *d, ImVec2 a, float W, float h) {
  namespace sl = studio_languages;
  studio_languages::Poll();
  const float by = a.y + (h - P(26.0f)) / 2.0f;
  float x = a.x;
  x += Wide(d, 9.5f, x, Mid(a.y, h, 9.5f), C(TK_LABEL), "Languages", 0.1f) + P(14.0f);
  const std::vector<std::string> langs = sl::Languages();
  if (langs.empty())
    x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_INK3),
           "the mod's own text alone, in " + sl::Name(sl::SourceLanguage())) + P(14.0f);
  for (const std::string &code : langs) {
    const sl::Count c = sl::CountOf(code);
    const std::string name = sl::Name(code);
    const float cw = LangChip(d, ImVec2(), c, false);
    const float w = TW(STUDIO_FONT_TEXT, kStudioFsS, name) + cw + P(52.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, by));
    ImDrawList *pd = ImGui::GetWindowDrawList();
    const bool bOpen = StudioSelectBegin(("##lang" + code).c_str(), name.c_str(), w / StudioPx(1.0f), 26.0f);
    LangChip(pd, ImVec2(x + w - P(28.0f) - cw, by + P(4.0f)), c, true);
    if (g_missingIn == code)
      pd->AddRect(ImVec2(x - P(1.0f), by - P(1.0f)), ImVec2(x + w + P(1.0f), by + P(27.0f)), C(TK_SEL_LINE), P(3.0f), 0, P(1.5f));
    if (bOpen) {
      const std::string counts = std::to_string(c.nDone) + " of " + std::to_string(c.nNeeded) + " translated" +
                                 (c.nStale ? ", " + std::to_string(c.nStale) + " of them stale" : std::string());
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), ("  " + name + " (" + code + "): " + counts).c_str());
      if (StudioItem(("Show what is missing or stale in " + name).c_str(), g_missingIn == code))
        g_missingIn = g_missingIn == code ? std::string() : code;
      StudioItemSeparator();
      if (StudioItem("Translator's file: CSV, for a spreadsheet..."))
        sl::AskExport(code, "csv");
      if (StudioItem("Translator's file: PO, for translation tools..."))
        sl::AskExport(code, "po");
      if (StudioItem("Read a translator's file back in..."))
        sl::AskImport(code);
      StudioItemSeparator();
      if (StudioItem(("Take " + name + " away").c_str())) {
        if (g_missingIn == code)
          g_missingIn.clear();
        sl::AskRemove(code);
      }
      StudioSelectEnd();
    }
    x += w + P(8.0f);
  }
  // a language added: Hyperion's that the mod has not, and not its own text's
  ImGui::SetCursorScreenPos(ImVec2(x, by));
  if (StudioSelectBegin("##langadd", "Add a language", 150.0f, 26.0f)) {
    for (int i = 0; i < g_nPortLanguages; ++i) {
      const std::string code = g_apszPortLanguages[i];
      if (code == sl::SourceLanguage() || std::find(langs.begin(), langs.end(), code) != langs.end())
        continue;
      if (StudioItem((sl::Name(code) + "   " + code).c_str()))
        sl::AskAdd(code);
    }
    StudioSelectEnd();
  }
  x += P(158.0f);
  if (!g_missingIn.empty()) {
    const std::string words = "Missing in " + sl::Name(g_missingIn);
    ImGui::SetCursorScreenPos(ImVec2(x + P(6.0f), by));
    if (StudioButton("##langoff", words.c_str(), "x", SB_ON, 26.0f))
      g_missingIn.clear();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Only the strings missing or stale in %s are shown; click to show them all", sl::Name(g_missingIn).c_str());
  }
  // the test game's language, on the right
  const std::string game = sl::GameLanguage().empty() ? std::string("the mod's own text") : sl::Name(sl::GameLanguage());
  const float gw = std::max(P(170.0f), TW(STUDIO_FONT_TEXT, kStudioFsS, game) + P(44.0f));
  const std::string label = "Test game in";
  const float lw = TW(STUDIO_FONT_TEXT, kStudioFsS, label);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + W - gw - lw - P(8.0f), Mid(a.y, h, kStudioFsS), C(TK_INK2), label);
  ImGui::SetCursorScreenPos(ImVec2(a.x + W - gw, by));
  if (StudioSelectBegin("##langgame", game.c_str(), gw / StudioPx(1.0f), 26.0f)) {
    if (StudioItem(("The mod's own text, " + sl::Name(sl::SourceLanguage())).c_str(), sl::GameLanguage().empty()))
      sl::AskGame(std::string());
    for (const std::string &code : langs)
      if (StudioItem(sl::Name(code).c_str(), sl::GameLanguage() == code))
        sl::AskGame(code);
    StudioSelectEnd();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The language the test game and a race's Play it show the mod's text in");
}

// Three rows: the search, what the strings come from and the count; the kinds, and find and replace;
// the languages.
float Bar(ImDrawList *d, ImVec2 a, float W, int nShown, int nAll) {
  const float h = P(30.0f), gap = P(8.0f);
  const float sw = StudioSegWidth(kSrcSeg, 3);
  const std::string count = "showing " + std::to_string(nShown) + " of " + std::to_string(nAll);
  const float cw = TW(STUDIO_FONT_TEXT, kStudioFsS, count);
  const float fw = std::max(P(160.0f), std::min(P(420.0f), W - sw - cw - P(40.0f)));
  ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + (h - P(28.0f)) / 2.0f));
  if (g_bFocusSearch) {
    ImGui::SetKeyboardFocusHere();
    g_bFocusSearch = false;
  }
  StudioInput("##q", g_szQ, sizeof g_szQ, fw / StudioPx(1.0f), 28.0f, "Search the texts and where they are", "search",
              g_bReplace ? nullptr : "Ctrl F");
  float x = a.x + fw + P(14.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(26.0f)) / 2.0f));
  g_nSrc = StudioSeg("##src", kSrcSeg, 3, g_nSrc);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + W - cw, Mid(a.y, h, kStudioFsS), C(TK_INK3), count);
  const float y2 = a.y + h + gap;
  ImGui::SetCursorScreenPos(ImVec2(a.x, y2 + (h - P(26.0f)) / 2.0f));
  g_nKindSeg = StudioSeg("##kind", kKindSeg, 8, g_nKindSeg);
  const float bw = StudioButtonSize("Find and replace", "pen", 26.0f).x;
  ImGui::SetCursorScreenPos(ImVec2(a.x + W - bw, y2 + (h - P(26.0f)) / 2.0f));
  if (StudioButton("##repl", "Find and replace", "pen", g_bReplace ? SB_ON : SB_PLAIN, 26.0f))
    g_bReplace = !g_bReplace;
  LangBar(d, ImVec2(a.x, y2 + h + gap), W, h);
  return h * 3.0f + gap * 2.0f;
}

// Find and replace: the find, its replacement, the count, and the button that makes them.
float ReplaceBar(ImDrawList *d, ImVec2 a, float W, const std::vector<Change> &changes, int nReadOnly) {
  const float h = P(40.0f);
  d->AddRectFilled(a, ImVec2(a.x + W, a.y + h), C(TK_SEL), P(3.0f));
  d->AddRect(a, ImVec2(a.x + W, a.y + h), C(TK_SEL_LINE2), P(3.0f));
  float x = a.x + P(12.0f);
  const float y = a.y + (h - P(26.0f)) / 2.0f;
  x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_INK2), "Find") + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (g_bFocusFind) {
    ImGui::SetKeyboardFocusHere();
    g_bFocusFind = false;
  }
  StudioInput("##find", g_szFind, sizeof g_szFind, 220.0f, 26.0f, "a word or a part of one", nullptr, "Ctrl F");
  x += P(228.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), C(TK_INK2), "with") + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  StudioInput("##with", g_szRepl, sizeof g_szRepl, 220.0f, 26.0f, "nothing, to take it out");
  x += P(232.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (h - P(20.0f)) / 2.0f));
  StudioCheck("##case", "Match case", &g_bCase);
  x += P(112.0f);
  int nFinds = 0;
  for (const Change &c : changes)
    nFinds += c.nCount;
  std::string words;
  if (g_szFind[0] == '\0')
    words = "the strings shown, those the search and the filters leave";
  else
    words = std::to_string(nFinds) + (nFinds == 1 ? " find in " : " finds in ") + std::to_string(changes.size()) +
            (changes.size() == 1 ? " string" : " strings") +
            (nReadOnly ? ", and " + std::to_string(nReadOnly) + " read only left as they are" : std::string());
  const std::string go = changes.empty() ? std::string("Replace") : "Replace in " + std::to_string(changes.size());
  const float gw = StudioButtonSize(go.c_str(), nullptr, 26.0f).x;
  const float cw = StudioButtonSize("Close", nullptr, 26.0f).x;
  const float rx = a.x + W - P(10.0f) - cw - P(6.0f) - gw;
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(a.y, h, kStudioFsS), changes.empty() ? C(TK_INK3) : C(TK_HI), words, rx - x - P(10.0f));
  ImGui::SetCursorScreenPos(ImVec2(rx, y));
  if (StudioButton("##go", go.c_str(), nullptr, SB_GOOD, 26.0f, 0.0f, kStudioFsS, !changes.empty())) {
    std::string fields = "\"find\":" + Q(g_szFind) + ",\"replace\":" + Q(g_szRepl) + ",\"case\":" + (g_bCase ? "true" : "false");
    const Filter f = UiFilter();
    fields += ",\"query\":" + Q(f.query) + ",\"mine\":" + (f.bMine ? "true" : "false") + ",\"changed\":" + (f.bChanged ? "true" : "false");
    if (g_nKindSeg > 0)
      fields += ",\"kind\":" + Q(kKindIds[g_nKindSeg - 1]);
    StudioAsk("studio_strings_replace", fields);
    StudioSay("replaced in " + std::to_string(changes.size()) + (changes.size() == 1 ? " string" : " strings") + "; one Undo takes them all back");
    // the find made: the list shows the strings again, the changed ones marked
    g_szFind[0] = '\0';
  }
  ImGui::SetCursorScreenPos(ImVec2(rx + gw + P(6.0f), y));
  if (StudioButton("##close", "Close", nullptr, SB_FLAT, 26.0f))
    g_bReplace = false;
  return h + P(8.0f);
}

// The box for a text of more than one line: Enter keeps it, Ctrl+Enter starts a new line.
void MultiEditor(void) {
  if (g_editId.empty() || !g_bMultiOpen)
    return;
  const Str *p = Find(g_editId);
  if (p == nullptr) {
    g_editId.clear();
    return;
  }
  if (g_bMultiAsk) {
    ImGui::OpenPopup("##multi");
    g_bMultiAsk = false;
  }
  ImGui::SetNextWindowPos(g_multiAt, ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(12.0f), P(10.0f)));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, C(TK_MENU));
  ImGui::PushStyleColor(ImGuiCol_Border, C(TK_MENU_RIM));
  if (ImGui::BeginPopup("##multi")) {
    ImDrawList *d = ImGui::GetWindowDrawList();
    const Str s = *p;
    const bool bLang = !g_editLang.empty();
    StudioLabel(STUDIO_FONT_BOLD, kStudioFsS, C(TK_HI),
                ((bLang ? studio_languages::Name(g_editLang) + ": " : std::string()) + s.where + ", " + s.place).c_str());
    ImGui::Dummy(ImVec2(1.0f, P(4.0f)));
    StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
    if (g_bEditFocus) {
      ImGui::SetKeyboardFocusHere();
      g_bEditFocus = false;
    }
    const bool bDone = ImGui::InputTextMultiline("##mtext", g_szEdit, sizeof g_szEdit, ImVec2(P(520.0f), P(96.0f)),
                                                 ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_EnterReturnsTrue);
    const bool bEscape = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(1.0f, P(2.0f)));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "Enter keeps it, Ctrl+Enter starts a new line, Esc leaves it");
    // what it stands in for: the classic text, or for a translation the mod's own
    const std::string &shown = bLang ? s.text : s.classicOwn;
    if (!shown.empty()) {
      ImGui::Dummy(ImVec2(1.0f, P(4.0f)));
      const ImVec2 q = ImGui::GetCursorScreenPos();
      const float lw = T(d, STUDIO_FONT_TEXT, kStudioFsXs, q.x, q.y + P(1.0f), C(TK_INK3), bLang ? "the mod's: " : "classic: ");
      DrawText(d, q.x + lw, q.y + P(1.0f), shown, kStudioFsXs, P(520.0f) - lw, C(TK_INK2), true, C(TK_INK));
      ImGui::Dummy(ImVec2(P(520.0f), P(16.0f)));
    }
    ImGui::Dummy(ImVec2(1.0f, P(6.0f)));
    const bool bKeep = StudioButton("##mkeep", "Keep", "done", SB_GOOD, 26.0f);
    ImGui::SameLine(0.0f, P(6.0f));
    const bool bCancel = StudioButton("##mcancel", "Leave it", nullptr, SB_FLAT, 26.0f);
    bool bBack = false;
    if (!bLang && !s.classicOwn.empty() && s.nSource == SRC_MOD) {
      ImGui::SameLine(0.0f, P(6.0f));
      bBack = StudioButton("##mback", "Back to the classic words", "undo", SB_PLAIN, 26.0f);
    }
    if ((bDone || bKeep) && bLang) {
      AskLang(s, g_editLang, g_szEdit);
      g_editId.clear();
      ImGui::CloseCurrentPopup();
    } else if (bDone || bKeep) {
      if (std::string(g_szEdit) != s.text)
        Ask(s, g_szEdit);
      g_editId.clear();
      ImGui::CloseCurrentPopup();
    } else if (bBack) {
      Ask(s, std::string());
      g_editId.clear();
      ImGui::CloseCurrentPopup();
    } else if (bEscape || bCancel) {
      g_editId.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  } else {
    // closed by a click outside it: what was typed is left
    g_editId.clear();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

// the workspace drawn, below
void StudioStringsDraw(ImVec2 a, ImVec2 b);

}  // namespace

void StudioStringsWorkspace(ImVec2 a, ImVec2 b) {
  // the workspace's ids apart from the shell's, drawn in the same window (its Explorer has a ##find too)
  ImGui::PushID("##strings");
  StudioStringsDraw(a, b);
  ImGui::PopID();
}

void StudioStringsFocusFind(void) {
  if (g_bReplace)
    g_bFocusFind = true;
  else
    g_bFocusSearch = true;
}

namespace {

void StudioStringsDraw(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = b.x - a.x - P(32.0f);
  const ImVec2 o(a.x + P(16.0f), a.y + P(12.0f));
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, o.y + P(10.0f), C(TK_INK3), "Open a mod to see its strings.");
    return;
  }
  unsigned nVersion = 0;
  const std::vector<Str> &all = Strings(&nVersion);
  // the strings shown, kept until the list or the filters change
  static std::vector<const Str *> s_shown;
  static unsigned s_nVersion = ~0u;
  static std::string s_key;
  const Filter f = UiFilter();
  const std::string key = f.query + "\x1f" + std::to_string(f.nKinds) + (f.bMine ? "m" : "") + (f.bChanged ? "c" : "") + "\x1f" + f.missing;
  if (s_nVersion != nVersion || s_key != key) {
    s_nVersion = nVersion;
    s_key = key;
    s_shown.clear();
    for (const Str &s : all)
      if (Shown(s, f))
        s_shown.push_back(&s);
  }
  int nReadOnly = 0;
  const bool bPreview = g_bReplace && g_szFind[0] != '\0';
  const std::vector<Change> changes = g_bReplace ? Changes(s_shown, g_szFind, g_szRepl, g_bCase, &nReadOnly) : std::vector<Change>();
  std::map<const Str *, const Change *> after;
  for (const Change &c : changes)
    after[c.p] = &c;

  float y = o.y;
  y += Bar(d, ImVec2(o.x, y), W, static_cast<int>(s_shown.size()), static_cast<int>(all.size())) + P(10.0f);
  if (g_bReplace)
    y += ReplaceBar(d, ImVec2(o.x, y), W, changes, nReadOnly);
  if (sgpStatStrs == NULL) {
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, o.x, y, C(TK_WARN),
      "The game's strings are not read yet: the classic buttons' words come once the game is up.");
    y += P(20.0f);
  }

  // the rows: the mod's first, then the classic ones, each part in the sort's order; with a find to
  // replace, only those it changes
  std::vector<const Str *> mods, classic;
  for (const Str *p : s_shown) {
    if (bPreview && !after.count(p))
      continue;
    (p->nSource == SRC_CLASSIC ? classic : mods).push_back(p);
  }

  const float footH = P(30.0f);
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBodyUntilResize;
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(P(8.0f), P(5.0f)));
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, C(TK_PANE2));
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, C(TK_ROW));
  enum { COL_KIND, COL_WHERE, COL_TEXT, COL_KEY, COL_SOURCE, COL_OTHER, COL_LANG };
  // a column for each of the mod's languages, after the classic text
  const std::vector<std::string> langs = studio_languages::Languages();
  const int nLangs = static_cast<int>(langs.size());
  const int COL_ACT = COL_LANG + nLangs, COLS = COL_ACT + 1;
  const int ID_ACT = 999;
  std::vector<std::string> langHeads;
  for (const std::string &code : langs) {
    const studio_languages::Count c = studio_languages::CountOf(code);
    langHeads.push_back(studio_languages::Name(code) + (c.nMissing ? "  (" + std::to_string(c.nMissing) + " missing)" : std::string()));
  }
  std::vector<const Str *> order;  // the rows as drawn, for Tab
  if (ImGui::BeginTable(("##strings" + std::to_string(nLangs)).c_str(), COLS, flags, ImVec2(W, b.y - y - footH - P(8.0f)))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_None, P(112.0f), COL_KIND);
    ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_None, P(nLangs ? 220.0f : 290.0f), COL_WHERE);
    ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch, nLangs ? 2.4f : 3.0f, COL_TEXT);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_NoSort, P(36.0f), COL_KEY);
    ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_None, P(128.0f), COL_SOURCE);
    ImGui::TableSetupColumn(bPreview ? "After the replace" : "Classic text", ImGuiTableColumnFlags_WidthStretch, nLangs ? 0.9f : 2.0f, COL_OTHER);
    for (int li = 0; li < nLangs; ++li)
      ImGui::TableSetupColumn(langHeads[static_cast<size_t>(li)].c_str(), ImGuiTableColumnFlags_WidthStretch, 2.4f,
                              static_cast<ImGuiID>(COL_LANG + li));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize, P(60.0f), ID_ACT);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs *pSpecs = ImGui::TableGetSortSpecs()) {
      if (pSpecs->SpecsCount > 0) {
        const ImGuiTableColumnSortSpecs &s = pSpecs->Specs[0];
        const bool bDown = s.SortDirection == ImGuiSortDirection_Descending;
        auto keyOf = [&](const Str *p) -> std::string {
          switch (s.ColumnUserID) {
          case COL_KIND: return kKindWords[p->nKind];
          case COL_WHERE: return Lower(p->where + " " + p->place);
          case COL_TEXT: return Lower(Plain(p->text));
          case COL_SOURCE: return SourceWords(*p);
          case COL_OTHER: return Lower(Plain(bPreview && after.count(p) ? after[p]->after : p->classic));
          default:
            if (static_cast<int>(s.ColumnUserID) >= COL_LANG && static_cast<int>(s.ColumnUserID) < COL_LANG + nLangs)
              return Lower(Plain(LangCell(*p, langs[s.ColumnUserID - COL_LANG]).text));
            return std::string();
          }
        };
        auto sort = [&](std::vector<const Str *> &v) {
          std::vector<std::pair<std::string, const Str *>> keyed;
          for (const Str *p : v)
            keyed.push_back({keyOf(p), p});
          std::stable_sort(keyed.begin(), keyed.end(), [&](const std::pair<std::string, const Str *> &x, const std::pair<std::string, const Str *> &y2) {
            return bDown ? x.first > y2.first : x.first < y2.first;
          });
          for (size_t i = 0; i < v.size(); ++i)
            v[i] = keyed[i].second;
        };
        sort(mods);
        sort(classic);
      }
    }
    struct Item {
      const Str *p;
      const char *pszHead;
    };
    std::vector<Item> items;
    const std::string modHead = "In the mod: " + std::to_string(mods.size()) + (mods.size() == 1 ? " string" : " strings") +
                                ", its own and what it builds on";
    const std::string clsHead = "Classic: " + std::to_string(classic.size()) + (classic.size() == 1 ? " string" : " strings") +
                                "; a text given here becomes the mod's";
    items.push_back({nullptr, modHead.c_str()});
    for (const Str *p : mods) {
      items.push_back({p, nullptr});
      order.push_back(p);
    }
    static const char *const kNone = "  Nothing here: no string the search and the filters leave.";
    static const char *const kNoFind = "  Nothing the find changes among the strings shown.";
    if (mods.empty() && classic.empty())
      items.push_back({nullptr, bPreview ? kNoFind : kNone});
    if (!classic.empty()) {
      items.push_back({nullptr, clsHead.c_str()});
      for (const Str *p : classic) {
        items.push_back({p, nullptr});
        order.push_back(p);
      }
    }
    const float rowH = P(30.0f);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(items.size()), rowH);
    // the row being edited is drawn whatever the scroll, so its field keeps the keyboard
    int nEditItem = -1;
    for (size_t i = 0; i < items.size() && !g_editId.empty(); ++i)
      if (items[i].p && items[i].p->id == g_editId && !g_bMultiOpen)
        nEditItem = static_cast<int>(i);
    if (nEditItem >= 0)
      clip.IncludeItemByIndex(nEditItem);
    while (clip.Step()) {
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const Item &it = items[static_cast<size_t>(i)];
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
        if (it.p == nullptr) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, C(TK_PANE2));
          ImGui::TableSetColumnIndex(0);
          const ImVec2 p = ImGui::GetCursorScreenPos();
          ImDrawList *td = ImGui::GetWindowDrawList();
          td->PushClipRect(ImVec2(p.x, p.y - P(5.0f)), ImVec2(p.x + W, p.y + rowH), false);
          T(td, STUDIO_FONT_BOLD, kStudioFsS, p.x, Mid(p.y - P(5.0f), rowH, kStudioFsS), C(TK_LABEL), it.pszHead);
          td->PopClipRect();
          continue;
        }
        const Str &s = *it.p;
        ImGui::PushID(s.id.c_str());
        ImDrawList *td = ImGui::GetWindowDrawList();
        const bool bRowHover = ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex();
        const bool bButtons = s.nKind == K_BUTTON;
        // the kind
        ImGui::TableSetColumnIndex(COL_KIND);
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          StudioGlyph(td, kKindGlyphs[s.nKind], ImVec2(p.x, p.y + (rowH - P(10.0f) - P(14.0f)) / 2.0f), P(14.0f), C(TK_INK3));
          T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(20.0f), Mid(p.y - P(5.0f), rowH, kStudioFsXs), C(TK_INK2), kKindWords[s.nKind],
            ImGui::GetContentRegionAvail().x - P(20.0f));
        }
        // where it is: the record or card, then the place in it
        ImGui::TableSetColumnIndex(COL_WHERE);
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float cw = ImGui::GetContentRegionAvail().x;
          if (s.nSource == SRC_MOD)
            td->AddRectFilled(ImVec2(p.x - P(8.0f), p.y - P(3.0f)), ImVec2(p.x - P(5.0f), p.y + rowH - P(7.0f)), C(TK_MOD), P(1.5f));
          const float ty = Mid(p.y - P(5.0f), rowH, kStudioFsS);
          const float ww = T(td, s.nSource == SRC_MOD ? STUDIO_FONT_BOLD : STUDIO_FONT_TEXT, kStudioFsS, p.x, ty,
                             s.nSource == SRC_MOD ? C(TK_INK) : C(TK_INK2), s.where, cw);
          if (ww + P(8.0f) < cw)
            T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x + ww + P(8.0f), Mid(p.y - P(5.0f), rowH, kStudioFsXs), C(TK_INK3), s.place,
              cw - ww - P(8.0f));
          ImGui::Dummy(ImVec2(cw, rowH - P(10.0f)));
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s\n%s", s.where.c_str(), s.place.c_str(), s.id.c_str());
        }
        // the text: edited in place, or in its box when it has more than one line
        ImGui::TableSetColumnIndex(COL_TEXT);
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float cw = ImGui::GetContentRegionAvail().x;
          const bool bEditing = g_editId == s.id && g_editLang.empty() && !g_bMultiOpen;
          if (bEditing) {
            ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(3.0f)));
            if (g_bEditFocus) {
              ImGui::SetKeyboardFocusHere();
              g_bEditFocus = false;
            }
            ImGui::SetNextItemWidth(cw + P(8.0f));
            const bool bDone = ImGui::InputText("##ed", g_szEdit, sizeof g_szEdit, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool bEscape = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool bTab = ImGui::IsItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_DownArrow));
            const bool bUp = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_UpArrow);
            if (bEscape) {
              g_editId.clear();
            } else if (bDone || bTab || bUp || ImGui::IsItemDeactivated()) {
              if (std::string(g_szEdit) != s.text)
                Ask(s, g_szEdit);
              g_editId.clear();
              // on to the next that edits, or the one before, once the table is drawn
              if (bTab || bUp) {
                g_nextEdit = std::string(bUp ? "-" : "+") + s.id;
                g_nextLang.clear();
              }
            }
          } else {
            const bool bEmpty = s.text.empty();
            const ImU32 col = s.nSource == SRC_MOD ? C(TK_HI) : s.bEdit ? C(TK_INK) : C(TK_INK2);
            if (s.bEdit) {
              ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(3.0f)));
              if (ImGui::InvisibleButton("##cell", ImVec2(cw + P(8.0f), rowH - P(4.0f))))
                StartEdit(s, ImVec2(p.x - P(4.0f), p.y + rowH - P(4.0f)));
              if (ImGui::IsItemHovered()) {
                td->AddRect(ImVec2(p.x - P(4.0f), p.y - P(3.0f)), ImVec2(p.x + cw + P(4.0f), p.y + rowH - P(7.0f)), C(TK_BTN_RIM_HI), P(2.0f));
                if (!s.note.empty())
                  ImGui::SetTooltip("%s\nClick to edit", s.note.c_str());
              }
            } else {
              ImGui::Dummy(ImVec2(cw, rowH - P(10.0f)));
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Read only: %s", s.note.c_str());
            }
            if (s.bChanged)
              td->AddCircleFilled(ImVec2(p.x + P(1.0f) - P(4.0f), p.y + P(1.0f)), P(3.0f), C(TK_CHANGED));
            const float ty = Mid(p.y - P(5.0f), rowH, kStudioFsS);
            if (bEmpty)
              T(td, STUDIO_FONT_TEXT, kStudioFsS, p.x, ty, C(TK_INK_OFF), s.note.empty() ? std::string("none") : s.note, cw);
            else
              DrawText(td, p.x, ty, s.text, kStudioFsS, cw, col, bButtons, C(TK_WARN));
          }
        }
        // the hotkey
        ImGui::TableSetColumnIndex(COL_KEY);
        if (bButtons && s.cKey) {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          StudioKbd(td, ImVec2(p.x, p.y + (rowH - P(10.0f) - P(18.0f)) / 2.0f), std::string(1, static_cast<char>(toupper(s.cKey))).c_str());
        }
        // where the text comes from
        ImGui::TableSetColumnIndex(COL_SOURCE);
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          SourceChip(td, ImVec2(p.x, p.y + (rowH - P(10.0f) - P(18.0f)) / 2.0f), s);
        }
        // the classic text the mod's stands in for, or what a replace makes of it
        ImGui::TableSetColumnIndex(COL_OTHER);
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float cw = ImGui::GetContentRegionAvail().x;
          const float ty = Mid(p.y - P(5.0f), rowH, kStudioFsS);
          if (bPreview && after.count(&s))
            DrawText(td, p.x, ty, after[&s]->after, kStudioFsS, cw, C(TK_GOOD), bButtons, C(TK_WARN));
          else if (!s.classic.empty())
            DrawText(td, p.x, ty, s.classic, kStudioFsS, cw, C(TK_INK3), bButtons, C(TK_INK2));
        }
        // its translations: each edited in place, a missing one marked, a stale one chipped
        for (int li = 0; li < nLangs; ++li) {
          const std::string &code = langs[static_cast<size_t>(li)];
          ImGui::TableSetColumnIndex(COL_LANG + li);
          ImGui::PushID(li);
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float cw = ImGui::GetContentRegionAvail().x;
          const float ty = Mid(p.y - P(5.0f), rowH, kStudioFsS);
          const studio_languages::Cell c = LangCell(s, code);
          const bool bEditing = g_editId == s.id && g_editLang == code && !g_bMultiOpen;
          if (bEditing) {
            ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(3.0f)));
            if (g_bEditFocus) {
              ImGui::SetKeyboardFocusHere();
              g_bEditFocus = false;
            }
            ImGui::SetNextItemWidth(cw + P(8.0f));
            const bool bDone = ImGui::InputTextWithHint("##led", Plain(s.text).c_str(), g_szEdit, sizeof g_szEdit,
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool bEscape = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool bTab = ImGui::IsItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_DownArrow));
            const bool bUp = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_UpArrow);
            if (bEscape) {
              g_editId.clear();
            } else if (bDone || bTab || bUp || ImGui::IsItemDeactivated()) {
              AskLang(s, code, g_szEdit);
              g_editId.clear();
              if (bTab || bUp) {
                g_nextEdit = std::string(bUp ? "-" : "+") + s.id;
                g_nextLang = code;
              }
            }
          } else if (c.nState == studio_languages::CELL_EMPTY) {
            ImGui::Dummy(ImVec2(cw, rowH - P(10.0f)));
            if (Translatable(s))
              T(td, STUDIO_FONT_TEXT, kStudioFsXs, p.x, Mid(p.y - P(5.0f), rowH, kStudioFsXs), C(TK_INK_OFF), "nothing to translate", cw);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(Translatable(s) ? "The mod gives no text here, so there is nothing to translate"
                                                : "A classic text: the game gives it in %s itself", studio_languages::Name(code).c_str());
          } else {
            ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(3.0f)));
            if (ImGui::InvisibleButton("##lcell", ImVec2(cw + P(8.0f), rowH - P(4.0f))))
              StartEditLang(s, code, ImVec2(p.x - P(4.0f), p.y + rowH - P(4.0f)));
            if (ImGui::IsItemHovered()) {
              td->AddRect(ImVec2(p.x - P(4.0f), p.y - P(3.0f)), ImVec2(p.x + cw + P(4.0f), p.y + rowH - P(7.0f)), C(TK_BTN_RIM_HI), P(2.0f));
              const std::string name = studio_languages::Name(code);
              if (c.nState == studio_languages::CELL_MISSING)
                ImGui::SetTooltip("Not in %s yet: the game shows the mod's own text.\nClick to translate it", name.c_str());
              else if (c.nState == studio_languages::CELL_STALE)
                ImGui::SetTooltip("Made from \"%s\"; the mod's text is now \"%s\".\nClick, then Enter keeps it as it is, or change it",
                                  Plain(c.madeFrom).c_str(), Plain(s.text).c_str());
              else if (!c.from.empty())
                ImGui::SetTooltip("%s's translation, which this mod builds on.\nClick to give one of this mod's own", c.from.c_str());
              else
                ImGui::SetTooltip("Click to edit");
            }
            float tw = cw;
            if (c.nState == studio_languages::CELL_STALE || !c.from.empty()) {
              const char *pszChip = c.nState == studio_languages::CELL_STALE ? "stale" : "base mod";
              const float chw = StudioChipW(pszChip);
              StudioChip(td, ImVec2(p.x + cw - chw, p.y + (rowH - P(10.0f) - P(18.0f)) / 2.0f), pszChip,
                         c.nState == studio_languages::CELL_STALE ? CH_GOLD : CH_PLAIN);
              tw = cw - chw - P(6.0f);
            }
            if (c.nState == studio_languages::CELL_MISSING) {
              td->AddCircleFilled(ImVec2(p.x + P(3.0f), ty + P(kStudioFsS) * 0.55f), P(3.0f), C(TK_WARN));
              T(td, STUDIO_FONT_TEXT, kStudioFsS, p.x + P(12.0f), ty, C(TK_WARN), "missing", tw - P(12.0f));
            } else {
              DrawText(td, p.x, ty, c.text, kStudioFsS, tw, c.from.empty() ? C(TK_INK) : C(TK_INK2), bButtons, C(TK_WARN));
            }
          }
          ImGui::PopID();
        }
        // a row's ways on: open it where it is made; the classic words back
        ImGui::TableSetColumnIndex(COL_ACT);
        if (bRowHover) {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          ImGui::SetCursorScreenPos(ImVec2(p.x - P(4.0f), p.y - P(2.0f)));
          if (StudioButton("##open", "", "link", SB_FLAT, 22.0f, 26.0f))
            OpenOf(s);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip(s.nKind == K_BUTTON || s.nKind == K_DISABLED ? "Open its card" : s.nKind == K_RACE ? "Open the race"
                              : s.nKind == K_MOD ? "Open the mod's page" : "Open the record");
          if (s.bEdit && s.nSource == SRC_MOD && !s.classic.empty()) {
            ImGui::SameLine(0.0f, P(2.0f));
            if (StudioButton("##back", "", "undo", SB_FLAT, 22.0f, 26.0f))
              Ask(s, std::string());
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Back to the classic text");
          }
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
  ImGui::PopFont();
  // Tab and the arrows: the next string that edits, or the one before
  if (!g_nextEdit.empty()) {
    const bool bBack = g_nextEdit[0] == '-';
    const std::string from = g_nextEdit.substr(1);
    g_nextEdit.clear();
    int k = -1;
    for (size_t q = 0; q < order.size(); ++q)
      if (order[q]->id == from)
        k = static_cast<int>(q);
    const std::string lang = g_nextLang;
    g_nextLang.clear();
    for (int q = k + (bBack ? -1 : 1); k >= 0 && q >= 0 && q < static_cast<int>(order.size()); q += bBack ? -1 : 1)
      if (!lang.empty()) {
        // in a language's column: the next string it translates
        const Str &n = *order[static_cast<size_t>(q)];
        if (LangCell(n, lang).nState == studio_languages::CELL_EMPTY)
          continue;
        StartEditLang(n, lang, g_multiAt);
        if (g_bMultiOpen)
          g_multiAt = ImVec2(o.x + P(500.0f), o.y + P(120.0f));
        break;
      } else if (order[static_cast<size_t>(q)]->bEdit) {
        StartEdit(*order[static_cast<size_t>(q)], g_multiAt);
        if (g_bMultiOpen)
          g_multiAt = ImVec2(o.x + P(500.0f), o.y + P(120.0f));
        break;
      }
  }
  MultiEditor();
  // the foot: the marks and how to edit
  const float fy = b.y - footH;
  float x = o.x;
  d->AddRectFilled(ImVec2(x, fy + P(8.0f)), ImVec2(x + P(3.0f), fy + P(22.0f)), C(TK_MOD), P(1.5f));
  x += P(9.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK2), "the mod's text") + P(16.0f);
  d->AddCircleFilled(ImVec2(x + P(4.0f), fy + footH / 2.0f), P(3.5f), C(TK_CHANGED));
  x += P(12.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK2), "unsaved change") + P(16.0f);
  x += T(d, STUDIO_FONT_BOLD, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_WARN), "K") + P(4.0f);
  x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK2), "a button's hotkey, written <K>") + P(16.0f);
  if (nLangs > 0) {
    d->AddCircleFilled(ImVec2(x + P(4.0f), fy + footH / 2.0f), P(3.0f), C(TK_WARN));
    x += P(12.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK2), "not translated: the game shows the mod's own") + P(10.0f);
    x += StudioChip(d, ImVec2(x, fy + (footH - P(18.0f)) / 2.0f), "stale", CH_GOLD) + P(6.0f);
    x += T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK2), "made from an older text") + P(16.0f);
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(fy, footH, kStudioFsXs), C(TK_INK3),
    nLangs > 0 ? "Click a text to edit it; Tab goes on. Each edit is one Undo."
               : "Click a text to edit it: Enter keeps it, Esc leaves it, Tab goes on. Each edit is one Undo.",
    o.x + W - x);
}

}  // namespace

std::string StudioStringKey(const std::string &id) {
  const Str *p = Find(id);
  return p != nullptr && Translatable(*p) ? p->tkey : std::string();
}

namespace {

// The row a translation's key names: the first with it.
const Str *RowOfKey(const std::string &key) {
  static std::map<std::string, size_t> s_at;
  static unsigned s_nVersion = ~0u;
  unsigned nVersion = 0;
  const std::vector<Str> &all = Strings(&nVersion);
  if (nVersion != s_nVersion) {
    s_nVersion = nVersion;
    s_at.clear();
    for (size_t i = 0; i < all.size(); ++i)
      if (!all[i].tkey.empty() && !s_at.count(all[i].tkey))
        s_at[all[i].tkey] = i;
  }
  auto it = s_at.find(key);
  return it == s_at.end() ? nullptr : &all[it->second];
}

}  // namespace

std::string StudioStringWhere(const std::string &key) {
  const Str *p = RowOfKey(key);
  if (p == nullptr)
    return key;
  return std::string(kKindWords[p->nKind]) + ", " + p->where + ", " + p->place;
}

std::string StudioStringIdOf(const std::string &key) {
  const Str *p = RowOfKey(key);
  return p ? p->id : std::string();
}

void StudioStringsShowMissing(const std::string &code) {
  g_missingIn = code;
  StudioWorkspaceSet(9);
}

void StudioStringsInit(void) {
  PortControlCommand("studio_strings", CmdStrings);
  PortControlCommand("studio_string_set", CmdStringSet);
  PortControlCommand("studio_strings_replace", CmdReplace);
}
