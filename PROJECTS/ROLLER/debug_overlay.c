#include "debug_overlay.h"
#include "debug_overlay_shaders.h"
#include "crt_filter.h"
#include "frontend.h"
#include "roller.h"
#include "rollerinput.h"
#include "menu_render.h"
#include "game_render_hw.h"
#include "3d.h"
#include "sound.h"
#include "view.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Nuklear — full implementation in this translation unit
// ---------------------------------------------------------------------------
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_ZERO_COMMAND_MEMORY
#define NK_IMPLEMENTATION
#include "nuklear.h"

// ---------------------------------------------------------------------------

#define OVERLAY_W        1280
#define OVERLAY_H        800
#define OVERLAY_BPP      4    // RGBA
#define OVERLAY_ASPECT   ((float)OVERLAY_W / (float)OVERLAY_H)

#define MAX_LOG_MESSAGES 512
#define MAX_LOG_LEN      256

#define OVERLAY_FONT_SIZE 24.0f
#define PANEL_MARGIN     10
#define DEBUG_ROW_H      30
#define COMBO_ITEM_H     (DEBUG_ROW_H + 4)
#define COMBO_W          190
#define DEBUG_SPACING_H  12
#define HINT_H           42
#define PANEL_Y          (PANEL_MARGIN + HINT_H + PANEL_MARGIN)
#define PANEL_H          (OVERLAY_H - PANEL_Y - PANEL_MARGIN)
#define LEFT_W           410
#define RIGHT_X          (PANEL_MARGIN + LEFT_W + PANEL_MARGIN)
#define RIGHT_W          (OVERLAY_W - RIGHT_X - PANEL_MARGIN)
#define LOG_ROW_H        DEBUG_ROW_H
#define TOUCH_GESTURE_THRESHOLD 24.0f
#define TOUCH_SCROLL_PIXELS_PER_STEP 72.0f

typedef struct {
  char szText[MAX_LOG_LEN];
} tLogEntry;

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
  SDL_GPUGraphicsPipeline *pPipeline;
  SDL_GPUSampler          *pSampler;

  // Log ring buffer
  tLogEntry              aLogEntries[MAX_LOG_MESSAGES];
  int                    iLogHead;     // index of oldest entry
  int                    iLogCount;
  SDL_Mutex             *pLogMutex;

  // Chained SDL log function
  SDL_LogOutputFunction  pPrevLogFn;
  void                  *pPrevLogUserdata;

  uint32_t               uLastCmdHash; // FNV-1a of Nuklear command buffer; 0 = force rasterize

  bool                   bInputBegun;
  bool                   bTouchActive;
  SDL_FingerID           ullTouchFingerId;
  bool                   bTouchScrolling;
  bool                   bTouchDragging;
  bool                   bTouchButtonDown;
  float                  fTouchStartX;
  float                  fTouchStartY;
  float                  fTouchLastX;
  float                  fTouchLastY;
  bool                   bTouchTapReleasePending;
  int                    iTouchTapReleaseDelay;
  int                    iTouchTapReleaseX;
  int                    iTouchTapReleaseY;
  bool                   bDismissActive;
  bool                   bDismissTouch;
  SDL_FingerID           ullDismissFingerId;
  SDL_MouseID            uiDismissMouseId;
  Uint8                  byDismissMouseButton;
  bool                   bHideLog;

};

// ---------------------------------------------------------------------------
// Surface debug label storage
// ---------------------------------------------------------------------------

static SurfaceDebugLabel s_surfLabels[SURFACE_LABEL_MAX];
static int               s_surfLabelCount;

/* Grid deduplication: one label per cell, O(1) per push. */
#define LABEL_CELL_W 240
#define LABEL_CELL_H  32
#define LABEL_GRID_COLS (OVERLAY_W  / LABEL_CELL_W)
#define LABEL_GRID_ROWS (OVERLAY_H  / LABEL_CELL_H)
static bool s_labelGrid[LABEL_GRID_ROWS][LABEL_GRID_COLS];

void debug_overlay_surface_labels_reset(void) {
  s_surfLabelCount = 0;
  memset(s_labelGrid, 0, sizeof(s_labelGrid));
}

void debug_overlay_surface_label_push(float nx, float ny, const char *text) {
  if (s_surfLabelCount >= SURFACE_LABEL_MAX) return;
  int col = (int)(nx * OVERLAY_W) / LABEL_CELL_W;
  int row = (int)(ny * OVERLAY_H) / LABEL_CELL_H;
  if (col < 0 || col >= LABEL_GRID_COLS || row < 0 || row >= LABEL_GRID_ROWS) return;
  if (s_labelGrid[row][col]) return;
  s_labelGrid[row][col] = true;
  SurfaceDebugLabel *p = &s_surfLabels[s_surfLabelCount++];
  p->nx = nx; p->ny = ny;
  snprintf(p->text, sizeof(p->text), "%s", text);
}

// ---------------------------------------------------------------------------
// Pick outline: highlights the last surface picked via click-to-pick
// ---------------------------------------------------------------------------

static struct { bool active; float nx[4], ny[4]; bool valid[4]; } s_pickOutline;
/* True if the outline was active as of the last debug_overlay_render() call.
 * Needed so the lazy-rasterize cache below forces one extra redraw on the
 * exact frame the outline goes inactive -- otherwise, if nothing else in the
 * overlay changed that frame, the Nuklear command hash matches the previous
 * frame's and the cached pixel buffer (which still has the outline baked
 * into it) gets reused forever instead of being redrawn without it. */
static bool s_pickOutlineWasActive;

void debug_overlay_pick_outline_set(bool active, const float nx[4], const float ny[4], const bool valid[4]) {
  s_pickOutline.active = active;
  if (active) {
    for (int i = 0; i < 4; i++) {
      s_pickOutline.nx[i]    = nx[i];
      s_pickOutline.ny[i]    = ny[i];
      s_pickOutline.valid[i] = valid[i];
    }
  }
}

// ---------------------------------------------------------------------------
// Log callback
// ---------------------------------------------------------------------------

static const char *PriorityPrefix(SDL_LogPriority priority) {
  switch (priority) {
  case SDL_LOG_PRIORITY_VERBOSE:  return "[VRB] ";
  case SDL_LOG_PRIORITY_DEBUG:    return "[DBG] ";
  case SDL_LOG_PRIORITY_INFO:     return "[INF] ";
  case SDL_LOG_PRIORITY_WARN:     return "[WRN] ";
  case SDL_LOG_PRIORITY_ERROR:    return "[ERR] ";
  case SDL_LOG_PRIORITY_CRITICAL: return "[CRT] ";
  default:                        return "      ";
  }
}

static void LogCallback(void *pUserdata, int iCategory, SDL_LogPriority priority,
                         const char *pMessage) {
  DebugOverlay *pOverlay = (DebugOverlay *)pUserdata;

  // Chain to previous handler so console output is preserved
  if (pOverlay->pPrevLogFn)
    pOverlay->pPrevLogFn(pOverlay->pPrevLogUserdata, iCategory, priority, pMessage);

  SDL_LockMutex(pOverlay->pLogMutex);

  int iIdx;
  if (pOverlay->iLogCount < MAX_LOG_MESSAGES) {
    iIdx = (pOverlay->iLogHead + pOverlay->iLogCount) % MAX_LOG_MESSAGES;
    pOverlay->iLogCount++;
  } else {
    iIdx = pOverlay->iLogHead;
    pOverlay->iLogHead = (pOverlay->iLogHead + 1) % MAX_LOG_MESSAGES;
  }

  snprintf(pOverlay->aLogEntries[iIdx].szText, MAX_LOG_LEN,
           "%s%s", PriorityPrefix(priority), pMessage);

  SDL_UnlockMutex(pOverlay->pLogMutex);
}

// ---------------------------------------------------------------------------
// Software rasterizer helpers
// ---------------------------------------------------------------------------

