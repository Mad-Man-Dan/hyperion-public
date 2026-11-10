// design_imgui.cpp -- the design engine's panels on Dear ImGui (docking), drawn on the GPU through
// its SDL3 and SDL_GPU backends, last over the window's picture. The game goes in the dockspace's
// central node, which the panels leave free: PortGpuGameBox asks DesignGameRoom, so the picture and
// every mouse mapping follow it, and the game's view widens to the room's shape. Each SDL event
// comes here before Hyperion turns it into a window message: what the panels take the game never
// sees, and a key or button that went down to the game always comes up to it. Over the panels
// the game keeps its pointer, through the overlay layer below, as over Debug Tools' window.
#include <windows.h>
#include <bitset>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"  // the dock builder
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "../port/render/port_gpu.h"           // PortGpuGameBox
#include "../port/render/port_gpu_internal.h"  // g_device, g_window, g_claimed, g_swapFormat
#include "../port/render/port_overlay.h"
#include "../port/render/port_present.h"  // PortPresentClientSize
#include "../port/render/port_view.h"     // PortViewRequest, PortViewScreenToMap, g_portView
#include "../port/platform/port_json.h"
#include "../port/platform/port_settings.h"
#include "../port/render/port_camera.h"  // PortCameraZoomTo and its limits
#include "../port/tools/port_control.h"
#include "../port/tools/port_startup.h"  // PortStartupPending
#include "design_engine.h"
#include "design_hooks.h"
#include "design_ui.h"

namespace {

bool g_bReady;
bool g_bFailed;
bool g_bDrew;   // this frame's panels are drawn, for the grab
bool g_bReset;  // the default layout again
// The default layout's version: a saved layout older than it is laid out again, so new panels
// find their places.
std::string g_ini;
const ImGuiID kDock = 0x48594431;  // the dockspace's id, kept in imgui.ini with its layout
// the game's room, the central node, in fractions of the window; none out of a game
bool g_bRoom;
float g_afRoom[4];
// the room a program's panel gave this frame (DesignRoomSet), in the main viewport's coordinates
bool g_bRoomGiven;
float g_afRoomGiven[4];
// what went down to the game, and what the panels took, so each comes up to the same side
unsigned g_uButtonsGame;
unsigned g_uButtonsPanels;
std::bitset<SDL_SCANCODE_COUNT> g_keysGame;
std::bitset<SDL_SCANCODE_COUNT> g_keysPanels;
// how the game and the panels share the window, and the mode F11 goes back to
int g_nView = DESIGN_VIEW_BESIDE;
int g_nViewBefore = DESIGN_VIEW_BESIDE;
// the game's view widened to the room's shape once the room has settled
int g_nWantWidth;
Uint64 g_nWantSince;
int g_nAsked;
// a drag's events after its press, given one a frame so the panels see the button held while it moves
std::vector<SDL_Event> g_drip;

// The text field's place for an input method, without ever stopping text input: Hyperion keeps
// it on the whole session for the game's own typing.
void ImeData(ImGuiContext *, ImGuiViewport *, ImGuiPlatformImeData *pData) {
  if (!pData->WantVisible || g_window == NULL)
    return;
  SDL_Rect r = {static_cast<int>(pData->InputPos.x), static_cast<int>(pData->InputPos.y), 1,
                static_cast<int>(pData->InputLineHeight)};
  SDL_SetTextInputArea(g_window, &r, 0);
}

bool Ready(void) {
  if (g_bReady)
    return true;
  if (g_bFailed || g_device == NULL || !g_claimed || g_window == NULL)
    return false;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  // no keyboard navigation, so a click on a panel leaves the game its keys; Hyperion's cursors
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange;
  g_ini = DesignDataFolder() + "imgui.ini";
  io.IniFilename = g_ini.c_str();
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  float fScale = SDL_GetWindowDisplayScale(g_window);
  if (fScale < 1.0f || fScale > 4.0f)
    fScale = 1.0f;
  style.ScaleAllSizes(fScale);
  style.FontScaleDpi = fScale;
  style.WindowRounding = 2.0f;
  style.FrameRounding = 2.0f;
  char szFonts[MAX_PATH];
  const UINT n = GetWindowsDirectoryA(szFonts, MAX_PATH);
  const std::string font = std::string(szFonts, n) + "\\Fonts\\segoeui.ttf";
  if (n > 0 && GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES)
    io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);
  if (!ImGui_ImplSDL3_InitForSDLGPU(g_window)) {
    g_bFailed = true;
    return false;
  }
  // Hyperion holds the mouse itself (port_winmsg.cpp); the backend's own capture would let it go
  ImGui_ImplSDL3_SetMouseCaptureMode(ImGui_ImplSDL3_MouseCaptureMode_Disabled);
  ImGui_ImplSDLGPU3_InitInfo info;
  info.Device = g_device;
  info.ColorTargetFormat = g_swapFormat;
  info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  if (!ImGui_ImplSDLGPU3_Init(&info)) {
    ImGui_ImplSDL3_Shutdown();
    g_bFailed = true;
    return false;
  }
  ImGui::GetPlatformIO().Platform_SetImeDataFn = ImeData;
  SDL_SetWindowTitle(g_window, DesignThisProgram().pszTitle);
  double dView = DESIGN_VIEW_BESIDE;
  PortSettingNumber("design", "view", &dView);
  g_nView = SDL_clamp(static_cast<int>(dView), DESIGN_VIEW_BESIDE, DESIGN_VIEW_GAME);
  g_nViewBefore = g_nView == DESIGN_VIEW_GAME ? DESIGN_VIEW_BESIDE : g_nView;
  // the camera may zoom out as far as it goes, under a key of this program's own: the settings
  // file is shared with the game, whose own limit the player sets
  double dZoomMax = 0.0;
  if (!PortSettingNumber("hyperion", "designZoomMax", &dZoomMax))
    PortSettingSetNumber("hyperion", "designZoomMax", 8.0);
  // a layout kept from before the panels it lacks is laid out again, once
  double dLayout = 0.0;
  PortSettingNumber("design", "layout", &dLayout);
  if (dLayout < DesignThisProgram().dLayout) {
    g_bReset = true;
    PortSettingSetNumber("design", "layout", DesignThisProgram().dLayout);
  }
  // the program's own look over the dark one, sized as the window's display asks
  if (DesignThisProgram().pfnStyle)
    DesignThisProgram().pfnStyle();
  g_bReady = true;
  return true;
}

