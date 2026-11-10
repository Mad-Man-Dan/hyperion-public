// studio_sounds.cpp -- a unit's Sounds view, and a sound's and a portrait's heads: what a unit says
// (its ready line, and the ranges of sounds it says when selected, annoyed and given an order),
// each sound played as the game decodes it; a classic unit's voice borrowed whole; sounds of the
// mod's own made from files brought in; the unit's portrait, and a portrait's videos brought in
// under the names the loader looks for (docs/mod-data.md, "Sounds and portraits").
//
// Commands of its own:
//   studio_play {name | file}                     a sound record, or a file of the data folder, played once
//   studio_unit_sounds {unit, from}               a unit's voice and portrait borrowed from another unit
//   studio_unit_sound_add {unit, line, from}      a file brought in as a new sound at the end of a line
//   studio_unit_sound_remove {unit, line, sound}  a sound of the unit's own taken out of a line
//   studio_unit_portrait {unit}                   a portrait of the unit's own made and given to it
//   studio_portrait_video {portrait, kind, n, from}   a video brought in as idle or talking video n
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"
#include "studio_media.h"
#include "studio_voice.h"

namespace {

using namespace studio_draw;

// A unit's lines: the field of its first sound and of its last (none for the single ready line),
// the word its own sounds are named by, and when the game says it.
struct Line {
  const char *pszLabel;
  const char *pszFirst;
  const char *pszLast;
  const char *pszWord;
  const char *pszWhen;
};
const Line kLines[] = {
    {"Ready", "ready_sound", nullptr, "ready", "once, when it is made"},
    {"Selected", "what_sound_first", "what_sound_last", "what", "at random, when selected"},
    {"Annoyed", "pissed_sound_first", "pissed_sound_last", "pissed", "in turn, clicked on and on"},
    {"Acknowledged", "yes_sound_first", "yes_sound_last", "yes", "at random, when given an order"},
};
const int kLineCount = 4;

const Line *LineOf(const std::string &word) {
  for (const Line &l : kLines)
    if (word == l.pszWord || Lower(word) == Lower(l.pszLabel))
      return &l;
  return nullptr;
}

std::string Two(int n) {
  char sz[8];
  snprintf(sz, sizeof sz, "%02d", n);
  return sz;
}

bool Mine(int t, const std::string &name) {
  StudioRecord r;
  return StudioRecordOf(t, name, &r) && (r.kind == SK_OWN || r.kind == SK_EDIT);
}

// A sound record by its row, "" for none (row 0, "none", is the classic table's empty sound).
std::string SoundAt(long long n) {
  if (n <= 0)
    return std::string();
  return StudioRefName(ST_SOUNDS, n);
}

// The sounds of a line, first to last, by their rows; one for the ready line.
std::vector<std::string> LineSounds(const std::string &unit, const Line &l) {
  std::vector<std::string> out;
  StudioValue a, b;
  if (!StudioGet(ST_UNITS, unit, l.pszFirst, &a) || a.n.empty())
    return out;
  long long nFirst = a.n[0], nLast = nFirst;
  if (l.pszLast && StudioGet(ST_UNITS, unit, l.pszLast, &b) && !b.n.empty())
    nLast = b.n[0];
  if (nFirst <= 0)
    return out;
  if (nLast < nFirst)
    std::swap(nFirst, nLast);  // a range the classic file gives backwards, as the Ultralisk's
  for (long long n = nFirst; n <= nLast && out.size() < 64; ++n)
    out.push_back(SoundAt(n));
  return out;
}

// Whether a unit has its sound columns: the classic files keep them for the units below 106 only.
bool HasLines(const std::string &unit) {
  StudioValue v;
  return StudioGet(ST_UNITS, unit, "ready_sound", &v) && !v.n.empty();
}

// The mod's own sounds of a unit's line, named <unit>_<word>_NN, in their order.
std::vector<std::string> OwnLineSounds(const std::string &unit, const Line &l) {
  std::vector<std::string> out;
  const std::string prefix = unit + "_" + l.pszWord + "_";
  for (const StudioRecord &r : StudioRecords(ST_SOUNDS))
    if (r.kind == SK_OWN && r.name.compare(0, prefix.size(), prefix) == 0)
      out.push_back(r.name);
  std::sort(out.begin(), out.end());
  return out;
}

// A sound shown short: its file's name without its folders and end, else its record's name.
std::string SoundWord(const std::string &sound) {
  if (sound.empty())
    return "none";
  StudioValue v;
  if (StudioGet(ST_SOUNDS, sound, "file", &v) && v.own.kind == PortJson::STRING && !v.own.s.empty()) {
    std::string s = v.own.s;
    const size_t cut = s.find_last_of("/\\");
    if (cut != std::string::npos)
      s = s.substr(cut + 1);
    const size_t dot = s.find_last_of('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
  }
  return StudioShowName(ST_SOUNDS, sound);
}

// The units whose lines say a sound, the mod's first, looked for again when the mod changes.
const std::vector<std::string> &SoundUsers(const std::string &sound, long long nRow) {
  static std::vector<std::string> s_list;
  static std::string s_sound;
  static unsigned s_nChange = ~0u;
  if (s_nChange == StudioChange() && s_sound == sound)
    return s_list;
  s_list.clear();
  s_sound = sound;
  s_nChange = StudioChange();
  auto check = [&](const std::string &unit) {
    for (const Line &l : kLines) {
      const long long a = StudioNumber(ST_UNITS, unit, l.pszFirst, -1);
      const long long b = l.pszLast ? StudioNumber(ST_UNITS, unit, l.pszLast, -1) : a;
      if (a > 0 && nRow >= a && nRow <= b) {
        s_list.push_back(unit);
        return;
      }
    }
  };
  for (const StudioRecord &r : StudioRecords(ST_UNITS))
    if (r.kind != SK_EDIT)
      check(r.name);
  for (int n = 0; n < StudioClassicRows(ST_UNITS) && n < 106; ++n)
    if (const char *psz = StudioClassicName(ST_UNITS, n))
      check(psz);
  return s_list;
}

const std::vector<std::string> &PortraitUsers(long long nRow) {
  static std::vector<std::string> s_list;
  static long long s_nRow = -2;
  static unsigned s_nChange = ~0u;
  if (s_nChange == StudioChange() && s_nRow == nRow)
    return s_list;
  s_list.clear();
  s_nRow = nRow;
  s_nChange = StudioChange();
  for (const StudioRecord &r : StudioRecords(ST_UNITS))
    if (r.kind != SK_EDIT && StudioNumber(ST_UNITS, r.name, "portrait", -1) == nRow)
      s_list.push_back(r.name);
  for (int n = 0; n < StudioClassicRows(ST_UNITS); ++n)
    if (const char *psz = StudioClassicName(ST_UNITS, n))
      if (StudioNumber(ST_UNITS, psz, "portrait", -1) == nRow)
        s_list.push_back(psz);
  return s_list;
}

// Buttons that open units, laid out left to right after a label, wrapping; the cursor below.
void UnitButtons(const char *pszLabel, const std::vector<std::string> &units, float fW, int nView) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  Wide(d, 9.5f, o.x, o.y + P(5.0f), C(TK_INK3), pszLabel, 0.1f);
  const float x0 = o.x + std::max(P(78.0f), TW(STUDIO_FONT_WIDE, 9.5f, pszLabel, 0.1f, true) + P(12.0f));
  float x = x0, y = o.y;
  if (units.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(2.0f), C(TK_INK3), "no unit yet");
  int nShown = 0;
  for (const std::string &u : units) {
    const std::string label = StudioShowName(ST_UNITS, u);
    const float bw = StudioButtonSize(label.c_str(), "unit", 24.0f, kStudioFsXs).x;
    if (x + bw > o.x + fW && x > x0) {
      x = x0;
      y += P(30.0f);
    }
    if (++nShown > 16) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y + P(5.0f), C(TK_INK3), "and " + std::to_string(units.size() - 16) + " more");
      break;
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushID(u.c_str());
    if (StudioButton("##user", label.c_str(), "unit", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioOpenDoc(ST_UNITS, u, nView);
    ImGui::PopID();
    x += bw + P(6.0f);
  }
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  ImGui::Dummy(ImVec2(1, P(34.0f)));
}

// A title, and chips of what the record is.
void Title(int t, const std::string &name, const StudioRecord &rec, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 24.0f, o.x, o.y, C(TK_HI), StudioShowName(t, name), fW);
  float cx = o.x;
  const float y = o.y + P(36.0f);
  if (rec.kind == SK_CLASSIC)
    cx += StudioChip(d, ImVec2(cx, y), "classic, read only", CH_GOLD) + P(6.0f);
  else if (rec.kind == SK_DEPENDENCY)
    cx += StudioChip(d, ImVec2(cx, y), ("from " + rec.mod + ", read only").c_str(), CH_GOLD) + P(6.0f);
  else if (rec.kind == SK_EDIT)
    cx += StudioChip(d, ImVec2(cx, y), "an edit of a classic row", CH_CHANGED) + P(6.0f);
  if (!rec.base.empty())
    cx += StudioChip(d, ImVec2(cx, y), ("based on " + StudioShowName(t, rec.base)).c_str()) + P(6.0f);
  if (StudioChangedSinceSave(t, name) > 0)
    StudioChip(d, ImVec2(cx, y), "changed since the save", CH_CHANGED);
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(1, y + P(30.0f) - o.y));
}

