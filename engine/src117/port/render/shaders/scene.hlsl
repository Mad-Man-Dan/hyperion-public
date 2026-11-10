// scene.hlsl -- the map drawn from the frame description (port_scene.cpp,
// docs/renderer.md, R4b), into an 8-bit map of the GPU's own that the check compares
// with the game's.
//
// CSScene draws the terrain, or takes the map as it is, and draws the primitives
// from gFirst up to gLast over it: one thread draws four pixels side by side and
// writes them as one word, so the map is the game's bytes in the game's order, and
// it walks only the primitives of its 32-pixel square. A distortion takes the pixels
// a distance away as they were before it, so CSDistort draws one on its own, reading
// a copy of the map made just before (gPrev). At the last stage CSShow puts the
// pixels the screen takes into the GPU's copy of the screen (gShown), which the
// presenter shows (present.hlsl, PSCompose).
//
// Bindings follow SDL_GPU's layout for compute: read-only storage buffers in space 0
// (set 0), read-write ones in space 1 (set 1), uniforms in space 2 (set 2). SDL_GPU
// binds at most 8 storage buffers of each kind, so the tileset's two tables share a
// buffer, and so do the squares' starts and lists.

cbuffer SceneParams : register(b0, space2)
{
  uint gWidth;       // the map layer's width and rows
  uint gRows;
  uint gScrollX;     // the map pixel at its top left
  uint gScrollY;
  uint gRingX;       // the scroll ring's size in megatiles
  uint gRingY;
  uint gHeight;      // the screen buffer's rows, where a distortion wraps
  uint gBinsX;       // the squares in a row
  uint gFirst;       // CSScene: the primitives to draw; CSDistort: the first word
  uint gLast;        // column of the distortion's box and how many
  uint gFromTerrain; // CSScene: draw the terrain first
  uint gPrim;        // CSDistort: the distortion
  uint gCellsAt;     // where the cells start in gTileset, in bytes
  uint gBinListAt;   // where the lists start in gFrame, in words
  uint gBytesAt;     // where the frame's own bytes start in gFrame
  uint gCopiedAt;    // CSShow: where the pixels the screen takes start in them
  uint gOverFrom;    // CSScene: the first primitive after every one drawn in HD, from which
                     // a pixel drawn is over the HD frames (gTouched's 2 and 4)
  uint gUnderAt;     // CSScene: where in gTouched the pixels under a dialog's translucency
                     // are kept, and at twice it the dialog's own; 0 for nowhere
  uint gExtraAt;     // CSSceneFull: where each primitive's full-colour extras start in gFrame
  uint gOrigin;      // CSScene, CSSceneFull: the dispatch's top left in the layer, x in the low 16
                     // bits (a multiple of 4), y in the high: a pass after a distortion covers
                     // only the box of its primitives; CSDistort: its first row
  uint gStep;        // the game's pixels to one of the layer's, across and down: each
                     // pixel of the layer is the game's at the middle of its square. 1 but with
                     // the camera far out, where a pixel of the window is many of the game's
  uint gViewW;       // the render view's width in the game's pixels, a row of the screen buffer
  uint gLooksAt;     // CSSceneFull: where the looks' data starts in gFrame, 0 for none
  uint gTimeMs;      // and the time, in milliseconds within the hour
  uint gMarksAt;     // CSSceneFull: where the marks' numbers start in gFrame, 0 for none
  uint gMarksUnused;
};

// The full colour's (stage G; CSSceneFull, CSDistortFull, CSShowFull): the palette as shown
// and each colour slot's eight team shades, RGBA8 a word.
cbuffer FullParams : register(b1, space2)
{
  uint4 gPal[64];
  uint4 gShade[136];       // PLAYER_SLOTS_MAX * 8 words, rounded up
};

// And the fog's colour matrices at each of its 32 shades, three rows each, for the sprites
// and for the terrain, fitted to 1.17's tables (port_gpu.cpp, GpuHdFogMatrices). A block of
// their own: SDL's Vulkan binds 4 KiB of a block, and past it the matrices read as zeros,
// which drew the fog's edges black, a block at a time.
cbuffer FogParams : register(b3, space2)
{
  float4 gFogSprite[96];
  float4 gFogTerrain[96];
};

struct Slot { uint mtx; uint tile; uint edge; uint edgeFrame; };
struct Entry { uint offset; uint size; uint origin; uint pad; };
struct Prim { uint kind; int x; int y; uint entry; uint clip0; uint clip1; uint table; uint param; };

StructuredBuffer<Slot> gSlots : register(t0, space0);       // what the ring holds, per tile
ByteAddressBuffer gTileset : register(t1, space0);          // the megatiles, 16 cell references a megatile in dwords,
                                                            // then .vr4, 64 bytes a cell
StructuredBuffer<Entry> gEntries : register(t2, space0);    // the atlas's frames
ByteAddressBuffer gAtlas : register(t3, space0);            // their pixels, 16 bits each
StructuredBuffer<Prim> gPrims : register(t4, space0);       // the primitives, in order
ByteAddressBuffer gTables : register(t5, space0);           // the tables they read
ByteAddressBuffer gFrame : register(t6, space0);            // each square's first in the lists, the
                                                            // lists (the primitives of each square),
                                                            // then the frame's own bytes
ByteAddressBuffer gPrev : register(t7, space0);             // the map before a distortion

RWByteAddressBuffer gMap : register(u0, space1);            // the map, a byte a pixel
RWByteAddressBuffer gStats : register(u1, space1);          // [0] pixels of a tile the ring no longer holds
RWByteAddressBuffer gShown : register(u2, space1);          // the screen as the GPU has it
RWByteAddressBuffer gTouched : register(u3, space1);        // beside the map, a byte a pixel: 1 where
                                                            // anything but the terrain and the fog
                                                            // drew, where the HD frame keeps the map's
                                                            // pixel and does not draw HD terrain; 2
                                                            // too where a primitive after every HD
                                                            // frame drew (the cursor, a health bar,
                                                            // a dialog), which goes back over them;
                                                            // 4 instead where that was a dialog's
                                                            // translucency, its fit (the blit's
                                                            // param above 8 bits, less 1) in bits 3
                                                            // to 5, the pixel under it at gUnderAt
                                                            // on, and the dialog's at twice that;
                                                            // from three times that two bytes a
                                                            // pixel, the draw order (RankSet)
RWByteAddressBuffer gMapRgb : register(u4, space1);         // the full colour's map, RGBA8 a pixel; alpha
                                                            // 1 where a sprite or a line drew, 0 terrain
RWByteAddressBuffer gShownRgb : register(u5, space1);       // its copy of the screen, which PSComposeFull reads
RWByteAddressBuffer gPrevRgb : register(u6, space1);        // its map before a distortion
RWByteAddressBuffer gHud : register(u7, space1);            // with the camera zoomed out, the
                                                            // screen's, a byte a pixel: 1 where a
                                                            // primitive over the map drew (CSHud)

// CSHud's: the camera, and the render view's map layer it looks at.
cbuffer HudParams : register(b2, space2)
{
  float gCamX;     // the map pixel at the screen's top left
  float gCamY;
  float gZoom;     // the map's pixels to a screen pixel
  uint gHudFull;   // 1: the full colour's copy of the screen too
  uint gMapX;      // the map layer's top left in map pixels, and its size
  uint gMapY;
  uint gMapW;
  uint gMapH;
  uint gMapStep;   // the layer's step (SceneParams' gStep), and its pixels across
  uint gMapStride;
  uint gHudUnused0;
  uint gHudUnused1;
};

static const uint NONE = 0xFFFFFFFFu;
static const uint PRIM_NORMAL = 1u;
static const uint PRIM_SHADOW = 2u;
static const uint PRIM_EFFECT = 3u;
static const uint PRIM_FADE = 4u;
static const uint PRIM_UNCLOAKED = 5u;
static const uint PRIM_WARP = 6u;
static const uint PRIM_SPAN = 7u;
static const uint PRIM_STAR = 9u;
static const uint PRIM_FOG = 10u;
static const uint PRIM_GLYPH = 11u;
static const uint PRIM_BLIT = 12u;
static const uint PRIM_LOOK = 13u;
static const uint PRIM_MARK = 14u;
static const uint PRIM_MIRROR = 0x100u;

uint LoadByte(ByteAddressBuffer b, uint nAt)
{
  return (b.Load(nAt & ~3u) >> ((nAt & 3u) * 8u)) & 0xFFu;
}

uint LoadHalf(ByteAddressBuffer b, uint nAt)
{
  return (b.Load(nAt & ~3u) >> ((nAt & 2u) * 8u)) & 0xFFFFu;
}

uint Table(uint nAt)
{
  return LoadByte(gTables, nAt);
}

// An atlas pixel of frame e at x, y inside it: the colour with bit 8 set, or 0 where
// the frame is clear or x, y is outside it.
uint AtlasPixel(uint e, int x, int y)
{
  Entry entry = gEntries[e];
  int w = int(entry.size & 0xFFFFu);
  int h = int(entry.size >> 16);
  if (x < 0 || y < 0 || x >= w || y >= h)
    return 0;
  return LoadHalf(gAtlas, (entry.offset + uint(y * w + x)) * 2u);
}

