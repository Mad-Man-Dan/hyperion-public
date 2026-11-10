// studio_theme.cpp -- Hyperion Studio's look (studio_ui.h): Hyperion's own typefaces, which the exe
// carries (fonts.res), and the design's colours (design/55-looks-studio/studio.css), dark, with a
// light set beside them. The scale is the display's times the modder's own (100 to 200 %), and
// both apply again when either changes.
#include <windows.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "../port/platform/port_settings.h"
#include "../port/platform/port_winmsg.h"  // WinMsgSdlWindow
#include "studio_ui.h"

namespace {

ImFont *g_apFonts[STUDIO_FONTS];
// each face's line height over its em (hhea's ascent less descent over head's units an em): Dear
// ImGui sizes a face by its line height, the design by its em
float g_afEm[STUDIO_FONTS] = {1.0f, 1.0f, 1.0f, 1.0f};
float g_fScale = 1.0f;     // the modder's: 1 to 2
float g_fDisplay = 1.0f;   // the display's
bool g_bLight;
bool g_bApplied;

// a colour as rgb and alpha
struct Tok {
  unsigned rgb;
  float a;
};

// the design's tokens in StudioTok's order
const Tok kDark[TK_COUNT] = {
    {0x020913, 1.0f},  {0x061A33, 1.0f},  {0x01060D, 1.0f},  {0x051325, 0.94f}, {0x081C34, 0.72f},
    {0x00040A, 0.85f}, {0x276096, 0.32f}, {0x3980BC, 0.5f},  {0x276096, 0.16f}, {0x124070, 0.55f},
    {0x1E6EC8, 0.32f}, {0xCCE7FF, 1.0f},  {0x8DBFEA, 1.0f},  {0x5A7FA3, 1.0f},  {0x39506D, 1.0f},
    {0xFFFFFF, 1.0f},  {0xC6F0FF, 1.0f},  {0x66D5FE, 1.0f},  {0x9ECCF3, 1.0f},  {0xC9B8FF, 1.0f},
    {0xFFE490, 1.0f},  {0xDAA308, 0.7f},  {0x7BD88F, 1.0f},  {0xFF6B5E, 1.0f},  {0xFFB347, 1.0f},
    {0xE2A8FF, 1.0f},  {0xC478FF, 0.65f}, {0x4198E9, 1.0f},  {0x9FEAFF, 1.0f},  {0x000C16, 1.0f},
    {0x002B4F, 1.0f},  {0x006AA5, 1.0f},  {0x00D4FF, 1.0f},  {0x0480D5, 0.35f}, {0x002346, 1.0f},
    {0x00467D, 1.0f},  {0x010305, 1.0f},  {0x09223C, 1.0f},  {0x3780BC, 1.0f},  {0x00B4FF, 1.0f},
    {0x010918, 0.97f}, {0x498FDA, 1.0f},  {0x0060FF, 0.35f}, {0x2A1B00, 1.0f},  {0x6A4A00, 1.0f},
    {0xD8A52A, 1.0f},  {0x011A0C, 1.0f},  {0x0A4A26, 1.0f},  {0x000000, 1.0f},  {0x000000, 0.6f},
    {0x3980BC, 0.14f}, {0x051325, 1.0f},  {0x041224, 0.95f}, {0x010812, 0.95f},
};
const Tok kLight[TK_COUNT] = {
    {0xE7EDF4, 1.0f},  {0xF6F9FC, 1.0f},  {0xDCE5EF, 1.0f},  {0xFFFFFF, 0.97f}, {0xE8EFF7, 0.9f},
    {0xF7FAFD, 1.0f},  {0x285A8C, 0.22f}, {0x2864A0, 0.42f}, {0x2860A0, 0.07f}, {0x286EBE, 0.16f},
    {0x1E6EC8, 0.16f}, {0x10243A, 1.0f},  {0x2F5578, 1.0f},  {0x5D7590, 1.0f},  {0x9AAABB, 1.0f},
    {0x000814, 1.0f},  {0x0C3A5E, 1.0f},  {0x0A6FA8, 1.0f},  {0x134D7C, 1.0f},  {0x6B4FD0, 1.0f},
    {0x9A6A00, 1.0f},  {0xA87400, 0.6f},  {0x1E8A3C, 1.0f},  {0xC8372B, 1.0f},  {0xB86A00, 1.0f},
    {0x8A3FC4, 1.0f},  {0x8A3FC4, 0.6f},  {0x2A7FD0, 1.0f},  {0x0A6FA8, 1.0f},  {0xFFFFFF, 1.0f},
    {0xDCE8F4, 1.0f},  {0x6A9CC8, 1.0f},  {0x0A8AD8, 1.0f},  {0x0480D5, 0.2f},  {0xCFE4FA, 1.0f},
    {0xA8CDF2, 1.0f},  {0xFFFFFF, 1.0f},  {0xE4EEF8, 1.0f},  {0x7AA6CF, 1.0f},  {0x0A8AD8, 1.0f},
    {0xFAFCFF, 0.98f}, {0x6A9CC8, 1.0f},  {0x0060FF, 0.15f}, {0xFFF3D6, 1.0f},  {0xF3D68A, 1.0f},
    {0xB88A10, 1.0f},  {0xE6F7EA, 1.0f},  {0xBFE8C9, 1.0f},  {0x8094AA, 0.5f},  {0x000000, 0.15f},
    {0x3980BC, 0.12f}, {0xFFFFFF, 1.0f},  {0xF4F8FC, 1.0f},  {0xE2EAF3, 1.0f},
};

// A typeface the exe carries, by its resource name; null when it has none.
const void *Resource(const char *pszName, int *pnBytes) {
  HRSRC hRes = FindResourceA(NULL, pszName, MAKEINTRESOURCEA(10));  // RT_RCDATA
  HGLOBAL hData = hRes != NULL ? LoadResource(NULL, hRes) : NULL;
  const void *p = hData != NULL ? LockResource(hData) : NULL;
  *pnBytes = p != NULL ? static_cast<int>(SizeofResource(NULL, hRes)) : 0;
  return p;
}

unsigned Be16(const unsigned char *p) {
  return (p[0] << 8) | p[1];
}

unsigned Be32(const unsigned char *p) {
  return (static_cast<unsigned>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

// A TrueType face's line height over its em, from its head and hhea tables; 1 when unread.
float EmRatio(const void *pData, int nBytes) {
  const unsigned char *p = static_cast<const unsigned char *>(pData);
  if (nBytes < 12)
    return 1.0f;
  const unsigned nTables = Be16(p + 4);
  unsigned nUnits = 0;
  int nAscent = 0, nDescent = 0;
  for (unsigned i = 0; i < nTables && 12 + i * 16 + 16 <= static_cast<unsigned>(nBytes); ++i) {
    const unsigned char *r = p + 12 + i * 16;
    const unsigned off = Be32(r + 8);
    if (off + 8 > static_cast<unsigned>(nBytes))
      continue;
    if (memcmp(r, "head", 4) == 0 && off + 20 <= static_cast<unsigned>(nBytes))
      nUnits = Be16(p + off + 18);
    if (memcmp(r, "hhea", 4) == 0) {
      nAscent = static_cast<short>(Be16(p + off + 4));
      nDescent = static_cast<short>(Be16(p + off + 6));
    }
  }
  return nUnits > 0 && nAscent - nDescent > 0 ? static_cast<float>(nAscent - nDescent) / nUnits : 1.0f;
}

ImFont *Face(int nFont, const char *pszResource) {
  int nBytes = 0;
  const void *p = Resource(pszResource, &nBytes);
  if (p == NULL)
    return NULL;
  g_afEm[nFont] = EmRatio(p, nBytes);
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false;  // a resource stays mapped for the process's life
  return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<void *>(p), nBytes, kStudioFs, &cfg);
}

void Apply(void) {
  ImGuiIO &io = ImGui::GetIO();
  ImGuiStyle &s = ImGui::GetStyle();
  s = ImGuiStyle();
  s.WindowRounding = 3.0f;
  s.ChildRounding = 0.0f;
  s.PopupRounding = 3.0f;
  s.FrameRounding = 2.0f;
  s.GrabRounding = 2.0f;
  s.ScrollbarRounding = 2.0f;
  s.ScrollbarSize = 8.0f;
  s.WindowPadding = ImVec2(6, 6);
  s.FramePadding = ImVec2(8, 4);
  s.ItemSpacing = ImVec2(6, 6);
  s.ItemInnerSpacing = ImVec2(6, 4);
  s.CellPadding = ImVec2(6, 3);
  s.WindowBorderSize = 0.0f;
  s.ChildBorderSize = 0.0f;
  s.PopupBorderSize = 1.0f;
  s.ScaleAllSizes(g_fDisplay * g_fScale);
  s.FontScaleDpi = g_fDisplay * g_fScale;
  StudioImGuiColours();
  if (!g_bApplied) {
    // the typefaces once: Dear ImGui draws them sharp at any size
    io.Fonts->Clear();
    g_apFonts[STUDIO_FONT_TEXT] = Face(STUDIO_FONT_TEXT, "HYPERION_FONT_TEXT");
    g_apFonts[STUDIO_FONT_BOLD] = Face(STUDIO_FONT_BOLD, "HYPERION_FONT_BOLD");
    g_apFonts[STUDIO_FONT_MONO] = Face(STUDIO_FONT_MONO, "HYPERION_FONT_MONO");
    g_apFonts[STUDIO_FONT_WIDE] = Face(STUDIO_FONT_WIDE, "HYPERION_FONT_WIDE");
    if (g_apFonts[STUDIO_FONT_TEXT] == NULL)
      g_apFonts[STUDIO_FONT_TEXT] = io.Fonts->AddFontDefault();
    for (ImFont *&p : g_apFonts)
      if (p == NULL)
        p = g_apFonts[STUDIO_FONT_TEXT];
    io.FontDefault = g_apFonts[STUDIO_FONT_TEXT];
  }
  s.FontSizeBase = kStudioFs * g_afEm[STUDIO_FONT_TEXT];
  g_bApplied = true;
}

}  // namespace

ImU32 StudioCol(int tok, float fAlpha) {
  const Tok &k = (g_bLight ? kLight : kDark)[tok >= 0 && tok < TK_COUNT ? tok : TK_INK];
  return ImGui::GetColorU32(ImVec4(((k.rgb >> 16) & 0xFF) / 255.0f, ((k.rgb >> 8) & 0xFF) / 255.0f,
                                   (k.rgb & 0xFF) / 255.0f, k.a * fAlpha));
}

float StudioPx(float f) {
  return f * g_fDisplay * g_fScale;
}

void StudioImGuiColours(void) {
  ImVec4 *c = ImGui::GetStyle().Colors;
  auto v = [](int tok, float a = 1.0f) { return ImGui::ColorConvertU32ToFloat4(StudioCol(tok, a)); };
  c[ImGuiCol_Text] = v(TK_INK);
  c[ImGuiCol_TextDisabled] = v(TK_INK3);
  c[ImGuiCol_WindowBg] = v(TK_PANE);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = v(TK_MENU);
  c[ImGuiCol_Border] = v(TK_MENU_RIM);
  c[ImGuiCol_FrameBg] = v(TK_WELL);
  c[ImGuiCol_FrameBgHovered] = v(TK_WELL);
  c[ImGuiCol_FrameBgActive] = v(TK_WELL);
  c[ImGuiCol_TitleBg] = v(TK_BAR);
  c[ImGuiCol_TitleBgActive] = v(TK_BAR);
  c[ImGuiCol_MenuBarBg] = v(TK_BAR);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = v(TK_LINE2);
  c[ImGuiCol_ScrollbarGrabHovered] = v(TK_SEL_LINE);
  c[ImGuiCol_ScrollbarGrabActive] = v(TK_SEL_LINE);
  c[ImGuiCol_CheckMark] = v(TK_FILL);
  c[ImGuiCol_SliderGrab] = v(TK_HI);
  c[ImGuiCol_SliderGrabActive] = v(TK_HI);
  c[ImGuiCol_Button] = v(TK_BTN_BOT);
  c[ImGuiCol_ButtonHovered] = v(TK_ON_BOT);
  c[ImGuiCol_ButtonActive] = v(TK_ON_BOT);
  c[ImGuiCol_Header] = v(TK_MENU_SEL);
  c[ImGuiCol_HeaderHovered] = v(TK_MENU_SEL);
  c[ImGuiCol_HeaderActive] = v(TK_MENU_SEL);
  c[ImGuiCol_Separator] = v(TK_LINE);
  c[ImGuiCol_TextSelectedBg] = v(TK_SEL);
  c[ImGuiCol_NavCursor] = v(TK_BTN_RIM_HI);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0.01f, 0.03f, 0.55f);
  c[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableHeaderBg] = v(TK_PANE2);
  c[ImGuiCol_TableBorderStrong] = v(TK_LINE);
  c[ImGuiCol_TableBorderLight] = v(TK_ROW);
  c[ImGuiCol_TableRowBgAlt] = v(TK_ROW, 0.5f);
}

