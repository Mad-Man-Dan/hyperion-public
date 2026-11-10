// port_modtileset.cpp -- a mod's changes to a classic tileset (port_modtileset.h,
// docs/mod-data.md "Tilesets"). They are read with the mod's types.json at start-up, each
// submission over the ones it depends on, and made to the game's own buffers as a game that has
// the mod reads its map's tileset (gamemap_init). The palette's entries and the groups' and
// minitiles' flags change in place; a picture becomes minitiles appended after the tileset's own,
// its megatile's references pointed at them, so the minitiles other megatiles share stay as
// they were.
#include "port_modtileset.h"

#include <string.h>
#include <algorithm>
#include <unordered_map>

#include "../../h/platform.h"
#include "../../h/Storm.h"         // SFileLoadFile, SMemAlloc
#include "../platform/port_json.h"
#include "../platform/port_util.h"  // PortReadFile
#include "../platform/port_log.h"  // PortLogger
#include "port_imagefile.h"         // PortImageDecode
#include "port_moddata.h"           // g_hxModGame
#include "port_stormapi.h"          // SFileUnloadFile
#include "port_stormbase.h"         // PortMemSize

// The tilesets' colour cycles, as maphdr.cpp's init_cycles reads them: a word of the cycles that
// start, then 8-byte records (rate, steps, first, last, flags) until one with no steps.
extern BYTE *sgCycleTbl[8] OC_UNALIGNED;  // 0x5089B0

const char *const kModTilesetNames[MOD_TILESETS] = {"badlands", "platform", "install", "ashworld",
                                                    "jungle",   "desert",   "ice",     "twilight"};

namespace {

ModTilesetChanges g_changes[MOD_TILESETS];
bool g_abOwnLook[MOD_TILESETS];
const PortLogger Log("hyperion_mods.log");  // port_moddata.cpp's too

bool GameFile(const std::string &name, std::vector<unsigned char> *pOut) {
  void *p = nullptr;
  DWORD cb = 0;
  pOut->clear();
  if (!SFileLoadFile(name.c_str(), &p, &cb, 0, nullptr) || p == nullptr)
    return false;
  pOut->assign(static_cast<unsigned char *>(p), static_cast<unsigned char *>(p) + cb);
  SFileUnloadFile(p);
  return true;
}

std::string Path(int nEra, const char *pszEnd) {
  return std::string("tileset\\") + kModTilesetNames[nEra] + pszEnd;
}

bool Fail(std::string *pWhy, const std::string &what) {
  *pWhy = what;
  return false;
}

// "#rrggbb", or [r, g, b]
bool Colour(const PortJson &v, unsigned *pRgb) {
  if (v.kind == PortJson::STRING && v.s.size() == 7 && v.s[0] == '#') {
    unsigned n = 0;
    for (size_t i = 1; i < 7; ++i) {
      const char c = v.s[i];
      const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0)
        return false;
      n = n * 16 + static_cast<unsigned>(d);
    }
    *pRgb = n;
    return true;
  }
  if (v.kind == PortJson::ARRAY && v.items.size() == 3) {
    unsigned n = 0;
    for (const PortJson &c : v.items) {
      if (c.kind != PortJson::NUMBER || c.d < 0 || c.d > 255)
        return false;
      n = n * 256 + static_cast<unsigned>(c.d);
    }
    *pRgb = n;
    return true;
  }
  return false;
}

bool Number(const std::string &s, int *pn) {
  if (s.empty() || s.size() > 6)
    return false;
  int n = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      return false;
    n = n * 10 + (c - '0');
  }
  *pn = n;
  return true;
}

bool Bool(const PortJson &v, int *pn) {
  if (v.kind != PortJson::BOOL)
    return false;
  *pn = v.b ? 1 : 0;
  return true;
}

