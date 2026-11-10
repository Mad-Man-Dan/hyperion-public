// port_looks.cpp -- looks: a unit's images drawn past their own colours
// (port_looks.h). Presentation only.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "port_looks.h"
#include "port_present.h"           // PortPresentRedraw
#include "port_scene.h"             // PortSceneLook
#include "port_teamcolor.h"         // PortRgbToLab
#include "../data/port_mods.h"      // PortMods, PortModActive: the active mod's folder
#include "../platform/port_json.h"
#include "../platform/port_log.h"   // PortLogger
#include "../platform/port_paths.h" // PortDataFolder
#include "../platform/port_util.h"  // PortReadFile
#include "../tools/port_control.h"  // PortControlFloat, PortControlCommand
#include "../../h/CUnit.h"
#include "../../h/CSprite.h"
#include "../../h/CImage.h"
#include "../../h/CThingy.h"        // the fogged copies
#include "../../h/grp.h"            // GROUP: an image's canvas
#include "../../h/poollimits.h"     // POOL_UNITS_STORE, g_pools
#include "../../h/typelimits.h"     // UNIT_TYPES_MAX, IMAGE_TYPES_MAX
#include "../../gen/globals117.h"   // sgGamePalette_g, _gbRemapColorTbl, glScrollPixelX
#include "../data/port_moddata.h"  // ModTypeByName

