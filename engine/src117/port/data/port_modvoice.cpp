// port_modvoice.cpp -- a mod's words for its units' voice lines and the least time between two
// plays of a sound (port_modvoice.h). A subtitle goes on the game's message line, the one the
// game's own "Not enough minerals" uses, with the speaker's name before it, for the local player's
// units only; it is drawn as that line is and goes as it goes, so a replay or another player sees
// nothing of it.
#include "port_modvoice.h"

#include <string>
#include "../../h/CUnitMisc.h"  // get_unit_name_from_type
#include "../../h/mapmsg.h"     // map_msg_target
#include "../../gen/globals117.h"  // g_LocalNationID
#include "../platform/port_json.h"
#include "../platform/port_settings.h"
#include "../tools/port_control.h"  // PortControlCommand
#include "port_moddata.h"  // ModTypeByName
#include "port_sound.h"

void ModVoiceSpoken(int nSound, int nUnitType, int nPlayer) {
  if (nSound <= 0 || nPlayer != static_cast<int>(g_LocalNationID))
    return;
  const char *pszText = ModSoundSubtitle(static_cast<unsigned>(nSound));
  if (pszText == nullptr)
    return;
  double dOn = 0.0;
  PortSettingNumber("hyperion", "unitSubtitles", &dOn);
  if (dOn == 0.0)
    return;
  std::string line = pszText;
  const char *pszName = nUnitType >= 0 ? get_unit_name_from_type(static_cast<WORD>(nUnitType)) : nullptr;
  if (pszName != nullptr && pszName[0] != '\0')
    line = std::string(pszName) + ": " + line;
  map_msg_target(line.c_str());
}

namespace {

bool CmdSounds(const PortJson &j, std::string *pAnswer) {
  std::string played;
  if (j["play"].kind != PortJson::NONE) {
    const int n = j["play"].kind == PortJson::STRING ? ModTypeByName("sounds", j["play"].s.c_str()) : static_cast<int>(j["play"].Num(-1));
    if (n <= 0) {
      *pAnswer = "play names no sound";
      return false;
    }
    played = ",\"played\":" + std::to_string(n) + ",\"started\":" + (PortSoundPlay(n) ? "true" : "false");
  }
  int anId[8], anPriority[8];
  const int nOn = PortSoundSlots(anId, anPriority);
  std::string slots;
  for (int i = 0; i < 8; ++i)
    slots += std::string(i ? "," : "") + "{\"sound\":" + std::to_string(anId[i]) + ",\"priority\":" + std::to_string(anPriority[i]) + "}";
  *pAnswer = "\"playing\":" + std::to_string(nOn) + ",\"slots\":[" + slots + "]" + played;
  return true;
}

}  // namespace

void ModVoiceForGame(void) {
  static bool s_bCommand = false;
  if (!s_bCommand) {
    PortControlCommand("sounds", CmdSounds);
    s_bCommand = true;
  }
}

unsigned PortSoundGap(int nSound) {
  // 80 ms, the classic game's for every sound, unless the mod gives the sound its own
  const int n = nSound >= 0 ? ModSoundGap(static_cast<unsigned>(nSound)) : -1;
  return n >= 0 ? static_cast<unsigned>(n) : 0x50u;
}