// The room in a target's pixels.
void RoomIn(float fW, float fH, float *pfX, float *pfY, float *pfW, float *pfH) {
  *pfX = SDL_floorf(g_afRoom[0] * fW);
  *pfY = SDL_floorf(g_afRoom[1] * fH);
  *pfW = SDL_floorf(g_afRoom[2] * fW);
  *pfH = SDL_floorf(g_afRoom[3] * fH);
}

bool InRoomOf(float fX, float fY);

// Whether a window point, in its pixels, is over a panel, the menu bar or an open list: found by
// its place, not by where the last frame had the pointer, so a click that comes with its move
// goes to the right side.
bool OverPanel(float fX, float fY) {
  if (!g_bReady)
    return false;
  const float fDensity = SDL_GetWindowPixelDensity(g_window) > 0.0f ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
  ImGuiWindow *pHovered = NULL;
  ImGuiWindow *pUnder = NULL;
  ImGui::FindHoveredWindowEx(ImVec2(fX / fDensity, fY / fDensity), false, &pHovered, &pUnder);
  // a program's room is the game's though its pane's window is under it; a list or menu open over
  // it is still the panels'
  if (pHovered != NULL && DesignThisProgram().bOwnRoom && g_bRoom && InRoomOf(fX, fY) &&
      (pHovered->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu | ImGuiWindowFlags_Tooltip)) == 0)
    return false;
  return pHovered != NULL;
}

// A window point, in its pixels, over the panels or off the game's room.
bool LayerOutside(float fX, float fY) {
  if (!g_bReady)
    return false;
  if (OverPanel(fX, fY) && g_uButtonsGame == 0)
    return true;
  if (!g_bRoom)
    return false;  // the game has the whole window
  int nW = 0, nH = 0;
  PortPresentClientSize(&nW, &nH);
  float x, y, w, h;
  RoomIn(static_cast<float>(nW), static_cast<float>(nH), &x, &y, &w, &h);
  return fX < x || fY < y || fX >= x + w || fY >= y + h;
}

bool LayerShown(void) {
  return g_bReady && DesignInGame();
}