std::string FlagWords(long long n) {
  static const char *const kWords[] = {"kept loaded", "a unit type says one at a time", "", "", "plays once at a time",
                                       "not cut off"};
  std::string s;
  for (int i = 0; i < 6; ++i)
    if (((n >> i) & 1) && kWords[i][0])
      s += (s.empty() ? "" : "; ") + std::string(kWords[i]);
  return s.empty() ? "no flags" : s;
}

void SoundHead(const std::string &name, const StudioRecord &rec) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = ImGui::GetContentRegionAvail().x - P(44.0f);
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(16.0f);
  ImGui::SetCursorScreenPos(o);
  ImGui::BeginGroup();
  Title(ST_SOUNDS, name, rec, W);
  // the play button, large, and what the file is beside it
  const StudioSoundWhere w = StudioSoundFile(name);
  float y = ImGui::GetCursorScreenPos().y;
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  const bool bNow = StudioSoundPlaying(nullptr) == name;
  if (StudioButton("##play", bNow ? "Playing" : "Play", "play", bNow ? SB_ON : SB_GOOD, 34.0f, 0.0f, 14.0f, w.bThere)) {
    StudioSoundInfo info;
    std::string why;
    if (!StudioPlaySound(name, &info, &why))
      StudioSay(why);
  }
  const float x = o.x + StudioButtonSize("Playing", "play", 34.0f, 14.0f).x + P(16.0f);
  StudioSoundInfo info;
  std::string where = w.shown.empty() ? std::string("It names no file, so it plays nothing.")
                      : !w.mpq.empty() ? "sound\\" + w.shown + " in the classic game's files"
                                       : w.shown + " in " + (rec.kind == SK_DEPENDENCY ? rec.mod + "'s" : std::string("the mod's")) + " data folder";
  T(d, STUDIO_FONT_TEXT, 14.0f, x, y, C(TK_INK), where, o.x + W - x);
  std::string what;
  if (!w.shown.empty() && !w.bThere)
    what = "the file is not there: the game plays nothing for it";
  else if (StudioSoundInfoOf(name, &info))
    what = info.kind + ", " + Fmt(info.dSeconds, 2) + " s, " + std::to_string(info.nRate) + " Hz, " +
           (info.nChannels == 1 ? std::string("mono") : info.nChannels == 2 ? std::string("stereo") : std::to_string(info.nChannels) + " channels");
  else if (!w.shown.empty())
    what = "it does not decode: a sound is a WAV, Ogg (Vorbis or Opus), MP3 or FLAC file";
  T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(19.0f), !w.shown.empty() && (!w.bThere || info.nRate == 0) ? C(TK_WARN) : C(TK_INK2), what,
    o.x + W - x);
  y += P(48.0f);
  const long long nFlags = StudioNumber(ST_SOUNDS, name, "flags");
  const long long nPri = StudioNumber(ST_SOUNDS, name, "priority"), nVol = StudioNumber(ST_SOUNDS, name, "min_volume");
  T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x, y, C(TK_INK2),
    "Priority " + std::to_string(nPri) + ", " + FlagWords(nFlags) + (nVol > 0 ? ", never quieter than " + std::to_string(nVol) + "%" : std::string()) + ".",
    W);
  y += P(28.0f);
  ImGui::SetCursorScreenPos(ImVec2(o.x, y));
  UnitButtons("Said by", SoundUsers(name, rec.nId), W, 5);
  y = ImGui::GetCursorScreenPos().y;
  ImGui::EndGroup();
  d->AddLine(ImVec2(o.x, y), ImVec2(o.x + W, y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

// ---- a portrait's videos ------------------------------------------------------------------------------

// The path of a portrait's videos without their number and end: its own, or one made for it.
std::string VideoBase(const std::string &portrait, const char *pszKey) {
  StudioValue v;
  if (StudioGet(ST_PORTRAITS, portrait, pszKey, &v) && v.own.kind == PortJson::STRING)
    return v.own.s;
  return std::string();
}

std::string g_videoFor;  // the slot the system's dialog is open for: portrait, kind, number

// The video a slot plays, as the game finds it: a classic name in its files, or a path in the data
// folder, the one numbered below it when it has none of its own (*pnKind 1), "" when none plays
// (*pnKind 2). *pShown is what the row says.
std::string SlotVideo(const std::string &portrait, const char *pszKey, int n, bool bClassic, int *pnKind, std::string *pShown) {
  const std::string base = VideoBase(portrait, pszKey);
  *pnKind = 0;
  if (base.empty()) {
    *pShown = "none";
    *pnKind = 2;
    return std::string();
  }
  if (bClassic) {
    // the game puts the number after the classic name ("tmarine\TMaFid0" gives TMaFid00.smk to TMaFid03.smk)
    *pShown = "portrait\\" + base + std::to_string(n) + ".smk";
    return *pShown;
  }
  const std::string own = StudioFirstVideo(base + std::to_string(n));
  if (!own.empty()) {
    *pShown = own;
    return own;
  }
  std::string below;
  for (int k = n - 1; k >= 0 && below.empty(); --k)
    below = StudioFirstVideo(base + std::to_string(k));
  *pShown = below.empty() ? "none: " + base + std::to_string(n) + ".webm is not there" : "plays " + below + " in its place";
  *pnKind = below.empty() ? 2 : 1;
  return below;
}

// The slot each portrait's page shows in its preview: "idle#0" until another is picked.
std::map<std::string, std::string> g_slotShown;

std::string ShownSlot(const std::string &portrait) {
  auto it = g_slotShown.find(portrait);
  return it == g_slotShown.end() ? std::string("idle#0") : it->second;
}

void VideoSlot(const std::string &portrait, const char *pszKey, int n, bool bClassic, bool bMine, float fW) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const float h = P(30.0f);
  const std::string label = (strcmp(pszKey, "idle") == 0 ? "Idle " : "Talking ") + std::to_string(n);
  const std::string slot = std::string(pszKey) + "#" + std::to_string(n);
  int nKind = 0;  // 0 there, 1 falls to another, 2 none
  std::string shown;
  const std::string video = SlotVideo(portrait, pszKey, n, bClassic, &nKind, &shown);
  // the row picks the slot the preview shows
  const bool bShown = ShownSlot(portrait) == slot;
  ImGui::PushID(slot.c_str());
  ImGui::SetCursorScreenPos(o);
  if (ImGui::InvisibleButton("##show", ImVec2(std::max(P(40.0f), fW - P(260.0f)), h)) && !video.empty())
    g_slotShown[portrait] = slot;
  const bool bHover = ImGui::IsItemHovered() && !video.empty();
  ImGui::PopID();
  if (bShown || bHover)
    d->AddRectFilled(ImVec2(o.x - P(6.0f), o.y + P(1.0f)), ImVec2(o.x + fW - P(250.0f), o.y + h - P(1.0f)), bShown ? C(TK_SEL) : C(TK_ROW_HI),
                     P(3.0f));
  if (bHover)
    ImGui::SetTooltip("Show it in the preview");
  if (!video.empty())
    StudioGlyph(d, "play", ImVec2(o.x, Mid(o.y, h, 12.0f)), P(12.0f), bShown ? C(TK_HI) : C(TK_INK3));
  T(d, STUDIO_FONT_TEXT, kStudioFsS, o.x + P(20.0f), Mid(o.y, h, kStudioFsS), bShown ? C(TK_HI) : C(TK_INK), label);
  const float x = o.x + P(100.0f);
  T(d, STUDIO_FONT_MONO, kStudioFsXs, x, Mid(o.y, h, kStudioFsXs), nKind == 2 ? C(TK_INK3) : nKind == 1 ? C(TK_INK2) : C(TK_INK), shown,
    fW - P(360.0f));
  if (bMine) {
    const std::string id = portrait + "#" + pszKey + "#" + std::to_string(n);
    ImGui::SetCursorScreenPos(ImVec2(o.x + fW - P(150.0f), o.y + P(3.0f)));
    ImGui::PushID(id.c_str());
    if (StudioButton("##bring", "Bring in a video", "plus", SB_PLAIN, 24.0f, 150.0f, kStudioFsXs)) {
      g_videoFor = id;
      StudioPickFile(("video:" + id).c_str(), "Videos", "webm,mkv,mp4,m4v,mov,smk");
    }
    ImGui::PopID();
    std::string path;
    if (StudioPickedFile(("video:" + id).c_str(), &path))
      StudioAsk("studio_portrait_video", "\"portrait\":" + Q(portrait) + ",\"kind\":" + Q(pszKey) + ",\"n\":" + std::to_string(n) +
                                             ",\"from\":" + Q(path));
  }
  d->AddLine(ImVec2(o.x, o.y + h - 0.5f), ImVec2(o.x + fW, o.y + h - 0.5f), C(TK_ROW));
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(1, h));
}

