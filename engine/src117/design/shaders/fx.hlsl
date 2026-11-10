// fx.hlsl -- what the design engine draws over the game's frame (design_draw.cpp): the units' paths and
// the effects' shapes, each one quad the vertex shader builds from a list of primitives in a
// storage buffer, its shape worked out at each pixel from its distance to it. One draw for each
// run of the list in one blend, added light or laid over.
//
// Built by tools/build_design_x64.py with dxc, to DXIL for Direct3D 12 and to SPIR-V for Vulkan. The
// registers follow SDL_GPU's layout: a vertex shader's storage buffers in space 0 (set 0) and its
// uniforms in space 1 (set 1), a fragment shader's textures in space 2 (set 2) and its uniforms in
// space 3 (set 3).

struct FxPrim
{
  float4 head;  // kind, blend (0 added, 1 over), dashes; a line's round caps (1 its start, 2 its end)
  float4 at;    // the centre x, y; a line's ends, a triangle's first two corners
  float4 axes;  // a circle's x axis and y axis in pixels, its radius long; a line's widths (its
                // own, its core's, its edge's); a triangle's third corner
  float4 ring;  // the inner radius over the outer, the arc's first and last angle, its turn
  float4 c0;    // straight r, g, b, a
  float4 c1;
  float4 c2;
  float4 clip;  // x0, y0, x1, y1 in the target
};

// The kinds (design_engine.h, DesignPrimKind).
static const int KIND_GLOW = 0;
static const int KIND_DOT = 1;
static const int KIND_RING = 2;
static const int KIND_LINE = 3;
static const int KIND_STAR = 4;
static const int KIND_TRI = 5;
static const int KIND_WASH = 6;

[[vk::binding(0, 0)]] StructuredBuffer<FxPrim> gPrims : register(t0, space0);

[[vk::binding(0, 1)]] cbuffer FxTarget : register(b0, space1)
{
  float2 gTarget;  // the target's size in pixels
  float2 gUnused;
};

// The classic console's mask at the game's pixels, 1 where the console is drawn, so nothing is
// drawn over it; where the game's frame is in the target; and whether an effect is drawn in the
// game's pixels: each cell of them one colour, worked out at its middle, with hard edges, its light
// and cover in steps with a 4x4 ordered dither, and few levels of each colour.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> gMask : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState gMaskSampler : register(s0, space2);

[[vk::binding(0, 3)]] cbuffer FxMask : register(b0, space3)
{
  float4 gBox;   // the game's frame: x, y, and target pixels a game pixel across and down
  float2 gGame;  // its size in game pixels
  float gMaskOn;
  float gMaskUnused;
  float4 gPixel;    // on, a cell in game pixels (1 to 4, or a half, a third or a quarter of one), the steps of
                    // light and cover, the levels of each colour (0 any)
  float4 gPixelAt;  // the dither's offset in cells, so it stays on the map as it scrolls
};

static const float BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

struct FxOut
{
  float4 pos : SV_Position;
  nointerpolation float4 head : TEXCOORD0;
  nointerpolation float4 at : TEXCOORD1;
  nointerpolation float4 axes : TEXCOORD2;
  nointerpolation float4 ring : TEXCOORD3;
  nointerpolation float4 c0 : TEXCOORD4;
  nointerpolation float4 c1 : TEXCOORD5;
  nointerpolation float4 c2 : TEXCOORD6;
  nointerpolation float4 clip : TEXCOORD7;
};

// The box a primitive can draw in, a pixel and a half wider for its soft edge.
float4 Bounds(FxPrim p)
{
  int kind = (int)p.head.x;
  float2 lo, hi;
  if (kind == KIND_LINE) {
    float h = max(p.axes.x, p.axes.z) * 0.5 + 1.5;
    lo = min(p.at.xy, p.at.zw) - h;
    hi = max(p.at.xy, p.at.zw) + h;
  } else if (kind == KIND_TRI) {
    lo = min(min(p.at.xy, p.at.zw), p.axes.xy) - 1.5;
    hi = max(max(p.at.xy, p.at.zw), p.axes.xy) + 1.5;
  } else if (kind == KIND_STAR) {
    lo = p.at.xy - p.axes.x - 2.0;
    hi = p.at.xy + p.axes.x + 2.0;
  } else if (kind == KIND_WASH) {
    lo = float2(0, 0);
    hi = gTarget;
  } else {
    float2 e = abs(p.axes.xy) + abs(p.axes.zw) + 1.5;
    lo = p.at.xy - e;
    hi = p.at.xy + e;
  }
  return float4(max(lo, p.clip.xy), min(hi, p.clip.zw));
}

