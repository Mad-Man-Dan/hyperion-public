// design_style.cpp -- styles: how the game is drawn, kept as one named set in design/styles/. A
// style holds the picture's Settings rows, the live values that differ from their defaults (the
// post effects, the lights' strength, HD, the looks' tuning), the lights' table, the looks and the
// HUD's picks. It is taken and put back through the live channel's own commands, so a style
// changes nothing a tool could not. Two styles held as A and B swap with F8, to compare them on
// the same fight (Again, design_scenario.cpp).
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "imgui.h"
#include "../port/platform/port_json.h"
#include "../port/tools/port_control.h"
#include "design_engine.h"
#include "design_ui.h"

namespace {

// The Settings rows that change the picture, by the name settings gives them.
const char *const kRows[] = {"Art",
                             "Classic art filtering",
                             "Gamma",
                             "Colour cycling",
                             "HD lighting",
                             "Player colours",
                             "Unit portraits",
                             "Console layout",
                             "Console skin",
                             "Allies blue, enemies red",
                             "Colour-blind player colours"};
// The live values that are the camera's or a tool's, not the picture's.
const char *const kLeftOut[] = {"zoom", "camera.smooth", "hd.debug"};

std::string g_current;  // the style last applied or saved
std::string g_a, g_b;
bool g_bOnB;
std::string g_said;

bool IsRow(const std::string &name) {
  for (const char *psz : kRows)
    if (name == psz)
      return true;
  return false;
}

bool LeftOut(const std::string &name) {
  for (const char *psz : kLeftOut)
    if (name == psz)
      return true;
  return false;
}

std::string Folder(void) {
  return DesignFolder().empty() ? std::string() : DesignFolder() + "styles\\";
}

std::string FileName(const std::string &name) {
  std::string s;
  for (char c : name)
    s += isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.' ? c : '-';
  return s;
}

std::string Inner(const std::string &obj) {
  const size_t a = obj.find('{'), b = obj.rfind('}');
  return a == std::string::npos || b == std::string::npos || b <= a ? std::string() : obj.substr(a + 1, b - a - 1);
}

std::string Num(double d) {
  char sz[32];
  snprintf(sz, sizeof sz, "%.6g", d);
  return sz;
}

bool Read(const std::string &name, PortJson *pOut, std::string *pWhy) {
  FILE *f = fopen((Folder() + FileName(name) + ".json").c_str(), "rb");
  if (f == NULL) {
    *pWhy = "no style is named " + name;
    return false;
  }
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    text.append(buf, n);
  fclose(f);
  std::string why;
  if (!PortJsonParse(text.c_str(), pOut, &why)) {
    *pWhy = name + ": " + why;
    return false;
  }
  return true;
}

// What a style is taken from, gathered as the answers come.
struct Capture {
  std::string name;
  std::string settings, values, lights, looks, hud;
};

// Each item of an array on a line of its own.
std::string Lines(const PortJson &arr) {
  std::string s;
  for (size_t i = 0; i < arr.size(); ++i)
    s += std::string(i ? "," : "") + "\n    " + PortJsonWrite(arr.At(i));
  return arr.size() ? s + "\n  " : s;
}

void Write(const Capture &c) {
  CreateDirectoryA(Folder().c_str(), NULL);
  const std::string text = "{\n  \"name\": " + PortJsonQuote(c.name) + ",\n  \"settings\": [" + c.settings +
                           "],\n  \"values\": {" + c.values + "},\n  \"lights\": [" + c.lights + "],\n  \"looks\": [" +
                           c.looks + "],\n  \"hud\": " + (c.hud.empty() ? "{}" : c.hud) + "\n}\n";
  const std::string path = Folder() + FileName(c.name) + ".json";
  FILE *f = fopen(path.c_str(), "wb");
  const bool bOk = f != NULL && fwrite(text.data(), 1, text.size(), f) == text.size();
  if (f != NULL)
    fclose(f);
  g_said = bOk ? "saved style " + c.name : "could not write " + path;
  if (bOk)
    g_current = c.name;
}

// The style as the game is drawn now, saved under name once the answers are in.
void SaveNow(const std::string &name) {
  auto c = std::make_shared<Capture>();
  c->name = name;
  DesignCall("settings", "", [c](const PortJson &j) {
    const PortJson &rows = j["settings"];
    for (size_t i = 0; i < rows.size(); ++i) {
      const PortJson &r = rows.At(i);
      if (!IsRow(r["name"].Str()))
        continue;
      c->settings += std::string(c->settings.empty() ? "" : ",") + "\n    {\"name\":" + PortJsonQuote(r["name"].Str()) +
                     ",\"value\":" + Num(r["value"].Num()) +
                     (r["switch"].b ? std::string(",\"on\":") + (r["on"].b ? "true" : "false") : std::string()) + "}";
    }
    if (!c->settings.empty())
      c->settings += "\n  ";
  });
  DesignCall("list", "", [c](const PortJson &j) {
    const PortJson &values = j["values"];
    for (size_t i = 0; i < values.size(); ++i) {
      const PortJson &v = values.At(i);
      const std::string name = v["name"].Str();
      if (LeftOut(name) || fabs(v["value"].Num() - v["default"].Num()) < 1e-9)
        continue;
      c->values += std::string(c->values.empty() ? "" : ",") + "\n    " + PortJsonQuote(name) + ": " + Num(v["value"].Num());
    }
    if (!c->values.empty())
      c->values += "\n  ";
  });
  DesignCall("lights", "", [c](const PortJson &j) { c->lights = Lines(j["lights"]); });
  DesignCall("looks", "", [c](const PortJson &j) { c->looks = Lines(j["looks"]); });
  DesignCall("design_hud", "", [c](const PortJson &j) {
    const PortJson &h = j["hud"];
    c->hud = "{\"design\":" + PortJsonQuote(h["design"].Str()) + ",\"layout\":" + PortJsonQuote(h["layout"].Str()) +
             ",\"size\":" + PortJsonQuote(h["size"].Str()) + ",\"material\":" + PortJsonQuote(h["material"].Str()) +
             ",\"hidden\":" + (h["hidden"].b ? "true" : "false") + "}";
    Write(*c);
  });
}

// A style put back: its rows, its values (the others to their defaults), its lights (the others
// taken away), its looks and the HUD.
bool Apply(const std::string &name, std::string *pWhy) {
  auto s = std::make_shared<PortJson>();
  if (!Read(name, s.get(), pWhy))
    return false;
  const PortJson &rows = (*s)["settings"];
  for (size_t i = 0; i < rows.size(); ++i)
    DesignCall("setting", Inner(PortJsonWrite(rows.At(i))));
  DesignCall("list", "", [s](const PortJson &j) {
    const PortJson &values = j["values"];
    for (size_t i = 0; i < values.size(); ++i) {
      const PortJson &v = values.At(i);
      const std::string n = v["name"].Str();
      if (LeftOut(n))
        continue;
      const PortJson &want = (*s)["values"][n.c_str()];
      const double d = want.kind == PortJson::NUMBER ? want.d : v["default"].Num();
      if (fabs(d - v["value"].Num()) > 1e-9)
        DesignCall("set", "\"name\":" + PortJsonQuote(n) + ",\"value\":" + Num(d));
    }
  });
  if ((*s)["lights"].kind == PortJson::ARRAY)
    DesignCall("lights", "", [s](const PortJson &j) {
      const PortJson &mine = (*s)["lights"];
      std::set<int> kept;
      for (size_t i = 0; i < mine.size(); ++i)
        kept.insert(static_cast<int>(mine.At(i)["image"].Num(-1)));
      const PortJson &now = j["lights"];
      for (size_t i = 0; i < now.size(); ++i) {
        const int nImage = static_cast<int>(now.At(i)["image"].Num(-1));
        if (kept.count(nImage) == 0)
          DesignCall("light", "\"image\":" + std::to_string(nImage) + ",\"remove\":true");
      }
      for (size_t i = 0; i < mine.size(); ++i)
        DesignCall("light", Inner(PortJsonWrite(mine.At(i))));
    });
  const PortJson &looks = (*s)["looks"];
  for (size_t i = 0; i < looks.size(); ++i)
    DesignCall("look", Inner(PortJsonWrite(looks.At(i))));
  if ((*s)["hud"].kind == PortJson::OBJECT && (*s)["hud"].size() > 0)
    DesignCall("design_hud", Inner(PortJsonWrite((*s)["hud"])));
  g_current = name;
  g_said = "style " + name;
  return true;
}

std::string Fields(void) {
  return "\"style\":{\"current\":" + PortJsonQuote(g_current) + ",\"a\":" + PortJsonQuote(g_a) + ",\"b\":" +
         PortJsonQuote(g_b) + ",\"on\":\"" + (g_bOnB ? "b" : "a") + "\"}";
}

// design_style: with nothing, the state. {list}; {apply: name}; {save: name}, written once the
// answers it is taken from are in; {a: name}, {b: name}; {flip} applies the other of the two.
bool CmdStyle(const PortJson &j, std::string *pAnswer) {
  if (j["list"].kind == PortJson::BOOL) {
    std::string s;
    for (const std::string &n : DesignStyleNames())
      s += std::string(s.empty() ? "" : ",") + PortJsonQuote(n);
    *pAnswer = "\"styles\":[" + s + "],\"folder\":" + PortJsonQuote(Folder());
    return true;
  }
  if (j["a"].kind == PortJson::STRING)
    g_a = j["a"].s;
  if (j["b"].kind == PortJson::STRING)
    g_b = j["b"].s;
  if (j["save"].kind == PortJson::STRING) {
    if (Folder().empty()) {
      *pAnswer = "there is no design folder to save in";
      return false;
    }
    SaveNow(j["save"].s);
  }
  if (j["apply"].kind == PortJson::STRING && !Apply(j["apply"].s, pAnswer))
    return false;
  if (j["flip"].kind == PortJson::BOOL) {
    const std::string &other = g_bOnB ? g_a : g_b;
    if (other.empty()) {
      *pAnswer = "flip needs both A and B";
      return false;
    }
    if (!Apply(other, pAnswer))
      return false;
    g_bOnB = !g_bOnB;
  }
  *pAnswer = Fields();
  return true;
}

// ---- the panel ----------------------------------------------------------------------------------

std::vector<std::string> g_names;
bool g_bNamesAsked;
char g_szSaveAs[64];
std::string g_shown;  // the style whose contents are listed
PortJson g_shownJson;

void Pick(const char *pszLabel, std::string *pSlot) {
  if (ImGui::BeginCombo(pszLabel, pSlot->empty() ? "none" : pSlot->c_str())) {
    for (const std::string &n : g_names)
      if (ImGui::Selectable(n.c_str(), n == *pSlot))
        *pSlot = n;
    ImGui::EndCombo();
  }
}

}  // namespace

