// design_engine.cpp -- HyperionDesign.exe's start and its own channel commands. The design engine is
// Hyperion's engine as a program of its own for trying ideas out: the canvas in a window of any
// size, the GPU presenter, panels round the game (design_imgui.cpp, design_panels.cpp), and every
// command of the live channel open to the panels and to tools. Hyperion.exe is built without any of
// it (design_hooks.h).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <SDL3/SDL.h>
#include "../port/platform/port_json.h"
#include "../port/platform/port_paths.h"  // PortOptions, PortDataFolder
#include "../port/platform/port_winmsg.h"  // WinMsgSdlWindow
#include "../port/render/port_present.h"  // PortToggleFullscreen
#include "../port/render/port_view.h"     // g_portView
#include "../port/tools/port_canvas.h"    // PORT_CANVAS_TILESET
#include "../port/tools/port_control.h"
#include "design_engine.h"
#include "design_hooks.h"
#include "design_ui.h"  // DesignThisProgram

namespace {

// The seed of every design game when none is given, so each start shows the same game.
const char kSeed[] = "1117";
const DWORD kQuitMs = 300;  // design_quit's answer goes out first

// the grab asked for: where it goes, with the panels or not, and who waits for its answer
bool g_bGrabAsked;
bool g_bGrabPanels;
std::string g_grabPath;
int g_nGrabState;  // 0 none, 1 asked, 2 being drawn, 3 saved, 4 failed
int g_nGrabW, g_nGrabH;
bool g_bClipFrame;  // the frame being taken is a clip's (design_record.cpp)

std::string FindFolder(void) {
  const char *pszGiven = getenv("HYPERION_DESIGN_FOLDER");
  if (pszGiven != NULL && pszGiven[0] != '\0') {
    std::string root = pszGiven;
    if (root.back() != '\\' && root.back() != '/')
      root += '\\';
    return root;
  }
  char sz[MAX_PATH] = "";
  if (GetModuleFileNameA(NULL, sz, MAX_PATH) == 0)
    return std::string();
  std::string dir = sz;
  for (int i = 0; i < 8; ++i) {
    const size_t cut = dir.find_last_of("\\/");
    if (cut == std::string::npos)
      break;
    dir.erase(cut);
    const DWORD attr = GetFileAttributesA((dir + "\\design\\_shared").c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
      return dir + "\\design\\";
  }
  return std::string();
}

// An environment variable both the C runtime and SDL see, unless it is set already.
void Default(const char *pszName, const char *pszValue) {
  const char *psz = getenv(pszName);
  if (psz != NULL && psz[0] != '\0')
    return;
  _putenv_s(pszName, pszValue);
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), pszName, pszValue, false);
}

bool CmdStatus(const PortJson &, std::string *pAnswer) {
  *pAnswer = "\"engine\":" + PortJsonQuote(DesignThisProgram().pszName) + ",\"port\":" +
             std::to_string(PortControlPort()) +
             ",\"folder\":" + PortJsonQuote(DesignFolder()) + ",\"game\":{\"on\":" +
             (DesignInGame() ? "true" : "false") + ",\"frame\":" + std::to_string(DesignGameFrame()) +
             ",\"paused\":" + (DesignGamePaused() ? "true" : "false") + ",\"view\":[" +
             std::to_string(g_portView.nWidth) + "," + std::to_string(g_portView.nHeight) + "]}," +
             DesignWindowFields() + "," + DesignHudFields() + "," + DesignPathsFields() + "," + DesignFxFields() +
             "," + DesignPanelsFields() + "," + DesignScenarioFields() + "," + DesignStyleFields();
  return true;
}

// Nothing is saved on the way out: the design engine's state is the game's, made again each start.
DWORD WINAPI QuitSoon(void *) {
  Sleep(kQuitMs);
  ExitProcess(0);
}

bool CmdQuit(const PortJson &, std::string *pAnswer) {
  HANDLE h = CreateThread(NULL, 0, QuitSoon, NULL, 0, NULL);
  if (h != NULL)
    CloseHandle(h);
  *pAnswer = "\"quitting\":true";
  return true;
}

