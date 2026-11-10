// studio_looks.cpp -- the Looks workspace and a unit's Look view: looks.json (the looks a unit's
// images are drawn with past their own colours: the palette entries a rule takes put onto a ramp,
// as a crystal or as gas, with a light on the ground and a plume; engine/docs/modules/render.md,
// "Looks") and hud.json (the console's art and layout, the minimap's dots, the selection's marks;
// docs/mod-data.md). Both are presentation only. Every change is an edit of the "looks" or "hud"
// document, so undo takes it back, and a look changed is sent to the canvas game at once (the
// channel's look) so it shows there.
//
// Commands of its own:
//   studio_look_set {name, field?, value?, merge?, remove?}   a look's field set (plume.height,
//                                       light.color), taken away without value; the look made when
//                                       it is not in looks.json; the whole look taken out with remove
//   studio_look_unit {unit, look}       the look a unit's type is drawn with, "" for none
//   studio_hud_set {path, value?, merge?}   a member of hud.json by a dotted path (minimap.dots.own),
//                                       taken away without value
//   studio_looks_show {look | page}     the Looks workspace shown at a look, or at the console's page
//                                       (console, minimap, selection)
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../design/design_engine.h"  // DesignCall
#include "../design/design_hooks.h"   // DesignInGame
#include "../port/tools/port_control.h"
#include "studio_draw.h"
#include "studio_marks.h"
#include "studio_readout.h"
#include "studio_media.h"

namespace {

using namespace studio_draw;

const char *const kBuiltins[] = {"rich-gold", "rich-violet"};
const char *const kRules[] = {"blues", "greens", "all"};
const char *const kRuleWords[] = {"Crystal blues", "Vespene greens", "Every colour"};
const char *const kMaterials[] = {"crystal", "gas"};
const char *const kMaterialWords[] = {"Crystal", "Gas"};

// ---- a look's values ----------------------------------------------------------------------------------

bool IsBuiltin(const std::string &name) {
  return name == kBuiltins[0] || name == kBuiltins[1];
}

PortJson Parse(const char *psz) {
  PortJson j;
  PortJsonParse(psz, &j);
  return j;
}

// A look as the engine starts it (port_looks.cpp, Fresh and Builtins): a new look is the gold one's
// ramp and values taking every entry; the built-in ones as they are made.
const PortJson &Defaults(const std::string &name) {
  static const PortJson s_fresh = Parse(
      "{\"rule\":\"all\",\"material\":\"crystal\",\"ramp\":[\"#2c1403\",\"#6e3a06\",\"#b8740e\",\"#eaa81c\",\"#ffd64a\",\"#fff3b8\"],"
      "\"images\":[],\"units\":[],\"clarity\":0.38,\"shine\":0.55,\"glow\":0.5,\"sparkle\":0.55,"
      "\"plume\":{\"height\":54,\"density\":0.992,\"rise\":20,\"replaces\":[],\"vents\":[]},"
      "\"light\":{\"color\":\"#ffd64a\",\"radius\":40,\"strength\":0.75,\"x\":0,\"y\":8}}");
  static const PortJson s_gold = Parse(
      "{\"rule\":\"blues\",\"material\":\"crystal\",\"ramp\":[\"#2c1403\",\"#6e3a06\",\"#b8740e\",\"#eaa81c\",\"#ffd64a\",\"#fff3b8\"],"
      "\"images\":[347,349,351,397,398],\"units\":[],\"clarity\":0.38,\"shine\":0.55,\"glow\":0.5,\"sparkle\":0.55,"
      "\"plume\":{\"height\":54,\"density\":0.992,\"rise\":20,\"replaces\":[],\"vents\":[]},"
      "\"light\":{\"color\":\"#ffd64a\",\"radius\":40,\"strength\":0.75,\"x\":0,\"y\":8}}");
  static const PortJson s_violet = Parse(
      "{\"rule\":\"greens\",\"material\":\"gas\",\"ramp\":[\"#1c0628\",\"#4c1166\",\"#8c2eb4\",\"#c35ee8\",\"#eba6ff\",\"#fff0ff\"],"
      "\"images\":[93,158,307,344,399,400,401,402,403,404,430,431,432,433,434,435,436,437,438,439],\"units\":[],"
      "\"clarity\":0.38,\"shine\":0.55,\"glow\":0.5,\"sparkle\":0.55,"
      "\"plume\":{\"height\":54,\"density\":0.992,\"rise\":20,\"replaces\":[430,431,432,433,434,435,436,437,438,439],\"vents\":\"builtin\"},"
      "\"light\":{\"color\":\"#c35ee8\",\"radius\":74,\"strength\":1.25,\"x\":6,\"y\":-4}}");
  return name == kBuiltins[0] ? s_gold : name == kBuiltins[1] ? s_violet : s_fresh;
}

// The design engine's fields as it starts them (port_looks.cpp's Zone and Look, design_looks.cpp's
// kAuraFirst): an aura when it is turned on, and a zone past the first.
const char kAuraFirst[] =
    "{\"reach\":2,\"width\":1,\"strength\":1,\"crackle\":0.5,\"speed\":1,\"pulse\":0.3,\"sparks\":0.3,\"gap\":0,"
    "\"falloff\":1,\"inner\":0.3,\"opacity\":1,\"pulseRate\":1,\"flicker\":0,\"embers\":0,\"bands\":0,\"bandSpeed\":1,"
    "\"steps\":4,\"crackleSize\":1,\"block\":1,\"color\":\"#40ff60\",\"mid\":\"#9fffa0\",\"core\":\"#eaffea\","
    "\"mode\":\"fixed\",\"blend\":\"add\",\"images\":\"all\",\"dither\":true,\"overUnits\":false}";
const char kZoneFirst[] =
    "{\"rule\":\"none\",\"material\":\"paint\",\"ramp\":[\"#2c1403\",\"#6e3a06\",\"#b8740e\",\"#eaa81c\",\"#ffd64a\",\"#fff3b8\"],"
    "\"mix\":1,\"fit\":\"spread\",\"hue\":{\"from\":0,\"to\":360,\"chroma\":0.02,\"lightMin\":0,\"lightMax\":1}}";

// The looks in looks.json, an array (empty when the mod has none).
PortJson FileLooks(void) {
  const PortJson &a = StudioDoc("looks")["looks"];
  if (a.kind == PortJson::ARRAY)
    return a;
  PortJson out;
  out.kind = PortJson::ARRAY;
  return out;
}

const PortJson *FileLook(const PortJson &looks, const std::string &name) {
  for (const PortJson &l : looks.items)
    if (l["name"].Str() == name)
      return &l;
  return nullptr;
}

// A value over its default: an object's members each over the default's, anything else in place.
PortJson Over(const PortJson &base, const PortJson &top) {
  if (top.kind != PortJson::OBJECT || base.kind != PortJson::OBJECT)
    return top.kind == PortJson::NONE ? base : top;
  PortJson out = base;
  for (const PortJsonMember &m : top.members) {
    PortJson *p = StudioJsonMember(out, m.first);
    StudioJsonPut(out, m.first, p ? Over(*p, m.second) : m.second);
  }
  return out;
}

// A look as the game will draw it: its defaults with what looks.json gives over them.
PortJson Effective(const std::string &name) {
  const PortJson looks = FileLooks();
  const PortJson *p = FileLook(looks, name);
  PortJson out = Over(Defaults(name), p ? *p : PortJson());
  StudioJsonPut(out, "name", StudioJsonString(name), 0);
  // the design engine's aura and zones past the first over their starting values; the first zone
  // is the look's own colours, so what it leaves out stays the look's
  if (out["aura"].kind == PortJson::OBJECT)
    StudioJsonPut(out, "aura", Over(Parse(kAuraFirst), out["aura"]));
  if (PortJson *pZones = StudioJsonMember(out, "zones"))
    if (pZones->kind == PortJson::ARRAY)
      for (size_t z = 1; z < pZones->items.size(); ++z)
        pZones->items[z] = Over(Parse(kZoneFirst), pZones->items[z]);
  return out;
}

// The looks to list: the built-in two, then the mod's own in its file's order.
std::vector<std::string> LookNames(void) {
  std::vector<std::string> out(std::begin(kBuiltins), std::end(kBuiltins));
  for (const PortJson &l : FileLooks().items)
    if (!IsBuiltin(l["name"].Str()) && !l["name"].Str().empty())
      out.push_back(l["name"].Str());
  return out;
}

unsigned Hex(const std::string &s) {
  if (s.size() != 7 || s[0] != '#')
    return 0;
  return static_cast<unsigned>(strtoul(s.c_str() + 1, nullptr, 16));
}

std::string HexText(unsigned rgb) {
  char sz[8];
  snprintf(sz, sizeof sz, "#%06x", rgb & 0xFFFFFF);
  return sz;
}

ImU32 Col(unsigned rgb, float a = 1.0f) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, static_cast<int>(a * 255.0f));
}

// ---- the edits ------------------------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::vector<std::string> Split(const std::string &path) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= path.size()) {
    const size_t dot = path.find('.', at);
    out.push_back(path.substr(at, dot == std::string::npos ? std::string::npos : dot - at));
    if (dot == std::string::npos)
      break;
    at = dot + 1;
  }
  return out;
}

// A member set at a path of objects (made as needed), or taken away when value is NONE.
void SetAt(PortJson &obj, const std::vector<std::string> &path, size_t i, const PortJson &value) {
  if (obj.kind != PortJson::OBJECT)
    obj = StudioJsonObject();
  if (i + 1 == path.size()) {
    if (value.kind == PortJson::NONE)
      StudioJsonErase(obj, path[i]);
    else
      StudioJsonPut(obj, path[i], value);
    return;
  }
  PortJson *p = StudioJsonMember(obj, path[i]);
  PortJson child = p ? *p : StudioJsonObject();
  SetAt(child, path, i + 1, value);
  if (child.kind == PortJson::OBJECT && child.members.empty())
    StudioJsonErase(obj, path[i]);
  else
    StudioJsonPut(obj, path[i], child);
}

// The canvas game shown a look as the file now has it.
void ShowLive(const std::string &name) {
  if (!DesignInGame() || name.empty())
    return;
  const PortJson l = Effective(name);
  std::string fields;
  for (const PortJsonMember &m : l.members) {
    if (m.second.kind == PortJson::STRING && m.second.s == "builtin")
      continue;
    PortJson v = m.second;
    if (m.first == "plume" && v["vents"].kind == PortJson::STRING)
      StudioJsonErase(v, "vents");  // the built-in's vents stay as they are
    fields += (fields.empty() ? "" : ",") + Q(m.first) + ":" + PortJsonWrite(v);
  }
  // what the file leaves out goes as off, so an aura, circle or zone taken out leaves the canvas too
  if (l["aura"].kind == PortJson::NONE)
    fields += ",\"aura\":{\"reach\":0}";
  if (l["circle"].kind == PortJson::NONE)
    fields += ",\"circle\":false";
  if (l["zones"].kind == PortJson::NONE)
    fields += ",\"zones\":[{}]";
  DesignCall("look", fields);
}

bool PutLooks(const PortJson &looks, const std::string &sender, const std::string &label, const std::string &merge, std::string *pWhy) {
  StudioGroupBegin(sender, label, merge);
  const bool bOk = StudioPut("looks", {"looks"}, looks, sender, label, pWhy);
  StudioGroupEnd();
  return bOk;
}

