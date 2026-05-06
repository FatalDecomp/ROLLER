# PRD: Deepen the `game_render` interface for a real 3D GPU backend

## Problem Statement

The `feat/game-render` branch introduces a rendering abstraction layer with a software-only backend. The current interface routes all game rendering through a facade — which is the right architecture — but the interface was designed at the **screen-space level** for polygon quads. A GPU backend doing real 3D rendering (vertex shaders, world-space transforms) cannot use screen-space primitives without rendering itself to a 2D blitter — losing most of the GPU's value.

The same interface also has mixed abstraction levels: `game_render_quad` takes screen-space `{x, y}` coordinates while `game_render_draw_car` takes world-space `{x, y, z}` transforms. The software backend ignores the car transform parameters entirely, reading state from globals instead — making the interface misleading about what it actually does. There are also five separate but mostly no-op "load" functions per asset type (track textures, car meshes, horizon, blocks) when one unified texture-loading path would suffice.

As the codebase follows the `menu_render` pattern (which already has both software and GPU backends on `master`), the `game_render` interface should be reshaped now to match the abstraction level a GPU backend expects, rather than freezing a screen-space interface that would need to change later — touching every call site again.

## Solution

Deepen the `game_render` interface into two clear seams:

- **Scene** — world-space 3D geometry submission (quads, cars, sky), each backend handles its own projection/rasterization pipeline
- **Overlay** — screen-space 2D elements (HUD sprites, print_block), which both backends naturally handle at screen resolution

Introduce three new backend-agnostic types (`GameRenderVertex`, `GameRenderCamera`, `TextureHandle`) that let callers submit geometry and resources without knowing backend internals. Move `subdivide` (the 2000-line software rasterizer) fully inside the software backend as an implementation detail. Change `calculateview()` into a function that produces a `GameRenderCamera` struct the renderer consumes, rather than scattering its results across globals.

## User Stories

### Core interface

1. As a **game loop author**, I want to submit a world-space quad with four vertices and a texture handle to the renderer, so that the backend can decide how to project and rasterize it without me needing to know whether it runs on CPU or GPU.

2. As a **software backend maintainer**, I want `subdivide` and `POLYTEX`/`POLYFLAT` to live entirely inside my module, so that I can optimize or replace them without affecting callers or the GPU backend.

3. As a **GPU backend author**, I want to receive world-space vertices and a camera matrix from the renderer interface, so that I can upload them to vertex buffers and draw with a vertex shader — rather than receiving screen-space quads that have already been projected.

4. As a **caller of game_render_draw_car**, I want the position and orientation I pass to be what actually gets rendered, so that I can trust the interface to mean what it says rather than having the backend silently read global `Car[]` array state.

5. As a **caller rendering HUD sprites**, I want a dedicated screen-space 2D path that doesn't intersect with the 3D camera pipeline, so that I don't accidentally think HUD elements need world-space coordinates.

### Camera

6. As a **game loop author**, I want to compute the camera once and pass a `GameRenderCamera` struct to the renderer, so that the renderer doesn't need to know about chase cam vs external view vs replay — it just consumes the result.

7. As a **software backend**, I want to receive a `GameRenderCamera` struct and unpack it into the `viewx/viewy/viewz` and `fcos`/`fsin` globals that the existing software pipeline expects, so that `calculateview`'s math stays on the game-loop side and the backend only does mechanical translation.

8. As a **GPU backend**, I want to receive a `GameRenderCamera` struct and upload the view-projection matrix as a shader uniform, so that GPU vertex transforms are driven by the same camera the software backend sees.

### Texture management

9. As a **game loop author**, I want to load a texture once and get a handle I can pass to draw calls, so that I don't pass raw pixel buffers and magic-number texture indices per-quad.

10. As a **software backend**, I want `game_render_load_texture` to store the raw pixel buffer and return an opaque handle, so that `game_render_quad_world` can look up the buffer internally without the caller managing it.

11. As a **GPU backend**, I want `game_render_load_texture` to upload pixel data to an `SDL_GPUTexture` and return an opaque handle, so that draw calls bind the pre-uploaded texture rather than re-uploading per frame.

12. As a **caller**, I want a single `game_render_load_texture`/`game_render_free_texture` pair rather than five different load/free pairs for each asset category, so that the asset lifecycle is consistent and I don't need to remember which function loads what.

### Facade and dispatch

13. As a **mode switcher**, I want `game_render_set_mode(SOFTWARE | GPU)` to remain a single call, so that the game can switch backends at startup without changing any other code.