// a flag of the 16 minitiles: true or false for all, or four rows of four, '#' set and '.' clear
bool Mask(const PortJson &v, int *pn) {
  if (v.kind == PortJson::BOOL) {
    *pn = v.b ? 0xFFFF : 0;
    return true;
  }
  if (v.kind != PortJson::ARRAY || v.items.size() != 4)
    return false;
  int n = 0;
  for (size_t r = 0; r < 4; ++r) {
    const PortJson &row = v.items[r];
    if (row.kind != PortJson::STRING || row.s.size() != 4)
      return false;
    for (size_t c = 0; c < 4; ++c) {
      const char ch = row.s[c];
      if (ch == '#' || ch == 'x' || ch == 'X' || ch == '1')
        n |= 1 << (r * 4 + c);
      else if (ch != '.' && ch != '0' && ch != '-')
        return false;
    }
  }
  *pn = n;
  return true;
}

int HeightWord(const std::string &s) {
  return s == "low" ? 0 : s == "mid" ? 1 : s == "high" ? 2 : -1;
}

// "low", "mid" or "high" for all, or four rows of four of l, m and h
bool Heights(const PortJson &v, unsigned char *pa) {
  if (v.kind == PortJson::STRING) {
    const int n = HeightWord(v.s);
    if (n < 0)
      return false;
    memset(pa, n, 16);
    return true;
  }
  if (v.kind != PortJson::ARRAY || v.items.size() != 4)
    return false;
  for (size_t r = 0; r < 4; ++r) {
    const PortJson &row = v.items[r];
    if (row.kind != PortJson::STRING || row.s.size() != 4)
      return false;
    for (size_t c = 0; c < 4; ++c) {
      const char *psz = strchr("lmh", row.s[c] | 0x20);
      if (psz == nullptr)
        return false;
      pa[r * 4 + c] = static_cast<unsigned char>(psz - "lmh");
    }
  }
  return true;
}

// A picture read and brought to 32 pixels a megatile across: nAcross megatiles in a row, each
// square; a side larger than 32 (a picture at the HD scale) averaged down.
bool Picture(const std::string &folder, const std::string &rel, int nAcross, std::vector<unsigned char> *pOut,
             std::string *pWhy) {
  std::vector<unsigned char> file;
  if (rel.empty() || rel.find("..") != std::string::npos || rel.find(':') != std::string::npos)
    return Fail(pWhy, "the picture " + rel + " is no file of the data folder");
  if (!PortReadFile(folder + rel, &file))
    return Fail(pWhy, "the picture " + rel + " is not in the data folder");
  std::vector<unsigned char> rgba;
  int w = 0, h = 0;
  std::string error;
  if (!PortImageDecode(file.data(), file.size(), rel, &rgba, &w, &h, &error))
    return Fail(pWhy, "the picture " + rel + " does not decode: " + error);
  if (h < 32 || h % 32 != 0 || w != h * nAcross)
    return Fail(pWhy, "the picture " + rel + " is " + std::to_string(w) + " by " + std::to_string(h) +
                          (nAcross == 1 ? " pixels; a megatile's is 32 by 32, or a multiple of it"
                                        : " pixels; a group's is its 16 megatiles in a row, 512 by 32, or a multiple of it"));
  const int k = h / 32, nW = 32 * nAcross;
  pOut->assign(static_cast<size_t>(nW) * 32 * 4, 0);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < nW; ++x) {
      unsigned s[4] = {0, 0, 0, 0};
      for (int yy = 0; yy < k; ++yy)
        for (int xx = 0; xx < k; ++xx) {
          const unsigned char *p = &rgba[(static_cast<size_t>(y * k + yy) * w + (x * k + xx)) * 4];
          for (int c = 0; c < 4; ++c)
            s[c] += p[c];
        }
      unsigned char *d = &(*pOut)[(static_cast<size_t>(y) * nW + x) * 4];
      for (int c = 0; c < 4; ++c)
        d[c] = static_cast<unsigned char>(s[c] / (k * k));
    }
  return true;
}

bool Under(const char *pszWhat, int n, size_t nCount, const std::string &where, std::string *pWhy) {
  if (nCount != 0 && static_cast<size_t>(n) >= nCount)
    return Fail(pWhy, where + ": " + pszWhat + " " + std::to_string(n) + " is past the tileset's " +
                          std::to_string(nCount));
  return true;
}

