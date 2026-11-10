// port_hud.h -- the HUD design a game shows: which one, read from its files and
// played, its events taken from the data layer (port_hudstate.h) and its clock run, for the
// GPU presenter to draw (render/port_gpu_hud.cpp). Classic, 1.17's own console, is no design:
// with it nothing here is on.
//
// The design is Settings' "hud" section: "design" is classic, mech, r9 or a player's design,
// "user:<folder>", a folder of the player data folder's huds\ holding its layout, hud.json, and
// its style, style.json, when it has one; the mech's "layout" (span, flank, flank-right),
// "size" (full, min) and "material" (gunmetal, graphite, blackout); each design's
// "<design>.light" (#rrggbb, or empty for the style's own), its options,
// "<design>.option.<id>", and the buttons the player leaves out of it, "<design>.hide.<slot>"
// "1"; "sound" "off" silences the designs' sounds; "mapRaise", 0 to 200, raises the map over every design
// (PortHudMapRaise). HYPERION_HUD=<layout.json>[;<style.json>] plays a design straight from its files
// instead, for its maker.
#pragma once

#include <functional>
#include <string>
#include <vector>

class HudPlayer;
struct HudDesign;
struct HudEnv;
struct HudLayerDef;
struct HudTipContent;
struct PortJson;
struct PortHudState;

// A design the player can choose (Settings' HUD tab): Classic, Mech and R9, then the player's
// own, folders and .hud packages in huds\ in the data folder, by their names.
struct PortHudDesignInfo {
  std::string id;    // "classic", "mech", "r9", "user:<folder>" or "user:<name>.hud"
  std::string name;  // its file's "name", or the folder's
  std::string note;  // its file's "note"
  bool bBuiltIn = false;
  bool bHeld = false;  // not ready for players (PortHudDesignHeld): listed, not chosen
};
std::vector<PortHudDesignInfo> PortHudDesigns(void);
// A built-in design held back from players while it is worked on: the HUD tab shows it greyed,
// "Not available yet", and a game whose saved design it is shows the classic console. R9,
// since 2026-10-27 (the user: "disable for now as its a WIP build and not ready").
bool PortHudDesignHeld(const std::string &id);
// A design's files: its layout and its style (empty for none), Mech's by its picks of layout,
// size and material ("span", "full", "gunmetal" when empty). False for Classic and for a design
// that is not there. A package's are where it is unpacked (unpacked again when it is newer), and
// *pPackage is the package.
bool PortHudDesignFiles(const std::string &id, const std::string &layout, const std::string &size,
                        const std::string &material, std::string *pLayout, std::string *pStyle,
                        std::string *pPackage = nullptr);

// Once a presented frame, in a game: the design loaded (again, when the choice changed), laid
// out on a screen nWidth x nHeight pixels, the events since the last frame fired and its clock
// run on. The player to draw and the values its bindings read, or NULL with no design on.
HudPlayer *PortHudFrameBegin(int nWidth, int nHeight, const HudEnv **ppEnv);
// A design is chosen (not Classic), whether or not a game is on.
bool PortHudChosen(void);
// Whether a game's console is the HUD's (port_console.cpp asks as the console loads): a design is
// chosen, it reads without errors and the GPU presenter draws the screen. Loads the design when
// it is not loaded; its events are then taken from the next game's start.
bool PortHudPlays(void);
// A slot the player left out of the design (Settings' HUD tab): it draws nothing and takes no
// press.
bool PortHudSlotHidden(int nSlot);
// Whether the design worn now places the players' stats' view nView (port_hudstats.h's
// HUD_STATS_SIDE or HUD_STATS_POPUP) in a stats slot; a view it does not place is drawn in a place
// of its own (port_hudstats.cpp).
bool PortHudStatsPlaced(int nView);
// The engine's own clock and APM (port_overlay.cpp) are off in a replay layout that places
// no time or apm slot, whose position readout stands for the clock; and never drawn under one of
// the HUD's panels, the point u, v of the screen's units.
bool PortHudCountersOff(void);
bool PortHudCovers(double u, double v);
// How far the map is raised over the HUD, in units of a 1080-high screen, 0 to 200 (Settings' HUD
// tab, "Raise the map", the same for every design): port_view.cpp draws the
// camera's sight that much higher while a HUD design's console is on. A design with a side
// column has none (PortHudSides).
double PortHudMapRaise(void);
// Settings' HUD tab's picture: the design pPlayer plays, drawn over a picture of a map
// in a box of the menus' frame, after the UI kit's first nAtPrim primitives of the frame and before
// the rest (render/port_gpu_ui.cpp, port_gpu_hud.cpp). The tab sets it each frame it shows it, and
// the frame's end takes it; what it points to stays the tab's.
struct PortHudPreview {
  float x = 0, y = 0, w = 0, h = 0;  // the box, in the frame's pixels
  int nAtPrim = 0;
  HudPlayer *pPlayer = nullptr;      // laid out at the box's shape
  const HudEnv *pEnv = nullptr;
  const PortHudState *pState = nullptr;  // what its slots show
  unsigned int nKey = 0;             // changes when the design is read again
  const unsigned char *pMap = nullptr;  // the map's picture, RGBA, nMapW x nMapH, over the whole box
  int nMapW = 0, nMapH = 0;
  unsigned long long nMapKey = 0;    // changes with the picture
  const std::vector<bool> *pHidden = nullptr;  // the slots left out, by HUD_SLOT_*
};
void PortHudPreviewSet(const PortHudPreview &p);
const PortHudPreview *PortHudPreviewTake(void);
// The game's design's side columns as last laid out, in units of a 1080-high screen: the width it
// covers top to bottom from the screen's left and right edges (HudPlayer::Sides); 0 and 0 when
// no design plays or it has none. port_view.cpp draws the map beside them.
void PortHudSides(double *pfLeft, double *pfRight);
// The colour the selection's unit boxes are drawn in (Settings' HUD tab, "<design>.selection":
// "light" for the design's light, or #rrggbb), 0 to 1; false for 1.17's own.
bool PortHudSelectionAccent(float rgb[3]);
// A design's files read and made ready to play: its layout and style, the parts files in the
// parts folder beside the layout, its own files' folder (files\ beside it), the light, and the
// picks option gives for its options by id ("" for the design's own). A game's design and
// Settings' HUD tab are read by it.
// tipAccent: the selection boxes' colour, which the tooltip's lit parts take ("" for the style's).
bool PortHudRead(const std::string &layout, const std::string &style, const std::string &light,
                 const std::function<std::string(const std::string &)> &option, HudDesign *pDesign,
                 std::vector<std::string> *pErrors, std::vector<std::string> *pWarnings,
                 const std::string &tipAccent = std::string());
