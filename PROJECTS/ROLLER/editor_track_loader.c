#include "editor_track_loader.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const uint8_t *pbyData;
    size_t uiLength;
    size_t uiOffset;
    uint32_t uiLine;
} tEdTrackTextReader;

static size_t g_uiLiveAllocations;
static size_t g_uiLiveBytes;

static void ed_track_error(char *szError,
                           size_t uiErrorCapacity,
                           const char *szMessage)
{
    if (!szError || uiErrorCapacity == 0)
        return;
    if (!szMessage)
        szMessage = "";
    snprintf(szError, uiErrorCapacity, "%s", szMessage);
}

static void ed_track_error_line(char *szError,
                                size_t uiErrorCapacity,
                                uint32_t uiLine,
                                const char *szMessage)
{
    if (!szError || uiErrorCapacity == 0)
        return;
    snprintf(szError, uiErrorCapacity, "line %u: %s", uiLine, szMessage);
}

static void *ed_track_alloc(size_t uiSize)
{
    size_t *puiAllocation;

    if (uiSize > SIZE_MAX - sizeof(*puiAllocation))
        return NULL;
    puiAllocation = malloc(sizeof(*puiAllocation) + uiSize);
    if (!puiAllocation)
        return NULL;
    *puiAllocation = uiSize;
    g_uiLiveAllocations++;
    g_uiLiveBytes += uiSize;
    return puiAllocation + 1;
}

static void ed_track_free(void *pData)
{
    size_t *puiAllocation;

    if (!pData)
        return;
    puiAllocation = (size_t *)pData - 1;
    g_uiLiveAllocations--;
    g_uiLiveBytes -= *puiAllocation;
    free(puiAllocation);
}

static uint32_t ed_track_read_u32_le(const uint8_t *pbyData)
{
    return (uint32_t)pbyData[0]
         | ((uint32_t)pbyData[1] << 8)
         | ((uint32_t)pbyData[2] << 16)
         | ((uint32_t)pbyData[3] << 24);
}

static int16_t ed_track_read_i16_le(const uint8_t *pbyData)
{
    uint16_t unValue = (uint16_t)pbyData[0]
                     | ((uint16_t)pbyData[1] << 8);
    return (int16_t)unValue;
}

static void ed_track_write_i16_le(uint8_t *pbyData, int16_t nValue)
{
    uint16_t unValue = (uint16_t)nValue;
    pbyData[0] = (uint8_t)(unValue & 0xffu);
    pbyData[1] = (uint8_t)(unValue >> 8);
}

static uint64_t ed_track_hash(const uint8_t *pbyData, size_t uiLength)
{
    uint64_t ullHash = UINT64_C(14695981039346656037);

    for (size_t i = 0; i < uiLength; i++) {
        ullHash ^= pbyData[i];
        ullHash *= UINT64_C(1099511628211);
    }
    return ullHash;
}

static eEdTrackLoadResult ed_track_read_file(
    const char *szPath,
    uint8_t **ppbyFile,
    size_t *puiFileLength,
    char *szError,
    size_t uiErrorCapacity)
{
    FILE *pFile;
    long lLength;
    uint8_t *pbyFile;

    *ppbyFile = NULL;
    *puiFileLength = 0;
    pFile = fopen(szPath, "rb");
    if (!pFile) {
        ed_track_error(szError, uiErrorCapacity, "unable to open track file");
        return ED_TRACK_LOAD_IO_FAILED;
    }
    if (fseek(pFile, 0, SEEK_END) != 0
            || (lLength = ftell(pFile)) < 0
            || fseek(pFile, 0, SEEK_SET) != 0) {
        fclose(pFile);
        ed_track_error(szError, uiErrorCapacity, "unable to size track file");
        return ED_TRACK_LOAD_IO_FAILED;
    }
    if ((unsigned long)lLength > ED_TRACK_MAX_DECOMPRESSED_SIZE) {
        fclose(pFile);
        ed_track_error(szError, uiErrorCapacity, "track file is too large");
        return ED_TRACK_LOAD_INVALID_SIZE;
    }
    pbyFile = ed_track_alloc((size_t)lLength);
    if (!pbyFile && lLength != 0) {
        fclose(pFile);
        ed_track_error(szError, uiErrorCapacity, "track file allocation failed");
        return ED_TRACK_LOAD_OUT_OF_MEMORY;
    }
    if ((size_t)lLength != 0
            && fread(pbyFile, 1, (size_t)lLength, pFile) != (size_t)lLength) {
        fclose(pFile);
        ed_track_free(pbyFile);
        ed_track_error(szError, uiErrorCapacity, "track file read was truncated");
        return ED_TRACK_LOAD_IO_FAILED;
    }
    fclose(pFile);
    *ppbyFile = pbyFile;
    *puiFileLength = (size_t)lLength;
    return ED_TRACK_LOAD_OK;
}