14. As a **debugger**, I want `game_render_get_mode()` to tell me which backend is active, so that I can confirm the correct backend is running without inspecting internals.

### Frame lifecycle and palette

15. As a **game loop author**, I want `begin_frame`/`end_frame` to bracket all rendering for a single game frame, so that backends can batch draw calls and present at `end_frame`.

16. As a **caller using palette-based effects**, I want `game_render_set_palette` to set the colour palette uniformly regardless of backend, so that colour remapping is backend-agnostic.

17. As a **fade author**, I want `begin_fade`/`fade_active`/`fade_wait` to work the same way for both backends, so that screen transitions don't have backend-specific code paths in the game loop.

### Testability

18. As a **developer testing the seam**, I want to pass known `GameRenderVertex` and `GameRenderCamera` inputs to the software backend and read back the internal state it derives, so that I can verify the conversion logic between the public interface and the internal software pipeline without rendering a single pixel.

19. As a **developer testing asset management**, I want to load and free textures through the facade and verify the backend's texture table state, so that texture handle lifecycle bugs are caught at unit-test time rather than visually.

### Sky rendering

20. As a **game loop author**, I want to call `game_render_draw_sky` with the current camera to draw the horizon, so that sky rendering is consistent with the camera rather than reading camera state from globals.

## Implementation Decisions

### Modules

**Refactored: `game_render.h` — deepened public interface**

The header splits into two seams: scene (world-space 3D) and overlay (screen-space 2D).

Scene functions accept `GameRenderVertex` and `GameRenderCamera` at world-space; overlay functions (`sprite`, `print_block`) remain screen-space. Texture loading unifies into a single `game_render_load_texture`/`free_texture` pair returning `TextureHandle`. `game_render_quad` becomes `game_render_quad_world` taking `GameRenderVertex[4]` + `TextureHandle` instead of `tPolyParams*` + raw data. `game_render_set_camera` takes `GameRenderCamera*` instead of individual view mode parameters. `game_render_draw_horizon` becomes `game_render_draw_sky` taking `GameRenderCamera*`. The palette (`game_render_set_palette`), fade (`begin_fade`/`fade_active`/`fade_wait`), viewport, and frame lifecycle functions stay as-is — they are already backend-agnostic.

**New: `GameRenderVertex` / `GameRenderCamera` / `TextureHandle` — public types**

Three new types added to the public header:
- `GameRenderVertex` holds `float x,y,z` (world position) and `float u,v` (texture coordinates), replacing screen-space `tPolyParams` for scene geometry
- `GameRenderCamera` holds `viewX, viewY, viewZ` (camera position) and `cosYaw, sinYaw` (orientation), letting callers pass a full camera description in one struct
- `TextureHandle` is an opaque type (backend stores the real resource — the caller sees only the handle)

**Refactored: `game_render.c` — facade dispatcher**

Same dispatch-through pattern as currently, but updated to the new signatures. Passes `GameRenderVertex` arrays, `TextureHandle`, and `GameRenderCamera` structs through to backend-specific functions. No new logic — purely a passthrough.

**Refactored: `game_render_software.c` — software backend**

Internal changes only (the public interface is `game_render.h`):
- `game_render_sw_quad_world` converts `GameRenderVertex[4]` into the internal format `subdivide` expects, then calls `subdivide` which in turn calls `POLYTEX`/`POLYFLAT`
- `subdivide` moves into this module (currently in `drawtrk3.c`) as a static function
- `game_render_sw_set_camera` unpacks `GameRenderCamera` into `viewx/viewy/viewz`, `fcos`/`fsin` globals
- `game_render_sw_load_texture` stores `{pixels, width, height}` in an internal table and returns a slot index as the handle; `game_render_sw_quad_world` looks up pixels by handle
- `game_render_sw_draw_car` uses the passed yaw/pitch/roll instead of reading from globals, computing the car pose internally and passing it to `DisplayCar`
- `game_render_sw_draw_sky` uses the passed `GameRenderCamera` instead of globals

**Unchanged: `game_render_software.h`**

Internal header, mirroring public signature changes with `_sw` suffix. No structural change.

**Modified: call-site files**

Eight files that currently call `POLYTEX`/`POLYFLAT` directly need their calls updated to the new `game_render_quad_world` signature. `drawtrk3.c` has its `subdivide` calls removed (the function moves into the software backend). `3d.c` is updated to compute a `GameRenderCamera` from `calculateview()` results and pass it to the renderer. Each file's changes are mechanical — same rendering result, different interface shape.

### Architectural decisions

