#ifndef ROLLER_EDITOR_TRACK_LOADER_H
#define ROLLER_EDITOR_TRACK_LOADER_H

#include <stddef.h>
#include <stdint.h>

#define ED_TRACK_MAX_DECOMPRESSED_SIZE (64u * 1024u * 1024u)
#define ED_TRACK_MAX_CHUNKS 500u
#define ED_TRACK_ASSET_NAME_CAPACITY 13u

typedef enum
{
    ED_TRACK_LOAD_OK = 0,
    ED_TRACK_LOAD_INVALID_ARGUMENT,
    ED_TRACK_LOAD_IO_FAILED,
    ED_TRACK_LOAD_TRUNCATED,
    ED_TRACK_LOAD_INVALID_SIZE,
    ED_TRACK_LOAD_INVALID_BACK_REFERENCE,
    ED_TRACK_LOAD_OUTPUT_OVERFLOW,
    ED_TRACK_LOAD_MALFORMED_TEXT,
    ED_TRACK_LOAD_OUT_OF_MEMORY
} eEdTrackLoadResult;

typedef struct
{
    uint8_t *pbyData;
    size_t uiDataLength;
    uint32_t uiChunkCount;
    uint64_t ullContentHash;
    char szTextureFile[ED_TRACK_ASSET_NAME_CAPACITY];
    char szBuildingTextureFile[ED_TRACK_ASSET_NAME_CAPACITY];
} tEdTrackStage;

typedef struct
{
    tEdTrackStage Committed;
    uint32_t uiGeneration;
} tEdTrackDocument;

void ed_track_stage_init(tEdTrackStage *pStage);
void ed_track_stage_dispose(tEdTrackStage *pStage);

/*
 * Reads, bounds-checks, and decodes a compacted file. The output is copied
 * into pStage and terminated with DOS EOF (0x1a) plus NUL. No caller-owned
 * state is changed on failure.
 */
eEdTrackLoadResult ed_compacted_file_stage(
    const char *szPath,
    tEdTrackStage *pStage,
    char *szError,
    size_t uiErrorCapacity);

/*
 * Adds track-text validation to ed_compacted_file_stage: the header and all
 * three records for every declared chunk must be present and numeric, and
 * texture/building asset names are extracted without writing legacy globals.
 */
eEdTrackLoadResult ed_track_file_stage(
    const char *szPath,
    tEdTrackStage *pStage,
    char *szError,
    size_t uiErrorCapacity);

void ed_track_document_init(tEdTrackDocument *pDocument);
void ed_track_document_dispose(tEdTrackDocument *pDocument);

/*
 * Transactional editor spike boundary. A successful stage atomically
 * replaces the committed document and advances uiGeneration. A rejected
 * stage leaves the last good document and generation unchanged.
 */
eEdTrackLoadResult ed_track_document_reload(
    tEdTrackDocument *pDocument,
    const char *szPath,
    char *szError,
    size_t uiErrorCapacity);

const char *ed_track_load_result_name(eEdTrackLoadResult eResult);

/* Deterministic leak accounting for the soak on platforms without ASan. */
size_t ed_track_loader_live_allocations(void);
size_t ed_track_loader_live_bytes(void);

#endif
