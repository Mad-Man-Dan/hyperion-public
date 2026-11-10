// design_panels.cpp -- the design engine's panels: the game's clock, spawning, the selection, the
// live values, the effects, the HUD, the units' paths, the lights, the frame's cost and a console
// for any command; the scenario's, the styles' and the looks' panels are in files of their own. Each panel is a thin face on the live channel's commands (design_bridge.cpp),
// so what a panel does a tool can do the same way, and the reverse; only reads with no effect,
// the selection's and the frame's, are taken straight from the engine.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"  // the dock builder
#include "CUnitMisc.h"       // get_unit_name_from_type
#include "../port/platform/port_json.h"
#include "../port/platform/port_settings.h"
#include "../port/render/port_camera.h"    // PortCameraZoomTo and its limits
#include "../port/render/port_view.h"      // PortViewCameraMiddle, g_portView
#include "../port/tools/port_benchinfo.h"  // PortBenchSelected
#include "design_engine.h"
#include "design_hooks.h"
#include "design_ui.h"

// The loaded stat_txt.tbl, the units' names (status.cpp): there once a game's console is up.
extern void *sgpStatStrs;

namespace {

enum Panel {
  P_GAME, P_SPAWN, P_INSPECTOR, P_VALUES, P_EFFECTS, P_HUD, P_PATHS, P_LIGHTS, P_PERF, P_CONSOLE,
  P_SCENARIO, P_STYLE, P_LOOKS, P_PAINTER, P_RECORD, P_COUNT
};
const char *const kPanels[P_COUNT] = {"Game",  "Spawn",   "Inspector", "Values", "Effects",
                                      "HUD",   "Paths",   "Lights",    "Perf",   "Console",
                                      "Scenario", "Style", "Looks",    "Painter", "Record"};
bool g_abShown[P_COUNT] = {true, true, true, true, true, true, true, true, true, true, true, true, true, true, true};

enum { UNIT_TYPES = 228, PLAYERS = 8 };
std::string g_said;  // the last answer worth saying, in the menu bar

std::string Num(double d) {
  char sz[32];
  snprintf(sz, sizeof sz, "%.6g", d);
  return sz;
}

// An answer said in the menu bar: its error, or what it was for.
DesignAnswerFn Say(const std::string &what) {
  return [what](const PortJson &j) { g_said = j["ok"].b ? what : j["error"].Str("refused"); };
}

void Spawn(int nUnit, int nX, int nY, int nPlayer, int nCount, const std::string &extra = std::string()) {
  const std::string fields = "\"unit\":" + std::to_string(nUnit) + ",\"x\":" + std::to_string(nX) + ",\"y\":" +
                             std::to_string(nY) + ",\"player\":" + std::to_string(nPlayer) +
                             ",\"count\":" + std::to_string(nCount) + extra;
  DesignCall("spawn", fields, Say("spawned"));
  DesignScenarioKeep("units", fields);
}

void Middle(int *pnX, int *pnY) {
  double fX = 0, fY = 0;
  PortViewCameraMiddle(&fX, &fY);
  *pnX = static_cast<int>(fX);
  *pnY = static_cast<int>(fY);
}

// ---- what a click on the map is for, when a panel waits for one -----------------------------------

enum Placing { PLACE_NONE, PLACE_UNIT, PLACE_FX };
Placing g_placing = PLACE_NONE;

// ---- Game ---------------------------------------------------------------------------------------

void GamePanel(void) {
  const bool bGame = DesignInGame();
  ImGui::Text(bGame ? "Frame %u%s" : "No game", DesignGameFrame(), DesignGamePaused() ? ", paused" : "");
  ImGui::Text("View %d x %d", g_portView.nWidth, g_portView.nHeight);
  int nView = DesignViewMode();
  ImGui::RadioButton("Beside", &nView, DESIGN_VIEW_BESIDE);
  ImGui::SameLine();
  ImGui::RadioButton("Over the game", &nView, DESIGN_VIEW_OVER);
  ImGui::SameLine();
  ImGui::RadioButton("Game only (F11)", &nView, DESIGN_VIEW_GAME);
  if (nView != DesignViewMode())
    DesignViewSet(nView);
  if (bGame) {
    float fZoom = static_cast<float>(PortCameraZoomTarget());
    if (ImGui::SliderFloat("Zoom", &fZoom, static_cast<float>(PortCameraZoomMin()),
                           static_cast<float>(PortCameraZoomMax()), "%.2f"))
      PortCameraZoomTo(fZoom);
  }
  double dMost = 8.0;  // this program's own key, not the game's zoomMax
  PortSettingNumber("hyperion", "designZoomMax", &dMost);
  float fMost = static_cast<float>(dMost);
  if (ImGui::SliderFloat("Zoom out up to", &fMost, 1.0f, 8.0f, "%.1f"))
    PortSettingSetNumber("hyperion", "designZoomMax", fMost);
  ImGui::BeginDisabled(!bGame);
  if (ImGui::Button(DesignGamePaused() ? "Resume" : "Pause"))
    DesignCall(DesignGamePaused() ? "resume" : "pause", "", Say("done"));
  ImGui::SameLine();
  if (ImGui::Button("Step 1"))
    DesignCall("step", "\"frames\":1", Say("stepped"));
  ImGui::SameLine();
  if (ImGui::Button("Step 24"))
    DesignCall("step", "\"frames\":24", Say("stepped"));
  if (ImGui::Button("Reveal the map"))
    DesignCall("reveal", "", Say("revealed"));
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Grab"))
    DesignCall("design_grab", "", [](const PortJson &j) { g_said = j["ok"].b ? "grab: " + j["path"].Str() : j["error"].Str(); });
  if (ImGui::Button("Grab with the panels"))
    DesignCall("design_grab", "\"panels\":true",
               [](const PortJson &j) { g_said = j["ok"].b ? "grab: " + j["path"].Str() : j["error"].Str(); });
}

// ---- Spawn --------------------------------------------------------------------------------------

int g_nUnit = 0;
int g_nPlayer = 0;
int g_nCount = 6;
char g_szUnitFilter[64];

const char *UnitName(int nType) {
  const char *psz = get_unit_name_from_type(static_cast<WORD>(nType));
  return psz != NULL && psz[0] != '\0' ? psz : "(no name)";
}

bool Matches(const char *pszName, const char *pszFilter) {
  if (pszFilter[0] == '\0')
    return true;
  char a[96], b[64];
  lstrcpynA(a, pszName, sizeof a);
  lstrcpynA(b, pszFilter, sizeof b);
  CharLowerA(a);
  CharLowerA(b);
  return strstr(a, b) != NULL;
}

void SpawnPanel(void) {
  const bool bGame = DesignInGame();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##unitfilter", "Filter, such as marine", g_szUnitFilter, sizeof g_szUnitFilter);
  if (ImGui::BeginListBox("##units", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 9))) {
    if (bGame && sgpStatStrs != NULL)
      for (int t = 0; t < UNIT_TYPES; ++t) {
        const char *pszName = UnitName(t);
        if (!Matches(pszName, g_szUnitFilter))
          continue;
        char sz[112];
        snprintf(sz, sizeof sz, "%s##%d", pszName, t);
        if (ImGui::Selectable(sz, t == g_nUnit))
          g_nUnit = t;
      }
    ImGui::EndListBox();
  }
  ImGui::SliderInt("Player", &g_nPlayer, 0, PLAYERS - 1);
  ImGui::SliderInt("Count", &g_nCount, 1, 64);
  ImGui::BeginDisabled(!bGame);
  if (ImGui::Button("At the middle")) {
    int x, y;
    Middle(&x, &y);
    Spawn(g_nUnit, x, y, g_nPlayer, g_nCount);
  }
  ImGui::SameLine();
  const bool bPlacing = g_placing == PLACE_UNIT;
  if (bPlacing)
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  if (ImGui::Button(bPlacing ? "Placing: click the map" : "Place by clicking"))
    g_placing = bPlacing ? PLACE_NONE : PLACE_UNIT;
  if (bPlacing)
    ImGui::PopStyleColor();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Each left click on the map spawns there; a right click stops.");
  if (ImGui::Button("A fight")) {
    // the chosen units against as many of player 2's, each side sent at the other
    int x, y;
    Middle(&x, &y);
    Spawn(g_nUnit, x - 160, y, g_nPlayer, g_nCount, ",\"attack\":[" + std::to_string(x + 160) + "," + std::to_string(y) + "]");
    Spawn(g_nUnit, x + 160, y, g_nPlayer == 1 ? 0 : 1, g_nCount,
          ",\"attack\":[" + std::to_string(x - 160) + "," + std::to_string(y) + "]");
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear every player"))
    for (int p = 0; p < PLAYERS; ++p)
      DesignCall("clear", "\"player\":" + std::to_string(p), Say("cleared"));
  ImGui::EndDisabled();
}

// ---- Inspector -----------------------------------------------------------------------------------

void Bar(const char *pszLabel, int nNow, int nMax) {
  if (nMax <= 0)
    return;
  char sz[64];
  snprintf(sz, sizeof sz, "%s %d / %d", pszLabel, nNow, nMax);
  ImGui::ProgressBar(static_cast<float>(nNow) / nMax, ImVec2(-1, 0), sz);
}

void InspectorPanel(void) {
  if (!DesignInGame()) {
    ImGui::TextDisabled("No game");
    return;
  }
  PortBenchUnit units[PORT_BENCH_SELECT_MAX];
  const int n = PortBenchSelected(units, PORT_BENCH_SELECT_MAX);
  if (n == 0) {
    ImGui::TextDisabled("Select units in the game");
    return;
  }
  const PortBenchUnit &u = units[0];
  ImGui::Text("%s", u.szName);
  ImGui::TextDisabled("type %d, player %d, unit %d", u.nType, u.nPlayer, u.nIndex);
  Bar("Hit points", u.nHp, u.nHpMax);
  Bar("Shields", u.nShields, u.nShieldsMax);
  Bar("Energy", u.nEnergy, u.nEnergyMax);
  ImGui::Text("Kills %d, armour %d+%d", u.nKills, u.nArmour, u.nArmourUp);
  if (u.bHasWeapon)
    ImGui::Text("%s: %d+%d damage", u.szWeapon, u.nDamage, u.nDamageUp);
  if (u.nProgressPercent >= 0)
    ImGui::ProgressBar(u.nProgressPercent / 100.0f, ImVec2(-1, 0), "Progress");
  if (u.nResources >= 0)
    ImGui::Text("Resources left %d", u.nResources);
  if (n > 1 && ImGui::BeginTable("##selection", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    for (int i = 1; i < n; ++i) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(units[i].szName);
      ImGui::TableNextColumn();
      ImGui::Text("%d/%d", units[i].nHp, units[i].nHpMax);
      ImGui::TableNextColumn();
      ImGui::Text("p%d", units[i].nPlayer);
    }
    ImGui::EndTable();
  }
}

// ---- Values ---------------------------------------------------------------------------------------

struct Value {
  std::string name, help;
  float fValue, fMin, fMax, fDefault, fStep;
};
std::vector<Value> g_values;
bool g_bValuesAsked;
char g_szValueFilter[64];

void ValuesAsk(void) {
  g_bValuesAsked = true;
  DesignCall("list", "", [](const PortJson &j) {
    g_values.clear();
    for (const PortJson &v : j["values"].items)
      g_values.push_back({v["name"].Str(), v["help"].Str(), static_cast<float>(v["value"].Num()),
                          static_cast<float>(v["min"].Num()), static_cast<float>(v["max"].Num(1)),
                          static_cast<float>(v["default"].Num()), static_cast<float>(v["step"].Num())});
  });
}

void ValueSet(Value &v, float f) {
  v.fValue = f;
  DesignCall("set", "\"name\":" + PortJsonQuote(v.name) + ",\"value\":" + Num(f));
}

void ValuesPanel(void) {
  if (!g_bValuesAsked)
    ValuesAsk();
  ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Reread").x - ImGui::GetStyle().FramePadding.x * 4);
  ImGui::InputTextWithHint("##valuefilter", "Filter, such as bloom or light", g_szValueFilter, sizeof g_szValueFilter);
  ImGui::SameLine();
  if (ImGui::Button("Reread"))
    ValuesAsk();
  if (!ImGui::BeginChild("##values"))
    return ImGui::EndChild();
  for (size_t i = 0; i < g_values.size(); ++i) {
    Value &v = g_values[i];
    if (!Matches(v.name.c_str(), g_szValueFilter))
      continue;
    ImGui::PushID(static_cast<int>(i));
    float f = v.fValue;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    const bool bWhole = v.fStep >= 1.0f;
    bool bChanged;
    if (bWhole) {
      int n = static_cast<int>(f);
      bChanged = ImGui::SliderInt(v.name.c_str(), &n, static_cast<int>(v.fMin), static_cast<int>(v.fMax));
      f = static_cast<float>(n);
    } else {
      bChanged = ImGui::SliderFloat(v.name.c_str(), &f, v.fMin, v.fMax, "%.3f");
    }
    if (ImGui::IsItemHovered() && !v.help.empty())
      ImGui::SetTooltip("%s\ndefault %g", v.help.c_str(), v.fDefault);
    if (bChanged)
      ValueSet(v, f);
    if (f != v.fDefault && ImGui::IsItemClicked(ImGuiMouseButton_Right))
      ValueSet(v, v.fDefault);
    ImGui::PopID();
  }
  ImGui::EndChild();
}

// ---- Effects ---------------------------------------------------------------------------------------

struct Param {
  std::string name;
  PortJson::Kind kind;  // NUMBER, BOOL or STRING
  double fValue, fMin, fMax, fStep;
  bool bValue;
  std::string text;
  std::vector<std::string> choices;
};
struct Effect {
  std::string id, name, slot;
  bool bLoop;
  std::vector<Param> params;
};
std::vector<Effect> g_effects;
std::vector<std::string> g_fxErrors;
int g_nEffect;
bool g_bFxLoop;
bool g_bFxAsked;
double g_fFxMs;
int g_nFxShapes, g_nFxPlaying;
ULONGLONG g_qwFxScan;
FILETIME g_ftFxNewest;

void FxFields(const PortJson &fx) {
  g_fFxMs = fx["ms"].Num();
  g_nFxShapes = static_cast<int>(fx["shapes"].Num());
  g_nFxPlaying = static_cast<int>(fx["playing"].Num());
  g_fxErrors.clear();
  for (const PortJson &e : fx["errors"].items)
    g_fxErrors.push_back(e.Str());
}

Param ParamOf(const std::string &name, const PortJson &v) {
  Param p{name, PortJson::NUMBER, 0, 0, 1, 0, false, "", {}};
  const PortJson &first = v.kind == PortJson::ARRAY ? v.At(0) : v;
  p.kind = first.kind == PortJson::BOOL ? PortJson::BOOL : first.kind == PortJson::STRING ? PortJson::STRING : PortJson::NUMBER;
  p.fValue = first.Num();
  p.bValue = first.kind == PortJson::BOOL && first.b;
  p.text = first.Str();
  if (v.kind == PortJson::ARRAY) {
    if (p.kind == PortJson::NUMBER && v.size() >= 3) {
      p.fMin = v.At(1).Num();
      p.fMax = v.At(2).Num(1);
      p.fStep = v.At(3).Num();
    } else if (p.kind == PortJson::STRING) {
      for (size_t i = 1; i < v.size(); ++i)
        p.choices.push_back(v.At(i).Str());
    }
  } else if (p.kind == PortJson::NUMBER) {
    // a bare number: a range round it
    p.fMin = p.fValue < 0 ? p.fValue * 2 : 0;
    p.fMax = p.fValue > 0 ? p.fValue * 2 : 1;
  }
  return p;
}

void FxAsk(void) {
  g_bFxAsked = true;
  DesignCall("fx_list", "", [](const PortJson &j) {
    const std::string keep = g_nEffect < static_cast<int>(g_effects.size()) ? g_effects[g_nEffect].id : "";
    g_effects.clear();
    for (const PortJson &e : j["effects"].items) {
      Effect fx{e["id"].Str(), e["name"].Str(), e["slot"].Str(), e["loop"].kind == PortJson::BOOL && e["loop"].b, {}};
      for (const PortJsonMember &m : e["params"].members)
        fx.params.push_back(ParamOf(m.first, m.second));
      g_effects.push_back(fx);
    }
    g_nEffect = 0;
    for (size_t i = 0; i < g_effects.size(); ++i)
      if (g_effects[i].id == keep)
        g_nEffect = static_cast<int>(i);
    FxFields(j["fx"]);
  });
}

// The effects' files looked at once a second: one changed, they load again.
void FxWatch(void) {
  const ULONGLONG now = GetTickCount64();
  if (now - g_qwFxScan < 1000)
    return;
  g_qwFxScan = now;
  if (DesignFolder().empty())
    return;
  WIN32_FIND_DATAA f;
  HANDLE h = FindFirstFileA((DesignFolder() + "fx\\luau\\*.luau").c_str(), &f);
  if (h == INVALID_HANDLE_VALUE)
    return;
  FILETIME newest = {0, 0};
  do {
    if (CompareFileTime(&f.ftLastWriteTime, &newest) > 0)
      newest = f.ftLastWriteTime;
  } while (FindNextFileA(h, &f));
  FindClose(h);
  const bool bFirst = g_ftFxNewest.dwLowDateTime == 0 && g_ftFxNewest.dwHighDateTime == 0;
  if (!bFirst && CompareFileTime(&newest, &g_ftFxNewest) != 0)
    DesignCall("fx_load", "", [](const PortJson &) {
      g_said = "effects loaded again";
      FxAsk();
    });
  g_ftFxNewest = newest;
}

std::string FxParamsJson(const Effect &e) {
  std::string s = "{";
  for (const Param &p : e.params) {
    s += (s.size() > 1 ? "," : "") + PortJsonQuote(p.name) + ":";
    s += p.kind == PortJson::BOOL ? (p.bValue ? "true" : "false") : p.kind == PortJson::STRING ? PortJsonQuote(p.text) : Num(p.fValue);
  }
  return s + "}";
}

void FxPlay(const std::string &where) {
  if (g_nEffect >= static_cast<int>(g_effects.size()))
    return;
  const Effect &e = g_effects[g_nEffect];
  const std::string fields = "\"effect\":" + PortJsonQuote(e.id) + ",\"loop\":" + (g_bFxLoop ? "true" : "false") +
                             ",\"params\":" + FxParamsJson(e);
  DesignCall("fx_play", fields + where, Say("playing " + e.id));
  // kept in the scenario at a place on the map: the middle's, or where it was put; one on the
  // selection names units by their place in the table, which the next game does not keep
  if (where.find("\"unit\"") == std::string::npos) {
    int x, y;
    Middle(&x, &y);
    DesignScenarioKeep("effects", fields + (where.empty() ? ",\"at\":[" + std::to_string(x) + "," + std::to_string(y) + "]" : where));
  }
}

void EffectsPanel(void) {
  if (!g_bFxAsked)
    FxAsk();
  FxWatch();
  const bool bGame = DesignInGame();
  const char *pszNow = g_nEffect < static_cast<int>(g_effects.size()) ? g_effects[g_nEffect].name.c_str() : "none";
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##effect", pszNow)) {
    for (size_t i = 0; i < g_effects.size(); ++i) {
      const std::string label = g_effects[i].name + "  (" + g_effects[i].slot + ")##" + g_effects[i].id;
      if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == g_nEffect))
        g_nEffect = static_cast<int>(i);
    }
    ImGui::EndCombo();
  }
  if (g_nEffect < static_cast<int>(g_effects.size())) {
    Effect &e = g_effects[g_nEffect];
    for (Param &p : e.params) {
      ImGui::PushID(p.name.c_str());
      if (p.kind == PortJson::BOOL) {
        ImGui::Checkbox(p.name.c_str(), &p.bValue);
      } else if (p.kind == PortJson::STRING && !p.choices.empty()) {
        if (ImGui::BeginCombo(p.name.c_str(), p.text.c_str())) {
          for (const std::string &c : p.choices)
            if (ImGui::Selectable(c.c_str(), c == p.text))
              p.text = c;
          ImGui::EndCombo();
        }
      } else if (p.kind == PortJson::NUMBER) {
        float f = static_cast<float>(p.fValue);
        if (p.fStep >= 1.0) {
          int n = static_cast<int>(f);
          if (ImGui::SliderInt(p.name.c_str(), &n, static_cast<int>(p.fMin), static_cast<int>(p.fMax)))
            p.fValue = n;
        } else if (ImGui::SliderFloat(p.name.c_str(), &f, static_cast<float>(p.fMin), static_cast<float>(p.fMax))) {
          p.fValue = f;
        }
      } else {
        ImGui::LabelText(p.name.c_str(), "%s", p.text.c_str());
      }
      ImGui::PopID();
    }
  }
  ImGui::Checkbox("Loop", &g_bFxLoop);
  ImGui::BeginDisabled(!bGame);
  if (ImGui::Button("At the middle"))
    FxPlay("");
  ImGui::SameLine();
  if (ImGui::Button("On the selection"))
    FxPlay(",\"unit\":\"selected\",\"toUnit\":\"selected\"");
  ImGui::SameLine();
  const bool bPlacing = g_placing == PLACE_FX;
  if (ImGui::Button(bPlacing ? "Placing: click the map" : "Place"))
    g_placing = bPlacing ? PLACE_NONE : PLACE_FX;
  if (ImGui::Button("Stop all"))
    DesignCall("fx_stop", "", Say("stopped"));
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Load again"))
    DesignCall("fx_load", "", [](const PortJson &) { FxAsk(); });
  ImGui::Text("%d playing, %d shapes, %.2f ms", g_nFxPlaying, g_nFxShapes, g_fFxMs);
  // the effects in the game's pixels (the fx.pixel values)
  DesignPixel *pPixel = DesignFxPixel();
  bool bPixel = pPixel->fOn != 0.0f;
  if (ImGui::Checkbox("In the game's pixels", &bPixel))
    pPixel->fOn = bPixel ? 1.0f : 0.0f;
  if (bPixel) {
    // the pixel's level on the game's grid: a game pixel split evenly, or whole game pixels
    static const float kLevels[] = {0.25f, 1.0f / 3.0f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f};
    static const char *const kLevelNames[] = {"1/4 game pixel", "1/3 game pixel", "1/2 game pixel", "1 game pixel",
                                              "2 game pixels",  "3 game pixels",  "4 game pixels"};
    const float fCell = DesignPixelCell(pPixel->fSize);
    int nLevel = 3;
    for (int k = 0; k < 7; ++k)
      if (fabsf(kLevels[k] - fCell) < 0.01f)
        nLevel = k;
    if (ImGui::SliderInt("Pixel level", &nLevel, 0, 6, kLevelNames[nLevel]))
      pPixel->fSize = kLevels[nLevel];
    int n = static_cast<int>(pPixel->fSteps);
    if (ImGui::SliderInt("Light steps", &n, 2, 8))
      pPixel->fSteps = static_cast<float>(n);
    n = static_cast<int>(pPixel->fColours);
    if (ImGui::SliderInt("Colour levels", &n, 0, 16, n < 2 ? "any" : "%d"))
      pPixel->fColours = static_cast<float>(n);
    n = static_cast<int>(pPixel->fHold);
    if (ImGui::SliderInt("Frames held", &n, 1, 4))
      pPixel->fHold = static_cast<float>(n);
  }
  float *pFlash = DesignFxFlash();
  ImGui::SliderFloat("Screen flash", pFlash, 0.0f, 0.5f, *pFlash <= 0.0f ? "off" : "%.2f");
  for (const std::string &e : g_fxErrors)
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", e.c_str());
  ImGui::TextDisabled("Edit design/fx/luau/*.luau and it loads again.");
}

