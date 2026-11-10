// studio_tilesets.cpp -- the Tilesets workspace: a mod's changes to a classic tileset (types.json's
// "tilesets", docs/mod-data.md "Tilesets"). The classic tileset is read from the player's archives
// and drawn with the mod's changes made by the engine's own code (port_modtileset.cpp), so what the
// workspace shows is what a game with the mod draws. Its groups are rows of their 16 megatiles in a
// grid that zooms; a megatile opened large shows its 16 minitiles' flags, painted with a brush; a
// picture is brought in for a megatile or a group; the palette is edited with its cycling colours
// moving; a patch of terrain is painted from the groups; and what the mod changed is listed, each
// change put back on its own or undone. Every change is a command, so Claude makes the same edits.
//
//   studio_tilesets {tileset?}                                   the tilesets and what the mod changes
//   studio_tileset {tileset, group? | megatile? | palette?}      a group, a megatile or the palette
//   studio_tile_set {tileset, group | megatile | colour | colours, field?, value | revert, merge?}
//   studio_tile_image {tileset, group | megatile, from | revert}  a picture brought in for it
//   studio_tile_export {tileset, group | megatile, to, classic?}  its picture written as a PNG
//   studio_tilesets_show {tileset?, view?, group?, megatile?, colour?, colours?, layer?, zoom?, overlay?}
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <sys/stat.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <SDL3/SDL.h>  // SDL_SavePNG
#include "imgui_internal.h"  // RegisterUserTexture, ImTextureDataQueueUpload
#include "../port/data/port_imagefile.h"  // PortImageDecode
#include "../port/data/port_modtileset.h"
#include "../port/data/port_stormapi.h"  // SFileUnloadFile
#include "../port/platform/port_util.h"  // PortReadFile
#include "../h/Storm.h"  // SFileLoadFile
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_media.h"  // StudioCopyAs, StudioModDataFolder

namespace {

using namespace studio_draw;

const char *const kShown[MOD_TILESETS] = {"Badlands", "Space Platform", "Installation", "Ash World",
                                          "Jungle",   "Desert",         "Ice",          "Twilight"};
enum { VIEW_TILES, VIEW_PALETTE };
enum { LAYER_WALK, LAYER_HEIGHT, LAYER_VIEW, LAYER_BUILD, LAYERS };
const char *const kLayers[LAYERS] = {"Walkable", "Height", "Blocks view", "Buildable"};
const char *const kLayerKeys[LAYERS] = {"walkable", "height", "blocks_view", "buildable"};
const int kPage = 32;  // groups a texture of the grid holds

// ---- the classic tilesets, read once ---------------------------------------------------------------------

struct Classic {
  bool bTried = false, bOk = false;
  std::string why;
  ModTileset t;
  std::vector<unsigned> uses;  // each palette entry's pixels among the minitiles
};
Classic g_classic[MOD_TILESETS];
bool g_bSharedTried = false;
bool g_abShared[256];  // entries every tileset has alike: the units' colours

const Classic &ClassicOf(int nEra) {
  Classic &c = g_classic[nEra];
  if (!c.bTried) {
    c.bTried = true;
    c.bOk = ModTilesetReadClassic(nEra, &c.t, &c.why);
    c.uses.assign(256, 0);
    for (unsigned char v : c.t.vr4)
      ++c.uses[v];
  }
  return c;
}

void SharedCheck(void) {
  if (g_bSharedTried)
    return;
  g_bSharedTried = true;
  std::vector<unsigned char> pal[MOD_TILESETS];
  for (int e = 0; e < MOD_TILESETS; ++e) {
    void *p = nullptr;
    DWORD cb = 0;
    if (SFileLoadFile((std::string("tileset\\") + kModTilesetNames[e] + ".wpe").c_str(), &p, &cb, 0, nullptr) && p) {
      pal[e].assign(static_cast<unsigned char *>(p), static_cast<unsigned char *>(p) + cb);
      SFileUnloadFile(p);
    }
  }
  for (int i = 0; i < 256; ++i) {
    g_abShared[i] = true;
    for (int e = 1; e < MOD_TILESETS; ++e)
      if (pal[e].size() < 1024 || pal[0].size() < 1024 || memcmp(&pal[e][i * 4], &pal[0][i * 4], 3) != 0)
        g_abShared[i] = false;
  }
}

// ---- the tileset as the mod makes it ------------------------------------------------------------------

struct Now {
  int nEra = -1;
  std::string key;
  ModTileset t;                // classic with every change, the mods it depends on first
  ModTilesetChanges own, all;  // the open mod's changes; and with its dependencies'
  std::string error;           // where the open mod's tileset does not read
  unsigned nSerial = 0;        // made again: the textures drawn from it are old
  std::vector<int> shownGroups;
};
Now g_now;
unsigned g_nFiles = 0;  // pictures brought in, which a path that stays the same does not show
int g_nEra = 0, g_nView = VIEW_TILES, g_nLayer = LAYER_WALK, g_nZoom = 1, g_nBrushHeight = 2;
int g_nGroup = 2, g_nSlot = 0, g_nColour = -1;
std::set<int> g_picked;  // the palette's entries picked for a recolour
bool g_bOverlay = false, g_bChangedOnly = false, g_bAllGroups = false, g_bScrollTo = true;
double g_dFilesChecked = 0;
std::string g_filesStamp;

const PortJson &OwnJson(int nEra) {
  return StudioDoc("types")["tilesets"][kModTilesetNames[nEra]];
}

// the pictures' files' times, so a picture changed on disk is drawn again
std::string FilesStamp(const PortJson &ts) {
  std::string s;
  for (const char *pszPart : {"groups", "megatiles"})
    for (const PortJsonMember &m : ts[pszPart].members) {
      const std::string rel = m.second["image"].Str();
      struct _stat64 st;
      if (!rel.empty() && _stat64((StudioDataFolder() + rel).c_str(), &st) == 0)
        s += rel + std::to_string(static_cast<long long>(st.st_mtime)) + ";";
    }
  return s;
}

bool Placeholder(const ModTileset &t, int g) {
  const unsigned short *w = &t.cv5[static_cast<size_t>(g) * MOD_GROUP_WORDS];
  std::set<int> megas;
  for (int k = 0; k < 16; ++k)
    if (w[MOD_GROUP_FIRST_MEGATILE + k] != 0)
      megas.insert(w[MOD_GROUP_FIRST_MEGATILE + k]);
  return megas.size() <= 1 && w[0] == 0 && (w[1] & MOD_GROUP_CREEP) == 0;
}

bool GroupChanged(const ModTilesetChanges &c, const ModTileset &t, int g) {
  if (c.groups.count(g))
    return true;
  for (int k = 0; k < 16; ++k)
    if (c.megatiles.count(t.cv5[static_cast<size_t>(g) * MOD_GROUP_WORDS + MOD_GROUP_FIRST_MEGATILE + k]))
      return true;
  return false;
}

const Now &NowOf(int nEra) {
  const Classic &cl = ClassicOf(nEra);
  if (ImGui::GetTime() - g_dFilesChecked > 1.0) {
    g_dFilesChecked = ImGui::GetTime();
    g_filesStamp = FilesStamp(OwnJson(nEra));
  }
  std::string key = std::to_string(nEra) + "|" + StudioModId() + "|" + StudioDataFolder() + "|" + std::to_string(g_nFiles) +
                    "|" + g_filesStamp + "|" + PortJsonWrite(OwnJson(nEra)) + "|" + (g_bChangedOnly ? "c" : "") +
                    (g_bAllGroups ? "a" : "");
  for (int i = 0; i < StudioDependencies(); ++i)
    key += "|" + PortJsonWrite(StudioDependencyTypes(i)["tilesets"][kModTilesetNames[nEra]]);
  if (g_now.nEra == nEra && g_now.key == key)
    return g_now;
  Now n;
  n.nEra = nEra;
  n.key = key;
  n.nSerial = g_now.nSerial + 1;
  n.t = cl.t;
  if (cl.bOk) {
    std::string why;
    for (int i = 0; i < StudioDependencies(); ++i) {
      const PortJson &dep = StudioDependencyTypes(i)["tilesets"][kModTilesetNames[nEra]];
      if (dep.kind != PortJson::NONE)
        ModTilesetParse(dep, StudioModDataFolder(StudioDependencyId(i)), nEra, cl.t.Groups(), cl.t.Megatiles(), &n.all, &why);
    }
    if (StudioIsOpen() && OwnJson(nEra).kind != PortJson::NONE) {
      if (!ModTilesetParse(OwnJson(nEra), StudioDataFolder(), nEra, cl.t.Groups(), cl.t.Megatiles(), &n.own, &why))
        n.error = why;
      why.clear();
      // the open mod's over its dependencies', as the loader reads them
      ModTilesetParse(OwnJson(nEra), StudioDataFolder(), nEra, cl.t.Groups(), cl.t.Megatiles(), &n.all, &why);
    }
    ModTilesetApplyTo(n.all, nEra, &n.t);
    for (int g = 0; g < static_cast<int>(n.t.Groups()); ++g) {
      const bool bChanged = GroupChanged(n.own, n.t, g);
      if (g_bChangedOnly ? bChanged : (g_bAllGroups || bChanged || !Placeholder(n.t, g)))
        n.shownGroups.push_back(g);
    }
  }
  g_now = std::move(n);
  return g_now;
}

// ---- drawing a tileset's pixels ---------------------------------------------------------------------------

// The palette as the game shows it nTick frames on (24 a second at Fastest): each plain cycle the
// game starts moved up one entry a step, the top one to the bottom.
std::vector<unsigned char> Cycled(const std::vector<unsigned char> &pal, int nEra, long long nTick, std::string *pKey) {
  std::vector<unsigned char> out = pal;
  if (pKey)
    pKey->clear();
  for (const ModTilesetCycle &c : ModTilesetCycles(nEra)) {
    if (!c.bStarted || c.bTable || c.nRate <= 0 || c.nHi <= c.nLo)
      continue;
    const int n = c.nHi - c.nLo + 1;
    const int nStep = static_cast<int>((nTick / c.nRate) % n);
    for (int k = 0; k < n; ++k)
      memcpy(&out[(c.nLo + (k + nStep) % n) * 4], &pal[(c.nLo + k) * 4], 4);
    if (pKey)
      *pKey += std::to_string(nStep) + ",";
  }
  return out;
}

long long Tick(void) {
  return static_cast<long long>(ImGui::GetTime() * 24.0);
}

// A megatile's 32 x 32 pixels into dst (RGBA), a row stride of nStride pixels.
void PaintMega(const ModTileset &t, const unsigned char *pal, int nMega, unsigned char *dst, int nStride) {
  if (nMega < 0 || static_cast<size_t>(nMega) >= t.Megatiles())
    return;
  const size_t nCells = t.Minitiles();
  for (int k = 0; k < 16; ++k) {
    const unsigned nRef = t.vx4[static_cast<size_t>(nMega) * 16 + k], nCell = nRef >> 1;
    const bool bMirror = (nRef & 1) != 0;
    if (nCell >= nCells)
      continue;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x) {
        const unsigned char v = t.vr4[nCell * 64 + y * 8 + (bMirror ? 7 - x : x)];
        unsigned char *d = dst + (static_cast<size_t>((k / 4) * 8 + y) * nStride + (k % 4) * 8 + x) * 4;
        d[0] = pal[v * 4];
        d[1] = pal[v * 4 + 1];
        d[2] = pal[v * 4 + 2];
        d[3] = 255;
      }
  }
}

int MegaOf(const ModTileset &t, int nGroup, int nSlot) {
  if (nGroup < 0 || static_cast<size_t>(nGroup) >= t.Groups() || nSlot < 0 || nSlot > 15)
    return -1;
  return t.cv5[static_cast<size_t>(nGroup) * MOD_GROUP_WORDS + MOD_GROUP_FIRST_MEGATILE + nSlot];
}

// ---- textures --------------------------------------------------------------------------------------------

struct Tex {
  ImTextureData *p = nullptr;
  unsigned nSerial = 0;
  std::string key;
  double tUsed = 0;
};
std::map<int, Tex> g_pages;  // the grid's, by page
Tex g_big, g_patch, g_groupTex;
std::vector<ImTextureData *> g_dying;

void Drop(Tex *pT) {
  if (pT->p) {
    pT->p->WantDestroyNextFrame = true;
    g_dying.push_back(pT->p);
  }
  pT->p = nullptr;
  pT->key.clear();
}

