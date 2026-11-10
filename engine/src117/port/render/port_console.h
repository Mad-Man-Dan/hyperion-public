// port_console.h -- the console laid out as Remastered's HD one, and the units' wireframes
// (port_console.cpp).
#ifndef PORT_CONSOLE_H
#define PORT_CONSOLE_H

#include <vector>
#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The console laid out as Remastered's HD one (port_console.cpp): its regions toward the
// screen's edges while the frame shows the HD console. PortConsoleArtLoaded reads
// 1.17's image from status_load_console, its skin as PortGpuConsoleArt's; PortConsoleFreed is
// status_free's. PortConsoleShift is a dialog's console origin: nConsoleX, or its
// region's while laid out at the edges. PortConsoleArtX is where the image
// status_get_console_art copies from starts. PortConsoleUpdate lays it out between
// frames (RunGameLoop), PortConsoleUnsplit back as 1.17's (before the view changes).
struct ctrl_t;
void PortConsoleArtLoaded(const unsigned char *pPixels, int nWidth, int nHeight, int nSkin);
void PortConsoleFreed(void);
int PortConsoleShift(const struct ctrl_t *pDlg);
int PortConsoleArtX(void);
int PortConsoleSplit(void);
void PortConsoleUnsplit(void);
void PortConsoleUpdate(void);
// A benchmark's run shows no console: its dialogs hidden, its clicks the map's.
bool PortConsoleHidden(void);
// Whether a mask node is the hidden console's, which the screen's mask leaves out (gds_vidblit.cpp).
bool PortConsoleMaskLeftOut(intptr_t nMask);

// The HUD's console (port_hud.h): with a HUD design on (PortHudPlays as the console
// loads), 1.17's console art and its art panels are not drawn and the map has the view's whole
// height. The console's dialogs live on, each drawing into its own bitmap as in 1.17, without the
// art under it (PortConsoleHudBlank, status_get_console_art's), for the HUD's slots to show
// (PortConsoleHudSource); a held dialog is not put on the screen (sub_4C5630) and takes no mouse
// (dlg_event), the HUD's slots standing in for it. A design chosen or left during a game builds
// the console again between frames (PortConsoleUpdate), as the trainer does, keeping the HUD's
// record of the game (PortConsoleRebuilding) and the selection. Presentation only.
bool PortConsoleHudOn(void);
bool PortConsoleRebuilding(void);
// Whether frame nFrame of a console group (ModArtKind: the command icons or the wireframes) is
// a mod's own that the GPU draws at the HD scale over the console, which leaves it out
// (statcmd.cpp, statdata.cpp, statwire.cpp).
bool PortConsoleModUi(int nKind, unsigned nFrame);
bool PortConsoleHudHeld(const struct ctrl_t *pDlg);
// status_get_console_art: the dialog's new bitmap cleared to 0 for the HUD's console, true; false
// for 1.17's, whose art it copies.
bool PortConsoleHudBlank(struct bmp_t *pBmp);
// What a slot shows of the console: a rectangle of a dialog's bitmap, 8-bit pixels in the game's
// palette, 0 where the dialog drew nothing; nPitch bytes a row.
struct PortHudPixels {
  const unsigned char *pPixels;  // the rectangle's top left
  int nPitch;
  int nWidth, nHeight;
};
enum PortHudSourceKind {
  PORT_HUD_SRC_MINIMAP,    // the minimap's picture
  PORT_HUD_SRC_SELECTION,  // the selection's panel, whole
  PORT_HUD_SRC_PORTRAIT,   // the portrait
  PORT_HUD_SRC_CARD,       // a command button, nIndex 0 to 8 left to right and top to bottom
  PORT_HUD_SRC_RESOURCE,   // the resource counter's icon, nIndex 0 minerals, 1 gas, 2 supply
  PORT_HUD_SRC_BUTTON,     // a console button, nIndex port_hudstate.h's PORT_HUD_BTN_MENU to _MESSAGING
};
// Where a slot's part of the console is: its dialog and the rectangle of it, in the dialog's
// coordinates, right and bottom inside it. False when the game shows none there now.
struct rect16_t;
bool PortConsoleHudPart(int nSource, int nIndex, struct ctrl_t **ppDlg, struct rect16_t *pRect);
// The part's pixels. False when the game shows none there now.
bool PortConsoleHudSource(int nSource, int nIndex, PortHudPixels *p);
// The selection panel's boxes as 1.17 draws them (statdata_slot_frame_draw): a selected unit's
// box behind its wireframe, for a unit in its normal state, and the frames of one unit's slots,
// its upgrades and kills, its queue and its cargo; the queue's arrow and its empty places'
// numbers, and the progress bar's frame (its fill keeps its colour). Each pixel of the panel's
// picture (PORT_HUD_SRC_SELECTION, nWidth by nHeight) that still shows its box is set to 1 in
// pMask, the rest 0. False when no box shows. The other states' unit boxes keep their colours,
// which say something (a hallucination, a parasite).
bool PortConsoleHudSelectionBoxes(unsigned char *pMask, int nWidth, int nHeight);
// The progress bar's frame where its fill hides it: 1.17 draws the full picture over the empty
// one, and a full one with no frame, only its dots (as port_remastered.cpp made it from
// Remastered's files before 0.6.21), shortens the frame as the bar fills. The
// empty picture's edge, two pixels deep, over the fill where the panel's picture is clear, as
// palette indices (-1 none), *pnWidth by *pnHeight from *pnX, *pnY of the selection's picture
// (PORT_HUD_SRC_SELECTION). A HUD design draws it over the panel, so the bar keeps its whole frame;
// PortConsoleHudSelectionBoxes counts it. False when no bar shows or none of it is full.
bool PortConsoleHudBarEdge(std::vector<int> *pPixels, int *pnX, int *pnY, int *pnWidth, int *pnHeight);
// The held dialog the HUD gives a mouse event to (port_hudinput.cpp), which then takes it as
// any dialog does; NULL after.
void PortConsoleHudDeliver(const struct ctrl_t *pDlg);
// A console button's face as 1.17 writes it (port_hudstate.h's PORT_HUD_BTN_MENU to
// PORT_HUD_BTN_MESSAGING), its colour bytes left out, into psz; its length.
int PortConsoleHudLabel(int nButton, char *psz, int nMax);