// The panels round the game: the cursor reaches them, and over them the game keeps its pointer.
const PortOverlayLayer kPanels = {"design panels", 950, PORT_OVERLAY_FREE_CURSOR, LayerShown, NULL, NULL,
                                  NULL, NULL, NULL, LayerOutside, NULL};
PORT_OVERLAY_LAYER(kPanels);

bool PaneShown(void) {
  return g_bReady && DesignInGame() && DesignThisProgram().bOwnRoom;
}

// The game as a pane: no console under it.
const PortOverlayLayer kPane = {"design pane", 951, PORT_OVERLAY_HIDES_CONSOLE, PaneShown, NULL, NULL,
                                NULL, NULL, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kPane);

// A window point of an SDL event, in the window's pixels.
void EventPixels(float fX, float fY, float *pfX, float *pfY) {
  const float fDensity = g_window != NULL ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
  *pfX = fX * (fDensity > 0.0f ? fDensity : 1.0f);
  *pfY = fY * (fDensity > 0.0f ? fDensity : 1.0f);
}

// Whether a window point, in its pixels, is in the room.
bool InRoomOf(float fX, float fY) {
  int nW = 0, nH = 0;
  PortPresentClientSize(&nW, &nH);
  float x, y, w, h;
  RoomIn(static_cast<float>(nW), static_cast<float>(nH), &x, &y, &w, &h);
  return fX >= x && fY >= y && fX < x + w && fY < y + h;
}

bool InRoom(float fX, float fY) {
  if (!g_bRoom)
    return true;  // the game has the whole window
  int nW = 0, nH = 0;
  PortPresentClientSize(&nW, &nH);
  float x, y, w, h;
  RoomIn(static_cast<float>(nW), static_cast<float>(nH), &x, &y, &w, &h);
  return fX >= x && fY >= y && fX < x + w && fY < y + h;
}

// Up goes where down went: true when the panels have it.
bool UpTaken(unsigned *puGame, unsigned *puPanels, unsigned uBit) {
  if ((*puGame & uBit) != 0) {
    *puGame &= ~uBit;
    return false;
  }
  if ((*puPanels & uBit) != 0) {
    *puPanels &= ~uBit;
    return true;
  }
  return false;
}

// The game's view as wide as the room's shape asks, 480 rows high, once the room has stayed one
// shape a moment (a drag of a panel's edge goes through many).
void FollowRoom(int nTargetW, int nTargetH) {
  if (!DesignInGame())
    return;
  float x = 0.0f, y = 0.0f, w = static_cast<float>(nTargetW), h = static_cast<float>(nTargetH);
  if (g_bRoom)
    RoomIn(static_cast<float>(nTargetW), static_cast<float>(nTargetH), &x, &y, &w, &h);
  if (w < 64.0f || h < 64.0f)
    return;
  int nWant = static_cast<int>(480.0f * w / h / 16.0f + 0.5f) * 16;
  nWant = SDL_clamp(nWant, 640, 1712);
  const Uint64 nNow = SDL_GetTicks();
  if (nWant != g_nWantWidth) {
    g_nWantWidth = nWant;
    g_nWantSince = nNow;
    return;
  }
  if (nNow - g_nWantSince < 250 || nWant == g_nAsked)
    return;
  if (nWant != g_portView.nWidth)
    PortViewRequest(nWant);
  g_nAsked = nWant;
}

void Pass(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, ImDrawData *pData) {
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pTarget;
  target.load_op = SDL_GPU_LOADOP_LOAD;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  ImGui_ImplSDLGPU3_RenderDrawData(pData, pCmd, pPass);
  SDL_EndGPURenderPass(pPass);
}

const char *const kViewNames[] = {"beside", "over", "game"};

std::string ViewFields(void) {
  char sz[160];
  snprintf(sz, sizeof sz, "\"view\":{\"mode\":\"%s\",\"zoom\":%.3f,\"zoomMin\":%.3f,\"zoomMax\":%.3f}",
           kViewNames[g_nView], PortCameraZoomTarget(), PortCameraZoomMin(), PortCameraZoomMax());
  return sz;
}

