# ADR 0003: Canonical editor geometry conventions

- **Status:** Accepted
- **Date:** 2026-08-05
- **Drives:** Track Editor Modernization spec v8.6, EPIC 4A story E4A-S4
- **Supersedes:** none
- **Superseded by:** [ADR 0005](0005-camera-independent-scenery-traversal.md),
  for the "Known limitation" paragraph under *Winding* only. Every other
  convention in this document stands.

## Context

The canonical surface emitter (`PROJECTS/ROLLER/editor_surface.c`, E4A-S1) feeds
two consumers with different needs. The renderer wants exactly what the legacy
code always produced. An exporter wants a mesh that opens correctly in Blender,
which needs normals, and needs to know the coordinate system, the winding, the
units, and where UV (0,0) sits.

None of that was written down. The legacy renderer never needed it: its vertex
carries position and UV only, it generates no normals, and every consumer of a
`GameRenderVertex` was inside ROLLER and already agreed with itself by
construction. So the conventions existed only as behaviour spread across
`transfrm.c`, `drawtrk3.c`, `polytex.c`, and `scene_render_gpu.c`.

Leaving them implicit is how the parallel-implementation problem this migration
exists to end would come back: each exporter (OBJ in E4-S1, glTF in E4-S2, FBX
in E4-S3) would re-derive winding and handedness from trial and error against
its own importer, and they would not agree.

## Decision

The emitter publishes these conventions, and `tests/editor_surface_test.c`
asserts each one.

### Coordinate system

Legacy ROLLER world space, unchanged and unconverted. Axes satisfy the algebraic
right-hand rule (`X × Y = Z`), and **world +Z is up**.

This follows from the view basis in `transfrm.c`. With zero elevation and tilt,
`calculatetransform` builds:

```text
view X (screen right)  = world ( sin d, -cos d, 0 )
view Y (screen up)     = world ( 0, 0, 1 )
view Z (into screen)   = world ( cos d,  sin d, 0 )
```

So a heading of zero looks along world +X, increasing heading rotates toward +Y,
and world Z is the vertical axis. Note that `right × up = -forward`: view space
is **left-handed** (x right, y up, z into the screen), which is why the
projection in `tower.c` divides by a positive view Z and then flips Y with
`199 - y` for the framebuffer. The emitter never enters view space; this is
recorded only so an exporter does not mistake the renderer's left-handed view
frame for the world frame.

### Scale

There is none. World positions are copied verbatim out of `TrakPt` / `GroundPt`
in legacy track units, with no multiply and no unit conversion.

Exporters own their own conversion. Putting a scale factor in the emitter would
mean every consumer had to know whether it had already been applied, and the
editor viewport — which shares this emitter — needs the raw values.

### Winding

The producer's vertex order `v0..v3` is preserved exactly. The emitter never
reorders vertices; `SURFACE_FLAG_FLIP_HORIZ` and `SURFACE_FLAG_FLIP_VERT`
permute UVs only.

The right-hand-rule normal of that order is the **front face**. This is not an
arbitrary choice — it is the renderer's own definition. `scene_render_gpu.c`
decides whether to apply the `texture_back[]` substitution by computing
`bnrm = (v1-v0) × (v3-v0)` and testing `bnrm · (camera - v0)`; the surface is
front-facing exactly when that dot product is positive. So the side the emitted
normal points at is the side `uiFrontMaterialId` describes, and
`uiBackMaterialId` (AD-7e) describes the other one.

Whether a primitive is single- or double-sided is carried separately in
`ROLLER_ED_SURFACE_FLAG_TWO_SIDED`, never inferred from the normal or from the
back-material sentinel. **Two legacy flags set it:**
`SURFACE_FLAG_FLIP_BACKFACE`, which draws the reverse side, and
`SURFACE_FLAG_CONCAVE`, which makes the renderer bypass its facing test entirely
(`drawtrk3.c:3001` and its three siblings, `polyf.c:245`, `polytex.c:625`). A
`CONCAVE` surface is visible from both sides, so an exporter that emitted it
single-sided would leave holes.

