// studio_art.cpp -- the Art workspace, a unit's Art view, an image's, a sprite's and a flingy's heads,
// the image script's editor, and a unit's picture: its frames decoded and drawn in a player's colour.
//
// A unit is drawn through its chain, unit -> flingy -> sprite -> image, each a record of the mod or a
// classic row. An image's frames are a classic GRP from the player's archives (images.tbl names it),
// a GRP file of the mod's, or a picture sheet sliced by the image's sheet fields (docs/mod-data.md,
// "Art sheets"). The frames are made RGBA in classic's colours (badlands' palette, tunit.pcx's
// player colours) and drawn as Dear ImGui textures, kept per (image, frame, colour) and dropped when
// unused for a while. Facings past 16 are drawn mirrored as the game draws them; a 32-direction
// sheet has its own.
//
// The image script's editor is studio_art_script.cpp. Its commands (StudioArtInit below): studio_unit_art gives a unit art of its own, studio_art_sheet
// gives an image a sheet and its slicing, studio_art_frame says what a picture is made of.
#define NOMINMAX
#include "../h/platform.h"
#include "../h/Storm.h"  // SFileLoadFile
#include "../port/data/port_stormapi.h"  // SFileUnloadFile
#include <sys/stat.h>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "imgui_internal.h"  // RegisterUserTexture
#include "../port/data/port_imagefile.h"
#include "../port/data/port_iscript.h"
#include "../port/platform/port_util.h"  // PortReadFile
#include "../port/tools/port_control.h"
#include "studio_draw.h"

// The image script's part (studio_art_script.cpp): the frame a script of the mod's text, or of a mod
// it depends on, shows first, by its Name; the script editor in a rectangle.
namespace studio_art {
int ScriptFirstFrame(const std::string &name);
void ScriptPane(ImVec2 a, ImVec2 b);
// the animation player (studio_art_anim.cpp): its pane for an image W wide at the cursor; an
// animation's ticks in words for studio_art_anim
void AnimPanel(const std::string &image, float W);
bool AnimDescribe(const std::string &image, const std::string &anim, int nFacing, std::string *pAnswer);
void AnimSet(const std::string &anim, int nTick, int nPlaying);
bool AnimAsIf(const std::string &key, int nChoice);
}  // namespace studio_art

