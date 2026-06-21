#include "crt_filter.h"
#include "crt_shaders.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Uniform layout (matches cbuffer CRTUniforms in crt_pixel.hlsl)
// ---------------------------------------------------------------------------

typedef struct {
    float srcSizeX, srcSizeY;   // source texture pixel dimensions
    float dstSizeX, dstSizeY;   // reserved / destination viewport size
    float scanlines;            // SCANLINES_STRENGTH  (0-1; x4 inside shader)
    float beamMin;              // BEAM_MIN_WIDTH
    float beamMax;              // BEAM_MAX_WIDTH
    float colorBoost;           // COLOR_BOOST
    float phosphorLayout;       // 0=off 1=aperture 2=shadow-mask
    float maskIntensity;        // MASK_INTENSITY
    float inputGamma;           // INPUT_GAMMA
    float outputGamma;          // OUTPUT_GAMMA
    float antiRinging;          // CRT_ANTI_RINGING
    float _pad;
} CRTUniforms;

// ---------------------------------------------------------------------------
// Defaults — decent 1080p look out of the box
// ---------------------------------------------------------------------------

#define CRT_DEFAULT_SCANLINES       0.40f  // scanStr=1.6 — just above the ~1.55 anti-scanline
                                           // crossover.  Below that, gaps exceed centres.
                                           // At 1.6 bright areas clamp to 1.0 (invisible
                                           // scanlines on sky), dark/mid areas get CRT texture.
#define CRT_DEFAULT_BEAM_MIN        0.86f
#define CRT_DEFAULT_BEAM_MAX        1.00f
#define CRT_DEFAULT_COLOR_BOOST     1.40f
#define CRT_DEFAULT_PHOSPHOR_LAYOUT 1.0f   // aperture grille — no Y-dependence, so no
                                           // row-flicker at any zoom level or camera movement
#define CRT_DEFAULT_MASK_INTENSITY  0.20f
#define CRT_DEFAULT_INPUT_GAMMA     2.40f
#define CRT_DEFAULT_OUTPUT_GAMMA    2.20f
#define CRT_DEFAULT_ANTI_RINGING    1.00f

// GPU-mode reference magnification (640×400 → 1080p): 1080/400 = 2.7 pix/row.
// Used to scale the phosphor mask intensity at other source/dest combinations.
#define CRT_REF_PIX_PER_ROW         2.7f

// ---------------------------------------------------------------------------

struct CRTFilter {
    SDL_GPUDevice            *device;
    SDL_GPUGraphicsPipeline  *pipeline;
    SDL_GPUSampler           *sampler;
};

// ---------------------------------------------------------------------------

static SDL_GPUShader *LoadCRTShader(SDL_GPUDevice *device,
                                    SDL_GPUShaderStage stage,
                                    const unsigned char *pSpirv, unsigned int uSpirv,
                                    const unsigned char *pMsl,   unsigned int uMsl,
                                    int numSamplers, int numUniforms)
{
    SDL_GPUShaderFormat fmts = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info = {0};
    info.stage              = stage;
    info.num_samplers       = numSamplers;
    info.num_uniform_buffers = numUniforms;

    if (fmts & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format     = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code       = pSpirv;
        info.code_size  = uSpirv;
        info.entrypoint = "main";
    } else if (fmts & SDL_GPU_SHADERFORMAT_MSL) {
        info.format     = SDL_GPU_SHADERFORMAT_MSL;
        info.code       = pMsl;
        info.code_size  = uMsl;
        info.entrypoint = "main0";
    } else {
        return NULL;
    }
    return SDL_CreateGPUShader(device, &info);
}

CRTFilter *crt_filter_create(SDL_GPUDevice *device, SDL_Window *window)
{
    if (!device || !window) return NULL;

    CRTFilter *f = calloc(1, sizeof(CRTFilter));
    if (!f) return NULL;
    f->device = device;

    // --- Shaders ---
    SDL_GPUShader *pVert = LoadCRTShader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        crt_vertex_spirv, crt_vertex_spirv_size,
        crt_vertex_msl,   crt_vertex_msl_size,
        0, 0);
    SDL_GPUShader *pFrag = LoadCRTShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        crt_pixel_spirv,  crt_pixel_spirv_size,
        crt_pixel_msl,    crt_pixel_msl_size,
        1, 1);    /* 1 sampler, 1 uniform buffer */

    if (!pVert || !pFrag) {
        SDL_Log("crt_filter: failed to load shaders: %s", SDL_GetError());
        if (pVert) SDL_ReleaseGPUShader(device, pVert);
        if (pFrag) SDL_ReleaseGPUShader(device, pFrag);
        free(f);
        return NULL;
    }

    // --- Pipeline ---
    SDL_GPUColorTargetDescription ct = {0};
    ct.format = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUGraphicsPipelineCreateInfo pi = {0};
    pi.vertex_shader   = pVert;
    pi.fragment_shader = pFrag;
    pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets         = 1;

    f->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);

    SDL_ReleaseGPUShader(device, pVert);
    SDL_ReleaseGPUShader(device, pFrag);

    if (!f->pipeline) {
        SDL_Log("crt_filter: failed to create pipeline: %s", SDL_GetError());
        free(f);
        return NULL;
    }

    // --- Nearest sampler (shader does its own cubic interpolation) ---
    SDL_GPUSamplerCreateInfo si = {0};
    si.min_filter     = SDL_GPU_FILTER_NEAREST;
    si.mag_filter     = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    f->sampler = SDL_CreateGPUSampler(device, &si);

    if (!f->sampler) {
        SDL_Log("crt_filter: failed to create sampler: %s", SDL_GetError());
        SDL_ReleaseGPUGraphicsPipeline(device, f->pipeline);
        free(f);
        return NULL;
    }

    return f;
}

