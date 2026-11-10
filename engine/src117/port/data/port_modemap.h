// port_modemap.h -- the map read a mode is given (port_modemap.cpp): the starts, the resource
// clusters, the walking distance between two places and whether a tile can be walked on.
//
// A mode's generate is handed it as ctx.map, and a setting's read as its second argument, both
// from the map's own scenario.chk, so the set-up and the game read the same map the same way.
// The read is kept for one map at a time.
#ifndef PORT_MODEMAP_H
#define PORT_MODEMAP_H

#include <string>
#include <vector>

struct ModeMapStart {
  int x, y;    // map pixels
  int tx, ty;  // and tiles
  int nSlot;   // the slot the map gives it to
};

// A cluster of mineral fields and geysers: its middle, how many patches it holds and the
// pixels from the middle to the farthest of them. A cluster of fewer than three patches is a
// stray patch rather than a base, which the mode decides.
struct ModeMapBase {
  int x, y, tx, ty;
  int nFields;
  int nSpread;
};

struct ModeMapRead {
  bool bHave = false;
  std::string of;  // the map file it was read from
  int w = 0, h = 0;  // tiles
  std::vector<ModeMapStart> starts;
  std::vector<ModeMapBase> bases;
};

// The read of a map file (--map's path, a lobby's chosen map), or of the game's own map when
// pszMap is null or empty; null when the map cannot be read. The read is kept, so asking again
// for the same map costs nothing.
const ModeMapRead *ModeMapOf(const char *pszMap);
// The walking distance in tiles between two map places on the read last, or -1 where there is
// no ground path. Walks from one place are worked out once and kept, so a mode that measures
// every base from one home pays for one search.
int ModeMapWalk(int x1, int y1, int x2, int y2);
// Whether that tile of the read last can be walked on.
bool ModeMapWalkable(int tx, int ty);
// The read put away, with the walks kept for it (as a game ends).
void ModeMapForget();

#endif
