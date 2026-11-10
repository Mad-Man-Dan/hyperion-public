// studio_voice.cpp -- how each sound of a unit's line plays, in plain words, in the unit's Sounds
// view: its subtitle (the words shown as the unit says it, docs/mod-data.md, Sounds), its priority
// against the other sounds playing, the least time before it plays again, and whether it is cut
// off, said once at a time, or one at a time for its unit type. A classic sound is changed through
// an edit of it, made with the first change, so every unit that says it changes too.
//
//   studio_sound_set {sound, field, value | revert, merge?}   one of a sound's fields, an edit made for a classic one
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_voice.h"

namespace {

using namespace studio_draw;

// sfxdata.dat's flags that change how a voice plays (port_moddata.cpp's kSoundFlags)
const int kOnePerUnit = 0x02, kOneAtATime = 0x10, kUninterrupted = 0x20;

const char *const kFields[] = {"subtitle", "priority", "flags", "min_interval", "length_adjust", "min_volume"};

// The mod's record of a sound: its own, its edit of the classic row, or "" for none yet.
std::string RecordOf(const std::string &sound) {
  StudioRecord r;
  if (StudioRecordOf(ST_SOUNDS, sound, &r) && r.kind != SK_CLASSIC)
    return r.kind == SK_DEPENDENCY ? std::string() : sound;
  const int nRow = StudioClassicRow(ST_SOUNDS, sound);
  for (const StudioRecord &e : StudioRecords(ST_SOUNDS)) {
    if (e.kind != SK_EDIT)
      continue;
    const PortJson &v = StudioRecordJson(ST_SOUNDS, e.name)["edit"];
    if ((v.kind == PortJson::STRING && v.s == sound) || (v.kind == PortJson::NUMBER && static_cast<int>(v.d) == nRow))
      return e.name;
  }
  return std::string();
}

// What the sound has now: a number field, and its subtitle.
long long Number(const std::string &sound, const char *pszField) {
  const std::string rec = RecordOf(sound);
  return StudioNumber(ST_SOUNDS, rec.empty() ? sound : rec, pszField, 0);
}

std::string Subtitle(const std::string &sound) {
  const std::string rec = RecordOf(sound);
  return StudioRecordJson(ST_SOUNDS, rec.empty() ? sound : rec)["subtitle"].Str();
}

long long Gap(const std::string &sound) {
  const std::string rec = RecordOf(sound);
  const PortJson &v = StudioRecordJson(ST_SOUNDS, rec.empty() ? sound : rec)["min_interval"];
  return v.kind == PortJson::NUMBER ? static_cast<long long>(v.d) : -1;
}

bool SetSound(const std::string &sound, const std::string &field, const PortJson &value, bool bRevert, const std::string &merge,
              const std::string &sender, std::string *pWhy) {
  if (std::find(std::begin(kFields), std::end(kFields), field) == std::end(kFields)) {
    *pWhy = "field is one of subtitle, priority, flags, min_interval, length_adjust, min_volume";
    return false;
  }
  StudioRecord r;
  if (!StudioRecordOf(ST_SOUNDS, sound, &r)) {
    *pWhy = sound + " is no sound";
    return false;
  }
  if (r.kind == SK_DEPENDENCY) {
    *pWhy = sound + " is a mod's this one depends on: change it there";
    return false;
  }
  std::string rec = RecordOf(sound);
  if (bRevert)
    return rec.empty() || StudioRevertField(ST_SOUNDS, rec, field, sender, pWhy);
  if (field == "subtitle" ? value.kind != PortJson::STRING : value.kind != PortJson::NUMBER) {
    *pWhy = field == "subtitle" ? "a subtitle is a text" : field + " is a number";
    return false;
  }
  if (rec.empty()) {
    // a classic sound changed through an edit of it, in the same undo as the change
    rec = sound + "_edit";
    for (int i = 2; StudioRecordOf(ST_SOUNDS, rec, &r); ++i)
      rec = sound + "_edit" + std::to_string(i);
    PortJson e = StudioJsonObject();
    StudioJsonPut(e, "edit", StudioJsonString(sound));
    StudioJsonPut(e, field, value);
    return StudioCreate(ST_SOUNDS, rec, e, sender, pWhy);
  }
  return StudioSetField(ST_SOUNDS, rec, field, value, sender, merge, pWhy);
}

bool CmdSoundSet(const PortJson &j, std::string *pAnswer) {
  const std::string sound = j["sound"].Str(), field = j["field"].Str();
  if (!SetSound(sound, field, j["value"], j["revert"].kind == PortJson::BOOL && j["revert"].b, j["merge"].Str(), j["sender"].Str("claude"),
                pAnswer))
    return false;
  const std::string rec = RecordOf(sound);
  *pAnswer = "\"sound\":" + Q(sound) + ",\"record\":" + Q(rec) + ",\"subtitle\":" + Q(Subtitle(sound)) + ",\"priority\":" +
             std::to_string(Number(sound, "priority")) + ",\"flags\":" + std::to_string(Number(sound, "flags")) +
             ",\"min_interval\":" + std::to_string(Gap(sound)) + ",\"length_adjust\":" + std::to_string(Number(sound, "length_adjust")) +
             ",\"min_volume\":" + std::to_string(Number(sound, "min_volume"));
  return true;
}

void Ask(const std::string &sound, const char *pszField, const std::string &value, bool bMerge) {
  StudioAsk("studio_sound_set", "\"sound\":" + Q(sound) + ",\"field\":\"" + pszField + "\",\"value\":" + value +
                                    (bMerge ? ",\"merge\":" + Q(std::string("voice:") + sound + ":" + pszField) : std::string()));
}

std::string PriorityWords(long long n) {
  if (n <= 0)
    return "gives way to every sound";
  return "with eight sounds playing, it stops one below " + std::to_string(n) + " to be heard";
}

// A flag as a small check: its words, its tip; on the next line when the row is full.
void Flag(const std::string &sound, long long nFlags, int nBit, const char *pszLabel, const char *pszTip, bool bMine, float *pX, float *pY,
          float x0, float right) {
  bool bOn = (nFlags & nBit) != 0;
  if (*pX + TW(STUDIO_FONT_TEXT, kStudioFsS, pszLabel) + P(34.0f) > right && *pX > x0) {
    *pX = x0;
    *pY += P(28.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(*pX, *pY));
  ImGui::BeginDisabled(!bMine);
  if (StudioCheck((std::string("##f") + std::to_string(nBit)).c_str(), pszLabel, &bOn))
    Ask(sound, "flags", std::to_string(bOn ? (nFlags | nBit) : (nFlags & ~static_cast<long long>(nBit))), false);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", pszTip);
  *pX = ImGui::GetItemRectMax().x + P(14.0f);
}

std::map<std::string, std::string> g_typing;  // a subtitle being typed, by sound

}  // namespace

float StudioSoundRows(const std::vector<std::string> &sounds, bool bMine, float x0, float y, float right) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = right - x0;
  for (const std::string &sound : sounds) {
    if (sound.empty())
      continue;
    ImGui::PushID(("voice:" + sound).c_str());
    StudioRecord r;
    StudioRecordOf(ST_SOUNDS, sound, &r);
    const bool bCan = bMine && r.kind != SK_DEPENDENCY;
    const std::string rec = RecordOf(sound);
    d->AddLine(ImVec2(x0, y - P(4.0f)), ImVec2(right, y - P(4.0f)), C(TK_ROW));
    // its name, then its subtitle
    T(d, STUDIO_FONT_MONO, kStudioFsXs, x0, y + P(5.0f), C(TK_INK2), Short(ST_SOUNDS, sound), P(150.0f));
    const float ix = x0 + P(156.0f), iw = W - P(156.0f);
    char buf[256];
    auto it = g_typing.find(sound);
    snprintf(buf, sizeof buf, "%s", it != g_typing.end() ? it->second.c_str() : Subtitle(sound).c_str());
    ImGui::SetCursorScreenPos(ImVec2(ix, y));
    ImGui::BeginDisabled(!bCan);
    const bool bChanged = StudioInput("##sub", buf, sizeof buf, iw / P(1.0f), 24.0f, "Subtitle: the words, as a player reads them");
    ImGui::EndDisabled();
    if (ImGui::IsItemActive())
      g_typing[sound] = buf;
    else
      g_typing.erase(sound);
    if (bChanged && bCan)
      Ask(sound, "subtitle", Q(buf), true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Shown on the message line as the unit says it, to a player with Settings' Unit subtitles on;\n"
                        "in the player's language through the mod's languages (Strings). %s",
                        rec.empty() && bCan ? "A classic sound gets an edit of it, so each unit that says it shows it." : "");
    y += P(28.0f);
    // how it plays: priority, the gap, the flags
    float x = x0 + P(156.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(y, P(24.0f), kStudioFsXs), C(TK_LABEL), "Priority");
    x += P(48.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    double dp = static_cast<double>(Number(sound, "priority"));
    ImGui::BeginDisabled(!bCan);
    if (StudioNumber("##pri", &dp, 46.0f, 24.0f, !rec.empty(), 12.0f) && bCan)
      Ask(sound, "priority", std::to_string(std::max(0, std::min(255, static_cast<int>(dp)))), false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("0 to 255. %s; a sound marked not cut off keeps playing whatever comes.\nThe classic voices are 1.",
                        PriorityWords(static_cast<long long>(dp)).c_str());
    x += P(54.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(y, P(24.0f), kStudioFsXs), C(TK_LABEL), "again after");
    x += P(66.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const long long nGap = Gap(sound);
    double dg = nGap >= 0 ? static_cast<double>(nGap) : 80.0;
    ImGui::BeginDisabled(!bCan);
    if (StudioNumber("##gap", &dg, 58.0f, 24.0f, nGap >= 0, 12.0f) && bCan)
      Ask(sound, "min_interval", std::to_string(std::max(0, std::min(600000, static_cast<int>(dg)))), false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Milliseconds before this sound plays again; asked sooner, it is not heard. 80, as the classic game, when not given");
    x += P(62.0f);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, Mid(y, P(24.0f), kStudioFsXs), C(TK_INK3), "ms");
    x += P(26.0f);
    const long long nFlags = Number(sound, "flags");
    const float fx = x0 + P(156.0f);
    Flag(sound, nFlags, kUninterrupted, "not cut off", "A sound of higher priority does not stop it", bCan, &x, &y, fx, right);
    Flag(sound, nFlags, kOnePerUnit, "one per unit type", "While a unit of the type says one of its sounds, it says no other", bCan, &x, &y, fx,
         right);
    Flag(sound, nFlags, kOneAtATime, "once at a time", "While it plays, it does not start again", bCan, &x, &y, fx, right);
    ImGui::PopID();
    y += P(32.0f);
  }
  return y;
}

void StudioVoiceInit(void) {
  PortControlCommand("studio_sound_set", CmdSoundSet);
}