namespace {

using namespace studio_draw;

// ---- classic's files and colours -------------------------------------------------------------------

bool GameFile(const char *pszName, std::vector<unsigned char> *pOut) {
  void *p = nullptr;
  DWORD cb = 0;
  pOut->clear();
  if (!SFileLoadFile(pszName, &p, &cb, 0, nullptr) || p == nullptr)
    return false;
  pOut->assign(static_cast<unsigned char *>(p), static_cast<unsigned char *>(p) + cb);
  SFileUnloadFile(p);
  return true;
}

// The colours units are drawn in: badlands' palette, whose 16 to 199 every tileset shares, and the
// players' eight steps each (tunit.pcx's first row, indexes into the palette), brightest first.
struct Colours {
  bool bTried = false, bOk = false;
  unsigned char pal[256][3] = {};
  unsigned char team[16][8][3] = {};
};

const Colours &ColoursOf(void) {
  static Colours c;
  if (c.bTried)
    return c;
  c.bTried = true;
  std::vector<unsigned char> wpe, pcx;
  if (!GameFile("tileset\\badlands.wpe", &wpe) || wpe.size() < 1024 || !GameFile("game\\tunit.pcx", &pcx) || pcx.size() < 129)
    return c;
  for (int i = 0; i < 256; ++i)
    memcpy(c.pal[i], &wpe[4 * i], 3);
  // the PCX's first row: a byte of 0xC0 or more repeats the next (its low 6 bits) times
  size_t at = 128;
  for (int n = 0; n < 128 && at < pcx.size();) {
    int nRun = 1, v = pcx[at++];
    if ((v & 0xC0) == 0xC0 && at < pcx.size())
      nRun = v & 0x3F, v = pcx[at++];
    for (; nRun > 0 && n < 128; --nRun, ++n)
      memcpy(c.team[n / 8][n % 8], c.pal[v], 3);
  }
  c.bOk = true;
  return c;
}

ImU32 PlayerColour(int n) {
  const Colours &c = ColoursOf();
  static const ImU32 kStandIn[8] = {IM_COL32(244, 4, 4, 255),    IM_COL32(12, 72, 204, 255),  IM_COL32(44, 180, 148, 255),
                                    IM_COL32(136, 64, 156, 255), IM_COL32(248, 140, 20, 255), IM_COL32(112, 48, 20, 255),
                                    IM_COL32(204, 224, 208, 255), IM_COL32(252, 252, 56, 255)};
  n &= 7;
  if (!c.bOk)
    return kStandIn[n];
  return IM_COL32(c.team[n][0][0], c.team[n][0][1], c.team[n][0][2], 255);
}

const char *const kColourNames[8] = {"Red", "Blue", "Teal", "Purple", "Orange", "Brown", "White", "Yellow"};

// images.tbl's strings, a GRP's path under unit\ by an image's grp column (1-based)
const std::vector<std::string> &ImagesTbl(void) {
  static std::vector<std::string> s_tbl;
  static bool s_bTried = false;
  if (s_bTried)
    return s_tbl;
  s_bTried = true;
  std::vector<unsigned char> f;
  if (!GameFile("arr\\images.tbl", &f) || f.size() < 2)
    return s_tbl;
  const size_t n = f[0] | f[1] << 8;
  for (size_t i = 0; i < n && 2 + 2 * i + 1 < f.size(); ++i) {
    const size_t at = f[2 + 2 * i] | f[3 + 2 * i] << 8;
    std::string s;
    for (size_t k = at; k < f.size() && f[k] != 0; ++k)
      s += static_cast<char>(f[k]);
    s_tbl.push_back(s);
  }
  return s_tbl;
}

// ---- the first frame an image's script shows --------------------------------------------------------

// Classic iscript.bin: an opcode's argument bytes (-1 not known, -2 a byte count of words follows).
const signed char kOpArgs[0x45] = {
    2, 2, 1, 1, 2, 1, 2, 2, 4, 4, 2, 2, -1, 4, 4, 4,   // 00-0F
    4, 4, 2, 4, 4, 3, 0, 1, 2, -2, 4, 0, -2, 0, 3, 1,  // 10-1F
    1, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0,    // 20-2F
    0, 1, 0, 0, 1, 2, 0, 2, 1, 2, 4, 6, 6, 2, -1, 2,   // 30-3F
    2, 1, 4, -1, 0,                                    // 40-44
};

// The frame Init plays first, by the script's id in classic iscript.bin; 0 when it cannot be read.
int ClassicFirstFrame(int nScript) {
  static std::vector<unsigned char> s_bin;
  static std::map<int, int> s_first;
  static bool s_bTried = false;
  if (!s_bTried) {
    s_bTried = true;
    GameFile("scripts\\iscript.bin", &s_bin);
  }
  auto it = s_first.find(nScript);
  if (it != s_first.end())
    return it->second;
  int nFirst = 0;
  const std::vector<unsigned char> &b = s_bin;
  auto w = [&](size_t at) { return at + 1 < b.size() ? static_cast<size_t>(b[at] | b[at + 1] << 8) : 0u; };
  size_t at = w(0), nHead = 0;
  for (int i = 0; i < 2000 && at + 3 < b.size(); ++i, at += 4) {
    if (w(at) == 0xFFFF)
      break;
    if (static_cast<int>(w(at)) == nScript) {
      nHead = w(at + 2);
      break;
    }
  }
  if (nHead != 0 && nHead + 10 <= b.size() && memcmp(&b[nHead], "SCPE", 4) == 0) {
    size_t pc = w(nHead + 8);
    for (int nStep = 0; nStep < 256 && pc != 0 && pc < b.size(); ++nStep) {
      const int op = b[pc];
      if (op == 0x00 || op == 0x01) {
        nFirst = static_cast<int>(w(pc + 1));
        break;
      }
      if (op == 0x07) {  // goto
        pc = w(pc + 1);
        continue;
      }
      if (op == 0x16 || op == 0x36 || op >= 0x45 || kOpArgs[op] == -1)
        break;  // end, return, or what is not known
      pc += 1 + (kOpArgs[op] == -2 ? 1 + 2 * (pc + 1 < b.size() ? b[pc + 1] : 0) : kOpArgs[op]);
    }
  }
  s_first[nScript] = nFirst;
  return nFirst;
}

// ---- an image's art, as its records resolve ---------------------------------------------------------

// A field of a record of the mod or of its bases among the mod's records: the nearest that gives it.
const PortJson &ChainField(int t, const std::string &name, const char *pszKey, std::string *pFrom = nullptr) {
  static const PortJson kNone;
  std::string at = name;
  for (int i = 0; i < 16 && !at.empty(); ++i) {
    const PortJson &j = StudioRecordJson(t, at);
    if (j.kind == PortJson::NONE)
      break;
    if (j[pszKey].kind != PortJson::NONE) {
      if (pFrom)
        *pFrom = at;
      return j[pszKey];
    }
    at = j["base"].Str();
  }
  return kNone;
}

enum { ART_NONE, ART_CLASSIC, ART_GRP, ART_SHEET };

struct Art {
  int nKind = ART_NONE;
  std::string key;     // the frames' source, as the caches know it
  std::string file;    // in words: the archive's path or the data folder's
  std::string from;    // the record whose frames they are
  std::string why;     // why there are none
  PortJson grp;        // a sheet's file, files or levels
  std::string team;
  double fFrameW = 0, fFrameH = 0, fOriginX = -1, fOriginY = -1;
  int nDirs = 17, nFrames = 0, nScale = 1;
  bool bTurns = true;
  int nDraw = 0;       // the draw function: 10 a shadow
  int nFirst = 0;      // the frame its script's Init shows
  std::string script;  // the script, by its Name or number
};

bool IsMine(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && (r.kind == SK_OWN || r.kind == SK_EDIT);
}

bool IsOwn(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && r.kind == SK_OWN;
}

// What a frame let go of, freed at the next: a pane may hold a reference across an edit it asks for.
std::vector<std::map<std::string, Art>> g_retiredArt;

const Art &ArtOf(const std::string &image) {
  static std::map<std::string, Art> s_cache;
  static unsigned s_nChange = ~0u;
  static std::string s_mod;
  if (s_nChange != StudioChange() || s_mod != StudioModId()) {
    g_retiredArt.push_back(std::move(s_cache));
    s_cache.clear();
    s_nChange = StudioChange();
    s_mod = StudioModId();
  }
  auto it = s_cache.find(image);
  if (it != s_cache.end())
    return it->second;
  Art &a = s_cache[image];
  StudioRecord rec;
  if (!StudioRecordOf(ST_IMAGES, image, &rec)) {
    a.why = "no image " + image;
    return a;
  }
  a.bTurns = StudioNumber(ST_IMAGES, image, "turns", 1) != 0;
  a.nDraw = static_cast<int>(StudioNumber(ST_IMAGES, image, "draw_function"));
  // the script: a Name of the mod's text, else classic's by number
  const PortJson &script = ChainField(ST_IMAGES, image, "script");
  if (script.kind == PortJson::STRING) {
    a.script = script.s;
    a.nFirst = studio_art::ScriptFirstFrame(script.s);
  } else {
    const long long n = StudioNumber(ST_IMAGES, image, "script", -1);
    a.script = std::to_string(n);
    a.nFirst = n >= 0 && n < 1000 ? ClassicFirstFrame(static_cast<int>(n)) : 0;
  }
  std::string from;
  const PortJson &grp = ChainField(ST_IMAGES, image, "grp", &from);
  if (grp.kind != PortJson::NONE) {
    a.from = from;
    const std::string one = grp.kind == PortJson::STRING ? grp.s : std::string();
    if (!one.empty() && Lower(one).size() > 4 && Lower(one).compare(one.size() - 4, 4, ".grp") == 0) {
      a.nKind = ART_GRP;
      a.file = one;
      a.key = "g:" + one;
      if (!StudioFileExists(one))
        a.why = one + " is not in the data folder";
    } else {
      a.nKind = ART_SHEET;
      a.grp = grp;
      a.file = grp.kind == PortJson::STRING ? grp.s : PortJsonWrite(grp);
      a.team = ChainField(ST_IMAGES, image, "team").Str();
      const PortJson &f = ChainField(ST_IMAGES, image, "frame");
      if (f.kind == PortJson::ARRAY && f.items.size() >= 2) {
        a.fFrameW = f.items[0].Num(0);
        a.fFrameH = f.items[1].Num(0);
      } else if (f.kind == PortJson::NUMBER) {
        a.fFrameW = a.fFrameH = f.d;
      }
      const PortJson &o = ChainField(ST_IMAGES, image, "origin");
      if (o.kind == PortJson::ARRAY && o.items.size() >= 2) {
        a.fOriginX = o.items[0].Num(-1);
        a.fOriginY = o.items[1].Num(-1);
      }
      a.nDirs = static_cast<int>(ChainField(ST_IMAGES, image, "directions").Num(17)) == 32 ? 32 : 17;
      a.nFrames = static_cast<int>(ChainField(ST_IMAGES, image, "frames").Num(0));
      a.nScale = std::max(1, std::min(8, static_cast<int>(ChainField(ST_IMAGES, image, "scale").Num(1))));
      a.key = "s:" + PortJsonWrite(grp) + "|" + a.team + "|" + Fmt(a.fFrameW, 3) + "x" + Fmt(a.fFrameH, 3) + "|" +
              Fmt(a.fOriginX, 3) + "," + Fmt(a.fOriginY, 3) + "|" + std::to_string(a.nDirs) + "|" + std::to_string(a.nFrames) +
              "|" + std::to_string(a.nScale);
      if (a.fFrameW < 1 || a.fFrameH < 1)
        a.why = "the sheet has no frame size";
    }
    return a;
  }
  // classic frames: the grp column the image keeps, its base's
  const long long nGrp = StudioNumber(ST_IMAGES, image, "grp", 0);
  const std::vector<std::string> &tbl = ImagesTbl();
  if (nGrp <= 0 || nGrp > static_cast<long long>(tbl.size())) {
    a.why = tbl.empty() ? "the classic archives do not read" : "no frames";
    return a;
  }
  a.nKind = ART_CLASSIC;
  a.file = "unit\\" + tbl[static_cast<size_t>(nGrp - 1)];
  a.key = "c:" + std::to_string(nGrp);
  // the classic image whose frames these are, for the words: the first base that is classic
  std::string at = image;
  for (int i = 0; i < 16; ++i) {
    StudioRecord r;
    if (!StudioRecordOf(ST_IMAGES, at, &r) || r.kind == SK_CLASSIC || r.base.empty())
      break;
    at = r.base;
    if (isdigit(static_cast<unsigned char>(at[0])))
      if (const char *psz = StudioClassicName(ST_IMAGES, atoi(at.c_str())))
        at = psz;
  }
  a.from = at;
  return a;
}

// ---- the frames' sources ----------------------------------------------------------------------------

struct Rect {
  float l = 0, t = 0, r = 0, b = 0;  // game pixels from the origin
  bool Empty() const { return r <= l || b <= t; }
};

struct Source {
  bool bOk = false;
  std::string why;
  long long nTime = 0;    // the file's time when read (a mod's)
  uint64_t nChecked = 0;  // when it was last looked at
  uint64_t nUsed = 0;     // when it was last asked for
  // a GRP's
  std::vector<unsigned char> bytes;
  int gw = 0, gh = 0, nCount = 0;
  // a sheet's
  std::vector<unsigned char> rgba, team;
  int w = 0, h = 0, nScale = 1, cw = 0, ch = 0, nAcross = 0;
  double ox = 0, oy = 0;  // the origin in game pixels from a cell's corner
  std::vector<Rect> cells;  // each cell's drawn pixels, game pixels from the origin
};

long long FileTime(const std::string &path) {
  struct _stat64 st;
  return _stat64(path.c_str(), &st) == 0 ? static_cast<long long>(st.st_mtime) * 1000 + (st.st_size & 0xFFF) : -1;
}

struct GrpFrame {
  int x = 0, y = 0, w = 0, h = 0;
  size_t off = 0;
};

bool GrpFrameAt(const Source &s, int i, GrpFrame *pOut) {
  const size_t at = 6 + static_cast<size_t>(i) * 8;
  if (i < 0 || i >= s.nCount || at + 8 > s.bytes.size())
    return false;
  const unsigned char *p = &s.bytes[at];
  DWORD dwOff;
  memcpy(&dwOff, p + 4, sizeof dwOff);
  pOut->x = p[0];
  pOut->y = p[1];
  pOut->w = p[2] + ((dwOff & 0x80000000u) ? 256 : 0);
  pOut->h = p[3];
  pOut->off = dwOff & 0x7FFFFFFFu;
  return pOut->off < s.bytes.size();
}

// A sheet's file or files: one picture, a list stacked top to bottom, or levels by scale of which
// the smallest is taken.
bool ReadSheet(const PortJson &grp, int nScaleField, Source *pS, long long *pnTime) {
  std::vector<std::string> files;
  int nScale = nScaleField;
  const PortJson *pFiles = &grp;
  if (grp.kind == PortJson::OBJECT && !grp.members.empty()) {
    int nBest = 1 << 30;
    for (const PortJsonMember &m : grp.members)
      if (atoi(m.first.c_str()) > 0 && atoi(m.first.c_str()) < nBest) {
        nBest = atoi(m.first.c_str());
        pFiles = &m.second;
      }
    nScale = nBest == (1 << 30) ? 1 : nBest;
  }
  if (pFiles->kind == PortJson::STRING)
    files.push_back(pFiles->s);
  else
    for (const PortJson &f : pFiles->items)
      if (f.kind == PortJson::STRING)
        files.push_back(f.s);
  if (files.empty()) {
    pS->why = "the sheet names no picture";
    return false;
  }
  *pnTime = 0;
  for (const std::string &f : files) {
    const std::string path = StudioDataFolder() + f;
    std::vector<unsigned char> bytes, rgba;
    int w = 0, h = 0;
    std::string err;
    if (!PortReadFile(path, &bytes, size_t(1) << 30)) {
      pS->why = f + " is not in the data folder";
      return false;
    }
    *pnTime += FileTime(path);
    if (!PortImageDecode(bytes.data(), bytes.size(), f, &rgba, &w, &h, &err)) {
      pS->why = f + ": " + err;
      return false;
    }
    if (pS->w != 0 && w != pS->w) {
      pS->why = f + " is not as wide as the sheet's first part";
      return false;
    }
    pS->w = w;
    pS->h += h;
    pS->rgba.insert(pS->rgba.end(), rgba.begin(), rgba.end());
  }
  pS->nScale = std::max(1, nScale);
  return true;
}

std::map<std::string, std::unique_ptr<Source>> g_sources;
std::vector<std::unique_ptr<Source>> g_retiredSources;

// Sources unasked for a while let go, once a frame (TexTick), so looking through classic's images
// does not keep every one.
void SourcesTick(void) {
  g_retiredArt.clear();
  g_retiredSources.clear();
  const uint64_t now = PortNowMs();
  if (g_sources.size() < 96)
    return;
  for (auto it = g_sources.begin(); it != g_sources.end();)
    it = it->second && now - it->second->nUsed > 10000 ? g_sources.erase(it) : std::next(it);
}

Source &SourceOf(const Art &a) {
  static const std::string kNoMod;
  const std::string key = a.key + "@" + (a.nKind == ART_CLASSIC ? kNoMod : StudioDataFolder());
  std::unique_ptr<Source> &p = g_sources[key];
  const uint64_t now = PortNowMs();
  if (p)
    p->nUsed = now;
  if (p && (a.nKind == ART_CLASSIC || now - p->nChecked < 1500))
    return *p;
  // a mod's file is looked at again now and then, and read again when it changed
  if (p && a.nKind != ART_CLASSIC) {
    p->nChecked = now;
    long long t = 0;
    if (a.nKind == ART_GRP)
      t = FileTime(StudioDataFolder() + a.file);
    else {
      std::vector<std::string> files;
      // the same files ReadSheet reads, by their times alone
      std::function<void(const PortJson &)> add = [&](const PortJson &j) {
        if (j.kind == PortJson::STRING)
          files.push_back(j.s);
        for (const PortJson &i : j.items)
          add(i);
      };
      const PortJson *pLevel = &a.grp;
      if (a.grp.kind == PortJson::OBJECT) {
        int nBest = 1 << 30;
        for (const PortJsonMember &m : a.grp.members)
          if (atoi(m.first.c_str()) > 0 && atoi(m.first.c_str()) < nBest)
            nBest = atoi(m.first.c_str()), pLevel = &m.second;
      }
      add(*pLevel);
      for (const std::string &f : files)
        t += FileTime(StudioDataFolder() + f);
      if (!a.team.empty())
        t += FileTime(StudioDataFolder() + a.team);
    }
    if (t == p->nTime)
      return *p;
  }
  if (p)
    g_retiredSources.push_back(std::move(p));
  p.reset(new Source);
  Source &s = *p;
  s.nChecked = now;
  s.nUsed = now;
  if (a.nKind == ART_CLASSIC || a.nKind == ART_GRP) {
    bool bRead;
    if (a.nKind == ART_CLASSIC) {
      bRead = GameFile(a.file.c_str(), &s.bytes);
    } else {
      bRead = PortReadFile(StudioDataFolder() + a.file, &s.bytes);
      s.nTime = FileTime(StudioDataFolder() + a.file);
    }
    if (!bRead || s.bytes.size() < 6) {
      s.why = a.file + (bRead ? " is not a GRP" : " does not read");
      return s;
    }
    s.nCount = s.bytes[0] | s.bytes[1] << 8;
    s.gw = s.bytes[2] | s.bytes[3] << 8;
    s.gh = s.bytes[4] | s.bytes[5] << 8;
    if (s.nCount == 0 || 6 + static_cast<size_t>(s.nCount) * 8 > s.bytes.size()) {
      s.why = a.file + " is not a GRP";
      return s;
    }
    s.bOk = true;
    return s;
  }
  // a sheet
  long long nTime = 0;
  if (!ReadSheet(a.grp, a.nScale, &s, &nTime))
    return s;
  if (!a.team.empty()) {
    std::vector<unsigned char> bytes;
    int w = 0, h = 0;
    std::string err;
    nTime += FileTime(StudioDataFolder() + a.team);
    if (PortReadFile(StudioDataFolder() + a.team, &bytes, size_t(1) << 30) &&
        PortImageDecode(bytes.data(), bytes.size(), a.team, &s.team, &w, &h, &err)) {
      if (w != s.w || h != s.h)
        s.team.clear();  // not the sheet's size: drawn without its team colour
    }
  }
  s.nTime = nTime;
  s.cw = static_cast<int>(a.fFrameW * s.nScale + 0.5);
  s.ch = static_cast<int>(a.fFrameH * s.nScale + 0.5);
  if (s.cw < 1 || s.ch < 1 || s.w < s.cw || s.h < s.ch) {
    s.why = s.cw < 1 ? "the sheet has no frame size" : "the frame is larger than the sheet";
    return s;
  }
  s.nAcross = s.w / s.cw;
  const int nDown = s.h / s.ch;
  s.nCount = s.nAcross * nDown;
  if (a.nFrames > 0)
    s.nCount = std::min(s.nCount, a.nFrames);
  s.ox = a.fOriginX >= 0 ? a.fOriginX : a.fFrameW / 2.0;
  s.oy = a.fOriginY >= 0 ? a.fOriginY : a.fFrameH / 2.0;
  // each cell's drawn pixels, for the picture's size
  s.cells.resize(static_cast<size_t>(s.nCount));
  for (int c = 0; c < s.nCount; ++c) {
    const int x0 = (c % s.nAcross) * s.cw, y0 = (c / s.nAcross) * s.ch;
    int l = s.cw, t = s.ch, r = -1, b = -1;
    for (int y = 0; y < s.ch; ++y) {
      const unsigned char *row = &s.rgba[(static_cast<size_t>(y0 + y) * s.w + x0) * 4];
      for (int x = 0; x < s.cw; ++x)
        if (row[x * 4 + 3] >= 128) {
          l = std::min(l, x), r = std::max(r, x);
          t = std::min(t, y), b = std::max(b, y);
        }
    }
    Rect &e = s.cells[static_cast<size_t>(c)];
    if (r >= 0) {
      const float k = 1.0f / s.nScale;
      e.l = static_cast<float>(l * k - s.ox);
      e.r = static_cast<float>((r + 1) * k - s.ox);
      e.t = static_cast<float>(t * k - s.oy);
      e.b = static_cast<float>((b + 1) * k - s.oy);
    }
  }
  s.bOk = true;
  return s;
}

int FrameCount(const Art &a) {
  if (a.nKind == ART_NONE)
    return 0;
  const Source &s = SourceOf(a);
  return s.bOk ? s.nCount : 0;
}

// The frame set a picture starts at: where the image's script starts, or the first when that is past
// its frames (a script of its base's that does not fit a sheet of its own).
int StartSet(const Art &a) {
  return a.nFirst < FrameCount(a) ? a.nFirst : 0;
}

// A frame's drawn rectangle, game pixels from the origin, unmirrored.
Rect FrameRect(const Art &a, int nFrame) {
  Rect r;
  const Source &s = SourceOf(a);
  if (!s.bOk)
    return r;
  if (a.nKind == ART_SHEET) {
    if (nFrame >= 0 && nFrame < static_cast<int>(s.cells.size()))
      r = s.cells[static_cast<size_t>(nFrame)];
    return r;
  }
  GrpFrame f;
  if (!GrpFrameAt(s, nFrame, &f) || f.w == 0 || f.h == 0)
    return r;
  r.l = f.x - s.gw / 2.0f;
  r.t = f.y - s.gh / 2.0f;
  r.r = r.l + f.w;
  r.b = r.t + f.h;
  return r;
}

// A frame made RGBA: its pixels, their size, where their corner is from the origin in game
// pixels, and how many of them make a game pixel.
struct Pic {
  std::vector<unsigned char> rgba;
  int w = 0, h = 0;
  float ox = 0, oy = 0, fScale = 1;
  int nDrawn = 0;
};

// A GRP frame's rows, as classic encodes them (0x80 | n clear, 0x40 | n of one index, n indexes),
// read inside the file's bytes only: a mod's file may be broken.
bool GrpPixels(const Source &s, const GrpFrame &f, std::vector<int> *pPx) {
  const std::vector<unsigned char> &b = s.bytes;
  pPx->assign(static_cast<size_t>(f.w) * f.h, -1);
  if (f.off + 2u * f.h > b.size())
    return false;
  const bool bRuns = (b[f.off] | b[f.off + 1] << 8) == 2 * f.h;
  if (!bRuns) {
    // whole rows of indexes, 0 clear
    if (f.off + static_cast<size_t>(f.w) * f.h > b.size())
      return false;
    for (size_t i = 0; i < pPx->size(); ++i)
      (*pPx)[i] = b[f.off + i] ? b[f.off + i] : -1;
    return true;
  }
  for (int y = 0; y < f.h; ++y) {
    size_t p = f.off + (b[f.off + 2 * y] | b[f.off + 2 * y + 1] << 8);
    int *row = pPx->data() + static_cast<size_t>(y) * f.w;
    int x = 0;
    while (x < f.w && p < b.size()) {
      const int c = b[p++];
      if (c & 0x80) {
        x += c & 0x7F;
      } else if (c & 0x40) {
        if (p >= b.size())
          return false;
        const int v = b[p++];
        for (int n = c & 0x3F; n > 0 && x < f.w; --n)
          row[x++] = v;
      } else {
        if (c == 0 || p + c > b.size())
          break;
        for (int n = 0; n < c && x < f.w; ++n)
          row[x++] = b[p + n];
        p += c;
      }
    }
  }
  return true;
}

bool MakePic(const Art &a, int nFrame, int nColour, Pic *pOut) {
  const Source &s = SourceOf(a);
  if (!s.bOk || nFrame < 0 || nFrame >= s.nCount)
    return false;
  const Colours &col = ColoursOf();
  nColour &= 7;
  if (a.nKind == ART_SHEET) {
    const int x0 = (nFrame % s.nAcross) * s.cw, y0 = (nFrame / s.nAcross) * s.ch;
    pOut->w = s.cw;
    pOut->h = s.ch;
    pOut->fScale = static_cast<float>(s.nScale);
    pOut->ox = static_cast<float>(-s.ox);
    pOut->oy = static_cast<float>(-s.oy);
    pOut->rgba.assign(static_cast<size_t>(s.cw) * s.ch * 4, 0);
    // the team colour takes the place of the team pixels' hue, at their brightness
    int tr = 255, tg = 255, tb = 255;
    if (col.bOk)
      tr = col.team[nColour][0][0], tg = col.team[nColour][0][1], tb = col.team[nColour][0][2];
    for (int y = 0; y < s.ch; ++y) {
      const size_t at = (static_cast<size_t>(y0 + y) * s.w + x0) * 4;
      const unsigned char *src = &s.rgba[at];
      const unsigned char *team = s.team.empty() ? nullptr : &s.team[at];
      unsigned char *dst = &pOut->rgba[static_cast<size_t>(y) * s.cw * 4];
      for (int x = 0; x < s.cw; ++x) {
        const unsigned char *q = src + x * 4;
        unsigned char *d = dst + x * 4;
        const int alpha = a.nDraw == 10 ? (q[3] > 0 ? 128 : 0) : q[3];
        if (alpha == 0)
          continue;
        ++pOut->nDrawn;
        if (a.nDraw == 10) {
          d[0] = d[1] = d[2] = 150;
          d[3] = 170;
          continue;
        }
        memcpy(d, q, 3);
        d[3] = static_cast<unsigned char>(alpha);
        if (team && team[x * 4 + 3] >= 128 && std::max({team[x * 4], team[x * 4 + 1], team[x * 4 + 2]}) >= 128) {
          const float l = std::min(1.0f, std::max({q[0], q[1], q[2]}) / 255.0f * 1.15f);
          d[0] = static_cast<unsigned char>(tr * l);
          d[1] = static_cast<unsigned char>(tg * l);
          d[2] = static_cast<unsigned char>(tb * l);
        }
      }
    }
    return true;
  }
  GrpFrame f;
  if (!GrpFrameAt(s, nFrame, &f))
    return false;
  pOut->w = std::max(1, f.w);
  pOut->h = std::max(1, f.h);
  pOut->ox = f.x - s.gw / 2.0f;
  pOut->oy = f.y - s.gh / 2.0f;
  pOut->fScale = 1.0f;
  pOut->rgba.assign(static_cast<size_t>(pOut->w) * pOut->h * 4, 0);
  if (f.w == 0 || f.h == 0)
    return true;
  std::vector<int> px;
  if (!GrpPixels(s, f, &px))
    return false;
  for (size_t i = 0; i < px.size(); ++i) {
    const int v = px[i];
    if (v <= 0)
      continue;
    unsigned char *d = &pOut->rgba[i * 4];
    ++pOut->nDrawn;
    if (a.nDraw == 10) {  // a shadow: grey, where the game's black would not show on the studio's dark ground
      d[0] = d[1] = d[2] = 150;
      d[3] = 170;
      continue;
    }
    const unsigned char *rgb = v >= 8 && v < 16 && col.bOk ? col.team[nColour][v - 8] : col.pal[v];
    if (!col.bOk) {
      d[0] = d[1] = d[2] = static_cast<unsigned char>(v);
    } else {
      memcpy(d, rgb, 3);
    }
    d[3] = a.nDraw == 9 ? 200 : 255;  // a remapped image (a glow) a little clear
  }
  return true;
}

// ---- textures ---------------------------------------------------------------------------------------

struct Tex {
  ImTextureData *p = nullptr;
  float ox = 0, oy = 0, fScale = 1;
  int w = 0, h = 0;
  bool bEmpty = false;
  double tUsed = 0;
};

std::map<std::string, Tex> g_tex;
std::vector<ImTextureData *> g_dying;
int g_nFrameSeen = -1;
int g_nMadeThisFrame = 0;
const int kMakePerFrame = 48;

// Once a frame: textures unused for a while given up, and those the backend has destroyed freed.
void TexTick(void) {
  if (g_nFrameSeen == ImGui::GetFrameCount())
    return;
  g_nFrameSeen = ImGui::GetFrameCount();
  g_nMadeThisFrame = 0;
  SourcesTick();
  const double now = ImGui::GetTime();
  if (g_tex.size() > 384) {
    for (auto it = g_tex.begin(); it != g_tex.end();) {
      if (now - it->second.tUsed > 4.0) {
        if (it->second.p) {
          it->second.p->WantDestroyNextFrame = true;
          g_dying.push_back(it->second.p);
        }
        it = g_tex.erase(it);
      } else {
        ++it;
      }
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

ImTextureData *MakeTexture(const std::vector<unsigned char> &rgba, int w, int h) {
  ImTextureData *p = IM_NEW(ImTextureData)();
  p->Create(ImTextureFormat_RGBA32, w, h);
  memcpy(p->Pixels, rgba.data(), std::min(rgba.size(), static_cast<size_t>(w) * h * 4));
  p->UseColors = true;
  ImGui::RegisterUserTexture(p);
  ++g_nMadeThisFrame;
  return p;
}

// A frame's texture; null while it waits its turn to be made, or when it has nothing to draw.
const Tex *FrameTex(const Art &a, int nFrame, int nColour) {
  TexTick();
  const std::string key = a.key + "@" + StudioDataFolder() + "#" + std::to_string(nFrame) + "#" +
                          std::to_string(a.nDraw) + "#" + std::to_string(nColour & 7) + "#" +
                          std::to_string(a.nKind == ART_CLASSIC ? 0 : SourceOf(a).nTime);
  auto it = g_tex.find(key);
  if (it != g_tex.end()) {
    it->second.tUsed = ImGui::GetTime();
    return it->second.bEmpty ? nullptr : &it->second;
  }
  if (g_nMadeThisFrame >= kMakePerFrame)
    return nullptr;
  Pic pic;
  Tex t;
  t.tUsed = ImGui::GetTime();
  if (!MakePic(a, nFrame, nColour, &pic) || pic.nDrawn == 0) {
    t.bEmpty = true;
    g_tex[key] = t;
    return nullptr;
  }
  t.p = MakeTexture(pic.rgba, pic.w, pic.h);
  t.w = pic.w;
  t.h = pic.h;
  t.ox = pic.ox;
  t.oy = pic.oy;
  t.fScale = pic.fScale;
  return &(g_tex[key] = t);
}

// A whole sheet, shown with its slicing over it: made smaller to fit 4,096 a side.
const Tex *SheetTex(const Art &a) {
  TexTick();
  const Source &s = SourceOf(a);
  if (!s.bOk || a.nKind != ART_SHEET)
    return nullptr;
  const std::string key = "sheet:" + a.key + "@" + StudioDataFolder() + "#" + std::to_string(s.nTime);
  auto it = g_tex.find(key);
  if (it != g_tex.end()) {
    it->second.tUsed = ImGui::GetTime();
    return &it->second;
  }
  if (g_nMadeThisFrame >= kMakePerFrame)
    return nullptr;
  const int k = std::max(1, (std::max(s.w, s.h) + 4095) / 4096);
  const int w = std::max(1, s.w / k), h = std::max(1, s.h / k);
  std::vector<unsigned char> shrunk(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      memcpy(&shrunk[(static_cast<size_t>(y) * w + x) * 4], &s.rgba[(static_cast<size_t>(y * k) * s.w + x * k) * 4], 4);
  Tex t;
  t.tUsed = ImGui::GetTime();
  t.p = MakeTexture(shrunk, w, h);
  t.w = w;
  t.h = h;
  t.fScale = 1.0f / k;
  return &(g_tex[key] = t);
}

void Nearest(ImDrawList *d, bool bOn) {
  ImGuiPlatformIO &io = ImGui::GetPlatformIO();
  ImDrawCallback cb = bOn ? io.DrawCallback_SetSamplerNearest : io.DrawCallback_SetSamplerLinear;
  if (cb)
    d->AddCallback(cb, nullptr);
}

// ---- pictures ---------------------------------------------------------------------------------------

// The frame a facing (0 up, 8 right, 16 down, 24 left) shows from a frame set, and whether it is
// drawn mirrored: 17 drawn, the left side mirrored, or a 32-direction sheet's own.
int FacingFrame(const Art &a, int nSet, int nFacing, bool *pbMirror) {
  *pbMirror = false;
  nFacing &= 31;
  if (!a.bTurns)
    return nSet;
  if (a.nKind == ART_SHEET && a.nDirs == 32)
    return (nSet / 17) * 32 + nFacing;
  *pbMirror = nFacing > 16;
  return nSet + (nFacing <= 16 ? nFacing : 32 - nFacing);
}

// What a frame set covers at every facing, game pixels from the origin: the size its picture is
// drawn at, so it holds still as it turns.
Rect SetExtent(const Art &a, int nSet) {
  Rect e;
  bool bAny = false;
  auto add = [&](const Rect &r, bool bMirror) {
    if (r.Empty())
      return;
    const float l = bMirror ? -r.r : r.l, rr = bMirror ? -r.l : r.r;
    if (!bAny)
      e = Rect{l, r.t, rr, r.b};
    e.l = std::min(e.l, l), e.r = std::max(e.r, rr), e.t = std::min(e.t, r.t), e.b = std::max(e.b, r.b);
    bAny = true;
  };
  if (!a.bTurns) {
    add(FrameRect(a, nSet), false);
  } else {
    const int nFacings = a.nKind == ART_SHEET && a.nDirs == 32 ? 32 : 17;
    for (int f = 0; f < nFacings; ++f) {
      bool bMirror;
      const int n = FacingFrame(a, nSet, f, &bMirror);
      add(FrameRect(a, n), false);
      if (nFacings == 17)
        add(FrameRect(a, n), true);
    }
  }
  return e;
}

// How a picture is placed in a box: game pixels to the window's, and where the origin falls.
struct Place {
  float k = 1;
  ImVec2 origin;
};

Place PlaceIn(ImVec2 a, ImVec2 b, const Rect &e, bool bPixelArt, float fFill = 0.86f) {
  Place p;
  const float W = b.x - a.x, H = b.y - a.y;
  const float ew = std::max(4.0f, e.r - e.l), eh = std::max(4.0f, e.b - e.t);
  float k = std::min(W * fFill / ew, H * fFill / eh);
  k = std::min(k, P(8.0f));
  if (bPixelArt && k >= 2.0f)
    k = floorf(k);
  p.k = k;
  p.origin = ImVec2(floorf((a.x + b.x) / 2.0f - k * (e.l + e.r) / 2.0f), floorf((a.y + b.y) / 2.0f - k * (e.t + e.b) / 2.0f));
  return p;
}

// A frame drawn with its origin at a point, k window pixels a game pixel; false when it has none.
bool DrawFrame(ImDrawList *d, const Art &a, int nFrame, bool bMirror, int nColour, const Place &pl, ImU32 tint = IM_COL32_WHITE) {
  const Tex *t = FrameTex(a, nFrame, nColour);
  if (t == nullptr || t->p == nullptr)
    return false;
  const float gw = t->w / t->fScale, gh = t->h / t->fScale;
  const float left = bMirror ? -(t->ox + gw) : t->ox;
  const ImVec2 p0(pl.origin.x + left * pl.k, pl.origin.y + t->oy * pl.k);
  const ImVec2 p1(p0.x + gw * pl.k, p0.y + gh * pl.k);
  const bool bNearest = pl.k / t->fScale >= 1.0f;
  if (bNearest)
    Nearest(d, true);
  d->AddImage(t->p->GetTexRef(), p0, p1, ImVec2(bMirror ? 1.0f : 0.0f, 0.0f), ImVec2(bMirror ? 0.0f : 1.0f, 1.0f), tint);
  if (bNearest)
    Nearest(d, false);
  return true;
}

// An image in a box at a frame set and facing; false when it has nothing to draw.
bool DrawImage(ImDrawList *d, ImVec2 a, ImVec2 b, const std::string &image, int nSet, int nFacing, int nColour,
               Place *pPlace = nullptr) {
  const Art &art = ArtOf(image);
  if (art.nKind == ART_NONE || FrameCount(art) == 0)
    return false;
  if (nSet < 0)
    nSet = StartSet(art);
  Rect e = SetExtent(art, nSet);
  if (e.Empty())
    e = Rect{-16, -16, 16, 16};
  const Place pl = pPlace && pPlace->k > 0 && pPlace->origin.x != 0 ? *pPlace : PlaceIn(a, b, e, art.nKind != ART_SHEET || SourceOf(art).nScale == 1);
  if (pPlace)
    *pPlace = pl;
  bool bMirror;
  const int n = FacingFrame(art, nSet, nFacing, &bMirror);
  d->PushClipRect(a, b, true);
  const bool bDrawn = DrawFrame(d, art, n, bMirror, nColour, pl);
  d->PopClipRect();
  return bDrawn;
}

// ---- command icons ----------------------------------------------------------------------------------

// The console's shades a command icon is drawn in: ticon.pcx's rows (0 a button that can be pressed,
// 1 one that cannot, 2 pressed), each mapping an icon's values 0 to 15 to the palette.
const unsigned char *IconRow(int nRow) {
  static unsigned char s_rows[6][16];
  static bool s_bTried = false;
  if (!s_bTried) {
    s_bTried = true;
    for (int r = 0; r < 6; ++r)
      for (int i = 0; i < 16; ++i)
        s_rows[r][i] = static_cast<unsigned char>(i);
    std::vector<unsigned char> f;
    unsigned char *pPixels = nullptr;
    int w = 0, h = 0;
    if (GameFile("unit\\cmdbtns\\ticon.pcx", &f) && PortBmpDecodePcx(f.data(), f.size(), &pPixels, &w, &h) && pPixels) {
      for (int r = 0; r < 6 && r < h; ++r)
        if (w >= 16)
          memcpy(s_rows[r], pPixels + static_cast<size_t>(r) * w, 16);
      SMemFree(pPixels, "studio_art.cpp", __LINE__, 0);
    }
  }
  return s_rows[std::max(0, std::min(5, nRow))];
}

// A mod's PNG icon as the console's shades, brightest first, by the brightness each gives (the
// loader's own rule, ArtValues in port_moddata.cpp): -1 where it is clear.
void IconShades(const std::vector<unsigned char> &rgba, int w, int h, std::vector<int> *pPx) {
  static const struct {
    int v, nLum;
  } kShades[] = {{1, 230}, {4, 200}, {6, 170}, {7, 140}, {8, 123}, {9, 96}, {10, 79}, {11, 63}, {12, 35}, {13, 27}, {14, 18}, {15, 7}};
  pPx->assign(static_cast<size_t>(w) * h, -1);
  for (size_t i = 0; i < pPx->size(); ++i) {
    const unsigned char *c = &rgba[i * 4];
    if (c[3] < 128)
      continue;
    const int nLum = ((c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8) * 230 / 255;
    int nBest = 0;
    for (int k = 1; k < static_cast<int>(sizeof kShades / sizeof kShades[0]); ++k)
      if (abs(kShades[k].nLum - nLum) < abs(kShades[nBest].nLum - nLum))
        nBest = k;
    (*pPx)[i] = kShades[nBest].v;
  }
}

const Tex *NoTex(const std::string &key) {
  Tex t;
  t.bEmpty = true;
  t.tUsed = ImGui::GetTime();
  g_tex[key] = t;
  return nullptr;
}

// An icon's values made a texture: 1 to 15 through the button's row, the rest the palette's.
const Tex *IconTex(const std::string &key, const std::vector<int> &px, int w, int h, bool bDisabled) {
  const Colours &col = ColoursOf();
  const unsigned char *row = IconRow(bDisabled ? 1 : 0);
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4, 0);
  int nDrawn = 0;
  for (size_t i = 0; i < px.size() && i * 4 < rgba.size(); ++i) {
    int v = px[i];
    if (v <= 0)
      continue;
    if (v < 16)
      v = row[v];
    memcpy(&rgba[i * 4], col.pal[v & 255], 3);
    rgba[i * 4 + 3] = 255;
    ++nDrawn;
  }
  if (nDrawn == 0 || !col.bOk)
    return NoTex(key);
  Tex t;
  t.tUsed = ImGui::GetTime();
  t.p = MakeTexture(rgba, w, h);
  t.w = w;
  t.h = h;
  return &(g_tex[key] = t);
}

// A texture already made for key, or null with *pbFound false when it is still to make.
const Tex *IconFound(const std::string &key, bool *pbFound) {
  auto it = g_tex.find(key);
  *pbFound = it != g_tex.end();
  if (!*pbFound)
    return nullptr;
  it->second.tUsed = ImGui::GetTime();
  return it->second.bEmpty ? nullptr : &it->second;
}

// A GRP's frame as an icon: cmdicons.grp's, or a mod's GRP icon's first.
const Tex *GrpIconTex(const Art &a, int nFrame, bool bDisabled) {
  const std::string key = "icon:" + a.key + "@" + StudioDataFolder() + "#" + std::to_string(nFrame) + (bDisabled ? "#off" : "#on");
  bool bFound;
  const Tex *pt = IconFound(key, &bFound);
  if (bFound || g_nMadeThisFrame >= kMakePerFrame)
    return pt;
  const Source &s = SourceOf(a);
  GrpFrame f;
  std::vector<int> px;
  if (!s.bOk || !GrpFrameAt(s, nFrame, &f) || f.w == 0 || f.h == 0 || !GrpPixels(s, f, &px))
    return NoTex(key);
  return IconTex(key, px, f.w, f.h, bDisabled);
}

// A mod's PNG icon, read again when the file changes.
const Tex *PngIconTex(const std::string &rel, bool bDisabled) {
  const std::string path = StudioDataFolder() + rel;
  const std::string key = "icon:p:" + path + "#" + std::to_string(FileTime(path)) + (bDisabled ? "#off" : "#on");
  bool bFound;
  const Tex *pt = IconFound(key, &bFound);
  if (bFound || g_nMadeThisFrame >= kMakePerFrame)
    return pt;
  std::vector<unsigned char> bytes, rgba;
  int w = 0, h = 0;
  std::string err;
  std::vector<int> px;
  if (PortReadFile(path, &bytes) && PortImageDecode(bytes.data(), bytes.size(), rel, &rgba, &w, &h, &err) && w > 0 && h > 0)
    IconShades(rgba, w, h, &px);
  if (px.empty())
    return NoTex(key);
  return IconTex(key, px, w, h, bDisabled);
}

const Art &CmdIcons(void) {
  static Art a;
  if (a.key.empty()) {
    a.nKind = ART_CLASSIC;
    a.file = "unit\\cmdbtns\\cmdicons.grp";
    a.key = "i:cmdicons";
  }
  return a;
}

// An icon in a box: centred, a whole number of times its size when it fits, drawn sharp; a
// larger one (a mod's PNG at the HD scale) made smaller to fit.
bool DrawIconTex(ImDrawList *d, ImVec2 a, ImVec2 b, const Tex *t) {
  if (t == nullptr || t->p == nullptr)
    return false;
  const float W = b.x - a.x, H = b.y - a.y;
  float k = std::min(W / t->w, H / t->h);
  if (k >= 1.0f)
    k = floorf(k);
  const float w = t->w * k, h = t->h * k;
  const ImVec2 p0(floorf(a.x + (W - w) / 2.0f), floorf(a.y + (H - h) / 2.0f));
  if (k >= 1.0f)
    Nearest(d, true);
  d->AddImage(t->p->GetTexRef(), p0, ImVec2(p0.x + w, p0.y + h));
  if (k >= 1.0f)
    Nearest(d, false);
  return true;
}

// ---- wireframes -------------------------------------------------------------------------------------

// What a unit's console picture of a kind (its record's "icon", "wireframe" or "transport_wireframe";
// "" for one no field gives) is drawn from: a file of the mod's (*pFile), or a classic unit's row of
// the classic GRP (*pnRow). Its own field, or the unit it names, followed a few steps; else the
// classic unit its looks or base come to. False when the chain does not come to either.
bool ConsoleArtOf(const std::string &unit, const char *pszKey, std::string *pFile, int *pnRow) {
  pFile->clear();
  *pnRow = -1;
  std::string at = unit;
  for (int nStep = 0; nStep < 8; ++nStep) {
    StudioRecord r;
    if (!StudioRecordOf(ST_UNITS, at, &r))
      return false;
    const PortJson &own = pszKey[0] ? ChainField(ST_UNITS, at, pszKey) : PortJson();
    if (own.kind == PortJson::STRING && !own.s.empty()) {
      StudioRecord ru;
      if (StudioRecordOf(ST_UNITS, own.s, &ru)) {
        at = own.s;
        continue;
      }
      *pFile = own.s;
      return true;
    }
    if (r.kind == SK_CLASSIC || r.kind == SK_EDIT) {
      *pnRow = r.kind == SK_CLASSIC ? StudioClassicRow(ST_UNITS, at) : r.nId;
      return *pnRow >= 0;
    }
    // a mod's unit without one: its looks', else its base's
    const PortJson &looks = ChainField(ST_UNITS, at, "looks");
    std::string next = looks.kind == PortJson::STRING ? looks.s : r.base;
    if (looks.kind == PortJson::NUMBER)
      if (const char *psz = StudioClassicName(ST_UNITS, static_cast<int>(looks.d)))
        next = psz;
    if (!next.empty() && isdigit(static_cast<unsigned char>(next[0])))
      if (const char *psz = StudioClassicName(ST_UNITS, atoi(next.c_str())))
        next = psz;
    if (next.empty() || next == at)
      return false;
    at = next;
  }
  return false;
}

bool IsGrpName(const std::string &file) {
  return file.size() > 4 && Lower(file.substr(file.size() - 4)) == ".grp";
}

// A wireframe's values as the status panel colours them, for a unit whole and unhurt: the outline
// (0xC0 and 0xC1) in the shields' blue, the four parts (0xD0 to 0xD3) green, each value's low bits
// its shade; any other value the palette's.
void WireColour(int v, unsigned char *d) {
  static const unsigned char kOutline[4][3] = {{96, 160, 255}, {56, 104, 216}, {40, 72, 168}, {24, 48, 120}};
  static const unsigned char kPart[4][3] = {{36, 232, 36}, {24, 196, 24}, {16, 160, 16}, {8, 120, 8}};
  if ((v & 0xF0) == 0xC0)
    memcpy(d, kOutline[v & 3], 3);
  else if ((v & 0xF0) == 0xD0)
    memcpy(d, kPart[v & 3], 3);
  else
    memcpy(d, ColoursOf().pal[v & 255], 3);
  d[3] = 255;
}

const Tex *WireTexOf(const std::string &key, const std::vector<int> &px, int w, int h) {
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4, 0);
  int nDrawn = 0;
  for (size_t i = 0; i < px.size() && i * 4 < rgba.size(); ++i)
    if (px[i] > 0) {
      WireColour(px[i], &rgba[i * 4]);
      ++nDrawn;
    }
  if (nDrawn == 0)
    return NoTex(key);
  Tex t;
  t.tUsed = ImGui::GetTime();
  t.p = MakeTexture(rgba, w, h);
  t.w = w;
  t.h = h;
  return &(g_tex[key] = t);
}

// A PNG wireframe as the loader makes its values (ArtValues): its edge the outline, bright or dark,
// and inside it the four parts, a quarter each.
void WireValues(const std::vector<unsigned char> &rgba, int w, int h, std::vector<int> *pPx) {
  std::vector<int> lum(static_cast<size_t>(w) * h, -1);
  for (size_t i = 0; i < lum.size(); ++i)
    if (rgba[i * 4 + 3] >= 128)
      lum[i] = (rgba[i * 4] * 77 + rgba[i * 4 + 1] * 150 + rgba[i * 4 + 2] * 29) >> 8;
  auto clear = [&](int x, int y) { return x < 0 || y < 0 || x >= w || y >= h || lum[static_cast<size_t>(y) * w + x] < 0; };
  pPx->assign(lum.size(), -1);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int v = lum[static_cast<size_t>(y) * w + x];
      if (v < 0)
        continue;
      int &o = (*pPx)[static_cast<size_t>(y) * w + x];
      if (clear(x - 1, y) || clear(x + 1, y) || clear(x, y - 1) || clear(x, y + 1))
        o = v >= 128 ? 0xC0 : 0xC1;
      else
        o = 0xD0 + (x * 2 >= w ? 1 : 0) + (y * 2 >= h ? 2 : 0);
    }
}

// A wireframe's texture: nKind 0 the status panel's (wirefram.grp), 1 a transport's (tranwire.grp),
// 2 a group's (grpwire.grp); the mod's own file for the first two when it gives one.
const Tex *WireTex(const std::string &unit, int nKind) {
  static const char *const kKeys[3] = {"wireframe", "transport_wireframe", ""};
  static const char *const kFiles[3] = {"unit\\wirefram\\wirefram.grp", "unit\\wirefram\\tranwire.grp", "unit\\wirefram\\grpwire.grp"};
  std::string file;
  int nRow = -1;
  if (!ConsoleArtOf(unit, kKeys[nKind], &file, &nRow))
    return nullptr;
  if (file.empty()) {
    Art a;
    a.nKind = ART_CLASSIC;
    a.file = kFiles[nKind];
    a.key = std::string("w:") + kFiles[nKind];
    const std::string key = "wire:" + a.key + "#" + std::to_string(nRow);
    bool bFound;
    const Tex *pt = IconFound(key, &bFound);
    if (bFound || g_nMadeThisFrame >= kMakePerFrame)
      return pt;
    const Source &s = SourceOf(a);
    GrpFrame f;
    std::vector<int> px;
    if (!s.bOk || !GrpFrameAt(s, nRow, &f) || f.w == 0 || f.h == 0 || !GrpPixels(s, f, &px))
      return NoTex(key);
    return WireTexOf(key, px, f.w, f.h);
  }
  const std::string path = StudioDataFolder() + file;
  const std::string key = "wire:f:" + path + "#" + std::to_string(FileTime(path));
  bool bFound;
  const Tex *pt = IconFound(key, &bFound);
  if (bFound || g_nMadeThisFrame >= kMakePerFrame)
    return pt;
  std::vector<int> px;
  int w = 0, h = 0;
  if (IsGrpName(file)) {
    Art g;
    g.nKind = ART_GRP;
    g.file = file;
    g.key = "g:" + file;
    const Source &s = SourceOf(g);
    GrpFrame f;
    if (s.bOk && GrpFrameAt(s, 0, &f) && GrpPixels(s, f, &px))
      w = f.w, h = f.h;
  } else {
    std::vector<unsigned char> bytes, rgba;
    std::string err;
    if (PortReadFile(path, &bytes) && PortImageDecode(bytes.data(), bytes.size(), file, &rgba, &w, &h, &err) && w > 0 && h > 0)
      WireValues(rgba, w, h, &px);
  }
  if (px.empty() || w == 0 || h == 0)
    return NoTex(key);
  return WireTexOf(key, px, w, h);
}

// The console's pictures of a unit in a row: its command icon as a button that can be pressed and
// one that cannot, its wireframe, a transport's and a group's; each in a dark well with its word.
float ConsoleRow(ImDrawList *d, ImVec2 o, const std::string &unit) {
  struct Cell {
    const char *pszWord;
    float w, h;
  };
  static const Cell kCells[] = {{"icon", 44, 42}, {"not ready", 44, 42}, {"wireframe", 72, 72}, {"in a transport", 44, 42}, {"in a group", 44, 42}};
  float x = o.x;
  const float top = o.y;
  for (int i = 0; i < 5; ++i) {
    const ImVec2 a(x, top + (P(72.0f) - P(kCells[i].h))), b(a.x + P(kCells[i].w), top + P(72.0f));
    d->AddRectFilled(a, b, C(TK_BLACK), P(2.0f));
    d->AddRect(a, b, C(TK_LINE), P(2.0f));
    const ImVec2 ia(a.x + P(2.0f), a.y + P(2.0f)), ib(b.x - P(2.0f), b.y - P(2.0f));
    if (i < 2)
      StudioUnitIconPicture(d, ia, ib, unit, i == 1);
    else
      DrawIconTex(d, ia, ib, WireTex(unit, i - 2));
    const float tw = TW(STUDIO_FONT_TEXT, 9.5f, kCells[i].pszWord);
    T(d, STUDIO_FONT_TEXT, 9.5f, (a.x + b.x - tw) / 2.0f, top + P(76.0f), C(TK_INK3), kCells[i].pszWord);
    x = std::max(b.x, (a.x + b.x + tw) / 2.0f) + P(14.0f);
  }
  return P(94.0f);
}

// ---- the chain --------------------------------------------------------------------------------------

struct Chain {
  std::string flingy, sprite, image, subunit;
};

std::string RefOf(int t, const std::string &name, const char *pszKey, int nTable) {
  const long long n = StudioNumber(t, name, pszKey, -1);
  if (n < 0)
    return std::string();
  const std::string s = StudioRefName(nTable, n);
  StudioRecord r;
  return StudioRecordOf(nTable, s, &r) ? s : std::string();
}

Chain ChainOf(const std::string &unit) {
  Chain c;
  c.flingy = RefOf(ST_UNITS, unit, "flingy", ST_FLINGY);
  if (!c.flingy.empty())
    c.sprite = RefOf(ST_FLINGY, c.flingy, "sprite", ST_SPRITES);
  if (!c.sprite.empty())
    c.image = RefOf(ST_SPRITES, c.sprite, "image", ST_IMAGES);
  const long long nSub = StudioNumber(ST_UNITS, unit, "subunit", 228);
  if (nSub >= 0 && nSub < 228)
    c.subunit = StudioRefName(ST_UNITS, nSub);
  else if (nSub >= StudioModFirst(ST_UNITS))
    c.subunit = StudioRefName(ST_UNITS, nSub);
  StudioRecord r;
  if (!c.subunit.empty() && !StudioRecordOf(ST_UNITS, c.subunit, &r))
    c.subunit.clear();
  return c;
}

std::string SpriteImage(const std::string &sprite) {
  return RefOf(ST_SPRITES, sprite, "image", ST_IMAGES);
}

std::string FlingyImage(const std::string &flingy) {
  const std::string s = RefOf(ST_FLINGY, flingy, "sprite", ST_SPRITES);
  return s.empty() ? s : SpriteImage(s);
}

// A unit drawn with its turret (a subunit's image over it at the same origin).
// A point of an image's overlay file (.lo) at a frame, as the game's GetOverlayOffset reads it: the
// file of kind nKind (0 attack, 1 damage, 2 special, 3 landing dust, 4 lift-off dust), point nPoint of
// the frame, x turned for a mirrored frame. Classic frames only: a mod's own frames have their
// overlays at the frame's centre. False when there is no such point.
bool LoOffset(const std::string &image, const Art &a, int nKind, int nFrame, int nPoint, bool bMirror, ImVec2 *pOut) {
  static const char *const kLo[] = {"attack_overlay", "damage_overlay", "special_overlay", "landing_dust_overlay",
                                    "liftoff_dust_overlay"};
  static std::map<long long, std::vector<unsigned char>> s_lo;
  *pOut = ImVec2(0, 0);
  if (a.nKind != ART_CLASSIC || nKind < 0 || nKind > 4)
    return false;
  const long long nLo = StudioNumber(ST_IMAGES, image, kLo[nKind], 0);
  const std::vector<std::string> &tbl = ImagesTbl();
  if (nLo <= 0 || nLo > static_cast<long long>(tbl.size()))
    return false;
  auto it = s_lo.find(nLo);
  if (it == s_lo.end()) {
    std::vector<unsigned char> f;
    GameFile(("unit\\" + tbl[static_cast<size_t>(nLo - 1)]).c_str(), &f);
    it = s_lo.emplace(nLo, f).first;
  }
  const std::vector<unsigned char> &f = it->second;
  // the file: frames and points each, then each frame's offset of its points (x, y signed bytes)
  if (f.size() < 8)
    return false;
  const size_t nFrames = f[0] | f[1] << 8 | f[2] << 16 | static_cast<size_t>(f[3]) << 24;
  const size_t at = 8 + 4 * static_cast<size_t>(nFrame);
  if (nFrame < 0 || static_cast<size_t>(nFrame) >= nFrames || at + 4 > f.size())
    return false;
  const long long off = static_cast<long long>(f[at] | f[at + 1] << 8 | f[at + 2] << 16 | static_cast<size_t>(f[at + 3]) << 24) +
                        2LL * nPoint;
  if (off < 0 || static_cast<size_t>(off) + 2 > f.size())
    return false;
  const float x = static_cast<signed char>(f[static_cast<size_t>(off)]), y = static_cast<signed char>(f[static_cast<size_t>(off) + 1]);
  *pOut = ImVec2(bMirror ? -x : x, y);
  return true;
}

// Where a turret stands on its unit at a frame: the special overlay's first point.
ImVec2 TurretOffset(const std::string &image, const Art &a, int nFrame, bool bMirror) {
  ImVec2 off;
  LoOffset(image, a, 2, nFrame, 0, bMirror, &off);
  return off;
}

bool DrawUnit(ImDrawList *d, ImVec2 a, ImVec2 b, const std::string &unit, int nFacing, int nColour, int nSet = -1) {
  const Chain c = ChainOf(unit);
  if (c.image.empty())
    return false;
  Place pl;
  pl.k = 0;
  if (!DrawImage(d, a, b, c.image, nSet, nFacing, nColour, &pl))
    return false;
  if (!c.subunit.empty() && c.subunit != unit) {
    const Chain s = ChainOf(c.subunit);
    if (!s.image.empty()) {
      // the turret where the game puts it: the unit's position moved by its frame's overlay point
      const Art &art = ArtOf(c.image);
      bool bMirror;
      const int nFrame = FacingFrame(art, nSet < 0 ? StartSet(art) : nSet, nFacing, &bMirror);
      const ImVec2 off = TurretOffset(c.image, art, nFrame, bMirror);
      Place turret = pl;
      turret.origin = ImVec2(floorf(pl.origin.x + off.x * pl.k), floorf(pl.origin.y + off.y * pl.k));
      DrawImage(d, a, b, s.image, -1, nFacing, nColour, &turret);
    }
  }
  return true;
}

// ---- the views' shared pieces ------------------------------------------------------------------------

// What the views show: the facing (-1 turning), the player's colour, the frame set (-1 the script's
// first), each kept while the same image is shown.
struct ViewState {
  int nFacing = 12;
  bool bTurning = false;
  int nColour = 0;
  int nSet = -1;
  std::string image;
};
ViewState g_view;

int ViewFacing(void) {
  return g_view.bTurning ? static_cast<int>(ImGui::GetTime() * 5.0) % 32 : g_view.nFacing;
}

void ViewFor(const std::string &image) {
  if (g_view.image != image) {
    g_view.image = image;
    g_view.nSet = -1;
  }
}

// The player's colour as eight swatches.
void ColourSwatches(const char *pszId) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImGui::PushID(pszId);
  const float s = P(20.0f);
  for (int i = 0; i < 8; ++i) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##c", ImVec2(s, s)))
      g_view.nColour = i;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", kColourNames[i]);
    ImGui::PopID();
    d->AddRectFilled(ImVec2(at.x + P(2.0f), at.y + P(2.0f)), ImVec2(at.x + s - P(2.0f), at.y + s - P(2.0f)), PlayerColour(i), P(2.0f));
    d->AddRect(at, ImVec2(at.x + s, at.y + s), g_view.nColour == i ? C(TK_HI) : C(TK_LINE), P(3.0f), 0,
               g_view.nColour == i ? P(2.0f) : P(1.0f));
    ImGui::SameLine(0.0f, P(3.0f));
  }
  ImGui::NewLine();
  ImGui::PopID();
}

// The facing: turning or still, and a dial of 32 to pick one.
void FacingControl(const char *pszId) {
  ImGui::PushID(pszId);
  static const char *const kTurn[] = {"Still", "Turning"};
  g_view.bTurning = StudioSeg("##turn", kTurn, 2, g_view.bTurning ? 1 : 0, 24.0f, kStudioFsXs) == 1;
  ImGui::SameLine(0.0f, P(10.0f));
  if (StudioButton("##l", "", "back", SB_PLAIN, 24.0f, 26.0f)) {
    g_view.bTurning = false;
    g_view.nFacing = (g_view.nFacing + 31) % 32;
  }
  ImGui::SameLine(0.0f, P(4.0f));
  // the dial: a ring of 32 ticks, the one shown bright; a click picks the nearest
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float r = P(15.0f);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 c(at.x + r + P(2.0f), at.y + P(12.0f));
  ImGui::InvisibleButton("##dial", ImVec2(2 * r + P(4.0f), P(24.0f)));
  if (ImGui::IsItemActive()) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float ang = atan2f(m.x - c.x, -(m.y - c.y));
    g_view.nFacing = (static_cast<int>(floorf(ang / (6.2831853f / 32.0f) + 0.5f)) + 32) % 32;
    g_view.bTurning = false;
  }
  const int nNow = ViewFacing();
  for (int i = 0; i < 32; ++i) {
    const float ang = i * 6.2831853f / 32.0f;
    const float r0 = i % 8 == 0 ? r * 0.55f : r * 0.72f;
    d->AddLine(ImVec2(c.x + sinf(ang) * r0, c.y - cosf(ang) * r0 * 0.8f), ImVec2(c.x + sinf(ang) * r, c.y - cosf(ang) * r * 0.8f),
               i == nNow ? C(TK_HI) : C(TK_LINE2), i == nNow ? P(2.5f) : P(1.0f));
  }
  ImGui::SameLine(0.0f, P(4.0f));
  if (StudioButton("##r", "", "caret", SB_PLAIN, 24.0f, 26.0f)) {
    g_view.bTurning = false;
    g_view.nFacing = (g_view.nFacing + 1) % 32;
  }
  ImGui::SameLine(0.0f, P(8.0f));
  bool bMirror = nNow > 16;
  if (!g_view.image.empty()) {
    const Art &a = ArtOf(g_view.image);
    FacingFrame(a, 0, nNow, &bMirror);
  }
  const std::string s = "facing " + std::to_string(nNow) + (bMirror ? ", mirrored" : "");
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + P(4.0f)));
  StudioLabel(STUDIO_FONT_MONO, kStudioFsXs, C(TK_INK3), s.c_str());
  ImGui::PopID();
}

