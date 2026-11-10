// studio_languages.h -- a mod's strings in each language (studio_languages.cpp), as the Strings
// workspace (studio_strings.cpp) shows them: the mod's languages, each string's translation in
// one, what is missing or was made from a text that has changed since, and the language the test
// game is shown in. A language is a file of the mod's lang folder (port/data/port_modlang.h), the
// model's "lang" document.
#ifndef STUDIO_LANGUAGES_H
#define STUDIO_LANGUAGES_H

#include <string>
#include <vector>

namespace studio_languages {

// A string a translation may name: its key, the mod's text, and the mod it is in ("" the open one).
struct Key {
  std::string key;
  std::string source;
  std::string mod;
};
// The open mod's strings and those of the mods it builds on, as the loader finds them.
const std::vector<Key> &Keys(void);
const Key *KeyOf(const std::string &key);

// The open mod's languages, by code ("deDE"), in its lang document's order; the language its own
// text is in (mod.json "language", English without one).
std::vector<std::string> Languages(void);
std::string SourceLanguage(void);
// "Deutsch" for deDE.
std::string Name(const std::string &code);

// A string's translation in a language: made, made from a text that has changed since, missing,
// or not wanted (a string with no text of its own); its text, the mod that gives it when that is
// one it builds on, and the text it was made from.
enum { CELL_MISSING, CELL_DONE, CELL_STALE, CELL_EMPTY };
struct Cell {
  int nState = CELL_MISSING;
  std::string text;
  std::string from;
  std::string madeFrom;
};
Cell CellOf(const std::string &key, const std::string &code);
struct Count {
  int nNeeded = 0, nDone = 0, nMissing = 0, nStale = 0;
};
Count CountOf(const std::string &code);

// The language the test game shows the mod's text in, "" for the studio's own.
const std::string &GameLanguage(void);

// The workspace asks, as the modder's: a translation set, a language added or taken away, the
// test game's language, a translator's file written or read.
void AskSet(const std::string &key, const std::string &code, const std::string &text);
void AskAdd(const std::string &code);
void AskRemove(const std::string &code);
void AskGame(const std::string &code);
void AskExport(const std::string &code, const std::string &format);
void AskImport(const std::string &code);
// Each frame the workspace is drawn: the file dialogs' answers taken.
void Poll(void);

}  // namespace studio_languages

// A string of the Strings workspace by its id: its key in a translation, "" when none names it
// (a classic text, which the game gives in the language); and where it is, in words, for a
// translator (studio_strings.cpp).
std::string StudioStringKey(const std::string &id);
std::string StudioStringWhere(const std::string &key);
std::string StudioStringIdOf(const std::string &key);
// The Strings workspace shown with the strings missing in a language.
void StudioStringsShowMissing(const std::string &code);

#endif
