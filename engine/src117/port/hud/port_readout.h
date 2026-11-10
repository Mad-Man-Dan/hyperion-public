// port_readout.h -- a mod's unit readout: classic's single-unit status panel as parts a mod may
// set and add to (docs/mod-data.md, "readout.json").
//
// Classic draws one unit's panel in the StatData dialog: the wireframe, the name, the status
// line, the kills, up to four stat icons, the life and the energy. Hyperion names those seven
// parts, and a mod's readout.json changes the parameters it wants and leaves the rest. With no
// mod readout the panel is classic's, pixel for pixel: nothing here runs.
//
// One drawing serves both consoles. The readout is drawn into the panel's own picture, which the
// classic console puts on the screen where it always does and a HUD design's "selection" slot
// shows as its pixels (port_console.h, PORT_HUD_SRC_SELECTION). So a mod's readout shows whichever
// console the player chose, and neither console needs to know the format.
//
// Presentation only, every part of it: the file is outside the mod's fingerprint, the hook reads
// the game and never sets it, a replay draws from the same values, and nothing here is in the
// state hash or a save.
#ifndef PORT_READOUT_HEADER
#define PORT_READOUT_HEADER

#include <string>
#include <vector>

class CUnit;
struct PortJson;

// ---- the readout's own pixels -------------------------------------------------------------------
// The readout is laid out in the StatData dialog's own coordinates: 270 by 92 game pixels, at
// screen (138, 388) on the 640 by 480 console (rez\statdata.bin, src117/gen/layouts117.cpp). A
// HUD slot taller than 92 gives the readout spare rows under the classic parts, which is where
// what a mod adds goes; on the classic console the same additions take the room classic leaves
// free, which is why the lines it adds share a line with the kills.
#define PORT_READOUT_W 270
#define PORT_READOUT_H 92

// Classic's seven parts, in the order statdraw draws them.
enum PortReadoutPart {
  PORT_READOUT_WIRE,    // the wireframe, tinted part by part as the unit is hurt
  PORT_READOUT_NAME,    // the unit's name
  PORT_READOUT_STATUS,  // classic's one line under the name: a hallucination, disabled, acid
                        // spores, blind, a parasite, or the unit's rank
  PORT_READOUT_KILLS,   // "Kills: n", for a unit that can attack or has killed
  PORT_READOUT_SLOTS,   // up to four icons: armour, shields, the ground and air weapons, then
                        // interceptors, mines or a nuke, each with its upgrade level
  PORT_READOUT_LIFE,    // shields and life as now/most, coloured by the damage state
  PORT_READOUT_ENERGY,  // energy as now/most, or a hallucination's time left
  PORT_READOUT_PARTS
};
// How many stat icons the slots part holds.
#define PORT_READOUT_SLOTS_MOST 4

struct PortReadoutRect {
  int x = 0, y = 0, w = 0, h = 0;
};

// A part's name in readout.json ("wire", "name", "status", "kills", "slots", "life", "energy"),
// and the part of that name (-1 for one no part has). "lines" is the row the lines a mod adds are
// read on, which is the kills' row: it is the name the design writes in an effect, and a file
// written from the design's own JSON names it.
const char *PortReadoutPartName(int nPart);
int PortReadoutPartOf(const char *pszName);
// Where classic puts a part, in the readout's own pixels, and which control of the panel draws
// it. Measured from rez\statdata.bin, not from the console's art:
//   wire    control 1    (30, 0)  64 x 64   statwire_update
//   name    control -5   (102, 0) 152 x 14  statdraw_draw_unit_name
//   status  control -20  (106, 16) 144 x 14 statdraw_draw_properties
//   kills   control -21  (106, 30) 144 x 14 statdraw_draw_kills
//   slots   controls 9 to 12, (104, 51) 36 x 36 each, 39 apart   statdraw_draw_stats
//   life    control -7   (22, 64) 80 x 12   statdraw_draw_hp
//   energy  control -8   (22, 76) 80 x 12   statdraw_draw_mana
// The slots part's rect covers all four; PortReadoutSlotRect is one of them, 0 to 3.
PortReadoutRect PortReadoutPartRect(int nPart);
PortReadoutRect PortReadoutSlotRect(int nSlot);
int PortReadoutPartControl(int nPart);

