// studio_languages.cpp -- a mod's strings in each language (studio_languages.h): its languages,
// each string's translation in them with what is missing or stale, the language the test game
// shows the mod's text in, and the files a translator works on outside the studio.
//
// A translation is a member of the model's "lang" document, saved as the mod's lang\<code>.json
// (port/data/port_modlang.h), so each change is an edit that Undo takes back. A translation keeps
// the mod's text it was made from ("source"); once the mod's text changes it is stale until it is
// set again. A translator's file is a CSV (key, where, source, translation) or a PO file (msgctxt
// the key, msgid the mod's text, msgstr the translation); importing one is one edit, and a line
// that names no string of the mod is refused and named.
//
// The commands:
//   studio_languages {add? | remove? | game?}          the languages, each with its counts
//   studio_translations {language, missing?, stale?}   a language's strings and their translations
//   studio_translation_set {language, id | key, text}  one set, as one edit; "" or null takes it away
//   studio_translations_export {language, file?, format?}  a translator's file (csv or po)
//   studio_translations_import {language, file}        a translator's file read back, as one edit
#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include "../port/data/port_language.h"    // g_apszPortLanguages, PortLanguageNative
#include "../port/data/port_modlang.h"     // ModLangStrings, ModLangUse, ModLangWorking
#include "../port/platform/port_util.h"    // PortReadFile
#include "../port/platform/port_winmsg.h"  // WinMsgSdlWindow
#include "../port/tools/port_control.h"    // PortControlCommand
#include "studio_draw.h"
#include "studio_languages.h"