bool CmdLookSet(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const std::string name = j["name"].Str();
  if (name.empty() || name.find('"') != std::string::npos) {
    *pAnswer = "a look needs a name";
    return false;
  }
  PortJson looks = FileLooks();
  int at = -1;
  for (size_t i = 0; i < looks.items.size(); ++i)
    if (looks.items[i]["name"].Str() == name)
      at = static_cast<int>(i);
  std::string label;
  if (j["remove"].kind == PortJson::BOOL && j["remove"].b) {
    if (at < 0) {
      *pAnswer = "looks.json has no look " + name;
      return false;
    }
    looks.items.erase(looks.items.begin() + at);
    label = IsBuiltin(name) ? name + " back to the built-in look" : "look " + name + " taken out";
  } else {
    if (at < 0) {
      PortJson l = StudioJsonObject();
      StudioJsonPut(l, "name", StudioJsonString(name));
      looks.items.push_back(l);
      at = static_cast<int>(looks.items.size()) - 1;
    }
    const std::string field = j["field"].Str();
    if (!field.empty()) {
      if (field == "name") {
        *pAnswer = "a look keeps its name";
        return false;
      }
      SetAt(looks.items[static_cast<size_t>(at)], Split(field), 0, j["value"]);
      label = "look " + name + " " + field + (j["value"].kind == PortJson::NONE ? " back to its default" : " set");
    } else if (j["value"].kind == PortJson::OBJECT) {
      PortJson l = j["value"];
      StudioJsonPut(l, "name", StudioJsonString(name), 0);
      looks.items[static_cast<size_t>(at)] = l;
      label = "look " + name + " set";
    } else {
      label = "look " + name + " made";
    }
  }
  std::string why;
  if (!PutLooks(looks, Sender(j), label, j["merge"].Str(), &why)) {
    *pAnswer = why;
    return false;
  }
  ShowLive(name);
  *pAnswer = "\"look\":" + PortJsonWrite(Effective(name));
  return true;
}

// A unit's row: its record's, which looks.json names it by.
int UnitRow(const std::string &unit) {
  StudioRecord r;
  return StudioRecordOf(ST_UNITS, unit, &r) ? r.nId : -1;
}

// A row a look's units or images give: a number, or a record's name, which the game finds the row of
// as each game begins; -1 for a name no record has.
int ItemRow(int nTable, const PortJson &v) {
  if (v.kind == PortJson::STRING) {
    StudioRecord r;
    return StudioRecordOf(nTable, v.s, &r) ? r.nId : -1;
  }
  return static_cast<int>(v.Num(-1));
}

// A row as the studio writes it: its record's name, which stays when a record sorts before it; its
// number when no record names it.
PortJson ItemOf(int nTable, int nRow) {
  const std::string name = StudioRefName(nTable, nRow);
  return name == std::to_string(nRow) ? StudioJsonNumber(nRow) : StudioJsonString(name);
}

// The look whose units have a row, "" for none (the file's first, as the game takes it).
std::string LookOfRow(int nRow) {
  for (const PortJson &l : FileLooks().items)
    for (const PortJson &u : l["units"].items)
      if (ItemRow(ST_UNITS, u) == nRow)
        return l["name"].Str();
  return std::string();
}

bool CmdLookUnit(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str(), look = j["look"].Str();
  const int nRow = UnitRow(unit);
  if (nRow < 0) {
    *pAnswer = "no unit " + unit;
    return false;
  }
  PortJson looks = FileLooks();
  for (PortJson &l : looks.items) {
    PortJson *pUnits = StudioJsonMember(l, "units");
    if (pUnits == nullptr || pUnits->kind != PortJson::ARRAY)
      continue;
    pUnits->items.erase(std::remove_if(pUnits->items.begin(), pUnits->items.end(),
                                       [&](const PortJson &u) { return ItemRow(ST_UNITS, u) == nRow; }),
                        pUnits->items.end());
  }
  if (!look.empty()) {
    PortJson *pLook = nullptr;
    for (PortJson &l : looks.items)
      if (l["name"].Str() == look)
        pLook = &l;
    if (pLook == nullptr) {
      if (!IsBuiltin(look)) {
        *pAnswer = "no look " + look + " in looks.json: rich-gold, rich-violet or one the mod makes";
        return false;
      }
      PortJson l = StudioJsonObject();
      StudioJsonPut(l, "name", StudioJsonString(look));
      looks.items.push_back(l);
      pLook = &looks.items.back();
    }
    PortJson *pUnits = StudioJsonMember(*pLook, "units");
    PortJson units;
    units.kind = PortJson::ARRAY;
    if (pUnits && pUnits->kind == PortJson::ARRAY)
      units = *pUnits;
    units.items.push_back(ItemOf(ST_UNITS, nRow));
    std::stable_sort(units.items.begin(), units.items.end(),
                     [](const PortJson &a, const PortJson &b) { return ItemRow(ST_UNITS, a) < ItemRow(ST_UNITS, b); });
    StudioJsonPut(*pLook, "units", units);
  }
  std::string why;
  if (!PutLooks(looks, Sender(j), unit + (look.empty() ? " drawn with no look" : " drawn with the look " + look), std::string(), &why)) {
    *pAnswer = why;
    return false;
  }
  if (!look.empty())
    ShowLive(look);
  *pAnswer = "\"unit\":" + Q(unit) + ",\"row\":" + std::to_string(nRow) + ",\"look\":" + Q(look);
  return true;
}

bool CmdHudSet(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const std::vector<std::string> path = Split(j["path"].Str());
  for (const std::string &p : path)
    if (p.empty()) {
      *pAnswer = "path is the member's names with dots between, as minimap.dots.own";
      return false;
    }
  std::string why;
  const std::string label = "hud.json " + j["path"].Str() + (j["value"].kind == PortJson::NONE ? " back to the game's" : " set");
  StudioGroupBegin(Sender(j), label, j["merge"].Str());
  bool bOk = true;
  // a section left empty is taken away with its last member, so the file says only what it changes
  if (j["value"].kind == PortJson::NONE) {
    PortJson top = StudioDoc("hud")[path[0].c_str()];
    if (path.size() > 1 && top.kind == PortJson::OBJECT) {
      SetAt(top, path, 1, PortJson());
      bOk = top.members.empty() ? StudioPut("hud", {path[0]}, PortJson(), Sender(j), label, &why)
                                : StudioPut("hud", {path[0]}, top, Sender(j), label, &why);
    } else {
      bOk = StudioPut("hud", path, PortJson(), Sender(j), label, &why);
    }
  } else {
    bOk = StudioPut("hud", path, j["value"], Sender(j), label, &why);
  }
  StudioGroupEnd();
  if (!bOk) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"hud\":" + PortJsonWrite(StudioDoc("hud"));
  return true;
}

void LookSet(const std::string &look, const std::string &field, const std::string &valueJson, bool bMerge = false) {
  StudioAsk("studio_look_set", "\"name\":" + Q(look) + ",\"field\":" + Q(field) +
                                   (valueJson.empty() ? std::string() : ",\"value\":" + valueJson) +
                                   (bMerge ? ",\"merge\":" + Q("look." + look + "." + field) : std::string()));
}

void HudSet(const std::string &path, const std::string &valueJson) {
  StudioAsk("studio_hud_set", "\"path\":" + Q(path) + (valueJson.empty() ? std::string() : ",\"value\":" + valueJson));
}

// ---- the pieces --------------------------------------------------------------------------------------

// A value being dragged keeps the number the modder holds while its edits go through, a few frames
// behind; it is read from the file again once it rests.
struct Held {
  std::string id;
  float af[3] = {0, 0, 0};
  int nFrame = -100;
};
Held g_held;

bool Holding(const std::string &id) {
  return g_held.id == id && ImGui::GetFrameCount() - g_held.nFrame < 20;
}

// A slider for a number field of a look, its default shown when the look leaves it out.
void Slider(const std::string &look, const PortJson &eff, const std::string &field, const char *pszLabel, float fMin, float fMax,
            const char *pszFmt, float fW) {
  const std::vector<std::string> path = Split(field);
  const PortJson *p = &eff;
  for (const std::string &k : path)
    p = &(*p)[k.c_str()];
  const std::string id = look + "#" + field;
  float f = Holding(id) ? g_held.af[0] : static_cast<float>(p->Num(0));
  const ImVec2 o = ImGui::GetCursorScreenPos();
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(110.0f), o.y));
  ImGui::PushID(id.c_str());
  ImGui::SetNextItemWidth(fW - P(110.0f));
  StudioPushFont(STUDIO_FONT_MONO, kStudioFsXs);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, C(TK_WELL));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, C(TK_SEL_LINE));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, C(TK_HI));
  if (ImGui::SliderFloat("##s", &f, fMin, fMax, pszFmt)) {
    g_held.id = id;
    g_held.af[0] = f;
    g_held.nFrame = ImGui::GetFrameCount();
    LookSet(look, field, Fmt(f, 3), true);
  }
  ImGui::PopStyleColor(3);
  ImGui::PopFont();
  ImGui::PopID();
}

// A colour field of a look: a swatch that opens the picker.
void Swatch(const std::string &look, const std::string &field, const std::string &now, float fSize, int nRampSlot = -1,
            const PortJson *pRamp = nullptr) {
  const std::string id = look + "#" + field + "#" + std::to_string(nRampSlot);
  float af[3];
  if (Holding(id)) {
    memcpy(af, g_held.af, sizeof af);
  } else {
    const unsigned rgb = Hex(now);
    af[0] = ((rgb >> 16) & 0xFF) / 255.0f;
    af[1] = ((rgb >> 8) & 0xFF) / 255.0f;
    af[2] = (rgb & 0xFF) / 255.0f;
  }
  ImGui::PushID(id.c_str());
  if (ImGui::ColorEdit3("##c", af, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayHex)) {
    g_held.id = id;
    memcpy(g_held.af, af, sizeof af);
    g_held.nFrame = ImGui::GetFrameCount();
    const unsigned rgb = static_cast<unsigned>(af[0] * 255 + 0.5f) << 16 | static_cast<unsigned>(af[1] * 255 + 0.5f) << 8 |
                         static_cast<unsigned>(af[2] * 255 + 0.5f);
    if (nRampSlot >= 0 && pRamp) {
      std::string arr = "[";
      for (int k = 0; k < 6; ++k)
        arr += (k ? "," : "") + Q(k == nRampSlot ? HexText(rgb) : pRamp->At(static_cast<size_t>(k)).Str("#000000"));
      LookSet(look, "ramp", arr + "]", true);
    } else {
      LookSet(look, field, Q(HexText(rgb)), true);
    }
  }
  (void)fSize;
  ImGui::PopID();
}