namespace {

// The palette entries a look takes, and what it is made of on the GPU.
// The rules past the first three: none keeps the image's colours, for a look that is only an
// aura; reds, golds, greys and skins; a band of hue; and a zone's own entries. The materials past
// the first two: paint, the ramp at the tone as the art is shaded, and glow, the ramp added as
// light.
enum { RULE_BLUES, RULE_GREENS, RULE_ALL, RULE_NONE, RULE_REDS, RULE_GOLDS, RULE_GREYS, RULE_SKINS, RULE_HUE, RULE_ENTRIES, RULES };
enum { MAT_CRYSTAL, MAT_GAS, MAT_PAINT, MAT_GLOW, MATERIALS };
enum { RAMP = 6, LOOKS_MAX = 254, ZONES = 4 };
const char *const kRules[] = {"blues", "greens", "all", "none", "reds", "golds", "greys", "skins", "hue", "entries"};
const char *const kMaterials[] = {"crystal", "gas", "paint", "glow"};

// A zone of a look's colours: the entries it takes, onto its ramp by their tone, spread over the
// zone's own lightness or kept at theirs, and how much of its colour shows over the image's own.
// The first zone's rule, material and ramp are the look's own (nRule, nMaterial, adwRamp).
struct Zone {
  int nRule = RULE_NONE;
  int nMaterial = MAT_PAINT;
  unsigned int adwRamp[RAMP] = {};
  float afHue[5] = {0.0f, 360.0f, 0.02f, 0.0f, 1.0f};  // from and to in degrees, the least chroma, the lightness between
  std::vector<int> entries;                          // RULE_ENTRIES's
  std::vector<int> except;                           // never taken
  float fMix = 1.0f;
  bool bKeep = false;
};

// An aura's numbers as looks.json names them, the least and most of each, and whether it is whole.
enum { AU_REACH, AU_WIDTH, AU_STRENGTH, AU_CRACKLE, AU_SPEED, AU_PULSE, AU_SPARKS, AU_GAP, AU_FALLOFF, AU_INNER,
       AU_OPACITY, AU_PULSE_RATE, AU_FLICKER, AU_EMBERS, AU_BANDS, AU_BAND_SPEED, AU_STEPS, AU_CRACKLE_SIZE, AU_BLOCK,
       AU_FIELDS };
struct AuraField {
  const char *pszName;
  float fLo, fHi;
  bool bWhole;
};
const AuraField kAura[AU_FIELDS] = {
    {"reach", 0, 8, true},      {"width", 0.5f, 3, false},     {"strength", 0, 2, false},  {"crackle", 0, 1, false},
    {"speed", 0, 4, false},     {"pulse", 0, 1, false},        {"sparks", 0, 1, false},    {"gap", 0, 3, true},
    {"falloff", 0.5f, 3, false}, {"inner", 0, 1, false},       {"opacity", 0, 1, false},   {"pulseRate", 0.1f, 4, false},
    {"flicker", 0, 1, false},   {"embers", 0, 1, false},       {"bands", 0, 8, true},      {"bandSpeed", -4, 4, false},
    {"steps", 2, 8, true},      {"crackleSize", 0.25f, 4, false}, {"block", 1, 2, true}};
// its colours by the line's distance (fixed: the glow's and the line's; ramp: line, middle, glow
// outward; team: the player's; cycle: the hue turning), and how they go over what is under
enum { AURA_FIXED, AURA_RAMP, AURA_TEAM, AURA_CYCLE, AURA_MODES };
// how many auras a unit shows at once, so three looks that each bring one all show
enum { AURAS_MAX = 3 };
const char *const kAuraModes[] = {"fixed", "ramp", "team", "cycle"};
const char *const kAuraBlends[] = {"add", "over"};
// how far a look's embers rise past an image's top
const int kEmberRise = 12;

// A vent of an image a plume rises from: its place in the image's canvas, how strong it is
// and how wide (the design's numbers, design/41-rich-bases/richfx.js), and the
// frame it is for, -1 for any. An image's vents for the frame drawn stand in for its others.
struct Vent {
  int nImage;
  float x, y, fStrength, fWidth;
  int nFrame;
};

struct Look {
  std::string name;
  int nRule = RULE_ALL;
  int nMaterial = MAT_CRYSTAL;
  unsigned int adwRamp[RAMP] = {};  // 0xRRGGBB, dark to light
  std::vector<int> images;          // the images it covers, in order; none for all of a sprite's
  std::vector<int> units;           // the unit types that have it
  std::vector<int> replaced;        // the images its plume replaces in full colour, in order
  float afMat[4] = {};              // clarity, shine, glow, sparkle, 0 to 1
  float afPlume[3] = {};            // height in pixels, density, rise in pixels a second
  unsigned int dwLight = 0;         // the ground light's colour
  float afLight[4] = {};            // its radius and strength, and its centre from the unit's place
  std::vector<Vent> vents;
  int nZones = 1;
  Zone az[ZONES];        // [0]'s rule, material and ramp are the look's own above
  bool bCircle = false;  // a selection's mark takes it too
  // the aura: its glow, middle and line colours, its numbers (kAura's), its mode and blend, whether
  // only the sprite's main image has it, its dither, and whether its glow goes over other sprites
  unsigned int adwAura[3] = {0x40FF60, 0x9FFFA0, 0xEAFFEA};
  float afAura[AU_FIELDS] = {0, 1, 1, 0.5f, 1, 0.3f, 0.3f, 0, 1, 0.3f, 1, 1, 0, 0, 0, 1, 4, 1, 1};
  int nAuraMode = AURA_FIXED;
  int nAuraBlend = 0;
  bool bAuraMain = false, bAuraDither = true, bAuraOver = false;
  bool bLive = false;     // made or changed by the channel: kept from game to game
  BYTE abZone[256] = {};  // the zone that takes each entry
  // made for the palette in g_abPalette
  bool abTakes[256] = {};
  BYTE abRemap[256] = {};
  BYTE abTone[256] = {};  // 0 where the rule does not take the entry, else 1 + its tone * 254
  BYTE bPen = 0;
};

std::vector<Look> g_looks;  // [0] is none
std::string g_source;       // where the looks past the built-in ones came from
BYTE g_abPalette[1024];     // the palette the looks were last made for
bool g_bMade;
unsigned char g_abUnitLook[POOL_UNITS_STORE];  // given by hand
unsigned char g_abTypeLook[UNIT_TYPES_MAX];    // given by type
unsigned char g_abTypeAura[UNIT_TYPES_MAX][AURAS_MAX - 1];  // the auras of the looks before it
PFN_PORT_LOOK_OF_UNIT g_pfnProvider;

bool HasAura(const Look &l);
bool Paints(const Look &l);

struct Looked {
  const CSprite *pSprite;
  int nLook;
  int x, y, nType;  // a unit's place and type, for a fogged copy and the minimap
  bool bUnit;
  int nAura;  // the looks its auras are from, one a byte, 0 for none; nLook is its paint's
};
std::vector<Looked> g_looked;  // this pass's, in the order of the sprites
BYTE g_abSaved[256];
unsigned int g_nNow;  // PortLookNow's
unsigned char g_abPlayerLook[12];  // given by player (look_player)
unsigned int g_nNowAura;           // PortLookAuraNow's look, the first of g_anNowAura
unsigned int g_anNowAura[AURAS_MAX];  // the looks of the auras of the image drawn now
int g_nMainImage = -1;             // the main image of the sprite drawn now
float g_fAura = 1.0f, g_fAuraSpeed = 1.0f;

// The live values (port_looks.h)
float g_fOn = 1.0f, g_fSteps = 1.0f;
float g_fClarity = 1.0f, g_fShine = 1.0f, g_fGlow = 1.0f, g_fSparkle = 1.0f, g_fPlume = 1.0f, g_fLight = 1.0f;

std::vector<unsigned char> g_gpu;  // PortLooksGpu's

const PortLogger Log("hyperion_mods.log");

// 1.17's mineral fields, its geyser, and the buildings on one; what a worker carries (the
// mineral chunks, the protoss orbs, the zerg sacs and the terran tanks, each with its smaller
// one); and the geyser's puffs.
const int kGoldImages[] = {347, 349, 351, 0x18D, 0x18E};
const int kVioletImages[] = {93, 158, 307, 344, 0x18F, 0x190, 0x191, 0x192, 0x193, 0x194,
                             430, 431, 432, 433, 434, 435, 436, 437, 438, 439};
const int kPuffs[] = {430, 431, 432, 433, 434, 435, 436, 437, 438, 439};
// the geyser's three vents, and those of the refinery, the assimilator and the extractor on it.
// The geyser's frame is the tileset's: space platform's and installation's (1 and 2) are the
// others' drawn 6 pixels to the right, ash world's (3) is drawn apart; each vent is at its
// crater's mouth, set after play (2026-10-26) where the design's sat left of and below it
const Vent kVents[] = {
    {344, 73, 19, 1.0f, 4.5f, -1},   {344, 91, 40, 0.45f, 2.5f, -1},  {344, 25, 29, 0.5f, 2.5f, -1},
    {344, 79, 19, 1.0f, 4.5f, 1},    {344, 97, 40, 0.45f, 2.5f, 1},   {344, 31, 29, 0.5f, 2.5f, 1},
    {344, 79, 19, 1.0f, 4.5f, 2},    {344, 97, 40, 0.45f, 2.5f, 2},   {344, 31, 29, 0.5f, 2.5f, 2},
    {344, 74, 16, 1.0f, 4.5f, 3},    {344, 95, 35, 0.45f, 2.5f, 3},   {344, 25, 24, 0.5f, 2.5f, 3},
    {307, 103, 39, 1.0f, 4.5f, -1},  {158, 53, 50, 0.75f, 3.0f, -1},  {158, 123, 41, 0.75f, 3.0f, -1},
    {158, 137, 80, 0.7f, 3.0f, -1},  {93, 64, 50, 1.0f, 4.0f, -1},    {93, 23, 68, 0.4f, 2.5f, -1},
    {93, 107, 92, 0.4f, 2.5f, -1}};
// the design's ramps
const unsigned int kGold[RAMP] = {0x2C1403, 0x6E3A06, 0xB8740E, 0xEAA81C, 0xFFD64A, 0xFFF3B8};
const unsigned int kViolet[RAMP] = {0x1C0628, 0x4C1166, 0x8C2EB4, 0xC35EE8, 0xEBA6FF, 0xFFF0FF};

// A new look as the design's gold: clarity 38, shine 55, glow 50, sparkle 55; a plume 54 high at
// 62, with no vents; a light of radius 40 at three quarters' strength below the unit's place (the
// design's half, raised after play: "The ground light is too faint, make it stronger just a bit").
Look Fresh(const char *pszName) {
  Look l;
  l.name = pszName;
  memcpy(l.adwRamp, kGold, sizeof l.adwRamp);
  const float afMat[4] = {0.38f, 0.55f, 0.50f, 0.55f};
  memcpy(l.afMat, afMat, sizeof l.afMat);
  l.afPlume[0] = 54.0f;
  l.afPlume[1] = 0.62f * 1.6f;
  l.afPlume[2] = 20.0f;
  l.dwLight = kGold[4];
  l.afLight[0] = 40.0f;
  l.afLight[1] = 0.75f;
  l.afLight[3] = 8.0f;
  for (int e = 0; e < 256; e++)
    l.abRemap[e] = static_cast<BYTE>(e);
  for (Zone &z : l.az)
    memcpy(z.adwRamp, kGold, sizeof z.adwRamp);
  return l;
}

// The built-in looks (design/41-rich-bases/richfx.js).
void Builtins(void) {
  g_looks.assign(1, Look());
  Look gold = Fresh("rich-gold");
  gold.nRule = RULE_BLUES;
  gold.images.assign(kGoldImages, kGoldImages + sizeof kGoldImages / sizeof kGoldImages[0]);
  g_looks.push_back(gold);
  // the gas lights the ground wider and stronger, from a little right of the geyser and above
  // its middle, in the ramp's middle violet; its plume replaces 1.17's puffs
  Look violet = Fresh("rich-violet");
  violet.nRule = RULE_GREENS;
  violet.nMaterial = MAT_GAS;
  memcpy(violet.adwRamp, kViolet, sizeof violet.adwRamp);
  violet.images.assign(kVioletImages, kVioletImages + sizeof kVioletImages / sizeof kVioletImages[0]);
  violet.dwLight = kViolet[3];
  violet.afLight[0] = 74.0f;
  violet.afLight[1] = 1.25f;
  violet.afLight[2] = 6.0f;
  violet.afLight[3] = -4.0f;
  violet.replaced.assign(kPuffs, kPuffs + sizeof kPuffs / sizeof kPuffs[0]);
  violet.vents.assign(kVents, kVents + sizeof kVents / sizeof kVents[0]);
  g_looks.push_back(violet);
  std::sort(g_looks[1].images.begin(), g_looks[1].images.end());
  std::sort(g_looks[2].images.begin(), g_looks[2].images.end());
  // a green charge round a unit, its colours left as they are
  Look aura = Fresh("aura-green");
  aura.nRule = RULE_NONE;
  memset(aura.afMat, 0, sizeof aura.afMat);
  aura.dwLight = 0x40FF60;
  aura.afLight[0] = 28.0f;
  aura.afLight[1] = 0.45f;
  aura.afAura[AU_REACH] = 4.0f;
  g_looks.push_back(aura);
  g_bMade = false;
}

// A type's look is the last that names it and paints it, else the last that names it. The auras of
// the other looks that name it are kept beside it, up to two, so a unit made of several looks
// shows each of their auras.
void Reindex(void) {
  memset(g_abTypeLook, 0, sizeof g_abTypeLook);
  memset(g_abTypeAura, 0, sizeof g_abTypeAura);
  for (size_t i = 1; i < g_looks.size(); i++)
    for (int t : g_looks[i].units) {
      const unsigned char bWas = g_abTypeLook[t];
      if (bWas == 0 || Paints(g_looks[i]) || !Paints(g_looks[bWas]))
        g_abTypeLook[t] = static_cast<unsigned char>(i);
    }
  for (size_t i = 1; i < g_looks.size(); i++)
    for (int t : g_looks[i].units) {
      if (static_cast<int>(i) == g_abTypeLook[t] || !HasAura(g_looks[i]))
        continue;
      for (int k = 0; k < AURAS_MAX - 1; k++)
        if (g_abTypeAura[t][k] == 0) {
          g_abTypeAura[t][k] = static_cast<unsigned char>(i);
          break;
        }
    }
  g_bMade = false;
}

// looks.json: the active mod's, else the data folder's.
std::string FilePath(void) {
  const std::vector<PortMod> &mods = PortMods();
  const int nActive = PortModActive();
  if (nActive >= 0 && nActive < static_cast<int>(mods.size())) {
    const std::string folder = mods[nActive].data.empty() ? mods[nActive].folder : mods[nActive].data;
    if (!folder.empty())
      return folder + "looks.json";
  }
  return std::string(PortDataFolder()) + "looks.json";
}

bool ParseColour(const PortJson &j, unsigned int *pdw) {
  unsigned int v = 0;
  if (j.kind != PortJson::STRING || j.s.size() != 7 || j.s[0] != '#' || sscanf_s(j.s.c_str() + 1, "%6x", &v) != 1)
    return false;
  *pdw = v;
  return true;
}

std::string ColourText(unsigned int dw) {
  char sz[8];
  sprintf_s(sz, "#%06x", dw & 0xFFFFFF);
  return sz;
}

float Clamp(double d, float fLo, float fHi) {
  return static_cast<float>(d < fLo ? fLo : d > fHi ? fHi : d);
}

bool Name(const PortJson &j, const char *const *ppszNames, int nNames, int *pn) {
  for (int i = 0; i < nNames; i++)
    if (j.s == ppszNames[i]) {
      *pn = i;
      return true;
    }
  return false;
}

// A list of numbers below nEnd, in order.
// A list of rows, each a number or, when pszTable is given, a name of that table's ("terran_marine",
// a mod's unit), so a mod's row is named by what does not move when records sort before it.
bool Numbers(const PortJson &j, int nEnd, std::vector<int> *pOut, const char *pszTable = nullptr) {
  if (j.kind != PortJson::ARRAY)
    return false;
  std::vector<int> out;
  for (size_t i = 0; i < j.size(); i++) {
    const PortJson &v = j.At(i);
    const double d = v.kind == PortJson::STRING && pszTable ? ModTypeByName(pszTable, v.s.c_str()) : v.Num(-1);
    if (d < 0 || d >= nEnd)
      return false;
    out.push_back(static_cast<int>(d));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  *pOut = out;
  return true;
}

// the reader below reads a look's rule, material, zones, circle and aura round this one
#define ReadLook ReadLookShared
// The fields of a look a message or the file gives, past its first zone's; the rest stay as they
// are.
bool ReadLook(const PortJson &j, Look *pLook, std::string *pWhy) {
  if (j["ramp"].kind != PortJson::NONE) {
    unsigned int adw[RAMP];
    bool bOk = j["ramp"].size() == RAMP;
    for (int k = 0; bOk && k < RAMP; k++)
      bOk = ParseColour(j["ramp"].At(k), &adw[k]);
    if (!bOk) {
      *pWhy = "ramp is six colours, \"#rrggbb\", dark to light";
      return false;
    }
    memcpy(pLook->adwRamp, adw, sizeof adw);
  }
  if (j["images"].kind != PortJson::NONE && !Numbers(j["images"], IMAGE_TYPES_MAX, &pLook->images, "images")) {
    *pWhy = "images is a list of places in images.dat";
    return false;
  }
  if (j["units"].kind != PortJson::NONE && !Numbers(j["units"], UNIT_TYPES_MAX, &pLook->units, "units")) {
    *pWhy = "units is a list of places in units.dat";
    return false;
  }
  const char *const apszMat[4] = {"clarity", "shine", "glow", "sparkle"};
  for (int k = 0; k < 4; k++)
    if (j[apszMat[k]].kind == PortJson::NUMBER)
      pLook->afMat[k] = Clamp(j[apszMat[k]].d, 0.0f, 1.0f);
  const PortJson &plume = j["plume"];
  if (plume["height"].kind == PortJson::NUMBER)
    pLook->afPlume[0] = Clamp(plume["height"].d, 0.0f, 256.0f);
  if (plume["density"].kind == PortJson::NUMBER)
    pLook->afPlume[1] = Clamp(plume["density"].d, 0.0f, 4.0f);
  if (plume["rise"].kind == PortJson::NUMBER)
    pLook->afPlume[2] = Clamp(plume["rise"].d, 0.0f, 200.0f);
  if (plume["replaces"].kind != PortJson::NONE && !Numbers(plume["replaces"], IMAGE_TYPES_MAX, &pLook->replaced)) {
    *pWhy = "plume.replaces is a list of places in images.dat";
    return false;
  }
  if (plume["vents"].kind != PortJson::NONE) {
    std::vector<Vent> vents;
    for (size_t i = 0; i < plume["vents"].size(); i++) {
      const PortJson &v = plume["vents"].At(i);
      const double dImage = v["image"].Num(-1);
      if (dImage < 0 || dImage >= IMAGE_TYPES_MAX) {
        *pWhy = "a vent is {image, x, y, strength, width, frame}, x and y in the image's frame";
        return false;
      }
      const double dFrame = v["frame"].Num(-1);
      vents.push_back({static_cast<int>(dImage), Clamp(v["x"].Num(0), -512.0f, 512.0f), Clamp(v["y"].Num(0), -512.0f, 512.0f),
                       Clamp(v["strength"].Num(1), 0.0f, 4.0f), Clamp(v["width"].Num(3), 0.5f, 32.0f),
                       dFrame >= 0 && dFrame < 65536 ? static_cast<int>(dFrame) : -1});
    }
    pLook->vents = vents;
  }
  const PortJson &light = j["light"];
  if (light["color"].kind != PortJson::NONE && !ParseColour(light["color"], &pLook->dwLight)) {
    *pWhy = "light.color is \"#rrggbb\"";
    return false;
  }
  if (light["radius"].kind == PortJson::NUMBER)
    pLook->afLight[0] = Clamp(light["radius"].d, 1.0f, 512.0f);
  if (light["strength"].kind == PortJson::NUMBER)
    pLook->afLight[1] = Clamp(light["strength"].d, 0.0f, 4.0f);
  if (light["x"].kind == PortJson::NUMBER)
    pLook->afLight[2] = Clamp(light["x"].d, -256.0f, 256.0f);
  if (light["y"].kind == PortJson::NUMBER)
    pLook->afLight[3] = Clamp(light["y"].d, -256.0f, 256.0f);
  return true;
}
#undef ReadLook

// Zone z of a look, the first's rule, material and ramp from the look's own; and back.
Zone ZoneOf(const Look &l, int z) {
  Zone o = l.az[z];
  if (z == 0) {
    o.nRule = l.nRule;
    o.nMaterial = l.nMaterial;
    memcpy(o.adwRamp, l.adwRamp, sizeof o.adwRamp);
  }
  return o;
}

void ZoneSet(Look *pLook, int z, const Zone &zone) {
  pLook->az[z] = zone;
  if (z == 0) {
    pLook->nRule = zone.nRule;
    pLook->nMaterial = zone.nMaterial;
    memcpy(pLook->adwRamp, zone.adwRamp, sizeof pLook->adwRamp);
  }
}

// One colour as a ramp of six from dark to light: a fifth of it, toward it, it at the fourth, then
// toward white.
void RampOf(unsigned int dw, unsigned int *pdwRamp) {
  static const float kAt[RAMP] = {0.18f, 0.45f, 0.75f, 1.0f, 1.0f, 1.0f};
  static const float kWhite[RAMP] = {0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.7f};
  for (int k = 0; k < RAMP; k++) {
    unsigned int out = 0;
    for (int s = 16; s >= 0; s -= 8) {
      const float f = (dw >> s & 0xFF) * kAt[k];
      out |= static_cast<unsigned int>(lroundf(f + (255.0f - f) * kWhite[k])) << s;
    }
    pdwRamp[k] = out;
  }
}

bool Flag(const PortJson &j, bool *pb) {
  if (j.kind != PortJson::BOOL)
    return j.kind == PortJson::NONE;
  *pb = j.b;
  return true;
}

// A zone's fields; those it does not give stay as they are.
bool ReadZone(const PortJson &j, Zone *pZone, std::string *pWhy) {
  if (j["rule"].kind != PortJson::NONE && !Name(j["rule"], kRules, RULES, &pZone->nRule)) {
    *pWhy = "rule is blues, greens, all, none, reds, golds, greys, skins, hue or entries";
    return false;
  }
  if (j["material"].kind != PortJson::NONE && !Name(j["material"], kMaterials, MATERIALS, &pZone->nMaterial)) {
    *pWhy = "material is crystal, gas, paint or glow";
    return false;
  }
  if (j["ramp"].kind != PortJson::NONE) {
    unsigned int adw[RAMP];
    bool bOk = j["ramp"].size() == RAMP;
    for (int k = 0; bOk && k < RAMP; k++)
      bOk = ParseColour(j["ramp"].At(k), &adw[k]);
    if (!bOk) {
      *pWhy = "ramp is six colours, \"#rrggbb\", dark to light";
      return false;
    }
    memcpy(pZone->adwRamp, adw, sizeof adw);
  }
  if (j["color"].kind != PortJson::NONE) {
    unsigned int dw;
    if (!ParseColour(j["color"], &dw)) {
      *pWhy = "color is \"#rrggbb\", made into a ramp by lightness";
      return false;
    }
    RampOf(dw, pZone->adwRamp);
  }
  const PortJson &hue = j["hue"];
  const char *const apszHue[5] = {"from", "to", "chroma", "lightMin", "lightMax"};
  const float afMost[5] = {360.0f, 360.0f, 0.4f, 1.0f, 1.0f};
  for (int k = 0; k < 5; k++)
    if (hue[apszHue[k]].kind == PortJson::NUMBER)
      pZone->afHue[k] = Clamp(hue[apszHue[k]].d, 0.0f, afMost[k]);
  if (j["entries"].kind != PortJson::NONE) {
    if (!Numbers(j["entries"], 256, &pZone->entries)) {
      *pWhy = "entries is a list of palette entries, 0 to 255";
      return false;
    }
    if (j["rule"].kind == PortJson::NONE)
      pZone->nRule = RULE_ENTRIES;
  }
  if (j["except"].kind != PortJson::NONE && !Numbers(j["except"], 256, &pZone->except)) {
    *pWhy = "except is a list of palette entries, 0 to 255";
    return false;
  }
  if (j["mix"].kind == PortJson::NUMBER)
    pZone->fMix = Clamp(j["mix"].d, 0.0f, 1.0f);
  if (j["fit"].kind != PortJson::NONE) {
    if (j["fit"].s != "spread" && j["fit"].s != "keep") {
      *pWhy = "fit is \"spread\" (the zone's lightness over the ramp) or \"keep\" (each entry's own)";
      return false;
    }
    pZone->bKeep = j["fit"].s == "keep";
  }
  return true;
}

bool ReadAura(const PortJson &a, Look *pLook, std::string *pWhy) {
  for (int k = 0; k < AU_FIELDS; k++)
    if (a[kAura[k].pszName].kind == PortJson::NUMBER) {
      const float f = Clamp(a[kAura[k].pszName].d, kAura[k].fLo, kAura[k].fHi);
      pLook->afAura[k] = kAura[k].bWhole ? floorf(f + 0.5f) : f;
    }
  const char *const apszColour[3] = {"color", "mid", "core"};
  for (int k = 0; k < 3; k++)
    if (a[apszColour[k]].kind != PortJson::NONE && !ParseColour(a[apszColour[k]], &pLook->adwAura[k])) {
      *pWhy = std::string("aura.") + apszColour[k] + " is \"#rrggbb\"";
      return false;
    }
  if (a["mode"].kind != PortJson::NONE && !Name(a["mode"], kAuraModes, AURA_MODES, &pLook->nAuraMode)) {
    *pWhy = "aura.mode is fixed, ramp, team or cycle";
    return false;
  }
  if (a["blend"].kind != PortJson::NONE && !Name(a["blend"], kAuraBlends, 2, &pLook->nAuraBlend)) {
    *pWhy = "aura.blend is add or over";
    return false;
  }
  if (a["images"].kind != PortJson::NONE) {
    if (a["images"].s != "all" && a["images"].s != "main") {
      *pWhy = "aura.images is \"all\" or \"main\"";
      return false;
    }
    pLook->bAuraMain = a["images"].s == "main";
  }
  if (!Flag(a["dither"], &pLook->bAuraDither) || !Flag(a["overUnits"], &pLook->bAuraOver)) {
    *pWhy = "aura.dither and aura.overUnits are true or false";
    return false;
  }
  return true;
}

// A look in full: the first zone's fields at the top, then the fields above, the zones, whether
// a selection's mark takes it, and its aura.
bool ReadLook(const PortJson &j, Look *pLook, std::string *pWhy) {
  Zone first = ZoneOf(*pLook, 0);
  if (!ReadZone(j, &first, pWhy))
    return false;
  ZoneSet(pLook, 0, first);
  PortJson rest = j;
  rest.members.erase(std::remove_if(rest.members.begin(), rest.members.end(),
                                    [](const PortJsonMember &m) { return m.first == "rule" || m.first == "material"; }),
                     rest.members.end());
  if (!ReadLookShared(rest, pLook, pWhy))
    return false;
  const PortJson &zones = j["zones"];
  if (zones.kind != PortJson::NONE) {
    if (zones.kind != PortJson::ARRAY || zones.size() < 1 || zones.size() > ZONES) {
      *pWhy = "zones is a list of one to four zones";
      return false;
    }
    for (size_t z = 0; z < zones.size(); z++) {
      Zone zone;
      if (static_cast<int>(z) < pLook->nZones)
        zone = ZoneOf(*pLook, static_cast<int>(z));
      else
        memcpy(zone.adwRamp, kGold, sizeof zone.adwRamp);
      if (!ReadZone(zones.At(z), &zone, pWhy)) {
        *pWhy = "zone " + std::to_string(z) + ": " + *pWhy;
        return false;
      }
      ZoneSet(pLook, static_cast<int>(z), zone);
    }
    pLook->nZones = static_cast<int>(zones.size());
  }
  if (!Flag(j["circle"], &pLook->bCircle)) {
    *pWhy = "circle is true or false";
    return false;
  }
  return j["aura"].kind == PortJson::NONE || ReadAura(j["aura"], pLook, pWhy);
}

std::string NumbersText(const std::vector<int> &v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); i++)
    s += (i ? "," : "") + std::to_string(v[i]);
  return s + "]";
}

