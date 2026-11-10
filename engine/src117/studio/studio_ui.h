// studio_ui.h -- what the studio's panels share (studio_main.cpp, studio_shell.cpp, studio_theme.cpp,
// studio_kit.cpp): its look, the small pieces the frame is drawn from, the record the modder has
// open, and the edits a panel asks for.
#ifndef STUDIO_UI_H
#define STUDIO_UI_H

#include <string>
#include <vector>
#include "imgui.h"
#include "../port/platform/port_json.h"

// ---- the look (studio_theme.cpp) ----------------------------------------------------------------------
// Hyperion's typefaces: Saira for text, its semibold, IBM Plex Mono for numbers, Michroma for the
// wide capitals of headings and tabs.
enum { STUDIO_FONT_TEXT, STUDIO_FONT_BOLD, STUDIO_FONT_MONO, STUDIO_FONT_WIDE, STUDIO_FONTS };
// The design's text sizes, in its pixels at 100 %.
const float kStudioFs = 14.0f;
const float kStudioFsS = 12.5f;
const float kStudioFsXs = 11.0f;
// The design's colours (design/55-looks-studio/studio.css, --ls-*), dark, and a light set.
enum StudioTok {
  TK_BG, TK_BG_TOP, TK_BAR, TK_PANE, TK_PANE2, TK_WELL, TK_LINE, TK_LINE2, TK_ROW, TK_ROW_HI, TK_SEL,
  TK_INK, TK_INK2, TK_INK3, TK_INK_OFF, TK_HI, TK_HEAD, TK_LABEL, TK_BUTTON, TK_MOD, TK_CHANGED,
  TK_CHANGED_RIM, TK_GOOD, TK_BAD, TK_WARN, TK_CLAUDE, TK_CLAUDE_RIM, TK_SEL_LINE, TK_SEL_LINE2,
  TK_BTN_TOP, TK_BTN_BOT, TK_BTN_RIM, TK_BTN_RIM_HI, TK_BTN_LIP, TK_ON_TOP, TK_ON_BOT, TK_DD_TOP,
  TK_DD_BOT, TK_DD_RIM, TK_FILL, TK_MENU, TK_MENU_RIM, TK_MENU_SEL, TK_GOLD_TOP, TK_GOLD_BOT,
  TK_GOLD_RIM, TK_GOOD_TOP, TK_GOOD_BOT, TK_BLACK, TK_SHADOW, TK_KBD_BG, TK_TAB_ON, TK_TOOL_TOP,
  TK_TOOL_BOT, TK_COUNT
};
ImU32 StudioCol(int tok, float fAlpha = 1.0f);
// A length of the design in the window's pixels: the display's scale times the modder's.
float StudioPx(float f);
// The style, once Dear ImGui starts (DesignProgram's pfnStyle), and again when the display's
// scale changes (checked each frame).
void StudioStyle(void);
void StudioStyleCheck(void);
// The UI's scale, 1 to 2 over the display's, and dark or light; each kept in the settings.
float StudioScale(void);
void StudioScaleSet(float fScale);
bool StudioLight(void);
void StudioLightSet(bool bLight);
ImFont *StudioFont(int nFont);
// A face's line height over its em: the design sizes text by its em, Dear ImGui by its line.
float StudioFontEm(int nFont);
// A face pushed at a size of the design (its em), for Dear ImGui's own text.
void StudioPushFont(int nFont, float fPx);

