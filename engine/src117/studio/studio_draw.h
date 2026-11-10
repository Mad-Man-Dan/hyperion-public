// studio_draw.h -- the small helpers the studio's panes draw with (studio_shell.cpp, studio_tables.cpp,
// studio_weapon.cpp, studio_newunit.cpp): lengths and colours of the design, text placed and
// measured, numbers and names put in words.
#ifndef STUDIO_DRAW_H
#define STUDIO_DRAW_H

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "imgui.h"
#include "../port/platform/port_json.h"
#include "studio_model.h"
#include "studio_ui.h"

namespace studio_draw {

inline float P(float f) {
  return StudioPx(f);
}

inline ImU32 C(int tok, float a = 1.0f) {
  return StudioCol(tok, a);
}

inline float TW(int f, float px, const std::string &s, float track = 0.0f, bool upper = false) {
  return StudioTextW(f, px, s.c_str(), track, upper);
}

inline float T(ImDrawList *d, int f, float px, float x, float y, ImU32 col, const std::string &s, float maxw = 0.0f) {
  return StudioText(d, f, px, ImVec2(x, y), col, s.c_str(), 0.0f, false, maxw);
}

// wide spaced capitals, as the design's headings and tabs
inline float Wide(ImDrawList *d, float px, float x, float y, ImU32 col, const std::string &s, float track = 0.1f,
                  bool bUpper = true) {
  return StudioText(d, STUDIO_FONT_WIDE, px, ImVec2(x, y), col, s.c_str(), track, bUpper);
}

// a text's top for it to sit in the middle of a band
inline float Mid(float y, float h, float px) {
  return floorf(y + (h - P(px)) / 2.0f);
}

// a number with its places only when it has a fraction
inline std::string Fmt(double d, int nPlaces = 1) {
  char sz[32];
  if (fabs(d - floor(d + 0.5)) < 1e-6)
    snprintf(sz, sizeof sz, "%.0f", d);
  else
    snprintf(sz, sizeof sz, "%.*f", nPlaces, d);
  return sz;
}

inline std::string Lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

inline bool Has(const std::string &hay, const char *pszNeedle) {
  return pszNeedle[0] == '\0' || Lower(hay).find(Lower(pszNeedle)) != std::string::npos;
}

inline std::string Q(const std::string &s) {
  return PortJsonQuote(s);
}

inline std::string Table(int t) {
  return StudioTableName(t);
}

// "Ultralisk" for zerg_ultralisk: the race's word left off
inline std::string Short(int t, const std::string &name) {
  std::string s = StudioShowName(t, name);
  for (const char *psz : {"Zerg ", "Terran ", "Protoss "})
    if (s.compare(0, strlen(psz), psz) == 0 && s.size() > strlen(psz))
      return s.substr(strlen(psz));
  return s;
}

// "a" or "an" before a word
inline std::string An(const std::string &word) {
  return (!word.empty() && strchr("aeiouAEIOU", word[0]) ? "an " : "a ") + word;
}

}  // namespace studio_draw

#endif