static int ed_track_output_available(size_t uiOutput,
                                     size_t uiCapacity,
                                     size_t uiRequired)
{
    return uiRequired <= uiCapacity - uiOutput;
}

static eEdTrackLoadResult ed_track_decode_compacted(
    const uint8_t *pbySource,
    size_t uiSourceLength,
    uint8_t *pbyDestination,
    size_t uiDestinationLength,
    size_t *puiWritten,
    char *szError,
    size_t uiErrorCapacity)
{
    size_t uiSource = 4;
    size_t uiOutput = 0;

#define REQUIRE_SOURCE(count) do { \
    if ((count) > uiSourceLength - uiSource) { \
        ed_track_error(szError, uiErrorCapacity, \
                       "compacted stream ended inside an instruction"); \
        return ED_TRACK_LOAD_TRUNCATED; \
    } \
} while (0)
#define REQUIRE_OUTPUT(count) do { \
    if (!ed_track_output_available( \
            uiOutput, uiDestinationLength, (count))) { \
        ed_track_error(szError, uiErrorCapacity, \
                       "compacted instruction exceeds declared output size"); \
        return ED_TRACK_LOAD_OUTPUT_OVERFLOW; \
    } \
} while (0)
#define REQUIRE_HISTORY(count) do { \
    if (uiOutput < (count)) { \
        ed_track_error(szError, uiErrorCapacity, \
                       "compacted instruction references unavailable history"); \
        return ED_TRACK_LOAD_INVALID_BACK_REFERENCE; \
    } \
} while (0)

    for (;;) {
        uint8_t byCommand;

        REQUIRE_SOURCE(1);
        byCommand = pbySource[uiSource++];
        if (byCommand == 0)
            break;

        if (byCommand & 0x80u) {
            size_t uiDistance;
            size_t uiCount;

            if (byCommand & 0x40u) {
                REQUIRE_SOURCE(1);
                uiDistance = ((size_t)(byCommand
                             & ((byCommand & 0x20u) ? 0x1fu : 0x03u)) << 8)
                           + pbySource[uiSource++] + 3u;
                if (byCommand & 0x20u) {
                    REQUIRE_SOURCE(1);
                    uiCount = (size_t)pbySource[uiSource++] + 5u;
                } else {
                    uiCount = ((size_t)(byCommand >> 2) & 7u) + 4u;
                }
            } else {
                uiDistance = (size_t)(byCommand & 0x3fu) + 3u;
                uiCount = 3u;
            }
            REQUIRE_HISTORY(uiDistance);
            REQUIRE_OUTPUT(uiCount);
            for (size_t i = 0; i < uiCount; i++) {
                pbyDestination[uiOutput] =
                    pbyDestination[uiOutput - uiDistance];
                uiOutput++;
            }
        } else if (byCommand & 0x40u) {
            size_t uiCount;

            if (byCommand & 0x20u) {
                if (byCommand & 0x10u) {
                    int16_t nValue;

                    uiCount = (size_t)(byCommand & 0x0fu) + 2u;
                    REQUIRE_HISTORY(2);
                    REQUIRE_OUTPUT(uiCount * 2u);
                    nValue = ed_track_read_i16_le(
                        &pbyDestination[uiOutput - 2u]);
                    for (size_t i = 0; i < uiCount; i++) {
                        ed_track_write_i16_le(
                            &pbyDestination[uiOutput], nValue);
                        uiOutput += 2u;
                    }
                } else {
                    uint8_t byValue;

                    uiCount = (size_t)(byCommand & 0x0fu) + 3u;
                    REQUIRE_HISTORY(1);
                    REQUIRE_OUTPUT(uiCount);
                    byValue = pbyDestination[uiOutput - 1u];
                    memset(&pbyDestination[uiOutput], byValue, uiCount);
                    uiOutput += uiCount;
                }
            } else if (byCommand & 0x10u) {
                int16_t nCurrent;
                int16_t nPrevious;
                int iDifference;

                uiCount = (size_t)(byCommand & 0x0fu) + 2u;
                REQUIRE_HISTORY(4);
                REQUIRE_OUTPUT(uiCount * 2u);
                nCurrent = ed_track_read_i16_le(
                    &pbyDestination[uiOutput - 2u]);
                nPrevious = ed_track_read_i16_le(
                    &pbyDestination[uiOutput - 4u]);
                iDifference = (int)nCurrent - (int)nPrevious;
                for (size_t i = 0; i < uiCount; i++) {
                    nCurrent = (int16_t)((int)nCurrent + iDifference);
                    ed_track_write_i16_le(
                        &pbyDestination[uiOutput], nCurrent);
                    uiOutput += 2u;
                }
            } else {
                int iCurrent;
                int iPrevious;
                int iDifference;

                uiCount = (size_t)(byCommand & 0x0fu) + 3u;
                REQUIRE_HISTORY(2);
                REQUIRE_OUTPUT(uiCount);
                iCurrent = pbyDestination[uiOutput - 1u];
                iPrevious = pbyDestination[uiOutput - 2u];
                iDifference = iCurrent - iPrevious;
                for (size_t i = 0; i < uiCount; i++) {
                    iCurrent += iDifference;
                    pbyDestination[uiOutput++] = (uint8_t)iCurrent;
                }
            }
        } else {
            size_t uiCount = byCommand & 0x3fu;

            REQUIRE_SOURCE(uiCount);
            REQUIRE_OUTPUT(uiCount);
            memcpy(&pbyDestination[uiOutput],
                   &pbySource[uiSource], uiCount);
            uiSource += uiCount;
            uiOutput += uiCount;
        }
    }

