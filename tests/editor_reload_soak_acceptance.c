/*
 * E1-S9 -- reload robustness.
 *
 * Hundreds of load/render/reload cycles through the real facade, with a
 * malformed file in every cycle, asserting that the process returns to the
 * same steady state each time. What that buys over the E0-S7 lifecycle suite
 * is repetition against a *real* track and a *real* renderer: the lifecycle
 * tests prove each transition is correct once, and this proves nothing
 * accumulates when the editor does what it actually does all day -- serialize,
 * reload, render, and occasionally hand a broken file to the loader.
 *
 * Three independent leak detectors, because they catch different things:
 *
 *   1. The sanitizer. Under `-Dvalgrind` this executable is the soak the E6-S5
 *      job runs, and Valgrind owns "no leaks" in the malloc sense.
 *   2. Renderer texture accounting. Every texture slot table is bounded, so a
 *      reload that failed to release its predecessor's atlas shows up as a
 *      count that climbs. Valgrind would not call a still-reachable slot a
 *      leak; the editor would nonetheless die of it after ~16 reloads.
 *   3. Live SDL allocations across the second half of a phase. Every cycle in
 *      a phase does identical work, so a steady state must be flat there. This
 *      is the "or platform equivalent" half of the acceptance criterion: it
 *      is the only one of the three that works on Windows, where Valgrind
 *      does not run. SDL's own SDL_GetNumAllocations is compiled out of the
 *      vendored build (SDL_TRACK_ALLOCATION_COUNT is not defined), so the
 *      count comes from allocator hooks installed here instead -- which also
 *      makes it independent of how SDL was configured. Asserted for software,
 *      reported for GPU; see soak_run_phase for why.
 *
 * ROLLER_SOAK_TRACE=1 in the environment prints the live count after every
 * cycle, which is how the shape of any growth gets identified rather than
 * guessed at.
 *
 * Frame checksums are asserted byte-exact for software mode only. E1-S7 made
 * that distinction deliberately -- software output is pixel-exact, GPU output
 * is not claimed to be -- so GPU frames are checked for content and reported,
 * not compared.
 *
 * usage: editor_reload_soak_acceptance TRACK ASSET_ROOT SCRATCH_DIR [CYCLES]
 */

#include "3d.h"
#include "editor_api.h"
#include "editor_track_loader.h"
#include "game_render.h"
#include "graphics.h"
#include "horizon.h"
#include "scene_render.h"
#include "tower.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SOAK_WIDTH = 320,
    SOAK_HEIGHT = 240,
    SOAK_ROW_PITCH = SOAK_WIDTH * 4,
    SOAK_BUFFER_SIZE = SOAK_ROW_PITCH * SOAK_HEIGHT,
    SOAK_DEFAULT_CYCLES = 250,
    SOAK_MALFORMED_COUNT = 4,
    SOAK_TOWER_INPUT_COUNT = MAX_TOWERS + 2,
    /* Full geometry extraction is two whole-track traversals, so it rides
     * along periodically rather than every cycle. */
    SOAK_FILL_INTERVAL = 8,
    SOAK_UNLOAD_INTERVAL = 16
};

typedef struct
{
    char szTrackPath[1024];
    char szAssetRoot[1024];
    char szScratchDir[1024];
    int iCycles;
    char szMalformed[SOAK_MALFORMED_COUNT][1024];
    char szTowerLimitTrack[1024];
    char szError[512];
    int iResult;

    uint8_t *pPixels;
    tEdCameraState Camera;

    /* Established by the first successful cycle and never moved afterwards. */
    int bHaveTrackBaseline;
    uint32_t uiBaselineVertexCount;
    uint32_t uiBaselineIndexCount;
    uint32_t uiBaselinePrimitiveCount;
    uint32_t uiBaselineMaterialCount;

    /* The first software frame. Phase C compares against this one, so a GPU
     * excursion that left the software path disturbed is a failure. */
    int bHaveSoftwareChecksum;
    uint64_t ullSoftwareChecksum;

    /* Reporting. */
    int iValidLoads;
    int iRefusedLoads;
    int iFrames;
    int iFills;
    int iUnloads;
} tSoakContext;

/*
 * Allocator hooks. SDL's own counter is compiled out of the vendored build, so
 * count here instead. SDL normalizes before it calls these -- free is never
 * handed NULL and realloc's size is never zero -- so the arithmetic is the
 * same as SDL's own: a block is born when a hook returns memory it was not
 * given, and dies in free.
 */
static SDL_AtomicInt s_LiveAllocations;

static void *SDLCALL soak_malloc(size_t uiSize)
{
    void *pMemory = malloc(uiSize);

    if (pMemory)
        SDL_AtomicIncRef(&s_LiveAllocations);
    return pMemory;
}

static void *SDLCALL soak_calloc(size_t uiCount, size_t uiSize)
{
    void *pMemory = calloc(uiCount, uiSize);

    if (pMemory)
        SDL_AtomicIncRef(&s_LiveAllocations);
    return pMemory;
}

static void *SDLCALL soak_realloc(void *pMemory, size_t uiSize)
{
    void *pResult = realloc(pMemory, uiSize);

    if (pResult && !pMemory)
        SDL_AtomicIncRef(&s_LiveAllocations);
    return pResult;
}

static void SDLCALL soak_free(void *pMemory)
{
    if (!pMemory)
        return;
    SDL_AtomicDecRef(&s_LiveAllocations);
    free(pMemory);
}

static int soak_live_allocations(void)
{
    return SDL_GetAtomicInt(&s_LiveAllocations);
}

static void soak_fail(tSoakContext *pContext, const char *szFormat, ...)
{
    va_list Args;

    if (pContext->iResult)
        return;
    va_start(Args, szFormat);
    vsnprintf(pContext->szError, sizeof(pContext->szError), szFormat, Args);
    va_end(Args);
    pContext->szError[sizeof(pContext->szError) - 1u] = '\0';
    pContext->iResult = 1;
}

/*
 * The facade requires absolute paths (F-S2), and -Dassets-path is whatever the
 * caller typed -- CI passes a relative one. Resolving here rather than
 * demanding an absolute argument keeps the CI invocation the same shape as
 * every other assets-path consumer's.
 */