// ---- readout.json -------------------------------------------------------------------------------
// The data folder, or the mod's own folder when it names none, may hold readout.json, read as
// each game's console loads, as hud.json is. Every key may be left out; what is left out or
// wrong stays classic's and is named in hyperion_mods.log with the line it was on.
//
//   {
//     "readout": {
//       "from": "classic",
//       "in_huds": true,
//       "parts":  { ... the seven parts, each key an override ... },
//       "add":    { "lines": { ... } },
//       "ranks":  { "elite": "#f2c94c", ... },
//       "fx":     [ { "on": "damaged", "part": "wire", "do": "flash", "time": 0.18 }, ... ]
//     }
//   }
//
// A colour is "#rrggbb", or one of the names a part takes ("classic", "rank", "team", "life",
// "damage"); an "#rrggbb" is drawn as the nearest entry of the game's palette, since the panel's
// picture is palettized.
//
// Text is a pattern: a value of the readout in braces, and anything else as written. The values
// are {name}, {rank}, {kills}, {life}, {life_max}, {shields}, {shields_max}, {energy},
// {energy_max}, {armor}, {level}, {stat}, {value}, {title}, and, for a line that names a status,
// {left}, {stacks} and {effects}. A brace naming nothing is left as it was written and said once
// in the log.

// The wireframe's tint.
enum PortReadoutTint { PORT_READOUT_TINT_LIFE, PORT_READOUT_TINT_RANK, PORT_READOUT_TINT_TEAM, PORT_READOUT_TINT_NONE };
// A mark round the wireframe. None is classic's.
enum PortReadoutFrame { PORT_READOUT_FRAME_NONE, PORT_READOUT_FRAME_RANK, PORT_READOUT_FRAME_CORNERS };
// Which lines the status part shows. Classic's own statuses keep their place first, so a parasite
// is never hidden by a mod's line.
enum PortReadoutLines { PORT_READOUT_LINES_CLASSIC, PORT_READOUT_LINES_BOTH, PORT_READOUT_LINES_MOD };
// Whether a part is drawn at all. Auto is classic's rule for that part.
enum PortReadoutShow { PORT_READOUT_SHOW_AUTO, PORT_READOUT_SHOW_ALWAYS, PORT_READOUT_SHOW_NEVER };
// Where a rolled change shows on a stat icon.
enum PortReadoutDelta { PORT_READOUT_DELTA_OFF, PORT_READOUT_DELTA_ICON, PORT_READOUT_DELTA_TIP };
// A colour that is not a number: which value it comes from.
enum PortReadoutInk { PORT_READOUT_INK_CLASSIC, PORT_READOUT_INK_RANK, PORT_READOUT_INK_TEAM, PORT_READOUT_INK_RGB,
                      PORT_READOUT_INK_LIFE, PORT_READOUT_INK_DAMAGE };
// How text sits in its part.
enum PortReadoutAlign { PORT_READOUT_ALIGN_CENTRE, PORT_READOUT_ALIGN_LEFT };

struct PortReadoutColour {
  int nInk = PORT_READOUT_INK_CLASSIC;
  unsigned int nRgb = 0xFFFFFFu;  // with PORT_READOUT_INK_RGB
};

// "parts": each part's parameters, classic's value first. A part's block names only what it
// changes.
struct PortReadoutParts {
  // "wire": { "tint", "states", "frame", "frame_colour", "art" }
  int nTint = PORT_READOUT_TINT_LIFE;
  int anStates[2] = {66, 33};  // where the wireframe turns yellow and red, percent of life
  int nFrame = PORT_READOUT_FRAME_NONE;
  PortReadoutColour frameColour;
  bool bModArt = false;  // the mod's own wireframes (ModConsoleGroup) rather than the unit's

  // "name": { "text", "colour", "align" }
  std::string name = "{name}";
  PortReadoutColour nameColour;
  int nNameAlign = PORT_READOUT_ALIGN_CENTRE;