#undef REQUIRE_HISTORY
#undef REQUIRE_OUTPUT
#undef REQUIRE_SOURCE

    if (uiOutput != uiDestinationLength) {
        ed_track_error(szError, uiErrorCapacity,
                       "compacted output does not match declared size");
        return ED_TRACK_LOAD_INVALID_SIZE;
    }
    *puiWritten = uiOutput;
    return ED_TRACK_LOAD_OK;
}

static eEdTrackLoadResult ed_track_stage_bytes(
    const uint8_t *pbyFile,
    size_t uiFileLength,
    tEdTrackStage *pStage,
    char *szError,
    size_t uiErrorCapacity)
{
    tEdTrackStage Staged;
    uint32_t uiDeclaredLength;
    eEdTrackLoadResult eResult;
    size_t uiWritten = 0;

    ed_track_stage_init(&Staged);
    if (uiFileLength < 4u) {
        ed_track_error(szError, uiErrorCapacity, "track header is truncated");
        return ED_TRACK_LOAD_TRUNCATED;
    }

    uiDeclaredLength = ed_track_read_u32_le(pbyFile);
    if ((uint16_t)uiDeclaredLength == 0x2020u) {
        uiDeclaredLength = (uint32_t)uiFileLength;
        Staged.pbyData = ed_track_alloc((size_t)uiDeclaredLength + 2u);
        if (!Staged.pbyData) {
            ed_track_error(szError, uiErrorCapacity,
                           "track staging allocation failed");
            return ED_TRACK_LOAD_OUT_OF_MEMORY;
        }
        memcpy(Staged.pbyData, pbyFile, uiFileLength);
        uiWritten = uiFileLength;
    } else {
        if (uiDeclaredLength == 0
                || uiDeclaredLength > ED_TRACK_MAX_DECOMPRESSED_SIZE) {
            ed_track_error(szError, uiErrorCapacity,
                           "declared track size is invalid");
            return ED_TRACK_LOAD_INVALID_SIZE;
        }
        Staged.pbyData = ed_track_alloc((size_t)uiDeclaredLength + 2u);
        if (!Staged.pbyData) {
            ed_track_error(szError, uiErrorCapacity,
                           "track staging allocation failed");
            return ED_TRACK_LOAD_OUT_OF_MEMORY;
        }
        eResult = ed_track_decode_compacted(
            pbyFile, uiFileLength, Staged.pbyData, uiDeclaredLength,
            &uiWritten, szError, uiErrorCapacity);
        if (eResult != ED_TRACK_LOAD_OK) {
            ed_track_stage_dispose(&Staged);
            return eResult;
        }
    }

    Staged.pbyData[uiWritten] = 0x1au;
    Staged.pbyData[uiWritten + 1u] = '\0';
    Staged.uiDataLength = uiWritten;
    Staged.ullContentHash = ed_track_hash(Staged.pbyData, uiWritten);
    ed_track_stage_dispose(pStage);
    *pStage = Staged;
    ed_track_error(szError, uiErrorCapacity, "");
    return ED_TRACK_LOAD_OK;
}