namespace {

using namespace studio_draw;
using studio_languages::Cell;
using studio_languages::Count;
using studio_languages::Key;

std::string g_game;          // the test game's language, "" for the studio's own
std::string g_envLanguage;   // HYPERION_LANGUAGE as the studio started, which a game started from it takes
bool g_bEnvLanguage = false;

// ---- languages by their codes -----------------------------------------------------------------------

// A language as the modder may name it: its code in any spelling, its first two letters, or its name.
std::string Locale(const std::string &code) {
  const std::string s = ModLangLocale(code);
  if (!s.empty())
    return s;
  for (int i = 0; i < g_nPortLanguages; ++i)
    if ((code.size() == 2 && Lower(code) == Lower(std::string(g_apszPortLanguages[i]).substr(0, 2))) ||
        Lower(code) == Lower(PortLanguageNative(g_apszPortLanguages[i])) || Lower(code) == Lower(studio_languages::Name(g_apszPortLanguages[i])))
      return g_apszPortLanguages[i];
  return std::string();
}

std::string KnownList(void) {
  std::string s;
  for (int i = 0; i < g_nPortLanguages; ++i)
    s += (i ? ", " : "") + std::string(g_apszPortLanguages[i]);
  return s;
}

// ---- a button's hotkey in a translation ---------------------------------------------------------------

char Marked(const std::string &text) {
  for (size_t i = 0; i + 2 < text.size(); ++i)
    if (text[i] == '<' && text[i + 2] == '>')
      return static_cast<char>(tolower(static_cast<unsigned char>(text[i + 1])));
  return 0;
}

// A button's translated text with the mod's hotkey letter marked when the translator marked none
// and the text has that letter: the language's button keeps the key. A text without the letter is
// left as typed.
std::string MarkKey(const std::string &key, const std::string &text, const std::string &source) {
  if (key.compare(0, 5, "card/") != 0 || key.size() < 5 || key.compare(key.size() - 5, 5, "/text") != 0 || Marked(text) != 0)
    return text;
  const char c = Marked(source);
  if (c == 0)
    return text;
  const size_t at = Lower(text).find(c);
  if (at == std::string::npos)
    return text;
  return text.substr(0, at) + "<" + text[at] + ">" + text.substr(at + 1);
}

// ---- the model's translations ----------------------------------------------------------------------------

std::string TextOf(const PortJson &v) {
  return v.kind == PortJson::STRING ? v.s : v["text"].Str();
}

int DependencyIndex(const std::string &mod) {
  for (int i = 0; i < StudioDependencies(); ++i)
    if (StudioDependencyId(i) == mod)
      return i;
  return -1;
}

// The translations the loader reads in a game the studio pushes into: the model's, saved or not.
const PortJson *WorkingLangs(const std::string &mod) {
  if (!StudioIsOpen())
    return nullptr;
  if (mod == StudioModId())
    return &StudioDocAccepted("lang");  // Claude's proposal stays out until it is accepted
  const int i = DependencyIndex(mod);
  return i >= 0 ? &StudioDependencyLang(i) : nullptr;
}

std::string Sender(const PortJson &j) {
  return j["sender"].Str("claude");
}

PortJson TranslationOf(const std::string &text, const std::string &source) {
  PortJson v = StudioJsonObject();
  StudioJsonPut(v, "text", StudioJsonString(text));
  StudioJsonPut(v, "source", StudioJsonString(source));
  return v;
}

// A language added as an empty file, inside the caller's group.
bool AddLanguage(const std::string &code, const std::string &sender, std::string *pWhy) {
  if (StudioDoc("lang")[code.c_str()].kind != PortJson::NONE)
    return true;
  PortJson file = StudioJsonObject();
  StudioJsonPut(file, "language", StudioJsonString(code));
  StudioJsonPut(file, "strings", StudioJsonObject());
  return StudioPut("lang", {code}, file, sender, "added the language " + studio_languages::Name(code), pWhy);
}

// The language a command names: one of Hyperion's, and not the mod's own text's.
bool LanguageArg(const PortJson &j, std::string *pCode, std::string *pWhy) {
  const std::string asked = j["language"].Str();
  *pCode = Locale(asked);
  if (pCode->empty()) {
    *pWhy = asked.empty() ? "it needs a language, such as deDE" : asked + " is none of Hyperion's languages: " + KnownList();
    return false;
  }
  if (*pCode == studio_languages::SourceLanguage()) {
    *pWhy = "the mod's own text is in " + studio_languages::Name(*pCode) + " (mod.json's \"language\"); a translation is into another";
    return false;
  }
  return true;
}

std::string CellState(int n) {
  return n == studio_languages::CELL_DONE ? "done" : n == studio_languages::CELL_STALE ? "stale" : n == studio_languages::CELL_MISSING ? "missing" : "empty";
}

std::string LanguageJson(const std::string &code) {
  const Count c = studio_languages::CountOf(code);
  return "{\"language\":" + Q(code) + ",\"name\":" + Q(studio_languages::Name(code)) + ",\"strings\":" + std::to_string(c.nNeeded) +
         ",\"translated\":" + std::to_string(c.nDone) + ",\"missing\":" + std::to_string(c.nMissing) +
         ",\"stale\":" + std::to_string(c.nStale) + "}";
}

// The test game's language: the mod's text in it in the studio's game, pushed again, and in each game
// a race's Play it starts (HYPERION_LANGUAGE, which the game's own text follows too).
void GameSet(const std::string &code) {
  g_game = code;
  ModLangUse(code.c_str());
  if (!code.empty())
    SetEnvironmentVariableA("HYPERION_LANGUAGE", code.c_str());
  else
    SetEnvironmentVariableA("HYPERION_LANGUAGE", g_bEnvLanguage ? g_envLanguage.c_str() : nullptr);
  if (StudioIsOpen() && StudioPushCount() > 0)
    StudioAsk("studio_push", "\"replace\":\"all\"");
}

// ---- the commands ---------------------------------------------------------------------------------------

// studio_languages {add? | remove? | game?}: a language added or taken away, each one edit; the test
// game's language ("" for the studio's own); the languages with their counts.
bool CmdLanguages(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string why;
  for (const char *psz : {"add", "remove"}) {
    if (j[psz].kind == PortJson::NONE)
      continue;
    PortJson arg = StudioJsonObject();
    StudioJsonPut(arg, "language", j[psz]);
    std::string code;
    if (!LanguageArg(arg, &code, pAnswer))
      return false;
    if (strcmp(psz, "add") == 0) {
      if (StudioDoc("lang")[code.c_str()].kind != PortJson::NONE) {
        *pAnswer = "the mod has " + studio_languages::Name(code) + " already";
        return false;
      }
      if (!AddLanguage(code, Sender(j), pAnswer))
        return false;
    } else {
      if (StudioDoc("lang")[code.c_str()].kind == PortJson::NONE) {
        *pAnswer = "the mod has no " + studio_languages::Name(code);
        return false;
      }
      if (!StudioPut("lang", {code}, PortJson(), Sender(j), "took away the language " + studio_languages::Name(code), pAnswer))
        return false;
    }
  }
  if (j["game"].kind == PortJson::STRING || j["game"].kind == PortJson::NULLV) {
    const std::string asked = j["game"].Str();
    const std::string code = asked.empty() ? std::string() : Locale(asked);
    if (!asked.empty() && code.empty()) {
      *pAnswer = asked + " is none of Hyperion's languages: " + KnownList();
      return false;
    }
    GameSet(code);
  }
  std::string list;
  for (const std::string &code : studio_languages::Languages())
    list += (list.empty() ? "" : ",") + LanguageJson(code);
  *pAnswer = "\"source\":" + Q(studio_languages::SourceLanguage()) + ",\"game\":" + Q(g_game) + ",\"strings\":" +
             std::to_string(studio_languages::Keys().size()) + ",\"languages\":[" + list + "],\"unsaved\":" +
             std::to_string(StudioUnsaved());
  return true;
}

std::string RowJson(const Key &k, const std::string &code) {
  const Cell c = studio_languages::CellOf(k.key, code);
  return "{\"key\":" + Q(k.key) + ",\"id\":" + Q(StudioStringIdOf(k.key)) + ",\"where\":" + Q(StudioStringWhere(k.key)) +
         ",\"source\":" + Q(k.source) + ",\"text\":" + Q(c.text) + ",\"state\":" + Q(CellState(c.nState)) + ",\"from\":" +
         Q(c.from.empty() ? (c.nState == studio_languages::CELL_DONE || c.nState == studio_languages::CELL_STALE ? StudioModId() : std::string()) : c.from) +
         ",\"made_from\":" + Q(c.madeFrom) + ",\"mod\":" + Q(k.mod.empty() ? StudioModId() : k.mod) + "}";
}

// studio_translations {language, missing?, stale?}: each string, its translation and its state.
bool CmdTranslations(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string code;
  if (!LanguageArg(j, &code, pAnswer))
    return false;
  const bool bMissing = j["missing"].kind == PortJson::BOOL && j["missing"].b;
  const bool bStale = j["stale"].kind == PortJson::BOOL && j["stale"].b;
  std::string list;
  int n = 0;
  for (const Key &k : studio_languages::Keys()) {
    const int nState = studio_languages::CellOf(k.key, code).nState;
    if ((bMissing && nState != studio_languages::CELL_MISSING) || (bStale && nState != studio_languages::CELL_STALE))
      continue;
    list += (n++ ? "," : "") + RowJson(k, code);
  }
  *pAnswer = LanguageJson(code).substr(1, std::string::npos);
  pAnswer->pop_back();
  *pAnswer += ",\"count\":" + std::to_string(n) + ",\"rows\":[" + list + "]";
  return true;
}

// One translation set inside the caller's group: its text, made from the text given or the mod's own;
// "" takes it away.
bool SetOne(const std::string &code, const Key &k, const std::string &text, const std::string &madeFrom, const std::string &sender,
            std::string *pWhy) {
  if (!AddLanguage(code, sender, pWhy))
    return false;
  const std::string label = "translated " + StudioStringWhere(k.key) + " into " + studio_languages::Name(code);
  if (text.empty())
    return StudioDoc("lang")[code.c_str()]["strings"][k.key.c_str()].kind == PortJson::NONE ||
           StudioPut("lang", {code, "strings", k.key}, PortJson(), sender, "took away the " + studio_languages::Name(code) + " of " + StudioStringWhere(k.key), pWhy);
  return StudioPut("lang", {code, "strings", k.key}, TranslationOf(MarkKey(k.key, text, k.source), madeFrom), sender, label, pWhy);
}

const Key *KeyArg(const PortJson &j, std::string *pWhy) {
  std::string key = j["key"].Str();
  if (key.empty() && !j["id"].Str().empty()) {
    key = StudioStringKey(j["id"].Str());
    if (key.empty()) {
      *pWhy = j["id"].Str() + " is no string of the mod a translation names: a classic text is the game's, in its own language";
      return nullptr;
    }
  }
  const Key *k = studio_languages::KeyOf(key);
  if (k == nullptr)
    *pWhy = key.empty() ? std::string("it needs the string's id (studio_strings) or its key (studio_translations)")
                        : "no string of the mod has the key " + key + "; studio_translations lists them";
  return k;
}

// studio_translation_set {language, id | key, text}: one translation, as one edit.
bool CmdTranslationSet(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string code;
  if (!LanguageArg(j, &code, pAnswer))
    return false;
  const Key *pKey = KeyArg(j, pAnswer);
  if (pKey == nullptr)
    return false;
  if (j["text"].kind != PortJson::STRING && j["text"].kind != PortJson::NULLV) {
    *pAnswer = "studio_translation_set needs text, a string (or null to take the translation away)";
    return false;
  }
  const Key k = *pKey;
  const std::string sender = Sender(j);
  StudioGroupBegin(sender, "translated " + StudioStringWhere(k.key) + " into " + studio_languages::Name(code));
  const bool bOk = SetOne(code, k, j["text"].Str(), k.source, sender, pAnswer);
  StudioGroupEnd();
  if (!bOk)
    return false;
  *pAnswer = "\"row\":" + RowJson(k, code) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// ---- a translator's files ----------------------------------------------------------------------------

std::string CsvField(const std::string &s) {
  if (s.find_first_of(",\"\r\n") == std::string::npos && (s.empty() || (s[0] != ' ' && s.back() != ' ')))
    return s;
  std::string out = "\"";
  for (char c : s)
    out += c == '"' ? std::string("\"\"") : std::string(1, c);
  return out + "\"";
}

std::string PoString(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '\\' || c == '"')
      out += '\\', out += c;
    else if (c == '\n')
      out += "\\n";
    else if (c == '\t')
      out += "\\t";
    else
      out += c;
  }
  return out + "\"";
}

