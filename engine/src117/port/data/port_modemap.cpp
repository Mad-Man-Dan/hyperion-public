// port_modemap.cpp -- the map read a mode is given (port_modemap.h).
//
// The map's scenario.chk is read as the lobby reads it for its picture (MapPictureRead), so the
// set-up and the game agree on every number: the starts, the resources joined into
// clusters by single linkage, and the minitiles walkable or not. The walk is Dial's queue over
// the minitiles, eight ways, 10 straight and 14 across and never across a corner, from one
// place to every other; the field of a place is kept, since a mode measures every base from the
// same home.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <deque>

#include "port_modemap.h"
#include "../front/port_frontlobby.h"  // MapPicture, MapPictureRead, MapWalkable
#include "../platform/port_log.h"
#include "../../h/maphdr.h"    // get_map_chk
#include "../../h/glues.h"     // mem_free
#include "../../h/map_load.h"  // GetCurrentMap, and a replay's map image
#include "../../gen/globals117.h"

// 0x66FD4C ghScenArchive, spelled as src117/lang/maphdr.cpp spells it: the scenario archive,
// which a running game keeps open as its map's.
extern HANDLE ghScenArchive;

// 0x67CC78 sgpMap, 0x67CC74 sglBytes (map_load.cpp): a replay's map image and its size, which a
// game being played back loads its map from in place of a file.
extern void *sgpMap;
extern DWORD sglBytes;

namespace {

const PortLogger Log("hyperion_mods.log");

const int MINI = 8;      // a minitile in pixels, the walk's step
const int LINK = 7 * 32; // patches within this of one another are one cluster

struct Field {
  int mx = -1, my = -1;                 // the minitile the walk started from
  std::vector<unsigned short> d;        // tenths of a minitile, 0xFFFF for out of reach
};

ModeMapRead g_read;
int g_mw, g_mh;                       // the read's size in minitiles
std::vector<unsigned char> g_walk;    // a minitile walkable
std::deque<Field> g_fields;           // the walks worked out, the newest first
const size_t kFieldsKept = 4;

long long Sq(long long n) {
  return n * n;
}

// The map's CHK: the file named, else the game's own map. A replay carries its map's chk in
// memory and names no file, so that image is the read. Opening the scenario archive and closing
// it again is get_map_chk's own work; a running game's handle stays its own.
bool ReadChk(const char *pszMap, std::vector<BYTE> *pOut, std::string *pOf) {
  const bool bOwn = pszMap == nullptr || pszMap[0] == '\0';
  if (bOwn && gPlayback != 0 && sgpMap != nullptr && sglBytes > 0) {
    *pOf = "the replay's map";
    const BYTE *pb = static_cast<const BYTE *>(sgpMap);
    pOut->assign(pb, pb + sglBytes);
    return true;
  }
  const char *pszPath = !bOwn ? pszMap : static_cast<const char *>(GetCurrentMap(1));
  if (pszPath == nullptr || pszPath[0] == '\0')
    return false;
  *pOf = pszPath;
  const HANDLE hGame = ghScenArchive;
  ghScenArchive = NULL;
  DWORD dwSize = 0;
  BYTE *p = static_cast<BYTE *>(get_map_chk(pszPath, &dwSize));
  ghScenArchive = hGame;
  if (p == nullptr)
    return false;
  pOut->assign(p, p + dwSize);
  mem_free(p, __FILE__, __LINE__);
  return true;
}

// The resources joined into clusters: a patch belongs to a cluster when it is within LINK of
// any patch already in it. The clusters come out in the order of their middles, so the read is
// the same every time.
void Cluster(const FrontKit::MapPicture &pic) {
  const size_t n = pic.resources.size();
  std::vector<int> of(n, -1);
  int nGroups = 0;
  for (size_t i = 0; i < n; ++i) {
    if (of[i] >= 0)
      continue;
    of[i] = nGroups++;
    // everything reachable from it by links, breadth first
    std::vector<size_t> open(1, i);
    while (!open.empty()) {
      const size_t at = open.back();
      open.pop_back();
      for (size_t k = 0; k < n; ++k) {
        if (of[k] >= 0)
          continue;
        const long long ll = Sq(static_cast<int>(pic.resources[at].x) - pic.resources[k].x) +
                             Sq(static_cast<int>(pic.resources[at].y) - pic.resources[k].y);
        if (ll <= Sq(LINK)) {
          of[k] = of[i];
          open.push_back(k);
        }
      }
    }
  }
  for (int g = 0; g < nGroups; ++g) {
    long long sx = 0, sy = 0, nIn = 0;
    for (size_t i = 0; i < n; ++i)
      if (of[i] == g) {
        sx += pic.resources[i].x;
        sy += pic.resources[i].y;
        ++nIn;
      }
    if (nIn == 0)
      continue;
    ModeMapBase b;
    b.x = static_cast<int>(sx / nIn);
    b.y = static_cast<int>(sy / nIn);
    b.tx = b.x / 32;
    b.ty = b.y / 32;
    b.nFields = static_cast<int>(nIn);
    long long llFar = 0;
    for (size_t i = 0; i < n; ++i)
      if (of[i] == g)
        llFar = std::max(llFar, Sq(pic.resources[i].x - b.x) + Sq(pic.resources[i].y - b.y));
    b.nSpread = static_cast<int>(sqrt(static_cast<double>(llFar)));
    g_read.bases.push_back(b);
  }
  std::sort(g_read.bases.begin(), g_read.bases.end(), [](const ModeMapBase &a, const ModeMapBase &b) {
    return a.y != b.y ? a.y < b.y : a.x < b.x;
  });
}

// The walkable minitile nearest one, ring by ring, or the minitile itself when none is near.
void Nearest(int *pmx, int *pmy) {
  if (*pmx >= 0 && *pmx < g_mw && *pmy >= 0 && *pmy < g_mh &&
      g_walk[static_cast<size_t>(*pmy) * g_mw + *pmx])
    return;
  for (int r = 1; r <= 24; ++r)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(abs(dx), abs(dy)) != r)
          continue;
        const int x = *pmx + dx, y = *pmy + dy;
        if (x >= 0 && x < g_mw && y >= 0 && y < g_mh && g_walk[static_cast<size_t>(y) * g_mw + x]) {
          *pmx = x;
          *pmy = y;
          return;
        }
      }
}