// Chips of a list of rows (images or units), each taken out with its x, more added from a pick.
void RowList(const std::string &look, const PortJson &eff, const char *pszField, int nTable, bool bMine, float fW,
             const char *pszEmpty) {
  const std::vector<std::string> path = Split(pszField);
  const PortJson *p = &eff;
  for (const std::string &k : path)
    p = &(*p)[k.c_str()];
  // each item's row, and the item as the file has it, so one whose name no record has is kept
  std::vector<int> rows;
  std::vector<PortJson> items;
  for (const PortJson &v : p->items) {
    rows.push_back(ItemRow(nTable, v));
    items.push_back(v);
  }
  const ImVec2 o = ImGui::GetCursorScreenPos();
  float x = o.x, y = o.y;
  const float right = o.x + fW;
  int nOut = -1;
  ImGui::PushID(pszField);
  if (rows.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), pszEmpty);
  for (size_t i = 0; i < rows.size(); ++i) {
    const bool bLost = rows[i] < 0;
    const std::string name = !bLost ? StudioRefName(nTable, rows[i])
                             : items[i].kind == PortJson::STRING ? items[i].s
                                                                 : std::to_string(static_cast<long long>(items[i].Num()));
    const std::string label = bLost ? name + "  no such record"
                                    : StudioShowName(nTable, name) + (name == std::to_string(rows[i]) ? std::string() : "  " + std::to_string(rows[i]));
    const float w = StudioButtonSize(label.c_str(), nullptr, 22.0f, kStudioFsXs).x + (bMine ? P(22.0f) : 0.0f);
    if (x + w > right && x > o.x) {
      x = o.x;
      y += P(28.0f);
    }
    ImGui::PushID(static_cast<int>(i));
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (StudioButton("##r", label.c_str(), nullptr, SB_PLAIN, 22.0f, 0.0f, kStudioFsXs) && StudioClassicRow(nTable, name) >= 0)
      StudioOpenDoc(nTable, name, 0);
    if (bMine) {
      ImGui::SetCursorScreenPos(ImVec2(x + w - P(22.0f), y));
      if (StudioButton("##x", "", "x", SB_FLAT, 22.0f, 20.0f))
        nOut = static_cast<int>(i);
    }
    ImGui::PopID();
    x += w + P(6.0f);
  }
  if (bMine) {
    if (x + P(170.0f) > right && x > o.x) {
      x = o.x;
      y += P(28.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, rows.empty() ? y + P(22.0f) : y));
    std::string picked;
    if (StudioRecordPick("##add", nTable, std::string(), 160.0f, &picked, false, 22.0f, nTable == ST_UNITS ? "+ a unit" : "+ an image")) {
      StudioRecord r;
      if (StudioRecordOf(nTable, picked, &r) && std::find(rows.begin(), rows.end(), r.nId) == rows.end()) {
        rows.push_back(r.nId);
        items.push_back(ItemOf(nTable, r.nId));
        nOut = -2;
      }
    }
    if (rows.empty())
      y += P(22.0f);
  }
  ImGui::PopID();
  if (nOut != -1) {
    if (nOut >= 0) {
      rows.erase(rows.begin() + nOut);
      items.erase(items.begin() + nOut);
    }
    // written by name in row order: a number written before is named now, a name no record has kept
    std::vector<size_t> order(rows.size());
    for (size_t i = 0; i < order.size(); ++i)
      order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rows[a] < rows[b]; });
    PortJson arr;
    arr.kind = PortJson::ARRAY;
    for (size_t i : order)
      arr.items.push_back(rows[i] < 0 ? items[i] : ItemOf(nTable, rows[i]));
    LookSet(look, pszField, PortJsonWrite(arr));
  }
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(fW, y - o.y + P(28.0f)));
}

// A row's label at the left, the cursor left after it where the row's control goes.
void RowLabel(const char *psz, float x) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, x, p.y + P(5.0f), C(TK_INK2), psz);
  ImGui::SetCursorScreenPos(ImVec2(x + P(110.0f), p.y));
}

// The ramp drawn as the tones it gives, dark to light.
void RampBar(ImDrawList *d, ImVec2 a, ImVec2 b, const PortJson &ramp) {
  const float w = (b.x - a.x) / 5.0f;
  for (int k = 0; k < 5; ++k) {
    const ImU32 c0 = Col(Hex(ramp.At(static_cast<size_t>(k)).Str())), c1 = Col(Hex(ramp.At(static_cast<size_t>(k + 1)).Str()));
    d->AddRectFilledMultiColor(ImVec2(a.x + w * k, a.y), ImVec2(a.x + w * (k + 1), b.y), c0, c1, c1, c0);
  }
  d->AddRect(a, b, C(TK_LINE), P(2.0f));
}

// A picture of what the look does: a crystal's faces in the ramp's tones over the ground, its light
// under it and, for gas, a plume rising.
void LookPicture(ImDrawList *d, ImVec2 a, ImVec2 b, const PortJson &eff) {
  d->AddRectFilled(a, b, IM_COL32(28, 30, 22, 255), P(4.0f));
  d->PushClipRect(a, b, true);
  const ImVec2 c((a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f + P(18.0f));
  const PortJson &light = eff["light"];
  const float r = static_cast<float>(light["radius"].Num(40)) * P(1.4f);
  const float s = std::min(1.0f, static_cast<float>(light["strength"].Num(0.75)) * 0.5f);
  const ImVec2 lc(c.x + static_cast<float>(light["x"].Num(0)) * P(1.4f), c.y + static_cast<float>(light["y"].Num(0)) * P(1.4f));
  for (int i = 8; i >= 1; --i)
    d->AddEllipseFilled(lc, ImVec2(r * i / 8.0f, r * i / 16.0f), Col(Hex(light["color"].Str("#ffd64a")), s * 0.16f), 0.0f, 40);
  const PortJson &ramp = eff["ramp"];
  if (eff["material"].Str() == "gas") {
    // a vent's column, the ramp's middle to its light as it rises
    const float h = static_cast<float>(eff["plume"]["height"].Num(54)) * P(1.4f);
    for (int i = 0; i < 24; ++i) {
      const float t = i / 23.0f;
      const ImU32 col = Col(Hex(ramp.At(static_cast<size_t>(2 + std::min(3, static_cast<int>(t * 4)))).Str()), 0.35f * (1.0f - t));
      d->AddCircleFilled(ImVec2(c.x + sinf(t * 5.0f) * P(6.0f) * t, c.y - P(10.0f) - h * t), P(6.0f) + t * P(14.0f), col, 20);
    }
    d->AddEllipseFilled(ImVec2(c.x, c.y), ImVec2(P(46.0f), P(16.0f)), Col(Hex(ramp.At(1).Str())), 0.0f, 32);
    d->AddEllipseFilled(ImVec2(c.x, c.y - P(3.0f)), ImVec2(P(22.0f), P(7.0f)), Col(Hex(ramp.At(4).Str())), 0.0f, 32);
  } else {
    // three crystals, each face a tone from dark to light, lit from the top left as the art is
    const float kx[] = {-30.0f, 0.0f, 28.0f}, kh[] = {46.0f, 70.0f, 40.0f};
    for (int n = 0; n < 3; ++n) {
      const float x = c.x + P(kx[n]), h = P(kh[n]), w = P(16.0f);
      const ImVec2 top(x, c.y - h), l(x - w, c.y - h * 0.7f), rr(x + w, c.y - h * 0.7f), bl(x - w, c.y), br(x + w, c.y), bm(x, c.y + P(6.0f));
      d->AddQuadFilled(top, l, bl, bm, Col(Hex(ramp.At(4).Str())));
      d->AddQuadFilled(top, bm, br, rr, Col(Hex(ramp.At(2).Str())));
      d->AddTriangleFilled(top, ImVec2(x - w * 0.4f, c.y - h * 0.62f), ImVec2(x - w * 0.15f, c.y - h * 0.2f), Col(Hex(ramp.At(5).Str())));
      d->AddLine(top, bm, Col(Hex(ramp.At(1).Str())), P(1.0f));
      const float g = static_cast<float>(eff["glow"].Num(0.5));
      d->AddCircleFilled(ImVec2(x, c.y - h * 0.45f), w * 0.9f, Col(Hex(ramp.At(4).Str()), 0.12f * g), 20);
    }
  }
  d->PopClipRect();
  d->AddRect(a, b, C(TK_LINE), P(4.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(8.0f), b.y - P(18.0f), C(TK_INK3), "a sketch: Try it shows it in the game");
}

// ---- the workspace ------------------------------------------------------------------------------------------

enum { SEL_LOOK, SEL_CONSOLE, SEL_MINIMAP, SEL_SELECTION, SEL_READOUT, SEL_MARK };
int g_nSel = SEL_LOOK;
std::string g_look = kBuiltins[0];
std::string g_mark;  // the mark look shown, SEL_MARK (studio_marks.cpp)
char g_szNew[48];
char g_szNewMark[48];

void NavRow(const char *pszId, const std::string &label, const std::string &note, bool bSel, int nSel, const std::string &look, float fW,
            int nChip = -1) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const float h = P(30.0f);
  if (ImGui::InvisibleButton(pszId, ImVec2(fW, h))) {
    g_nSel = nSel;
    if (!look.empty())
      g_look = look;
  }
  const bool bHover = ImGui::IsItemHovered();
  if (bSel)
    d->AddRectFilled(o, ImVec2(o.x + fW, o.y + h), C(TK_SEL));
  else if (bHover)
    d->AddRectFilled(o, ImVec2(o.x + fW, o.y + h), C(TK_ROW));
  float x = o.x + P(12.0f);
  if (!look.empty()) {
    RampBar(d, ImVec2(x, o.y + P(10.0f)), ImVec2(x + P(26.0f), o.y + P(20.0f)), Effective(look)["ramp"]);
    x += P(34.0f);
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, Mid(o.y, h, kStudioFsS), bSel ? C(TK_HI) : C(TK_INK), label, fW - (x - o.x) - P(80.0f));
  if (nChip >= 0) {
    const float cw = StudioChipW(note.c_str());
    StudioChip(d, ImVec2(o.x + fW - cw - P(8.0f), o.y + P(6.0f)), note.c_str(), nChip);
  } else if (!note.empty()) {
    const float nw = TW(STUDIO_FONT_TEXT, kStudioFsXs, note);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, o.x + fW - nw - P(10.0f), Mid(o.y, h, kStudioFsXs), C(TK_INK3), note);
  }
}