// The frame set: back and on a set (17 frames for an image that turns), and which it is.
void SetControl(const Art &a) {
  const int nCount = FrameCount(a);
  const int nStep = a.bTurns ? 17 : 1;
  int nSet = g_view.nSet < 0 ? StartSet(a) : g_view.nSet;
  if (StudioButton("##setdown", "", "back", SB_PLAIN, 24.0f, 26.0f, kStudioFsXs, nSet - nStep >= 0))
    g_view.nSet = std::max(0, nSet - nStep);
  ImGui::SameLine(0.0f, P(4.0f));
  if (StudioButton("##setup", "", "caret", SB_PLAIN, 24.0f, 26.0f, kStudioFsXs, nSet + nStep < nCount))
    g_view.nSet = nSet + nStep;
  ImGui::SameLine(0.0f, P(8.0f));
  nSet = g_view.nSet < 0 ? StartSet(a) : g_view.nSet;
  const bool b32 = a.nKind == ART_SHEET && a.nDirs == 32;
  std::string s = (!a.bTurns ? "frame " + std::to_string(nSet)
                   : b32     ? "frames " + std::to_string(nSet / 17 * 32) + " to " + std::to_string(nSet / 17 * 32 + 31)
                             : "frames " + std::to_string(nSet) + " to " + std::to_string(nSet + 16)) +
                  " of " + std::to_string(nCount);
  if (g_view.nSet < 0 || g_view.nSet == StartSet(a))
    s += ", where its script starts";
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + P(4.0f)));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK2), s.c_str());
  if (g_view.nSet >= 0 && g_view.nSet != StartSet(a)) {
    ImGui::SameLine(0.0f, P(8.0f));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y - P(4.0f)));
    if (StudioButton("##setfirst", "Where it starts", "again", SB_FLAT, 22.0f, 0.0f, kStudioFsXs))
      g_view.nSet = -1;
  }
}

