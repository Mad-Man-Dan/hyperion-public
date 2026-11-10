// studio_statuses.cpp -- the Statuses workspace: a mod's statuses ("statuses" in types.json,
// docs/mod-data.md, "Statuses"), each read as plain sentences, what it changes, how it looks on a
// unit, and where it is used: the weapons that put it on what they damage, the units whose aura
// gives it, the actions whose blocks apply it or take it off.
//
//   studio_statuses {query?, show?, section?}     the statuses, each with its sentence and its uses
//   studio_status_new {name?, label?, from?, values?}   one made, from a kind or another status, one edit
//   studio_status_set {name, field + value | values | rename}   its fields set (null takes one off), one edit
//   studio_status_delete {name}                   taken away with every use of it, one edit
//   studio_weapon_status {weapon, status}         the status a weapon puts on what it damages, "" none
//   studio_unit_aura {unit, aura | remove}        a unit's passive aura
#include <algorithm>
#include <map>
#include <vector>
#include "../design/design_engine.h"     // DesignCall
#include "../port/tools/port_control.h"  // PortControlCommand
#include "../port/data/port_iscript.h"  // ImageScriptDamages117, IscriptAnimName
#include "../port/data/port_modstatus.h"  // ModStatusKindOfDef, the rule a game works a kind out by
#include "../port/hud/port_readout.h"   // the status kinds, their glyphs, hues and words
#include "studio_draw.h"
#include "studio_statuses.h"

// The image drawer of the Art area (studio_art.cpp), for the look's preview.
namespace studio_art {
bool DrawImageAt(ImDrawList *d, ImVec2 a, ImVec2 b, const std::string &image, int nSet, int nFacing, bool bFlip, int nColour,
                 float *pK, ImVec2 *pOff, int nExtentSet);
int ImageFrames(const std::string &image);
bool ImageTurns(const std::string &image);
}  // namespace studio_art

namespace {

using namespace studio_draw;

// The fields a status takes, as the loader reads them (port_modstatus.cpp).
const char *const kFields[] = {"name", "icon", "duration", "stacking", "max_stacks", "move_speed", "armor", "damage_dealt",
                               "damage_taken", "attack_cooldown", "cannot_move", "cannot_attack", "damage_per_second",
                               "life_per_second", "side", "overlay", "overlay_by_size", "start_image", "end_image",
                               "comment", "look"};
// The keys of a status's "look", which says how the status shows itself where the kind worked out
// from its fields reads wrong (port_modstatus.cpp, ReadDef). A field of the form "look.kind" is
// written into the block, so the one command serves both.
const char *const kLookFields[] = {"kind", "icon", "colour", "hue", "pattern", "show", "priority"};
const int kLookFieldCount = 7;

bool IsField(const std::string &key) {
  for (const char *psz : kFields)
    if (key == psz)
      return true;
  // a key of the block, "look.kind" and the rest
  if (key.compare(0, 5, "look.") == 0)
    for (int i = 0; i < kLookFieldCount; ++i)
      if (key.compare(5, std::string::npos, kLookFields[i]) == 0)
        return true;
  return false;
}

// The path a field is written at: a key of the look block is two members deep.
std::vector<std::string> FieldPath(const std::string &status, const std::string &key) {
  std::vector<std::string> path;
  path.push_back("statuses");
  path.push_back(status);
  if (key.compare(0, 5, "look.") == 0) {
    path.push_back("look");
    path.push_back(key.substr(5));
  } else {
    path.push_back(key);
  }
  return path;
}

// ---- the statuses -----------------------------------------------------------------------------------------

struct Status {
  std::string name;
  const PortJson *pJson;
  bool bMine;
  std::string mod;
};

std::vector<Status> List(void) {
  std::vector<Status> out;
  for (const PortJsonMember &m : StudioDoc("types")["statuses"].members)
    out.push_back({m.first, &m.second, true, StudioModId()});
  std::sort(out.begin(), out.end(), [](const Status &a, const Status &b) { return a.name < b.name; });
  for (int i = StudioDependencies() - 1; i >= 0; --i)
    for (const PortJsonMember &m : StudioDependencyTypes(i)["statuses"].members)
      if (std::none_of(out.begin(), out.end(), [&](const Status &s) { return s.name == m.first; }))
        out.push_back({m.first, &m.second, false, StudioDependencyId(i)});
  return out;
}

const PortJson &JsonOf(const std::string &name) {
  static const PortJson s_none;
  for (const Status &s : List())
    if (s.name == name)
      return *s.pJson;
  return s_none;
}

bool Exists(const std::string &name) {
  for (const Status &s : List())
    if (s.name == name)
      return true;
  return false;
}

bool MineStatus(const std::string &name) {
  return StudioDoc("types")["statuses"][name.c_str()].kind == PortJson::OBJECT;
}

std::string Cap(std::string s) {
  if (!s.empty())
    s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
  return s;
}

std::string Joined(const std::vector<std::string> &parts) {
  std::string s;
  for (size_t i = 0; i < parts.size(); ++i)
    s += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
  return s;
}

std::string Seconds(double d) {
  return Fmt(d, 2) + (fabs(d - 1.0) < 1e-6 ? " second" : " seconds");
}

// An image a field names, by its name ("" for none).
std::string ImageOf(const PortJson &v) {
  if (v.kind == PortJson::STRING)
    return v.s;
  if (v.kind == PortJson::NUMBER)
    return StudioRefName(ST_IMAGES, static_cast<long long>(v.d));
  return std::string();
}

std::string Sentence(const PortJson &s) {
  std::vector<std::string> parts;
  const double sp = s["move_speed"].Num(0), cd = s["attack_cooldown"].Num(0), dm = s["damage_dealt"].Num(0);
  const double ar = s["armor"].Num(0), dps = s["damage_per_second"].Num(0), life = s["life_per_second"].Num(0);
  if (s["cannot_move"].kind == PortJson::BOOL && s["cannot_move"].b)
    parts.push_back("holds it still");
  else if (sp < 0)
    parts.push_back("slows by " + Fmt(-sp) + " %");
  else if (sp > 0)
    parts.push_back("speeds it up by " + Fmt(sp) + " %");
  if (s["cannot_attack"].kind == PortJson::BOOL && s["cannot_attack"].b)
    parts.push_back("stops it attacking");
  else if (cd > 0)
    parts.push_back("makes it attack " + Fmt(cd) + " % slower");
  else if (cd < 0)
    parts.push_back("makes it attack " + Fmt(-cd) + " % faster");
  if (dm > 0)
    parts.push_back("makes its attacks deal " + Fmt(dm) + " % more");
  else if (dm < 0)
    parts.push_back("makes its attacks deal " + Fmt(-dm) + " % less");
  const double dt = s["damage_taken"].Num(0);
  if (dt > 0)
    parts.push_back("makes it take " + Fmt(dt) + " % more damage");
  else if (dt < 0)
    parts.push_back("makes it take " + Fmt(-dt) + " % less damage");
  if (ar > 0)
    parts.push_back("gives it " + Fmt(ar) + " armour");
  else if (ar < 0)
    parts.push_back("takes " + Fmt(-ar) + " armour from it");
  if (dps > 0)
    parts.push_back("deals " + Fmt(dps) + " damage a second");
  if (life > 0)
    parts.push_back("heals " + Fmt(life) + " life a second");
  else if (life < 0)
    parts.push_back("drains " + Fmt(-life) + " life a second, never below 1");
  const std::string stacking = s["stacking"].Str("refresh");
  const int nMost = static_cast<int>(s["max_stacks"].Num(5));
  std::string out = parts.empty() ? std::string("Changes nothing") : Joined(parts);
  out = stacking == "stack" && nMost > 1 ? "Each stack " + out : Cap(out);
  out += " for " + (s["duration"].kind == PortJson::NUMBER ? Seconds(s["duration"].d) : std::string("a time not set yet"));
  if (stacking == "stack" && nMost > 1)
    out += "; each new hit adds a stack, up to " + std::to_string(nMost) + ", and restarts it";
  else if (stacking == "ignore")
    out += "; a new hit while it lasts does nothing";
  else
    out += "; a new hit restarts it";
  return out + ".";
}

std::string LookWords(const PortJson &s) {
  std::vector<std::string> parts;
  const std::string ov = ImageOf(s["overlay"]), st = ImageOf(s["start_image"]), en = ImageOf(s["end_image"]);
  if (!ov.empty())
    parts.push_back("shows " + StudioShowName(ST_IMAGES, ov) +
                    (s["overlay_by_size"].kind == PortJson::BOOL && s["overlay_by_size"].b ? " (by the unit's size)" : "") +
                    " on the unit while it lasts");
  if (!st.empty())
    parts.push_back("plays " + StudioShowName(ST_IMAGES, st) + " as it starts");
  if (!en.empty())
    parts.push_back("plays " + StudioShowName(ST_IMAGES, en) + " as it ends");
  return parts.empty() ? std::string("It shows nothing on the unit.") : "It " + Joined(parts) + ".";
}

// ---- where a status is used --------------------------------------------------------------------------------

struct Use {
  int t;
  std::string record;
  std::string words;
};

bool BlocksName(const PortJson &blocks, const std::string &status, bool *pbRemoves) {
  bool bAny = false;
  for (const PortJson &b : blocks.items) {
    if (b["status"].Str() == status) {
      bAny = true;
      if (b["do"].Str() == "remove_statuses")
        *pbRemoves = true;
    }
    if (b["blocks"].kind == PortJson::ARRAY && BlocksName(b["blocks"], status, pbRemoves))
      bAny = true;
  }
  return bAny;
}

std::vector<Use> UsesOf(const std::string &status) {
  std::vector<Use> out;
  for (const StudioRecord &r : StudioRecords(ST_WEAPONS))
    if (StudioRecordJson(ST_WEAPONS, r.name)["status"].Str() == status)
      out.push_back({ST_WEAPONS, r.name, "puts it on what it damages"});
  for (const StudioRecord &r : StudioRecords(ST_UNITS))
    if (StudioRecordJson(ST_UNITS, r.name)["aura"]["status"].Str() == status)
      out.push_back({ST_UNITS, r.name, "its aura gives it"});
  for (const StudioRecord &r : StudioRecords(ST_ORDERS)) {
    bool bRemoves = false;
    if (BlocksName(StudioRecordJson(ST_ORDERS, r.name)["blocks"], status, &bRemoves))
      out.push_back({ST_ORDERS, r.name, bRemoves ? "a block applies it or takes it off" : "a block applies it"});
  }
  return out;
}

// A record's name as the mod's records name it: a classic row's edit, by its own name.
std::string EditOf(int t, const std::string &name) {
  for (const StudioRecord &r : StudioRecords(t))
    if (r.kind == SK_EDIT && r.base == name)
      return r.name;
  return std::string();
}

// ---- drawing pieces -------------------------------------------------------------------------------------------

void Lab(const char *psz) {
  StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), psz, 0.1f, true);
}