void DesignStyleInit(void) {
  PortControlCommand("design_style", CmdStyle);
}

std::string DesignStyleFields(void) {
  return Fields();
}

void DesignStyleApply(const std::string &name) {
  std::string why;
  if (!Apply(name, &why))
    g_said = why;
}

const std::string &DesignStyleCurrent(void) {
  return g_current;
}

std::vector<std::string> DesignStyleNames(void) {
  std::vector<std::string> names;
  WIN32_FIND_DATAA fd;
  HANDLE h = Folder().empty() ? INVALID_HANDLE_VALUE : FindFirstFileA((Folder() + "*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return names;
  do {
    std::string n = fd.cFileName;
    names.push_back(n.substr(0, n.size() - 5));
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return names;
}

void DesignStylePanel(void) {
  if (!g_bNamesAsked) {
    g_bNamesAsked = true;
    g_names = DesignStyleNames();
  }
  ImGui::Text("Style: %s", g_current.empty() ? "as it is" : g_current.c_str());
  if (!g_said.empty())
    ImGui::TextDisabled("%s", g_said.c_str());
  if (ImGui::BeginListBox("##styles", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 5))) {
    for (const std::string &n : g_names) {
      ImGui::PushID(n.c_str());
      if (ImGui::SmallButton("Apply"))
        DesignCall("design_style", "\"apply\":" + PortJsonQuote(n));
      ImGui::SameLine();
      if (ImGui::Selectable(n.c_str(), n == g_shown)) {
        std::string why;
        g_shown = n;
        if (!Read(n, &g_shownJson, &why))
          g_said = why;
      }
      ImGui::PopID();
    }
    ImGui::EndListBox();
  }
  if (ImGui::SmallButton("List again"))
    g_names = DesignStyleNames();
  ImGui::SetNextItemWidth(-90);
  ImGui::InputTextWithHint("##stylesaveas", "Save as, such as dusk", g_szSaveAs, sizeof g_szSaveAs);
  ImGui::SameLine();
  ImGui::BeginDisabled(g_szSaveAs[0] == '\0');
  if (ImGui::Button("Save")) {
    DesignCall("design_style", "\"save\":" + PortJsonQuote(g_szSaveAs), [](const PortJson &) {
      g_names = DesignStyleNames();
    });
  }
  ImGui::EndDisabled();
  ImGui::Separator();
  ImGui::SetNextItemWidth(160);
  Pick("A", &g_a);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160);
  Pick("B", &g_b);
  ImGui::BeginDisabled(g_a.empty() || g_b.empty());
  if (ImGui::Button(g_bOnB ? "Flip to A (F8)" : "Flip to B (F8)"))
    DesignCall("design_style", "\"flip\":true");
  ImGui::EndDisabled();
  if (!g_shown.empty() && ImGui::CollapsingHeader(("What " + g_shown + " holds").c_str())) {
    const PortJson &rows = g_shownJson["settings"];
    for (size_t i = 0; i < rows.size(); ++i)
      ImGui::BulletText("%s: %s", rows.At(i)["name"].Str().c_str(), Num(rows.At(i)["value"].Num()).c_str());
    const PortJson &values = g_shownJson["values"];
    for (const PortJsonMember &m : values.members)
      ImGui::BulletText("%s = %s", m.first.c_str(), Num(m.second.Num()).c_str());
    ImGui::BulletText("%d lights, %d looks", static_cast<int>(g_shownJson["lights"].size()),
                      static_cast<int>(g_shownJson["looks"].size()));
    const PortJson &hud = g_shownJson["hud"];
    if (hud.size() > 0)
      ImGui::BulletText("HUD: %s, %s, %s", hud["design"].Str().c_str(), hud["layout"].Str().c_str(),
                        hud["material"].Str().c_str());
  }
}