// A box with the picture of an image at the view's set, facing and colour, a ring under it.
bool PictureBox(ImVec2 a, ImVec2 b, const std::string &image, const std::string &unit = std::string()) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddRectFilled(a, b, C(TK_WELL), P(3.0f));
  // a faint tile grid, 32 game pixels a square, to judge the size
  d->AddRect(a, b, C(TK_LINE), P(3.0f));
  const Art &art = ArtOf(image);
  const bool bDrawn = unit.empty() ? DrawImage(d, ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), image, g_view.nSet, ViewFacing(), g_view.nColour)
                                   : DrawUnit(d, ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), unit, ViewFacing(), g_view.nColour,
                                              g_view.nSet);
  if (!bDrawn) {
    StudioGlyph(d, "image", ImVec2((a.x + b.x) / 2.0f - P(16.0f), (a.y + b.y) / 2.0f - P(28.0f)), P(32.0f), C(TK_INK3));
    const std::string why = !art.why.empty() ? art.why : FrameCount(art) == 0 ? SourceOf(art).why : std::string("nothing drawn at this frame");
    const float w = TW(STUDIO_FONT_TEXT, kStudioFsXs, why);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, std::max(a.x + P(8.0f), (a.x + b.x - w) / 2.0f), (a.y + b.y) / 2.0f + P(12.0f), C(TK_INK3), why,
      b.x - a.x - P(16.0f));
  }
  return bDrawn;
}