void Nav(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = b.x - a.x;
  d->AddRectFilled(a, b, C(TK_PANE2));
  d->AddLine(ImVec2(b.x - 0.5f, a.y), ImVec2(b.x - 0.5f, b.y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + P(10.0f)));
  if (!ImGui::BeginChild("##looksnav", ImVec2(W - 1, b.y - a.y - P(10.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::EndChild();
    return;
  }
  const ImVec2 o = ImGui::GetCursorScreenPos();
  Wide(d, 9.5f, o.x + P(12.0f), o.y, C(TK_INK3), "Looks", 0.1f);
  ImGui::Dummy(ImVec2(1, P(20.0f)));
  const PortJson looks = FileLooks();
  for (const std::string &name : LookNames()) {
    const bool bBuilt = IsBuiltin(name), bInFile = FileLook(looks, name) != nullptr;
    const std::string note = bBuilt ? (bInFile ? "changed" : "built in") : "the mod's";
    NavRow(("##l" + name).c_str(), name, note, g_nSel == SEL_LOOK && g_look == name, SEL_LOOK, name, W - 1,
           bBuilt && bInFile ? CH_CHANGED : bBuilt ? CH_PLAIN : CH_GOOD);
  }
  // a new look by its name
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(12.0f), ImGui::GetCursorScreenPos().y + P(8.0f)));
  StudioInput("##newlook", g_szNew, sizeof g_szNew, (W / StudioPx(1.0f)) - 110.0f, 26.0f, "new look's name");
  const bool bTyped = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
  ImGui::SameLine(0.0f, P(6.0f));
  std::string newName = g_szNew;
  for (char &c : newName)
    c = c == ' ' ? '-' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
  bool bTaken = false;
  for (const std::string &n : LookNames())
    bTaken = bTaken || n == newName;
  if ((StudioButton("##make", "Make", "plus", SB_GOOD, 26.0f, 0.0f, kStudioFsXs, StudioIsOpen() && !newName.empty() && !bTaken) || bTyped) &&
      !newName.empty() && !bTaken) {
    StudioAsk("studio_look_set", "\"name\":" + Q(newName));
    g_look = newName;
    g_nSel = SEL_LOOK;
    g_szNew[0] = '\0';
  }
  ImGui::Dummy(ImVec2(1, P(16.0f)));
  const ImVec2 h = ImGui::GetCursorScreenPos();
  Wide(d, 9.5f, h.x + P(12.0f), h.y, C(TK_INK3), "The console", 0.1f);
  ImGui::Dummy(ImVec2(1, P(20.0f)));
  const PortJson &hud = StudioDoc("hud");
  NavRow("##console", "Console art and layout", hud["console"].kind == PortJson::OBJECT ? "the mod's" : "the game's",
         g_nSel == SEL_CONSOLE, SEL_CONSOLE, std::string(), W - 1);
  NavRow("##minimap", "Minimap's dots", hud["minimap"].kind == PortJson::OBJECT ? "the mod's" : "the game's", g_nSel == SEL_MINIMAP,
         SEL_MINIMAP, std::string(), W - 1);
  NavRow("##selection", "Selection's marks", hud["selection"].kind == PortJson::OBJECT ? "the mod's" : "the game's",
         g_nSel == SEL_SELECTION, SEL_SELECTION, std::string(), W - 1);
  // the one unit's readout, readout.json rather than hud.json: the panel's own parts
  NavRow("##readout", "Unit readout",
         StudioDoc("readout")["readout"].kind == PortJson::OBJECT ? "the mod's" : "classic's", g_nSel == SEL_READOUT,
         SEL_READOUT, std::string(), W - 1);
  // the looks the mod's marks on the ground wear, which types.json names under "marks"
  ImGui::Dummy(ImVec2(1, P(16.0f)));
  const ImVec2 mh = ImGui::GetCursorScreenPos();
  Wide(d, 9.5f, mh.x + P(12.0f), mh.y, C(TK_INK3), "Marks on the map", 0.1f);
  ImGui::Dummy(ImVec2(1, P(20.0f)));
  const std::vector<std::string> marks = StudioMarkNames();
  for (const std::string &name : marks)
    NavRow(("##m" + name).c_str(), name, StudioMarkNote(name), g_nSel == SEL_MARK && g_mark == name, SEL_MARK,
           std::string(), W - 1, CH_GOOD);
  if (marks.empty())
    NavRow("##mnone", "What a mark look is", "none yet", g_nSel == SEL_MARK && g_mark.empty(), SEL_MARK,
           std::string(), W - 1);
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(12.0f), ImGui::GetCursorScreenPos().y + P(8.0f)));
  StudioInput("##newmark", g_szNewMark, sizeof g_szNewMark, (W / StudioPx(1.0f)) - 110.0f, 26.0f, "new look's name");
  const bool bMarkTyped = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
  ImGui::SameLine(0.0f, P(6.0f));
  std::string newMark = g_szNewMark;
  for (char &c : newMark)
    c = c == ' ' ? '-' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
  bool bMarkTaken = false;
  for (const std::string &n : marks)
    bMarkTaken = bMarkTaken || n == newMark;
  if ((StudioButton("##makemark", "Make", "plus", SB_GOOD, 26.0f, 0.0f, kStudioFsXs,
                    StudioIsOpen() && !newMark.empty() && !bMarkTaken) ||
       bMarkTyped) &&
      !newMark.empty() && !bMarkTaken) {
    StudioAsk("studio_mark_set", "\"name\":" + Q(newMark) + ",\"sender\":\"you\"");
    g_mark = newMark;
    g_nSel = SEL_MARK;
    g_szNewMark[0] = '\0';
  }
  ImGui::Dummy(ImVec2(1, P(16.0f)));
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(12.0f), ImGui::GetCursorScreenPos().y));
  StudioMediaPara("All of it is presentation only: a replay, a save and the game's rules never see it.", W - P(30.0f), C(TK_INK3), kStudioFsXs);
  ImGui::EndChild();
}

// The units whose type a look is given, as words.
std::string RowWords(const PortJson &rows, int nTable, size_t nMost) {
  std::string s;
  for (size_t i = 0; i < rows.items.size() && i < nMost; ++i)
    s += (i ? ", " : "") + StudioShowName(nTable, StudioRefName(nTable, ItemRow(nTable, rows.items[i])));
  if (rows.items.size() > nMost)
    s += " and " + std::to_string(rows.items.size() - nMost) + " more";
  return s;
}

// ---- the design engine's fields: zones, the aura, the circle ------------------------------------------

const char *const kZoneRules[] = {"none", "blues", "greens", "all", "reds", "golds", "greys", "skins", "hue", "entries"};
const char *const kZoneRuleWords[] = {"Nothing",  "Crystal blues", "Vespene greens", "Every colour", "Reds",
                                      "Golds",    "Greys",         "Skins",          "A band of hue", "Entries picked"};
const char *const kZoneMats[] = {"crystal", "gas", "paint", "glow"};
const char *const kZoneMatWords[] = {"Crystal", "Gas", "Paint", "Glow"};
int g_nZoneShown = 1;

// A colour from a swatch, held while it is dragged; true with *pRgb when it changed.
bool ColourPick(const std::string &id, const std::string &now, unsigned *pRgb) {
  float af[3];
  if (Holding(id)) {
    memcpy(af, g_held.af, sizeof af);
  } else {
    const unsigned rgb = Hex(now);
    af[0] = ((rgb >> 16) & 0xFF) / 255.0f;
    af[1] = ((rgb >> 8) & 0xFF) / 255.0f;
    af[2] = (rgb & 0xFF) / 255.0f;
  }
  ImGui::PushID(id.c_str());
  const bool b = ImGui::ColorEdit3("##c", af, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayHex);
  ImGui::PopID();
  if (!b)
    return false;
  g_held.id = id;
  memcpy(g_held.af, af, sizeof af);
  g_held.nFrame = ImGui::GetFrameCount();
  *pRgb = static_cast<unsigned>(af[0] * 255 + 0.5f) << 16 | static_cast<unsigned>(af[1] * 255 + 0.5f) << 8 |
          static_cast<unsigned>(af[2] * 255 + 0.5f);
  return true;
}

// A number held while it is dragged, as Slider draws it; true with *pf when it changed.
bool HeldSlider(const std::string &id, const char *pszLabel, float fNow, float fMin, float fMax, const char *pszFmt, float fW, float *pf) {
  float f = Holding(id) ? g_held.af[0] : fNow;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), pszLabel);
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(110.0f), o.y));
  ImGui::PushID(id.c_str());
  ImGui::SetNextItemWidth(fW - P(110.0f));
  StudioPushFont(STUDIO_FONT_MONO, kStudioFsXs);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, C(TK_WELL));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, C(TK_SEL_LINE));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, C(TK_HI));
  const bool b = ImGui::SliderFloat("##s", &f, fMin, fMax, pszFmt);
  ImGui::PopStyleColor(3);
  ImGui::PopFont();
  ImGui::PopID();
  if (!b)
    return false;
  g_held.id = id;
  g_held.af[0] = f;
  g_held.nFrame = ImGui::GetFrameCount();
  *pf = f;
  return true;
}

int IndexOf(const char *const *ppsz, int n, const std::string &s, int nDefault) {
  for (int i = 0; i < n; ++i)
    if (s == ppsz[i])
      return i;
  return nDefault;
}

// A zone's member set (taken out with NONE), the zones written whole.
void ZonePut(const std::string &look, const PortJson &eff, int z, const char *pszKey, const PortJson &v, bool bMerge = false) {
  PortJson zones = eff["zones"];
  if (zones.kind != PortJson::ARRAY || z >= static_cast<int>(zones.items.size()))
    return;
  PortJson &zone = zones.items[static_cast<size_t>(z)];
  if (v.kind == PortJson::NONE)
    StudioJsonErase(zone, pszKey);
  else
    StudioJsonPut(zone, pszKey, v);
  LookSet(look, "zones", PortJsonWrite(zones), bMerge);
}

void SubHeading(const char *psz) {
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  StudioLabel(STUDIO_FONT_BOLD, kStudioFsS, C(TK_INK), psz);
  ImGui::Dummy(ImVec2(1, P(2.0f)));
}