// ---- the pieces (studio_kit.cpp) ----------------------------------------------------------------------
// Sizes are the design's pixels; the pieces scale them. The interactive ones are laid out at
// Dear ImGui's cursor, as its own widgets are, and return true when clicked.
// A 16 x 16 stroke glyph of the design's set (cs-shell.js GL): unit, weapon, action, upgrade, tech,
// sprite, sound, look, race, home, make, table, image, cards, fx, check, game, undo, redo, push,
// search, save, try, filter, needs, note, clash, engine, done, link, dock, caret, down, x, plus,
// pause, play, again, copy, pen, back, claude.
void StudioGlyph(ImDrawList *pDraw, const char *pszGlyph, ImVec2 pos, float fSize, ImU32 col);
// Text in a face at a size, with letter spacing in ems and in capitals when asked (the wide
// headings); its width, and drawn, cut with an ellipsis at fMaxW when that is not 0.
float StudioTextW(int nFont, float fPx, const char *psz, float fTrack = 0.0f, bool bUpper = false);
float StudioText(ImDrawList *pDraw, int nFont, float fPx, ImVec2 pos, ImU32 col, const char *psz,
                 float fTrack = 0.0f, bool bUpper = false, float fMaxW = 0.0f);
// Text at the cursor, as an item: plain, in a face and colour.
void StudioLabel(int nFont, float fPx, ImU32 col, const char *psz, float fTrack = 0.0f, bool bUpper = false);
// The design's buttons (ls-btn): plain, pressed, good (green), gold, Claude's, or flat (a text
// button with no frame).
enum { SB_PLAIN, SB_ON, SB_GOOD, SB_GOLD, SB_CLAUDE, SB_FLAT };
ImVec2 StudioButtonSize(const char *pszLabel, const char *pszGlyph, float fH, float fFs = kStudioFsS);
bool StudioButton(const char *pszId, const char *pszLabel, const char *pszGlyph = nullptr, int nKind = SB_PLAIN,
                  float fH = 28.0f, float fW = 0.0f, float fFs = kStudioFsS, bool bEnabled = true);
// The segmented choice (ls-seg): the choice made, or nSel.
float StudioSegWidth(const char *const *ppszLabels, int n, float fFs = kStudioFsS);
int StudioSeg(const char *pszId, const char *const *ppszLabels, int n, int nSel, float fH = 26.0f,
              float fFs = kStudioFsS);
// A chip (ls-chip) and a key cap (cs-kbd), drawn at a place; their widths.
enum { CH_PLAIN, CH_GOOD, CH_WARN, CH_CHANGED, CH_CLAUDE, CH_GOLD, CH_BAD };
float StudioChip(ImDrawList *pDraw, ImVec2 pos, const char *psz, int nKind = CH_PLAIN, float fH = 18.0f,
                 float fFs = 9.0f);
float StudioChipW(const char *psz, float fH = 18.0f, float fFs = 9.0f);
float StudioKbd(ImDrawList *pDraw, ImVec2 pos, const char *psz, float fH = 18.0f);
// A check box with its label (ls-check).
bool StudioCheck(const char *pszId, const char *pszLabel, bool *pbOn);
// A pulldown (ls-select): its box at the cursor; true while its list is open, its items given
// with StudioItem, then StudioSelectEnd.
bool StudioSelectBegin(const char *pszId, const char *pszPreview, float fW, float fH = 26.0f, float fFs = kStudioFsS);
void StudioSelectEnd(void);
// A menu's or a list's item in the design's menu look.
bool StudioItem(const char *pszLabel, bool bSelected = false, const char *pszKeys = nullptr, bool bEnabled = true);
void StudioItemSeparator(void);
// A field to type in (ls-text), with a glyph before and a key cap after when given; true when
// its text changed. bMono for numbers, right-aligned.
bool StudioInput(const char *pszId, char *psz, int nSize, float fW, float fH, const char *pszHint = nullptr,
                 const char *pszGlyph = nullptr, const char *pszKeys = nullptr, bool bMono = false,
                 float fFs = kStudioFsS);
// A number field: true when the modder gave a new number (Enter, or leaving the field changed).
bool StudioNumber(const char *pszId, double *pd, float fW, float fH, bool bMine, float fFs = 17.0f);
// A pane's ground (ls-pane), its head (ls-pane-h) when fHead is not 0, leaving a hole when given:
// the game shows through it.
void StudioPane(ImDrawList *pDraw, ImVec2 a, ImVec2 b, float fHead = 0.0f, const ImVec4 *pHole = nullptr);
// A rectangle filled, leaving a hole (x, y, w, h) when given.
void StudioFill(ImDrawList *pDraw, ImVec2 a, ImVec2 b, ImU32 col, const ImVec4 *pHole = nullptr);
// A dashed rectangle, for a place something will go (cs-ph).
void StudioDashed(ImDrawList *pDraw, ImVec2 a, ImVec2 b, ImU32 col);
// The colours Dear ImGui draws its own widgets in, set from the tokens.
void StudioImGuiColours(void);