// The rows a translator gets: every string with text of its own, and any that has a translation.
std::vector<const Key *> ExportRows(const std::string &code) {
  std::vector<const Key *> out;
  for (const Key &k : studio_languages::Keys())
    if (!k.source.empty() || studio_languages::CellOf(k.key, code).nState != studio_languages::CELL_EMPTY)
      out.push_back(&k);
  return out;
}

bool WriteText(const std::string &path, const std::string &text, std::string *pWhy) {
  FILE *f = fopen(path.c_str(), "wb");
  if (f == NULL) {
    *pWhy = path + " does not open to write";
    return false;
  }
  const bool bOk = fwrite(text.data(), 1, text.size(), f) == text.size();
  if (fclose(f) != 0 || !bOk) {
    *pWhy = path + " was not written whole";
    return false;
  }
  return true;
}

std::string Ext(const std::string &path) {
  const size_t dot = path.rfind('.'), slash = path.find_last_of("\\/");
  return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : Lower(path.substr(dot + 1));
}

// A translator's file by default: in the mod's .studio folder, the studio's own, which the game
// does not read and a mod's package leaves out.
std::string ExportPath(const std::string &code, const std::string &format) {
  const std::string studio = StudioModFolder() + ".studio";
  CreateDirectoryA(studio.c_str(), NULL);
  CreateDirectoryA((studio + "\\translations").c_str(), NULL);
  return studio + "\\translations\\" + code + "." + format;
}

