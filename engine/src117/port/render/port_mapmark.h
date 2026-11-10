// port_mapmark.h -- the marks a mod's scripts draw on the map (port_mapmark.cpp): a circle, a
// ring, a rectangle, a polygon or a line, placed in map pixels, so each one sits on the ground
// and moves and scales with the camera. A mod sets a mark and clears it by name (hx.area in
// port_modlua.cpp), and this draws them over the game as a layer of the overlay.
//
// A mark can be mostly hidden and show as a unit comes near it, which is how a mod marks ground
// a player must not cross: the fade says whose units reveal it, how far away they begin to, how
// far the opacity rises, and whether the whole mark shows or only the part of its edge near the
// unit. Only units the player watching can see reveal a mark, so nothing shows through the fog.
//
// How a mark is drawn past its own edge and inside is a look: blocks of light a mod's own files
// stack on it, each with its values (docs/mod-data.md, "The looks a mark wears"). A field stands
// up the screen from the edge in bands that fade toward the top; cells drift in it along the
// edge; the foot is the edge itself with a glow spilling onto the ground; a tear is torn in the
// field where a unit came through; and a light on the ground says which of the player's own
// units are outside. The engine knows no look by name: a script names one of the mod's, and a
// mark's own fields win over what the look says.
//
// The field, the cells and the tear stand above the ground, so they are drawn over the units
// while the foot is painted under them: a unit inside a wall of light stands behind it rather
// than in front of it.
//
// A mark can also answer a unit crossing its edge: the units it is lit by are watched frame to
// frame, and one that has changed sides sets off a ripple running away along the edge from the
// place, in the warning colour where the unit was going out. A unit seen for the first time
// raises nothing, so units already standing about when a mark is set do not all cross at once.
//
// Presentation only. Nothing here is read by the simulation, saved or sent, and a game plays and
// records the same with the marks or without them. Each seat draws its own, since the fade is
// measured against what that player sees; a replay and a watched seat draw them because the
// mod's scripts run there too and set them again.
#ifndef PORT_MAPMARK_H
#define PORT_MAPMARK_H

#include <string>
#include <vector>

// A mark's shape. A circle and a rectangle have a fill and an edge; a ring is an edge of its own
// width; a polygon is the closed shape its points make; a line is one stroke.
enum { MAP_MARK_CIRCLE, MAP_MARK_RING, MAP_MARK_RECT, MAP_MARK_POLYGON, MAP_MARK_LINE, MAP_MARK_SHAPES };

// Whose units reveal a mark: the player watching, their allies as well, every player's, or the
// players a mod names.
enum { MAP_MARK_WHO_ME, MAP_MARK_WHO_ALLIES, MAP_MARK_WHO_ALL, MAP_MARK_WHO_LIST, MAP_MARK_WHOS };

// Where a mark is drawn. On the ground it is painted into the map itself, so the units and
// buildings stand on it and it takes the ground's own light; over the game it is drawn above
// everything on the map, which reads at any zoom and on any ground. The ground wants the full
// colour the GPU presenter draws by default; where it is not drawn (the exact classic colours,
// SDL's renderer) a ground mark is drawn over the game instead, so it is never lost.
enum { MAP_MARK_GROUND, MAP_MARK_OVER, MAP_MARK_LAYERS };

// What is drawn under one of the player's own units standing outside a mark: nothing, or a light
// on the ground beneath it, beating, in a warning colour within the band outside the edge and a
// danger colour past it. ring and tether are taken and draw as none for now.
enum { MAP_MARK_OUT_NONE, MAP_MARK_OUT_TINT, MAP_MARK_OUT_RING, MAP_MARK_OUT_TETHER, MAP_MARK_OUTS };

// What a mark does when a unit crosses its edge: nothing, or a ripple that runs away along the
// edge from where it happened, brightest at the place itself. One going out is answered in the
// warning colour. flash, stamp and arc are taken and draw as none for now.
enum { MAP_MARK_CROSS_NONE, MAP_MARK_CROSS_RIPPLE, MAP_MARK_CROSS_FLASH, MAP_MARK_CROSS_STAMP,
       MAP_MARK_CROSS_ARC, MAP_MARK_CROSSES };

