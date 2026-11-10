// studio_modeshow.cpp -- the Mode workspace's two views drawn from the mode's own Luau: Set-up,
// the mode's tab as a player meets it in the game's set-up, and Preview, what the mode's preview
// draws of a map.
//
// Both read the mod through StudioModeDecl, which gives the engine's own calls a declaration made
// from the studio's model: so the words and the numbers a modder types show at once, while the
// Luau the calls run is the mod folder's and shows once a script is saved. Neither view changes
// the mod. The values the player would set are the two views' own, kept here and shared, and the
// map is the one both read.
//
//   studio_mode_seen {setting?, value?, players?, map?, preset?, bases?}   the set-up as a player sees it
//   studio_mode_preview {map?, settings?}                                 the shapes the preview draws
#define NOMINMAX
#include "../h/platform.h"
#include "../h/glues.h"   // mem_free
#include "../h/maphdr.h"  // get_map_chk
#include <algorithm>
#include <map>
#include <math.h>
#include <string>
#include <vector>
#include "imgui_internal.h"                 // RegisterUserTexture, ImTextureDataQueueUpload
#include "../port/data/port_mode.h"         // PortModeDecl and the set-up's own calls
#include "../port/data/port_modemap.h"      // ModeMapOf: the starts and the bases
#include "../port/front/port_frontlobby.h"  // FrontKit::MapMinimapRgba
#include "../port/tools/port_control.h"     // PortControlCommand
#include "studio_draw.h"
#include "studio_media.h"                   // StudioMediaPara
#include "studio_modes.h"

// 0x66FD4C ghScenArchive (src117/lang/maphdr.cpp): the scenario archive a running game keeps
// open as its map's. get_map_chk opens one of its own, so the studio's canvas game's handle is
// put aside over the call and given back, as port_modemap.cpp does it.
extern HANDLE ghScenArchive;

namespace {

using namespace studio_draw;

// ---- what the two views share ----------------------------------------------------------------

std::string g_map;                            // the map both read, a path
std::map<std::string, std::string> g_values;  // each setting's value as JSON text
int g_nPlayers = 2;                           // the players the set-up would seat
std::string g_mode;                           // the mode the values were filled for

// Every setting at its base, as the set-up starts a player off.
void FromBases(const PortModeDecl &d) {
  g_values.clear();
  for (const PortModeSetting &s : d.settings)
    g_values[s.id] = s.base;
  g_mode = d.id;
}

// The values filled in once for the mode open, and again for another mod, so a view opened finds
// something to draw. A setting added since is filled in from its base and one taken away is
// dropped, so the set never names a setting the mode has not got.
void Settle(const PortModeDecl &d) {
  if (g_mode != d.id) {
    FromBases(d);
    return;
  }
  std::map<std::string, std::string> out;
  for (const PortModeSetting &s : d.settings) {
    const std::map<std::string, std::string>::const_iterator it = g_values.find(s.id);
    out[s.id] = it == g_values.end() ? s.base : it->second;
  }
  g_values.swap(out);
}

// The values as one JSON object with the players beside them, which is what every call of the
// mode's is given (PortModeSettingsWhole's shape, and the set-up's own Whole).
std::string Whole(const PortModeDecl &d) {
  std::string out = "{";
  for (const PortModeSetting &s : d.settings) {
    const std::map<std::string, std::string>::const_iterator it = g_values.find(s.id);
    if (it == g_values.end())
      continue;
    PortJson value;
    if (!PortJsonParse(it->second.c_str(), &value))
      continue;
    out += (out.size() > 1 ? "," : "") + Q(s.id) + ":" + PortJsonWrite(value);
  }
  return out + "," + Q("players") + ":" + std::to_string(g_nPlayers) + "}";
}

const char *MapPath(void) {
  return g_map.empty() ? nullptr : g_map.c_str();
}

std::string MapShown(void) {
  if (g_map.empty())
    return "a map to read";
  const size_t at = g_map.find_last_of("\\/");
  return at == std::string::npos ? g_map : g_map.substr(at + 1);
}

// A row of a form, as the workspace's other views draw one.
float LabelW(void) {
  return P(150.0f);
}

void Row(const char *pszLabel) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y + P(6.0f), C(TK_LABEL), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(at.x + LabelW(), at.y));
}

// The map pick both views draw, the one map between them, and Read again: what a call of the
// mode's answered is kept by what it was asked, so a script saved since needs the kept answers
// dropped before its words show.
void MapRow(void) {
  Row("Map");
  const std::vector<StudioModeMap> &maps = StudioModeMaps();
  if (StudioSelectBegin("seenmap", MapShown().c_str(), P(280.0f))) {
    for (size_t i = 0; i < maps.size() && i < 400; ++i)
      if (StudioItem(maps[i].name.c_str(), maps[i].path == g_map))
        g_map = maps[i].path;
    StudioSelectEnd();
  }
  ImGui::SameLine();
  if (StudioButton("again", "Read again", "again", SB_FLAT, 26.0f))
    PortModeForget();
}

// A wrapped paragraph at the cursor, as the studio's pages draw one.
void Para(const std::string &text, float fW, ImU32 col, float fPx = 12.5f) {
  StudioMediaPara(text, fW, col, fPx);
}

