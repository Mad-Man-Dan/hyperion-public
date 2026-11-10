// port_front_modes.cpp -- the game modes in the skirmish set-up (port_front_modes.h).
//
// Its sizes are in SC2's units and its colours the kit's, with the dark red of a harder choice,
// which no theme changes, as the Mutators tab marks one. Everything drawn here comes from the
// mode's declaration (../data/port_mode.h): the Game type list's entries from the modes
// installed, the presets and the settings from the chosen one. Nothing here names a mode or a
// setting.
//
// The chosen mode takes a tab of its own, laid out as the Mutators tab is: the settings as the
// set-up's own rows in the Game Options box's place, and beside it the map with what the row the
// player is on does and what the mode works out for that map. Game Options itself keeps a card of
// what the mode has changed, under the Game type row.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_front_modes.h"
#include "port_frontkit.h"
#include "port_frontlobby.h"  // the set-up's pieces
#include "port_ui.h"
#include "../platform/port_json.h"
#include "../platform/port_settings.h"

namespace FrontKit {
namespace {

// the colours no theme changes: a harder choice's dark red (mutators.css --mu-hard)
constexpr UiColor kHard = KitRgb(0xcc4136);
constexpr UiColor kHardRim = KitRgb(0xcc4136, 0.6f);

// the settings file's keys, under "hyperion"
const char *const kKeyMode = "gameMode";
const char *const kKeySettings = "gameModeSettings";
const char *const kKeySeed = "gameModeSeed";

// ---------------------------------------------------------------------------
// A setting's value, by its kind, and a mode's values as the set-up works on them.

struct Value {
  std::string s;                 // choice: the choice's id. seed: its digits, "" for a drawn one
  double d = 0.0;                // number
  bool b = false;                // toggle
  std::vector<std::string> off;  // list: the items turned off
};
typedef std::map<std::string, Value> Values;

bool IsSeed(const PortModeSetting &s) { return s.kind == "seed"; }

Value ValueOfJson(const std::string &kind, const PortJson &j)
{
  Value v;
  if (kind == "choice") {
    v.s = j.Str();
  } else if (kind == "number") {
    v.d = j.Num();
  } else if (kind == "toggle") {
    v.b = j.kind == PortJson::BOOL ? j.b : j.Num() != 0.0;
  } else if (kind == "list") {
    for (size_t i = 0; i < j.size(); ++i)
      if (j.At(i).kind == PortJson::STRING)
        v.off.push_back(j.At(i).s);
  } else if (kind == "seed") {
    // a mode may write its own as a number, 0 for one the game draws
    const long long n = j.kind == PortJson::NUMBER ? static_cast<long long>(j.d) : 0;
    v.s = n > 0 ? std::to_string(n) : std::string();
  }
  return v;
}

PortJson JsonOfValue(const std::string &kind, const Value &v)
{
  PortJson j;
  if (kind == "choice") {
    j.kind = PortJson::STRING;
    j.s = v.s;
  } else if (kind == "number") {
    j.kind = PortJson::NUMBER;
    j.d = v.d;
  } else if (kind == "toggle") {
    j.kind = PortJson::BOOL;
    j.b = v.b;
  } else if (kind == "list") {
    j.kind = PortJson::ARRAY;
    for (const std::string &id : v.off) {
      PortJson one;
      one.kind = PortJson::STRING;
      one.s = id;
      j.items.push_back(one);
    }
  } else if (kind == "seed") {
    j.kind = PortJson::NUMBER;
    j.d = v.s.empty() ? 0.0 : strtod(v.s.c_str(), NULL);
  }
  return j;
}

bool Same(const std::string &kind, const Value &a, const Value &b)
{
  if (kind == "number")
    return a.d == b.d;
  if (kind == "toggle")
    return a.b == b.b;
  if (kind != "list")
    return a.s == b.s;
  // a list's value is the items turned off, so the same set is the same value whatever its order
  if (a.off.size() != b.off.size())
    return false;
  for (const std::string &id : a.off)
    if (std::find(b.off.begin(), b.off.end(), id) == b.off.end())
      return false;
  return true;
}

Value BaseOf(const PortModeSetting &s)
{
  PortJson j;
  if (!s.base.empty())
    PortJsonParse(s.base.c_str(), &j);
  return ValueOfJson(s.kind, j);
}

Values Defaults(const PortModeDecl &m)
{
  Values v;
  for (const PortModeSetting &s : m.settings)
    v[s.id] = BaseOf(s);
  return v;
}

// A JSON object's members over the values already there, each read as its setting's kind.
void Apply(const PortModeDecl &m, const std::string &text, Values *pv)
{
  PortJson j;
  if (text.empty() || !PortJsonParse(text.c_str(), &j) || j.kind != PortJson::OBJECT)
    return;
  for (const PortModeSetting &s : m.settings) {
    const PortJson &one = j[s.id.c_str()];
    if (one.kind != PortJson::NONE)
      (*pv)[s.id] = ValueOfJson(s.kind, one);
  }
}

// The mode's values as the set-up works on them: its own defaults, with what the choice names
// over them, and the seed from the choice rather than from the settings.
Values Work(const PortModeDecl &m, const PortModeChoice &c)
{
  Values v = Defaults(m);
  Apply(m, c.settings, &v);
  for (const PortModeSetting &s : m.settings)
    if (IsSeed(s))
      v[s.id].s = c.seed != 0 ? std::to_string(c.seed) : std::string();
  return v;
}

// The settings as the choice keeps them: a JSON object of the values off the mode's own
// defaults, the others left out. The seed is the choice's own field.
std::string SettingsOf(const PortModeDecl &m, const Values &v)
{
  PortJson out;
  out.kind = PortJson::OBJECT;
  for (const PortModeSetting &s : m.settings) {
    if (IsSeed(s))
      continue;
    const Values::const_iterator it = v.find(s.id);
    if (it == v.end() || Same(s.kind, it->second, BaseOf(s)))
      continue;
    PortJsonMember mem;
    mem.first = s.id;
    mem.second = JsonOfValue(s.kind, it->second);
    out.members.push_back(mem);
  }
  return out.members.empty() ? std::string() : PortJsonWrite(out);
}

unsigned SeedOf(const PortModeDecl &m, const Values &v)
{
  for (const PortModeSetting &s : m.settings)
    if (IsSeed(s)) {
      const Values::const_iterator it = v.find(s.id);
      if (it != v.end() && !it->second.s.empty())
        return static_cast<unsigned>(strtoul(it->second.s.c_str(), NULL, 10));
    }
  return 0;
}

// Settings off their own value, of the ones a player is shown: a setting a mode keeps to itself
// is not one the player chose, so it is not counted or named anywhere a player reads.
int ChangedCount(const PortModeDecl &m, const Values &v, const std::vector<char> *pShown)
{
  int n = 0;
  for (size_t i = 0; i < m.settings.size(); ++i) {
    const PortModeSetting &s = m.settings[i];
    if (pShown != NULL && i < pShown->size() && (*pShown)[i] == 0)
      continue;
    const Values::const_iterator it = v.find(s.id);
    if (it != v.end() && !Same(s.kind, it->second, BaseOf(s)))
      ++n;
  }
  return n;
}

// ---------------------------------------------------------------------------
// A setting's choices or list items as they are drawn. A mode declares each with an id and a
// name: the id is what the value keeps, the name what a player reads. A declaration that gives
// one word for both reads that word as each.

struct Item {
  std::string id, name, line, from;
  bool bHard = false;
};

std::vector<Item> ItemsOf(const PortModeSetting &s)
{
  std::vector<Item> out;
  for (const PortModeItem &one : s.items) {
    Item it;
    it.id = one.id;
    it.name = one.name.empty() ? one.id : one.name;
    it.line = one.line;
    it.bHard = one.bHard;
    out.push_back(it);
  }
  // a mode that gives one word for the id and the name
  for (size_t i = out.size(); i < s.choices.size(); ++i) {
    Item it;
    it.id = it.name = s.choices[i];
    out.push_back(it);
  }
  return out;
}

// The item a value names: by its id, then by its name, so a value keeps working whichever of the
// two the mode wrote. -1 for none.
int ItemAt(const std::vector<Item> &items, const std::string &id)
{
  for (size_t i = 0; i < items.size(); ++i)
    if (items[i].id == id)
      return static_cast<int>(i);
  for (size_t i = 0; i < items.size(); ++i)
    if (items[i].name == id)
      return static_cast<int>(i);
  return -1;
}

// ---------------------------------------------------------------------------
// What the mode itself says about a value: the sentence under its control, what the chosen map
// makes of it, and whether it plays a part at all. Each is a function the mode declares, run in
// the set-up's own VM (PortModeEffect), and each is given every setting's value with the
// players this game seats beside them.

// Every value as JSON object text, with the players the set-up seats.
std::string Whole(const PortModeDecl &m, const Values &v, int nPlayers)
{
  PortJson out;
  out.kind = PortJson::OBJECT;
  for (const PortModeSetting &s : m.settings) {
    const Values::const_iterator it = v.find(s.id);
    if (it == v.end())
      continue;
    PortJsonMember mem;
    mem.first = s.id;
    mem.second = JsonOfValue(s.kind, it->second);
    out.members.push_back(mem);
  }
  PortJsonMember players;
  players.first = "players";
  players.second.kind = PortJson::NUMBER;
  players.second.d = nPlayers;
  out.members.push_back(players);
  return PortJsonWrite(out);
}

// A call runs the mode's Luau, so each answer is worked out once for a set of values and kept
// until one of them changes, rather than every frame the set-up is up.

struct Kept {
  std::string mode, of, map;
  std::vector<char> shown;  // one a setting
};
Kept g_shown;

// Whether each setting is drawn, in the mode's own order.
const std::vector<char> &ShownAll(const PortModeDecl &m, const std::string &whole)
{
  if (g_shown.mode != m.id || g_shown.of != whole || g_shown.shown.size() != m.settings.size()) {
    g_shown.mode = m.id;
    g_shown.of = whole;
    g_shown.shown.assign(m.settings.size(), 1);
    for (size_t i = 0; i < m.settings.size(); ++i)
      g_shown.shown[i] = PortModeShown(m, m.settings[i], whole) ? 1 : 0;
  }
  return g_shown.shown;
}

struct Said {
  std::string mode, of, map;
  std::vector<std::string> eff, read;  // one a setting
};
Said g_said;

// Every setting's own sentence, and what the chosen map makes of its value. A setting whose mode
// names no function for either is "" and costs no call.
const Said &SaidAll(const PortModeDecl &m, const std::string &whole, const char *pszMap)
{
  const std::string map = pszMap != NULL ? pszMap : "";
  if (g_said.mode != m.id || g_said.of != whole || g_said.map != map ||
      g_said.eff.size() != m.settings.size()) {
    g_said.mode = m.id;
    g_said.of = whole;
    g_said.map = map;
    g_said.eff.assign(m.settings.size(), std::string());
    g_said.read.assign(m.settings.size(), std::string());
    for (size_t i = 0; i < m.settings.size(); ++i) {
      g_said.eff[i] = PortModeEffect(m, m.settings[i], whole);
      g_said.read[i] = PortModeRead(m, m.settings[i], whole, map.empty() ? NULL : map.c_str());
    }
  }
  return g_said;
}

// ---------------------------------------------------------------------------
// The presets. The mode's own set stands first, so there is always a way back to it; any other
// set is the player's own, which no preset holds.

Values PresetValues(const PortModeDecl &m, const PortModePreset &p)
{
  Values v = Defaults(m);
  Apply(m, p.values, &v);
  return v;
}

bool SameSet(const PortModeDecl &m, const Values &a, const Values &b)
{
  for (const PortModeSetting &s : m.settings) {
    if (IsSeed(s))
      continue;
    const Values::const_iterator x = a.find(s.id), y = b.find(s.id);
    if (x == a.end() || y == b.end() || !Same(s.kind, x->second, y->second))
      return false;
  }
  return true;
}

// Which of the presets the values are: 0 the mode's own, 1 and up its presets, or -1 for a set of
// the player's own. A preset names no seed, so a run typed in is still the preset it came from.
int PresetRow(const PortModeDecl &m, const Values &v)
{
  if (SameSet(m, v, Defaults(m)))
    return 0;
  for (size_t i = 0; i < m.presets.size(); ++i)
    if (SameSet(m, v, PresetValues(m, m.presets[i])))
      return static_cast<int>(i) + 1;
  return -1;
}

// ---------------------------------------------------------------------------
// The modes, and the one chosen.

const PortModeDecl *Find(const std::string &id) { return id.empty() ? NULL : PortModeFind(id); }

// Why a mode cannot be chosen here, or "" when it can: what the mode itself says, then the
// players this game seats against the mode's own least and most.
// The players a mode is measured by: a mode that plays a player of its own does not count the
// seat it will claim, where the set-up has one for it to claim. So a game of four people and the
// mode's enemy computer is a game of four to a mode, and its own range is written in the players
// a person would say were playing.
int Players(const PortModeDecl *p, int nSeated, bool bEnemySeated)
{
  return p != NULL && p->enemyPlayer && bEnemySeated && nSeated > 0 ? nSeated - 1 : nSeated;
}

std::string WhyNot(const PortModeDecl &m, int nPlayers)
{
  if (!m.needsMet)
    return m.whyNot.empty() ? std::string(PortText("What it needs is not installed.")) : m.whyNot;
  if (nPlayers > 0 && nPlayers < m.playersMin)
    // {0} the fewest players the mode takes
    return PortTextNF("It needs {0} player.", "It needs {0} players.", m.playersMin,
                      {std::to_string(m.playersMin)});
  if (nPlayers > 0 && m.playersMax > 0 && nPlayers > m.playersMax)
    // {0} the most players the mode takes
    return PortTextNF("It holds {0} player.", "It holds {0} players.", m.playersMax,
                      {std::to_string(m.playersMax)});
  return std::string();
}

int g_nSel = 0;             // the setting the keys act on
int g_nDrag = -1;           // the slider under the button, the setting's place
bool g_bToKeep;             // the choice has changed and is not in the settings file yet
std::string g_seed;         // the run as it is typed
std::string g_seedOf;       // the mode it is typed for
std::map<std::string, std::string> g_kept;  // a mode's settings while another is chosen
int g_nSee = -1;      // a setting the column is to be scrolled to, chosen by a key
int g_nList = -1;     // the list setting whose things are open under its row, -1 for none
float g_fRows = 0;    // how far the tab's rows are scrolled, and how far they can be
float g_fRowsMax = 0;

// The name the mode's own game type goes by, which its tab and its card are named for; its own
// name where it names no other.
std::string TypeNameOf(const PortModeDecl &m) { return m.typeName.empty() ? m.name : m.typeName; }

void Clamp(const PortModeDecl &m)
{
  const int n = static_cast<int>(m.settings.size());
  g_nSel = n <= 0 ? 0 : (g_nSel % n + n) % n;
}

// ---------------------------------------------------------------------------
// The controls' looks.

// a row of a setting (n: 1 the keys' row, 2 pointed at, 4 off its default, 8 harder)
void DrawRow(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  const bool bSel = (n & 1) != 0, bHot = (n & 2) != 0, bOn = (n & 4) != 0, bHard = (n & 8) != 0;
  UiFill(x, y, w, h, bSel || bHot ? kLbItemHi : kLbItem, Sc(3));
  UiRing(x, y, w, h, bOn ? (bHard ? kHardRim : kLbHyperionRim) : bSel || bHot ? kScButtonHi.rim : kLbItemRim, Sc(3),
         fmaxf(1.0f, Sc(1)));
  if (bSel)
    UiFill(x, y, Sc(3), h, kScSelLine2);
}

// Game Options' card of what the mode changed, as the mutators' own summary is drawn (n: 1 with
// something changed)
void DrawSummary(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  UiFill(x, y, w, h, kLbItem, Sc(4));
  UiRing(x, y, w, h, n ? kLbHyperionRim : kLbItemRim, Sc(4), fmaxf(1.0f, Sc(1)));
  if (n)
    UiFill(x, y, Sc(3), h, kScInkAccept);
}

// what the value does, ruled at its left (n: 1 off its default, 2 harder)
void DrawEffect(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  UiFill(x, y, w, h, kLbItem, Sc(3));
  UiFill(x, y, Sc(3), h, (n & 2) ? kHard : (n & 1) ? kScInkAccept : kScWellRim);
}

// ---------------------------------------------------------------------------
// Laying out.

float TextWidth(UiFace face, float fSize, const std::string &s, float fSpacingEm = 0.0f)
{
  return UiTextWidth(face, Sc(fSize), Sc(fSize) * fSpacingEm, s.c_str()) / Sc(1);
}

void Rule(float fHeight)
{
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(fHeight));
  Open(gap);
  Close();
}

// A tag (lobby.css .lb-tag), lit when bOn.
void Tag(const char *psz, bool bOn)
{
  Decl tag;
  tag.d.layout.padding = Pad(Fs(9), Fs(9), Fs(3), Fs(3));
  tag.d.border = Border(bOn ? kLbHyperionRim : kScWellRim, 1, 1, 1, 1);
  Radius(tag, 3);
  Open(tag);
  Text(psz, UI_FACE_WIDE, Fs(12), 0.04f, bOn ? kScInkAccept : kScInkOff);
  Close();
}

// Items fWidth units wide at most to a row, fGap apart: the first of each row.
std::vector<int> WrapTo(const std::vector<float> &widths, float fWidth, float fGap)
{
  std::vector<int> first;
  float x = 0;
  for (size_t i = 0; i < widths.size(); ++i) {
    if (first.empty() || x + widths[i] > fWidth) {
      first.push_back(static_cast<int>(i));
      x = 0;
    }
    x += widths[i] + fGap;
  }
  return first;
}

// A number as it is written: a whole number plain, a fraction with the places it needs, so a mode
// whose step is a fraction still reads right.
std::string Num(double d)
{
  if (d == static_cast<double>(static_cast<long long>(d)))
    return std::to_string(static_cast<long long>(d));
  char sz[32];
  SDL_snprintf(sz, sizeof sz, "%g", d);
  return sz;
}

// A number as the game's slider, its ends named under it: the value chosen, or the one it had.
double Slider(const char *pszId, const PortModeSetting &s, double dCur, bool bMine, int nWhich)
{
  const double dSpan = s.max - s.min, dStep = s.step > 0 ? s.step : 1;
  const int nSteps = dSpan > 0 ? static_cast<int>(dSpan / dStep + 0.5) + 1 : 1;
  const Clay_ElementId id = Id(pszId, nWhich);
  if (bMine && g_bPressed && Over(id))
    g_nDrag = nWhich;
  double dTo = dCur;
  if (bMine && g_nDrag == nWhich) {
    const Clay_ElementData e = Clay_GetElementData(id);
    if (e.found && e.boundingBox.width > Sc(18) && nSteps > 1) {
      float f = (g_mouseX - e.boundingBox.x - Sc(9)) / (e.boundingBox.width - Sc(18));
      f = fmaxf(0.0f, fminf(1.0f, f));
      dTo = s.min + static_cast<int>(f * (nSteps - 1) + 0.5f) * dStep;
    }
  }
  Decl wrap;
  wrap.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wrap.d.layout.sizing.width = Grow();
  wrap.d.layout.childGap = Gap(Fs(3));
  Open(wrap);
  Decl sl;
  sl.d.id = id;
  sl.d.layout.sizing.width = Grow();
  sl.d.layout.sizing.height = Fixed(Sc(30));
  const float fAt = dSpan > 0 ? static_cast<float>((dTo - s.min) / dSpan) : 0.0f;
  sl.d.custom.customData =
      Custom(DrawScSlider, kScButton, fAt, (bMine && (Over(id) || g_nDrag == nWhich) ? 1 : 0) | (bMine ? 0 : 2));
  Open(sl);
  Close();
  Decl ends;
  ends.d.layout.sizing.width = Grow();
  Open(ends);
  Text(Keep(Num(s.min)), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff);
  Spacer();
  Text(Keep(Num(s.max)), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff);
  Close();
  Close();
  return dTo;
}

// A value as the row says it in a word.
std::string Shows(const PortModeSetting &s, const Value &v, const std::vector<Item> &items)
{
  if (s.kind == "toggle")
    return PortText(v.b ? "On" : "Off");
  if (s.kind == "seed")
    return v.s.empty() ? std::string(PortText("Random")) : v.s;
  if (s.kind == "number") {
    // the unit is the mode's own word, already in the player's language
    const std::string n = Num(v.d);
    return s.unit.empty() ? n : n + " " + s.unit;
  }
  if (s.kind == "list") {
    const int nAll = static_cast<int>(items.size()), nOn = nAll - static_cast<int>(v.off.size());
    if (nOn >= nAll)
      // {0} how many of the mode's things are in play
      return PortTextF("All {0}", {std::to_string(nAll)});
    if (nOn <= 0)
      return PortTextC("things in play", "None");
    // {0} how many are in play, {1} how many there are
    return PortTextF("{0} of {1}", {std::to_string(nOn), std::to_string(nAll)});
  }
  const int k = ItemAt(items, v.s);
  return k >= 0 ? items[k].name : v.s;
}

// Whether the value is a choice harder than the mode's own.
bool Harder(const PortModeSetting &s, const Value &v, const std::vector<Item> &items)
{
  if (s.kind != "choice")
    return false;
  const int k = ItemAt(items, v.s);
  return k >= 0 && items[k].bHard;
}

// ---------------------------------------------------------------------------
// The settings by group, in the order the mode declares them.

struct Group {
  std::string name;
  std::vector<int> items;  // places in the mode's settings
};

// The settings by group. A setting the mode does not show is left out: one whose `shown` answers
// false with these values does not apply, and one that always answers false is the mode's own, for
// a tool and not for a player.
std::vector<Group> Groups(const PortModeDecl &m, const std::vector<char> *pShown)
{
  std::vector<Group> out;
  for (size_t i = 0; i < m.settings.size(); ++i) {
    if (pShown != NULL && i < pShown->size() && (*pShown)[i] == 0)
      continue;
    const std::string name = m.settings[i].group.empty() ? std::string(PortText("Settings")) : m.settings[i].group;
    size_t g = 0;
    while (g < out.size() && out[g].name != name)
      ++g;
    if (g == out.size()) {
      Group one;
      one.name = name;
      out.push_back(one);
    }
    out[g].items.push_back(static_cast<int>(i));
  }
  return out;
}

// The settings in the column's order, which Up and Down follow.
std::vector<int> Order(const PortModeDecl &m, const std::vector<char> *pShown)
{
  std::vector<int> out;
  for (const Group &g : Groups(m, pShown))
    for (int i : g.items)
      out.push_back(i);
  return out;
}

// What the mode draws of the map, for the picture beside the settings: its preview's shapes in
// the picture's own kind. The mode names its shapes and the picture numbers them, so the kinds
// are matched here and nowhere else; a kind neither side knows draws nothing. The mode's call
// is kept by the settings and the map, so this runs its Luau only when one of them changes.
std::vector<MapShape> Shapes(const PortModeDecl &m, const std::string &whole, const char *pszMap)
{
  static const char *const kapsz[MapShape::SHAPES] = {"circle", "ring", "rect", "polygon", "line"};
  std::vector<MapShape> out;
  std::vector<PortModeShape> own;
  if (!PortModePreview(m, whole, pszMap, &own))
    return out;
  for (const PortModeShape &one : own) {
    MapShape sh;
    sh.nShape = -1;
    for (int i = 0; i < MapShape::SHAPES; ++i)
      if (one.shape == kapsz[i])
        sh.nShape = i;
    if (sh.nShape < 0)
      continue;
    sh.x = static_cast<float>(one.x);
    sh.y = static_cast<float>(one.y);
    sh.w = static_cast<float>(one.w);
    sh.h = static_cast<float>(one.h);
    sh.r = static_cast<float>(one.r);
    sh.x2 = static_cast<float>(one.x2);
    sh.y2 = static_cast<float>(one.y2);
    for (double d : one.points)
      sh.points.push_back(static_cast<float>(d));
    sh.rgb = one.nRgb;
    sh.fFill = static_cast<float>(one.fFill);
    sh.fEdge = static_cast<float>(one.fEdge);
    sh.fWidth = static_cast<float>(one.fWidth);
    sh.fDash = static_cast<float>(one.fDash);
    out.push_back(sh);
  }
  return out;
}

}  // namespace

