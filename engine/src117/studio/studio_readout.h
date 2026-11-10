// studio_readout.h -- the Looks workspace's Unit readout page (studio_readout.cpp).
#ifndef STUDIO_READOUT_H
#define STUDIO_READOUT_H

// readout.json as a diff from classic's own panel: its parts and their parameters, the lines it
// adds, its ranks, its effects, and the panel as the game is drawing it now.
void StudioReadoutPage(float fW);

// Registers the page's channel commands. Called once as the studio starts.
void StudioReadoutInit(void);

#endif
