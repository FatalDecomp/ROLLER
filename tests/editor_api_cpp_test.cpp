#include "editor_api.h"

#include <type_traits>

static_assert(std::is_standard_layout<tRollerEdBootstrapInfo>::value,
              "bootstrap info must be standard layout");
static_assert(std::is_standard_layout<tRollerEdInitInfo>::value,
              "init info must be standard layout");
static_assert(std::is_standard_layout<tEdReferenceMesh>::value,
              "reference mesh must be standard layout");
static_assert(std::is_standard_layout<tEdGeometrySizes>::value,
              "geometry sizes must be standard layout");
static_assert(std::is_standard_layout<tEdOverlayState>::value,
              "overlay state must be standard layout");
static_assert(std::is_standard_layout<tEdTowerInfo>::value,
              "tower info must be standard layout");

int main()
{
    return ROLLER_ED_INVALID_CHUNK_ID == UINT32_MAX
        && ROLLER_ED_INVALID_MATERIAL_ID == UINT32_MAX
        && ROLLER_ED_PAIR_TEXTURE_TILE_SPAN == 2u
        && ROLLER_ED_OVERLAY_SHOW_SURFACES == (1u << 0)
        && ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH == (1u << 9)
        /* E3A-S2: the class masks index eRollerEdSurfaceClass, and only this
         * struct's version moved. */
        && ROLLER_ED_SURFACE_CLASS_COUNT == 14u
        && ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_TOWER)
               == (1u << 13)
        && ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES == 0x3fffu
        && ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS == (1u << 10)
        && ROLLER_ED_OVERLAY_TEST_CAR_ADVANCED == (1u << 11)
        && ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS == (1u << 12)
        && ROLLER_ED_OVERLAY_DETACH_LAST == (1u << 13)
        && ROLLER_ED_TEST_CAR_DESIGN_COUNT == 14u
        && ROLLER_ED_TEST_CAR_AI_LINE_COUNT == 4u
        && ROLLER_ED_OVERLAY_STATE_VERSION == 3u
        && ROLLER_ED_CAMERA_STATE_VERSION == 1u
        && ROLLER_ED_REFERENCE_MESH_VERSION == 1u
        && ROLLER_ED_TOWER_INFO_VERSION == 1u
        ? 0 : 1;
}