// ---- what the panels share (studio_shell.cpp) ---------------------------------------------------------
// The record open in the document area: its table and name, none when t is -1.
struct StudioOpenRecord {
  int t = -1;
  std::string name;
};
StudioOpenRecord &StudioCurrent(void);
// A studio command run for a panel, as the modder's ("sender": "you"); its answer's error, when it
// has one, said in the status bar and the output.
void StudioAsk(const char *pszCmd, const std::string &fields);
// The last thing worth saying in the status bar.
void StudioSay(const std::string &what);
const std::string &StudioSaid(void);
// A record opened in the document area at a view (0 its first), out of any workspace; the unsaved
// edits that touch a record or a field of it; a line in the Output tab.
void StudioOpenDoc(int t, const std::string &name, int nView = 0);
int StudioChangedSinceSave(int t, const std::string &name, const std::string &key = std::string());
void StudioLog(const std::string &level, const std::string &text);

// The field editors (studio_editors.cpp), for every kind of field fields.json names, each change a
// studio_set: a field's editor as Numbers shows it; a record of a table picked by name with a
// search (true when one is picked, "" for none); a file of the data folder picked, or brought in
// through the system's dialog into folder (true when it changes); a record's requirements in
// words; the system's file dialog opened for a purpose, and the file picked for it, once.
struct StudioField;
struct StudioValue;
void StudioFieldEditor(int t, const std::string &name, const StudioField &f, const StudioValue &v, bool bMine, bool bBig);
bool StudioRecordPick(const char *pszId, int nTable, const std::string &now, float fW, std::string *pPicked, bool bNone = true,
                      float fH = 26.0f, const char *pszEmpty = "none");
bool StudioFileField(const char *pszId, const std::string &now, const char *pszExts, const char *pszFolder, float fW,
                     std::string *pNew);
void StudioRequirements(int t, const std::string &name, const std::string &key, bool bMine, float fW);
void StudioPickFile(const char *pszFor, const char *pszWhat, const char *pszExts);
bool StudioPickedFile(const char *pszFor, std::string *pPath);

// The panes beside the shell: the Tables workspace in the document area's place
// (studio_tables.cpp); a weapon's document above its numbers (studio_weapon.cpp); the New unit
// dialog, opened from a classic unit's name or none, and drawn each frame (studio_newunit.cpp).
void StudioTables(ImVec2 a, ImVec2 b);
void StudioTablesShow(int t);  // the table Tables shows
void StudioWeaponHead(const std::string &name);
void StudioNewUnitOpen(const std::string &base);
void StudioNewUnitDraw(void);
// The New mod dialog (studio_newmod.cpp): a mod made among the installed ones and opened.
void StudioNewModOpen(void);
void StudioNewModDraw(void);
void StudioNewModInit(void);  // studio_new_mod

