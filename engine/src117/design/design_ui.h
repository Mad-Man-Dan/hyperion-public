// design_ui.h -- what the design engine's Dear ImGui glue (design_imgui.cpp) and its panels
// (design_panels.cpp) share.
#ifndef DESIGN_UI_H
#define DESIGN_UI_H

#include <string>
#include <vector>
#include "imgui.h"
#include "../port/platform/port_json.h"

// The program a build of the design base makes, each with its own name, window, port and panels:
// the design engine (design_main.cpp) or Hyperion Studio (studio/studio_main.cpp). A build links
// one of them.
struct DesignProgram {
  const char *pszName;   // design_status's engine
  const char *pszTitle;  // the window's title
  int nPort;             // the live channel's port when the command line gives none
  double dLayout;        // its default layout's version: a saved layout older than it is laid out again
  bool bPanelsAlways;    // its panels drawn out of a game too, not only in one
  bool bOwnRoom;         // its panels give the game's room (DesignRoomSet), not the central node
  void (*pfnInit)(void);                                // its own commands, once at the start
  void (*pfnStyle)(void);                               // its look, once Dear ImGui starts; null for the dark one
  void (*pfnMenuBar)(void);                             // its menu bar, each frame
  void (*pfnPanels)(void);                              // its panels, each frame they are drawn
  void (*pfnLayout)(ImGuiID dock, const ImVec2 &size);  // its panels' first places
  void (*pfnPoll)(void);                                // each poll of the channel; may be null
};
const DesignProgram &DesignThisProgram(void);

// The game's room as a program's panel gives it, in the main viewport's coordinates, set each
// frame the panel shows it (the studio's Game pane); a frame without one hides the game behind the
// panels. Without a program room the game takes the dockspace's central node. A click in the room
// goes to the game, though the panel's window is under it.
void DesignRoomSet(float fX, float fY, float fW, float fH);

// The design engine's menu bar, over everything, in a game and out of one.
void DesignMenuBar(void);
// The panels, docked round the game or floating, each frame of a game.
void DesignPanels(void);
// The panels' first places, docked into the dockspace dock of size size, when there is no saved
// layout or the Window menu resets it.
void DesignDefaultLayout(ImGuiID dock, const ImVec2 &size);
// A press in the game's room before the game sees it, at a point of the window in its pixels,
// nButton SDL's (1 left, 3 right). True when a panel waiting for a place took it.
bool DesignPanelsPress(float fX, float fY, int nButton);
// The Window menu's reset: the default layout again on the next frame.
void DesignLayoutReset(void);
// The scenario, style and looks panels, each in its own file.
void DesignScenarioPanel(void);
void DesignStylePanel(void);
void DesignLooksPanel(void);
void DesignPainterPanel(void);
// The Record panel, clips to share (design_record.cpp).
void DesignRecordPanel(void);
// What the looks and painter panels share (design_looks.cpp): the game's palette as looks_palette
// last gave it, 0xRRGGBB each and empty until it comes, and asked again; the templates in
// design/looks/templates/, and a template's fields for the look command, its aura over an aura as
// it starts; the looks listed again.
const std::vector<unsigned> &DesignLooksPalette(void);
void DesignLooksPaletteAgain(void);
const std::vector<PortJson> &DesignLookTemplates(void);
std::string DesignLookTemplateFields(const PortJson &t);
const char *DesignAuraFirst(void);
void DesignLooksAgain(void);
// F8 (style A or B) and F9 (Again, with Shift a start point here), by SDL scancode.
void DesignPanelsKey(int nScancode, bool bShift);

// How the game and the panels share the window: the panels docked beside the game, the game
// filling the window with the panels over it, or the game alone (F11 switches to it and back).
enum { DESIGN_VIEW_BESIDE = 0, DESIGN_VIEW_OVER = 1, DESIGN_VIEW_GAME = 2 };
int DesignViewMode(void);
void DesignViewSet(int nMode);

#endif
