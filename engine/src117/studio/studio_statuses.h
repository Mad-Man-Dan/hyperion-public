// studio_statuses.h -- what the Statuses workspace (studio_statuses.cpp) gives the other areas: the
// mod's statuses by name, a status in words, and a pulldown that picks one.
#ifndef STUDIO_STATUSES_H
#define STUDIO_STATUSES_H

#include <string>
#include <vector>

// The statuses the open mod has: its own, then those of the mods it depends on, by name.
std::vector<std::string> StudioStatusNames(void);
// The name a player reads ("Venom"), or the record's name when it gives none.
std::string StudioStatusLabel(const std::string &name);
// What it does in a sentence: "Slows by 50 % and deals 4 damage a second for 6 seconds; a new hit
// restarts it".
std::string StudioStatusSentence(const std::string &name);
// A pulldown of the statuses at the cursor, with "none" when bNone; true when one is picked ("" for
// none).
bool StudioStatusPick(const char *pszId, const std::string &now, float fW, std::string *pPicked, bool bNone = false,
                      float fH = 24.0f);
// The Statuses workspace shown with a status chosen.
void StudioStatusesShow(const std::string &name);

#endif