// The design's tooltip (HudPlayer::Tip) for the button drawn at box (the screen's units:
// left, top, w, h) in panel nPanel, or hidden with pContent NULL. False when no design plays or
// its style has no tooltip: 1.17's bubble is shown then.
bool PortHudTip(const HudTipContent *pContent, const double box[4], int nPanel);
// The music player's skin in the style of the design this game plays (2026-10-24), for a music
// add-on's windows: NULL when no design plays or its style has no
// "musicPlayer", and the player keeps its own skin. Good until the design loads again.
const struct HudMusicSkin *PortHudMusicSkin(void);
// One of the music player's moments (open, close, shade, denied) sounded as the style gives it,
// at the game's sound volume; nothing when the style gives none or the designs' sound is off.
void PortHudMusicSound(const char *pszMoment);
// The music player's plates for the next frame the design draws: each part a look frames at a box
// in the HUD's units over the game (port_hudfile.h, HudMusicPlate); none puts them away.
void PortHudMusicPlates(const struct HudMusicPlate *pPlates, int nPlates);
// A .hud package unpacked where the player's packages are (unless it was since it was last
// written): *pFolder is its folder, with its slash; what it holds that a design may not is left
// out and told in pWarnings. False when it is not a zip.
bool PortHudUnpack(const std::string &package, std::string *pFolder, std::vector<std::string> *pWarnings);
// How many times a design has loaded: a drawer holding what it built from the last one builds
// again when this changes (a reload keeps the design at the same address).
unsigned int PortHudLoads(void);
// Reads the design again at the next frame: its option picks changed in Settings.
void PortHudReload(void);
// What the last load said: its errors (none when it plays) and warnings.
const std::vector<std::string> &PortHudErrors(void);
const std::vector<std::string> &PortHudWarnings(void);

// The HUD's layers on the screen as last drawn, in the order drawn, for the mouse
// (port_hudinput.cpp): each one's box, the screen's units to the box's and back. The screen's
// units are the HUD's, 1080 high and lw across. Set by the presenter each frame it draws the HUD
// (port_gpu_hud.cpp); empty when it draws none.
struct HudHit {
  double m[6];    // the box's units to the screen's: a, b, c, d, e, f
  double inv[6];  // the screen's units to the box's
  double w, h;    // the box
  int nSlot;      // its slot (port_hudfile.h's HudSlot), or -1 for a panel's layer
  int nAction;    // a button's action (HudAction), or -1; one the engine does not know takes no press
  int nActionArg;
  int nPanel;
  const HudLayerDef *pLayer;
  int nMeterFrom;     // the side a slider (replay.seek) runs from: 0 left, 1 right, 2 top, 3 bottom
  std::string label;  // the button's own name (the layer's "label"), or ""
};
void PortHudHitsSet(std::vector<HudHit> &&hits, double lw);
const std::vector<HudHit> &PortHudHits(double *pLw);
// A panel's box on the screen as last laid out, in the screen's units: left, top, w, h.
bool PortHudPanelRect(int nPanel, double out[4]);
// The pointer at the screen's units u, v (the mouse, port_hudinput.cpp), or off the screen with
// bOn false: panel.hover and panel.leave for the panels that ask for them ("hover"), by their
// boxes, as the design center's player fires them. And a press on a panel's slot: panel.press.
void PortHudPointer(bool bOn, double u, double v);
void PortHudPressed(int nPanel, int nSlot, int nAction = -1, int nActionArg = 0);
// The design worn now has a layer that moves a replay (replay.seek): its snapshots are
// kept as a replay plays (port_replayseek.cpp).
bool PortHudCanSeek(void);

