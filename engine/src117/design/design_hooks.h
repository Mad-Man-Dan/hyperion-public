// design_hooks.h -- what the engine's shared files call in the design engine (HyperionDesign.exe),
// each call under #ifdef HX_DESIGN, and what those files give it back. Only
// tools/build_design_x64.py compiles with HX_DESIGN, so Hyperion.exe is built without these calls
// and without the design engine's files.
#ifndef DESIGN_HOOKS_H
#define DESIGN_HOOKS_H

#include <string>
#include <vector>

struct PortOptions;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
struct SDL_FRect;
struct SDL_Window;
union SDL_Event;

// The client the panels' own channel requests come from (port_control.cpp); the channel's
// sockets are numbered from 1.
enum { DESIGN_CLIENT = -2 };

// The design engine's start (port_paths.cpp, once the command line is read): the canvas unless a
// map, replay or save is named, its own channel port and a fixed seed unless given, the window
// and the GPU presenter, and its channel commands registered.
void DesignDefaultOptions(PortOptions *pOptions);

// The window put in its windowed place (port_present.cpp, PresentSetWindowed): maximized, or
// HYPERION_DESIGN_SIZE's size centred. True when it was placed.
bool DesignWindowPlace(SDL_Window *pWindow);
// Whether the next frame is to be drawn whatever else changed (port_present.cpp): the panels
// are drawn every frame shown.
bool DesignWantsFrame(void);
// Whether the game is one pane of a tool's window (the studio, whose panels give the game its
// room): the pointer is never caged (port_present.cpp), Debug Tools stay off (port_debugview.cpp),
// the game fills its room edge to edge (port_gpu.cpp, PortGpuGameBox), and the camera may come
// closer than the game's own view (port_camera.cpp).
bool DesignGamePane(void);

// Each poll of the live channel (port_control.cpp, PortControlPoll), in a game and out of one,
// whether or not a frame is drawn: the panels' answers, and a scenario's game followed from its
// launch to its set-up.
void DesignPoll(void);

// Each SDL event, before Hyperion turns it into a window message (port_winmsg.cpp, Pump). True:
// the panels took it, and the game does not see it.
bool DesignEvent(const SDL_Event *pEvent);

// Where the game goes in a target fTargetW x fTargetH (port_gpu.cpp, PortGpuGameBox): the room
// the panels leave, in the target's pixels. False for the whole target.
bool DesignGameRoom(float fTargetW, float fTargetH, float *pfX, float *pfY, float *pfW, float *pfH);
// The units' paths and the effects over the game's frame, before the HUD (design_draw.cpp). box
// is the game's frame in the target, nGameW x nGameH its game pixels; nFormat the target's
// SDL_GPUTextureFormat.
void DesignWorldDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nWidth, int nHeight,
                     const SDL_FRect &box, int nGameW, int nGameH, int nFormat);
// Whether the HUD is hidden (design_hud.cpp): no HUD design is drawn.
bool DesignHudHidden(void);
// The panels, last over the window's picture (port_gpu.cpp) and over Hyperion's menus
// (port_gpu_ui.cpp); no render pass may be open. DesignPanelsRedraw draws this frame's panels
// again into another target of the window's format, for the grab.
void DesignPanelsDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nWidth, int nHeight);
void DesignPanelsRedraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nWidth, int nHeight);
// The grab (design_grab) and a clip's frames (design_record): whether one is wanted of this frame,
// with the panels or without, and whether it is a clip's, drawn without the HUD and the menus; and
// its pixels once drawn, 4 bytes each in r g b a order (b g r a with bBgra), NULL when none came.
bool DesignGrabAsked(bool *pbPanels, bool *pbClip);
void DesignGrabTake(const void *pPixels, int nWidth, int nHeight, bool bBgra);
// The part of a window of nWinW x nWinH a clip's frame takes, in its pixels, the game being drawn
// in box at nGameW x nGameH; false when there is none.
bool DesignClipRegion(const SDL_FRect &box, int nGameW, int nGameH, int nWinW, int nWinH, int *pnX, int *pnY,
                      int *pnW, int *pnH);

// A scenario's game waiting to start (port_front.cpp, design_scenario.cpp): the front end leaves
// its screen for it and starts it before anything else it would start. DesignLaunch answers as
// PortFrontStartMode does, nonzero when the game was made.
bool DesignLaunchPending(void);
int DesignLaunch(void);
// The seed of a game a scenario starts, and of its restarts (port_observe.cpp, PortObserveSeed);
// false for the command line's.
bool DesignSeed(int *pnSeed);

// port_console.cpp: the classic console's mask (port_mask.cpp) and its place on the screen, or
// NULL while none is drawn.
const struct _HSTRANS *DesignConsoleMask(int *pnLeft, int *pnTop);

// port_control.cpp: a request of the panels, run with the channel's others, and each answer to
// one (design_bridge.cpp); a single-player game is on, its frame, and whether it is paused.
void DesignChannelPost(const std::string &text);
void DesignChannelAnswer(const std::string &text);
bool DesignInGame(void);
unsigned DesignGameFrame(void);
bool DesignGamePaused(void);

// port_moddata.cpp: the loader's tables as the studio reads them (src117/studio/). A table's name,
// classic rows, first mod id and most rows; a column's name, the table its values name ("" for
// numbers), the file's scale, its values a row, and whether it holds a path (a sound's file, a
// portrait's videos); a classic row's values column by column as the loader starts a record from
// it, a path column's text in *pPaths; and the newest record of a name the loader has built, its
// row, id and submission, false when it has none.
struct DesignModColumn {
  const char *pszName;
  const char *pszRef;
  int nScale;
  int nCount;
  bool bPath;
};
bool DesignModTable(int t, const char **ppszName, int *pnRows117, int *pnModFirst, int *pnModMax);
int DesignModColumns(int t);
bool DesignModColumnOf(int t, int c, DesignModColumn *pOut);
bool DesignModClassicRow(int t, int nRow, std::vector<std::vector<long long>> *pCols,
                         std::vector<std::string> *pPaths);
bool DesignModLoaded(int t, const std::string &name, int *pnId, int *pnSub,
                     std::vector<std::vector<long long>> *pCols);

#endif