bool ModesAny(void) { return !PortModeList().empty(); }

bool ModesChosen(const PortModeChoice &c) { return Find(c.id) != NULL; }

int ModesPlayers(const PortModeChoice &c, int nSeated, bool bEnemySeated)
{
  const PortModeDecl *p = Find(c.id);
  return Players(p, nSeated, bEnemySeated);
}

const char *ModesChosenName(const PortModeChoice &c)
{
  const PortModeDecl *p = Find(c.id);
  return p != NULL ? Keep(p->name) : "";
}

const char *ModesTypeName(const PortModeChoice &c)
{
  const PortModeDecl *p = Find(c.id);
  return p != NULL ? Keep(TypeNameOf(*p)) : "";
}

std::string ModesRulesNote(const PortModeChoice &c)
{
  const PortModeDecl *p = Find(c.id);
  if (p == NULL || !p->sim)
    return std::string();
  // {0} the game type the mode is listed as
  return PortTextF("Required by {0}", {TypeNameOf(*p)});
}

std::vector<ModeType> ModesTypes(void)
{
  std::vector<ModeType> out;
  for (const PortModeDecl &m : PortModeList()) {
    ModeType one;
    one.id = m.id;
    one.name = m.typeName;
    one.tag = m.typeTag;
    one.line = m.line;
    one.about = m.about;
    one.plays = m.typePlays;
    out.push_back(one);
  }
  return out;
}