void Wrapped(const std::string &s, float fW, float fPx, int tok, int nFont = STUDIO_FONT_TEXT) {
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fW);
  StudioPushFont(nFont, fPx);
  ImGui::PushStyleColor(ImGuiCol_Text, C(tok));
  ImGui::TextWrapped("%s", s.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::PopTextWrapPos();
}

void RowLabel(const char *psz, float fW = 150.0f) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsS, p.x, p.y + P(5.0f), C(TK_INK2), psz, P(fW - 6.0f));
  ImGui::SetCursorScreenPos(ImVec2(p.x + P(fW), p.y));
}

void After(const std::string &s, int tok = TK_INK3) {
  const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
  ImGui::SameLine(0.0f, P(8.0f));
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float w = TW(STUDIO_FONT_TEXT, kStudioFsXs, s);
  T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, at.x, Mid(lo.y, hi.y - lo.y, kStudioFsXs), C(tok), s);
  ImGui::Dummy(ImVec2(w, hi.y - lo.y));
}

// A section: its title and words at its head, its rows inside, its ground drawn once its height is known.
struct Section {
  ImDrawList *d = nullptr;
  ImVec2 a;
  float w = 0.0f;
};

void SectionBegin(Section &k, const char *pszGlyph, const std::string &title, const std::string &words, float fW) {
  k.d = ImGui::GetWindowDrawList();
  k.a = ImGui::GetCursorScreenPos();
  k.w = fW;
  k.d->ChannelsSplit(2);
  k.d->ChannelsSetCurrent(1);
  const float x = k.a.x + P(14.0f), y = k.a.y + P(12.0f);
  StudioGlyph(k.d, pszGlyph, ImVec2(x, y), P(16.0f), C(TK_LABEL));
  T(k.d, STUDIO_FONT_BOLD, 14.0f, x + P(26.0f), y, C(TK_HI), title, fW - P(54.0f));
  float ty = y + P(24.0f);
  if (!words.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(x, ty));
    Wrapped(words, fW - P(28.0f), kStudioFsS, TK_INK2);
    ty = ImGui::GetCursorScreenPos().y + P(4.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(x, ty));
  ImGui::BeginGroup();
}

void SectionEnd(Section &k) {
  ImGui::Dummy(ImVec2(1, 1));
  ImGui::EndGroup();
  const float bottom = ImGui::GetItemRectMax().y + P(10.0f);
  k.d->ChannelsSetCurrent(0);
  const ImVec2 b(k.a.x + k.w, bottom);
  k.d->AddRectFilled(k.a, b, C(TK_PANE2), P(4.0f));
  k.d->AddRect(k.a, b, C(TK_LINE), P(4.0f));
  k.d->AddRectFilled(k.a, ImVec2(k.a.x + P(3.0f), bottom), C(TK_SEL_LINE), P(4.0f), ImDrawFlags_RoundCornersLeft);
  k.d->ChannelsMerge();
  ImGui::SetCursorScreenPos(ImVec2(k.a.x, bottom + P(10.0f)));
  ImGui::Dummy(ImVec2(k.w, 0.0f));
}

// ---- the editor ------------------------------------------------------------------------------------------------

std::string g_sel;  // the status chosen
std::string g_scrollTo;  // a section of the editor to bring into view once (studio_statuses {section})

// Brings the editor to the section named, when a command asked for it.
void ScrollMark(const char *pszSection) {
  if (g_scrollTo == pszSection) {
    ImGui::SetScrollHereY(0.0f);
    g_scrollTo.clear();
  }
}
std::string g_previewUnit = "terran_marine";

void SetField(const std::string &status, const char *pszKey, const std::string &json) {
  StudioAsk("studio_status_set", "\"name\":" + Q(status) + ",\"field\":" + Q(pszKey) + ",\"value\":" + json);
}

// A number of the status, its words after it; set when the modder gives a new one, taken off at its default.
void NumberRow(const std::string &status, const char *pszKey, const char *pszLabel, double dDefault, double dMin, double dMax,
               const std::string &after, bool bMine) {
  const PortJson &s = JsonOf(status);
  RowLabel(pszLabel);
  ImGui::PushID(pszKey);
  double d = s[pszKey].Num(dDefault);
  if (StudioNumber("##n", &d, 86.0f, 24.0f, bMine && s[pszKey].kind != PortJson::NONE, 13.0f)) {
    d = std::max(dMin, std::min(dMax, d));
    SetField(status, pszKey, d == dDefault && strcmp(pszKey, "duration") != 0 ? std::string("null") : Fmt(d, 3));
  }
  if (!after.empty())
    After(after);
  ImGui::PopID();
}

void CheckRow(const std::string &status, const char *pszKey, const char *pszLabel, const char *pszWords, bool bMine) {
  const PortJson &s = JsonOf(status);
  RowLabel(pszLabel);
  bool b = s[pszKey].kind == PortJson::BOOL && s[pszKey].b;
  if (!bMine)
    ImGui::BeginDisabled();
  ImGui::PushID(pszKey);
  if (StudioCheck("##c", pszWords, &b))
    SetField(status, pszKey, b ? "true" : "null");
  ImGui::PopID();
  if (!bMine)
    ImGui::EndDisabled();
}

// Which animation of a picture's script damages, -1 for none: the row the loader would resolve
// the name to, read through the picture's own script (ImageScriptDamages117). A picture of the
// mod's own gives -1, since its script is the mod author's to write.
int PictureDamages(const std::string &image) {
  if (image.empty())
    return -1;
  StudioRecord r;
  if (StudioRecordOf(ST_IMAGES, image, &r))
    return ImageScriptDamages117(r.nId);
  const int nRow = StudioClassicRow(ST_IMAGES, image);
  return nRow >= 0 ? ImageScriptDamages117(nRow) : -1;
}

// The kind and the side a status comes to, asked of the engine rather than worked out here: the
// same call a game makes of the same record (ModStatusKindOfDef), so what the Studio draws and what
// the game draws cannot differ. False for a record the loader will not read, which is drawn as
// unreadable rather than as a kind.
bool KindOfStatus(const std::string &status, int *pnKind, bool *pbGood) {
  return ModStatusKindOfDef(JsonOf(status), pnKind, pbGood);
}

// What a status's icon is drawn in: the colour its look names for it, else its side's green or
// red. A look that says "for" or "against" names a side rather than a colour, which is what the
// loader makes of it too (port_modstatus.cpp), so those take the side's as well.
unsigned int SideOrOwn(const std::string &hue, bool bGood) {
  unsigned int nRgb = 0;
  if (hue.size() == 7 && hue[0] == '#' && sscanf(hue.c_str() + 1, "%6x", &nRgb) == 1)
    return nRgb;
  return bGood ? 0x6FE08Au : 0xFF7A6Au;
}

// A kind's framed icon as the readout draws it: a near-black disc for a status that helps the
// unit, a diamond for one that hurts, its edge and the kind's glyph inside it in the colour the
// status named for itself, or in its side's green or red where it named none (SideOrOwn). A
// kind's own hue is not drawn here: the side is what the icon says first, and the hue goes on the
// pattern, the tile's edge mark and a line's words. Both consoles walk the same glyph table
// (PortReadoutGlyph), so what is drawn here is what the game draws.
void FramedIcon(ImDrawList *d, ImVec2 at, float fZ, int nKind, bool bGood, unsigned int nInk) {
  const ImU32 col = IM_COL32((nInk >> 16) & 0xFF, (nInk >> 8) & 0xFF, nInk & 0xFF, 255);
  const ImVec2 mid(at.x + fZ * 0.5f, at.y + fZ * 0.5f);
  const float r = fZ * 0.5f;
  const ImU32 fill = IM_COL32(0x06, 0x0A, 0x0E, 235);
  if (bGood) {
    d->AddCircleFilled(mid, r, fill, 20);
    d->AddCircle(mid, r, col, 20, std::max(1.0f, fZ / 14.0f));
  } else {
    const ImVec2 pts[4] = {ImVec2(mid.x, at.y), ImVec2(at.x + fZ, mid.y), ImVec2(mid.x, at.y + fZ),
                           ImVec2(at.x, mid.y)};
    d->AddConvexPolyFilled(pts, 4, fill);
    d->AddPolyline(pts, 4, col, ImDrawFlags_Closed, std::max(1.0f, fZ / 14.0f));
  }
  // the glyph: a run of strokes in a 16 by 16 square, a count of points then that many x, y pairs
  const signed char *p = PortReadoutGlyph(nKind);
  if (p == nullptr)
    return;
  const float k = fZ * 0.56f / 16.0f;
  const float ox = at.x + fZ * 0.22f, oy = at.y + fZ * 0.22f;
  const float fWide = std::max(1.0f, fZ / 14.0f);
  for (; *p != 0;) {
    const int nPoints = *p++;
    ImVec2 prev;
    for (int i = 0; i < nPoints; ++i, p += 2) {
      const ImVec2 q(ox + p[0] * k, oy + p[1] * k);
      if (i > 0)
        d->AddLine(prev, q, col, fWide);
      prev = q;
    }
  }
}