// Dial's queue: the walking distance from (x, y) to every minitile, tenths of a minitile.
const Field *FieldFrom(int x, int y) {
  int mx = x / MINI, my = y / MINI;
  Nearest(&mx, &my);
  for (const Field &f : g_fields)
    if (f.mx == mx && f.my == my)
      return &f;
  Field f;
  f.mx = mx;
  f.my = my;
  const size_t nAll = static_cast<size_t>(g_mw) * g_mh;
  f.d.assign(nAll, 0xFFFF);
  const size_t s = static_cast<size_t>(my) * g_mw + mx;
  if (s < nAll && g_walk[s]) {
    f.d[s] = 0;
    std::vector<std::vector<unsigned int>> buckets(15);
    buckets[0].push_back(static_cast<unsigned int>(s));
    for (unsigned at = 0; at < 0xFFFF; ++at) {
      std::vector<unsigned int> &now = buckets[at % 15];
      while (!now.empty()) {
        const unsigned int i = now.back();
        now.pop_back();
        if (f.d[i] != at)
          continue;
        const int tx = static_cast<int>(i % g_mw), ty = static_cast<int>(i / g_mw);
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
              continue;
            const int nx = tx + dx, ny = ty + dy;
            if (nx < 0 || nx >= g_mw || ny < 0 || ny >= g_mh)
              continue;
            const size_t k = static_cast<size_t>(ny) * g_mw + nx;
            if (!g_walk[k])
              continue;
            // never across a corner that is not walkable, as 1.17's regions take it
            if (dx != 0 && dy != 0 &&
                (!g_walk[static_cast<size_t>(ty) * g_mw + nx] || !g_walk[static_cast<size_t>(ny) * g_mw + tx]))
              continue;
            const unsigned nStep = (dx != 0 && dy != 0) ? 14u : 10u;
            if (at + nStep < f.d[k]) {
              f.d[k] = static_cast<unsigned short>(at + nStep);
              buckets[(at + nStep) % 15].push_back(static_cast<unsigned int>(k));
            }
          }
      }
    }
  }
  g_fields.push_front(f);
  while (g_fields.size() > kFieldsKept)
    g_fields.pop_back();
  return &g_fields.front();
}

}  // namespace

