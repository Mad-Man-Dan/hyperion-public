// port_modtileset.h -- a mod's changes to a classic tileset (docs/mod-data.md, "Tilesets"):
// colours of its palette replaced, megatiles' pictures replaced from PNG files, mapped into the
// palette, and tile flags changed: a group's buildable and creep, its megatiles' minitiles'
// walkable, height and blocks view. types.json's "tilesets" holds them, by the tileset's name.
// A game that has the mod draws and plays the tileset so changed (gamemap_init); any other game
// has the classic files. The studio's Tilesets workspace previews them through the same code.
#ifndef PORT_MODTILESET_H
#define PORT_MODTILESET_H

#include <map>
#include <string>
#include <vector>

struct PortJson;

enum { MOD_TILESETS = 8, MOD_GROUP_WORDS = 26, MOD_GROUP_FIRST_MEGATILE = 10 };
// The classic tilesets by the names their files have, as a map's era numbers them:
// badlands, platform, install, ashworld, jungle, desert, ice, twilight.
extern const char *const kModTilesetNames[MOD_TILESETS];
int ModTilesetEra(const std::string &name);  // -1 for none

// The bits of a minitile's flags (.vf4) and of a group's (.cv5's second word) a mod may set.
enum {
  MOD_CELL_WALKABLE = 0x01, MOD_CELL_MID = 0x02, MOD_CELL_HIGH = 0x04, MOD_CELL_BLOCKS_VIEW = 0x08,
  MOD_GROUP_CREEP = 0x0040, MOD_GROUP_UNBUILDABLE = 0x0080
};

// A tileset's files decoded: the palette (.wpe, four bytes a colour), the groups (.cv5, 26 words
// each: its kind, its flags, its edges, then 16 megatiles), each megatile's 16 minitiles' flags
// (.vf4), its 16 minitile references (.vx4, or .vx4ex: the minitile times two, plus one when
// mirrored) and the minitiles' 8 by 8 pixels (.vr4).
struct ModTileset {
  std::vector<unsigned char> palette;
  std::vector<unsigned short> cv5;
  std::vector<unsigned short> vf4;
  std::vector<unsigned int> vx4;
  std::vector<unsigned char> vr4;
  size_t Groups() const { return cv5.size() / MOD_GROUP_WORDS; }
  size_t Megatiles() const { return vx4.size() / 16; }
  size_t Minitiles() const { return vr4.size() / 64; }
};
// A classic tileset read through the game's files (the archives). False with why.
bool ModTilesetReadClassic(int nEra, ModTileset *pOut, std::string *pWhy);

// The colour ranges of a tileset's palette the game may cycle (its water, lava, lights): each a
// first and last entry, the frames between its steps, and whether the game starts it with the
// map. A plain cycle moves the range's colours up one entry a step, the top one to the bottom;
// one of a table (bTable) takes its colours from a table of the game's in turn.
struct ModTilesetCycle {
  int nLo, nHi, nRate;
  bool bTable, bStarted;
};
std::vector<ModTilesetCycle> ModTilesetCycles(int nEra);
// An entry of the palette the game cycles; a picture is never mapped onto one.
bool ModTilesetCycled(int nEra, int nIndex);

// What a mod changes in one tileset, as types.json gives it, with its pictures decoded.
struct ModTilesetChanges {
  struct Group {
    int nBuildable = -1, nCreep = -1;            // 0 or 1, -1 kept
    int nWalkable = -1, nHeight = -1, nBlocksView = -1;  // for every minitile of its megatiles; height 0 low, 1 mid, 2 high
    std::string image;                           // the picture of its 16 megatiles, a row of 16
    std::vector<unsigned char> rgba;             // decoded, 512 x 32
  };
  struct Megatile {
    int anMask[2] = {-1, -1};      // walkable, blocks view: 16 bits, minitile 0 the top left; -1 kept
    int nHeightSet = 0;            // the height was given, then anHeight is every minitile's
    unsigned char anHeight[16] = {};
    std::string image;
    std::vector<unsigned char> rgba;  // decoded, 32 x 32
  };
  std::map<int, unsigned> palette;  // an entry to 0xRRGGBB
  std::map<int, Group> groups;
  std::map<int, Megatile> megatiles;
  bool Any() const { return !palette.empty() || !groups.empty() || !megatiles.empty(); }
  // the tileset looks other than the classic one: a colour or a picture
  bool OwnLook() const;
};

// One tileset's member of types.json's "tilesets" read into *pOut, over what it holds (a mod
// after the mods it depends on), its pictures read from the data folder. nGroups and nMegatiles,
// when not 0, are the tileset's counts, which the numbers must be under. False with why.
bool ModTilesetParse(const PortJson &j, const std::string &dataFolder, int nEra, size_t nGroups, size_t nMegatiles,
                     ModTilesetChanges *pOut, std::string *pWhy);

// The changes made to a tileset's files held in memory: palette (1024 bytes), groups, minitile
// flags and megatiles changed in place; the minitiles the pictures need appended to *pNewCells,
// numbered from nMinitiles, the megatiles' references pointed at them. A picture's clear
// pixels (alpha under half) keep the classic tile's. Returns whether a minitile's flags changed.
bool ModTilesetApply(const ModTilesetChanges &c, int nEra, unsigned char *pPalette, unsigned short *pCv5, size_t nGroups,
                     unsigned short *pVf4, size_t nVf4, unsigned int *pVx4, size_t nMegatiles,
                     const unsigned char *pVr4, size_t nMinitiles, std::vector<unsigned char> *pNewCells);
// The same on a tileset decoded whole.
bool ModTilesetApplyTo(const ModTilesetChanges &c, int nEra, ModTileset *p);

// A colour mapped to the nearest entry of a palette (1024 bytes) that the game does not cycle.
int ModTilesetNearest(const unsigned char *pPalette, int nEra, unsigned rgb);

// ---- the loaded mod ------------------------------------------------------------------------------------
// At start-up, after each submission's types.json loads (port_moddata.cpp): its "tilesets",
// over what earlier submissions gave. False with why.
bool ModTilesetLoad(const PortJson &types, const std::string &dataFolder, std::string *pWhy);
// What the loaded mod changes, a line for each tileset it changes, for --mod-check's report.
std::vector<std::string> ModTilesetSummary();
// The mod's data put away (a load that failed).
void ModTilesetClear();

// In gamemap_init, once the tileset's files are read, for a game that has the mod: its changes to
// the map's tileset made to the game's buffers. The minitiles'
// buffer is given again when the pictures need more minitiles (SMemAlloc'd, the old one freed). True when a
// minitile's flags changed, and the flags' table must be made again.
bool ModTilesetForGame(int nEra, unsigned char *pPalette, unsigned short *pCv5, unsigned nGroups, unsigned short *pVf4,
                       unsigned nVf4, unsigned char **ppVx4, unsigned char **ppVr4);
// The game being played draws a tileset of the mod's own look: the HD terrain, Remastered's
// pictures of the classic megatiles in the classic colours, is left out for it.
bool ModTilesetOwnLook(int nEra);
// For a picture of a map in the menus: the loaded mod's changes to a tileset made to copies of its
// files, as a new game with the mod would have them; the minitiles the pictures need appended to
// the .vr4's. False when no mod's data loaded or it does not change the tileset.
bool ModTilesetForPicture(int nEra, unsigned char *pPalette, unsigned short *pCv5, size_t nGroups, unsigned int *pVx4,
                          size_t nMegatiles, std::vector<unsigned char> *pVr4);

#endif