// One key of the status's look, which is only ever an override: left out, the kind worked out from
// the status's own fields decides it.
void LookSet(const std::string &status, const char *pszKey, const std::string &json) {
  StudioAsk("studio_status_set",
            "\"name\":" + Q(status) + ",\"field\":" + Q(std::string("look.") + pszKey) + ",\"value\":" + json);
}

void ImageRow(const std::string &status, const char *pszKey, const char *pszLabel, const char *pszNone, bool bMine) {
  const PortJson &s = JsonOf(status);
  RowLabel(pszLabel);
  if (!bMine)
    ImGui::BeginDisabled();
  ImGui::PushID(pszKey);
  std::string picked;
  const std::string now = ImageOf(s[pszKey]);
  if (StudioRecordPick("##img", ST_IMAGES, now, 300.0f, &picked, true, 24.0f, pszNone))
    SetField(status, pszKey, picked.empty() ? std::string("null") : Q(picked));
  ImGui::PopID();
  if (!bMine)
    ImGui::EndDisabled();
  // a weapon's own picture damages in one of its animations, which is how a shot applies its
  // damage: worn as a status it is asked to damage with no shot behind it, and nothing happens.
  // Said here as the loader says it in the log, so a modder sees it while picking
  const int nAnim = PictureDamages(now);
  if (nAnim >= 0) {
    ImGui::SameLine();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *d = ImGui::GetWindowDrawList();
    float x = at.x + P(8.0f) + StudioChip(d, ImVec2(at.x + P(8.0f), at.y + P(3.0f)), "a weapon's own", CH_WARN) +
              P(8.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, at.y + P(6.0f), C(TK_WARN),
      std::string("it damages in its ") + IscriptAnimName(nAnim) + " animation" +
          (nAnim == 0 ? ", as soon as it is drawn" : ", which a unit's death plays") +
          ": it wants a picture meant to sit on a unit");
    ImGui::Dummy(ImVec2(P(520.0f), P(24.0f)));
  }
}

// The image a unit is drawn with: its flingy's sprite's.
std::string UnitImage(const std::string &unit) {
  const std::string flingy = StudioRefName(ST_FLINGY, StudioNumber(ST_UNITS, unit, "flingy"));
  const std::string sprite = StudioRefName(ST_SPRITES, StudioNumber(ST_FLINGY, flingy, "sprite"));
  return StudioRefName(ST_IMAGES, StudioNumber(ST_SPRITES, sprite, "image"));
}

// The look on a unit, played: the unit's picture, the overlay over it at the same place and scale, its
// frames in turn; the start and end images beside it.
void LookPreview(const PortJson &s, ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  d->AddRectFilled(a, b, C(TK_WELL), P(4.0f));
  d->AddRect(a, b, C(TK_LINE), P(4.0f));
  const std::string unitImage = UnitImage(g_previewUnit);
  const std::string ov = ImageOf(s["overlay"]);
  const double t = ImGui::GetTime();
  float k = 0.0f;
  ImVec2 off;
  const ImVec2 ia(a.x + P(10.0f), a.y + P(10.0f)), ib(b.x - P(10.0f), b.y - P(26.0f));
  const bool bUnit = !unitImage.empty() && studio_art::DrawImageAt(d, ia, ib, unitImage, -1, 8, false, 0, &k, &off, -1);
  if (!ov.empty()) {
    std::string img = ov;
    if (s["overlay_by_size"].kind == PortJson::BOOL && s["overlay_by_size"].b) {
      const long long nSize = std::max(1LL, std::min(3LL, StudioNumber(ST_UNITS, g_previewUnit, "size", 1)));
      const int nRow = StudioClassicRow(ST_IMAGES, ov);
      if (nRow >= 0 && StudioClassicName(ST_IMAGES, nRow + static_cast<int>(nSize) - 1))
        img = StudioClassicName(ST_IMAGES, nRow + static_cast<int>(nSize) - 1);
    }
    const int nFrames = std::max(1, studio_art::ImageFrames(img));
    const bool bTurns = studio_art::ImageTurns(img);
    const int nSets = bTurns ? std::max(1, nFrames / 17) : nFrames;
    const int nSet = static_cast<int>(t * 10.0) % nSets;
    if (!bUnit)
      k = 0.0f;
    if (!studio_art::DrawImageAt(d, ia, ib, img, nSet, 8, false, 0, &k, &off, -1))
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(10.0f), a.y + P(10.0f), C(TK_INK3), "its frames are not drawn here", b.x - a.x - P(20.0f));
  }
  if (!bUnit && ov.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(10.0f), a.y + P(10.0f), C(TK_INK3), "no look: pick an overlay", b.x - a.x - P(20.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, a.x + P(10.0f), b.y - P(20.0f), C(TK_INK3), "on the " + Short(ST_UNITS, g_previewUnit),
    b.x - a.x - P(20.0f));
}

