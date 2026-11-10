// port_mode.cpp -- the mode a game is played with (port_mode.h).
//
// The choice comes from the set-up's Game type list, the command line or a room's match, and is
// settled as the game starts (PortModeForGame): a new game takes the choice, a replay or a save
// with HX_OPT_MODE the one it recorded. A game with a mode plays by Hyperion's rules and
// records the mode's id, version, the fingerprint of its data, every setting's value and the
// run's seed, so it plays back and loads as it was played.
//
// The mode's data and the records its generate makes are loaded by port_moddata.cpp, which asks
// this file what the game's mode is.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "port_mode.h"
#include "port_mods.h"
#include "port_moddata.h"  // ModDataId, PortModeEnemyFind
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../platform/port_util.h"  // PortReadFile: --mode-settings @<file>
#include "../gameplay/port_session.h"
#include "../../h/hxrules.h"
#include "../../h/net_rand.h"  // net_get_fixed_seed
#include "../../gen/globals117.h"

namespace {

const PortLogger Log("hyperion_mods.log");

PortModeChoice g_choice;       // the next game's, from the tab or the command line
PortModeChoice g_net;          // a room's match, which outranks it for one game
bool g_bNet;
bool g_bCommandLine;

// The game being played
std::string g_id, g_settings;
uint32_t g_seed;
std::string g_folder;
unsigned char g_abFingerprint[20];
// the player the mode plays itself: -2 until it is looked for, then the slot or -1 for none
int g_nEnemySlot = -2;

// the replay or save read last
bool g_bRecorded;
std::string g_recordedId, g_recordedVersion, g_recordedSettings;
uint32_t g_recordedSeed;
unsigned char g_abRecordedFingerprint[20];

#pragma pack(push, 1)
struct Record {
  char szId[64];       // the mode's id, "" for none
  char szVersion[32];  // the version its mod.json gave
  BYTE abData[20];     // SHA-1 of the data it loaded
  DWORD dwSeed;        // the run's seed, drawn by now
  DWORD dwFlags;       // none yet
  char szSettings[PORT_MODE_RECORD_BYTES - 124];  // every setting's value, as JSON object text
};
#pragma pack(pop)
static_assert(sizeof(Record) == PORT_MODE_RECORD_BYTES, "the mode's record");

// --mode-settings as "<preset>" or "name=value,...": a preset's values, else each value by its
// setting's id, read as its kind takes it (a list's value is the ids turned off, separated by
// +). A value that is already JSON ({...}) is taken as it is.
std::string SettingsFromList(const PortModeDecl &d, const std::string &list) {
  if (!list.empty() && list[0] == '{')
    return list;
  // @<file>: the JSON object in that file. A command line cannot carry one itself, as the
  // quotes inside it are taken as the word's own and every value comes out empty.
  if (!list.empty() && list[0] == '@') {
    std::string text;
    if (PortReadFile(list.substr(1), &text) && !text.empty())
      return text;
    Log("modes: --mode-settings cannot read %s, so every setting takes its base", list.c_str() + 1);
    return std::string("{}");
  }
  for (const PortModePreset &p : d.presets)
    if (p.id == list)
      return p.values;
  std::string out = "{";
  size_t at = 0;
  while (at < list.size()) {
    const size_t comma = list.find(',', at);
    const std::string one = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    at = comma == std::string::npos ? list.size() : comma + 1;
    const size_t eq = one.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = one.substr(0, eq), value = one.substr(eq + 1);
    const PortModeSetting *pSetting = nullptr;
    for (const PortModeSetting &s : d.settings)
      if (s.id == key)
        pSetting = &s;
    if (pSetting == nullptr) {
      Log("modes: --mode-settings names %s, which %s does not have", key.c_str(), d.id.c_str());
      continue;
    }
    std::string text;
    if (pSetting->kind == "toggle") {
      text = (value == "off" || value == "false" || value == "0" || value == "no") ? "false" : "true";
    } else if (pSetting->kind == "number" || pSetting->kind == "seed") {
      char sz[40];
      snprintf(sz, sizeof sz, "%.10g", atof(value.c_str()));
      text = sz;
    } else if (pSetting->kind == "list") {
      text = "[";
      size_t k = 0;
      while (k < value.size()) {
        const size_t plus = value.find('+', k);
        const std::string item = value.substr(k, plus == std::string::npos ? std::string::npos : plus - k);
        k = plus == std::string::npos ? value.size() : plus + 1;
        if (!item.empty() && item != "none")
          text += (text.size() > 1 ? "," : "") + PortJsonQuote(item);
      }
      text += "]";
    } else {
      text = PortJsonQuote(value);
    }
    out += (out.size() > 1 ? "," : "") + PortJsonQuote(key) + ":" + text;
  }
  return out + "}";
}

}  // namespace