// the minitile's flags set from a megatile's or group's changes
void CellFlags(unsigned short *pCells, int nWalk, int nView, int nHeightAll, const unsigned char *pHeights) {
  for (int k = 0; k < 16; ++k) {
    unsigned short &w = pCells[k];
    if (nWalk >= 0)
      w = static_cast<unsigned short>((nWalk >> k) & 1 ? w | MOD_CELL_WALKABLE : w & ~MOD_CELL_WALKABLE);
    if (nView >= 0)
      w = static_cast<unsigned short>((nView >> k) & 1 ? w | MOD_CELL_BLOCKS_VIEW : w & ~MOD_CELL_BLOCKS_VIEW);
    const int h = pHeights ? pHeights[k] : nHeightAll;
    if (h >= 0) {
      w = static_cast<unsigned short>(w & ~(MOD_CELL_MID | MOD_CELL_HIGH));
      w = static_cast<unsigned short>(w | (h == 1 ? MOD_CELL_MID : h == 2 ? MOD_CELL_HIGH : 0));
    }
  }
}

}  // namespace

int ModTilesetEra(const std::string &name) {
  for (int i = 0; i < MOD_TILESETS; ++i)
    if (name == kModTilesetNames[i])
      return i;
  return -1;
}

bool ModTilesetChanges::OwnLook() const {
  if (!palette.empty())
    return true;
  for (const auto &g : groups)
    if (!g.second.image.empty())
      return true;
  for (const auto &m : megatiles)
    if (!m.second.image.empty())
      return true;
  return false;
}

bool ModTilesetReadClassic(int nEra, ModTileset *pOut, std::string *pWhy) {
  if (nEra < 0 || nEra >= MOD_TILESETS)
    return Fail(pWhy, "no such tileset");
  std::vector<unsigned char> wpe, cv5, vf4, vx4, vr4;
  bool bEx = GameFile(Path(nEra, ".vx4ex"), &vx4);
  if (!GameFile(Path(nEra, ".wpe"), &wpe) || wpe.size() < 1024 || !GameFile(Path(nEra, ".cv5"), &cv5) ||
      !GameFile(Path(nEra, ".vf4"), &vf4) || (!bEx && !GameFile(Path(nEra, ".vx4"), &vx4)) ||
      !GameFile(Path(nEra, ".vr4"), &vr4))
    return Fail(pWhy, std::string("the game's files have no whole ") + kModTilesetNames[nEra] + " tileset");
  pOut->palette.assign(wpe.begin(), wpe.begin() + 1024);
  pOut->cv5.resize(cv5.size() / (MOD_GROUP_WORDS * 2) * MOD_GROUP_WORDS);
  memcpy(pOut->cv5.data(), cv5.data(), pOut->cv5.size() * 2);
  pOut->vf4.resize(vf4.size() / 32 * 16);
  memcpy(pOut->vf4.data(), vf4.data(), pOut->vf4.size() * 2);
  const size_t nRefSize = bEx ? 4 : 2;
  pOut->vx4.resize(vx4.size() / (16 * nRefSize) * 16);
  for (size_t i = 0; i < pOut->vx4.size(); ++i) {
    unsigned v = 0;
    memcpy(&v, &vx4[i * nRefSize], nRefSize);
    pOut->vx4[i] = v;
  }
  pOut->vr4.assign(vr4.begin(), vr4.begin() + vr4.size() / 64 * 64);
  return true;
}

std::vector<ModTilesetCycle> ModTilesetCycles(int nEra) {
  std::vector<ModTilesetCycle> out;
  if (nEra < 0 || nEra >= MOD_TILESETS || sgCycleTbl[nEra] == nullptr)
    return out;
  WORD wStarted;
  memcpy(&wStarted, sgCycleTbl[nEra], 2);
  const BYTE *p = sgCycleTbl[nEra] + 4;
  for (int i = 0; i < 8 && p[4] != 0; ++i, p += 8) {
    DWORD dwTable;
    memcpy(&dwTable, p, 4);
    out.push_back({p[5], p[6], p[4], dwTable != 0, (wStarted >> i & 1) != 0});
  }
  return out;
}

bool ModTilesetCycled(int nEra, int nIndex) {
  for (const ModTilesetCycle &c : ModTilesetCycles(nEra))
    if (nIndex >= c.nLo && nIndex <= c.nHi)
      return true;
  return false;
}