// ---- the Set-up view -------------------------------------------------------------------------
// The mode's own tab, drawn the way the set-up draws it: the head, the card of what it is, and
// the settings by group with each one's own sentence under it. What it adds past the set-up is
// the two things a modder cannot see in a game: the setting a player is never shown, and which
// of the mode's functions each line came from.

// A value as JSON text, set.
void Set(const std::string &id, const std::string &json) {
  g_values[id] = json;
}

PortJson ValueOf(const std::string &id) {
  PortJson j;
  const std::map<std::string, std::string>::const_iterator it = g_values.find(id);
  if (it != g_values.end())
    PortJsonParse(it->second.c_str(), &j);
  return j;
}

// A list's items that are turned off, by their ids.
bool ListHas(const PortJson &v, const std::string &id) {
  for (size_t i = 0; i < v.size(); ++i)
    if (v.At(i).Str() == id)
      return true;
  return false;
}

// One setting's control, drawn as the set-up draws its kind. True when the value changed.
bool Control(const PortModeSetting &s, float fW) {
  const PortJson v = ValueOf(s.id);
  if (s.kind == "choice") {
    // the choices as a segmented row while they fit, else a pulldown
    std::vector<const char *> names;
    int nAt = 0;
    for (size_t i = 0; i < s.items.size(); ++i) {
      names.push_back(s.items[i].name.c_str());
      if (s.items[i].id == v.Str())
        nAt = static_cast<int>(i);
    }
    if (names.empty())
      return false;
    const int n = static_cast<int>(names.size());
    if (StudioSegWidth(names.data(), n) <= fW * 1.7f) {
      const int nPick = StudioSeg("c", names.data(), n, nAt);
      if (nPick != nAt) {
        Set(s.id, Q(s.items[nPick].id));
        return true;
      }
      return false;
    }
    if (StudioSelectBegin("c", names[nAt], fW)) {
      for (int i = 0; i < n; ++i)
        if (StudioItem(names[i], i == nAt)) {
          Set(s.id, Q(s.items[i].id));
          StudioSelectEnd();
          return true;
        }
      StudioSelectEnd();
    }
    return false;
  }
  if (s.kind == "toggle") {
    bool b = v.kind == PortJson::BOOL && v.b;
    if (StudioCheck("t", b ? "on" : "off", &b)) {
      Set(s.id, b ? "true" : "false");
      return true;
    }
    return false;
  }
  if (s.kind == "number" || s.kind == "seed") {
    double d = v.Num(0);
    bool bChanged = false;
    if (StudioNumber("n", &d, P(110.0f), P(26.0f), true)) {
      if (s.kind == "number") {
        d = d < s.min ? s.min : d > s.max ? s.max : d;
        if (s.step > 0)
          d = s.min + floor((d - s.min) / s.step + 0.5) * s.step;
      }
      Set(s.id, Fmt(d, 3));
      bChanged = true;
    }
    if (!s.unit.empty()) {
      ImGui::SameLine();
      const ImVec2 at = ImGui::GetCursorScreenPos();
      T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, at.x + P(6.0f), at.y + P(8.0f), C(TK_INK3), s.unit);
      ImGui::Dummy(ImVec2(P(60.0f), P(26.0f)));
    }
    return bChanged;
  }
  if (s.kind == "list") {
    // a list's value is the items turned off, so a box ticked is one left in the game
    bool bChanged = false;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    float y = at.y;
    for (size_t i = 0; i < s.items.size(); ++i) {
      bool bIn = !ListHas(v, s.items[i].id);
      ImGui::SetCursorScreenPos(ImVec2(at.x, y));
      ImGui::PushID(static_cast<int>(i));
      if (StudioCheck("i", s.items[i].name.c_str(), &bIn)) {
        PortJson out;
        out.kind = PortJson::ARRAY;
        for (const PortModeItem &o : s.items) {
          const bool bOff = o.id == s.items[i].id ? !bIn : ListHas(v, o.id);
          if (!bOff)
            continue;
          PortJson one;
          one.kind = PortJson::STRING;
          one.s = o.id;
          out.items.push_back(one);
        }
        Set(s.id, PortJsonWrite(out));
        bChanged = true;
      }
      ImGui::PopID();
      y += P(22.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    ImGui::Dummy(ImVec2(fW, P(2.0f)));
    return bChanged;
  }
  return false;
}

// The head: the name the Game type list calls it, the mod its row names, and its line.
void Head(const PortModeDecl &d, float fW) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const std::string name = d.typeName.empty() ? d.name : d.typeName;
  ImVec2 at = ImGui::GetCursorScreenPos();
  float x = at.x + T(dl, STUDIO_FONT_BOLD, 17.0f, at.x, at.y, C(TK_INK), d.version.empty() ? name : name + " " + d.version);
  // a mode that declares no game_type takes its mod's name for both, and a row saying the same
  // word twice says nothing, so the tag is left out there as the set-up leaves it out
  if (!d.typeTag.empty() && d.typeTag != name)
    StudioChip(dl, ImVec2(x + P(10.0f), at.y + P(2.0f)), ("from " + d.typeTag + " mod").c_str(), CH_GOLD);
  ImGui::Dummy(ImVec2(fW, P(26.0f)));
  if (!d.line.empty())
    Para(d.line, fW * 2.2f, C(TK_INK2), 14.0f);
}

