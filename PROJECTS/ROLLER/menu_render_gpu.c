#include "menu_render_gpu.h"
#include "menu_shaders.h"
#include "carplans.h"
#include "car.h"
#include "graphics.h"
#include "3d.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define MENU_WIDTH 640
#define MENU_HEIGHT 400
#define MAX_SLOTS 16
#define MAX_BLOCKS_PER_SLOT 256
#define MAX_QUADS_PER_FRAME 1024

typedef struct {
    float position[2];
    float uv[2];
} MenuVertex;

// Pixel uniform block (must match HLSL cbuffer PixelUniforms layout)
typedef struct {
    float alphaMul;
    float transparentR, transparentG, transparentB;
    float replaceFromR, replaceFromG, replaceFromB;
    float replaceToR, replaceToG, replaceToB;
    float _pad0, _pad1;
} PixelUniforms;

// Recorded draw command (deferred — replayed in end_frame)
typedef struct {
    SDL_GPUTexture *texture;
    int vertexOffset;  // offset into vertex array (in vertices)
    int vertexCount;
    MenuDrawLayer layer;
    PixelUniforms uniforms;
} DrawCommand;

#define MAX_DRAW_COMMANDS 512

// 3D mesh vertex (must match HLSL MeshVertexInput layout: 36 bytes)
typedef struct {
    float position[3];
    float uv[2];
    float color[4];
} MeshVertex;

// Loaded mesh GPU buffers
typedef struct {
    SDL_GPUBuffer *vertexBuffer;
    SDL_GPUBuffer *indexBuffer;
    SDL_GPUTexture *texture;
    int indexCount;
    bool loaded;
} MeshPreview;

// Recorded mesh draw command (deferred -- replayed in end_frame)
typedef struct {
    SDL_GPUBuffer *vertexBuffer;
    SDL_GPUBuffer *indexBuffer;
    SDL_GPUTexture *texture;
    int indexCount;
    float mvp[16];
    float vpX, vpY, vpW, vpH; // viewport in virtual 640x400 coords
    bool useDepth;
} MeshDrawCommand;

#define MAX_MESH_DRAWS 4

struct MenuRendererGPU {
    SDL_GPUDevice *device;
    SDL_Window *window;

    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler *sampler;
    SDL_GPUBuffer *vertexBuffer;
    SDL_GPUTransferBuffer *vertexTransferBuffer;

    // Per-slot textures (front_vga[0..15])
    MenuTexture *slotTextures[MAX_SLOTS];
    int slotTextureCount[MAX_SLOTS];

    // Background textures (full 640x400 raw images)
    SDL_GPUTexture *backgroundTextures[MAX_SLOTS];

    // Frame state
    SDL_GPUCommandBuffer *cmdBuf;
    SDL_GPUTexture *swapchainTexture;
    Uint32 swapchainWidth;
    Uint32 swapchainHeight;

    // Deferred vertex + draw command accumulation
    MenuVertex vertices[MAX_QUADS_PER_FRAME * 6];
    int vertexCount;
    DrawCommand drawCommands[MAX_DRAW_COMMANDS];
    int drawCommandCount;

    // Fade
    SDL_GPUTexture *blackTexture;
    float fadeAlpha;
    float fadeStep;
    int fadeActive;

    // Mesh pipelines (3D previews)
    SDL_GPUGraphicsPipeline *meshPipeline;
    SDL_GPUGraphicsPipeline *meshPipelineNoDepth;
    SDL_GPUTexture *depthTexture;
    Uint32 depthWidth;
    Uint32 depthHeight;
    SDL_GPUTexture *whiteTexture;

    // 3D mesh state
    MeshPreview carMesh;
    int loadedCarIdx;   // -1 = no car loaded (avoids per-frame reload)
    bool trackMeshLoaded;
    MeshPreview trackMesh;

    // Per-frame mesh draw commands
    MeshDrawCommand meshDraws[MAX_MESH_DRAWS];
    int meshDrawCount;
    MenuDrawLayer currentLayer;

};

//---------------------------------------------------------------------------
// Shader loading helper
//---------------------------------------------------------------------------

static SDL_GPUShader *LoadShader(SDL_GPUDevice *device, SDL_GPUShaderStage stage,
    const unsigned char *spirv, unsigned int spirvSize,
    const unsigned char *msl, unsigned int mslSize,
    int numSamplers, int numUniformBuffers)
{
    SDL_GPUShaderFormat fmts = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info = {0};
    info.stage = stage;
    info.num_samplers = numSamplers;
    info.num_uniform_buffers = numUniformBuffers;

    if (fmts & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = spirv;
        info.code_size = spirvSize;
        info.entrypoint = "main";
    } else if (fmts & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = msl;
        info.code_size = mslSize;
        info.entrypoint = "main0";
    } else {
        return NULL;
    }

    return SDL_CreateGPUShader(device, &info);
}

//---------------------------------------------------------------------------
// Projection, quad emit, draw recording
//---------------------------------------------------------------------------

static void FreeMeshPreview(SDL_GPUDevice *dev, MeshPreview *mp, SDL_GPUTexture *whiteTexture);

static void EnsureDepthTexture(MenuRendererGPU *r)
{
    if (r->depthTexture && r->depthWidth == r->swapchainWidth &&
        r->depthHeight == r->swapchainHeight)
        return;

    if (r->depthTexture)
        SDL_ReleaseGPUTexture(r->device, r->depthTexture);

    SDL_GPUTextureCreateInfo dti = {0};
    dti.type = SDL_GPU_TEXTURETYPE_2D;
    dti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    dti.width = r->swapchainWidth;
    dti.height = r->swapchainHeight;
    dti.layer_count_or_depth = 1;
    dti.num_levels = 1;
    dti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    r->depthTexture = SDL_CreateGPUTexture(r->device, &dti);
    r->depthWidth = r->swapchainWidth;
    r->depthHeight = r->swapchainHeight;
}

static void MakeOrthoProjection(float *m, float l, float r, float b, float t)
{
    memset(m, 0, 16 * sizeof(float));
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1.0f;
}

static void EmitQuad(MenuRendererGPU *r, float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1)
{
    if (r->vertexCount + 6 > MAX_QUADS_PER_FRAME * 6) return;
    MenuVertex *v = &r->vertices[r->vertexCount];
    v[0] = (MenuVertex){{x,     y},     {u0, v0}};
    v[1] = (MenuVertex){{x + w, y},     {u1, v0}};
    v[2] = (MenuVertex){{x,     y + h}, {u0, v1}};
    v[3] = (MenuVertex){{x + w, y},     {u1, v0}};
    v[4] = (MenuVertex){{x + w, y + h}, {u1, v1}};
    v[5] = (MenuVertex){{x,     y + h}, {u0, v1}};
    r->vertexCount += 6;
}

// tColor has unusual field order: byR, byB, byG
static float ColorToFloat(uint8 component6bit)
{
    return (float)(component6bit * 255 / 63) / 255.0f;
}

static void RecordDraw(MenuRendererGPU *r, SDL_GPUTexture *texture,
                       float alphaMul, int transparentIdx, const tColor *pal)
{
    if (r->vertexCount == 0 || r->drawCommandCount >= MAX_DRAW_COMMANDS) return;

    DrawCommand *cmd = &r->drawCommands[r->drawCommandCount++];
    cmd->texture = texture;
    cmd->vertexOffset = r->vertexCount - 6; // last EmitQuad wrote 6 verts
    cmd->vertexCount = 6;
    cmd->layer = r->currentLayer;
    memset(&cmd->uniforms, 0, sizeof(cmd->uniforms));
    cmd->uniforms.alphaMul = alphaMul;

    if (transparentIdx >= 0 && pal) {
        const tColor *c = &pal[transparentIdx];
        cmd->uniforms.transparentR = ColorToFloat(c->byR);
        cmd->uniforms.transparentG = ColorToFloat(c->byG); // byG, not byB
        cmd->uniforms.transparentB = ColorToFloat(c->byB); // byB, not byG
    } else {
        cmd->uniforms.transparentR = cmd->uniforms.transparentG = cmd->uniforms.transparentB = -1.0f;
    }
    // Color replacement disabled by default
    cmd->uniforms.replaceFromR = -1.0f;
}