int ModTilesetNearest(const unsigned char *pPalette, int nEra, unsigned rgb) {
  static int s_nEra = -1;
  static bool s_abCycled[256];
  if (s_nEra != nEra) {
    for (int i = 0; i < 256; ++i)
      s_abCycled[i] = ModTilesetCycled(nEra, i);
    s_nEra = nEra;
  }
  const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  int nBest = 0;
  long nBestD = 0x7FFFFFFF;
  for (int i = 0; i < 256; ++i) {
    if (s_abCycled[i])
      continue;
    const int dr = r - pPalette[i * 4], dg = g - pPalette[i * 4 + 1], db = b - pPalette[i * 4 + 2];
    const long d = 2L * dr * dr + 4L * dg * dg + 3L * db * db;
    if (d < nBestD) {
      nBestD = d;
      nBest = i;
    }
  }
  return nBest;
}

bool ModTilesetParse(const PortJson &j, const std::string &dataFolder, int nEra, size_t nGroups, size_t nMegatiles,
                     ModTilesetChanges *pOut, std::string *pWhy) {
  const std::string at = std::string("tilesets ") + kModTilesetNames[nEra];
  if (j.kind != PortJson::OBJECT)
    return Fail(pWhy, at + " is no object");
  for (const PortJsonMember &m : j.members) {
    const std::string where = at + " " + m.first;
    if (m.first == "comment")
      continue;
    if (m.first != "palette" && m.first != "groups" && m.first != "megatiles")
      return Fail(pWhy, at + " has no " + m.first + ": a tileset gives palette, groups and megatiles");
    if (m.second.kind != PortJson::OBJECT)
      return Fail(pWhy, where + " is no object");
    for (const PortJsonMember &e : m.second.members) {
      const std::string here = where + " " + e.first;
      int n = 0;
      if (!Number(e.first, &n))
        return Fail(pWhy, here + ": the members are numbers, the " +
                              (m.first == "palette" ? "palette's entries" : m.first == "groups" ? "groups" : "megatiles"));
      if (m.first == "palette") {
        unsigned rgb = 0;
        if (n > 255)
          return Fail(pWhy, here + ": a palette has entries 0 to 255");
        if (!Colour(e.second, &rgb))
          return Fail(pWhy, here + " is no colour: \"#rrggbb\" or [r, g, b]");
        pOut->palette[n] = rgb;
        continue;
      }
      if (e.second.kind != PortJson::OBJECT)
        return Fail(pWhy, here + " is no object");
      if (m.first == "groups") {
        if (!Under("group", n, nGroups, here, pWhy))
          return false;
        ModTilesetChanges::Group g = pOut->groups[n];
        for (const PortJsonMember &f : e.second.members) {
          const std::string &k = f.first;
          bool bOk = true;
          if (k == "comment")
            continue;
          if (k == "buildable")
            bOk = Bool(f.second, &g.nBuildable);
          else if (k == "creep")
            bOk = Bool(f.second, &g.nCreep);
          else if (k == "walkable")
            bOk = Bool(f.second, &g.nWalkable);
          else if (k == "blocks_view")
            bOk = Bool(f.second, &g.nBlocksView);
          else if (k == "height")
            bOk = f.second.kind == PortJson::STRING && (g.nHeight = HeightWord(f.second.s)) >= 0;
          else if (k == "image") {
            bOk = f.second.kind == PortJson::STRING;
            if (bOk) {
              g.image = f.second.s;
              if (!Picture(dataFolder, g.image, 16, &g.rgba, pWhy)) {
                *pWhy = here + ": " + *pWhy;
                return false;
              }
            }
          } else
            return Fail(pWhy, here + " has no " + k +
                                  ": a group gives buildable, creep, walkable, height, blocks_view and image");
          if (!bOk)
            return Fail(pWhy, here + " " + k +
                                  (k == "height" ? " is \"low\", \"mid\" or \"high\"" : k == "image" ? " is a file's path" : " is true or false"));
        }
        pOut->groups[n] = g;
        continue;
      }
      if (!Under("megatile", n, nMegatiles, here, pWhy))
        return false;
      ModTilesetChanges::Megatile t = pOut->megatiles[n];
      for (const PortJsonMember &f : e.second.members) {
        const std::string &k = f.first;
        bool bOk = true;
        if (k == "comment")
          continue;
        if (k == "walkable")
          bOk = Mask(f.second, &t.anMask[0]);
        else if (k == "blocks_view")
          bOk = Mask(f.second, &t.anMask[1]);
        else if (k == "height") {
          bOk = Heights(f.second, t.anHeight);
          t.nHeightSet = bOk ? 1 : 0;
        } else if (k == "image") {
          bOk = f.second.kind == PortJson::STRING;
          if (bOk) {
            t.image = f.second.s;
            if (!Picture(dataFolder, t.image, 1, &t.rgba, pWhy)) {
              *pWhy = here + ": " + *pWhy;
              return false;
            }
          }
        } else
          return Fail(pWhy, here + " has no " + k + ": a megatile gives walkable, height, blocks_view and image");
        if (!bOk)
          return Fail(pWhy, here + " " + k +
                                (k == "height" ? " is \"low\", \"mid\", \"high\" or four rows of four of l, m and h"
                                 : k == "image" ? " is a file's path"
                                                : " is true, false or four rows of four of # and ."));
      }
      pOut->megatiles[n] = t;
    }
  }
  return true;
}