  // "status": { "lines", "rotate", "rank_pips" }
  int nStatusLines = PORT_READOUT_LINES_CLASSIC;
  double fRotate = 2.5;    // with more than one line, how long each shows, in seconds
  bool bRankPips = false;  // the rank as pips beside the line, and in the group view

  // "kills": { "show", "text" }
  int nKillsShow = PORT_READOUT_SHOW_AUTO;
  std::string kills = "{title} {kills}";  // classic's own, its word from the game's text

  // "slots": { "order", "level", "delta", "tooltip" }
  std::vector<std::string> slots;  // "armor", "shields", "weapon_ground", "weapon_air",
                                   // "special", then a mod's own names; empty is classic's order
  std::string level = "{level}";
  int nDelta = PORT_READOUT_DELTA_OFF;
  std::vector<std::string> tooltip;  // a line each; empty is classic's tooltip

  // "life": { "text", "colour" }
  std::string life = "{shields}/{shields_max} {life}/{life_max}";
  PortReadoutColour lifeColour;  // classic's is the damage state: yellow smouldering, red burning

  // "energy": { "text", "show" }
  std::string energy = "{energy}/{energy_max}";
  int nEnergyShow = PORT_READOUT_SHOW_AUTO;  // classic shows it for your own casters only
};

// "add": { "lines": ... } -- the lines a mod adds past classic's parts. On the classic console
// they take the kills' line in turn with it; in a HUD slot with the room they each take a line of
// their own.
struct PortReadoutAdd {
  bool bLines = false;
  std::string each = "{name} {left}s {effects}";
  // "each_short": the same line where the room is short. On the classic console a line shares the
  // kills' row of 144 pixels, where a line naming a status and all it does is half as long again;
  // with none the status's own words are used there and cut on a word's boundary.
  std::string eachShort;
  bool bBar = true;        // a bar for a status's time left
  bool bLinePerStatus = true;  // in a HUD slot with the room, a line each rather than in turn
};

// One rank the hook may name, in the order the file lists them: a rank's place is what carries it
// where its colour cannot, since the classic panel draws in the palette's nearest entry and two
// ranks may land on one. name is what a player reads ("Elite"), the id when the file gives none.
struct PortReadoutRank {
  std::string id, name;
  unsigned int nRgb = 0xFFFFFFu;
};

// ---- how a status shows itself -------------------------------------------------------------------
// A status falls into one of fourteen kinds, worked out from its own fields unless its "look" names
// one, strongest first: what stops the unit, then what hurts it, then the rest. A kind carries the
// glyph drawn for it, its hue, and the classic command icon that says the same thing.
enum PortReadoutKind {
  PORT_KIND_ROOT,        // cannot move
  PORT_KIND_DISARM,      // cannot attack
  PORT_KIND_DOT,         // damage over time
  PORT_KIND_HEAL,        // heals
  PORT_KIND_ARMOR_DOWN,  // less armour
  PORT_KIND_ARMOR_UP,    // more armour
  PORT_KIND_HASTE,       // attacks faster
  PORT_KIND_JAM,         // attacks slower
  PORT_KIND_SLOW,        // slower
  PORT_KIND_SWIFT,       // faster
  PORT_KIND_MIGHT,       // deals more
  PORT_KIND_WEAK,        // deals less
  PORT_KIND_FRAIL,       // takes more
  PORT_KIND_TOUGH,       // takes less
  PORT_READOUT_KINDS
};
// A kind's name in a mod's files ("root", "armor_up"), what a player reads ("Can't move"), its own
// hue, the pattern it lays over a wireframe, and the classic icon that says the same thing.
const char *PortReadoutKindName(int nKind);
const char *PortReadoutKindWords(int nKind);
unsigned int PortReadoutKindHue(int nKind);
const char *PortReadoutKindPattern(int nKind);
int PortReadoutKindIcon(int nKind);
int PortReadoutKindOf(const char *pszName);  // -1 for a name no kind has
// The kind a status comes to: the one its "look" names (nKindNamed, -1 for none), else the first
// its own fields match, strongest first. This is the rule itself, taking the fields rather than a
// status, so a game, a check and a tool showing a definition with no game all ask the same
// question and cannot drift (ModStatusKindOfDef).
int PortReadoutKindFrom(int nKindNamed, bool bNoMove, bool bNoAttack, int nDps, int nRegen, int nArmor,
                        int nCooldown, int nSpeed, int nDamage, int nTaken);
