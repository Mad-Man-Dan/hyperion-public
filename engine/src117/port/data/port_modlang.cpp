// port_modlang.cpp -- a mod's text in the player's language (port_modlang.h).
//
// A language's file, <mod>\lang\deDE.json:
//
//   {"language": "deDE",
//    "strings": {"unit/iron_trooper": {"text": "Eisensoldat", "source": "Iron Trooper"},
//                "race/iron": "Eiserne Legion"}}
//
// Each string is its text, or an object with the text and the mod's text it was made from
// ("source"), which tells a tool the translation is stale once the mod's own text changes. The
// loader rewrites each mod's types.json as read, before anything is made of it, so every place
// that shows a name or a button's words shows the translation; a button whose text gives its
// hotkey's letter (<K>) takes that letter as its key in the language.
#include <windows.h>
#include <ctype.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../platform/port_util.h"  // PortReadFile
#include "port_language.h"          // PortLanguageChosen, g_apszPortLanguages, PortLanguageNative
#include "port_modlang.h"
#include "port_mode.h"  // PortModeDecl, for a mode's words
#include "port_mods.h"

namespace {

std::string g_use;                          // ModLangUse's language
const PortJson *(*g_pfnWorking)(const std::string &);  // ModLangWorking's
std::vector<std::string> g_chain;           // the mods loading, in order
std::map<std::string, std::vector<ModLangString>> g_known;  // each mod's strings as it loaded
std::map<std::string, std::string> g_own;   // a translated string's own text, by key

std::string Lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

// A value that names a record, as its key does: the name, or the number.
std::string NameOf(const PortJson &v) {
  if (v.kind == PortJson::STRING)
    return v.s;
  if (v.kind == PortJson::NUMBER)
    return std::to_string(static_cast<long long>(v.d));
  if (v.kind == PortJson::BOOL && v.b)
    return "first";
  return std::string();
}

template <class J>
J *Member(J &obj, const char *pszName) {
  if (obj.kind != PortJson::OBJECT)
    return nullptr;
  for (auto &m : obj.members)
    if (m.first == pszName)
      return &m.second;
  return nullptr;
}

void Set(PortJson &obj, const char *pszName, const std::string &text) {
  if (PortJson *p = Member(obj, pszName)) {
    *p = PortJson();
    p->kind = PortJson::STRING;
    p->s = text;
    return;
  }
  PortJsonMember m;
  m.first = pszName;
  m.second.kind = PortJson::STRING;
  m.second.s = text;
  obj.members.push_back(m);
}

const char *const kButtonKinds[] = {"train", "build", "cast", "research", "upgrade"};

// Every string of a types.json: fn(key, the object that holds it, its member, the mod's text).
template <class J, class F>
void Walk(J &types, F fn) {
  if (J *pRaces = Member(types, "races"))
    for (auto &r : pRaces->members)
      if (r.second.kind == PortJson::OBJECT)
        fn("race/" + r.first, r.second, "name", r.second["name"].Str(r.first.c_str()));
  if (J *pUnits = Member(types, "units"))
    for (auto &u : pUnits->members) {
      if (u.second.kind != PortJson::OBJECT)
        continue;
      const bool bEdit = u.second["edit"].kind != PortJson::NONE;
      if (bEdit && u.second["name"].kind != PortJson::STRING)
        continue;  // an edit that gives no name: the game's own, in the game's language
      fn("unit/" + u.first, u.second, "name", u.second["name"].Str(u.first.c_str()));
    }
  // a weapon's, a tech's or an upgrade's name where the record gives one; else the game's own
  static const char *const kNamed[][2] = {{"weapons", "weapon/"}, {"techs", "tech/"}, {"upgrades", "upgrade/"}};
  for (const auto &n : kNamed)
    if (J *pTable = Member(types, n[0]))
      for (auto &w : pTable->members)
        if (w.second.kind == PortJson::OBJECT && w.second["name"].kind == PortJson::STRING)
          fn(std::string(n[1]) + w.first, w.second, "name", w.second["name"].Str());
  // a status's name, which the console shows over a unit it is on
  if (J *pStatuses = Member(types, "statuses"))
    for (auto &t : pStatuses->members)
      if (t.second.kind == PortJson::OBJECT && t.second["name"].kind == PortJson::STRING)
        fn("status/" + t.first, t.second, "name", t.second["name"].Str());
  // a sound's subtitle, the words of a voice line
  if (J *pSounds = Member(types, "sounds"))
    for (auto &s : pSounds->members)
      if (s.second.kind == PortJson::OBJECT && s.second["subtitle"].kind == PortJson::STRING)
        fn("sound/" + s.first, s.second, "subtitle", s.second["subtitle"].Str());
  if (J *pCards = Member(types, "cards"))
    for (auto &c : pCards->members) {
      if (c.second.kind != PortJson::ARRAY)
        continue;
      for (auto &e : c.second.items) {
        if (e.kind != PortJson::OBJECT)
          continue;
        const std::string key = ModLangCardKey(c.first, e);
        const bool bClassic = e["classic"].kind != PortJson::NONE;
        if (!bClassic || e["text"].kind == PortJson::STRING)
          fn(key + "/text", e, "text", e["text"].Str());
        if (!bClassic || e["disabled"].kind == PortJson::STRING)
          fn(key + "/disabled", e, "disabled", e["disabled"].Str());
      }
    }
}

// The folder of a mod by its id, ending in a backslash; "" for none.
std::string FolderOf(const std::string &mod) {
  for (const PortMod &m : PortMods())
    if (m.id == mod && !m.bBuiltIn)
      return m.folder;
  return std::string();
}

bool ReadJson(const std::string &path, PortJson *pOut, std::string *pWhy) {
  std::string text;
  if (!PortReadFile(path, &text))
    return false;
  std::string error;
  if (!PortJsonParse(text.c_str(), pOut, &error) || pOut->kind != PortJson::OBJECT) {
    *pWhy = error.empty() ? std::string("it is no JSON object") : error;
    return false;
  }
  return true;
}

// The language the mod's own text is in: mod.json's "language", English without one.
std::string SourceLanguage(const PortJson &modJson) {
  const std::string s = ModLangLocale(modJson["language"].Str());
  return s.empty() ? std::string("enUS") : s;
}

// A file's translations: key to text, the empty ones left out.
std::map<std::string, std::string> Texts(const PortJson &file) {
  std::map<std::string, std::string> out;
  for (const PortJsonMember &m : file["strings"].members) {
    const std::string text = m.second.kind == PortJson::STRING ? m.second.s : m.second["text"].Str();
    if (!text.empty())
      out[m.first] = text;
  }
  return out;
}

// A mod's file for a language: the tool's working one, else lang\<code>.json; NONE for none.
PortJson FileOf(const std::string &mod, const std::string &code) {
  if (const PortJson *pLangs = g_pfnWorking ? g_pfnWorking(mod) : nullptr)
    return (*pLangs)[code.c_str()];
  PortJson j;
  std::string why;
  const std::string folder = FolderOf(mod);
  if (folder.empty() || !ReadJson(folder + "lang\\" + code + ".json", &j, &why))
    return PortJson();
  return j;
}

// The letter a text marks as its hotkey, <K>, lower case; 0 for none.
char Marked(const std::string &text) {
  for (size_t i = 0; i + 2 < text.size(); ++i)
    if (text[i] == '<' && text[i + 2] == '>' && isalnum(static_cast<unsigned char>(text[i + 1])))
      return static_cast<char>(tolower(static_cast<unsigned char>(text[i + 1])));
  return 0;
}

PortJson ModJson(const std::string &folder) {
  PortJson j;
  std::string why;
  if (!folder.empty())
    ReadJson(folder + "mod.json", &j, &why);
  return j;
}

// The language files a mod's lang\ folder holds, by their names without .json.
std::vector<std::string> LangFiles(const std::string &folder) {
  std::vector<std::string> out;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((folder + "lang\\*.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return out;
  do {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      std::string name = fd.cFileName;
      out.push_back(name.substr(0, name.size() - 5));
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end());
  return out;
}

std::string List(const std::vector<std::string> &keys, size_t nMost) {
  std::string s;
  for (size_t i = 0; i < keys.size() && i < nMost; ++i)
    s += (i ? ", " : "") + keys[i];
  if (keys.size() > nMost)
    s += " and " + std::to_string(keys.size() - nMost) + " more";
  return s;
}

}  // namespace

std::string ModLangCardKey(const std::string &card, const PortJson &entry) {
  std::string what, target;
  if (entry["classic"].kind != PortJson::NONE) {
    what = "classic";
    target = Lower(NameOf(entry["classic"]));
  } else {
    for (const char *psz : kButtonKinds)
      if (entry[psz].kind != PortJson::NONE) {
        what = psz;
        target = NameOf(entry[psz]);
        break;
      }
  }
  return "card/" + card + "/" + std::to_string(static_cast<long long>(entry["position"].Num(0))) + "/" + what + "/" + target;
}

// A mode mod's own keys, mode/<id>/..., which ModLangMode reads back: its line and about, its
// notes by their place, each setting's name, group and gist, a choice or item by its id, and
// each preset's name and line (docs/mod-data.md, "A mode's words").
void ModeStrings(const PortJson &modJson, std::vector<ModLangString> *pOut) {
  const PortJson &mode = modJson["mode"];
  if (mode.kind != PortJson::OBJECT)
    return;
  const std::string at = "mode/" + modJson["id"].Str() + "/";
  pOut->push_back({at + "line", mode["line"].Str()});
  pOut->push_back({at + "about", mode["about"].Str()});
  pOut->push_back({at + "game_type/name", mode["game_type"]["name"].Str()});
  pOut->push_back({at + "game_type/tag", mode["game_type"]["tag"].Str()});
  for (size_t i = 0; i < mode["notes"].size(); ++i)
    pOut->push_back({at + "note/" + std::to_string(i + 1), mode["notes"].At(i).Str()});
  for (size_t i = 0; i < mode["settings"].size(); ++i) {
    const PortJson &st = mode["settings"].At(i);
    const std::string of = at + "setting/" + st["id"].Str() + "/";
    pOut->push_back({of + "name", st["name"].Str()});
    pOut->push_back({of + "group", st["group"].Str()});
    pOut->push_back({of + "gist", st["gist"].Str()});
    const bool bList = st["kind"].Str() == "list";
    const PortJson &items = bList ? st["items"] : st["choices"];
    for (size_t k = 0; k < items.size(); ++k) {
      const PortJson &it = items.At(k);
      const std::string id = it.kind == PortJson::STRING ? it.s : it["id"].Str();
      const std::string name = it.kind == PortJson::STRING ? it.s : it["name"].Str();
      if (bList) {
        pOut->push_back({of + "item/" + id + "/name", name});
        pOut->push_back({of + "item/" + id + "/line", it["line"].Str()});
      } else {
        pOut->push_back({of + "choice/" + id, name});
      }
    }
  }
  for (size_t i = 0; i < mode["presets"].size(); ++i) {
    const PortJson &p = mode["presets"].At(i);
    pOut->push_back({at + "preset/" + p["id"].Str() + "/name", p["name"].Str()});
    pOut->push_back({at + "preset/" + p["id"].Str() + "/line", p["line"].Str()});
  }
}

void ModLangStrings(const PortJson &types, const PortJson *pModJson, std::vector<ModLangString> *pOut) {
  if (pModJson != nullptr && pModJson->kind == PortJson::OBJECT) {
    pOut->push_back({"mod/name", (*pModJson)["name"].Str()});
    pOut->push_back({"mod/line", (*pModJson)["line"].Str()});
    ModeStrings(*pModJson, pOut);
  }
  Walk(types, [&](const std::string &key, const PortJson &, const char *, const std::string &source) {
    pOut->push_back({key, source});
  });
}

std::string ModLangLocale(const std::string &code) {
  std::string s;
  for (char c : code)
    if (c != '-' && c != '_')
      s += c;
  if (s.size() != 4)
    return std::string();
  for (int i = 0; i < g_nPortLanguages; ++i)
    if (Lower(s) == Lower(g_apszPortLanguages[i]))
      return g_apszPortLanguages[i];
  return std::string();
}

const char *ModLangLanguage(void) {
  return g_use.empty() ? PortLanguageChosen() : g_use.c_str();
}

void ModLangUse(const char *pszLocale) {
  g_use = pszLocale ? ModLangLocale(pszLocale) : std::string();
}

void ModLangWorking(const PortJson *(*pfnLangs)(const std::string &mod)) {
  g_pfnWorking = pfnLangs;
}

void ModLangChain(const std::vector<std::string> &ids) {
  g_chain = ids;
  g_known.clear();
  g_own.clear();
}

void ModLangApply(const std::string &mod, PortJson *pTypes) {
  const PortJson modJson = ModJson(FolderOf(mod));
  std::vector<ModLangString> strings;
  ModLangStrings(*pTypes, &modJson, &strings);
  g_known[mod] = strings;
  const std::string code = ModLangLanguage();
  if (code == SourceLanguage(modJson))
    return;
  // its own translations, then those of each mod after it in the chain, the last one's kept
  std::vector<std::string> from(1, mod);
  auto at = std::find(g_chain.begin(), g_chain.end(), mod);
  if (at != g_chain.end())
    from.assign(at, g_chain.end());
  std::map<std::string, std::string> texts;
  for (const std::string &m : from)
    for (const auto &t : Texts(FileOf(m, code)))
      texts[t.first] = t.second;
  if (texts.empty())
    return;
  Walk(*pTypes, [&](const std::string &key, PortJson &holder, const char *pszField, const std::string &source) {
    auto it = texts.find(key);
    if (it == texts.end())
      return;
    g_own[key] = source;
    Set(holder, pszField, it->second);
    // a button's key is the letter its text marks, in the language as in the mod's
    const char cKey = strcmp(pszField, "text") == 0 && key.compare(0, 5, "card/") == 0 ? Marked(it->second) : 0;
    if (cKey != 0)
      Set(holder, "hotkey", std::string(1, cKey));
  });
}

std::string ModLangOwnText(const std::string &key) {
  auto it = g_own.find(key);
  return it == g_own.end() ? std::string() : it->second;
}

// A mode's own words: the block's lines, each setting's words, its choices' and items' names,
// and the presets'. The keys are mode/<mod>/..., as docs/mod-data.md lists them; a key the
// language file has not leaves the mode's own text.
void ModLangMode(const std::string &mod, PortModeDecl *pDecl) {
  const std::string folder = FolderOf(mod);
  const PortJson modJson = ModJson(folder);
  const std::string code = ModLangLanguage();
  if (folder.empty() || code == SourceLanguage(modJson))
    return;
  std::map<std::string, std::string> texts;
  for (const auto &t : Texts(FileOf(mod, code)))
    texts[t.first] = t.second;
  if (texts.empty())
    return;
  const std::string at = "mode/" + mod + "/";
  auto take = [&](const std::string &key, std::string *pOut) {
    auto it = texts.find(at + key);
    if (it != texts.end())
      *pOut = it->second;
  };
  take("line", &pDecl->line);
  take("about", &pDecl->about);
  take("game_type/name", &pDecl->typeName);
  take("game_type/tag", &pDecl->typeTag);
  for (size_t i = 0; i < pDecl->notes.size(); ++i)
    take("note/" + std::to_string(i + 1), &pDecl->notes[i]);
  for (PortModeSetting &s : pDecl->settings) {
    const std::string of = "setting/" + s.id + "/";
    take(of + "name", &s.name);
    take(of + "group", &s.group);
    take(of + "gist", &s.gist);
    for (PortModeItem &it : s.items) {
      take(of + "choice/" + it.id, &it.name);
      take(of + "item/" + it.id + "/name", &it.name);
      take(of + "item/" + it.id + "/line", &it.line);
    }
  }
  for (PortModePreset &p : pDecl->presets) {
    take("preset/" + p.id + "/name", &p.name);
    take("preset/" + p.id + "/line", &p.line);
  }
}

// A line the mode's scripts say, by its key (hx.message, hx.hud): the language's text, or the
// key as it is when nothing has it, so a mod that keeps its own English still works.
std::string ModLangModeText(const std::string &mod, const std::string &key) {
  const std::string folder = FolderOf(mod);
  const PortJson modJson = ModJson(folder);
  if (folder.empty())
    return key;
  const std::string code = ModLangLanguage();
  const std::map<std::string, std::string> texts =
      Texts(FileOf(mod, code == SourceLanguage(modJson) ? SourceLanguage(modJson) : code));
  auto it = texts.find(key);
  if (it != texts.end())
    return it->second;
  // the mod's own language, where the player's has no such key
  if (code != SourceLanguage(modJson)) {
    const std::map<std::string, std::string> own = Texts(FileOf(mod, SourceLanguage(modJson)));
    auto o = own.find(key);
    if (o != own.end())
      return o->second;
  }
  return key;
}

void ModLangModPage(const std::string &folder, const PortJson &modJson, std::string *pName, std::string *pLine) {
  const std::string code = ModLangLanguage();
  if (code == SourceLanguage(modJson))
    return;
  PortJson file;
  std::string why;
  if (!ReadJson(folder + "lang\\" + code + ".json", &file, &why))
    return;
  const std::map<std::string, std::string> texts = Texts(file);
  auto name = texts.find("mod/name"), line = texts.find("mod/line");
  if (name != texts.end())
    *pName = name->second;
  if (line != texts.end() && !pLine->empty())
    *pLine = line->second;
}

void ModLangCheck(std::vector<std::string> *pWarnings, std::vector<std::string> *pNotes) {
  if (g_chain.empty())
    return;
  struct File {
    size_t nMod;
    std::string code;
    PortJson tree;
  };
  std::vector<File> files;
  std::set<std::string> known;  // the strings of the mods so far in the chain
  std::map<std::string, size_t> owner;
  std::map<std::string, std::string> source;
  const std::string top = g_chain.back();
  for (size_t i = 0; i < g_chain.size(); ++i) {
    const std::string &mod = g_chain[i];
    const std::string folder = FolderOf(mod);
    const PortJson modJson = ModJson(folder);
    auto k = g_known.find(mod);
    if (k != g_known.end())
      for (const ModLangString &s : k->second) {
        // a mod's page is its own: the mods it builds on keep theirs
        const bool bPage = s.key.compare(0, 4, "mod/") == 0;
        if (bPage && mod != top)
          continue;
        known.insert(s.key);
        owner[s.key] = i;
        source[s.key] = s.source;
      }
    if (k == g_known.end()) {  // a mod with no data: its page alone
      for (const char *psz : {"name", "line"}) {
        known.insert(std::string("mod/") + psz);
        owner[std::string("mod/") + psz] = i;
        source[std::string("mod/") + psz] = modJson[psz].Str();
      }
    }
    // a mode's own keys: its scripts name their lines and HUD values themselves (hx.message,
    // hx.hud), so any key under mode/<id>/ is the mode's and is not warned about
    const bool bMode = modJson["mode"].kind == PortJson::OBJECT;
    const std::string at = "mode/" + mod + "/";
    for (const std::string &name : LangFiles(folder)) {
      const std::string shown = mod + "/lang/" + name + ".json";
      const std::string code = ModLangLocale(name);
      if (code.empty() || code != name) {
        pWarnings->push_back(shown + ": a language's file is named by its locale as Hyperion writes it (deDE, frFR, esMX...); " +
                             name + " is none, so no game reads it");
        continue;
      }
      File f;
      f.nMod = i;
      f.code = code;
      std::string why;
      if (!ReadJson(folder + "lang\\" + name + ".json", &f.tree, &why)) {
        pWarnings->push_back(shown + " does not read: " + why);
        continue;
      }
      if (f.tree["language"].kind == PortJson::STRING && ModLangLocale(f.tree["language"].s) != code)
        pWarnings->push_back(shown + " says its language is " + f.tree["language"].s + "; its name, " + code + ", is what counts");
      for (const PortJsonMember &m : f.tree["strings"].members) {
        const bool bText = m.second.kind == PortJson::STRING || m.second["text"].kind == PortJson::STRING;
        if (!bText)
          pWarnings->push_back(shown + ": " + m.first + " is a text, or an object with its \"text\"");
        else if (!known.count(m.first) && !(bMode && m.first.compare(0, at.size(), at) == 0))
          pWarnings->push_back(shown + ": " + m.first + " names no string of " + mod +
                               (i > 0 ? " or the mods it builds on" : "") + ", so nothing shows it");
      }
      files.push_back(f);
    }
  }
  // each language: the strings with text of their own that no file at or after their mod gives
  std::set<std::string> codes;
  for (const File &f : files)
    codes.insert(f.code);
  for (const std::string &code : codes) {
    std::vector<std::string> missing, stale;
    size_t nNeeded = 0;
    for (const std::string &key : known) {
      if (source[key].empty())
        continue;
      ++nNeeded;
      bool bHave = false;
      for (const File &f : files) {
        if (f.code != code || f.nMod < owner[key])
          continue;
        const PortJson &v = f.tree["strings"][key.c_str()];
        const std::string text = v.kind == PortJson::STRING ? v.s : v["text"].Str();
        if (text.empty())
          continue;
        bHave = true;
        if (v["source"].kind == PortJson::STRING && v["source"].s != source[key] &&
            std::find(stale.begin(), stale.end(), key) == stale.end())
          stale.push_back(key);
      }
      if (!bHave)
        missing.push_back(key);
    }
    std::string line = std::string(PortLanguageNative(code.c_str())) + " (" + code + "): " +
                       std::to_string(nNeeded - missing.size()) + " of " + std::to_string(nNeeded) + " strings translated";
    if (!missing.empty())
      line += "; " + std::to_string(missing.size()) + " missing, shown in the mod's own text: " + List(missing, 6);
    pNotes->push_back(line);
    if (!stale.empty())
      pNotes->push_back(std::string(PortLanguageNative(code.c_str())) + " (" + code + "): " + std::to_string(stale.size()) +
                        (stale.size() == 1 ? " translation was" : " translations were") +
                        " made from a text the mod has changed since: " + List(stale, 6));
  }
}