// The zones past the first: each a rule of its own onto a ramp of its own, as the design engine's
// Looks panel has them. The first zone is the look's own colours above, so the file's first is
// left as it is ({} when the studio made the list).
void ZonesPart(const std::string &name, const PortJson &eff, float W, bool bMine) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  StudioMediaHeading("Zones", "up to three more sets of the unit's colours, each onto a ramp of its own", W);
  const PortJson &zones = eff["zones"];
  const int n = zones.kind == PortJson::ARRAY ? static_cast<int>(zones.items.size()) : 1;
  const float x0 = ImGui::GetCursorScreenPos().x;
  if (n > 1) {
    static const char *const kWords[] = {"Zone 2", "Zone 3", "Zone 4"};
    if (g_nZoneShown < 1 || g_nZoneShown >= n)
      g_nZoneShown = 1;
    g_nZoneShown = 1 + StudioSeg("##zones", kWords, n - 1, g_nZoneShown - 1);
  } else {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "none: the look's colours above are its one zone");
  }
  if (bMine && n < 4) {
    ImGui::SameLine(0.0f, P(10.0f));
    if (StudioButton("##addzone", "Add a zone", "plus", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs)) {
      PortJson arr;
      arr.kind = PortJson::ARRAY;
      if (zones.kind == PortJson::ARRAY)
        arr = zones;
      else
        arr.items.push_back(StudioJsonObject());
      arr.items.push_back(Parse(kZoneFirst));
      g_nZoneShown = static_cast<int>(arr.items.size()) - 1;
      LookSet(name, "zones", PortJsonWrite(arr));
    }
  }
  if (bMine && n > 1) {
    ImGui::SameLine(0.0f, P(8.0f));
    if (StudioButton("##outzone", "Take this zone out", "x", SB_FLAT, 26.0f, 0.0f, kStudioFsXs)) {
      PortJson arr = zones;
      arr.items.erase(arr.items.begin() + g_nZoneShown);
      const bool bNone = arr.items.size() == 1 && arr.items[0].kind == PortJson::OBJECT && arr.items[0].members.empty();
      LookSet(name, "zones", bNone ? std::string() : PortJsonWrite(arr));
      g_nZoneShown = 1;
      ImGui::Dummy(ImVec2(1, P(6.0f)));
      return;
    }
  }
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  if (n <= 1)
    return;
  const int z = g_nZoneShown;
  const PortJson &zone = zones.items[static_cast<size_t>(z)];
  const std::string id = name + "#zone" + std::to_string(z);
  const float sw = (std::min)(W, P(520.0f));
  ImGui::PushID(id.c_str());
  RowLabel("Takes", x0);
  const int nRule = IndexOf(kZoneRules, 10, zone["rule"].Str("none"), 0);
  if (StudioSelectBegin("##zrule", kZoneRuleWords[nRule], 200.0f)) {
    for (int i = 0; i < 10; ++i)
      if (StudioItem(kZoneRuleWords[i], i == nRule))
        ZonePut(name, eff, z, "rule", StudioJsonString(kZoneRules[i]));
    StudioSelectEnd();
  }
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  RowLabel("Drawn as", x0);
  const int nMat = IndexOf(kZoneMats, 4, zone["material"].Str("paint"), 2);
  const int nNewMat = StudioSeg("##zmat", kZoneMatWords, 4, nMat);
  if (nNewMat != nMat)
    ZonePut(name, eff, z, "material", StudioJsonString(kZoneMats[nNewMat]));
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  RowLabel("Ramp", x0);
  const ImVec2 ra = ImGui::GetCursorScreenPos();
  const float rampW = (std::min)(W - P(110.0f), P(420.0f));
  const PortJson &ramp = zone["ramp"];
  for (int k = 0; k < 6; ++k) {
    ImGui::SetCursorScreenPos(ImVec2(ra.x + (rampW - P(24.0f)) * k / 5.0f, ra.y));
    unsigned rgb = 0;
    if (ColourPick(id + "#r" + std::to_string(k), ramp.At(static_cast<size_t>(k)).Str("#000000"), &rgb) && bMine) {
      PortJson arr;
      arr.kind = PortJson::ARRAY;
      for (int i = 0; i < 6; ++i)
        arr.items.push_back(StudioJsonString(i == k ? HexText(rgb) : ramp.At(static_cast<size_t>(i)).Str("#000000")));
      ZonePut(name, eff, z, "ramp", arr, true);
    }
  }
  RampBar(d, ImVec2(ra.x, ra.y + P(30.0f)), ImVec2(ra.x + rampW, ra.y + P(40.0f)), ramp);
  ImGui::SetCursorScreenPos(ImVec2(x0, ra.y + P(50.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  float f = 0.0f;
  if (HeldSlider(id + "#mix", "Mix", static_cast<float>(zone["mix"].Num(1)), 0.0f, 1.0f, "%.2f", sw, &f) && bMine)
    ZonePut(name, eff, z, "mix", StudioJsonNumber(f), true);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How much of the zone's colour shows over the image's own");
  RowLabel("Fit", x0);
  static const char *const kFits[] = {"spread", "keep"};
  static const char *const kFitWords[] = {"Spread over the ramp", "Each entry's own lightness"};
  const int nFit = IndexOf(kFits, 2, zone["fit"].Str("spread"), 0);
  const int nNewFit = StudioSeg("##zfit", kFitWords, 2, nFit);
  if (nNewFit != nFit)
    ZonePut(name, eff, z, "fit", StudioJsonString(kFits[nNewFit]));
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  if (nRule == 8) {
    // a band of hue: from and to in degrees, the least chroma, the lightness between
    static const char *const kHue[] = {"from", "to", "chroma", "lightMin", "lightMax"};
    static const char *const kHueWords[] = {"Hue from", "Hue to", "Least chroma", "Lightness from", "Lightness to"};
    static const float kMost[] = {360.0f, 360.0f, 0.4f, 1.0f, 1.0f};
    static const char *const kFmt[] = {"%.0f", "%.0f", "%.3f", "%.2f", "%.2f"};
    for (int k = 0; k < 5; ++k)
      if (HeldSlider(id + "#hue" + std::to_string(k), kHueWords[k], static_cast<float>(zone["hue"][kHue[k]].Num(0)), 0.0f, kMost[k], kFmt[k], sw,
                     &f) &&
          bMine) {
        PortJson hue = zone["hue"].kind == PortJson::OBJECT ? zone["hue"] : StudioJsonObject();
        StudioJsonPut(hue, kHue[k], StudioJsonNumber(f));
        ZonePut(name, eff, z, "hue", hue, true);
      }
  }
  if (nRule == 9) {
    // the entries picked, on the game's palette
    const std::vector<unsigned> &pal = StudioPalette();
    std::set<int> on;
    for (const PortJson &e : zone["entries"].items)
      on.insert(static_cast<int>(e.Num(-1)));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "Click an entry of the game's palette to take it or let it go.");
    if (pal.size() == 256) {
      const float s = P(14.0f);
      const ImVec2 g = ImGui::GetCursorScreenPos();
      int nToggled = -1;
      for (int e = 0; e < 256; ++e) {
        const ImVec2 a(g.x + (e % 32) * (s + 1), g.y + (e / 32) * (s + 1));
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(e);
        if (ImGui::InvisibleButton("##e", ImVec2(s, s)) && bMine)
          nToggled = e;
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%d  %s%s", e, HexText(pal[static_cast<size_t>(e)]).c_str(), on.count(e) ? "  taken" : "");
        ImGui::PopID();
        d->AddRectFilled(a, ImVec2(a.x + s, a.y + s), Col(pal[static_cast<size_t>(e)]));
        if (on.count(e))
          d->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(a.x + s + 1, a.y + s + 1), C(TK_HI), 0.0f, 0, P(2.0f));
      }
      ImGui::SetCursorScreenPos(ImVec2(g.x, g.y + 8 * (s + 1)));
      ImGui::Dummy(ImVec2(1, P(6.0f)));
      if (nToggled >= 0) {
        if (!on.erase(nToggled))
          on.insert(nToggled);
        PortJson arr;
        arr.kind = PortJson::ARRAY;
        for (int e : on)
          arr.items.push_back(StudioJsonNumber(e));
        ZonePut(name, eff, z, "entries", arr);
      }
    }
  }
  ImGui::PopID();
  ImGui::Dummy(ImVec2(1, P(10.0f)));
}

// The aura: a charge round the unit's outline (design_looks.cpp's Aura, its fields and ranges).
void AuraPart(const std::string &name, const PortJson &eff, float W, bool bMine) {
  StudioMediaHeading("Aura", "a charge round the unit's outline, drawn past its colours", W);
  const PortJson &a = eff["aura"];
  const bool bOn = a.kind == PortJson::OBJECT;
  bool b = bOn;
  if (StudioCheck("##auraon", "An aura round the unit", &b) && bMine)
    LookSet(name, "aura", b ? std::string(kAuraFirst) : std::string());
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  if (!bOn)
    return;
  const float sw = (std::min)(W, P(520.0f));
  const float x0 = ImGui::GetCursorScreenPos().x;
  SubHeading("Shape");
  Slider(name, eff, "aura.reach", "Reach", 0.0f, 8.0f, "%.0f px", sw);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How far past the outline the glow reaches, in game pixels; 0 is no aura");
  Slider(name, eff, "aura.width", "Line width", 0.5f, 3.0f, "%.2f", sw);
  Slider(name, eff, "aura.gap", "Gap", 0.0f, 3.0f, "%.0f px", sw);
  Slider(name, eff, "aura.falloff", "Falloff", 0.5f, 3.0f, "%.2f", sw);
  Slider(name, eff, "aura.inner", "Inner edge", 0.0f, 1.0f, "%.2f", sw);
  SubHeading("Colour");
  RowLabel("Colours by", x0);
  static const char *const kModes[] = {"fixed", "ramp", "team", "cycle"};
  static const char *const kModeWords[] = {"Glow and line", "A ramp outward", "The player's", "Hue turning"};
  const int nMode = IndexOf(kModes, 4, a["mode"].Str("fixed"), 0);
  const int nNewMode = StudioSeg("##amode", kModeWords, 4, nMode);
  if (nNewMode != nMode && bMine)
    LookSet(name, "aura.mode", Q(kModes[nNewMode]));
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  {
    static const char *const kKeys[] = {"color", "mid", "core"};
    static const char *const kWords[] = {"Glow", "Middle", "Line"};
    const ImVec2 p = ImGui::GetCursorScreenPos();
    for (int k = 0; k < 3; ++k) {
      const float x = p.x + P(110.0f) + k * P(110.0f);
      ImGui::SetCursorScreenPos(ImVec2(x, p.y));
      Swatch(name, std::string("aura.") + kKeys[k], a[kKeys[k]].Str("#ffffff"), 24.0f);
      T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, x + P(32.0f), p.y + P(5.0f), C(TK_INK2), kWords[k]);
    }
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(3.0f), C(TK_INK2), "Colours");
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(32.0f)));
    ImGui::Dummy(ImVec2(1, 1));
  }
  Slider(name, eff, "aura.strength", "Strength", 0.0f, 2.0f, "%.2f", sw);
  Slider(name, eff, "aura.opacity", "Opacity", 0.0f, 1.0f, "%.2f", sw);
  RowLabel("Over what is under", x0);
  static const char *const kBlends[] = {"add", "over"};
  static const char *const kBlendWords[] = {"Adds light", "Laid over"};
  const int nBlend = IndexOf(kBlends, 2, a["blend"].Str("add"), 0);
  const int nNewBlend = StudioSeg("##ablend", kBlendWords, 2, nBlend);
  if (nNewBlend != nBlend && bMine)
    LookSet(name, "aura.blend", Q(kBlends[nNewBlend]));
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  SubHeading("Motion");
  Slider(name, eff, "aura.speed", "Speed", 0.0f, 4.0f, "%.2f", sw);
  Slider(name, eff, "aura.pulse", "Pulse", 0.0f, 1.0f, "%.2f", sw);
  Slider(name, eff, "aura.pulseRate", "Pulse rate", 0.1f, 4.0f, "%.2f", sw);
  Slider(name, eff, "aura.flicker", "Flicker", 0.0f, 1.0f, "%.2f", sw);
  Slider(name, eff, "aura.crackle", "Crackle", 0.0f, 1.0f, "%.2f", sw);
  Slider(name, eff, "aura.crackleSize", "Crackle size", 0.25f, 4.0f, "%.2f", sw);
  Slider(name, eff, "aura.sparks", "Sparks", 0.0f, 1.0f, "%.2f", sw);
  Slider(name, eff, "aura.embers", "Embers", 0.0f, 1.0f, "%.2f", sw);
  Slider(name, eff, "aura.bands", "Bands", 0.0f, 8.0f, "%.0f", sw);
  Slider(name, eff, "aura.bandSpeed", "Band speed", -4.0f, 4.0f, "%.2f", sw);
  SubHeading("Pixels");
  Slider(name, eff, "aura.steps", "Steps", 2.0f, 8.0f, "%.0f", sw);
  Slider(name, eff, "aura.block", "Block", 1.0f, 2.0f, "%.0f px", sw);
  bool bDither = a["dither"].kind == PortJson::BOOL ? a["dither"].b : true;
  if (StudioCheck("##adither", "Dithered between its steps", &bDither) && bMine)
    LookSet(name, "aura.dither", bDither ? "true" : "false");
  SubHeading("Where");
  RowLabel("Images", x0);
  static const char *const kImages[] = {"all", "main"};
  static const char *const kImageWords[] = {"All of the unit's", "Its main image only"};
  const int nImages = IndexOf(kImages, 2, a["images"].Str("all"), 0);
  const int nNewImages = StudioSeg("##aimages", kImageWords, 2, nImages);
  if (nNewImages != nImages && bMine)
    LookSet(name, "aura.images", Q(kImages[nNewImages]));
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  bool bOver = a["overUnits"].kind == PortJson::BOOL && a["overUnits"].b;
  if (StudioCheck("##aover", "Its glow over other units", &bOver) && bMine)
    LookSet(name, "aura.overUnits", bOver ? "true" : "false");
  ImGui::Dummy(ImVec2(1, P(10.0f)));
}