static int soak_absolute_path(const char *szPath, char *szOut,
                              size_t uiCapacity)
{
    char *szWorkingDirectory;

    if (szPath[0] == '/' || szPath[0] == '\\'
            || (szPath[0] != '\0' && szPath[1] == ':')) {
        snprintf(szOut, uiCapacity, "%s", szPath);
        return 1;
    }
    szWorkingDirectory = SDL_GetCurrentDirectory();
    if (!szWorkingDirectory)
        return 0;
    /* SDL guarantees the trailing separator. */
    snprintf(szOut, uiCapacity, "%s%s", szWorkingDirectory, szPath);
    SDL_free(szWorkingDirectory);
    return 1;
}

static uint64_t soak_checksum(const uint8_t *pBytes, size_t uiSize)
{
    uint64_t ullHash = 1469598103934665603ull;

    for (size_t uiOffset = 0u; uiOffset < uiSize; ++uiOffset) {
        ullHash ^= pBytes[uiOffset];
        ullHash *= 1099511628211ull;
    }
    return ullHash;
}

static int soak_frame_has_content(const uint8_t *pPixels, size_t uiSize)
{
    uint32_t uiFirst;

    if (uiSize < sizeof(uiFirst))
        return 0;
    memcpy(&uiFirst, pPixels, sizeof(uiFirst));
    for (size_t uiOffset = sizeof(uiFirst);
         uiOffset + sizeof(uint32_t) <= uiSize;
         uiOffset += sizeof(uint32_t)) {
        uint32_t uiPixel;

        memcpy(&uiPixel, pPixels + uiOffset, sizeof(uiPixel));
        if (uiPixel != uiFirst)
            return 1;
    }
    return 0;
}

static int soak_write_file(tSoakContext *pContext, const char *szPath,
                           const uint8_t *pBytes, size_t uiSize)
{
    FILE *pFile = fopen(szPath, "wb");

    if (!pFile) {
        soak_fail(pContext, "could not create %s", szPath);
        return 0;
    }
    if (uiSize > 0u && fwrite(pBytes, 1u, uiSize, pFile) != uiSize) {
        fclose(pFile);
        soak_fail(pContext, "could not write %s", szPath);
        return 0;
    }
    fclose(pFile);
    return 1;
}

/*
 * The malformed inputs are derived from the real track rather than checked in,
 * so they stay malformed in exactly the ways a real file goes wrong: cut
 * short, corrupted mid-stream, empty, and absent. The last one is never
 * created; a path that does not exist is the failure a host hits most often.
 */
static int soak_build_malformed_inputs(tSoakContext *pContext)
{
    uint8_t *pBytes = NULL;
    size_t uiSize = 0u;
    FILE *pFile = fopen(pContext->szTrackPath, "rb");
    int bOk = 0;

    if (!pFile) {
        soak_fail(pContext, "could not open %s", pContext->szTrackPath);
        return 0;
    }
    if (fseek(pFile, 0, SEEK_END) != 0) {
        fclose(pFile);
        soak_fail(pContext, "could not measure %s", pContext->szTrackPath);
        return 0;
    }
    {
        long lSize = ftell(pFile);

        if (lSize <= 16) {
            fclose(pFile);
            soak_fail(pContext, "%s is too small to derive malformed inputs",
                      pContext->szTrackPath);
            return 0;
        }
        uiSize = (size_t)lSize;
    }
    rewind(pFile);
    pBytes = (uint8_t *)malloc(uiSize);
    if (!pBytes) {
        fclose(pFile);
        soak_fail(pContext, "could not buffer %s", pContext->szTrackPath);
        return 0;
    }
    if (fread(pBytes, 1u, uiSize, pFile) != uiSize) {
        fclose(pFile);
        free(pBytes);
        soak_fail(pContext, "could not read %s", pContext->szTrackPath);
        return 0;
    }
    fclose(pFile);

    snprintf(pContext->szMalformed[0], sizeof(pContext->szMalformed[0]),
             "%s/soak_truncated.trk", pContext->szScratchDir);
    snprintf(pContext->szMalformed[1], sizeof(pContext->szMalformed[1]),
             "%s/soak_corrupt.trk", pContext->szScratchDir);
    snprintf(pContext->szMalformed[2], sizeof(pContext->szMalformed[2]),
             "%s/soak_empty.trk", pContext->szScratchDir);
    snprintf(pContext->szMalformed[3], sizeof(pContext->szMalformed[3]),
             "%s/soak_missing.trk", pContext->szScratchDir);

    if (!soak_write_file(pContext, pContext->szMalformed[0], pBytes,
                         uiSize / 2u))
        goto done;
    /* Corrupt the compressed body, not the header: the decoder has to reject
     * this one on its own bounds rather than on a size field. */
    for (size_t uiOffset = uiSize / 4u; uiOffset < uiSize; uiOffset += 7u)
        pBytes[uiOffset] = (uint8_t)(pBytes[uiOffset] ^ 0xA5u);
    if (!soak_write_file(pContext, pContext->szMalformed[1], pBytes, uiSize))
        goto done;
    if (!soak_write_file(pContext, pContext->szMalformed[2], pBytes, 0u))
        goto done;
    remove(pContext->szMalformed[3]);
    bOk = 1;

done:
    free(pBytes);
    return bOk;
}

static int soak_take_text_line(const uint8_t **ppbyCursor,
                               const uint8_t *pbyEnd,
                               const uint8_t **ppbyLine,
                               size_t *puiLineLength)
{
    const uint8_t *pbyCursor = *ppbyCursor;
    const uint8_t *pbyLine = pbyCursor;

    if (pbyCursor >= pbyEnd)
        return 0;
    while (pbyCursor < pbyEnd && *pbyCursor != '\r'
            && *pbyCursor != '\n' && *pbyCursor != 0x1au)
        ++pbyCursor;
    *ppbyLine = pbyLine;
    *puiLineLength = (size_t)(pbyCursor - pbyLine);
    while (pbyCursor < pbyEnd
            && (*pbyCursor == '\r' || *pbyCursor == '\n'))
        ++pbyCursor;
    *ppbyCursor = pbyCursor;
    return 1;
}

