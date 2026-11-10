// studio_ab.h -- fights played out in the canvas game, and A and B, two versions of a unit fought
// against the same units with the same seeds (studio_ab.cpp). The Balance workspace shows it as its
// second page; the Test workspace and a matchup's cell open it on their fight.
#ifndef STUDIO_AB_H
#define STUDIO_AB_H

#include <string>
#include "imgui.h"

// Whether Balance shows the fights played out (else the grid worked out), and the choice between
// the two drawn at the top right of the workspace a to b.
bool StudioAbShown(void);
void StudioAbPages(ImVec2 a, ImVec2 b);
// The page itself, in the workspace's rectangle.
void StudioAbWorkspace(ImVec2 a, ImVec2 b);
// The page shown on a fight: nYours of the unit against units, a JSON list of {unit, count, player}
// rows (player 0 joins the unit's side), as studio_try takes them.
void StudioAbOpen(const std::string &unit, int nYours, const std::string &unitsJson);
// Each frame: a run that waits too long on the game is stopped.
void StudioAbTick(void);
void StudioAbInit(void);

#endif
