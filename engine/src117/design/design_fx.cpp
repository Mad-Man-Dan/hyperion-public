// design_fx.cpp -- the effects of design/fx/ in the engine: each one a Luau script (design/fx/luau/),
// a pure function of its time and the places it is handed, run every frame the window draws,
// its shapes put straight into the frame's list of primitives (design_draw.cpp). The design's
// study (design/fx/fx-perf.js) set how: the numbers a script leans on most, the hash, the noise
// and the eases' inputs, are the host's, ported from fxcore.js so f.rand gives the studio's
// numbers; places are Luau's vectors, which cost no allocation; and a shape crosses as one call
// that writes its primitive, never as a table kept for later. The clock is the game's: an
// effect stops with a paused game and steps with a stepped one.
//
// This first stage draws the shapes that need no more than the frame: sparks, glows and lights,
// rings and discs, beams, bolts and traces, stars, shards and the flash, all over the map. Decals,
// distortions, sprite ops, trails, tethers, telegraphs, sound and the layers under the units are
// counted and left out; design_status says how many.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "CUnit.h"
#include "../port/platform/port_json.h"
#include "../port/render/port_view.h"  // PortViewCameraMiddle
#include "../port/tools/port_benchinfo.h"  // PortBenchUnitRead
#include "../port/tools/port_control.h"
#include "design_engine.h"
#include "design_hooks.h"

namespace {

// ---- numbers: fxcore.js's hash and noise ----------------------------------------------------------

// JavaScript's x | 0 then >>> 0: toward zero, then the low 32 bits.
unsigned I32(double x) {
  double n = x < 0 ? ceil(x) : floor(x);
  n = fmod(n, 4294967296.0);
  if (n < 0)
    n += 4294967296.0;
  return static_cast<unsigned>(n);
}

unsigned Pcg(unsigned v) {
  const unsigned s = v * 747796405u + 2891336453u;
  const unsigned sh = ((s >> 28) + 4u) & 31u;
  const unsigned w = ((s >> sh) ^ s) * 277803737u;
  return (w >> 22) ^ w;
}

double Rand3(double a, double b, double c) {
  const unsigned h = Pcg((I32(a) * 73856093u) ^ Pcg((I32(b) * 19349663u) ^ Pcg(I32(c))));
  return h / 4294967296.0;
}

double Lerp(double a, double b, double x) {
  return a + (b - a) * x;
}

double Clamp01(double x) {
  return x < 0 ? 0 : x > 1 ? 1 : x;
}

double Noise(double x, double y, double z) {
  const double xi = floor(x), yi = floor(y), zi = floor(z);
  const double xf = x - xi, yf = y - yi, zf = z - zi;
  const double u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf), w = zf * zf * (3 - 2 * zf);
  auto h = [&](int i, int j, int k) { return Rand3(xi + i, yi + j, (zi + k) * 7919 + 13); };
  const double a = Lerp(Lerp(h(0, 0, 0), h(1, 0, 0), u), Lerp(h(0, 1, 0), h(1, 1, 0), u), v);
  const double b = Lerp(Lerp(h(0, 0, 1), h(1, 0, 1), u), Lerp(h(0, 1, 1), h(1, 1, 1), u), v);
  return Lerp(a, b, w);
}

double Fbm(double x, double y, double z) {
  return (Noise(x, y, z) * 0.5 + Noise(x * 2.03, y * 2.03, z * 2.03 + 5.1) * 0.25 +
          Noise(x * 4.1, y * 4.1, z * 4.1 + 9.7) * 0.125) /
         0.875;
}

// ---- places -------------------------------------------------------------------------------------