**Winding is consistent within the road body, not across the terrain skirt.**
Centre, shoulders, walls, roof, and outer wall floor form one continuous ribbon
whose normal turns gradually, even through corkscrews. The four outer-wall
classes are the environment skirt, and where the terrain profile crosses over, a
panel legitimately faces inward at one chunk and outward at the next — retail
`TRACK3` does this twelve times. That is the source data, not a defect, and it
is why the acceptance test asserts neighbour consistency only for the road body.
An exporter must not assume a globally coherent outside for the skirt.

**Known limitation — superseded by ADR 0005.** *(Original text, retained because
ADR 0003 is the record of what was decided in E4A-S4:)* building and sign
surfaces reach the emitter only through the camera-driven render path, and
`building.c` reverses its vertex order for back-facing quads at draw time. Their
winding, and therefore their normals, follow that draw-time decision. The
canonical full-track traversal (`drawtrk3_emit_full_track`, E4A-S2) covers track
chunks only and is camera-independent, so this does not affect it. A
camera-independent building traversal is out of scope until an export story
needs one.

E4A-S6 built that traversal. `drawtrk3_emit_full_scenery` emits scenery in the
plan's authored vertex order and at the authored yaw, so scenery winding no
longer follows any draw-time decision and the rest of this section applies to it
unchanged. See ADR 0005.

### Normals

Generated by the emitter, because the render vertex carries position and UV only
and there is nothing upstream to inherit.

- `tEdSurfaceEmission.fNormal` is the whole-quad normal by **Newell's method**
  over all four corners.
- `tEdSurfaceVertex.fNormal` is that vertex's **adjacent-edge** normal,
  `normalize((v_next - v) × (v_prev - v))`.

Both follow the right-hand rule for `v0..v3`, so both point at the front face.
On a flat quad they are identical. They diverge on the twisted and rolled
sections this track format allows, which is the reason for computing both: a
normal taken from three of four corners is unreliable on corkscrews — the same
failure that made `scene_render_gpu.c` abandon its 3-vertex normal for
pair-texture suppression.

All normals are unit length. Degenerate geometry yields exactly zero rather than
a NaN: a collapsed quad zeroes the surface normal.

A corner whose own triangle is negligible against the quad — under
`ED_NORMAL_DEGENERATE_RATIO`, currently 1/64 of the Newell magnitude, where both
quantities are twice an area — falls back to the surface normal. This is not a
theoretical guard. Retail `TRACK3` chunk 124 pinches its right shoulder so that
`v0` and `v1` sit under one unit apart at coordinates near 90,000; the sliver
triangle those corners describe is about 92° from the quad's own plane, which
would be a visibly wrong shading normal. The threshold sits roughly an order of
magnitude either side of both the pinched case (~0.0015) and a well-formed
corner (~0.5).

The same pinch makes the renderer's three-corner facing vector meaningless
there, so the acceptance test compares the emitted normal against it only where
that vector is well conditioned. Across `TRACK3`'s 3,801 emitted surfaces, 61
quads are pinched to that degree; the remaining 3,740 agree with the renderer.

### UV origin

Top-left, matching the top-left-origin RGBA8 texture rows used everywhere else
in the facade. `v0`/`v1` sit on `V = 0` and `v2`/`v3` on `V = 1`; `U` runs left
to right across the tile, or across both tiles of a pair texture.

Emitted UVs are **material-local** (AD-7b). An exporter resolves them to the
atlas with the selected material's `fAtlasScale` / `fAtlasBias`, and must not do
tile arithmetic from `uiTileIndex`.

## Consequences

- E4A-S5's `RollerEd_FillGeometry` copies `tEdSurfaceVertex.fNormal` straight
  into `tEdVertex.fNormal`; no consumer generates normals.
- Epic 4's exporters state their own axis conversion, unit scale, and winding
  flip **relative to this document** rather than deriving one empirically.
- `tEdReferenceMesh` (AD-13) inherits this coordinate system and winding, as its
  spec text already promised.
- Changing any convention here is a breaking change for every exporter, so it
  needs a superseding ADR rather than an edit.
