#ifndef ROLLER_EDITOR_API_H
#define ROLLER_EDITOR_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(ROLLER_ED_BUILD_SHARED)
#    if defined(ROLLER_ED_EXPORTS)
#      define ROLLER_ED_API __declspec(dllexport)
#    else
#      define ROLLER_ED_API __declspec(dllimport)
#    endif
#  else
#    define ROLLER_ED_API
#  endif
#  define ROLLER_ED_CALL __cdecl
#elif defined(__GNUC__) || defined(__clang__)
#  define ROLLER_ED_API __attribute__((visibility("default")))
#  define ROLLER_ED_CALL
#else
#  define ROLLER_ED_API
#  define ROLLER_ED_CALL
#endif

#if defined(__cplusplus)
#  define ROLLER_ED_STATIC_ASSERT(condition, message) static_assert(condition, message)
#  define ROLLER_ED_ALIGNOF(type) alignof(type)
#elif defined(_MSC_VER)
#  define ROLLER_ED_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#  define ROLLER_ED_ALIGNOF(type) __alignof(type)
#else
#  define ROLLER_ED_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#  define ROLLER_ED_ALIGNOF(type) _Alignof(type)
#endif

#define ROLLER_ED_API_VERSION 1u

/* The v1 ABI uses native C field alignment with an eight-byte packing cap. */
#define ROLLER_ED_ABI_PACKING 8u

/* Public sentinels. A missing back material does not imply single-sidedness. */
#define ROLLER_ED_INVALID_CHUNK_ID UINT32_MAX
#define ROLLER_ED_INVALID_MATERIAL_ID UINT32_MAX

/* Fixed-width enum ABI: public enum-shaped types are uint32_t or uint8_t. */
typedef uint32_t eRollerEdResult;
enum
{
    ROLLER_ED_RESULT_OK = 0u,
    ROLLER_ED_RESULT_INVALID_ARGUMENT = 1u,
    ROLLER_ED_RESULT_INVALID_VERSION = 2u,
    ROLLER_ED_RESULT_NOT_INITIALIZED = 3u,
    ROLLER_ED_RESULT_RENDERER_UNAVAILABLE = 4u,
    ROLLER_ED_RESULT_NO_SCENE = 5u,
    ROLLER_ED_RESULT_STALE = 6u,
    ROLLER_ED_RESULT_BUFFER_TOO_SMALL = 7u,
    ROLLER_ED_RESULT_LOAD_FAILED = 8u,
    ROLLER_ED_RESULT_GPU_FAILED = 9u,
    ROLLER_ED_RESULT_OUT_OF_MEMORY = 10u,
    ROLLER_ED_RESULT_IO_FAILED = 11u,
    ROLLER_ED_RESULT_UNSUPPORTED = 12u,
    ROLLER_ED_RESULT_INTERNAL_ERROR = 13u,
    ROLLER_ED_RESULT_INVALID_STATE = 14u,
    ROLLER_ED_RESULT_WRONG_THREAD = 15u
};

typedef uint32_t eRollerEdRenderer;
enum
{
    ROLLER_ED_RENDERER_SOFTWARE = 1u,
    ROLLER_ED_RENDERER_GPU = 2u
};

typedef uint32_t eRollerEdPixelFormat;
enum
{
    ROLLER_ED_PIXEL_RGBA8 = 0u
};

typedef uint32_t eRollerEdSceneState;
enum
{
    ROLLER_ED_SCENE_EMPTY = 0u,
    ROLLER_ED_SCENE_READY = 1u,
    ROLLER_ED_SCENE_FAILED = 2u
};

typedef uint32_t eRollerEdContentClass;
enum
{
    ROLLER_ED_CONTENT_AUTHORED_TRACK = 0u,
    ROLLER_ED_CONTENT_AUTHORED_SIGN = 1u,
    ROLLER_ED_CONTENT_AUTHORED_SCENERY = 2u,
    ROLLER_ED_CONTENT_RUNTIME_SCENERY = 3u
};

typedef uint32_t eRollerEdMaterialKind;
enum
{
    ROLLER_ED_MATERIAL_TEXTURED_TILE = 0u,
    ROLLER_ED_MATERIAL_TEXTURED_PAIR = 1u,
    ROLLER_ED_MATERIAL_FLAT_PALETTE_COLOR = 2u,
    ROLLER_ED_MATERIAL_SCREEN_DARKEN = 3u
};

typedef uint8_t eRollerEdPrimitiveTopology;
enum
{
    ROLLER_ED_TOPOLOGY_TRIANGLE_LIST = 0u,
    ROLLER_ED_TOPOLOGY_LINE_LIST = 1u
};