bool ModTilesetApply(const ModTilesetChanges &c, int nEra, unsigned char *pPalette, unsigned short *pCv5, size_t nGroups,
                     unsigned short *pVf4, size_t nVf4, unsigned int *pVx4, size_t nMegatiles,
                     const unsigned char *pVr4, size_t nMinitiles, std::vector<unsigned char> *pNewCells) {
  bool bFlags = false;
  for (const auto &p : c.palette) {
    pPalette[p.first * 4] = static_cast<unsigned char>(p.second >> 16);
    pPalette[p.first * 4 + 1] = static_cast<unsigned char>(p.second >> 8);
    pPalette[p.first * 4 + 2] = static_cast<unsigned char>(p.second);
  }
  // the megatiles' pictures: a group's give its megatiles theirs, a megatile's own comes after
  std::map<int, const unsigned char *> pictures;  // a megatile's 32 x 32 RGBA, and its row's stride
  std::map<int, int> strides;
  for (const auto &g : c.groups) {
    if (static_cast<size_t>(g.first) >= nGroups)
      continue;
    unsigned short *pGroup = pCv5 + static_cast<size_t>(g.first) * MOD_GROUP_WORDS;
    const ModTilesetChanges::Group &x = g.second;
    if (x.nBuildable >= 0)
      pGroup[1] = static_cast<unsigned short>(x.nBuildable ? pGroup[1] & ~MOD_GROUP_UNBUILDABLE : pGroup[1] | MOD_GROUP_UNBUILDABLE);
    if (x.nCreep >= 0)
      pGroup[1] = static_cast<unsigned short>(x.nCreep ? pGroup[1] | MOD_GROUP_CREEP : pGroup[1] & ~MOD_GROUP_CREEP);
    for (int k = 0; k < 16; ++k) {
      const int nMega = pGroup[MOD_GROUP_FIRST_MEGATILE + k];
      if (nMega == 0)
        continue;  // the empty megatile every group's unused places name
      if ((x.nWalkable >= 0 || x.nBlocksView >= 0 || x.nHeight >= 0) && static_cast<size_t>(nMega) < nVf4) {
        CellFlags(pVf4 + static_cast<size_t>(nMega) * 16, x.nWalkable < 0 ? -1 : x.nWalkable ? 0xFFFF : 0,
                  x.nBlocksView < 0 ? -1 : x.nBlocksView ? 0xFFFF : 0, x.nHeight, nullptr);
        bFlags = true;
      }
      if (!x.rgba.empty() && pictures.find(nMega) == pictures.end()) {
        pictures[nMega] = &x.rgba[static_cast<size_t>(k) * 32 * 4];
        strides[nMega] = 512;
      }
    }
  }
  for (const auto &m : c.megatiles) {
    const ModTilesetChanges::Megatile &t = m.second;
    if (static_cast<size_t>(m.first) < nVf4 && (t.anMask[0] >= 0 || t.anMask[1] >= 0 || t.nHeightSet)) {
      CellFlags(pVf4 + static_cast<size_t>(m.first) * 16, t.anMask[0], t.anMask[1], -1, t.nHeightSet ? t.anHeight : nullptr);
      bFlags = true;
    }
    if (!t.rgba.empty()) {
      pictures[m.first] = t.rgba.data();
      strides[m.first] = 32;
    }
  }
  std::unordered_map<unsigned, unsigned char> nearest;
  std::map<std::string, unsigned> made;  // a new minitile's pixels to its number, so a picture's repeats share one
  for (const auto &p : pictures) {
    const size_t nMega = static_cast<size_t>(p.first);
    if (nMega >= nMegatiles)
      continue;
    unsigned int *pRefs = pVx4 + nMega * 16;
    const int nStride = strides[p.first];
    for (int k = 0; k < 16; ++k) {
      unsigned char cell[64];
      const unsigned nRef = pRefs[k], nOld = nRef >> 1;
      const bool bMirror = (nRef & 1) != 0;
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
          const int px = (k % 4) * 8 + x, py = (k / 4) * 8 + y;
          const unsigned char *s = p.second + (static_cast<size_t>(py) * nStride + px) * 4;
          unsigned char v;
          if (s[3] >= 128) {
            const unsigned rgb = static_cast<unsigned>(s[0]) << 16 | static_cast<unsigned>(s[1]) << 8 | s[2];
            auto it = nearest.find(rgb);
            if (it == nearest.end())
              it = nearest.emplace(rgb, static_cast<unsigned char>(ModTilesetNearest(pPalette, nEra, rgb))).first;
            v = it->second;
          } else {
            v = nOld < nMinitiles ? pVr4[nOld * 64 + y * 8 + (bMirror ? 7 - x : x)] : 0;
          }
          cell[y * 8 + x] = v;
        }
      const std::string key(reinterpret_cast<const char *>(cell), 64);
      auto it = made.find(key);
      if (it == made.end()) {
        it = made.emplace(key, static_cast<unsigned>(nMinitiles + pNewCells->size() / 64)).first;
        pNewCells->insert(pNewCells->end(), cell, cell + 64);
      }
      pRefs[k] = it->second << 1;
    }
  }
  return bFlags;
}