static void RecordDrawWithColorReplace(MenuRendererGPU *r, SDL_GPUTexture *texture,
                                       float alphaMul, uint8 fromIdx, uint8 toIdx,
                                       const tColor *pal)
{
    if (r->vertexCount == 0 || r->drawCommandCount >= MAX_DRAW_COMMANDS) return;

    DrawCommand *cmd = &r->drawCommands[r->drawCommandCount++];
    cmd->texture = texture;
    cmd->vertexOffset = r->vertexCount - 6;
    cmd->vertexCount = 6;
    cmd->layer = r->currentLayer;
    memset(&cmd->uniforms, 0, sizeof(cmd->uniforms));
    cmd->uniforms.alphaMul = alphaMul;

    // Enable alpha-based transparency discard (shader checks transparentR >= 0)
    cmd->uniforms.transparentR = 0.0f;
    cmd->uniforms.transparentG = 0.0f;
    cmd->uniforms.transparentB = 0.0f;

    // Color replacement: fromIdx -> toIdx
    if (pal) {
        const tColor *fc = &pal[fromIdx];
        cmd->uniforms.replaceFromR = ColorToFloat(fc->byR);
        cmd->uniforms.replaceFromG = ColorToFloat(fc->byG);
        cmd->uniforms.replaceFromB = ColorToFloat(fc->byB);
        const tColor *tc = &pal[toIdx];
        cmd->uniforms.replaceToR = ColorToFloat(tc->byR);
        cmd->uniforms.replaceToG = ColorToFloat(tc->byG);
        cmd->uniforms.replaceToB = ColorToFloat(tc->byB);
    } else {
        cmd->uniforms.replaceFromR = -1.0f;
    }
}

static void ReplayDrawsLayer(MenuRendererGPU *r, SDL_GPURenderPass *renderPass,
                              MenuDrawLayer minLayer, MenuDrawLayer maxLayer)
{
    float proj[16];
    MakeOrthoProjection(proj, 0, MENU_WIDTH, MENU_HEIGHT, 0);

    SDL_BindGPUGraphicsPipeline(renderPass, r->pipeline);
    SDL_PushGPUVertexUniformData(r->cmdBuf, 0, proj, sizeof(proj));

    SDL_GPUBufferBinding vbb = { .buffer = r->vertexBuffer };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vbb, 1);

    for (int i = 0; i < r->drawCommandCount; i++) {
        DrawCommand *cmd = &r->drawCommands[i];
        if (cmd->layer < minLayer || cmd->layer > maxLayer) continue;

        SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &cmd->uniforms, sizeof(cmd->uniforms));

        SDL_GPUTextureSamplerBinding tsb = { .texture = cmd->texture, .sampler = r->sampler };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &tsb, 1);

        SDL_DrawGPUPrimitives(renderPass, cmd->vertexCount, 1, cmd->vertexOffset, 0);
    }
}

static void ReplayMeshDraws(MenuRendererGPU *r, SDL_GPURenderPass *renderPass)
{
    if (!r->meshPipeline || r->meshDrawCount == 0) return;

    // Compute viewport mapping from virtual 640x400 to swapchain pixels
    float wAsp = (float)r->swapchainWidth / (float)r->swapchainHeight;
    float mAsp = (float)MENU_WIDTH / (float)MENU_HEIGHT;
    float baseX, baseY, baseW, baseH;
    if (wAsp > mAsp) {
        baseH = (float)r->swapchainHeight;
        baseW = mAsp * baseH;
        baseX = (r->swapchainWidth - baseW) / 2.0f;
        baseY = 0;
    } else {
        baseW = (float)r->swapchainWidth;
        baseH = baseW / mAsp;
        baseX = 0;
        baseY = (r->swapchainHeight - baseH) / 2.0f;
    }
    float scaleX = baseW / (float)MENU_WIDTH;
    float scaleY = baseH / (float)MENU_HEIGHT;

    SDL_GPUGraphicsPipeline *curPipeline = NULL;

    for (int i = 0; i < r->meshDrawCount; i++) {
        MeshDrawCommand *cmd = &r->meshDraws[i];
        SDL_GPUGraphicsPipeline *wanted = cmd->useDepth ? r->meshPipeline : r->meshPipelineNoDepth;
        if (wanted != curPipeline) {
            SDL_BindGPUGraphicsPipeline(renderPass, wanted);
            curPipeline = wanted;
        }

        // Set sub-viewport for this mesh preview
        SDL_GPUViewport mvp = {0};
        mvp.x = baseX + cmd->vpX * scaleX;
        mvp.y = baseY + cmd->vpY * scaleY;
        mvp.w = cmd->vpW * scaleX;
        mvp.h = cmd->vpH * scaleY;
        mvp.min_depth = 0.0f;
        mvp.max_depth = 1.0f;
        SDL_SetGPUViewport(renderPass, &mvp);

        SDL_PushGPUVertexUniformData(r->cmdBuf, 0, cmd->mvp, sizeof(cmd->mvp));

        SDL_GPUTextureSamplerBinding tsb = { .texture = cmd->texture, .sampler = r->sampler };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &tsb, 1);

        SDL_GPUBufferBinding vbb = { .buffer = cmd->vertexBuffer };
        SDL_BindGPUVertexBuffers(renderPass, 0, &vbb, 1);

        SDL_GPUBufferBinding ibb = { .buffer = cmd->indexBuffer };
        SDL_BindGPUIndexBuffer(renderPass, &ibb, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(renderPass, cmd->indexCount, 1, 0, 0, 0);
    }
}

//---------------------------------------------------------------------------
// Asset upload helper
//---------------------------------------------------------------------------