static int ed_track_next_line(tEdTrackTextReader *pReader,
                              const uint8_t **ppbyLine,
                              size_t *puiLength)
{
    while (pReader->uiOffset < pReader->uiLength) {
        size_t uiStart = pReader->uiOffset;
        size_t uiEnd;

        while (pReader->uiOffset < pReader->uiLength
                && pReader->pbyData[pReader->uiOffset] != '\r'
                && pReader->pbyData[pReader->uiOffset] != '\n'
                && pReader->pbyData[pReader->uiOffset] != 0x1au)
            pReader->uiOffset++;
        uiEnd = pReader->uiOffset;
        if (pReader->uiOffset < pReader->uiLength
                && pReader->pbyData[pReader->uiOffset] == 0x1au) {
            pReader->uiOffset = pReader->uiLength;
        } else {
            while (pReader->uiOffset < pReader->uiLength
                    && (pReader->pbyData[pReader->uiOffset] == '\r'
                        || pReader->pbyData[pReader->uiOffset] == '\n'))
                pReader->uiOffset++;
        }
        pReader->uiLine++;

        while (uiStart < uiEnd
                && (pReader->pbyData[uiStart] == ' '
                    || pReader->pbyData[uiStart] == '\t'))
            uiStart++;
        while (uiEnd > uiStart
                && (pReader->pbyData[uiEnd - 1u] == ' '
                    || pReader->pbyData[uiEnd - 1u] == '\t'))
            uiEnd--;
        if (uiStart == uiEnd)
            continue;
        if (pReader->pbyData[uiStart] == ';')
            continue;
        if (uiEnd - uiStart >= 2u
                && pReader->pbyData[uiStart] == '/'
                && pReader->pbyData[uiStart + 1u] == '/')
            continue;
        *ppbyLine = &pReader->pbyData[uiStart];
        *puiLength = uiEnd - uiStart;
        return 1;
    }
    return 0;
}

static int ed_track_parse_numeric_line(const uint8_t *pbyLine,
                                       size_t uiLength,
                                       size_t uiRequiredFields,
                                       long *plFirstValue)
{
    char szLine[1024];
    char *szCursor;
    size_t uiFields = 0;

    if (uiLength == 0 || uiLength >= sizeof(szLine))
        return 0;
    memcpy(szLine, pbyLine, uiLength);
    szLine[uiLength] = '\0';
    szCursor = szLine;
    while (*szCursor) {
        char *szEnd;
        double dValue;

        while (*szCursor == ' ' || *szCursor == '\t'
                || *szCursor == ',')
            szCursor++;
        if (!*szCursor || *szCursor == ';'
                || (szCursor[0] == '/' && szCursor[1] == '/'))
            break;
        errno = 0;
        dValue = strtod(szCursor, &szEnd);
        (void)dValue;
        if (szEnd == szCursor || errno == ERANGE)
            return 0;
        if (*szEnd && *szEnd != ' ' && *szEnd != '\t'
                && *szEnd != ',' && *szEnd != ';'
                && !(szEnd[0] == '/' && szEnd[1] == '/'))
            return 0;
        if (uiFields == 0 && plFirstValue) {
            char *szIntegerEnd;
            errno = 0;
            *plFirstValue = strtol(szCursor, &szIntegerEnd, 10);
            if (errno == ERANGE || szIntegerEnd != szEnd)
                return 0;
        }
        uiFields++;
        szCursor = szEnd;
    }
    return uiFields >= uiRequiredFields;
}

