# ADR 0005: Canonical scenery placement and winding

- **Status:** Accepted
- **Date:** 2026-08-07
- **Drives:** Track Editor Modernization spec v8.6, EPIC 4A story E4A-S6
- **Supersedes:** the "Known limitation" paragraph under *Winding* in
  [ADR 0003](0003-canonical-geometry-conventions.md)
- **Superseded by:** none

## Context

ADR 0003 recorded that building and sign surfaces reached the canonical emitter
only through the camera-driven render path, that `building.c` reverses its
vertex order for back-facing quads at draw time, and that "a camera-independent
building traversal is out of scope until an export story needs one."

Two export stories now need one. E4-S1 (OBJ) and E4-S2 (glTF) both consume
`RollerEd_FillGeometry`, which is fed by `drawtrk3_emit_full_track` — track
chunks only. Neither exporter can produce a sign, a building, or an advert
panel, and neither can fix that on its own: the missing piece is a producer in
the core, not an exporter feature.

Building *placement* was never the problem. `InitBuildings()` runs at load and
computes each building's origin and rotation basis from the track file with no
camera involved. Three things in the draw path are camera-dependent, and each
needed a decision rather than a port:

1. **The visible set.** `CalcVisibleBuildings()` walks `TrackSize` sections from
   `start_sect` and caps at `MAX_VISIBLE_BUILDINGS`. This is the direct analogue
   of what `ed_traverse_full_track_chunks` replaced for chunks.
1. **Winding.** `DrawBuilding` backface-culls, then reverses `v0..v3` for a
   back-facing `SURFACE_FLAG_FLIP_BACKFACE` quad so its texture stays the right
   way round. Which order a quad gets therefore depends on where the viewer is.
1. **Billboard yaw.** Building plans 9 (`balloon`), 10 (`tree`), and 15
   (`balloon2`) take their yaw from `worlddirn` — the view heading — instead of
   the yaw the track file recorded, so they always face the viewer.

The billboard yaw turned out to matter far more than the prior analysis assumed.
That analysis expected billboards to be a scope *reduction*: classify them
`RUNTIME_SCENERY`, skip them, and export the rest. In fact `balloon` and
`balloon2` polygons carry `uiTex == 0x2200` (`FLIP_BACKFACE | NO_EXTRAS`), which
is exactly the test E4A-S3 uses for a real advert panel. **They are the signs.**
Retail `TRACK3`'s 66 placed objects are all balloons; skipping billboards would
have exported nothing at all and closed none of the gap.

## Decision

### The traversal

`drawtrk3_emit_full_scenery()` walks building indices `0 .. NumBuildings-1` in
order, driven by `ed_traverse_full_scenery_objects()`, and emits every polygon
of every placed plan through the same `ed_emit_surface()` seam the renderer
uses. It reads no `start_sect`, `TrackSize`, `VisibleBuildings`, camera
position, depth sort, or render queue, and it performs no backface cull. It is
the scenery counterpart of `drawtrk3_emit_full_track` (E4A-S2) and shares its
material table with it, so one extraction interns one set of materials across
track and scenery alike.

`DrawBuilding` and the traversal share the per-building transform
(`building_transform_plan_coords`) and the per-polygon texture selection and
identity (`building_polygon_surface_info`), the same way E4A-S1 split
`set_starts()` so one calculator served both. Neither can drift from the other
by editing one of them.

### Winding: the authored vertex order is canonical

The plan's own `verts[0..3]` order is emitted verbatim. The draw-time reversal
is **not** reproduced.

This costs nothing, because the reversal only ever applies to a quad carrying
`SURFACE_FLAG_FLIP_BACKFACE`, and ADR 0003 already publishes
`ROLLER_ED_SURFACE_FLAG_TWO_SIDED` for exactly that flag. An exporter draws
those quads from both sides, so which of the two orders it received never
mattered.

For a quad *without* `FLIP_BACKFACE`, the authored order is unambiguously the
front face. `DrawBuilding`'s cull test computes `((v2-v0) x (v1-v3)) · v0` in
view space and culls when that is non-negative, which is the same determinant
`facing_ok()` evaluates for track surfaces — for a planar quad,
`(v2-v0) x (v1-v3) = -2 · ((v1-v0) x (v3-v0))`. Buildings and track surfaces
therefore obey one facing rule, and ADR 0003's statement that the
right-hand-rule normal of `v0..v3` is the front face holds for scenery
unchanged. Nothing in ADR 0003's *Winding* section needs a different reading;
only its "Known limitation" paragraph, which said scenery winding follows the
draw-time decision, is superseded.

### Placement: the authored yaw is canonical