static void OverlayFillRect(uint8_t *pBuf, int iBx, int iBy, int iBw, int iBh,
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

static void OverlayFillTriangle(uint8_t *pBuf,
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

static void OverlayStrokeLine(uint8_t *pBuf, int iX0, int iY0, int iX1, int iY1,
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

static void OverlayDrawGlyph(DebugOverlay *pOverlay, const struct nk_font_glyph *pGlyph,
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

static void OverlayDrawString(DebugOverlay *pOverlay, int x, int y,
                              const char *text, struct nk_color fg) {
  struct nk_font *pFont = (struct nk_font *)pOverlay->nk.style.font->userdata.ptr;
  float fCx = (float)x;
  for (; *text; text++) {
    const struct nk_font_glyph *pG = nk_font_find_glyph(pFont, (nk_rune)(unsigned char)*text);
    if (!pG) { fCx += pOverlay->nk.style.font->height * 0.5f; continue; }
    OverlayDrawGlyph(pOverlay, pG, (int)fCx, y, fg);
    fCx += pG->xadvance;
  }
}

static void DrawSurfaceLabels(DebugOverlay *pOverlay) {
  struct nk_color bg  = nk_rgba(0, 0, 0, 176);
  struct nk_color fg  = nk_rgba(255, 255, 100, 255);
  float fh = pOverlay->nk.style.font->height;
  for (int i = 0; i < s_surfLabelCount; i++) {
    const SurfaceDebugLabel *p = &s_surfLabels[i];
    int ox = (int)(p->nx * OVERLAY_W);
    int oy = (int)(p->ny * OVERLAY_H) - (int)(fh * 0.5f);
    int len = (int)strlen(p->text);
    int tw  = (int)(len * fh * 0.52f);  /* approximate text width */
    OverlayFillRect(pOverlay->pPixels, ox - 1, oy - 1, tw + 4, (int)fh + 2, bg);
    OverlayDrawString(pOverlay, ox + 1, oy, p->text, fg);
  }
}

static void DrawSurfaceOutline(DebugOverlay *pOverlay) {
  if (!s_pickOutline.active) return;
  struct nk_color col = nk_rgba(255, 40, 40, 255);
  int px[4], py[4];
  for (int i = 0; i < 4; i++) {
    px[i] = (int)(s_pickOutline.nx[i] * OVERLAY_W);
    py[i] = (int)(s_pickOutline.ny[i] * OVERLAY_H);
  }
  /* Quad perimeter is 0-1-2-3-0; skip an edge if either endpoint was behind
   * the camera (that vertex has no valid screen position). */
  static const int edges[4][2] = { {0,1}, {1,2}, {2,3}, {3,0} };
  for (int e = 0; e < 4; e++) {
    int a = edges[e][0], b = edges[e][1];
    if (!s_pickOutline.valid[a] || !s_pickOutline.valid[b]) continue;
    OverlayStrokeLine(pOverlay->pPixels, px[a], py[a], px[b], py[b], col);
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
      OverlayStrokeLine(pOverlay->pPixels, pL->begin.x, pL->begin.y,
                 pL->end.x, pL->end.y, pL->color);
    } break;
    case NK_COMMAND_RECT_FILLED: {
      const struct nk_command_rect_filled *pR =
        (const struct nk_command_rect_filled *)pCmd;
      OverlayFillRect(pOverlay->pPixels, pR->x, pR->y, pR->w, pR->h, pR->color);
    } break;
    case NK_COMMAND_TRIANGLE_FILLED: {
      const struct nk_command_triangle_filled *pT =
        (const struct nk_command_triangle_filled *)pCmd;
      OverlayFillTriangle(pOverlay->pPixels,
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
        OverlayDrawGlyph(pOverlay, pGlyph, (int)fCx, pT->y, pT->foreground);
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

static SDL_GPUShader *LoadOverlayShader(SDL_GPUDevice *pDevice, SDL_GPUShaderStage stage,
    const unsigned char *pSpirv, unsigned int uiSpirvSize,
    const unsigned char *pMsl, unsigned int uiMslSize,
    int iNumSamplers, int iNumUniformBuffers) {
  SDL_GPUShaderFormat fmts = SDL_GetGPUShaderFormats(pDevice);
  SDL_GPUShaderCreateInfo info = {0};
  info.stage = stage;
  info.num_samplers = iNumSamplers;
  info.num_uniform_buffers = iNumUniformBuffers;
  if (fmts & SDL_GPU_SHADERFORMAT_SPIRV) {
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.code = pSpirv;
    info.code_size = uiSpirvSize;
    info.entrypoint = "main";
  } else if (fmts & SDL_GPU_SHADERFORMAT_MSL) {
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.code = pMsl;
    info.code_size = uiMslSize;
    info.entrypoint = "main0";
  } else {
    return NULL;
  }
  return SDL_CreateGPUShader(pDevice, &info);
}

DebugOverlay *debug_overlay_create(SDL_GPUDevice *pDevice, SDL_Window *pWindow) {
  DebugOverlay *pOverlay = calloc(1, sizeof(DebugOverlay));
  if (!pOverlay) return NULL;
  pOverlay->pDevice     = pDevice;
  pOverlay->pWindow     = pWindow;
  pOverlay->bVisible    = false;

  nk_font_atlas_init_default(&pOverlay->atlas);
  nk_font_atlas_begin(&pOverlay->atlas);
  struct nk_font *pFont = nk_font_atlas_add_default(&pOverlay->atlas,
                                                    OVERLAY_FONT_SIZE, NULL);

  const void *pBaked = nk_font_atlas_bake(&pOverlay->atlas,
                                           &pOverlay->iAtlasW, &pOverlay->iAtlasH,
                                           NK_FONT_ATLAS_RGBA32);
  size_t uiAtlasBytes = (size_t)(pOverlay->iAtlasW * pOverlay->iAtlasH * 4);
  pOverlay->pAtlasPixels = malloc(uiAtlasBytes);
  memcpy(pOverlay->pAtlasPixels, pBaked, uiAtlasBytes);

  struct nk_draw_null_texture nullTex = {0};
  nk_font_atlas_end(&pOverlay->atlas, nk_handle_ptr(pOverlay->pAtlasPixels), &nullTex);
  nk_init_default(&pOverlay->nk, &pFont->handle);

  // Make window and group backgrounds translucent (~80% opacity)
  struct nk_style *pStyle = &pOverlay->nk.style;
  pStyle->window.fixed_background      = nk_style_item_color(nk_rgba(45,  45,  45,  200));
  pStyle->window.background            = nk_rgba(45,  45,  45,  200);
  pStyle->window.header.normal         = nk_style_item_color(nk_rgba(40,  40,  40,  200));
  pStyle->window.header.hover          = nk_style_item_color(nk_rgba(40,  40,  40,  200));
  pStyle->window.header.active         = nk_style_item_color(nk_rgba(40,  40,  40,  200));
  pStyle->window.group_border_color    = nk_rgba(60,  60,  60,  200);
  pStyle->window.border_color          = nk_rgba(60,  60,  60,  200);
  pStyle->checkbox.cursor_normal       = nk_style_item_color(nk_rgba(220, 220, 220, 255));
  pStyle->checkbox.cursor_hover        = nk_style_item_color(nk_rgba(255, 255, 255, 255));
  pStyle->combo.content_padding        = nk_vec2(4, 0);
  pStyle->combo.border                 = 0;
#if defined(IS_ANDROID)
  pStyle->window.scrollbar_size        = nk_vec2(24, 24);
#endif

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

  pOverlay->pLogMutex = SDL_CreateMutex();
  SDL_GetLogOutputFunction(&pOverlay->pPrevLogFn, &pOverlay->pPrevLogUserdata);
  SDL_SetLogOutputFunction(LogCallback, pOverlay);

  // Build alpha-blend pipeline for compositing overlay over swapchain
  SDL_GPUShader *pVert = LoadOverlayShader(pDevice, SDL_GPU_SHADERSTAGE_VERTEX,
    overlay_vertex_spirv, overlay_vertex_spirv_size,
    overlay_vertex_msl,  overlay_vertex_msl_size,
    0, 0);
  SDL_GPUShader *pFrag = LoadOverlayShader(pDevice, SDL_GPU_SHADERSTAGE_FRAGMENT,
    overlay_pixel_spirv, overlay_pixel_spirv_size,
    overlay_pixel_msl,  overlay_pixel_msl_size,
    1, 0);
  if (pVert && pFrag) {
    SDL_GPUGraphicsPipelineCreateInfo pipeInfo = {0};
    pipeInfo.vertex_shader   = pVert;
    pipeInfo.fragment_shader = pFrag;
    pipeInfo.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    SDL_GPUColorTargetDescription ct = {0};
    ct.format = SDL_GetGPUSwapchainTextureFormat(pDevice, pWindow);
    ct.blend_state.enable_blend              = true;
    ct.blend_state.src_color_blendfactor     = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ct.blend_state.dst_color_blendfactor     = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op            = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor     = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor     = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op            = SDL_GPU_BLENDOP_ADD;
    pipeInfo.target_info.color_target_descriptions = &ct;
    pipeInfo.target_info.num_color_targets         = 1;

    pOverlay->pPipeline = SDL_CreateGPUGraphicsPipeline(pDevice, &pipeInfo);
    if (!pOverlay->pPipeline)
      SDL_Log("debug_overlay: failed to create pipeline: %s", SDL_GetError());
  } else {
    SDL_Log("debug_overlay: failed to load overlay shaders");
  }
  if (pVert) SDL_ReleaseGPUShader(pDevice, pVert);
  if (pFrag) SDL_ReleaseGPUShader(pDevice, pFrag);

  SDL_GPUSamplerCreateInfo si = {0};
  si.min_filter     = SDL_GPU_FILTER_LINEAR;
  si.mag_filter     = SDL_GPU_FILTER_LINEAR;
  si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  pOverlay->pSampler = SDL_CreateGPUSampler(pDevice, &si);

  return pOverlay;
}

void debug_overlay_destroy(DebugOverlay *pOverlay) {
  if (!pOverlay) return;
  SDL_SetLogOutputFunction(pOverlay->pPrevLogFn, pOverlay->pPrevLogUserdata);
  SDL_DestroyMutex(pOverlay->pLogMutex);
  nk_free(&pOverlay->nk);
  nk_font_atlas_clear(&pOverlay->atlas);
  free(pOverlay->pAtlasPixels);
  free(pOverlay->pPixels);
  SDL_ReleaseGPUTexture(pOverlay->pDevice, pOverlay->pTexture);
  SDL_ReleaseGPUTransferBuffer(pOverlay->pDevice, pOverlay->pTransfer);
  if (pOverlay->pPipeline)
    SDL_ReleaseGPUGraphicsPipeline(pOverlay->pDevice, pOverlay->pPipeline);
  if (pOverlay->pSampler)
    SDL_ReleaseGPUSampler(pOverlay->pDevice, pOverlay->pSampler);
  free(pOverlay);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void debug_overlay_set_visible(DebugOverlay *pOverlay, bool bVisible) {
  if (!pOverlay) return;
  if (pOverlay->bVisible == bVisible) return;

  pOverlay->bVisible = bVisible;
  pOverlay->bDismissActive = false;
  if (!bVisible && pOverlay->bInputBegun) {
    nk_input_end(&pOverlay->nk);
    pOverlay->bInputBegun = false;
  }
#if defined(IS_ANDROID)
  SDL_StopTextInput(pOverlay->pWindow);
#else
  if (bVisible) {
    SDL_StartTextInput(pOverlay->pWindow);
  } else {
    SDL_StopTextInput(pOverlay->pWindow);
  }
#endif
  pOverlay->bTouchActive = false;
  pOverlay->bTouchScrolling = false;
  pOverlay->bTouchDragging = false;
  pOverlay->bTouchButtonDown = false;
  pOverlay->bTouchTapReleasePending = false;
  noclip_camera_set_input_enabled(!bVisible);
}

bool debug_overlay_visible(DebugOverlay *pOverlay) {
  return pOverlay && pOverlay->bVisible;
}

void debug_overlay_toggle(DebugOverlay *pOverlay) {
  if (!pOverlay) return;
  debug_overlay_set_visible(pOverlay, !pOverlay->bVisible);
}

static bool DebugOverlayInputEventPoint(DebugOverlay *pOverlay,
                                        SDL_Event *pEvent,
                                        int *piX, int *piY)
{
  int iWinW = 0;
  int iWinH = 0;
  SDL_GPUViewport viewport = {0};
  float fWindowX = 0.0f;
  float fWindowY = 0.0f;
  float fOverlayX;
  float fOverlayY;

  if (!pOverlay || !pEvent || !piX || !piY)
    return false;

  if (!SDL_GetWindowSizeInPixels(pOverlay->pWindow, &iWinW, &iWinH) ||
      iWinW <= 0 || iWinH <= 0)
    return false;

  ROLLERGetPresentViewport((Uint32)iWinW, (Uint32)iWinH, OVERLAY_ASPECT,
                           &viewport);

  switch (pEvent->type) {
    case SDL_EVENT_MOUSE_MOTION:
      fWindowX = pEvent->motion.x;
      fWindowY = pEvent->motion.y;
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      fWindowX = pEvent->button.x;
      fWindowY = pEvent->button.y;
      break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
      fWindowX = pEvent->tfinger.x * (float)iWinW;
      fWindowY = pEvent->tfinger.y * (float)iWinH;
      break;
    default:
      return false;
  }

  if (viewport.w <= 0.0f || viewport.h <= 0.0f)
    return false;

  fOverlayX = (fWindowX - viewport.x) * (float)OVERLAY_W / viewport.w;
  fOverlayY = (fWindowY - viewport.y) * (float)OVERLAY_H / viewport.h;
  if (fOverlayX < 0.0f || fOverlayY < 0.0f ||
      fOverlayX >= (float)OVERLAY_W ||
      fOverlayY >= (float)OVERLAY_H)
    return false;

  *piX = (int)fOverlayX;
  *piY = (int)fOverlayY;
  return true;
}

static bool DebugOverlayConsumesEvent(SDL_Event *pEvent)
{
  switch (pEvent->type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
      return true;
    default:
      return false;
  }
}

static bool DebugOverlayIsTouchMouseEvent(SDL_Event *pEvent)
{
  if (pEvent->type == SDL_EVENT_MOUSE_MOTION)
    return pEvent->motion.which == SDL_TOUCH_MOUSEID;
  if (pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      pEvent->type == SDL_EVENT_MOUSE_BUTTON_UP)
    return pEvent->button.which == SDL_TOUCH_MOUSEID;
  if (pEvent->type == SDL_EVENT_MOUSE_WHEEL)
    return pEvent->wheel.which == SDL_TOUCH_MOUSEID;

  return false;
}

static bool DebugOverlayHandlePendingDismiss(DebugOverlay *pOverlay,
                                             SDL_Event *pEvent)
{
  if (!pOverlay->bDismissActive)
    return false;

  if (pOverlay->bDismissTouch) {
    if (DebugOverlayIsTouchMouseEvent(pEvent))
      return true;

    if ((pEvent->type == SDL_EVENT_FINGER_MOTION ||
         pEvent->type == SDL_EVENT_FINGER_UP ||
         pEvent->type == SDL_EVENT_FINGER_CANCELED) &&
        pEvent->tfinger.fingerID == pOverlay->ullDismissFingerId) {
      if (pEvent->type == SDL_EVENT_FINGER_UP ||
          pEvent->type == SDL_EVENT_FINGER_CANCELED) {
        pOverlay->bDismissActive = false;
        frontend_mouse_cancel_click();
        debug_overlay_set_visible(pOverlay, false);
      }
      return true;
    }

    return false;
  }

  if (pEvent->type == SDL_EVENT_MOUSE_MOTION &&
      pEvent->motion.which == pOverlay->uiDismissMouseId)
    return true;

  if ((pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       pEvent->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
      pEvent->button.which == pOverlay->uiDismissMouseId) {
    if (pEvent->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        pEvent->button.button == pOverlay->byDismissMouseButton) {
      pOverlay->bDismissActive = false;
      frontend_mouse_cancel_click();
      debug_overlay_set_visible(pOverlay, false);
    }
    return true;
  }

  return false;
}

static float DebugOverlayAbsFloat(float fValue)
{
  return fValue < 0.0f ? -fValue : fValue;
}

static void DebugOverlayBeginInput(DebugOverlay *pOverlay)
{
  if (!pOverlay->bInputBegun) {
    nk_input_begin(&pOverlay->nk);
    pOverlay->bInputBegun = true;
  }
}

static void DebugOverlayReleaseTouchButton(DebugOverlay *pOverlay, int iX, int iY)
{
  DebugOverlayBeginInput(pOverlay);
  nk_input_motion(&pOverlay->nk, iX, iY);
  nk_input_button(&pOverlay->nk, NK_BUTTON_LEFT, iX, iY, nk_false);
  pOverlay->bTouchButtonDown = false;
}

static void DebugOverlayHandlePendingTapRelease(DebugOverlay *pOverlay)
{
  if (!pOverlay || !pOverlay->bTouchTapReleasePending)
    return;

  if (pOverlay->iTouchTapReleaseDelay > 0) {
    --pOverlay->iTouchTapReleaseDelay;
    return;
  }

  DebugOverlayReleaseTouchButton(pOverlay,
                                 pOverlay->iTouchTapReleaseX,
                                 pOverlay->iTouchTapReleaseY);
  pOverlay->bTouchTapReleasePending = false;
}

bool debug_overlay_handle_event(DebugOverlay *pOverlay, SDL_Event *pEvent) {
  int iOverlayX = 0;
  int iOverlayY = 0;
  bool bHasPoint;

  if (!pOverlay || !pEvent)
    return false;

  if (!pOverlay->bVisible)
    return false;

  if (!DebugOverlayConsumesEvent(pEvent))
    return false;

  if (DebugOverlayHandlePendingDismiss(pOverlay, pEvent))
    return true;

  bHasPoint = DebugOverlayInputEventPoint(pOverlay, pEvent,
                                          &iOverlayX, &iOverlayY);
  if (DebugOverlayIsTouchMouseEvent(pEvent))
    return true;

  if ((pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       pEvent->type == SDL_EVENT_FINGER_DOWN) &&
      bHasPoint && iOverlayY < PANEL_Y) {
    frontend_mouse_cancel_click();
    pOverlay->bDismissActive = true;
    pOverlay->bDismissTouch = pEvent->type == SDL_EVENT_FINGER_DOWN;
    if (pOverlay->bDismissTouch) {
      pOverlay->ullDismissFingerId = pEvent->tfinger.fingerID;
    } else {
      pOverlay->uiDismissMouseId = pEvent->button.which;
      pOverlay->byDismissMouseButton = pEvent->button.button;
    }
    return true;
  }

  // Open input bracket on first event of the frame
  DebugOverlayBeginInput(pOverlay);

  struct nk_context *pCtx = &pOverlay->nk;
  if (pEvent->type == SDL_EVENT_MOUSE_MOTION && bHasPoint) {
    nk_input_motion(pCtx, iOverlayX, iOverlayY);
  } else if ((pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
              pEvent->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
             bHasPoint) {
    int iDown = (pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    if (pEvent->button.button == SDL_BUTTON_LEFT)
      nk_input_button(pCtx, NK_BUTTON_LEFT, iOverlayX, iOverlayY, iDown);
  } else if (pEvent->type == SDL_EVENT_MOUSE_WHEEL) {
    nk_input_scroll(pCtx, nk_vec2(pEvent->wheel.x, pEvent->wheel.y));
  } else if (pEvent->type == SDL_EVENT_FINGER_DOWN && bHasPoint) {
    if (!pOverlay->bTouchActive) {
      pOverlay->bTouchActive = true;
      pOverlay->ullTouchFingerId = pEvent->tfinger.fingerID;
      pOverlay->bTouchScrolling = false;
      pOverlay->bTouchDragging = false;
      pOverlay->bTouchButtonDown = false;
      pOverlay->fTouchStartX = (float)iOverlayX;
      pOverlay->fTouchStartY = (float)iOverlayY;
      pOverlay->fTouchLastX = (float)iOverlayX;
      pOverlay->fTouchLastY = (float)iOverlayY;
      nk_input_motion(pCtx, iOverlayX, iOverlayY);
    }
  } else if (pEvent->type == SDL_EVENT_FINGER_MOTION && bHasPoint) {
    if (pOverlay->bTouchActive &&
        pOverlay->ullTouchFingerId == pEvent->tfinger.fingerID) {
      float fDeltaX = (float)iOverlayX - pOverlay->fTouchStartX;
      float fDeltaY = (float)iOverlayY - pOverlay->fTouchStartY;
      float fAbsDeltaX = DebugOverlayAbsFloat(fDeltaX);
      float fAbsDeltaY = DebugOverlayAbsFloat(fDeltaY);

      if (!pOverlay->bTouchScrolling && !pOverlay->bTouchDragging &&
          (fAbsDeltaX > TOUCH_GESTURE_THRESHOLD ||
           fAbsDeltaY > TOUCH_GESTURE_THRESHOLD)) {
        if (fAbsDeltaY > fAbsDeltaX) {
          pOverlay->bTouchScrolling = true;
        } else {
          pOverlay->bTouchDragging = true;
          nk_input_motion(pCtx, (int)pOverlay->fTouchStartX,
                          (int)pOverlay->fTouchStartY);
          nk_input_button(pCtx, NK_BUTTON_LEFT,
                          (int)pOverlay->fTouchStartX,
                          (int)pOverlay->fTouchStartY, nk_true);
          pOverlay->bTouchButtonDown = true;
        }
      }

      nk_input_motion(pCtx, iOverlayX, iOverlayY);
      if (pOverlay->bTouchScrolling) {
        float fFrameDeltaY = (float)iOverlayY - pOverlay->fTouchLastY;
        nk_input_scroll(pCtx,
                        nk_vec2(0.0f,
                                fFrameDeltaY / TOUCH_SCROLL_PIXELS_PER_STEP));
      }
      pOverlay->fTouchLastX = (float)iOverlayX;
      pOverlay->fTouchLastY = (float)iOverlayY;
    }
  } else if ((pEvent->type == SDL_EVENT_FINGER_UP ||
              pEvent->type == SDL_EVENT_FINGER_CANCELED)) {
    if (pOverlay->bTouchActive &&
        pOverlay->ullTouchFingerId == pEvent->tfinger.fingerID) {
      if (bHasPoint) {
        float fDeltaX = (float)iOverlayX - pOverlay->fTouchStartX;
        float fDeltaY = (float)iOverlayY - pOverlay->fTouchStartY;

        nk_input_motion(pCtx, iOverlayX, iOverlayY);
        if (pOverlay->bTouchButtonDown) {
          nk_input_button(pCtx, NK_BUTTON_LEFT, iOverlayX, iOverlayY,
                          nk_false);
          pOverlay->bTouchButtonDown = false;
        } else if (pEvent->type == SDL_EVENT_FINGER_UP &&
                   !pOverlay->bTouchScrolling &&
                   DebugOverlayAbsFloat(fDeltaX) <= TOUCH_GESTURE_THRESHOLD &&
                   DebugOverlayAbsFloat(fDeltaY) <= TOUCH_GESTURE_THRESHOLD) {
          nk_input_button(pCtx, NK_BUTTON_LEFT, iOverlayX, iOverlayY, nk_true);
          pOverlay->bTouchButtonDown = true;
          pOverlay->bTouchTapReleasePending = true;
          pOverlay->iTouchTapReleaseDelay = 1;
          pOverlay->iTouchTapReleaseX = iOverlayX;
          pOverlay->iTouchTapReleaseY = iOverlayY;
        }
      } else if (pOverlay->bTouchButtonDown) {
        DebugOverlayReleaseTouchButton(pOverlay,
                                       (int)pOverlay->fTouchLastX,
                                       (int)pOverlay->fTouchLastY);
      }
      pOverlay->bTouchActive = false;
      pOverlay->bTouchScrolling = false;
      pOverlay->bTouchDragging = false;
    }
  } else if (pEvent->type == SDL_EVENT_KEY_DOWN || pEvent->type == SDL_EVENT_KEY_UP) {
    nk_bool bDown = (pEvent->type == SDL_EVENT_KEY_DOWN) ? nk_true : nk_false;
    SDL_Keymod mod = SDL_GetModState();
    switch (pEvent->key.key) {
    case SDLK_BACKSPACE: nk_input_key(pCtx, NK_KEY_BACKSPACE, bDown); break;
    case SDLK_DELETE:    nk_input_key(pCtx, NK_KEY_DEL, bDown); break;
    case SDLK_RETURN:
    case SDLK_RETURN2:   nk_input_key(pCtx, NK_KEY_ENTER, bDown); break;
    case SDLK_LEFT:
      nk_input_key(pCtx, (mod & SDL_KMOD_CTRL) ? NK_KEY_TEXT_WORD_LEFT  : NK_KEY_LEFT,  bDown);
      break;
    case SDLK_RIGHT:
      nk_input_key(pCtx, (mod & SDL_KMOD_CTRL) ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, bDown);
      break;
    case SDLK_HOME: nk_input_key(pCtx, NK_KEY_TEXT_LINE_START, bDown); break;
    case SDLK_END:  nk_input_key(pCtx, NK_KEY_TEXT_LINE_END,   bDown); break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: nk_input_key(pCtx, NK_KEY_SHIFT, bDown); break;
    case SDLK_A:
      if (mod & SDL_KMOD_CTRL) nk_input_key(pCtx, NK_KEY_TEXT_SELECT_ALL, bDown);
      break;
    default: break;
    }
  } else if (pEvent->type == SDL_EVENT_TEXT_INPUT) {
    for (const char *pCh = pEvent->text.text; *pCh; pCh++)
      nk_input_char(pCtx, *pCh);
  }

  return true;
}

// ---------------------------------------------------------------------------
// UI panels
// ---------------------------------------------------------------------------

static void DrawHintPanel(DebugOverlay *pOverlay) {
  struct nk_context *pCtx = &pOverlay->nk;
  if (nk_begin(pCtx, "Toggle Hint",
               nk_rect(PANEL_MARGIN, PANEL_MARGIN,
                       OVERLAY_W - PANEL_MARGIN * 2, HINT_H),
               NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    nk_label(pCtx, "Debug menu: press ` to toggle", NK_TEXT_LEFT);
  }
  nk_end(pCtx);
}

// ---------------------------------------------------------------------------

static void DrawDebugPanel(DebugOverlay *pOverlay) {
  struct nk_context *pCtx = &pOverlay->nk;
  if (nk_begin(pCtx, "Settings",
               nk_rect(PANEL_MARGIN, PANEL_Y, LEFT_W, PANEL_H),
               NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
#ifdef __ANDROID__
    static const char *apszMusic[] = { "MIDI", "MIDI (OPL3)", "CD" };
    int iMusicSel = (MusicCD != 0) ? 2 : (MusicOPL != 0) ? 1 : 0;
#else
    static const char *apszMusic[] = { "MIDI", "MIDI (OS)", "MIDI (OPL3)", "CD" };
    int iMusicSel = (MusicCD != 0) ? 3 : (MusicOPL != 0) ? 2 : (MusicOS != 0) ? 1 : 0;
#endif
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
    nk_label(pCtx, "Music", NK_TEXT_LEFT);
    int iNewMusicSel = nk_combo(pCtx, apszMusic, NK_LEN(apszMusic), iMusicSel, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
    if (iNewMusicSel != iMusicSel) {
      stopmusic();
      const char *pszSel = apszMusic[iNewMusicSel];
      if (pszSel == "CD")          { MusicCD = -1; MusicCard = 0;  MusicOS = 0;  MusicOPL = 0; }
      else if (pszSel == "MIDI (OPL3)") { MusicCD = 0;  MusicCard = 0;  MusicOS = 0;  MusicOPL = -1; }
      else if (pszSel == "MIDI (OS)")   { MusicCD = 0;  MusicCard = 0;  MusicOS = -1; MusicOPL = 0; }
      else                         { MusicCD = 0;  MusicCard = -1; MusicOS = 0;  MusicOPL = 0; }
      startmusic(g_iCurrentSong);
      InputSaveConfig();
    }

    { char buf[16]; snprintf(buf, sizeof(buf), "Draw dist. %.0f%%", g_fDrawDistanceFraction * 100.0f);
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, buf, NK_TEXT_LEFT);
      float fNewDrawDist = nk_slide_float(pCtx, 0.0f, g_fDrawDistanceFraction, 1.0f, 0.01f);
      if (fNewDrawDist != g_fDrawDistanceFraction) {
        g_fDrawDistanceFraction = fNewDrawDist;
        InputSaveConfig();
      }
    }

    int bNoCollisionLimit = (int)g_bNoCollisionLimit;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "No collision limit", &bNoCollisionLimit)) {
      g_bNoCollisionLimit = (bool)bNoCollisionLimit;
      InputSaveConfig();
    }

    int bAirborneCollisions = (int)g_bAirborneCollisions;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "Airborne collisions", &bAirborneCollisions)) {
      g_bAirborneCollisions = (bool)bAirborneCollisions;
      InputSaveConfig();
    }

    int bAINoCheatStart = (int)g_bAINoCheatStart;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "AI automatic gears", &bAINoCheatStart)) {
      g_bAINoCheatStart = (bool)bAINoCheatStart;
      InputSaveConfig();
    }

    int bFixCarMenuBug = (int)g_bFixCarMenuBug;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "Fix car menu bug", &bFixCarMenuBug)) {
      g_bFixCarMenuBug = (bool)bFixCarMenuBug;
      InputSaveConfig();
    }

    int bImprovedJumpLanding = (int)g_bImprovedJumpLanding;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "Improved jump landing", &bImprovedJumpLanding)) {
      g_bImprovedJumpLanding = (bool)bImprovedJumpLanding;
      InputSaveConfig();
    }

    int bNoclip = (int)g_bNoclip;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "Noclip", &bNoclip)) {
      g_bNoclip = (bool)bNoclip;
      noclip_camera_reset();
      noclip_camera_set_input_enabled(!pOverlay->bVisible);
    }

    int bShowCarOnExplosion = (int)g_bShowCarOnExplosion;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "Show car on explosion", &bShowCarOnExplosion)) {
      g_bShowCarOnExplosion = (bool)bShowCarOnExplosion;
      InputSaveConfig();
    }

    int bFreeCamera = (int)g_bFreeCamera;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "Free Camera", &bFreeCamera)) {
      g_bFreeCamera = (bool)bFreeCamera;
    }