void PortModeChoose(const PortModeChoice &c) {
  PortModeCommandLine();  // so the command line never overwrites a later choice
  g_choice = c;
  lstrcpynA(Session().szMode, c.id.c_str(), sizeof Session().szMode);
  lstrcpynA(Session().szModeSettings, c.settings.c_str(), sizeof Session().szModeSettings);
  Session().nModeSeed = c.seed;
}

const PortModeChoice &PortModeChosen() {
  PortModeCommandLine();
  return g_choice;
}

void PortModeChooseNet(const PortModeChoice &c) {
  g_net = c;
  g_bNet = true;
}

void PortModeCommandLine() {
  if (g_bCommandLine)
    return;
  g_bCommandLine = true;
  // the session's choice (the tab, an earlier game of the session), then the command line
  if (Session().szMode[0] != '\0') {
    g_choice.id = Session().szMode;
    g_choice.settings = Session().szModeSettings;
    g_choice.seed = Session().nModeSeed;
  }
  const PortOptions *pOpt = PortCommandOptions();
  if (pOpt->szMode[0] == '\0')
    return;
  const PortModeDecl *pDecl = PortModeFind(pOpt->szMode);
  if (pDecl == nullptr) {
    Log("modes: --mode %s names no installed mode", pOpt->szMode);
    return;
  }
  g_choice.id = pDecl->id;
  g_choice.settings = PortModeSettingsWhole(*pDecl, SettingsFromList(*pDecl, pOpt->szModeSettings));
  g_choice.seed = pOpt->szModeSeed[0] != '\0' ? static_cast<uint32_t>(strtoul(pOpt->szModeSeed, nullptr, 10)) : 0;
  Log("modes: --mode %s, settings %s, seed %u", g_choice.id.c_str(), g_choice.settings.c_str(),
      static_cast<unsigned>(g_choice.seed));
}

bool PortModeChosenThisGame() {
  if (gPlayback || gpLoadGameFile != NULL)
    return g_bRecorded;
  if (g_bNet)
    return !g_net.id.empty();
  return !PortModeChosen().id.empty();
}

void PortModeForGame() {
  g_id.clear();
  g_settings.clear();
  g_folder.clear();
  g_seed = 0;
  g_nEnemySlot = -2;  // looked for again once the game's slots are seated
  memset(g_abFingerprint, 0, sizeof g_abFingerprint);
  if (gPlayback || gpLoadGameFile != NULL) {
    if (!g_bRecorded || (g_hxRuleOptions & HX_OPT_MODE) == 0)
      return;
    g_id = g_recordedId;
    g_settings = g_recordedSettings;
    g_seed = g_recordedSeed;
  } else {
    const PortModeChoice c = g_bNet ? g_net : PortModeChosen();
    g_bNet = false;
    if (c.id.empty() || (g_hxRuleOptions & HX_OPT_MODE) == 0)
      return;
    const PortModeDecl *pDecl = PortModeFind(c.id);
    if (pDecl == nullptr) {
      Log("modes: %s is not installed, so the game is played without it", c.id.c_str());
      return;
    }
    g_id = c.id;
    g_settings = PortModeSettingsWhole(*pDecl, c.settings);
    // a seed of 0 is drawn from the game's own, which every machine has
    g_seed = c.seed != 0 ? c.seed : static_cast<uint32_t>(net_get_fixed_seed());
    if (g_seed == 0)
      g_seed = 1;
  }
  if (const PortModeDecl *pDecl = PortModeFind(g_id)) {
    const std::vector<PortMod> &mods = PortMods();
    if (pDecl->nMod >= 0 && pDecl->nMod < static_cast<int>(mods.size()))
      g_folder = mods[pDecl->nMod].folder + mods[pDecl->nMod].data;
  }
  Log("modes: the game plays %s, seed %u, settings %s", g_id.c_str(), static_cast<unsigned>(g_seed),
      g_settings.c_str());
}