// ---- HUD -------------------------------------------------------------------------------------------

struct HudDesign {
  std::string id, name;
  bool bHeld;
};
std::vector<HudDesign> g_huds;
std::string g_hudDesign = "classic", g_hudLayout = "span", g_hudSize = "full", g_hudMaterial = "gunmetal";
bool g_bHudHidden;
bool g_bHudAsked;

DesignAnswerFn HudTake(void) {
  return [](const PortJson &j) {
    if (!j["ok"].b) {
      g_said = j["error"].Str();
      return;
    }
    const PortJson &h = j["hud"];
    g_hudDesign = h["design"].Str("classic");
    g_hudLayout = h["layout"].Str("span");
    g_hudSize = h["size"].Str("full");
    g_hudMaterial = h["material"].Str("gunmetal");
    g_bHudHidden = h["hidden"].kind == PortJson::BOOL && h["hidden"].b;
    g_huds.clear();
    for (const PortJson &d : h["designs"].items)
      g_huds.push_back({d["id"].Str(), d["name"].Str(), d["held"].kind == PortJson::BOOL && d["held"].b});
  };
}

void Pick(const char *pszLabel, const char *pszField, std::string *pNow, const char *const *ppszChoices, int nChoices) {
  if (!ImGui::BeginCombo(pszLabel, pNow->c_str()))
    return;
  for (int i = 0; i < nChoices; ++i)
    if (ImGui::Selectable(ppszChoices[i], *pNow == ppszChoices[i]))
      DesignCall("design_hud", std::string("\"") + pszField + "\":" + PortJsonQuote(ppszChoices[i]), HudTake());
  ImGui::EndCombo();
}