#define LookJson LookJsonShared
// A look as looks.json and the channel give it, on one line.
std::string LookJson(const Look &l) {
  char sz[256];
  std::string s = "{\"name\":" + PortJsonQuote(l.name) + ",\"rule\":\"" + kRules[l.nRule] + "\",\"material\":\"" +
                  kMaterials[l.nMaterial] + "\",\"ramp\":[";
  for (int k = 0; k < RAMP; k++)
    s += std::string(k ? "," : "") + "\"" + ColourText(l.adwRamp[k]) + "\"";
  s += "],\"images\":" + NumbersText(l.images) + ",\"units\":" + NumbersText(l.units);
  sprintf_s(sz, ",\"clarity\":%g,\"shine\":%g,\"glow\":%g,\"sparkle\":%g,\"plume\":{\"height\":%g,\"density\":%g,\"rise\":%g",
            l.afMat[0], l.afMat[1], l.afMat[2], l.afMat[3], l.afPlume[0], l.afPlume[1], l.afPlume[2]);
  s += sz;
  s += ",\"replaces\":" + NumbersText(l.replaced) + ",\"vents\":[";
  for (size_t i = 0; i < l.vents.size(); i++) {
    const Vent &v = l.vents[i];
    sprintf_s(sz, "%s{\"image\":%d,\"x\":%g,\"y\":%g,\"strength\":%g,\"width\":%g", i ? "," : "", v.nImage, v.x, v.y,
              v.fStrength, v.fWidth);
    s += sz;
    if (v.nFrame >= 0) {
      sprintf_s(sz, ",\"frame\":%d", v.nFrame);
      s += sz;
    }
    s += "}";
  }
  sprintf_s(sz, "]},\"light\":{\"color\":\"%s\",\"radius\":%g,\"strength\":%g,\"x\":%g,\"y\":%g}}",
            ColourText(l.dwLight).c_str(), l.afLight[0], l.afLight[1], l.afLight[2], l.afLight[3]);
  return s + sz;
}
#undef LookJson