// How a mark's light falls off between a unit and the fade's distance: smooth eases away, even
// falls in a straight line, and sharp holds full past half the distance and then drops, which
// draws a mark that is dark until a unit is nearly on it.
enum { MAP_MARK_CURVE_SMOOTH, MAP_MARK_CURVE_EVEN, MAP_MARK_CURVE_SHARP, MAP_MARK_CURVES };

// How near a unit the mark shows when nothing else is said: a third of a screen at the classic
// view, which is far enough to warn a player and near enough to stay out of the way.
#define PORT_MAP_MARK_NEAR 192.0

// ---- the blocks a look is made of -----------------------------------------------------------
//
// Every length is in map pixels, which the camera scales, and every opacity is a part of the
// light at that point of the edge, so a block fades with the mark rather than apart from it. The
// values here are the defaults a block takes where a look leaves one out.

// The field: light standing up the screen from the edge, in bands that fade toward the top. Its
// height is the look's own, which the cells and the tear are measured in as well, and its face
// is the wall the cells are drawn on.
//
// The wall stands on an edge that may run at any angle across the map, and the game's view is
// oblique, so the face is seen fully where the edge runs across the screen and edge-on where it
// runs up the screen. A face seen exactly edge-on would be drawn as a line of no width at all,
// since going north and standing tall are the same direction on this screen: fLean leans the
// top of the wall out along the edge's own normal, which gives that face its width back and
// rounds a closed mark off like a bowl, and fEdgeOn says what such a face is worth to look at.
struct PortMarkField {
  bool bOn = false;
  double fHigh = 26.0;    // how high it stands
  int nBands = 6;         // the bands it is drawn in
  double fAlpha = 0.42;   // the band at the foot's opacity
  double fFade = 1.0;     // how much of that the band at the top has lost
  bool bAdd = true;       // drawn so that the bands add up where they overlap
  double fLean = 0.35;    // how far the top leans out along the edge's normal, in its heights
  double fEdgeOn = 1.20;  // the face's brightness edge-on, as a part of its brightness face-on
};

// The cells: outlines drifting in the field along the edge, in rows at their own heights, each
// flickering on a beat of its own and taking the warning colour where a unit went out.
struct PortMarkCells {
  bool bOn = false;
  int nSides = 6;         // the outline's corners: a hexagon is all a look may ask for now
  double fStep = 13.0;    // the edge between one set of cells and the next
  double fStagger = 0.5;  // how far along that step each row after the first is moved
  double fRadius = 4.2;   // the outline's radius, and how much it is squashed
  double fSquash = 0.8;
  double fWidth = 1.0;    // the line's width
  double fAlpha = 0.7;    // and its opacity
  double fLeast = 0.12;   // the light a stretch of edge needs before its cells are drawn
  double fBase = 0.55;    // the flicker: the middle it swings about, and how far
  double fSwing = 0.45;
  double fRate = 3.1;     // its beat by the second, by the edge gone along, and by the row
  double fAlong = 0.37;
  double fRow = 1.7;
  double fWarm = 0.2;     // the warmth past which a cell takes the warning colour
  std::vector<double> rows = {0.30, 0.72};  // each row's height, as a part of the field's
};

// The foot: the mark's own edge with a glow spilling onto the ground under it. It is drawn where
// the mark's layer says, as the edge it is.
struct PortMarkFoot {
  bool bOn = false;
  double fGlow = 4.0;    // how many of the edge's widths the glow reaches
  double fAlpha = 0.22;  // and its opacity, as a part of the edge's
};

// The tear: a column of light in the field where a unit came through, widening as it fades. It
// stands at the place and leans the way the unit was going, so one going out leans outward and
// one coming in leans over the inside, whatever angle the edge runs at there.
struct PortMarkTear {
  bool bOn = false;
  double fLife = 0.9;   // the seconds it lasts
  double fWide = 10.0;  // how wide it begins, and how much wider it is at the end
  double fGrow = 30.0;
  double fHigh = 1.6;   // how tall it is, in the field's heights
  double fAlpha = 0.9;  // its opacity at the foot, falling to nothing at the top
  double fLean = 0.5;   // how far it leans the way the unit went, in its own heights
};

