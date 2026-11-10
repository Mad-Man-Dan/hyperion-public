// port_modlang.h -- a mod's text in the player's language (port_modlang.cpp, docs/mod-data.md,
// "Languages"). A mod's lang\ folder holds a file for each language, lang\deDE.json, that maps
// the mod's strings by key ("unit/iron_trooper") to their text in that language. The loader
// takes each mod's types.json with the language's text in place of the mod's own, so a unit's
// name, a button's words and a race's name are shown in it, and the mod's own text where a
// translation is missing. A translation in a mod that depends on another covers that one's
// strings too. What is shown is all it changes: the fingerprint is of the files as they are.
#ifndef PORT_MODLANG_H
#define PORT_MODLANG_H

#include <string>
#include <vector>
#include "../platform/port_json.h"

// A string a translation may name: its key, and the mod's own text, "" when the mod gives none (a
// button that shows the name of the unit it trains, a race's name for its record's).
struct ModLangString {
  std::string key;
  std::string source;
};

// The strings a mod's types.json and mod.json give, in their order:
//   mod/name, mod/line                           mod.json's name and line
//   race/<race>                                  a race's name
//   unit/<record>                                a unit's name (a mod's unit, or an edit that names one)
//   weapon/<record>, tech/<record>, upgrade/<record>
//                                                the name a weapon, a tech or an upgrade gives
//   sound/<record>                               a sound's subtitle, the words of a voice line
//   card/<card>/<position>/<what>/<target>/text  a card button's text, and .../disabled its text
//                                                while it cannot; <what> is train, build, cast,
//                                                research, upgrade or classic, <target> what it names
//                                                ("first" for "classic": true)
// pModJson may be null for none.
void ModLangStrings(const PortJson &types, const PortJson *pModJson, std::vector<ModLangString> *pOut);
// A card entry's key, without the field: card/<card>/<position>/<what>/<target>.
std::string ModLangCardKey(const std::string &card, const PortJson &entry);
// A locale as Hyperion's languages write it ("deDE" from "de-de" or "de_DE"), "" for none of them.
std::string ModLangLocale(const std::string &code);

// The language a mod's text is shown in: the one ModLangUse gave, else the Language setting's
// (PortLanguageChosen, HYPERION_LANGUAGE in a test).
const char *ModLangLanguage(void);
void ModLangUse(const char *pszLocale);  // "" or null for the setting's
// The translations a tool holds (Hyperion Studio's, not saved yet), read in place of the files in
// a mod's lang folder: pfn gives a mod's languages, an object of each file's tree by its code, or
// null for the mod's files.
void ModLangWorking(const PortJson *(*pfnLangs)(const std::string &mod));

// The loader's calls (port_moddata.cpp): the chain about to load, by id in load order, so a
// dependent mod's translations reach the mods it builds on; then each mod's types.json as read,
// given the language's text; and a string's own text when a translation took its place, "" when
// none did (a race is chosen by either name).
void ModLangChain(const std::vector<std::string> &ids);
void ModLangApply(const std::string &mod, PortJson *pTypes);
std::string ModLangOwnText(const std::string &key);
// mod.json's name and line in the language, from the mod's own lang\ file (port_mods.cpp).
void ModLangModPage(const std::string &folder, const PortJson &modJson, std::string *pName, std::string *pLine);

// A mode's words in the language (port_modedecl.cpp): the "mode" block's lines, every
// setting's name, group and gist, its choices' and items' names and lines, and the presets'.
// The keys are mode/<mod>/line, /about, /note/<n>, /setting/<id>/name and the rest
// (docs/mod-data.md, "Modes").
struct PortModeDecl;
void ModLangMode(const std::string &mod, PortModeDecl *pDecl);
// A line a mode's scripts say, by its key (hx.message, hx.hud): the language's text, the mod's
// own where the language has none, and the key as it is when neither has it.
std::string ModLangModeText(const std::string &mod, const std::string &key);

// --mod-check (port_modcheck.cpp): a translation that names no string, a file that does not
// read or is named by no language, as warnings; each language's missing strings and those
// translated from a text that has changed since, as notes.
void ModLangCheck(std::vector<std::string> *pWarnings, std::vector<std::string> *pNotes);

#endif