void UsesSection(const std::string &status, float fW, bool bMine) {
  const std::vector<Use> uses = UsesOf(status);
  Section k;
  SectionBegin(k, "link", "Where it is used",
               uses.empty() ? "Nothing applies it yet: put it on a weapon, give it as a unit's aura, or apply it with an action's block."
                            : std::to_string(uses.size()) + (uses.size() == 1 ? " place applies it." : " places apply it."),
               fW);
  float x0 = ImGui::GetCursorScreenPos().x, x = x0, y = ImGui::GetCursorScreenPos().y;
  for (size_t i = 0; i < uses.size(); ++i) {
    const Use &u = uses[i];
    const char *pszGlyph = u.t == ST_WEAPONS ? "weapon" : u.t == ST_UNITS ? "unit" : "action";
    const std::string label = StudioShowName(u.t, u.record) + ": " + u.words;
    const float bw = StudioButtonSize(label.c_str(), pszGlyph, 24.0f, kStudioFsXs).x;
    if (x + bw > x0 + fW - P(28.0f) && x > x0) {
      x = x0;
      y += P(30.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushID(static_cast<int>(i));
    if (StudioButton("##use", label.c_str(), pszGlyph, SB_PLAIN, 24.0f, 0.0f, kStudioFsXs)) {
      if (u.t == ST_ORDERS)
        StudioAsk("studio_actions", "\"show\":" + Q(u.record));
      else
        StudioOpenDoc(u.t, u.record, u.t == ST_UNITS ? 2 : 0);
    }
    ImGui::PopID();
    x += bw + P(6.0f);
  }
  if (!uses.empty())
    ImGui::SetCursorScreenPos(ImVec2(x0, y + P(32.0f)));
  if (bMine) {
    RowLabel("Put it on a weapon");
    std::string picked;
    if (StudioRecordPick("##onweapon", ST_WEAPONS, std::string(), 260.0f, &picked, false, 24.0f, "pick a weapon"))
      StudioAsk("studio_weapon_status", "\"weapon\":" + Q(picked) + ",\"status\":" + Q(status));
    After("every unit it damages gets it");
    RowLabel("Give it as an aura");
    if (StudioRecordPick("##onunit", ST_UNITS, std::string(), 260.0f, &picked, false, 24.0f, "pick a unit"))
      StudioAsk("studio_unit_aura", "\"unit\":" + Q(picked) + ",\"aura\":{\"status\":" + Q(status) + "}");
    After("its allies within 5 tiles get it each second");
  }
  SectionEnd(k);
}

void Editor(const std::string &status, float fW) {
  const PortJson &s = JsonOf(status);
  const bool bMine = MineStatus(status);
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const std::string label = s["name"].Str(status.c_str());
  float tx = p.x;
  if (s["icon"].kind == PortJson::NUMBER) {
    StudioIconPicture(d, p, ImVec2(p.x + P(36.0f), p.y + P(36.0f)), static_cast<int>(s["icon"].d));
    tx += P(46.0f);
  }
  T(d, STUDIO_FONT_BOLD, 22.0f, tx, p.y + P(4.0f), C(TK_HI), label, fW - (tx - p.x));
  float cx = p.x;
  const float cy = p.y + P(44.0f);
  cx += StudioChip(d, ImVec2(cx, cy), ("in the files: " + status).c_str()) + P(6.0f);
  if (!bMine) {
    for (const Status &st : List())
      if (st.name == status)
        cx += StudioChip(d, ImVec2(cx, cy), ("from " + st.mod + ", read only").c_str(), CH_GOLD) + P(6.0f);
  }
  const size_t nUses = UsesOf(status).size();
  StudioChip(d, ImVec2(cx, cy), nUses == 0 ? "not used yet" : ("used in " + std::to_string(nUses) + (nUses == 1 ? " place" : " places")).c_str(),
             nUses == 0 ? CH_WARN : CH_GOOD);
  ImGui::SetCursorScreenPos(ImVec2(p.x, cy + P(28.0f)));
  Wrapped(Sentence(s), fW, 16.0f, TK_INK);
  ImGui::Dummy(ImVec2(1, P(2.0f)));
  Wrapped(LookWords(s), fW, kStudioFsS, TK_INK2);
  ImGui::Dummy(ImVec2(1, P(8.0f)));

  if (!bMine)
    ImGui::BeginDisabled();
  ImGui::PushID(status.c_str());
  // its name
  Section k;
  SectionBegin(k, "note", "Its name", "What a player reads, and the command icon it may show.", fW);
  RowLabel("Name");
  static char s_szName[64];
  static std::string s_nameFor;
  if (s_nameFor != status + label)
    snprintf(s_szName, sizeof s_szName, "%s", label.c_str()), s_nameFor = status + label;
  StudioInput("##name", s_szName, sizeof s_szName, 260.0f, 24.0f, status.c_str());
  if (ImGui::IsItemDeactivated() && label != s_szName)
    SetField(status, "name", s_szName[0] ? Q(s_szName) : std::string("null"));
  RowLabel("Icon");
  int nIcon = static_cast<int>(s["icon"].Num(-1));
  if (StudioIconPick("##icon", &nIcon, 30.0f, bMine))
    SetField(status, "icon", nIcon < 0 ? std::string("null") : std::to_string(nIcon));
  After("the command icon its tooltip shows; a click picks another");
  SectionEnd(k);

  // how long
  SectionBegin(k, "pause", "How long it lasts", "From when it is put on, in seconds at Fastest; what a new one does while it lasts.", fW);
  NumberRow(status, "duration", "Lasts", 0, 1.0 / 24.0, 2700, "seconds", bMine);
  RowLabel("A new hit");
  static const char *const kStack[] = {"restarts it", "adds a stack", "does nothing"};
  const std::string stacking = s["stacking"].Str("refresh");
  const int nStack = stacking == "stack" ? 1 : stacking == "ignore" ? 2 : 0;
  const int nNew = StudioSeg("##stack", kStack, 3, nStack, 24.0f, kStudioFsXs);
  if (nNew != nStack)
    SetField(status, "stacking", nNew == 0 ? std::string("null") : nNew == 1 ? Q("stack") : Q("ignore"));
  if (nStack == 1)
    NumberRow(status, "max_stacks", "Stacks at most", 5, 1, 255, "each stack adds what it changes once more", bMine);
  SectionEnd(k);

  // what it changes
  ScrollMark("changes");
  SectionBegin(k, "try", "What it changes", "In percent of the unit's own, or in points; 0 leaves it as it is.", fW);
  const double sp = s["move_speed"].Num(0), cd = s["attack_cooldown"].Num(0), dm = s["damage_dealt"].Num(0), ar = s["armor"].Num(0);
  NumberRow(status, "move_speed", "Speed", 0, -100, 400, sp < 0 ? Fmt(-sp) + " % slower" : sp > 0 ? Fmt(sp) + " % faster" : "% (-50 is half its speed)", bMine);
  NumberRow(status, "attack_cooldown", "Time between attacks", 0, -90, 400,
            cd > 0 ? Fmt(cd) + " % longer: it attacks slower" : cd < 0 ? Fmt(-cd) + " % shorter: it attacks faster" : "%", bMine);
  NumberRow(status, "damage_dealt", "Damage it deals", 0, -100, 400, dm > 0 ? Fmt(dm) + " % more" : dm < 0 ? Fmt(-dm) + " % less" : "%", bMine);
  const double dt = s["damage_taken"].Num(0);
  NumberRow(status, "damage_taken", "Damage it takes", 0, -100, 400,
            dt > 0 ? Fmt(dt) + " % more: it is softer" : dt < 0 ? Fmt(-dt) + " % less: it is harder" : "%", bMine);
  NumberRow(status, "armor", "Armour", 0, -100, 100, ar > 0 ? "points more" : ar < 0 ? "points less" : "points", bMine);
  CheckRow(status, "cannot_move", "Moving", "it cannot move while it lasts", bMine);
  CheckRow(status, "cannot_attack", "Attacking", "it cannot attack while it lasts", bMine);
  // which side it is on, where the way its fields point reads wrong: what shows the status, the
  // unit readout and the status's own look read it
  RowLabel("Its side");
  if (!bMine)
    ImGui::BeginDisabled();
  static const char *const kSides[] = {"from its fields", "good", "bad"};
  const std::string side = s["side"].Str();
  const int nSideNow = side == "good" ? 1 : side == "bad" ? 2 : 0;
  const int nSide = StudioSeg("##side", kSides, 3, nSideNow);
  if (nSide != nSideNow)
    SetField(status, "side", nSide == 0 ? std::string("null") : nSide == 1 ? Q("good") : Q("bad"));
  if (!bMine)
    ImGui::EndDisabled();
  ImGui::SameLine();
  {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, at.x + P(8.0f), at.y + P(8.0f), C(TK_INK3),
      "good is one the unit wants, bad one put on it");
    ImGui::Dummy(ImVec2(P(240.0f), P(26.0f)));
  }
  SectionEnd(k);

  // life over time
  ScrollMark("life");
  const double life = s["life_per_second"].Num(0);
  SectionBegin(k, "plus", "Life over time", "Given every third of a second; a stack gives it again.", fW);
  NumberRow(status, "damage_per_second", "Damage a second", 0, 0, 5000, "life lost; it can kill", bMine);
  NumberRow(status, "life_per_second", "Life a second", 0, -5000, 5000,
            life < 0 ? "lost, never below 1 life" : life > 0 ? "healed, up to its most" : "healed, or lost when below 0", bMine);
  SectionEnd(k);

  // how it shows itself: the kind, which the readout draws from, and what the look overrides
  ScrollMark("kind");
  SectionBegin(k, "mark", "How it shows itself",
               "The readout draws a status by its kind, which its own fields decide. The look below "
               "overrides that, a key at a time.",
               fW);
  {
    ImDrawList *d = ImGui::GetWindowDrawList();
    const PortJson &look = s["look"];
    const std::string kind = look["kind"].Str();
    const int nNamed = kind.empty() ? -1 : PortReadoutKindOf(kind.c_str());
    // the kind and the side the status comes to, the engine's own answer for this record
    int nCame = -1;
    bool bGood = false;
    const bool bReads = KindOfStatus(status, &nCame, &bGood);
    const int nDrawn = nCame >= 0 ? nCame : PORT_KIND_MIGHT;
    const std::string hue = look["hue"].Str(look["colour"].Str("").c_str());
    RowLabel("Its kind");
    const ImVec2 kAt = ImGui::GetCursorScreenPos();
    FramedIcon(d, ImVec2(kAt.x, kAt.y + P(2.0f)), P(28.0f), nDrawn, bGood, SideOrOwn(hue, bGood));
    ImGui::SetCursorScreenPos(ImVec2(kAt.x + P(36.0f), kAt.y));
    if (!bMine)
      ImGui::BeginDisabled();
    if (StudioSelectBegin("##kind",
                          nNamed >= 0 ? (std::string(PortReadoutKindName(nNamed)) + "  " +
                                         PortReadoutKindWords(nNamed))
                                            .c_str()
                                      : "from its own fields",
                          P(260.0f))) {
      if (StudioItem("from its own fields", nNamed < 0))
        LookSet(status, "kind", "null");
      for (int i = 0; i < PORT_READOUT_KINDS; ++i)
        if (StudioItem((std::string(PortReadoutKindName(i)) + "  " + PortReadoutKindWords(i)).c_str(), i == nNamed))
          LookSet(status, "kind", Q(PortReadoutKindName(i)));
      StudioSelectEnd();
    }
    if (!bMine)
      ImGui::EndDisabled();
    ImGui::SameLine();
    {
      const ImVec2 w = ImGui::GetCursorScreenPos();
      const std::string why =
          !bReads ? std::string("the loader will not read this status, so no kind comes of it")
          : nNamed >= 0
              ? std::string("named here, so its own fields do not decide it")
              : std::string("its own fields come to ") + PortReadoutKindName(nDrawn) + ", " +
                    Lower(PortReadoutKindWords(nDrawn)) + ", on the " + (bGood ? "unit's side" : "side against it");
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, w.x + P(8.0f), w.y + P(8.0f), C(bReads ? TK_INK3 : TK_WARN), why,
        P(330.0f));
      ImGui::Dummy(ImVec2(P(340.0f), P(30.0f)));
    }
    // the picture it is drawn with, where the kind's own icon is not wanted
    RowLabel("Its own icon");
    if (!bMine)
      ImGui::BeginDisabled();
    double dIcon = look["icon"].Num(-1);
    const bool bIcon = look["icon"].kind == PortJson::NUMBER;
    if (StudioNumber("##lookicon", &dIcon, P(90.0f), P(24.0f), bMine && bIcon, 13.0f)) {
      dIcon = dIcon < 0 ? 0 : dIcon > 65535 ? 65535 : dIcon;
      LookSet(status, "icon", Fmt(dIcon, 0));
    }
    if (!bMine)
      ImGui::EndDisabled();
    ImGui::SameLine();
    if (StudioButton("##iconx", bIcon ? "" : "Give it one", bIcon ? "x" : nullptr, SB_FLAT, 24.0f, 0.0f,
                     kStudioFsXs, bMine))
      LookSet(status, "icon", bIcon ? "null" : std::to_string(PortReadoutKindIcon(nDrawn)));
    ImGui::SameLine();
    {
      const ImVec2 w = ImGui::GetCursorScreenPos();
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, w.x + P(8.0f), w.y + P(6.0f), C(TK_INK3),
        bIcon ? "a frame of the game's command icons"
              : std::string("the kind's own, frame ") + std::to_string(PortReadoutKindIcon(nDrawn)));
      ImGui::Dummy(ImVec2(P(300.0f), P(26.0f)));
    }
    // the colour its icon, its pattern, the tile's edge mark and its line's words are drawn in.
    // A colour of its own is drawn as it stands; "for" and "against" name a side rather than a
    // colour, so those leave the icon and the words to the side's own green or red.
    RowLabel("Its colour");
    if (!bMine)
      ImGui::BeginDisabled();
    const bool bRgb = hue.size() == 7 && hue[0] == '#';
    if (StudioSelectBegin("##hue",
                          bRgb ? hue.c_str() : hue == "for" ? "its side's, for the unit"
                                 : hue == "against" ? "its side's, against the unit"
                                                    : "the kind's own",
                          P(220.0f))) {
      if (StudioItem("the kind's own", hue.empty()))
        LookSet(status, "hue", "null");
      if (StudioItem("its side's, for the unit", hue == "for"))
        LookSet(status, "hue", Q("for"));
      if (StudioItem("its side's, against the unit", hue == "against"))
        LookSet(status, "hue", Q("against"));
      if (StudioItem("a colour of its own", bRgb))
        LookSet(status, "hue", Q("#ffffff"));
      StudioSelectEnd();
    }
    if (!bMine)
      ImGui::EndDisabled();
    if (bRgb) {
      ImGui::SameLine();
      char szHue[16];
      snprintf(szHue, sizeof szHue, "%s", hue.c_str());
      if (StudioInput("##huergb", szHue, sizeof szHue, P(96.0f), P(24.0f), "#rrggbb")) {
        unsigned int n = 0;
        if (strlen(szHue) == 7 && szHue[0] == '#' && sscanf(szHue + 1, "%6x", &n) == 1)
          LookSet(status, "hue", Q(szHue));
        else
          StudioSay("a look's colour is #rrggbb, or for or against to take its side's");
      }
    }
    // the colour is not the framed icon's: that is near-black with the side's green or red on its
    // edge and its glyph, whichever status wears it
    {
      const ImVec2 w = ImGui::GetCursorScreenPos();
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, w.x + P(8.0f), w.y + P(6.0f), C(TK_INK3),
        "a colour of its own draws the framed icon's edge and glyph, the pattern over the wireframe, the "
        "wireframe's own tint where the readout asks for that, the status's row in a HUD's room and its "
        "mark in the group view. for and against name a side rather than a colour, so all of those take "
        "its green or red instead, which is also what a status that names none takes",
        P(330.0f));
    }
    ImGui::Dummy(ImVec2(fW, P(40.0f)));
    // where it shows at all
    RowLabel("Shows on");
    if (!bMine)
      ImGui::BeginDisabled();
    static const char *const kWhere[3] = {"readout", "wire", "group"};
    static const char *const kWhereWords[3] = {"the readout's lines", "the wireframe's mark",
                                               "the group view"};
    const PortJson &show = look["show"];
    const bool bAll = show.kind != PortJson::ARRAY;
    for (int i = 0; i < 3; ++i) {
      bool bOn = bAll;
      for (size_t n = 0; !bAll && n < show.size(); ++n)
        bOn = bOn || show.At(n).Str() == kWhere[i];
      ImGui::PushID(i);
      if (StudioCheck("##w", kWhereWords[i], &bOn)) {
        PortJson out;
        out.kind = PortJson::ARRAY;
        for (int n = 0; n < 3; ++n) {
          bool bKeep = n == i ? bOn : bAll;
          for (size_t m = 0; !bAll && n != i && m < show.size(); ++m)
            bKeep = bKeep || show.At(m).Str() == kWhere[n];
          if (bKeep) {
            PortJson one;
            one.kind = PortJson::STRING;
            one.s = kWhere[n];
            out.items.push_back(one);
          }
        }
        LookSet(status, "show", out.items.size() == 3 ? std::string("null") : PortJsonWrite(out));
      }
      ImGui::PopID();
      if (i < 2)
        ImGui::SameLine(0.0f, P(14.0f));
    }
    if (!bMine)
      ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(fW, P(28.0f)));
    // which status the one mark on the wireframe shows, where the kind's own order reads wrong
    RowLabel("Asks for the mark");
    if (!bMine)
      ImGui::BeginDisabled();
    double dPri = look["priority"].Num(PortReadoutKindPriority(nDrawn));
    const bool bPri = look["priority"].kind == PortJson::NUMBER;
    if (StudioNumber("##pri", &dPri, P(90.0f), P(24.0f), bMine && bPri, 13.0f)) {
      dPri = dPri < 0 ? 0 : dPri > 1000 ? 1000 : dPri;
      LookSet(status, "priority", Fmt(dPri, 0));
    }
    if (!bMine)
      ImGui::EndDisabled();
    ImGui::SameLine();
    if (StudioButton("##prix", bPri ? "" : "Give it one", bPri ? "x" : nullptr, SB_FLAT, 24.0f, 0.0f, kStudioFsXs,
                     bMine))
      LookSet(status, "priority", bPri ? "null" : std::to_string(PortReadoutKindPriority(nDrawn)));
    ImGui::SameLine();
    {
      const ImVec2 w = ImGui::GetCursorScreenPos();
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, w.x + P(8.0f), w.y + P(6.0f), C(TK_INK3),
        bPri ? "the higher, the sooner: a wireframe has room for one mark"
             : std::string("the kind's own, ") + std::to_string(PortReadoutKindPriority(nDrawn)));
      ImGui::Dummy(ImVec2(P(340.0f), P(26.0f)));
    }
    // the pattern laid over the wireframe
    RowLabel("Its pattern");
    if (!bMine)
      ImGui::BeginDisabled();
    char szPat[64];
    snprintf(szPat, sizeof szPat, "%s", look["pattern"].Str().c_str());
    if (StudioInput("##pat", szPat, sizeof szPat, P(180.0f), P(24.0f), PortReadoutKindPattern(nDrawn)))
      LookSet(status, "pattern", szPat[0] == '\0' ? std::string("null") : Q(szPat));
    if (!bMine)
      ImGui::EndDisabled();
    ImGui::SameLine();
    {
      const ImVec2 w = ImGui::GetCursorScreenPos();
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, w.x + P(8.0f), w.y + P(6.0f), C(TK_INK3),
        std::string("over the wireframe; the kind's own is ") + PortReadoutKindPattern(nDrawn));
      ImGui::Dummy(ImVec2(P(340.0f), P(26.0f)));
    }
  }
  SectionEnd(k);

  // its look
  ScrollMark("look");
  SectionBegin(k, "look", "Its look on the unit", "An image over the unit while it lasts, and images played once as it starts and ends.", fW);
  {
    const ImVec2 row = ImGui::GetCursorScreenPos();
    const float pw = P(230.0f), ph = P(190.0f);
    ImGui::BeginGroup();
    ImageRow(status, "overlay", "While it lasts", "no overlay", bMine);
    CheckRow(status, "overlay_by_size", "By size", "the next images on medium and large units, as Ensnare's", bMine);
    ImageRow(status, "start_image", "As it starts", "nothing", bMine);
    ImageRow(status, "end_image", "As it ends", "nothing", bMine);
    RowLabel("Shown on");
    std::string picked;
    if (StudioRecordPick("##prev", ST_UNITS, g_previewUnit, 300.0f, &picked, false, 24.0f, "a unit"))
      g_previewUnit = picked;
    ImGui::EndGroup();
    // the preview beside the fields, or below them when the editor is too narrow for both
    const float groupBottom = ImGui::GetItemRectMax().y;
    float lx = std::max(ImGui::GetItemRectMax().x + P(16.0f), row.x + fW - P(28.0f) - pw);
    float ly = row.y;
    if (lx + pw > row.x + fW) {
      lx = row.x + P(150.0f);
      ly = groupBottom + P(8.0f);
    }
    if (!bMine)
      ImGui::EndDisabled();
    LookPreview(s, ImVec2(lx, ly), ImVec2(lx + pw, ly + ph));
    if (!bMine)
      ImGui::BeginDisabled();
    ImGui::SetCursorScreenPos(ImVec2(row.x, std::max(groupBottom, ly + ph)));
    ImGui::Dummy(ImVec2(1, 1));
  }
  SectionEnd(k);
  ImGui::PopID();
  if (!bMine)
    ImGui::EndDisabled();
  ScrollMark("uses");
  UsesSection(status, fW, bMine);
}