// How strongly a kind asks for the one mark a wireframe has room for: the higher, the sooner. The
// order is what stops the unit, then what hurts it, then the rest; among equals the least time
// left wins. A status's "look" may give its own.
int PortReadoutKindPriority(int nKind);

// A kind's glyph, drawn rather than art, so it shows in the panel's eight bits and in a HUD's
// full colour from one description. It is a run of strokes in a 16 by 16 square: each stroke is
// its count of points and then that many x, y pairs, and the run ends with a count of 0. The
// curves of the drawing are written as short straight runs, which is what they come to at this
// size. Both consoles walk the same table (PortConsoleRollMarks, GridMarks).
const signed char *PortReadoutGlyph(int nKind);

// ---- the effects --------------------------------------------------------------------------------
// An effect is one op on one part, set off by an event or held while a test is true. Every one is
// carried by light: no part shakes, bounces or changes size, by the user's rule for the HUD
// (2026-10-06). Only text moves, and only in the ops marked as motion.
enum PortReadoutEvent {
  PORT_READOUT_SELECTED,      // the unit became the one the panel describes
  PORT_READOUT_DAMAGED,       // it lost life or shields, by that much
  PORT_READOUT_HEALED,        // it gained them
  PORT_READOUT_STATUS_ADDED,  // a status was put on it
  PORT_READOUT_STAT_CHANGED,  // a status moved one of its stats
  PORT_READOUT_KILL,          // it killed something
  PORT_READOUT_EVENTS
};
// The ops. Those marked motion are turned off by the player's Reduce motion.
enum PortReadoutOp {
  PORT_READOUT_FLASH,    // the part flashes a colour for a time
  PORT_READOUT_FLOAT,    // text rises off the part and fades (motion)
  PORT_READOUT_ROLL,     // the numbers count to their new value rather than jumping
  PORT_READOUT_SLIDE,    // a line slides in, with a glow in its side's colour (motion)
  PORT_READOUT_LIGHT,    // the part lights up, with a glint across it if asked
  PORT_READOUT_BLINK,    // the part blinks at a rate, capped at 3 a second
  PORT_READOUT_SCAN,     // a line runs down the part (motion)
  PORT_READOUT_TYPE,     // text types itself in, a letter at a time (motion)
  PORT_READOUT_BEAT,     // the part beats in light at a rate
  PORT_READOUT_GLOW,     // the part glows, at a size and a rate
  PORT_READOUT_SHIMMER,  // light runs across text every so often (motion)
  PORT_READOUT_OPS
};
// What holds an effect on, for "while".
enum PortReadoutWhile {
  PORT_READOUT_WHILE_NONE,
  PORT_READOUT_WHILE_LIFE,     // life below fAt, as a part of its most
  PORT_READOUT_WHILE_RANK,     // the rank at or above the one named
  PORT_READOUT_WHILE_STATUS    // a status with less than fAt seconds left
};
// The name of an op, an event and a "while" test, for the file and the log.
const char *PortReadoutOpName(int nOp);
const char *PortReadoutEventName(int nEvent);
// Whether an op moves text, which Reduce motion turns off.
bool PortReadoutOpMoves(int nOp);