// The slot picked, playing in a box of three times the console's 60 by 56, and what it is beside it.
void PortraitPreview(const std::string &portrait, bool bClassic, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const float bw = P(180.0f), bh = P(168.0f);
  const std::string slot = ShownSlot(portrait);
  const size_t hash = slot.find('#');
  const std::string key = slot.substr(0, hash);
  const int n = atoi(slot.c_str() + hash + 1);
  int nKind = 0;
  std::string shown;
  const std::string video = SlotVideo(portrait, key.c_str(), n, bClassic, &nKind, &shown);
  d->AddRectFilled(o, ImVec2(o.x + bw, o.y + bh), C(TK_BLACK), P(4.0f));
  StudioVideoState st;
  const bool bOn =
      StudioVideoDraw(video, bClassic, ImVec2(o.x + P(4.0f), o.y + P(4.0f)), ImVec2(o.x + bw - P(4.0f), o.y + bh - P(4.0f)), &st, true);
  if (!bOn)
    StudioGlyph(d, "image", ImVec2(o.x + bw / 2.0f - P(12.0f), o.y + bh / 2.0f - P(12.0f)), P(24.0f), C(TK_INK3));
  d->AddRect(o, ImVec2(o.x + bw, o.y + bh), C(TK_LINE2), P(4.0f));
  const float x = o.x + bw + P(18.0f), tw = W - bw - P(20.0f);
  float y = o.y + P(4.0f);
  T(d, STUDIO_FONT_BOLD, 15.0f, x, y, C(TK_INK), (key == "idle" ? "Idle " : "Talking ") + std::to_string(n));
  y += P(26.0f);
  if (bOn) {
    T(d, STUDIO_FONT_MONO, kStudioFsXs, x, y, C(TK_INK2), st.from, tw);
    y += P(22.0f);
    char sz[160];
    snprintf(sz, sizeof sz, "%d by %d, %u frames, %.1f seconds%s", st.w, st.h, st.nFrames, st.dSeconds, st.bSmacker ? ", Smacker" : "");
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y, C(TK_INK3), sz);
    y += P(22.0f);
    if (st.w > 60 || st.h > 56) {
      T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y, C(TK_INK3), "larger than the console's 60 by 56: the game draws it smaller, its shape kept", tw);
      y += P(22.0f);
    }
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y, C(TK_INK3),
      std::string(!st.bHasSound ? "It has no sound." : StudioVideoMuted() ? "Its sound is muted." : "Its sound plays with it.") +
          " Plays in a loop. Pick another video below to show it.",
      tw);
    if (st.bHasSound) {
      ImGui::SetCursorScreenPos(ImVec2(x, y + P(24.0f)));
      if (StudioButton("##pmute", StudioVideoMuted() ? "Sound on" : "Mute", "sound", StudioVideoMuted() ? SB_ON : SB_FLAT, 24.0f, 0.0f,
                       kStudioFsXs))
        StudioVideoMute(!StudioVideoMuted());
    }
  } else {
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x, y, st.bReading ? C(TK_INK2) : C(TK_WARN), video.empty() ? std::string("no video plays here") : st.why, tw);
  }
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(1, bh + P(14.0f)));
}

