// design_draw.cpp -- what the design engine draws over the game's frame in the window (port_gpu.cpp,
// GpuFrameWindow), before the HUD: the units' paths (design_paths.cpp) and the effects (design_fx.cpp).
// Both make DesignPrims in the target's pixels, from the map's through the camera; one storage
// buffer holds the frame's list, and shaders/fx.hlsl draws it, one draw for each run of it in
// one blend.
#include <math.h>
#include <string.h>
#include <vector>
#include "../port/render/port_mask.h"  // PortMaskHasPixel
#include "../port/render/port_gpu_internal.h"  // g_device, GpuShader, GpuTransfer
#include "../port/render/port_teamcolor.h"
#include "../port/render/port_view.h"  // PortViewCamera, PortViewScreenToMap, g_portRender, g_portView
#include "design_engine.h"
#include "design_hooks.h"
#include "PSFx.dxil.h"
#include "PSFx.spv.h"
#include "VSFx.dxil.h"
#include "VSFx.spv.h"

namespace {

SDL_GPUShader *g_pVertex;
SDL_GPUShader *g_pFragment;
SDL_GPUGraphicsPipeline *g_apPipeline[2];  // added, over
int g_nPipelineFormat = -1;
bool g_bFailed;
SDL_GPUBuffer *g_pPrims;
SDL_GPUTransferBuffer *g_pUp;
Uint32 g_nCapacity;  // prims the buffers hold
std::vector<DesignPrim> g_prims;
// the classic console's mask at the game's pixels, made again when the console or the view changes
SDL_GPUTexture *g_pMask;
const void *g_pMaskFrom;
int g_nMaskLeft, g_nMaskTop, g_nMaskW, g_nMaskH;

struct FxTarget {
  float afSize[2];
  float afUnused[2];
};

struct FxMask {
  float afBox[4];
  float afGame[2];
  float fOn;
  float fUnused;
  float afPixel[4];    // the effects in the game's pixels: on, a cell, steps, colours
  float afPixelAt[4];  // the dither's offset in cells
};

SDL_GPUGraphicsPipeline *Pipeline(SDL_GPUTextureFormat format, bool bAdd) {
  SDL_GPUColorTargetDescription target;
  SDL_GPUGraphicsPipelineCreateInfo info;
  SDL_zero(target);
  SDL_zero(info);
  target.format = format;
  target.blend_state.enable_blend = true;
  target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_color_blendfactor = bAdd ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  // light adds to the picture and leaves its alpha; a shape laid over covers it
  target.blend_state.src_alpha_blendfactor = bAdd ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_alpha_blendfactor = bAdd ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  info.vertex_shader = g_pVertex;
  info.fragment_shader = g_pFragment;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  return SDL_CreateGPUGraphicsPipeline(g_device, &info);
}

// The shaders once, and the two pipelines for the target's format.
bool Ready(SDL_GPUTextureFormat format) {
  if (g_bFailed || g_device == NULL)
    return false;
  if (g_pVertex == NULL) {
    g_pVertex = GpuShader(GPU_CODE(VSFx), "VSFx", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1);
    g_pFragment = GpuShader(GPU_CODE(PSFx), "PSFx", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 1);
    if (g_pVertex == NULL || g_pFragment == NULL) {
      g_bFailed = true;
      return false;
    }
  }
  if (g_nPipelineFormat != static_cast<int>(format)) {
    for (SDL_GPUGraphicsPipeline *&p : g_apPipeline)
      if (p != NULL) {
        SDL_ReleaseGPUGraphicsPipeline(g_device, p);
        p = NULL;
      }
    g_apPipeline[0] = Pipeline(format, true);
    g_apPipeline[1] = Pipeline(format, false);
    if (g_apPipeline[0] == NULL || g_apPipeline[1] == NULL) {
      g_bFailed = true;
      return false;
    }
    g_nPipelineFormat = static_cast<int>(format);
  }
  return true;
}

// The list into the storage buffer, before the pass.
bool Upload(SDL_GPUCommandBuffer *pCmd) {
  const Uint32 n = static_cast<Uint32>(g_prims.size());
  if (n > g_nCapacity) {
    if (g_pPrims != NULL)
      SDL_ReleaseGPUBuffer(g_device, g_pPrims);
    if (g_pUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, g_pUp);
    g_nCapacity = n < 4096 ? 4096 : n + n / 2;
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = g_nCapacity * static_cast<Uint32>(sizeof(DesignPrim));
    g_pPrims = SDL_CreateGPUBuffer(g_device, &info);
    g_pUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, info.size);
    if (g_pPrims == NULL || g_pUp == NULL) {
      g_nCapacity = 0;
      return false;
    }
  }
  void *pMapped = SDL_MapGPUTransferBuffer(g_device, g_pUp, true);
  if (pMapped == NULL)
    return false;
  memcpy(pMapped, g_prims.data(), n * sizeof(DesignPrim));
  SDL_UnmapGPUTransferBuffer(g_device, g_pUp);
  SDL_GPUTransferBufferLocation from;
  SDL_GPUBufferRegion to;
  SDL_zero(from);
  SDL_zero(to);
  from.transfer_buffer = g_pUp;
  to.buffer = g_pPrims;
  to.size = n * static_cast<Uint32>(sizeof(DesignPrim));
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_UploadToGPUBuffer(pCopy, &from, &to, true);
  SDL_EndGPUCopyPass(pCopy);
  return true;
}