void HudPanel(void) {
  if (!g_bHudAsked) {
    g_bHudAsked = true;
    DesignCall("design_hud", "", HudTake());
  }
  if (ImGui::BeginCombo("Design", g_hudDesign.c_str())) {
    for (const HudDesign &d : g_huds) {
      if (d.bHeld)
        continue;
      if (ImGui::Selectable((d.name + "##" + d.id).c_str(), d.id == g_hudDesign))
        DesignCall("design_hud", "\"design\":" + PortJsonQuote(d.id), HudTake());
    }
    ImGui::EndCombo();
  }
  static const char *const kLayouts[] = {"span", "flank", "flank-right"};
  static const char *const kSizes[] = {"full", "min"};
  static const char *const kMaterials[] = {"gunmetal", "graphite", "blackout"};
  Pick("Layout", "layout", &g_hudLayout, kLayouts, 3);
  Pick("Size", "size", &g_hudSize, kSizes, 2);
  Pick("Material", "material", &g_hudMaterial, kMaterials, 3);
  bool bHidden = g_bHudHidden;
  if (ImGui::Checkbox("Hide the HUD", &bHidden))
    DesignCall("design_hud", std::string("\"hidden\":") + (bHidden ? "true" : "false"), HudTake());
}

// ---- Paths -----------------------------------------------------------------------------------------