struct V3 {
  double x, y, z;
};
V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Scale(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Len(V3 a) { return sqrt(Dot(a, a)); }
V3 Norm(V3 a) {
  const double l = Len(a);
  return l == 0 ? a : Scale(a, 1 / l);
}
V3 Mix(V3 a, V3 b, double x) { return {Lerp(a.x, b.x, x), Lerp(a.y, b.y, x), Lerp(a.z, b.z, x)}; }
void Perp(V3 n, V3 *pU, V3 *pV) {
  n = Norm(n);
  const V3 a = fabs(n.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
  *pU = Norm(Cross(a, n));
  *pV = Cross(n, *pU);
}
V3 RotX(V3 p, double a) { const double c = cos(a), s = sin(a); return {p.x, p.y * c - p.z * s, p.y * s + p.z * c}; }
V3 RotY(V3 p, double a) { const double c = cos(a), s = sin(a); return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c}; }
V3 RotZ(V3 p, double a) { const double c = cos(a), s = sin(a); return {p.x * c - p.y * s, p.x * s + p.y * c, p.z}; }

// ---- ramps: fxcore.js's, dark to hot ------------------------------------------------------------

struct Ramp {
  const char *pszName;
  unsigned anRgb[6];
};
const Ramp kRamps[] = {
    {"fire", {0x140400, 0x521000, 0xa82800, 0xec6410, 0xffac2c, 0xfff0a0}},
    {"plasma", {0x030820, 0x0a2470, 0x1660cc, 0x38acff, 0xa4e8ff, 0xffffff}},
    {"psi", {0x080418, 0x240c60, 0x5228c8, 0x8a6cff, 0xcbb6ff, 0xffffff}},
    {"frost", {0x04101c, 0x0a3456, 0x2676ac, 0x66c0ea, 0xc2f0ff, 0xffffff}},
    {"venom", {0x031404, 0x0a420e, 0x26861e, 0x6ad02e, 0xc4f270, 0xf8ffd2}},
    {"red", {0x120000, 0x4c0006, 0xac0c1a, 0xff2c3c, 0xff9aa2, 0xffffff}},
    {"gold", {0x2c1403, 0x6e3a06, 0xb8740e, 0xeaa81c, 0xffd64a, 0xfff3b8}},
    {"violet", {0x1c0628, 0x4c1166, 0x8c2eb4, 0xc35ee8, 0xeba6ff, 0xfff0ff}},
    {"void", {0x000000, 0x14001e, 0x3c0054, 0x8c1aa4, 0xe264ff, 0xffd6ff}},
    {"warp", {0x021010, 0x063c3c, 0x0c7c7c, 0x22c4bc, 0x94fff0, 0xffffff}},
    {"smoke", {0x0a0a0c, 0x202024, 0x3a3a40, 0x5a5a62, 0x828088, 0xaaa8ae}},
    {"ash", {0x0c0806, 0x241a14, 0x3e2e22, 0x5e4632, 0x86684a, 0xb09070}},
    {"white", {0x000000, 0x2a2a2a, 0x5c5c5c, 0x999999, 0xd4d4d4, 0xffffff}},
};

// A ramp's six colours, 0 to 1; or a team's, dark to a pale tint of its colour (teamRamp).
struct Colours {
  float af[6][3];
};
Colours RampColours(const std::string &name, const float *pafTeam) {
  Colours c;
  if (name == "team") {
    static const float kStops[6] = {0.06f, 0.22f, 0.5f, 0.85f, 1.0f, 1.0f}, kTint[6] = {0, 0, 0, 0, 0.45f, 0.8f};
    for (int i = 0; i < 6; ++i)
      for (int k = 0; k < 3; ++k)
        c.af[i][k] = static_cast<float>(Lerp(pafTeam[k] * kStops[i], 1.0, kTint[i]));
    return c;
  }
  const Ramp *pRamp = &kRamps[0];
  for (const Ramp &r : kRamps)
    if (name == r.pszName)
      pRamp = &r;
  for (int i = 0; i < 6; ++i)
    for (int k = 0; k < 3; ++k)
      c.af[i][k] = ((pRamp->anRgb[i] >> (16 - 8 * k)) & 255) / 255.0f;
  return c;
}

// ---- the effects and the ones playing -----------------------------------------------------------

struct Effect {
  std::string id, name, slot, ramp, file;
  double fLength = 1.0;
  bool bLoop = false;
  int nDraw = LUA_NOREF;    // the script's function (f, p)
  int nParams = LUA_NOREF;  // its params as given, each a value or a spec whose first item is it
};

struct Playing {
  int nId;
  size_t nEffect;
  unsigned nStartFrame;
  bool bLoop;
  double fSeed;
  int nUnit = -1;     // the unit it follows, at its place
  int nToUnit = -1;   // the unit it is aimed at
  float afTeam[3];
  int nF = LUA_NOREF;  // its f, made once
  int nP = LUA_NOREF;  // its p
};

lua_State *g_L;
std::vector<Effect> g_effects;
std::vector<Playing> g_playing;
std::vector<std::string> g_errors;
int g_nNextId = 1;
// the moment being drawn: who asked, through which camera, into which list
const Playing *g_pNow;
const Effect *g_pNowEffect;
const DesignCamera *g_pCam;
std::vector<DesignPrim> *g_pOut;
// what the last frame cost, and what this stage left out
double g_dMs;
unsigned g_nShapes, g_nSkipped;
LARGE_INTEGER g_deadline;
// the game's frame and when it came, so a moment between two of them is smooth
unsigned g_nLastFrame;
LARGE_INTEGER g_lastFrameAt;

const double kFrame = 0.042;  // classic's frame at Fastest
const double kRepeatRest = 0.6;  // the rest before an effect that plays once starts again, as the studio repeats it
const double kScriptMs = 4.0;  // an effect's most a frame before it is stopped
// the effects in the game's pixels: hard edges, a dither, few colours, and time in held frames
DesignPixel g_pixel = {1.0f, 1.0f, 6.0f, 8.0f, 2.0f};
// a flash of the whole screen, at most this strong; none unless asked
float g_fFlash = 0.0f;

// ---- reading what a script hands over -----------------------------------------------------------

double Num(lua_State *L, int o, const char *pszKey, double d) {
  if (!lua_istable(L, o))
    return d;
  lua_rawgetfield(L, o, pszKey);
  const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : d;
  lua_pop(L, 1);
  return v;
}

bool Has(lua_State *L, int o, const char *pszKey) {
  if (!lua_istable(L, o))
    return false;
  lua_rawgetfield(L, o, pszKey);
  const bool b = !lua_isnil(L, -1);
  lua_pop(L, 1);
  return b;
}

std::string Str(lua_State *L, int o, const char *pszKey, const char *pszDefault) {
  if (!lua_istable(L, o))
    return pszDefault;
  lua_rawgetfield(L, o, pszKey);
  const char *psz = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
  std::string s = psz != NULL ? psz : pszDefault;
  lua_pop(L, 1);
  return s;
}

bool Bool(lua_State *L, int o, const char *pszKey, bool bDefault) {
  if (!lua_istable(L, o))
    return bDefault;
  lua_rawgetfield(L, o, pszKey);
  const bool b = lua_isnil(L, -1) ? bDefault : lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return b;
}

// A place: a vector, or a list {x, y, z}.
bool Place(lua_State *L, int idx, V3 *pOut) {
  if (lua_isvector(L, idx)) {
    const float *p = lua_tovector(L, idx);
    *pOut = {p[0], p[1], p[2]};
    return true;
  }
  if (lua_istable(L, idx)) {
    double a[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
      lua_rawgeti(L, idx, i + 1);
      a[i] = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
      lua_pop(L, 1);
    }
    *pOut = {a[0], a[1], a[2]};
    return true;
  }
  return false;
}

V3 NeedPlace(lua_State *L, int idx) {
  V3 v;
  if (!Place(L, idx, &v))
    luaL_error(L, "a place is a vector: vector.create(x, y, z)");
  return v;
}

bool OptPlace(lua_State *L, int o, const char *pszKey, V3 *pOut) {
  if (!lua_istable(L, o))
    return false;
  lua_rawgetfield(L, o, pszKey);
  const bool b = Place(L, -1, pOut);
  lua_pop(L, 1);
  return b;
}

// A shape's colour along its ramp: its own ramp, the effect's, or a colour of its own.
struct Look {
  Colours ramp;
  bool bColour;
  float afColour[3];
  double fHeat, fAlpha;
  bool bAdd;
};

Look LookOf(lua_State *L, int o, bool bAddDefault) {
  Look k;
  k.ramp = RampColours(Str(L, o, "ramp", g_pNowEffect->ramp.c_str()), g_pNow->afTeam);
  k.fHeat = Num(L, o, "heat", 1.0);
  k.fAlpha = Num(L, o, "a", 1.0);
  const std::string blend = Str(L, o, "blend", "");
  k.bAdd = blend.empty() ? bAddDefault : blend != "normal";
  const std::string colour = Str(L, o, "color", "");
  unsigned rgb = 0;
  k.bColour = colour.size() == 7 && colour[0] == '#' && sscanf(colour.c_str() + 1, "%x", &rgb) == 1;
  for (int i = 0; i < 3; ++i)
    k.afColour[i] = ((rgb >> (16 - 8 * i)) & 255) / 255.0f;
  return k;
}

// The colour at a heat, as rampAt blends it.
void At(const Look &k, double heat, float fAlpha, float *pafOut) {
  if (k.bColour) {
    for (int i = 0; i < 3; ++i)
      pafOut[i] = k.afColour[i];
  } else {
    const double x = Clamp01(heat) * 5;
    const int i = x >= 4 ? 4 : static_cast<int>(floor(x));
    const double f = x - i;
    for (int c = 0; c < 3; ++c)
      pafOut[c] = static_cast<float>(Lerp(k.ramp.af[i][c], k.ramp.af[i + 1][c], f));
  }
  pafOut[3] = fAlpha < 0 ? 0 : fAlpha > 1 ? 1 : fAlpha;
}

// ---- into the frame's list ----------------------------------------------------------------------

void Project(V3 q, float *pfX, float *pfY) {
  DesignProject(*g_pCam, q.x, q.y, q.z, pfX, pfY);
}

// A direction on the map as it moves on the screen, in the target's pixels: the game's camera
// looks straight down, with height going up the screen.
void Delta(V3 d, float *pfX, float *pfY) {
  *pfX = static_cast<float>(d.x * g_pCam->fPerMap);
  *pfY = static_cast<float>((d.y - d.z) * g_pCam->fPerMap);
}

// The two directions a flat shape is drawn along: camera, ground (squashed 3 in 5 as classic's art
// is), map, up, or square to a direction.
void Basis(lua_State *L, int o, const char *pszDefault, V3 *pU, V3 *pV) {
  V3 dir;
  if (OptPlace(L, o, "face", &dir)) {
    Perp(dir, pU, pV);
    return;
  }
  const std::string face = Str(L, o, "face", pszDefault);
  *pU = {1, 0, 0};
  if (face == "ground")
    *pV = {0, 0.6, 0};
  else if (face == "map")
    *pV = {0, 1, 0};
  else  // camera and up: the screen's up is height
    *pV = {0, 0, 1};
}

DesignPrim *Push(int nKind, bool bAdd) {
  g_pOut->emplace_back();
  DesignPrim *p = &g_pOut->back();
  DesignPrimClear(p, nKind, bAdd, g_pCam->afMap);
  ++g_nShapes;
  return p;
}

// A circle's axes in the target from its basis and radius; false when it is seen edge on.
bool Axes(DesignPrim *p, V3 u, V3 v, double r) {
  Delta(Scale(u, r), &p->axes[0], &p->axes[1]);
  Delta(Scale(v, r), &p->axes[2], &p->axes[3]);
  const float det = p->axes[0] * p->axes[3] - p->axes[2] * p->axes[1];
  return fabsf(det) > 0.05f;
}

void Glow(V3 q, double r, V3 u, V3 v, const Look &k, double kk) {
  if (r <= 0 || k.fAlpha <= 0.003)
    return;
  DesignPrim *p = Push(DESIGN_PRIM_GLOW, k.bAdd);
  Project(q, &p->at[0], &p->at[1]);
  if (!Axes(p, u, v, r)) {
    g_pOut->pop_back();
    return;
  }
  At(k, k.fHeat * 1.08, static_cast<float>(k.fAlpha * kk), p->c0);
  At(k, k.fHeat * 0.72, static_cast<float>(k.fAlpha * 0.62 * kk), p->c1);
  At(k, k.fHeat * 0.45, static_cast<float>(k.fAlpha * 0.22 * kk), p->c2);
}

// A line through the target's points, its halo, body and core as the studio strokes them.
void Polyline(const std::vector<V3> &pts, double width, double core, const Look &k, double heat, double alpha) {
  if (pts.size() < 2 || alpha <= 0.003)
    return;
  const float lw = static_cast<float>(fmax(1.0, width * g_pCam->fPerMap));
  for (size_t i = 1; i < pts.size(); ++i) {
    DesignPrim *p = Push(DESIGN_PRIM_LINE, k.bAdd);
    Project(pts[i - 1], &p->at[0], &p->at[1]);
    Project(pts[i], &p->at[2], &p->at[3]);
    p->head[3] = (i == 1 ? 1.0f : 0.0f) + (i + 1 == pts.size() ? 2.0f : 0.0f);  // round caps at the ends only
    p->axes[0] = lw;
    p->axes[1] = core > 0 ? fmaxf(0.6f, lw * static_cast<float>(core)) : 0.0f;
    p->axes[2] = lw * 2.6f;
    At(k, fmin(1.0, heat + 0.3), static_cast<float>(alpha), p->c0);
    At(k, heat, static_cast<float>(alpha), p->c1);
    At(k, heat * 0.7, static_cast<float>(alpha * 0.35), p->c2);
  }
}

int Skip(lua_State *) {
  ++g_nSkipped;
  return 0;
}

int HySpark(lua_State *L) {
  const V3 q = NeedPlace(L, 1);
  const Look k = LookOf(L, 2, true);
  if (k.fAlpha <= 0.003)
    return 0;
  const double s = Num(L, 2, "size", 1.0);
  V3 tail, vel;
  const bool bTail = OptPlace(L, 2, "tail", &tail);
  const double streak = Num(L, 2, "streak", 0.0);
  if (!bTail && OptPlace(L, 2, "vel", &vel) && streak != 0)
    tail = Sub(q, Scale(vel, streak));
  if (bTail || streak != 0) {
    DesignPrim *p = Push(DESIGN_PRIM_LINE, k.bAdd);
    Project(tail, &p->at[0], &p->at[1]);
    Project(q, &p->at[2], &p->at[3]);
    p->head[3] = 3.0f;
    p->axes[0] = static_cast<float>(fmax(1.0, s * g_pCam->fPerMap));
    At(k, k.fHeat, static_cast<float>(k.fAlpha), p->c1);
    return 0;
  }
  DesignPrim *p = Push(DESIGN_PRIM_DOT, k.bAdd);
  Project(q, &p->at[0], &p->at[1]);
  const float r = static_cast<float>(fmax(0.75, s * g_pCam->fPerMap * 0.5));
  p->axes[0] = r;
  p->axes[3] = r;
  At(k, k.fHeat, static_cast<float>(k.fAlpha), p->c0);
  return 0;
}

int HyGlow(lua_State *L) {
  const V3 q = NeedPlace(L, 1);
  const Look k = LookOf(L, 2, true);
  V3 u, v;
  Basis(L, 2, "camera", &u, &v);
  Glow(q, Num(L, 2, "r", 8.0), u, v, k, 1.0);
  return 0;
}

// Light on the ground: drawn here as the studio draws it without its lights, a glow on the ground.
int HyLight(lua_State *L) {
  V3 q = NeedPlace(L, 1);
  q.z = 0;
  const Look k = LookOf(L, 2, true);
  V3 u, v;
  Basis(L, 2, "ground", &u, &v);
  Glow(q, Num(L, 2, "r", 48.0), u, v, k, 0.75);
  return 0;
}

void Ring(lua_State *L, bool bFill) {
  const V3 q = NeedPlace(L, 1);
  const Look k = LookOf(L, 2, true);
  if (k.fAlpha <= 0.003)
    return;
  V3 u, v;
  Basis(L, 2, "ground", &u, &v);
  const double r = Num(L, 2, "r", 16.0), w = Num(L, 2, "width", 1.0);
  bFill = bFill || Bool(L, 2, "fill", false);
  const double outer = bFill ? r : r + w / 2;
  DesignPrim *p = Push(bFill ? DESIGN_PRIM_DOT : DESIGN_PRIM_RING, k.bAdd);
  Project(q, &p->at[0], &p->at[1]);
  if (outer <= 0 || !Axes(p, u, v, outer)) {
    g_pOut->pop_back();
    --g_nShapes;
    return;
  }
  p->ring[0] = bFill ? 0.0f : static_cast<float>(fmax(0.0, r - w / 2) / outer);
  p->head[2] = static_cast<float>(Num(L, 2, "dash", 0.0));
  p->ring[3] = static_cast<float>(Num(L, 2, "rot", 0.0));
  if (lua_istable(L, 2)) {
    lua_rawgetfield(L, 2, "arc");
    if (lua_istable(L, -1)) {
      lua_rawgeti(L, -1, 1);
      p->ring[1] = static_cast<float>(lua_tonumber(L, -1));
      lua_pop(L, 1);
      lua_rawgeti(L, -1, 2);
      p->ring[2] = static_cast<float>(lua_tonumber(L, -1));
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  At(k, k.fHeat, static_cast<float>(k.fAlpha), p->c0);
}

int HyRing(lua_State *L) {
  Ring(L, false);
  return 0;
}

int HyDisc(lua_State *L) {
  Ring(L, true);
  return 0;
}

int HyBeam(lua_State *L) {
  const V3 a = NeedPlace(L, 1), b = NeedPlace(L, 2);
  const Look k = LookOf(L, 3, true);
  if (k.fAlpha <= 0.003)
    return 0;
  const double wob = Num(L, 3, "wobble", 0.0);
  const int n = wob != 0 ? static_cast<int>(fmax(2.0, Num(L, 3, "segs", ceil(Len(Sub(b, a)) / 10)))) : 1;
  V3 ax0, ax1;
  const V3 d = Sub(b, a);
  Perp(Len(d) == 0 ? V3{1, 0, 0} : d, &ax0, &ax1);
  std::vector<V3> pts;
  pts.reserve(n + 1);
  // the moment's time, as f.t has it
  lua_getref(L, g_pNow->nF);
  const double ft = Num(L, -1, "t", 0.0);
  lua_pop(L, 1);
  const double ph = ft * Num(L, 3, "speed", 9.0);
  for (int i = 0; i <= n; ++i) {
    const double x = static_cast<double>(i) / n;
    V3 q = Mix(a, b, x);
    if (wob != 0 && i > 0 && i < n) {
      const double e = sin(3.14159265358979 * x);
      q = Add(q, Add(Scale(ax0, (Noise(i * 0.7, ph, g_pNow->fSeed * 0.01) - 0.5) * 2 * wob * e),
                     Scale(ax1, (Noise(i * 0.7 + 40, ph, g_pNow->fSeed * 0.01) - 0.5) * 2 * wob * e)));
    }
    pts.push_back(q);
  }
  Polyline(pts, Num(L, 3, "width", 2.0), Num(L, 3, "core", 0.4), k, k.fHeat, k.fAlpha);
  return 0;
}

int HyTrace(lua_State *L) {
  if (!lua_istable(L, 1))
    luaL_error(L, "f.trace(points, o): a list of places");
  std::vector<V3> pts;
  const int n = lua_objlen(L, 1);
  for (int i = 1; i <= n; ++i) {
    lua_rawgeti(L, 1, i);
    V3 q;
    if (Place(L, -1, &q))
      pts.push_back(q);
    lua_pop(L, 1);
  }
  const Look k = LookOf(L, 2, true);
  Polyline(pts, Num(L, 2, "width", 1.0), Num(L, 2, "core", 0.0), k, k.fHeat, k.fAlpha);
  return 0;
}

// Lightning, as fxcore.js's bolt: the line halved again and again, each middle pushed aside,
// drawn again rate times a second, with branches.
void Split(V3 p0, V3 p1, int lvl, double amp, double key, double roll, double seed, std::vector<V3> *pList) {
  if (lvl == 0) {
    pList->push_back(p1);
    return;
  }
  const V3 d = Sub(p1, p0);
  const double L = Len(d);
  V3 u, v;
  Perp(L > 0 ? d : V3{1, 0, 0}, &u, &v);
  const double r1 = Rand3(key, roll, seed) - 0.5, r2 = Rand3(key + 7, roll, seed) - 0.5;
  const V3 m = Add(Mix(p0, p1, 0.5), Add(Scale(u, r1 * amp * L), Scale(v, r2 * amp * L)));
  Split(p0, m, lvl - 1, amp, key * 2, roll, seed, pList);
  Split(m, p1, lvl - 1, amp, key * 2 + 1, roll, seed, pList);
}

int HyBolt(lua_State *L) {
  const V3 a = NeedPlace(L, 1), b = NeedPlace(L, 2);
  const Look k = LookOf(L, 3, true);
  lua_getref(L, g_pNow->nF);
  const double t = Num(L, -1, "t", 0.0);
  lua_pop(L, 1);
  const double seed = g_pNow->fSeed;
  const double rate = Num(L, 3, "rate", 18.0);
  const double roll = floor(t * rate) + static_cast<int>(Num(L, 3, "seed", 0.0)) * 1013.0;
  const double jag = Num(L, 3, "jag", 0.22), w = Num(L, 3, "width", 1.5);
  const int depth = static_cast<int>(Num(L, 3, "detail", 5.0)), branches = static_cast<int>(Num(L, 3, "branches", 2.0));
  std::vector<V3> main{a};
  Split(a, b, depth, jag, 1, roll, seed, &main);
  Polyline(main, w, Num(L, 3, "core", 0.5), k, k.fHeat, k.fAlpha);
  const V3 dir = Sub(b, a);
  V3 su, sv;
  Perp(Len(dir) > 0 ? dir : V3{1, 0, 0}, &su, &sv);
  for (int n = 0; n < branches && main.size() > 2; ++n) {
    const size_t at = 1 + static_cast<size_t>(floor(Rand3(n + 50, roll, seed) * (main.size() - 2)));
    const V3 from = main[at];
    const double Lb = Len(dir) * (0.2 + 0.25 * Rand3(n + 60, roll, seed));
    const V3 end = Add(Add(from, Scale(Norm(dir), Lb * 0.6)),
                       Add(Scale(su, (Rand3(n + 70, roll, seed) - 0.5) * Lb * 1.4),
                           Scale(sv, (Rand3(n + 80, roll, seed) - 0.5) * Lb * 1.4)));
    std::vector<V3> br{from};
    Split(from, end, depth - 2 > 2 ? depth - 2 : 2, jag * 1.2, 100 + n, roll, seed, &br);
    Polyline(br, fmax(1.0, w * 0.6), 0.0, k, k.fHeat * 0.8, k.fAlpha * 0.8);
  }
  return 0;
}

int HyStar(lua_State *L) {
  const V3 q = NeedPlace(L, 1);
  const Look k = LookOf(L, 2, true);
  if (k.fAlpha <= 0.003)
    return 0;
  DesignPrim *p = Push(DESIGN_PRIM_STAR, k.bAdd);
  Project(q, &p->at[0], &p->at[1]);
  p->axes[0] = static_cast<float>(Num(L, 2, "r", 6.0) * g_pCam->fPerMap);
  At(k, 1.0, static_cast<float>(k.fAlpha), p->c0);
  return 0;
}

// A flat piece turned in 3D, lit by its facing: a triangle, or a fan of them.
int HyShard(lua_State *L) {
  const V3 q = NeedPlace(L, 1);
  const Look k = LookOf(L, 2, false);
  if (k.fAlpha <= 0.003)
    return 0;
  const int sides = static_cast<int>(Num(L, 2, "sides", 3.0));
  const double sz = Num(L, 2, "size", 4.0), st = Num(L, 2, "stretch", 1.0), spin = Num(L, 2, "spin", 0.0);
  const bool bJag = Bool(L, 2, "jag", false);
  const double oseed = Num(L, 2, "seed", 0.0);
  V3 rot = {0, 0, 0};
  OptPlace(L, 2, "rot", &rot);
  std::vector<V3> pts;
  for (int i = 0; i < sides && i < 16; ++i) {
    const double an = static_cast<double>(i) / sides * 6.283185307 + spin;
    const double r = sz * (bJag ? 0.7 + 0.6 * Rand3(i, g_pNow->fSeed, oseed + 5) : 1);
    V3 c = {cos(an) * r, sin(an) * r * st, 0};
    c = RotZ(RotY(RotX(c, rot.x), rot.y), rot.z);
    pts.push_back(Add(q, c));
  }
  if (pts.size() < 3)
    return 0;
  const V3 nn = Norm(Cross(Sub(pts[1], pts[0]), Sub(pts[2], pts[0])));
  const V3 light = Norm(V3{-0.5, -0.7, 0.9});  // classic's art is lit from the top left
  const double shade = Bool(L, 2, "lit", true) ? 0.45 + 0.75 * fabs(Dot(nn, light)) : 1.0;
  float c[4];
  At(k, Clamp01(k.fHeat * shade), static_cast<float>(k.fAlpha), c);
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    DesignPrim *p = Push(DESIGN_PRIM_TRI, k.bAdd);
    Project(pts[0], &p->at[0], &p->at[1]);
    Project(pts[i], &p->at[2], &p->at[3]);
    Project(pts[i + 1], &p->axes[0], &p->axes[1]);
    memcpy(p->c0, c, sizeof c);
  }
  return 0;
}

// The screen lit, an even wash, held to fx.flash as a player's comfort caps would hold it: none
// by default.
int HyFlash(lua_State *L) {
  const Look k = LookOf(L, 1, true);
  if (k.fAlpha <= 0.003 || g_fFlash <= 0.0f)
    return 0;
  DesignPrim *p = Push(DESIGN_PRIM_WASH, k.bAdd);
  At(k, k.fHeat, static_cast<float>(fmin(g_fFlash, k.fAlpha)), p->c0);
  return 0;
}

int HyRand(lua_State *L) {
  const double i = luaL_checknumber(L, 1), k = luaL_checknumber(L, 2);
  // f.rand(i, k): rand3(seed + (i | 0) * 7 + 1, (k | 0) + 3, depth * 977 + 17), depth 0
  const double seed = g_pNow != NULL ? g_pNow->fSeed : 0;
  lua_pushnumber(L, Rand3(seed + static_cast<double>(static_cast<int>(I32(i))) * 7 + 1,
                          static_cast<double>(static_cast<int>(I32(k))) + 3, 17));
  return 1;
}

int HyNoise(lua_State *L) {
  lua_pushnumber(L, Noise(luaL_checknumber(L, 1), luaL_optnumber(L, 2, 0), luaL_optnumber(L, 3, 0)));
  return 1;
}

int HyFbm(lua_State *L) {
  lua_pushnumber(L, Fbm(luaL_checknumber(L, 1), luaL_optnumber(L, 2, 0), luaL_optnumber(L, 3, 0)));
  return 1;
}

// effect(meta, draw), the call each file makes once for each of its effects.
int HyEffect(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  Effect e;
  e.id = Str(L, 1, "id", "");
  if (e.id.empty())
    luaL_error(L, "effect(meta, draw): meta.id names it");
  e.name = Str(L, 1, "name", e.id.c_str());
  e.slot = Str(L, 1, "slot", "spell");
  e.ramp = Str(L, 1, "ramp", "fire");
  e.fLength = Num(L, 1, "length", 1.0);
  e.bLoop = Bool(L, 1, "loop", false);
  lua_Debug ar;
  e.file = lua_getinfo(L, 1, "s", &ar) ? ar.short_src : "";
  lua_pushvalue(L, 2);
  e.nDraw = lua_ref(L, -1);
  lua_pop(L, 1);
  lua_rawgetfield(L, 1, "params");
  e.nParams = lua_istable(L, -1) ? lua_ref(L, -1) : LUA_NOREF;
  lua_pop(L, 1);
  for (Effect &old : g_effects)
    if (old.id == e.id) {
      old = e;
      return 0;
    }
  g_effects.push_back(e);
  return 0;
}

// The effects' f, made in Luau once for each one played: what is arithmetic is the script's, the
// hash and the noise the host's, and every shape a host call.
const char kPrelude[] = R"LUAU(
local V = vector.create
local floor, sqrt, sin, cos, exp, abs, min, max, pi, huge = math.floor, math.sqrt, math.sin, math.cos, math.exp, math.abs, math.min, math.max, math.pi, math.huge
local rand, noise, fbm = hyrand, hynoise, hyfbm

local function clamp(x, a, b)
  a = a or 0; b = b or 1
  if x < a then return a elseif x > b then return b end
  return x
end
local function lerp(a, b, x) return a + (b - a) * x end
local function smooth(a, b, x) local t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t) end
local function fract(x) return x - floor(x) end

local ease = {}
ease.linear = function (x) return clamp(x) end
ease["in"] = function (x) x = clamp(x); return x * x end
ease.out = function (x) x = clamp(x); return 1 - (1 - x) * (1 - x) end
ease.inOut = function (x) x = clamp(x); if x < 0.5 then return 2 * x * x end return 1 - 2 * (1 - x) * (1 - x) end
ease.in3 = function (x) x = clamp(x); return x * x * x end
ease.out3 = function (x) x = clamp(x); return 1 - (1 - x) ^ 3 end
ease.inExpo = function (x) x = clamp(x); if x <= 0 then return 0 end return 2 ^ (10 * x - 10) end
ease.outExpo = function (x) x = clamp(x); if x >= 1 then return 1 end return 1 - 2 ^ (-10 * x) end
ease.outBack = function (x) x = clamp(x) - 1; return 1 + 2.70158 * x * x * x + 1.70158 * x * x end
ease.outElastic = function (x)
  x = clamp(x)
  if x == 0 or x == 1 then return x end
  return 2 ^ (-10 * x) * sin((x * 10 - 0.75) * 2.0944) + 1
end
ease.pulse = function (x) x = clamp(x); return sin(pi * x) end
ease.hit = function (x)
  x = clamp(x)
  if x < 0.08 then return x / 0.08 end
  return (1 - (x - 0.08) / 0.92) ^ 2
end

-- places are Luau's vectors: V(x, y, z), q.x, q.y, q.z, and + - * / as numbers have them
local function len(a) return vector.magnitude(a) end
local function norm(a) local l = vector.magnitude(a); if l == 0 then return a end return a / l end
local function mix(a, b, x) return a + (b - a) * x end
local function perp(n)
  n = norm(n)
  local a = if abs(n.z) < 0.9 then V(0, 0, 1) else V(1, 0, 0)
  local u = norm(vector.cross(a, n))
  return { u, vector.cross(n, u) }
end
local function rotZ(p, a) local c, s = cos(a), sin(a); return V(p.x * c - p.y * s, p.x * s + p.y * c, p.z) end
local function rotX(p, a) local c, s = cos(a), sin(a); return V(p.x, p.y * c - p.z * s, p.y * s + p.z * c) end
local function rotY(p, a) local c, s = cos(a), sin(a); return V(p.x * c + p.z * s, p.y, -p.x * s + p.z * c) end
local function curl(x, y, z)
  local e = 0.05
  local dx = (noise(x, y + e, z) - noise(x, y - e, z)) / (2 * e)
  local dy = (noise(x + e, y, z) - noise(x - e, y, z)) / (2 * e)
  return V(dx, -dy, noise(x + 31.7, y - 11.3, z) - 0.5)
end
local function ballistic(p0, v0, s, o)
  o = o or {}
  local g = if o.g == nil then 260 else o.g
  local drag, bounce = o.drag or 0, o.bounce or 0
  local tt = max(0, s)
  local k = if drag > 0 then (1 - exp(-drag * tt)) / drag else tt
  local x, y = p0.x + v0.x * k, p0.y + v0.y * k
  local z0, vz, time = p0.z, v0.z, tt
  for _ = 1, 4 do
    local land = if g > 0 then (vz + sqrt(vz * vz + 2 * g * max(0, z0))) / g else huge
    if time <= land or bounce == 0 then return V(x, y, max(0, z0 + vz * time - 0.5 * g * time * time)) end
    time -= land
    vz = (g * land - vz) * bounce
    z0 = 0
  end
  return V(x, y, 0)
end

local shapes = {
  spark = hyspark, glow = hyglow, light = hylight, ring = hyring, disc = hydisc, beam = hybeam,
  bolt = hybolt, trace = hytrace, star = hystar, shard = hyshard, flash = hyflash,
  trail = hyskip, tether = hyskip, telegraph = hyskip, box = hyskip, sphere = hyskip, decal = hyskip,
  distort = hyskip, shake = hyskip, pixels = hyskip, sound = hyskip, spawn = hyskip, sprite = hyskip,
}

function hymake(inp)
  local f = {
    t = 0, u = 0, length = inp.length, seed = inp.seed, at = inp.at, from = inp.from, to = inp.to,
    dir = inp.dir, radius = inp.radius, targets = inp.targets, team = inp.team, units = {}, area = inp.area,
    FRAME = 0.042, frame = 0, stepped = 0, detail = 3, life = { state = 'loop', t = 0, on = 1 },
  }
  f.ease, f.noise, f.fbm, f.curl = ease, noise, fbm, curl
  f.clamp, f.lerp, f.smooth, f.fract = clamp, lerp, smooth, fract
  f.v = function (x, y, z) return V(x or 0, y or 0, z or 0) end
  f.add = function (a, b) return a + b end
  f.sub = function (a, b) return a - b end
  f.scale = function (a, k) return a * k end
  f.dot, f.cross, f.len, f.norm, f.mix, f.perp = vector.dot, vector.cross, len, norm, mix, perp
  f.rotX, f.rotY, f.rotZ, f.ballistic = rotX, rotY, rotZ, ballistic
  f.rand = rand
  f.range = function (i, k, a, b) return a + (b - a) * rand(i, k) end
  f.phase = function (a, b)
    if b == a then return if f.t >= a then 1 else 0 end
    return clamp((f.t - a) / (b - a))
  end
  f.along = mix
  f.ground = function (q) return V(q.x, q.y, 0) end
  f.orbit = function (c, r, ang, tilt) return c + rotX(V(cos(ang) * r, sin(ang) * r, 0), tilt or 0) end
  f.look = function () end
  -- one r for every pass of f.each, reading the pass it is in: no function made a pass
  local cur = 0
  local function r(k) return rand(cur, k) end
  f.each = function (n, fn)
    n = min(20000, max(0, floor(n)))
    local was = cur
    for i = 0, n - 1 do
      cur = i
      fn(i, r)
    end
    cur = was
  end
  for k, fn in shapes do f[k] = fn end
  return f
end
)LUAU";