// "unit\terran\marine.grp, 229 frames" and the like: where an image's frames come from.
std::string SourceWords(const Art &a) {
  const Source &s = SourceOf(a);
  std::string w;
  switch (a.nKind) {
  case ART_CLASSIC: w = "classic frames, " + a.file; break;
  case ART_GRP: w = "a GRP of the mod's, " + a.file; break;
  case ART_SHEET: w = "a sheet, " + a.file; break;
  default: return a.why.empty() ? std::string("no frames") : a.why;
  }
  if (!s.bOk)
    return w + "; " + (s.why.empty() ? a.why : s.why);
  w += ", " + std::to_string(s.nCount) + (s.nCount == 1 ? " frame" : " frames");
  if (a.nKind == ART_SHEET)
    w += ", " + std::to_string(s.w) + " x " + std::to_string(s.h) + " px" + (s.nScale > 1 ? " at " + std::to_string(s.nScale) + "x" : "");
  else
    w += ", " + std::to_string(s.gw) + " x " + std::to_string(s.gh) + " each";
  return w;
}

// A record's button that opens it, with its table's glyph.
void OpenButton(const char *pszId, int t, const std::string &name, const char *pszGlyph) {
  if (name.empty()) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "none");
    return;
  }
  const bool bMine = IsMine(t, name);
  const std::string label = StudioShowName(t, name) + (bMine ? "" : "  (classic)");
  if (StudioButton(pszId, label.c_str(), pszGlyph, bMine ? SB_ON : SB_PLAIN, 26.0f, 0.0f, kStudioFsXs))
    StudioOpenDoc(t, name, 0);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Open %s %s", StudioTableName(t), name.c_str());
}

// The records of a table that name one: the sprites that show an image, the flingy that move a
// sprite, the units that move with a flingy; found again when the mod changes.
std::vector<std::string> UsersOf(int t, const std::string &name) {
  static std::map<std::string, std::vector<std::string>> s_cache;
  static unsigned s_nChange = ~0u;
  if (s_nChange != StudioChange()) {
    s_cache.clear();
    s_nChange = StudioChange();
  }
  const std::string key = std::to_string(t) + ":" + name;
  auto it = s_cache.find(key);
  if (it != s_cache.end())
    return it->second;
  std::vector<std::string> &out = s_cache[key];
  StudioRecord r;
  if (!StudioRecordOf(t, name, &r))
    return out;
  const int nUser = t == ST_IMAGES ? ST_SPRITES : t == ST_SPRITES ? ST_FLINGY : ST_UNITS;
  const char *pszKey = t == ST_IMAGES ? "image" : t == ST_SPRITES ? "sprite" : "flingy";
  auto check = [&](const std::string &u) {
    if (StudioNumber(nUser, u, pszKey, -1) == r.nId)
      out.push_back(u);
  };
  for (const StudioRecord &u : StudioRecords(nUser))
    if (u.kind != SK_EDIT)
      check(u.name);
  for (int n = 0; n < StudioClassicRows(nUser) && out.size() < 40; ++n)
    if (const char *psz = StudioClassicName(nUser, n))
      check(psz);
  return out;
}

void UsedBy(int t, const std::string &name, float fRight) {
  const int nUser = t == ST_IMAGES ? ST_SPRITES : t == ST_SPRITES ? ST_FLINGY : ST_UNITS;
  const char *pszGlyph = nUser == ST_UNITS ? "unit" : nUser == ST_SPRITES ? "sprite" : "link";
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  Wide(d, 9.5f, o.x, o.y + P(6.0f), C(TK_INK3), "Used by", 0.1f);
  float x = o.x + P(78.0f), y = o.y;
  const std::vector<std::string> users = UsersOf(t, name);
  if (users.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(4.0f), C(TK_INK3), "nothing yet");
  int nShown = 0;
  for (const std::string &u : users) {
    const std::string label = StudioShowName(nUser, u);
    const float bw = StudioButtonSize(label.c_str(), pszGlyph, 24.0f, kStudioFsXs).x;
    if (x + bw > fRight && x > o.x + P(78.0f)) {
      x = o.x + P(78.0f);
      y += P(30.0f);
    }
    if (++nShown > 10) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y + P(5.0f), C(TK_INK3), "and " + std::to_string(users.size() - 10) + " more");
      break;
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushID(u.c_str());
    if (StudioButton("##user", label.c_str(), pszGlyph, SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioOpenDoc(nUser, u, 0);
    ImGui::PopID();
    x += bw + P(6.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(30.0f)));
  ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// A field of a record with its label, as Numbers draws it.
void FieldRow(int t, const std::string &name, const char *pszKey, float fLabelW, bool bMine) {
  const StudioField *pf = StudioFieldOf(t, pszKey);
  StudioValue v;
  if (pf == nullptr)
    return;
  StudioGet(t, name, pszKey, &v);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  ImDrawList *d = ImGui::GetWindowDrawList();
  const bool bOwn = v.source == SS_OWN || v.source == SS_EDIT;
  if (bOwn)
    d->AddRectFilled(ImVec2(o.x - P(8.0f), o.y + P(3.0f)), ImVec2(o.x - P(5.0f), o.y + P(23.0f)), C(TK_MOD), P(1.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, o.y + P(5.0f), C(TK_INK2), pf->label.empty() ? pf->key : pf->label, fLabelW - P(8.0f));
  if (ImGui::IsMouseHoveringRect(o, ImVec2(o.x + fLabelW, o.y + P(26.0f))) && !pf->help.empty())
    ImGui::SetTooltip("%s", pf->help.c_str());
  ImGui::SetCursorScreenPos(ImVec2(o.x + fLabelW, o.y));
  ImGui::BeginGroup();
  StudioFieldEditor(t, name, *pf, v, bMine, false);
  ImGui::EndGroup();
  ImGui::SetCursorScreenPos(ImVec2(o.x, std::max(ImGui::GetItemRectMax().y, o.y + P(26.0f)) + P(2.0f)));
  ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// Fields in a column of a width: a child window that grows to hold them, so their editors take its
// width and not the document's.
void FieldColumn(const char *pszId, int t, const std::string &name, std::initializer_list<const char *> keys, float fW, bool bMine) {
  ImGui::BeginChild(pszId, ImVec2(fW, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + P(10.0f), ImGui::GetCursorScreenPos().y));
  ImGui::BeginGroup();
  for (const char *psz : keys)
    FieldRow(t, name, psz, P(120.0f), bMine);
  ImGui::EndGroup();
  ImGui::EndChild();
}

// ---- the sheet and the frames ----------------------------------------------------------------------

bool g_bOriginPick = false;  // a click on the sheet sets the origin
int g_nScrollTo = 0;  // studio_art_view's scroll, done once: 1 the view's top, 2 the animation player

// The animation player at the cursor, the view scrolled to it when asked.
void Animations(const std::string &image, float W) {
  if (g_nScrollTo == 2) {
    ImGui::Dummy(ImVec2(0.0f, 0.0f));  // the line SetScrollHereY goes to
    ImGui::SetScrollHereY(0.0f);
    g_nScrollTo = 0;
  }
  studio_art::AnimPanel(image, W);
}

// The sheet at a width, its slicing over it: each cell's edge, its number, the origin in each; a
// click picks the cell (its frame set and facing), or with the origin picker, where the origin is.
void SheetView(const std::string &image, const Art &a, float fW, float fMaxH, bool bMine) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const Source &s = SourceOf(a);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  if (!s.bOk) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_WARN), (s.why.empty() ? a.why : s.why).c_str());
    return;
  }
  const Tex *t = SheetTex(a);
  const float k = std::min(fW / s.w, fMaxH / s.h);
  const ImVec2 b(o.x + s.w * k, o.y + s.h * k);
  // a checker under the picture, for its clear pixels
  d->AddRectFilled(o, b, C(TK_WELL));
  const float sq = P(8.0f);
  d->PushClipRect(o, b, true);
  for (float y = o.y; y < b.y; y += sq)
    for (float x = o.x + (static_cast<int>((y - o.y) / sq) % 2) * sq; x < b.x; x += 2 * sq)
      d->AddRectFilled(ImVec2(x, y), ImVec2(x + sq, y + sq), C(TK_PANE2));
  if (t && t->p) {
    const bool bNearest = k * t->fScale >= 1.0f;
    if (bNearest)
      Nearest(d, true);
    d->AddImage(t->p->GetTexRef(), o, b);
    if (bNearest)
      Nearest(d, false);
  }
  // the slicing
  const float cw = s.cw * k, ch = s.ch * k;
  const int nDown = (s.nCount + s.nAcross - 1) / s.nAcross;
  for (int i = 0; i <= s.nAcross; ++i)
    d->AddLine(ImVec2(o.x + i * cw, o.y), ImVec2(o.x + i * cw, o.y + nDown * ch), C(TK_SEL_LINE, 0.55f));
  for (int j = 0; j <= nDown; ++j)
    d->AddLine(ImVec2(o.x, o.y + j * ch), ImVec2(o.x + s.nAcross * cw, o.y + j * ch), C(TK_SEL_LINE, 0.55f));
  // cells past the frames' count greyed
  for (int c = s.nCount; c < s.nAcross * nDown; ++c)
    d->AddRectFilled(ImVec2(o.x + (c % s.nAcross) * cw, o.y + (c / s.nAcross) * ch),
                     ImVec2(o.x + (c % s.nAcross + 1) * cw, o.y + (c / s.nAcross + 1) * ch), C(TK_BLACK, 0.55f));
  const bool bNumbers = cw > P(26.0f) && ch > P(16.0f);
  bool bMirror;
  const int nShown = FacingFrame(a, g_view.nSet < 0 ? StartSet(a) : g_view.nSet, ViewFacing(), &bMirror);
  for (int c = 0; c < s.nCount; ++c) {
    const ImVec2 ca(o.x + (c % s.nAcross) * cw, o.y + (c / s.nAcross) * ch);
    const ImVec2 org(ca.x + static_cast<float>(s.ox * s.nScale) * k, ca.y + static_cast<float>(s.oy * s.nScale) * k);
    if (cw > P(10.0f)) {
      d->AddLine(ImVec2(org.x - P(3.0f), org.y), ImVec2(org.x + P(4.0f), org.y), C(TK_GOLD_TOP, 0.8f));
      d->AddLine(ImVec2(org.x, org.y - P(3.0f)), ImVec2(org.x, org.y + P(4.0f)), C(TK_GOLD_TOP, 0.8f));
    }
    if (bNumbers)
      T(d, STUDIO_FONT_MONO, 9.0f, ca.x + P(2.0f), ca.y + P(1.0f), C(TK_INK2, 0.8f), std::to_string(c));
    if (c == nShown)
      d->AddRect(ca, ImVec2(ca.x + cw, ca.y + ch), C(TK_HI), 0.0f, 0, P(2.0f));
  }
  d->PopClipRect();
  d->AddRect(o, b, C(TK_LINE));
  ImGui::SetCursorScreenPos(o);
  ImGui::InvisibleButton("##sheet", ImVec2(std::max(1.0f, b.x - o.x), std::max(1.0f, b.y - o.y)));
  if (ImGui::IsItemHovered()) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    const int col = static_cast<int>((m.x - o.x) / cw), row = static_cast<int>((m.y - o.y) / ch);
    const int c = row * s.nAcross + col;
    if (c >= 0 && c < s.nCount) {
      const double gx = ((m.x - o.x) - col * cw) / k / s.nScale, gy = ((m.y - o.y) - row * ch) / k / s.nScale;
      ImGui::SetTooltip(g_bOriginPick ? "origin %.1f, %.1f" : "frame %d (row %d, direction %d)",
                        g_bOriginPick ? gx : static_cast<double>(c), g_bOriginPick ? gy : static_cast<double>(row),
                        static_cast<int>(g_bOriginPick ? 0 : col % (a.nDirs == 32 ? 32 : 17)));
      if (ImGui::IsItemClicked()) {
        if (g_bOriginPick && bMine) {
          const double hx = floor(gx * 2.0 + 0.5) / 2.0, hy = floor(gy * 2.0 + 0.5) / 2.0;
          StudioAsk("studio_set", "\"table\":\"images\",\"name\":" + Q(image) + ",\"field\":\"origin\",\"value\":[" + Fmt(hx, 1) + "," +
                                      Fmt(hy, 1) + "]");
          g_bOriginPick = false;
        } else if (a.bTurns) {
          const int nPer = a.nDirs == 32 ? 32 : 17;
          g_view.nSet = (c / nPer) * 17;
          g_view.nFacing = c % nPer;
          g_view.bTurning = false;
        } else {
          g_view.nSet = c;
        }
      }
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, b.y + P(8.0f)));
}

// Every frame of an image as a grid of little pictures, in the view's colour; a click picks it.
void FrameGrid(const Art &a, float fW, float fMaxH) {
  const int nCount = FrameCount(a);
  if (nCount == 0)
    return;
  const float cell = P(58.0f);
  const int nAcross = std::max(1, static_cast<int>(fW / (cell + P(4.0f))));
  const int nRows = (nCount + nAcross - 1) / nAcross;
  const float h = std::min(fMaxH, nRows * (cell + P(4.0f)) + P(4.0f));
  // one size for every frame: the largest frame's
  Rect e;
  for (int f = 0; f < nCount; f += std::max(1, nCount / 64)) {
    const Rect r = FrameRect(a, f);
    if (!r.Empty())
      e = e.Empty() ? r : Rect{std::min(e.l, r.l), std::min(e.t, r.t), std::max(e.r, r.r), std::max(e.b, r.b)};
  }
  if (e.Empty())
    e = Rect{-16, -16, 16, 16};
  ImGui::BeginChild("##frames", ImVec2(fW, h), ImGuiChildFlags_None);
  ImDrawList *d = ImGui::GetWindowDrawList();
  bool bMirror;
  const int nShown = FacingFrame(a, g_view.nSet < 0 ? StartSet(a) : g_view.nSet, ViewFacing(), &bMirror);
  ImGuiListClipper clip;
  clip.Begin(nRows);
  while (clip.Step())
    for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
      const ImVec2 o = ImGui::GetCursorScreenPos();
      for (int i = 0; i < nAcross; ++i) {
        const int f = row * nAcross + i;
        if (f >= nCount)
          break;
        const ImVec2 ca(o.x + i * (cell + P(4.0f)), o.y), cb(ca.x + cell, ca.y + cell);
        ImGui::SetCursorScreenPos(ca);
        ImGui::PushID(f);
        if (ImGui::InvisibleButton("##f", ImVec2(cell, cell))) {
          if (a.bTurns && !(a.nKind == ART_SHEET && a.nDirs == 32)) {
            g_view.nSet = (f / 17) * 17;
            g_view.nFacing = f % 17;
          } else if (a.bTurns) {
            g_view.nSet = (f / 32) * 17;
            g_view.nFacing = f % 32;
          } else {
            g_view.nSet = f;
          }
          g_view.bTurning = false;
        }
        const bool bHover = ImGui::IsItemHovered();
        ImGui::PopID();
        d->AddRectFilled(ca, cb, C(TK_WELL), P(2.0f));
        const Place pl = PlaceIn(ca, cb, e, a.nKind != ART_SHEET, 0.92f);
        d->PushClipRect(ca, cb, true);
        DrawFrame(d, a, f, false, g_view.nColour, pl);
        d->PopClipRect();
        T(d, STUDIO_FONT_MONO, 9.0f, ca.x + P(3.0f), ca.y + P(2.0f), C(TK_INK3), std::to_string(f));
        d->AddRect(ca, cb, f == nShown ? C(TK_HI) : bHover ? C(TK_BTN_RIM_HI) : C(TK_LINE), P(2.0f), 0, f == nShown ? P(2.0f) : P(1.0f));
      }
      ImGui::SetCursorScreenPos(o);
      ImGui::Dummy(ImVec2(1, cell));
    }
  ImGui::EndChild();
}

// ---- an image's head ---------------------------------------------------------------------------------

std::string g_sheetFor;  // the image the file dialog brings a sheet in for

void BringSheet(const std::string &image) {
  g_sheetFor = image;
  StudioPickFile(("art.sheet#" + StudioModId()).c_str(), "Pictures and GRPs", "png,webp,tga,grp");
}

void SheetPicked(void) {
  std::string path;
  if (!StudioPickedFile(("art.sheet#" + StudioModId()).c_str(), &path) || g_sheetFor.empty())
    return;
  StudioAsk("studio_art_sheet", "\"image\":" + Q(g_sheetFor) + ",\"from\":" + Q(path));
}

