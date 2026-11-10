// studio_main.cpp -- Hyperion Studio as a program (DesignThisProgram, design/design_ui.h): its name,
// window title and channel port, its look, its commands and its panels. It stands in for the
// design engine's design_main.cpp in tools/build_studio_x64.py's link, so HyperionStudio.exe is the
// design base with the studio's own parts in place of the design engine's panels.
#include "../design/design_engine.h"
#include "../design/design_ui.h"
#include "studio_ab.h"
#include "studio_marks.h"
#include "studio_readout.h"
#include "studio_model.h"
#include "studio_modes.h"
#include "studio_ui.h"

namespace {

// the channel's port when none is given: two past Hyperion's, beside the design engine's
const int kPort = 27119;
// the default layout's version: a layout saved before a panel it lacks is laid out again
const double kLayoutVersion = 1.0;

void Init(void) {
  StudioModelInit();
  StudioCommandsInit();
  StudioShellInit();
  StudioClaudeInit();
  StudioNewModInit();
  StudioRacesInit();
  StudioArtInit();
  StudioSoundsInit();
  StudioLooksInit();
  StudioActionsInit();
  StudioTestInit();
  StudioChecksInit();
  StudioModPageInit();
  StudioStringsInit();
  StudioBalanceInit();
  StudioAbInit();
  StudioRaceTestInit();
  StudioStatusesInit();
  StudioTilesetsInit();
  StudioLanguagesInit();
  StudioBuildRulesInit();
  StudioScriptsInit();
  StudioModesInit();
  StudioModeShowInit();
  StudioMarksInit();
  StudioReadoutInit();
}

void Poll(void) {
  StudioPoll();
  StudioAbTick();
}

const DesignProgram kStudio = {
    "HyperionStudio", "Hyperion Studio", kPort, kLayoutVersion, true, true,
    Init,             StudioStyle,       StudioMenuBar, StudioPanels, StudioLayout,
    Poll,
};

}  // namespace

const DesignProgram &DesignThisProgram(void) {
  return kStudio;
}