void TexTick(void) {
  static int s_nFrame = -1;
  if (s_nFrame == ImGui::GetFrameCount())
    return;
  s_nFrame = ImGui::GetFrameCount();
  for (auto it = g_pages.begin(); it != g_pages.end();) {
    if (ImGui::GetTime() - it->second.tUsed > 3.0) {
      Drop(&it->second);
      it = g_pages.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = g_dying.begin(); it != g_dying.end();) {
    if ((*it)->Status == ImTextureStatus_Destroyed) {
      ImGui::UnregisterUserTexture(*it);
      IM_DELETE(*it);
      it = g_dying.erase(it);
    } else {
      ++it;
    }
  }
}

// The texture given these pixels: the same one written again when its size is, else a new one.
void TexSet(Tex *pT, const std::vector<unsigned char> &rgba, int w, int h) {
  if (pT->p && pT->p->Width == w && pT->p->Height == h &&
      (pT->p->Status == ImTextureStatus_OK || pT->p->Status == ImTextureStatus_WantUpdates ||
       pT->p->Status == ImTextureStatus_WantCreate)) {
    memcpy(pT->p->Pixels, rgba.data(), static_cast<size_t>(w) * h * 4);
    ImTextureDataQueueUpload(pT->p, 0, 0, w, h);
    return;
  }
  Drop(pT);
  ImTextureData *p = IM_NEW(ImTextureData)();
  p->Create(ImTextureFormat_RGBA32, w, h);
  memcpy(p->Pixels, rgba.data(), static_cast<size_t>(w) * h * 4);
  p->UseColors = true;
  ImGui::RegisterUserTexture(p);
  pT->p = p;
}

void Nearest(ImDrawList *d, bool bOn) {
  ImGuiPlatformIO &io = ImGui::GetPlatformIO();
  ImDrawCallback cb = bOn ? io.DrawCallback_SetSamplerNearest : io.DrawCallback_SetSamplerLinear;
  if (cb)
    d->AddCallback(cb, nullptr);
}

// The grid's page of 32 groups, 512 x 1024, from the tileset as it is now (the cycles at rest).
ImTextureData *Page(const Now &n, int nPage) {
  Tex &t = g_pages[nPage];
  t.tUsed = ImGui::GetTime();
  if (t.p && t.nSerial == n.nSerial)
    return t.p;
  static int s_nFrame = -1, s_nMade = 0;
  if (s_nFrame != ImGui::GetFrameCount()) {
    s_nFrame = ImGui::GetFrameCount();
    s_nMade = 0;
  }
  if (t.p && s_nMade >= 3)
    return t.p;  // the old one a frame longer, while others are made
  ++s_nMade;
  std::vector<unsigned char> rgba(static_cast<size_t>(512) * 32 * kPage * 4, 0);
  for (int r = 0; r < kPage; ++r) {
    const int g = nPage * kPage + r;
    if (static_cast<size_t>(g) >= n.t.Groups())
      break;
    for (int k = 0; k < 16; ++k)
      PaintMega(n.t, n.t.palette.data(), MegaOf(n.t, g, k), &rgba[(static_cast<size_t>(r) * 32 * 512 + k * 32) * 4], 512);
  }
  TexSet(&t, rgba, 512, 32 * kPage);
  t.nSerial = n.nSerial;
  return t.p;
}

// ---- the open mod's tileset in types.json, changed through the model ------------------------------------

std::string Num(int n) {
  return std::to_string(n);
}

PortJson JBool(bool b) {
  PortJson v;
  v.kind = PortJson::BOOL;
  v.b = b;
  return v;
}

PortJson JStrings(const std::vector<std::string> &rows) {
  PortJson v;
  v.kind = PortJson::ARRAY;
  for (const std::string &s : rows)
    v.items.push_back(StudioJsonString(s));
  return v;
}

// A flag of 16 minitiles as types.json gives it: true or false for all, else four rows.
PortJson MaskJson(int nMask) {
  if (nMask == 0xFFFF || nMask == 0)
    return JBool(nMask != 0);
  std::vector<std::string> rows;
  for (int r = 0; r < 4; ++r) {
    std::string s;
    for (int c = 0; c < 4; ++c)
      s += (nMask >> (r * 4 + c)) & 1 ? '#' : '.';
    rows.push_back(s);
  }
  return JStrings(rows);
}

int HeightOf(unsigned short w) {
  return (w & MOD_CELL_HIGH) ? 2 : (w & MOD_CELL_MID) ? 1 : 0;
}

PortJson HeightJson(const int *pa) {
  bool bSame = true;
  for (int k = 1; k < 16; ++k)
    bSame = bSame && pa[k] == pa[0];
  static const char *const kWords[] = {"low", "mid", "high"};
  if (bSame)
    return StudioJsonString(kWords[pa[0]]);
  std::vector<std::string> rows;
  for (int r = 0; r < 4; ++r) {
    std::string s;
    for (int c = 0; c < 4; ++c)
      s += "lmh"[pa[r * 4 + c]];
    rows.push_back(s);
  }
  return JStrings(rows);
}

int MaskOf(const unsigned short *pCells, int nBit) {
  int n = 0;
  for (int k = 0; k < 16; ++k)
    if (pCells[k] & nBit)
      n |= 1 << k;
  return n;
}

bool Fail(std::string *pWhy, const std::string &what) {
  *pWhy = what;
  return false;
}

// The members a set leaves empty taken away: the group's or megatile's, its part, the tileset, tilesets.
bool Tidy(int nEra, const std::string &part, const std::string &n, std::string *pWhy) {
  const std::string name = kModTilesetNames[nEra];
  const PortJson &types = StudioDoc("types");
  std::vector<std::vector<std::string>> paths;
  if (!part.empty() && !n.empty())
    paths.push_back({"tilesets", name, part, n});
  if (!part.empty())
    paths.push_back({"tilesets", name, part});
  paths.push_back({"tilesets", name});
  paths.push_back({"tilesets"});
  for (const auto &path : paths) {
    const PortJson *p = &types;
    for (const std::string &k : path)
      p = &(*p)[k.c_str()];
    if (p->kind == PortJson::OBJECT && p->members.empty() &&
        !StudioPut("types", path, PortJson(), "", "", pWhy))
      return false;
  }
  return true;
}

// One member of the tileset set (or taken away, value NONE), checked by the loader's reader first.
bool SetMember(int nEra, const std::string &part, const std::string &n, const std::string &field, const PortJson &value,
               std::string *pWhy) {
  const Classic &cl = ClassicOf(nEra);
  if (!cl.bOk)
    return Fail(pWhy, cl.why);
  std::vector<std::string> path = {"tilesets", kModTilesetNames[nEra], part, n};
  if (!field.empty())
    path.push_back(field);
  if (value.kind != PortJson::NONE) {
    // what the loader would make of it alone
    PortJson trial = StudioJsonObject(), inner = StudioJsonObject();
    if (part == "palette") {
      StudioJsonPut(inner, n, value);
    } else {
      PortJson rec = StudioJsonObject();
      StudioJsonPut(rec, field, value);
      StudioJsonPut(inner, n, rec);
    }
    StudioJsonPut(trial, part, inner);
    ModTilesetChanges c;
    std::string why;
    if (!ModTilesetParse(trial, StudioDataFolder(), nEra, cl.t.Groups(), cl.t.Megatiles(), &c, &why))
      return Fail(pWhy, why);
  }
  const PortJson *pNow = &StudioDoc("types");
  for (const std::string &k : path)
    pNow = &(*pNow)[k.c_str()];
  if (PortJsonWrite(*pNow) == PortJsonWrite(value))
    return true;
  return StudioPut("types", path, value, "", "", pWhy) && Tidy(nEra, part, n, pWhy);
}

int EraOf(const PortJson &j, std::string *pWhy) {
  const std::string s = j["tileset"].Str(kModTilesetNames[g_nEra]);
  int n = ModTilesetEra(s);
  for (int i = 0; i < MOD_TILESETS && n < 0; ++i)
    if (Lower(s) == Lower(kShown[i]))
      n = i;
  if (n < 0)
    *pWhy = "no tileset " + s + ": badlands, platform, install, ashworld, jungle, desert, ice or twilight";
  return n;
}

std::string Hex(const unsigned char *p) {
  char sz[8];
  snprintf(sz, sizeof sz, "#%02x%02x%02x", p[0], p[1], p[2]);
  return sz;
}

std::string Hex(unsigned rgb) {
  char sz[8];
  snprintf(sz, sizeof sz, "#%06x", rgb & 0xFFFFFF);
  return sz;
}

// ---- what the mod changed, in words ---------------------------------------------------------------------

struct Change {
  std::string part, n, field;  // what a revert takes away
  std::string text;
  int nGroup = -1, nMega = -1, nColour = -1;
};

std::vector<Change> ChangesOf(int nEra) {
  std::vector<Change> out;
  const ModTileset &cl = ClassicOf(nEra).t;
  const PortJson &ts = OwnJson(nEra);
  for (const PortJsonMember &m : ts["palette"].members) {
    Change c;
    c.part = "palette";
    c.n = m.first;
    c.nColour = atoi(m.first.c_str());
    const std::string was = c.nColour >= 0 && c.nColour < 256 && cl.palette.size() >= 1024 ? Hex(&cl.palette[c.nColour * 4]) : "?";
    c.text = "Colour " + m.first + " is " + (m.second.kind == PortJson::STRING ? m.second.s : PortJsonWrite(m.second)) +
             ", was " + was;
    out.push_back(c);
  }
  static const std::map<std::string, std::string> kWords = {
      {"buildable", "buildable"}, {"creep", "creep"}, {"walkable", "walkable"}, {"height", "height"},
      {"blocks_view", "blocks view"}, {"image", "picture"}, {"comment", "name"}};
  for (const char *pszPart : {"groups", "megatiles"})
    for (const PortJsonMember &m : ts[pszPart].members)
      for (const PortJsonMember &f : m.second.members) {
        Change c;
        c.part = pszPart;
        c.n = m.first;
        c.field = f.first;
        (pszPart[0] == 'g' ? c.nGroup : c.nMega) = atoi(m.first.c_str());
        auto it = kWords.find(f.first);
        std::string what = it != kWords.end() ? it->second : f.first;
        std::string value = f.second.kind == PortJson::BOOL     ? (f.second.b ? "yes" : "no")
                            : f.second.kind == PortJson::STRING ? f.second.s
                                                                : "by minitile";
        c.text = std::string(pszPart[0] == 'g' ? "Group " : "Megatile ") + m.first + ": " + what + " " + value;
        out.push_back(c);
      }
  return out;
}

// ---- commands -------------------------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

bool Done(bool bOk, const std::string &why, std::string *pAnswer) {
  if (!bOk)
    *pAnswer = why;
  return bOk;
}

std::string CountsJson(int nEra) {
  const Now &n = NowOf(nEra);
  size_t nPictures = 0;
  for (const auto &g : n.own.groups)
    nPictures += g.second.image.empty() ? 0 : 1;
  for (const auto &m : n.own.megatiles)
    nPictures += m.second.image.empty() ? 0 : 1;
  return "\"colours\":" + Num(static_cast<int>(n.own.palette.size())) + ",\"groups\":" + Num(static_cast<int>(n.own.groups.size())) +
         ",\"megatiles\":" + Num(static_cast<int>(n.own.megatiles.size())) + ",\"pictures\":" + Num(static_cast<int>(nPictures));
}

// studio_tilesets {tileset?}: the eight tilesets and how much the mod changes each; or one: its counts,
// its cycles, what the mod changes, and where its tileset does not read.
bool CmdTilesets(const PortJson &j, std::string *pAnswer) {
  if (j["tileset"].kind == PortJson::NONE) {
    std::string s;
    for (int e = 0; e < MOD_TILESETS; ++e)
      s += std::string(e ? "," : "") + "{\"tileset\":" + Q(kModTilesetNames[e]) + ",\"name\":" + Q(kShown[e]) +
           ",\"changed\":" + (OwnJson(e).kind != PortJson::NONE ? "true" : "false") + "," + CountsJson(e) + "}";
    *pAnswer = "\"tilesets\":[" + s + "],\"shown\":" + Q(kModTilesetNames[g_nEra]);
    return true;
  }
  const int nEra = EraOf(j, pAnswer);
  if (nEra < 0)
    return false;
  const Classic &cl = ClassicOf(nEra);
  if (!cl.bOk) {
    *pAnswer = cl.why;
    return false;
  }
  const Now &n = NowOf(nEra);
  std::string cycles, changes;
  for (const ModTilesetCycle &c : ModTilesetCycles(nEra))
    cycles += std::string(cycles.empty() ? "" : ",") + "{\"from\":" + Num(c.nLo) + ",\"to\":" + Num(c.nHi) + ",\"frames\":" +
              Num(c.nRate) + ",\"table\":" + (c.bTable ? "true" : "false") + ",\"started\":" + (c.bStarted ? "true" : "false") + "}";
  for (const Change &c : ChangesOf(nEra))
    changes += std::string(changes.empty() ? "" : ",") + Q(c.text);
  *pAnswer = "\"tileset\":" + Q(kModTilesetNames[nEra]) + ",\"name\":" + Q(kShown[nEra]) + ",\"groups_count\":" +
             Num(static_cast<int>(cl.t.Groups())) + ",\"megatiles_count\":" + Num(static_cast<int>(cl.t.Megatiles())) +
             ",\"minitiles\":" + Num(static_cast<int>(cl.t.Minitiles())) + ",\"minitiles_now\":" +
             Num(static_cast<int>(n.t.Minitiles())) + ",\"cycles\":[" + cycles + "],\"changes\":[" + changes + "]," +
             CountsJson(nEra) + ",\"error\":" + (n.error.empty() ? std::string("null") : Q(n.error));
  return true;
}

std::string FlagsJson(const unsigned short *p) {
  std::string s;
  for (int k = 0; k < 16; ++k)
    s += std::string(k ? "," : "") + Num(p[k]);
  return "[" + s + "]";
}

std::string MegaJson(int nEra, int nMega) {
  const Now &n = NowOf(nEra);
  const ModTileset &cl = ClassicOf(nEra).t;
  const unsigned short *pNow = &n.t.vf4[static_cast<size_t>(nMega) * 16], *pWas = &cl.vf4[static_cast<size_t>(nMega) * 16];
  int an[16];
  for (int k = 0; k < 16; ++k)
    an[k] = HeightOf(pNow[k]);
  std::string groups;
  int nUses = 0;
  for (size_t g = 0; g < n.t.Groups(); ++g)
    for (int k = 0; k < 16; ++k)
      if (MegaOf(n.t, static_cast<int>(g), k) == nMega) {
        if (nUses++ < 24)
          groups += std::string(groups.empty() ? "" : ",") + Num(static_cast<int>(g));
        break;
      }
  auto it = n.own.megatiles.find(nMega);
  return "\"megatile\":" + Num(nMega) + ",\"flags\":" + FlagsJson(pNow) + ",\"classic_flags\":" + FlagsJson(pWas) +
         ",\"walkable\":" + PortJsonWrite(MaskJson(MaskOf(pNow, MOD_CELL_WALKABLE))) + ",\"height\":" +
         PortJsonWrite(HeightJson(an)) + ",\"blocks_view\":" + PortJsonWrite(MaskJson(MaskOf(pNow, MOD_CELL_BLOCKS_VIEW))) +
         ",\"image\":" + (it != n.own.megatiles.end() && !it->second.image.empty() ? Q(it->second.image) : std::string("null")) +
         ",\"in_groups\":[" + groups + "],\"groups_using\":" + Num(nUses) +
         ",\"record\":" + PortJsonWrite(OwnJson(nEra)["megatiles"][Num(nMega).c_str()]);
}

// The palette entries a group's megatiles draw with, the most used first.
std::vector<std::pair<int, int>> GroupColours(const ModTileset &t, int nGroup) {
  std::vector<int> n(256, 0);
  std::set<int> seen;
  for (int k = 0; k < 16; ++k) {
    const int m = MegaOf(t, nGroup, k);
    if (m <= 0 || static_cast<size_t>(m) >= t.Megatiles() || !seen.insert(m).second)
      continue;
    for (int c = 0; c < 16; ++c) {
      const size_t nCell = t.vx4[static_cast<size_t>(m) * 16 + c] >> 1;
      if (nCell < t.Minitiles())
        for (int i = 0; i < 64; ++i)
          ++n[t.vr4[nCell * 64 + i]];
    }
  }
  std::vector<std::pair<int, int>> out;
  for (int i = 0; i < 256; ++i)
    if (n[i])
      out.push_back({n[i], i});
  std::sort(out.rbegin(), out.rend());
  return out;
}

// studio_tileset {tileset, group? | megatile? | palette?}: a group (its kind, flags as they are and as
// classic has them, its 16 megatiles, the colours it draws with), a megatile (its 16 minitiles' flags
// now and classic, as types.json writes them, the groups that use it), or the palette's 256 entries.
bool CmdTileset(const PortJson &j, std::string *pAnswer) {
  const int nEra = EraOf(j, pAnswer);
  if (nEra < 0)
    return false;
  const Classic &cl = ClassicOf(nEra);
  if (!cl.bOk)
    return Done(false, cl.why, pAnswer);
  const Now &n = NowOf(nEra);
  if (j["group"].kind == PortJson::NUMBER) {
    const int g = static_cast<int>(j["group"].d);
    if (g < 0 || static_cast<size_t>(g) >= n.t.Groups())
      return Done(false, "no group " + Num(g) + " in " + kShown[nEra] + ", which has " + Num(static_cast<int>(n.t.Groups())), pAnswer);
    const unsigned short *w = &n.t.cv5[static_cast<size_t>(g) * MOD_GROUP_WORDS];
    const unsigned short *was = &cl.t.cv5[static_cast<size_t>(g) * MOD_GROUP_WORDS];
    std::string megas, colours;
    for (int k = 0; k < 16; ++k)
      megas += std::string(k ? "," : "") + Num(w[MOD_GROUP_FIRST_MEGATILE + k]);
    const auto cols = GroupColours(n.t, g);
    for (size_t i = 0; i < cols.size() && i < 16; ++i)
      colours += std::string(i ? "," : "") + "[" + Num(cols[i].second) + "," + Num(cols[i].first) + "]";
    *pAnswer = "\"group\":" + Num(g) + ",\"kind\":" + Num(w[0]) + ",\"flags\":" + Num(w[1]) + ",\"classic_flags\":" + Num(was[1]) +
               ",\"buildable\":" + ((w[1] & MOD_GROUP_UNBUILDABLE) ? "false" : "true") + ",\"creep\":" +
               ((w[1] & MOD_GROUP_CREEP) ? "true" : "false") + ",\"megatiles\":[" + megas + "],\"colours\":[" + colours +
               "],\"record\":" + PortJsonWrite(OwnJson(nEra)["groups"][Num(g).c_str()]);
    return true;
  }
  if (j["megatile"].kind == PortJson::NUMBER) {
    const int m = static_cast<int>(j["megatile"].d);
    if (m < 0 || static_cast<size_t>(m) >= n.t.Megatiles())
      return Done(false, "no megatile " + Num(m) + " in " + kShown[nEra], pAnswer);
    *pAnswer = MegaJson(nEra, m);
    return true;
  }
  SharedCheck();
  std::string s;
  for (int i = 0; i < 256; ++i)
    s += std::string(i ? "," : "") + "{\"index\":" + Num(i) + ",\"now\":" + Q(Hex(&n.t.palette[i * 4])) + ",\"classic\":" +
         Q(Hex(&cl.t.palette[i * 4])) + ",\"cycled\":" + (ModTilesetCycled(nEra, i) ? "true" : "false") +
         ",\"units\":" + (g_abShared[i] ? "true" : "false") + ",\"pixels\":" + Num(static_cast<int>(cl.uses[i])) + "}";
  *pAnswer = "\"tileset\":" + Q(kModTilesetNames[nEra]) + ",\"palette\":[" + s + "]";
  return true;
}

// studio_tile_set {tileset, group | megatile, field?, value | revert} or {tileset, colour, value | revert}
// or {tileset, colours: {"216": "#rrggbb" | null, ...}}: one change to the tileset, an edit; a field
// taken back to classic with revert, or the whole group, megatile or colour without a field.
bool CmdTileSet(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen())
    return Done(false, "no mod is open", pAnswer);
  const int nEra = EraOf(j, pAnswer);
  if (nEra < 0)
    return false;
  const bool bRevert = j["revert"].kind == PortJson::BOOL && j["revert"].b;
  std::string why, part, n, label;
  const std::string field = j["field"].Str();
  if (j["colours"].kind == PortJson::OBJECT) {
    StudioGroupBegin(Sender(j), "recoloured " + std::string(kShown[nEra]), j["merge"].Str());
    bool bOk = true;
    for (const PortJsonMember &m : j["colours"].members) {
      bOk = SetMember(nEra, "palette", m.first, "", m.second.kind == PortJson::NULLV ? PortJson() : m.second, &why);
      if (!bOk)
        break;
    }
    StudioGroupEnd();
    if (bOk)
      *pAnswer = "\"tileset\":" + Q(kModTilesetNames[nEra]) + "," + CountsJson(nEra);
    return Done(bOk, why, pAnswer);
  }
  if (j["colour"].kind == PortJson::NUMBER) {
    part = "palette";
    n = Num(static_cast<int>(j["colour"].d));
    label = "colour " + n;
  } else if (j["group"].kind == PortJson::NUMBER) {
    part = "groups";
    n = Num(static_cast<int>(j["group"].d));
    label = "group " + n;
  } else if (j["megatile"].kind == PortJson::NUMBER) {
    part = "megatiles";
    n = Num(static_cast<int>(j["megatile"].d));
    label = "megatile " + n;
  } else {
    return Done(false, "studio_tile_set needs a group, a megatile, a colour or colours", pAnswer);
  }
  if (!bRevert && j["value"].kind == PortJson::NONE)
    return Done(false, "studio_tile_set needs a value, or revert", pAnswer);
  if (part != "palette" && field.empty() && !bRevert)
    return Done(false, "studio_tile_set needs the field of the " + label, pAnswer);
  StudioGroupBegin(Sender(j), (bRevert ? "put back " : "set ") + std::string(kShown[nEra]) + " " + label +
                                  (field.empty() ? std::string() : " " + field),
                   j["merge"].Str());
  const bool bOk = SetMember(nEra, part, n, part == "palette" ? std::string() : field,
                             bRevert ? PortJson() : j["value"], &why);
  StudioGroupEnd();
  if (bOk)
    *pAnswer = "\"tileset\":" + Q(kModTilesetNames[nEra]) + "," + CountsJson(nEra);
  return Done(bOk, why, pAnswer);
}