static int ed_track_copy_asset_name(const uint8_t *pbyLine,
                                    size_t uiLength,
                                    const char *szPrefix,
                                    char szDestination[
                                        ED_TRACK_ASSET_NAME_CAPACITY])
{
    size_t uiPrefixLength = strlen(szPrefix);
    size_t uiColon;
    size_t uiNameStart;
    size_t uiNameLength;

    if (uiLength <= uiPrefixLength + 1u
            || memcmp(pbyLine, szPrefix, uiPrefixLength) != 0)
        return 0;
    uiColon = uiPrefixLength;
    while (uiColon < uiLength && pbyLine[uiColon] != ':')
        uiColon++;
    if (uiColon == uiLength)
        return 0;
    uiNameStart = uiColon + 1u;
    while (uiNameStart < uiLength
            && (pbyLine[uiNameStart] == ' '
                || pbyLine[uiNameStart] == '\t'))
        uiNameStart++;
    uiNameLength = uiLength - uiNameStart;
    while (uiNameLength > 0
            && (pbyLine[uiNameStart + uiNameLength - 1u] == ' '
                || pbyLine[uiNameStart + uiNameLength - 1u] == '\t'))
        uiNameLength--;
    if (uiNameLength == 0
            || uiNameLength >= ED_TRACK_ASSET_NAME_CAPACITY)
        return -1;
    for (size_t i = 0; i < uiNameLength; i++) {
        uint8_t byCharacter = pbyLine[uiNameStart + i];

        if (byCharacter < 0x21u || byCharacter > 0x7eu
                || byCharacter == '/' || byCharacter == '\\'
                || byCharacter == ':')
            return -1;
        if (byCharacter >= 'a' && byCharacter <= 'z')
            byCharacter = (uint8_t)(byCharacter - 'a' + 'A');
        szDestination[i] = (char)byCharacter;
    }
    szDestination[uiNameLength] = '\0';
    return 1;
}

static eEdTrackLoadResult ed_track_validate_text(
    tEdTrackStage *pStage,
    char *szError,
    size_t uiErrorCapacity)
{
    tEdTrackTextReader Reader = {
        .pbyData = pStage->pbyData,
        .uiLength = pStage->uiDataLength
    };
    const uint8_t *pbyLine;
    size_t uiLineLength;
    long lChunkCount;

    if (!ed_track_next_line(&Reader, &pbyLine, &uiLineLength)
            || !ed_track_parse_numeric_line(
                pbyLine, uiLineLength, 4u, &lChunkCount)
            || lChunkCount <= 0
            || lChunkCount > (long)ED_TRACK_MAX_CHUNKS) {
        ed_track_error_line(szError, uiErrorCapacity, Reader.uiLine,
                            "invalid track header");
        return ED_TRACK_LOAD_MALFORMED_TEXT;
    }
    pStage->uiChunkCount = (uint32_t)lChunkCount;

    for (uint32_t iChunk = 0; iChunk < pStage->uiChunkCount; iChunk++) {
        static const size_t auiRequiredFields[3] = { 22u, 18u, 30u };

        for (size_t iRecord = 0; iRecord < 3u; iRecord++) {
            if (!ed_track_next_line(&Reader, &pbyLine, &uiLineLength)
                    || !ed_track_parse_numeric_line(
                        pbyLine, uiLineLength,
                        auiRequiredFields[iRecord], NULL)) {
                char szMessage[96];
                snprintf(szMessage, sizeof(szMessage),
                         "malformed chunk %u record %u",
                         iChunk, (unsigned int)iRecord);
                ed_track_error_line(
                    szError, uiErrorCapacity, Reader.uiLine, szMessage);
                return ED_TRACK_LOAD_MALFORMED_TEXT;
            }
        }
    }

    memset(pStage->szTextureFile, 0, sizeof(pStage->szTextureFile));
    memset(pStage->szBuildingTextureFile, 0,
           sizeof(pStage->szBuildingTextureFile));
    while (ed_track_next_line(&Reader, &pbyLine, &uiLineLength)) {
        int iCopyResult;

        if (!pStage->szTextureFile[0]) {
            iCopyResult = ed_track_copy_asset_name(
                pbyLine, uiLineLength, "T", pStage->szTextureFile);
            if (iCopyResult < 0) {
                ed_track_error_line(
                    szError, uiErrorCapacity, Reader.uiLine,
                    "invalid track texture filename");
                return ED_TRACK_LOAD_MALFORMED_TEXT;
            }
        }
        if (!pStage->szBuildingTextureFile[0]) {
            iCopyResult = ed_track_copy_asset_name(
                pbyLine, uiLineLength, "BLD",
                pStage->szBuildingTextureFile);
            if (iCopyResult < 0) {
                ed_track_error_line(
                    szError, uiErrorCapacity, Reader.uiLine,
                    "invalid building texture filename");
                return ED_TRACK_LOAD_MALFORMED_TEXT;
            }
        }
    }
    if (!pStage->szTextureFile[0]) {
        ed_track_error(szError, uiErrorCapacity,
                       "track texture declaration is missing");
        return ED_TRACK_LOAD_MALFORMED_TEXT;
    }
    if (!pStage->szBuildingTextureFile[0])
        snprintf(pStage->szBuildingTextureFile,
                 sizeof(pStage->szBuildingTextureFile), "BUILDING.DRH");
    return ED_TRACK_LOAD_OK;
}

