// studio_modes.h -- what the Mode workspace's files share (studio_modes.cpp, studio_modeshow.cpp):
// the mod's mode block read as a declaration the engine's own calls take, the maps the workspace
// may read, and the two views drawn from the mode's own Luau.
#ifndef STUDIO_MODES_H
#define STUDIO_MODES_H

#include <string>
#include <vector>

struct PortModeDecl;

// The mode block of the mod open, read as the engine reads an installed mode's, so the set-up's
// own calls answer for the mod as the studio has it: PortModeEffect, PortModeRead, PortModeShown
// and PortModePreview all take a declaration. The words and the numbers come from the studio's
// model, so an unsaved edit shows at once; the Luau those calls run is the mod folder's, so a
// change to a script shows once it is saved. False when no mod is open, the mod is not a mode, or
// the block does not read, with why in *pWhy.
bool StudioModeDecl(PortModeDecl *pOut, std::string *pWhy);
// Every setting at its base, as JSON object text: where both views start.
std::string StudioModeBases(void);

// A map the studio can read: its file name and its path.
struct StudioModeMap {
  std::string name, path;
};
// The maps of the data folder, the game folder and each --maps folder, by name; read once.
const std::vector<StudioModeMap> &StudioModeMaps(void);

// The views drawn at the cursor in the workspace's body, fW the form's width.
// Set-up: the mode's own tab as the set-up draws it, from the mode's words and its Luau.
void StudioModeSeenView(float fW);
// Preview: what the mode's preview draws of the map for the settings the Set-up view holds.
void StudioModePreviewView(float fW);
// Its start: the commands of the two views (studio_modeshow.cpp).
void StudioModeShowInit(void);

#endif
