// design_main.cpp -- the design engine as a program (DesignThisProgram, design_ui.h): its name,
// window title and channel port, and its panels round the game (design_panels.cpp). Hyperion Studio
// is built from the same base with studio/studio_main.cpp in this file's place
// (tools/build_studio_x64.py).
#include "design_engine.h"
#include "design_ui.h"

namespace {

// the default layout's version: a layout saved before a panel it lacks is laid out again
const double kLayoutVersion = 3.0;

const DesignProgram kDesign = {
    "HyperionDesign", "Hyperion design engine", DESIGN_CONTROL_PORT, kLayoutVersion, false, false,
    nullptr,          nullptr,                  DesignMenuBar,       DesignPanels,   DesignDefaultLayout,
    nullptr,
};

}  // namespace

const DesignProgram &DesignThisProgram(void) {
  return kDesign;
}