enum
{
    ROLLER_ED_PRIMITIVE_FLAG_ALPHA_BLEND = 1u << 0,
    ROLLER_ED_PRIMITIVE_FLAG_TWO_SIDED = 1u << 1,
    ROLLER_ED_PRIMITIVE_FLAG_WIREFRAME = 1u << 2
};

enum
{
    ROLLER_ED_MATERIAL_FLAG_ALPHA_BLEND = 1u << 0,
    ROLLER_ED_MATERIAL_FLAG_PARTIAL_ALPHA = 1u << 1,
    /* TEXTURED_PAIR only. The pair's second tile is the first tile of the
     * next atlas row, so fAtlasScale/fAtlasBias run past the right edge of
     * the atlas. The legacy renderer composes that pair from a flat
     * row-stride read rather than from the tile to the right, so a consumer
     * that samples the transform directly reproduces the left tile exactly
     * and only approximates the wrapped half. */
    ROLLER_ED_MATERIAL_FLAG_PAIR_WRAPS_ATLAS_ROW = 1u << 2
};

/* These values intentionally match the legacy renderer texture banks. */
enum
{
    ROLLER_ED_TEXTURE_SET_TRACK = 0u,
    ROLLER_ED_TEXTURE_SET_BUILDING_SIGN = 17u
};

/* A pair starts at uiTileIndex and spans it plus the next horizontal tile. */
#define ROLLER_ED_PAIR_TEXTURE_TILE_SPAN 2u
/* For a pair, material-local U in [0,1] spans both tiles left-to-right. */

enum
{
    ROLLER_ED_OVERLAY_SHOW_SURFACES = 1u << 0,
    ROLLER_ED_OVERLAY_SHOW_WIREFRAME = 1u << 1,
    ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION = 1u << 2,
    ROLLER_ED_OVERLAY_SHOW_AI_LINES = 1u << 3,
    ROLLER_ED_OVERLAY_SHOW_CENTER_LINE = 1u << 4,
    ROLLER_ED_OVERLAY_SHOW_ENVIRONMENT_FLOOR = 1u << 5,
    ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS = 1u << 6,
    ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS = 1u << 7,
    ROLLER_ED_OVERLAY_SHOW_TEST_CAR = 1u << 8,
    ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH = 1u << 9
};

enum
{
    ROLLER_ED_REFERENCE_HAS_NORMALS = 1u << 0,
    ROLLER_ED_REFERENCE_WIREFRAME = 1u << 1,
    ROLLER_ED_REFERENCE_TWO_SIDED = 1u << 2
};

#define ROLLER_ED_BOOTSTRAP_INFO_VERSION ROLLER_ED_API_VERSION
#define ROLLER_ED_INIT_INFO_VERSION ROLLER_ED_API_VERSION
#define ROLLER_ED_CAMERA_STATE_VERSION ROLLER_ED_API_VERSION
#define ROLLER_ED_OVERLAY_STATE_VERSION ROLLER_ED_API_VERSION
#define ROLLER_ED_REFERENCE_MESH_VERSION ROLLER_ED_API_VERSION
#define ROLLER_ED_GEOMETRY_SIZES_VERSION ROLLER_ED_API_VERSION

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#  pragma pack(push, 8)
#endif

typedef struct
{
    uint32_t uiStructSize;
    uint32_t uiVersion;
    uint32_t uiFlags; /* Reserved for v1; must be zero. */
} tRollerEdBootstrapInfo;

typedef struct
{
    uint32_t uiStructSize;
    uint32_t uiVersion;
    const char *szAssetRoot; /* Copied by RollerEd_Init; caller storage is not retained. */
    eRollerEdRenderer ePreferredRenderer;
    uint32_t uiAllowSoftwareFallback; /* 0 or 1 only. */
} tRollerEdInitInfo;

typedef struct
{
    uint32_t uiStructSize;
    uint32_t uiVersion;
    float fPosition[3];
    float fYawDegrees;
    float fPitchDegrees;
} tEdCameraState;

typedef struct
{
    uint32_t uiStructSize;
    uint32_t uiVersion;
    uint32_t uiFlags; /* OR of ROLLER_ED_OVERLAY_* flags. */
    uint32_t uiFirstSelectedChunk;
    uint32_t uiLastSelectedChunk;
} tEdOverlayState;

typedef struct
{
    float fPosition[3];
    float fNormal[3];
    float fUV[2]; /* Normalized, top-left-origin mesh-local UV. */
} tEdReferenceVertex;

typedef struct
{
    uint32_t uiStructSize;
    uint32_t uiVersion;

    const tEdReferenceVertex *pVertices;
    uint32_t uiVertexCount;

    const uint32_t *puiIndices; /* NULL means a non-indexed triangle list. */
    uint32_t uiIndexCount;

    const uint8_t *pbyTextureRGBA; /* Top-left, straight-alpha sRGB RGBA8. */
    uint32_t uiTextureWidth;
    uint32_t uiTextureHeight;
    uint32_t uiTextureRowPitch;

    float fPosition[3];
    float fRotation[3]; /* Degrees, applied yaw then pitch then roll. */
    float fScale[3]; /* Transform order is scale, rotate, translate. */

    uint32_t uiFlags; /* OR of ROLLER_ED_REFERENCE_* flags. */
} tEdReferenceMesh;