void ed_track_stage_init(tEdTrackStage *pStage)
{
    if (pStage)
        memset(pStage, 0, sizeof(*pStage));
}

void ed_track_stage_dispose(tEdTrackStage *pStage)
{
    if (!pStage)
        return;
    ed_track_free(pStage->pbyData);
    memset(pStage, 0, sizeof(*pStage));
}

eEdTrackLoadResult ed_compacted_file_stage(
    const char *szPath,
    tEdTrackStage *pStage,
    char *szError,
    size_t uiErrorCapacity)
{
    uint8_t *pbyFile;
    size_t uiFileLength;
    eEdTrackLoadResult eResult;

    if (!szPath || !pStage) {
        ed_track_error(szError, uiErrorCapacity, "invalid staging argument");
        return ED_TRACK_LOAD_INVALID_ARGUMENT;
    }
    eResult = ed_track_read_file(
        szPath, &pbyFile, &uiFileLength, szError, uiErrorCapacity);
    if (eResult != ED_TRACK_LOAD_OK)
        return eResult;
    eResult = ed_track_stage_bytes(
        pbyFile, uiFileLength, pStage, szError, uiErrorCapacity);
    ed_track_free(pbyFile);
    return eResult;
}

eEdTrackLoadResult ed_track_file_stage(
    const char *szPath,
    tEdTrackStage *pStage,
    char *szError,
    size_t uiErrorCapacity)
{
    tEdTrackStage Staged;
    eEdTrackLoadResult eResult;

    if (!szPath || !pStage) {
        ed_track_error(szError, uiErrorCapacity, "invalid staging argument");
        return ED_TRACK_LOAD_INVALID_ARGUMENT;
    }
    ed_track_stage_init(&Staged);
    eResult = ed_compacted_file_stage(
        szPath, &Staged, szError, uiErrorCapacity);
    if (eResult != ED_TRACK_LOAD_OK)
        return eResult;
    eResult = ed_track_validate_text(&Staged, szError, uiErrorCapacity);
    if (eResult != ED_TRACK_LOAD_OK) {
        ed_track_stage_dispose(&Staged);
        return eResult;
    }
    ed_track_stage_dispose(pStage);
    *pStage = Staged;
    ed_track_error(szError, uiErrorCapacity, "");
    return ED_TRACK_LOAD_OK;
}

void ed_track_document_init(tEdTrackDocument *pDocument)
{
    if (pDocument)
        memset(pDocument, 0, sizeof(*pDocument));
}

void ed_track_document_dispose(tEdTrackDocument *pDocument)
{
    if (!pDocument)
        return;
    ed_track_stage_dispose(&pDocument->Committed);
    pDocument->uiGeneration = 0;
}

eEdTrackLoadResult ed_track_document_reload(
    tEdTrackDocument *pDocument,
    const char *szPath,
    char *szError,
    size_t uiErrorCapacity)
{
    tEdTrackStage Staged;
    eEdTrackLoadResult eResult;

    if (!pDocument || !szPath) {
        ed_track_error(szError, uiErrorCapacity, "invalid reload argument");
        return ED_TRACK_LOAD_INVALID_ARGUMENT;
    }
    ed_track_stage_init(&Staged);
    eResult = ed_track_file_stage(
        szPath, &Staged, szError, uiErrorCapacity);
    if (eResult != ED_TRACK_LOAD_OK)
        return eResult;
    ed_track_stage_dispose(&pDocument->Committed);
    pDocument->Committed = Staged;
    pDocument->uiGeneration++;
    return ED_TRACK_LOAD_OK;
}

const char *ed_track_load_result_name(eEdTrackLoadResult eResult)
{
    static const char *const aszNames[] = {
        "ok",
        "invalid argument",
        "I/O failed",
        "truncated",
        "invalid size",
        "invalid back-reference",
        "output overflow",
        "malformed text",
        "out of memory"
    };

    if ((unsigned int)eResult
            >= sizeof(aszNames) / sizeof(aszNames[0]))
        return "unknown";
    return aszNames[eResult];
}

size_t ed_track_loader_live_allocations(void)
{
    return g_uiLiveAllocations;
}

size_t ed_track_loader_live_bytes(void)
{
    return g_uiLiveBytes;
}