std::string ModesWhyNot(const std::string &id, int nSeated, bool bEnemySeated)
{
  const PortModeDecl *p = Find(id);
  return p != NULL ? WhyNot(*p, Players(p, nSeated, bEnemySeated)) : std::string();
}

void ModesChoose(PortModeChoice *pc, const std::string &id)
{
  if (pc->id == id)
    return;
  // the mode set aside keeps its settings, so one chosen again comes back as it was left
  if (!pc->id.empty())
    g_kept[pc->id] = pc->settings;
  pc->id = id;
  pc->settings = g_kept.count(id) != 0 ? g_kept[id] : std::string();
  pc->seed = 0;
  g_seed.clear();
  g_seedOf.clear();
  g_nSel = 0;
  g_nList = -1;  // another mode's settings are not this one's
  g_fRows = 0;   // and its tab starts at its top
  ModesRemember(*pc);
}

void ModesRemember(const PortModeChoice &c)
{
  PortSettingsHold();
  PortSettingSetString("hyperion", kKeyMode, c.id.c_str());
  PortSettingSetString("hyperion", kKeySettings, c.settings.c_str());
  PortSettingSetNumber("hyperion", kKeySeed, c.seed);
  PortSettingsRelease();
}

PortModeChoice ModesRemembered(void)
{
  PortModeChoice c;
  char sz[1024] = {};
  if (PortSettingString("hyperion", kKeyMode, sz, sizeof sz))
    c.id = sz;
  // a mode no longer installed comes back as no mode, with nothing of its left behind
  if (Find(c.id) == NULL)
    return PortModeChoice();
  if (PortSettingString("hyperion", kKeySettings, sz, sizeof sz))
    c.settings = sz;
  double d = 0;
  if (PortSettingNumber("hyperion", kKeySeed, &d) && d > 0)
    c.seed = static_cast<uint32_t>(d);
  return c;
}