// design_view {mode?, zoom?, zoomMax?}: how the game and the panels share the window (beside,
// over or game), the camera's zoom, and how far it may zoom out (1 to 8, kept under this
// program's own key, so the game's limit is left alone).
bool CmdView(const PortJson &j, std::string *pAnswer) {
  const std::string mode = j["mode"].Str();
  if (!mode.empty()) {
    int n = -1;
    for (int i = 0; i < 3; ++i)
      if (mode == kViewNames[i])
        n = i;
    if (n < 0) {
      *pAnswer = "mode is beside, over or game";
      return false;
    }
    DesignViewSet(n);
  }
  if (j["zoomMax"].kind == PortJson::NUMBER)
    PortSettingSetNumber("hyperion", "designZoomMax", SDL_clamp(j["zoomMax"].d, 1.0, 8.0));
  if (j["zoom"].kind == PortJson::NUMBER)
    PortCameraZoomTo(j["zoom"].d);
  *pAnswer = ViewFields();
  return true;
}

// design_click {x, y, button?, wheel?, to?}: a click at a window point in its pixels, as SDL events, so
// it reaches the panels as the mouse's do (the channel's click posts window messages, which the
// panels never see). button is SDL's, 1 left and 3 right; wheel turns it instead, notches away
// from the player.
bool CmdClick(const PortJson &j, std::string *pAnswer) {
  if (g_window == NULL) {
    *pAnswer = "no window yet";
    return false;
  }
  const float fDensity = SDL_GetWindowPixelDensity(g_window) > 0.0f ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
  const float fX = static_cast<float>(j["x"].Num(0)) / fDensity;
  const float fY = static_cast<float>(j["y"].Num(0)) / fDensity;
  const SDL_WindowID id = SDL_GetWindowID(g_window);
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.windowID = id;
  e.motion.x = fX;
  e.motion.y = fY;
  SDL_PushEvent(&e);
  if (j["wheel"].kind == PortJson::NUMBER) {
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.windowID = id;
    e.wheel.y = static_cast<float>(j["wheel"].d);
    e.wheel.mouse_x = fX;
    e.wheel.mouse_y = fY;
    SDL_PushEvent(&e);
  } else {
    // with to, a drag: pressed here, moved there in steps, let go there
    const bool bDrag = j["to"].kind == PortJson::ARRAY && j["to"].items.size() >= 2;
    const float fToX = bDrag ? static_cast<float>(j["to"].items[0].Num(0)) / fDensity : fX;
    const float fToY = bDrag ? static_cast<float>(j["to"].items[1].Num(0)) / fDensity : fY;
    for (int i = 0; i < 2; ++i) {
      if (i == 1 && bDrag)
        for (int n = 1; n <= 8; ++n) {
          SDL_zero(e);
          e.type = SDL_EVENT_MOUSE_MOTION;
          e.motion.windowID = id;
          e.motion.state = SDL_BUTTON_MASK(static_cast<Uint8>(j["button"].Num(1)));
          e.motion.x = fX + (fToX - fX) * n / 8.0f;
          e.motion.y = fY + (fToY - fY) * n / 8.0f;
          g_drip.push_back(e);
        }
      SDL_zero(e);
      e.type = i == 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
      e.button.windowID = id;
      e.button.button = static_cast<Uint8>(j["button"].Num(1));
      e.button.down = i == 0;
      e.button.clicks = 1;
      e.button.x = i == 0 ? fX : fToX;
      e.button.y = i == 0 ? fY : fToY;
      if (i == 1 && bDrag)
        g_drip.push_back(e);
      else
        SDL_PushEvent(&e);
    }
  }
  PortPresentRedraw();
  *pAnswer = "\"x\":" + std::to_string(static_cast<int>(j["x"].Num(0))) + ",\"y\":" +
             std::to_string(static_cast<int>(j["y"].Num(0)));
  return true;
}