static int soak_take_data_line(const uint8_t **ppbyCursor,
                               const uint8_t *pbyEnd,
                               const uint8_t **ppbyLine,
                               size_t *puiLineLength)
{
    while (soak_take_text_line(ppbyCursor, pbyEnd,
                               ppbyLine, puiLineLength)) {
        size_t uiFirst = 0u;

        while (uiFirst < *puiLineLength
                && ((*ppbyLine)[uiFirst] == ' '
                    || (*ppbyLine)[uiFirst] == '\t'))
            ++uiFirst;
        if (uiFirst == *puiLineLength || (*ppbyLine)[uiFirst] == ';'
                || (uiFirst + 1u < *puiLineLength
                    && (*ppbyLine)[uiFirst] == '/'
                    && (*ppbyLine)[uiFirst + 1u] == '/'))
            continue;
        return 1;
    }
    return 0;
}

static int soak_write_text_line(FILE *pFile, const uint8_t *pbyLine,
                                size_t uiLineLength)
{
    return fwrite(pbyLine, 1u, uiLineLength, pFile) == uiLineLength
        && fputs("\r\n", pFile) != EOF;
}

static int soak_tower_zoom(int iChunk)
{
    return (iChunk % 25) / 5;
}

static int soak_tower_mode_nibble(int iChunk)
{
    static const int aiModeNibbles[] = { 0, 1, 3, 4, 5 };

    return aiModeNibbles[iChunk % 5];
}

static int soak_tower_enabled(int iChunk)
{
    static const int aiEnabledModes[] = { -1, -4, -2, -5, -3 };

    return aiEnabledModes[iChunk % 5];
}

static int soak_write_tower_surface_record(FILE *pFile,
                                           const uint8_t *pbyLine,
                                           size_t uiLineLength,
                                           int iChunk)
{
    const uint8_t *apbyField[18];
    size_t auiFieldLength[18];
    size_t uiCursor = 0u;

    for (int iField = 0; iField < 18; ++iField) {
        size_t uiStart;

        while (uiCursor < uiLineLength
                && (pbyLine[uiCursor] == ' '
                    || pbyLine[uiCursor] == '\t'))
            ++uiCursor;
        uiStart = uiCursor;
        while (uiCursor < uiLineLength
                && pbyLine[uiCursor] != ' '
                && pbyLine[uiCursor] != '\t')
            ++uiCursor;
        if (uiCursor == uiStart)
            return 0;
        apbyField[iField] = pbyLine + uiStart;
        auiFieldLength[iField] = uiCursor - uiStart;
    }
    for (int iField = 0; iField < 18; ++iField) {
        if (iField != 0 && fputc(' ', pFile) == EOF)
            return 0;
        if (iField == 12) {
            int iSignType = 256 + 16 * soak_tower_zoom(iChunk)
                          + soak_tower_mode_nibble(iChunk);

            if (fprintf(pFile, "%d", iSignType) < 0)
                return 0;
        } else if (iField == 13) {
            if (fprintf(pFile, "%d", iChunk) < 0)
                return 0;
        } else if (iField == 14) {
            if (fprintf(pFile, "%d", -iChunk) < 0)
                return 0;
        } else if (fwrite(apbyField[iField], 1u, auiFieldLength[iField], pFile)
                != auiFieldLength[iField]) {
            return 0;
        }
    }
    return fputs("\r\n", pFile) != EOF;
}

/*
 * E7-S1. Build this at runtime so the Valgrind soak exercises the real
 * staged-loader and facade path with more authored towers than the legacy
 * fixed table can hold. The two tower-authored chunks past capacity remain
 * valid track data; only their decoded runtime towers are omitted. The first
 * 25 entries cover every E7-S3 camera-mode/zoom combination while retaining
 * the same 34-tower overflow boundary.
 */
static int soak_build_tower_limit_input(tSoakContext *pContext)
{
    tEdTrackStage Stage;
    char szStageError[256];
    const uint8_t *pbyCursor;
    const uint8_t *pbyEnd;
    const uint8_t *pbyLine;
    size_t uiLineLength;
    FILE *pFile = NULL;
    int bOk = 1;

    ed_track_stage_init(&Stage);
    if (ed_track_file_stage(pContext->szTrackPath, &Stage,
                            szStageError, sizeof(szStageError))
            != ED_TRACK_LOAD_OK) {
        soak_fail(pContext, "could not stage tower-limit source: %s",
                  szStageError);
        return 0;
    }
    if (Stage.uiChunkCount < SOAK_TOWER_INPUT_COUNT) {
        soak_fail(pContext,
                  "tower-limit source has only %u chunks; need at least %d",
                  Stage.uiChunkCount, SOAK_TOWER_INPUT_COUNT);
        ed_track_stage_dispose(&Stage);
        return 0;
    }

    snprintf(pContext->szTowerLimitTrack,
             sizeof(pContext->szTowerLimitTrack),
             "%s/e7_s1_tower_limit.trk", pContext->szScratchDir);
    pFile = fopen(pContext->szTowerLimitTrack, "wb");
    if (!pFile) {
        soak_fail(pContext, "could not create %s",
                  pContext->szTowerLimitTrack);
        ed_track_stage_dispose(&Stage);
        return 0;
    }

    pbyCursor = Stage.pbyData;
    pbyEnd = Stage.pbyData + Stage.uiDataLength;
    if (!soak_take_data_line(&pbyCursor, pbyEnd,
                             &pbyLine, &uiLineLength)) {
        bOk = 0;
    } else {
        size_t uiLeadingWhitespace = 0u;

        while (uiLeadingWhitespace < uiLineLength
                && (pbyLine[uiLeadingWhitespace] == ' '
                    || pbyLine[uiLeadingWhitespace] == '\t'))
            ++uiLeadingWhitespace;
        bOk = fputs("  ", pFile) != EOF
           && soak_write_text_line(
               pFile, pbyLine + uiLeadingWhitespace,
               uiLineLength - uiLeadingWhitespace);
    }
    for (uint32_t uiChunk = 0u; uiChunk < Stage.uiChunkCount && bOk;
         ++uiChunk) {
        bOk = soak_take_data_line(&pbyCursor, pbyEnd,
                                  &pbyLine, &uiLineLength)
           && soak_write_text_line(pFile, pbyLine, uiLineLength)
           && soak_take_data_line(&pbyCursor, pbyEnd,
                                  &pbyLine, &uiLineLength);
        if (bOk && uiChunk < SOAK_TOWER_INPUT_COUNT) {
            bOk = soak_write_tower_surface_record(
                pFile, pbyLine, uiLineLength, (int)uiChunk);
        } else if (bOk) {
            bOk = soak_write_text_line(pFile, pbyLine, uiLineLength);
        }
        bOk = bOk
           && soak_take_data_line(&pbyCursor, pbyEnd,
                                  &pbyLine, &uiLineLength)
           && soak_write_text_line(pFile, pbyLine, uiLineLength);
    }
    if (bOk && fwrite(pbyCursor, 1u, (size_t)(pbyEnd - pbyCursor), pFile)
            != (size_t)(pbyEnd - pbyCursor))
        bOk = 0;
    if (fclose(pFile) != 0)
        bOk = 0;
    ed_track_stage_dispose(&Stage);
    if (!bOk) {
        soak_fail(pContext, "could not write %s",
                  pContext->szTowerLimitTrack);
        return 0;
    }
    return 1;
}