// A picture checked as the loader takes it: a megatile's square, a group's 16 megatiles in a row.
bool PictureFits(const std::string &path, bool bGroup, std::string *pWhy) {
  std::vector<unsigned char> file, rgba;
  if (!PortReadFile(path, &file))
    return Fail(pWhy, "no such file: " + path);
  int w = 0, h = 0;
  std::string err;
  if (!PortImageDecode(file.data(), file.size(), path, &rgba, &w, &h, &err))
    return Fail(pWhy, path + " does not decode: " + err);
  if (h < 32 || h % 32 != 0 || w != h * (bGroup ? 16 : 1))
    return Fail(pWhy, path + " is " + Num(w) + " by " + Num(h) +
                          (bGroup ? "; a group's picture is its 16 megatiles in a row, 512 by 32 or a multiple"
                                  : "; a megatile's picture is 32 by 32 or a multiple"));
  return true;
}

// studio_tile_image {tileset, group | megatile, from | revert}: a PNG brought into the mod as the
// picture of a megatile (32 by 32) or of a group's 16 (512 by 32), mapped into the palette as the
// game reads it; its clear pixels keep the classic tile's. revert takes the picture away (the file stays).
bool CmdTileImage(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen())
    return Done(false, "no mod is open", pAnswer);
  const int nEra = EraOf(j, pAnswer);
  if (nEra < 0)
    return false;
  const bool bGroup = j["group"].kind == PortJson::NUMBER;
  if (!bGroup && j["megatile"].kind != PortJson::NUMBER)
    return Done(false, "studio_tile_image needs a group or a megatile", pAnswer);
  const int nWhich = static_cast<int>(bGroup ? j["group"].d : j["megatile"].d);
  const std::string part = bGroup ? "groups" : "megatiles";
  std::string why;
  StudioGroupBegin(Sender(j), std::string(j["revert"].b ? "took away" : "brought in") + " the picture of " +
                                  (bGroup ? "group " : "megatile ") + Num(nWhich));
  bool bOk;
  if (j["revert"].kind == PortJson::BOOL && j["revert"].b) {
    bOk = SetMember(nEra, part, Num(nWhich), "image", PortJson(), &why);
  } else {
    const std::string from = j["from"].Str();
    const std::string rel = std::string("tilesets/") + kModTilesetNames[nEra] + "/" + part + "/" + Num(nWhich) + ".png";
    std::string relBack = rel;
    std::replace(relBack.begin(), relBack.end(), '/', '\\');
    bOk = PictureFits(from, bGroup, &why) && StudioCopyAs(from, relBack, &why);
    if (bOk) {
      ++g_nFiles;
      bOk = SetMember(nEra, part, Num(nWhich), "image", StudioJsonString(rel), &why);
    }
  }
  StudioGroupEnd();
  if (bOk)
    *pAnswer = "\"tileset\":" + Q(kModTilesetNames[nEra]) + "," + CountsJson(nEra);
  return Done(bOk, why, pAnswer);
}

bool SavePng(const std::string &path, std::vector<unsigned char> &rgba, int w, int h, std::string *pWhy) {
  for (size_t at = path.find_first_of("\\/", 3); at != std::string::npos; at = path.find_first_of("\\/", at + 1))
    CreateDirectoryA(path.substr(0, at).c_str(), nullptr);
  SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba.data(), w * 4);
  const bool bOk = s != nullptr && SDL_SavePNG(s, path.c_str());
  if (s)
    SDL_DestroySurface(s);
  return bOk || Fail(pWhy, "could not write " + path);
}

// The picture of a megatile or a group's 16, now or classic, as RGBA.
std::vector<unsigned char> PictureOf(int nEra, bool bGroup, int nWhich, bool bClassic, int *pnW) {
  const ModTileset &t = bClassic ? ClassicOf(nEra).t : NowOf(nEra).t;
  const int w = bGroup ? 512 : 32;
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * 32 * 4, 0);
  for (int k = 0; k < (bGroup ? 16 : 1); ++k)
    PaintMega(t, t.palette.data(), bGroup ? MegaOf(t, nWhich, k) : nWhich, &rgba[k * 32 * 4], w);
  *pnW = w;
  return rgba;
}