// The card under the head: what it is, who plays it, what it needs, and its notes. Its ground is
// drawn on a channel of its own, since how tall it is is only known once its words are laid out.
void Card(const PortModeDecl &d, float fW) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float fIn = P(14.0f), fBox = fW * 2.3f, fIns = fBox - fIn * 2.0f;
  dl->ChannelsSplit(2);
  dl->ChannelsSetCurrent(1);
  ImGui::SetCursorScreenPos(ImVec2(a.x + fIn, a.y + fIn));
  ImGui::BeginGroup();
  if (!d.about.empty())
    Para(d.about, fIns, C(TK_INK2));
  ImGui::Dummy(ImVec2(0, P(6.0f)));
  // the chips the row carries: how many play it, whether it changes the simulation, its own
  // enemy, and what it needs
  ImVec2 at = ImGui::GetCursorScreenPos();
  float x = at.x;
  char sz[80];
  if (d.playersMin == d.playersMax)
    snprintf(sz, sizeof sz, d.playersMin == 1 ? "%d player" : "%d players", d.playersMin);
  else
    snprintf(sz, sizeof sz, "%d to %d players", d.playersMin, d.playersMax);
  x += StudioChip(dl, ImVec2(x, at.y), sz) + P(6.0f);
  x += StudioChip(dl, ImVec2(x, at.y), d.sim ? "changes the simulation" : "presentation only",
                  d.sim ? CH_WARN : CH_GOOD) + P(6.0f);
  if (d.enemyPlayer)
    x += StudioChip(dl, ImVec2(x, at.y), "plays its own enemy") + P(6.0f);
  if (!d.needs.empty() && d.needs != "any race")
    x += StudioChip(dl, ImVec2(x, at.y), ("needs " + d.needs).c_str(), CH_WARN) + P(6.0f);
  ImGui::Dummy(ImVec2(fIns, P(24.0f)));
  for (const std::string &note : d.notes) {
    at = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(at.x + P(3.0f), at.y + P(6.0f)), P(2.0f), C(TK_HI));
    ImGui::SetCursorScreenPos(ImVec2(at.x + P(12.0f), at.y));
    Para(note, fIns - P(12.0f), C(TK_INK3));
    ImGui::Dummy(ImVec2(0, P(2.0f)));
  }
  ImGui::EndGroup();
  const ImVec2 b(a.x + fBox, ImGui::GetItemRectMax().y + fIn);
  dl->ChannelsSetCurrent(0);
  dl->AddRectFilled(a, b, C(TK_PANE2), P(6.0f));
  dl->AddRect(a, b, C(TK_LINE), P(6.0f));
  dl->ChannelsMerge();
  ImGui::SetCursorScreenPos(a);
  ImGui::Dummy(ImVec2(fBox, b.y - a.y));
}

// The presets, as a row of buttons: the mode's own sets of values.
void Presets(const PortModeDecl &d, float fW) {
  if (d.presets.empty())
    return;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 at = ImGui::GetCursorScreenPos();
  Wide(dl, 11.0f, at.x, at.y, C(TK_HEAD), "Presets", 0.08f);
  ImGui::Dummy(ImVec2(0, P(22.0f)));
  for (size_t i = 0; i < d.presets.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (StudioButton("p", d.presets[i].name.c_str(), nullptr, SB_PLAIN, 26.0f)) {
      FromBases(d);
      PortJson values;
      if (PortJsonParse(d.presets[i].values.c_str(), &values) && values.kind == PortJson::OBJECT)
        for (const PortJsonMember &m : values.members)
          g_values[m.first] = PortJsonWrite(m.second);
    }
    ImGui::PopID();
    if (i + 1 < d.presets.size())
      ImGui::SameLine();
  }
  ImGui::SameLine();
  if (StudioButton("bases", "Back to the bases", nullptr, SB_FLAT, 26.0f))
    FromBases(d);
  ImGui::Dummy(ImVec2(0, P(14.0f)));
  (void)fW;
}

// One setting written out: its name, its control, its gist, and the two lines the mode's own
// functions give for the value as it stands.
void SettingRows(const PortModeSetting &s, const std::string &eff, const std::string &read, float fW) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImGui::PushID(s.id.c_str());
  const ImVec2 at = ImGui::GetCursorScreenPos();
  T(dl, STUDIO_FONT_BOLD, kStudioFsS, at.x, at.y + P(6.0f), C(TK_INK), s.name.empty() ? s.id : s.name);
  ImGui::SetCursorScreenPos(ImVec2(at.x + LabelW() + P(30.0f), at.y));
  ImGui::BeginGroup();
  Control(s, fW);
  ImGui::EndGroup();
  const float fTop = at.y;
  float y = std::max(ImGui::GetItemRectMax().y, fTop + P(26.0f));
  const float fTextX = at.x + LabelW() + P(30.0f);
  const float fTextW = fW * 1.7f;
  if (!s.gist.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(fTextX, y + P(4.0f)));
    Para(s.gist, fTextW, C(TK_INK3), 11.5f);
    y = ImGui::GetCursorScreenPos().y;
  }
  // what the mode says of this value: the effect's sentence, then what the map makes of it
  for (int i = 0; i < 2; ++i) {
    const std::string &text = i == 0 ? eff : read;
    const std::string &fn = i == 0 ? s.effect : s.read;
    if (text.empty())
      continue;
    ImGui::SetCursorScreenPos(ImVec2(fTextX, y + P(3.0f)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + P(2.0f), p.y + P(14.0f)), C(i == 0 ? TK_HI : TK_GOOD));
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(10.0f), p.y));
    Para(text, fTextW - P(10.0f), C(i == 0 ? TK_INK2 : TK_GOOD), 11.5f);
    y = ImGui::GetCursorScreenPos().y;
    T(dl, STUDIO_FONT_MONO, 9.0f, p.x + P(10.0f), y, C(TK_INK_OFF), fn);
    y += P(13.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(at.x, y + P(10.0f)));
  ImGui::Dummy(ImVec2(fW, P(1.0f)));
  ImGui::PopID();
}