// One effect of "fx". Either an event ("on", one or several) or a test ("while"), never both.
struct PortReadoutFx {
  int nOp = PORT_READOUT_FLASH;
  int nPart = PORT_READOUT_WIRE;
  std::string slot;    // "slots.{stat}": the icon of the stat the event names, "" for the part
  bool abOn[PORT_READOUT_EVENTS] = {false, false, false, false, false, false};
  int nWhile = PORT_READOUT_WHILE_NONE;
  double fAt = 0;      // "while"'s number: a part of the life, or the seconds left
  std::string rank;    // "while": the rank at or above which it holds
  std::string ifRank;  // "if": "rank" draws it only for a unit the hook gave a rank
  PortReadoutColour colour;
  std::string text;    // float's and type's pattern
  double fTime = 0.25; // how long one run takes, in seconds
  double fHold = 0;    // how long it stays after that
  double fRate = 1.0;  // blink's and beat's rate a second; shimmer's "every", in seconds
  double fEvery = 0;
  double fSize = 0;    // glow's size, in readout pixels
  double fLight = 0.5; // beat's and light's depth
  bool bGlint = false;
  bool bGlow = false;
  bool bFromRight = true;  // slide's side
  bool bLines = false;     // its part was written "lines": wherever a status's line is drawn, which
                           // is the room's rows and, on the panel, the row showing that line
};

// ---- the runs, for the drawing -------------------------------------------------------------------
// What is running on a part now. An effect an event set off runs for its own time and then holds
// for as long as it asked; one with a "while" test runs while the test is true and has no
// beginning of its own, so its light is read off the readout's clock. The drawing asks for a part
// and draws the light each run carries; it reads and changes nothing.
struct PortReadoutRun {
  int nOp = PORT_READOUT_FLASH;
  bool bHeld = false;      // held by a "while" test rather than set off by an event
  double fThrough = 0;     // 0 to 1 over the run's own time, 1 while it is only holding
  double fAge = 0;         // seconds since it began; 0 for a held run
  double fPhase = 0;       // the seconds a rate counts in: the age, or the clock while it is held
  bool bColour = false;    // the effect named a colour of its own; else the part's own is used
  unsigned int nRgb = 0xFFFFFFu;
  double fTime = 0.25, fHold = 0, fRate = 1.0, fEvery = 0, fSize = 0, fLight = 0.5;
  double fAt = 0;          // a held test's own figure: the part of the life, or the seconds left,
                           // so a drawing that has several lines in front of it can tell which of
                           // them the test is holding for. 0 for a run an event set off.
  bool bGlint = false, bGlow = false, bFromRight = true;
  int nAmount = 0;         // the life a hit took or gave, or the stat a status moved
  char szText[64] = {0};   // float's and type's words, its pattern filled in
  char szStatus[32] = {0}; // the status that set it off, for the line that slides in
};
// At most this many runs on one part are given to the drawing at once.
#define PORT_READOUT_RUNS_MOST 6
// The runs on a part now, the one that says most first: what an event set off before what a test
// holds, and the newest of those first. pszSlot names a stat for one of the four icons ("armor"),
// null or "" for the part itself. The player's Reduce motion is already applied, so a motion op
// comes back as the light it carries or not at all, and a blink's rate is capped.
int PortReadoutRuns(int nPart, const char *pszSlot, PortReadoutRun *paOut, int nMost);

// "status_look": where and how a status shows itself. The values are the user's picks: a framed
// icon before the name, line 3 taken in turn, a corner mark on the wireframe, glyphs in the group
// view.
enum PortReadoutIconStyle { PORT_READOUT_ICON_FRAMED, PORT_READOUT_ICON_GLYPH, PORT_READOUT_ICON_GAME,
                            PORT_READOUT_ICON_NONE };
enum PortReadoutLine3 { PORT_READOUT_LINE3_TURN, PORT_READOUT_LINE3_ROW };
enum PortReadoutWireMark { PORT_READOUT_WIRE_CORNER, PORT_READOUT_WIRE_TINT, PORT_READOUT_WIRE_PATTERN,
                           PORT_READOUT_WIRE_MARK_NONE };
enum PortReadoutGroupMark { PORT_READOUT_GROUP_GLYPHS, PORT_READOUT_GROUP_DOTS, PORT_READOUT_GROUP_EDGE,
                            PORT_READOUT_GROUP_MARK_NONE };