// studio_translations_export {language, file?, format?}: a translator's file,
// .studio\translations\<code>.csv (or .po) in the mod's folder unless a file is named.
bool CmdExport(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string code;
  if (!LanguageArg(j, &code, pAnswer))
    return false;
  std::string file = j["file"].Str();
  std::string format = Lower(j["format"].Str(file.empty() ? "csv" : Ext(file).c_str()));
  if (format != "csv" && format != "po") {
    *pAnswer = "a translator's file is a csv or a po file";
    return false;
  }
  if (file.empty())
    file = ExportPath(code, format);
  const std::string modName = StudioDoc("mod")["name"].Str(StudioModId().c_str());
  std::string text;
  int nRows = 0, nMissing = 0;
  if (format == "csv") {
    text = "\xEF\xBB\xBF" "key,where,source," + code + ",state\r\n";
    for (const Key *k : ExportRows(code)) {
      const Cell c = studio_languages::CellOf(k->key, code);
      text += CsvField(k->key) + "," + CsvField(StudioStringWhere(k->key)) + "," + CsvField(k->source) + "," + CsvField(c.text) + "," +
              CellState(c.nState) + "\r\n";
      ++nRows;
      nMissing += c.nState == studio_languages::CELL_MISSING;
    }
  } else {
    std::string lang = code.substr(0, 2) + "_" + code.substr(2);
    text = "# " + modName + ", " + studio_languages::Name(code) + " (" + code + "), from Hyperion Studio.\n"
           "# msgctxt is the string's key, msgid the mod's text, msgstr its translation; <K> marks a button's hotkey.\n"
           "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\"Language: " + lang + "\\n\"\n";
    for (const Key *k : ExportRows(code)) {
      const Cell c = studio_languages::CellOf(k->key, code);
      text += "\n#. " + StudioStringWhere(k->key) + "\n";
      if (c.nState == studio_languages::CELL_STALE)
        text += "#, fuzzy\n#| msgid " + PoString(c.madeFrom) + "\n";
      text += "msgctxt " + PoString(k->key) + "\nmsgid " + PoString(k->source) + "\nmsgstr " + PoString(c.text) + "\n";
      ++nRows;
      nMissing += c.nState == studio_languages::CELL_MISSING;
    }
  }
  if (!WriteText(file, text, pAnswer))
    return false;
  *pAnswer = "\"file\":" + Q(file) + ",\"format\":" + Q(format) + ",\"rows\":" + std::to_string(nRows) + ",\"missing\":" + std::to_string(nMissing);
  return true;
}