// The console's layout: 1.17's, or the "console" of the active mod's hud.json, in its data
// folder or else its own (docs/mod-data.md), read again as each console loads: an image for a
// race or for replays, shown in place of the HD console, its empty pixels the map's; whether
// the HD console's regions move to the edges; whether the map is drawn to the foot at 640.
// PortConsoleModArt is status_load_console's: the mod's image for a skin (0 zerg, 1 terran,
// 2 protoss, 3 a replay's) in the record's place, false when it gives none.
struct bmp_t;
bool PortConsoleModArt(int nSkin, struct bmp_t *pArt);

// Settings' Console skin ("consoleSkin": 0 by race, 1 Terran, 2 Protoss, 3 Zerg) in a game you
// play. PortConsoleSkinTake reads it as status_load_console loads a game's console; until the
// next, PortConsoleRace is the race whose art the console's parts load (0 zerg, 1 terran,
// 2 protoss): byte_5264C2, the race played, unless one was chosen. A replay keeps its own
// console. The race's other uses, its advisor's voice and its supply icon, stay the race played.
void PortConsoleSkinTake(void);
int PortConsoleRace(void);
// Settings' Console layout ("consoleLayout", on unless set, read again by PortConsoleSkinTake):
// the HD console's regions move to the screen's edges in a wide view, as long as a mod's
// hud.json does not keep them together.
void PortConsoleEdgesSet(bool bOn);

// The numbers 1.17 writes over the selection panel's icons (statdata.cpp's two icon draws):
// Begin before the font writes one, End after, which keeps the pixels it changed, so that the
// HD console shows them over its own icons as 1.17 drew them.
void PortConsoleSlotTextBegin(const struct ctrl_t *pCtrl);
void PortConsoleSlotTextEnd(const struct ctrl_t *pCtrl);

// A unit type's wireframe as a white picture in a cell of nCell pixels (port_console.cpp).
bool PortWireframeCell(int nType, unsigned char *pRgba, int nPitchPx, int nCell);

// The HUD's selection grid, its cells as 1.17's selection panel draws a selected unit
// (statdata_select_btn_draw), as palette indices, -1 clear, each its frame's own box: a box of the
// command buttons' GRP (StatDataBoxFrame's frame), and a unit's wireframe of grpwire.grp in the
// colours of its hit points and shields. False when the console's art is not loaded.
// PortConsoleLighten is the lighter colour the console's subgroup's box is drawn in, and
// PortConsoleLightRemap the palette's nearest to each colour lightened, for 1.17's own panel.
class CUnit;
bool PortConsoleHudBox(int nFrame, std::vector<int> *pPixels, int *pnWidth, int *pnHeight);
bool PortConsoleHudWire(CUnit *pUnit, std::vector<int> *pPixels, int *pnWidth, int *pnHeight);
// A selected unit's wireframe for the selection panel where grpwire.grp has none: its frames are
// 1.17's units', and Hyperion's selection takes buildings. Theirs is the
// console's large wireframe (wirefram.grp) at half its size, a frame as 1.17's are, so that
// rle_draw_normal draws it in the hit points' colours. False when grpwire.grp has the frame; true
// with a frame of no size when there is no picture.
struct FRAME;
bool PortConsoleSmallWire(unsigned nLooks, FRAME *pFrame);
void PortConsoleLighten(unsigned char *pRgb);
const unsigned char *PortConsoleLightRemap(void);