static SDL_GPUTexture *UploadRGBA(SDL_GPUDevice *dev, const uint8 *rgba, int w, int h)
{
    SDL_GPUTextureCreateInfo ti = {0};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width = w; ti.height = h;
    ti.layer_count_or_depth = 1; ti.num_levels = 1;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(dev, &ti);
    if (!tex) return NULL;

    SDL_GPUTransferBufferCreateInfo tbi = {0};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = w * h * 4;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
    memcpy(m, rgba, w * h * 4);
    SDL_UnmapGPUTransferBuffer(dev, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = { .transfer_buffer = tb };
    SDL_GPUTextureRegion dst = { .texture = tex, .w = w, .h = h, .d = 1 };
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return tex;
}

static SDL_GPUBuffer *UploadGPUBuffer(SDL_GPUDevice *dev, SDL_GPUBufferUsageFlags usage,
                                       const void *data, Uint32 size)
{
    SDL_GPUBufferCreateInfo bi = {0};
    bi.usage = usage;
    bi.size = size;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(dev, &bi);
    if (!buf) return NULL;

    SDL_GPUTransferBufferCreateInfo tbi = {0};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = size;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
    memcpy(m, data, size);
    SDL_UnmapGPUTransferBuffer(dev, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { .transfer_buffer = tb };
    SDL_GPUBufferRegion dst = { .buffer = buf, .size = size };
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return buf;
}

// tColor field order: byR, byB, byG — read byG for green, byB for blue
static void IndexedToRGBA(const uint8 *indexed, const tColor *pal, uint8 *rgba, int count)
{
    for (int i = 0; i < count; i++) {
        const tColor *c = &pal[indexed[i]];
        rgba[i * 4 + 0] = (c->byR * 255) / 63;
        rgba[i * 4 + 1] = (c->byG * 255) / 63;
        rgba[i * 4 + 2] = (c->byB * 255) / 63;
        // Bake transparency for palette index 0 into alpha channel.
        // Software display_block compares palette indices, not RGB values;
        // multiple palette entries can share the same RGB, so comparing
        // colors in the shader would incorrectly discard non-transparent pixels.
        rgba[i * 4 + 3] = (indexed[i] == 0) ? 0 : 255;
    }
}

//---------------------------------------------------------------------------
// Create / Destroy
//---------------------------------------------------------------------------

MenuRendererGPU *menu_render_gpu_create(SDL_GPUDevice *device, SDL_Window *window)
{
    MenuRendererGPU *r = calloc(1, sizeof(MenuRendererGPU));
    r->device = device;
    r->window = window;

    // Load shaders
    SDL_GPUShader *vert = LoadShader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        menu_vertex_spirv, menu_vertex_spirv_size,
        menu_vertex_msl, menu_vertex_msl_size,
        0, 1);

    SDL_GPUShader *frag = LoadShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        menu_pixel_spirv, menu_pixel_spirv_size,
        menu_pixel_msl, menu_pixel_msl_size,
        1, 1);

    if (!vert || !frag) {
        SDL_Log("Failed to create menu shaders");
        free(r);
        return NULL;
    }

    // Pipeline
    SDL_GPUGraphicsPipelineCreateInfo pipeInfo = {0};
    pipeInfo.vertex_shader = vert;
    pipeInfo.fragment_shader = frag;
    pipeInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    SDL_GPUVertexBufferDescription vbDesc = {0};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(MenuVertex);
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[2] = {0};
    attrs[0].location = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset = offsetof(MenuVertex, position);
    attrs[1].location = 1;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[1].offset = offsetof(MenuVertex, uv);

    pipeInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    pipeInfo.vertex_input_state.num_vertex_buffers = 1;
    pipeInfo.vertex_input_state.vertex_attributes = attrs;
    pipeInfo.vertex_input_state.num_vertex_attributes = 2;

    SDL_GPUColorTargetDescription colorTarget = {0};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    colorTarget.blend_state.enable_blend = true;
    colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    pipeInfo.target_info.color_target_descriptions = &colorTarget;
    pipeInfo.target_info.num_color_targets = 1;
    pipeInfo.target_info.has_depth_stencil_target = true;
    pipeInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    // depth_stencil_state defaults: depth test/write disabled (2D quads don't need depth)

    r->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeInfo);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!r->pipeline) {
        SDL_Log("Failed to create menu pipeline: %s", SDL_GetError());
        free(r);
        return NULL;
    }

    // --- Mesh pipeline (3D previews with depth testing) ---
    SDL_GPUShader *meshVert = LoadShader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        menu_mesh_vertex_spirv, menu_mesh_vertex_spirv_size,
        menu_mesh_vertex_msl, menu_mesh_vertex_msl_size,
        0, 1);  // 0 samplers, 1 uniform buffer (MVP)

    SDL_GPUShader *meshFrag = LoadShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        menu_mesh_pixel_spirv, menu_mesh_pixel_spirv_size,
        menu_mesh_pixel_msl, menu_mesh_pixel_msl_size,
        1, 0);  // 1 sampler, 0 uniform buffers

    if (!meshVert || !meshFrag) {
        SDL_Log("Failed to create mesh shaders");
    } else {
        SDL_GPUGraphicsPipelineCreateInfo meshPipeInfo = {0};
        meshPipeInfo.vertex_shader = meshVert;
        meshPipeInfo.fragment_shader = meshFrag;
        meshPipeInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

        SDL_GPUVertexBufferDescription meshVbDesc = {0};
        meshVbDesc.slot = 0;
        meshVbDesc.pitch = sizeof(MeshVertex);
        meshVbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute meshAttrs[3] = {0};
        meshAttrs[0].location = 0;
        meshAttrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        meshAttrs[0].offset = offsetof(MeshVertex, position);
        meshAttrs[1].location = 1;
        meshAttrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        meshAttrs[1].offset = offsetof(MeshVertex, uv);
        meshAttrs[2].location = 2;
        meshAttrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        meshAttrs[2].offset = offsetof(MeshVertex, color);

        meshPipeInfo.vertex_input_state.vertex_buffer_descriptions = &meshVbDesc;
        meshPipeInfo.vertex_input_state.num_vertex_buffers = 1;
        meshPipeInfo.vertex_input_state.vertex_attributes = meshAttrs;
        meshPipeInfo.vertex_input_state.num_vertex_attributes = 3;

        meshPipeInfo.target_info.color_target_descriptions = &colorTarget;
        meshPipeInfo.target_info.num_color_targets = 1;
        meshPipeInfo.target_info.has_depth_stencil_target = true;
        meshPipeInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

        meshPipeInfo.depth_stencil_state.enable_depth_test = true;
        meshPipeInfo.depth_stencil_state.enable_depth_write = true;
        meshPipeInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

        meshPipeInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        meshPipeInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        meshPipeInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

        r->meshPipeline = SDL_CreateGPUGraphicsPipeline(device, &meshPipeInfo);
        if (!r->meshPipeline)
            SDL_Log("Failed to create mesh pipeline: %s", SDL_GetError());

        // No-depth variant for flat meshes (track map)
        meshPipeInfo.depth_stencil_state.enable_depth_test = false;
        meshPipeInfo.depth_stencil_state.enable_depth_write = false;
        r->meshPipelineNoDepth = SDL_CreateGPUGraphicsPipeline(device, &meshPipeInfo);
        if (!r->meshPipelineNoDepth)
            SDL_Log("Failed to create no-depth mesh pipeline: %s", SDL_GetError());
    }
    if (meshVert) SDL_ReleaseGPUShader(device, meshVert);
    if (meshFrag) SDL_ReleaseGPUShader(device, meshFrag);

    // Sampler
    SDL_GPUSamplerCreateInfo sampInfo = {0};
    sampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    sampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    r->sampler = SDL_CreateGPUSampler(device, &sampInfo);

    // Vertex buffer
    SDL_GPUBufferCreateInfo bufInfo = {0};
    bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufInfo.size = sizeof(r->vertices);
    r->vertexBuffer = SDL_CreateGPUBuffer(device, &bufInfo);

    SDL_GPUTransferBufferCreateInfo tbInfo = {0};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = sizeof(r->vertices);
    r->vertexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &tbInfo);

    // 1x1 black texture for fade overlay
    uint8 blackPixel[4] = {0, 0, 0, 255};
    r->blackTexture = UploadRGBA(device, blackPixel, 1, 1);

    // 1x1 white texture for flat-colored meshes (track map)
    uint8 whitePixel[4] = {255, 255, 255, 255};
    r->whiteTexture = UploadRGBA(device, whitePixel, 1, 1);

    r->loadedCarIdx = -1;

    return r;
}