struct Line {
  int nLine = 0;
  std::string key, source, text;
  bool bSource = false;
};

// A CSV's rows by its head's columns: key, source, and the translation (the language's code,
// "translation" or "text"); without a head, key, where, source, translation.
bool ParseCsv(const std::string &textIn, const std::string &code, std::vector<Line> *pOut, std::string *pWhy) {
  std::string text = textIn.compare(0, 3, "\xEF\xBB\xBF") == 0 ? textIn.substr(3) : textIn;
  std::vector<std::pair<int, std::vector<std::string>>> rows;
  std::vector<std::string> row;
  std::string field;
  bool bQuoted = false, bAny = false;
  int nLine = 1, nRowLine = 1;
  for (size_t i = 0; i <= text.size(); ++i) {
    const char c = i < text.size() ? text[i] : '\n';
    if (bQuoted) {
      if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
        field += '"';
        ++i;
      } else if (c == '"') {
        bQuoted = false;
      } else {
        if (c == '\n')
          ++nLine;
        if (c != '\r')
          field += c;
      }
      continue;
    }
    if (c == '"') {
      bQuoted = true;
      bAny = true;
    } else if (c == ',') {
      row.push_back(field);
      field.clear();
      bAny = true;
    } else if (c == '\n') {
      if (bAny || !field.empty()) {
        row.push_back(field);
        rows.push_back({nRowLine, row});
      }
      row.clear();
      field.clear();
      bAny = false;
      nRowLine = ++nLine;
    } else if (c != '\r') {
      field += c;
      bAny = true;
    }
  }
  if (bQuoted) {
    *pWhy = "a quote is not closed";
    return false;
  }
  if (rows.empty())
    return true;
  int nKey = 0, nSource = 2, nText = 3;
  size_t nFirst = 0;
  const std::vector<std::string> &head = rows[0].second;
  if (!head.empty() && Lower(head[0]) == "key") {
    nFirst = 1;
    nSource = nText = -1;
    for (size_t c = 0; c < head.size(); ++c) {
      const std::string h = Lower(head[c]);
      if (h == "source")
        nSource = static_cast<int>(c);
      else if (h == Lower(code) || h == "translation" || h == "text" || (nText < 0 && Locale(head[c]) == code))
        nText = static_cast<int>(c);
    }
    if (nText < 0) {
      *pWhy = "its head names no column " + code + " (or translation) for the translations";
      return false;
    }
  }
  for (size_t r = nFirst; r < rows.size(); ++r) {
    const std::vector<std::string> &v = rows[r].second;
    Line l;
    l.nLine = rows[r].first;
    l.key = nKey < static_cast<int>(v.size()) ? v[nKey] : std::string();
    l.bSource = nSource >= 0 && nSource < static_cast<int>(v.size());
    l.source = l.bSource ? v[nSource] : std::string();
    l.text = nText < static_cast<int>(v.size()) ? v[nText] : std::string();
    pOut->push_back(l);
  }
  return true;
}

// A PO file's entries: msgctxt the key, msgid the mod's text, msgstr the translation; a blank line
// or the next msgctxt ends one.
bool ParsePo(const std::string &text, std::vector<Line> *pOut, std::string *pWhy) {
  Line cur;
  bool bHave = false;
  std::string *pInto = nullptr;
  auto flush = [&](void) {
    if (bHave && !(cur.key.empty() && cur.source.empty()))
      pOut->push_back(cur);
    cur = Line();
    bHave = false;
    pInto = nullptr;
  };
  auto start = [&](int nLine) {
    flush();
    bHave = true;
    cur.nLine = nLine;
  };
  int nLine = 0;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    ++nLine;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (nLine == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
      line = line.substr(3);
    const size_t s = line.find_first_not_of(" \t");
    if (s == std::string::npos) {
      flush();
      continue;
    }
    if (line[s] == '#')
      continue;
    line = line.substr(s);
    const std::string word = line.substr(0, line.find_first_of(" \t\""));
    if (word == "msgctxt") {
      start(nLine);
      pInto = &cur.key;
    } else if (word == "msgid") {
      if (!bHave || cur.bSource)
        start(nLine);
      cur.bSource = true;
      pInto = &cur.source;
    } else if (word == "msgstr") {
      pInto = bHave ? &cur.text : nullptr;
    } else if (!word.empty()) {
      *pWhy = "line " + std::to_string(nLine) + " is no msgctxt, msgid, msgstr or string";
      return false;
    }
    const size_t q0 = line.find('"'), q1 = line.rfind('"');
    if (pInto == nullptr || q0 == std::string::npos || q1 == q0) {
      *pWhy = "line " + std::to_string(nLine) + " has no string in quotes where one goes";
      return false;
    }
    for (size_t i = q0 + 1; i < q1; ++i) {
      if (line[i] == '\\' && i + 1 < q1) {
        const char e = line[++i];
        *pInto += e == 'n' ? '\n' : e == 't' ? '\t' : e;
      } else {
        *pInto += line[i];
      }
    }
  }
  flush();
  return true;
}

