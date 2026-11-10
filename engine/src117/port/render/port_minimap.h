// port_minimap.h -- the minimap's look: the looks over 1.17's minimap, a mod's among them
// (port_minimap.cpp).
#ifndef PORT_MINIMAP_H
#define PORT_MINIMAP_H

// The minimap's look (port_minimap.cpp). 1.17 makes the minimap's picture in passes into its
// bitmap: the terrain, the fog over it, the units' dots, the pings; the view's box is drawn over
// it as it is shown. A look gives a dot's colour, and 1.17's, as Settings colour it, stays
// wherever no look says otherwise. The dots' places and their walk stay 1.17's whatever a look
// does: the walk's counts go into the multiplayer game's hash (net_sync.cpp, SaveMinimapCounts).
// A module adds a look with PORT_MINIMAP_LOOK at file scope, and the active mod's hud.json gives
// one ("minimap", docs/mod-data.md).
struct PortMinimapLook {
  const char *pszName;
  // A dot's palette number for a unit of the slot nOwner and the type nType, the colour so far
  // being bColour (1.17's as Settings show it, or an earlier look's); bColour leaves it.
  unsigned char (*DotColour)(int nOwner, int nType, unsigned char bColour);
};

// Adds a look, asked after those added before it; false when the list is full. Looks add
// themselves as the program starts, in no fixed order between files, so a look leans on no
// other's colour.
bool PortMinimapAdd(const PortMinimapLook *pLook);
#define PORT_MINIMAP_LOOK(look) static const bool s_bMinimap_##look = PortMinimapAdd(&(look))

// The dot pass's (minimap.cpp): a dot's colour through every look in turn.
unsigned char PortMinimapDotColour(int nOwner, int nType, unsigned char bColour);

// The "minimap" of the active mod's hud.json, as each console loads (port_console.cpp): dots
// in a colour of the mod's by the owner's relation to the player.
struct PortJson;
void PortMinimapHud(const PortJson &minimap);

// ---- a mod's marks on the minimap -----------------------------------------------------------
//
// A mark a mod set with hx.area, drawn on the minimap's own picture as well as on the ground,
// for the marks whose minimap block asks for it (port_mapmark.h). The picture is palette
// indices with no alpha, so a mark there is its colour's nearest palette entry and an opacity
// is drawn as a part of the pixels rather than a weaker colour. Nothing fades: a perimeter
// shows on the minimap whole, whatever the ground's light is doing.
//
// The pass runs after the terrain and the fog and before the unit dots (minimap.cpp,
// draw_minimap_body), so a mark never hides a dot and nothing here touches the dots' own walk,
// whose counts the multiplayer game's hash takes.
void PortMinimapMarks(unsigned char *pbPixels, int nWidth, int nHeight, int nScale);

// The minimap's picture as the scale ladder settles it (minimap.cpp, sub_46D850): its size in
// pixels and the map pixels one of them covers. The size and the scale are the minimap's own
// and no other file can read them, so the minimap hands them over as it sizes itself; a mod
// asks for them with hx.minimap and false is the answer before a game's minimap is made.
void PortMinimapSized(int nWidth, int nHeight, int nScale);
bool PortMinimapSize(int *pnWidth, int *pnHeight, int *pnScale);

#endif