// studio_tile_export {tileset, group | megatile, to, classic?}: its picture as it is (or classic's)
// written to a PNG, a start to draw over.
bool CmdTileExport(const PortJson &j, std::string *pAnswer) {
  const int nEra = EraOf(j, pAnswer);
  if (nEra < 0)
    return false;
  if (!ClassicOf(nEra).bOk)
    return Done(false, ClassicOf(nEra).why, pAnswer);
  const bool bGroup = j["group"].kind == PortJson::NUMBER;
  if (!bGroup && j["megatile"].kind != PortJson::NUMBER)
    return Done(false, "studio_tile_export needs a group or a megatile", pAnswer);
  const std::string to = j["to"].Str();
  if (to.empty())
    return Done(false, "studio_tile_export needs a file to write (to)", pAnswer);
  int w = 0;
  std::vector<unsigned char> rgba =
      PictureOf(nEra, bGroup, static_cast<int>(bGroup ? j["group"].d : j["megatile"].d), j["classic"].b, &w);
  std::string why;
  if (!SavePng(to, rgba, w, 32, &why))
    return Done(false, why, pAnswer);
  *pAnswer = "\"written\":" + Q(to) + ",\"width\":" + Num(w) + ",\"height\":32";
  return true;
}

int LayerOf(const std::string &s) {
  for (int i = 0; i < LAYERS; ++i)
    if (s == kLayerKeys[i] || Lower(s) == Lower(kLayers[i]))
      return i;
  return -1;
}

// studio_tilesets_show {tileset?, view?, group?, megatile?, colour?, colours?, layer?, zoom?, overlay?,
// changed?}: the workspace shown at a tileset, a group (its megatile at a place, or a megatile), a
// palette entry or entries picked, a layer of flags, the grid's zoom and its flags shown over it.
bool CmdShow(const PortJson &j, std::string *pAnswer) {
  if (j["tileset"].kind != PortJson::NONE) {
    const int nEra = EraOf(j, pAnswer);
    if (nEra < 0)
      return false;
    if (nEra != g_nEra)
      g_nGroup = 2, g_nSlot = 0;
    g_nEra = nEra;
  }
  const std::string view = j["view"].Str();
  if (view == "tiles" || view == "palette")
    g_nView = view == "tiles" ? VIEW_TILES : VIEW_PALETTE;
  const Now &n = NowOf(g_nEra);
  if (j["group"].kind == PortJson::NUMBER) {
    g_nGroup = std::max(0, std::min(static_cast<int>(n.t.Groups()) - 1, static_cast<int>(j["group"].d)));
    g_nSlot = std::max(0, std::min(15, static_cast<int>(j["slot"].Num(0))));
    g_bScrollTo = true;
  }
  if (j["megatile"].kind == PortJson::NUMBER) {
    const int m = static_cast<int>(j["megatile"].d);
    for (size_t g = 0; g < n.t.Groups(); ++g)
      for (int k = 0; k < 16; ++k)
        if (MegaOf(n.t, static_cast<int>(g), k) == m) {
          g_nGroup = static_cast<int>(g), g_nSlot = k, g_bScrollTo = true;
          g = n.t.Groups();
          break;
        }
  }
  if (j["colour"].kind == PortJson::NUMBER) {
    g_nColour = std::max(0, std::min(255, static_cast<int>(j["colour"].d)));
    g_picked = {g_nColour};
    g_nView = view.empty() ? VIEW_PALETTE : g_nView;
  }
  if (j["colours"].kind == PortJson::ARRAY) {
    g_picked.clear();
    for (const PortJson &c : j["colours"].items)
      if (c.kind == PortJson::NUMBER && c.d >= 0 && c.d < 256)
        g_picked.insert(static_cast<int>(c.d));
    g_nColour = g_picked.empty() ? -1 : *g_picked.begin();
    g_nView = view.empty() ? VIEW_PALETTE : g_nView;
  }
  if (j["layer"].kind == PortJson::STRING && LayerOf(j["layer"].s) >= 0)
    g_nLayer = LayerOf(j["layer"].s);
  if (j["zoom"].kind == PortJson::NUMBER)
    g_nZoom = std::max(1, std::min(3, static_cast<int>(j["zoom"].d)));
  if (j["overlay"].kind == PortJson::BOOL)
    g_bOverlay = j["overlay"].b;
  if (j["changed"].kind == PortJson::BOOL)
    g_bChangedOnly = j["changed"].b;
  StudioWorkspaceSet(12);
  *pAnswer = "\"tileset\":" + Q(kModTilesetNames[g_nEra]) + ",\"view\":" + Q(g_nView == VIEW_TILES ? "tiles" : "palette") +
             ",\"group\":" + Num(g_nGroup) + ",\"megatile\":" + Num(MegaOf(n.t, g_nGroup, g_nSlot)) + ",\"layer\":" +
             Q(kLayerKeys[g_nLayer]);
  return true;
}

// ---- the panes ------------------------------------------------------------------------------------------

void Ask(const char *pszCmd, const std::string &fields) {
  StudioAsk(pszCmd, "\"tileset\":" + Q(kModTilesetNames[g_nEra]) + "," + fields);
}

void Tip(const char *psz) {
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", psz);
}

ImU32 Rgb(const unsigned char *p, float a = 1.0f) {
  return IM_COL32(p[0], p[1], p[2], static_cast<int>(a * 255.0f));
}

// The bar above: the tileset, the view, what the mod changes in it.
void Bar(ImDrawList *d, ImVec2 o, float W) {
  const float h = P(32.0f);
  float x = o.x;
  x += Wide(d, 11.0f, x, Mid(o.y, h, 11.0f), C(TK_HEAD), "Tilesets") + P(16.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  if (StudioSelectBegin("##tileset", kShown[g_nEra], 150.0f)) {
    for (int e = 0; e < MOD_TILESETS; ++e) {
      const std::string label = std::string(kShown[e]) + (OwnJson(e).kind != PortJson::NONE ? "  (changed)" : "");
      if (StudioItem(label.c_str(), e == g_nEra) && e != g_nEra) {
        g_nEra = e;
        g_nGroup = 2, g_nSlot = 0, g_bScrollTo = true;
      }
    }
    StudioSelectEnd();
  }
  Tip("The classic tileset the mod changes. A map plays on the tileset its era names; a game with the mod\n"
      "draws and plays that tileset with these changes.");
  x += P(150.0f) + P(10.0f);
  static const char *const kViews[] = {"Tiles", "Palette"};
  ImGui::SetCursorScreenPos(ImVec2(x, o.y + (h - P(26.0f)) / 2.0f));
  g_nView = StudioSeg("##view", kViews, 2, g_nView);
  x += StudioSegWidth(kViews, 2) + P(16.0f);
  const Now &n = NowOf(g_nEra);
  const ModTileset &cl = ClassicOf(g_nEra).t;
  std::string what = Num(static_cast<int>(cl.Groups())) + " groups, " + Num(static_cast<int>(cl.Megatiles())) + " megatiles, " +
                     Num(static_cast<int>(cl.Minitiles())) + " minitiles";
  const std::vector<Change> changes = ChangesOf(g_nEra);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(o.y, h, kStudioFsS), C(TK_INK3), what, W - (x - o.x) - P(330.0f));
  const std::string mine = changes.empty() ? "The mod leaves it classic" : "The mod makes " + Num(static_cast<int>(changes.size())) +
                                                                                (changes.size() == 1 ? " change" : " changes");
  const float mw = TW(STUDIO_FONT_BOLD, kStudioFsS, mine);
  T(d, STUDIO_FONT_BOLD, kStudioFsS, o.x + W - mw, Mid(o.y, h, kStudioFsS), changes.empty() ? C(TK_INK3) : C(TK_CHANGED), mine);
  if (!n.error.empty()) {
    const float ew = TW(STUDIO_FONT_TEXT, kStudioFsXs, n.error);
    (void)ew;
  }
}

// Thin diagonal lines over a box, for the empty megatile.
void Hatch(ImDrawList *d, ImVec2 a, ImVec2 b) {
  d->PushClipRect(a, b, true);
  d->AddRectFilled(a, b, C(TK_INK3, 0.10f));
  const float step = P(6.0f), h = b.y - a.y;
  for (float x = a.x - h; x < b.x; x += step)
    d->AddLine(ImVec2(x, b.y), ImVec2(x + h, a.y), C(TK_INK3, 0.35f), P(1.0f));
  d->PopClipRect();
}

// A minitile's mark for a layer, in a cell's box, and whether it differs from classic.
void CellMark(ImDrawList *d, ImVec2 a, ImVec2 b, int nLayer, unsigned short w, unsigned short was, unsigned short wGroup,
              bool bBig) {
  const float fs = (b.x - a.x) / P(1.0f);
  if (nLayer == LAYER_WALK && !(w & MOD_CELL_WALKABLE)) {
    d->AddRectFilled(a, b, IM_COL32(230, 60, 50, 105));
    if (bBig) {
      d->AddLine(a, b, IM_COL32(255, 120, 110, 150), P(1.5f));
      d->AddLine(ImVec2(b.x, a.y), ImVec2(a.x, b.y), IM_COL32(255, 120, 110, 150), P(1.5f));
    }
  } else if (nLayer == LAYER_HEIGHT && HeightOf(w) > 0) {
    d->AddRectFilled(a, b, HeightOf(w) == 2 ? IM_COL32(250, 150, 40, 120) : IM_COL32(250, 220, 80, 80));
    if (bBig)
      T(d, STUDIO_FONT_BOLD, std::min(18.0f, fs * 0.36f), a.x + P(4.0f), a.y + P(2.0f), IM_COL32(255, 245, 220, 235),
        HeightOf(w) == 2 ? "H" : "M");
  } else if (nLayer == LAYER_VIEW && (w & MOD_CELL_BLOCKS_VIEW)) {
    d->AddRectFilled(a, b, IM_COL32(70, 120, 255, 120));
    if (bBig)
      T(d, STUDIO_FONT_BOLD, std::min(18.0f, fs * 0.36f), a.x + P(4.0f), a.y + P(2.0f), IM_COL32(225, 235, 255, 235), "V");
  } else if (nLayer == LAYER_BUILD && (wGroup & MOD_GROUP_UNBUILDABLE)) {
    d->AddRectFilled(a, b, IM_COL32(230, 60, 50, 80));
  }
  const unsigned short nBits = nLayer == LAYER_WALK ? MOD_CELL_WALKABLE : nLayer == LAYER_HEIGHT ? (MOD_CELL_MID | MOD_CELL_HIGH)
                               : nLayer == LAYER_VIEW ? MOD_CELL_BLOCKS_VIEW : 0;
  if (bBig && nBits && (w & nBits) != (was & nBits))
    d->AddRect(ImVec2(a.x + P(1.0f), a.y + P(1.0f)), ImVec2(b.x - P(1.0f), b.y - P(1.0f)), C(TK_CHANGED), 0.0f, 0, P(2.0f));
}