// The kinds New starts from.
struct Kind {
  const char *pszKey;
  const char *pszLabel;
  const char *pszFields;  // its fields, as JSON members
};
const Kind kKinds[] = {
    {"blank", "Blank", "\"duration\":5"},
    {"slow", "Slow", "\"duration\":4,\"move_speed\":-50,\"attack_cooldown\":25,\"overlay\":\"thingy_ensgoos\",\"overlay_by_size\":true"},
    {"poison", "Poison", "\"duration\":6,\"damage_per_second\":4,\"stacking\":\"stack\",\"max_stacks\":3"},
    {"stun", "Stun", "\"duration\":2,\"cannot_move\":true,\"cannot_attack\":true"},
    {"haste", "Haste", "\"duration\":8,\"move_speed\":40,\"attack_cooldown\":-25"},
    {"weaken", "Weaken", "\"duration\":10,\"armor\":-3,\"damage_dealt\":-25"},
    {"mending", "Mending", "\"duration\":2,\"life_per_second\":5"},
};

std::string FreeName(const std::string &want) {
  std::string name = want;
  for (int i = 2; Exists(name); ++i)
    name = want + "_" + std::to_string(i);
  return name;
}

void NewStatus(const std::string &from, const std::string &label) {
  const std::string name = FreeName(Lower(label));
  DesignCall("studio_status_new", "\"name\":" + Q(name) + ",\"label\":" + Q(label) + ",\"from\":" + Q(from) + ",\"sender\":\"you\"",
             [name](const PortJson &a) {
               if (a["ok"].kind == PortJson::BOOL && a["ok"].b) {
                 g_sel = name;
                 StudioSay("made the status " + name);
               } else {
                 StudioSay(a["error"].Str("refused"));
               }
             });
}

bool ListRow(const char *pszId, const std::string &title, const std::string &sub, bool bOn, bool bMine, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = P(46.0f);
  const bool b = ImGui::InvisibleButton(pszId, ImVec2(fW, h));
  const bool bHover = ImGui::IsItemHovered();
  if (bOn || bHover)
    d->AddRectFilled(p, ImVec2(p.x + fW, p.y + h), C(bOn ? TK_SEL : TK_ROW_HI), P(3.0f));
  if (bOn)
    d->AddRectFilled(p, ImVec2(p.x + P(3.0f), p.y + h), C(TK_SEL_LINE));
  StudioGlyph(d, "look", ImVec2(p.x + P(8.0f), p.y + P(7.0f)), P(16.0f), C(bMine ? TK_LABEL : TK_INK3));
  T(d, STUDIO_FONT_BOLD, 13.0f, p.x + P(30.0f), p.y + P(5.0f), bOn ? C(TK_HI) : C(TK_INK), title, fW - P(36.0f));
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, p.x + P(30.0f), p.y + P(25.0f), C(TK_INK3), sub, fW - P(36.0f));
  if (bHover)
    ImGui::SetTooltip("%s", sub.c_str());
  return b;
}