// ---- the areas, each a file of its own (engine/docs/modules/studio.md, "The areas") -------------------
// Workspaces, drawn in the document area's place (a, b the area): Art and its scripts
// (studio_art.cpp), Looks with the console's (studio_looks.cpp), Effects and actions
// (studio_actions.cpp), Cards and races (studio_races.cpp), Test (studio_testloop.cpp), Checks
// (studio_checks.cpp), and the mod's own page: mod.json, what it depends on, its cinematics
// (studio_modpage.cpp).
void StudioArtWorkspace(ImVec2 a, ImVec2 b);
void StudioLooksWorkspace(ImVec2 a, ImVec2 b);
void StudioEffectsWorkspace(ImVec2 a, ImVec2 b);
void StudioRacesWorkspace(ImVec2 a, ImVec2 b);
void StudioTestWorkspace(ImVec2 a, ImVec2 b);
// Each frame: the field watched while a try is going (studio_testloop.cpp).
void StudioTestTick(void);
void StudioChecksWorkspace(ImVec2 a, ImVec2 b);
// The mod's problems as Checks finds them (studio_checks.cpp): its errors and warnings, the studio's
// and the last game's check of the mod open now, their sum given back; and their rows at the
// cursor, fW wide, each opening what it is about, for the Problems tab.
int StudioCheckProblems(int *pnErrors, int *pnWarnings);
void StudioChecksList(float fW);
void StudioModWorkspace(ImVec2 a, ImVec2 b);
// A unit's views, drawn at the cursor in its document's scrolling region as Overview is, ending
// with the cursor below what they drew.
void StudioUnitActions(const std::string &unit);  // studio_actions.cpp
void StudioUnitArt(const std::string &unit);      // studio_art.cpp
void StudioUnitLook(const std::string &unit);     // studio_looks.cpp
void StudioUnitSounds(const std::string &unit);   // studio_sounds.cpp
void StudioUnitCard(const std::string &unit);     // studio_races.cpp
// Other records' heads, drawn above their numbers as a weapon's is: an image or a sprite
// (studio_art.cpp), a sound or a portrait (studio_sounds.cpp), an order (studio_actions.cpp), a
// tech or an upgrade (studio_races.cpp).
void StudioArtHead(int t, const std::string &name);
void StudioSoundHead(int t, const std::string &name);
void StudioOrderHead(const std::string &name);
void StudioTechHead(int t, const std::string &name);
// A unit's picture in a box (studio_art.cpp), at a facing (0 to 31) in a player's colour (0 to 7);
// false when there is none to draw, and the caller draws its stand-in.
bool StudioUnitPicture(ImDrawList *pDraw, ImVec2 a, ImVec2 b, const std::string &unit, int nFacing, int nColour);
// Where the picture's frames come from, in a few words ("its own sheet", "the Marine's frames").
std::string StudioUnitArtWords(const std::string &unit);
// A command icon in a box (studio_art.cpp), in the console's colours for a button that can be
// pressed or, bDisabled, one that cannot: classic cmdicons.grp's frame nIcon; or a unit's command
// icon, its own file (PNG or GRP) or the classic one it shows. False when there is none to draw.
bool StudioIconPicture(ImDrawList *pDraw, ImVec2 a, ImVec2 b, int nIcon, bool bDisabled = false);
bool StudioUnitIconPicture(ImDrawList *pDraw, ImVec2 a, ImVec2 b, const std::string &unit, bool bDisabled = false);
// A command icon picked from the classic ones: the icon picked in a box with its number, a click
// opening a grid of every icon (studio_art.cpp). True when another is picked, *pIcon -1 for none.
bool StudioIconPick(const char *pszId, int *pIcon, float fH = 26.0f, bool bEnabled = true);
// The workspace shown (0 Make, 1 Tables, 2 Art, 3 Looks, 4 Effects, 5 Cards and races, 6 Test,
// 7 Checks, 8 the mod's page), for an area that sends the modder to another.
void StudioWorkspaceSet(int nWs);
int StudioWorkspaceShown(void);  // the workspace shown, numbered as above

// The Claude panel (studio_claude.cpp): the dock's Claude tab; each frame's reading of an ask that
// runs; the count of the proposal's changes waiting, for the tab and the toolbar; whether an ask
// runs; the keyboard given to its words; its commands.
void StudioClaudeTab(ImVec2 a, ImVec2 b);
void StudioClaudeTick(void);
int StudioClaudeBadge(void);
bool StudioClaudeRunning(void);
void StudioClaudeFocus(void);
void StudioClaudeInit(void);