void SeenBody(const PortModeDecl &d, float fW) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const std::string whole = Whole(d);
  const char *pszMap = MapPath();
  // the mode's own calls, each kept by what it was asked (PortModeEffect's own cache), so the
  // view asking every frame costs one run of the Luau for each set of values
  std::vector<char> shown(d.settings.size(), 1);
  std::vector<std::string> eff(d.settings.size()), read(d.settings.size());
  for (size_t i = 0; i < d.settings.size(); ++i) {
    shown[i] = PortModeShown(d, d.settings[i], whole) ? 1 : 0;
    eff[i] = PortModeEffect(d, d.settings[i], whole);
    read[i] = PortModeRead(d, d.settings[i], whole, pszMap);
  }

  Head(d, fW);
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  Card(d, fW);
  ImGui::Dummy(ImVec2(0, P(16.0f)));
  Presets(d, fW);

  // the groups, in the order the mode declares its settings
  std::vector<std::string> groups;
  for (size_t i = 0; i < d.settings.size(); ++i) {
    if (!shown[i])
      continue;
    const std::string name = d.settings[i].group.empty() ? std::string("Settings") : d.settings[i].group;
    if (std::find(groups.begin(), groups.end(), name) == groups.end())
      groups.push_back(name);
  }
  for (const std::string &group : groups) {
    ImVec2 at = ImGui::GetCursorScreenPos();
    Wide(dl, 11.0f, at.x, at.y, C(TK_HEAD), group, 0.08f);
    dl->AddLine(ImVec2(at.x, at.y + P(18.0f)), ImVec2(at.x + fW * 2.3f, at.y + P(18.0f)), C(TK_LINE));
    ImGui::Dummy(ImVec2(0, P(28.0f)));
    for (size_t i = 0; i < d.settings.size(); ++i) {
      const std::string name = d.settings[i].group.empty() ? std::string("Settings") : d.settings[i].group;
      if (!shown[i] || name != group)
        continue;
      SettingRows(d.settings[i], eff[i], read[i], fW);
    }
    ImGui::Dummy(ImVec2(0, P(8.0f)));
  }

  // the settings a player never meets: one whose shown answers false for these values, and one
  // whose shown is always false, which is the mode's own for a tool. A game shows neither, so
  // the studio is the only place a modder can see them at all
  bool bAny = false;
  for (size_t i = 0; i < d.settings.size(); ++i)
    bAny = bAny || !shown[i];
  if (bAny) {
    ImVec2 at = ImGui::GetCursorScreenPos();
    Wide(dl, 11.0f, at.x, at.y, C(TK_HEAD), "Not shown to a player", 0.08f);
    ImGui::Dummy(ImVec2(0, P(22.0f)));
    Para("A setting whose shown answers false for these values. One that always answers false is "
         "the mode's own, for the command line and the studio rather than for a player.",
         fW * 2.0f, C(TK_INK3), 11.5f);
    ImGui::Dummy(ImVec2(0, P(6.0f)));
    for (size_t i = 0; i < d.settings.size(); ++i) {
      if (shown[i])
        continue;
      at = ImGui::GetCursorScreenPos();
      float x = at.x + T(dl, STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y + P(3.0f), C(TK_INK2),
                         d.settings[i].name.empty() ? d.settings[i].id : d.settings[i].name) +
                P(10.0f);
      x += StudioChip(dl, ImVec2(x, at.y + P(2.0f)), d.settings[i].kind.c_str()) + P(8.0f);
      T(dl, STUDIO_FONT_MONO, 9.5f, x, at.y + P(5.0f), C(TK_INK_OFF), d.settings[i].shown);
      ImGui::Dummy(ImVec2(fW * 2.0f, P(22.0f)));
    }
  }
  ImGui::Dummy(ImVec2(0, P(20.0f)));
}

// ---- the Preview view ------------------------------------------------------------------------
// The mode's preview run for the map and the values the Set-up view holds, and its shapes drawn
// over the map's picture: the shapes the set-up's own map box draws, in the map's own pixels,
// brought down by the picture's scale.

struct Tex {
  ImTextureData *p = nullptr;
};
Tex g_tex;
std::vector<ImTextureData *> g_dying;

void Drop(Tex *pT) {
  if (pT->p != nullptr) {
    pT->p->WantDestroyNextFrame = true;
    g_dying.push_back(pT->p);
  }
  pT->p = nullptr;
}