// A picture sw x sh fitted whole in the box x, y, w, h, in its middle: out's x, y, w, h. The
// slots' pictures, as drawn and as pressed.
inline void HudFit(double sw, double sh, double x, double y, double w, double h, double out[4]) {
  const double f = sw > 0 && sh > 0 ? (w / sw < h / sh ? w / sw : h / sh) : 0.0;
  out[0] = x + (w - sw * f) / 2;
  out[1] = y + (h - sh * f) / 2;
  out[2] = sw * f;
  out[3] = sh * f;
}

// The room a slot of w by h units gives a picture sw by sh, in the picture's own pixels: the
// picture fitted whole, then the slot measured in its pixels, with the spare counted only when
// it is wide or tall enough to write a line in. A slot fits by the smaller of its two scales, so
// one of the two always comes back as the picture's own size: a wide short slot gives columns, a
// squarer one gives rows.
inline void HudRoomIn(double sw, double sh, double w, double h, double *pfW, double *pfH) {
  const double kColumns = 110;  // picture pixels across, for a status's name, what it does and its time
  const double kRows = 22;      // picture pixels down, for two of those lines and the gap above them
  *pfW = sw, *pfH = sh;
  if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0)
    return;
  const double fw = w / sw, fh = h / sh, f = fw < fh ? fw : fh;
  const double rw = w / f, rh = h / f;
  if (rw - sw >= kColumns)
    *pfW = rw;
  else if (rh - sh >= kRows)
    *pfH = rh;
}

// Classic's panel in the selection slot: fitted whole as ever, and moved to the slot's start on
// the axis that has room to spare, so that what a mod adds has one unbroken room beside or under
// it rather than a margin at each end. With no room to spare this is HudFit, which is what a
// slot showing no mod readout keeps. out takes x, y, w, h in the slot's units.
inline void HudReadoutBox(double sw, double sh, double x, double y, double w, double h, double out[4]) {
  double rw = sw, rh = sh;
  HudRoomIn(sw, sh, w, h, &rw, &rh);
  HudFit(sw, sh, x, y, w, h, out);
  if (rw > sw)
    out[0] = x;
  if (rh > sh)
    out[1] = y;
}

// The room a mod's unit readout has in the design's selection slot, in the readout's own pixels
// (port_readout.h), for PortReadoutRoomSet. The classic console, a design with no selection slot
// and a slot no bigger than the panel all give the panel's own size.
void PortHudReadoutRoom(int *pnWidth, int *pnHeight);
// Where the unit readout's panel is on the screen, for the readout's own drawing over it
// (port_readout.h): classic's panel, the room beside or under it for what a mod adds, and the
// scale the panel is drawn at. All in the game's own screen pixels, the pixels a dialog's
// rectangle is in, so a part's place is PortReadoutPartRect scaled by fScale and offset by the
// panel's box.
//
// Both consoles answer. The classic console's panel is the StatData dialog's own rectangle at a
// scale of 1, with no room, since classic's panel is the whole of it; a design's is its selection
// slot's placing of the panel (HudReadoutBox), at the slot's scale, with the room the slot leaves
// (PortHudReadoutRoom). False when no panel is on the screen: no game, the slot left out in
// Settings, or a design with no selection slot.
//
// A design may turn a panel (`rotate`), and a slot's layer is drawn into its own buffer and then
// composited with that turn, so a rectangle cannot say where a turned panel's parts go: the box is
// then the turned panel's bounding box, which is a frame to clip to. A readout drawing into a
// turned panel wants the slot's own layer instead.
struct PortReadoutPlace {
  bool bOn = false;
  double x = 0, y = 0, w = 0, h = 0;                  // classic's panel
  double roomX = 0, roomY = 0, roomW = 0, roomH = 0;  // the room, 0 wide or tall for none
  double fScale = 1.0;                                // screen pixels a readout pixel
};
bool PortHudReadoutPlace(PortReadoutPlace *pOut);

