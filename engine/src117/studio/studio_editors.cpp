// studio_editors.cpp -- a field's editor, for every kind of field a mod's records have
// (engine/studio/fields.json's editors): numbers in the units a modder thinks in (seconds, supply),
// choices, other records picked by name with a search, flags as chips, boxes, requirements in
// words, pictures and sounds as files of the data folder (picked among them, or brought in through
// the system's file dialog), script names, a sheet's sizes. Each change goes through studio_set,
// so it is an edit like any other. The pickers are shared with the other panes (studio_ui.h).
#include <SDL3/SDL.h>
#include <map>
#include <mutex>
#include <vector>
#include "../port/platform/port_winmsg.h"  // WinMsgSdlWindow
#include "studio_media.h"
#include "studio_draw.h"
#include "studio_races.h"  // ClassicRequires

namespace {

using namespace studio_draw;

void Set(int t, const std::string &name, const std::string &key, const std::string &json, bool bMerge = false) {
  StudioAsk("studio_set", "\"table\":" + Q(Table(t)) + ",\"name\":" + Q(name) + ",\"field\":" + Q(key) + ",\"value\":" + json +
                              (bMerge ? ",\"merge\":" + Q(name + "." + key) : std::string()));
}

// ---- the system's file dialog ------------------------------------------------------------------------
// SDL calls back on a thread of its own: the file waits here for the frame that asked.
std::mutex g_pickMutex;
std::map<std::string, std::string> g_picked;
std::string g_pickFor;

void SDLCALL OnPicked(void *pUser, const char *const *ppList, int) {
  std::string *pFor = static_cast<std::string *>(pUser);
  std::lock_guard<std::mutex> lock(g_pickMutex);
  if (ppList != nullptr && ppList[0] != nullptr)
    g_picked[*pFor] = ppList[0];
  delete pFor;
}

// ---- requirements in words --------------------------------------------------------------------------

std::string TermWords(const PortJson &q) {
  if (q.kind == PortJson::STRING) {
    if (q.s == "broodwar")
      return "an expansion game";
    if (q.s == "researched")
      return "the tech researched";
    return "a finished " + Short(ST_UNITS, q.s);
  }
  if (q.kind == PortJson::NUMBER)
    return "a finished " + Short(ST_UNITS, StudioRefName(ST_UNITS, static_cast<long long>(q.d)));
  if (q.kind == PortJson::OBJECT) {
    if (q["at"].kind != PortJson::NONE)
      return "made at the " + Short(ST_UNITS, q["at"].kind == PortJson::NUMBER ? StudioRefName(ST_UNITS, static_cast<long long>(q["at"].d)) : q["at"].Str());
    if (q["tech"].kind != PortJson::NONE)
      return StudioShowName(ST_TECHS, q["tech"].kind == PortJson::NUMBER ? StudioRefName(ST_TECHS, static_cast<long long>(q["tech"].d)) : q["tech"].Str()) + " researched";
    if (q["caster"].kind != PortJson::NONE)
      return "cast by a " + Short(ST_UNITS, q["caster"].Str());
  }
  if (q.kind == PortJson::ARRAY) {
    std::string s = "one of ";
    for (size_t i = 0; i < q.items.size(); ++i)
      s += (i ? ", " : "") + TermWords(q.items[i]);
    return s;
  }
  return PortJsonWrite(q);
}

// A field's value as the record would have it: its own, or its base's.
PortJson Effective(int t, const StudioValue &v, const std::string &key) {
  if (v.own.kind != PortJson::NONE)
    return v.own;
  if (!v.from.empty())
    return StudioRecordJson(t, v.from)[key.c_str()];
  return PortJson();
}

// Chips laid out left to right, wrapping at the right edge; a chip's x takes it out when given.
struct ChipRow {
  float x0, x, y, right, h;
  ChipRow(float fRight) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    x0 = x = p.x;
    y = p.y;
    right = fRight;
    h = P(24.0f);
  }
  // true when its x is clicked
  bool Chip(const std::string &text, const char *pszGlyph, bool bX, int nKind = SB_PLAIN) {
    const float w = StudioButtonSize(text.c_str(), pszGlyph, 24.0f, kStudioFsXs).x + (bX ? P(22.0f) : 0.0f);
    if (x + w > right && x > x0) {
      x = x0;
      y += h + P(6.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    StudioButton("##chip", text.c_str(), pszGlyph, nKind, 24.0f, 0.0f, kStudioFsXs, true);
    bool bOut = false;
    if (bX) {
      ImGui::SetCursorScreenPos(ImVec2(x + w - P(24.0f), y));
      bOut = StudioButton("##x", "", "x", SB_FLAT, 24.0f, 22.0f);
    }
    x += w + P(6.0f);
    return bOut;
  }
  void Place(float w) {
    if (x + w > right && x > x0) {
      x = x0;
      y += h + P(6.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    x += w + P(6.0f);
  }
  void End() {
    ImGui::SetCursorScreenPos(ImVec2(x0, y + h));
    ImGui::Dummy(ImVec2(1, P(2.0f)));
  }
};

int g_nReqAdding = 0;  // the kind of requirement being added: 0 none, 1 made at, 2 a unit, 3 a tech, 4 a caster
std::string g_reqFor;  // the record and field it is added to

}  // namespace

// ---- the shared pieces ----------------------------------------------------------------------------------

void StudioPickFile(const char *pszFor, const char *pszWhat, const char *pszExts) {
  static SDL_DialogFileFilter s_filter[2];
  static std::string s_what, s_exts;
  s_what = pszWhat;
  s_exts = pszExts;
  for (char &c : s_exts)
    if (c == ',')
      c = ';';
  s_filter[0] = {s_what.c_str(), s_exts.c_str()};
  s_filter[1] = {"Every file", "*"};
  SDL_ShowOpenFileDialog(OnPicked, new std::string(pszFor), WinMsgSdlWindow(), s_filter, 2,
                         StudioIsOpen() ? StudioDataFolder().c_str() : nullptr, false);
}

bool StudioPickedFile(const char *pszFor, std::string *pPath) {
  std::lock_guard<std::mutex> lock(g_pickMutex);
  auto it = g_picked.find(pszFor);
  if (it == g_picked.end())
    return false;
  *pPath = it->second;
  g_picked.erase(it);
  return true;
}

bool StudioRecordPick(const char *pszId, int nTable, const std::string &now, float fW, std::string *pPicked, bool bNone,
                      float fH, const char *pszEmpty) {
  static char s_szFind[48];
  bool bPicked = false;
  const std::string show = now.empty() ? std::string(pszEmpty) : StudioShowName(nTable, now);
  if (StudioSelectBegin(pszId, show.c_str(), fW, fH)) {
    if (ImGui::IsWindowAppearing()) {
      s_szFind[0] = '\0';
      ImGui::SetKeyboardFocusHere();
    }
    StudioInput("##find", s_szFind, sizeof s_szFind, 300.0f, 24.0f, "Find", "search");
    ImGui::BeginChild("##list", ImVec2(P(300.0f), P(300.0f)));
    if (bNone && StudioItem("none", now.empty())) {
      pPicked->clear();
      bPicked = true;
    }
    for (const StudioRecord &r : StudioRecords(nTable)) {
      if (r.kind == SK_EDIT)
        continue;
      const std::string s = StudioShowName(nTable, r.name) + "  (mod)";
      if (Has(s, s_szFind) && StudioItem(s.c_str(), r.name == now)) {
        *pPicked = r.name;
        bPicked = true;
      }
    }
    ImGuiListClipper clip;
    std::vector<int> rows;
    for (int n = 0; n < StudioClassicRows(nTable); ++n) {
      const char *psz = StudioClassicName(nTable, n);
      if (psz && (s_szFind[0] == '\0' || Has(StudioShowName(nTable, psz), s_szFind) || Has(psz, s_szFind)))
        rows.push_back(n);
    }
    clip.Begin(static_cast<int>(rows.size()), P(28.0f));
    while (clip.Step())
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const char *psz = StudioClassicName(nTable, rows[static_cast<size_t>(i)]);
        ImGui::PushID(rows[static_cast<size_t>(i)]);
        if (StudioItem(StudioShowName(nTable, psz).c_str(), now == psz)) {
          *pPicked = psz;
          bPicked = true;
        }
        ImGui::PopID();
      }
    ImGui::EndChild();
    StudioSelectEnd();
  }
  return bPicked;
}

bool StudioFileField(const char *pszId, const std::string &now, const char *pszExts, const char *pszFolder, float fW,
                     std::string *pNew) {
  ImGui::PushID(pszId);
  bool bChanged = false;
  const bool bThere = now.empty() || StudioFileExists(now);
  const std::string show = now.empty() ? std::string("none") : now + (bThere ? "" : "  (not in the folder)");
  if (StudioSelectBegin("##file", show.c_str(), fW, 26.0f)) {
    if (StudioItem("none", now.empty())) {
      pNew->clear();
      bChanged = true;
    }
    for (const std::string &f : StudioFiles(pszExts))
      if (StudioItem(f.c_str(), f == now)) {
        *pNew = f;
        bChanged = true;
      }
    StudioSelectEnd();
  }
  ImGui::SameLine(0.0f, P(6.0f));
  const std::string key = std::string(pszId) + "#" + (StudioIsOpen() ? StudioModId() : "");
  if (StudioButton("##browse", "Bring in a file", "plus", SB_PLAIN, 26.0f, 0.0f, kStudioFsXs, StudioIsOpen()))
    StudioPickFile(key.c_str(), "Files", pszExts);
  std::string path;
  if (StudioPickedFile(key.c_str(), &path)) {
    std::string rel, why;
    if (StudioImport(path, pszFolder, &rel, &why)) {
      *pNew = rel;
      bChanged = true;
      StudioLog("info", "brought " + rel + " into the mod");
    } else {
      StudioSay(why);
    }
  }
  if (!bThere) {
    ImGui::SameLine(0.0f, P(6.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, StudioCol(TK_WARN), "missing");
  }
  ImGui::PopID();
  return bChanged;
}

// The requirements of a unit, a tech or an upgrade, in words: each a chip, taken off with its x,
// and more added from a list of the kinds there are.
void StudioRequirements(int t, const std::string &name, const std::string &key, bool bMine, float fW) {
  StudioValue v;
  StudioGet(t, name, key, &v);
  const PortJson now = Effective(t, v, key);
  // a classic row, or an edit of one that gives none of its own, shows the classic trees' list, so
  // a change starts from it
  PortJson classic;
  if (now.kind == PortJson::NONE) {
    int nRow = StudioClassicRow(t, name);
    StudioRecord rec;
    if (StudioRecordOf(t, name, &rec) && rec.kind == SK_EDIT)
      nRow = rec.nId;
    else if (StudioRecordOf(t, name, &rec) && rec.kind == SK_OWN)
      nRow = -1;
    classic = studio_races::ClassicRequires(t, nRow, key == "use_requires");
  }
  ImGui::PushID(key.c_str());
  ChipRow row(ImGui::GetCursorScreenPos().x + fW);
  if (now.kind == PortJson::NONE) {
    row.Place(P(classic.kind == PortJson::ARRAY ? 120.0f : 260.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3),
                classic.kind == PortJson::ARRAY ? (classic.items.empty() ? "classic: nothing" : "classic:")
                : StudioRecordJson(t, name).kind == PortJson::NONE || v.from.empty() ? "the classic requirements"
                                                                                      : "nothing: made nowhere yet");
  }
  std::vector<PortJson> list;
  if (now.kind == PortJson::ARRAY)
    list = now.items;
  else if (classic.kind == PortJson::ARRAY)
    list = classic.items;
  int nOut = -1;
  for (size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const bool bAt = list[i].kind == PortJson::OBJECT && list[i]["at"].kind != PortJson::NONE;
    if (row.Chip(TermWords(list[i]), bAt ? "make" : "link", bMine))
      nOut = static_cast<int>(i);
    ImGui::PopID();
  }
  if (!bMine) {
    row.End();
    ImGui::PopID();
    return;
  }
  // the kinds that can be added
  const bool bUse = key == "use_requires";
  row.Place(P(150.0f));
  int nAdd = 0;
  if (StudioSelectBegin("##add", "+ needs", 140.0f, 24.0f, kStudioFsXs)) {
    if (!bUse && StudioItem("Made at a building..."))
      nAdd = 1;
    if (StudioItem("A finished unit..."))
      nAdd = 2;
    if (StudioItem("A tech researched..."))
      nAdd = 3;
    if (bUse && StudioItem("Cast by a unit..."))
      nAdd = 4;
    if (bUse && StudioItem("The tech researched"))
      nAdd = 6;
    if (StudioItem("An expansion game"))
      nAdd = 5;
    StudioSelectEnd();
  }
  std::string json;
  auto add = [&](const PortJson &term) {
    std::vector<PortJson> n = list;
    if (term.kind == PortJson::OBJECT && term["at"].kind != PortJson::NONE)
      n.insert(n.begin(), term);  // where it is made comes first, as the classic trees have it
    else
      n.push_back(term);
    PortJson a;
    a.kind = PortJson::ARRAY;
    a.items = n;
    json = PortJsonWrite(a);
  };
  if (nAdd == 5)
    add(StudioJsonString("broodwar"));
  else if (nAdd == 6)
    add(StudioJsonString("researched"));
  else if (nAdd > 0) {
    g_nReqAdding = nAdd;
    g_reqFor = name + "." + key;
  }
  if (g_nReqAdding > 0 && g_reqFor == name + "." + key) {
    row.Place(P(230.0f));
    std::string picked;
    const int nTable = g_nReqAdding == 3 ? ST_TECHS : ST_UNITS;
    if (StudioRecordPick("##pickreq", nTable, std::string(), 220.0f, &picked, false, 24.0f)) {
      PortJson term = StudioJsonObject();
      if (g_nReqAdding == 2)
        term = StudioJsonString(picked);
      else
        StudioJsonPut(term, g_nReqAdding == 1 ? "at" : g_nReqAdding == 3 ? "tech" : "caster", StudioJsonString(picked));
      add(term);
      g_nReqAdding = 0;
    }
  }
  row.End();
  if (nOut >= 0) {
    std::vector<PortJson> n = list;
    n.erase(n.begin() + nOut);
    PortJson a;
    a.kind = PortJson::ARRAY;
    a.items = n;
    json = PortJsonWrite(a);
  }
  if (!json.empty())
    Set(t, name, key, json);
  ImGui::PopID();
}

// ---- the editor -----------------------------------------------------------------------------------------

void StudioFieldEditor(int t, const std::string &name, const StudioField &f, const StudioValue &v, bool bMine, bool bBig) {
  const float h = bBig ? 36.0f : 26.0f;
  const float fWide = ImGui::GetContentRegionAvail().x / StudioPx(1.0f);
  ImGui::PushID(f.key.c_str());
  const std::string &e = f.editor;
  const bool bEnum = e.compare(0, 5, "enum:") == 0;
  const bool bFlags = e.compare(0, 6, "flags:") == 0;
  const bool bLink = f.nRef >= 0 || e.compare(0, 5, "link:") == 0;
  const int nRef = f.nRef >= 0 ? f.nRef : bLink ? StudioTableOf(e.substr(5)) : -1;
  const PortJson eff = Effective(t, v, f.key);
  if (!bMine)
    ImGui::BeginDisabled();
  if (e == "req") {
    if (!bMine)
      ImGui::EndDisabled();
    StudioRequirements(t, name, f.key, bMine, std::min(fWide, 560.0f));
    if (!bMine)
      ImGui::BeginDisabled();
  } else if (bFlags) {
    // the flags set as chips, one taken off with its x, one added from the list
    const std::vector<std::string> &names = StudioFlags(e.substr(6));
    const long long bits = v.n.empty() ? 0 : v.n[0];
    long long nNew = bits;
    ChipRow row(ImGui::GetCursorScreenPos().x + P(std::min(fWide, 560.0f)));
    for (size_t i = 0; i < names.size() && i < 64; ++i) {
      if (!((bits >> i) & 1) || names[i].empty())
        continue;
      ImGui::PushID(static_cast<int>(i));
      std::string label = names[i];
      for (char &c : label)
        if (c == '_')
          c = ' ';
      if (row.Chip(label, nullptr, bMine))
        nNew &= ~(1LL << i);
      ImGui::PopID();
    }
    if (bMine) {
      row.Place(P(120.0f));
      if (StudioSelectBegin("##addflag", "+ flag", 110.0f, 24.0f, kStudioFsXs)) {
        for (size_t i = 0; i < names.size() && i < 64; ++i) {
          if (((bits >> i) & 1) || names[i].empty() || names[i] == "unused")
            continue;
          std::string label = names[i];
          for (char &c : label)
            if (c == '_')
              c = ' ';
          ImGui::PushID(static_cast<int>(i));
          if (StudioItem(label.c_str()))
            nNew |= 1LL << i;
          ImGui::PopID();
        }
        StudioSelectEnd();
      }
    }
    row.End();
    if (nNew != bits) {
      if (t == ST_UNITS && f.key == "flags") {
        // a unit's flags by their names, as the mod's file keeps them readable
        std::string arr = "[";
        for (size_t i = 0; i < names.size() && i < 64; ++i)
          if (((nNew >> i) & 1) && !names[i].empty())
            arr += (arr.size() > 1 ? "," : "") + Q(names[i]);
        Set(t, name, f.key, arr + "]");
      } else {
        Set(t, name, f.key, std::to_string(nNew));
      }
    }
  } else if (f.nColumn >= 0 && f.nCount > 1) {
    // a box: its values side by side
    static const char *const kBox[] = {"left", "up", "right", "down"};
    std::vector<long long> vals = v.n;
    vals.resize(static_cast<size_t>(f.nCount), 0);
    bool bChanged = false;
    for (int i = 0; i < f.nCount; ++i) {
      if (i)
        ImGui::SameLine(0.0f, P(6.0f));
      ImGui::PushID(i);
      double d = static_cast<double>(vals[static_cast<size_t>(i)]);
      if (StudioNumber("##n", &d, 64.0f, h, bMine, bBig ? 16.0f : 13.0f)) {
        vals[static_cast<size_t>(i)] = static_cast<long long>(floor(d + 0.5));
        bChanged = true;
      }
      if (ImGui::IsItemHovered() && f.nCount == 4)
        ImGui::SetTooltip("%s", kBox[i]);
      ImGui::PopID();
    }
    if (bChanged) {
      std::string arr = "[";
      for (size_t i = 0; i < vals.size(); ++i)
        arr += (i ? "," : "") + std::to_string(vals[i]);
      Set(t, name, f.key, arr + "]");
    }
  } else if (e == "icon" && f.nColumn >= 0 && f.nCount == 1) {
    int nIcon = v.n.empty() ? -1 : static_cast<int>(v.n[0]);
    if (StudioIconPick("##icon", &nIcon, bBig ? 34.0f : 28.0f, bMine) && nIcon >= 0)
      Set(t, name, f.key, std::to_string(nIcon));
  } else if (e == "bool" && f.nColumn >= 0) {
    bool b = !v.n.empty() && v.n[0] != 0;
    if (StudioCheck("##b", b ? "yes" : "no", &b))
      Set(t, name, f.key, b ? "1" : "0");
  } else if (bEnum && f.nColumn >= 0 && f.nCount == 1) {
    std::string now = v.n.empty() ? std::string() : std::to_string(v.n[0]);
    for (const auto &it : StudioEnum(e.substr(5)))
      if (!v.n.empty() && it.first == v.n[0])
        now = it.second;
    if (StudioSelectBegin("##enum", now.c_str(), bBig ? 230.0f : 190.0f, bBig ? 34.0f : 24.0f, bBig ? 15.0f : kStudioFsS)) {
      for (const auto &it : StudioEnum(e.substr(5)))
        if (StudioItem(it.second.c_str(), !v.n.empty() && v.n[0] == it.first))
          Set(t, name, f.key, std::to_string(it.first));
      StudioSelectEnd();
    }
  } else if (bEnum && f.nColumn < 0) {
    // a choice the mod keeps itself, as a sheet's directions
    const long long n = static_cast<long long>(eff.Num(e == "enum:directions" ? 17 : 0));
    std::string now = std::to_string(n);
    for (const auto &it : StudioEnum(e.substr(5)))
      if (it.first == n)
        now = it.second;
    if (StudioSelectBegin("##enum", now.c_str(), 190.0f, 24.0f)) {
      for (const auto &it : StudioEnum(e.substr(5)))
        if (StudioItem(it.second.c_str(), it.first == n))
          Set(t, name, f.key, std::to_string(it.first));
      StudioSelectEnd();
    }
  } else if (bLink && nRef >= 0 && (f.nColumn >= 0 || f.key == "looks" || f.key == "buttons")) {
    // another record, picked by name; opened with the arrow beside it
    std::string now;
    if (f.nColumn >= 0 && !v.n.empty())
      now = v.n[0] == StudioClassicRows(nRef) ? std::string() : StudioRefName(nRef, v.n[0]);
    else if (eff.kind == PortJson::STRING)
      now = eff.s;
    else if (eff.kind == PortJson::NUMBER)
      now = StudioRefName(nRef, static_cast<long long>(eff.d));
    std::string picked;
    if (StudioRecordPick("##link", nRef, now, bBig ? 240.0f : 200.0f, &picked, f.nColumn >= 0, bBig ? 34.0f : 24.0f))
      Set(t, name, f.key, picked.empty() ? std::to_string(StudioClassicRows(nRef)) : Q(picked));
    if (!now.empty()) {
      ImGui::SameLine(0.0f, P(4.0f));
      if (!bMine)
        ImGui::EndDisabled();
      if (StudioButton("##open", "", "link", SB_FLAT, bBig ? 34.0f : 24.0f, 24.0f))
        StudioOpenDoc(nRef, now, 0);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open %s", StudioShowName(nRef, now).c_str());
      if (!bMine)
        ImGui::BeginDisabled();
    }
  } else if (e == "picture" || e == "sheetpath" || e == "grp" || e == "soundfile" || e == "video" || f.bPath) {
    // a file of the data folder; a unit's picture may also be a unit whose own it shows
    const bool bVideo = e == "video";
    const char *pszExts = e == "soundfile" || (f.bPath && t == ST_SOUNDS) ? "wav,ogg,opus,mp3,flac"
                          : bVideo || t == ST_PORTRAITS                   ? "webm,mkv,mp4,m4v,mov,smk"
                          : e == "picture"                                ? "png,grp"
                                                                          : "png,webp,tga,grp";
    const char *pszFolder = e == "soundfile" || t == ST_SOUNDS ? "sounds" : bVideo || t == ST_PORTRAITS ? "portraits" : "art";
    std::string now = eff.kind == PortJson::STRING ? eff.s : std::string();
    if (eff.kind == PortJson::OBJECT || eff.kind == PortJson::ARRAY)
      now = PortJsonWrite(eff);  // levels or parts of a sheet: shown, edited in Raw
    if (e == "picture" && !now.empty() && StudioClassicRow(ST_UNITS, now) >= 0) {
      std::string picked;
      if (StudioRecordPick("##unitpic", ST_UNITS, now, 220.0f, &picked, true))
        Set(t, name, f.key, picked.empty() ? "null" : Q(picked));
    } else if ((t == ST_SOUNDS || t == ST_PORTRAITS) && v.source == SS_CLASSIC && !now.empty()) {
      // the classic game's own file, in its archives: named, a sound played, and replaced by one
      // of the mod's
      StudioLabel(STUDIO_FONT_MONO, kStudioFsS, C(TK_INK2), now.c_str());
      ImGui::SameLine(0.0f, P(8.0f));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "the classic game's");
      if (t == ST_SOUNDS) {
        ImGui::SameLine(0.0f, P(8.0f));
        StudioPlayButton("##play", name);
      }
      ImGui::SameLine(0.0f, P(6.0f));
      const std::string key = "classicfile#" + name + "." + f.key;
      if (StudioButton("##own", "Give it a file of the mod's", "plus", SB_PLAIN, 24.0f, 0.0f, kStudioFsXs, bMine))
        StudioPickFile(key.c_str(), t == ST_SOUNDS ? "Sounds" : "Videos", pszExts);
      std::string path, rel, why;
      if (StudioPickedFile(key.c_str(), &path)) {
        if (StudioImport(path, pszFolder, &rel, &why)) {
          if (bVideo) {
            // a portrait's videos are named without their number and end, as below
            const size_t dot = rel.find_last_of('.');
            if (dot != std::string::npos)
              rel = rel.substr(0, dot);
            while (!rel.empty() && isdigit(static_cast<unsigned char>(rel.back())))
              rel.pop_back();
          }
          Set(t, name, f.key, Q(rel));
        }
        else
          StudioSay(why);
      }
    } else {
      std::string chosen;
      if (StudioFileField("##path", now, pszExts, pszFolder, std::min(fWide - 150.0f, 320.0f), &chosen)) {
        if (bVideo) {
          // a portrait's videos are named without their number and end: idle0.webm is idle
          const size_t dot = chosen.find_last_of('.');
          if (dot != std::string::npos)
            chosen = chosen.substr(0, dot);
          while (!chosen.empty() && isdigit(static_cast<unsigned char>(chosen.back())))
            chosen.pop_back();
        }
        Set(t, name, f.key, chosen.empty() ? "null" : Q(chosen));
      }
      if (t == ST_SOUNDS && !now.empty()) {
        ImGui::SameLine(0.0f, P(6.0f));
        StudioPlayButton("##play", name);
      }
      if (e == "picture") {
        ImGui::SameLine(0.0f, P(6.0f));
        std::string picked;
        if (StudioRecordPick("##unitpic", ST_UNITS, std::string(), 150.0f, &picked, false, 26.0f, "or a unit's own"))
          Set(t, name, f.key, Q(picked));
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Show the picture a unit has, in place of a file");
      }
    }
  } else if (e == "pair") {
    // a sheet's frame size or origin: two numbers; an origin not given is the frame's centre, as
    // the loader takes it, so it shows that until one is set
    const bool bUnset = eff.kind != PortJson::ARRAY;
    double a = !bUnset && eff.items.size() > 0 ? eff.items[0].Num(0) : 0.0;
    double b = !bUnset && eff.items.size() > 1 ? eff.items[1].Num(0) : 0.0;
    if (bUnset && f.key == "origin") {
      StudioValue fr;
      PortJson size = StudioGet(t, name, "frame", &fr) ? fr.own : PortJson();
      if (size.kind == PortJson::ARRAY && size.items.size() >= 2) {
        a = floor(size.items[0].Num(0) / 2.0);
        b = floor(size.items[1].Num(0) / 2.0);
      }
    }
    bool bChanged = StudioNumber("##a", &a, 70.0f, h, bMine, 13.0f);
    ImGui::SameLine(0.0f, P(6.0f));
    bChanged = StudioNumber("##b", &b, 70.0f, h, bMine, 13.0f) || bChanged;
    if (bUnset && f.key == "origin") {
      ImGui::SameLine(0.0f, P(8.0f));
      StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "the frame's centre");
    }
    if (bChanged)
      Set(t, name, f.key, "[" + Fmt(a, 2) + "," + Fmt(b, 2) + "]");
  } else if (e == "script" || f.bScript) {
    // an image script by its Name in iscript.txt, the mod's listed; a number is a classic script
    std::string now = eff.kind == PortJson::STRING ? eff.s : !v.n.empty() ? std::to_string(v.n[0]) : std::string();
    if (StudioSelectBegin("##script", now.empty() ? "its base's" : now.c_str(), 220.0f, 24.0f)) {
      const std::string &text = StudioScript();
      size_t at = 0;
      while ((at = text.find("Name", at)) != std::string::npos) {
        const size_t eol = text.find('\n', at);
        std::string line = text.substr(at + 4, eol == std::string::npos ? std::string::npos : eol - at - 4);
        while (!line.empty() && isspace(static_cast<unsigned char>(line.front())))
          line.erase(line.begin());
        while (!line.empty() && isspace(static_cast<unsigned char>(line.back())))
          line.pop_back();
        if (!line.empty() && StudioItem(line.c_str(), line == now))
          Set(t, name, f.key, Q(line));
        at = eol == std::string::npos ? text.size() : eol;
      }
      StudioSelectEnd();
    }
  } else if (f.nColumn >= 0 && f.nCount == 1 && e != "readonly") {
    // a number, in seconds or supply where the record keeps frames or halves
    const double dScale = e == "frames" ? 24.0 : e == "half" ? 2.0 : 1.0;
    double dv = v.n.empty() ? 0.0 : static_cast<double>(v.n[0]) / dScale;
    if (StudioNumber("##n", &dv, bBig ? 112.0f : 86.0f, h, bMine, bBig ? 20.0f : 14.0f))
      Set(t, name, f.key, Fmt(floor(dv * dScale + 0.5)), true);
    if (dScale != 1.0) {
      // the unit's word set in the middle of the number's box, not at its top as a line would be
      const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
      const char *pszUnit = e == "frames" ? "s" : "supply";
      ImGui::SameLine(0.0f, P(6.0f));
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const float w = TW(STUDIO_FONT_TEXT, kStudioFsXs, pszUnit);
      T(ImGui::GetWindowDrawList(), STUDIO_FONT_TEXT, kStudioFsXs, at.x, Mid(lo.y, hi.y - lo.y, kStudioFsXs), C(TK_INK3), pszUnit);
      ImGui::Dummy(ImVec2(w, hi.y - lo.y));
    }
  } else if (f.nColumn < 0 && (e == "text" || f.key == "name" || f.key == "comment")) {
    // text the record keeps as it is typed
    static std::string s_editing;
    static char s_sz[512];
    const std::string id = name + "." + f.key;
    if (s_editing != id)
      snprintf(s_sz, sizeof s_sz, "%s", eff.kind == PortJson::STRING ? eff.s.c_str() : f.key == "name" ? StudioShowName(t, name).c_str() : "");
    StudioInput("##t", s_sz, sizeof s_sz, f.key == "comment" ? std::min(fWide, 520.0f) : bBig ? 300.0f : 240.0f, h, nullptr, nullptr, nullptr,
                false, bBig ? 18.0f : kStudioFsS);
    if (ImGui::IsItemActive())
      s_editing = id;
    if (s_editing == id && !ImGui::IsItemActive()) {
      if (std::string(s_sz) != (eff.kind == PortJson::STRING ? eff.s : std::string()))
        Set(t, name, f.key, Q(s_sz));
      s_editing.clear();
    }
  } else if (t == ST_PORTRAITS && f.key == "idle_chances") {
    // the chance in 100 of each idle video: the loader's four columns, written as one array
    double ad[4];
    for (int i = 0; i < 4; ++i) {
      StudioValue c;
      ad[i] = StudioGet(t, name, "idle_chance_" + std::to_string(i), &c) && !c.n.empty() ? static_cast<double>(c.n[0]) : 0.0;
    }
    bool bChanged = false;
    for (int i = 0; i < 4; ++i) {
      if (i)
        ImGui::SameLine(0.0f, P(6.0f));
      ImGui::PushID(i);
      bChanged = StudioNumber("##c", &ad[i], 56.0f, h, bMine, 13.0f) || bChanged;
      ImGui::PopID();
    }
    ImGui::SameLine(0.0f, P(8.0f));
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), "in 100, idle 0 to 3");
    if (bChanged)
      Set(t, name, f.key, "[" + Fmt(floor(ad[0] + 0.5)) + "," + Fmt(floor(ad[1] + 0.5)) + "," + Fmt(floor(ad[2] + 0.5)) + "," +
                              Fmt(floor(ad[3] + 0.5)) + "]");
  } else if (f.nColumn < 0 && e == "int") {
    double dv = eff.Num(0);
    if (StudioNumber("##n", &dv, 86.0f, h, bMine, 14.0f))
      Set(t, name, f.key, Fmt(floor(dv + 0.5)));
  } else {
    // read only, or shaped beyond a field: its words; Raw edits it whole
    std::string words;
    if (f.nColumn >= 0)
      for (size_t i = 0; i < v.n.size(); ++i)
        words += (i ? ", " : "") + std::to_string(v.n[i]);
    else if (eff.kind != PortJson::NONE)
      words = PortJsonWrite(eff);
    StudioLabel(STUDIO_FONT_TEXT, bBig ? 15.0f : kStudioFsS, words.empty() ? C(TK_INK3) : C(TK_INK2),
                words.empty() ? "none" : words.c_str());
  }
  if (!bMine)
    ImGui::EndDisabled();
  ImGui::PopID();
}