void ImageBody(const std::string &image, float W, bool bLarge) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ViewFor(image);
  const Art &a = ArtOf(image);
  StudioRecord rec;
  StudioRecordOf(ST_IMAGES, image, &rec);
  const bool bMine = rec.kind == SK_OWN;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  // the picture at the left, its controls under it
  const float pw = bLarge ? P(340.0f) : P(260.0f), ph = bLarge ? P(280.0f) : P(220.0f);
  PictureBox(o, ImVec2(o.x + pw, o.y + ph), image);
  ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + ph + P(8.0f)));
  ImGui::BeginGroup();
  ImGui::PushID("pic");
  FacingControl("##facing");
  ColourSwatches("##colour");
  SetControl(a);
  ImGui::PopID();
  ImGui::EndGroup();
  const float leftBottom = ImGui::GetItemRectMax().y;
  // its frames' words at the right
  const float x = o.x + pw + P(22.0f), rw = W - pw - P(22.0f);
  float y = o.y;
  auto line = [&](const char *pszLabel, const std::string &value, ImU32 col) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_INK3), pszLabel);
    ImGui::SetCursorScreenPos(ImVec2(x + P(110.0f), y));
    ImGui::PushTextWrapPos(x + rw - ImGui::GetWindowPos().x + ImGui::GetScrollX());
    StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", value.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    y = std::max(y + P(22.0f), ImGui::GetItemRectMax().y + P(6.0f));
  };
  const Source &s = SourceOf(a);
  line("Frames", SourceWords(a), s.bOk ? C(TK_INK) : C(TK_WARN));
  if (a.nKind != ART_NONE && a.from != image)
    line("Whose", (a.nKind == ART_CLASSIC ? "the classic " : "") + StudioShowName(ST_IMAGES, a.from) + "'s", C(TK_INK2));
  const bool bPast = a.nFirst >= FrameCount(a) && FrameCount(a) > 0;
  line("Script", a.script + ", Init shows frame " + std::to_string(a.nFirst) +
                     (bPast ? ", past its frames: give it a script of its own (shown from frame 0)" : ""),
       bPast ? C(TK_WARN) : C(TK_INK2));
  std::string draw = std::to_string(a.nDraw);
  for (const auto &e : StudioEnum("draw_function"))
    if (e.first == a.nDraw)
      draw = Lower(e.second);
  line("Drawn", draw + (!a.bTurns ? ", does not turn"
                        : a.nKind == ART_SHEET && a.nDirs == 32 ? ", turns: all 32 facings drawn in the sheet"
                                                                 : ", turns: 17 facings drawn, the rest mirrored"),
       C(TK_INK2));
  y += P(6.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  UsedBy(ST_IMAGES, image, x + rw);
  y = ImGui::GetCursorScreenPos().y + P(4.0f);
  // the frames' file, and the sheet's slicing, for an image of the mod's own
  if (bMine) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (StudioButton("##bring", a.nKind == ART_SHEET || a.nKind == ART_GRP ? "Bring in other frames" : "Bring in a sheet", "plus", SB_GOLD,
                     28.0f, 0.0f, 13.0f))
      BringSheet(image);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("A PNG, WebP or TGA sheet of its frames, or a GRP, copied into the mod's art folder");
    if (StudioRecordJson(ST_IMAGES, image)["grp"].kind != PortJson::NONE) {
      ImGui::SameLine(0.0f, P(8.0f));
      if (StudioButton("##basefr", "Use its base's frames", "back", SB_PLAIN, 28.0f, 0.0f, 13.0f))
        StudioAsk("studio_set", "\"table\":\"images\",\"name\":" + Q(image) + ",\"field\":\"grp\",\"value\":null");
    }
    y = ImGui::GetItemRectMax().y + P(12.0f);
    ImGui::SetCursorScreenPos(ImVec2(x - P(10.0f), y));
    if (a.nKind == ART_SHEET)
      FieldColumn("##slicing", ST_IMAGES, image, {"grp", "frame", "origin", "directions", "frames", "scale", "team"}, rw + P(10.0f), true);
    else
      FieldColumn("##slicing", ST_IMAGES, image, {"grp"}, rw + P(10.0f), true);
    y = ImGui::GetItemRectMax().y + P(8.0f);
  } else if (rec.kind == SK_CLASSIC || rec.kind == SK_DEPENDENCY) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_INK3), rec.kind == SK_CLASSIC ? "Classic, read only: an image based on it can have its own frames."
                                                                                : "An image of a mod this one depends on, read only.",
      rw);
    y += P(26.0f);
    if (rec.kind == SK_CLASSIC && StudioIsOpen()) {
      ImGui::SetCursorScreenPos(ImVec2(x, y));
      if (StudioButton("##mk", "Make an image based on it", "copy", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs))
        StudioAsk("studio_create", "\"table\":\"images\",\"name\":" + Q(image + "_mod") + ",\"base\":" + Q(image));
      y += P(34.0f);
    }
  }
  y = std::max(y, leftBottom + P(16.0f));
  // the sheet with its slicing, or the frames one by one
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  if (a.nKind == ART_SHEET && s.bOk) {
    float hx = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, y, C(TK_HEAD), "The sheet") + P(10.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, hx, y + P(3.0f), C(TK_INK3),
      std::to_string(s.nAcross) + " across, frames of " + Fmt(s.cw / static_cast<double>(s.nScale)) + " x " + Fmt(s.ch / static_cast<double>(s.nScale)) +
          " game px, the origin marked in gold; a click shows a frame");
    if (bMine) {
      ImGui::SetCursorScreenPos(ImVec2(o.x + W - StudioButtonSize("Set the origin", "pen", 24.0f, kStudioFsXs).x, y - P(2.0f)));
      if (StudioButton("##origin", g_bOriginPick ? "Click where it stands" : "Set the origin", "pen", g_bOriginPick ? SB_ON : SB_PLAIN, 24.0f, 0.0f,
                       kStudioFsXs))
        g_bOriginPick = !g_bOriginPick;
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(30.0f)));
    SheetView(image, a, W, bLarge ? P(520.0f) : P(420.0f), bMine);
    y = ImGui::GetCursorScreenPos().y + P(8.0f);
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  }
  if (s.bOk) {
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    Animations(image, W);
    y = ImGui::GetCursorScreenPos().y + P(12.0f);
    float hx = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, y, C(TK_HEAD), "Its frames") + P(10.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, hx, y + P(3.0f), C(TK_INK3),
      !a.bTurns ? "a click shows one"
      : a.nKind == ART_SHEET && a.nDirs == 32 ? "32 a row, up round the right and back to up; a click shows one"
                                               : "17 a set, up round the right to down; a click shows one");
    ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(28.0f)));
    FrameGrid(a, W, bLarge ? P(360.0f) : P(260.0f));
    y = ImGui::GetItemRectMax().y + P(12.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::Dummy(ImVec2(1, 1));
}

void Title(int t, const std::string &name, const char *pszKind, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, o.y, C(TK_HI), StudioShowName(t, name), W);
  float cx = o.x;
  const float y = o.y + P(36.0f);
  StudioRecord rec;
  StudioRecordOf(t, name, &rec);
  cx += StudioChip(d, ImVec2(cx, y), pszKind) + P(6.0f);
  if (rec.kind == SK_CLASSIC)
    cx += StudioChip(d, ImVec2(cx, y), "classic, read only", CH_GOLD) + P(6.0f);
  else if (rec.kind == SK_DEPENDENCY)
    cx += StudioChip(d, ImVec2(cx, y), ("from " + rec.mod).c_str(), CH_GOLD) + P(6.0f);
  else if (!rec.base.empty())
    cx += StudioChip(d, ImVec2(cx, y), ("based on " + Short(t, rec.base)).c_str()) + P(6.0f);
  if (StudioChangedSinceSave(t, name) > 0)
    StudioChip(d, ImVec2(cx, y), "changed since the save", CH_CHANGED);
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(30.0f)));
  ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// A sprite's or a flingy's: what it shows, as a chain of records, and the picture.
void ChainHead(int t, const std::string &name, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const std::string sprite = t == ST_SPRITES ? name : RefOf(ST_FLINGY, name, "sprite", ST_SPRITES);
  const std::string image = sprite.empty() ? std::string() : SpriteImage(sprite);
  ViewFor(image);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const float pw = P(240.0f), ph = P(200.0f);
  PictureBox(o, ImVec2(o.x + pw, o.y + ph), image);
  ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + ph + P(8.0f)));
  ImGui::BeginGroup();
  FacingControl("##cf");
  ColourSwatches("##cc");
  ImGui::EndGroup();
  const float bottom = ImGui::GetItemRectMax().y;
  const float x = o.x + pw + P(22.0f);
  float y = o.y;
  T(d, STUDIO_FONT_BOLD, 16.0f, x, y, C(TK_HEAD), "What it shows");
  y += P(28.0f);
  if (t == ST_FLINGY) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(5.0f), C(TK_INK3), "Sprite");
    ImGui::SetCursorScreenPos(ImVec2(x + P(70.0f), y));
    OpenButton("##sprite", ST_SPRITES, sprite, "sprite");
    y += P(34.0f);
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(5.0f), C(TK_INK3), "Image");
  ImGui::SetCursorScreenPos(ImVec2(x + P(70.0f), y));
  OpenButton("##image", ST_IMAGES, image, "image");
  y += P(34.0f);
  if (!image.empty()) {
    const Art &a = ArtOf(image);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_INK2), SourceWords(a), W - pw - P(22.0f));
    y += P(26.0f);
  }
  if (t == ST_FLINGY) {
    const double v = StudioNumber(ST_FLINGY, name, "top_speed") / 256.0 * 24.0;
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y, C(TK_INK2), "Moves at up to " + Fmt(v) + " px a second at Fastest, " + Fmt(v / 32.0, 2) + " tiles");
    y += P(26.0f);
  }
  y += P(6.0f);
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  UsedBy(t, name, o.x + W);
  y = std::max(ImGui::GetCursorScreenPos().y, bottom + P(14.0f));
  d->AddLine(ImVec2(o.x, y), ImVec2(o.x + W, y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(6.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

// ---- the Art workspace ------------------------------------------------------------------------------

struct Workspace {
  int nTab = 0;  // 0 images, 1 sprites, 2 flingy, 3 the image script
  bool bClassic = false;
  char szFind[48] = {};
  std::string sel[3];
} g_ws;

const int kTabTable[3] = {ST_IMAGES, ST_SPRITES, ST_FLINGY};

// A list row: a little picture, the name, what kind of record.
bool ListRow(const std::string &name, int t, StudioKind kind, bool bSel, float w) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const float h = P(40.0f);
  ImGui::PushID(name.c_str());
  const bool bClick = ImGui::InvisibleButton("##row", ImVec2(w, h));
  const bool bHover = ImGui::IsItemHovered();
  ImGui::PopID();
  if (bSel)
    d->AddRectFilled(o, ImVec2(o.x + w, o.y + h), C(TK_SEL));
  else if (bHover)
    d->AddRectFilled(o, ImVec2(o.x + w, o.y + h), C(TK_ROW_HI));
  const std::string image = t == ST_IMAGES ? name : t == ST_SPRITES ? SpriteImage(name) : FlingyImage(name);
  const ImVec2 pa(o.x + P(4.0f), o.y + P(2.0f)), pb(o.x + P(40.0f), o.y + h - P(2.0f));
  d->AddRectFilled(pa, pb, C(TK_WELL), P(2.0f));
  if (!image.empty())
    DrawImage(d, pa, pb, image, -1, 12, g_view.nColour);
  if (kind == SK_OWN || kind == SK_EDIT)
    d->AddRectFilled(ImVec2(o.x, o.y + P(6.0f)), ImVec2(o.x + P(2.0f), o.y + h - P(6.0f)), C(TK_MOD));
  const float tx = o.x + P(48.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, tx, o.y + P(4.0f), bSel ? C(TK_HI) : C(TK_INK), StudioShowName(t, name), w - P(56.0f));
  const char *pszKind = kind == SK_OWN ? "the mod's" : kind == SK_EDIT ? "an edit" : kind == SK_DEPENDENCY ? "a dependency's" : "classic";
  std::string sub = pszKind;
  if (t == ST_IMAGES) {
    const Art &a = ArtOf(name);
    sub += a.nKind == ART_SHEET ? ", a sheet" : a.nKind == ART_GRP ? ", its own GRP" : a.nKind == ART_CLASSIC && kind != SK_CLASSIC ? ", classic frames" : "";
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, tx, o.y + P(22.0f), C(TK_INK3), sub, w - P(56.0f));
  return bClick;
}

void ArtList(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const int t = kTabTable[g_ws.nTab];
  StudioPane(d, a, b, 0.0f);
  ImGui::SetCursorScreenPos(ImVec2(a.x + P(8.0f), a.y + P(8.0f)));
  StudioInput("##find", g_ws.szFind, sizeof g_ws.szFind, (b.x - a.x - P(16.0f)) / P(1.0f), 26.0f, "Find", "search");
  ImGui::SetCursorScreenPos(ImVec2(a.x + P(8.0f), a.y + P(40.0f)));
  StudioCheck("##classic", "Classic ones too", &g_ws.bClassic);
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + P(70.0f)));
  ImGui::BeginChild("##list", ImVec2(b.x - a.x - 2, b.y - a.y - P(72.0f)));
  const float w = ImGui::GetContentRegionAvail().x;
  std::string &sel = g_ws.sel[g_ws.nTab];
  int nMod = 0;
  for (const StudioRecord &r : StudioRecords(t)) {
    if (!Has(r.name, g_ws.szFind) && !Has(StudioShowName(t, r.name), g_ws.szFind))
      continue;
    ++nMod;
    if (ListRow(r.name, t, r.kind, sel == r.name, w))
      sel = r.name;
  }
  if (nMod == 0 && !g_ws.bClassic) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), g_ws.szFind[0] ? "  Nothing of the mod's by that name." : "  The mod has none of its own yet.");
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "  Tick Classic ones too to look through classic's.");
  }
  if (g_ws.bClassic) {
    std::vector<int> rows;
    for (int n = 0; n < StudioClassicRows(t); ++n)
      if (const char *psz = StudioClassicName(t, n))
        if (g_ws.szFind[0] == '\0' || Has(psz, g_ws.szFind) || Has(StudioShowName(t, psz), g_ws.szFind))
          rows.push_back(n);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(rows.size()));
    while (clip.Step())
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const char *psz = StudioClassicName(t, rows[static_cast<size_t>(i)]);
        if (ListRow(psz, t, SK_CLASSIC, sel == psz, w))
          sel = psz;
      }
  }
  ImGui::EndChild();
}

void ArtDetail(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const int t = kTabTable[g_ws.nTab];
  const std::string &sel = g_ws.sel[g_ws.nTab];
  StudioPane(d, a, b, 0.0f);
  StudioRecord rec;
  if (sel.empty() || !StudioRecordOf(t, sel, &rec)) {
    const char *psz = t == ST_IMAGES ? "Pick an image to see its frames, bring in a sheet and slice it."
                                     : t == ST_SPRITES ? "Pick a sprite to see the image it shows." : "Pick a flingy to see what it moves.";
    T(d, STUDIO_FONT_TEXT, kStudioFs, a.x + P(24.0f), a.y + P(24.0f), C(TK_INK3), psz);
    return;
  }
  ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + 1));
  ImGui::BeginChild("##detail", ImVec2(b.x - a.x - 2, b.y - a.y - 2));
  const float W = ImGui::GetContentRegionAvail().x - P(44.0f);
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + P(22.0f), ImGui::GetCursorScreenPos().y + P(16.0f)));
  ImGui::BeginGroup();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  Title(t, sel, t == ST_IMAGES ? "image" : t == ST_SPRITES ? "sprite" : "flingy", W - P(200.0f));
  const ImVec2 after = ImGui::GetCursorScreenPos();
  const float bw = StudioButtonSize("Open as a document", "link", 26.0f, kStudioFsXs).x;
  ImGui::SetCursorScreenPos(ImVec2(o.x + W - bw, o.y));
  if (StudioButton("##opendoc", "Open as a document", "link", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs))
    StudioOpenDoc(t, sel, 0);
  ImGui::SetCursorScreenPos(after);
  if (t == ST_IMAGES)
    ImageBody(sel, W, true);
  else
    ChainHead(t, sel, W);
  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(1, P(20.0f)));
  ImGui::EndChild();
}

// ---- commands ---------------------------------------------------------------------------------------

std::string FreeName(int t, const std::string &want) {
  StudioRecord r;
  if (!StudioRecordOf(t, want, &r))
    return want;
  for (int i = 2; i < 100; ++i)
    if (!StudioRecordOf(t, want + "_" + std::to_string(i), &r))
      return want + "_" + std::to_string(i);
  return want + "_art";
}

std::string StatusFields(void) {
  return "\"unsaved\":" + std::to_string(StudioUnsaved()) + ",\"change\":" + std::to_string(StudioChange());
}