void ModesTabAt(int nAt) { g_nSel = nAt; }

namespace {

// The settings file is written once the button is up, not on every frame of a slider's drag:
// one write a change the player has finished making.
void Settle(bool bChanged, const PortModeChoice &c)
{
  g_bToKeep = g_bToKeep || bChanged;
  if (g_bToKeep && !g_bDown) {
    g_bToKeep = false;
    ModesRemember(c);
  }
}

// A number with more steps than a pulldown can be asked to list keeps the slider, since a mode
// may declare a thousand of them.
const int kStepsMost = 48;

// The word on a pulldown's row for the mode's own value, so a player can always see which it is.
const char *OwnWord(void) { return PortText("the mode\xe2\x80\x99s"); }

// A setting's control, at the right of its row as Game Options puts its own: a choice and a
// number as pulldowns, with the mode's own value marked; a list as a pulldown that opens its
// things under the row (the row itself holds the check box of a toggle, which its click turns
// over); a seed as the number typed in. True when the value changed.
bool Control(const PortModeDecl &m, int i, const PortModeSetting &one, const std::vector<Item> &items, Value *pVal,
             bool bPick)
{
  bool bChanged = false;
  const int nState = bPick ? PD_ON : PD_LOCKED;
  if (one.kind == "choice") {
    const int nBase = ItemAt(items, BaseOf(one).s);
    std::vector<PulldownRow> rows;
    for (size_t k = 0; k < items.size(); ++k) {
      PulldownRow r;
      r.text = items[k].name;
      if (static_cast<int>(k) == nBase)
        r.note = OwnWord();
      r.title = items[k].name;
      r.tip = items[k].line;
      rows.push_back(r);
    }
    const int nPick = PulldownRows("skmodepick", i, 188, rows, ItemAt(items, pVal->s), nState, 300);
    if (nPick >= 0 && items[nPick].id != pVal->s) {
      pVal->s = items[nPick].id;
      bChanged = true;
    }
  } else if (one.kind == "number") {
    const double dStep = one.step > 0 ? one.step : 1, dSpan = one.max - one.min;
    const int nSteps = dSpan > 0 ? static_cast<int>(dSpan / dStep + 0.5) + 1 : 1;
    if (nSteps > kStepsMost) {
      const double dTo = Slider("skmodeslider", one, pVal->d, bPick, i);
      if (dTo != pVal->d) {
        pVal->d = dTo;
        bChanged = true;
      }
    } else {
      const double dBase = BaseOf(one).d;
      std::vector<PulldownRow> rows;
      int nCur = 0;
      for (int k = 0; k < nSteps; ++k) {
        const double d = one.min + k * dStep;
        Value step;
        step.d = d;
        PulldownRow r;
        r.text = Shows(one, step, items);
        if (fabs(d - dBase) < dStep * 0.5)
          r.note = OwnWord();
        rows.push_back(r);
        if (fabs(d - pVal->d) < dStep * 0.5)
          nCur = k;
      }
      const int nPick = PulldownRows("skmodepick", i, 188, rows, nCur, nState, 220);
      if (nPick >= 0) {
        const double dTo = one.min + nPick * dStep;
        if (dTo != pVal->d) {
          pVal->d = dTo;
          bChanged = true;
        }
      }
    }
  } else if (one.kind == "list") {
    // all of them, or the things one by one under the row
    const std::vector<std::string> what = {
        // {0} how many things the setting has
        PortTextF("All {0}", {std::to_string(items.size())}), PortText("Choose\xe2\x80\xa6")};
    const int nPick = Pulldown("skmodepick", i, 188, what, -1, nState, NULL, Keep(Shows(one, *pVal, items)), 200);
    if (nPick == 0) {
      if (!pVal->off.empty()) {
        pVal->off.clear();
        bChanged = true;
      }
      g_nList = -1;
    } else if (nPick == 1) {
      g_nList = i;
    }
  } else if (one.kind == "seed") {
    // the run: a number typed in, or none, which draws one as the game starts
    if (g_seedOf != m.id) {
      g_seedOf = m.id;
      g_seed = pVal->s;
    }
    Decl edit = Box("skmodeseed", i);
    edit.d.layout.sizing.width = Fixed(Sc(188));
    edit.d.layout.sizing.height = Fixed(Sc(34));
    if (bPick) {
      ScEdit(edit, &g_seed, 9, PortText("Drawn at start"));
      // a run is a number, so only its digits are kept
      std::string digits;
      for (char ch : g_seed)
        if (ch >= '0' && ch <= '9')
          digits.push_back(ch);
      g_seed = digits;
      if (g_seed != pVal->s) {
        pVal->s = g_seed;
        bChanged = true;
      }
    } else {
      Open(edit);
      Text(Keep(pVal->s.empty() ? std::string(PortText("Drawn at start")) : pVal->s), UI_FACE_TEXT, Fs(14), 0.0f,
           kScInkOff);
      Close();
    }
    TipOn(edit.d.id, PortText("The run"),
          PortText("A number plays the same run again, so a wave comes as it came before. Left empty, the run is drawn "
                   "as the game starts and the score screen says its number at the end."));
  }
  return bChanged;
}

// The things of a list setting, as check boxes under its row while it is open. True when one
// changed.
bool ListUnder(int i, const std::vector<Item> &items, Value *pVal, bool bPick)
{
  bool bChanged = false;
  Decl list = Box("skmodelist", i);
  list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  list.d.layout.sizing.width = Grow();
  list.d.layout.padding = Pad(Fs(22), Fs(10), Fs(2), Fs(6));
  list.d.layout.childGap = Gap(Fs(3));
  Open(list);
  for (size_t k = 0; k < items.size(); ++k) {
    const bool bIn = std::find(pVal->off.begin(), pVal->off.end(), items[k].id) == pVal->off.end();
    Decl row = Box("skmodeitem", i * 64 + static_cast<int>(k));
    const bool bHot = bPick && Over(row.d.id);
    const bool bHit = bPick && Clicked(row.d.id);
    row.d.layout.sizing.width = Grow();
    row.d.layout.padding = Pad(0, 0, Fs(1), Fs(1));
    row.d.layout.childGap = Gap(Fs(8));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(row);
    Decl cb;
    cb.d.layout.sizing.width = Fixed(Sc(22));
    cb.d.layout.sizing.height = Fixed(Sc(21));
    cb.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bIn ? 1 : 0) | (bHot ? 2 : 0) | (bPick ? 0 : 8));
    Open(cb);
    Close();
    Text(Keep(items[k].name), UI_FACE_BOLD, Fs(14), 0.0f, !bIn ? kScInkOff : bHot ? kScInkHi : kScInkLabel);
    Close();
    // what the thing is, in the mode's own words
    if (!items[k].line.empty())
      TipOn(row.d.id, Keep(items[k].name),
            Keep(items[k].from.empty() ? items[k].line : items[k].from + kDot + items[k].line));
    if (bHit) {
      if (bIn)
        pVal->off.push_back(items[k].id);
      else
        pVal->off.erase(std::find(pVal->off.begin(), pVal->off.end(), items[k].id));
      bChanged = true;
    }
  }
  Decl done = Box("skmodedone", i);
  done.d.layout.sizing.width = Fixed(Sc(130));
  done.d.layout.sizing.height = Fixed(Sc(30));
  if (ScMini(done, PortText("Done"), '\0', true))
    g_nList = -1;
  Close();
  return bChanged;
}