// The groups' grid: each group a row, its number and flags at the left, its 16 megatiles after.
void Grid(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Now &n = NowOf(g_nEra);
  const ModTileset &cl = ClassicOf(g_nEra).t;
  StudioPane(d, a, b, 34.0f);
  float x = a.x + P(10.0f);
  x += Wide(d, 10.5f, x, Mid(a.y, P(34.0f), 10.5f), C(TK_HEAD), "Groups") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, P(34.0f), kStudioFsXs), C(TK_INK3),
    Num(static_cast<int>(n.shownGroups.size())) + " shown, each its 16 megatiles");
  // the controls at the right of the head
  float rx = b.x - P(8.0f);
  static const char *const kZoom[] = {"1x", "2x", "3x"};
  rx -= StudioSegWidth(kZoom, 3, kStudioFsXs);
  ImGui::SetCursorScreenPos(ImVec2(rx, a.y + P(5.0f)));
  g_nZoom = StudioSeg("##zoom", kZoom, 3, g_nZoom - 1, 24.0f, kStudioFsXs) + 1;
  Tip("The grid's zoom (Ctrl and the wheel over it)");
  const char *const kFilter[] = {"Terrain", "Changed", "All"};
  const int nFilter = g_bChangedOnly ? 1 : g_bAllGroups ? 2 : 0;
  rx -= StudioSegWidth(kFilter, 3, kStudioFsXs) + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(rx, a.y + P(5.0f)));
  const int nF = StudioSeg("##filter", kFilter, 3, nFilter, 24.0f, kStudioFsXs);
  g_bChangedOnly = nF == 1;
  g_bAllGroups = nF == 2;
  Tip("Terrain: the groups that draw something, the empty places left out; Changed: those the mod\n"
      "changes, or whose megatiles it does; All: every group of the tileset");
  bool bOverlay = g_bOverlay;
  const float cw = StudioTextW(STUDIO_FONT_TEXT, kStudioFsS, "Flags over the tiles") + P(36.0f);
  rx -= cw + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(rx, a.y + P(4.0f)));
  if (StudioCheck("##overlay", "Flags over the tiles", &bOverlay))
    g_bOverlay = bOverlay;
  Tip("The layer the megatile's view paints (walkable, height, blocks view, buildable) drawn over every tile");

  const int ts = static_cast<int>(floorf(P(32.0f))) * g_nZoom;
  const float labelW = P(136.0f), rowH = static_cast<float>(ts) + P(8.0f);
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + P(34.0f)));
  ImGui::BeginChild("##groups", ImVec2(b.x - a.x - 2, b.y - a.y - P(34.0f) - 1), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
  ImDrawList *cd = ImGui::GetWindowDrawList();
  if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
    g_nZoom = std::max(1, std::min(3, g_nZoom + (ImGui::GetIO().MouseWheel > 0 ? 1 : -1)));
    ImGui::SetScrollY(ImGui::GetScrollY());
  }
  const int nRows = static_cast<int>(n.shownGroups.size());
  if (g_bScrollTo) {
    g_bScrollTo = false;
    const auto it = std::find(n.shownGroups.begin(), n.shownGroups.end(), g_nGroup);
    if (it != n.shownGroups.end()) {
      const float y = static_cast<float>(it - n.shownGroups.begin()) * rowH;
      if (y < ImGui::GetScrollY() || y + rowH > ImGui::GetScrollY() + ImGui::GetWindowHeight())
        ImGui::SetScrollY(std::max(0.0f, y - ImGui::GetWindowHeight() * 0.3f));
    }
  }
  ImGuiListClipper clip;
  clip.Begin(nRows, rowH);
  Nearest(cd, true);
  while (clip.Step()) {
    for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
      const int g = n.shownGroups[r];
      ImGui::PushID(g);
      const ImVec2 o = ImGui::GetCursorScreenPos();
      const unsigned short *w = &n.t.cv5[static_cast<size_t>(g) * MOD_GROUP_WORDS];
      const bool bSel = g == g_nGroup;
      const bool bChanged = GroupChanged(n.own, n.t, g);
      if (bSel)
        cd->AddRectFilled(o, ImVec2(o.x + labelW + 16 * ts + P(12.0f), o.y + rowH), C(TK_SEL, 0.55f), P(3.0f));
      // the number and the group's chips on the first line, the mod's name for it below
      T(cd, STUDIO_FONT_MONO, 13.0f, o.x + P(10.0f), o.y + P(4.0f), bSel ? C(TK_HI) : C(TK_INK), Num(g));
      float chx = o.x + P(48.0f);
      const float chy = o.y + P(4.0f);
      if (w[1] & MOD_GROUP_UNBUILDABLE)
        chx += StudioChip(cd, ImVec2(chx, chy), "no build", CH_WARN, 15.0f, 8.0f) + P(3.0f);
      if ((w[1] & MOD_GROUP_CREEP) && chx + P(36.0f) < o.x + labelW)
        chx += StudioChip(cd, ImVec2(chx, chy), "creep", CH_PLAIN, 15.0f, 8.0f) + P(3.0f);
      if (bChanged && chx + P(30.0f) < o.x + labelW)
        StudioChip(cd, ImVec2(chx, chy), "mod", CH_CHANGED, 15.0f, 8.0f);
      const std::string comment = OwnJson(g_nEra)["groups"][Num(g).c_str()]["comment"].Str();
      if (!comment.empty() && ts >= 32)
        T(cd, STUDIO_FONT_TEXT, kStudioFsXs, o.x + P(10.0f), o.y + P(22.0f), C(TK_INK2), comment, labelW - P(14.0f));
      if (bChanged)
        cd->AddRectFilled(ImVec2(o.x + P(2.0f), o.y + P(3.0f)), ImVec2(o.x + P(5.0f), o.y + rowH - P(3.0f)), C(TK_CHANGED));
      // the 16 megatiles from the group's page
      ImTextureData *pTex = Page(n, g / kPage);
      const ImVec2 t0(o.x + labelW, o.y + P(4.0f));
      if (pTex) {
        const float v0 = static_cast<float>(g % kPage) / kPage, v1 = static_cast<float>(g % kPage + 1) / kPage;
        cd->AddImage(pTex->GetTexRef(), t0, ImVec2(t0.x + 16 * ts, t0.y + ts), ImVec2(0, v0), ImVec2(1, v1));
      }
      ImGui::SetCursorScreenPos(t0);
      ImGui::InvisibleButton("##tiles", ImVec2(static_cast<float>(16 * ts), static_cast<float>(ts)));
      const bool bHover = ImGui::IsItemHovered();
      int nHoverSlot = -1;
      if (bHover)
        nHoverSlot = std::max(0, std::min(15, static_cast<int>((ImGui::GetIO().MousePos.x - t0.x) / ts)));
      if (ImGui::IsItemClicked()) {
        g_nGroup = g;
        g_nSlot = nHoverSlot;
      }
      if (g_bOverlay) {
        const float cs = ts / 4.0f;
        for (int k = 0; k < 16; ++k) {
          const int m = w[MOD_GROUP_FIRST_MEGATILE + k];
          if (m <= 0 || static_cast<size_t>(m) >= n.t.Megatiles())
            continue;
          const unsigned short *cells = &n.t.vf4[static_cast<size_t>(m) * 16];
          for (int c = 0; c < 16; ++c) {
            const ImVec2 ca(t0.x + k * ts + (c % 4) * cs, t0.y + (c / 4) * cs);
            CellMark(cd, ca, ImVec2(ca.x + cs, ca.y + cs), g_nLayer, cells[c], cells[c], w[1], false);
          }
        }
      }
      for (int k = 0; k < 16; ++k) {
        const int m = w[MOD_GROUP_FIRST_MEGATILE + k];
        const ImVec2 ka(t0.x + k * ts, t0.y), kb(t0.x + (k + 1) * ts, t0.y + ts);
        if (m == 0)  // the empty megatile: hatched, so it does not read as black ground
          Hatch(cd, ka, kb);
        if (n.own.megatiles.count(m) && m != 0)
          cd->AddRect(ka, kb, C(TK_CHANGED), 0.0f, 0, P(1.5f));
        if (bSel && k == g_nSlot)
          cd->AddRect(ImVec2(ka.x - P(1.0f), ka.y - P(1.0f)), ImVec2(kb.x + P(1.0f), kb.y + P(1.0f)), C(TK_HI), 0.0f, 0, P(2.0f));
        else if (k == nHoverSlot)
          cd->AddRect(ka, kb, C(TK_INK, 0.7f), 0.0f, 0, P(1.0f));
      }
      if (bHover && nHoverSlot >= 0) {
        const int m = w[MOD_GROUP_FIRST_MEGATILE + nHoverSlot];
        ImGui::SetTooltip("Group %d, place %d: megatile %d%s", g, nHoverSlot, m,
                          m == 0 ? " (the empty one)" : n.own.megatiles.count(m) ? ", the mod changes it" : "");
      }
      (void)cl;
      ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rowH));
      ImGui::Dummy(ImVec2(labelW + 16 * ts + P(12.0f), 0.0f));
      ImGui::PopID();
    }
  }
  Nearest(cd, false);
  if (nRows == 0)
    T(cd, STUDIO_FONT_TEXT, kStudioFs, a.x + P(16.0f), a.y + P(50.0f), C(TK_INK3),
      g_bChangedOnly ? "The mod changes no group of this tileset yet." : "The tileset does not read.");
  ImGui::EndChild();
}

// The megatile open large, its 16 minitiles' flags over it; a brush paints the layer.
struct Stroke {
  bool bOn = false;
  int nMega = -1, nLayer = -1, nValue = 0;
  unsigned nId = 0;
  int nLast = -1;
};
Stroke g_stroke;

void PaintCell(int nMega, int nCell, int nValue) {
  const Now &n = NowOf(g_nEra);
  const ModTileset &cl = ClassicOf(g_nEra).t;
  const unsigned short *pNow = &n.t.vf4[static_cast<size_t>(nMega) * 16];
  const unsigned short *pWas = &cl.vf4[static_cast<size_t>(nMega) * 16];
  const std::string merge = "\"merge\":" + Q("tile-brush-" + Num(static_cast<int>(g_stroke.nId)));
  const std::string at = "\"megatile\":" + Num(nMega) + ",";
  if (g_nLayer == LAYER_HEIGHT) {
    int an[16], anWas[16];
    for (int k = 0; k < 16; ++k)
      an[k] = HeightOf(pNow[k]), anWas[k] = HeightOf(pWas[k]);
    if (an[nCell] == nValue)
      return;
    an[nCell] = nValue;
    const bool bClassic = std::equal(an, an + 16, anWas);
    Ask("studio_tile_set", at + "\"field\":\"height\"," + (bClassic ? std::string("\"revert\":true") : "\"value\":" + PortJsonWrite(HeightJson(an))) +
                               "," + merge);
    return;
  }
  const int nBit = g_nLayer == LAYER_WALK ? MOD_CELL_WALKABLE : MOD_CELL_BLOCKS_VIEW;
  int nMask = MaskOf(pNow, nBit);
  const int nNew = nValue ? nMask | (1 << nCell) : nMask & ~(1 << nCell);
  if (nNew == nMask)
    return;
  const char *pszField = g_nLayer == LAYER_WALK ? "walkable" : "blocks_view";
  Ask("studio_tile_set", at + "\"field\":\"" + pszField + "\"," +
                             (nNew == MaskOf(pWas, nBit) ? std::string("\"revert\":true") : "\"value\":" + PortJsonWrite(MaskJson(nNew))) +
                             "," + merge);
}