// The design engine's fields of a look, after the game's.
void DesignFields(const std::string &name, const PortJson &eff, float W, bool bMine) {
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  StudioMediaPara("Zones, the aura and the selection circle are the design engine's: the studio's game and HyperionDesign.exe draw them, "
                  "and Hyperion.exe passes them over.",
                  W, C(TK_INK3), kStudioFsXs);
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  ZonesPart(name, eff, W, bMine);
  AuraPart(name, eff, W, bMine);
  StudioMediaHeading("Selection circle", "the mark under a selected unit", W);
  bool bCircle = eff["circle"].kind == PortJson::BOOL && eff["circle"].b;
  if (StudioCheck("##circle", "Drawn with the look too", &bCircle) && bMine)
    LookSet(name, "circle", bCircle ? "true" : "false");
  ImGui::Dummy(ImVec2(1, P(10.0f)));
}

void LookPage(const std::string &name, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson looks = FileLooks();
  const PortJson *pFile = FileLook(looks, name);
  const PortJson eff = Effective(name);
  const bool bMine = StudioIsOpen();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, o.y, C(TK_HI), name);
  float cx = o.x;
  const float cy = o.y + P(36.0f);
  if (IsBuiltin(name))
    cx += StudioChip(d, ImVec2(cx, cy), pFile ? "built in, changed by the mod" : "built in", pFile ? CH_CHANGED : CH_GOLD) + P(6.0f);
  else
    cx += StudioChip(d, ImVec2(cx, cy), "the mod's own", CH_GOOD) + P(6.0f);
  if (pFile && ((*pFile)["zones"].kind != PortJson::NONE || (*pFile)["aura"].kind != PortJson::NONE))
    StudioChip(d, ImVec2(cx, cy), "with the design engine's zones or aura", CH_PLAIN);
  // the buttons at the right of the title
  const float bw = StudioButtonSize("Try it in the game", "play", 28.0f, 13.0f).x;
  const float rw = StudioButtonSize(IsBuiltin(name) ? "Back to the built-in" : "Take it out", "x", 28.0f, 13.0f).x;
  ImGui::SetCursorScreenPos(ImVec2(o.x + W - bw - rw - P(8.0f), o.y));
  if (StudioButton("##try", "Try it in the game", "play", SB_GOOD, 28.0f, 0.0f, 13.0f, DesignInGame())) {
    ShowLive(name);
    // a classic unit it is given, put in the canvas to see it on
    for (const PortJson &u : eff["units"].items) {
      const int n = ItemRow(ST_UNITS, u);
      if (n >= 0 && n < StudioClassicRows(ST_UNITS)) {
        DesignCall("spawn", "\"unit\":" + std::to_string(n) + ",\"x\":1070,\"y\":1024,\"player\":0,\"count\":1");
        DesignCall("camera", "\"x\":1070,\"y\":1024");
        break;
      }
    }
    StudioSay("the look " + name + " is in the canvas game until it ends");
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Sends the look to the canvas game, and puts one of the classic units it is given there");
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##out", IsBuiltin(name) ? "Back to the built-in" : "Take it out", "x", SB_PLAIN, 28.0f, 0.0f, 13.0f, bMine && pFile != nullptr)) {
    StudioAsk("studio_look_set", "\"name\":" + Q(name) + ",\"remove\":true");
    if (!IsBuiltin(name))
      g_look = kBuiltins[0];
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, cy + P(32.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  // the picture at the right, the colours at the left
  const float pw = P(300.0f), ph = P(220.0f);
  const bool bSide = W > P(860.0f);
  const float lw = bSide ? W - pw - P(24.0f) : W;
  const float top = ImGui::GetCursorScreenPos().y;
  if (bSide)
    LookPicture(d, ImVec2(o.x + W - pw, top), ImVec2(o.x + W, top + ph), eff);
  ImGui::SetCursorScreenPos(ImVec2(o.x, top));
  ImGui::BeginGroup();
  StudioMediaHeading("Colours", "the palette's entries the rule takes, put onto the ramp by their tone", lw);
  RowLabel("Takes", o.x);
  int nRule = 2;
  for (int i = 0; i < 3; ++i)
    if (eff["rule"].Str() == kRules[i])
      nRule = i;
  const int nNewRule = StudioSeg("##rule", kRuleWords, 3, nRule);
  if (nNewRule != nRule)
    LookSet(name, "rule", Q(kRules[nNewRule]));
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  RowLabel("Drawn as", o.x);
  int nMat = eff["material"].Str() == "gas" ? 1 : 0;
  const int nNewMat = StudioSeg("##mat", kMaterialWords, 2, nMat);
  if (nNewMat != nMat)
    LookSet(name, "material", Q(kMaterials[nNewMat]));
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  // the ramp: six swatches over the bar they make
  RowLabel("Ramp", o.x);
  const ImVec2 ra = ImGui::GetCursorScreenPos();
  const float rampW = std::min(lw - P(110.0f), P(420.0f));
  const PortJson &ramp = eff["ramp"];
  for (int k = 0; k < 6; ++k) {
    ImGui::SetCursorScreenPos(ImVec2(ra.x + (rampW - P(24.0f)) * k / 5.0f, ra.y));
    Swatch(name, "ramp", ramp.At(static_cast<size_t>(k)).Str("#000000"), 24.0f, k, &ramp);
  }
  RampBar(d, ImVec2(ra.x, ra.y + P(30.0f)), ImVec2(ra.x + rampW, ra.y + P(40.0f)), ramp);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, ra.x, ra.y + P(44.0f), C(TK_INK3), "dark");
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, ra.x + rampW - TW(STUDIO_FONT_TEXT, kStudioFsXs, "light"), ra.y + P(44.0f), C(TK_INK3), "light");
  ImGui::SetCursorScreenPos(ImVec2(o.x, ra.y + P(66.0f)));
  ImGui::Dummy(ImVec2(1, 1));
  StudioMediaHeading("In full colour", "how the GPU draws it past the remap: each 0 to 1", lw);
  Slider(name, eff, "clarity", "Clarity", 0.0f, 1.0f, "%.2f", std::min(lw, P(520.0f)));
  Slider(name, eff, "shine", "Shine", 0.0f, 1.0f, "%.2f", std::min(lw, P(520.0f)));
  Slider(name, eff, "glow", "Glow", 0.0f, 1.0f, "%.2f", std::min(lw, P(520.0f)));
  Slider(name, eff, "sparkle", "Sparkle", 0.0f, 1.0f, "%.2f", std::min(lw, P(520.0f)));
  ImGui::EndGroup();
  if (!bSide) {
    ImGui::Dummy(ImVec2(1, P(8.0f)));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    LookPicture(d, p, ImVec2(p.x + std::min(W, pw), p.y + ph), eff);
    ImGui::Dummy(ImVec2(1, ph));
  }
  ImGui::Dummy(ImVec2(1, P(14.0f)));
  // what it covers
  StudioMediaHeading("Given to", "the unit types drawn with it, by their names", W);
  RowList(name, eff, "units", ST_UNITS, bMine, W, "no unit type: a unit has it when the game or a tool gives it");
  StudioMediaHeading("Images", "the images of a unit it draws; with none, every image of its sprite", W);
  RowList(name, eff, "images", ST_IMAGES, bMine, W, "every image of its sprite");
  // the light on the ground
  StudioMediaHeading("Light", "on the ground under the unit, before the sprites are drawn", W);
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Colour");
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(110.0f), p.y));
    Swatch(name, "light.color", eff["light"]["color"].Str("#ffd64a"), 24.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(30.0f)));
    ImGui::Dummy(ImVec2(1, 1));
  }
  const float sw = std::min(W, P(520.0f));
  Slider(name, eff, "light.radius", "Radius", 1.0f, 256.0f, "%.0f px", sw);
  Slider(name, eff, "light.strength", "Strength", 0.0f, 4.0f, "%.2f", sw);
  Slider(name, eff, "light.x", "Across", -64.0f, 64.0f, "%.0f px", sw);
  Slider(name, eff, "light.y", "Down", -64.0f, 64.0f, "%.0f px", sw);
  ImGui::Dummy(ImVec2(1, P(14.0f)));
  // the plume
  StudioMediaHeading("Plume", "a column rising from each vent, in place of the images it replaces", W);
  Slider(name, eff, "plume.height", "Height", 0.0f, 256.0f, "%.0f px", sw);
  Slider(name, eff, "plume.density", "Density", 0.0f, 4.0f, "%.2f", sw);
  Slider(name, eff, "plume.rise", "Rise", 0.0f, 200.0f, "%.0f", sw);
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Replaces");
  RowList(name, eff, "plume.replaces", ST_IMAGES, bMine, W, "none");
  const PortJson &vents = eff["plume"]["vents"];
  if (vents.kind == PortJson::STRING) {
    StudioMediaPara("Vents: the built-in look's nineteen, at the geyser's craters and the buildings on one, for each tileset's frame.", W,
                    C(TK_INK2), kStudioFsS);
  } else {
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), vents.items.empty() ? "Vents: none, so no plume rises" : "Vents");
    const char *const kVentKeys[] = {"image", "x", "y", "strength", "width", "frame"};
    const char *const kVentWords[] = {"image", "x", "y", "strength", "width", "frame"};
    int nOut = -1;
    PortJson newVents = vents;
    bool bChanged = false;
    for (size_t i = 0; i < vents.items.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      for (int k = 0; k < 6; ++k) {
        if (k)
          ImGui::SameLine(0.0f, P(6.0f));
        double dv = vents.items[i][kVentKeys[k]].Num(k == 5 ? -1.0 : k == 3 ? 1.0 : k == 4 ? 3.0 : 0.0);
        ImGui::PushID(k);
        if (StudioNumber("##v", &dv, k == 0 ? 70.0f : 60.0f, 24.0f, bMine, 12.0f)) {
          PortJson &v = newVents.items[i];
          if (k == 5 && dv < 0)
            StudioJsonErase(v, "frame");
          else
            StudioJsonPut(v, kVentKeys[k], StudioJsonNumber(dv));
          bChanged = true;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s%s", kVentWords[k], k == 5 ? ": the tileset's frame it is for, -1 for every frame" : "");
        ImGui::PopID();
      }
      ImGui::SameLine(0.0f, P(6.0f));
      if (bMine && StudioButton("##x", "", "x", SB_FLAT, 24.0f, 22.0f))
        nOut = static_cast<int>(i);
      ImGui::PopID();
    }
    if (nOut >= 0) {
      newVents.items.erase(newVents.items.begin() + nOut);
      bChanged = true;
    }
    if (bMine && StudioButton("##vent", "Add a vent", "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs)) {
      PortJson v = StudioJsonObject();
      // a vent names its image by number
      const int nImage = eff["images"].items.empty() ? 0 : (std::max)(0, ItemRow(ST_IMAGES, eff["images"].items[0]));
      StudioJsonPut(v, "image", StudioJsonNumber(nImage));
      StudioJsonPut(v, "x", StudioJsonNumber(0));
      StudioJsonPut(v, "y", StudioJsonNumber(0));
      StudioJsonPut(v, "strength", StudioJsonNumber(1));
      StudioJsonPut(v, "width", StudioJsonNumber(3));
      newVents.kind = PortJson::ARRAY;
      newVents.items.push_back(v);
      bChanged = true;
    }
    if (bChanged)
      LookSet(name, "plume.vents", PortJsonWrite(newVents));
  }
  DesignFields(name, eff, W, bMine);
  ImGui::Dummy(ImVec2(1, P(20.0f)));
}