// studio_translations_import {language, file}: a translator's file read back as one edit. A line that
// names no string of the mod is refused and named; an empty translation is left as it is.
bool CmdImport(const PortJson &j, std::string *pAnswer) {
  if (!StudioIsOpen()) {
    *pAnswer = "no mod is open";
    return false;
  }
  std::string code;
  if (!LanguageArg(j, &code, pAnswer))
    return false;
  const std::string file = j["file"].Str();
  std::string text;
  if (file.empty() || !PortReadFile(file, &text)) {
    *pAnswer = file.empty() ? std::string("studio_translations_import needs the file") : file + " does not read";
    return false;
  }
  std::vector<Line> lines;
  std::string why;
  const std::string ext = Ext(file);
  if (!(ext == "po" || ext == "pot" ? ParsePo(text, &lines, &why) : ParseCsv(text, code, &lines, &why))) {
    *pAnswer = file + " does not read as a " + (ext == "po" || ext == "pot" ? "PO" : "CSV") + " file: " + why;
    return false;
  }
  struct Todo {
    Key k;
    std::string text, source;
  };
  std::vector<Todo> todo;
  std::string refused;
  int nRefused = 0, nEmpty = 0, nSame = 0;
  for (const Line &l : lines) {
    const Key *k = studio_languages::KeyOf(l.key);
    if (k == nullptr) {
      refused += (nRefused++ ? "," : "") + std::string("{\"line\":") + std::to_string(l.nLine) + ",\"key\":" + Q(l.key) + ",\"why\":" +
                 Q(l.key.empty() ? "it names no string" : "no string of the mod has this key") + "}";
      continue;
    }
    if (l.text.empty()) {
      ++nEmpty;
      continue;
    }
    // made from the text the translator saw, so a translation of an older text shows as stale
    const std::string source = l.bSource ? l.source : k->source;
    const PortJson &now = StudioDoc("lang")[code.c_str()]["strings"][k->key.c_str()];
    if (TextOf(now) == MarkKey(k->key, l.text, k->source) && now["source"].Str() == source) {
      ++nSame;
      continue;
    }
    todo.push_back({*k, l.text, source});
  }
  const std::string sender = Sender(j);
  if (!todo.empty() || StudioDoc("lang")[code.c_str()].kind == PortJson::NONE) {
    const int nAt = StudioHistoryAt();
    StudioGroupBegin(sender, "imported " + std::to_string(todo.size()) + " translations into " + studio_languages::Name(code));
    bool bOk = AddLanguage(code, sender, pAnswer);
    for (size_t i = 0; bOk && i < todo.size(); ++i)
      bOk = SetOne(code, todo[i].k, todo[i].text, todo[i].source, sender, pAnswer);
    StudioGroupEnd();
    if (!bOk) {
      std::string undo;
      if (StudioHistoryAt() > nAt)
        StudioUndo(&undo);
      return false;
    }
  }
  const Count c = studio_languages::CountOf(code);
  *pAnswer = "\"file\":" + Q(file) + ",\"lines\":" + std::to_string(lines.size()) + ",\"imported\":" + std::to_string(todo.size()) +
             ",\"unchanged\":" + std::to_string(nSame) + ",\"empty\":" + std::to_string(nEmpty) + ",\"refused\":[" + refused + "]" +
             ",\"missing\":" + std::to_string(c.nMissing) + ",\"stale\":" + std::to_string(c.nStale) + ",\"unsaved\":" + std::to_string(StudioUnsaved());
  return true;
}

// ---- the file dialogs' answers --------------------------------------------------------------------------

std::mutex g_saveLock;
std::vector<std::pair<std::string, std::string>> g_saved;  // the language and the file the save dialog gave