static int soak_verify_tower_limit(tSoakContext *pContext)
{
    uint32_t uiTowerCount = 0u;

    if (RollerEd_LoadTrackFile(pContext->szTowerLimitTrack,
                               pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "tower-limit track load failed: %s",
                  RollerEd_GetLastError());
        return 0;
    }
    if (NumTowers != MAX_TOWERS) {
        soak_fail(pContext, "tower-limit track decoded %d towers; expected %d",
                  NumTowers, MAX_TOWERS);
        return 0;
    }
    if (RollerEd_QueryTowerCount(&uiTowerCount) != ROLLER_ED_RESULT_OK
            || uiTowerCount != MAX_TOWERS) {
        soak_fail(pContext,
                  "tower query returned %u entries; expected %d: %s",
                  uiTowerCount, MAX_TOWERS, RollerEd_GetLastError());
        return 0;
    }
    for (int iTower = 0; iTower < MAX_TOWERS; ++iTower) {
        tEdTowerInfo Info = {
            .uiStructSize = sizeof(Info),
            .uiVersion = ROLLER_ED_TOWER_INFO_VERSION
        };
        int iChunkIdx = TowerBase[iTower].iChunkIdx;

        if (TowerBase[iTower].iChunkIdx != iTower
                || TowerBase[iTower].iHOffset != iTower
                || TowerBase[iTower].iVOffset != -iTower
                || TowerBase[iTower].iEnabled != soak_tower_enabled(iTower)
                || TowerBase[iTower].iTowerType != soak_tower_zoom(iTower)
                || TowerSect[iTower] != iTower) {
            soak_fail(pContext,
                      "tower %d did not preserve its decoded fields",
                      iTower);
            return 0;
        }
        if (RollerEd_QueryTower((uint32_t)iTower, &Info)
                != ROLLER_ED_RESULT_OK
                || Info.uiStructSize != sizeof(Info)
                || Info.uiVersion != ROLLER_ED_TOWER_INFO_VERSION
                || Info.uiChunkId != (uint32_t)iChunkIdx
                || Info.fWorldPosition[0] != TowerX[iTower]
                || Info.fWorldPosition[1] != TowerY[iTower]
                || Info.fWorldPosition[2] != TowerZ[iTower]
                || Info.fAnchorPosition[0]
                    != -localdata[iChunkIdx].pointAy[3].fX
                || Info.fAnchorPosition[1]
                    != -localdata[iChunkIdx].pointAy[3].fY
                || Info.fAnchorPosition[2]
                    != -localdata[iChunkIdx].pointAy[3].fZ) {
            soak_fail(pContext,
                      "tower query %d disagreed with InitTowers output",
                      iTower);
            return 0;
        }
    }
    for (int iChunk = MAX_TOWERS;
         iChunk < SOAK_TOWER_INPUT_COUNT; ++iChunk) {
        if (TowerSect[iChunk] != -1) {
            soak_fail(pContext,
                      "overflow tower on chunk %d entered the runtime table",
                      iChunk);
            return 0;
        }
    }
    {
        tEdTowerInfo Info;
        tEdTowerInfo Before;

        memset(&Info, 0xc7, sizeof(Info));
        Info.uiStructSize = sizeof(Info);
        Info.uiVersion = ROLLER_ED_TOWER_INFO_VERSION;
        Before = Info;
        if (RollerEd_QueryTower(uiTowerCount, &Info)
                != ROLLER_ED_RESULT_INVALID_ARGUMENT
                || memcmp(&Info, &Before, sizeof(Before)) != 0) {
            soak_fail(pContext,
                      "out-of-range tower query changed caller output");
            return 0;
        }
    }
    printf("  E7-S1/S2/S3: queried the first %d of %d authored towers "
           "across all 25 mode/zoom combinations\n",
           NumTowers, SOAK_TOWER_INPUT_COUNT);
    return 1;
}

static int soak_query(tSoakContext *pContext, tEdGeometrySizes *pSizes,
                      const char *szPhase)
{
    memset(pSizes, 0, sizeof(*pSizes));
    pSizes->uiStructSize = sizeof(*pSizes);
    pSizes->uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    if (RollerEd_QueryGeometrySizes(pSizes) != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "%s: geometry query failed: %s", szPhase,
                  RollerEd_GetLastError());
        return 0;
    }
    return 1;
}

/* AD-7d: query stays readable in every scene state, and a state with nothing
 * to extract publishes zero counts rather than a stale set. */
static int soak_counts_are_zero(const tEdGeometrySizes *pSizes)
{
    return pSizes->uiVertexCount == 0u && pSizes->uiIndexCount == 0u
        && pSizes->uiPrimitiveCount == 0u && pSizes->uiMaterialCount == 0u;
}

