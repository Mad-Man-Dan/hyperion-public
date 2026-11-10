// design_scenario.cpp -- scenarios: a game set up the same way each time, to try an idea on. A
// scenario is a file in design/scenarios/: the game (the canvas, or a map with its game type,
// seats, mutators and seed), the units placed on it with their orders, the camera, the effects
// playing and the style the picture is drawn in. Loading one starts its game and sets it up, then
// takes its start point, a snapshot of the game (port_snap.cpp); Again puts the game back there,
// so the same fight plays again under whatever was changed since.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <SDL3/SDL_scancode.h>
#include "imgui.h"
#include "../gen/globals117.h"  // gPlayerMinerals, gPlayerGas
#include "CUnitMisc.h"    // get_unit_name_from_type
#include "cursor.h"       // cursor_forget_target
#include "gamemap.h"      // gamemap_force_redraw
#include "gametype.h"     // the game types' templates
#include "gluBNCreate.h"  // gametype_get_subtype_name
#include "net_cmds.h"     // gameloop_init, status_set_update
#include "snd.h"          // stop_digital
#include "status.h"       // status_get_new_selection, status_set_update
#include "../port/front/port_frontscreens.h"  // SkirmishGame, PortLaunchSkirmishGame
#include "../port/gameplay/port_ai.h"         // AiLevelName
#include "../port/gameplay/port_launch.h"     // PortLaunchCanvas
#include "../port/gameplay/port_mutators.h"
#include "../port/hud/port_hudstate.h"        // PortHudRewound
#include "../port/platform/port_json.h"
#include "../port/render/port_camera.h"       // PortCameraZoomTarget
#include "../port/render/port_present.h"      // PortPresentRedraw
#include "../port/render/port_view.h"         // PortViewCameraMiddle
#include "../port/replays/port_snap.h"
#include "../port/tools/port_control.h"
#include "design_engine.h"
#include "design_hooks.h"
#include "design_ui.h"

// The loaded stat_txt.tbl, the units' names (status.cpp).
extern void *sgpStatStrs;