void BigMegatile(ImDrawList *d, ImVec2 o, float side, int nMega) {
  const Now &n = NowOf(g_nEra);
  const ModTileset &cl = ClassicOf(g_nEra).t;
  std::string key;
  const std::vector<unsigned char> pal = Cycled(n.t.palette, g_nEra, Tick(), &key);
  key += "|" + Num(nMega) + "|" + Num(static_cast<int>(n.nSerial));
  if (g_big.key != key || !g_big.p) {
    std::vector<unsigned char> rgba(32 * 32 * 4, 0);
    PaintMega(n.t, pal.data(), nMega, rgba.data(), 32);
    TexSet(&g_big, rgba, 32, 32);
    g_big.key = key;
  }
  const ImVec2 b(o.x + side, o.y + side);
  d->AddRectFilled(o, b, C(TK_BLACK));
  Nearest(d, true);
  d->AddImage(g_big.p->GetTexRef(), o, b);
  Nearest(d, false);
  const float cs = side / 4.0f;
  const unsigned short *pNow = &n.t.vf4[static_cast<size_t>(nMega) * 16], *pWas = &cl.vf4[static_cast<size_t>(nMega) * 16];
  const unsigned short wGroup = n.t.cv5[static_cast<size_t>(g_nGroup) * MOD_GROUP_WORDS + 1];
  for (int c = 0; c < 16; ++c) {
    const ImVec2 ca(o.x + (c % 4) * cs, o.y + (c / 4) * cs);
    CellMark(d, ca, ImVec2(ca.x + cs, ca.y + cs), g_nLayer, pNow[c], pWas[c], wGroup, true);
  }
  for (int i = 1; i < 4; ++i) {
    d->AddLine(ImVec2(o.x + i * cs, o.y), ImVec2(o.x + i * cs, b.y), IM_COL32(0, 0, 0, 120), P(1.0f));
    d->AddLine(ImVec2(o.x, o.y + i * cs), ImVec2(b.x, o.y + i * cs), IM_COL32(0, 0, 0, 120), P(1.0f));
  }
  d->AddRect(o, b, C(TK_LINE2), 0.0f, 0, P(1.0f));
  // the brush: left paints the layer on, right off (low for height)
  ImGui::SetCursorScreenPos(o);
  ImGui::InvisibleButton("##big", ImVec2(side, side), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool bCan = StudioIsOpen() && nMega > 0 && g_nLayer != LAYER_BUILD;
  if (ImGui::IsItemHovered()) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    const int c = std::max(0, std::min(3, static_cast<int>((m.x - o.x) / cs))) + 4 * std::max(0, std::min(3, static_cast<int>((m.y - o.y) / cs)));
    static const char *const kH[] = {"low", "mid", "high"};
    ImGui::SetTooltip("Minitile %d: %s, %s ground%s%s\n%s", c, (pNow[c] & MOD_CELL_WALKABLE) ? "walkable" : "not walkable",
                      kH[HeightOf(pNow[c])], (pNow[c] & MOD_CELL_BLOCKS_VIEW) ? ", blocks view" : "",
                      pNow[c] != pWas[c] ? " (the mod's)" : "",
                      !bCan ? (nMega == 0 ? "The empty megatile is every group's unused place; it is not painted."
                                          : g_nLayer == LAYER_BUILD ? "Buildable is the group's: set it below." : "Open a mod to paint.")
                      : g_nLayer == LAYER_HEIGHT ? "Left paints the height picked below, right paints low."
                                                 : "Left paints it on, right paints it off.");
    if (bCan && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
      g_stroke.bOn = true;
      g_stroke.nMega = nMega;
      g_stroke.nLayer = g_nLayer;
      g_stroke.nId++;
      g_stroke.nLast = -1;
      g_stroke.nValue = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? (g_nLayer == LAYER_HEIGHT ? g_nBrushHeight : 1) : 0;
    }
    if (g_stroke.bOn && g_stroke.nMega == nMega && c != g_stroke.nLast) {
      g_stroke.nLast = c;
      PaintCell(nMega, c, g_stroke.nValue);
    }
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    g_stroke.bOn = false;
}

// A row of swatches for palette entries, each opening the palette at it.
void Swatches(ImDrawList *d, const Now &n, const std::vector<std::pair<int, int>> &cols, float W) {
  const float s = P(20.0f);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  int i = 0;
  for (const auto &c : cols) {
    const float x = o.x + i * (s + P(3.0f));
    if (x + s > o.x + W)
      break;
    const ImVec2 a(x, o.y);
    d->AddRectFilled(a, ImVec2(a.x + s, a.y + s), Rgb(&n.t.palette[c.second * 4]), P(2.0f));
    if (n.own.palette.count(c.second))
      d->AddRect(a, ImVec2(a.x + s, a.y + s), C(TK_CHANGED), P(2.0f), 0, P(1.5f));
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(c.second);
    if (ImGui::InvisibleButton("##sw", ImVec2(s, s))) {
      g_nColour = c.second;
      g_picked = {c.second};
      g_nView = VIEW_PALETTE;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Colour %d, %s: %d of the group's pixels", c.second, Hex(&n.t.palette[c.second * 4]).c_str(), c.first);
    ImGui::PopID();
    ++i;
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + s + P(6.0f)));
}

// A choice of three or four for a group's flag that may be left to each tile: the value now, or -1.
int Seg3(const char *pszId, const char *const *ppsz, int nCount, int nNow) {
  return StudioSeg(pszId, ppsz, nCount, nNow + 1, 24.0f, kStudioFsXs) - 1;
}

void TilesInspector(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Now &n = NowOf(g_nEra);
  const ModTileset &cl = ClassicOf(g_nEra).t;
  StudioPane(d, a, b, 34.0f);
  const int nMega = MegaOf(n.t, g_nGroup, g_nSlot);
  Wide(d, 10.5f, a.x + P(10.0f), Mid(a.y, P(34.0f), 10.5f), C(TK_HEAD), "Megatile");
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + P(34.0f)));
  ImGui::BeginChild("##tileinsp", ImVec2(b.x - a.x - 2, b.y - a.y - P(34.0f) - 1), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  ImDrawList *cd = ImGui::GetWindowDrawList();
  const float W = b.x - a.x - P(24.0f);
  const float x0 = ImGui::GetCursorScreenPos().x + P(11.0f);
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(10.0f)));
  if (nMega < 0) {
    StudioMediaPara("Pick a megatile in the groups at the left.", W, C(TK_INK3));
    ImGui::EndChild();
    return;
  }
  const auto own = n.own.megatiles.find(nMega);
  int nUses = 0;
  for (size_t g = 0; g < n.t.Groups(); ++g)
    for (int k = 0; k < 16; ++k)
      if (MegaOf(n.t, static_cast<int>(g), k) == nMega) {
        ++nUses;
        break;
      }
  StudioMediaHeading(("Megatile " + Num(nMega)).c_str(),
                     ("group " + Num(g_nGroup) + ", place " + Num(g_nSlot) + (nUses > 1 ? ", in " + Num(nUses) + " groups" : "")).c_str(), W);
  // the layer, and the brush's height
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  g_nLayer = StudioSeg("##layer", kLayers, LAYERS, g_nLayer, 26.0f, kStudioFsXs);
  Tip("The flags shown over the tiles and painted: a minitile's walkable, height and blocks view (.vf4),\n"
      "and the group's buildable (.cv5), set below. They change how a game with the mod plays.");
  // as large as the pane's height allows, the tools below it still in view
  const float side = std::min(W, std::max(P(160.0f), (b.y - a.y) * 0.42f));
  ImVec2 bo = ImGui::GetCursorScreenPos();
  bo = ImVec2(x0, bo.y + P(8.0f));
  BigMegatile(cd, bo, side, nMega);
  ImGui::SetCursorScreenPos(ImVec2(x0, bo.y + side + P(8.0f)));
  if (g_nLayer == LAYER_HEIGHT) {
    static const char *const kH[] = {"Low", "Mid", "High"};
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "The brush paints");
    ImGui::SameLine(0.0f, P(8.0f));
    g_nBrushHeight = StudioSeg("##bh", kH, 3, g_nBrushHeight, 24.0f, kStudioFsXs);
  } else {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3),
                g_nLayer == LAYER_BUILD ? "Buildable is the group's: below." : "Left paints on, right off; a stroke is one undo.");
  }
  if (nUses > 1)
    StudioMediaPara("Every group that places this megatile changes with it.", W, C(TK_INK3), kStudioFsXs);
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  // its picture
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  const bool bOpen = StudioIsOpen() && nMega > 0;
  const std::string forMega = "tiles.mega#" + StudioModId(), forGroup = "tiles.group#" + StudioModId();
  if (StudioButton("##pic", "Picture...", "image", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, bOpen))
    StudioPickFile(forMega.c_str(), "Pictures (32 by 32)", "png,webp,tga");
  Tip("A picture for this megatile, 32 by 32 or a multiple (the HD scale is averaged down), mapped into\n"
      "the palette; its clear pixels keep the classic tile's");
  ImGui::SameLine(0.0f, P(6.0f));
  if (StudioButton("##gpic", "Group's picture...", "image", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, StudioIsOpen()))
    StudioPickFile(forGroup.c_str(), "Pictures (512 by 32)", "png,webp,tga");
  Tip("A picture for the group's 16 megatiles in a row, 512 by 32 or a multiple");
  std::string picked;
  if (StudioPickedFile(forMega.c_str(), &picked))
    Ask("studio_tile_image", "\"megatile\":" + Num(nMega) + ",\"from\":" + Q(picked));
  if (StudioPickedFile(forGroup.c_str(), &picked))
    Ask("studio_tile_image", "\"group\":" + Num(g_nGroup) + ",\"from\":" + Q(picked));
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(4.0f)));
  if (StudioButton("##export", "Save as PNG", "save", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, StudioIsOpen() && nMega > 0)) {
    const std::string to = StudioDataFolder() + "tilesets\\" + kModTilesetNames[g_nEra] + "\\art\\megatile-" + Num(nMega) + ".png";
    Ask("studio_tile_export", "\"megatile\":" + Num(nMega) + ",\"to\":" + Q(to));
    StudioSay("written to " + to);
  }
  Tip("The megatile as it is now written to the mod's tilesets\\<tileset>\\art\\ folder, a start to draw over.\n"
      "An art folder is left out of the mod's package.");
  if (own != n.own.megatiles.end()) {
    ImGui::SameLine(0.0f, P(6.0f));
    if (!own->second.image.empty() &&
        StudioButton("##noimg", "Classic picture", "undo", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, true))
      Ask("studio_tile_image", "\"megatile\":" + Num(nMega) + ",\"revert\":true");
    ImGui::SameLine(0.0f, P(6.0f));
    if ((own->second.anMask[0] >= 0 || own->second.anMask[1] >= 0 || own->second.nHeightSet) &&
        StudioButton("##noflags", "Classic flags", "undo", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, true)) {
      StudioAsk("studio_tile_set", "\"tileset\":" + Q(kModTilesetNames[g_nEra]) + ",\"megatile\":" + Num(nMega) +
                                       ",\"field\":\"walkable\",\"revert\":true,\"merge\":\"tile-classic\"");
      StudioAsk("studio_tile_set", "\"tileset\":" + Q(kModTilesetNames[g_nEra]) + ",\"megatile\":" + Num(nMega) +
                                       ",\"field\":\"height\",\"revert\":true,\"merge\":\"tile-classic\"");
      StudioAsk("studio_tile_set", "\"tileset\":" + Q(kModTilesetNames[g_nEra]) + ",\"megatile\":" + Num(nMega) +
                                       ",\"field\":\"blocks_view\",\"revert\":true,\"merge\":\"tile-classic\"");
    }
  }
  if (own != n.own.megatiles.end() && !own->second.image.empty())
    StudioMediaPara("Its picture: " + own->second.image, W, C(TK_CHANGED), kStudioFsXs);

  // the group
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  const unsigned short *w = &n.t.cv5[static_cast<size_t>(g_nGroup) * MOD_GROUP_WORDS];
  const unsigned short *was = &cl.cv5[static_cast<size_t>(g_nGroup) * MOD_GROUP_WORDS];
  StudioMediaHeading(("Group " + Num(g_nGroup)).c_str(), ("kind " + Num(w[0])).c_str(), W);
  const PortJson &grec = OwnJson(g_nEra)["groups"][Num(g_nGroup).c_str()];
  const std::string gat = "\"group\":" + Num(g_nGroup) + ",";
  // its name, the modder's
  static char s_szName[64];
  static std::string s_nameOf;
  const std::string nameKey = StudioModId() + "#" + Num(g_nEra) + "#" + Num(g_nGroup) + "#" + grec["comment"].Str();
  if (s_nameOf != nameKey && !ImGui::IsAnyItemActive()) {
    snprintf(s_szName, sizeof s_szName, "%s", grec["comment"].Str().c_str());
    s_nameOf = nameKey;
  }
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  StudioInput("##gname", s_szName, sizeof s_szName, W / P(1.0f), 26.0f, "a name for the group (yours, the game reads none)");
  if (ImGui::IsItemDeactivatedAfterEdit() && StudioIsOpen())
    Ask("studio_tile_set", gat + "\"field\":\"comment\"," + (s_szName[0] ? "\"value\":" + Q(s_szName) : std::string("\"revert\":true")));
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  for (int f = 0; f < 2; ++f) {
    const int nBit = f == 0 ? MOD_GROUP_UNBUILDABLE : MOD_GROUP_CREEP;
    bool bOn = f == 0 ? !(w[1] & nBit) : (w[1] & nBit) != 0;
    const bool bWas = f == 0 ? !(was[1] & nBit) : (was[1] & nBit) != 0;
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
    const char *pszKey = f == 0 ? "buildable" : "creep";
    if (StudioCheck(f == 0 ? "##build" : "##creep", f == 0 ? "Buildable" : "Creep", &bOn) && StudioIsOpen())
      Ask("studio_tile_set", gat + "\"field\":\"" + pszKey + "\"," +
                                 (bOn == bWas ? std::string("\"revert\":true") : std::string("\"value\":") + (bOn ? "true" : "false")));
    Tip(f == 0 ? "Whether a building may stand on the group's tiles (classic .cv5's flag 0x80)"
               : "Whether the group's tiles are creep, which Zerg buildings stand on (.cv5's flag 0x40)");
    ImGui::SameLine(0.0f, P(10.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, bOn != bWas ? C(TK_CHANGED) : C(TK_INK3),
                bOn != bWas ? (std::string("the mod's; classic ") + (bWas ? "yes" : "no")).c_str() : "classic");
  }
  // the flags of all its megatiles at once
  static const char *const kAll[] = {"Each tile's", "All", "None"};
  static const char *const kHeights[] = {"Each tile's", "Low", "Mid", "High"};
  for (int f = 0; f < 3; ++f) {
    const char *const pszKey = f == 0 ? "walkable" : f == 1 ? "height" : "blocks_view";
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(2.0f)));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK2), f == 0 ? "Walkable" : f == 1 ? "Height" : "Blocks view");
    ImGui::SameLine(x0 - ImGui::GetWindowPos().x + P(92.0f));
    const PortJson &v = grec[pszKey];
    int nNow = -1;
    if (f == 1)
      nNow = v.kind == PortJson::STRING ? (v.s == "low" ? 0 : v.s == "mid" ? 1 : 2) : -1;
    else
      nNow = v.kind == PortJson::BOOL ? (v.b ? 0 : 1) : -1;
    ImGui::PushID(f);
    const int nNew = f == 1 ? Seg3("##gh", kHeights, 4, nNow) : Seg3("##gf", kAll, 3, nNow);
    ImGui::PopID();
    Tip("The flag for every minitile of the group's megatiles; a megatile's own painting comes after it");
    if (nNew != nNow && StudioIsOpen()) {
      static const char *const kHW[] = {"\"low\"", "\"mid\"", "\"high\""};
      Ask("studio_tile_set", gat + "\"field\":\"" + pszKey + "\"," +
                                 (nNew < 0 ? std::string("\"revert\":true")
                                           : std::string("\"value\":") + (f == 1 ? kHW[nNew] : nNew == 0 ? "true" : "false")));
    }
  }
  // its colours, for a recolour
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK2), "Its colours, the most used first");
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(2.0f)));
  const auto cols = GroupColours(n.t, g_nGroup);
  Swatches(cd, n, cols, W);
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  if (StudioButton("##recol", "Recolour the group...", "pen", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, !cols.empty())) {
    g_picked.clear();
    SharedCheck();
    for (size_t i = 0; i < cols.size() && g_picked.size() < 12; ++i)
      if (!g_abShared[cols[i].second] && !ModTilesetCycled(g_nEra, cols[i].second))
        g_picked.insert(cols[i].second);
    g_nColour = g_picked.empty() ? -1 : *g_picked.begin();
    g_nView = VIEW_PALETTE;
  }
  Tip("The palette with the group's own colours picked (those no unit draws with, at most 12),\n"
      "to move their hue, saturation and brightness together");
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  ImGui::EndChild();
}

// ---- the palette ---------------------------------------------------------------------------------------

float g_fHue = 0.0f, g_fSat = 1.0f, g_fVal = 1.0f;