// The ripple a crossing sends along the edge: a wave running away from the place, and a flash at
// the place itself that dies sooner. Nothing draws it of itself -- it is light the edge and the
// other blocks take, which is why it has no layer.
struct PortMarkRipple {
  double fSpeed = 384.0;  // how fast the wave travels along the edge
  double fWide = 46.0;    // how wide it is, and the seconds it lasts
  double fLife = 1.6;
  double fFlash = 50.0;   // the flash at the place: how wide it is, and its own seconds
  double fFlashLife = 0.5;
  double fSide = 0.5;     // how far the flash leans to the side the unit was heading for
};

// The light under one of the player's own units standing outside a closed mark: the warning
// colour within the band outside the edge, the danger colour past it, each beating on its own.
struct PortMarkOutside {
  int nStyle = MAP_MARK_OUT_NONE;
  int nLayer = MAP_MARK_GROUND;
  double fBand = 0.0;     // how far outside the edge the warning reaches
  double fRadius = 26.0;  // how far the light reaches under the unit
  double fAlpha = 0.55;   // its opacity at the middle
  double fBase = 0.65;    // the beat: the middle it swings about, and how far
  double fSwing = 0.35;
  double fBeat = 4.0;     // its rate within the band, and past it
  double fPast = 9.0;
  unsigned int nNear = 0xFFB347u;  // the warning colour, and the danger colour past the band
  unsigned int nFar = 0xE0453Au;
};

// How a mark shows on the minimap's own picture, which is a palettized bitmap with no alpha
// that redraws on a timer of its own: so a mark there is drawn in the palette entry nearest its
// colour, its lengths are the minimap's own pixels rather than the map's, and nothing of it
// fades or beats -- a perimeter shows on the minimap whole, whatever the ground's light is
// doing. Nothing is drawn without a block, and every renderer draws this one, SDL's included.
struct PortMarkMini {
  bool bOn = false;
  double fFill = 0.0;    // the inside's opacity, as a part of its pixels
  double fEdge = 1.0;    // the edge's
  double fWidth = 1.0;   // the edge's width, in minimap pixels
  double fDash = 0.0;    // a dash and its gap, in minimap pixels; 0 draws the edge whole
  int nPlayer = -1;      // a colour of its own: a player's, else nRgb, else the mark's own
  unsigned int nRgb = 0xFFFFFF;
  bool bRgb = false;
};

// What a look asks of the mark's own values, as a mark drawn in that look wants them: a mark
// takes these when it puts the look on, and anything a script says itself wins over them. The
// values here are a mark's own defaults, so a look that asks for nothing changes nothing.
struct PortMarkAsks {
  double fFill = 0.0, fEdge = 1.0, fWidth = 2.0, fDash = 0.0, fSoft = 0.0;
  double fPulse = 0.0, fDepth = 0.30;
  int nCurve = MAP_MARK_CURVE_SMOOTH;
  int nCross = MAP_MARK_CROSS_NONE;
};

// A look: the blocks a mark wears, and the colour they answer a unit on its way out in. A mod's
// files name the blocks and their values and a script asks for the look by name.
struct PortMarkLook {
  unsigned int nWarn = 0xFFB347u;
  PortMarkAsks asks;
  PortMarkField field;
  PortMarkCells cells;
  PortMarkFoot foot;
  PortMarkTear tear;
  PortMarkRipple ripple;
  PortMarkOutside outside;
  PortMarkMini mini;
  // whether any block stands above the ground, which the ground's paint cannot draw: such a mark
  // is painted for its foot and drawn over the units for the rest
  bool Stands() const { return field.bOn || cells.bOn || tear.bOn; }
};

