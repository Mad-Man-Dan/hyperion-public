// port_modeview.h -- what a game mode shows on screen, under its scripts' control
// (docs/mod-data.md, "What a mode shows"): a countdown or timer, a banner with lines, and
// counters such as the wave out of the run's. A mode's scripts set each part and clear it
// (hx.timer, hx.banner, hx.counter in port_modlua.cpp), and this draws them over the game as a
// layer of the overlay (port_modeview.cpp).
//
// Presentation only. Nothing here is read by the simulation, saved or sent, so a game with a
// mode plays and records the same with these parts or without them; a replay and a watched
// seat draw them because the mode's own scripts run there and set them again.
#ifndef PORT_MODEVIEW_H
#define PORT_MODEVIEW_H

#include <string>
#include <vector>

// A part's kind.
enum { MODE_VIEW_TIMER, MODE_VIEW_BANNER, MODE_VIEW_COUNTER };

// Where a part sits in the game's picture. There is no bottom: the console and a HUD design
// own the bottom of the screen.
enum {
  MODE_VIEW_TOP, MODE_VIEW_TOP_LEFT, MODE_VIEW_TOP_RIGHT, MODE_VIEW_LEFT, MODE_VIEW_RIGHT,
  MODE_VIEW_MIDDLE, MODE_VIEW_ANCHORS
};

// How it is drawn: a timer as a clock, a bar, or both; a counter plain or with a bar.
enum { MODE_VIEW_PLAIN, MODE_VIEW_CLOCK, MODE_VIEW_BAR, MODE_VIEW_BOTH };

// Its colour: the plain ink, or one that says the part is good, pressing or bad news.
enum { MODE_VIEW_TONE_PLAIN, MODE_VIEW_TONE_GOOD, MODE_VIEW_TONE_WARN, MODE_VIEW_TONE_BAD };

// One part a mode shows. The words are the mode's own, already in the player's language.
struct PortModeViewPart {
  std::string id;                    // the scripts' name for it, which setting again replaces
  int nKind = MODE_VIEW_TIMER;
  int nAnchor = MODE_VIEW_TOP;
  int nStyle = MODE_VIEW_CLOCK;
  int nTone = MODE_VIEW_TONE_PLAIN;
  std::string label;                 // the words over the clock, before the count, or a banner's title
  std::vector<std::string> lines;    // a banner's lines under its title
  int nFrames = 0;                   // a timer: the frames it has left, counting down; -1 counts up
  int nFull = 0;                     // a timer's bar: the frames it was given, for how full it is
  double dValue = 0, dOf = 0;        // a counter: the count, and what it is out of (0 for none)
  int nShows = 0;                    // the frames it is drawn for; 0 until the scripts clear it
};

// A part set, which replaces the one of its id and kind, or cleared. A mode shows 16 at most;
// the 17th is passed over.
void PortModeViewSet(const PortModeViewPart &part);
// A counter's count alone, the rest of it left as it stands; false when there is no counter of
// that name yet.
bool PortModeViewCounterSet(const char *pszId, double dValue);
void PortModeViewClear(const char *pszId, int nKind);
void PortModeViewClearAll(void);  // a game starting or loaded (ModLuaForGame)

// An anchor, a style or a tone by the name a script writes; -1 for a name there is none of.
int PortModeViewAnchorOf(const char *pszName);
int PortModeViewStyleOf(const char *pszName);
int PortModeViewToneOf(const char *pszName);

// What is drawn now, a line a part, for a test and for the scripts' log: "timer next: Next
// attack 0:45". The count is as the parts stand this frame.
int PortModeViewCount(void);
std::string PortModeViewLine(int i);

#endif