// studio_unit_art {unit, sender?}: the unit given art of its own, one edit undone together: an image
// of the mod's based on the one it shows, a sprite showing it, and a flingy showing the sprite, each
// made only where the unit's is not the mod's own already, and the unit's flingy set to it. Answers
// the records' names.
bool CmdUnitArt(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str(), sender = j["sender"].Str("claude");
  StudioRecord ru;
  if (!StudioRecordOf(ST_UNITS, unit, &ru)) {
    *pAnswer = "studio_unit_art needs a unit, by name";
    return false;
  }
  if (ru.kind != SK_OWN && ru.kind != SK_EDIT) {
    *pAnswer = unit + " is not the mod's: make a unit based on it, or an edit of it, first";
    return false;
  }
  const Chain c = ChainOf(unit);
  if (c.flingy.empty() || c.sprite.empty() || c.image.empty()) {
    *pAnswer = unit + "'s flingy, sprite or image does not resolve";
    return false;
  }
  const bool bFlingy = IsOwn(ST_FLINGY, c.flingy), bSprite = IsOwn(ST_SPRITES, c.sprite), bImage = IsOwn(ST_IMAGES, c.image);
  if (bFlingy && bSprite && bImage) {
    *pAnswer = "\"flingy\":" + Q(c.flingy) + ",\"sprite\":" + Q(c.sprite) + ",\"image\":" + Q(c.image) + ",\"made\":[]," + StatusFields();
    return true;
  }
  std::string why;
  bool bOk = true;
  std::string made;
  const std::string image = bImage ? c.image : FreeName(ST_IMAGES, unit);
  const std::string sprite = bSprite ? c.sprite : FreeName(ST_SPRITES, unit);
  const std::string flingy = bFlingy ? c.flingy : FreeName(ST_FLINGY, unit);
  const int nAt = StudioHistoryAt();
  StudioGroupBegin(sender, "gave " + unit + " art of its own");
  if (!bImage) {
    PortJson rec = StudioJsonObject();
    StudioJsonPut(rec, "base", StudioJsonString(c.image));
    bOk = StudioCreate(ST_IMAGES, image, rec, sender, &why);
    made += Q(image);
  }
  if (bOk && !bSprite) {
    PortJson rec = StudioJsonObject();
    StudioJsonPut(rec, "base", StudioJsonString(c.sprite));
    StudioJsonPut(rec, "image", StudioJsonString(image));
    bOk = StudioCreate(ST_SPRITES, sprite, rec, sender, &why);
    made += std::string(made.empty() ? "" : ",") + Q(sprite);
  } else if (bOk && !bImage) {
    bOk = StudioSetField(ST_SPRITES, sprite, "image", StudioJsonString(image), sender, std::string(), &why);
  }
  if (bOk && !bFlingy) {
    PortJson rec = StudioJsonObject();
    StudioJsonPut(rec, "base", StudioJsonString(c.flingy));
    StudioJsonPut(rec, "sprite", StudioJsonString(sprite));
    bOk = StudioCreate(ST_FLINGY, flingy, rec, sender, &why);
    made += std::string(made.empty() ? "" : ",") + Q(flingy);
    if (bOk)
      bOk = StudioSetField(ST_UNITS, unit, "flingy", StudioJsonString(flingy), sender, std::string(), &why);
  } else if (bOk && !bSprite) {
    bOk = StudioSetField(ST_FLINGY, flingy, "sprite", StudioJsonString(sprite), sender, std::string(), &why);
  }
  StudioGroupEnd();
  if (!bOk) {
    std::string undoWhy;
    if (StudioHistoryAt() > nAt)
      StudioUndo(&undoWhy);  // what was made of it taken back
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"flingy\":" + Q(flingy) + ",\"sprite\":" + Q(sprite) + ",\"image\":" + Q(image) + ",\"made\":[" + made + "]," + StatusFields();
  return true;
}

// A sheet's slicing guessed from its picture: square frames of a 17-wide or 32-wide row, or one
// frame the picture's size.
void GuessSlicing(int w, int h, int *pnFrame, int *pnDirs) {
  *pnDirs = 17;
  *pnFrame = 0;
  for (int dirs : {17, 32})
    if (w % dirs == 0 && h % (w / dirs) == 0) {
      *pnDirs = dirs;
      *pnFrame = w / dirs;
      return;
    }
  if (w == h)
    *pnFrame = w;
}

// studio_art_sheet {image, from? | file?, frame?, origin?, directions?, frames?, scale?, team?, sender?}:
// an image of the mod's given frames, one edit: a file from anywhere brought into the data folder's
// art (from), or one already there (file), as its grp; a sheet's slicing given, or for a sheet without
// any, guessed (square frames, 17 or 32 to a row).
bool CmdArtSheet(const PortJson &j, std::string *pAnswer) {
  const std::string image = j["image"].Str(), sender = j["sender"].Str("claude");
  StudioRecord r;
  if (!StudioRecordOf(ST_IMAGES, image, &r) || r.kind != SK_OWN) {
    *pAnswer = "studio_art_sheet needs an image of the mod's own, by name";
    return false;
  }
  std::string rel = j["file"].Str(), why;
  if (!j["from"].Str().empty() && !StudioImport(j["from"].Str(), "art", &rel, &why)) {
    *pAnswer = why;
    return false;
  }
  if (rel.empty()) {
    *pAnswer = "studio_art_sheet needs from (a file anywhere) or file (one in the data folder)";
    return false;
  }
  const bool bGrp = Lower(rel).size() > 4 && Lower(rel).compare(rel.size() - 4, 4, ".grp") == 0;
  const int nAt = StudioHistoryAt();
  StudioGroupBegin(sender, "gave " + image + " the frames of " + rel);
  bool bOk = StudioSetField(ST_IMAGES, image, "grp", StudioJsonString(rel), sender, std::string(), &why);
  std::string guessed;
  if (bOk && !bGrp) {
    const PortJson &own = StudioRecordJson(ST_IMAGES, image);
    PortJson frame = j["frame"];
    PortJson dirs = j["directions"];
    if (frame.kind == PortJson::NONE && own["frame"].kind == PortJson::NONE) {
      std::vector<unsigned char> bytes;
      int w = 0, h = 0, nFrame = 0, nDirs = 17;
      std::string err;
      if (PortReadFile(StudioDataFolder() + rel, &bytes, size_t(1) << 30) && PortImageInfo(bytes.data(), bytes.size(), rel, &w, &h, &err)) {
        GuessSlicing(w, h, &nFrame, &nDirs);
        const int nScale = std::max(1, static_cast<int>(j["scale"].Num(own["scale"].Num(1))));
        if (nFrame > 0) {
          frame = PortJson();
          frame.kind = PortJson::ARRAY;
          frame.items.push_back(StudioJsonNumber(nFrame / nScale));
          frame.items.push_back(StudioJsonNumber(nFrame / nScale));
          if (dirs.kind == PortJson::NONE && nDirs == 32)
            dirs = StudioJsonNumber(32);
          guessed = ",\"guessed\":{\"frame\":" + std::to_string(nFrame / nScale) + ",\"directions\":" + std::to_string(nDirs) + "}";
        }
      }
    }
    for (const char *psz : {"origin", "frames", "scale", "team"})
      if (bOk && j[psz].kind != PortJson::NONE)
        bOk = StudioSetField(ST_IMAGES, image, psz, j[psz], sender, std::string(), &why);
    if (bOk && frame.kind != PortJson::NONE)
      bOk = StudioSetField(ST_IMAGES, image, "frame", frame, sender, std::string(), &why);
    if (bOk && dirs.kind != PortJson::NONE)
      bOk = StudioSetField(ST_IMAGES, image, "directions", dirs, sender, std::string(), &why);
  }
  StudioGroupEnd();
  if (!bOk) {
    std::string undoWhy;
    if (StudioHistoryAt() > nAt)
      StudioUndo(&undoWhy);
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"image\":" + Q(image) + ",\"grp\":" + Q(rel) + guessed + "," + StatusFields();
  return true;
}

// studio_art_frame {image | sprite | flingy | unit, frame?, facing?, colour?}: what a picture is made
// of, as the studio draws it: the chain to the image, where its frames come from, their count, the
// frame its script starts at, and the frame a facing shows from it (or the frame given) with its size
// in pixels, its corner from the origin, whether it is mirrored and how many pixels are drawn.
bool CmdArtFrame(const PortJson &j, std::string *pAnswer) {
  std::string image = j["image"].Str(), chain;
  if (!j["unit"].Str().empty()) {
    StudioRecord r;
    if (!StudioRecordOf(ST_UNITS, j["unit"].Str(), &r)) {
      *pAnswer = "no unit " + j["unit"].Str();
      return false;
    }
    const Chain c = ChainOf(j["unit"].Str());
    image = c.image;
    chain = "\"flingy\":" + Q(c.flingy) + ",\"sprite\":" + Q(c.sprite) + ",\"subunit\":" + Q(c.subunit) + ",";
  } else if (!j["sprite"].Str().empty()) {
    image = SpriteImage(j["sprite"].Str());
  } else if (!j["flingy"].Str().empty()) {
    image = FlingyImage(j["flingy"].Str());
  }
  StudioRecord r;
  if (image.empty() || !StudioRecordOf(ST_IMAGES, image, &r)) {
    *pAnswer = "studio_art_frame needs an image, a sprite, a flingy or a unit that resolves to one";
    return false;
  }
  const Art &a = ArtOf(image);
  static const char *const kKind[] = {"none", "classic", "grp", "sheet"};
  const Source &s = SourceOf(a);
  std::string out = chain + "\"image\":" + Q(image) + ",\"source\":" + Q(kKind[a.nKind]) + ",\"file\":" + Q(a.file) + ",\"from\":" + Q(a.from) +
                    ",\"frames\":" + std::to_string(s.bOk ? s.nCount : 0) + ",\"first\":" + std::to_string(a.nFirst) +
                    ",\"turns\":" + (a.bTurns ? "true" : "false") + ",\"directions\":" + std::to_string(a.nKind == ART_SHEET ? a.nDirs : 17) +
                    ",\"script\":" + Q(a.script);
  if (a.nKind != ART_SHEET && s.bOk)
    out += ",\"group\":[" + std::to_string(s.gw) + "," + std::to_string(s.gh) + "]";
  if (a.nKind == ART_SHEET && s.bOk)
    out += ",\"sheet\":[" + std::to_string(s.w) + "," + std::to_string(s.h) + "],\"cell\":[" + std::to_string(s.cw) + "," +
           std::to_string(s.ch) + "],\"scale\":" + std::to_string(s.nScale);
  if (!s.bOk || !a.why.empty())
    out += ",\"why\":" + Q(!a.why.empty() ? a.why : s.why);
  bool bMirror = false;
  int nFrame;
  if (j["frame"].kind == PortJson::NUMBER)
    nFrame = static_cast<int>(j["frame"].d);
  else
    nFrame = FacingFrame(a, StartSet(a), static_cast<int>(j["facing"].Num(12)), &bMirror);
  Pic pic;
  if (s.bOk && MakePic(a, nFrame, static_cast<int>(j["colour"].Num(0)), &pic)) {
    const int nSet = !a.bTurns ? nFrame : a.nKind == ART_SHEET && a.nDirs == 32 ? nFrame / 32 * 17 : nFrame - nFrame % 17;
    const Rect e = SetExtent(a, nSet);
    out += ",\"frame\":" + std::to_string(nFrame) + ",\"mirrored\":" + (bMirror ? "true" : "false") + ",\"width\":" + std::to_string(pic.w) +
           ",\"height\":" + std::to_string(pic.h) + ",\"corner\":[" + Fmt(pic.ox, 2) + "," + Fmt(pic.oy, 2) + "],\"drawn\":" +
           std::to_string(pic.nDrawn) + ",\"extent\":[" + Fmt(e.l, 1) + "," + Fmt(e.t, 1) + "," + Fmt(e.r, 1) + "," + Fmt(e.b, 1) + "]";
    // the colours of its team pixels, to see the player's colour taken
    long long nTeam = 0;
    if (a.nKind != ART_SHEET) {
      GrpFrame f;
      std::vector<int> px;
      if (GrpFrameAt(s, nFrame, &f) && GrpPixels(s, f, &px))
        for (int v : px)
          nTeam += v >= 8 && v < 16;
      out += ",\"team_pixels\":" + std::to_string(nTeam);
    }
  } else if (s.bOk) {
    out += ",\"frame\":" + std::to_string(nFrame) + ",\"why_frame\":\"no such frame\"";
  }
  *pAnswer = out;
  return true;
}

// studio_art_view {facing?, colour?, turning?, set?, tab?, select?}: what the art's views show, as the
// modder would pick it: the facing (0 to 31), the player's colour (0 to 7), turning or still, the
// frame set (-1 where the script starts); the Art workspace's tab (images, sprites, flingy, scripts)
// and the record its list has picked. For grabs and tools.
bool CmdArtView(const PortJson &j, std::string *pAnswer) {
  if (j["facing"].kind == PortJson::NUMBER) {
    g_view.nFacing = static_cast<int>(j["facing"].d) & 31;
    g_view.bTurning = false;
  }
  if (j["colour"].kind == PortJson::NUMBER)
    g_view.nColour = static_cast<int>(j["colour"].d) & 7;
  if (j["turning"].kind == PortJson::BOOL)
    g_view.bTurning = j["turning"].b;
  if (j["set"].kind == PortJson::NUMBER)
    g_view.nSet = static_cast<int>(j["set"].d);
  static const char *const kTabs[] = {"images", "sprites", "flingy", "scripts"};
  for (int i = 0; i < 4; ++i)
    if (j["tab"].Str() == kTabs[i])
      g_ws.nTab = i;
  if (!j["select"].Str().empty() && g_ws.nTab < 3)
    g_ws.sel[g_ws.nTab] = j["select"].Str();
  if (j["classic"].kind == PortJson::BOOL)
    g_ws.bClassic = j["classic"].b;
  // the animation player: an animation by name, a tick, playing or not, and the view scrolled to it
  if (!j["anim"].Str().empty() || j["tick"].kind == PortJson::NUMBER || j["playing"].kind == PortJson::BOOL)
    studio_art::AnimSet(j["anim"].Str(), static_cast<int>(j["tick"].Num(-1)),
                        j["playing"].kind == PortJson::BOOL ? (j["playing"].b ? 1 : 0) : -1);
  // the state the script's conditional jumps test: {"range": 1, ...}, each a choice of the player's "as if"
  for (const PortJsonMember &m : j["as_if"].members)
    if (!studio_art::AnimAsIf(m.first, static_cast<int>(m.second.Num(0)))) {
      *pAnswer = "as_if has no choice " + m.first + ": random, carried, range, arc, facing or lifted";
      return false;
    }
  if (j["to_anim"].kind == PortJson::BOOL && j["to_anim"].b)
    g_nScrollTo = 2;
  if (j["to_top"].kind == PortJson::BOOL && j["to_top"].b)
    g_nScrollTo = 1;
  *pAnswer = "\"facing\":" + std::to_string(g_view.nFacing) + ",\"colour\":" + std::to_string(g_view.nColour) + ",\"turning\":" +
             (g_view.bTurning ? "true" : "false") + ",\"set\":" + std::to_string(g_view.nSet) + ",\"tab\":" + Q(kTabs[g_ws.nTab]);
  return true;
}

// studio_art_anim {image | unit, anim?, facing?}: an image's animations by name, and the one named run
// as the player runs it: each tick's frame set, facing and events (a attack, s sound, c spell, o order),
// the tick it goes on from at its end (-1 when it ends), the overlays it made, what it could not show.
bool CmdArtAnim(const PortJson &j, std::string *pAnswer) {
  std::string image = j["image"].Str();
  if (!j["unit"].Str().empty())
    image = ChainOf(j["unit"].Str()).image;
  StudioRecord r;
  if (image.empty() || !StudioRecordOf(ST_IMAGES, image, &r)) {
    *pAnswer = "studio_art_anim needs an image, or a unit that resolves to one";
    return false;
  }
  std::string out;
  if (!studio_art::AnimDescribe(image, j["anim"].Str("Init"), static_cast<int>(j["facing"].Num(12)) & 31, &out)) {
    *pAnswer = out;
    return false;
  }
  *pAnswer = "\"image\":" + Q(image) + "," + out;
  return true;
}

}  // namespace

// ---- what the animation player draws with (studio_art_anim.cpp) ----------------------------------------

namespace studio_art {

bool DrawImageAt(ImDrawList *d, ImVec2 a, ImVec2 b, const std::string &image, int nSet, int nFacing, bool bFlip, int nColour,
                 float *pK, ImVec2 *pOff, int nExtentSet) {
  const Art &art = ArtOf(image);
  if (art.nKind == ART_NONE || FrameCount(art) == 0)
    return false;
  if (*pK <= 0.0f) {
    Rect e = SetExtent(art, nExtentSet < 0 ? StartSet(art) : nExtentSet);
    if (e.Empty())
      e = Rect{-16, -16, 16, 16};
    const Place p = PlaceIn(a, b, e, art.nKind != ART_SHEET || SourceOf(art).nScale == 1);
    *pK = p.k;
    *pOff = ImVec2(p.origin.x - a.x, p.origin.y - a.y);
  }
  Place pl;
  pl.k = *pK;
  pl.origin = ImVec2(a.x + pOff->x, a.y + pOff->y);
  bool bMirror;
  const int n = FacingFrame(art, nSet < 0 ? StartSet(art) : nSet, nFacing, &bMirror);
  if (!art.bTurns)
    bMirror = bFlip;
  d->PushClipRect(a, b, true);
  const bool bDrawn = DrawFrame(d, art, n, bMirror, nColour, pl);
  d->PopClipRect();
  return bDrawn;
}

std::string ImageScriptOf(const std::string &image, bool *pbText) {
  const PortJson &script = ChainField(ST_IMAGES, image, "script");
  *pbText = script.kind == PortJson::STRING;
  return ArtOf(image).script;
}

bool ImageTurns(const std::string &image) {
  return ArtOf(image).bTurns;
}

int ImageFrames(const std::string &image) {
  return FrameCount(ArtOf(image));
}

int ImageStart(const std::string &image) {
  return StartSet(ArtOf(image));
}

// An overlay point of the image's .lo file of kind nKind at a frame set and facing (LoOffset).
bool ImageLoPoint(const std::string &image, int nKind, int nSet, int nFacing, int nPoint, ImVec2 *pOut) {
  const Art &art = ArtOf(image);
  bool bMirror;
  const int nFrame = FacingFrame(art, nSet < 0 ? StartSet(art) : nSet, nFacing, &bMirror);
  return LoOffset(image, art, nKind, nFrame, nPoint, bMirror, pOut);
}

bool GameFileRead(const char *pszName, std::vector<unsigned char> *pOut) {
  return GameFile(pszName, pOut);
}

int ViewColour(void) {
  return g_view.nColour;
}

int ViewFacingStill(void) {
  return g_view.nFacing;
}

std::string SpriteImageOf(const std::string &sprite) {
  return SpriteImage(sprite);
}

}  // namespace studio_art

// ---- the contract (studio_ui.h) ----------------------------------------------------------------------

bool StudioIconPicture(ImDrawList *pDraw, ImVec2 a, ImVec2 b, int nIcon, bool bDisabled) {
  if (pDraw == nullptr || nIcon < 0 || b.x - a.x < 2.0f || b.y - a.y < 2.0f)
    return false;
  TexTick();
  return DrawIconTex(pDraw, a, b, GrpIconTex(CmdIcons(), nIcon, bDisabled));
}

bool StudioIconPick(const char *pszId, int *pIcon, float fH, bool bEnabled) {
  ImGui::PushID(pszId);
  bool bPicked = false;
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float s = StudioPx(fH);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const std::string now = *pIcon < 0 ? std::string("none") : "icon " + std::to_string(*pIcon);
  const ImVec2 size = StudioButtonSize(now.c_str(), nullptr, fH, kStudioFsXs);
  const float w = s + StudioPx(8.0f) + size.x;
  if (!bEnabled)
    ImGui::BeginDisabled();
  const bool bClick = ImGui::InvisibleButton("##icon", ImVec2(w, s));
  const bool bHover = ImGui::IsItemHovered();
  if (!bEnabled)
    ImGui::EndDisabled();
  d->AddRectFilled(p, ImVec2(p.x + w, p.y + s), StudioCol(bHover ? TK_ROW_HI : TK_WELL), StudioPx(3.0f));
  d->AddRect(p, ImVec2(p.x + w, p.y + s), StudioCol(TK_LINE), StudioPx(3.0f));
  StudioIconPicture(d, ImVec2(p.x + StudioPx(2.0f), p.y + StudioPx(2.0f)), ImVec2(p.x + s - StudioPx(2.0f), p.y + s - StudioPx(2.0f)), *pIcon);
  StudioText(d, STUDIO_FONT_TEXT, kStudioFsXs, ImVec2(p.x + s + StudioPx(6.0f), p.y + (s - StudioPx(kStudioFsXs)) * 0.5f),
             StudioCol(*pIcon < 0 ? TK_INK3 : TK_INK2), now.c_str());
  if (bClick && bEnabled)
    ImGui::OpenPopup("##icons");
  ImGui::SetNextWindowSize(ImVec2(StudioPx(12 * 40.0f + 28.0f), StudioPx(440.0f)));
  if (ImGui::BeginPopup("##icons")) {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, StudioCol(TK_INK2), "The classic command icons: pick one");
    if (StudioButton("##none", "None", "x", *pIcon < 0 ? SB_ON : SB_PLAIN, 24.0f, 0.0f, kStudioFsXs)) {
      bPicked = *pIcon != -1;
      *pIcon = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::BeginChild("##grid", ImVec2(0.0f, 0.0f), false);
    const float c = StudioPx(40.0f);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList *g = ImGui::GetWindowDrawList();
    int nShown = 0;
    for (int i = 0; i < 1024; ++i) {
      const ImVec2 q(o.x + (i % 12) * c, o.y + (i / 12) * c);
      ImGui::SetCursorScreenPos(q);
      ImGui::PushID(i);
      const bool bCell = ImGui::InvisibleButton("##c", ImVec2(c - StudioPx(4.0f), c - StudioPx(4.0f)));
      const bool bOver = ImGui::IsItemHovered();
      ImGui::PopID();
      const ImVec2 qb(q.x + c - StudioPx(4.0f), q.y + c - StudioPx(4.0f));
      g->AddRectFilled(q, qb, StudioCol(i == *pIcon ? TK_SEL : bOver ? TK_ROW_HI : TK_WELL), StudioPx(3.0f));
      if (!StudioIconPicture(g, ImVec2(q.x + StudioPx(2.0f), q.y + StudioPx(2.0f)), ImVec2(qb.x - StudioPx(2.0f), qb.y - StudioPx(2.0f)), i)) {
        // the sheet's end: its frames are drawn in order, so the first one missing past the start ends it
        if (i > 0)
          break;
        continue;
      }
      ++nShown;
      if (i == *pIcon)
        g->AddRect(q, qb, StudioCol(TK_SEL_LINE), StudioPx(3.0f), 0, StudioPx(2.0f));
      if (bOver)
        ImGui::SetTooltip("icon %d", i);
      if (bCell) {
        bPicked = *pIcon != i;
        *pIcon = i;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + ((nShown + 11) / 12) * c));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return bPicked;
}

bool StudioUnitIconPicture(ImDrawList *pDraw, ImVec2 a, ImVec2 b, const std::string &unit, bool bDisabled) {
  if (pDraw == nullptr || b.x - a.x < 2.0f || b.y - a.y < 2.0f)
    return false;
  TexTick();
  std::string file;
  int nRow = -1;
  if (!ConsoleArtOf(unit, "icon", &file, &nRow))
    return false;
  if (file.empty())
    return StudioIconPicture(pDraw, a, b, nRow, bDisabled);
  if (IsGrpName(file)) {
    Art g;
    g.nKind = ART_GRP;
    g.file = file;
    g.key = "g:" + file;
    return DrawIconTex(pDraw, a, b, GrpIconTex(g, 0, bDisabled));
  }
  return DrawIconTex(pDraw, a, b, PngIconTex(file, bDisabled));
}

std::string StudioUnitArtWords(const std::string &unit) {
  const Chain c = ChainOf(unit);
  if (c.image.empty())
    return "no image to draw";
  const Art &a = ArtOf(c.image);
  switch (a.nKind) {
  case ART_SHEET: return "its own sheet";
  case ART_GRP: return "its own GRP";
  case ART_CLASSIC: return "the " + Short(ST_IMAGES, a.from) + "'s frames";
  default: return "no frames";
  }
}

bool StudioUnitPicture(ImDrawList *pDraw, ImVec2 a, ImVec2 b, const std::string &unit, int nFacing, int nColour) {
  if (pDraw == nullptr || b.x - a.x < 4.0f || b.y - a.y < 4.0f)
    return false;
  return DrawUnit(pDraw, a, b, unit, nFacing, nColour);
}

void StudioArtWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  SheetPicked();
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, a.x + P(20.0f), a.y + P(20.0f), C(TK_INK3), "Open a mod to see its art.");
    return;
  }
  // the bar: which records, and the view's colour
  float x = a.x + P(12.0f);
  const float bh = P(44.0f);
  Wide(d, 11.0f, x, Mid(a.y, bh, 11.0f), C(TK_HEAD), "Art");
  x += P(56.0f);
  static const char *const kTabs[] = {"Images", "Sprites", "Flingy", "Image scripts"};
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + P(9.0f)));
  g_ws.nTab = StudioSeg("##arttab", kTabs, 4, g_ws.nTab, 26.0f);
  if (g_ws.nTab < 3) {
    ImGui::SameLine(0.0f, P(18.0f));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, a.y + P(12.0f)));
    ColourSwatches("##wscolour");
  }
  const ImVec2 ca(a.x + P(8.0f), a.y + bh), cb(b.x - P(8.0f), b.y - P(8.0f));
  if (g_ws.nTab == 3) {
    studio_art::ScriptPane(ca, cb);
    return;
  }
  const float lw = std::min(P(320.0f), (cb.x - ca.x) * 0.35f);
  ArtList(ca, ImVec2(ca.x + lw, cb.y));
  ArtDetail(ImVec2(ca.x + lw + P(8.0f), ca.y), cb);
}