// design_window {w?, h?, fullscreen?, maximized?}: the window's size or full screen; nothing given,
// how it is now.
bool CmdWindow(const PortJson &j, std::string *pAnswer) {
  if (j["fullscreen"].kind == PortJson::BOOL)
    DesignWindowFullscreen(j["fullscreen"].b);
  if (j["maximized"].kind == PortJson::BOOL && j["maximized"].b)
    DesignWindowMaximize();
  if (j["w"].kind == PortJson::NUMBER && j["h"].kind == PortJson::NUMBER)
    DesignWindowSet(static_cast<int>(j["w"].d), static_cast<int>(j["h"].d));
  *pAnswer = DesignWindowFields();
  return true;
}

// design_grab {path?, panels?}: the window's picture as a PNG, its panels too with panels: true.
// It is taken of the next frame drawn; design_grabbed says how it went.
bool CmdGrab(const PortJson &j, std::string *pAnswer) {
  if (g_nGrabState == 1 || g_nGrabState == 2) {
    *pAnswer = "a grab is being taken";
    return false;
  }
  std::string path = j["path"].Str();
  if (path.empty()) {
    char sz[64];
    snprintf(sz, sizeof sz, "grabs\\grab-%llu.png", static_cast<unsigned long long>(GetTickCount64()));
    path = DesignDataFolder() + sz;
    CreateDirectoryA((DesignDataFolder() + "grabs").c_str(), NULL);
  }
  g_grabPath = path;
  g_bGrabPanels = j["panels"].kind == PortJson::BOOL && j["panels"].b;
  g_bGrabAsked = true;
  g_nGrabState = 1;
  PortPresentRedraw();
  *pAnswer = "\"path\":" + PortJsonQuote(path) + ",\"asked\":true";
  return true;
}

// design_grabbed: how the last grab went, its path and size once saved.
bool CmdGrabbed(const PortJson &, std::string *pAnswer) {
  static const char *const kStates[] = {"none", "asked", "drawing", "saved", "failed"};
  *pAnswer = std::string("\"state\":\"") + kStates[g_nGrabState] + "\",\"path\":" + PortJsonQuote(g_grabPath) +
             ",\"width\":" + std::to_string(g_nGrabW) + ",\"height\":" + std::to_string(g_nGrabH);
  return true;
}

}  // namespace

const std::string &DesignFolder(void) {
  static const std::string root = FindFolder();
  return root;
}

std::string DesignDataFolder(void) {
  std::string data = PortDataFolder();
  if (!data.empty() && data.back() != '\\' && data.back() != '/')
    data += '\\';
  return data;
}

void DesignDefaultOptions(PortOptions *pOptions) {
  if (pOptions->szReplay[0] == '\0' && pOptions->szMap[0] == '\0' && pOptions->szLoad[0] == '\0' &&
      pOptions->szCanvas[0] == '\0')
    lstrcpynA(pOptions->szCanvas, PORT_CANVAS_TILESET, sizeof pOptions->szCanvas);
  if (pOptions->szControl[0] == '\0')
    snprintf(pOptions->szControl, sizeof pOptions->szControl, "%d", DesignThisProgram().nPort);
  if (pOptions->szSeed[0] == '\0')
    lstrcpynA(pOptions->szSeed, kSeed, sizeof pOptions->szSeed);
  // a window of its own on the GPU presenter, whatever the design engine's Settings say
  Default("HYPERION_WINDOW", "windowed");
  const char *pszRenderer = getenv("HYPERION_RENDERER");
  if (pszRenderer != NULL && pszRenderer[0] != '\0' && _stricmp(pszRenderer, "direct3d12") != 0 &&
      _stricmp(pszRenderer, "d3d12") != 0 && _stricmp(pszRenderer, "vulkan") != 0 && _stricmp(pszRenderer, "gpu") != 0) {
    _putenv_s("HYPERION_RENDERER", "vulkan");
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "HYPERION_RENDERER", "vulkan", true);
  }
  PortControlCommand("design_status", CmdStatus);
  PortControlCommand("design_quit", CmdQuit);
  PortControlCommand("design_window", CmdWindow);
  PortControlCommand("design_grab", CmdGrab);
  PortControlCommand("design_grabbed", CmdGrabbed);
  DesignHudInit();
  DesignPathsInit();
  DesignFxInit();
  DesignPanelsInit();
  DesignImGuiInit();
  DesignScenarioInit();
  DesignStyleInit();
  DesignPainterInit();
  DesignRecordInit();
  if (DesignThisProgram().pfnInit)
    DesignThisProgram().pfnInit();
}