// The console's mask as a texture of the game's size, on the copy pass before the drawing; false
// when no classic console is drawn.
bool MaskReady(SDL_GPUCommandBuffer *pCmd, int nGameW, int nGameH) {
  int nLeft = 0, nTop = 0;
  const struct _HSTRANS *pMask = DesignHudHidden() ? NULL : DesignConsoleMask(&nLeft, &nTop);
  if (pMask == NULL)
    return false;
  if (g_pMask != NULL && pMask == g_pMaskFrom && nLeft == g_nMaskLeft && nTop == g_nMaskTop &&
      nGameW == g_nMaskW && nGameH == g_nMaskH)
    return true;
  if (g_pMask != NULL)
    SDL_ReleaseGPUTexture(g_device, g_pMask);
  g_pMaskFrom = NULL;
  g_pMask = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, nGameW, nGameH);
  const Uint32 nBytes = static_cast<Uint32>(nGameW) * static_cast<Uint32>(nGameH);
  SDL_GPUTransferBuffer *pUp = GpuTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, nBytes);
  unsigned char *pBytes = pUp != NULL ? static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(g_device, pUp, false)) : NULL;
  if (g_pMask == NULL || pBytes == NULL) {
    if (pUp != NULL)
      SDL_ReleaseGPUTransferBuffer(g_device, pUp);
    return false;
  }
  for (int y = 0; y < nGameH; ++y)
    for (int x = 0; x < nGameW; ++x)
      pBytes[y * nGameW + x] = PortMaskHasPixel(pMask, x - nLeft, y - nTop) ? 255 : 0;
  SDL_UnmapGPUTransferBuffer(g_device, pUp);
  SDL_GPUTextureTransferInfo from;
  SDL_GPUTextureRegion to;
  SDL_zero(from);
  SDL_zero(to);
  from.transfer_buffer = pUp;
  from.pixels_per_row = static_cast<Uint32>(nGameW);
  from.rows_per_layer = static_cast<Uint32>(nGameH);
  to.texture = g_pMask;
  to.w = static_cast<Uint32>(nGameW);
  to.h = static_cast<Uint32>(nGameH);
  to.d = 1;
  SDL_GPUCopyPass *pCopy = SDL_BeginGPUCopyPass(pCmd);
  SDL_UploadToGPUTexture(pCopy, &from, &to, false);
  SDL_EndGPUCopyPass(pCopy);
  SDL_ReleaseGPUTransferBuffer(g_device, pUp);  // released once the card has used it
  g_pMaskFrom = pMask;
  g_nMaskLeft = nLeft;
  g_nMaskTop = nTop;
  g_nMaskW = nGameW;
  g_nMaskH = nGameH;
  return true;
}

// A texture to bind when there is no mask, which the shader then does not read.
SDL_GPUTexture *NoMask(void) {
  static SDL_GPUTexture *s_pNone;
  if (s_pNone == NULL)
    s_pNone = GpuTexture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1);
  return s_pNone;
}

}  // namespace

void DesignPrimClear(DesignPrim *p, int nKind, bool bAdd, const float *pafClip) {
  memset(p, 0, sizeof *p);
  p->head[0] = static_cast<float>(nKind);
  p->head[1] = bAdd ? 0.0f : 1.0f;
  for (int i = 0; i < 4; ++i)
    p->clip[i] = pafClip[i];
}

bool DesignCameraNow(const SDL_FRect &box, int nGameW, int nGameH, DesignCamera *pCam) {
  if (!DesignInGame() || nGameW <= 0 || nGameH <= 0)
    return false;
  if (g_portRender.bOn) {
    // zoomed: a game pixel is the middle of its square of the map
    PortViewCamera(&pCam->fCamX, &pCam->fCamY, &pCam->fZoom);
    pCam->fHalf = 0.5;
  } else {
    int nX = 0, nY = 0;
    PortViewScreenToMap(0, 0, &nX, &nY);
    pCam->fCamX = nX;
    pCam->fCamY = nY;
    pCam->fZoom = 1.0;
    pCam->fHalf = 0.0;
  }
  if (pCam->fZoom <= 0.0)
    pCam->fZoom = 1.0;
  pCam->fX = box.x;
  pCam->fY = box.y;
  pCam->fSx = box.w / nGameW;
  pCam->fSy = box.h / nGameH;
  pCam->fPerMap = static_cast<float>(pCam->fSx / pCam->fZoom);
  // the map's rows; the classic console over them is left out by its mask (fx.hlsl), and a HUD
  // design is drawn over these
  const int nRows = g_portView.nMapRows > 0 && g_portView.nMapRows < nGameH ? g_portView.nMapRows : nGameH;
  pCam->afMap[0] = box.x;
  pCam->afMap[1] = box.y;
  pCam->afMap[2] = box.x + box.w;
  pCam->afMap[3] = box.y + nRows * pCam->fSy;
  return true;
}