const ModeMapRead *ModeMapOf(const char *pszMap) {
  std::vector<BYTE> chk;
  std::string of;
  if (!ReadChk(pszMap, &chk, &of))
    return g_read.bHave && (pszMap == nullptr || pszMap[0] == '\0') ? &g_read : nullptr;
  if (g_read.bHave && g_read.of == of)
    return &g_read;
  ModeMapForget();
  FrontKit::MapPicture pic;
  pic.nSlot = -1;  // no picture: the atlas is the front end's
  FrontKit::MapPictureRead(&pic, of, chk.data(), static_cast<DWORD>(chk.size()));
  if (pic.w <= 0 || pic.h <= 0)
    return nullptr;
  g_read.of = of;
  g_read.w = pic.w;
  g_read.h = pic.h;
  for (const FrontKit::MapStart &s : pic.starts) {
    const ModeMapStart one = {s.x, s.y, s.x / 32, s.y / 32, s.nSlot};
    g_read.starts.push_back(one);
  }
  std::sort(g_read.starts.begin(), g_read.starts.end(), [](const ModeMapStart &a, const ModeMapStart &b) {
    return a.y != b.y ? a.y < b.y : a.x < b.x;
  });
  Cluster(pic);
  g_mw = pic.w * 4;
  g_mh = pic.h * 4;
  if (!FrontKit::MapWalkable(pic, &g_walk) || g_walk.size() != static_cast<size_t>(g_mw) * g_mh) {
    g_walk.assign(static_cast<size_t>(g_mw) * g_mh, 1);  // the tileset did not read: every tile walkable
    Log("modes: %s's tileset does not read, so every tile is taken as walkable", of.c_str());
  }
  g_read.bHave = true;
  Log("modes: the map %s read: %u starts, %u resource clusters", of.c_str(),
      static_cast<unsigned>(g_read.starts.size()), static_cast<unsigned>(g_read.bases.size()));
  return &g_read;
}

int ModeMapWalk(int x1, int y1, int x2, int y2) {
  if (!g_read.bHave || g_walk.empty())
    return -1;
  int mx = x2 / MINI, my = y2 / MINI;
  Nearest(&mx, &my);
  if (mx < 0 || mx >= g_mw || my < 0 || my >= g_mh)
    return -1;
  const Field *pField = FieldFrom(x1, y1);
  const unsigned short d = pField->d[static_cast<size_t>(my) * g_mw + mx];
  if (d == 0xFFFF)
    return -1;
  return static_cast<int>(d) / 40;  // tenths of a minitile into tiles, four minitiles a tile
}

bool ModeMapWalkable(int tx, int ty) {
  if (!g_read.bHave || g_walk.empty() || tx < 0 || ty < 0 || tx >= g_read.w || ty >= g_read.h)
    return false;
  // a tile is walkable when any of its minitiles is, as a unit stands on a part of one
  for (int k = 0; k < 16; ++k)
    if (g_walk[static_cast<size_t>(ty * 4 + k / 4) * g_mw + tx * 4 + k % 4])
      return true;
  return false;
}

void ModeMapForget() {
  g_read = ModeMapRead();
  g_walk.clear();
  g_fields.clear();
  g_mw = g_mh = 0;
}