void PortraitHead(const std::string &name, const StudioRecord &rec) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = std::min(ImGui::GetContentRegionAvail().x - P(44.0f), P(900.0f));
  ImVec2 o = ImGui::GetCursorScreenPos();
  o.x += P(22.0f);
  o.y += P(16.0f);
  ImGui::SetCursorScreenPos(o);
  ImGui::BeginGroup();
  Title(ST_PORTRAITS, name, rec, W);
  const bool bClassic = rec.kind == SK_CLASSIC || (rec.kind == SK_EDIT && StudioRecordJson(ST_PORTRAITS, name)["idle"].kind == PortJson::NONE);
  const bool bMine = rec.kind == SK_OWN || rec.kind == SK_EDIT;
  StudioMediaPara(bClassic ? "The classic game's talking head: Smacker videos in its files. The game plays an idle video by the chances below, "
                             "and a talking one at random while the unit speaks."
                           : "Each video is the path with its number and an end the loader tries in order: .webm, .mkv, .mp4, .m4v, .mov, "
                             ".smk. A video that is not there falls to the one numbered below it, so idle 0 is enough. AV1 or VP9 "
                             "in WebM plays everywhere; the console's box is 60 by 56, and a larger video is drawn smaller to fit.",
                  W, C(TK_INK2), kStudioFsS);
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  PortraitPreview(name, bClassic, W);
  StudioMediaHeading("Idle", "played while it waits, each by its chance in 100", W);
  StudioValue ch;
  long long anChance[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; ++i) {
    StudioValue v;
    const std::string key = "idle_chance_" + std::to_string(i);
    if (StudioGet(ST_PORTRAITS, name, key, &v) && !v.n.empty())
      anChance[i] = v.n[0];
  }
  if (StudioGet(ST_PORTRAITS, name, "idle_chances", &ch) && ch.own.kind == PortJson::ARRAY)
    for (size_t i = 0; i < 4 && i < ch.own.items.size(); ++i)
      anChance[i] = static_cast<long long>(ch.own.items[i].Num(0));
  for (int n = 0; n < 4; ++n) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    VideoSlot(name, "idle", n, bClassic, bMine, W);
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, at.x + W - P(200.0f), Mid(at.y, P(30.0f), kStudioFsXs), C(TK_INK3),
      std::to_string(anChance[n]) + " in 100");
  }
  ImGui::Dummy(ImVec2(1, P(14.0f)));
  StudioMediaHeading("Talking", VideoBase(name, "talking").empty() ? "none: it talks with its idle videos" : "played at random while it speaks", W);
  for (int n = 0; n < 3; ++n)
    VideoSlot(name, "talking", n, bClassic, bMine, W);
  ImGui::Dummy(ImVec2(1, P(14.0f)));
  UnitButtons("Shown by", PortraitUsers(rec.nId), W, 5);
  const float y = ImGui::GetCursorScreenPos().y;
  ImGui::EndGroup();
  d->AddLine(ImVec2(o.x, y), ImVec2(o.x + W, y), C(TK_LINE));
  ImGui::SetCursorScreenPos(ImVec2(o.x - P(22.0f), y + P(4.0f)));
  ImGui::Dummy(ImVec2(1, 1));
}

// ---- a unit's Sounds view ------------------------------------------------------------------------------

std::string g_addFor;  // the unit and line the system's dialog is open for

// A sound as a chip: played when clicked, opened with the arrow, taken out with its x when it is the
// unit's own. 0 nothing, 1 taken out.
int SoundChip(const std::string &sound, bool bOwn, float *pX, float *pY, float x0, float right) {
  const std::string word = SoundWord(sound);
  const bool bNow = StudioSoundPlaying(nullptr) == sound;
  const float bw = StudioButtonSize(word.c_str(), "play", 24.0f, kStudioFsXs).x;
  const float w = bw + P(26.0f) + (bOwn ? P(24.0f) : 0.0f);
  if (*pX + w > right && *pX > x0) {
    *pX = x0;
    *pY += P(30.0f);
  }
  int nOut = 0;
  ImGui::PushID(sound.c_str());
  ImGui::SetCursorScreenPos(ImVec2(*pX, *pY));
  const StudioSoundWhere where = StudioSoundFile(sound);
  if (StudioButton("##p", word.c_str(), bNow ? "pause" : "play", bNow ? SB_ON : SB_PLAIN, 24.0f, 0.0f, kStudioFsXs)) {
    StudioSoundInfo info;
    std::string why;
    if (!StudioPlaySound(sound, &info, &why))
      StudioSay(why);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s\n%s", StudioShowName(ST_SOUNDS, sound).c_str(),
                      where.shown.empty() ? "no file" : (where.shown + (where.bThere ? "" : " (not there)")).c_str());
  if (!where.shown.empty() && !where.bThere)
    ImGui::GetWindowDrawList()->AddLine(ImVec2(*pX + P(4.0f), *pY + P(22.0f)), ImVec2(*pX + bw - P(4.0f), *pY + P(22.0f)), C(TK_WARN), P(2.0f));
  ImGui::SetCursorScreenPos(ImVec2(*pX + bw + P(2.0f), *pY));
  if (StudioButton("##o", "", "link", SB_FLAT, 24.0f, 22.0f))
    StudioOpenDoc(ST_SOUNDS, sound, 0);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Open the sound");
  if (bOwn) {
    ImGui::SetCursorScreenPos(ImVec2(*pX + bw + P(26.0f), *pY));
    if (StudioButton("##x", "", "x", SB_FLAT, 24.0f, 22.0f))
      nOut = 1;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Take it out of the line");
  }
  ImGui::PopID();
  *pX += w + P(8.0f);
  return nOut;
}