// design_type {text?, key?, ctrl?, shift?}: typing as SDL events, so it reaches the panels as the
// keyboard's does: text as typed characters, or a key by its name (enter, escape, tab, backspace,
// delete, home, end, left, right, up, down, or a letter), with Ctrl or Shift held when asked.
bool CmdType(const PortJson &j, std::string *pAnswer) {
  if (g_window == NULL) {
    *pAnswer = "no window yet";
    return false;
  }
  const SDL_WindowID id = SDL_GetWindowID(g_window);
  SDL_Event e;
  const std::string key = j["key"].Str();
  if (!key.empty()) {
    static const struct {
      const char *pszName;
      SDL_Scancode sc;
      SDL_Keycode k;
    } kKeys[] = {{"enter", SDL_SCANCODE_RETURN, SDLK_RETURN},       {"escape", SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
                 {"tab", SDL_SCANCODE_TAB, SDLK_TAB},               {"backspace", SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE},
                 {"delete", SDL_SCANCODE_DELETE, SDLK_DELETE},      {"home", SDL_SCANCODE_HOME, SDLK_HOME},
                 {"end", SDL_SCANCODE_END, SDLK_END},               {"left", SDL_SCANCODE_LEFT, SDLK_LEFT},
                 {"right", SDL_SCANCODE_RIGHT, SDLK_RIGHT},         {"up", SDL_SCANCODE_UP, SDLK_UP},
                 {"down", SDL_SCANCODE_DOWN, SDLK_DOWN},            {"f5", SDL_SCANCODE_F5, SDLK_F5},
                 {"f9", SDL_SCANCODE_F9, SDLK_F9}};
    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
    SDL_Keycode k = SDLK_UNKNOWN;
    for (const auto &kk : kKeys)
      if (key == kk.pszName) {
        sc = kk.sc;
        k = kk.k;
      }
    if (sc == SDL_SCANCODE_UNKNOWN && key.size() == 1 && isalpha(static_cast<unsigned char>(key[0]))) {
      const char c = static_cast<char>(tolower(static_cast<unsigned char>(key[0])));
      sc = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'a'));
      k = static_cast<SDL_Keycode>(c);
    }
    if (sc == SDL_SCANCODE_UNKNOWN) {
      *pAnswer = "no key " + key;
      return false;
    }
    SDL_Keymod mod = SDL_KMOD_NONE;
    if (j["ctrl"].kind == PortJson::BOOL && j["ctrl"].b)
      mod |= SDL_KMOD_LCTRL;
    if (j["shift"].kind == PortJson::BOOL && j["shift"].b)
      mod |= SDL_KMOD_LSHIFT;
    for (int i = 0; i < 2; ++i) {
      SDL_zero(e);
      e.type = i == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      e.key.windowID = id;
      e.key.scancode = sc;
      e.key.key = k;
      e.key.mod = i == 0 ? mod : SDL_KMOD_NONE;  // let go with the key, so Ctrl is not left held
      e.key.down = i == 0;
      SDL_PushEvent(&e);
    }
  }
  const std::string text = j["text"].Str();
  if (!text.empty()) {
    // the event names its text, so the text is kept until long after it is read
    static std::string s_kept[16];
    static int s_nKept;
    std::string &kept = s_kept[s_nKept++ % 16];
    kept = text;
    SDL_zero(e);
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.windowID = id;
    e.text.text = kept.c_str();
    SDL_PushEvent(&e);
  }
  PortPresentRedraw();
  *pAnswer = "\"typed\":true";
  return true;
}

}  // namespace

void DesignImGuiInit(void) {
  PortControlCommand("design_click", CmdClick);
  PortControlCommand("design_type", CmdType);
  PortControlCommand("design_view", CmdView);
}

int DesignViewMode(void) {
  return g_nView;
}

void DesignViewSet(int nMode) {
  nMode = SDL_clamp(nMode, DESIGN_VIEW_BESIDE, DESIGN_VIEW_GAME);
  if (nMode == g_nView)
    return;
  if (nMode == DESIGN_VIEW_GAME)
    g_nViewBefore = g_nView;
  g_nView = nMode;
  g_nAsked = 0;  // the view's width found again for the new room
  PortSettingSetNumber("design", "view", nMode);
  PortPresentRedraw();
}

void DesignLayoutReset(void) {
  g_bReset = true;
}

bool DesignGamePane(void) {
  return DesignThisProgram().bOwnRoom;
}

bool DesignWantsFrame(void) {
  if (!g_bReady)
    return false;
  // While the game starts, its loading calls for a frame after every group of art it reads, a
  // few thousand times; drawing the panels and waiting on the display each time took the start
  // from under two seconds to forty. The panels are drawn a few times a second until it is done.
  if (PortStartupPending()) {
    static Uint64 s_uLast = 0;
    const Uint64 uNow = SDL_GetTicks();
    if (s_uLast != 0 && uNow - s_uLast < 250)
      return false;
    s_uLast = uNow;
  }
  return true;
}