// A mod readout's marks over one box of the group view, where several units are selected: the
// rank's pip in the box's top right corner, a pixel wider for each rank above the first, and a
// dot for each of the first three things on the unit down the box's left, in its side's colour
// (port_readout.h). The selection panel draws them over its own boxes and a
// HUD design over the cells of its grid, from the same values, so the two cannot drift.
#define PORT_ROLL_DOTS 3
// Down a tile's left: a square of PORT_ROLL_MARK pixels every PORT_ROLL_PITCH, the first at
// PORT_ROLL_EDGE, in the tile's own pixels. Both consoles place them by these, so a tile of the
// panel and a cell of a design's grid carry the same marks in the same places.
#define PORT_ROLL_MARK 7
#define PORT_ROLL_PITCH 8
#define PORT_ROLL_EDGE 1
struct PortRollMark {
  int nRank = 0;                  // the rank's place among the readout's, plus one; 0 for a unit
                                  // the hook gave no rank the file names, which takes no pip
  unsigned int nRgb = 0xFFFFFFu;  // that rank's colour
  int nStyle = 0;                 // how the things show: PortReadoutGroupMark
  int nMarks = 0;                 // the things marked, up to PORT_ROLL_DOTS, strongest first
  int anKind[PORT_ROLL_DOTS] = {0, 0, 0};             // each one's kind, for its glyph
  unsigned int anHue[PORT_ROLL_DOTS] = {0, 0, 0};     // and its hue
  unsigned char abHue[PORT_ROLL_DOTS] = {0, 0, 0};    // 1 where the status named that colour
                                                      // itself, so a glyph is drawn in it rather
                                                      // than in its side's green or red
  unsigned char abFor[PORT_ROLL_DOTS] = {0, 0, 0};    // 1 for one that works for the unit
  int nThings = 0;                // how many are on it in all
};
// A unit's marks: false when no mod readout is in force, when the hook gives this unit none, or
// when it has nothing to mark. The pip wants the readout's "rank_pips"; what the things look like
// is the readout's "status_look" (its glyphs by default, its dots or its tile edge instead).
bool PortConsoleRollMarkOf(const CUnit *pUnit, PortRollMark *pOut);
// The marks drawn into the bitmap being drawn, the box the control's own rectangle in it. The
// panel is eight bit, so each colour goes to the palette's nearest and the rank's hues are
// approximate there; a HUD design draws the same marks in full colour.
void PortConsoleRollMarks(int x, int y, int w, int h, const PortRollMark &mark);
// What the group view says of a unit when the pointer rests on its box: the name the readout
// gives it, and the things on it with the time each has left, or that it has none. False when
// the hook gives the unit no readout and the box keeps classic's words. Two buffers and their
// sizes rather than strings, since the game's own TUs include this header.
bool PortConsoleRollTip(const CUnit *pUnit, char *pszName, int cchName, char *pszThings, int cchThings);
// A mod readout's words for one of the panel's stat icons, by the control's own index: classic's
// words with the readout's under them, or the readout's own in place of them
// (port_readout.h, PortReadoutSlotTip). False for a control that is no stat icon and for a unit
// with no readout, and the tooltip stays as it was. pszClassic may be null.
bool PortConsoleRollIconTip(int nIndex, const char *pszClassic, char *pszOut, int cchOut);
// A mod readout's words for the parts classic gives no tooltip: the wireframe's life and the
// name's rolled changes (port_readout.h, PortReadoutPartTip). The point decides which, in the
// panel's own pixels, because both are an image or a text control and those answer no hit test,
// so nothing of classic's ever hands them to the panel's hover. *pnIndex is the control the
// bubble is shown against, so it anchors where classic's own do. False for a point over neither,
// and for a unit with no readout.
bool PortConsoleRollPartTipAt(int x, int y, int *pnIndex, char *pszOut, int cchOut);

// The console's art panels as laid out now (statfluf.cpp): the four races' lists, 26 records
// in all, each list ending in a wX of 0xFFFF.
struct fluf_t;
fluf_t *StatFlufPanels();

#endif