void Interrupt(lua_State *L, int gc) {
  if (gc >= 0)
    return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (g_deadline.QuadPart != 0 && now.QuadPart > g_deadline.QuadPart) {
    g_deadline.QuadPart = 0;
    luaL_error(L, "the effect took more than %.0f ms of one frame", kScriptMs);
  }
}

// Runs a chunk of Luau on L or a thread of it; false with the error kept.
bool Run(lua_State *L, const std::string &source, const char *pszName) {
  size_t nBytes = 0;
  char *pBytecode = luau_compile(source.data(), source.size(), NULL, &nBytes);
  const int nLoad = luau_load(L, pszName, pBytecode, nBytes, 0);
  free(pBytecode);
  if (nLoad != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    g_errors.push_back(std::string(pszName) + ": " + (lua_tostring(L, -1) != NULL ? lua_tostring(L, -1) : "?"));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

void Close(void) {
  g_playing.clear();
  g_effects.clear();
  if (g_L != NULL)
    lua_close(g_L);
  g_L = NULL;
}

// Every file of design/fx/luau/ loaded again, each in a sandbox of its own.
void Load(void) {
  Close();
  g_errors.clear();
  g_L = luaL_newstate();
  luaL_openlibs(g_L);
  static const luaL_Reg kHost[] = {
      {"hyrand", HyRand},   {"hynoise", HyNoise}, {"hyfbm", HyFbm},     {"hyspark", HySpark},
      {"hyglow", HyGlow},   {"hylight", HyLight}, {"hyring", HyRing},   {"hydisc", HyDisc},
      {"hybeam", HyBeam},   {"hybolt", HyBolt},   {"hytrace", HyTrace}, {"hystar", HyStar},
      {"hyshard", HyShard}, {"hyflash", HyFlash}, {"hyskip", Skip},     {"effect", HyEffect},
      {NULL, NULL}};
  for (const luaL_Reg *r = kHost; r->name != NULL; ++r) {
    lua_pushcfunction(g_L, r->func, r->name);
    lua_setglobal(g_L, r->name);
  }
  if (!Run(g_L, kPrelude, "=prelude"))
    return;
  luaL_sandbox(g_L);
  lua_callbacks(g_L)->interrupt = Interrupt;
  const std::string dir = DesignFolder() + "fx\\luau\\";
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((dir + "*.luau").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    FILE *f = fopen((dir + fd.cFileName).c_str(), "rb");
    if (f == NULL)
      continue;
    std::string source;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
      source.append(buf, n);
    fclose(f);
    lua_State *T = lua_newthread(g_L);
    luaL_sandboxthread(T);
    Run(T, source, (std::string("=") + fd.cFileName).c_str());
    lua_pop(g_L, 1);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

// ---- playing them -------------------------------------------------------------------------------

bool UnitPlace(int nUnit, V3 *pOut) {
  PortBenchUnit u;
  if (nUnit < 0 || !PortBenchUnitRead(nUnit, &u))
    return false;
  const CUnit *pUnit = CUnit::Head() + nUnit;
  *pOut = {static_cast<double>(pUnit->wPositionX), static_cast<double>(pUnit->wPositionY), 0};
  return true;
}

// A unit by its id, or "selected": the first unit selected (the second for toUnit); -1 for none.
int UnitGiven(const PortJson &j, int nPick) {
  if (j.kind == PortJson::NUMBER)
    return static_cast<int>(j.d);
  if (j.kind != PortJson::STRING || j.s != "selected")
    return -1;
  PortBenchUnit units[2];
  const int n = PortBenchSelected(units, 2);
  return nPick < n ? units[nPick].nIndex : -1;
}

void PushVec(lua_State *L, V3 v) {
  lua_pushvector(L, static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

V3 JsonPlace(const PortJson &j, V3 dflt) {
  if (j.kind != PortJson::ARRAY || j.size() < 2)
    return dflt;
  return {j.At(0).Num(0), j.At(1).Num(0), j.size() > 2 ? j.At(2).Num(0) : 0};
}

// p from the effect's params, each a value or a spec whose first item is the value, with the
// command's own over them.
void MakeParams(lua_State *L, const Effect &e, const PortJson &given) {
  lua_newtable(L);
  const int nP = lua_gettop(L);
  if (e.nParams != LUA_NOREF) {
    lua_getref(L, e.nParams);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      if (lua_istable(L, -1))
        lua_rawgeti(L, -1, 1);
      else
        lua_pushvalue(L, -1);
      lua_pushvalue(L, -3);  // the key
      lua_insert(L, -2);
      lua_settable(L, nP);
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  for (const PortJsonMember &m : given.members) {
    if (m.second.kind == PortJson::NUMBER)
      lua_pushnumber(L, m.second.d);
    else if (m.second.kind == PortJson::STRING)
      lua_pushstring(L, m.second.s.c_str());
    else if (m.second.kind == PortJson::BOOL)
      lua_pushboolean(L, m.second.b);
    else
      continue;
    lua_setfield(L, nP, m.first.c_str());
  }
}

bool CmdFxPlay(const PortJson &j, std::string *pAnswer) {
  if (g_L == NULL)
    Load();
  if (!DesignInGame()) {
    *pAnswer = "fx_play needs a single-player game";
    return false;
  }
  // the effect's id as "effect": the channel's own "id" numbers the request
  const std::string id = j["effect"].Str();
  size_t nEffect = g_effects.size();
  for (size_t i = 0; i < g_effects.size(); ++i)
    if (g_effects[i].id == id)
      nEffect = i;
  if (nEffect == g_effects.size()) {
    std::string names;
    for (const Effect &e : g_effects)
      names += (names.empty() ? "" : ", ") + e.id;
    *pAnswer = "no effect " + id + "; the effects: " + (names.empty() ? "none loaded" : names);
    return false;
  }
  const Effect &e = g_effects[nEffect];
  Playing p;
  p.nId = g_nNextId++;
  p.nEffect = nEffect;
  p.nStartFrame = DesignGameFrame();
  p.bLoop = j["loop"].kind == PortJson::BOOL ? j["loop"].b : e.bLoop;
  p.fSeed = j["seed"].Num(p.nId);
  p.nUnit = UnitGiven(j["unit"], 0);
  p.nToUnit = UnitGiven(j["toUnit"], 1);
  DesignPlayerRgb(static_cast<int>(j["team"].Num(0)), p.afTeam);
  double fMidX = 0, fMidY = 0;
  PortViewCameraMiddle(&fMidX, &fMidY);
  V3 at = {fMidX, fMidY, 0};
  UnitPlace(p.nUnit, &at);
  at = JsonPlace(j["at"], at);
  // a weapon's from and to, either side of the place unless given
  V3 to = JsonPlace(j["to"], Add(at, V3{90, -20, 10}));
  UnitPlace(p.nToUnit, &to);
  const V3 from = JsonPlace(j["from"], Add(at, V3{-90, 20, 14}));
  lua_State *L = g_L;
  lua_getglobal(L, "hymake");
  lua_newtable(L);
  lua_pushnumber(L, e.fLength);
  lua_setfield(L, -2, "length");
  lua_pushnumber(L, p.fSeed);
  lua_setfield(L, -2, "seed");
  PushVec(L, at);
  lua_setfield(L, -2, "at");
  PushVec(L, from);
  lua_setfield(L, -2, "from");
  PushVec(L, to);
  lua_setfield(L, -2, "to");
  PushVec(L, Norm(Sub(to, from)));
  lua_setfield(L, -2, "dir");
  lua_pushnumber(L, j["radius"].Num(64));
  lua_setfield(L, -2, "radius");
  PushVec(L, {p.afTeam[0] * 255.0, p.afTeam[1] * 255.0, p.afTeam[2] * 255.0});
  lua_setfield(L, -2, "team");
  lua_newtable(L);
  PushVec(L, to);
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "targets");
  if (lua_pcall(L, 1, 1, 0) != 0) {
    *pAnswer = std::string("the effect's f: ") + lua_tostring(L, -1);
    lua_pop(L, 1);
    return false;
  }
  p.nF = lua_ref(L, -1);
  lua_pop(L, 1);
  MakeParams(L, e, j["params"]);
  p.nP = lua_ref(L, -1);
  lua_pop(L, 1);
  g_playing.push_back(p);
  *pAnswer = "\"playing\":" + std::to_string(p.nId) + ",\"effect\":" + PortJsonQuote(e.id);
  return true;
}

void Stop(size_t i) {
  if (g_L != NULL) {
    lua_unref(g_L, g_playing[i].nF);
    lua_unref(g_L, g_playing[i].nP);
  }
  g_playing.erase(g_playing.begin() + i);
}

bool CmdFxStop(const PortJson &j, std::string *pAnswer) {
  const int nId = static_cast<int>(j["playing"].Num(0));
  size_t nStopped = 0;
  for (size_t i = g_playing.size(); i-- > 0;)
    if (nId == 0 || g_playing[i].nId == nId) {
      Stop(i);
      ++nStopped;
    }
  *pAnswer = "\"stopped\":" + std::to_string(nStopped);
  return true;
}

bool CmdFxLoad(const PortJson &, std::string *pAnswer) {
  Load();
  *pAnswer = DesignFxFields();
  return true;
}

// A Luau value as JSON: a number, a string, a boolean, or a list of them; null for anything else.
std::string ValueJson(lua_State *L, int idx, bool bList) {
  char sz[32];
  switch (lua_type(L, idx)) {
  case LUA_TNUMBER:
    snprintf(sz, sizeof sz, "%.6g", lua_tonumber(L, idx));
    return sz;
  case LUA_TSTRING:
    return PortJsonQuote(lua_tostring(L, idx));
  case LUA_TBOOLEAN:
    return lua_toboolean(L, idx) ? "true" : "false";
  case LUA_TTABLE: {
    if (!bList)
      return "null";
    std::string s = "[";
    const int n = lua_objlen(L, idx);
    for (int k = 1; k <= n; ++k) {
      lua_rawgeti(L, idx, k);
      s += (k > 1 ? "," : "") + ValueJson(L, lua_gettop(L), false);
      lua_pop(L, 1);
    }
    return s + "]";
  }
  default:
    return "null";
  }
}

// An effect's params as given: each a value, or a list whose first item is the value and the rest
// its range and step, or the choices.
std::string ParamsJson(lua_State *L, const Effect &e) {
  if (e.nParams == LUA_NOREF)
    return "{}";
  std::string s = "{";
  lua_getref(L, e.nParams);
  const int nT = lua_gettop(L);
  lua_pushnil(L);
  while (lua_next(L, nT) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING)
      s += (s.size() > 1 ? "," : "") + PortJsonQuote(lua_tostring(L, -2)) + ":" + ValueJson(L, lua_gettop(L), true);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return s + "}";
}

bool CmdFxList(const PortJson &, std::string *pAnswer) {
  if (g_L == NULL)
    Load();
  std::string s = "\"effects\":[";
  for (size_t i = 0; i < g_effects.size(); ++i) {
    const Effect &e = g_effects[i];
    s += std::string(i ? "," : "") + "{\"id\":" + PortJsonQuote(e.id) + ",\"name\":" + PortJsonQuote(e.name) +
         ",\"slot\":" + PortJsonQuote(e.slot) + ",\"length\":" + std::to_string(e.fLength) +
         ",\"loop\":" + (e.bLoop ? "true" : "false") + ",\"file\":" + PortJsonQuote(e.file) +
         ",\"params\":" + ParamsJson(g_L, e) + "}";
  }
  *pAnswer = s + "]," + DesignFxFields();
  return true;
}

// f.rand, f.noise and f.fbm at fixed places for a seed, to check against the studio's (fxcore.js).
bool CmdFxCheck(const PortJson &j, std::string *pAnswer) {
  static const double kI[] = {-3, 0, 1, 2, 2.7, 7, 100, 12345};
  static const double kK[] = {1, 2, 5};
  static const double kAt[][3] = {{0.3, 1.7, 2.2}, {10.5, -3.25, 0}, {123.4, 56.7, 8.9}, {-7.1, 0.5, 3.3}};
  const double seed = j["seed"].Num(0);
  char sz[48];
  std::string s = "\"rand\":[";
  for (size_t a = 0; a < sizeof kI / sizeof kI[0]; ++a)
    for (size_t b = 0; b < sizeof kK / sizeof kK[0]; ++b) {
      const double v = Rand3(seed + static_cast<double>(static_cast<int>(I32(kI[a]))) * 7 + 1,
                             static_cast<double>(static_cast<int>(I32(kK[b]))) + 3, 17);
      snprintf(sz, sizeof sz, "%s%.17g", a + b ? "," : "", v);
      s += sz;
    }
  s += "],\"noise\":[";
  for (size_t a = 0; a < sizeof kAt / sizeof kAt[0]; ++a) {
    snprintf(sz, sizeof sz, "%s%.17g", a ? "," : "", Noise(kAt[a][0], kAt[a][1], kAt[a][2]));
    s += sz;
  }
  s += "],\"fbm\":[";
  for (size_t a = 0; a < sizeof kAt / sizeof kAt[0]; ++a) {
    snprintf(sz, sizeof sz, "%s%.17g", a ? "," : "", Fbm(kAt[a][0], kAt[a][1], kAt[a][2]));
    s += sz;
  }
  *pAnswer = s + "]";
  return true;
}

}  // namespace

DesignPixel *DesignFxPixel(void) {
  return &g_pixel;
}

float DesignPixelCell(float fSize) {
  if (fSize >= 1.0f)
    return fminf(4.0f, floorf(fSize + 0.5f));
  return 1.0f / fminf(4.0f, floorf(1.0f / fmaxf(fSize, 0.25f) + 0.5f));
}

float *DesignFxFlash(void) {
  return &g_fFlash;
}

void DesignFxInit(void) {
  PortControlFloat("fx.pixel", &g_pixel.fOn, 0.0f, 1.0f,
                   "the effects drawn in the game's pixels (1): hard edges, light in steps with a dither, few "
                   "colours and time in held frames; 0 smooth at the window's pixels",
                   1.0f);
  PortControlFloat("fx.pixel.size", &g_pixel.fSize, 0.25f, 4.0f,
                   "an effect's pixel in the game's pixels: 1/4, 1/3, 1/2, 1, 2, 3 or 4, the nearest taken, so its "
                   "grid is the game's");
  PortControlFloat("fx.flash", &g_fFlash, 0.0f, 0.5f,
                   "how strong an effect's flash of the whole screen may be; 0 none");
  PortControlFloat("fx.pixel.steps", &g_pixel.fSteps, 2.0f, 8.0f, "the steps of an effect's light and cover", 1.0f);
  PortControlFloat("fx.pixel.colours", &g_pixel.fColours, 0.0f, 16.0f,
                   "the levels of each colour channel an effect draws with; 0 any", 1.0f);
  PortControlFloat("fx.pixel.hold", &g_pixel.fHold, 1.0f, 4.0f,
                   "the game frames each step of an effect's time holds, as an image script's wait does", 1.0f);
  PortControlCommand("fx_check", CmdFxCheck);
  PortControlCommand("fx_list", CmdFxList);
  PortControlCommand("fx_load", CmdFxLoad);
  PortControlCommand("fx_play", CmdFxPlay);
  PortControlCommand("fx_stop", CmdFxStop);
}

std::string DesignFxFields(void) {
  std::string errors;
  for (const std::string &e : g_errors)
    errors += (errors.empty() ? "" : ",") + PortJsonQuote(e);
  char sz[200];
  snprintf(sz, sizeof sz, "\"fx\":{\"loaded\":%u,\"playing\":%u,\"ms\":%.3f,\"shapes\":%u,\"leftOut\":%u,",
           static_cast<unsigned>(g_effects.size()), static_cast<unsigned>(g_playing.size()), g_dMs, g_nShapes,
           g_nSkipped);
  return sz + std::string("\"errors\":[") + errors + "]}";
}

void DesignFxBuild(const DesignCamera &cam, std::vector<DesignPrim> *pOut) {
  g_nShapes = g_nSkipped = 0;
  if (g_playing.empty() || g_L == NULL) {
    g_dMs = 0;
    return;
  }
  LARGE_INTEGER from, to, freq;
  QueryPerformanceCounter(&from);
  QueryPerformanceFrequency(&freq);
  // the game's time, smooth between its frames while it runs
  const unsigned nFrame = DesignGameFrame();
  if (nFrame != g_nLastFrame) {
    g_nLastFrame = nFrame;
    g_lastFrameAt = from;
  }
  double fWithin = DesignGamePaused() ? 0.0 : static_cast<double>(from.QuadPart - g_lastFrameAt.QuadPart) / freq.QuadPart;
  if (fWithin > kFrame)
    fWithin = kFrame;
  g_pCam = &cam;
  g_pOut = pOut;
  lua_State *L = g_L;
  for (size_t i = 0; i < g_playing.size();) {
    Playing &p = g_playing[i];
    const Effect &e = g_effects[p.nEffect];
    double t = (static_cast<double>(nFrame) - p.nStartFrame) * kFrame + fWithin;
    if (t < 0)
      t = 0;
    if (!p.bLoop && t > e.fLength + 0.05) {
      Stop(i);
      continue;
    }
    // asked to repeat, an effect made to play once starts again from its beginning; its own time
    // would otherwise run past its length and draw nothing
    if (p.bLoop && !e.bLoop)
      t = fmod(t, e.fLength + kRepeatRest);
    // in the game's pixels its time steps as an image script's frames do, each held a while
    if (g_pixel.fOn != 0.0f && g_pixel.fHold >= 1.0f) {
      const double fStep = kFrame * floor(g_pixel.fHold);
      t = floor(t / fStep + 1e-6) * fStep;
    }
    g_pNow = &p;
    g_pNowEffect = &e;
    lua_getref(L, p.nF);
    const int nF = lua_gettop(L);
    lua_pushnumber(L, t);
    lua_setfield(L, nF, "t");
    lua_pushnumber(L, p.bLoop && e.bLoop ? t / e.fLength - floor(t / e.fLength) : Clamp01(t / e.fLength));
    lua_setfield(L, nF, "u");
    const double fFrames = floor(t / kFrame + 1e-6);
    lua_pushnumber(L, fFrames);
    lua_setfield(L, nF, "frame");
    lua_pushnumber(L, fFrames * kFrame);
    lua_setfield(L, nF, "stepped");
    V3 at;
    if (UnitPlace(p.nUnit, &at)) {
      PushVec(L, at);
      lua_setfield(L, nF, "at");
    }
    if (UnitPlace(p.nToUnit, &at)) {
      PushVec(L, at);
      lua_setfield(L, nF, "to");
    }
    lua_getref(L, e.nDraw);
    lua_pushvalue(L, nF);
    lua_getref(L, p.nP);
    QueryPerformanceCounter(&g_deadline);
    g_deadline.QuadPart += static_cast<LONGLONG>(freq.QuadPart * kScriptMs / 1000.0);
    if (lua_pcall(L, 2, 0, 0) != 0) {
      g_errors.push_back(e.id + ": " + (lua_tostring(L, -1) != NULL ? lua_tostring(L, -1) : "?"));
      if (g_errors.size() > 20)
        g_errors.erase(g_errors.begin());
      lua_settop(L, nF - 1);
      Stop(i);
      continue;
    }
    lua_settop(L, nF - 1);
    ++i;
  }
  g_deadline.QuadPart = 0;
  g_pNow = NULL;
  g_pNowEffect = NULL;
  QueryPerformanceCounter(&to);
  g_dMs = 1000.0 * static_cast<double>(to.QuadPart - from.QuadPart) / freq.QuadPart;
}