void TexTick(void) {
  for (std::vector<ImTextureData *>::iterator it = g_dying.begin(); it != g_dying.end();) {
    if ((*it)->Status == ImTextureStatus_Destroyed) {
      ImGui::UnregisterUserTexture(*it);
      IM_DELETE(*it);
      it = g_dying.erase(it);
    } else {
      ++it;
    }
  }
}

void TexSet(Tex *pT, const std::vector<unsigned char> &rgba, int w, int h) {
  Drop(pT);
  if (rgba.size() < static_cast<size_t>(w) * h * 4)
    return;
  ImTextureData *p = IM_NEW(ImTextureData)();
  p->Create(ImTextureFormat_RGBA32, w, h);
  memcpy(p->Pixels, rgba.data(), static_cast<size_t>(w) * h * 4);
  p->UseColors = true;
  ImGui::RegisterUserTexture(p);
  pT->p = p;
}

// The map's picture, read once for each map: its minimap as RGBA, at most 512 pixels a side.
struct Picture {
  std::string of;
  int w = 0, h = 0;  // the picture's own pixels
  bool bTried = false;
  std::string why;
};
Picture g_pic;

void PictureRead(void) {
  if (g_pic.of == g_map && g_pic.bTried)
    return;
  g_pic = Picture();
  g_pic.of = g_map;
  g_pic.bTried = true;
  Drop(&g_tex);
  if (g_map.empty())
    return;
  const HANDLE hGame = ghScenArchive;
  ghScenArchive = NULL;
  DWORD cb = 0;
  BYTE *pb = static_cast<BYTE *>(get_map_chk(g_map.c_str(), &cb));
  ghScenArchive = hGame;
  if (pb == NULL) {
    g_pic.why = "the map does not open";
    return;
  }
  std::vector<unsigned char> rgba;
  int w = 0, h = 0;
  const bool bOk = FrontKit::MapMinimapRgba(pb, cb, 512, &rgba, &w, &h);
  mem_free(pb, __FILE__, __LINE__);
  if (!bOk || w <= 0 || h <= 0) {
    g_pic.why = "the map's tileset cannot be read";
    return;
  }
  g_pic.w = w;
  g_pic.h = h;
  TexSet(&g_tex, rgba, w, h);
}

// A stroke cut into dashes, as the set-up's picture cuts one.
void ShapeLine(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float fWidth, float fDash) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float fLen = sqrtf(dx * dx + dy * dy);
  if (fLen <= 0.0f)
    return;
  if (fDash <= 0.0f) {
    dl->AddLine(a, b, col, fWidth);
    return;
  }
  const float fStep = std::max(fDash, 1.0f) * 2.0f;
  if (fLen / fStep > 96.0f) {
    dl->AddLine(a, b, col, fWidth);
    return;
  }
  for (float at = 0.0f; at < fLen; at += fStep) {
    const float to = std::min(at + fStep * 0.5f, fLen);
    dl->AddLine(ImVec2(a.x + dx * at / fLen, a.y + dy * at / fLen), ImVec2(a.x + dx * to / fLen, a.y + dy * to / fLen),
                col, fWidth);
  }
}

void ShapeCircle(ImDrawList *dl, ImVec2 c, float r, ImU32 col, float fWidth, float fDash) {
  if (r <= 0.0f)
    return;
  if (fDash <= 0.0f) {
    dl->AddCircle(c, r, col, 0, fWidth);
    return;
  }
  const float fStep = std::max(fDash, 1.0f) * 2.0f;
  int n = static_cast<int>(6.2831853f * r / fStep);
  n = n < 4 ? 4 : n > 96 ? 96 : n;
  for (int i = 0; i < n * 2; i += 2) {
    const float a0 = 3.1415927f * i / n, a1 = 3.1415927f * (i + 1) / n;
    dl->AddLine(ImVec2(c.x + r * cosf(a0), c.y + r * sinf(a0)), ImVec2(c.x + r * cosf(a1), c.y + r * sinf(a1)), col,
                fWidth);
  }
}