// One mark. The lengths are map pixels, which the camera scales; the opacities are 0 to 1.
struct PortMapMark {
  std::string id;             // the scripts' name for it, which setting again replaces
  int nShape = MAP_MARK_CIRCLE;
  double x = 0, y = 0;        // a circle's and a ring's centre, a rectangle's top left, a line's first point
  double w = 0, h = 0;        // a rectangle's size
  double r = 0;               // a circle's and a ring's radius
  double x2 = 0, y2 = 0;      // a line's second point
  std::vector<double> points; // a polygon's x and y in turn
  int nPlayer = -1;           // a player's own colour, -1 for nRgb
  unsigned int nRgb = 0xFFFFFF;
  // Who sees it: a bit a player (1 << slot), every player without one. A mark of one player's
  // own ground, where a mode gives each their own, is drawn on that player's screen alone; a
  // viewer of no side (an observer, a replay watched whole) sees every mark.
  unsigned int nPlayers = 0xFFFFFFFFu;
  double fFill = 0.0;         // the inside's opacity, 0 for none
  double fEdge = 1.0;         // the edge's
  double fWidth = 2.0;        // the edge's width in map pixels; a ring's is its own
  double fDash = 0.0;         // a dash and its gap, in map pixels; 0 draws the edge whole
  double fSoft = 0.0;         // a glow under the edge, as wide as this many of its widths
  double fPulse = 0.0;        // seconds a beat, over which the opacity swells and falls; 0 none
  // how deep the swell goes: the opacity falls to this much less than its own at the beat's
  // lowest. The swell travels along the edge, so a long one shows it as a wave running round.
  double fDepth = 0.30;
  // The fade. Without it the mark is drawn at the opacities above.
  bool bFade = false;
  int nWho = MAP_MARK_WHO_ME;
  std::vector<int> who;       // MAP_MARK_WHO_LIST: the players whose units reveal it
  double fNear = PORT_MAP_MARK_NEAR;  // the distance at which a unit begins to reveal it
  int nCurve = MAP_MARK_CURVE_SMOOTH;  // how the light falls off over that distance
  double fMost = 1.0;         // the opacity it reaches, as a part of its own
  double fLeast = 0.0;        // and the one it rests at with nothing near
  bool bGlow = true;          // only the part of the edge near a unit shows; false: all of it together
  int nLayer = MAP_MARK_GROUND;  // an area is a piece of the map, so it is painted on it
  int nCross = MAP_MARK_CROSS_NONE;  // what a unit crossing the edge stirs up
  PortMarkLook look;                 // and the blocks of light it wears, from the mod's own files
};

// A mark set, which replaces the one of its name, or cleared. A game shows 32 at most; the 33rd
// is passed over. A mark cleared fades away over half a second rather than going at once, so a
// perimeter lifted leaves the screen as a perimeter would; one set again while it fades comes
// back. A mark's own pulse keeps its beat through a change, so a script may set the same mark
// every tick to move it or grow it.
void PortMapMarkSet(const PortMapMark &mark);
void PortMapMarkClear(const char *pszId);
void PortMapMarkClearAll(void);  // a game starting or loaded (ModLuaForGame)

// The pass, before the sprites (CSprite::DrawAll): the marks of the ground layer recorded as
// primitives of the scene, which the sprites then cover (SCENE_PRIM_MARK, port_scene.h). Where
// the scene is not painting them, the overlay draws them over the game instead.
void PortMapMarkRecord(void);

// A mark as the minimap draws it (port_minimap.cpp): its shape in map pixels, with the colour
// and the widths of its own minimap block and nothing of the fade. The places and the lengths
// are the map's, which the minimap scales itself, while the edge's width and its dash are the
// block's own minimap pixels: an edge asked for in map pixels would be a fraction of one there
// and draw nothing. points is the mark's own list and stands as long as the marks do, which is
// to the end of the frame.
struct PortMarkMinimap {
  int nShape;
  double x, y, w, h, r, x2, y2;
  const std::vector<double> *points;
  int nPlayer;
  unsigned int nRgb;
  double fWidth, fFill, fEdge, fDash;
};
// The marks for the minimap's pass, and how many: those with a minimap block, in a game, that
// the player watching is among the players of and that have not finished fading away. None
// outside a game, where the marks of the last one are still standing until the next begins, so
// the lobby's map preview draws none of them.
int PortMapMarkOnMinimap(std::vector<PortMarkMinimap> *pOut);

// A shape, a fade's players or a layer by the name a script writes; -1 for a name there is
// none of.
int PortMapMarkShapeOf(const char *pszName);
int PortMapMarkWhoOf(const char *pszName);
int PortMapMarkLayerOf(const char *pszName);
int PortMapMarkCurveOf(const char *pszName);
int PortMapMarkCrossingOf(const char *pszName);
int PortMapMarkOutsideOf(const char *pszName);
// A colour a script writes, "#rrggbb" or "rrggbb"; false when it is neither.
bool PortMapMarkRgbOf(const char *pszName, unsigned int *pnRgb);

// What a mark is, for the live channel and the scripts' log: how many there are, and one of
// them as a line.
int PortMapMarkCount(void);
std::string PortMapMarkLine(int i);

#endif