bool DesignGameRoom(float fTargetW, float fTargetH, float *pfX, float *pfY, float *pfW, float *pfH) {
  if (!g_bRoom || !DesignInGame())
    return false;
  RoomIn(fTargetW, fTargetH, pfX, pfY, pfW, pfH);
  return *pfW >= 1.0f && *pfH >= 1.0f;
}

bool DesignEvent(const SDL_Event *pEvent) {
  if (!g_bReady)
    return false;
  ImGui_ImplSDL3_ProcessEvent(pEvent);
  const ImGuiIO &io = ImGui::GetIO();
  switch (pEvent->type) {
  case SDL_EVENT_MOUSE_BUTTON_DOWN: {
    const unsigned uBit = 1u << (pEvent->button.button & 31);
    float fX, fY;
    EventPixels(pEvent->button.x, pEvent->button.y, &fX, &fY);
    if (OverPanel(fX, fY) || (InRoom(fX, fY) && DesignPanelsPress(fX, fY, pEvent->button.button))) {
      g_uButtonsPanels |= uBit;
      return true;
    }
    g_uButtonsGame |= uBit;
    return false;
  }
  case SDL_EVENT_MOUSE_BUTTON_UP:
    return UpTaken(&g_uButtonsGame, &g_uButtonsPanels, 1u << (pEvent->button.button & 31));
  case SDL_EVENT_MOUSE_WHEEL: {
    float fX, fY;
    EventPixels(pEvent->wheel.mouse_x, pEvent->wheel.mouse_y, &fX, &fY);
    return OverPanel(fX, fY);
  }
  case SDL_EVENT_MOUSE_MOTION:
    // a panel's drag keeps its moves; the game's own drag keeps them too, over a panel or not
    return g_uButtonsPanels != 0 && g_uButtonsGame == 0;
  case SDL_EVENT_KEY_DOWN: {
    const SDL_Scancode sc = pEvent->key.scancode;
    if (sc == SDL_SCANCODE_F8 || sc == SDL_SCANCODE_F9) {
      // a style's flip, Again and a start point, wherever the keyboard is
      if (!pEvent->key.repeat)
        DesignPanelsKey(sc, (pEvent->key.mod & SDL_KMOD_SHIFT) != 0);
      return true;
    }
    if (sc == SDL_SCANCODE_F11) {
      // the game alone, and back
      if (!pEvent->key.repeat)
        DesignViewSet(g_nView == DESIGN_VIEW_GAME ? g_nViewBefore : DESIGN_VIEW_GAME);
      return true;
    }
    if (sc >= SDL_SCANCODE_COUNT)
      return false;
    if (g_keysGame[sc])
      return false;  // held: its repeats go where it went down
    if (g_keysPanels[sc] || io.WantCaptureKeyboard) {
      g_keysPanels[sc] = true;
      return true;
    }
    g_keysGame[sc] = true;
    return false;
  }
  case SDL_EVENT_KEY_UP: {
    const SDL_Scancode sc = pEvent->key.scancode;
    if (sc == SDL_SCANCODE_F11 || sc == SDL_SCANCODE_F8 || sc == SDL_SCANCODE_F9)
      return true;
    if (sc >= SDL_SCANCODE_COUNT)
      return false;
    if (g_keysGame[sc]) {
      g_keysGame[sc] = false;
      return false;
    }
    const bool bTaken = g_keysPanels[sc];
    g_keysPanels[sc] = false;
    return bTaken;
  }
  case SDL_EVENT_TEXT_INPUT:
  case SDL_EVENT_TEXT_EDITING:
    return io.WantTextInput;
  default:
    return false;
  }
}

void DesignPanelsDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nWidth, int nHeight) {
  g_bDrew = false;
  if (pTarget == NULL || !Ready())
    return;
  DesignBridgeDeliver();
  ImGui_ImplSDLGPU3_NewFrame();
  if (!g_drip.empty()) {
    SDL_PushEvent(&g_drip.front());
    g_drip.erase(g_drip.begin());
    PortPresentRedraw();
  }
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  const DesignProgram &prog = DesignThisProgram();
  const bool bGame = DesignInGame() || prog.bPanelsAlways;
  const bool bPanels = bGame && g_nView != DESIGN_VIEW_GAME;
  if (g_nView != DESIGN_VIEW_GAME)
    prog.pfnMenuBar();
  // over the game the panels let it show through a little
  ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = g_nView == DESIGN_VIEW_OVER ? 0.8f : 0.94f;
  const ImGuiViewport *pView = ImGui::GetMainViewport();
  if (g_bReset || ImGui::DockBuilderGetNode(kDock) == NULL) {
    g_bReset = false;
    prog.pfnLayout(kDock, pView->WorkSize);
  }
  // out of a game, or with the game alone, the panels and their places wait, kept
  ImGui::DockSpaceOverViewport(kDock, pView,
                               ImGuiDockNodeFlags_PassthruCentralNode | (bPanels ? 0 : ImGuiDockNodeFlags_KeepAliveOnly));
  g_bRoom = false;
  g_bRoomGiven = false;
  if (bPanels) {
    prog.pfnPanels();
    if (prog.bOwnRoom && pView->Size.x > 0.0f && pView->Size.y > 0.0f) {
      // the program's pane, or none: off the window, so the game is hidden behind the panels
      g_afRoom[0] = g_bRoomGiven ? (g_afRoomGiven[0] - pView->Pos.x) / pView->Size.x : 2.0f;
      g_afRoom[1] = g_bRoomGiven ? (g_afRoomGiven[1] - pView->Pos.y) / pView->Size.y : 2.0f;
      g_afRoom[2] = g_bRoomGiven ? g_afRoomGiven[2] / pView->Size.x : 0.0f;
      g_afRoom[3] = g_bRoomGiven ? g_afRoomGiven[3] / pView->Size.y : 0.0f;
      g_bRoom = true;
    }
    const ImGuiDockNode *pCentral = ImGui::DockBuilderGetCentralNode(kDock);
    if (!prog.bOwnRoom && g_nView == DESIGN_VIEW_BESIDE && pCentral != NULL && pView->Size.x > 0.0f &&
        pView->Size.y > 0.0f) {
      g_afRoom[0] = (pCentral->Pos.x - pView->Pos.x) / pView->Size.x;
      g_afRoom[1] = (pCentral->Pos.y - pView->Pos.y) / pView->Size.y;
      g_afRoom[2] = pCentral->Size.x / pView->Size.x;
      g_afRoom[3] = pCentral->Size.y / pView->Size.y;
      g_bRoom = true;
    }
  }
  ImGui::Render();
  ImDrawData *pData = ImGui::GetDrawData();
  ImGui_ImplSDLGPU3_PrepareDrawData(pData, pCmd);
  Pass(pCmd, pTarget, pData);
  g_bDrew = true;
  FollowRoom(nWidth, nHeight);
}

void DesignRoomSet(float fX, float fY, float fW, float fH) {
  g_bRoomGiven = fW >= 1.0f && fH >= 1.0f;
  g_afRoomGiven[0] = fX;
  g_afRoomGiven[1] = fY;
  g_afRoomGiven[2] = fW;
  g_afRoomGiven[3] = fH;
}

void DesignPanelsRedraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int, int) {
  if (g_bDrew && pTarget != NULL)
    Pass(pCmd, pTarget, ImGui::GetDrawData());
}

bool DesignWindowToMap(float fX, float fY, int *pnMapX, int *pnMapY) {
  int nW = 0, nH = 0;
  PortPresentClientSize(&nW, &nH);
  if (nW <= 0 || nH <= 0 || g_portView.nWidth <= 0 || g_portView.nHeight <= 0)
    return false;
  SDL_FRect box;
  PortGpuGameBox(static_cast<float>(nW), static_cast<float>(nH), static_cast<float>(g_portView.nWidth),
                 static_cast<float>(g_portView.nHeight), &box);
  if (box.w <= 0.0f || box.h <= 0.0f || fX < box.x || fY < box.y || fX >= box.x + box.w || fY >= box.y + box.h)
    return false;
  const int nX = static_cast<int>((fX - box.x) * g_portView.nWidth / box.w);
  const int nY = static_cast<int>((fY - box.y) * g_portView.nHeight / box.h);
  if (g_portView.nMapRows > 0 && nY >= g_portView.nMapRows)
    return false;  // over the console
  PortViewScreenToMap(nX, nY, pnMapX, pnMapY);
  return true;
}
