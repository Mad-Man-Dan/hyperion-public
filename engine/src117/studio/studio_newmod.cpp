// studio_newmod.cpp -- a mod started in the studio: its folder made among the installed mods with a
// mod.json, a data folder and an empty types.json, then opened (studio_new_mod), and the New mod
// dialog that asks for its name and id. A mod is files, not edits, so making one is not undone.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <string>
#include "../port/data/port_mods.h"  // PortModsFolder, PortModsRescan, PortMods
#include "../port/platform/port_json.h"
#include "../port/tools/port_control.h"  // PortControlCommand
#include "studio_draw.h"

namespace {

using namespace studio_draw;

// The id a name gives: lower case, words joined by hyphens, letters and digits only.
std::string IdOf(const std::string &name) {
  std::string id;
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (isalnum(u))
      id += static_cast<char>(tolower(u));
    else if (!id.empty() && id.back() != '-')
      id += '-';
  }
  while (!id.empty() && id.back() == '-')
    id.pop_back();
  return id.substr(0, 40);
}

bool IdGood(const std::string &id) {
  if (id.empty() || id.size() > 40)
    return false;
  for (char c : id)
    if (!(islower(static_cast<unsigned char>(c)) || isdigit(static_cast<unsigned char>(c)) || c == '-'))
      return false;
  return true;
}

bool WriteText(const std::string &path, const std::string &text) {
  HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  DWORD n = 0;
  const BOOL bOk = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &n, NULL);
  CloseHandle(h);
  return bOk && n == text.size();
}

// The mod made and opened; false with why, and nothing left behind but what was there before.
bool NewMod(const std::string &id, const std::string &name, const std::string &by, const std::string &line, std::string *pWhy) {
  if (!IdGood(id)) {
    *pWhy = "an id is lower-case letters, digits and hyphens, at most 40";
    return false;
  }
  PortModsRescan();  // the folder as it is now: a mod taken out since the start is no longer there
  for (const PortMod &m : PortMods())
    if (m.id == id) {
      *pWhy = "a mod " + id + " is installed already";
      return false;
    }
  const std::string folder = PortModsFolder() + id;
  if (GetFileAttributesA(folder.c_str()) != INVALID_FILE_ATTRIBUTES) {
    *pWhy = "the mods folder has " + id + " already";
    return false;
  }
  if (!CreateDirectoryA(folder.c_str(), NULL) || !CreateDirectoryA((folder + "\\data").c_str(), NULL)) {
    *pWhy = "could not make " + folder;
    return false;
  }
  PortJson mod = StudioJsonObject();
  StudioJsonPut(mod, "id", StudioJsonString(id));
  StudioJsonPut(mod, "name", StudioJsonString(name.empty() ? id : name));
  if (!by.empty())
    StudioJsonPut(mod, "by", StudioJsonString(by));
  StudioJsonPut(mod, "version", StudioJsonString("0.1"));
  if (!line.empty())
    StudioJsonPut(mod, "line", StudioJsonString(line));
  StudioJsonPut(mod, "rules", StudioJsonString("changed"));
  StudioJsonPut(mod, "data", StudioJsonString("data/"));
  if (!WriteText(folder + "\\mod.json", StudioJsonText(mod)) || !WriteText(folder + "\\data\\types.json", "{}\n")) {
    *pWhy = "could not write the mod's files in " + folder;
    return false;
  }
  PortModsRescan();
  return StudioOpen(id, pWhy);
}

// studio_new_mod {id?, name, by?, line?}: a mod made among the installed ones and opened; its id
// from its name when not given.
bool CmdNewMod(const PortJson &j, std::string *pAnswer) {
  const std::string name = j["name"].Str();
  const std::string id = j["id"].Str().empty() ? IdOf(name) : j["id"].Str();
  std::string why;
  if (!NewMod(id, name, j["by"].Str(), j["line"].Str(), &why)) {
    *pAnswer = why;
    return false;
  }
  *pAnswer = "\"mod\":" + Q(id) + ",\"folder\":" + Q(StudioModFolder());
  return true;
}