struct PortReadoutStatusLook {
  int nIcon = PORT_READOUT_ICON_FRAMED;    // a glyph in a frame: a disc for a status that helps
                                           // the unit, a diamond for one that hurts, the time
                                           // left running out round it and the stacks in a corner
  int nLine3 = PORT_READOUT_LINE3_TURN;    // the statuses take line 3 in turn with the kills
  int nWireMark = PORT_READOUT_WIRE_CORNER;  // the most telling status's icon in the wireframe's
                                             // top left, the next smaller under it, then +n
  int nGroupMark = PORT_READOUT_GROUP_GLYPHS;  // up to three glyphs down a group tile's left
};

// The file read.
struct PortReadoutFile {
  bool bOn = false;      // the mod gave a readout; false leaves the panel classic's
  bool bInHuds = true;   // a HUD design's selection slot shows it too
  PortReadoutParts parts;
  PortReadoutAdd add;
  PortReadoutStatusLook statusLook;
  std::vector<PortReadoutRank> ranks;
  std::vector<PortReadoutFx> fx;
};

// The game's readout.json read again as each console loads (PortConsoleUpdate): the mode's when
// it has one, else the active mod's, left off when neither gives one. What is wrong is left
// classic's and named in hyperion_mods.log.
void PortReadoutLoad(void);
// The file in force, or null for classic's own panel.
const PortReadoutFile *PortReadoutOf(void);
// readout.json's text checked on its own, for --mod-check and the Studio: false with why, which
// names the key and the line. pFile takes what did read.
bool PortReadoutCheck(const std::string &text, PortReadoutFile *pFile, std::vector<std::string> *pWarnings,
                      std::string *pWhy);

// ---- the hook -----------------------------------------------------------------------------------
// readout(unit): what the readout shows of a unit, or nil for classic's own panel. Called in the
// presentation VM, a state of its own built from the same scripts with no way to set the game, so
// what it returns can differ between viewers without the game going its own way. It is called for
// a unit the viewer sees, when the selection changes and when the unit's own values do -- not
// every frame.
//
//   function readout(unit)
//     return {
//       name = "...",          -- the name the readout shows
//       rank = "elite",        -- a rank of the layout's "ranks", which gives its colour
//       lines = { "...", { status = "z_gorged" }, { text = "...", colour = "#rrggbb" } },
//       deltas = { armor = 4, damage = 24, cooldown = -8, life = 18, speed = 6 },
//       tooltips = { life = { "...", "..." } },
//     }
//   end
//
// A line that names a status takes the status's name, its side, its time left and its stacks from
// the status's own definition, and its timer runs between calls, so a status counting down costs
// no call. A line's own text is a pattern, as a part's is.
struct PortReadoutLine {
  std::string text;    // the line as written, a pattern; empty for a status's own words
  std::string status;  // the status it names, "" for none
  PortReadoutColour colour;
  // filled in from the status's definition and the unit's state
  std::string label;
  bool bGood = false;  // its side: a status that helps
  double fLeft = 0, fOf = 0;  // seconds left and of how many; 0 for a status with no time of
                              // its own, which draws no bar and never blinks
  int nStacks = 0;
  int nIcon = -1;      // its own icon, a cmdicons frame, -1 for none
  int nKind = PORT_KIND_MIGHT;  // its kind, for the glyph and the hue
  unsigned int nHue = 0xFFFFFFu;
  bool bHue = false;   // the status named a colour of its own, rather than leaving it to its kind
                       // or to its side: what is drawn in its own colour takes nHue only then
  std::string effect;  // what it does, in words ("+3 armour, -35% speed")
  int nPriority = 0;   // which status the one mark on the wireframe shows
  bool bShowWire = true, bShowGroup = true;  // its look may keep it off the wireframe's mark or
                                             // the group view, as a born trait is kept off both
};
// A stat the deltas may name. Each has an icon of the panel's that can carry it, shields
// included, so a mod that rolls shields can show what it rolled.
enum PortReadoutStat { PORT_READOUT_STAT_ARMOR, PORT_READOUT_STAT_SHIELDS, PORT_READOUT_STAT_DAMAGE,
                       PORT_READOUT_STAT_COOLDOWN, PORT_READOUT_STAT_LIFE, PORT_READOUT_STAT_SPEED,
                       PORT_READOUT_STATS };