#if defined(_WIN32)
    {
      static const char *apszInputBackends[] = { "WinMM", "SDL DirectInput" };
      int iInputBackend = InputGetWindowsBackend() == INPUT_WINDOWS_BACKEND_SDL_DINPUT ? 1 : 0;
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, "Windows input", NK_TEXT_LEFT);
      int iNewInputBackend = nk_combo(pCtx, apszInputBackends, NK_LEN(apszInputBackends), iInputBackend, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
      if (iNewInputBackend != iInputBackend) {
        InputSetWindowsBackend(iNewInputBackend == 1 ? INPUT_WINDOWS_BACKEND_SDL_DINPUT : INPUT_WINDOWS_BACKEND_WINMM);
        InputSaveConfig();
      }
    }
#endif

#if defined(IS_ANDROID)
    {
      static const char *apszPhoneControls[] = { "Disabled", "Tilt turn", "Touch turn" };
      int iSel = (int)g_ePhoneControls;
      if (iSel < 0 || iSel > 2)
        iSel = 0;
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, "Phone controls", NK_TEXT_LEFT);
      int iNewSel = nk_combo(pCtx, apszPhoneControls, NK_LEN(apszPhoneControls), iSel, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
      if (iNewSel != iSel) {
        g_ePhoneControls = (ePhoneControls)iNewSel;
        InputSaveConfig();
      }

      int bShowActiveTouchControls = (int)g_bShowActiveTouchControls;
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      if (nk_checkbox_label(pCtx, "Show active touch controls", &bShowActiveTouchControls)) {
        g_bShowActiveTouchControls = (bool)bShowActiveTouchControls;
        InputSaveConfig();
      }
    }