void crt_filter_destroy(CRTFilter *filter)
{
    if (!filter) return;
    if (filter->sampler)  SDL_ReleaseGPUSampler(filter->device, filter->sampler);
    if (filter->pipeline) SDL_ReleaseGPUGraphicsPipeline(filter->device, filter->pipeline);
    free(filter);
}

void crt_filter_apply(CRTFilter       *filter,
                      SDL_GPUCommandBuffer *cmd,
                      SDL_GPUTexture  *srcTex,
                      Uint32           srcW,
                      Uint32           srcH,
                      SDL_GPUTexture  *dstTex,
                      Uint32           dstX,
                      Uint32           dstY,
                      Uint32           dstW,
                      Uint32           dstH)
{
    if (!filter || !cmd || !srcTex || !dstTex) return;
    if (dstW == 0 || dstH == 0) return;

    // Clear whole dest to black, then render CRT into letterbox rect via
    // viewport + scissor so black bars outside the rect stay black.
    SDL_GPUColorTargetInfo cti = {0};
    cti.texture     = dstTex;
    cti.load_op     = SDL_GPU_LOADOP_CLEAR;
    cti.store_op    = SDL_GPU_STOREOP_STORE;
    cti.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};

    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &cti, 1, NULL);
    if (!rp) return;

    SDL_GPUViewport vp = {(float)dstX, (float)dstY, (float)dstW, (float)dstH, 0.0f, 1.0f};
    SDL_SetGPUViewport(rp, &vp);

    SDL_Rect scissor = {(int)dstX, (int)dstY, (int)dstW, (int)dstH};
    SDL_SetGPUScissor(rp, &scissor);

    SDL_BindGPUGraphicsPipeline(rp, filter->pipeline);

    SDL_GPUTextureSamplerBinding tsb = {srcTex, filter->sampler};
    SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);

    /* maskIntensity scales down at higher magnification so phosphor column
     * stripes stay subtle when each source pixel spans many output pixels.
     * Scanlines and colorBoost are NOT scaled by t: the Gaussian formula has a
     * ~1.55 anti-scanline crossover (below that, gaps are brighter than centres).
     * CRT_DEFAULT_SCANLINES=0.40 (scanStr=1.6) sits just above the crossover;
     * halving it for SW mode would invert the effect.  At scanStr=1.6, bright
     * areas self-clip to 1.0 so scanlines are invisible on sky at any
     * magnification — no additional scaling is needed or safe. */
    float pixPerRow = (srcH > 0)
                      ? (float)dstH / (float)srcH
                      : CRT_REF_PIX_PER_ROW;
    float t = SDL_min(1.0f, CRT_REF_PIX_PER_ROW / pixPerRow);

    CRTUniforms u;
    u.srcSizeX       = (float)srcW;
    u.srcSizeY       = (float)srcH;
    u.dstSizeX       = (float)dstW;
    u.dstSizeY       = (float)dstH;
    u.scanlines      = CRT_DEFAULT_SCANLINES;
    u.beamMin        = CRT_DEFAULT_BEAM_MIN;
    u.beamMax        = CRT_DEFAULT_BEAM_MAX;
    u.colorBoost     = CRT_DEFAULT_COLOR_BOOST;
    u.phosphorLayout = CRT_DEFAULT_PHOSPHOR_LAYOUT;
    u.maskIntensity  = CRT_DEFAULT_MASK_INTENSITY * t;  // 0.20 → ~0.10 at SW 5.4x scale
    u.inputGamma     = CRT_DEFAULT_INPUT_GAMMA;
    u.outputGamma    = CRT_DEFAULT_OUTPUT_GAMMA;
    u.antiRinging    = CRT_DEFAULT_ANTI_RINGING;
    u._pad           = 0.0f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));

    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
}