// The terrain at map pixel px, py: the megatile the ring holds for its tile, and the
// creep edge over it (cell_draw_mtx, draw_cell, rle_creep).
uint Terrain(uint px, uint py, inout uint nStale)
{
  uint tx = px >> 5;
  uint ty = py >> 5;
  Slot s = gSlots[(ty % gRingY) * gRingX + tx % gRingX];
  if (s.tile != (tx | (ty << 16))) {
    ++nStale;
    return 0;
  }
  uint lx = px & 31u;
  uint ly = py & 31u;
  if (s.edge != NONE) {
    Entry entry = gEntries[s.edge];
    uint v = AtlasPixel(s.edge, int(lx) - int(entry.origin & 0xFFFFu), int(ly) - int(entry.origin >> 16));
    if (v & 0x100u)
      return v & 0xFFu;
  }
  uint mtx = s.mtx & 0x7FFFu;
  uint cellRef = gTileset.Load((mtx * 16u + (ly >> 3) * 4u + (lx >> 3)) * 4u);
  uint cx = lx & 7u;
  if (cellRef & 1u)
    cx = 7u - cx;
  return LoadByte(gTileset, gCellsAt + (cellRef >> 1) * 64u + (ly & 7u) * 8u + cx);
}

// The frame's pixel a primitive draws at dx, dy inside its clip: the clip keeps
// columns from its left, counted from the frame's right edge when it is mirrored
// (port_draw.cpp, DrawRle).
uint PrimPixel(Prim p, int dx, int dy)
{
  Entry entry = gEntries[p.entry];
  int w = int(entry.size & 0xFFFFu);
  int col = int(p.clip0 & 0xFFFFu) + dx;
  if (p.kind & PRIM_MIRROR)
    col = w - 1 - col;
  return AtlasPixel(p.entry, col, int(p.clip0 >> 16) + dy);
}

int SignedByte(uint b)
{
  return int(b << 24) >> 24;
}

uint FrameWord(uint n)
{
  return gFrame.Load(n * 4u);
}

uint FrameByte(uint nAt)
{
  uint nByte = gBytesAt + nAt;
  return (gFrame.Load(nByte & ~3u) >> ((nByte & 3u) * 8u)) & 0xFFu;
}

// The fog over pixel x, y (mask_apply_light): its 8x8 block's shade from the light
// map's four corners, flat when they agree (0 is black, 0x1F leaves the pixel), else
// stepped down the block's sides and then across each row from its right end
// (mask_block_lerp).
uint Fog(Prim p, uint x, uint y, uint v)
{
  uint nAt = uint(p.x) + (y >> 3) * uint(p.y) + (x >> 3);
  int tl = int(FrameByte(nAt));
  int tr = int(FrameByte(nAt + 1u));
  int bl = int(FrameByte(nAt + uint(p.y)));
  int br = int(FrameByte(nAt + uint(p.y) + 1u));
  if (tl == tr && tl == bl && tl == br) {
    if (tl == 0)
      return 0;
    if (tl == 0x1F)
      return v;
    return Table(p.table + uint(tl) * 256u + v);
  }
  int i = int(y & 7u);
  int left = (tl << 8) + i * (((bl << 8) - (tl << 8)) / 8);
  int right = (tr << 8) + i * (((br << 8) - (tr << 8)) / 8);
  int step = (left - right) >> 3;
  int shade = right + (7 - int(x & 7u)) * step;
  return Table(p.table + uint((shade >> 8) & 0x1F) * 256u + v);
}

// Primitive p over pixel x, y (nAt in the map) that has v.
uint Apply(Prim p, int x, int y, uint nAt, uint v)
{
  uint kind = p.kind & 0xFFu;
  if (kind == PRIM_LOOK || kind == PRIM_MARK)
    return v;  // drawn in the full colour alone, so the index map is as the game drew it
  if (kind == PRIM_FOG)
    return Fog(p, uint(x), uint(y), v);
  if (kind == PRIM_SPAN) {
    int d = int(nAt) - p.x;
    if (d < 0 || (uint(d) % p.clip0) != 0u || uint(d) / p.clip0 >= uint(p.y))
      return v;
    return p.table & 0xFFu;
  }
  int cw = int(p.clip1 & 0xFFFFu);
  int ch = int(p.clip1 >> 16);
  int dx = x - p.x;
  int dy = y - p.y;
  if (dx < 0 || dy < 0 || dx >= cw || dy >= ch)
    return v;
  if (kind == PRIM_STAR)
    return v == 0u ? Table(p.table + uint(dy * cw + dx)) : v;
  if (kind == PRIM_BLIT) {
    // A bitmap's pixels in the frame's bytes: copied, or those not 0 (bmp_mask_rect),
    // or through a table with the map's pixel (the dialogs' translucency).
    uint b = FrameByte(p.table + uint(dy * cw + dx));
    uint mode = p.param & 0xFFu;  // above it, the HD frame's fit (port_gpu.cpp)
    if (mode == 0u)
      return b;
    if (mode == 1u)
      return b != 0u ? b : v;
    return Table(p.entry + (mode == 2u ? (b << 8) | v : (v << 8) | b));
  }
  uint s = PrimPixel(p, dx, dy);
  if ((s & 0x100u) == 0u)
    return v;
  s &= 0xFFu;
  if (kind == PRIM_NORMAL)
    return Table(p.table + s);
  if (kind == PRIM_GLYPH)
    return Table(p.table + (s & 7u));
  if (kind == PRIM_SHADOW || kind == PRIM_FADE)
    return Table(p.table + v);
  if (kind == PRIM_EFFECT)
    return Table(p.table + s * 256u + v);
  if (kind == PRIM_UNCLOAKED) {
    uint c = Table(p.table + s);
    if (SignedByte(Table((p.param >> 8) + c)) > SignedByte(p.param & 0xFFu))
      return c;
    return v;
  }
  if (kind == PRIM_WARP) {
    // The texture is read on through the pixels drawn, one row of the clip after
    // another, from the right when mirrored.
    int col = (p.kind & PRIM_MIRROR) ? cw - 1 - dx : dx;
    return Table(p.table + uint(dy * cw + col));
  }
  return v;
}

// The draw order with the frame in HD: the HD draws before the last primitive that drew a
// pixel solidly, from its kind's 16 bits on (port_gpu_hd.cpp, GpuHdScene). An HD frame drawn
// before that leaves the pixel to it (hd.hlsl, RankOver). A shadow, a translucency and the
// fog leave the order as it is.
bool RankKind(Prim p)
{
  uint kind = p.kind & 0xFFu;
  return kind == PRIM_NORMAL || kind == PRIM_UNCLOAKED || kind == PRIM_WARP || kind == PRIM_SPAN ||
         kind == PRIM_GLYPH || (kind == PRIM_BLIT && (p.param & 0xFFu) < 2u);
}

uint2 RankLoad(uint nAt)
{
  return gUnderAt != 0u && gFromTerrain == 0u ? gTouched.Load2(3u * gUnderAt + 2u * nAt) : uint2(0u, 0u);
}

// Pixel i of four (two to a word) given primitive p's rank.
uint2 RankSet(uint2 rank, uint i, Prim p)
{
  uint shift = (i & 1u) * 16u;
  uint w = ((i < 2u ? rank.x : rank.y) & ~(0xFFFFu << shift)) | ((p.kind >> 16) << shift);
  return i < 2u ? uint2(w, rank.y) : uint2(rank.x, w);
}