// The mode's name and version, the mod it comes from, and its line: the tab's head, and the
// card's in short. bShort leaves out the line and the version.
void Head(const PortModeDecl &m, bool bShort, int nPlayers)
{
  Decl top;
  top.d.layout.sizing.width = Grow();
  top.d.layout.childGap = Gap(Fs(8));
  top.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(top);
  Text(Keep(bShort || m.version.empty() ? TypeNameOf(m) : TypeNameOf(m) + " " + m.version), UI_FACE_BOLD,
       Fs(bShort ? 16 : 17), 0.0f, kScInkAccept);
  Spacer();
  // the mod it comes from, unless that is the name already above it: a mode that declares no
  // game_type takes its mod's name for both, and "Survival, from Survival mod" says nothing
  // twice. The Game type list keeps the tag either way, where it is what marks a mod's entry
  // out from the classic types.
  if (!m.typeTag.empty() && m.typeTag != TypeNameOf(m))
    // {0} the mod the mode comes from
    Tag(Keep(Upper(PortTextF("from {0} mod", {m.typeTag}).c_str())), true);
  Close();
  if (!bShort && !m.line.empty()) {
    Decl line;
    line.d.layout.sizing.width = Grow();
    Open(line);
    Text(Keep(m.line), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc, -1, 1.35f);
    Close();
  }
  // this game does not fit the mode: the players seated may have changed since the type was
  // chosen, so the reason is said here as well as on the list's row, and Start Game says it too
  const std::string why = WhyNot(m, nPlayers);
  if (!why.empty()) {
    Decl warn;
    warn.d.layout.sizing.width = Grow();
    Open(warn);
    Text(Keep(why), UI_FACE_BOLD, Fs(13), 0.0f, kLbWarn, -1, 1.35f);
    Close();
  }
}

}  // namespace