bool DesignWindowPlace(SDL_Window *pWindow) {
  int nW = 0, nH = 0;
  const char *psz = getenv("HYPERION_DESIGN_SIZE");
  if (psz != NULL && sscanf(psz, "%dx%d", &nW, &nH) == 2 && nW >= 320 && nH >= 240) {
    SDL_SetWindowSize(pWindow, nW, nH);
    SDL_SetWindowPosition(pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return true;
  }
  // a size to come back to from maximized, then maximized
  SDL_Rect usable;
  if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(pWindow), &usable)) {
    SDL_SetWindowSize(pWindow, usable.w * 3 / 4, usable.h * 3 / 4);
    SDL_SetWindowPosition(pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
  SDL_MaximizeWindow(pWindow);
  return true;
}

void DesignWindowSet(int nWidth, int nHeight) {
  SDL_Window *pWindow = WinMsgSdlWindow();
  if (pWindow == NULL)
    return;
  if ((SDL_GetWindowFlags(pWindow) & SDL_WINDOW_FULLSCREEN) != 0)
    PortToggleFullscreen();
  SDL_RestoreWindow(pWindow);
  SDL_SetWindowSize(pWindow, SDL_clamp(nWidth, 320, 7680), SDL_clamp(nHeight, 240, 4320));
  SDL_SetWindowPosition(pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_SyncWindow(pWindow);
  PortPresentRedraw();
}

void DesignWindowFullscreen(bool bOn) {
  SDL_Window *pWindow = WinMsgSdlWindow();
  if (pWindow == NULL)
    return;
  const bool bNow = (SDL_GetWindowFlags(pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
  if (bNow != bOn)
    PortToggleFullscreen();
}

void DesignWindowMaximize(void) {
  SDL_Window *pWindow = WinMsgSdlWindow();
  if (pWindow == NULL)
    return;
  if ((SDL_GetWindowFlags(pWindow) & SDL_WINDOW_FULLSCREEN) != 0)
    PortToggleFullscreen();
  SDL_MaximizeWindow(pWindow);
  PortPresentRedraw();
}

std::string DesignWindowFields(void) {
  SDL_Window *pWindow = WinMsgSdlWindow();
  int nW = 0, nH = 0;
  SDL_WindowFlags flags = 0;
  if (pWindow != NULL) {
    SDL_GetWindowSizeInPixels(pWindow, &nW, &nH);
    flags = SDL_GetWindowFlags(pWindow);
  }
  return "\"window\":{\"w\":" + std::to_string(nW) + ",\"h\":" + std::to_string(nH) +
         ",\"fullscreen\":" + ((flags & SDL_WINDOW_FULLSCREEN) != 0 ? "true" : "false") +
         ",\"maximized\":" + ((flags & SDL_WINDOW_MAXIMIZED) != 0 ? "true" : "false") + "}";
}

bool DesignGrabAsked(bool *pbPanels, bool *pbClip) {
  *pbClip = false;
  g_bClipFrame = false;
  if (!g_bGrabAsked) {
    // a clip's frame when one is due, a grab going first
    if (!DesignClipDue())
      return false;
    g_bClipFrame = true;
    *pbPanels = false;
    *pbClip = true;
    return true;
  }
  g_bGrabAsked = false;
  g_nGrabState = 2;
  *pbPanels = g_bGrabPanels;
  return true;
}

void DesignGrabTake(const void *pPixels, int nWidth, int nHeight, bool bBgra) {
  if (g_bClipFrame) {
    g_bClipFrame = false;
    DesignClipTake(pPixels, nWidth, nHeight, bBgra);
    return;
  }
  if (pPixels == NULL) {
    g_nGrabState = 4;
    return;
  }
  SDL_Surface *pSurface = SDL_CreateSurfaceFrom(nWidth, nHeight, bBgra ? SDL_PIXELFORMAT_BGRA32 : SDL_PIXELFORMAT_RGBA32,
                                                const_cast<void *>(pPixels), nWidth * 4);
  SDL_Surface *pRgb = pSurface != NULL ? SDL_ConvertSurface(pSurface, SDL_PIXELFORMAT_RGB24) : NULL;
  const bool bSaved = pRgb != NULL && SDL_SavePNG(pRgb, g_grabPath.c_str());
  SDL_DestroySurface(pRgb);
  SDL_DestroySurface(pSurface);
  g_nGrabState = bSaved ? 3 : 4;
  g_nGrabW = nWidth;
  g_nGrabH = nHeight;
}