// Each area's start, called once after the model's and the shell's: where an area registers the
// commands of its own (PortControlCommand).
void StudioRacesInit(void);
void StudioArtInit(void);
void StudioSoundsInit(void);
void StudioLooksInit(void);
void StudioActionsInit(void);
void StudioTestInit(void);
void StudioChecksInit(void);
void StudioModPageInit(void);

// The frame (studio_shell.cpp): the menu bar's keys, the whole window, and the dockspace left empty.
void StudioShellInit(void);  // its commands (studio_show)
void StudioMenuBar(void);
void StudioPanels(void);
void StudioLayout(ImGuiID dock, const ImVec2 &size);

// The Strings workspace (studio_strings.cpp): every text of the mod in one place, searched and
// edited; its commands.
void StudioStringsWorkspace(ImVec2 a, ImVec2 b);
void StudioStringsInit(void);
// Ctrl+F while Strings is shown: its search, or its find when find and replace is open.
void StudioStringsFocusFind(void);
// The Balance workspace (studio_balance.cpp): the mod's units against each other and classic ones,
// worked out; its commands.
void StudioBalanceWorkspace(ImVec2 a, ImVec2 b);
void StudioBalanceInit(void);
// A race tried on a real map, and scenarios of the modder's own (studio_racetest.cpp): its commands.
// The race page draws its part (StudioRaceTestPanel) for the race shown.
void StudioRaceTestPanel(const std::string &race, float fW);
void StudioRaceTestInit(void);
// A game started the way the race page's Play it starts one, with more words on its command line
// (the Mode workspace's Test: --mode and its settings); false with why. The game the studio has
// started, its map and its channel's port, or null when none is playing.
bool StudioPlayWith(const std::string &race, const std::string &map, int nComputers, const std::string &computerRace,
                    const std::string &extra, std::string *pWhy);
const char *StudioPlayingWith(int *pnPort);
// The trace of what a mode's scripts decide in that game (the channel's script_trace), while an
// area asks for it: the rows that have come in since the last read, as JSON members in a row.
void StudioTraceWant(bool bOn);
bool StudioTraceRows(std::string *pRows);
// The Statuses workspace (studio_statuses.cpp): a mod's statuses, what each changes and how it
// looks; the action blocks that apply them; its commands.
void StudioStatusesWorkspace(ImVec2 a, ImVec2 b);
void StudioStatusesInit(void);
// The Tilesets workspace (studio_tilesets.cpp): a mod's tileset made on a classic one; its commands.
void StudioTilesetsWorkspace(ImVec2 a, ImVec2 b);
void StudioTilesetsInit(void);
// A mod's strings in each language (studio_languages.cpp): the Strings workspace's languages; its
// commands.
void StudioLanguagesInit(void);
// Its problems for the Problems tab: each language's missing and stale strings, a click showing them
// in Strings (StudioLanguagesShow).
struct StudioLanguageProblem {
  const char *pszKind;  // "note" or "needs"
  std::string text;
  std::string code;     // the language
};
std::vector<StudioLanguageProblem> StudioLanguagesProblems(void);
void StudioLanguagesShow(const std::string &code);
// A race's building rules and its advisor (studio_buildrules.cpp): the race page draws its part for
// the race shown; its commands.
void StudioBuildRulesPanel(const std::string &race, float fW);
void StudioBuildRulesInit(void);
// The Scripts workspace (studio_scripts.cpp): the mod's Lua, written, checked as the loader checks
// it and tried in the test game; Ctrl+F while it shows; its commands. A script block's function
// opened in it (the action editor's "open it").
void StudioScriptsWorkspace(ImVec2 a, ImVec2 b);
void StudioScriptsFocusFind(void);
void StudioScriptsOpenFunction(const std::string &function);
void StudioScriptsInit(void);
// The functions the mod's scripts give, as the action editor's script block picks one.
std::vector<std::string> StudioScriptFunctions(void);
// The Mode workspace (studio_modes.cpp): a mod that is a game mode, its settings and presets
// written, and its generate rolled to see a run; its commands.
void StudioModesWorkspace(ImVec2 a, ImVec2 b);
void StudioModesInit(void);

#endif
