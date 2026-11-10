// port_modvoice.h -- what a mod gives a sound beside sfxdata.dat's columns (docs/mod-data.md,
// "Sounds and portraits"): the words of a voice line, shown on the game's message line as the
// unit says it when the player's Unit subtitles setting is on, and the least time between two
// plays of the sound. Both are heard and seen only: no game's state holds them.
#ifndef PORT_MODVOICE_H
#define PORT_MODVOICE_H

// The loader's answers (port_moddata.cpp), in a game with the mod: a sound's subtitle in the
// player's language, null for none; its least gap in milliseconds, -1 for the classic one.
const char *ModSoundSubtitle(unsigned nSound);
int ModSoundGap(unsigned nSound);

// A unit's voice line has begun (statport_talking_portrait): its subtitle shown for the local
// player's unit when the setting is on.
void ModVoiceSpoken(int nSound, int nUnitType, int nPlayer);

// Each game's start (ModDataForGame): the live channel's sounds, registered once. sounds {play?}
// plays a sound by its number or name as the interface does, then lists the eight slots playing,
// each with its sound and priority.
void ModVoiceForGame(void);

#endif
