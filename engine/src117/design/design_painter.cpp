// design_painter.cpp -- the army painter: a player's whole army in a scheme of its own colours,
// tried live in the design engine. A scheme is the player's colour (the channel's colour) and a
// colour, finish and mix for each zone of the race's zone map (design/painter/zones/<race>.json,
// palette entries named as armour, trim and so on), with an aura from a template if wanted; the
// panel makes it one look, painter-p<slot>, and gives it to the player's units (look_player).
// Presets are in design/painter/presets/, showrooms in design/scenarios/painter-<race>.json.
// Also the channel's looks_palette and look_colours, which the looks panels read colours with.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>
#include <string>
#include <vector>
#include "imgui.h"
#include "CUnit.h"
#include "CSprite.h"
#include "CImage.h"
#include "grp.h"
#include "../gen/globals117.h"  // sgGamePalette_g, gulUnitTypeFlags
#include "../port/platform/port_json.h"
#include "../port/tools/port_control.h"  // PortControlCommand
#include "design_engine.h"
#include "design_hooks.h"
#include "design_ui.h"

namespace {

// ---- the channel's commands ------------------------------------------------------------------------

std::string Colour(unsigned rgb) {
  char sz[8];
  snprintf(sz, sizeof sz, "#%06x", rgb & 0xFFFFFF);
  return sz;
}

// looks_palette: the game's 256 colours as they are now.
bool CmdPalette(const PortJson &, std::string *pAnswer) {
  std::string s = "\"palette\":[";
  for (int e = 0; e < 256; ++e)
    s += std::string(e ? "," : "") + "\"" +
         Colour(static_cast<unsigned>(sgGamePalette_g[4 * e]) << 16 | static_cast<unsigned>(sgGamePalette_g[4 * e + 1]) << 8 |
                sgGamePalette_g[4 * e + 2]) +
         "\"";
  *pAnswer = s + "]";
  return true;
}

// The palette entries a sprite's images draw with in the frames they show now, the selection's
// marks and the shadows left out.
void Count(const CSprite *pSprite, unsigned *pCounts) {
  for (const CImage *pImage = pSprite->pImageHead; pImage != NULL; pImage = pImage->pNext) {
    if (pImage->flags.b.Hidden || pImage->pGroup == NULL || pImage->bPaletteType > 1 ||
        (pImage->wImageID >= 0x231 && pImage->wImageID <= 0x244) || !GroupHasFrame(pImage->pGroup, pImage->wFrameIndex))
      continue;
    const FRAME *pFrame = &pImage->pGroup->frames[pImage->wFrameIndex];
    for (int y = 0; y < pFrame->bHeight; ++y) {
      const BYTE *pCode = GrpFrameRow(pFrame, y);
      for (int x = 0; x < pFrame->bWidth;) {
        const int c = *pCode++;
        if (c == 0)
          break;
        if (c & 0x80) {
          x += c & 0x7F;
        } else if (c & 0x40) {
          pCounts[*pCode++] += c & 0x3F;
          x += c & 0x3F;
        } else {
          for (int i = 0; i < c; ++i)
            ++pCounts[*pCode++];
          x += c;
        }
      }
    }
  }
}

// look_colours: the colours a unit (its id), a player's units or a unit type's draw with, most
// used first, each [entry, pixels].
bool CmdColours(const PortJson &j, std::string *pAnswer) {
  const int nUnit = static_cast<int>(j["unit"].Num(-1)), nPlayer = static_cast<int>(j["player"].Num(-1)),
            nType = static_cast<int>(j["type"].Num(-1));
  if (nUnit < 0 && nPlayer < 0 && nType < 0) {
    *pAnswer = "look_colours needs unit, player or type";
    return false;
  }
  unsigned counts[256] = {};
  int nUnits = 0;
  for (const CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    const int n = static_cast<int>(p - CUnit::Head());
    if (p->pSprite == NULL || (nUnit >= 0 && n != nUnit) || (nPlayer >= 0 && p->bPlayer != nPlayer) ||
        (nType >= 0 && p->wType != nType))
      continue;
    ++nUnits;
    Count(p->pSprite, counts);
    // and its turret, which is not in the list
    if (p->subunit != NULL && p->subunit->pSprite != NULL && p->subunit->wType < 228 &&
        (gulUnitTypeFlags[p->subunit->wType] & 0x10) != 0)
      Count(p->subunit->pSprite, counts);
  }
  std::vector<std::pair<unsigned, int>> used;
  for (int e = 1; e < 256; ++e)
    if (counts[e] != 0)
      used.push_back({counts[e], e});
  std::sort(used.begin(), used.end(), [](const std::pair<unsigned, int> &a, const std::pair<unsigned, int> &b) {
    return a.first != b.first ? a.first > b.first : a.second < b.second;
  });
  std::string s = "\"units\":" + std::to_string(nUnits) + ",\"entries\":[";
  // with detail, a unit's images as they are drawn: image, palette type, frame, hidden
  if (j["detail"].kind == PortJson::BOOL && nUnit >= 0 && nUnit < 1700 && CUnit::Head()[nUnit].pSprite != NULL) {
    std::string d = "\"images\":[";
    for (const CImage *pImage = CUnit::Head()[nUnit].pSprite->pImageHead; pImage != NULL; pImage = pImage->pNext)
      d += std::string(d.size() > 10 ? "," : "") + "[" + std::to_string(pImage->wImageID) + "," +
           std::to_string(pImage->bPaletteType) + "," + std::to_string(pImage->wFrameIndex) + "," +
           std::to_string(pImage->flags.b.Hidden) + "," + std::to_string(pImage->pGroup ? GroupFrames(pImage->pGroup) : -1) + "]";
    s = d + "]," + s;
  }
  for (size_t i = 0; i < used.size(); ++i)
    s += std::string(i ? "," : "") + "[" + std::to_string(used[i].second) + "," + std::to_string(used[i].first) + "]";
  *pAnswer = s + "]";
  return true;
}

// ---- the panel -------------------------------------------------------------------------------------

const char *const kRaces[] = {"zerg", "terran", "protoss"};
const char *const kFinishes[] = {"paint", "metal", "glow"};
enum { ZONES_MAX = 4 };

struct PaintZone {
  std::string name;
  std::vector<int> entries;
  bool bKeep = false;  // each colour at its own lightness, for small bright zones
  float afColour[3] = {0.5f, 0.5f, 0.5f};
  int nFinish = 0;
  float fMix = 0.85f;
};

struct Seat {
  int nSlot, nRace;
  std::string name;
};

std::vector<Seat> g_seats;
bool g_bSeatsAsked;
int g_nSeat;
int g_nRace = 1;  // the zone map shown, the seat's race unless changed
int g_nRaceRead = -1;
std::vector<PaintZone> g_zones;
float g_afPrimary[3] = {0.2f, 0.35f, 0.8f};
int g_nAura;  // 0 none, else a template with an aura, 1 up
std::vector<PortJson> g_presets;
bool g_bPresetsRead;
int g_nPreset;
char g_szSave[48];
bool g_bLive;
int g_nEditZone;
std::vector<std::pair<int, int>> g_sampled;  // the player's colours, entry and pixels
std::string g_said;

std::string Folder(const char *pszSub) {
  return DesignFolder().empty() ? std::string() : DesignFolder() + "painter\\" + pszSub + "\\";
}

bool ReadText(const std::string &path, std::string *pText) {
  FILE *f = fopen(path.c_str(), "rb");
  if (f == NULL)
    return false;
  char buf[4096];
  size_t n;
  pText->clear();
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    pText->append(buf, n);
  fclose(f);
  return true;
}

bool WriteText(const std::string &path, const std::string &text) {
  FILE *f = fopen(path.c_str(), "wb");
  const bool bOk = f != NULL && fwrite(text.data(), 1, text.size(), f) == text.size();
  if (f != NULL)
    fclose(f);
  return bOk;
}

void FromHex(const std::string &s, float *pf) {
  unsigned v = 0;
  if (s.size() == 7 && s[0] == '#')
    sscanf(s.c_str() + 1, "%x", &v);
  pf[0] = ((v >> 16) & 0xFF) / 255.0f;
  pf[1] = ((v >> 8) & 0xFF) / 255.0f;
  pf[2] = (v & 0xFF) / 255.0f;
}

std::string Hex(const float *pf) {
  char sz[8];
  snprintf(sz, sizeof sz, "#%02x%02x%02x", static_cast<int>(pf[0] * 255.0f + 0.5f), static_cast<int>(pf[1] * 255.0f + 0.5f),
           static_cast<int>(pf[2] * 255.0f + 0.5f));
  return sz;
}

// The race's zone map; its colours kept for the zones of the same name.
void ReadZones(int nRace) {
  g_nRaceRead = nRace;
  std::vector<PaintZone> before = g_zones;
  g_zones.clear();
  std::string text;
  PortJson j;
  if (!ReadText(Folder("zones") + kRaces[nRace] + ".json", &text) || !PortJsonParse(text.c_str(), &j)) {
    g_said = std::string("no zone map for ") + kRaces[nRace] + " in design/painter/zones/";
    return;
  }
  for (size_t i = 0; i < j["zones"].size() && g_zones.size() < ZONES_MAX; ++i) {
    const PortJson &z = j["zones"].At(i);
    PaintZone pz;
    pz.name = z["name"].Str("zone");
    for (size_t k = 0; k < z["entries"].size(); ++k)
      pz.entries.push_back(static_cast<int>(z["entries"].At(k).Num()));
    pz.bKeep = z["fit"].Str() == "keep";
    FromHex(z["color"].Str("#808080"), pz.afColour);
    for (const PaintZone &b : before)
      if (b.name == pz.name) {
        memcpy(pz.afColour, b.afColour, sizeof pz.afColour);
        pz.nFinish = b.nFinish;
        pz.fMix = b.fMix;
      }
    g_zones.push_back(pz);
  }
}

void SaveZones(void) {
  std::string s = "{\n  \"race\": \"" + std::string(kRaces[g_nRace]) + "\",\n  \"zones\": [\n";
  for (size_t i = 0; i < g_zones.size(); ++i) {
    const PaintZone &z = g_zones[i];
    s += "    {\"name\": " + PortJsonQuote(z.name) + ", \"color\": \"" + Hex(z.afColour) + "\", \"fit\": \"" +
         (z.bKeep ? "keep" : "spread") + "\", \"entries\": [";
    for (size_t k = 0; k < z.entries.size(); ++k)
      s += (k ? "," : "") + std::to_string(z.entries[k]);
    s += std::string("]}") + (i + 1 < g_zones.size() ? "," : "") + "\n";
  }
  s += "  ]\n}\n";
  const std::string path = Folder("zones") + kRaces[g_nRace] + ".json";
  g_said = WriteText(path, s) ? "zone map saved" : "could not write " + path;
}

void ReadPresets(void) {
  g_bPresetsRead = true;
  g_presets.clear();
  const std::string folder = Folder("presets");
  WIN32_FIND_DATAA fd;
  HANDLE h = folder.empty() ? INVALID_HANDLE_VALUE : FindFirstFileA((folder + "*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    std::string text;
    PortJson j;
    if (ReadText(folder + fd.cFileName, &text) && PortJsonParse(text.c_str(), &j) && j["name"].kind == PortJson::STRING)
      g_presets.push_back(j);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

// The templates that have an aura, for the army's.
std::vector<const PortJson *> AuraTemplates(void) {
  std::vector<const PortJson *> out;
  for (const PortJson &t : DesignLookTemplates())
    if (t["look"]["aura"].kind == PortJson::OBJECT && t["look"]["aura"]["reach"].Num(0) > 0)
      out.push_back(&t);
  return out;
}

void UsePreset(const PortJson &p) {
  for (int r = 0; r < 3; ++r)
    if (p["race"].Str() == kRaces[r] && r != g_nRaceRead) {
      g_nRace = r;
      ReadZones(r);
    }
  FromHex(p["primary"].Str("#3060d0"), g_afPrimary);
  for (PaintZone &z : g_zones) {
    const PortJson &pz = p["zones"][z.name.c_str()];
    if (pz.kind != PortJson::OBJECT)
      continue;
    FromHex(pz["color"].Str(Hex(z.afColour).c_str()), z.afColour);
    for (int f = 0; f < 3; ++f)
      if (pz["finish"].Str() == kFinishes[f])
        z.nFinish = f;
    z.fMix = static_cast<float>(pz["mix"].Num(z.fMix));
  }
  g_nAura = 0;
  const std::vector<const PortJson *> auras = AuraTemplates();
  for (size_t i = 0; i < auras.size(); ++i)
    if ((*auras[i])["name"].Str() == p["aura"].Str())
      g_nAura = static_cast<int>(i) + 1;
}

void SavePreset(const std::string &name) {
  std::string s = "{\n  \"name\": " + PortJsonQuote(name) + ",\n  \"race\": \"" + kRaces[g_nRace] + "\",\n  \"primary\": \"" +
                  Hex(g_afPrimary) + "\",\n  \"zones\": {\n";
  for (size_t i = 0; i < g_zones.size(); ++i) {
    const PaintZone &z = g_zones[i];
    char sz[64];
    snprintf(sz, sizeof sz, "%.2f", z.fMix);
    s += "    " + PortJsonQuote(z.name) + ": {\"color\": \"" + Hex(z.afColour) + "\", \"finish\": \"" + kFinishes[z.nFinish] +
         "\", \"mix\": " + sz + "}" + (i + 1 < g_zones.size() ? "," : "") + "\n";
  }
  const std::vector<const PortJson *> auras = AuraTemplates();
  const std::string aura = g_nAura > 0 && g_nAura <= static_cast<int>(auras.size()) ? (*auras[g_nAura - 1])["name"].Str() : "";
  s += "  },\n  \"aura\": " + PortJsonQuote(aura) + "\n}\n";
  // its file: lower case, & as "and", a dash between words
  std::string file;
  for (char c : name) {
    const std::string part = c == '&' ? "and" : isalnum(static_cast<unsigned char>(c)) ? std::string(1, static_cast<char>(tolower(c))) : "";
    if (part.empty() || (c == '&' && !file.empty() && file.back() != '-'))
      file += file.empty() || file.back() == '-' ? "" : "-";
    file += part;
    if (c == '&')
      file += "-";
  }
  while (!file.empty() && file.back() == '-')
    file.pop_back();
  const std::string path = Folder("presets") + file + ".json";
  g_said = WriteText(path, s) ? "saved as " + name : "could not write " + path;
  ReadPresets();
}

void AskSeats(void) {
  g_bSeatsAsked = true;
  DesignCall("players", "", [](const PortJson &j) {
    g_seats.clear();
    for (size_t i = 0; i < j["players"].size(); ++i) {
      const PortJson &p = j["players"].At(i);
      const int nSlot = static_cast<int>(p["slot"].Num(-1));
      if (nSlot < 0 || nSlot >= 8)
        continue;
      g_seats.push_back({nSlot, static_cast<int>(p["race"].Num(1)), p["name"].Str()});
    }
    if (g_nSeat >= static_cast<int>(g_seats.size()))
      g_nSeat = 0;
    if (!g_seats.empty() && g_seats[g_nSeat].nRace >= 0 && g_seats[g_nSeat].nRace < 3 && g_nRaceRead < 0)
      g_nRace = g_seats[g_nSeat].nRace;
  });
}

int Slot(void) {
  return g_nSeat < static_cast<int>(g_seats.size()) ? g_seats[g_nSeat].nSlot : 0;
}

// The scheme as one look for the player's units, the player's colour set, and the look given.
void Apply(void) {
  const int nSlot = Slot();
  const std::string name = "painter-p" + std::to_string(nSlot);
  DesignCall("colour", "\"slot\":" + std::to_string(nSlot) + ",\"rgb\":\"" + Hex(g_afPrimary) + "\"");
  // metal is a crystal that shows nothing through, its shine and a few stars
  std::string fields = "\"rule\":\"none\",\"clarity\":0,\"shine\":0.6,\"glow\":0,\"sparkle\":0.25,"
                       "\"light\":{\"strength\":0},\"circle\":false,\"zones\":[";
  int nZones = 0;
  for (const PaintZone &z : g_zones) {
    if (z.entries.empty())
      continue;
    static const char *const kMaterials[] = {"paint", "crystal", "glow"};
    char sz[64];
    snprintf(sz, sizeof sz, "%.3f", z.fMix);
    fields += std::string(nZones++ ? "," : "") + "{\"rule\":\"entries\",\"entries\":[";
    for (size_t k = 0; k < z.entries.size(); ++k)
      fields += (k ? "," : "") + std::to_string(z.entries[k]);
    fields += "],\"color\":\"" + Hex(z.afColour) + "\",\"material\":\"" + kMaterials[z.nFinish] + "\",\"mix\":" + sz +
              ",\"fit\":\"" + (z.bKeep ? "keep" : "spread") + "\"}";
  }
  if (nZones == 0)
    fields += "{\"rule\":\"none\"}";
  fields += "]";
  const std::vector<const PortJson *> auras = AuraTemplates();
  PortJson aura;
  PortJsonParse(DesignAuraFirst(), &aura);
  if (g_nAura > 0 && g_nAura <= static_cast<int>(auras.size())) {
    const std::string t = DesignLookTemplateFields(*auras[g_nAura - 1]);
    PortJson tj;
    if (PortJsonParse(("{" + t + "}").c_str(), &tj) && tj["aura"].kind == PortJson::OBJECT)
      aura = tj["aura"];
  }
  fields += ",\"aura\":" + PortJsonWrite(aura);
  DesignCall("look", "\"name\":" + PortJsonQuote(name) + "," + fields, [name, nSlot](const PortJson &j) {
    if (!j["ok"].b) {
      g_said = j["error"].Str("refused");
      return;
    }
    DesignCall("look_player", "\"player\":" + std::to_string(nSlot) + ",\"look\":" + PortJsonQuote(name),
               [nSlot](const PortJson &k) {
                 g_said = k["ok"].b ? "painted player " + std::to_string(nSlot) : k["error"].Str("refused");
                 DesignLooksAgain();
               });
  });
}

void TakeOff(void) {
  const int nSlot = Slot();
  DesignCall("look_player", "\"player\":" + std::to_string(nSlot) + ",\"look\":\"\"");
  DesignCall("colour", "\"slot\":" + std::to_string(nSlot) + ",\"rgb\":\"\"",
             [](const PortJson &) { g_said = "the player's own colours again"; });
}

void Hsv(float h, float s, float v, float *pRgb) {
  h = fmodf(h, 360.0f) / 60.0f;
  if (h < 0)
    h += 6.0f;
  const int i = static_cast<int>(h);
  const float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  const float rgb[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
  memcpy(pRgb, rgb[i % 6], 3 * sizeof(float));
}

float Rand(float lo, float hi) {
  return lo + (hi - lo) * (rand() / static_cast<float>(RAND_MAX));
}

// Hues that go together: the primary's, and the zones' from it, beside it, opposite or a third
// round, the first zone muted and the last bright.
void Randomize(void) {
  const float h = Rand(0, 360);
  static const float kTurns[3][3] = {{25, -25, 50}, {180, 150, 210}, {120, 240, 180}};
  const float *pTurn = kTurns[rand() % 3];
  Hsv(h, Rand(0.65f, 0.9f), Rand(0.7f, 0.95f), g_afPrimary);
  for (size_t i = 0; i < g_zones.size(); ++i) {
    const float hz = h + pTurn[i % 3];
    if (i == 0)
      Hsv(hz, Rand(0.15f, 0.45f), Rand(0.5f, 0.8f), g_zones[i].afColour);
    else if (i + 1 == g_zones.size())
      Hsv(hz, Rand(0.4f, 0.85f), Rand(0.85f, 1.0f), g_zones[i].afColour);
    else
      Hsv(hz, Rand(0.55f, 0.9f), Rand(0.65f, 0.9f), g_zones[i].afColour);
    g_zones[i].nFinish = rand() % 4 == 0 ? 1 : 0;
  }
}

// The sampled colours parted into the zones by colour: the greys, the warm ones and the rest. A
// start the hand then puts right.
void Suggest(void) {
  const std::vector<unsigned> &pal = DesignLooksPalette();
  if (pal.size() != 256 || g_zones.empty())
    return;
  for (PaintZone &z : g_zones)
    z.entries.clear();
  for (const std::pair<int, int> &c : g_sampled) {
    const int e = c.first;
    if (e == 0 || (e >= 8 && e <= 15))
      continue;
    const int r = pal[e] >> 16 & 0xFF, g = pal[e] >> 8 & 0xFF, b = pal[e] & 0xFF;
    const int hi = std::max(r, std::max(g, b)), lo = std::min(r, std::min(g, b));
    size_t z = 0;
    if (hi - lo >= 28)
      z = r >= b && g >= b - 10 && r > 60 ? 1 : 2;
    g_zones[std::min(z, g_zones.size() - 1)].entries.push_back(e);
  }
  for (PaintZone &z : g_zones)
    std::sort(z.entries.begin(), z.entries.end());
}

void Sample(void) {
  DesignCall("look_colours", "\"player\":" + std::to_string(Slot()), [](const PortJson &j) {
    g_sampled.clear();
    for (size_t i = 0; i < j["entries"].size(); ++i)
      g_sampled.push_back({static_cast<int>(j["entries"].At(i).At(0).Num()), static_cast<int>(j["entries"].At(i).At(1).Num())});
    g_said = std::to_string(g_sampled.size()) + " colours from " + std::to_string(static_cast<int>(j["units"].Num())) + " units";
  });
}

ImVec4 Swatch(unsigned rgb) {
  return ImVec4((rgb >> 16 & 0xFF) / 255.0f, (rgb >> 8 & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

// The sampled colours, or the whole palette, each outlined in the zone being edited when it is in
// it; a click puts it in that zone or takes it out.
void EntryGrid(void) {
  const std::vector<unsigned> &pal = DesignLooksPalette();
  if (pal.size() != 256 || g_nEditZone >= static_cast<int>(g_zones.size()))
    return;
  static bool s_bAll;
  ImGui::Checkbox("The whole palette", &s_bAll);
  std::vector<int> shown;
  if (s_bAll)
    for (int e = 1; e < 256; ++e)
      shown.push_back(e);
  else
    for (const std::pair<int, int> &c : g_sampled)
      shown.push_back(c.first);
  if (shown.empty()) {
    ImGui::TextDisabled("sample the player's colours first");
    return;
  }
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
  int k = 0;
  for (int e : shown) {
    const bool bTeam = e >= 8 && e <= 15;
    int nIn = -1;
    for (size_t z = 0; z < g_zones.size(); ++z)
      if (std::binary_search(g_zones[z].entries.begin(), g_zones[z].entries.end(), e))
        nIn = static_cast<int>(z);
    ImGui::PushID(e);
    if (ImGui::ColorButton("##e", Swatch(pal[e]), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(14, 14)) &&
        !bTeam) {
      for (PaintZone &z : g_zones)
        z.entries.erase(std::remove(z.entries.begin(), z.entries.end(), e), z.entries.end());
      if (nIn != g_nEditZone) {
        std::vector<int> &v = g_zones[g_nEditZone].entries;
        v.insert(std::lower_bound(v.begin(), v.end(), e), e);
      }
      if (g_bLive)
        Apply();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("entry %d%s%s", e, bTeam ? ", the team's: the primary colour" : "",
                        nIn >= 0 ? (", in " + g_zones[nIn].name).c_str() : "");
    if (nIn == g_nEditZone)
      pDraw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
    else if (nIn >= 0)
      pDraw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(110, 110, 110, 255), 0.0f, 0, 1.0f);
    ImGui::PopID();
    if (++k % 16 != 0)
      ImGui::SameLine();
  }
  if (k % 16 != 0)
    ImGui::NewLine();
  ImGui::PopStyleVar();
}

}  // namespace

void DesignPainterInit(void) {
  PortControlCommand("looks_palette", CmdPalette);
  PortControlCommand("look_colours", CmdColours);
}

void DesignPainterPanel(void) {
  if (!DesignInGame()) {
    ImGui::TextDisabled("a game first");
    g_bSeatsAsked = false;
    return;
  }
  if (!g_bSeatsAsked)
    AskSeats();
  if (!g_bPresetsRead)
    ReadPresets();
  if (g_nRaceRead != g_nRace)
    ReadZones(g_nRace);
  bool bChanged = false;
  // the player, and the race whose zone map is used
  const std::string seat = g_nSeat < static_cast<int>(g_seats.size())
                               ? "player " + std::to_string(g_seats[g_nSeat].nSlot) + " " + g_seats[g_nSeat].name
                               : std::string("player 0");
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo("##seat", seat.c_str())) {
    for (size_t i = 0; i < g_seats.size(); ++i) {
      const std::string s = "player " + std::to_string(g_seats[i].nSlot) + " " + g_seats[i].name;
      if (ImGui::Selectable(s.c_str(), static_cast<int>(i) == g_nSeat)) {
        g_nSeat = static_cast<int>(i);
        if (g_seats[i].nRace >= 0 && g_seats[i].nRace < 3)
          g_nRace = g_seats[i].nRace;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  ImGui::Combo("##race", &g_nRace, kRaces, 3);
  ImGui::SameLine();
  if (ImGui::SmallButton("Players again"))
    AskSeats();
  // a preset
  std::vector<int> mine;
  for (size_t i = 0; i < g_presets.size(); ++i)
    if (g_presets[i]["race"].Str() == kRaces[g_nRace])
      mine.push_back(static_cast<int>(i));
  if (g_nPreset >= static_cast<int>(mine.size()))
    g_nPreset = 0;
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo("##preset", mine.empty() ? "no presets" : g_presets[mine[g_nPreset]]["name"].Str().c_str())) {
    for (size_t i = 0; i < mine.size(); ++i)
      if (ImGui::Selectable(g_presets[mine[i]]["name"].Str().c_str(), static_cast<int>(i) == g_nPreset))
        g_nPreset = static_cast<int>(i);
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(mine.empty());
  if (ImGui::Button("Use preset")) {
    UsePreset(g_presets[mine[g_nPreset]]);
    bChanged = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Randomize")) {
    Randomize();
    bChanged = true;
  }
  // the scheme
  ImGui::SeparatorText("Scheme");
  bChanged |= ImGui::ColorEdit3("Primary (the team's colour)", g_afPrimary, ImGuiColorEditFlags_NoInputs);
  for (size_t i = 0; i < g_zones.size(); ++i) {
    PaintZone &z = g_zones[i];
    ImGui::PushID(static_cast<int>(i));
    bChanged |= ImGui::ColorEdit3("##c", z.afColour, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    if (ImGui::RadioButton(z.name.c_str(), g_nEditZone == static_cast<int>(i)))
      g_nEditZone = static_cast<int>(i);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%d colours; pick to edit which below", static_cast<int>(z.entries.size()));
    ImGui::SameLine(200);
    ImGui::SetNextItemWidth(70);
    bChanged |= ImGui::Combo("##f", &z.nFinish, kFinishes, 3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    bChanged |= ImGui::SliderFloat("##m", &z.fMix, 0.0f, 1.0f, "mix %.2f");
    ImGui::PopID();
  }
  const std::vector<const PortJson *> auras = AuraTemplates();
  if (g_nAura > static_cast<int>(auras.size()))
    g_nAura = 0;
  if (ImGui::BeginCombo("Army aura", g_nAura == 0 ? "none" : (*auras[g_nAura - 1])["name"].Str().c_str())) {
    if (ImGui::Selectable("none", g_nAura == 0)) {
      g_nAura = 0;
      bChanged = true;
    }
    for (size_t i = 0; i < auras.size(); ++i)
      if (ImGui::Selectable((*auras[i])["name"].Str().c_str(), g_nAura == static_cast<int>(i) + 1)) {
        g_nAura = static_cast<int>(i) + 1;
        bChanged = true;
      }
    ImGui::EndCombo();
  }
  if (ImGui::Button("Apply to the player"))
    Apply();
  ImGui::SameLine();
  ImGui::Checkbox("As it changes", &g_bLive);
  ImGui::SameLine();
  if (ImGui::Button("Take it off"))
    TakeOff();
  if (bChanged && g_bLive)
    Apply();
  ImGui::SetNextItemWidth(-110);
  ImGui::InputTextWithHint("##save", "A name for this scheme", g_szSave, sizeof g_szSave);
  ImGui::SameLine();
  ImGui::BeginDisabled(g_szSave[0] == '\0');
  if (ImGui::Button("Save scheme"))
    SavePreset(g_szSave);
  ImGui::EndDisabled();
  if (ImGui::Button("Showroom")) {
    const std::string scenario = std::string("painter-") + kRaces[g_nRace];
    DesignCall("design_scenario", "\"load\":" + PortJsonQuote(scenario), [](const PortJson &j) {
      g_said = j["ok"].b ? "the showroom loads; apply the scheme again once it is up" : j["error"].Str("refused");
      g_bSeatsAsked = false;
      DesignLooksPaletteAgain();
    });
  }
  if (!g_said.empty())
    ImGui::TextDisabled("%s", g_said.c_str());
  // the zone map: which of the race's colours each zone is
  if (ImGui::CollapsingHeader("Zone map")) {
    if (ImGui::SmallButton("Sample this player's colours"))
      Sample();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_sampled.empty());
    if (ImGui::SmallButton("Suggest zones"))
      Suggest();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Save zone map"))
      SaveZones();
    ImGui::SameLine();
    if (ImGui::SmallButton("Read again"))
      ReadZones(g_nRace);
    if (g_nEditZone < static_cast<int>(g_zones.size())) {
      ImGui::TextDisabled("editing %s: a click puts a colour in it or takes it out", g_zones[g_nEditZone].name.c_str());
      ImGui::Checkbox("Each colour at its own lightness", &g_zones[g_nEditZone].bKeep);
    }
    EntryGrid();
  }
}