void StudioStyle(void) {
  double d = 1.0;
  if (PortSettingNumber("studio", "scale", &d))
    g_fScale = SDL_clamp(static_cast<float>(d), 1.0f, 2.0f);
  double dLight = 0.0;
  PortSettingNumber("studio", "light", &dLight);
  g_bLight = dLight != 0.0;
  SDL_Window *pWindow = WinMsgSdlWindow();
  const float f = pWindow != NULL ? SDL_GetWindowDisplayScale(pWindow) : 1.0f;
  g_fDisplay = f >= 1.0f && f <= 4.0f ? f : 1.0f;
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  Apply();
}

void StudioStyleCheck(void) {
  // the window moved to a display of another scale
  SDL_Window *pWindow = WinMsgSdlWindow();
  const float f = pWindow != NULL ? SDL_GetWindowDisplayScale(pWindow) : 1.0f;
  if (f >= 1.0f && f <= 4.0f && f != g_fDisplay) {
    g_fDisplay = f;
    Apply();
  }
}

float StudioScale(void) {
  return g_fScale;
}

void StudioScaleSet(float fScale) {
  g_fScale = SDL_clamp(fScale, 1.0f, 2.0f);
  PortSettingSetNumber("studio", "scale", g_fScale);
  Apply();
}

bool StudioLight(void) {
  return g_bLight;
}

void StudioLightSet(bool bLight) {
  g_bLight = bLight;
  PortSettingSetNumber("studio", "light", bLight ? 1.0 : 0.0);
  Apply();
}

ImFont *StudioFont(int nFont) {
  return nFont >= 0 && nFont < STUDIO_FONTS ? g_apFonts[nFont] : NULL;
}

float StudioFontEm(int nFont) {
  return nFont >= 0 && nFont < STUDIO_FONTS ? g_afEm[nFont] : 1.0f;
}

void StudioPushFont(int nFont, float fPx) {
  ImGui::PushFont(StudioFont(nFont), fPx * StudioFontEm(nFont));
}