static int soak_fill_geometry(tSoakContext *pContext,
                              const tEdGeometrySizes *pSizes,
                              const char *szPhase)
{
    tEdVertex *pVertices = (tEdVertex *)malloc(
        (size_t)pSizes->uiVertexCount * sizeof(tEdVertex));
    uint32_t *puiIndices = (uint32_t *)malloc(
        (size_t)pSizes->uiIndexCount * sizeof(uint32_t));
    tEdPrimitive *pPrimitives = (tEdPrimitive *)malloc(
        (size_t)pSizes->uiPrimitiveCount * sizeof(tEdPrimitive));
    tEdMaterial *pMaterials = (tEdMaterial *)malloc(
        (size_t)pSizes->uiMaterialCount * sizeof(tEdMaterial));
    int bOk = 0;

    if (!pVertices || !puiIndices || !pPrimitives || !pMaterials) {
        soak_fail(pContext, "%s: geometry buffer allocation failed", szPhase);
        goto done;
    }
    if (RollerEd_FillGeometry(
            pSizes->uiGeometryEpoch,
            pVertices, pSizes->uiVertexCount,
            puiIndices, pSizes->uiIndexCount,
            pPrimitives, pSizes->uiPrimitiveCount,
            pMaterials, pSizes->uiMaterialCount) != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "%s: geometry fill failed: %s", szPhase,
                  RollerEd_GetLastError());
        goto done;
    }
    bOk = 1;

done:
    free(pVertices);
    free(puiIndices);
    free(pPrimitives);
    free(pMaterials);
    return bOk;
}

static void soak_texture_counts(SceneRenderTextureCounts *pCounts)
{
    memset(pCounts, 0, sizeof(*pCounts));
    game_render_get_texture_counts(g_pGameRenderer, pCounts);
}

/* Clouds are part of the editor frame, not extracted track geometry. Assert
 * both halves of their lifecycle seam after every reload: GENTEX.DRH owns
 * texture-bank slot 18, and initclouds() populated all 40 sky quads once. */
static int soak_clouds_ready(tSoakContext *pContext, const char *szPhase)
{
    if (num_textures[18] < 13
            || game_render_get_texture_handle(g_pGameRenderer, 18)
                == TEXTURE_HANDLE_INVALID) {
        soak_fail(pContext, "%s: generic cloud texture bank is unavailable",
                  szPhase);
        return 0;
    }
    for (int iCloud = 0; iCloud < 40; ++iCloud) {
        const int iTile = cloud[iCloud].iSurfaceType
            & SURFACE_MASK_TEXTURE_INDEX;
        if (cloud[iCloud].fRadius < 1000000.0f
                || cloud[iCloud].fRadius > 1800000.0f
                || iTile < 8 || iTile > 12) {
            soak_fail(pContext,
                      "%s: cloud %d was not initialized (radius %.1f, tile %d)",
                      szPhase, iCloud, cloud[iCloud].fRadius, iTile);
            return 0;
        }
    }
    return 1;
}

static int soak_counts_match(const SceneRenderTextureCounts *pLeft,
                             const SceneRenderTextureCounts *pRight)
{
    return pLeft->softwareSlots == pRight->softwareSlots
        && pLeft->gpuSlots == pRight->gpuSlots
        && pLeft->gpuTextures == pRight->gpuTextures;
}

static int soak_render(tSoakContext *pContext, const char *szPhase)
{
    memset(pContext->pPixels, 0x5A, SOAK_BUFFER_SIZE);
    if (RollerEd_RenderFrame(pContext->pPixels, SOAK_BUFFER_SIZE,
                             SOAK_ROW_PITCH, SOAK_WIDTH, SOAK_HEIGHT,
                             ROLLER_ED_PIXEL_RGBA8) != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "%s: render failed: %s", szPhase,
                  RollerEd_GetLastError());
        return 0;
    }
    pContext->iFrames++;
    if (!soak_frame_has_content(pContext->pPixels, SOAK_BUFFER_SIZE)) {
        soak_fail(pContext, "%s: render produced a uniform frame", szPhase);
        return 0;
    }
    return 1;
}

/* The camera is fixed for the whole soak so every software frame is comparable
 * with every other one. It looks at the first chunk from the same offset the
 * E1-S8 acceptance uses. */
static void soak_place_camera(tSoakContext *pContext)
{
    float fX = 0.0f;
    float fY = 0.0f;
    float fZ = 0.0f;

    for (int iPoint = 0; iPoint < 6; ++iPoint) {
        fX += TrakPt[0].pointAy[iPoint].fX;
        fY += TrakPt[0].pointAy[iPoint].fY;
        fZ += TrakPt[0].pointAy[iPoint].fZ;
    }
    memset(&pContext->Camera, 0, sizeof(pContext->Camera));
    pContext->Camera.uiStructSize = sizeof(pContext->Camera);
    pContext->Camera.uiVersion = ROLLER_ED_CAMERA_STATE_VERSION;
    pContext->Camera.fPosition[0] = fX / 6.0f - 4000.0f;
    pContext->Camera.fPosition[1] = fY / 6.0f;
    pContext->Camera.fPosition[2] = fZ / 6.0f + 1600.0f;
    pContext->Camera.fYawDegrees = 0.0f;
    pContext->Camera.fPitchDegrees = -25.0f;
}