// The shapes over the picture: one map pixel is fScale of the box's, from (ox, oy).
void DrawShapes(ImDrawList *dl, const std::vector<PortModeShape> &shapes, ImVec2 o, float fScale) {
  for (const PortModeShape &s : shapes) {
    const ImVec2 at(o.x + static_cast<float>(s.x) * fScale, o.y + static_cast<float>(s.y) * fScale);
    const float r = static_cast<float>(s.r) * fScale;
    const float sw = static_cast<float>(s.w) * fScale, sh = static_cast<float>(s.h) * fScale;
    const float fWidth = std::max(1.0f, static_cast<float>(s.fWidth) * fScale);
    const float fDash = static_cast<float>(s.fDash) * fScale;
    const ImU32 fill = IM_COL32((s.nRgb >> 16) & 0xFF, (s.nRgb >> 8) & 0xFF, s.nRgb & 0xFF,
                                static_cast<int>(s.fFill * 255.0 + 0.5));
    const ImU32 edge = IM_COL32((s.nRgb >> 16) & 0xFF, (s.nRgb >> 8) & 0xFF, s.nRgb & 0xFF,
                                static_cast<int>(s.fEdge * 255.0 + 0.5));
    std::vector<ImVec2> pts;
    if (s.shape == "polygon")
      for (size_t i = 0; i + 1 < s.points.size(); i += 2)
        pts.push_back(ImVec2(o.x + static_cast<float>(s.points[i]) * fScale,
                             o.y + static_cast<float>(s.points[i + 1]) * fScale));
    if (s.fFill > 0.004) {
      if (s.shape == "circle" && r > 0.0f)
        dl->AddCircleFilled(at, r, fill);
      else if (s.shape == "rect" && sw > 0.0f && sh > 0.0f)
        dl->AddRectFilled(at, ImVec2(at.x + sw, at.y + sh), fill);
      else if (s.shape == "polygon" && pts.size() >= 3)
        dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), fill);
    }
    if (s.fEdge <= 0.004)
      continue;
    if (s.shape == "circle" || s.shape == "ring") {
      ShapeCircle(dl, at, r, edge, fWidth, fDash);
    } else if (s.shape == "rect") {
      if (sw <= 0.0f || sh <= 0.0f)
        continue;
      ShapeLine(dl, at, ImVec2(at.x + sw, at.y), edge, fWidth, fDash);
      ShapeLine(dl, ImVec2(at.x + sw, at.y), ImVec2(at.x + sw, at.y + sh), edge, fWidth, fDash);
      ShapeLine(dl, ImVec2(at.x + sw, at.y + sh), ImVec2(at.x, at.y + sh), edge, fWidth, fDash);
      ShapeLine(dl, ImVec2(at.x, at.y + sh), at, edge, fWidth, fDash);
    } else if (s.shape == "polygon") {
      for (size_t i = 0; i < pts.size() && pts.size() >= 2; ++i)
        ShapeLine(dl, pts[i], pts[(i + 1) % pts.size()], edge, fWidth, fDash);
    } else if (s.shape == "line") {
      ShapeLine(dl, at, ImVec2(o.x + static_cast<float>(s.x2) * fScale, o.y + static_cast<float>(s.y2) * fScale), edge,
                fWidth, fDash);
    }
  }
}

// A shape's fields in one line, so a modder can read the numbers the call gave back.
std::string ShapeWords(const PortModeShape &s) {
  std::string out = s.shape;
  char sz[200];
  if (s.shape == "circle" || s.shape == "ring")
    snprintf(sz, sizeof sz, " at %.0f, %.0f  r %.0f", s.x, s.y, s.r);
  else if (s.shape == "rect")
    snprintf(sz, sizeof sz, " at %.0f, %.0f  %.0f by %.0f", s.x, s.y, s.w, s.h);
  else if (s.shape == "line")
    snprintf(sz, sizeof sz, " %.0f, %.0f to %.0f, %.0f", s.x, s.y, s.x2, s.y2);
  else
    snprintf(sz, sizeof sz, " %u corners", static_cast<unsigned>(s.points.size() / 2));
  out += sz;
  snprintf(sz, sizeof sz, "  #%06x  fill %.2f  edge %.2f  width %.0f%s", s.nRgb & 0xffffffu, s.fFill, s.fEdge,
           s.fWidth, s.fDash > 0 ? "  dashed" : "");
  return out + sz;
}