Every plan is placed at the yaw, pitch, and roll the track file recorded —
`BuildingAngles[3i .. 3i+2]` — including the billboard plans. `worlddirn` is
never read.

A billboard has no single correct orientation; it has whatever orientation the
viewer implies. An exported mesh has no viewer, so it needs a definite one, and
the only defensible source is the value the track author supplied. That value is
real data, not a placeholder: on retail `TRACK3` the balloon yaws vary from 0 to
340 degrees and track the road heading through the long right-hand sequence at
chunks 426–451.

The consequence is deliberate and worth stating plainly: **the editor viewport
and an exported mesh disagree about billboard facing.** The viewport still turns
them toward the camera, because `DrawBuilding` still uses `worlddirn`; the
export freezes them at the authored yaw. That is inherent to exporting a
billboard, not a defect in either path.

### Content class decides what travels, per polygon

E4A-S3's classification stands unchanged and remains the authority. The
traversal emits every polygon *except* those classified
`ROLLER_ED_CONTENT_RUNTIME_SCENERY`, so no exporter ever sees one.

The filter is applied per polygon, not per plan, which is what makes the
balloons work: a billboard plan whose polygon is a real advert panel is
`AUTHORED_SIGN` and does travel. In practice the only plan that produces
`RUNTIME_SCENERY` is type 10, the tree, whose polygon is `0x2501` and so is not
a real sign. **No retail track places a tree**, so this filter drops nothing
from any shipped track today.

Towers are out of scope entirely and are not emitted. `DrawTower` builds its
quad from the view basis vectors (`vk1`, `vk2`, `vk4`, `vk5`, `vk7`, `vk8`) at a
size derived from the clamped view-space depth: there is no authored geometry
underneath to recover, only a screen-space sprite. E4A-S3 already classifies
towers `RUNTIME_SCENERY`, which is now also the rule that excludes them.

### Two-sidedness comes from the plan, not from the advert texture

An advert panel's polygon is replaced at draw time by the texture
`advert_list[]` names for that building. That substitution carries the advert
entry's flags, and retail advert entries do not set `FLIP_BACKFACE` — but
`DrawBuilding`'s cull test reads the *plan's* `uiTex`, which does. The renderer
therefore draws every panel from both sides while the substituted flags say
single-sided.

`ROLLER_ED_SURFACE_FLAG_TWO_SIDED` is set from the plan's flags, so the emission
agrees with what is on screen. It is published through `tEdSurfaceInfo.unFlags`
rather than by putting `FLIP_BACKFACE` back into `uiRenderFlags`, because
`unFlags` is additive information no render path reads — the same argument ADR
0003 makes for `SURFACE_FLAG_CONCAVE` — and the renderer's own flag word stays
exactly what it was.

This was found by exporting retail `TRACK3` end to end rather than by reading
the code: all 66 balloons came out single-sided and would have vanished from
behind in any importer that honours backface culling.

### An unresolvable texture drops one surface, not the traversal

A textured surface whose tile index is not in its bank is refused by
`ed_emit_surface`. On the render path that refusal is already how the surface
gets dropped — `DrawBuilding` ignores the return of
`drawtrk3_emit_surface_to_renderer`, so the quad is simply not drawn.

The traversal matches that behaviour rather than failing: it checks
`ed_surface_material_resolvable()` and skips such a polygon. This is not
hypothetical — retail `TRACK5`'s advert list names tile 45 of a 45-tile building
bank, and treating that as fatal would have made the whole track unexportable
over one bad entry.

Every *other* `ed_emit_surface` refusal stays fatal, because E4A-S5's probe pass
relies on a full material table returning false in order to grow the table and
retry.

## Consequences

- `RollerEd_QueryGeometrySizes` / `RollerEd_FillGeometry` now publish scenery
  alongside track geometry, in one buffer, with `unContentClass` as the only
  thing an exporter needs to separate them. Retail `TRACK3` goes from 3,801 to
  3,867 primitives.
- `<name>_BLD.png`, written unconditionally since E4-S4 and referenced by
  nothing, is now addressed by real materials — 17 of them on `TRACK3`.
- Both canonical exporters gain signs and scenery without either of them
  changing: the fix is in the core, once, as AD-6a requires.
- `EDITOR_GEOMETRY_MAX_SURFACES` now budgets for scenery as well as chunks.
- Whichever story wires up `CExportWizard::m_bExportSigns` should filter on
  `unContentClass`, never on `unSurfaceClass`, and must not re-derive object
  class from geometry.
- Changing any convention here is a breaking change for every exporter, so it
  needs a superseding ADR rather than an edit.