**The seam is `menu_render`-proven, not hypothetical.** The codebase already has `menu_render_software` and `menu_render_gpu` on `master`. Following the same pattern for `game_render` is consistent with the project's established convention. The second adapter (GPU game backend) follows naturally.

**`subdivide` moves entirely into the software backend** rather than being split into "shared math" vs "backend rasterization." The 2000-line function interleaves clipping, projection, and span output in a way that makes extraction expensive. The GPU backend receives the same world-space vertices and does its own projection in a shader. If GPU-side CPU culling is later needed, the shared projection math can be extracted at that point with a concrete consumer.

**The facade stays single-adapter during this PRD.** The GPU backend is not part of this work. The deepened interface is designed to make adding it straightforward, but the mode dispatch continues to route only to the software backend. This follows the `menu_render` precedent: the software backend landed first, the GPU backend followed.

**Asset loading unifies** into a single `game_render_load_texture`/`free_texture` pair. The caller provides raw pixel data and dimensions; the backend stores (sw) or uploads (gpu) it. The `load_blocks` function for HUD sprite sheets remains separate because the data format differs from raw pixel data. All previous per-asset-type load/free pairs are removed.

### No schema changes, no API contracts beyond C header

The "API" is the C header file `game_render.h`. There is no serialisation, no wire protocol, no database. Backward compatibility is not a concern — the branch hasn't merged to `master` yet, and call-site changes are already in progress throughout the 21 commits on `feat/game-render`.

## Testing Decisions

### Aspirational scope (north star)

The ideal test suite validates the **conversion layer** between the public interface and the software backend's internal format, via pure unit tests on the `_sw` functions, plus cross-backend visual comparison once the GPU backend exists.

### What a good test looks like

A good test exercises the external interface of the module using known inputs and verifies the output — without reaching into internal implementation details. For the software backend, this means calling `game_render_sw_set_camera(cam)` and reading back the globals it populates, or calling `game_render_sw_load_texture` and verifying the returned handle is valid and unique. The actual pixel output of `POLYTEX` is not tested — that is a visual-snapshot test and fragile to any rendering change.

### Practical landing scope

Tests are deferred. The codebase has zero test infrastructure (no framework, no test targets in build systems, no test directory). Adding one is a separate project. The conversion functions are simple enough that visual verification (run the game, confirm it looks the same as before) is sufficient for this stage. If the GPU backend lands and exposes visual differences, that is the trigger to add the conversion-layer tests.

### Prior art

There is no prior test art in this codebase. The project is verified by building and playing. The `menu_render` GPU backend was verified visually against the software backend without automated tests.

## Out of Scope

- **The GPU backend itself.** This work reshapes the interface to be GPU-ready but does not implement `game_render_gpu.c`. That is a follow-up PRD.
- **`render_common` extraction.** The design doc mentions extracting shared sprite/fade/presentation code into a `render_common` module used by both `menu_render` and `game_render`. That remains deferred until the GPU backend is added, as stated in the original implementation plan.
- **Fixing `subdivide` internals.** The function is moved into the software backend but not refactored. The GOTO-based control flow, the interleaved clipping/rasterization, and the global state dependencies within `subdivide` are unchanged.
- **Removing `fade_palette` direct calls from the game loop.** The game loop currently has two direct `fade_palette()` calls with comments explaining they happen before/after the renderer's lifetime. These are kept as-is — they work and the comments are clear.
- **Moving `calculateview` out of the game loop.** The function stays in `3d.c` — it is still called by the game loop to compute camera parameters. The only change is that its result is packaged into a `GameRenderCamera` struct rather than scattered across globals.
- **Automated tests.** Deferred as described in Testing Decisions.
- **Any changes to `menu_render`.** The menu renderer is not in scope.

## Further Notes

- This PRD is the result of an architectural deep-dive on the 21 commits currently on `feat/game-render`. The branch has already done the hard work of routing every rendering call site through the facade. This work reshapes the interface the call sites use, building on that investment.
- The `GameRenderCamera` struct should start simple (`viewX/Y/Z`, `cosYaw`, `sinYaw`, a `fovScale` field) and grow only as the GPU backend reveals what it needs. Avoid packing a full 4×4 matrix unless required.
- The `TextureHandle` type should be `int` initially (array index in software backend, opaque handle in GPU backend). A forward-compatible opaque `typedef struct GameRenderTexture* TextureHandle` works but may be overengineered for C code before the GPU backend exists.
- The implementation plan from `docs/plans/2026-03-20-game-render-plan.md` explicitly deferred deeper wire-up and DRY extraction until the GPU backend. Those deferrals remain valid — this PRD replaces the "deeper refactor" phase referenced in the original plan.