[numthreads(8, 8, 1)]
void CSScene(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u + (gOrigin & 0xFFFFu);
  uint y = id.y + (gOrigin >> 16);
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint old = gFromTerrain != 0u ? 0u : gMap.Load(nAt);
  uint oldTouched = gFromTerrain != 0u ? 0u : gTouched.Load(nAt);
  uint bin = (y / 32u) * gBinsX + x0 / 32u;
  uint nStart = gBinListAt + FrameWord(bin);
  uint nEnd = gBinListAt + FrameWord(bin + 1u);
  uint nStale = 0;
  uint word = 0;
  uint touched = 0;
  uint under = gUnderAt != 0u ? gTouched.Load(gUnderAt + nAt) : 0u;
  uint panel = gUnderAt != 0u ? gTouched.Load(2u * gUnderAt + nAt) : 0u;
  uint2 rank = RankLoad(nAt);
  // the game's pixel each of the layer's is (gStep)
  uint gy = y * gStep + (gStep >> 1);
  for (uint i = 0; i < 4u; ++i) {
    uint gx = (x0 + i) * gStep + (gStep >> 1);
    uint v = gFromTerrain != 0u ? Terrain(gScrollX + gx, gScrollY + gy, nStale) : (old >> (i * 8u)) & 0xFFu;
    uint t = (oldTouched >> (i * 8u)) & 0xFFu;
    for (uint k = nStart; k < nEnd; ++k) {
      uint n = FrameWord(k);
      if (n < gFirst)
        continue;
      if (n >= gLast)
        break;
      Prim p = gPrims[n];
      uint before = v;
      v = Apply(p, int(gx), int(gy), gy * gViewW + gx, v);
      // a star leaves the pixel the terrain's, under the HD frames: the HD terrain draws its
      // own star field where it is clear (hd.hlsl, HdStars), and 1.17's stars land on every
      // pixel still 0, which is also under each sprite drawn in HD
      if (v == before || (p.kind & 0xFFu) == PRIM_FOG || (p.kind & 0xFFu) == PRIM_STAR)
        continue;
      if (RankKind(p))
        rank = RankSet(rank, i, p);
      uint fit = (p.kind & 0xFFu) == PRIM_BLIT && (p.param & 0xFFu) >= 2u ? p.param >> 8 : 0u;
      if (n < gOverFrom) {
        t |= 1u;
      } else if (fit != 0u && gUnderAt != 0u) {
        // a dialog's translucency, over the HD frames by its fit, unless something drawn
        // after them is under it; the pixel under the first of them kept, and the dialog's
        if ((t & 2u) == 0u) {
          if ((t & 4u) == 0u)
            under = (under & ~(0xFFu << (i * 8u))) | (before << (i * 8u));
          uint src = FrameByte(p.table + uint((int(gy) - p.y) * int(p.clip1 & 0xFFFFu) + (int(gx) - p.x)));
          panel = (panel & ~(0xFFu << (i * 8u))) | (src << (i * 8u));
          t = 5u | ((fit - 1u) << 3);
        }
      } else {
        t = 3u;
      }
    }
    word |= v << (i * 8u);
    touched |= t << (i * 8u);
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
  if (gUnderAt != 0u) {
    gTouched.Store(gUnderAt + nAt, under);
    gTouched.Store(2u * gUnderAt + nAt, panel);
    gTouched.Store2(3u * gUnderAt + 2u * nAt, rank);
  }
  if (nStale != 0) {
    uint nOld;
    gStats.InterlockedAdd(0, nStale, nOld);
  }
}

// The layer's pixel a distortion takes for the game's pixel gx, gy of its box: the pixel
// nShift along the screen buffer from it, wrapping at the buffer's ends, in the layer.
uint DistortFrom(Prim p, uint gx, uint gy, uint nShift)
{
  int nSize = int(gViewW * gHeight);
  int nAt = int(gy * gViewW + gx);
  int nSrc = (p.kind & PRIM_MIRROR) ? nAt - int(nShift) : nAt + int(nShift);
  if (nSrc >= nSize)
    nSrc -= nSize;
  if (nSrc < 0)
    nSrc += nSize;
  return (uint(nSrc) / gViewW / gStep) * gWidth + (uint(nSrc) % gViewW) / gStep;
}

// The game's pixel of distortion p a pixel of the layer is, and its shift there, false where
// the frame is clear. Its words are from gFirst, gLast of them, its rows from gOrigin.
bool DistortAt(Prim p, int x, int gy, out uint gx, out uint nShift)
{
  gx = uint(x) * gStep + (gStep >> 1);
  nShift = 0u;
  int dx = int(gx) - p.x;
  if (dx < 0 || dx >= int(p.clip1 & 0xFFFFu))
    return false;
  uint s = PrimPixel(p, dx, gy - p.y);
  if ((s & 0x100u) == 0u)
    return false;
  s &= 0xFFu;
  nShift = p.table != NONE ? Table(p.table + s) : s;
  return true;
}

// One distortion (port_draw.cpp, DrawDistort): each opaque pixel of the frame takes
// the pixel a distance to its right, to its left when mirrored, the distance the
// frame's own pixel (the EMP shockwave) or its brightness (a cloaked unit), wrapping
// at the ends of the screen buffer.
[numthreads(8, 8, 1)]
void CSDistort(uint3 id : SV_DispatchThreadID)
{
  Prim p = gPrims[gPrim];
  int y = int(gOrigin + id.y);
  int x0 = (int(gFirst) + int(id.x)) * 4;
  int gy = y * int(gStep) + int(gStep >> 1);
  if (id.x >= gLast || y >= int(gRows) || x0 < 0 || x0 >= int(gWidth) || gy < p.y ||
      gy - p.y >= int(p.clip1 >> 16))
    return;
  uint nAt = uint(y) * gWidth + uint(x0);
  uint word = gPrev.Load(nAt);
  uint touched = gTouched.Load(nAt);
  for (uint i = 0; i < 4u; ++i) {
    uint gx, nShift;
    if (!DistortAt(p, x0 + int(i), gy, gx, nShift))
      continue;
    uint b = LoadByte(gPrev, DistortFrom(p, gx, uint(gy), nShift));
    word = (word & ~(0xFFu << (i * 8u))) | (b << (i * 8u));
    touched |= 1u << (i * 8u);
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
}

// The pixels of the map's rows the screen takes (the console mask where the grid is
// dirty, vid_draw_mask) go from the map into the GPU's copy of the screen.
[numthreads(8, 8, 1)]
void CSShow(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u;
  uint y = id.y;
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint map = gMap.Load(nAt);
  uint word = gShown.Load(nAt);
  for (uint i = 0; i < 4u; ++i)
    if (FrameByte(gCopiedAt + nAt + i) != 0u)
      word = (word & ~(0xFFu << (i * 8u))) | (map & (0xFFu << (i * 8u)));
  gShown.Store(nAt, word);
}

// ---------------------------------------------------------------------------
// The full colour (stage G, docs/renderer.md). CSSceneFull draws the map as CSScene does,
// the same bytes into gMap and gTouched, and beside them each pixel's colour: the palette's
// colour of its index, but a team pixel of a sprite in its slot's shade, a shadow scaling
// the colour under it by the fit port_gpu.cpp makes of its table, a translucent frame's
// change to the index carried onto the colour under it, and the fog's shade taken smoothly
// between its corners and applied as a colour matrix.

float3 Unpack(uint c)
{
  return float3(c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu) / 255.0;
}

uint Pack(float3 c, uint sprite)
{
  uint3 u = uint3(saturate(c) * 255.0 + 0.5);
  return u.x | (u.y << 8) | (u.z << 16) | (sprite << 24);
}

float3 Pal(uint i)
{
  return Unpack(gPal[i >> 2][i & 3u]);
}

float3 Shade(uint slot, uint k)
{
  uint n = slot * 8u + k;
  return Unpack(gShade[n >> 2][n & 3u]);
}

// A primitive's extras, 32 bytes each (port_gpu.cpp, GpuFullExtras): its colour slot in
// the low byte and 0x100 where its remap's team colours are the slot's ramp; then four
// floats, a shadow's scale of each channel.
uint ExtraWord(uint n, uint k)
{
  return gFrame.Load(gExtraAt + n * 32u + k * 4u);
}

float4 ExtraFit(uint n)
{
  return float4(asfloat(ExtraWord(n, 4u)), asfloat(ExtraWord(n, 5u)), asfloat(ExtraWord(n, 6u)),
                asfloat(ExtraWord(n, 7u)));
}

// Colour s of primitive n's frame as its remap gives it, c: the slot's shade for a team
// colour when the remap's team colours are the slot's ramp, else c's colour.
float3 Remapped(uint n, uint s, uint c)
{
  uint e = ExtraWord(n, 0u);
  if ((e & 0x100u) != 0u && s >= 8u && s <= 15u)
    return Shade(e & 0xFFu, s - 8u);
  return Pal(c);
}

float3 FogRow(uint sprite, uint n, uint r)
{
  return sprite != 0u ? gFogSprite[n * 3u + r].xyz : gFogTerrain[n * 3u + r].xyz;
}

// The fog in full colour: the shade 1.17 steps across each 8x8 block (Fog), taken smoothly
// between the same corners, and the colour matrices at the two shades around it.
float3 FogColour(Prim p, uint x, uint y, float3 c, uint sprite)
{
  uint nAt = uint(p.x) + (y >> 3) * uint(p.y) + (x >> 3);
  float tl = float(FrameByte(nAt));
  float tr = float(FrameByte(nAt + 1u));
  float bl = float(FrameByte(nAt + uint(p.y)));
  float br = float(FrameByte(nAt + uint(p.y) + 1u));
  float fy = float(y & 7u) / 8.0;
  float fx = float((x & 7u) + 1u) / 8.0;
  float s = lerp(lerp(tl, bl, fy), lerp(tr, br, fy), fx);
  if (s >= 31.0)
    return c;
  if (s <= 0.0)
    return float3(0, 0, 0);
  uint n = min(uint(s), 30u);
  float t = s - float(n);
  float3 r0 = lerp(FogRow(sprite, n, 0u), FogRow(sprite, n + 1u, 0u), t);
  float3 r1 = lerp(FogRow(sprite, n, 1u), FogRow(sprite, n + 1u, 1u), t);
  float3 r2 = lerp(FogRow(sprite, n, 2u), FogRow(sprite, n + 1u, 2u), t);
  return saturate(float3(dot(r0, c), dot(r1, c), dot(r2, c)));
}

// ---------------------------------------------------------------------------
// Looks (port_looks.h): in full colour, a looked image's pixels drawn as the design
// draws a rich resource (design/41-rich-bases/richfx.js), and a look's light on
// the ground and its plume. Each look's data is LOOK_BYTES at gLooksAt in gFrame: the tone of
// each palette entry its rule takes (0 none, else 1 + the tone * 254), the ramp as six float4s
// at 256, the clarity, shine, glow and sparkle at 352, the plume's height, density and rise at
// 368, the light's colour and strength at 384, and whether it is gas and in steps at 400.

// the aura's at 416, each entry's zone at 544, the ramps of zones 1 to 3 at
// 800 and each zone's material and mix at 1088 (port_looks.h)
static const uint LOOK_BYTES = 1152u;
// how far past the first zone's ramp LookRampAt reads: the ramp of the zone being drawn
static uint g_lookRamp = 0u;
static const float2 LOOK_ART_LIGHT = float2(-0.62, -0.78);  // 1.17's art is lit from the top left
static const float3 LOOK_SUN = float3(-0.55, -0.62, 0.5596);
static const float LOOK_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static const int2 LOOK_RING[8] = {int2(1, 0), int2(1, 1), int2(0, 1), int2(-1, 1), int2(-1, 0), int2(-1, -1),
                                  int2(0, -1), int2(1, -1)};
// a star where a bright face catches the light: this pixel as an arm of each centre near it
static const float3 LOOK_STAR[9] = {float3(0, 0, 1), float3(1, 0, 0.6), float3(-1, 0, 0.6), float3(0, 1, 0.6),
                                    float3(0, -1, 0.6), float3(2, 0, 0.25), float3(-2, 0, 0.25), float3(0, 2, 0.25),
                                    float3(0, -2, 0.25)};

uint LookAt(uint look)
{
  return gLooksAt + ((look & 0xFFu) - 1u) * LOOK_BYTES;
}

// The tone of palette entry s for the look at at, -1 where its rule does not take it.
float LookTone(uint at, uint s)
{
  uint b = (gFrame.Load(at + (s & ~3u)) >> ((s & 3u) * 8u)) & 0xFFu;
  return b == 0u ? -1.0 : float(b - 1u) / 254.0;
}

float4 LookF4(uint at, uint off)
{
  return asfloat(gFrame.Load4(at + off));
}

float3 LookRampAt(uint at, uint i)
{
  return LookF4(at, 256u + g_lookRamp + i * 16u).xyz;
}

float3 LookRamp(uint at, float t)
{
  t = saturate(t) * 5.0;
  uint i = min(uint(t), 4u);
  return lerp(LookRampAt(at, i), LookRampAt(at, i + 1u), t - float(i));
}

float LookTime()
{
  return float(gTimeMs) / 1000.0;
}

float LookBayer(int2 p)
{
  uint2 q = uint2(p) & 3u;
  return (LOOK_BAYER[q.x + q.y * 4u] + 0.5) / 16.0;
}

float LookHash(float2 p)
{
  p = frac(p * float2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return frac(p.x * p.y);
}

float LookNoise(float2 p)
{
  float2 i = floor(p);
  float2 f = frac(p);
  f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(LookHash(i), LookHash(i + float2(1, 0)), f.x), lerp(LookHash(i + float2(0, 1)), LookHash(i + float2(1, 1)), f.x),
              f.y);
}

float LookFbm(float2 p)
{
  float s = 0.0;
  float a = 0.5;
  for (int i = 0; i < 4; ++i) {
    s += a * LookNoise(p);
    p = p * 2.03 + float2(1.7, 9.2);
    a *= 0.5;
  }
  return s;
}

float LookLuma(uint s)
{
  return dot(Pal(s & 0xFFu), float3(0.299, 0.587, 0.114));
}

// The frame's own column and row of primitive p's pixel at dx, dy, mirrored as it is drawn.
int2 LookFramePos(Prim p, int dx, int dy)
{
  Entry entry = gEntries[p.entry];
  int col = int(p.clip0 & 0xFFFFu) + dx;
  if (p.kind & PRIM_MIRROR)
    col = int(entry.size & 0xFFFFu) - 1 - col;
  return int2(col, int(p.clip0 >> 16) + dy);
}

// The stars on a crystal at dx, dy: a few of its bright pixels, chosen by their place in the
// frame so a star stays on its face, flash now and then.
float LookSparkle(Prim p, uint at, int dx, int dy, float amount, float t)
{
  float s = 0.0;
  for (uint k = 0; k < 9u; ++k) {
    int qx = dx - int(LOOK_STAR[k].x);
    int qy = dy - int(LOOK_STAR[k].y);
    uint q = PrimPixel(p, qx, qy);
    if ((q & 0x100u) == 0u || LookTone(at, q & 0xFFu) < 0.55)
      continue;
    float2 own = float2(LookFramePos(p, qx, qy)) + 0.5 + float(p.entry & 0xFFu) * 0.37;
    if (LookHash(own) < 1.0 - 0.03 * amount)
      continue;
    s += pow(max(0.0, sin(t * 1.9 + LookHash(own + 17.0) * 6.283)), 28.0) * LOOK_STAR[k].z;
  }
  return s;
}

// Pixel dx, dy of looked primitive p whose frame colour is s: under is the colour beneath it
// and flat the one the 8-bit recolour gives.
float3 LookColour(Prim p, uint look, uint s, int dx, int dy, float3 under, float3 flat)
{
  uint at = LookAt(look);
  float4 mat = LookF4(at, 352u);  // clarity, shine, glow, sparkle
  bool gas = gFrame.Load(at + 400u) != 0u;
  // the zone that takes the entry: its ramp, its material and how much of it shows
  uint zone = (gFrame.Load(at + 544u + (s & ~3u)) >> ((s & 3u) * 8u)) & 3u;
  g_lookRamp = zone == 0u ? 0u : 448u + zone * 96u;
  float4 zm = LookF4(at, 1088u + zone * 16u);  // material, mix
  gas = zm.x > 0.5 && zm.x < 1.5;
  float t = LookTime();
  float pulse = 0.8 + 0.2 * sin(t * 2.2);
  float3 glowCol = LookRampAt(at, 4u);
  // how deep in the frame it is, and how much of what is round it the look takes
  float nOpaque = 0.0;
  float nTaken = 0.0;
  for (uint r = 2u; r <= 8u; r += 3u)
    for (uint k = 0; k < 8u; ++k) {
      uint q = PrimPixel(p, dx + LOOK_RING[k].x * int(r), dy + LOOK_RING[k].y * int(r));
      if ((q & 0x100u) == 0u)
        continue;
      nOpaque += 1.0;
      if (LookTone(at, q & 0xFFu) >= 0.0)
        nTaken += 1.0;
    }
  float thick = saturate((nOpaque / 24.0 - 0.45) * 1.8);
  float tone = LookTone(at, s);
  if (tone < 0.0) {
    // the rock and the metal round it, lit by what glows beside them
    float g = nTaken / 24.0 * 1.3;
    return saturate(flat + (flat * glowCol * 1.2 + glowCol * 0.08) * g * mat.z);
  }
  // paint: the zone's ramp at the tone, shaded as the art is; glow: the ramp added as light
  if (zm.x > 1.5) {
    float3 own = Pal(s);
    float3 paint = LookRamp(at, tone);
    if (zm.x > 2.5)
      return saturate(own * (1.0 - 0.5 * zm.y) + paint * zm.y * 1.2 * pulse);
    return saturate(lerp(own, paint, zm.y));
  }
  // how it faces: a crystal's faces turned out from its middle and toward the art's light by
  // how bright each band of it was drawn; gas and metal by the slope of their brightness
  int2 fp = LookFramePos(p, dx, dy);
  float2 v;
  if (gas) {
    float l0 = LookLuma(s);
    uint a = PrimPixel(p, dx - 1, dy), b = PrimPixel(p, dx + 1, dy), c = PrimPixel(p, dx, dy - 1), d = PrimPixel(p, dx, dy + 1);
    float la = (a & 0x100u) != 0u ? LookLuma(a) : l0, lb = (b & 0x100u) != 0u ? LookLuma(b) : l0;
    float lc = (c & 0x100u) != 0u ? LookLuma(c) : l0, ld = (d & 0x100u) != 0u ? LookLuma(d) : l0;
    v = -float2(lb - la, ld - lc) * 0.5 * 2.8;
  } else {
    Entry entry = gEntries[p.entry];
    float2 size = float2(entry.size & 0xFFFFu, entry.size >> 16);
    float rad = max(4.0, 0.4 * min(size.x, size.y));
    float2 o = (float2(fp) + 0.5 - size * 0.5) / rad;
    float ol = length(o);
    if (ol > 1.0)
      o /= ol;
    float band = (floor(tone * 6.0) + 0.5) / 6.0;
    v = (o * 0.45 + LOOK_ART_LIGHT * clamp((band - 0.5) * 2.0, -1.0, 1.0) * 0.55) * 0.875;
  }
  float l2 = dot(v, v);
  if (l2 > 0.85) {
    v *= sqrt(0.85 / l2);
    l2 = 0.85;
  }
  float3 nrm = float3(v, sqrt(1.0 - l2));
  float relit = max(dot(nrm, LOOK_SUN), 0.0) / max(LOOK_SUN.z, 0.2);
  float3 col = LookRamp(at, tone) * lerp(1.0, 0.4 + 0.6 * relit, 0.55);
  if (!gas) {
    // the ground under it, deeper toward amber where it is thick
    float3 tint = LookRampAt(at, 3u);
    float3 absorb = exp(-(1.0 - tint) * 1.1 * (0.4 + thick * 2.6));
    float edge = pow(1.0 - saturate(nrm.z), 1.5) * 0.45;
    float3 through = under * absorb * lerp(float3(1, 1, 1), tint * 1.25, 0.3) + col * 0.3;
    col = lerp(col, through, saturate(mat.x * (1.0 - edge)));
    // a warm sky, more at faces turned away, glints toward the sun, and a sweep now and then
    float3 V = normalize(float3(0.0, 0.45, 1.0));
    float3 R = reflect(-V, nrm);
    float3 sky = lerp(LookRampAt(at, 5u), float3(1, 1, 1), 0.5);
    float3 ground = LookRampAt(at, 1u) * 0.6;
    float3 env = lerp(ground, sky, saturate(R.y * -0.5 + 0.55 + R.z * 0.25));
    float f = 0.04 + 0.96 * pow(1.0 - saturate(dot(nrm, V)), 5.0);
    float spec = pow(max(dot(R, LOOK_SUN), 0.0), 70.0) * 2.0;
    float sweep = smoothstep(0.06, 0.0, abs(frac((float(fp.x) + float(fp.y) * 0.6) / 150.0 - t * 0.16) - 0.5));
    col = lerp(col, env, saturate(mat.y * f * 0.9)) +
          (spec + sweep * 0.55) * mat.y * lerp(LookRampAt(at, 5u), float3(1, 1, 1), 0.6);
  }
  // light inside it, strongest at its heart
  col += glowCol * (0.25 + 0.75 * thick) * mat.z * (gas ? 0.6 : 0.45) * pulse;
  if (!gas)
    col += float3(1.0, 0.97, 0.86) * LookSparkle(p, at, dx, dy, mat.w, t) * mat.w * 1.6;
  return saturate(lerp(Pal(s), saturate(col), zm.y));
}

int LookLow(uint u)
{
  return int(u << 16) >> 16;
}

int LookHigh(uint u)
{
  return int(u) >> 16;
}

// Past a crystal's edge, at dx, dy of its frame (the halo primitive carries the image's frame):
// the glow of the crystal near it over what is behind, as the design gathers it (richfx.js,
// gathered), in steps, and the arms of its stars that reach past the edge.
float3 LookHalo(Prim p, uint at, int dx, int dy, float3 c, bool steps, int2 mp)
{
  if ((PrimPixel(p, dx, dy) & 0x100u) != 0u)
    return c;
  float4 mat = LookF4(at, 352u);  // clarity, shine, glow, sparkle
  float acc = 0.0;
  float ws = 0.0;
  for (int j = -3; j <= 3; ++j)
    for (int i = -3; i <= 3; ++i) {
      float w = 1.0 / (1.0 + float(i * i + j * j));
      uint q = PrimPixel(p, dx + int(floor(float(i) * 1.5 + 0.5)), dy + int(floor(float(j) * 1.5 + 0.5)));
      if ((q & 0x100u) != 0u && LookTone(at, q & 0xFFu) >= 0.0)
        acc += w;
      ws += w;
    }
  float t = LookTime();
  float hl = acc / ws * mat.z * 1.5 * (0.8 + 0.2 * sin(t * 2.2));
  if (steps)
    hl = floor(hl * 6.0 + LookBayer(mp)) / 6.0;
  float3 glowCol = LookRampAt(at, 4u);
  float3 col = c * (1.0 + glowCol * hl * 0.9) + glowCol * hl * 0.1;
  col += float3(1.0, 0.97, 0.86) * LookSparkle(p, at, dx, dy, mat.w, t) * mat.w * 1.2;
  return saturate(col);
}

// Whether pixel dx + i, dy + j of the frame is drawn, keeping the least squared distance to one.
void LookNear(Prim p, int dx, int dy, int i, int j, inout float best)
{
  float d2 = float(i * i + j * j);
  if (d2 < best && (PrimPixel(p, dx + i, dy + j) & 0x100u) != 0u)
    best = d2;
}

// c turned round the grey axis by a radians: an aura's hue as it cycles.
float3 LookHueTurn(float3 c, float a)
{
  float3 k = float3(0.57735, 0.57735, 0.57735);
  float ca = cos(a);
  return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

// The aura round a looked image, at dx, dy of its frame (the primitive carries
// the image's frame), mp its place on the map: past the frame's edge and a gap, a line of light
// hugging it and a glow beyond to reach pixels, and the frame's own edge lit. Its colours are the
// look's or the player's (slot), by distance or turning in hue; bands march round the outline,
// the line crackles, the whole pulses and flickers, a spark now and then, and embers rise off the
// edges that face up. In steps with a dither, in blocks of one or two game pixels, timed by
// classic's frames two at a time. It goes only where no sprite is drawn yet, so a turret's aura
// stays off its base, unless the look lets it over other sprites.
float3 LookAura(Prim p, uint at, int dx, int dy, float3 c, bool steps, int2 mp, int reach, uint sprite, uint slot)
{
  float4 glow = LookF4(at, 416u);   // the glow's colour, the strength
  float4 core = LookF4(at, 432u);   // the line's colour, the crackle
  float4 mid = LookF4(at, 448u);    // the middle colour, the opacity
  float4 shape = LookF4(at, 464u);  // the line's width, the gap, the falloff, the inner edge
  float4 move = LookF4(at, 480u);   // the speed, the pulse, its rate, the flicker
  float4 more = LookF4(at, 496u);   // the sparks, the embers, the bands, their speed
  float4 style = LookF4(at, 512u);  // the mode, the blend, the steps, the dither
  float4 last = LookF4(at, 528u);   // the block, the crackle's size, over other sprites
  float tick = floor(LookTime() * move.x / 0.084);
  float secs = tick * 0.084;
  float pulse = 1.0 - move.y * 0.5 + move.y * 0.5 * sin(tick * 0.5 * move.z);
  pulse *= 1.0 - move.w * 0.7 * LookHash(float2(tick * 0.731, float(p.entry & 0xFFFFu) * 0.013));
  float strength = glow.w * pulse;
  bool over = style.y > 0.5;
  uint mode = uint(style.x + 0.5);
  bool own = (PrimPixel(p, dx, dy) & 0x100u) != 0u;
  if (!own && sprite != 0u && last.z < 0.5)
    return c;
  // in blocks of two, each block's corner on the map stands for it
  int2 q = int2(dx, dy);
  int2 m = mp;
  if (last.x > 1.5) {
    int2 snapped = (mp >> 1) << 1;
    q += snapped - mp;
    m = snapped;
  }
  float flick = LookHash(float2(m) * 0.37 + tick * 1.618);
  float dith = style.w > 0.5 ? LookBayer(m) : 0.5;
  float levels = max(style.z, 2.0);
  float3 lineCol = core.rgb;
  float3 glowCol = glow.rgb;
  if (own) {
    // the frame's own edge, lit by the inner amount
    if (shape.w <= 0.0)
      return c;
    bool edge = false;
    for (int k = 0; k < 8; k += 2)
      edge = edge || (PrimPixel(p, dx + LOOK_RING[k].x, dy + LOOK_RING[k].y) & 0x100u) == 0u;
    if (!edge)
      return c;
    if (mode == 2u) {
      float3 s0 = Shade(slot, 0u), s7 = Shade(slot, 7u);
      glowCol = dot(s0, float3(0.299, 0.587, 0.114)) >= dot(s7, float3(0.299, 0.587, 0.114)) ? s0 : s7;
    } else if (mode == 3u) {
      glowCol = LookHueTurn(glow.rgb, secs * 1.2);
    }
    return over ? lerp(c, lineCol, saturate(strength * shape.w)) : saturate(c + glowCol * strength * shape.w);
  }
  // the nearest drawn pixel, ring by ring out to the reach
  float best = (PrimPixel(p, q.x, q.y) & 0x100u) != 0u ? 1.0 : 1e6;
  for (int r = 1; r <= reach && best > float(r * r); ++r)
    for (int k = -r; k <= r; ++k) {
      LookNear(p, q.x, q.y, k, -r, best);
      LookNear(p, q.x, q.y, k, r, best);
      if (k != -r && k != r) {
        LookNear(p, q.x, q.y, -r, k, best);
        LookNear(p, q.x, q.y, r, k, best);
      }
    }
  float d = max(sqrt(best), 1.0);
  float w = max(shape.x, 0.5);
  float e = d - shape.y;
  float u = saturate((e - w) / max(float(reach) - shape.y - w + 0.5, 0.5));
  bool near = best < 1e5 && e >= 0.5 && d <= float(reach) + 0.5;
  if (mode == 1u) {
    glowCol = u < 0.5 ? lerp(core.rgb, mid.rgb, u * 2.0) : lerp(mid.rgb, glow.rgb, u * 2.0 - 1.0);
  } else if (mode == 2u) {
    float3 s0 = Shade(slot, 0u), s7 = Shade(slot, 7u);
    bool lightFirst = dot(s0, float3(0.299, 0.587, 0.114)) >= dot(s7, float3(0.299, 0.587, 0.114));
    float3 light = lightFirst ? s0 : s7;
    lineCol = lerp(light, float3(1, 1, 1), 0.45);
    glowCol = lerp(light, lightFirst ? s7 : s0, 0.3 + 0.4 * u);
  } else if (mode == 3u) {
    lineCol = LookHueTurn(core.rgb, secs * 1.2);
    glowCol = LookHueTurn(glow.rgb, secs * 1.2 + u * 1.2);
  }
  // the bands round the outline, by the angle about the frame's middle
  float band = 1.0;
  if (more.z >= 1.0) {
    Entry entry = gEntries[p.entry];
    float2 o = float2(LookFramePos(p, q.x, q.y)) + 0.5 - float2(entry.size & 0xFFFFu, entry.size >> 16) * 0.5;
    band = 0.35 + 1.3 * pow(0.5 + 0.5 * cos(atan2(o.y, o.x) * more.z - secs * more.w * 2.0), 4.0);
  }
  float g = 0.0;
  float a = 0.0;
  if (near) {
    g = pow(1.0 - u, 1.5 * shape.z) * strength * band * (0.75 + 0.25 * flick);
    if (e <= w + 0.5) {
      float crack = LookNoise(float2(m) * 0.45 / max(last.y, 0.25) + float2(tick * 0.9, -tick * 0.6));
      a = saturate(strength * band * (crack > core.w * 0.7 ? 1.0 : 0.3) * (0.8 + 0.2 * flick));
    }
  }
  // an ember: one a column now and then, rising from the first drawn pixel below
  float ember = 0.0;
  if (more.y > 0.0) {
    int h = 0;
    for (int k = 1; k <= reach + 12 && h == 0; ++k)
      if ((PrimPixel(p, q.x, q.y + k) & 0x100u) != 0u)
        h = k;
    if (h > 0) {
      float column = float(LookFramePos(p, q.x, q.y).x);
      float run = secs * (0.3 + LookHash(float2(column, 3.1)) * 0.4) + LookHash(float2(column, 7.7));
      float life = frac(run);
      if (LookHash(float2(column, floor(run) * 1.37)) < more.y * 0.35 && abs(float(h) - 1.0 - life * float(reach + 10)) < 0.5)
        ember = 1.0 - life;
    }
  }
  if (steps) {
    g = floor(g * levels + dith) / levels;
    a = floor(a * (levels - 1.0) + dith) / (levels - 1.0);
    ember = floor(ember * 3.0 + dith) / 3.0;
  }
  float3 col = over ? lerp(c, glowCol, saturate(g)) : c + glowCol * g;
  col = lerp(col, lineCol, saturate(a));
  if (near && a <= 0.0 && flick > 1.0 - more.x * 0.06)
    col = lerp(col, lineCol, 0.85);
  if (ember > 0.0)
    col = over ? lerp(col, lineCol, ember) : col + lerp(lineCol, glowCol, 0.5) * ember;
  return saturate(lerp(c, col, mid.w));
}

// A look primitive over pixel x, y of colour c: the light on the ground where nothing but the
// terrain is drawn yet, a crystal's halo, or a vent's plume over whatever is.
float3 LookPrim(Prim p, int x, int y, float3 c, uint sprite)
{
  if (gLooksAt == 0u)
    return c;
  g_lookRamp = 0u;
  uint at = LookAt(p.param);
  float2 centre = float2(LookLow(p.clip0), LookHigh(p.clip0));
  float t = LookTime();
  bool steps = gFrame.Load(at + 404u) != 0u;
  float2 px = float2(x, y);
  int2 mp = int2(x + int(gScrollX), y + int(gScrollY));  // the dither stays on the map as it scrolls
  float seed = float((p.param >> 16) & 0xFFu);
  uint what = (p.param >> 8) & 0xFFu;
  if (what == 3u) {
    int reach = int((p.param >> 16) & 0xFFu);
    return LookHalo(p, at, x - p.x - reach, y - p.y - reach, c, steps, mp);
  }
  if (what == 4u) {
    int reach = int((p.param >> 16) & 0xFu);
    int rise = int((p.param >> 20) & 0xFu) * 2;
    return LookAura(p, at, x - p.x - reach, y - p.y - reach - rise, c, steps, mp, reach, sprite, p.param >> 24);
  }
  if (what == 1u) {
    if (sprite != 0u)
      return c;
    float r = float(p.entry & 0xFFFFu);
    float4 light = LookF4(at, 384u);
    float2 d = (px - centre) / float2(r, r * 0.55);
    float3 acc = light.rgb * exp(-dot(d, d) * 2.0) * (float(p.entry >> 16) / 1000.0) * light.w *
                 (0.88 + 0.12 * sin(t * 2.2 + seed * 1.3));
    float m = max(acc.r, max(acc.g, acc.b));
    if (steps && m > 0.0)
      acc *= (floor(m * 8.0 + LookBayer(mp)) / 8.0) / m;
    // the faint edge of a light adds little to black ground, so its steps do not show there
    return saturate(c * (1.0 + acc * 1.3) + acc * acc * 0.12);
  }
  // a column from the vent, widening and drifting as it rises, broken by noise that rises with it;
  // swollen higher, wider and denser while 1.17's puff there plays (port_looks.cpp, PuffSwell)
  float4 plume = LookF4(at, 368u);  // height, density, rise
  float strength = float(p.entry & 0xFFFFu) / 1000.0;
  float width = float(p.entry >> 16) / 100.0;
  float swell = float(p.param >> 24) / 255.0;
  float h = centre.y - px.y;
  if (h < -2.0)
    return c;
  float top = max(8.0, plume.x * (0.45 + 0.55 * strength)) * (1.0 + 0.5 * swell);
  float k = saturate(h / top);
  float sway = sin(h * 0.055 - t * 1.2 + seed * 1.9) * h * 0.09 + h * 0.16;
  float w = width * (1.0 + h * 0.075) * (1.0 + 0.5 * swell);
  float dx = (px.x - centre.x - sway) / w;
  float n = LookFbm(float2((px.x - sway) * 0.12 + seed * 7.3, (px.y + t * plume.z) * 0.1));
  float dd = max(0.0, exp(-dx * dx * 1.4) - 0.1) * (1.0 - k) * (1.0 - k) * smoothstep(-2.0, 2.5, h) * (0.5 + n);
  float a = saturate((dd * plume.y * (0.5 + 0.5 * strength) * (1.0 + 0.6 * swell) - 0.06) * 1.15);
  float core = dd * exp(-max(h, 0.0) * 0.07) * strength * (1.0 + 0.5 * swell);
  if (steps)
    a = floor(a * 4.0 + LookBayer(mp)) / 4.0;
  float3 col = lerp(LookRampAt(at, 2u), LookRampAt(at, 4u), saturate(core * 0.8 + a * 0.35));
  return lerp(c, col, min(a, 0.9));
}

// Primitive p, the n-th, over pixel x, y in full colour: c the colour so far, v the index
// before it and vAfter the one Apply gives. sprite becomes 1 where a sprite or a line draws.
// ---- a mod's mark painted on the ground (port_mapmark.h) ------------------------------------
//
// A mark primitive's entry is a byte offset into the marks' numbers, which port_mapmark.cpp
// writes in this order, 4 bytes each: the shape, its colour, the opacity of its inside and of
// its edge, the edge's width, its dash and the glow under it, the shape's places in the view's
// pixels, then the fade (how near a unit reveals it, the opacity it reaches and the one it
// rests at, and whether each unit lights only the ground round itself), its curve, the pulse's
// beat and depth with the mark's age, the crossings being answered, the numbers of the look it
// wears that the paint has a use for, and the units that reveal it with the corners of a
// polygon after them. The MARK_AT_ constants below are those offsets, and they move together
// with that order.
//
// It is drawn in the full colour alone and leaves the index map as it is, as a look's light
// does, and it is recorded before the sprites, so a sprite drawn later covers the paint.

static const uint MARK_CIRCLE = 0u;
static const uint MARK_RING = 1u;
static const uint MARK_RECT = 2u;
static const uint MARK_POLY = 3u;
static const uint MARK_LINE = 4u;
static const uint MARK_TINT = 5u;  // not a shape: a light under a unit standing outside a mark

// how a mark's light falls off over the fade's distance (port_mapmark.h's MAP_MARK_CURVE_*)
static const uint MARK_CURVE_SMOOTH = 0u;
static const uint MARK_CURVE_EVEN = 1u;
static const uint MARK_CURVE_SHARP = 2u;

// where each number sits in a mark's own bytes (port_mapmark.cpp's MarkWords, in its order)
static const uint MARK_AT_SHAPE = 0u;
static const uint MARK_AT_RGB = 4u;
static const uint MARK_AT_FILL = 8u;
static const uint MARK_AT_EDGE = 12u;
static const uint MARK_AT_WIDTH = 16u;
static const uint MARK_AT_DASH = 20u;
static const uint MARK_AT_SOFT = 24u;
static const uint MARK_AT_X = 28u;
static const uint MARK_AT_Y = 32u;
static const uint MARK_AT_W = 36u;      // a rectangle's width, else a circle's and a ring's radius
static const uint MARK_AT_H = 40u;      // its height, else a line's second x
static const uint MARK_AT_Y2 = 44u;
static const uint MARK_AT_NEAR = 48u;
static const uint MARK_AT_MOST = 52u;
static const uint MARK_AT_LEAST = 56u;
static const uint MARK_AT_UNITS = 60u;  // how many revealing units
static const uint MARK_AT_POINTS = 64u; // and how many corners a polygon has
static const uint MARK_AT_CURVE = 68u;
static const uint MARK_AT_PULSE = 72u;  // the beat in seconds, its depth, and the mark's age
static const uint MARK_AT_DEPTH = 76u;
static const uint MARK_AT_AGE = 80u;
static const uint MARK_AT_EVENTS = 84u;  // how many crossings are being answered
static const uint MARK_AT_LEN = 88u;     // the edge's whole length, which a ripple wraps on
static const uint MARK_AT_CLOSED = 92u;  // and whether the shape closes, so that it wraps at all
// the look's own numbers the paint has a use for: the foot's glow, in the edge's widths and with
// its opacity, 0 widths for a look with no foot; the colour a unit on its way out is answered
// in; and the ripple a crossing sends along the edge, its wave and then its flash
static const uint MARK_AT_FOOT = 96u;
static const uint MARK_AT_FOOT_ALPHA = 100u;
static const uint MARK_AT_WARN = 104u;
static const uint MARK_AT_SPEED = 108u;
static const uint MARK_AT_WIDE = 112u;
static const uint MARK_AT_LIFE = 116u;
static const uint MARK_AT_FLASH = 120u;
static const uint MARK_AT_FLASH_LIFE = 124u;
static const uint MARK_AT_SIDE = 128u;
static const uint MARK_UNITS_AT = 132u;  // the revealing units, after the numbers above, then a
                                         // polygon's corners and then the crossings, four
                                         // numbers each: where on the edge, how long ago, out,
                                         // and how much of the step ran along the edge

float MarkReal(uint at)
{
  return asfloat(gFrame.Load(at));
}

float2 MarkPoint(uint at, uint i)
{
  return float2(MarkReal(at + i * 8u), MarkReal(at + 4u + i * 8u));
}

// The distance from px to the segment ab, with how far along it the nearest point lies (which
// a dash is cut by), and the ray down from px counted where it crosses the segment, so a
// polygon's inside is where the crossings are odd.
float MarkSeg(float2 px, float2 a, float2 b, inout float phase, inout bool bIn)
{
  float2 ab = b - a, ap = px - a;
  float t = saturate(dot(ap, ab) / max(dot(ab, ab), 1e-6));
  if ((a.y > px.y) != (b.y > px.y) && px.x < a.x + (b.x - a.x) * (px.y - a.y) / (b.y - a.y))
    bIn = !bIn;
  phase = t * length(ab);
  return length(ap - ab * t);
}

// The mark's distance at px, under 0 inside the shape, with the dash's phase along its edge.
// A dash begins again at each corner, so the dashes of a side are whole.
float MarkDist(uint at, uint shape, uint nUnits, uint nPoints, float2 px, out float phase)
{
  float2 c = float2(MarkReal(at + MARK_AT_X), MarkReal(at + MARK_AT_Y));
  float a = MarkReal(at + MARK_AT_W), b = MarkReal(at + MARK_AT_H);
  phase = 0.0;
  if (shape == MARK_CIRCLE || shape == MARK_RING) {
    float2 d = px - c;
    phase = (atan2(d.y, d.x) + 3.14159265) * max(a, 1.0);
    return length(d) - a;
  }
  if (shape == MARK_RECT) {
    float2 half2 = float2(a, b) * 0.5;
    float2 q = abs(px - (c + half2)) - half2;
    phase = px.x + px.y;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
  }
  bool bIn = false;
  if (shape == MARK_LINE) {
    float2 q = float2(MarkReal(at + MARK_AT_H), MarkReal(at + MARK_AT_Y2));
    return MarkSeg(px, c, q, phase, bIn);
  }
  // a polygon: its corners after the units, the sides closed
  uint pts = at + MARK_UNITS_AT + nUnits * 8u;
  float d = 1e9;
  for (uint i = 0u; i < nPoints; ++i) {
    float2 p0 = MarkPoint(pts, i);
    float2 p1 = MarkPoint(pts, (i + 1u) % nPoints);
    float f = 0.0;
    float one = MarkSeg(px, p0, p1, f, bIn);
    if (one < d) {
      d = one;
      phase = f;
    }
  }
  return bIn ? -d : d;
}

// The light at a part of the fade's distance, as port_mapmark.cpp's Falloff gives it, so the
// paint and the layer fade a mark the same way.
float MarkFalloff(float x, uint curve)
{
  if (x >= 1.0)
    return 0.0;
  if (x <= 0.0)
    return 1.0;
  if (curve == MARK_CURVE_EVEN)
    return 1.0 - x;
  if (curve == MARK_CURVE_SHARP) {
    if (x < 0.55)
      return 1.0;
    float s = 1.0 - (x - 0.55) / 0.45;
    return s * s;
  }
  float t = 1.0 - x;
  return t * t * (3.0 - 2.0 * t);
}

// What the crossings add at this point of the edge, 0 to 1, and in warm the share of that from
// a unit on its way out, as port_mapmark.cpp's Boost gives them: the ripple block's wave running
// away from the place, and its flash at the place itself, each over its own life.
float MarkBoost(uint at, uint nUnits, uint nPoints, float phase, out float warm)
{
  warm = 0.0;
  uint n = gFrame.Load(at + MARK_AT_EVENTS);
  if (n == 0u)
    return 0.0;
  float len = MarkReal(at + MARK_AT_LEN);
  bool closed = gFrame.Load(at + MARK_AT_CLOSED) != 0u;
  float speed = MarkReal(at + MARK_AT_SPEED), wide = max(MarkReal(at + MARK_AT_WIDE), 1e-3);
  float life = max(MarkReal(at + MARK_AT_LIFE), 1e-3);
  float flash = max(MarkReal(at + MARK_AT_FLASH), 1e-3);
  float flashLife = max(MarkReal(at + MARK_AT_FLASH_LIFE), 1e-3);
  float side = MarkReal(at + MARK_AT_SIDE);
  uint ev = at + MARK_UNITS_AT + nUnits * 8u + nPoints * 8u;
  float boost = 0.0;
  for (uint i = 0u; i < n; ++i) {
    float s = MarkReal(ev + i * 16u);
    float tau = MarkReal(ev + i * 16u + 4u);
    float went = MarkReal(ev + i * 16u + 8u);
    float way = MarkReal(ev + i * 16u + 12u);
    float d = phase - s;
    if (closed && len > 0.0)
      d = d > len * 0.5 ? d - len : (d < len * -0.5 ? d + len : d);
    float ds = abs(d);
    float a = (ds - tau * speed) / wide, b = ds / flash;
    // the wave runs away both ways alike; the flash leans to the side the unit was heading for
    float lean = max(0.0, 1.0 + side * way * (d >= 0.0 ? 1.0 : -1.0));
    float f = exp(-a * a) * max(0.0, 1.0 - tau / life) +
              exp(-b * b) * max(0.0, 1.0 - tau / flashLife) * lean;
    boost = max(boost, f);
    if (went > 0.5)
      warm = max(warm, f);
  }
  warm = saturate(warm);
  return saturate(boost);
}

float3 MarkPrim(Prim p, int x, int y, float3 c)
{
  if (gMarksAt == 0u)
    return c;
  uint at = gMarksAt + p.entry;
  uint shape = gFrame.Load(at + MARK_AT_SHAPE);
  uint rgb = gFrame.Load(at + MARK_AT_RGB);
  float3 colOf = float3(float((rgb >> 16) & 0xFFu), float((rgb >> 8) & 0xFFu), float(rgb & 0xFFu)) / 255.0;
  // a light under a unit standing outside a mark: a round gradient on the ground, and its own
  // few numbers rather than a shape's (port_mapmark.cpp's OutWords)
  if (shape == MARK_TINT) {
    float2 ctr = float2(MarkReal(at + MARK_AT_X), MarkReal(at + MARK_AT_Y));
    float r = max(MarkReal(at + MARK_AT_W), 1.0);
    float a = MarkReal(at + MARK_AT_FILL) * saturate(1.0 - length(float2(x, y) - ctr) / r);
    return a <= 0.004 ? c : lerp(c, colOf, a);
  }
  float fill = MarkReal(at + MARK_AT_FILL), edge = MarkReal(at + MARK_AT_EDGE);
  float width = max(MarkReal(at + MARK_AT_WIDTH), 0.75);
  float dash = MarkReal(at + MARK_AT_DASH), soft = MarkReal(at + MARK_AT_SOFT);
  float near = MarkReal(at + MARK_AT_NEAR), most = MarkReal(at + MARK_AT_MOST);
  float least = MarkReal(at + MARK_AT_LEAST);
  uint nUnits = gFrame.Load(at + MARK_AT_UNITS);
  uint nPoints = gFrame.Load(at + MARK_AT_POINTS);
  uint curve = gFrame.Load(at + MARK_AT_CURVE);
  float2 px = float2(x, y);
  float phase = 0.0;
  float d = MarkDist(at, shape, nUnits, nPoints, px, phase);
  // the fade: each unit lights the ground round itself. A mark that rises whole instead comes
  // with the opacity it has reached and no units, and is left alone here.
  float reveal = 1.0;
  if (near > 0.0) {
    reveal = 0.0;
    for (uint i = 0u; i < nUnits; ++i) {
      float2 u = MarkPoint(at + MARK_UNITS_AT, i);
      reveal = max(reveal, MarkFalloff(length(px - u) / near, curve));
    }
  }
  // a crossing lights its own stretch of the edge, over whatever a unit near it lights
  float warm = 0.0;
  reveal = max(reveal, MarkBoost(at, nUnits, nPoints, phase, warm));
  float alpha = least + (most - least) * reveal;
  // the pulse, at this pixel's own point of the edge, so the swell runs along it as the layer
  // draws it (a rectangle's phase runs across the shape rather than round it, as its dash does)
  float pulse = MarkReal(at + MARK_AT_PULSE), depth = MarkReal(at + MARK_AT_DEPTH);
  if (pulse > 0.0 && depth > 0.0)
    alpha *= 1.0 - depth * 0.5 * (1.0 + sin(MarkReal(at + MARK_AT_AGE) * 6.2831853 / pulse - phase / 90.0));
  if (alpha <= 0.004)
    return c;
  // a pixel of the layer is this many of the game's with the camera far out, so the edge keeps
  // its softness at every zoom
  float step = max(1.0, float(gStep));
  float paint = 0.0;
  if (fill > 0.0 && shape != MARK_RING && shape != MARK_LINE)
    paint = fill * saturate(-d / step);
  if (edge > 0.0) {
    float band = abs(d) - width * 0.5;
    float lit = saturate(1.0 - band / step);
    if (dash > 0.0 && fmod(abs(phase), dash * 2.0) >= dash)
      lit = 0.0;
    paint = max(paint, edge * lit);
    // the glow under the edge: the foot block's, as many of the edge's widths wide as it asks
    // for, which is the light standing on the edge spilling onto the ground; else soft's own
    float foot = MarkReal(at + MARK_AT_FOOT);
    if (foot > 0.0)
      paint = max(paint, edge * MarkReal(at + MARK_AT_FOOT_ALPHA) * saturate(1.0 - band / (width * foot)));
    else if (soft > 0.0)
      paint = max(paint, edge * 0.3 * saturate(1.0 - band / (width * soft + step)));
  }
  paint = saturate(paint * alpha);
  if (paint <= 0.004)
    return c;
  // the way a unit went shows in the colour: out turns the paint toward the look's warning
  // colour, which the layer draws as a second stroke over its own
  uint warnRgb = gFrame.Load(at + MARK_AT_WARN);
  float3 warnOf = float3(float((warnRgb >> 16) & 0xFFu), float((warnRgb >> 8) & 0xFFu),
                         float(warnRgb & 0xFFu)) / 255.0;
  float3 col = lerp(colOf, warnOf, warm);
  return lerp(c, col, paint);
}

float3 ApplyColour(Prim p, uint n, int x, int y, uint nAt, uint v, uint vAfter, float3 c, inout uint sprite)
{
  uint kind = p.kind & 0xFFu;
  if (kind == PRIM_FOG)
    return FogColour(p, uint(x), uint(y), c, sprite);
  if (kind == PRIM_SPAN) {
    int d = int(nAt) - p.x;
    if (d < 0 || (uint(d) % p.clip0) != 0u || uint(d) / p.clip0 >= uint(p.y))
      return c;
    sprite = 1u;
    return Pal(vAfter);
  }
  int cw = int(p.clip1 & 0xFFFFu);
  int ch = int(p.clip1 >> 16);
  int dx = x - p.x;
  int dy = y - p.y;
  if (dx < 0 || dy < 0 || dx >= cw || dy >= ch)
    return c;
  if (kind == PRIM_LOOK)
    return LookPrim(p, x, y, c, sprite);
  if (kind == PRIM_MARK)
    return sprite != 0u ? c : MarkPrim(p, x, y, c);
  if (kind == PRIM_STAR)
    return v == 0u ? Pal(vAfter) : c;
  if (kind == PRIM_BLIT) {
    if ((p.param & 0xFFu) == 1u && FrameByte(p.table + uint(dy * cw + dx)) == 0u)
      return c;
    sprite = 1u;
    return Pal(vAfter);
  }
  uint s = PrimPixel(p, dx, dy);
  if ((s & 0x100u) == 0u)
    return c;
  s &= 0xFFu;
  if (kind == PRIM_NORMAL) {
    // a looked image, and 1.17's puffs where the look's plume replaces them
    uint look = gLooksAt != 0u ? ExtraWord(n, 1u) : 0u;
    if ((look & 0x100u) != 0u)
      return c;
    sprite = 1u;
    if (look != 0u)
      return LookColour(p, look, s, dx, dy, c, Remapped(n, s, vAfter));
    return Remapped(n, s, vAfter);
  }
  if (kind == PRIM_SHADOW || kind == PRIM_FADE)
    return saturate(c * ExtraFit(n).xyz);
  if (kind == PRIM_EFFECT) {
    // 1.17's own table on the index, its change carried onto the colour under: exact where
    // that colour is the palette's, and a team shade under it keeps its detail. No blend of
    // the frame's colour fits these tables, since a remapped light's frame (the SCV's
    // engine, the warp-in's glow) holds levels into its table, not colours.
    sprite = 1u;
    return saturate(c + Pal(vAfter) - Pal(v));
  }
  if (kind == PRIM_UNCLOAKED) {
    uint r = Table(p.table + s);
    if (SignedByte(Table((p.param >> 8) + r)) <= SignedByte(p.param & 0xFFu))
      return c;
    sprite = 1u;
    return Remapped(n, s, r);
  }
  if (kind == PRIM_GLYPH || kind == PRIM_WARP) {
    sprite = 1u;
    return Pal(vAfter);
  }
  return c;
}

[numthreads(8, 8, 1)]
void CSSceneFull(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u + (gOrigin & 0xFFFFu);
  uint y = id.y + (gOrigin >> 16);
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint old = gFromTerrain != 0u ? 0u : gMap.Load(nAt);
  uint oldTouched = gFromTerrain != 0u ? 0u : gTouched.Load(nAt);
  uint bin = (y / 32u) * gBinsX + x0 / 32u;
  uint nStart = gBinListAt + FrameWord(bin);
  uint nEnd = gBinListAt + FrameWord(bin + 1u);
  uint nStale = 0;
  uint word = 0;
  uint touched = 0;
  uint2 rank = RankLoad(nAt);
  uint gy = y * gStep + (gStep >> 1);
  for (uint i = 0; i < 4u; ++i) {
    uint gx = (x0 + i) * gStep + (gStep >> 1);
    uint v = gFromTerrain != 0u ? Terrain(gScrollX + gx, gScrollY + gy, nStale) : (old >> (i * 8u)) & 0xFFu;
    uint t = (oldTouched >> (i * 8u)) & 0xFFu;
    uint rgb = gFromTerrain != 0u ? 0u : gMapRgb.Load((nAt + i) * 4u);
    float3 c = gFromTerrain != 0u ? Pal(v) : Unpack(rgb);
    uint sprite = gFromTerrain != 0u ? 0u : rgb >> 24;
    for (uint k = nStart; k < nEnd; ++k) {
      uint n = FrameWord(k);
      if (n < gFirst)
        continue;
      if (n >= gLast)
        break;
      Prim p = gPrims[n];
      uint before = v;
      v = Apply(p, int(gx), int(gy), gy * gViewW + gx, v);
      c = ApplyColour(p, n, int(gx), int(gy), gy * gViewW + gx, before, v, c, sprite);
      if (v == before || (p.kind & 0xFFu) == PRIM_FOG || (p.kind & 0xFFu) == PRIM_STAR)
        continue;
      if (RankKind(p))
        rank = RankSet(rank, i, p);
      t |= n < gOverFrom ? 1u : 3u;
    }
    word |= v << (i * 8u);
    touched |= t << (i * 8u);
    gMapRgb.Store((nAt + i) * 4u, Pack(c, sprite));
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
  if (gUnderAt != 0u)
    gTouched.Store2(3u * gUnderAt + 2u * nAt, rank);
  if (nStale != 0) {
    uint nOld;
    gStats.InterlockedAdd(0, nStale, nOld);
  }
}

// A distortion in full colour: as CSDistort, the colour from the same pixel of the map's
// colours before it.
[numthreads(8, 8, 1)]
void CSDistortFull(uint3 id : SV_DispatchThreadID)
{
  Prim p = gPrims[gPrim];
  int y = int(gOrigin + id.y);
  int x0 = (int(gFirst) + int(id.x)) * 4;
  int gy = y * int(gStep) + int(gStep >> 1);
  if (id.x >= gLast || y >= int(gRows) || x0 < 0 || x0 >= int(gWidth) || gy < p.y ||
      gy - p.y >= int(p.clip1 >> 16))
    return;
  uint nAt = uint(y) * gWidth + uint(x0);
  uint word = gPrev.Load(nAt);
  uint touched = gTouched.Load(nAt);
  for (uint i = 0; i < 4u; ++i) {
    uint gx, nShift;
    if (!DistortAt(p, x0 + int(i), gy, gx, nShift))
      continue;
    uint nSrc = DistortFrom(p, gx, uint(gy), nShift);
    uint b = LoadByte(gPrev, nSrc);
    word = (word & ~(0xFFu << (i * 8u))) | (b << (i * 8u));
    touched |= 1u << (i * 8u);
    gMapRgb.Store((nAt + i) * 4u, (gPrevRgb.Load(nSrc * 4u) & 0xFFFFFFu) | (1u << 24));
  }
  gMap.Store(nAt, word);
  gTouched.Store(nAt, touched);
}

// CSShow in full colour: the screen's copy of the colours too.
[numthreads(8, 8, 1)]
void CSShowFull(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u;
  uint y = id.y;
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint map = gMap.Load(nAt);
  uint word = gShown.Load(nAt);
  for (uint i = 0; i < 4u; ++i)
    if (FrameByte(gCopiedAt + nAt + i) != 0u) {
      word = (word & ~(0xFFu << (i * 8u))) | (map & (0xFFu << (i * 8u)));
      gShownRgb.Store((nAt + i) * 4u, gMapRgb.Load((nAt + i) * 4u));
    }
  gShown.Store(nAt, word);
}

// ---------------------------------------------------------------------------
// With the camera zoomed out (port_view.cpp's render view), the map layer above is
// the render view's, and CSHud draws what was drawn over the map at the screen's size (the
// dialogs, the map's text, the drag box, the cursor): the primitives gFirst up to gLast,
// each square's listed from word gPrim of the frame, over the map at each pixel the screen
// takes (the render view's pixel nearest the camera's), into the GPU's copy of the screen.
// gHud says where they drew, which the presenter shows in place of the zoomed map.
[numthreads(8, 8, 1)]
void CSHud(uint3 id : SV_DispatchThreadID)
{
  uint x0 = id.x * 4u;
  uint y = id.y;
  if (x0 >= gWidth || y >= gRows)
    return;
  uint nAt = y * gWidth + x0;
  uint bin = (y / 32u) * gBinsX + x0 / 32u;
  uint nStart = gBinListAt + FrameWord(gPrim + bin);
  uint nEnd = gBinListAt + FrameWord(gPrim + bin + 1u);
  uint word = gShown.Load(nAt);
  uint hud = 0u;
  for (uint i = 0; i < 4u; ++i) {
    uint x = x0 + i;
    if (FrameByte(gCopiedAt + nAt + i) == 0u)
      continue;
    int mx = int(floor(gCamX + (float(x) + 0.5) * gZoom)) - int(gMapX);
    int my = int(floor(gCamY + (float(y) + 0.5) * gZoom)) - int(gMapY);
    uint v = 0u;
    uint rgb = 0u;
    if (mx >= 0 && my >= 0 && uint(mx) < gMapW && uint(my) < gMapH) {
      uint m = (uint(my) / gMapStep) * gMapStride + uint(mx) / gMapStep;
      v = (gMap.Load(m & ~3u) >> ((m & 3u) * 8u)) & 0xFFu;
      if (gHudFull != 0u)
        rgb = gMapRgb.Load(m * 4u);
    }
    bool drew = false;
    for (uint k = nStart; k < nEnd; ++k) {
      uint n = FrameWord(k);
      if (n < gFirst)
        continue;
      if (n >= gLast)
        break;
      uint before = v;
      v = Apply(gPrims[n], int(x), int(y), nAt + i, v);
      drew = drew || v != before;
    }
    word = (word & ~(0xFFu << (i * 8u))) | (v << (i * 8u));
    if (drew)
      hud |= 1u << (i * 8u);
    if (gHudFull != 0u)
      gShownRgb.Store((nAt + i) * 4u, drew ? Pack(Pal(v), 1u) : rgb);
  }
  gShown.Store(nAt, word);
  gHud.Store(nAt, hud);
}