void menu_render_gpu_destroy(MenuRendererGPU *r)
{
    if (!r) return;
    for (int i = 0; i < MAX_SLOTS; i++)
        menu_render_gpu_free_blocks(r, i);
    if (r->blackTexture) SDL_ReleaseGPUTexture(r->device, r->blackTexture);
    FreeMeshPreview(r->device, &r->carMesh, r->whiteTexture);
    FreeMeshPreview(r->device, &r->trackMesh, r->whiteTexture);
    if (r->whiteTexture) SDL_ReleaseGPUTexture(r->device, r->whiteTexture);
    if (r->depthTexture) SDL_ReleaseGPUTexture(r->device, r->depthTexture);
    if (r->meshPipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->meshPipeline);
    if (r->meshPipelineNoDepth) SDL_ReleaseGPUGraphicsPipeline(r->device, r->meshPipelineNoDepth);
    SDL_ReleaseGPUBuffer(r->device, r->vertexBuffer);
    SDL_ReleaseGPUTransferBuffer(r->device, r->vertexTransferBuffer);
    SDL_ReleaseGPUSampler(r->device, r->sampler);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipeline);
    free(r);
}

//---------------------------------------------------------------------------
// Frame lifecycle
//---------------------------------------------------------------------------

void menu_render_gpu_begin_frame(MenuRendererGPU *r)
{
    r->vertexCount = 0;
    r->drawCommandCount = 0;
    r->meshDrawCount = 0;
    r->currentLayer = MENU_LAYER_BACKGROUND;

    r->cmdBuf = SDL_AcquireGPUCommandBuffer(r->device);
    if (!r->cmdBuf) return;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(r->cmdBuf, r->window,
            &r->swapchainTexture, &r->swapchainWidth, &r->swapchainHeight)
        || !r->swapchainTexture) {
        SDL_CancelGPUCommandBuffer(r->cmdBuf);
        r->cmdBuf = NULL;
        return;
    }
}

void menu_render_gpu_set_layer(MenuRendererGPU *r, MenuDrawLayer layer)
{
    r->currentLayer = layer;
}

void menu_render_gpu_end_frame(MenuRendererGPU *r)
{
    if (!r->cmdBuf) return;

    // Step fade and emit overlay quad (before copy pass so vertices are included)
    if (r->fadeActive) {
        r->fadeAlpha += r->fadeStep;
        if (r->fadeAlpha <= 0.0f) { r->fadeAlpha = 0.0f; r->fadeActive = 0; }
        if (r->fadeAlpha >= 1.0f) { r->fadeAlpha = 1.0f; r->fadeActive = 0; }
    }
    if (r->fadeAlpha > 0.001f && r->blackTexture) {
        EmitQuad(r, 0, 0, MENU_WIDTH, MENU_HEIGHT, 0, 0, 1, 1);
        RecordDraw(r, r->blackTexture, r->fadeAlpha, -1, NULL);
    }

    // Phase 1: Copy pass — upload all accumulated vertex data
    if (r->vertexCount > 0) {
        void *mapped = SDL_MapGPUTransferBuffer(r->device, r->vertexTransferBuffer, true);
        memcpy(mapped, r->vertices, r->vertexCount * sizeof(MenuVertex));
        SDL_UnmapGPUTransferBuffer(r->device, r->vertexTransferBuffer);

        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
        SDL_GPUTransferBufferLocation tbLoc = { .transfer_buffer = r->vertexTransferBuffer };
        SDL_GPUBufferRegion bufReg = { .buffer = r->vertexBuffer,
                                       .size = r->vertexCount * sizeof(MenuVertex) };
        SDL_UploadToGPUBuffer(cp, &tbLoc, &bufReg, false);
        SDL_EndGPUCopyPass(cp);
    }

    // Phase 2: Render pass — replay all recorded draw commands
    EnsureDepthTexture(r);

    SDL_GPUColorTargetInfo ct = {0};
    ct.texture = r->swapchainTexture;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = (SDL_FColor){0, 0, 0, 1};

    SDL_GPUDepthStencilTargetInfo dsi = {0};
    dsi.texture = r->depthTexture;
    dsi.load_op = SDL_GPU_LOADOP_CLEAR;
    dsi.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dsi.clear_depth = 1.0f;

    SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(r->cmdBuf, &ct, 1,
        r->depthTexture ? &dsi : NULL);

    // Set viewport for aspect-ratio preservation
    SDL_GPUViewport vp = {0};
    float wAsp = (float)r->swapchainWidth / (float)r->swapchainHeight;
    float mAsp = (float)MENU_WIDTH / (float)MENU_HEIGHT;
    if (wAsp > mAsp) {
        vp.h = (float)r->swapchainHeight;
        vp.w = mAsp * r->swapchainHeight;
        vp.x = (r->swapchainWidth - vp.w) / 2.0f;
    } else {
        vp.w = (float)r->swapchainWidth;
        vp.h = r->swapchainWidth / mAsp;
        vp.y = (r->swapchainHeight - vp.h) / 2.0f;
    }
    vp.max_depth = 1.0f;
    SDL_SetGPUViewport(renderPass, &vp);

    // Background 2D draws (behind 3D preview)
    ReplayDrawsLayer(r, renderPass, MENU_LAYER_BACKGROUND, MENU_LAYER_BACKGROUND);

    // 3D mesh draws (car/track previews)
    ReplayMeshDraws(r, renderPass);

    // Restore full viewport for foreground 2D draws
    SDL_SetGPUViewport(renderPass, &vp);

    // Foreground 2D draws (on top of 3D preview)
    ReplayDrawsLayer(r, renderPass, MENU_LAYER_FOREGROUND, MENU_LAYER_FOREGROUND);

    SDL_EndGPURenderPass(renderPass);
    SDL_SubmitGPUCommandBuffer(r->cmdBuf);
    r->cmdBuf = NULL;
}

//---------------------------------------------------------------------------
// Asset conversion
//---------------------------------------------------------------------------

int menu_render_gpu_load_blocks(MenuRendererGPU *r, int slot, tBlockHeader *blocks, const tColor *pal)
{
    if (slot < 0 || slot >= MAX_SLOTS || !blocks) return 0;
    menu_render_gpu_free_blocks(r, slot);

    // Count valid sub-blocks
    int count = 0;
    for (int i = 0; i < MAX_BLOCKS_PER_SLOT; i++) {
        if (blocks[i].iWidth <= 0 || blocks[i].iHeight <= 0 || blocks[i].iDataOffset <= 0)
            break;
        if (blocks[i].iWidth > MENU_WIDTH || blocks[i].iHeight > MENU_HEIGHT)
            break;
        count++;
    }

    if (count == 0) {
        // Full-screen background (raw pixels, no block headers)
        uint8 *rgba = malloc(MENU_WIDTH * MENU_HEIGHT * 4);
        IndexedToRGBA((uint8 *)blocks, pal, rgba, MENU_WIDTH * MENU_HEIGHT);
        r->backgroundTextures[slot] = UploadRGBA(r->device, rgba, MENU_WIDTH, MENU_HEIGHT);
        free(rgba);
        return 1;
    }

    r->slotTextures[slot] = calloc(count, sizeof(MenuTexture));
    r->slotTextureCount[slot] = count;

    for (int i = 0; i < count; i++) {
        int w = blocks[i].iWidth, h = blocks[i].iHeight;
        uint8 *src = (uint8 *)blocks + blocks[i].iDataOffset;
        uint8 *rgba = malloc(w * h * 4);
        IndexedToRGBA(src, pal, rgba, w * h);
        r->slotTextures[slot][i].texture = UploadRGBA(r->device, rgba, w, h);
        r->slotTextures[slot][i].width = w;
        r->slotTextures[slot][i].height = h;
        free(rgba);
    }
    return count;
}

void menu_render_gpu_free_blocks(MenuRendererGPU *r, int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS) return;
    if (r->slotTextures[slot]) {
        for (int i = 0; i < r->slotTextureCount[slot]; i++)
            if (r->slotTextures[slot][i].texture)
                SDL_ReleaseGPUTexture(r->device, r->slotTextures[slot][i].texture);
        free(r->slotTextures[slot]);
        r->slotTextures[slot] = NULL;
        r->slotTextureCount[slot] = 0;
    }
    if (r->backgroundTextures[slot]) {
        SDL_ReleaseGPUTexture(r->device, r->backgroundTextures[slot]);
        r->backgroundTextures[slot] = NULL;
    }
}

