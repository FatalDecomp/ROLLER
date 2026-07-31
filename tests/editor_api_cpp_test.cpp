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

int main()
{
    return ROLLER_ED_INVALID_CHUNK_ID == UINT32_MAX
        && ROLLER_ED_INVALID_MATERIAL_ID == UINT32_MAX
        && ROLLER_ED_PAIR_TEXTURE_TILE_SPAN == 2u
        && ROLLER_ED_OVERLAY_SHOW_SURFACES == (1u << 0)
        && ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH == (1u << 9)
        ? 0 : 1;
}
