#include "debug_overlay.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Nuklear — full implementation in this translation unit
// ---------------------------------------------------------------------------
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"

// ---------------------------------------------------------------------------

#define OVERLAY_W 1280
#define OVERLAY_H 720
#define OVERLAY_BPP 4  // RGBA

struct DebugOverlay {
  SDL_GPUDevice         *pDevice;
  SDL_Window            *pWindow;
  bool                   bVisible;

  struct nk_context      nk;
  struct nk_font_atlas   atlas;
  int                    iAtlasW;
  int                    iAtlasH;
  uint8_t               *pAtlasPixels; // owned RGBA copy

  uint8_t               *pPixels;      // OVERLAY_W * OVERLAY_H * OVERLAY_BPP
  SDL_GPUTexture        *pTexture;
  SDL_GPUTransferBuffer *pTransfer;
};

// ---------------------------------------------------------------------------
// Software rasterizer helpers
// ---------------------------------------------------------------------------

static void FillRect(uint8_t *pBuf, int iBx, int iBy, int iBw, int iBh,
                     struct nk_color c) {
  for (int iY = iBy; iY < iBy + iBh; iY++) {
    if (iY < 0 || iY >= OVERLAY_H) continue;
    for (int iX = iBx; iX < iBx + iBw; iX++) {
      if (iX < 0 || iX >= OVERLAY_W) continue;
      uint8_t *pP = pBuf + (iY * OVERLAY_W + iX) * OVERLAY_BPP;
      int iA = c.a;
      pP[0] = (uint8_t)((c.r * iA + pP[0] * (255 - iA)) / 255);
      pP[1] = (uint8_t)((c.g * iA + pP[1] * (255 - iA)) / 255);
      pP[2] = (uint8_t)((c.b * iA + pP[2] * (255 - iA)) / 255);
      pP[3] = (uint8_t)(iA + pP[3] * (255 - iA) / 255);
    }
  }
}

static void FillTriangle(uint8_t *pBuf,
                          int iX0, int iY0, int iX1, int iY1, int iX2, int iY2,
                          struct nk_color c) {
  int iMinX = iX0 < iX1 ? (iX0 < iX2 ? iX0 : iX2) : (iX1 < iX2 ? iX1 : iX2);
  int iMinY = iY0 < iY1 ? (iY0 < iY2 ? iY0 : iY2) : (iY1 < iY2 ? iY1 : iY2);
  int iMaxX = iX0 > iX1 ? (iX0 > iX2 ? iX0 : iX2) : (iX1 > iX2 ? iX1 : iX2);
  int iMaxY = iY0 > iY1 ? (iY0 > iY2 ? iY0 : iY2) : (iY1 > iY2 ? iY1 : iY2);
  for (int iY = iMinY; iY <= iMaxY; iY++) {
    if (iY < 0 || iY >= OVERLAY_H) continue;
    for (int iX = iMinX; iX <= iMaxX; iX++) {
      if (iX < 0 || iX >= OVERLAY_W) continue;
      int iD0 = (iX1-iX0)*(iY-iY0) - (iY1-iY0)*(iX-iX0);
      int iD1 = (iX2-iX1)*(iY-iY1) - (iY2-iY1)*(iX-iX1);
      int iD2 = (iX0-iX2)*(iY-iY2) - (iY0-iY2)*(iX-iX2);
      if (!((iD0>=0&&iD1>=0&&iD2>=0)||(iD0<=0&&iD1<=0&&iD2<=0))) continue;
      uint8_t *pP = pBuf + (iY * OVERLAY_W + iX) * OVERLAY_BPP;
      int iA = c.a;
      pP[0] = (uint8_t)((c.r * iA + pP[0] * (255 - iA)) / 255);
      pP[1] = (uint8_t)((c.g * iA + pP[1] * (255 - iA)) / 255);
      pP[2] = (uint8_t)((c.b * iA + pP[2] * (255 - iA)) / 255);
      pP[3] = (uint8_t)(iA + pP[3] * (255 - iA) / 255);
    }
  }
}