void SDLCALL OnSave(void *pUser, const char *const *ppszFiles, int) {
  std::string *pCode = static_cast<std::string *>(pUser);
  if (ppszFiles != nullptr && ppszFiles[0] != nullptr) {
    std::lock_guard<std::mutex> lock(g_saveLock);
    g_saved.push_back({*pCode, ppszFiles[0]});
  }
  delete pCode;
}

std::string g_importFor;  // the language a file dialog for an import is open for

}  // namespace

namespace studio_languages {

const std::vector<Key> &Keys(void) {
  static std::vector<Key> s_keys;
  static unsigned s_nChange = ~0u;
  static std::string s_mod;
  if (s_nChange != StudioChange() || s_mod != StudioModId()) {
    s_nChange = StudioChange();
    s_mod = StudioModId();
    s_keys.clear();
    if (!StudioIsOpen())
      return s_keys;
    std::map<std::string, size_t> at;
    auto add = [&](const PortJson &types, const PortJson *pMod, const std::string &mod) {
      std::vector<ModLangString> strings;
      ModLangStrings(types, pMod, &strings);
      for (const ModLangString &s : strings) {
        auto it = at.find(s.key);
        if (it != at.end()) {
          s_keys[it->second] = {s.key, s.source, mod};
        } else {
          at[s.key] = s_keys.size();
          s_keys.push_back({s.key, s.source, mod});
        }
      }
    };
    for (int i = 0; i < StudioDependencies(); ++i)
      add(StudioDependencyTypes(i), nullptr, StudioDependencyId(i));
    add(StudioDoc("types"), &StudioDoc("mod"), std::string());
  }
  return s_keys;
}

const Key *KeyOf(const std::string &key) {
  static std::map<std::string, size_t> s_at;
  static const std::vector<Key> *s_pFor = nullptr;
  static size_t s_nFor = ~static_cast<size_t>(0);
  static unsigned s_nChange = ~0u;
  const std::vector<Key> &keys = Keys();
  if (s_pFor != &keys || s_nFor != keys.size() || s_nChange != StudioChange()) {
    s_pFor = &keys;
    s_nFor = keys.size();
    s_nChange = StudioChange();
    s_at.clear();
    for (size_t i = 0; i < keys.size(); ++i)
      s_at[keys[i].key] = i;
  }
  auto it = s_at.find(key);
  return it == s_at.end() ? nullptr : &keys[it->second];
}

std::vector<std::string> Languages(void) {
  std::vector<std::string> out;
  for (const PortJsonMember &m : StudioDoc("lang").members)
    if (m.second.kind == PortJson::OBJECT && ModLangLocale(m.first) == m.first)  // a file no game reads is left out
      out.push_back(m.first);
  return out;
}

std::string SourceLanguage(void) {
  const std::string s = ModLangLocale(StudioDoc("mod")["language"].Str());
  return s.empty() ? std::string("enUS") : s;
}

std::string Name(const std::string &code) {
  // the scripts the studio's faces lack are named in English
  static const char *const kEnglish[][2] = {{"jaJP", "Japanese"},         {"koKR", "Korean"}, {"ruRU", "Russian"},
                                            {"zhCN", "Chinese, simplified"}, {"zhTW", "Chinese, traditional"}};
  for (const auto &e : kEnglish)
    if (code == e[0])
      return e[1];
  return PortLanguageNative(code.c_str());
}

Cell CellOf(const std::string &key, const std::string &code) {
  Cell c;
  const Key *k = KeyOf(key);
  auto fill = [&](const PortJson &v, const std::string &from) -> bool {
    const std::string text = TextOf(v);
    if (text.empty())
      return false;
    c.text = text;
    c.from = from;
    c.madeFrom = v["source"].Str();
    c.nState = k != nullptr && v["source"].kind == PortJson::STRING && v["source"].s != k->source ? CELL_STALE : CELL_DONE;
    return true;
  };
  if (fill(StudioDoc("lang")[code.c_str()]["strings"][key.c_str()], std::string()))
    return c;
  // a string of a mod it builds on: that mod's translation, or one of a mod between them
  if (k != nullptr && !k->mod.empty())
    for (int i = StudioDependencies() - 1, i0 = DependencyIndex(k->mod); i0 >= 0 && i >= i0; --i)
      if (fill(StudioDependencyLang(i)[code.c_str()]["strings"][key.c_str()], StudioDependencyId(i)))
        return c;
  c.nState = k == nullptr || k->source.empty() ? CELL_EMPTY : CELL_MISSING;
  return c;
}

Count CountOf(const std::string &code) {
  Count n;
  for (const Key &k : Keys()) {
    if (k.source.empty())
      continue;
    ++n.nNeeded;
    const int s = CellOf(k.key, code).nState;
    n.nDone += s == CELL_DONE || s == CELL_STALE;
    n.nMissing += s == CELL_MISSING;
    n.nStale += s == CELL_STALE;
  }
  return n;
}

const std::string &GameLanguage(void) {
  return g_game;
}

void AskSet(const std::string &key, const std::string &code, const std::string &text) {
  StudioAsk("studio_translation_set", "\"language\":" + Q(code) + ",\"key\":" + Q(key) + ",\"text\":" + (text.empty() ? std::string("null") : Q(text)));
}

void AskAdd(const std::string &code) {
  StudioAsk("studio_languages", "\"add\":" + Q(code));
}

void AskRemove(const std::string &code) {
  StudioAsk("studio_languages", "\"remove\":" + Q(code));
  StudioSay("took away " + Name(code) + "; Undo brings it back, and saving deletes its file");
}

void AskGame(const std::string &code) {
  StudioAsk("studio_languages", "\"game\":" + Q(code));
  StudioSay(code.empty() ? std::string("the test game shows the mod's own text") : "the test game, and a race's Play it, show the mod's text in " + Name(code));
}

void AskExport(const std::string &code, const std::string &format) {
  static SDL_DialogFileFilter s_filter[2];
  static std::string s_what, s_ext;
  s_what = format == "po" ? "PO file, for translation tools" : "CSV, for a spreadsheet";
  s_ext = format;
  s_filter[0] = {s_what.c_str(), s_ext.c_str()};
  s_filter[1] = {"Every file", "*"};
  static std::string s_default;
  s_default = ExportPath(code, format);
  SDL_ShowSaveFileDialog(OnSave, new std::string(code), WinMsgSdlWindow(), s_filter, 2, s_default.c_str());
}

void AskImport(const std::string &code) {
  g_importFor = code;
  StudioPickFile("##lang-import", "A translator's file (CSV or PO)", "csv,po");
}

void Poll(void) {
  std::vector<std::pair<std::string, std::string>> saved;
  {
    std::lock_guard<std::mutex> lock(g_saveLock);
    saved.swap(g_saved);
  }
  for (const auto &s : saved) {
    std::string answer;
    PortJson j = StudioJsonObject();
    StudioJsonPut(j, "language", StudioJsonString(s.first));
    StudioJsonPut(j, "file", StudioJsonString(s.second));
    if (CmdExport(j, &answer))
      StudioSay("wrote " + s.second + " for a translator into " + Name(s.first));
    else
      StudioSay(answer);
  }
  std::string path;
  if (StudioPickedFile("##lang-import", &path) && !g_importFor.empty()) {
    StudioAsk("studio_translations_import", "\"language\":" + Q(g_importFor) + ",\"file\":" + Q(path));
    StudioSay("read " + path + " into " + Name(g_importFor) + "; Output lists the lines refused, and one Undo takes it back");
  }
}

}  // namespace studio_languages

