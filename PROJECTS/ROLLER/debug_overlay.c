#include "debug_overlay.h"
#include "debug_overlay_shaders.h"
#include "roller.h"
#include "menu_render.h"
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
#define NK_IMPLEMENTATION
#include "nuklear.h"

// ---------------------------------------------------------------------------

#define OVERLAY_W        1280
#define OVERLAY_H        720
#define OVERLAY_BPP      4    // RGBA

#define MAX_LOG_MESSAGES 512
#define MAX_LOG_LEN      256

#define PANEL_MARGIN     10
#define PANEL_H          (OVERLAY_H - PANEL_MARGIN * 2)
#define LEFT_W           410
#define RIGHT_X          (PANEL_MARGIN + LEFT_W + PANEL_MARGIN)
#define RIGHT_W          (OVERLAY_W - RIGHT_X - PANEL_MARGIN)
#define LOG_ROW_H        20

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

  bool                   bInputBegun;
};

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
  pOverlay->bVisible = false;

  nk_font_atlas_init_default(&pOverlay->atlas);
  nk_font_atlas_begin(&pOverlay->atlas);
  struct nk_font *pFont = nk_font_atlas_add_default(&pOverlay->atlas, 16, NULL);

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

bool debug_overlay_is_visible(DebugOverlay *pOverlay) {
  return pOverlay && pOverlay->bVisible;
}

void debug_overlay_toggle(DebugOverlay *pOverlay) {
  if (pOverlay) pOverlay->bVisible = !pOverlay->bVisible;
}

void debug_overlay_handle_event(DebugOverlay *pOverlay, SDL_Event *pEvent) {
  if (!pOverlay || !pOverlay->bVisible) return;

  // Open input bracket on first event of the frame
  if (!pOverlay->bInputBegun) {
    nk_input_begin(&pOverlay->nk);
    pOverlay->bInputBegun = true;
  }

  // Scale window coords to logical overlay space
  int iWinW, iWinH;
  SDL_GetWindowSizeInPixels(pOverlay->pWindow, &iWinW, &iWinH);
  float fScaleX = (float)OVERLAY_W / (float)iWinW;
  float fScaleY = (float)OVERLAY_H / (float)iWinH;

  struct nk_context *pCtx = &pOverlay->nk;
  if (pEvent->type == SDL_EVENT_MOUSE_MOTION) {
    nk_input_motion(pCtx,
                    (int)(pEvent->motion.x * fScaleX),
                    (int)(pEvent->motion.y * fScaleY));
  } else if (pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             pEvent->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    int iDown = (pEvent->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    if (pEvent->button.button == SDL_BUTTON_LEFT)
      nk_input_button(pCtx, NK_BUTTON_LEFT,
                      (int)(pEvent->button.x * fScaleX),
                      (int)(pEvent->button.y * fScaleY), iDown);
  } else if (pEvent->type == SDL_EVENT_MOUSE_WHEEL) {
    nk_input_scroll(pCtx, nk_vec2(0.0f, pEvent->wheel.y * 20.0f));
  }
}

// ---------------------------------------------------------------------------
// UI panels
// ---------------------------------------------------------------------------

static void DrawDebugPanel(DebugOverlay *pOverlay) {
  struct nk_context *pCtx = &pOverlay->nk;
  if (nk_begin(pCtx, "Settings",
               nk_rect(PANEL_MARGIN, PANEL_MARGIN, LEFT_W, PANEL_H),
               NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
    nk_layout_row_dynamic(pCtx, 20, 1);
    nk_label(pCtx, "Experimental", NK_TEXT_LEFT);

    MenuRenderer *pRenderer = GetMenuRenderer();
    if (pRenderer) {
      int bGPU = (menu_render_get_mode(pRenderer) == MENU_RENDER_GPU);
      nk_layout_row_dynamic(pCtx, 20, 1);
      if (nk_checkbox_label(pCtx, "Hardware rendering", &bGPU))
        menu_render_set_mode(pRenderer, bGPU ? MENU_RENDER_GPU : MENU_RENDER_SOFTWARE);
    }

    int bForceMaxDraw = (int)g_bForceMaxDraw;
    nk_layout_row_dynamic(pCtx, 20, 1);
    if (nk_checkbox_label(pCtx, "Infinite draw distance", &bForceMaxDraw))
      g_bForceMaxDraw = (bool)bForceMaxDraw;
  }
  nk_end(pCtx);
}

static void DrawLogPanel(DebugOverlay *pOverlay) {
  struct nk_context *pCtx = &pOverlay->nk;

  if (nk_begin(pCtx, "Log",
               nk_rect(RIGHT_X, PANEL_MARGIN, RIGHT_W, PANEL_H),
               NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
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
  if (!pOverlay || !pOverlay->bVisible) return;

  struct nk_context *pCtx = &pOverlay->nk;
  // Close input bracket (open it first if no events arrived this frame)
  if (!pOverlay->bInputBegun)
    nk_input_begin(pCtx);
  nk_input_end(pCtx);
  pOverlay->bInputBegun = false;

  DrawDebugPanel(pOverlay);
  DrawLogPanel(pOverlay);

  memset(pOverlay->pPixels, 0, OVERLAY_W * OVERLAY_H * OVERLAY_BPP);
  RenderCommands(pOverlay);
  UploadAndBlit(pOverlay, pCmdBuf);

  if (!pOverlay->pPipeline || !pOverlay->pSampler) return;

  SDL_GPUColorTargetInfo ct = {0};
  ct.texture   = pSwapchainTex;
  ct.load_op   = SDL_GPU_LOADOP_LOAD;
  ct.store_op  = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass *pRp = SDL_BeginGPURenderPass(pCmdBuf, &ct, 1, NULL);
  SDL_BindGPUGraphicsPipeline(pRp, pOverlay->pPipeline);
  SDL_GPUTextureSamplerBinding binding = { .texture = pOverlay->pTexture, .sampler = pOverlay->pSampler };
  SDL_BindGPUFragmentSamplers(pRp, 0, &binding, 1);
  SDL_DrawGPUPrimitives(pRp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pRp);
}