std::string ZoneJson(const Zone &z) {
  char sz[192];
  std::string s = std::string("{\"rule\":\"") + kRules[z.nRule] + "\",\"material\":\"" + kMaterials[z.nMaterial] + "\",\"ramp\":[";
  for (int k = 0; k < RAMP; k++)
    s += std::string(k ? "," : "") + "\"" + ColourText(z.adwRamp[k]) + "\"";
  sprintf_s(sz, "],\"hue\":{\"from\":%g,\"to\":%g,\"chroma\":%g,\"lightMin\":%g,\"lightMax\":%g},\"mix\":%g,\"fit\":\"%s\"",
            z.afHue[0], z.afHue[1], z.afHue[2], z.afHue[3], z.afHue[4], z.fMix, z.bKeep ? "keep" : "spread");
  return s + sz + ",\"entries\":" + NumbersText(z.entries) + ",\"except\":" + NumbersText(z.except) + "}";
}

// A look in full as JSON: the fields above, then its zones, its circle and its aura.
std::string LookJson(const Look &l) {
  std::string s = LookJsonShared(l);
  s.pop_back();
  s += ",\"zones\":[";
  for (int z = 0; z < l.nZones; z++)
    s += (z ? "," : "") + ZoneJson(ZoneOf(l, z));
  s += std::string("],\"circle\":") + (l.bCircle ? "true" : "false") + ",\"aura\":{";
  char sz[64];
  for (int k = 0; k < AU_FIELDS; k++) {
    sprintf_s(sz, "%s\"%s\":%g", k ? "," : "", kAura[k].pszName, l.afAura[k]);
    s += sz;
  }
  s += ",\"color\":\"" + ColourText(l.adwAura[0]) + "\",\"mid\":\"" + ColourText(l.adwAura[1]) + "\",\"core\":\"" +
       ColourText(l.adwAura[2]) + "\",\"mode\":\"" + kAuraModes[l.nAuraMode] + "\",\"blend\":\"" + kAuraBlends[l.nAuraBlend] +
       "\",\"images\":\"" + (l.bAuraMain ? "main" : "all") + "\",\"dither\":" + (l.bAuraDither ? "true" : "false") +
       ",\"overUnits\":" + (l.bAuraOver ? "true" : "false");
  return s + "}}";
}

// The looks a game's mode brings beside the file's: its own looks.json and the entries its
// generate returned, each one changing the look of its name or adding it (port_moddata.cpp).
std::string g_modeFile, g_modeLooks;

// The looks of one tree, each changing the look of its name or added after it.
void ReadLooks(const PortJson &j, const char *pszWhere) {
  for (size_t i = 0; i < j["looks"].size(); i++) {
    const PortJson &l = j["looks"].At(i);
    const std::string name = l["name"].Str("");
    if (name.empty()) {
      Log("looks: look %d in %s has no name, and is left out", static_cast<int>(i), pszWhere);
      continue;
    }
    const int nAt = PortLookFind(name.c_str());
    Look look = nAt != 0 ? g_looks[nAt] : Fresh(name.c_str());
    std::string why;
    if (!ReadLook(l, &look, &why)) {
      Log("looks: %s in %s is left out: %s", name.c_str(), pszWhere, why.c_str());
      continue;
    }
    if (nAt != 0)
      g_looks[nAt] = look;
    else if (g_looks.size() <= LOOKS_MAX)
      g_looks.push_back(look);
  }
}

// A file's looks, or a tree's text; false when neither reads.
bool ReadLooksText(const std::string &text, const char *pszWhere) {
  PortJson j;
  std::string why;
  if (!PortJsonParse(text.c_str(), &j, &why) || j["looks"].kind != PortJson::ARRAY) {
    Log("looks: %s does not read: %s", pszWhere, why.empty() ? "it has no \"looks\" list" : why.c_str());
    return false;
  }
  ReadLooks(j, pszWhere);
  return true;
}

// looks.json over the built-in looks: each look in it changes the one of its name, or is added.
void ReadLooksFile(void) {
  g_source = "built in";
  const std::string path = FilePath();
  std::string text;
  if (!PortReadFile(path, &text))
    return;
  PortJson j;
  std::string why;
  if (!PortJsonParse(text.c_str(), &j, &why) || j["looks"].kind != PortJson::ARRAY) {
    Log("looks: %s does not read, the built-in looks are used: %s", path.c_str(),
        why.empty() ? "it has no \"looks\" list" : why.c_str());
    return;
  }
  for (size_t i = 0; i < j["looks"].size(); i++) {
    const PortJson &l = j["looks"].At(i);
    const std::string name = l["name"].Str("");
    if (name.empty()) {
      Log("looks: look %d in %s has no name, and is left out", static_cast<int>(i), path.c_str());
      continue;
    }
    const int nAt = PortLookFind(name.c_str());
    Look look = nAt != 0 ? g_looks[nAt] : Fresh(name.c_str());
    if (!ReadLook(l, &look, &why)) {
      Log("looks: %s in %s is left out: %s", name.c_str(), path.c_str(), why.c_str());
      continue;
    }
    if (nAt != 0)
      g_looks[nAt] = look;
    else if (g_looks.size() <= LOOKS_MAX)
      g_looks.push_back(look);
  }
  g_source = path;
  Log("looks: %d looks, from %s", static_cast<int>(g_looks.size()) - 1, path.c_str());
}