static void StrokeLine(uint8_t *pBuf, int iX0, int iY0, int iX1, int iY1,
                        struct nk_color c) {
  int iDx = abs(iX1-iX0), iDy = abs(iY1-iY0);
  int iSx = iX0 < iX1 ? 1 : -1, iSy = iY0 < iY1 ? 1 : -1;
  int iErr = iDx - iDy;
  for (;;) {
    if (iX0 >= 0 && iX0 < OVERLAY_W && iY0 >= 0 && iY0 < OVERLAY_H) {
      uint8_t *pP = pBuf + (iY0 * OVERLAY_W + iX0) * OVERLAY_BPP;
      int iA = c.a;
      pP[0] = (uint8_t)((c.r * iA + pP[0] * (255 - iA)) / 255);
      pP[1] = (uint8_t)((c.g * iA + pP[1] * (255 - iA)) / 255);
      pP[2] = (uint8_t)((c.b * iA + pP[2] * (255 - iA)) / 255);
      pP[3] = (uint8_t)(iA + pP[3] * (255 - iA) / 255);
    }
    if (iX0 == iX1 && iY0 == iY1) break;
    int iE2 = 2 * iErr;
    if (iE2 > -iDy) { iErr -= iDy; iX0 += iSx; }
    if (iE2 <  iDx) { iErr += iDx; iY0 += iSy; }
  }
}

static void DrawGlyph(DebugOverlay *pOverlay, const struct nk_font_glyph *pGlyph,
                       int iCx, int iCy, struct nk_color fg) {
  int iGw = (int)(pGlyph->x1 - pGlyph->x0);
  int iGh = (int)(pGlyph->y1 - pGlyph->y0);
  for (int iPy = 0; iPy < iGh; iPy++) {
    for (int iPx = 0; iPx < iGw; iPx++) {
      float fU = pGlyph->u0 + (pGlyph->u1 - pGlyph->u0) * ((float)iPx / (float)iGw);
      float fV = pGlyph->v0 + (pGlyph->v1 - pGlyph->v0) * ((float)iPy / (float)iGh);
      int iAx = (int)(fU * (float)pOverlay->iAtlasW);
      int iAy = (int)(fV * (float)pOverlay->iAtlasH);
      if (iAx < 0 || iAx >= pOverlay->iAtlasW || iAy < 0 || iAy >= pOverlay->iAtlasH) continue;
      int iAlpha = pOverlay->pAtlasPixels[(iAy * pOverlay->iAtlasW + iAx) * 4 + 3];
      iAlpha = iAlpha * fg.a / 255;
      int iDx = iCx + (int)pGlyph->x0 + iPx;
      int iDy = iCy + (int)pGlyph->y0 + iPy;
      if (iDx < 0 || iDx >= OVERLAY_W || iDy < 0 || iDy >= OVERLAY_H) continue;
      uint8_t *pDst = pOverlay->pPixels + (iDy * OVERLAY_W + iDx) * OVERLAY_BPP;
      pDst[0] = (uint8_t)((fg.r * iAlpha + pDst[0] * (255 - iAlpha)) / 255);
      pDst[1] = (uint8_t)((fg.g * iAlpha + pDst[1] * (255 - iAlpha)) / 255);
      pDst[2] = (uint8_t)((fg.b * iAlpha + pDst[2] * (255 - iAlpha)) / 255);
      pDst[3] = (uint8_t)(iAlpha + pDst[3] * (255 - iAlpha) / 255);
    }
  }
}