bool ModesTab(float x, float y, float w, float h, const ModeSetUp &s)
{
  PortModeChoice &c = *s.pChoice;
  const PortModeDecl *pMode = Find(c.id);
  if (pMode == NULL)
    return false;
  bool bChanged = false;
  if (!g_bDown)
    g_nDrag = -1;
  Values v = Work(*pMode, c);
  Clamp(*pMode);
  // what the mode itself says of these values, worked out once and kept
  const std::string whole = Whole(*pMode, v, s.nPlayers);
  const std::vector<char> shown = ShownAll(*pMode, whole);
  const std::vector<Group> groups = Groups(*pMode, &shown);
  const std::vector<int> drawn = Order(*pMode, &shown);
  if (!drawn.empty() && std::find(drawn.begin(), drawn.end(), g_nSel) == drawn.end())
    g_nSel = drawn[0];

  Decl box = At("skmodebox", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(20), Fs(20), Fs(16), Fs(18));
  box.d.layout.childGap = Gap(Fs(6));
  InnerBox(box);
  Open(box);
  Decl head = Box("skmodehead");
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  head.d.layout.sizing.width = Grow();
  head.d.layout.childGap = Gap(Fs(4));
  Open(head);
  Head(*pMode, false, s.nPlayers);
  Close();
  TipOn(head.d.id, Keep(TypeNameOf(*pMode)), Keep(pMode->about.empty() ? pMode->line : pMode->about));

  // the mode's own presets, where it has any: its own set first, so there is always a way back to
  // it, and a set of the player's own named as such
  if (!pMode->presets.empty()) {
    const int nRow = PresetRow(*pMode, v);
    Rule(6);
    Open(OptionRow(44));
    OptionName(PortText("Preset"), NULL, 8.0f);
    std::vector<PulldownRow> rows;
    PulldownRow own;
    own.text = PortText("Standard");
    own.note = OwnWord();
    own.tip = PortText("The mode as its maker set it.");
    own.title = own.text;
    rows.push_back(own);
    for (const PortModePreset &p : pMode->presets) {
      PulldownRow r;
      r.text = p.name;
      r.tip = p.line;
      r.title = p.name;
      rows.push_back(r);
    }
    const int nPick = PulldownRows("skmodepreset", 0, 188, rows, nRow, s.bMine ? PD_ON : PD_LOCKED, 300);
    if (nPick >= 0 && nPick != nRow) {
      const Values want = nPick == 0 ? Defaults(*pMode) : PresetValues(*pMode, pMode->presets[nPick - 1]);
      for (const PortModeSetting &one : pMode->settings)
        if (!IsSeed(one))
          v[one.id] = want.find(one.id)->second;
      c.settings = SettingsOf(*pMode, v);
      bChanged = true;
    }
    if (nRow < 0)
      TipOn(Id("skmodepreset", 0), PortText("Preset"),
            PortText("Settings of your own: they are no preset\xe2\x80\x99s. Choosing one takes its settings."));
    Close();
  }

  // a mode with nothing to set says so and is done
  if (drawn.empty()) {
    Rule(10);
    Decl none;
    none.d.layout.sizing.width = Grow();
    none.d.layout.sizing.height = Grow();
    Open(none);
    Text(pMode->settings.empty() ? PortText("This mode has no settings: its rules are fixed.")
                                 : PortText("This mode has nothing to set for this game."),
         UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff, -1, 1.35f);
    Close();
    Close();
    Settle(bChanged, c);
    return bChanged;
  }

  // the settings by group, each a row of the set-up's own: the column they are in is what scrolls
  Rule(6);
  Decl clip = Box("skmodeclip");
  clip.d.layout.sizing.width = Grow();
  clip.d.layout.sizing.height = Grow();
  clip.d.clip.vertical = true;
  g_fRows += WheelPixels(Over(clip.d.id), Sc(90));
  g_fRows = SDL_clamp(g_fRows, 0.0f, g_fRowsMax);
  clip.d.clip.childOffset.y = -g_fRows;
  Open(clip);
  Decl rows = Box("skmoderows");
  rows.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  rows.d.layout.sizing.width = Grow();
  rows.d.layout.childGap = Gap(Fs(4));
  Open(rows);
  for (size_t g = 0; g < groups.size(); ++g) {
    if (!groups[g].name.empty()) {
      Decl gh;
      gh.d.layout.sizing.width = Grow();
      gh.d.layout.padding = Pad(Fs(2), Fs(2), Fs(g == 0 ? 0 : 10), Fs(4));
      gh.d.border = Border(kLbDivider, 0, 0, 0, 1);
      Open(gh);
      Text(Keep(Upper(groups[g].name.c_str())), UI_FACE_WIDE, Fs(12), 0.06f, kScInkHead);
      Close();
    }
    for (int i : groups[g].items) {
      const PortModeSetting &one = pMode->settings[i];
      const std::vector<Item> items = ItemsOf(one);
      Value &val = v[one.id];
      const bool bOn = !Same(one.kind, val, BaseOf(one)), bHard = Harder(one, val, items);
      const bool bToggle = one.kind == "toggle";
      Decl row = Box("skmoderow", i);
      const bool bHot = Over(row.d.id);
      const bool bHit = s.bMine && Clicked(row.d.id);
      row.d.layout.sizing.width = Grow();
      row.d.layout.sizing.height = Fixed(Sc(46));
      row.d.layout.padding = Pad(Fs(10), Fs(10), 0, 0);
      row.d.layout.childGap = Gap(Fs(8));
      row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      row.d.custom.customData =
          Custom(DrawRow, kScTab, 0.0f, (i == g_nSel ? 1 : 0) | (bHot ? 2 : 0) | (bOn ? 4 : 0) | (bHard ? 8 : 0));
      Open(row);
      Text(Keep(one.name), UI_FACE_BOLD, Fs(15), 0.0f, bHard ? kHard : bOn ? kScInkAccept : kScInkLabel);
      Spacer();
      if (bToggle) {
        // the check box, as Lock teams is; the row's own click turns it over
        Decl cb;
        cb.d.layout.sizing.width = Fixed(Sc(24));
        cb.d.layout.sizing.height = Fixed(Sc(23));
        cb.d.custom.customData =
            Custom(DrawScCheck, kScButton, 0.0f, (val.b ? 1 : 0) | (bHot ? 2 : 0) | (s.bMine ? 0 : 8));
        Open(cb);
        Close();
        if (bHit) {
          val.b = !val.b;
          bChanged = true;
        }
      } else if (Control(*pMode, i, one, items, &val, s.bMine)) {
        bChanged = true;
      }
      Close();  // the row
      if (!one.gist.empty())
        TipOn(row.d.id, Keep(one.name), Keep(one.gist));
      if (bHit)
        g_nSel = i;
      // a list's things, under its row while it is open
      if (one.kind == "list" && g_nList == i && ListUnder(i, items, &val, s.bMine))
        bChanged = true;
    }
  }
  Close();  // the rows
  Close();  // the clipping box
  // how far the rows scroll, from this frame's layout for the next
  const Clay_ElementData in = Clay_GetElementData(Id("skmoderows"));
  const Clay_ElementData out = Clay_GetElementData(Id("skmodeclip"));
  g_fRowsMax = in.found && out.found && in.boundingBox.height > out.boundingBox.height
                   ? in.boundingBox.height - out.boundingBox.height
                   : 0.0f;
  if (g_fRows > g_fRowsMax)
    g_fRows = g_fRowsMax;

  // the foot: how many settings are off the mode's own, and the way back to them
  const int nOff = ChangedCount(*pMode, v, &shown);
  const int nAll = static_cast<int>(drawn.size());
  Rule(6);
  Decl foot;
  foot.d.layout.sizing.width = Grow();
  foot.d.layout.childGap = Gap(Fs(8));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  // {0} how many settings are off the mode's own, {1} how many there are
  Text(Keep(nOff > 0 ? PortTextF("{0} of {1} changed from the mode\xe2\x80\x99s own",
                                 {std::to_string(nOff), std::to_string(nAll)})
                     : PortTextNF("{0} setting, as the mode sets it", "{0} settings, as the mode sets them", nAll,
                                  {std::to_string(nAll)})),
       UI_FACE_TEXT, Fs(13), 0.0f, nOff > 0 ? kScInkAccept : kScInkOff);
  Spacer();
  Decl reset = Box("skmodereset");
  reset.d.layout.sizing.width = Fixed(Sc(190));
  reset.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(reset, PortText("Mode Defaults"), 'R', s.bMine && (nOff > 0 || c.seed != 0))) {
    c.settings.clear();
    c.seed = 0;
    g_seed.clear();
    g_seedOf.clear();
    bChanged = true;
  }
  Close();
  TipOn(reset.d.id, PortText("Mode Defaults"),
        PortText("Puts every setting back to the mode\xe2\x80\x99s own, and the run back to one drawn as the game "
                 "starts."));
  Close();  // the box

  // a change made with a control keeps the values and the seed
  if (bChanged) {
    c.settings = SettingsOf(*pMode, v);
    c.seed = SeedOf(*pMode, v);
  }
  // a row taken by a key that is out of sight brings the column to it
  if (g_nSee >= 0) {
    const Clay_ElementData row = Clay_GetElementData(Id("skmoderow", g_nSee));
    if (row.found && out.found) {
      const float fPad = Sc(8);
      if (row.boundingBox.y < out.boundingBox.y + fPad)
        g_fRows -= out.boundingBox.y + fPad - row.boundingBox.y;
      else if (row.boundingBox.y + row.boundingBox.height > out.boundingBox.y + out.boundingBox.height - fPad)
        g_fRows += row.boundingBox.y + row.boundingBox.height - (out.boundingBox.y + out.boundingBox.height) + fPad;
      g_fRows = SDL_clamp(g_fRows, 0.0f, g_fRowsMax);
    }
    g_nSee = -1;
  }
  Settle(bChanged, c);
  return bChanged;
}

