// studio_media.h -- what the studio's sound, looks, mod page and checks areas share
// (studio_media_*.cpp): a sound found and played, the game's palette, the folders of the mods a
// mod depends on, a file copied into the data folder under a name, and the small pieces their
// pages are laid out with.
#ifndef STUDIO_MEDIA_H
#define STUDIO_MEDIA_H

#include <string>
#include <vector>
#include "imgui.h"

// ---- sounds (studio_media_audio.cpp) ---------------------------------------------------------------
// Where a sound record's file is: in a mod's data folder (a path on disk), or the classic game's
// (a name in its archives, under sound\). Empty when the record has no file.
struct StudioSoundWhere {
  std::string shown;  // the path as the record gives it
  std::string disk;   // the file on disk, when a mod's
  std::string mpq;    // the archives' name, when classic (sound\Terran\MARINE\TMaYes00.WAV)
  bool bThere = false;
};
StudioSoundWhere StudioSoundFile(const std::string &sound);
// What a decoded sound is: its seconds, rate and channels as the file has them.
struct StudioSoundInfo {
  double dSeconds = 0.0;
  int nRate = 0;
  int nChannels = 0;
  std::string kind;  // "WAV", "Ogg", "MP3", "FLAC" or "Opus", by its first bytes
};
// A sound record played once on the mixer; a file of the data folder played; false with why.
bool StudioPlaySound(const std::string &sound, StudioSoundInfo *pInfo, std::string *pWhy);
bool StudioPlayDataFile(const std::string &rel, StudioSoundInfo *pInfo, std::string *pWhy);
// A sound's length and kind without playing it, decoded once and kept; false when it does not decode.
bool StudioSoundInfoOf(const std::string &sound, StudioSoundInfo *pInfo);
// The sound and the time it was last played, for a play button that shows it is playing.
const std::string &StudioSoundPlaying(double *pdUntil);

// ---- the rest (studio_media_audio.cpp) -------------------------------------------------------------
// The game's palette as 0xRRGGBB, 256 entries (the Badlands tileset's, which the units' colours
// share with every other); empty when the archives do not have it.
const std::vector<unsigned> &StudioPalette(void);
// The data folder of a mod by id, the open one's or one it depends on, ending in a backslash.
std::string StudioModDataFolder(const std::string &mod);
// A file copied into the data folder as rel (folders made as needed), replacing one there.
bool StudioCopyAs(const std::string &from, const std::string &rel, std::string *pWhy);
// A file's size in bytes, -1 when it is not there; for a path of the data folder.
long long StudioDataFileSize(const std::string &rel);
// The first of a path's videos the folder has, by the loader's order of ends (.webm, .mkv, .mp4,
// .m4v, .mov, .smk), as a path of the data folder; empty for none.
std::string StudioFirstVideo(const std::string &relWithoutEnd);
extern const char *const kStudioVideoEnds[6];

// ---- the pages' pieces (studio_media_audio.cpp) ----------------------------------------------------
// A section's heading at the cursor, with a line of what it is after it, and the cursor below.
void StudioMediaHeading(const char *pszTitle, const char *pszWhat, float fW);
// A wrapped paragraph at the cursor in a colour, the cursor below.
void StudioMediaPara(const std::string &text, float fW, unsigned col, float fPx = 13.0f);
// A play button at the cursor for a sound record: true when it was pressed (and the sound played).
bool StudioPlayButton(const char *pszId, const std::string &sound, float fH = 24.0f);

// ---- pictures (studio_media_video.cpp) -------------------------------------------------------------
// What a video or a console image drawn is: where it was read from, or why it was not.
struct StudioVideoState {
  std::string from;  // its file, in words
  std::string why;   // why it does not show, when it does not
  int w = 0, h = 0;
  unsigned nFrames = 0;
  unsigned nFrame = 0;  // the frame shown
  double dSeconds = 0.0;
  double dAt = 0.0;       // the time into it of the frame shown
  bool bSmacker = false;
  bool bReading = false;  // still opening, on a thread of its own
  bool bHasSound = false;  // the video has a sound track
  bool bSoundOn = false;   // it is playing, not muted
  double dFrame0At = -1.0, dSoundAt = -1.0;  // ImGui's clock when frame 0 was painted and its sound put in
};
// A video drawn in a..b, its shape kept, playing in a loop without sound while it is drawn: a classic
// name in the game's files ("portrait\tmarine\TMaFid0.smk", "smk\blizzard.smk", Remastered's
// .webm of the same name in its place when the archives have none), or a path in the mod's data
// folder. False when it does not open, with why in *pState.
// With bSound its sound plays with it, kept with the frames, while it is drawn: a Smacker file's
// first track a frame at a time, a video in full colour's from the track decoded as it opened.
bool StudioVideoDraw(const std::string &where, bool bClassic, ImVec2 a, ImVec2 b, StudioVideoState *pState = nullptr,
                     bool bSound = false);
// The previews' sound muted, or not, for every preview.
void StudioVideoMute(bool bMuted);
bool StudioVideoMuted(void);
// A PCX of 8 bits a pixel drawn in a..b in the game's palette, its pixels at 0 clear: a console
// image, the classic one by its name in the game's files ("game\tconsole.pcx") or the mod's.
// The subtitles of a cinematic at a time into it: the mod's smk/<name>.srt beside its own video
// (bOwn), else Remastered's in English when its install is there; "" between lines or with none, a
// newline between lines. *pFrom names the file, "" with none.
std::string StudioSubtitleAt(const std::string &cinematic, bool bOwn, double dSeconds, std::string *pFrom = nullptr);
bool StudioPcxDraw(const std::string &where, bool bClassic, ImVec2 a, ImVec2 b, StudioVideoState *pState = nullptr);

#endif