void PaletteGrid(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Now &n = NowOf(g_nEra);
  const ModTileset &cl = ClassicOf(g_nEra).t;
  SharedCheck();
  StudioPane(d, a, b, 34.0f);
  float x = a.x + P(10.0f);
  x += Wide(d, 10.5f, x, Mid(a.y, P(34.0f), 10.5f), C(TK_HEAD), "Palette") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, P(34.0f), kStudioFsXs), C(TK_INK3),
    "256 colours, the cycling ones moving as the game moves them. Click picks one, Ctrl+click adds, Shift+click a run.",
    b.x - x - P(10.0f));
  const std::vector<unsigned char> pal = Cycled(n.t.palette, g_nEra, Tick(), nullptr);
  const float top = a.y + P(44.0f);
  // 16 by 16, or 32 by 8 in a wide pane, whichever gives the larger swatches
  const float fW = b.x - a.x - P(24.0f), fH = b.y - top - P(64.0f);
  const int nCols = std::min(fW / 32.0f, fH / 8.0f) > std::min(fW / 16.0f, fH / 16.0f) ? 32 : 16;
  const float s = floorf(std::min(fW / nCols, fH / (256 / nCols)));
  const ImVec2 o(a.x + P(12.0f), top);
  static int s_nAnchor = 0;
  for (int i = 0; i < 256; ++i) {
    const ImVec2 ca(o.x + (i % nCols) * s, o.y + (i / nCols) * s), cb(ca.x + s - P(2.0f), ca.y + s - P(2.0f));
    d->AddRectFilled(ca, cb, Rgb(&pal[i * 4]), P(2.0f));
    if (n.own.palette.count(i))
      d->AddTriangleFilled(ImVec2(cb.x - s * 0.34f, ca.y), ImVec2(cb.x, ca.y), ImVec2(cb.x, ca.y + s * 0.34f), C(TK_CHANGED));
    if (g_abShared[i])
      d->AddCircleFilled(ImVec2(ca.x + P(5.0f), cb.y - P(5.0f)), P(2.2f), IM_COL32(255, 255, 255, 150));
    if (g_picked.count(i))
      d->AddRect(ImVec2(ca.x - P(1.0f), ca.y - P(1.0f)), ImVec2(cb.x + P(1.0f), cb.y + P(1.0f)), C(TK_HI), P(2.0f), 0, P(2.0f));
    ImGui::SetCursorScreenPos(ca);
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##e", ImVec2(s - P(2.0f), s - P(2.0f)))) {
      if (ImGui::GetIO().KeyShift) {
        for (int k = std::min(s_nAnchor, i); k <= std::max(s_nAnchor, i); ++k)
          g_picked.insert(k);
      } else if (ImGui::GetIO().KeyCtrl) {
        if (!g_picked.erase(i))
          g_picked.insert(i);
        s_nAnchor = i;
      } else {
        g_picked = {i};
        s_nAnchor = i;
      }
      g_nColour = i;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Colour %d: %s%s\n%s%s%u of the tileset's minitile pixels", i, Hex(&n.t.palette[i * 4]).c_str(),
                        n.own.palette.count(i) ? (", the mod's; classic " + Hex(&cl.palette[i * 4])).c_str() : "",
                        g_abShared[i] ? "Every tileset has it alike: units draw with it too.\n" : "",
                        ModTilesetCycled(g_nEra, i) ? "The game cycles it; no picture is mapped onto it.\n" : "",
                        ClassicOf(g_nEra).uses[i]);
    ImGui::PopID();
  }
  // the cycles' ranges, outlined
  for (const ModTilesetCycle &c : ModTilesetCycles(g_nEra)) {
    for (int i = c.nLo; i <= c.nHi; ++i) {
      const ImVec2 ca(o.x + (i % nCols) * s, o.y + (i / nCols) * s);
      d->AddLine(ImVec2(ca.x, ca.y + s - P(1.0f)), ImVec2(ca.x + s - P(2.0f), ca.y + s - P(1.0f)),
                 c.bStarted ? IM_COL32(120, 200, 255, 230) : IM_COL32(120, 200, 255, 110), P(2.0f));
    }
  }
  // the legend
  const float ly = o.y + (256 / nCols) * s + P(10.0f);
  float lx = o.x;
  d->AddTriangleFilled(ImVec2(lx, ly), ImVec2(lx + P(9.0f), ly), ImVec2(lx + P(9.0f), ly + P(9.0f)), C(TK_CHANGED));
  lx += P(14.0f) + T(d, STUDIO_FONT_TEXT, kStudioFsXs, lx + P(14.0f), ly - P(2.0f), C(TK_INK2), "the mod's colour") + P(18.0f);
  d->AddCircleFilled(ImVec2(lx + P(4.0f), ly + P(4.0f)), P(2.6f), IM_COL32(255, 255, 255, 170));
  lx += P(14.0f) + T(d, STUDIO_FONT_TEXT, kStudioFsXs, lx + P(14.0f), ly - P(2.0f), C(TK_INK2), "every tileset's, units draw with it") + P(18.0f);
  d->AddLine(ImVec2(lx, ly + P(8.0f)), ImVec2(lx + P(14.0f), ly + P(8.0f)), IM_COL32(120, 200, 255, 230), P(2.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, lx + P(20.0f), ly - P(2.0f), C(TK_INK2), "cycles in the game");
}

void HsvShift(const unsigned char *p, float fHue, float fSat, float fVal, unsigned char *out) {
  float h, s, v;
  ImGui::ColorConvertRGBtoHSV(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, h, s, v);
  h = fmodf(h + fHue / 360.0f + 1.0f, 1.0f);
  s = std::max(0.0f, std::min(1.0f, s * fSat));
  v = std::max(0.0f, std::min(1.0f, v * fVal));
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
  out[0] = static_cast<unsigned char>(r * 255.0f + 0.5f);
  out[1] = static_cast<unsigned char>(g * 255.0f + 0.5f);
  out[2] = static_cast<unsigned char>(b * 255.0f + 0.5f);
}

void PaletteInspector(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Now &n = NowOf(g_nEra);
  const ModTileset &cl = ClassicOf(g_nEra).t;
  StudioPane(d, a, b, 34.0f);
  Wide(d, 10.5f, a.x + P(10.0f), Mid(a.y, P(34.0f), 10.5f), C(TK_HEAD), g_picked.size() > 1 ? "Colours" : "Colour");
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + P(34.0f)));
  ImGui::BeginChild("##palinsp", ImVec2(b.x - a.x - 2, b.y - a.y - P(34.0f) - 1), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  ImDrawList *cd = ImGui::GetWindowDrawList();
  const float W = b.x - a.x - P(24.0f);
  const float x0 = ImGui::GetCursorScreenPos().x + P(11.0f);
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(10.0f)));
  SharedCheck();
  if (g_picked.empty()) {
    StudioMediaPara("Pick a colour of the palette, or the group's colours from its megatile's view.", W, C(TK_INK3));
  } else if (g_picked.size() == 1) {
    const int i = *g_picked.begin();
    StudioMediaHeading(("Colour " + Num(i)).c_str(), (Hex(&n.t.palette[i * 4]) + (n.own.palette.count(i) ? ", the mod's" : ", classic")).c_str(), W);
    ImVec2 o = ImGui::GetCursorScreenPos();
    o.x = x0;
    const float sw = (W - P(10.0f)) / 2.0f;
    cd->AddRectFilled(o, ImVec2(o.x + sw, o.y + P(46.0f)), Rgb(&n.t.palette[i * 4]), P(3.0f));
    cd->AddRectFilled(ImVec2(o.x + sw + P(10.0f), o.y), ImVec2(o.x + W, o.y + P(46.0f)), Rgb(&cl.palette[i * 4]), P(3.0f));
    T(cd, STUDIO_FONT_TEXT, kStudioFsXs, o.x, o.y + P(50.0f), C(TK_INK3), "now");
    T(cd, STUDIO_FONT_TEXT, kStudioFsXs, o.x + sw + P(10.0f), o.y + P(50.0f), C(TK_INK3), "classic " + Hex(&cl.palette[i * 4]));
    ImGui::SetCursorScreenPos(ImVec2(x0, o.y + P(70.0f)));
    float rgb[3] = {n.t.palette[i * 4] / 255.0f, n.t.palette[i * 4 + 1] / 255.0f, n.t.palette[i * 4 + 2] / 255.0f};
    ImGui::SetNextItemWidth(W);
    if (ImGui::ColorPicker3("##pick", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayHex |
                                               ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_PickerHueBar) &&
        StudioIsOpen()) {
      const unsigned v = static_cast<unsigned>(rgb[0] * 255.0f + 0.5f) << 16 | static_cast<unsigned>(rgb[1] * 255.0f + 0.5f) << 8 |
                         static_cast<unsigned>(rgb[2] * 255.0f + 0.5f);
      Ask("studio_tile_set", "\"colour\":" + Num(i) + ",\"value\":" + Q(Hex(v)) + ",\"merge\":" + Q("tile-colour-" + Num(i)));
    }
    ImGui::Dummy(ImVec2(1, P(6.0f)));
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
    if (StudioButton("##classic", "Classic colour", "undo", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, n.own.palette.count(i) > 0))
      Ask("studio_tile_set", "\"colour\":" + Num(i) + ",\"revert\":true");
    std::string about = Num(static_cast<int>(ClassicOf(g_nEra).uses[i])) + " of the tileset's minitile pixels draw with it.";
    if (g_abShared[i])
      about += " Every tileset has it alike, and units draw with it: a change shows on them too in this tileset's games.";
    if (ModTilesetCycled(g_nEra, i))
      about += " The game cycles it (its water, lava or lights), and a picture is never mapped onto it.";
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(6.0f)));
    StudioMediaPara(about, W, C(TK_INK2), kStudioFsXs);
  } else {
    StudioMediaHeading((Num(static_cast<int>(g_picked.size())) + " colours").c_str(), "moved together", W);
    // the picked, now and as the shift makes them
    ImVec2 o = ImGui::GetCursorScreenPos();
    o.x = x0;
    const float s = std::min(P(22.0f), (W - P(2.0f)) / std::max<size_t>(8, g_picked.size()));
    int k = 0;
    bool bUnits = false;
    for (int i : g_picked) {
      unsigned char shifted[3];
      HsvShift(&n.t.palette[i * 4], g_fHue, g_fSat, g_fVal, shifted);
      const ImVec2 ca(o.x + k * s, o.y);
      cd->AddRectFilled(ca, ImVec2(ca.x + s - P(2.0f), ca.y + s), Rgb(&n.t.palette[i * 4]));
      cd->AddRectFilled(ImVec2(ca.x, ca.y + s + P(2.0f)), ImVec2(ca.x + s - P(2.0f), ca.y + 2 * s + P(2.0f)),
                        IM_COL32(shifted[0], shifted[1], shifted[2], 255));
      bUnits = bUnits || g_abShared[i];
      if (++k * s + s > W)
        break;
    }
    ImGui::SetCursorScreenPos(ImVec2(x0, o.y + 2 * s + P(12.0f)));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "now, and as they would be");
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(6.0f)));
    StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
    ImGui::SetNextItemWidth(W - P(90.0f));
    ImGui::SliderFloat("Hue", &g_fHue, -180.0f, 180.0f, "%.0f degrees");
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
    ImGui::SetNextItemWidth(W - P(90.0f));
    ImGui::SliderFloat("Saturation", &g_fSat, 0.0f, 2.0f, "x %.2f");
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
    ImGui::SetNextItemWidth(W - P(90.0f));
    ImGui::SliderFloat("Brightness", &g_fVal, 0.0f, 2.0f, "x %.2f");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(6.0f)));
    const bool bAny = g_fHue != 0.0f || g_fSat != 1.0f || g_fVal != 1.0f;
    if (StudioButton("##apply", "Apply to the picked", "done", SB_GOOD, 26.0f, 0.0f, kStudioFsXs, bAny && StudioIsOpen())) {
      std::string cols;
      for (int i : g_picked) {
        unsigned char sh[3];
        HsvShift(&n.t.palette[i * 4], g_fHue, g_fSat, g_fVal, sh);
        const unsigned v = static_cast<unsigned>(sh[0]) << 16 | static_cast<unsigned>(sh[1]) << 8 | sh[2];
        const bool bClassic = memcmp(sh, &cl.palette[i * 4], 3) == 0;
        cols += std::string(cols.empty() ? "" : ",") + Q(Num(i)) + ":" + (bClassic ? std::string("null") : Q(Hex(v)));
      }
      Ask("studio_tile_set", "\"colours\":{" + cols + "}");
      g_fHue = 0.0f, g_fSat = 1.0f, g_fVal = 1.0f;
    }
    ImGui::SameLine(0.0f, P(6.0f));
    if (StudioButton("##reset", "Reset", "again", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, bAny))
      g_fHue = 0.0f, g_fSat = 1.0f, g_fVal = 1.0f;
    ImGui::SameLine(0.0f, P(6.0f));
    bool bMine = false;
    for (int i : g_picked)
      bMine = bMine || n.own.palette.count(i) > 0;
    if (StudioButton("##classics", "Classic colours", "undo", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, bMine && StudioIsOpen())) {
      std::string cols;
      for (int i : g_picked)
        if (n.own.palette.count(i))
          cols += std::string(cols.empty() ? "" : ",") + Q(Num(i)) + ":null";
      Ask("studio_tile_set", "\"colours\":{" + cols + "}");
    }
    if (bUnits) {
      ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(6.0f)));
      StudioMediaPara("Some picked colours are every tileset's, and units draw with them: their change shows on units too.", W,
                      C(TK_WARN), kStudioFsXs);
    }
  }
  // the cycles
  ImGui::Dummy(ImVec2(1, P(12.0f)));
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
  StudioMediaHeading("Cycles", "the ranges the game moves", W);
  for (const ModTilesetCycle &c : ModTilesetCycles(g_nEra)) {
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
    char sz[160];
    snprintf(sz, sizeof sz, "%d to %d, %s%s", c.nLo, c.nHi,
             c.bTable ? "from a table of the game's" : (std::string("a step each ") + Num(c.nRate) + " frames").c_str(),
             c.bStarted ? "" : ", not started with the map");
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, c.bStarted ? C(TK_INK2) : C(TK_INK3), sz);
  }
  ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + P(4.0f)));
  StudioMediaPara("A cycled colour may be changed; it keeps cycling in its range.", W, C(TK_INK3), kStudioFsXs);
  ImGui::EndChild();
}