bool g_bPathsOn;
int g_nPathsUnits;  // 0 selected, 1 all
int g_nPathsPlayer = -1;

void PathsSend(void) {
  DesignCall("design_paths", std::string("\"on\":") + (g_bPathsOn ? "true" : "false") + ",\"units\":" +
                                 (g_nPathsUnits == 0 ? "\"selected\"" : "\"all\"") + ",\"player\":" +
                                 std::to_string(g_nPathsPlayer),
             Say(g_bPathsOn ? "paths on" : "paths off"));
}

void PathsPanel(void) {
  bool bChanged = ImGui::Checkbox("Show where units are going", &g_bPathsOn);
  bChanged |= ImGui::Combo("Units", &g_nPathsUnits, "Selected\0All\0");
  bChanged |= ImGui::SliderInt("Player", &g_nPathsPlayer, -1, PLAYERS - 1, g_nPathsPlayer < 0 ? "any" : "%d");
  if (bChanged)
    PathsSend();
  ImGui::TextDisabled("Solid: the path's waypoints. Dashed: the regions ahead.");
}

// ---- Lights ----------------------------------------------------------------------------------------

struct Light {
  int nImage;
  std::string name;
  float afColor[3];
  float fRadius, fIntensity, fFlicker;
};
std::vector<Light> g_lights;
bool g_bLightsAsked;