static int soak_load_valid(tSoakContext *pContext, const char *szPhase,
                           tEdGeometrySizes *pSizesOut)
{
    tEdGeometrySizes Before;
    tEdGeometrySizes After;

    if (!soak_query(pContext, &Before, szPhase))
        return 0;
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "%s: valid load failed: %s", szPhase,
                  RollerEd_GetLastError());
        return 0;
    }
    pContext->iValidLoads++;
    if (!soak_query(pContext, &After, szPhase))
        return 0;
    if (After.uiSceneState != ROLLER_ED_SCENE_READY) {
        soak_fail(pContext, "%s: scene state %u after a successful load",
                  szPhase, After.uiSceneState);
        return 0;
    }
    if (After.uiTrackGeneration != Before.uiTrackGeneration + 1u) {
        soak_fail(pContext,
                  "%s: track generation went %u -> %u across one load",
                  szPhase, Before.uiTrackGeneration, After.uiTrackGeneration);
        return 0;
    }
    if (After.uiGeometryEpoch == Before.uiGeometryEpoch) {
        soak_fail(pContext, "%s: a load left the geometry epoch at %u",
                  szPhase, After.uiGeometryEpoch);
        return 0;
    }

    if (!pContext->bHaveTrackBaseline) {
        pContext->uiBaselineVertexCount = After.uiVertexCount;
        pContext->uiBaselineIndexCount = After.uiIndexCount;
        pContext->uiBaselinePrimitiveCount = After.uiPrimitiveCount;
        pContext->uiBaselineMaterialCount = After.uiMaterialCount;
        pContext->bHaveTrackBaseline = 1;
    } else if (After.uiVertexCount != pContext->uiBaselineVertexCount
            || After.uiIndexCount != pContext->uiBaselineIndexCount
            || After.uiPrimitiveCount != pContext->uiBaselinePrimitiveCount
            || After.uiMaterialCount != pContext->uiBaselineMaterialCount) {
        soak_fail(pContext,
                  "%s: reloading the same track produced %u/%u/%u/%u instead "
                  "of %u/%u/%u/%u vertices/indices/primitives/materials",
                  szPhase, After.uiVertexCount, After.uiIndexCount,
                  After.uiPrimitiveCount, After.uiMaterialCount,
                  pContext->uiBaselineVertexCount,
                  pContext->uiBaselineIndexCount,
                  pContext->uiBaselinePrimitiveCount,
                  pContext->uiBaselineMaterialCount);
        return 0;
    }
    *pSizesOut = After;
    return 1;
}

/*
 * A malformed file must fail, must land the facade in SCENE_FAILED with a
 * refused render, and must leave the committed track generation exactly where
 * it was -- AD-7d's hole, and the reason the fill guard is on the epoch rather
 * than the generation.
 */
static int soak_load_malformed(tSoakContext *pContext, const char *szPath,
                               const char *szPhase)
{
    tEdGeometrySizes Before;
    tEdGeometrySizes After;

    if (!soak_query(pContext, &Before, szPhase))
        return 0;
    if (RollerEd_LoadTrackFile(szPath, pContext->szAssetRoot)
            == ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "%s: malformed input %s loaded successfully",
                  szPhase, szPath);
        return 0;
    }
    pContext->iRefusedLoads++;
    if (!soak_query(pContext, &After, szPhase))
        return 0;
    if (After.uiSceneState != ROLLER_ED_SCENE_FAILED) {
        soak_fail(pContext, "%s: scene state %u after a refused load", szPhase,
                  After.uiSceneState);
        return 0;
    }
    if (After.uiTrackGeneration != Before.uiTrackGeneration) {
        soak_fail(pContext,
                  "%s: a refused load advanced the track generation %u -> %u",
                  szPhase, Before.uiTrackGeneration, After.uiTrackGeneration);
        return 0;
    }
    if (After.uiGeometryEpoch == Before.uiGeometryEpoch) {
        soak_fail(pContext,
                  "%s: a refused load left the geometry epoch at %u so a stale "
                  "fill would still be accepted", szPhase,
                  After.uiGeometryEpoch);
        return 0;
    }
    if (!soak_counts_are_zero(&After)) {
        soak_fail(pContext, "%s: a failed scene still publishes geometry",
                  szPhase);
        return 0;
    }
    if (RollerEd_RenderFrame(pContext->pPixels, SOAK_BUFFER_SIZE,
                             SOAK_ROW_PITCH, SOAK_WIDTH, SOAK_HEIGHT,
                             ROLLER_ED_PIXEL_RGBA8)
            != ROLLER_ED_RESULT_NO_SCENE) {
        soak_fail(pContext, "%s: a failed scene still rendered", szPhase);
        return 0;
    }
    return 1;
}

static int soak_unload(tSoakContext *pContext, const char *szPhase)
{
    tEdGeometrySizes Sizes;

    if (RollerEd_UnloadTrack() != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "%s: unload failed: %s", szPhase,
                  RollerEd_GetLastError());
        return 0;
    }
    pContext->iUnloads++;
    if (!soak_query(pContext, &Sizes, szPhase))
        return 0;
    if (Sizes.uiSceneState != ROLLER_ED_SCENE_EMPTY
            || !soak_counts_are_zero(&Sizes)) {
        soak_fail(pContext, "%s: unload left scene state %u with %u primitives",
                  szPhase, Sizes.uiSceneState, Sizes.uiPrimitiveCount);
        return 0;
    }
    if (RollerEd_RenderFrame(pContext->pPixels, SOAK_BUFFER_SIZE,
                             SOAK_ROW_PITCH, SOAK_WIDTH, SOAK_HEIGHT,
                             ROLLER_ED_PIXEL_RGBA8)
            != ROLLER_ED_RESULT_NO_SCENE) {
        soak_fail(pContext, "%s: an unloaded scene still rendered", szPhase);
        return 0;
    }
    return 1;
}