// ---- the patch of terrain, painted from the groups ------------------------------------------------------

struct Patch {
  int nEra = -1, cols = 0, rows = 0;
  std::vector<int> groups;
};
Patch g_patchCells;

unsigned Hash(int x, int y) {
  unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  return h ^ (h >> 15);
}

// a cell's megatile: one of its group's that is not empty, by where it is
int PatchMega(const ModTileset &t, int nGroup, int x, int y) {
  int megas[16], n = 0;
  for (int k = 0; k < 16; ++k) {
    const int m = MegaOf(t, nGroup, k);
    if (m > 0)
      megas[n++] = m;
  }
  return n ? megas[Hash(x, y) % n] : 0;
}

void PatchPane(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Now &n = NowOf(g_nEra);
  StudioPane(d, a, b, 34.0f);
  float x = a.x + P(10.0f);
  x += Wide(d, 10.5f, x, Mid(a.y, P(34.0f), 10.5f), C(TK_HEAD), "Preview") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, P(34.0f), kStudioFsXs), C(TK_INK3),
    "Left paints the group picked at the left, right picks a cell's group. The cycles move.", b.x - x - P(170.0f));
  ImGui::SetCursorScreenPos(ImVec2(b.x - P(8.0f) - StudioButtonSize("Fill", "again", 24.0f, kStudioFsXs).x, a.y + P(5.0f)));
  const bool bFill = StudioButton("##fill", "Fill", "again", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs);
  Tip("The whole patch painted with the group picked");
  const float top = a.y + P(42.0f);
  const int unit = static_cast<int>(floorf(P(32.0f)));
  const int scale = (b.y - top - P(8.0f)) >= unit * 2 * 6 ? 2 : 1;
  const int ts = unit * scale;
  const int cols = std::max(1, static_cast<int>((b.x - a.x - P(20.0f)) / ts));
  const int rows = std::max(1, static_cast<int>((b.y - top - P(8.0f)) / ts));
  Patch &p = g_patchCells;
  if (p.nEra != g_nEra || p.cols != cols || p.rows != rows || bFill) {
    std::vector<int> old = p.groups;
    const int oc = p.cols, orows = p.rows;
    const bool bKeep = p.nEra == g_nEra && !bFill;
    p.nEra = g_nEra, p.cols = cols, p.rows = rows;
    p.groups.assign(static_cast<size_t>(cols) * rows, g_nGroup);
    if (bKeep)
      for (int y = 0; y < std::min(rows, orows); ++y)
        for (int xx = 0; xx < std::min(cols, oc); ++xx)
          p.groups[static_cast<size_t>(y) * cols + xx] = old[static_cast<size_t>(y) * oc + xx];
  }
  const ImVec2 o(a.x + P(10.0f), top);
  const int pw = cols * 32, ph = rows * 32;
  std::string key;
  const std::vector<unsigned char> pal = Cycled(n.t.palette, g_nEra, Tick(), &key);
  key += "|" + Num(static_cast<int>(n.nSerial)) + "|" + Num(cols) + "x" + Num(rows);
  for (int gIdx : p.groups)
    key += "," + Num(gIdx);
  if (g_patch.key != key || !g_patch.p) {
    std::vector<unsigned char> rgba(static_cast<size_t>(pw) * ph * 4, 0);
    for (int y = 0; y < rows; ++y)
      for (int xx = 0; xx < cols; ++xx)
        PaintMega(n.t, pal.data(), PatchMega(n.t, p.groups[static_cast<size_t>(y) * cols + xx], xx, y),
                  &rgba[(static_cast<size_t>(y) * 32 * pw + xx * 32) * 4], pw);
    TexSet(&g_patch, rgba, pw, ph);
    g_patch.key = key;
  }
  const ImVec2 pb(o.x + cols * ts, o.y + rows * ts);
  Nearest(d, true);
  d->AddImage(g_patch.p->GetTexRef(), o, pb);
  Nearest(d, false);
  if (g_bOverlay) {
    const float cs = ts / 4.0f;
    for (int y = 0; y < rows; ++y)
      for (int xx = 0; xx < cols; ++xx) {
        const int g = p.groups[static_cast<size_t>(y) * cols + xx];
        const int m = PatchMega(n.t, g, xx, y);
        if (m <= 0 || static_cast<size_t>(m) >= n.t.Megatiles())
          continue;
        const unsigned short *cells = &n.t.vf4[static_cast<size_t>(m) * 16];
        const unsigned short wg = n.t.cv5[static_cast<size_t>(g) * MOD_GROUP_WORDS + 1];
        for (int c = 0; c < 16; ++c) {
          const ImVec2 ca(o.x + xx * ts + (c % 4) * cs, o.y + y * ts + (c / 4) * cs);
          CellMark(d, ca, ImVec2(ca.x + cs, ca.y + cs), g_nLayer, cells[c], cells[c], wg, false);
        }
      }
  }
  d->AddRect(o, pb, C(TK_LINE2));
  ImGui::SetCursorScreenPos(o);
  ImGui::InvisibleButton("##patch", ImVec2(pb.x - o.x, pb.y - o.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  if (ImGui::IsItemHovered()) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    const int cx = std::max(0, std::min(cols - 1, static_cast<int>((m.x - o.x) / ts)));
    const int cy = std::max(0, std::min(rows - 1, static_cast<int>((m.y - o.y) / ts)));
    int &cell = p.groups[static_cast<size_t>(cy) * cols + cx];
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
      cell = g_nGroup;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      g_nGroup = cell;
      g_nSlot = 0;
      for (int k = 0; k < 16; ++k)
        if (MegaOf(n.t, cell, k) == PatchMega(n.t, cell, cx, cy))
          g_nSlot = k;
      g_bScrollTo = true;
    }
    d->AddRect(ImVec2(o.x + cx * ts, o.y + cy * ts), ImVec2(o.x + (cx + 1) * ts, o.y + (cy + 1) * ts), C(TK_HI, 0.8f));
    ImGui::SetTooltip("Group %d, megatile %d", cell, PatchMega(n.t, cell, cx, cy));
  }
}

// ---- what the mod changed --------------------------------------------------------------------------------

void ChangesPane(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Now &n = NowOf(g_nEra);
  StudioPane(d, a, b, 34.0f);
  const std::vector<Change> changes = ChangesOf(g_nEra);
  float x = a.x + P(10.0f);
  x += Wide(d, 10.5f, x, Mid(a.y, P(34.0f), 10.5f), C(TK_HEAD), "Changes") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(a.y, P(34.0f), kStudioFsXs), C(TK_INK3),
    changes.empty() ? "none yet" : Num(static_cast<int>(changes.size())) + " in " + kShown[g_nEra] + "; x puts one back",
    b.x - x - P(8.0f));
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + P(34.0f)));
  ImGui::BeginChild("##changes", ImVec2(b.x - a.x - 2, b.y - a.y - P(34.0f) - 1), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  ImDrawList *cd = ImGui::GetWindowDrawList();
  const float W = b.x - a.x - P(16.0f);
  if (!n.error.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(a.x + P(10.0f), ImGui::GetCursorScreenPos().y + P(6.0f)));
    StudioMediaPara("The loader does not take it: " + n.error, W - P(8.0f), C(TK_BAD), kStudioFsXs);
  }
  const float rh = P(26.0f);
  for (size_t i = 0; i < changes.size(); ++i) {
    const Change &c = changes[i];
    ImGui::PushID(static_cast<int>(i));
    const ImVec2 o(a.x + P(8.0f), ImGui::GetCursorScreenPos().y);
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("##row", ImVec2(W - P(30.0f), rh));
    const bool bHover = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
      if (c.nColour >= 0) {
        g_nColour = c.nColour;
        g_picked = {c.nColour};
        g_nView = VIEW_PALETTE;
      } else if (c.nGroup >= 0) {
        g_nGroup = c.nGroup, g_nSlot = 0, g_bScrollTo = true, g_nView = VIEW_TILES;
      } else if (c.nMega >= 0) {
        for (size_t g = 0; g < n.t.Groups(); ++g)
          for (int k = 0; k < 16; ++k)
            if (MegaOf(n.t, static_cast<int>(g), k) == c.nMega) {
              g_nGroup = static_cast<int>(g), g_nSlot = k, g_bScrollTo = true;
              g = n.t.Groups();
              break;
            }
        g_nView = VIEW_TILES;
      }
    }
    if (bHover)
      cd->AddRectFilled(o, ImVec2(o.x + W, o.y + rh), C(TK_ROW_HI), P(3.0f));
    float tx = o.x + P(6.0f);
    if (c.nColour >= 0 && c.nColour < 256) {
      cd->AddRectFilled(ImVec2(tx, o.y + P(6.0f)), ImVec2(tx + P(14.0f), o.y + rh - P(6.0f)), Rgb(&n.t.palette[c.nColour * 4]), P(2.0f));
      tx += P(20.0f);
    } else {
      StudioGlyph(cd, c.field == "image" ? "image" : "pen", ImVec2(tx, o.y + (rh - P(14.0f)) / 2.0f), P(14.0f), C(TK_INK3));
      tx += P(20.0f);
    }
    T(cd, STUDIO_FONT_TEXT, kStudioFsXs, tx, Mid(o.y, rh, kStudioFsXs), C(TK_INK), c.text, o.x + W - P(34.0f) - tx);
    ImGui::SetCursorScreenPos(ImVec2(o.x + W - P(26.0f), o.y + P(2.0f)));
    if (StudioButton("##x", "", "x", SB_FLAT, 22.0f, 22.0f, kStudioFsXs, StudioIsOpen())) {
      std::string at = c.part == "palette" ? "\"colour\":" + c.n : std::string(c.part == "groups" ? "\"group\":" : "\"megatile\":") + c.n;
      Ask("studio_tile_set", at + (c.field.empty() ? std::string() : ",\"field\":" + Q(c.field)) + ",\"revert\":true");
    }
    Tip("Put this one back as classic has it (an edit, which undo takes back)");
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rh));
    ImGui::Dummy(ImVec2(W, 0.0f));
    ImGui::PopID();
  }
  if (changes.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(a.x + P(10.0f), ImGui::GetCursorScreenPos().y + P(8.0f)));
    StudioMediaPara("Paint a megatile's flags, set a group's, bring in a picture or change a colour; each change is listed here, "
                    "and Ctrl+Z takes back the last.",
                    W - P(8.0f), C(TK_INK3), kStudioFsXs);
  }
  ImGui::EndChild();
}

void Workspace(ImVec2 a, ImVec2 b) {
  TexTick();
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = b.x - a.x - P(32.0f);
  const ImVec2 o(a.x + P(16.0f), a.y + P(10.0f));
  const Classic &cl = ClassicOf(g_nEra);
  Bar(d, o, W);
  float y = o.y + P(32.0f) + P(8.0f);
  if (!cl.bOk) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, o.x, y + P(10.0f), C(TK_BAD), cl.why, W);
    return;
  }
  if (!StudioIsOpen())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, y - P(4.0f), C(TK_INK3), "Open a mod to change its tileset; the classic one is shown.", W);
  const float right = std::min(P(440.0f), W * 0.38f);
  const float bottomH = std::max(P(220.0f), std::min(P(300.0f), (b.y - y) * 0.34f));
  const float midB = b.y - P(10.0f) - bottomH - P(10.0f);
  const ImVec2 la(o.x, y), lb(o.x + W - right - P(10.0f), midB);
  const ImVec2 ra(lb.x + P(10.0f), y), rb(o.x + W, midB);
  if (g_nView == VIEW_TILES) {
    Grid(la, lb);
    TilesInspector(ra, rb);
  } else {
    PaletteGrid(la, lb);
    PaletteInspector(ra, rb);
  }
  const ImVec2 pa(o.x, midB + P(10.0f)), pb(o.x + W - right - P(10.0f), b.y - P(10.0f));
  PatchPane(pa, pb);
  ChangesPane(ImVec2(pb.x + P(10.0f), pa.y), ImVec2(o.x + W, pb.y));
}

}  // namespace

void StudioTilesetsWorkspace(ImVec2 a, ImVec2 b) {
  ImGui::PushID("##tilesets");
  Workspace(a, b);
  ImGui::PopID();
}

void StudioTilesetsInit(void) {
  PortControlCommand("studio_tilesets", CmdTilesets);
  PortControlCommand("studio_tileset", CmdTileset);
  PortControlCommand("studio_tile_set", CmdTileSet);
  PortControlCommand("studio_tile_image", CmdTileImage);
  PortControlCommand("studio_tile_export", CmdTileExport);
  PortControlCommand("studio_tilesets_show", CmdShow);
}
