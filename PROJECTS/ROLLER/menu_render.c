#include "menu_render.h"
#include "menu_shaders.h"
#include <string.h>
#include <stdlib.h>

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
    PixelUniforms uniforms;
} DrawCommand;

#define MAX_DRAW_COMMANDS 512

struct MenuRenderer {
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

static void EmitQuad(MenuRenderer *r, float x, float y, float w, float h,
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

static void RecordDraw(MenuRenderer *r, SDL_GPUTexture *texture,
                       float alphaMul, int transparentIdx, const tColor *pal)
{
    if (r->vertexCount == 0 || r->drawCommandCount >= MAX_DRAW_COMMANDS) return;

    DrawCommand *cmd = &r->drawCommands[r->drawCommandCount++];
    cmd->texture = texture;
    cmd->vertexOffset = r->vertexCount - 6; // last EmitQuad wrote 6 verts
    cmd->vertexCount = 6;
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

static void RecordDrawWithColorReplace(MenuRenderer *r, SDL_GPUTexture *texture,
                                       float alphaMul, uint8 fromIdx, uint8 toIdx,
                                       const tColor *pal)
{
    if (r->vertexCount == 0 || r->drawCommandCount >= MAX_DRAW_COMMANDS) return;

    DrawCommand *cmd = &r->drawCommands[r->drawCommandCount++];
    cmd->texture = texture;
    cmd->vertexOffset = r->vertexCount - 6;
    cmd->vertexCount = 6;
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

static void ReplayDraws(MenuRenderer *r, SDL_GPURenderPass *renderPass)
{
    float proj[16];
    MakeOrthoProjection(proj, 0, MENU_WIDTH, MENU_HEIGHT, 0);

    SDL_BindGPUGraphicsPipeline(renderPass, r->pipeline);
    SDL_PushGPUVertexUniformData(r->cmdBuf, 0, proj, sizeof(proj));

    SDL_GPUBufferBinding vbb = { .buffer = r->vertexBuffer };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vbb, 1);

    for (int i = 0; i < r->drawCommandCount; i++) {
        DrawCommand *cmd = &r->drawCommands[i];

        SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &cmd->uniforms, sizeof(cmd->uniforms));

        SDL_GPUTextureSamplerBinding tsb = { .texture = cmd->texture, .sampler = r->sampler };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &tsb, 1);

        SDL_DrawGPUPrimitives(renderPass, cmd->vertexCount, 1, cmd->vertexOffset, 0);
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

MenuRenderer *menu_render_create(SDL_GPUDevice *device, SDL_Window *window)
{
    MenuRenderer *r = calloc(1, sizeof(MenuRenderer));
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

    r->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeInfo);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!r->pipeline) {
        SDL_Log("Failed to create menu pipeline: %s", SDL_GetError());
        free(r);
        return NULL;
    }

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

    return r;
}

void menu_render_destroy(MenuRenderer *r)
{
    if (!r) return;
    for (int i = 0; i < MAX_SLOTS; i++)
        menu_render_free_blocks(r, i);
    if (r->blackTexture) SDL_ReleaseGPUTexture(r->device, r->blackTexture);
    SDL_ReleaseGPUBuffer(r->device, r->vertexBuffer);
    SDL_ReleaseGPUTransferBuffer(r->device, r->vertexTransferBuffer);
    SDL_ReleaseGPUSampler(r->device, r->sampler);
    SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipeline);
    free(r);
}

//---------------------------------------------------------------------------
// Frame lifecycle
//---------------------------------------------------------------------------

void menu_render_begin_frame(MenuRenderer *r)
{
    r->vertexCount = 0;
    r->drawCommandCount = 0;

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

void menu_render_end_frame(MenuRenderer *r)
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
    SDL_GPUColorTargetInfo ct = {0};
    ct.texture = r->swapchainTexture;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = (SDL_FColor){0, 0, 0, 1};

    SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(r->cmdBuf, &ct, 1, NULL);

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

    // Replay all draw commands (including fade overlay if active)
    ReplayDraws(r, renderPass);

    SDL_EndGPURenderPass(renderPass);
    SDL_SubmitGPUCommandBuffer(r->cmdBuf);
    r->cmdBuf = NULL;
}

//---------------------------------------------------------------------------
// Asset conversion
//---------------------------------------------------------------------------

int menu_render_load_blocks(MenuRenderer *r, int slot, tBlockHeader *blocks, const tColor *pal)
{
    if (slot < 0 || slot >= MAX_SLOTS || !blocks) return 0;
    menu_render_free_blocks(r, slot);

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

void menu_render_free_blocks(MenuRenderer *r, int slot)
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

void menu_render_clear(MenuRenderer *r, uint8 colorIndex, const tColor *pal)
{
    (void)r; (void)colorIndex; (void)pal;
    // Clear already done by render pass LOADOP_CLEAR (black).
}

void menu_render_background(MenuRenderer *r, int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS || !r->backgroundTextures[slot]) return;
    EmitQuad(r, 0, 0, MENU_WIDTH, MENU_HEIGHT, 0, 0, 1, 1);
    RecordDraw(r, r->backgroundTextures[slot], 1.0f, -1, NULL);
}

void menu_render_sprite(MenuRenderer *r, int slot, int blockIdx, int x, int y,
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

void menu_render_text(MenuRenderer *r, int fontSlot, const char *text,
                      const uint8 *mappingTable, int *charVOffsets,
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

void menu_render_scaled_text(MenuRenderer *r, int fontSlot, const char *text,
                             const char *mappingTable, int *charVOffsets,
                             int x, int y, char colorReplace,
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

void menu_render_begin_fade(MenuRenderer *r, int direction, int durationFrames)
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

int menu_render_fade_active(MenuRenderer *r) { return r->fadeActive; }

void menu_render_fade_wait(MenuRenderer *r, void (*redraw_fn)(void *ctx), void *ctx)
{
    while (menu_render_fade_active(r)) {
        menu_render_begin_frame(r);
        if (redraw_fn) redraw_fn(ctx);
        menu_render_end_frame(r);
    }
}