typedef struct
{
    float fPosition[3];
    float fNormal[3];
    float fUV[2]; /* Material-local; resolve through the selected material. */
} tEdVertex;

typedef struct
{
    uint32_t uiFirstIndex;
    uint32_t uiIndexCount;
    uint32_t uiChunkId;
    uint32_t uiFrontMaterialId;
    uint32_t uiBackMaterialId;
    uint16_t unSurfaceClass;
    uint16_t unContentClass;
    uint16_t unFlags; /* OR of ROLLER_ED_PRIMITIVE_FLAG_*. */
    uint8_t byTopology; /* One ROLLER_ED_TOPOLOGY_* value. */
    uint8_t byReserved;
} tEdPrimitive;

typedef struct
{
    uint32_t uiKind;
    uint32_t uiTextureSet;
    uint32_t uiTileIndex;
    uint32_t uiPaletteColour;
    uint32_t uiDarkenLevel;
    uint32_t uiFlags; /* OR of ROLLER_ED_MATERIAL_FLAG_*. */
    float fAtlasScale[2];
    float fAtlasBias[2];
} tEdMaterial;

typedef struct
{
    uint32_t uiStructSize;
    uint32_t uiVersion;
    uint32_t uiGeometryEpoch;
    uint32_t uiTrackGeneration;
    uint32_t uiSceneState;
    uint32_t uiVertexCount;
    uint32_t uiIndexCount;
    uint32_t uiPrimitiveCount;
    uint32_t uiMaterialCount;
    uint32_t uiVertexStride;
    uint32_t uiPrimitiveStride;
    uint32_t uiMaterialStride;
} tEdGeometrySizes;

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#  pragma pack(pop)
#endif

ROLLER_ED_STATIC_ASSERT(sizeof(eRollerEdResult) == 4u, "result ABI");
ROLLER_ED_STATIC_ASSERT(sizeof(eRollerEdRenderer) == 4u, "renderer ABI");
ROLLER_ED_STATIC_ASSERT(sizeof(eRollerEdPixelFormat) == 4u, "pixel ABI");
ROLLER_ED_STATIC_ASSERT(sizeof(tRollerEdBootstrapInfo) == 12u, "bootstrap size");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tRollerEdBootstrapInfo) == 4u,
                        "bootstrap alignment");
ROLLER_ED_STATIC_ASSERT(offsetof(tRollerEdBootstrapInfo, uiFlags) == 8u,
                        "bootstrap flags offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tRollerEdInitInfo, szAssetRoot) == 8u,
                        "init asset-root offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tRollerEdInitInfo, ePreferredRenderer)
                            == (sizeof(void *) == 8u ? 16u : 12u),
                        "init renderer offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tRollerEdInitInfo) == (sizeof(void *) == 8u ? 24u : 20u),
                        "init size");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tRollerEdInitInfo)
                            == ROLLER_ED_ALIGNOF(void *),
                        "init alignment");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdCameraState) == 28u, "camera size");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdCameraState) == 4u,
                        "camera alignment");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdCameraState, fPosition) == 8u,
                        "camera position offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdCameraState, fPitchDegrees) == 24u,
                        "camera pitch offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdOverlayState) == 20u, "overlay size");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdOverlayState) == 4u,
                        "overlay alignment");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdOverlayState, uiFlags) == 8u,
                        "overlay mask offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdOverlayState, uiLastSelectedChunk) == 16u,
                        "overlay selection offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdReferenceVertex) == 32u, "reference vertex size");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdReferenceVertex) == 4u,
                        "reference vertex alignment");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdReferenceVertex, fUV) == 24u,
                        "reference vertex UV offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdReferenceMesh, pVertices) == 8u,
                        "reference vertex pointer offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdReferenceMesh, pbyTextureRGBA)
                            == (sizeof(void *) == 8u ? 40u : 24u),
                        "reference texture pointer offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdReferenceMesh) == (sizeof(void *) == 8u ? 104u : 80u),
                        "reference mesh size");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdReferenceMesh)
                            == ROLLER_ED_ALIGNOF(void *),
                        "reference mesh alignment");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdReferenceMesh, uiFlags)
                            == (sizeof(void *) == 8u ? 96u : 76u),
                        "reference flags offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdVertex) == 32u, "vertex size");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdVertex, fNormal) == 12u, "vertex normal offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdVertex, fUV) == 24u, "vertex UV offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdPrimitive) == 28u, "primitive size");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdPrimitive, uiFrontMaterialId) == 12u,
                        "primitive front material offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdPrimitive, uiBackMaterialId) == 16u,
                        "primitive back material offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdPrimitive, unContentClass) == 22u,
                        "primitive content offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdPrimitive, byTopology) == 26u,
                        "primitive topology offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdMaterial) == 40u, "material size");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdMaterial, uiDarkenLevel) == 16u,
                        "material darken offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdMaterial, fAtlasScale) == 24u,
                        "material scale offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdMaterial, fAtlasBias) == 32u,
                        "material bias offset");