const char *PortReadoutStatName(int nStat);
// What a player reads for that stat ("Armour"), for a line that names a rolled change.
const char *PortReadoutStatWords(int nStat);
// Whether a stat reads better the lower it is, so what shows a change knows which way is good:
// true for the cooldown alone, where -8 is a faster attack.
bool PortReadoutStatLowerIsBetter(int nStat);
// Whether a stat's change is a count rather than a part of the base: true for the armour alone,
// so +4 armour is not read as +4 per cent.
bool PortReadoutStatIsCount(int nStat);
// Which stat one of the panel's four icons carries for this unit, or -1 for one that carries
// none: the panel fills the icons in classic's order and leaves out what the unit has not got, so
// which stat an icon shows depends on the unit.
int PortReadoutSlotStat(const CUnit *pUnit, int nSlot);

struct PortReadoutTip {
  std::string stat;
  std::vector<std::string> lines;
};
// What the hook gave for one unit, with the engine's own values filled in.
struct PortReadoutView {
  bool bOn = false;    // the hook gave a readout; false is classic's panel for this unit
  std::string name;
  std::string rank;
  std::string rankName;  // what a player reads for that rank
  int nRankAt = -1;      // its place in the file's ranks, -1 for a rank the file does not name
  unsigned int nRankRgb = 0xFFFFFFu;
  std::vector<PortReadoutLine> lines;
  bool abDelta[PORT_READOUT_STATS] = {false, false, false, false, false, false};
  int anDelta[PORT_READOUT_STATS] = {0, 0, 0, 0, 0, 0};
  std::vector<PortReadoutTip> tips;
};
// The readout of the unit the panel describes, or null for classic's panel: the hook's answer,
// called again when the selection or the unit's values change. Presentation: a replay and a
// watcher get the same answer from the same game.
const PortReadoutView *PortReadoutUnit(const CUnit *pUnit);
// Each frame, before the panel is drawn: the statuses' time left brought up to date and the
// effects' clocks moved on. Nothing of it is read by the game.
void PortReadoutFrame(void);
// For each game, after ModStatusForGame: the presentation VM built for a game that has a mod with
// a readout, and every unit's view and effects dropped.
void PortReadoutForGame(void);

// ---- the events ---------------------------------------------------------------------------------
// The events are watched, not raised: PortReadoutFrame reads the one unit the panel describes
// each game frame and compares it with the frame before, so nothing in the game calls this and a
// presentation-only readout needs no hook in CUnit::Damage. Several hits in one frame come out as
// one event, which is what a flash wants. nAmount is the life a hit took or gave, the stat a
// status moved (PortReadoutStat) or 0.
void PortReadoutEvent(const CUnit *pUnit, int nEvent, int nAmount);
// hx.readout.fx(unit, op, args): an effect a script sets off itself, at a moment the engine has
// no event for. The same ops and the same caps; it draws for this viewer only and the call is
// kept out of the game. False with why for an op or a part no name has.
bool PortReadoutScriptFx(const CUnit *pUnit, const char *pszOp, const PortJson &args, std::string *pWhy);

// ---- the player's own ---------------------------------------------------------------------------
// Settings' Reduce motion ("hyperion"/"reduceMotion", the access tab): the ops that move text are
// not drawn and a flash is half as bright. A blink is capped at 3 a second whichever way it is
// set. A mod cannot override either.
bool PortReadoutReduceMotion(void);

