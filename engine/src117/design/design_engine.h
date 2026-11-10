// design_engine.h -- the design engine's own parts, as its files share them. The hooks the engine
// calls are in design_hooks.h.
#ifndef DESIGN_ENGINE_H
#define DESIGN_ENGINE_H

#include <functional>
#include <string>
#include <vector>

struct PortJson;
struct SDL_FRect;

// The channel's port when none is given: one past Hyperion's, so both can run side by side.
enum { DESIGN_CONTROL_PORT = 27118 };

// The repository's design folder, where the effects are (design/fx/luau/): HYPERION_DESIGN_FOLDER,
// else the first design\ with a _shared\ found going up from the program's folder; empty when
// there is none. It ends in a backslash.
const std::string &DesignFolder(void);
// The design engine's data folder (--data), where its settings and its panels' layout are kept.
std::string DesignDataFolder(void);

// Each part's channel commands, registered once at the start (DesignDefaultOptions), and its state
// as JSON fields for design_status.
void DesignHudInit(void);
void DesignPathsInit(void);
void DesignFxInit(void);
void DesignPanelsInit(void);
void DesignImGuiInit(void);
void DesignScenarioInit(void);
void DesignStyleInit(void);
void DesignPainterInit(void);
void DesignRecordInit(void);
std::string DesignHudFields(void);
std::string DesignPathsFields(void);
std::string DesignFxFields(void);
std::string DesignPanelsFields(void);
std::string DesignScenarioFields(void);
std::string DesignStyleFields(void);

// A clip being taken (design_record.cpp): whether this frame is one of its frames, asked as the
// frame is drawn; and that frame's pixels, the region DesignClipRegion gave, NULL when none came.
bool DesignClipDue(void);
void DesignClipTake(const void *pPixels, int nWidth, int nHeight, bool bBgra);

// A scenario's game followed from its launch to its set-up (design_scenario.cpp), each poll of the
// channel (DesignPoll), in a game and out of one.
void DesignScenarioStep(void);
// A spawn or an effect a panel gave, kept in the scenario while the Scenario panel keeps them:
// pszKind "units" or "effects", fields the command's.
void DesignScenarioKeep(const char *pszKind, const std::string &fields);
// The styles in design/styles/ (design_style.cpp): one applied by name, the last applied or
// saved, and their names.
void DesignStyleApply(const std::string &name);
const std::string &DesignStyleCurrent(void);
std::vector<std::string> DesignStyleNames(void);

// A channel command run for the panels with the channel's others, between frames
// (design_bridge.cpp): pfnDone gets its answer, parsed, on the game's thread. Any command works,
// the channel's own and the ones added with PortControlCommand.
typedef std::function<void(const PortJson &answer)> DesignAnswerFn;
void DesignCall(const char *pszCmd, const std::string &fields, DesignAnswerFn pfnDone = DesignAnswerFn());
// The answers come back to the panels each frame they are drawn.
void DesignBridgeDeliver(void);
// The last request's answer, raw, and a count of the requests still out.
const std::string &DesignBridgeLast(void);
int DesignBridgeWaiting(void);

// The window as the Window menu and design_window set it.
void DesignWindowSet(int nWidth, int nHeight);
void DesignWindowFullscreen(bool bOn);
void DesignWindowMaximize(void);
std::string DesignWindowFields(void);

// The game's map as the window shows it (design_draw.cpp): the camera, and where the game's
// frame is in the target. A map point x, y at height z goes to the target's pixels by
// DesignProject; fPerMap is the target's pixels to a map pixel.
struct DesignCamera {
  double fCamX, fCamY;  // the map pixel at the game's frame's top left
  double fZoom;         // the map's pixels to a game pixel
  double fHalf;         // 0.5 when zoomed, where a game pixel is its map square's middle
  float fX, fY;         // the game's frame's top left in the target
  float fSx, fSy;       // the target's pixels to a game pixel
  float fPerMap;        // the target's pixels to a map pixel
  float afMap[4];       // the map's part of the frame in the target, x0, y0, x1, y1
};
bool DesignCameraNow(const SDL_FRect &box, int nGameW, int nGameH, DesignCamera *pCam);
inline void DesignProject(const DesignCamera &c, double x, double y, double z, float *pfX, float *pfY) {
  *pfX = c.fX + static_cast<float>(((x - c.fCamX) / c.fZoom - c.fHalf) * c.fSx);
  *pfY = c.fY + static_cast<float>(((y - z - c.fCamY) / c.fZoom - c.fHalf) * c.fSy);
}
// A point of the window, in its pixels, as a map point; false off the game's map.
bool DesignWindowToMap(float fX, float fY, int *pnMapX, int *pnMapY);

// One shape drawn over the game's frame, as shaders/fx.hlsl's FxPrim has it: the paths' lines and
// the effects' shapes, in the target's pixels.
enum DesignPrimKind {
  DESIGN_PRIM_GLOW = 0,  // light round a point: c0 at its middle, c1 at 0.22, c2 at 0.55, none at its edge
  DESIGN_PRIM_DOT = 1,   // a filled circle
  DESIGN_PRIM_RING = 2,  // a ring between ring.x and 1 of its radius, a disc with ring.x 0; arc and dashes
  DESIGN_PRIM_LINE = 3,  // from at.xy to at.zw: c1 axes.x wide, a halo of c2 axes.z wide, a core of c0 axes.y
  DESIGN_PRIM_STAR = 4,  // a cross of light, axes.x its arm
  DESIGN_PRIM_TRI = 5,   // a triangle at.xy, at.zw, axes.xy, in c0
  DESIGN_PRIM_WASH = 6   // the whole target in c0
};
struct DesignPrim {
  float head[4];  // kind, blend (0 added, 1 over), dashes, 0
  float at[4];    // the centre x, y; a line's or a triangle's first points
  float axes[4];  // a circle's x axis and y axis in pixels, its radius long; a line's widths
  float ring[4];  // the inner radius over the outer, the arc's first and last angle, its turn
  float c0[4];    // straight r, g, b, a, 0 to 1
  float c1[4];
  float c2[4];
  float clip[4];  // x0, y0, x1, y1 in the target
};
void DesignPrimClear(DesignPrim *p, int nKind, bool bAdd, const float *pafClip);
// The paths' and the effects' shapes this frame, added to the list.
void DesignPathsBuild(const DesignCamera &cam, std::vector<DesignPrim> *pOut);
void DesignFxBuild(const DesignCamera &cam, std::vector<DesignPrim> *pOut);
// The effects drawn in the game's pixels (the live values fx.pixel ...): on, a cell in game pixels,
// the steps of light and cover, the colours of each channel (0 any), and the game frames each step
// of their time holds.
struct DesignPixel {
  float fOn, fSize, fSteps, fColours, fHold;
};
DesignPixel *DesignFxPixel(void);
// The cell fSize asks for, kept on the game's grid: a whole number of game pixels, 1 to 4, or a
// game pixel split evenly, a half, a third or a quarter.
float DesignPixelCell(float fSize);
// How strong a flash of the whole screen may be (fx.flash), as a player's comfort cap: 0 none.
float *DesignFxFlash(void);

// A player's colour, r g b 0 to 1: the slot's own, else classic's for the first eight.
void DesignPlayerRgb(int nPlayer, float *pafRgb);

#endif
