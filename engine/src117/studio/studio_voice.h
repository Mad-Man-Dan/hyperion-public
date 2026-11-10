// studio_voice.h -- how each sound of a unit's line plays, in the unit's Sounds view
// (studio_voice.cpp): its subtitle, priority, least gap and flags, each changed in place.
#ifndef STUDIO_VOICE_H
#define STUDIO_VOICE_H

#include <string>
#include <vector>

// Each sound's rows from x0 to right at y: its subtitle, then how it plays; editable when bMine.
// The y below them.
float StudioSoundRows(const std::vector<std::string> &sounds, bool bMine, float x0, float y, float right);
// studio_sound_set.
void StudioVoiceInit(void);

#endif