static void RenderCommands(DebugOverlay *pOverlay) {
  const struct nk_command *pCmd;
  nk_foreach(pCmd, &pOverlay->nk) {
    switch (pCmd->type) {
    case NK_COMMAND_NOP:
    case NK_COMMAND_SCISSOR:
      break;
    case NK_COMMAND_LINE: {
      const struct nk_command_line *pL = (const struct nk_command_line *)pCmd;
      StrokeLine(pOverlay->pPixels, pL->begin.x, pL->begin.y,
                 pL->end.x, pL->end.y, pL->color);
    } break;
    case NK_COMMAND_RECT_FILLED: {
      const struct nk_command_rect_filled *pR =
        (const struct nk_command_rect_filled *)pCmd;
      FillRect(pOverlay->pPixels, pR->x, pR->y, pR->w, pR->h, pR->color);
    } break;
    case NK_COMMAND_TRIANGLE_FILLED: {
      const struct nk_command_triangle_filled *pT =
        (const struct nk_command_triangle_filled *)pCmd;
      FillTriangle(pOverlay->pPixels,
        pT->a.x, pT->a.y, pT->b.x, pT->b.y, pT->c.x, pT->c.y, pT->color);
    } break;
    case NK_COMMAND_TEXT: {
      const struct nk_command_text *pT = (const struct nk_command_text *)pCmd;
      struct nk_font *pFont = (struct nk_font *)pT->font->userdata.ptr;
      float fCx = (float)pT->x;
      for (int iI = 0; iI < pT->length; iI++) {
        nk_rune uiRune = (nk_rune)(unsigned char)pT->string[iI];
        const struct nk_font_glyph *pGlyph = nk_font_find_glyph(pFont, uiRune);
        if (!pGlyph) { fCx += pT->font->height * 0.5f; continue; }
        DrawGlyph(pOverlay, pGlyph, (int)fCx, pT->y, pT->foreground);
        fCx += pGlyph->xadvance;
      }
    } break;
    default: break;
    }
  }
  nk_clear(&pOverlay->nk);
}

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------