void DesignPlayerRgb(int nPlayer, float *pafRgb) {
  // classic's red, blue, teal, purple, orange, brown, white and yellow
  static const unsigned char kClassic[8][3] = {{244, 4, 4},   {12, 72, 204},  {44, 180, 148}, {136, 64, 156},
                                               {248, 140, 20}, {112, 48, 20},  {204, 224, 208}, {252, 252, 56}};
  unsigned char rgb[3] = {200, 200, 200};
  if (!PortTeamColorGet(nPlayer, rgb) && nPlayer >= 0 && nPlayer < 8)
    memcpy(rgb, kClassic[nPlayer], 3);
  for (int i = 0; i < 3; ++i)
    pafRgb[i] = rgb[i] / 255.0f;
}

void DesignWorldDraw(SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, int nWidth, int nHeight,
                     const SDL_FRect &box, int nGameW, int nGameH, int nFormat) {
  g_prims.clear();
  DesignCamera cam;
  if (!DesignCameraNow(box, nGameW, nGameH, &cam))
    return;
  DesignPathsBuild(cam, &g_prims);
  const size_t nPaths = g_prims.size();  // the effects after the paths, which stay smooth
  DesignFxBuild(cam, &g_prims);
  if (g_prims.empty() || !Ready(static_cast<SDL_GPUTextureFormat>(nFormat)) || !Upload(pCmd))
    return;
  const bool bMask = MaskReady(pCmd, nGameW, nGameH);
  SDL_GPUTexture *pMaskTex = bMask ? g_pMask : NoMask();
  if (pMaskTex == NULL)
    return;
  SDL_GPUColorTargetInfo target;
  SDL_zero(target);
  target.texture = pTarget;
  target.load_op = SDL_GPU_LOADOP_LOAD;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *pPass = SDL_BeginGPURenderPass(pCmd, &target, 1, NULL);
  SDL_BindGPUVertexStorageBuffers(pPass, 0, &g_pPrims, 1);
  FxTarget tp;
  tp.afSize[0] = static_cast<float>(nWidth);
  tp.afSize[1] = static_cast<float>(nHeight);
  tp.afUnused[0] = tp.afUnused[1] = 0.0f;
  SDL_PushGPUVertexUniformData(pCmd, 0, &tp, sizeof tp);
  FxMask mask;
  mask.afBox[0] = box.x;
  mask.afBox[1] = box.y;
  mask.afBox[2] = box.w / nGameW;
  mask.afBox[3] = box.h / nGameH;
  mask.afGame[0] = static_cast<float>(nGameW);
  mask.afGame[1] = static_cast<float>(nGameH);
  mask.fOn = bMask ? 1.0f : 0.0f;
  mask.fUnused = 0.0f;
  const DesignPixel &pixel = *DesignFxPixel();
  const float fCell = DesignPixelCell(pixel.fSize);
  mask.afPixel[0] = 0.0f;
  mask.afPixel[1] = fCell;
  mask.afPixel[2] = pixel.fSteps;
  mask.afPixel[3] = pixel.fColours;
  // the dither's cells counted from the map's corner, so it stays on the map as it scrolls
  for (int k = 0; k < 2; ++k) {
    const double fCells = floor((k == 0 ? cam.fCamX : cam.fCamY) / cam.fZoom / fCell);
    mask.afPixelAt[k] = static_cast<float>(fCells - 4.0 * floor(fCells / 4.0));
  }
  mask.afPixelAt[2] = mask.afPixelAt[3] = 0.0f;
  const SDL_GPUTextureSamplerBinding maskBinding = {pMaskTex, g_nearest};
  SDL_BindGPUFragmentSamplers(pPass, 0, &maskBinding, 1);
  // in order, a draw for each run in one blend, the paths' apart from the effects'
  size_t nFirst = 0;
  while (nFirst < g_prims.size()) {
    const bool bOver = g_prims[nFirst].head[1] > 0.5f;
    size_t nEnd = nFirst + 1;
    while (nEnd < g_prims.size() && nEnd != nPaths && (g_prims[nEnd].head[1] > 0.5f) == bOver)
      ++nEnd;
    mask.afPixel[0] = nFirst >= nPaths && pixel.fOn != 0.0f ? 1.0f : 0.0f;
    SDL_PushGPUFragmentUniformData(pCmd, 0, &mask, sizeof mask);
    SDL_BindGPUGraphicsPipeline(pPass, g_apPipeline[bOver ? 1 : 0]);
    SDL_DrawGPUPrimitives(pPass, static_cast<Uint32>(nEnd - nFirst) * 6u, 1, static_cast<Uint32>(nFirst) * 6u, 0);
    nFirst = nEnd;
  }
  SDL_EndGPURenderPass(pPass);
}