bool CmdLooks(const PortJson &, std::string *pAnswer) {
  std::string s = "\"source\":" + PortJsonQuote(g_source) + ",\"file\":" + PortJsonQuote(FilePath()) + ",\"looks\":[";
  for (size_t i = 1; i < g_looks.size(); i++)
    s += (i > 1 ? "," : "") + LookJson(g_looks[i]);
  *pAnswer = s + "]";
  *pAnswer += ",\"players\":[";
  for (int p = 0; p < 12; p++)
    *pAnswer += (p ? "," : "") + PortJsonQuote(PortLookName(g_abPlayerLook[p]));
  *pAnswer += "]";
  return true;
}

bool CmdLook(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str("");
  if (name.empty()) {
    *pAnswer = "look needs name";
    return false;
  }
  const int nAt = PortLookFind(name.c_str());
  if (nAt == 0 && g_looks.size() > LOOKS_MAX) {
    *pAnswer = "there are 254 looks already";
    return false;
  }
  Look look = nAt != 0 ? g_looks[nAt] : Fresh(name.c_str());
  if (!ReadLook(j, &look, pAnswer))
    return false;
  look.bLive = true;
  if (nAt != 0)
    g_looks[nAt] = look;
  else
    g_looks.push_back(look);
  Reindex();
  PortPresentRedraw();
  *pAnswer = "\"look\":" + std::to_string(nAt != 0 ? nAt : static_cast<int>(g_looks.size()) - 1);
  return true;
}

bool CmdLookUnit(const PortJson &j, std::string *pAnswer) {
  const double dUnit = j["unit"].Num(-1);
  const std::string name = j["look"].Str("");
  if (dUnit < 0 || dUnit >= g_pools.units) {
    *pAnswer = "look_unit needs unit, its id as units gives it";
    return false;
  }
  const int nLook = name.empty() ? 0 : name == "none" ? PORT_LOOK_NONE : PortLookFind(name.c_str());
  if (!name.empty() && nLook == 0) {
    *pAnswer = "no look is named " + name;
    return false;
  }
  PortLookUnitSet(static_cast<int>(dUnit), nLook);
  PortPresentRedraw();
  *pAnswer = "";
  return true;
}

// The look for each of a player's units that has no look by hand or from the provider; "" takes
// it away.
bool CmdLookPlayer(const PortJson &j, std::string *pAnswer) {
  const double dPlayer = j["player"].Num(-1);
  const std::string name = j["look"].Str("");
  if (dPlayer < 0 || dPlayer >= 12) {
    *pAnswer = "look_player needs player, 0 to 11";
    return false;
  }
  const int nLook = name.empty() ? 0 : PortLookFind(name.c_str());
  if (!name.empty() && nLook == 0) {
    *pAnswer = "no look is named " + name;
    return false;
  }
  g_abPlayerLook[static_cast<int>(dPlayer)] = static_cast<unsigned char>(nLook);
  PortPresentRedraw();
  *pAnswer = "";
  return true;
}

void Make(void);

// What a look takes in the palette as it is now: each entry's zone, -1 for none, and the entry the
// 8-bit picture draws it with.
bool CmdLookTakes(const PortJson &j, std::string *pAnswer) {
  const int nLook = PortLookFind(j["name"].Str("").c_str());
  if (nLook == 0) {
    *pAnswer = "look_takes needs name, a look's";
    return false;
  }
  Make();
  const Look &l = g_looks[nLook];
  std::string zone = "[", remap = "[";
  for (int e = 0; e < 256; e++) {
    zone += (e ? "," : "") + std::to_string(l.abTakes[e] ? l.abZone[e] : -1);
    remap += (e ? "," : "") + std::to_string(l.abRemap[e]);
  }
  *pAnswer = "\"zone\":" + zone + "],\"remap\":" + remap + "]";
  return true;
}

bool CmdLooksSave(const PortJson &, std::string *pAnswer) {
  const std::string path = FilePath();
  std::string text = "{\n  \"looks\": [\n";
  for (size_t i = 1; i < g_looks.size(); i++)
    text += "    " + LookJson(g_looks[i]) + (i + 1 < g_looks.size() ? ",\n" : "\n");
  text += "  ]\n}\n";
  FILE *f = fopen(path.c_str(), "wb");
  const bool bOk = f != NULL && fwrite(text.data(), 1, text.size(), f) == text.size();
  if (f != NULL)
    fclose(f);
  if (!bOk) {
    *pAnswer = "could not write " + path;
    return false;
  }
  g_source = path;
  *pAnswer = "\"path\":" + PortJsonQuote(path);
  return true;
}

// The built-in looks, the live values and the channel's commands, once.
void Init(void) {
  if (!g_looks.empty())
    return;
  Builtins();
  Reindex();
  g_source = "built in";
  PortControlFloat("looks", &g_fOn, 0.0f, 1.0f,
                   "the looks in full colour (1): a look's relief, transparency, shine, glow and sparkle, its light on "
                   "the ground and its plume; 0 leaves the recolour alone. GPU scene only",
                   1.0f);
  PortControlFloat("looks.steps", &g_fSteps, 0.0f, 1.0f,
                   "the light on the ground and the plume in steps with a dither, as 1.17 draws its smoke (1), or "
                   "smooth (0)",
                   1.0f);
  PortControlFloat("looks.clarity", &g_fClarity, 0.0f, 2.0f, "how much of the ground shows through a crystal; 1 as the look gives");
  PortControlFloat("looks.shine", &g_fShine, 0.0f, 2.0f, "how much a crystal reflects; 1 as the look gives");
  PortControlFloat("looks.glow", &g_fGlow, 0.0f, 2.0f, "how much a look glows from within; 1 as the look gives");
  PortControlFloat("looks.sparkle", &g_fSparkle, 0.0f, 2.0f, "how many stars catch the light on a crystal; 1 as the look gives");
  PortControlFloat("looks.plume", &g_fPlume, 0.0f, 2.0f, "how dense a plume is; 1 as the look gives");
  PortControlFloat("looks.light", &g_fLight, 0.0f, 2.0f, "how strongly a look lights the ground; 1 as the look gives");
  PortControlCommand("looks", CmdLooks);
  PortControlCommand("look", CmdLook);
  PortControlCommand("look_unit", CmdLookUnit);
  PortControlCommand("looks_save", CmdLooksSave);
  PortControlCommand("look_player", CmdLookPlayer);
  PortControlCommand("look_takes", CmdLookTakes);
  PortControlFloat("looks.aura", &g_fAura, 0.0f, 2.0f, "how strong a look's aura is; 1 as the look gives");
  PortControlFloat("looks.aura.speed", &g_fAuraSpeed, 0.0f, 4.0f, "how fast every aura moves; 1 as the look gives");
}

void Lab(unsigned int dwRgb, float *pLab) {
  const float rgb[3] = {(dwRgb >> 16 & 0xFF) / 255.0f, (dwRgb >> 8 & 0xFF) / 255.0f, (dwRgb & 0xFF) / 255.0f};
  PortRgbToLab(rgb, pLab);
}

unsigned int Entry(int e) {
  return static_cast<unsigned int>(sgGamePalette_g[4 * e]) << 16 | static_cast<unsigned int>(sgGamePalette_g[4 * e + 1]) << 8 |
         sgGamePalette_g[4 * e + 2];
}

bool Takes(int nRule, unsigned int dwRgb) {
  const int r = dwRgb >> 16 & 0xFF, g = dwRgb >> 8 & 0xFF, b = dwRgb & 0xFF;
  // a crystal's blues, and its darkest outline, a little bluer than it is red or green
  if (nRule == RULE_BLUES)
    return (b > r + 24 && b + g > 2 * r + 40) || (b >= r + 12 && b >= g + 4);
  if (nRule == RULE_GREENS)
    return g > r + 16 && g > b + 6;
  return nRule == RULE_ALL;
}

// The entries a look may draw with: not the transparent one, the team colours or the
// colour-cycling ones a tileset animates.
bool Drawable(int e) {
  return e >= 16 && e < 248;
}