static int soak_run_phase(tSoakContext *pContext, eRollerEdRenderer eRenderer,
                          int iCycles, const char *szPhase)
{
    const int bSoftware = eRenderer == ROLLER_ED_RENDERER_SOFTWARE;
    SceneRenderTextureCounts Baseline = { 0, 0, 0 };
    int bHaveTextureBaseline = 0;
    int iAllocationsAtMidpoint = soak_live_allocations();
    int iAllocationsAtEnd;

    if (RollerEd_SelectRenderer(eRenderer) != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "%s: renderer selection failed: %s", szPhase,
                  RollerEd_GetLastError());
        return 0;
    }

    for (int iCycle = 0; iCycle < iCycles; ++iCycle) {
        tEdGeometrySizes Sizes;
        SceneRenderTextureCounts Counts;

        if (!soak_load_malformed(
                pContext, pContext->szMalformed[iCycle % SOAK_MALFORMED_COUNT],
                szPhase))
            return 0;
        if (!soak_load_valid(pContext, szPhase, &Sizes))
            return 0;
        if (!soak_clouds_ready(pContext, szPhase))
            return 0;

        if (iCycle == 0)
            soak_place_camera(pContext);
        if (RollerEd_SetCamera(&pContext->Camera) != ROLLER_ED_RESULT_OK) {
            soak_fail(pContext, "%s: camera update failed: %s", szPhase,
                      RollerEd_GetLastError());
            return 0;
        }
        if (!soak_render(pContext, szPhase))
            return 0;

        if (bSoftware) {
            uint64_t ullChecksum = soak_checksum(pContext->pPixels,
                                                 SOAK_BUFFER_SIZE);

            if (!pContext->bHaveSoftwareChecksum) {
                pContext->ullSoftwareChecksum = ullChecksum;
                pContext->bHaveSoftwareChecksum = 1;
                /* Printed, not just held: it is the cheapest pixel-level
                 * regression check this repository has for a change to the
                 * track loader, and it costs one line of output. */
                printf("  %s: software frame checksum %016llx\n", szPhase,
                       (unsigned long long)ullChecksum);
            } else if (ullChecksum != pContext->ullSoftwareChecksum) {
                soak_fail(pContext,
                          "%s cycle %d: software frame changed after a reload "
                          "(%016llx, expected %016llx)", szPhase, iCycle,
                          (unsigned long long)ullChecksum,
                          (unsigned long long)pContext->ullSoftwareChecksum);
                return 0;
            }
        }

        soak_texture_counts(&Counts);
        if (!bHaveTextureBaseline) {
            Baseline = Counts;
            bHaveTextureBaseline = 1;
        } else if (!soak_counts_match(&Counts, &Baseline)) {
            soak_fail(pContext,
                      "%s cycle %d: renderer texture resources drifted to "
                      "%d/%d/%d from %d/%d/%d (software slots / GPU slots / "
                      "GPU textures)", szPhase, iCycle, Counts.softwareSlots,
                      Counts.gpuSlots, Counts.gpuTextures,
                      Baseline.softwareSlots, Baseline.gpuSlots,
                      Baseline.gpuTextures);
            return 0;
        }

        if (iCycle % SOAK_FILL_INTERVAL == 0) {
            if (!soak_fill_geometry(pContext, &Sizes, szPhase))
                return 0;
            pContext->iFills++;
        }
        if (iCycle % SOAK_UNLOAD_INTERVAL == SOAK_UNLOAD_INTERVAL - 1) {
            if (!soak_unload(pContext, szPhase))
                return 0;
        }
        /* Measured from the midpoint rather than the start: the first cycles
         * of a phase legitimately populate caches, and only the flat part
         * afterwards distinguishes a warm-up from a leak. */
        if (iCycle == iCycles / 2)
            iAllocationsAtMidpoint = soak_live_allocations();
        if (getenv("ROLLER_SOAK_TRACE"))
            printf("    %s cycle %d: live=%d\n", szPhase, iCycle,
                   soak_live_allocations());
    }

    iAllocationsAtEnd = soak_live_allocations();
    printf("  %s: live SDL allocations %d -> %d over the second half\n",
           szPhase, iAllocationsAtMidpoint, iAllocationsAtEnd);
    /*
     * Asserted while no GPU backend exists, reported once one does. The pure
     * software path frees everything it takes inside the call that took it and
     * measures dead flat over hundreds of cycles, so any growth there is a
     * defect. Once a GPU backend is attached the test is on the wrong side of
     * SDL and the driver, which defer destruction to fence retirement and pool
     * transfer buffers and descriptors: the observed trace is long flats
     * punctuated by pool-sized jumps, a shape no threshold separates from a
     * slow leak without inventing one. Note the predicate is the backend, not
     * the selected renderer -- selecting software back does not detach the GPU
     * backend, and texture loads keep it synchronized (E1-S8), so a
     * software-after-GPU phase is still allocating through it. Stale GPU
     * resources are caught exactly, and per cycle, by the texture slot and
     * texture object counts above; those are ROLLER's own accounting.
     */
    if (!game_render_get_gpu(g_pGameRenderer)
            && iAllocationsAtEnd > iAllocationsAtMidpoint) {
        soak_fail(pContext,
                  "%s: live SDL allocations grew from %d to %d over %d "
                  "identical cycles", szPhase, iAllocationsAtMidpoint,
                  iAllocationsAtEnd, iCycles - iCycles / 2);
        return 0;
    }
    return 1;
}

static int SDLCALL soak_worker(void *pUserData)
{
    tSoakContext *pContext = (tSoakContext *)pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE,
        .uiAllowSoftwareFallback = 0u
    };
    uint32_t uiAvailable;
    int iGPUCycles;

    if (!soak_build_malformed_inputs(pContext)
            || !soak_build_tower_limit_input(pContext))
        return pContext->iResult;

    pContext->pPixels = (uint8_t *)malloc(SOAK_BUFFER_SIZE);
    if (!pContext->pPixels) {
        soak_fail(pContext, "soak pixel allocation failed");
        return pContext->iResult;
    }

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        soak_fail(pContext, "RollerEd_Init failed: %s", RollerEd_GetLastError());
        goto done;
    }
    if (!soak_verify_tower_limit(pContext))
        goto shutdown;
    uiAvailable = RollerEd_GetAvailableRenderers();

    if (!soak_run_phase(pContext, ROLLER_ED_RENDERER_SOFTWARE,
                        pContext->iCycles, "software"))
        goto shutdown;

    /* The GPU leg is opportunistic: hosted CI runners under Valgrind have no
     * GPU, and the acceptance criterion is about resources, not about which
     * backend owns them. Where a GPU exists it is the only place stale GPU
     * resources can actually be observed, so it runs. */
    iGPUCycles = pContext->iCycles / 4;
    if (iGPUCycles < 8)
        iGPUCycles = 8;
    if ((uiAvailable & ROLLER_ED_RENDERER_GPU) != 0u) {
        /* Availability is a capability answer, not a promise that a device
         * can be created: a hosted runner with a Vulkan loader and no driver
         * advertises GPU and then refuses it. E1-S8 already says a refused
         * selection keeps the previous renderer, so treat that as a skip
         * rather than failing a nightly for the host's configuration. */
        eRollerEdResult eProbe = RollerEd_SelectRenderer(ROLLER_ED_RENDERER_GPU);

        if (eProbe == ROLLER_ED_RESULT_RENDERER_UNAVAILABLE
                || eProbe == ROLLER_ED_RESULT_GPU_FAILED) {
            printf("  gpu: advertised but unusable here (%s); "
                   "software phases only\n", RollerEd_GetLastError());
        } else if (eProbe != ROLLER_ED_RESULT_OK) {
            soak_fail(pContext, "GPU renderer selection failed: %s",
                      RollerEd_GetLastError());
            goto shutdown;
        } else {
            if (!soak_run_phase(pContext, ROLLER_ED_RENDERER_GPU, iGPUCycles,
                                "gpu"))
                goto shutdown;
            /* Back to software: the first software frame must come back byte
             * for byte, which a GPU excursion that disturbed shared renderer
             * state would not manage. */
            if (!soak_run_phase(pContext, ROLLER_ED_RENDERER_SOFTWARE, 8,
                                "software-after-gpu"))
                goto shutdown;
        }
    } else {
        printf("  gpu: unavailable on this host; software phases only\n");
    }

    if (!soak_unload(pContext, "teardown"))
        goto shutdown;