// Six vertices a primitive, two triangles over its box.
FxOut VSFx(uint id : SV_VertexID)
{
  static const float2 kCorner[6] = { float2(0, 0), float2(1, 0), float2(0, 1),
                                     float2(1, 0), float2(1, 1), float2(0, 1) };
  FxPrim p = gPrims[id / 6u];
  float4 b = Bounds(p);
  // a primitive wholly clipped away collapses to a point
  if (b.z <= b.x || b.w <= b.y)
    b = float4(0, 0, 0, 0);
  float2 at = lerp(b.xy, b.zw, kCorner[id % 6u]);
  FxOut o;
  o.pos = float4(at / gTarget * float2(2, -2) + float2(-1, 1), 0, 1);
  o.head = p.head;
  o.at = p.at;
  o.axes = p.axes;
  o.ring = p.ring;
  o.c0 = p.c0;
  o.c1 = p.c1;
  o.c2 = p.c2;
  o.clip = p.clip;
  return o;
}

// A pixel in a circle's own units: 1 at its radius along either axis.
float2 Local(FxOut i, float2 q)
{
  float2 u = i.axes.xy, v = i.axes.zw, d = q - i.at.xy;
  float det = u.x * v.y - v.x * u.y;
  return float2(v.y * d.x - v.x * d.y, -u.y * d.x + u.x * d.y) / det;
}

// A pixel's distance to a segment; past an end without a round cap, none of it is drawn, so the
// lines of a chain meet without doubling where they join.
float SegmentDistance(float2 q, float2 a, float2 b, int caps)
{
  float2 ab = b - a;
  float t = dot(q - a, ab) / max(dot(ab, ab), 1e-6);
  if ((t < 0.0 && (caps & 1) == 0) || (t > 1.0 && (caps & 2) == 0))
    return 1e6;
  return length(q - a - ab * saturate(t));
}

// One colour laid over another, both premultiplied.
float4 Over(float4 top, float4 under)
{
  return top + under * (1.0 - top.a);
}

float4 Pre(float3 rgb, float a)
{
  return float4(rgb * a, a);
}

// How much of a band w wide covers a pixel d from its middle: a soft edge a pixel wide, or in the
// game's pixels all or nothing, never thinner than least.
float Band(float w, float d, bool px, float least)
{
  if (!px)
    return saturate(w * 0.5 - d + 0.5);
  return d <= max(w, least) * 0.5 ? 1.0 : 0.0;
}