//---------------------------------------------------------------------------
// Draw calls
//---------------------------------------------------------------------------

void menu_render_gpu_clear(MenuRendererGPU *r, uint8 colorIndex, const tColor *pal)
{
    (void)r; (void)colorIndex; (void)pal;
    // Clear already done by render pass LOADOP_CLEAR (black).
}

void menu_render_gpu_background(MenuRendererGPU *r, int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS || !r->backgroundTextures[slot]) return;
    EmitQuad(r, 0, 0, MENU_WIDTH, MENU_HEIGHT, 0, 0, 1, 1);
    RecordDraw(r, r->backgroundTextures[slot], 1.0f, -1, NULL);
}

void menu_render_gpu_sprite(MenuRendererGPU *r, int slot, int blockIdx, int x, int y,
                        int transparentIdx, const tColor *pal)
{
    if (slot < 0 || slot >= MAX_SLOTS) return;
    if (!r->slotTextures[slot] || blockIdx >= r->slotTextureCount[slot]) return;
    MenuTexture *mt = &r->slotTextures[slot][blockIdx];
    if (!mt->texture) return;
    EmitQuad(r, (float)x, (float)y, (float)mt->width, (float)mt->height, 0, 0, 1, 1);
    RecordDraw(r, mt->texture, 1.0f, transparentIdx, pal);
}

//---------------------------------------------------------------------------
// Text rendering
//---------------------------------------------------------------------------

void menu_render_gpu_text(MenuRendererGPU *r, int fontSlot, const char *text,
                      const char *mappingTable, int *charVOffsets,
                      int x, int y, uint8 colorReplace, int alignment,
                      const tColor *pal)
{
    if (!text || fontSlot < 0 || fontSlot >= MAX_SLOTS || !r->slotTextures[fontSlot])
        return;

    // Pass 1: measure width
    int totalWidth = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') continue;
        uint8 idx = mappingTable[(uint8)*p];
        if (idx == 0xFF) { totalWidth += 8; continue; }
        if (idx < r->slotTextureCount[fontSlot])
            totalWidth += r->slotTextures[fontSlot][idx].width + 1;
    }
    if (totalWidth > 0) totalWidth--;

    // Apply alignment
    int curX = x;
    if (alignment == 1) curX = x - totalWidth / 2;
    else if (alignment == 2) curX = x - totalWidth;

    // Pass 2: render glyphs
    for (const char *p = text; *p; p++) {
        if (*p == '\n') continue;
        uint8 idx = mappingTable[(uint8)*p];
        if (idx == 0xFF) { curX += 8; continue; }
        if (idx >= r->slotTextureCount[fontSlot]) continue;

        MenuTexture *g = &r->slotTextures[fontSlot][idx];
        if (!g->texture) continue;

        int cy = y + (charVOffsets ? charVOffsets[idx] : 0);

        EmitQuad(r, (float)curX, (float)cy, (float)g->width, (float)g->height,
                 0, 0, 1, 1);
        RecordDrawWithColorReplace(r, g->texture, 1.0f, 0x8F, colorReplace, pal);

        curX += g->width + 1;
    }
}

void menu_render_gpu_scaled_text(MenuRendererGPU *r, int fontSlot, const char *text,
                             const char *mappingTable, int *charVOffsets,
                             int x, int y, uint8 colorReplace,
                             unsigned int alignment, int clipLeft, int clipRight,
                             const tColor *pal)
{
    if (!text || fontSlot < 0 || fontSlot >= MAX_SLOTS || !r->slotTextures[fontSlot])
        return;

    // Measure unscaled width
    int totalWidth = 0;
    for (const char *p = text; *p; p++) {
        uint8 idx = (uint8)mappingTable[(uint8)*p];
        if (idx < r->slotTextureCount[fontSlot])
            totalWidth += r->slotTextures[fontSlot][idx].width + 1;
    }
    if (totalWidth > 0) totalWidth--;

    // Scale factor
    int avail = clipRight - clipLeft;
    float scale = 1.0f;
    if (totalWidth > avail && avail > 0)
        scale = (float)avail / (float)totalWidth;

    float scaledWidth = totalWidth * scale;

    // Alignment
    float startX = (float)x;
    if (alignment == 1) startX = x - scaledWidth / 2.0f;
    else if (alignment == 2) startX = x - scaledWidth;

    // Render scaled glyphs
    float curX = startX;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') continue;
        uint8 idx = (uint8)mappingTable[(uint8)*p];
        if (idx == 0xFF) { curX += 8 * scale; continue; }
        if (idx >= r->slotTextureCount[fontSlot]) continue;

        MenuTexture *g = &r->slotTextures[fontSlot][idx];
        if (!g->texture) { curX += scale; continue; }

        float cw = g->width * scale;
        float ch = g->height * scale;
        int cy = y + (charVOffsets ? charVOffsets[idx] : 0);

        if (curX + cw >= clipLeft && curX <= clipRight) {
            EmitQuad(r, curX, (float)cy, cw, ch, 0, 0, 1, 1);
            RecordDrawWithColorReplace(r, g->texture, 1.0f, 0x8F,
                                       (uint8)colorReplace, pal);
        }

        curX += cw + scale;
    }
}

//---------------------------------------------------------------------------
// Fade system
//---------------------------------------------------------------------------

void menu_render_gpu_begin_fade(MenuRendererGPU *r, int direction, int durationFrames)
{
    if (durationFrames <= 0) durationFrames = 32;
    if (direction == 0) {
        // Fade out: overlay goes from transparent to opaque
        r->fadeStep = 1.0f / (float)durationFrames;
    } else {
        // Fade in: overlay goes from opaque to transparent
        r->fadeAlpha = 1.0f;
        r->fadeStep = -1.0f / (float)durationFrames;
    }
    r->fadeActive = 1;
}

int menu_render_gpu_fade_active(MenuRendererGPU *r) { return r->fadeActive; }

void menu_render_gpu_fade_wait(MenuRendererGPU *r, void (*redraw_fn)(void *ctx), void *ctx)
{
    while (menu_render_gpu_fade_active(r)) {
        menu_render_gpu_begin_frame(r);
        if (redraw_fn) redraw_fn(ctx);
        menu_render_gpu_end_frame(r);
    }
}

//---------------------------------------------------------------------------
// 4x4 matrix helpers (column-major, matching GPU convention)
// Column-major: m[col*4 + row], i.e. m[0..3]=col0, m[4..7]=col1, etc.
//---------------------------------------------------------------------------

static void Mat4Multiply(float *out, const float *a, const float *b)
{
    float tmp[16];
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            tmp[c * 4 + r] =
                a[0 * 4 + r] * b[c * 4 + 0] +
                a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] +
                a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    memcpy(out, tmp, sizeof(tmp));
}