ROLLER_ED_STATIC_ASSERT(sizeof(tEdGeometrySizes) == 48u, "geometry sizes size");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdGeometrySizes) == 4u,
                        "geometry sizes alignment");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdGeometrySizes, uiGeometryEpoch) == 8u,
                        "geometry epoch offset");
ROLLER_ED_STATIC_ASSERT(offsetof(tEdGeometrySizes, uiVertexStride) == 36u,
                        "geometry stride offset");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdVertex) == 4u, "vertex alignment");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdPrimitive) == 4u, "primitive alignment");
ROLLER_ED_STATIC_ASSERT(ROLLER_ED_ALIGNOF(tEdMaterial) == 4u, "material alignment");

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Main-thread lifecycle. Bootstrap acquires one ref-counted SDL video
 * subsystem reference even when the host already owns SDL. Bootstrap and
 * Teardown are idempotent on their owning main thread. Teardown releases only
 * the reference acquired by this library and is invalid before worker cleanup.
 */
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_Bootstrap(
    const tRollerEdBootstrapInfo *pInfo);
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_Teardown(void);

/*
 * Render-worker lifecycle. Init copies szAssetRoot, performs no eager GPU
 * creation, may be retried after failure, and rejects a repeated success.
 * Shutdown is valid after full or partial initialization, rejects repeats,
 * and must complete before Teardown.
 */
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_Init(
    const tRollerEdInitInfo *pInfo);
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_Shutdown(void);

/* Returns a bitmask of ROLLER_ED_RENDERER_* values. */
ROLLER_ED_API uint32_t ROLLER_ED_CALL RollerEd_GetAvailableRenderers(void);
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_SelectRenderer(
    eRollerEdRenderer eKind);

/*
 * Both paths are consumed synchronously and never retained. A successful
 * load advances both the geometry epoch and committed track generation. A
 * failed file load clears the renderable scene, advances only the geometry
 * epoch, and reports SCENE_FAILED until a later load succeeds.
 */
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_LoadTrackFile(
    const char *szTrackPath, const char *szDocumentAssetRoot);
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_UnloadTrack(void);
/* Camera and overlay inputs are copied during the call and are not retained. */
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_SetCamera(
    const tEdCameraState *pCam);
/*
 * Completes synchronously into top-left, straight-alpha RGBA8 device pixels.
 * Software mode renders at its native internal size, then aspect-preserving
 * nearest-neighbour scales with opaque-black letterboxing.
 */
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_RenderFrame(
    uint8_t *pbyPixels, uint32_t uiBufferSize, uint32_t uiRowPitch,
    uint32_t uiWidth, uint32_t uiHeight, eRollerEdPixelFormat eFormat);
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_SetOverlayState(
    const tEdOverlayState *pState);

/*
 * Vertex, index, and texture payloads are copied during a successful call.
 * The mesh is a triangle list with uint32_t indices; NULL indices mean
 * non-indexed. Missing normals are generated when HAS_NORMALS is clear.
 * NULL vertices or zero vertices clear the mesh. A failed replacement keeps
 * the previous mesh. Texture and UV origins are top-left; texture data is
 * straight-alpha sRGB RGBA8. Transform order is scale, yaw/pitch/roll, then
 * translate.
 */
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_SetReferenceMesh(
    const tEdReferenceMesh *pMesh);

ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_QueryGeometrySizes(
    tEdGeometrySizes *pSizesOut);
/* Caller-owned buffers are written only when this call returns OK. */
ROLLER_ED_API eRollerEdResult ROLLER_ED_CALL RollerEd_FillGeometry(
    uint32_t uiExpectedGeometryEpoch,
    tEdVertex *pVerts, uint32_t uiVertexCapacity,
    uint32_t *puiIndices, uint32_t uiIndexCapacity,
    tEdPrimitive *pPrims, uint32_t uiPrimitiveCapacity,
    tEdMaterial *pMats, uint32_t uiMaterialCapacity);

/* Worker-owned text valid until the next facade call on that worker. */
ROLLER_ED_API const char *ROLLER_ED_CALL RollerEd_GetLastError(void);

#ifdef __cplusplus
}
#endif

#endif