// The console as a wide screen shows it: its three parts at the edges or whole in the middle, the map
// to the screen's foot or to the console's top.
void ConsoleSketch(ImDrawList *d, ImVec2 a, float w, bool bEdges, bool bFull) {
  const float h = w * 9.0f / 16.0f;
  const ImVec2 b(a.x + w, a.y + h);
  d->AddRectFilled(a, b, IM_COL32(52, 74, 40, 255), P(3.0f));
  const float cw = h * 4.0f / 3.0f, cx = a.x + (w - cw) / 2.0f;
  const float ch = h * 0.24f;
  const ImU32 metal = C(TK_BTN_BOT), rim = C(TK_LINE2);
  if (!bFull)
    d->AddRectFilled(ImVec2(a.x, b.y - ch * 0.62f), b, IM_COL32(0, 0, 0, 255));
  auto part = [&](float x0, float x1, float top) {
    d->AddRectFilled(ImVec2(x0, top), ImVec2(x1, b.y), metal, P(2.0f));
    d->AddRect(ImVec2(x0, top), ImVec2(x1, b.y), rim, P(2.0f));
  };
  if (bEdges) {
    part(a.x, a.x + cw * 0.28f, b.y - ch);
    part(a.x + (w - cw * 0.4f) / 2.0f, a.x + (w + cw * 0.4f) / 2.0f, b.y - ch * 0.62f);
    part(b.x - cw * 0.3f, b.x, b.y - ch);
  } else {
    part(cx, cx + cw * 0.28f, b.y - ch);
    part(cx + cw * 0.3f, cx + cw * 0.7f, b.y - ch * 0.62f);
    part(cx + cw * 0.72f, cx + cw, b.y - ch);
  }
  d->AddRect(a, b, C(TK_LINE), P(3.0f));
}

// A PCX of the console's: 640 by 480 at 8 bits a pixel, in one plane.
std::string PcxWords(const std::string &rel) {
  if (rel.empty())
    return std::string();
  HANDLE h = CreateFileA((StudioDataFolder() + rel).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return "the data folder does not have it: the classic console shows";
  unsigned char hd[128] = {};
  DWORD n = 0;
  ReadFile(h, hd, sizeof hd, &n, nullptr);
  CloseHandle(h);
  if (n < 128 || hd[0] != 0x0A)
    return "it is no PCX file";
  const int w = (hd[8] | hd[9] << 8) - (hd[4] | hd[5] << 8) + 1, ht = (hd[10] | hd[11] << 8) - (hd[6] | hd[7] << 8) + 1;
  if (hd[3] != 8 || hd[65] != 1)
    return "it is not 8 bits a pixel in one plane, as the console's is";
  if (w != 640 || ht != 480)
    return "it is " + std::to_string(w) + " by " + std::to_string(ht) + ": a console is 640 by 480";
  return "640 by 480, 8 bits a pixel: its pixels at 0 are where the map shows";
}

int g_nConsoleShown = 1;  // the race whose console image the page shows: zerg, terran, protoss, replay

// A race's console image as the game draws it, the mod's or the classic one, over a map's colour so
// the clear pixels (0) show where the map is.
void ConsoleImage(const PortJson &c, const char *const *ppRaces, const char *const *ppWords, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  g_nConsoleShown = StudioSeg("##consoleshow", ppWords, 4, g_nConsoleShown);
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  const std::string own = c["art"][ppRaces[g_nConsoleShown]].Str();
  const bool bClassic = own.empty() || !StudioFileExists(own);
  const std::string where = bClassic ? std::string("game\\") + "ztpn"[g_nConsoleShown] + "console.pcx" : own;
  // what it is, above it
  StudioVideoState st;
  ImVec2 o = ImGui::GetCursorScreenPos();
  const float bw = std::min(W, P(640.0f)), bh = bw * 3.0f / 4.0f;
  const ImVec2 top = o;
  o.y += P(bClassic && !own.empty() ? 50.0f : 28.0f);
  d->AddRectFilled(o, ImVec2(o.x + bw, o.y + bh), IM_COL32(52, 74, 40, 255), P(3.0f));
  if (!StudioPcxDraw(where, bClassic, o, ImVec2(o.x + bw, o.y + bh), &st))
    T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + P(20.0f), o.y + P(20.0f), C(TK_WARN), st.why, bw - P(40.0f));
  d->AddRect(o, ImVec2(o.x + bw, o.y + bh), C(TK_LINE2), P(3.0f));
  const std::string size = st.w > 0 ? ", " + std::to_string(st.w) + " by " + std::to_string(st.h) + "; the green shows where the map is" : std::string();
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, top.x, top.y, C(TK_INK3), (st.from.empty() ? where : st.from) + size, W);
  if (!own.empty() && bClassic)
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, top.x, top.y + P(22.0f), C(TK_WARN), own + " is not in the data folder: the classic console shows", W);
  ImGui::SetCursorScreenPos(top);
  ImGui::Dummy(ImVec2(1, o.y - top.y + bh));
}

void ConsolePage(float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson &c = StudioDoc("hud")["console"];
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, o.y, C(TK_HI), "The console");
  ImGui::Dummy(ImVec2(1, P(40.0f)));
  StudioMediaPara("hud.json's console: an image of the mod's own for a race, in place of the classic game's, and how a wide view lays "
                  "the console out. Read again as each game's console loads.",
                  W, C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  StudioMediaHeading("Art", "a PCX of 640 by 480 at 8 bits a pixel, in the game's palette", W);
  static const char *const kRaces[] = {"zerg", "terran", "protoss", "replay"};
  static const char *const kRaceWords[] = {"Zerg", "Terran", "Protoss", "Replays"};
  for (int i = 0; i < 4; ++i) {
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK), kRaceWords[i]);
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(110.0f), p.y));
    const std::string now = c["art"][kRaces[i]].Str();
    std::string chosen;
    if (StudioFileField("##art", now, "pcx", "art", 320.0f, &chosen))
      HudSet(std::string("console.art.") + kRaces[i], chosen.empty() ? std::string() : Q(chosen));
    const std::string words = now.empty() ? std::string("the classic console") : PcxWords(now);
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(110.0f), p.y + P(30.0f)));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, words.compare(0, 3, "640") == 0 || now.empty() ? C(TK_INK3) : C(TK_WARN), words.c_str());
    ImGui::Dummy(ImVec2(1, P(6.0f)));
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  ConsoleImage(c, kRaces, kRaceWords, W);
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  StudioMediaHeading("Layout", "on a view wider than the classic 640", W);
  const bool bEdges = c["toEdges"].kind == PortJson::BOOL ? c["toEdges"].b : true;
  const bool bFull = c["fullMap"].kind == PortJson::BOOL ? c["fullMap"].b : false;
  const float top = ImGui::GetCursorScreenPos().y;
  auto choice = [&](const char *pszId, const char *pszLabel, const char *const *ppWords, bool bNow, const char *pszKey, bool bSet) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), pszLabel);
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(110.0f), p.y));
    const int n = StudioSeg(pszId, ppWords, 2, bNow ? 0 : 1);
    if (n != (bNow ? 0 : 1))
      HudSet(std::string("console.") + pszKey, n == 0 ? "true" : "false");
    ImGui::SameLine(0.0f, P(8.0f));
    if (bSet) {
      if (StudioButton((std::string(pszId) + "r").c_str(), "the game's", "undo", SB_FLAT, 26.0f, 0.0f, kStudioFsXs))
        HudSet(std::string("console.") + pszKey, std::string());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Take the mod's choice out of hud.json");
    } else {
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "the game's");
    }
    ImGui::Dummy(ImVec2(1, P(4.0f)));
  };
  static const char *const kEdges[] = {"Parts to the edges", "Whole in the middle"};
  static const char *const kFull[] = {"Map to the foot", "Map to 400 rows"};
  choice("##edges", "HD console", kEdges, bEdges, "toEdges", c["toEdges"].kind == PortJson::BOOL);
  choice("##full", "Map", kFull, bFull, "fullMap", c["fullMap"].kind == PortJson::BOOL);
  const float sw = std::min(P(360.0f), W - P(520.0f));
  if (sw > P(160.0f))
    ConsoleSketch(d, ImVec2(o.x + W - sw, top), sw, bEdges, bFull);
  ImGui::Dummy(ImVec2(1, P(sw > P(160.0f) ? std::max(0.0f, sw * 9.0f / 16.0f / StudioPx(1.0f) - 70.0f) : 0.0f)));
  StudioMediaPara("The dialogs keep the places the classic dialog files give them, and draw over the image there. The map at 400 rows "
                  "shows black through a hole below them.",
                  W, C(TK_INK3), kStudioFsXs);
}

// The palette as a grid to pick an entry from; true when one is picked.
bool PalettePick(const char *pszId, int nNow, int *pPicked) {
  const std::vector<unsigned> &pal = StudioPalette();
  bool b = false;
  const std::string preview = nNow < 0 ? std::string("the game's") : std::to_string(nNow);
  if (StudioSelectBegin(pszId, preview.c_str(), 120.0f, 26.0f)) {
    if (pal.size() != 256) {
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "the palette is not in the game's files");
    } else {
      ImDrawList *d = ImGui::GetWindowDrawList();
      const float s = P(16.0f);
      const ImVec2 o = ImGui::GetCursorScreenPos();
      for (int e = 0; e < 256; ++e) {
        const ImVec2 a(o.x + (e % 16) * (s + 1), o.y + (e / 16) * (s + 1));
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(e);
        if (ImGui::InvisibleButton("##e", ImVec2(s, s))) {
          *pPicked = e;
          b = true;
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%d  %s", e, HexText(pal[static_cast<size_t>(e)]).c_str());
        ImGui::PopID();
        d->AddRectFilled(a, ImVec2(a.x + s, a.y + s), Col(pal[static_cast<size_t>(e)]));
        if (e == nNow)
          d->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(a.x + s + 1, a.y + s + 1), C(TK_HI), 0.0f, 0, P(2.0f));
      }
      ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + 16 * (s + 1)));
      ImGui::Dummy(ImVec2(16 * (s + 1), P(4.0f)));
    }
    StudioSelectEnd();
  }
  return b;
}