static void MakePerspective(float *m, float fovY, float aspect, float zNear, float zFar)
{
    memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / tanf(fovY * 0.5f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = zFar / (zNear - zFar);
    m[11] = -1.0f;
    m[14] = (zNear * zFar) / (zNear - zFar);
}

static void MakeLookAt(float *m, float eyeX, float eyeY, float eyeZ,
                        float atX, float atY, float atZ)
{
    // Forward = normalize(at - eye)
    float fx = atX - eyeX, fy = atY - eyeY, fz = atZ - eyeZ;
    float flen = sqrtf(fx*fx + fy*fy + fz*fz);
    if (flen < 1e-6f) flen = 1.0f;
    fx /= flen; fy /= flen; fz /= flen;

    // Right = normalize(cross(forward, up))  where up = (0, 1, 0)
    // cross(f, (0,1,0)) = (-fz, 0, fx)
    float rx = -fz, ry = 0.0f, rz = fx;
    float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
    if (rlen < 1e-6f) rlen = 1.0f;
    rx /= rlen; ry /= rlen; rz /= rlen;

    // True up = cross(right, forward)
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    // Column-major view matrix
    m[0] = rx;   m[4] = ry;   m[8]  = rz;   m[12] = -(rx*eyeX + ry*eyeY + rz*eyeZ);
    m[1] = ux;   m[5] = uy;   m[9]  = uz;   m[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    m[2] = -fx;  m[6] = -fy;  m[10] = -fz;  m[14] =  (fx*eyeX + fy*eyeY + fz*eyeZ);
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
}

static void FreeMeshPreview(SDL_GPUDevice *dev, MeshPreview *mp, SDL_GPUTexture *whiteTexture)
{
    if (mp->vertexBuffer) { SDL_ReleaseGPUBuffer(dev, mp->vertexBuffer); mp->vertexBuffer = NULL; }
    if (mp->indexBuffer)  { SDL_ReleaseGPUBuffer(dev, mp->indexBuffer);  mp->indexBuffer  = NULL; }
    // Don't release whiteTexture — it's shared
    if (mp->texture && mp->texture != whiteTexture)
        SDL_ReleaseGPUTexture(dev, mp->texture);
    mp->texture = NULL;
    mp->indexCount = 0;
    mp->loaded = false;
}

//---------------------------------------------------------------------------
// 3D mesh previews — car
//---------------------------------------------------------------------------

// Build RGBA texture atlas from car's indexed-color tile data.
// Layout: 256px wide (4 tiles per row), with one extra white row at bottom.
static SDL_GPUTexture *BuildCarTextureAtlas(MenuRendererGPU *r, int carIdx,
                                            const tColor *pal,
                                            int *outNumTiles, int *outAtlasH)
{
    int texSlot = car_texmap[carIdx];
    if (texSlot <= 0) return NULL;

    uint8 *texData = cartex_vga[texSlot - 1];
    if (!texData) return NULL;

    int nTiles = num_textures[texSlot - 1];
    if (nTiles <= 0) return NULL;

    int numRows = (nTiles + 3) / 4;
    int atlasW = 256;
    int atlasH = numRows * 64 + 1; // +1 row for white pixel

    uint8 *rgba = calloc((size_t)atlasW * atlasH * 4, 1);
    if (!rgba) return NULL;

    // Convert sorted indexed-color tiles (4-per-row, 256px wide) to RGBA
    for (int t = 0; t < nTiles; t++) {
        int col = t % 4;
        int row = t / 4;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int srcOff = (row * 64 + y) * 256 + col * 64 + x;
                uint8 palIdx = texData[srcOff];
                int ax = col * 64 + x;
                int ay = row * 64 + y;
                int dstOff = (ay * atlasW + ax) * 4;
                const tColor *c = &pal[palIdx];
                rgba[dstOff + 0] = (uint8)(c->byR * 255 / 63);
                rgba[dstOff + 1] = (uint8)(c->byG * 255 / 63);
                rgba[dstOff + 2] = (uint8)(c->byB * 255 / 63);
                rgba[dstOff + 3] = palIdx ? 255 : 0;
            }
        }
    }

    // Fill bottom row with white (flat-colored polygon UVs sample here)
    for (int x = 0; x < atlasW; x++) {
        int off = ((atlasH - 1) * atlasW + x) * 4;
        rgba[off] = rgba[off + 1] = rgba[off + 2] = rgba[off + 3] = 255;
    }

    SDL_GPUTexture *tex = UploadRGBA(r->device, rgba, atlasW, atlasH);
    free(rgba);

    *outNumTiles = nTiles;
    *outAtlasH = atlasH;
    return tex;
}

void menu_render_gpu_load_car_mesh(MenuRendererGPU *r, int carIdx, const tColor *pal)
{
    if (r->loadedCarIdx == carIdx && r->carMesh.loaded) return;
    menu_render_gpu_free_car_mesh(r);

    if (carIdx < 0 || carIdx > CAR_DESIGN_DEATH) return;

    tCarDesign *design = &CarDesigns[carIdx];
    int numPols = design->byNumPols;
    int numCoords = design->byNumCoords;
    tVec3 *coords = design->pCoords;
    tPolygon *pols = design->pPols;
    tAnimation *pAnms = design->pAnms;
    if (!coords || !pols || numPols == 0) return;

    // Build texture atlas from car texture data
    int numTiles = 0, atlasH = 0;
    SDL_GPUTexture *atlas = BuildCarTextureAtlas(r, carIdx, pal,
                                                 &numTiles, &atlasH);
    bool hasAtlas = (atlas != NULL && numTiles > 0);
    float fAtlasH = hasAtlas ? (float)atlasH : 1.0f;

    // White pixel UV (center of bottom row, for flat-colored polygons)
    float whiteU = hasAtlas ? 0.5f / 256.0f : 0.0f;
    float whiteV = hasAtlas ? (atlasH - 0.5f) / fAtlasH : 0.0f;

    // Each quad becomes 4 verts + 6 indices (2 triangles)
    MeshVertex *vertices = calloc(numPols * 4, sizeof(MeshVertex));
    uint32 *indices = calloc(numPols * 6, sizeof(uint32));
    int vertCount = 0, idxCount = 0;

    tCarColorRemap *remap = &car_flat_remap[carIdx];

    for (int p = 0; p < numPols; p++) {
        uint32 tex = pols[p].uiTex;
        if (tex & SURFACE_FLAG_SKIP_RENDER) continue;

        // Resolve animation texture lookups (use frame 0 for menu preview)
        if ((tex & CAR_FLAG_ANMS_LOOKUP) && pAnms) {
            tex = pAnms[(uint8)tex].framesAy[0];
        }

        bool isTextured = hasAtlas && (tex & SURFACE_FLAG_APPLY_TEXTURE) &&
                          (uint8)tex < numTiles;

        float u0, u1, v0, v1;
        float cr, cg, cb, ca;

        if (isTextured) {
            // Textured polygon — compute UV rect for tile in atlas
            uint8 tileIdx = (uint8)tex;
            int col = tileIdx % 4;
            int row = tileIdx / 4;
            u0 = (col * 64.0f) / 256.0f;
            u1 = ((col + 1) * 64.0f) / 256.0f;
            v0 = (row * 64.0f) / fAtlasH;
            v1 = ((row + 1) * 64.0f) / fAtlasH;

            if (tex & SURFACE_FLAG_FLIP_HORIZ) {
                float tmp = u0; u0 = u1; u1 = tmp;
            }
            if (tex & SURFACE_FLAG_FLIP_VERT) {
                float tmp = v0; v0 = v1; v1 = tmp;
            }

            // Vertex color = white (texture provides all color)
            cr = cg = cb = ca = 1.0f;
        } else {
            // Flat-colored polygon — palette color via vertex color
            uint8 colorIdx = (uint8)tex;
            if (!(tex & SURFACE_FLAG_APPLY_TEXTURE) &&
                remap->uiColorFrom != 0xFFFFFFFF &&
                colorIdx == (uint8)remap->uiColorFrom)
                colorIdx = (uint8)remap->uiColorTo;

            const tColor *c = &pal[colorIdx];
            cr = (c->byR * 255.0f / 63.0f) / 255.0f;
            cg = (c->byG * 255.0f / 63.0f) / 255.0f;
            cb = (c->byB * 255.0f / 63.0f) / 255.0f;
            ca = 1.0f;

            // UV points to white pixel in atlas
            u0 = u1 = whiteU;
            v0 = v1 = whiteV;
        }

        int baseVert = vertCount;

        // UV mapping matches game's startsx/startsy defaults:
        // v0 → (right, top), v1 → (left, top),
        // v2 → (left, bottom), v3 → (right, bottom)
        float uvs[4][2] = {
            { u1, v0 }, { u0, v0 }, { u0, v1 }, { u1, v1 }
        };

        for (int v = 0; v < 4; v++) {
            uint8 vi = pols[p].verts[v];
            if (vi >= numCoords) vi = 0;

            MeshVertex *mv = &vertices[vertCount++];
            // Game uses Z-up (X=forward, Y=lateral, Z=up);
            // GPU pipeline uses Y-up (X=right, Y=up, Z=depth)
            mv->position[0] = coords[vi].fY;   // GPU X = model lateral
            mv->position[1] = coords[vi].fZ;   // GPU Y = model up
            mv->position[2] = coords[vi].fX;   // GPU Z = model forward
            mv->uv[0] = uvs[v][0];
            mv->uv[1] = uvs[v][1];
            mv->color[0] = cr;
            mv->color[1] = cg;
            mv->color[2] = cb;
            mv->color[3] = ca;
        }

        // Two triangles: (0,1,2) and (0,2,3)
        indices[idxCount++] = baseVert + 0;
        indices[idxCount++] = baseVert + 1;
        indices[idxCount++] = baseVert + 2;
        indices[idxCount++] = baseVert + 0;
        indices[idxCount++] = baseVert + 2;
        indices[idxCount++] = baseVert + 3;
    }

    if (idxCount > 0) {
        r->carMesh.vertexBuffer = UploadGPUBuffer(r->device,
            SDL_GPU_BUFFERUSAGE_VERTEX, vertices, vertCount * sizeof(MeshVertex));
        r->carMesh.indexBuffer = UploadGPUBuffer(r->device,
            SDL_GPU_BUFFERUSAGE_INDEX, indices, idxCount * sizeof(uint32));
        r->carMesh.texture = atlas ? atlas : r->whiteTexture;
        r->carMesh.indexCount = idxCount;
        r->carMesh.loaded = true;
        r->loadedCarIdx = carIdx;
    } else if (atlas) {
        SDL_ReleaseGPUTexture(r->device, atlas);
    }

    free(vertices);
    free(indices);
}