struct Dialog {
  bool bOpen = false;
  bool bAppearing = false;
  char szName[64] = "";
  char szId[48] = "";
  char szBy[64] = "";
  char szLine[200] = "";
  bool bIdTyped = false;  // the id typed by hand, no longer following the name
  std::string why;
} g;

void Label(const char *psz) {
  StudioLabel(STUDIO_FONT_WIDE, 9.0f, C(TK_LABEL), psz, 0.1f, true);
}

}  // namespace

void StudioNewModOpen(void) {
  g = Dialog();
  g.bOpen = true;
  g.bAppearing = true;
}

void StudioNewModDraw(void) {
  if (!g.bOpen)
    return;
  if (g.bAppearing) {
    ImGui::OpenPopup("##newmod");
    g.bAppearing = false;
  }
  const ImGuiViewport *v = ImGui::GetMainViewport();
  const float W = std::min(P(560.0f), v->Size.x - P(80.0f));
  ImGui::SetNextWindowPos(ImVec2(v->Pos.x + v->Size.x / 2.0f, v->Pos.y + v->Size.y / 2.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(W, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(20.0f), P(18.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(P(8.0f), P(6.0f)));
  const bool bShown = ImGui::BeginPopupModal("##newmod", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar(2);
  if (!bShown) {
    g.bOpen = false;
    return;
  }
  const float inner = (W - P(40.0f)) / StudioPx(1.0f);
  StudioLabel(STUDIO_FONT_WIDE, 14.0f, C(TK_HEAD), "New mod", 0.06f, true);
  ImGui::Dummy(ImVec2(1, P(4.0f)));
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_INK3),
              "A mod of your own among the installed ones, with no records yet: make units, races and the rest in it.");
  ImGui::Dummy(ImVec2(1, P(6.0f)));
  Label("Its name");
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  if (StudioInput("##name", g.szName, sizeof g.szName, inner, 28.0f, "Shown in the game's Mods screen") && !g.bIdTyped)
    snprintf(g.szId, sizeof g.szId, "%s", IdOf(g.szName).c_str());
  Label("Its id");
  if (StudioInput("##id", g.szId, sizeof g.szId, inner, 28.0f, "lower-case-with-hyphens", nullptr, nullptr, false))
    g.bIdTyped = true;
  StudioLabel(STUDIO_FONT_TEXT, kStudioFsXs, C(TK_INK3), ("its folder: " + PortModsFolder() + g.szId).c_str());
  Label("By");
  StudioInput("##by", g.szBy, sizeof g.szBy, inner, 28.0f, "you, as players see it");
  Label("In a line");
  StudioInput("##line", g.szLine, sizeof g.szLine, inner, 28.0f, "what it is, for the Mods screen");
  if (!g.why.empty())
    StudioLabel(STUDIO_FONT_TEXT, kStudioFsS, C(TK_BAD), g.why.c_str());
  ImGui::Dummy(ImVec2(1, P(8.0f)));
  const bool bReady = IdGood(g.szId);
  if (StudioButton("##make", "Make it", "make", SB_GOOD, 30.0f, 0.0f, kStudioFsS, bReady) ||
      (bReady && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsAnyItemActive())) {
    if (NewMod(g.szId, g.szName, g.szBy, g.szLine, &g.why)) {
      StudioLog("info", std::string("made the mod ") + g.szId + " in " + StudioModFolder());
      StudioSay(std::string("the mod ") + g.szId + " is made and open");
      StudioWorkspaceSet(8);
      g.bOpen = false;
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine(0.0f, P(8.0f));
  if (StudioButton("##cancel", "Cancel", nullptr, SB_PLAIN, 30.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    g.bOpen = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void StudioNewModInit(void) {
  PortControlCommand("studio_new_mod", CmdNewMod);
}
