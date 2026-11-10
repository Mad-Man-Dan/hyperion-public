// port_modcheck.h -- a mod's check, Hyperion --mod-check <id> [<report.txt>] (port_modcheck.cpp):
// its data loaded as start-up loads it, then every file it names decoded, with no game played.
#ifndef PORT_MODCHECK_H
#define PORT_MODCHECK_H

#include <string>
#include <vector>

enum ModCheckKind { MOD_CHECK_SOUND, MOD_CHECK_VIDEO, MOD_CHECK_PICTURE, MOD_CHECK_GRP };

// A file a mod's record names: where ("units trooper.icon"), the file on disk, and for a sound
// the name the game reads it by.
struct ModCheckFile {
  std::string where;
  std::string path;
  std::string game;
  ModCheckKind kind;
};

// What the mod's load found (port_moddata.cpp), once start-up has loaded it: false with why when
// its data did not load; what the load passed over, a line each; a line for each table it adds to
// or edits; and the files its records name that its folders have.
bool ModDataCheckResult(std::string *pError, std::vector<std::string> *pNotes, std::vector<std::string> *pTables,
                        std::vector<ModCheckFile> *pFiles);

// A mode's generate rolled once for the check (port_moddata.cpp): the map read, the settings as
// JSON object text, the seed and the race every player takes. nPlayers seats play, each at a
// start of the map: the first a person, the rest computers, and the last of them the mode's
// enemy base, since a mode's run reads who plays and where. It gives the records it returned and
// the run, or false with why. No game is played and nothing reaches the game's tables.
bool ModDataModeRoll(const char *pszMap, const char *pszSettings, unsigned dwSeed, const char *pszRace,
                     std::string *pTypes, std::string *pRun, std::string *pWhy, int nPlayers = 2);

// The check, once the game's files are open (RunGame): it writes the report and ends the process,
// with 0 when nothing is wrong, 1 when something is, and 2 when there is no such mod.
void PortModCheckRun(void);

// --mode-roll's run, once the game's files are open (RunGame): the mode's generate rolled once
// for --map, --mode-settings, --mode-seed, --race and --players, written as a JSON object to
// --mode-roll's file or to standard output, then the process ended, with 0 when it rolled, 1
// when it did not and 2 when there is no such mode. The object names the mode, its version, the
// map, the players, the race, the seed and the settings, and then either "run" and "records"
// or "error".
void PortModeRollRun(void);

#endif