void PreviewBody(const PortModeDecl &d, float fW) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  TexTick();
  MapRow();
  ImGui::Dummy(ImVec2(0, P(10.0f)));

  if (d.preview.empty()) {
    Para("This mode names no preview, so the set-up shows the map alone and leaves its caption out. "
         "A mode may name a function in its mode block that is given the settings and the map and "
         "gives back the shapes it draws over the map's picture.",
         fW * 2.2f, C(TK_INK3));
    return;
  }
  if (g_map.empty()) {
    Para("Choose a map and the mode's preview is run for it, with the settings the Set-up view holds.", fW * 2.2f,
         C(TK_INK3));
    return;
  }
  PictureRead();
  const std::string whole = Whole(d);
  std::vector<PortModeShape> shapes;
  const bool bAny = PortModePreview(d, whole, g_map.c_str(), &shapes);
  const ModeMapRead *pMap = ModeMapOf(g_map.c_str());

  // the picture, at the box's own size, and the shapes over it
  const float fBox = std::min(fW * 2.0f, P(520.0f));
  ImVec2 pos = ImGui::GetCursorScreenPos();
  const int tw = pMap != nullptr ? pMap->w : 0, th = pMap != nullptr ? pMap->h : 0;
  float fPicW = fBox, fPicH = fBox;
  if (g_pic.w > 0 && g_pic.h > 0) {
    const float k = std::min(fBox / g_pic.w, fBox / g_pic.h);
    fPicW = g_pic.w * k;
    fPicH = g_pic.h * k;
  } else if (tw > 0 && th > 0) {
    const float k = std::min(fBox / static_cast<float>(tw), fBox / static_cast<float>(th));
    fPicW = tw * k;
    fPicH = th * k;
  }
  const ImVec2 a = pos, b(pos.x + fPicW, pos.y + fPicH);
  dl->AddRectFilled(a, b, C(TK_WELL));
  if (g_tex.p != nullptr)
    dl->AddImage(g_tex.p->GetTexRef(), a, b);
  // one map pixel in the box's: the picture covers the whole map, so its width over the map's
  const float fScale = tw > 0 ? fPicW / (tw * 32.0f) : 0.0f;
  if (fScale > 0.0f && pMap != nullptr) {
    // the starts and the resource clusters, as the set-up's picture marks them
    for (const ModeMapBase &one : pMap->bases)
      dl->AddCircleFilled(ImVec2(a.x + one.x * fScale, a.y + one.y * fScale), P(2.5f), C(TK_GOOD, 0.8f));
    for (const ModeMapStart &one : pMap->starts) {
      const ImVec2 at(a.x + one.x * fScale, a.y + one.y * fScale);
      dl->AddCircleFilled(at, P(5.0f), C(TK_BG, 0.7f));
      dl->AddCircle(at, P(5.0f), C(TK_INK), 0, P(1.5f));
      char sz[8];
      snprintf(sz, sizeof sz, "%d", one.nSlot + 1);
      T(dl, STUDIO_FONT_MONO, 9.0f, at.x - P(2.5f), at.y - P(4.5f), C(TK_INK), sz);
    }
    dl->PushClipRect(a, b, true);
    DrawShapes(dl, shapes, a, fScale);
    dl->PopClipRect();
  }
  dl->AddRect(a, b, C(TK_LINE));
  ImGui::Dummy(ImVec2(fPicW, fPicH + P(8.0f)));

  // the map's own line and, where the mode drew something, the caption the set-up puts under
  // the picture. The studio has no seating, so its starts are never a player's, which is the
  // half of the caption a game with fixed starts leaves out
  if (pMap != nullptr) {
    char sz[120];
    snprintf(sz, sizeof sz, "%u %s  ·  %d x %d tiles", static_cast<unsigned>(pMap->starts.size()),
             pMap->starts.size() == 1 ? "start" : "starts", tw, th);
    pos = ImGui::GetCursorScreenPos();
    T(dl, STUDIO_FONT_TEXT, kStudioFsS, pos.x, pos.y, C(TK_INK_OFF), sz);
    ImGui::Dummy(ImVec2(fW, P(20.0f)));
  }
  const std::string name = d.typeName.empty() ? d.name : d.typeName;
  if (bAny)
    Para("Drawn as " + name + " reads this map. Starts are drawn at random, so a place here is not "
         "a player’s.",
         fW * 2.2f, C(TK_INK3), 11.5f);
  ImGui::Dummy(ImVec2(0, P(10.0f)));

  if (!g_pic.why.empty()) {
    Para("The map's picture does not show: " + g_pic.why + ". The shapes are drawn over the box all the same, "
         "since the preview is read from the map and not from its picture.",
         fW * 2.2f, C(TK_WARN), 11.5f);
    ImGui::Dummy(ImVec2(0, P(8.0f)));
  }
  if (!bAny) {
    Para("The preview gave nothing back for this map and these settings, so the set-up would show the map "
         "alone. Its own problems are named in hyperion_mods.log, with the mode and the function.",
         fW * 2.2f, C(TK_INK3), 11.5f);
    return;
  }

  pos = ImGui::GetCursorScreenPos();
  Wide(dl, 11.0f, pos.x, pos.y, C(TK_HEAD), "What it gave back", 0.08f);
  ImGui::Dummy(ImVec2(0, P(24.0f)));
  for (size_t i = 0; i < shapes.size(); ++i) {
    pos = ImGui::GetCursorScreenPos();
    T(dl, STUDIO_FONT_MONO, 10.5f, pos.x, pos.y + P(2.0f), C(TK_INK2), ShapeWords(shapes[i]));
    ImGui::Dummy(ImVec2(fW * 2.2f, P(17.0f)));
  }
  ImGui::Dummy(ImVec2(0, P(10.0f)));
  pos = ImGui::GetCursorScreenPos();
  T(dl, STUDIO_FONT_TEXT, kStudioFsXs, pos.x, pos.y, C(TK_INK_OFF),
    std::to_string(shapes.size()) + " of the 64 the set-up draws  ·  lengths in map pixels, 32 to the tile");
  ImGui::Dummy(ImVec2(0, P(20.0f)));
}

// ---- the commands ----------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