// Whether a zone takes entry e, of colour dwRgb and pLab in OKLab.
bool ZoneTakes(const Zone &z, int e, unsigned int dwRgb, const float *pLab) {
  if (std::binary_search(z.except.begin(), z.except.end(), e))
    return false;
  if (z.nRule <= RULE_ALL)
    return Takes(z.nRule, dwRgb);
  const int r = dwRgb >> 16 & 0xFF, g = dwRgb >> 8 & 0xFF, b = dwRgb & 0xFF;
  const int nHi = std::max(r, std::max(g, b)), nLo = std::min(r, std::min(g, b));
  switch (z.nRule) {
  case RULE_REDS:
    return r > g + 40 && r > b + 24;
  case RULE_GOLDS:
    return r > b + 40 && g > b + 20 && g + 50 > r;
  case RULE_GREYS:
    return nHi - nLo < 20 && nHi > 24;
  case RULE_SKINS:
    return r > g && g > b && r > 96 && r - b > 40 && r - b < 140 && g - b > 10;
  case RULE_HUE: {
    const float fChroma = sqrtf(pLab[1] * pLab[1] + pLab[2] * pLab[2]);
    float fHue = atan2f(pLab[2], pLab[1]) * 57.29578f;
    if (fHue < 0.0f)
      fHue += 360.0f;
    const bool bIn = z.afHue[0] <= z.afHue[1] ? fHue >= z.afHue[0] && fHue <= z.afHue[1]
                                               : fHue >= z.afHue[0] || fHue <= z.afHue[1];
    return bIn && fChroma >= z.afHue[2] && pLab[0] >= z.afHue[3] && pLab[0] <= z.afHue[4];
  }
  case RULE_ENTRIES:
    return std::binary_search(z.entries.begin(), z.entries.end(), e);
  }
  return false;
}

// Each zone in turn takes the entries no zone before it took and fits them to its ramp; the
// 8-bit picture's colour is the ramp's over the entry's own by the zone's mix. The pen is that of
// the first zone that takes any, else none.
void Make(void) {
  if (g_bMade && memcmp(g_abPalette, sgGamePalette_g, sizeof g_abPalette) == 0)
    return;
  memcpy(g_abPalette, sgGamePalette_g, sizeof g_abPalette);
  g_bMade = true;
  float lab[256][3];
  for (int e = 0; e < 256; e++)
    Lab(Entry(e), lab[e]);
  auto nearest = [&lab](const float *p) {
    int nBest = 16;
    float fBest = 1e9f;
    for (int e = 0; e < 256; e++) {
      if (!Drawable(e))
        continue;
      const float d0 = lab[e][0] - p[0], d1 = lab[e][1] - p[1], d2 = lab[e][2] - p[2];
      const float d = d0 * d0 + d1 * d1 + d2 * d2;
      if (d < fBest) {
        fBest = d;
        nBest = e;
      }
    }
    return static_cast<BYTE>(nBest);
  };
  for (size_t i = 1; i < g_looks.size(); i++) {
    Look &l = g_looks[i];
    for (int e = 0; e < 256; e++) {
      l.abTakes[e] = false;
      l.abRemap[e] = static_cast<BYTE>(e);
      l.abTone[e] = 0;
      l.abZone[e] = 0;
    }
    l.bPen = 0;
    for (int z = 0; z < l.nZones; z++) {
      const Zone zone = ZoneOf(l, z);
      float ramp[RAMP][3];
      for (int k = 0; k < RAMP; k++)
        Lab(zone.adwRamp[k], ramp[k]);
      bool abHere[256] = {};
      float fLo = 1.0f, fHi = 0.0f;
      for (int e = 1; e < 256; e++) {
        if (l.abTakes[e] || (e >= 8 && e <= 15) || !ZoneTakes(zone, e, Entry(e), lab[e]))
          continue;
        abHere[e] = l.abTakes[e] = true;
        l.abZone[e] = static_cast<BYTE>(z);
        fLo = std::min(fLo, lab[e][0]);
        fHi = std::max(fHi, lab[e][0]);
      }
      bool bAny = false;
      for (int e = 0; e < 256; e++) {
        if (!abHere[e])
          continue;
        bAny = true;
        const float fTone = zone.bKeep ? Clamp(lab[e][0], 0.0f, 1.0f) : fHi > fLo ? (lab[e][0] - fLo) / (fHi - fLo) : 0.5f;
        const float t = fTone * (RAMP - 1);
        const int k = std::min(static_cast<int>(t), RAMP - 2);
        const float f = t - k;
        float at[3];
        for (int c = 0; c < 3; c++)
          at[c] = lab[e][c] + (ramp[k][c] + (ramp[k + 1][c] - ramp[k][c]) * f - lab[e][c]) * zone.fMix;
        l.abRemap[e] = nearest(at);
        l.abTone[e] = static_cast<BYTE>(1 + lroundf(powf(fTone, 0.85f) * 254.0f));
      }
      if (bAny && l.bPen == 0)
        l.bPen = nearest(ramp[3]);
    }
  }
}

bool Paints(const Look &l) {
  for (int z = 0; z < l.nZones; z++)
    if ((z == 0 ? l.nRule : l.az[z].nRule) != RULE_NONE)
      return true;
  return false;
}

bool HasAura(const Look &l) {
  return l.afAura[AU_REACH] > 0.0f && l.afAura[AU_STRENGTH] > 0.0f;
}

// A unit's paint and auras, paint | aura << 8 | another << 16 | another << 24. The paint is the
// look given by hand unless that look only has an aura, else the provider's, else its player's
// (unless bPlayer is false), else its type's; the first aura is the hand's look's if it has one,
// else the paint's, and the rest are those of the other looks that name the unit's type.
int LookOfUnit(const CUnit *pUnit, bool bPlayer = true) {
  const int n = static_cast<int>(pUnit - CUnit::Head());
  if (n < 0 || n >= g_pools.units)
    return 0;
  const int nLooks = static_cast<int>(g_looks.size());
  const int nHand = g_abUnitLook[n] < nLooks ? g_abUnitLook[n] : 0;
  if (g_abUnitLook[n] == PORT_LOOK_NONE)
    return 0;
  int nPaint = nHand != 0 && Paints(g_looks[nHand]) ? nHand : 0;
  if (nPaint == 0 && g_pfnProvider != NULL)
    nPaint = g_pfnProvider(n, pUnit);
  if (nPaint == 0 && bPlayer && pUnit->bPlayer < 12)
    nPaint = g_abPlayerLook[pUnit->bPlayer];
  if (nPaint == 0 && pUnit->wType < UNIT_TYPES_MAX)
    nPaint = g_abTypeLook[pUnit->wType];
  if (nPaint >= nLooks)
    nPaint = 0;
  const int nAura = nHand != 0 && HasAura(g_looks[nHand]) ? nHand : nPaint != 0 && HasAura(g_looks[nPaint]) ? nPaint : 0;
  int nOut = nPaint | nAura << 8;
  if (pUnit->wType < UNIT_TYPES_MAX)
    for (int k = 0, nAt = 1; k < AURAS_MAX - 1; k++) {
      const int nNext = g_abTypeAura[pUnit->wType][k];
      if (nNext == 0 || nNext >= nLooks || nNext == nAura || ++nAt >= AURAS_MAX + 1)
        continue;
      nOut |= nNext << (8 * nAt);
    }
  return nOut;
}

bool Covers(const Look &l, int nImage) {
  return l.images.empty() || std::binary_search(l.images.begin(), l.images.end(), nImage);
}

const Looked *LookedOf(const CSprite *pSprite) {
  auto it = std::lower_bound(g_looked.begin(), g_looked.end(), pSprite,
                             [](const Looked &l, const CSprite *p) { return l.pSprite < p; });
  return it != g_looked.end() && it->pSprite == pSprite ? &*it : NULL;
}

// The top left of an image's canvas in the map layer's pixels: where its vents are measured from.
bool CanvasOf(const CSprite *pSprite, const CImage *pImage, int *pnX, int *pnY) {
  if (pImage == NULL || pImage->pGroup == NULL)
    return false;
  *pnX = static_cast<int>(pSprite->wPosX) + pImage->cHorzOffset - (pImage->pGroup->wMaxWidth >> 1) - glScrollPixelX;
  *pnY = static_cast<int>(pSprite->wPosY) + pImage->cVertOffset - (pImage->pGroup->wMaxHeight >> 1) - glScrollPixelY;
  return true;
}

unsigned int Pack16(int nLow, int nHigh) {
  return (static_cast<unsigned int>(nLow) & 0xFFFF) | (static_cast<unsigned int>(nHigh) << 16);
}

// The light a looked unit casts on the ground, recorded before the sprites so that only the
// ground under it takes it (scene.hlsl, LookPrim).
void Light(const Looked &looked, int nSeed) {
  const Look &l = g_looks[looked.nLook];
  const CImage *pMain = looked.pSprite->pMainImage;
  if (pMain == NULL || !Covers(l, pMain->wImageID) || l.afLight[1] <= 0.0f)
    return;
  const int x = static_cast<int>(looked.pSprite->wPosX) - glScrollPixelX + static_cast<int>(l.afLight[2]);
  const int y = static_cast<int>(looked.pSprite->wPosY) - glScrollPixelY + static_cast<int>(l.afLight[3]);
  const int r = static_cast<int>(l.afLight[0]);
  // to where the light is a hundredth of its middle
  const int w = r * 8 / 5, h = r * 8 * 55 / 500;
  PortSceneLook(x - w, y - h, 2 * w + 1, 2 * h + 1, Pack16(x, y), Pack16(r, lroundf(l.afLight[1] * 1000.0f)),
                static_cast<unsigned int>(looked.nLook) | (PORT_LOOK_LIGHT << 8) |
                    (static_cast<unsigned int>(nSeed & 0xFF) << 16));
}