bool ModTilesetApplyTo(const ModTilesetChanges &c, int nEra, ModTileset *p) {
  std::vector<unsigned char> cells;
  const bool bFlags = ModTilesetApply(c, nEra, p->palette.data(), p->cv5.data(), p->Groups(), p->vf4.data(),
                                      p->vf4.size() / 16, p->vx4.data(), p->Megatiles(), p->vr4.data(), p->Minitiles(),
                                      &cells);
  p->vr4.insert(p->vr4.end(), cells.begin(), cells.end());
  return bFlags;
}

bool ModTilesetLoad(const PortJson &types, const std::string &dataFolder, std::string *pWhy) {
  const PortJson &ts = types["tilesets"];
  if (ts.kind == PortJson::NONE)
    return true;
  if (ts.kind != PortJson::OBJECT)
    return Fail(pWhy, "types.json's tilesets is no object");
  for (const PortJsonMember &m : ts.members) {
    const int nEra = ModTilesetEra(m.first);
    if (nEra < 0)
      return Fail(pWhy, "types.json's tilesets has no " + m.first +
                            ": the tilesets are badlands, platform, install, ashworld, jungle, desert, ice and twilight");
    std::vector<unsigned char> cv5, vf4;
    GameFile(Path(nEra, ".cv5"), &cv5);
    GameFile(Path(nEra, ".vf4"), &vf4);
    ModTilesetChanges c = g_changes[nEra];
    if (!ModTilesetParse(m.second, dataFolder, nEra, cv5.size() / 52, vf4.size() / 32, &c, pWhy))
      return false;
    g_changes[nEra] = c;
  }
  for (const std::string &s : ModTilesetSummary())
    Log("mods: %s", s);
  return true;
}