// A surface a mod's unit readout draws into, in the readout's own pixels (port_readout.h): (0, 0)
// is classic's panel's top left, the panel is PORT_READOUT_W by PORT_READOUT_H, and what a mod
// adds goes in the room past one of those two, as PortHudReadoutRoom reports it.
//
// A design's selection slot makes one of these over its own layer, so the readout is drawn inside
// the layer and comes out right under whatever the design does to the panel -- a turn, a scale, a
// fade -- and takes the layer's effects with it. The classic console has no layer and draws by
// PortHudReadoutPlace instead.
//
// pCtx is the surface's own and goes back to every call. A colour is 0xRRGGBB; alpha is 0 to 1,
// and a surface that cannot blend takes anything over a half as solid. fPx says how many of the
// surface's own pixels a readout pixel is, for a caller deciding how much detail will show.
struct PortReadoutSurface {
  void *pCtx;
  bool bFull;   // full colour; false takes each colour to the game palette's nearest
  double fPx;
  void (*pfnFill)(void *pCtx, double x, double y, double w, double h, unsigned int nRgb, double fAlpha,
                  double fRadius);
  void (*pfnLine)(void *pCtx, double x0, double y0, double x1, double y1, unsigned int nRgb, double fWidth,
                  double fAlpha);
  // a status kind's glyph in a box of z (port_readout.h, PortReadoutGlyph)
  void (*pfnGlyph)(void *pCtx, int nKind, double x, double y, double z, unsigned int nRgb, double fAlpha);
  // text of fSize readout pixels, its left at x, or centred on x with nAlign 1 and right at x
  // with 2; y is its middle. nFace is 0 for the kit's text and 1 for its bold.
  void (*pfnText)(void *pCtx, const char *pszUtf8, double x, double y, double fSize, unsigned int nRgb,
                  double fAlpha, int nAlign, int nFace);
  double (*pfnTextWidth)(void *pCtx, const char *pszUtf8, double fSize, int nFace);
};
// The readout's own drawing, registered by port_readout.cpp as a game with a readout starts and
// called wherever a surface for it is made. Nothing is drawn while none is registered, which is
// what a build without the readout's drawing, and a game without a mod readout, both get.
typedef void (*PortReadoutSurfaceFn)(const PortReadoutSurface *pSurface);
void PortHudReadoutSurfaceSet(PortReadoutSurfaceFn pfn);
PortReadoutSurfaceFn PortHudReadoutSurface(void);

// The selection slot's grid (docs/hud-format.md, "The selection's pages"), as the design
// center's page lays it out (hud.js's gridAt, hud.html's .sel7) in a slot w x h units holding
// nUnits: a column of page tabs at its side, the cells beside them, a page's rows from the top
// of the room left and its columns in its middle. The cells keep the file's pitch, smaller when
// the slot holds less than its fewest columns. All in the slot's units.
struct HudGrid {
  int cols = 0, rows = 0, per = 0, pages = 1;
  double x0 = 0, y0 = 0, cw = 0, ch = 0;        // the first cell's top left, and the pitch
  double tabX = 0, tabY = 0, tabW = 0, tabH = 0;  // the first page tab
  bool bTabs = false;                             // a tab a page, with more than one page
  // a page's unit i's cell, and page i's tab: x, y, w, h
  void Cell(int i, double q[4]) const {
    q[0] = x0 + (i % cols) * cw, q[1] = y0 + (i / cols) * ch, q[2] = cw, q[3] = ch;
  }
  void Tab(int i, double q[4]) const { q[0] = tabX, q[1] = tabY + i * (tabH + 3), q[2] = tabW, q[3] = tabH; }
};
// False when the layer has no grid, or the slot no room for one.
bool HudGridOf(const PortJson &grid, double w, double h, int nUnits, HudGrid *pGrid);

// The players' board (docs/hud-format.md, "Replays"), as the design center's page lays it out
// (hud.js's drawBoard, hud.html's .pboard) in a slot w x h units: a row a player, the rows sharing
// the slot's height between the layer's rowH, least and most; with split the first team's rows in
// the left half and the rest's in the right, mirrored so each team's figures face the middle. A row
// 36 units tall or more has its name on a line over its figures. Past the rows that fit at the
// least height, a side's last row is "+N more · F1", which opens the players' stats.
struct PortHudState;
struct HudBoardRow {
  int nPlayer = 0;      // its index in PortHudState::aPlayers; -1 the "+N more" row
  int nMore = 0;        // that row's N
  bool bRight = false;  // in the right half, mirrored
  bool bTwo = false;    // its name over its figures
  double size = 14;     // its text's size
  double row[4] = {}, swatch[4] = {}, name[4] = {}, figures[4] = {}, eye[4] = {};  // x, y, w, h
};
void HudBoardOf(const PortJson &layer, double w, double h, const PortHudState &st, std::vector<HudBoardRow> *pRows);