#endif

    nk_layout_row_dynamic(pCtx, DEBUG_SPACING_H, 1);
    nk_spacing(pCtx, 1);
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    nk_label(pCtx, "Experimental", NK_TEXT_LEFT);

    int bCRTFilter = (int)g_bCRTFilter;
    nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
    if (nk_checkbox_label(pCtx, "CRT filter", &bCRTFilter)) {
      g_bCRTFilter = (bool)bCRTFilter;
      game_render_set_crt_filter(g_pGameRenderer,
                                 g_bCRTFilter ? ROLLERGetCRTFilter() : NULL);
      InputSaveConfig();
    }

    MenuRenderer *pRenderer = GetMenuRenderer();
    if (pRenderer) {
      int bGPU = (menu_render_get_pending_mode(pRenderer) == MENU_RENDER_GPU);
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      if (nk_checkbox_label(pCtx, "Hardware rendering", &bGPU)) {
        menu_render_set_mode(pRenderer, bGPU ? MENU_RENDER_GPU : MENU_RENDER_SOFTWARE);
        game_render_set_mode(g_pGameRenderer, bGPU ? GAME_RENDER_GPU : GAME_RENDER_SOFTWARE);
        InputSaveConfig();
      }

      if (!bGPU) nk_widget_disable_begin(pCtx);

      int bSplit = game_render_is_split_screen(g_pGameRenderer);
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      if (nk_checkbox_label(pCtx, "Split view (SW/HW)", &bSplit)) {
        game_render_set_split_screen(g_pGameRenderer, (bool)bSplit);
      }

      static const char *apszScale[] = { "1x (native)", "1.5x", "2x", "3x" };
      static const float k_scaleVals[] = { 1.0f, 1.5f, 2.0f, 3.0f };
      int iCurScale = 0;
      for (int i = 1; i < 4; i++)
        if (g_fRenderScale >= k_scaleVals[i] - 0.01f) iCurScale = i;
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, "Render scale", NK_TEXT_LEFT);
      int iNewScale = nk_combo(pCtx, apszScale, NK_LEN(apszScale), iCurScale, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
      if (iNewScale != iCurScale) {
        g_fRenderScale = k_scaleVals[iNewScale];
        game_render_set_render_scale(g_pGameRenderer, g_fRenderScale);
        InputSaveConfig();
      }

      static const char *apszAA[] = { "Off", "MSAA 2x", "MSAA 4x", "MSAA 8x" };
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, "Anti-aliasing", NK_TEXT_LEFT);
      int iNewAA = nk_combo(pCtx, apszAA, NK_LEN(apszAA), g_iAntiAliasing, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
      if (iNewAA != g_iAntiAliasing) {
        g_iAntiAliasing = iNewAA;
        game_render_set_antialiasing(g_pGameRenderer, g_iAntiAliasing);
        InputSaveConfig();
      }

      static const char *apszAniso[] = { "2x", "4x", "8x", "16x" };
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, "Anisotropy", NK_TEXT_LEFT);
      int iNewAniso = nk_combo(pCtx, apszAniso, NK_LEN(apszAniso), g_iAnisotropyLevel, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
      if (iNewAniso != g_iAnisotropyLevel) {
        g_iAnisotropyLevel = iNewAniso;
        game_render_set_anisotropy_level(g_pGameRenderer, g_iAnisotropyLevel);
        InputSaveConfig();
      }

      static const char *apszFilter[] = { "Nearest", "Bilinear", "Anisotropic" };
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, "Texture filter", NK_TEXT_LEFT);
      int iNewFilter = nk_combo(pCtx, apszFilter, NK_LEN(apszFilter), g_iTextureFilter, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
      if (iNewFilter != g_iTextureFilter) {
        g_iTextureFilter = iNewFilter;
        game_render_set_texture_filter(g_pGameRenderer, g_iTextureFilter);
        InputSaveConfig();
      }

      if (!bGPU) nk_widget_disable_end(pCtx);

      if (!bGPU || g_bDisableMipmaps) nk_widget_disable_begin(pCtx);
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bTrilinear = (int)g_bTrilinear;
      if (nk_checkbox_label(pCtx, "Trilinear filtering", &bTrilinear)) {
        g_bTrilinear = (bool)bTrilinear;
        game_render_set_trilinear(g_pGameRenderer, g_bTrilinear);
        InputSaveConfig();
      }
      if (!bGPU || g_bDisableMipmaps) nk_widget_disable_end(pCtx);

      if (!bGPU) nk_widget_disable_begin(pCtx);
      
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bDisableMipmaps = (int)g_bDisableMipmaps;
      if (nk_checkbox_label(pCtx, "Disable mipmaps (debug)", &bDisableMipmaps)) {
        g_bDisableMipmaps = (bool)bDisableMipmaps;
        game_render_set_disable_mipmaps(g_pGameRenderer, g_bDisableMipmaps);
      }
      if (!bGPU) nk_widget_disable_end(pCtx);

      if (!bGPU || !g_bTrilinear || g_bDisableMipmaps) nk_widget_disable_begin(pCtx);
      { char buf[20]; snprintf(buf, sizeof(buf), "LOD bias %.1f", g_fLodBias);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewBias = nk_slide_float(pCtx, -4.0f, g_fLodBias, 4.0f, 0.1f);
        if (fNewBias != g_fLodBias) {
          g_fLodBias = fNewBias;
          game_render_set_lod_bias(g_pGameRenderer, g_fLodBias);
          InputSaveConfig();
        }
      }
      if (!bGPU || !g_bTrilinear || g_bDisableMipmaps) nk_widget_disable_end(pCtx);


      if (!bGPU) nk_widget_disable_begin(pCtx);

      { char buf[20]; snprintf(buf, sizeof(buf), "Fog start %.0f", g_fFogStart);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewFogStart = nk_slide_float(pCtx, 0.0f, g_fFogStart, 10000.0f, 50.0f);
        if (fNewFogStart != g_fFogStart) {
          g_fFogStart = fNewFogStart;
          game_render_set_fog_start(g_pGameRenderer, g_fFogStart);
          InputSaveConfig();
        }
      }

      { char buf[24]; snprintf(buf, sizeof(buf), "Fog density %.5f", g_fFogDensity);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewFog = nk_slide_float(pCtx, 0.0f, g_fFogDensity, 0.0001f, 0.000001f);
        if (fNewFog != g_fFogDensity) {
          g_fFogDensity = fNewFog;
          game_render_set_fog_density(g_pGameRenderer, g_fFogDensity);
          InputSaveConfig();
        }
      }

      if (!bGPU) nk_widget_disable_end(pCtx);

      if (!bGPU || frontend_on) nk_widget_disable_begin(pCtx);

      { static char szBuf[8]; static int iLen = 0; static nk_flags lastEv = 0;
        /* Only sync buffer from global when the field was not active last frame,
         * so we never clobber Nuklear's internal edit state mid-edit. */
        if (!(lastEv & NK_EDIT_ACTIVE)) {
          snprintf(szBuf, sizeof(szBuf), "%06x", g_uFogColor);
          iLen = 6;
        }
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, "Fog color", NK_TEXT_LEFT);
        nk_flags ev = nk_edit_string(pCtx,
                                     NK_EDIT_FIELD|NK_EDIT_SIG_ENTER|NK_EDIT_AUTO_SELECT,
                                     szBuf, &iLen, 7, nk_filter_hex);
        if (!bGPU || frontend_on) nk_widget_disable_end(pCtx);
        lastEv = frontend_on ? 0 : ev;
        if (!frontend_on && (ev & (NK_EDIT_DEACTIVATED | NK_EDIT_COMMITED))) {
          szBuf[iLen < 7 ? iLen : 6] = '\0';
          if (iLen == 6) {
            unsigned int uNew = 0;
            if (sscanf(szBuf, "%x", &uNew) == 1) {
              g_uFogColor = uNew & 0xFFFFFFu;
              game_render_set_fog_color(g_pGameRenderer,
                  ((g_uFogColor >> 16) & 0xFF) / 255.0f,
                  ((g_uFogColor >>  8) & 0xFF) / 255.0f,
                  ( g_uFogColor        & 0xFF) / 255.0f);
              InputSaveConfig();
            }
          }
        }
      }

      if (!bGPU) nk_widget_disable_begin(pCtx);

      { char buf[14]; snprintf(buf, sizeof(buf), "FOV %.2f", g_fFovMultiplier);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewFov = nk_slide_float(pCtx, 0.5f, g_fFovMultiplier, 2.0f, 0.05f);
        if (fNewFov != g_fFovMultiplier) {
          g_fFovMultiplier = fNewFov;
          game_render_set_fov_multiplier(g_pGameRenderer, g_fFovMultiplier);
          InputSaveConfig();
        }
      }

      { char buf[18]; snprintf(buf, sizeof(buf), "Vignette %.2f", g_fVigStrength);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewVig = nk_slide_float(pCtx, 0.0f, g_fVigStrength, 2.0f, 0.05f);
        if (fNewVig != g_fVigStrength) {
          g_fVigStrength = fNewVig;
          game_render_set_vignette(g_pGameRenderer, g_fVigStrength);
          InputSaveConfig();
        }
      }

      { char buf[20]; snprintf(buf, sizeof(buf), "Brightness %.2f", g_fBrightness);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewBrightness = nk_slide_float(pCtx, -0.5f, g_fBrightness, 0.5f, 0.02f);
        if (fNewBrightness != g_fBrightness) {
          g_fBrightness = fNewBrightness;
          game_render_set_brightness(g_pGameRenderer, g_fBrightness);
          InputSaveConfig();
        }
      }

      { char buf[18]; snprintf(buf, sizeof(buf), "Contrast %.2f", g_fContrast);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewContrast = nk_slide_float(pCtx, 0.0f, g_fContrast, 3.0f, 0.05f);
        if (fNewContrast != g_fContrast) {
          g_fContrast = fNewContrast;
          game_render_set_contrast(g_pGameRenderer, g_fContrast);
          InputSaveConfig();
        }
      }

      { char buf[16]; snprintf(buf, sizeof(buf), "Gamma %.2f", g_fGamma);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewGamma = nk_slide_float(pCtx, 0.5f, g_fGamma, 2.5f, 0.05f);
        if (fNewGamma != g_fGamma) {
          g_fGamma = fNewGamma;
          game_render_set_gamma(g_pGameRenderer, g_fGamma);
          InputSaveConfig();
        }
      }

      { char buf[20]; snprintf(buf, sizeof(buf), "Saturation %.2f", g_fSaturation);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewSat = nk_slide_float(pCtx, 0.0f, g_fSaturation, 3.0f, 0.05f);
        if (fNewSat != g_fSaturation) {
          g_fSaturation = fNewSat;
          game_render_set_saturation(g_pGameRenderer, g_fSaturation);
          InputSaveConfig();
        }
      }

      { char buf[20]; snprintf(buf, sizeof(buf), "Mirror FOV %.2f", g_fMirrorFov);
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, buf, NK_TEXT_LEFT);
        float fNewMirrorFov = nk_slide_float(pCtx, 0.25f, g_fMirrorFov, 3.0f, 0.05f);
        if (fNewMirrorFov != g_fMirrorFov) {
          g_fMirrorFov = fNewMirrorFov;
          InputSaveConfig();
        }
      }

      static const char *apszFps[] = { "FPS off", "Top left", "Top right", "Bottom left", "Bottom right" };
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
      nk_label(pCtx, "FPS counter", NK_TEXT_LEFT);
      int iNewFps = nk_combo(pCtx, apszFps, NK_LEN(apszFps), g_iFpsDisplay, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
      if (iNewFps != g_iFpsDisplay) {
        g_iFpsDisplay = iNewFps;
        InputSaveConfig();
      }

      {
        static const char *apszBgFps[] = { "Off (default)", "15", "30", "60" };
        static const int aiBgFpsValues[] = { 0, 15, 30, 60 };
        int iBgFpsIdx = 0;
        for (int i = 1; i < 4; i++) {
          if (g_iFpsBackground == aiBgFpsValues[i]) { iBgFpsIdx = i; break; }
        }
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, "FPS Background", NK_TEXT_LEFT);
        int iNewBgFpsIdx = nk_combo(pCtx, apszBgFps, NK_LEN(apszBgFps), iBgFpsIdx, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
        if (iNewBgFpsIdx != iBgFpsIdx) {
          g_iFpsBackground = aiBgFpsValues[iNewBgFpsIdx];
          InputSaveConfig();
        }
      }

      {
        static const char *apszCull[] = { "default", "none", "back", "front" };
        nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 2);
        nk_label(pCtx, "Culling", NK_TEXT_LEFT);
        int iNewCull = nk_combo(pCtx, apszCull, NK_LEN(apszCull), g_iCullMode, COMBO_ITEM_H, nk_vec2(COMBO_W, 9999));
        if (iNewCull != g_iCullMode) {
          g_iCullMode = iNewCull;
          game_render_set_cull_mode(g_pGameRenderer, g_iCullMode);
        }
      }

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      {
        int bSignsOnTop = (int)g_bSignsOnTop;
        if (nk_checkbox_label(pCtx, "Signs on top", &bSignsOnTop))
          g_bSignsOnTop = (bool)bSignsOnTop;
      }

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bWireframe = (int)g_bWireframe;
      if (nk_checkbox_label(pCtx, "Wireframe", &bWireframe)) {
        g_bWireframe = (bool)bWireframe;
        game_render_set_wireframe(g_pGameRenderer, g_bWireframe);
      }

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bPickTextures = (int)g_bPickTextures;
      if (nk_checkbox_label(pCtx, "Pick Textures", &bPickTextures))
        g_bPickTextures = (bool)bPickTextures;

      /* Nuklear can't nest disable regions -- close the outer !bGPU one,
       * apply a combined !bGPU||!g_bPickTextures pair just for this widget,
       * then reopen the outer region for the widgets below. */
      if (!bGPU) nk_widget_disable_end(pCtx);
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      if (!bGPU || !g_bPickTextures) nk_widget_disable_begin(pCtx);
      int bPickTexturesPNG = (int)g_bPickTexturesPNG;
      if (nk_checkbox_label(pCtx, "Pick Textures as PNG", &bPickTexturesPNG))
        g_bPickTexturesPNG = (bool)bPickTexturesPNG;
      if (!bGPU || !g_bPickTextures) nk_widget_disable_end(pCtx);
      if (!bGPU) nk_widget_disable_begin(pCtx);

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bRenderStatsLog = (int)g_bRenderStatsLog;
      if (nk_checkbox_label(pCtx, "Render Stats Log", &bRenderStatsLog))
        g_bRenderStatsLog = (bool)bRenderStatsLog;

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bSurfDbg = (int)g_bSurfaceDebugViz;
      if (nk_checkbox_label(pCtx, "Surface debug labels", &bSurfDbg))
        g_bSurfaceDebugViz = (bool)bSurfDbg;

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bSurfLog = (int)g_bSurfaceLog;
      if (nk_checkbox_label(pCtx, "Surface Log", &bSurfLog))
        g_bSurfaceLog = (bool)bSurfLog;

      {
        static char s_surfLogIdBuf[8] = "";
        static int  s_surfLogIdLen    = 0;
        static const float aCols[]    = { 0.65f, 0.35f };
        nk_layout_row(pCtx, NK_DYNAMIC, DEBUG_ROW_H, 2, aCols);
        nk_label(pCtx, "Surface Log ID", NK_TEXT_LEFT);
        nk_flags lastEv = nk_edit_string(pCtx, NK_EDIT_FIELD | NK_EDIT_AUTO_SELECT,
                                         s_surfLogIdBuf, &s_surfLogIdLen,
                                         (int)sizeof(s_surfLogIdBuf) - 1,
                                         nk_filter_default);
        if (lastEv & NK_EDIT_ACTIVE) {
          if (s_surfLogIdLen == 0) {
            g_iSurfaceLogId = -2;
          } else {
            s_surfLogIdBuf[s_surfLogIdLen] = '\0';
            g_iSurfaceLogId = SDL_atoi(s_surfLogIdBuf);
          }
        }
      }

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bVsync = (int)g_bVsync;
      if (nk_checkbox_label(pCtx, "V-sync", &bVsync)) {
        g_bVsync = (bool)bVsync;
        game_render_set_vsync(g_pGameRenderer, g_bVsync);
        InputSaveConfig();
      }

      if (!bGPU) nk_widget_disable_end(pCtx);

      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      int bShiftFreeze = (int)g_bShiftFreezeEnabled;
      if (nk_checkbox_label(pCtx, "Hold SHIFT to freeze", &bShiftFreeze)) {
        g_bShiftFreezeEnabled = (bool)bShiftFreeze;
        InputSaveConfig();
      }

      int bKeepWindowSize = g_bKeepWindowSize ? 1 : 0;
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      if (nk_checkbox_label(pCtx, "Keep Window Size", &bKeepWindowSize)) {
        g_bKeepWindowSize = (bool)bKeepWindowSize;
        InputSaveConfig();
      }

      int bHideLog = pOverlay->bHideLog ? 1 : 0;
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      if (nk_checkbox_label(pCtx, "Hide log", &bHideLog))
        pOverlay->bHideLog = bHideLog != 0;

      int bReset = 0;
      nk_layout_row_dynamic(pCtx, DEBUG_ROW_H, 1);
      if (nk_checkbox_label(pCtx, "Reset graphics", &bReset) && bReset) {
        g_fRenderScale    = 1.0f;  g_iAntiAliasing   = 0;
        g_iAnisotropyLevel= 3;     g_iTextureFilter  = 0;
        g_bTrilinear      = false; g_fLodBias        = 0.0f;
        g_fFogStart       = 0.0f;  g_fFogDensity     = 0.0f;
        g_uFogColor       = 0xB3BFCCu;
        g_fFovMultiplier  = 1.0f;  g_fVigStrength    = 0.0f;
        g_fMirrorFov      = 0.75f;
        g_fBrightness     = 0.0f;  g_fContrast       = 1.0f;
        g_fGamma          = 1.0f;  g_fSaturation     = 1.0f;
        g_iFpsDisplay     = 0;     g_bWireframe      = false;  g_iCullMode = 0;
        g_bVsync          = true;  g_bCRTFilter      = false;
        game_render_set_render_scale(g_pGameRenderer,    g_fRenderScale);
        game_render_set_antialiasing(g_pGameRenderer,    g_iAntiAliasing);
        game_render_set_anisotropy_level(g_pGameRenderer,g_iAnisotropyLevel);
        game_render_set_texture_filter(g_pGameRenderer,  g_iTextureFilter);
        game_render_set_trilinear(g_pGameRenderer,       g_bTrilinear);
        game_render_set_lod_bias(g_pGameRenderer,        g_fLodBias);
        game_render_set_fog_start(g_pGameRenderer,       g_fFogStart);
        game_render_set_fog_density(g_pGameRenderer,     g_fFogDensity);
        game_render_set_fog_color(g_pGameRenderer,
            ((g_uFogColor >> 16) & 0xFF) / 255.0f,
            ((g_uFogColor >>  8) & 0xFF) / 255.0f,
            ( g_uFogColor        & 0xFF) / 255.0f);
        game_render_set_fov_multiplier(g_pGameRenderer,  g_fFovMultiplier);
        game_render_set_vignette(g_pGameRenderer,        g_fVigStrength);
        game_render_set_brightness(g_pGameRenderer,      g_fBrightness);
        game_render_set_contrast(g_pGameRenderer,        g_fContrast);
        game_render_set_gamma(g_pGameRenderer,           g_fGamma);
        game_render_set_saturation(g_pGameRenderer,      g_fSaturation);
        game_render_set_vsync(g_pGameRenderer,           g_bVsync);
        game_render_set_wireframe(g_pGameRenderer,       g_bWireframe);
        game_render_set_cull_mode(g_pGameRenderer,       g_iCullMode);
        game_render_set_crt_filter(g_pGameRenderer,      NULL);
        InputSaveConfig();
      }
    }
  }
  nk_end(pCtx);
}