// ---- the commands ---------------------------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

std::string StatusJson(const Status &s) {
  std::string uses;
  for (const Use &u : UsesOf(s.name))
    uses += std::string(uses.empty() ? "" : ",") + "{\"table\":" + Q(StudioTableName(u.t)) + ",\"name\":" + Q(u.record) + ",\"words\":" + Q(u.words) + "}";
  return "{\"name\":" + Q(s.name) + ",\"label\":" + Q((*s.pJson)["name"].Str(s.name.c_str())) + ",\"read_only\":" +
         (s.bMine ? "false" : "true") + ",\"mod\":" + Q(s.mod) + ",\"sentence\":" + Q(Sentence(*s.pJson)) + ",\"look\":" +
         Q(LookWords(*s.pJson)) + ",\"fields\":" + PortJsonWrite(*s.pJson) + ",\"used_by\":[" + uses + "]}";
}

bool CmdStatuses(const PortJson &j, std::string *pAnswer) {
  const std::string query = j["query"].Str(), show = j["show"].Str();
  if (!show.empty()) {
    if (!Exists(show)) {
      *pAnswer = "show names no status of the mod's";
      return false;
    }
    StudioStatusesShow(show);
  }
  if (!j["section"].Str().empty())
    g_scrollTo = j["section"].Str();  // changes, life, look or uses
  std::string list;
  for (const Status &s : List())
    if (Has(s.name, query.c_str()) || Has((*s.pJson)["name"].Str(), query.c_str()) || Has(Sentence(*s.pJson), query.c_str()))
      list += std::string(list.empty() ? "" : ",") + StatusJson(s);
  *pAnswer = "\"statuses\":[" + list + "],\"selected\":" + Q(g_sel);
  return true;
}

bool CheckValue(const std::string &key, const PortJson &v, std::string *pWhy) {
  if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV)
    return true;
  if (key == "name" || key == "comment" || key == "stacking") {
    if (v.kind != PortJson::STRING || (key == "stacking" && v.s != "refresh" && v.s != "stack" && v.s != "ignore")) {
      *pWhy = key == "stacking" ? "stacking is refresh, stack or ignore" : key + " is text";
      return false;
    }
    return true;
  }
  if (key == "cannot_move" || key == "cannot_attack" || key == "overlay_by_size") {
    if (v.kind != PortJson::BOOL) {
      *pWhy = key + " is true or false";
      return false;
    }
    return true;
  }
  if (key == "overlay" || key == "start_image" || key == "end_image") {
    if (v.kind == PortJson::STRING) {
      StudioRecord r;
      if (!StudioRecordOf(ST_IMAGES, v.s, &r) && StudioClassicRow(ST_IMAGES, v.s) < 0) {
        *pWhy = key + " names no image " + v.s;
        return false;
      }
      return true;
    }
    if (v.kind != PortJson::NUMBER) {
      *pWhy = key + " is an image, by name or number";
      return false;
    }
    return true;
  }
  if (v.kind != PortJson::NUMBER) {
    *pWhy = key + " is a number";
    return false;
  }
  // the loader's bounds (port_modstatus.cpp), so a value it would refuse is refused here
  static const struct {
    const char *pszKey;
    double dMin, dMax;
  } kBounds[] = {{"duration", 1.0 / 24.0, 2700}, {"max_stacks", 1, 255},       {"move_speed", -100, 400},
                 {"armor", -100, 100},           {"damage_dealt", -100, 400},   {"attack_cooldown", -90, 400},
                 {"damage_per_second", 0, 5000}, {"life_per_second", -5000, 5000}};
  for (const auto &b : kBounds)
    if (key == b.pszKey && (v.d < b.dMin || v.d > b.dMax)) {
      *pWhy = key + " takes a number from " + Fmt(b.dMin, 3) + " to " + Fmt(b.dMax);
      return false;
    }
  return true;
}

// A copy of a block list with each status name renamed, or the blocks naming it taken away (to empty).
PortJson Renamed(const PortJson &blocks, const std::string &from, const std::string &to) {
  PortJson out = blocks;
  out.items.clear();
  for (const PortJson &b : blocks.items) {
    if (b["status"].Str() == from && to.empty())
      continue;
    PortJson c = b;
    if (b["status"].Str() == from)
      StudioJsonPut(c, "status", StudioJsonString(to));
    if (b["blocks"].kind == PortJson::ARRAY)
      StudioJsonPut(c, "blocks", Renamed(b["blocks"], from, to));
    out.items.push_back(c);
  }
  return out;
}

// Every use of a status turned to another name, or taken away when to is empty; inside the group open.
bool Repoint(const std::string &from, const std::string &to, const std::string &sender, std::string *pWhy) {
  bool bOk = true;
  for (const Use &u : UsesOf(from)) {
    StudioRecord r;
    if (!StudioRecordOf(u.t, u.record, &r) || (r.kind != SK_OWN && r.kind != SK_EDIT))
      continue;
    if (u.t == ST_WEAPONS)
      bOk = bOk && (to.empty() ? StudioRevertField(ST_WEAPONS, u.record, "status", sender, pWhy)
                               : StudioSetField(ST_WEAPONS, u.record, "status", StudioJsonString(to), sender, std::string(), pWhy));
    if (u.t == ST_UNITS) {
      PortJson a = StudioRecordJson(ST_UNITS, u.record)["aura"];
      StudioJsonPut(a, "status", StudioJsonString(to));
      bOk = bOk && (to.empty() ? StudioRevertField(ST_UNITS, u.record, "aura", sender, pWhy)
                               : StudioSetField(ST_UNITS, u.record, "aura", a, sender, std::string(), pWhy));
    }
    if (u.t == ST_ORDERS)
      bOk = bOk && StudioSetField(ST_ORDERS, u.record, "blocks", Renamed(StudioRecordJson(ST_ORDERS, u.record)["blocks"], from, to), sender,
                                  std::string(), pWhy);
  }
  return bOk;
}

bool Undone(int nAt, const std::string &why, std::string *pAnswer) {
  std::string ignore;
  if (StudioHistoryAt() > nAt)
    StudioUndo(&ignore);
  *pAnswer = why;
  return false;
}