static void UploadAndBlit(DebugOverlay *pOverlay, SDL_GPUCommandBuffer *pCmdBuf) {
  void *pMapped = SDL_MapGPUTransferBuffer(pOverlay->pDevice, pOverlay->pTransfer, true);
  if (!pMapped) return;
  memcpy(pMapped, pOverlay->pPixels, OVERLAY_W * OVERLAY_H * OVERLAY_BPP);
  SDL_UnmapGPUTransferBuffer(pOverlay->pDevice, pOverlay->pTransfer);

  SDL_GPUCopyPass *pCp = SDL_BeginGPUCopyPass(pCmdBuf);
  SDL_GPUTextureTransferInfo src = { .transfer_buffer = pOverlay->pTransfer };
  SDL_GPUTextureRegion dst = {
    .texture = pOverlay->pTexture, .w = OVERLAY_W, .h = OVERLAY_H, .d = 1
  };
  SDL_UploadToGPUTexture(pCp, &src, &dst, false);
  SDL_EndGPUCopyPass(pCp);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

DebugOverlay *debug_overlay_create(SDL_GPUDevice *pDevice, SDL_Window *pWindow) {
  DebugOverlay *pOverlay = calloc(1, sizeof(DebugOverlay));
  if (!pOverlay) return NULL;
  pOverlay->pDevice  = pDevice;
  pOverlay->pWindow  = pWindow;
  pOverlay->bVisible = false;

  nk_font_atlas_init_default(&pOverlay->atlas);
  nk_font_atlas_begin(&pOverlay->atlas);
  struct nk_font *pFont = nk_font_atlas_add_default(&pOverlay->atlas, 13, NULL);

  const void *pBaked = nk_font_atlas_bake(&pOverlay->atlas,
                                           &pOverlay->iAtlasW, &pOverlay->iAtlasH,
                                           NK_FONT_ATLAS_RGBA32);
  size_t uiAtlasBytes = (size_t)(pOverlay->iAtlasW * pOverlay->iAtlasH * 4);
  pOverlay->pAtlasPixels = malloc(uiAtlasBytes);
  memcpy(pOverlay->pAtlasPixels, pBaked, uiAtlasBytes);

  struct nk_draw_null_texture nullTex = {0};
  nk_font_atlas_end(&pOverlay->atlas, nk_handle_ptr(pOverlay->pAtlasPixels), &nullTex);
  nk_init_default(&pOverlay->nk, &pFont->handle);

  pOverlay->pPixels = calloc(1, OVERLAY_W * OVERLAY_H * OVERLAY_BPP);

  SDL_GPUTextureCreateInfo ti = {0};
  ti.type   = SDL_GPU_TEXTURETYPE_2D;
  ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  ti.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  ti.width  = OVERLAY_W;
  ti.height = OVERLAY_H;
  ti.layer_count_or_depth = 1;
  ti.num_levels = 1;
  pOverlay->pTexture = SDL_CreateGPUTexture(pDevice, &ti);

  SDL_GPUTransferBufferCreateInfo tbi = {0};
  tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tbi.size  = OVERLAY_W * OVERLAY_H * OVERLAY_BPP;
  pOverlay->pTransfer = SDL_CreateGPUTransferBuffer(pDevice, &tbi);

  return pOverlay;
}

void debug_overlay_destroy(DebugOverlay *pOverlay) {
  if (!pOverlay) return;
  nk_free(&pOverlay->nk);
  nk_font_atlas_clear(&pOverlay->atlas);
  free(pOverlay->pAtlasPixels);
  free(pOverlay->pPixels);
  SDL_ReleaseGPUTexture(pOverlay->pDevice, pOverlay->pTexture);
  SDL_ReleaseGPUTransferBuffer(pOverlay->pDevice, pOverlay->pTransfer);
  free(pOverlay);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool debug_overlay_is_visible(DebugOverlay *pOverlay) {
  return pOverlay && pOverlay->bVisible;
}

void debug_overlay_toggle(DebugOverlay *pOverlay) {
  if (pOverlay) pOverlay->bVisible = !pOverlay->bVisible;
}

void debug_overlay_handle_event(DebugOverlay *pOverlay, SDL_Event *pEvent) {
  if (!pOverlay) return;
  if (pEvent->type == SDL_EVENT_KEY_DOWN &&
      pEvent->key.scancode == SDL_SCANCODE_F1) {
    pOverlay->bVisible = !pOverlay->bVisible;
    return;
  }
  if (!pOverlay->bVisible) return;
  struct nk_context *pCtx = &pOverlay->nk;
  if (pEvent->type == SDL_EVENT_MOUSE_MOTION)
    nk_input_motion(pCtx, (int)pEvent->motion.x, (int)pEvent->motion.y);
  else if (pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           pEvent->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    int iDown = (pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    if (pEvent->button.button == SDL_BUTTON_LEFT)
      nk_input_button(pCtx, NK_BUTTON_LEFT,
                      (int)pEvent->button.x, (int)pEvent->button.y, iDown);
  }
}

void debug_overlay_render(DebugOverlay *pOverlay,
                          SDL_GPUCommandBuffer *pCmdBuf,
                          SDL_GPUTexture *pSwapchainTex,
                          Uint32 uiSwapchainW, Uint32 uiSwapchainH) {
  if (!pOverlay || !pOverlay->bVisible) return;

  struct nk_context *pCtx = &pOverlay->nk;
  nk_input_begin(pCtx);
  nk_input_end(pCtx);

  if (nk_begin(pCtx, "Debug", nk_rect(10, 10, 220, 80),
               NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE)) {
    nk_layout_row_dynamic(pCtx, 20, 1);
    nk_label(pCtx, "Debug overlay (F1 to hide)", NK_TEXT_LEFT);
  }
  nk_end(pCtx);

  memset(pOverlay->pPixels, 0, OVERLAY_W * OVERLAY_H * OVERLAY_BPP);
  RenderCommands(pOverlay);
  UploadAndBlit(pOverlay, pCmdBuf);

  SDL_GPUBlitInfo blit = {0};
  blit.source.texture      = pOverlay->pTexture;
  blit.source.w            = OVERLAY_W;
  blit.source.h            = OVERLAY_H;
  blit.destination.texture = pSwapchainTex;
  blit.destination.w       = uiSwapchainW;
  blit.destination.h       = uiSwapchainH;
  blit.filter  = SDL_GPU_FILTER_LINEAR;
  blit.load_op = SDL_GPU_LOADOP_LOAD;
  SDL_BlitGPUTexture(pCmdBuf, &blit);
}