shutdown:
    if (RollerEd_Shutdown() != ROLLER_ED_RESULT_OK)
        soak_fail(pContext, "RollerEd_Shutdown failed: %s",
                  RollerEd_GetLastError());
done:
    free(pContext->pPixels);
    pContext->pPixels = NULL;
    return pContext->iResult;
}

int main(int argc, char **argv)
{
    tRollerEdBootstrapInfo BootstrapInfo = {
        .uiStructSize = sizeof(BootstrapInfo),
        .uiVersion = ROLLER_ED_BOOTSTRAP_INFO_VERSION,
        .uiFlags = 0u
    };
    tSoakContext Context;
    SDL_Thread *pWorker;
    int iWorkerResult = 1;

    if (argc != 4 && argc != 5) {
        fprintf(stderr,
                "usage: %s ABSOLUTE_TRACK_PATH ABSOLUTE_ASSET_ROOT "
                "SCRATCH_DIR [CYCLES]\n", argv[0]);
        return 2;
    }
    memset(&Context, 0, sizeof(Context));
    Context.iCycles = argc == 5 ? atoi(argv[4]) : SOAK_DEFAULT_CYCLES;
    if (Context.iCycles < 1) {
        fprintf(stderr, "cycle count must be positive\n");
        return 2;
    }

    /* Before SDL_SetMainReady and before anything can allocate: SDL refuses
     * to swap allocators once it owns live blocks. */
    if (!SDL_SetMemoryFunctions(soak_malloc, soak_calloc, soak_realloc,
                                soak_free)) {
        fprintf(stderr, "could not install allocation counters: %s\n",
                SDL_GetError());
        return 1;
    }

    if (!soak_absolute_path(argv[1], Context.szTrackPath,
                            sizeof(Context.szTrackPath))
            || !soak_absolute_path(argv[2], Context.szAssetRoot,
                                   sizeof(Context.szAssetRoot))
            || !soak_absolute_path(argv[3], Context.szScratchDir,
                                   sizeof(Context.szScratchDir))) {
        fprintf(stderr, "could not resolve the soak's paths: %s\n",
                SDL_GetError());
        return 1;
    }

    SDL_SetMainReady();
    if (!SDL_CreateDirectory(Context.szScratchDir)) {
        fprintf(stderr, "could not create %s: %s\n", Context.szScratchDir,
                SDL_GetError());
        return 1;
    }
    if (RollerEd_Bootstrap(&BootstrapInfo) != ROLLER_ED_RESULT_OK) {
        /*
         * A hosted runner has no video device, and the soak needs no window:
         * software rendering completes into a caller buffer and the GPU path
         * is windowless too. So fall back to SDL's dummy driver, which is what
         * the E0-S7 lifecycle test asks for outright. Asking for it only
         * *after* a real driver has been refused is the difference that
         * matters -- on a developer machine the real driver answers, and the
         * GPU phase, the only place stale GPU resources are observable, still
         * runs. Bootstrap failed before it took ownership of anything, so the
         * retry starts from the same state the first call did.
         */
        printf("bootstrap falling back to the dummy video driver (%s)\n",
               RollerEd_GetLastError());
        /* OVERRIDE, not SDL_SetHint: SDL_GetHint prefers the environment
         * variable over a normal-priority hint, so a host with a stale
         * SDL_VIDEODRIVER set would otherwise fail the retry the same way it
         * failed the first attempt. */
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy",
                                SDL_HINT_OVERRIDE);
        if (RollerEd_Bootstrap(&BootstrapInfo) != ROLLER_ED_RESULT_OK) {
            fprintf(stderr, "RollerEd_Bootstrap failed: %s\n",
                    RollerEd_GetLastError());
            return 1;
        }
    }
    printf("E1-S9 soak: %d load/render/reload cycles per software phase\n",
           Context.iCycles);
    pWorker = SDL_CreateThread(soak_worker, "editor-reload-soak", &Context);
    if (!pWorker) {
        fprintf(stderr, "worker creation failed: %s\n", SDL_GetError());
        RollerEd_Teardown();
        return 1;
    }
    SDL_WaitThread(pWorker, &iWorkerResult);
    if (RollerEd_Teardown() != ROLLER_ED_RESULT_OK && iWorkerResult == 0) {
        fprintf(stderr, "RollerEd_Teardown failed: %s\n",
                RollerEd_GetLastError());
        return 1;
    }
    if (iWorkerResult != 0) {
        fprintf(stderr, "E1-S9 acceptance failed: %s\n", Context.szError);
        return 1;
    }
    printf("E1-S9 PASS: %d successful loads, %d refused loads, %d frames, "
           "%d geometry extractions, %d unloads left renderer resources and "
           "software output unchanged\n",
           Context.iValidLoads, Context.iRefusedLoads, Context.iFrames,
           Context.iFills, Context.iUnloads);
    return 0;
}