namespace {

// The snapshots' line the start point is kept on, apart from the trainer's and a replay's seek.
const int kSnapLine = 0xD5E1;

const char *const kTilesets[] = {"badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight"};
const char *const kRaces[] = {"zerg", "terran", "protoss"};

struct Seat {
  int nRace = 6;  // 0 Zerg, 1 Terran, 2 Protoss, 6 random
  int nTeam = 0;
  int nLevel = -1;
  int nStart = -1;
  int nHandicap = 100;
};

struct Game {
  bool bCanvas = true;
  std::string tileset = "jungle";
  std::string map;
  std::string type = "Melee";
  int nSub = 0;  // a sub type's dwSubValue (Team Melee's teams, Top vs Bottom's split), 0 none
  int nSpeed = 6;
  bool bHyperion = false;
  int nSeed = 1117;
  bool bWatch = false;
  std::vector<Seat> seats;
  MutatorSet mut;
};

struct Scenario {
  std::string name;
  Game game;
  std::vector<std::string> units;    // spawn's fields, each a JSON object
  std::vector<std::string> effects;  // fx_play's
  bool bCamera = false;
  int nCamX = 0, nCamY = 0;
  double fZoom = 1.0;
  bool bReveal = false;
  bool bPaused = false;
  std::string style;
};

Scenario g_sc;
bool g_bKeep = true;  // the panels' spawns and effects kept in the scenario

// The game a launch makes, and how far it has come.
Game g_launch;
bool g_bLaunchPending;
bool g_bSeedArmed;
int g_nSeed;
// pending: waiting for the front end (a game on is left first); launching: the front end is making
// the game; started: it began, and is set up once its first frame has run
enum Stage { STAGE_NONE, STAGE_PENDING, STAGE_LAUNCHING, STAGE_STARTED, STAGE_READY };
Stage g_stage = STAGE_NONE;
bool g_bScenarioGame;  // the game on is the scenario's, set up by it
bool g_bLeaving;       // the game on was told to end, for a launch waiting
unsigned g_nLeftAt;    // and the frame it was told on
bool g_bResumed;       // the new game, begun paused, was told to go on

// The start point: its frame, while the snapshot is there.
bool g_bStart;
unsigned g_nStartFrame;
int g_nAgains;
std::string g_said;

std::string Lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

// An object's members as fields, its braces taken off, for DesignCall.
std::string Inner(const std::string &obj) {
  const size_t a = obj.find('{'), b = obj.rfind('}');
  return a == std::string::npos || b == std::string::npos || b <= a ? std::string() : obj.substr(a + 1, b - a - 1);
}

std::string Folder(void) {
  return DesignFolder().empty() ? std::string() : DesignFolder() + "scenarios\\";
}

// A name as a file's: letters, digits, space, - _ and . kept, anything else a -.
std::string FileName(const std::string &name) {
  std::string s;
  for (char c : name)
    s += isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.' ? c : '-';
  return s;
}

bool ReadText(const std::string &path, std::string *pText) {
  FILE *f = fopen(path.c_str(), "rb");
  if (f == NULL)
    return false;
  char buf[4096];
  size_t n;
  pText->clear();
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    pText->append(buf, n);
  fclose(f);
  return true;
}

bool WriteText(const std::string &path, const std::string &text) {
  FILE *f = fopen(path.c_str(), "wb");
  if (f == NULL)
    return false;
  const bool bOk = fwrite(text.data(), 1, text.size(), f) == text.size();
  return fclose(f) == 0 && bOk;
}

// ---- the game types, as the skirmish set-up lists them ------------------------------------------

struct Template {
  std::string name;
  TGAMEPARAMS params;
};
std::vector<Template> g_types;
std::vector<Template> *g_pFill;
int g_nFillSlots;

void __stdcall AddType(const char *pszName, const char *, const TGAMEPARAMS *pParams, const TTEMPLATEMETA *) {
  if (gametype_template_allowed(reinterpret_cast<const BYTE *>(pParams), 0))
    g_pFill->push_back({pszName, *pParams});
}

void __stdcall AddSub(const char *, const char *, const TGAMEPARAMS *pParams, const TTEMPLATEMETA *) {
  if (pParams->dwSubValue == 0)
    return;
  // Top vs Bottom splits the map's players: a split needs a player on each side
  if (pParams->bCategory == 0x0F && static_cast<int>(pParams->dwSubValue) >= g_nFillSlots)
    return;
  char sz[64] = "";
  gametype_get_subtype_name(sz, sizeof sz, pParams, g_nFillSlots);
  g_pFill->push_back({sz, *pParams});
}

const std::vector<Template> &Types(void) {
  if (g_types.empty()) {
    g_pFill = &g_types;
    IterateMainTemplates(AddType);
  }
  return g_types;
}

const Template *TypeNamed(const std::string &name) {
  for (const Template &t : Types())
    if (Lower(t.name) == Lower(name))
      return &t;
  return NULL;
}

std::vector<Template> Subs(const Template &t, int nSlots) {
  std::vector<Template> subs;
  g_pFill = &subs;
  g_nFillSlots = nSlots;
  IterateLeagueTemplates(t.params.bCategory, t.params.bSub, AddSub);
  return subs;
}

// ---- a game as JSON, and back -------------------------------------------------------------------

std::string RaceName(int nRace) {
  return nRace >= 0 && nRace < 3 ? kRaces[nRace] : "random";
}

int RaceOf(const PortJson &j) {
  if (j.kind == PortJson::NUMBER)
    return static_cast<int>(j.d);
  const std::string s = Lower(j.Str("random"));
  for (int i = 0; i < 3; ++i)
    if (s == kRaces[i])
      return i;
  return 6;
}

int LevelOf(const PortJson &j) {
  if (j.kind == PortJson::NUMBER)
    return static_cast<int>(j.d);
  const std::string s = Lower(j.Str(""));
  for (int i = 0; i < AI_LEVELS; ++i)
    if (s == Lower(AiLevelName(i)))
      return i;
  return -1;
}

std::string GameJson(const Game &g) {
  if (g.bCanvas)
    return "{\"canvas\":" + PortJsonQuote(g.tileset) + ",\"seed\":" + std::to_string(g.nSeed) + "}";
  std::string seats;
  for (const Seat &s : g.seats) {
    seats += std::string(seats.empty() ? "" : ",") + "{\"race\":" + PortJsonQuote(RaceName(s.nRace));
    if (s.nTeam > 0)
      seats += ",\"team\":" + std::to_string(s.nTeam);
    if (s.nLevel >= 0)
      seats += ",\"level\":" + PortJsonQuote(AiLevelName(s.nLevel));
    if (s.nStart >= 0)
      seats += ",\"start\":" + std::to_string(s.nStart);
    if (s.nHandicap < 100)
      seats += ",\"handicap\":" + std::to_string(s.nHandicap);
    seats += "}";
  }
  return "{\"map\":" + PortJsonQuote(g.map) + ",\"type\":" + PortJsonQuote(g.type) +
         (g.nSub > 0 ? ",\"sub\":" + std::to_string(g.nSub) : std::string()) + ",\"speed\":" + std::to_string(g.nSpeed) +
         ",\"hyperion\":" + (g.bHyperion ? "true" : "false") + ",\"seed\":" + std::to_string(g.nSeed) +
         ",\"watch\":" + (g.bWatch ? "true" : "false") + ",\"seats\":[" + seats + "],\"mutators\":" + MutatorsJson(g.mut) +
         "}";
}

bool GameFrom(const PortJson &j, Game *pGame, std::string *pWhy) {
  Game g;
  g.nSeed = static_cast<int>(j["seed"].Num(1117));
  if (j["canvas"].kind == PortJson::STRING) {
    g.tileset = j["canvas"].s;
    *pGame = g;
    return true;
  }
  if (j["map"].kind != PortJson::STRING || j["map"].s.empty()) {
    *pWhy = "a game is {canvas: tileset} or {map: path, type?, seats?, mutators?, seed?}";
    return false;
  }
  g.bCanvas = false;
  g.map = j["map"].s;
  g.type = j["type"].Str("Melee");
  g.nSub = static_cast<int>(j["sub"].Num(0));
  g.nSpeed = static_cast<int>(j["speed"].Num(6));
  g.bHyperion = j["hyperion"].kind == PortJson::BOOL && j["hyperion"].b;
  g.bWatch = j["watch"].kind == PortJson::BOOL && j["watch"].b;
  for (size_t i = 0; i < j["seats"].size(); ++i) {
    const PortJson &s = j["seats"].At(i);
    Seat seat;
    seat.nRace = RaceOf(s["race"]);
    seat.nTeam = static_cast<int>(s["team"].Num(0));
    seat.nLevel = LevelOf(s["level"]);
    seat.nStart = static_cast<int>(s["start"].Num(-1));
    seat.nHandicap = static_cast<int>(s["handicap"].Num(100));
    g.seats.push_back(seat);
  }
  if (g.seats.empty()) {
    g.seats.resize(2);
    g.seats[0].nRace = 1;
  }
  g.mut = MutatorsFromJson(j["mutators"]);
  if (TypeNamed(g.type) == NULL) {
    *pWhy = "no game type is named " + g.type;
    return false;
  }
  *pGame = g;
  return true;
}

// The skirmish set-up's record of a game (port_frontscreens.h), as SkirmishRecord makes it.
bool MakeSkirmish(const Game &g, SkirmishGame *pOut, std::string *pWhy) {
  const Template *pType = TypeNamed(g.type);
  if (pType == NULL) {
    *pWhy = "no game type is named " + g.type;
    return false;
  }
  TGAMEPARAMS t = pType->params;
  if (g.nSub > 0) {
    bool bFound = false;
    for (const Template &s : Subs(*pType, 8))
      if (static_cast<int>(s.params.dwSubValue) == g.nSub) {
        t = s.params;
        bFound = true;
      }
    if (!bFound) {
      *pWhy = g.type + " has no sub type " + std::to_string(g.nSub);
      return false;
    }
  }
  SkirmishGame s;
  s.map = g.map;
  const size_t cut = g.map.find_last_of("\\/");
  s.title = cut == std::string::npos ? g.map : g.map.substr(cut + 1);
  s.type = g.type;
  s.nKey = t.bCategory | t.bSub << 8 | t.wType << 16;
  s.bMelee = IsMeleeTemplate(t.bCategory, t.bSub, t.wType) != 0;
  s.nSpeed = g.nSpeed;
  s.bHyperion = g.bHyperion;
  s.mut = g.mut;
  s.bObserve = g.bWatch;
  for (const Seat &seat : g.seats) {
    SkirmishSeat r;
    r.nRace = seat.nRace;
    r.nTeam = seat.nTeam;
    r.nLevel = seat.nLevel;
    r.nStart = seat.nStart;
    r.nHandicap = seat.nHandicap;
    s.seats.push_back(r);
  }
  if (s.bMelee && s.seats.size() < 2) {
    *pWhy = "a melee game needs a computer's seat";
    return false;
  }
  *pOut = s;
  return true;
}

// ---- a scenario as a file, and back -------------------------------------------------------------

std::string ScenarioJson(const Scenario &sc) {
  std::string s = "{\n  \"name\": " + PortJsonQuote(sc.name) + ",\n  \"game\": " + GameJson(sc.game) + ",\n  \"units\": [";
  for (size_t i = 0; i < sc.units.size(); ++i)
    s += std::string(i ? "," : "") + "\n    " + sc.units[i];
  s += sc.units.empty() ? "],\n" : "\n  ],\n";
  s += "  \"effects\": [";
  for (size_t i = 0; i < sc.effects.size(); ++i)
    s += std::string(i ? "," : "") + "\n    " + sc.effects[i];
  s += sc.effects.empty() ? "],\n" : "\n  ],\n";
  if (sc.bCamera) {
    char sz[96];
    snprintf(sz, sizeof sz, "{\"x\": %d, \"y\": %d, \"zoom\": %.3g}", sc.nCamX, sc.nCamY, sc.fZoom);
    s += "  \"camera\": " + std::string(sz) + ",\n";
  }
  s += "  \"reveal\": " + std::string(sc.bReveal ? "true" : "false") + ",\n  \"paused\": " + (sc.bPaused ? "true" : "false") +
       ",\n  \"style\": " + PortJsonQuote(sc.style) + "\n}\n";
  return s;
}

bool ScenarioFrom(const PortJson &j, Scenario *pOut, std::string *pWhy) {
  Scenario sc;
  sc.name = j["name"].Str("");
  if (!GameFrom(j["game"], &sc.game, pWhy))
    return false;
  for (size_t i = 0; i < j["units"].size(); ++i)
    sc.units.push_back(PortJsonWrite(j["units"].At(i)));
  for (size_t i = 0; i < j["effects"].size(); ++i)
    sc.effects.push_back(PortJsonWrite(j["effects"].At(i)));
  const PortJson &cam = j["camera"];
  if (cam.kind == PortJson::OBJECT) {
    sc.bCamera = true;
    sc.nCamX = static_cast<int>(cam["x"].Num(0));
    sc.nCamY = static_cast<int>(cam["y"].Num(0));
    sc.fZoom = cam["zoom"].Num(1.0);
  }
  sc.bReveal = j["reveal"].kind == PortJson::BOOL && j["reveal"].b;
  sc.bPaused = j["paused"].kind == PortJson::BOOL && j["paused"].b;
  sc.style = j["style"].Str("");
  *pOut = sc;
  return true;
}

std::vector<std::string> Names(void) {
  std::vector<std::string> names;
  const std::string folder = Folder();
  WIN32_FIND_DATAA fd;
  HANDLE h = folder.empty() ? INVALID_HANDLE_VALUE : FindFirstFileA((folder + "*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return names;
  do {
    std::string n = fd.cFileName;
    names.push_back(n.substr(0, n.size() - 5));
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return names;
}

bool Load(const std::string &name, Scenario *pOut, std::string *pWhy) {
  std::string text;
  if (!ReadText(Folder() + FileName(name) + ".json", &text)) {
    *pWhy = "no scenario is named " + name;
    return false;
  }
  PortJson j;
  std::string why;
  if (!PortJsonParse(text.c_str(), &j, &why)) {
    *pWhy = name + ": " + why;
    return false;
  }
  if (!ScenarioFrom(j, pOut, pWhy))
    return false;
  if (pOut->name.empty())
    pOut->name = name;
  return true;
}

bool Save(const std::string &name, std::string *pWhy) {
  if (Folder().empty()) {
    *pWhy = "there is no design folder to save in";
    return false;
  }
  CreateDirectoryA(Folder().c_str(), NULL);
  g_sc.name = name;
  if (DesignInGame()) {
    double fX = 0, fY = 0;
    PortViewCameraMiddle(&fX, &fY);
    g_sc.bCamera = true;
    g_sc.nCamX = static_cast<int>(fX);
    g_sc.nCamY = static_cast<int>(fY);
    g_sc.fZoom = PortCameraZoomTarget();
  }
  g_sc.style = DesignStyleCurrent();
  if (!WriteText(Folder() + FileName(name) + ".json", ScenarioJson(g_sc))) {
    *pWhy = "could not write " + Folder() + FileName(name) + ".json";
    return false;
  }
  return true;
}

// ---- starting a scenario's game, and setting it up ----------------------------------------------

// The game left, if one is on, and the scenario's started from the front end (DesignLaunch).
void Launch(const Game &g) {
  g_launch = g;
  g_nSeed = g.nSeed;
  g_bSeedArmed = true;
  g_bLaunchPending = true;
  g_stage = STAGE_PENDING;
  g_bStart = false;
  g_bLeaving = false;
}

void Play(const std::vector<std::string> &effects) {
  DesignCall("fx_stop", "");
  for (const std::string &e : effects)
    DesignCall("fx_play", Inner(e));
}

// The scenario's set-up on its new game, run with the channel's commands in one poll, so the
// game does not move between them; then its start point.
void SetUp(void) {
  DesignCall("pause", "");
  for (const std::string &u : g_sc.units)
    DesignCall("spawn", Inner(u), [](const PortJson &j) {
      if (!j["ok"].b)
        g_said = "a unit was not placed: " + j["error"].Str("refused");
    });
  if (g_sc.bReveal)
    DesignCall("reveal", "");
  if (g_sc.bCamera) {
    char sz[96];
    snprintf(sz, sizeof sz, "\"x\":%d,\"y\":%d,\"zoom\":%.6g", g_sc.nCamX, g_sc.nCamY, g_sc.fZoom);
    DesignCall("camera", sz);
  }
  Play(g_sc.effects);
  DesignCall("design_start_here", "", [](const PortJson &j) {
    g_stage = STAGE_READY;
    g_said = j["ok"].b ? "set up: " + g_sc.name : j["error"].Str("refused");
  });
  if (!g_sc.bPaused)
    DesignCall("resume", "");
  if (!g_sc.style.empty())
    DesignStyleApply(g_sc.style);
}

bool Again(std::string *pAnswer) {
  if (!DesignInGame()) {
    *pAnswer = "Again needs a single-player game";
    return false;
  }
  const bool bThere = g_bStart && PortSnapHas(kSnapLine, g_nStartFrame);
  const unsigned nAt = bThere ? PortSnapRestore(kSnapLine, g_nStartFrame, false) : ~0u;
  if (nAt == ~0u) {
    // no start point to go back to: the scenario's game made again, when this is its game
    if (!g_bScenarioGame) {
      *pAnswer = "there is no start point: Start here takes one";
      return false;
    }
    Launch(g_launch);
    *pAnswer = "\"again\":\"loading\"";
    return true;
  }
  // the selection came back with the game; the console's copy, the unit under the cursor and
  // the sound are the program's (as the trainer's rewind does)
  status_get_new_selection();
  status_set_update();
  cursor_forget_target();
  PortHudRewound();
  gamemap_force_redraw();
  stop_digital();
  PortPresentRedraw();
  ++g_nAgains;
  // the effects are the design engine's, not the game's: played again from the start
  if (g_bScenarioGame)
    Play(g_sc.effects);
  if (!(g_bScenarioGame && g_sc.bPaused))
    DesignCall("resume", "");
  *pAnswer = "\"again\":\"done\",\"frame\":" + std::to_string(nAt);
  return true;
}

// ---- the channel's commands ---------------------------------------------------------------------

std::string Fields(void);

// design_scenario: with nothing, the scenario's state. {list} the scenarios; {load: name} starts
// one; {save: name}; {game: {...}} the game it starts, with launch: true to start it now;
// {units: [...]} and {effects: [...]} added to it; {clear_units}, {clear_effects}; {reveal},
// {paused}, {style}; {new: true} an empty one on the game now.
bool CmdScenario(const PortJson &j, std::string *pAnswer) {
  std::string why;
  if (j["list"].kind == PortJson::BOOL) {
    std::string s;
    for (const std::string &n : Names())
      s += std::string(s.empty() ? "" : ",") + PortJsonQuote(n);
    *pAnswer = "\"scenarios\":[" + s + "],\"folder\":" + PortJsonQuote(Folder());
    return true;
  }
  if (j["load"].kind == PortJson::STRING) {
    Scenario sc;
    if (!Load(j["load"].s, &sc, pAnswer))
      return false;
    g_sc = sc;
    Launch(g_sc.game);
    *pAnswer = Fields();
    return true;
  }
  if (j["new"].kind == PortJson::BOOL) {
    g_sc = Scenario();
    g_sc.game = g_launch;
  }
  if (j["game"].kind == PortJson::OBJECT) {
    Game g;
    if (!GameFrom(j["game"], &g, pAnswer))
      return false;
    g_sc.game = g;
  }
  // cleared first, so one request can replace them
  if (j["clear_units"].kind == PortJson::BOOL)
    g_sc.units.clear();
  if (j["clear_effects"].kind == PortJson::BOOL)
    g_sc.effects.clear();
  for (size_t i = 0; i < j["units"].size(); ++i)
    g_sc.units.push_back(PortJsonWrite(j["units"].At(i)));
  for (size_t i = 0; i < j["effects"].size(); ++i)
    g_sc.effects.push_back(PortJsonWrite(j["effects"].At(i)));
  if (j["reveal"].kind == PortJson::BOOL)
    g_sc.bReveal = j["reveal"].b;
  if (j["paused"].kind == PortJson::BOOL)
    g_sc.bPaused = j["paused"].b;
  if (j["style"].kind == PortJson::STRING)
    g_sc.style = j["style"].s;
  if (j["save"].kind == PortJson::STRING && !Save(j["save"].s, pAnswer))
    return false;
  if (j["launch"].kind == PortJson::BOOL && j["launch"].b)
    Launch(g_sc.game);
  *pAnswer = Fields();
  return true;
}

// design_launch {game}: a game started as a scenario's would be, with nothing set up on it.
bool CmdLaunch(const PortJson &j, std::string *pAnswer) {
  Game g;
  if (!GameFrom(j["game"].kind == PortJson::OBJECT ? j["game"] : j, &g, pAnswer))
    return false;
  g_sc = Scenario();
  g_sc.game = g;
  Launch(g);
  *pAnswer = Fields();
  return true;
}

bool CmdAgain(const PortJson &, std::string *pAnswer) {
  return Again(pAnswer);
}

// design_give {player?, minerals?, gas?}: minerals and gas added to a player's in the game being tried
// (player 0 when not given), the game then marked as changed, as a spawn marks it.
bool CmdGive(const PortJson &j, std::string *pAnswer) {
  if (!DesignInGame()) {
    *pAnswer = "giving needs a single-player game";
    return false;
  }
  const int nPlayer = static_cast<int>(j["player"].Num(0));
  if (nPlayer < 0 || nPlayer >= 8) {
    *pAnswer = "a player is 0 to 7";
    return false;
  }
  const int nMinerals = gPlayerMinerals[nPlayer] + static_cast<int>(j["minerals"].Num(0));
  const int nGas = gPlayerGas[nPlayer] + static_cast<int>(j["gas"].Num(0));
  gPlayerMinerals[nPlayer] = nMinerals < 0 ? 0 : nMinerals;
  gPlayerGas[nPlayer] = nGas < 0 ? 0 : nGas;
  PortControlChanged();
  *pAnswer = "\"player\":" + std::to_string(nPlayer) + ",\"minerals\":" + std::to_string(gPlayerMinerals[nPlayer]) +
             ",\"gas\":" + std::to_string(gPlayerGas[nPlayer]);
  return true;
}

// design_start_here: the start point Again goes back to, at the frame now.
bool CmdStartHere(const PortJson &, std::string *pAnswer) {
  if (!DesignInGame()) {
    *pAnswer = "a start point needs a single-player game";
    return false;
  }
  PortSnapDropLine(kSnapLine);
  g_nStartFrame = DesignGameFrame();
  PortSnapTake(kSnapLine, g_nStartFrame);
  g_bStart = PortSnapHas(kSnapLine, g_nStartFrame);
  PortSnapStats st;
  PortSnapStatsGet(&st);
  char sz[128];
  snprintf(sz, sizeof sz, "\"start\":%u,\"ms\":%.2f,\"mb\":%.1f", g_nStartFrame, st.msTake, st.cbState / 1048576.0);
  *pAnswer = g_bStart ? std::string(sz) : std::string("the snapshot could not be taken");
  return g_bStart;
}

const char *StageName(void) {
  static const char *const kNames[] = {"none", "pending", "launching", "started", "ready"};
  return kNames[g_stage];
}

std::string Fields(void) {
  return "\"scenario\":{\"name\":" + PortJsonQuote(g_sc.name) + ",\"stage\":\"" + StageName() +
         "\",\"its_game\":" + (g_bScenarioGame ? "true" : "false") + ",\"start\":" +
         (g_bStart ? std::to_string(g_nStartFrame) : std::string("null")) + ",\"agains\":" + std::to_string(g_nAgains) +
         ",\"units\":" + std::to_string(g_sc.units.size()) + ",\"effects\":" + std::to_string(g_sc.effects.size()) +
         ",\"keep\":" + (g_bKeep ? "true" : "false") + ",\"style\":" + PortJsonQuote(g_sc.style) +
         ",\"game\":" + GameJson(g_sc.game) + "}";
}

// ---- the panel ----------------------------------------------------------------------------------

std::vector<std::string> g_names;
bool g_bNamesAsked;
char g_szSaveAs[64];
char g_szMapFilter[64];
struct MapRow {
  std::string name, path;
};
std::vector<MapRow> g_maps;
int g_nMapPlayers = 8;

void FindMaps(void) {
  DesignCall("maps", "\"filter\":" + PortJsonQuote(g_szMapFilter) + ",\"max\":400", [](const PortJson &j) {
    g_maps.clear();
    for (size_t i = 0; i < j["maps"].size(); ++i)
      g_maps.push_back({j["maps"].At(i)["name"].Str(), j["maps"].At(i)["path"].Str()});
  });
}

void MapChosen(const std::string &path) {
  g_sc.game.map = path;
  DesignCall("map_info", "\"path\":" + PortJsonQuote(path), [](const PortJson &j) {
    if (j["ok"].b)
      g_nMapPlayers = static_cast<int>(j["players"].Num(8));
  });
}

std::string UnitLabel(const std::string &obj) {
  PortJson j;
  if (!PortJsonParse(obj.c_str(), &j))
    return obj;
  std::string unit = j["unit"].Str("");
  if (j["unit"].kind == PortJson::NUMBER) {
    const char *psz = sgpStatStrs != NULL ? get_unit_name_from_type(static_cast<WORD>(j["unit"].d)) : NULL;
    unit = psz != NULL && psz[0] ? psz : std::to_string(static_cast<int>(j["unit"].d));
  }
  char sz[160];
  snprintf(sz, sizeof sz, "%s x%d, player %d, at %d, %d%s", unit.c_str(), static_cast<int>(j["count"].Num(1)),
           static_cast<int>(j["player"].Num(0)), static_cast<int>(j["x"].Num(0)), static_cast<int>(j["y"].Num(0)),
           j["attack"].kind == PortJson::ARRAY ? ", attacking" : j["move"].kind == PortJson::ARRAY ? ", moving" : "");
  return sz;
}

std::string EffectLabel(const std::string &obj) {
  PortJson j;
  if (!PortJsonParse(obj.c_str(), &j))
    return obj;
  char sz[128];
  snprintf(sz, sizeof sz, "%s at %d, %d%s", j["effect"].Str("?").c_str(), static_cast<int>(j["at"].At(0).Num(0)),
           static_cast<int>(j["at"].At(1).Num(0)), j["loop"].kind == PortJson::BOOL && j["loop"].b ? ", looping" : "");
  return sz;
}

// Each kept spawn or effect, with a button that drops it.
void KeptList(std::vector<std::string> *pList, std::string (*pfnLabel)(const std::string &), const char *pszId) {
  for (size_t i = 0; i < pList->size(); ++i) {
    ImGui::PushID(pszId);
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::SmallButton("x")) {
      pList->erase(pList->begin() + static_cast<std::ptrdiff_t>(i));
      ImGui::PopID();
      ImGui::PopID();
      break;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(pfnLabel((*pList)[i]).c_str());
    ImGui::PopID();
    ImGui::PopID();
  }
}

void GameEditor(Game &g) {
  int nKind = g.bCanvas ? 0 : 1;
  ImGui::RadioButton("The canvas", &nKind, 0);
  ImGui::SameLine();
  ImGui::RadioButton("A map", &nKind, 1);
  g.bCanvas = nKind == 0;
  if (g.bCanvas) {
    if (ImGui::BeginCombo("Tileset", g.tileset.c_str())) {
      for (const char *psz : kTilesets)
        if (ImGui::Selectable(psz, g.tileset == psz))
          g.tileset = psz;
      ImGui::EndCombo();
    }
  } else {
    ImGui::TextWrapped("%s", g.map.empty() ? "No map chosen" : g.map.c_str());
    ImGui::SetNextItemWidth(-80);
    if (ImGui::InputTextWithHint("##mapfilter", "Filter, such as python", g_szMapFilter, sizeof g_szMapFilter,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
      FindMaps();
    ImGui::SameLine();
    if (ImGui::Button("Find"))
      FindMaps();
    if (!g_maps.empty() && ImGui::BeginListBox("##maps", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 6))) {
      for (const MapRow &m : g_maps)
        if (ImGui::Selectable(m.name.c_str(), m.path == g.map))
          MapChosen(m.path);
      ImGui::EndListBox();
    }
    const Template *pType = TypeNamed(g.type);
    if (ImGui::BeginCombo("Game type", g.type.c_str())) {
      for (const Template &t : Types())
        if (ImGui::Selectable(t.name.c_str(), t.name == g.type)) {
          g.type = t.name;
          g.nSub = 0;
        }
      ImGui::EndCombo();
    }
    if (pType != NULL) {
      const std::vector<Template> subs = Subs(*pType, g_nMapPlayers);
      if (!subs.empty()) {
        std::string now = "choose";
        for (const Template &s : subs)
          if (static_cast<int>(s.params.dwSubValue) == g.nSub)
            now = s.name;
        if (ImGui::BeginCombo("Sides", now.c_str())) {
          for (const Template &s : subs)
            if (ImGui::Selectable(s.name.c_str(), static_cast<int>(s.params.dwSubValue) == g.nSub))
              g.nSub = static_cast<int>(s.params.dwSubValue);
          ImGui::EndCombo();
        }
      }
    }
    ImGui::SliderInt("Speed", &g.nSpeed, 0, 6);
    ImGui::Checkbox("Hyperion's rules", &g.bHyperion);
    ImGui::SameLine();
    ImGui::Checkbox("Watch (every seat a computer)", &g.bWatch);
    if (ImGui::BeginTable("##seats", 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Seat");
      ImGui::TableSetupColumn("Race");
      ImGui::TableSetupColumn("Computer");
      ImGui::TableSetupColumn("Team");
      ImGui::TableSetupColumn("Start");
      ImGui::TableSetupColumn("");
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < g.seats.size(); ++i) {
        Seat &s = g.seats[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(i == 0 ? "You" : ("Computer " + std::to_string(i)).c_str());
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##race", RaceName(s.nRace).c_str())) {
          for (int r : {1, 0, 2, 6})
            if (ImGui::Selectable(RaceName(r).c_str(), s.nRace == r))
              s.nRace = r;
          ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##level", s.nLevel < 0 ? "the default" : AiLevelName(s.nLevel))) {
          if (ImGui::Selectable("the default", s.nLevel < 0))
            s.nLevel = -1;
          for (int l = 0; l < AI_LEVELS; ++l)
            if (ImGui::Selectable(AiLevelName(l), s.nLevel == l))
              s.nLevel = l;
          ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##team", &s.nTeam, 0, 8, s.nTeam == 0 ? "none" : "%d");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##start", &s.nStart, -1, 7, s.nStart < 0 ? "drawn" : "%d");
        ImGui::TableNextColumn();
        if (i > 0 && ImGui::SmallButton("x")) {
          g.seats.erase(g.seats.begin() + static_cast<std::ptrdiff_t>(i));
          ImGui::PopID();
          break;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (ImGui::SmallButton("Add a computer"))
      g.seats.push_back(Seat());
    if (ImGui::TreeNode("Mutators")) {
      // each of the set's values by its JSON name (port_mutators.h), 1.17's when untouched
      PortJson m;
      PortJsonParse(MutatorsJson(g.mut).c_str(), &m);
      bool bChanged = false;
      for (PortJsonMember &v : m.members) {
        int n = static_cast<int>(v.second.d);
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputInt(v.first.c_str(), &n)) {
          v.second.d = n;
          bChanged = true;
        }
      }
      if (bChanged)
        g.mut = MutatorsFromJson(m);
      if (ImGui::SmallButton("Classic's"))
        g.mut = MutatorSet();
      ImGui::TreePop();
    }
  }
  ImGui::InputInt("Seed", &g.nSeed);
}

}  // namespace

void DesignScenarioInit(void) {
  PortControlCommand("design_scenario", CmdScenario);
  PortControlCommand("design_launch", CmdLaunch);
  PortControlCommand("design_again", CmdAgain);
  PortControlCommand("design_give", CmdGive);
  PortControlCommand("design_start_here", CmdStartHere);
}

std::string DesignScenarioFields(void) {
  return Fields();
}

bool DesignLaunchPending(void) {
  return g_bLaunchPending;
}

int DesignLaunch(void) {
  g_bLaunchPending = false;
  g_stage = STAGE_LAUNCHING;
  g_bResumed = false;
  std::string why;
  int nStarted = FALSE;
  if (g_launch.bCanvas) {
    nStarted = PortLaunchCanvas(g_launch.tileset.c_str());
  } else {
    SkirmishGame s;
    if (MakeSkirmish(g_launch, &s, &why))
      nStarted = PortLaunchSkirmishGame(s);
  }
  if (!nStarted) {
    g_stage = STAGE_NONE;
    g_said = why.empty() ? "the game could not be started" : why;
  }
  return nStarted;
}

bool DesignSeed(int *pnSeed) {
  // every new game: the start point went with the last game's blocks; the game is the
  // scenario's when it is the one just launched
  g_bStart = false;
  g_bScenarioGame = g_stage == STAGE_LAUNCHING;
  if (g_stage == STAGE_LAUNCHING)
    g_stage = STAGE_STARTED;
  if (!g_bSeedArmed)
    return false;
  *pnSeed = g_nSeed;
  return true;
}

void DesignScenarioStep(void) {
  // a launch waits for the front end: a game on, whether it was on when asked or began after
  // (the command line's, still starting), is left as Quit Mission leaves, once its loop runs (a
  // game still starting sets its loop going after), and told again if it plays on
  const unsigned nFrame = DesignGameFrame();
  if (g_stage == STAGE_PENDING && DesignInGame() && nFrame >= 1 && (!g_bLeaving || nFrame >= g_nLeftAt + 24)) {
    g_bLeaving = true;
    g_nLeftAt = nFrame;
    gameloop_init(4);
  }
  // classic's pause outlives the game it was given in: a game left paused (a set-up's, a start
  // point's) begins the next one paused, on its first frame
  if (g_stage == STAGE_STARTED && DesignInGame() && DesignGamePaused() && !g_bResumed) {
    g_bResumed = true;
    DesignCall("resume", "");
  }
  if (g_stage == STAGE_STARTED && DesignInGame() && DesignGameFrame() >= 1 && sgpStatStrs != NULL) {
    g_stage = STAGE_READY;
    SetUp();
  }
}

void DesignPoll(void) {
  DesignBridgeDeliver();
  DesignScenarioStep();
  if (DesignThisProgram().pfnPoll)
    DesignThisProgram().pfnPoll();
}

void DesignScenarioKeep(const char *pszKind, const std::string &fields) {
  if (!g_bKeep || !DesignInGame())
    return;
  (strcmp(pszKind, "units") == 0 ? g_sc.units : g_sc.effects).push_back("{" + fields + "}");
}

void DesignPanelsKey(int nScancode, bool bShift) {
  if (nScancode == SDL_SCANCODE_F9)
    DesignCall(bShift ? "design_start_here" : "design_again", "", [](const PortJson &j) {
      g_said = j["ok"].b ? (j["start"].kind == PortJson::NUMBER ? "start point at frame " + std::to_string(static_cast<int>(j["start"].d))
                                                                  : std::string("again"))
                         : j["error"].Str("refused");
    });
  else if (nScancode == SDL_SCANCODE_F8)
    DesignCall("design_style", "\"flip\":true");
}

void DesignScenarioPanel(void) {
  if (!g_bNamesAsked) {
    g_bNamesAsked = true;
    g_names = Names();
  }
  const bool bGame = DesignInGame();
  ImGui::Text("%s", g_sc.name.empty() ? "No scenario" : g_sc.name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("(%s%s)", StageName(), g_bStart ? (", start at " + std::to_string(g_nStartFrame)).c_str() : "");
  ImGui::BeginDisabled(!bGame);
  if (ImGui::Button("Again (F9)"))
    DesignPanelsKey(SDL_SCANCODE_F9, false);
  ImGui::SameLine();
  if (ImGui::Button("Start here (Shift+F9)"))
    DesignPanelsKey(SDL_SCANCODE_F9, true);
  ImGui::EndDisabled();
  if (!g_said.empty())
    ImGui::TextDisabled("%s", g_said.c_str());
  ImGui::Separator();
  if (ImGui::BeginListBox("##scenarios", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 5))) {
    for (const std::string &n : g_names) {
      ImGui::PushID(n.c_str());
      if (ImGui::SmallButton("Load"))
        DesignCall("design_scenario", "\"load\":" + PortJsonQuote(n), [](const PortJson &j) {
          g_said = j["ok"].b ? "loading" : j["error"].Str("refused");
        });
      ImGui::SameLine();
      ImGui::TextUnformatted(n.c_str());
      ImGui::PopID();
    }
    ImGui::EndListBox();
  }
  if (ImGui::SmallButton("List again"))
    g_names = Names();
  ImGui::SetNextItemWidth(-90);
  ImGui::InputTextWithHint("##saveas", "Save as, such as marines-vs-lurkers", g_szSaveAs, sizeof g_szSaveAs);
  ImGui::SameLine();
  ImGui::BeginDisabled(g_szSaveAs[0] == '\0');
  if (ImGui::Button("Save")) {
    std::string why;
    g_said = Save(g_szSaveAs, &why) ? "saved " + std::string(g_szSaveAs) : why;
    g_names = Names();
  }
  ImGui::EndDisabled();
  ImGui::Checkbox("Keep spawns and effects in it", &g_bKeep);
  ImGui::Checkbox("Reveal the map", &g_sc.bReveal);
  ImGui::SameLine();
  ImGui::Checkbox("Start paused", &g_sc.bPaused);
  const std::vector<std::string> styles = DesignStyleNames();
  if (ImGui::BeginCombo("Style", g_sc.style.empty() ? "none" : g_sc.style.c_str())) {
    if (ImGui::Selectable("none", g_sc.style.empty()))
      g_sc.style.clear();
    for (const std::string &s : styles)
      if (ImGui::Selectable(s.c_str(), s == g_sc.style))
        g_sc.style = s;
    ImGui::EndCombo();
  }
  if (ImGui::CollapsingHeader("The game")) {
    GameEditor(g_sc.game);
    if (ImGui::Button("Start this game"))
      DesignCall("design_scenario", "\"launch\":true,\"game\":" + GameJson(g_sc.game), [](const PortJson &j) {
        g_said = j["ok"].b ? "starting" : j["error"].Str("refused");
      });
    ImGui::SameLine();
    ImGui::TextDisabled("its units and effects are placed on it");
  }
  if (ImGui::CollapsingHeader("Units placed")) {
    KeptList(&g_sc.units, UnitLabel, "units");
    if (!g_sc.units.empty() && ImGui::SmallButton("Clear the units"))
      g_sc.units.clear();
  }
  if (ImGui::CollapsingHeader("Effects played")) {
    KeptList(&g_sc.effects, EffectLabel, "effects");
    if (!g_sc.effects.empty() && ImGui::SmallButton("Clear the effects"))
      g_sc.effects.clear();
  }
}