void ModesTabBox(float x, float y, float w, float h, const ModeSetUp &s)
{
  const PortModeDecl *pMode = Find(s.pChoice->id);
  if (pMode == NULL)
    return;
  const Values v = Work(*pMode, *s.pChoice);
  const std::string whole = Whole(*pMode, v, s.nPlayers);
  const std::vector<char> shown = ShownAll(*pMode, whole);
  const Said &said = SaidAll(*pMode, whole, s.pszMapFile);
  const std::vector<int> drawn = Order(*pMode, &shown);
  // the row pointed at is briefed until the pointer leaves the rows; otherwise the one the keys
  // are on
  int nHot = -1;
  for (int i : drawn)
    if (Over(Id("skmoderow", i)))
      nHot = i;
  const int nAt = nHot >= 0 ? nHot : g_nSel;

  Decl box = At("skmodeinfo", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(11), Fs(11), Fs(10), Fs(10));
  box.d.layout.childGap = Gap(Fs(3));
  InnerBox(box);
  Open(box);
  // the map the game is played on, as Game Options' own box shows it, with what the mode makes
  // of it drawn over it where the mode has a preview to give
  const std::vector<MapShape> shapes = Shapes(*pMode, whole, s.pszMapFile);
  const float fPic = fminf(w - 26, 150);
  MapPreview("skmodepic", s.pMap, s.nTileset, s.pszMapTitle, s.nMapW, s.nMapH, fPic, fPic, 30, false, false, NULL,
             shapes.empty() ? NULL : &shapes);
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(6));
  Open(gap);
  Close();
  // the caption, only where the mode drew something. Where the starts are drawn at random no
  // seat holds a known start until the game is launched, so the shapes say the size and the
  // shape of what the mode makes of the map and not the place: the caption says so in the words
  // the Start positions row's own tip uses, so a player meets the one explanation twice.
  if (!shapes.empty()) {
    Decl caption;
    caption.d.layout.sizing.width = Grow();
    Open(caption);
    // {0} the mode's name
    Text(Keep(s.bStartsFixed
                  ? PortTextF("Drawn as {0} reads this map", {TypeNameOf(*pMode)})
                  : PortTextF("Drawn as {0} reads this map. Starts are drawn at random, so a "
                              "place here is not a player\xe2\x80\x99s.",
                              {TypeNameOf(*pMode)})),
         UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff, -1, 1.35f);
    Close();
  }
  Text(Keep(s.pszMapTitle != NULL ? s.pszMapTitle : ""), UI_FACE_BOLD, Fs(20), 0.0f, kLbInkInfo);
  // {0} how many players the map starts, {1} and {2} its size in tiles
  Text(Keep(PortTextNF("{0} start \xc2\xb7 {1} \xc3\x97 {2}", "{0} starts \xc2\xb7 {1} \xc3\x97 {2}",
                       s.pMap != NULL ? static_cast<int>(s.pMap->starts.size()) : 0,
                       {std::to_string(s.pMap != NULL ? static_cast<int>(s.pMap->starts.size()) : 0),
                        std::to_string(s.nMapW), std::to_string(s.nMapH)})),
       UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
  // {0} the mode's name, where the caption above has not already said it
  if (shapes.empty())
    Text(Keep(Upper(PortTextF("played as {0}", {TypeNameOf(*pMode)}).c_str())), UI_FACE_WIDE, Fs(12), 0.04f,
         kScInkAccept);

  // the setting the player is on: what it is, what the value does, and what the mode works out
  // for this map
  if (nAt >= 0 && static_cast<size_t>(nAt) < pMode->settings.size()) {
    const PortModeSetting &one = pMode->settings[nAt];
    const std::vector<Item> items = ItemsOf(one);
    const Value &val = v.find(one.id)->second;
    const bool bOn = !Same(one.kind, val, BaseOf(one)), bHard = Harder(one, val, items);
    Rule(10);
    if (!one.group.empty())
      Text(Keep(Upper(one.group.c_str())), UI_FACE_WIDE, Fs(11), 0.06f, kScInkHead);
    Decl name;
    name.d.layout.sizing.width = Grow();
    Open(name);
    Text(Keep(one.name), UI_FACE_BOLD, Fs(18), 0.0f, kScInkLabel, -1, 1.3f);
    Close();
    if (!one.gist.empty()) {
      Decl gist;
      gist.d.layout.sizing.width = Grow();
      Open(gist);
      Text(Keep(one.gist), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff, -1, 1.35f);
      Close();
    }
    // what it is set to, and whether that is the mode's own
    // {0} the value in a word
    const std::string says = bOn ? Shows(one, val, items)
                                 : PortTextF("{0}, the mode\xe2\x80\x99s own", {Shows(one, val, items)});
    Decl value;
    value.d.layout.sizing.width = Grow();
    value.d.layout.padding = Pad(0, 0, Fs(6), 0);
    Open(value);
    Text(Keep(says), UI_FACE_BOLD, Fs(16), 0.0f, bHard ? kHard : bOn ? kScInkAccept : kScInkOff, -1, 1.3f);
    Close();
    const std::string eff = static_cast<size_t>(nAt) < said.eff.size() ? said.eff[nAt] : std::string();
    if (!eff.empty()) {
      Decl e = Box("skmodeeff");
      e.d.layout.sizing.width = Grow();
      e.d.layout.sizing.height = Fit();
      e.d.layout.padding = Pad(Fs(10), Fs(10), Fs(7), Fs(7));
      e.d.custom.customData = Custom(DrawEffect, kScTab, 0.0f, (bOn ? 1 : 0) | (bHard ? 2 : 0));
      Open(e);
      Text(Keep(eff), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkInfo, -1, 1.4f);
      Close();
    }
    const std::string read = static_cast<size_t>(nAt) < said.read.size() ? said.read[nAt] : std::string();
    if (!read.empty()) {
      Decl r;
      r.d.layout.sizing.width = Grow();
      r.d.layout.padding = Pad(0, 0, Fs(6), 0);
      Open(r);
      // {0} what the mode works out for the map chosen
      Text(Keep(PortTextF("On this map: {0}", {read})), UI_FACE_TEXT, Fs(13), 0.0f, kScInkAccept, -1, 1.35f);
      Close();
    }
  }
  Spacer();
  // what the mode takes from elsewhere, or hands a mod: its own lines, at the box's foot
  for (const std::string &note : pMode->notes) {
    Decl n;
    n.d.layout.sizing.width = Grow();
    n.d.layout.padding = Pad(0, 0, Fs(6), 0);
    Open(n);
    Text(Keep(note), UI_FACE_TEXT, Fs(12), 0.0f, kScInkOff, -1, 1.35f);
    Close();
  }
  Close();
}