static void DrawLogPanel(DebugOverlay *pOverlay) {
  struct nk_context *pCtx = &pOverlay->nk;

  if (nk_begin(pCtx, "Log",
               nk_rect(RIGHT_X, PANEL_Y, RIGHT_W, PANEL_H),
               NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
    nk_layout_row_dynamic(pCtx, PANEL_H - 50, 1);
    if (nk_group_begin(pCtx, "log_inner", NK_WINDOW_BORDER)) {
      nk_layout_row_dynamic(pCtx, LOG_ROW_H, 1);
      SDL_LockMutex(pOverlay->pLogMutex);
      int iCount = pOverlay->iLogCount;
      int iHead  = pOverlay->iLogHead;
      for (int iI = 0; iI < iCount; iI++) {
        int iIdx = (iHead + iI) % MAX_LOG_MESSAGES;
        nk_label(pCtx, pOverlay->aLogEntries[iIdx].szText, NK_TEXT_LEFT);
      }
      SDL_UnlockMutex(pOverlay->pLogMutex);
      nk_group_end(pCtx);
    }
  }
  nk_end(pCtx);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void debug_overlay_render(DebugOverlay *pOverlay,
                          SDL_GPUCommandBuffer *pCmdBuf,
                          SDL_GPUTexture *pSwapchainTex,
                          Uint32 uiSwapchainW, Uint32 uiSwapchainH) {
  SDL_GPUViewport viewport = {0};

  const bool *_kb = SDL_GetKeyboardState(NULL);
  bool bShiftLabel = _kb[SDL_SCANCODE_LSHIFT] || _kb[SDL_SCANCODE_RSHIFT];
  bool bShowLabels = g_bSurfaceDebugViz || bShiftLabel;

  /* Releasing SHIFT (or turning off surface debug viz) dismisses the pick
   * outline entirely rather than letting it reappear -- showing labels acts
   * as a "clear the outline" gesture, not just a temporary hide. */
  static bool s_showLabelsWasActive;
  if (s_showLabelsWasActive && !bShowLabels)
    s_pickOutline.active = false;
  s_showLabelsWasActive = bShowLabels;

  if (!pOverlay || (!pOverlay->bVisible && !bShowLabels && !s_pickOutline.active && !s_pickOutlineWasActive)) return;

  struct nk_context *pCtx = &pOverlay->nk;
  DebugOverlayHandlePendingTapRelease(pOverlay);
  // Close input bracket (open it first if no events arrived this frame)
  if (!pOverlay->bInputBegun)
    nk_input_begin(pCtx);
  nk_input_end(pCtx);
  pOverlay->bInputBegun = false;

  if (pOverlay->bVisible) {
    DrawHintPanel(pOverlay);
    DrawDebugPanel(pOverlay);
    if (!pOverlay->bHideLog)
      DrawLogPanel(pOverlay);
  }

  /* Lazy re-rasterize: hash the Nuklear command buffer (FNV-1a).
   * Skip the expensive software raster + GPU upload when nothing changed;
   * the GPU texture still holds the previous frame's correct content.
   * Surface debug labels change every frame, so bypass the cache while active. */
  if (bShowLabels || s_pickOutline.active || s_pickOutlineWasActive)
    pOverlay->uLastCmdHash = 0;  /* force dirty every frame, plus one extra
                                    frame on the active->inactive transition */
  s_pickOutlineWasActive = s_pickOutline.active;

  uint32_t uHash = 2166136261u;
  const uint8_t *pCmdBytes = (const uint8_t *)pCtx->memory.memory.ptr;
  nk_size uCmdSize = pCtx->memory.allocated;
  for (nk_size i = 0; i < uCmdSize; i++) {
    uHash ^= pCmdBytes[i];
    uHash *= 16777619u;
  }

  if (uHash != pOverlay->uLastCmdHash) {
    pOverlay->uLastCmdHash = uHash;
    memset(pOverlay->pPixels, 0, OVERLAY_W * OVERLAY_H * OVERLAY_BPP);
    RenderCommands(pOverlay);
    if (bShowLabels) DrawSurfaceLabels(pOverlay);
    else             DrawSurfaceOutline(pOverlay);
    UploadAndBlit(pOverlay, pCmdBuf);
  } else {
    nk_clear(pCtx);
  }

  if (!pOverlay->pPipeline || !pOverlay->pSampler) return;

  SDL_GPUColorTargetInfo ct = {0};
  ct.texture   = pSwapchainTex;
  ct.load_op   = SDL_GPU_LOADOP_LOAD;
  ct.store_op  = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass *pRp = SDL_BeginGPURenderPass(pCmdBuf, &ct, 1, NULL);
  ROLLERGetPresentViewport(uiSwapchainW, uiSwapchainH, OVERLAY_ASPECT,
                           &viewport);
  SDL_SetGPUViewport(pRp, &viewport);
  SDL_BindGPUGraphicsPipeline(pRp, pOverlay->pPipeline);
  SDL_GPUTextureSamplerBinding binding = { .texture = pOverlay->pTexture, .sampler = pOverlay->pSampler };
  SDL_BindGPUFragmentSamplers(pRp, 0, &binding, 1);
  SDL_DrawGPUPrimitives(pRp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pRp);
}