std::vector<StudioLanguageProblem> StudioLanguagesProblems(void) {
  std::vector<StudioLanguageProblem> out;
  if (!StudioIsOpen())
    return out;
  for (const std::string &code : studio_languages::Languages()) {
    const Count c = studio_languages::CountOf(code);
    if (c.nMissing > 0)
      out.push_back({"note", studio_languages::Name(code) + ": " + std::to_string(c.nMissing) + " of " + std::to_string(c.nNeeded) +
                                 (c.nMissing == 1 ? " string is" : " strings are") + " not translated; the game shows the mod's own text there", code});
    if (c.nStale > 0)
      out.push_back({"needs", studio_languages::Name(code) + ": " + std::to_string(c.nStale) +
                                  (c.nStale == 1 ? " translation was" : " translations were") + " made from a text the mod has changed since", code});
  }
  return out;
}

void StudioLanguagesShow(const std::string &code) {
  StudioStringsShowMissing(code);
}

void StudioLanguagesInit(void) {
  if (const char *psz = getenv("HYPERION_LANGUAGE")) {
    g_bEnvLanguage = true;
    g_envLanguage = psz;
  }
  ModLangWorking(WorkingLangs);
  PortControlCommand("studio_languages", CmdLanguages);
  PortControlCommand("studio_translations", CmdTranslations);
  PortControlCommand("studio_translation_set", CmdTranslationSet);
  PortControlCommand("studio_translations_export", CmdExport);
  PortControlCommand("studio_translations_import", CmdImport);
}