void LightsAsk(void) {
  g_bLightsAsked = true;
  DesignCall("lights", "", [](const PortJson &j) {
    g_lights.clear();
    for (const PortJson &l : j["lights"].items) {
      Light light{static_cast<int>(l["image"].Num()), l["name"].Str(), {1, 1, 1},
                  static_cast<float>(l["radius"].Num()), static_cast<float>(l["intensity"].Num()),
                  static_cast<float>(l["flicker"].Num())};
      unsigned r = 255, g = 255, b = 255;
      if (sscanf(l["color"].Str("#ffffff").c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
        light.afColor[0] = r / 255.0f;
        light.afColor[1] = g / 255.0f;
        light.afColor[2] = b / 255.0f;
      }
      g_lights.push_back(light);
    }
  });
}

void LightSend(const Light &l) {
  char sz[16];
  snprintf(sz, sizeof sz, "#%02x%02x%02x", static_cast<int>(l.afColor[0] * 255 + 0.5f),
           static_cast<int>(l.afColor[1] * 255 + 0.5f), static_cast<int>(l.afColor[2] * 255 + 0.5f));
  DesignCall("light", "\"image\":" + std::to_string(l.nImage) + ",\"color\":" + PortJsonQuote(sz) +
                          ",\"radius\":" + Num(l.fRadius) + ",\"intensity\":" + Num(l.fIntensity) +
                          ",\"flicker\":" + Num(l.fFlicker));
}

void LightsPanel(void) {
  if (!g_bLightsAsked)
    LightsAsk();
  if (ImGui::Button("Reread"))
    LightsAsk();
  ImGui::SameLine();
  if (ImGui::Button("Save to lights.json"))
    DesignCall("lights_save", "", [](const PortJson &j) { g_said = j["ok"].b ? "lights saved" : j["error"].Str(); });
  if (!ImGui::BeginChild("##lights"))
    return ImGui::EndChild();
  for (size_t i = 0; i < g_lights.size(); ++i) {
    Light &l = g_lights[i];
    ImGui::PushID(static_cast<int>(i));
    char szHead[96];
    snprintf(szHead, sizeof szHead, "%s (image %d)", l.name.empty() ? "light" : l.name.c_str(), l.nImage);
    if (ImGui::TreeNode(szHead)) {
      bool b = ImGui::ColorEdit3("Colour", l.afColor);
      b |= ImGui::SliderFloat("Radius", &l.fRadius, 0.0f, 512.0f);
      b |= ImGui::SliderFloat("Intensity", &l.fIntensity, 0.0f, 4.0f);
      b |= ImGui::SliderFloat("Flicker", &l.fFlicker, 0.0f, 1.0f);
      if (b)
        LightSend(l);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
}

// ---- Perf ------------------------------------------------------------------------------------------

float g_afFrameMs[240];
int g_nFrameAt;
ULONGLONG g_qwStatusAt;

void PerfPanel(void) {
  const ImGuiIO &io = ImGui::GetIO();
  float fWorst = 0.0f, fSum = 0.0f;
  for (float f : g_afFrameMs) {
    fWorst = f > fWorst ? f : fWorst;
    fSum += f;
  }
  ImGui::Text("%.0f frames a second, %.2f ms; the worst of the last 240 %.2f ms", io.Framerate,
              fSum / (sizeof g_afFrameMs / sizeof g_afFrameMs[0]), fWorst);
  ImGui::PlotLines("##frames", g_afFrameMs, static_cast<int>(sizeof g_afFrameMs / sizeof g_afFrameMs[0]), g_nFrameAt,
                   NULL, 0.0f, fWorst > 33.0f ? fWorst : 33.0f, ImVec2(-1, 80));
  ImGui::Text("Effects %.2f ms of 1 ms, %d shapes of 2,000", g_fFxMs, g_nFxShapes);
  ImGui::ProgressBar(static_cast<float>(g_fFxMs), ImVec2(-1, 0), "effects' time against the budget");
  ImGui::TextDisabled("Channel requests waiting: %d", DesignBridgeWaiting());
}

// ---- Console ---------------------------------------------------------------------------------------

std::deque<std::string> g_lines;
char g_szCommand[1024];
bool g_bScroll;

void Line(const std::string &s) {
  g_lines.push_back(s);
  while (g_lines.size() > 400)
    g_lines.pop_front();
  g_bScroll = true;
}

void Run(const char *pszLine) {
  std::string text = pszLine;
  const size_t space = text.find_first_of(" {");
  const std::string cmd = text.substr(0, space);
  std::string rest = space == std::string::npos ? std::string() : text.substr(space);
  while (!rest.empty() && rest[0] == ' ')
    rest.erase(0, 1);
  if (cmd.empty())
    return;
  std::string fields;
  if (!rest.empty()) {
    PortJson j;
    std::string why;
    if (!PortJsonParse(rest.c_str(), &j, &why) || j.kind != PortJson::OBJECT) {
      Line("! the arguments are a JSON object, such as {\"frames\": 24}: " + why);
      return;
    }
    const std::string all = PortJsonWrite(j);
    fields = all.size() > 2 ? all.substr(1, all.size() - 2) : std::string();
  }
  Line("> " + text);
  DesignCall(cmd.c_str(), fields, [](const PortJson &j) { Line(PortJsonWrite(j)); });
}

void ConsolePanel(void) {
  const float fFoot = ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginChild("##lines", ImVec2(0, -fFoot), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
    for (const std::string &s : g_lines)
      ImGui::TextUnformatted(s.c_str());
    if (g_bScroll)
      ImGui::SetScrollHereY(1.0f);
    g_bScroll = false;
  }
  ImGui::EndChild();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputTextWithHint("##command", "A command and its arguments, such as step {\"frames\": 24}", g_szCommand,
                               sizeof g_szCommand, ImGuiInputTextFlags_EnterReturnsTrue)) {
    Run(g_szCommand);
    g_szCommand[0] = '\0';
    ImGui::SetKeyboardFocusHere(-1);
  }
}

// ---- the panels' common work -------------------------------------------------------------------------

typedef void (*PanelFn)(void);
const PanelFn kPanelFns[P_COUNT] = {GamePanel,    SpawnPanel,          InspectorPanel,   ValuesPanel, EffectsPanel,
                                    HudPanel,     PathsPanel,          LightsPanel,      PerfPanel,   ConsolePanel,
                                    DesignScenarioPanel, DesignStylePanel, DesignLooksPanel, DesignPainterPanel,
                                    DesignRecordPanel};

// The effects' cost and the paths' state, asked once a second.
void Poll(void) {
  const ULONGLONG now = GetTickCount64();
  if (now - g_qwStatusAt < 1000)
    return;
  g_qwStatusAt = now;
  DesignCall("design_status", "", [](const PortJson &j) {
    FxFields(j["fx"]);
    const PortJson &p = j["paths"];
    g_bPathsOn = p["on"].kind == PortJson::BOOL && p["on"].b;
    g_nPathsUnits = p["units"].Str("selected") == "all" ? 1 : 0;
    g_nPathsPlayer = static_cast<int>(p["player"].Num(-1));
  });
}

}  // namespace

void DesignPanelsInit(void) {
}

std::string DesignPanelsFields(void) {
  std::string shown;
  for (int i = 0; i < P_COUNT; ++i)
    if (g_abShown[i])
      shown += std::string(shown.empty() ? "" : ",") + PortJsonQuote(kPanels[i]);
  return "\"panels\":{\"shown\":[" + shown + "],\"placing\":" +
         (g_placing == PLACE_UNIT ? "\"unit\"" : g_placing == PLACE_FX ? "\"effect\"" : "null") + "}";
}

void DesignMenuBar(void) {
  if (!ImGui::BeginMainMenuBar())
    return;
  ImGui::TextDisabled("Hyperion design");
  if (ImGui::BeginMenu("Window")) {
    if (ImGui::MenuItem("Full screen", "Alt+Enter"))
      DesignWindowFullscreen(true);
    if (ImGui::MenuItem("Windowed"))
      DesignWindowFullscreen(false);
    if (ImGui::MenuItem("Maximized"))
      DesignWindowMaximize();
    ImGui::Separator();
    const int nView = DesignViewMode();
    if (ImGui::MenuItem("Panels beside the game", NULL, nView == DESIGN_VIEW_BESIDE))
      DesignViewSet(DESIGN_VIEW_BESIDE);
    if (ImGui::MenuItem("Panels over the game", NULL, nView == DESIGN_VIEW_OVER))
      DesignViewSet(DESIGN_VIEW_OVER);
    if (ImGui::MenuItem("The game only", "F11", nView == DESIGN_VIEW_GAME))
      DesignViewSet(DESIGN_VIEW_GAME);
    ImGui::Separator();
    static const int kSizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    for (const auto &s : kSizes) {
      char sz[32];
      snprintf(sz, sizeof sz, "%d x %d", s[0], s[1]);
      if (ImGui::MenuItem(sz))
        DesignWindowSet(s[0], s[1]);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Lay the panels out again"))
      DesignLayoutReset();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Panels")) {
    for (int i = 0; i < P_COUNT; ++i)
      ImGui::MenuItem(kPanels[i], NULL, &g_abShown[i]);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Game")) {
    const bool bGame = DesignInGame();
    if (ImGui::MenuItem(DesignGamePaused() ? "Resume" : "Pause", NULL, false, bGame))
      DesignCall(DesignGamePaused() ? "resume" : "pause", "", Say("done"));
    if (ImGui::MenuItem("Step 1 frame", NULL, false, bGame))
      DesignCall("step", "\"frames\":1", Say("stepped"));
    if (ImGui::MenuItem("Step 24 frames", NULL, false, bGame))
      DesignCall("step", "\"frames\":24", Say("stepped"));
    if (ImGui::MenuItem("Reveal the map", NULL, false, bGame))
      DesignCall("reveal", "", Say("revealed"));
    ImGui::Separator();
    if (ImGui::MenuItem("Quit"))
      DesignCall("design_quit", "");
    ImGui::EndMenu();
  }
  const std::string right = g_said + (g_said.empty() ? "" : "   ") + std::to_string(g_portView.nWidth) + " x " +
                            std::to_string(g_portView.nHeight) + "   " +
                            std::to_string(static_cast<int>(ImGui::GetIO().Framerate + 0.5f)) + " fps";
  const float fRight = ImGui::CalcTextSize(right.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2;
  if (ImGui::GetContentRegionAvail().x > fRight) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fRight);
    ImGui::TextDisabled("%s", right.c_str());
  }
  ImGui::EndMainMenuBar();
}

void DesignPanels(void) {
  g_afFrameMs[g_nFrameAt] = ImGui::GetIO().DeltaTime * 1000.0f;
  g_nFrameAt = (g_nFrameAt + 1) % static_cast<int>(sizeof g_afFrameMs / sizeof g_afFrameMs[0]);
  Poll();
  for (int i = 0; i < P_COUNT; ++i) {
    if (!g_abShown[i])
      continue;
    if (ImGui::Begin(kPanels[i], &g_abShown[i]))
      kPanelFns[i]();
    ImGui::End();
  }
}

void DesignDefaultLayout(ImGuiID dock, const ImVec2 &size) {
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, size);
  // the panels down the right and a strip along the foot, so the game keeps a wide room
  ImGuiID center = dock;
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, NULL, &center);
  ImGuiID rightTop = right;
  const ImGuiID rightFoot = ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, 0.36f, NULL, &rightTop);
  // the scenario and its style over the rest, as what a test starts from
  const ImGuiID rightHead = ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Up, 0.42f, NULL, &rightTop);
  const ImGuiID foot = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.2f, NULL, &center);
  ImGui::DockBuilderDockWindow("Scenario", rightHead);
  ImGui::DockBuilderDockWindow("Style", rightHead);
  ImGui::DockBuilderDockWindow("Record", rightHead);
  ImGui::DockBuilderDockWindow("Spawn", rightTop);
  ImGui::DockBuilderDockWindow("Effects", rightTop);
  ImGui::DockBuilderDockWindow("Values", rightTop);
  ImGui::DockBuilderDockWindow("Lights", rightTop);
  ImGui::DockBuilderDockWindow("Inspector", rightFoot);
  ImGui::DockBuilderDockWindow("HUD", rightFoot);
  ImGui::DockBuilderDockWindow("Paths", rightFoot);
  ImGui::DockBuilderDockWindow("Looks", rightFoot);
  ImGui::DockBuilderDockWindow("Painter", rightFoot);
  ImGui::DockBuilderDockWindow("Game", foot);
  ImGui::DockBuilderDockWindow("Console", foot);
  ImGui::DockBuilderDockWindow("Perf", foot);
  ImGui::DockBuilderFinish(dock);
}

bool DesignPanelsPress(float fX, float fY, int nButton) {
  if (g_placing == PLACE_NONE)
    return false;
  if (nButton == 3) {
    g_placing = PLACE_NONE;
    return true;
  }
  if (nButton != 1)
    return false;
  int nMapX = 0, nMapY = 0;
  if (!DesignWindowToMap(fX, fY, &nMapX, &nMapY))
    return false;
  if (g_placing == PLACE_UNIT)
    Spawn(g_nUnit, nMapX, nMapY, g_nPlayer, g_nCount);
  else
    FxPlay(",\"at\":[" + std::to_string(nMapX) + "," + std::to_string(nMapY) + "]");
  return true;
}