// ---- the drawing --------------------------------------------------------------------------------
// The room the readout has, in its own pixels: PORT_READOUT_W by PORT_READOUT_H on the classic
// console, and a HUD slot's own size where that is larger (PortHudReadoutRoom). Set between
// frames, each clamped up to the panel's own size. A slot fits the panel by the smaller of its
// two scales, so most designs give spare columns rather than spare rows: the lines a mod adds go
// into the columns right of PORT_READOUT_W where there are any, and else into the rows under
// PORT_READOUT_H, as the design has it.
void PortReadoutRoomSet(int nWidth, int nHeight);
void PortReadoutRoom(int *pnWidth, int *pnHeight);
// That room as a rectangle in the readout's own pixels, the lines a mod adds going in it: beside
// classic's panel where a slot gives spare columns, under it where it gives spare rows. False
// where there is none to write in, which is the classic console and a slot no larger than the
// panel. The words and the drawing both ask, so neither can think the room is somewhere else.
bool PortReadoutRoomBox(double *pfX, double *pfY, double *pfW, double *pfH);
// Whether that room lists the statuses itself: there is room for a row each and the file asks
// for one ("in_huds": "a line each"). The panel's own rows then leave the statuses out, so none
// is drawn in two places; with "in turn" the room lists the hook's lines only and the kills' row
// rotates the statuses as it does on the classic console.
bool PortReadoutRoomLists(void);
// Whether the readout draws the panel now: a mod gave one, the hook gave a readout for the unit
// the panel describes, and the selection is one unit. False leaves every part classic's.
bool PortReadoutDraws(void);

// The words a text part shows, for the control classic draws it with (statdraw.cpp): the name,
// the status line, the kills, the life and the energy. The readout writes its own words into
// classic's own controls, so the panel keeps classic's font, places and spacing, and a part the
// mod leaves alone answers null and is drawn as it always was. An empty answer keeps the part
// off altogether. pszClassic is the words classic would show, which a pattern may keep
// ({title}); it may be null.
//
// The answer carries classic's own colour code (gds_font.cpp, font_set_color_tbl) nearest the
// colour the mod asked for, since the panel's picture is palettized. The pointer is the
// readout's own buffer, one for each part, and holds until that part is asked for again, which
// is what the control wants: it keeps what it is given and draws from it later.
const char *PortReadoutPartText(int nPart, const char *pszClassic);
// The status whose line the kills' row shows now, null for the kills' own line and for a part
// that is not rotating a status: the drawing puts that status's icon at the line's start, so the
// words and the icon are always the same status (PortReadoutPartText settles it as it answers).
const PortReadoutLine *PortReadoutRowLine(void);
// One line's words, as the panel writes them: the line's own text where it has some, else the
// pattern of "add.lines" ("each", or "each_short" with bShort, for a row no wider than classic's),
// else the status's own name with its stacks and the time left. Both patterns go through the
// readout's values, so a line reads the same in a HUD's room as on the panel. The answer is the
// readout's own buffer and holds until the next call.
const char *PortReadoutLineText(const PortReadoutLine *pLine, bool bShort);
// The words the panel was last given for a part, without the colour code, "" for a part that has
// none: the drawing measures them to put the rank's pips just before the line they belong to,
// since the panel centres a line and only the words say where its left edge is.
const char *PortReadoutPartWords(int nPart);
// The kills' row rotation as the words settled it: which line of how many is showing, how long it
// has been showing and how long each one shows. False where the row is not rotating, where it
// holds one line only, and while a status that has just landed is holding the row out of turn --
// the three cases the design draws no turn index and no fade for.
bool PortReadoutRowTurn(int *pnAt, int *pnOf, double *pfPhase, double *pfEach);

// The readout's words for one of the panel's stat icons, nSlot 0 to 3: the lines it adds under
// classic's own, or the whole tooltip where the readout gives one, the patterns of "tooltip"
// already filled in and the rolled change among them when "delta" is "in the tooltip". False
// leaves classic's own tooltip, which is what a unit with no readout gets.
bool PortReadoutSlotTip(const CUnit *pUnit, int nSlot, std::string *pOut, bool *pbWhole);

// The readout's words for a part's own tooltip, for the parts classic gives none: the wireframe
// carries the life, and the name what the unit was rolled from with every change it rolled, one
// to a line. The panel's four icons are the armour, the shields and the two weapons, so three of
// the six stats a hook may name have no icon to carry them -- the life, the move speed and the
// attack's cooldown, which the weapon's own icon answers for. False where there is nothing to
// say beyond what the panel already shows.
bool PortReadoutPartTip(const CUnit *pUnit, int nPart, std::string *pOut, bool *pbWhole);

#endif