float4 PSFx(FxOut i) : SV_Target
{
  float2 q = i.pos.xy;
  if (q.x < i.clip.x || q.y < i.clip.y || q.x > i.clip.z || q.y > i.clip.w)
    discard;
  if (gMaskOn > 0.5) {
    int2 g = int2(floor((q - gBox.xy) / gBox.zw));
    if (all(g >= 0) && all(g < int2(gGame)) && gMask.Load(int3(g, 0)).r > 0.5)
      discard;
  }
  // in the game's pixels, each cell drawn as its middle is
  bool px = gPixel.x > 0.5;
  float2 cell = gBox.zw * max(gPixel.y, 0.25);
  float steps = max(gPixel.z, 1.0);
  float bayer = 0.5;
  if (px) {
    float2 k = floor((q - gBox.xy) / cell);
    q = gBox.xy + (k + 0.5) * cell;
    uint2 b = uint2(int2(k) + int2(gPixelAt.xy)) & 3u;
    bayer = (BAYER[b.x + b.y * 4u] + 0.5) / 16.0;
  }
  int kind = (int)i.head.x;
  bool bAdd = i.head.y < 0.5;
  float4 o = float4(0, 0, 0, 0);
  if (kind == KIND_GLOW) {
    float d = length(Local(i, q));
    // in bands, their edges dithered
    if (px)
      d = floor(d * steps * 2.0 + bayer) / (steps * 2.0);
    if (d >= 1.0)
      discard;
    // canvas's radial gradient as the studio draws it: hot, then 0.62, then 0.22, then nothing
    float4 c;
    if (d < 0.22)
      c = lerp(i.c0, i.c1, d / 0.22);
    else if (d < 0.55)
      c = lerp(i.c1, i.c2, (d - 0.22) / 0.33);
    else
      c = lerp(i.c2, float4(i.c2.rgb, 0), (d - 0.55) / 0.45);
    o = Pre(c.rgb, c.a);
  } else if (kind == KIND_DOT || kind == KIND_RING) {
    float2 l = Local(i, q);
    float d = length(l);
    float aa = px ? 1e-4 : max(fwidth(d), 1e-4);
    float cover = saturate((1.0 - d) / aa + 0.5);
    if (i.ring.x > 0.0)
      cover *= saturate((d - i.ring.x) / aa + 0.5);
    float a0 = i.ring.y, a1 = i.ring.z;
    float ang = atan2(l.y, l.x) - i.ring.w;
    if (a1 > a0 + 1e-4 || i.head.z > 0.0) {
      float span = a1 > a0 + 1e-4 ? a1 - a0 : 6.2831853;
      float x = ang - a0;
      x -= floor(x / 6.2831853) * 6.2831853;
      if (x > span)
        discard;
      if (i.head.z > 0.0 && frac(x / span * i.head.z) > 0.5)
        discard;
    }
    o = Pre(i.c0.rgb, i.c0.a * cover);
  } else if (kind == KIND_LINE) {
    float d = SegmentDistance(q, i.at.xy, i.at.zw, (int)i.head.w);
    float edge = i.axes.z > 0.0 ? Band(i.axes.z, d, px, 0.0) * i.c2.a : 0.0;
    float body = Band(i.axes.x, d, px, cell.x) * i.c1.a;
    float core = i.axes.y > 0.0 ? Band(i.axes.y, d, px, cell.x) * i.c0.a : 0.0;
    if (bAdd)
      o = Pre(i.c2.rgb, edge) + Pre(i.c1.rgb, body) + Pre(i.c0.rgb, core);
    else
      o = Over(Pre(i.c0.rgb, core), Over(Pre(i.c1.rgb, body), Pre(i.c2.rgb, edge)));
  } else if (kind == KIND_STAR) {
    float2 d = abs(q - i.at.xy);
    float r = max(i.axes.x, 1.0);
    float across = (px ? (d.y < cell.y * 0.5 ? 1.0 : 0.0) : saturate(1.1 - d.y)) * saturate(1.0 - d.x / r);
    float down = (px ? (d.x < cell.x * 0.5 ? 1.0 : 0.0) : saturate(1.1 - d.x)) * saturate(1.0 - d.y / r);
    o = Pre(i.c0.rgb, i.c0.a * max(across, down));
  } else if (kind == KIND_TRI) {
    float2 a = i.at.xy, b = i.at.zw, c = i.axes.xy;
    float s = sign((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
    float2 e0 = b - a, e1 = c - b, e2 = a - c;
    float d0 = s * (e0.x * (q.y - a.y) - e0.y * (q.x - a.x)) / max(length(e0), 1e-4);
    float d1 = s * (e1.x * (q.y - b.y) - e1.y * (q.x - b.x)) / max(length(e1), 1e-4);
    float d2 = s * (e2.x * (q.y - c.y) - e2.y * (q.x - c.x)) / max(length(e2), 1e-4);
    float cover = px ? (min(d0, min(d1, d2)) >= 0.0 ? 1.0 : 0.0) : saturate(min(d0, min(d1, d2)) + 0.5);
    o = Pre(i.c0.rgb, i.c0.a * cover);
  } else {
    o = Pre(i.c0.rgb, i.c0.a);
  }
  if (px) {
    // light and cover in steps, the dither between them; then few levels of each colour
    if (bAdd) {
      float m = max(o.r, max(o.g, o.b));
      o.rgb *= m > 0.0 ? (floor(m * steps + bayer) / steps) / m : 0.0;
    } else if (o.a > 0.0) {
      o *= saturate(floor(o.a * steps + bayer) / steps) / o.a;
    }
    if (gPixel.w >= 2.0) {
      float levels = gPixel.w - 1.0;
      float a = max(o.a, 1e-4);
      float3 rgb = saturate(bAdd ? o.rgb : o.rgb / a);
      rgb = min(floor(rgb * levels + bayer), levels) / levels;
      o.rgb = bAdd ? rgb : rgb * o.a;
    }
  }
  if (o.a <= 0.0 && dot(o.rgb, float3(1, 1, 1)) <= 0.0)
    discard;
  return o;
}