void PutFloat(unsigned char *p, float f) {
  memcpy(p, &f, 4);
}

// The rest of a look's GPU data: its zones and its aura (port_looks.h, PortLookAuraNow;
// scene.hlsl, LookAura and LookColour).
void LookGpuDesign(const Look &l, unsigned char *p) {
  const float *a = l.afAura;
  const float afColour[3] = {a[AU_STRENGTH] * g_fAura, a[AU_OPACITY], a[AU_CRACKLE]};
  const int anAt[3] = {416, 448, 432};  // glow, middle, line
  for (int k = 0; k < 3; k++) {
    PutFloat(p + anAt[k], (l.adwAura[k] >> 16 & 0xFF) / 255.0f);
    PutFloat(p + anAt[k] + 4, (l.adwAura[k] >> 8 & 0xFF) / 255.0f);
    PutFloat(p + anAt[k] + 8, (l.adwAura[k] & 0xFF) / 255.0f);
    PutFloat(p + anAt[k] + 12, afColour[k]);
  }
  const float af[20] = {a[AU_WIDTH],  a[AU_GAP],    a[AU_FALLOFF],  a[AU_INNER],
                        a[AU_SPEED] * g_fAuraSpeed, a[AU_PULSE], a[AU_PULSE_RATE], a[AU_FLICKER],
                        a[AU_SPARKS], a[AU_EMBERS], a[AU_BANDS],    a[AU_BAND_SPEED],
                        static_cast<float>(l.nAuraMode), static_cast<float>(l.nAuraBlend), a[AU_STEPS],
                        l.bAuraDither ? 1.0f : 0.0f, a[AU_BLOCK], a[AU_CRACKLE_SIZE], l.bAuraOver ? 1.0f : 0.0f,
                        a[AU_REACH]};
  for (int k = 0; k < 20; k++)
    PutFloat(p + 464 + 4 * k, af[k]);
  memcpy(p + 544, l.abZone, 256);
  for (int z = 0; z < ZONES; z++) {
    const Zone zone = ZoneOf(l, z);
    if (z > 0)
      for (int k = 0; k < RAMP; k++) {
        unsigned char *q = p + 800 + (z - 1) * 96 + k * 16;
        PutFloat(q, (zone.adwRamp[k] >> 16 & 0xFF) / 255.0f);
        PutFloat(q + 4, (zone.adwRamp[k] >> 8 & 0xFF) / 255.0f);
        PutFloat(q + 8, (zone.adwRamp[k] & 0xFF) / 255.0f);
      }
    PutFloat(p + 1088 + z * 16, static_cast<float>(zone.nMaterial));
    PutFloat(p + 1092 + z * 16, zone.fMix);
  }
}

// How much 1.17's puffs swell the plume of the vent at x, y (the map layer's pixels), 0 to 255:
// a puff the plume replaces, playing on the sprite with its middle near the vent, swells it as
// it starts and lets it go as it plays out, so the puffs' own timing (their iscript's waitrand,
// part of the simulation) is the plume's rhythm.
int PuffSwell(const CSprite *pSprite, const Look &l, int x, int y) {
  float fSwell = 0.0f;
  for (const CImage *p = pSprite->pImageHead; p != NULL; p = p->pNext) {
    if (p->flags.b.Hidden || !std::binary_search(l.replaced.begin(), l.replaced.end(), static_cast<int>(p->wImageID)))
      continue;
    int px, py;
    if (!CanvasOf(pSprite, p, &px, &py))
      continue;
    // the puff's frame is drawn above its middle as it rises: from its foot
    const int dx = px + (p->pGroup->wMaxWidth >> 1) - x, dy = py + p->pGroup->wMaxHeight - y;
    if (dx * dx + dy * dy > 24 * 24)
      continue;
    // the group's frame count (grp.h: unmapped, as 1.17 never reads it)
    WORD wFrames;
    memcpy(&wFrames, p->pGroup, sizeof wFrames);
    const float t = wFrames > 1 ? std::min(1.0f, p->wFrameIndex / static_cast<float>(wFrames - 1)) : 0.0f;
    fSwell = std::max(fSwell, std::min(1.0f, t * 6.0f) * (1.0f - t * t));
  }
  return lroundf(fSwell * 255.0f);
}

}  // namespace

int PortLookFind(const char *pszName) {
  Init();
  for (size_t i = 1; i < g_looks.size(); i++)
    if (g_looks[i].name == pszName)
      return static_cast<int>(i);
  return 0;
}

const char *PortLookName(int nLook) {
  Init();
  return nLook > 0 && nLook < static_cast<int>(g_looks.size()) ? g_looks[nLook].name.c_str() : "";
}

void PortLookUnitSet(int nUnit, int nLook) {
  if (nUnit >= 0 && nUnit < POOL_UNITS_STORE)
    g_abUnitLook[nUnit] = static_cast<unsigned char>(nLook);
}

void PortLooksGameBegin(void) {
  Init();
  memset(g_abUnitLook, 0, sizeof g_abUnitLook);
  g_looked.clear();
  // the looks the channel made or changed, and each player's, stay from game to game
  std::vector<Look> live;
  for (size_t i = 1; i < g_looks.size(); i++)
    if (g_looks[i].bLive)
      live.push_back(g_looks[i]);
  std::string players[12];
  for (int p = 0; p < 12; p++)
    players[p] = PortLookName(g_abPlayerLook[p]);
  Builtins();
  ReadLooksFile();
  // then the mode's own, so its looks stand over the file's
  if (!g_modeFile.empty()) {
    std::string text;
    if (PortReadFile(g_modeFile, &text))
      ReadLooksText(text, g_modeFile.c_str());
  }
  if (!g_modeLooks.empty())
    ReadLooksText(g_modeLooks, "the mode's generated looks");
  for (const Look &l : live) {
    const int nAt = PortLookFind(l.name.c_str());
    if (nAt != 0)
      g_looks[nAt] = l;
    else if (g_looks.size() <= LOOKS_MAX)
      g_looks.push_back(l);
  }
  for (int p = 0; p < 12; p++)
    g_abPlayerLook[p] = static_cast<unsigned char>(players[p].empty() ? 0 : PortLookFind(players[p].c_str()));
  Reindex();
}

void PortLooksModeFile(const std::string &path) {
  g_modeFile = path;
}

void PortLooksModeLooks(const std::string &text) {
  g_modeLooks = text;
}

void PortLooksProvider(PFN_PORT_LOOK_OF_UNIT pfn) {
  g_pfnProvider = pfn;
}

void PortLooksPassBegin(void) {
  Init();
  g_looked.clear();
  // each unit's paint and aura, and its turret's, which is not in the list: its base's unless it
  // has a look by hand
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->pSprite == NULL || (p->wType < UNIT_TYPES_MAX && (gulUnitTypeFlags[p->wType] & 0x10) != 0))
      continue;
    const int nPair = LookOfUnit(p);
    if (nPair != 0) {
      const Looked l = {p->pSprite, nPair & 0xFF, p->wPositionX, p->wPositionY, p->wType, true, nPair >> 8};
      g_looked.push_back(l);
    }
    const CUnit *pTurret = p->subunit;
    if (pTurret == NULL || pTurret->pSprite == NULL || pTurret->wType >= UNIT_TYPES_MAX ||
        (gulUnitTypeFlags[pTurret->wType] & 0x10) == 0)
      continue;
    const int n = static_cast<int>(pTurret - CUnit::Head());
    const int nTurret = n >= 0 && n < g_pools.units && g_abUnitLook[n] != 0 ? LookOfUnit(pTurret) : nPair;
    if (nTurret != 0) {
      const Looked l = {pTurret->pSprite, nTurret & 0xFF, pTurret->wPositionX, pTurret->wPositionY, pTurret->wType, true,
                        nTurret >> 8};
      g_looked.push_back(l);
    }
  }
  // a looked resource under the fog keeps its look: its copy stands where it stood
  const size_t nUnits = g_looked.size();
  if (nUnits != 0)
    for (CThingy *p = CThingy::FirstFoggedThingy(); p != NULL; p = p->pNext) {
      if (p->pSprite == NULL)
        continue;
      for (size_t i = 0; i < nUnits; i++)
        if (g_looked[i].x == p->pSprite->wPosX && g_looked[i].y == p->pSprite->wPosY && g_looked[i].nType == p->wUnitType) {
          const Looked l = {p->pSprite, g_looked[i].nLook, g_looked[i].x, g_looked[i].y, g_looked[i].nType, false};
          g_looked.push_back(l);
          break;
        }
    }
  std::sort(g_looked.begin(), g_looked.end(), [](const Looked &a, const Looked &b) { return a.pSprite < b.pSprite; });
  if (g_looked.empty())
    return;
  Make();
  if (g_fOn != 0.0f && g_fLight > 0.0f)
    for (size_t i = 0; i < g_looked.size(); i++) {
      Light(g_looked[i], static_cast<int>(i));
      // and the light of each look its auras are from
      for (int k = 0; k < AURAS_MAX; k++) {
        const int nAura = g_looked[i].nAura >> (8 * k) & 0xFF;
        if (nAura == 0 || nAura == g_looked[i].nLook)
          continue;
        Looked aura = g_looked[i];
        aura.nLook = nAura;
        Light(aura, static_cast<int>(i));
      }
    }
}