void MinimapPage(float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson &dots = StudioDoc("hud")["minimap"]["dots"];
  const std::vector<unsigned> &pal = StudioPalette();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, o.y, C(TK_HI), "The minimap's dots");
  ImGui::Dummy(ImVec2(1, P(40.0f)));
  StudioMediaPara("The units' dots in a colour of the mod's, a palette entry, by their owner's relation to the player. A relation left out "
                  "keeps the colour the game gives it, Settings' team and ally colours with it; in a replay the dots keep theirs.",
                  W, C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  static const char *const kRel[] = {"own", "allied", "enemy", "neutral"};
  static const char *const kRelWords[] = {"Your own", "Allies'", "Enemies'", "Neutral"};
  const unsigned kGame[] = {0x10FC18, 0xFCFC38, 0xF40404, 0xC0C0C0};  // the game's usual: green, yellow, red, grey
  const float top = ImGui::GetCursorScreenPos().y;
  for (int i = 0; i < 4; ++i) {
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const int n = dots[kRel[i]].kind == PortJson::NUMBER ? static_cast<int>(dots[kRel[i]].d) : -1;
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK), kRelWords[i]);
    const unsigned rgb = n >= 0 && pal.size() == 256 ? pal[static_cast<size_t>(n)] : kGame[i];
    d->AddRectFilled(ImVec2(p.x + P(100.0f), p.y + P(4.0f)), ImVec2(p.x + P(118.0f), p.y + P(22.0f)), Col(rgb), P(2.0f));
    d->AddRect(ImVec2(p.x + P(100.0f), p.y + P(4.0f)), ImVec2(p.x + P(118.0f), p.y + P(22.0f)), C(TK_LINE2), P(2.0f));
    ImGui::SetCursorScreenPos(ImVec2(p.x + P(128.0f), p.y));
    int picked = -1;
    if (PalettePick("##pal", n, &picked))
      HudSet(std::string("minimap.dots.") + kRel[i], std::to_string(picked));
    ImGui::SameLine(0.0f, P(8.0f));
    if (n >= 0) {
      if (StudioButton("##r", "the game's", "undo", SB_FLAT, 26.0f, 0.0f, kStudioFsXs))
        HudSet(std::string("minimap.dots.") + kRel[i], std::string());
    } else {
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "the game's colour");
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(34.0f)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::PopID();
  }
  // a minimap with dots of each, as the game draws them
  const float s = P(180.0f);
  if (W > P(560.0f)) {
    const ImVec2 a(o.x + W - s, top), b(a.x + s, a.y + s);
    d->AddRectFilled(a, b, IM_COL32(40, 52, 30, 255));
    d->AddRect(a, b, C(TK_LINE2));
    static const float kDots[][3] = {{0.2f, 0.75f, 0}, {0.26f, 0.8f, 0}, {0.3f, 0.7f, 0}, {0.7f, 0.25f, 2}, {0.75f, 0.3f, 2},
                                     {0.72f, 0.2f, 2}, {0.25f, 0.25f, 1}, {0.3f, 0.3f, 1}, {0.5f, 0.5f, 3}, {0.6f, 0.65f, 3}};
    for (const auto &k : kDots) {
      const int i = static_cast<int>(k[2]);
      const int n = dots[kRel[i]].kind == PortJson::NUMBER ? static_cast<int>(dots[kRel[i]].d) : -1;
      const unsigned rgb = n >= 0 && pal.size() == 256 ? pal[static_cast<size_t>(n)] : kGame[i];
      const ImVec2 c(a.x + s * k[0], a.y + s * k[1]);
      d->AddRectFilled(ImVec2(c.x - P(3.0f), c.y - P(3.0f)), ImVec2(c.x + P(3.0f), c.y + P(3.0f)), Col(rgb));
    }
  }
}

void SelectionPage(float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const PortJson &s = StudioDoc("hud")["selection"];
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, o.y, C(TK_HI), "The selection's marks");
  ImGui::Dummy(ImVec2(1, P(40.0f)));
  StudioMediaPara("Whether the marks are drawn. A mark left undrawn is still there for the game: what is selected, and what a command "
                  "does to it, stay the game's.",
                  W, C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(10.0f)));
  static const char *const kKeys[] = {"circles", "teamCircles", "healthBars"};
  static const char *const kWords[] = {"Circles under the units you select", "Circles under a teammate's selection",
                                       "Health bars over selected units"};
  for (int i = 0; i < 3; ++i) {
    ImGui::PushID(i);
    bool b = s[kKeys[i]].kind == PortJson::BOOL ? s[kKeys[i]].b : true;
    if (StudioCheck("##c", kWords[i], &b))
      HudSet(std::string("selection.") + kKeys[i], b ? "true" : "false");
    ImGui::SameLine(0.0f, P(12.0f));
    if (s[kKeys[i]].kind == PortJson::BOOL) {
      if (StudioButton("##r", "the game's", "undo", SB_FLAT, 26.0f, 0.0f, kStudioFsXs))
        HudSet(std::string("selection.") + kKeys[i], std::string());
    } else {
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "the game's: drawn");
    }
    ImGui::PopID();
  }
}

}  // namespace

void StudioLooksWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFs, a.x + P(20.0f), a.y + P(20.0f), C(TK_INK3), "Open a mod to see its looks and its console.");
    return;
  }
  const float nw = P(290.0f);
  Nav(ImVec2(a.x + 1, a.y + 1), ImVec2(a.x + nw, b.y - 1));
  ImGui::SetCursorScreenPos(ImVec2(a.x + nw, a.y + 1));
  if (ImGui::BeginChild("##looksbody", ImVec2(b.x - a.x - nw - 1, b.y - a.y - 2), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(o.x + P(24.0f), o.y + P(18.0f)));
    ImGui::BeginGroup();
    const float W = std::min(ImGui::GetContentRegionAvail().x - P(24.0f), P(1200.0f));
    bool bThere = false;
    for (const std::string &n : LookNames())
      bThere = bThere || n == g_look;
    if (!bThere)
      g_look = kBuiltins[0];
    switch (g_nSel) {
    case SEL_CONSOLE: ConsolePage(W); break;
    case SEL_MINIMAP: MinimapPage(W); break;
    case SEL_SELECTION: SelectionPage(W); break;
    case SEL_READOUT: StudioReadoutPage(W); break;
    case SEL_MARK: StudioMarksPage(g_mark, W); break;
    default: LookPage(g_look, W); break;
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, P(24.0f)));
  }
  ImGui::EndChild();
}

void StudioUnitLook(const std::string &unit) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = std::min(ImGui::GetContentRegionAvail().x - P(22.0f), P(1000.0f));
  StudioRecord rec;
  if (!StudioRecordOf(ST_UNITS, unit, &rec))
    return;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 18.0f, o.x, o.y, C(TK_HEAD), "Its look");
  ImGui::Dummy(ImVec2(W, P(28.0f)));
  StudioMediaPara("A look draws the unit's images past their own colours: the palette's entries its rule takes put onto a ramp, and in "
                  "full colour as a crystal or as gas, with a light on the ground. It changes how the unit looks, never what it does.",
                  W, C(TK_INK2), 13.0f);
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  const std::string now = LookOfRow(rec.nId);
  // the picture, and the look's colours beside it
  const ImVec2 pa = ImGui::GetCursorScreenPos(), pb(pa.x + P(200.0f), pa.y + P(170.0f));
  d->AddRectFilled(pa, pb, C(TK_WELL), P(3.0f));
  d->AddRect(pa, pb, C(TK_LINE), P(3.0f));
  if (!StudioUnitPicture(d, ImVec2(pa.x + 1, pa.y + 1), ImVec2(pb.x - 1, pb.y - 1), unit, 4, 0))
    StudioGlyph(d, "unit", ImVec2((pa.x + pb.x) / 2.0f - P(20.0f), (pa.y + pb.y) / 2.0f - P(20.0f)), P(40.0f), C(TK_INK3));
  if (!now.empty())
    RampBar(d, ImVec2(pa.x + P(8.0f), pb.y - P(16.0f)), ImVec2(pb.x - P(8.0f), pb.y - P(8.0f)), Effective(now)["ramp"]);
  const float x = pb.x + P(20.0f);
  T(d, STUDIO_FONT_TEXT, 15.0f, x, pa.y, C(TK_INK), now.empty() ? std::string("No look: drawn in its own colours.")
                                                               : "Drawn with the look " + now + ".");
  if (!now.empty()) {
    const PortJson eff = Effective(now);
    int nRule = 2;
    for (int i = 0; i < 3; ++i)
      if (eff["rule"].Str() == kRules[i])
        nRule = i;
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, pa.y + P(24.0f), C(TK_INK2),
      std::string(kRuleWords[nRule]) + " onto its ramp, as " + (eff["material"].Str() == "gas" ? "gas" : "a crystal") +
          (eff["images"].items.empty() ? ", every image of its sprite" : ", " + RowWords(eff["images"], ST_IMAGES, 4)),
      o.x + W - x);
  }
  const bool bMine = StudioIsOpen();
  ImGui::SetCursorScreenPos(ImVec2(x, pa.y + P(56.0f)));
  if (StudioSelectBegin("##look", now.empty() ? "no look" : now.c_str(), 220.0f, 28.0f, 13.0f)) {
    if (StudioItem("no look", now.empty()))
      StudioAsk("studio_look_unit", "\"unit\":" + Q(unit) + ",\"look\":\"\"");
    for (const std::string &n : LookNames())
      if (StudioItem(n.c_str(), n == now))
        StudioAsk("studio_look_unit", "\"unit\":" + Q(unit) + ",\"look\":" + Q(n));
    StudioSelectEnd();
  }
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##edit", now.empty() ? "Make a look" : "Edit the look", "look", SB_PLAIN, 28.0f, 0.0f, 13.0f, bMine)) {
    g_nSel = SEL_LOOK;
    if (!now.empty())
      g_look = now;
    StudioWorkspaceSet(3);
  }
  ImGui::SetCursorScreenPos(ImVec2(x, pa.y + P(96.0f)));
  StudioMediaPara("looks.json names it " + unit + ", and the game finds its row, " + std::to_string(rec.nId) + " now, as each game begins.",
                  o.x + W - x, C(TK_INK3), kStudioFsXs);
  ImGui::SetCursorScreenPos(ImVec2(pa.x, std::max(pb.y, ImGui::GetCursorScreenPos().y)));
  ImGui::Dummy(ImVec2(1, P(8.0f)));
}

namespace {

bool CmdLooksShow(const PortJson &j, std::string *pAnswer) {
  const std::string page = j["page"].Str(), look = j["look"].Str();
  if (!look.empty()) {
    g_nSel = SEL_LOOK;
    g_look = look;
  } else if (page == "console" || page == "minimap" || page == "selection") {
    g_nSel = page == "console" ? SEL_CONSOLE : page == "minimap" ? SEL_MINIMAP : SEL_SELECTION;
  } else if (page == "marks" || !j["mark"].Str().empty()) {
    g_nSel = SEL_MARK;
    if (!j["mark"].Str().empty())
      g_mark = j["mark"].Str();
  } else {
    *pAnswer = "studio_looks_show takes look, mark, or page: console, minimap, selection or marks";
    return false;
  }
  StudioWorkspaceSet(3);
  *pAnswer = "\"page\":" + Q(look.empty() ? page : "look") + ",\"look\":" + Q(g_look) + ",\"mark\":" + Q(g_mark);
  return true;
}

}  // namespace

void StudioLooksInit(void) {
  PortControlCommand("studio_looks_show", CmdLooksShow);
  PortControlCommand("studio_look_set", CmdLookSet);
  PortControlCommand("studio_look_unit", CmdLookUnit);
  PortControlCommand("studio_hud_set", CmdHudSet);
}