void LineCard(const std::string &unit, const Line &l, bool bMine, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const std::vector<std::string> sounds = LineSounds(unit, l);
  const std::vector<std::string> own = OwnLineSounds(unit, l);
  const std::set<std::string> ownSet(own.begin(), own.end());
  const float lw = P(190.0f);
  d->ChannelsSplit(2);
  d->ChannelsSetCurrent(1);
  T(d, STUDIO_FONT_BOLD, 15.0f, o.x + P(14.0f), o.y + P(12.0f), C(TK_INK), l.pszLabel);
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, o.x + P(14.0f), o.y + P(32.0f), C(TK_INK3), l.pszWhen, lw - P(20.0f));
  // a play of the line as the game would pick: at random, or the first for the ready line
  ImGui::SetCursorScreenPos(ImVec2(o.x + P(14.0f), o.y + P(52.0f)));
  ImGui::PushID(l.pszWord);
  if (StudioButton("##line", sounds.size() > 1 ? "Play one" : "Play", "play", SB_GOOD, 24.0f, 0.0f, kStudioFsXs, !sounds.empty())) {
    static unsigned s_nPick = 1;
    s_nPick = s_nPick * 22695477u + 1u;
    const std::string pick = sounds[(s_nPick >> 16) % sounds.size()];
    StudioSoundInfo info;
    std::string why;
    if (!StudioPlaySound(pick, &info, &why))
      StudioSay(why);
  }
  float x0 = o.x + lw, x = x0, y = o.y + P(12.0f);
  const float right = o.x + W - P(14.0f);
  if (sounds.empty())
    T(d, STUDIO_FONT_TEXT, kStudioFsS, x, y + P(4.0f), C(TK_INK3), "it says nothing");
  std::string taken;
  for (const std::string &s : sounds)
    if (!s.empty() && SoundChip(s, bMine && ownSet.count(s) > 0, &x, &y, x0, right) == 1)
      taken = s;
  // the range in rows, and whether it holds sounds that are not the unit's own
  y += P(32.0f);
  if (!sounds.empty() && l.pszLast) {
    const std::string range = std::to_string(sounds.size()) + (sounds.size() == 1 ? " sound: " : " sounds: ") +
                              StudioShowName(ST_SOUNDS, sounds.front()) +
                              (sounds.size() > 1 ? " to " + StudioShowName(ST_SOUNDS, sounds.back()) : std::string());
    T(d, STUDIO_FONT_TEXT, kStudioFsXs, x0, y, C(TK_INK3), range, right - x0);
    y += P(20.0f);
  }
  // each sound's subtitle and how it plays (studio_voice.cpp)
  y = StudioSoundRows(sounds, bMine, x0, y + P(6.0f), right);
  if (bMine) {
    ImGui::SetCursorScreenPos(ImVec2(x0, y));
    const std::string key = "sound:" + unit + "#" + l.pszWord;
    const char *pszAdd = l.pszLast == nullptr ? (own.empty() ? "Give it its own sound" : "Another file in its place")
                         : own.empty()        ? "Give it its own sound"
                                              : "Add a sound";
    if (StudioButton("##add", pszAdd, "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs))
      StudioPickFile(key.c_str(), "Sounds", "wav,ogg,opus,mp3,flac");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(own.empty() ? "A file brought in as a sound of the mod's own; the line then says it in place of what it says now"
                                    : "A file brought in at the end of the line");
    std::string path;
    if (StudioPickedFile(key.c_str(), &path))
      StudioAsk("studio_unit_sound_add", "\"unit\":" + Q(unit) + ",\"line\":" + Q(l.pszWord) + ",\"from\":" + Q(path));
    // or the line pointed at sounds there are: the first, and the last of its range
    ImGui::SameLine(0.0f, P(12.0f));
    std::string picked;
    StudioValue a;
    StudioGet(ST_UNITS, unit, l.pszFirst, &a);
    const std::string first = a.n.empty() ? std::string() : SoundAt(a.n[0]);
    if (StudioRecordPick("##first", ST_SOUNDS, first, 200.0f, &picked, true, 24.0f, "no sound"))
      StudioAsk("studio_set", "\"table\":\"units\",\"name\":" + Q(unit) + ",\"field\":" + Q(l.pszFirst) + ",\"value\":" +
                                  (picked.empty() ? std::string("0") : Q(picked)));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(l.pszLast ? "Its first sound" : "Its sound");
    if (l.pszLast) {
      ImGui::SameLine(0.0f, P(6.0f));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "to");
      ImGui::SameLine(0.0f, P(6.0f));
      StudioValue b;
      StudioGet(ST_UNITS, unit, l.pszLast, &b);
      const std::string last = b.n.empty() ? std::string() : SoundAt(b.n[0]);
      if (StudioRecordPick("##last", ST_SOUNDS, last, 200.0f, &picked, true, 24.0f, "no sound"))
        StudioAsk("studio_set", "\"table\":\"units\",\"name\":" + Q(unit) + ",\"field\":" + Q(l.pszLast) + ",\"value\":" +
                                    (picked.empty() ? std::string("0") : Q(picked)));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Its last sound: the line is every sound from the first to it, by their rows");
    }
    y += P(32.0f);
  }
  ImGui::PopID();
  const float h = std::max(y - o.y + P(6.0f), P(92.0f));
  d->ChannelsSetCurrent(0);
  d->AddRectFilled(o, ImVec2(o.x + W, o.y + h), C(TK_PANE2), P(4.0f));
  d->AddRect(o, ImVec2(o.x + W, o.y + h), C(TK_LINE), P(4.0f));
  d->AddLine(ImVec2(o.x + lw - P(14.0f), o.y + P(10.0f)), ImVec2(o.x + lw - P(14.0f), o.y + h - P(10.0f)), C(TK_ROW));
  d->ChannelsMerge();
  if (!taken.empty())
    StudioAsk("studio_unit_sound_remove", "\"unit\":" + Q(unit) + ",\"line\":" + Q(l.pszWord) + ",\"sound\":" + Q(taken));
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(1, h + P(10.0f)));
}