bool CmdStatusNew(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  const std::string from = j["from"].Str("blank");
  PortJson rec;
  const Kind *pKind = nullptr;
  for (const Kind &k : kKinds)
    if (from == k.pszKey)
      pKind = &k;
  if (pKind) {
    std::string err;
    PortJsonParse(("{" + std::string(pKind->pszFields) + "}").c_str(), &rec, &err);
  } else if (Exists(from)) {
    rec = JsonOf(from);
  } else {
    std::string kinds;
    for (const Kind &k : kKinds)
      kinds += std::string(kinds.empty() ? "" : ", ") + k.pszKey;
    *pAnswer = "from is a kind (" + kinds + ") or a status of the mod's to copy";
    return false;
  }
  const std::string label = j["label"].Str(pKind ? pKind->pszLabel : (JsonOf(from)["name"].Str(from.c_str()) + " copy").c_str());
  std::string name = j["name"].Str();
  if (name.empty()) {
    for (char ch : Lower(label))
      name += isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
    name = FreeName(name.empty() ? std::string("status") : name);
  }
  for (char ch : name)
    if (!isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
      *pAnswer = "a status's name is letters, digits and _";
      return false;
    }
  if (Exists(name)) {
    *pAnswer = name + " is taken among the statuses: give it another name";
    return false;
  }
  StudioJsonPut(rec, "name", StudioJsonString(label));
  for (const PortJsonMember &m : j["values"].members) {
    std::string why;
    if (!IsField(m.first) || !CheckValue(m.first, m.second, &why)) {
      *pAnswer = IsField(m.first) ? why : m.first + " is no field of a status";
      return false;
    }
    StudioJsonPut(rec, m.first, m.second);
  }
  std::string why;
  if (!StudioPut("types", {"statuses", name}, rec, Sender(j), "made the status " + name, &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"status\":" + StatusJson({name, &JsonOf(name), true, StudioModId()}) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool CmdStatusSet(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str(), sender = Sender(j);
  if (!MineStatus(name)) {
    *pAnswer = Exists(name) ? name + " is a status of a mod this one depends on: change it there" : "name is one of the mod's statuses";
    return false;
  }
  const int nAt = StudioHistoryAt();
  std::string why;
  if (j["rename"].kind == PortJson::STRING) {
    const std::string to = j["rename"].s;
    bool bName = !to.empty();
    for (char ch : to)
      bName = bName && (isalnum(static_cast<unsigned char>(ch)) || ch == '_');
    if (!bName || Exists(to)) {
      *pAnswer = bName ? to + " is taken among the statuses" : "rename is letters, digits and _";
      return false;
    }
    PortJson rec = JsonOf(name);
    StudioGroupBegin(sender, "renamed the status " + name + " " + to);
    bool bOk = Repoint(name, to, sender, &why);
    bOk = bOk && StudioPut("types", {"statuses", to}, rec, sender, "the status " + to, &why);
    bOk = bOk && StudioPut("types", {"statuses", name}, PortJson(), sender, "took away the status " + name, &why);
    StudioGroupEnd();
    if (!bOk)
      return Undone(nAt, why, pAnswer);
    if (g_sel == name)
      g_sel = to;
    *pAnswer = "\"status\":" + StatusJson({to, &JsonOf(to), true, StudioModId()}) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
    return true;
  }
  std::vector<std::pair<std::string, PortJson>> sets;
  if (j["values"].kind == PortJson::OBJECT)
    for (const PortJsonMember &m : j["values"].members)
      sets.push_back({m.first, m.second});
  else if (!j["field"].Str().empty())
    sets.push_back({j["field"].Str(), j["value"]});
  if (sets.empty()) {
    *pAnswer = "give field and value, values, or rename";
    return false;
  }
  for (const auto &s : sets)
    if (!IsField(s.first) || !CheckValue(s.first, s.second, &why)) {
      *pAnswer = IsField(s.first) ? why : s.first + " is no field of a status: " + "name, icon, duration, stacking, max_stacks, move_speed, armor, "
                                                                                    "damage_dealt, attack_cooldown, cannot_move, cannot_attack, "
                                                                                    "damage_per_second, life_per_second, overlay, overlay_by_size, "
                                                                                    "start_image, end_image, comment";
      return false;
    }
  const std::string merge = sets.size() == 1 ? "status." + name + "." + sets[0].first : std::string();
  StudioGroupBegin(sender, sets.size() == 1 ? name + "." + sets[0].first + " set" : name + " set", merge);
  bool bOk = true;
  for (const auto &s : sets) {
    const PortJson v = s.second.kind == PortJson::NULLV ? PortJson() : s.second;
    const PortJson &now = s.first.compare(0, 5, "look.") == 0
                              ? JsonOf(name)["look"][s.first.substr(5).c_str()]
                              : JsonOf(name)[s.first.c_str()];
    if (PortJsonWrite(now) == PortJsonWrite(v))
      continue;
    bOk = bOk && StudioPut("types", FieldPath(name, s.first), v, sender, name + "." + s.first, &why);
  }
  StudioGroupEnd();
  if (!bOk)
    return Undone(nAt, why, pAnswer);
  *pAnswer = "\"status\":" + StatusJson({name, &JsonOf(name), true, StudioModId()}) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool CmdStatusDelete(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str(), sender = Sender(j);
  if (!MineStatus(name)) {
    *pAnswer = "name is one of the mod's own statuses";
    return false;
  }
  const std::vector<Use> uses = UsesOf(name);
  const int nAt = StudioHistoryAt();
  std::string why;
  StudioGroupBegin(sender, "deleted the status " + name);
  bool bOk = Repoint(name, std::string(), sender, &why);
  bOk = bOk && StudioPut("types", {"statuses", name}, PortJson(), sender, "took away the status " + name, &why);
  StudioGroupEnd();
  if (!bOk)
    return Undone(nAt, why, pAnswer);
  std::string off;
  for (const Use &u : uses)
    off += std::string(off.empty() ? "" : ",") + "{\"table\":" + Q(StudioTableName(u.t)) + ",\"name\":" + Q(u.record) + "}";
  if (g_sel == name)
    g_sel.clear();
  *pAnswer = "\"deleted\":" + Q(name) + ",\"uses_taken_off\":[" + off + "],\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// The mod's record of a weapon or a unit to change: its own, its edit of a classic row, or an edit made
// for a classic row (in the group open).
bool Changeable(int t, const std::string &name, const std::string &sender, std::string *pKey, std::string *pWhy) {
  StudioRecord r;
  if (!StudioRecordOf(t, name, &r)) {
    *pWhy = name + " is no " + std::string(t == ST_WEAPONS ? "weapon" : "unit");
    return false;
  }
  if (r.kind == SK_OWN || r.kind == SK_EDIT) {
    *pKey = name;
    return true;
  }
  if (r.kind == SK_DEPENDENCY) {
    *pWhy = name + " is a mod's this one depends on: change it there";
    return false;
  }
  const std::string edit = EditOf(t, name);
  if (!edit.empty()) {
    *pKey = edit;
    return true;
  }
  std::string key = name + "_edit";
  for (int i = 2; StudioRecordOf(t, key, &r); ++i)
    key = name + "_edit" + std::to_string(i);
  PortJson rec = StudioJsonObject();
  StudioJsonPut(rec, "edit", StudioJsonString(name));
  if (!StudioCreate(t, key, rec, sender, pWhy))
    return false;
  *pKey = key;
  return true;
}

bool CmdWeaponStatus(const PortJson &j, std::string *pAnswer) {
  const std::string weapon = j["weapon"].Str(), status = j["status"].Str(), sender = Sender(j);
  if (!status.empty() && !Exists(status)) {
    *pAnswer = "status names no status of the mod's (\"\" for none)";
    return false;
  }
  const int nAt = StudioHistoryAt();
  std::string why, key;
  StudioGroupBegin(sender, status.empty() ? "no status on " + weapon : weapon + " puts " + status + " on what it damages");
  bool bOk = Changeable(ST_WEAPONS, weapon, sender, &key, &why);
  if (bOk)
    bOk = status.empty() ? StudioRevertField(ST_WEAPONS, key, "status", sender, &why)
                         : StudioSetField(ST_WEAPONS, key, "status", StudioJsonString(status), sender, std::string(), &why);
  StudioGroupEnd();
  if (!bOk)
    return Undone(nAt, why, pAnswer);
  *pAnswer = "\"weapon\":" + Q(key) + ",\"status\":" + Q(status) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

bool CmdUnitAura(const PortJson &j, std::string *pAnswer) {
  const std::string unit = j["unit"].Str(), sender = Sender(j);
  const bool bRemove = j["remove"].kind == PortJson::BOOL && j["remove"].b;
  PortJson aura = j["aura"];
  if (!bRemove) {
    if (aura.kind != PortJson::OBJECT || !Exists(aura["status"].Str())) {
      *pAnswer = "aura is {status, radius?, targets?, every?, self?, buildings?}, its status one of the mod's; or remove: true";
      return false;
    }
    for (const PortJsonMember &m : aura.members)
      if (m.first != "status" && m.first != "radius" && m.first != "targets" && m.first != "every" && m.first != "self" &&
          m.first != "buildings") {
        *pAnswer = m.first + " is no field of an aura: status, radius, targets, every, self, buildings";
        return false;
      }
    const std::string who = aura["targets"].Str("allies");
    if (who != "allies" && who != "enemies" && who != "own" && who != "all") {
      *pAnswer = "targets is allies, enemies, own or all";
      return false;
    }
  }
  const int nAt = StudioHistoryAt();
  std::string why, key;
  StudioGroupBegin(sender, bRemove ? "no aura on " + unit : unit + "'s aura");
  bool bOk = Changeable(ST_UNITS, unit, sender, &key, &why);
  if (bOk)
    bOk = bRemove ? StudioRevertField(ST_UNITS, key, "aura", sender, &why)
                  : StudioSetField(ST_UNITS, key, "aura", aura, sender, "aura." + key, &why);
  StudioGroupEnd();
  if (!bOk)
    return Undone(nAt, why, pAnswer);
  *pAnswer = "\"unit\":" + Q(key) + ",\"aura\":" + PortJsonWrite(StudioRecordJson(ST_UNITS, key)["aura"]) + ",\"unsaved\":" +
             std::to_string(StudioUnsaved());
  return true;
}

}  // namespace

// ---- what the other areas use (studio_statuses.h) ---------------------------------------------------------------

std::vector<std::string> StudioStatusNames(void) {
  std::vector<std::string> out;
  for (const Status &s : List())
    out.push_back(s.name);
  return out;
}

std::string StudioStatusLabel(const std::string &name) {
  return JsonOf(name)["name"].Str(name.c_str());
}

std::string StudioStatusSentence(const std::string &name) {
  return Exists(name) ? Sentence(JsonOf(name)) : "No status " + name + " in the mod.";
}

bool StudioStatusPick(const char *pszId, const std::string &now, float fW, std::string *pPicked, bool bNone, float fH) {
  bool bPicked = false;
  const std::string show = now.empty() ? std::string(bNone ? "none" : "pick a status") : StudioStatusLabel(now);
  if (StudioSelectBegin(pszId, show.c_str(), fW, fH)) {
    if (bNone && StudioItem("none", now.empty())) {
      pPicked->clear();
      bPicked = true;
    }
    for (const Status &s : List()) {
      const std::string label = (*s.pJson)["name"].Str(s.name.c_str()) + (s.bMine ? "" : "  (" + s.mod + ")");
      if (StudioItem(label.c_str(), s.name == now)) {
        *pPicked = s.name;
        bPicked = true;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Sentence(*s.pJson).c_str());
    }
    if (List().empty())
      StudioItem("the mod has no statuses yet: the Statuses workspace makes them", false, nullptr, false);
    StudioSelectEnd();
  }
  return bPicked;
}

void StudioStatusesShow(const std::string &name) {
  g_sel = name;
  StudioWorkspaceSet(11);
}

bool g_bSheet = false;  // the sheet of every status, rather than the one chosen

// Every status of the mod as a tile with its icon, the way a mod's statuses are read side by side
// rather than one at a time. A status whose look names its kind is drawn in that kind; one that
// leaves the kind to its own fields is drawn with the frame alone and marked, since what decides
// a derived kind lives in the engine (port_modstatus.cpp) and the Studio does not keep a second
// copy of it.
void Sheet(float fW, const std::vector<Status> &all) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 18.0f, pos.x, pos.y, C(TK_HEAD), "Every status");
  ImGui::Dummy(ImVec2(fW, P(28.0f)));
  Wrapped("The icon the readout draws each status with: its kind's glyph, its hue, and a disc where the "
          "status helps the unit or a diamond where it hurts. The kind is the engine's own answer for "
          "that record, the same call a game makes, so a status whose look names a kind and one that "
          "leaves it to its own fields are both drawn as they will be drawn.",
          fW, kStudioFsS, TK_INK2);
  ImGui::Dummy(ImVec2(fW, P(10.0f)));
  const float fTile = P(118.0f), fZ = P(34.0f);
  const int nAcross = std::max(1, static_cast<int>(fW / fTile));
  int n = 0;
  for (const Status &st : all) {
    if (!st.bMine)
      continue;
    const PortJson &j = *st.pJson;
    int nKind = -1;
    bool bGood = false;
    const bool bReads = ModStatusKindOfDef(j, &nKind, &bGood);
    const ImVec2 at(ImGui::GetCursorScreenPos().x + (n % nAcross) * fTile,
                    ImGui::GetCursorScreenPos().y + (n / nAcross) * (fTile * 0.82f));
    // the tile: its icon, its name, and its kind or that its fields decide it
    ImGui::SetCursorScreenPos(at);
    ImGui::PushID(st.name.c_str());
    if (ImGui::InvisibleButton("##tile", ImVec2(fTile - P(8.0f), fTile * 0.82f - P(8.0f))))
      g_sel = st.name, g_bSheet = false;
    const bool bHot = ImGui::IsItemHovered();
    ImGui::PopID();
    if (bHot)
      d->AddRectFilled(at, ImVec2(at.x + fTile - P(8.0f), at.y + fTile * 0.82f - P(8.0f)), C(TK_INK, 0.06f),
                       P(4.0f));
    if (bReads) {
      FramedIcon(d, ImVec2(at.x + (fTile - P(8.0f) - fZ) / 2.0f, at.y + P(8.0f)), fZ, nKind, bGood,
                 SideOrOwn(j["look"]["hue"].Str(j["look"]["colour"].Str("").c_str()), bGood));
    } else {
      // a record the loader will not read: the frame alone, and the tile says so under it
      const ImVec2 o(at.x + (fTile - P(8.0f) - fZ) / 2.0f, at.y + P(8.0f));
      d->AddCircleFilled(ImVec2(o.x + fZ / 2, o.y + fZ / 2), fZ / 2, C(TK_BAD, 0.20f), 20);
      d->AddCircle(ImVec2(o.x + fZ / 2, o.y + fZ / 2), fZ / 2, C(TK_BAD), 20, 1.0f);
      T(d, STUDIO_FONT_TEXT, 11.0f, o.x + fZ / 2 - P(3.0f), o.y + fZ / 2 - P(7.0f), C(TK_BAD), "!");
    }
    const std::string label = j["name"].Str(st.name.c_str());
    const float fLw = TW(STUDIO_FONT_TEXT, 10.5f, label);
    T(d, STUDIO_FONT_TEXT, 10.5f, at.x + (fTile - P(8.0f) - std::min(fLw, fTile - P(16.0f))) / 2.0f,
      at.y + P(12.0f) + fZ, C(TK_INK), label, fTile - P(16.0f));
    const std::string note = bReads ? PortReadoutKindWords(nKind) : "will not read";
    const float fNw = TW(STUDIO_FONT_TEXT, 9.0f, note);
    T(d, STUDIO_FONT_TEXT, 9.0f, at.x + (fTile - P(8.0f) - std::min(fNw, fTile - P(16.0f))) / 2.0f,
      at.y + P(26.0f) + fZ, C(TK_INK3), note, fTile - P(16.0f));
    ++n;
  }
  const int nRows = (n + nAcross - 1) / nAcross;
  ImGui::Dummy(ImVec2(fW, nRows * fTile * 0.82f + P(10.0f)));
  if (n == 0) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    T(d, STUDIO_FONT_TEXT, kStudioFsS, at.x, at.y, C(TK_INK3), "The mod has no statuses yet.");
    ImGui::Dummy(ImVec2(fW, P(22.0f)));
  }
}

void StudioStatusesWorkspace(ImVec2 a, ImVec2 b) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float hh = P(44.0f);
  d->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + hh), C(TK_BAR));
  d->AddLine(ImVec2(a.x, a.y + hh - 0.5f), ImVec2(b.x, a.y + hh - 0.5f), C(TK_LINE));
  float x = a.x + P(14.0f);
  x += Wide(d, 13.0f, x, Mid(a.y, hh, 13.0f), C(TK_HEAD), "Statuses", 0.06f) + P(16.0f);
  if (!StudioIsOpen()) {
    T(d, STUDIO_FONT_TEXT, kStudioFsS, a.x + P(20.0f), a.y + hh + P(20.0f), C(TK_INK3), "Open a mod to make its statuses.");
    return;
  }
  const std::vector<Status> all = List();
  if ((g_sel.empty() || !Exists(g_sel)) && !all.empty())
    g_sel = all[0].name;
  static char s_szFind[48];
  ImGui::SetCursorScreenPos(ImVec2(x, a.y + (hh - P(26.0f)) / 2.0f));
  StudioInput("##stfind", s_szFind, sizeof s_szFind, 220.0f, 26.0f, "Filter", "search");
  ImGui::SameLine(0.0f, P(10.0f));
  if (StudioButton("##stsheet", "Sheet", "grid", g_bSheet ? SB_ON : SB_FLAT, 26.0f))
    g_bSheet = !g_bSheet;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Every status with the icon the readout draws it with");
  // New, Duplicate and Delete at the right
  const bool bMine = MineStatus(g_sel);
  const float wDel = StudioButtonSize("Delete", "x", 28.0f).x, wDup = StudioButtonSize("Duplicate", "copy", 28.0f).x;
  const float wNew = StudioButtonSize("New status", "plus", 28.0f).x;
  float bx = b.x - P(14.0f) - wDel;
  const float by = a.y + (hh - P(28.0f)) / 2.0f;
  ImGui::SetCursorScreenPos(ImVec2(bx, by));
  if (StudioButton("##stdel", "Delete", "x", SB_PLAIN, 28.0f, 0.0f, kStudioFsS, bMine))
    StudioAsk("studio_status_delete", "\"name\":" + Q(g_sel));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The status and every use of it (weapons, auras, blocks), as one edit: Undo puts them back");
  bx -= P(8.0f) + wDup;
  ImGui::SetCursorScreenPos(ImVec2(bx, by));
  if (StudioButton("##stdup", "Duplicate", "copy", SB_PLAIN, 28.0f, 0.0f, kStudioFsS, Exists(g_sel)))
    NewStatus(g_sel, StudioStatusLabel(g_sel) + " copy");
  bx -= P(8.0f) + wNew;
  ImGui::SetCursorScreenPos(ImVec2(bx, by));
  if (StudioButton("##stnew", "New status", "plus", SB_GOOD, 28.0f))
    ImGui::OpenPopup("##stkinds");
  if (ImGui::BeginPopup("##stkinds")) {
    StudioLabel(STUDIO_FONT_WIDE, 9.5f, C(TK_INK3), "Start from", 0.1f, true);
    for (const Kind &k : kKinds) {
      PortJson rec;
      PortJsonParse(("{" + std::string(k.pszFields) + "}").c_str(), &rec);
      const std::string item = std::string(k.pszLabel) + ": " + Sentence(rec);
      if (StudioItem(item.c_str()))
        NewStatus(k.pszKey, k.pszLabel);
    }
    ImGui::EndPopup();
  }
  // the list
  const float lw = std::min(P(340.0f), (b.x - a.x) * 0.34f);
  const ImVec2 la(a.x + 1, a.y + hh), lb(a.x + lw, b.y - 1);
  d->AddLine(ImVec2(lb.x + 0.5f, la.y), ImVec2(lb.x + 0.5f, lb.y), C(TK_LINE));
  ImGui::SetCursorScreenPos(la);
  if (ImGui::BeginChild("##stlist", ImVec2(lb.x - la.x, lb.y - la.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    ImGui::SetCursorPos(ImVec2(P(10.0f), P(10.0f)));
    ImGui::BeginGroup();
    const float fW = lw - P(24.0f);
    int nShown = 0, nOwn = 0;
    for (const Status &s : all)
      nOwn += s.bMine;
    Lab(("The mod's statuses  " + std::to_string(nOwn)).c_str());
    bool bDeps = false;
    for (const Status &s : all) {
      if (!Has(s.name, s_szFind) && !Has((*s.pJson)["name"].Str(), s_szFind) && !Has(Sentence(*s.pJson), s_szFind))
        continue;
      if (!s.bMine && !bDeps) {
        ImGui::Dummy(ImVec2(1, P(6.0f)));
        Lab("From the mods it depends on");
        bDeps = true;
      }
      ++nShown;
      ImGui::PushID(s.name.c_str());
      if (ListRow("##st", (*s.pJson)["name"].Str(s.name.c_str()), Sentence(*s.pJson), g_sel == s.name, s.bMine, fW))
        g_sel = s.name;
      ImGui::PopID();
    }
    if (nShown == 0)
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3), all.empty() ? "None yet: New status makes one." : "None match.");
    ImGui::EndGroup();
  }
  ImGui::EndChild();
  // the status chosen
  ImGui::SetCursorScreenPos(ImVec2(lb.x + 1, la.y));
  if (ImGui::BeginChild("##stright", ImVec2(b.x - lb.x - 2, b.y - la.y - 1), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    const float rw = std::min(ImGui::GetContentRegionAvail().x - P(40.0f), P(880.0f));
    ImGui::SetCursorPos(ImVec2(P(20.0f), P(16.0f)));
    ImGui::BeginGroup();
    if (g_bSheet)
      Sheet(rw, all);
    else if (Exists(g_sel))
      Editor(g_sel, rw);
    else
      Wrapped("A status is a thing a unit has for a while: slowed, poisoned, stunned, hastened. Make one with New status, then put it on a "
              "weapon, give it as a unit's aura, or apply it with an action's block.",
              rw, kStudioFsS, TK_INK3);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, P(24.0f)));
  }
  ImGui::EndChild();
}

void StudioStatusesInit(void) {
  PortControlCommand("studio_statuses", CmdStatuses);
  PortControlCommand("studio_status_new", CmdStatusNew);
  PortControlCommand("studio_status_set", CmdStatusSet);
  PortControlCommand("studio_status_delete", CmdStatusDelete);
  PortControlCommand("studio_weapon_status", CmdWeaponStatus);
  PortControlCommand("studio_unit_aura", CmdUnitAura);
}