void StudioArtHead(int t, const std::string &name) {
  SheetPicked();
  StudioRecord rec;
  if (!StudioRecordOf(t, name, &rec))
    return;
  const float w = ImGui::GetContentRegionAvail().x;
  ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(22.0f), o.y + P(16.0f)));
  const float W = w - P(44.0f);
  ImGui::BeginGroup();
  Title(t, name, t == ST_IMAGES ? "image" : t == ST_SPRITES ? "sprite" : "flingy", W);
  if (t == ST_IMAGES)
    ImageBody(name, W, false);
  else
    ChainHead(t, name, W);
  ImGui::EndGroup();
  const float y = ImGui::GetItemRectMax().y + P(6.0f);
  ImGui::GetWindowDrawList()->AddLine(ImVec2(o.x + P(22.0f), y), ImVec2(o.x + P(22.0f) + W, y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

void StudioUnitArt(const std::string &unit) {
  SheetPicked();
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioRecord ru;
  if (!StudioRecordOf(ST_UNITS, unit, &ru))
    return;
  const bool bMine = ru.kind == SK_OWN || ru.kind == SK_EDIT;
  const Chain c = ChainOf(unit);
  ViewFor(c.image);
  if (g_nScrollTo == 1) {
    ImGui::SetScrollY(0.0f);
    g_nScrollTo = 0;
  }
  const float W = ImGui::GetContentRegionAvail().x - P(22.0f);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  // the chain: flingy, sprite, image, each opening its record
  float y = o.y;
  float hx = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, y, C(TK_HEAD), "What it is drawn with") + P(10.0f);
  T(d, STUDIO_FONT_TEXT, kStudioFsS, hx, y + P(3.0f), C(TK_INK3), "it moves as its flingy, which shows a sprite, which plays an image's frames");
  y += P(30.0f);
  float x = o.x;
  auto link = [&](const char *pszId, const char *pszWord, int t, const std::string &name, const char *pszGlyph, bool bLast) {
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y + P(6.0f), C(TK_INK3), pszWord);
    x += TW(STUDIO_FONT_TEXT, kStudioFsXs, pszWord) + P(6.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    OpenButton(pszId, t, name, pszGlyph);
    x = ImGui::GetItemRectMax().x + P(8.0f);
    if (!bLast) {
      StudioGlyph(d, "caret", ImVec2(x, y + P(5.0f)), P(16.0f), C(TK_INK3));
      x += P(24.0f);
    }
  };
  link("##fl", "flingy", ST_FLINGY, c.flingy, "link", false);
  link("##sp", "sprite", ST_SPRITES, c.sprite, "sprite", false);
  link("##im", "image", ST_IMAGES, c.image, "image", true);
  y += P(40.0f);
  // the picture large, its controls under it
  const float pw = std::min(P(420.0f), W * 0.42f), ph = P(330.0f);
  PictureBox(ImVec2(o.x, y), ImVec2(o.x + pw, y + ph), c.image, unit);
  ImGui::SetCursorScreenPos(ImVec2(o.x, y + ph + P(8.0f)));
  ImGui::BeginGroup();
  FacingControl("##uf");
  ColourSwatches("##uc");
  if (!c.image.empty())
    SetControl(ArtOf(c.image));
  ImGui::EndGroup();
  const float leftBottom = ImGui::GetItemRectMax().y;
  // at the right: whose art it is, making its own, giving it a sheet
  const float rx = o.x + pw + P(24.0f), rw = W - pw - P(24.0f);
  float ry = y;
  const Art &a = ArtOf(c.image);
  const bool bOwnArt = !c.image.empty() && IsOwn(ST_IMAGES, c.image) && IsOwn(ST_SPRITES, c.sprite) && IsOwn(ST_FLINGY, c.flingy);
  T(d, STUDIO_FONT_BOLD, 15.0f, rx, ry, C(TK_HI), bOwnArt ? "Art of its own" : "Borrowed art");
  ry += P(26.0f);
  ImGui::SetCursorScreenPos(ImVec2(rx, ry));
  ImGui::PushTextWrapPos(rx + rw - ImGui::GetWindowPos().x + ImGui::GetScrollX());
  StudioPushFont(STUDIO_FONT_TEXT, kStudioFsS);
  ImGui::PushStyleColor(ImGuiCol_Text, C(TK_INK));
  std::string words;
  if (c.image.empty())
    words = "Its chain does not reach an image, so nothing is drawn.";
  else if (a.nKind == ART_CLASSIC)
    words = "It is drawn with the classic " + StudioShowName(ST_IMAGES, a.from) + "'s frames (" + a.file + ").";
  else if (a.nKind == ART_GRP)
    words = "It is drawn with " + a.file + ", a GRP of the mod's.";
  else if (a.nKind == ART_SHEET)
    words = "It is drawn from the sheet " + a.file + ".";
  if (!c.image.empty() && !bOwnArt)
    words += " Its image is shared, so new frames need art of its own: an image, a sprite and a flingy of the mod's, based on these.";
  ImGui::TextWrapped("%s", words.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
  ry = ImGui::GetItemRectMax().y + P(12.0f);
  ImGui::SetCursorScreenPos(ImVec2(rx, ry));
  if (!bOwnArt) {
    if (bMine) {
      if (StudioButton("##own", "Make its own art", "image", SB_GOLD, 30.0f, 0.0f, 13.0f, !c.image.empty()))
        StudioAsk("studio_unit_art", "\"unit\":" + Q(unit));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("An image, a sprite and a flingy of the mod's, based on these, in one edit");
    } else {
      // a classic unit keeps classic's art: a unit of the mod's based on it can have its own
      if (StudioButton("##newunit", "New unit based on it", "plus", SB_PLAIN, 30.0f, 0.0f, 13.0f, StudioIsOpen()))
        StudioNewUnitOpen(unit);
      ImGui::SameLine(0.0f, P(10.0f));
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + P(7.0f)));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), "a classic unit keeps classic's art");
    }
    ry += P(40.0f);
  } else {
    if (StudioButton("##sheet", a.nKind == ART_CLASSIC ? "Bring in a sheet" : "Bring in other frames", "plus", SB_GOLD, 30.0f, 0.0f, 13.0f))
      BringSheet(c.image);
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##slice", "Its image and slicing", "image", SB_PLAIN, 30.0f, 0.0f, 13.0f))
      StudioOpenDoc(ST_IMAGES, c.image, 0);
    ry += P(40.0f);
    if (a.nKind != ART_CLASSIC) {
      ImGui::SetCursorScreenPos(ImVec2(rx, ry));
      T(d, STUDIO_FONT_TEXT, kStudioFsS, rx, ry, C(TK_INK2), SourceWords(a), rw);
      ry += P(24.0f);
    }
  }
  if (!c.subunit.empty()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, rx, ry + P(5.0f), C(TK_INK3), "Turret");
    ImGui::SetCursorScreenPos(ImVec2(rx + P(70.0f), ry));
    OpenButton("##sub", ST_UNITS, c.subunit, "unit");
    ry += P(36.0f);
  }
  // its console pictures
  ry += P(8.0f);
  T(d, STUDIO_FONT_BOLD, 15.0f, rx, ry, C(TK_HI), "On the console");
  ry += P(26.0f);
  ry += ConsoleRow(d, ImVec2(rx, ry), unit) + P(6.0f);
  ImGui::SetCursorScreenPos(ImVec2(rx - P(10.0f), ry));
  FieldColumn("##console", ST_UNITS, unit, {"icon", "wireframe", "transport_wireframe"}, rw + P(10.0f), bMine);
  ry = ImGui::GetItemRectMax().y;
  y = std::max(leftBottom, ry) + P(20.0f);
  // its animations, then the image's frames one by one
  if (!c.image.empty() && FrameCount(a) > 0) {
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    Animations(c.image, W);
    y = ImGui::GetCursorScreenPos().y + P(12.0f);
  }
  if (!c.image.empty() && FrameCount(a) > 0) {
    float fx = o.x + T(d, STUDIO_FONT_BOLD, 16.0f, o.x, y, C(TK_HEAD), "Its frames") + P(10.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsS, fx, y + P(3.0f), C(TK_INK3), SourceWords(a) + "; a click shows one", W - (fx - o.x));
    ImGui::SetCursorScreenPos(ImVec2(o.x, y + P(28.0f)));
    FrameGrid(a, W, P(300.0f));
    y = ImGui::GetItemRectMax().y + P(8.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::Dummy(ImVec2(1, 1));
}

void StudioArtInit(void) {
  PortControlCommand("studio_unit_art", CmdUnitArt);
  PortControlCommand("studio_art_sheet", CmdArtSheet);
  PortControlCommand("studio_art_frame", CmdArtFrame);
  PortControlCommand("studio_art_view", CmdArtView);
  PortControlCommand("studio_art_anim", CmdArtAnim);
}