void PortraitCard(const std::string &unit, bool bMine, float W) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  StudioValue v;
  StudioGet(ST_UNITS, unit, "portrait", &v);
  const long long nRow = v.n.empty() ? -1 : v.n[0];
  StudioRecord pr;
  std::string portrait = nRow >= 0 ? StudioRefName(ST_PORTRAITS, nRow) : std::string();
  if (!StudioRecordOf(ST_PORTRAITS, portrait, &pr))
    portrait.clear();  // a row no portrait has: the game shows none
  const float h = P(bMine ? 96.0f : 70.0f);
  d->AddRectFilled(o, ImVec2(o.x + W, o.y + h), C(TK_PANE2), P(4.0f));
  d->AddRect(o, ImVec2(o.x + W, o.y + h), C(TK_LINE), P(4.0f));
  // its first idle video playing, at the console's size
  const ImVec2 v0(o.x + P(12.0f), o.y + P(7.0f)), v1(v0.x + P(60.0f), v0.y + P(56.0f));
  d->AddRectFilled(v0, v1, C(TK_BLACK), P(3.0f));
  if (!portrait.empty()) {
    const bool bClassicVideos = pr.kind == SK_CLASSIC ||
                                (pr.kind == SK_EDIT && StudioRecordJson(ST_PORTRAITS, portrait)["idle"].kind == PortJson::NONE);
    int nKind = 0;
    std::string shown;
    StudioVideoDraw(SlotVideo(portrait, "idle", 0, bClassicVideos, &nKind, &shown), bClassicVideos, v0, v1);
  }
  d->AddRect(v0, v1, C(TK_LINE2), P(3.0f));
  const float xl = o.x + P(86.0f);
  T(d, STUDIO_FONT_BOLD, 15.0f, xl, o.y + P(12.0f), C(TK_INK), "Portrait");
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, xl, o.y + P(32.0f), C(TK_INK3), "its talking head in the console", P(170.0f));
  const float x0 = o.x + P(266.0f);
  T(d, STUDIO_FONT_TEXT, 14.0f, x0, o.y + P(12.0f), C(TK_INK), portrait.empty() ? std::string("none") : StudioShowName(ST_PORTRAITS, portrait));
  std::string words;
  if (!portrait.empty()) {
    const std::string idle = VideoBase(portrait, "idle");
    const bool bClassic = pr.kind == SK_CLASSIC;
    words = idle.empty() ? "no idle videos" : (bClassic ? "portrait\\" + idle + "0.smk in the classic game's files" : idle + "0 in the data folder");
  }
  T(d, STUDIO_FONT_TEXT, kStudioFsXs, x0, o.y + P(32.0f), C(TK_INK3), words, W - P(476.0f));
  ImGui::SetCursorScreenPos(ImVec2(o.x + W - P(14.0f) - StudioButtonSize("Open the portrait", "link", 24.0f, kStudioFsXs).x, o.y + P(10.0f)));
  if (StudioButton("##openp", "Open the portrait", "link", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, !portrait.empty()))
    StudioOpenDoc(ST_PORTRAITS, portrait, 0);
  if (bMine) {
    ImGui::SetCursorScreenPos(ImVec2(x0, o.y + P(58.0f)));
    std::string picked;
    if (StudioRecordPick("##portrait", ST_PORTRAITS, portrait, 240.0f, &picked, false, 24.0f, "a portrait"))
      StudioAsk("studio_set", "\"table\":\"units\",\"name\":" + Q(unit) + ",\"field\":\"portrait\",\"value\":" + Q(picked));
    ImGui::SameLine(0.0f, P(8.0f));
    StudioRecord own;
    const bool bHasOwn = StudioRecordOf(ST_PORTRAITS, unit, &own) && own.kind == SK_OWN;
    if (StudioButton("##ownp", bHasOwn ? "Its own portrait" : "Make it a portrait of its own", "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs)) {
      if (bHasOwn) {
        StudioAsk("studio_set", "\"table\":\"units\",\"name\":" + Q(unit) + ",\"field\":\"portrait\",\"value\":" + Q(unit));
        StudioOpenDoc(ST_PORTRAITS, unit, 0);
      } else {
        StudioAsk("studio_unit_portrait", "\"unit\":" + Q(unit));
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("A portrait record named after the unit, its videos brought in on its page");
  }
  ImGui::SetCursorScreenPos(o);
  ImGui::Dummy(ImVec2(1, h + P(10.0f)));
}

// ---- the commands ----------------------------------------------------------------------------------------

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

bool Fail(std::string *pAnswer, const std::string &why) {
  *pAnswer = why;
  return false;
}

bool CmdPlay(const PortJson &j, std::string *pAnswer) {
  StudioSoundInfo info;
  std::string why, from;
  bool bOk;
  if (j["file"].kind == PortJson::STRING) {
    bOk = StudioPlayDataFile(j["file"].s, &info, &why);
    from = "mod";
    if (bOk)
      why = StudioDataFolder() + j["file"].s;
  } else {
    const std::string name = j["name"].Str();
    if (!j["table"].Str("sounds").empty() && j["table"].Str("sounds") != "sounds")
      return Fail(pAnswer, "studio_play plays sounds; a portrait's videos are not played here");
    bOk = StudioPlaySound(name, &info, &why);
    const StudioSoundWhere w = StudioSoundFile(name);
    from = w.mpq.empty() ? "mod" : "classic";
    if (bOk)
      why = w.mpq.empty() ? w.disk : w.mpq;
  }
  if (!bOk)
    return Fail(pAnswer, why);
  *pAnswer = "\"from\":" + Q(from) + ",\"path\":" + Q(why) + ",\"kind\":" + Q(info.kind) + ",\"seconds\":" + Fmt(info.dSeconds, 3) +
             ",\"rate\":" + std::to_string(info.nRate) + ",\"channels\":" + std::to_string(info.nChannels);
  return true;
}

bool UnitOf(const PortJson &j, std::string *pUnit, std::string *pAnswer) {
  *pUnit = j["unit"].Str();
  StudioRecord r;
  if (!StudioRecordOf(ST_UNITS, *pUnit, &r))
    return Fail(pAnswer, "no unit " + *pUnit);
  if (r.kind != SK_OWN && r.kind != SK_EDIT)
    return Fail(pAnswer, "units " + *pUnit + " is not the mod's: edit it in the mod first");
  return true;
}

// The unit's voice and portrait as another unit has them, as names, one edit.
bool CmdUnitSounds(const PortJson &j, std::string *pAnswer) {
  std::string unit;
  if (!UnitOf(j, &unit, pAnswer))
    return false;
  const std::string from = j["from"].Str();
  StudioRecord r;
  if (!StudioRecordOf(ST_UNITS, from, &r))
    return Fail(pAnswer, "no unit " + from);
  if (!HasLines(from))
    return Fail(pAnswer, StudioShowName(ST_UNITS, from) + " has no voice lines in the classic files");
  std::string why;
  bool bOk = true;
  StudioGroupBegin(Sender(j), unit + " speaks as the " + StudioShowName(ST_UNITS, from));
  for (const Line &l : kLines)
    for (const char *psz : {l.pszFirst, l.pszLast}) {
      if (psz == nullptr || !bOk)
        continue;
      const long long n = StudioNumber(ST_UNITS, from, psz, 0);
      const PortJson v = n > 0 ? StudioJsonString(SoundAt(n)) : StudioJsonNumber(0);
      bOk = StudioSetField(ST_UNITS, unit, psz, v, Sender(j), std::string(), &why);
    }
  const bool bPortrait = j["portrait"].kind != PortJson::BOOL || j["portrait"].b;
  if (bOk && bPortrait) {
    const long long n = StudioNumber(ST_UNITS, from, "portrait", -1);
    if (n >= 0)
      bOk = StudioSetField(ST_UNITS, unit, "portrait", StudioJsonString(StudioRefName(ST_PORTRAITS, n)), Sender(j), std::string(), &why);
  }
  StudioGroupEnd();
  if (!bOk)
    return Fail(pAnswer, why);
  *pAnswer = "\"unit\":" + Q(unit) + ",\"from\":" + Q(from);
  return true;
}

// A unit's line pointed at its own sounds, the first to the last by name, or back to its base's
// when it has none left.
bool PointLine(const std::string &unit, const Line &l, const std::string &sender, std::string *pWhy) {
  const std::vector<std::string> own = OwnLineSounds(unit, l);
  if (own.empty()) {
    bool bOk = StudioRevertField(ST_UNITS, unit, l.pszFirst, sender, pWhy);
    if (bOk && l.pszLast)
      bOk = StudioRevertField(ST_UNITS, unit, l.pszLast, sender, pWhy);
    return bOk;
  }
  bool bOk = StudioSetField(ST_UNITS, unit, l.pszFirst, StudioJsonString(own.front()), sender, std::string(), pWhy);
  if (bOk && l.pszLast)
    bOk = StudioSetField(ST_UNITS, unit, l.pszLast, StudioJsonString(own.back()), sender, std::string(), pWhy);
  return bOk;
}

bool CmdUnitSoundAdd(const PortJson &j, std::string *pAnswer) {
  std::string unit;
  if (!UnitOf(j, &unit, pAnswer))
    return false;
  const Line *pLine = LineOf(j["line"].Str());
  if (pLine == nullptr)
    return Fail(pAnswer, "line is one of ready, what, pissed, yes");
  std::string rel, why;
  if (!StudioImport(j["from"].Str(), "sounds/" + unit, &rel, &why))
    return Fail(pAnswer, why);
  const std::vector<std::string> own = OwnLineSounds(unit, *pLine);
  // the ready line says one sound: a new one takes the place of the one it has
  int n = 0;
  if (!own.empty())
    n = atoi(own.back().substr(own.back().find_last_of('_') + 1).c_str()) + 1;
  const std::string name = unit + "_" + pLine->pszWord + "_" + Two(n);
  PortJson rec = StudioJsonObject();
  StudioJsonPut(rec, "file", StudioJsonString(rel));
  bool bOk = true;
  StudioGroupBegin(Sender(j), "a sound for " + unit + "'s " + pLine->pszLabel + " line");
  if (pLine->pszLast == nullptr)
    for (const std::string &s : own)
      bOk = bOk && StudioDelete(ST_SOUNDS, s, Sender(j), &why);
  bOk = bOk && StudioCreate(ST_SOUNDS, name, rec, Sender(j), &why) && PointLine(unit, *pLine, Sender(j), &why);
  StudioGroupEnd();
  if (!bOk)
    return Fail(pAnswer, why);
  *pAnswer = "\"sound\":" + Q(name) + ",\"file\":" + Q(rel);
  return true;
}

bool CmdUnitSoundRemove(const PortJson &j, std::string *pAnswer) {
  std::string unit;
  if (!UnitOf(j, &unit, pAnswer))
    return false;
  const Line *pLine = LineOf(j["line"].Str());
  if (pLine == nullptr)
    return Fail(pAnswer, "line is one of ready, what, pissed, yes");
  const std::string sound = j["sound"].Str();
  const std::vector<std::string> own = OwnLineSounds(unit, *pLine);
  if (std::find(own.begin(), own.end(), sound) == own.end())
    return Fail(pAnswer, sound + " is not one of " + unit + "'s own sounds of that line");
  std::string why;
  StudioGroupBegin(Sender(j), sound + " taken out of " + unit + "'s " + pLine->pszLabel + " line");
  const bool bOk = StudioDelete(ST_SOUNDS, sound, Sender(j), &why) && PointLine(unit, *pLine, Sender(j), &why);
  StudioGroupEnd();
  if (!bOk)
    return Fail(pAnswer, why);
  *pAnswer = "\"sound\":" + Q(sound);
  return true;
}

bool CmdUnitPortrait(const PortJson &j, std::string *pAnswer) {
  std::string unit;
  if (!UnitOf(j, &unit, pAnswer))
    return false;
  StudioRecord r;
  std::string why;
  bool bOk = true;
  StudioGroupBegin(Sender(j), "a portrait of " + unit + "'s own");
  if (!StudioRecordOf(ST_PORTRAITS, unit, &r)) {
    PortJson rec = StudioJsonObject();
    StudioJsonPut(rec, "idle", StudioJsonString("portraits/" + unit + "/idle"));
    StudioJsonPut(rec, "talking", StudioJsonString("portraits/" + unit + "/talk"));
    PortJson chances;
    chances.kind = PortJson::ARRAY;
    for (int c : {85, 5, 5, 5})
      chances.items.push_back(StudioJsonNumber(c));
    StudioJsonPut(rec, "idle_chances", chances);
    bOk = StudioCreate(ST_PORTRAITS, unit, rec, Sender(j), &why);
  }
  bOk = bOk && StudioSetField(ST_UNITS, unit, "portrait", StudioJsonString(unit), Sender(j), std::string(), &why);
  StudioGroupEnd();
  if (!bOk)
    return Fail(pAnswer, why);
  *pAnswer = "\"portrait\":" + Q(unit);
  return true;
}

// A video copied in as a portrait's idle or talking video n, under the name the loader looks for;
// the portrait given a path for its videos when it has none.
bool CmdPortraitVideo(const PortJson &j, std::string *pAnswer) {
  const std::string portrait = j["portrait"].Str(), kind = j["kind"].Str("idle");
  StudioRecord r;
  if (!StudioRecordOf(ST_PORTRAITS, portrait, &r))
    return Fail(pAnswer, "no portrait " + portrait);
  if (r.kind != SK_OWN && r.kind != SK_EDIT)
    return Fail(pAnswer, "portraits " + portrait + " is not the mod's: edit it in the mod first");
  if (kind != "idle" && kind != "talking")
    return Fail(pAnswer, "kind is idle or talking");
  const int n = static_cast<int>(j["n"].Num(0));
  if (n < 0 || n > (kind == "idle" ? 3 : 2))
    return Fail(pAnswer, kind == "idle" ? "an idle video is 0 to 3" : "a talking video is 0 to 2");
  const std::string from = j["from"].Str();
  std::string end = from.substr(from.find_last_of('.') == std::string::npos ? from.size() : from.find_last_of('.'));
  end = Lower(end);
  if (std::find_if(std::begin(kStudioVideoEnds), std::end(kStudioVideoEnds), [&](const char *psz) { return end == psz; }) ==
      std::end(kStudioVideoEnds))
    return Fail(pAnswer, "a portrait's video ends in .webm, .mkv, .mp4, .m4v, .mov or .smk");
  // the path it has, unless it is the classic game's (an edit that gives none)
  std::string base;
  const PortJson &own = StudioRecordJson(ST_PORTRAITS, portrait)[kind.c_str()];
  if (own.kind == PortJson::STRING)
    base = own.s;
  if (base.empty() && r.kind == SK_OWN && !r.base.empty() && StudioClassicRow(ST_PORTRAITS, r.base) < 0)
    base = VideoBase(portrait, kind.c_str());
  const bool bNewPath = base.empty();
  if (bNewPath)
    base = "portraits/" + portrait + "/" + (kind == "idle" ? "idle" : "talk");
  // another end of the same number would be tried first or passed over: the one brought in is the one
  const std::string rel = base + std::to_string(n) + end;
  std::string why;
  if (!StudioCopyAs(from, rel, &why))
    return Fail(pAnswer, why);
  if (bNewPath && !StudioSetField(ST_PORTRAITS, portrait, kind, StudioJsonString(base), Sender(j), std::string(), &why))
    return Fail(pAnswer, why);
  std::string shadow;
  for (const char *psz : kStudioVideoEnds)
    if (end != psz && StudioFileExists(base + std::to_string(n) + psz))
      shadow += (shadow.empty() ? "" : ", ") + base + std::to_string(n) + psz;
  *pAnswer = "\"file\":" + Q(rel) + ",\"path\":" + Q(base) + (shadow.empty() ? std::string() : ",\"also\":" + Q(shadow));
  if (!shadow.empty())
    StudioLog("warn", "the data folder also has " + shadow + "; the loader takes the first by .webm, .mkv, .mp4, .m4v, .mov, .smk");
  return true;
}

}  // namespace

void StudioUnitSounds(const std::string &unit) {
  ImDrawList *d = ImGui::GetWindowDrawList();
  const float W = std::min(ImGui::GetContentRegionAvail().x - P(22.0f), P(1100.0f));
  StudioRecord rec;
  if (!StudioRecordOf(ST_UNITS, unit, &rec))
    return;
  const bool bMine = rec.kind == SK_OWN || rec.kind == SK_EDIT;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  T(d, STUDIO_FONT_BOLD, 18.0f, o.x, o.y, C(TK_HEAD), "What it says");
  ImGui::Dummy(ImVec2(W, P(28.0f)));
  std::vector<StudioValue> row;
  std::string why;
  if (!StudioRow(ST_UNITS, unit, &row, &why)) {
    // a record that does not resolve has no lines to show: what is wrong, and the field put back
    StudioMediaPara("Its lines cannot be read: " + why, W, C(TK_BAD), 13.0f);
    const std::string where = "units " + unit + ".";
    if (bMine && why.compare(0, where.size(), where) == 0) {
      const std::string field = why.substr(where.size(), why.find_first_of(" :,", where.size()) - where.size());
      ImGui::Dummy(ImVec2(1, P(6.0f)));
      if (StudioButton("##revert", ("Put " + field + " back to its base's").c_str(), "undo", SB_PLAIN, 28.0f, 0.0f, 13.0f))
        StudioAsk("studio_set", "\"table\":\"units\",\"name\":" + Q(unit) + ",\"field\":" + Q(field) + ",\"revert\":true");
    }
    return;
  }
  if (!HasLines(unit)) {
    StudioMediaPara("The classic files keep voice lines for the units below the buildings only, so the " + StudioShowName(ST_UNITS, unit) +
                        " says nothing of its own. A unit of the mod's own based on it has lines to give.",
                    W, C(TK_INK2), 14.0f);
  } else {
    StudioMediaPara(bMine ? "Each line plays as the game plays it. Bring in files to give it sounds of its own (WAV, Ogg Vorbis, Opus, MP3 "
                            "or FLAC: they are copied into sounds/ in the data folder and made sound records), or point a line at sounds "
                            "there are, from its first to its last."
                          : "Each line plays as the game plays it. This unit is " +
                                std::string(rec.kind == SK_DEPENDENCY ? "a mod's this one depends on" : "the classic game's") +
                                ": make a unit of the mod's own based on it to change what it says.",
                    W, C(TK_INK2), 13.0f);
    ImGui::Dummy(ImVec2(1, P(6.0f)));
    if (bMine) {
      // a classic unit's voice taken whole, its portrait with it
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK2), "Speak as");
      ImGui::SameLine(0.0f, P(8.0f));
      std::string picked;
      if (StudioRecordPick("##borrow", ST_UNITS, std::string(), 220.0f, &picked, false, 26.0f, "a unit there is"))
        StudioAsk("studio_unit_sounds", "\"unit\":" + Q(unit) + ",\"from\":" + Q(picked));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Its four lines and its portrait made those of the unit picked, in one edit");
      ImGui::Dummy(ImVec2(1, P(8.0f)));
    }
    for (int i = 0; i < kLineCount; ++i)
      LineCard(unit, kLines[i], bMine, W);
  }
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  PortraitCard(unit, bMine, W);
}

void StudioSoundHead(int t, const std::string &name) {
  StudioRecord rec;
  if (!StudioRecordOf(t, name, &rec))
    return;
  if (t == ST_SOUNDS)
    SoundHead(name, rec);
  else if (t == ST_PORTRAITS)
    PortraitHead(name, rec);
}

void StudioSoundsInit(void) {
  PortControlCommand("studio_play", CmdPlay);
  PortControlCommand("studio_unit_sounds", CmdUnitSounds);
  PortControlCommand("studio_unit_sound_add", CmdUnitSoundAdd);
  PortControlCommand("studio_unit_sound_remove", CmdUnitSoundRemove);
  PortControlCommand("studio_unit_portrait", CmdUnitPortrait);
  PortControlCommand("studio_portrait_video", CmdPortraitVideo);
  StudioVoiceInit();
}