// A setting's value as the view holds it, or set; and the words the mode gives for it.
bool CmdModeSeen(const PortJson &j, std::string *pAnswer) {
  PortModeDecl d;
  std::string why;
  if (!StudioModeDecl(&d, &why)) {
    *pAnswer = why;
    return false;
  }
  Settle(d);
  if (!j["map"].Str().empty())
    g_map = j["map"].Str();
  if (j["players"].kind == PortJson::NUMBER)
    g_nPlayers = static_cast<int>(j["players"].Num(2));
  if (j["bases"].kind == PortJson::BOOL && j["bases"].b)
    FromBases(d);
  if (!j["preset"].Str().empty()) {
    bool bFound = false;
    for (const PortModePreset &p : d.presets) {
      if (p.id != j["preset"].Str() && p.name != j["preset"].Str())
        continue;
      bFound = true;
      FromBases(d);
      PortJson values;
      if (PortJsonParse(p.values.c_str(), &values) && values.kind == PortJson::OBJECT)
        for (const PortJsonMember &m : values.members)
          g_values[m.first] = PortJsonWrite(m.second);
    }
    if (!bFound) {
      *pAnswer = "the mode has no preset " + j["preset"].Str();
      return false;
    }
  }
  if (!j["setting"].Str().empty() && j["value"].kind != PortJson::NONE) {
    const std::string id = j["setting"].Str();
    bool bFound = false;
    for (const PortModeSetting &s : d.settings)
      bFound = bFound || s.id == id;
    if (!bFound) {
      *pAnswer = "the mode has no setting " + id;
      return false;
    }
    g_values[id] = PortJsonWrite(j["value"]);
  }
  const std::string whole = Whole(d);
  const char *pszMap = MapPath();
  std::string rows;
  for (const PortModeSetting &s : d.settings) {
    const bool bShown = PortModeShown(d, s, whole);
    rows += std::string(rows.empty() ? "" : ",") + "{\"id\":" + Q(s.id) + ",\"name\":" + Q(s.name) + ",\"kind\":" +
            Q(s.kind) + ",\"group\":" + Q(s.group) + ",\"value\":" + g_values[s.id] + ",\"shown\":" +
            (bShown ? "true" : "false") + ",\"effect\":" + Q(PortModeEffect(d, s, whole)) + ",\"read\":" +
            Q(PortModeRead(d, s, whole, pszMap)) + "}";
  }
  *pAnswer = "\"name\":" + Q(d.typeName.empty() ? d.name : d.typeName) + ",\"tag\":" + Q(d.typeTag) + ",\"plays\":" +
             std::to_string(d.typePlays) + ",\"line\":" + Q(d.line) + ",\"players\":" + std::to_string(g_nPlayers) +
             ",\"map\":" + Q(g_map) + ",\"settings\":[" + rows + "]";
  (void)Sender(j);
  return true;
}

bool CmdModePreview(const PortJson &j, std::string *pAnswer) {
  PortModeDecl d;
  std::string why;
  if (!StudioModeDecl(&d, &why)) {
    *pAnswer = why;
    return false;
  }
  Settle(d);
  if (!j["map"].Str().empty())
    g_map = j["map"].Str();
  if (j["settings"].kind == PortJson::OBJECT) {
    FromBases(d);
    for (const PortJsonMember &m : j["settings"].members)
      g_values[m.first] = PortJsonWrite(m.second);
  }
  if (d.preview.empty()) {
    *pAnswer = "the mode names no preview";
    return false;
  }
  if (g_map.empty()) {
    *pAnswer = "map is the map to read";
    return false;
  }
  std::vector<PortModeShape> shapes;
  const bool bAny = PortModePreview(d, Whole(d), g_map.c_str(), &shapes);
  std::string rows;
  for (const PortModeShape &s : shapes)
    rows += std::string(rows.empty() ? "" : ",") + Q(ShapeWords(s));
  const ModeMapRead *pMap = ModeMapOf(g_map.c_str());
  *pAnswer = "\"preview\":" + Q(d.preview) + ",\"map\":" + Q(g_map) + ",\"tiles\":[" +
             std::to_string(pMap != nullptr ? pMap->w : 0) + "," + std::to_string(pMap != nullptr ? pMap->h : 0) +
             "],\"starts\":" + std::to_string(pMap != nullptr ? pMap->starts.size() : 0) + ",\"drew\":" +
             (bAny ? "true" : "false") + ",\"shapes\":[" + rows + "]";
  return true;
}

}  // namespace

void StudioModeSeenView(float fW) {
  PortModeDecl d;
  std::string why;
  if (!StudioModeDecl(&d, &why)) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y, C(TK_BAD),
      "The mode block does not read: " + why);
    return;
  }
  Settle(d);
  // what the mode's own calls are given beside the values: the map chosen and how many are seated
  MapRow();
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(P(20.0f), P(26.0f)));
  ImGui::SameLine();
  {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    T(dl, STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y + P(6.0f), C(TK_LABEL), "Players");
    ImGui::SetCursorScreenPos(ImVec2(at.x + P(56.0f), at.y));
    double dN = g_nPlayers;
    if (StudioNumber("##seenplayers", &dN, P(70.0f), P(26.0f), true))
      g_nPlayers = static_cast<int>(dN < 1 ? 1 : dN > 8 ? 8 : dN);
  }
  ImGui::Dummy(ImVec2(0, P(8.0f)));
  Para("The mode's own tab, as a player meets it in the set-up: its words, its controls, and the "
       "sentence the mode's own effect and read give for each value. The words and the numbers are "
       "the ones open here; the Luau is read from the mod's folder, so save a script to see its "
       "sentences change.",
       fW * 2.2f, C(TK_INK3), 11.5f);
  ImGui::Dummy(ImVec2(0, P(12.0f)));
  SeenBody(d, fW);
}

void StudioModePreviewView(float fW) {
  PortModeDecl d;
  std::string why;
  if (!StudioModeDecl(&d, &why)) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y, C(TK_BAD),
      "The mode block does not read: " + why);
    return;
  }
  Settle(d);
  PreviewBody(d, fW);
}

void StudioModeShowInit(void) {
  PortControlCommand("studio_mode_seen", CmdModeSeen);
  PortControlCommand("studio_mode_preview", CmdModePreview);
}