void menu_render_gpu_free_car_mesh(MenuRendererGPU *r)
{
    FreeMeshPreview(r->device, &r->carMesh, r->whiteTexture);
    r->loadedCarIdx = -1;
}

void menu_render_gpu_draw_car_preview(MenuRendererGPU *r, float angle, float distance,
                                  int carYaw,
                                  int destX, int destY, int destW, int destH)
{
    if (!r->carMesh.loaded || r->meshDrawCount >= MAX_MESH_DRAWS) return;

    // Convert TRIG angle (0-16383) to radians
    float rad = angle * (2.0f * 3.14159265f / 16384.0f);

    // Game uses Z-up: camera at world(-dist*cos, 0, dist*sin)
    // After Z-up to Y-up conversion: X=lateral, Y=up, Z=forward
    float eyeX = 0.0f;                       // world Y (lateral) = 0
    float eyeY = distance * sinf(rad);        // world Z (up) = dist*sin
    float eyeZ = -distance * cosf(rad);       // world X (forward) = -dist*cos

    // Build model rotation from car yaw (rotation around Y axis in GPU space)
    // Negate to match game's rotation convention (original DrawCar rotates
    // X' = cos*X - sin*Y; column-major layout inverts without negation)
    float yawRad = -(float)carYaw * (2.0f * 3.14159265f / 16384.0f);
    float cy = cosf(yawRad), sy = sinf(yawRad);
    float model[16] = {
         cy,  0.0f, sy,  0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        -sy,  0.0f, cy,  0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    float view[16], proj[16], mv[16], mvp[16];
    MakeLookAt(view, eyeX, eyeY, eyeZ, 0.0f, 0.0f, 0.0f);
    float aspect = (float)destW / (float)destH;
    float fov = 2.0f * atanf(81.0f / (float)VIEWDIST);
    MakePerspective(proj, fov, aspect, 1.0f, distance * 4.0f);
    Mat4Multiply(mv, view, model);      // view * model
    Mat4Multiply(mvp, proj, mv);        // proj * view * model

    // Expand viewport to full width to avoid clipping, compensate in MVP
    int padLeft = destX;
    int padRight = MENU_WIDTH - (destX + destW);
    int totalW = destW + padLeft + padRight;
    float S = (float)destW / (float)totalW;
    float T = (float)(padLeft - padRight) / (float)totalW;
    for (int j = 0; j < 4; j++)
        mvp[j * 4] = S * mvp[j * 4] + T * mvp[j * 4 + 3];

    MeshDrawCommand *cmd = &r->meshDraws[r->meshDrawCount++];
    cmd->vertexBuffer = r->carMesh.vertexBuffer;
    cmd->indexBuffer = r->carMesh.indexBuffer;
    cmd->texture = r->carMesh.texture;
    cmd->indexCount = r->carMesh.indexCount;
    memcpy(cmd->mvp, mvp, sizeof(mvp));
    cmd->vpX = (float)(destX - padLeft);
    cmd->vpY = (float)destY;
    cmd->vpW = (float)totalW;
    cmd->vpH = (float)destH;
    cmd->useDepth = true;
}

//---------------------------------------------------------------------------
// 3D mesh previews — track map
//---------------------------------------------------------------------------

void menu_render_gpu_load_track_mesh(MenuRendererGPU *r, const tColor *pal)
{
    menu_render_gpu_free_track_mesh(r);

    extern int TRAK_LEN;
    if (TRAK_LEN <= 0) return;

    // Compute track center, bounding box, and height range (matching original show_3dmap)
    float accX = 0, accY = 0, accZ = 0;
    float bbMinX = 1e18f, bbMinY = 1e18f, bbMinZ = 1e18f;
    float bbMaxX = -1e18f, bbMaxY = -1e18f, bbMaxZ = -1e18f;
    float minZ = 1e18f, maxZ = -1e18f;
    for (int i = 0; i < TRAK_LEN; i++) {
        accX += TrakPt[i].pointAy[0].fX + TrakPt[i].pointAy[4].fX;
        accY += TrakPt[i].pointAy[0].fY + TrakPt[i].pointAy[4].fY;
        accZ += TrakPt[i].pointAy[0].fZ + TrakPt[i].pointAy[4].fZ;
        for (int p = 0; p <= 4; p += 4) {
            float px = TrakPt[i].pointAy[p].fX;
            float py = TrakPt[i].pointAy[p].fY;
            float pz = TrakPt[i].pointAy[p].fZ;
            if (px < bbMinX) bbMinX = px; if (px > bbMaxX) bbMaxX = px;
            if (py < bbMinY) bbMinY = py; if (py > bbMaxY) bbMaxY = py;
            if (pz < bbMinZ) bbMinZ = pz; if (pz > bbMaxZ) bbMaxZ = pz;
        }
        if (TrakPt[i].pointAy[2].fZ < minZ) minZ = TrakPt[i].pointAy[2].fZ;
        if (TrakPt[i].pointAy[2].fZ > maxZ) maxZ = TrakPt[i].pointAy[2].fZ;
    }
    float div = 1.0f / (float)(2 * TRAK_LEN);
    float cenX = -accX * div;
    float cenY = -accY * div;
    float cenZ = -accZ * div;
    float zRange = maxZ - minZ;
    if (zRange < 1.0f) zRange = 1.0f;

    // Expanded bounding box with 10% margin (matching original)
    float expandX = (bbMaxX - bbMinX) * 0.1f;
    float expandY = (bbMaxY - bbMinY) * 0.1f;
    float floorMinX = bbMinX - expandX;
    float floorMaxX = bbMaxX + expandX;
    float floorMinY = bbMinY - expandY;
    float floorMaxY = bbMaxY + expandY;
    float bbZRange = bbMaxZ - bbMinZ;
    if (bbZRange < 100.0f) bbZRange = 100.0f;
    float floorZ = bbMinZ - bbZRange * 0.4f;

    // One quad per 2-chunk segment + 1 floor quad
    int numSegments = TRAK_LEN / 2;
    MeshVertex *vertices = calloc(numSegments * 4 + 4, sizeof(MeshVertex));
    uint32 *indices = calloc(numSegments * 6 + 6, sizeof(uint32));
    int vertCount = 0, idxCount = 0;

    // Floor quad: semi-transparent plane at bottom of bounding box (color index 2)
    {
        float fr = 0.0f, fg = 0.0f, fb = 0.0f;
        float floorCorners[4][3] = {
            { floorMinY + cenY, floorZ + cenZ, floorMinX + cenX },
            { floorMaxY + cenY, floorZ + cenZ, floorMinX + cenX },
            { floorMaxY + cenY, floorZ + cenZ, floorMaxX + cenX },
            { floorMinY + cenY, floorZ + cenZ, floorMaxX + cenX },
        };
        for (int v = 0; v < 4; v++) {
            MeshVertex *mv = &vertices[vertCount++];
            mv->position[0] = floorCorners[v][0];
            mv->position[1] = floorCorners[v][1];
            mv->position[2] = floorCorners[v][2];
            mv->uv[0] = 0.0f; mv->uv[1] = 0.0f;
            mv->color[0] = fr; mv->color[1] = fg; mv->color[2] = fb;
            mv->color[3] = 0.5f;
        }
        indices[idxCount++] = 0; indices[idxCount++] = 2; indices[idxCount++] = 1;
        indices[idxCount++] = 0; indices[idxCount++] = 3; indices[idxCount++] = 2;
    }

    for (int seg = 0; seg < numSegments - 1; seg++) {
        int chunk = seg * 2;
        int nextChunk = (seg + 1) * 2;
        if (nextChunk >= TRAK_LEN) break;

        // Height-based color gradient (palette 128-139, matching original)
        float heightCalc = (maxZ - TrakPt[chunk].pointAy[2].fZ) * 15.0f / zRange;
        int colorIdx = 143 - (int)heightCalc;
        if (colorIdx > 139) colorIdx = 139;
        if (colorIdx < 128) colorIdx = 128;

        const tColor *c = &pal[colorIdx];
        float cr = c->byR / 63.0f;
        float cg = c->byG / 63.0f;
        float cb = c->byB / 63.0f;

        // Quad spanning full track width: pointAy[4] to pointAy[0]
        // Vertex order matches original: next[4], next[0], cur[0], cur[4]
        tVec3 *pts[4] = {
            &TrakPt[nextChunk].pointAy[4],
            &TrakPt[nextChunk].pointAy[0],
            &TrakPt[chunk].pointAy[0],
            &TrakPt[chunk].pointAy[4],
        };

        int baseVert = vertCount;
        for (int v = 0; v < 4; v++) {
            MeshVertex *mv = &vertices[vertCount++];
            // Apply centering + Z-up to Y-up swap
            float gx = pts[v]->fX + cenX;
            float gy = pts[v]->fY + cenY;
            float gz = pts[v]->fZ + cenZ;
            mv->position[0] = gy;   // GPU X = game Y (lateral)
            mv->position[1] = gz;   // GPU Y = game Z (up)
            mv->position[2] = gx;   // GPU Z = game X (forward)
            mv->uv[0] = 0.0f;
            mv->uv[1] = 0.0f;
            mv->color[0] = cr;
            mv->color[1] = cg;
            mv->color[2] = cb;
            mv->color[3] = 1.0f;
        }

        indices[idxCount++] = baseVert + 0;
        indices[idxCount++] = baseVert + 1;
        indices[idxCount++] = baseVert + 2;
        indices[idxCount++] = baseVert + 0;
        indices[idxCount++] = baseVert + 2;
        indices[idxCount++] = baseVert + 3;
    }

    if (idxCount > 0) {
        r->trackMesh.vertexBuffer = UploadGPUBuffer(r->device,
            SDL_GPU_BUFFERUSAGE_VERTEX, vertices, vertCount * sizeof(MeshVertex));
        r->trackMesh.indexBuffer = UploadGPUBuffer(r->device,
            SDL_GPU_BUFFERUSAGE_INDEX, indices, idxCount * sizeof(uint32));
        r->trackMesh.texture = r->whiteTexture;
        r->trackMesh.indexCount = idxCount;
        r->trackMesh.loaded = true;
    }

    free(vertices);
    free(indices);
}

void menu_render_gpu_free_track_mesh(MenuRendererGPU *r)
{
    FreeMeshPreview(r->device, &r->trackMesh, r->whiteTexture);
}

void menu_render_gpu_draw_track_preview(MenuRendererGPU *r, float cameraZ,
                                    int elevation, int yaw,
                                    int destX, int destY, int destW, int destH)
{
    if (!r->trackMesh.loaded || r->meshDrawCount >= MAX_MESH_DRAWS) return;

    // Convert TRIG angles to radians
    float yawRad = (float)yaw * (2.0f * 3.14159265f / 16384.0f);
    float elevRad = (float)elevation * (2.0f * 3.14159265f / 16384.0f);

    // Camera position: match original show_3dmap
    // Original: worldx = -fZ*tcos[elev], worldy = 0, worldz = fZ*tsin[elev]
    // Game→GPU: GPU_X = game_Y, GPU_Y = game_Z, GPU_Z = game_X
    // Camera orbit by +yaw with -Z forward matches original's vertex rotation
    float camDist = cameraZ;
    float horizDist = camDist * cosf(elevRad);
    float eyeX = horizDist * sinf(yawRad);
    float eyeY = camDist * sinf(elevRad);
    float eyeZ = -horizDist * cosf(yawRad);

    float view[16], proj[16], mvp[16];
    MakeLookAt(view, eyeX, eyeY, eyeZ, 0.0f, 0.0f, 0.0f);
    float aspect = (float)destW / (float)destH;
    float fov = 2.0f * atanf(81.0f / (float)VIEWDIST);
    MakePerspective(proj, fov, aspect, 1.0f, camDist * 8.0f);
    Mat4Multiply(mvp, proj, view);

    // Expand viewport to full width to avoid clipping, compensate in MVP
    int padLeft = destX;
    int padRight = MENU_WIDTH - (destX + destW);
    int totalW = destW + padLeft + padRight;
    float S = (float)destW / (float)totalW;
    float T = (float)(padLeft - padRight) / (float)totalW;
    for (int j = 0; j < 4; j++)
        mvp[j * 4] = S * mvp[j * 4] + T * mvp[j * 4 + 3];

    MeshDrawCommand *cmd = &r->meshDraws[r->meshDrawCount++];
    cmd->vertexBuffer = r->trackMesh.vertexBuffer;
    cmd->indexBuffer = r->trackMesh.indexBuffer;
    cmd->texture = r->trackMesh.texture;
    cmd->indexCount = r->trackMesh.indexCount;
    memcpy(cmd->mvp, mvp, sizeof(mvp));
    cmd->vpX = (float)(destX - padLeft);
    cmd->vpY = (float)destY;
    cmd->vpW = (float)totalW;
    cmd->vpH = (float)destH;
    cmd->useDepth = false;
}