const char *PortModeInGame() {
  return g_id.c_str();
}

const PortModeDecl *PortModeInGameDecl() {
  return g_id.empty() ? nullptr : PortModeFind(g_id);
}

const std::string &PortModeSettingsOfGame() {
  return g_settings;
}

uint32_t PortModeSeedOfGame() {
  return g_seed;
}

std::string PortModeSettingOfGame(const std::string &id) {
  PortJson j;
  if (g_settings.empty() || !PortJsonParse(g_settings.c_str(), &j) || j.kind != PortJson::OBJECT)
    return std::string();
  const PortJson &v = j[id.c_str()];
  return v.kind == PortJson::NONE ? std::string() : PortJsonWrite(v);
}

// The slot is looked for once a game, the first time it is asked for, which is after the game's
// slots are seated: every machine reads the same slots, so every machine takes the same player.
int PortModeEnemySlot() {
  if (g_nEnemySlot != -2)
    return g_nEnemySlot;
  g_nEnemySlot = -1;
  const PortModeDecl *pDecl = PortModeInGameDecl();
  if (pDecl == nullptr || !pDecl->enemyPlayer)
    return g_nEnemySlot;
  // the same player the mode's generate was given as its enemy base, by the one rule
  g_nEnemySlot = PortModeEnemyFind();
  Log("modes: %s plays player %d itself, so no classic AI starts for it", g_id.c_str(), g_nEnemySlot);
  return g_nEnemySlot;
}

const char *PortModeFolderOfGame() {
  return g_folder.c_str();
}

const unsigned char *PortModeFingerprintOfGame() {
  return g_abFingerprint;
}

void PortModeFingerprintSet(const unsigned char *pb) {
  memcpy(g_abFingerprint, pb, sizeof g_abFingerprint);
}

void PortModeSave(unsigned char *pbOut) {
  Record r;
  memset(&r, 0, sizeof r);
  lstrcpynA(r.szId, g_id.c_str(), sizeof r.szId);
  if (const PortModeDecl *pDecl = PortModeInGameDecl())
    lstrcpynA(r.szVersion, pDecl->version.c_str(), sizeof r.szVersion);
  memcpy(r.abData, g_abFingerprint, sizeof r.abData);
  r.dwSeed = g_seed;
  lstrcpynA(r.szSettings, g_settings.c_str(), sizeof r.szSettings);
  memcpy(pbOut, &r, sizeof r);
}

bool PortModeRecordBytesRead(int nBytes) {
  return nBytes == PORT_MODE_RECORD_BYTES;
}

bool PortModeRecorded(const unsigned char *pbIn, int nBytes) {
  g_bRecorded = false;
  g_recordedId.clear();
  g_recordedVersion.clear();
  g_recordedSettings.clear();
  g_recordedSeed = 0;
  memset(g_abRecordedFingerprint, 0, sizeof g_abRecordedFingerprint);
  if (pbIn == NULL)
    return true;
  if (!PortModeRecordBytesRead(nBytes))
    return false;
  Record r;
  memcpy(&r, pbIn, sizeof r);
  r.szId[sizeof r.szId - 1] = 0;
  r.szVersion[sizeof r.szVersion - 1] = 0;
  r.szSettings[sizeof r.szSettings - 1] = 0;
  g_recordedId = r.szId;
  g_recordedVersion = r.szVersion;
  g_recordedSettings = r.szSettings;
  g_recordedSeed = r.dwSeed;
  memcpy(g_abRecordedFingerprint, r.abData, sizeof g_abRecordedFingerprint);
  g_bRecorded = !g_recordedId.empty();
  return true;
}

const char *PortModeRecordRefusal() {
  static std::string s_why;
  if (!g_bRecorded)
    return NULL;
  const PortModeDecl *pDecl = PortModeFind(g_recordedId);
  if (pDecl == nullptr) {
    s_why = "it was played with the game mode " + g_recordedId + ", which is not installed";
    return s_why.c_str();
  }
  if (!g_recordedVersion.empty() && pDecl->version != g_recordedVersion) {
    s_why = "it was played with " + g_recordedId + " " + g_recordedVersion + ", and the one installed is " +
            pDecl->version;
    return s_why.c_str();
  }
  return NULL;
}