int PortLookOfSprite(const CSprite *pSprite) {
  if (g_looked.empty())
    return 0;
  const Looked *p = LookedOf(pSprite);
  g_nMainImage = p != NULL && pSprite->pMainImage != NULL ? pSprite->pMainImage->wImageID : -1;
  return p != NULL ? p->nLook | p->nAura << 8 : 0;
}

// The paint's remap and the aura, each where its look covers the image: a selection's mark only
// when the look's circle is on, and an aura only on the main image when the look says so.
bool PortLookImageBegin(int nLook, int nImage) {
  const int nPaint = nLook & 0xFF, nLooks = static_cast<int>(g_looks.size());
  const bool bMark = nImage >= 0x231 && nImage <= 0x244;
  memset(g_anNowAura, 0, sizeof g_anNowAura);
  int nAuras = 0;
  for (int k = 0; k < AURAS_MAX; k++) {
    const int nAura = nLook >> (8 * (k + 1)) & 0xFF;
    if (nAura <= 0 || nAura >= nLooks)
      continue;
    const Look &a = g_looks[nAura];
    if (Covers(a, nImage) && (!bMark || a.bCircle) && (!a.bAuraMain || nImage == g_nMainImage))
      g_anNowAura[nAuras++] = static_cast<unsigned int>(nAura);
  }
  g_nNowAura = g_anNowAura[0];
  const bool bPaint = nPaint > 0 && nPaint < nLooks && Covers(g_looks[nPaint], nImage) && (!bMark || g_looks[nPaint].bCircle);
  if (!bPaint && nAuras == 0)
    return false;
  memcpy(g_abSaved, _gbRemapColorTbl, sizeof g_abSaved);
  if (!bPaint)
    return true;
  const Look &l = g_looks[nPaint];
  for (int e = 0; e < 256; e++)
    if ((e < 8 || e > 15) && l.abTakes[_gbRemapColorTbl[e]])
      _gbRemapColorTbl[e] = l.abRemap[_gbRemapColorTbl[e]];
  if (g_fOn != 0.0f)
    g_nNow = static_cast<unsigned int>(nPaint) |
             (std::binary_search(l.replaced.begin(), l.replaced.end(), nImage) ? PORT_LOOK_REPLACED : 0u);
  return true;
}

void PortLookImageEnd(void) {
  memcpy(_gbRemapColorTbl, g_abSaved, sizeof g_abSaved);
  g_nNow = 0;
  g_nNowAura = 0;
  memset(g_anNowAura, 0, sizeof g_anNowAura);
}

unsigned int PortLookNow(void) {
  return g_nNow;
}

int PortLookHalo(unsigned int nNow) {
  const int nLook = static_cast<int>(nNow & 0xFF);
  if (g_fOn == 0.0f || (nNow & PORT_LOOK_REPLACED) != 0 || nLook <= 0 || nLook >= static_cast<int>(g_looks.size()))
    return 0;
  const Look &l = g_looks[nLook];
  // the design's gather reaches 4.5 pixels, a star's arms 2
  return l.nMaterial == MAT_CRYSTAL && l.afMat[2] * g_fGlow > 0.0f ? 6 : 0;
}

unsigned int PortLookAuraAt(int i) {
  const int nLook = i >= 0 && i < AURAS_MAX ? static_cast<int>(g_anNowAura[i]) : 0;
  if (g_fOn == 0.0f || g_fAura <= 0.0f || (g_nNow & PORT_LOOK_REPLACED) != 0 || nLook <= 0 ||
      nLook >= static_cast<int>(g_looks.size()))
    return 0;
  const Look &l = g_looks[nLook];
  const int nReach = static_cast<int>(l.afAura[AU_REACH]);
  if (nReach <= 0 || l.afAura[AU_STRENGTH] <= 0.0f)
    return 0;
  return static_cast<unsigned int>(nLook) | static_cast<unsigned int>(nReach) << 8 |
         static_cast<unsigned int>(l.afAura[AU_EMBERS] > 0.0f ? kEmberRise : 0) << 16;
}

unsigned int PortLookAuraNow(void) {
  return PortLookAuraAt(0);
}

void PortLookSpriteDrawn(const CSprite *pSprite, int nLook) {
  nLook &= 0xFF;
  if (g_fOn == 0.0f || g_fPlume <= 0.0f || nLook <= 0 || nLook >= static_cast<int>(g_looks.size()))
    return;
  const Look &l = g_looks[nLook];
  const Looked *pLooked = LookedOf(pSprite);
  const CImage *pMain = pSprite->pMainImage;
  int x0, y0;
  if (l.vents.empty() || pLooked == NULL || !pLooked->bUnit || !CanvasOf(pSprite, pMain, &x0, &y0))
    return;
  // a column from each vent, up to its height and as wide as it spreads and sways there
  const int nTop = static_cast<int>(l.afPlume[0]) + 12;
  const int nFrame = pMain->wFrameIndex;
  bool bFramed = false;
  for (size_t i = 0; i < l.vents.size(); i++)
    bFramed |= l.vents[i].nImage == pMain->wImageID && l.vents[i].nFrame == nFrame;
  for (size_t i = 0; i < l.vents.size(); i++) {
    const Vent &v = l.vents[i];
    if (v.nImage != pMain->wImageID || v.nFrame != (bFramed ? nFrame : -1))
      continue;
    const int x = x0 + static_cast<int>(v.x), y = y0 + static_cast<int>(v.y);
    // a swollen column rises higher and spreads wider
    const int nSwell = PuffSwell(pSprite, l, x, y);
    const int nHigh = nTop + nTop * nSwell / 512;
    const int w = static_cast<int>(v.fWidth * (1.0f + nHigh * 0.075f) * (1.0f + nSwell / 512.0f)) + nHigh / 4 + 6;
    PortSceneLook(x - w, y - nHigh, 2 * w + 1, nHigh + 4, Pack16(x, y),
                  Pack16(lroundf(v.fStrength * 1000.0f), lroundf(v.fWidth * 100.0f)),
                  static_cast<unsigned int>(nLook) | (PORT_LOOK_PLUME << 8) | (static_cast<unsigned int>(i) << 16) |
                      (static_cast<unsigned int>(nSwell) << 24));
  }
}

const unsigned char *PortLooksGpu(unsigned int *pnBytes) {
  *pnBytes = 0;
  if (g_fOn == 0.0f || g_looks.size() < 2 || !g_bMade)
    return NULL;
  const size_t nLooks = g_looks.size() - 1;
  g_gpu.assign(nLooks * PORT_LOOK_GPU_BYTES, 0);
  for (size_t i = 0; i < nLooks; i++) {
    const Look &l = g_looks[i + 1];
    unsigned char *p = &g_gpu[i * PORT_LOOK_GPU_BYTES];
    memcpy(p, l.abTone, 256);
    for (int k = 0; k < RAMP; k++) {
      PutFloat(p + 256 + k * 16, (l.adwRamp[k] >> 16 & 0xFF) / 255.0f);
      PutFloat(p + 260 + k * 16, (l.adwRamp[k] >> 8 & 0xFF) / 255.0f);
      PutFloat(p + 264 + k * 16, (l.adwRamp[k] & 0xFF) / 255.0f);
    }
    PutFloat(p + 352, std::min(1.0f, l.afMat[0] * g_fClarity));
    PutFloat(p + 356, std::min(1.0f, l.afMat[1] * g_fShine));
    PutFloat(p + 360, std::min(1.0f, l.afMat[2] * g_fGlow));
    PutFloat(p + 364, std::min(1.0f, l.afMat[3] * g_fSparkle));
    PutFloat(p + 368, l.afPlume[0]);
    PutFloat(p + 372, l.afPlume[1] * g_fPlume);
    PutFloat(p + 376, l.afPlume[2]);
    PutFloat(p + 384, (l.dwLight >> 16 & 0xFF) / 255.0f);
    PutFloat(p + 388, (l.dwLight >> 8 & 0xFF) / 255.0f);
    PutFloat(p + 392, (l.dwLight & 0xFF) / 255.0f);
    PutFloat(p + 396, 0.55f * g_fLight);
    const unsigned int anFlags[2] = {l.nMaterial == MAT_GAS ? 1u : 0u, g_fSteps != 0.0f ? 1u : 0u};
    memcpy(p + 400, anFlags, sizeof anFlags);
    LookGpuDesign(l, p);
  }
  *pnBytes = static_cast<unsigned int>(g_gpu.size());
  return g_gpu.data();
}

// A player's look leaves the dot in the player's colour, and so does a look that takes no
// colours.
unsigned char PortLookUnitPen(const CUnit *pUnit, unsigned char bColour) {
  Init();
  const int nLook = LookOfUnit(pUnit, false) & 0xFF;
  if (nLook == 0)
    return bColour;
  Make();
  return g_looks[nLook].bPen != 0 ? g_looks[nLook].bPen : bColour;
}

unsigned char PortLookPlacePen(int x, int y, int nType, unsigned char bColour) {
  for (const Looked &l : g_looked)
    if (l.bUnit && l.x == x && l.y == y && l.nType == nType)
      return g_looks[l.nLook].bPen != 0 ? g_looks[l.nLook].bPen : bColour;
  return bColour;
}
