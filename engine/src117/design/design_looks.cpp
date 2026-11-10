// design_looks.cpp -- the Looks panel: the looks (port_looks.cpp), each up to four zones of the
// colours it takes with their ramps and materials, and in the design engine an aura round the
// units that have it, edited live through the channel's look command and given to the selection
// (look_unit) or to a player's units (look_player); the templates in design/looks/templates/.
#include <windows.h>
#include <stdio.h>
#include <algorithm>
#include <string>
#include <vector>
#include "imgui.h"
#include "../port/platform/port_json.h"
#include "../port/tools/port_benchinfo.h"  // PortBenchSelected
#include "design_engine.h"
#include "design_hooks.h"
#include "design_ui.h"

namespace {

// a look's aura as it starts (port_looks.cpp, Look), which a template's aura goes over
const char kAuraFirst[] =
    "{\"reach\":0,\"width\":1,\"strength\":1,\"crackle\":0.5,\"speed\":1,\"pulse\":0.3,\"sparks\":0.3,\"gap\":0,"
    "\"falloff\":1,\"inner\":0.3,\"opacity\":1,\"pulseRate\":1,\"flicker\":0,\"embers\":0,\"bands\":0,\"bandSpeed\":1,"
    "\"steps\":4,\"crackleSize\":1,\"block\":1,\"color\":\"#40ff60\",\"mid\":\"#9fffa0\",\"core\":\"#eaffea\","
    "\"mode\":\"fixed\",\"blend\":\"add\",\"images\":\"all\",\"dither\":true,\"overUnits\":false}";

const char *const kRules[] = {"blues", "greens", "all", "none", "reds", "golds", "greys", "skins", "hue", "entries"};
const char *const kMaterials[] = {"crystal", "gas", "paint", "glow"};
const char *const kModes[] = {"fixed", "ramp", "team", "cycle"};
const char *const kBlends[] = {"add", "over"};

std::vector<PortJson> g_looks;
std::vector<std::string> g_players;  // each player's look, "" for none
bool g_bAsked;
int g_nLook;
int g_nZone;
char g_szNew[48];
std::string g_said;
std::vector<unsigned> g_palette;
bool g_bPaletteAsked;
// the selected look's zone of each entry (-1 none) and its 8-bit colour, as look_takes gave them
int g_anZone[256];
int g_anRemap[256];
std::string g_takesOf;
// the colours of the unit last read, entry and pixels
std::vector<std::pair<int, int>> g_unitColours;
std::string g_unitName;
std::vector<PortJson> g_templates;
bool g_bTemplatesRead;
int g_nTemplate;
int g_nGivePlayer;

PortJson *Member(PortJson &o, const char *pszName) {
  for (PortJsonMember &m : o.members)
    if (m.first == pszName)
      return &m.second;
  return NULL;
}

// A member of an object, made if it is not there.
PortJson &Ensure(PortJson &o, const char *pszName, PortJson::Kind kind) {
  if (PortJson *p = Member(o, pszName))
    return *p;
  o.kind = PortJson::OBJECT;
  o.members.push_back({pszName, PortJson()});
  o.members.back().second.kind = kind;
  return o.members.back().second;
}

// src's members over dst's, objects within objects merged too.
void Merge(PortJson &dst, const PortJson &src) {
  for (const PortJsonMember &m : src.members) {
    PortJson *p = Member(dst, m.first.c_str());
    if (p != NULL && p->kind == PortJson::OBJECT && m.second.kind == PortJson::OBJECT)
      Merge(*p, m.second);
    else if (p != NULL)
      *p = m.second;
    else
      dst.members.push_back(m);
  }
  dst.kind = PortJson::OBJECT;
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

ImVec4 Swatch(unsigned rgb) {
  return ImVec4((rgb >> 16 & 0xFF) / 255.0f, (rgb >> 8 & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

void AskTakes(const std::string &name) {
  if (name != g_takesOf)
    for (int e = 0; e < 256; ++e)
      g_anZone[e] = -1;
  g_takesOf = name;
  DesignCall("look_takes", "\"name\":" + PortJsonQuote(name), [name](const PortJson &j) {
    if (!j["ok"].b || j["zone"].size() != 256 || name != g_takesOf)
      return;
    for (int e = 0; e < 256; ++e) {
      g_anZone[e] = static_cast<int>(j["zone"].At(e).Num(-1));
      g_anRemap[e] = static_cast<int>(j["remap"].At(e).Num(e));
    }
  });
}

void Ask(void) {
  g_bAsked = true;
  DesignCall("looks", "", [](const PortJson &j) {
    g_looks.clear();
    for (size_t i = 0; i < j["looks"].size(); ++i)
      g_looks.push_back(j["looks"].At(i));
    g_players.clear();
    for (size_t i = 0; i < j["players"].size(); ++i)
      g_players.push_back(j["players"].At(i).Str());
    if (g_nLook >= static_cast<int>(g_looks.size()))
      g_nLook = 0;
    if (g_nLook < static_cast<int>(g_looks.size()))
      AskTakes(g_looks[g_nLook]["name"].Str());
  });
}

void AskPalette(void) {
  g_bPaletteAsked = true;
  DesignCall("looks_palette", "", [](const PortJson &j) {
    if (j["palette"].size() != 256)
      return;
    g_palette.assign(256, 0);
    for (int e = 0; e < 256; ++e) {
      float af[3];
      FromHex(j["palette"].At(e).Str(), af);
      g_palette[e] = static_cast<unsigned>(af[0] * 255 + 0.5f) << 16 | static_cast<unsigned>(af[1] * 255 + 0.5f) << 8 |
                     static_cast<unsigned>(af[2] * 255 + 0.5f);
    }
  });
}

void ReadTemplates(void) {
  g_bTemplatesRead = true;
  g_templates.clear();
  const std::string folder = DesignFolder().empty() ? std::string() : DesignFolder() + "looks\\templates\\";
  WIN32_FIND_DATAA fd;
  HANDLE h = folder.empty() ? INVALID_HANDLE_VALUE : FindFirstFileA((folder + "*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    FILE *f = fopen((folder + fd.cFileName).c_str(), "rb");
    if (f == NULL)
      continue;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
      text.append(buf, n);
    fclose(f);
    PortJson j;
    if (PortJsonParse(text.c_str(), &j) && j["look"].kind == PortJson::OBJECT)
      g_templates.push_back(j);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

// The look's message: its name and these fields; the looks asked for again when it is answered,
// when bAgain.
void Send(const std::string &name, const std::string &fields, bool bAgain = false) {
  DesignCall("look", "\"name\":" + PortJsonQuote(name) + "," + fields, [name, bAgain](const PortJson &j) {
    if (!j["ok"].b)
      g_said = j["error"].Str("refused");
    else if (bAgain)
      Ask();
    else
      AskTakes(name);
  });
}

void SendZones(PortJson &look) {
  if (PortJson *pZones = Member(look, "zones"))
    Send(look["name"].Str(), "\"zones\":" + PortJsonWrite(*pZones));
}

// The zones with one field of one zone set, sent; the panel's own copy is left as it is while it
// draws, and comes back with the looks.
void SendZoneField(PortJson &look, int nZone, const char *pszName, const PortJson &value) {
  PortJson *pZones = Member(look, "zones");
  if (pZones == NULL || nZone < 0 || nZone >= static_cast<int>(pZones->size()))
    return;
  PortJson zones = *pZones;
  Ensure(zones.items[nZone], pszName, value.kind) = value;
  Send(look["name"].Str(), "\"zones\":" + PortJsonWrite(zones), true);
}

void SendAura(PortJson &look, const std::string &field, const PortJson &value) {
  Send(look["name"].Str(), "\"aura\":{" + PortJsonQuote(field) + ":" + PortJsonWrite(value) + "}");
}

// A number of the look, its slider, sent as it moves.
void Slider(PortJson &look, const char *pszName, const char *pszLabel, float fMost) {
  PortJson *p = Member(look, pszName);
  if (p == NULL)
    return;
  float f = static_cast<float>(p->d);
  if (ImGui::SliderFloat(pszLabel, &f, 0.0f, fMost, "%.2f")) {
    p->d = f;
    char sz[64];
    snprintf(sz, sizeof sz, "\"%s\":%.4g", pszName, f);
    Send(look["name"].Str(), sz);
  }
}

// A number of the aura, its slider, sent as it moves.
void AuraSlider(PortJson &look, PortJson &aura, const char *pszName, const char *pszLabel, float fLeast, float fMost,
                bool bWhole, const char *pszHelp = NULL) {
  PortJson *p = Member(aura, pszName);
  if (p == NULL)
    return;
  bool bChanged;
  if (bWhole) {
    int n = static_cast<int>(p->d + 0.5);
    bChanged = ImGui::SliderInt(pszLabel, &n, static_cast<int>(fLeast), static_cast<int>(fMost), n == 0 ? "off" : "%d");
    if (bChanged)
      p->d = n;
  } else {
    float f = static_cast<float>(p->d);
    bChanged = ImGui::SliderFloat(pszLabel, &f, fLeast, fMost, "%.2f");
    if (bChanged)
      p->d = f;
  }
  if (pszHelp != NULL && ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", pszHelp);
  if (bChanged)
    SendAura(look, pszName, *p);
}

void AuraColour(PortJson &look, PortJson &aura, const char *pszName, const char *pszLabel) {
  PortJson *p = Member(aura, pszName);
  if (p == NULL)
    return;
  float af[3];
  FromHex(p->Str("#000000"), af);
  if (ImGui::ColorEdit3(pszLabel, af, ImGuiColorEditFlags_NoInputs)) {
    p->s = Hex(af);
    SendAura(look, pszName, *p);
  }
}

// A field that is one of names, its combo; true when it changed.
bool Choice(const char *pszLabel, PortJson *p, const char *const *ppszNames, int nNames) {
  if (p == NULL)
    return false;
  bool bChanged = false;
  if (ImGui::BeginCombo(pszLabel, p->Str().c_str())) {
    for (int i = 0; i < nNames; ++i)
      if (ImGui::Selectable(ppszNames[i], p->Str() == ppszNames[i])) {
        p->s = ppszNames[i];
        bChanged = true;
      }
    ImGui::EndCombo();
  }
  return bChanged;
}

bool Check(const char *pszLabel, PortJson *p) {
  if (p == NULL)
    return false;
  bool b = p->kind == PortJson::BOOL && p->b;
  if (!ImGui::Checkbox(pszLabel, &b))
    return false;
  p->kind = PortJson::BOOL;
  p->b = b;
  return true;
}

void GiveSelection(const std::string &look) {
  PortBenchUnit units[PORT_BENCH_SELECT_MAX];
  const int n = PortBenchSelected(units, PORT_BENCH_SELECT_MAX);
  for (int i = 0; i < n; ++i)
    DesignCall("look_unit", "\"unit\":" + std::to_string(units[i].nIndex) + ",\"look\":" + PortJsonQuote(look));
  g_said = n ? "given to " + std::to_string(n) + " units" : "select units in the game first";
}

void ReadUnitColours(void) {
  PortBenchUnit units[PORT_BENCH_SELECT_MAX];
  if (PortBenchSelected(units, PORT_BENCH_SELECT_MAX) == 0) {
    g_said = "select a unit in the game first";
    return;
  }
  g_unitName = units[0].szName;
  DesignCall("look_colours", "\"unit\":" + std::to_string(units[0].nIndex), [](const PortJson &j) {
    g_unitColours.clear();
    for (size_t i = 0; i < j["entries"].size(); ++i)
      g_unitColours.push_back({static_cast<int>(j["entries"].At(i).At(0).Num()), static_cast<int>(j["entries"].At(i).At(1).Num())});
  });
}

// The zone's entries as they are taken now, entry e added or taken away, the zone turned to its
// own entries.
void ToggleEntry(PortJson &look, PortJson &zone, int nZone, int e) {
  PortJson list;
  list.kind = PortJson::ARRAY;
  for (int k = 0; k < 256; ++k) {
    if (k == e && g_anZone[k] == nZone)
      continue;
    if (g_anZone[k] == nZone || k == e) {
      PortJson n;
      n.kind = PortJson::NUMBER;
      n.d = k;
      list.items.push_back(n);
    }
  }
  Ensure(zone, "entries", PortJson::ARRAY) = list;
  Ensure(zone, "rule", PortJson::STRING).s = "entries";
  // an entry another zone takes is left to it, so it goes from its list too
  if (e >= 0 && g_anZone[e] >= 0 && g_anZone[e] != nZone) {
    PortJson *pZones = Member(look, "zones");
    if (pZones != NULL && g_anZone[e] < static_cast<int>(pZones->size())) {
      PortJson &other = pZones->items[g_anZone[e]];
      PortJson &except = Ensure(other, "except", PortJson::ARRAY);
      PortJson n;
      n.kind = PortJson::NUMBER;
      n.d = e;
      except.items.push_back(n);
    }
  }
  SendZones(look);
}

// The palette, 16 by 16, the entries the zone takes outlined; a click adds an entry to it or takes
// one away. Transparent and team entries are never taken.
void PaletteGrid(PortJson &look, PortJson &zone, int nZone) {
  if (g_palette.size() != 256) {
    ImGui::TextDisabled("the palette comes with a game");
    return;
  }
  const float fSide = 13.0f;
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
  for (int e = 0; e < 256; ++e) {
    ImGui::PushID(e);
    const bool bNever = e == 0 || (e >= 8 && e <= 15);
    char sz[64];
    snprintf(sz, sizeof sz, "##e%d", e);
    if (ImGui::ColorButton(sz, Swatch(g_palette[e]), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           ImVec2(fSide, fSide)) &&
        !bNever)
      ToggleEntry(look, zone, nZone, e);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("entry %d  #%06x%s%s", e, g_palette[e], bNever ? "  (never taken)" : "",
                        g_anZone[e] >= 0 ? (g_anZone[e] == nZone ? "  this zone" : "  another zone") : "");
    if (g_anZone[e] == nZone)
      pDraw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
    else if (g_anZone[e] >= 0)
      pDraw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(120, 120, 120, 255), 0.0f, 0, 1.0f);
    ImGui::PopID();
    if ((e & 15) != 15)
      ImGui::SameLine();
  }
  ImGui::PopStyleVar();
}

// The selected unit's colours, most used first, each with what the look makes of it; a click
// adds it to the zone or takes it away.
void UnitColours(PortJson &look, PortJson &zone, int nZone) {
  if (ImGui::SmallButton("Read the selected unit's colours"))
    ReadUnitColours();
  if (g_unitColours.empty() || g_palette.size() != 256)
    return;
  ImGui::SameLine();
  if (ImGui::SmallButton("Take all but team")) {
    PortJson list;
    list.kind = PortJson::ARRAY;
    for (const std::pair<int, int> &c : g_unitColours)
      if (c.first != 0 && (c.first < 8 || c.first > 15)) {
        PortJson n;
        n.kind = PortJson::NUMBER;
        n.d = c.first;
        list.items.push_back(n);
      }
    Ensure(zone, "entries", PortJson::ARRAY) = list;
    Ensure(zone, "rule", PortJson::STRING).s = "entries";
    SendZones(look);
  }
  ImGui::TextDisabled("%s: each colour, and below it the look's", g_unitName.c_str());
  ImDrawList *pDraw = ImGui::GetWindowDrawList();
  int k = 0;
  for (const std::pair<int, int> &c : g_unitColours) {
    const int e = c.first;
    ImGui::PushID(1000 + e);
    ImGui::BeginGroup();
    if (ImGui::ColorButton("##own", Swatch(g_palette[e]), ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16)) && e != 0 &&
        (e < 8 || e > 15))
      ToggleEntry(look, zone, nZone, e);
    if (g_anZone[e] == nZone)
      pDraw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
    ImGui::ColorButton("##after", Swatch(g_palette[g_anRemap[e] & 0xFF]), ImGuiColorEditFlags_NoTooltip, ImVec2(16, 8));
    ImGui::EndGroup();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("entry %d, %d pixels%s", e, c.second, e >= 8 && e <= 15 ? ", the team's" : "");
    ImGui::PopID();
    if (++k % 14 != 0)
      ImGui::SameLine();
  }
  if (k % 14 != 0)
    ImGui::NewLine();
}

void ZoneEditor(PortJson &look, PortJson &zones, int nZone) {
  PortJson &zone = zones.items[nZone];
  PortJson *pRule = Member(zone, "rule");
  if (Choice("Takes", pRule, kRules, 10))
    SendZones(look);
  if (pRule != NULL && pRule->Str() == "hue") {
    PortJson &hue = Ensure(zone, "hue", PortJson::OBJECT);
    bool bChanged = false;
    float afHue[5] = {static_cast<float>(hue["from"].Num(0)), static_cast<float>(hue["to"].Num(360)),
                      static_cast<float>(hue["chroma"].Num(0.02)), static_cast<float>(hue["lightMin"].Num(0)),
                      static_cast<float>(hue["lightMax"].Num(1))};
    bChanged |= ImGui::SliderFloat("Hue from", &afHue[0], 0.0f, 360.0f, "%.0f");
    bChanged |= ImGui::SliderFloat("Hue to", &afHue[1], 0.0f, 360.0f, "%.0f");
    bChanged |= ImGui::SliderFloat("Least chroma", &afHue[2], 0.0f, 0.3f, "%.3f");
    bChanged |= ImGui::SliderFloat("Lightness from", &afHue[3], 0.0f, 1.0f, "%.2f");
    bChanged |= ImGui::SliderFloat("Lightness to", &afHue[4], 0.0f, 1.0f, "%.2f");
    if (bChanged) {
      const char *const apsz[5] = {"from", "to", "chroma", "lightMin", "lightMax"};
      for (int k = 0; k < 5; ++k) {
        PortJson &v = Ensure(hue, apsz[k], PortJson::NUMBER);
        v.kind = PortJson::NUMBER;
        v.d = afHue[k];
      }
      SendZones(look);
    }
  }
  if (Choice("Material", Member(zone, "material"), kMaterials, 4))
    SendZones(look);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("crystal: see-through with shine and stars; gas: soft relief; paint: the colour as the art is "
                      "shaded; glow: the colour added as light");
  static const char *const kFits[] = {"spread", "keep"};
  if (Choice("Fit", Member(zone, "fit"), kFits, 2))
    SendZones(look);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("spread: the zone's lightness over the whole ramp; keep: each colour at its own lightness");
  if (PortJson *pMix = Member(zone, "mix")) {
    float f = static_cast<float>(pMix->d);
    if (ImGui::SliderFloat("Mix", &f, 0.0f, 1.0f, "%.2f")) {
      pMix->d = f;
      SendZones(look);
    }
  }
  // one colour made into a ramp, or the ramp's six colours from dark to light
  if (PortJson *pRamp = Member(zone, "ramp")) {
    if (pRamp->size() == 6) {
      float afOne[3];
      FromHex(pRamp->items[3].Str("#000000"), afOne);
      if (ImGui::ColorEdit3("One colour", afOne, ImGuiColorEditFlags_NoInputs)) {
        // made a ramp by the engine, which comes back with the looks
        PortJson c;
        c.kind = PortJson::STRING;
        c.s = Hex(afOne);
        SendZoneField(look, nZone, "color", c);
      }
      ImGui::SameLine();
      bool bChanged = false;
      for (size_t k = 0; k < 6; ++k) {
        float af[3];
        FromHex(pRamp->items[k].Str("#000000"), af);
        ImGui::PushID(static_cast<int>(k));
        if (ImGui::ColorEdit3("##ramp", af, ImGuiColorEditFlags_NoInputs)) {
          pRamp->items[k].s = Hex(af);
          bChanged = true;
        }
        ImGui::PopID();
        ImGui::SameLine();
      }
      ImGui::TextUnformatted("Ramp");
      if (bChanged)
        SendZones(look);
    }
  }
  if (ImGui::TreeNodeEx("Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
    PaletteGrid(look, zone, nZone);
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("The unit's colours", ImGuiTreeNodeFlags_DefaultOpen)) {
    UnitColours(look, zone, nZone);
    ImGui::TreePop();
  }
}

void Zones(PortJson &look) {
  PortJson *pZones = Member(look, "zones");
  if (pZones == NULL || pZones->kind != PortJson::ARRAY || pZones->size() == 0)
    return;
  if (g_nZone >= static_cast<int>(pZones->size()))
    g_nZone = 0;
  if (ImGui::BeginTabBar("##zones")) {
    for (size_t z = 0; z < pZones->size(); ++z) {
      char sz[24];
      snprintf(sz, sizeof sz, "Zone %d", static_cast<int>(z) + 1);
      if (ImGui::BeginTabItem(sz)) {
        g_nZone = static_cast<int>(z);
        ImGui::EndTabItem();
      }
    }
    if (pZones->size() < 4 && ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
      PortJson zone;
      PortJsonParse("{\"rule\":\"none\",\"material\":\"paint\",\"mix\":1,\"fit\":\"spread\"}", &zone);
      pZones->items.push_back(zone);
      g_nZone = static_cast<int>(pZones->size()) - 1;
      SendZones(look);
      Ask();
    }
    ImGui::EndTabBar();
  }
  if (pZones->size() > 1) {
    if (ImGui::SmallButton("Remove this zone")) {
      pZones->items.erase(pZones->items.begin() + g_nZone);
      g_nZone = 0;
      SendZones(look);
      Ask();
      return;
    }
  }
  ZoneEditor(look, *pZones, g_nZone);
}

void Aura(PortJson &look) {
  PortJson *pAura = Member(look, "aura");
  if (pAura == NULL || pAura->kind != PortJson::OBJECT)
    return;
  PortJson &a = *pAura;
  if (ImGui::TreeNodeEx("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
    AuraSlider(look, a, "reach", "Reach", 0, 8, true, "how far past the outline the glow reaches, in game pixels");
    AuraSlider(look, a, "width", "Line width", 0.5f, 3, false);
    AuraSlider(look, a, "gap", "Gap", 0, 3, true, "pixels between the unit and the line");
    AuraSlider(look, a, "falloff", "Falloff", 0.5f, 3, false, "how fast the glow fades outward");
    AuraSlider(look, a, "inner", "Inner edge", 0, 1, false, "the unit's own edge pixels lit");
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("Colour", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (Choice("Mode", Member(a, "mode"), kModes, 4))
      SendAura(look, "mode", a["mode"]);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("fixed: glow and line; ramp: line, middle, glow outward; team: the player's colour; cycle: the "
                        "hue turning");
    AuraColour(look, a, "color", "Glow");
    ImGui::SameLine();
    AuraColour(look, a, "mid", "Middle");
    ImGui::SameLine();
    AuraColour(look, a, "core", "Line");
    AuraSlider(look, a, "strength", "Strength", 0, 2, false);
    AuraSlider(look, a, "opacity", "Opacity", 0, 1, false);
    if (Choice("Blend", Member(a, "blend"), kBlends, 2))
      SendAura(look, "blend", a["blend"]);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("add: light over what is under; over: the colour laid on, so a dark one outlines");
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("Motion")) {
    AuraSlider(look, a, "speed", "Speed", 0, 4, false);
    AuraSlider(look, a, "pulse", "Pulse", 0, 1, false);
    AuraSlider(look, a, "pulseRate", "Pulse rate", 0.1f, 4, false);
    AuraSlider(look, a, "flicker", "Flicker", 0, 1, false);
    AuraSlider(look, a, "crackle", "Crackle", 0, 1, false, "how broken the line is");
    AuraSlider(look, a, "crackleSize", "Crackle size", 0.25f, 4, false);
    AuraSlider(look, a, "sparks", "Sparks", 0, 1, false);
    AuraSlider(look, a, "embers", "Embers", 0, 1, false, "pixels rising off the edges that face up");
    AuraSlider(look, a, "bands", "Bands", 0, 8, true, "bright bands marching round the outline");
    AuraSlider(look, a, "bandSpeed", "Band speed", -4, 4, false);
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("Pixels")) {
    AuraSlider(look, a, "steps", "Steps", 2, 8, true, "the levels of light, with a dither between");
    if (Check("Dither", Member(a, "dither")))
      SendAura(look, "dither", a["dither"]);
    AuraSlider(look, a, "block", "Block", 1, 2, true, "one game pixel, or blocks of two");
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("Where")) {
    static const char *const kImages[] = {"all", "main"};
    if (Choice("Images", Member(a, "images"), kImages, 2))
      SendAura(look, "images", a["images"]);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("all of the unit's images, or its main one only (no muzzle flashes or overlays)");
    if (Check("Over other units", Member(a, "overUnits")))
      SendAura(look, "overUnits", a["overUnits"]);
    if (Check("On the selection circle", Member(look, "circle")))
      Send(look["name"].Str(), std::string("\"circle\":") + (look["circle"].b ? "true" : "false"));
    ImGui::TreePop();
  }
}

// A template's fields over the look: its aura over an aura as it starts, so nothing of the one
// before stays.
std::string TemplateFields(const PortJson &t) {
  PortJson fields = t["look"];
  if (fields["aura"].kind == PortJson::OBJECT) {
    PortJson aura;
    PortJsonParse(kAuraFirst, &aura);
    Merge(aura, fields["aura"]);
    *Member(fields, "aura") = aura;
  }
  const std::string s = PortJsonWrite(fields);
  return s.size() > 2 ? s.substr(1, s.size() - 2) : std::string();
}

void Templates(PortJson *pLook) {
  if (!g_bTemplatesRead)
    ReadTemplates();
  if (g_templates.empty()) {
    ImGui::TextDisabled("no templates in design/looks/templates/");
    return;
  }
  if (g_nTemplate >= static_cast<int>(g_templates.size()))
    g_nTemplate = 0;
  const PortJson &t = g_templates[g_nTemplate];
  if (ImGui::BeginCombo("Template", t["name"].Str().c_str())) {
    for (size_t i = 0; i < g_templates.size(); ++i)
      if (ImGui::Selectable(g_templates[i]["name"].Str().c_str(), static_cast<int>(i) == g_nTemplate))
        g_nTemplate = static_cast<int>(i);
    ImGui::EndCombo();
  }
  if (!t["about"].Str().empty())
    ImGui::TextWrapped("%s", t["about"].Str().c_str());
  ImGui::BeginDisabled(pLook == NULL);
  if (ImGui::Button("Apply to this look") && pLook != NULL)
    Send((*pLook)["name"].Str(), TemplateFields(t), true);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("New look from it")) {
    std::string name = t["name"].Str();
    for (int n = 2; n < 100; ++n) {
      bool bTaken = false;
      for (const PortJson &l : g_looks)
        bTaken |= l["name"].Str() == name;
      if (!bTaken)
        break;
      name = t["name"].Str() + "-" + std::to_string(n);
    }
    Send(name, TemplateFields(t), true);
    g_nLook = static_cast<int>(g_looks.size());
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Read again"))
    ReadTemplates();
}

}  // namespace

const std::vector<unsigned> &DesignLooksPalette(void) {
  if (!g_bPaletteAsked && DesignInGame())
    AskPalette();
  return g_palette;
}

void DesignLooksPaletteAgain(void) {
  AskPalette();
}

const std::vector<PortJson> &DesignLookTemplates(void) {
  if (!g_bTemplatesRead)
    ReadTemplates();
  return g_templates;
}

std::string DesignLookTemplateFields(const PortJson &t) {
  return TemplateFields(t);
}

const char *DesignAuraFirst(void) {
  return kAuraFirst;
}

void DesignLooksAgain(void) {
  Ask();
}

void DesignLooksPanel(void) {
  if (!g_bAsked)
    Ask();
  DesignLooksPalette();
  if (ImGui::BeginListBox("##looks", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 5))) {
    for (size_t i = 0; i < g_looks.size(); ++i)
      if (ImGui::Selectable(g_looks[i]["name"].Str().c_str(), static_cast<int>(i) == g_nLook)) {
        g_nLook = static_cast<int>(i);
        g_nZone = 0;
        AskTakes(g_looks[i]["name"].Str());
      }
    ImGui::EndListBox();
  }
  if (ImGui::SmallButton("List again")) {
    Ask();
    AskPalette();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Save to looks.json"))
    DesignCall("looks_save", "", [](const PortJson &j) { g_said = j["ok"].b ? "saved" : j["error"].Str("refused"); });
  ImGui::SetNextItemWidth(-60);
  ImGui::InputTextWithHint("##newlook", "A new look's name", g_szNew, sizeof g_szNew);
  ImGui::SameLine();
  ImGui::BeginDisabled(g_szNew[0] == '\0');
  if (ImGui::Button("Add")) {
    // a new look starts taking no colours, with no light on the ground
    Send(g_szNew, "\"rule\":\"none\",\"light\":{\"strength\":0}", true);
    g_szNew[0] = '\0';
  }
  ImGui::EndDisabled();
  if (!g_said.empty())
    ImGui::TextDisabled("%s", g_said.c_str());
  PortJson *pLook = g_nLook < static_cast<int>(g_looks.size()) ? &g_looks[g_nLook] : NULL;
  if (ImGui::CollapsingHeader("Templates"))
    Templates(pLook);
  if (pLook == NULL)
    return;
  PortJson &look = *pLook;
  const std::string name = look["name"].Str();
  ImGui::SeparatorText(name.c_str());
  const bool bGame = DesignInGame();
  ImGui::BeginDisabled(!bGame);
  if (ImGui::Button("Give the selection this look"))
    GiveSelection(name);
  ImGui::SameLine();
  if (ImGui::Button("Their own look"))
    GiveSelection("");
  ImGui::SetNextItemWidth(90);
  ImGui::SliderInt("##player", &g_nGivePlayer, 0, 7, "player %d");
  ImGui::SameLine();
  if (ImGui::Button("Give to the player"))
    DesignCall("look_player", "\"player\":" + std::to_string(g_nGivePlayer) + ",\"look\":" + PortJsonQuote(name),
               [](const PortJson &) { Ask(); });
  ImGui::SameLine();
  if (ImGui::Button("Take it back"))
    DesignCall("look_player", "\"player\":" + std::to_string(g_nGivePlayer) + ",\"look\":\"\"",
               [](const PortJson &) { Ask(); });
  if (g_nGivePlayer < static_cast<int>(g_players.size()) && !g_players[g_nGivePlayer].empty())
    ImGui::TextDisabled("player %d has %s", g_nGivePlayer, g_players[g_nGivePlayer].c_str());
  ImGui::EndDisabled();
  if (ImGui::CollapsingHeader("Colours it takes", ImGuiTreeNodeFlags_DefaultOpen))
    Zones(look);
  if (ImGui::CollapsingHeader("Aura", ImGuiTreeNodeFlags_DefaultOpen))
    Aura(look);
  if (ImGui::CollapsingHeader("Material")) {
    Slider(look, "clarity", "Clarity", 1.0f);
    Slider(look, "shine", "Shine", 1.0f);
    Slider(look, "glow", "Glow", 1.0f);
    Slider(look, "sparkle", "Sparkle", 1.0f);
  }
  // the light it gives on the ground, if any
  PortJson *pLight = Member(look, "light");
  if (pLight != NULL && pLight->kind == PortJson::OBJECT && ImGui::CollapsingHeader("Light on the ground")) {
    float af[3];
    FromHex((*pLight)["color"].Str("#000000"), af);
    PortJson *pRadius = Member(*pLight, "radius");
    PortJson *pStrength = Member(*pLight, "strength");
    bool bChanged = ImGui::ColorEdit3("Light", af, ImGuiColorEditFlags_NoInputs);
    float fRadius = pRadius ? static_cast<float>(pRadius->d) : 0.0f;
    float fStrength = pStrength ? static_cast<float>(pStrength->d) : 0.0f;
    bChanged |= ImGui::SliderFloat("Light radius", &fRadius, 0.0f, 256.0f, "%.0f");
    bChanged |= ImGui::SliderFloat("Light strength", &fStrength, 0.0f, 4.0f, "%.2f");
    if (bChanged) {
      if (PortJson *pColor = Member(*pLight, "color"))
        pColor->s = Hex(af);
      if (pRadius)
        pRadius->d = fRadius;
      if (pStrength)
        pStrength->d = fStrength;
      char sz[160];
      snprintf(sz, sizeof sz, "\"light\":{\"color\":\"%s\",\"radius\":%.4g,\"strength\":%.4g}", Hex(af).c_str(), fRadius,
               fStrength);
      Send(name, sz);
    }
  }
}