std::vector<std::string> ModTilesetSummary() {
  std::vector<std::string> out;
  for (int nEra = 0; nEra < MOD_TILESETS; ++nEra) {
    const ModTilesetChanges &c = g_changes[nEra];
    if (!c.Any())
      continue;
    size_t nPictures = 0, nGroupFlags = 0, nMegaFlags = 0;
    for (const auto &g : c.groups) {
      nPictures += g.second.image.empty() ? 0 : 1;
      nGroupFlags += g.second.nBuildable >= 0 || g.second.nCreep >= 0 || g.second.nWalkable >= 0 ||
                             g.second.nHeight >= 0 || g.second.nBlocksView >= 0
                         ? 1
                         : 0;
    }
    for (const auto &t : c.megatiles) {
      nPictures += t.second.image.empty() ? 0 : 1;
      nMegaFlags += t.second.anMask[0] >= 0 || t.second.anMask[1] >= 0 || t.second.nHeightSet ? 1 : 0;
    }
    const auto count = [](size_t n, const char *pszOne, const char *pszMany) {
      return std::to_string(n) + " " + (n == 1 ? pszOne : pszMany);
    };
    out.push_back(std::string("tileset ") + kModTilesetNames[nEra] + ": " + count(c.palette.size(), "colour", "colours") +
                  ", flags of " + count(nGroupFlags, "group", "groups") + " and " + count(nMegaFlags, "megatile", "megatiles") +
                  ", " + count(nPictures, "picture", "pictures"));
  }
  return out;
}

void ModTilesetClear() {
  for (ModTilesetChanges &c : g_changes)
    c = ModTilesetChanges();
}

bool ModTilesetForGame(int nEra, unsigned char *pPalette, unsigned short *pCv5, unsigned nGroups, unsigned short *pVf4,
                       unsigned nVf4, unsigned char **ppVx4, unsigned char **ppVr4) {
  for (bool &b : g_abOwnLook)
    b = false;
  if (!g_hxModGame || nEra < 0 || nEra >= MOD_TILESETS || !g_changes[nEra].Any() || pPalette == nullptr ||
      pCv5 == nullptr || pVf4 == nullptr || ppVx4 == nullptr || *ppVx4 == nullptr || ppVr4 == nullptr || *ppVr4 == nullptr)
    return false;
  const ModTilesetChanges &c = g_changes[nEra];
  const size_t nMegatiles = PortMemSize(*ppVx4) / 64, nCells = PortMemSize(*ppVr4) / 64;
  std::vector<unsigned char> cells;
  const bool bFlags = ModTilesetApply(c, nEra, pPalette, pCv5, nGroups, pVf4, nVf4, reinterpret_cast<unsigned int *>(*ppVx4),
                                      nMegatiles, *ppVr4, nCells, &cells);
  if (!cells.empty()) {
    const size_t nOld = nCells * 64;
    unsigned char *pNew =
        static_cast<unsigned char *>(SMemAlloc(static_cast<DWORD>(nOld + cells.size()), "port_modtileset.cpp", __LINE__, 0));
    memcpy(pNew, *ppVr4, nOld);
    memcpy(pNew + nOld, cells.data(), cells.size());
    SMemFree(*ppVr4, "port_modtileset.cpp", __LINE__, 0);
    *ppVr4 = pNew;
  }
  g_abOwnLook[nEra] = c.OwnLook();
  return bFlags;
}

bool ModTilesetForPicture(int nEra, unsigned char *pPalette, unsigned short *pCv5, size_t nGroups, unsigned int *pVx4,
                          size_t nMegatiles, std::vector<unsigned char> *pVr4) {
  // a new game has the mod's data whenever a mod's data loaded (RulesForGame)
  if (ModDataId()[0] == '\0' || nEra < 0 || nEra >= MOD_TILESETS || !g_changes[nEra].Any() || pPalette == nullptr ||
      pCv5 == nullptr || pVx4 == nullptr || pVr4 == nullptr)
    return false;
  std::vector<unsigned char> cells;
  ModTilesetApply(g_changes[nEra], nEra, pPalette, pCv5, nGroups, nullptr, 0, pVx4, nMegatiles, pVr4->data(),
                  pVr4->size() / 64, &cells);
  pVr4->insert(pVr4->end(), cells.begin(), cells.end());
  return true;
}

bool ModTilesetOwnLook(int nEra) {
  return nEra >= 0 && nEra < MOD_TILESETS && g_abOwnLook[nEra];
}
