// design_hud.cpp -- the HUD from the channel (design_hud): the designs there are, the one chosen with
// Mech's picks and a light colour, and the HUD hidden. The picks are the design engine's own
// settings (settings.json in its data folder, testrun\design), written as Settings' HUD tab writes
// them (port_front_hud.cpp, HudTabAccept), so the player's Hyperion keeps its own. Hidden, the
// window draws no design (port_gpu.cpp) and a layer takes away the classic console, so the
// map has every row (port_console.cpp).
#include <stdio.h>
#include <string>
#include <vector>
#include "../port/hud/port_hud.h"
#include "../port/platform/port_json.h"
#include "../port/platform/port_settings.h"
#include "../port/render/port_overlay.h"
#include "../port/tools/port_control.h"
#include "design_engine.h"
#include "design_hooks.h"

namespace {

bool g_bHidden;

bool HiddenShown(void) {
  return g_bHidden;
}

// The classic console taken away while the HUD is hidden; it draws nothing itself.
const PortOverlayLayer kHidden = {"design hud hidden", 60, PORT_OVERLAY_HIDES_CONSOLE, HiddenShown,
                                  NULL, NULL, NULL, NULL, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kHidden);

std::string Setting(const char *pszName, const char *pszDefault) {
  char sz[260] = "";
  return PortSettingString("hud", pszName, sz, sizeof sz) && sz[0] != '\0' ? std::string(sz) : pszDefault;
}

bool CmdHud(const PortJson &j, std::string *pAnswer) {
  bool bChanged = false;
  const std::string design = j["design"].Str();
  if (!design.empty()) {
    bool bFound = false;
    std::string names;
    for (const PortHudDesignInfo &d : PortHudDesigns()) {
      names += (names.empty() ? "" : ", ") + d.id;
      if (d.id == design)
        bFound = !d.bHeld;
    }
    if (!bFound) {
      *pAnswer = "no design " + design + " to choose; the designs: " + names;
      return false;
    }
    PortSettingSetString("hud", "design", design.c_str());
    bChanged = true;
  }
  static const char *const kPicks[] = {"layout", "size", "material"};
  for (const char *pszPick : kPicks)
    if (j[pszPick].kind == PortJson::STRING) {
      PortSettingSetString("hud", pszPick, j[pszPick].s.c_str());
      bChanged = true;
    }
  if (j["light"].kind == PortJson::STRING) {
    const std::string key = Setting("design", "classic") + ".light";
    PortSettingSetString("hud", key.c_str(), j["light"].s.c_str());
    bChanged = true;
  }
  if (bChanged)
    PortHudReload();
  if (j["hidden"].kind == PortJson::BOOL)
    g_bHidden = j["hidden"].b;
  *pAnswer = DesignHudFields();
  return true;
}

}  // namespace

void DesignHudInit(void) {
  PortControlCommand("design_hud", CmdHud);
}

std::string DesignHudFields(void) {
  std::string designs;
  for (const PortHudDesignInfo &d : PortHudDesigns())
    designs += std::string(designs.empty() ? "" : ",") + "{\"id\":" + PortJsonQuote(d.id) + ",\"name\":" +
               PortJsonQuote(d.name) + ",\"held\":" + (d.bHeld ? "true" : "false") + "}";
  return "\"hud\":{\"design\":" + PortJsonQuote(Setting("design", "classic")) + ",\"layout\":" +
         PortJsonQuote(Setting("layout", "span")) + ",\"size\":" + PortJsonQuote(Setting("size", "full")) +
         ",\"material\":" + PortJsonQuote(Setting("material", "gunmetal")) +
         ",\"hidden\":" + (g_bHidden ? "true" : "false") + ",\"plays\":" + (PortHudPlays() ? "true" : "false") +
         ",\"designs\":[" + designs + "]}";
}

bool DesignHudHidden(void) {
  return g_bHidden || DesignGamePane();  // a pane of the studio shows the map alone
}