int ModesChanged(const ModeSetUp &s)
{
  const PortModeDecl *pMode = Find(s.pChoice->id);
  if (pMode == NULL)
    return 0;
  const Values v = Work(*pMode, *s.pChoice);
  const std::vector<char> shown = ShownAll(*pMode, Whole(*pMode, v, s.nPlayers));
  return ChangedCount(*pMode, v, &shown);
}

bool ModesSummary(const ModeSetUp &s)
{
  const PortModeDecl *pMode = Find(s.pChoice->id);
  if (pMode == NULL)
    return false;
  const Values v = Work(*pMode, *s.pChoice);
  const std::string whole = Whole(*pMode, v, s.nPlayers);
  const std::vector<char> shown = ShownAll(*pMode, whole);
  const std::vector<int> drawn = Order(*pMode, &shown);
  const int nPreset = PresetRow(*pMode, v);
  // a chip for every setting off the mode's own: its name and what it is set to
  std::vector<int> off;
  std::vector<float> widths;
  std::vector<std::string> values;
  for (int i : drawn) {
    const PortModeSetting &one = pMode->settings[i];
    const Value &val = v.find(one.id)->second;
    if (Same(one.kind, val, BaseOf(one)))
      continue;
    off.push_back(i);
    values.push_back(" " + Shows(one, val, ItemsOf(one)));
    widths.push_back(18 + TextWidth(UI_FACE_BOLD, 14, one.name) + TextWidth(UI_FACE_TEXT, 14, values.back()) + 2);
  }

  Decl sum = Box("skmodecard");
  sum.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  sum.d.layout.sizing.width = Grow();
  sum.d.layout.padding = Pad(Fs(16), Fs(16), Fs(14), Fs(14));
  sum.d.layout.childGap = Gap(Fs(8));
  sum.d.custom.customData = Custom(DrawSummary, kScTab, 0.0f, off.empty() ? 0 : 1);
  Open(sum);
  Head(*pMode, true, s.nPlayers);
  if (off.empty()) {
    Decl none;
    none.d.layout.sizing.width = Grow();
    Open(none);
    Text(PortText("As the mode sets it."), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc, -1, 1.4f);
    Close();
  } else {
    if (nPreset > 0) {
      Decl pr;
      pr.d.layout.sizing.width = Grow();
      Open(pr);
      // {0} the preset's name
      Text(Keep(PortTextF("Preset: {0}", {pMode->presets[nPreset - 1].name})), UI_FACE_TEXT, Fs(14), 0.0f,
           kLbInkDesc);
      Close();
    }
    // the chips, wrapped to the card
    const std::vector<int> first = WrapTo(widths, 438 - 35 - 32, 6);
    Decl rows;
    rows.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    rows.d.layout.childGap = Gap(Fs(6));
    Open(rows);
    for (size_t r = 0; r < first.size(); ++r) {
      const int nEnd = r + 1 < first.size() ? first[r + 1] : static_cast<int>(off.size());
      Decl row;
      row.d.layout.childGap = Gap(Fs(6));
      Open(row);
      for (int k = first[r]; k < nEnd; ++k) {
        const PortModeSetting &one = pMode->settings[off[k]];
        const bool bHard = Harder(one, v.find(one.id)->second, ItemsOf(one));
        Decl chip;
        chip.d.layout.padding = Pad(Fs(9), Fs(9), Fs(3), Fs(3));
        chip.d.border = Border(bHard ? kHardRim : kLbHyperionRim, 1, 1, 1, 1);
        Radius(chip, 3);
        Open(chip);
        Text(Keep(one.name), UI_FACE_BOLD, Fs(14), 0.0f, bHard ? kHard : kScInkAccept);
        Text(Keep(values[k]), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkInfo);
        Close();
      }
      Close();
    }
    Close();
  }
  Decl go = Box("skmodego");
  go.d.layout.sizing.height = Fixed(Sc(34));
  // {0} the mode's own name, which its tab is named for
  const bool bGo = ScMini(go, Keep(PortTextF("The {0} tab", {TypeNameOf(*pMode)})), 'M');
  Close();
  TipOn(go.d.id, Keep(TypeNameOf(*pMode)),
        PortText("The mode\xe2\x80\x99s own tab: its presets and every setting it offers, with what each one does and "
                 "what it works out to on this map. Hotkey: M"));
  return bGo;
}

bool ModesTabKeys(const ModeSetUp &s, bool *pbChanged)
{
  *pbChanged = false;
  const PortModeDecl *pMode = Find(s.pChoice->id);
  if (pMode == NULL || pMode->settings.empty())
    return false;
  // R puts every setting back to the mode's own, as the foot's button does
  if (Key('R')) {
    PortModeChoice &c = *s.pChoice;
    if (s.bMine && (!c.settings.empty() || c.seed != 0)) {
      c.settings.clear();
      c.seed = 0;
      g_seed.clear();
      g_seedOf.clear();
      g_bToKeep = true;
      *pbChanged = true;
    }
    return true;
  }
  const bool bUp = Key(VK_UP), bDown = Key(VK_DOWN), bLeft = Key(VK_LEFT), bRight = Key(VK_RIGHT);
  if (bUp || bDown) {
    const Values vKeys = Work(*pMode, *s.pChoice);
    const std::vector<int> order = Order(*pMode, &ShownAll(*pMode, Whole(*pMode, vKeys, s.nPlayers)));
    if (order.empty())
      return false;
    size_t at = 0;
    while (at + 1 < order.size() && order[at] != g_nSel)
      ++at;
    g_nSel = order[(at + (bDown ? 1 : order.size() - 1)) % order.size()];
    g_nSee = g_nSel;  // the column scrolls to it, where it is below the fold
    return true;
  }
  if (!(bLeft || bRight))
    return false;
  if (!s.bMine)
    return true;
  // Left and Right step the setting the keys are on: a choice through its choices, a number by
  // its step, a toggle off and on
  Clamp(*pMode);
  const PortModeSetting &sel = pMode->settings[g_nSel];
  const std::vector<Item> items = ItemsOf(sel);
  Values v = Work(*pMode, *s.pChoice);
  Value &val = v[sel.id];
  if (sel.kind == "choice" && !items.empty()) {
    const int nAt = ItemAt(items, val.s);
    const int nTo = std::min(std::max(nAt + (bRight ? 1 : -1), 0), static_cast<int>(items.size()) - 1);
    if (nTo == nAt)
      return true;
    val.s = items[nTo].id;
  } else if (sel.kind == "number") {
    const double dStep = sel.step > 0 ? sel.step : 1;
    const double dTo = std::min(std::max(val.d + (bRight ? dStep : -dStep), sel.min), sel.max);
    if (dTo == val.d)
      return true;
    val.d = dTo;
  } else if (sel.kind == "toggle") {
    if (val.b == bRight)
      return true;
    val.b = bRight;
  } else {
    return true;
  }
  s.pChoice->settings = SettingsOf(*pMode, v);
  g_bToKeep = true;
  *pbChanged = true;
  return true;
}

}  // namespace FrontKit
